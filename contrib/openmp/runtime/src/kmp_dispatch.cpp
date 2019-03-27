/*
 * kmp_dispatch.cpp: dynamic scheduling - iteration initialization and dispatch.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

/* Dynamic scheduling initialization and dispatch.
 *
 * NOTE: __kmp_nth is a constant inside of any dispatch loop, however
 *       it may change values between parallel regions.  __kmp_max_nth
 *       is the largest value __kmp_nth may take, 1 is the smallest.
 */

#include "kmp.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_itt.h"
#include "kmp_stats.h"
#include "kmp_str.h"
#if KMP_USE_X87CONTROL
#include <float.h>
#endif
#include "kmp_lock.h"
#include "kmp_dispatch.h"
#if KMP_USE_HIER_SCHED
#include "kmp_dispatch_hier.h"
#endif

#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

void __kmp_dispatch_deo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(gtid_ref);

  if (__kmp_env_consistency_check) {
    th = __kmp_threads[*gtid_ref];
    if (th->th.th_root->r.r_active &&
        (th->th.th_dispatch->th_dispatch_pr_current->pushed_ws != ct_none)) {
#if KMP_USE_DYNAMIC_LOCK
      __kmp_push_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref, NULL, 0);
#else
      __kmp_push_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref, NULL);
#endif
    }
  }
}

void __kmp_dispatch_dxo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  kmp_info_t *th;

  if (__kmp_env_consistency_check) {
    th = __kmp_threads[*gtid_ref];
    if (th->th.th_dispatch->th_dispatch_pr_current->pushed_ws != ct_none) {
      __kmp_pop_sync(*gtid_ref, ct_ordered_in_pdo, loc_ref);
    }
  }
}

// Initialize a dispatch_private_info_template<T> buffer for a particular
// type of schedule,chunk.  The loop description is found in lb (lower bound),
// ub (upper bound), and st (stride).  nproc is the number of threads relevant
// to the scheduling (often the number of threads in a team, but not always if
// hierarchical scheduling is used).  tid is the id of the thread calling
// the function within the group of nproc threads.  It will have a value
// between 0 and nproc - 1.  This is often just the thread id within a team, but
// is not necessarily the case when using hierarchical scheduling.
// loc is the source file location of the corresponding loop
// gtid is the global thread id
template <typename T>
void __kmp_dispatch_init_algorithm(ident_t *loc, int gtid,
                                   dispatch_private_info_template<T> *pr,
                                   enum sched_type schedule, T lb, T ub,
                                   typename traits_t<T>::signed_t st,
#if USE_ITT_BUILD
                                   kmp_uint64 *cur_chunk,
#endif
                                   typename traits_t<T>::signed_t chunk,
                                   T nproc, T tid) {
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::floating_t DBL;

  int active;
  T tc;
  kmp_info_t *th;
  kmp_team_t *team;

#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_init_algorithm: T#%%d called "
                            "pr:%%p lb:%%%s ub:%%%s st:%%%s "
                            "schedule:%%d chunk:%%%s nproc:%%%s tid:%%%s\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<ST>::spec,
                            traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr, lb, ub, st, schedule, chunk, nproc, tid));
    __kmp_str_free(&buff);
  }
#endif
  /* setup data */
  th = __kmp_threads[gtid];
  team = th->th.th_team;
  active = !team->t.t_serialized;

#if USE_ITT_BUILD
  int itt_need_metadata_reporting = __itt_metadata_add_ptr &&
                                    __kmp_forkjoin_frames_mode == 3 &&
                                    KMP_MASTER_GTID(gtid) &&
#if OMP_40_ENABLED
                                    th->th.th_teams_microtask == NULL &&
#endif
                                    team->t.t_active_level == 1;
#endif
#if (KMP_STATIC_STEAL_ENABLED)
  if (SCHEDULE_HAS_NONMONOTONIC(schedule))
    // AC: we now have only one implementation of stealing, so use it
    schedule = kmp_sch_static_steal;
  else
#endif
    schedule = SCHEDULE_WITHOUT_MODIFIERS(schedule);

  /* Pick up the nomerge/ordered bits from the scheduling type */
  if ((schedule >= kmp_nm_lower) && (schedule < kmp_nm_upper)) {
    pr->flags.nomerge = TRUE;
    schedule =
        (enum sched_type)(((int)schedule) - (kmp_nm_lower - kmp_sch_lower));
  } else {
    pr->flags.nomerge = FALSE;
  }
  pr->type_size = traits_t<T>::type_size; // remember the size of variables
  if (kmp_ord_lower & schedule) {
    pr->flags.ordered = TRUE;
    schedule =
        (enum sched_type)(((int)schedule) - (kmp_ord_lower - kmp_sch_lower));
  } else {
    pr->flags.ordered = FALSE;
  }

  if (schedule == kmp_sch_static) {
    schedule = __kmp_static;
  } else {
    if (schedule == kmp_sch_runtime) {
      // Use the scheduling specified by OMP_SCHEDULE (or __kmp_sch_default if
      // not specified)
      schedule = team->t.t_sched.r_sched_type;
      // Detail the schedule if needed (global controls are differentiated
      // appropriately)
      if (schedule == kmp_sch_guided_chunked) {
        schedule = __kmp_guided;
      } else if (schedule == kmp_sch_static) {
        schedule = __kmp_static;
      }
      // Use the chunk size specified by OMP_SCHEDULE (or default if not
      // specified)
      chunk = team->t.t_sched.chunk;
#if USE_ITT_BUILD
      if (cur_chunk)
        *cur_chunk = chunk;
#endif
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_init_algorithm: T#%%d new: "
                                "schedule:%%d chunk:%%%s\n",
                                traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    } else {
      if (schedule == kmp_sch_guided_chunked) {
        schedule = __kmp_guided;
      }
      if (chunk <= 0) {
        chunk = KMP_DEFAULT_CHUNK;
      }
    }

    if (schedule == kmp_sch_auto) {
      // mapping and differentiation: in the __kmp_do_serial_initialize()
      schedule = __kmp_auto;
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_init_algorithm: kmp_sch_auto: T#%%d new: "
            "schedule:%%d chunk:%%%s\n",
            traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    }

    /* guided analytical not safe for too many threads */
    if (schedule == kmp_sch_guided_analytical_chunked && nproc > 1 << 20) {
      schedule = kmp_sch_guided_iterative_chunked;
      KMP_WARNING(DispatchManyThreads);
    }
#if OMP_45_ENABLED
    if (schedule == kmp_sch_runtime_simd) {
      // compiler provides simd_width in the chunk parameter
      schedule = team->t.t_sched.r_sched_type;
      // Detail the schedule if needed (global controls are differentiated
      // appropriately)
      if (schedule == kmp_sch_static || schedule == kmp_sch_auto ||
          schedule == __kmp_static) {
        schedule = kmp_sch_static_balanced_chunked;
      } else {
        if (schedule == kmp_sch_guided_chunked || schedule == __kmp_guided) {
          schedule = kmp_sch_guided_simd;
        }
        chunk = team->t.t_sched.chunk * chunk;
      }
#if USE_ITT_BUILD
      if (cur_chunk)
        *cur_chunk = chunk;
#endif
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_init: T#%%d new: schedule:%%d"
                                " chunk:%%%s\n",
                                traits_t<ST>::spec);
        KD_TRACE(10, (buff, gtid, schedule, chunk));
        __kmp_str_free(&buff);
      }
#endif
    }
#endif // OMP_45_ENABLED
    pr->u.p.parm1 = chunk;
  }
  KMP_ASSERT2((kmp_sch_lower < schedule && schedule < kmp_sch_upper),
              "unknown scheduling type");

  pr->u.p.count = 0;

  if (__kmp_env_consistency_check) {
    if (st == 0) {
      __kmp_error_construct(kmp_i18n_msg_CnsLoopIncrZeroProhibited,
                            (pr->flags.ordered ? ct_pdo_ordered : ct_pdo), loc);
    }
  }
  // compute trip count
  if (st == 1) { // most common case
    if (ub >= lb) {
      tc = ub - lb + 1;
    } else { // ub < lb
      tc = 0; // zero-trip
    }
  } else if (st < 0) {
    if (lb >= ub) {
      // AC: cast to unsigned is needed for loops like (i=2B; i>-2B; i-=1B),
      // where the division needs to be unsigned regardless of the result type
      tc = (UT)(lb - ub) / (-st) + 1;
    } else { // lb < ub
      tc = 0; // zero-trip
    }
  } else { // st > 0
    if (ub >= lb) {
      // AC: cast to unsigned is needed for loops like (i=-2B; i<2B; i+=1B),
      // where the division needs to be unsigned regardless of the result type
      tc = (UT)(ub - lb) / st + 1;
    } else { // ub < lb
      tc = 0; // zero-trip
    }
  }

  pr->u.p.lb = lb;
  pr->u.p.ub = ub;
  pr->u.p.st = st;
  pr->u.p.tc = tc;

#if KMP_OS_WINDOWS
  pr->u.p.last_upper = ub + st;
#endif /* KMP_OS_WINDOWS */

  /* NOTE: only the active parallel region(s) has active ordered sections */

  if (active) {
    if (pr->flags.ordered) {
      pr->ordered_bumped = 0;
      pr->u.p.ordered_lower = 1;
      pr->u.p.ordered_upper = 0;
    }
  }

  switch (schedule) {
#if (KMP_STATIC_STEAL_ENABLED)
  case kmp_sch_static_steal: {
    T ntc, init;

    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_steal case\n",
              gtid));

    ntc = (tc % chunk ? 1 : 0) + tc / chunk;
    if (nproc > 1 && ntc >= nproc) {
      KMP_COUNT_BLOCK(OMP_LOOP_STATIC_STEAL);
      T id = tid;
      T small_chunk, extras;

      small_chunk = ntc / nproc;
      extras = ntc % nproc;

      init = id * small_chunk + (id < extras ? id : extras);
      pr->u.p.count = init;
      pr->u.p.ub = init + small_chunk + (id < extras ? 1 : 0);

      pr->u.p.parm2 = lb;
      // pr->pfields.parm3 = 0; // it's not used in static_steal
      pr->u.p.parm4 = (id + 1) % nproc; // remember neighbour tid
      pr->u.p.st = st;
      if (traits_t<T>::type_size > 4) {
        // AC: TODO: check if 16-byte CAS available and use it to
        // improve performance (probably wait for explicit request
        // before spending time on this).
        // For now use dynamically allocated per-thread lock,
        // free memory in __kmp_dispatch_next when status==0.
        KMP_DEBUG_ASSERT(th->th.th_dispatch->th_steal_lock == NULL);
        th->th.th_dispatch->th_steal_lock =
            (kmp_lock_t *)__kmp_allocate(sizeof(kmp_lock_t));
        __kmp_init_lock(th->th.th_dispatch->th_steal_lock);
      }
      break;
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_balanced\n",
                     gtid));
      schedule = kmp_sch_static_balanced;
      /* too few iterations: fall-through to kmp_sch_static_balanced */
    } // if
    /* FALL-THROUGH to static balanced */
  } // case
#endif
  case kmp_sch_static_balanced: {
    T init, limit;

    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_balanced case\n",
         gtid));

    if (nproc > 1) {
      T id = tid;

      if (tc < nproc) {
        if (id < tc) {
          init = id;
          limit = id;
          pr->u.p.parm1 = (id == tc - 1); /* parm1 stores *plastiter */
        } else {
          pr->u.p.count = 1; /* means no more chunks to execute */
          pr->u.p.parm1 = FALSE;
          break;
        }
      } else {
        T small_chunk = tc / nproc;
        T extras = tc % nproc;
        init = id * small_chunk + (id < extras ? id : extras);
        limit = init + small_chunk - (id < extras ? 0 : 1);
        pr->u.p.parm1 = (id == nproc - 1);
      }
    } else {
      if (tc > 0) {
        init = 0;
        limit = tc - 1;
        pr->u.p.parm1 = TRUE;
      } else {
        // zero trip count
        pr->u.p.count = 1; /* means no more chunks to execute */
        pr->u.p.parm1 = FALSE;
        break;
      }
    }
#if USE_ITT_BUILD
    // Calculate chunk for metadata report
    if (itt_need_metadata_reporting)
      if (cur_chunk)
        *cur_chunk = limit - init + 1;
#endif
    if (st == 1) {
      pr->u.p.lb = lb + init;
      pr->u.p.ub = lb + limit;
    } else {
      // calculated upper bound, "ub" is user-defined upper bound
      T ub_tmp = lb + limit * st;
      pr->u.p.lb = lb + init * st;
      // adjust upper bound to "ub" if needed, so that MS lastprivate will match
      // it exactly
      if (st > 0) {
        pr->u.p.ub = (ub_tmp + st > ub ? ub : ub_tmp);
      } else {
        pr->u.p.ub = (ub_tmp + st < ub ? ub : ub_tmp);
      }
    }
    if (pr->flags.ordered) {
      pr->u.p.ordered_lower = init;
      pr->u.p.ordered_upper = limit;
    }
    break;
  } // case
#if OMP_45_ENABLED
  case kmp_sch_static_balanced_chunked: {
    // similar to balanced, but chunk adjusted to multiple of simd width
    T nth = nproc;
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d runtime(simd:static)"
                   " -> falling-through to static_greedy\n",
                   gtid));
    schedule = kmp_sch_static_greedy;
    if (nth > 1)
      pr->u.p.parm1 = ((tc + nth - 1) / nth + chunk - 1) & ~(chunk - 1);
    else
      pr->u.p.parm1 = tc;
    break;
  } // case
  case kmp_sch_guided_simd:
#endif // OMP_45_ENABLED
  case kmp_sch_guided_iterative_chunked: {
    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_guided_iterative_chunked"
         " case\n",
         gtid));

    if (nproc > 1) {
      if ((2L * chunk + 1) * nproc >= tc) {
        /* chunk size too large, switch to dynamic */
        schedule = kmp_sch_dynamic_chunked;
      } else {
        // when remaining iters become less than parm2 - switch to dynamic
        pr->u.p.parm2 = guided_int_param * nproc * (chunk + 1);
        *(double *)&pr->u.p.parm3 =
            guided_flt_param / nproc; // may occupy parm3 and parm4
      }
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_greedy\n",
                     gtid));
      schedule = kmp_sch_static_greedy;
      /* team->t.t_nproc == 1: fall-through to kmp_sch_static_greedy */
      KD_TRACE(
          100,
          ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_greedy case\n",
           gtid));
      pr->u.p.parm1 = tc;
    } // if
  } // case
  break;
  case kmp_sch_guided_analytical_chunked: {
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d "
                   "kmp_sch_guided_analytical_chunked case\n",
                   gtid));

    if (nproc > 1) {
      if ((2L * chunk + 1) * nproc >= tc) {
        /* chunk size too large, switch to dynamic */
        schedule = kmp_sch_dynamic_chunked;
      } else {
        /* commonly used term: (2 nproc - 1)/(2 nproc) */
        DBL x;

#if KMP_USE_X87CONTROL
        /* Linux* OS already has 64-bit computation by default for long double,
           and on Windows* OS on Intel(R) 64, /Qlong_double doesn't work. On
           Windows* OS on IA-32 architecture, we need to set precision to 64-bit
           instead of the default 53-bit. Even though long double doesn't work
           on Windows* OS on Intel(R) 64, the resulting lack of precision is not
           expected to impact the correctness of the algorithm, but this has not
           been mathematically proven. */
        // save original FPCW and set precision to 64-bit, as
        // Windows* OS on IA-32 architecture defaults to 53-bit
        unsigned int oldFpcw = _control87(0, 0);
        _control87(_PC_64, _MCW_PC); // 0,0x30000
#endif
        /* value used for comparison in solver for cross-over point */
        long double target = ((long double)chunk * 2 + 1) * nproc / tc;

        /* crossover point--chunk indexes equal to or greater than
           this point switch to dynamic-style scheduling */
        UT cross;

        /* commonly used term: (2 nproc - 1)/(2 nproc) */
        x = (long double)1.0 - (long double)0.5 / nproc;

#ifdef KMP_DEBUG
        { // test natural alignment
          struct _test_a {
            char a;
            union {
              char b;
              DBL d;
            };
          } t;
          ptrdiff_t natural_alignment =
              (ptrdiff_t)&t.b - (ptrdiff_t)&t - (ptrdiff_t)1;
          //__kmp_warn( " %llx %llx %lld", (long long)&t.d, (long long)&t, (long
          // long)natural_alignment );
          KMP_DEBUG_ASSERT(
              (((ptrdiff_t)&pr->u.p.parm3) & (natural_alignment)) == 0);
        }
#endif // KMP_DEBUG

        /* save the term in thread private dispatch structure */
        *(DBL *)&pr->u.p.parm3 = x;

        /* solve for the crossover point to the nearest integer i for which C_i
           <= chunk */
        {
          UT left, right, mid;
          long double p;

          /* estimate initial upper and lower bound */

          /* doesn't matter what value right is as long as it is positive, but
             it affects performance of the solver */
          right = 229;
          p = __kmp_pow<UT>(x, right);
          if (p > target) {
            do {
              p *= p;
              right <<= 1;
            } while (p > target && right < (1 << 27));
            /* lower bound is previous (failed) estimate of upper bound */
            left = right >> 1;
          } else {
            left = 0;
          }

          /* bisection root-finding method */
          while (left + 1 < right) {
            mid = (left + right) / 2;
            if (__kmp_pow<UT>(x, mid) > target) {
              left = mid;
            } else {
              right = mid;
            }
          } // while
          cross = right;
        }
        /* assert sanity of computed crossover point */
        KMP_ASSERT(cross && __kmp_pow<UT>(x, cross - 1) > target &&
                   __kmp_pow<UT>(x, cross) <= target);

        /* save the crossover point in thread private dispatch structure */
        pr->u.p.parm2 = cross;

// C75803
#if ((KMP_OS_LINUX || KMP_OS_WINDOWS) && KMP_ARCH_X86) && (!defined(KMP_I8))
#define GUIDED_ANALYTICAL_WORKAROUND (*(DBL *)&pr->u.p.parm3)
#else
#define GUIDED_ANALYTICAL_WORKAROUND (x)
#endif
        /* dynamic-style scheduling offset */
        pr->u.p.count = tc - __kmp_dispatch_guided_remaining(
                                 tc, GUIDED_ANALYTICAL_WORKAROUND, cross) -
                        cross * chunk;
#if KMP_USE_X87CONTROL
        // restore FPCW
        _control87(oldFpcw, _MCW_PC);
#endif
      } // if
    } else {
      KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d falling-through to "
                     "kmp_sch_static_greedy\n",
                     gtid));
      schedule = kmp_sch_static_greedy;
      /* team->t.t_nproc == 1: fall-through to kmp_sch_static_greedy */
      pr->u.p.parm1 = tc;
    } // if
  } // case
  break;
  case kmp_sch_static_greedy:
    KD_TRACE(
        100,
        ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_static_greedy case\n",
         gtid));
    pr->u.p.parm1 = (nproc > 1) ? (tc + nproc - 1) / nproc : tc;
    break;
  case kmp_sch_static_chunked:
  case kmp_sch_dynamic_chunked:
    if (pr->u.p.parm1 <= 0) {
      pr->u.p.parm1 = KMP_DEFAULT_CHUNK;
    }
    KD_TRACE(100, ("__kmp_dispatch_init_algorithm: T#%d "
                   "kmp_sch_static_chunked/kmp_sch_dynamic_chunked cases\n",
                   gtid));
    break;
  case kmp_sch_trapezoidal: {
    /* TSS: trapezoid self-scheduling, minimum chunk_size = parm1 */

    T parm1, parm2, parm3, parm4;
    KD_TRACE(100,
             ("__kmp_dispatch_init_algorithm: T#%d kmp_sch_trapezoidal case\n",
              gtid));

    parm1 = chunk;

    /* F : size of the first cycle */
    parm2 = (tc / (2 * nproc));

    if (parm2 < 1) {
      parm2 = 1;
    }

    /* L : size of the last cycle.  Make sure the last cycle is not larger
       than the first cycle. */
    if (parm1 < 1) {
      parm1 = 1;
    } else if (parm1 > parm2) {
      parm1 = parm2;
    }

    /* N : number of cycles */
    parm3 = (parm2 + parm1);
    parm3 = (2 * tc + parm3 - 1) / parm3;

    if (parm3 < 2) {
      parm3 = 2;
    }

    /* sigma : decreasing incr of the trapezoid */
    parm4 = (parm3 - 1);
    parm4 = (parm2 - parm1) / parm4;

    // pointless check, because parm4 >= 0 always
    // if ( parm4 < 0 ) {
    //    parm4 = 0;
    //}

    pr->u.p.parm1 = parm1;
    pr->u.p.parm2 = parm2;
    pr->u.p.parm3 = parm3;
    pr->u.p.parm4 = parm4;
  } // case
  break;

  default: {
    __kmp_fatal(KMP_MSG(UnknownSchedTypeDetected), // Primary message
                KMP_HNT(GetNewerLibrary), // Hint
                __kmp_msg_null // Variadic argument list terminator
                );
  } break;
  } // switch
  pr->schedule = schedule;
}

#if KMP_USE_HIER_SCHED
template <typename T>
inline void __kmp_dispatch_init_hier_runtime(ident_t *loc, T lb, T ub,
                                             typename traits_t<T>::signed_t st);
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_int32>(ident_t *loc, kmp_int32 lb,
                                            kmp_int32 ub, kmp_int32 st) {
  __kmp_dispatch_init_hierarchy<kmp_int32>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.small_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_uint32>(ident_t *loc, kmp_uint32 lb,
                                             kmp_uint32 ub, kmp_int32 st) {
  __kmp_dispatch_init_hierarchy<kmp_uint32>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.small_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_int64>(ident_t *loc, kmp_int64 lb,
                                            kmp_int64 ub, kmp_int64 st) {
  __kmp_dispatch_init_hierarchy<kmp_int64>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.large_chunks, lb, ub, st);
}
template <>
inline void
__kmp_dispatch_init_hier_runtime<kmp_uint64>(ident_t *loc, kmp_uint64 lb,
                                             kmp_uint64 ub, kmp_int64 st) {
  __kmp_dispatch_init_hierarchy<kmp_uint64>(
      loc, __kmp_hier_scheds.size, __kmp_hier_scheds.layers,
      __kmp_hier_scheds.scheds, __kmp_hier_scheds.large_chunks, lb, ub, st);
}

// free all the hierarchy scheduling memory associated with the team
void __kmp_dispatch_free_hierarchies(kmp_team_t *team) {
  int num_disp_buff = team->t.t_max_nproc > 1 ? __kmp_dispatch_num_buffers : 2;
  for (int i = 0; i < num_disp_buff; ++i) {
    // type does not matter here so use kmp_int32
    auto sh =
        reinterpret_cast<dispatch_shared_info_template<kmp_int32> volatile *>(
            &team->t.t_disp_buffer[i]);
    if (sh->hier) {
      sh->hier->deallocate();
      __kmp_free(sh->hier);
    }
  }
}
#endif

// UT - unsigned flavor of T, ST - signed flavor of T,
// DBL - double if sizeof(T)==4, or long double if sizeof(T)==8
template <typename T>
static void
__kmp_dispatch_init(ident_t *loc, int gtid, enum sched_type schedule, T lb,
                    T ub, typename traits_t<T>::signed_t st,
                    typename traits_t<T>::signed_t chunk, int push_ws) {
  typedef typename traits_t<T>::unsigned_t UT;

  int active;
  kmp_info_t *th;
  kmp_team_t *team;
  kmp_uint32 my_buffer_index;
  dispatch_private_info_template<T> *pr;
  dispatch_shared_info_template<T> volatile *sh;

  KMP_BUILD_ASSERT(sizeof(dispatch_private_info_template<T>) ==
                   sizeof(dispatch_private_info));
  KMP_BUILD_ASSERT(sizeof(dispatch_shared_info_template<UT>) ==
                   sizeof(dispatch_shared_info));

  if (!TCR_4(__kmp_init_parallel))
    __kmp_parallel_initialize();

#if INCLUDE_SSC_MARKS
  SSC_MARK_DISPATCH_INIT();
#endif
#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_init: T#%%d called: schedule:%%d "
                            "chunk:%%%s lb:%%%s ub:%%%s st:%%%s\n",
                            traits_t<ST>::spec, traits_t<T>::spec,
                            traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, schedule, chunk, lb, ub, st));
    __kmp_str_free(&buff);
  }
#endif
  /* setup data */
  th = __kmp_threads[gtid];
  team = th->th.th_team;
  active = !team->t.t_serialized;
  th->th.th_ident = loc;

  // Any half-decent optimizer will remove this test when the blocks are empty
  // since the macros expand to nothing
  // when statistics are disabled.
  if (schedule == __kmp_static) {
    KMP_COUNT_BLOCK(OMP_LOOP_STATIC);
  } else {
    KMP_COUNT_BLOCK(OMP_LOOP_DYNAMIC);
  }

#if KMP_USE_HIER_SCHED
  // Initialize the scheduling hierarchy if requested in OMP_SCHEDULE envirable
  // Hierarchical scheduling does not work with ordered, so if ordered is
  // detected, then revert back to threaded scheduling.
  bool ordered;
  enum sched_type my_sched = schedule;
  my_buffer_index = th->th.th_dispatch->th_disp_index;
  pr = reinterpret_cast<dispatch_private_info_template<T> *>(
      &th->th.th_dispatch
           ->th_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
  my_sched = SCHEDULE_WITHOUT_MODIFIERS(my_sched);
  if ((my_sched >= kmp_nm_lower) && (my_sched < kmp_nm_upper))
    my_sched =
        (enum sched_type)(((int)my_sched) - (kmp_nm_lower - kmp_sch_lower));
  ordered = (kmp_ord_lower & my_sched);
  if (pr->flags.use_hier) {
    if (ordered) {
      KD_TRACE(100, ("__kmp_dispatch_init: T#%d ordered loop detected.  "
                     "Disabling hierarchical scheduling.\n",
                     gtid));
      pr->flags.use_hier = FALSE;
    }
  }
  if (schedule == kmp_sch_runtime && __kmp_hier_scheds.size > 0) {
    // Don't use hierarchical for ordered parallel loops and don't
    // use the runtime hierarchy if one was specified in the program
    if (!ordered && !pr->flags.use_hier)
      __kmp_dispatch_init_hier_runtime<T>(loc, lb, ub, st);
  }
#endif // KMP_USE_HIER_SCHED

#if USE_ITT_BUILD
  kmp_uint64 cur_chunk = chunk;
  int itt_need_metadata_reporting = __itt_metadata_add_ptr &&
                                    __kmp_forkjoin_frames_mode == 3 &&
                                    KMP_MASTER_GTID(gtid) &&
#if OMP_40_ENABLED
                                    th->th.th_teams_microtask == NULL &&
#endif
                                    team->t.t_active_level == 1;
#endif
  if (!active) {
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_disp_buffer); /* top of the stack */
  } else {
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    my_buffer_index = th->th.th_dispatch->th_disp_index++;

    /* What happens when number of threads changes, need to resize buffer? */
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        &th->th.th_dispatch
             ->th_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
    sh = reinterpret_cast<dispatch_shared_info_template<T> volatile *>(
        &team->t.t_disp_buffer[my_buffer_index % __kmp_dispatch_num_buffers]);
    KD_TRACE(10, ("__kmp_dispatch_init: T#%d my_buffer_index:%d\n", gtid,
                  my_buffer_index));
  }

  __kmp_dispatch_init_algorithm(loc, gtid, pr, schedule, lb, ub, st,
#if USE_ITT_BUILD
                                &cur_chunk,
#endif
                                chunk, (T)th->th.th_team_nproc,
                                (T)th->th.th_info.ds.ds_tid);
  if (active) {
    if (pr->flags.ordered == 0) {
      th->th.th_dispatch->th_deo_fcn = __kmp_dispatch_deo_error;
      th->th.th_dispatch->th_dxo_fcn = __kmp_dispatch_dxo_error;
    } else {
      th->th.th_dispatch->th_deo_fcn = __kmp_dispatch_deo<UT>;
      th->th.th_dispatch->th_dxo_fcn = __kmp_dispatch_dxo<UT>;
    }
  }

  if (active) {
    /* The name of this buffer should be my_buffer_index when it's free to use
     * it */

    KD_TRACE(100, ("__kmp_dispatch_init: T#%d before wait: my_buffer_index:%d "
                   "sh->buffer_index:%d\n",
                   gtid, my_buffer_index, sh->buffer_index));
    __kmp_wait_yield<kmp_uint32>(&sh->buffer_index, my_buffer_index,
                                 __kmp_eq<kmp_uint32> USE_ITT_BUILD_ARG(NULL));
    // Note: KMP_WAIT_YIELD() cannot be used there: buffer index and
    // my_buffer_index are *always* 32-bit integers.
    KMP_MB(); /* is this necessary? */
    KD_TRACE(100, ("__kmp_dispatch_init: T#%d after wait: my_buffer_index:%d "
                   "sh->buffer_index:%d\n",
                   gtid, my_buffer_index, sh->buffer_index));

    th->th.th_dispatch->th_dispatch_pr_current = (dispatch_private_info_t *)pr;
    th->th.th_dispatch->th_dispatch_sh_current =
        CCAST(dispatch_shared_info_t *, (volatile dispatch_shared_info_t *)sh);
#if USE_ITT_BUILD
    if (pr->flags.ordered) {
      __kmp_itt_ordered_init(gtid);
    }
    // Report loop metadata
    if (itt_need_metadata_reporting) {
      // Only report metadata by master of active team at level 1
      kmp_uint64 schedtype = 0;
      switch (schedule) {
      case kmp_sch_static_chunked:
      case kmp_sch_static_balanced: // Chunk is calculated in the switch above
        break;
      case kmp_sch_static_greedy:
        cur_chunk = pr->u.p.parm1;
        break;
      case kmp_sch_dynamic_chunked:
        schedtype = 1;
        break;
      case kmp_sch_guided_iterative_chunked:
      case kmp_sch_guided_analytical_chunked:
#if OMP_45_ENABLED
      case kmp_sch_guided_simd:
#endif
        schedtype = 2;
        break;
      default:
        // Should we put this case under "static"?
        // case kmp_sch_static_steal:
        schedtype = 3;
        break;
      }
      __kmp_itt_metadata_loop(loc, schedtype, pr->u.p.tc, cur_chunk);
    }
#if KMP_USE_HIER_SCHED
    if (pr->flags.use_hier) {
      pr->u.p.count = 0;
      pr->u.p.ub = pr->u.p.lb = pr->u.p.st = pr->u.p.tc = 0;
    }
#endif // KMP_USER_HIER_SCHED
#endif /* USE_ITT_BUILD */
  }

#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_init: T#%%d returning: schedule:%%d ordered:%%%s "
        "lb:%%%s ub:%%%s"
        " st:%%%s tc:%%%s count:%%%s\n\tordered_lower:%%%s ordered_upper:%%%s"
        " parm1:%%%s parm2:%%%s parm3:%%%s parm4:%%%s\n",
        traits_t<UT>::spec, traits_t<T>::spec, traits_t<T>::spec,
        traits_t<ST>::spec, traits_t<UT>::spec, traits_t<UT>::spec,
        traits_t<UT>::spec, traits_t<UT>::spec, traits_t<T>::spec,
        traits_t<T>::spec, traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr->schedule, pr->flags.ordered, pr->u.p.lb,
                  pr->u.p.ub, pr->u.p.st, pr->u.p.tc, pr->u.p.count,
                  pr->u.p.ordered_lower, pr->u.p.ordered_upper, pr->u.p.parm1,
                  pr->u.p.parm2, pr->u.p.parm3, pr->u.p.parm4));
    __kmp_str_free(&buff);
  }
#endif
#if (KMP_STATIC_STEAL_ENABLED)
  // It cannot be guaranteed that after execution of a loop with some other
  // schedule kind all the parm3 variables will contain the same value. Even if
  // all parm3 will be the same, it still exists a bad case like using 0 and 1
  // rather than program life-time increment. So the dedicated variable is
  // required. The 'static_steal_counter' is used.
  if (schedule == kmp_sch_static_steal) {
    // Other threads will inspect this variable when searching for a victim.
    // This is a flag showing that other threads may steal from this thread
    // since then.
    volatile T *p = &pr->u.p.static_steal_counter;
    *p = *p + 1;
  }
#endif // ( KMP_STATIC_STEAL_ENABLED )

#if OMPT_SUPPORT && OMPT_OPTIONAL
  if (ompt_enabled.ompt_callback_work) {
    ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);
    ompt_task_info_t *task_info = __ompt_get_task_info_object(0);
    ompt_callbacks.ompt_callback(ompt_callback_work)(
        ompt_work_loop, ompt_scope_begin, &(team_info->parallel_data),
        &(task_info->task_data), pr->u.p.tc, OMPT_LOAD_RETURN_ADDRESS(gtid));
  }
#endif
  KMP_PUSH_PARTITIONED_TIMER(OMP_loop_dynamic);
}

/* For ordered loops, either __kmp_dispatch_finish() should be called after
 * every iteration, or __kmp_dispatch_finish_chunk() should be called after
 * every chunk of iterations.  If the ordered section(s) were not executed
 * for this iteration (or every iteration in this chunk), we need to set the
 * ordered iteration counters so that the next thread can proceed. */
template <typename UT>
static void __kmp_dispatch_finish(int gtid, ident_t *loc) {
  typedef typename traits_t<UT>::signed_t ST;
  kmp_info_t *th = __kmp_threads[gtid];

  KD_TRACE(100, ("__kmp_dispatch_finish: T#%d called\n", gtid));
  if (!th->th.th_team->t.t_serialized) {

    dispatch_private_info_template<UT> *pr =
        reinterpret_cast<dispatch_private_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_pr_current);
    dispatch_shared_info_template<UT> volatile *sh =
        reinterpret_cast<dispatch_shared_info_template<UT> volatile *>(
            th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(pr);
    KMP_DEBUG_ASSERT(sh);
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    if (pr->ordered_bumped) {
      KD_TRACE(
          1000,
          ("__kmp_dispatch_finish: T#%d resetting ordered_bumped to zero\n",
           gtid));
      pr->ordered_bumped = 0;
    } else {
      UT lower = pr->u.p.ordered_lower;

#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_finish: T#%%d before wait: "
                                "ordered_iteration:%%%s lower:%%%s\n",
                                traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower));
        __kmp_str_free(&buff);
      }
#endif

      __kmp_wait_yield<UT>(&sh->u.s.ordered_iteration, lower,
                           __kmp_ge<UT> USE_ITT_BUILD_ARG(NULL));
      KMP_MB(); /* is this necessary? */
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format("__kmp_dispatch_finish: T#%%d after wait: "
                                "ordered_iteration:%%%s lower:%%%s\n",
                                traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower));
        __kmp_str_free(&buff);
      }
#endif

      test_then_inc<ST>((volatile ST *)&sh->u.s.ordered_iteration);
    } // if
  } // if
  KD_TRACE(100, ("__kmp_dispatch_finish: T#%d returned\n", gtid));
}

#ifdef KMP_GOMP_COMPAT

template <typename UT>
static void __kmp_dispatch_finish_chunk(int gtid, ident_t *loc) {
  typedef typename traits_t<UT>::signed_t ST;
  kmp_info_t *th = __kmp_threads[gtid];

  KD_TRACE(100, ("__kmp_dispatch_finish_chunk: T#%d called\n", gtid));
  if (!th->th.th_team->t.t_serialized) {
    //        int cid;
    dispatch_private_info_template<UT> *pr =
        reinterpret_cast<dispatch_private_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_pr_current);
    dispatch_shared_info_template<UT> volatile *sh =
        reinterpret_cast<dispatch_shared_info_template<UT> volatile *>(
            th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(pr);
    KMP_DEBUG_ASSERT(sh);
    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    //        for (cid = 0; cid < KMP_MAX_ORDERED; ++cid) {
    UT lower = pr->u.p.ordered_lower;
    UT upper = pr->u.p.ordered_upper;
    UT inc = upper - lower + 1;

    if (pr->ordered_bumped == inc) {
      KD_TRACE(
          1000,
          ("__kmp_dispatch_finish: T#%d resetting ordered_bumped to zero\n",
           gtid));
      pr->ordered_bumped = 0;
    } else {
      inc -= pr->ordered_bumped;

#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_finish_chunk: T#%%d before wait: "
            "ordered_iteration:%%%s lower:%%%s upper:%%%s\n",
            traits_t<UT>::spec, traits_t<UT>::spec, traits_t<UT>::spec);
        KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower, upper));
        __kmp_str_free(&buff);
      }
#endif

      __kmp_wait_yield<UT>(&sh->u.s.ordered_iteration, lower,
                           __kmp_ge<UT> USE_ITT_BUILD_ARG(NULL));

      KMP_MB(); /* is this necessary? */
      KD_TRACE(1000, ("__kmp_dispatch_finish_chunk: T#%d resetting "
                      "ordered_bumped to zero\n",
                      gtid));
      pr->ordered_bumped = 0;
//!!!!! TODO check if the inc should be unsigned, or signed???
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_finish_chunk: T#%%d after wait: "
            "ordered_iteration:%%%s inc:%%%s lower:%%%s upper:%%%s\n",
            traits_t<UT>::spec, traits_t<UT>::spec, traits_t<UT>::spec,
            traits_t<UT>::spec);
        KD_TRACE(1000,
                 (buff, gtid, sh->u.s.ordered_iteration, inc, lower, upper));
        __kmp_str_free(&buff);
      }
#endif

      test_then_add<ST>((volatile ST *)&sh->u.s.ordered_iteration, inc);
    }
    //        }
  }
  KD_TRACE(100, ("__kmp_dispatch_finish_chunk: T#%d returned\n", gtid));
}

#endif /* KMP_GOMP_COMPAT */

template <typename T>
int __kmp_dispatch_next_algorithm(int gtid,
                                  dispatch_private_info_template<T> *pr,
                                  dispatch_shared_info_template<T> volatile *sh,
                                  kmp_int32 *p_last, T *p_lb, T *p_ub,
                                  typename traits_t<T>::signed_t *p_st, T nproc,
                                  T tid) {
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  typedef typename traits_t<T>::floating_t DBL;
  int status = 0;
  kmp_int32 last = 0;
  T start;
  ST incr;
  UT limit, trip, init;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;

  KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                   &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);
  KMP_DEBUG_ASSERT(pr);
  KMP_DEBUG_ASSERT(sh);
  KMP_DEBUG_ASSERT(tid >= 0 && tid < nproc);
#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff =
        __kmp_str_format("__kmp_dispatch_next_algorithm: T#%%d called pr:%%p "
                         "sh:%%p nproc:%%%s tid:%%%s\n",
                         traits_t<T>::spec, traits_t<T>::spec);
    KD_TRACE(10, (buff, gtid, pr, sh, nproc, tid));
    __kmp_str_free(&buff);
  }
#endif

  // zero trip count
  if (pr->u.p.tc == 0) {
    KD_TRACE(10,
             ("__kmp_dispatch_next_algorithm: T#%d early exit trip count is "
              "zero status:%d\n",
              gtid, status));
    return 0;
  }

  switch (pr->schedule) {
#if (KMP_STATIC_STEAL_ENABLED)
  case kmp_sch_static_steal: {
    T chunk = pr->u.p.parm1;

    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_static_steal case\n",
              gtid));

    trip = pr->u.p.tc - 1;

    if (traits_t<T>::type_size > 4) {
      // use lock for 8-byte and CAS for 4-byte induction
      // variable. TODO (optional): check and use 16-byte CAS
      kmp_lock_t *lck = th->th.th_dispatch->th_steal_lock;
      KMP_DEBUG_ASSERT(lck != NULL);
      if (pr->u.p.count < (UT)pr->u.p.ub) {
        __kmp_acquire_lock(lck, gtid);
        // try to get own chunk of iterations
        init = (pr->u.p.count)++;
        status = (init < (UT)pr->u.p.ub);
        __kmp_release_lock(lck, gtid);
      } else {
        status = 0; // no own chunks
      }
      if (!status) { // try to steal
        kmp_info_t **other_threads = team->t.t_threads;
        int while_limit = nproc; // nproc attempts to find a victim
        int while_index = 0;
        // TODO: algorithm of searching for a victim
        // should be cleaned up and measured
        while ((!status) && (while_limit != ++while_index)) {
          T remaining;
          T victimIdx = pr->u.p.parm4;
          T oldVictimIdx = victimIdx ? victimIdx - 1 : nproc - 1;
          dispatch_private_info_template<T> *victim =
              reinterpret_cast<dispatch_private_info_template<T> *>(
                  other_threads[victimIdx]
                      ->th.th_dispatch->th_dispatch_pr_current);
          while ((victim == NULL || victim == pr ||
                  (*(volatile T *)&victim->u.p.static_steal_counter !=
                   *(volatile T *)&pr->u.p.static_steal_counter)) &&
                 oldVictimIdx != victimIdx) {
            victimIdx = (victimIdx + 1) % nproc;
            victim = reinterpret_cast<dispatch_private_info_template<T> *>(
                other_threads[victimIdx]
                    ->th.th_dispatch->th_dispatch_pr_current);
          }
          if (!victim || (*(volatile T *)&victim->u.p.static_steal_counter !=
                          *(volatile T *)&pr->u.p.static_steal_counter)) {
            continue; // try once more (nproc attempts in total)
            // no victim is ready yet to participate in stealing
            // because all victims are still in kmp_init_dispatch
          }
          if (victim->u.p.count + 2 > (UT)victim->u.p.ub) {
            pr->u.p.parm4 = (victimIdx + 1) % nproc; // shift start tid
            continue; // not enough chunks to steal, goto next victim
          }

          lck = other_threads[victimIdx]->th.th_dispatch->th_steal_lock;
          KMP_ASSERT(lck != NULL);
          __kmp_acquire_lock(lck, gtid);
          limit = victim->u.p.ub; // keep initial ub
          if (victim->u.p.count >= limit ||
              (remaining = limit - victim->u.p.count) < 2) {
            __kmp_release_lock(lck, gtid);
            pr->u.p.parm4 = (victimIdx + 1) % nproc; // next victim
            continue; // not enough chunks to steal
          }
          // stealing succeded, reduce victim's ub by 1/4 of undone chunks or
          // by 1
          if (remaining > 3) {
            // steal 1/4 of remaining
            KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen, remaining >> 2);
            init = (victim->u.p.ub -= (remaining >> 2));
          } else {
            // steal 1 chunk of 2 or 3 remaining
            KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen, 1);
            init = (victim->u.p.ub -= 1);
          }
          __kmp_release_lock(lck, gtid);

          KMP_DEBUG_ASSERT(init + 1 <= limit);
          pr->u.p.parm4 = victimIdx; // remember victim to steal from
          status = 1;
          while_index = 0;
          // now update own count and ub with stolen range but init chunk
          __kmp_acquire_lock(th->th.th_dispatch->th_steal_lock, gtid);
          pr->u.p.count = init + 1;
          pr->u.p.ub = limit;
          __kmp_release_lock(th->th.th_dispatch->th_steal_lock, gtid);
        } // while (search for victim)
      } // if (try to find victim and steal)
    } else {
      // 4-byte induction variable, use 8-byte CAS for pair (count, ub)
      typedef union {
        struct {
          UT count;
          T ub;
        } p;
        kmp_int64 b;
      } union_i4;
      // All operations on 'count' or 'ub' must be combined atomically
      // together.
      {
        union_i4 vold, vnew;
        vold.b = *(volatile kmp_int64 *)(&pr->u.p.count);
        vnew = vold;
        vnew.p.count++;
        while (!KMP_COMPARE_AND_STORE_ACQ64(
            (volatile kmp_int64 *)&pr->u.p.count,
            *VOLATILE_CAST(kmp_int64 *) & vold.b,
            *VOLATILE_CAST(kmp_int64 *) & vnew.b)) {
          KMP_CPU_PAUSE();
          vold.b = *(volatile kmp_int64 *)(&pr->u.p.count);
          vnew = vold;
          vnew.p.count++;
        }
        vnew = vold;
        init = vnew.p.count;
        status = (init < (UT)vnew.p.ub);
      }

      if (!status) {
        kmp_info_t **other_threads = team->t.t_threads;
        int while_limit = nproc; // nproc attempts to find a victim
        int while_index = 0;

        // TODO: algorithm of searching for a victim
        // should be cleaned up and measured
        while ((!status) && (while_limit != ++while_index)) {
          union_i4 vold, vnew;
          kmp_int32 remaining;
          T victimIdx = pr->u.p.parm4;
          T oldVictimIdx = victimIdx ? victimIdx - 1 : nproc - 1;
          dispatch_private_info_template<T> *victim =
              reinterpret_cast<dispatch_private_info_template<T> *>(
                  other_threads[victimIdx]
                      ->th.th_dispatch->th_dispatch_pr_current);
          while ((victim == NULL || victim == pr ||
                  (*(volatile T *)&victim->u.p.static_steal_counter !=
                   *(volatile T *)&pr->u.p.static_steal_counter)) &&
                 oldVictimIdx != victimIdx) {
            victimIdx = (victimIdx + 1) % nproc;
            victim = reinterpret_cast<dispatch_private_info_template<T> *>(
                other_threads[victimIdx]
                    ->th.th_dispatch->th_dispatch_pr_current);
          }
          if (!victim || (*(volatile T *)&victim->u.p.static_steal_counter !=
                          *(volatile T *)&pr->u.p.static_steal_counter)) {
            continue; // try once more (nproc attempts in total)
            // no victim is ready yet to participate in stealing
            // because all victims are still in kmp_init_dispatch
          }
          pr->u.p.parm4 = victimIdx; // new victim found
          while (1) { // CAS loop if victim has enough chunks to steal
            vold.b = *(volatile kmp_int64 *)(&victim->u.p.count);
            vnew = vold;

            KMP_DEBUG_ASSERT((vnew.p.ub - 1) * (UT)chunk <= trip);
            if (vnew.p.count >= (UT)vnew.p.ub ||
                (remaining = vnew.p.ub - vnew.p.count) < 2) {
              pr->u.p.parm4 = (victimIdx + 1) % nproc; // shift start victim id
              break; // not enough chunks to steal, goto next victim
            }
            if (remaining > 3) {
              vnew.p.ub -= (remaining >> 2); // try to steal 1/4 of remaining
            } else {
              vnew.p.ub -= 1; // steal 1 chunk of 2 or 3 remaining
            }
            KMP_DEBUG_ASSERT((vnew.p.ub - 1) * (UT)chunk <= trip);
            // TODO: Should this be acquire or release?
            if (KMP_COMPARE_AND_STORE_ACQ64(
                    (volatile kmp_int64 *)&victim->u.p.count,
                    *VOLATILE_CAST(kmp_int64 *) & vold.b,
                    *VOLATILE_CAST(kmp_int64 *) & vnew.b)) {
              // stealing succedded
              KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_stolen,
                                        vold.p.ub - vnew.p.ub);
              status = 1;
              while_index = 0;
              // now update own count and ub
              init = vnew.p.ub;
              vold.p.count = init + 1;
#if KMP_ARCH_X86
              KMP_XCHG_FIXED64((volatile kmp_int64 *)(&pr->u.p.count), vold.b);
#else
              *(volatile kmp_int64 *)(&pr->u.p.count) = vold.b;
#endif
              break;
            } // if (check CAS result)
            KMP_CPU_PAUSE(); // CAS failed, repeate attempt
          } // while (try to steal from particular victim)
        } // while (search for victim)
      } // if (try to find victim and steal)
    } // if (4-byte induction variable)
    if (!status) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.parm2;
      init *= chunk;
      limit = chunk + init - 1;
      incr = pr->u.p.st;
      KMP_COUNT_DEVELOPER_VALUE(FOR_static_steal_chunks, 1);

      KMP_DEBUG_ASSERT(init <= trip);
      if ((last = (limit >= trip)) != 0)
        limit = trip;
      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
    break;
  } // case
#endif // ( KMP_STATIC_STEAL_ENABLED )
  case kmp_sch_static_balanced: {
    KD_TRACE(
        10,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_static_balanced case\n",
         gtid));
    /* check if thread has any iteration to do */
    if ((status = !pr->u.p.count) != 0) {
      pr->u.p.count = 1;
      *p_lb = pr->u.p.lb;
      *p_ub = pr->u.p.ub;
      last = pr->u.p.parm1;
      if (p_st != NULL)
        *p_st = pr->u.p.st;
    } else { /* no iterations to do */
      pr->u.p.lb = pr->u.p.ub + pr->u.p.st;
    }
  } // case
  break;
  case kmp_sch_static_greedy: /* original code for kmp_sch_static_greedy was
                                 merged here */
  case kmp_sch_static_chunked: {
    T parm1;

    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d "
                   "kmp_sch_static_[affinity|chunked] case\n",
                   gtid));
    parm1 = pr->u.p.parm1;

    trip = pr->u.p.tc - 1;
    init = parm1 * (pr->u.p.count + tid);

    if ((status = (init <= trip)) != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      limit = parm1 + init - 1;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      pr->u.p.count += nproc;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
  } // case
  break;

  case kmp_sch_dynamic_chunked: {
    T chunk = pr->u.p.parm1;

    KD_TRACE(
        100,
        ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_dynamic_chunked case\n",
         gtid));

    init = chunk * test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
    trip = pr->u.p.tc - 1;

    if ((status = (init <= trip)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.lb;
      limit = chunk + init - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
  } // case
  break;

  case kmp_sch_guided_iterative_chunked: {
    T chunkspec = pr->u.p.parm1;
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_guided_chunked "
                   "iterative case\n",
                   gtid));
    trip = pr->u.p.tc;
    // Start atomic part of calculations
    while (1) {
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        // nothing to do, don't try atomic op
        status = 0;
        break;
      }
      if ((T)remaining <
          pr->u.p.parm2) { // compare with K*nproc*(chunk+1), K=2 by default
        // use dynamic-style shcedule
        // atomically inrement iterations, get old value
        init = test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                                 (ST)chunkspec);
        remaining = trip - init;
        if (remaining <= 0) {
          status = 0; // all iterations got by other threads
        } else {
          // got some iterations to work on
          status = 1;
          if ((T)remaining > chunkspec) {
            limit = init + chunkspec - 1;
          } else {
            last = 1; // the last chunk
            limit = init + remaining - 1;
          } // if
        } // if
        break;
      } // if
      limit = init +
              (UT)(remaining * *(double *)&pr->u.p.parm3); // divide by K*nproc
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } // if
  } // case
  break;

#if OMP_45_ENABLED
  case kmp_sch_guided_simd: {
    // same as iterative but curr-chunk adjusted to be multiple of given
    // chunk
    T chunk = pr->u.p.parm1;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_guided_simd case\n",
              gtid));
    trip = pr->u.p.tc;
    // Start atomic part of calculations
    while (1) {
      ST remaining; // signed, because can be < 0
      init = sh->u.s.iteration; // shared value
      remaining = trip - init;
      if (remaining <= 0) { // AC: need to compare with 0 first
        status = 0; // nothing to do, don't try atomic op
        break;
      }
      KMP_DEBUG_ASSERT(init % chunk == 0);
      // compare with K*nproc*(chunk+1), K=2 by default
      if ((T)remaining < pr->u.p.parm2) {
        // use dynamic-style shcedule
        // atomically inrement iterations, get old value
        init = test_then_add<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                                 (ST)chunk);
        remaining = trip - init;
        if (remaining <= 0) {
          status = 0; // all iterations got by other threads
        } else {
          // got some iterations to work on
          status = 1;
          if ((T)remaining > chunk) {
            limit = init + chunk - 1;
          } else {
            last = 1; // the last chunk
            limit = init + remaining - 1;
          } // if
        } // if
        break;
      } // if
      // divide by K*nproc
      UT span = remaining * (*(double *)&pr->u.p.parm3);
      UT rem = span % chunk;
      if (rem) // adjust so that span%chunk == 0
        span += chunk - rem;
      limit = init + span;
      if (compare_and_swap<ST>(RCAST(volatile ST *, &sh->u.s.iteration),
                               (ST)init, (ST)limit)) {
        // CAS was successful, chunk obtained
        status = 1;
        --limit;
        break;
      } // if
    } // while
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } // if
  } // case
  break;
#endif // OMP_45_ENABLED

  case kmp_sch_guided_analytical_chunked: {
    T chunkspec = pr->u.p.parm1;
    UT chunkIdx;
#if KMP_USE_X87CONTROL
    /* for storing original FPCW value for Windows* OS on
       IA-32 architecture 8-byte version */
    unsigned int oldFpcw;
    unsigned int fpcwSet = 0;
#endif
    KD_TRACE(100, ("__kmp_dispatch_next_algorithm: T#%d "
                   "kmp_sch_guided_analytical_chunked case\n",
                   gtid));

    trip = pr->u.p.tc;

    KMP_DEBUG_ASSERT(nproc > 1);
    KMP_DEBUG_ASSERT((2UL * chunkspec + 1) * (UT)nproc < trip);

    while (1) { /* this while loop is a safeguard against unexpected zero
                   chunk sizes */
      chunkIdx = test_then_inc_acq<ST>((volatile ST *)&sh->u.s.iteration);
      if (chunkIdx >= (UT)pr->u.p.parm2) {
        --trip;
        /* use dynamic-style scheduling */
        init = chunkIdx * chunkspec + pr->u.p.count;
        /* need to verify init > 0 in case of overflow in the above
         * calculation */
        if ((status = (init > 0 && init <= trip)) != 0) {
          limit = init + chunkspec - 1;

          if ((last = (limit >= trip)) != 0)
            limit = trip;
        }
        break;
      } else {
/* use exponential-style scheduling */
/* The following check is to workaround the lack of long double precision on
   Windows* OS.
   This check works around the possible effect that init != 0 for chunkIdx == 0.
 */
#if KMP_USE_X87CONTROL
        /* If we haven't already done so, save original
           FPCW and set precision to 64-bit, as Windows* OS
           on IA-32 architecture defaults to 53-bit */
        if (!fpcwSet) {
          oldFpcw = _control87(0, 0);
          _control87(_PC_64, _MCW_PC);
          fpcwSet = 0x30000;
        }
#endif
        if (chunkIdx) {
          init = __kmp_dispatch_guided_remaining<T>(
              trip, *(DBL *)&pr->u.p.parm3, chunkIdx);
          KMP_DEBUG_ASSERT(init);
          init = trip - init;
        } else
          init = 0;
        limit = trip - __kmp_dispatch_guided_remaining<T>(
                           trip, *(DBL *)&pr->u.p.parm3, chunkIdx + 1);
        KMP_ASSERT(init <= limit);
        if (init < limit) {
          KMP_DEBUG_ASSERT(limit <= trip);
          --limit;
          status = 1;
          break;
        } // if
      } // if
    } // while (1)
#if KMP_USE_X87CONTROL
    /* restore FPCW if necessary
       AC: check fpcwSet flag first because oldFpcw can be uninitialized here
    */
    if (fpcwSet && (oldFpcw & fpcwSet))
      _control87(oldFpcw, _MCW_PC);
#endif
    if (status != 0) {
      start = pr->u.p.lb;
      incr = pr->u.p.st;
      if (p_st != NULL)
        *p_st = incr;
      *p_lb = start + init * incr;
      *p_ub = start + limit * incr;
      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      }
    } else {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    }
  } // case
  break;

  case kmp_sch_trapezoidal: {
    UT index;
    T parm2 = pr->u.p.parm2;
    T parm3 = pr->u.p.parm3;
    T parm4 = pr->u.p.parm4;
    KD_TRACE(100,
             ("__kmp_dispatch_next_algorithm: T#%d kmp_sch_trapezoidal case\n",
              gtid));

    index = test_then_inc<ST>((volatile ST *)&sh->u.s.iteration);

    init = (index * ((2 * parm2) - (index - 1) * parm4)) / 2;
    trip = pr->u.p.tc - 1;

    if ((status = ((T)index < parm3 && init <= trip)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      if (p_st != NULL)
        *p_st = 0;
    } else {
      start = pr->u.p.lb;
      limit = ((index + 1) * (2 * parm2 - index * parm4)) / 2 - 1;
      incr = pr->u.p.st;

      if ((last = (limit >= trip)) != 0)
        limit = trip;

      if (p_st != NULL)
        *p_st = incr;

      if (incr == 1) {
        *p_lb = start + init;
        *p_ub = start + limit;
      } else {
        *p_lb = start + init * incr;
        *p_ub = start + limit * incr;
      }

      if (pr->flags.ordered) {
        pr->u.p.ordered_lower = init;
        pr->u.p.ordered_upper = limit;
      } // if
    } // if
  } // case
  break;
  default: {
    status = 0; // to avoid complaints on uninitialized variable use
    __kmp_fatal(KMP_MSG(UnknownSchedTypeDetected), // Primary message
                KMP_HNT(GetNewerLibrary), // Hint
                __kmp_msg_null // Variadic argument list terminator
                );
  } break;
  } // switch
  if (p_last)
    *p_last = last;
#ifdef KMP_DEBUG
  if (pr->flags.ordered) {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmp_dispatch_next_algorithm: T#%%d "
                            "ordered_lower:%%%s ordered_upper:%%%s\n",
                            traits_t<UT>::spec, traits_t<UT>::spec);
    KD_TRACE(1000, (buff, gtid, pr->u.p.ordered_lower, pr->u.p.ordered_upper));
    __kmp_str_free(&buff);
  }
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_next_algorithm: T#%%d exit status:%%d p_last:%%d "
        "p_lb:%%%s p_ub:%%%s p_st:%%%s\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, status, *p_last, *p_lb, *p_ub, *p_st));
    __kmp_str_free(&buff);
  }
#endif
  return status;
}

/* Define a macro for exiting __kmp_dispatch_next(). If status is 0 (no more
   work), then tell OMPT the loop is over. In some cases kmp_dispatch_fini()
   is not called. */
#if OMPT_SUPPORT && OMPT_OPTIONAL
#define OMPT_LOOP_END                                                          \
  if (status == 0) {                                                           \
    if (ompt_enabled.ompt_callback_work) {                                     \
      ompt_team_info_t *team_info = __ompt_get_teaminfo(0, NULL);              \
      ompt_task_info_t *task_info = __ompt_get_task_info_object(0);            \
      ompt_callbacks.ompt_callback(ompt_callback_work)(                        \
          ompt_work_loop, ompt_scope_end, &(team_info->parallel_data),         \
          &(task_info->task_data), 0, codeptr);                                \
    }                                                                          \
  }
// TODO: implement count
#else
#define OMPT_LOOP_END // no-op
#endif

#if KMP_STATS_ENABLED
#define KMP_STATS_LOOP_END                                                     \
  {                                                                            \
    kmp_int64 u, l, t, i;                                                      \
    l = (kmp_int64)(*p_lb);                                                    \
    u = (kmp_int64)(*p_ub);                                                    \
    i = (kmp_int64)(pr->u.p.st);                                               \
    if (status == 0) {                                                         \
      t = 0;                                                                   \
      KMP_POP_PARTITIONED_TIMER();                                             \
    } else if (i == 1) {                                                       \
      if (u >= l)                                                              \
        t = u - l + 1;                                                         \
      else                                                                     \
        t = 0;                                                                 \
    } else if (i < 0) {                                                        \
      if (l >= u)                                                              \
        t = (l - u) / (-i) + 1;                                                \
      else                                                                     \
        t = 0;                                                                 \
    } else {                                                                   \
      if (u >= l)                                                              \
        t = (u - l) / i + 1;                                                   \
      else                                                                     \
        t = 0;                                                                 \
    }                                                                          \
    KMP_COUNT_VALUE(OMP_loop_dynamic_iterations, t);                           \
  }
#else
#define KMP_STATS_LOOP_END /* Nothing */
#endif

template <typename T>
static int __kmp_dispatch_next(ident_t *loc, int gtid, kmp_int32 *p_last,
                               T *p_lb, T *p_ub,
                               typename traits_t<T>::signed_t *p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                               ,
                               void *codeptr
#endif
                               ) {

  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  // This is potentially slightly misleading, schedule(runtime) will appear here
  // even if the actual runtme schedule is static. (Which points out a
  // disadavantage of schedule(runtime): even when static scheduling is used it
  // costs more than a compile time choice to use static scheduling would.)
  KMP_TIME_PARTITIONED_BLOCK(OMP_loop_dynamic_scheduling);

  int status;
  dispatch_private_info_template<T> *pr;
  kmp_info_t *th = __kmp_threads[gtid];
  kmp_team_t *team = th->th.th_team;

  KMP_DEBUG_ASSERT(p_lb && p_ub && p_st); // AC: these cannot be NULL
  KD_TRACE(
      1000,
      ("__kmp_dispatch_next: T#%d called p_lb:%p p_ub:%p p_st:%p p_last: %p\n",
       gtid, p_lb, p_ub, p_st, p_last));

  if (team->t.t_serialized) {
    /* NOTE: serialize this dispatch becase we are not at the active level */
    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_disp_buffer); /* top of the stack */
    KMP_DEBUG_ASSERT(pr);

    if ((status = (pr->u.p.tc != 0)) == 0) {
      *p_lb = 0;
      *p_ub = 0;
      //            if ( p_last != NULL )
      //                *p_last = 0;
      if (p_st != NULL)
        *p_st = 0;
      if (__kmp_env_consistency_check) {
        if (pr->pushed_ws != ct_none) {
          pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
        }
      }
    } else if (pr->flags.nomerge) {
      kmp_int32 last;
      T start;
      UT limit, trip, init;
      ST incr;
      T chunk = pr->u.p.parm1;

      KD_TRACE(100, ("__kmp_dispatch_next: T#%d kmp_sch_dynamic_chunked case\n",
                     gtid));

      init = chunk * pr->u.p.count++;
      trip = pr->u.p.tc - 1;

      if ((status = (init <= trip)) == 0) {
        *p_lb = 0;
        *p_ub = 0;
        //                if ( p_last != NULL )
        //                    *p_last = 0;
        if (p_st != NULL)
          *p_st = 0;
        if (__kmp_env_consistency_check) {
          if (pr->pushed_ws != ct_none) {
            pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
          }
        }
      } else {
        start = pr->u.p.lb;
        limit = chunk + init - 1;
        incr = pr->u.p.st;

        if ((last = (limit >= trip)) != 0) {
          limit = trip;
#if KMP_OS_WINDOWS
          pr->u.p.last_upper = pr->u.p.ub;
#endif /* KMP_OS_WINDOWS */
        }
        if (p_last != NULL)
          *p_last = last;
        if (p_st != NULL)
          *p_st = incr;
        if (incr == 1) {
          *p_lb = start + init;
          *p_ub = start + limit;
        } else {
          *p_lb = start + init * incr;
          *p_ub = start + limit * incr;
        }

        if (pr->flags.ordered) {
          pr->u.p.ordered_lower = init;
          pr->u.p.ordered_upper = limit;
#ifdef KMP_DEBUG
          {
            char *buff;
            // create format specifiers before the debug output
            buff = __kmp_str_format("__kmp_dispatch_next: T#%%d "
                                    "ordered_lower:%%%s ordered_upper:%%%s\n",
                                    traits_t<UT>::spec, traits_t<UT>::spec);
            KD_TRACE(1000, (buff, gtid, pr->u.p.ordered_lower,
                            pr->u.p.ordered_upper));
            __kmp_str_free(&buff);
          }
#endif
        } // if
      } // if
    } else {
      pr->u.p.tc = 0;
      *p_lb = pr->u.p.lb;
      *p_ub = pr->u.p.ub;
#if KMP_OS_WINDOWS
      pr->u.p.last_upper = *p_ub;
#endif /* KMP_OS_WINDOWS */
      if (p_last != NULL)
        *p_last = TRUE;
      if (p_st != NULL)
        *p_st = pr->u.p.st;
    } // if
#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format(
          "__kmp_dispatch_next: T#%%d serialized case: p_lb:%%%s "
          "p_ub:%%%s p_st:%%%s p_last:%%p %%d  returning:%%d\n",
          traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
      KD_TRACE(10, (buff, gtid, *p_lb, *p_ub, *p_st, p_last, *p_last, status));
      __kmp_str_free(&buff);
    }
#endif
#if INCLUDE_SSC_MARKS
    SSC_MARK_DISPATCH_NEXT();
#endif
    OMPT_LOOP_END;
    KMP_STATS_LOOP_END;
    return status;
  } else {
    kmp_int32 last = 0;
    dispatch_shared_info_template<T> volatile *sh;

    KMP_DEBUG_ASSERT(th->th.th_dispatch ==
                     &th->th.th_team->t.t_dispatch[th->th.th_info.ds.ds_tid]);

    pr = reinterpret_cast<dispatch_private_info_template<T> *>(
        th->th.th_dispatch->th_dispatch_pr_current);
    KMP_DEBUG_ASSERT(pr);
    sh = reinterpret_cast<dispatch_shared_info_template<T> volatile *>(
        th->th.th_dispatch->th_dispatch_sh_current);
    KMP_DEBUG_ASSERT(sh);

#if KMP_USE_HIER_SCHED
    if (pr->flags.use_hier)
      status = sh->hier->next(loc, gtid, pr, &last, p_lb, p_ub, p_st);
    else
#endif // KMP_USE_HIER_SCHED
      status = __kmp_dispatch_next_algorithm<T>(gtid, pr, sh, &last, p_lb, p_ub,
                                                p_st, th->th.th_team_nproc,
                                                th->th.th_info.ds.ds_tid);
    // status == 0: no more iterations to execute
    if (status == 0) {
      UT num_done;

      num_done = test_then_inc<ST>((volatile ST *)&sh->u.s.num_done);
#ifdef KMP_DEBUG
      {
        char *buff;
        // create format specifiers before the debug output
        buff = __kmp_str_format(
            "__kmp_dispatch_next: T#%%d increment num_done:%%%s\n",
            traits_t<UT>::spec);
        KD_TRACE(10, (buff, gtid, sh->u.s.num_done));
        __kmp_str_free(&buff);
      }
#endif

#if KMP_USE_HIER_SCHED
      pr->flags.use_hier = FALSE;
#endif
      if ((ST)num_done == th->th.th_team_nproc - 1) {
#if (KMP_STATIC_STEAL_ENABLED)
        if (pr->schedule == kmp_sch_static_steal &&
            traits_t<T>::type_size > 4) {
          int i;
          kmp_info_t **other_threads = team->t.t_threads;
          // loop complete, safe to destroy locks used for stealing
          for (i = 0; i < th->th.th_team_nproc; ++i) {
            kmp_lock_t *lck = other_threads[i]->th.th_dispatch->th_steal_lock;
            KMP_ASSERT(lck != NULL);
            __kmp_destroy_lock(lck);
            __kmp_free(lck);
            other_threads[i]->th.th_dispatch->th_steal_lock = NULL;
          }
        }
#endif
        /* NOTE: release this buffer to be reused */

        KMP_MB(); /* Flush all pending memory write invalidates.  */

        sh->u.s.num_done = 0;
        sh->u.s.iteration = 0;

        /* TODO replace with general release procedure? */
        if (pr->flags.ordered) {
          sh->u.s.ordered_iteration = 0;
        }

        KMP_MB(); /* Flush all pending memory write invalidates.  */

        sh->buffer_index += __kmp_dispatch_num_buffers;
        KD_TRACE(100, ("__kmp_dispatch_next: T#%d change buffer_index:%d\n",
                       gtid, sh->buffer_index));

        KMP_MB(); /* Flush all pending memory write invalidates.  */

      } // if
      if (__kmp_env_consistency_check) {
        if (pr->pushed_ws != ct_none) {
          pr->pushed_ws = __kmp_pop_workshare(gtid, pr->pushed_ws, loc);
        }
      }

      th->th.th_dispatch->th_deo_fcn = NULL;
      th->th.th_dispatch->th_dxo_fcn = NULL;
      th->th.th_dispatch->th_dispatch_sh_current = NULL;
      th->th.th_dispatch->th_dispatch_pr_current = NULL;
    } // if (status == 0)
#if KMP_OS_WINDOWS
    else if (last) {
      pr->u.p.last_upper = pr->u.p.ub;
    }
#endif /* KMP_OS_WINDOWS */
    if (p_last != NULL && status != 0)
      *p_last = last;
  } // if

#ifdef KMP_DEBUG
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format(
        "__kmp_dispatch_next: T#%%d normal case: "
        "p_lb:%%%s p_ub:%%%s p_st:%%%s p_last:%%p (%%d) returning:%%d\n",
        traits_t<T>::spec, traits_t<T>::spec, traits_t<ST>::spec);
    KD_TRACE(10, (buff, gtid, *p_lb, *p_ub, p_st ? *p_st : 0, p_last,
                  (p_last ? *p_last : 0), status));
    __kmp_str_free(&buff);
  }
#endif
#if INCLUDE_SSC_MARKS
  SSC_MARK_DISPATCH_NEXT();
#endif
  OMPT_LOOP_END;
  KMP_STATS_LOOP_END;
  return status;
}

template <typename T>
static void __kmp_dist_get_bounds(ident_t *loc, kmp_int32 gtid,
                                  kmp_int32 *plastiter, T *plower, T *pupper,
                                  typename traits_t<T>::signed_t incr) {
  typedef typename traits_t<T>::unsigned_t UT;
  kmp_uint32 team_id;
  kmp_uint32 nteams;
  UT trip_count;
  kmp_team_t *team;
  kmp_info_t *th;

  KMP_DEBUG_ASSERT(plastiter && plower && pupper);
  KE_TRACE(10, ("__kmpc_dist_get_bounds called (%d)\n", gtid));
#ifdef KMP_DEBUG
  typedef typename traits_t<T>::signed_t ST;
  {
    char *buff;
    // create format specifiers before the debug output
    buff = __kmp_str_format("__kmpc_dist_get_bounds: T#%%d liter=%%d "
                            "iter=(%%%s, %%%s, %%%s) signed?<%s>\n",
                            traits_t<T>::spec, traits_t<T>::spec,
                            traits_t<ST>::spec, traits_t<T>::spec);
    KD_TRACE(100, (buff, gtid, *plastiter, *plower, *pupper, incr));
    __kmp_str_free(&buff);
  }
#endif

  if (__kmp_env_consistency_check) {
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
  th = __kmp_threads[gtid];
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

  if (trip_count <= nteams) {
    KMP_DEBUG_ASSERT(
        __kmp_static == kmp_sch_static_greedy ||
        __kmp_static ==
            kmp_sch_static_balanced); // Unknown static scheduling type.
    // only some teams get single iteration, others get nothing
    if (team_id < trip_count) {
      *pupper = *plower = *plower + team_id * incr;
    } else {
      *plower = *pupper + incr; // zero-trip loop
    }
    if (plastiter != NULL)
      *plastiter = (team_id == trip_count - 1);
  } else {
    if (__kmp_static == kmp_sch_static_balanced) {
      UT chunk = trip_count / nteams;
      UT extras = trip_count % nteams;
      *plower +=
          incr * (team_id * chunk + (team_id < extras ? team_id : extras));
      *pupper = *plower + chunk * incr - (team_id < extras ? 0 : incr);
      if (plastiter != NULL)
        *plastiter = (team_id == nteams - 1);
    } else {
      T chunk_inc_count =
          (trip_count / nteams + ((trip_count % nteams) ? 1 : 0)) * incr;
      T upper = *pupper;
      KMP_DEBUG_ASSERT(__kmp_static == kmp_sch_static_greedy);
      // Unknown static scheduling type.
      *plower += team_id * chunk_inc_count;
      *pupper = *plower + chunk_inc_count - incr;
      // Check/correct bounds if needed
      if (incr > 0) {
        if (*pupper < *plower)
          *pupper = traits_t<T>::max_value;
        if (plastiter != NULL)
          *plastiter = *plower <= upper && *pupper > upper - incr;
        if (*pupper > upper)
          *pupper = upper; // tracker C73258
      } else {
        if (*pupper > *plower)
          *pupper = traits_t<T>::min_value;
        if (plastiter != NULL)
          *plastiter = *plower >= upper && *pupper < upper - incr;
        if (*pupper < upper)
          *pupper = upper; // tracker C73258
      }
    }
  }
}

//-----------------------------------------------------------------------------
// Dispatch routines
//    Transfer call to template< type T >
//    __kmp_dispatch_init( ident_t *loc, int gtid, enum sched_type schedule,
//                         T lb, T ub, ST st, ST chunk )
extern "C" {

/*!
@ingroup WORK_SHARING
@{
@param loc Source location
@param gtid Global thread id
@param schedule Schedule type
@param lb  Lower bound
@param ub  Upper bound
@param st  Step (or increment if you prefer)
@param chunk The chunk size to block with

This function prepares the runtime to start a dynamically scheduled for loop,
saving the loop arguments.
These functions are all identical apart from the types of the arguments.
*/

void __kmpc_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                            enum sched_type schedule, kmp_int32 lb,
                            kmp_int32 ub, kmp_int32 st, kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}
/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                             enum sched_type schedule, kmp_uint32 lb,
                             kmp_uint32 ub, kmp_int32 st, kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                            enum sched_type schedule, kmp_int64 lb,
                            kmp_int64 ub, kmp_int64 st, kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4
*/
void __kmpc_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                             enum sched_type schedule, kmp_uint64 lb,
                             kmp_uint64 ub, kmp_int64 st, kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
See @ref __kmpc_dispatch_init_4

Difference from __kmpc_dispatch_init set of functions is these functions
are called for composite distribute parallel for construct. Thus before
regular iterations dispatching we need to calc per-team iteration space.

These functions are all identical apart from the types of the arguments.
*/
void __kmpc_dist_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                                 enum sched_type schedule, kmp_int32 *p_last,
                                 kmp_int32 lb, kmp_int32 ub, kmp_int32 st,
                                 kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_int32>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                                  enum sched_type schedule, kmp_int32 *p_last,
                                  kmp_uint32 lb, kmp_uint32 ub, kmp_int32 st,
                                  kmp_int32 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_uint32>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                                 enum sched_type schedule, kmp_int32 *p_last,
                                 kmp_int64 lb, kmp_int64 ub, kmp_int64 st,
                                 kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_int64>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

void __kmpc_dist_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                                  enum sched_type schedule, kmp_int32 *p_last,
                                  kmp_uint64 lb, kmp_uint64 ub, kmp_int64 st,
                                  kmp_int64 chunk) {
  KMP_DEBUG_ASSERT(__kmp_init_serial);
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  __kmp_dist_get_bounds<kmp_uint64>(loc, gtid, p_last, &lb, &ub, st);
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk, true);
}

/*!
@param loc Source code location
@param gtid Global thread id
@param p_last Pointer to a flag set to one if this is the last chunk or zero
otherwise
@param p_lb   Pointer to the lower bound for the next chunk of work
@param p_ub   Pointer to the upper bound for the next chunk of work
@param p_st   Pointer to the stride for the next chunk of work
@return one if there is work to be done, zero otherwise

Get the next dynamically allocated chunk of work for this thread.
If there is no more work, then the lb,ub and stride need not be modified.
*/
int __kmpc_dispatch_next_4(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                           kmp_int32 *p_lb, kmp_int32 *p_ub, kmp_int32 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_int32>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                        ,
                                        OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                            );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_4u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                            kmp_uint32 *p_lb, kmp_uint32 *p_ub,
                            kmp_int32 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_uint32>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                         ,
                                         OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                             );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_8(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                           kmp_int64 *p_lb, kmp_int64 *p_ub, kmp_int64 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_int64>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                        ,
                                        OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                            );
}

/*!
See @ref __kmpc_dispatch_next_4
*/
int __kmpc_dispatch_next_8u(ident_t *loc, kmp_int32 gtid, kmp_int32 *p_last,
                            kmp_uint64 *p_lb, kmp_uint64 *p_ub,
                            kmp_int64 *p_st) {
#if OMPT_SUPPORT && OMPT_OPTIONAL
  OMPT_STORE_RETURN_ADDRESS(gtid);
#endif
  return __kmp_dispatch_next<kmp_uint64>(loc, gtid, p_last, p_lb, p_ub, p_st
#if OMPT_SUPPORT && OMPT_OPTIONAL
                                         ,
                                         OMPT_LOAD_RETURN_ADDRESS(gtid)
#endif
                                             );
}

/*!
@param loc Source code location
@param gtid Global thread id

Mark the end of a dynamic loop.
*/
void __kmpc_dispatch_fini_4(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint32>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_8(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint64>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_4u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint32>(gtid, loc);
}

/*!
See @ref __kmpc_dispatch_fini_4
*/
void __kmpc_dispatch_fini_8u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish<kmp_uint64>(gtid, loc);
}
/*! @} */

//-----------------------------------------------------------------------------
// Non-template routines from kmp_dispatch.cpp used in other sources

kmp_uint32 __kmp_eq_4(kmp_uint32 value, kmp_uint32 checker) {
  return value == checker;
}

kmp_uint32 __kmp_neq_4(kmp_uint32 value, kmp_uint32 checker) {
  return value != checker;
}

kmp_uint32 __kmp_lt_4(kmp_uint32 value, kmp_uint32 checker) {
  return value < checker;
}

kmp_uint32 __kmp_ge_4(kmp_uint32 value, kmp_uint32 checker) {
  return value >= checker;
}

kmp_uint32 __kmp_le_4(kmp_uint32 value, kmp_uint32 checker) {
  return value <= checker;
}

kmp_uint32
__kmp_wait_yield_4(volatile kmp_uint32 *spinner, kmp_uint32 checker,
                   kmp_uint32 (*pred)(kmp_uint32, kmp_uint32),
                   void *obj // Higher-level synchronization object, or NULL.
                   ) {
  // note: we may not belong to a team at this point
  volatile kmp_uint32 *spin = spinner;
  kmp_uint32 check = checker;
  kmp_uint32 spins;
  kmp_uint32 (*f)(kmp_uint32, kmp_uint32) = pred;
  kmp_uint32 r;

  KMP_FSYNC_SPIN_INIT(obj, CCAST(kmp_uint32 *, spin));
  KMP_INIT_YIELD(spins);
  // main wait spin loop
  while (!f(r = TCR_4(*spin), check)) {
    KMP_FSYNC_SPIN_PREPARE(obj);
    /* GEH - remove this since it was accidentally introduced when kmp_wait was
       split. It causes problems with infinite recursion because of exit lock */
    /* if ( TCR_4(__kmp_global.g.g_done) && __kmp_global.g.g_abort)
        __kmp_abort_thread(); */

    /* if we have waited a bit, or are oversubscribed, yield */
    /* pause is in the following code */
    KMP_YIELD(TCR_4(__kmp_nth) > __kmp_avail_proc);
    KMP_YIELD_SPIN(spins);
  }
  KMP_FSYNC_SPIN_ACQUIRED(obj);
  return r;
}

void __kmp_wait_yield_4_ptr(
    void *spinner, kmp_uint32 checker, kmp_uint32 (*pred)(void *, kmp_uint32),
    void *obj // Higher-level synchronization object, or NULL.
    ) {
  // note: we may not belong to a team at this point
  void *spin = spinner;
  kmp_uint32 check = checker;
  kmp_uint32 spins;
  kmp_uint32 (*f)(void *, kmp_uint32) = pred;

  KMP_FSYNC_SPIN_INIT(obj, spin);
  KMP_INIT_YIELD(spins);
  // main wait spin loop
  while (!f(spin, check)) {
    KMP_FSYNC_SPIN_PREPARE(obj);
    /* if we have waited a bit, or are oversubscribed, yield */
    /* pause is in the following code */
    KMP_YIELD(TCR_4(__kmp_nth) > __kmp_avail_proc);
    KMP_YIELD_SPIN(spins);
  }
  KMP_FSYNC_SPIN_ACQUIRED(obj);
}

} // extern "C"

#ifdef KMP_GOMP_COMPAT

void __kmp_aux_dispatch_init_4(ident_t *loc, kmp_int32 gtid,
                               enum sched_type schedule, kmp_int32 lb,
                               kmp_int32 ub, kmp_int32 st, kmp_int32 chunk,
                               int push_ws) {
  __kmp_dispatch_init<kmp_int32>(loc, gtid, schedule, lb, ub, st, chunk,
                                 push_ws);
}

void __kmp_aux_dispatch_init_4u(ident_t *loc, kmp_int32 gtid,
                                enum sched_type schedule, kmp_uint32 lb,
                                kmp_uint32 ub, kmp_int32 st, kmp_int32 chunk,
                                int push_ws) {
  __kmp_dispatch_init<kmp_uint32>(loc, gtid, schedule, lb, ub, st, chunk,
                                  push_ws);
}

void __kmp_aux_dispatch_init_8(ident_t *loc, kmp_int32 gtid,
                               enum sched_type schedule, kmp_int64 lb,
                               kmp_int64 ub, kmp_int64 st, kmp_int64 chunk,
                               int push_ws) {
  __kmp_dispatch_init<kmp_int64>(loc, gtid, schedule, lb, ub, st, chunk,
                                 push_ws);
}

void __kmp_aux_dispatch_init_8u(ident_t *loc, kmp_int32 gtid,
                                enum sched_type schedule, kmp_uint64 lb,
                                kmp_uint64 ub, kmp_int64 st, kmp_int64 chunk,
                                int push_ws) {
  __kmp_dispatch_init<kmp_uint64>(loc, gtid, schedule, lb, ub, st, chunk,
                                  push_ws);
}

void __kmp_aux_dispatch_fini_chunk_4(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint32>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_8(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint64>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_4u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint32>(gtid, loc);
}

void __kmp_aux_dispatch_fini_chunk_8u(ident_t *loc, kmp_int32 gtid) {
  __kmp_dispatch_finish_chunk<kmp_uint64>(gtid, loc);
}

#endif /* KMP_GOMP_COMPAT */

/* ------------------------------------------------------------------------ */
