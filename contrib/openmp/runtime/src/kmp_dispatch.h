/*
 * kmp_dispatch.h: dynamic scheduling - iteration initialization and dispatch.
 */

//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//

#ifndef KMP_DISPATCH_H
#define KMP_DISPATCH_H

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#include "kmp.h"
#include "kmp_error.h"
#include "kmp_i18n.h"
#include "kmp_itt.h"
#include "kmp_stats.h"
#include "kmp_str.h"
#if KMP_OS_WINDOWS && KMP_ARCH_X86
#include <float.h>
#endif

#if OMPT_SUPPORT
#include "ompt-internal.h"
#include "ompt-specific.h"
#endif

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
#if KMP_USE_HIER_SCHED
// Forward declarations of some hierarchical scheduling data structures
template <typename T> struct kmp_hier_t;
template <typename T> struct kmp_hier_top_unit_t;
#endif // KMP_USE_HIER_SCHED

template <typename T> struct dispatch_shared_info_template;
template <typename T> struct dispatch_private_info_template;

template <typename T>
extern void __kmp_dispatch_init_algorithm(ident_t *loc, int gtid,
                                          dispatch_private_info_template<T> *pr,
                                          enum sched_type schedule, T lb, T ub,
                                          typename traits_t<T>::signed_t st,
#if USE_ITT_BUILD
                                          kmp_uint64 *cur_chunk,
#endif
                                          typename traits_t<T>::signed_t chunk,
                                          T nproc, T unit_id);
template <typename T>
extern int __kmp_dispatch_next_algorithm(
    int gtid, dispatch_private_info_template<T> *pr,
    dispatch_shared_info_template<T> volatile *sh, kmp_int32 *p_last, T *p_lb,
    T *p_ub, typename traits_t<T>::signed_t *p_st, T nproc, T unit_id);

void __kmp_dispatch_dxo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref);
void __kmp_dispatch_deo_error(int *gtid_ref, int *cid_ref, ident_t *loc_ref);

#if KMP_STATIC_STEAL_ENABLED

// replaces dispatch_private_info{32,64} structures and
// dispatch_private_info{32,64}_t types
template <typename T> struct dispatch_private_infoXX_template {
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  UT count; // unsigned
  T ub;
  /* Adding KMP_ALIGN_CACHE here doesn't help / can hurt performance */
  T lb;
  ST st; // signed
  UT tc; // unsigned
  T static_steal_counter; // for static_steal only; maybe better to put after ub

  /* parm[1-4] are used in different ways by different scheduling algorithms */

  // KMP_ALIGN( 32 ) ensures ( if the KMP_ALIGN macro is turned on )
  //    a) parm3 is properly aligned and
  //    b) all parm1-4 are in the same cache line.
  // Because of parm1-4 are used together, performance seems to be better
  // if they are in the same line (not measured though).

  struct KMP_ALIGN(32) { // compiler does not accept sizeof(T)*4
    T parm1;
    T parm2;
    T parm3;
    T parm4;
  };

  UT ordered_lower; // unsigned
  UT ordered_upper; // unsigned
#if KMP_OS_WINDOWS
  T last_upper;
#endif /* KMP_OS_WINDOWS */
};

#else /* KMP_STATIC_STEAL_ENABLED */

// replaces dispatch_private_info{32,64} structures and
// dispatch_private_info{32,64}_t types
template <typename T> struct dispatch_private_infoXX_template {
  typedef typename traits_t<T>::unsigned_t UT;
  typedef typename traits_t<T>::signed_t ST;
  T lb;
  T ub;
  ST st; // signed
  UT tc; // unsigned

  T parm1;
  T parm2;
  T parm3;
  T parm4;

  UT count; // unsigned

  UT ordered_lower; // unsigned
  UT ordered_upper; // unsigned
#if KMP_OS_WINDOWS
  T last_upper;
#endif /* KMP_OS_WINDOWS */
};
#endif /* KMP_STATIC_STEAL_ENABLED */

template <typename T> struct KMP_ALIGN_CACHE dispatch_private_info_template {
  // duplicate alignment here, otherwise size of structure is not correct in our
  // compiler
  union KMP_ALIGN_CACHE private_info_tmpl {
    dispatch_private_infoXX_template<T> p;
    dispatch_private_info64_t p64;
  } u;
  enum sched_type schedule; /* scheduling algorithm */
  kmp_sched_flags_t flags; /* flags (e.g., ordered, nomerge, etc.) */
  kmp_uint32 ordered_bumped;
  // to retain the structure size after making order
  kmp_int32 ordered_dummy[KMP_MAX_ORDERED - 3];
  dispatch_private_info *next; /* stack of buffers for nest of serial regions */
  kmp_uint32 type_size;
#if KMP_USE_HIER_SCHED
  kmp_int32 hier_id;
  kmp_hier_top_unit_t<T> *hier_parent;
  // member functions
  kmp_int32 get_hier_id() const { return hier_id; }
  kmp_hier_top_unit_t<T> *get_parent() { return hier_parent; }
#endif
  enum cons_type pushed_ws;
};

// replaces dispatch_shared_info{32,64} structures and
// dispatch_shared_info{32,64}_t types
template <typename T> struct dispatch_shared_infoXX_template {
  typedef typename traits_t<T>::unsigned_t UT;
  /* chunk index under dynamic, number of idle threads under static-steal;
     iteration index otherwise */
  volatile UT iteration;
  volatile UT num_done;
  volatile UT ordered_iteration;
  // to retain the structure size making ordered_iteration scalar
  UT ordered_dummy[KMP_MAX_ORDERED - 3];
};

// replaces dispatch_shared_info structure and dispatch_shared_info_t type
template <typename T> struct dispatch_shared_info_template {
  typedef typename traits_t<T>::unsigned_t UT;
  // we need union here to keep the structure size
  union shared_info_tmpl {
    dispatch_shared_infoXX_template<UT> s;
    dispatch_shared_info64_t s64;
  } u;
  volatile kmp_uint32 buffer_index;
#if OMP_45_ENABLED
  volatile kmp_int32 doacross_buf_idx; // teamwise index
  kmp_uint32 *doacross_flags; // array of iteration flags (0/1)
  kmp_int32 doacross_num_done; // count finished threads
#endif
#if KMP_USE_HIER_SCHED
  kmp_hier_t<T> *hier;
#endif
#if KMP_USE_HWLOC
  // When linking with libhwloc, the ORDERED EPCC test slowsdown on big
  // machines (> 48 cores). Performance analysis showed that a cache thrash
  // was occurring and this padding helps alleviate the problem.
  char padding[64];
#endif
};

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#undef USE_TEST_LOCKS

// test_then_add template (general template should NOT be used)
template <typename T> static __forceinline T test_then_add(volatile T *p, T d);

template <>
__forceinline kmp_int32 test_then_add<kmp_int32>(volatile kmp_int32 *p,
                                                 kmp_int32 d) {
  kmp_int32 r;
  r = KMP_TEST_THEN_ADD32(p, d);
  return r;
}

template <>
__forceinline kmp_int64 test_then_add<kmp_int64>(volatile kmp_int64 *p,
                                                 kmp_int64 d) {
  kmp_int64 r;
  r = KMP_TEST_THEN_ADD64(p, d);
  return r;
}

// test_then_inc_acq template (general template should NOT be used)
template <typename T> static __forceinline T test_then_inc_acq(volatile T *p);

template <>
__forceinline kmp_int32 test_then_inc_acq<kmp_int32>(volatile kmp_int32 *p) {
  kmp_int32 r;
  r = KMP_TEST_THEN_INC_ACQ32(p);
  return r;
}

template <>
__forceinline kmp_int64 test_then_inc_acq<kmp_int64>(volatile kmp_int64 *p) {
  kmp_int64 r;
  r = KMP_TEST_THEN_INC_ACQ64(p);
  return r;
}

// test_then_inc template (general template should NOT be used)
template <typename T> static __forceinline T test_then_inc(volatile T *p);

template <>
__forceinline kmp_int32 test_then_inc<kmp_int32>(volatile kmp_int32 *p) {
  kmp_int32 r;
  r = KMP_TEST_THEN_INC32(p);
  return r;
}

template <>
__forceinline kmp_int64 test_then_inc<kmp_int64>(volatile kmp_int64 *p) {
  kmp_int64 r;
  r = KMP_TEST_THEN_INC64(p);
  return r;
}

// compare_and_swap template (general template should NOT be used)
template <typename T>
static __forceinline kmp_int32 compare_and_swap(volatile T *p, T c, T s);

template <>
__forceinline kmp_int32 compare_and_swap<kmp_int32>(volatile kmp_int32 *p,
                                                    kmp_int32 c, kmp_int32 s) {
  return KMP_COMPARE_AND_STORE_REL32(p, c, s);
}

template <>
__forceinline kmp_int32 compare_and_swap<kmp_int64>(volatile kmp_int64 *p,
                                                    kmp_int64 c, kmp_int64 s) {
  return KMP_COMPARE_AND_STORE_REL64(p, c, s);
}

template <typename T> kmp_uint32 __kmp_ge(T value, T checker) {
  return value >= checker;
}
template <typename T> kmp_uint32 __kmp_eq(T value, T checker) {
  return value == checker;
}

/*
    Spin wait loop that first does pause, then yield.
    Waits until function returns non-zero when called with *spinner and check.
    Does NOT put threads to sleep.
    Arguments:
        UT is unsigned 4- or 8-byte type
        spinner - memory location to check value
        checker - value which spinner is >, <, ==, etc.
        pred - predicate function to perform binary comparison of some sort
#if USE_ITT_BUILD
        obj -- is higher-level synchronization object to report to ittnotify. It
        is used to report locks consistently. For example, if lock is acquired
        immediately, its address is reported to ittnotify via
        KMP_FSYNC_ACQUIRED(). However, it lock cannot be acquired immediately
        and lock routine calls to KMP_WAIT_YIELD(), the later should report the
        same address, not an address of low-level spinner.
#endif // USE_ITT_BUILD
    TODO: make inline function (move to header file for icl)
*/
template <typename UT>
static UT __kmp_wait_yield(volatile UT *spinner, UT checker,
                           kmp_uint32 (*pred)(UT, UT)
                               USE_ITT_BUILD_ARG(void *obj)) {
  // note: we may not belong to a team at this point
  volatile UT *spin = spinner;
  UT check = checker;
  kmp_uint32 spins;
  kmp_uint32 (*f)(UT, UT) = pred;
  UT r;

  KMP_FSYNC_SPIN_INIT(obj, CCAST(UT *, spin));
  KMP_INIT_YIELD(spins);
  // main wait spin loop
  while (!f(r = *spin, check)) {
    KMP_FSYNC_SPIN_PREPARE(obj);
    /* GEH - remove this since it was accidentally introduced when kmp_wait was
       split.
       It causes problems with infinite recursion because of exit lock */
    /* if ( TCR_4(__kmp_global.g.g_done) && __kmp_global.g.g_abort)
        __kmp_abort_thread(); */

    // if we are oversubscribed,
    // or have waited a bit (and KMP_LIBRARY=throughput, then yield
    // pause is in the following code
    KMP_YIELD(TCR_4(__kmp_nth) > __kmp_avail_proc);
    KMP_YIELD_SPIN(spins);
  }
  KMP_FSYNC_SPIN_ACQUIRED(obj);
  return r;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

template <typename UT>
void __kmp_dispatch_deo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  dispatch_private_info_template<UT> *pr;

  int gtid = *gtid_ref;
  //    int  cid = *cid_ref;
  kmp_info_t *th = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(th->th.th_dispatch);

  KD_TRACE(100, ("__kmp_dispatch_deo: T#%d called\n", gtid));
  if (__kmp_env_consistency_check) {
    pr = reinterpret_cast<dispatch_private_info_template<UT> *>(
        th->th.th_dispatch->th_dispatch_pr_current);
    if (pr->pushed_ws != ct_none) {
#if KMP_USE_DYNAMIC_LOCK
      __kmp_push_sync(gtid, ct_ordered_in_pdo, loc_ref, NULL, 0);
#else
      __kmp_push_sync(gtid, ct_ordered_in_pdo, loc_ref, NULL);
#endif
    }
  }

  if (!th->th.th_team->t.t_serialized) {
    dispatch_shared_info_template<UT> *sh =
        reinterpret_cast<dispatch_shared_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_sh_current);
    UT lower;

    if (!__kmp_env_consistency_check) {
      pr = reinterpret_cast<dispatch_private_info_template<UT> *>(
          th->th.th_dispatch->th_dispatch_pr_current);
    }
    lower = pr->u.p.ordered_lower;

#if !defined(KMP_GOMP_COMPAT)
    if (__kmp_env_consistency_check) {
      if (pr->ordered_bumped) {
        struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
        __kmp_error_construct2(kmp_i18n_msg_CnsMultipleNesting,
                               ct_ordered_in_pdo, loc_ref,
                               &p->stack_data[p->w_top]);
      }
    }
#endif /* !defined(KMP_GOMP_COMPAT) */

    KMP_MB();
#ifdef KMP_DEBUG
    {
      char *buff;
      // create format specifiers before the debug output
      buff = __kmp_str_format("__kmp_dispatch_deo: T#%%d before wait: "
                              "ordered_iter:%%%s lower:%%%s\n",
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
      buff = __kmp_str_format("__kmp_dispatch_deo: T#%%d after wait: "
                              "ordered_iter:%%%s lower:%%%s\n",
                              traits_t<UT>::spec, traits_t<UT>::spec);
      KD_TRACE(1000, (buff, gtid, sh->u.s.ordered_iteration, lower));
      __kmp_str_free(&buff);
    }
#endif
  }
  KD_TRACE(100, ("__kmp_dispatch_deo: T#%d returned\n", gtid));
}

template <typename UT>
void __kmp_dispatch_dxo(int *gtid_ref, int *cid_ref, ident_t *loc_ref) {
  typedef typename traits_t<UT>::signed_t ST;
  dispatch_private_info_template<UT> *pr;

  int gtid = *gtid_ref;
  //    int  cid = *cid_ref;
  kmp_info_t *th = __kmp_threads[gtid];
  KMP_DEBUG_ASSERT(th->th.th_dispatch);

  KD_TRACE(100, ("__kmp_dispatch_dxo: T#%d called\n", gtid));
  if (__kmp_env_consistency_check) {
    pr = reinterpret_cast<dispatch_private_info_template<UT> *>(
        th->th.th_dispatch->th_dispatch_pr_current);
    if (pr->pushed_ws != ct_none) {
      __kmp_pop_sync(gtid, ct_ordered_in_pdo, loc_ref);
    }
  }

  if (!th->th.th_team->t.t_serialized) {
    dispatch_shared_info_template<UT> *sh =
        reinterpret_cast<dispatch_shared_info_template<UT> *>(
            th->th.th_dispatch->th_dispatch_sh_current);

    if (!__kmp_env_consistency_check) {
      pr = reinterpret_cast<dispatch_private_info_template<UT> *>(
          th->th.th_dispatch->th_dispatch_pr_current);
    }

    KMP_FSYNC_RELEASING(CCAST(UT *, &sh->u.s.ordered_iteration));
#if !defined(KMP_GOMP_COMPAT)
    if (__kmp_env_consistency_check) {
      if (pr->ordered_bumped != 0) {
        struct cons_header *p = __kmp_threads[gtid]->th.th_cons;
        /* How to test it? - OM */
        __kmp_error_construct2(kmp_i18n_msg_CnsMultipleNesting,
                               ct_ordered_in_pdo, loc_ref,
                               &p->stack_data[p->w_top]);
      }
    }
#endif /* !defined(KMP_GOMP_COMPAT) */

    KMP_MB(); /* Flush all pending memory write invalidates.  */

    pr->ordered_bumped += 1;

    KD_TRACE(1000,
             ("__kmp_dispatch_dxo: T#%d bumping ordered ordered_bumped=%d\n",
              gtid, pr->ordered_bumped));

    KMP_MB(); /* Flush all pending memory write invalidates.  */

    /* TODO use general release procedure? */
    test_then_inc<ST>((volatile ST *)&sh->u.s.ordered_iteration);

    KMP_MB(); /* Flush all pending memory write invalidates.  */
  }
  KD_TRACE(100, ("__kmp_dispatch_dxo: T#%d returned\n", gtid));
}

/* Computes and returns x to the power of y, where y must a non-negative integer
 */
template <typename UT>
static __forceinline long double __kmp_pow(long double x, UT y) {
  long double s = 1.0L;

  KMP_DEBUG_ASSERT(x > 0.0 && x < 1.0);
  // KMP_DEBUG_ASSERT(y >= 0); // y is unsigned
  while (y) {
    if (y & 1)
      s *= x;
    x *= x;
    y >>= 1;
  }
  return s;
}

/* Computes and returns the number of unassigned iterations after idx chunks
   have been assigned
   (the total number of unassigned iterations in chunks with index greater than
   or equal to idx).
   __forceinline seems to be broken so that if we __forceinline this function,
   the behavior is wrong
   (one of the unit tests, sch_guided_analytical_basic.cpp, fails)
*/
template <typename T>
static __inline typename traits_t<T>::unsigned_t
__kmp_dispatch_guided_remaining(T tc, typename traits_t<T>::floating_t base,
                                typename traits_t<T>::unsigned_t idx) {
  /* Note: On Windows* OS on IA-32 architecture and Intel(R) 64, at
     least for ICL 8.1, long double arithmetic may not really have
     long double precision, even with /Qlong_double.  Currently, we
     workaround that in the caller code, by manipulating the FPCW for
     Windows* OS on IA-32 architecture.  The lack of precision is not
     expected to be a correctness issue, though.
  */
  typedef typename traits_t<T>::unsigned_t UT;

  long double x = tc * __kmp_pow<UT>(base, idx);
  UT r = (UT)x;
  if (x == r)
    return r;
  return r + 1;
}

// Parameters of the guided-iterative algorithm:
//   p2 = n * nproc * ( chunk + 1 )  // point of switching to dynamic
//   p3 = 1 / ( n * nproc )          // remaining iterations multiplier
// by default n = 2. For example with n = 3 the chunks distribution will be more
// flat.
// With n = 1 first chunk is the same as for static schedule, e.g. trip / nproc.
static const int guided_int_param = 2;
static const double guided_flt_param = 0.5; // = 1.0 / guided_int_param;
#endif // KMP_DISPATCH_H
