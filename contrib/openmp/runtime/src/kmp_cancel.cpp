
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#include "kmp.h"
#include "kmp_i18n.h"
#include "kmp_io.h"
#include "kmp_str.h"
#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#if OMP_40_ENABLED

/*!
@ingroup CANCELLATION
@param loc_ref location of the original task directive
@param gtid Global thread ID of encountering thread
@param cncl_kind Cancellation kind (parallel, for, sections, taskgroup)

@return returns true if the cancellation request has been activated and the
execution thread needs to proceed to the end of the canceled region.

Request cancellation of the binding OpenMP region.
*/
kmp_int32 __kmpc_cancel(ident_t *loc_ref, kmp_int32 gtid, kmp_int32 cncl_kind) {
  kmp_info_t *this_thr = __kmp_threads[gtid];

  KC_TRACE(10, ("__kmpc_cancel: T#%d request %d OMP_CANCELLATION=%d\n", gtid,
                cncl_kind, __kmp_omp_cancellation));

  KMP_DEBUG_ASSERT(cncl_kind != cancel_noreq);
  KMP_DEBUG_ASSERT(cncl_kind == cancel_parallel || cncl_kind == cancel_loop ||
                   cncl_kind == cancel_sections ||
                   cncl_kind == cancel_taskgroup);
  KMP_DEBUG_ASSERT(__kmp_get_gtid() == gtid);

  if (__kmp_omp_cancellation) {
    switch (cncl_kind) {
    case cancel_parallel:
    case cancel_loop:
    case cancel_sections:
      // cancellation requests for parallel and worksharing constructs
      // are handled through the team structure
      {
        kmp_team_t *this_team = this_thr->th.th_team;
        KMP_DEBUG_ASSERT(this_team);
        kmp_int32 old = cancel_noreq;
        this_team->t.t_cancel_request.compare_exchange_strong(old, cncl_kind);
        if (old == cancel_noreq || old == cncl_kind) {
// we do not have a cancellation request in this team or we do have
// one that matches the current request -> cancel
#if OMPT_SUPPORT && OMPT_OPTIONAL
          if (ompt_enabled.ompt_callback_cancel) {
            ompt_data_t *task_data;
            __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL,
                                          NULL);
            ompt_cancel_flag_t type = ompt_cancel_parallel;
            if (cncl_kind == cancel_parallel)
              type = ompt_cancel_parallel;
            else if (cncl_kind == cancel_loop)
              type = ompt_cancel_loop;
            else if (cncl_kind == cancel_sections)
              type = ompt_cancel_sections;
            ompt_callbacks.ompt_callback(ompt_callback_cancel)(
                task_data, type | ompt_cancel_activated,
                OMPT_GET_RETURN_ADDRESS(0));
          }
#endif
          return 1 /* true */;
        }
        break;
      }
    case cancel_taskgroup:
      // cancellation requests for a task group
      // are handled through the taskgroup structure
      {
        kmp_taskdata_t *task;
        kmp_taskgroup_t *taskgroup;

        task = this_thr->th.th_current_task;
        KMP_DEBUG_ASSERT(task);

        taskgroup = task->td_taskgroup;
        if (taskgroup) {
          kmp_int32 old = cancel_noreq;
          taskgroup->cancel_request.compare_exchange_strong(old, cncl_kind);
          if (old == cancel_noreq || old == cncl_kind) {
// we do not have a cancellation request in this taskgroup or we do
// have one that matches the current request -> cancel
#if OMPT_SUPPORT && OMPT_OPTIONAL
            if (ompt_enabled.ompt_callback_cancel) {
              ompt_data_t *task_data;
              __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL,
                                            NULL);
              ompt_callbacks.ompt_callback(ompt_callback_cancel)(
                  task_data, ompt_cancel_taskgroup | ompt_cancel_activated,
                  OMPT_GET_RETURN_ADDRESS(0));
            }
#endif
            return 1 /* true */;
          }
        } else {
          // TODO: what needs to happen here?
          // the specification disallows cancellation w/o taskgroups
          // so we might do anything here, let's abort for now
          KMP_ASSERT(0 /* false */);
        }
      }
      break;
    default:
      KMP_ASSERT(0 /* false */);
    }
  }

  // ICV OMP_CANCELLATION=false, so we ignored this cancel request
  KMP_DEBUG_ASSERT(!__kmp_omp_cancellation);
  return 0 /* false */;
}

/*!
@ingroup CANCELLATION
@param loc_ref location of the original task directive
@param gtid Global thread ID of encountering thread
@param cncl_kind Cancellation kind (parallel, for, sections, taskgroup)

@return returns true if a matching cancellation request has been flagged in the
RTL and the encountering thread has to cancel..

Cancellation point for the encountering thread.
*/
kmp_int32 __kmpc_cancellationpoint(ident_t *loc_ref, kmp_int32 gtid,
                                   kmp_int32 cncl_kind) {
  kmp_info_t *this_thr = __kmp_threads[gtid];

  KC_TRACE(10,
           ("__kmpc_cancellationpoint: T#%d request %d OMP_CANCELLATION=%d\n",
            gtid, cncl_kind, __kmp_omp_cancellation));

  KMP_DEBUG_ASSERT(cncl_kind != cancel_noreq);
  KMP_DEBUG_ASSERT(cncl_kind == cancel_parallel || cncl_kind == cancel_loop ||
                   cncl_kind == cancel_sections ||
                   cncl_kind == cancel_taskgroup);
  KMP_DEBUG_ASSERT(__kmp_get_gtid() == gtid);

  if (__kmp_omp_cancellation) {
    switch (cncl_kind) {
    case cancel_parallel:
    case cancel_loop:
    case cancel_sections:
      // cancellation requests for parallel and worksharing constructs
      // are handled through the team structure
      {
        kmp_team_t *this_team = this_thr->th.th_team;
        KMP_DEBUG_ASSERT(this_team);
        if (this_team->t.t_cancel_request) {
          if (cncl_kind == this_team->t.t_cancel_request) {
// the request in the team structure matches the type of
// cancellation point so we can cancel
#if OMPT_SUPPORT && OMPT_OPTIONAL
            if (ompt_enabled.ompt_callback_cancel) {
              ompt_data_t *task_data;
              __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL,
                                            NULL);
              ompt_cancel_flag_t type = ompt_cancel_parallel;
              if (cncl_kind == cancel_parallel)
                type = ompt_cancel_parallel;
              else if (cncl_kind == cancel_loop)
                type = ompt_cancel_loop;
              else if (cncl_kind == cancel_sections)
                type = ompt_cancel_sections;
              ompt_callbacks.ompt_callback(ompt_callback_cancel)(
                  task_data, type | ompt_cancel_detected,
                  OMPT_GET_RETURN_ADDRESS(0));
            }
#endif
            return 1 /* true */;
          }
          KMP_ASSERT(0 /* false */);
        } else {
          // we do not have a cancellation request pending, so we just
          // ignore this cancellation point
          return 0;
        }
        break;
      }
    case cancel_taskgroup:
      // cancellation requests for a task group
      // are handled through the taskgroup structure
      {
        kmp_taskdata_t *task;
        kmp_taskgroup_t *taskgroup;

        task = this_thr->th.th_current_task;
        KMP_DEBUG_ASSERT(task);

        taskgroup = task->td_taskgroup;
        if (taskgroup) {
// return the current status of cancellation for the taskgroup
#if OMPT_SUPPORT && OMPT_OPTIONAL
          if (ompt_enabled.ompt_callback_cancel &&
              !!taskgroup->cancel_request) {
            ompt_data_t *task_data;
            __ompt_get_task_info_internal(0, NULL, &task_data, NULL, NULL,
                                          NULL);
            ompt_callbacks.ompt_callback(ompt_callback_cancel)(
                task_data, ompt_cancel_taskgroup | ompt_cancel_detected,
                OMPT_GET_RETURN_ADDRESS(0));
          }
#endif
          return !!taskgroup->cancel_request;
        } else {
          // if a cancellation point is encountered by a task that does not
          // belong to a taskgroup, it is OK to ignore it
          return 0 /* false */;
        }
      }
    default:
      KMP_ASSERT(0 /* false */);
    }
  }

  // ICV OMP_CANCELLATION=false, so we ignore the cancellation point
  KMP_DEBUG_ASSERT(!__kmp_omp_cancellation);
  return 0 /* false */;
}

/*!
@ingroup CANCELLATION
@param loc_ref location of the original task directive
@param gtid Global thread ID of encountering thread

@return returns true if a matching cancellation request has been flagged in the
RTL and the encountering thread has to cancel..

Barrier with cancellation point to send threads from the barrier to the
end of the parallel region.  Needs a special code pattern as documented
in the design document for the cancellation feature.
*/
kmp_int32 __kmpc_cancel_barrier(ident_t *loc, kmp_int32 gtid) {
  int ret = 0 /* false */;
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *this_team = this_thr->th.th_team;

  KMP_DEBUG_ASSERT(__kmp_get_gtid() == gtid);

  // call into the standard barrier
  __kmpc_barrier(loc, gtid);

  // if cancellation is active, check cancellation flag
  if (__kmp_omp_cancellation) {
    // depending on which construct to cancel, check the flag and
    // reset the flag
    switch (KMP_ATOMIC_LD_RLX(&(this_team->t.t_cancel_request))) {
    case cancel_parallel:
      ret = 1;
      // ensure that threads have checked the flag, when
      // leaving the above barrier
      __kmpc_barrier(loc, gtid);
      this_team->t.t_cancel_request = cancel_noreq;
      // the next barrier is the fork/join barrier, which
      // synchronizes the threads leaving here
      break;
    case cancel_loop:
    case cancel_sections:
      ret = 1;
      // ensure that threads have checked the flag, when
      // leaving the above barrier
      __kmpc_barrier(loc, gtid);
      this_team->t.t_cancel_request = cancel_noreq;
      // synchronize the threads again to make sure we do not have any run-away
      // threads that cause a race on the cancellation flag
      __kmpc_barrier(loc, gtid);
      break;
    case cancel_taskgroup:
      // this case should not occur
      KMP_ASSERT(0 /* false */);
      break;
    case cancel_noreq:
      // do nothing
      break;
    default:
      KMP_ASSERT(0 /* false */);
    }
  }

  return ret;
}

/*!
@ingroup CANCELLATION
@param loc_ref location of the original task directive
@param gtid Global thread ID of encountering thread

@return returns true if a matching cancellation request has been flagged in the
RTL and the encountering thread has to cancel..

Query function to query the current status of cancellation requests.
Can be used to implement the following pattern:

if (kmp_get_cancellation_status(kmp_cancel_parallel)) {
    perform_cleanup();
    #pragma omp cancellation point parallel
}
*/
int __kmp_get_cancellation_status(int cancel_kind) {
  if (__kmp_omp_cancellation) {
    kmp_info_t *this_thr = __kmp_entry_thread();

    switch (cancel_kind) {
    case cancel_parallel:
    case cancel_loop:
    case cancel_sections: {
      kmp_team_t *this_team = this_thr->th.th_team;
      return this_team->t.t_cancel_request == cancel_kind;
    }
    case cancel_taskgroup: {
      kmp_taskdata_t *task;
      kmp_taskgroup_t *taskgroup;
      task = this_thr->th.th_current_task;
      taskgroup = task->td_taskgroup;
      return taskgroup && taskgroup->cancel_request;
    }
    }
  }

  return 0 /* false */;
}

#endif
