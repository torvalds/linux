/*
 * kmp_barrier.cpp
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
#include "kmp_wait_release.h"
#include "kmp_itt.h"
#include "kmp_os.h"
#include "kmp_stats.h"
#if OMPT_SUPPORT
#include "ompt-specific.h"
#endif

#if KMP_MIC
#include <immintrin.h>
#define USE_NGO_STORES 1
#endif // KMP_MIC

#include "tsan_annotations.h"

#if KMP_MIC && USE_NGO_STORES
// ICV copying
#define ngo_load(src) __m512d Vt = _mm512_load_pd((void *)(src))
#define ngo_store_icvs(dst, src) _mm512_storenrngo_pd((void *)(dst), Vt)
#define ngo_store_go(dst, src) _mm512_storenrngo_pd((void *)(dst), Vt)
#define ngo_sync() __asm__ volatile("lock; addl $0,0(%%rsp)" ::: "memory")
#else
#define ngo_load(src) ((void)0)
#define ngo_store_icvs(dst, src) copy_icvs((dst), (src))
#define ngo_store_go(dst, src) KMP_MEMCPY((dst), (src), CACHE_LINE)
#define ngo_sync() ((void)0)
#endif /* KMP_MIC && USE_NGO_STORES */

void __kmp_print_structure(void); // Forward declaration

// ---------------------------- Barrier Algorithms ----------------------------

// Linear Barrier
static void __kmp_linear_barrier_gather(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    void (*reduce)(void *, void *) USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_linear_gather);
  kmp_team_t *team = this_thr->th.th_team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_info_t **other_threads = team->t.t_threads;

  KA_TRACE(
      20,
      ("__kmp_linear_barrier_gather: T#%d(%d:%d) enter for barrier type %d\n",
       gtid, team->t.t_id, tid, bt));
  KMP_DEBUG_ASSERT(this_thr == other_threads[this_thr->th.th_info.ds.ds_tid]);

#if USE_ITT_BUILD && USE_ITT_NOTIFY
  // Barrier imbalance - save arrive time to the thread
  if (__kmp_forkjoin_frames_mode == 3 || __kmp_forkjoin_frames_mode == 2) {
    this_thr->th.th_bar_arrive_time = this_thr->th.th_bar_min_time =
        __itt_get_timestamp();
  }
#endif
  // We now perform a linear reduction to signal that all of the threads have
  // arrived.
  if (!KMP_MASTER_TID(tid)) {
    KA_TRACE(20,
             ("__kmp_linear_barrier_gather: T#%d(%d:%d) releasing T#%d(%d:%d)"
              "arrived(%p): %llu => %llu\n",
              gtid, team->t.t_id, tid, __kmp_gtid_from_tid(0, team),
              team->t.t_id, 0, &thr_bar->b_arrived, thr_bar->b_arrived,
              thr_bar->b_arrived + KMP_BARRIER_STATE_BUMP));
    // Mark arrival to master thread
    /* After performing this write, a worker thread may not assume that the team
       is valid any more - it could be deallocated by the master thread at any
       time. */
    ANNOTATE_BARRIER_BEGIN(this_thr);
    kmp_flag_64 flag(&thr_bar->b_arrived, other_threads[0]);
    flag.release();
  } else {
    kmp_balign_team_t *team_bar = &team->t.t_bar[bt];
    int nproc = this_thr->th.th_team_nproc;
    int i;
    // Don't have to worry about sleep bit here or atomic since team setting
    kmp_uint64 new_state = team_bar->b_arrived + KMP_BARRIER_STATE_BUMP;

    // Collect all the worker team member threads.
    for (i = 1; i < nproc; ++i) {
#if KMP_CACHE_MANAGE
      // Prefetch next thread's arrived count
      if (i + 1 < nproc)
        KMP_CACHE_PREFETCH(&other_threads[i + 1]->th.th_bar[bt].bb.b_arrived);
#endif /* KMP_CACHE_MANAGE */
      KA_TRACE(20, ("__kmp_linear_barrier_gather: T#%d(%d:%d) wait T#%d(%d:%d) "
                    "arrived(%p) == %llu\n",
                    gtid, team->t.t_id, tid, __kmp_gtid_from_tid(i, team),
                    team->t.t_id, i,
                    &other_threads[i]->th.th_bar[bt].bb.b_arrived, new_state));

      // Wait for worker thread to arrive
      kmp_flag_64 flag(&other_threads[i]->th.th_bar[bt].bb.b_arrived,
                       new_state);
      flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
      ANNOTATE_BARRIER_END(other_threads[i]);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
      // Barrier imbalance - write min of the thread time and the other thread
      // time to the thread.
      if (__kmp_forkjoin_frames_mode == 2) {
        this_thr->th.th_bar_min_time = KMP_MIN(
            this_thr->th.th_bar_min_time, other_threads[i]->th.th_bar_min_time);
      }
#endif
      if (reduce) {
        KA_TRACE(100,
                 ("__kmp_linear_barrier_gather: T#%d(%d:%d) += T#%d(%d:%d)\n",
                  gtid, team->t.t_id, tid, __kmp_gtid_from_tid(i, team),
                  team->t.t_id, i));
        ANNOTATE_REDUCE_AFTER(reduce);
        (*reduce)(this_thr->th.th_local.reduce_data,
                  other_threads[i]->th.th_local.reduce_data);
        ANNOTATE_REDUCE_BEFORE(reduce);
        ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
      }
    }
    // Don't have to worry about sleep bit here or atomic since team setting
    team_bar->b_arrived = new_state;
    KA_TRACE(20, ("__kmp_linear_barrier_gather: T#%d(%d:%d) set team %d "
                  "arrived(%p) = %llu\n",
                  gtid, team->t.t_id, tid, team->t.t_id, &team_bar->b_arrived,
                  new_state));
  }
  KA_TRACE(
      20,
      ("__kmp_linear_barrier_gather: T#%d(%d:%d) exit for barrier type %d\n",
       gtid, team->t.t_id, tid, bt));
}

static void __kmp_linear_barrier_release(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    int propagate_icvs USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_linear_release);
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_team_t *team;

  if (KMP_MASTER_TID(tid)) {
    unsigned int i;
    kmp_uint32 nproc = this_thr->th.th_team_nproc;
    kmp_info_t **other_threads;

    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    other_threads = team->t.t_threads;

    KA_TRACE(20, ("__kmp_linear_barrier_release: T#%d(%d:%d) master enter for "
                  "barrier type %d\n",
                  gtid, team->t.t_id, tid, bt));

    if (nproc > 1) {
#if KMP_BARRIER_ICV_PUSH
      {
        KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(USER_icv_copy);
        if (propagate_icvs) {
          ngo_load(&team->t.t_implicit_task_taskdata[0].td_icvs);
          for (i = 1; i < nproc; ++i) {
            __kmp_init_implicit_task(team->t.t_ident, team->t.t_threads[i],
                                     team, i, FALSE);
            ngo_store_icvs(&team->t.t_implicit_task_taskdata[i].td_icvs,
                           &team->t.t_implicit_task_taskdata[0].td_icvs);
          }
          ngo_sync();
        }
      }
#endif // KMP_BARRIER_ICV_PUSH

      // Now, release all of the worker threads
      for (i = 1; i < nproc; ++i) {
#if KMP_CACHE_MANAGE
        // Prefetch next thread's go flag
        if (i + 1 < nproc)
          KMP_CACHE_PREFETCH(&other_threads[i + 1]->th.th_bar[bt].bb.b_go);
#endif /* KMP_CACHE_MANAGE */
        KA_TRACE(
            20,
            ("__kmp_linear_barrier_release: T#%d(%d:%d) releasing T#%d(%d:%d) "
             "go(%p): %u => %u\n",
             gtid, team->t.t_id, tid, other_threads[i]->th.th_info.ds.ds_gtid,
             team->t.t_id, i, &other_threads[i]->th.th_bar[bt].bb.b_go,
             other_threads[i]->th.th_bar[bt].bb.b_go,
             other_threads[i]->th.th_bar[bt].bb.b_go + KMP_BARRIER_STATE_BUMP));
        ANNOTATE_BARRIER_BEGIN(other_threads[i]);
        kmp_flag_64 flag(&other_threads[i]->th.th_bar[bt].bb.b_go,
                         other_threads[i]);
        flag.release();
      }
    }
  } else { // Wait for the MASTER thread to release us
    KA_TRACE(20, ("__kmp_linear_barrier_release: T#%d wait go(%p) == %u\n",
                  gtid, &thr_bar->b_go, KMP_BARRIER_STATE_BUMP));
    kmp_flag_64 flag(&thr_bar->b_go, KMP_BARRIER_STATE_BUMP);
    flag.wait(this_thr, TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    ANNOTATE_BARRIER_END(this_thr);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
    if ((__itt_sync_create_ptr && itt_sync_obj == NULL) || KMP_ITT_DEBUG) {
      // In a fork barrier; cannot get the object reliably (or ITTNOTIFY is
      // disabled)
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier, 0, -1);
      // Cancel wait on previous parallel region...
      __kmp_itt_task_starting(itt_sync_obj);

      if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
        return;

      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
      if (itt_sync_obj != NULL)
        // Call prepare as early as possible for "new" barrier
        __kmp_itt_task_finished(itt_sync_obj);
    } else
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
        // Early exit for reaping threads releasing forkjoin barrier
        if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
      return;
// The worker thread may now assume that the team is valid.
#ifdef KMP_DEBUG
    tid = __kmp_tid_from_gtid(gtid);
    team = __kmp_threads[gtid]->th.th_team;
#endif
    KMP_DEBUG_ASSERT(team != NULL);
    TCW_4(thr_bar->b_go, KMP_INIT_BARRIER_STATE);
    KA_TRACE(20,
             ("__kmp_linear_barrier_release: T#%d(%d:%d) set go(%p) = %u\n",
              gtid, team->t.t_id, tid, &thr_bar->b_go, KMP_INIT_BARRIER_STATE));
    KMP_MB(); // Flush all pending memory write invalidates.
  }
  KA_TRACE(
      20,
      ("__kmp_linear_barrier_release: T#%d(%d:%d) exit for barrier type %d\n",
       gtid, team->t.t_id, tid, bt));
}

// Tree barrier
static void
__kmp_tree_barrier_gather(enum barrier_type bt, kmp_info_t *this_thr, int gtid,
                          int tid, void (*reduce)(void *, void *)
                                       USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_tree_gather);
  kmp_team_t *team = this_thr->th.th_team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_info_t **other_threads = team->t.t_threads;
  kmp_uint32 nproc = this_thr->th.th_team_nproc;
  kmp_uint32 branch_bits = __kmp_barrier_gather_branch_bits[bt];
  kmp_uint32 branch_factor = 1 << branch_bits;
  kmp_uint32 child;
  kmp_uint32 child_tid;
  kmp_uint64 new_state;

  KA_TRACE(
      20, ("__kmp_tree_barrier_gather: T#%d(%d:%d) enter for barrier type %d\n",
           gtid, team->t.t_id, tid, bt));
  KMP_DEBUG_ASSERT(this_thr == other_threads[this_thr->th.th_info.ds.ds_tid]);

#if USE_ITT_BUILD && USE_ITT_NOTIFY
  // Barrier imbalance - save arrive time to the thread
  if (__kmp_forkjoin_frames_mode == 3 || __kmp_forkjoin_frames_mode == 2) {
    this_thr->th.th_bar_arrive_time = this_thr->th.th_bar_min_time =
        __itt_get_timestamp();
  }
#endif
  // Perform tree gather to wait until all threads have arrived; reduce any
  // required data as we go
  child_tid = (tid << branch_bits) + 1;
  if (child_tid < nproc) {
    // Parent threads wait for all their children to arrive
    new_state = team->t.t_bar[bt].b_arrived + KMP_BARRIER_STATE_BUMP;
    child = 1;
    do {
      kmp_info_t *child_thr = other_threads[child_tid];
      kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
#if KMP_CACHE_MANAGE
      // Prefetch next thread's arrived count
      if (child + 1 <= branch_factor && child_tid + 1 < nproc)
        KMP_CACHE_PREFETCH(
            &other_threads[child_tid + 1]->th.th_bar[bt].bb.b_arrived);
#endif /* KMP_CACHE_MANAGE */
      KA_TRACE(20,
               ("__kmp_tree_barrier_gather: T#%d(%d:%d) wait T#%d(%d:%u) "
                "arrived(%p) == %llu\n",
                gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                team->t.t_id, child_tid, &child_bar->b_arrived, new_state));
      // Wait for child to arrive
      kmp_flag_64 flag(&child_bar->b_arrived, new_state);
      flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
      ANNOTATE_BARRIER_END(child_thr);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
      // Barrier imbalance - write min of the thread time and a child time to
      // the thread.
      if (__kmp_forkjoin_frames_mode == 2) {
        this_thr->th.th_bar_min_time = KMP_MIN(this_thr->th.th_bar_min_time,
                                               child_thr->th.th_bar_min_time);
      }
#endif
      if (reduce) {
        KA_TRACE(100,
                 ("__kmp_tree_barrier_gather: T#%d(%d:%d) += T#%d(%d:%u)\n",
                  gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                  team->t.t_id, child_tid));
        ANNOTATE_REDUCE_AFTER(reduce);
        (*reduce)(this_thr->th.th_local.reduce_data,
                  child_thr->th.th_local.reduce_data);
        ANNOTATE_REDUCE_BEFORE(reduce);
        ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
      }
      child++;
      child_tid++;
    } while (child <= branch_factor && child_tid < nproc);
  }

  if (!KMP_MASTER_TID(tid)) { // Worker threads
    kmp_int32 parent_tid = (tid - 1) >> branch_bits;

    KA_TRACE(20,
             ("__kmp_tree_barrier_gather: T#%d(%d:%d) releasing T#%d(%d:%d) "
              "arrived(%p): %llu => %llu\n",
              gtid, team->t.t_id, tid, __kmp_gtid_from_tid(parent_tid, team),
              team->t.t_id, parent_tid, &thr_bar->b_arrived, thr_bar->b_arrived,
              thr_bar->b_arrived + KMP_BARRIER_STATE_BUMP));

    // Mark arrival to parent thread
    /* After performing this write, a worker thread may not assume that the team
       is valid any more - it could be deallocated by the master thread at any
       time.  */
    ANNOTATE_BARRIER_BEGIN(this_thr);
    kmp_flag_64 flag(&thr_bar->b_arrived, other_threads[parent_tid]);
    flag.release();
  } else {
    // Need to update the team arrived pointer if we are the master thread
    if (nproc > 1) // New value was already computed above
      team->t.t_bar[bt].b_arrived = new_state;
    else
      team->t.t_bar[bt].b_arrived += KMP_BARRIER_STATE_BUMP;
    KA_TRACE(20, ("__kmp_tree_barrier_gather: T#%d(%d:%d) set team %d "
                  "arrived(%p) = %llu\n",
                  gtid, team->t.t_id, tid, team->t.t_id,
                  &team->t.t_bar[bt].b_arrived, team->t.t_bar[bt].b_arrived));
  }
  KA_TRACE(20,
           ("__kmp_tree_barrier_gather: T#%d(%d:%d) exit for barrier type %d\n",
            gtid, team->t.t_id, tid, bt));
}

static void __kmp_tree_barrier_release(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    int propagate_icvs USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_tree_release);
  kmp_team_t *team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_uint32 nproc;
  kmp_uint32 branch_bits = __kmp_barrier_release_branch_bits[bt];
  kmp_uint32 branch_factor = 1 << branch_bits;
  kmp_uint32 child;
  kmp_uint32 child_tid;

  // Perform a tree release for all of the threads that have been gathered
  if (!KMP_MASTER_TID(
          tid)) { // Handle fork barrier workers who aren't part of a team yet
    KA_TRACE(20, ("__kmp_tree_barrier_release: T#%d wait go(%p) == %u\n", gtid,
                  &thr_bar->b_go, KMP_BARRIER_STATE_BUMP));
    // Wait for parent thread to release us
    kmp_flag_64 flag(&thr_bar->b_go, KMP_BARRIER_STATE_BUMP);
    flag.wait(this_thr, TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    ANNOTATE_BARRIER_END(this_thr);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
    if ((__itt_sync_create_ptr && itt_sync_obj == NULL) || KMP_ITT_DEBUG) {
      // In fork barrier where we could not get the object reliably (or
      // ITTNOTIFY is disabled)
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier, 0, -1);
      // Cancel wait on previous parallel region...
      __kmp_itt_task_starting(itt_sync_obj);

      if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
        return;

      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
      if (itt_sync_obj != NULL)
        // Call prepare as early as possible for "new" barrier
        __kmp_itt_task_finished(itt_sync_obj);
    } else
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
        // Early exit for reaping threads releasing forkjoin barrier
        if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
      return;

    // The worker thread may now assume that the team is valid.
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    tid = __kmp_tid_from_gtid(gtid);

    TCW_4(thr_bar->b_go, KMP_INIT_BARRIER_STATE);
    KA_TRACE(20,
             ("__kmp_tree_barrier_release: T#%d(%d:%d) set go(%p) = %u\n", gtid,
              team->t.t_id, tid, &thr_bar->b_go, KMP_INIT_BARRIER_STATE));
    KMP_MB(); // Flush all pending memory write invalidates.
  } else {
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    KA_TRACE(20, ("__kmp_tree_barrier_release: T#%d(%d:%d) master enter for "
                  "barrier type %d\n",
                  gtid, team->t.t_id, tid, bt));
  }
  nproc = this_thr->th.th_team_nproc;
  child_tid = (tid << branch_bits) + 1;

  if (child_tid < nproc) {
    kmp_info_t **other_threads = team->t.t_threads;
    child = 1;
    // Parent threads release all their children
    do {
      kmp_info_t *child_thr = other_threads[child_tid];
      kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
#if KMP_CACHE_MANAGE
      // Prefetch next thread's go count
      if (child + 1 <= branch_factor && child_tid + 1 < nproc)
        KMP_CACHE_PREFETCH(
            &other_threads[child_tid + 1]->th.th_bar[bt].bb.b_go);
#endif /* KMP_CACHE_MANAGE */

#if KMP_BARRIER_ICV_PUSH
      {
        KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(USER_icv_copy);
        if (propagate_icvs) {
          __kmp_init_implicit_task(team->t.t_ident,
                                   team->t.t_threads[child_tid], team,
                                   child_tid, FALSE);
          copy_icvs(&team->t.t_implicit_task_taskdata[child_tid].td_icvs,
                    &team->t.t_implicit_task_taskdata[0].td_icvs);
        }
      }
#endif // KMP_BARRIER_ICV_PUSH
      KA_TRACE(20,
               ("__kmp_tree_barrier_release: T#%d(%d:%d) releasing T#%d(%d:%u)"
                "go(%p): %u => %u\n",
                gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                team->t.t_id, child_tid, &child_bar->b_go, child_bar->b_go,
                child_bar->b_go + KMP_BARRIER_STATE_BUMP));
      // Release child from barrier
      ANNOTATE_BARRIER_BEGIN(child_thr);
      kmp_flag_64 flag(&child_bar->b_go, child_thr);
      flag.release();
      child++;
      child_tid++;
    } while (child <= branch_factor && child_tid < nproc);
  }
  KA_TRACE(
      20, ("__kmp_tree_barrier_release: T#%d(%d:%d) exit for barrier type %d\n",
           gtid, team->t.t_id, tid, bt));
}

// Hyper Barrier
static void
__kmp_hyper_barrier_gather(enum barrier_type bt, kmp_info_t *this_thr, int gtid,
                           int tid, void (*reduce)(void *, void *)
                                        USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_hyper_gather);
  kmp_team_t *team = this_thr->th.th_team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_info_t **other_threads = team->t.t_threads;
  kmp_uint64 new_state = KMP_BARRIER_UNUSED_STATE;
  kmp_uint32 num_threads = this_thr->th.th_team_nproc;
  kmp_uint32 branch_bits = __kmp_barrier_gather_branch_bits[bt];
  kmp_uint32 branch_factor = 1 << branch_bits;
  kmp_uint32 offset;
  kmp_uint32 level;

  KA_TRACE(
      20,
      ("__kmp_hyper_barrier_gather: T#%d(%d:%d) enter for barrier type %d\n",
       gtid, team->t.t_id, tid, bt));
  KMP_DEBUG_ASSERT(this_thr == other_threads[this_thr->th.th_info.ds.ds_tid]);

#if USE_ITT_BUILD && USE_ITT_NOTIFY
  // Barrier imbalance - save arrive time to the thread
  if (__kmp_forkjoin_frames_mode == 3 || __kmp_forkjoin_frames_mode == 2) {
    this_thr->th.th_bar_arrive_time = this_thr->th.th_bar_min_time =
        __itt_get_timestamp();
  }
#endif
  /* Perform a hypercube-embedded tree gather to wait until all of the threads
     have arrived, and reduce any required data as we go.  */
  kmp_flag_64 p_flag(&thr_bar->b_arrived);
  for (level = 0, offset = 1; offset < num_threads;
       level += branch_bits, offset <<= branch_bits) {
    kmp_uint32 child;
    kmp_uint32 child_tid;

    if (((tid >> level) & (branch_factor - 1)) != 0) {
      kmp_int32 parent_tid = tid & ~((1 << (level + branch_bits)) - 1);

      KA_TRACE(20,
               ("__kmp_hyper_barrier_gather: T#%d(%d:%d) releasing T#%d(%d:%d) "
                "arrived(%p): %llu => %llu\n",
                gtid, team->t.t_id, tid, __kmp_gtid_from_tid(parent_tid, team),
                team->t.t_id, parent_tid, &thr_bar->b_arrived,
                thr_bar->b_arrived,
                thr_bar->b_arrived + KMP_BARRIER_STATE_BUMP));
      // Mark arrival to parent thread
      /* After performing this write (in the last iteration of the enclosing for
         loop), a worker thread may not assume that the team is valid any more
         - it could be deallocated by the master thread at any time.  */
      ANNOTATE_BARRIER_BEGIN(this_thr);
      p_flag.set_waiter(other_threads[parent_tid]);
      p_flag.release();
      break;
    }

    // Parent threads wait for children to arrive
    if (new_state == KMP_BARRIER_UNUSED_STATE)
      new_state = team->t.t_bar[bt].b_arrived + KMP_BARRIER_STATE_BUMP;
    for (child = 1, child_tid = tid + (1 << level);
         child < branch_factor && child_tid < num_threads;
         child++, child_tid += (1 << level)) {
      kmp_info_t *child_thr = other_threads[child_tid];
      kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
#if KMP_CACHE_MANAGE
      kmp_uint32 next_child_tid = child_tid + (1 << level);
      // Prefetch next thread's arrived count
      if (child + 1 < branch_factor && next_child_tid < num_threads)
        KMP_CACHE_PREFETCH(
            &other_threads[next_child_tid]->th.th_bar[bt].bb.b_arrived);
#endif /* KMP_CACHE_MANAGE */
      KA_TRACE(20,
               ("__kmp_hyper_barrier_gather: T#%d(%d:%d) wait T#%d(%d:%u) "
                "arrived(%p) == %llu\n",
                gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                team->t.t_id, child_tid, &child_bar->b_arrived, new_state));
      // Wait for child to arrive
      kmp_flag_64 c_flag(&child_bar->b_arrived, new_state);
      c_flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
      ANNOTATE_BARRIER_END(child_thr);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
      // Barrier imbalance - write min of the thread time and a child time to
      // the thread.
      if (__kmp_forkjoin_frames_mode == 2) {
        this_thr->th.th_bar_min_time = KMP_MIN(this_thr->th.th_bar_min_time,
                                               child_thr->th.th_bar_min_time);
      }
#endif
      if (reduce) {
        KA_TRACE(100,
                 ("__kmp_hyper_barrier_gather: T#%d(%d:%d) += T#%d(%d:%u)\n",
                  gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                  team->t.t_id, child_tid));
        ANNOTATE_REDUCE_AFTER(reduce);
        (*reduce)(this_thr->th.th_local.reduce_data,
                  child_thr->th.th_local.reduce_data);
        ANNOTATE_REDUCE_BEFORE(reduce);
        ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
      }
    }
  }

  if (KMP_MASTER_TID(tid)) {
    // Need to update the team arrived pointer if we are the master thread
    if (new_state == KMP_BARRIER_UNUSED_STATE)
      team->t.t_bar[bt].b_arrived += KMP_BARRIER_STATE_BUMP;
    else
      team->t.t_bar[bt].b_arrived = new_state;
    KA_TRACE(20, ("__kmp_hyper_barrier_gather: T#%d(%d:%d) set team %d "
                  "arrived(%p) = %llu\n",
                  gtid, team->t.t_id, tid, team->t.t_id,
                  &team->t.t_bar[bt].b_arrived, team->t.t_bar[bt].b_arrived));
  }
  KA_TRACE(
      20, ("__kmp_hyper_barrier_gather: T#%d(%d:%d) exit for barrier type %d\n",
           gtid, team->t.t_id, tid, bt));
}

// The reverse versions seem to beat the forward versions overall
#define KMP_REVERSE_HYPER_BAR
static void __kmp_hyper_barrier_release(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    int propagate_icvs USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_hyper_release);
  kmp_team_t *team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_info_t **other_threads;
  kmp_uint32 num_threads;
  kmp_uint32 branch_bits = __kmp_barrier_release_branch_bits[bt];
  kmp_uint32 branch_factor = 1 << branch_bits;
  kmp_uint32 child;
  kmp_uint32 child_tid;
  kmp_uint32 offset;
  kmp_uint32 level;

  /* Perform a hypercube-embedded tree release for all of the threads that have
     been gathered. If KMP_REVERSE_HYPER_BAR is defined (default) the threads
     are released in the reverse order of the corresponding gather, otherwise
     threads are released in the same order. */
  if (KMP_MASTER_TID(tid)) { // master
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    KA_TRACE(20, ("__kmp_hyper_barrier_release: T#%d(%d:%d) master enter for "
                  "barrier type %d\n",
                  gtid, team->t.t_id, tid, bt));
#if KMP_BARRIER_ICV_PUSH
    if (propagate_icvs) { // master already has ICVs in final destination; copy
      copy_icvs(&thr_bar->th_fixed_icvs,
                &team->t.t_implicit_task_taskdata[tid].td_icvs);
    }
#endif
  } else { // Handle fork barrier workers who aren't part of a team yet
    KA_TRACE(20, ("__kmp_hyper_barrier_release: T#%d wait go(%p) == %u\n", gtid,
                  &thr_bar->b_go, KMP_BARRIER_STATE_BUMP));
    // Wait for parent thread to release us
    kmp_flag_64 flag(&thr_bar->b_go, KMP_BARRIER_STATE_BUMP);
    flag.wait(this_thr, TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    ANNOTATE_BARRIER_END(this_thr);
#if USE_ITT_BUILD && USE_ITT_NOTIFY
    if ((__itt_sync_create_ptr && itt_sync_obj == NULL) || KMP_ITT_DEBUG) {
      // In fork barrier where we could not get the object reliably
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier, 0, -1);
      // Cancel wait on previous parallel region...
      __kmp_itt_task_starting(itt_sync_obj);

      if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
        return;

      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
      if (itt_sync_obj != NULL)
        // Call prepare as early as possible for "new" barrier
        __kmp_itt_task_finished(itt_sync_obj);
    } else
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
        // Early exit for reaping threads releasing forkjoin barrier
        if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
      return;

    // The worker thread may now assume that the team is valid.
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    tid = __kmp_tid_from_gtid(gtid);

    TCW_4(thr_bar->b_go, KMP_INIT_BARRIER_STATE);
    KA_TRACE(20,
             ("__kmp_hyper_barrier_release: T#%d(%d:%d) set go(%p) = %u\n",
              gtid, team->t.t_id, tid, &thr_bar->b_go, KMP_INIT_BARRIER_STATE));
    KMP_MB(); // Flush all pending memory write invalidates.
  }
  num_threads = this_thr->th.th_team_nproc;
  other_threads = team->t.t_threads;

#ifdef KMP_REVERSE_HYPER_BAR
  // Count up to correct level for parent
  for (level = 0, offset = 1;
       offset < num_threads && (((tid >> level) & (branch_factor - 1)) == 0);
       level += branch_bits, offset <<= branch_bits)
    ;

  // Now go down from there
  for (level -= branch_bits, offset >>= branch_bits; offset != 0;
       level -= branch_bits, offset >>= branch_bits)
#else
  // Go down the tree, level by level
  for (level = 0, offset = 1; offset < num_threads;
       level += branch_bits, offset <<= branch_bits)
#endif // KMP_REVERSE_HYPER_BAR
  {
#ifdef KMP_REVERSE_HYPER_BAR
    /* Now go in reverse order through the children, highest to lowest.
       Initial setting of child is conservative here. */
    child = num_threads >> ((level == 0) ? level : level - 1);
    for (child = (child < branch_factor - 1) ? child : branch_factor - 1,
        child_tid = tid + (child << level);
         child >= 1; child--, child_tid -= (1 << level))
#else
    if (((tid >> level) & (branch_factor - 1)) != 0)
      // No need to go lower than this, since this is the level parent would be
      // notified
      break;
    // Iterate through children on this level of the tree
    for (child = 1, child_tid = tid + (1 << level);
         child < branch_factor && child_tid < num_threads;
         child++, child_tid += (1 << level))
#endif // KMP_REVERSE_HYPER_BAR
    {
      if (child_tid >= num_threads)
        continue; // Child doesn't exist so keep going
      else {
        kmp_info_t *child_thr = other_threads[child_tid];
        kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
#if KMP_CACHE_MANAGE
        kmp_uint32 next_child_tid = child_tid - (1 << level);
// Prefetch next thread's go count
#ifdef KMP_REVERSE_HYPER_BAR
        if (child - 1 >= 1 && next_child_tid < num_threads)
#else
        if (child + 1 < branch_factor && next_child_tid < num_threads)
#endif // KMP_REVERSE_HYPER_BAR
          KMP_CACHE_PREFETCH(
              &other_threads[next_child_tid]->th.th_bar[bt].bb.b_go);
#endif /* KMP_CACHE_MANAGE */

#if KMP_BARRIER_ICV_PUSH
        if (propagate_icvs) // push my fixed ICVs to my child
          copy_icvs(&child_bar->th_fixed_icvs, &thr_bar->th_fixed_icvs);
#endif // KMP_BARRIER_ICV_PUSH

        KA_TRACE(
            20,
            ("__kmp_hyper_barrier_release: T#%d(%d:%d) releasing T#%d(%d:%u)"
             "go(%p): %u => %u\n",
             gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
             team->t.t_id, child_tid, &child_bar->b_go, child_bar->b_go,
             child_bar->b_go + KMP_BARRIER_STATE_BUMP));
        // Release child from barrier
        ANNOTATE_BARRIER_BEGIN(child_thr);
        kmp_flag_64 flag(&child_bar->b_go, child_thr);
        flag.release();
      }
    }
  }
#if KMP_BARRIER_ICV_PUSH
  if (propagate_icvs &&
      !KMP_MASTER_TID(tid)) { // copy ICVs locally to final dest
    __kmp_init_implicit_task(team->t.t_ident, team->t.t_threads[tid], team, tid,
                             FALSE);
    copy_icvs(&team->t.t_implicit_task_taskdata[tid].td_icvs,
              &thr_bar->th_fixed_icvs);
  }
#endif
  KA_TRACE(
      20,
      ("__kmp_hyper_barrier_release: T#%d(%d:%d) exit for barrier type %d\n",
       gtid, team->t.t_id, tid, bt));
}

// Hierarchical Barrier

// Initialize thread barrier data
/* Initializes/re-initializes the hierarchical barrier data stored on a thread.
   Performs the minimum amount of initialization required based on how the team
   has changed. Returns true if leaf children will require both on-core and
   traditional wake-up mechanisms. For example, if the team size increases,
   threads already in the team will respond to on-core wakeup on their parent
   thread, but threads newly added to the team will only be listening on the
   their local b_go. */
static bool __kmp_init_hierarchical_barrier_thread(enum barrier_type bt,
                                                   kmp_bstate_t *thr_bar,
                                                   kmp_uint32 nproc, int gtid,
                                                   int tid, kmp_team_t *team) {
  // Checks to determine if (re-)initialization is needed
  bool uninitialized = thr_bar->team == NULL;
  bool team_changed = team != thr_bar->team;
  bool team_sz_changed = nproc != thr_bar->nproc;
  bool tid_changed = tid != thr_bar->old_tid;
  bool retval = false;

  if (uninitialized || team_sz_changed) {
    __kmp_get_hierarchy(nproc, thr_bar);
  }

  if (uninitialized || team_sz_changed || tid_changed) {
    thr_bar->my_level = thr_bar->depth - 1; // default for master
    thr_bar->parent_tid = -1; // default for master
    if (!KMP_MASTER_TID(
            tid)) { // if not master, find parent thread in hierarchy
      kmp_uint32 d = 0;
      while (d < thr_bar->depth) { // find parent based on level of thread in
        // hierarchy, and note level
        kmp_uint32 rem;
        if (d == thr_bar->depth - 2) { // reached level right below the master
          thr_bar->parent_tid = 0;
          thr_bar->my_level = d;
          break;
        } else if ((rem = tid % thr_bar->skip_per_level[d + 1]) !=
                   0) { // TODO: can we make this op faster?
          // thread is not a subtree root at next level, so this is max
          thr_bar->parent_tid = tid - rem;
          thr_bar->my_level = d;
          break;
        }
        ++d;
      }
    }
    thr_bar->offset = 7 - (tid - thr_bar->parent_tid - 1);
    thr_bar->old_tid = tid;
    thr_bar->wait_flag = KMP_BARRIER_NOT_WAITING;
    thr_bar->team = team;
    thr_bar->parent_bar =
        &team->t.t_threads[thr_bar->parent_tid]->th.th_bar[bt].bb;
  }
  if (uninitialized || team_changed || tid_changed) {
    thr_bar->team = team;
    thr_bar->parent_bar =
        &team->t.t_threads[thr_bar->parent_tid]->th.th_bar[bt].bb;
    retval = true;
  }
  if (uninitialized || team_sz_changed || tid_changed) {
    thr_bar->nproc = nproc;
    thr_bar->leaf_kids = thr_bar->base_leaf_kids;
    if (thr_bar->my_level == 0)
      thr_bar->leaf_kids = 0;
    if (thr_bar->leaf_kids && (kmp_uint32)tid + thr_bar->leaf_kids + 1 > nproc)
      thr_bar->leaf_kids = nproc - tid - 1;
    thr_bar->leaf_state = 0;
    for (int i = 0; i < thr_bar->leaf_kids; ++i)
      ((char *)&(thr_bar->leaf_state))[7 - i] = 1;
  }
  return retval;
}

static void __kmp_hierarchical_barrier_gather(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    void (*reduce)(void *, void *) USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_hier_gather);
  kmp_team_t *team = this_thr->th.th_team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_uint32 nproc = this_thr->th.th_team_nproc;
  kmp_info_t **other_threads = team->t.t_threads;
  kmp_uint64 new_state;

  int level = team->t.t_level;
#if OMP_40_ENABLED
  if (other_threads[0]
          ->th.th_teams_microtask) // are we inside the teams construct?
    if (this_thr->th.th_teams_size.nteams > 1)
      ++level; // level was not increased in teams construct for team_of_masters
#endif
  if (level == 1)
    thr_bar->use_oncore_barrier = 1;
  else
    thr_bar->use_oncore_barrier = 0; // Do not use oncore barrier when nested

  KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) enter for "
                "barrier type %d\n",
                gtid, team->t.t_id, tid, bt));
  KMP_DEBUG_ASSERT(this_thr == other_threads[this_thr->th.th_info.ds.ds_tid]);

#if USE_ITT_BUILD && USE_ITT_NOTIFY
  // Barrier imbalance - save arrive time to the thread
  if (__kmp_forkjoin_frames_mode == 3 || __kmp_forkjoin_frames_mode == 2) {
    this_thr->th.th_bar_arrive_time = __itt_get_timestamp();
  }
#endif

  (void)__kmp_init_hierarchical_barrier_thread(bt, thr_bar, nproc, gtid, tid,
                                               team);

  if (thr_bar->my_level) { // not a leaf (my_level==0 means leaf)
    kmp_int32 child_tid;
    new_state =
        (kmp_uint64)team->t.t_bar[bt].b_arrived + KMP_BARRIER_STATE_BUMP;
    if (__kmp_dflt_blocktime == KMP_MAX_BLOCKTIME &&
        thr_bar->use_oncore_barrier) {
      if (thr_bar->leaf_kids) {
        // First, wait for leaf children to check-in on my b_arrived flag
        kmp_uint64 leaf_state =
            KMP_MASTER_TID(tid)
                ? thr_bar->b_arrived | thr_bar->leaf_state
                : team->t.t_bar[bt].b_arrived | thr_bar->leaf_state;
        KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) waiting "
                      "for leaf kids\n",
                      gtid, team->t.t_id, tid));
        kmp_flag_64 flag(&thr_bar->b_arrived, leaf_state);
        flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
        if (reduce) {
          ANNOTATE_REDUCE_AFTER(reduce);
          for (child_tid = tid + 1; child_tid <= tid + thr_bar->leaf_kids;
               ++child_tid) {
            KA_TRACE(100, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) += "
                           "T#%d(%d:%d)\n",
                           gtid, team->t.t_id, tid,
                           __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                           child_tid));
            ANNOTATE_BARRIER_END(other_threads[child_tid]);
            (*reduce)(this_thr->th.th_local.reduce_data,
                      other_threads[child_tid]->th.th_local.reduce_data);
          }
          ANNOTATE_REDUCE_BEFORE(reduce);
          ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
        }
        // clear leaf_state bits
        KMP_TEST_THEN_AND64(&thr_bar->b_arrived, ~(thr_bar->leaf_state));
      }
      // Next, wait for higher level children on each child's b_arrived flag
      for (kmp_uint32 d = 1; d < thr_bar->my_level;
           ++d) { // gather lowest level threads first, but skip 0
        kmp_uint32 last = tid + thr_bar->skip_per_level[d + 1],
                   skip = thr_bar->skip_per_level[d];
        if (last > nproc)
          last = nproc;
        for (child_tid = tid + skip; child_tid < (int)last; child_tid += skip) {
          kmp_info_t *child_thr = other_threads[child_tid];
          kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
          KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) wait "
                        "T#%d(%d:%d) "
                        "arrived(%p) == %llu\n",
                        gtid, team->t.t_id, tid,
                        __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                        child_tid, &child_bar->b_arrived, new_state));
          kmp_flag_64 flag(&child_bar->b_arrived, new_state);
          flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
          ANNOTATE_BARRIER_END(child_thr);
          if (reduce) {
            KA_TRACE(100, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) += "
                           "T#%d(%d:%d)\n",
                           gtid, team->t.t_id, tid,
                           __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                           child_tid));
            ANNOTATE_REDUCE_AFTER(reduce);
            (*reduce)(this_thr->th.th_local.reduce_data,
                      child_thr->th.th_local.reduce_data);
            ANNOTATE_REDUCE_BEFORE(reduce);
            ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
          }
        }
      }
    } else { // Blocktime is not infinite
      for (kmp_uint32 d = 0; d < thr_bar->my_level;
           ++d) { // Gather lowest level threads first
        kmp_uint32 last = tid + thr_bar->skip_per_level[d + 1],
                   skip = thr_bar->skip_per_level[d];
        if (last > nproc)
          last = nproc;
        for (child_tid = tid + skip; child_tid < (int)last; child_tid += skip) {
          kmp_info_t *child_thr = other_threads[child_tid];
          kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
          KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) wait "
                        "T#%d(%d:%d) "
                        "arrived(%p) == %llu\n",
                        gtid, team->t.t_id, tid,
                        __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                        child_tid, &child_bar->b_arrived, new_state));
          kmp_flag_64 flag(&child_bar->b_arrived, new_state);
          flag.wait(this_thr, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
          ANNOTATE_BARRIER_END(child_thr);
          if (reduce) {
            KA_TRACE(100, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) += "
                           "T#%d(%d:%d)\n",
                           gtid, team->t.t_id, tid,
                           __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                           child_tid));
            ANNOTATE_REDUCE_AFTER(reduce);
            (*reduce)(this_thr->th.th_local.reduce_data,
                      child_thr->th.th_local.reduce_data);
            ANNOTATE_REDUCE_BEFORE(reduce);
            ANNOTATE_REDUCE_BEFORE(&team->t.t_bar);
          }
        }
      }
    }
  }
  // All subordinates are gathered; now release parent if not master thread

  if (!KMP_MASTER_TID(tid)) { // worker threads release parent in hierarchy
    KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) releasing"
                  " T#%d(%d:%d) arrived(%p): %llu => %llu\n",
                  gtid, team->t.t_id, tid,
                  __kmp_gtid_from_tid(thr_bar->parent_tid, team), team->t.t_id,
                  thr_bar->parent_tid, &thr_bar->b_arrived, thr_bar->b_arrived,
                  thr_bar->b_arrived + KMP_BARRIER_STATE_BUMP));
    /* Mark arrival to parent: After performing this write, a worker thread may
       not assume that the team is valid any more - it could be deallocated by
       the master thread at any time. */
    if (thr_bar->my_level || __kmp_dflt_blocktime != KMP_MAX_BLOCKTIME ||
        !thr_bar->use_oncore_barrier) { // Parent is waiting on my b_arrived
      // flag; release it
      ANNOTATE_BARRIER_BEGIN(this_thr);
      kmp_flag_64 flag(&thr_bar->b_arrived, other_threads[thr_bar->parent_tid]);
      flag.release();
    } else {
      // Leaf does special release on "offset" bits of parent's b_arrived flag
      thr_bar->b_arrived = team->t.t_bar[bt].b_arrived + KMP_BARRIER_STATE_BUMP;
      kmp_flag_oncore flag(&thr_bar->parent_bar->b_arrived, thr_bar->offset);
      flag.set_waiter(other_threads[thr_bar->parent_tid]);
      flag.release();
    }
  } else { // Master thread needs to update the team's b_arrived value
    team->t.t_bar[bt].b_arrived = new_state;
    KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) set team %d "
                  "arrived(%p) = %llu\n",
                  gtid, team->t.t_id, tid, team->t.t_id,
                  &team->t.t_bar[bt].b_arrived, team->t.t_bar[bt].b_arrived));
  }
  // Is the team access below unsafe or just technically invalid?
  KA_TRACE(20, ("__kmp_hierarchical_barrier_gather: T#%d(%d:%d) exit for "
                "barrier type %d\n",
                gtid, team->t.t_id, tid, bt));
}

static void __kmp_hierarchical_barrier_release(
    enum barrier_type bt, kmp_info_t *this_thr, int gtid, int tid,
    int propagate_icvs USE_ITT_BUILD_ARG(void *itt_sync_obj)) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_hier_release);
  kmp_team_t *team;
  kmp_bstate_t *thr_bar = &this_thr->th.th_bar[bt].bb;
  kmp_uint32 nproc;
  bool team_change = false; // indicates on-core barrier shouldn't be used

  if (KMP_MASTER_TID(tid)) {
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    KA_TRACE(20, ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) master "
                  "entered barrier type %d\n",
                  gtid, team->t.t_id, tid, bt));
  } else { // Worker threads
    // Wait for parent thread to release me
    if (!thr_bar->use_oncore_barrier ||
        __kmp_dflt_blocktime != KMP_MAX_BLOCKTIME || thr_bar->my_level != 0 ||
        thr_bar->team == NULL) {
      // Use traditional method of waiting on my own b_go flag
      thr_bar->wait_flag = KMP_BARRIER_OWN_FLAG;
      kmp_flag_64 flag(&thr_bar->b_go, KMP_BARRIER_STATE_BUMP);
      flag.wait(this_thr, TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
      ANNOTATE_BARRIER_END(this_thr);
      TCW_8(thr_bar->b_go,
            KMP_INIT_BARRIER_STATE); // Reset my b_go flag for next time
    } else { // Thread barrier data is initialized, this is a leaf, blocktime is
      // infinite, not nested
      // Wait on my "offset" bits on parent's b_go flag
      thr_bar->wait_flag = KMP_BARRIER_PARENT_FLAG;
      kmp_flag_oncore flag(&thr_bar->parent_bar->b_go, KMP_BARRIER_STATE_BUMP,
                           thr_bar->offset, bt,
                           this_thr USE_ITT_BUILD_ARG(itt_sync_obj));
      flag.wait(this_thr, TRUE);
      if (thr_bar->wait_flag ==
          KMP_BARRIER_SWITCHING) { // Thread was switched to own b_go
        TCW_8(thr_bar->b_go,
              KMP_INIT_BARRIER_STATE); // Reset my b_go flag for next time
      } else { // Reset my bits on parent's b_go flag
        (RCAST(volatile char *,
               &(thr_bar->parent_bar->b_go)))[thr_bar->offset] = 0;
      }
    }
    thr_bar->wait_flag = KMP_BARRIER_NOT_WAITING;
    // Early exit for reaping threads releasing forkjoin barrier
    if (bt == bs_forkjoin_barrier && TCR_4(__kmp_global.g.g_done))
      return;
    // The worker thread may now assume that the team is valid.
    team = __kmp_threads[gtid]->th.th_team;
    KMP_DEBUG_ASSERT(team != NULL);
    tid = __kmp_tid_from_gtid(gtid);

    KA_TRACE(
        20,
        ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) set go(%p) = %u\n",
         gtid, team->t.t_id, tid, &thr_bar->b_go, KMP_INIT_BARRIER_STATE));
    KMP_MB(); // Flush all pending memory write invalidates.
  }

  nproc = this_thr->th.th_team_nproc;
  int level = team->t.t_level;
#if OMP_40_ENABLED
  if (team->t.t_threads[0]
          ->th.th_teams_microtask) { // are we inside the teams construct?
    if (team->t.t_pkfn != (microtask_t)__kmp_teams_master &&
        this_thr->th.th_teams_level == level)
      ++level; // level was not increased in teams construct for team_of_workers
    if (this_thr->th.th_teams_size.nteams > 1)
      ++level; // level was not increased in teams construct for team_of_masters
  }
#endif
  if (level == 1)
    thr_bar->use_oncore_barrier = 1;
  else
    thr_bar->use_oncore_barrier = 0; // Do not use oncore barrier when nested

  // If the team size has increased, we still communicate with old leaves via
  // oncore barrier.
  unsigned short int old_leaf_kids = thr_bar->leaf_kids;
  kmp_uint64 old_leaf_state = thr_bar->leaf_state;
  team_change = __kmp_init_hierarchical_barrier_thread(bt, thr_bar, nproc, gtid,
                                                       tid, team);
  // But if the entire team changes, we won't use oncore barrier at all
  if (team_change)
    old_leaf_kids = 0;

#if KMP_BARRIER_ICV_PUSH
  if (propagate_icvs) {
    __kmp_init_implicit_task(team->t.t_ident, team->t.t_threads[tid], team, tid,
                             FALSE);
    if (KMP_MASTER_TID(
            tid)) { // master already has copy in final destination; copy
      copy_icvs(&thr_bar->th_fixed_icvs,
                &team->t.t_implicit_task_taskdata[tid].td_icvs);
    } else if (__kmp_dflt_blocktime == KMP_MAX_BLOCKTIME &&
               thr_bar->use_oncore_barrier) { // optimization for inf blocktime
      if (!thr_bar->my_level) // I'm a leaf in the hierarchy (my_level==0)
        // leaves (on-core children) pull parent's fixed ICVs directly to local
        // ICV store
        copy_icvs(&team->t.t_implicit_task_taskdata[tid].td_icvs,
                  &thr_bar->parent_bar->th_fixed_icvs);
      // non-leaves will get ICVs piggybacked with b_go via NGO store
    } else { // blocktime is not infinite; pull ICVs from parent's fixed ICVs
      if (thr_bar->my_level) // not a leaf; copy ICVs to my fixed ICVs child can
        // access
        copy_icvs(&thr_bar->th_fixed_icvs, &thr_bar->parent_bar->th_fixed_icvs);
      else // leaves copy parent's fixed ICVs directly to local ICV store
        copy_icvs(&team->t.t_implicit_task_taskdata[tid].td_icvs,
                  &thr_bar->parent_bar->th_fixed_icvs);
    }
  }
#endif // KMP_BARRIER_ICV_PUSH

  // Now, release my children
  if (thr_bar->my_level) { // not a leaf
    kmp_int32 child_tid;
    kmp_uint32 last;
    if (__kmp_dflt_blocktime == KMP_MAX_BLOCKTIME &&
        thr_bar->use_oncore_barrier) {
      if (KMP_MASTER_TID(tid)) { // do a flat release
        // Set local b_go to bump children via NGO store of the cache line
        // containing IVCs and b_go.
        thr_bar->b_go = KMP_BARRIER_STATE_BUMP;
        // Use ngo stores if available; b_go piggybacks in the last 8 bytes of
        // the cache line
        ngo_load(&thr_bar->th_fixed_icvs);
        // This loops over all the threads skipping only the leaf nodes in the
        // hierarchy
        for (child_tid = thr_bar->skip_per_level[1]; child_tid < (int)nproc;
             child_tid += thr_bar->skip_per_level[1]) {
          kmp_bstate_t *child_bar =
              &team->t.t_threads[child_tid]->th.th_bar[bt].bb;
          KA_TRACE(20, ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) "
                        "releasing T#%d(%d:%d)"
                        " go(%p): %u => %u\n",
                        gtid, team->t.t_id, tid,
                        __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                        child_tid, &child_bar->b_go, child_bar->b_go,
                        child_bar->b_go + KMP_BARRIER_STATE_BUMP));
          // Use ngo store (if available) to both store ICVs and release child
          // via child's b_go
          ngo_store_go(&child_bar->th_fixed_icvs, &thr_bar->th_fixed_icvs);
        }
        ngo_sync();
      }
      TCW_8(thr_bar->b_go,
            KMP_INIT_BARRIER_STATE); // Reset my b_go flag for next time
      // Now, release leaf children
      if (thr_bar->leaf_kids) { // if there are any
        // We test team_change on the off-chance that the level 1 team changed.
        if (team_change ||
            old_leaf_kids < thr_bar->leaf_kids) { // some old, some new
          if (old_leaf_kids) { // release old leaf kids
            thr_bar->b_go |= old_leaf_state;
          }
          // Release new leaf kids
          last = tid + thr_bar->skip_per_level[1];
          if (last > nproc)
            last = nproc;
          for (child_tid = tid + 1 + old_leaf_kids; child_tid < (int)last;
               ++child_tid) { // skip_per_level[0]=1
            kmp_info_t *child_thr = team->t.t_threads[child_tid];
            kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
            KA_TRACE(
                20,
                ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) releasing"
                 " T#%d(%d:%d) go(%p): %u => %u\n",
                 gtid, team->t.t_id, tid, __kmp_gtid_from_tid(child_tid, team),
                 team->t.t_id, child_tid, &child_bar->b_go, child_bar->b_go,
                 child_bar->b_go + KMP_BARRIER_STATE_BUMP));
            // Release child using child's b_go flag
            ANNOTATE_BARRIER_BEGIN(child_thr);
            kmp_flag_64 flag(&child_bar->b_go, child_thr);
            flag.release();
          }
        } else { // Release all children at once with leaf_state bits on my own
          // b_go flag
          thr_bar->b_go |= thr_bar->leaf_state;
        }
      }
    } else { // Blocktime is not infinite; do a simple hierarchical release
      for (int d = thr_bar->my_level - 1; d >= 0;
           --d) { // Release highest level threads first
        last = tid + thr_bar->skip_per_level[d + 1];
        kmp_uint32 skip = thr_bar->skip_per_level[d];
        if (last > nproc)
          last = nproc;
        for (child_tid = tid + skip; child_tid < (int)last; child_tid += skip) {
          kmp_info_t *child_thr = team->t.t_threads[child_tid];
          kmp_bstate_t *child_bar = &child_thr->th.th_bar[bt].bb;
          KA_TRACE(20, ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) "
                        "releasing T#%d(%d:%d) go(%p): %u => %u\n",
                        gtid, team->t.t_id, tid,
                        __kmp_gtid_from_tid(child_tid, team), team->t.t_id,
                        child_tid, &child_bar->b_go, child_bar->b_go,
                        child_bar->b_go + KMP_BARRIER_STATE_BUMP));
          // Release child using child's b_go flag
          ANNOTATE_BARRIER_BEGIN(child_thr);
          kmp_flag_64 flag(&child_bar->b_go, child_thr);
          flag.release();
        }
      }
    }
#if KMP_BARRIER_ICV_PUSH
    if (propagate_icvs && !KMP_MASTER_TID(tid))
      // non-leaves copy ICVs from fixed ICVs to local dest
      copy_icvs(&team->t.t_implicit_task_taskdata[tid].td_icvs,
                &thr_bar->th_fixed_icvs);
#endif // KMP_BARRIER_ICV_PUSH
  }
  KA_TRACE(20, ("__kmp_hierarchical_barrier_release: T#%d(%d:%d) exit for "
                "barrier type %d\n",
                gtid, team->t.t_id, tid, bt));
}

// End of Barrier Algorithms

// Internal function to do a barrier.
/* If is_split is true, do a split barrier, otherwise, do a plain barrier
   If reduce is non-NULL, do a split reduction barrier, otherwise, do a split
   barrier
   Returns 0 if master thread, 1 if worker thread.  */
int __kmp_barrier(enum barrier_type bt, int gtid, int is_split,
                  size_t reduce_size, void *reduce_data,
                  void (*reduce)(void *, void *)) {
  KMP_TIME_PARTITIONED_BLOCK(OMP_plain_barrier);
  KMP_SET_THREAD_STATE_BLOCK(PLAIN_BARRIER);
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = this_thr->th.th_team;
  int status = 0;
#if OMPT_SUPPORT && OMPT_OPTIONAL
  ompt_data_t *my_task_data;
  ompt_data_t *my_parallel_data;
  void *return_address;
#endif

  KA_TRACE(15, ("__kmp_barrier: T#%d(%d:%d) has arrived\n", gtid,
                __kmp_team_from_gtid(gtid)->t.t_id, __kmp_tid_from_gtid(gtid)));

  ANNOTATE_BARRIER_BEGIN(&team->t.t_bar);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
#if OMPT_OPTIONAL
    my_task_data = OMPT_CUR_TASK_DATA(this_thr);
    my_parallel_data = OMPT_CUR_TEAM_DATA(this_thr);
    return_address = OMPT_LOAD_RETURN_ADDRESS(gtid);
    if (ompt_enabled.ompt_callback_sync_region) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region)(
          ompt_sync_region_barrier, ompt_scope_begin, my_parallel_data,
          my_task_data, return_address);
    }
    if (ompt_enabled.ompt_callback_sync_region_wait) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait)(
          ompt_sync_region_barrier, ompt_scope_begin, my_parallel_data,
          my_task_data, return_address);
    }
#endif
    // It is OK to report the barrier state after the barrier begin callback.
    // According to the OMPT specification, a compliant implementation may
    // even delay reporting this state until the barrier begins to wait.
    this_thr->th.ompt_thread_info.state = ompt_state_wait_barrier;
  }
#endif

  if (!team->t.t_serialized) {
#if USE_ITT_BUILD
    // This value will be used in itt notify events below.
    void *itt_sync_obj = NULL;
#if USE_ITT_NOTIFY
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bt, 1);
#endif
#endif /* USE_ITT_BUILD */
    if (__kmp_tasking_mode == tskm_extra_barrier) {
      __kmp_tasking_barrier(team, this_thr, gtid);
      KA_TRACE(15,
               ("__kmp_barrier: T#%d(%d:%d) past tasking barrier\n", gtid,
                __kmp_team_from_gtid(gtid)->t.t_id, __kmp_tid_from_gtid(gtid)));
    }

    /* Copy the blocktime info to the thread, where __kmp_wait_template() can
       access it when the team struct is not guaranteed to exist. */
    // See note about the corresponding code in __kmp_join_barrier() being
    // performance-critical.
    if (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME) {
#if KMP_USE_MONITOR
      this_thr->th.th_team_bt_intervals =
          team->t.t_implicit_task_taskdata[tid].td_icvs.bt_intervals;
      this_thr->th.th_team_bt_set =
          team->t.t_implicit_task_taskdata[tid].td_icvs.bt_set;
#else
      this_thr->th.th_team_bt_intervals = KMP_BLOCKTIME_INTERVAL(team, tid);
#endif
    }

#if USE_ITT_BUILD
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
      __kmp_itt_barrier_starting(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */
#if USE_DEBUGGER
    // Let the debugger know: the thread arrived to the barrier and waiting.
    if (KMP_MASTER_TID(tid)) { // Master counter is stored in team structure.
      team->t.t_bar[bt].b_master_arrived += 1;
    } else {
      this_thr->th.th_bar[bt].bb.b_worker_arrived += 1;
    } // if
#endif /* USE_DEBUGGER */
    if (reduce != NULL) {
      // KMP_DEBUG_ASSERT( is_split == TRUE );  // #C69956
      this_thr->th.th_local.reduce_data = reduce_data;
    }

    if (KMP_MASTER_TID(tid) && __kmp_tasking_mode != tskm_immediate_exec)
      __kmp_task_team_setup(
          this_thr, team,
          0); // use 0 to only setup the current team if nthreads > 1

    switch (__kmp_barrier_gather_pattern[bt]) {
    case bp_hyper_bar: {
      KMP_ASSERT(__kmp_barrier_gather_branch_bits[bt]); // don't set branch bits
      // to 0; use linear
      __kmp_hyper_barrier_gather(bt, this_thr, gtid, tid,
                                 reduce USE_ITT_BUILD_ARG(itt_sync_obj));
      break;
    }
    case bp_hierarchical_bar: {
      __kmp_hierarchical_barrier_gather(bt, this_thr, gtid, tid,
                                        reduce USE_ITT_BUILD_ARG(itt_sync_obj));
      break;
    }
    case bp_tree_bar: {
      KMP_ASSERT(__kmp_barrier_gather_branch_bits[bt]); // don't set branch bits
      // to 0; use linear
      __kmp_tree_barrier_gather(bt, this_thr, gtid, tid,
                                reduce USE_ITT_BUILD_ARG(itt_sync_obj));
      break;
    }
    default: {
      __kmp_linear_barrier_gather(bt, this_thr, gtid, tid,
                                  reduce USE_ITT_BUILD_ARG(itt_sync_obj));
    }
    }

    KMP_MB();

    if (KMP_MASTER_TID(tid)) {
      status = 0;
      if (__kmp_tasking_mode != tskm_immediate_exec) {
        __kmp_task_team_wait(this_thr, team USE_ITT_BUILD_ARG(itt_sync_obj));
      }
#if USE_DEBUGGER
      // Let the debugger know: All threads are arrived and starting leaving the
      // barrier.
      team->t.t_bar[bt].b_team_arrived += 1;
#endif

#if OMP_40_ENABLED
      kmp_int32 cancel_request = KMP_ATOMIC_LD_RLX(&team->t.t_cancel_request);
      // Reset cancellation flag for worksharing constructs
      if (cancel_request == cancel_loop || cancel_request == cancel_sections) {
        KMP_ATOMIC_ST_RLX(&team->t.t_cancel_request, cancel_noreq);
      }
#endif
#if USE_ITT_BUILD
      /* TODO: In case of split reduction barrier, master thread may send
         acquired event early, before the final summation into the shared
         variable is done (final summation can be a long operation for array
         reductions).  */
      if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
        __kmp_itt_barrier_middle(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */
#if USE_ITT_BUILD && USE_ITT_NOTIFY
      // Barrier - report frame end (only if active_level == 1)
      if ((__itt_frame_submit_v3_ptr || KMP_ITT_DEBUG) &&
          __kmp_forkjoin_frames_mode &&
#if OMP_40_ENABLED
          this_thr->th.th_teams_microtask == NULL &&
#endif
          team->t.t_active_level == 1) {
        ident_t *loc = __kmp_threads[gtid]->th.th_ident;
        kmp_uint64 cur_time = __itt_get_timestamp();
        kmp_info_t **other_threads = team->t.t_threads;
        int nproc = this_thr->th.th_team_nproc;
        int i;
        switch (__kmp_forkjoin_frames_mode) {
        case 1:
          __kmp_itt_frame_submit(gtid, this_thr->th.th_frame_time, cur_time, 0,
                                 loc, nproc);
          this_thr->th.th_frame_time = cur_time;
          break;
        case 2: // AC 2015-01-19: currently does not work for hierarchical (to
          // be fixed)
          __kmp_itt_frame_submit(gtid, this_thr->th.th_bar_min_time, cur_time,
                                 1, loc, nproc);
          break;
        case 3:
          if (__itt_metadata_add_ptr) {
            // Initialize with master's wait time
            kmp_uint64 delta = cur_time - this_thr->th.th_bar_arrive_time;
            // Set arrive time to zero to be able to check it in
            // __kmp_invoke_task(); the same is done inside the loop below
            this_thr->th.th_bar_arrive_time = 0;
            for (i = 1; i < nproc; ++i) {
              delta += (cur_time - other_threads[i]->th.th_bar_arrive_time);
              other_threads[i]->th.th_bar_arrive_time = 0;
            }
            __kmp_itt_metadata_imbalance(gtid, this_thr->th.th_frame_time,
                                         cur_time, delta,
                                         (kmp_uint64)(reduce != NULL));
          }
          __kmp_itt_frame_submit(gtid, this_thr->th.th_frame_time, cur_time, 0,
                                 loc, nproc);
          this_thr->th.th_frame_time = cur_time;
          break;
        }
      }
#endif /* USE_ITT_BUILD */
    } else {
      status = 1;
#if USE_ITT_BUILD
      if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
        __kmp_itt_barrier_middle(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */
    }
    if (status == 1 || !is_split) {
      switch (__kmp_barrier_release_pattern[bt]) {
      case bp_hyper_bar: {
        KMP_ASSERT(__kmp_barrier_release_branch_bits[bt]);
        __kmp_hyper_barrier_release(bt, this_thr, gtid, tid,
                                    FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
        break;
      }
      case bp_hierarchical_bar: {
        __kmp_hierarchical_barrier_release(
            bt, this_thr, gtid, tid, FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
        break;
      }
      case bp_tree_bar: {
        KMP_ASSERT(__kmp_barrier_release_branch_bits[bt]);
        __kmp_tree_barrier_release(bt, this_thr, gtid, tid,
                                   FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
        break;
      }
      default: {
        __kmp_linear_barrier_release(bt, this_thr, gtid, tid,
                                     FALSE USE_ITT_BUILD_ARG(itt_sync_obj));
      }
      }
      if (__kmp_tasking_mode != tskm_immediate_exec) {
        __kmp_task_team_sync(this_thr, team);
      }
    }

#if USE_ITT_BUILD
    /* GEH: TODO: Move this under if-condition above and also include in
       __kmp_end_split_barrier(). This will more accurately represent the actual
       release time of the threads for split barriers.  */
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
      __kmp_itt_barrier_finished(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */
  } else { // Team is serialized.
    status = 0;
    if (__kmp_tasking_mode != tskm_immediate_exec) {
#if OMP_45_ENABLED
      if (this_thr->th.th_task_team != NULL) {
#if USE_ITT_NOTIFY
        void *itt_sync_obj = NULL;
        if (__itt_sync_create_ptr || KMP_ITT_DEBUG) {
          itt_sync_obj = __kmp_itt_barrier_object(gtid, bt, 1);
          __kmp_itt_barrier_starting(gtid, itt_sync_obj);
        }
#endif

        KMP_DEBUG_ASSERT(this_thr->th.th_task_team->tt.tt_found_proxy_tasks ==
                         TRUE);
        __kmp_task_team_wait(this_thr, team USE_ITT_BUILD_ARG(itt_sync_obj));
        __kmp_task_team_setup(this_thr, team, 0);

#if USE_ITT_BUILD
        if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
          __kmp_itt_barrier_finished(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */
      }
#else
      // The task team should be NULL for serialized code (tasks will be
      // executed immediately)
      KMP_DEBUG_ASSERT(team->t.t_task_team[this_thr->th.th_task_state] == NULL);
      KMP_DEBUG_ASSERT(this_thr->th.th_task_team == NULL);
#endif
    }
  }
  KA_TRACE(15, ("__kmp_barrier: T#%d(%d:%d) is leaving with return value %d\n",
                gtid, __kmp_team_from_gtid(gtid)->t.t_id,
                __kmp_tid_from_gtid(gtid), status));

#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
#if OMPT_OPTIONAL
    if (ompt_enabled.ompt_callback_sync_region_wait) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait)(
          ompt_sync_region_barrier, ompt_scope_end, my_parallel_data,
          my_task_data, return_address);
    }
    if (ompt_enabled.ompt_callback_sync_region) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region)(
          ompt_sync_region_barrier, ompt_scope_end, my_parallel_data,
          my_task_data, return_address);
    }
#endif
    this_thr->th.ompt_thread_info.state = ompt_state_work_parallel;
  }
#endif
  ANNOTATE_BARRIER_END(&team->t.t_bar);

  return status;
}

void __kmp_end_split_barrier(enum barrier_type bt, int gtid) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_end_split_barrier);
  KMP_SET_THREAD_STATE_BLOCK(PLAIN_BARRIER);
  int tid = __kmp_tid_from_gtid(gtid);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = this_thr->th.th_team;

  ANNOTATE_BARRIER_BEGIN(&team->t.t_bar);
  if (!team->t.t_serialized) {
    if (KMP_MASTER_GTID(gtid)) {
      switch (__kmp_barrier_release_pattern[bt]) {
      case bp_hyper_bar: {
        KMP_ASSERT(__kmp_barrier_release_branch_bits[bt]);
        __kmp_hyper_barrier_release(bt, this_thr, gtid, tid,
                                    FALSE USE_ITT_BUILD_ARG(NULL));
        break;
      }
      case bp_hierarchical_bar: {
        __kmp_hierarchical_barrier_release(bt, this_thr, gtid, tid,
                                           FALSE USE_ITT_BUILD_ARG(NULL));
        break;
      }
      case bp_tree_bar: {
        KMP_ASSERT(__kmp_barrier_release_branch_bits[bt]);
        __kmp_tree_barrier_release(bt, this_thr, gtid, tid,
                                   FALSE USE_ITT_BUILD_ARG(NULL));
        break;
      }
      default: {
        __kmp_linear_barrier_release(bt, this_thr, gtid, tid,
                                     FALSE USE_ITT_BUILD_ARG(NULL));
      }
      }
      if (__kmp_tasking_mode != tskm_immediate_exec) {
        __kmp_task_team_sync(this_thr, team);
      } // if
    }
  }
  ANNOTATE_BARRIER_END(&team->t.t_bar);
}

void __kmp_join_barrier(int gtid) {
  KMP_TIME_PARTITIONED_BLOCK(OMP_join_barrier);
  KMP_SET_THREAD_STATE_BLOCK(FORK_JOIN_BARRIER);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team;
  kmp_uint nproc;
  kmp_info_t *master_thread;
  int tid;
#ifdef KMP_DEBUG
  int team_id;
#endif /* KMP_DEBUG */
#if USE_ITT_BUILD
  void *itt_sync_obj = NULL;
#if USE_ITT_NOTIFY
  if (__itt_sync_create_ptr || KMP_ITT_DEBUG) // Don't call routine without need
    // Get object created at fork_barrier
    itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
#endif
#endif /* USE_ITT_BUILD */
  KMP_MB();

  // Get current info
  team = this_thr->th.th_team;
  nproc = this_thr->th.th_team_nproc;
  KMP_DEBUG_ASSERT((int)nproc == team->t.t_nproc);
  tid = __kmp_tid_from_gtid(gtid);
#ifdef KMP_DEBUG
  team_id = team->t.t_id;
#endif /* KMP_DEBUG */
  master_thread = this_thr->th.th_team_master;
#ifdef KMP_DEBUG
  if (master_thread != team->t.t_threads[0]) {
    __kmp_print_structure();
  }
#endif /* KMP_DEBUG */
  KMP_DEBUG_ASSERT(master_thread == team->t.t_threads[0]);
  KMP_MB();

  // Verify state
  KMP_DEBUG_ASSERT(__kmp_threads && __kmp_threads[gtid]);
  KMP_DEBUG_ASSERT(TCR_PTR(this_thr->th.th_team));
  KMP_DEBUG_ASSERT(TCR_PTR(this_thr->th.th_root));
  KMP_DEBUG_ASSERT(this_thr == team->t.t_threads[tid]);
  KA_TRACE(10, ("__kmp_join_barrier: T#%d(%d:%d) arrived at join barrier\n",
                gtid, team_id, tid));

  ANNOTATE_BARRIER_BEGIN(&team->t.t_bar);
#if OMPT_SUPPORT
  if (ompt_enabled.enabled) {
#if OMPT_OPTIONAL
    ompt_data_t *my_task_data;
    ompt_data_t *my_parallel_data;
    void *codeptr = NULL;
    int ds_tid = this_thr->th.th_info.ds.ds_tid;
    if (KMP_MASTER_TID(ds_tid) &&
        (ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait) ||
         ompt_callbacks.ompt_callback(ompt_callback_sync_region)))
      codeptr = team->t.ompt_team_info.master_return_address;
    my_task_data = OMPT_CUR_TASK_DATA(this_thr);
    my_parallel_data = OMPT_CUR_TEAM_DATA(this_thr);
    if (ompt_enabled.ompt_callback_sync_region) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region)(
          ompt_sync_region_barrier, ompt_scope_begin, my_parallel_data,
          my_task_data, codeptr);
    }
    if (ompt_enabled.ompt_callback_sync_region_wait) {
      ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait)(
          ompt_sync_region_barrier, ompt_scope_begin, my_parallel_data,
          my_task_data, codeptr);
    }
    if (!KMP_MASTER_TID(ds_tid))
      this_thr->th.ompt_thread_info.task_data = *OMPT_CUR_TASK_DATA(this_thr);
#endif
    this_thr->th.ompt_thread_info.state = ompt_state_wait_barrier_implicit;
  }
#endif

  if (__kmp_tasking_mode == tskm_extra_barrier) {
    __kmp_tasking_barrier(team, this_thr, gtid);
    KA_TRACE(10, ("__kmp_join_barrier: T#%d(%d:%d) past taking barrier\n", gtid,
                  team_id, tid));
  }
#ifdef KMP_DEBUG
  if (__kmp_tasking_mode != tskm_immediate_exec) {
    KA_TRACE(20, ("__kmp_join_barrier: T#%d, old team = %d, old task_team = "
                  "%p, th_task_team = %p\n",
                  __kmp_gtid_from_thread(this_thr), team_id,
                  team->t.t_task_team[this_thr->th.th_task_state],
                  this_thr->th.th_task_team));
    KMP_DEBUG_ASSERT(this_thr->th.th_task_team ==
                     team->t.t_task_team[this_thr->th.th_task_state]);
  }
#endif /* KMP_DEBUG */

  /* Copy the blocktime info to the thread, where __kmp_wait_template() can
     access it when the team struct is not guaranteed to exist. Doing these
     loads causes a cache miss slows down EPCC parallel by 2x. As a workaround,
     we do not perform the copy if blocktime=infinite, since the values are not
     used by __kmp_wait_template() in that case. */
  if (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME) {
#if KMP_USE_MONITOR
    this_thr->th.th_team_bt_intervals =
        team->t.t_implicit_task_taskdata[tid].td_icvs.bt_intervals;
    this_thr->th.th_team_bt_set =
        team->t.t_implicit_task_taskdata[tid].td_icvs.bt_set;
#else
    this_thr->th.th_team_bt_intervals = KMP_BLOCKTIME_INTERVAL(team, tid);
#endif
  }

#if USE_ITT_BUILD
  if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
    __kmp_itt_barrier_starting(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */

  switch (__kmp_barrier_gather_pattern[bs_forkjoin_barrier]) {
  case bp_hyper_bar: {
    KMP_ASSERT(__kmp_barrier_gather_branch_bits[bs_forkjoin_barrier]);
    __kmp_hyper_barrier_gather(bs_forkjoin_barrier, this_thr, gtid, tid,
                               NULL USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  case bp_hierarchical_bar: {
    __kmp_hierarchical_barrier_gather(bs_forkjoin_barrier, this_thr, gtid, tid,
                                      NULL USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  case bp_tree_bar: {
    KMP_ASSERT(__kmp_barrier_gather_branch_bits[bs_forkjoin_barrier]);
    __kmp_tree_barrier_gather(bs_forkjoin_barrier, this_thr, gtid, tid,
                              NULL USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  default: {
    __kmp_linear_barrier_gather(bs_forkjoin_barrier, this_thr, gtid, tid,
                                NULL USE_ITT_BUILD_ARG(itt_sync_obj));
  }
  }

  /* From this point on, the team data structure may be deallocated at any time
     by the master thread - it is unsafe to reference it in any of the worker
     threads. Any per-team data items that need to be referenced before the
     end of the barrier should be moved to the kmp_task_team_t structs.  */
  if (KMP_MASTER_TID(tid)) {
    if (__kmp_tasking_mode != tskm_immediate_exec) {
      __kmp_task_team_wait(this_thr, team USE_ITT_BUILD_ARG(itt_sync_obj));
    }
#if OMP_50_ENABLED
    if (__kmp_display_affinity) {
      KMP_CHECK_UPDATE(team->t.t_display_affinity, 0);
    }
#endif
#if KMP_STATS_ENABLED
    // Have master thread flag the workers to indicate they are now waiting for
    // next parallel region, Also wake them up so they switch their timers to
    // idle.
    for (int i = 0; i < team->t.t_nproc; ++i) {
      kmp_info_t *team_thread = team->t.t_threads[i];
      if (team_thread == this_thr)
        continue;
      team_thread->th.th_stats->setIdleFlag();
      if (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME &&
          team_thread->th.th_sleep_loc != NULL)
        __kmp_null_resume_wrapper(__kmp_gtid_from_thread(team_thread),
                                  team_thread->th.th_sleep_loc);
    }
#endif
#if USE_ITT_BUILD
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
      __kmp_itt_barrier_middle(gtid, itt_sync_obj);
#endif /* USE_ITT_BUILD */

#if USE_ITT_BUILD && USE_ITT_NOTIFY
    // Join barrier - report frame end
    if ((__itt_frame_submit_v3_ptr || KMP_ITT_DEBUG) &&
        __kmp_forkjoin_frames_mode &&
#if OMP_40_ENABLED
        this_thr->th.th_teams_microtask == NULL &&
#endif
        team->t.t_active_level == 1) {
      kmp_uint64 cur_time = __itt_get_timestamp();
      ident_t *loc = team->t.t_ident;
      kmp_info_t **other_threads = team->t.t_threads;
      int nproc = this_thr->th.th_team_nproc;
      int i;
      switch (__kmp_forkjoin_frames_mode) {
      case 1:
        __kmp_itt_frame_submit(gtid, this_thr->th.th_frame_time, cur_time, 0,
                               loc, nproc);
        break;
      case 2:
        __kmp_itt_frame_submit(gtid, this_thr->th.th_bar_min_time, cur_time, 1,
                               loc, nproc);
        break;
      case 3:
        if (__itt_metadata_add_ptr) {
          // Initialize with master's wait time
          kmp_uint64 delta = cur_time - this_thr->th.th_bar_arrive_time;
          // Set arrive time to zero to be able to check it in
          // __kmp_invoke_task(); the same is done inside the loop below
          this_thr->th.th_bar_arrive_time = 0;
          for (i = 1; i < nproc; ++i) {
            delta += (cur_time - other_threads[i]->th.th_bar_arrive_time);
            other_threads[i]->th.th_bar_arrive_time = 0;
          }
          __kmp_itt_metadata_imbalance(gtid, this_thr->th.th_frame_time,
                                       cur_time, delta, 0);
        }
        __kmp_itt_frame_submit(gtid, this_thr->th.th_frame_time, cur_time, 0,
                               loc, nproc);
        this_thr->th.th_frame_time = cur_time;
        break;
      }
    }
#endif /* USE_ITT_BUILD */
  }
#if USE_ITT_BUILD
  else {
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG)
      __kmp_itt_barrier_middle(gtid, itt_sync_obj);
  }
#endif /* USE_ITT_BUILD */

#if KMP_DEBUG
  if (KMP_MASTER_TID(tid)) {
    KA_TRACE(
        15,
        ("__kmp_join_barrier: T#%d(%d:%d) says all %d team threads arrived\n",
         gtid, team_id, tid, nproc));
  }
#endif /* KMP_DEBUG */

  // TODO now, mark worker threads as done so they may be disbanded
  KMP_MB(); // Flush all pending memory write invalidates.
  KA_TRACE(10,
           ("__kmp_join_barrier: T#%d(%d:%d) leaving\n", gtid, team_id, tid));

  ANNOTATE_BARRIER_END(&team->t.t_bar);
}

// TODO release worker threads' fork barriers as we are ready instead of all at
// once
void __kmp_fork_barrier(int gtid, int tid) {
  KMP_TIME_PARTITIONED_BLOCK(OMP_fork_barrier);
  KMP_SET_THREAD_STATE_BLOCK(FORK_JOIN_BARRIER);
  kmp_info_t *this_thr = __kmp_threads[gtid];
  kmp_team_t *team = (tid == 0) ? this_thr->th.th_team : NULL;
#if USE_ITT_BUILD
  void *itt_sync_obj = NULL;
#endif /* USE_ITT_BUILD */
  if (team)
    ANNOTATE_BARRIER_END(&team->t.t_bar);

  KA_TRACE(10, ("__kmp_fork_barrier: T#%d(%d:%d) has arrived\n", gtid,
                (team != NULL) ? team->t.t_id : -1, tid));

  // th_team pointer only valid for master thread here
  if (KMP_MASTER_TID(tid)) {
#if USE_ITT_BUILD && USE_ITT_NOTIFY
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG) {
      // Create itt barrier object
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier, 1);
      __kmp_itt_barrier_middle(gtid, itt_sync_obj); // Call acquired/releasing
    }
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */

#ifdef KMP_DEBUG
    kmp_info_t **other_threads = team->t.t_threads;
    int i;

    // Verify state
    KMP_MB();

    for (i = 1; i < team->t.t_nproc; ++i) {
      KA_TRACE(500,
               ("__kmp_fork_barrier: T#%d(%d:0) checking T#%d(%d:%d) fork go "
                "== %u.\n",
                gtid, team->t.t_id, other_threads[i]->th.th_info.ds.ds_gtid,
                team->t.t_id, other_threads[i]->th.th_info.ds.ds_tid,
                other_threads[i]->th.th_bar[bs_forkjoin_barrier].bb.b_go));
      KMP_DEBUG_ASSERT(
          (TCR_4(other_threads[i]->th.th_bar[bs_forkjoin_barrier].bb.b_go) &
           ~(KMP_BARRIER_SLEEP_STATE)) == KMP_INIT_BARRIER_STATE);
      KMP_DEBUG_ASSERT(other_threads[i]->th.th_team == team);
    }
#endif

    if (__kmp_tasking_mode != tskm_immediate_exec) {
      // 0 indicates setup current task team if nthreads > 1
      __kmp_task_team_setup(this_thr, team, 0);
    }

    /* The master thread may have changed its blocktime between the join barrier
       and the fork barrier. Copy the blocktime info to the thread, where
       __kmp_wait_template() can access it when the team struct is not
       guaranteed to exist. */
    // See note about the corresponding code in __kmp_join_barrier() being
    // performance-critical
    if (__kmp_dflt_blocktime != KMP_MAX_BLOCKTIME) {
#if KMP_USE_MONITOR
      this_thr->th.th_team_bt_intervals =
          team->t.t_implicit_task_taskdata[tid].td_icvs.bt_intervals;
      this_thr->th.th_team_bt_set =
          team->t.t_implicit_task_taskdata[tid].td_icvs.bt_set;
#else
      this_thr->th.th_team_bt_intervals = KMP_BLOCKTIME_INTERVAL(team, tid);
#endif
    }
  } // master

  switch (__kmp_barrier_release_pattern[bs_forkjoin_barrier]) {
  case bp_hyper_bar: {
    KMP_ASSERT(__kmp_barrier_release_branch_bits[bs_forkjoin_barrier]);
    __kmp_hyper_barrier_release(bs_forkjoin_barrier, this_thr, gtid, tid,
                                TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  case bp_hierarchical_bar: {
    __kmp_hierarchical_barrier_release(bs_forkjoin_barrier, this_thr, gtid, tid,
                                       TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  case bp_tree_bar: {
    KMP_ASSERT(__kmp_barrier_release_branch_bits[bs_forkjoin_barrier]);
    __kmp_tree_barrier_release(bs_forkjoin_barrier, this_thr, gtid, tid,
                               TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
    break;
  }
  default: {
    __kmp_linear_barrier_release(bs_forkjoin_barrier, this_thr, gtid, tid,
                                 TRUE USE_ITT_BUILD_ARG(itt_sync_obj));
  }
  }

#if OMPT_SUPPORT
  if (ompt_enabled.enabled &&
      this_thr->th.ompt_thread_info.state == ompt_state_wait_barrier_implicit) {
    int ds_tid = this_thr->th.th_info.ds.ds_tid;
    ompt_data_t *task_data = (team)
                                 ? OMPT_CUR_TASK_DATA(this_thr)
                                 : &(this_thr->th.ompt_thread_info.task_data);
    this_thr->th.ompt_thread_info.state = ompt_state_overhead;
#if OMPT_OPTIONAL
    void *codeptr = NULL;
    if (KMP_MASTER_TID(ds_tid) &&
        (ompt_callbacks.ompt_callback(ompt_callback_sync_region_wait) ||
         ompt_callbacks.ompt_callback(ompt_callback_sync_region)))
      codeptr = team->t.ompt_team_info.master_return_address;
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

  // Early exit for reaping threads releasing forkjoin barrier
  if (TCR_4(__kmp_global.g.g_done)) {
    this_thr->th.th_task_team = NULL;

#if USE_ITT_BUILD && USE_ITT_NOTIFY
    if (__itt_sync_create_ptr || KMP_ITT_DEBUG) {
      if (!KMP_MASTER_TID(tid)) {
        itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
        if (itt_sync_obj)
          __kmp_itt_barrier_finished(gtid, itt_sync_obj);
      }
    }
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
    KA_TRACE(10, ("__kmp_fork_barrier: T#%d is leaving early\n", gtid));
    return;
  }

  /* We can now assume that a valid team structure has been allocated by the
     master and propagated to all worker threads. The current thread, however,
     may not be part of the team, so we can't blindly assume that the team
     pointer is non-null.  */
  team = (kmp_team_t *)TCR_PTR(this_thr->th.th_team);
  KMP_DEBUG_ASSERT(team != NULL);
  tid = __kmp_tid_from_gtid(gtid);

#if KMP_BARRIER_ICV_PULL
  /* Master thread's copy of the ICVs was set up on the implicit taskdata in
     __kmp_reinitialize_team. __kmp_fork_call() assumes the master thread's
     implicit task has this data before this function is called. We cannot
     modify __kmp_fork_call() to look at the fixed ICVs in the master's thread
     struct, because it is not always the case that the threads arrays have
     been allocated when __kmp_fork_call() is executed. */
  {
    KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(USER_icv_copy);
    if (!KMP_MASTER_TID(tid)) { // master thread already has ICVs
      // Copy the initial ICVs from the master's thread struct to the implicit
      // task for this tid.
      KA_TRACE(10,
               ("__kmp_fork_barrier: T#%d(%d) is PULLing ICVs\n", gtid, tid));
      __kmp_init_implicit_task(team->t.t_ident, team->t.t_threads[tid], team,
                               tid, FALSE);
      copy_icvs(&team->t.t_implicit_task_taskdata[tid].td_icvs,
                &team->t.t_threads[0]
                     ->th.th_bar[bs_forkjoin_barrier]
                     .bb.th_fixed_icvs);
    }
  }
#endif // KMP_BARRIER_ICV_PULL

  if (__kmp_tasking_mode != tskm_immediate_exec) {
    __kmp_task_team_sync(this_thr, team);
  }

#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
  kmp_proc_bind_t proc_bind = team->t.t_proc_bind;
  if (proc_bind == proc_bind_intel) {
#endif
#if KMP_AFFINITY_SUPPORTED
    // Call dynamic affinity settings
    if (__kmp_affinity_type == affinity_balanced && team->t.t_size_changed) {
      __kmp_balanced_affinity(this_thr, team->t.t_nproc);
    }
#endif // KMP_AFFINITY_SUPPORTED
#if OMP_40_ENABLED && KMP_AFFINITY_SUPPORTED
  } else if (proc_bind != proc_bind_false) {
    if (this_thr->th.th_new_place == this_thr->th.th_current_place) {
      KA_TRACE(100, ("__kmp_fork_barrier: T#%d already in correct place %d\n",
                     __kmp_gtid_from_thread(this_thr),
                     this_thr->th.th_current_place));
    } else {
      __kmp_affinity_set_place(gtid);
    }
  }
#endif
#if OMP_50_ENABLED
  // Perform the display affinity functionality
  if (__kmp_display_affinity) {
    if (team->t.t_display_affinity
#if KMP_AFFINITY_SUPPORTED
        || (__kmp_affinity_type == affinity_balanced && team->t.t_size_changed)
#endif
            ) {
      // NULL means use the affinity-format-var ICV
      __kmp_aux_display_affinity(gtid, NULL);
      this_thr->th.th_prev_num_threads = team->t.t_nproc;
      this_thr->th.th_prev_level = team->t.t_level;
    }
  }
  if (!KMP_MASTER_TID(tid))
    KMP_CHECK_UPDATE(this_thr->th.th_def_allocator, team->t.t_def_allocator);
#endif

#if USE_ITT_BUILD && USE_ITT_NOTIFY
  if (__itt_sync_create_ptr || KMP_ITT_DEBUG) {
    if (!KMP_MASTER_TID(tid)) {
      // Get correct barrier object
      itt_sync_obj = __kmp_itt_barrier_object(gtid, bs_forkjoin_barrier);
      __kmp_itt_barrier_finished(gtid, itt_sync_obj); // Workers call acquired
    } // (prepare called inside barrier_release)
  }
#endif /* USE_ITT_BUILD && USE_ITT_NOTIFY */
  ANNOTATE_BARRIER_END(&team->t.t_bar);
  KA_TRACE(10, ("__kmp_fork_barrier: T#%d(%d:%d) is leaving\n", gtid,
                team->t.t_id, tid));
}

void __kmp_setup_icv_copy(kmp_team_t *team, int new_nproc,
                          kmp_internal_control_t *new_icvs, ident_t *loc) {
  KMP_TIME_DEVELOPER_PARTITIONED_BLOCK(KMP_setup_icv_copy);

  KMP_DEBUG_ASSERT(team && new_nproc && new_icvs);
  KMP_DEBUG_ASSERT((!TCR_4(__kmp_init_parallel)) || new_icvs->nproc);

/* Master thread's copy of the ICVs was set up on the implicit taskdata in
   __kmp_reinitialize_team. __kmp_fork_call() assumes the master thread's
   implicit task has this data before this function is called. */
#if KMP_BARRIER_ICV_PULL
  /* Copy ICVs to master's thread structure into th_fixed_icvs (which remains
     untouched), where all of the worker threads can access them and make their
     own copies after the barrier. */
  KMP_DEBUG_ASSERT(team->t.t_threads[0]); // The threads arrays should be
  // allocated at this point
  copy_icvs(
      &team->t.t_threads[0]->th.th_bar[bs_forkjoin_barrier].bb.th_fixed_icvs,
      new_icvs);
  KF_TRACE(10, ("__kmp_setup_icv_copy: PULL: T#%d this_thread=%p team=%p\n", 0,
                team->t.t_threads[0], team));
#elif KMP_BARRIER_ICV_PUSH
  // The ICVs will be propagated in the fork barrier, so nothing needs to be
  // done here.
  KF_TRACE(10, ("__kmp_setup_icv_copy: PUSH: T#%d this_thread=%p team=%p\n", 0,
                team->t.t_threads[0], team));
#else
  // Copy the ICVs to each of the non-master threads.  This takes O(nthreads)
  // time.
  ngo_load(new_icvs);
  KMP_DEBUG_ASSERT(team->t.t_threads[0]); // The threads arrays should be
  // allocated at this point
  for (int f = 1; f < new_nproc; ++f) { // Skip the master thread
    // TODO: GEH - pass in better source location info since usually NULL here
    KF_TRACE(10, ("__kmp_setup_icv_copy: LINEAR: T#%d this_thread=%p team=%p\n",
                  f, team->t.t_threads[f], team));
    __kmp_init_implicit_task(loc, team->t.t_threads[f], team, f, FALSE);
    ngo_store_icvs(&team->t.t_implicit_task_taskdata[f].td_icvs, new_icvs);
    KF_TRACE(10, ("__kmp_setup_icv_copy: LINEAR: T#%d this_thread=%p team=%p\n",
                  f, team->t.t_threads[f], team));
  }
  ngo_sync();
#endif // KMP_BARRIER_ICV_PULL
}
