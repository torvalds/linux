/*===- GCDAProfiling.c - Support library for GCDA file emission -----------===*\
|*
|*                     The LLVM Compiler Infrastructure
|*
|* This file is distributed under the University of Illinois Open Source
|* License. See LICENSE.TXT for details.
|* 
|*===----------------------------------------------------------------------===*|
|* 
|* This file implements the call back routines for the gcov profiling
|* instrumentation pass. Link against this library when running code through
|* the -insert-gcov-profiling LLVM pass.
|*
|* We emit files in a corrupt version of GCOV's "gcda" file format. These files
|* are only close enough that LCOV will happily parse them. Anything that lcov
|* ignores is missing.
|*
|* TODO: gcov is multi-process safe by having each exit open the existing file
|* and append to it. We'd like to achieve that and be thread-safe too.
|*
\*===----------------------------------------------------------------------===*/

#if !defined(__Fuchsia__)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "WindowsMMap.h"
#else
#include <sys/mman.h>
#include <sys/file.h>
#endif

#if defined(__FreeBSD__) && defined(__i386__)
#define I386_FREEBSD 1
#else
#define I386_FREEBSD 0
#endif

#if !defined(_MSC_VER) && !I386_FREEBSD
#include <stdint.h>
#endif

#if defined(_MSC_VER)
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#elif I386_FREEBSD
/* System headers define 'size_t' incorrectly on x64 FreeBSD (prior to
 * FreeBSD 10, r232261) when compiled in 32-bit mode.
 */
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#endif

#include "InstrProfiling.h"
#include "InstrProfilingUtil.h"

/* #define DEBUG_GCDAPROFILING */

/*
 * --- GCOV file format I/O primitives ---
 */

/*
 * The current file name we're outputting. Used primarily for error logging.
 */
static char *filename = NULL;

/*
 * The current file we're outputting.
 */ 
static FILE *output_file = NULL;

/*
 * Buffer that we write things into.
 */
#define WRITE_BUFFER_SIZE (128 * 1024)
static unsigned char *write_buffer = NULL;
static uint64_t cur_buffer_size = 0;
static uint64_t cur_pos = 0;
static uint64_t file_size = 0;
static int new_file = 0;
#if defined(_WIN32)
static HANDLE mmap_handle = NULL;
#endif
static int fd = -1;

typedef void (*fn_ptr)();

typedef void* dynamic_object_id;
// The address of this variable identifies a given dynamic object.
static dynamic_object_id current_id;
#define CURRENT_ID (&current_id)

struct fn_node {
  dynamic_object_id id;
  fn_ptr fn;
  struct fn_node* next;
};

struct fn_list {
  struct fn_node *head, *tail;
};

/*
 * A list of functions to write out the data, shared between all dynamic objects.
 */
struct fn_list writeout_fn_list;

/*
 *  A list of flush functions that our __gcov_flush() function should call, shared between all dynamic objects.
 */
struct fn_list flush_fn_list;

static void fn_list_insert(struct fn_list* list, fn_ptr fn) {
  struct fn_node* new_node = malloc(sizeof(struct fn_node));
  new_node->fn = fn;
  new_node->next = NULL;
  new_node->id = CURRENT_ID;

  if (!list->head) {
    list->head = list->tail = new_node;
  } else {
    list->tail->next = new_node;
    list->tail = new_node;
  }
}

static void fn_list_remove(struct fn_list* list) {
  struct fn_node* curr = list->head;
  struct fn_node* prev = NULL;
  struct fn_node* next = NULL;

  while (curr) {
    next = curr->next;

    if (curr->id == CURRENT_ID) {
      if (curr == list->head) {
        list->head = next;
      }

      if (curr == list->tail) {
        list->tail = prev;
      }

      if (prev) {
        prev->next = next;
      }

      free(curr);
    } else {
      prev = curr;
    }

    curr = next;
  }
}

static void resize_write_buffer(uint64_t size) {
  if (!new_file) return;
  size += cur_pos;
  if (size <= cur_buffer_size) return;
  size = (size - 1) / WRITE_BUFFER_SIZE + 1;
  size *= WRITE_BUFFER_SIZE;
  write_buffer = realloc(write_buffer, size);
  cur_buffer_size = size;
}

static void write_bytes(const char *s, size_t len) {
  resize_write_buffer(len);
  memcpy(&write_buffer[cur_pos], s, len);
  cur_pos += len;
}

static void write_32bit_value(uint32_t i) {
  write_bytes((char*)&i, 4);
}

static void write_64bit_value(uint64_t i) {
  // GCOV uses a lo-/hi-word format even on big-endian systems.
  // See also GCOVBuffer::readInt64 in LLVM.
  uint32_t lo = (uint32_t) i;
  uint32_t hi = (uint32_t) (i >> 32);
  write_32bit_value(lo);
  write_32bit_value(hi);
}

static uint32_t length_of_string(const char *s) {
  return (strlen(s) / 4) + 1;
}

static void write_string(const char *s) {
  uint32_t len = length_of_string(s);
  write_32bit_value(len);
  write_bytes(s, strlen(s));
  write_bytes("\0\0\0\0", 4 - (strlen(s) % 4));
}

static uint32_t read_32bit_value() {
  uint32_t val;

  if (new_file)
    return (uint32_t)-1;

  val = *(uint32_t*)&write_buffer[cur_pos];
  cur_pos += 4;
  return val;
}

static uint32_t read_le_32bit_value() {
  uint32_t val = 0;
  int i;

  if (new_file)
    return (uint32_t)-1;

  for (i = 0; i < 4; i++)
    val |= write_buffer[cur_pos++] << (8*i);
  return val;
}

static uint64_t read_64bit_value() {
  // GCOV uses a lo-/hi-word format even on big-endian systems.
  // See also GCOVBuffer::readInt64 in LLVM.
  uint32_t lo = read_32bit_value();
  uint32_t hi = read_32bit_value();
  return ((uint64_t)hi << 32) | ((uint64_t)lo);
}

static char *mangle_filename(const char *orig_filename) {
  char *new_filename;
  size_t prefix_len;
  int prefix_strip;
  const char *prefix = lprofGetPathPrefix(&prefix_strip, &prefix_len);

  if (prefix == NULL)
    return strdup(orig_filename);

  new_filename = malloc(prefix_len + 1 + strlen(orig_filename) + 1);
  lprofApplyPathPrefix(new_filename, orig_filename, prefix, prefix_len,
                       prefix_strip);

  return new_filename;
}

static int map_file() {
  fseek(output_file, 0L, SEEK_END);
  file_size = ftell(output_file);

  /* A size of 0 is invalid to `mmap'. Return a fail here, but don't issue an
   * error message because it should "just work" for the user. */
  if (file_size == 0)
    return -1;

#if defined(_WIN32)
  HANDLE mmap_fd;
  if (fd == -1)
    mmap_fd = INVALID_HANDLE_VALUE;
  else
    mmap_fd = (HANDLE)_get_osfhandle(fd);

  mmap_handle = CreateFileMapping(mmap_fd, NULL, PAGE_READWRITE, DWORD_HI(file_size), DWORD_LO(file_size), NULL);
  if (mmap_handle == NULL) {
    fprintf(stderr, "profiling: %s: cannot create file mapping: %d\n", filename,
            GetLastError());
    return -1;
  }

  write_buffer = MapViewOfFile(mmap_handle, FILE_MAP_WRITE, 0, 0, file_size);
  if (write_buffer == NULL) {
    fprintf(stderr, "profiling: %s: cannot map: %d\n", filename,
            GetLastError());
    CloseHandle(mmap_handle);
    return -1;
  }
#else
  write_buffer = mmap(0, file_size, PROT_READ | PROT_WRITE,
                      MAP_FILE | MAP_SHARED, fd, 0);
  if (write_buffer == (void *)-1) {
    int errnum = errno;
    fprintf(stderr, "profiling: %s: cannot map: %s\n", filename,
            strerror(errnum));
    return -1;
  }
#endif

  return 0;
}

static void unmap_file() {
#if defined(_WIN32)
  if (!FlushViewOfFile(write_buffer, file_size)) {
    fprintf(stderr, "profiling: %s: cannot flush mapped view: %d\n", filename,
            GetLastError());
  }

  if (!UnmapViewOfFile(write_buffer)) {
    fprintf(stderr, "profiling: %s: cannot unmap mapped view: %d\n", filename,
            GetLastError());
  }

  if (!CloseHandle(mmap_handle)) {
    fprintf(stderr, "profiling: %s: cannot close file mapping handle: %d\n", filename,
            GetLastError());
  }

  mmap_handle = NULL;
#else
  if (msync(write_buffer, file_size, MS_SYNC) == -1) {
    int errnum = errno;
    fprintf(stderr, "profiling: %s: cannot msync: %s\n", filename,
            strerror(errnum));
  }

  /* We explicitly ignore errors from unmapping because at this point the data
   * is written and we don't care.
   */
  (void)munmap(write_buffer, file_size);
#endif

  write_buffer = NULL;
  file_size = 0;
}

/*
 * --- LLVM line counter API ---
 */

/* A file in this case is a translation unit. Each .o file built with line
 * profiling enabled will emit to a different file. Only one file may be
 * started at a time.
 */
COMPILER_RT_VISIBILITY
void llvm_gcda_start_file(const char *orig_filename, const char version[4],
                          uint32_t checksum) {
  const char *mode = "r+b";
  filename = mangle_filename(orig_filename);

  /* Try just opening the file. */
  new_file = 0;
  fd = open(filename, O_RDWR | O_BINARY);

  if (fd == -1) {
    /* Try opening the file, creating it if necessary. */
    new_file = 1;
    mode = "w+b";
    fd = open(filename, O_RDWR | O_CREAT | O_BINARY, 0644);
    if (fd == -1) {
      /* Try creating the directories first then opening the file. */
      __llvm_profile_recursive_mkdir(filename);
      fd = open(filename, O_RDWR | O_CREAT | O_BINARY, 0644);
      if (fd == -1) {
        /* Bah! It's hopeless. */
        int errnum = errno;
        fprintf(stderr, "profiling: %s: cannot open: %s\n", filename,
                strerror(errnum));
        return;
      }
    }
  }

  /* Try to flock the file to serialize concurrent processes writing out to the
   * same GCDA. This can fail if the filesystem doesn't support it, but in that
   * case we'll just carry on with the old racy behaviour and hope for the best.
   */
  lprofLockFd(fd);
  output_file = fdopen(fd, mode);

  /* Initialize the write buffer. */
  write_buffer = NULL;
  cur_buffer_size = 0;
  cur_pos = 0;

  if (new_file) {
    resize_write_buffer(WRITE_BUFFER_SIZE);
    memset(write_buffer, 0, WRITE_BUFFER_SIZE);
  } else {
    if (map_file() == -1) {
      /* mmap failed, try to recover by clobbering */
      new_file = 1;
      write_buffer = NULL;
      cur_buffer_size = 0;
      resize_write_buffer(WRITE_BUFFER_SIZE);
      memset(write_buffer, 0, WRITE_BUFFER_SIZE);
    }
  }

  /* gcda file, version, stamp checksum. */
  write_bytes("adcg", 4);
  write_bytes(version, 4);
  write_32bit_value(checksum);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: [%s]\n", orig_filename);
#endif
}

/* Given an array of pointers to counters (counters), increment the n-th one,
 * where we're also given a pointer to n (predecessor).
 */
COMPILER_RT_VISIBILITY
void llvm_gcda_increment_indirect_counter(uint32_t *predecessor,
                                          uint64_t **counters) {
  uint64_t *counter;
  uint32_t pred;

  pred = *predecessor;
  if (pred == 0xffffffff)
    return;
  counter = counters[pred];

  /* Don't crash if the pred# is out of sync. This can happen due to threads,
     or because of a TODO in GCOVProfiling.cpp buildEdgeLookupTable(). */
  if (counter)
    ++*counter;
#ifdef DEBUG_GCDAPROFILING
  else
    fprintf(stderr,
            "llvmgcda: increment_indirect_counter counters=%08llx, pred=%u\n",
            *counter, *predecessor);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_emit_function(uint32_t ident, const char *function_name,
                             uint32_t func_checksum, uint8_t use_extra_checksum,
                             uint32_t cfg_checksum) {
  uint32_t len = 2;

  if (use_extra_checksum)
    len++;
#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: function id=0x%08x name=%s\n", ident,
          function_name ? function_name : "NULL");
#endif
  if (!output_file) return;

  /* function tag */
  write_bytes("\0\0\0\1", 4);
  if (function_name)
    len += 1 + length_of_string(function_name);
  write_32bit_value(len);
  write_32bit_value(ident);
  write_32bit_value(func_checksum);
  if (use_extra_checksum)
    write_32bit_value(cfg_checksum);
  if (function_name)
    write_string(function_name);
}

COMPILER_RT_VISIBILITY
void llvm_gcda_emit_arcs(uint32_t num_counters, uint64_t *counters) {
  uint32_t i;
  uint64_t *old_ctrs = NULL;
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  if (!output_file) return;

  val = read_le_32bit_value();

  if (val != (uint32_t)-1) {
    /* There are counters present in the file. Merge them. */
    if (val != 0x01a10000) {
      fprintf(stderr, "profiling: %s: cannot merge previous GCDA file: "
                      "corrupt arc tag (0x%08x)\n",
              filename, val);
      return;
    }

    val = read_32bit_value();
    if (val == (uint32_t)-1 || val / 2 != num_counters) {
      fprintf(stderr, "profiling: %s: cannot merge previous GCDA file: "
                      "mismatched number of counters (%d)\n",
              filename, val);
      return;
    }

    old_ctrs = malloc(sizeof(uint64_t) * num_counters);
    for (i = 0; i < num_counters; ++i)
      old_ctrs[i] = read_64bit_value();
  }

  cur_pos = save_cur_pos;

  /* Counter #1 (arcs) tag */
  write_bytes("\0\0\xa1\1", 4);
  write_32bit_value(num_counters * 2);
  for (i = 0; i < num_counters; ++i) {
    counters[i] += (old_ctrs ? old_ctrs[i] : 0);
    write_64bit_value(counters[i]);
  }

  free(old_ctrs);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda:   %u arcs\n", num_counters);
  for (i = 0; i < num_counters; ++i)
    fprintf(stderr, "llvmgcda:   %llu\n", (unsigned long long)counters[i]);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_summary_info() {
  const uint32_t obj_summary_len = 9; /* Length for gcov compatibility. */
  uint32_t i;
  uint32_t runs = 1;
  static uint32_t run_counted = 0; // We only want to increase the run count once.
  uint32_t val = 0;
  uint64_t save_cur_pos = cur_pos;

  if (!output_file) return;

  val = read_le_32bit_value();

  if (val != (uint32_t)-1) {
    /* There are counters present in the file. Merge them. */
    if (val != 0xa1000000) {
      fprintf(stderr, "profiling: %s: cannot merge previous run count: "
                      "corrupt object tag (0x%08x)\n",
              filename, val);
      return;
    }

    val = read_32bit_value(); /* length */
    if (val != obj_summary_len) {
      fprintf(stderr, "profiling: %s: cannot merge previous run count: "
                      "mismatched object length (%d)\n",
              filename, val);
      return;
    }

    read_32bit_value(); /* checksum, unused */
    read_32bit_value(); /* num, unused */
    uint32_t prev_runs = read_32bit_value();
    /* Add previous run count to new counter, if not already counted before. */
    runs = run_counted ? prev_runs : prev_runs + 1;
  }

  cur_pos = save_cur_pos;

  /* Object summary tag */
  write_bytes("\0\0\0\xa1", 4);
  write_32bit_value(obj_summary_len);
  write_32bit_value(0); /* checksum, unused */
  write_32bit_value(0); /* num, unused */
  write_32bit_value(runs);
  for (i = 3; i < obj_summary_len; ++i)
    write_32bit_value(0);

  /* Program summary tag */
  write_bytes("\0\0\0\xa3", 4); /* tag indicates 1 program */
  write_32bit_value(0); /* 0 length */

  run_counted = 1;

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda:   %u runs\n", runs);
#endif
}

COMPILER_RT_VISIBILITY
void llvm_gcda_end_file() {
  /* Write out EOF record. */
  if (output_file) {
    write_bytes("\0\0\0\0\0\0\0\0", 8);

    if (new_file) {
      fwrite(write_buffer, cur_pos, 1, output_file);
      free(write_buffer);
    } else {
      unmap_file();
    }

    fflush(output_file);
    lprofUnlockFd(fd);
    fclose(output_file);
    output_file = NULL;
    write_buffer = NULL;
  }
  free(filename);

#ifdef DEBUG_GCDAPROFILING
  fprintf(stderr, "llvmgcda: -----\n");
#endif
}

COMPILER_RT_VISIBILITY
void llvm_register_writeout_function(fn_ptr fn) {
  fn_list_insert(&writeout_fn_list, fn);
}

COMPILER_RT_VISIBILITY
void llvm_writeout_files(void) {
  struct fn_node *curr = writeout_fn_list.head;

  while (curr) {
    if (curr->id == CURRENT_ID) {
      curr->fn();
    }
    curr = curr->next;
  }
}

COMPILER_RT_VISIBILITY
void llvm_delete_writeout_function_list(void) {
  fn_list_remove(&writeout_fn_list);
}

COMPILER_RT_VISIBILITY
void llvm_register_flush_function(fn_ptr fn) {
  fn_list_insert(&flush_fn_list, fn);
}

void __gcov_flush() {
  struct fn_node* curr = flush_fn_list.head;

  while (curr) {
    curr->fn();
    curr = curr->next;
  }
}

COMPILER_RT_VISIBILITY
void llvm_delete_flush_function_list(void) {
  fn_list_remove(&flush_fn_list);
}

COMPILER_RT_VISIBILITY
void llvm_gcov_init(fn_ptr wfn, fn_ptr ffn) {
  static int atexit_ran = 0;

  if (wfn)
    llvm_register_writeout_function(wfn);

  if (ffn)
    llvm_register_flush_function(ffn);

  if (atexit_ran == 0) {
    atexit_ran = 1;

    /* Make sure we write out the data and delete the data structures. */
    atexit(llvm_delete_flush_function_list);
    atexit(llvm_delete_writeout_function_list);
    atexit(llvm_writeout_files);
  }
}

#endif
