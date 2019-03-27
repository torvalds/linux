#if USE_ITT_BUILD
/*
 * kmp_itt.h -- ITT Notify interface.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_ITT_H
#define KMP_ITT_H

#include "kmp_lock.h"

#define INTEL_ITTNOTIFY_API_PRIVATE
#include "ittnotify.h"
#include "legacy/ittnotify.h"

#if KMP_DEBUG
#define __kmp_inline // Turn off inlining in debug mode.
#else
#define __kmp_inline static inline
#endif

#if USE_ITT_NOTIFY
extern kmp_int32 __kmp_itt_prepare_delay;
#ifdef __cplusplus
extern "C" void __kmp_itt_fini_ittlib(void);
#else
extern void __kmp_itt_fini_ittlib(void);
#endif
#endif

// Simplify the handling of an argument that is only required when USE_ITT_BUILD
// is enabled.
#define USE_ITT_BUILD_ARG(x) , x

void __kmp_itt_initialize();
void __kmp_itt_destroy();
void __kmp_itt_reset();

// -----------------------------------------------------------------------------
// New stuff for reporting high-level constructs.

// Note the naming convention:
//     __kmp_itt_xxxing() function should be called before action, while
//     __kmp_itt_xxxed()  function should be called after action.

// --- Parallel region reporting ---
__kmp_inline void
__kmp_itt_region_forking(int gtid, int team_size,
                         int barriers); // Master only, before forking threads.
__kmp_inline void
__kmp_itt_region_joined(int gtid); // Master only, after joining threads.
// (*) Note: A thread may execute tasks after this point, though.

// --- Frame reporting ---
// region=0: no regions, region=1: parallel, region=2: serialized parallel
__kmp_inline void __kmp_itt_frame_submit(int gtid, __itt_timestamp begin,
                                         __itt_timestamp end, int imbalance,
                                         ident_t *loc, int team_size,
                                         int region = 0);

// --- Metadata reporting ---
// begin/end - begin/end timestamps of a barrier frame, imbalance - aggregated
// wait time value, reduction -if this is a reduction barrier
__kmp_inline void __kmp_itt_metadata_imbalance(int gtid, kmp_uint64 begin,
                                               kmp_uint64 end,
                                               kmp_uint64 imbalance,
                                               kmp_uint64 reduction);
// sched_type: 0 - static, 1 - dynamic, 2 - guided, 3 - custom (all others);
// iterations - loop trip count, chunk - chunk size
__kmp_inline void __kmp_itt_metadata_loop(ident_t *loc, kmp_uint64 sched_type,
                                          kmp_uint64 iterations,
                                          kmp_uint64 chunk);
__kmp_inline void __kmp_itt_metadata_single(ident_t *loc);

// --- Barrier reporting ---
__kmp_inline void *__kmp_itt_barrier_object(int gtid, int bt, int set_name = 0,
                                            int delta = 0);
__kmp_inline void __kmp_itt_barrier_starting(int gtid, void *object);
__kmp_inline void __kmp_itt_barrier_middle(int gtid, void *object);
__kmp_inline void __kmp_itt_barrier_finished(int gtid, void *object);

// --- Taskwait reporting ---
__kmp_inline void *__kmp_itt_taskwait_object(int gtid);
__kmp_inline void __kmp_itt_taskwait_starting(int gtid, void *object);
__kmp_inline void __kmp_itt_taskwait_finished(int gtid, void *object);

// --- Task reporting ---
__kmp_inline void __kmp_itt_task_starting(void *object);
__kmp_inline void __kmp_itt_task_finished(void *object);

// --- Lock reporting ---
#if KMP_USE_DYNAMIC_LOCK
__kmp_inline void __kmp_itt_lock_creating(kmp_user_lock_p lock,
                                          const ident_t *);
#else
__kmp_inline void __kmp_itt_lock_creating(kmp_user_lock_p lock);
#endif
__kmp_inline void __kmp_itt_lock_acquiring(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_lock_acquired(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_lock_releasing(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_lock_cancelled(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_lock_destroyed(kmp_user_lock_p lock);

// --- Critical reporting ---
#if KMP_USE_DYNAMIC_LOCK
__kmp_inline void __kmp_itt_critical_creating(kmp_user_lock_p lock,
                                              const ident_t *);
#else
__kmp_inline void __kmp_itt_critical_creating(kmp_user_lock_p lock);
#endif
__kmp_inline void __kmp_itt_critical_acquiring(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_critical_acquired(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_critical_releasing(kmp_user_lock_p lock);
__kmp_inline void __kmp_itt_critical_destroyed(kmp_user_lock_p lock);

// --- Single reporting ---
__kmp_inline void __kmp_itt_single_start(int gtid);
__kmp_inline void __kmp_itt_single_end(int gtid);

// --- Ordered reporting ---
__kmp_inline void __kmp_itt_ordered_init(int gtid);
__kmp_inline void __kmp_itt_ordered_prep(int gtid);
__kmp_inline void __kmp_itt_ordered_start(int gtid);
__kmp_inline void __kmp_itt_ordered_end(int gtid);

// --- Threads reporting ---
__kmp_inline void __kmp_itt_thread_ignore();
__kmp_inline void __kmp_itt_thread_name(int gtid);

// --- System objects ---
__kmp_inline void __kmp_itt_system_object_created(void *object,
                                                  char const *name);

// --- Stack stitching ---
__kmp_inline __itt_caller __kmp_itt_stack_caller_create(void);
__kmp_inline void __kmp_itt_stack_caller_destroy(__itt_caller);
__kmp_inline void __kmp_itt_stack_callee_enter(__itt_caller);
__kmp_inline void __kmp_itt_stack_callee_leave(__itt_caller);

// -----------------------------------------------------------------------------
// Old stuff for reporting low-level internal synchronization.

#if USE_ITT_NOTIFY

/* Support for SSC marks, which are used by SDE
   http://software.intel.com/en-us/articles/intel-software-development-emulator
   to mark points in instruction traces that represent spin-loops and are
   therefore uninteresting when collecting traces for architecture simulation.
 */
#ifndef INCLUDE_SSC_MARKS
#define INCLUDE_SSC_MARKS (KMP_OS_LINUX && KMP_ARCH_X86_64)
#endif

/* Linux 64 only for now */
#if (INCLUDE_SSC_MARKS && KMP_OS_LINUX && KMP_ARCH_X86_64)
// Portable (at least for gcc and icc) code to insert the necessary instructions
// to set %ebx and execute the unlikely no-op.
#if defined(__INTEL_COMPILER)
#define INSERT_SSC_MARK(tag) __SSC_MARK(tag)
#else
#define INSERT_SSC_MARK(tag)                                                   \
  __asm__ __volatile__("movl %0, %%ebx; .byte 0x64, 0x67, 0x90 " ::"i"(tag)    \
                       : "%ebx")
#endif
#else
#define INSERT_SSC_MARK(tag) ((void)0)
#endif

/* Markers for the start and end of regions that represent polling and are
   therefore uninteresting to architectural simulations 0x4376 and 0x4377 are
   arbitrary numbers that should be unique in the space of SSC tags, but there
   is no central issuing authority rather randomness is expected to work. */
#define SSC_MARK_SPIN_START() INSERT_SSC_MARK(0x4376)
#define SSC_MARK_SPIN_END() INSERT_SSC_MARK(0x4377)

// Markers for architecture simulation.
// FORKING      : Before the master thread forks.
// JOINING      : At the start of the join.
// INVOKING     : Before the threads invoke microtasks.
// DISPATCH_INIT: At the start of dynamically scheduled loop.
// DISPATCH_NEXT: After claming next iteration of dynamically scheduled loop.
#define SSC_MARK_FORKING() INSERT_SSC_MARK(0xd693)
#define SSC_MARK_JOINING() INSERT_SSC_MARK(0xd694)
#define SSC_MARK_INVOKING() INSERT_SSC_MARK(0xd695)
#define SSC_MARK_DISPATCH_INIT() INSERT_SSC_MARK(0xd696)
#define SSC_MARK_DISPATCH_NEXT() INSERT_SSC_MARK(0xd697)

// The object is an address that associates a specific set of the prepare,
// acquire, release, and cancel operations.

/* Sync prepare indicates a thread is going to start waiting for another thread
   to send a release event.  This operation should be done just before the
   thread begins checking for the existence of the release event */

/* Sync cancel indicates a thread is cancelling a wait on another thread and
   continuing execution without waiting for the other thread to release it */

/* Sync acquired indicates a thread has received a release event from another
   thread and has stopped waiting.  This operation must occur only after the
   release event is received. */

/* Sync release indicates a thread is going to send a release event to another
   thread so it will stop waiting and continue execution. This operation must
   just happen before the release event. */

#define KMP_FSYNC_PREPARE(obj) __itt_fsync_prepare((void *)(obj))
#define KMP_FSYNC_CANCEL(obj) __itt_fsync_cancel((void *)(obj))
#define KMP_FSYNC_ACQUIRED(obj) __itt_fsync_acquired((void *)(obj))
#define KMP_FSYNC_RELEASING(obj) __itt_fsync_releasing((void *)(obj))

/* In case of waiting in a spin loop, ITT wants KMP_FSYNC_PREPARE() to be called
   with a delay (and not called at all if waiting time is small). So, in spin
   loops, do not use KMP_FSYNC_PREPARE(), but use KMP_FSYNC_SPIN_INIT() (before
   spin loop), KMP_FSYNC_SPIN_PREPARE() (whithin the spin loop), and
   KMP_FSYNC_SPIN_ACQUIRED(). See KMP_WAIT_YIELD() for example. */

#undef KMP_FSYNC_SPIN_INIT
#define KMP_FSYNC_SPIN_INIT(obj, spin)                                         \
  int sync_iters = 0;                                                          \
  if (__itt_fsync_prepare_ptr) {                                               \
    if (obj == NULL) {                                                         \
      obj = spin;                                                              \
    } /* if */                                                                 \
  } /* if */                                                                   \
  SSC_MARK_SPIN_START()

#undef KMP_FSYNC_SPIN_PREPARE
#define KMP_FSYNC_SPIN_PREPARE(obj)                                            \
  do {                                                                         \
    if (__itt_fsync_prepare_ptr && sync_iters < __kmp_itt_prepare_delay) {     \
      ++sync_iters;                                                            \
      if (sync_iters >= __kmp_itt_prepare_delay) {                             \
        KMP_FSYNC_PREPARE((void *)obj);                                        \
      } /* if */                                                               \
    } /* if */                                                                 \
  } while (0)
#undef KMP_FSYNC_SPIN_ACQUIRED
#define KMP_FSYNC_SPIN_ACQUIRED(obj)                                           \
  do {                                                                         \
    SSC_MARK_SPIN_END();                                                       \
    if (sync_iters >= __kmp_itt_prepare_delay) {                               \
      KMP_FSYNC_ACQUIRED((void *)obj);                                         \
    } /* if */                                                                 \
  } while (0)

/* ITT will not report objects created within KMP_ITT_IGNORE(), e. g.:
       KMP_ITT_IGNORE(
           ptr = malloc( size );
       );
*/
#define KMP_ITT_IGNORE(statement)                                              \
  do {                                                                         \
    __itt_state_t __itt_state_;                                                \
    if (__itt_state_get_ptr) {                                                 \
      __itt_state_ = __itt_state_get();                                        \
      __itt_obj_mode_set(__itt_obj_prop_ignore, __itt_obj_state_set);          \
    } /* if */                                                                 \
    { statement }                                                              \
    if (__itt_state_get_ptr) {                                                 \
      __itt_state_set(__itt_state_);                                           \
    } /* if */                                                                 \
  } while (0)

const int KMP_MAX_FRAME_DOMAINS =
    512; // Maximum number of frame domains to use (maps to
// different OpenMP regions in the user source code).
extern kmp_int32 __kmp_barrier_domain_count;
extern kmp_int32 __kmp_region_domain_count;
extern __itt_domain *__kmp_itt_barrier_domains[KMP_MAX_FRAME_DOMAINS];
extern __itt_domain *__kmp_itt_region_domains[KMP_MAX_FRAME_DOMAINS];
extern __itt_domain *__kmp_itt_imbalance_domains[KMP_MAX_FRAME_DOMAINS];
extern kmp_int32 __kmp_itt_region_team_size[KMP_MAX_FRAME_DOMAINS];
extern __itt_domain *metadata_domain;
extern __itt_string_handle *string_handle_imbl;
extern __itt_string_handle *string_handle_loop;
extern __itt_string_handle *string_handle_sngl;

#else

// Null definitions of the synchronization tracing functions.
#define KMP_FSYNC_PREPARE(obj) ((void)0)
#define KMP_FSYNC_CANCEL(obj) ((void)0)
#define KMP_FSYNC_ACQUIRED(obj) ((void)0)
#define KMP_FSYNC_RELEASING(obj) ((void)0)

#define KMP_FSYNC_SPIN_INIT(obj, spin) ((void)0)
#define KMP_FSYNC_SPIN_PREPARE(obj) ((void)0)
#define KMP_FSYNC_SPIN_ACQUIRED(obj) ((void)0)

#define KMP_ITT_IGNORE(stmt)                                                   \
  do {                                                                         \
    stmt                                                                       \
  } while (0)

#endif // USE_ITT_NOTIFY

#if !KMP_DEBUG
// In release mode include definitions of inline functions.
#include "kmp_itt.inl"
#endif

#endif // KMP_ITT_H

#else /* USE_ITT_BUILD */

// Null definitions of the synchronization tracing functions.
// If USE_ITT_BULID is not enabled, USE_ITT_NOTIFY cannot be either.
// By defining these we avoid unpleasant ifdef tests in many places.
#define KMP_FSYNC_PREPARE(obj) ((void)0)
#define KMP_FSYNC_CANCEL(obj) ((void)0)
#define KMP_FSYNC_ACQUIRED(obj) ((void)0)
#define KMP_FSYNC_RELEASING(obj) ((void)0)

#define KMP_FSYNC_SPIN_INIT(obj, spin) ((void)0)
#define KMP_FSYNC_SPIN_PREPARE(obj) ((void)0)
#define KMP_FSYNC_SPIN_ACQUIRED(obj) ((void)0)

#define KMP_ITT_IGNORE(stmt)                                                   \
  do {                                                                         \
    stmt                                                                       \
  } while (0)

#define USE_ITT_BUILD_ARG(x)

#endif /* USE_ITT_BUILD */
