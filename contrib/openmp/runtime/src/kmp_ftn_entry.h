/*
 * kmp_ftn_entry.h -- Fortran entry linkage support for OpenMP.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef FTN_STDCALL
#error The support file kmp_ftn_entry.h should not be compiled by itself.
#endif

#ifdef KMP_STUB
#include "kmp_stub.h"
#endif

#include "kmp_i18n.h"

#if OMP_50_ENABLED
// For affinity format functions
#include "kmp_io.h"
#include "kmp_str.h"
#endif

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/* For compatibility with the Gnu/MS Open MP codegen, omp_set_num_threads(),
 * omp_set_nested(), and omp_set_dynamic() [in lowercase on MS, and w/o
 * a trailing underscore on Linux* OS] take call by value integer arguments.
 * + omp_set_max_active_levels()
 * + omp_set_schedule()
 *
 * For backward compatibility with 9.1 and previous Intel compiler, these
 * entry points take call by reference integer arguments. */
#ifdef KMP_GOMP_COMPAT
#if (KMP_FTN_ENTRIES == KMP_FTN_PLAIN) || (KMP_FTN_ENTRIES == KMP_FTN_UPPER)
#define PASS_ARGS_BY_VALUE 1
#endif
#endif
#if KMP_OS_WINDOWS
#if (KMP_FTN_ENTRIES == KMP_FTN_PLAIN) || (KMP_FTN_ENTRIES == KMP_FTN_APPEND)
#define PASS_ARGS_BY_VALUE 1
#endif
#endif

// This macro helps to reduce code duplication.
#ifdef PASS_ARGS_BY_VALUE
#define KMP_DEREF
#else
#define KMP_DEREF *
#endif

void FTN_STDCALL FTN_SET_STACKSIZE(int KMP_DEREF arg) {
#ifdef KMP_STUB
  __kmps_set_stacksize(KMP_DEREF arg);
#else
  // __kmp_aux_set_stacksize initializes the library if needed
  __kmp_aux_set_stacksize((size_t)KMP_DEREF arg);
#endif
}

void FTN_STDCALL FTN_SET_STACKSIZE_S(size_t KMP_DEREF arg) {
#ifdef KMP_STUB
  __kmps_set_stacksize(KMP_DEREF arg);
#else
  // __kmp_aux_set_stacksize initializes the library if needed
  __kmp_aux_set_stacksize(KMP_DEREF arg);
#endif
}

int FTN_STDCALL FTN_GET_STACKSIZE(void) {
#ifdef KMP_STUB
  return __kmps_get_stacksize();
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return (int)__kmp_stksize;
#endif
}

size_t FTN_STDCALL FTN_GET_STACKSIZE_S(void) {
#ifdef KMP_STUB
  return __kmps_get_stacksize();
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return __kmp_stksize;
#endif
}

void FTN_STDCALL FTN_SET_BLOCKTIME(int KMP_DEREF arg) {
#ifdef KMP_STUB
  __kmps_set_blocktime(KMP_DEREF arg);
#else
  int gtid, tid;
  kmp_info_t *thread;

  gtid = __kmp_entry_gtid();
  tid = __kmp_tid_from_gtid(gtid);
  thread = __kmp_thread_from_gtid(gtid);

  __kmp_aux_set_blocktime(KMP_DEREF arg, thread, tid);
#endif
}

int FTN_STDCALL FTN_GET_BLOCKTIME(void) {
#ifdef KMP_STUB
  return __kmps_get_blocktime();
#else
  int gtid, tid;
  kmp_info_t *thread;
  kmp_team_p *team;

  gtid = __kmp_entry_gtid();
  tid = __kmp_tid_from_gtid(gtid);
  thread = __kmp_thread_from_gtid(gtid);
  team = __kmp_threads[gtid]->th.th_team;

  /* These must match the settings used in __kmp_wait_sleep() */
  if (__kmp_dflt_blocktime == KMP_MAX_BLOCKTIME) {
    KF_TRACE(10, ("kmp_get_blocktime: T#%d(%d:%d), blocktime=%d\n", gtid,
                  team->t.t_id, tid, KMP_MAX_BLOCKTIME));
    return KMP_MAX_BLOCKTIME;
  }
#ifdef KMP_ADJUST_BLOCKTIME
  else if (__kmp_zero_bt && !get__bt_set(team, tid)) {
    KF_TRACE(10, ("kmp_get_blocktime: T#%d(%d:%d), blocktime=%d\n", gtid,
                  team->t.t_id, tid, 0));
    return 0;
  }
#endif /* KMP_ADJUST_BLOCKTIME */
  else {
    KF_TRACE(10, ("kmp_get_blocktime: T#%d(%d:%d), blocktime=%d\n", gtid,
                  team->t.t_id, tid, get__blocktime(team, tid)));
    return get__blocktime(team, tid);
  }
#endif
}

void FTN_STDCALL FTN_SET_LIBRARY_SERIAL(void) {
#ifdef KMP_STUB
  __kmps_set_library(library_serial);
#else
  // __kmp_user_set_library initializes the library if needed
  __kmp_user_set_library(library_serial);
#endif
}

void FTN_STDCALL FTN_SET_LIBRARY_TURNAROUND(void) {
#ifdef KMP_STUB
  __kmps_set_library(library_turnaround);
#else
  // __kmp_user_set_library initializes the library if needed
  __kmp_user_set_library(library_turnaround);
#endif
}

void FTN_STDCALL FTN_SET_LIBRARY_THROUGHPUT(void) {
#ifdef KMP_STUB
  __kmps_set_library(library_throughput);
#else
  // __kmp_user_set_library initializes the library if needed
  __kmp_user_set_library(library_throughput);
#endif
}

void FTN_STDCALL FTN_SET_LIBRARY(int KMP_DEREF arg) {
#ifdef KMP_STUB
  __kmps_set_library(KMP_DEREF arg);
#else
  enum library_type lib;
  lib = (enum library_type)KMP_DEREF arg;
  // __kmp_user_set_library initializes the library if needed
  __kmp_user_set_library(lib);
#endif
}

int FTN_STDCALL FTN_GET_LIBRARY(void) {
#ifdef KMP_STUB
  return __kmps_get_library();
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return ((int)__kmp_library);
#endif
}

void FTN_STDCALL FTN_SET_DISP_NUM_BUFFERS(int KMP_DEREF arg) {
#ifdef KMP_STUB
  ; // empty routine
#else
  // ignore after initialization because some teams have already
  // allocated dispatch buffers
  if (__kmp_init_serial == 0 && (KMP_DEREF arg) > 0)
    __kmp_dispatch_num_buffers = KMP_DEREF arg;
#endif
}

int FTN_STDCALL FTN_SET_AFFINITY(void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_set_affinity(mask);
#endif
}

int FTN_STDCALL FTN_GET_AFFINITY(void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_get_affinity(mask);
#endif
}

int FTN_STDCALL FTN_GET_AFFINITY_MAX_PROC(void) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return 0;
#else
  // We really only NEED serial initialization here.
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_get_affinity_max_proc();
#endif
}

void FTN_STDCALL FTN_CREATE_AFFINITY_MASK(void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  *mask = NULL;
#else
  // We really only NEED serial initialization here.
  kmp_affin_mask_t *mask_internals;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  mask_internals = __kmp_affinity_dispatch->allocate_mask();
  KMP_CPU_ZERO(mask_internals);
  *mask = mask_internals;
#endif
}

void FTN_STDCALL FTN_DESTROY_AFFINITY_MASK(void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
// Nothing
#else
  // We really only NEED serial initialization here.
  kmp_affin_mask_t *mask_internals;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (__kmp_env_consistency_check) {
    if (*mask == NULL) {
      KMP_FATAL(AffinityInvalidMask, "kmp_destroy_affinity_mask");
    }
  }
  mask_internals = (kmp_affin_mask_t *)(*mask);
  __kmp_affinity_dispatch->deallocate_mask(mask_internals);
  *mask = NULL;
#endif
}

int FTN_STDCALL FTN_SET_AFFINITY_MASK_PROC(int KMP_DEREF proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_set_affinity_mask_proc(KMP_DEREF proc, mask);
#endif
}

int FTN_STDCALL FTN_UNSET_AFFINITY_MASK_PROC(int KMP_DEREF proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_unset_affinity_mask_proc(KMP_DEREF proc, mask);
#endif
}

int FTN_STDCALL FTN_GET_AFFINITY_MASK_PROC(int KMP_DEREF proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_get_affinity_mask_proc(KMP_DEREF proc, mask);
#endif
}

/* ------------------------------------------------------------------------ */

/* sets the requested number of threads for the next parallel region */
void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_NUM_THREADS)(int KMP_DEREF arg) {
#ifdef KMP_STUB
// Nothing.
#else
  __kmp_set_num_threads(KMP_DEREF arg, __kmp_entry_gtid());
#endif
}

/* returns the number of threads in current team */
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_THREADS)(void) {
#ifdef KMP_STUB
  return 1;
#else
  // __kmpc_bound_num_threads initializes the library if needed
  return __kmpc_bound_num_threads(NULL);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_MAX_THREADS)(void) {
#ifdef KMP_STUB
  return 1;
#else
  int gtid;
  kmp_info_t *thread;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  gtid = __kmp_entry_gtid();
  thread = __kmp_threads[gtid];
  // return thread -> th.th_team -> t.t_current_task[
  // thread->th.th_info.ds.ds_tid ] -> icvs.nproc;
  return thread->th.th_current_task->td_icvs.nproc;
#endif
}

#if OMP_50_ENABLED
int FTN_STDCALL FTN_CONTROL_TOOL(int command, int modifier, void *arg) {
#if defined(KMP_STUB) || !OMPT_SUPPORT
  return -2;
#else
  OMPT_STORE_RETURN_ADDRESS(__kmp_entry_gtid());
  if (!TCR_4(__kmp_init_middle)) {
    return -2;
  }
  kmp_info_t *this_thr = __kmp_threads[__kmp_entry_gtid()];
  ompt_task_info_t *parent_task_info = OMPT_CUR_TASK_INFO(this_thr);
  parent_task_info->frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  int ret = __kmp_control_tool(command, modifier, arg);
  parent_task_info->frame.enter_frame.ptr = 0;
  return ret;
#endif
}

/* OpenMP 5.0 Memory Management support */
void FTN_STDCALL FTN_SET_DEFAULT_ALLOCATOR(const omp_allocator_t *allocator) {
#ifndef KMP_STUB
  __kmpc_set_default_allocator(__kmp_entry_gtid(), allocator);
#endif
}
const omp_allocator_t *FTN_STDCALL FTN_GET_DEFAULT_ALLOCATOR(void) {
#ifdef KMP_STUB
  return NULL;
#else
  return __kmpc_get_default_allocator(__kmp_entry_gtid());
#endif
}
void *FTN_STDCALL FTN_ALLOC(size_t size, const omp_allocator_t *allocator) {
#ifdef KMP_STUB
  return malloc(size);
#else
  return __kmpc_alloc(__kmp_entry_gtid(), size, allocator);
#endif
}
void FTN_STDCALL FTN_FREE(void *ptr, const omp_allocator_t *allocator) {
#ifdef KMP_STUB
  free(ptr);
#else
  __kmpc_free(__kmp_entry_gtid(), ptr, allocator);
#endif
}

/* OpenMP 5.0 affinity format support */

#ifndef KMP_STUB
static void __kmp_fortran_strncpy_truncate(char *buffer, size_t buf_size,
                                           char const *csrc, size_t csrc_size) {
  size_t capped_src_size = csrc_size;
  if (csrc_size >= buf_size) {
    capped_src_size = buf_size - 1;
  }
  KMP_STRNCPY_S(buffer, buf_size, csrc, capped_src_size);
  if (csrc_size >= buf_size) {
    KMP_DEBUG_ASSERT(buffer[buf_size - 1] == '\0');
    buffer[buf_size - 1] = csrc[buf_size - 1];
  } else {
    for (size_t i = csrc_size; i < buf_size; ++i)
      buffer[i] = ' ';
  }
}

// Convert a Fortran string to a C string by adding null byte
class ConvertedString {
  char *buf;
  kmp_info_t *th;

public:
  ConvertedString(char const *fortran_str, size_t size) {
    th = __kmp_get_thread();
    buf = (char *)__kmp_thread_malloc(th, size + 1);
    KMP_STRNCPY_S(buf, size + 1, fortran_str, size);
    buf[size] = '\0';
  }
  ~ConvertedString() { __kmp_thread_free(th, buf); }
  const char *get() const { return buf; }
};
#endif // KMP_STUB

/*
 * Set the value of the affinity-format-var ICV on the current device to the
 * format specified in the argument.
*/
void FTN_STDCALL FTN_SET_AFFINITY_FORMAT(char const *format, size_t size) {
#ifdef KMP_STUB
  return;
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  ConvertedString cformat(format, size);
  // Since the __kmp_affinity_format variable is a C string, do not
  // use the fortran strncpy function
  __kmp_strncpy_truncate(__kmp_affinity_format, KMP_AFFINITY_FORMAT_SIZE,
                         cformat.get(), KMP_STRLEN(cformat.get()));
#endif
}

/*
 * Returns the number of characters required to hold the entire affinity format
 * specification (not including null byte character) and writes the value of the
 * affinity-format-var ICV on the current device to buffer. If the return value
 * is larger than size, the affinity format specification is truncated.
*/
size_t FTN_STDCALL FTN_GET_AFFINITY_FORMAT(char *buffer, size_t size) {
#ifdef KMP_STUB
  return 0;
#else
  size_t format_size;
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  format_size = KMP_STRLEN(__kmp_affinity_format);
  if (buffer && size) {
    __kmp_fortran_strncpy_truncate(buffer, size, __kmp_affinity_format,
                                   format_size);
  }
  return format_size;
#endif
}

/*
 * Prints the thread affinity information of the current thread in the format
 * specified by the format argument. If the format is NULL or a zero-length
 * string, the value of the affinity-format-var ICV is used.
*/
void FTN_STDCALL FTN_DISPLAY_AFFINITY(char const *format, size_t size) {
#ifdef KMP_STUB
  return;
#else
  int gtid;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  gtid = __kmp_get_gtid();
  ConvertedString cformat(format, size);
  __kmp_aux_display_affinity(gtid, cformat.get());
#endif
}

/*
 * Returns the number of characters required to hold the entire affinity format
 * specification (not including null byte) and prints the thread affinity
 * information of the current thread into the character string buffer with the
 * size of size in the format specified by the format argument. If the format is
 * NULL or a zero-length string, the value of the affinity-format-var ICV is
 * used. The buffer must be allocated prior to calling the routine. If the
 * return value is larger than size, the affinity format specification is
 * truncated.
*/
size_t FTN_STDCALL FTN_CAPTURE_AFFINITY(char *buffer, char const *format,
                                        size_t buf_size, size_t for_size) {
#if defined(KMP_STUB)
  return 0;
#else
  int gtid;
  size_t num_required;
  kmp_str_buf_t capture_buf;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  gtid = __kmp_get_gtid();
  __kmp_str_buf_init(&capture_buf);
  ConvertedString cformat(format, for_size);
  num_required = __kmp_aux_capture_affinity(gtid, cformat.get(), &capture_buf);
  if (buffer && buf_size) {
    __kmp_fortran_strncpy_truncate(buffer, buf_size, capture_buf.str,
                                   capture_buf.used);
  }
  __kmp_str_buf_free(&capture_buf);
  return num_required;
#endif
}
#endif /* OMP_50_ENABLED */

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_THREAD_NUM)(void) {
#ifdef KMP_STUB
  return 0;
#else
  int gtid;

#if KMP_OS_DARWIN || KMP_OS_DRAGONFLY || KMP_OS_FREEBSD || KMP_OS_NETBSD ||    \
        KMP_OS_HURD
  gtid = __kmp_entry_gtid();
#elif KMP_OS_WINDOWS
  if (!__kmp_init_parallel ||
      (gtid = (int)((kmp_intptr_t)TlsGetValue(__kmp_gtid_threadprivate_key))) ==
          0) {
    // Either library isn't initialized or thread is not registered
    // 0 is the correct TID in this case
    return 0;
  }
  --gtid; // We keep (gtid+1) in TLS
#elif KMP_OS_LINUX
#ifdef KMP_TDATA_GTID
  if (__kmp_gtid_mode >= 3) {
    if ((gtid = __kmp_gtid) == KMP_GTID_DNE) {
      return 0;
    }
  } else {
#endif
    if (!__kmp_init_parallel ||
        (gtid = (kmp_intptr_t)(
             pthread_getspecific(__kmp_gtid_threadprivate_key))) == 0) {
      return 0;
    }
    --gtid;
#ifdef KMP_TDATA_GTID
  }
#endif
#else
#error Unknown or unsupported OS
#endif

  return __kmp_tid_from_gtid(gtid);
#endif
}

int FTN_STDCALL FTN_GET_NUM_KNOWN_THREADS(void) {
#ifdef KMP_STUB
  return 1;
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  /* NOTE: this is not syncronized, so it can change at any moment */
  /* NOTE: this number also includes threads preallocated in hot-teams */
  return TCR_4(__kmp_nth);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_PROCS)(void) {
#ifdef KMP_STUB
  return 1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_avail_proc;
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_NESTED)(int KMP_DEREF flag) {
#ifdef KMP_STUB
  __kmps_set_nested(KMP_DEREF flag);
#else
  kmp_info_t *thread;
  /* For the thread-private internal controls implementation */
  thread = __kmp_entry_thread();
  __kmp_save_internal_controls(thread);
  set__nested(thread, ((KMP_DEREF flag) ? TRUE : FALSE));
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NESTED)(void) {
#ifdef KMP_STUB
  return __kmps_get_nested();
#else
  kmp_info_t *thread;
  thread = __kmp_entry_thread();
  return get__nested(thread);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_DYNAMIC)(int KMP_DEREF flag) {
#ifdef KMP_STUB
  __kmps_set_dynamic(KMP_DEREF flag ? TRUE : FALSE);
#else
  kmp_info_t *thread;
  /* For the thread-private implementation of the internal controls */
  thread = __kmp_entry_thread();
  // !!! What if foreign thread calls it?
  __kmp_save_internal_controls(thread);
  set__dynamic(thread, KMP_DEREF flag ? TRUE : FALSE);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_DYNAMIC)(void) {
#ifdef KMP_STUB
  return __kmps_get_dynamic();
#else
  kmp_info_t *thread;
  thread = __kmp_entry_thread();
  return get__dynamic(thread);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_IN_PARALLEL)(void) {
#ifdef KMP_STUB
  return 0;
#else
  kmp_info_t *th = __kmp_entry_thread();
#if OMP_40_ENABLED
  if (th->th.th_teams_microtask) {
    // AC: r_in_parallel does not work inside teams construct where real
    // parallel is inactive, but all threads have same root, so setting it in
    // one team affects other teams.
    // The solution is to use per-team nesting level
    return (th->th.th_team->t.t_active_level ? 1 : 0);
  } else
#endif /* OMP_40_ENABLED */
    return (th->th.th_root->r.r_in_parallel ? FTN_TRUE : FTN_FALSE);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_SCHEDULE)(kmp_sched_t KMP_DEREF kind,
                                                   int KMP_DEREF modifier) {
#ifdef KMP_STUB
  __kmps_set_schedule(KMP_DEREF kind, KMP_DEREF modifier);
#else
  /* TO DO: For the per-task implementation of the internal controls */
  __kmp_set_schedule(__kmp_entry_gtid(), KMP_DEREF kind, KMP_DEREF modifier);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_SCHEDULE)(kmp_sched_t *kind,
                                                   int *modifier) {
#ifdef KMP_STUB
  __kmps_get_schedule(kind, modifier);
#else
  /* TO DO: For the per-task implementation of the internal controls */
  __kmp_get_schedule(__kmp_entry_gtid(), kind, modifier);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_MAX_ACTIVE_LEVELS)(int KMP_DEREF arg) {
#ifdef KMP_STUB
// Nothing.
#else
  /* TO DO: We want per-task implementation of this internal control */
  __kmp_set_max_active_levels(__kmp_entry_gtid(), KMP_DEREF arg);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_MAX_ACTIVE_LEVELS)(void) {
#ifdef KMP_STUB
  return 0;
#else
  /* TO DO: We want per-task implementation of this internal control */
  return __kmp_get_max_active_levels(__kmp_entry_gtid());
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_ACTIVE_LEVEL)(void) {
#ifdef KMP_STUB
  return 0; // returns 0 if it is called from the sequential part of the program
#else
  /* TO DO: For the per-task implementation of the internal controls */
  return __kmp_entry_thread()->th.th_team->t.t_active_level;
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_LEVEL)(void) {
#ifdef KMP_STUB
  return 0; // returns 0 if it is called from the sequential part of the program
#else
  /* TO DO: For the per-task implementation of the internal controls */
  return __kmp_entry_thread()->th.th_team->t.t_level;
#endif
}

int FTN_STDCALL
    KMP_EXPAND_NAME(FTN_GET_ANCESTOR_THREAD_NUM)(int KMP_DEREF level) {
#ifdef KMP_STUB
  return (KMP_DEREF level) ? (-1) : (0);
#else
  return __kmp_get_ancestor_thread_num(__kmp_entry_gtid(), KMP_DEREF level);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_TEAM_SIZE)(int KMP_DEREF level) {
#ifdef KMP_STUB
  return (KMP_DEREF level) ? (-1) : (1);
#else
  return __kmp_get_team_size(__kmp_entry_gtid(), KMP_DEREF level);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_THREAD_LIMIT)(void) {
#ifdef KMP_STUB
  return 1; // TO DO: clarify whether it returns 1 or 0?
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  /* global ICV */
  return __kmp_cg_max_nth;
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_IN_FINAL)(void) {
#ifdef KMP_STUB
  return 0; // TO DO: clarify whether it returns 1 or 0?
#else
  if (!TCR_4(__kmp_init_parallel)) {
    return 0;
  }
  return __kmp_entry_thread()->th.th_current_task->td_flags.final;
#endif
}

#if OMP_40_ENABLED

kmp_proc_bind_t FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PROC_BIND)(void) {
#ifdef KMP_STUB
  return __kmps_get_proc_bind();
#else
  return get__proc_bind(__kmp_entry_thread());
#endif
}

#if OMP_45_ENABLED
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_PLACES)(void) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return 0;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return 0;
  return __kmp_affinity_num_masks;
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PLACE_NUM_PROCS)(int place_num) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return 0;
#else
  int i;
  int retval = 0;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return 0;
  if (place_num < 0 || place_num >= (int)__kmp_affinity_num_masks)
    return 0;
  kmp_affin_mask_t *mask = KMP_CPU_INDEX(__kmp_affinity_masks, place_num);
  KMP_CPU_SET_ITERATE(i, mask) {
    if ((!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) ||
        (!KMP_CPU_ISSET(i, mask))) {
      continue;
    }
    ++retval;
  }
  return retval;
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PLACE_PROC_IDS)(int place_num,
                                                         int *ids) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
// Nothing.
#else
  int i, j;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return;
  if (place_num < 0 || place_num >= (int)__kmp_affinity_num_masks)
    return;
  kmp_affin_mask_t *mask = KMP_CPU_INDEX(__kmp_affinity_masks, place_num);
  j = 0;
  KMP_CPU_SET_ITERATE(i, mask) {
    if ((!KMP_CPU_ISSET(i, __kmp_affin_fullMask)) ||
        (!KMP_CPU_ISSET(i, mask))) {
      continue;
    }
    ids[j++] = i;
  }
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PLACE_NUM)(void) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  int gtid;
  kmp_info_t *thread;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return -1;
  gtid = __kmp_entry_gtid();
  thread = __kmp_thread_from_gtid(gtid);
  if (thread->th.th_current_place < 0)
    return -1;
  return thread->th.th_current_place;
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PARTITION_NUM_PLACES)(void) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return 0;
#else
  int gtid, num_places, first_place, last_place;
  kmp_info_t *thread;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return 0;
  gtid = __kmp_entry_gtid();
  thread = __kmp_thread_from_gtid(gtid);
  first_place = thread->th.th_first_place;
  last_place = thread->th.th_last_place;
  if (first_place < 0 || last_place < 0)
    return 0;
  if (first_place <= last_place)
    num_places = last_place - first_place + 1;
  else
    num_places = __kmp_affinity_num_masks - first_place + last_place + 1;
  return num_places;
#endif
}

void
    FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_PARTITION_PLACE_NUMS)(int *place_nums) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
// Nothing.
#else
  int i, gtid, place_num, first_place, last_place, start, end;
  kmp_info_t *thread;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  if (!KMP_AFFINITY_CAPABLE())
    return;
  gtid = __kmp_entry_gtid();
  thread = __kmp_thread_from_gtid(gtid);
  first_place = thread->th.th_first_place;
  last_place = thread->th.th_last_place;
  if (first_place < 0 || last_place < 0)
    return;
  if (first_place <= last_place) {
    start = first_place;
    end = last_place;
  } else {
    start = last_place;
    end = first_place;
  }
  for (i = 0, place_num = start; place_num <= end; ++place_num, ++i) {
    place_nums[i] = place_num;
  }
#endif
}
#endif

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_TEAMS)(void) {
#ifdef KMP_STUB
  return 1;
#else
  return __kmp_aux_get_num_teams();
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_TEAM_NUM)(void) {
#ifdef KMP_STUB
  return 0;
#else
  return __kmp_aux_get_team_num();
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_DEFAULT_DEVICE)(void) {
#if KMP_MIC || KMP_OS_DARWIN || defined(KMP_STUB)
  return 0;
#else
  return __kmp_entry_thread()->th.th_current_task->td_icvs.default_device;
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_DEFAULT_DEVICE)(int KMP_DEREF arg) {
#if KMP_MIC || KMP_OS_DARWIN || defined(KMP_STUB)
// Nothing.
#else
  __kmp_entry_thread()->th.th_current_task->td_icvs.default_device =
      KMP_DEREF arg;
#endif
}

// Get number of NON-HOST devices.
// libomptarget, if loaded, provides this function in api.cpp.
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_DEVICES)(void) KMP_WEAK_ATTRIBUTE;
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_NUM_DEVICES)(void) {
#if KMP_MIC || KMP_OS_DARWIN || KMP_OS_WINDOWS || defined(KMP_STUB)
  return 0;
#else
  int (*fptr)();
  if ((*(void **)(&fptr) = dlsym(RTLD_DEFAULT, "_Offload_number_of_devices"))) {
    return (*fptr)();
  } else if ((*(void **)(&fptr) = dlsym(RTLD_NEXT, "omp_get_num_devices"))) {
    return (*fptr)();
  } else { // liboffload & libomptarget don't exist
    return 0;
  }
#endif // KMP_MIC || KMP_OS_DARWIN || KMP_OS_WINDOWS || defined(KMP_STUB)
}

// This function always returns true when called on host device.
// Compilier/libomptarget should handle when it is called inside target region.
int FTN_STDCALL KMP_EXPAND_NAME(FTN_IS_INITIAL_DEVICE)(void) KMP_WEAK_ATTRIBUTE;
int FTN_STDCALL KMP_EXPAND_NAME(FTN_IS_INITIAL_DEVICE)(void) {
  return 1; // This is the host
}

#endif // OMP_40_ENABLED

#if OMP_45_ENABLED
// OpenMP 4.5 entries

// libomptarget, if loaded, provides this function
int FTN_STDCALL FTN_GET_INITIAL_DEVICE(void) KMP_WEAK_ATTRIBUTE;
int FTN_STDCALL FTN_GET_INITIAL_DEVICE(void) {
#if KMP_MIC || KMP_OS_DARWIN || KMP_OS_WINDOWS || defined(KMP_STUB)
  return KMP_HOST_DEVICE;
#else
  int (*fptr)();
  if ((*(void **)(&fptr) = dlsym(RTLD_NEXT, "omp_get_initial_device"))) {
    return (*fptr)();
  } else { // liboffload & libomptarget don't exist
    return KMP_HOST_DEVICE;
  }
#endif
}

#if defined(KMP_STUB)
// Entries for stubs library
// As all *target* functions are C-only parameters always passed by value
void *FTN_STDCALL FTN_TARGET_ALLOC(size_t size, int device_num) { return 0; }

void FTN_STDCALL FTN_TARGET_FREE(void *device_ptr, int device_num) {}

int FTN_STDCALL FTN_TARGET_IS_PRESENT(void *ptr, int device_num) { return 0; }

int FTN_STDCALL FTN_TARGET_MEMCPY(void *dst, void *src, size_t length,
                                  size_t dst_offset, size_t src_offset,
                                  int dst_device, int src_device) {
  return -1;
}

int FTN_STDCALL FTN_TARGET_MEMCPY_RECT(
    void *dst, void *src, size_t element_size, int num_dims,
    const size_t *volume, const size_t *dst_offsets, const size_t *src_offsets,
    const size_t *dst_dimensions, const size_t *src_dimensions, int dst_device,
    int src_device) {
  return -1;
}

int FTN_STDCALL FTN_TARGET_ASSOCIATE_PTR(void *host_ptr, void *device_ptr,
                                         size_t size, size_t device_offset,
                                         int device_num) {
  return -1;
}

int FTN_STDCALL FTN_TARGET_DISASSOCIATE_PTR(void *host_ptr, int device_num) {
  return -1;
}
#endif // defined(KMP_STUB)
#endif // OMP_45_ENABLED

#ifdef KMP_STUB
typedef enum { UNINIT = -1, UNLOCKED, LOCKED } kmp_stub_lock_t;
#endif /* KMP_STUB */

#if KMP_USE_DYNAMIC_LOCK
void FTN_STDCALL FTN_INIT_LOCK_WITH_HINT(void **user_lock,
                                         uintptr_t KMP_DEREF hint) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNLOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_init_lock_with_hint(NULL, gtid, user_lock, KMP_DEREF hint);
#endif
}

void FTN_STDCALL FTN_INIT_NEST_LOCK_WITH_HINT(void **user_lock,
                                              uintptr_t KMP_DEREF hint) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNLOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_init_nest_lock_with_hint(NULL, gtid, user_lock, KMP_DEREF hint);
#endif
}
#endif

/* initialize the lock */
void FTN_STDCALL KMP_EXPAND_NAME(FTN_INIT_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNLOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_init_lock(NULL, gtid, user_lock);
#endif
}

/* initialize the lock */
void FTN_STDCALL KMP_EXPAND_NAME(FTN_INIT_NEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNLOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_init_nest_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_DESTROY_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNINIT;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_destroy_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_DESTROY_NEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  *((kmp_stub_lock_t *)user_lock) = UNINIT;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_destroy_nest_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  if (*((kmp_stub_lock_t *)user_lock) != UNLOCKED) {
    // TODO: Issue an error.
  }
  *((kmp_stub_lock_t *)user_lock) = LOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_set_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_SET_NEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  (*((int *)user_lock))++;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_set_nest_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_UNSET_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  if (*((kmp_stub_lock_t *)user_lock) == UNLOCKED) {
    // TODO: Issue an error.
  }
  *((kmp_stub_lock_t *)user_lock) = UNLOCKED;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_unset_lock(NULL, gtid, user_lock);
#endif
}

void FTN_STDCALL KMP_EXPAND_NAME(FTN_UNSET_NEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  if (*((kmp_stub_lock_t *)user_lock) == UNLOCKED) {
    // TODO: Issue an error.
  }
  (*((int *)user_lock))--;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_unset_nest_lock(NULL, gtid, user_lock);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_TEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  if (*((kmp_stub_lock_t *)user_lock) == LOCKED) {
    return 0;
  }
  *((kmp_stub_lock_t *)user_lock) = LOCKED;
  return 1;
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmpc_test_lock(NULL, gtid, user_lock);
#endif
}

int FTN_STDCALL KMP_EXPAND_NAME(FTN_TEST_NEST_LOCK)(void **user_lock) {
#ifdef KMP_STUB
  if (*((kmp_stub_lock_t *)user_lock) == UNINIT) {
    // TODO: Issue an error.
  }
  return ++(*((int *)user_lock));
#else
  int gtid = __kmp_entry_gtid();
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmpc_test_nest_lock(NULL, gtid, user_lock);
#endif
}

double FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_WTIME)(void) {
#ifdef KMP_STUB
  return __kmps_get_wtime();
#else
  double data;
#if !KMP_OS_LINUX
  // We don't need library initialization to get the time on Linux* OS. The
  // routine can be used to measure library initialization time on Linux* OS now
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
#endif
  __kmp_elapsed(&data);
  return data;
#endif
}

double FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_WTICK)(void) {
#ifdef KMP_STUB
  return __kmps_get_wtick();
#else
  double data;
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  __kmp_elapsed_tick(&data);
  return data;
#endif
}

/* ------------------------------------------------------------------------ */

void *FTN_STDCALL FTN_MALLOC(size_t KMP_DEREF size) {
  // kmpc_malloc initializes the library if needed
  return kmpc_malloc(KMP_DEREF size);
}

void *FTN_STDCALL FTN_ALIGNED_MALLOC(size_t KMP_DEREF size,
                                     size_t KMP_DEREF alignment) {
  // kmpc_aligned_malloc initializes the library if needed
  return kmpc_aligned_malloc(KMP_DEREF size, KMP_DEREF alignment);
}

void *FTN_STDCALL FTN_CALLOC(size_t KMP_DEREF nelem, size_t KMP_DEREF elsize) {
  // kmpc_calloc initializes the library if needed
  return kmpc_calloc(KMP_DEREF nelem, KMP_DEREF elsize);
}

void *FTN_STDCALL FTN_REALLOC(void *KMP_DEREF ptr, size_t KMP_DEREF size) {
  // kmpc_realloc initializes the library if needed
  return kmpc_realloc(KMP_DEREF ptr, KMP_DEREF size);
}

void FTN_STDCALL FTN_KFREE(void *KMP_DEREF ptr) {
  // does nothing if the library is not initialized
  kmpc_free(KMP_DEREF ptr);
}

void FTN_STDCALL FTN_SET_WARNINGS_ON(void) {
#ifndef KMP_STUB
  __kmp_generate_warnings = kmp_warnings_explicit;
#endif
}

void FTN_STDCALL FTN_SET_WARNINGS_OFF(void) {
#ifndef KMP_STUB
  __kmp_generate_warnings = FALSE;
#endif
}

void FTN_STDCALL FTN_SET_DEFAULTS(char const *str
#ifndef PASS_ARGS_BY_VALUE
                                  ,
                                  int len
#endif
                                  ) {
#ifndef KMP_STUB
#ifdef PASS_ARGS_BY_VALUE
  int len = (int)KMP_STRLEN(str);
#endif
  __kmp_aux_set_defaults(str, len);
#endif
}

/* ------------------------------------------------------------------------ */

#if OMP_40_ENABLED
/* returns the status of cancellation */
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_CANCELLATION)(void) {
#ifdef KMP_STUB
  return 0 /* false */;
#else
  // initialize the library if needed
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return __kmp_omp_cancellation;
#endif
}

int FTN_STDCALL FTN_GET_CANCELLATION_STATUS(int cancel_kind) {
#ifdef KMP_STUB
  return 0 /* false */;
#else
  return __kmp_get_cancellation_status(cancel_kind);
#endif
}

#endif // OMP_40_ENABLED

#if OMP_45_ENABLED
/* returns the maximum allowed task priority */
int FTN_STDCALL KMP_EXPAND_NAME(FTN_GET_MAX_TASK_PRIORITY)(void) {
#ifdef KMP_STUB
  return 0;
#else
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return __kmp_max_task_priority;
#endif
}
#endif

#if OMP_50_ENABLED
// This function will be defined in libomptarget. When libomptarget is not
// loaded, we assume we are on the host and return KMP_HOST_DEVICE.
// Compiler/libomptarget will handle this if called inside target.
int FTN_STDCALL FTN_GET_DEVICE_NUM(void) KMP_WEAK_ATTRIBUTE;
int FTN_STDCALL FTN_GET_DEVICE_NUM(void) { return KMP_HOST_DEVICE; }
#endif // OMP_50_ENABLED

// GCC compatibility (versioned symbols)
#ifdef KMP_USE_VERSION_SYMBOLS

/* These following sections create versioned symbols for the
   omp_* routines. The KMP_VERSION_SYMBOL macro expands the API name and
   then maps it to a versioned symbol.
   libgomp ``versions'' its symbols (OMP_1.0, OMP_2.0, OMP_3.0, ...) while also
   retaining the default version which libomp uses: VERSION (defined in
   exports_so.txt). If you want to see the versioned symbols for libgomp.so.1
   then just type:

   objdump -T /path/to/libgomp.so.1 | grep omp_

   Example:
   Step 1) Create __kmp_api_omp_set_num_threads_10_alias which is alias of
     __kmp_api_omp_set_num_threads
   Step 2) Set __kmp_api_omp_set_num_threads_10_alias to version:
     omp_set_num_threads@OMP_1.0
   Step 2B) Set __kmp_api_omp_set_num_threads to default version:
     omp_set_num_threads@@VERSION
*/

// OMP_1.0 versioned symbols
KMP_VERSION_SYMBOL(FTN_SET_NUM_THREADS, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_NUM_THREADS, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_MAX_THREADS, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_THREAD_NUM, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_NUM_PROCS, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_IN_PARALLEL, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_SET_DYNAMIC, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_DYNAMIC, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_SET_NESTED, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_GET_NESTED, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_INIT_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_INIT_NEST_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_DESTROY_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_DESTROY_NEST_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_SET_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_SET_NEST_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_UNSET_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_UNSET_NEST_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_TEST_LOCK, 10, "OMP_1.0");
KMP_VERSION_SYMBOL(FTN_TEST_NEST_LOCK, 10, "OMP_1.0");

// OMP_2.0 versioned symbols
KMP_VERSION_SYMBOL(FTN_GET_WTICK, 20, "OMP_2.0");
KMP_VERSION_SYMBOL(FTN_GET_WTIME, 20, "OMP_2.0");

// OMP_3.0 versioned symbols
KMP_VERSION_SYMBOL(FTN_SET_SCHEDULE, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_SCHEDULE, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_THREAD_LIMIT, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_SET_MAX_ACTIVE_LEVELS, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_MAX_ACTIVE_LEVELS, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_ANCESTOR_THREAD_NUM, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_LEVEL, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_TEAM_SIZE, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_GET_ACTIVE_LEVEL, 30, "OMP_3.0");

// the lock routines have a 1.0 and 3.0 version
KMP_VERSION_SYMBOL(FTN_INIT_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_INIT_NEST_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_DESTROY_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_DESTROY_NEST_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_SET_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_SET_NEST_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_UNSET_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_UNSET_NEST_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_TEST_LOCK, 30, "OMP_3.0");
KMP_VERSION_SYMBOL(FTN_TEST_NEST_LOCK, 30, "OMP_3.0");

// OMP_3.1 versioned symbol
KMP_VERSION_SYMBOL(FTN_IN_FINAL, 31, "OMP_3.1");

#if OMP_40_ENABLED
// OMP_4.0 versioned symbols
KMP_VERSION_SYMBOL(FTN_GET_PROC_BIND, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_GET_NUM_TEAMS, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_GET_TEAM_NUM, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_GET_CANCELLATION, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_GET_DEFAULT_DEVICE, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_SET_DEFAULT_DEVICE, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_IS_INITIAL_DEVICE, 40, "OMP_4.0");
KMP_VERSION_SYMBOL(FTN_GET_NUM_DEVICES, 40, "OMP_4.0");
#endif /* OMP_40_ENABLED */

#if OMP_45_ENABLED
// OMP_4.5 versioned symbols
KMP_VERSION_SYMBOL(FTN_GET_MAX_TASK_PRIORITY, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_NUM_PLACES, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_PLACE_NUM_PROCS, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_PLACE_PROC_IDS, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_PLACE_NUM, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_PARTITION_NUM_PLACES, 45, "OMP_4.5");
KMP_VERSION_SYMBOL(FTN_GET_PARTITION_PLACE_NUMS, 45, "OMP_4.5");
// KMP_VERSION_SYMBOL(FTN_GET_INITIAL_DEVICE, 45, "OMP_4.5");
#endif

#if OMP_50_ENABLED
// OMP_5.0 versioned symbols
// KMP_VERSION_SYMBOL(FTN_GET_DEVICE_NUM, 50, "OMP_5.0");
#endif

#endif // KMP_USE_VERSION_SYMBOLS

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

// end of file //
