//
//  memmgr.c
//  memmgr
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ARGC_ERROR 1
#define FILE_ERROR 2
#define BUFLEN 256
#define FRAME_SIZE  256

char main_mem[65536];
char main_mem_fifo[32768]; // 128 physical frames
int page_queue[128];
int qhead = 0, qtail = 0;
int tlb[16][2];
int current_tlb_entry = 0;
int page_table[256];
int current_frame = 0;
FILE* fstore;

// data for statistics
int pfc[5], pfc2[5]; // page fault count
int tlbh[5], tlbh2[5]; // tlb hit count
int count[5], count2[5]; // access count

#define PAGES 256
#define FRAMES_PART1 256
#define FRAMES_PART2 128

//-------------------------------------------------------------------
unsigned getpage(unsigned x) { return (0xff00 & x) >> 8; }

unsigned getoffset(unsigned x) { return (0xff & x); }

void getpage_offset(unsigned x) {
  unsigned  page   = getpage(x);
  unsigned  offset = getoffset(x);
  printf("x is: %u, page: %u, offset: %u, address: %u, paddress: %u\n", x, page, offset,
         (page << 8) | getoffset(x), page * 256 + offset);
}

int tlb_contains(unsigned x) {  // TODO:
  for(int i = 0; i < 16; i++){
    if (tlb[i][0] == x){
      return i;
    }
  }
  return -1;
}

void update_tlb(unsigned page) {  // TODO:
  tlb[current_tlb_entry][0] = page;
  tlb[current_tlb_entry][1] = page_table[page];
  current_tlb_entry = (current_tlb_entry + 1) % 16;
}

unsigned getframe(FILE* fstore, unsigned logic_add, unsigned page,
         int *page_fault_count, int *tlb_hit_count) {              // TODO
  // tlb hit
  int count = tlb_contains(page);
  if (count != -1){
    (*tlb_hit_count)++;
    return tlb[count][1];
  }
  
  // tlb miss
  // if page table hit
  if (page_table[page] != -1){
    update_tlb(page);
    return page_table[page];
  }
  
  // page table miss -> page fault
  // find page location in backing_store
  int offset = (logic_add / FRAME_SIZE) * FRAME_SIZE;
  fseek(fstore, offset, 0);
  page_table[page] = current_frame;
  current_frame = (current_frame + 1) % 256;
  (*page_fault_count)++;
  // bring data into memory, update tlb and page table
  fread(&main_mem[page_table[page] * FRAME_SIZE], sizeof(char), 256, fstore);
  update_tlb(page);
  return page_table[page];
}

int get_available_frame(unsigned page) {    // TODO
  // empty queue
   if (qhead == 0 && qtail == 0 && page_queue[qhead] == -1) {
    ++qtail;
    page_queue[qhead] = page;
    return qhead;
  }
  // queue not full
  if (page_queue[qtail] == -1) {
    page_queue[qtail] = page;
    int temp = qtail;
    qtail = (qtail + 1) % 128;
    return temp;
  }
  // queue full
  if (qhead == qtail && page_queue[qtail] != -1) {
      int temp = qhead;
      page_queue[qhead] = page;
      qhead = (qhead + 1) % 128;
      qtail = (qtail + 1) % 128;
      return temp;
    }
  return -1;   // failed to find a value
}

unsigned getframe_fifo(FILE* fstore, unsigned logic_add, unsigned page,
         int *page_fault_count, int *tlb_hit_count) {
  // tlb hit
  int count = tlb_contains(page);
    if (count != -1 && page_queue[tlb[count][1]] == page) {
      (*tlb_hit_count)++;
      return tlb[count][1];
    }


  // tlb miss, page table hit
  if (page_table[page] != -1 && page_queue[page_table[page]] == page){
    update_tlb(page);
    return page_table[page];
  }
  
  
  // page table miss -> page fault
  // find location in backing_store
  int offset = (logic_add / FRAME_SIZE) * FRAME_SIZE;
  fseek(fstore, offset, 0); 
  int available = get_available_frame(page); 
  page_table[page] = available;
  (*page_fault_count)++;
  // bring data into memory, update tlb and page table
  fread(&main_mem_fifo[available * FRAME_SIZE], sizeof(char), 256, fstore); 
  update_tlb(page);
  return page_table[page];
}

void open_files(FILE** fadd, FILE** fcorr, FILE** fstore) {
  *fadd = fopen("addresses.txt", "r");    // open file addresses.txt  (contains the logical addresses)
  if (*fadd ==  NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR);  }

  *fcorr = fopen("correct.txt", "r");     // contains the logical and physical address, and its value
  if (*fcorr ==  NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR);  }

  *fstore = fopen("BACKING_STORE.bin", "rb");
  if (*fstore ==  NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR);  }
}


void close_files(FILE* fadd, FILE* fcorr, FILE* fstore) {
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
}


void simulate_pages_frames_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt


  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);
  
  // Initialize page table, tlb
  memset(page_table, -1, sizeof(page_table));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  current_frame = 0;
  current_tlb_entry = 0;
  
  printf("\n Starting nPages == nFrames memory simulation...\n");

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    frame = getframe(fstore, logic_add, page, &page_fault_count, &tlb_hit_count);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem[physical_add]);

    // update tlb hit count and page fault count every 200 accesses
    if (access_count > 0 && access_count % 200 == 0){
      tlbh[(access_count / 200) - 1] = tlb_hit_count;
      pfc[(access_count / 200) - 1] = page_fault_count;
      count[(access_count / 200) - 1] = access_count;
    }
    
    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    assert(physical_add ==  phys_add);
    assert(value ==  val);
  }
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
  
  printf("ALL logical ---> physical assertions PASSED!\n");
  printf("ALL read memory value assertions PASSED!\n");

  printf("\n\t\t... nPages == nFrames memory simulation done.\n");
}


void simulate_pages_frames_not_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt

  printf("\n Starting nPages != nFrames memory simulation...\n");

  // Initialize page table, tlb, page queue
  memset(page_table, -1, sizeof(page_table));
  memset(page_queue, -1, sizeof(page_queue));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  qhead = 0; qtail = 0;

  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    frame = getframe_fifo(fstore, logic_add, page, &page_fault_count, &tlb_hit_count);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem_fifo[physical_add]);

    // update tlb hit count and page fault count every 200 accesses
    if (access_count > 0 && access_count%200 == 0){
      tlbh2[(access_count / 200) - 1] = tlb_hit_count;
      pfc2[(access_count / 200) - 1] = page_fault_count;
      count2[(access_count / 200) - 1] = access_count;
    }
    
    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    assert(value ==  val);
  }
  close_files(fadd, fcorr, fstore);

  printf("ALL read memory value assertions PASSED!\n");
  printf("\n\t\t... nPages != nFrames memory simulation done.\n");
}


int main(int argc, const char* argv[]) {
  // initialize statistics data
  for (int i = 0; i < 5; ++i){
    pfc[i] = pfc2[i] = tlbh[i]  = tlbh2[i] = count[i] = count2[i] = 0;
  }

  simulate_pages_frames_equal(); // 256 physical frames
  simulate_pages_frames_not_equal(); // 128 physical frames

  // Statistics
  printf("\n\nnPages == nFrames Statistics (256 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count[i], tlbh[i], pfc[i],
           1.0f * tlbh[i] / count[i], 1.0f * pfc[i] / count[i]);
  }

  printf("\nnPages != nFrames Statistics (128 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count2[i], tlbh2[i], pfc2[i],
           1.0f * tlbh2[i] / count2[i], 1.0f * pfc2[i] / count2[i]);
  }
  printf("\n\t\t...memory management simulation completed!\n");

  return 0;
}
/*
 Starting nPages == nFrames memory simulation...
logical: 16916 (page:  66, offset:  20) ---> physical:    20 -> value:    0  ok
logical: 62493 (page: 244, offset:  29) ---> physical:   285 -> value:    0  ok
logical: 30198 (page: 117, offset: 246) ---> physical:   758 -> value:   29  ok
logical: 53683 (page: 209, offset: 179) ---> physical:   947 -> value:  108  ok
logical: 40185 (page: 156, offset: 249) ---> physical:  1273 -> value:    0  ok

logical: 28781 (page: 112, offset: 109) ---> physical:  1389 -> value:    0  ok
logical: 24462 (page:  95, offset: 142) ---> physical:  1678 -> value:   23  ok
logical: 48399 (page: 189, offset:  15) ---> physical:  1807 -> value:   67  ok
logical: 64815 (page: 253, offset:  47) ---> physical:  2095 -> value:   75  ok
logical: 18295 (page:  71, offset: 119) ---> physical:  2423 -> value:  -35  ok

logical: 12218 (page:  47, offset: 186) ---> physical:  2746 -> value:   11  ok
logical: 22760 (page:  88, offset: 232) ---> physical:  3048 -> value:    0  ok
logical: 57982 (page: 226, offset: 126) ---> physical:  3198 -> value:   56  ok
logical: 27966 (page: 109, offset:  62) ---> physical:  3390 -> value:   27  ok
logical: 54894 (page: 214, offset: 110) ---> physical:  3694 -> value:   53  ok

logical: 38929 (page: 152, offset:  17) ---> physical:  3857 -> value:    0  ok
logical: 32865 (page: 128, offset:  97) ---> physical:  4193 -> value:    0  ok
logical: 64243 (page: 250, offset: 243) ---> physical:  4595 -> value:  -68  ok
logical:  2315 (page:   9, offset:  11) ---> physical:  4619 -> value:   66  ok
logical: 64454 (page: 251, offset: 198) ---> physical:  5062 -> value:   62  ok

logical: 55041 (page: 215, offset:   1) ---> physical:  5121 -> value:    0  ok
logical: 18633 (page:  72, offset: 201) ---> physical:  5577 -> value:    0  ok
logical: 14557 (page:  56, offset: 221) ---> physical:  5853 -> value:    0  ok
logical: 61006 (page: 238, offset:  78) ---> physical:  5966 -> value:   59  ok
logical: 62615 (page: 244, offset: 151) ---> physical:   407 -> value:   37  ok

logical:  7591 (page:  29, offset: 167) ---> physical:  6311 -> value:  105  ok
logical: 64747 (page: 252, offset: 235) ---> physical:  6635 -> value:   58  ok
logical:  6727 (page:  26, offset:  71) ---> physical:  6727 -> value: -111  ok
logical: 32315 (page: 126, offset:  59) ---> physical:  6971 -> value: -114  ok
logical: 60645 (page: 236, offset: 229) ---> physical:  7397 -> value:    0  ok

logical:  6308 (page:  24, offset: 164) ---> physical:  7588 -> value:    0  ok
logical: 45688 (page: 178, offset: 120) ---> physical:  7800 -> value:    0  ok
logical:   969 (page:   3, offset: 201) ---> physical:  8137 -> value:    0  ok
logical: 40891 (page: 159, offset: 187) ---> physical:  8379 -> value:  -18  ok
logical: 49294 (page: 192, offset: 142) ---> physical:  8590 -> value:   48  ok

logical: 41118 (page: 160, offset: 158) ---> physical:  8862 -> value:   40  ok
logical: 21395 (page:  83, offset: 147) ---> physical:  9107 -> value:  -28  ok
logical:  6091 (page:  23, offset: 203) ---> physical:  9419 -> value:  -14  ok
logical: 32541 (page: 127, offset:  29) ---> physical:  9501 -> value:    0  ok
logical: 17665 (page:  69, offset:   1) ---> physical:  9729 -> value:    0  ok

logical:  3784 (page:  14, offset: 200) ---> physical: 10184 -> value:    0  ok
logical: 28718 (page: 112, offset:  46) ---> physical:  1326 -> value:   28  ok
logical: 59240 (page: 231, offset: 104) ---> physical: 10344 -> value:    0  ok
logical: 40178 (page: 156, offset: 242) ---> physical:  1266 -> value:   39  ok
logical: 60086 (page: 234, offset: 182) ---> physical: 10678 -> value:   58  ok

logical: 42252 (page: 165, offset:  12) ---> physical: 10764 -> value:    0  ok
logical: 44770 (page: 174, offset: 226) ---> physical: 11234 -> value:   43  ok
logical: 22514 (page:  87, offset: 242) ---> physical: 11506 -> value:   21  ok
logical:  3067 (page:  11, offset: 251) ---> physical: 11771 -> value:   -2  ok
logical: 15757 (page:  61, offset: 141) ---> physical: 11917 -> value:    0  ok

logical: 31649 (page: 123, offset: 161) ---> physical: 12193 -> value:    0  ok
logical: 10842 (page:  42, offset:  90) ---> physical: 12378 -> value:   10  ok
logical: 43765 (page: 170, offset: 245) ---> physical: 12789 -> value:    0  ok
logical: 33405 (page: 130, offset: 125) ---> physical: 12925 -> value:    0  ok
logical: 44954 (page: 175, offset: 154) ---> physical: 13210 -> value:   43  ok

logical: 56657 (page: 221, offset:  81) ---> physical: 13393 -> value:    0  ok
logical:  5003 (page:  19, offset: 139) ---> physical: 13707 -> value:  -30  ok
logical: 50227 (page: 196, offset:  51) ---> physical: 13875 -> value:   12  ok
logical: 19358 (page:  75, offset: 158) ---> physical: 14238 -> value:   18  ok
logical: 36529 (page: 142, offset: 177) ---> physical: 14513 -> value:    0  ok

logical: 10392 (page:  40, offset: 152) ---> physical: 14744 -> value:    0  ok
logical: 58882 (page: 230, offset:   2) ---> physical: 14850 -> value:   57  ok
logical:  5129 (page:  20, offset:   9) ---> physical: 15113 -> value:    0  ok
logical: 58554 (page: 228, offset: 186) ---> physical: 15546 -> value:   57  ok
logical: 58584 (page: 228, offset: 216) ---> physical: 15576 -> value:    0  ok

logical: 27444 (page: 107, offset:  52) ---> physical: 15668 -> value:    0  ok
logical: 58982 (page: 230, offset: 102) ---> physical: 14950 -> value:   57  ok
logical: 51476 (page: 201, offset:  20) ---> physical: 15892 -> value:    0  ok
logical:  6796 (page:  26, offset: 140) ---> physical:  6796 -> value:    0  ok
logical: 21311 (page:  83, offset:  63) ---> physical:  9023 -> value:  -49  ok

logical: 30705 (page: 119, offset: 241) ---> physical: 16369 -> value:    0  ok
logical: 28964 (page: 113, offset:  36) ---> physical: 16420 -> value:    0  ok
logical: 41003 (page: 160, offset:  43) ---> physical:  8747 -> value:   10  ok
logical: 20259 (page:  79, offset:  35) ---> physical: 16675 -> value:  -56  ok
logical: 57857 (page: 226, offset:   1) ---> physical:  3073 -> value:    0  ok

logical: 63258 (page: 247, offset:  26) ---> physical: 16922 -> value:   61  ok
logical: 36374 (page: 142, offset:  22) ---> physical: 14358 -> value:   35  ok
logical:   692 (page:   2, offset: 180) ---> physical: 17332 -> value:    0  ok
logical: 43121 (page: 168, offset: 113) ---> physical: 17521 -> value:    0  ok
logical: 48128 (page: 188, offset:   0) ---> physical: 17664 -> value:    0  ok

logical: 34561 (page: 135, offset:   1) ---> physical: 17921 -> value:    0  ok
logical: 49213 (page: 192, offset:  61) ---> physical:  8509 -> value:    0  ok
logical: 36922 (page: 144, offset:  58) ---> physical: 18234 -> value:   36  ok
logical: 59162 (page: 231, offset:  26) ---> physical: 10266 -> value:   57  ok
logical: 50552 (page: 197, offset: 120) ---> physical: 18552 -> value:    0  ok

logical: 17866 (page:  69, offset: 202) ---> physical:  9930 -> value:   17  ok
logical: 18145 (page:  70, offset: 225) ---> physical: 18913 -> value:    0  ok
logical:  3884 (page:  15, offset:  44) ---> physical: 18988 -> value:    0  ok
logical: 54388 (page: 212, offset: 116) ---> physical: 19316 -> value:    0  ok
logical: 42932 (page: 167, offset: 180) ---> physical: 19636 -> value:    0  ok

logical: 46919 (page: 183, offset:  71) ---> physical: 19783 -> value:  -47  ok
logical: 58892 (page: 230, offset:  12) ---> physical: 14860 -> value:    0  ok
logical:  8620 (page:  33, offset: 172) ---> physical: 20140 -> value:    0  ok
logical: 38336 (page: 149, offset: 192) ---> physical: 20416 -> value:    0  ok
logical: 64357 (page: 251, offset: 101) ---> physical:  4965 -> value:    0  ok

logical: 23387 (page:  91, offset:  91) ---> physical: 20571 -> value:  -42  ok
logical: 42632 (page: 166, offset: 136) ---> physical: 20872 -> value:    0  ok
logical: 15913 (page:  62, offset:  41) ---> physical: 21033 -> value:    0  ok
logical: 15679 (page:  61, offset:  63) ---> physical: 11839 -> value:   79  ok
logical: 22501 (page:  87, offset: 229) ---> physical: 11493 -> value:    0  ok

logical: 37540 (page: 146, offset: 164) ---> physical: 21412 -> value:    0  ok
logical:  5527 (page:  21, offset: 151) ---> physical: 21655 -> value:  101  ok
logical: 63921 (page: 249, offset: 177) ---> physical: 21937 -> value:    0  ok
logical: 62716 (page: 244, offset: 252) ---> physical:   508 -> value:    0  ok
logical: 32874 (page: 128, offset: 106) ---> physical:  4202 -> value:   32  ok

logical: 64390 (page: 251, offset: 134) ---> physical:  4998 -> value:   62  ok
logical: 63101 (page: 246, offset: 125) ---> physical: 22141 -> value:    0  ok
logical: 61802 (page: 241, offset: 106) ---> physical: 22378 -> value:   60  ok
logical: 19648 (page:  76, offset: 192) ---> physical: 22720 -> value:    0  ok
logical: 29031 (page: 113, offset: 103) ---> physical: 16487 -> value:   89  ok

logical: 44981 (page: 175, offset: 181) ---> physical: 13237 -> value:    0  ok
logical: 28092 (page: 109, offset: 188) ---> physical:  3516 -> value:    0  ok
logical:  9448 (page:  36, offset: 232) ---> physical: 23016 -> value:    0  ok
logical: 44744 (page: 174, offset: 200) ---> physical: 11208 -> value:    0  ok
logical: 61496 (page: 240, offset:  56) ---> physical: 23096 -> value:    0  ok

logical: 31453 (page: 122, offset: 221) ---> physical: 23517 -> value:    0  ok
logical: 60746 (page: 237, offset:  74) ---> physical: 23626 -> value:   59  ok
logical: 12199 (page:  47, offset: 167) ---> physical:  2727 -> value:  -23  ok
logical: 62255 (page: 243, offset:  47) ---> physical: 23855 -> value:  -53  ok
logical: 21793 (page:  85, offset:  33) ---> physical: 24097 -> value:    0  ok

logical: 26544 (page: 103, offset: 176) ---> physical: 24496 -> value:    0  ok
logical: 14964 (page:  58, offset: 116) ---> physical: 24692 -> value:    0  ok
logical: 41462 (page: 161, offset: 246) ---> physical: 25078 -> value:   40  ok
logical: 56089 (page: 219, offset:  25) ---> physical: 25113 -> value:    0  ok
logical: 52038 (page: 203, offset:  70) ---> physical: 25414 -> value:   50  ok

logical: 47982 (page: 187, offset: 110) ---> physical: 25710 -> value:   46  ok
logical: 59484 (page: 232, offset:  92) ---> physical: 25948 -> value:    0  ok
logical: 50924 (page: 198, offset: 236) ---> physical: 26348 -> value:    0  ok
logical:  6942 (page:  27, offset:  30) ---> physical: 26398 -> value:    6  ok
logical: 34998 (page: 136, offset: 182) ---> physical: 26806 -> value:   34  ok

logical: 27069 (page: 105, offset: 189) ---> physical: 27069 -> value:    0  ok
logical: 51926 (page: 202, offset: 214) ---> physical: 27350 -> value:   50  ok
logical: 60645 (page: 236, offset: 229) ---> physical:  7397 -> value:    0  ok
logical: 43181 (page: 168, offset: 173) ---> physical: 17581 -> value:    0  ok
logical: 10559 (page:  41, offset:  63) ---> physical: 27455 -> value:   79  ok

logical:  4664 (page:  18, offset:  56) ---> physical: 27704 -> value:    0  ok
logical: 28578 (page: 111, offset: 162) ---> physical: 28066 -> value:   27  ok
logical: 59516 (page: 232, offset: 124) ---> physical: 25980 -> value:    0  ok
logical: 38912 (page: 152, offset:   0) ---> physical:  3840 -> value:    0  ok
logical: 63562 (page: 248, offset:  74) ---> physical: 28234 -> value:   62  ok

logical: 64846 (page: 253, offset:  78) ---> physical:  2126 -> value:   63  ok
logical: 62938 (page: 245, offset: 218) ---> physical: 28634 -> value:   61  ok
logical: 27194 (page: 106, offset:  58) ---> physical: 28730 -> value:   26  ok
logical: 28804 (page: 112, offset: 132) ---> physical:  1412 -> value:    0  ok
logical: 61703 (page: 241, offset:   7) ---> physical: 22279 -> value:   65  ok

logical: 10998 (page:  42, offset: 246) ---> physical: 12534 -> value:   10  ok
logical:  6596 (page:  25, offset: 196) ---> physical: 29124 -> value:    0  ok
logical: 37721 (page: 147, offset:  89) ---> physical: 29273 -> value:    0  ok
logical: 43430 (page: 169, offset: 166) ---> physical: 29606 -> value:   42  ok
logical: 22692 (page:  88, offset: 164) ---> physical:  2980 -> value:    0  ok

logical: 62971 (page: 245, offset: 251) ---> physical: 28667 -> value:  126  ok
logical: 47125 (page: 184, offset:  21) ---> physical: 29717 -> value:    0  ok
logical: 52521 (page: 205, offset:  41) ---> physical: 29993 -> value:    0  ok
logical: 34646 (page: 135, offset:  86) ---> physical: 18006 -> value:   33  ok
logical: 32889 (page: 128, offset: 121) ---> physical:  4217 -> value:    0  ok

logical: 13055 (page:  50, offset: 255) ---> physical: 30463 -> value:  -65  ok
logical: 65416 (page: 255, offset: 136) ---> physical: 30600 -> value:    0  ok
logical: 62869 (page: 245, offset: 149) ---> physical: 28565 -> value:    0  ok
logical: 57314 (page: 223, offset: 226) ---> physical: 30946 -> value:   55  ok
logical: 12659 (page:  49, offset: 115) ---> physical: 31091 -> value:   92  ok

logical: 14052 (page:  54, offset: 228) ---> physical: 31460 -> value:    0  ok
logical: 32956 (page: 128, offset: 188) ---> physical:  4284 -> value:    0  ok
logical: 49273 (page: 192, offset: 121) ---> physical:  8569 -> value:    0  ok
logical: 50352 (page: 196, offset: 176) ---> physical: 14000 -> value:    0  ok
logical: 49737 (page: 194, offset:  73) ---> physical: 31561 -> value:    0  ok

logical: 15555 (page:  60, offset: 195) ---> physical: 31939 -> value:   48  ok
logical: 47475 (page: 185, offset: 115) ---> physical: 32115 -> value:   92  ok
logical: 15328 (page:  59, offset: 224) ---> physical: 32480 -> value:    0  ok
logical: 34621 (page: 135, offset:  61) ---> physical: 17981 -> value:    0  ok
logical: 51365 (page: 200, offset: 165) ---> physical: 32677 -> value:    0  ok

logical: 32820 (page: 128, offset:  52) ---> physical:  4148 -> value:    0  ok
logical: 48855 (page: 190, offset: 215) ---> physical: 32983 -> value:  -75  ok
logical: 12224 (page:  47, offset: 192) ---> physical:  2752 -> value:    0  ok
logical:  2035 (page:   7, offset: 243) ---> physical: 33267 -> value:   -4  ok
logical: 60539 (page: 236, offset: 123) ---> physical:  7291 -> value:   30  ok

logical: 14595 (page:  57, offset:   3) ---> physical: 33283 -> value:   64  ok
logical: 13853 (page:  54, offset:  29) ---> physical: 31261 -> value:    0  ok
logical: 24143 (page:  94, offset:  79) ---> physical: 33615 -> value: -109  ok
logical: 15216 (page:  59, offset: 112) ---> physical: 32368 -> value:    0  ok
logical:  8113 (page:  31, offset: 177) ---> physical: 33969 -> value:    0  ok

logical: 22640 (page:  88, offset: 112) ---> physical:  2928 -> value:    0  ok
logical: 32978 (page: 128, offset: 210) ---> physical:  4306 -> value:   32  ok
logical: 39151 (page: 152, offset: 239) ---> physical:  4079 -> value:   59  ok
logical: 19520 (page:  76, offset:  64) ---> physical: 22592 -> value:    0  ok
logical: 58141 (page: 227, offset:  29) ---> physical: 34077 -> value:    0  ok

logical: 63959 (page: 249, offset: 215) ---> physical: 21975 -> value:  117  ok
logical: 53040 (page: 207, offset:  48) ---> physical: 34352 -> value:    0  ok
logical: 55842 (page: 218, offset:  34) ---> physical: 34594 -> value:   54  ok
logical:   585 (page:   2, offset:  73) ---> physical: 17225 -> value:    0  ok
logical: 51229 (page: 200, offset:  29) ---> physical: 32541 -> value:    0  ok

logical: 64181 (page: 250, offset: 181) ---> physical:  4533 -> value:    0  ok
logical: 54879 (page: 214, offset:  95) ---> physical:  3679 -> value: -105  ok
logical: 28210 (page: 110, offset:  50) ---> physical: 34866 -> value:   27  ok
logical: 10268 (page:  40, offset:  28) ---> physical: 14620 -> value:    0  ok
logical: 15395 (page:  60, offset:  35) ---> physical: 31779 -> value:    8  ok

logical: 12884 (page:  50, offset:  84) ---> physical: 30292 -> value:    0  ok
logical:  2149 (page:   8, offset: 101) ---> physical: 35173 -> value:    0  ok
logical: 53483 (page: 208, offset: 235) ---> physical: 35563 -> value:   58  ok
logical: 59606 (page: 232, offset: 214) ---> physical: 26070 -> value:   58  ok
logical: 14981 (page:  58, offset: 133) ---> physical: 24709 -> value:    0  ok

logical: 36672 (page: 143, offset:  64) ---> physical: 35648 -> value:    0  ok
logical: 23197 (page:  90, offset: 157) ---> physical: 35997 -> value:    0  ok
logical: 36518 (page: 142, offset: 166) ---> physical: 14502 -> value:   35  ok
logical: 13361 (page:  52, offset:  49) ---> physical: 36145 -> value:    0  ok
logical: 19810 (page:  77, offset:  98) ---> physical: 36450 -> value:   19  ok

logical: 25955 (page: 101, offset:  99) ---> physical: 36707 -> value:   88  ok
logical: 62678 (page: 244, offset: 214) ---> physical:   470 -> value:   61  ok
logical: 26021 (page: 101, offset: 165) ---> physical: 36773 -> value:    0  ok
logical: 29409 (page: 114, offset: 225) ---> physical: 37089 -> value:    0  ok
logical: 38111 (page: 148, offset: 223) ---> physical: 37343 -> value:   55  ok

logical: 58573 (page: 228, offset: 205) ---> physical: 15565 -> value:    0  ok
logical: 56840 (page: 222, offset:   8) ---> physical: 37384 -> value:    0  ok
logical: 41306 (page: 161, offset:  90) ---> physical: 24922 -> value:   40  ok
logical: 54426 (page: 212, offset: 154) ---> physical: 19354 -> value:   53  ok
logical:  3617 (page:  14, offset:  33) ---> physical: 10017 -> value:    0  ok

logical: 50652 (page: 197, offset: 220) ---> physical: 18652 -> value:    0  ok
logical: 41452 (page: 161, offset: 236) ---> physical: 25068 -> value:    0  ok
logical: 20241 (page:  79, offset:  17) ---> physical: 16657 -> value:    0  ok
logical: 31723 (page: 123, offset: 235) ---> physical: 12267 -> value:   -6  ok
logical: 53747 (page: 209, offset: 243) ---> physical:  1011 -> value:  124  ok

logical: 28550 (page: 111, offset: 134) ---> physical: 28038 -> value:   27  ok
logical: 23402 (page:  91, offset: 106) ---> physical: 20586 -> value:   22  ok
logical: 21205 (page:  82, offset: 213) ---> physical: 37845 -> value:    0  ok
logical: 56181 (page: 219, offset: 117) ---> physical: 25205 -> value:    0  ok
logical: 57470 (page: 224, offset: 126) ---> physical: 38014 -> value:   56  ok

logical: 39933 (page: 155, offset: 253) ---> physical: 38397 -> value:    0  ok
logical: 34964 (page: 136, offset: 148) ---> physical: 26772 -> value:    0  ok
logical: 24781 (page:  96, offset: 205) ---> physical: 38605 -> value:    0  ok
logical: 41747 (page: 163, offset:  19) ---> physical: 38675 -> value:  -60  ok
logical: 62564 (page: 244, offset: 100) ---> physical:   356 -> value:    0  ok

logical: 58461 (page: 228, offset:  93) ---> physical: 15453 -> value:    0  ok
logical: 20858 (page:  81, offset: 122) ---> physical: 39034 -> value:   20  ok
logical: 49301 (page: 192, offset: 149) ---> physical:  8597 -> value:    0  ok
logical: 40572 (page: 158, offset: 124) ---> physical: 39292 -> value:    0  ok
logical: 23840 (page:  93, offset:  32) ---> physical: 39456 -> value:    0  ok

logical: 35278 (page: 137, offset: 206) ---> physical: 39886 -> value:   34  ok
logical: 62905 (page: 245, offset: 185) ---> physical: 28601 -> value:    0  ok
logical: 56650 (page: 221, offset:  74) ---> physical: 13386 -> value:   55  ok
logical: 11149 (page:  43, offset: 141) ---> physical: 40077 -> value:    0  ok
logical: 38920 (page: 152, offset:   8) ---> physical:  3848 -> value:    0  ok

logical: 23430 (page:  91, offset: 134) ---> physical: 20614 -> value:   22  ok
logical: 57592 (page: 224, offset: 248) ---> physical: 38136 -> value:    0  ok
logical:  3080 (page:  12, offset:   8) ---> physical: 40200 -> value:    0  ok
logical:  6677 (page:  26, offset:  21) ---> physical:  6677 -> value:    0  ok
logical: 50704 (page: 198, offset:  16) ---> physical: 26128 -> value:    0  ok

logical: 51883 (page: 202, offset: 171) ---> physical: 27307 -> value:  -86  ok
logical: 62799 (page: 245, offset:  79) ---> physical: 28495 -> value:   83  ok
logical: 20188 (page:  78, offset: 220) ---> physical: 40668 -> value:    0  ok
logical:  1245 (page:   4, offset: 221) ---> physical: 40925 -> value:    0  ok
logical: 12220 (page:  47, offset: 188) ---> physical:  2748 -> value:    0  ok

logical: 17602 (page:  68, offset: 194) ---> physical: 41154 -> value:   17  ok
logical: 28609 (page: 111, offset: 193) ---> physical: 28097 -> value:    0  ok
logical: 42694 (page: 166, offset: 198) ---> physical: 20934 -> value:   41  ok
logical: 29826 (page: 116, offset: 130) ---> physical: 41346 -> value:   29  ok
logical: 13827 (page:  54, offset:   3) ---> physical: 31235 -> value: -128  ok

logical: 27336 (page: 106, offset: 200) ---> physical: 28872 -> value:    0  ok
logical: 53343 (page: 208, offset:  95) ---> physical: 35423 -> value:   23  ok
logical: 11533 (page:  45, offset:  13) ---> physical: 41485 -> value:    0  ok
logical: 41713 (page: 162, offset: 241) ---> physical: 41969 -> value:    0  ok
logical: 33890 (page: 132, offset:  98) ---> physical: 42082 -> value:   33  ok

logical:  4894 (page:  19, offset:  30) ---> physical: 13598 -> value:    4  ok
logical: 57599 (page: 224, offset: 255) ---> physical: 38143 -> value:   63  ok
logical:  3870 (page:  15, offset:  30) ---> physical: 18974 -> value:    3  ok
logical: 58622 (page: 228, offset: 254) ---> physical: 15614 -> value:   57  ok
logical: 29780 (page: 116, offset:  84) ---> physical: 41300 -> value:    0  ok

logical: 62553 (page: 244, offset:  89) ---> physical:   345 -> value:    0  ok
logical:  2303 (page:   8, offset: 255) ---> physical: 35327 -> value:   63  ok
logical: 51915 (page: 202, offset: 203) ---> physical: 27339 -> value:  -78  ok
logical:  6251 (page:  24, offset: 107) ---> physical:  7531 -> value:   26  ok
logical: 38107 (page: 148, offset: 219) ---> physical: 37339 -> value:   54  ok

logical: 59325 (page: 231, offset: 189) ---> physical: 10429 -> value:    0  ok
logical: 61295 (page: 239, offset: 111) ---> physical: 42351 -> value:  -37  ok
logical: 26699 (page: 104, offset:  75) ---> physical: 42571 -> value:   18  ok
logical: 51188 (page: 199, offset: 244) ---> physical: 42996 -> value:    0  ok
logical: 59519 (page: 232, offset: 127) ---> physical: 25983 -> value:   31  ok

logical:  7345 (page:  28, offset: 177) ---> physical: 43185 -> value:    0  ok
logical: 20325 (page:  79, offset: 101) ---> physical: 16741 -> value:    0  ok
logical: 39633 (page: 154, offset: 209) ---> physical: 43473 -> value:    0  ok
logical:  1562 (page:   6, offset:  26) ---> physical: 43546 -> value:    1  ok
logical:  7580 (page:  29, offset: 156) ---> physical:  6300 -> value:    0  ok

logical:  8170 (page:  31, offset: 234) ---> physical: 34026 -> value:    7  ok
logical: 62256 (page: 243, offset:  48) ---> physical: 23856 -> value:    0  ok
logical: 35823 (page: 139, offset: 239) ---> physical: 44015 -> value:   -5  ok
logical: 27790 (page: 108, offset: 142) ---> physical: 44174 -> value:   27  ok
logical: 13191 (page:  51, offset: 135) ---> physical: 44423 -> value:  -31  ok

logical:  9772 (page:  38, offset:  44) ---> physical: 44588 -> value:    0  ok
logical:  7477 (page:  29, offset:  53) ---> physical:  6197 -> value:    0  ok
logical: 44455 (page: 173, offset: 167) ---> physical: 44967 -> value:  105  ok
logical: 59546 (page: 232, offset: 154) ---> physical: 26010 -> value:   58  ok
logical: 49347 (page: 192, offset: 195) ---> physical:  8643 -> value:   48  ok

logical: 36539 (page: 142, offset: 187) ---> physical: 14523 -> value:  -82  ok
logical: 12453 (page:  48, offset: 165) ---> physical: 45221 -> value:    0  ok
logical: 49640 (page: 193, offset: 232) ---> physical: 45544 -> value:    0  ok
logical: 28290 (page: 110, offset: 130) ---> physical: 34946 -> value:   27  ok
logical: 44817 (page: 175, offset:  17) ---> physical: 13073 -> value:    0  ok

logical:  8565 (page:  33, offset: 117) ---> physical: 20085 -> value:    0  ok
logical: 16399 (page:  64, offset:  15) ---> physical: 45583 -> value:    3  ok
logical: 41934 (page: 163, offset: 206) ---> physical: 38862 -> value:   40  ok
logical: 45457 (page: 177, offset: 145) ---> physical: 45969 -> value:    0  ok
logical: 33856 (page: 132, offset:  64) ---> physical: 42048 -> value:    0  ok

logical: 19498 (page:  76, offset:  42) ---> physical: 22570 -> value:   19  ok
logical: 17661 (page:  68, offset: 253) ---> physical: 41213 -> value:    0  ok
logical: 63829 (page: 249, offset:  85) ---> physical: 21845 -> value:    0  ok
logical: 42034 (page: 164, offset:  50) ---> physical: 46130 -> value:   41  ok
logical: 28928 (page: 113, offset:   0) ---> physical: 16384 -> value:    0  ok

logical: 30711 (page: 119, offset: 247) ---> physical: 16375 -> value:   -3  ok
logical:  8800 (page:  34, offset:  96) ---> physical: 46432 -> value:    0  ok
logical: 52335 (page: 204, offset: 111) ---> physical: 46703 -> value:   27  ok
logical: 38775 (page: 151, offset: 119) ---> physical: 46967 -> value:  -35  ok
logical: 52704 (page: 205, offset: 224) ---> physical: 30176 -> value:    0  ok

logical: 24380 (page:  95, offset:  60) ---> physical:  1596 -> value:    0  ok
logical: 19602 (page:  76, offset: 146) ---> physical: 22674 -> value:   19  ok
logical: 57998 (page: 226, offset: 142) ---> physical:  3214 -> value:   56  ok
logical:  2919 (page:  11, offset: 103) ---> physical: 11623 -> value:  -39  ok
logical:  8362 (page:  32, offset: 170) ---> physical: 47274 -> value:    8  ok

logical: 17884 (page:  69, offset: 220) ---> physical:  9948 -> value:    0  ok
logical: 45737 (page: 178, offset: 169) ---> physical:  7849 -> value:    0  ok
logical: 47894 (page: 187, offset:  22) ---> physical: 25622 -> value:   46  ok
logical: 59667 (page: 233, offset:  19) ---> physical: 47379 -> value:   68  ok
logical: 10385 (page:  40, offset: 145) ---> physical: 14737 -> value:    0  ok

logical: 52782 (page: 206, offset:  46) ---> physical: 47662 -> value:   51  ok
logical: 64416 (page: 251, offset: 160) ---> physical:  5024 -> value:    0  ok
logical: 40946 (page: 159, offset: 242) ---> physical:  8434 -> value:   39  ok
logical: 16778 (page:  65, offset: 138) ---> physical: 48010 -> value:   16  ok
logical: 27159 (page: 106, offset:  23) ---> physical: 28695 -> value: -123  ok

logical: 24324 (page:  95, offset:   4) ---> physical:  1540 -> value:    0  ok
logical: 32450 (page: 126, offset: 194) ---> physical:  7106 -> value:   31  ok
logical:  9108 (page:  35, offset: 148) ---> physical: 48276 -> value:    0  ok
logical: 65305 (page: 255, offset:  25) ---> physical: 30489 -> value:    0  ok
logical: 19575 (page:  76, offset: 119) ---> physical: 22647 -> value:   29  ok

logical: 11117 (page:  43, offset: 109) ---> physical: 40045 -> value:    0  ok
logical: 65170 (page: 254, offset: 146) ---> physical: 48530 -> value:   63  ok
logical: 58013 (page: 226, offset: 157) ---> physical:  3229 -> value:    0  ok
logical: 61676 (page: 240, offset: 236) ---> physical: 23276 -> value:    0  ok
logical: 63510 (page: 248, offset:  22) ---> physical: 28182 -> value:   62  ok

logical: 17458 (page:  68, offset:  50) ---> physical: 41010 -> value:   17  ok
logical: 54675 (page: 213, offset: 147) ---> physical: 48787 -> value:  100  ok
logical:  1713 (page:   6, offset: 177) ---> physical: 43697 -> value:    0  ok
logical: 55105 (page: 215, offset:  65) ---> physical:  5185 -> value:    0  ok
logical: 65321 (page: 255, offset:  41) ---> physical: 30505 -> value:    0  ok

logical: 45278 (page: 176, offset: 222) ---> physical: 49118 -> value:   44  ok
logical: 26256 (page: 102, offset: 144) ---> physical: 49296 -> value:    0  ok
logical: 64198 (page: 250, offset: 198) ---> physical:  4550 -> value:   62  ok
logical: 29441 (page: 115, offset:   1) ---> physical: 49409 -> value:    0  ok
logical:  1928 (page:   7, offset: 136) ---> physical: 33160 -> value:    0  ok

logical: 39425 (page: 154, offset:   1) ---> physical: 43265 -> value:    0  ok
logical: 32000 (page: 125, offset:   0) ---> physical: 49664 -> value:    0  ok
logical: 28549 (page: 111, offset: 133) ---> physical: 28037 -> value:    0  ok
logical: 46295 (page: 180, offset: 215) ---> physical: 50135 -> value:   53  ok
logical: 22772 (page:  88, offset: 244) ---> physical:  3060 -> value:    0  ok

logical: 58228 (page: 227, offset: 116) ---> physical: 34164 -> value:    0  ok
logical: 63525 (page: 248, offset:  37) ---> physical: 28197 -> value:    0  ok
logical: 32602 (page: 127, offset:  90) ---> physical:  9562 -> value:   31  ok
logical: 46195 (page: 180, offset: 115) ---> physical: 50035 -> value:   28  ok
logical: 55849 (page: 218, offset:  41) ---> physical: 34601 -> value:    0  ok

logical: 46454 (page: 181, offset: 118) ---> physical: 50294 -> value:   45  ok
logical:  7487 (page:  29, offset:  63) ---> physical:  6207 -> value:   79  ok
logical: 33879 (page: 132, offset:  87) ---> physical: 42071 -> value:   21  ok
logical: 42004 (page: 164, offset:  20) ---> physical: 46100 -> value:    0  ok
logical:  8599 (page:  33, offset: 151) ---> physical: 20119 -> value:  101  ok

logical: 18641 (page:  72, offset: 209) ---> physical:  5585 -> value:    0  ok
logical: 49015 (page: 191, offset: 119) ---> physical: 50551 -> value:  -35  ok
logical: 26830 (page: 104, offset: 206) ---> physical: 42702 -> value:   26  ok
logical: 34754 (page: 135, offset: 194) ---> physical: 18114 -> value:   33  ok
logical: 14668 (page:  57, offset:  76) ---> physical: 33356 -> value:    0  ok

logical: 38362 (page: 149, offset: 218) ---> physical: 20442 -> value:   37  ok
logical: 38791 (page: 151, offset: 135) ---> physical: 46983 -> value:  -31  ok
logical:  4171 (page:  16, offset:  75) ---> physical: 50763 -> value:   18  ok
logical: 45975 (page: 179, offset: 151) ---> physical: 51095 -> value:  -27  ok
logical: 14623 (page:  57, offset:  31) ---> physical: 33311 -> value:   71  ok

logical: 62393 (page: 243, offset: 185) ---> physical: 23993 -> value:    0  ok
logical: 64658 (page: 252, offset: 146) ---> physical:  6546 -> value:   63  ok
logical: 10963 (page:  42, offset: 211) ---> physical: 12499 -> value:  -76  ok
logical:  9058 (page:  35, offset:  98) ---> physical: 48226 -> value:    8  ok
logical: 51031 (page: 199, offset:  87) ---> physical: 42839 -> value:  -43  ok

logical: 32425 (page: 126, offset: 169) ---> physical:  7081 -> value:    0  ok
logical: 45483 (page: 177, offset: 171) ---> physical: 45995 -> value:  106  ok
logical: 44611 (page: 174, offset:  67) ---> physical: 11075 -> value: -112  ok
logical: 63664 (page: 248, offset: 176) ---> physical: 28336 -> value:    0  ok
logical: 54920 (page: 214, offset: 136) ---> physical:  3720 -> value:    0  ok

logical:  7663 (page:  29, offset: 239) ---> physical:  6383 -> value:  123  ok
logical: 56480 (page: 220, offset: 160) ---> physical: 51360 -> value:    0  ok
logical:  1489 (page:   5, offset: 209) ---> physical: 51665 -> value:    0  ok
logical: 28438 (page: 111, offset:  22) ---> physical: 27926 -> value:   27  ok
logical: 65449 (page: 255, offset: 169) ---> physical: 30633 -> value:    0  ok

logical: 12441 (page:  48, offset: 153) ---> physical: 45209 -> value:    0  ok
logical: 58530 (page: 228, offset: 162) ---> physical: 15522 -> value:   57  ok
logical: 63570 (page: 248, offset:  82) ---> physical: 28242 -> value:   62  ok
logical: 26251 (page: 102, offset: 139) ---> physical: 49291 -> value:  -94  ok
logical: 15972 (page:  62, offset: 100) ---> physical: 21092 -> value:    0  ok

logical: 35826 (page: 139, offset: 242) ---> physical: 44018 -> value:   34  ok
logical:  5491 (page:  21, offset: 115) ---> physical: 21619 -> value:   92  ok
logical: 54253 (page: 211, offset: 237) ---> physical: 51949 -> value:    0  ok
logical: 49655 (page: 193, offset: 247) ---> physical: 45559 -> value:  125  ok
logical:  5868 (page:  22, offset: 236) ---> physical: 52204 -> value:    0  ok

logical: 20163 (page:  78, offset: 195) ---> physical: 40643 -> value:  -80  ok
logical: 51079 (page: 199, offset: 135) ---> physical: 42887 -> value:  -31  ok
logical: 21398 (page:  83, offset: 150) ---> physical:  9110 -> value:   20  ok
logical: 32756 (page: 127, offset: 244) ---> physical:  9716 -> value:    0  ok
logical: 64196 (page: 250, offset: 196) ---> physical:  4548 -> value:    0  ok

logical: 43218 (page: 168, offset: 210) ---> physical: 17618 -> value:   42  ok
logical: 21583 (page:  84, offset:  79) ---> physical: 52303 -> value:   19  ok
logical: 25086 (page:  97, offset: 254) ---> physical: 52734 -> value:   24  ok
logical: 45515 (page: 177, offset: 203) ---> physical: 46027 -> value:  114  ok
logical: 12893 (page:  50, offset:  93) ---> physical: 30301 -> value:    0  ok

logical: 22914 (page:  89, offset: 130) ---> physical: 52866 -> value:   22  ok
logical: 58969 (page: 230, offset:  89) ---> physical: 14937 -> value:    0  ok
logical: 20094 (page:  78, offset: 126) ---> physical: 40574 -> value:   19  ok
logical: 13730 (page:  53, offset: 162) ---> physical: 53154 -> value:   13  ok
logical: 44059 (page: 172, offset:  27) ---> physical: 53275 -> value:    6  ok

logical: 28931 (page: 113, offset:   3) ---> physical: 16387 -> value:   64  ok
logical: 13533 (page:  52, offset: 221) ---> physical: 36317 -> value:    0  ok
logical: 33134 (page: 129, offset: 110) ---> physical: 53614 -> value:   32  ok
logical: 28483 (page: 111, offset:  67) ---> physical: 27971 -> value:  -48  ok
logical:  1220 (page:   4, offset: 196) ---> physical: 40900 -> value:    0  ok

logical: 38174 (page: 149, offset:  30) ---> physical: 20254 -> value:   37  ok
logical: 53502 (page: 208, offset: 254) ---> physical: 35582 -> value:   52  ok
logical: 43328 (page: 169, offset:  64) ---> physical: 29504 -> value:    0  ok
logical:  4970 (page:  19, offset: 106) ---> physical: 13674 -> value:    4  ok
logical:  8090 (page:  31, offset: 154) ---> physical: 33946 -> value:    7  ok

logical:  2661 (page:  10, offset: 101) ---> physical: 53861 -> value:    0  ok
logical: 53903 (page: 210, offset: 143) ---> physical: 54159 -> value:  -93  ok
logical: 11025 (page:  43, offset:  17) ---> physical: 39953 -> value:    0  ok
logical: 26627 (page: 104, offset:   3) ---> physical: 42499 -> value:    0  ok
logical: 18117 (page:  70, offset: 197) ---> physical: 18885 -> value:    0  ok

logical: 14505 (page:  56, offset: 169) ---> physical:  5801 -> value:    0  ok
logical: 61528 (page: 240, offset:  88) ---> physical: 23128 -> value:    0  ok
logical: 20423 (page:  79, offset: 199) ---> physical: 16839 -> value:  -15  ok
logical: 26962 (page: 105, offset:  82) ---> physical: 26962 -> value:   26  ok
logical: 36392 (page: 142, offset:  40) ---> physical: 14376 -> value:    0  ok

logical: 11365 (page:  44, offset: 101) ---> physical: 54373 -> value:    0  ok
logical: 50882 (page: 198, offset: 194) ---> physical: 26306 -> value:   49  ok
logical: 41668 (page: 162, offset: 196) ---> physical: 41924 -> value:    0  ok
logical: 30497 (page: 119, offset:  33) ---> physical: 16161 -> value:    0  ok
logical: 36216 (page: 141, offset: 120) ---> physical: 54648 -> value:    0  ok

logical:  5619 (page:  21, offset: 243) ---> physical: 21747 -> value:  124  ok
logical: 36983 (page: 144, offset: 119) ---> physical: 18295 -> value:   29  ok
logical: 59557 (page: 232, offset: 165) ---> physical: 26021 -> value:    0  ok
logical: 36663 (page: 143, offset:  55) ---> physical: 35639 -> value:  -51  ok
logical: 36436 (page: 142, offset:  84) ---> physical: 14420 -> value:    0  ok

logical: 37057 (page: 144, offset: 193) ---> physical: 18369 -> value:    0  ok
logical: 23585 (page:  92, offset:  33) ---> physical: 54817 -> value:    0  ok
logical: 58791 (page: 229, offset: 167) ---> physical: 55207 -> value:  105  ok
logical: 46666 (page: 182, offset:  74) ---> physical: 55370 -> value:   45  ok
logical: 64475 (page: 251, offset: 219) ---> physical:  5083 -> value:  -10  ok

logical: 21615 (page:  84, offset: 111) ---> physical: 52335 -> value:   27  ok
logical: 41090 (page: 160, offset: 130) ---> physical:  8834 -> value:   40  ok
logical:  1771 (page:   6, offset: 235) ---> physical: 43755 -> value:  -70  ok
logical: 47513 (page: 185, offset: 153) ---> physical: 32153 -> value:    0  ok
logical: 39338 (page: 153, offset: 170) ---> physical: 55722 -> value:   38  ok

logical:  1390 (page:   5, offset: 110) ---> physical: 51566 -> value:    1  ok
logical: 38772 (page: 151, offset: 116) ---> physical: 46964 -> value:    0  ok
logical: 58149 (page: 227, offset:  37) ---> physical: 34085 -> value:    0  ok
logical:  7196 (page:  28, offset:  28) ---> physical: 43036 -> value:    0  ok
logical:  9123 (page:  35, offset: 163) ---> physical: 48291 -> value:  -24  ok

logical:  7491 (page:  29, offset:  67) ---> physical:  6211 -> value:   80  ok
logical: 62616 (page: 244, offset: 152) ---> physical:   408 -> value:    0  ok
logical: 15436 (page:  60, offset:  76) ---> physical: 31820 -> value:    0  ok
logical: 17491 (page:  68, offset:  83) ---> physical: 41043 -> value:   20  ok
logical: 53656 (page: 209, offset: 152) ---> physical:   920 -> value:    0  ok

logical: 26449 (page: 103, offset:  81) ---> physical: 24401 -> value:    0  ok
logical: 34935 (page: 136, offset: 119) ---> physical: 26743 -> value:   29  ok
logical: 19864 (page:  77, offset: 152) ---> physical: 36504 -> value:    0  ok
logical: 51388 (page: 200, offset: 188) ---> physical: 32700 -> value:    0  ok
logical: 15155 (page:  59, offset:  51) ---> physical: 32307 -> value:  -52  ok

logical: 64775 (page: 253, offset:   7) ---> physical:  2055 -> value:   65  ok
logical: 47969 (page: 187, offset:  97) ---> physical: 25697 -> value:    0  ok
logical: 16315 (page:  63, offset: 187) ---> physical: 55995 -> value:  -18  ok
logical:  1342 (page:   5, offset:  62) ---> physical: 51518 -> value:    1  ok
logical: 51185 (page: 199, offset: 241) ---> physical: 42993 -> value:    0  ok

logical:  6043 (page:  23, offset: 155) ---> physical:  9371 -> value:  -26  ok
logical: 21398 (page:  83, offset: 150) ---> physical:  9110 -> value:   20  ok
logical:  3273 (page:  12, offset: 201) ---> physical: 40393 -> value:    0  ok
logical:  9370 (page:  36, offset: 154) ---> physical: 22938 -> value:    9  ok
logical: 35463 (page: 138, offset: 135) ---> physical: 56199 -> value:  -95  ok

logical: 28205 (page: 110, offset:  45) ---> physical: 34861 -> value:    0  ok
logical:  2351 (page:   9, offset:  47) ---> physical:  4655 -> value:   75  ok
logical: 28999 (page: 113, offset:  71) ---> physical: 16455 -> value:   81  ok
logical: 47699 (page: 186, offset:  83) ---> physical: 56403 -> value: -108  ok
logical: 46870 (page: 183, offset:  22) ---> physical: 19734 -> value:   45  ok

logical: 22311 (page:  87, offset:  39) ---> physical: 11303 -> value:  -55  ok
logical: 22124 (page:  86, offset: 108) ---> physical: 56684 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical: 11419 -> value:  -26  ok
logical: 49344 (page: 192, offset: 192) ---> physical:  8640 -> value:    0  ok
logical: 23224 (page:  90, offset: 184) ---> physical: 36024 -> value:    0  ok

logical:  5514 (page:  21, offset: 138) ---> physical: 21642 -> value:    5  ok
logical: 20504 (page:  80, offset:  24) ---> physical: 56856 -> value:    0  ok
logical:   376 (page:   1, offset: 120) ---> physical: 57208 -> value:    0  ok
logical:  2014 (page:   7, offset: 222) ---> physical: 33246 -> value:    1  ok
logical: 38700 (page: 151, offset:  44) ---> physical: 46892 -> value:    0  ok

logical: 13098 (page:  51, offset:  42) ---> physical: 44330 -> value:   12  ok
logical: 62435 (page: 243, offset: 227) ---> physical: 24035 -> value:   -8  ok
logical: 48046 (page: 187, offset: 174) ---> physical: 25774 -> value:   46  ok
logical: 63464 (page: 247, offset: 232) ---> physical: 17128 -> value:    0  ok
logical: 12798 (page:  49, offset: 254) ---> physical: 31230 -> value:   12  ok

logical: 51178 (page: 199, offset: 234) ---> physical: 42986 -> value:   49  ok
logical:  8627 (page:  33, offset: 179) ---> physical: 20147 -> value:  108  ok
logical: 27083 (page: 105, offset: 203) ---> physical: 27083 -> value:  114  ok
logical: 47198 (page: 184, offset:  94) ---> physical: 29790 -> value:   46  ok
logical: 44021 (page: 171, offset: 245) ---> physical: 57589 -> value:    0  ok

logical: 32792 (page: 128, offset:  24) ---> physical:  4120 -> value:    0  ok
logical: 43996 (page: 171, offset: 220) ---> physical: 57564 -> value:    0  ok
logical: 41126 (page: 160, offset: 166) ---> physical:  8870 -> value:   40  ok
logical: 64244 (page: 250, offset: 244) ---> physical:  4596 -> value:    0  ok
logical: 37047 (page: 144, offset: 183) ---> physical: 18359 -> value:   45  ok

logical: 60281 (page: 235, offset: 121) ---> physical: 57721 -> value:    0  ok
logical: 52904 (page: 206, offset: 168) ---> physical: 47784 -> value:    0  ok
logical:  7768 (page:  30, offset:  88) ---> physical: 57944 -> value:    0  ok
logical: 55359 (page: 216, offset:  63) ---> physical: 58175 -> value:   15  ok
logical:  3230 (page:  12, offset: 158) ---> physical: 40350 -> value:    3  ok

logical: 44813 (page: 175, offset:  13) ---> physical: 13069 -> value:    0  ok
logical:  4116 (page:  16, offset:  20) ---> physical: 50708 -> value:    0  ok
logical: 65222 (page: 254, offset: 198) ---> physical: 48582 -> value:   63  ok
logical: 28083 (page: 109, offset: 179) ---> physical:  3507 -> value:  108  ok
logical: 60660 (page: 236, offset: 244) ---> physical:  7412 -> value:    0  ok

logical:    39 (page:   0, offset:  39) ---> physical: 58407 -> value:    9  ok
logical:   328 (page:   1, offset:  72) ---> physical: 57160 -> value:    0  ok
logical: 47868 (page: 186, offset: 252) ---> physical: 56572 -> value:    0  ok
logical: 13009 (page:  50, offset: 209) ---> physical: 30417 -> value:    0  ok
logical: 22378 (page:  87, offset: 106) ---> physical: 11370 -> value:   21  ok

logical: 39304 (page: 153, offset: 136) ---> physical: 55688 -> value:    0  ok
logical: 11171 (page:  43, offset: 163) ---> physical: 40099 -> value:  -24  ok
logical:  8079 (page:  31, offset: 143) ---> physical: 33935 -> value:  -29  ok
logical: 52879 (page: 206, offset: 143) ---> physical: 47759 -> value:  -93  ok
logical:  5123 (page:  20, offset:   3) ---> physical: 15107 -> value:    0  ok

logical:  4356 (page:  17, offset:   4) ---> physical: 58628 -> value:    0  ok
logical: 45745 (page: 178, offset: 177) ---> physical:  7857 -> value:    0  ok
logical: 32952 (page: 128, offset: 184) ---> physical:  4280 -> value:    0  ok
logical:  4657 (page:  18, offset:  49) ---> physical: 27697 -> value:    0  ok
logical: 24142 (page:  94, offset:  78) ---> physical: 33614 -> value:   23  ok

logical: 23319 (page:  91, offset:  23) ---> physical: 20503 -> value:  -59  ok
logical: 13607 (page:  53, offset:  39) ---> physical: 53031 -> value:   73  ok
logical: 46304 (page: 180, offset: 224) ---> physical: 50144 -> value:    0  ok
logical: 17677 (page:  69, offset:  13) ---> physical:  9741 -> value:    0  ok
logical: 59691 (page: 233, offset:  43) ---> physical: 47403 -> value:   74  ok

logical: 50967 (page: 199, offset:  23) ---> physical: 42775 -> value:  -59  ok
logical:  7817 (page:  30, offset: 137) ---> physical: 57993 -> value:    0  ok
logical:  8545 (page:  33, offset:  97) ---> physical: 20065 -> value:    0  ok
logical: 55297 (page: 216, offset:   1) ---> physical: 58113 -> value:    0  ok
logical: 52954 (page: 206, offset: 218) ---> physical: 47834 -> value:   51  ok

logical: 39720 (page: 155, offset:  40) ---> physical: 38184 -> value:    0  ok
logical: 18455 (page:  72, offset:  23) ---> physical:  5399 -> value:    5  ok
logical: 30349 (page: 118, offset: 141) ---> physical: 59021 -> value:    0  ok
logical: 63270 (page: 247, offset:  38) ---> physical: 16934 -> value:   61  ok
logical: 27156 (page: 106, offset:  20) ---> physical: 28692 -> value:    0  ok

logical: 20614 (page:  80, offset: 134) ---> physical: 56966 -> value:   20  ok
logical: 19372 (page:  75, offset: 172) ---> physical: 14252 -> value:    0  ok
logical: 48689 (page: 190, offset:  49) ---> physical: 32817 -> value:    0  ok
logical: 49386 (page: 192, offset: 234) ---> physical:  8682 -> value:   48  ok
logical: 50584 (page: 197, offset: 152) ---> physical: 18584 -> value:    0  ok

logical: 51936 (page: 202, offset: 224) ---> physical: 27360 -> value:    0  ok
logical: 34705 (page: 135, offset: 145) ---> physical: 18065 -> value:    0  ok
logical: 13653 (page:  53, offset:  85) ---> physical: 53077 -> value:    0  ok
logical: 50077 (page: 195, offset: 157) ---> physical: 59293 -> value:    0  ok
logical: 54518 (page: 212, offset: 246) ---> physical: 19446 -> value:   53  ok

logical: 41482 (page: 162, offset:  10) ---> physical: 41738 -> value:   40  ok
logical:  4169 (page:  16, offset:  73) ---> physical: 50761 -> value:    0  ok
logical: 36118 (page: 141, offset:  22) ---> physical: 54550 -> value:   35  ok
logical:  9584 (page:  37, offset: 112) ---> physical: 59504 -> value:    0  ok
logical: 18490 (page:  72, offset:  58) ---> physical:  5434 -> value:   18  ok

logical: 55420 (page: 216, offset: 124) ---> physical: 58236 -> value:    0  ok
logical:  5708 (page:  22, offset:  76) ---> physical: 52044 -> value:    0  ok
logical: 23506 (page:  91, offset: 210) ---> physical: 20690 -> value:   22  ok
logical: 15391 (page:  60, offset:  31) ---> physical: 31775 -> value:    7  ok
logical: 36368 (page: 142, offset:  16) ---> physical: 14352 -> value:    0  ok

logical: 38976 (page: 152, offset:  64) ---> physical:  3904 -> value:    0  ok
logical: 50406 (page: 196, offset: 230) ---> physical: 14054 -> value:   49  ok
logical: 49236 (page: 192, offset:  84) ---> physical:  8532 -> value:    0  ok
logical: 65035 (page: 254, offset:  11) ---> physical: 48395 -> value: -126  ok
logical: 30120 (page: 117, offset: 168) ---> physical:   680 -> value:    0  ok

logical: 62551 (page: 244, offset:  87) ---> physical:   343 -> value:   21  ok
logical: 46809 (page: 182, offset: 217) ---> physical: 55513 -> value:    0  ok
logical: 21687 (page:  84, offset: 183) ---> physical: 52407 -> value:   45  ok
logical: 53839 (page: 210, offset:  79) ---> physical: 54095 -> value: -109  ok
logical:  2098 (page:   8, offset:  50) ---> physical: 35122 -> value:    2  ok

logical: 12364 (page:  48, offset:  76) ---> physical: 45132 -> value:    0  ok
logical: 45366 (page: 177, offset:  54) ---> physical: 45878 -> value:   44  ok
logical: 50437 (page: 197, offset:   5) ---> physical: 18437 -> value:    0  ok
logical: 36675 (page: 143, offset:  67) ---> physical: 35651 -> value:  -48  ok
logical: 55382 (page: 216, offset:  86) ---> physical: 58198 -> value:   54  ok

logical: 11846 (page:  46, offset:  70) ---> physical: 59718 -> value:   11  ok
logical: 49127 (page: 191, offset: 231) ---> physical: 50663 -> value:   -7  ok
logical: 19900 (page:  77, offset: 188) ---> physical: 36540 -> value:    0  ok
logical: 20554 (page:  80, offset:  74) ---> physical: 56906 -> value:   20  ok
logical: 19219 (page:  75, offset:  19) ---> physical: 14099 -> value:  -60  ok

logical: 51483 (page: 201, offset:  27) ---> physical: 15899 -> value:   70  ok
logical: 58090 (page: 226, offset: 234) ---> physical:  3306 -> value:   56  ok
logical: 39074 (page: 152, offset: 162) ---> physical:  4002 -> value:   38  ok
logical: 16060 (page:  62, offset: 188) ---> physical: 21180 -> value:    0  ok
logical: 10447 (page:  40, offset: 207) ---> physical: 14799 -> value:   51  ok

logical: 54169 (page: 211, offset: 153) ---> physical: 51865 -> value:    0  ok
logical: 20634 (page:  80, offset: 154) ---> physical: 56986 -> value:   20  ok
logical: 57555 (page: 224, offset: 211) ---> physical: 38099 -> value:   52  ok
logical: 61210 (page: 239, offset:  26) ---> physical: 42266 -> value:   59  ok
logical:   269 (page:   1, offset:  13) ---> physical: 57101 -> value:    0  ok

logical: 33154 (page: 129, offset: 130) ---> physical: 53634 -> value:   32  ok
logical: 64487 (page: 251, offset: 231) ---> physical:  5095 -> value:   -7  ok
logical: 61223 (page: 239, offset:  39) ---> physical: 42279 -> value:  -55  ok
logical: 47292 (page: 184, offset: 188) ---> physical: 29884 -> value:    0  ok
logical: 21852 (page:  85, offset:  92) ---> physical: 24156 -> value:    0  ok

logical:  5281 (page:  20, offset: 161) ---> physical: 15265 -> value:    0  ok
logical: 45912 (page: 179, offset:  88) ---> physical: 51032 -> value:    0  ok
logical: 32532 (page: 127, offset:  20) ---> physical:  9492 -> value:    0  ok
logical: 63067 (page: 246, offset:  91) ---> physical: 22107 -> value: -106  ok
logical: 41683 (page: 162, offset: 211) ---> physical: 41939 -> value:  -76  ok

logical: 20981 (page:  81, offset: 245) ---> physical: 39157 -> value:    0  ok
logical: 33881 (page: 132, offset:  89) ---> physical: 42073 -> value:    0  ok
logical: 41785 (page: 163, offset:  57) ---> physical: 38713 -> value:    0  ok
logical:  4580 (page:  17, offset: 228) ---> physical: 58852 -> value:    0  ok
logical: 41389 (page: 161, offset: 173) ---> physical: 25005 -> value:    0  ok

logical: 28572 (page: 111, offset: 156) ---> physical: 28060 -> value:    0  ok
logical:   782 (page:   3, offset:  14) ---> physical:  7950 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical: 58945 -> value:    0  ok
logical: 62267 (page: 243, offset:  59) ---> physical: 23867 -> value:  -50  ok
logical: 17922 (page:  70, offset:   2) ---> physical: 18690 -> value:   17  ok

logical: 63238 (page: 247, offset:   6) ---> physical: 16902 -> value:   61  ok
logical:  3308 (page:  12, offset: 236) ---> physical: 40428 -> value:    0  ok
logical: 26545 (page: 103, offset: 177) ---> physical: 24497 -> value:    0  ok
logical: 44395 (page: 173, offset: 107) ---> physical: 44907 -> value:   90  ok
logical: 39120 (page: 152, offset: 208) ---> physical:  4048 -> value:    0  ok

logical: 21706 (page:  84, offset: 202) ---> physical: 52426 -> value:   21  ok
logical:  7144 (page:  27, offset: 232) ---> physical: 26600 -> value:    0  ok
logical: 30244 (page: 118, offset:  36) ---> physical: 58916 -> value:    0  ok
logical:  3725 (page:  14, offset: 141) ---> physical: 10125 -> value:    0  ok
logical: 54632 (page: 213, offset: 104) ---> physical: 48744 -> value:    0  ok

logical: 30574 (page: 119, offset: 110) ---> physical: 16238 -> value:   29  ok
logical:  8473 (page:  33, offset:  25) ---> physical: 19993 -> value:    0  ok
logical: 12386 (page:  48, offset:  98) ---> physical: 45154 -> value:   12  ok
logical: 41114 (page: 160, offset: 154) ---> physical:  8858 -> value:   40  ok
logical: 57930 (page: 226, offset:  74) ---> physical:  3146 -> value:   56  ok

logical: 15341 (page:  59, offset: 237) ---> physical: 32493 -> value:    0  ok
logical: 15598 (page:  60, offset: 238) ---> physical: 31982 -> value:   15  ok
logical: 59922 (page: 234, offset:  18) ---> physical: 10514 -> value:   58  ok
logical: 18226 (page:  71, offset:  50) ---> physical:  2354 -> value:   17  ok
logical: 48162 (page: 188, offset:  34) ---> physical: 17698 -> value:   47  ok

logical: 41250 (page: 161, offset:  34) ---> physical: 24866 -> value:   40  ok
logical:  1512 (page:   5, offset: 232) ---> physical: 51688 -> value:    0  ok
logical:  2546 (page:   9, offset: 242) ---> physical:  4850 -> value:    2  ok
logical: 41682 (page: 162, offset: 210) ---> physical: 41938 -> value:   40  ok
logical:   322 (page:   1, offset:  66) ---> physical: 57154 -> value:    0  ok

logical:   880 (page:   3, offset: 112) ---> physical:  8048 -> value:    0  ok
logical: 20891 (page:  81, offset: 155) ---> physical: 39067 -> value:  102  ok
logical: 56604 (page: 221, offset:  28) ---> physical: 13340 -> value:    0  ok
logical: 40166 (page: 156, offset: 230) ---> physical:  1254 -> value:   39  ok
logical: 26791 (page: 104, offset: 167) ---> physical: 42663 -> value:   41  ok

logical: 44560 (page: 174, offset:  16) ---> physical: 11024 -> value:    0  ok
logical: 38698 (page: 151, offset:  42) ---> physical: 46890 -> value:   37  ok
logical: 64127 (page: 250, offset: 127) ---> physical:  4479 -> value:  -97  ok
logical: 15028 (page:  58, offset: 180) ---> physical: 24756 -> value:    0  ok
logical: 38669 (page: 151, offset:  13) ---> physical: 46861 -> value:    0  ok

logical: 45637 (page: 178, offset:  69) ---> physical:  7749 -> value:    0  ok
logical: 43151 (page: 168, offset: 143) ---> physical: 17551 -> value:   35  ok
logical:  9465 (page:  36, offset: 249) ---> physical: 23033 -> value:    0  ok
logical:  2498 (page:   9, offset: 194) ---> physical:  4802 -> value:    2  ok
logical: 13978 (page:  54, offset: 154) ---> physical: 31386 -> value:   13  ok

logical: 16326 (page:  63, offset: 198) ---> physical: 56006 -> value:   15  ok
logical: 51442 (page: 200, offset: 242) ---> physical: 32754 -> value:   50  ok
logical: 34845 (page: 136, offset:  29) ---> physical: 26653 -> value:    0  ok
logical: 63667 (page: 248, offset: 179) ---> physical: 28339 -> value:   44  ok
logical: 39370 (page: 153, offset: 202) ---> physical: 55754 -> value:   38  ok

logical: 55671 (page: 217, offset: 119) ---> physical: 60023 -> value:   93  ok
logical: 64496 (page: 251, offset: 240) ---> physical:  5104 -> value:    0  ok
logical:  7767 (page:  30, offset:  87) ---> physical: 57943 -> value: -107  ok
logical:  6283 (page:  24, offset: 139) ---> physical:  7563 -> value:   34  ok
logical: 55884 (page: 218, offset:  76) ---> physical: 34636 -> value:    0  ok

logical: 61103 (page: 238, offset: 175) ---> physical:  6063 -> value:  -85  ok
logical: 10184 (page:  39, offset: 200) ---> physical: 60360 -> value:    0  ok
logical: 39543 (page: 154, offset: 119) ---> physical: 43383 -> value:  -99  ok
logical:  9555 (page:  37, offset:  83) ---> physical: 59475 -> value:   84  ok
logical: 13963 (page:  54, offset: 139) ---> physical: 31371 -> value:  -94  ok

logical: 58975 (page: 230, offset:  95) ---> physical: 14943 -> value: -105  ok
logical: 19537 (page:  76, offset:  81) ---> physical: 22609 -> value:    0  ok
logical:  6101 (page:  23, offset: 213) ---> physical:  9429 -> value:    0  ok
logical: 41421 (page: 161, offset: 205) ---> physical: 25037 -> value:    0  ok
logical: 45502 (page: 177, offset: 190) ---> physical: 46014 -> value:   44  ok

logical: 29328 (page: 114, offset: 144) ---> physical: 37008 -> value:    0  ok
logical:  8149 (page:  31, offset: 213) ---> physical: 34005 -> value:    0  ok
logical: 25450 (page:  99, offset: 106) ---> physical: 60522 -> value:   24  ok
logical: 58944 (page: 230, offset:  64) ---> physical: 14912 -> value:    0  ok
logical: 50666 (page: 197, offset: 234) ---> physical: 18666 -> value:   49  ok

logical: 23084 (page:  90, offset:  44) ---> physical: 35884 -> value:    0  ok
logical: 36468 (page: 142, offset: 116) ---> physical: 14452 -> value:    0  ok
logical: 33645 (page: 131, offset: 109) ---> physical: 60781 -> value:    0  ok
logical: 25002 (page:  97, offset: 170) ---> physical: 52650 -> value:   24  ok
logical: 53715 (page: 209, offset: 211) ---> physical:   979 -> value:  116  ok

logical: 60173 (page: 235, offset:  13) ---> physical: 57613 -> value:    0  ok
logical: 46354 (page: 181, offset:  18) ---> physical: 50194 -> value:   45  ok
logical:  4708 (page:  18, offset: 100) ---> physical: 27748 -> value:    0  ok
logical: 28208 (page: 110, offset:  48) ---> physical: 34864 -> value:    0  ok
logical: 58844 (page: 229, offset: 220) ---> physical: 55260 -> value:    0  ok

logical: 22173 (page:  86, offset: 157) ---> physical: 56733 -> value:    0  ok
logical:  8535 (page:  33, offset:  87) ---> physical: 20055 -> value:   85  ok
logical: 42261 (page: 165, offset:  21) ---> physical: 10773 -> value:    0  ok
logical: 29687 (page: 115, offset: 247) ---> physical: 49655 -> value:   -3  ok
logical: 37799 (page: 147, offset: 167) ---> physical: 29351 -> value:  -23  ok

logical: 22566 (page:  88, offset:  38) ---> physical:  2854 -> value:   22  ok
logical: 62520 (page: 244, offset:  56) ---> physical:   312 -> value:    0  ok
logical:  4098 (page:  16, offset:   2) ---> physical: 50690 -> value:    4  ok
logical: 47999 (page: 187, offset: 127) ---> physical: 25727 -> value:  -33  ok
logical: 49660 (page: 193, offset: 252) ---> physical: 45564 -> value:    0  ok

logical: 37063 (page: 144, offset: 199) ---> physical: 18375 -> value:   49  ok
logical: 41856 (page: 163, offset: 128) ---> physical: 38784 -> value:    0  ok
logical:  5417 (page:  21, offset:  41) ---> physical: 21545 -> value:    0  ok
logical: 48856 (page: 190, offset: 216) ---> physical: 32984 -> value:    0  ok
logical: 10682 (page:  41, offset: 186) ---> physical: 27578 -> value:   10  ok

logical: 22370 (page:  87, offset:  98) ---> physical: 11362 -> value:   21  ok
logical: 63281 (page: 247, offset:  49) ---> physical: 16945 -> value:    0  ok
logical: 62452 (page: 243, offset: 244) ---> physical: 24052 -> value:    0  ok
logical: 50532 (page: 197, offset: 100) ---> physical: 18532 -> value:    0  ok
logical:  9022 (page:  35, offset:  62) ---> physical: 48190 -> value:    8  ok

logical: 59300 (page: 231, offset: 164) ---> physical: 10404 -> value:    0  ok
logical: 58660 (page: 229, offset:  36) ---> physical: 55076 -> value:    0  ok
logical: 56401 (page: 220, offset:  81) ---> physical: 51281 -> value:    0  ok
logical:  8518 (page:  33, offset:  70) ---> physical: 20038 -> value:    8  ok
logical: 63066 (page: 246, offset:  90) ---> physical: 22106 -> value:   61  ok

logical: 63250 (page: 247, offset:  18) ---> physical: 16914 -> value:   61  ok
logical: 48592 (page: 189, offset: 208) ---> physical:  2000 -> value:    0  ok
logical: 28771 (page: 112, offset:  99) ---> physical:  1379 -> value:   24  ok
logical: 37673 (page: 147, offset:  41) ---> physical: 29225 -> value:    0  ok
logical: 60776 (page: 237, offset: 104) ---> physical: 23656 -> value:    0  ok

logical: 56438 (page: 220, offset: 118) ---> physical: 51318 -> value:   55  ok
logical: 60424 (page: 236, offset:   8) ---> physical:  7176 -> value:    0  ok
logical: 39993 (page: 156, offset:  57) ---> physical:  1081 -> value:    0  ok
logical: 56004 (page: 218, offset: 196) ---> physical: 34756 -> value:    0  ok
logical: 59002 (page: 230, offset: 122) ---> physical: 14970 -> value:   57  ok

logical: 33982 (page: 132, offset: 190) ---> physical: 42174 -> value:   33  ok
logical: 25498 (page:  99, offset: 154) ---> physical: 60570 -> value:   24  ok
logical: 57047 (page: 222, offset: 215) ---> physical: 37591 -> value:  -75  ok
logical:  1401 (page:   5, offset: 121) ---> physical: 51577 -> value:    0  ok
logical: 15130 (page:  59, offset:  26) ---> physical: 32282 -> value:   14  ok

logical: 42960 (page: 167, offset: 208) ---> physical: 19664 -> value:    0  ok
logical: 61827 (page: 241, offset: 131) ---> physical: 22403 -> value:   96  ok
logical: 32442 (page: 126, offset: 186) ---> physical:  7098 -> value:   31  ok
logical: 64304 (page: 251, offset:  48) ---> physical:  4912 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical: 58945 -> value:    0  ok

logical: 38082 (page: 148, offset: 194) ---> physical: 37314 -> value:   37  ok
logical: 22404 (page:  87, offset: 132) ---> physical: 11396 -> value:    0  ok
logical:  3808 (page:  14, offset: 224) ---> physical: 10208 -> value:    0  ok
logical: 16883 (page:  65, offset: 243) ---> physical: 48115 -> value:  124  ok
logical: 23111 (page:  90, offset:  71) ---> physical: 35911 -> value: -111  ok

logical: 62417 (page: 243, offset: 209) ---> physical: 24017 -> value:    0  ok
logical: 60364 (page: 235, offset: 204) ---> physical: 57804 -> value:    0  ok
logical:  4542 (page:  17, offset: 190) ---> physical: 58814 -> value:    4  ok
logical: 14829 (page:  57, offset: 237) ---> physical: 33517 -> value:    0  ok
logical: 44964 (page: 175, offset: 164) ---> physical: 13220 -> value:    0  ok

logical: 33924 (page: 132, offset: 132) ---> physical: 42116 -> value:    0  ok
logical:  2141 (page:   8, offset:  93) ---> physical: 35165 -> value:    0  ok
logical: 19245 (page:  75, offset:  45) ---> physical: 14125 -> value:    0  ok
logical: 47168 (page: 184, offset:  64) ---> physical: 29760 -> value:    0  ok
logical: 24048 (page:  93, offset: 240) ---> physical: 39664 -> value:    0  ok

logical:  1022 (page:   3, offset: 254) ---> physical:  8190 -> value:    0  ok
logical: 23075 (page:  90, offset:  35) ---> physical: 35875 -> value: -120  ok
logical: 24888 (page:  97, offset:  56) ---> physical: 52536 -> value:    0  ok
logical: 49247 (page: 192, offset:  95) ---> physical:  8543 -> value:   23  ok
logical:  4900 (page:  19, offset:  36) ---> physical: 13604 -> value:    0  ok

logical: 22656 (page:  88, offset: 128) ---> physical:  2944 -> value:    0  ok
logical: 34117 (page: 133, offset:  69) ---> physical: 60997 -> value:    0  ok
logical: 55555 (page: 217, offset:   3) ---> physical: 59907 -> value:   64  ok
logical: 48947 (page: 191, offset:  51) ---> physical: 50483 -> value:  -52  ok
logical: 59533 (page: 232, offset: 141) ---> physical: 25997 -> value:    0  ok

logical: 21312 (page:  83, offset:  64) ---> physical:  9024 -> value:    0  ok
logical: 21415 (page:  83, offset: 167) ---> physical:  9127 -> value:  -23  ok
logical:   813 (page:   3, offset:  45) ---> physical:  7981 -> value:    0  ok
logical: 19419 (page:  75, offset: 219) ---> physical: 14299 -> value:  -10  ok
logical:  1999 (page:   7, offset: 207) ---> physical: 33231 -> value:  -13  ok

logical: 20155 (page:  78, offset: 187) ---> physical: 40635 -> value:  -82  ok
logical: 21521 (page:  84, offset:  17) ---> physical: 52241 -> value:    0  ok
logical: 13670 (page:  53, offset: 102) ---> physical: 53094 -> value:   13  ok
logical: 19289 (page:  75, offset:  89) ---> physical: 14169 -> value:    0  ok
logical: 58483 (page: 228, offset: 115) ---> physical: 15475 -> value:   28  ok

logical: 41318 (page: 161, offset: 102) ---> physical: 24934 -> value:   40  ok
logical: 16151 (page:  63, offset:  23) ---> physical: 55831 -> value:  -59  ok
logical: 13611 (page:  53, offset:  43) ---> physical: 53035 -> value:   74  ok
logical: 21514 (page:  84, offset:  10) ---> physical: 52234 -> value:   21  ok
logical: 13499 (page:  52, offset: 187) ---> physical: 36283 -> value:   46  ok

logical: 45583 (page: 178, offset:  15) ---> physical:  7695 -> value: -125  ok
logical: 49013 (page: 191, offset: 117) ---> physical: 50549 -> value:    0  ok
logical: 64843 (page: 253, offset:  75) ---> physical:  2123 -> value:   82  ok
logical: 63485 (page: 247, offset: 253) ---> physical: 17149 -> value:    0  ok
logical: 38697 (page: 151, offset:  41) ---> physical: 46889 -> value:    0  ok

logical: 59188 (page: 231, offset:  52) ---> physical: 10292 -> value:    0  ok
logical: 24593 (page:  96, offset:  17) ---> physical: 38417 -> value:    0  ok
logical: 57641 (page: 225, offset:  41) ---> physical: 61225 -> value:    0  ok
logical: 36524 (page: 142, offset: 172) ---> physical: 14508 -> value:    0  ok
logical: 56980 (page: 222, offset: 148) ---> physical: 37524 -> value:    0  ok

logical: 36810 (page: 143, offset: 202) ---> physical: 35786 -> value:   35  ok
logical:  6096 (page:  23, offset: 208) ---> physical:  9424 -> value:    0  ok
logical: 11070 (page:  43, offset:  62) ---> physical: 39998 -> value:   10  ok
logical: 60124 (page: 234, offset: 220) ---> physical: 10716 -> value:    0  ok
logical: 37576 (page: 146, offset: 200) ---> physical: 21448 -> value:    0  ok

logical: 15096 (page:  58, offset: 248) ---> physical: 24824 -> value:    0  ok
logical: 45247 (page: 176, offset: 191) ---> physical: 49087 -> value:   47  ok
logical: 32783 (page: 128, offset:  15) ---> physical:  4111 -> value:    3  ok
logical: 58390 (page: 228, offset:  22) ---> physical: 15382 -> value:   57  ok
logical: 60873 (page: 237, offset: 201) ---> physical: 23753 -> value:    0  ok

logical: 23719 (page:  92, offset: 167) ---> physical: 54951 -> value:   41  ok
logical: 24385 (page:  95, offset:  65) ---> physical:  1601 -> value:    0  ok
logical: 22307 (page:  87, offset:  35) ---> physical: 11299 -> value:  -56  ok
logical: 17375 (page:  67, offset: 223) ---> physical: 61663 -> value:   -9  ok
logical: 15990 (page:  62, offset: 118) ---> physical: 21110 -> value:   15  ok

logical: 20526 (page:  80, offset:  46) ---> physical: 56878 -> value:   20  ok
logical: 25904 (page: 101, offset:  48) ---> physical: 36656 -> value:    0  ok
logical: 42224 (page: 164, offset: 240) ---> physical: 46320 -> value:    0  ok
logical:  9311 (page:  36, offset:  95) ---> physical: 22879 -> value:   23  ok
logical:  7862 (page:  30, offset: 182) ---> physical: 58038 -> value:    7  ok

logical:  3835 (page:  14, offset: 251) ---> physical: 10235 -> value:  -66  ok
logical: 30535 (page: 119, offset:  71) ---> physical: 16199 -> value:  -47  ok
logical: 65179 (page: 254, offset: 155) ---> physical: 48539 -> value:  -90  ok
logical: 57387 (page: 224, offset:  43) ---> physical: 37931 -> value:   10  ok
logical: 63579 (page: 248, offset:  91) ---> physical: 28251 -> value:   22  ok

logical:  4946 (page:  19, offset:  82) ---> physical: 13650 -> value:    4  ok
logical:  9037 (page:  35, offset:  77) ---> physical: 48205 -> value:    0  ok
logical: 61033 (page: 238, offset: 105) ---> physical:  5993 -> value:    0  ok
logical: 55543 (page: 216, offset: 247) ---> physical: 58359 -> value:   61  ok
logical: 50361 (page: 196, offset: 185) ---> physical: 14009 -> value:    0  ok

logical:  6480 (page:  25, offset:  80) ---> physical: 29008 -> value:    0  ok
logical: 14042 (page:  54, offset: 218) ---> physical: 31450 -> value:   13  ok
logical: 21531 (page:  84, offset:  27) ---> physical: 52251 -> value:    6  ok
logical: 39195 (page: 153, offset:  27) ---> physical: 55579 -> value:   70  ok
logical: 37511 (page: 146, offset: 135) ---> physical: 21383 -> value:  -95  ok

logical: 23696 (page:  92, offset: 144) ---> physical: 54928 -> value:    0  ok
logical: 27440 (page: 107, offset:  48) ---> physical: 15664 -> value:    0  ok
logical: 28201 (page: 110, offset:  41) ---> physical: 34857 -> value:    0  ok
logical: 23072 (page:  90, offset:  32) ---> physical: 35872 -> value:    0  ok
logical:  7814 (page:  30, offset: 134) ---> physical: 57990 -> value:    7  ok

logical:  6552 (page:  25, offset: 152) ---> physical: 29080 -> value:    0  ok
logical: 43637 (page: 170, offset: 117) ---> physical: 12661 -> value:    0  ok
logical: 35113 (page: 137, offset:  41) ---> physical: 39721 -> value:    0  ok
logical: 34890 (page: 136, offset:  74) ---> physical: 26698 -> value:   34  ok
logical: 61297 (page: 239, offset: 113) ---> physical: 42353 -> value:    0  ok

logical: 45633 (page: 178, offset:  65) ---> physical:  7745 -> value:    0  ok
logical: 61431 (page: 239, offset: 247) ---> physical: 42487 -> value:   -3  ok
logical: 46032 (page: 179, offset: 208) ---> physical: 51152 -> value:    0  ok
logical: 18774 (page:  73, offset:  86) ---> physical: 61782 -> value:   18  ok
logical: 62991 (page: 246, offset:  15) ---> physical: 22031 -> value: -125  ok

logical: 28059 (page: 109, offset: 155) ---> physical:  3483 -> value:  102  ok
logical: 35229 (page: 137, offset: 157) ---> physical: 39837 -> value:    0  ok
logical: 51230 (page: 200, offset:  30) ---> physical: 32542 -> value:   50  ok
logical: 14405 (page:  56, offset:  69) ---> physical:  5701 -> value:    0  ok
logical: 52242 (page: 204, offset:  18) ---> physical: 46610 -> value:   51  ok

logical: 43153 (page: 168, offset: 145) ---> physical: 17553 -> value:    0  ok
logical:  2709 (page:  10, offset: 149) ---> physical: 53909 -> value:    0  ok
logical: 47963 (page: 187, offset:  91) ---> physical: 25691 -> value:  -42  ok
logical: 36943 (page: 144, offset:  79) ---> physical: 18255 -> value:   19  ok
logical: 54066 (page: 211, offset:  50) ---> physical: 51762 -> value:   52  ok

logical: 10054 (page:  39, offset:  70) ---> physical: 60230 -> value:    9  ok
logical: 43051 (page: 168, offset:  43) ---> physical: 17451 -> value:   10  ok
logical: 11525 (page:  45, offset:   5) ---> physical: 41477 -> value:    0  ok
logical: 17684 (page:  69, offset:  20) ---> physical:  9748 -> value:    0  ok
logical: 41681 (page: 162, offset: 209) ---> physical: 41937 -> value:    0  ok

logical: 27883 (page: 108, offset: 235) ---> physical: 44267 -> value:   58  ok
logical: 56909 (page: 222, offset:  77) ---> physical: 37453 -> value:    0  ok
logical: 45772 (page: 178, offset: 204) ---> physical:  7884 -> value:    0  ok
logical: 27496 (page: 107, offset: 104) ---> physical: 15720 -> value:    0  ok
logical: 46842 (page: 182, offset: 250) ---> physical: 55546 -> value:   45  ok

logical: 38734 (page: 151, offset:  78) ---> physical: 46926 -> value:   37  ok
logical: 28972 (page: 113, offset:  44) ---> physical: 16428 -> value:    0  ok
logical: 59684 (page: 233, offset:  36) ---> physical: 47396 -> value:    0  ok
logical: 11384 (page:  44, offset: 120) ---> physical: 54392 -> value:    0  ok
logical: 21018 (page:  82, offset:  26) ---> physical: 37658 -> value:   20  ok

logical:  2192 (page:   8, offset: 144) ---> physical: 35216 -> value:    0  ok
logical: 18384 (page:  71, offset: 208) ---> physical:  2512 -> value:    0  ok
logical: 13464 (page:  52, offset: 152) ---> physical: 36248 -> value:    0  ok
logical: 31018 (page: 121, offset:  42) ---> physical: 61994 -> value:   30  ok
logical: 62958 (page: 245, offset: 238) ---> physical: 28654 -> value:   61  ok

logical: 30611 (page: 119, offset: 147) ---> physical: 16275 -> value:  -28  ok
logical:  1913 (page:   7, offset: 121) ---> physical: 33145 -> value:    0  ok
logical: 18904 (page:  73, offset: 216) ---> physical: 61912 -> value:    0  ok
logical: 26773 (page: 104, offset: 149) ---> physical: 42645 -> value:    0  ok
logical: 55491 (page: 216, offset: 195) ---> physical: 58307 -> value:   48  ok

logical: 21899 (page:  85, offset: 139) ---> physical: 24203 -> value:   98  ok
logical: 64413 (page: 251, offset: 157) ---> physical:  5021 -> value:    0  ok
logical: 47134 (page: 184, offset:  30) ---> physical: 29726 -> value:   46  ok
logical: 23172 (page:  90, offset: 132) ---> physical: 35972 -> value:    0  ok
logical:  7262 (page:  28, offset:  94) ---> physical: 43102 -> value:    7  ok

logical: 12705 (page:  49, offset: 161) ---> physical: 31137 -> value:    0  ok
logical:  7522 (page:  29, offset:  98) ---> physical:  6242 -> value:    7  ok
logical: 58815 (page: 229, offset: 191) ---> physical: 55231 -> value:  111  ok
logical: 34916 (page: 136, offset: 100) ---> physical: 26724 -> value:    0  ok
logical:  3802 (page:  14, offset: 218) ---> physical: 10202 -> value:    3  ok

logical: 58008 (page: 226, offset: 152) ---> physical:  3224 -> value:    0  ok
logical:  1239 (page:   4, offset: 215) ---> physical: 40919 -> value:   53  ok
logical: 63947 (page: 249, offset: 203) ---> physical: 21963 -> value:  114  ok
logical:   381 (page:   1, offset: 125) ---> physical: 57213 -> value:    0  ok
logical: 60734 (page: 237, offset:  62) ---> physical: 23614 -> value:   59  ok

logical: 48769 (page: 190, offset: 129) ---> physical: 32897 -> value:    0  ok
logical: 41938 (page: 163, offset: 210) ---> physical: 38866 -> value:   40  ok
logical: 38025 (page: 148, offset: 137) ---> physical: 37257 -> value:    0  ok
logical: 55099 (page: 215, offset:  59) ---> physical:  5179 -> value:  -50  ok
logical: 56691 (page: 221, offset: 115) ---> physical: 13427 -> value:   92  ok

logical: 39530 (page: 154, offset: 106) ---> physical: 43370 -> value:   38  ok
logical: 59003 (page: 230, offset: 123) ---> physical: 14971 -> value:  -98  ok
logical:  6029 (page:  23, offset: 141) ---> physical:  9357 -> value:    0  ok
logical: 20920 (page:  81, offset: 184) ---> physical: 39096 -> value:    0  ok
logical:  8077 (page:  31, offset: 141) ---> physical: 33933 -> value:    0  ok

logical: 42633 (page: 166, offset: 137) ---> physical: 20873 -> value:    0  ok
logical: 17443 (page:  68, offset:  35) ---> physical: 40995 -> value:    8  ok
logical: 53570 (page: 209, offset:  66) ---> physical:   834 -> value:   52  ok
logical: 22833 (page:  89, offset:  49) ---> physical: 52785 -> value:    0  ok
logical:  3782 (page:  14, offset: 198) ---> physical: 10182 -> value:    3  ok

logical: 47758 (page: 186, offset: 142) ---> physical: 56462 -> value:   46  ok
logical: 22136 (page:  86, offset: 120) ---> physical: 56696 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical: 11419 -> value:  -26  ok
logical: 23867 (page:  93, offset:  59) ---> physical: 39483 -> value:   78  ok
logical: 59968 (page: 234, offset:  64) ---> physical: 10560 -> value:    0  ok

logical: 62166 (page: 242, offset: 214) ---> physical: 62422 -> value:   60  ok
logical:  6972 (page:  27, offset:  60) ---> physical: 26428 -> value:    0  ok
logical: 63684 (page: 248, offset: 196) ---> physical: 28356 -> value:    0  ok
logical: 46388 (page: 181, offset:  52) ---> physical: 50228 -> value:    0  ok
logical: 41942 (page: 163, offset: 214) ---> physical: 38870 -> value:   40  ok

logical: 36524 (page: 142, offset: 172) ---> physical: 14508 -> value:    0  ok
logical:  9323 (page:  36, offset: 107) ---> physical: 22891 -> value:   26  ok
logical: 31114 (page: 121, offset: 138) ---> physical: 62090 -> value:   30  ok
logical: 22345 (page:  87, offset:  73) ---> physical: 11337 -> value:    0  ok
logical: 46463 (page: 181, offset: 127) ---> physical: 50303 -> value:   95  ok

logical: 54671 (page: 213, offset: 143) ---> physical: 48783 -> value:   99  ok
logical:  9214 (page:  35, offset: 254) ---> physical: 48382 -> value:    8  ok
logical:  7257 (page:  28, offset:  89) ---> physical: 43097 -> value:    0  ok
logical: 33150 (page: 129, offset: 126) ---> physical: 53630 -> value:   32  ok
logical: 41565 (page: 162, offset:  93) ---> physical: 41821 -> value:    0  ok

logical: 26214 (page: 102, offset: 102) ---> physical: 49254 -> value:   25  ok
logical:  3595 (page:  14, offset:  11) ---> physical:  9995 -> value: -126  ok
logical: 17932 (page:  70, offset:  12) ---> physical: 18700 -> value:    0  ok
logical: 34660 (page: 135, offset: 100) ---> physical: 18020 -> value:    0  ok
logical: 51961 (page: 202, offset: 249) ---> physical: 27385 -> value:    0  ok

logical: 58634 (page: 229, offset:  10) ---> physical: 55050 -> value:   57  ok
logical: 57990 (page: 226, offset: 134) ---> physical:  3206 -> value:   56  ok
logical: 28848 (page: 112, offset: 176) ---> physical:  1456 -> value:    0  ok
logical: 49920 (page: 195, offset:   0) ---> physical: 59136 -> value:    0  ok
logical: 18351 (page:  71, offset: 175) ---> physical:  2479 -> value:  -21  ok

logical: 53669 (page: 209, offset: 165) ---> physical:   933 -> value:    0  ok
logical: 33996 (page: 132, offset: 204) ---> physical: 42188 -> value:    0  ok
logical:  6741 (page:  26, offset:  85) ---> physical:  6741 -> value:    0  ok
logical: 64098 (page: 250, offset:  98) ---> physical:  4450 -> value:   62  ok
logical:   606 (page:   2, offset:  94) ---> physical: 17246 -> value:    0  ok

logical: 27383 (page: 106, offset: 247) ---> physical: 28919 -> value:  -67  ok
logical: 63140 (page: 246, offset: 164) ---> physical: 22180 -> value:    0  ok
logical: 32228 (page: 125, offset: 228) ---> physical: 49892 -> value:    0  ok
logical: 63437 (page: 247, offset: 205) ---> physical: 17101 -> value:    0  ok
logical: 29085 (page: 113, offset: 157) ---> physical: 16541 -> value:    0  ok

logical: 65080 (page: 254, offset:  56) ---> physical: 48440 -> value:    0  ok
logical: 38753 (page: 151, offset:  97) ---> physical: 46945 -> value:    0  ok
logical: 16041 (page:  62, offset: 169) ---> physical: 21161 -> value:    0  ok
logical:  9041 (page:  35, offset:  81) ---> physical: 48209 -> value:    0  ok
logical: 42090 (page: 164, offset: 106) ---> physical: 46186 -> value:   41  ok

logical: 46388 (page: 181, offset:  52) ---> physical: 50228 -> value:    0  ok
logical: 63650 (page: 248, offset: 162) ---> physical: 28322 -> value:   62  ok
logical: 36636 (page: 143, offset:  28) ---> physical: 35612 -> value:    0  ok
logical: 21947 (page:  85, offset: 187) ---> physical: 24251 -> value:  110  ok
logical: 19833 (page:  77, offset: 121) ---> physical: 36473 -> value:    0  ok

logical: 36464 (page: 142, offset: 112) ---> physical: 14448 -> value:    0  ok
logical:  8541 (page:  33, offset:  93) ---> physical: 20061 -> value:    0  ok
logical: 12712 (page:  49, offset: 168) ---> physical: 31144 -> value:    0  ok
logical: 48955 (page: 191, offset:  59) ---> physical: 50491 -> value:  -50  ok
logical: 39206 (page: 153, offset:  38) ---> physical: 55590 -> value:   38  ok

logical: 15578 (page:  60, offset: 218) ---> physical: 31962 -> value:   15  ok
logical: 49205 (page: 192, offset:  53) ---> physical:  8501 -> value:    0  ok
logical:  7731 (page:  30, offset:  51) ---> physical: 57907 -> value: -116  ok
logical: 43046 (page: 168, offset:  38) ---> physical: 17446 -> value:   42  ok
logical: 60498 (page: 236, offset:  82) ---> physical:  7250 -> value:   59  ok

logical:  9237 (page:  36, offset:  21) ---> physical: 22805 -> value:    0  ok
logical: 47706 (page: 186, offset:  90) ---> physical: 56410 -> value:   46  ok
logical: 43973 (page: 171, offset: 197) ---> physical: 57541 -> value:    0  ok
logical: 42008 (page: 164, offset:  24) ---> physical: 46104 -> value:    0  ok
logical: 27460 (page: 107, offset:  68) ---> physical: 15684 -> value:    0  ok

logical: 24999 (page:  97, offset: 167) ---> physical: 52647 -> value:  105  ok
logical: 51933 (page: 202, offset: 221) ---> physical: 27357 -> value:    0  ok
logical: 34070 (page: 133, offset:  22) ---> physical: 60950 -> value:   33  ok
logical: 65155 (page: 254, offset: 131) ---> physical: 48515 -> value:  -96  ok
logical: 59955 (page: 234, offset:  51) ---> physical: 10547 -> value: -116  ok

logical:  9277 (page:  36, offset:  61) ---> physical: 22845 -> value:    0  ok
logical: 20420 (page:  79, offset: 196) ---> physical: 16836 -> value:    0  ok
logical: 44860 (page: 175, offset:  60) ---> physical: 13116 -> value:    0  ok
logical: 50992 (page: 199, offset:  48) ---> physical: 42800 -> value:    0  ok
logical: 10583 (page:  41, offset:  87) ---> physical: 27479 -> value:   85  ok

logical: 57751 (page: 225, offset: 151) ---> physical: 61335 -> value:  101  ok
logical: 23195 (page:  90, offset: 155) ---> physical: 35995 -> value:  -90  ok
logical: 27227 (page: 106, offset:  91) ---> physical: 28763 -> value: -106  ok
logical: 42816 (page: 167, offset:  64) ---> physical: 19520 -> value:    0  ok
logical: 58219 (page: 227, offset: 107) ---> physical: 34155 -> value:  -38  ok

logical: 37606 (page: 146, offset: 230) ---> physical: 21478 -> value:   36  ok
logical: 18426 (page:  71, offset: 250) ---> physical:  2554 -> value:   17  ok
logical: 21238 (page:  82, offset: 246) ---> physical: 37878 -> value:   20  ok
logical: 11983 (page:  46, offset: 207) ---> physical: 59855 -> value:  -77  ok
logical: 48394 (page: 189, offset:  10) ---> physical:  1802 -> value:   47  ok

logical: 11036 (page:  43, offset:  28) ---> physical: 39964 -> value:    0  ok
logical: 30557 (page: 119, offset:  93) ---> physical: 16221 -> value:    0  ok
logical: 23453 (page:  91, offset: 157) ---> physical: 20637 -> value:    0  ok
logical: 49847 (page: 194, offset: 183) ---> physical: 31671 -> value:  -83  ok
logical: 30032 (page: 117, offset:  80) ---> physical:   592 -> value:    0  ok

logical: 48065 (page: 187, offset: 193) ---> physical: 25793 -> value:    0  ok
logical:  6957 (page:  27, offset:  45) ---> physical: 26413 -> value:    0  ok
logical:  2301 (page:   8, offset: 253) ---> physical: 35325 -> value:    0  ok
logical:  7736 (page:  30, offset:  56) ---> physical: 57912 -> value:    0  ok
logical: 31260 (page: 122, offset:  28) ---> physical: 23324 -> value:    0  ok

logical: 17071 (page:  66, offset: 175) ---> physical:   175 -> value:  -85  ok
logical:  8940 (page:  34, offset: 236) ---> physical: 46572 -> value:    0  ok
logical:  9929 (page:  38, offset: 201) ---> physical: 44745 -> value:    0  ok
logical: 45563 (page: 177, offset: 251) ---> physical: 46075 -> value:  126  ok
logical: 12107 (page:  47, offset:  75) ---> physical:  2635 -> value:  -46  ok

ALL logical ---> physical assertions PASSED!
ALL read memory value assertions PASSED!

		... nPages == nFrames memory simulation done.

 Starting nPages != nFrames memory simulation...
logical: 16916 (page:  66, offset:  20) ---> physical:    20 -> value:    0  ok
logical: 62493 (page: 244, offset:  29) ---> physical:   285 -> value:    0  ok
logical: 30198 (page: 117, offset: 246) ---> physical:   758 -> value:   29  ok
logical: 53683 (page: 209, offset: 179) ---> physical:   947 -> value:  108  ok
logical: 40185 (page: 156, offset: 249) ---> physical:  1273 -> value:    0  ok

logical: 28781 (page: 112, offset: 109) ---> physical:  1389 -> value:    0  ok
logical: 24462 (page:  95, offset: 142) ---> physical:  1678 -> value:   23  ok
logical: 48399 (page: 189, offset:  15) ---> physical:  1807 -> value:   67  ok
logical: 64815 (page: 253, offset:  47) ---> physical:  2095 -> value:   75  ok
logical: 18295 (page:  71, offset: 119) ---> physical:  2423 -> value:  -35  ok

logical: 12218 (page:  47, offset: 186) ---> physical:  2746 -> value:   11  ok
logical: 22760 (page:  88, offset: 232) ---> physical:  3048 -> value:    0  ok
logical: 57982 (page: 226, offset: 126) ---> physical:  3198 -> value:   56  ok
logical: 27966 (page: 109, offset:  62) ---> physical:  3390 -> value:   27  ok
logical: 54894 (page: 214, offset: 110) ---> physical:  3694 -> value:   53  ok

logical: 38929 (page: 152, offset:  17) ---> physical:  3857 -> value:    0  ok
logical: 32865 (page: 128, offset:  97) ---> physical:  4193 -> value:    0  ok
logical: 64243 (page: 250, offset: 243) ---> physical:  4595 -> value:  -68  ok
logical:  2315 (page:   9, offset:  11) ---> physical:  4619 -> value:   66  ok
logical: 64454 (page: 251, offset: 198) ---> physical:  5062 -> value:   62  ok

logical: 55041 (page: 215, offset:   1) ---> physical:  5121 -> value:    0  ok
logical: 18633 (page:  72, offset: 201) ---> physical:  5577 -> value:    0  ok
logical: 14557 (page:  56, offset: 221) ---> physical:  5853 -> value:    0  ok
logical: 61006 (page: 238, offset:  78) ---> physical:  5966 -> value:   59  ok
logical: 62615 (page: 244, offset: 151) ---> physical:   407 -> value:   37  ok

logical:  7591 (page:  29, offset: 167) ---> physical:  6311 -> value:  105  ok
logical: 64747 (page: 252, offset: 235) ---> physical:  6635 -> value:   58  ok
logical:  6727 (page:  26, offset:  71) ---> physical:  6727 -> value: -111  ok
logical: 32315 (page: 126, offset:  59) ---> physical:  6971 -> value: -114  ok
logical: 60645 (page: 236, offset: 229) ---> physical:  7397 -> value:    0  ok

logical:  6308 (page:  24, offset: 164) ---> physical:  7588 -> value:    0  ok
logical: 45688 (page: 178, offset: 120) ---> physical:  7800 -> value:    0  ok
logical:   969 (page:   3, offset: 201) ---> physical:  8137 -> value:    0  ok
logical: 40891 (page: 159, offset: 187) ---> physical:  8379 -> value:  -18  ok
logical: 49294 (page: 192, offset: 142) ---> physical:  8590 -> value:   48  ok

logical: 41118 (page: 160, offset: 158) ---> physical:  8862 -> value:   40  ok
logical: 21395 (page:  83, offset: 147) ---> physical:  9107 -> value:  -28  ok
logical:  6091 (page:  23, offset: 203) ---> physical:  9419 -> value:  -14  ok
logical: 32541 (page: 127, offset:  29) ---> physical:  9501 -> value:    0  ok
logical: 17665 (page:  69, offset:   1) ---> physical:  9729 -> value:    0  ok

logical:  3784 (page:  14, offset: 200) ---> physical: 10184 -> value:    0  ok
logical: 28718 (page: 112, offset:  46) ---> physical:  1326 -> value:   28  ok
logical: 59240 (page: 231, offset: 104) ---> physical: 10344 -> value:    0  ok
logical: 40178 (page: 156, offset: 242) ---> physical:  1266 -> value:   39  ok
logical: 60086 (page: 234, offset: 182) ---> physical: 10678 -> value:   58  ok

logical: 42252 (page: 165, offset:  12) ---> physical: 10764 -> value:    0  ok
logical: 44770 (page: 174, offset: 226) ---> physical: 11234 -> value:   43  ok
logical: 22514 (page:  87, offset: 242) ---> physical: 11506 -> value:   21  ok
logical:  3067 (page:  11, offset: 251) ---> physical: 11771 -> value:   -2  ok
logical: 15757 (page:  61, offset: 141) ---> physical: 11917 -> value:    0  ok

logical: 31649 (page: 123, offset: 161) ---> physical: 12193 -> value:    0  ok
logical: 10842 (page:  42, offset:  90) ---> physical: 12378 -> value:   10  ok
logical: 43765 (page: 170, offset: 245) ---> physical: 12789 -> value:    0  ok
logical: 33405 (page: 130, offset: 125) ---> physical: 12925 -> value:    0  ok
logical: 44954 (page: 175, offset: 154) ---> physical: 13210 -> value:   43  ok

logical: 56657 (page: 221, offset:  81) ---> physical: 13393 -> value:    0  ok
logical:  5003 (page:  19, offset: 139) ---> physical: 13707 -> value:  -30  ok
logical: 50227 (page: 196, offset:  51) ---> physical: 13875 -> value:   12  ok
logical: 19358 (page:  75, offset: 158) ---> physical: 14238 -> value:   18  ok
logical: 36529 (page: 142, offset: 177) ---> physical: 14513 -> value:    0  ok

logical: 10392 (page:  40, offset: 152) ---> physical: 14744 -> value:    0  ok
logical: 58882 (page: 230, offset:   2) ---> physical: 14850 -> value:   57  ok
logical:  5129 (page:  20, offset:   9) ---> physical: 15113 -> value:    0  ok
logical: 58554 (page: 228, offset: 186) ---> physical: 15546 -> value:   57  ok
logical: 58584 (page: 228, offset: 216) ---> physical: 15576 -> value:    0  ok

logical: 27444 (page: 107, offset:  52) ---> physical: 15668 -> value:    0  ok
logical: 58982 (page: 230, offset: 102) ---> physical: 14950 -> value:   57  ok
logical: 51476 (page: 201, offset:  20) ---> physical: 15892 -> value:    0  ok
logical:  6796 (page:  26, offset: 140) ---> physical:  6796 -> value:    0  ok
logical: 21311 (page:  83, offset:  63) ---> physical:  9023 -> value:  -49  ok

logical: 30705 (page: 119, offset: 241) ---> physical: 16369 -> value:    0  ok
logical: 28964 (page: 113, offset:  36) ---> physical: 16420 -> value:    0  ok
logical: 41003 (page: 160, offset:  43) ---> physical:  8747 -> value:   10  ok
logical: 20259 (page:  79, offset:  35) ---> physical: 16675 -> value:  -56  ok
logical: 57857 (page: 226, offset:   1) ---> physical:  3073 -> value:    0  ok

logical: 63258 (page: 247, offset:  26) ---> physical: 16922 -> value:   61  ok
logical: 36374 (page: 142, offset:  22) ---> physical: 14358 -> value:   35  ok
logical:   692 (page:   2, offset: 180) ---> physical: 17332 -> value:    0  ok
logical: 43121 (page: 168, offset: 113) ---> physical: 17521 -> value:    0  ok
logical: 48128 (page: 188, offset:   0) ---> physical: 17664 -> value:    0  ok

logical: 34561 (page: 135, offset:   1) ---> physical: 17921 -> value:    0  ok
logical: 49213 (page: 192, offset:  61) ---> physical:  8509 -> value:    0  ok
logical: 36922 (page: 144, offset:  58) ---> physical: 18234 -> value:   36  ok
logical: 59162 (page: 231, offset:  26) ---> physical: 10266 -> value:   57  ok
logical: 50552 (page: 197, offset: 120) ---> physical: 18552 -> value:    0  ok

logical: 17866 (page:  69, offset: 202) ---> physical:  9930 -> value:   17  ok
logical: 18145 (page:  70, offset: 225) ---> physical: 18913 -> value:    0  ok
logical:  3884 (page:  15, offset:  44) ---> physical: 18988 -> value:    0  ok
logical: 54388 (page: 212, offset: 116) ---> physical: 19316 -> value:    0  ok
logical: 42932 (page: 167, offset: 180) ---> physical: 19636 -> value:    0  ok

logical: 46919 (page: 183, offset:  71) ---> physical: 19783 -> value:  -47  ok
logical: 58892 (page: 230, offset:  12) ---> physical: 14860 -> value:    0  ok
logical:  8620 (page:  33, offset: 172) ---> physical: 20140 -> value:    0  ok
logical: 38336 (page: 149, offset: 192) ---> physical: 20416 -> value:    0  ok
logical: 64357 (page: 251, offset: 101) ---> physical:  4965 -> value:    0  ok

logical: 23387 (page:  91, offset:  91) ---> physical: 20571 -> value:  -42  ok
logical: 42632 (page: 166, offset: 136) ---> physical: 20872 -> value:    0  ok
logical: 15913 (page:  62, offset:  41) ---> physical: 21033 -> value:    0  ok
logical: 15679 (page:  61, offset:  63) ---> physical: 11839 -> value:   79  ok
logical: 22501 (page:  87, offset: 229) ---> physical: 11493 -> value:    0  ok

logical: 37540 (page: 146, offset: 164) ---> physical: 21412 -> value:    0  ok
logical:  5527 (page:  21, offset: 151) ---> physical: 21655 -> value:  101  ok
logical: 63921 (page: 249, offset: 177) ---> physical: 21937 -> value:    0  ok
logical: 62716 (page: 244, offset: 252) ---> physical:   508 -> value:    0  ok
logical: 32874 (page: 128, offset: 106) ---> physical:  4202 -> value:   32  ok

logical: 64390 (page: 251, offset: 134) ---> physical:  4998 -> value:   62  ok
logical: 63101 (page: 246, offset: 125) ---> physical: 22141 -> value:    0  ok
logical: 61802 (page: 241, offset: 106) ---> physical: 22378 -> value:   60  ok
logical: 19648 (page:  76, offset: 192) ---> physical: 22720 -> value:    0  ok
logical: 29031 (page: 113, offset: 103) ---> physical: 16487 -> value:   89  ok

logical: 44981 (page: 175, offset: 181) ---> physical: 13237 -> value:    0  ok
logical: 28092 (page: 109, offset: 188) ---> physical:  3516 -> value:    0  ok
logical:  9448 (page:  36, offset: 232) ---> physical: 23016 -> value:    0  ok
logical: 44744 (page: 174, offset: 200) ---> physical: 11208 -> value:    0  ok
logical: 61496 (page: 240, offset:  56) ---> physical: 23096 -> value:    0  ok

logical: 31453 (page: 122, offset: 221) ---> physical: 23517 -> value:    0  ok
logical: 60746 (page: 237, offset:  74) ---> physical: 23626 -> value:   59  ok
logical: 12199 (page:  47, offset: 167) ---> physical:  2727 -> value:  -23  ok
logical: 62255 (page: 243, offset:  47) ---> physical: 23855 -> value:  -53  ok
logical: 21793 (page:  85, offset:  33) ---> physical: 24097 -> value:    0  ok

logical: 26544 (page: 103, offset: 176) ---> physical: 24496 -> value:    0  ok
logical: 14964 (page:  58, offset: 116) ---> physical: 24692 -> value:    0  ok
logical: 41462 (page: 161, offset: 246) ---> physical: 25078 -> value:   40  ok
logical: 56089 (page: 219, offset:  25) ---> physical: 25113 -> value:    0  ok
logical: 52038 (page: 203, offset:  70) ---> physical: 25414 -> value:   50  ok

logical: 47982 (page: 187, offset: 110) ---> physical: 25710 -> value:   46  ok
logical: 59484 (page: 232, offset:  92) ---> physical: 25948 -> value:    0  ok
logical: 50924 (page: 198, offset: 236) ---> physical: 26348 -> value:    0  ok
logical:  6942 (page:  27, offset:  30) ---> physical: 26398 -> value:    6  ok
logical: 34998 (page: 136, offset: 182) ---> physical: 26806 -> value:   34  ok

logical: 27069 (page: 105, offset: 189) ---> physical: 27069 -> value:    0  ok
logical: 51926 (page: 202, offset: 214) ---> physical: 27350 -> value:   50  ok
logical: 60645 (page: 236, offset: 229) ---> physical:  7397 -> value:    0  ok
logical: 43181 (page: 168, offset: 173) ---> physical: 17581 -> value:    0  ok
logical: 10559 (page:  41, offset:  63) ---> physical: 27455 -> value:   79  ok

logical:  4664 (page:  18, offset:  56) ---> physical: 27704 -> value:    0  ok
logical: 28578 (page: 111, offset: 162) ---> physical: 28066 -> value:   27  ok
logical: 59516 (page: 232, offset: 124) ---> physical: 25980 -> value:    0  ok
logical: 38912 (page: 152, offset:   0) ---> physical:  3840 -> value:    0  ok
logical: 63562 (page: 248, offset:  74) ---> physical: 28234 -> value:   62  ok

logical: 64846 (page: 253, offset:  78) ---> physical:  2126 -> value:   63  ok
logical: 62938 (page: 245, offset: 218) ---> physical: 28634 -> value:   61  ok
logical: 27194 (page: 106, offset:  58) ---> physical: 28730 -> value:   26  ok
logical: 28804 (page: 112, offset: 132) ---> physical:  1412 -> value:    0  ok
logical: 61703 (page: 241, offset:   7) ---> physical: 22279 -> value:   65  ok

logical: 10998 (page:  42, offset: 246) ---> physical: 12534 -> value:   10  ok
logical:  6596 (page:  25, offset: 196) ---> physical: 29124 -> value:    0  ok
logical: 37721 (page: 147, offset:  89) ---> physical: 29273 -> value:    0  ok
logical: 43430 (page: 169, offset: 166) ---> physical: 29606 -> value:   42  ok
logical: 22692 (page:  88, offset: 164) ---> physical:  2980 -> value:    0  ok

logical: 62971 (page: 245, offset: 251) ---> physical: 28667 -> value:  126  ok
logical: 47125 (page: 184, offset:  21) ---> physical: 29717 -> value:    0  ok
logical: 52521 (page: 205, offset:  41) ---> physical: 29993 -> value:    0  ok
logical: 34646 (page: 135, offset:  86) ---> physical: 18006 -> value:   33  ok
logical: 32889 (page: 128, offset: 121) ---> physical:  4217 -> value:    0  ok

logical: 13055 (page:  50, offset: 255) ---> physical: 30463 -> value:  -65  ok
logical: 65416 (page: 255, offset: 136) ---> physical: 30600 -> value:    0  ok
logical: 62869 (page: 245, offset: 149) ---> physical: 28565 -> value:    0  ok
logical: 57314 (page: 223, offset: 226) ---> physical: 30946 -> value:   55  ok
logical: 12659 (page:  49, offset: 115) ---> physical: 31091 -> value:   92  ok

logical: 14052 (page:  54, offset: 228) ---> physical: 31460 -> value:    0  ok
logical: 32956 (page: 128, offset: 188) ---> physical:  4284 -> value:    0  ok
logical: 49273 (page: 192, offset: 121) ---> physical:  8569 -> value:    0  ok
logical: 50352 (page: 196, offset: 176) ---> physical: 14000 -> value:    0  ok
logical: 49737 (page: 194, offset:  73) ---> physical: 31561 -> value:    0  ok

logical: 15555 (page:  60, offset: 195) ---> physical: 31939 -> value:   48  ok
logical: 47475 (page: 185, offset: 115) ---> physical: 32115 -> value:   92  ok
logical: 15328 (page:  59, offset: 224) ---> physical: 32480 -> value:    0  ok
logical: 34621 (page: 135, offset:  61) ---> physical: 17981 -> value:    0  ok
logical: 51365 (page: 200, offset: 165) ---> physical: 32677 -> value:    0  ok

logical: 32820 (page: 128, offset:  52) ---> physical:  4148 -> value:    0  ok
logical: 48855 (page: 190, offset: 215) ---> physical:   215 -> value:  -75  ok
logical: 12224 (page:  47, offset: 192) ---> physical:  2752 -> value:    0  ok
logical:  2035 (page:   7, offset: 243) ---> physical:   499 -> value:   -4  ok
logical: 60539 (page: 236, offset: 123) ---> physical:  7291 -> value:   30  ok

logical: 14595 (page:  57, offset:   3) ---> physical:   515 -> value:   64  ok
logical: 13853 (page:  54, offset:  29) ---> physical: 31261 -> value:    0  ok
logical: 24143 (page:  94, offset:  79) ---> physical:   847 -> value: -109  ok
logical: 15216 (page:  59, offset: 112) ---> physical: 32368 -> value:    0  ok
logical:  8113 (page:  31, offset: 177) ---> physical:  1201 -> value:    0  ok

logical: 22640 (page:  88, offset: 112) ---> physical:  2928 -> value:    0  ok
logical: 32978 (page: 128, offset: 210) ---> physical:  4306 -> value:   32  ok
logical: 39151 (page: 152, offset: 239) ---> physical:  4079 -> value:   59  ok
logical: 19520 (page:  76, offset:  64) ---> physical: 22592 -> value:    0  ok
logical: 58141 (page: 227, offset:  29) ---> physical:  1309 -> value:    0  ok

logical: 63959 (page: 249, offset: 215) ---> physical: 21975 -> value:  117  ok
logical: 53040 (page: 207, offset:  48) ---> physical:  1584 -> value:    0  ok
logical: 55842 (page: 218, offset:  34) ---> physical:  1826 -> value:   54  ok
logical:   585 (page:   2, offset:  73) ---> physical: 17225 -> value:    0  ok
logical: 51229 (page: 200, offset:  29) ---> physical: 32541 -> value:    0  ok

logical: 64181 (page: 250, offset: 181) ---> physical:  4533 -> value:    0  ok
logical: 54879 (page: 214, offset:  95) ---> physical:  3679 -> value: -105  ok
logical: 28210 (page: 110, offset:  50) ---> physical:  2098 -> value:   27  ok
logical: 10268 (page:  40, offset:  28) ---> physical: 14620 -> value:    0  ok
logical: 15395 (page:  60, offset:  35) ---> physical: 31779 -> value:    8  ok

logical: 12884 (page:  50, offset:  84) ---> physical: 30292 -> value:    0  ok
logical:  2149 (page:   8, offset: 101) ---> physical:  2405 -> value:    0  ok
logical: 53483 (page: 208, offset: 235) ---> physical:  2795 -> value:   58  ok
logical: 59606 (page: 232, offset: 214) ---> physical: 26070 -> value:   58  ok
logical: 14981 (page:  58, offset: 133) ---> physical: 24709 -> value:    0  ok

logical: 36672 (page: 143, offset:  64) ---> physical:  2880 -> value:    0  ok
logical: 23197 (page:  90, offset: 157) ---> physical:  3229 -> value:    0  ok
logical: 36518 (page: 142, offset: 166) ---> physical: 14502 -> value:   35  ok
logical: 13361 (page:  52, offset:  49) ---> physical:  3377 -> value:    0  ok
logical: 19810 (page:  77, offset:  98) ---> physical:  3682 -> value:   19  ok

logical: 25955 (page: 101, offset:  99) ---> physical:  3939 -> value:   88  ok
logical: 62678 (page: 244, offset: 214) ---> physical:  4310 -> value:   61  ok
logical: 26021 (page: 101, offset: 165) ---> physical:  4005 -> value:    0  ok
logical: 29409 (page: 114, offset: 225) ---> physical:  4577 -> value:    0  ok
logical: 38111 (page: 148, offset: 223) ---> physical:  4831 -> value:   55  ok

logical: 58573 (page: 228, offset: 205) ---> physical: 15565 -> value:    0  ok
logical: 56840 (page: 222, offset:   8) ---> physical:  4872 -> value:    0  ok
logical: 41306 (page: 161, offset:  90) ---> physical: 24922 -> value:   40  ok
logical: 54426 (page: 212, offset: 154) ---> physical: 19354 -> value:   53  ok
logical:  3617 (page:  14, offset:  33) ---> physical: 10017 -> value:    0  ok

logical: 50652 (page: 197, offset: 220) ---> physical: 18652 -> value:    0  ok
logical: 41452 (page: 161, offset: 236) ---> physical: 25068 -> value:    0  ok
logical: 20241 (page:  79, offset:  17) ---> physical: 16657 -> value:    0  ok
logical: 31723 (page: 123, offset: 235) ---> physical: 12267 -> value:   -6  ok
logical: 53747 (page: 209, offset: 243) ---> physical:  5363 -> value:  124  ok

logical: 28550 (page: 111, offset: 134) ---> physical: 28038 -> value:   27  ok
logical: 23402 (page:  91, offset: 106) ---> physical: 20586 -> value:   22  ok
logical: 21205 (page:  82, offset: 213) ---> physical:  5589 -> value:    0  ok
logical: 56181 (page: 219, offset: 117) ---> physical: 25205 -> value:    0  ok
logical: 57470 (page: 224, offset: 126) ---> physical:  5758 -> value:   56  ok

logical: 39933 (page: 155, offset: 253) ---> physical:  6141 -> value:    0  ok
logical: 34964 (page: 136, offset: 148) ---> physical: 26772 -> value:    0  ok
logical: 24781 (page:  96, offset: 205) ---> physical:  6349 -> value:    0  ok
logical: 41747 (page: 163, offset:  19) ---> physical:  6419 -> value:  -60  ok
logical: 62564 (page: 244, offset: 100) ---> physical:  4196 -> value:    0  ok

logical: 58461 (page: 228, offset:  93) ---> physical: 15453 -> value:    0  ok
logical: 20858 (page:  81, offset: 122) ---> physical:  6778 -> value:   20  ok
logical: 49301 (page: 192, offset: 149) ---> physical:  8597 -> value:    0  ok
logical: 40572 (page: 158, offset: 124) ---> physical:  7036 -> value:    0  ok
logical: 23840 (page:  93, offset:  32) ---> physical:  7200 -> value:    0  ok

logical: 35278 (page: 137, offset: 206) ---> physical:  7630 -> value:   34  ok
logical: 62905 (page: 245, offset: 185) ---> physical: 28601 -> value:    0  ok
logical: 56650 (page: 221, offset:  74) ---> physical: 13386 -> value:   55  ok
logical: 11149 (page:  43, offset: 141) ---> physical:  7821 -> value:    0  ok
logical: 38920 (page: 152, offset:   8) ---> physical:  7944 -> value:    0  ok

logical: 23430 (page:  91, offset: 134) ---> physical: 20614 -> value:   22  ok
logical: 57592 (page: 224, offset: 248) ---> physical:  5880 -> value:    0  ok
logical:  3080 (page:  12, offset:   8) ---> physical:  8200 -> value:    0  ok
logical:  6677 (page:  26, offset:  21) ---> physical:  8469 -> value:    0  ok
logical: 50704 (page: 198, offset:  16) ---> physical: 26128 -> value:    0  ok

logical: 51883 (page: 202, offset: 171) ---> physical: 27307 -> value:  -86  ok
logical: 62799 (page: 245, offset:  79) ---> physical: 28495 -> value:   83  ok
logical: 20188 (page:  78, offset: 220) ---> physical:  8924 -> value:    0  ok
logical:  1245 (page:   4, offset: 221) ---> physical:  9181 -> value:    0  ok
logical: 12220 (page:  47, offset: 188) ---> physical:  9404 -> value:    0  ok

logical: 17602 (page:  68, offset: 194) ---> physical:  9666 -> value:   17  ok
logical: 28609 (page: 111, offset: 193) ---> physical: 28097 -> value:    0  ok
logical: 42694 (page: 166, offset: 198) ---> physical: 20934 -> value:   41  ok
logical: 29826 (page: 116, offset: 130) ---> physical:  9858 -> value:   29  ok
logical: 13827 (page:  54, offset:   3) ---> physical: 31235 -> value: -128  ok

logical: 27336 (page: 106, offset: 200) ---> physical: 28872 -> value:    0  ok
logical: 53343 (page: 208, offset:  95) ---> physical:  2655 -> value:   23  ok
logical: 11533 (page:  45, offset:  13) ---> physical:  9997 -> value:    0  ok
logical: 41713 (page: 162, offset: 241) ---> physical: 10481 -> value:    0  ok
logical: 33890 (page: 132, offset:  98) ---> physical: 10594 -> value:   33  ok

logical:  4894 (page:  19, offset:  30) ---> physical: 13598 -> value:    4  ok
logical: 57599 (page: 224, offset: 255) ---> physical:  5887 -> value:   63  ok
logical:  3870 (page:  15, offset:  30) ---> physical: 18974 -> value:    3  ok
logical: 58622 (page: 228, offset: 254) ---> physical: 15614 -> value:   57  ok
logical: 29780 (page: 116, offset:  84) ---> physical:  9812 -> value:    0  ok

logical: 62553 (page: 244, offset:  89) ---> physical:  4185 -> value:    0  ok
logical:  2303 (page:   8, offset: 255) ---> physical:  2559 -> value:   63  ok
logical: 51915 (page: 202, offset: 203) ---> physical: 27339 -> value:  -78  ok
logical:  6251 (page:  24, offset: 107) ---> physical: 10859 -> value:   26  ok
logical: 38107 (page: 148, offset: 219) ---> physical:  4827 -> value:   54  ok

logical: 59325 (page: 231, offset: 189) ---> physical: 11197 -> value:    0  ok
logical: 61295 (page: 239, offset: 111) ---> physical: 11375 -> value:  -37  ok
logical: 26699 (page: 104, offset:  75) ---> physical: 11595 -> value:   18  ok
logical: 51188 (page: 199, offset: 244) ---> physical: 12020 -> value:    0  ok
logical: 59519 (page: 232, offset: 127) ---> physical: 25983 -> value:   31  ok

logical:  7345 (page:  28, offset: 177) ---> physical: 12209 -> value:    0  ok
logical: 20325 (page:  79, offset: 101) ---> physical: 16741 -> value:    0  ok
logical: 39633 (page: 154, offset: 209) ---> physical: 12497 -> value:    0  ok
logical:  1562 (page:   6, offset:  26) ---> physical: 12570 -> value:    1  ok
logical:  7580 (page:  29, offset: 156) ---> physical: 12956 -> value:    0  ok

logical:  8170 (page:  31, offset: 234) ---> physical:  1258 -> value:    7  ok
logical: 62256 (page: 243, offset:  48) ---> physical: 23856 -> value:    0  ok
logical: 35823 (page: 139, offset: 239) ---> physical: 13295 -> value:   -5  ok
logical: 27790 (page: 108, offset: 142) ---> physical: 13454 -> value:   27  ok
logical: 13191 (page:  51, offset: 135) ---> physical: 13703 -> value:  -31  ok

logical:  9772 (page:  38, offset:  44) ---> physical: 13868 -> value:    0  ok
logical:  7477 (page:  29, offset:  53) ---> physical: 12853 -> value:    0  ok
logical: 44455 (page: 173, offset: 167) ---> physical: 14247 -> value:  105  ok
logical: 59546 (page: 232, offset: 154) ---> physical: 26010 -> value:   58  ok
logical: 49347 (page: 192, offset: 195) ---> physical: 14531 -> value:   48  ok

logical: 36539 (page: 142, offset: 187) ---> physical: 14779 -> value:  -82  ok
logical: 12453 (page:  48, offset: 165) ---> physical: 15013 -> value:    0  ok
logical: 49640 (page: 193, offset: 232) ---> physical: 15336 -> value:    0  ok
logical: 28290 (page: 110, offset: 130) ---> physical:  2178 -> value:   27  ok
logical: 44817 (page: 175, offset:  17) ---> physical: 15377 -> value:    0  ok

logical:  8565 (page:  33, offset: 117) ---> physical: 20085 -> value:    0  ok
logical: 16399 (page:  64, offset:  15) ---> physical: 15631 -> value:    3  ok
logical: 41934 (page: 163, offset: 206) ---> physical:  6606 -> value:   40  ok
logical: 45457 (page: 177, offset: 145) ---> physical: 16017 -> value:    0  ok
logical: 33856 (page: 132, offset:  64) ---> physical: 10560 -> value:    0  ok

logical: 19498 (page:  76, offset:  42) ---> physical: 22570 -> value:   19  ok
logical: 17661 (page:  68, offset: 253) ---> physical:  9725 -> value:    0  ok
logical: 63829 (page: 249, offset:  85) ---> physical: 21845 -> value:    0  ok
logical: 42034 (page: 164, offset:  50) ---> physical: 16178 -> value:   41  ok
logical: 28928 (page: 113, offset:   0) ---> physical: 16384 -> value:    0  ok

logical: 30711 (page: 119, offset: 247) ---> physical: 16631 -> value:   -3  ok
logical:  8800 (page:  34, offset:  96) ---> physical: 16736 -> value:    0  ok
logical: 52335 (page: 204, offset: 111) ---> physical: 17007 -> value:   27  ok
logical: 38775 (page: 151, offset: 119) ---> physical: 17271 -> value:  -35  ok
logical: 52704 (page: 205, offset: 224) ---> physical: 30176 -> value:    0  ok

logical: 24380 (page:  95, offset:  60) ---> physical: 17468 -> value:    0  ok
logical: 19602 (page:  76, offset: 146) ---> physical: 22674 -> value:   19  ok
logical: 57998 (page: 226, offset: 142) ---> physical: 17806 -> value:   56  ok
logical:  2919 (page:  11, offset: 103) ---> physical: 18023 -> value:  -39  ok
logical:  8362 (page:  32, offset: 170) ---> physical: 18346 -> value:    8  ok

logical: 17884 (page:  69, offset: 220) ---> physical: 18652 -> value:    0  ok
logical: 45737 (page: 178, offset: 169) ---> physical: 18857 -> value:    0  ok
logical: 47894 (page: 187, offset:  22) ---> physical: 25622 -> value:   46  ok
logical: 59667 (page: 233, offset:  19) ---> physical: 18963 -> value:   68  ok
logical: 10385 (page:  40, offset: 145) ---> physical: 19345 -> value:    0  ok

logical: 52782 (page: 206, offset:  46) ---> physical: 19502 -> value:   51  ok
logical: 64416 (page: 251, offset: 160) ---> physical: 19872 -> value:    0  ok
logical: 40946 (page: 159, offset: 242) ---> physical: 20210 -> value:   39  ok
logical: 16778 (page:  65, offset: 138) ---> physical: 20362 -> value:   16  ok
logical: 27159 (page: 106, offset:  23) ---> physical: 28695 -> value: -123  ok

logical: 24324 (page:  95, offset:   4) ---> physical: 17412 -> value:    0  ok
logical: 32450 (page: 126, offset: 194) ---> physical: 20674 -> value:   31  ok
logical:  9108 (page:  35, offset: 148) ---> physical: 20884 -> value:    0  ok
logical: 65305 (page: 255, offset:  25) ---> physical: 30489 -> value:    0  ok
logical: 19575 (page:  76, offset: 119) ---> physical: 22647 -> value:   29  ok

logical: 11117 (page:  43, offset: 109) ---> physical:  7789 -> value:    0  ok
logical: 65170 (page: 254, offset: 146) ---> physical: 21138 -> value:   63  ok
logical: 58013 (page: 226, offset: 157) ---> physical: 17821 -> value:    0  ok
logical: 61676 (page: 240, offset: 236) ---> physical: 23276 -> value:    0  ok
logical: 63510 (page: 248, offset:  22) ---> physical: 28182 -> value:   62  ok

logical: 17458 (page:  68, offset:  50) ---> physical:  9522 -> value:   17  ok
logical: 54675 (page: 213, offset: 147) ---> physical: 21395 -> value:  100  ok
logical:  1713 (page:   6, offset: 177) ---> physical: 12721 -> value:    0  ok
logical: 55105 (page: 215, offset:  65) ---> physical: 21569 -> value:    0  ok
logical: 65321 (page: 255, offset:  41) ---> physical: 30505 -> value:    0  ok

logical: 45278 (page: 176, offset: 222) ---> physical: 21982 -> value:   44  ok
logical: 26256 (page: 102, offset: 144) ---> physical: 22160 -> value:    0  ok
logical: 64198 (page: 250, offset: 198) ---> physical: 22470 -> value:   62  ok
logical: 29441 (page: 115, offset:   1) ---> physical: 22529 -> value:    0  ok
logical:  1928 (page:   7, offset: 136) ---> physical:   392 -> value:    0  ok

logical: 39425 (page: 154, offset:   1) ---> physical: 12289 -> value:    0  ok
logical: 32000 (page: 125, offset:   0) ---> physical: 22784 -> value:    0  ok
logical: 28549 (page: 111, offset: 133) ---> physical: 28037 -> value:    0  ok
logical: 46295 (page: 180, offset: 215) ---> physical: 23255 -> value:   53  ok
logical: 22772 (page:  88, offset: 244) ---> physical: 23540 -> value:    0  ok

logical: 58228 (page: 227, offset: 116) ---> physical:  1396 -> value:    0  ok
logical: 63525 (page: 248, offset:  37) ---> physical: 28197 -> value:    0  ok
logical: 32602 (page: 127, offset:  90) ---> physical: 23642 -> value:   31  ok
logical: 46195 (page: 180, offset: 115) ---> physical: 23155 -> value:   28  ok
logical: 55849 (page: 218, offset:  41) ---> physical:  1833 -> value:    0  ok

logical: 46454 (page: 181, offset: 118) ---> physical: 23926 -> value:   45  ok
logical:  7487 (page:  29, offset:  63) ---> physical: 12863 -> value:   79  ok
logical: 33879 (page: 132, offset:  87) ---> physical: 10583 -> value:   21  ok
logical: 42004 (page: 164, offset:  20) ---> physical: 16148 -> value:    0  ok
logical:  8599 (page:  33, offset: 151) ---> physical: 24215 -> value:  101  ok

logical: 18641 (page:  72, offset: 209) ---> physical: 24529 -> value:    0  ok
logical: 49015 (page: 191, offset: 119) ---> physical: 24695 -> value:  -35  ok
logical: 26830 (page: 104, offset: 206) ---> physical: 11726 -> value:   26  ok
logical: 34754 (page: 135, offset: 194) ---> physical: 25026 -> value:   33  ok
logical: 14668 (page:  57, offset:  76) ---> physical:   588 -> value:    0  ok

logical: 38362 (page: 149, offset: 218) ---> physical: 25306 -> value:   37  ok
logical: 38791 (page: 151, offset: 135) ---> physical: 17287 -> value:  -31  ok
logical:  4171 (page:  16, offset:  75) ---> physical: 25419 -> value:   18  ok
logical: 45975 (page: 179, offset: 151) ---> physical: 25751 -> value:  -27  ok
logical: 14623 (page:  57, offset:  31) ---> physical:   543 -> value:   71  ok

logical: 62393 (page: 243, offset: 185) ---> physical: 26041 -> value:    0  ok
logical: 64658 (page: 252, offset: 146) ---> physical: 26258 -> value:   63  ok
logical: 10963 (page:  42, offset: 211) ---> physical: 26579 -> value:  -76  ok
logical:  9058 (page:  35, offset:  98) ---> physical: 20834 -> value:    8  ok
logical: 51031 (page: 199, offset:  87) ---> physical: 11863 -> value:  -43  ok

logical: 32425 (page: 126, offset: 169) ---> physical: 20649 -> value:    0  ok
logical: 45483 (page: 177, offset: 171) ---> physical: 16043 -> value:  106  ok
logical: 44611 (page: 174, offset:  67) ---> physical: 26691 -> value: -112  ok
logical: 63664 (page: 248, offset: 176) ---> physical: 28336 -> value:    0  ok
logical: 54920 (page: 214, offset: 136) ---> physical: 27016 -> value:    0  ok

logical:  7663 (page:  29, offset: 239) ---> physical: 13039 -> value:  123  ok
logical: 56480 (page: 220, offset: 160) ---> physical: 27296 -> value:    0  ok
logical:  1489 (page:   5, offset: 209) ---> physical: 27601 -> value:    0  ok
logical: 28438 (page: 111, offset:  22) ---> physical: 27926 -> value:   27  ok
logical: 65449 (page: 255, offset: 169) ---> physical: 30633 -> value:    0  ok

logical: 12441 (page:  48, offset: 153) ---> physical: 15001 -> value:    0  ok
logical: 58530 (page: 228, offset: 162) ---> physical: 27810 -> value:   57  ok
logical: 63570 (page: 248, offset:  82) ---> physical: 28242 -> value:   62  ok
logical: 26251 (page: 102, offset: 139) ---> physical: 22155 -> value:  -94  ok
logical: 15972 (page:  62, offset: 100) ---> physical: 28004 -> value:    0  ok

logical: 35826 (page: 139, offset: 242) ---> physical: 13298 -> value:   34  ok
logical:  5491 (page:  21, offset: 115) ---> physical: 28275 -> value:   92  ok
logical: 54253 (page: 211, offset: 237) ---> physical: 28653 -> value:    0  ok
logical: 49655 (page: 193, offset: 247) ---> physical: 15351 -> value:  125  ok
logical:  5868 (page:  22, offset: 236) ---> physical: 28908 -> value:    0  ok

logical: 20163 (page:  78, offset: 195) ---> physical:  8899 -> value:  -80  ok
logical: 51079 (page: 199, offset: 135) ---> physical: 11911 -> value:  -31  ok
logical: 21398 (page:  83, offset: 150) ---> physical: 29078 -> value:   20  ok
logical: 32756 (page: 127, offset: 244) ---> physical: 23796 -> value:    0  ok
logical: 64196 (page: 250, offset: 196) ---> physical: 22468 -> value:    0  ok

logical: 43218 (page: 168, offset: 210) ---> physical: 29394 -> value:   42  ok
logical: 21583 (page:  84, offset:  79) ---> physical: 29519 -> value:   19  ok
logical: 25086 (page:  97, offset: 254) ---> physical: 29950 -> value:   24  ok
logical: 45515 (page: 177, offset: 203) ---> physical: 16075 -> value:  114  ok
logical: 12893 (page:  50, offset:  93) ---> physical: 30301 -> value:    0  ok

logical: 22914 (page:  89, offset: 130) ---> physical: 30082 -> value:   22  ok
logical: 58969 (page: 230, offset:  89) ---> physical: 30297 -> value:    0  ok
logical: 20094 (page:  78, offset: 126) ---> physical:  8830 -> value:   19  ok
logical: 13730 (page:  53, offset: 162) ---> physical: 30626 -> value:   13  ok
logical: 44059 (page: 172, offset:  27) ---> physical: 30747 -> value:    6  ok

logical: 28931 (page: 113, offset:   3) ---> physical: 30979 -> value:   64  ok
logical: 13533 (page:  52, offset: 221) ---> physical:  3549 -> value:    0  ok
logical: 33134 (page: 129, offset: 110) ---> physical: 31342 -> value:   32  ok
logical: 28483 (page: 111, offset:  67) ---> physical: 31555 -> value:  -48  ok
logical:  1220 (page:   4, offset: 196) ---> physical:  9156 -> value:    0  ok

logical: 38174 (page: 149, offset:  30) ---> physical: 25118 -> value:   37  ok
logical: 53502 (page: 208, offset: 254) ---> physical:  2814 -> value:   52  ok
logical: 43328 (page: 169, offset:  64) ---> physical: 31808 -> value:    0  ok
logical:  4970 (page:  19, offset: 106) ---> physical: 32106 -> value:    4  ok
logical:  8090 (page:  31, offset: 154) ---> physical:  1178 -> value:    7  ok

logical:  2661 (page:  10, offset: 101) ---> physical: 32357 -> value:    0  ok
logical: 53903 (page: 210, offset: 143) ---> physical: 32655 -> value:  -93  ok
logical: 11025 (page:  43, offset:  17) ---> physical:  7697 -> value:    0  ok
logical: 26627 (page: 104, offset:   3) ---> physical: 11523 -> value:    0  ok
logical: 18117 (page:  70, offset: 197) ---> physical:   197 -> value:    0  ok

logical: 14505 (page:  56, offset: 169) ---> physical:   425 -> value:    0  ok
logical: 61528 (page: 240, offset:  88) ---> physical:   600 -> value:    0  ok
logical: 20423 (page:  79, offset: 199) ---> physical:   967 -> value:  -15  ok
logical: 26962 (page: 105, offset:  82) ---> physical:  1106 -> value:   26  ok
logical: 36392 (page: 142, offset:  40) ---> physical: 14632 -> value:    0  ok

logical: 11365 (page:  44, offset: 101) ---> physical:  1381 -> value:    0  ok
logical: 50882 (page: 198, offset: 194) ---> physical:  1730 -> value:   49  ok
logical: 41668 (page: 162, offset: 196) ---> physical: 10436 -> value:    0  ok
logical: 30497 (page: 119, offset:  33) ---> physical: 16417 -> value:    0  ok
logical: 36216 (page: 141, offset: 120) ---> physical:  1912 -> value:    0  ok

logical:  5619 (page:  21, offset: 243) ---> physical: 28403 -> value:  124  ok
logical: 36983 (page: 144, offset: 119) ---> physical:  2167 -> value:   29  ok
logical: 59557 (page: 232, offset: 165) ---> physical:  2469 -> value:    0  ok
logical: 36663 (page: 143, offset:  55) ---> physical:  2871 -> value:  -51  ok
logical: 36436 (page: 142, offset:  84) ---> physical: 14676 -> value:    0  ok

logical: 37057 (page: 144, offset: 193) ---> physical:  2241 -> value:    0  ok
logical: 23585 (page:  92, offset:  33) ---> physical:  2593 -> value:    0  ok
logical: 58791 (page: 229, offset: 167) ---> physical:  2983 -> value:  105  ok
logical: 46666 (page: 182, offset:  74) ---> physical:  3146 -> value:   45  ok
logical: 64475 (page: 251, offset: 219) ---> physical: 19931 -> value:  -10  ok

logical: 21615 (page:  84, offset: 111) ---> physical: 29551 -> value:   27  ok
logical: 41090 (page: 160, offset: 130) ---> physical:  3458 -> value:   40  ok
logical:  1771 (page:   6, offset: 235) ---> physical: 12779 -> value:  -70  ok
logical: 47513 (page: 185, offset: 153) ---> physical:  3737 -> value:    0  ok
logical: 39338 (page: 153, offset: 170) ---> physical:  4010 -> value:   38  ok

logical:  1390 (page:   5, offset: 110) ---> physical: 27502 -> value:    1  ok
logical: 38772 (page: 151, offset: 116) ---> physical: 17268 -> value:    0  ok
logical: 58149 (page: 227, offset:  37) ---> physical:  4133 -> value:    0  ok
logical:  7196 (page:  28, offset:  28) ---> physical: 12060 -> value:    0  ok
logical:  9123 (page:  35, offset: 163) ---> physical: 20899 -> value:  -24  ok

logical:  7491 (page:  29, offset:  67) ---> physical: 12867 -> value:   80  ok
logical: 62616 (page: 244, offset: 152) ---> physical:  4504 -> value:    0  ok
logical: 15436 (page:  60, offset:  76) ---> physical:  4684 -> value:    0  ok
logical: 17491 (page:  68, offset:  83) ---> physical:  9555 -> value:   20  ok
logical: 53656 (page: 209, offset: 152) ---> physical:  5272 -> value:    0  ok

logical: 26449 (page: 103, offset:  81) ---> physical:  4945 -> value:    0  ok
logical: 34935 (page: 136, offset: 119) ---> physical:  5239 -> value:   29  ok
logical: 19864 (page:  77, offset: 152) ---> physical:  5528 -> value:    0  ok
logical: 51388 (page: 200, offset: 188) ---> physical:  5820 -> value:    0  ok
logical: 15155 (page:  59, offset:  51) ---> physical:  5939 -> value:  -52  ok

logical: 64775 (page: 253, offset:   7) ---> physical:  6151 -> value:   65  ok
logical: 47969 (page: 187, offset:  97) ---> physical:  6497 -> value:    0  ok
logical: 16315 (page:  63, offset: 187) ---> physical:  6843 -> value:  -18  ok
logical:  1342 (page:   5, offset:  62) ---> physical: 27454 -> value:    1  ok
logical: 51185 (page: 199, offset: 241) ---> physical: 12017 -> value:    0  ok

logical:  6043 (page:  23, offset: 155) ---> physical:  7067 -> value:  -26  ok
logical: 21398 (page:  83, offset: 150) ---> physical: 29078 -> value:   20  ok
logical:  3273 (page:  12, offset: 201) ---> physical:  8393 -> value:    0  ok
logical:  9370 (page:  36, offset: 154) ---> physical:  7322 -> value:    9  ok
logical: 35463 (page: 138, offset: 135) ---> physical:  7559 -> value:  -95  ok

logical: 28205 (page: 110, offset:  45) ---> physical:  7725 -> value:    0  ok
logical:  2351 (page:   9, offset:  47) ---> physical:  7983 -> value:   75  ok
logical: 28999 (page: 113, offset:  71) ---> physical: 31047 -> value:   81  ok
logical: 47699 (page: 186, offset:  83) ---> physical:  8275 -> value: -108  ok
logical: 46870 (page: 183, offset:  22) ---> physical:  8470 -> value:   45  ok

logical: 22311 (page:  87, offset:  39) ---> physical:  8743 -> value:  -55  ok
logical: 22124 (page:  86, offset: 108) ---> physical:  9068 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical:  8859 -> value:  -26  ok
logical: 49344 (page: 192, offset: 192) ---> physical: 14528 -> value:    0  ok
logical: 23224 (page:  90, offset: 184) ---> physical:  9400 -> value:    0  ok

logical:  5514 (page:  21, offset: 138) ---> physical: 28298 -> value:    5  ok
logical: 20504 (page:  80, offset:  24) ---> physical:  9496 -> value:    0  ok
logical:   376 (page:   1, offset: 120) ---> physical:  9848 -> value:    0  ok
logical:  2014 (page:   7, offset: 222) ---> physical: 10206 -> value:    1  ok
logical: 38700 (page: 151, offset:  44) ---> physical: 17196 -> value:    0  ok

logical: 13098 (page:  51, offset:  42) ---> physical: 13610 -> value:   12  ok
logical: 62435 (page: 243, offset: 227) ---> physical: 26083 -> value:   -8  ok
logical: 48046 (page: 187, offset: 174) ---> physical:  6574 -> value:   46  ok
logical: 63464 (page: 247, offset: 232) ---> physical: 10472 -> value:    0  ok
logical: 12798 (page:  49, offset: 254) ---> physical: 10750 -> value:   12  ok

logical: 51178 (page: 199, offset: 234) ---> physical: 12010 -> value:   49  ok
logical:  8627 (page:  33, offset: 179) ---> physical: 24243 -> value:  108  ok
logical: 27083 (page: 105, offset: 203) ---> physical:  1227 -> value:  114  ok
logical: 47198 (page: 184, offset:  94) ---> physical: 10846 -> value:   46  ok
logical: 44021 (page: 171, offset: 245) ---> physical: 11253 -> value:    0  ok

logical: 32792 (page: 128, offset:  24) ---> physical: 11288 -> value:    0  ok
logical: 43996 (page: 171, offset: 220) ---> physical: 11228 -> value:    0  ok
logical: 41126 (page: 160, offset: 166) ---> physical:  3494 -> value:   40  ok
logical: 64244 (page: 250, offset: 244) ---> physical: 22516 -> value:    0  ok
logical: 37047 (page: 144, offset: 183) ---> physical:  2231 -> value:   45  ok

logical: 60281 (page: 235, offset: 121) ---> physical: 11641 -> value:    0  ok
logical: 52904 (page: 206, offset: 168) ---> physical: 19624 -> value:    0  ok
logical:  7768 (page:  30, offset:  88) ---> physical: 11864 -> value:    0  ok
logical: 55359 (page: 216, offset:  63) ---> physical: 12095 -> value:   15  ok
logical:  3230 (page:  12, offset: 158) ---> physical: 12446 -> value:    3  ok

logical: 44813 (page: 175, offset:  13) ---> physical: 15373 -> value:    0  ok
logical:  4116 (page:  16, offset:  20) ---> physical: 25364 -> value:    0  ok
logical: 65222 (page: 254, offset: 198) ---> physical: 21190 -> value:   63  ok
logical: 28083 (page: 109, offset: 179) ---> physical: 12723 -> value:  108  ok
logical: 60660 (page: 236, offset: 244) ---> physical: 13044 -> value:    0  ok

logical:    39 (page:   0, offset:  39) ---> physical: 13095 -> value:    9  ok
logical:   328 (page:   1, offset:  72) ---> physical:  9800 -> value:    0  ok
logical: 47868 (page: 186, offset: 252) ---> physical:  8444 -> value:    0  ok
logical: 13009 (page:  50, offset: 209) ---> physical: 13521 -> value:    0  ok
logical: 22378 (page:  87, offset: 106) ---> physical:  8810 -> value:   21  ok

logical: 39304 (page: 153, offset: 136) ---> physical:  3976 -> value:    0  ok
logical: 11171 (page:  43, offset: 163) ---> physical: 13731 -> value:  -24  ok
logical:  8079 (page:  31, offset: 143) ---> physical: 13967 -> value:  -29  ok
logical: 52879 (page: 206, offset: 143) ---> physical: 19599 -> value:  -93  ok
logical:  5123 (page:  20, offset:   3) ---> physical: 14083 -> value:    0  ok

logical:  4356 (page:  17, offset:   4) ---> physical: 14340 -> value:    0  ok
logical: 45745 (page: 178, offset: 177) ---> physical: 18865 -> value:    0  ok
logical: 32952 (page: 128, offset: 184) ---> physical: 11448 -> value:    0  ok
logical:  4657 (page:  18, offset:  49) ---> physical: 14641 -> value:    0  ok
logical: 24142 (page:  94, offset:  78) ---> physical: 14926 -> value:   23  ok

logical: 23319 (page:  91, offset:  23) ---> physical: 15127 -> value:  -59  ok
logical: 13607 (page:  53, offset:  39) ---> physical: 30503 -> value:   73  ok
logical: 46304 (page: 180, offset: 224) ---> physical: 23264 -> value:    0  ok
logical: 17677 (page:  69, offset:  13) ---> physical: 18445 -> value:    0  ok
logical: 59691 (page: 233, offset:  43) ---> physical: 18987 -> value:   74  ok

logical: 50967 (page: 199, offset:  23) ---> physical: 15383 -> value:  -59  ok
logical:  7817 (page:  30, offset: 137) ---> physical: 11913 -> value:    0  ok
logical:  8545 (page:  33, offset:  97) ---> physical: 24161 -> value:    0  ok
logical: 55297 (page: 216, offset:   1) ---> physical: 12033 -> value:    0  ok
logical: 52954 (page: 206, offset: 218) ---> physical: 19674 -> value:   51  ok

logical: 39720 (page: 155, offset:  40) ---> physical: 15656 -> value:    0  ok
logical: 18455 (page:  72, offset:  23) ---> physical: 24343 -> value:    5  ok
logical: 30349 (page: 118, offset: 141) ---> physical: 16013 -> value:    0  ok
logical: 63270 (page: 247, offset:  38) ---> physical: 10278 -> value:   61  ok
logical: 27156 (page: 106, offset:  20) ---> physical: 16148 -> value:    0  ok

logical: 20614 (page:  80, offset: 134) ---> physical:  9606 -> value:   20  ok
logical: 19372 (page:  75, offset: 172) ---> physical: 16556 -> value:    0  ok
logical: 48689 (page: 190, offset:  49) ---> physical: 16689 -> value:    0  ok
logical: 49386 (page: 192, offset: 234) ---> physical: 17130 -> value:   48  ok
logical: 50584 (page: 197, offset: 152) ---> physical: 17304 -> value:    0  ok

logical: 51936 (page: 202, offset: 224) ---> physical: 17632 -> value:    0  ok
logical: 34705 (page: 135, offset: 145) ---> physical: 24977 -> value:    0  ok
logical: 13653 (page:  53, offset:  85) ---> physical: 30549 -> value:    0  ok
logical: 50077 (page: 195, offset: 157) ---> physical: 17821 -> value:    0  ok
logical: 54518 (page: 212, offset: 246) ---> physical: 18166 -> value:   53  ok

logical: 41482 (page: 162, offset:  10) ---> physical: 18186 -> value:   40  ok
logical:  4169 (page:  16, offset:  73) ---> physical: 25417 -> value:    0  ok
logical: 36118 (page: 141, offset:  22) ---> physical:  1814 -> value:   35  ok
logical:  9584 (page:  37, offset: 112) ---> physical: 18544 -> value:    0  ok
logical: 18490 (page:  72, offset:  58) ---> physical: 24378 -> value:   18  ok

logical: 55420 (page: 216, offset: 124) ---> physical: 12156 -> value:    0  ok
logical:  5708 (page:  22, offset:  76) ---> physical: 28748 -> value:    0  ok
logical: 23506 (page:  91, offset: 210) ---> physical: 15314 -> value:   22  ok
logical: 15391 (page:  60, offset:  31) ---> physical:  4639 -> value:    7  ok
logical: 36368 (page: 142, offset:  16) ---> physical: 18704 -> value:    0  ok

logical: 38976 (page: 152, offset:  64) ---> physical: 19008 -> value:    0  ok
logical: 50406 (page: 196, offset: 230) ---> physical: 19430 -> value:   49  ok
logical: 49236 (page: 192, offset:  84) ---> physical: 16980 -> value:    0  ok
logical: 65035 (page: 254, offset:  11) ---> physical: 21003 -> value: -126  ok
logical: 30120 (page: 117, offset: 168) ---> physical: 19624 -> value:    0  ok

logical: 62551 (page: 244, offset:  87) ---> physical:  4439 -> value:   21  ok
logical: 46809 (page: 182, offset: 217) ---> physical:  3289 -> value:    0  ok
logical: 21687 (page:  84, offset: 183) ---> physical: 29623 -> value:   45  ok
logical: 53839 (page: 210, offset:  79) ---> physical: 32591 -> value: -109  ok
logical:  2098 (page:   8, offset:  50) ---> physical: 19762 -> value:    2  ok

logical: 12364 (page:  48, offset:  76) ---> physical: 20044 -> value:    0  ok
logical: 45366 (page: 177, offset:  54) ---> physical: 20278 -> value:   44  ok
logical: 50437 (page: 197, offset:   5) ---> physical: 17157 -> value:    0  ok
logical: 36675 (page: 143, offset:  67) ---> physical: 20547 -> value:  -48  ok
logical: 55382 (page: 216, offset:  86) ---> physical: 12118 -> value:   54  ok

logical: 11846 (page:  46, offset:  70) ---> physical: 20806 -> value:   11  ok
logical: 49127 (page: 191, offset: 231) ---> physical: 24807 -> value:   -7  ok
logical: 19900 (page:  77, offset: 188) ---> physical:  5564 -> value:    0  ok
logical: 20554 (page:  80, offset:  74) ---> physical:  9546 -> value:   20  ok
logical: 19219 (page:  75, offset:  19) ---> physical: 16403 -> value:  -60  ok

logical: 51483 (page: 201, offset:  27) ---> physical: 21019 -> value:   70  ok
logical: 58090 (page: 226, offset: 234) ---> physical: 21482 -> value:   56  ok
logical: 39074 (page: 152, offset: 162) ---> physical: 19106 -> value:   38  ok
logical: 16060 (page:  62, offset: 188) ---> physical: 28092 -> value:    0  ok
logical: 10447 (page:  40, offset: 207) ---> physical: 21711 -> value:   51  ok

logical: 54169 (page: 211, offset: 153) ---> physical: 28569 -> value:    0  ok
logical: 20634 (page:  80, offset: 154) ---> physical:  9626 -> value:   20  ok
logical: 57555 (page: 224, offset: 211) ---> physical: 21971 -> value:   52  ok
logical: 61210 (page: 239, offset:  26) ---> physical: 22042 -> value:   59  ok
logical:   269 (page:   1, offset:  13) ---> physical:  9741 -> value:    0  ok

logical: 33154 (page: 129, offset: 130) ---> physical: 31362 -> value:   32  ok
logical: 64487 (page: 251, offset: 231) ---> physical: 22503 -> value:   -7  ok
logical: 61223 (page: 239, offset:  39) ---> physical: 22055 -> value:  -55  ok
logical: 47292 (page: 184, offset: 188) ---> physical: 10940 -> value:    0  ok
logical: 21852 (page:  85, offset:  92) ---> physical: 22620 -> value:    0  ok

logical:  5281 (page:  20, offset: 161) ---> physical: 14241 -> value:    0  ok
logical: 45912 (page: 179, offset:  88) ---> physical: 25688 -> value:    0  ok
logical: 32532 (page: 127, offset:  20) ---> physical: 23572 -> value:    0  ok
logical: 63067 (page: 246, offset:  91) ---> physical: 22875 -> value: -106  ok
logical: 41683 (page: 162, offset: 211) ---> physical: 18387 -> value:  -76  ok

logical: 20981 (page:  81, offset: 245) ---> physical: 23285 -> value:    0  ok
logical: 33881 (page: 132, offset:  89) ---> physical: 23385 -> value:    0  ok
logical: 41785 (page: 163, offset:  57) ---> physical: 23609 -> value:    0  ok
logical:  4580 (page:  17, offset: 228) ---> physical: 14564 -> value:    0  ok
logical: 41389 (page: 161, offset: 173) ---> physical: 23981 -> value:    0  ok

logical: 28572 (page: 111, offset: 156) ---> physical: 31644 -> value:    0  ok
logical:   782 (page:   3, offset:  14) ---> physical: 24078 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical: 15937 -> value:    0  ok
logical: 62267 (page: 243, offset:  59) ---> physical: 25915 -> value:  -50  ok
logical: 17922 (page:  70, offset:   2) ---> physical:     2 -> value:   17  ok

logical: 63238 (page: 247, offset:   6) ---> physical: 10246 -> value:   61  ok
logical:  3308 (page:  12, offset: 236) ---> physical: 12524 -> value:    0  ok
logical: 26545 (page: 103, offset: 177) ---> physical:  5041 -> value:    0  ok
logical: 44395 (page: 173, offset: 107) ---> physical: 24427 -> value:   90  ok
logical: 39120 (page: 152, offset: 208) ---> physical: 19152 -> value:    0  ok

logical: 21706 (page:  84, offset: 202) ---> physical: 29642 -> value:   21  ok
logical:  7144 (page:  27, offset: 232) ---> physical: 24808 -> value:    0  ok
logical: 30244 (page: 118, offset:  36) ---> physical: 15908 -> value:    0  ok
logical:  3725 (page:  14, offset: 141) ---> physical: 24973 -> value:    0  ok
logical: 54632 (page: 213, offset: 104) ---> physical: 25192 -> value:    0  ok

logical: 30574 (page: 119, offset: 110) ---> physical: 25454 -> value:   29  ok
logical:  8473 (page:  33, offset:  25) ---> physical: 25625 -> value:    0  ok
logical: 12386 (page:  48, offset:  98) ---> physical: 20066 -> value:   12  ok
logical: 41114 (page: 160, offset: 154) ---> physical:  3482 -> value:   40  ok
logical: 57930 (page: 226, offset:  74) ---> physical: 21322 -> value:   56  ok

logical: 15341 (page:  59, offset: 237) ---> physical:  6125 -> value:    0  ok
logical: 15598 (page:  60, offset: 238) ---> physical:  4846 -> value:   15  ok
logical: 59922 (page: 234, offset:  18) ---> physical: 25874 -> value:   58  ok
logical: 18226 (page:  71, offset:  50) ---> physical: 26162 -> value:   17  ok
logical: 48162 (page: 188, offset:  34) ---> physical: 26402 -> value:   47  ok

logical: 41250 (page: 161, offset:  34) ---> physical: 23842 -> value:   40  ok
logical:  1512 (page:   5, offset: 232) ---> physical: 27624 -> value:    0  ok
logical:  2546 (page:   9, offset: 242) ---> physical:  8178 -> value:    2  ok
logical: 41682 (page: 162, offset: 210) ---> physical: 18386 -> value:   40  ok
logical:   322 (page:   1, offset:  66) ---> physical:  9794 -> value:    0  ok

logical:   880 (page:   3, offset: 112) ---> physical: 24176 -> value:    0  ok
logical: 20891 (page:  81, offset: 155) ---> physical: 23195 -> value:  102  ok
logical: 56604 (page: 221, offset:  28) ---> physical: 26652 -> value:    0  ok
logical: 40166 (page: 156, offset: 230) ---> physical: 27110 -> value:   39  ok
logical: 26791 (page: 104, offset: 167) ---> physical: 27303 -> value:   41  ok

logical: 44560 (page: 174, offset:  16) ---> physical: 27408 -> value:    0  ok
logical: 38698 (page: 151, offset:  42) ---> physical: 27690 -> value:   37  ok
logical: 64127 (page: 250, offset: 127) ---> physical: 28031 -> value:  -97  ok
logical: 15028 (page:  58, offset: 180) ---> physical: 28340 -> value:    0  ok
logical: 38669 (page: 151, offset:  13) ---> physical: 27661 -> value:    0  ok

logical: 45637 (page: 178, offset:  69) ---> physical: 28485 -> value:    0  ok
logical: 43151 (page: 168, offset: 143) ---> physical: 29327 -> value:   35  ok
logical:  9465 (page:  36, offset: 249) ---> physical:  7417 -> value:    0  ok
logical:  2498 (page:   9, offset: 194) ---> physical:  8130 -> value:    2  ok
logical: 13978 (page:  54, offset: 154) ---> physical: 28826 -> value:   13  ok

logical: 16326 (page:  63, offset: 198) ---> physical:  6854 -> value:   15  ok
logical: 51442 (page: 200, offset: 242) ---> physical:  5874 -> value:   50  ok
logical: 34845 (page: 136, offset:  29) ---> physical:  5149 -> value:    0  ok
logical: 63667 (page: 248, offset: 179) ---> physical: 29107 -> value:   44  ok
logical: 39370 (page: 153, offset: 202) ---> physical:  4042 -> value:   38  ok

logical: 55671 (page: 217, offset: 119) ---> physical: 29303 -> value:   93  ok
logical: 64496 (page: 251, offset: 240) ---> physical: 22512 -> value:    0  ok
logical:  7767 (page:  30, offset:  87) ---> physical: 11863 -> value: -107  ok
logical:  6283 (page:  24, offset: 139) ---> physical: 29579 -> value:   34  ok
logical: 55884 (page: 218, offset:  76) ---> physical: 29772 -> value:    0  ok

logical: 61103 (page: 238, offset: 175) ---> physical: 30127 -> value:  -85  ok
logical: 10184 (page:  39, offset: 200) ---> physical: 30408 -> value:    0  ok
logical: 39543 (page: 154, offset: 119) ---> physical: 30583 -> value:  -99  ok
logical:  9555 (page:  37, offset:  83) ---> physical: 18515 -> value:   84  ok
logical: 13963 (page:  54, offset: 139) ---> physical: 28811 -> value:  -94  ok

logical: 58975 (page: 230, offset:  95) ---> physical: 30815 -> value: -105  ok
logical: 19537 (page:  76, offset:  81) ---> physical: 31057 -> value:    0  ok
logical:  6101 (page:  23, offset: 213) ---> physical:  7125 -> value:    0  ok
logical: 41421 (page: 161, offset: 205) ---> physical: 24013 -> value:    0  ok
logical: 45502 (page: 177, offset: 190) ---> physical: 20414 -> value:   44  ok

logical: 29328 (page: 114, offset: 144) ---> physical: 31376 -> value:    0  ok
logical:  8149 (page:  31, offset: 213) ---> physical: 14037 -> value:    0  ok
logical: 25450 (page:  99, offset: 106) ---> physical: 31594 -> value:   24  ok
logical: 58944 (page: 230, offset:  64) ---> physical: 30784 -> value:    0  ok
logical: 50666 (page: 197, offset: 234) ---> physical: 17386 -> value:   49  ok

logical: 23084 (page:  90, offset:  44) ---> physical:  9260 -> value:    0  ok
logical: 36468 (page: 142, offset: 116) ---> physical: 18804 -> value:    0  ok
logical: 33645 (page: 131, offset: 109) ---> physical: 31853 -> value:    0  ok
logical: 25002 (page:  97, offset: 170) ---> physical: 32170 -> value:   24  ok
logical: 53715 (page: 209, offset: 211) ---> physical: 32467 -> value:  116  ok

logical: 60173 (page: 235, offset:  13) ---> physical: 11533 -> value:    0  ok
logical: 46354 (page: 181, offset:  18) ---> physical: 32530 -> value:   45  ok
logical:  4708 (page:  18, offset: 100) ---> physical: 14692 -> value:    0  ok
logical: 28208 (page: 110, offset:  48) ---> physical:  7728 -> value:    0  ok
logical: 58844 (page: 229, offset: 220) ---> physical:  3036 -> value:    0  ok

logical: 22173 (page:  86, offset: 157) ---> physical:  9117 -> value:    0  ok
logical:  8535 (page:  33, offset:  87) ---> physical: 25687 -> value:   85  ok
logical: 42261 (page: 165, offset:  21) ---> physical:    21 -> value:    0  ok
logical: 29687 (page: 115, offset: 247) ---> physical:   503 -> value:   -3  ok
logical: 37799 (page: 147, offset: 167) ---> physical:   679 -> value:  -23  ok

logical: 22566 (page:  88, offset:  38) ---> physical:   806 -> value:   22  ok
logical: 62520 (page: 244, offset:  56) ---> physical:  4408 -> value:    0  ok
logical:  4098 (page:  16, offset:   2) ---> physical:  1026 -> value:    4  ok
logical: 47999 (page: 187, offset: 127) ---> physical:  6527 -> value:  -33  ok
logical: 49660 (page: 193, offset: 252) ---> physical:  1532 -> value:    0  ok

logical: 37063 (page: 144, offset: 199) ---> physical:  2247 -> value:   49  ok
logical: 41856 (page: 163, offset: 128) ---> physical: 23680 -> value:    0  ok
logical:  5417 (page:  21, offset:  41) ---> physical:  1577 -> value:    0  ok
logical: 48856 (page: 190, offset: 216) ---> physical: 16856 -> value:    0  ok
logical: 10682 (page:  41, offset: 186) ---> physical:  1978 -> value:   10  ok

logical: 22370 (page:  87, offset:  98) ---> physical:  8802 -> value:   21  ok
logical: 63281 (page: 247, offset:  49) ---> physical: 10289 -> value:    0  ok
logical: 62452 (page: 243, offset: 244) ---> physical:  2292 -> value:    0  ok
logical: 50532 (page: 197, offset: 100) ---> physical: 17252 -> value:    0  ok
logical:  9022 (page:  35, offset:  62) ---> physical:  2366 -> value:    8  ok

logical: 59300 (page: 231, offset: 164) ---> physical:  2724 -> value:    0  ok
logical: 58660 (page: 229, offset:  36) ---> physical:  2852 -> value:    0  ok
logical: 56401 (page: 220, offset:  81) ---> physical:  2897 -> value:    0  ok
logical:  8518 (page:  33, offset:  70) ---> physical: 25670 -> value:    8  ok
logical: 63066 (page: 246, offset:  90) ---> physical: 22874 -> value:   61  ok

logical: 63250 (page: 247, offset:  18) ---> physical: 10258 -> value:   61  ok
logical: 48592 (page: 189, offset: 208) ---> physical:  3280 -> value:    0  ok
logical: 28771 (page: 112, offset:  99) ---> physical:  3427 -> value:   24  ok
logical: 37673 (page: 147, offset:  41) ---> physical:   553 -> value:    0  ok
logical: 60776 (page: 237, offset: 104) ---> physical:  3688 -> value:    0  ok

logical: 56438 (page: 220, offset: 118) ---> physical:  2934 -> value:   55  ok
logical: 60424 (page: 236, offset:   8) ---> physical: 12808 -> value:    0  ok
logical: 39993 (page: 156, offset:  57) ---> physical: 26937 -> value:    0  ok
logical: 56004 (page: 218, offset: 196) ---> physical: 29892 -> value:    0  ok
logical: 59002 (page: 230, offset: 122) ---> physical: 30842 -> value:   57  ok

logical: 33982 (page: 132, offset: 190) ---> physical: 23486 -> value:   33  ok
logical: 25498 (page:  99, offset: 154) ---> physical: 31642 -> value:   24  ok
logical: 57047 (page: 222, offset: 215) ---> physical:  4055 -> value:  -75  ok
logical:  1401 (page:   5, offset: 121) ---> physical:  4217 -> value:    0  ok
logical: 15130 (page:  59, offset:  26) ---> physical:  5914 -> value:   14  ok

logical: 42960 (page: 167, offset: 208) ---> physical:  4560 -> value:    0  ok
logical: 61827 (page: 241, offset: 131) ---> physical:  4739 -> value:   96  ok
logical: 32442 (page: 126, offset: 186) ---> physical:  5050 -> value:   31  ok
logical: 64304 (page: 251, offset:  48) ---> physical: 22320 -> value:    0  ok
logical: 30273 (page: 118, offset:  65) ---> physical: 15937 -> value:    0  ok

logical: 38082 (page: 148, offset: 194) ---> physical:  5314 -> value:   37  ok
logical: 22404 (page:  87, offset: 132) ---> physical:  8836 -> value:    0  ok
logical:  3808 (page:  14, offset: 224) ---> physical: 25056 -> value:    0  ok
logical: 16883 (page:  65, offset: 243) ---> physical:  5619 -> value:  124  ok
logical: 23111 (page:  90, offset:  71) ---> physical:  9287 -> value: -111  ok

logical: 62417 (page: 243, offset: 209) ---> physical:  2257 -> value:    0  ok
logical: 60364 (page: 235, offset: 204) ---> physical: 11724 -> value:    0  ok
logical:  4542 (page:  17, offset: 190) ---> physical: 14526 -> value:    4  ok
logical: 14829 (page:  57, offset: 237) ---> physical:  5869 -> value:    0  ok
logical: 44964 (page: 175, offset: 164) ---> physical:  6052 -> value:    0  ok

logical: 33924 (page: 132, offset: 132) ---> physical: 23428 -> value:    0  ok
logical:  2141 (page:   8, offset:  93) ---> physical: 19805 -> value:    0  ok
logical: 19245 (page:  75, offset:  45) ---> physical: 16429 -> value:    0  ok
logical: 47168 (page: 184, offset:  64) ---> physical: 10816 -> value:    0  ok
logical: 24048 (page:  93, offset: 240) ---> physical:  6384 -> value:    0  ok

logical:  1022 (page:   3, offset: 254) ---> physical: 24318 -> value:    0  ok
logical: 23075 (page:  90, offset:  35) ---> physical:  9251 -> value: -120  ok
logical: 24888 (page:  97, offset:  56) ---> physical: 32056 -> value:    0  ok
logical: 49247 (page: 192, offset:  95) ---> physical: 16991 -> value:   23  ok
logical:  4900 (page:  19, offset:  36) ---> physical:  6436 -> value:    0  ok

logical: 22656 (page:  88, offset: 128) ---> physical:   896 -> value:    0  ok
logical: 34117 (page: 133, offset:  69) ---> physical:  6725 -> value:    0  ok
logical: 55555 (page: 217, offset:   3) ---> physical: 29187 -> value:   64  ok
logical: 48947 (page: 191, offset:  51) ---> physical:  6963 -> value:  -52  ok
logical: 59533 (page: 232, offset: 141) ---> physical:  7309 -> value:    0  ok

logical: 21312 (page:  83, offset:  64) ---> physical:  7488 -> value:    0  ok
logical: 21415 (page:  83, offset: 167) ---> physical:  7591 -> value:  -23  ok
logical:   813 (page:   3, offset:  45) ---> physical: 24109 -> value:    0  ok
logical: 19419 (page:  75, offset: 219) ---> physical: 16603 -> value:  -10  ok
logical:  1999 (page:   7, offset: 207) ---> physical: 10191 -> value:  -13  ok

logical: 20155 (page:  78, offset: 187) ---> physical:  7867 -> value:  -82  ok
logical: 21521 (page:  84, offset:  17) ---> physical:  7953 -> value:    0  ok
logical: 13670 (page:  53, offset: 102) ---> physical:  8294 -> value:   13  ok
logical: 19289 (page:  75, offset:  89) ---> physical: 16473 -> value:    0  ok
logical: 58483 (page: 228, offset: 115) ---> physical:  8563 -> value:   28  ok

logical: 41318 (page: 161, offset: 102) ---> physical: 23910 -> value:   40  ok
logical: 16151 (page:  63, offset:  23) ---> physical:  8727 -> value:  -59  ok
logical: 13611 (page:  53, offset:  43) ---> physical:  8235 -> value:   74  ok
logical: 21514 (page:  84, offset:  10) ---> physical:  7946 -> value:   21  ok
logical: 13499 (page:  52, offset: 187) ---> physical:  9147 -> value:   46  ok

logical: 45583 (page: 178, offset:  15) ---> physical: 28431 -> value: -125  ok
logical: 49013 (page: 191, offset: 117) ---> physical:  7029 -> value:    0  ok
logical: 64843 (page: 253, offset:  75) ---> physical:  9291 -> value:   82  ok
logical: 63485 (page: 247, offset: 253) ---> physical: 10493 -> value:    0  ok
logical: 38697 (page: 151, offset:  41) ---> physical: 27689 -> value:    0  ok

logical: 59188 (page: 231, offset:  52) ---> physical:  2612 -> value:    0  ok
logical: 24593 (page:  96, offset:  17) ---> physical:  9489 -> value:    0  ok
logical: 57641 (page: 225, offset:  41) ---> physical:  9769 -> value:    0  ok
logical: 36524 (page: 142, offset: 172) ---> physical: 18860 -> value:    0  ok
logical: 56980 (page: 222, offset: 148) ---> physical:  3988 -> value:    0  ok

logical: 36810 (page: 143, offset: 202) ---> physical: 20682 -> value:   35  ok
logical:  6096 (page:  23, offset: 208) ---> physical: 10192 -> value:    0  ok
logical: 11070 (page:  43, offset:  62) ---> physical: 13630 -> value:   10  ok
logical: 60124 (page: 234, offset: 220) ---> physical: 26076 -> value:    0  ok
logical: 37576 (page: 146, offset: 200) ---> physical: 10440 -> value:    0  ok

logical: 15096 (page:  58, offset: 248) ---> physical: 28408 -> value:    0  ok
logical: 45247 (page: 176, offset: 191) ---> physical: 10687 -> value:   47  ok
logical: 32783 (page: 128, offset:  15) ---> physical: 11279 -> value:    3  ok
logical: 58390 (page: 228, offset:  22) ---> physical:  8470 -> value:   57  ok
logical: 60873 (page: 237, offset: 201) ---> physical:  3785 -> value:    0  ok

logical: 23719 (page:  92, offset: 167) ---> physical: 10919 -> value:   41  ok
logical: 24385 (page:  95, offset:  65) ---> physical: 11073 -> value:    0  ok
logical: 22307 (page:  87, offset:  35) ---> physical: 11299 -> value:  -56  ok
logical: 17375 (page:  67, offset: 223) ---> physical: 11743 -> value:   -9  ok
logical: 15990 (page:  62, offset: 118) ---> physical: 11894 -> value:   15  ok

logical: 20526 (page:  80, offset:  46) ---> physical: 12078 -> value:   20  ok
logical: 25904 (page: 101, offset:  48) ---> physical: 12336 -> value:    0  ok
logical: 42224 (page: 164, offset: 240) ---> physical: 12784 -> value:    0  ok
logical:  9311 (page:  36, offset:  95) ---> physical: 12895 -> value:   23  ok
logical:  7862 (page:  30, offset: 182) ---> physical: 13238 -> value:    7  ok

logical:  3835 (page:  14, offset: 251) ---> physical: 25083 -> value:  -66  ok
logical: 30535 (page: 119, offset:  71) ---> physical: 25415 -> value:  -47  ok
logical: 65179 (page: 254, offset: 155) ---> physical: 13467 -> value:  -90  ok
logical: 57387 (page: 224, offset:  43) ---> physical: 21803 -> value:   10  ok
logical: 63579 (page: 248, offset:  91) ---> physical: 29019 -> value:   22  ok

logical:  4946 (page:  19, offset:  82) ---> physical:  6482 -> value:    4  ok
logical:  9037 (page:  35, offset:  77) ---> physical:  2381 -> value:    0  ok
logical: 61033 (page: 238, offset: 105) ---> physical: 30057 -> value:    0  ok
logical: 55543 (page: 216, offset: 247) ---> physical: 13815 -> value:   61  ok
logical: 50361 (page: 196, offset: 185) ---> physical: 19385 -> value:    0  ok

logical:  6480 (page:  25, offset:  80) ---> physical: 13904 -> value:    0  ok
logical: 14042 (page:  54, offset: 218) ---> physical: 28890 -> value:   13  ok
logical: 21531 (page:  84, offset:  27) ---> physical:  7963 -> value:    6  ok
logical: 39195 (page: 153, offset:  27) ---> physical: 14107 -> value:   70  ok
logical: 37511 (page: 146, offset: 135) ---> physical: 10375 -> value:  -95  ok

logical: 23696 (page:  92, offset: 144) ---> physical: 10896 -> value:    0  ok
logical: 27440 (page: 107, offset:  48) ---> physical: 14384 -> value:    0  ok
logical: 28201 (page: 110, offset:  41) ---> physical: 14633 -> value:    0  ok
logical: 23072 (page:  90, offset:  32) ---> physical: 14880 -> value:    0  ok
logical:  7814 (page:  30, offset: 134) ---> physical: 13190 -> value:    7  ok

logical:  6552 (page:  25, offset: 152) ---> physical: 13976 -> value:    0  ok
logical: 43637 (page: 170, offset: 117) ---> physical: 15221 -> value:    0  ok
logical: 35113 (page: 137, offset:  41) ---> physical: 15401 -> value:    0  ok
logical: 34890 (page: 136, offset:  74) ---> physical: 15690 -> value:   34  ok
logical: 61297 (page: 239, offset: 113) ---> physical: 22129 -> value:    0  ok

logical: 45633 (page: 178, offset:  65) ---> physical: 28481 -> value:    0  ok
logical: 61431 (page: 239, offset: 247) ---> physical: 22263 -> value:   -3  ok
logical: 46032 (page: 179, offset: 208) ---> physical: 16080 -> value:    0  ok
logical: 18774 (page:  73, offset:  86) ---> physical: 16214 -> value:   18  ok
logical: 62991 (page: 246, offset:  15) ---> physical: 22799 -> value: -125  ok

logical: 28059 (page: 109, offset: 155) ---> physical: 16539 -> value:  102  ok
logical: 35229 (page: 137, offset: 157) ---> physical: 15517 -> value:    0  ok
logical: 51230 (page: 200, offset:  30) ---> physical: 16670 -> value:   50  ok
logical: 14405 (page:  56, offset:  69) ---> physical: 16965 -> value:    0  ok
logical: 52242 (page: 204, offset:  18) ---> physical: 17170 -> value:   51  ok

logical: 43153 (page: 168, offset: 145) ---> physical: 17553 -> value:    0  ok
logical:  2709 (page:  10, offset: 149) ---> physical: 17813 -> value:    0  ok
logical: 47963 (page: 187, offset:  91) ---> physical: 18011 -> value:  -42  ok
logical: 36943 (page: 144, offset:  79) ---> physical: 18255 -> value:   19  ok
logical: 54066 (page: 211, offset:  50) ---> physical: 18482 -> value:   52  ok

logical: 10054 (page:  39, offset:  70) ---> physical: 30278 -> value:    9  ok
logical: 43051 (page: 168, offset:  43) ---> physical: 17451 -> value:   10  ok
logical: 11525 (page:  45, offset:   5) ---> physical: 18693 -> value:    0  ok
logical: 17684 (page:  69, offset:  20) ---> physical: 18964 -> value:    0  ok
logical: 41681 (page: 162, offset: 209) ---> physical: 19409 -> value:    0  ok

logical: 27883 (page: 108, offset: 235) ---> physical: 19691 -> value:   58  ok
logical: 56909 (page: 222, offset:  77) ---> physical:  3917 -> value:    0  ok
logical: 45772 (page: 178, offset: 204) ---> physical: 28620 -> value:    0  ok
logical: 27496 (page: 107, offset: 104) ---> physical: 14440 -> value:    0  ok
logical: 46842 (page: 182, offset: 250) ---> physical: 19962 -> value:   45  ok

logical: 38734 (page: 151, offset:  78) ---> physical: 27726 -> value:   37  ok
logical: 28972 (page: 113, offset:  44) ---> physical: 20012 -> value:    0  ok
logical: 59684 (page: 233, offset:  36) ---> physical: 20260 -> value:    0  ok
logical: 11384 (page:  44, offset: 120) ---> physical: 20600 -> value:    0  ok
logical: 21018 (page:  82, offset:  26) ---> physical: 20762 -> value:   20  ok

logical:  2192 (page:   8, offset: 144) ---> physical: 21136 -> value:    0  ok
logical: 18384 (page:  71, offset: 208) ---> physical: 26320 -> value:    0  ok
logical: 13464 (page:  52, offset: 152) ---> physical:  9112 -> value:    0  ok
logical: 31018 (page: 121, offset:  42) ---> physical: 21290 -> value:   30  ok
logical: 62958 (page: 245, offset: 238) ---> physical: 21742 -> value:   61  ok

logical: 30611 (page: 119, offset: 147) ---> physical: 25491 -> value:  -28  ok
logical:  1913 (page:   7, offset: 121) ---> physical: 21881 -> value:    0  ok
logical: 18904 (page:  73, offset: 216) ---> physical: 16344 -> value:    0  ok
logical: 26773 (page: 104, offset: 149) ---> physical: 27285 -> value:    0  ok
logical: 55491 (page: 216, offset: 195) ---> physical: 13763 -> value:   48  ok

logical: 21899 (page:  85, offset: 139) ---> physical: 22667 -> value:   98  ok
logical: 64413 (page: 251, offset: 157) ---> physical: 22429 -> value:    0  ok
logical: 47134 (page: 184, offset:  30) ---> physical: 22046 -> value:   46  ok
logical: 23172 (page:  90, offset: 132) ---> physical: 14980 -> value:    0  ok
logical:  7262 (page:  28, offset:  94) ---> physical: 22366 -> value:    7  ok

logical: 12705 (page:  49, offset: 161) ---> physical: 22689 -> value:    0  ok
logical:  7522 (page:  29, offset:  98) ---> physical: 22882 -> value:    7  ok
logical: 58815 (page: 229, offset: 191) ---> physical: 23231 -> value:  111  ok
logical: 34916 (page: 136, offset: 100) ---> physical: 15716 -> value:    0  ok
logical:  3802 (page:  14, offset: 218) ---> physical: 25050 -> value:    3  ok

logical: 58008 (page: 226, offset: 152) ---> physical: 23448 -> value:    0  ok
logical:  1239 (page:   4, offset: 215) ---> physical: 23767 -> value:   53  ok
logical: 63947 (page: 249, offset: 203) ---> physical: 24011 -> value:  114  ok
logical:   381 (page:   1, offset: 125) ---> physical: 24189 -> value:    0  ok
logical: 60734 (page: 237, offset:  62) ---> physical:  3646 -> value:   59  ok

logical: 48769 (page: 190, offset: 129) ---> physical: 24449 -> value:    0  ok
logical: 41938 (page: 163, offset: 210) ---> physical: 24786 -> value:   40  ok
logical: 38025 (page: 148, offset: 137) ---> physical:  5257 -> value:    0  ok
logical: 55099 (page: 215, offset:  59) ---> physical: 24891 -> value:  -50  ok
logical: 56691 (page: 221, offset: 115) ---> physical: 26739 -> value:   92  ok

logical: 39530 (page: 154, offset: 106) ---> physical: 30570 -> value:   38  ok
logical: 59003 (page: 230, offset: 123) ---> physical: 30843 -> value:  -98  ok
logical:  6029 (page:  23, offset: 141) ---> physical: 10125 -> value:    0  ok
logical: 20920 (page:  81, offset: 184) ---> physical: 25272 -> value:    0  ok
logical:  8077 (page:  31, offset: 141) ---> physical: 25485 -> value:    0  ok

logical: 42633 (page: 166, offset: 137) ---> physical: 25737 -> value:    0  ok
logical: 17443 (page:  68, offset:  35) ---> physical: 25891 -> value:    8  ok
logical: 53570 (page: 209, offset:  66) ---> physical: 32322 -> value:   52  ok
logical: 22833 (page:  89, offset:  49) ---> physical: 26161 -> value:    0  ok
logical:  3782 (page:  14, offset: 198) ---> physical: 26566 -> value:    3  ok

logical: 47758 (page: 186, offset: 142) ---> physical: 26766 -> value:   46  ok
logical: 22136 (page:  86, offset: 120) ---> physical: 27000 -> value:    0  ok
logical: 22427 (page:  87, offset: 155) ---> physical: 11419 -> value:  -26  ok
logical: 23867 (page:  93, offset:  59) ---> physical:  6203 -> value:   78  ok
logical: 59968 (page: 234, offset:  64) ---> physical: 27200 -> value:    0  ok

logical: 62166 (page: 242, offset: 214) ---> physical: 27606 -> value:   60  ok
logical:  6972 (page:  27, offset:  60) ---> physical: 27708 -> value:    0  ok
logical: 63684 (page: 248, offset: 196) ---> physical: 29124 -> value:    0  ok
logical: 46388 (page: 181, offset:  52) ---> physical: 32564 -> value:    0  ok
logical: 41942 (page: 163, offset: 214) ---> physical: 24790 -> value:   40  ok

logical: 36524 (page: 142, offset: 172) ---> physical: 28076 -> value:    0  ok
logical:  9323 (page:  36, offset: 107) ---> physical: 12907 -> value:   26  ok
logical: 31114 (page: 121, offset: 138) ---> physical: 21386 -> value:   30  ok
logical: 22345 (page:  87, offset:  73) ---> physical: 11337 -> value:    0  ok
logical: 46463 (page: 181, offset: 127) ---> physical: 32639 -> value:   95  ok

logical: 54671 (page: 213, offset: 143) ---> physical: 28303 -> value:   99  ok
logical:  9214 (page:  35, offset: 254) ---> physical:  2558 -> value:    8  ok
logical:  7257 (page:  28, offset:  89) ---> physical: 22361 -> value:    0  ok
logical: 33150 (page: 129, offset: 126) ---> physical: 28542 -> value:   32  ok
logical: 41565 (page: 162, offset:  93) ---> physical: 19293 -> value:    0  ok

logical: 26214 (page: 102, offset: 102) ---> physical: 28774 -> value:   25  ok
logical:  3595 (page:  14, offset:  11) ---> physical: 26379 -> value: -126  ok
logical: 17932 (page:  70, offset:  12) ---> physical: 28940 -> value:    0  ok
logical: 34660 (page: 135, offset: 100) ---> physical: 29284 -> value:    0  ok
logical: 51961 (page: 202, offset: 249) ---> physical: 29689 -> value:    0  ok

logical: 58634 (page: 229, offset:  10) ---> physical: 23050 -> value:   57  ok
logical: 57990 (page: 226, offset: 134) ---> physical: 23430 -> value:   56  ok
logical: 28848 (page: 112, offset: 176) ---> physical:  3504 -> value:    0  ok
logical: 49920 (page: 195, offset:   0) ---> physical: 29696 -> value:    0  ok
logical: 18351 (page:  71, offset: 175) ---> physical: 30127 -> value:  -21  ok

logical: 53669 (page: 209, offset: 165) ---> physical: 32421 -> value:    0  ok
logical: 33996 (page: 132, offset: 204) ---> physical: 30412 -> value:    0  ok
logical:  6741 (page:  26, offset:  85) ---> physical: 30549 -> value:    0  ok
logical: 64098 (page: 250, offset:  98) ---> physical: 30818 -> value:   62  ok
logical:   606 (page:   2, offset:  94) ---> physical: 31070 -> value:    0  ok

logical: 27383 (page: 106, offset: 247) ---> physical: 31479 -> value:  -67  ok
logical: 63140 (page: 246, offset: 164) ---> physical: 31652 -> value:    0  ok
logical: 32228 (page: 125, offset: 228) ---> physical: 31972 -> value:    0  ok
logical: 63437 (page: 247, offset: 205) ---> physical: 32205 -> value:    0  ok
logical: 29085 (page: 113, offset: 157) ---> physical: 20125 -> value:    0  ok

logical: 65080 (page: 254, offset:  56) ---> physical: 13368 -> value:    0  ok
logical: 38753 (page: 151, offset:  97) ---> physical: 32353 -> value:    0  ok
logical: 16041 (page:  62, offset: 169) ---> physical: 11945 -> value:    0  ok
logical:  9041 (page:  35, offset:  81) ---> physical:  2385 -> value:    0  ok
logical: 42090 (page: 164, offset: 106) ---> physical: 12650 -> value:   41  ok

logical: 46388 (page: 181, offset:  52) ---> physical: 32564 -> value:    0  ok
logical: 63650 (page: 248, offset: 162) ---> physical: 32674 -> value:   62  ok
logical: 36636 (page: 143, offset:  28) ---> physical:    28 -> value:    0  ok
logical: 21947 (page:  85, offset: 187) ---> physical:   443 -> value:  110  ok
logical: 19833 (page:  77, offset: 121) ---> physical:   633 -> value:    0  ok

logical: 36464 (page: 142, offset: 112) ---> physical: 28016 -> value:    0  ok
logical:  8541 (page:  33, offset:  93) ---> physical:   861 -> value:    0  ok
logical: 12712 (page:  49, offset: 168) ---> physical: 22696 -> value:    0  ok
logical: 48955 (page: 191, offset:  59) ---> physical:  6971 -> value:  -50  ok
logical: 39206 (page: 153, offset:  38) ---> physical: 14118 -> value:   38  ok

logical: 15578 (page:  60, offset: 218) ---> physical:  1242 -> value:   15  ok
logical: 49205 (page: 192, offset:  53) ---> physical:  1333 -> value:    0  ok
logical:  7731 (page:  30, offset:  51) ---> physical: 13107 -> value: -116  ok
logical: 43046 (page: 168, offset:  38) ---> physical: 17446 -> value:   42  ok
logical: 60498 (page: 236, offset:  82) ---> physical:  1618 -> value:   59  ok

logical:  9237 (page:  36, offset:  21) ---> physical: 12821 -> value:    0  ok
logical: 47706 (page: 186, offset:  90) ---> physical: 26714 -> value:   46  ok
logical: 43973 (page: 171, offset: 197) ---> physical:  1989 -> value:    0  ok
logical: 42008 (page: 164, offset:  24) ---> physical: 12568 -> value:    0  ok
logical: 27460 (page: 107, offset:  68) ---> physical: 14404 -> value:    0  ok

logical: 24999 (page:  97, offset: 167) ---> physical:  2215 -> value:  105  ok
logical: 51933 (page: 202, offset: 221) ---> physical: 29661 -> value:    0  ok
logical: 34070 (page: 133, offset:  22) ---> physical:  6678 -> value:   33  ok
logical: 65155 (page: 254, offset: 131) ---> physical: 13443 -> value:  -96  ok
logical: 59955 (page: 234, offset:  51) ---> physical: 27187 -> value: -116  ok

logical:  9277 (page:  36, offset:  61) ---> physical: 12861 -> value:    0  ok
logical: 20420 (page:  79, offset: 196) ---> physical:  2500 -> value:    0  ok
logical: 44860 (page: 175, offset:  60) ---> physical:  5948 -> value:    0  ok
logical: 50992 (page: 199, offset:  48) ---> physical:  2608 -> value:    0  ok
logical: 10583 (page:  41, offset:  87) ---> physical:  2903 -> value:   85  ok

logical: 57751 (page: 225, offset: 151) ---> physical:  9879 -> value:  101  ok
logical: 23195 (page:  90, offset: 155) ---> physical: 15003 -> value:  -90  ok
logical: 27227 (page: 106, offset:  91) ---> physical: 31323 -> value: -106  ok
logical: 42816 (page: 167, offset:  64) ---> physical:  4416 -> value:    0  ok
logical: 58219 (page: 227, offset: 107) ---> physical:  3179 -> value:  -38  ok

logical: 37606 (page: 146, offset: 230) ---> physical: 10470 -> value:   36  ok
logical: 18426 (page:  71, offset: 250) ---> physical: 30202 -> value:   17  ok
logical: 21238 (page:  82, offset: 246) ---> physical: 20982 -> value:   20  ok
logical: 11983 (page:  46, offset: 207) ---> physical:  3535 -> value:  -77  ok
logical: 48394 (page: 189, offset:  10) ---> physical:  3594 -> value:   47  ok

logical: 11036 (page:  43, offset:  28) ---> physical:  3868 -> value:    0  ok
logical: 30557 (page: 119, offset:  93) ---> physical:  4189 -> value:    0  ok
logical: 23453 (page:  91, offset: 157) ---> physical:  4509 -> value:    0  ok
logical: 49847 (page: 194, offset: 183) ---> physical:  4791 -> value:  -83  ok
logical: 30032 (page: 117, offset:  80) ---> physical:  4944 -> value:    0  ok

logical: 48065 (page: 187, offset: 193) ---> physical: 18113 -> value:    0  ok
logical:  6957 (page:  27, offset:  45) ---> physical: 27693 -> value:    0  ok
logical:  2301 (page:   8, offset: 253) ---> physical: 21245 -> value:    0  ok
logical:  7736 (page:  30, offset:  56) ---> physical: 13112 -> value:    0  ok
logical: 31260 (page: 122, offset:  28) ---> physical:  5148 -> value:    0  ok

logical: 17071 (page:  66, offset: 175) ---> physical:  5551 -> value:  -85  ok
logical:  8940 (page:  34, offset: 236) ---> physical:  5868 -> value:    0  ok
logical:  9929 (page:  38, offset: 201) ---> physical:  6089 -> value:    0  ok
logical: 45563 (page: 177, offset: 251) ---> physical:  6395 -> value:  126  ok
logical: 12107 (page:  47, offset:  75) ---> physical:  6475 -> value:  -46  ok

ALL read memory value assertions PASSED!

		... nPages != nFrames memory simulation done.


nPages == nFrames Statistics (256 frames):
Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate
      200           12                139             0.0600         0.6950
      400           25                204             0.0625         0.5100
      600           33                234             0.0550         0.3900
      800           47                241             0.0587         0.3013
     1000           54                244             0.0540         0.2440

nPages != nFrames Statistics (128 frames):
Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate
      200           12                139             0.0600         0.6950
      400           25                242             0.0625         0.6050
      600           33                345             0.0550         0.5750
      800           47                431             0.0587         0.5387
     1000           54                538             0.0540         0.5380

		...memory management simulation completed!
(base) 
*/

