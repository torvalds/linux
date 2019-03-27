/*
 * kmp_sched.cpp -- static scheduling -- iteration initialization
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

/* Static scheduling initialization.

  NOTE: team->t.t_nproc is a constant inside of any dispatch loop, however
        it may change values between parallel regions.  __kmp_max_nth
        is the largest value __kmp_nth may take, 1 is the smallest. */

#include "kmp.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_itt.h"
#include "kmp_stats.h"
#include "kmp_str.h"

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#ifdef KMP_DEBUG
//-------------------------------------------------------------------------
// template for debug prints specification ( d, u, lld, llu )
char const *traits_t<int>::spec = "d";
char const *traits_t<unsigned int>::spec = "u";
char const *traits_t<long long>::spec = "lld";
char const *traits_t<unsigned long long>::spec = "llu";
char const *traits_t<long>::spec = "ld";
//-------------------------------------------------------------------------
#endif

template <typename T>
static void __kmp_for_static_init(ident_t *loc, kmp_int32 global_tid,
                                  kmp_int32 schedtype, kmp_int32 *plastiter,
                                  T *plower, T *pupper,
                                  typename traits_t<T>::signed_t *pstride,
                                  typename traits_t<T>::signed_t incr,
                                  typename traits_t<T>::signed_t chunk
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                  ,
                                  void *codeptr
#endif
                                  ) {
  KMP_COUNT_BLOCK(OMP_LOOP_STATIC);
  KMP_PUSH_PARTITIONED_TIMER(OMP_loop_static);
  KMP_PUSH_PARTITIONED_TIMER(OMP_loop_static_scheduling);

  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  /*  this all has to be changed back to TID and such.. */
  kmp_int32 gtid = global_tid;
  kmp_uint32 tid;
  kmp_uint32 nth;
  UT trip_count;
  kmp_team_t *team;
  kmp_info_t *th = __kmp_threads[gtid];

#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_team_info_t *team_info = NULL;
  ompt_task_info_t *task_info = NULL;
  ompt_work_t ompt_work_type = ompt_work_loop;

  static kmp_int8 warn = 0;

  if (ompt_enabled.ompt_callback_work) {
    // Only fully initialize variables needed by OMPT if OMPT is enabled.
    team_info = __ompt_get_teaminfo(0, NULL);
    task_info = __ompt_get_task_info_object(0);
    // Determine workshare type
    if (loc != NULL) {
      if ((loc->flags & KMP_IDENT_WORK_LOOP) != 0) {
        ompt_work_type = ompt_work_loop;
      } else if ((loc->flags & KMP_IDENT_WORK_SECTIONS) != 0) {
        ompt_work_type = ompt_work_sections;
      } else if ((loc->flags & KMP_IDENT_WORK_DISTRIBUTE) != 0) {
        ompt_work_type = ompt_work_distribute;
      } else {
        kmp_int8 bool_res =
            KMP_COMPARE_AND_STORE_ACQ8(&warn, (kmp_int8)0, (kmp_int8)1);
        if (bool_res)
          KMP_WARNING(OmptOutdatedWorkshare);
      }
      KMP_DEBUG_ASSERT(ompt_work_type);
    }
  }
#endif

  KMP_DEBUG_ASSERT(plastiter && plower && pupper && pstride);
  KE_TRACE(10, ("__kmpc_for_static_init called (%d)\n", global_tid));
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmpc_for_static_init: T#%%d sched=%%d liter=%%d iter=(%%%s,"
        " %%%s, %%%s) incr=%%%s chunk=%%%s signed?<%s>\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec,
        traits_t<ST>::spec, traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100, (buff, global_tid, schedtype, *plastiter, *plower, *pupper,
                   *pstride, incr, chunk));
    __kmp_str_free(&buff);
  }
#endif

  if (__kmp_env_consistency_check) {
    __kmp_push_workshare(global_tid, ct_pdo, loc);
    if (incr == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited, ct_pdo,
                            loc);
    }
  }
  /* special handling for zero-trip loops */
  if (incr > 0 ? (*pupper < *plower) : (*plower < *pupper)) {
    if (plastiter != NULL)
      *plastiter = FALSE;
    /* leave pupper and plower set to entire iteration space */
    *pstride = incr; /* value should never be used */
// *plower = *pupper - incr;
// let compiler bypass the illegal loop (like for(i=1;i<10;i--))
// THE LINE COMMENTED ABOVE CAUSED shape2F/h_tests_1.f TO HAVE A FAILURE
// ON A ZERO-TRIP LOOP (lower=1, upper=0,stride=1) - JPH June 23, 2009.
#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format("__kmpc_for_static_init:(ZERO TRIP) liter=%%d "
                              "lower=%%%s upper=%%%s stride = %%%s "
                              "signed?<%s>, loc = %%s\n",
                              traits_t<T>::spec, traits_t<T>::spec,
                              traits_t<ST>::spec, traits_t<T>::spec);
      KD_TRACE(100,
               (buff, *plastiter, *plower, *pupper, *pstride, loc->psource));
      __kmp_str_free(&buff);
    }
#endif
    KE_TRACE(10, ("__kmpc_for_static_init: T#%d return\n", global_tid));

#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.ompt_callback_work) {
      ompt_callbacks.ompt_callback(ompt_callback_work)(
          ompt_work_type, ompt_scope_begin, &(team_info->parallel_data),
          &(task_info->task_data), 0, codeptr);
    }
#endif
    return;
  }

#if OMP_40_ENABLED
  // Although there are schedule enumerations above kmp_ord_upper which are not
  // schedules for "distribute", the only ones which are useful are dynamic, so
  // cannot be seen here, since this codepath is only executed for static
  // schedules.
  if (schedtype > kmp_ord_upper) {
    // we are in DISTRIBUTE construct
    schedtype += kmp_sch_static -
                 kmp_distribute_static; // AC: convert to usual schedule type
    tid = th->th.th_team->t.t_master_tid;
    team = th->th.th_team->t.t_parent;
  } else
#endif
  {
    tid = __kmp_tid_from_gtid(global_tid);
    team = th->th.th_team;
  }

  /* determine if "for" loop is an active worksharing construct */
  if (team->t.t_serialized) {
    /* serialized parallel, each thread executes whole iteration space */
    if (plastiter != NULL)
      *plastiter = TRUE;
    /* leave pupper and plower set to entire iteration space */
    *pstride =
        (incr > 0) ? (*pupper - *plower + 1) : (-(*plower - *pupper + 1));

#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format("__kmpc_for_static_init: (serial) liter=%%d "
                              "lower=%%%s upper=%%%s stride = %%%s\n",
                              traits_t<T>::spec, traits_t<T>::spec,
                              traits_t<ST>::spec);
      KD_TRACE(100, (buff, *plastiter, *plower, *pupper, *pstride));
      __kmp_str_free(&buff);
    }
#endif
    KE_TRACE(10, ("__kmpc_for_static_init: T#%d return\n", global_tid));

#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.ompt_callback_work) {
      ompt_callbacks.ompt_callback(ompt_callback_work)(
          ompt_work_type, ompt_scope_begin, &(team_info->parallel_data),
          &(task_info->task_data), *pstride, codeptr);
    }
#endif
    return;
  }
  nth = team->t.t_nproc;
  if (nth == 1) {
    if (plastiter != NULL)
      *plastiter = TRUE;
    *pstride =
        (incr > 0) ? (*pupper - *plower + 1) : (-(*plower - *pupper + 1));
#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format("__kmpc_for_static_init: (serial) liter=%%d "
                              "lower=%%%s upper=%%%s stride = %%%s\n",
                              traits_t<T>::spec, traits_t<T>::spec,
                              traits_t<ST>::spec);
      KD_TRACE(100, (buff, *plastiter, *plower, *pupper, *pstride));
      __kmp_str_free(&buff);
    }
#endif
    KE_TRACE(10, ("__kmpc_for_static_init: T#%d return\n", global_tid));

#if OMPT_SUPPORT && OMPT_OPTIONAL
    if (ompt_enabled.ompt_callback_work) {
      ompt_callbacks.ompt_callback(ompt_callback_work)(
          ompt_work_type, ompt_scope_begin, &(team_info->parallel_data),
          &(task_info->task_data), *pstride, codeptr);
    }
#endif
    return;
  }

  /* compute trip count */
  if (incr == 1) {
    trip_count = *pupper - *plower + 1;
  } else if (incr == -1) {
    trip_count = *plower - *pupper + 1;
  } else if (incr > 0) {
    // upper-lower can exceed the limit of signed type
    trip_count = (UT)(*pupper - *plower) / incr + 1;
  } else {
    trip_count = (UT)(*plower - *pupper) / (-incr) + 1;
  }

  if (__kmp_env_consistency_check) {
    /* tripcount overflow? */
    if (trip_count == 0 && *pupper != *plower) {
      __kmp_error_construct(kmp_i18n_msg_CnsIterationRangeTooLarge, ct_pdo,
                            loc);
    }
  }

  /* compute remaining parameters */
  switch (schedtype) {
  case kmp_sch_static: {
    if (trip_count < nth) {
      KMP_DEBUG_ASSERT(
          __kmp_static == kmp_sch_static_greedy ||
          __kmp_static ==
              kmp_sch_static_balanced); // Unknown static scheduling type.
      if (tid < trip_count) {
        *pupper = *plower = *plower + tid * incr;
      } else {
        *plower = *pupper + incr;
      }
      if (plastiter != NULL)
        *plastiter = (tid == trip_count - 1);
    } else {
      if (__kmp_static == kmp_sch_static_balanced) {
        UT small_chunk = trip_count / nth;
        UT extras = trip_count % nth;
        *plower += incr * (tid * small_chunk + (tid < extras ? tid : extras));
        *pupper = *plower + small_chunk * incr - (tid < extras ? 0 : incr);
        if (plastiter != NULL)
          *plastiter = (tid == nth - 1);
      } else {
        T big_chunk_inc_count =
            (trip_count / nth + ((trip_count % nth) ? 1 : 0)) * incr;
        T old_upper = *pupper;

        KMP_DEBUG_ASSERT(__kmp_static == kmp_sch_static_greedy);
        // Unknown static scheduling type.

        *plower += tid * big_chunk_inc_count;
        *pupper = *plower + big_chunk_inc_count - incr;
        if (incr > 0) {
          if (*pupper < *plower)
            *pupper = traits_t<T>::max_value;
          if (plastiter != NULL)
            *plastiter = *plower <= old_upper && *pupper > old_upper - incr;
          if (*pupper > old_upper)
            *pupper = old_upper; // tracker C73258
        } else {
          if (*pupper > *plower)
            *pupper = traits_t<T>::min_value;
          if (plastiter != NULL)
            *plastiter = *plower >= old_upper && *pupper < old_upper - incr;
          if (*pupper < old_upper)
            *pupper = old_upper; // tracker C73258
        }
      }
    }
    *pstride = trip_count;
    break;
  }
  case kmp_sch_static_chunked: {
    ST span;
    if (chunk < 1) {
      chunk = 1;
    }
    span = chunk * incr;
    *pstride = span * nth;
    *plower = *plower + (span * tid);
    *pupper = *plower + span - incr;
    if (plastiter != NULL)
      *plastiter = (tid == ((trip_count - 1) / (UT)chunk) % nth);
    break;
  }
#if OMP_45_ENABLED
  case kmp_sch_static_balanced_chunked: {
    T old_upper = *pupper;
    // round up to make sure the chunk is enough to cover all iterations
    UT span = (trip_count + nth - 1) / nth;

    // perform chunk adjustment
    chunk = (span + chunk - 1) & ~(chunk - 1);

    span = chunk * incr;
    *plower = *plower + (span * tid);
    *pupper = *plower + span - incr;
    if (incr > 0) {
      if (*pupper > old_upper)
        *pupper = old_upper;
    } else if (*pupper < old_upper)
      *pupper = old_upper;

    if (plastiter != NULL)
      *plastiter = (tid == ((trip_count - 1) / (UT)chunk));
    break;
  }
#endif
  default:
    KMP_ASSERT2(0, "__kmpc_for_static_init: unknown scheduling type");
    break;
  }

#if USE_ITT_BUILD
  // Report loop metadata
  if (KMP_MASTER_TID(tid) && __itt_metadata_add_ptr &&
      __kmp_forkjoin_frames_mode == 3 &&
#if OMP_40_ENABLED
      th->th.th_teams_microtask == NULL &&
#endif
      team->t.t_active_level == 1) {
    kmp_uint64 cur_chunk = chunk;
    // Calculate chunk in case it was not specified; it is specified for
    // kmp_sch_static_chunked
    if (schedtype == kmp_sch_static) {
      cur_chunk = trip_count / nth + ((trip_count % nth) ? 1 : 0);
    }
    // 0 - "static" schedule
    __kmp_itt_metadata_loop(loc, 0, trip_count, cur_chunk);
  }
#endif
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmpc_for_static_init: liter=%%d lower=%%%s "
                            "upper=%%%s stride = %%%s signed?<%s>\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100, (buff, *plastiter, *plower, *pupper, *pstride));
    __kmp_str_free(&buff);
  }
#endif
  KE_TRACE(10, ("__kmpc_for_static_init: T#%d return\n", global_tid));

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_work) {
    ompt_callbacks.ompt_callback(ompt_callback_work)(
        ompt_work_type, ompt_scope_begin, &(team_info->parallel_data),
        &(task_info->task_data), trip_count, codeptr);
  }
#endif

#if KMP_STATS_ENABLED
  {
    kmp_int64 t;
    kmp_int64 u = (kmp_int64)(*pupper);
    kmp_int64 l = (kmp_int64)(*plower);
    kmp_int64 i = (kmp_int64)incr;
    /* compute trip count */
    if (i == 1) {
      t = u - l + 1;
    } else if (i == -1) {
      t = l - u + 1;
    } else if (i > 0) {
      t = (u - l) / i + 1;
    } else {
      t = (l - u) / (-i) + 1;
    }
    KMP_COUNT_VALUE(OMP_loop_static_iterations, t);
    KMP_POP_PARTITIONED_TIMER();
  }
#endif
  return;
}

template <typename T>
static void __kmp_dist_for_static_init(ident_t *loc, kmp_int32 gtid,
                                       kmp_int32 schedule, kmp_int32 *plastiter,
                                       T *plower, T *pupper, T *pupperDist,
                                       typename traits_t<T>::signed_t *pstride,
                                       typename traits_t<T>::signed_t incr,
                                       typename traits_t<T>::signed_t chunk) {
  KMP_COUNT_BLOCK(OMP_DISTRIBUTE);
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  kmp_uint32 tid;
  kmp_uint32 nth;
  kmp_uint32 team_id;
  kmp_uint32 nteams;
  UT trip_count;
  kmp_team_t *team;
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(plastiter && plower && pupper && pupperDist && pstride);
  KE_TRACE(10, ("__kmpc_dist_for_static_init called (%d)\n", gtid));
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmpc_dist_for_static_init: T#%%d schedLoop=%%d liter=%%d "
        "iter=(%%%s, %%%s, %%%s) chunk=%%%s signed?<%s>\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec,
        traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100,
             (buff, gtid, schedule, *plastiter, *plower, *pupper, incr, chunk));
    __kmp_str_free(&buff);
  }
#endif

  if (__kmp_env_consistency_check) {
    __kmp_push_workshare(gtid, ct_pdo, loc);
    if (incr == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited, ct_pdo,
                            loc);
    }
    if (incr > 0 ? (*pupper < *plower) : (*plower < *pupper)) {
      // The loop is illegal.
      // Some zero-trip loops maintained by compiler, e.g.:
      //   for(i=10;i<0;++i) // lower >= upper - run-time check
      //   for(i=0;i>10;--i) // lower <= upper - run-time check
      //   for(i=0;i>10;++i) // incr > 0       - compile-time check
      //   for(i=10;i<0;--i) // incr < 0       - compile-time check
      // Compiler does not check the following illegal loops:
      //   for(i=0;i<10;i+=incr) // where incr<0
      //   for(i=10;i>0;i-=incr) // where incr<0
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrIllegal, ct_pdo, loc);
    }
  }
  tid = __kmp_tid_from_gtid(gtid);
  th = __kmp_threads[gtid];
  nth = th->th.th_team_nproc;
  team = th->th.th_team;
#if OMP_40_ENABLED
  KMP_DEBUG_ASSERT(th->th.th_teams_microtask); // we are in the teams construct
  nteams = th->th.th_teams_size.nteams;
#endif
  team_id = team->t.t_master_tid;
  KMP_DEBUG_ASSERT(nteams == (kmp_uint32)team->t.t_parent->t.t_nproc);

  // compute global trip count
  if (incr == 1) {
    trip_count = *pupper - *plower + 1;
  } else if (incr == -1) {
    trip_count = *plower - *pupper + 1;
  } else if (incr > 0) {
    // upper-lower can exceed the limit of signed type
    trip_count = (UT)(*pupper - *plower) / incr + 1;
  } else {
    trip_count = (UT)(*plower - *pupper) / (-incr) + 1;
  }

  *pstride = *pupper - *plower; // just in case (can be unused)
  if (trip_count <= nteams) {
    KMP_DEBUG_ASSERT(
        __kmp_static == kmp_sch_static_greedy ||
        __kmp_static ==
            kmp_sch_static_balanced); // Unknown static scheduling type.
    // only masters of some teams get single iteration, other threads get
    // nothing
    if (team_id < trip_count && tid == 0) {
      *pupper = *pupperDist = *plower = *plower + team_id * incr;
    } else {
      *pupperDist = *pupper;
      *plower = *pupper + incr; // compiler should skip loop body
    }
    if (plastiter != NULL)
      *plastiter = (tid == 0 && team_id == trip_count - 1);
  } else {
    // Get the team's chunk first (each team gets at most one chunk)
    if (__kmp_static == kmp_sch_static_balanced) {
      UT chunkD = trip_count / nteams;
      UT extras = trip_count % nteams;
      *plower +=
          incr * (team_id * chunkD + (team_id < extras ? team_id : extras));
      *pupperDist = *plower + chunkD * incr - (team_id < extras ? 0 : incr);
      if (plastiter != NULL)
        *plastiter = (team_id == nteams - 1);
    } else {
      T chunk_inc_count =
          (trip_count / nteams + ((trip_count % nteams) ? 1 : 0)) * incr;
      T upper = *pupper;
      KMP_DEBUG_ASSERT(__kmp_static == kmp_sch_static_greedy);
      // Unknown static scheduling type.
      *plower += team_id * chunk_inc_count;
      *pupperDist = *plower + chunk_inc_count - incr;
      // Check/correct bounds if needed
      if (incr > 0) {
        if (*pupperDist < *plower)
          *pupperDist = traits_t<T>::max_value;
        if (plastiter != NULL)
          *plastiter = *plower <= upper && *pupperDist > upper - incr;
        if (*pupperDist > upper)
          *pupperDist = upper; // tracker C73258
        if (*plower > *pupperDist) {
          *pupper = *pupperDist; // no iterations available for the team
          goto end;
        }
      } else {
        if (*pupperDist > *plower)
          *pupperDist = traits_t<T>::min_value;
        if (plastiter != NULL)
          *plastiter = *plower >= upper && *pupperDist < upper - incr;
        if (*pupperDist < upper)
          *pupperDist = upper; // tracker C73258
        if (*plower < *pupperDist) {
          *pupper = *pupperDist; // no iterations available for the team
          goto end;
        }
      }
    }
    // Get the parallel loop chunk now (for thread)
    // compute trip count for team's chunk
    if (incr == 1) {
      trip_count = *pupperDist - *plower + 1;
    } else if (incr == -1) {
      trip_count = *plower - *pupperDist + 1;
    } else if (incr > 1) {
      // upper-lower can exceed the limit of signed type
      trip_count = (UT)(*pupperDist - *plower) / incr + 1;
    } else {
      trip_count = (UT)(*plower - *pupperDist) / (-incr) + 1;
    }
    KMP_DEBUG_ASSERT(trip_count);
    switch (schedule) {
    case kmp_sch_static: {
      if (trip_count <= nth) {
        KMP_DEBUG_ASSERT(
            __kmp_static == kmp_sch_static_greedy ||
            __kmp_static ==
                kmp_sch_static_balanced); // Unknown static scheduling type.
        if (tid < trip_count)
          *pupper = *plower = *plower + tid * incr;
        else
          *plower = *pupper + incr; // no iterations available
        if (plastiter != NULL)
          if (*plastiter != 0 && !(tid == trip_count - 1))
            *plastiter = 0;
      } else {
        if (__kmp_static == kmp_sch_static_balanced) {
          UT chunkL = trip_count / nth;
          UT extras = trip_count % nth;
          *plower += incr * (tid * chunkL + (tid < extras ? tid : extras));
          *pupper = *plower + chunkL * incr - (tid < extras ? 0 : incr);
          if (plastiter != NULL)
            if (*plastiter != 0 && !(tid == nth - 1))
              *plastiter = 0;
        } else {
          T chunk_inc_count =
              (trip_count / nth + ((trip_count % nth) ? 1 : 0)) * incr;
          T upper = *pupperDist;
          KMP_DEBUG_ASSERT(__kmp_static == kmp_sch_static_greedy);
          // Unknown static scheduling type.
          *plower += tid * chunk_inc_count;
          *pupper = *plower + chunk_inc_count - incr;
          if (incr > 0) {
            if (*pupper < *plower)
              *pupper = traits_t<T>::max_value;
            if (plastiter != NULL)
              if (*plastiter != 0 &&
                  !(*plower <= upper && *pupper > upper - incr))
                *plastiter = 0;
            if (*pupper > upper)
              *pupper = upper; // tracker C73258
          } else {
            if (*pupper > *plower)
              *pupper = traits_t<T>::min_value;
            if (plastiter != NULL)
              if (*plastiter != 0 &&
                  !(*plower >= upper && *pupper < upper - incr))
                *plastiter = 0;
            if (*pupper < upper)
              *pupper = upper; // tracker C73258
          }
        }
      }
      break;
    }
    case kmp_sch_static_chunked: {
      ST span;
      if (chunk < 1)
        chunk = 1;
      span = chunk * incr;
      *pstride = span * nth;
      *plower = *plower + (span * tid);
      *pupper = *plower + span - incr;
      if (plastiter != NULL)
        if (*plastiter != 0 && !(tid == ((trip_count - 1) / (UT)chunk) % nth))
          *plastiter = 0;
      break;
    }
    default:
      KMP_ASSERT2(0,
                  "__kmpc_dist_for_static_init: unknown loop scheduling type");
      break;
    }
  }
end:;
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmpc_dist_for_static_init: last=%%d lo=%%%s up=%%%s upDist=%%%s "
        "stride=%%%s signed?<%s>\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<T>::spec,
        traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100, (buff, *plastiter, *plower, *pupper, *pupperDist, *pstride));
    __kmp_str_free(&buff);
  }
#endif
  KE_TRACE(10, ("__kmpc_dist_for_static_init: T#%d return\n", gtid));
  return;
}

template <typename T>
static void __kmp_team_static_init(ident_t *loc, kmp_int32 gtid,
                                   kmp_int32 *p_last, T *p_lb, T *p_ub,
                                   typename traits_t<T>::signed_t *p_st,
                                   typename traits_t<T>::signed_t incr,
                                   typename traits_t<T>::signed_t chunk) {
  // The routine returns the first chunk distributed to the team and
  // stride for next chunks calculation.
  // Last iteration flag set for the team that will execute
  // the last iteration of the loop.
  // The routine is called for dist_schedue(static,chunk) only.
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  kmp_uint32 team_id;
  kmp_uint32 nteams;
  UT trip_count;
  T lower;
  T upper;
  ST span;
  kmp_team_t *team;
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(p_last && p_lb && p_ub && p_st);
  KE_TRACE(10, ("__kmp_team_static_init called (%d)\n", gtid));
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_team_static_init enter: T#%%d liter=%%d "
                            "iter=(%%%s, %%%s, %%%s) chunk %%%s; signed?<%s>\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<ST>::spec,
                            traits_t<T>::spec);
    KD_TRACE(100, (buff, gtid, *p_last, *p_lb, *p_ub, *p_st, chunk));
    __kmp_str_free(&buff);
  }
#endif

  lower = *p_lb;
  upper = *p_ub;
  if (__kmp_env_consistency_check) {
    if (incr == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited, ct_pdo,
                            loc);
    }
    if (incr > 0 ? (upper < lower) : (lower < upper)) {
      // The loop is illegal.
      // Some zero-trip loops maintained by compiler, e.g.:
      //   for(i=10;i<0;++i) // lower >= upper - run-time check
      //   for(i=0;i>10;--i) // lower <= upper - run-time check
      //   for(i=0;i>10;++i) // incr > 0       - compile-time check
      //   for(i=10;i<0;--i) // incr < 0       - compile-time check
      // Compiler does not check the following illegal loops:
      //   for(i=0;i<10;i+=incr) // where incr<0
      //   for(i=10;i>0;i-=incr) // where incr<0
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrIllegal, ct_pdo, loc);
    }
  }
  th = __kmp_threads[gtid];
  team = th->th.th_team;
#if OMP_40_ENABLED
  KMP_DEBUG_ASSERT(th->th.th_teams_microtask); // we are in the teams construct
  nteams = th->th.th_teams_size.nteams;
#endif
  team_id = team->t.t_master_tid;
  KMP_DEBUG_ASSERT(nteams == (kmp_uint32)team->t.t_parent->t.t_nproc);

  // compute trip count
  if (incr == 1) {
    trip_count = upper - lower + 1;
  } else if (incr == -1) {
    trip_count = lower - upper + 1;
  } else if (incr > 0) {
    // upper-lower can exceed the limit of signed type
    trip_count = (UT)(upper - lower) / incr + 1;
  } else {
    trip_count = (UT)(lower - upper) / (-incr) + 1;
  }
  if (chunk < 1)
    chunk = 1;
  span = chunk * incr;
  *p_st = span * nteams;
  *p_lb = lower + (span * team_id);
  *p_ub = *p_lb + span - incr;
  if (p_last != NULL)
    *p_last = (team_id == ((trip_count - 1) / (UT)chunk) % nteams);
  // Correct upper bound if needed
  if (incr > 0) {
    if (*p_ub < *p_lb) // overflow?
      *p_ub = traits_t<T>::max_value;
    if (*p_ub > upper)
      *p_ub = upper; // tracker C73258
  } else { // incr < 0
    if (*p_ub > *p_lb)
      *p_ub = traits_t<T>::min_value;
    if (*p_ub < upper)
      *p_ub = upper; // tracker C73258
  }
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff =
        __kmp_str_format("__kmp_team_static_init exit: T#%%d team%%u liter=%%d "
                         "iter=(%%%s, %%%s, %%%s) chunk %%%s\n",
                         traits_t<T>::spec, traits_t<T>::spec,
                         traits_t<ST>::spec, traits_t<ST>::spec);
    KD_TRACE(100, (buff, gtid, team_id, *p_last, *p_lb, *p_ub, *p_st, chunk));
    __kmp_str_free(&buff);
  }
#endif
}

//------------------------------------------------------------------------------
extern "C" {
/*!
@ingroup WORK_SHARING
@param    loc       Source code location
@param    gtid      Global thread id of this thread
@param    schedtype  Scheduling type
@param    plastiter Pointer to the "last iteration" flag
@param    plower    Pointer to the lower bound
@param    pupper    Pointer to the upper bound
@param    pstride   Pointer to the stride
@param    incr      Loop increment
@param    chunk     The chunk size

Each of the four functions here are identical apart from the argument types.

The functions compute the upper and lower bounds and stride to be used for the
set of iterations to be executed by the current thread from the statically
scheduled loop that is described by the initial values of the bounds, stride,
increment and chunk size.

@{
*/
void __kmpc_for_static_init_4(ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype,
                              kmp_int32 *plastiter, kmp_int32 *plower,
                              kmp_int32 *pupper, kmp_int32 *pstride,
                              kmp_int32 incr, kmp_int32 chunk) {
  __kmp_for_static_init<kmp_int32>(loc, gtid, schedtype, plastiter, plower,
                                   pupper, pstride, incr, chunk
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                   ,
                                   OMPT_GET_RETURN_ADDRESS(0)
#endif
                                       );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void __kmpc_for_static_init_4u(ident_t *loc, kmp_int32 gtid,
                               kmp_int32 schedtype, kmp_int32 *plastiter,
                               kmp_uint32 *plower, kmp_uint32 *pupper,
                               kmp_int32 *pstride, kmp_int32 incr,
                               kmp_int32 chunk) {
  __kmp_for_static_init<kmp_uint32>(loc, gtid, schedtype, plastiter, plower,
                                    pupper, pstride, incr, chunk
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                    ,
                                    OMPT_GET_RETURN_ADDRESS(0)
#endif
                                        );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void __kmpc_for_static_init_8(ident_t *loc, kmp_int32 gtid, kmp_int32 schedtype,
                              kmp_int32 *plastiter, kmp_int64 *plower,
                              kmp_int64 *pupper, kmp_int64 *pstride,
                              kmp_int64 incr, kmp_int64 chunk) {
  __kmp_for_static_init<kmp_int64>(loc, gtid, schedtype, plastiter, plower,
                                   pupper, pstride, incr, chunk
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                   ,
                                   OMPT_GET_RETURN_ADDRESS(0)
#endif
                                       );
}

/*!
 See @ref __kmpc_for_static_init_4
 */
void __kmpc_for_static_init_8u(ident_t *loc, kmp_int32 gtid,
                               kmp_int32 schedtype, kmp_int32 *plastiter,
                               kmp_uint64 *plower, kmp_uint64 *pupper,
                               kmp_int64 *pstride, kmp_int64 incr,
                               kmp_int64 chunk) {
  __kmp_for_static_init<kmp_uint64>(loc, gtid, schedtype, plastiter, plower,
                                    pupper, pstride, incr, chunk
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                    ,
                                    OMPT_GET_RETURN_ADDRESS(0)
#endif
                                        );
}
/*!
@}
*/

/*!
@ingroup WORK_SHARING
@param    loc       Source code location
@param    gtid      Global thread id of this thread
@param    schedule  Scheduling type for the parallel loop
@param    plastiter Pointer to the "last iteration" flag
@param    plower    Pointer to the lower bound
@param    pupper    Pointer to the upper bound of loop chunk
@param    pupperD   Pointer to the upper bound of dist_chunk
@param    pstride   Pointer to the stride for parallel loop
@param    incr      Loop increment
@param    chunk     The chunk size for the parallel loop

Each of the four functions here are identical apart from the argument types.

The functions compute the upper and lower bounds and strides to be used for the
set of iterations to be executed by the current thread from the statically
scheduled loop that is described by the initial values of the bounds, strides,
increment and chunks for parallel loop and distribute constructs.

@{
*/
void __kmpc_dist_for_static_init_4(ident_t *loc, kmp_int32 gtid,
                                   kmp_int32 schedule, kmp_int32 *plastiter,
                                   kmp_int32 *plower, kmp_int32 *pupper,
                                   kmp_int32 *pupperD, kmp_int32 *pstride,
                                   kmp_int32 incr, kmp_int32 chunk) {
  __kmp_dist_for_static_init<kmp_int32>(loc, gtid, schedule, plastiter, plower,
                                        pupper, pupperD, pstride, incr, chunk);
}

/*!
 See @ref __kmpc_dist_for_static_init_4
 */
void __kmpc_dist_for_static_init_4u(ident_t *loc, kmp_int32 gtid,
                                    kmp_int32 schedule, kmp_int32 *plastiter,
                                    kmp_uint32 *plower, kmp_uint32 *pupper,
                                    kmp_uint32 *pupperD, kmp_int32 *pstride,
                                    kmp_int32 incr, kmp_int32 chunk) {
  __kmp_dist_for_static_init<kmp_uint32>(loc, gtid, schedule, plastiter, plower,
                                         pupper, pupperD, pstride, incr, chunk);
}

/*!
 See @ref __kmpc_dist_for_static_init_4
 */
void __kmpc_dist_for_static_init_8(ident_t *loc, kmp_int32 gtid,
                                   kmp_int32 schedule, kmp_int32 *plastiter,
                                   kmp_int64 *plower, kmp_int64 *pupper,
                                   kmp_int64 *pupperD, kmp_int64 *pstride,
                                   kmp_int64 incr, kmp_int64 chunk) {
  __kmp_dist_for_static_init<kmp_int64>(loc, gtid, schedule, plastiter, plower,
                                        pupper, pupperD, pstride, incr, chunk);
}

/*!
 See @ref __kmpc_dist_for_static_init_4
 */
void __kmpc_dist_for_static_init_8u(ident_t *loc, kmp_int32 gtid,
                                    kmp_int32 schedule, kmp_int32 *plastiter,
                                    kmp_uint64 *plower, kmp_uint64 *pupper,
                                    kmp_uint64 *pupperD, kmp_int64 *pstride,
                                    kmp_int64 incr, kmp_int64 chunk) {
  __kmp_dist_for_static_init<kmp_uint64>(loc, gtid, schedule, plastiter, plower,
                                         pupper, pupperD, pstride, incr, chunk);
}
/*!
@}
*/

//------------------------------------------------------------------------------
// Auxiliary routines for Distribute Parallel Loop construct implementation
//    Transfer call to template< type T >
//    __kmp_team_static_init( ident_t *loc, int gtid,
//        int *p_last, T *lb, T *ub, ST *st, ST incr, ST chunk )

/*!
@ingroup WORK_SHARING
@{
@param loc Source location
@param gtid Global thread id
@param p_last pointer to last iteration flag
@param p_lb  pointer to Lower bound
@param p_ub  pointer to Upper bound
@param p_st  Step (or increment if you prefer)
@param incr  Loop increment
@param chunk The chunk size to block with

The functions compute the upper and lower bounds and stride to be used for the
set of iterations to be executed by the current team from the statically
scheduled loop that is described by the initial values of the bounds, stride,
increment and chunk for the distribute construct as part of composite distribute
parallel loop construct. These functions are all identical apart from the types
of the arguments.
*/

void __kmpc_team_static_init_4(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                               kmp_int32 *p_lb, kmp_int32 *p_ub,
                               kmp_int32 *p_st, kmp_int32 incr,
                               kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  __kmp_team_static_init<kmp_int32>(loc, gtid, p_last, p_lb, p_ub, p_st, incr,
                                    chunk);
}

/*!
 See @ref __kmpc_team_static_init_4
 */
void __kmpc_team_static_init_4u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                                kmp_uint32 *p_lb, kmp_uint32 *p_ub,
                                kmp_int32 *p_st, kmp_int32 incr,
                                kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  __kmp_team_static_init<kmp_uint32>(loc, gtid, p_last, p_lb, p_ub, p_st, incr,
                                     chunk);
}

/*!
 See @ref __kmpc_team_static_init_4
 */
void __kmpc_team_static_init_8(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                               kmp_int64 *p_lb, kmp_int64 *p_ub,
                               kmp_int64 *p_st, kmp_int64 incr,
                               kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  __kmp_team_static_init<kmp_int64>(loc, gtid, p_last, p_lb, p_ub, p_st, incr,
                                    chunk);
}

/*!
 See @ref __kmpc_team_static_init_4
 */
void __kmpc_team_static_init_8u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                                kmp_uint64 *p_lb, kmp_uint64 *p_ub,
                                kmp_int64 *p_st, kmp_int64 incr,
                                kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
  __kmp_team_static_init<kmp_uint64>(loc, gtid, p_last, p_lb, p_ub, p_st, incr,
                                     chunk);
}
/*!
@}
*/

} // extern "C"
