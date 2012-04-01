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

enum blkio_policy_id {
	BLKIO_POLICY_PROP = 0,		/* Proportional Bandwidth division */
	BLKIO_POLICY_THROTL,		/* Throttling */

	BLKIO_NR_POLICIES,
};

/* Max limits for throttle policy */
#define THROTL_IOPS_MAX		UINT_MAX

#ifdef CONFIG_BLK_CGROUP

/* cft->private [un]packing for stat printing */
#define BLKCG_STAT_PRIV(pol, off)	(((unsigned)(pol) << 16) | (off))
#define BLKCG_STAT_POL(prv)		((unsigned)(prv) >> 16)
#define BLKCG_STAT_OFF(prv)		((unsigned)(prv) & 0xffff)

enum blkg_rwstat_type {
	BLKG_RWSTAT_READ,
	BLKG_RWSTAT_WRITE,
	BLKG_RWSTAT_SYNC,
	BLKG_RWSTAT_ASYNC,

	BLKG_RWSTAT_NR,
	BLKG_RWSTAT_TOTAL = BLKG_RWSTAT_NR,
};

/* blkg state flags */
enum blkg_state_flags {
	BLKG_waiting = 0,
	BLKG_idling,
	BLKG_empty,
};

/* cgroup files owned by proportional weight policy */
enum blkcg_file_name_prop {
	BLKIO_PROP_weight = 1,
	BLKIO_PROP_weight_device,
};

/* cgroup files owned by throttle policy */
enum blkcg_file_name_throtl {
	BLKIO_THROTL_read_bps_device,
	BLKIO_THROTL_write_bps_device,
	BLKIO_THROTL_read_iops_device,
	BLKIO_THROTL_write_iops_device,
};

struct blkio_cgroup {
	struct cgroup_subsys_state css;
	unsigned int weight;
	spinlock_t lock;
	struct hlist_head blkg_list;

	/* for policies to test whether associated blkcg has changed */
	uint64_t id;
};

struct blkg_stat {
	struct u64_stats_sync		syncp;
	uint64_t			cnt;
};

struct blkg_rwstat {
	struct u64_stats_sync		syncp;
	uint64_t			cnt[BLKG_RWSTAT_NR];
};

struct blkio_group_stats {
	/* number of ios merged */
	struct blkg_rwstat		merged;
	/* total time spent on device in ns, may not be accurate w/ queueing */
	struct blkg_rwstat		service_time;
	/* total time spent waiting in scheduler queue in ns */
	struct blkg_rwstat		wait_time;
	/* number of IOs queued up */
	struct blkg_rwstat		queued;
	/* total disk time and nr sectors dispatched by this group */
	struct blkg_stat		time;
#ifdef CONFIG_DEBUG_BLK_CGROUP
	/* time not charged to this cgroup */
	struct blkg_stat		unaccounted_time;
	/* sum of number of ios queued across all samples */
	struct blkg_stat		avg_queue_size_sum;
	/* count of samples taken for average */
	struct blkg_stat		avg_queue_size_samples;
	/* how many times this group has been removed from service tree */
	struct blkg_stat		dequeue;
	/* total time spent waiting for it to be assigned a timeslice. */
	struct blkg_stat		group_wait_time;
	/* time spent idling for this blkio_group */
	struct blkg_stat		idle_time;
	/* total time with empty current active q with other requests queued */
	struct blkg_stat		empty_time;
	/* fields after this shouldn't be cleared on stat reset */
	uint64_t			start_group_wait_time;
	uint64_t			start_idle_time;
	uint64_t			start_empty_time;
	uint16_t			flags;
#endif
};

/* Per cpu blkio group stats */
struct blkio_group_stats_cpu {
	/* total bytes transferred */
	struct blkg_rwstat		service_bytes;
	/* total IOs serviced, post merge */
	struct blkg_rwstat		serviced;
	/* total sectors transferred */
	struct blkg_stat		sectors;
};

struct blkio_group_conf {
	unsigned int weight;
	unsigned int iops[2];
	u64 bps[2];
};

/* per-blkg per-policy data */
struct blkg_policy_data {
	/* the blkg this per-policy data belongs to */
	struct blkio_group *blkg;

	/* Configuration */
	struct blkio_group_conf conf;

	struct blkio_group_stats stats;
	/* Per cpu stats pointer */
	struct blkio_group_stats_cpu __percpu *stats_cpu;

	/* pol->pdata_size bytes of private data used by policy impl */
	char pdata[] __aligned(__alignof__(unsigned long long));
};

struct blkio_group {
	/* Pointer to the associated request_queue */
	struct request_queue *q;
	struct list_head q_node;
	struct hlist_node blkcg_node;
	struct blkio_cgroup *blkcg;
	/* Store cgroup path */
	char path[128];
	/* reference count */
	int refcnt;

	struct blkg_policy_data *pd[BLKIO_NR_POLICIES];

	/* List of blkg waiting for per cpu stats memory to be allocated */
	struct list_head alloc_node;
	struct rcu_head rcu_head;
};

typedef void (blkio_init_group_fn)(struct blkio_group *blkg);
typedef void (blkio_update_group_weight_fn)(struct request_queue *q,
			struct blkio_group *blkg, unsigned int weight);
typedef void (blkio_update_group_read_bps_fn)(struct request_queue *q,
			struct blkio_group *blkg, u64 read_bps);
typedef void (blkio_update_group_write_bps_fn)(struct request_queue *q,
			struct blkio_group *blkg, u64 write_bps);
typedef void (blkio_update_group_read_iops_fn)(struct request_queue *q,
			struct blkio_group *blkg, unsigned int read_iops);
typedef void (blkio_update_group_write_iops_fn)(struct request_queue *q,
			struct blkio_group *blkg, unsigned int write_iops);

struct blkio_policy_ops {
	blkio_init_group_fn *blkio_init_group_fn;
	blkio_update_group_weight_fn *blkio_update_group_weight_fn;
	blkio_update_group_read_bps_fn *blkio_update_group_read_bps_fn;
	blkio_update_group_write_bps_fn *blkio_update_group_write_bps_fn;
	blkio_update_group_read_iops_fn *blkio_update_group_read_iops_fn;
	blkio_update_group_write_iops_fn *blkio_update_group_write_iops_fn;
};

struct blkio_policy_type {
	struct list_head list;
	struct blkio_policy_ops ops;
	enum blkio_policy_id plid;
	size_t pdata_size;		/* policy specific private data size */
};

extern int blkcg_init_queue(struct request_queue *q);
extern void blkcg_drain_queue(struct request_queue *q);
extern void blkcg_exit_queue(struct request_queue *q);

/* Blkio controller policy registration */
extern void blkio_policy_register(struct blkio_policy_type *);
extern void blkio_policy_unregister(struct blkio_policy_type *);
extern void blkg_destroy_all(struct request_queue *q, bool destroy_root);
extern void update_root_blkg_pd(struct request_queue *q,
				enum blkio_policy_id plid);

/**
 * blkg_to_pdata - get policy private data
 * @blkg: blkg of interest
 * @pol: policy of interest
 *
 * Return pointer to private data associated with the @blkg-@pol pair.
 */
static inline void *blkg_to_pdata(struct blkio_group *blkg,
			      struct blkio_policy_type *pol)
{
	return blkg ? blkg->pd[pol->plid]->pdata : NULL;
}

/**
 * pdata_to_blkg - get blkg associated with policy private data
 * @pdata: policy private data of interest
 *
 * @pdata is policy private data.  Determine the blkg it's associated with.
 */
static inline struct blkio_group *pdata_to_blkg(void *pdata)
{
	if (pdata) {
		struct blkg_policy_data *pd =
			container_of(pdata, struct blkg_policy_data, pdata);
		return pd->blkg;
	}
	return NULL;
}

static inline char *blkg_path(struct blkio_group *blkg)
{
	return blkg->path;
}

/**
 * blkg_get - get a blkg reference
 * @blkg: blkg to get
 *
 * The caller should be holding queue_lock and an existing reference.
 */
static inline void blkg_get(struct blkio_group *blkg)
{
	lockdep_assert_held(blkg->q->queue_lock);
	WARN_ON_ONCE(!blkg->refcnt);
	blkg->refcnt++;
}

void __blkg_release(struct blkio_group *blkg);

/**
 * blkg_put - put a blkg reference
 * @blkg: blkg to put
 *
 * The caller should be holding queue_lock.
 */
static inline void blkg_put(struct blkio_group *blkg)
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
static struct blkg_rwstat blkg_rwstat_read(struct blkg_rwstat *rwstat)
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

#else

struct blkio_group {
};

struct blkio_policy_type {
};

static inline int blkcg_init_queue(struct request_queue *q) { return 0; }
static inline void blkcg_drain_queue(struct request_queue *q) { }
static inline void blkcg_exit_queue(struct request_queue *q) { }
static inline void blkio_policy_register(struct blkio_policy_type *blkiop) { }
static inline void blkio_policy_unregister(struct blkio_policy_type *blkiop) { }
static inline void blkg_destroy_all(struct request_queue *q,
				    bool destory_root) { }
static inline void update_root_blkg_pd(struct request_queue *q,
				       enum blkio_policy_id plid) { }

static inline void *blkg_to_pdata(struct blkio_group *blkg,
				struct blkio_policy_type *pol) { return NULL; }
static inline struct blkio_group *pdata_to_blkg(void *pdata,
				struct blkio_policy_type *pol) { return NULL; }
static inline char *blkg_path(struct blkio_group *blkg) { return NULL; }
static inline void blkg_get(struct blkio_group *blkg) { }
static inline void blkg_put(struct blkio_group *blkg) { }

#endif

#define BLKIO_WEIGHT_MIN	10
#define BLKIO_WEIGHT_MAX	1000
#define BLKIO_WEIGHT_DEFAULT	500

#ifdef CONFIG_DEBUG_BLK_CGROUP
void blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg,
					 struct blkio_policy_type *pol);
void blkiocg_update_dequeue_stats(struct blkio_group *blkg,
				  struct blkio_policy_type *pol,
				  unsigned long dequeue);
void blkiocg_update_set_idle_time_stats(struct blkio_group *blkg,
					struct blkio_policy_type *pol);
void blkiocg_update_idle_time_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol);
void blkiocg_set_start_empty_time(struct blkio_group *blkg,
				  struct blkio_policy_type *pol);

#define BLKG_FLAG_FNS(name)						\
static inline void blkio_mark_blkg_##name(				\
		struct blkio_group_stats *stats)			\
{									\
	stats->flags |= (1 << BLKG_##name);				\
}									\
static inline void blkio_clear_blkg_##name(				\
		struct blkio_group_stats *stats)			\
{									\
	stats->flags &= ~(1 << BLKG_##name);				\
}									\
static inline int blkio_blkg_##name(struct blkio_group_stats *stats)	\
{									\
	return (stats->flags & (1 << BLKG_##name)) != 0;		\
}									\

BLKG_FLAG_FNS(waiting)
BLKG_FLAG_FNS(idling)
BLKG_FLAG_FNS(empty)
#undef BLKG_FLAG_FNS
#else
static inline void blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol) { }
static inline void blkiocg_update_dequeue_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol, unsigned long dequeue) { }
static inline void blkiocg_update_set_idle_time_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol) { }
static inline void blkiocg_update_idle_time_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol) { }
static inline void blkiocg_set_start_empty_time(struct blkio_group *blkg,
			struct blkio_policy_type *pol) { }
#endif

#ifdef CONFIG_BLK_CGROUP
extern struct blkio_cgroup blkio_root_cgroup;
extern struct blkio_cgroup *cgroup_to_blkio_cgroup(struct cgroup *cgroup);
extern struct blkio_cgroup *bio_blkio_cgroup(struct bio *bio);
extern struct blkio_group *blkg_lookup(struct blkio_cgroup *blkcg,
				       struct request_queue *q);
struct blkio_group *blkg_lookup_create(struct blkio_cgroup *blkcg,
				       struct request_queue *q,
				       bool for_root);
void blkiocg_update_timeslice_used(struct blkio_group *blkg,
				   struct blkio_policy_type *pol,
				   unsigned long time,
				   unsigned long unaccounted_time);
void blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				   struct blkio_policy_type *pol,
				   uint64_t bytes, bool direction, bool sync);
void blkiocg_update_completion_stats(struct blkio_group *blkg,
				     struct blkio_policy_type *pol,
				     uint64_t start_time,
				     uint64_t io_start_time, bool direction,
				     bool sync);
void blkiocg_update_io_merged_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync);
void blkiocg_update_io_add_stats(struct blkio_group *blkg,
				 struct blkio_policy_type *pol,
				 struct blkio_group *curr_blkg, bool direction,
				 bool sync);
void blkiocg_update_io_remove_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync);
#else
struct cgroup;
static inline struct blkio_cgroup *
cgroup_to_blkio_cgroup(struct cgroup *cgroup) { return NULL; }
static inline struct blkio_cgroup *
bio_blkio_cgroup(struct bio *bio) { return NULL; }

static inline struct blkio_group *blkg_lookup(struct blkio_cgroup *blkcg,
					      void *key) { return NULL; }
static inline void blkiocg_update_timeslice_used(struct blkio_group *blkg,
			struct blkio_policy_type *pol, unsigned long time,
			unsigned long unaccounted_time) { }
static inline void blkiocg_update_dispatch_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol, uint64_t bytes,
			bool direction, bool sync) { }
static inline void blkiocg_update_completion_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol, uint64_t start_time,
			uint64_t io_start_time, bool direction, bool sync) { }
static inline void blkiocg_update_io_merged_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol, bool direction,
			bool sync) { }
static inline void blkiocg_update_io_add_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol,
			struct blkio_group *curr_blkg, bool direction,
			bool sync) { }
static inline void blkiocg_update_io_remove_stats(struct blkio_group *blkg,
			struct blkio_policy_type *pol, bool direction,
			bool sync) { }
#endif
#endif /* _BLK_CGROUP_H */
