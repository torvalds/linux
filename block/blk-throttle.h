#ifndef BLK_THROTTLE_H
#define BLK_THROTTLE_H

#include "blk-cgroup-rwstat.h"

/*
 * To implement hierarchical throttling, throtl_grps form a tree and bios
 * are dispatched upwards level by level until they reach the top and get
 * issued.  When dispatching bios from the children and local group at each
 * level, if the bios are dispatched into a single bio_list, there's a risk
 * of a local or child group which can queue many bios at once filling up
 * the list starving others.
 *
 * To avoid such starvation, dispatched bios are queued separately
 * according to where they came from.  When they are again dispatched to
 * the parent, they're popped in round-robin order so that no single source
 * hogs the dispatch window.
 *
 * throtl_qnode is used to keep the queued bios separated by their sources.
 * Bios are queued to throtl_qnode which in turn is queued to
 * throtl_service_queue and then dispatched in round-robin order.
 *
 * It's also used to track the reference counts on blkg's.  A qnode always
 * belongs to a throtl_grp and gets queued on itself or the parent, so
 * incrementing the reference of the associated throtl_grp when a qnode is
 * queued and decrementing when dequeued is enough to keep the whole blkg
 * tree pinned while bios are in flight.
 */
struct throtl_qnode {
	struct list_head	node;		/* service_queue->queued[] */
	struct bio_list		bios;		/* queued bios */
	struct throtl_grp	*tg;		/* tg this qnode belongs to */
};

struct throtl_service_queue {
	struct throtl_service_queue *parent_sq;	/* the parent service_queue */

	/*
	 * Bios queued directly to this service_queue or dispatched from
	 * children throtl_grp's.
	 */
	struct list_head	queued[2];	/* throtl_qnode [READ/WRITE] */
	unsigned int		nr_queued[2];	/* number of queued bios */

	/*
	 * RB tree of active children throtl_grp's, which are sorted by
	 * their ->disptime.
	 */
	struct rb_root_cached	pending_tree;	/* RB tree of active tgs */
	unsigned int		nr_pending;	/* # queued in the tree */
	unsigned long		first_pending_disptime;	/* disptime of the first tg */
	struct timer_list	pending_timer;	/* fires on first_pending_disptime */
};

enum tg_state_flags {
	THROTL_TG_PENDING	= 1 << 0,	/* on parent's pending tree */
	THROTL_TG_WAS_EMPTY	= 1 << 1,	/* bio_lists[] became non-empty */
	THROTL_TG_CANCELING	= 1 << 2,	/* starts to cancel bio */
};

struct throtl_grp {
	/* must be the first member */
	struct blkg_policy_data pd;

	/* active throtl group service_queue member */
	struct rb_node rb_node;

	/* throtl_data this group belongs to */
	struct throtl_data *td;

	/* this group's service queue */
	struct throtl_service_queue service_queue;

	/*
	 * qnode_on_self is used when bios are directly queued to this
	 * throtl_grp so that local bios compete fairly with bios
	 * dispatched from children.  qnode_on_parent is used when bios are
	 * dispatched from this throtl_grp into its parent and will compete
	 * with the sibling qnode_on_parents and the parent's
	 * qnode_on_self.
	 */
	struct throtl_qnode qnode_on_self[2];
	struct throtl_qnode qnode_on_parent[2];

	/*
	 * Dispatch time in jiffies. This is the estimated time when group
	 * will unthrottle and is ready to dispatch more bio. It is used as
	 * key to sort active groups in service tree.
	 */
	unsigned long disptime;

	unsigned int flags;

	/* are there any throtl rules between this group and td? */
	bool has_rules_bps[2];
	bool has_rules_iops[2];

	/* bytes per second rate limits */
	uint64_t bps[2];

	/* IOPS limits */
	unsigned int iops[2];

	/* Number of bytes dispatched in current slice */
	uint64_t bytes_disp[2];
	/* Number of bio's dispatched in current slice */
	unsigned int io_disp[2];

	unsigned long last_low_overflow_time[2];

	uint64_t last_bytes_disp[2];
	unsigned int last_io_disp[2];

	/*
	 * The following two fields are updated when new configuration is
	 * submitted while some bios are still throttled, they record how many
	 * bytes/ios are waited already in previous configuration, and they will
	 * be used to calculate wait time under new configuration.
	 */
	long long carryover_bytes[2];
	int carryover_ios[2];

	unsigned long last_check_time;

	/* When did we start a new slice */
	unsigned long slice_start[2];
	unsigned long slice_end[2];

	struct blkg_rwstat stat_bytes;
	struct blkg_rwstat stat_ios;
};

extern struct blkcg_policy blkcg_policy_throtl;

static inline struct throtl_grp *pd_to_tg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct throtl_grp, pd) : NULL;
}

static inline struct throtl_grp *blkg_to_tg(struct blkcg_gq *blkg)
{
	return pd_to_tg(blkg_to_pd(blkg, &blkcg_policy_throtl));
}

/*
 * Internal throttling interface
 */
#ifndef CONFIG_BLK_DEV_THROTTLING
static inline void blk_throtl_exit(struct gendisk *disk) { }
static inline bool blk_throtl_bio(struct bio *bio) { return false; }
static inline void blk_throtl_cancel_bios(struct gendisk *disk) { }
#else /* CONFIG_BLK_DEV_THROTTLING */
void blk_throtl_exit(struct gendisk *disk);
bool __blk_throtl_bio(struct bio *bio);
void blk_throtl_cancel_bios(struct gendisk *disk);

static inline bool blk_throtl_activated(struct request_queue *q)
{
	return q->td != NULL;
}

static inline bool blk_should_throtl(struct bio *bio)
{
	struct throtl_grp *tg;
	int rw = bio_data_dir(bio);

	/*
	 * This is called under bio_queue_enter(), and it's synchronized with
	 * the activation of blk-throtl, which is protected by
	 * blk_mq_freeze_queue().
	 */
	if (!blk_throtl_activated(bio->bi_bdev->bd_queue))
		return false;

	tg = blkg_to_tg(bio->bi_blkg);
	if (!cgroup_subsys_on_dfl(io_cgrp_subsys)) {
		if (!bio_flagged(bio, BIO_CGROUP_ACCT)) {
			bio_set_flag(bio, BIO_CGROUP_ACCT);
			blkg_rwstat_add(&tg->stat_bytes, bio->bi_opf,
					bio->bi_iter.bi_size);
		}
		blkg_rwstat_add(&tg->stat_ios, bio->bi_opf, 1);
	}

	/* iops limit is always counted */
	if (tg->has_rules_iops[rw])
		return true;

	if (tg->has_rules_bps[rw] && !bio_flagged(bio, BIO_BPS_THROTTLED))
		return true;

	return false;
}

static inline bool blk_throtl_bio(struct bio *bio)
{

	if (!blk_should_throtl(bio))
		return false;

	return __blk_throtl_bio(bio);
}
#endif /* CONFIG_BLK_DEV_THROTTLING */

#endif
