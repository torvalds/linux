/*
 * kmp_runtime.cpp -- KPTS runtime support library
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
#include "kmp_affinity.h"
#include "kmp_atomic.h"
#include "kmp_environment.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_io.h"
#include "kmp_itt.h"
#include "kmp_settings.h"
#include "kmp_stats.h"
#include "kmp_str.h"
#include "kmp_wait_release.h"
#include "kmp_wrapper_getpid.h"
#include "kmp_dispatch.h"
#if KMP_USE_HIER_SCHED
#include "kmp_dispatch_hier.h"
#endif

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

/* these are temporary issues to be dealt with */
#define KMP_USE_PRCTL 0

#if KMP_OS_WINDOWS
#include <process.h>
#endif

#include "tsan_annotations.h"

#if defined(KMP_GOMP_COMPAT)
char const __kmp_version_alt_comp[] =
    KMP_VERSION_PREFIX "alternative compiler support: yes";
#endif /* defined(KMP_GOMP_COMPAT) */

char const __kmp_version_omp_api[] = KMP_VERSION_PREFIX "API version: "
#if OMP_50_ENABLED
                                                        "5.0 (201611)";
#elif OMP_45_ENABLED
                                                        "4.5 (201511)";
#elif OMP_40_ENABLED
                                                        "4.0 (201307)";
#else
                                                        "3.1 (201107)";
#endif

#ifdef KMP_DEBUG
char const __kmp_version_lock[] =
    KMP_VERSION_PREFIX "lock type: run time selectable";
#endif /* KMP_DEBUG */

#define KMP_MIN(x, y) ((x) < (y) ? (x) : (y))

/* ------------------------------------------------------------------------ */

#if KMP_USE_MONITOR
kmp_info_t __kmp_monitor;
#endif

/* Forward declarations */

void __kmp_cleanup(void);

static void __kmp_initialize_info(kmp_info_t *, kmp_team_t *, int tid,
                                  int gtid);
static void __kmp_initialize_team(kmp_team_t *team, int new_nproc,
                                  kmp_internal_control_t *new_icvs,
                                  ident_t *loc);
#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
static void __kmp_partition_places(kmp_team_t *team,
                                   int update_master_only = 0);
#endif
static void __kmp_do_serial_initialize(void);
void __kmp_fork_barrier(int gtid, int tid);
void __kmp_join_barrier(int gtid);
void __kmp_setup_icv_copy(kmp_team_t *team, int new_nproc,
                          kmp_internal_control_t *new_icvs, ident_t *loc);

#ifdef USE_LOAD_BALANCE
static int __kmp_load_balance_nproc(kmp_root_t *root, int set_nproc);
#endif

static int __kmp_expand_threads(int nNeed);
#if KMP_OS_WINDOWS
static int __kmp_unregister_root_other_thread(int gtid);
#endif
static void __kmp_unregister_library(void); // called by __kmp_internal_end()
static void __kmp_reap_thread(kmp_info_t *thread, int is_root);
kmp_info_t *__kmp_thread_pool_insert_pt = NULL;

/* Calculate the identifier of the current thread */
/* fast (and somewhat portable) way to get unique identifier of executing
   thread. Returns KMP_GTID_DNE if we haven't been assigned a gtid. */
int __kmp_get_global_thread_id() {
  int i;
  kmp_info_t **other_threads;
  size_t stack_data;
  char *stack_addr;
  size_t stack_size;
  char *stack_base;

  KA_TRACE(
      1000,
      ("*** __kmp_get_global_thread_id: entering, nproc=%d  all_nproc=%d\n",
       __kmp_nth, __kmp_all_nth));

  /* JPH - to handle the case where __kmpc_end(0) is called immediately prior to
     a parallel region, made it return KMP_GTID_DNE to force serial_initialize
     by caller. Had to handle KMP_GTID_DNE at all call-sites, or else guarantee
     __kmp_init_gtid for this to work. */

  if (!TCR_4(__kmp_init_gtid))
    return KMP_GTID_DNE;

#ifdef KMP_TDATA_GTID
  if (TCR_4(__kmp_gtid_mode) >= 3) {
    KA_TRACE(1000, ("*** __kmp_get_global_thread_id: using TDATA\n"));
    return __kmp_gtid;
  }
#endif
  if (TCR_4(__kmp_gtid_mode) >= 2) {
    KA_TRACE(1000, ("*** __kmp_get_global_thread_id: using keyed TLS\n"));
    return __kmp_gtid_get_specific();
  }
  KA_TRACE(1000, ("*** __kmp_get_global_thread_id: using internal alg.\n"));

  stack_addr = (char *)&stack_data;
  other_threads = __kmp_threads;

  /* ATT: The code below is a source of potential bugs due to unsynchronized
     access to __kmp_threads array. For example:
     1. Current thread loads other_threads[i] to thr and checks it, it is
        non-NULL.
     2. Current thread is suspended by OS.
     3. Another thread unregisters and finishes (debug versions of free()
        may fill memory with something like 0xEF).
     4. Current thread is resumed.
     5. Current thread reads junk from *thr.
     TODO: Fix it.  --ln  */

  for (i = 0; i < __kmp_threads_capacity; i++) {

    kmp_info_t *thr = (kmp_info_t *)TCR_SYNC_PTR(other_threads[i]);
    if (!thr)
      continue;

    stack_size = (size_t)TCR_PTR(thr->th.th_info.ds.ds_stacksize);
    stack_base = (char *)TCR_PTR(thr->th.th_info.ds.ds_stackbase);

    /* stack grows down -- search through all of the active threads */

    if (stack_addr <= stack_base) {
      size_t stack_diff = stack_base - stack_addr;

      if (stack_diff <= stack_size) {
        /* The only way we can be closer than the allocated */
        /* stack size is if we are running on this thread. */
        KMP_DEBUG_ASSERT(__kmp_gtid_get_specific() == i);
        return i;
      }
    }
  }

  /* get specific to try and determine our gtid */
  KA_TRACE(1000,
           ("*** __kmp_get_global_thread_id: internal alg. failed to find "
            "thread, using TLS\n"));
  i = __kmp_gtid_get_specific();

  /*fprintf( stderr, "=== %d\n", i );  */ /* GROO */

  /* if we havn't been assigned a gtid, then return code */
  if (i < 0)
    return i;

  /* dynamically updated stack window for uber threads to avoid get_specific
     call */
  if (!TCR_4(other_threads[i]->th.th_info.ds.ds_stackgrow)) {
    KMP_FATAL(StackOverflow, i);
  }

  stack_base = (char *)other_threads[i]->th.th_info.ds.ds_stackbase;
  if (stack_addr > stack_base) {
    TCW_PTR(other_threads[i]->th.th_info.ds.ds_stackbase, stack_addr);
    TCW_PTR(other_threads[i]->th.th_info.ds.ds_stacksize,
            other_threads[i]->th.th_info.ds.ds_stacksize + stack_addr -
                stack_base);
  } else {
    TCW_PTR(other_threads[i]->th.th_info.ds.ds_stacksize,
            stack_base - stack_addr);
  }

  /* Reprint stack bounds for ubermaster since they have been refined */
  if (__kmp_storage_map) {
    char *stack_end = (char *)other_threads[i]->th.th_info.ds.ds_stackbase;
    char *stack_beg = stack_end - other_threads[i]->th.th_info.ds.ds_stacksize;
    __kmp_print_storage_map_gtid(i, stack_beg, stack_end,
                                 other_threads[i]->th.th_info.ds.ds_stacksize,
                                 "th_%d stack (refinement)", i);
  }
  return i;
}

int __kmp_get_global_thread_id_reg() {
  int gtid;

  if (!__kmp_init_serial) {
    gtid = KMP_GTID_DNE;
  } else
#ifdef KMP_TDATA_GTID
      if (TCR_4(__kmp_gtid_mode) >= 3) {
    KA_TRACE(1000, ("*** __kmp_get_global_thread_id_reg: using TDATA\n"));
    gtid = __kmp_gtid;
  } else
#endif
      if (TCR_4(__kmp_gtid_mode) >= 2) {
    KA_TRACE(1000, ("*** __kmp_get_global_thread_id_reg: using keyed TLS\n"));
    gtid = __kmp_gtid_get_specific();
  } else {
    KA_TRACE(1000,
             ("*** __kmp_get_global_thread_id_reg: using internal alg.\n"));
    gtid = __kmp_get_global_thread_id();
  }

  /* we must be a new uber master sibling thread */
  if (gtid == KMP_GTID_DNE) {
    KA_TRACE(10,
             ("__kmp_get_global_thread_id_reg: Encountered new root thread. "
              "Registering a new gtid.\n"));
    __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);
    if (!__kmp_init_serial) {
      __kmp_do_serial_initialize();
      gtid = __kmp_gtid_get_specific();
    } else {
      gtid = __kmp_register_root(FALSE);
    }
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    /*__kmp_printf( "+++ %d\n", gtid ); */ /* GROO */
  }

  KMP_DEBUG_ASSERT(gtid >= 0);

  return gtid;
}

/* caller must hold forkjoin_lock */
void __kmp_check_stack_overlap(kmp_info_t *th) {
  int f;
  char *stack_beg = NULL;
  char *stack_end = NULL;
  int gtid;

  KA_TRACE(10, ("__kmp_check_stack_overlap: called\n"));
  if (__kmp_storage_map) {
    stack_end = (char *)th->th.th_info.ds.ds_stackbase;
    stack_beg = stack_end - th->th.th_info.ds.ds_stacksize;

    gtid = __kmp_gtid_from_thread(th);

    if (gtid == KMP_GTID_MONITOR) {
      __kmp_print_storage_map_gtid(
          gtid, stack_beg, stack_end, th->th.th_info.ds.ds_stacksize,
          "th_%s stack (%s)", "mon",
          (th->th.th_info.ds.ds_stackgrow) ? "initial" : "actual");
    } else {
      __kmp_print_storage_map_gtid(
          gtid, stack_beg, stack_end, th->th.th_info.ds.ds_stacksize,
          "th_%d stack (%s)", gtid,
          (th->th.th_info.ds.ds_stackgrow) ? "initial" : "actual");
    }
  }

  /* No point in checking ubermaster threads since they use refinement and
   * cannot overlap */
  gtid = __kmp_gtid_from_thread(th);
  if (__kmp_env_checks == TRUE && !KMP_UBER_GTID(gtid)) {
    KA_TRACE(10,
             ("__kmp_check_stack_overlap: performing extensive checking\n"));
    if (stack_beg == NULL) {
      stack_end = (char *)th->th.th_info.ds.ds_stackbase;
      stack_beg = stack_end - th->th.th_info.ds.ds_stacksize;
    }

    for (f = 0; f < __kmp_threads_capacity; f++) {
      kmp_info_t *f_th = (kmp_info_t *)TCR_SYNC_PTR(__kmp_threads[f]);

      if (f_th && f_th != th) {
        char *other_stack_end =
            (char *)TCR_PTR(f_th->th.th_info.ds.ds_stackbase);
        char *other_stack_beg =
            other_stack_end - (size_t)TCR_PTR(f_th->th.th_info.ds.ds_stacksize);
        if ((stack_beg > other_stack_beg && stack_beg < other_stack_end) ||
            (stack_end > other_stack_beg && stack_end < other_stack_end)) {

          /* Print the other stack values before the abort */
          if (__kmp_storage_map)
            __kmp_print_storage_map_gtid(
                -1, other_stack_beg, other_stack_end,
                (size_t)TCR_PTR(f_th->th.th_info.ds.ds_stacksize),
                "th_%d stack (overlapped)", __kmp_gtid_from_thread(f_th));

          __kmp_fatal(KMP_MSG(StackOverlap), KMP_HNT(ChangeStackLimit),
                      __kmp_msg_null);
        }
      }
    }
  }
  KA_TRACE(10, ("__kmp_check_stack_overlap: returning\n"));
}

/* ------------------------------------------------------------------------ */

void __kmp_infinite_loop(void) {
  static int done = FALSE;

  while (!done) {
    KMP_YIELD(1);
  }
}

#define MAX_MESSAGE 512

void __kmp_print_storage_map_gtid(int gtid, void *p1, void *p2, size_t size,
                                  char const *format, ...) {
  char buffer[MAX_MESSAGE];
  va_list ap;

  va_start(ap, format);
  KMP_SNPRINTF(buffer, sizeof(buffer), "OMP storage map: %p %p%8lu %s\n", p1,
               p2, (unsigned long)size, format);
  __kmp_acquire_bootstrap_lock(&__kmp_stdio_lock);
  __kmp_vprintf(kmp_err, buffer, ap);
#if KMP_PRINT_DATA_PLACEMENT
  int node;
  if (gtid >= 0) {
    if (p1 <= p2 && (char *)p2 - (char *)p1 == size) {
      if (__kmp_storage_map_verbose) {
        node = __kmp_get_host_node(p1);
        if (node < 0) /* doesn't work, so don't try this next time */
          __kmp_storage_map_verbose = FALSE;
        else {
          char *last;
          int lastNode;
          int localProc = __kmp_get_cpu_from_gtid(gtid);

          const int page_size = KMP_GET_PAGE_SIZE();

          p1 = (void *)((size_t)p1 & ~((size_t)page_size - 1));
          p2 = (void *)(((size_t)p2 - 1) & ~((size_t)page_size - 1));
          if (localProc >= 0)
            __kmp_printf_no_lock("  GTID %d localNode %d\n", gtid,
                                 localProc >> 1);
          else
            __kmp_printf_no_lock("  GTID %d\n", gtid);
#if KMP_USE_PRCTL
          /* The more elaborate format is disabled for now because of the prctl
           * hanging bug. */
          do {
            last = p1;
            lastNode = node;
            /* This loop collates adjacent pages with the same host node. */
            do {
              (char *)p1 += page_size;
            } while (p1 <= p2 && (node = __kmp_get_host_node(p1)) == lastNode);
            __kmp_printf_no_lock("    %p-%p memNode %d\n", last, (char *)p1 - 1,
                                 lastNode);
          } while (p1 <= p2);
#else
          __kmp_printf_no_lock("    %p-%p memNode %d\n", p1,
                               (char *)p1 + (page_size - 1),
                               __kmp_get_host_node(p1));
          if (p1 < p2) {
            __kmp_printf_no_lock("    %p-%p memNode %d\n", p2,
                                 (char *)p2 + (page_size - 1),
                                 __kmp_get_host_node(p2));
          }
#endif
        }
      }
    } else
      __kmp_printf_no_lock("  %s\n", KMP_I18N_STR(StorageMapWarning));
  }
#endif /* KMP_PRINT_DATA_PLACEMENT */
  __kmp_release_bootstrap_lock(&__kmp_stdio_lock);
}

void __kmp_warn(char const *format, ...) {
  char buffer[MAX_MESSAGE];
  va_list ap;

  if (__kmp_generate_warnings == kmp_warnings_off) {
    return;
  }

  va_start(ap, format);

  KMP_SNPRINTF(buffer, sizeof(buffer), "OMP warning: %s\n", format);
  __kmp_acquire_bootstrap_lock(&__kmp_stdio_lock);
  __kmp_vprintf(kmp_err, buffer, ap);
  __kmp_release_bootstrap_lock(&__kmp_stdio_lock);

  va_end(ap);
}

void __kmp_abort_process() {
  // Later threads may stall here, but that's ok because abort() will kill them.
  __kmp_acquire_bootstrap_lock(&__kmp_exit_lock);

  if (__kmp_debug_buf) {
    __kmp_dump_debug_buffer();
  }

  if (KMP_OS_WINDOWS) {
    // Let other threads know of abnormal termination and prevent deadlock
    // if abort happened during library initialization or shutdown
    __kmp_global.g.g_abort = SIGABRT;

    /* On Windows* OS by default abort() causes pop-up error box, which stalls
       nightly testing. Unfortunately, we cannot reliably suppress pop-up error
       boxes. _set_abort_behavior() works well, but this function is not
       available in VS7 (this is not problem for DLL, but it is a problem for
       static OpenMP RTL). SetErrorMode (and so, timelimit utility) does not
       help, at least in some versions of MS C RTL.

       It seems following sequence is the only way to simulate abort() and
       avoid pop-up error box. */
    raise(SIGABRT);
    _exit(3); // Just in case, if signal ignored, exit anyway.
  } else {
    abort();
  }

  __kmp_infinite_loop();
  __kmp_release_bootstrap_lock(&__kmp_exit_lock);

} // __kmp_abort_process

void __kmp_abort_thread(void) {
  // TODO: Eliminate g_abort global variable and this function.
  // In case of abort just call abort(), it will kill all the threads.
  __kmp_infinite_loop();
} // __kmp_abort_thread

/* Print out the storage map for the major kmp_info_t thread data structures
   that are allocated together. */

static void __kmp_print_thread_storage_map(kmp_info_t *thr, int gtid) {
  __kmp_print_storage_map_gtid(gtid, thr, thr + 1, sizeof(kmp_info_t), "th_%d",
                               gtid);

  __kmp_print_storage_map_gtid(gtid, &thr->th.th_info, &thr->th.th_team,
                               sizeof(kmp_desc_t), "th_%d.th_info", gtid);

  __kmp_print_storage_map_gtid(gtid, &thr->th.th_local, &thr->th.th_pri_head,
                               sizeof(kmp_local_t), "th_%d.th_local", gtid);

  __kmp_print_storage_map_gtid(
      gtid, &thr->th.th_bar[0], &thr->th.th_bar[bs_last_barrier],
      sizeof(kmp_balign_t) * bs_last_barrier, "th_%d.th_bar", gtid);

  __kmp_print_storage_map_gtid(gtid, &thr->th.th_bar[bs_plain_barrier],
                               &thr->th.th_bar[bs_plain_barrier + 1],
                               sizeof(kmp_balign_t), "th_%d.th_bar[plain]",
                               gtid);

  __kmp_print_storage_map_gtid(gtid, &thr->th.th_bar[bs_forkjoin_barrier],
                               &thr->th.th_bar[bs_forkjoin_barrier + 1],
                               sizeof(kmp_balign_t), "th_%d.th_bar[forkjoin]",
                               gtid);

#if KMP_FAST_REDUCTION_BARRIER
  __kmp_print_storage_map_gtid(gtid, &thr->th.th_bar[bs_reduction_barrier],
                               &thr->th.th_bar[bs_reduction_barrier + 1],
                               sizeof(kmp_balign_t), "th_%d.th_bar[reduction]",
                               gtid);
#endif // KMP_FAST_REDUCTION_BARRIER
}

/* Print out the storage map for the major kmp_team_t team data structures
   that are allocated together. */

static void __kmp_print_team_storage_map(const char *header, kmp_team_t *team,
                                         int team_id, int num_thr) {
  int num_disp_buff = team->t.t_max_nproc > 1 ? __kmp_dispatch_num_buffers : 2;
  __kmp_print_storage_map_gtid(-1, team, team + 1, sizeof(kmp_team_t), "%s_%d",
                               header, team_id);

  __kmp_print_storage_map_gtid(-1, &team->t.t_bar[0],
                               &team->t.t_bar[bs_last_barrier],
                               sizeof(kmp_balign_team_t) * bs_last_barrier,
                               "%s_%d.t_bar", header, team_id);

  __kmp_print_storage_map_gtid(-1, &team->t.t_bar[bs_plain_barrier],
                               &team->t.t_bar[bs_plain_barrier + 1],
                               sizeof(kmp_balign_team_t), "%s_%d.t_bar[plain]",
                               header, team_id);

  __kmp_print_storage_map_gtid(-1, &team->t.t_bar[bs_forkjoin_barrier],
                               &team->t.t_bar[bs_forkjoin_barrier + 1],
                               sizeof(kmp_balign_team_t),
                               "%s_%d.t_bar[forkjoin]", header, team_id);

#if KMP_FAST_REDUCTION_BARRIER
  __kmp_print_storage_map_gtid(-1, &team->t.t_bar[bs_reduction_barrier],
                               &team->t.t_bar[bs_reduction_barrier + 1],
                               sizeof(kmp_balign_team_t),
                               "%s_%d.t_bar[reduction]", header, team_id);
#endif // KMP_FAST_REDUCTION_BARRIER

  __kmp_print_storage_map_gtid(
      -1, &team->t.t_dispatch[0], &team->t.t_dispatch[num_thr],
      sizeof(kmp_disp_t) * num_thr, "%s_%d.t_dispatch", header, team_id);

  __kmp_print_storage_map_gtid(
      -1, &team->t.t_threads[0], &team->t.t_threads[num_thr],
      sizeof(kmp_info_t *) * num_thr, "%s_%d.t_threads", header, team_id);

  __kmp_print_storage_map_gtid(-1, &team->t.t_disp_buffer[0],
                               &team->t.t_disp_buffer[num_disp_buff],
                               sizeof(dispatch_shared_info_t) * num_disp_buff,
                               "%s_%d.t_disp_buffer", header, team_id);

  __kmp_print_storage_map_gtid(-1, &team->t.t_taskq, &team->t.t_copypriv_data,
                               sizeof(kmp_taskq_t), "%s_%d.t_taskq", header,
                               team_id);
}

static void __kmp_init_allocator() {
#if OMP_50_ENABLED
  __kmp_init_memkind();
#endif
}
static void __kmp_fini_allocator() {
#if OMP_50_ENABLED
  __kmp_fini_memkind();
#endif
}

/* ------------------------------------------------------------------------ */

#if KMP_DYNAMIC_LIB
#if KMP_OS_WINDOWS

static void __kmp_reset_lock(kmp_bootstrap_lock_t *lck) {
  // TODO: Change to __kmp_break_bootstrap_lock().
  __kmp_init_bootstrap_lock(lck); // make the lock released
}

static void __kmp_reset_locks_on_process_detach(int gtid_req) {
  int i;
  int thread_count;

  // PROCESS_DETACH is expected to be called by a thread that executes
  // ProcessExit() or FreeLibrary(). OS terminates other threads (except the one
  // calling ProcessExit or FreeLibrary). So, it might be safe to access the
  // __kmp_threads[] without taking the forkjoin_lock. However, in fact, some
  // threads can be still alive here, although being about to be terminated. The
  // threads in the array with ds_thread==0 are most suspicious. Actually, it
  // can be not safe to access the __kmp_threads[].

  // TODO: does it make sense to check __kmp_roots[] ?

  // Let's check that there are no other alive threads registered with the OMP
  // lib.
  while (1) {
    thread_count = 0;
    for (i = 0; i < __kmp_threads_capacity; ++i) {
      if (!__kmp_threads)
        continue;
      kmp_info_t *th = __kmp_threads[i];
      if (th == NULL)
        continue;
      int gtid = th->th.th_info.ds.ds_gtid;
      if (gtid == gtid_req)
        continue;
      if (gtid < 0)
        continue;
      DWORD exit_val;
      int alive = __kmp_is_thread_alive(th, &exit_val);
      if (alive) {
        ++thread_count;
      }
    }
    if (thread_count == 0)
      break; // success
  }

  // Assume that I'm alone. Now it might be safe to check and reset locks.
  // __kmp_forkjoin_lock and __kmp_stdio_lock are expected to be reset.
  __kmp_reset_lock(&__kmp_forkjoin_lock);
#ifdef KMP_DEBUG
  __kmp_reset_lock(&__kmp_stdio_lock);
#endif // KMP_DEBUG
}

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID lpReserved) {
  //__kmp_acquire_bootstrap_lock( &__kmp_initz_lock );

  switch (fdwReason) {

  case DLL_PROCESS_ATTACH:
    KA_TRACE(10, ("DllMain: PROCESS_ATTACH\n"));

    return TRUE;

  case DLL_PROCESS_DETACH:
    KA_TRACE(10, ("DllMain: PROCESS_DETACH T#%d\n", __kmp_gtid_get_specific()));

    if (lpReserved != NULL) {
      // lpReserved is used for telling the difference:
      //   lpReserved == NULL when FreeLibrary() was called,
      //   lpReserved != NULL when the process terminates.
      // When FreeLibrary() is called, worker threads remain alive. So they will
      // release the forkjoin lock by themselves. When the process terminates,
      // worker threads disappear triggering the problem of unreleased forkjoin
      // lock as described below.

      // A worker thread can take the forkjoin lock. The problem comes up if
      // that worker thread becomes dead before it releases the forkjoin lock.
      // The forkjoin lock remains taken, while the thread executing
      // DllMain()->PROCESS_DETACH->__kmp_internal_end_library() below will try
      // to take the forkjoin lock and will always fail, so that the application
      // will never finish [normally]. This scenario is possible if
      // __kmpc_end() has not been executed. It looks like it's not a corner
      // case, but common cases:
      // - the main function was compiled by an alternative compiler;
      // - the main function was compiled by icl but without /Qopenmp
      //   (application with plugins);
      // - application terminates by calling C exit(), Fortran CALL EXIT() or
      //   Fortran STOP.
      // - alive foreign thread prevented __kmpc_end from doing cleanup.
      //
      // This is a hack to work around the problem.
      // TODO: !!! figure out something better.
      __kmp_reset_locks_on_process_detach(__kmp_gtid_get_specific());
    }

    __kmp_internal_end_library(__kmp_gtid_get_specific());

    return TRUE;

  case DLL_THREAD_ATTACH:
    KA_TRACE(10, ("DllMain: THREAD_ATTACH\n"));

    /* if we want to register new siblings all the time here call
     * __kmp_get_gtid(); */
    return TRUE;

  case DLL_THREAD_DETACH:
    KA_TRACE(10, ("DllMain: THREAD_DETACH T#%d\n", __kmp_gtid_get_specific()));

    __kmp_internal_end_thread(__kmp_gtid_get_specific());
    return TRUE;
  }

  return TRUE;
}

#endif /* KMP_OS_WINDOWS */
#endif /* KMP_DYNAMIC_LIB */

/* Change the library type to "status" and return the old type */
/* called from within initialization routines where __kmp_initz_lock is held */
int __kmp_change_library(int status) {
  int old_status;

  old_status = __kmp_yield_init &
               1; // check whether KMP_LIBRARY=throughput (even init count)

  if (status) {
    __kmp_yield_init |= 1; // throughput => turnaround (odd init count)
  } else {
    __kmp_yield_init &= ~1; // turnaround => throughput (even init count)
  }

  return old_status; // return previous setting of whether
  // KMP_LIBRARY=throughput
}

/* __kmp_parallel_deo -- Wait until it's our turn. */
void __kmp_parallel_deo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  int gtid = *gtid_ref;
#ifdef BUILD_PARALLEL_ORDERED
  kmp_team_t *team = __kmp_team_from_gtid(gtid);
#endif /* BUILD_PARALLEL_ORDERED */

  if (__kmp_env_consistency_check) {
    if (__kmp_threads[gtid]->th.th_root->r.r_active)
#if KMP_USE_DYNAMIC_LOCK
      __kmp_push_sync(gtid, ct_ordered_in_parallel, loc_ref, NULL, 0);
#else
      __kmp_push_sync(gtid, ct_ordered_in_parallel, loc_ref, NULL);
#endif
  }
#ifdef BUILD_PARALLEL_ORDERED
  if (!team->t.t_serialized) {
    KMP_MB();
    KMP_WAIT_YIELD(&team->t.t_ordered.dt.t_value, __kmp_tid_from_gtid(gtid),
                   KMP_EQ, NULL);
    KMP_MB();
  }
#endif /* BUILD_PARALLEL_ORDERED */
}

/* __kmp_parallel_dxo -- Signal the next task. */
void __kmp_parallel_dxo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  int gtid = *gtid_ref;
#ifdef BUILD_PARALLEL_ORDERED
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_team_t *team = __kmp_team_from_gtid(gtid);
#endif /* BUILD_PARALLEL_ORDERED */

  if (__kmp_env_consistency_check) {
    if (__kmp_threads[gtid]->th.th_root->r.r_active)
      __kmp_pop_sync(gtid, ct_ordered_in_parallel, loc_ref);
  }
#ifdef BUILD_PARALLEL_ORDERED
  if (!team->t.t_serialized) {
    KMP_MB(); /* Flush all pending memory write invalidates.  */

    /* use the tid of the next thread in this team */
    /* TODO replace with general release procedure */
    team->t.t_ordered.dt.t_value = ((tid + 1) % team->t.t_nproc);

    KMP_MB(); /* Flush all pending memory write invalidates.  */
  }
#endif /* BUILD_PARALLEL_ORDERED */
}

/* ------------------------------------------------------------------------ */
/* The BARRIER for a SINGLE process section is always explicit   */

int __kmp_enter_single(int gtid, ident_t *id_ref, int push_ws) {
  int status;
  kmp_info_t *th;
  kmp_team_t *team;

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  th = __kmp_threads[gtid];
  team = th->th.th_team;
  status = 0;

  th->th.th_ident = id_ref;

  if (team->t.t_serialized) {
    status = 1;
  } else {
    kmp_int32 old_this = th->th.th_local.this_construct;

    ++th->th.th_local.this_construct;
    /* try to set team count to thread count--success means thread got the
       single block */
    /* TODO: Should this be acquire or release? */
    if (team->t.t_construct == old_this) {
      status = __kmp_atomic_compare_store_acq(&team->t.t_construct, old_this,
                                              th->th.th_local.this_construct);
    }
#if USE_ITT_BUILD
    if (__itt_metadata_add_ptr && __kmp_forkjoin_frames_mode == 3 &&
        KMP_MASTER_GTID(gtid) &&
#if OMP_40_ENABLED
        th->th.th_teams_microtask == NULL &&
#endif
        team->t.t_active_level ==
            1) { // Only report metadata by master of active team at level 1
      __kmp_itt_metadata_single(id_ref);
    }
#endif /* USE_ITT_BUILD */
  }

  if (__kmp_env_consistency_check) {
    if (status && push_ws) {
      __kmp_push_workshare(gtid, ct_psingle, id_ref);
    } else {
      __kmp_check_workshare(gtid, ct_psingle, id_ref);
    }
  }
#if USE_ITT_BUILD
  if (status) {
    __kmp_itt_single_start(gtid);
  }
#endif /* USE_ITT_BUILD */
  return status;
}

void __kmp_exit_single(int gtid) {
#if USE_ITT_BUILD
  __kmp_itt_single_end(gtid);
#endif /* USE_ITT_BUILD */
  if (__kmp_env_consistency_check)
    __kmp_pop_workshare(gtid, ct_psingle, NULL);
}

/* determine if we can go parallel or must use a serialized parallel region and
 * how many threads we can use
 * set_nproc is the number of threads requested for the team
 * returns 0 if we should serialize or only use one thread,
 * otherwise the number of threads to use
 * The forkjoin lock is held by the caller. */
static int __kmp_reserve_threads(kmp_root_t *root, kmp_team_t *parent_team,
                                 int master_tid, int set_nthreads
#if OMP_40_ENABLED
                                 ,
                                 int enter_teams
#endif /* OMP_40_ENABLED */
                                 ) {
  int capacity;
  int new_nthreads;
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  KMP_DEBUG_ASSERT(root && parent_team);

  // If dyn-var is set, dynamically adjust the number of desired threads,
  // according to the method specified by dynamic_mode.
  new_nthreads = set_nthreads;
  if (!get__dynamic_2(parent_team, master_tid)) {
    ;
  }
#ifdef USE_LOAD_BALANCE
  else if (__kmp_global.g.g_dynamic_mode == dynamic_load_balance) {
    new_nthreads = __kmp_load_balance_nproc(root, set_nthreads);
    if (new_nthreads == 1) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d load balance reduced "
                    "reservation to 1 thread\n",
                    master_tid));
      return 1;
    }
    if (new_nthreads < set_nthreads) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d load balance reduced "
                    "reservation to %d threads\n",
                    master_tid, new_nthreads));
    }
  }
#endif /* USE_LOAD_BALANCE */
  else if (__kmp_global.g.g_dynamic_mode == dynamic_thread_limit) {
    new_nthreads = __kmp_avail_proc - __kmp_nth +
                   (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc);
    if (new_nthreads <= 1) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d thread limit reduced "
                    "reservation to 1 thread\n",
                    master_tid));
      return 1;
    }
    if (new_nthreads < set_nthreads) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d thread limit reduced "
                    "reservation to %d threads\n",
                    master_tid, new_nthreads));
    } else {
      new_nthreads = set_nthreads;
    }
  } else if (__kmp_global.g.g_dynamic_mode == dynamic_random) {
    if (set_nthreads > 2) {
      new_nthreads = __kmp_get_random(parent_team->t.t_threads[master_tid]);
      new_nthreads = (new_nthreads % set_nthreads) + 1;
      if (new_nthreads == 1) {
        KC_TRACE(10, ("__kmp_reserve_threads: T#%d dynamic random reduced "
                      "reservation to 1 thread\n",
                      master_tid));
        return 1;
      }
      if (new_nthreads < set_nthreads) {
        KC_TRACE(10, ("__kmp_reserve_threads: T#%d dynamic random reduced "
                      "reservation to %d threads\n",
                      master_tid, new_nthreads));
      }
    }
  } else {
    KMP_ASSERT(0);
  }

  // Respect KMP_ALL_THREADS/KMP_DEVICE_THREAD_LIMIT.
  if (__kmp_nth + new_nthreads -
          (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc) >
      __kmp_max_nth) {
    int tl_nthreads = __kmp_max_nth - __kmp_nth +
                      (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc);
    if (tl_nthreads <= 0) {
      tl_nthreads = 1;
    }

    // If dyn-var is false, emit a 1-time warning.
    if (!get__dynamic_2(parent_team, master_tid) && (!__kmp_reserve_warn)) {
      __kmp_reserve_warn = 1;
      __kmp_msg(kmp_ms_warning,
                KMP_MSG(CantFormThrTeam, set_nthreads, tl_nthreads),
                KMP_HNT(Unset_ALL_THREADS), __kmp_msg_null);
    }
    if (tl_nthreads == 1) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d KMP_DEVICE_THREAD_LIMIT "
                    "reduced reservation to 1 thread\n",
                    master_tid));
      return 1;
    }
    KC_TRACE(10, ("__kmp_reserve_threads: T#%d KMP_DEVICE_THREAD_LIMIT reduced "
                  "reservation to %d threads\n",
                  master_tid, tl_nthreads));
    new_nthreads = tl_nthreads;
  }

  // Respect OMP_THREAD_LIMIT
  if (root->r.r_cg_nthreads + new_nthreads -
          (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc) >
      __kmp_cg_max_nth) {
    int tl_nthreads = __kmp_cg_max_nth - root->r.r_cg_nthreads +
                      (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc);
    if (tl_nthreads <= 0) {
      tl_nthreads = 1;
    }

    // If dyn-var is false, emit a 1-time warning.
    if (!get__dynamic_2(parent_team, master_tid) && (!__kmp_reserve_warn)) {
      __kmp_reserve_warn = 1;
      __kmp_msg(kmp_ms_warning,
                KMP_MSG(CantFormThrTeam, set_nthreads, tl_nthreads),
                KMP_HNT(Unset_ALL_THREADS), __kmp_msg_null);
    }
    if (tl_nthreads == 1) {
      KC_TRACE(10, ("__kmp_reserve_threads: T#%d OMP_THREAD_LIMIT "
                    "reduced reservation to 1 thread\n",
                    master_tid));
      return 1;
    }
    KC_TRACE(10, ("__kmp_reserve_threads: T#%d OMP_THREAD_LIMIT reduced "
                  "reservation to %d threads\n",
                  master_tid, tl_nthreads));
    new_nthreads = tl_nthreads;
  }

  // Check if the threads array is large enough, or needs expanding.
  // See comment in __kmp_register_root() about the adjustment if
  // __kmp_threads[0] == NULL.
  capacity = __kmp_threads_capacity;
  if (TCR_PTR(__kmp_threads[0]) == NULL) {
    --capacity;
  }
  if (__kmp_nth + new_nthreads -
          (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc) >
      capacity) {
    // Expand the threads array.
    int slotsRequired = __kmp_nth + new_nthreads -
                        (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc) -
                        capacity;
    int slotsAdded = __kmp_expand_threads(slotsRequired);
    if (slotsAdded < slotsRequired) {
      // The threads array was not expanded enough.
      new_nthreads -= (slotsRequired - slotsAdded);
      KMP_ASSERT(new_nthreads >= 1);

      // If dyn-var is false, emit a 1-time warning.
      if (!get__dynamic_2(parent_team, master_tid) && (!__kmp_reserve_warn)) {
        __kmp_reserve_warn = 1;
        if (__kmp_tp_cached) {
          __kmp_msg(kmp_ms_warning,
                    KMP_MSG(CantFormThrTeam, set_nthreads, new_nthreads),
                    KMP_HNT(Set_ALL_THREADPRIVATE, __kmp_tp_capacity),
                    KMP_HNT(PossibleSystemLimitOnThreads), __kmp_msg_null);
        } else {
          __kmp_msg(kmp_ms_warning,
                    KMP_MSG(CantFormThrTeam, set_nthreads, new_nthreads),
                    KMP_HNT(SystemLimitOnThreads), __kmp_msg_null);
        }
      }
    }
  }

#ifdef KMP_DEBUG
  if (new_nthreads == 1) {
    KC_TRACE(10,
             ("__kmp_reserve_threads: T#%d serializing team after reclaiming "
              "dead roots and rechecking; requested %d threads\n",
              __kmp_get_gtid(), set_nthreads));
  } else {
    KC_TRACE(10, ("__kmp_reserve_threads: T#%d allocating %d threads; requested"
                  " %d threads\n",
                  __kmp_get_gtid(), new_nthreads, set_nthreads));
  }
#endif // KMP_DEBUG
  return new_nthreads;
}

/* Allocate threads from the thread pool and assign them to the new team. We are
   assured that there are enough threads available, because we checked on that
   earlier within critical section forkjoin */
static void __kmp_fork_team_threads(kmp_root_t *root, kmp_team_t *team,
                                    kmp_info_t *master_th, int master_gtid) {
  int i;
  int use_hot_team;

  KA_TRACE(10, ("__kmp_fork_team_threads: new_nprocs = %d\n", team->t.t_nproc));
  KMP_DEBUG_ASSERT(master_gtid == __kmp_get_gtid());
  KMP_MB();

  /* first, let's setup the master thread */
  master_th->th.th_info.ds.ds_tid = 0;
  master_th->th.th_team = team;
  master_th->th.th_team_nproc = team->t.t_nproc;
  master_th->th.th_team_master = master_th;
  master_th->th.th_team_serialized = FALSE;
  master_th->th.th_dispatch = &team->t.t_dispatch[0];

/* make sure we are not the optimized hot team */
#if KMP_NESTED_HOT_TEAMS
  use_hot_team = 0;
  kmp_hot_team_ptr_t *hot_teams = master_th->th.th_hot_teams;
  if (hot_teams) { // hot teams array is not allocated if
    // KMP_HOT_TEAMS_MAX_LEVEL=0
    int level = team->t.t_active_level - 1; // index in array of hot teams
    if (master_th->th.th_teams_microtask) { // are we inside the teams?
      if (master_th->th.th_teams_size.nteams > 1) {
        ++level; // level was not increased in teams construct for
        // team_of_masters
      }
      if (team->t.t_pkfn != (microtask_t)__kmp_teams_master &&
          master_th->th.th_teams_level == team->t.t_level) {
        ++level; // level was not increased in teams construct for
        // team_of_workers before the parallel
      } // team->t.t_level will be increased inside parallel
    }
    if (level < __kmp_hot_teams_max_level) {
      if (hot_teams[level].hot_team) {
        // hot team has already been allocated for given level
        KMP_DEBUG_ASSERT(hot_teams[level].hot_team == team);
        use_hot_team = 1; // the team is ready to use
      } else {
        use_hot_team = 0; // AC: threads are not allocated yet
        hot_teams[level].hot_team = team; // remember new hot team
        hot_teams[level].hot_team_nth = team->t.t_nproc;
      }
    } else {
      use_hot_team = 0;
    }
  }
#else
  use_hot_team = team == root->r.r_hot_team;
#endif
  if (!use_hot_team) {

    /* install the master thread */
    team->t.t_threads[0] = master_th;
    __kmp_initialize_info(master_th, team, 0, master_gtid);

    /* now, install the worker threads */
    for (i = 1; i < team->t.t_nproc; i++) {

      /* fork or reallocate a new thread and install it in team */
      kmp_info_t *thr = __kmp_allocate_thread(root, team, i);
      team->t.t_threads[i] = thr;
      KMP_DEBUG_ASSERT(thr);
      KMP_DEBUG_ASSERT(thr->th.th_team == team);
      /* align team and thread arrived states */
      KA_TRACE(20, ("__kmp_fork_team_threads: T#%d(%d:%d) init arrived "
                    "T#%d(%d:%d) join =%llu, plain=%llu\n",
                    __kmp_gtid_from_tid(0, team), team->t.t_id, 0,
                    __kmp_gtid_from_tid(i, team), team->t.t_id, i,
                    team->t.t_bar[bs_forkjoin_barrier].b_arrived,
                    team->t.t_bar[bs_plain_barrier].b_arrived));
#if OMP_40_ENABLED
      thr->th.th_teams_microtask = master_th->th.th_teams_microtask;
      thr->th.th_teams_level = master_th->th.th_teams_level;
      thr->th.th_teams_size = master_th->th.th_teams_size;
#endif
      { // Initialize threads' barrier data.
        int b;
        kmp_balign_t *balign = team->t.t_threads[i]->th.th_bar;
        for (b = 0; b < bs_last_barrier; ++b) {
          balign[b].bb.b_arrived = team->t.t_bar[b].b_arrived;
          KMP_DEBUG_ASSERT(balign[b].bb.wait_flag != KMP_BARRIER_PARENT_FLAG);
#if USE_DEBUGGER
          balign[b].bb.b_worker_arrived = team->t.t_bar[b].b_team_arrived;
#endif
        }
      }
    }

#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
    __kmp_partition_places(team);
#endif
  }

#if OMP_50_ENABLED
  if (__kmp_display_affinity && team->t.t_display_affinity != 1) {
    for (i = 0; i < team->t.t_nproc; i++) {
      kmp_info_t *thr = team->t.t_threads[i];
      if (thr->th.th_prev_num_threads != team->t.t_nproc ||
          thr->th.th_prev_level != team->t.t_level) {
        team->t.t_display_affinity = 1;
        break;
      }
    }
  }
#endif

  KMP_MB();
}

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
// Propagate any changes to the floating point control registers out to the team
// We try to avoid unnecessary writes to the relevant cache line in the team
// structure, so we don't make changes unless they are needed.
inline static void propagateFPControl(kmp_team_t *team) {
  if (__kmp_inherit_fp_control) {
    kmp_int16 x87_fpu_control_word;
    kmp_uint32 mxcsr;

    // Get master values of FPU control flags (both X87 and vector)
    __kmp_store_x87_fpu_control_word(&x87_fpu_control_word);
    __kmp_store_mxcsr(&mxcsr);
    mxcsr &= KMP_X86_MXCSR_MASK;

    // There is no point looking at t_fp_control_saved here.
    // If it is TRUE, we still have to update the values if they are different
    // from those we now have. If it is FALSE we didn't save anything yet, but
    // our objective is the same. We have to ensure that the values in the team
    // are the same as those we have.
    // So, this code achieves what we need whether or not t_fp_control_saved is
    // true. By checking whether the value needs updating we avoid unnecessary
    // writes that would put the cache-line into a written state, causing all
    // threads in the team to have to read it again.
    KMP_CHECK_UPDATE(team->t.t_x87_fpu_control_word, x87_fpu_control_word);
    KMP_CHECK_UPDATE(team->t.t_mxcsr, mxcsr);
    // Although we don't use this value, other code in the runtime wants to know
    // whether it should restore them. So we must ensure it is correct.
    KMP_CHECK_UPDATE(team->t.t_fp_control_saved, TRUE);
  } else {
    // Similarly here. Don't write to this cache-line in the team structure
    // unless we have to.
    KMP_CHECK_UPDATE(team->t.t_fp_control_saved, FALSE);
  }
}

// Do the opposite, setting the hardware registers to the updated values from
// the team.
inline static void updateHWFPControl(kmp_team_t *team) {
  if (__kmp_inherit_fp_control && team->t.t_fp_control_saved) {
    // Only reset the fp control regs if they have been changed in the team.
    // the parallel region that we are exiting.
    kmp_int16 x87_fpu_control_word;
    kmp_uint32 mxcsr;
    __kmp_store_x87_fpu_control_word(&x87_fpu_control_word);
    __kmp_store_mxcsr(&mxcsr);
    mxcsr &= KMP_X86_MXCSR_MASK;

    if (team->t.t_x87_fpu_control_word != x87_fpu_control_word) {
      __kmp_clear_x87_fpu_status_word();
      __kmp_load_x87_fpu_control_word(&team->t.t_x87_fpu_control_word);
    }

    if (team->t.t_mxcsr != mxcsr) {
      __kmp_load_mxcsr(&team->t.t_mxcsr);
    }
  }
}
#else
#define propagateFPControl(x) ((void)0)
#define updateHWFPControl(x) ((void)0)
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

static void __kmp_alloc_argv_entries(int argc, kmp_team_t *team,
                                     int realloc); // forward declaration

/* Run a parallel region that has been serialized, so runs only in a team of the
   single master thread. */
void __kmp_serialized_parallel(ident_t *loc, kmp_int32 global_tid) {
  kmp_info_t *this_thr;
  kmp_team_t *serial_team;

  KC_TRACE(10, ("__kmpc_serialized_parallel: called by T#%d\n", global_tid));

  /* Skip all this code for autopar serialized loops since it results in
     unacceptable overhead */
  if (loc != NULL && (loc->flags & KMP_IDENT_AUTOPAR))
    return;

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  this_thr = __kmp_threads[global_tid];
  serial_team = this_thr->th.th_serial_team;

  /* utilize the serialized team held by this thread */
  KMP_DEBUG_ASSERT(serial_team);
  KMP_MB();

  if (__kmp_tasking_mode != tskm_immediate_exec) {
    KMP_DEBUG_ASSERT(
        this_thr->th.th_task_team ==
        this_thr->th.th_team->t.t_task_team[this_thr->th.th_task_state]);
    KMP_DEBUG_ASSERT(serial_team->t.t_task_team[this_thr->th.th_task_state] ==
                     NULL);
    KA_TRACE(20, ("__kmpc_serialized_parallel: T#%d pushing task_team %p / "
                  "team %p, new task_team = NULL\n",
                  global_tid, this_thr->th.th_task_team, this_thr->th.th_team));
    this_thr->th.th_task_team = NULL;
  }

#if OMP_40_ENABLED
  kmp_proc_bind_t proc_bind = this_thr->th.th_set_proc_bind;
  if (this_thr->th.th_current_task->td_icvs.proc_bind == proc_bind_false) {
    proc_bind = proc_bind_false;
  } else if (proc_bind == proc_bind_default) {
    // No proc_bind clause was specified, so use the current value
    // of proc-bind-var for this parallel region.
    proc_bind = this_thr->th.th_current_task->td_icvs.proc_bind;
  }
  // Reset for next parallel region
  this_thr->th.th_set_proc_bind = proc_bind_default;
#endif /* OMP_40_ENABLED */

#if OMPT_SUPPORT
  ompt_data_t ompt_parallel_data = ompt_data_none;
  ompt_data_t *implicit_task_data;
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(global_tid);
  if (ompt_enabled.enabled &&
      this_thr->th.ompt_thread_info.state != ompt_state_overhead) {

    ompt_task_info_t *parent_task_info;
    parent_task_info = OMPT_CUR_TASK_INFO(this_thr);

    parent_task_info->frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    if (ompt_enabled.ompt_callback_parallel_begin) {
      int team_size = 1;

      ompt_callbacks.ompt_callback(ompt_callback_parallel_begin)(
          &(parent_task_info->task_data), &(parent_task_info->frame),
          &ompt_parallel_data, team_size, ompt_parallel_invoker_program,
          codeptr);
    }
  }
#endif // OMPT_SUPPORT

  if (this_thr->th.th_team != serial_team) {
    // Nested level will be an index in the nested nthreads array
    int level = this_thr->th.th_team->t.t_level;

    if (serial_team->t.t_serialized) {
      /* this serial team was already used
         TODO increase performance by making this locks more specific */
      kmp_team_t *new_team;

      __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);

      new_team = __kmp_allocate_team(this_thr->th.th_root, 1, 1,
#if OMPT_SUPPORT
                                     ompt_parallel_data,
#endif
#if OMP_40_ENABLED
                                     proc_bind,
#endif
                                     &this_thr->th.th_current_task->td_icvs,
                                     0 USE_NESTED_HOT_ARG(NULL));
      __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
      KMP_ASSERT(new_team);

      /* setup new serialized team and install it */
      new_team->t.t_threads[0] = this_thr;
      new_team->t.t_parent = this_thr->th.th_team;
      serial_team = new_team;
      this_thr->th.th_serial_team = serial_team;

      KF_TRACE(
          10,
          ("__kmpc_serialized_parallel: T#%d allocated new serial team %p\n",
           global_tid, serial_team));

      /* TODO the above breaks the requirement that if we run out of resources,
         then we can still guarantee that serialized teams are ok, since we may
         need to allocate a new one */
    } else {
      KF_TRACE(
          10,
          ("__kmpc_serialized_parallel: T#%d reusing cached serial team %p\n",
           global_tid, serial_team));
    }

    /* we have to initialize this serial team */
    KMP_DEBUG_ASSERT(serial_team->t.t_threads);
    KMP_DEBUG_ASSERT(serial_team->t.t_threads[0] == this_thr);
    KMP_DEBUG_ASSERT(this_thr->th.th_team != serial_team);
    serial_team->t.t_ident = loc;
    serial_team->t.t_serialized = 1;
    serial_team->t.t_nproc = 1;
    serial_team->t.t_parent = this_thr->th.th_team;
    serial_team->t.t_sched.sched = this_thr->th.th_team->t.t_sched.sched;
    this_thr->th.th_team = serial_team;
    serial_team->t.t_master_tid = this_thr->th.th_info.ds.ds_tid;

    KF_TRACE(10, ("__kmpc_serialized_parallel: T#d curtask=%p\n", global_tid,
                  this_thr->th.th_current_task));
    KMP_ASSERT(this_thr->th.th_current_task->td_flags.executing == 1);
    this_thr->th.th_current_task->td_flags.executing = 0;

    __kmp_push_current_task_to_thread(this_thr, serial_team, 0);

    /* TODO: GEH: do ICVs work for nested serialized teams? Don't we need an
       implicit task for each serialized task represented by
       team->t.t_serialized? */
    copy_icvs(&this_thr->th.th_current_task->td_icvs,
              &this_thr->th.th_current_task->td_parent->td_icvs);

    // Thread value exists in the nested nthreads array for the next nested
    // level
    if (__kmp_nested_nth.used && (level + 1 < __kmp_nested_nth.used)) {
      this_thr->th.th_current_task->td_icvs.nproc =
          __kmp_nested_nth.nth[level + 1];
    }

#if OMP_40_ENABLED
    if (__kmp_nested_proc_bind.used &&
        (level + 1 < __kmp_nested_proc_bind.used)) {
      this_thr->th.th_current_task->td_icvs.proc_bind =
          __kmp_nested_proc_bind.bind_types[level + 1];
    }
#endif /* OMP_40_ENABLED */

#if USE_DEBUGGER
    serial_team->t.t_pkfn = (microtask_t)(~0); // For the debugger.
#endif
    this_thr->th.th_info.ds.ds_tid = 0;

    /* set thread cache values */
    this_thr->th.th_team_nproc = 1;
    this_thr->th.th_team_master = this_thr;
    this_thr->th.th_team_serialized = 1;

    serial_team->t.t_level = serial_team->t.t_parent->t.t_level + 1;
    serial_team->t.t_active_level = serial_team->t.t_parent->t.t_active_level;
#if OMP_50_ENABLED
    serial_team->t.t_def_allocator = this_thr->th.th_def_allocator; // save
#endif

    propagateFPControl(serial_team);

    /* check if we need to allocate dispatch buffers stack */
    KMP_DEBUG_ASSERT(serial_team->t.t_dispatch);
    if (!serial_team->t.t_dispatch->th_disp_buffer) {
      serial_team->t.t_dispatch->th_disp_buffer =
          (dispatch_private_info_t *)__kmp_allocate(
              sizeof(dispatch_private_info_t));
    }
    this_thr->th.th_dispatch = serial_team->t.t_dispatch;

    KMP_MB();

  } else {
    /* this serialized team is already being used,
     * that's fine, just add another nested level */
    KMP_DEBUG_ASSERT(this_thr->th.th_team == serial_team);
    KMP_DEBUG_ASSERT(serial_team->t.t_threads);
    KMP_DEBUG_ASSERT(serial_team->t.t_threads[0] == this_thr);
    ++serial_team->t.t_serialized;
    this_thr->th.th_team_serialized = serial_team->t.t_serialized;

    // Nested level will be an index in the nested nthreads array
    int level = this_thr->th.th_team->t.t_level;
    // Thread value exists in the nested nthreads array for the next nested
    // level
    if (__kmp_nested_nth.used && (level + 1 < __kmp_nested_nth.used)) {
      this_thr->th.th_current_task->td_icvs.nproc =
          __kmp_nested_nth.nth[level + 1];
    }
    serial_team->t.t_level++;
    KF_TRACE(10, ("__kmpc_serialized_parallel: T#%d increasing nesting level "
                  "of serial team %p to %d\n",
                  global_tid, serial_team, serial_team->t.t_level));

    /* allocate/push dispatch buffers stack */
    KMP_DEBUG_ASSERT(serial_team->t.t_dispatch);
    {
      dispatch_private_info_t *disp_buffer =
          (dispatch_private_info_t *)__kmp_allocate(
              sizeof(dispatch_private_info_t));
      disp_buffer->next = serial_team->t.t_dispatch->th_disp_buffer;
      serial_team->t.t_dispatch->th_disp_buffer = disp_buffer;
    }
    this_thr->th.th_dispatch = serial_team->t.t_dispatch;

    KMP_MB();
  }
#if OMP_40_ENABLED
  KMP_CHECK_UPDATE(serial_team->t.t_cancel_request, cancel_noreq);
#endif

#if OMP_50_ENABLED
  // Perform the display affinity functionality for
  // serialized parallel regions
  if (__kmp_display_affinity) {
    if (this_thr->th.th_prev_level != serial_team->t.t_level ||
        this_thr->th.th_prev_num_threads != 1) {
      // NULL means use the affinity-format-var ICV
      __kmp_aux_display_affinity(global_tid, NULL);
      this_thr->th.th_prev_level = serial_team->t.t_level;
      this_thr->th.th_prev_num_threads = 1;
    }
  }
#endif

  if (__kmp_env_consistency_check)
    __kmp_push_parallel(global_tid, NULL);
#if OMPT_SUPPORT
  serial_team->t.ompt_team_info.master_return_address = codeptr;
  if (ompt_enabled.enabled &&
      this_thr->th.ompt_thread_info.state != ompt_state_overhead) {
    OMPT_CUR_TASK_INFO(this_thr)->frame.exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);

    ompt_lw_taskteam_t lw_taskteam;
    __ompt_lw_taskteam_init(&lw_taskteam, this_thr, global_tid,
                            &ompt_parallel_data, codeptr);

    __ompt_lw_taskteam_link(&lw_taskteam, this_thr, 1);
    // don't use lw_taskteam after linking. content was swaped

    /* OMPT implicit task begin */
    implicit_task_data = OMPT_CUR_TASK_DATA(this_thr);
    if (ompt_enabled.ompt_callback_implicit_task) {
      ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
          ompt_scope_begin, OMPT_CUR_TEAM_DATA(this_thr),
          OMPT_CUR_TASK_DATA(this_thr), 1, __kmp_tid_from_gtid(global_tid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
      OMPT_CUR_TASK_INFO(this_thr)
          ->thread_num = __kmp_tid_from_gtid(global_tid);
    }

    /* OMPT state */
    this_thr->th.ompt_thread_info.state = ompt_state_work_parallel;
    OMPT_CUR_TASK_INFO(this_thr)->frame.exit_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
#endif
}

/* most of the work for a fork */
/* return true if we really went parallel, false if serialized */
int __kmp_fork_call(ident_t *loc, int gtid,
                    enum fork_context_e call_context, // Intel, GNU, ...
                    kmp_int32 argc, microtask_t microtask, launch_t invoker,
/* TODO: revert workaround for Intel(R) 64 tracker #96 */
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
                    va_list *ap
#else
                    va_list ap
#endif
                    ) {
  void **argv;
  int i;
  int master_tid;
  int master_this_cons;
  kmp_team_t *team;
  kmp_team_t *parent_team;
  kmp_info_t *master_th;
  kmp_root_t *root;
  int nthreads;
  int master_active;
  int master_set_numthreads;
  int level;
#if OMP_40_ENABLED
  int active_level;
  int teams_level;
#endif
#if KMP_NESTED_HOT_TEAMS
  kmp_hot_team_ptr_t **p_hot_teams;
#endif
  { // KMP_TIME_BLOCK
    KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_fork_call);
    KMP_COUNT_VALUE(OMP_PARALLEL_args, argc);

    KA_TRACE(20, ("__kmp_fork_call: enter T#%d\n", gtid));
    if (__kmp_stkpadding > 0 && __kmp_root[gtid] != NULL) {
      /* Some systems prefer the stack for the root thread(s) to start with */
      /* some gap from the parent stack to prevent false sharing. */
      void *dummy = KMP_ALLOCA(__kmp_stkpadding);
      /* These 2 lines below are so this does not get optimized out */
      if (__kmp_stkpadding > KMP_MAX_STKPADDING)
        __kmp_stkpadding += (short)((kmp_int64)dummy);
    }

    /* initialize if needed */
    KMP_DEBUG_ASSERT(
        __kmp_init_serial); // AC: potentially unsafe, not in sync with shutdown
    if (!TCR_4(__kmp_init_parallel))
      __kmp_parallel_initialize();

    /* setup current data */
    master_th = __kmp_threads[gtid]; // AC: potentially unsafe, not in sync with
    // shutdown
    parent_team = master_th->th.th_team;
    master_tid = master_th->th.th_info.ds.ds_tid;
    master_this_cons = master_th->th.th_local.this_construct;
    root = master_th->th.th_root;
    master_active = root->r.r_active;
    master_set_numthreads = master_th->th.th_set_nproc;

#if OMPT_SUPPORT
    ompt_data_t ompt_parallel_data = ompt_data_none;
    ompt_data_t *parent_task_data;
    ompt_frame_t *ompt_frame;
    ompt_data_t *implicit_task_data;
    void *return_address = NULL;

    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, &parent_task_data, &ompt_frame,
                                    NULL, NULL);
      return_address = OMPT_LOAD_RETURN_ADDRESS(gtid);
    }
#endif

    // Nested level will be an index in the nested nthreads array
    level = parent_team->t.t_level;
    // used to launch non-serial teams even if nested is not allowed
    active_level = parent_team->t.t_active_level;
#if OMP_40_ENABLED
    // needed to check nesting inside the teams
    teams_level = master_th->th.th_teams_level;
#endif
#if KMP_NESTED_HOT_TEAMS
    p_hot_teams = &master_th->th.th_hot_teams;
    if (*p_hot_teams == NULL && __kmp_hot_teams_max_level > 0) {
      *p_hot_teams = (kmp_hot_team_ptr_t *)__kmp_allocate(
          sizeof(kmp_hot_team_ptr_t) * __kmp_hot_teams_max_level);
      (*p_hot_teams)[0].hot_team = root->r.r_hot_team;
      // it is either actual or not needed (when active_level > 0)
      (*p_hot_teams)[0].hot_team_nth = 1;
    }
#endif

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      if (ompt_enabled.ompt_callback_parallel_begin) {
        int team_size = master_set_numthreads
                            ? master_set_numthreads
                            : get__nproc_2(parent_team, master_tid);
        ompt_callbacks.ompt_callback(ompt_callback_parallel_begin)(
            parent_task_data, ompt_frame, &ompt_parallel_data, team_size,
            OMPT_INVOKER(call_context), return_address);
      }
      master_th->th.ompt_thread_info.state = ompt_state_overhead;
    }
#endif

    master_th->th.th_ident = loc;

#if OMP_40_ENABLED
    if (master_th->th.th_teams_microtask && ap &&
        microtask != (microtask_t)__kmp_teams_master && level == teams_level) {
      // AC: This is start of parallel that is nested inside teams construct.
      // The team is actual (hot), all workers are ready at the fork barrier.
      // No lock needed to initialize the team a bit, then free workers.
      parent_team->t.t_ident = loc;
      __kmp_alloc_argv_entries(argc, parent_team, TRUE);
      parent_team->t.t_argc = argc;
      argv = (void **)parent_team->t.t_argv;
      for (i = argc - 1; i >= 0; --i)
/* TODO: revert workaround for Intel(R) 64 tracker #96 */
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
        *argv++ = va_arg(*ap, void *);
#else
        *argv++ = va_arg(ap, void *);
#endif
      // Increment our nested depth levels, but not increase the serialization
      if (parent_team == master_th->th.th_serial_team) {
        // AC: we are in serialized parallel
        __kmpc_serialized_parallel(loc, gtid);
        KMP_DEBUG_ASSERT(parent_team->t.t_serialized > 1);
        // AC: need this in order enquiry functions work
        // correctly, will restore at join time
        parent_team->t.t_serialized--;
#if OMPT_SUPPORT
        void *dummy;
        void **exit_runtime_p;

        ompt_lw_taskteam_t lw_taskteam;

        if (ompt_enabled.enabled) {
          __ompt_lw_taskteam_init(&lw_taskteam, master_th, gtid,
                                  &ompt_parallel_data, return_address);
          exit_runtime_p = &(lw_taskteam.ompt_task_info.frame.exit_frame.ptr);

          __ompt_lw_taskteam_link(&lw_taskteam, master_th, 0);
          // don't use lw_taskteam after linking. content was swaped

          /* OMPT implicit task begin */
          implicit_task_data = OMPT_CUR_TASK_DATA(master_th);
          if (ompt_enabled.ompt_callback_implicit_task) {
            ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                ompt_scope_begin, OMPT_CUR_TEAM_DATA(master_th),
                implicit_task_data, 1, __kmp_tid_from_gtid(gtid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
            OMPT_CUR_TASK_INFO(master_th)
                ->thread_num = __kmp_tid_from_gtid(gtid);
          }

          /* OMPT state */
          master_th->th.ompt_thread_info.state = ompt_state_work_parallel;
        } else {
          exit_runtime_p = &dummy;
        }
#endif

        {
          KMP_TIME_PARTITIONED_BLOCK(OMP_parallel);
          KMP_SET_THREAD_STATE_BLOCK(IMPLICIT_TASK);
          __kmp_invoke_microtask(microtask, gtid, 0, argc, parent_team->t.t_argv
#if OMPT_SUPPORT
                                 ,
                                 exit_runtime_p
#endif
                                 );
        }

#if OMPT_SUPPORT
        *exit_runtime_p = NULL;
        if (ompt_enabled.enabled) {
          OMPT_CUR_TASK_INFO(master_th)->frame.exit_frame = ompt_data_none;
          if (ompt_enabled.ompt_callback_implicit_task) {
            ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                ompt_scope_end, NULL, implicit_task_data, 1,
                OMPT_CUR_TASK_INFO(master_th)->thread_num, ompt_task_implicit); // TODO: Can this be ompt_task_initial?
          }
          __ompt_lw_taskteam_unlink(master_th);

          if (ompt_enabled.ompt_callback_parallel_end) {
            ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
                OMPT_CUR_TEAM_DATA(master_th), OMPT_CUR_TASK_DATA(master_th),
                OMPT_INVOKER(call_context), return_address);
          }
          master_th->th.ompt_thread_info.state = ompt_state_overhead;
        }
#endif
        return TRUE;
      }

      parent_team->t.t_pkfn = microtask;
      parent_team->t.t_invoke = invoker;
      KMP_ATOMIC_INC(&root->r.r_in_parallel);
      parent_team->t.t_active_level++;
      parent_team->t.t_level++;
#if OMP_50_ENABLED
      parent_team->t.t_def_allocator = master_th->th.th_def_allocator; // save
#endif

      /* Change number of threads in the team if requested */
      if (master_set_numthreads) { // The parallel has num_threads clause
        if (master_set_numthreads < master_th->th.th_teams_size.nth) {
          // AC: only can reduce number of threads dynamically, can't increase
          kmp_info_t **other_threads = parent_team->t.t_threads;
          parent_team->t.t_nproc = master_set_numthreads;
          for (i = 0; i < master_set_numthreads; ++i) {
            other_threads[i]->th.th_team_nproc = master_set_numthreads;
          }
          // Keep extra threads hot in the team for possible next parallels
        }
        master_th->th.th_set_nproc = 0;
      }

#if USE_DEBUGGER
      if (__kmp_debugging) { // Let debugger override number of threads.
        int nth = __kmp_omp_num_threads(loc);
        if (nth > 0) { // 0 means debugger doesn't want to change num threads
          master_set_numthreads = nth;
        }
      }
#endif

      KF_TRACE(10, ("__kmp_fork_call: before internal fork: root=%p, team=%p, "
                    "master_th=%p, gtid=%d\n",
                    root, parent_team, master_th, gtid));
      __kmp_internal_fork(loc, gtid, parent_team);
      KF_TRACE(10, ("__kmp_fork_call: after internal fork: root=%p, team=%p, "
                    "master_th=%p, gtid=%d\n",
                    root, parent_team, master_th, gtid));

      /* Invoke microtask for MASTER thread */
      KA_TRACE(20, ("__kmp_fork_call: T#%d(%d:0) invoke microtask = %p\n", gtid,
                    parent_team->t.t_id, parent_team->t.t_pkfn));

      if (!parent_team->t.t_invoke(gtid)) {
        KMP_ASSERT2(0, "cannot invoke microtask for MASTER thread");
      }
      KA_TRACE(20, ("__kmp_fork_call: T#%d(%d:0) done microtask = %p\n", gtid,
                    parent_team->t.t_id, parent_team->t.t_pkfn));
      KMP_MB(); /* Flush all pending memory write invalidates.  */

      KA_TRACE(20, ("__kmp_fork_call: parallel exit T#%d\n", gtid));

      return TRUE;
    } // Parallel closely nested in teams construct
#endif /* OMP_40_ENABLED */

#if KMP_DEBUG
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      KMP_DEBUG_ASSERT(master_th->th.th_task_team ==
                       parent_team->t.t_task_team[master_th->th.th_task_state]);
    }
#endif

    if (parent_team->t.t_active_level >=
        master_th->th.th_current_task->td_icvs.max_active_levels) {
      nthreads = 1;
    } else {
#if OMP_40_ENABLED
      int enter_teams = ((ap == NULL && active_level == 0) ||
                         (ap && teams_level > 0 && teams_level == level));
#endif
      nthreads =
          master_set_numthreads
              ? master_set_numthreads
              : get__nproc_2(
                    parent_team,
                    master_tid); // TODO: get nproc directly from current task

      // Check if we need to take forkjoin lock? (no need for serialized
      // parallel out of teams construct). This code moved here from
      // __kmp_reserve_threads() to speedup nested serialized parallels.
      if (nthreads > 1) {
        if ((!get__nested(master_th) && (root->r.r_in_parallel
#if OMP_40_ENABLED
                                         && !enter_teams
#endif /* OMP_40_ENABLED */
                                         )) ||
            (__kmp_library == library_serial)) {
          KC_TRACE(10, ("__kmp_fork_call: T#%d serializing team; requested %d"
                        " threads\n",
                        gtid, nthreads));
          nthreads = 1;
        }
      }
      if (nthreads > 1) {
        /* determine how many new threads we can use */
        __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);
        nthreads = __kmp_reserve_threads(
            root, parent_team, master_tid, nthreads
#if OMP_40_ENABLED
            /* AC: If we execute teams from parallel region (on host), then
               teams should be created but each can only have 1 thread if
               nesting is disabled. If teams called from serial region, then
               teams and their threads should be created regardless of the
               nesting setting. */
            ,
            enter_teams
#endif /* OMP_40_ENABLED */
            );
        if (nthreads == 1) {
          // Free lock for single thread execution here; for multi-thread
          // execution it will be freed later after team of threads created
          // and initialized
          __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
        }
      }
    }
    KMP_DEBUG_ASSERT(nthreads > 0);

    // If we temporarily changed the set number of threads then restore it now
    master_th->th.th_set_nproc = 0;

    /* create a serialized parallel region? */
    if (nthreads == 1) {
/* josh todo: hypothetical question: what do we do for OS X*? */
#if KMP_OS_LINUX &&                                                            \
    (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64)
      void *args[argc];
#else
      void **args = (void **)KMP_ALLOCA(argc * sizeof(void *));
#endif /* KMP_OS_LINUX && ( KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM || \
          KMP_ARCH_AARCH64) */

      KA_TRACE(20,
               ("__kmp_fork_call: T#%d serializing parallel region\n", gtid));

      __kmpc_serialized_parallel(loc, gtid);

      if (call_context == fork_context_intel) {
        /* TODO this sucks, use the compiler itself to pass args! :) */
        master_th->th.th_serial_team->t.t_ident = loc;
#if OMP_40_ENABLED
        if (!ap) {
          // revert change made in __kmpc_serialized_parallel()
          master_th->th.th_serial_team->t.t_level--;
// Get args from parent team for teams construct

#if OMPT_SUPPORT
          void *dummy;
          void **exit_runtime_p;
          ompt_task_info_t *task_info;

          ompt_lw_taskteam_t lw_taskteam;

          if (ompt_enabled.enabled) {
            __ompt_lw_taskteam_init(&lw_taskteam, master_th, gtid,
                                    &ompt_parallel_data, return_address);

            __ompt_lw_taskteam_link(&lw_taskteam, master_th, 0);
            // don't use lw_taskteam after linking. content was swaped

            task_info = OMPT_CUR_TASK_INFO(master_th);
            exit_runtime_p = &(task_info->frame.exit_frame.ptr);
            if (ompt_enabled.ompt_callback_implicit_task) {
              ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                  ompt_scope_begin, OMPT_CUR_TEAM_DATA(master_th),
                  &(task_info->task_data), 1, __kmp_tid_from_gtid(gtid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
              OMPT_CUR_TASK_INFO(master_th)
                  ->thread_num = __kmp_tid_from_gtid(gtid);
            }

            /* OMPT state */
            master_th->th.ompt_thread_info.state = ompt_state_work_parallel;
          } else {
            exit_runtime_p = &dummy;
          }
#endif

          {
            KMP_TIME_PARTITIONED_BLOCK(OMP_parallel);
            KMP_SET_THREAD_STATE_BLOCK(IMPLICIT_TASK);
            __kmp_invoke_microtask(microtask, gtid, 0, argc,
                                   parent_team->t.t_argv
#if OMPT_SUPPORT
                                   ,
                                   exit_runtime_p
#endif
                                   );
          }

#if OMPT_SUPPORT
          if (ompt_enabled.enabled) {
            exit_runtime_p = NULL;
            if (ompt_enabled.ompt_callback_implicit_task) {
              ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                  ompt_scope_end, NULL, &(task_info->task_data), 1,
                  OMPT_CUR_TASK_INFO(master_th)->thread_num, ompt_task_implicit); // TODO: Can this be ompt_task_initial?
            }

            __ompt_lw_taskteam_unlink(master_th);
            if (ompt_enabled.ompt_callback_parallel_end) {
              ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
                  OMPT_CUR_TEAM_DATA(master_th), parent_task_data,
                  OMPT_INVOKER(call_context), return_address);
            }
            master_th->th.ompt_thread_info.state = ompt_state_overhead;
          }
#endif
        } else if (microtask == (microtask_t)__kmp_teams_master) {
          KMP_DEBUG_ASSERT(master_th->th.th_team ==
                           master_th->th.th_serial_team);
          team = master_th->th.th_team;
          // team->t.t_pkfn = microtask;
          team->t.t_invoke = invoker;
          __kmp_alloc_argv_entries(argc, team, TRUE);
          team->t.t_argc = argc;
          argv = (void **)team->t.t_argv;
          if (ap) {
            for (i = argc - 1; i >= 0; --i)
// TODO: revert workaround for Intel(R) 64 tracker #96
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
              *argv++ = va_arg(*ap, void *);
#else
              *argv++ = va_arg(ap, void *);
#endif
          } else {
            for (i = 0; i < argc; ++i)
              // Get args from parent team for teams construct
              argv[i] = parent_team->t.t_argv[i];
          }
          // AC: revert change made in __kmpc_serialized_parallel()
          //     because initial code in teams should have level=0
          team->t.t_level--;
          // AC: call special invoker for outer "parallel" of teams construct
          invoker(gtid);
        } else {
#endif /* OMP_40_ENABLED */
          argv = args;
          for (i = argc - 1; i >= 0; --i)
// TODO: revert workaround for Intel(R) 64 tracker #96
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
            *argv++ = va_arg(*ap, void *);
#else
          *argv++ = va_arg(ap, void *);
#endif
          KMP_MB();

#if OMPT_SUPPORT
          void *dummy;
          void **exit_runtime_p;
          ompt_task_info_t *task_info;

          ompt_lw_taskteam_t lw_taskteam;

          if (ompt_enabled.enabled) {
            __ompt_lw_taskteam_init(&lw_taskteam, master_th, gtid,
                                    &ompt_parallel_data, return_address);
            __ompt_lw_taskteam_link(&lw_taskteam, master_th, 0);
            // don't use lw_taskteam after linking. content was swaped
            task_info = OMPT_CUR_TASK_INFO(master_th);
            exit_runtime_p = &(task_info->frame.exit_frame.ptr);

            /* OMPT implicit task begin */
            implicit_task_data = OMPT_CUR_TASK_DATA(master_th);
            if (ompt_enabled.ompt_callback_implicit_task) {
              ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                  ompt_scope_begin, OMPT_CUR_TEAM_DATA(master_th),
                  implicit_task_data, 1, __kmp_tid_from_gtid(gtid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
              OMPT_CUR_TASK_INFO(master_th)
                  ->thread_num = __kmp_tid_from_gtid(gtid);
            }

            /* OMPT state */
            master_th->th.ompt_thread_info.state = ompt_state_work_parallel;
          } else {
            exit_runtime_p = &dummy;
          }
#endif

          {
            KMP_TIME_PARTITIONED_BLOCK(OMP_parallel);
            KMP_SET_THREAD_STATE_BLOCK(IMPLICIT_TASK);
            __kmp_invoke_microtask(microtask, gtid, 0, argc, args
#if OMPT_SUPPORT
                                   ,
                                   exit_runtime_p
#endif
                                   );
          }

#if OMPT_SUPPORT
          if (ompt_enabled.enabled) {
            *exit_runtime_p = NULL;
            if (ompt_enabled.ompt_callback_implicit_task) {
              ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
                  ompt_scope_end, NULL, &(task_info->task_data), 1,
                  OMPT_CUR_TASK_INFO(master_th)->thread_num, ompt_task_implicit); // TODO: Can this be ompt_task_initial?
            }

            ompt_parallel_data = *OMPT_CUR_TEAM_DATA(master_th);
            __ompt_lw_taskteam_unlink(master_th);
            if (ompt_enabled.ompt_callback_parallel_end) {
              ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
                  &ompt_parallel_data, parent_task_data,
                  OMPT_INVOKER(call_context), return_address);
            }
            master_th->th.ompt_thread_info.state = ompt_state_overhead;
          }
#endif
#if OMP_40_ENABLED
        }
#endif /* OMP_40_ENABLED */
      } else if (call_context == fork_context_gnu) {
#if OMPT_SUPPORT
        ompt_lw_taskteam_t lwt;
        __ompt_lw_taskteam_init(&lwt, master_th, gtid, &ompt_parallel_data,
                                return_address);

        lwt.ompt_task_info.frame.exit_frame = ompt_data_none;
        __ompt_lw_taskteam_link(&lwt, master_th, 1);
// don't use lw_taskteam after linking. content was swaped
#endif

        // we were called from GNU native code
        KA_TRACE(20, ("__kmp_fork_call: T#%d serial exit\n", gtid));
        return FALSE;
      } else {
        KMP_ASSERT2(call_context < fork_context_last,
                    "__kmp_fork_call: unknown fork_context parameter");
      }

      KA_TRACE(20, ("__kmp_fork_call: T#%d serial exit\n", gtid));
      KMP_MB();
      return FALSE;
    } // if (nthreads == 1)

    // GEH: only modify the executing flag in the case when not serialized
    //      serialized case is handled in kmpc_serialized_parallel
    KF_TRACE(10, ("__kmp_fork_call: parent_team_aclevel=%d, master_th=%p, "
                  "curtask=%p, curtask_max_aclevel=%d\n",
                  parent_team->t.t_active_level, master_th,
                  master_th->th.th_current_task,
                  master_th->th.th_current_task->td_icvs.max_active_levels));
    // TODO: GEH - cannot do this assertion because root thread not set up as
    // executing
    // KMP_ASSERT( master_th->th.th_current_task->td_flags.executing == 1 );
    master_th->th.th_current_task->td_flags.executing = 0;

#if OMP_40_ENABLED
    if (!master_th->th.th_teams_microtask || level > teams_level)
#endif /* OMP_40_ENABLED */
    {
      /* Increment our nested depth level */
      KMP_ATOMIC_INC(&root->r.r_in_parallel);
    }

    // See if we need to make a copy of the ICVs.
    int nthreads_icv = master_th->th.th_current_task->td_icvs.nproc;
    if ((level + 1 < __kmp_nested_nth.used) &&
        (__kmp_nested_nth.nth[level + 1] != nthreads_icv)) {
      nthreads_icv = __kmp_nested_nth.nth[level + 1];
    } else {
      nthreads_icv = 0; // don't update
    }

#if OMP_40_ENABLED
    // Figure out the proc_bind_policy for the new team.
    kmp_proc_bind_t proc_bind = master_th->th.th_set_proc_bind;
    kmp_proc_bind_t proc_bind_icv =
        proc_bind_default; // proc_bind_default means don't update
    if (master_th->th.th_current_task->td_icvs.proc_bind == proc_bind_false) {
      proc_bind = proc_bind_false;
    } else {
      if (proc_bind == proc_bind_default) {
        // No proc_bind clause specified; use current proc-bind-var for this
        // parallel region
        proc_bind = master_th->th.th_current_task->td_icvs.proc_bind;
      }
      /* else: The proc_bind policy was specified explicitly on parallel clause.
         This overrides proc-bind-var for this parallel region, but does not
         change proc-bind-var. */
      // Figure the value of proc-bind-var for the child threads.
      if ((level + 1 < __kmp_nested_proc_bind.used) &&
          (__kmp_nested_proc_bind.bind_types[level + 1] !=
           master_th->th.th_current_task->td_icvs.proc_bind)) {
        proc_bind_icv = __kmp_nested_proc_bind.bind_types[level + 1];
      }
    }

    // Reset for next parallel region
    master_th->th.th_set_proc_bind = proc_bind_default;
#endif /* OMP_40_ENABLED */

    if ((nthreads_icv > 0)
#if OMP_40_ENABLED
        || (proc_bind_icv != proc_bind_default)
#endif /* OMP_40_ENABLED */
            ) {
      kmp_internal_control_t new_icvs;
      copy_icvs(&new_icvs, &master_th->th.th_current_task->td_icvs);
      new_icvs.next = NULL;
      if (nthreads_icv > 0) {
        new_icvs.nproc = nthreads_icv;
      }

#if OMP_40_ENABLED
      if (proc_bind_icv != proc_bind_default) {
        new_icvs.proc_bind = proc_bind_icv;
      }
#endif /* OMP_40_ENABLED */

      /* allocate a new parallel team */
      KF_TRACE(10, ("__kmp_fork_call: before __kmp_allocate_team\n"));
      team = __kmp_allocate_team(root, nthreads, nthreads,
#if OMPT_SUPPORT
                                 ompt_parallel_data,
#endif
#if OMP_40_ENABLED
                                 proc_bind,
#endif
                                 &new_icvs, argc USE_NESTED_HOT_ARG(master_th));
    } else {
      /* allocate a new parallel team */
      KF_TRACE(10, ("__kmp_fork_call: before __kmp_allocate_team\n"));
      team = __kmp_allocate_team(root, nthreads, nthreads,
#if OMPT_SUPPORT
                                 ompt_parallel_data,
#endif
#if OMP_40_ENABLED
                                 proc_bind,
#endif
                                 &master_th->th.th_current_task->td_icvs,
                                 argc USE_NESTED_HOT_ARG(master_th));
    }
    KF_TRACE(
        10, ("__kmp_fork_call: after __kmp_allocate_team - team = %p\n", team));

    /* setup the new team */
    KMP_CHECK_UPDATE(team->t.t_master_tid, master_tid);
    KMP_CHECK_UPDATE(team->t.t_master_this_cons, master_this_cons);
    KMP_CHECK_UPDATE(team->t.t_ident, loc);
    KMP_CHECK_UPDATE(team->t.t_parent, parent_team);
    KMP_CHECK_UPDATE_SYNC(team->t.t_pkfn, microtask);
#if OMPT_SUPPORT
    KMP_CHECK_UPDATE_SYNC(team->t.ompt_team_info.master_return_address,
                          return_address);
#endif
    KMP_CHECK_UPDATE(team->t.t_invoke, invoker); // TODO move to root, maybe
// TODO: parent_team->t.t_level == INT_MAX ???
#if OMP_40_ENABLED
    if (!master_th->th.th_teams_microtask || level > teams_level) {
#endif /* OMP_40_ENABLED */
      int new_level = parent_team->t.t_level + 1;
      KMP_CHECK_UPDATE(team->t.t_level, new_level);
      new_level = parent_team->t.t_active_level + 1;
      KMP_CHECK_UPDATE(team->t.t_active_level, new_level);
#if OMP_40_ENABLED
    } else {
      // AC: Do not increase parallel level at start of the teams construct
      int new_level = parent_team->t.t_level;
      KMP_CHECK_UPDATE(team->t.t_level, new_level);
      new_level = parent_team->t.t_active_level;
      KMP_CHECK_UPDATE(team->t.t_active_level, new_level);
    }
#endif /* OMP_40_ENABLED */
    kmp_r_sched_t new_sched = get__sched_2(parent_team, master_tid);
    // set master's schedule as new run-time schedule
    KMP_CHECK_UPDATE(team->t.t_sched.sched, new_sched.sched);

#if OMP_40_ENABLED
    KMP_CHECK_UPDATE(team->t.t_cancel_request, cancel_noreq);
#endif
#if OMP_50_ENABLED
    KMP_CHECK_UPDATE(team->t.t_def_allocator, master_th->th.th_def_allocator);
#endif

    // Update the floating point rounding in the team if required.
    propagateFPControl(team);

    if (__kmp_tasking_mode != tskm_immediate_exec) {
      // Set master's task team to team's task team. Unless this is hot team, it
      // should be NULL.
      KMP_DEBUG_ASSERT(master_th->th.th_task_team ==
                       parent_team->t.t_task_team[master_th->th.th_task_state]);
      KA_TRACE(20, ("__kmp_fork_call: Master T#%d pushing task_team %p / team "
                    "%p, new task_team %p / team %p\n",
                    __kmp_gtid_from_thread(master_th),
                    master_th->th.th_task_team, parent_team,
                    team->t.t_task_team[master_th->th.th_task_state], team));

      if (active_level || master_th->th.th_task_team) {
        // Take a memo of master's task_state
        KMP_DEBUG_ASSERT(master_th->th.th_task_state_memo_stack);
        if (master_th->th.th_task_state_top >=
            master_th->th.th_task_state_stack_sz) { // increase size
          kmp_uint32 new_size = 2 * master_th->th.th_task_state_stack_sz;
          kmp_uint8 *old_stack, *new_stack;
          kmp_uint32 i;
          new_stack = (kmp_uint8 *)__kmp_allocate(new_size);
          for (i = 0; i < master_th->th.th_task_state_stack_sz; ++i) {
            new_stack[i] = master_th->th.th_task_state_memo_stack[i];
          }
          for (i = master_th->th.th_task_state_stack_sz; i < new_size;
               ++i) { // zero-init rest of stack
            new_stack[i] = 0;
          }
          old_stack = master_th->th.th_task_state_memo_stack;
          master_th->th.th_task_state_memo_stack = new_stack;
          master_th->th.th_task_state_stack_sz = new_size;
          __kmp_free(old_stack);
        }
        // Store master's task_state on stack
        master_th->th
            .th_task_state_memo_stack[master_th->th.th_task_state_top] =
            master_th->th.th_task_state;
        master_th->th.th_task_state_top++;
#if KMP_NESTED_HOT_TEAMS
        if (master_th->th.th_hot_teams &&
            active_level < __kmp_hot_teams_max_level &&
            team == master_th->th.th_hot_teams[active_level].hot_team) {
          // Restore master's nested state if nested hot team
          master_th->th.th_task_state =
              master_th->th
                  .th_task_state_memo_stack[master_th->th.th_task_state_top];
        } else {
#endif
          master_th->th.th_task_state = 0;
#if KMP_NESTED_HOT_TEAMS
        }
#endif
      }
#if !KMP_NESTED_HOT_TEAMS
      KMP_DEBUG_ASSERT((master_th->th.th_task_team == NULL) ||
                       (team == root->r.r_hot_team));
#endif
    }

    KA_TRACE(
        20,
        ("__kmp_fork_call: T#%d(%d:%d)->(%d:0) created a team of %d threads\n",
         gtid, parent_team->t.t_id, team->t.t_master_tid, team->t.t_id,
         team->t.t_nproc));
    KMP_DEBUG_ASSERT(team != root->r.r_hot_team ||
                     (team->t.t_master_tid == 0 &&
                      (team->t.t_parent == root->r.r_root_team ||
                       team->t.t_parent->t.t_serialized)));
    KMP_MB();

    /* now, setup the arguments */
    argv = (void **)team->t.t_argv;
#if OMP_40_ENABLED
    if (ap) {
#endif /* OMP_40_ENABLED */
      for (i = argc - 1; i >= 0; --i) {
// TODO: revert workaround for Intel(R) 64 tracker #96
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
        void *new_argv = va_arg(*ap, void *);
#else
      void *new_argv = va_arg(ap, void *);
#endif
        KMP_CHECK_UPDATE(*argv, new_argv);
        argv++;
      }
#if OMP_40_ENABLED
    } else {
      for (i = 0; i < argc; ++i) {
        // Get args from parent team for teams construct
        KMP_CHECK_UPDATE(argv[i], team->t.t_parent->t.t_argv[i]);
      }
    }
#endif /* OMP_40_ENABLED */

    /* now actually fork the threads */
    KMP_CHECK_UPDATE(team->t.t_master_active, master_active);
    if (!root->r.r_active) // Only do assignment if it prevents cache ping-pong
      root->r.r_active = TRUE;

    __kmp_fork_team_threads(root, team, master_th, gtid);
    __kmp_setup_icv_copy(team, nthreads,
                         &master_th->th.th_current_task->td_icvs, loc);

#if OMPT_SUPPORT
    master_th->th.ompt_thread_info.state = ompt_state_work_parallel;
#endif

    __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);

#if USE_ITT_BUILD
    if (team->t.t_active_level == 1 // only report frames at level 1
#if OMP_40_ENABLED
        && !master_th->th.th_teams_microtask // not in teams construct
#endif /* OMP_40_ENABLED */
        ) {
#if USE_ITT_NOTIFY
      if ((__itt_frame_submit_v3_ptr || KMP_ITT_DEBUG) &&
          (__kmp_forkjoin_frames_mode == 3 ||
           __kmp_forkjoin_frames_mode == 1)) {
        kmp_uint64 tmp_time = 0;
        if (__itt_get_timestamp_ptr)
          tmp_time = __itt_get_timestamp();
        // Internal fork - report frame begin
        master_th->th.th_frame_time = tmp_time;
        if (__kmp_forkjoin_frames_mode == 3)
          team->t.t_region_time = tmp_time;
      } else
// only one notification scheme (either "submit" or "forking/joined", not both)
#endif /* USE_ITT_NOTIFY */
          if ((__itt_frame_begin_v3_ptr || KMP_ITT_DEBUG) &&
              __kmp_forkjoin_frames && !__kmp_forkjoin_frames_mode) {
        // Mark start of "parallel" region for Intel(R) VTune(TM) analyzer.
        __kmp_itt_region_forking(gtid, team->t.t_nproc, 0);
      }
    }
#endif /* USE_ITT_BUILD */

    /* now go on and do the work */
    KMP_DEBUG_ASSERT(team == __kmp_threads[gtid]->th.th_team);
    KMP_MB();
    KF_TRACE(10,
             ("__kmp_internal_fork : root=%p, team=%p, master_th=%p, gtid=%d\n",
              root, team, master_th, gtid));

#if USE_ITT_BUILD
    if (__itt_stack_caller_create_ptr) {
      team->t.t_stack_id =
          __kmp_itt_stack_caller_create(); // create new stack stitching id
      // before entering fork barrier
    }
#endif /* USE_ITT_BUILD */

#if OMP_40_ENABLED
    // AC: skip __kmp_internal_fork at teams construct, let only master
    // threads execute
    if (ap)
#endif /* OMP_40_ENABLED */
    {
      __kmp_internal_fork(loc, gtid, team);
      KF_TRACE(10, ("__kmp_internal_fork : after : root=%p, team=%p, "
                    "master_th=%p, gtid=%d\n",
                    root, team, master_th, gtid));
    }

    if (call_context == fork_context_gnu) {
      KA_TRACE(20, ("__kmp_fork_call: parallel exit T#%d\n", gtid));
      return TRUE;
    }

    /* Invoke microtask for MASTER thread */
    KA_TRACE(20, ("__kmp_fork_call: T#%d(%d:0) invoke microtask = %p\n", gtid,
                  team->t.t_id, team->t.t_pkfn));
  } // END of timer KMP_fork_call block

  if (!team->t.t_invoke(gtid)) {
    KMP_ASSERT2(0, "cannot invoke microtask for MASTER thread");
  }
  KA_TRACE(20, ("__kmp_fork_call: T#%d(%d:0) done microtask = %p\n", gtid,
                team->t.t_id, team->t.t_pkfn));
  KMP_MB(); /* Flush all pending memory write invalidates.  */

  KA_TRACE(20, ("__kmp_fork_call: parallel exit T#%d\n", gtid));

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    master_th->th.ompt_thread_info.state = ompt_state_overhead;
  }
#endif

  return TRUE;
}

#if OMPT_SUPPORT
static inline void __kmp_join_restore_state(kmp_info_t *thread,
                                            kmp_team_t *team) {
  // restore state outside the region
  thread->th.ompt_thread_info.state =
      ((team->t.t_serialized) ? ompt_state_work_serial
                              : ompt_state_work_parallel);
}

static inline void __kmp_join_ompt(int gtid, kmp_info_t *thread,
                                   kmp_team_t *team, ompt_data_t *parallel_data,
                                   fork_context_e fork_context, void *codeptr) {
  ompt_task_info_t *task_info = __ompt_get_task_info_object(0);
  if (ompt_enabled.ompt_callback_parallel_end) {
    ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
        parallel_data, &(task_info->task_data), OMPT_INVOKER(fork_context),
        codeptr);
  }

  task_info->frame.enter_frame = ompt_data_none;
  __kmp_join_restore_state(thread, team);
}
#endif

void __kmp_join_call(ident_t *loc, int gtid
#if OMPT_SUPPORT
                     ,
                     enum fork_context_e fork_context
#endif
#if OMP_40_ENABLED
                     ,
                     int exit_teams
#endif /* OMP_40_ENABLED */
                     ) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_join_call);
  kmp_team_t *team;
  kmp_team_t *parent_team;
  kmp_info_t *master_th;
  kmp_root_t *root;
  int master_active;
  int i;

  KA_TRACE(20, ("__kmp_join_call: enter T#%d\n", gtid));

  /* setup current data */
  master_th = __kmp_threads[gtid];
  root = master_th->th.th_root;
  team = master_th->th.th_team;
  parent_team = team->t.t_parent;

  master_th->th.th_ident = loc;

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    master_th->th.ompt_thread_info.state = ompt_state_overhead;
  }
#endif

#if KMP_DEBUG
  if (__kmp_tasking_mode != tskm_immediate_exec && !exit_teams) {
    KA_TRACE(20, ("__kmp_join_call: T#%d, old team = %p old task_team = %p, "
                  "th_task_team = %p\n",
                  __kmp_gtid_from_thread(master_th), team,
                  team->t.t_task_team[master_th->th.th_task_state],
                  master_th->th.th_task_team));
    KMP_DEBUG_ASSERT(master_th->th.th_task_team ==
                     team->t.t_task_team[master_th->th.th_task_state]);
  }
#endif

  if (team->t.t_serialized) {
#if OMP_40_ENABLED
    if (master_th->th.th_teams_microtask) {
      // We are in teams construct
      int level = team->t.t_level;
      int tlevel = master_th->th.th_teams_level;
      if (level == tlevel) {
        // AC: we haven't incremented it earlier at start of teams construct,
        //     so do it here - at the end of teams construct
        team->t.t_level++;
      } else if (level == tlevel + 1) {
        // AC: we are exiting parallel inside teams, need to increment
        // serialization in order to restore it in the next call to
        // __kmpc_end_serialized_parallel
        team->t.t_serialized++;
      }
    }
#endif /* OMP_40_ENABLED */
    __kmpc_end_serialized_parallel(loc, gtid);

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      __kmp_join_restore_state(master_th, parent_team);
    }
#endif

    return;
  }

  master_active = team->t.t_master_active;

#if OMP_40_ENABLED
  if (!exit_teams)
#endif /* OMP_40_ENABLED */
  {
    // AC: No barrier for internal teams at exit from teams construct.
    //     But there is barrier for external team (league).
    __kmp_internal_join(loc, gtid, team);
  }
#if OMP_40_ENABLED
  else {
    master_th->th.th_task_state =
        0; // AC: no tasking in teams (out of any parallel)
  }
#endif /* OMP_40_ENABLED */

  KMP_MB();

#if OMPT_SUPPORT
  ompt_data_t *parallel_data = &(team->t.ompt_team_info.parallel_data);
  void *codeptr = team->t.ompt_team_info.master_return_address;
#endif

#if USE_ITT_BUILD
  if (__itt_stack_caller_create_ptr) {
    __kmp_itt_stack_caller_destroy(
        (__itt_caller)team->t
            .t_stack_id); // destroy the stack stitching id after join barrier
  }

  // Mark end of "parallel" region for Intel(R) VTune(TM) analyzer.
  if (team->t.t_active_level == 1
#if OMP_40_ENABLED
      && !master_th->th.th_teams_microtask /* not in teams construct */
#endif /* OMP_40_ENABLED */
      ) {
    master_th->th.th_ident = loc;
    // only one notification scheme (either "submit" or "forking/joined", not
    // both)
    if ((__itt_frame_submit_v3_ptr || KMP_ITT_DEBUG) &&
        __kmp_forkjoin_frames_mode == 3)
      __kmp_itt_frame_submit(gtid, team->t.t_region_time,
                             master_th->th.th_frame_time, 0, loc,
                             master_th->th.th_team_nproc, 1);
    else if ((__itt_frame_end_v3_ptr || KMP_ITT_DEBUG) &&
             !__kmp_forkjoin_frames_mode && __kmp_forkjoin_frames)
      __kmp_itt_region_joined(gtid);
  } // active_level == 1
#endif /* USE_ITT_BUILD */

#if OMP_40_ENABLED
  if (master_th->th.th_teams_microtask && !exit_teams &&
      team->t.t_pkfn != (microtask_t)__kmp_teams_master &&
      team->t.t_level == master_th->th.th_teams_level + 1) {
    // AC: We need to leave the team structure intact at the end of parallel
    // inside the teams construct, so that at the next parallel same (hot) team
    // works, only adjust nesting levels

    /* Decrement our nested depth level */
    team->t.t_level--;
    team->t.t_active_level--;
    KMP_ATOMIC_DEC(&root->r.r_in_parallel);

    /* Restore number of threads in the team if needed */
    if (master_th->th.th_team_nproc < master_th->th.th_teams_size.nth) {
      int old_num = master_th->th.th_team_nproc;
      int new_num = master_th->th.th_teams_size.nth;
      kmp_info_t **other_threads = team->t.t_threads;
      team->t.t_nproc = new_num;
      for (i = 0; i < old_num; ++i) {
        other_threads[i]->th.th_team_nproc = new_num;
      }
      // Adjust states of non-used threads of the team
      for (i = old_num; i < new_num; ++i) {
        // Re-initialize thread's barrier data.
        int b;
        kmp_balign_t *balign = other_threads[i]->th.th_bar;
        for (b = 0; b < bs_last_barrier; ++b) {
          balign[b].bb.b_arrived = team->t.t_bar[b].b_arrived;
          KMP_DEBUG_ASSERT(balign[b].bb.wait_flag != KMP_BARRIER_PARENT_FLAG);
#if USE_DEBUGGER
          balign[b].bb.b_worker_arrived = team->t.t_bar[b].b_team_arrived;
#endif
        }
        if (__kmp_tasking_mode != tskm_immediate_exec) {
          // Synchronize thread's task state
          other_threads[i]->th.th_task_state = master_th->th.th_task_state;
        }
      }
    }

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      __kmp_join_ompt(gtid, master_th, parent_team, parallel_data, fork_context,
                      codeptr);
    }
#endif

    return;
  }
#endif /* OMP_40_ENABLED */

  /* do cleanup and restore the parent team */
  master_th->th.th_info.ds.ds_tid = team->t.t_master_tid;
  master_th->th.th_local.this_construct = team->t.t_master_this_cons;

  master_th->th.th_dispatch = &parent_team->t.t_dispatch[team->t.t_master_tid];

  /* jc: The following lock has instructions with REL and ACQ semantics,
     separating the parallel user code called in this parallel region
     from the serial user code called after this function returns. */
  __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);

#if OMP_40_ENABLED
  if (!master_th->th.th_teams_microtask ||
      team->t.t_level > master_th->th.th_teams_level)
#endif /* OMP_40_ENABLED */
  {
    /* Decrement our nested depth level */
    KMP_ATOMIC_DEC(&root->r.r_in_parallel);
  }
  KMP_DEBUG_ASSERT(root->r.r_in_parallel >= 0);

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    ompt_task_info_t *task_info = __ompt_get_task_info_object(0);
    if (ompt_enabled.ompt_callback_implicit_task) {
      int ompt_team_size = team->t.t_nproc;
      ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
          ompt_scope_end, NULL, &(task_info->task_data), ompt_team_size,
          OMPT_CUR_TASK_INFO(master_th)->thread_num, ompt_task_implicit); // TODO: Can this be ompt_task_initial?
    }

    task_info->frame.exit_frame = ompt_data_none;
    task_info->task_data = ompt_data_none;
  }
#endif

  KF_TRACE(10, ("__kmp_join_call1: T#%d, this_thread=%p team=%p\n", 0,
                master_th, team));
  __kmp_pop_current_task_from_thread(master_th);

#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
  // Restore master thread's partition.
  master_th->th.th_first_place = team->t.t_first_place;
  master_th->th.th_last_place = team->t.t_last_place;
#endif /* OMP_40_ENABLED */
#if OMP_50_ENABLED
  master_th->th.th_def_allocator = team->t.t_def_allocator;
#endif

  updateHWFPControl(team);

  if (root->r.r_active != master_active)
    root->r.r_active = master_active;

  __kmp_free_team(root, team USE_NESTED_HOT_ARG(
                            master_th)); // this will free worker threads

  /* this race was fun to find. make sure the following is in the critical
     region otherwise assertions may fail occasionally since the old team may be
     reallocated and the hierarchy appears inconsistent. it is actually safe to
     run and won't cause any bugs, but will cause those assertion failures. it's
     only one deref&assign so might as well put this in the critical region */
  master_th->th.th_team = parent_team;
  master_th->th.th_team_nproc = parent_team->t.t_nproc;
  master_th->th.th_team_master = parent_team->t.t_threads[0];
  master_th->th.th_team_serialized = parent_team->t.t_serialized;

  /* restore serialized team, if need be */
  if (parent_team->t.t_serialized &&
      parent_team != master_th->th.th_serial_team &&
      parent_team != root->r.r_root_team) {
    __kmp_free_team(root,
                    master_th->th.th_serial_team USE_NESTED_HOT_ARG(NULL));
    master_th->th.th_serial_team = parent_team;
  }

  if (__kmp_tasking_mode != tskm_immediate_exec) {
    if (master_th->th.th_task_state_top >
        0) { // Restore task state from memo stack
      KMP_DEBUG_ASSERT(master_th->th.th_task_state_memo_stack);
      // Remember master's state if we re-use this nested hot team
      master_th->th.th_task_state_memo_stack[master_th->th.th_task_state_top] =
          master_th->th.th_task_state;
      --master_th->th.th_task_state_top; // pop
      // Now restore state at this level
      master_th->th.th_task_state =
          master_th->th
              .th_task_state_memo_stack[master_th->th.th_task_state_top];
    }
    // Copy the task team from the parent team to the master thread
    master_th->th.th_task_team =
        parent_team->t.t_task_team[master_th->th.th_task_state];
    KA_TRACE(20,
             ("__kmp_join_call: Master T#%d restoring task_team %p / team %p\n",
              __kmp_gtid_from_thread(master_th), master_th->th.th_task_team,
              parent_team));
  }

  // TODO: GEH - cannot do this assertion because root thread not set up as
  // executing
  // KMP_ASSERT( master_th->th.th_current_task->td_flags.executing == 0 );
  master_th->th.th_current_task->td_flags.executing = 1;

  __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    __kmp_join_ompt(gtid, master_th, parent_team, parallel_data, fork_context,
                    codeptr);
  }
#endif

  KMP_MB();
  KA_TRACE(20, ("__kmp_join_call: exit T#%d\n", gtid));
}

/* Check whether we should push an internal control record onto the
   serial team stack.  If so, do it.  */
void __kmp_save_internal_controls(kmp_info_t *thread) {

  if (thread->th.th_team != thread->th.th_serial_team) {
    return;
  }
  if (thread->th.th_team->t.t_serialized > 1) {
    int push = 0;

    if (thread->th.th_team->t.t_control_stack_top == NULL) {
      push = 1;
    } else {
      if (thread->th.th_team->t.t_control_stack_top->serial_nesting_level !=
          thread->th.th_team->t.t_serialized) {
        push = 1;
      }
    }
    if (push) { /* push a record on the serial team's stack */
      kmp_internal_control_t *control =
          (kmp_internal_control_t *)__kmp_allocate(
              sizeof(kmp_internal_control_t));

      copy_icvs(control, &thread->th.th_current_task->td_icvs);

      control->serial_nesting_level = thread->th.th_team->t.t_serialized;

      control->next = thread->th.th_team->t.t_control_stack_top;
      thread->th.th_team->t.t_control_stack_top = control;
    }
  }
}

/* Changes set_nproc */
void __kmp_set_num_threads(int new_nth, int gtid) {
  kmp_info_t *thread;
  kmp_root_t *root;

  KF_TRACE(10, ("__kmp_set_num_threads: new __kmp_nth = %d\n", new_nth));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  if (new_nth < 1)
    new_nth = 1;
  else if (new_nth > __kmp_max_nth)
    new_nth = __kmp_max_nth;

  KMP_COUNT_VALUE(OMP_set_numthreads, new_nth);
  thread = __kmp_threads[gtid];
  if (thread->th.th_current_task->td_icvs.nproc == new_nth)
    return; // nothing to do

  __kmp_save_internal_controls(thread);

  set__nproc(thread, new_nth);

  // If this omp_set_num_threads() call will cause the hot team size to be
  // reduced (in the absence of a num_threads clause), then reduce it now,
  // rather than waiting for the next parallel region.
  root = thread->th.th_root;
  if (__kmp_init_parallel && (!root->r.r_active) &&
      (root->r.r_hot_team->t.t_nproc > new_nth)
#if KMP_NESTED_HOT_TEAMS
      && __kmp_hot_teams_max_level && !__kmp_hot_teams_mode
#endif
      ) {
    kmp_team_t *hot_team = root->r.r_hot_team;
    int f;

    __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);

    // Release the extra threads we don't need any more.
    for (f = new_nth; f < hot_team->t.t_nproc; f++) {
      KMP_DEBUG_ASSERT(hot_team->t.t_threads[f] != NULL);
      if (__kmp_tasking_mode != tskm_immediate_exec) {
        // When decreasing team size, threads no longer in the team should unref
        // task team.
        hot_team->t.t_threads[f]->th.th_task_team = NULL;
      }
      __kmp_free_thread(hot_team->t.t_threads[f]);
      hot_team->t.t_threads[f] = NULL;
    }
    hot_team->t.t_nproc = new_nth;
#if KMP_NESTED_HOT_TEAMS
    if (thread->th.th_hot_teams) {
      KMP_DEBUG_ASSERT(hot_team == thread->th.th_hot_teams[0].hot_team);
      thread->th.th_hot_teams[0].hot_team_nth = new_nth;
    }
#endif

    __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);

    // Update the t_nproc field in the threads that are still active.
    for (f = 0; f < new_nth; f++) {
      KMP_DEBUG_ASSERT(hot_team->t.t_threads[f] != NULL);
      hot_team->t.t_threads[f]->th.th_team_nproc = new_nth;
    }
    // Special flag in case omp_set_num_threads() call
    hot_team->t.t_size_changed = -1;
  }
}

/* Changes max_active_levels */
void __kmp_set_max_active_levels(int gtid, int max_active_levels) {
  kmp_info_t *thread;

  KF_TRACE(10, ("__kmp_set_max_active_levels: new max_active_levels for thread "
                "%d = (%d)\n",
                gtid, max_active_levels));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  // validate max_active_levels
  if (max_active_levels < 0) {
    KMP_WARNING(ActiveLevelsNegative, max_active_levels);
    // We ignore this call if the user has specified a negative value.
    // The current setting won't be changed. The last valid setting will be
    // used. A warning will be issued (if warnings are allowed as controlled by
    // the KMP_WARNINGS env var).
    KF_TRACE(10, ("__kmp_set_max_active_levels: the call is ignored: new "
                  "max_active_levels for thread %d = (%d)\n",
                  gtid, max_active_levels));
    return;
  }
  if (max_active_levels <= KMP_MAX_ACTIVE_LEVELS_LIMIT) {
    // it's OK, the max_active_levels is within the valid range: [ 0;
    // KMP_MAX_ACTIVE_LEVELS_LIMIT ]
    // We allow a zero value. (implementation defined behavior)
  } else {
    KMP_WARNING(ActiveLevelsExceedLimit, max_active_levels,
                KMP_MAX_ACTIVE_LEVELS_LIMIT);
    max_active_levels = KMP_MAX_ACTIVE_LEVELS_LIMIT;
    // Current upper limit is MAX_INT. (implementation defined behavior)
    // If the input exceeds the upper limit, we correct the input to be the
    // upper limit. (implementation defined behavior)
    // Actually, the flow should never get here until we use MAX_INT limit.
  }
  KF_TRACE(10, ("__kmp_set_max_active_levels: after validation: new "
                "max_active_levels for thread %d = (%d)\n",
                gtid, max_active_levels));

  thread = __kmp_threads[gtid];

  __kmp_save_internal_controls(thread);

  set__max_active_levels(thread, max_active_levels);
}

/* Gets max_active_levels */
int __kmp_get_max_active_levels(int gtid) {
  kmp_info_t *thread;

  KF_TRACE(10, ("__kmp_get_max_active_levels: thread %d\n", gtid));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  thread = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(thread->th.th_current_task);
  KF_TRACE(10, ("__kmp_get_max_active_levels: thread %d, curtask=%p, "
                "curtask_maxaclevel=%d\n",
                gtid, thread->th.th_current_task,
                thread->th.th_current_task->td_icvs.max_active_levels));
  return thread->th.th_current_task->td_icvs.max_active_levels;
}

/* Changes def_sched_var ICV values (run-time schedule kind and chunk) */
void __kmp_set_schedule(int gtid, kmp_sched_t kind, int chunk) {
  kmp_info_t *thread;
  //    kmp_team_t *team;

  KF_TRACE(10, ("__kmp_set_schedule: new schedule for thread %d = (%d, %d)\n",
                gtid, (int)kind, chunk));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  // Check if the kind parameter is valid, correct if needed.
  // Valid parameters should fit in one of two intervals - standard or extended:
  //       <lower>, <valid>, <upper_std>, <lower_ext>, <valid>, <upper>
  // 2008-01-25: 0,  1 - 4,       5,         100,     101 - 102, 103
  if (kind <= kmp_sched_lower || kind >= kmp_sched_upper ||
      (kind <= kmp_sched_lower_ext && kind >= kmp_sched_upper_std)) {
    // TODO: Hint needs attention in case we change the default schedule.
    __kmp_msg(kmp_ms_warning, KMP_MSG(ScheduleKindOutOfRange, kind),
              KMP_HNT(DefaultScheduleKindUsed, "static, no chunk"),
              __kmp_msg_null);
    kind = kmp_sched_default;
    chunk = 0; // ignore chunk value in case of bad kind
  }

  thread = __kmp_threads[gtid];

  __kmp_save_internal_controls(thread);

  if (kind < kmp_sched_upper_std) {
    if (kind == kmp_sched_static && chunk < KMP_DEFAULT_CHUNK) {
      // differ static chunked vs. unchunked:  chunk should be invalid to
      // indicate unchunked schedule (which is the default)
      thread->th.th_current_task->td_icvs.sched.r_sched_type = kmp_sch_static;
    } else {
      thread->th.th_current_task->td_icvs.sched.r_sched_type =
          __kmp_sch_map[kind - kmp_sched_lower - 1];
    }
  } else {
    //    __kmp_sch_map[ kind - kmp_sched_lower_ext + kmp_sched_upper_std -
    //    kmp_sched_lower - 2 ];
    thread->th.th_current_task->td_icvs.sched.r_sched_type =
        __kmp_sch_map[kind - kmp_sched_lower_ext + kmp_sched_upper_std -
                      kmp_sched_lower - 2];
  }
  if (kind == kmp_sched_auto || chunk < 1) {
    // ignore parameter chunk for schedule auto
    thread->th.th_current_task->td_icvs.sched.chunk = KMP_DEFAULT_CHUNK;
  } else {
    thread->th.th_current_task->td_icvs.sched.chunk = chunk;
  }
}

/* Gets def_sched_var ICV values */
void __kmp_get_schedule(int gtid, kmp_sched_t *kind, int *chunk) {
  kmp_info_t *thread;
  enum sched_type th_type;

  KF_TRACE(10, ("__kmp_get_schedule: thread %d\n", gtid));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  thread = __kmp_threads[gtid];

  th_type = thread->th.th_current_task->td_icvs.sched.r_sched_type;

  switch (th_type) {
  case kmp_sch_static:
  case kmp_sch_static_greedy:
  case kmp_sch_static_balanced:
    *kind = kmp_sched_static;
    *chunk = 0; // chunk was not set, try to show this fact via zero value
    return;
  case kmp_sch_static_chunked:
    *kind = kmp_sched_static;
    break;
  case kmp_sch_dynamic_chunked:
    *kind = kmp_sched_dynamic;
    break;
  case kmp_sch_guided_chunked:
  case kmp_sch_guided_iterative_chunked:
  case kmp_sch_guided_analytical_chunked:
    *kind = kmp_sched_guided;
    break;
  case kmp_sch_auto:
    *kind = kmp_sched_auto;
    break;
  case kmp_sch_trapezoidal:
    *kind = kmp_sched_trapezoidal;
    break;
#if KMP_STATIC_STEAL_ENABLED
  case kmp_sch_static_steal:
    *kind = kmp_sched_static_steal;
    break;
#endif
  default:
    KMP_FATAL(UnknownSchedulingType, th_type);
  }

  *chunk = thread->th.th_current_task->td_icvs.sched.chunk;
}

int __kmp_get_ancestor_thread_num(int gtid, int level) {

  int ii, dd;
  kmp_team_t *team;
  kmp_info_t *thr;

  KF_TRACE(10, ("__kmp_get_ancestor_thread_num: thread %d %d\n", gtid, level));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  // validate level
  if (level == 0)
    return 0;
  if (level < 0)
    return -1;
  thr = __kmp_threads[gtid];
  team = thr->th.th_team;
  ii = team->t.t_level;
  if (level > ii)
    return -1;

#if OMP_40_ENABLED
  if (thr->th.th_teams_microtask) {
    // AC: we are in teams region where multiple nested teams have same level
    int tlevel = thr->th.th_teams_level; // the level of the teams construct
    if (level <=
        tlevel) { // otherwise usual algorithm works (will not touch the teams)
      KMP_DEBUG_ASSERT(ii >= tlevel);
      // AC: As we need to pass by the teams league, we need to artificially
      // increase ii
      if (ii == tlevel) {
        ii += 2; // three teams have same level
      } else {
        ii++; // two teams have same level
      }
    }
  }
#endif

  if (ii == level)
    return __kmp_tid_from_gtid(gtid);

  dd = team->t.t_serialized;
  level++;
  while (ii > level) {
    for (dd = team->t.t_serialized; (dd > 0) && (ii > level); dd--, ii--) {
    }
    if ((team->t.t_serialized) && (!dd)) {
      team = team->t.t_parent;
      continue;
    }
    if (ii > level) {
      team = team->t.t_parent;
      dd = team->t.t_serialized;
      ii--;
    }
  }

  return (dd > 1) ? (0) : (team->t.t_master_tid);
}

int __kmp_get_team_size(int gtid, int level) {

  int ii, dd;
  kmp_team_t *team;
  kmp_info_t *thr;

  KF_TRACE(10, ("__kmp_get_team_size: thread %d %d\n", gtid, level));
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  // validate level
  if (level == 0)
    return 1;
  if (level < 0)
    return -1;
  thr = __kmp_threads[gtid];
  team = thr->th.th_team;
  ii = team->t.t_level;
  if (level > ii)
    return -1;

#if OMP_40_ENABLED
  if (thr->th.th_teams_microtask) {
    // AC: we are in teams region where multiple nested teams have same level
    int tlevel = thr->th.th_teams_level; // the level of the teams construct
    if (level <=
        tlevel) { // otherwise usual algorithm works (will not touch the teams)
      KMP_DEBUG_ASSERT(ii >= tlevel);
      // AC: As we need to pass by the teams league, we need to artificially
      // increase ii
      if (ii == tlevel) {
        ii += 2; // three teams have same level
      } else {
        ii++; // two teams have same level
      }
    }
  }
#endif

  while (ii > level) {
    for (dd = team->t.t_serialized; (dd > 0) && (ii > level); dd--, ii--) {
    }
    if (team->t.t_serialized && (!dd)) {
      team = team->t.t_parent;
      continue;
    }
    if (ii > level) {
      team = team->t.t_parent;
      ii--;
    }
  }

  return team->t.t_nproc;
}

kmp_r_sched_t __kmp_get_schedule_global() {
  // This routine created because pairs (__kmp_sched, __kmp_chunk) and
  // (__kmp_static, __kmp_guided) may be changed by kmp_set_defaults
  // independently. So one can get the updated schedule here.

  kmp_r_sched_t r_sched;

  // create schedule from 4 globals: __kmp_sched, __kmp_chunk, __kmp_static,
  // __kmp_guided. __kmp_sched should keep original value, so that user can set
  // KMP_SCHEDULE multiple times, and thus have different run-time schedules in
  // different roots (even in OMP 2.5)
  if (__kmp_sched == kmp_sch_static) {
    // replace STATIC with more detailed schedule (balanced or greedy)
    r_sched.r_sched_type = __kmp_static;
  } else if (__kmp_sched == kmp_sch_guided_chunked) {
    // replace GUIDED with more detailed schedule (iterative or analytical)
    r_sched.r_sched_type = __kmp_guided;
  } else { // (STATIC_CHUNKED), or (DYNAMIC_CHUNKED), or other
    r_sched.r_sched_type = __kmp_sched;
  }

  if (__kmp_chunk < KMP_DEFAULT_CHUNK) {
    // __kmp_chunk may be wrong here (if it was not ever set)
    r_sched.chunk = KMP_DEFAULT_CHUNK;
  } else {
    r_sched.chunk = __kmp_chunk;
  }

  return r_sched;
}

/* Allocate (realloc == FALSE) * or reallocate (realloc == TRUE)
   at least argc number of *t_argv entries for the requested team. */
static void __kmp_alloc_argv_entries(int argc, kmp_team_t *team, int realloc) {

  KMP_DEBUG_ASSERT(team);
  if (!realloc || argc > team->t.t_max_argc) {

    KA_TRACE(100, ("__kmp_alloc_argv_entries: team %d: needed entries=%d, "
                   "current entries=%d\n",
                   team->t.t_id, argc, (realloc) ? team->t.t_max_argc : 0));
    /* if previously allocated heap space for args, free them */
    if (realloc && team->t.t_argv != &team->t.t_inline_argv[0])
      __kmp_free((void *)team->t.t_argv);

    if (argc <= KMP_INLINE_ARGV_ENTRIES) {
      /* use unused space in the cache line for arguments */
      team->t.t_max_argc = KMP_INLINE_ARGV_ENTRIES;
      KA_TRACE(100, ("__kmp_alloc_argv_entries: team %d: inline allocate %d "
                     "argv entries\n",
                     team->t.t_id, team->t.t_max_argc));
      team->t.t_argv = &team->t.t_inline_argv[0];
      if (__kmp_storage_map) {
        __kmp_print_storage_map_gtid(
            -1, &team->t.t_inline_argv[0],
            &team->t.t_inline_argv[KMP_INLINE_ARGV_ENTRIES],
            (sizeof(void *) * KMP_INLINE_ARGV_ENTRIES), "team_%d.t_inline_argv",
            team->t.t_id);
      }
    } else {
      /* allocate space for arguments in the heap */
      team->t.t_max_argc = (argc <= (KMP_MIN_MALLOC_ARGV_ENTRIES >> 1))
                               ? KMP_MIN_MALLOC_ARGV_ENTRIES
                               : 2 * argc;
      KA_TRACE(100, ("__kmp_alloc_argv_entries: team %d: dynamic allocate %d "
                     "argv entries\n",
                     team->t.t_id, team->t.t_max_argc));
      team->t.t_argv =
          (void **)__kmp_page_allocate(sizeof(void *) * team->t.t_max_argc);
      if (__kmp_storage_map) {
        __kmp_print_storage_map_gtid(-1, &team->t.t_argv[0],
                                     &team->t.t_argv[team->t.t_max_argc],
                                     sizeof(void *) * team->t.t_max_argc,
                                     "team_%d.t_argv", team->t.t_id);
      }
    }
  }
}

static void __kmp_allocate_team_arrays(kmp_team_t *team, int max_nth) {
  int i;
  int num_disp_buff = max_nth > 1 ? __kmp_dispatch_num_buffers : 2;
  team->t.t_threads =
      (kmp_info_t **)__kmp_allocate(sizeof(kmp_info_t *) * max_nth);
  team->t.t_disp_buffer = (dispatch_shared_info_t *)__kmp_allocate(
      sizeof(dispatch_shared_info_t) * num_disp_buff);
  team->t.t_dispatch =
      (kmp_disp_t *)__kmp_allocate(sizeof(kmp_disp_t) * max_nth);
  team->t.t_implicit_task_taskdata =
      (kmp_taskdata_t *)__kmp_allocate(sizeof(kmp_taskdata_t) * max_nth);
  team->t.t_max_nproc = max_nth;

  /* setup dispatch buffers */
  for (i = 0; i < num_disp_buff; ++i) {
    team->t.t_disp_buffer[i].buffer_index = i;
#if OMP_45_ENABLED
    team->t.t_disp_buffer[i].doacross_buf_idx = i;
#endif
  }
}

static void __kmp_free_team_arrays(kmp_team_t *team) {
  /* Note: this does not free the threads in t_threads (__kmp_free_threads) */
  int i;
  for (i = 0; i < team->t.t_max_nproc; ++i) {
    if (team->t.t_dispatch[i].th_disp_buffer != NULL) {
      __kmp_free(team->t.t_dispatch[i].th_disp_buffer);
      team->t.t_dispatch[i].th_disp_buffer = NULL;
    }
  }
#if KMP_USE_HIER_SCHED
  __kmp_dispatch_free_hierarchies(team);
#endif
  __kmp_free(team->t.t_threads);
  __kmp_free(team->t.t_disp_buffer);
  __kmp_free(team->t.t_dispatch);
  __kmp_free(team->t.t_implicit_task_taskdata);
  team->t.t_threads = NULL;
  team->t.t_disp_buffer = NULL;
  team->t.t_dispatch = NULL;
  team->t.t_implicit_task_taskdata = 0;
}

static void __kmp_reallocate_team_arrays(kmp_team_t *team, int max_nth) {
  kmp_info_t **oldThreads = team->t.t_threads;

  __kmp_free(team->t.t_disp_buffer);
  __kmp_free(team->t.t_dispatch);
  __kmp_free(team->t.t_implicit_task_taskdata);
  __kmp_allocate_team_arrays(team, max_nth);

  KMP_MEMCPY(team->t.t_threads, oldThreads,
             team->t.t_nproc * sizeof(kmp_info_t *));

  __kmp_free(oldThreads);
}

static kmp_internal_control_t __kmp_get_global_icvs(void) {

  kmp_r_sched_t r_sched =
      __kmp_get_schedule_global(); // get current state of scheduling globals

#if OMP_40_ENABLED
  KMP_DEBUG_ASSERT(__kmp_nested_proc_bind.used > 0);
#endif /* OMP_40_ENABLED */

  kmp_internal_control_t g_icvs = {
    0, // int serial_nesting_level; //corresponds to value of th_team_serialized
    (kmp_int8)__kmp_dflt_nested, // int nested; //internal control
    // for nested parallelism (per thread)
    (kmp_int8)__kmp_global.g.g_dynamic, // internal control for dynamic
    // adjustment of threads (per thread)
    (kmp_int8)__kmp_env_blocktime, // int bt_set; //internal control for
    // whether blocktime is explicitly set
    __kmp_dflt_blocktime, // int blocktime; //internal control for blocktime
#if KMP_USE_MONITOR
    __kmp_bt_intervals, // int bt_intervals; //internal control for blocktime
// intervals
#endif
    __kmp_dflt_team_nth, // int nproc; //internal control for # of threads for
    // next parallel region (per thread)
    // (use a max ub on value if __kmp_parallel_initialize not called yet)
    __kmp_dflt_max_active_levels, // int max_active_levels; //internal control
    // for max_active_levels
    r_sched, // kmp_r_sched_t sched; //internal control for runtime schedule
// {sched,chunk} pair
#if OMP_40_ENABLED
    __kmp_nested_proc_bind.bind_types[0],
    __kmp_default_device,
#endif /* OMP_40_ENABLED */
    NULL // struct kmp_internal_control *next;
  };

  return g_icvs;
}

static kmp_internal_control_t __kmp_get_x_global_icvs(const kmp_team_t *team) {

  kmp_internal_control_t gx_icvs;
  gx_icvs.serial_nesting_level =
      0; // probably =team->t.t_serial like in save_inter_controls
  copy_icvs(&gx_icvs, &team->t.t_threads[0]->th.th_current_task->td_icvs);
  gx_icvs.next = NULL;

  return gx_icvs;
}

static void __kmp_initialize_root(kmp_root_t *root) {
  int f;
  kmp_team_t *root_team;
  kmp_team_t *hot_team;
  int hot_team_max_nth;
  kmp_r_sched_t r_sched =
      __kmp_get_schedule_global(); // get current state of scheduling globals
  kmp_internal_control_t r_icvs = __kmp_get_global_icvs();
  KMP_DEBUG_ASSERT(root);
  KMP_ASSERT(!root->r.r_begin);

  /* setup the root state structure */
  __kmp_init_lock(&root->r.r_begin_lock);
  root->r.r_begin = FALSE;
  root->r.r_active = FALSE;
  root->r.r_in_parallel = 0;
  root->r.r_blocktime = __kmp_dflt_blocktime;
  root->r.r_nested = __kmp_dflt_nested;
  root->r.r_cg_nthreads = 1;

  /* setup the root team for this task */
  /* allocate the root team structure */
  KF_TRACE(10, ("__kmp_initialize_root: before root_team\n"));

  root_team =
      __kmp_allocate_team(root,
                          1, // new_nproc
                          1, // max_nproc
#if OMPT_SUPPORT
                          ompt_data_none, // root parallel id
#endif
#if OMP_40_ENABLED
                          __kmp_nested_proc_bind.bind_types[0],
#endif
                          &r_icvs,
                          0 // argc
                          USE_NESTED_HOT_ARG(NULL) // master thread is unknown
                          );
#if USE_DEBUGGER
  // Non-NULL value should be assigned to make the debugger display the root
  // team.
  TCW_SYNC_PTR(root_team->t.t_pkfn, (microtask_t)(~0));
#endif

  KF_TRACE(10, ("__kmp_initialize_root: after root_team = %p\n", root_team));

  root->r.r_root_team = root_team;
  root_team->t.t_control_stack_top = NULL;

  /* initialize root team */
  root_team->t.t_threads[0] = NULL;
  root_team->t.t_nproc = 1;
  root_team->t.t_serialized = 1;
  // TODO???: root_team->t.t_max_active_levels = __kmp_dflt_max_active_levels;
  root_team->t.t_sched.sched = r_sched.sched;
  KA_TRACE(
      20,
      ("__kmp_initialize_root: init root team %d arrived: join=%u, plain=%u\n",
       root_team->t.t_id, KMP_INIT_BARRIER_STATE, KMP_INIT_BARRIER_STATE));

  /* setup the  hot team for this task */
  /* allocate the hot team structure */
  KF_TRACE(10, ("__kmp_initialize_root: before hot_team\n"));

  hot_team =
      __kmp_allocate_team(root,
                          1, // new_nproc
                          __kmp_dflt_team_nth_ub * 2, // max_nproc
#if OMPT_SUPPORT
                          ompt_data_none, // root parallel id
#endif
#if OMP_40_ENABLED
                          __kmp_nested_proc_bind.bind_types[0],
#endif
                          &r_icvs,
                          0 // argc
                          USE_NESTED_HOT_ARG(NULL) // master thread is unknown
                          );
  KF_TRACE(10, ("__kmp_initialize_root: after hot_team = %p\n", hot_team));

  root->r.r_hot_team = hot_team;
  root_team->t.t_control_stack_top = NULL;

  /* first-time initialization */
  hot_team->t.t_parent = root_team;

  /* initialize hot team */
  hot_team_max_nth = hot_team->t.t_max_nproc;
  for (f = 0; f < hot_team_max_nth; ++f) {
    hot_team->t.t_threads[f] = NULL;
  }
  hot_team->t.t_nproc = 1;
  // TODO???: hot_team->t.t_max_active_levels = __kmp_dflt_max_active_levels;
  hot_team->t.t_sched.sched = r_sched.sched;
  hot_team->t.t_size_changed = 0;
}

#ifdef KMP_DEBUG

typedef struct kmp_team_list_item {
  kmp_team_p const *entry;
  struct kmp_team_list_item *next;
} kmp_team_list_item_t;
typedef kmp_team_list_item_t *kmp_team_list_t;

static void __kmp_print_structure_team_accum( // Add team to list of teams.
    kmp_team_list_t list, // List of teams.
    kmp_team_p const *team // Team to add.
    ) {

  // List must terminate with item where both entry and next are NULL.
  // Team is added to the list only once.
  // List is sorted in ascending order by team id.
  // Team id is *not* a key.

  kmp_team_list_t l;

  KMP_DEBUG_ASSERT(list != NULL);
  if (team == NULL) {
    return;
  }

  __kmp_print_structure_team_accum(list, team->t.t_parent);
  __kmp_print_structure_team_accum(list, team->t.t_next_pool);

  // Search list for the team.
  l = list;
  while (l->next != NULL && l->entry != team) {
    l = l->next;
  }
  if (l->next != NULL) {
    return; // Team has been added before, exit.
  }

  // Team is not found. Search list again for insertion point.
  l = list;
  while (l->next != NULL && l->entry->t.t_id <= team->t.t_id) {
    l = l->next;
  }

  // Insert team.
  {
    kmp_team_list_item_t *item = (kmp_team_list_item_t *)KMP_INTERNAL_MALLOC(
        sizeof(kmp_team_list_item_t));
    *item = *l;
    l->entry = team;
    l->next = item;
  }
}

static void __kmp_print_structure_team(char const *title, kmp_team_p const *team

                                       ) {
  __kmp_printf("%s", title);
  if (team != NULL) {
    __kmp_printf("%2x %p\n", team->t.t_id, team);
  } else {
    __kmp_printf(" - (nil)\n");
  }
}

static void __kmp_print_structure_thread(char const *title,
                                         kmp_info_p const *thread) {
  __kmp_printf("%s", title);
  if (thread != NULL) {
    __kmp_printf("%2d %p\n", thread->th.th_info.ds.ds_gtid, thread);
  } else {
    __kmp_printf(" - (nil)\n");
  }
}

void __kmp_print_structure(void) {

  kmp_team_list_t list;

  // Initialize list of teams.
  list =
      (kmp_team_list_item_t *)KMP_INTERNAL_MALLOC(sizeof(kmp_team_list_item_t));
  list->entry = NULL;
  list->next = NULL;

  __kmp_printf("\n------------------------------\nGlobal Thread "
               "Table\n------------------------------\n");
  {
    int gtid;
    for (gtid = 0; gtid < __kmp_threads_capacity; ++gtid) {
      __kmp_printf("%2d", gtid);
      if (__kmp_threads != NULL) {
        __kmp_printf(" %p", __kmp_threads[gtid]);
      }
      if (__kmp_root != NULL) {
        __kmp_printf(" %p", __kmp_root[gtid]);
      }
      __kmp_printf("\n");
    }
  }

  // Print out __kmp_threads array.
  __kmp_printf("\n------------------------------\nThreads\n--------------------"
               "----------\n");
  if (__kmp_threads != NULL) {
    int gtid;
    for (gtid = 0; gtid < __kmp_threads_capacity; ++gtid) {
      kmp_info_t const *thread = __kmp_threads[gtid];
      if (thread != NULL) {
        __kmp_printf("GTID %2d %p:\n", gtid, thread);
        __kmp_printf("    Our Root:        %p\n", thread->th.th_root);
        __kmp_print_structure_team("    Our Team:     ", thread->th.th_team);
        __kmp_print_structure_team("    Serial Team:  ",
                                   thread->th.th_serial_team);
        __kmp_printf("    Threads:      %2d\n", thread->th.th_team_nproc);
        __kmp_print_structure_thread("    Master:       ",
                                     thread->th.th_team_master);
        __kmp_printf("    Serialized?:  %2d\n", thread->th.th_team_serialized);
        __kmp_printf("    Set NProc:    %2d\n", thread->th.th_set_nproc);
#if OMP_40_ENABLED
        __kmp_printf("    Set Proc Bind: %2d\n", thread->th.th_set_proc_bind);
#endif
        __kmp_print_structure_thread("    Next in pool: ",
                                     thread->th.th_next_pool);
        __kmp_printf("\n");
        __kmp_print_structure_team_accum(list, thread->th.th_team);
        __kmp_print_structure_team_accum(list, thread->th.th_serial_team);
      }
    }
  } else {
    __kmp_printf("Threads array is not allocated.\n");
  }

  // Print out __kmp_root array.
  __kmp_printf("\n------------------------------\nUbers\n----------------------"
               "--------\n");
  if (__kmp_root != NULL) {
    int gtid;
    for (gtid = 0; gtid < __kmp_threads_capacity; ++gtid) {
      kmp_root_t const *root = __kmp_root[gtid];
      if (root != NULL) {
        __kmp_printf("GTID %2d %p:\n", gtid, root);
        __kmp_print_structure_team("    Root Team:    ", root->r.r_root_team);
        __kmp_print_structure_team("    Hot Team:     ", root->r.r_hot_team);
        __kmp_print_structure_thread("    Uber Thread:  ",
                                     root->r.r_uber_thread);
        __kmp_printf("    Active?:      %2d\n", root->r.r_active);
        __kmp_printf("    Nested?:      %2d\n", root->r.r_nested);
        __kmp_printf("    In Parallel:  %2d\n",
                     KMP_ATOMIC_LD_RLX(&root->r.r_in_parallel));
        __kmp_printf("\n");
        __kmp_print_structure_team_accum(list, root->r.r_root_team);
        __kmp_print_structure_team_accum(list, root->r.r_hot_team);
      }
    }
  } else {
    __kmp_printf("Ubers array is not allocated.\n");
  }

  __kmp_printf("\n------------------------------\nTeams\n----------------------"
               "--------\n");
  while (list->next != NULL) {
    kmp_team_p const *team = list->entry;
    int i;
    __kmp_printf("Team %2x %p:\n", team->t.t_id, team);
    __kmp_print_structure_team("    Parent Team:      ", team->t.t_parent);
    __kmp_printf("    Master TID:       %2d\n", team->t.t_master_tid);
    __kmp_printf("    Max threads:      %2d\n", team->t.t_max_nproc);
    __kmp_printf("    Levels of serial: %2d\n", team->t.t_serialized);
    __kmp_printf("    Number threads:   %2d\n", team->t.t_nproc);
    for (i = 0; i < team->t.t_nproc; ++i) {
      __kmp_printf("    Thread %2d:      ", i);
      __kmp_print_structure_thread("", team->t.t_threads[i]);
    }
    __kmp_print_structure_team("    Next in pool:     ", team->t.t_next_pool);
    __kmp_printf("\n");
    list = list->next;
  }

  // Print out __kmp_thread_pool and __kmp_team_pool.
  __kmp_printf("\n------------------------------\nPools\n----------------------"
               "--------\n");
  __kmp_print_structure_thread("Thread pool:          ",
                               CCAST(kmp_info_t *, __kmp_thread_pool));
  __kmp_print_structure_team("Team pool:            ",
                             CCAST(kmp_team_t *, __kmp_team_pool));
  __kmp_printf("\n");

  // Free team list.
  while (list != NULL) {
    kmp_team_list_item_t *item = list;
    list = list->next;
    KMP_INTERNAL_FREE(item);
  }
}

#endif

//---------------------------------------------------------------------------
//  Stuff for per-thread fast random number generator
//  Table of primes
static const unsigned __kmp_primes[] = {
    0x9e3779b1, 0xffe6cc59, 0x2109f6dd, 0x43977ab5, 0xba5703f5, 0xb495a877,
    0xe1626741, 0x79695e6b, 0xbc98c09f, 0xd5bee2b3, 0x287488f9, 0x3af18231,
    0x9677cd4d, 0xbe3a6929, 0xadc6a877, 0xdcf0674b, 0xbe4d6fe9, 0x5f15e201,
    0x99afc3fd, 0xf3f16801, 0xe222cfff, 0x24ba5fdb, 0x0620452d, 0x79f149e3,
    0xc8b93f49, 0x972702cd, 0xb07dd827, 0x6c97d5ed, 0x085a3d61, 0x46eb5ea7,
    0x3d9910ed, 0x2e687b5b, 0x29609227, 0x6eb081f1, 0x0954c4e1, 0x9d114db9,
    0x542acfa9, 0xb3e6bd7b, 0x0742d917, 0xe9f3ffa7, 0x54581edb, 0xf2480f45,
    0x0bb9288f, 0xef1affc7, 0x85fa0ca7, 0x3ccc14db, 0xe6baf34b, 0x343377f7,
    0x5ca19031, 0xe6d9293b, 0xf0a9f391, 0x5d2e980b, 0xfc411073, 0xc3749363,
    0xb892d829, 0x3549366b, 0x629750ad, 0xb98294e5, 0x892d9483, 0xc235baf3,
    0x3d2402a3, 0x6bdef3c9, 0xbec333cd, 0x40c9520f};

//---------------------------------------------------------------------------
//  __kmp_get_random: Get a random number using a linear congruential method.
unsigned short __kmp_get_random(kmp_info_t *thread) {
  unsigned x = thread->th.th_x;
  unsigned short r = x >> 16;

  thread->th.th_x = x * thread->th.th_a + 1;

  KA_TRACE(30, ("__kmp_get_random: THREAD: %d, RETURN: %u\n",
                thread->th.th_info.ds.ds_tid, r));

  return r;
}
//--------------------------------------------------------
// __kmp_init_random: Initialize a random number generator
void __kmp_init_random(kmp_info_t *thread) {
  unsigned seed = thread->th.th_info.ds.ds_tid;

  thread->th.th_a =
      __kmp_primes[seed % (sizeof(__kmp_primes) / sizeof(__kmp_primes[0]))];
  thread->th.th_x = (seed + 1) * thread->th.th_a + 1;
  KA_TRACE(30,
           ("__kmp_init_random: THREAD: %u; A: %u\n", seed, thread->th.th_a));
}

#if KMP_OS_WINDOWS
/* reclaim array entries for root threads that are already dead, returns number
 * reclaimed */
static int __kmp_reclaim_dead_roots(void) {
  int i, r = 0;

  for (i = 0; i < __kmp_threads_capacity; ++i) {
    if (KMP_UBER_GTID(i) &&
        !__kmp_still_running((kmp_info_t *)TCR_SYNC_PTR(__kmp_threads[i])) &&
        !__kmp_root[i]
             ->r.r_active) { // AC: reclaim only roots died in non-active state
      r += __kmp_unregister_root_other_thread(i);
    }
  }
  return r;
}
#endif

/* This function attempts to create free entries in __kmp_threads and
   __kmp_root, and returns the number of free entries generated.

   For Windows* OS static library, the first mechanism used is to reclaim array
   entries for root threads that are already dead.

   On all platforms, expansion is attempted on the arrays __kmp_threads_ and
   __kmp_root, with appropriate update to __kmp_threads_capacity. Array
   capacity is increased by doubling with clipping to __kmp_tp_capacity, if
   threadprivate cache array has been created. Synchronization with
   __kmpc_threadprivate_cached is done using __kmp_tp_cached_lock.

   After any dead root reclamation, if the clipping value allows array expansion
   to result in the generation of a total of nNeed free slots, the function does
   that expansion. If not, nothing is done beyond the possible initial root
   thread reclamation.

   If any argument is negative, the behavior is undefined. */
static int __kmp_expand_threads(int nNeed) {
  int added = 0;
  int minimumRequiredCapacity;
  int newCapacity;
  kmp_info_t **newThreads;
  kmp_root_t **newRoot;

// All calls to __kmp_expand_threads should be under __kmp_forkjoin_lock, so
// resizing __kmp_threads does not need additional protection if foreign
// threads are present

#if KMP_OS_WINDOWS && !KMP_DYNAMIC_LIB
  /* only for Windows static library */
  /* reclaim array entries for root threads that are already dead */
  added = __kmp_reclaim_dead_roots();

  if (nNeed) {
    nNeed -= added;
    if (nNeed < 0)
      nNeed = 0;
  }
#endif
  if (nNeed <= 0)
    return added;

  // Note that __kmp_threads_capacity is not bounded by __kmp_max_nth. If
  // __kmp_max_nth is set to some value less than __kmp_sys_max_nth by the
  // user via KMP_DEVICE_THREAD_LIMIT, then __kmp_threads_capacity may become
  // > __kmp_max_nth in one of two ways:
  //
  // 1) The initialization thread (gtid = 0) exits.  __kmp_threads[0]
  //    may not be resused by another thread, so we may need to increase
  //    __kmp_threads_capacity to __kmp_max_nth + 1.
  //
  // 2) New foreign root(s) are encountered.  We always register new foreign
  //    roots. This may cause a smaller # of threads to be allocated at
  //    subsequent parallel regions, but the worker threads hang around (and
  //    eventually go to sleep) and need slots in the __kmp_threads[] array.
  //
  // Anyway, that is the reason for moving the check to see if
  // __kmp_max_nth was exceeded into __kmp_reserve_threads()
  // instead of having it performed here. -BB

  KMP_DEBUG_ASSERT(__kmp_sys_max_nth >= __kmp_threads_capacity);

  /* compute expansion headroom to check if we can expand */
  if (__kmp_sys_max_nth - __kmp_threads_capacity < nNeed) {
    /* possible expansion too small -- give up */
    return added;
  }
  minimumRequiredCapacity = __kmp_threads_capacity + nNeed;

  newCapacity = __kmp_threads_capacity;
  do {
    newCapacity = newCapacity <= (__kmp_sys_max_nth >> 1) ? (newCapacity << 1)
                                                          : __kmp_sys_max_nth;
  } while (newCapacity < minimumRequiredCapacity);
  newThreads = (kmp_info_t **)__kmp_allocate(
      (sizeof(kmp_info_t *) + sizeof(kmp_root_t *)) * newCapacity + CACHE_LINE);
  newRoot =
      (kmp_root_t **)((char *)newThreads + sizeof(kmp_info_t *) * newCapacity);
  KMP_MEMCPY(newThreads, __kmp_threads,
             __kmp_threads_capacity * sizeof(kmp_info_t *));
  KMP_MEMCPY(newRoot, __kmp_root,
             __kmp_threads_capacity * sizeof(kmp_root_t *));

  kmp_info_t **temp_threads = __kmp_threads;
  *(kmp_info_t * *volatile *)&__kmp_threads = newThreads;
  *(kmp_root_t * *volatile *)&__kmp_root = newRoot;
  __kmp_free(temp_threads);
  added += newCapacity - __kmp_threads_capacity;
  *(volatile int *)&__kmp_threads_capacity = newCapacity;

  if (newCapacity > __kmp_tp_capacity) {
    __kmp_acquire_bootstrap_lock(&__kmp_tp_cached_lock);
    if (__kmp_tp_cached && newCapacity > __kmp_tp_capacity) {
      __kmp_threadprivate_resize_cache(newCapacity);
    } else { // increase __kmp_tp_capacity to correspond with kmp_threads size
      *(volatile int *)&__kmp_tp_capacity = newCapacity;
    }
    __kmp_release_bootstrap_lock(&__kmp_tp_cached_lock);
  }

  return added;
}

/* Register the current thread as a root thread and obtain our gtid. We must
   have the __kmp_initz_lock held at this point. Argument TRUE only if are the
   thread that calls from __kmp_do_serial_initialize() */
int __kmp_register_root(int initial_thread) {
  kmp_info_t *root_thread;
  kmp_root_t *root;
  int gtid;
  int capacity;
  __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);
  KA_TRACE(20, ("__kmp_register_root: entered\n"));
  KMP_MB();

  /* 2007-03-02:
     If initial thread did not invoke OpenMP RTL yet, and this thread is not an
     initial one, "__kmp_all_nth >= __kmp_threads_capacity" condition does not
     work as expected -- it may return false (that means there is at least one
     empty slot in __kmp_threads array), but it is possible the only free slot
     is #0, which is reserved for initial thread and so cannot be used for this
     one. Following code workarounds this bug.

     However, right solution seems to be not reserving slot #0 for initial
     thread because:
     (1) there is no magic in slot #0,
     (2) we cannot detect initial thread reliably (the first thread which does
        serial initialization may be not a real initial thread).
  */
  capacity = __kmp_threads_capacity;
  if (!initial_thread && TCR_PTR(__kmp_threads[0]) == NULL) {
    --capacity;
  }

  /* see if there are too many threads */
  if (__kmp_all_nth >= capacity && !__kmp_expand_threads(1)) {
    if (__kmp_tp_cached) {
      __kmp_fatal(KMP_MSG(CantRegisterNewThread),
                  KMP_HNT(Set_ALL_THREADPRIVATE, __kmp_tp_capacity),
                  KMP_HNT(PossibleSystemLimitOnThreads), __kmp_msg_null);
    } else {
      __kmp_fatal(KMP_MSG(CantRegisterNewThread), KMP_HNT(SystemLimitOnThreads),
                  __kmp_msg_null);
    }
  }

  /* find an available thread slot */
  /* Don't reassign the zero slot since we need that to only be used by initial
     thread */
  for (gtid = (initial_thread ? 0 : 1); TCR_PTR(__kmp_threads[gtid]) != NULL;
       gtid++)
    ;
  KA_TRACE(1,
           ("__kmp_register_root: found slot in threads array: T#%d\n", gtid));
  KMP_ASSERT(gtid < __kmp_threads_capacity);

  /* update global accounting */
  __kmp_all_nth++;
  TCW_4(__kmp_nth, __kmp_nth + 1);

  // if __kmp_adjust_gtid_mode is set, then we use method #1 (sp search) for low
  // numbers of procs, and method #2 (keyed API call) for higher numbers.
  if (__kmp_adjust_gtid_mode) {
    if (__kmp_all_nth >= __kmp_tls_gtid_min) {
      if (TCR_4(__kmp_gtid_mode) != 2) {
        TCW_4(__kmp_gtid_mode, 2);
      }
    } else {
      if (TCR_4(__kmp_gtid_mode) != 1) {
        TCW_4(__kmp_gtid_mode, 1);
      }
    }
  }

#ifdef KMP_ADJUST_BLOCKTIME
  /* Adjust blocktime to zero if necessary            */
  /* Middle initialization might not have occurred yet */
  if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
    if (__kmp_nth > __kmp_avail_proc) {
      __kmp_zero_bt = TRUE;
    }
  }
#endif /* KMP_ADJUST_BLOCKTIME */

  /* setup this new hierarchy */
  if (!(root = __kmp_root[gtid])) {
    root = __kmp_root[gtid] = (kmp_root_t *)__kmp_allocate(sizeof(kmp_root_t));
    KMP_DEBUG_ASSERT(!root->r.r_root_team);
  }

#if KMP_STATS_ENABLED
  // Initialize stats as soon as possible (right after gtid assignment).
  __kmp_stats_thread_ptr = __kmp_stats_list->push_back(gtid);
  __kmp_stats_thread_ptr->startLife();
  KMP_SET_THREAD_STATE(SERIAL_REGION);
  KMP_INIT_PARTITIONED_TIMERS(OMP_serial);
#endif
  __kmp_initialize_root(root);

  /* setup new root thread structure */
  if (root->r.r_uber_thread) {
    root_thread = root->r.r_uber_thread;
  } else {
    root_thread = (kmp_info_t *)__kmp_allocate(sizeof(kmp_info_t));
    if (__kmp_storage_map) {
      __kmp_print_thread_storage_map(root_thread, gtid);
    }
    root_thread->th.th_info.ds.ds_gtid = gtid;
#if OMPT_SUPPORT
    root_thread->th.ompt_thread_info.thread_data = ompt_data_none;
#endif
    root_thread->th.th_root = root;
    if (__kmp_env_consistency_check) {
      root_thread->th.th_cons = __kmp_allocate_cons_stack(gtid);
    }
#if USE_FAST_MEMORY
    __kmp_initialize_fast_memory(root_thread);
#endif /* USE_FAST_MEMORY */

#if KMP_USE_BGET
    KMP_DEBUG_ASSERT(root_thread->th.th_local.bget_data == NULL);
    __kmp_initialize_bget(root_thread);
#endif
    __kmp_init_random(root_thread); // Initialize random number generator
  }

  /* setup the serial team held in reserve by the root thread */
  if (!root_thread->th.th_serial_team) {
    kmp_internal_control_t r_icvs = __kmp_get_global_icvs();
    KF_TRACE(10, ("__kmp_register_root: before serial_team\n"));
    root_thread->th.th_serial_team =
        __kmp_allocate_team(root, 1, 1,
#if OMPT_SUPPORT
                            ompt_data_none, // root parallel id
#endif
#if OMP_40_ENABLED
                            proc_bind_default,
#endif
                            &r_icvs, 0 USE_NESTED_HOT_ARG(NULL));
  }
  KMP_ASSERT(root_thread->th.th_serial_team);
  KF_TRACE(10, ("__kmp_register_root: after serial_team = %p\n",
                root_thread->th.th_serial_team));

  /* drop root_thread into place */
  TCW_SYNC_PTR(__kmp_threads[gtid], root_thread);

  root->r.r_root_team->t.t_threads[0] = root_thread;
  root->r.r_hot_team->t.t_threads[0] = root_thread;
  root_thread->th.th_serial_team->t.t_threads[0] = root_thread;
  // AC: the team created in reserve, not for execution (it is unused for now).
  root_thread->th.th_serial_team->t.t_serialized = 0;
  root->r.r_uber_thread = root_thread;

  /* initialize the thread, get it ready to go */
  __kmp_initialize_info(root_thread, root->r.r_root_team, 0, gtid);
  TCW_4(__kmp_init_gtid, TRUE);

  /* prepare the master thread for get_gtid() */
  __kmp_gtid_set_specific(gtid);

#if USE_ITT_BUILD
  __kmp_itt_thread_name(gtid);
#endif /* USE_ITT_BUILD */

#ifdef KMP_TDATA_GTID
  __kmp_gtid = gtid;
#endif
  __kmp_create_worker(gtid, root_thread, __kmp_stksize);
  KMP_DEBUG_ASSERT(__kmp_gtid_get_specific() == gtid);

  KA_TRACE(20, ("__kmp_register_root: T#%d init T#%d(%d:%d) arrived: join=%u, "
                "plain=%u\n",
                gtid, __kmp_gtid_from_tid(0, root->r.r_hot_team),
                root->r.r_hot_team->t.t_id, 0, KMP_INIT_BARRIER_STATE,
                KMP_INIT_BARRIER_STATE));
  { // Initialize barrier data.
    int b;
    for (b = 0; b < bs_last_barrier; ++b) {
      root_thread->th.th_bar[b].bb.b_arrived = KMP_INIT_BARRIER_STATE;
#if USE_DEBUGGER
      root_thread->th.th_bar[b].bb.b_worker_arrived = 0;
#endif
    }
  }
  KMP_DEBUG_ASSERT(root->r.r_hot_team->t.t_bar[bs_forkjoin_barrier].b_arrived ==
                   KMP_INIT_BARRIER_STATE);

#if KMP_AFFINITY_SUPPORTED
#if OMP_40_ENABLED
  root_thread->th.th_current_place = KMP_PLACE_UNDEFINED;
  root_thread->th.th_new_place = KMP_PLACE_UNDEFINED;
  root_thread->th.th_first_place = KMP_PLACE_UNDEFINED;
  root_thread->th.th_last_place = KMP_PLACE_UNDEFINED;
#endif
  if (TCR_4(__kmp_init_middle)) {
    __kmp_affinity_set_init_mask(gtid, TRUE);
  }
#endif /* KMP_AFFINITY_SUPPORTED */
#if OMP_50_ENABLED
  root_thread->th.th_def_allocator = __kmp_def_allocator;
  root_thread->th.th_prev_level = 0;
  root_thread->th.th_prev_num_threads = 1;
#endif

  __kmp_root_counter++;

#if OMPT_SUPPORT
  if (!initial_thread && ompt_enabled.enabled) {

    kmp_info_t *root_thread = ompt_get_thread();

    ompt_set_thread_state(root_thread, ompt_state_overhead);

    if (ompt_enabled.ompt_callback_thread_begin) {
      ompt_callbacks.ompt_callback(ompt_callback_thread_begin)(
          ompt_thread_initial, __ompt_get_thread_data_internal());
    }
    ompt_data_t *task_data;
    __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL, NULL);
    if (ompt_enabled.ompt_callback_task_create) {
      ompt_callbacks.ompt_callback(ompt_callback_task_create)(
          NULL, NULL, task_data, ompt_task_initial, 0, NULL);
      // initial task has nothing to return to
    }

    ompt_set_thread_state(root_thread, ompt_state_work_serial);
  }
#endif

  KMP_MB();
  __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);

  return gtid;
}

#if KMP_NESTED_HOT_TEAMS
static int __kmp_free_hot_teams(kmp_root_t *root, kmp_info_t *thr, int level,
                                const int max_level) {
  int i, n, nth;
  kmp_hot_team_ptr_t *hot_teams = thr->th.th_hot_teams;
  if (!hot_teams || !hot_teams[level].hot_team) {
    return 0;
  }
  KMP_DEBUG_ASSERT(level < max_level);
  kmp_team_t *team = hot_teams[level].hot_team;
  nth = hot_teams[level].hot_team_nth;
  n = nth - 1; // master is not freed
  if (level < max_level - 1) {
    for (i = 0; i < nth; ++i) {
      kmp_info_t *th = team->t.t_threads[i];
      n += __kmp_free_hot_teams(root, th, level + 1, max_level);
      if (i > 0 && th->th.th_hot_teams) {
        __kmp_free(th->th.th_hot_teams);
        th->th.th_hot_teams = NULL;
      }
    }
  }
  __kmp_free_team(root, team, NULL);
  return n;
}
#endif

// Resets a root thread and clear its root and hot teams.
// Returns the number of __kmp_threads entries directly and indirectly freed.
static int __kmp_reset_root(int gtid, kmp_root_t *root) {
  kmp_team_t *root_team = root->r.r_root_team;
  kmp_team_t *hot_team = root->r.r_hot_team;
  int n = hot_team->t.t_nproc;
  int i;

  KMP_DEBUG_ASSERT(!root->r.r_active);

  root->r.r_root_team = NULL;
  root->r.r_hot_team = NULL;
  // __kmp_free_team() does not free hot teams, so we have to clear r_hot_team
  // before call to __kmp_free_team().
  __kmp_free_team(root, root_team USE_NESTED_HOT_ARG(NULL));
#if KMP_NESTED_HOT_TEAMS
  if (__kmp_hot_teams_max_level >
      0) { // need to free nested hot teams and their threads if any
    for (i = 0; i < hot_team->t.t_nproc; ++i) {
      kmp_info_t *th = hot_team->t.t_threads[i];
      if (__kmp_hot_teams_max_level > 1) {
        n += __kmp_free_hot_teams(root, th, 1, __kmp_hot_teams_max_level);
      }
      if (th->th.th_hot_teams) {
        __kmp_free(th->th.th_hot_teams);
        th->th.th_hot_teams = NULL;
      }
    }
  }
#endif
  __kmp_free_team(root, hot_team USE_NESTED_HOT_ARG(NULL));

  // Before we can reap the thread, we need to make certain that all other
  // threads in the teams that had this root as ancestor have stopped trying to
  // steal tasks.
  if (__kmp_tasking_mode != tskm_immediate_exec) {
    __kmp_wait_to_unref_task_teams();
  }

#if KMP_OS_WINDOWS
  /* Close Handle of root duplicated in __kmp_create_worker (tr #62919) */
  KA_TRACE(
      10, ("__kmp_reset_root: free handle, th = %p, handle = %" KMP_UINTPTR_SPEC
           "\n",
           (LPVOID) & (root->r.r_uber_thread->th),
           root->r.r_uber_thread->th.th_info.ds.ds_thread));
  __kmp_free_handle(root->r.r_uber_thread->th.th_info.ds.ds_thread);
#endif /* KMP_OS_WINDOWS */

#if OMPT_SUPPORT
  if (ompt_enabled.ompt_callback_thread_end) {
    ompt_callbacks.ompt_callback(ompt_callback_thread_end)(
        &(root->r.r_uber_thread->th.ompt_thread_info.thread_data));
  }
#endif

  TCW_4(__kmp_nth,
        __kmp_nth - 1); // __kmp_reap_thread will decrement __kmp_all_nth.
  root->r.r_cg_nthreads--;

  __kmp_reap_thread(root->r.r_uber_thread, 1);

  // We canot put root thread to __kmp_thread_pool, so we have to reap it istead
  // of freeing.
  root->r.r_uber_thread = NULL;
  /* mark root as no longer in use */
  root->r.r_begin = FALSE;

  return n;
}

void __kmp_unregister_root_current_thread(int gtid) {
  KA_TRACE(1, ("__kmp_unregister_root_current_thread: enter T#%d\n", gtid));
  /* this lock should be ok, since unregister_root_current_thread is never
     called during an abort, only during a normal close. furthermore, if you
     have the forkjoin lock, you should never try to get the initz lock */
  __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);
  if (TCR_4(__kmp_global.g.g_done) || !__kmp_init_serial) {
    KC_TRACE(10, ("__kmp_unregister_root_current_thread: already finished, "
                  "exiting T#%d\n",
                  gtid));
    __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
    return;
  }
  kmp_root_t *root = __kmp_root[gtid];

  KMP_DEBUG_ASSERT(__kmp_threads && __kmp_threads[gtid]);
  KMP_ASSERT(KMP_UBER_GTID(gtid));
  KMP_ASSERT(root == __kmp_threads[gtid]->th.th_root);
  KMP_ASSERT(root->r.r_active == FALSE);

  KMP_MB();

#if OMP_45_ENABLED
  kmp_info_t *thread = __kmp_threads[gtid];
  kmp_team_t *team = thread->th.th_team;
  kmp_task_team_t *task_team = thread->th.th_task_team;

  // we need to wait for the proxy tasks before finishing the thread
  if (task_team != NULL && task_team->tt.tt_found_proxy_tasks) {
#if OMPT_SUPPORT
    // the runtime is shutting down so we won't report any events
    thread->th.ompt_thread_info.state = ompt_state_undefined;
#endif
    __kmp_task_team_wait(thread, team USE_ITT_BUILD_ARG(NULL));
  }
#endif

  __kmp_reset_root(gtid, root);

  /* free up this thread slot */
  __kmp_gtid_set_specific(KMP_GTID_DNE);
#ifdef KMP_TDATA_GTID
  __kmp_gtid = KMP_GTID_DNE;
#endif

  KMP_MB();
  KC_TRACE(10,
           ("__kmp_unregister_root_current_thread: T#%d unregistered\n", gtid));

  __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
}

#if KMP_OS_WINDOWS
/* __kmp_forkjoin_lock must be already held
   Unregisters a root thread that is not the current thread.  Returns the number
   of __kmp_threads entries freed as a result. */
static int __kmp_unregister_root_other_thread(int gtid) {
  kmp_root_t *root = __kmp_root[gtid];
  int r;

  KA_TRACE(1, ("__kmp_unregister_root_other_thread: enter T#%d\n", gtid));
  KMP_DEBUG_ASSERT(__kmp_threads && __kmp_threads[gtid]);
  KMP_ASSERT(KMP_UBER_GTID(gtid));
  KMP_ASSERT(root == __kmp_threads[gtid]->th.th_root);
  KMP_ASSERT(root->r.r_active == FALSE);

  r = __kmp_reset_root(gtid, root);
  KC_TRACE(10,
           ("__kmp_unregister_root_other_thread: T#%d unregistered\n", gtid));
  return r;
}
#endif

#if KMP_DEBUG
void __kmp_task_info() {

  kmp_int32 gtid = __kmp_entry_gtid();
  kmp_int32 tid = __kmp_tid_from_gtid(gtid);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *steam = this_thr->th.th_serial_team;
  kmp_team_t *team = this_thr->th.th_team;

  __kmp_printf(
      "__kmp_task_info: gtid=%d tid=%d t_thread=%p team=%p steam=%p curtask=%p "
      "ptask=%p\n",
      gtid, tid, this_thr, team, steam, this_thr->th.th_current_task,
      team->t.t_implicit_task_taskdata[tid].td_parent);
}
#endif // KMP_DEBUG

/* TODO optimize with one big memclr, take out what isn't needed, split
   responsibility to workers as much as possible, and delay initialization of
   features as much as possible  */
static void __kmp_initialize_info(kmp_info_t *this_thr, kmp_team_t *team,
                                  int tid, int gtid) {
  /* this_thr->th.th_info.ds.ds_gtid is setup in
     kmp_allocate_thread/create_worker.
     this_thr->th.th_serial_team is setup in __kmp_allocate_thread */
  kmp_info_t *master = team->t.t_threads[0];
  KMP_DEBUG_ASSERT(this_thr != NULL);
  KMP_DEBUG_ASSERT(this_thr->th.th_serial_team);
  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(team->t.t_threads);
  KMP_DEBUG_ASSERT(team->t.t_dispatch);
  KMP_DEBUG_ASSERT(master);
  KMP_DEBUG_ASSERT(master->th.th_root);

  KMP_MB();

  TCW_SYNC_PTR(this_thr->th.th_team, team);

  this_thr->th.th_info.ds.ds_tid = tid;
  this_thr->th.th_set_nproc = 0;
  if (__kmp_tasking_mode != tskm_immediate_exec)
    // When tasking is possible, threads are not safe to reap until they are
    // done tasking; this will be set when tasking code is exited in wait
    this_thr->th.th_reap_state = KMP_NOT_SAFE_TO_REAP;
  else // no tasking --> always safe to reap
    this_thr->th.th_reap_state = KMP_SAFE_TO_REAP;
#if OMP_40_ENABLED
  this_thr->th.th_set_proc_bind = proc_bind_default;
#if KMP_AFFINITY_SUPPORTED
  this_thr->th.th_new_place = this_thr->th.th_current_place;
#endif
#endif
  this_thr->th.th_root = master->th.th_root;

  /* setup the thread's cache of the team structure */
  this_thr->th.th_team_nproc = team->t.t_nproc;
  this_thr->th.th_team_master = master;
  this_thr->th.th_team_serialized = team->t.t_serialized;
  TCW_PTR(this_thr->th.th_sleep_loc, NULL);

  KMP_DEBUG_ASSERT(team->t.t_implicit_task_taskdata);

  KF_TRACE(10, ("__kmp_initialize_info1: T#%d:%d this_thread=%p curtask=%p\n",
                tid, gtid, this_thr, this_thr->th.th_current_task));

  __kmp_init_implicit_task(this_thr->th.th_team_master->th.th_ident, this_thr,
                           team, tid, TRUE);

  KF_TRACE(10, ("__kmp_initialize_info2: T#%d:%d this_thread=%p curtask=%p\n",
                tid, gtid, this_thr, this_thr->th.th_current_task));
  // TODO: Initialize ICVs from parent; GEH - isn't that already done in
  // __kmp_initialize_team()?

  /* TODO no worksharing in speculative threads */
  this_thr->th.th_dispatch = &team->t.t_dispatch[tid];

  this_thr->th.th_local.this_construct = 0;

  if (!this_thr->th.th_pri_common) {
    this_thr->th.th_pri_common =
        (struct common_table *)__kmp_allocate(sizeof(struct common_table));
    if (__kmp_storage_map) {
      __kmp_print_storage_map_gtid(
          gtid, this_thr->th.th_pri_common, this_thr->th.th_pri_common + 1,
          sizeof(struct common_table), "th_%d.th_pri_common\n", gtid);
    }
    this_thr->th.th_pri_head = NULL;
  }

  /* Initialize dynamic dispatch */
  {
    volatile kmp_disp_t *dispatch = this_thr->th.th_dispatch;
    // Use team max_nproc since this will never change for the team.
    size_t disp_size =
        sizeof(dispatch_private_info_t) *
        (team->t.t_max_nproc == 1 ? 1 : __kmp_dispatch_num_buffers);
    KD_TRACE(10, ("__kmp_initialize_info: T#%d max_nproc: %d\n", gtid,
                  team->t.t_max_nproc));
    KMP_ASSERT(dispatch);
    KMP_DEBUG_ASSERT(team->t.t_dispatch);
    KMP_DEBUG_ASSERT(dispatch == &team->t.t_dispatch[tid]);

    dispatch->th_disp_index = 0;
#if OMP_45_ENABLED
    dispatch->th_doacross_buf_idx = 0;
#endif
    if (!dispatch->th_disp_buffer) {
      dispatch->th_disp_buffer =
          (dispatch_private_info_t *)__kmp_allocate(disp_size);

      if (__kmp_storage_map) {
        __kmp_print_storage_map_gtid(
            gtid, &dispatch->th_disp_buffer[0],
            &dispatch->th_disp_buffer[team->t.t_max_nproc == 1
                                          ? 1
                                          : __kmp_dispatch_num_buffers],
            disp_size, "th_%d.th_dispatch.th_disp_buffer "
                       "(team_%d.t_dispatch[%d].th_disp_buffer)",
            gtid, team->t.t_id, gtid);
      }
    } else {
      memset(&dispatch->th_disp_buffer[0], '\0', disp_size);
    }

    dispatch->th_dispatch_pr_current = 0;
    dispatch->th_dispatch_sh_current = 0;

    dispatch->th_deo_fcn = 0; /* ORDERED     */
    dispatch->th_dxo_fcn = 0; /* END ORDERED */
  }

  this_thr->th.th_next_pool = NULL;

  if (!this_thr->th.th_task_state_memo_stack) {
    size_t i;
    this_thr->th.th_task_state_memo_stack =
        (kmp_uint8 *)__kmp_allocate(4 * sizeof(kmp_uint8));
    this_thr->th.th_task_state_top = 0;
    this_thr->th.th_task_state_stack_sz = 4;
    for (i = 0; i < this_thr->th.th_task_state_stack_sz;
         ++i) // zero init the stack
      this_thr->th.th_task_state_memo_stack[i] = 0;
  }

  KMP_DEBUG_ASSERT(!this_thr->th.th_spin_here);
  KMP_DEBUG_ASSERT(this_thr->th.th_next_waiting == 0);

  KMP_MB();
}

/* allocate a new thread for the requesting team. this is only called from
   within a forkjoin critical section. we will first try to get an available
   thread from the thread pool. if none is available, we will fork a new one
   assuming we are able to create a new one. this should be assured, as the
   caller should check on this first. */
kmp_info_t *__kmp_allocate_thread(kmp_root_t *root, kmp_team_t *team,
                                  int new_tid) {
  kmp_team_t *serial_team;
  kmp_info_t *new_thr;
  int new_gtid;

  KA_TRACE(20, ("__kmp_allocate_thread: T#%d\n", __kmp_get_gtid()));
  KMP_DEBUG_ASSERT(root && team);
#if !KMP_NESTED_HOT_TEAMS
  KMP_DEBUG_ASSERT(KMP_MASTER_GTID(__kmp_get_gtid()));
#endif
  KMP_MB();

  /* first, try to get one from the thread pool */
  if (__kmp_thread_pool) {

    new_thr = CCAST(kmp_info_t *, __kmp_thread_pool);
    __kmp_thread_pool = (volatile kmp_info_t *)new_thr->th.th_next_pool;
    if (new_thr == __kmp_thread_pool_insert_pt) {
      __kmp_thread_pool_insert_pt = NULL;
    }
    TCW_4(new_thr->th.th_in_pool, FALSE);
    // Don't touch th_active_in_pool or th_active.
    // The worker thread adjusts those flags as it sleeps/awakens.
    __kmp_thread_pool_nth--;

    KA_TRACE(20, ("__kmp_allocate_thread: T#%d using thread T#%d\n",
                  __kmp_get_gtid(), new_thr->th.th_info.ds.ds_gtid));
    KMP_ASSERT(!new_thr->th.th_team);
    KMP_DEBUG_ASSERT(__kmp_nth < __kmp_threads_capacity);
    KMP_DEBUG_ASSERT(__kmp_thread_pool_nth >= 0);

    /* setup the thread structure */
    __kmp_initialize_info(new_thr, team, new_tid,
                          new_thr->th.th_info.ds.ds_gtid);
    KMP_DEBUG_ASSERT(new_thr->th.th_serial_team);

    TCW_4(__kmp_nth, __kmp_nth + 1);
    root->r.r_cg_nthreads++;

    new_thr->th.th_task_state = 0;
    new_thr->th.th_task_state_top = 0;
    new_thr->th.th_task_state_stack_sz = 4;

#ifdef KMP_ADJUST_BLOCKTIME
    /* Adjust blocktime back to zero if necessary */
    /* Middle initialization might not have occurred yet */
    if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
      if (__kmp_nth > __kmp_avail_proc) {
        __kmp_zero_bt = TRUE;
      }
    }
#endif /* KMP_ADJUST_BLOCKTIME */

#if KMP_DEBUG
    // If thread entered pool via __kmp_free_thread, wait_flag should !=
    // KMP_BARRIER_PARENT_FLAG.
    int b;
    kmp_balign_t *balign = new_thr->th.th_bar;
    for (b = 0; b < bs_last_barrier; ++b)
      KMP_DEBUG_ASSERT(balign[b].bb.wait_flag != KMP_BARRIER_PARENT_FLAG);
#endif

    KF_TRACE(10, ("__kmp_allocate_thread: T#%d using thread %p T#%d\n",
                  __kmp_get_gtid(), new_thr, new_thr->th.th_info.ds.ds_gtid));

    KMP_MB();
    return new_thr;
  }

  /* no, well fork a new one */
  KMP_ASSERT(__kmp_nth == __kmp_all_nth);
  KMP_ASSERT(__kmp_all_nth < __kmp_threads_capacity);

#if KMP_USE_MONITOR
  // If this is the first worker thread the RTL is creating, then also
  // launch the monitor thread.  We try to do this as early as possible.
  if (!TCR_4(__kmp_init_monitor)) {
    __kmp_acquire_bootstrap_lock(&__kmp_monitor_lock);
    if (!TCR_4(__kmp_init_monitor)) {
      KF_TRACE(10, ("before __kmp_create_monitor\n"));
      TCW_4(__kmp_init_monitor, 1);
      __kmp_create_monitor(&__kmp_monitor);
      KF_TRACE(10, ("after __kmp_create_monitor\n"));
#if KMP_OS_WINDOWS
      // AC: wait until monitor has started. This is a fix for CQ232808.
      // The reason is that if the library is loaded/unloaded in a loop with
      // small (parallel) work in between, then there is high probability that
      // monitor thread started after the library shutdown. At shutdown it is
      // too late to cope with the problem, because when the master is in
      // DllMain (process detach) the monitor has no chances to start (it is
      // blocked), and master has no means to inform the monitor that the
      // library has gone, because all the memory which the monitor can access
      // is going to be released/reset.
      while (TCR_4(__kmp_init_monitor) < 2) {
        KMP_YIELD(TRUE);
      }
      KF_TRACE(10, ("after monitor thread has started\n"));
#endif
    }
    __kmp_release_bootstrap_lock(&__kmp_monitor_lock);
  }
#endif

  KMP_MB();
  for (new_gtid = 1; TCR_PTR(__kmp_threads[new_gtid]) != NULL; ++new_gtid) {
    KMP_DEBUG_ASSERT(new_gtid < __kmp_threads_capacity);
  }

  /* allocate space for it. */
  new_thr = (kmp_info_t *)__kmp_allocate(sizeof(kmp_info_t));

  TCW_SYNC_PTR(__kmp_threads[new_gtid], new_thr);

  if (__kmp_storage_map) {
    __kmp_print_thread_storage_map(new_thr, new_gtid);
  }

  // add the reserve serialized team, initialized from the team's master thread
  {
    kmp_internal_control_t r_icvs = __kmp_get_x_global_icvs(team);
    KF_TRACE(10, ("__kmp_allocate_thread: before th_serial/serial_team\n"));
    new_thr->th.th_serial_team = serial_team =
        (kmp_team_t *)__kmp_allocate_team(root, 1, 1,
#if OMPT_SUPPORT
                                          ompt_data_none, // root parallel id
#endif
#if OMP_40_ENABLED
                                          proc_bind_default,
#endif
                                          &r_icvs, 0 USE_NESTED_HOT_ARG(NULL));
  }
  KMP_ASSERT(serial_team);
  serial_team->t.t_serialized = 0; // AC: the team created in reserve, not for
  // execution (it is unused for now).
  serial_team->t.t_threads[0] = new_thr;
  KF_TRACE(10,
           ("__kmp_allocate_thread: after th_serial/serial_team : new_thr=%p\n",
            new_thr));

  /* setup the thread structures */
  __kmp_initialize_info(new_thr, team, new_tid, new_gtid);

#if USE_FAST_MEMORY
  __kmp_initialize_fast_memory(new_thr);
#endif /* USE_FAST_MEMORY */

#if KMP_USE_BGET
  KMP_DEBUG_ASSERT(new_thr->th.th_local.bget_data == NULL);
  __kmp_initialize_bget(new_thr);
#endif

  __kmp_init_random(new_thr); // Initialize random number generator

  /* Initialize these only once when thread is grabbed for a team allocation */
  KA_TRACE(20,
           ("__kmp_allocate_thread: T#%d init go fork=%u, plain=%u\n",
            __kmp_get_gtid(), KMP_INIT_BARRIER_STATE, KMP_INIT_BARRIER_STATE));

  int b;
  kmp_balign_t *balign = new_thr->th.th_bar;
  for (b = 0; b < bs_last_barrier; ++b) {
    balign[b].bb.b_go = KMP_INIT_BARRIER_STATE;
    balign[b].bb.team = NULL;
    balign[b].bb.wait_flag = KMP_BARRIER_NOT_WAITING;
    balign[b].bb.use_oncore_barrier = 0;
  }

  new_thr->th.th_spin_here = FALSE;
  new_thr->th.th_next_waiting = 0;
#if KMP_OS_UNIX
  new_thr->th.th_blocking = false;
#endif

#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
  new_thr->th.th_current_place = KMP_PLACE_UNDEFINED;
  new_thr->th.th_new_place = KMP_PLACE_UNDEFINED;
  new_thr->th.th_first_place = KMP_PLACE_UNDEFINED;
  new_thr->th.th_last_place = KMP_PLACE_UNDEFINED;
#endif
#if OMP_50_ENABLED
  new_thr->th.th_def_allocator = __kmp_def_allocator;
  new_thr->th.th_prev_level = 0;
  new_thr->th.th_prev_num_threads = 1;
#endif

  TCW_4(new_thr->th.th_in_pool, FALSE);
  new_thr->th.th_active_in_pool = FALSE;
  TCW_4(new_thr->th.th_active, TRUE);

  /* adjust the global counters */
  __kmp_all_nth++;
  __kmp_nth++;

  root->r.r_cg_nthreads++;

  // if __kmp_adjust_gtid_mode is set, then we use method #1 (sp search) for low
  // numbers of procs, and method #2 (keyed API call) for higher numbers.
  if (__kmp_adjust_gtid_mode) {
    if (__kmp_all_nth >= __kmp_tls_gtid_min) {
      if (TCR_4(__kmp_gtid_mode) != 2) {
        TCW_4(__kmp_gtid_mode, 2);
      }
    } else {
      if (TCR_4(__kmp_gtid_mode) != 1) {
        TCW_4(__kmp_gtid_mode, 1);
      }
    }
  }

#ifdef KMP_ADJUST_BLOCKTIME
  /* Adjust blocktime back to zero if necessary       */
  /* Middle initialization might not have occurred yet */
  if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
    if (__kmp_nth > __kmp_avail_proc) {
      __kmp_zero_bt = TRUE;
    }
  }
#endif /* KMP_ADJUST_BLOCKTIME */

  /* actually fork it and create the new worker thread */
  KF_TRACE(
      10, ("__kmp_allocate_thread: before __kmp_create_worker: %p\n", new_thr));
  __kmp_create_worker(new_gtid, new_thr, __kmp_stksize);
  KF_TRACE(10,
           ("__kmp_allocate_thread: after __kmp_create_worker: %p\n", new_thr));

  KA_TRACE(20, ("__kmp_allocate_thread: T#%d forked T#%d\n", __kmp_get_gtid(),
                new_gtid));
  KMP_MB();
  return new_thr;
}

/* Reinitialize team for reuse.
   The hot team code calls this case at every fork barrier, so EPCC barrier
   test are extremely sensitive to changes in it, esp. writes to the team
   struct, which cause a cache invalidation in all threads.
   IF YOU TOUCH THIS ROUTINE, RUN EPCC C SYNCBENCH ON A BIG-IRON MACHINE!!! */
static void __kmp_reinitialize_team(kmp_team_t *team,
                                    kmp_internal_control_t *new_icvs,
                                    ident_t *loc) {
  KF_TRACE(10, ("__kmp_reinitialize_team: enter this_thread=%p team=%p\n",
                team->t.t_threads[0], team));
  KMP_DEBUG_ASSERT(team && new_icvs);
  KMP_DEBUG_ASSERT((!TCR_4(__kmp_init_parallel)) || new_icvs->nproc);
  KMP_CHECK_UPDATE(team->t.t_ident, loc);

  KMP_CHECK_UPDATE(team->t.t_id, KMP_GEN_TEAM_ID());
  // Copy ICVs to the master thread's implicit taskdata
  __kmp_init_implicit_task(loc, team->t.t_threads[0], team, 0, FALSE);
  copy_icvs(&team->t.t_implicit_task_taskdata[0].td_icvs, new_icvs);

  KF_TRACE(10, ("__kmp_reinitialize_team: exit this_thread=%p team=%p\n",
                team->t.t_threads[0], team));
}

/* Initialize the team data structure.
   This assumes the t_threads and t_max_nproc are already set.
   Also, we don't touch the arguments */
static void __kmp_initialize_team(kmp_team_t *team, int new_nproc,
                                  kmp_internal_control_t *new_icvs,
                                  ident_t *loc) {
  KF_TRACE(10, ("__kmp_initialize_team: enter: team=%p\n", team));

  /* verify */
  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(new_nproc <= team->t.t_max_nproc);
  KMP_DEBUG_ASSERT(team->t.t_threads);
  KMP_MB();

  team->t.t_master_tid = 0; /* not needed */
  /* team->t.t_master_bar;        not needed */
  team->t.t_serialized = new_nproc > 1 ? 0 : 1;
  team->t.t_nproc = new_nproc;

  /* team->t.t_parent     = NULL; TODO not needed & would mess up hot team */
  team->t.t_next_pool = NULL;
  /* memset( team->t.t_threads, 0, sizeof(kmp_info_t*)*new_nproc ); would mess
   * up hot team */

  TCW_SYNC_PTR(team->t.t_pkfn, NULL); /* not needed */
  team->t.t_invoke = NULL; /* not needed */

  // TODO???: team->t.t_max_active_levels       = new_max_active_levels;
  team->t.t_sched.sched = new_icvs->sched.sched;

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
  team->t.t_fp_control_saved = FALSE; /* not needed */
  team->t.t_x87_fpu_control_word = 0; /* not needed */
  team->t.t_mxcsr = 0; /* not needed */
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

  team->t.t_construct = 0;

  team->t.t_ordered.dt.t_value = 0;
  team->t.t_master_active = FALSE;

  memset(&team->t.t_taskq, '\0', sizeof(kmp_taskq_t));

#ifdef KMP_DEBUG
  team->t.t_copypriv_data = NULL; /* not necessary, but nice for debugging */
#endif
#if KMP_OS_WINDOWS
  team->t.t_copyin_counter = 0; /* for barrier-free copyin implementation */
#endif

  team->t.t_control_stack_top = NULL;

  __kmp_reinitialize_team(team, new_icvs, loc);

  KMP_MB();
  KF_TRACE(10, ("__kmp_initialize_team: exit: team=%p\n", team));
}

#if KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED
/* Sets full mask for thread and returns old mask, no changes to structures. */
static void
__kmp_set_thread_affinity_mask_full_tmp(kmp_affin_mask_t *old_mask) {
  if (KMP_AFFINITY_CAPABLE()) {
    int status;
    if (old_mask != NULL) {
      status = __kmp_get_system_affinity(old_mask, TRUE);
      int error = errno;
      if (status != 0) {
        __kmp_fatal(KMP_MSG(ChangeThreadAffMaskError), KMP_ERR(error),
                    __kmp_msg_null);
      }
    }
    __kmp_set_system_affinity(__kmp_affin_fullMask, TRUE);
  }
}
#endif

#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED

// __kmp_partition_places() is the heart of the OpenMP 4.0 affinity mechanism.
// It calculats the worker + master thread's partition based upon the parent
// thread's partition, and binds each worker to a thread in their partition.
// The master thread's partition should already include its current binding.
static void __kmp_partition_places(kmp_team_t *team, int update_master_only) {
  // Copy the master thread's place partion to the team struct
  kmp_info_t *master_th = team->t.t_threads[0];
  KMP_DEBUG_ASSERT(master_th != NULL);
  kmp_proc_bind_t proc_bind = team->t.t_proc_bind;
  int first_place = master_th->th.th_first_place;
  int last_place = master_th->th.th_last_place;
  int masters_place = master_th->th.th_current_place;
  team->t.t_first_place = first_place;
  team->t.t_last_place = last_place;

  KA_TRACE(20, ("__kmp_partition_places: enter: proc_bind = %d T#%d(%d:0) "
                "bound to place %d partition = [%d,%d]\n",
                proc_bind, __kmp_gtid_from_thread(team->t.t_threads[0]),
                team->t.t_id, masters_place, first_place, last_place));

  switch (proc_bind) {

  case proc_bind_default:
    // serial teams might have the proc_bind policy set to proc_bind_default. It
    // doesn't matter, as we don't rebind master thread for any proc_bind policy
    KMP_DEBUG_ASSERT(team->t.t_nproc == 1);
    break;

  case proc_bind_master: {
    int f;
    int n_th = team->t.t_nproc;
    for (f = 1; f < n_th; f++) {
      kmp_info_t *th = team->t.t_threads[f];
      KMP_DEBUG_ASSERT(th != NULL);
      th->th.th_first_place = first_place;
      th->th.th_last_place = last_place;
      th->th.th_new_place = masters_place;
#if OMP_50_ENABLED
      if (__kmp_display_affinity && masters_place != th->th.th_current_place &&
          team->t.t_display_affinity != 1) {
        team->t.t_display_affinity = 1;
      }
#endif

      KA_TRACE(100, ("__kmp_partition_places: master: T#%d(%d:%d) place %d "
                     "partition = [%d,%d]\n",
                     __kmp_gtid_from_thread(team->t.t_threads[f]), team->t.t_id,
                     f, masters_place, first_place, last_place));
    }
  } break;

  case proc_bind_close: {
    int f;
    int n_th = team->t.t_nproc;
    int n_places;
    if (first_place <= last_place) {
      n_places = last_place - first_place + 1;
    } else {
      n_places = __kmp_affinity_num_masks - first_place + last_place + 1;
    }
    if (n_th <= n_places) {
      int place = masters_place;
      for (f = 1; f < n_th; f++) {
        kmp_info_t *th = team->t.t_threads[f];
        KMP_DEBUG_ASSERT(th != NULL);

        if (place == last_place) {
          place = first_place;
        } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
          place = 0;
        } else {
          place++;
        }
        th->th.th_first_place = first_place;
        th->th.th_last_place = last_place;
        th->th.th_new_place = place;
#if OMP_50_ENABLED
        if (__kmp_display_affinity && place != th->th.th_current_place &&
            team->t.t_display_affinity != 1) {
          team->t.t_display_affinity = 1;
        }
#endif

        KA_TRACE(100, ("__kmp_partition_places: close: T#%d(%d:%d) place %d "
                       "partition = [%d,%d]\n",
                       __kmp_gtid_from_thread(team->t.t_threads[f]),
                       team->t.t_id, f, place, first_place, last_place));
      }
    } else {
      int S, rem, gap, s_count;
      S = n_th / n_places;
      s_count = 0;
      rem = n_th - (S * n_places);
      gap = rem > 0 ? n_places / rem : n_places;
      int place = masters_place;
      int gap_ct = gap;
      for (f = 0; f < n_th; f++) {
        kmp_info_t *th = team->t.t_threads[f];
        KMP_DEBUG_ASSERT(th != NULL);

        th->th.th_first_place = first_place;
        th->th.th_last_place = last_place;
        th->th.th_new_place = place;
#if OMP_50_ENABLED
        if (__kmp_display_affinity && place != th->th.th_current_place &&
            team->t.t_display_affinity != 1) {
          team->t.t_display_affinity = 1;
        }
#endif
        s_count++;

        if ((s_count == S) && rem && (gap_ct == gap)) {
          // do nothing, add an extra thread to place on next iteration
        } else if ((s_count == S + 1) && rem && (gap_ct == gap)) {
          // we added an extra thread to this place; move to next place
          if (place == last_place) {
            place = first_place;
          } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
            place = 0;
          } else {
            place++;
          }
          s_count = 0;
          gap_ct = 1;
          rem--;
        } else if (s_count == S) { // place full; don't add extra
          if (place == last_place) {
            place = first_place;
          } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
            place = 0;
          } else {
            place++;
          }
          gap_ct++;
          s_count = 0;
        }

        KA_TRACE(100,
                 ("__kmp_partition_places: close: T#%d(%d:%d) place %d "
                  "partition = [%d,%d]\n",
                  __kmp_gtid_from_thread(team->t.t_threads[f]), team->t.t_id, f,
                  th->th.th_new_place, first_place, last_place));
      }
      KMP_DEBUG_ASSERT(place == masters_place);
    }
  } break;

  case proc_bind_spread: {
    int f;
    int n_th = team->t.t_nproc;
    int n_places;
    int thidx;
    if (first_place <= last_place) {
      n_places = last_place - first_place + 1;
    } else {
      n_places = __kmp_affinity_num_masks - first_place + last_place + 1;
    }
    if (n_th <= n_places) {
      int place = -1;

      if (n_places != static_cast<int>(__kmp_affinity_num_masks)) {
        int S = n_places / n_th;
        int s_count, rem, gap, gap_ct;

        place = masters_place;
        rem = n_places - n_th * S;
        gap = rem ? n_th / rem : 1;
        gap_ct = gap;
        thidx = n_th;
        if (update_master_only == 1)
          thidx = 1;
        for (f = 0; f < thidx; f++) {
          kmp_info_t *th = team->t.t_threads[f];
          KMP_DEBUG_ASSERT(th != NULL);

          th->th.th_first_place = place;
          th->th.th_new_place = place;
#if OMP_50_ENABLED
          if (__kmp_display_affinity && place != th->th.th_current_place &&
              team->t.t_display_affinity != 1) {
            team->t.t_display_affinity = 1;
          }
#endif
          s_count = 1;
          while (s_count < S) {
            if (place == last_place) {
              place = first_place;
            } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
              place = 0;
            } else {
              place++;
            }
            s_count++;
          }
          if (rem && (gap_ct == gap)) {
            if (place == last_place) {
              place = first_place;
            } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
              place = 0;
            } else {
              place++;
            }
            rem--;
            gap_ct = 0;
          }
          th->th.th_last_place = place;
          gap_ct++;

          if (place == last_place) {
            place = first_place;
          } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
            place = 0;
          } else {
            place++;
          }

          KA_TRACE(100,
                   ("__kmp_partition_places: spread: T#%d(%d:%d) place %d "
                    "partition = [%d,%d], __kmp_affinity_num_masks: %u\n",
                    __kmp_gtid_from_thread(team->t.t_threads[f]), team->t.t_id,
                    f, th->th.th_new_place, th->th.th_first_place,
                    th->th.th_last_place, __kmp_affinity_num_masks));
        }
      } else {
        /* Having uniform space of available computation places I can create
           T partitions of round(P/T) size and put threads into the first
           place of each partition. */
        double current = static_cast<double>(masters_place);
        double spacing =
            (static_cast<double>(n_places + 1) / static_cast<double>(n_th));
        int first, last;
        kmp_info_t *th;

        thidx = n_th + 1;
        if (update_master_only == 1)
          thidx = 1;
        for (f = 0; f < thidx; f++) {
          first = static_cast<int>(current);
          last = static_cast<int>(current + spacing) - 1;
          KMP_DEBUG_ASSERT(last >= first);
          if (first >= n_places) {
            if (masters_place) {
              first -= n_places;
              last -= n_places;
              if (first == (masters_place + 1)) {
                KMP_DEBUG_ASSERT(f == n_th);
                first--;
              }
              if (last == masters_place) {
                KMP_DEBUG_ASSERT(f == (n_th - 1));
                last--;
              }
            } else {
              KMP_DEBUG_ASSERT(f == n_th);
              first = 0;
              last = 0;
            }
          }
          if (last >= n_places) {
            last = (n_places - 1);
          }
          place = first;
          current += spacing;
          if (f < n_th) {
            KMP_DEBUG_ASSERT(0 <= first);
            KMP_DEBUG_ASSERT(n_places > first);
            KMP_DEBUG_ASSERT(0 <= last);
            KMP_DEBUG_ASSERT(n_places > last);
            KMP_DEBUG_ASSERT(last_place >= first_place);
            th = team->t.t_threads[f];
            KMP_DEBUG_ASSERT(th);
            th->th.th_first_place = first;
            th->th.th_new_place = place;
            th->th.th_last_place = last;
#if OMP_50_ENABLED
            if (__kmp_display_affinity && place != th->th.th_current_place &&
                team->t.t_display_affinity != 1) {
              team->t.t_display_affinity = 1;
            }
#endif
            KA_TRACE(100,
                     ("__kmp_partition_places: spread: T#%d(%d:%d) place %d "
                      "partition = [%d,%d], spacing = %.4f\n",
                      __kmp_gtid_from_thread(team->t.t_threads[f]),
                      team->t.t_id, f, th->th.th_new_place,
                      th->th.th_first_place, th->th.th_last_place, spacing));
          }
        }
      }
      KMP_DEBUG_ASSERT(update_master_only || place == masters_place);
    } else {
      int S, rem, gap, s_count;
      S = n_th / n_places;
      s_count = 0;
      rem = n_th - (S * n_places);
      gap = rem > 0 ? n_places / rem : n_places;
      int place = masters_place;
      int gap_ct = gap;
      thidx = n_th;
      if (update_master_only == 1)
        thidx = 1;
      for (f = 0; f < thidx; f++) {
        kmp_info_t *th = team->t.t_threads[f];
        KMP_DEBUG_ASSERT(th != NULL);

        th->th.th_first_place = place;
        th->th.th_last_place = place;
        th->th.th_new_place = place;
#if OMP_50_ENABLED
        if (__kmp_display_affinity && place != th->th.th_current_place &&
            team->t.t_display_affinity != 1) {
          team->t.t_display_affinity = 1;
        }
#endif
        s_count++;

        if ((s_count == S) && rem && (gap_ct == gap)) {
          // do nothing, add an extra thread to place on next iteration
        } else if ((s_count == S + 1) && rem && (gap_ct == gap)) {
          // we added an extra thread to this place; move on to next place
          if (place == last_place) {
            place = first_place;
          } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
            place = 0;
          } else {
            place++;
          }
          s_count = 0;
          gap_ct = 1;
          rem--;
        } else if (s_count == S) { // place is full; don't add extra thread
          if (place == last_place) {
            place = first_place;
          } else if (place == (int)(__kmp_affinity_num_masks - 1)) {
            place = 0;
          } else {
            place++;
          }
          gap_ct++;
          s_count = 0;
        }

        KA_TRACE(100, ("__kmp_partition_places: spread: T#%d(%d:%d) place %d "
                       "partition = [%d,%d]\n",
                       __kmp_gtid_from_thread(team->t.t_threads[f]),
                       team->t.t_id, f, th->th.th_new_place,
                       th->th.th_first_place, th->th.th_last_place));
      }
      KMP_DEBUG_ASSERT(update_master_only || place == masters_place);
    }
  } break;

  default:
    break;
  }

  KA_TRACE(20, ("__kmp_partition_places: exit T#%d\n", team->t.t_id));
}

#endif /* OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED */

/* allocate a new team data structure to use.  take one off of the free pool if
   available */
kmp_team_t *
__kmp_allocate_team(kmp_root_t *root, int new_nproc, int max_nproc,
#if OMPT_SUPPORT
                    ompt_data_t ompt_parallel_data,
#endif
#if OMP_40_ENABLED
                    kmp_proc_bind_t new_proc_bind,
#endif
                    kmp_internal_control_t *new_icvs,
                    int argc USE_NESTED_HOT_ARG(kmp_info_t *master)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_allocate_team);
  int f;
  kmp_team_t *team;
  int use_hot_team = !root->r.r_active;
  int level = 0;

  KA_TRACE(20, ("__kmp_allocate_team: called\n"));
  KMP_DEBUG_ASSERT(new_nproc >= 1 && argc >= 0);
  KMP_DEBUG_ASSERT(max_nproc >= new_nproc);
  KMP_MB();

#if KMP_NESTED_HOT_TEAMS
  kmp_hot_team_ptr_t *hot_teams;
  if (master) {
    team = master->th.th_team;
    level = team->t.t_active_level;
    if (master->th.th_teams_microtask) { // in teams construct?
      if (master->th.th_teams_size.nteams > 1 &&
          ( // #teams > 1
              team->t.t_pkfn ==
                  (microtask_t)__kmp_teams_master || // inner fork of the teams
              master->th.th_teams_level <
                  team->t.t_level)) { // or nested parallel inside the teams
        ++level; // not increment if #teams==1, or for outer fork of the teams;
        // increment otherwise
      }
    }
    hot_teams = master->th.th_hot_teams;
    if (level < __kmp_hot_teams_max_level && hot_teams &&
        hot_teams[level]
            .hot_team) { // hot team has already been allocated for given level
      use_hot_team = 1;
    } else {
      use_hot_team = 0;
    }
  }
#endif
  // Optimization to use a "hot" team
  if (use_hot_team && new_nproc > 1) {
    KMP_DEBUG_ASSERT(new_nproc == max_nproc);
#if KMP_NESTED_HOT_TEAMS
    team = hot_teams[level].hot_team;
#else
    team = root->r.r_hot_team;
#endif
#if KMP_DEBUG
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      KA_TRACE(20, ("__kmp_allocate_team: hot team task_team[0] = %p "
                    "task_team[1] = %p before reinit\n",
                    team->t.t_task_team[0], team->t.t_task_team[1]));
    }
#endif

    // Has the number of threads changed?
    /* Let's assume the most common case is that the number of threads is
       unchanged, and put that case first. */
    if (team->t.t_nproc == new_nproc) { // Check changes in number of threads
      KA_TRACE(20, ("__kmp_allocate_team: reusing hot team\n"));
      // This case can mean that omp_set_num_threads() was called and the hot
      // team size was already reduced, so we check the special flag
      if (team->t.t_size_changed == -1) {
        team->t.t_size_changed = 1;
      } else {
        KMP_CHECK_UPDATE(team->t.t_size_changed, 0);
      }

      // TODO???: team->t.t_max_active_levels = new_max_active_levels;
      kmp_r_sched_t new_sched = new_icvs->sched;
      // set master's schedule as new run-time schedule
      KMP_CHECK_UPDATE(team->t.t_sched.sched, new_sched.sched);

      __kmp_reinitialize_team(team, new_icvs,
                              root->r.r_uber_thread->th.th_ident);

      KF_TRACE(10, ("__kmp_allocate_team2: T#%d, this_thread=%p team=%p\n", 0,
                    team->t.t_threads[0], team));
      __kmp_push_current_task_to_thread(team->t.t_threads[0], team, 0);

#if OMP_40_ENABLED
#if KMP_AFFINITY_SUPPORTED
      if ((team->t.t_size_changed == 0) &&
          (team->t.t_proc_bind == new_proc_bind)) {
        if (new_proc_bind == proc_bind_spread) {
          __kmp_partition_places(
              team, 1); // add flag to update only master for spread
        }
        KA_TRACE(200, ("__kmp_allocate_team: reusing hot team #%d bindings: "
                       "proc_bind = %d, partition = [%d,%d]\n",
                       team->t.t_id, new_proc_bind, team->t.t_first_place,
                       team->t.t_last_place));
      } else {
        KMP_CHECK_UPDATE(team->t.t_proc_bind, new_proc_bind);
        __kmp_partition_places(team);
      }
#else
      KMP_CHECK_UPDATE(team->t.t_proc_bind, new_proc_bind);
#endif /* KMP_AFFINITY_SUPPORTED */
#endif /* OMP_40_ENABLED */
    } else if (team->t.t_nproc > new_nproc) {
      KA_TRACE(20,
               ("__kmp_allocate_team: decreasing hot team thread count to %d\n",
                new_nproc));

      team->t.t_size_changed = 1;
#if KMP_NESTED_HOT_TEAMS
      if (__kmp_hot_teams_mode == 0) {
        // AC: saved number of threads should correspond to team's value in this
        // mode, can be bigger in mode 1, when hot team has threads in reserve
        KMP_DEBUG_ASSERT(hot_teams[level].hot_team_nth == team->t.t_nproc);
        hot_teams[level].hot_team_nth = new_nproc;
#endif // KMP_NESTED_HOT_TEAMS
        /* release the extra threads we don't need any more */
        for (f = new_nproc; f < team->t.t_nproc; f++) {
          KMP_DEBUG_ASSERT(team->t.t_threads[f]);
          if (__kmp_tasking_mode != tskm_immediate_exec) {
            // When decreasing team size, threads no longer in the team should
            // unref task team.
            team->t.t_threads[f]->th.th_task_team = NULL;
          }
          __kmp_free_thread(team->t.t_threads[f]);
          team->t.t_threads[f] = NULL;
        }
#if KMP_NESTED_HOT_TEAMS
      } // (__kmp_hot_teams_mode == 0)
      else {
        // When keeping extra threads in team, switch threads to wait on own
        // b_go flag
        for (f = new_nproc; f < team->t.t_nproc; ++f) {
          KMP_DEBUG_ASSERT(team->t.t_threads[f]);
          kmp_balign_t *balign = team->t.t_threads[f]->th.th_bar;
          for (int b = 0; b < bs_last_barrier; ++b) {
            if (balign[b].bb.wait_flag == KMP_BARRIER_PARENT_FLAG) {
              balign[b].bb.wait_flag = KMP_BARRIER_SWITCH_TO_OWN_FLAG;
            }
            KMP_CHECK_UPDATE(balign[b].bb.leaf_kids, 0);
          }
        }
      }
#endif // KMP_NESTED_HOT_TEAMS
      team->t.t_nproc = new_nproc;
      // TODO???: team->t.t_max_active_levels = new_max_active_levels;
      KMP_CHECK_UPDATE(team->t.t_sched.sched, new_icvs->sched.sched);
      __kmp_reinitialize_team(team, new_icvs,
                              root->r.r_uber_thread->th.th_ident);

      /* update the remaining threads */
      for (f = 0; f < new_nproc; ++f) {
        team->t.t_threads[f]->th.th_team_nproc = new_nproc;
      }
      // restore the current task state of the master thread: should be the
      // implicit task
      KF_TRACE(10, ("__kmp_allocate_team: T#%d, this_thread=%p team=%p\n", 0,
                    team->t.t_threads[0], team));

      __kmp_push_current_task_to_thread(team->t.t_threads[0], team, 0);

#ifdef KMP_DEBUG
      for (f = 0; f < team->t.t_nproc; f++) {
        KMP_DEBUG_ASSERT(team->t.t_threads[f] &&
                         team->t.t_threads[f]->th.th_team_nproc ==
                             team->t.t_nproc);
      }
#endif

#if OMP_40_ENABLED
      KMP_CHECK_UPDATE(team->t.t_proc_bind, new_proc_bind);
#if KMP_AFFINITY_SUPPORTED
      __kmp_partition_places(team);
#endif
#endif
    } else { // team->t.t_nproc < new_nproc
#if KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED
      kmp_affin_mask_t *old_mask;
      if (KMP_AFFINITY_CAPABLE()) {
        KMP_CPU_ALLOC(old_mask);
      }
#endif

      KA_TRACE(20,
               ("__kmp_allocate_team: increasing hot team thread count to %d\n",
                new_nproc));

      team->t.t_size_changed = 1;

#if KMP_NESTED_HOT_TEAMS
      int avail_threads = hot_teams[level].hot_team_nth;
      if (new_nproc < avail_threads)
        avail_threads = new_nproc;
      kmp_info_t **other_threads = team->t.t_threads;
      for (f = team->t.t_nproc; f < avail_threads; ++f) {
        // Adjust barrier data of reserved threads (if any) of the team
        // Other data will be set in __kmp_initialize_info() below.
        int b;
        kmp_balign_t *balign = other_threads[f]->th.th_bar;
        for (b = 0; b < bs_last_barrier; ++b) {
          balign[b].bb.b_arrived = team->t.t_bar[b].b_arrived;
          KMP_DEBUG_ASSERT(balign[b].bb.wait_flag != KMP_BARRIER_PARENT_FLAG);
#if USE_DEBUGGER
          balign[b].bb.b_worker_arrived = team->t.t_bar[b].b_team_arrived;
#endif
        }
      }
      if (hot_teams[level].hot_team_nth >= new_nproc) {
        // we have all needed threads in reserve, no need to allocate any
        // this only possible in mode 1, cannot have reserved threads in mode 0
        KMP_DEBUG_ASSERT(__kmp_hot_teams_mode == 1);
        team->t.t_nproc = new_nproc; // just get reserved threads involved
      } else {
        // we may have some threads in reserve, but not enough
        team->t.t_nproc =
            hot_teams[level]
                .hot_team_nth; // get reserved threads involved if any
        hot_teams[level].hot_team_nth = new_nproc; // adjust hot team max size
#endif // KMP_NESTED_HOT_TEAMS
        if (team->t.t_max_nproc < new_nproc) {
          /* reallocate larger arrays */
          __kmp_reallocate_team_arrays(team, new_nproc);
          __kmp_reinitialize_team(team, new_icvs, NULL);
        }

#if KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED
        /* Temporarily set full mask for master thread before creation of
           workers. The reason is that workers inherit the affinity from master,
           so if a lot of workers are created on the single core quickly, they
           don't get a chance to set their own affinity for a long time. */
        __kmp_set_thread_affinity_mask_full_tmp(old_mask);
#endif

        /* allocate new threads for the hot team */
        for (f = team->t.t_nproc; f < new_nproc; f++) {
          kmp_info_t *new_worker = __kmp_allocate_thread(root, team, f);
          KMP_DEBUG_ASSERT(new_worker);
          team->t.t_threads[f] = new_worker;

          KA_TRACE(20,
                   ("__kmp_allocate_team: team %d init T#%d arrived: "
                    "join=%llu, plain=%llu\n",
                    team->t.t_id, __kmp_gtid_from_tid(f, team), team->t.t_id, f,
                    team->t.t_bar[bs_forkjoin_barrier].b_arrived,
                    team->t.t_bar[bs_plain_barrier].b_arrived));

          { // Initialize barrier data for new threads.
            int b;
            kmp_balign_t *balign = new_worker->th.th_bar;
            for (b = 0; b < bs_last_barrier; ++b) {
              balign[b].bb.b_arrived = team->t.t_bar[b].b_arrived;
              KMP_DEBUG_ASSERT(balign[b].bb.wait_flag !=
                               KMP_BARRIER_PARENT_FLAG);
#if USE_DEBUGGER
              balign[b].bb.b_worker_arrived = team->t.t_bar[b].b_team_arrived;
#endif
            }
          }
        }

#if KMP_OS_LINUX && KMP_AFFINITY_SUPPORTED
        if (KMP_AFFINITY_CAPABLE()) {
          /* Restore initial master thread's affinity mask */
          __kmp_set_system_affinity(old_mask, TRUE);
          KMP_CPU_FREE(old_mask);
        }
#endif
#if KMP_NESTED_HOT_TEAMS
      } // end of check of t_nproc vs. new_nproc vs. hot_team_nth
#endif // KMP_NESTED_HOT_TEAMS
      /* make sure everyone is syncronized */
      int old_nproc = team->t.t_nproc; // save old value and use to update only
      // new threads below
      __kmp_initialize_team(team, new_nproc, new_icvs,
                            root->r.r_uber_thread->th.th_ident);

      /* reinitialize the threads */
      KMP_DEBUG_ASSERT(team->t.t_nproc == new_nproc);
      for (f = 0; f < team->t.t_nproc; ++f)
        __kmp_initialize_info(team->t.t_threads[f], team, f,
                              __kmp_gtid_from_tid(f, team));
      if (level) { // set th_task_state for new threads in nested hot team
        // __kmp_initialize_info() no longer zeroes th_task_state, so we should
        // only need to set the th_task_state for the new threads. th_task_state
        // for master thread will not be accurate until after this in
        // __kmp_fork_call(), so we look to the master's memo_stack to get the
        // correct value.
        for (f = old_nproc; f < team->t.t_nproc; ++f)
          team->t.t_threads[f]->th.th_task_state =
              team->t.t_threads[0]->th.th_task_state_memo_stack[level];
      } else { // set th_task_state for new threads in non-nested hot team
        int old_state =
            team->t.t_threads[0]->th.th_task_state; // copy master's state
        for (f = old_nproc; f < team->t.t_nproc; ++f)
          team->t.t_threads[f]->th.th_task_state = old_state;
      }

#ifdef KMP_DEBUG
      for (f = 0; f < team->t.t_nproc; ++f) {
        KMP_DEBUG_ASSERT(team->t.t_threads[f] &&
                         team->t.t_threads[f]->th.th_team_nproc ==
                             team->t.t_nproc);
      }
#endif

#if OMP_40_ENABLED
      KMP_CHECK_UPDATE(team->t.t_proc_bind, new_proc_bind);
#if KMP_AFFINITY_SUPPORTED
      __kmp_partition_places(team);
#endif
#endif
    } // Check changes in number of threads

#if OMP_40_ENABLED
    kmp_info_t *master = team->t.t_threads[0];
    if (master->th.th_teams_microtask) {
      for (f = 1; f < new_nproc; ++f) {
        // propagate teams construct specific info to workers
        kmp_info_t *thr = team->t.t_threads[f];
        thr->th.th_teams_microtask = master->th.th_teams_microtask;
        thr->th.th_teams_level = master->th.th_teams_level;
        thr->th.th_teams_size = master->th.th_teams_size;
      }
    }
#endif /* OMP_40_ENABLED */
#if KMP_NESTED_HOT_TEAMS
    if (level) {
      // Sync barrier state for nested hot teams, not needed for outermost hot
      // team.
      for (f = 1; f < new_nproc; ++f) {
        kmp_info_t *thr = team->t.t_threads[f];
        int b;
        kmp_balign_t *balign = thr->th.th_bar;
        for (b = 0; b < bs_last_barrier; ++b) {
          balign[b].bb.b_arrived = team->t.t_bar[b].b_arrived;
          KMP_DEBUG_ASSERT(balign[b].bb.wait_flag != KMP_BARRIER_PARENT_FLAG);
#if USE_DEBUGGER
          balign[b].bb.b_worker_arrived = team->t.t_bar[b].b_team_arrived;
#endif
        }
      }
    }
#endif // KMP_NESTED_HOT_TEAMS

    /* reallocate space for arguments if necessary */
    __kmp_alloc_argv_entries(argc, team, TRUE);
    KMP_CHECK_UPDATE(team->t.t_argc, argc);
    // The hot team re-uses the previous task team,
    // if untouched during the previous release->gather phase.

    KF_TRACE(10, (" hot_team = %p\n", team));

#if KMP_DEBUG
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      KA_TRACE(20, ("__kmp_allocate_team: hot team task_team[0] = %p "
                    "task_team[1] = %p after reinit\n",
                    team->t.t_task_team[0], team->t.t_task_team[1]));
    }
#endif

#if OMPT_SUPPORT
    __ompt_team_assign_id(team, ompt_parallel_data);
#endif

    KMP_MB();

    return team;
  }

  /* next, let's try to take one from the team pool */
  KMP_MB();
  for (team = CCAST(kmp_team_t *, __kmp_team_pool); (team);) {
    /* TODO: consider resizing undersized teams instead of reaping them, now
       that we have a resizing mechanism */
    if (team->t.t_max_nproc >= max_nproc) {
      /* take this team from the team pool */
      __kmp_team_pool = team->t.t_next_pool;

      /* setup the team for fresh use */
      __kmp_initialize_team(team, new_nproc, new_icvs, NULL);

      KA_TRACE(20, ("__kmp_allocate_team: setting task_team[0] %p and "
                    "task_team[1] %p to NULL\n",
                    &team->t.t_task_team[0], &team->t.t_task_team[1]));
      team->t.t_task_team[0] = NULL;
      team->t.t_task_team[1] = NULL;

      /* reallocate space for arguments if necessary */
      __kmp_alloc_argv_entries(argc, team, TRUE);
      KMP_CHECK_UPDATE(team->t.t_argc, argc);

      KA_TRACE(
          20, ("__kmp_allocate_team: team %d init arrived: join=%u, plain=%u\n",
               team->t.t_id, KMP_INIT_BARRIER_STATE, KMP_INIT_BARRIER_STATE));
      { // Initialize barrier data.
        int b;
        for (b = 0; b < bs_last_barrier; ++b) {
          team->t.t_bar[b].b_arrived = KMP_INIT_BARRIER_STATE;
#if USE_DEBUGGER
          team->t.t_bar[b].b_master_arrived = 0;
          team->t.t_bar[b].b_team_arrived = 0;
#endif
        }
      }

#if OMP_40_ENABLED
      team->t.t_proc_bind = new_proc_bind;
#endif

      KA_TRACE(20, ("__kmp_allocate_team: using team from pool %d.\n",
                    team->t.t_id));

#if OMPT_SUPPORT
      __ompt_team_assign_id(team, ompt_parallel_data);
#endif

      KMP_MB();

      return team;
    }

    /* reap team if it is too small, then loop back and check the next one */
    // not sure if this is wise, but, will be redone during the hot-teams
    // rewrite.
    /* TODO: Use technique to find the right size hot-team, don't reap them */
    team = __kmp_reap_team(team);
    __kmp_team_pool = team;
  }

  /* nothing available in the pool, no matter, make a new team! */
  KMP_MB();
  team = (kmp_team_t *)__kmp_allocate(sizeof(kmp_team_t));

  /* and set it up */
  team->t.t_max_nproc = max_nproc;
  /* NOTE well, for some reason allocating one big buffer and dividing it up
     seems to really hurt performance a lot on the P4, so, let's not use this */
  __kmp_allocate_team_arrays(team, max_nproc);

  KA_TRACE(20, ("__kmp_allocate_team: making a new team\n"));
  __kmp_initialize_team(team, new_nproc, new_icvs, NULL);

  KA_TRACE(20, ("__kmp_allocate_team: setting task_team[0] %p and task_team[1] "
                "%p to NULL\n",
                &team->t.t_task_team[0], &team->t.t_task_team[1]));
  team->t.t_task_team[0] = NULL; // to be removed, as __kmp_allocate zeroes
  // memory, no need to duplicate
  team->t.t_task_team[1] = NULL; // to be removed, as __kmp_allocate zeroes
  // memory, no need to duplicate

  if (__kmp_storage_map) {
    __kmp_print_team_storage_map("team", team, team->t.t_id, new_nproc);
  }

  /* allocate space for arguments */
  __kmp_alloc_argv_entries(argc, team, FALSE);
  team->t.t_argc = argc;

  KA_TRACE(20,
           ("__kmp_allocate_team: team %d init arrived: join=%u, plain=%u\n",
            team->t.t_id, KMP_INIT_BARRIER_STATE, KMP_INIT_BARRIER_STATE));
  { // Initialize barrier data.
    int b;
    for (b = 0; b < bs_last_barrier; ++b) {
      team->t.t_bar[b].b_arrived = KMP_INIT_BARRIER_STATE;
#if USE_DEBUGGER
      team->t.t_bar[b].b_master_arrived = 0;
      team->t.t_bar[b].b_team_arrived = 0;
#endif
    }
  }

#if OMP_40_ENABLED
  team->t.t_proc_bind = new_proc_bind;
#endif

#if OMPT_SUPPORT
  __ompt_team_assign_id(team, ompt_parallel_data);
  team->t.ompt_serialized_team_info = NULL;
#endif

  KMP_MB();

  KA_TRACE(20, ("__kmp_allocate_team: done creating a new team %d.\n",
                team->t.t_id));

  return team;
}

/* TODO implement hot-teams at all levels */
/* TODO implement lazy thread release on demand (disband request) */

/* free the team.  return it to the team pool.  release all the threads
 * associated with it */
void __kmp_free_team(kmp_root_t *root,
                     kmp_team_t *team USE_NESTED_HOT_ARG(kmp_info_t *master)) {
  int f;
  KA_TRACE(20, ("__kmp_free_team: T#%d freeing team %d\n", __kmp_get_gtid(),
                team->t.t_id));

  /* verify state */
  KMP_DEBUG_ASSERT(root);
  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(team->t.t_nproc <= team->t.t_max_nproc);
  KMP_DEBUG_ASSERT(team->t.t_threads);

  int use_hot_team = team == root->r.r_hot_team;
#if KMP_NESTED_HOT_TEAMS
  int level;
  kmp_hot_team_ptr_t *hot_teams;
  if (master) {
    level = team->t.t_active_level - 1;
    if (master->th.th_teams_microtask) { // in teams construct?
      if (master->th.th_teams_size.nteams > 1) {
        ++level; // level was not increased in teams construct for
        // team_of_masters
      }
      if (team->t.t_pkfn != (microtask_t)__kmp_teams_master &&
          master->th.th_teams_level == team->t.t_level) {
        ++level; // level was not increased in teams construct for
        // team_of_workers before the parallel
      } // team->t.t_level will be increased inside parallel
    }
    hot_teams = master->th.th_hot_teams;
    if (level < __kmp_hot_teams_max_level) {
      KMP_DEBUG_ASSERT(team == hot_teams[level].hot_team);
      use_hot_team = 1;
    }
  }
#endif // KMP_NESTED_HOT_TEAMS

  /* team is done working */
  TCW_SYNC_PTR(team->t.t_pkfn,
               NULL); // Important for Debugging Support Library.
#if KMP_OS_WINDOWS
  team->t.t_copyin_counter = 0; // init counter for possible reuse
#endif
  // Do not reset pointer to parent team to NULL for hot teams.

  /* if we are non-hot team, release our threads */
  if (!use_hot_team) {
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      // Wait for threads to reach reapable state
      for (f = 1; f < team->t.t_nproc; ++f) {
        KMP_DEBUG_ASSERT(team->t.t_threads[f]);
        kmp_info_t *th = team->t.t_threads[f];
        volatile kmp_uint32 *state = &th->th.th_reap_state;
        while (*state != KMP_SAFE_TO_REAP) {
#if KMP_OS_WINDOWS
          // On Windows a thread can be killed at any time, check this
          DWORD ecode;
          if (!__kmp_is_thread_alive(th, &ecode)) {
            *state = KMP_SAFE_TO_REAP; // reset the flag for dead thread
            break;
          }
#endif
          // first check if thread is sleeping
          kmp_flag_64 fl(&th->th.th_bar[bs_forkjoin_barrier].bb.b_go, th);
          if (fl.is_sleeping())
            fl.resume(__kmp_gtid_from_thread(th));
          KMP_CPU_PAUSE();
        }
      }

      // Delete task teams
      int tt_idx;
      for (tt_idx = 0; tt_idx < 2; ++tt_idx) {
        kmp_task_team_t *task_team = team->t.t_task_team[tt_idx];
        if (task_team != NULL) {
          for (f = 0; f < team->t.t_nproc;
               ++f) { // Have all threads unref task teams
            team->t.t_threads[f]->th.th_task_team = NULL;
          }
          KA_TRACE(
              20,
              ("__kmp_free_team: T#%d deactivating task_team %p on team %d\n",
               __kmp_get_gtid(), task_team, team->t.t_id));
#if KMP_NESTED_HOT_TEAMS
          __kmp_free_task_team(master, task_team);
#endif
          team->t.t_task_team[tt_idx] = NULL;
        }
      }
    }

    // Reset pointer to parent team only for non-hot teams.
    team->t.t_parent = NULL;
    team->t.t_level = 0;
    team->t.t_active_level = 0;

    /* free the worker threads */
    for (f = 1; f < team->t.t_nproc; ++f) {
      KMP_DEBUG_ASSERT(team->t.t_threads[f]);
      __kmp_free_thread(team->t.t_threads[f]);
      team->t.t_threads[f] = NULL;
    }

    /* put the team back in the team pool */
    /* TODO limit size of team pool, call reap_team if pool too large */
    team->t.t_next_pool = CCAST(kmp_team_t *, __kmp_team_pool);
    __kmp_team_pool = (volatile kmp_team_t *)team;
  }

  KMP_MB();
}

/* reap the team.  destroy it, reclaim all its resources and free its memory */
kmp_team_t *__kmp_reap_team(kmp_team_t *team) {
  kmp_team_t *next_pool = team->t.t_next_pool;

  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(team->t.t_dispatch);
  KMP_DEBUG_ASSERT(team->t.t_disp_buffer);
  KMP_DEBUG_ASSERT(team->t.t_threads);
  KMP_DEBUG_ASSERT(team->t.t_argv);

  /* TODO clean the threads that are a part of this? */

  /* free stuff */
  __kmp_free_team_arrays(team);
  if (team->t.t_argv != &team->t.t_inline_argv[0])
    __kmp_free((void *)team->t.t_argv);
  __kmp_free(team);

  KMP_MB();
  return next_pool;
}

// Free the thread.  Don't reap it, just place it on the pool of available
// threads.
//
// Changes for Quad issue 527845: We need a predictable OMP tid <-> gtid
// binding for the affinity mechanism to be useful.
//
// Now, we always keep the free list (__kmp_thread_pool) sorted by gtid.
// However, we want to avoid a potential performance problem by always
// scanning through the list to find the correct point at which to insert
// the thread (potential N**2 behavior).  To do this we keep track of the
// last place a thread struct was inserted (__kmp_thread_pool_insert_pt).
// With single-level parallelism, threads will always be added to the tail
// of the list, kept track of by __kmp_thread_pool_insert_pt.  With nested
// parallelism, all bets are off and we may need to scan through the entire
// free list.
//
// This change also has a potentially large performance benefit, for some
// applications.  Previously, as threads were freed from the hot team, they
// would be placed back on the free list in inverse order.  If the hot team
// grew back to it's original size, then the freed thread would be placed
// back on the hot team in reverse order.  This could cause bad cache
// locality problems on programs where the size of the hot team regularly
// grew and shrunk.
//
// Now, for single-level parallelism, the OMP tid is alway == gtid.
void __kmp_free_thread(kmp_info_t *this_th) {
  int gtid;
  kmp_info_t **scan;
  kmp_root_t *root = this_th->th.th_root;

  KA_TRACE(20, ("__kmp_free_thread: T#%d putting T#%d back on free pool.\n",
                __kmp_get_gtid(), this_th->th.th_info.ds.ds_gtid));

  KMP_DEBUG_ASSERT(this_th);

  // When moving thread to pool, switch thread to wait on own b_go flag, and
  // uninitialized (NULL team).
  int b;
  kmp_balign_t *balign = this_th->th.th_bar;
  for (b = 0; b < bs_last_barrier; ++b) {
    if (balign[b].bb.wait_flag == KMP_BARRIER_PARENT_FLAG)
      balign[b].bb.wait_flag = KMP_BARRIER_SWITCH_TO_OWN_FLAG;
    balign[b].bb.team = NULL;
    balign[b].bb.leaf_kids = 0;
  }
  this_th->th.th_task_state = 0;
  this_th->th.th_reap_state = KMP_SAFE_TO_REAP;

  /* put thread back on the free pool */
  TCW_PTR(this_th->th.th_team, NULL);
  TCW_PTR(this_th->th.th_root, NULL);
  TCW_PTR(this_th->th.th_dispatch, NULL); /* NOT NEEDED */

  /* If the implicit task assigned to this thread can be used by other threads
   * -> multiple threads can share the data and try to free the task at
   * __kmp_reap_thread at exit. This duplicate use of the task data can happen
   * with higher probability when hot team is disabled but can occurs even when
   * the hot team is enabled */
  __kmp_free_implicit_task(this_th);
  this_th->th.th_current_task = NULL;

  // If the __kmp_thread_pool_insert_pt is already past the new insert
  // point, then we need to re-scan the entire list.
  gtid = this_th->th.th_info.ds.ds_gtid;
  if (__kmp_thread_pool_insert_pt != NULL) {
    KMP_DEBUG_ASSERT(__kmp_thread_pool != NULL);
    if (__kmp_thread_pool_insert_pt->th.th_info.ds.ds_gtid > gtid) {
      __kmp_thread_pool_insert_pt = NULL;
    }
  }

  // Scan down the list to find the place to insert the thread.
  // scan is the address of a link in the list, possibly the address of
  // __kmp_thread_pool itself.
  //
  // In the absence of nested parallism, the for loop will have 0 iterations.
  if (__kmp_thread_pool_insert_pt != NULL) {
    scan = &(__kmp_thread_pool_insert_pt->th.th_next_pool);
  } else {
    scan = CCAST(kmp_info_t **, &__kmp_thread_pool);
  }
  for (; (*scan != NULL) && ((*scan)->th.th_info.ds.ds_gtid < gtid);
       scan = &((*scan)->th.th_next_pool))
    ;

  // Insert the new element on the list, and set __kmp_thread_pool_insert_pt
  // to its address.
  TCW_PTR(this_th->th.th_next_pool, *scan);
  __kmp_thread_pool_insert_pt = *scan = this_th;
  KMP_DEBUG_ASSERT((this_th->th.th_next_pool == NULL) ||
                   (this_th->th.th_info.ds.ds_gtid <
                    this_th->th.th_next_pool->th.th_info.ds.ds_gtid));
  TCW_4(this_th->th.th_in_pool, TRUE);
  __kmp_thread_pool_nth++;

  TCW_4(__kmp_nth, __kmp_nth - 1);
  root->r.r_cg_nthreads--;

#ifdef KMP_ADJUST_BLOCKTIME
  /* Adjust blocktime back to user setting or default if necessary */
  /* Middle initialization might never have occurred                */
  if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
    KMP_DEBUG_ASSERT(__kmp_avail_proc > 0);
    if (__kmp_nth <= __kmp_avail_proc) {
      __kmp_zero_bt = FALSE;
    }
  }
#endif /* KMP_ADJUST_BLOCKTIME */

  KMP_MB();
}

/* ------------------------------------------------------------------------ */

void *__kmp_launch_thread(kmp_info_t *this_thr) {
  int gtid = this_thr->th.th_info.ds.ds_gtid;
  /*    void                 *stack_data;*/
  kmp_team_t *(*volatile pteam);

  KMP_MB();
  KA_TRACE(10, ("__kmp_launch_thread: T#%d start\n", gtid));

  if (__kmp_env_consistency_check) {
    this_thr->th.th_cons = __kmp_allocate_cons_stack(gtid); // ATT: Memory leak?
  }

#if OMPT_SUPPORT
  ompt_data_t *thread_data;
  if (ompt_enabled.enabled) {
    thread_data = &(this_thr->th.ompt_thread_info.thread_data);
    *thread_data = ompt_data_none;

    this_thr->th.ompt_thread_info.state = ompt_state_overhead;
    this_thr->th.ompt_thread_info.wait_id = 0;
    this_thr->th.ompt_thread_info.idle_frame = OMPT_GET_FRAME_ADDRESS(0);
    if (ompt_enabled.ompt_callback_thread_begin) {
      ompt_callbacks.ompt_callback(ompt_callback_thread_begin)(
          ompt_thread_worker, thread_data);
    }
  }
#endif

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    this_thr->th.ompt_thread_info.state = ompt_state_idle;
  }
#endif
  /* This is the place where threads wait for work */
  while (!TCR_4(__kmp_global.g.g_done)) {
    KMP_DEBUG_ASSERT(this_thr == __kmp_threads[gtid]);
    KMP_MB();

    /* wait for work to do */
    KA_TRACE(20, ("__kmp_launch_thread: T#%d waiting for work\n", gtid));

    /* No tid yet since not part of a team */
    __kmp_fork_barrier(gtid, KMP_GTID_DNE);

#if OMPT_SUPPORT
    if (ompt_enabled.enabled) {
      this_thr->th.ompt_thread_info.state = ompt_state_overhead;
    }
#endif

    pteam = (kmp_team_t * (*))(&this_thr->th.th_team);

    /* have we been allocated? */
    if (TCR_SYNC_PTR(*pteam) && !TCR_4(__kmp_global.g.g_done)) {
      /* we were just woken up, so run our new task */
      if (TCR_SYNC_PTR((*pteam)->t.t_pkfn) != NULL) {
        int rc;
        KA_TRACE(20,
                 ("__kmp_launch_thread: T#%d(%d:%d) invoke microtask = %p\n",
                  gtid, (*pteam)->t.t_id, __kmp_tid_from_gtid(gtid),
                  (*pteam)->t.t_pkfn));

        updateHWFPControl(*pteam);

#if OMPT_SUPPORT
        if (ompt_enabled.enabled) {
          this_thr->th.ompt_thread_info.state = ompt_state_work_parallel;
        }
#endif

        rc = (*pteam)->t.t_invoke(gtid);
        KMP_ASSERT(rc);

        KMP_MB();
        KA_TRACE(20, ("__kmp_launch_thread: T#%d(%d:%d) done microtask = %p\n",
                      gtid, (*pteam)->t.t_id, __kmp_tid_from_gtid(gtid),
                      (*pteam)->t.t_pkfn));
      }
#if OMPT_SUPPORT
      if (ompt_enabled.enabled) {
        /* no frame set while outside task */
        __ompt_get_task_info_object(0)->frame.exit_frame = ompt_data_none;

        this_thr->th.ompt_thread_info.state = ompt_state_overhead;
      }
#endif
      /* join barrier after parallel region */
      __kmp_join_barrier(gtid);
    }
  }
  TCR_SYNC_PTR((intptr_t)__kmp_global.g.g_done);

#if OMPT_SUPPORT
  if (ompt_enabled.ompt_callback_thread_end) {
    ompt_callbacks.ompt_callback(ompt_callback_thread_end)(thread_data);
  }
#endif

  this_thr->th.th_task_team = NULL;
  /* run the destructors for the threadprivate data for this thread */
  __kmp_common_destroy_gtid(gtid);

  KA_TRACE(10, ("__kmp_launch_thread: T#%d done\n", gtid));
  KMP_MB();
  return this_thr;
}

/* ------------------------------------------------------------------------ */

void __kmp_internal_end_dest(void *specific_gtid) {
#if KMP_COMPILER_ICC
#pragma warning(push)
#pragma warning(disable : 810) // conversion from "void *" to "int" may lose
// significant bits
#endif
  // Make sure no significant bits are lost
  int gtid = (kmp_intptr_t)specific_gtid - 1;
#if KMP_COMPILER_ICC
#pragma warning(pop)
#endif

  KA_TRACE(30, ("__kmp_internal_end_dest: T#%d\n", gtid));
  /* NOTE: the gtid is stored as gitd+1 in the thread-local-storage
   * this is because 0 is reserved for the nothing-stored case */

  /* josh: One reason for setting the gtid specific data even when it is being
     destroyed by pthread is to allow gtid lookup through thread specific data
     (__kmp_gtid_get_specific).  Some of the code, especially stat code,
     that gets executed in the call to __kmp_internal_end_thread, actually
     gets the gtid through the thread specific data.  Setting it here seems
     rather inelegant and perhaps wrong, but allows __kmp_internal_end_thread
     to run smoothly.
     todo: get rid of this after we remove the dependence on
     __kmp_gtid_get_specific  */
  if (gtid >= 0 && KMP_UBER_GTID(gtid))
    __kmp_gtid_set_specific(gtid);
#ifdef KMP_TDATA_GTID
  __kmp_gtid = gtid;
#endif
  __kmp_internal_end_thread(gtid);
}

#if KMP_OS_UNIX && KMP_DYNAMIC_LIB

// 2009-09-08 (lev): It looks the destructor does not work. In simple test cases
// destructors work perfectly, but in real libomp.so I have no evidence it is
// ever called. However, -fini linker option in makefile.mk works fine.

__attribute__((destructor)) void __kmp_internal_end_dtor(void) {
  __kmp_internal_end_atexit();
}

void __kmp_internal_end_fini(void) { __kmp_internal_end_atexit(); }

#endif

/* [Windows] josh: when the atexit handler is called, there may still be more
   than one thread alive */
void __kmp_internal_end_atexit(void) {
  KA_TRACE(30, ("__kmp_internal_end_atexit\n"));
  /* [Windows]
     josh: ideally, we want to completely shutdown the library in this atexit
     handler, but stat code that depends on thread specific data for gtid fails
     because that data becomes unavailable at some point during the shutdown, so
     we call __kmp_internal_end_thread instead. We should eventually remove the
     dependency on __kmp_get_specific_gtid in the stat code and use
     __kmp_internal_end_library to cleanly shutdown the library.

     // TODO: Can some of this comment about GVS be removed?
     I suspect that the offending stat code is executed when the calling thread
     tries to clean up a dead root thread's data structures, resulting in GVS
     code trying to close the GVS structures for that thread, but since the stat
     code uses __kmp_get_specific_gtid to get the gtid with the assumption that
     the calling thread is cleaning up itself instead of another thread, it get
     confused. This happens because allowing a thread to unregister and cleanup
     another thread is a recent modification for addressing an issue.
     Based on the current design (20050722), a thread may end up
     trying to unregister another thread only if thread death does not trigger
     the calling of __kmp_internal_end_thread.  For Linux* OS, there is the
     thread specific data destructor function to detect thread death. For
     Windows dynamic, there is DllMain(THREAD_DETACH). For Windows static, there
     is nothing.  Thus, the workaround is applicable only for Windows static
     stat library. */
  __kmp_internal_end_library(-1);
#if KMP_OS_WINDOWS
  __kmp_close_console();
#endif
}

static void __kmp_reap_thread(kmp_info_t *thread, int is_root) {
  // It is assumed __kmp_forkjoin_lock is acquired.

  int gtid;

  KMP_DEBUG_ASSERT(thread != NULL);

  gtid = thread->th.th_info.ds.ds_gtid;

  if (!is_root) {

    if (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME) {
      /* Assume the threads are at the fork barrier here */
      KA_TRACE(
          20, ("__kmp_reap_thread: releasing T#%d from fork barrier for reap\n",
               gtid));
      /* Need release fence here to prevent seg faults for tree forkjoin barrier
       * (GEH) */
      ANNOTATE_HAPPENS_BEFORE(thread);
      kmp_flag_64 flag(&thread->th.th_bar[bs_forkjoin_barrier].bb.b_go, thread);
      __kmp_release_64(&flag);
    }

    // Terminate OS thread.
    __kmp_reap_worker(thread);

    // The thread was killed asynchronously.  If it was actively
    // spinning in the thread pool, decrement the global count.
    //
    // There is a small timing hole here - if the worker thread was just waking
    // up after sleeping in the pool, had reset it's th_active_in_pool flag but
    // not decremented the global counter __kmp_thread_pool_active_nth yet, then
    // the global counter might not get updated.
    //
    // Currently, this can only happen as the library is unloaded,
    // so there are no harmful side effects.
    if (thread->th.th_active_in_pool) {
      thread->th.th_active_in_pool = FALSE;
      KMP_ATOMIC_DEC(&__kmp_thread_pool_active_nth);
      KMP_DEBUG_ASSERT(__kmp_thread_pool_active_nth >= 0);
    }

    // Decrement # of [worker] threads in the pool.
    KMP_DEBUG_ASSERT(__kmp_thread_pool_nth > 0);
    --__kmp_thread_pool_nth;
  }

  __kmp_free_implicit_task(thread);

// Free the fast memory for tasking
#if USE_FAST_MEMORY
  __kmp_free_fast_memory(thread);
#endif /* USE_FAST_MEMORY */

  __kmp_suspend_uninitialize_thread(thread);

  KMP_DEBUG_ASSERT(__kmp_threads[gtid] == thread);
  TCW_SYNC_PTR(__kmp_threads[gtid], NULL);

  --__kmp_all_nth;
// __kmp_nth was decremented when thread is added to the pool.

#ifdef KMP_ADJUST_BLOCKTIME
  /* Adjust blocktime back to user setting or default if necessary */
  /* Middle initialization might never have occurred                */
  if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
    KMP_DEBUG_ASSERT(__kmp_avail_proc > 0);
    if (__kmp_nth <= __kmp_avail_proc) {
      __kmp_zero_bt = FALSE;
    }
  }
#endif /* KMP_ADJUST_BLOCKTIME */

  /* free the memory being used */
  if (__kmp_env_consistency_check) {
    if (thread->th.th_cons) {
      __kmp_free_cons_stack(thread->th.th_cons);
      thread->th.th_cons = NULL;
    }
  }

  if (thread->th.th_pri_common != NULL) {
    __kmp_free(thread->th.th_pri_common);
    thread->th.th_pri_common = NULL;
  }

  if (thread->th.th_task_state_memo_stack != NULL) {
    __kmp_free(thread->th.th_task_state_memo_stack);
    thread->th.th_task_state_memo_stack = NULL;
  }

#if KMP_USE_BGET
  if (thread->th.th_local.bget_data != NULL) {
    __kmp_finalize_bget(thread);
  }
#endif

#if KMP_AFFINITY_SUPPORTED
  if (thread->th.th_affin_mask != NULL) {
    KMP_CPU_FREE(thread->th.th_affin_mask);
    thread->th.th_affin_mask = NULL;
  }
#endif /* KMP_AFFINITY_SUPPORTED */

#if KMP_USE_HIER_SCHED
  if (thread->th.th_hier_bar_data != NULL) {
    __kmp_free(thread->th.th_hier_bar_data);
    thread->th.th_hier_bar_data = NULL;
  }
#endif

  __kmp_reap_team(thread->th.th_serial_team);
  thread->th.th_serial_team = NULL;
  __kmp_free(thread);

  KMP_MB();

} // __kmp_reap_thread

static void __kmp_internal_end(void) {
  int i;

  /* First, unregister the library */
  __kmp_unregister_library();

#if KMP_OS_WINDOWS
  /* In Win static library, we can't tell when a root actually dies, so we
     reclaim the data structures for any root threads that have died but not
     unregistered themselves, in order to shut down cleanly.
     In Win dynamic library we also can't tell when a thread dies.  */
  __kmp_reclaim_dead_roots(); // AC: moved here to always clean resources of
// dead roots
#endif

  for (i = 0; i < __kmp_threads_capacity; i++)
    if (__kmp_root[i])
      if (__kmp_root[i]->r.r_active)
        break;
  KMP_MB(); /* Flush all pending memory write invalidates.  */
  TCW_SYNC_4(__kmp_global.g.g_done, TRUE);

  if (i < __kmp_threads_capacity) {
#if KMP_USE_MONITOR
    // 2009-09-08 (lev): Other alive roots found. Why do we kill the monitor??
    KMP_MB(); /* Flush all pending memory write invalidates.  */

    // Need to check that monitor was initialized before reaping it. If we are
    // called form __kmp_atfork_child (which sets __kmp_init_parallel = 0), then
    // __kmp_monitor will appear to contain valid data, but it is only valid in
    // the parent process, not the child.
    // New behavior (201008): instead of keying off of the flag
    // __kmp_init_parallel, the monitor thread creation is keyed off
    // of the new flag __kmp_init_monitor.
    __kmp_acquire_bootstrap_lock(&__kmp_monitor_lock);
    if (TCR_4(__kmp_init_monitor)) {
      __kmp_reap_monitor(&__kmp_monitor);
      TCW_4(__kmp_init_monitor, 0);
    }
    __kmp_release_bootstrap_lock(&__kmp_monitor_lock);
    KA_TRACE(10, ("__kmp_internal_end: monitor reaped\n"));
#endif // KMP_USE_MONITOR
  } else {
/* TODO move this to cleanup code */
#ifdef KMP_DEBUG
    /* make sure that everything has properly ended */
    for (i = 0; i < __kmp_threads_capacity; i++) {
      if (__kmp_root[i]) {
        //                    KMP_ASSERT( ! KMP_UBER_GTID( i ) );         // AC:
        //                    there can be uber threads alive here
        KMP_ASSERT(!__kmp_root[i]->r.r_active); // TODO: can they be active?
      }
    }
#endif

    KMP_MB();

    // Reap the worker threads.
    // This is valid for now, but be careful if threads are reaped sooner.
    while (__kmp_thread_pool != NULL) { // Loop thru all the thread in the pool.
      // Get the next thread from the pool.
      kmp_info_t *thread = CCAST(kmp_info_t *, __kmp_thread_pool);
      __kmp_thread_pool = thread->th.th_next_pool;
      // Reap it.
      KMP_DEBUG_ASSERT(thread->th.th_reap_state == KMP_SAFE_TO_REAP);
      thread->th.th_next_pool = NULL;
      thread->th.th_in_pool = FALSE;
      __kmp_reap_thread(thread, 0);
    }
    __kmp_thread_pool_insert_pt = NULL;

    // Reap teams.
    while (__kmp_team_pool != NULL) { // Loop thru all the teams in the pool.
      // Get the next team from the pool.
      kmp_team_t *team = CCAST(kmp_team_t *, __kmp_team_pool);
      __kmp_team_pool = team->t.t_next_pool;
      // Reap it.
      team->t.t_next_pool = NULL;
      __kmp_reap_team(team);
    }

    __kmp_reap_task_teams();

#if KMP_OS_UNIX
    // Threads that are not reaped should not access any resources since they
    // are going to be deallocated soon, so the shutdown sequence should wait
    // until all threads either exit the final spin-waiting loop or begin
    // sleeping after the given blocktime.
    for (i = 0; i < __kmp_threads_capacity; i++) {
      kmp_info_t *thr = __kmp_threads[i];
      while (thr && KMP_ATOMIC_LD_ACQ(&thr->th.th_blocking))
        KMP_CPU_PAUSE();
    }
#endif

    for (i = 0; i < __kmp_threads_capacity; ++i) {
      // TBD: Add some checking...
      // Something like KMP_DEBUG_ASSERT( __kmp_thread[ i ] == NULL );
    }

    /* Make sure all threadprivate destructors get run by joining with all
       worker threads before resetting this flag */
    TCW_SYNC_4(__kmp_init_common, FALSE);

    KA_TRACE(10, ("__kmp_internal_end: all workers reaped\n"));
    KMP_MB();

#if KMP_USE_MONITOR
    // See note above: One of the possible fixes for CQ138434 / CQ140126
    //
    // FIXME: push both code fragments down and CSE them?
    // push them into __kmp_cleanup() ?
    __kmp_acquire_bootstrap_lock(&__kmp_monitor_lock);
    if (TCR_4(__kmp_init_monitor)) {
      __kmp_reap_monitor(&__kmp_monitor);
      TCW_4(__kmp_init_monitor, 0);
    }
    __kmp_release_bootstrap_lock(&__kmp_monitor_lock);
    KA_TRACE(10, ("__kmp_internal_end: monitor reaped\n"));
#endif
  } /* else !__kmp_global.t_active */
  TCW_4(__kmp_init_gtid, FALSE);
  KMP_MB(); /* Flush all pending memory write invalidates.  */

  __kmp_cleanup();
#if OMPT_SUPPORT
  ompt_fini();
#endif
}

void __kmp_internal_end_library(int gtid_req) {
  /* if we have already cleaned up, don't try again, it wouldn't be pretty */
  /* this shouldn't be a race condition because __kmp_internal_end() is the
     only place to clear __kmp_serial_init */
  /* we'll check this later too, after we get the lock */
  // 2009-09-06: We do not set g_abort without setting g_done. This check looks
  // redundaant, because the next check will work in any case.
  if (__kmp_global.g.g_abort) {
    KA_TRACE(11, ("__kmp_internal_end_library: abort, exiting\n"));
    /* TODO abort? */
    return;
  }
  if (TCR_4(__kmp_global.g.g_done) || !__kmp_init_serial) {
    KA_TRACE(10, ("__kmp_internal_end_library: already finished\n"));
    return;
  }

  KMP_MB(); /* Flush all pending memory write invalidates.  */

  /* find out who we are and what we should do */
  {
    int gtid = (gtid_req >= 0) ? gtid_req : __kmp_gtid_get_specific();
    KA_TRACE(
        10, ("__kmp_internal_end_library: enter T#%d  (%d)\n", gtid, gtid_req));
    if (gtid == KMP_GTID_SHUTDOWN) {
      KA_TRACE(10, ("__kmp_internal_end_library: !__kmp_init_runtime, system "
                    "already shutdown\n"));
      return;
    } else if (gtid == KMP_GTID_MONITOR) {
      KA_TRACE(10, ("__kmp_internal_end_library: monitor thread, gtid not "
                    "registered, or system shutdown\n"));
      return;
    } else if (gtid == KMP_GTID_DNE) {
      KA_TRACE(10, ("__kmp_internal_end_library: gtid not registered or system "
                    "shutdown\n"));
      /* we don't know who we are, but we may still shutdown the library */
    } else if (KMP_UBER_GTID(gtid)) {
      /* unregister ourselves as an uber thread.  gtid is no longer valid */
      if (__kmp_root[gtid]->r.r_active) {
        __kmp_global.g.g_abort = -1;
        TCW_SYNC_4(__kmp_global.g.g_done, TRUE);
        KA_TRACE(10,
                 ("__kmp_internal_end_library: root still active, abort T#%d\n",
                  gtid));
        return;
      } else {
        KA_TRACE(
            10,
            ("__kmp_internal_end_library: unregistering sibling T#%d\n", gtid));
        __kmp_unregister_root_current_thread(gtid);
      }
    } else {
/* worker threads may call this function through the atexit handler, if they
 * call exit() */
/* For now, skip the usual subsequent processing and just dump the debug buffer.
   TODO: do a thorough shutdown instead */
#ifdef DUMP_DEBUG_ON_EXIT
      if (__kmp_debug_buf)
        __kmp_dump_debug_buffer();
#endif
      return;
    }
  }
  /* synchronize the termination process */
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);

  /* have we already finished */
  if (__kmp_global.g.g_abort) {
    KA_TRACE(10, ("__kmp_internal_end_library: abort, exiting\n"));
    /* TODO abort? */
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }
  if (TCR_4(__kmp_global.g.g_done) || !__kmp_init_serial) {
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }

  /* We need this lock to enforce mutex between this reading of
     __kmp_threads_capacity and the writing by __kmp_register_root.
     Alternatively, we can use a counter of roots that is atomically updated by
     __kmp_get_global_thread_id_reg, __kmp_do_serial_initialize and
     __kmp_internal_end_*.  */
  __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);

  /* now we can safely conduct the actual termination */
  __kmp_internal_end();

  __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
  __kmp_release_bootstrap_lock(&__kmp_initz_lock);

  KA_TRACE(10, ("__kmp_internal_end_library: exit\n"));

#ifdef DUMP_DEBUG_ON_EXIT
  if (__kmp_debug_buf)
    __kmp_dump_debug_buffer();
#endif

#if KMP_OS_WINDOWS
  __kmp_close_console();
#endif

  __kmp_fini_allocator();

} // __kmp_internal_end_library

void __kmp_internal_end_thread(int gtid_req) {
  int i;

  /* if we have already cleaned up, don't try again, it wouldn't be pretty */
  /* this shouldn't be a race condition because __kmp_internal_end() is the
   * only place to clear __kmp_serial_init */
  /* we'll check this later too, after we get the lock */
  // 2009-09-06: We do not set g_abort without setting g_done. This check looks
  // redundant, because the next check will work in any case.
  if (__kmp_global.g.g_abort) {
    KA_TRACE(11, ("__kmp_internal_end_thread: abort, exiting\n"));
    /* TODO abort? */
    return;
  }
  if (TCR_4(__kmp_global.g.g_done) || !__kmp_init_serial) {
    KA_TRACE(10, ("__kmp_internal_end_thread: already finished\n"));
    return;
  }

  KMP_MB(); /* Flush all pending memory write invalidates.  */

  /* find out who we are and what we should do */
  {
    int gtid = (gtid_req >= 0) ? gtid_req : __kmp_gtid_get_specific();
    KA_TRACE(10,
             ("__kmp_internal_end_thread: enter T#%d  (%d)\n", gtid, gtid_req));
    if (gtid == KMP_GTID_SHUTDOWN) {
      KA_TRACE(10, ("__kmp_internal_end_thread: !__kmp_init_runtime, system "
                    "already shutdown\n"));
      return;
    } else if (gtid == KMP_GTID_MONITOR) {
      KA_TRACE(10, ("__kmp_internal_end_thread: monitor thread, gtid not "
                    "registered, or system shutdown\n"));
      return;
    } else if (gtid == KMP_GTID_DNE) {
      KA_TRACE(10, ("__kmp_internal_end_thread: gtid not registered or system "
                    "shutdown\n"));
      return;
      /* we don't know who we are */
    } else if (KMP_UBER_GTID(gtid)) {
      /* unregister ourselves as an uber thread.  gtid is no longer valid */
      if (__kmp_root[gtid]->r.r_active) {
        __kmp_global.g.g_abort = -1;
        TCW_SYNC_4(__kmp_global.g.g_done, TRUE);
        KA_TRACE(10,
                 ("__kmp_internal_end_thread: root still active, abort T#%d\n",
                  gtid));
        return;
      } else {
        KA_TRACE(10, ("__kmp_internal_end_thread: unregistering sibling T#%d\n",
                      gtid));
        __kmp_unregister_root_current_thread(gtid);
      }
    } else {
      /* just a worker thread, let's leave */
      KA_TRACE(10, ("__kmp_internal_end_thread: worker thread T#%d\n", gtid));

      if (gtid >= 0) {
        __kmp_threads[gtid]->th.th_task_team = NULL;
      }

      KA_TRACE(10,
               ("__kmp_internal_end_thread: worker thread done, exiting T#%d\n",
                gtid));
      return;
    }
  }
#if KMP_DYNAMIC_LIB
  // AC: lets not shutdown the Linux* OS dynamic library at the exit of uber
  // thread, because we will better shutdown later in the library destructor.
  // The reason of this change is performance problem when non-openmp thread in
  // a loop forks and joins many openmp threads. We can save a lot of time
  // keeping worker threads alive until the program shutdown.
  // OM: Removed Linux* OS restriction to fix the crash on OS X* (DPD200239966)
  // and Windows(DPD200287443) that occurs when using critical sections from
  // foreign threads.
  KA_TRACE(10, ("__kmp_internal_end_thread: exiting T#%d\n", gtid_req));
  return;
#endif
  /* synchronize the termination process */
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);

  /* have we already finished */
  if (__kmp_global.g.g_abort) {
    KA_TRACE(10, ("__kmp_internal_end_thread: abort, exiting\n"));
    /* TODO abort? */
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }
  if (TCR_4(__kmp_global.g.g_done) || !__kmp_init_serial) {
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }

  /* We need this lock to enforce mutex between this reading of
     __kmp_threads_capacity and the writing by __kmp_register_root.
     Alternatively, we can use a counter of roots that is atomically updated by
     __kmp_get_global_thread_id_reg, __kmp_do_serial_initialize and
     __kmp_internal_end_*.  */

  /* should we finish the run-time?  are all siblings done? */
  __kmp_acquire_bootstrap_lock(&__kmp_forkjoin_lock);

  for (i = 0; i < __kmp_threads_capacity; ++i) {
    if (KMP_UBER_GTID(i)) {
      KA_TRACE(
          10,
          ("__kmp_internal_end_thread: remaining sibling task: gtid==%d\n", i));
      __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
      __kmp_release_bootstrap_lock(&__kmp_initz_lock);
      return;
    }
  }

  /* now we can safely conduct the actual termination */

  __kmp_internal_end();

  __kmp_release_bootstrap_lock(&__kmp_forkjoin_lock);
  __kmp_release_bootstrap_lock(&__kmp_initz_lock);

  KA_TRACE(10, ("__kmp_internal_end_thread: exit T#%d\n", gtid_req));

#ifdef DUMP_DEBUG_ON_EXIT
  if (__kmp_debug_buf)
    __kmp_dump_debug_buffer();
#endif
} // __kmp_internal_end_thread

// -----------------------------------------------------------------------------
// Library registration stuff.

static long __kmp_registration_flag = 0;
// Random value used to indicate library initialization.
static char *__kmp_registration_str = NULL;
// Value to be saved in env var __KMP_REGISTERED_LIB_<pid>.

static inline char *__kmp_reg_status_name() {
  /* On RHEL 3u5 if linked statically, getpid() returns different values in
     each thread. If registration and unregistration go in different threads
     (omp_misc_other_root_exit.cpp test case), the name of registered_lib_env
     env var can not be found, because the name will contain different pid. */
  return __kmp_str_format("__KMP_REGISTERED_LIB_%d", (int)getpid());
} // __kmp_reg_status_get

void __kmp_register_library_startup(void) {

  char *name = __kmp_reg_status_name(); // Name of the environment variable.
  int done = 0;
  union {
    double dtime;
    long ltime;
  } time;
#if KMP_ARCH_X86 || KMP_ARCH_X86_64
  __kmp_initialize_system_tick();
#endif
  __kmp_read_system_time(&time.dtime);
  __kmp_registration_flag = 0xCAFE0000L | (time.ltime & 0x0000FFFFL);
  __kmp_registration_str =
      __kmp_str_format("%p-%lx-%s", &__kmp_registration_flag,
                       __kmp_registration_flag, KMP_LIBRARY_FILE);

  KA_TRACE(50, ("__kmp_register_library_startup: %s=\"%s\"\n", name,
                __kmp_registration_str));

  while (!done) {

    char *value = NULL; // Actual value of the environment variable.

    // Set environment variable, but do not overwrite if it is exist.
    __kmp_env_set(name, __kmp_registration_str, 0);
    // Check the variable is written.
    value = __kmp_env_get(name);
    if (value != NULL && strcmp(value, __kmp_registration_str) == 0) {

      done = 1; // Ok, environment variable set successfully, exit the loop.

    } else {

      // Oops. Write failed. Another copy of OpenMP RTL is in memory.
      // Check whether it alive or dead.
      int neighbor = 0; // 0 -- unknown status, 1 -- alive, 2 -- dead.
      char *tail = value;
      char *flag_addr_str = NULL;
      char *flag_val_str = NULL;
      char const *file_name = NULL;
      __kmp_str_split(tail, '-', &flag_addr_str, &tail);
      __kmp_str_split(tail, '-', &flag_val_str, &tail);
      file_name = tail;
      if (tail != NULL) {
        long *flag_addr = 0;
        long flag_val = 0;
        KMP_SSCANF(flag_addr_str, "%p", RCAST(void**, &flag_addr));
        KMP_SSCANF(flag_val_str, "%lx", &flag_val);
        if (flag_addr != 0 && flag_val != 0 && strcmp(file_name, "") != 0) {
          // First, check whether environment-encoded address is mapped into
          // addr space.
          // If so, dereference it to see if it still has the right value.
          if (__kmp_is_address_mapped(flag_addr) && *flag_addr == flag_val) {
            neighbor = 1;
          } else {
            // If not, then we know the other copy of the library is no longer
            // running.
            neighbor = 2;
          }
        }
      }
      switch (neighbor) {
      case 0: // Cannot parse environment variable -- neighbor status unknown.
        // Assume it is the incompatible format of future version of the
        // library. Assume the other library is alive.
        // WARN( ... ); // TODO: Issue a warning.
        file_name = "unknown library";
      // Attention! Falling to the next case. That's intentional.
      case 1: { // Neighbor is alive.
        // Check it is allowed.
        char *duplicate_ok = __kmp_env_get("KMP_DUPLICATE_LIB_OK");
        if (!__kmp_str_match_true(duplicate_ok)) {
          // That's not allowed. Issue fatal error.
          __kmp_fatal(KMP_MSG(DuplicateLibrary, KMP_LIBRARY_FILE, file_name),
                      KMP_HNT(DuplicateLibrary), __kmp_msg_null);
        }
        KMP_INTERNAL_FREE(duplicate_ok);
        __kmp_duplicate_library_ok = 1;
        done = 1; // Exit the loop.
      } break;
      case 2: { // Neighbor is dead.
        // Clear the variable and try to register library again.
        __kmp_env_unset(name);
      } break;
      default: { KMP_DEBUG_ASSERT(0); } break;
      }
    }
    KMP_INTERNAL_FREE((void *)value);
  }
  KMP_INTERNAL_FREE((void *)name);

} // func __kmp_register_library_startup

void __kmp_unregister_library(void) {

  char *name = __kmp_reg_status_name();
  char *value = __kmp_env_get(name);

  KMP_DEBUG_ASSERT(__kmp_registration_flag != 0);
  KMP_DEBUG_ASSERT(__kmp_registration_str != NULL);
  if (value != NULL && strcmp(value, __kmp_registration_str) == 0) {
    // Ok, this is our variable. Delete it.
    __kmp_env_unset(name);
  }

  KMP_INTERNAL_FREE(__kmp_registration_str);
  KMP_INTERNAL_FREE(value);
  KMP_INTERNAL_FREE(name);

  __kmp_registration_flag = 0;
  __kmp_registration_str = NULL;

} // __kmp_unregister_library

// End of Library registration stuff.
// -----------------------------------------------------------------------------

#if KMP_MIC_SUPPORTED

static void __kmp_check_mic_type() {
  kmp_cpuid_t cpuid_state = {0};
  kmp_cpuid_t *cs_p = &cpuid_state;
  __kmp_x86_cpuid(1, 0, cs_p);
  // We don't support mic1 at the moment
  if ((cs_p->eax & 0xff0) == 0xB10) {
    __kmp_mic_type = mic2;
  } else if ((cs_p->eax & 0xf0ff0) == 0x50670) {
    __kmp_mic_type = mic3;
  } else {
    __kmp_mic_type = non_mic;
  }
}

#endif /* KMP_MIC_SUPPORTED */

static void __kmp_do_serial_initialize(void) {
  int i, gtid;
  int size;

  KA_TRACE(10, ("__kmp_do_serial_initialize: enter\n"));

  KMP_DEBUG_ASSERT(sizeof(kmp_int32) == 4);
  KMP_DEBUG_ASSERT(sizeof(kmp_uint32) == 4);
  KMP_DEBUG_ASSERT(sizeof(kmp_int64) == 8);
  KMP_DEBUG_ASSERT(sizeof(kmp_uint64) == 8);
  KMP_DEBUG_ASSERT(sizeof(kmp_intptr_t) == sizeof(void *));

#if OMPT_SUPPORT
  ompt_pre_init();
#endif

  __kmp_validate_locks();

  /* Initialize internal memory allocator */
  __kmp_init_allocator();

  /* Register the library startup via an environment variable and check to see
     whether another copy of the library is already registered. */

  __kmp_register_library_startup();

  /* TODO reinitialization of library */
  if (TCR_4(__kmp_global.g.g_done)) {
    KA_TRACE(10, ("__kmp_do_serial_initialize: reinitialization of library\n"));
  }

  __kmp_global.g.g_abort = 0;
  TCW_SYNC_4(__kmp_global.g.g_done, FALSE);

/* initialize the locks */
#if KMP_USE_ADAPTIVE_LOCKS
#if KMP_DEBUG_ADAPTIVE_LOCKS
  __kmp_init_speculative_stats();
#endif
#endif
#if KMP_STATS_ENABLED
  __kmp_stats_init();
#endif
  __kmp_init_lock(&__kmp_global_lock);
  __kmp_init_queuing_lock(&__kmp_dispatch_lock);
  __kmp_init_lock(&__kmp_debug_lock);
  __kmp_init_atomic_lock(&__kmp_atomic_lock);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_1i);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_2i);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_4i);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_4r);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_8i);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_8r);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_8c);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_10r);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_16r);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_16c);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_20c);
  __kmp_init_atomic_lock(&__kmp_atomic_lock_32c);
  __kmp_init_bootstrap_lock(&__kmp_forkjoin_lock);
  __kmp_init_bootstrap_lock(&__kmp_exit_lock);
#if KMP_USE_MONITOR
  __kmp_init_bootstrap_lock(&__kmp_monitor_lock);
#endif
  __kmp_init_bootstrap_lock(&__kmp_tp_cached_lock);

  /* conduct initialization and initial setup of configuration */

  __kmp_runtime_initialize();

#if KMP_MIC_SUPPORTED
  __kmp_check_mic_type();
#endif

// Some global variable initialization moved here from kmp_env_initialize()
#ifdef KMP_DEBUG
  kmp_diag = 0;
#endif
  __kmp_abort_delay = 0;

  // From __kmp_init_dflt_team_nth()
  /* assume the entire machine will be used */
  __kmp_dflt_team_nth_ub = __kmp_xproc;
  if (__kmp_dflt_team_nth_ub < KMP_MIN_NTH) {
    __kmp_dflt_team_nth_ub = KMP_MIN_NTH;
  }
  if (__kmp_dflt_team_nth_ub > __kmp_sys_max_nth) {
    __kmp_dflt_team_nth_ub = __kmp_sys_max_nth;
  }
  __kmp_max_nth = __kmp_sys_max_nth;
  __kmp_cg_max_nth = __kmp_sys_max_nth;
  __kmp_teams_max_nth = __kmp_xproc; // set a "reasonable" default
  if (__kmp_teams_max_nth > __kmp_sys_max_nth) {
    __kmp_teams_max_nth = __kmp_sys_max_nth;
  }

  // Three vars below moved here from __kmp_env_initialize() "KMP_BLOCKTIME"
  // part
  __kmp_dflt_blocktime = KMP_DEFAULT_BLOCKTIME;
#if KMP_USE_MONITOR
  __kmp_monitor_wakeups =
      KMP_WAKEUPS_FROM_BLOCKTIME(__kmp_dflt_blocktime, __kmp_monitor_wakeups);
  __kmp_bt_intervals =
      KMP_INTERVALS_FROM_BLOCKTIME(__kmp_dflt_blocktime, __kmp_monitor_wakeups);
#endif
  // From "KMP_LIBRARY" part of __kmp_env_initialize()
  __kmp_library = library_throughput;
  // From KMP_SCHEDULE initialization
  __kmp_static = kmp_sch_static_balanced;
// AC: do not use analytical here, because it is non-monotonous
//__kmp_guided = kmp_sch_guided_iterative_chunked;
//__kmp_auto = kmp_sch_guided_analytical_chunked; // AC: it is the default, no
// need to repeat assignment
// Barrier initialization. Moved here from __kmp_env_initialize() Barrier branch
// bit control and barrier method control parts
#if KMP_FAST_REDUCTION_BARRIER
#define kmp_reduction_barrier_gather_bb ((int)1)
#define kmp_reduction_barrier_release_bb ((int)1)
#define kmp_reduction_barrier_gather_pat bp_hyper_bar
#define kmp_reduction_barrier_release_pat bp_hyper_bar
#endif // KMP_FAST_REDUCTION_BARRIER
  for (i = bs_plain_barrier; i < bs_last_barrier; i++) {
    __kmp_barrier_gather_branch_bits[i] = __kmp_barrier_gather_bb_dflt;
    __kmp_barrier_release_branch_bits[i] = __kmp_barrier_release_bb_dflt;
    __kmp_barrier_gather_pattern[i] = __kmp_barrier_gather_pat_dflt;
    __kmp_barrier_release_pattern[i] = __kmp_barrier_release_pat_dflt;
#if KMP_FAST_REDUCTION_BARRIER
    if (i == bs_reduction_barrier) { // tested and confirmed on ALTIX only (
      // lin_64 ): hyper,1
      __kmp_barrier_gather_branch_bits[i] = kmp_reduction_barrier_gather_bb;
      __kmp_barrier_release_branch_bits[i] = kmp_reduction_barrier_release_bb;
      __kmp_barrier_gather_pattern[i] = kmp_reduction_barrier_gather_pat;
      __kmp_barrier_release_pattern[i] = kmp_reduction_barrier_release_pat;
    }
#endif // KMP_FAST_REDUCTION_BARRIER
  }
#if KMP_FAST_REDUCTION_BARRIER
#undef kmp_reduction_barrier_release_pat
#undef kmp_reduction_barrier_gather_pat
#undef kmp_reduction_barrier_release_bb
#undef kmp_reduction_barrier_gather_bb
#endif // KMP_FAST_REDUCTION_BARRIER
#if KMP_MIC_SUPPORTED
  if (__kmp_mic_type == mic2) { // KNC
    // AC: plane=3,2, forkjoin=2,1 are optimal for 240 threads on KNC
    __kmp_barrier_gather_branch_bits[bs_plain_barrier] = 3; // plain gather
    __kmp_barrier_release_branch_bits[bs_forkjoin_barrier] =
        1; // forkjoin release
    __kmp_barrier_gather_pattern[bs_forkjoin_barrier] = bp_hierarchical_bar;
    __kmp_barrier_release_pattern[bs_forkjoin_barrier] = bp_hierarchical_bar;
  }
#if KMP_FAST_REDUCTION_BARRIER
  if (__kmp_mic_type == mic2) { // KNC
    __kmp_barrier_gather_pattern[bs_reduction_barrier] = bp_hierarchical_bar;
    __kmp_barrier_release_pattern[bs_reduction_barrier] = bp_hierarchical_bar;
  }
#endif // KMP_FAST_REDUCTION_BARRIER
#endif // KMP_MIC_SUPPORTED

// From KMP_CHECKS initialization
#ifdef KMP_DEBUG
  __kmp_env_checks = TRUE; /* development versions have the extra checks */
#else
  __kmp_env_checks = FALSE; /* port versions do not have the extra checks */
#endif

  // From "KMP_FOREIGN_THREADS_THREADPRIVATE" initialization
  __kmp_foreign_tp = TRUE;

  __kmp_global.g.g_dynamic = FALSE;
  __kmp_global.g.g_dynamic_mode = dynamic_default;

  __kmp_env_initialize(NULL);

// Print all messages in message catalog for testing purposes.
#ifdef KMP_DEBUG
  char const *val = __kmp_env_get("KMP_DUMP_CATALOG");
  if (__kmp_str_match_true(val)) {
    kmp_str_buf_t buffer;
    __kmp_str_buf_init(&buffer);
    __kmp_i18n_dump_catalog(&buffer);
    __kmp_printf("%s", buffer.str);
    __kmp_str_buf_free(&buffer);
  }
  __kmp_env_free(&val);
#endif

  __kmp_threads_capacity =
      __kmp_initial_threads_capacity(__kmp_dflt_team_nth_ub);
  // Moved here from __kmp_env_initialize() "KMP_ALL_THREADPRIVATE" part
  __kmp_tp_capacity = __kmp_default_tp_capacity(
      __kmp_dflt_team_nth_ub, __kmp_max_nth, __kmp_allThreadsSpecified);

  // If the library is shut down properly, both pools must be NULL. Just in
  // case, set them to NULL -- some memory may leak, but subsequent code will
  // work even if pools are not freed.
  KMP_DEBUG_ASSERT(__kmp_thread_pool == NULL);
  KMP_DEBUG_ASSERT(__kmp_thread_pool_insert_pt == NULL);
  KMP_DEBUG_ASSERT(__kmp_team_pool == NULL);
  __kmp_thread_pool = NULL;
  __kmp_thread_pool_insert_pt = NULL;
  __kmp_team_pool = NULL;

  /* Allocate all of the variable sized records */
  /* NOTE: __kmp_threads_capacity entries are allocated, but the arrays are
   * expandable */
  /* Since allocation is cache-aligned, just add extra padding at the end */
  size =
      (sizeof(kmp_info_t *) + sizeof(kmp_root_t *)) * __kmp_threads_capacity +
      CACHE_LINE;
  __kmp_threads = (kmp_info_t **)__kmp_allocate(size);
  __kmp_root = (kmp_root_t **)((char *)__kmp_threads +
                               sizeof(kmp_info_t *) * __kmp_threads_capacity);

  /* init thread counts */
  KMP_DEBUG_ASSERT(__kmp_all_nth ==
                   0); // Asserts fail if the library is reinitializing and
  KMP_DEBUG_ASSERT(__kmp_nth == 0); // something was wrong in termination.
  __kmp_all_nth = 0;
  __kmp_nth = 0;

  /* setup the uber master thread and hierarchy */
  gtid = __kmp_register_root(TRUE);
  KA_TRACE(10, ("__kmp_do_serial_initialize  T#%d\n", gtid));
  KMP_ASSERT(KMP_UBER_GTID(gtid));
  KMP_ASSERT(KMP_INITIAL_GTID(gtid));

  KMP_MB(); /* Flush all pending memory write invalidates.  */

  __kmp_common_initialize();

#if KMP_OS_UNIX
  /* invoke the child fork handler */
  __kmp_register_atfork();
#endif

#if !KMP_DYNAMIC_LIB
  {
    /* Invoke the exit handler when the program finishes, only for static
       library. For dynamic library, we already have _fini and DllMain. */
    int rc = atexit(__kmp_internal_end_atexit);
    if (rc != 0) {
      __kmp_fatal(KMP_MSG(FunctionError, "atexit()"), KMP_ERR(rc),
                  __kmp_msg_null);
    }
  }
#endif

#if KMP_HANDLE_SIGNALS
#if KMP_OS_UNIX
  /* NOTE: make sure that this is called before the user installs their own
     signal handlers so that the user handlers are called first. this way they
     can return false, not call our handler, avoid terminating the library, and
     continue execution where they left off. */
  __kmp_install_signals(FALSE);
#endif /* KMP_OS_UNIX */
#if KMP_OS_WINDOWS
  __kmp_install_signals(TRUE);
#endif /* KMP_OS_WINDOWS */
#endif

  /* we have finished the serial initialization */
  __kmp_init_counter++;

  __kmp_init_serial = TRUE;

  if (__kmp_settings) {
    __kmp_env_print();
  }

#if OMP_40_ENABLED
  if (__kmp_display_env || __kmp_display_env_verbose) {
    __kmp_env_print_2();
  }
#endif // OMP_40_ENABLED

#if OMPT_SUPPORT
  ompt_post_init();
#endif

  KMP_MB();

  KA_TRACE(10, ("__kmp_do_serial_initialize: exit\n"));
}

void __kmp_serial_initialize(void) {
  if (__kmp_init_serial) {
    return;
  }
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);
  if (__kmp_init_serial) {
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }
  __kmp_do_serial_initialize();
  __kmp_release_bootstrap_lock(&__kmp_initz_lock);
}

static void __kmp_do_middle_initialize(void) {
  int i, j;
  int prev_dflt_team_nth;

  if (!__kmp_init_serial) {
    __kmp_do_serial_initialize();
  }

  KA_TRACE(10, ("__kmp_middle_initialize: enter\n"));

  // Save the previous value for the __kmp_dflt_team_nth so that
  // we can avoid some reinitialization if it hasn't changed.
  prev_dflt_team_nth = __kmp_dflt_team_nth;

#if KMP_AFFINITY_SUPPORTED
  // __kmp_affinity_initialize() will try to set __kmp_ncores to the
  // number of cores on the machine.
  __kmp_affinity_initialize();

  // Run through the __kmp_threads array and set the affinity mask
  // for each root thread that is currently registered with the RTL.
  for (i = 0; i < __kmp_threads_capacity; i++) {
    if (TCR_PTR(__kmp_threads[i]) != NULL) {
      __kmp_affinity_set_init_mask(i, TRUE);
    }
  }
#endif /* KMP_AFFINITY_SUPPORTED */

  KMP_ASSERT(__kmp_xproc > 0);
  if (__kmp_avail_proc == 0) {
    __kmp_avail_proc = __kmp_xproc;
  }

  // If there were empty places in num_threads list (OMP_NUM_THREADS=,,2,3),
  // correct them now
  j = 0;
  while ((j < __kmp_nested_nth.used) && !__kmp_nested_nth.nth[j]) {
    __kmp_nested_nth.nth[j] = __kmp_dflt_team_nth = __kmp_dflt_team_nth_ub =
        __kmp_avail_proc;
    j++;
  }

  if (__kmp_dflt_team_nth == 0) {
#ifdef KMP_DFLT_NTH_CORES
    // Default #threads = #cores
    __kmp_dflt_team_nth = __kmp_ncores;
    KA_TRACE(20, ("__kmp_middle_initialize: setting __kmp_dflt_team_nth = "
                  "__kmp_ncores (%d)\n",
                  __kmp_dflt_team_nth));
#else
    // Default #threads = #available OS procs
    __kmp_dflt_team_nth = __kmp_avail_proc;
    KA_TRACE(20, ("__kmp_middle_initialize: setting __kmp_dflt_team_nth = "
                  "__kmp_avail_proc(%d)\n",
                  __kmp_dflt_team_nth));
#endif /* KMP_DFLT_NTH_CORES */
  }

  if (__kmp_dflt_team_nth < KMP_MIN_NTH) {
    __kmp_dflt_team_nth = KMP_MIN_NTH;
  }
  if (__kmp_dflt_team_nth > __kmp_sys_max_nth) {
    __kmp_dflt_team_nth = __kmp_sys_max_nth;
  }

  // There's no harm in continuing if the following check fails,
  // but it indicates an error in the previous logic.
  KMP_DEBUG_ASSERT(__kmp_dflt_team_nth <= __kmp_dflt_team_nth_ub);

  if (__kmp_dflt_team_nth != prev_dflt_team_nth) {
    // Run through the __kmp_threads array and set the num threads icv for each
    // root thread that is currently registered with the RTL (which has not
    // already explicitly set its nthreads-var with a call to
    // omp_set_num_threads()).
    for (i = 0; i < __kmp_threads_capacity; i++) {
      kmp_info_t *thread = __kmp_threads[i];
      if (thread == NULL)
        continue;
      if (thread->th.th_current_task->td_icvs.nproc != 0)
        continue;

      set__nproc(__kmp_threads[i], __kmp_dflt_team_nth);
    }
  }
  KA_TRACE(
      20,
      ("__kmp_middle_initialize: final value for __kmp_dflt_team_nth = %d\n",
       __kmp_dflt_team_nth));

#ifdef KMP_ADJUST_BLOCKTIME
  /* Adjust blocktime to zero if necessary  now that __kmp_avail_proc is set */
  if (!__kmp_env_blocktime && (__kmp_avail_proc > 0)) {
    KMP_DEBUG_ASSERT(__kmp_avail_proc > 0);
    if (__kmp_nth > __kmp_avail_proc) {
      __kmp_zero_bt = TRUE;
    }
  }
#endif /* KMP_ADJUST_BLOCKTIME */

  /* we have finished middle initialization */
  TCW_SYNC_4(__kmp_init_middle, TRUE);

  KA_TRACE(10, ("__kmp_do_middle_initialize: exit\n"));
}

void __kmp_middle_initialize(void) {
  if (__kmp_init_middle) {
    return;
  }
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);
  if (__kmp_init_middle) {
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }
  __kmp_do_middle_initialize();
  __kmp_release_bootstrap_lock(&__kmp_initz_lock);
}

void __kmp_parallel_initialize(void) {
  int gtid = __kmp_entry_gtid(); // this might be a new root

  /* synchronize parallel initialization (for sibling) */
  if (TCR_4(__kmp_init_parallel))
    return;
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);
  if (TCR_4(__kmp_init_parallel)) {
    __kmp_release_bootstrap_lock(&__kmp_initz_lock);
    return;
  }

  /* TODO reinitialization after we have already shut down */
  if (TCR_4(__kmp_global.g.g_done)) {
    KA_TRACE(
        10,
        ("__kmp_parallel_initialize: attempt to init while shutting down\n"));
    __kmp_infinite_loop();
  }

  /* jc: The lock __kmp_initz_lock is already held, so calling
     __kmp_serial_initialize would cause a deadlock.  So we call
     __kmp_do_serial_initialize directly. */
  if (!__kmp_init_middle) {
    __kmp_do_middle_initialize();
  }

  /* begin initialization */
  KA_TRACE(10, ("__kmp_parallel_initialize: enter\n"));
  KMP_ASSERT(KMP_UBER_GTID(gtid));

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
  // Save the FP control regs.
  // Worker threads will set theirs to these values at thread startup.
  __kmp_store_x87_fpu_control_word(&__kmp_init_x87_fpu_control_word);
  __kmp_store_mxcsr(&__kmp_init_mxcsr);
  __kmp_init_mxcsr &= KMP_X86_MXCSR_MASK;
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

#if KMP_OS_UNIX
#if KMP_HANDLE_SIGNALS
  /*  must be after __kmp_serial_initialize  */
  __kmp_install_signals(TRUE);
#endif
#endif

  __kmp_suspend_initialize();

#if defined(USE_LOAD_BALANCE)
  if (__kmp_global.g.g_dynamic_mode == dynamic_default) {
    __kmp_global.g.g_dynamic_mode = dynamic_load_balance;
  }
#else
  if (__kmp_global.g.g_dynamic_mode == dynamic_default) {
    __kmp_global.g.g_dynamic_mode = dynamic_thread_limit;
  }
#endif

  if (__kmp_version) {
    __kmp_print_version_2();
  }

  /* we have finished parallel initialization */
  TCW_SYNC_4(__kmp_init_parallel, TRUE);

  KMP_MB();
  KA_TRACE(10, ("__kmp_parallel_initialize: exit\n"));

  __kmp_release_bootstrap_lock(&__kmp_initz_lock);
}

/* ------------------------------------------------------------------------ */

void __kmp_run_before_invoked_task(int gtid, int tid, kmp_info_t *this_thr,
                                   kmp_team_t *team) {
  kmp_disp_t *dispatch;

  KMP_MB();

  /* none of the threads have encountered any constructs, yet. */
  this_thr->th.th_local.this_construct = 0;
#if KMP_CACHE_MANAGE
  KMP_CACHE_PREFETCH(&this_thr->th.th_bar[bs_forkjoin_barrier].bb.b_arrived);
#endif /* KMP_CACHE_MANAGE */
  dispatch = (kmp_disp_t *)TCR_PTR(this_thr->th.th_dispatch);
  KMP_DEBUG_ASSERT(dispatch);
  KMP_DEBUG_ASSERT(team->t.t_dispatch);
  // KMP_DEBUG_ASSERT( this_thr->th.th_dispatch == &team->t.t_dispatch[
  // this_thr->th.th_info.ds.ds_tid ] );

  dispatch->th_disp_index = 0; /* reset the dispatch buffer counter */
#if OMP_45_ENABLED
  dispatch->th_doacross_buf_idx =
      0; /* reset the doacross dispatch buffer counter */
#endif
  if (__kmp_env_consistency_check)
    __kmp_push_parallel(gtid, team->t.t_ident);

  KMP_MB(); /* Flush all pending memory write invalidates.  */
}

void __kmp_run_after_invoked_task(int gtid, int tid, kmp_info_t *this_thr,
                                  kmp_team_t *team) {
  if (__kmp_env_consistency_check)
    __kmp_pop_parallel(gtid, team->t.t_ident);

  __kmp_finish_implicit_task(this_thr);
}

int __kmp_invoke_task_func(int gtid) {
  int rc;
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = this_thr->th.th_team;

  __kmp_run_before_invoked_task(gtid, tid, this_thr, team);
#if USE_ITT_BUILD
  if (__itt_stack_caller_create_ptr) {
    __kmp_itt_stack_callee_enter(
        (__itt_caller)
            team->t.t_stack_id); // inform ittnotify about entering user's code
  }
#endif /* USE_ITT_BUILD */
#if INCLUDE_SSC_MARKS
  SSC_MARK_INVOKING();
#endif

#if OMPT_SUPPORT
  void *dummy;
  void **exit_runtime_p;
  ompt_data_t *my_task_data;
  ompt_data_t *my_parallel_data;
  int ompt_team_size;

  if (ompt_enabled.enabled) {
    exit_runtime_p = &(
        team->t.t_implicit_task_taskdata[tid].ompt_task_info.frame.exit_frame.ptr);
  } else {
    exit_runtime_p = &dummy;
  }

  my_task_data =
      &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data);
  my_parallel_data = &(team->t.ompt_team_info.parallel_data);
  if (ompt_enabled.ompt_callback_implicit_task) {
    ompt_team_size = team->t.t_nproc;
    ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
        ompt_scope_begin, my_parallel_data, my_task_data, ompt_team_size,
        __kmp_tid_from_gtid(gtid), ompt_task_implicit); // TODO: Can this be ompt_task_initial?
    OMPT_CUR_TASK_INFO(this_thr)->thread_num = __kmp_tid_from_gtid(gtid);
  }
#endif

  {
    KMP_TIME_PARTITIONED_BLOCK(OMP_parallel);
    KMP_SET_THREAD_STATE_BLOCK(IMPLICIT_TASK);
    rc =
        __kmp_invoke_microtask((microtask_t)TCR_SYNC_PTR(team->t.t_pkfn), gtid,
                               tid, (int)team->t.t_argc, (void **)team->t.t_argv
#if OMPT_SUPPORT
                               ,
                               exit_runtime_p
#endif
                               );
#if OMPT_SUPPORT
    *exit_runtime_p = NULL;
#endif
  }

#if USE_ITT_BUILD
  if (__itt_stack_caller_create_ptr) {
    __kmp_itt_stack_callee_leave(
        (__itt_caller)
            team->t.t_stack_id); // inform ittnotify about leaving user's code
  }
#endif /* USE_ITT_BUILD */
  __kmp_run_after_invoked_task(gtid, tid, this_thr, team);

  return rc;
}

#if OMP_40_ENABLED
void __kmp_teams_master(int gtid) {
  // This routine is called by all master threads in teams construct
  kmp_info_t *thr = __kmp_threads[gtid];
  kmp_team_t *team = thr->th.th_team;
  ident_t *loc = team->t.t_ident;
  thr->th.th_set_nproc = thr->th.th_teams_size.nth;
  KMP_DEBUG_ASSERT(thr->th.th_teams_microtask);
  KMP_DEBUG_ASSERT(thr->th.th_set_nproc);
  KA_TRACE(20, ("__kmp_teams_master: T#%d, Tid %d, microtask %p\n", gtid,
                __kmp_tid_from_gtid(gtid), thr->th.th_teams_microtask));
// Launch league of teams now, but not let workers execute
// (they hang on fork barrier until next parallel)
#if INCLUDE_SSC_MARKS
  SSC_MARK_FORKING();
#endif
  __kmp_fork_call(loc, gtid, fork_context_intel, team->t.t_argc,
                  (microtask_t)thr->th.th_teams_microtask, // "wrapped" task
                  VOLATILE_CAST(launch_t) __kmp_invoke_task_func, NULL);
#if INCLUDE_SSC_MARKS
  SSC_MARK_JOINING();
#endif

  // AC: last parameter "1" eliminates join barrier which won't work because
  // worker threads are in a fork barrier waiting for more parallel regions
  __kmp_join_call(loc, gtid
#if OMPT_SUPPORT
                  ,
                  fork_context_intel
#endif
                  ,
                  1);
}

int __kmp_invoke_teams_master(int gtid) {
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = this_thr->th.th_team;
#if KMP_DEBUG
  if (!__kmp_threads[gtid]->th.th_team->t.t_serialized)
    KMP_DEBUG_ASSERT((void *)__kmp_threads[gtid]->th.th_team->t.t_pkfn ==
                     (void *)__kmp_teams_master);
#endif
  __kmp_run_before_invoked_task(gtid, 0, this_thr, team);
  __kmp_teams_master(gtid);
  __kmp_run_after_invoked_task(gtid, 0, this_thr, team);
  return 1;
}
#endif /* OMP_40_ENABLED */

/* this sets the requested number of threads for the next parallel region
   encountered by this team. since this should be enclosed in the forkjoin
   critical section it should avoid race conditions with assymmetrical nested
   parallelism */

void __kmp_push_num_threads(ident_t *id, int gtid, int num_threads) {
  kmp_info_t *thr = __kmp_threads[gtid];

  if (num_threads > 0)
    thr->th.th_set_nproc = num_threads;
}

#if OMP_40_ENABLED

/* this sets the requested number of teams for the teams region and/or
   the number of threads for the next parallel region encountered  */
void __kmp_push_num_teams(ident_t *id, int gtid, int num_teams,
                          int num_threads) {
  kmp_info_t *thr = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(num_teams >= 0);
  KMP_DEBUG_ASSERT(num_threads >= 0);

  if (num_teams == 0)
    num_teams = 1; // default number of teams is 1.
  if (num_teams > __kmp_teams_max_nth) { // if too many teams requested?
    if (!__kmp_reserve_warn) {
      __kmp_reserve_warn = 1;
      __kmp_msg(kmp_ms_warning,
                KMP_MSG(CantFormThrTeam, num_teams, __kmp_teams_max_nth),
                KMP_HNT(Unset_ALL_THREADS), __kmp_msg_null);
    }
    num_teams = __kmp_teams_max_nth;
  }
  // Set number of teams (number of threads in the outer "parallel" of the
  // teams)
  thr->th.th_set_nproc = thr->th.th_teams_size.nteams = num_teams;

  // Remember the number of threads for inner parallel regions
  if (num_threads == 0) {
    if (!TCR_4(__kmp_init_middle))
      __kmp_middle_initialize(); // get __kmp_avail_proc calculated
    num_threads = __kmp_avail_proc / num_teams;
    if (num_teams * num_threads > __kmp_teams_max_nth) {
      // adjust num_threads w/o warning as it is not user setting
      num_threads = __kmp_teams_max_nth / num_teams;
    }
  } else {
    if (num_teams * num_threads > __kmp_teams_max_nth) {
      int new_threads = __kmp_teams_max_nth / num_teams;
      if (!__kmp_reserve_warn) { // user asked for too many threads
        __kmp_reserve_warn = 1; // that conflicts with KMP_TEAMS_THREAD_LIMIT
        __kmp_msg(kmp_ms_warning,
                  KMP_MSG(CantFormThrTeam, num_threads, new_threads),
                  KMP_HNT(Unset_ALL_THREADS), __kmp_msg_null);
      }
      num_threads = new_threads;
    }
  }
  thr->th.th_teams_size.nth = num_threads;
}

// Set the proc_bind var to use in the following parallel region.
void __kmp_push_proc_bind(ident_t *id, int gtid, kmp_proc_bind_t proc_bind) {
  kmp_info_t *thr = __kmp_threads[gtid];
  thr->th.th_set_proc_bind = proc_bind;
}

#endif /* OMP_40_ENABLED */

/* Launch the worker threads into the microtask. */

void __kmp_internal_fork(ident_t *id, int gtid, kmp_team_t *team) {
  kmp_info_t *this_thr = __kmp_threads[gtid];

#ifdef KMP_DEBUG
  int f;
#endif /* KMP_DEBUG */

  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(this_thr->th.th_team == team);
  KMP_ASSERT(KMP_MASTER_GTID(gtid));
  KMP_MB(); /* Flush all pending memory write invalidates.  */

  team->t.t_construct = 0; /* no single directives seen yet */
  team->t.t_ordered.dt.t_value =
      0; /* thread 0 enters the ordered section first */

  /* Reset the identifiers on the dispatch buffer */
  KMP_DEBUG_ASSERT(team->t.t_disp_buffer);
  if (team->t.t_max_nproc > 1) {
    int i;
    for (i = 0; i < __kmp_dispatch_num_buffers; ++i) {
      team->t.t_disp_buffer[i].buffer_index = i;
#if OMP_45_ENABLED
      team->t.t_disp_buffer[i].doacross_buf_idx = i;
#endif
    }
  } else {
    team->t.t_disp_buffer[0].buffer_index = 0;
#if OMP_45_ENABLED
    team->t.t_disp_buffer[0].doacross_buf_idx = 0;
#endif
  }

  KMP_MB(); /* Flush all pending memory write invalidates.  */
  KMP_ASSERT(this_thr->th.th_team == team);

#ifdef KMP_DEBUG
  for (f = 0; f < team->t.t_nproc; f++) {
    KMP_DEBUG_ASSERT(team->t.t_threads[f] &&
                     team->t.t_threads[f]->th.th_team_nproc == team->t.t_nproc);
  }
#endif /* KMP_DEBUG */

  /* release the worker threads so they may begin working */
  __kmp_fork_barrier(gtid, 0);
}

void __kmp_internal_join(ident_t *id, int gtid, kmp_team_t *team) {
  kmp_info_t *this_thr = __kmp_threads[gtid];

  KMP_DEBUG_ASSERT(team);
  KMP_DEBUG_ASSERT(this_thr->th.th_team == team);
  KMP_ASSERT(KMP_MASTER_GTID(gtid));
  KMP_MB(); /* Flush all pending memory write invalidates.  */

/* Join barrier after fork */

#ifdef KMP_DEBUG
  if (__kmp_threads[gtid] &&
      __kmp_threads[gtid]->th.th_team_nproc != team->t.t_nproc) {
    __kmp_printf("GTID: %d, __kmp_threads[%d]=%p\n", gtid, gtid,
                 __kmp_threads[gtid]);
    __kmp_printf("__kmp_threads[%d]->th.th_team_nproc=%d, TEAM: %p, "
                 "team->t.t_nproc=%d\n",
                 gtid, __kmp_threads[gtid]->th.th_team_nproc, team,
                 team->t.t_nproc);
    __kmp_print_structure();
  }
  KMP_DEBUG_ASSERT(__kmp_threads[gtid] &&
                   __kmp_threads[gtid]->th.th_team_nproc == team->t.t_nproc);
#endif /* KMP_DEBUG */

  __kmp_join_barrier(gtid); /* wait for everyone */
#if OMPT_SUPPORT
  if (ompt_enabled.enabled &&
      this_thr->th.ompt_thread_info.state == ompt_state_wait_barrier_implicit) {
    int ds_tid = this_thr->th.th_info.ds.ds_tid;
    ompt_data_t *task_data = OMPT_CUR_TASK_DATA(this_thr);
    this_thr->th.ompt_thread_info.state = ompt_state_overhead;
#if OMPT_OPTIONAL
    void *codeptr = NULL;
    if (KMP_MASTER_TID(ds_tid) &&
        (ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait) ||
         ompt_callbacks.ompt_callback(ompt_callback_sync_region)))
      codeptr = OMPT_CUR_TEAM_INFO(this_thr)->master_return_address;

    if (ompt_enabled.ompt_callback_sync_region_wait) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait)(
          ompt_sync_region_barrier, ompt_scope_end, NULL, task_data, codeptr);
    }
    if (ompt_enabled.ompt_callback_sync_region) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region)(
          ompt_sync_region_barrier, ompt_scope_end, NULL, task_data, codeptr);
    }
#endif
    if (!KMP_MASTER_TID(ds_tid) && ompt_enabled.ompt_callback_implicit_task) {
      ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
          ompt_scope_end, NULL, task_data, 0, ds_tid, ompt_task_implicit); // TODO: Can this be ompt_task_initial?
    }
  }
#endif

  KMP_MB(); /* Flush all pending memory write invalidates.  */
  KMP_ASSERT(this_thr->th.th_team == team);
}

/* ------------------------------------------------------------------------ */

#ifdef USE_LOAD_BALANCE

// Return the worker threads actively spinning in the hot team, if we
// are at the outermost level of parallelism.  Otherwise, return 0.
static int __kmp_active_hot_team_nproc(kmp_root_t *root) {
  int i;
  int retval;
  kmp_team_t *hot_team;

  if (root->r.r_active) {
    return 0;
  }
  hot_team = root->r.r_hot_team;
  if (__kmp_dflt_blocktime == KMP_MAX_BLOCKTIME) {
    return hot_team->t.t_nproc - 1; // Don't count master thread
  }

  // Skip the master thread - it is accounted for elsewhere.
  retval = 0;
  for (i = 1; i < hot_team->t.t_nproc; i++) {
    if (hot_team->t.t_threads[i]->th.th_active) {
      retval++;
    }
  }
  return retval;
}

// Perform an automatic adjustment to the number of
// threads used by the next parallel region.
static int __kmp_load_balance_nproc(kmp_root_t *root, int set_nproc) {
  int retval;
  int pool_active;
  int hot_team_active;
  int team_curr_active;
  int system_active;

  KB_TRACE(20, ("__kmp_load_balance_nproc: called root:%p set_nproc:%d\n", root,
                set_nproc));
  KMP_DEBUG_ASSERT(root);
  KMP_DEBUG_ASSERT(root->r.r_root_team->t.t_threads[0]
                       ->th.th_current_task->td_icvs.dynamic == TRUE);
  KMP_DEBUG_ASSERT(set_nproc > 1);

  if (set_nproc == 1) {
    KB_TRACE(20, ("__kmp_load_balance_nproc: serial execution.\n"));
    return 1;
  }

  // Threads that are active in the thread pool, active in the hot team for this
  // particular root (if we are at the outer par level), and the currently
  // executing thread (to become the master) are available to add to the new
  // team, but are currently contributing to the system load, and must be
  // accounted for.
  pool_active = __kmp_thread_pool_active_nth;
  hot_team_active = __kmp_active_hot_team_nproc(root);
  team_curr_active = pool_active + hot_team_active + 1;

  // Check the system load.
  system_active = __kmp_get_load_balance(__kmp_avail_proc + team_curr_active);
  KB_TRACE(30, ("__kmp_load_balance_nproc: system active = %d pool active = %d "
                "hot team active = %d\n",
                system_active, pool_active, hot_team_active));

  if (system_active < 0) {
    // There was an error reading the necessary info from /proc, so use the
    // thread limit algorithm instead. Once we set __kmp_global.g.g_dynamic_mode
    // = dynamic_thread_limit, we shouldn't wind up getting back here.
    __kmp_global.g.g_dynamic_mode = dynamic_thread_limit;
    KMP_WARNING(CantLoadBalUsing, "KMP_DYNAMIC_MODE=thread limit");

    // Make this call behave like the thread limit algorithm.
    retval = __kmp_avail_proc - __kmp_nth +
             (root->r.r_active ? 1 : root->r.r_hot_team->t.t_nproc);
    if (retval > set_nproc) {
      retval = set_nproc;
    }
    if (retval < KMP_MIN_NTH) {
      retval = KMP_MIN_NTH;
    }

    KB_TRACE(20, ("__kmp_load_balance_nproc: thread limit exit. retval:%d\n",
                  retval));
    return retval;
  }

  // There is a slight delay in the load balance algorithm in detecting new
  // running procs. The real system load at this instant should be at least as
  // large as the #active omp thread that are available to add to the team.
  if (system_active < team_curr_active) {
    system_active = team_curr_active;
  }
  retval = __kmp_avail_proc - system_active + team_curr_active;
  if (retval > set_nproc) {
    retval = set_nproc;
  }
  if (retval < KMP_MIN_NTH) {
    retval = KMP_MIN_NTH;
  }

  KB_TRACE(20, ("__kmp_load_balance_nproc: exit. retval:%d\n", retval));
  return retval;
} // __kmp_load_balance_nproc()

#endif /* USE_LOAD_BALANCE */

/* ------------------------------------------------------------------------ */

/* NOTE: this is called with the __kmp_init_lock held */
void __kmp_cleanup(void) {
  int f;

  KA_TRACE(10, ("__kmp_cleanup: enter\n"));

  if (TCR_4(__kmp_init_parallel)) {
#if KMP_HANDLE_SIGNALS
    __kmp_remove_signals();
#endif
    TCW_4(__kmp_init_parallel, FALSE);
  }

  if (TCR_4(__kmp_init_middle)) {
#if KMP_AFFINITY_SUPPORTED
    __kmp_affinity_uninitialize();
#endif /* KMP_AFFINITY_SUPPORTED */
    __kmp_cleanup_hierarchy();
    TCW_4(__kmp_init_middle, FALSE);
  }

  KA_TRACE(10, ("__kmp_cleanup: go serial cleanup\n"));

  if (__kmp_init_serial) {
    __kmp_runtime_destroy();
    __kmp_init_serial = FALSE;
  }

  __kmp_cleanup_threadprivate_caches();

  for (f = 0; f < __kmp_threads_capacity; f++) {
    if (__kmp_root[f] != NULL) {
      __kmp_free(__kmp_root[f]);
      __kmp_root[f] = NULL;
    }
  }
  __kmp_free(__kmp_threads);
  // __kmp_threads and __kmp_root were allocated at once, as single block, so
  // there is no need in freeing __kmp_root.
  __kmp_threads = NULL;
  __kmp_root = NULL;
  __kmp_threads_capacity = 0;

#if KMP_USE_DYNAMIC_LOCK
  __kmp_cleanup_indirect_user_locks();
#else
  __kmp_cleanup_user_locks();
#endif

#if KMP_AFFINITY_SUPPORTED
  KMP_INTERNAL_FREE(CCAST(char *, __kmp_cpuinfo_file));
  __kmp_cpuinfo_file = NULL;
#endif /* KMP_AFFINITY_SUPPORTED */

#if KMP_USE_ADAPTIVE_LOCKS
#if KMP_DEBUG_ADAPTIVE_LOCKS
  __kmp_print_speculative_stats();
#endif
#endif
  KMP_INTERNAL_FREE(__kmp_nested_nth.nth);
  __kmp_nested_nth.nth = NULL;
  __kmp_nested_nth.size = 0;
  __kmp_nested_nth.used = 0;
  KMP_INTERNAL_FREE(__kmp_nested_proc_bind.bind_types);
  __kmp_nested_proc_bind.bind_types = NULL;
  __kmp_nested_proc_bind.size = 0;
  __kmp_nested_proc_bind.used = 0;
#if OMP_50_ENABLED
  if (__kmp_affinity_format) {
    KMP_INTERNAL_FREE(__kmp_affinity_format);
    __kmp_affinity_format = NULL;
  }
#endif

  __kmp_i18n_catclose();

#if KMP_USE_HIER_SCHED
  __kmp_hier_scheds.deallocate();
#endif

#if KMP_STATS_ENABLED
  __kmp_stats_fini();
#endif

  KA_TRACE(10, ("__kmp_cleanup: exit\n"));
}

/* ------------------------------------------------------------------------ */

int __kmp_ignore_mppbeg(void) {
  char *env;

  if ((env = getenv("KMP_IGNORE_MPPBEG")) != NULL) {
    if (__kmp_str_match_false(env))
      return FALSE;
  }
  // By default __kmpc_begin() is no-op.
  return TRUE;
}

int __kmp_ignore_mppend(void) {
  char *env;

  if ((env = getenv("KMP_IGNORE_MPPEND")) != NULL) {
    if (__kmp_str_match_false(env))
      return FALSE;
  }
  // By default __kmpc_end() is no-op.
  return TRUE;
}

void __kmp_internal_begin(void) {
  int gtid;
  kmp_root_t *root;

  /* this is a very important step as it will register new sibling threads
     and assign these new uber threads a new gtid */
  gtid = __kmp_entry_gtid();
  root = __kmp_threads[gtid]->th.th_root;
  KMP_ASSERT(KMP_UBER_GTID(gtid));

  if (root->r.r_begin)
    return;
  __kmp_acquire_lock(&root->r.r_begin_lock, gtid);
  if (root->r.r_begin) {
    __kmp_release_lock(&root->r.r_begin_lock, gtid);
    return;
  }

  root->r.r_begin = TRUE;

  __kmp_release_lock(&root->r.r_begin_lock, gtid);
}

/* ------------------------------------------------------------------------ */

void __kmp_user_set_library(enum library_type arg) {
  int gtid;
  kmp_root_t *root;
  kmp_info_t *thread;

  /* first, make sure we are initialized so we can get our gtid */

  gtid = __kmp_entry_gtid();
  thread = __kmp_threads[gtid];

  root = thread->th.th_root;

  KA_TRACE(20, ("__kmp_user_set_library: enter T#%d, arg: %d, %d\n", gtid, arg,
                library_serial));
  if (root->r.r_in_parallel) { /* Must be called in serial section of top-level
                                  thread */
    KMP_WARNING(SetLibraryIncorrectCall);
    return;
  }

  switch (arg) {
  case library_serial:
    thread->th.th_set_nproc = 0;
    set__nproc(thread, 1);
    break;
  case library_turnaround:
    thread->th.th_set_nproc = 0;
    set__nproc(thread, __kmp_dflt_team_nth ? __kmp_dflt_team_nth
                                           : __kmp_dflt_team_nth_ub);
    break;
  case library_throughput:
    thread->th.th_set_nproc = 0;
    set__nproc(thread, __kmp_dflt_team_nth ? __kmp_dflt_team_nth
                                           : __kmp_dflt_team_nth_ub);
    break;
  default:
    KMP_FATAL(UnknownLibraryType, arg);
  }

  __kmp_aux_set_library(arg);
}

void __kmp_aux_set_stacksize(size_t arg) {
  if (!__kmp_init_serial)
    __kmp_serial_initialize();

#if KMP_OS_DARWIN
  if (arg & (0x1000 - 1)) {
    arg &= ~(0x1000 - 1);
    if (arg + 0x1000) /* check for overflow if we round up */
      arg += 0x1000;
  }
#endif
  __kmp_acquire_bootstrap_lock(&__kmp_initz_lock);

  /* only change the default stacksize before the first parallel region */
  if (!TCR_4(__kmp_init_parallel)) {
    size_t value = arg; /* argument is in bytes */

    if (value < __kmp_sys_min_stksize)
      value = __kmp_sys_min_stksize;
    else if (value > KMP_MAX_STKSIZE)
      value = KMP_MAX_STKSIZE;

    __kmp_stksize = value;

    __kmp_env_stksize = TRUE; /* was KMP_STACKSIZE specified? */
  }

  __kmp_release_bootstrap_lock(&__kmp_initz_lock);
}

/* set the behaviour of the runtime library */
/* TODO this can cause some odd behaviour with sibling parallelism... */
void __kmp_aux_set_library(enum library_type arg) {
  __kmp_library = arg;

  switch (__kmp_library) {
  case library_serial: {
    KMP_INFORM(LibraryIsSerial);
    (void)__kmp_change_library(TRUE);
  } break;
  case library_turnaround:
    (void)__kmp_change_library(TRUE);
    break;
  case library_throughput:
    (void)__kmp_change_library(FALSE);
    break;
  default:
    KMP_FATAL(UnknownLibraryType, arg);
  }
}

/* Getting team information common for all team API */
// Returns NULL if not in teams construct
static kmp_team_t *__kmp_aux_get_team_info(int &teams_serialized) {
  kmp_info_t *thr = __kmp_entry_thread();
  teams_serialized = 0;
  if (thr->th.th_teams_microtask) {
    kmp_team_t *team = thr->th.th_team;
    int tlevel = thr->th.th_teams_level; // the level of the teams construct
    int ii = team->t.t_level;
    teams_serialized = team->t.t_serialized;
    int level = tlevel + 1;
    KMP_DEBUG_ASSERT(ii >= tlevel);
    while (ii > level) {
      for (teams_serialized = team->t.t_serialized;
           (teams_serialized > 0) && (ii > level); teams_serialized--, ii--) {
      }
      if (team->t.t_serialized && (!teams_serialized)) {
        team = team->t.t_parent;
        continue;
      }
      if (ii > level) {
        team = team->t.t_parent;
        ii--;
      }
    }
    return team;
  }
  return NULL;
}

int __kmp_aux_get_team_num() {
  int serialized;
  kmp_team_t *team = __kmp_aux_get_team_info(serialized);
  if (team) {
    if (serialized > 1) {
      return 0; // teams region is serialized ( 1 team of 1 thread ).
    } else {
      return team->t.t_master_tid;
    }
  }
  return 0;
}

int __kmp_aux_get_num_teams() {
  int serialized;
  kmp_team_t *team = __kmp_aux_get_team_info(serialized);
  if (team) {
    if (serialized > 1) {
      return 1;
    } else {
      return team->t.t_parent->t.t_nproc;
    }
  }
  return 1;
}

/* ------------------------------------------------------------------------ */

#if OMP_50_ENABLED
/*
 * Affinity Format Parser
 *
 * Field is in form of: %[[[0].]size]type
 * % and type are required (%% means print a literal '%')
 * type is either single char or long name surrounded by {},
 * e.g., N or {num_threads}
 * 0 => leading zeros
 * . => right justified when size is specified
 * by default output is left justified
 * size is the *minimum* field length
 * All other characters are printed as is
 *
 * Available field types:
 * L {thread_level}      - omp_get_level()
 * n {thread_num}        - omp_get_thread_num()
 * h {host}              - name of host machine
 * P {process_id}        - process id (integer)
 * T {thread_identifier} - native thread identifier (integer)
 * N {num_threads}       - omp_get_num_threads()
 * A {ancestor_tnum}     - omp_get_ancestor_thread_num(omp_get_level()-1)
 * a {thread_affinity}   - comma separated list of integers or integer ranges
 *                         (values of affinity mask)
 *
 * Implementation-specific field types can be added
 * If a type is unknown, print "undefined"
*/

// Structure holding the short name, long name, and corresponding data type
// for snprintf.  A table of these will represent the entire valid keyword
// field types.
typedef struct kmp_affinity_format_field_t {
  char short_name; // from spec e.g., L -> thread level
  const char *long_name; // from spec thread_level -> thread level
  char field_format; // data type for snprintf (typically 'd' or 's'
  // for integer or string)
} kmp_affinity_format_field_t;

static const kmp_affinity_format_field_t __kmp_affinity_format_table[] = {
#if KMP_AFFINITY_SUPPORTED
    {'A', "thread_affinity", 's'},
#endif
    {'t', "team_num", 'd'},
    {'T', "num_teams", 'd'},
    {'L', "nesting_level", 'd'},
    {'n', "thread_num", 'd'},
    {'N', "num_threads", 'd'},
    {'a', "ancestor_tnum", 'd'},
    {'H', "host", 's'},
    {'P', "process_id", 'd'},
    {'i', "native_thread_id", 'd'}};

// Return the number of characters it takes to hold field
static int __kmp_aux_capture_affinity_field(int gtid, const kmp_info_t *th,
                                            const char **ptr,
                                            kmp_str_buf_t *field_buffer) {
  int rc, format_index, field_value;
  const char *width_left, *width_right;
  bool pad_zeros, right_justify, parse_long_name, found_valid_name;
  static const int FORMAT_SIZE = 20;
  char format[FORMAT_SIZE] = {0};
  char absolute_short_name = 0;

  KMP_DEBUG_ASSERT(gtid >= 0);
  KMP_DEBUG_ASSERT(th);
  KMP_DEBUG_ASSERT(**ptr == '%');
  KMP_DEBUG_ASSERT(field_buffer);

  __kmp_str_buf_clear(field_buffer);

  // Skip the initial %
  (*ptr)++;

  // Check for %% first
  if (**ptr == '%') {
    __kmp_str_buf_cat(field_buffer, "%", 1);
    (*ptr)++; // skip over the second %
    return 1;
  }

  // Parse field modifiers if they are present
  pad_zeros = false;
  if (**ptr == '0') {
    pad_zeros = true;
    (*ptr)++; // skip over 0
  }
  right_justify = false;
  if (**ptr == '.') {
    right_justify = true;
    (*ptr)++; // skip over .
  }
  // Parse width of field: [width_left, width_right)
  width_left = width_right = NULL;
  if (**ptr >= '0' && **ptr <= '9') {
    width_left = *ptr;
    SKIP_DIGITS(*ptr);
    width_right = *ptr;
  }

  // Create the format for KMP_SNPRINTF based on flags parsed above
  format_index = 0;
  format[format_index++] = '%';
  if (!right_justify)
    format[format_index++] = '-';
  if (pad_zeros)
    format[format_index++] = '0';
  if (width_left && width_right) {
    int i = 0;
    // Only allow 8 digit number widths.
    // This also prevents overflowing format variable
    while (i < 8 && width_left < width_right) {
      format[format_index++] = *width_left;
      width_left++;
      i++;
    }
  }

  // Parse a name (long or short)
  // Canonicalize the name into absolute_short_name
  found_valid_name = false;
  parse_long_name = (**ptr == '{');
  if (parse_long_name)
    (*ptr)++; // skip initial left brace
  for (size_t i = 0; i < sizeof(__kmp_affinity_format_table) /
                             sizeof(__kmp_affinity_format_table[0]);
       ++i) {
    char short_name = __kmp_affinity_format_table[i].short_name;
    const char *long_name = __kmp_affinity_format_table[i].long_name;
    char field_format = __kmp_affinity_format_table[i].field_format;
    if (parse_long_name) {
      int length = KMP_STRLEN(long_name);
      if (strncmp(*ptr, long_name, length) == 0) {
        found_valid_name = true;
        (*ptr) += length; // skip the long name
      }
    } else if (**ptr == short_name) {
      found_valid_name = true;
      (*ptr)++; // skip the short name
    }
    if (found_valid_name) {
      format[format_index++] = field_format;
      format[format_index++] = '\0';
      absolute_short_name = short_name;
      break;
    }
  }
  if (parse_long_name) {
    if (**ptr != '}') {
      absolute_short_name = 0;
    } else {
      (*ptr)++; // skip over the right brace
    }
  }

  // Attempt to fill the buffer with the requested
  // value using snprintf within __kmp_str_buf_print()
  switch (absolute_short_name) {
  case 't':
    rc = __kmp_str_buf_print(field_buffer, format, __kmp_aux_get_team_num());
    break;
  case 'T':
    rc = __kmp_str_buf_print(field_buffer, format, __kmp_aux_get_num_teams());
    break;
  case 'L':
    rc = __kmp_str_buf_print(field_buffer, format, th->th.th_team->t.t_level);
    break;
  case 'n':
    rc = __kmp_str_buf_print(field_buffer, format, __kmp_tid_from_gtid(gtid));
    break;
  case 'H': {
    static const int BUFFER_SIZE = 256;
    char buf[BUFFER_SIZE];
    __kmp_expand_host_name(buf, BUFFER_SIZE);
    rc = __kmp_str_buf_print(field_buffer, format, buf);
  } break;
  case 'P':
    rc = __kmp_str_buf_print(field_buffer, format, getpid());
    break;
  case 'i':
    rc = __kmp_str_buf_print(field_buffer, format, __kmp_gettid());
    break;
  case 'N':
    rc = __kmp_str_buf_print(field_buffer, format, th->th.th_team->t.t_nproc);
    break;
  case 'a':
    field_value =
        __kmp_get_ancestor_thread_num(gtid, th->th.th_team->t.t_level - 1);
    rc = __kmp_str_buf_print(field_buffer, format, field_value);
    break;
#if KMP_AFFINITY_SUPPORTED
  case 'A': {
    kmp_str_buf_t buf;
    __kmp_str_buf_init(&buf);
    __kmp_affinity_str_buf_mask(&buf, th->th.th_affin_mask);
    rc = __kmp_str_buf_print(field_buffer, format, buf.str);
    __kmp_str_buf_free(&buf);
  } break;
#endif
  default:
    // According to spec, If an implementation does not have info for field
    // type, then "undefined" is printed
    rc = __kmp_str_buf_print(field_buffer, "%s", "undefined");
    // Skip the field
    if (parse_long_name) {
      SKIP_TOKEN(*ptr);
      if (**ptr == '}')
        (*ptr)++;
    } else {
      (*ptr)++;
    }
  }

  KMP_ASSERT(format_index <= FORMAT_SIZE);
  return rc;
}

/*
 * Return number of characters needed to hold the affinity string
 * (not including null byte character)
 * The resultant string is printed to buffer, which the caller can then
 * handle afterwards
*/
size_t __kmp_aux_capture_affinity(int gtid, const char *format,
                                  kmp_str_buf_t *buffer) {
  const char *parse_ptr;
  size_t retval;
  const kmp_info_t *th;
  kmp_str_buf_t field;

  KMP_DEBUG_ASSERT(buffer);
  KMP_DEBUG_ASSERT(gtid >= 0);

  __kmp_str_buf_init(&field);
  __kmp_str_buf_clear(buffer);

  th = __kmp_threads[gtid];
  retval = 0;

  // If format is NULL or zero-length string, then we use
  // affinity-format-var ICV
  parse_ptr = format;
  if (parse_ptr == NULL || *parse_ptr == '\0') {
    parse_ptr = __kmp_affinity_format;
  }
  KMP_DEBUG_ASSERT(parse_ptr);

  while (*parse_ptr != '\0') {
    // Parse a field
    if (*parse_ptr == '%') {
      // Put field in the buffer
      int rc = __kmp_aux_capture_affinity_field(gtid, th, &parse_ptr, &field);
      __kmp_str_buf_catbuf(buffer, &field);
      retval += rc;
    } else {
      // Put literal character in buffer
      __kmp_str_buf_cat(buffer, parse_ptr, 1);
      retval++;
      parse_ptr++;
    }
  }
  __kmp_str_buf_free(&field);
  return retval;
}

// Displays the affinity string to stdout
void __kmp_aux_display_affinity(int gtid, const char *format) {
  kmp_str_buf_t buf;
  __kmp_str_buf_init(&buf);
  __kmp_aux_capture_affinity(gtid, format, &buf);
  __kmp_fprintf(kmp_out, "%s" KMP_END_OF_LINE, buf.str);
  __kmp_str_buf_free(&buf);
}
#endif // OMP_50_ENABLED

/* ------------------------------------------------------------------------ */

void __kmp_aux_set_blocktime(int arg, kmp_info_t *thread, int tid) {
  int blocktime = arg; /* argument is in milliseconds */
#if KMP_USE_MONITOR
  int bt_intervals;
#endif
  int bt_set;

  __kmp_save_internal_controls(thread);

  /* Normalize and set blocktime for the teams */
  if (blocktime < KMP_MIN_BLOCKTIME)
    blocktime = KMP_MIN_BLOCKTIME;
  else if (blocktime > KMP_MAX_BLOCKTIME)
    blocktime = KMP_MAX_BLOCKTIME;

  set__blocktime_team(thread->th.th_team, tid, blocktime);
  set__blocktime_team(thread->th.th_serial_team, 0, blocktime);

#if KMP_USE_MONITOR
  /* Calculate and set blocktime intervals for the teams */
  bt_intervals = KMP_INTERVALS_FROM_BLOCKTIME(blocktime, __kmp_monitor_wakeups);

  set__bt_intervals_team(thread->th.th_team, tid, bt_intervals);
  set__bt_intervals_team(thread->th.th_serial_team, 0, bt_intervals);
#endif

  /* Set whether blocktime has been set to "TRUE" */
  bt_set = TRUE;

  set__bt_set_team(thread->th.th_team, tid, bt_set);
  set__bt_set_team(thread->th.th_serial_team, 0, bt_set);
#if KMP_USE_MONITOR
  KF_TRACE(10, ("kmp_set_blocktime: T#%d(%d:%d), blocktime=%d, "
                "bt_intervals=%d, monitor_updates=%d\n",
                __kmp_gtid_from_tid(tid, thread->th.th_team),
                thread->th.th_team->t.t_id, tid, blocktime, bt_intervals,
                __kmp_monitor_wakeups));
#else
  KF_TRACE(10, ("kmp_set_blocktime: T#%d(%d:%d), blocktime=%d\n",
                __kmp_gtid_from_tid(tid, thread->th.th_team),
                thread->th.th_team->t.t_id, tid, blocktime));
#endif
}

void __kmp_aux_set_defaults(char const *str, int len) {
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  __kmp_env_initialize(str);

  if (__kmp_settings
#if OMP_40_ENABLED
      || __kmp_display_env || __kmp_display_env_verbose
#endif // OMP_40_ENABLED
      ) {
    __kmp_env_print();
  }
} // __kmp_aux_set_defaults

/* ------------------------------------------------------------------------ */
/* internal fast reduction routines */

PACKED_REDUCTION_METHOD_T
__kmp_determine_reduction_method(
    ident_t *loc, kmp_int32 global_tid, kmp_int32 num_vars, size_t reduce_size,
    void *reduce_data, void (*reduce_func)(void *lhs_data, void *rhs_data),
    kmp_critical_name *lck) {

  // Default reduction method: critical construct ( lck != NULL, like in current
  // PAROPT )
  // If ( reduce_data!=NULL && reduce_func!=NULL ): the tree-reduction method
  // can be selected by RTL
  // If loc->flags contains KMP_IDENT_ATOMIC_REDUCE, the atomic reduce method
  // can be selected by RTL
  // Finally, it's up to OpenMP RTL to make a decision on which method to select
  // among generated by PAROPT.

  PACKED_REDUCTION_METHOD_T retval;

  int team_size;

  KMP_DEBUG_ASSERT(loc); // it would be nice to test ( loc != 0 )
  KMP_DEBUG_ASSERT(lck); // it would be nice to test ( lck != 0 )

#define FAST_REDUCTION_ATOMIC_METHOD_GENERATED                                 \
  ((loc->flags & (KMP_IDENT_ATOMIC_REDUCE)) == (KMP_IDENT_ATOMIC_REDUCE))
#define FAST_REDUCTION_TREE_METHOD_GENERATED ((reduce_data) && (reduce_func))

  retval = critical_reduce_block;

  // another choice of getting a team size (with 1 dynamic deference) is slower
  team_size = __kmp_get_team_num_threads(global_tid);
  if (team_size == 1) {

    retval = empty_reduce_block;

  } else {

    int atomic_available = FAST_REDUCTION_ATOMIC_METHOD_GENERATED;

#if KMP_ARCH_X86_64 || KMP_ARCH_PPC64 || KMP_ARCH_AARCH64 || KMP_ARCH_MIPS64

#if KMP_OS_LINUX || KMP_OS_DRAGONFLY || KMP_OS_FREEBSD || KMP_OS_NETBSD ||     \
    KMP_OS_OPENBSD || KMP_OS_WINDOWS || KMP_OS_DARWIN || KMP_OS_HURD

    int teamsize_cutoff = 4;

#if KMP_MIC_SUPPORTED
    if (__kmp_mic_type != non_mic) {
      teamsize_cutoff = 8;
    }
#endif
    int tree_available = FAST_REDUCTION_TREE_METHOD_GENERATED;
    if (tree_available) {
      if (team_size <= teamsize_cutoff) {
        if (atomic_available) {
          retval = atomic_reduce_block;
        }
      } else {
        retval = TREE_REDUCE_BLOCK_WITH_REDUCTION_BARRIER;
      }
    } else if (atomic_available) {
      retval = atomic_reduce_block;
    }
#else
#error "Unknown or unsupported OS"
#endif // KMP_OS_LINUX || KMP_OS_DRAGONFLY || KMP_OS_FREEBSD || KMP_OS_NETBSD ||
       // KMP_OS_OPENBSD || KMP_OS_WINDOWS || KMP_OS_DARWIN || KMP_OS_HURD

#elif KMP_ARCH_X86 || KMP_ARCH_ARM || KMP_ARCH_AARCH || KMP_ARCH_MIPS

#if KMP_OS_LINUX || KMP_OS_FREEBSD || KMP_OS_WINDOWS || KMP_OS_HURD

    // basic tuning

    if (atomic_available) {
      if (num_vars <= 2) { // && ( team_size <= 8 ) due to false-sharing ???
        retval = atomic_reduce_block;
      }
    } // otherwise: use critical section

#elif KMP_OS_DARWIN

    int tree_available = FAST_REDUCTION_TREE_METHOD_GENERATED;
    if (atomic_available && (num_vars <= 3)) {
      retval = atomic_reduce_block;
    } else if (tree_available) {
      if ((reduce_size > (9 * sizeof(kmp_real64))) &&
          (reduce_size < (2000 * sizeof(kmp_real64)))) {
        retval = TREE_REDUCE_BLOCK_WITH_PLAIN_BARRIER;
      }
    } // otherwise: use critical section

#else
#error "Unknown or unsupported OS"
#endif

#else
#error "Unknown or unsupported architecture"
#endif
  }

  // KMP_FORCE_REDUCTION

  // If the team is serialized (team_size == 1), ignore the forced reduction
  // method and stay with the unsynchronized method (empty_reduce_block)
  if (__kmp_force_reduction_method != reduction_method_not_defined &&
      team_size != 1) {

    PACKED_REDUCTION_METHOD_T forced_retval = critical_reduce_block;

    int atomic_available, tree_available;

    switch ((forced_retval = __kmp_force_reduction_method)) {
    case critical_reduce_block:
      KMP_ASSERT(lck); // lck should be != 0
      break;

    case atomic_reduce_block:
      atomic_available = FAST_REDUCTION_ATOMIC_METHOD_GENERATED;
      if (!atomic_available) {
        KMP_WARNING(RedMethodNotSupported, "atomic");
        forced_retval = critical_reduce_block;
      }
      break;

    case tree_reduce_block:
      tree_available = FAST_REDUCTION_TREE_METHOD_GENERATED;
      if (!tree_available) {
        KMP_WARNING(RedMethodNotSupported, "tree");
        forced_retval = critical_reduce_block;
      } else {
#if KMP_FAST_REDUCTION_BARRIER
        forced_retval = TREE_REDUCE_BLOCK_WITH_REDUCTION_BARRIER;
#endif
      }
      break;

    default:
      KMP_ASSERT(0); // "unsupported method specified"
    }

    retval = forced_retval;
  }

  KA_TRACE(10, ("reduction method selected=%08x\n", retval));

#undef FAST_REDUCTION_TREE_METHOD_GENERATED
#undef FAST_REDUCTION_ATOMIC_METHOD_GENERATED

  return (retval);
}

// this function is for testing set/get/determine reduce method
kmp_int32 __kmp_get_reduce_method(void) {
  return ((__kmp_entry_thread()->th.th_local.packed_reduction_method) >> 8);
}
