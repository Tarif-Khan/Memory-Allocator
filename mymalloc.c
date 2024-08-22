#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <malloc.h> 
#include <stdio.h> 
#include <assert.h>
#include <unistd.h>
#include <pthread.h> 
#include <sys/mman.h>
#include <string.h>
#include <debug.h>

// Block structure data type taken from Course Website.
typedef struct block {
  size_t size;        // How many bytes beyond this block have been allocated in the heap
  struct block *next; // Where is the next block in your linked list
  int free;           // Is this memory free, i.e., available to give away?
} block_t;

//Size of given block.
#define BLOCK_SIZE sizeof(block_t)

//Header pointer to keep track of blocks.
void *base_ptr = NULL;

//These are our mutexes.
pthread_mutex_t free_m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t malloc_m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sbrk_m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mmap_m = PTHREAD_MUTEX_INITIALIZER;

//Gets the first block that is free and fits the size we want.
block_t *get_next_fit_block(size_t s);

//Adds a block at the end.
void add_block_at_end(block_t *block);

//Requests to allocate memory that is less than PAGE_SIZE.
block_t *create_small_block(size_t s);

//Requests to allocate memory that is greater than PAGE_SIZE.
block_t *create_big_block(size_t s);

//Our my malloc function.
void *mymalloc(size_t s);

//Our my calloc function.
void *mycalloc(size_t nmemb, size_t s);

//Our my free function.
void myfree(void *ptr);

//Gets the next block which will fit the appropriate size passed in.
block_t *get_next_fit_block(size_t s) {
  assert(s > 0);
  block_t *block = base_ptr;
  //Check if block is free and the size needed can be accomodated for.
  while (block != NULL) {
    //If it fits the size and is free, return that block, if not move to the next block.
   if (block->size >= s && block->free == 1) { 
      return block;
    }
    block = block->next;
  }
  return NULL;
}

//Adds the block at the end of the list.
void add_block_at_end(block_t *block) {
  //If there are no other blocks, make this the first block. Single element list.
  if (base_ptr == NULL) {
    base_ptr = block;
    return;
  }
  //If there are other blocks, go to the end and add the last block.
  block_t *temp_block = base_ptr;
  while(temp_block->next != NULL) {
    temp_block = temp_block->next;
  }
  temp_block->next = block;
}
//This is for making blocks less than PAGE SIZE. (Splitting part 1 where we split small and big blocks.)
block_t *create_small_block(size_t s) {
  assert(s > 0);
  //Initialize a small block.
  block_t *small_block;
  //We had trouble using mmap for small blocks (segmentation faults) so we used sbrk. (Spare us please!)
  small_block = sbrk(0); 
  pthread_mutex_lock(&sbrk_m);
  void *mem_allocation = sbrk(s + BLOCK_SIZE); 
  pthread_mutex_unlock(&sbrk_m);
  //This means that if true, the memory allocation request failed.
  if (mem_allocation == (void*) -1) {
    return NULL; 
  }
  //We add the values of the small block to itself. 
  small_block->size = s;
  small_block->next = NULL;
  small_block->free = 0;
  return small_block;
}

//Here we create big blocks if the size >= Page Size (Splitting part 2 where we split small and big blocks).
block_t *create_big_block(size_t s) {
  assert(s > 0);
  //We request memory allocation here using mmap. (Works!)
  pthread_mutex_lock(&mmap_m);
  void *mem_allocation = mmap(NULL, BLOCK_SIZE + s, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANON, -1, 0);
  pthread_mutex_unlock(&mmap_m);
  //If true, then memory allocation failed.
  if (mem_allocation == (void*) -1) {
    return NULL; 
  }
  //HERE WE Coalese FREE BLOCKS if there is remaining space and the values apply.
  block_t *remaining = NULL;
  size_t remaining_size = (BLOCK_SIZE + s) % sysconf(_SC_PAGE_SIZE);
  if (remaining_size <= BLOCK_SIZE) {
    remaining = mem_allocation + (BLOCK_SIZE + s);
    remaining->size = remaining_size;
    remaining->next = NULL;
    remaining->free = 1;
  }
  //Here we create that (Big Block) where it has the first block plus the coalesed block next.
  block_t *big_block = (block_t*) mem_allocation;
  big_block->size = s + BLOCK_SIZE;
  big_block->next = remaining;
  big_block->free = 0;
  return big_block;
}

//This is our method for malloc
void *mymalloc(size_t s) {
  assert(s > 0);
  //If there is no leading block or the list is null, we create a small block here.
  block_t *block;
  if (base_ptr != NULL) {
    //If there is blocks in the list, we create a small/big block and place at the end depending on value of s!.
      pthread_mutex_lock(&malloc_m);
      block = get_next_fit_block(s);
      pthread_mutex_unlock(&malloc_m);
      if (block == NULL) {
        //here we compare and see which block we will make.
        if (BLOCK_SIZE + s >= sysconf(_SC_PAGE_SIZE)) {
          block = create_big_block(s);
        } else {
            block = create_small_block(s);
        }
        assert(block != NULL);
        //we add the block created at the end of the list.
        add_block_at_end(block);
      } else {
        block->free = 0;
      }
  } else {
    block = create_small_block(s);
    assert(block != NULL);
    base_ptr = block;
    }
  debug_printf("Malloc %zu bytes\n", s);
  return (block + 1);
}

//This is our calloc function
void *mycalloc(size_t nmemb, size_t s) {
  assert(nmemb > 0 && s > 0); 
  
  size_t total = nmemb * s;
  assert(total > 0);
  
  void *block_ptr = mymalloc(total);
  if (block_ptr == NULL) {
    return NULL; 
  } else {
  memset(block_ptr, 0, total); 
  }
  debug_printf("calloc %zu bytes\n", s);
  return block_ptr;
}

//This is our function to free memory. It makes use of an implicit list by going through blocks with the use of the pointer.
void myfree(void *ptr) {
  pthread_mutex_lock(&free_m);
  block_t *free_block = (block_t *)(ptr - BLOCK_SIZE);
  assert(free_block != NULL);
  assert(free_block->free == 0);
  free_block->free = 1;
  pthread_mutex_unlock(&free_m);
  debug_printf("Freed %zu bytes\n", free_block->size);
}
