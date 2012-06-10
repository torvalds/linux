#ifndef _BLK_CGROUP_H
#define _BLK_CGROUP_H
/*
 * Common Block IO controller cgroup interface
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

#include <linux/cgroup.h>
#include <linux/u64_stats_sync.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>

/* Max limits for throttle policy */
#define THROTL_IOPS_MAX		UINT_MAX

/* CFQ specific, out here for blkcg->cfq_weight */
#define CFQ_WEIGHT_MIN		10
#define CFQ_WEIGHT_MAX		1000
#define CFQ_WEIGHT_DEFAULT	500

#ifdef CONFIG_BLK_CGROUP

enum blkg_rwstat_type {
	BLKG_RWSTAT_READ,
	BLKG_RWSTAT_WRITE,
	BLKG_RWSTAT_SYNC,
	BLKG_RWSTAT_ASYNC,

	BLKG_RWSTAT_NR,
	BLKG_RWSTAT_TOTAL = BLKG_RWSTAT_NR,
};

struct blkcg_gq;

struct blkcg {
	struct cgroup_subsys_state	css;
	spinlock_t			lock;

	struct radix_tree_root		blkg_tree;
	struct blkcg_gq			*blkg_hint;
	struct hlist_head		blkg_list;

	/* for policies to test whether associated blkcg has changed */
	uint64_t			id;

	/* TODO: per-policy storage in blkcg */
	unsigned int			cfq_weight;	/* belongs to cfq */
};

struct blkg_stat {
	struct u64_stats_sync		syncp;
	uint64_t			cnt;
};

struct blkg_rwstat {
	struct u64_stats_sync		syncp;
	uint64_t			cnt[BLKG_RWSTAT_NR];
};

/*
 * A blkcg_gq (blkg) is association between a block cgroup (blkcg) and a
 * request_queue (q).  This is used by blkcg policies which need to track
 * information per blkcg - q pair.
 *
 * There can be multiple active blkcg policies and each has its private
 * data on each blkg, the size of which is determined by
 * blkcg_policy->pd_size.  blkcg core allocates and frees such areas
 * together with blkg and invokes pd_init/exit_fn() methods.
 *
 * Such private data must embed struct blkg_policy_data (pd) at the
 * beginning and pd_size can't be smaller than pd.
 */
struct blkg_policy_data {
	/* the blkg this per-policy data belongs to */
	struct blkcg_gq			*blkg;

	/* used during policy activation */
	struct list_head		alloc_node;
};

/* association between a blk cgroup and a request queue */
struct blkcg_gq {
	/* Pointer to the associated request_queue */
	struct request_queue		*q;
	struct list_head		q_node;
	struct hlist_node		blkcg_node;
	struct blkcg			*blkcg;
	/* reference count */
	int				refcnt;

	struct blkg_policy_data		*pd[BLKCG_MAX_POLS];

	struct rcu_head			rcu_head;
};

typedef void (blkcg_pol_init_pd_fn)(struct blkcg_gq *blkg);
typedef void (blkcg_pol_exit_pd_fn)(struct blkcg_gq *blkg);
typedef void (blkcg_pol_reset_pd_stats_fn)(struct blkcg_gq *blkg);

struct blkcg_policy {
	int				plid;
	/* policy specific private data size */
	size_t				pd_size;
	/* cgroup files for the policy */
	struct cftype			*cftypes;

	/* operations */
	blkcg_pol_init_pd_fn		*pd_init_fn;
	blkcg_pol_exit_pd_fn		*pd_exit_fn;
	blkcg_pol_reset_pd_stats_fn	*pd_reset_stats_fn;
};

extern struct blkcg blkcg_root;

struct blkcg *cgroup_to_blkcg(struct cgroup *cgroup);
struct blkcg *bio_blkcg(struct bio *bio);
struct blkcg_gq *blkg_lookup(struct blkcg *blkcg, struct request_queue *q);
struct blkcg_gq *blkg_lookup_create(struct blkcg *blkcg,
				    struct request_queue *q);
int blkcg_init_queue(struct request_queue *q);
void blkcg_drain_queue(struct request_queue *q);
void blkcg_exit_queue(struct request_queue *q);

/* Blkio controller policy registration */
int blkcg_policy_register(struct blkcg_policy *pol);
void blkcg_policy_unregister(struct blkcg_policy *pol);
int blkcg_activate_policy(struct request_queue *q,
			  const struct blkcg_policy *pol);
void blkcg_deactivate_policy(struct request_queue *q,
			     const struct blkcg_policy *pol);

void blkcg_print_blkgs(struct seq_file *sf, struct blkcg *blkcg,
		       u64 (*prfill)(struct seq_file *,
				     struct blkg_policy_data *, int),
		       const struct blkcg_policy *pol, int data,
		       bool show_total);
u64 __blkg_prfill_u64(struct seq_file *sf, struct blkg_policy_data *pd, u64 v);
u64 __blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
			 const struct blkg_rwstat *rwstat);
u64 blkg_prfill_stat(struct seq_file *sf, struct blkg_policy_data *pd, int off);
u64 blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
		       int off);

struct blkg_conf_ctx {
	struct gendisk			*disk;
	struct blkcg_gq			*blkg;
	u64				v;
};

int blkg_conf_prep(struct blkcg *blkcg, const struct blkcg_policy *pol,
		   const char *input, struct blkg_conf_ctx *ctx);
void blkg_conf_finish(struct blkg_conf_ctx *ctx);


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
	int ret;

	rcu_read_lock();
	ret = cgroup_path(blkg->blkcg->css.cgroup, buf, buflen);
	rcu_read_unlock();
	if (ret)
		strncpy(buf, "<unavailable>", buflen);
	return ret;
}

/**
 * blkg_get - get a blkg reference
 * @blkg: blkg to get
 *
 * The caller should be holding queue_lock and an existing reference.
 */
static inline void blkg_get(struct blkcg_gq *blkg)
{
	lockdep_assert_held(blkg->q->queue_lock);
	WARN_ON_ONCE(!blkg->refcnt);
	blkg->refcnt++;
}

void __blkg_release(struct blkcg_gq *blkg);

/**
 * blkg_put - put a blkg reference
 * @blkg: blkg to put
 *
 * The caller should be holding queue_lock.
 */
static inline void blkg_put(struct blkcg_gq *blkg)
{
	lockdep_assert_held(blkg->q->queue_lock);
	WARN_ON_ONCE(blkg->refcnt <= 0);
	if (!--blkg->refcnt)
		__blkg_release(blkg);
}

/**
 * blkg_stat_add - add a value to a blkg_stat
 * @stat: target blkg_stat
 * @val: value to add
 *
 * Add @val to @stat.  The caller is responsible for synchronizing calls to
 * this function.
 */
static inline void blkg_stat_add(struct blkg_stat *stat, uint64_t val)
{
	u64_stats_update_begin(&stat->syncp);
	stat->cnt += val;
	u64_stats_update_end(&stat->syncp);
}

/**
 * blkg_stat_read - read the current value of a blkg_stat
 * @stat: blkg_stat to read
 *
 * Read the current value of @stat.  This function can be called without
 * synchroniztion and takes care of u64 atomicity.
 */
static inline uint64_t blkg_stat_read(struct blkg_stat *stat)
{
	unsigned int start;
	uint64_t v;

	do {
		start = u64_stats_fetch_begin(&stat->syncp);
		v = stat->cnt;
	} while (u64_stats_fetch_retry(&stat->syncp, start));

	return v;
}

/**
 * blkg_stat_reset - reset a blkg_stat
 * @stat: blkg_stat to reset
 */
static inline void blkg_stat_reset(struct blkg_stat *stat)
{
	stat->cnt = 0;
}

/**
 * blkg_rwstat_add - add a value to a blkg_rwstat
 * @rwstat: target blkg_rwstat
 * @rw: mask of REQ_{WRITE|SYNC}
 * @val: value to add
 *
 * Add @val to @rwstat.  The counters are chosen according to @rw.  The
 * caller is responsible for synchronizing calls to this function.
 */
static inline void blkg_rwstat_add(struct blkg_rwstat *rwstat,
				   int rw, uint64_t val)
{
	u64_stats_update_begin(&rwstat->syncp);

	if (rw & REQ_WRITE)
		rwstat->cnt[BLKG_RWSTAT_WRITE] += val;
	else
		rwstat->cnt[BLKG_RWSTAT_READ] += val;
	if (rw & REQ_SYNC)
		rwstat->cnt[BLKG_RWSTAT_SYNC] += val;
	else
		rwstat->cnt[BLKG_RWSTAT_ASYNC] += val;

	u64_stats_update_end(&rwstat->syncp);
}

/**
 * blkg_rwstat_read - read the current values of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Read the current snapshot of @rwstat and return it as the return value.
 * This function can be called without synchronization and takes care of
 * u64 atomicity.
 */
static inline struct blkg_rwstat blkg_rwstat_read(struct blkg_rwstat *rwstat)
{
	unsigned int start;
	struct blkg_rwstat tmp;

	do {
		start = u64_stats_fetch_begin(&rwstat->syncp);
		tmp = *rwstat;
	} while (u64_stats_fetch_retry(&rwstat->syncp, start));

	return tmp;
}

/**
 * blkg_rwstat_sum - read the total count of a blkg_rwstat
 * @rwstat: blkg_rwstat to read
 *
 * Return the total count of @rwstat regardless of the IO direction.  This
 * function can be called without synchronization and takes care of u64
 * atomicity.
 */
static inline uint64_t blkg_rwstat_sum(struct blkg_rwstat *rwstat)
{
	struct blkg_rwstat tmp = blkg_rwstat_read(rwstat);

	return tmp.cnt[BLKG_RWSTAT_READ] + tmp.cnt[BLKG_RWSTAT_WRITE];
}

/**
 * blkg_rwstat_reset - reset a blkg_rwstat
 * @rwstat: blkg_rwstat to reset
 */
static inline void blkg_rwstat_reset(struct blkg_rwstat *rwstat)
{
	memset(rwstat->cnt, 0, sizeof(rwstat->cnt));
}

#else	/* CONFIG_BLK_CGROUP */

struct cgroup;

struct blkg_policy_data {
};

struct blkcg_gq {
};

struct blkcg_policy {
};

static inline struct blkcg *cgroup_to_blkcg(struct cgroup *cgroup) { return NULL; }
static inline struct blkcg *bio_blkcg(struct bio *bio) { return NULL; }
static inline struct blkcg_gq *blkg_lookup(struct blkcg *blkcg, void *key) { return NULL; }
static inline int blkcg_init_queue(struct request_queue *q) { return 0; }
static inline void blkcg_drain_queue(struct request_queue *q) { }
static inline void blkcg_exit_queue(struct request_queue *q) { }
static inline int blkcg_policy_register(struct blkcg_policy *pol) { return 0; }
static inline void blkcg_policy_unregister(struct blkcg_policy *pol) { }
static inline int blkcg_activate_policy(struct request_queue *q,
					const struct blkcg_policy *pol) { return 0; }
static inline void blkcg_deactivate_policy(struct request_queue *q,
					   const struct blkcg_policy *pol) { }

static inline struct blkg_policy_data *blkg_to_pd(struct blkcg_gq *blkg,
						  struct blkcg_policy *pol) { return NULL; }
static inline struct blkcg_gq *pd_to_blkg(struct blkg_policy_data *pd) { return NULL; }
static inline char *blkg_path(struct blkcg_gq *blkg) { return NULL; }
static inline void blkg_get(struct blkcg_gq *blkg) { }
static inline void blkg_put(struct blkcg_gq *blkg) { }

#endif	/* CONFIG_BLK_CGROUP */
#endif	/* _BLK_CGROUP_H */
