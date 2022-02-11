/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BLK_CGROUP_PRIVATE_H
#define _BLK_CGROUP_PRIVATE_H
/*
 * block cgroup private header
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */

#include <linux/blk-cgroup.h>

/* percpu_counter batch for blkg_[rw]stats, per-cpu drift doesn't matter */
#define BLKG_STAT_CPU_BATCH	(INT_MAX / 2)

#ifdef CONFIG_BLK_CGROUP

/*
 * A blkcg_gq (blkg) is association between a block cgroup (blkcg) and a
 * request_queue (q).  This is used by blkcg policies which need to track
 * information per blkcg - q pair.
 *
 * There can be multiple active blkcg policies and each blkg:policy pair is
 * represented by a blkg_policy_data which is allocated and freed by each
 * policy's pd_alloc/free_fn() methods.  A policy can allocate private data
 * area by allocating larger data structure which embeds blkg_policy_data
 * at the beginning.
 */
struct blkg_policy_data {
	/* the blkg and policy id this per-policy data belongs to */
	struct blkcg_gq			*blkg;
	int				plid;
};

/*
 * Policies that need to keep per-blkcg data which is independent from any
 * request_queue associated to it should implement cpd_alloc/free_fn()
 * methods.  A policy can allocate private data area by allocating larger
 * data structure which embeds blkcg_policy_data at the beginning.
 * cpd_init() is invoked to let each policy handle per-blkcg data.
 */
struct blkcg_policy_data {
	/* the blkcg and policy id this per-policy data belongs to */
	struct blkcg			*blkcg;
	int				plid;
};

typedef struct blkcg_policy_data *(blkcg_pol_alloc_cpd_fn)(gfp_t gfp);
typedef void (blkcg_pol_init_cpd_fn)(struct blkcg_policy_data *cpd);
typedef void (blkcg_pol_free_cpd_fn)(struct blkcg_policy_data *cpd);
typedef void (blkcg_pol_bind_cpd_fn)(struct blkcg_policy_data *cpd);
typedef struct blkg_policy_data *(blkcg_pol_alloc_pd_fn)(gfp_t gfp,
				struct request_queue *q, struct blkcg *blkcg);
typedef void (blkcg_pol_init_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_online_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_offline_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_free_pd_fn)(struct blkg_policy_data *pd);
typedef void (blkcg_pol_reset_pd_stats_fn)(struct blkg_policy_data *pd);
typedef bool (blkcg_pol_stat_pd_fn)(struct blkg_policy_data *pd,
				struct seq_file *s);

struct blkcg_policy {
	int				plid;
	/* cgroup files for the policy */
	struct cftype			*dfl_cftypes;
	struct cftype			*legacy_cftypes;

	/* operations */
	blkcg_pol_alloc_cpd_fn		*cpd_alloc_fn;
	blkcg_pol_init_cpd_fn		*cpd_init_fn;
	blkcg_pol_free_cpd_fn		*cpd_free_fn;
	blkcg_pol_bind_cpd_fn		*cpd_bind_fn;

	blkcg_pol_alloc_pd_fn		*pd_alloc_fn;
	blkcg_pol_init_pd_fn		*pd_init_fn;
	blkcg_pol_online_pd_fn		*pd_online_fn;
	blkcg_pol_offline_pd_fn		*pd_offline_fn;
	blkcg_pol_free_pd_fn		*pd_free_fn;
	blkcg_pol_reset_pd_stats_fn	*pd_reset_stats_fn;
	blkcg_pol_stat_pd_fn		*pd_stat_fn;
};

extern struct blkcg blkcg_root;
extern bool blkcg_debug_stats;

struct blkcg_gq *blkg_lookup_slowpath(struct blkcg *blkcg,
				      struct request_queue *q, bool update_hint);
int blkcg_init_queue(struct request_queue *q);
void blkcg_exit_queue(struct request_queue *q);

/* Blkio controller policy registration */
int blkcg_policy_register(struct blkcg_policy *pol);
void blkcg_policy_unregister(struct blkcg_policy *pol);
int blkcg_activate_policy(struct request_queue *q,
			  const struct blkcg_policy *pol);
void blkcg_deactivate_policy(struct request_queue *q,
			     const struct blkcg_policy *pol);

const char *blkg_dev_name(struct blkcg_gq *blkg);
void blkcg_print_blkgs(struct seq_file *sf, struct blkcg *blkcg,
		       u64 (*prfill)(struct seq_file *,
				     struct blkg_policy_data *, int),
		       const struct blkcg_policy *pol, int data,
		       bool show_total);
u64 __blkg_prfill_u64(struct seq_file *sf, struct blkg_policy_data *pd, u64 v);

struct blkg_conf_ctx {
	struct block_device		*bdev;
	struct blkcg_gq			*blkg;
	char				*body;
};

struct block_device *blkcg_conf_open_bdev(char **inputp);
int blkg_conf_prep(struct blkcg *blkcg, const struct blkcg_policy *pol,
		   char *input, struct blkg_conf_ctx *ctx);
void blkg_conf_finish(struct blkg_conf_ctx *ctx);

/**
 * blkcg_css - find the current css
 *
 * Find the css associated with either the kthread or the current task.
 * This may return a dying css, so it is up to the caller to use tryget logic
 * to confirm it is alive and well.
 */
static inline struct cgroup_subsys_state *blkcg_css(void)
{
	struct cgroup_subsys_state *css;

	css = kthread_blkcg();
	if (css)
		return css;
	return task_css(current, io_cgrp_id);
}

/**
 * __bio_blkcg - internal, inconsistent version to get blkcg
 *
 * DO NOT USE.
 * This function is inconsistent and consequently is dangerous to use.  The
 * first part of the function returns a blkcg where a reference is owned by the
 * bio.  This means it does not need to be rcu protected as it cannot go away
 * with the bio owning a reference to it.  However, the latter potentially gets
 * it from task_css().  This can race against task migration and the cgroup
 * dying.  It is also semantically different as it must be called rcu protected
 * and is susceptible to failure when trying to get a reference to it.
 * Therefore, it is not ok to assume that *_get() will always succeed on the
 * blkcg returned here.
 */
static inline struct blkcg *__bio_blkcg(struct bio *bio)
{
	if (bio && bio->bi_blkg)
		return bio->bi_blkg->blkcg;
	return css_to_blkcg(blkcg_css());
}

/**
 * bio_issue_as_root_blkg - see if this bio needs to be issued as root blkg
 * @return: true if this bio needs to be submitted with the root blkg context.
 *
 * In order to avoid priority inversions we sometimes need to issue a bio as if
 * it were attached to the root blkg, and then backcharge to the actual owning
 * blkg.  The idea is we do bio_blkcg() to look up the actual context for the
 * bio and attach the appropriate blkg to the bio.  Then we call this helper and
 * if it is true run with the root blkg for that queue and then do any
 * backcharging to the originating cgroup once the io is complete.
 */
static inline bool bio_issue_as_root_blkg(struct bio *bio)
{
	return (bio->bi_opf & (REQ_META | REQ_SWAP)) != 0;
}

/**
 * __blkg_lookup - internal version of blkg_lookup()
 * @blkcg: blkcg of interest
 * @q: request_queue of interest
 * @update_hint: whether to update lookup hint with the result or not
 *
 * This is internal version and shouldn't be used by policy
 * implementations.  Looks up blkgs for the @blkcg - @q pair regardless of
 * @q's bypass state.  If @update_hint is %true, the caller should be
 * holding @q->queue_lock and lookup hint is updated on success.
 */
static inline struct blkcg_gq *__blkg_lookup(struct blkcg *blkcg,
					     struct request_queue *q,
					     bool update_hint)
{
	struct blkcg_gq *blkg;

	if (blkcg == &blkcg_root)
		return q->root_blkg;

	blkg = rcu_dereference(blkcg->blkg_hint);
	if (blkg && blkg->q == q)
		return blkg;

	return blkg_lookup_slowpath(blkcg, q, update_hint);
}

/**
 * blkg_lookup - lookup blkg for the specified blkcg - q pair
 * @blkcg: blkcg of interest
 * @q: request_queue of interest
 *
 * Lookup blkg for the @blkcg - @q pair.  This function should be called
 * under RCU read lock.
 */
static inline struct blkcg_gq *blkg_lookup(struct blkcg *blkcg,
					   struct request_queue *q)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return __blkg_lookup(blkcg, q, false);
}

/**
 * blk_queue_root_blkg - return blkg for the (blkcg_root, @q) pair
 * @q: request_queue of interest
 *
 * Lookup blkg for @q at the root level. See also blkg_lookup().
 */
static inline struct blkcg_gq *blk_queue_root_blkg(struct request_queue *q)
{
	return q->root_blkg;
}

/**
 * blkg_to_pdata - get policy private data
 * @blkg: blkg of interest
 * @pol: policy of interest
 *
 * Return pointer to private data associated with the @blkg-@pol pair.
 */
static inline struct blkg_policy_data *blkg_to_pd(struct blkcg_gq *blkg,
						  struct blkcg_policy *pol)
{
	return blkg ? blkg->pd[pol->plid] : NULL;
}

static inline struct blkcg_policy_data *blkcg_to_cpd(struct blkcg *blkcg,
						     struct blkcg_policy *pol)
{
	return blkcg ? blkcg->cpd[pol->plid] : NULL;
}

/**
 * pdata_to_blkg - get blkg associated with policy private data
 * @pd: policy private data of interest
 *
 * @pd is policy private data.  Determine the blkg it's associated with.
 */
static inline struct blkcg_gq *pd_to_blkg(struct blkg_policy_data *pd)
{
	return pd ? pd->blkg : NULL;
}

static inline struct blkcg *cpd_to_blkcg(struct blkcg_policy_data *cpd)
{
	return cpd ? cpd->blkcg : NULL;
}

/**
 * blkg_path - format cgroup path of blkg
 * @blkg: blkg of interest
 * @buf: target buffer
 * @buflen: target buffer length
 *
 * Format the path of the cgroup of @blkg into @buf.
 */
static inline int blkg_path(struct blkcg_gq *blkg, char *buf, int buflen)
{
	return cgroup_path(blkg->blkcg->css.cgroup, buf, buflen);
}

/**
 * blkg_get - get a blkg reference
 * @blkg: blkg to get
 *
 * The caller should be holding an existing reference.
 */
static inline void blkg_get(struct blkcg_gq *blkg)
{
	percpu_ref_get(&blkg->refcnt);
}

/**
 * blkg_tryget - try and get a blkg reference
 * @blkg: blkg to get
 *
 * This is for use when doing an RCU lookup of the blkg.  We may be in the midst
 * of freeing this blkg, so we can only use it if the refcnt is not zero.
 */
static inline bool blkg_tryget(struct blkcg_gq *blkg)
{
	return blkg && percpu_ref_tryget(&blkg->refcnt);
}

/**
 * blkg_put - put a blkg reference
 * @blkg: blkg to put
 */
static inline void blkg_put(struct blkcg_gq *blkg)
{
	percpu_ref_put(&blkg->refcnt);
}

/**
 * blkg_for_each_descendant_pre - pre-order walk of a blkg's descendants
 * @d_blkg: loop cursor pointing to the current descendant
 * @pos_css: used for iteration
 * @p_blkg: target blkg to walk descendants of
 *
 * Walk @c_blkg through the descendants of @p_blkg.  Must be used with RCU
 * read locked.  If called under either blkcg or queue lock, the iteration
 * is guaranteed to include all and only online blkgs.  The caller may
 * update @pos_css by calling css_rightmost_descendant() to skip subtree.
 * @p_blkg is included in the iteration and the first node to be visited.
 */
#define blkg_for_each_descendant_pre(d_blkg, pos_css, p_blkg)		\
	css_for_each_descendant_pre((pos_css), &(p_blkg)->blkcg->css)	\
		if (((d_blkg) = __blkg_lookup(css_to_blkcg(pos_css),	\
					      (p_blkg)->q, false)))

/**
 * blkg_for_each_descendant_post - post-order walk of a blkg's descendants
 * @d_blkg: loop cursor pointing to the current descendant
 * @pos_css: used for iteration
 * @p_blkg: target blkg to walk descendants of
 *
 * Similar to blkg_for_each_descendant_pre() but performs post-order
 * traversal instead.  Synchronization rules are the same.  @p_blkg is
 * included in the iteration and the last node to be visited.
 */
#define blkg_for_each_descendant_post(d_blkg, pos_css, p_blkg)		\
	css_for_each_descendant_post((pos_css), &(p_blkg)->blkcg->css)	\
		if (((d_blkg) = __blkg_lookup(css_to_blkcg(pos_css),	\
					      (p_blkg)->q, false)))

bool __blkcg_punt_bio_submit(struct bio *bio);

static inline bool blkcg_punt_bio_submit(struct bio *bio)
{
	if (bio->bi_opf & REQ_CGROUP_PUNT)
		return __blkcg_punt_bio_submit(bio);
	else
		return false;
}

static inline void blkcg_bio_issue_init(struct bio *bio)
{
	bio_issue_init(&bio->bi_issue, bio_sectors(bio));
}

static inline void blkcg_use_delay(struct blkcg_gq *blkg)
{
	if (WARN_ON_ONCE(atomic_read(&blkg->use_delay) < 0))
		return;
	if (atomic_add_return(1, &blkg->use_delay) == 1)
		atomic_inc(&blkg->blkcg->css.cgroup->congestion_count);
}

static inline int blkcg_unuse_delay(struct blkcg_gq *blkg)
{
	int old = atomic_read(&blkg->use_delay);

	if (WARN_ON_ONCE(old < 0))
		return 0;
	if (old == 0)
		return 0;

	/*
	 * We do this song and dance because we can race with somebody else
	 * adding or removing delay.  If we just did an atomic_dec we'd end up
	 * negative and we'd already be in trouble.  We need to subtract 1 and
	 * then check to see if we were the last delay so we can drop the
	 * congestion count on the cgroup.
	 */
	while (old) {
		int cur = atomic_cmpxchg(&blkg->use_delay, old, old - 1);
		if (cur == old)
			break;
		old = cur;
	}

	if (old == 0)
		return 0;
	if (old == 1)
		atomic_dec(&blkg->blkcg->css.cgroup->congestion_count);
	return 1;
}

/**
 * blkcg_set_delay - Enable allocator delay mechanism with the specified delay amount
 * @blkg: target blkg
 * @delay: delay duration in nsecs
 *
 * When enabled with this function, the delay is not decayed and must be
 * explicitly cleared with blkcg_clear_delay(). Must not be mixed with
 * blkcg_[un]use_delay() and blkcg_add_delay() usages.
 */
static inline void blkcg_set_delay(struct blkcg_gq *blkg, u64 delay)
{
	int old = atomic_read(&blkg->use_delay);

	/* We only want 1 person setting the congestion count for this blkg. */
	if (!old && atomic_cmpxchg(&blkg->use_delay, old, -1) == old)
		atomic_inc(&blkg->blkcg->css.cgroup->congestion_count);

	atomic64_set(&blkg->delay_nsec, delay);
}

/**
 * blkcg_clear_delay - Disable allocator delay mechanism
 * @blkg: target blkg
 *
 * Disable use_delay mechanism. See blkcg_set_delay().
 */
static inline void blkcg_clear_delay(struct blkcg_gq *blkg)
{
	int old = atomic_read(&blkg->use_delay);

	/* We only want 1 person clearing the congestion count for this blkg. */
	if (old && atomic_cmpxchg(&blkg->use_delay, old, 0) == old)
		atomic_dec(&blkg->blkcg->css.cgroup->congestion_count);
}

void blk_cgroup_bio_start(struct bio *bio);
void blkcg_add_delay(struct blkcg_gq *blkg, u64 now, u64 delta);
#else	/* CONFIG_BLK_CGROUP */

struct blkg_policy_data {
};

struct blkcg_policy_data {
};

struct blkcg_policy {
};

#ifdef CONFIG_BLOCK

static inline struct blkcg_gq *blkg_lookup(struct blkcg *blkcg, void *key) { return NULL; }
static inline struct blkcg_gq *blk_queue_root_blkg(struct request_queue *q)
{ return NULL; }
static inline int blkcg_init_queue(struct request_queue *q) { return 0; }
static inline void blkcg_exit_queue(struct request_queue *q) { }
static inline int blkcg_policy_register(struct blkcg_policy *pol) { return 0; }
static inline void blkcg_policy_unregister(struct blkcg_policy *pol) { }
static inline int blkcg_activate_policy(struct request_queue *q,
					const struct blkcg_policy *pol) { return 0; }
static inline void blkcg_deactivate_policy(struct request_queue *q,
					   const struct blkcg_policy *pol) { }

static inline struct blkcg *__bio_blkcg(struct bio *bio) { return NULL; }

static inline struct blkg_policy_data *blkg_to_pd(struct blkcg_gq *blkg,
						  struct blkcg_policy *pol) { return NULL; }
static inline struct blkcg_gq *pd_to_blkg(struct blkg_policy_data *pd) { return NULL; }
static inline char *blkg_path(struct blkcg_gq *blkg) { return NULL; }
static inline void blkg_get(struct blkcg_gq *blkg) { }
static inline void blkg_put(struct blkcg_gq *blkg) { }

static inline bool blkcg_punt_bio_submit(struct bio *bio) { return false; }
static inline void blkcg_bio_issue_init(struct bio *bio) { }
static inline void blk_cgroup_bio_start(struct bio *bio) { }

#define blk_queue_for_each_rl(rl, q)	\
	for ((rl) = &(q)->root_rl; (rl); (rl) = NULL)

#endif	/* CONFIG_BLOCK */
#endif	/* CONFIG_BLK_CGROUP */

#endif /* _BLK_CGROUP_PRIVATE_H */
