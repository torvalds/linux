/*
 * kmp_csupport.cpp -- kfront linkage support for OpenMP.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#define __KMP_IMP
#include "omp.h" /* extern "C" declarations of user-visible routines */
#include "kmp.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_itt.h"
#include "kmp_lock.h"
#include "kmp_stats.h"

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#define MAX_MESSAGE 512

// flags will be used in future, e.g. to implement openmp_strict library
// restrictions

/*!
 * @ingroup STARTUP_SHUTDOWN
 * @param loc   in   source location information
 * @param flags in   for future use (currently ignored)
 *
 * Initialize the runtime library. This call is optional; if it is not made then
 * it will be implicitly called by attempts to use other library functions.
 */
void __kmpc_begin(ident_t *loc, kmp_int32 flags) {
  // By default __kmpc_begin() is no-op.
  char *env;
  if ((env = getenv("KMP_INITIAL_THREAD_BIND")) != NULL &&
      __kmp_str_match_true(env)) {
    __kmp_middle_initialize();
    KC_TRACE(10, ("__kmpc_begin: middle initialization called\n"));
  } else if (__kmp_ignore_mppbeg() == FALSE) {
    // By default __kmp_ignore_mppbeg() returns TRUE.
    __kmp_internal_begin();
    KC_TRACE(10, ("__kmpc_begin: called\n"));
  }
}

/*!
 * @ingroup STARTUP_SHUTDOWN
 * @param loc source location information
 *
 * Shutdown the runtime library. This is also optional, and even if called will
 * not do anything unless the `KMP_IGNORE_MPPEND` environment variable is set to
 * zero.
 */
void __kmpc_end(ident_t *loc) {
  // By default, __kmp_ignore_mppend() returns TRUE which makes __kmpc_end()
  // call no-op. However, this can be overridden with KMP_IGNORE_MPPEND
  // environment variable. If KMP_IGNORE_MPPEND is 0, __kmp_ignore_mppend()
  // returns FALSE and __kmpc_end() will unregister this root (it can cause
  // library shut down).
  if (__kmp_ignore_mppend() == FALSE) {
    KC_TRACE(10, ("__kmpc_end: called\n"));
    KA_TRACE(30, ("__kmpc_end\n"));

    __kmp_internal_end_thread(-1);
  }
#if KMP_OS_WINDOWS && OMPT_SUPPORT
  // Normal exit process on Windows does not allow worker threads of the final
  // parallel region to finish reporting their events, so shutting down the
  // library here fixes the issue at least for the cases where __kmpc_end() is
  // placed properly.
  if (ompt_enabled.enabled)
    __kmp_internal_end_library(__kmp_gtid_get_specific());
#endif
}

/*!
@ingroup THREAD_STATES
@param loc Source location information.
@return The global thread index of the active thread.

This function can be called in any context.

If the runtime has ony been entered at the outermost level from a
single (necessarily non-OpenMP<sup>*</sup>) thread, then the thread number is
that which would be returned by omp_get_thread_num() in the outermost
active parallel construct. (Or zero if there is no active parallel
construct, since the master thread is necessarily thread zero).

If multiple non-OpenMP threads all enter an OpenMP construct then this
will be a unique thread identifier among all the threads created by
the OpenMP runtime (but the value cannote be defined in terms of
OpenMP thread ids returned by omp_get_thread_num()).
*/
kmp_int32 __kmpc_global_thread_num(ident_t *loc) {
  kmp_int32 gtid = __kmp_entry_gtid();

  KC_TRACE(10, ("__kmpc_global_thread_num: T#%d\n", gtid));

  return gtid;
}

/*!
@ingroup THREAD_STATES
@param loc Source location information.
@return The number of threads under control of the OpenMP<sup>*</sup> runtime

This function can be called in any context.
It returns the total number of threads under the control of the OpenMP runtime.
That is not a number that can be determined by any OpenMP standard calls, since
the library may be called from more than one non-OpenMP thread, and this
reflects the total over all such calls. Similarly the runtime maintains
underlying threads even when they are not active (since the cost of creating
and destroying OS threads is high), this call counts all such threads even if
they are not waiting for work.
*/
kmp_int32 __kmpc_global_num_threads(ident_t *loc) {
  KC_TRACE(10,
           ("__kmpc_global_num_threads: num_threads = %d\n", __kmp_all_nth));

  return TCR_4(__kmp_all_nth);
}

/*!
@ingroup THREAD_STATES
@param loc Source location information.
@return The thread number of the calling thread in the innermost active parallel
construct.
*/
kmp_int32 __kmpc_bound_thread_num(ident_t *loc) {
  KC_TRACE(10, ("__kmpc_bound_thread_num: called\n"));
  return __kmp_tid_from_gtid(__kmp_entry_gtid());
}

/*!
@ingroup THREAD_STATES
@param loc Source location information.
@return The number of threads in the innermost active parallel construct.
*/
kmp_int32 __kmpc_bound_num_threads(ident_t *loc) {
  KC_TRACE(10, ("__kmpc_bound_num_threads: called\n"));

  return __kmp_entry_thread()->th.th_team->t.t_nproc;
}

/*!
 * @ingroup DEPRECATED
 * @param loc location description
 *
 * This function need not be called. It always returns TRUE.
 */
kmp_int32 __kmpc_ok_to_fork(ident_t *loc) {
#ifndef KMP_DEBUG

  return TRUE;

#else

  const char *semi2;
  const char *semi3;
  int line_no;

  if (__kmp_par_range == 0) {
    return TRUE;
  }
  semi2 = loc->psource;
  if (semi2 == NULL) {
    return TRUE;
  }
  semi2 = strchr(semi2, ';');
  if (semi2 == NULL) {
    return TRUE;
  }
  semi2 = strchr(semi2 + 1, ';');
  if (semi2 == NULL) {
    return TRUE;
  }
  if (__kmp_par_range_filename[0]) {
    const char *name = semi2 - 1;
    while ((name > loc->psource) && (*name != '/') && (*name != ';')) {
      name--;
    }
    if ((*name == '/') || (*name == ';')) {
      name++;
    }
    if (strncmp(__kmp_par_range_filename, name, semi2 - name)) {
      return __kmp_par_range < 0;
    }
  }
  semi3 = strchr(semi2 + 1, ';');
  if (__kmp_par_range_routine[0]) {
    if ((semi3 != NULL) && (semi3 > semi2) &&
        (strncmp(__kmp_par_range_routine, semi2 + 1, semi3 - semi2 - 1))) {
      return __kmp_par_range < 0;
    }
  }
  if (KMP_SSCANF(semi3 + 1, "%d", &line_no) == 1) {
    if ((line_no >= __kmp_par_range_lb) && (line_no <= __kmp_par_range_ub)) {
      return __kmp_par_range > 0;
    }
    return __kmp_par_range < 0;
  }
  return TRUE;

#endif /* KMP_DEBUG */
}

/*!
@ingroup THREAD_STATES
@param loc Source location information.
@return 1 if this thread is executing inside an active parallel region, zero if
not.
*/
kmp_int32 __kmpc_in_parallel(ident_t *loc) {
  return __kmp_entry_thread()->th.th_root->r.r_active;
}

/*!
@ingroup PARALLEL
@param loc source location information
@param global_tid global thread number
@param num_threads number of threads requested for this parallel construct

Set the number of threads to be used by the next fork spawned by this thread.
This call is only required if the parallel construct has a `num_threads` clause.
*/
void __kmpc_push_num_threads(ident_t *loc, kmp_int32 global_tid,
                             kmp_int32 num_threads) {
  KA_TRACE(20, ("__kmpc_push_num_threads: enter T#%d num_threads=%d\n",
                global_tid, num_threads));

  __kmp_push_num_threads(loc, global_tid, num_threads);
}

void __kmpc_pop_num_threads(ident_t *loc, kmp_int32 global_tid) {
  KA_TRACE(20, ("__kmpc_pop_num_threads: enter\n"));

  /* the num_threads are automatically popped */
}

#if OMP_40_ENABLED

void __kmpc_push_proc_bind(ident_t *loc, kmp_int32 global_tid,
                           kmp_int32 proc_bind) {
  KA_TRACE(20, ("__kmpc_push_proc_bind: enter T#%d proc_bind=%d\n", global_tid,
                proc_bind));

  __kmp_push_proc_bind(loc, global_tid, (kmp_proc_bind_t)proc_bind);
}

#endif /* OMP_40_ENABLED */

/*!
@ingroup PARALLEL
@param loc  source location information
@param argc  total number of arguments in the ellipsis
@param microtask  pointer to callback routine consisting of outlined parallel
construct
@param ...  pointers to shared variables that aren't global

Do the actual fork and call the microtask in the relevant number of threads.
*/
void __kmpc_fork_call(ident_t *loc, kmp_int32 argc, kmpc_micro microtask, ...) {
  int gtid = __kmp_entry_gtid();

#if (KMP_STATS_ENABLED)
  // If we were in a serial region, then stop the serial timer, record
  // the event, and start parallel region timer
  stats_state_e previous_state = KMP_GET_THREAD_STATE();
  if (previous_state == stats_state_e::SERIAL_REGION) {
    KMP_EXCHANGE_PARTITIONED_TIMER(OMP_parallel_overhead);
  } else {
    KMP_PUSH_PARTITIONED_TIMER(OMP_parallel_overhead);
  }
  int inParallel = __kmpc_in_parallel(loc);
  if (inParallel) {
    KMP_COUNT_BLOCK(OMP_NESTED_PARALLEL);
  } else {
    KMP_COUNT_BLOCK(OMP_PARALLEL);
  }
#endif

  // maybe to save thr_state is enough here
  {
    va_list ap;
    va_start(ap, microtask);

#if OMPT_SUPPORT
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      kmp_info_t *master_th = __kmp_threads[gtid];
      kmp_team_t *parent_team = master_th->th.th_team;
      ompt_lw_taskteam_t *lwt = parent_team->t.ompt_serialized_team_info;
      if (lwt)
        ompt_frame = &(lwt->ompt_task_info.frame);
      else {
        int tid = __kmp_tid_from_gtid(gtid);
        ompt_frame = &(
            parent_team->t.t_implicit_task_taskdata[tid].ompt_task_info.frame);
      }
      ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(gtid);
    }
#endif

#if INCLUDE_SSC_MARKS
    SSC_MARK_FORKING();
#endif
    __kmp_fork_call(loc, gtid, fork_context_intel, argc,
                    VOLATILE_CAST(microtask_t) microtask, // "wrapped" task
                    VOLATILE_CAST(launch_t) __kmp_invoke_task_func,
/* TODO: revert workaround for Intel(R) 64 tracker #96 */
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
                    &ap
#else
                    ap
#endif
                    );
#if INCLUDE_SSC_MARKS
    SSC_MARK_JOINING();
#endif
    __kmp_join_call(loc, gtid
#if OMPT_SUPPORT
                    ,
                    fork_context_intel
#endif
                    );

    va_end(ap);
  }

#if KMP_STATS_ENABLED
  if (previous_state == stats_state_e::SERIAL_REGION) {
    KMP_EXCHANGE_PARTITIONED_TIMER(OMP_serial);
  } else {
    KMP_POP_PARTITIONED_TIMER();
  }
#endif // KMP_STATS_ENABLED
}

#if OMP_40_ENABLED
/*!
@ingroup PARALLEL
@param loc source location information
@param global_tid global thread number
@param num_teams number of teams requested for the teams construct
@param num_threads number of threads per team requested for the teams construct

Set the number of teams to be used by the teams construct.
This call is only required if the teams construct has a `num_teams` clause
or a `thread_limit` clause (or both).
*/
void __kmpc_push_num_teams(ident_t *loc, kmp_int32 global_tid,
                           kmp_int32 num_teams, kmp_int32 num_threads) {
  KA_TRACE(20,
           ("__kmpc_push_num_teams: enter T#%d num_teams=%d num_threads=%d\n",
            global_tid, num_teams, num_threads));

  __kmp_push_num_teams(loc, global_tid, num_teams, num_threads);
}

/*!
@ingroup PARALLEL
@param loc  source location information
@param argc  total number of arguments in the ellipsis
@param microtask  pointer to callback routine consisting of outlined teams
construct
@param ...  pointers to shared variables that aren't global

Do the actual fork and call the microtask in the relevant number of threads.
*/
void __kmpc_fork_teams(ident_t *loc, kmp_int32 argc, kmpc_micro microtask,
                       ...) {
  int gtid = __kmp_entry_gtid();
  kmp_info_t *this_thr = __kmp_threads[gtid];
  va_list ap;
  va_start(ap, microtask);

  KMP_COUNT_BLOCK(OMP_TEAMS);

  // remember teams entry point and nesting level
  this_thr->th.th_teams_microtask = microtask;
  this_thr->th.th_teams_level =
      this_thr->th.th_team->t.t_level; // AC: can be >0 on host

#if OMPT_SUPPORT
  kmp_team_t *parent_team = this_thr->th.th_team;
  int tid = __kmp_tid_from_gtid(gtid);
  if (ompt_enabled.enabled) {
    parent_team->t.t_implicit_task_taskdata[tid]
        .ompt_task_info.frame.enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
  }
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif

  // check if __kmpc_push_num_teams called, set default number of teams
  // otherwise
  if (this_thr->th.th_teams_size.nteams == 0) {
    __kmp_push_num_teams(loc, gtid, 0, 0);
  }
  KMP_DEBUG_ASSERT(this_thr->th.th_set_nproc >= 1);
  KMP_DEBUG_ASSERT(this_thr->th.th_teams_size.nteams >= 1);
  KMP_DEBUG_ASSERT(this_thr->th.th_teams_size.nth >= 1);

  __kmp_fork_call(loc, gtid, fork_context_intel, argc,
                  VOLATILE_CAST(microtask_t)
                      __kmp_teams_master, // "wrapped" task
                  VOLATILE_CAST(launch_t) __kmp_invoke_teams_master,
#if (KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64) && KMP_OS_LINUX
                  &ap
#else
                  ap
#endif
                  );
  __kmp_join_call(loc, gtid
#if OMPT_SUPPORT
                  ,
                  fork_context_intel
#endif
                  );

  this_thr->th.th_teams_microtask = NULL;
  this_thr->th.th_teams_level = 0;
  *(kmp_int64 *)(&this_thr->th.th_teams_size) = 0L;
  va_end(ap);
}
#endif /* OMP_40_ENABLED */

// I don't think this function should ever have been exported.
// The __kmpc_ prefix was misapplied.  I'm fairly certain that no generated
// openmp code ever called it, but it's been exported from the RTL for so
// long that I'm afraid to remove the definition.
int __kmpc_invoke_task_func(int gtid) { return __kmp_invoke_task_func(gtid); }

/*!
@ingroup PARALLEL
@param loc  source location information
@param global_tid  global thread number

Enter a serialized parallel construct. This interface is used to handle a
conditional parallel region, like this,
@code
#pragma omp parallel if (condition)
@endcode
when the condition is false.
*/
void __kmpc_serialized_parallel(ident_t *loc, kmp_int32 global_tid) {
// The implementation is now in kmp_runtime.cpp so that it can share static
// functions with kmp_fork_call since the tasks to be done are similar in
// each case.
#if OMPT_SUPPORT
  OMPT_STORE_RETURN_ADDRESS(global_tid);
#endif
  __kmp_serialized_parallel(loc, global_tid);
}

/*!
@ingroup PARALLEL
@param loc  source location information
@param global_tid  global thread number

Leave a serialized parallel construct.
*/
void __kmpc_end_serialized_parallel(ident_t *loc, kmp_int32 global_tid) {
  kmp_internal_control_t *top;
  kmp_info_t *this_thr;
  kmp_team_t *serial_team;

  KC_TRACE(10,
           ("__kmpc_end_serialized_parallel: called by T#%d\n", global_tid));

  /* skip all this code for autopar serialized loops since it results in
     unacceptable overhead */
  if (loc != NULL && (loc->flags & KMP_IDENT_AUTOPAR))
    return;

  // Not autopar code
  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  this_thr = __kmp_threads[global_tid];
  serial_team = this_thr->th.th_serial_team;

#if OMP_45_ENABLED
  kmp_task_team_t *task_team = this_thr->th.th_task_team;

  // we need to wait for the proxy tasks before finishing the thread
  if (task_team != NULL && task_team->tt.tt_found_proxy_tasks)
    __kmp_task_team_wait(this_thr, serial_team USE_ITT_BUILD_ARG(NULL));
#endif

  KMP_MB();
  KMP_DEBUG_ASSERT(serial_team);
  KMP_ASSERT(serial_team->t.t_serialized);
  KMP_DEBUG_ASSERT(this_thr->th.th_team == serial_team);
  KMP_DEBUG_ASSERT(serial_team != this_thr->th.th_root->r.r_root_team);
  KMP_DEBUG_ASSERT(serial_team->t.t_threads);
  KMP_DEBUG_ASSERT(serial_team->t.t_threads[0] == this_thr);

#if OMPT_SUPPORT
  if (ompt_enabled.enabled &&
      this_thr->th.ompt_thread_info.state != ompt_state_overhead) {
    OMPT_CUR_TASK_INFO(this_thr)->frame.exit_frame = ompt_data_none;
    if (ompt_enabled.ompt_callback_implicit_task) {
      ompt_callbacks.ompt_callback(ompt_callback_implicit_task)(
          ompt_scope_end, NULL, OMPT_CUR_TASK_DATA(this_thr), 1,
          OMPT_CUR_TASK_INFO(this_thr)->thread_num, ompt_task_implicit);
    }

    // reset clear the task id only after unlinking the task
    ompt_data_t *parent_task_data;
    __ompt_get_task_info_internal(1, NULL, &parent_task_data, NULL, NULL, NULL);

    if (ompt_enabled.ompt_callback_parallel_end) {
      ompt_callbacks.ompt_callback(ompt_callback_parallel_end)(
          &(serial_team->t.ompt_team_info.parallel_data), parent_task_data,
          ompt_parallel_invoker_program, OMPT_LOAD_RETURN_ADDRESS(global_tid));
    }
    __ompt_lw_taskteam_unlink(this_thr);
    this_thr->th.ompt_thread_info.state = ompt_state_overhead;
  }
#endif

  /* If necessary, pop the internal control stack values and replace the team
   * values */
  top = serial_team->t.t_control_stack_top;
  if (top && top->serial_nesting_level == serial_team->t.t_serialized) {
    copy_icvs(&serial_team->t.t_threads[0]->th.th_current_task->td_icvs, top);
    serial_team->t.t_control_stack_top = top->next;
    __kmp_free(top);
  }

  // if( serial_team -> t.t_serialized > 1 )
  serial_team->t.t_level--;

  /* pop dispatch buffers stack */
  KMP_DEBUG_ASSERT(serial_team->t.t_dispatch->th_disp_buffer);
  {
    dispatch_private_info_t *disp_buffer =
        serial_team->t.t_dispatch->th_disp_buffer;
    serial_team->t.t_dispatch->th_disp_buffer =
        serial_team->t.t_dispatch->th_disp_buffer->next;
    __kmp_free(disp_buffer);
  }
#if OMP_50_ENABLED
  this_thr->th.th_def_allocator = serial_team->t.t_def_allocator; // restore
#endif

  --serial_team->t.t_serialized;
  if (serial_team->t.t_serialized == 0) {

/* return to the parallel section */

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
    if (__kmp_inherit_fp_control && serial_team->t.t_fp_control_saved) {
      __kmp_clear_x87_fpu_status_word();
      __kmp_load_x87_fpu_control_word(&serial_team->t.t_x87_fpu_control_word);
      __kmp_load_mxcsr(&serial_team->t.t_mxcsr);
    }
#endif /* KMP_ARCH_X86 || KMP_ARCH_X86_64 */

    this_thr->th.th_team = serial_team->t.t_parent;
    this_thr->th.th_info.ds.ds_tid = serial_team->t.t_master_tid;

    /* restore values cached in the thread */
    this_thr->th.th_team_nproc = serial_team->t.t_parent->t.t_nproc; /*  JPH */
    this_thr->th.th_team_master =
        serial_team->t.t_parent->t.t_threads[0]; /* JPH */
    this_thr->th.th_team_serialized = this_thr->th.th_team->t.t_serialized;

    /* TODO the below shouldn't need to be adjusted for serialized teams */
    this_thr->th.th_dispatch =
        &this_thr->th.th_team->t.t_dispatch[serial_team->t.t_master_tid];

    __kmp_pop_current_task_from_thread(this_thr);

    KMP_ASSERT(this_thr->th.th_current_task->td_flags.executing == 0);
    this_thr->th.th_current_task->td_flags.executing = 1;

    if (__kmp_tasking_mode != tskm_immediate_exec) {
      // Copy the task team from the new child / old parent team to the thread.
      this_thr->th.th_task_team =
          this_thr->th.th_team->t.t_task_team[this_thr->th.th_task_state];
      KA_TRACE(20,
               ("__kmpc_end_serialized_parallel: T#%d restoring task_team %p / "
                "team %p\n",
                global_tid, this_thr->th.th_task_team, this_thr->th.th_team));
    }
  } else {
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      KA_TRACE(20, ("__kmpc_end_serialized_parallel: T#%d decreasing nesting "
                    "depth of serial team %p to %d\n",
                    global_tid, serial_team, serial_team->t.t_serialized));
    }
  }

  if (__kmp_env_consistency_check)
    __kmp_pop_parallel(global_tid, NULL);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled)
    this_thr->th.ompt_thread_info.state =
        ((this_thr->th.th_team_serialized) ? ompt_state_work_serial
                                           : ompt_state_work_parallel);
#endif
}

/*!
@ingroup SYNCHRONIZATION
@param loc  source location information.

Execute <tt>flush</tt>. This is implemented as a full memory fence. (Though
depending on the memory ordering convention obeyed by the compiler
even that may not be necessary).
*/
void __kmpc_flush(ident_t *loc) {
  KC_TRACE(10, ("__kmpc_flush: called\n"));

  /* need explicit __mf() here since use volatile instead in library */
  KMP_MB(); /* Flush all pending memory write invalidates.  */

#if (KMP_ARCH_X86 || KMP_ARCH_X86_64)
#if KMP_MIC
// fence-style instructions do not exist, but lock; xaddl $0,(%rsp) can be used.
// We shouldn't need it, though, since the ABI rules require that
// * If the compiler generates NGO stores it also generates the fence
// * If users hand-code NGO stores they should insert the fence
// therefore no incomplete unordered stores should be visible.
#else
  // C74404
  // This is to address non-temporal store instructions (sfence needed).
  // The clflush instruction is addressed either (mfence needed).
  // Probably the non-temporal load monvtdqa instruction should also be
  // addressed.
  // mfence is a SSE2 instruction. Do not execute it if CPU is not SSE2.
  if (!__kmp_cpuinfo.initialized) {
    __kmp_query_cpuid(&__kmp_cpuinfo);
  }
  if (!__kmp_cpuinfo.sse2) {
    // CPU cannot execute SSE2 instructions.
  } else {
#if KMP_COMPILER_ICC
    _mm_mfence();
#elif KMP_COMPILER_MSVC
    MemoryBarrier();
#else
    __sync_synchronize();
#endif // KMP_COMPILER_ICC
  }
#endif // KMP_MIC
#elif (KMP_ARCH_ARM || KMP_ARCH_AARCH64 || KMP_ARCH_MIPS || KMP_ARCH_MIPS64)
// Nothing to see here move along
#elif KMP_ARCH_PPC64
// Nothing needed here (we have a real MB above).
#if KMP_OS_CNK
  // The flushing thread needs to yield here; this prevents a
  // busy-waiting thread from saturating the pipeline. flush is
  // often used in loops like this:
  // while (!flag) {
  //   #pragma omp flush(flag)
  // }
  // and adding the yield here is good for at least a 10x speedup
  // when running >2 threads per core (on the NAS LU benchmark).
  __kmp_yield(TRUE);
#endif
#else
#error Unknown or unsupported architecture
#endif

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_flush) {
    ompt_callbacks.ompt_callback(ompt_callback_flush)(
        __ompt_get_thread_data_internal(), OMPT_GET_RETURN_ADDRESS(0));
  }
#endif
}

/* -------------------------------------------------------------------------- */
/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid thread id.

Execute a barrier.
*/
void __kmpc_barrier(ident_t *loc, kmp_int32 global_tid) {
  KMP_COUNT_BLOCK(OMP_BARRIER);
  KC_TRACE(10, ("__kmpc_barrier: called T#%d\n", global_tid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  if (__kmp_env_consistency_check) {
    if (loc == 0) {
      KMP_WARNING(ConstructIdentInvalid); // ??? What does it mean for the user?
    }

    __kmp_check_barrier(global_tid, ct_barrier, loc);
  }

#if OMPT_SUPPORT
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    if (ompt_frame->enter_frame.ptr == NULL)
      ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(global_tid);
  }
#endif
  __kmp_threads[global_tid]->th.th_ident = loc;
  // TODO: explicit barrier_wait_id:
  //   this function is called when 'barrier' directive is present or
  //   implicit barrier at the end of a worksharing construct.
  // 1) better to add a per-thread barrier counter to a thread data structure
  // 2) set to 0 when a new team is created
  // 4) no sync is required

  __kmp_barrier(bs_plain_barrier, global_tid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif
}

/* The BARRIER for a MASTER section is always explicit   */
/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param global_tid  global thread number .
@return 1 if this thread should execute the <tt>master</tt> block, 0 otherwise.
*/
kmp_int32 __kmpc_master(ident_t *loc, kmp_int32 global_tid) {
  int status = 0;

  KC_TRACE(10, ("__kmpc_master: called T#%d\n", global_tid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  if (KMP_MASTER_GTID(global_tid)) {
    KMP_COUNT_BLOCK(OMP_MASTER);
    KMP_PUSH_PARTITIONED_TIMER(OMP_master);
    status = 1;
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (status) {
    if (ompt_enabled.ompt_callback_master) {
      kmp_info_t *this_thr = __kmp_threads[global_tid];
      kmp_team_t *team = this_thr->th.th_team;

      int tid = __kmp_tid_from_gtid(global_tid);
      ompt_callbacks.ompt_callback(ompt_callback_master)(
          ompt_scope_begin, &(team->t.ompt_team_info.parallel_data),
          &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data),
          OMPT_GET_RETURN_ADDRESS(0));
    }
  }
#endif

  if (__kmp_env_consistency_check) {
#if KMP_USE_DYNAMIC_LOCK
    if (status)
      __kmp_push_sync(global_tid, ct_master, loc, NULL, 0);
    else
      __kmp_check_sync(global_tid, ct_master, loc, NULL, 0);
#else
    if (status)
      __kmp_push_sync(global_tid, ct_master, loc, NULL);
    else
      __kmp_check_sync(global_tid, ct_master, loc, NULL);
#endif
  }

  return status;
}

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param global_tid  global thread number .

Mark the end of a <tt>master</tt> region. This should only be called by the
thread that executes the <tt>master</tt> region.
*/
void __kmpc_end_master(ident_t *loc, kmp_int32 global_tid) {
  KC_TRACE(10, ("__kmpc_end_master: called T#%d\n", global_tid));

  KMP_DEBUG_ASSERT(KMP_MASTER_GTID(global_tid));
  KMP_POP_PARTITIONED_TIMER();

#if OMPT_SUPPORT && OMPT_OPTIONAL
  kmp_info_t *this_thr = __kmp_threads[global_tid];
  kmp_team_t *team = this_thr->th.th_team;
  if (ompt_enabled.ompt_callback_master) {
    int tid = __kmp_tid_from_gtid(global_tid);
    ompt_callbacks.ompt_callback(ompt_callback_master)(
        ompt_scope_end, &(team->t.ompt_team_info.parallel_data),
        &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data),
        OMPT_GET_RETURN_ADDRESS(0));
  }
#endif

  if (__kmp_env_consistency_check) {
    if (global_tid < 0)
      KMP_WARNING(ThreadIdentInvalid);

    if (KMP_MASTER_GTID(global_tid))
      __kmp_pop_sync(global_tid, ct_master, loc);
  }
}

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param gtid  global thread number.

Start execution of an <tt>ordered</tt> construct.
*/
void __kmpc_ordered(ident_t *loc, kmp_int32 gtid) {
  int cid = 0;
  kmp_info_t *th;
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  KC_TRACE(10, ("__kmpc_ordered: called T#%d\n", gtid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

#if USE_ITT_BUILD
  __kmp_itt_ordered_prep(gtid);
// TODO: ordered_wait_id
#endif /* USE_ITT_BUILD */

  th = __kmp_threads[gtid];

#if OMPT_SUPPORT && OMPT_OPTIONAL
  kmp_team_t *team;
  ompt_wait_id_t lck;
  void *codeptr_ra;
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
    team = __kmp_team_from_gtid(gtid);
    lck = (ompt_wait_id_t)&team->t.t_ordered.dt.t_value;
    /* OMPT state update */
    th->th.ompt_thread_info.wait_id = lck;
    th->th.ompt_thread_info.state = ompt_state_wait_ordered;

    /* OMPT event callback */
    codeptr_ra = OMPT_LOAD_RETURN_ADDRESS(gtid);
    if (ompt_enabled.ompt_callback_mutex_acquire) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
          ompt_mutex_ordered, omp_lock_hint_none, kmp_mutex_impl_spin,
          (ompt_wait_id_t)lck, codeptr_ra);
    }
  }
#endif

  if (th->th.th_dispatch->th_deo_fcn != 0)
    (*th->th.th_dispatch->th_deo_fcn)(&gtid, &cid, loc);
  else
    __kmp_parallel_deo(&gtid, &cid, loc);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    /* OMPT state update */
    th->th.ompt_thread_info.state = ompt_state_work_parallel;
    th->th.ompt_thread_info.wait_id = 0;

    /* OMPT event callback */
    if (ompt_enabled.ompt_callback_mutex_acquired) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
          ompt_mutex_ordered, (ompt_wait_id_t)lck, codeptr_ra);
    }
  }
#endif

#if USE_ITT_BUILD
  __kmp_itt_ordered_start(gtid);
#endif /* USE_ITT_BUILD */
}

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param gtid  global thread number.

End execution of an <tt>ordered</tt> construct.
*/
void __kmpc_end_ordered(ident_t *loc, kmp_int32 gtid) {
  int cid = 0;
  kmp_info_t *th;

  KC_TRACE(10, ("__kmpc_end_ordered: called T#%d\n", gtid));

#if USE_ITT_BUILD
  __kmp_itt_ordered_end(gtid);
// TODO: ordered_wait_id
#endif /* USE_ITT_BUILD */

  th = __kmp_threads[gtid];

  if (th->th.th_dispatch->th_dxo_fcn != 0)
    (*th->th.th_dispatch->th_dxo_fcn)(&gtid, &cid, loc);
  else
    __kmp_parallel_dxo(&gtid, &cid, loc);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
  if (ompt_enabled.ompt_callback_mutex_released) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
        ompt_mutex_ordered,
        (ompt_wait_id_t)&__kmp_team_from_gtid(gtid)->t.t_ordered.dt.t_value,
        OMPT_LOAD_RETURN_ADDRESS(gtid));
  }
#endif
}

#if KMP_USE_DYNAMIC_LOCK

static __forceinline void
__kmp_init_indirect_csptr(kmp_critical_name *crit, ident_t const *loc,
                          kmp_int32 gtid, kmp_indirect_locktag_t tag) {
  // Pointer to the allocated indirect lock is written to crit, while indexing
  // is ignored.
  void *idx;
  kmp_indirect_lock_t **lck;
  lck = (kmp_indirect_lock_t **)crit;
  kmp_indirect_lock_t *ilk = __kmp_allocate_indirect_lock(&idx, gtid, tag);
  KMP_I_LOCK_FUNC(ilk, init)(ilk->lock);
  KMP_SET_I_LOCK_LOCATION(ilk, loc);
  KMP_SET_I_LOCK_FLAGS(ilk, kmp_lf_critical_section);
  KA_TRACE(20,
           ("__kmp_init_indirect_csptr: initialized indirect lock #%d\n", tag));
#if USE_ITT_BUILD
  __kmp_itt_critical_creating(ilk->lock, loc);
#endif
  int status = KMP_COMPARE_AND_STORE_PTR(lck, nullptr, ilk);
  if (status == 0) {
#if USE_ITT_BUILD
    __kmp_itt_critical_destroyed(ilk->lock);
#endif
    // We don't really need to destroy the unclaimed lock here since it will be
    // cleaned up at program exit.
    // KMP_D_LOCK_FUNC(&idx, destroy)((kmp_dyna_lock_t *)&idx);
  }
  KMP_DEBUG_ASSERT(*lck != NULL);
}

// Fast-path acquire tas lock
#define KMP_ACQUIRE_TAS_LOCK(lock, gtid)                                       \
  {                                                                            \
    kmp_tas_lock_t *l = (kmp_tas_lock_t *)lock;                                \
    kmp_int32 tas_free = KMP_LOCK_FREE(tas);                                   \
    kmp_int32 tas_busy = KMP_LOCK_BUSY(gtid + 1, tas);                         \
    if (KMP_ATOMIC_LD_RLX(&l->lk.poll) != tas_free ||                          \
        !__kmp_atomic_compare_store_acq(&l->lk.poll, tas_free, tas_busy)) {    \
      kmp_uint32 spins;                                                        \
      KMP_FSYNC_PREPARE(l);                                                    \
      KMP_INIT_YIELD(spins);                                                   \
      if (TCR_4(__kmp_nth) >                                                   \
          (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc)) {               \
        KMP_YIELD(TRUE);                                                       \
      } else {                                                                 \
        KMP_YIELD_SPIN(spins);                                                 \
      }                                                                        \
      kmp_backoff_t backoff = __kmp_spin_backoff_params;                       \
      while (                                                                  \
          KMP_ATOMIC_LD_RLX(&l->lk.poll) != tas_free ||                        \
          !__kmp_atomic_compare_store_acq(&l->lk.poll, tas_free, tas_busy)) {  \
        __kmp_spin_backoff(&backoff);                                          \
        if (TCR_4(__kmp_nth) >                                                 \
            (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc)) {             \
          KMP_YIELD(TRUE);                                                     \
        } else {                                                               \
          KMP_YIELD_SPIN(spins);                                               \
        }                                                                      \
      }                                                                        \
    }                                                                          \
    KMP_FSYNC_ACQUIRED(l);                                                     \
  }

// Fast-path test tas lock
#define KMP_TEST_TAS_LOCK(lock, gtid, rc)                                      \
  {                                                                            \
    kmp_tas_lock_t *l = (kmp_tas_lock_t *)lock;                                \
    kmp_int32 tas_free = KMP_LOCK_FREE(tas);                                   \
    kmp_int32 tas_busy = KMP_LOCK_BUSY(gtid + 1, tas);                         \
    rc = KMP_ATOMIC_LD_RLX(&l->lk.poll) == tas_free &&                         \
         __kmp_atomic_compare_store_acq(&l->lk.poll, tas_free, tas_busy);      \
  }

// Fast-path release tas lock
#define KMP_RELEASE_TAS_LOCK(lock, gtid)                                       \
  { KMP_ATOMIC_ST_REL(&((kmp_tas_lock_t *)lock)->lk.poll, KMP_LOCK_FREE(tas)); }

#if KMP_USE_FUTEX

#include <sys/syscall.h>
#include <unistd.h>
#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#endif
#ifndef FUTEX_WAKE
#define FUTEX_WAKE 1
#endif

// Fast-path acquire futex lock
#define KMP_ACQUIRE_FUTEX_LOCK(lock, gtid)                                     \
  {                                                                            \
    kmp_futex_lock_t *ftx = (kmp_futex_lock_t *)lock;                          \
    kmp_int32 gtid_code = (gtid + 1) << 1;                                     \
    KMP_MB();                                                                  \
    KMP_FSYNC_PREPARE(ftx);                                                    \
    kmp_int32 poll_val;                                                        \
    while ((poll_val = KMP_COMPARE_AND_STORE_RET32(                            \
                &(ftx->lk.poll), KMP_LOCK_FREE(futex),                         \
                KMP_LOCK_BUSY(gtid_code, futex))) != KMP_LOCK_FREE(futex)) {   \
      kmp_int32 cond = KMP_LOCK_STRIP(poll_val) & 1;                           \
      if (!cond) {                                                             \
        if (!KMP_COMPARE_AND_STORE_RET32(&(ftx->lk.poll), poll_val,            \
                                         poll_val |                            \
                                             KMP_LOCK_BUSY(1, futex))) {       \
          continue;                                                            \
        }                                                                      \
        poll_val |= KMP_LOCK_BUSY(1, futex);                                   \
      }                                                                        \
      kmp_int32 rc;                                                            \
      if ((rc = syscall(__NR_futex, &(ftx->lk.poll), FUTEX_WAIT, poll_val,     \
                        NULL, NULL, 0)) != 0) {                                \
        continue;                                                              \
      }                                                                        \
      gtid_code |= 1;                                                          \
    }                                                                          \
    KMP_FSYNC_ACQUIRED(ftx);                                                   \
  }

// Fast-path test futex lock
#define KMP_TEST_FUTEX_LOCK(lock, gtid, rc)                                    \
  {                                                                            \
    kmp_futex_lock_t *ftx = (kmp_futex_lock_t *)lock;                          \
    if (KMP_COMPARE_AND_STORE_ACQ32(&(ftx->lk.poll), KMP_LOCK_FREE(futex),     \
                                    KMP_LOCK_BUSY(gtid + 1 << 1, futex))) {    \
      KMP_FSYNC_ACQUIRED(ftx);                                                 \
      rc = TRUE;                                                               \
    } else {                                                                   \
      rc = FALSE;                                                              \
    }                                                                          \
  }

// Fast-path release futex lock
#define KMP_RELEASE_FUTEX_LOCK(lock, gtid)                                     \
  {                                                                            \
    kmp_futex_lock_t *ftx = (kmp_futex_lock_t *)lock;                          \
    KMP_MB();                                                                  \
    KMP_FSYNC_RELEASING(ftx);                                                  \
    kmp_int32 poll_val =                                                       \
        KMP_XCHG_FIXED32(&(ftx->lk.poll), KMP_LOCK_FREE(futex));               \
    if (KMP_LOCK_STRIP(poll_val) & 1) {                                        \
      syscall(__NR_futex, &(ftx->lk.poll), FUTEX_WAKE,                         \
              KMP_LOCK_BUSY(1, futex), NULL, NULL, 0);                         \
    }                                                                          \
    KMP_MB();                                                                  \
    KMP_YIELD(TCR_4(__kmp_nth) >                                               \
              (__kmp_avail_proc ? __kmp_avail_proc : __kmp_xproc));            \
  }

#endif // KMP_USE_FUTEX

#else // KMP_USE_DYNAMIC_LOCK

static kmp_user_lock_p __kmp_get_critical_section_ptr(kmp_critical_name *crit,
                                                      ident_t const *loc,
                                                      kmp_int32 gtid) {
  kmp_user_lock_p *lck_pp = (kmp_user_lock_p *)crit;

  // Because of the double-check, the following load doesn't need to be volatile
  kmp_user_lock_p lck = (kmp_user_lock_p)TCR_PTR(*lck_pp);

  if (lck == NULL) {
    void *idx;

    // Allocate & initialize the lock.
    // Remember alloc'ed locks in table in order to free them in __kmp_cleanup()
    lck = __kmp_user_lock_allocate(&idx, gtid, kmp_lf_critical_section);
    __kmp_init_user_lock_with_checks(lck);
    __kmp_set_user_lock_location(lck, loc);
#if USE_ITT_BUILD
    __kmp_itt_critical_creating(lck);
// __kmp_itt_critical_creating() should be called *before* the first usage
// of underlying lock. It is the only place where we can guarantee it. There
// are chances the lock will destroyed with no usage, but it is not a
// problem, because this is not real event seen by user but rather setting
// name for object (lock). See more details in kmp_itt.h.
#endif /* USE_ITT_BUILD */

    // Use a cmpxchg instruction to slam the start of the critical section with
    // the lock pointer.  If another thread beat us to it, deallocate the lock,
    // and use the lock that the other thread allocated.
    int status = KMP_COMPARE_AND_STORE_PTR(lck_pp, 0, lck);

    if (status == 0) {
// Deallocate the lock and reload the value.
#if USE_ITT_BUILD
      __kmp_itt_critical_destroyed(lck);
// Let ITT know the lock is destroyed and the same memory location may be reused
// for another purpose.
#endif /* USE_ITT_BUILD */
      __kmp_destroy_user_lock_with_checks(lck);
      __kmp_user_lock_free(&idx, gtid, lck);
      lck = (kmp_user_lock_p)TCR_PTR(*lck_pp);
      KMP_DEBUG_ASSERT(lck != NULL);
    }
  }
  return lck;
}

#endif // KMP_USE_DYNAMIC_LOCK

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param global_tid  global thread number .
@param crit identity of the critical section. This could be a pointer to a lock
associated with the critical section, or some other suitably unique value.

Enter code protected by a `critical` construct.
This function blocks until the executing thread can enter the critical section.
*/
void __kmpc_critical(ident_t *loc, kmp_int32 global_tid,
                     kmp_critical_name *crit) {
#if KMP_USE_DYNAMIC_LOCK
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(global_tid);
#endif // OMPT_SUPPORT
  __kmpc_critical_with_hint(loc, global_tid, crit, omp_lock_hint_none);
#else
  KMP_COUNT_BLOCK(OMP_CRITICAL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_state_t prev_state = ompt_state_undefined;
  ompt_thread_info_t ti;
#endif
  kmp_user_lock_p lck;

  KC_TRACE(10, ("__kmpc_critical: called T#%d\n", global_tid));

  // TODO: add THR_OVHD_STATE

  KMP_PUSH_PARTITIONED_TIMER(OMP_critical_wait);
  KMP_CHECK_USER_LOCK_INIT();

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_CRITICAL_SIZE)) {
    lck = (kmp_user_lock_p)crit;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_CRITICAL_SIZE)) {
    lck = (kmp_user_lock_p)crit;
  }
#endif
  else { // ticket, queuing or drdpa
    lck = __kmp_get_critical_section_ptr(crit, loc, global_tid);
  }

  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_critical, loc, lck);

// since the critical directive binds to all threads, not just the current
// team we have to check this even if we are in a serialized team.
// also, even if we are the uber thread, we still have to conduct the lock,
// as we have to contend with sibling threads.

#if USE_ITT_BUILD
  __kmp_itt_critical_acquiring(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
  void *codeptr_ra = NULL;
  if (ompt_enabled.enabled) {
    ti = __kmp_threads[global_tid]->th.ompt_thread_info;
    /* OMPT state update */
    prev_state = ti.state;
    ti.wait_id = (ompt_wait_id_t)lck;
    ti.state = ompt_state_wait_critical;

    /* OMPT event callback */
    codeptr_ra = OMPT_LOAD_RETURN_ADDRESS(gtid);
    if (ompt_enabled.ompt_callback_mutex_acquire) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
          ompt_mutex_critical, omp_lock_hint_none, __ompt_get_mutex_impl_type(),
          (ompt_wait_id_t)crit, codeptr_ra);
    }
  }
#endif
  // Value of 'crit' should be good for using as a critical_id of the critical
  // section directive.
  __kmp_acquire_user_lock_with_checks(lck, global_tid);

#if USE_ITT_BUILD
  __kmp_itt_critical_acquired(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    /* OMPT state update */
    ti.state = prev_state;
    ti.wait_id = 0;

    /* OMPT event callback */
    if (ompt_enabled.ompt_callback_mutex_acquired) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
          ompt_mutex_critical, (ompt_wait_id_t)crit, codeptr_ra);
    }
  }
#endif
  KMP_POP_PARTITIONED_TIMER();

  KMP_PUSH_PARTITIONED_TIMER(OMP_critical);
  KA_TRACE(15, ("__kmpc_critical: done T#%d\n", global_tid));
#endif // KMP_USE_DYNAMIC_LOCK
}

#if KMP_USE_DYNAMIC_LOCK

// Converts the given hint to an internal lock implementation
static __forceinline kmp_dyna_lockseq_t __kmp_map_hint_to_lock(uintptr_t hint) {
#if KMP_USE_TSX
#define KMP_TSX_LOCK(seq) lockseq_##seq
#else
#define KMP_TSX_LOCK(seq) __kmp_user_lock_seq
#endif

#if KMP_ARCH_X86 || KMP_ARCH_X86_64
#define KMP_CPUINFO_RTM (__kmp_cpuinfo.rtm)
#else
#define KMP_CPUINFO_RTM 0
#endif

  // Hints that do not require further logic
  if (hint & kmp_lock_hint_hle)
    return KMP_TSX_LOCK(hle);
  if (hint & kmp_lock_hint_rtm)
    return KMP_CPUINFO_RTM ? KMP_TSX_LOCK(rtm) : __kmp_user_lock_seq;
  if (hint & kmp_lock_hint_adaptive)
    return KMP_CPUINFO_RTM ? KMP_TSX_LOCK(adaptive) : __kmp_user_lock_seq;

  // Rule out conflicting hints first by returning the default lock
  if ((hint & omp_lock_hint_contended) && (hint & omp_lock_hint_uncontended))
    return __kmp_user_lock_seq;
  if ((hint & omp_lock_hint_speculative) &&
      (hint & omp_lock_hint_nonspeculative))
    return __kmp_user_lock_seq;

  // Do not even consider speculation when it appears to be contended
  if (hint & omp_lock_hint_contended)
    return lockseq_queuing;

  // Uncontended lock without speculation
  if ((hint & omp_lock_hint_uncontended) && !(hint & omp_lock_hint_speculative))
    return lockseq_tas;

  // HLE lock for speculation
  if (hint & omp_lock_hint_speculative)
    return KMP_TSX_LOCK(hle);

  return __kmp_user_lock_seq;
}

#if OMPT_SUPPORT && OMPT_OPTIONAL
#if KMP_USE_DYNAMIC_LOCK
static kmp_mutex_impl_t
__ompt_get_mutex_impl_type(void *user_lock, kmp_indirect_lock_t *ilock = 0) {
  if (user_lock) {
    switch (KMP_EXTRACT_D_TAG(user_lock)) {
    case 0:
      break;
#if KMP_USE_FUTEX
    case locktag_futex:
      return kmp_mutex_impl_queuing;
#endif
    case locktag_tas:
      return kmp_mutex_impl_spin;
#if KMP_USE_TSX
    case locktag_hle:
      return kmp_mutex_impl_speculative;
#endif
    default:
      return kmp_mutex_impl_none;
    }
    ilock = KMP_LOOKUP_I_LOCK(user_lock);
  }
  KMP_ASSERT(ilock);
  switch (ilock->type) {
#if KMP_USE_TSX
  case locktag_adaptive:
  case locktag_rtm:
    return kmp_mutex_impl_speculative;
#endif
  case locktag_nested_tas:
    return kmp_mutex_impl_spin;
#if KMP_USE_FUTEX
  case locktag_nested_futex:
#endif
  case locktag_ticket:
  case locktag_queuing:
  case locktag_drdpa:
  case locktag_nested_ticket:
  case locktag_nested_queuing:
  case locktag_nested_drdpa:
    return kmp_mutex_impl_queuing;
  default:
    return kmp_mutex_impl_none;
  }
}
#else
// For locks without dynamic binding
static kmp_mutex_impl_t __ompt_get_mutex_impl_type() {
  switch (__kmp_user_lock_kind) {
  case lk_tas:
    return kmp_mutex_impl_spin;
#if KMP_USE_FUTEX
  case lk_futex:
#endif
  case lk_ticket:
  case lk_queuing:
  case lk_drdpa:
    return kmp_mutex_impl_queuing;
#if KMP_USE_TSX
  case lk_hle:
  case lk_rtm:
  case lk_adaptive:
    return kmp_mutex_impl_speculative;
#endif
  default:
    return kmp_mutex_impl_none;
  }
}
#endif // KMP_USE_DYNAMIC_LOCK
#endif // OMPT_SUPPORT && OMPT_OPTIONAL

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param global_tid  global thread number.
@param crit identity of the critical section. This could be a pointer to a lock
associated with the critical section, or some other suitably unique value.
@param hint the lock hint.

Enter code protected by a `critical` construct with a hint. The hint value is
used to suggest a lock implementation. This function blocks until the executing
thread can enter the critical section unless the hint suggests use of
speculative execution and the hardware supports it.
*/
void __kmpc_critical_with_hint(ident_t *loc, kmp_int32 global_tid,
                               kmp_critical_name *crit, uint32_t hint) {
  KMP_COUNT_BLOCK(OMP_CRITICAL);
  kmp_user_lock_p lck;
#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_state_t prev_state = ompt_state_undefined;
  ompt_thread_info_t ti;
  // This is the case, if called from __kmpc_critical:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(global_tid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
#endif

  KC_TRACE(10, ("__kmpc_critical: called T#%d\n", global_tid));

  kmp_dyna_lock_t *lk = (kmp_dyna_lock_t *)crit;
  // Check if it is initialized.
  KMP_PUSH_PARTITIONED_TIMER(OMP_critical_wait);
  if (*lk == 0) {
    kmp_dyna_lockseq_t lckseq = __kmp_map_hint_to_lock(hint);
    if (KMP_IS_D_LOCK(lckseq)) {
      KMP_COMPARE_AND_STORE_ACQ32((volatile kmp_int32 *)crit, 0,
                                  KMP_GET_D_TAG(lckseq));
    } else {
      __kmp_init_indirect_csptr(crit, loc, global_tid, KMP_GET_I_TAG(lckseq));
    }
  }
  // Branch for accessing the actual lock object and set operation. This
  // branching is inevitable since this lock initialization does not follow the
  // normal dispatch path (lock table is not used).
  if (KMP_EXTRACT_D_TAG(lk) != 0) {
    lck = (kmp_user_lock_p)lk;
    if (__kmp_env_consistency_check) {
      __kmp_push_sync(global_tid, ct_critical, loc, lck,
                      __kmp_map_hint_to_lock(hint));
    }
#if USE_ITT_BUILD
    __kmp_itt_critical_acquiring(lck);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ti = __kmp_threads[global_tid]->th.ompt_thread_info;
      /* OMPT state update */
      prev_state = ti.state;
      ti.wait_id = (ompt_wait_id_t)lck;
      ti.state = ompt_state_wait_critical;

      /* OMPT event callback */
      if (ompt_enabled.ompt_callback_mutex_acquire) {
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
            ompt_mutex_critical, (unsigned int)hint,
            __ompt_get_mutex_impl_type(crit), (ompt_wait_id_t)crit, codeptr);
      }
    }
#endif
#if KMP_USE_INLINED_TAS
    if (__kmp_user_lock_seq == lockseq_tas && !__kmp_env_consistency_check) {
      KMP_ACQUIRE_TAS_LOCK(lck, global_tid);
    } else
#elif KMP_USE_INLINED_FUTEX
    if (__kmp_user_lock_seq == lockseq_futex && !__kmp_env_consistency_check) {
      KMP_ACQUIRE_FUTEX_LOCK(lck, global_tid);
    } else
#endif
    {
      KMP_D_LOCK_FUNC(lk, set)(lk, global_tid);
    }
  } else {
    kmp_indirect_lock_t *ilk = *((kmp_indirect_lock_t **)lk);
    lck = ilk->lock;
    if (__kmp_env_consistency_check) {
      __kmp_push_sync(global_tid, ct_critical, loc, lck,
                      __kmp_map_hint_to_lock(hint));
    }
#if USE_ITT_BUILD
    __kmp_itt_critical_acquiring(lck);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ti = __kmp_threads[global_tid]->th.ompt_thread_info;
      /* OMPT state update */
      prev_state = ti.state;
      ti.wait_id = (ompt_wait_id_t)lck;
      ti.state = ompt_state_wait_critical;

      /* OMPT event callback */
      if (ompt_enabled.ompt_callback_mutex_acquire) {
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
            ompt_mutex_critical, (unsigned int)hint,
            __ompt_get_mutex_impl_type(0, ilk), (ompt_wait_id_t)crit, codeptr);
      }
    }
#endif
    KMP_I_LOCK_FUNC(ilk, set)(lck, global_tid);
  }
  KMP_POP_PARTITIONED_TIMER();

#if USE_ITT_BUILD
  __kmp_itt_critical_acquired(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    /* OMPT state update */
    ti.state = prev_state;
    ti.wait_id = 0;

    /* OMPT event callback */
    if (ompt_enabled.ompt_callback_mutex_acquired) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
          ompt_mutex_critical, (ompt_wait_id_t)crit, codeptr);
    }
  }
#endif

  KMP_PUSH_PARTITIONED_TIMER(OMP_critical);
  KA_TRACE(15, ("__kmpc_critical: done T#%d\n", global_tid));
} // __kmpc_critical_with_hint

#endif // KMP_USE_DYNAMIC_LOCK

/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param global_tid  global thread number .
@param crit identity of the critical section. This could be a pointer to a lock
associated with the critical section, or some other suitably unique value.

Leave a critical section, releasing any lock that was held during its execution.
*/
void __kmpc_end_critical(ident_t *loc, kmp_int32 global_tid,
                         kmp_critical_name *crit) {
  kmp_user_lock_p lck;

  KC_TRACE(10, ("__kmpc_end_critical: called T#%d\n", global_tid));

#if KMP_USE_DYNAMIC_LOCK
  if (KMP_IS_D_LOCK(__kmp_user_lock_seq)) {
    lck = (kmp_user_lock_p)crit;
    KMP_ASSERT(lck != NULL);
    if (__kmp_env_consistency_check) {
      __kmp_pop_sync(global_tid, ct_critical, loc);
    }
#if USE_ITT_BUILD
    __kmp_itt_critical_releasing(lck);
#endif
#if KMP_USE_INLINED_TAS
    if (__kmp_user_lock_seq == lockseq_tas && !__kmp_env_consistency_check) {
      KMP_RELEASE_TAS_LOCK(lck, global_tid);
    } else
#elif KMP_USE_INLINED_FUTEX
    if (__kmp_user_lock_seq == lockseq_futex && !__kmp_env_consistency_check) {
      KMP_RELEASE_FUTEX_LOCK(lck, global_tid);
    } else
#endif
    {
      KMP_D_LOCK_FUNC(lck, unset)((kmp_dyna_lock_t *)lck, global_tid);
    }
  } else {
    kmp_indirect_lock_t *ilk =
        (kmp_indirect_lock_t *)TCR_PTR(*((kmp_indirect_lock_t **)crit));
    KMP_ASSERT(ilk != NULL);
    lck = ilk->lock;
    if (__kmp_env_consistency_check) {
      __kmp_pop_sync(global_tid, ct_critical, loc);
    }
#if USE_ITT_BUILD
    __kmp_itt_critical_releasing(lck);
#endif
    KMP_I_LOCK_FUNC(ilk, unset)(lck, global_tid);
  }

#else // KMP_USE_DYNAMIC_LOCK

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_CRITICAL_SIZE)) {
    lck = (kmp_user_lock_p)crit;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_CRITICAL_SIZE)) {
    lck = (kmp_user_lock_p)crit;
  }
#endif
  else { // ticket, queuing or drdpa
    lck = (kmp_user_lock_p)TCR_PTR(*((kmp_user_lock_p *)crit));
  }

  KMP_ASSERT(lck != NULL);

  if (__kmp_env_consistency_check)
    __kmp_pop_sync(global_tid, ct_critical, loc);

#if USE_ITT_BUILD
  __kmp_itt_critical_releasing(lck);
#endif /* USE_ITT_BUILD */
  // Value of 'crit' should be good for using as a critical_id of the critical
  // section directive.
  __kmp_release_user_lock_with_checks(lck, global_tid);

#endif // KMP_USE_DYNAMIC_LOCK

#if OMPT_SUPPORT && OMPT_OPTIONAL
  /* OMPT release event triggers after lock is released; place here to trigger
   * for all #if branches */
  OMPT_STORE_RETURN_ADDRESS(global_tid);
  if (ompt_enabled.ompt_callback_mutex_released) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
        ompt_mutex_critical, (ompt_wait_id_t)crit, OMPT_LOAD_RETURN_ADDRESS(0));
  }
#endif

  KMP_POP_PARTITIONED_TIMER();
  KA_TRACE(15, ("__kmpc_end_critical: done T#%d\n", global_tid));
}

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid thread id.
@return one if the thread should execute the master block, zero otherwise

Start execution of a combined barrier and master. The barrier is executed inside
this function.
*/
kmp_int32 __kmpc_barrier_master(ident_t *loc, kmp_int32 global_tid) {
  int status;

  KC_TRACE(10, ("__kmpc_barrier_master: called T#%d\n", global_tid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  if (__kmp_env_consistency_check)
    __kmp_check_barrier(global_tid, ct_barrier, loc);

#if OMPT_SUPPORT
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    if (ompt_frame->enter_frame.ptr == NULL)
      ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(global_tid);
  }
#endif
#if USE_ITT_NOTIFY
  __kmp_threads[global_tid]->th.th_ident = loc;
#endif
  status = __kmp_barrier(bs_plain_barrier, global_tid, TRUE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif

  return (status != 0) ? 0 : 1;
}

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid thread id.

Complete the execution of a combined barrier and master. This function should
only be called at the completion of the <tt>master</tt> code. Other threads will
still be waiting at the barrier and this call releases them.
*/
void __kmpc_end_barrier_master(ident_t *loc, kmp_int32 global_tid) {
  KC_TRACE(10, ("__kmpc_end_barrier_master: called T#%d\n", global_tid));

  __kmp_end_split_barrier(bs_plain_barrier, global_tid);
}

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid thread id.
@return one if the thread should execute the master block, zero otherwise

Start execution of a combined barrier and master(nowait) construct.
The barrier is executed inside this function.
There is no equivalent "end" function, since the
*/
kmp_int32 __kmpc_barrier_master_nowait(ident_t *loc, kmp_int32 global_tid) {
  kmp_int32 ret;

  KC_TRACE(10, ("__kmpc_barrier_master_nowait: called T#%d\n", global_tid));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

  if (__kmp_env_consistency_check) {
    if (loc == 0) {
      KMP_WARNING(ConstructIdentInvalid); // ??? What does it mean for the user?
    }
    __kmp_check_barrier(global_tid, ct_barrier, loc);
  }

#if OMPT_SUPPORT
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    if (ompt_frame->enter_frame.ptr == NULL)
      ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(global_tid);
  }
#endif
#if USE_ITT_NOTIFY
  __kmp_threads[global_tid]->th.th_ident = loc;
#endif
  __kmp_barrier(bs_plain_barrier, global_tid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif

  ret = __kmpc_master(loc, global_tid);

  if (__kmp_env_consistency_check) {
    /*  there's no __kmpc_end_master called; so the (stats) */
    /*  actions of __kmpc_end_master are done here          */

    if (global_tid < 0) {
      KMP_WARNING(ThreadIdentInvalid);
    }
    if (ret) {
      /* only one thread should do the pop since only */
      /* one did the push (see __kmpc_master())       */

      __kmp_pop_sync(global_tid, ct_master, loc);
    }
  }

  return (ret);
}

/* The BARRIER for a SINGLE process section is always explicit   */
/*!
@ingroup WORK_SHARING
@param loc  source location information
@param global_tid  global thread number
@return One if this thread should execute the single construct, zero otherwise.

Test whether to execute a <tt>single</tt> construct.
There are no implicit barriers in the two "single" calls, rather the compiler
should introduce an explicit barrier if it is required.
*/

kmp_int32 __kmpc_single(ident_t *loc, kmp_int32 global_tid) {
  kmp_int32 rc = __kmp_enter_single(global_tid, loc, TRUE);

  if (rc) {
    // We are going to execute the single statement, so we should count it.
    KMP_COUNT_BLOCK(OMP_SINGLE);
    KMP_PUSH_PARTITIONED_TIMER(OMP_single);
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL
  kmp_info_t *this_thr = __kmp_threads[global_tid];
  kmp_team_t *team = this_thr->th.th_team;
  int tid = __kmp_tid_from_gtid(global_tid);

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

/*!
@ingroup WORK_SHARING
@param loc  source location information
@param global_tid  global thread number

Mark the end of a <tt>single</tt> construct.  This function should
only be called by the thread that executed the block of code protected
by the `single` construct.
*/
void __kmpc_end_single(ident_t *loc, kmp_int32 global_tid) {
  __kmp_exit_single(global_tid);
  KMP_POP_PARTITIONED_TIMER();

#if OMPT_SUPPORT && OMPT_OPTIONAL
  kmp_info_t *this_thr = __kmp_threads[global_tid];
  kmp_team_t *team = this_thr->th.th_team;
  int tid = __kmp_tid_from_gtid(global_tid);

  if (ompt_enabled.ompt_callback_work) {
    ompt_callbacks.ompt_callback(ompt_callback_work)(
        ompt_work_single_executor, ompt_scope_end,
        &(team->t.ompt_team_info.parallel_data),
        &(team->t.t_implicit_task_taskdata[tid].ompt_task_info.task_data), 1,
        OMPT_GET_RETURN_ADDRESS(0));
  }
#endif
}

/*!
@ingroup WORK_SHARING
@param loc Source location
@param global_tid Global thread id

Mark the end of a statically scheduled loop.
*/
void __kmpc_for_static_fini(ident_t *loc, kmp_int32 global_tid) {
  KMP_POP_PARTITIONED_TIMER();
  KE_TRACE(10, ("__kmpc_for_static_fini called T#%d\n", global_tid));

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_work) {
    ompt_work_t ompt_work_type = ompt_work_loop;
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    ompt_task_info_t *task_info = __ompt_get_task_info_object(0);
    // Determine workshare type
    if (loc != NULL) {
      if ((loc->flags & KMP_IDENT_WORK_LOOP) != 0) {
        ompt_work_type = ompt_work_loop;
      } else if ((loc->flags & KMP_IDENT_WORK_SECTIONS) != 0) {
        ompt_work_type = ompt_work_sections;
      } else if ((loc->flags & KMP_IDENT_WORK_DISTRIBUTE) != 0) {
        ompt_work_type = ompt_work_distribute;
      } else {
        // use default set above.
        // a warning about this case is provided in __kmpc_for_static_init
      }
      KMP_DEBUG_ASSERT(ompt_work_type);
    }
    ompt_callbacks.ompt_callback(ompt_callback_work)(
        ompt_work_type, ompt_scope_end, &(team_info->parallel_data),
        &(task_info->task_data), 0, OMPT_GET_RETURN_ADDRESS(0));
  }
#endif
  if (__kmp_env_consistency_check)
    __kmp_pop_workshare(global_tid, ct_pdo, loc);
}

// User routines which take C-style arguments (call by value)
// different from the Fortran equivalent routines

void ompc_set_num_threads(int arg) {
  // !!!!! TODO: check the per-task binding
  __kmp_set_num_threads(arg, __kmp_entry_gtid());
}

void ompc_set_dynamic(int flag) {
  kmp_info_t *thread;

  /* For the thread-private implementation of the internal controls */
  thread = __kmp_entry_thread();

  __kmp_save_internal_controls(thread);

  set__dynamic(thread, flag ? TRUE : FALSE);
}

void ompc_set_nested(int flag) {
  kmp_info_t *thread;

  /* For the thread-private internal controls implementation */
  thread = __kmp_entry_thread();

  __kmp_save_internal_controls(thread);

  set__nested(thread, flag ? TRUE : FALSE);
}

void ompc_set_max_active_levels(int max_active_levels) {
  /* TO DO */
  /* we want per-task implementation of this internal control */

  /* For the per-thread internal controls implementation */
  __kmp_set_max_active_levels(__kmp_entry_gtid(), max_active_levels);
}

void ompc_set_schedule(omp_sched_t kind, int modifier) {
  // !!!!! TODO: check the per-task binding
  __kmp_set_schedule(__kmp_entry_gtid(), (kmp_sched_t)kind, modifier);
}

int ompc_get_ancestor_thread_num(int level) {
  return __kmp_get_ancestor_thread_num(__kmp_entry_gtid(), level);
}

int ompc_get_team_size(int level) {
  return __kmp_get_team_size(__kmp_entry_gtid(), level);
}

#if OMP_50_ENABLED
/* OpenMP 5.0 Affinity Format API */

void ompc_set_affinity_format(char const *format) {
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  __kmp_strncpy_truncate(__kmp_affinity_format, KMP_AFFINITY_FORMAT_SIZE,
                         format, KMP_STRLEN(format) + 1);
}

size_t ompc_get_affinity_format(char *buffer, size_t size) {
  size_t format_size;
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  format_size = KMP_STRLEN(__kmp_affinity_format);
  if (buffer && size) {
    __kmp_strncpy_truncate(buffer, size, __kmp_affinity_format,
                           format_size + 1);
  }
  return format_size;
}

void ompc_display_affinity(char const *format) {
  int gtid;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  gtid = __kmp_get_gtid();
  __kmp_aux_display_affinity(gtid, format);
}

size_t ompc_capture_affinity(char *buffer, size_t buf_size,
                             char const *format) {
  int gtid;
  size_t num_required;
  kmp_str_buf_t capture_buf;
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  gtid = __kmp_get_gtid();
  __kmp_str_buf_init(&capture_buf);
  num_required = __kmp_aux_capture_affinity(gtid, format, &capture_buf);
  if (buffer && buf_size) {
    __kmp_strncpy_truncate(buffer, buf_size, capture_buf.str,
                           capture_buf.used + 1);
  }
  __kmp_str_buf_free(&capture_buf);
  return num_required;
}
#endif /* OMP_50_ENABLED */

void kmpc_set_stacksize(int arg) {
  // __kmp_aux_set_stacksize initializes the library if needed
  __kmp_aux_set_stacksize(arg);
}

void kmpc_set_stacksize_s(size_t arg) {
  // __kmp_aux_set_stacksize initializes the library if needed
  __kmp_aux_set_stacksize(arg);
}

void kmpc_set_blocktime(int arg) {
  int gtid, tid;
  kmp_info_t *thread;

  gtid = __kmp_entry_gtid();
  tid = __kmp_tid_from_gtid(gtid);
  thread = __kmp_thread_from_gtid(gtid);

  __kmp_aux_set_blocktime(arg, thread, tid);
}

void kmpc_set_library(int arg) {
  // __kmp_user_set_library initializes the library if needed
  __kmp_user_set_library((enum library_type)arg);
}

void kmpc_set_defaults(char const *str) {
  // __kmp_aux_set_defaults initializes the library if needed
  __kmp_aux_set_defaults(str, KMP_STRLEN(str));
}

void kmpc_set_disp_num_buffers(int arg) {
  // ignore after initialization because some teams have already
  // allocated dispatch buffers
  if (__kmp_init_serial == 0 && arg > 0)
    __kmp_dispatch_num_buffers = arg;
}

int kmpc_set_affinity_mask_proc(int proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_set_affinity_mask_proc(proc, mask);
#endif
}

int kmpc_unset_affinity_mask_proc(int proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_unset_affinity_mask_proc(proc, mask);
#endif
}

int kmpc_get_affinity_mask_proc(int proc, void **mask) {
#if defined(KMP_STUB) || !KMP_AFFINITY_SUPPORTED
  return -1;
#else
  if (!TCR_4(__kmp_init_middle)) {
    __kmp_middle_initialize();
  }
  return __kmp_aux_get_affinity_mask_proc(proc, mask);
#endif
}

/* -------------------------------------------------------------------------- */
/*!
@ingroup THREADPRIVATE
@param loc       source location information
@param gtid      global thread number
@param cpy_size  size of the cpy_data buffer
@param cpy_data  pointer to data to be copied
@param cpy_func  helper function to call for copying data
@param didit     flag variable: 1=single thread; 0=not single thread

__kmpc_copyprivate implements the interface for the private data broadcast
needed for the copyprivate clause associated with a single region in an
OpenMP<sup>*</sup> program (both C and Fortran).
All threads participating in the parallel region call this routine.
One of the threads (called the single thread) should have the <tt>didit</tt>
variable set to 1 and all other threads should have that variable set to 0.
All threads pass a pointer to a data buffer (cpy_data) that they have built.

The OpenMP specification forbids the use of nowait on the single region when a
copyprivate clause is present. However, @ref __kmpc_copyprivate implements a
barrier internally to avoid race conditions, so the code generation for the
single region should avoid generating a barrier after the call to @ref
__kmpc_copyprivate.

The <tt>gtid</tt> parameter is the global thread id for the current thread.
The <tt>loc</tt> parameter is a pointer to source location information.

Internal implementation: The single thread will first copy its descriptor
address (cpy_data) to a team-private location, then the other threads will each
call the function pointed to by the parameter cpy_func, which carries out the
copy by copying the data using the cpy_data buffer.

The cpy_func routine used for the copy and the contents of the data area defined
by cpy_data and cpy_size may be built in any fashion that will allow the copy
to be done. For instance, the cpy_data buffer can hold the actual data to be
copied or it may hold a list of pointers to the data. The cpy_func routine must
interpret the cpy_data buffer appropriately.

The interface to cpy_func is as follows:
@code
void cpy_func( void *destination, void *source )
@endcode
where void *destination is the cpy_data pointer for the thread being copied to
and void *source is the cpy_data pointer for the thread being copied from.
*/
void __kmpc_copyprivate(ident_t *loc, kmp_int32 gtid, size_t cpy_size,
                        void *cpy_data, void (*cpy_func)(void *, void *),
                        kmp_int32 didit) {
  void **data_ptr;

  KC_TRACE(10, ("__kmpc_copyprivate: called T#%d\n", gtid));

  KMP_MB();

  data_ptr = &__kmp_team_from_gtid(gtid)->t.t_copypriv_data;

  if (__kmp_env_consistency_check) {
    if (loc == 0) {
      KMP_WARNING(ConstructIdentInvalid);
    }
  }

  // ToDo: Optimize the following two barriers into some kind of split barrier

  if (didit)
    *data_ptr = cpy_data;

#if OMPT_SUPPORT
  ompt_frame_t *ompt_frame;
  if (ompt_enabled.enabled) {
    __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
    if (ompt_frame->enter_frame.ptr == NULL)
      ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
/* This barrier is not a barrier region boundary */
#if USE_ITT_NOTIFY
  __kmp_threads[gtid]->th.th_ident = loc;
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);

  if (!didit)
    (*cpy_func)(cpy_data, *data_ptr);

// Consider next barrier a user-visible barrier for barrier region boundaries
// Nesting checks are already handled by the single construct checks

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
    OMPT_STORE_RETURN_ADDRESS(gtid);
  }
#endif
#if USE_ITT_NOTIFY
  __kmp_threads[gtid]->th.th_ident = loc; // TODO: check if it is needed (e.g.
// tasks can overwrite the location)
#endif
  __kmp_barrier(bs_plain_barrier, gtid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    ompt_frame->enter_frame = ompt_data_none;
  }
#endif
}

/* -------------------------------------------------------------------------- */

#define INIT_LOCK __kmp_init_user_lock_with_checks
#define INIT_NESTED_LOCK __kmp_init_nested_user_lock_with_checks
#define ACQUIRE_LOCK __kmp_acquire_user_lock_with_checks
#define ACQUIRE_LOCK_TIMED __kmp_acquire_user_lock_with_checks_timed
#define ACQUIRE_NESTED_LOCK __kmp_acquire_nested_user_lock_with_checks
#define ACQUIRE_NESTED_LOCK_TIMED                                              \
  __kmp_acquire_nested_user_lock_with_checks_timed
#define RELEASE_LOCK __kmp_release_user_lock_with_checks
#define RELEASE_NESTED_LOCK __kmp_release_nested_user_lock_with_checks
#define TEST_LOCK __kmp_test_user_lock_with_checks
#define TEST_NESTED_LOCK __kmp_test_nested_user_lock_with_checks
#define DESTROY_LOCK __kmp_destroy_user_lock_with_checks
#define DESTROY_NESTED_LOCK __kmp_destroy_nested_user_lock_with_checks

// TODO: Make check abort messages use location info & pass it into
// with_checks routines

#if KMP_USE_DYNAMIC_LOCK

// internal lock initializer
static __forceinline void __kmp_init_lock_with_hint(ident_t *loc, void **lock,
                                                    kmp_dyna_lockseq_t seq) {
  if (KMP_IS_D_LOCK(seq)) {
    KMP_INIT_D_LOCK(lock, seq);
#if USE_ITT_BUILD
    __kmp_itt_lock_creating((kmp_user_lock_p)lock, NULL);
#endif
  } else {
    KMP_INIT_I_LOCK(lock, seq);
#if USE_ITT_BUILD
    kmp_indirect_lock_t *ilk = KMP_LOOKUP_I_LOCK(lock);
    __kmp_itt_lock_creating(ilk->lock, loc);
#endif
  }
}

// internal nest lock initializer
static __forceinline void
__kmp_init_nest_lock_with_hint(ident_t *loc, void **lock,
                               kmp_dyna_lockseq_t seq) {
#if KMP_USE_TSX
  // Don't have nested lock implementation for speculative locks
  if (seq == lockseq_hle || seq == lockseq_rtm || seq == lockseq_adaptive)
    seq = __kmp_user_lock_seq;
#endif
  switch (seq) {
  case lockseq_tas:
    seq = lockseq_nested_tas;
    break;
#if KMP_USE_FUTEX
  case lockseq_futex:
    seq = lockseq_nested_futex;
    break;
#endif
  case lockseq_ticket:
    seq = lockseq_nested_ticket;
    break;
  case lockseq_queuing:
    seq = lockseq_nested_queuing;
    break;
  case lockseq_drdpa:
    seq = lockseq_nested_drdpa;
    break;
  default:
    seq = lockseq_nested_queuing;
  }
  KMP_INIT_I_LOCK(lock, seq);
#if USE_ITT_BUILD
  kmp_indirect_lock_t *ilk = KMP_LOOKUP_I_LOCK(lock);
  __kmp_itt_lock_creating(ilk->lock, loc);
#endif
}

/* initialize the lock with a hint */
void __kmpc_init_lock_with_hint(ident_t *loc, kmp_int32 gtid, void **user_lock,
                                uintptr_t hint) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  if (__kmp_env_consistency_check && user_lock == NULL) {
    KMP_FATAL(LockIsUninitialized, "omp_init_lock_with_hint");
  }

  __kmp_init_lock_with_hint(loc, user_lock, __kmp_map_hint_to_lock(hint));

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_lock, (omp_lock_hint_t)hint,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif
}

/* initialize the lock with a hint */
void __kmpc_init_nest_lock_with_hint(ident_t *loc, kmp_int32 gtid,
                                     void **user_lock, uintptr_t hint) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  if (__kmp_env_consistency_check && user_lock == NULL) {
    KMP_FATAL(LockIsUninitialized, "omp_init_nest_lock_with_hint");
  }

  __kmp_init_nest_lock_with_hint(loc, user_lock, __kmp_map_hint_to_lock(hint));

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_nest_lock, (omp_lock_hint_t)hint,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif
}

#endif // KMP_USE_DYNAMIC_LOCK

/* initialize the lock */
void __kmpc_init_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

  KMP_DEBUG_ASSERT(__kmp_init_serial);
  if (__kmp_env_consistency_check && user_lock == NULL) {
    KMP_FATAL(LockIsUninitialized, "omp_init_lock");
  }
  __kmp_init_lock_with_hint(loc, user_lock, __kmp_user_lock_seq);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_lock, omp_lock_hint_none,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK

  static char const *const func = "omp_init_lock";
  kmp_user_lock_p lck;
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  if (__kmp_env_consistency_check) {
    if (user_lock == NULL) {
      KMP_FATAL(LockIsUninitialized, func);
    }
  }

  KMP_CHECK_USER_LOCK_INIT();

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_user_lock_allocate(user_lock, gtid, 0);
  }
  INIT_LOCK(lck);
  __kmp_set_user_lock_location(lck, loc);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_lock, omp_lock_hint_none, __ompt_get_mutex_impl_type(),
        (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#if USE_ITT_BUILD
  __kmp_itt_lock_creating(lck);
#endif /* USE_ITT_BUILD */

#endif // KMP_USE_DYNAMIC_LOCK
} // __kmpc_init_lock

/* initialize the lock */
void __kmpc_init_nest_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

  KMP_DEBUG_ASSERT(__kmp_init_serial);
  if (__kmp_env_consistency_check && user_lock == NULL) {
    KMP_FATAL(LockIsUninitialized, "omp_init_nest_lock");
  }
  __kmp_init_nest_lock_with_hint(loc, user_lock, __kmp_user_lock_seq);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_nest_lock, omp_lock_hint_none,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK

  static char const *const func = "omp_init_nest_lock";
  kmp_user_lock_p lck;
  KMP_DEBUG_ASSERT(__kmp_init_serial);

  if (__kmp_env_consistency_check) {
    if (user_lock == NULL) {
      KMP_FATAL(LockIsUninitialized, func);
    }
  }

  KMP_CHECK_USER_LOCK_INIT();

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_user_lock_allocate(user_lock, gtid, 0);
  }

  INIT_NESTED_LOCK(lck);
  __kmp_set_user_lock_location(lck, loc);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_init) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_init)(
        ompt_mutex_nest_lock, omp_lock_hint_none, __ompt_get_mutex_impl_type(),
        (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#if USE_ITT_BUILD
  __kmp_itt_lock_creating(lck);
#endif /* USE_ITT_BUILD */

#endif // KMP_USE_DYNAMIC_LOCK
} // __kmpc_init_nest_lock

void __kmpc_destroy_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

#if USE_ITT_BUILD
  kmp_user_lock_p lck;
  if (KMP_EXTRACT_D_TAG(user_lock) == 0) {
    lck = ((kmp_indirect_lock_t *)KMP_LOOKUP_I_LOCK(user_lock))->lock;
  } else {
    lck = (kmp_user_lock_p)user_lock;
  }
  __kmp_itt_lock_destroyed(lck);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_destroy) {
    kmp_user_lock_p lck;
    if (KMP_EXTRACT_D_TAG(user_lock) == 0) {
      lck = ((kmp_indirect_lock_t *)KMP_LOOKUP_I_LOCK(user_lock))->lock;
    } else {
      lck = (kmp_user_lock_p)user_lock;
    }
    ompt_callbacks.ompt_callback(ompt_callback_lock_destroy)(
        ompt_mutex_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif
  KMP_D_LOCK_FUNC(user_lock, destroy)((kmp_dyna_lock_t *)user_lock);
#else
  kmp_user_lock_p lck;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_destroy_lock");
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_destroy) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_destroy)(
        ompt_mutex_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#if USE_ITT_BUILD
  __kmp_itt_lock_destroyed(lck);
#endif /* USE_ITT_BUILD */
  DESTROY_LOCK(lck);

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
    ;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    ;
  }
#endif
  else {
    __kmp_user_lock_free(user_lock, gtid, lck);
  }
#endif // KMP_USE_DYNAMIC_LOCK
} // __kmpc_destroy_lock

/* destroy the lock */
void __kmpc_destroy_nest_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

#if USE_ITT_BUILD
  kmp_indirect_lock_t *ilk = KMP_LOOKUP_I_LOCK(user_lock);
  __kmp_itt_lock_destroyed(ilk->lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_destroy) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_destroy)(
        ompt_mutex_nest_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif
  KMP_D_LOCK_FUNC(user_lock, destroy)((kmp_dyna_lock_t *)user_lock);

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_destroy_nest_lock");
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_lock_destroy) {
    ompt_callbacks.ompt_callback(ompt_callback_lock_destroy)(
        ompt_mutex_nest_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#if USE_ITT_BUILD
  __kmp_itt_lock_destroyed(lck);
#endif /* USE_ITT_BUILD */

  DESTROY_NESTED_LOCK(lck);

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
    ;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    ;
  }
#endif
  else {
    __kmp_user_lock_free(user_lock, gtid, lck);
  }
#endif // KMP_USE_DYNAMIC_LOCK
} // __kmpc_destroy_nest_lock

void __kmpc_set_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
  KMP_COUNT_BLOCK(OMP_set_lock);
#if KMP_USE_DYNAMIC_LOCK
  int tag = KMP_EXTRACT_D_TAG(user_lock);
#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring(
      (kmp_user_lock_p)
          user_lock); // itt function will get to the right lock object.
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_lock, omp_lock_hint_none,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif
#if KMP_USE_INLINED_TAS
  if (tag == locktag_tas && !__kmp_env_consistency_check) {
    KMP_ACQUIRE_TAS_LOCK(user_lock, gtid);
  } else
#elif KMP_USE_INLINED_FUTEX
  if (tag == locktag_futex && !__kmp_env_consistency_check) {
    KMP_ACQUIRE_FUTEX_LOCK(user_lock, gtid);
  } else
#endif
  {
    __kmp_direct_set[tag]((kmp_dyna_lock_t *)user_lock, gtid);
  }
#if USE_ITT_BUILD
  __kmp_itt_lock_acquired((kmp_user_lock_p)user_lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_mutex_acquired) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
        ompt_mutex_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_set_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_lock, omp_lock_hint_none, __ompt_get_mutex_impl_type(),
        (ompt_wait_id_t)lck, codeptr);
  }
#endif

  ACQUIRE_LOCK(lck, gtid);

#if USE_ITT_BUILD
  __kmp_itt_lock_acquired(lck);
#endif /* USE_ITT_BUILD */

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_mutex_acquired) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
        ompt_mutex_lock, (ompt_wait_id_t)lck, codeptr);
  }
#endif

#endif // KMP_USE_DYNAMIC_LOCK
}

void __kmpc_set_nest_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring((kmp_user_lock_p)user_lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.enabled) {
    if (ompt_enabled.ompt_callback_mutex_acquire) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
          ompt_mutex_nest_lock, omp_lock_hint_none,
          __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
          codeptr);
    }
  }
#endif
  int acquire_status =
      KMP_D_LOCK_FUNC(user_lock, set)((kmp_dyna_lock_t *)user_lock, gtid);
  (void) acquire_status;
#if USE_ITT_BUILD
  __kmp_itt_lock_acquired((kmp_user_lock_p)user_lock);
#endif

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    if (acquire_status == KMP_LOCK_ACQUIRED_FIRST) {
      if (ompt_enabled.ompt_callback_mutex_acquired) {
        // lock_first
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)user_lock, codeptr);
      }
    } else {
      if (ompt_enabled.ompt_callback_nest_lock) {
        // lock_next
        ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
            ompt_scope_begin, (ompt_wait_id_t)user_lock, codeptr);
      }
    }
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK
  int acquire_status;
  kmp_user_lock_p lck;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_set_nest_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.enabled) {
    if (ompt_enabled.ompt_callback_mutex_acquire) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
          ompt_mutex_nest_lock, omp_lock_hint_none,
          __ompt_get_mutex_impl_type(), (ompt_wait_id_t)lck, codeptr);
    }
  }
#endif

  ACQUIRE_NESTED_LOCK(lck, gtid, &acquire_status);

#if USE_ITT_BUILD
  __kmp_itt_lock_acquired(lck);
#endif /* USE_ITT_BUILD */

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled) {
    if (acquire_status == KMP_LOCK_ACQUIRED_FIRST) {
      if (ompt_enabled.ompt_callback_mutex_acquired) {
        // lock_first
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)lck, codeptr);
      }
    } else {
      if (ompt_enabled.ompt_callback_nest_lock) {
        // lock_next
        ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
            ompt_scope_begin, (ompt_wait_id_t)lck, codeptr);
      }
    }
  }
#endif

#endif // KMP_USE_DYNAMIC_LOCK
}

void __kmpc_unset_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

  int tag = KMP_EXTRACT_D_TAG(user_lock);
#if USE_ITT_BUILD
  __kmp_itt_lock_releasing((kmp_user_lock_p)user_lock);
#endif
#if KMP_USE_INLINED_TAS
  if (tag == locktag_tas && !__kmp_env_consistency_check) {
    KMP_RELEASE_TAS_LOCK(user_lock, gtid);
  } else
#elif KMP_USE_INLINED_FUTEX
  if (tag == locktag_futex && !__kmp_env_consistency_check) {
    KMP_RELEASE_FUTEX_LOCK(user_lock, gtid);
  } else
#endif
  {
    __kmp_direct_unset[tag]((kmp_dyna_lock_t *)user_lock, gtid);
  }

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_released) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
        ompt_mutex_lock, (ompt_wait_id_t)user_lock, codeptr);
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;

  /* Can't use serial interval since not block structured */
  /* release the lock */

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
#if KMP_OS_LINUX &&                                                            \
    (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64)
// "fast" path implemented to fix customer performance issue
#if USE_ITT_BUILD
    __kmp_itt_lock_releasing((kmp_user_lock_p)user_lock);
#endif /* USE_ITT_BUILD */
    TCW_4(((kmp_user_lock_p)user_lock)->tas.lk.poll, 0);
    KMP_MB();

#if OMPT_SUPPORT && OMPT_OPTIONAL
    // This is the case, if called from omp_init_lock_with_hint:
    void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
    if (!codeptr)
      codeptr = OMPT_GET_RETURN_ADDRESS(0);
    if (ompt_enabled.ompt_callback_mutex_released) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
          ompt_mutex_lock, (ompt_wait_id_t)lck, codeptr);
    }
#endif

    return;
#else
    lck = (kmp_user_lock_p)user_lock;
#endif
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_unset_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_releasing(lck);
#endif /* USE_ITT_BUILD */

  RELEASE_LOCK(lck, gtid);

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_released) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
        ompt_mutex_lock, (ompt_wait_id_t)lck, codeptr);
  }
#endif

#endif // KMP_USE_DYNAMIC_LOCK
}

/* release the lock */
void __kmpc_unset_nest_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK

#if USE_ITT_BUILD
  __kmp_itt_lock_releasing((kmp_user_lock_p)user_lock);
#endif
  int release_status =
      KMP_D_LOCK_FUNC(user_lock, unset)((kmp_dyna_lock_t *)user_lock, gtid);
  (void) release_status;

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.enabled) {
    if (release_status == KMP_LOCK_RELEASED) {
      if (ompt_enabled.ompt_callback_mutex_released) {
        // release_lock_last
        ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)user_lock, codeptr);
      }
    } else if (ompt_enabled.ompt_callback_nest_lock) {
      // release_lock_prev
      ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
          ompt_scope_end, (ompt_wait_id_t)user_lock, codeptr);
    }
  }
#endif

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;

  /* Can't use serial interval since not block structured */

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
#if KMP_OS_LINUX &&                                                            \
    (KMP_ARCH_X86 || KMP_ARCH_X86_64 || KMP_ARCH_ARM || KMP_ARCH_AARCH64)
    // "fast" path implemented to fix customer performance issue
    kmp_tas_lock_t *tl = (kmp_tas_lock_t *)user_lock;
#if USE_ITT_BUILD
    __kmp_itt_lock_releasing((kmp_user_lock_p)user_lock);
#endif /* USE_ITT_BUILD */

#if OMPT_SUPPORT && OMPT_OPTIONAL
    int release_status = KMP_LOCK_STILL_HELD;
#endif

    if (--(tl->lk.depth_locked) == 0) {
      TCW_4(tl->lk.poll, 0);
#if OMPT_SUPPORT && OMPT_OPTIONAL
      release_status = KMP_LOCK_RELEASED;
#endif
    }
    KMP_MB();

#if OMPT_SUPPORT && OMPT_OPTIONAL
    // This is the case, if called from omp_init_lock_with_hint:
    void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
    if (!codeptr)
      codeptr = OMPT_GET_RETURN_ADDRESS(0);
    if (ompt_enabled.enabled) {
      if (release_status == KMP_LOCK_RELEASED) {
        if (ompt_enabled.ompt_callback_mutex_released) {
          // release_lock_last
          ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
              ompt_mutex_nest_lock, (ompt_wait_id_t)lck, codeptr);
        }
      } else if (ompt_enabled.ompt_callback_nest_lock) {
        // release_lock_previous
        ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
            ompt_mutex_scope_end, (ompt_wait_id_t)lck, codeptr);
      }
    }
#endif

    return;
#else
    lck = (kmp_user_lock_p)user_lock;
#endif
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_unset_nest_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_releasing(lck);
#endif /* USE_ITT_BUILD */

  int release_status;
  release_status = RELEASE_NESTED_LOCK(lck, gtid);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.enabled) {
    if (release_status == KMP_LOCK_RELEASED) {
      if (ompt_enabled.ompt_callback_mutex_released) {
        // release_lock_last
        ompt_callbacks.ompt_callback(ompt_callback_mutex_released)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)lck, codeptr);
      }
    } else if (ompt_enabled.ompt_callback_nest_lock) {
      // release_lock_previous
      ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
          ompt_mutex_scope_end, (ompt_wait_id_t)lck, codeptr);
    }
  }
#endif

#endif // KMP_USE_DYNAMIC_LOCK
}

/* try to acquire the lock */
int __kmpc_test_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
  KMP_COUNT_BLOCK(OMP_test_lock);

#if KMP_USE_DYNAMIC_LOCK
  int rc;
  int tag = KMP_EXTRACT_D_TAG(user_lock);
#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring((kmp_user_lock_p)user_lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_lock, omp_lock_hint_none,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif
#if KMP_USE_INLINED_TAS
  if (tag == locktag_tas && !__kmp_env_consistency_check) {
    KMP_TEST_TAS_LOCK(user_lock, gtid, rc);
  } else
#elif KMP_USE_INLINED_FUTEX
  if (tag == locktag_futex && !__kmp_env_consistency_check) {
    KMP_TEST_FUTEX_LOCK(user_lock, gtid, rc);
  } else
#endif
  {
    rc = __kmp_direct_test[tag]((kmp_dyna_lock_t *)user_lock, gtid);
  }
  if (rc) {
#if USE_ITT_BUILD
    __kmp_itt_lock_acquired((kmp_user_lock_p)user_lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.ompt_callback_mutex_acquired) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
          ompt_mutex_lock, (ompt_wait_id_t)user_lock, codeptr);
    }
#endif
    return FTN_TRUE;
  } else {
#if USE_ITT_BUILD
    __kmp_itt_lock_cancelled((kmp_user_lock_p)user_lock);
#endif
    return FTN_FALSE;
  }

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;
  int rc;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) <= OMP_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_test_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring(lck);
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_lock, omp_lock_hint_none, __ompt_get_mutex_impl_type(),
        (ompt_wait_id_t)lck, codeptr);
  }
#endif

  rc = TEST_LOCK(lck, gtid);
#if USE_ITT_BUILD
  if (rc) {
    __kmp_itt_lock_acquired(lck);
  } else {
    __kmp_itt_lock_cancelled(lck);
  }
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (rc && ompt_enabled.ompt_callback_mutex_acquired) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
        ompt_mutex_lock, (ompt_wait_id_t)lck, codeptr);
  }
#endif

  return (rc ? FTN_TRUE : FTN_FALSE);

/* Can't use serial interval since not block structured */

#endif // KMP_USE_DYNAMIC_LOCK
}

/* try to acquire the lock */
int __kmpc_test_nest_lock(ident_t *loc, kmp_int32 gtid, void **user_lock) {
#if KMP_USE_DYNAMIC_LOCK
  int rc;
#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring((kmp_user_lock_p)user_lock);
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.ompt_callback_mutex_acquire) {
    ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
        ompt_mutex_nest_lock, omp_lock_hint_none,
        __ompt_get_mutex_impl_type(user_lock), (ompt_wait_id_t)user_lock,
        codeptr);
  }
#endif
  rc = KMP_D_LOCK_FUNC(user_lock, test)((kmp_dyna_lock_t *)user_lock, gtid);
#if USE_ITT_BUILD
  if (rc) {
    __kmp_itt_lock_acquired((kmp_user_lock_p)user_lock);
  } else {
    __kmp_itt_lock_cancelled((kmp_user_lock_p)user_lock);
  }
#endif
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled && rc) {
    if (rc == 1) {
      if (ompt_enabled.ompt_callback_mutex_acquired) {
        // lock_first
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)user_lock, codeptr);
      }
    } else {
      if (ompt_enabled.ompt_callback_nest_lock) {
        // lock_next
        ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
            ompt_scope_begin, (ompt_wait_id_t)user_lock, codeptr);
      }
    }
  }
#endif
  return rc;

#else // KMP_USE_DYNAMIC_LOCK

  kmp_user_lock_p lck;
  int rc;

  if ((__kmp_user_lock_kind == lk_tas) &&
      (sizeof(lck->tas.lk.poll) + sizeof(lck->tas.lk.depth_locked) <=
       OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#if KMP_USE_FUTEX
  else if ((__kmp_user_lock_kind == lk_futex) &&
           (sizeof(lck->futex.lk.poll) + sizeof(lck->futex.lk.depth_locked) <=
            OMP_NEST_LOCK_T_SIZE)) {
    lck = (kmp_user_lock_p)user_lock;
  }
#endif
  else {
    lck = __kmp_lookup_user_lock(user_lock, "omp_test_nest_lock");
  }

#if USE_ITT_BUILD
  __kmp_itt_lock_acquiring(lck);
#endif /* USE_ITT_BUILD */

#if OMPT_SUPPORT && OMPT_OPTIONAL
  // This is the case, if called from omp_init_lock_with_hint:
  void *codeptr = OMPT_LOAD_RETURN_ADDRESS(gtid);
  if (!codeptr)
    codeptr = OMPT_GET_RETURN_ADDRESS(0);
  if (ompt_enabled.enabled) &&
        ompt_enabled.ompt_callback_mutex_acquire) {
      ompt_callbacks.ompt_callback(ompt_callback_mutex_acquire)(
          ompt_mutex_nest_lock, omp_lock_hint_none,
          __ompt_get_mutex_impl_type(), (ompt_wait_id_t)lck, codeptr);
    }
#endif

  rc = TEST_NESTED_LOCK(lck, gtid);
#if USE_ITT_BUILD
  if (rc) {
    __kmp_itt_lock_acquired(lck);
  } else {
    __kmp_itt_lock_cancelled(lck);
  }
#endif /* USE_ITT_BUILD */
#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.enabled && rc) {
    if (rc == 1) {
      if (ompt_enabled.ompt_callback_mutex_acquired) {
        // lock_first
        ompt_callbacks.ompt_callback(ompt_callback_mutex_acquired)(
            ompt_mutex_nest_lock, (ompt_wait_id_t)lck, codeptr);
      }
    } else {
      if (ompt_enabled.ompt_callback_nest_lock) {
        // lock_next
        ompt_callbacks.ompt_callback(ompt_callback_nest_lock)(
            ompt_mutex_scope_begin, (ompt_wait_id_t)lck, codeptr);
      }
    }
  }
#endif
  return rc;

/* Can't use serial interval since not block structured */

#endif // KMP_USE_DYNAMIC_LOCK
}

// Interface to fast scalable reduce methods routines

// keep the selected method in a thread local structure for cross-function
// usage: will be used in __kmpc_end_reduce* functions;
// another solution: to re-determine the method one more time in
// __kmpc_end_reduce* functions (new prototype required then)
// AT: which solution is better?
#define __KMP_SET_REDUCTION_METHOD(gtid, rmethod)                              \
  ((__kmp_threads[(gtid)]->th.th_local.packed_reduction_method) = (rmethod))

#define __KMP_GET_REDUCTION_METHOD(gtid)                                       \
  (__kmp_threads[(gtid)]->th.th_local.packed_reduction_method)

// description of the packed_reduction_method variable: look at the macros in
// kmp.h

// used in a critical section reduce block
static __forceinline void
__kmp_enter_critical_section_reduce_block(ident_t *loc, kmp_int32 global_tid,
                                          kmp_critical_name *crit) {

  // this lock was visible to a customer and to the threading profile tool as a
  // serial overhead span (although it's used for an internal purpose only)
  //            why was it visible in previous implementation?
  //            should we keep it visible in new reduce block?
  kmp_user_lock_p lck;

#if KMP_USE_DYNAMIC_LOCK

  kmp_dyna_lock_t *lk = (kmp_dyna_lock_t *)crit;
  // Check if it is initialized.
  if (*lk == 0) {
    if (KMP_IS_D_LOCK(__kmp_user_lock_seq)) {
      KMP_COMPARE_AND_STORE_ACQ32((volatile kmp_int32 *)crit, 0,
                                  KMP_GET_D_TAG(__kmp_user_lock_seq));
    } else {
      __kmp_init_indirect_csptr(crit, loc, global_tid,
                                KMP_GET_I_TAG(__kmp_user_lock_seq));
    }
  }
  // Branch for accessing the actual lock object and set operation. This
  // branching is inevitable since this lock initialization does not follow the
  // normal dispatch path (lock table is not used).
  if (KMP_EXTRACT_D_TAG(lk) != 0) {
    lck = (kmp_user_lock_p)lk;
    KMP_DEBUG_ASSERT(lck != NULL);
    if (__kmp_env_consistency_check) {
      __kmp_push_sync(global_tid, ct_critical, loc, lck, __kmp_user_lock_seq);
    }
    KMP_D_LOCK_FUNC(lk, set)(lk, global_tid);
  } else {
    kmp_indirect_lock_t *ilk = *((kmp_indirect_lock_t **)lk);
    lck = ilk->lock;
    KMP_DEBUG_ASSERT(lck != NULL);
    if (__kmp_env_consistency_check) {
      __kmp_push_sync(global_tid, ct_critical, loc, lck, __kmp_user_lock_seq);
    }
    KMP_I_LOCK_FUNC(ilk, set)(lck, global_tid);
  }

#else // KMP_USE_DYNAMIC_LOCK

  // We know that the fast reduction code is only emitted by Intel compilers
  // with 32 byte critical sections. If there isn't enough space, then we
  // have to use a pointer.
  if (__kmp_base_user_lock_size <= INTEL_CRITICAL_SIZE) {
    lck = (kmp_user_lock_p)crit;
  } else {
    lck = __kmp_get_critical_section_ptr(crit, loc, global_tid);
  }
  KMP_DEBUG_ASSERT(lck != NULL);

  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_critical, loc, lck);

  __kmp_acquire_user_lock_with_checks(lck, global_tid);

#endif // KMP_USE_DYNAMIC_LOCK
}

// used in a critical section reduce block
static __forceinline void
__kmp_end_critical_section_reduce_block(ident_t *loc, kmp_int32 global_tid,
                                        kmp_critical_name *crit) {

  kmp_user_lock_p lck;

#if KMP_USE_DYNAMIC_LOCK

  if (KMP_IS_D_LOCK(__kmp_user_lock_seq)) {
    lck = (kmp_user_lock_p)crit;
    if (__kmp_env_consistency_check)
      __kmp_pop_sync(global_tid, ct_critical, loc);
    KMP_D_LOCK_FUNC(lck, unset)((kmp_dyna_lock_t *)lck, global_tid);
  } else {
    kmp_indirect_lock_t *ilk =
        (kmp_indirect_lock_t *)TCR_PTR(*((kmp_indirect_lock_t **)crit));
    if (__kmp_env_consistency_check)
      __kmp_pop_sync(global_tid, ct_critical, loc);
    KMP_I_LOCK_FUNC(ilk, unset)(ilk->lock, global_tid);
  }

#else // KMP_USE_DYNAMIC_LOCK

  // We know that the fast reduction code is only emitted by Intel compilers
  // with 32 byte critical sections. If there isn't enough space, then we have
  // to use a pointer.
  if (__kmp_base_user_lock_size > 32) {
    lck = *((kmp_user_lock_p *)crit);
    KMP_ASSERT(lck != NULL);
  } else {
    lck = (kmp_user_lock_p)crit;
  }

  if (__kmp_env_consistency_check)
    __kmp_pop_sync(global_tid, ct_critical, loc);

  __kmp_release_user_lock_with_checks(lck, global_tid);

#endif // KMP_USE_DYNAMIC_LOCK
} // __kmp_end_critical_section_reduce_block

#if OMP_40_ENABLED
static __forceinline int
__kmp_swap_teams_for_teams_reduction(kmp_info_t *th, kmp_team_t **team_p,
                                     int *task_state) {
  kmp_team_t *team;

  // Check if we are inside the teams construct?
  if (th->th.th_teams_microtask) {
    *team_p = team = th->th.th_team;
    if (team->t.t_level == th->th.th_teams_level) {
      // This is reduction at teams construct.
      KMP_DEBUG_ASSERT(!th->th.th_info.ds.ds_tid); // AC: check that tid == 0
      // Let's swap teams temporarily for the reduction.
      th->th.th_info.ds.ds_tid = team->t.t_master_tid;
      th->th.th_team = team->t.t_parent;
      th->th.th_team_nproc = th->th.th_team->t.t_nproc;
      th->th.th_task_team = th->th.th_team->t.t_task_team[0];
      *task_state = th->th.th_task_state;
      th->th.th_task_state = 0;

      return 1;
    }
  }
  return 0;
}

static __forceinline void
__kmp_restore_swapped_teams(kmp_info_t *th, kmp_team_t *team, int task_state) {
  // Restore thread structure swapped in __kmp_swap_teams_for_teams_reduction.
  th->th.th_info.ds.ds_tid = 0;
  th->th.th_team = team;
  th->th.th_team_nproc = team->t.t_nproc;
  th->th.th_task_team = team->t.t_task_team[task_state];
  th->th.th_task_state = task_state;
}
#endif

/* 2.a.i. Reduce Block without a terminating barrier */
/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid global thread number
@param num_vars number of items (variables) to be reduced
@param reduce_size size of data in bytes to be reduced
@param reduce_data pointer to data to be reduced
@param reduce_func callback function providing reduction operation on two
operands and returning result of reduction in lhs_data
@param lck pointer to the unique lock data structure
@result 1 for the master thread, 0 for all other team threads, 2 for all team
threads if atomic reduction needed

The nowait version is used for a reduce clause with the nowait argument.
*/
kmp_int32
__kmpc_reduce_nowait(ident_t *loc, kmp_int32 global_tid, kmp_int32 num_vars,
                     size_t reduce_size, void *reduce_data,
                     void (*reduce_func)(void *lhs_data, void *rhs_data),
                     kmp_critical_name *lck) {

  KMP_COUNT_BLOCK(REDUCE_nowait);
  int retval = 0;
  PACKED_REDUCTION_METHOD_T packed_reduction_method;
#if OMP_40_ENABLED
  kmp_info_t *th;
  kmp_team_t *team;
  int teams_swapped = 0, task_state;
#endif
  KA_TRACE(10, ("__kmpc_reduce_nowait() enter: called T#%d\n", global_tid));

  // why do we need this initialization here at all?
  // Reduction clause can not be used as a stand-alone directive.

  // do not call __kmp_serial_initialize(), it will be called by
  // __kmp_parallel_initialize() if needed
  // possible detection of false-positive race by the threadchecker ???
  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

// check correctness of reduce block nesting
#if KMP_USE_DYNAMIC_LOCK
  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_reduce, loc, NULL, 0);
#else
  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_reduce, loc, NULL);
#endif

#if OMP_40_ENABLED
  th = __kmp_thread_from_gtid(global_tid);
  teams_swapped = __kmp_swap_teams_for_teams_reduction(th, &team, &task_state);
#endif // OMP_40_ENABLED

  // packed_reduction_method value will be reused by __kmp_end_reduce* function,
  // the value should be kept in a variable
  // the variable should be either a construct-specific or thread-specific
  // property, not a team specific property
  //     (a thread can reach the next reduce block on the next construct, reduce
  //     method may differ on the next construct)
  // an ident_t "loc" parameter could be used as a construct-specific property
  // (what if loc == 0?)
  //     (if both construct-specific and team-specific variables were shared,
  //     then unness extra syncs should be needed)
  // a thread-specific variable is better regarding two issues above (next
  // construct and extra syncs)
  // a thread-specific "th_local.reduction_method" variable is used currently
  // each thread executes 'determine' and 'set' lines (no need to execute by one
  // thread, to avoid unness extra syncs)

  packed_reduction_method = __kmp_determine_reduction_method(
      loc, global_tid, num_vars, reduce_size, reduce_data, reduce_func, lck);
  __KMP_SET_REDUCTION_METHOD(global_tid, packed_reduction_method);

  if (packed_reduction_method == critical_reduce_block) {

    __kmp_enter_critical_section_reduce_block(loc, global_tid, lck);
    retval = 1;

  } else if (packed_reduction_method == empty_reduce_block) {

    // usage: if team size == 1, no synchronization is required ( Intel
    // platforms only )
    retval = 1;

  } else if (packed_reduction_method == atomic_reduce_block) {

    retval = 2;

    // all threads should do this pop here (because __kmpc_end_reduce_nowait()
    // won't be called by the code gen)
    //     (it's not quite good, because the checking block has been closed by
    //     this 'pop',
    //      but atomic operation has not been executed yet, will be executed
    //      slightly later, literally on next instruction)
    if (__kmp_env_consistency_check)
      __kmp_pop_sync(global_tid, ct_reduce, loc);

  } else if (TEST_REDUCTION_METHOD(packed_reduction_method,
                                   tree_reduce_block)) {

// AT: performance issue: a real barrier here
// AT:     (if master goes slow, other threads are blocked here waiting for the
// master to come and release them)
// AT:     (it's not what a customer might expect specifying NOWAIT clause)
// AT:     (specifying NOWAIT won't result in improvement of performance, it'll
// be confusing to a customer)
// AT: another implementation of *barrier_gather*nowait() (or some other design)
// might go faster and be more in line with sense of NOWAIT
// AT: TO DO: do epcc test and compare times

// this barrier should be invisible to a customer and to the threading profile
// tool (it's neither a terminating barrier nor customer's code, it's
// used for an internal purpose)
#if OMPT_SUPPORT
    // JP: can this barrier potentially leed to task scheduling?
    // JP: as long as there is a barrier in the implementation, OMPT should and
    // will provide the barrier events
    //         so we set-up the necessary frame/return addresses.
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
      if (ompt_frame->enter_frame.ptr == NULL)
        ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(global_tid);
    }
#endif
#if USE_ITT_NOTIFY
    __kmp_threads[global_tid]->th.th_ident = loc;
#endif
    retval =
        __kmp_barrier(UNPACK_REDUCTION_BARRIER(packed_reduction_method),
                      global_tid, FALSE, reduce_size, reduce_data, reduce_func);
    retval = (retval != 0) ? (0) : (1);
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ompt_frame->enter_frame = ompt_data_none;
    }
#endif

    // all other workers except master should do this pop here
    //     ( none of other workers will get to __kmpc_end_reduce_nowait() )
    if (__kmp_env_consistency_check) {
      if (retval == 0) {
        __kmp_pop_sync(global_tid, ct_reduce, loc);
      }
    }

  } else {

    // should never reach this block
    KMP_ASSERT(0); // "unexpected method"
  }
#if OMP_40_ENABLED
  if (teams_swapped) {
    __kmp_restore_swapped_teams(th, team, task_state);
  }
#endif
  KA_TRACE(
      10,
      ("__kmpc_reduce_nowait() exit: called T#%d: method %08x, returns %08x\n",
       global_tid, packed_reduction_method, retval));

  return retval;
}

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid global thread id.
@param lck pointer to the unique lock data structure

Finish the execution of a reduce nowait.
*/
void __kmpc_end_reduce_nowait(ident_t *loc, kmp_int32 global_tid,
                              kmp_critical_name *lck) {

  PACKED_REDUCTION_METHOD_T packed_reduction_method;

  KA_TRACE(10, ("__kmpc_end_reduce_nowait() enter: called T#%d\n", global_tid));

  packed_reduction_method = __KMP_GET_REDUCTION_METHOD(global_tid);

  if (packed_reduction_method == critical_reduce_block) {

    __kmp_end_critical_section_reduce_block(loc, global_tid, lck);

  } else if (packed_reduction_method == empty_reduce_block) {

    // usage: if team size == 1, no synchronization is required ( on Intel
    // platforms only )

  } else if (packed_reduction_method == atomic_reduce_block) {

    // neither master nor other workers should get here
    //     (code gen does not generate this call in case 2: atomic reduce block)
    // actually it's better to remove this elseif at all;
    // after removal this value will checked by the 'else' and will assert

  } else if (TEST_REDUCTION_METHOD(packed_reduction_method,
                                   tree_reduce_block)) {

    // only master gets here

  } else {

    // should never reach this block
    KMP_ASSERT(0); // "unexpected method"
  }

  if (__kmp_env_consistency_check)
    __kmp_pop_sync(global_tid, ct_reduce, loc);

  KA_TRACE(10, ("__kmpc_end_reduce_nowait() exit: called T#%d: method %08x\n",
                global_tid, packed_reduction_method));

  return;
}

/* 2.a.ii. Reduce Block with a terminating barrier */

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid global thread number
@param num_vars number of items (variables) to be reduced
@param reduce_size size of data in bytes to be reduced
@param reduce_data pointer to data to be reduced
@param reduce_func callback function providing reduction operation on two
operands and returning result of reduction in lhs_data
@param lck pointer to the unique lock data structure
@result 1 for the master thread, 0 for all other team threads, 2 for all team
threads if atomic reduction needed

A blocking reduce that includes an implicit barrier.
*/
kmp_int32 __kmpc_reduce(ident_t *loc, kmp_int32 global_tid, kmp_int32 num_vars,
                        size_t reduce_size, void *reduce_data,
                        void (*reduce_func)(void *lhs_data, void *rhs_data),
                        kmp_critical_name *lck) {
  KMP_COUNT_BLOCK(REDUCE_wait);
  int retval = 0;
  PACKED_REDUCTION_METHOD_T packed_reduction_method;
#if OMP_40_ENABLED
  kmp_info_t *th;
  kmp_team_t *team;
  int teams_swapped = 0, task_state;
#endif

  KA_TRACE(10, ("__kmpc_reduce() enter: called T#%d\n", global_tid));

  // why do we need this initialization here at all?
  // Reduction clause can not be a stand-alone directive.

  // do not call __kmp_serial_initialize(), it will be called by
  // __kmp_parallel_initialize() if needed
  // possible detection of false-positive race by the threadchecker ???
  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

// check correctness of reduce block nesting
#if KMP_USE_DYNAMIC_LOCK
  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_reduce, loc, NULL, 0);
#else
  if (__kmp_env_consistency_check)
    __kmp_push_sync(global_tid, ct_reduce, loc, NULL);
#endif

#if OMP_40_ENABLED
  th = __kmp_thread_from_gtid(global_tid);
  teams_swapped = __kmp_swap_teams_for_teams_reduction(th, &team, &task_state);
#endif // OMP_40_ENABLED

  packed_reduction_method = __kmp_determine_reduction_method(
      loc, global_tid, num_vars, reduce_size, reduce_data, reduce_func, lck);
  __KMP_SET_REDUCTION_METHOD(global_tid, packed_reduction_method);

  if (packed_reduction_method == critical_reduce_block) {

    __kmp_enter_critical_section_reduce_block(loc, global_tid, lck);
    retval = 1;

  } else if (packed_reduction_method == empty_reduce_block) {

    // usage: if team size == 1, no synchronization is required ( Intel
    // platforms only )
    retval = 1;

  } else if (packed_reduction_method == atomic_reduce_block) {

    retval = 2;

  } else if (TEST_REDUCTION_METHOD(packed_reduction_method,
                                   tree_reduce_block)) {

// case tree_reduce_block:
// this barrier should be visible to a customer and to the threading profile
// tool (it's a terminating barrier on constructs if NOWAIT not specified)
#if OMPT_SUPPORT
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
      if (ompt_frame->enter_frame.ptr == NULL)
        ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(global_tid);
    }
#endif
#if USE_ITT_NOTIFY
    __kmp_threads[global_tid]->th.th_ident =
        loc; // needed for correct notification of frames
#endif
    retval =
        __kmp_barrier(UNPACK_REDUCTION_BARRIER(packed_reduction_method),
                      global_tid, TRUE, reduce_size, reduce_data, reduce_func);
    retval = (retval != 0) ? (0) : (1);
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ompt_frame->enter_frame = ompt_data_none;
    }
#endif

    // all other workers except master should do this pop here
    // ( none of other workers except master will enter __kmpc_end_reduce() )
    if (__kmp_env_consistency_check) {
      if (retval == 0) { // 0: all other workers; 1: master
        __kmp_pop_sync(global_tid, ct_reduce, loc);
      }
    }

  } else {

    // should never reach this block
    KMP_ASSERT(0); // "unexpected method"
  }
#if OMP_40_ENABLED
  if (teams_swapped) {
    __kmp_restore_swapped_teams(th, team, task_state);
  }
#endif

  KA_TRACE(10,
           ("__kmpc_reduce() exit: called T#%d: method %08x, returns %08x\n",
            global_tid, packed_reduction_method, retval));

  return retval;
}

/*!
@ingroup SYNCHRONIZATION
@param loc source location information
@param global_tid global thread id.
@param lck pointer to the unique lock data structure

Finish the execution of a blocking reduce.
The <tt>lck</tt> pointer must be the same as that used in the corresponding
start function.
*/
void __kmpc_end_reduce(ident_t *loc, kmp_int32 global_tid,
                       kmp_critical_name *lck) {

  PACKED_REDUCTION_METHOD_T packed_reduction_method;
#if OMP_40_ENABLED
  kmp_info_t *th;
  kmp_team_t *team;
  int teams_swapped = 0, task_state;
#endif

  KA_TRACE(10, ("__kmpc_end_reduce() enter: called T#%d\n", global_tid));

#if OMP_40_ENABLED
  th = __kmp_thread_from_gtid(global_tid);
  teams_swapped = __kmp_swap_teams_for_teams_reduction(th, &team, &task_state);
#endif // OMP_40_ENABLED

  packed_reduction_method = __KMP_GET_REDUCTION_METHOD(global_tid);

  // this barrier should be visible to a customer and to the threading profile
  // tool (it's a terminating barrier on constructs if NOWAIT not specified)

  if (packed_reduction_method == critical_reduce_block) {

    __kmp_end_critical_section_reduce_block(loc, global_tid, lck);

// TODO: implicit barrier: should be exposed
#if OMPT_SUPPORT
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
      if (ompt_frame->enter_frame.ptr == NULL)
        ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(global_tid);
    }
#endif
#if USE_ITT_NOTIFY
    __kmp_threads[global_tid]->th.th_ident = loc;
#endif
    __kmp_barrier(bs_plain_barrier, global_tid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ompt_frame->enter_frame = ompt_data_none;
    }
#endif

  } else if (packed_reduction_method == empty_reduce_block) {

// usage: if team size==1, no synchronization is required (Intel platforms only)

// TODO: implicit barrier: should be exposed
#if OMPT_SUPPORT
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
      if (ompt_frame->enter_frame.ptr == NULL)
        ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(global_tid);
    }
#endif
#if USE_ITT_NOTIFY
    __kmp_threads[global_tid]->th.th_ident = loc;
#endif
    __kmp_barrier(bs_plain_barrier, global_tid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ompt_frame->enter_frame = ompt_data_none;
    }
#endif

  } else if (packed_reduction_method == atomic_reduce_block) {

#if OMPT_SUPPORT
    ompt_frame_t *ompt_frame;
    if (ompt_enabled.enabled) {
      __ompt_get_task_info_internal(0, NULL, NULL, &ompt_frame, NULL, NULL);
      if (ompt_frame->enter_frame.ptr == NULL)
        ompt_frame->enter_frame.ptr = OMPT_GET_FRAME_ADDRESS(0);
      OMPT_STORE_RETURN_ADDRESS(global_tid);
    }
#endif
// TODO: implicit barrier: should be exposed
#if USE_ITT_NOTIFY
    __kmp_threads[global_tid]->th.th_ident = loc;
#endif
    __kmp_barrier(bs_plain_barrier, global_tid, FALSE, 0, NULL, NULL);
#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.enabled) {
      ompt_frame->enter_frame = ompt_data_none;
    }
#endif

  } else if (TEST_REDUCTION_METHOD(packed_reduction_method,
                                   tree_reduce_block)) {

    // only master executes here (master releases all other workers)
    __kmp_end_split_barrier(UNPACK_REDUCTION_BARRIER(packed_reduction_method),
                            global_tid);

  } else {

    // should never reach this block
    KMP_ASSERT(0); // "unexpected method"
  }
#if OMP_40_ENABLED
  if (teams_swapped) {
    __kmp_restore_swapped_teams(th, team, task_state);
  }
#endif

  if (__kmp_env_consistency_check)
    __kmp_pop_sync(global_tid, ct_reduce, loc);

  KA_TRACE(10, ("__kmpc_end_reduce() exit: called T#%d: method %08x\n",
                global_tid, packed_reduction_method));

  return;
}

#undef __KMP_GET_REDUCTION_METHOD
#undef __KMP_SET_REDUCTION_METHOD

/* end of interface to fast scalable reduce routines */

kmp_uint64 __kmpc_get_taskid() {

  kmp_int32 gtid;
  kmp_info_t *thread;

  gtid = __kmp_get_gtid();
  if (gtid < 0) {
    return 0;
  }
  thread = __kmp_thread_from_gtid(gtid);
  return thread->th.th_current_task->td_task_id;

} // __kmpc_get_taskid

kmp_uint64 __kmpc_get_parent_taskid() {

  kmp_int32 gtid;
  kmp_info_t *thread;
  kmp_taskdata_t *parent_task;

  gtid = __kmp_get_gtid();
  if (gtid < 0) {
    return 0;
  }
  thread = __kmp_thread_from_gtid(gtid);
  parent_task = thread->th.th_current_task->td_parent;
  return (parent_task == NULL ? 0 : parent_task->td_task_id);

} // __kmpc_get_parent_taskid

#if OMP_45_ENABLED
/*!
@ingroup WORK_SHARING
@param loc  source location information.
@param gtid  global thread number.
@param num_dims  number of associated doacross loops.
@param dims  info on loops bounds.

Initialize doacross loop information.
Expect compiler send us inclusive bounds,
e.g. for(i=2;i<9;i+=2) lo=2, up=8, st=2.
*/
void __kmpc_doacross_init(ident_t *loc, int gtid, int num_dims,
                          const struct kmp_dim *dims) {
  int j, idx;
  kmp_int64 last, trace_count;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;
  kmp_uint32 *flags;
  kmp_disp_t *pr_buf = th->th.th_dispatch;
  dispatch_shared_info_t *sh_buf;

  KA_TRACE(
      20,
      ("__kmpc_doacross_init() enter: called T#%d, num dims %d, active %d\n",
       gtid, num_dims, !team->t.t_serialized));
  KMP_DEBUG_ASSERT(dims != NULL);
  KMP_DEBUG_ASSERT(num_dims > 0);

  if (team->t.t_serialized) {
    KA_TRACE(20, ("__kmpc_doacross_init() exit: serialized team\n"));
    return; // no dependencies if team is serialized
  }
  KMP_DEBUG_ASSERT(team->t.t_nproc > 1);
  idx = pr_buf->th_doacross_buf_idx++; // Increment index of shared buffer for
  // the next loop
  sh_buf = &team->t.t_disp_buffer[idx % __kmp_dispatch_num_buffers];

  // Save bounds info into allocated private buffer
  KMP_DEBUG_ASSERT(pr_buf->th_doacross_info == NULL);
  pr_buf->th_doacross_info = (kmp_int64 *)__kmp_thread_malloc(
      th, sizeof(kmp_int64) * (4 * num_dims + 1));
  KMP_DEBUG_ASSERT(pr_buf->th_doacross_info != NULL);
  pr_buf->th_doacross_info[0] =
      (kmp_int64)num_dims; // first element is number of dimensions
  // Save also address of num_done in order to access it later without knowing
  // the buffer index
  pr_buf->th_doacross_info[1] = (kmp_int64)&sh_buf->doacross_num_done;
  pr_buf->th_doacross_info[2] = dims[0].lo;
  pr_buf->th_doacross_info[3] = dims[0].up;
  pr_buf->th_doacross_info[4] = dims[0].st;
  last = 5;
  for (j = 1; j < num_dims; ++j) {
    kmp_int64
        range_length; // To keep ranges of all dimensions but the first dims[0]
    if (dims[j].st == 1) { // most common case
      // AC: should we care of ranges bigger than LLONG_MAX? (not for now)
      range_length = dims[j].up - dims[j].lo + 1;
    } else {
      if (dims[j].st > 0) {
        KMP_DEBUG_ASSERT(dims[j].up > dims[j].lo);
        range_length = (kmp_uint64)(dims[j].up - dims[j].lo) / dims[j].st + 1;
      } else { // negative increment
        KMP_DEBUG_ASSERT(dims[j].lo > dims[j].up);
        range_length =
            (kmp_uint64)(dims[j].lo - dims[j].up) / (-dims[j].st) + 1;
      }
    }
    pr_buf->th_doacross_info[last++] = range_length;
    pr_buf->th_doacross_info[last++] = dims[j].lo;
    pr_buf->th_doacross_info[last++] = dims[j].up;
    pr_buf->th_doacross_info[last++] = dims[j].st;
  }

  // Compute total trip count.
  // Start with range of dims[0] which we don't need to keep in the buffer.
  if (dims[0].st == 1) { // most common case
    trace_count = dims[0].up - dims[0].lo + 1;
  } else if (dims[0].st > 0) {
    KMP_DEBUG_ASSERT(dims[0].up > dims[0].lo);
    trace_count = (kmp_uint64)(dims[0].up - dims[0].lo) / dims[0].st + 1;
  } else { // negative increment
    KMP_DEBUG_ASSERT(dims[0].lo > dims[0].up);
    trace_count = (kmp_uint64)(dims[0].lo - dims[0].up) / (-dims[0].st) + 1;
  }
  for (j = 1; j < num_dims; ++j) {
    trace_count *= pr_buf->th_doacross_info[4 * j + 1]; // use kept ranges
  }
  KMP_DEBUG_ASSERT(trace_count > 0);

  // Check if shared buffer is not occupied by other loop (idx -
  // __kmp_dispatch_num_buffers)
  if (idx != sh_buf->doacross_buf_idx) {
    // Shared buffer is occupied, wait for it to be free
    __kmp_wait_yield_4((volatile kmp_uint32 *)&sh_buf->doacross_buf_idx, idx,
                       __kmp_eq_4, NULL);
  }
#if KMP_32_BIT_ARCH
  // Check if we are the first thread. After the CAS the first thread gets 0,
  // others get 1 if initialization is in progress, allocated pointer otherwise.
  // Treat pointer as volatile integer (value 0 or 1) until memory is allocated.
  flags = (kmp_uint32 *)KMP_COMPARE_AND_STORE_RET32(
      (volatile kmp_int32 *)&sh_buf->doacross_flags, NULL, 1);
#else
  flags = (kmp_uint32 *)KMP_COMPARE_AND_STORE_RET64(
      (volatile kmp_int64 *)&sh_buf->doacross_flags, NULL, 1LL);
#endif
  if (flags == NULL) {
    // we are the first thread, allocate the array of flags
    size_t size = trace_count / 8 + 8; // in bytes, use single bit per iteration
    flags = (kmp_uint32 *)__kmp_thread_calloc(th, size, 1);
    KMP_MB();
    sh_buf->doacross_flags = flags;
  } else if (flags == (kmp_uint32 *)1) {
#if KMP_32_BIT_ARCH
    // initialization is still in progress, need to wait
    while (*(volatile kmp_int32 *)&sh_buf->doacross_flags == 1)
#else
    while (*(volatile kmp_int64 *)&sh_buf->doacross_flags == 1LL)
#endif
      KMP_YIELD(TRUE);
    KMP_MB();
  } else {
    KMP_MB();
  }
  KMP_DEBUG_ASSERT(sh_buf->doacross_flags > (kmp_uint32 *)1); // check ptr value
  pr_buf->th_doacross_flags =
      sh_buf->doacross_flags; // save private copy in order to not
  // touch shared buffer on each iteration
  KA_TRACE(20, ("__kmpc_doacross_init() exit: T#%d\n", gtid));
}

void __kmpc_doacross_wait(ident_t *loc, int gtid, const kmp_int64 *vec) {
  kmp_int32 shft, num_dims, i;
  kmp_uint32 flag;
  kmp_int64 iter_number; // iteration number of "collapsed" loop nest
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;
  kmp_disp_t *pr_buf;
  kmp_int64 lo, up, st;

  KA_TRACE(20, ("__kmpc_doacross_wait() enter: called T#%d\n", gtid));
  if (team->t.t_serialized) {
    KA_TRACE(20, ("__kmpc_doacross_wait() exit: serialized team\n"));
    return; // no dependencies if team is serialized
  }

  // calculate sequential iteration number and check out-of-bounds condition
  pr_buf = th->th.th_dispatch;
  KMP_DEBUG_ASSERT(pr_buf->th_doacross_info != NULL);
  num_dims = pr_buf->th_doacross_info[0];
  lo = pr_buf->th_doacross_info[2];
  up = pr_buf->th_doacross_info[3];
  st = pr_buf->th_doacross_info[4];
  if (st == 1) { // most common case
    if (vec[0] < lo || vec[0] > up) {
      KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                    "bounds [%lld,%lld]\n",
                    gtid, vec[0], lo, up));
      return;
    }
    iter_number = vec[0] - lo;
  } else if (st > 0) {
    if (vec[0] < lo || vec[0] > up) {
      KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                    "bounds [%lld,%lld]\n",
                    gtid, vec[0], lo, up));
      return;
    }
    iter_number = (kmp_uint64)(vec[0] - lo) / st;
  } else { // negative increment
    if (vec[0] > lo || vec[0] < up) {
      KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                    "bounds [%lld,%lld]\n",
                    gtid, vec[0], lo, up));
      return;
    }
    iter_number = (kmp_uint64)(lo - vec[0]) / (-st);
  }
  for (i = 1; i < num_dims; ++i) {
    kmp_int64 iter, ln;
    kmp_int32 j = i * 4;
    ln = pr_buf->th_doacross_info[j + 1];
    lo = pr_buf->th_doacross_info[j + 2];
    up = pr_buf->th_doacross_info[j + 3];
    st = pr_buf->th_doacross_info[j + 4];
    if (st == 1) {
      if (vec[i] < lo || vec[i] > up) {
        KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                      "bounds [%lld,%lld]\n",
                      gtid, vec[i], lo, up));
        return;
      }
      iter = vec[i] - lo;
    } else if (st > 0) {
      if (vec[i] < lo || vec[i] > up) {
        KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                      "bounds [%lld,%lld]\n",
                      gtid, vec[i], lo, up));
        return;
      }
      iter = (kmp_uint64)(vec[i] - lo) / st;
    } else { // st < 0
      if (vec[i] > lo || vec[i] < up) {
        KA_TRACE(20, ("__kmpc_doacross_wait() exit: T#%d iter %lld is out of "
                      "bounds [%lld,%lld]\n",
                      gtid, vec[i], lo, up));
        return;
      }
      iter = (kmp_uint64)(lo - vec[i]) / (-st);
    }
    iter_number = iter + ln * iter_number;
  }
  shft = iter_number % 32; // use 32-bit granularity
  iter_number >>= 5; // divided by 32
  flag = 1 << shft;
  while ((flag & pr_buf->th_doacross_flags[iter_number]) == 0) {
    KMP_YIELD(TRUE);
  }
  KMP_MB();
  KA_TRACE(20,
           ("__kmpc_doacross_wait() exit: T#%d wait for iter %lld completed\n",
            gtid, (iter_number << 5) + shft));
}

void __kmpc_doacross_post(ident_t *loc, int gtid, const kmp_int64 *vec) {
  kmp_int32 shft, num_dims, i;
  kmp_uint32 flag;
  kmp_int64 iter_number; // iteration number of "collapsed" loop nest
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;
  kmp_disp_t *pr_buf;
  kmp_int64 lo, st;

  KA_TRACE(20, ("__kmpc_doacross_post() enter: called T#%d\n", gtid));
  if (team->t.t_serialized) {
    KA_TRACE(20, ("__kmpc_doacross_post() exit: serialized team\n"));
    return; // no dependencies if team is serialized
  }

  // calculate sequential iteration number (same as in "wait" but no
  // out-of-bounds checks)
  pr_buf = th->th.th_dispatch;
  KMP_DEBUG_ASSERT(pr_buf->th_doacross_info != NULL);
  num_dims = pr_buf->th_doacross_info[0];
  lo = pr_buf->th_doacross_info[2];
  st = pr_buf->th_doacross_info[4];
  if (st == 1) { // most common case
    iter_number = vec[0] - lo;
  } else if (st > 0) {
    iter_number = (kmp_uint64)(vec[0] - lo) / st;
  } else { // negative increment
    iter_number = (kmp_uint64)(lo - vec[0]) / (-st);
  }
  for (i = 1; i < num_dims; ++i) {
    kmp_int64 iter, ln;
    kmp_int32 j = i * 4;
    ln = pr_buf->th_doacross_info[j + 1];
    lo = pr_buf->th_doacross_info[j + 2];
    st = pr_buf->th_doacross_info[j + 4];
    if (st == 1) {
      iter = vec[i] - lo;
    } else if (st > 0) {
      iter = (kmp_uint64)(vec[i] - lo) / st;
    } else { // st < 0
      iter = (kmp_uint64)(lo - vec[i]) / (-st);
    }
    iter_number = iter + ln * iter_number;
  }
  shft = iter_number % 32; // use 32-bit granularity
  iter_number >>= 5; // divided by 32
  flag = 1 << shft;
  KMP_MB();
  if ((flag & pr_buf->th_doacross_flags[iter_number]) == 0)
    KMP_TEST_THEN_OR32(&pr_buf->th_doacross_flags[iter_number], flag);
  KA_TRACE(20, ("__kmpc_doacross_post() exit: T#%d iter %lld posted\n", gtid,
                (iter_number << 5) + shft));
}

void __kmpc_doacross_fini(ident_t *loc, int gtid) {
  kmp_int32 num_done;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;
  kmp_disp_t *pr_buf = th->th.th_dispatch;

  KA_TRACE(20, ("__kmpc_doacross_fini() enter: called T#%d\n", gtid));
  if (team->t.t_serialized) {
    KA_TRACE(20, ("__kmpc_doacross_fini() exit: serialized team %p\n", team));
    return; // nothing to do
  }
  num_done = KMP_TEST_THEN_INC32((kmp_int32 *)pr_buf->th_doacross_info[1]) + 1;
  if (num_done == th->th.th_team_nproc) {
    // we are the last thread, need to free shared resources
    int idx = pr_buf->th_doacross_buf_idx - 1;
    dispatch_shared_info_t *sh_buf =
        &team->t.t_disp_buffer[idx % __kmp_dispatch_num_buffers];
    KMP_DEBUG_ASSERT(pr_buf->th_doacross_info[1] ==
                     (kmp_int64)&sh_buf->doacross_num_done);
    KMP_DEBUG_ASSERT(num_done == sh_buf->doacross_num_done);
    KMP_DEBUG_ASSERT(idx == sh_buf->doacross_buf_idx);
    __kmp_thread_free(th, CCAST(kmp_uint32 *, sh_buf->doacross_flags));
    sh_buf->doacross_flags = NULL;
    sh_buf->doacross_num_done = 0;
    sh_buf->doacross_buf_idx +=
        __kmp_dispatch_num_buffers; // free buffer for future re-use
  }
  // free private resources (need to keep buffer index forever)
  pr_buf->th_doacross_flags = NULL;
  __kmp_thread_free(th, (void *)pr_buf->th_doacross_info);
  pr_buf->th_doacross_info = NULL;
  KA_TRACE(20, ("__kmpc_doacross_fini() exit: T#%d\n", gtid));
}
#endif

#if OMP_50_ENABLED
int __kmpc_get_target_offload(void) {
  if (!__kmp_init_serial) {
    __kmp_serial_initialize();
  }
  return __kmp_target_offload;
}
#endif // OMP_50_ENABLED

// end of file //
