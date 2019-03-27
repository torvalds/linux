/*
 * kmp_gsupport.cpp
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_atomic.h"

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define MKLOC(loc, routine)                                                    \
  static ident_t(loc) = {0, KMP_IDENT_KMPC, 0, 0, ";unknown;unknown;0;0;;"};

#include "kmp_ftn_os.h"

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_BARRIER)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_barrier");
  KA_TRACE(20, ("GOMP_barrier: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmpc_barrier(&loc, gtid);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif
}

// Mutual exclusion

// The symbol that icc/ifort generates for unnamed for unnamed critical sections
// - .gomp_critical_user_ - is defined using .comm in any objects reference it.
// We can't reference it directly here in C code, as the symbol contains a ".".
//
// The RTL contains an assembly language definition of .gomp_critical_user_
// with another symbol __kmp_unnamed_critical_addr initialized with it's
// address.
extern kmp_critical_name *__kmp_unnamed_critical_addr;

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CRITICAL_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_critical_start");
  KA_TRACE(20, ("GOMP_critical_start: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_critical(&loc, gtid, __kmp_unnamed_critical_addr);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CRITICAL_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_critical_end");
  KA_TRACE(20, ("GOMP_critical_end: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_end_critical(&loc, gtid, __kmp_unnamed_critical_addr);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CRITICAL_NAME_START)(void **pptr) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_critical_name_start");
  KA_TRACE(20, ("GOMP_critical_name_start: T#%d\n", gtid));
  __kmpc_critical(&loc, gtid, (kmp_critical_name *)pptr);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CRITICAL_NAME_END)(void **pptr) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_critical_name_end");
  KA_TRACE(20, ("GOMP_critical_name_end: T#%d\n", gtid));
  __kmpc_end_critical(&loc, gtid, (kmp_critical_name *)pptr);
}

// The Gnu codegen tries to use locked operations to perform atomic updates
// inline.  If it can't, then it calls GOMP_atomic_start() before performing
// the update and GOMP_atomic_end() afterward, regardless of the data type.
void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_ATOMIC_START)(void) {
  int gtid = __kmp_entry_gtid();
  KA_TRACE(20, ("GOMP_atomic_start: T#%d\n", gtid));

#if OMPT_SUPPORT
  __ompt_thread_assign_wait_id(0);
#endif

  __kmp_acquire_atomic_lock(&__kmp_atomic_lock, gtid);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_ATOMIC_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_atomic_end: T#%d\n", gtid));
  __kmp_release_atomic_lock(&__kmp_atomic_lock, gtid);
}

int KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SINGLE_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_single_start");
  KA_TRACE(20, ("GOMP_single_start: T#%d\n", gtid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  // 3rd parameter == FALSE prevents kmp_enter_single from pushing a
  // workshare when USE_CHECKS is defined.  We need to avoid the push,
  // as there is no corresponding GOMP_single_end() call.
  kmp_int32 rc = __kmp_enter_single(gtid, &loc, FALSE);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = this_thr->th.th_team;
  int tid = __kmp_tid_from_gtid(gtid);

  if (ompt_enabled.enabled) {
    if (rc) {
      if (ompt_enabled.ompt_callback_work) {
        ompt_callbacks.ompt_callback(ompt_callback_work)(
            ompt_work_single_executor, ompt_scope_begin,
            &(team->t.ompt_team_info.parallel_data),
            &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data),
            1, OMPT_GET_RETURN_ADDRESS(0));
      }
    } else {
      if (ompt_enabled.ompt_callback_work) {
        ompt_callbacks.ompt_callback(ompt_callback_work)(
            ompt_work_single_other, ompt_scope_begin,
            &(team->t.ompt_team_info.parallel_data),
            &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data),
            1, OMPT_GET_RETURN_ADDRESS(0));
        ompt_callbacks.ompt_callback(ompt_callback_work)(
            ompt_work_single_other, ompt_scope_end,
            &(team->t.ompt_team_info.parallel_data),
            &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data),
            1, OMPT_GET_RETURN_ADDRESS(0));
      }
    }
  }
#endif

  return rc;
}

void *KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SINGLE_COPY_START)(void) {
  void *retval;
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_single_copy_start");
  KA_TRACE(20, ("GOMP_single_copy_start: T#%d\n", gtid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  // If this is the first thread to enter, return NULL.  The generated code will
  // then call GOMP_single_copy_end() for this thread only, with the
  // copyprivate data pointer as an argument.
  if (__kmp_enter_single(gtid, &loc, FALSE))
    return NULL;

// Wait for the first thread to set the copyprivate data pointer,
// and for all other threads to reach this point.

#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  // Retrieve the value of the copyprivate data point, and wait for all
  // threads to do likewise, then return.
  retval = __kmp_team_from_gtid(gtid)->t.t_copypriv_data;
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif
  return retval;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SINGLE_COPY_END)(void *data) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_single_copy_end: T#%d\n", gtid));

  // Set the copyprivate data pointer fo the team, then hit the barrier so that
  // the other threads will continue on and read it.  Hit another barrier before
  // continuing, so that the know that the copyprivate data pointer has been
  // propagated to all threads before trying to reuse the t_copypriv_data field.
  __kmp_team_from_gtid(gtid)->t.t_copypriv_data = data;
#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_ORDERED_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_ordered_start");
  KA_TRACE(20, ("GOMP_ordered_start: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_ordered(&loc, gtid);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_ORDERED_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_ordered_end");
  KA_TRACE(20, ("GOMP_ordered_start: T#%d\n", gtid));
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmpc_end_ordered(&loc, gtid);
}

// Dispatch macro defs
//
// They come in two flavors: 64-bit unsigned, and either 32-bit signed
// (IA-32 architecture) or 64-bit signed (Intel(R) 64).

#if KMP_ARCH_X86 || KMP_ARCH_ARM || KMP_ARCH_MIPS
#define KMP_DISPATCH_INIT __kmp_aux_dispatch_init_4
#define KMP_DISPATCH_FINI_CHUNK __kmp_aux_dispatch_fini_chunk_4
#define KMP_DISPATCH_NEXT __kmpc_dispatch_next_4
#else
#define KMP_DISPATCH_INIT __kmp_aux_dispatch_init_8
#define KMP_DISPATCH_FINI_CHUNK __kmp_aux_dispatch_fini_chunk_8
#define KMP_DISPATCH_NEXT __kmpc_dispatch_next_8
#endif /* KMP_ARCH_X86 */

#define KMP_DISPATCH_INIT_ULL __kmp_aux_dispatch_init_8u
#define KMP_DISPATCH_FINI_CHUNK_ULL __kmp_aux_dispatch_fini_chunk_8u
#define KMP_DISPATCH_NEXT_ULL __kmpc_dispatch_next_8u

// The parallel contruct

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_microtask_wrapper(int *gtid, int *npr, void (*task)(void *),
                                 void *data) {
#if OMPT_SUPPORT
  kmp_info_t *thr;
  ompt_frame_t *ompt_frame;
  ompt_state_t enclosing_state;

  if (ompt_enabled.enabled) {
    // get pointer to thread data structure
    thr = __kmp_threads[*gtid];

    // save enclosing task state; set current state for task
    enclosing_state = thr->th.ompt_thread_info.state;
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;

    // set task frame
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif

  task(data);

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    // clear task frame
    ompt_frame->exit_frame = ompt_data_none;

    // restore enclosing state
    thr->th.ompt_thread_info.state = enclosing_state;
  }
#endif
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_parallel_microtask_wrapper(int *gtid, int *npr,
                                          void (*task)(void *), void *data,
                                          unsigned num_threads, ident_t *loc,
                                          enum sched_type schedule, long start,
                                          long end, long incr,
                                          long chunk_size) {
  // Intialize the loop worksharing construct.

  KMP_DISPATCH_INIT(loc, *gtid, schedule, start, end, incr, chunk_size,
                    schedule != kmp_sch_static);

#if OMPT_SUPPORT
  kmp_info_t *thr;
  ompt_frame_t *ompt_frame;
  ompt_state_t enclosing_state;

  if (ompt_enabled.enabled) {
    thr = __kmp_threads[*gtid];
    // save enclosing task state; set current state for task
    enclosing_state = thr->th.ompt_thread_info.state;
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;

    // set task frame
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif

  // Now invoke the microtask.
  task(data);

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    // clear task frame
    ompt_frame->exit_frame = ompt_data_none;

    // reset enclosing state
    thr->th.ompt_thread_info.state = enclosing_state;
  }
#endif
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    void
    __kmp_GOMP_fork_call(ident_t *loc, int gtid, void (*unwrapped_task)(void *),
                         microtask_t wrapper, int argc, ...) {
  int rc;
  kmp_info_t *thr = __kmp_threads[gtid];
  kmp_team_t *team = thr->th.th_team;
  int tid = __kmp_tid_from_gtid(gtid);

  va_list ap;
  va_start(ap, argc);

  rc = __kmp_fork_call(loc, gtid, fork_context_gnu, argc, wrapper,
                       __kmp_invoke_task_func,
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
                       &ap
#else
                       ap
#endif
                       );

  va_end(ap);

  if (rc) {
    __kmp_run_before_invoked_task(gtid, tid, thr, team);
  }

#if OMPT_SUPPORT
  int ompt_team_size;
  if (ompt_enabled.enabled) {
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    ompt_task_info_t *task_info = __ompt_get_task_info_object(0);

    // implicit task callback
    if (ompt_enabled.ompt_callback_implicit_task) {
      ompt_team_size = __kmp_team_from_gtid(gtid)->t.t_nproc;
      ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
          ompt_scope_begin, &(team_info->parallel_data),
          &(task_info->task_data), ompt_team_size, __kmp_tid_from_gtid(gtid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
      task_info->thread_num = __kmp_tid_from_gtid(gtid);
    }
    thr->th.ompt_thread_info.state = ompt_state_work_parallel;
  }
#endif
}

static void __kmp_GOMP_serialized_parallel(ident_t *loc, kmp_int32 gtid,
                                           void (*task)(void *)) {
#if OMPT_SUPPORT
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_serialized_parallel(loc, gtid);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_START)(void (*task)(void *),
                                                       void *data,
                                                       unsigned num_threads) {
  int gtid = __kmp_entry_gtid();

#if OMPT_SUPPORT
  ompt_frame_t *parent_frame, *frame;

  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &parent_frame, NULL, NULL);
    parent_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif

  MKLOC(loc, "GOMP_parallel_start");
  KA_TRACE(20, ("GOMP_parallel_start: T#%d\n", gtid));

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_microtask_wrapper, 2, task,
                         data);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &frame, NULL, NULL);
    frame->exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_END)(void) {
  int gtid = __kmp_get_gtid();
  kmp_info_t *thr;

  thr = __kmp_threads[gtid];

  MKLOC(loc, "GOMP_parallel_end");
  KA_TRACE(20, ("GOMP_parallel_end: T#%d\n", gtid));

  if (!thr->th.th_team->t.t_serialized) {
    __kmp_run_after_invoked_task(gtid, __kmp_tid_from_gtid(gtid), thr,
                                 thr->th.th_team);

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      // Implicit task is finished here, in the barrier we might schedule
      // deferred tasks,
      // these don't see the implicit task on the stack
      OMPT_CUR_TASK_INFO(thr)->frame.exit_frame = ompt_data_none;
    }
#endif

    __kmp_join_call(&loc, gtid
#if OMPT_SUPPORT
                    ,
                    fork_context_gnu
#endif
                    );
  } else {
    __kmpc_end_serialized_parallel(&loc, gtid);
  }
}

// Loop worksharing constructs

// The Gnu codegen passes in an exclusive upper bound for the overall range,
// but the libguide dispatch code expects an inclusive upper bound, hence the
// "end - incr" 5th argument to KMP_DISPATCH_INIT (and the " ub - str" 11th
// argument to __kmp_GOMP_fork_call).
//
// Conversely, KMP_DISPATCH_NEXT returns and inclusive upper bound in *p_ub,
// but the Gnu codegen expects an excluside upper bound, so the adjustment
// "*p_ub += stride" compenstates for the discrepancy.
//
// Correction: the gnu codegen always adjusts the upper bound by +-1, not the
// stride value.  We adjust the dispatch parameters accordingly (by +-1), but
// we still adjust p_ub by the actual stride value.
//
// The "runtime" versions do not take a chunk_sz parameter.
//
// The profile lib cannot support construct checking of unordered loops that
// are predetermined by the compiler to be statically scheduled, as the gcc
// codegen will not always emit calls to GOMP_loop_static_next() to get the
// next iteration.  Instead, it emits inline code to call omp_get_thread_num()
// num and calculate the iteration space using the result.  It doesn't do this
// with ordered static loop, so they can be checked.

#if OMPT_SUPPORT
#define IF_OMPT_SUPPORT(code) code
#else
#define IF_OMPT_SUPPORT(code)
#endif

#define LOOP_START(func, schedule)                                             \
  int func(long lb, long ub, long str, long chunk_sz, long *p_lb,              \
           long *p_ub) {                                                       \
    int status;                                                                \
    long stride;                                                               \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n",  \
         gtid, lb, ub, str, chunk_sz));                                        \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                        \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,        \
                        (schedule) != kmp_sch_static);                         \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                        \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",    \
         gtid, *p_lb, *p_ub, status));                                         \
    return status;                                                             \
  }

#define LOOP_RUNTIME_START(func, schedule)                                     \
  int func(long lb, long ub, long str, long *p_lb, long *p_ub) {               \
    int status;                                                                \
    long stride;                                                               \
    long chunk_sz = 0;                                                         \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(func) ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz %d\n", \
         gtid, lb, ub, str, chunk_sz));                                        \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                        \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz, TRUE); \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                        \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",    \
         gtid, *p_lb, *p_ub, status));                                         \
    return status;                                                             \
  }

#if OMP_45_ENABLED
#define KMP_DOACROSS_FINI(status, gtid)                                        \
  if (!status && __kmp_threads[gtid]->th.th_dispatch->th_doacross_flags) {     \
    __kmpc_doacross_fini(NULL, gtid);                                          \
  }
#else
#define KMP_DOACROSS_FINI(status, gtid) /* Nothing */
#endif

#define LOOP_NEXT(func, fini_code)                                             \
  int func(long *p_lb, long *p_ub) {                                           \
    int status;                                                                \
    long stride;                                                               \
    int gtid = __kmp_get_gtid();                                               \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(20, (KMP_STR(func) ": T#%d\n", gtid));                            \
                                                                               \
    IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                          \
    fini_code status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,    \
                                         (kmp_int *)p_ub, (kmp_int *)&stride); \
    if (status) {                                                              \
      *p_ub += (stride > 0) ? 1 : -1;                                          \
    }                                                                          \
    KMP_DOACROSS_FINI(status, gtid)                                            \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(func) " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, stride 0x%lx, " \
                       "returning %d\n",                                       \
         gtid, *p_lb, *p_ub, stride, status));                                 \
    return status;                                                             \
  }

LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_STATIC_START), kmp_sch_static)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_STATIC_NEXT), {})
LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DYNAMIC_START),
           kmp_sch_dynamic_chunked)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT), {})
LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_GUIDED_START),
           kmp_sch_guided_chunked)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT), {})
LOOP_RUNTIME_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_RUNTIME_START),
                   kmp_sch_runtime)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT), {})

LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START),
           kmp_ord_static)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START),
           kmp_ord_dynamic_chunked)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_START(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START),
           kmp_ord_guided_chunked)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })
LOOP_RUNTIME_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START),
    kmp_ord_runtime)
LOOP_NEXT(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT),
          { KMP_DISPATCH_FINI_CHUNK(&loc, gtid); })

#if OMP_45_ENABLED
#define LOOP_DOACROSS_START(func, schedule)                                    \
  bool func(unsigned ncounts, long *counts, long chunk_sz, long *p_lb,         \
            long *p_ub) {                                                      \
    int status;                                                                \
    long stride, lb, ub, str;                                                  \
    int gtid = __kmp_entry_gtid();                                             \
    struct kmp_dim *dims =                                                     \
        (struct kmp_dim *)__kmp_allocate(sizeof(struct kmp_dim) * ncounts);    \
    MKLOC(loc, KMP_STR(func));                                                 \
    for (unsigned i = 0; i < ncounts; ++i) {                                   \
      dims[i].lo = 0;                                                          \
      dims[i].up = counts[i] - 1;                                              \
      dims[i].st = 1;                                                          \
    }                                                                          \
    __kmpc_doacross_init(&loc, gtid, (int)ncounts, dims);                      \
    lb = 0;                                                                    \
    ub = counts[0];                                                            \
    str = 1;                                                                   \
    KA_TRACE(20, (KMP_STR(func) ": T#%d, ncounts %u, lb 0x%lx, ub 0x%lx, str " \
                                "0x%lx, chunk_sz "                             \
                                "0x%lx\n",                                     \
                  gtid, ncounts, lb, ub, str, chunk_sz));                      \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,        \
                        (schedule) != kmp_sch_static);                         \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
    KMP_DOACROSS_FINI(status, gtid);                                           \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",    \
         gtid, *p_lb, *p_ub, status));                                         \
    __kmp_free(dims);                                                          \
    return status;                                                             \
  }

#define LOOP_DOACROSS_RUNTIME_START(func, schedule)                            \
  int func(unsigned ncounts, long *counts, long *p_lb, long *p_ub) {           \
    int status;                                                                \
    long stride, lb, ub, str;                                                  \
    long chunk_sz = 0;                                                         \
    int gtid = __kmp_entry_gtid();                                             \
    struct kmp_dim *dims =                                                     \
        (struct kmp_dim *)__kmp_allocate(sizeof(struct kmp_dim) * ncounts);    \
    MKLOC(loc, KMP_STR(func));                                                 \
    for (unsigned i = 0; i < ncounts; ++i) {                                   \
      dims[i].lo = 0;                                                          \
      dims[i].up = counts[i] - 1;                                              \
      dims[i].st = 1;                                                          \
    }                                                                          \
    __kmpc_doacross_init(&loc, gtid, (int)ncounts, dims);                      \
    lb = 0;                                                                    \
    ub = counts[0];                                                            \
    str = 1;                                                                   \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(func) ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz %d\n", \
         gtid, lb, ub, str, chunk_sz));                                        \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                            \
                        (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz, TRUE); \
      status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, (kmp_int *)p_lb,            \
                                 (kmp_int *)p_ub, (kmp_int *)&stride);         \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
    KMP_DOACROSS_FINI(status, gtid);                                           \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%lx, *p_ub 0x%lx, returning %d\n",    \
         gtid, *p_lb, *p_ub, status));                                         \
    __kmp_free(dims);                                                          \
    return status;                                                             \
  }

LOOP_DOACROSS_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DOACROSS_STATIC_START),
    kmp_sch_static)
LOOP_DOACROSS_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DOACROSS_DYNAMIC_START),
    kmp_sch_dynamic_chunked)
LOOP_DOACROSS_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DOACROSS_GUIDED_START),
    kmp_sch_guided_chunked)
LOOP_DOACROSS_RUNTIME_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_DOACROSS_RUNTIME_START),
    kmp_sch_runtime)
#endif // OMP_45_ENABLED

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_loop_end: T#%d\n", gtid))

#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif

  KA_TRACE(20, ("GOMP_loop_end exit: T#%d\n", gtid))
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_END_NOWAIT)(void) {
  KA_TRACE(20, ("GOMP_loop_end_nowait: T#%d\n", __kmp_get_gtid()))
}

// Unsigned long long loop worksharing constructs
//
// These are new with gcc 4.4

#define LOOP_START_ULL(func, schedule)                                         \
  int func(int up, unsigned long long lb, unsigned long long ub,               \
           unsigned long long str, unsigned long long chunk_sz,                \
           unsigned long long *p_lb, unsigned long long *p_ub) {               \
    int status;                                                                \
    long long str2 = up ? ((long long)str) : -((long long)str);                \
    long long stride;                                                          \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
                                                                               \
    KA_TRACE(20, (KMP_STR(func) ": T#%d, up %d, lb 0x%llx, ub 0x%llx, str "    \
                                "0x%llx, chunk_sz 0x%llx\n",                   \
                  gtid, up, lb, ub, str, chunk_sz));                           \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str2 > 0) ? (ub - 1) : (ub + 1), str2, chunk_sz,  \
                            (schedule) != kmp_sch_static);                     \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str2);                                      \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n",  \
         gtid, *p_lb, *p_ub, status));                                         \
    return status;                                                             \
  }

#define LOOP_RUNTIME_START_ULL(func, schedule)                                 \
  int func(int up, unsigned long long lb, unsigned long long ub,               \
           unsigned long long str, unsigned long long *p_lb,                   \
           unsigned long long *p_ub) {                                         \
    int status;                                                                \
    long long str2 = up ? ((long long)str) : -((long long)str);                \
    unsigned long long stride;                                                 \
    unsigned long long chunk_sz = 0;                                           \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
                                                                               \
    KA_TRACE(20, (KMP_STR(func) ": T#%d, up %d, lb 0x%llx, ub 0x%llx, str "    \
                                "0x%llx, chunk_sz 0x%llx\n",                   \
                  gtid, up, lb, ub, str, chunk_sz));                           \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str2 > 0) ? (ub - 1) : (ub + 1), str2, chunk_sz,  \
                            TRUE);                                             \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT((long long)stride == str2);                           \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n",  \
         gtid, *p_lb, *p_ub, status));                                         \
    return status;                                                             \
  }

#define LOOP_NEXT_ULL(func, fini_code)                                         \
  int func(unsigned long long *p_lb, unsigned long long *p_ub) {               \
    int status;                                                                \
    long long stride;                                                          \
    int gtid = __kmp_get_gtid();                                               \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(20, (KMP_STR(func) ": T#%d\n", gtid));                            \
                                                                               \
    fini_code status =                                                         \
        KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,            \
                              (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);       \
    if (status) {                                                              \
      *p_ub += (stride > 0) ? 1 : -1;                                          \
    }                                                                          \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, stride 0x%llx, "  \
                   "returning %d\n",                                           \
         gtid, *p_lb, *p_ub, stride, status));                                 \
    return status;                                                             \
  }

LOOP_START_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START),
               kmp_sch_static)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT), {})
LOOP_START_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START),
               kmp_sch_dynamic_chunked)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT), {})
LOOP_START_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START),
               kmp_sch_guided_chunked)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT), {})
LOOP_RUNTIME_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START), kmp_sch_runtime)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT), {})

LOOP_START_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START),
               kmp_ord_static)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START),
    kmp_ord_dynamic_chunked)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_START_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START),
               kmp_ord_guided_chunked)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })
LOOP_RUNTIME_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START),
    kmp_ord_runtime)
LOOP_NEXT_ULL(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT),
              { KMP_DISPATCH_FINI_CHUNK_ULL(&loc, gtid); })

#if OMP_45_ENABLED
#define LOOP_DOACROSS_START_ULL(func, schedule)                                \
  int func(unsigned ncounts, unsigned long long *counts,                       \
           unsigned long long chunk_sz, unsigned long long *p_lb,              \
           unsigned long long *p_ub) {                                         \
    int status;                                                                \
    long long stride, str, lb, ub;                                             \
    int gtid = __kmp_entry_gtid();                                             \
    struct kmp_dim *dims =                                                     \
        (struct kmp_dim *)__kmp_allocate(sizeof(struct kmp_dim) * ncounts);    \
    MKLOC(loc, KMP_STR(func));                                                 \
    for (unsigned i = 0; i < ncounts; ++i) {                                   \
      dims[i].lo = 0;                                                          \
      dims[i].up = counts[i] - 1;                                              \
      dims[i].st = 1;                                                          \
    }                                                                          \
    __kmpc_doacross_init(&loc, gtid, (int)ncounts, dims);                      \
    lb = 0;                                                                    \
    ub = counts[0];                                                            \
    str = 1;                                                                   \
                                                                               \
    KA_TRACE(20, (KMP_STR(func) ": T#%d, lb 0x%llx, ub 0x%llx, str "           \
                                "0x%llx, chunk_sz 0x%llx\n",                   \
                  gtid, lb, ub, str, chunk_sz));                               \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,    \
                            (schedule) != kmp_sch_static);                     \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
    KMP_DOACROSS_FINI(status, gtid);                                           \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n",  \
         gtid, *p_lb, *p_ub, status));                                         \
    __kmp_free(dims);                                                          \
    return status;                                                             \
  }

#define LOOP_DOACROSS_RUNTIME_START_ULL(func, schedule)                        \
  int func(unsigned ncounts, unsigned long long *counts,                       \
           unsigned long long *p_lb, unsigned long long *p_ub) {               \
    int status;                                                                \
    unsigned long long stride, str, lb, ub;                                    \
    unsigned long long chunk_sz = 0;                                           \
    int gtid = __kmp_entry_gtid();                                             \
    struct kmp_dim *dims =                                                     \
        (struct kmp_dim *)__kmp_allocate(sizeof(struct kmp_dim) * ncounts);    \
    MKLOC(loc, KMP_STR(func));                                                 \
    for (unsigned i = 0; i < ncounts; ++i) {                                   \
      dims[i].lo = 0;                                                          \
      dims[i].up = counts[i] - 1;                                              \
      dims[i].st = 1;                                                          \
    }                                                                          \
    __kmpc_doacross_init(&loc, gtid, (int)ncounts, dims);                      \
    lb = 0;                                                                    \
    ub = counts[0];                                                            \
    str = 1;                                                                   \
    KA_TRACE(20, (KMP_STR(func) ": T#%d, lb 0x%llx, ub 0x%llx, str "           \
                                "0x%llx, chunk_sz 0x%llx\n",                   \
                  gtid, lb, ub, str, chunk_sz));                               \
                                                                               \
    if ((str > 0) ? (lb < ub) : (lb > ub)) {                                   \
      KMP_DISPATCH_INIT_ULL(&loc, gtid, (schedule), lb,                        \
                            (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,    \
                            TRUE);                                             \
      status =                                                                 \
          KMP_DISPATCH_NEXT_ULL(&loc, gtid, NULL, (kmp_uint64 *)p_lb,          \
                                (kmp_uint64 *)p_ub, (kmp_int64 *)&stride);     \
      if (status) {                                                            \
        KMP_DEBUG_ASSERT(stride == str);                                       \
        *p_ub += (str > 0) ? 1 : -1;                                           \
      }                                                                        \
    } else {                                                                   \
      status = 0;                                                              \
    }                                                                          \
    KMP_DOACROSS_FINI(status, gtid);                                           \
                                                                               \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) " exit: T#%d, *p_lb 0x%llx, *p_ub 0x%llx, returning %d\n",  \
         gtid, *p_lb, *p_ub, status));                                         \
    __kmp_free(dims);                                                          \
    return status;                                                             \
  }

LOOP_DOACROSS_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_STATIC_START),
    kmp_sch_static)
LOOP_DOACROSS_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_DYNAMIC_START),
    kmp_sch_dynamic_chunked)
LOOP_DOACROSS_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_GUIDED_START),
    kmp_sch_guided_chunked)
LOOP_DOACROSS_RUNTIME_START_ULL(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_RUNTIME_START),
    kmp_sch_runtime)
#endif

// Combined parallel / loop worksharing constructs
//
// There are no ull versions (yet).

#define PARALLEL_LOOP_START(func, schedule, ompt_pre, ompt_post)               \
  void func(void (*task)(void *), void *data, unsigned num_threads, long lb,   \
            long ub, long str, long chunk_sz) {                                \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n",  \
         gtid, lb, ub, str, chunk_sz));                                        \
                                                                               \
    ompt_pre();                                                                \
                                                                               \
    if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {                       \
      if (num_threads != 0) {                                                  \
        __kmp_push_num_threads(&loc, gtid, num_threads);                       \
      }                                                                        \
      __kmp_GOMP_fork_call(&loc, gtid, task,                                   \
                           (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, \
                           9, task, data, num_threads, &loc, (schedule), lb,   \
                           (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz);    \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid));                        \
    } else {                                                                   \
      __kmp_GOMP_serialized_parallel(&loc, gtid, task);                        \
      IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid));                        \
    }                                                                          \
                                                                               \
    KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                              \
                      (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,          \
                      (schedule) != kmp_sch_static);                           \
                                                                               \
    ompt_post();                                                               \
                                                                               \
    KA_TRACE(20, (KMP_STR(func) " exit: T#%d\n", gtid));                       \
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL

#define OMPT_LOOP_PRE()                                                        \
  ompt_frame_t *parent_frame;                                                  \
  if (ompt_enabled.enabled) {                                                  \
    __ompt_get_task_info_internal(0, NULL, NULL, &parent_frame, NULL, NULL);   \
    parent_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);                 \
    OMPT_STORE_RETURN_ADDRESS(gtid);                                           \
  }

#define OMPT_LOOP_POST()                                                       \
  if (ompt_enabled.enabled) {                                                  \
    parent_frame->enter_frame = ompt_data_none;                                \
  }

#else

#define OMPT_LOOP_PRE()

#define OMPT_LOOP_POST()

#endif

PARALLEL_LOOP_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START),
    kmp_sch_static, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START),
    kmp_sch_dynamic_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START),
    kmp_sch_guided_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP_START(
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START),
    kmp_sch_runtime, OMPT_LOOP_PRE, OMPT_LOOP_POST)

// Tasking constructs

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASK)(void (*func)(void *), void *data,
                                             void (*copy_func)(void *, void *),
                                             long arg_size, long arg_align,
                                             bool if_cond, unsigned gomp_flags
#if OMP_40_ENABLED
                                             ,
                                             void **depend
#endif
                                             ) {
  MKLOC(loc, "GOMP_task");
  int gtid = __kmp_entry_gtid();
  kmp_int32 flags = 0;
  kmp_tasking_flags_t *input_flags = (kmp_tasking_flags_t *)&flags;

  KA_TRACE(20, ("GOMP_task: T#%d\n", gtid));

  // The low-order bit is the "untied" flag
  if (!(gomp_flags & 1)) {
    input_flags->tiedness = 1;
  }
  // The second low-order bit is the "final" flag
  if (gomp_flags & 2) {
    input_flags->final = 1;
  }
  input_flags->native = 1;
  // __kmp_task_alloc() sets up all other flags

  if (!if_cond) {
    arg_size = 0;
  }

  kmp_task_t *task = __kmp_task_alloc(
      &loc, gtid, input_flags, sizeof(kmp_task_t),
      arg_size ? arg_size + arg_align - 1 : 0, (kmp_routine_entry_t)func);

  if (arg_size > 0) {
    if (arg_align > 0) {
      task->shareds = (void *)((((size_t)task->shareds) + arg_align - 1) /
                               arg_align * arg_align);
    }
    // else error??

    if (copy_func) {
      (*copy_func)(task->shareds, data);
    } else {
      KMP_MEMCPY(task->shareds, data, arg_size);
    }
  }

#if OMPT_SUPPORT
  kmp_taskdata_t *current_task;
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
    current_task = __kmp_threads[gtid]->th.th_current_task;
    current_task->ompt_task_info.frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif

  if (if_cond) {
#if OMP_40_ENABLED
    if (gomp_flags & 8) {
      KMP_ASSERT(depend);
      const size_t ndeps = (kmp_intptr_t)depend[0];
      const size_t nout = (kmp_intptr_t)depend[1];
      kmp_depend_info_t dep_list[ndeps];

      for (size_t i = 0U; i < ndeps; i++) {
        dep_list[i].base_addr = (kmp_intptr_t)depend[2U + i];
        dep_list[i].len = 0U;
        dep_list[i].flags.in = 1;
        dep_list[i].flags.out = (i < nout);
      }
      __kmpc_omp_task_with_deps(&loc, gtid, task, ndeps, dep_list, 0, NULL);
    } else {
#endif
      __kmpc_omp_task(&loc, gtid, task);
    }
  } else {
#if OMPT_SUPPORT
    ompt_thread_info_t oldInfo;
    kmp_info_t *thread;
    kmp_taskdata_t *taskdata;
    if (ompt_enabled.enabled) {
      // Store the threads states and restore them after the task
      thread = __kmp_threads[gtid];
      taskdata = KMP_TASK_TO_TASKDATA(task);
      oldInfo = thread->th.ompt_thread_info;
      thread->th.ompt_thread_info.wait_id = 0;
      thread->th.ompt_thread_info.state = ompt_state_work_parallel;
      taskdata->ompt_task_info.frame.exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(gtid);
    }
#endif

    __kmpc_omp_task_begin_if0(&loc, gtid, task);
    func(data);
    __kmpc_omp_task_complete_if0(&loc, gtid, task);

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      thread->th.ompt_thread_info = oldInfo;
      taskdata->ompt_task_info.frame.exit_frame = ompt_data_none;
    }
#endif
  }
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    current_task->ompt_task_info.frame.enter_frame = ompt_data_none;
  }
#endif

  KA_TRACE(20, ("GOMP_task exit: T#%d\n", gtid));
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKWAIT)(void) {
  MKLOC(loc, "GOMP_taskwait");
  int gtid = __kmp_entry_gtid();

#if OMPT_SUPPORT
  if (ompt_enabled.enabled)
    OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  KA_TRACE(20, ("GOMP_taskwait: T#%d\n", gtid));

  __kmpc_omp_taskwait(&loc, gtid);

  KA_TRACE(20, ("GOMP_taskwait exit: T#%d\n", gtid));
}

// Sections worksharing constructs
//
// For the sections construct, we initialize a dynamically scheduled loop
// worksharing construct with lb 1 and stride 1, and use the iteration #'s
// that its returns as sections ids.
//
// There are no special entry points for ordered sections, so we always use
// the dynamically scheduled workshare, even if the sections aren't ordered.

unsigned KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SECTIONS_START)(unsigned count) {
  int status;
  kmp_int lb, ub, stride;
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_sections_start");
  KA_TRACE(20, ("GOMP_sections_start: T#%d\n", gtid));

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, &lb, &ub, &stride);
  if (status) {
    KMP_DEBUG_ASSERT(stride == 1);
    KMP_DEBUG_ASSERT(lb > 0);
    KMP_ASSERT(lb == ub);
  } else {
    lb = 0;
  }

  KA_TRACE(20, ("GOMP_sections_start exit: T#%d returning %u\n", gtid,
                (unsigned)lb));
  return (unsigned)lb;
}

unsigned KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SECTIONS_NEXT)(void) {
  int status;
  kmp_int lb, ub, stride;
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_sections_next");
  KA_TRACE(20, ("GOMP_sections_next: T#%d\n", gtid));

#if OMPT_SUPPORT
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  status = KMP_DISPATCH_NEXT(&loc, gtid, NULL, &lb, &ub, &stride);
  if (status) {
    KMP_DEBUG_ASSERT(stride == 1);
    KMP_DEBUG_ASSERT(lb > 0);
    KMP_ASSERT(lb == ub);
  } else {
    lb = 0;
  }

  KA_TRACE(
      20, ("GOMP_sections_next exit: T#%d returning %u\n", gtid, (unsigned)lb));
  return (unsigned)lb;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START)(
    void (*task)(void *), void *data, unsigned num_threads, unsigned count) {
  int gtid = __kmp_entry_gtid();

#if OMPT_SUPPORT
  ompt_frame_t *parent_frame;

  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &parent_frame, NULL, NULL);
    parent_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif

  MKLOC(loc, "GOMP_parallel_sections_start");
  KA_TRACE(20, ("GOMP_parallel_sections_start: T#%d\n", gtid));

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, 9,
                         task, data, num_threads, &loc, kmp_nm_dynamic_chunked,
                         (kmp_int)1, (kmp_int)count, (kmp_int)1, (kmp_int)1);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    parent_frame->enter_frame = ompt_data_none;
  }
#endif

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  KA_TRACE(20, ("GOMP_parallel_sections_start exit: T#%d\n", gtid));
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SECTIONS_END)(void) {
  int gtid = __kmp_get_gtid();
  KA_TRACE(20, ("GOMP_sections_end: T#%d\n", gtid))

#if OMPT_SUPPORT
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif

  KA_TRACE(20, ("GOMP_sections_end exit: T#%d\n", gtid))
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT)(void) {
  KA_TRACE(20, ("GOMP_sections_end_nowait: T#%d\n", __kmp_get_gtid()))
}

// libgomp has an empty function for GOMP_taskyield as of 2013-10-10
void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKYIELD)(void) {
  KA_TRACE(20, ("GOMP_taskyield: T#%d\n", __kmp_get_gtid()))
  return;
}

#if OMP_40_ENABLED // these are new GOMP_4.0 entry points

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL)(void (*task)(void *),
                                                 void *data,
                                                 unsigned num_threads,
                                                 unsigned int flags) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_parallel");
  KA_TRACE(20, ("GOMP_parallel: T#%d\n", gtid));

#if OMPT_SUPPORT
  ompt_task_info_t *parent_task_info, *task_info;
  if (ompt_enabled.enabled) {
    parent_task_info = __ompt_get_task_info_object(0);
    parent_task_info->frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    if (flags != 0) {
      __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_microtask_wrapper, 2, task,
                         data);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    task_info = __ompt_get_task_info_object(0);
    task_info->frame.exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif
  task(data);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
  KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_END)();
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    task_info->frame.exit_frame = ompt_data_none;
    parent_task_info->frame.enter_frame = ompt_data_none;
  }
#endif
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_SECTIONS)(void (*task)(void *),
                                                          void *data,
                                                          unsigned num_threads,
                                                          unsigned count,
                                                          unsigned flags) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_parallel_sections");
  KA_TRACE(20, ("GOMP_parallel_sections: T#%d\n", gtid));

#if OMPT_SUPPORT
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {
    if (num_threads != 0) {
      __kmp_push_num_threads(&loc, gtid, num_threads);
    }
    if (flags != 0) {
      __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);
    }
    __kmp_GOMP_fork_call(&loc, gtid, task,
                         (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, 9,
                         task, data, num_threads, &loc, kmp_nm_dynamic_chunked,
                         (kmp_int)1, (kmp_int)count, (kmp_int)1, (kmp_int)1);
  } else {
    __kmp_GOMP_serialized_parallel(&loc, gtid, task);
  }

#if OMPT_SUPPORT
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  KMP_DISPATCH_INIT(&loc, gtid, kmp_nm_dynamic_chunked, 1, count, 1, 1, TRUE);

  task(data);
  KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_END)();
  KA_TRACE(20, ("GOMP_parallel_sections exit: T#%d\n", gtid));
}

#define PARALLEL_LOOP(func, schedule, ompt_pre, ompt_post)                     \
  void func(void (*task)(void *), void *data, unsigned num_threads, long lb,   \
            long ub, long str, long chunk_sz, unsigned flags) {                \
    int gtid = __kmp_entry_gtid();                                             \
    MKLOC(loc, KMP_STR(func));                                                 \
    KA_TRACE(                                                                  \
        20,                                                                    \
        (KMP_STR(                                                              \
             func) ": T#%d, lb 0x%lx, ub 0x%lx, str 0x%lx, chunk_sz 0x%lx\n",  \
         gtid, lb, ub, str, chunk_sz));                                        \
                                                                               \
    ompt_pre();                                                                \
    if (__kmpc_ok_to_fork(&loc) && (num_threads != 1)) {                       \
      if (num_threads != 0) {                                                  \
        __kmp_push_num_threads(&loc, gtid, num_threads);                       \
      }                                                                        \
      if (flags != 0) {                                                        \
        __kmp_push_proc_bind(&loc, gtid, (kmp_proc_bind_t)flags);              \
      }                                                                        \
      __kmp_GOMP_fork_call(&loc, gtid, task,                                   \
                           (microtask_t)__kmp_GOMP_parallel_microtask_wrapper, \
                           9, task, data, num_threads, &loc, (schedule), lb,   \
                           (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz);    \
    } else {                                                                   \
      __kmp_GOMP_serialized_parallel(&loc, gtid, task);                        \
    }                                                                          \
                                                                               \
    IF_OMPT_SUPPORT(OMPT_STORE_RETURN_ADDRESS(gtid);)                          \
    KMP_DISPATCH_INIT(&loc, gtid, (schedule), lb,                              \
                      (str > 0) ? (ub - 1) : (ub + 1), str, chunk_sz,          \
                      (schedule) != kmp_sch_static);                           \
    task(data);                                                                \
    KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_END)();                         \
    ompt_post();                                                               \
                                                                               \
    KA_TRACE(20, (KMP_STR(func) " exit: T#%d\n", gtid));                       \
  }

PARALLEL_LOOP(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC),
              kmp_sch_static, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC),
              kmp_sch_dynamic_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED),
              kmp_sch_guided_chunked, OMPT_LOOP_PRE, OMPT_LOOP_POST)
PARALLEL_LOOP(KMP_EXPAND_NAME(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME),
              kmp_sch_runtime, OMPT_LOOP_PRE, OMPT_LOOP_POST)

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKGROUP_START)(void) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_taskgroup_start");
  KA_TRACE(20, ("GOMP_taskgroup_start: T#%d\n", gtid));

#if OMPT_SUPPORT
  if (ompt_enabled.enabled)
    OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  __kmpc_taskgroup(&loc, gtid);

  return;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKGROUP_END)(void) {
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_taskgroup_end");
  KA_TRACE(20, ("GOMP_taskgroup_end: T#%d\n", gtid));

#if OMPT_SUPPORT
  if (ompt_enabled.enabled)
    OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  __kmpc_end_taskgroup(&loc, gtid);

  return;
}

#ifndef KMP_DEBUG
static
#endif /* KMP_DEBUG */
    kmp_int32
    __kmp_gomp_to_omp_cancellation_kind(int gomp_kind) {
  kmp_int32 cncl_kind = 0;
  switch (gomp_kind) {
  case 1:
    cncl_kind = cancel_parallel;
    break;
  case 2:
    cncl_kind = cancel_loop;
    break;
  case 4:
    cncl_kind = cancel_sections;
    break;
  case 8:
    cncl_kind = cancel_taskgroup;
    break;
  }
  return cncl_kind;
}

bool KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CANCELLATION_POINT)(int which) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_cancellation_point");
  KA_TRACE(20, ("GOMP_cancellation_point: T#%d\n", gtid));

  kmp_int32 cncl_kind = __kmp_gomp_to_omp_cancellation_kind(which);

  return __kmpc_cancellationpoint(&loc, gtid, cncl_kind);
}

bool KMP_EXPAND_NAME(KMP_API_NAME_GOMP_BARRIER_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  KMP_FATAL(NoGompCancellation);
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_barrier_cancel");
  KA_TRACE(20, ("GOMP_barrier_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

bool KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CANCEL)(int which, bool do_cancel) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  } else {
    return FALSE;
  }

  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_cancel");
  KA_TRACE(20, ("GOMP_cancel: T#%d\n", gtid));

  kmp_int32 cncl_kind = __kmp_gomp_to_omp_cancellation_kind(which);

  if (do_cancel == FALSE) {
    return KMP_EXPAND_NAME(KMP_API_NAME_GOMP_CANCELLATION_POINT)(which);
  } else {
    return __kmpc_cancel(&loc, gtid, cncl_kind);
  }
}

bool KMP_EXPAND_NAME(KMP_API_NAME_GOMP_SECTIONS_END_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_sections_end_cancel");
  KA_TRACE(20, ("GOMP_sections_end_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

bool KMP_EXPAND_NAME(KMP_API_NAME_GOMP_LOOP_END_CANCEL)(void) {
  if (__kmp_omp_cancellation) {
    KMP_FATAL(NoGompCancellation);
  }
  int gtid = __kmp_get_gtid();
  MKLOC(loc, "GOMP_loop_end_cancel");
  KA_TRACE(20, ("GOMP_loop_end_cancel: T#%d\n", gtid));

  return __kmpc_cancel_barrier(&loc, gtid);
}

// All target functions are empty as of 2014-05-29
void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TARGET)(int device, void (*fn)(void *),
                                               const void *openmp_target,
                                               size_t mapnum, void **hostaddrs,
                                               size_t *sizes,
                                               unsigned char *kinds) {
  return;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TARGET_DATA)(
    int device, const void *openmp_target, size_t mapnum, void **hostaddrs,
    size_t *sizes, unsigned char *kinds) {
  return;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TARGET_END_DATA)(void) { return; }

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TARGET_UPDATE)(
    int device, const void *openmp_target, size_t mapnum, void **hostaddrs,
    size_t *sizes, unsigned char *kinds) {
  return;
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TEAMS)(unsigned int num_teams,
                                              unsigned int thread_limit) {
  return;
}
#endif // OMP_40_ENABLED

#if OMP_45_ENABLED

// Task duplication function which copies src to dest (both are
// preallocated task structures)
static void __kmp_gomp_task_dup(kmp_task_t *dest, kmp_task_t *src,
                                kmp_int32 last_private) {
  kmp_taskdata_t *taskdata = KMP_TASK_TO_TASKDATA(src);
  if (taskdata->td_copy_func) {
    (taskdata->td_copy_func)(dest->shareds, src->shareds);
  }
}

#ifdef __cplusplus
} // extern "C"
#endif

template <typename T>
void __GOMP_taskloop(void (*func)(void *), void *data,
                     void (*copy_func)(void *, void *), long arg_size,
                     long arg_align, unsigned gomp_flags,
                     unsigned long num_tasks, int priority, T start, T end,
                     T step) {
  typedef void (*p_task_dup_t)(kmp_task_t *, kmp_task_t *, kmp_int32);
  MKLOC(loc, "GOMP_taskloop");
  int sched;
  T *loop_bounds;
  int gtid = __kmp_entry_gtid();
  kmp_int32 flags = 0;
  int if_val = gomp_flags & (1u << 10);
  int nogroup = gomp_flags & (1u << 11);
  int up = gomp_flags & (1u << 8);
  p_task_dup_t task_dup = NULL;
  kmp_tasking_flags_t *input_flags = (kmp_tasking_flags_t *)&flags;
#ifdef KMP_DEBUG
  {
    char *buff;
    buff = __kmp_str_format(
        "GOMP_taskloop: T#%%d: func:%%p data:%%p copy_func:%%p "
        "arg_size:%%ld arg_align:%%ld gomp_flags:0x%%x num_tasks:%%lu "
        "priority:%%d start:%%%s end:%%%s step:%%%s\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<T>::spec);
    KA_TRACE(20, (buff, gtid, func, data, copy_func, arg_size, arg_align,
                  gomp_flags, num_tasks, priority, start, end, step));
    __kmp_str_free(&buff);
  }
#endif
  KMP_ASSERT((size_t)arg_size >= 2 * sizeof(T));
  KMP_ASSERT(arg_align > 0);
  // The low-order bit is the "untied" flag
  if (!(gomp_flags & 1)) {
    input_flags->tiedness = 1;
  }
  // The second low-order bit is the "final" flag
  if (gomp_flags & 2) {
    input_flags->final = 1;
  }
  // Negative step flag
  if (!up) {
    // If step is flagged as negative, but isn't properly sign extended
    // Then manually sign extend it.  Could be a short, int, char embedded
    // in a long.  So cannot assume any cast.
    if (step > 0) {
      for (int i = sizeof(T) * CHAR_BIT - 1; i >= 0L; --i) {
        // break at the first 1 bit
        if (step & ((T)1 << i))
          break;
        step |= ((T)1 << i);
      }
    }
  }
  input_flags->native = 1;
  // Figure out if none/grainsize/num_tasks clause specified
  if (num_tasks > 0) {
    if (gomp_flags & (1u << 9))
      sched = 1; // grainsize specified
    else
      sched = 2; // num_tasks specified
    // neither grainsize nor num_tasks specified
  } else {
    sched = 0;
  }

  // __kmp_task_alloc() sets up all other flags
  kmp_task_t *task =
      __kmp_task_alloc(&loc, gtid, input_flags, sizeof(kmp_task_t),
                       arg_size + arg_align - 1, (kmp_routine_entry_t)func);
  kmp_taskdata_t *taskdata = KMP_TASK_TO_TASKDATA(task);
  taskdata->td_copy_func = copy_func;
  taskdata->td_size_loop_bounds = sizeof(T);

  // re-align shareds if needed and setup firstprivate copy constructors
  // through the task_dup mechanism
  task->shareds = (void *)((((size_t)task->shareds) + arg_align - 1) /
                           arg_align * arg_align);
  if (copy_func) {
    task_dup = __kmp_gomp_task_dup;
  }
  KMP_MEMCPY(task->shareds, data, arg_size);

  loop_bounds = (T *)task->shareds;
  loop_bounds[0] = start;
  loop_bounds[1] = end + (up ? -1 : 1);
  __kmpc_taskloop(&loc, gtid, task, if_val, (kmp_uint64 *)&(loop_bounds[0]),
                  (kmp_uint64 *)&(loop_bounds[1]), (kmp_int64)step, nogroup,
                  sched, (kmp_uint64)num_tasks, (void *)task_dup);
}

// 4 byte version of GOMP_doacross_post
// This verison needs to create a temporary array which converts 4 byte
// integers into 8 byte integeres
template <typename T, bool need_conversion = (sizeof(long) == 4)>
void __kmp_GOMP_doacross_post(T *count);

template <> void __kmp_GOMP_doacross_post<long, true>(long *count) {
  int gtid = __kmp_entry_gtid();
  kmp_info_t *th = __kmp_threads[gtid];
  MKLOC(loc, "GOMP_doacross_post");
  kmp_int64 num_dims = th->th.th_dispatch->th_doacross_info[0];
  kmp_int64 *vec =
      (kmp_int64 *)__kmp_thread_malloc(th, sizeof(kmp_int64) * num_dims);
  for (kmp_int64 i = 0; i < num_dims; ++i) {
    vec[i] = (kmp_int64)count[i];
  }
  __kmpc_doacross_post(&loc, gtid, vec);
  __kmp_thread_free(th, vec);
}

// 8 byte versions of GOMP_doacross_post
// This version can just pass in the count array directly instead of creating
// a temporary array
template <> void __kmp_GOMP_doacross_post<long, false>(long *count) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_doacross_post");
  __kmpc_doacross_post(&loc, gtid, RCAST(kmp_int64 *, count));
}

template <typename T> void __kmp_GOMP_doacross_wait(T first, va_list args) {
  int gtid = __kmp_entry_gtid();
  kmp_info_t *th = __kmp_threads[gtid];
  MKLOC(loc, "GOMP_doacross_wait");
  kmp_int64 num_dims = th->th.th_dispatch->th_doacross_info[0];
  kmp_int64 *vec =
      (kmp_int64 *)__kmp_thread_malloc(th, sizeof(kmp_int64) * num_dims);
  vec[0] = (kmp_int64)first;
  for (kmp_int64 i = 1; i < num_dims; ++i) {
    T item = va_arg(args, T);
    vec[i] = (kmp_int64)item;
  }
  __kmpc_doacross_wait(&loc, gtid, vec);
  __kmp_thread_free(th, vec);
  return;
}

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKLOOP)(
    void (*func)(void *), void *data, void (*copy_func)(void *, void *),
    long arg_size, long arg_align, unsigned gomp_flags, unsigned long num_tasks,
    int priority, long start, long end, long step) {
  __GOMP_taskloop<long>(func, data, copy_func, arg_size, arg_align, gomp_flags,
                        num_tasks, priority, start, end, step);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_TASKLOOP_ULL)(
    void (*func)(void *), void *data, void (*copy_func)(void *, void *),
    long arg_size, long arg_align, unsigned gomp_flags, unsigned long num_tasks,
    int priority, unsigned long long start, unsigned long long end,
    unsigned long long step) {
  __GOMP_taskloop<unsigned long long>(func, data, copy_func, arg_size,
                                      arg_align, gomp_flags, num_tasks,
                                      priority, start, end, step);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_DOACROSS_POST)(long *count) {
  __kmp_GOMP_doacross_post(count);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_DOACROSS_WAIT)(long first, ...) {
  va_list args;
  va_start(args, first);
  __kmp_GOMP_doacross_wait<long>(first, args);
  va_end(args);
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_DOACROSS_ULL_POST)(
    unsigned long long *count) {
  int gtid = __kmp_entry_gtid();
  MKLOC(loc, "GOMP_doacross_ull_post");
  __kmpc_doacross_post(&loc, gtid, RCAST(kmp_int64 *, count));
}

void KMP_EXPAND_NAME(KMP_API_NAME_GOMP_DOACROSS_ULL_WAIT)(
    unsigned long long first, ...) {
  va_list args;
  va_start(args, first);
  __kmp_GOMP_doacross_wait<unsigned long long>(first, args);
  va_end(args);
}

#endif // OMP_45_ENABLED

/* The following sections of code create aliases for the GOMP_* functions, then
   create versioned symbols using the assembler directive .symver. This is only
   pertinent for ELF .so library. The KMP_VERSION_SYMBOL macro is defined in
   kmp_os.h  */

#ifdef KMP_USE_VERSION_SYMBOLS
// GOMP_1.0 versioned symbols
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_ATOMIC_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_ATOMIC_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_BARRIER, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CRITICAL_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CRITICAL_NAME_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CRITICAL_NAME_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CRITICAL_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DYNAMIC_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DYNAMIC_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_END_NOWAIT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_GUIDED_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_GUIDED_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_DYNAMIC_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_GUIDED_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_RUNTIME_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ORDERED_STATIC_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_RUNTIME_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_RUNTIME_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_STATIC_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_STATIC_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_ORDERED_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_ORDERED_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC_START, 10,
                   "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_SECTIONS_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SECTIONS_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SECTIONS_END_NOWAIT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SECTIONS_NEXT, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SECTIONS_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SINGLE_COPY_END, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SINGLE_COPY_START, 10, "GOMP_1.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SINGLE_START, 10, "GOMP_1.0");

// GOMP_2.0 versioned symbols
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASK, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKWAIT, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_NEXT, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DYNAMIC_START, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_NEXT, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_GUIDED_START, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_NEXT, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_DYNAMIC_START, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_NEXT, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_GUIDED_START, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_NEXT, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_RUNTIME_START, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_NEXT, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_ORDERED_STATIC_START, 20,
                   "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_NEXT, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_RUNTIME_START, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_NEXT, 20, "GOMP_2.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_STATIC_START, 20, "GOMP_2.0");

// GOMP_3.0 versioned symbols
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKYIELD, 30, "GOMP_3.0");

// GOMP_4.0 versioned symbols
#if OMP_40_ENABLED
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_SECTIONS, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_DYNAMIC, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_GUIDED, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_RUNTIME, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_PARALLEL_LOOP_STATIC, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKGROUP_START, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKGROUP_END, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_BARRIER_CANCEL, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CANCEL, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_CANCELLATION_POINT, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_END_CANCEL, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_SECTIONS_END_CANCEL, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TARGET, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TARGET_DATA, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TARGET_END_DATA, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TARGET_UPDATE, 40, "GOMP_4.0");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TEAMS, 40, "GOMP_4.0");
#endif

// GOMP_4.5 versioned symbols
#if OMP_45_ENABLED
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKLOOP, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_TASKLOOP_ULL, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_DOACROSS_POST, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_DOACROSS_WAIT, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DOACROSS_STATIC_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DOACROSS_DYNAMIC_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DOACROSS_GUIDED_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_DOACROSS_RUNTIME_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_DOACROSS_ULL_POST, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_DOACROSS_ULL_WAIT, 45, "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_STATIC_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_DYNAMIC_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_GUIDED_START, 45,
                   "GOMP_4.5");
KMP_VERSION_SYMBOL(KMP_API_NAME_GOMP_LOOP_ULL_DOACROSS_RUNTIME_START, 45,
                   "GOMP_4.5");
#endif

#endif // KMP_USE_VERSION_SYMBOLS

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
