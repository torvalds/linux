/*
 * Interface for controlling IO bandwidth on a request queue
 *
 * Copyright (C) 2010 Vivek Goyal <vgoyal@redhat.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/blktrace_api.h>
#include "blk-cgroup.h"
#include "blk.h"

/* Max dispatch from a group in 1 round */
static int throtl_grp_quantum = 8;

/* Total max dispatch from all groups in one round */
static int throtl_quantum = 32;

/* Throttling is performed over 100ms slice and after that slice is renewed */
static unsigned long throtl_slice = HZ/10;	/* 100 ms */

static struct blkio_policy_type blkio_policy_throtl;

/* A workqueue to queue throttle related work */
static struct workqueue_struct *kthrotld_workqueue;
static void throtl_schedule_delayed_work(struct throtl_data *td,
				unsigned long delay);

struct throtl_rb_root {
	struct rb_root rb;
	struct rb_node *left;
	unsigned int count;
	unsigned long min_disptime;
};

#define THROTL_RB_ROOT	(struct throtl_rb_root) { .rb = RB_ROOT, .left = NULL, \
			.count = 0, .min_disptime = 0}

#define rb_entry_tg(node)	rb_entry((node), struct throtl_grp, rb_node)

struct throtl_grp {
	/* List of throtl groups on the request queue*/
	struct hlist_node tg_node;

	/* active throtl group service_tree member */
	struct rb_node rb_node;

	/*
	 * Dispatch time in jiffies. This is the estimated time when group
	 * will unthrottle and is ready to dispatch more bio. It is used as
	 * key to sort active groups in service tree.
	 */
	unsigned long disptime;

	unsigned int flags;

	/* Two lists for READ and WRITE */
	struct bio_list bio_lists[2];

	/* Number of queued bios on READ and WRITE lists */
	unsigned int nr_queued[2];

	/* bytes per second rate limits */
	uint64_t bps[2];

	/* IOPS limits */
	unsigned int iops[2];

	/* Number of bytes disptached in current slice */
	uint64_t bytes_disp[2];
	/* Number of bio's dispatched in current slice */
	unsigned int io_disp[2];

	/* When did we start a new slice */
	unsigned long slice_start[2];
	unsigned long slice_end[2];

	/* Some throttle limits got updated for the group */
	int limits_changed;
};

struct throtl_data
{
	/* List of throtl groups */
	struct hlist_head tg_list;

	/* service tree for active throtl groups */
	struct throtl_rb_root tg_service_tree;

	struct throtl_grp *root_tg;
	struct request_queue *queue;

	/* Total Number of queued bios on READ and WRITE lists */
	unsigned int nr_queued[2];

	/*
	 * number of total undestroyed groups
	 */
	unsigned int nr_undestroyed_grps;

	/* Work for dispatching throttled bios */
	struct delayed_work throtl_work;

	int limits_changed;
};

static inline struct throtl_grp *blkg_to_tg(struct blkio_group *blkg)
{
	return blkg_to_pdata(blkg, &blkio_policy_throtl);
}

static inline struct blkio_group *tg_to_blkg(struct throtl_grp *tg)
{
	return pdata_to_blkg(tg, &blkio_policy_throtl);
}

enum tg_state_flags {
	THROTL_TG_FLAG_on_rr = 0,	/* on round-robin busy list */
};

#define THROTL_TG_FNS(name)						\
static inline void throtl_mark_tg_##name(struct throtl_grp *tg)		\
{									\
	(tg)->flags |= (1 << THROTL_TG_FLAG_##name);			\
}									\
static inline void throtl_clear_tg_##name(struct throtl_grp *tg)	\
{									\
	(tg)->flags &= ~(1 << THROTL_TG_FLAG_##name);			\
}									\
static inline int throtl_tg_##name(const struct throtl_grp *tg)		\
{									\
	return ((tg)->flags & (1 << THROTL_TG_FLAG_##name)) != 0;	\
}

THROTL_TG_FNS(on_rr);

#define throtl_log_tg(td, tg, fmt, args...)				\
	blk_add_trace_msg((td)->queue, "throtl %s " fmt,		\
			  blkg_path(tg_to_blkg(tg)), ##args);		\

#define throtl_log(td, fmt, args...)	\
	blk_add_trace_msg((td)->queue, "throtl " fmt, ##args)

static inline unsigned int total_nr_queued(struct throtl_data *td)
{
	return td->nr_queued[0] + td->nr_queued[1];
}

static void throtl_init_blkio_group(struct blkio_group *blkg)
{
	struct throtl_grp *tg = blkg_to_tg(blkg);

	INIT_HLIST_NODE(&tg->tg_node);
	RB_CLEAR_NODE(&tg->rb_node);
	bio_list_init(&tg->bio_lists[0]);
	bio_list_init(&tg->bio_lists[1]);
	tg->limits_changed = false;

	tg->bps[READ] = -1;
	tg->bps[WRITE] = -1;
	tg->iops[READ] = -1;
	tg->iops[WRITE] = -1;
}

static void throtl_link_blkio_group(struct request_queue *q,
				    struct blkio_group *blkg)
{
	struct throtl_data *td = q->td;
	struct throtl_grp *tg = blkg_to_tg(blkg);

	hlist_add_head(&tg->tg_node, &td->tg_list);
	td->nr_undestroyed_grps++;
}

static struct
throtl_grp *throtl_lookup_tg(struct throtl_data *td, struct blkio_cgroup *blkcg)
{
	/*
	 * This is the common case when there are no blkio cgroups.
	 * Avoid lookup in this case
	 */
	if (blkcg == &blkio_root_cgroup)
		return td->root_tg;

	return blkg_to_tg(blkg_lookup(blkcg, td->queue, BLKIO_POLICY_THROTL));
}

static struct throtl_grp *throtl_lookup_create_tg(struct throtl_data *td,
						  struct blkio_cgroup *blkcg)
{
	struct request_queue *q = td->queue;
	struct throtl_grp *tg = NULL;

	/*
	 * This is the common case when there are no blkio cgroups.
	 * Avoid lookup in this case
	 */
	if (blkcg == &blkio_root_cgroup) {
		tg = td->root_tg;
	} else {
		struct blkio_group *blkg;

		blkg = blkg_lookup_create(blkcg, q, BLKIO_POLICY_THROTL, false);

		/* if %NULL and @q is alive, fall back to root_tg */
		if (!IS_ERR(blkg))
			tg = blkg_to_tg(blkg);
		else if (!blk_queue_dead(q))
			tg = td->root_tg;
	}

	return tg;
}

static struct throtl_grp *throtl_rb_first(struct throtl_rb_root *root)
{
	/* Service tree is empty */
	if (!root->count)
		return NULL;

	if (!root->left)
		root->left = rb_first(&root->rb);

	if (root->left)
		return rb_entry_tg(root->left);

	return NULL;
}

static void rb_erase_init(struct rb_node *n, struct rb_root *root)
{
	rb_erase(n, root);
	RB_CLEAR_NODE(n);
}

static void throtl_rb_erase(struct rb_node *n, struct throtl_rb_root *root)
{
	if (root->left == n)
		root->left = NULL;
	rb_erase_init(n, &root->rb);
	--root->count;
}

static void update_min_dispatch_time(struct throtl_rb_root *st)
{
	struct throtl_grp *tg;

	tg = throtl_rb_first(st);
	if (!tg)
		return;

	st->min_disptime = tg->disptime;
}

static void
tg_service_tree_add(struct throtl_rb_root *st, struct throtl_grp *tg)
{
	struct rb_node **node = &st->rb.rb_node;
	struct rb_node *parent = NULL;
	struct throtl_grp *__tg;
	unsigned long key = tg->disptime;
	int left = 1;

	while (*node != NULL) {
		parent = *node;
		__tg = rb_entry_tg(parent);

		if (time_before(key, __tg->disptime))
			node = &parent->rb_left;
		else {
			node = &parent->rb_right;
			left = 0;
		}
	}

	if (left)
		st->left = &tg->rb_node;

	rb_link_node(&tg->rb_node, parent, node);
	rb_insert_color(&tg->rb_node, &st->rb);
}

static void __throtl_enqueue_tg(struct throtl_data *td, struct throtl_grp *tg)
{
	struct throtl_rb_root *st = &td->tg_service_tree;

	tg_service_tree_add(st, tg);
	throtl_mark_tg_on_rr(tg);
	st->count++;
}

static void throtl_enqueue_tg(struct throtl_data *td, struct throtl_grp *tg)
{
	if (!throtl_tg_on_rr(tg))
		__throtl_enqueue_tg(td, tg);
}

static void __throtl_dequeue_tg(struct throtl_data *td, struct throtl_grp *tg)
{
	throtl_rb_erase(&tg->rb_node, &td->tg_service_tree);
	throtl_clear_tg_on_rr(tg);
}

static void throtl_dequeue_tg(struct throtl_data *td, struct throtl_grp *tg)
{
	if (throtl_tg_on_rr(tg))
		__throtl_dequeue_tg(td, tg);
}

static void throtl_schedule_next_dispatch(struct throtl_data *td)
{
	struct throtl_rb_root *st = &td->tg_service_tree;

	/*
	 * If there are more bios pending, schedule more work.
	 */
	if (!total_nr_queued(td))
		return;

	BUG_ON(!st->count);

	update_min_dispatch_time(st);

	if (time_before_eq(st->min_disptime, jiffies))
		throtl_schedule_delayed_work(td, 0);
	else
		throtl_schedule_delayed_work(td, (st->min_disptime - jiffies));
}

static inline void
throtl_start_new_slice(struct throtl_data *td, struct throtl_grp *tg, bool rw)
{
	tg->bytes_disp[rw] = 0;
	tg->io_disp[rw] = 0;
	tg->slice_start[rw] = jiffies;
	tg->slice_end[rw] = jiffies + throtl_slice;
	throtl_log_tg(td, tg, "[%c] new slice start=%lu end=%lu jiffies=%lu",
			rw == READ ? 'R' : 'W', tg->slice_start[rw],
			tg->slice_end[rw], jiffies);
}

static inline void throtl_set_slice_end(struct throtl_data *td,
		struct throtl_grp *tg, bool rw, unsigned long jiffy_end)
{
	tg->slice_end[rw] = roundup(jiffy_end, throtl_slice);
}

static inline void throtl_extend_slice(struct throtl_data *td,
		struct throtl_grp *tg, bool rw, unsigned long jiffy_end)
{
	tg->slice_end[rw] = roundup(jiffy_end, throtl_slice);
	throtl_log_tg(td, tg, "[%c] extend slice start=%lu end=%lu jiffies=%lu",
			rw == READ ? 'R' : 'W', tg->slice_start[rw],
			tg->slice_end[rw], jiffies);
}

/* Determine if previously allocated or extended slice is complete or not */
static bool
throtl_slice_used(struct throtl_data *td, struct throtl_grp *tg, bool rw)
{
	if (time_in_range(jiffies, tg->slice_start[rw], tg->slice_end[rw]))
		return 0;

	return 1;
}

/* Trim the used slices and adjust slice start accordingly */
static inline void
throtl_trim_slice(struct throtl_data *td, struct throtl_grp *tg, bool rw)
{
	unsigned long nr_slices, time_elapsed, io_trim;
	u64 bytes_trim, tmp;

	BUG_ON(time_before(tg->slice_end[rw], tg->slice_start[rw]));

	/*
	 * If bps are unlimited (-1), then time slice don't get
	 * renewed. Don't try to trim the slice if slice is used. A new
	 * slice will start when appropriate.
	 */
	if (throtl_slice_used(td, tg, rw))
		return;

	/*
	 * A bio has been dispatched. Also adjust slice_end. It might happen
	 * that initially cgroup limit was very low resulting in high
	 * slice_end, but later limit was bumped up and bio was dispached
	 * sooner, then we need to reduce slice_end. A high bogus slice_end
	 * is bad because it does not allow new slice to start.
	 */

	throtl_set_slice_end(td, tg, rw, jiffies + throtl_slice);

	time_elapsed = jiffies - tg->slice_start[rw];

	nr_slices = time_elapsed / throtl_slice;

	if (!nr_slices)
		return;
	tmp = tg->bps[rw] * throtl_slice * nr_slices;
	do_div(tmp, HZ);
	bytes_trim = tmp;

	io_trim = (tg->iops[rw] * throtl_slice * nr_slices)/HZ;

	if (!bytes_trim && !io_trim)
		return;

	if (tg->bytes_disp[rw] >= bytes_trim)
		tg->bytes_disp[rw] -= bytes_trim;
	else
		tg->bytes_disp[rw] = 0;

	if (tg->io_disp[rw] >= io_trim)
		tg->io_disp[rw] -= io_trim;
	else
		tg->io_disp[rw] = 0;

	tg->slice_start[rw] += nr_slices * throtl_slice;

	throtl_log_tg(td, tg, "[%c] trim slice nr=%lu bytes=%llu io=%lu"
			" start=%lu end=%lu jiffies=%lu",
			rw == READ ? 'R' : 'W', nr_slices, bytes_trim, io_trim,
			tg->slice_start[rw], tg->slice_end[rw], jiffies);
}

static bool tg_with_in_iops_limit(struct throtl_data *td, struct throtl_grp *tg,
		struct bio *bio, unsigned long *wait)
{
	bool rw = bio_data_dir(bio);
	unsigned int io_allowed;
	unsigned long jiffy_elapsed, jiffy_wait, jiffy_elapsed_rnd;
	u64 tmp;

	jiffy_elapsed = jiffy_elapsed_rnd = jiffies - tg->slice_start[rw];

	/* Slice has just started. Consider one slice interval */
	if (!jiffy_elapsed)
		jiffy_elapsed_rnd = throtl_slice;

	jiffy_elapsed_rnd = roundup(jiffy_elapsed_rnd, throtl_slice);

	/*
	 * jiffy_elapsed_rnd should not be a big value as minimum iops can be
	 * 1 then at max jiffy elapsed should be equivalent of 1 second as we
	 * will allow dispatch after 1 second and after that slice should
	 * have been trimmed.
	 */

	tmp = (u64)tg->iops[rw] * jiffy_elapsed_rnd;
	do_div(tmp, HZ);

	if (tmp > UINT_MAX)
		io_allowed = UINT_MAX;
	else
		io_allowed = tmp;

	if (tg->io_disp[rw] + 1 <= io_allowed) {
		if (wait)
			*wait = 0;
		return 1;
	}

	/* Calc approx time to dispatch */
	jiffy_wait = ((tg->io_disp[rw] + 1) * HZ)/tg->iops[rw] + 1;

	if (jiffy_wait > jiffy_elapsed)
		jiffy_wait = jiffy_wait - jiffy_elapsed;
	else
		jiffy_wait = 1;

	if (wait)
		*wait = jiffy_wait;
	return 0;
}

static bool tg_with_in_bps_limit(struct throtl_data *td, struct throtl_grp *tg,
		struct bio *bio, unsigned long *wait)
{
	bool rw = bio_data_dir(bio);
	u64 bytes_allowed, extra_bytes, tmp;
	unsigned long jiffy_elapsed, jiffy_wait, jiffy_elapsed_rnd;

	jiffy_elapsed = jiffy_elapsed_rnd = jiffies - tg->slice_start[rw];

	/* Slice has just started. Consider one slice interval */
	if (!jiffy_elapsed)
		jiffy_elapsed_rnd = throtl_slice;

	jiffy_elapsed_rnd = roundup(jiffy_elapsed_rnd, throtl_slice);

	tmp = tg->bps[rw] * jiffy_elapsed_rnd;
	do_div(tmp, HZ);
	bytes_allowed = tmp;

	if (tg->bytes_disp[rw] + bio->bi_size <= bytes_allowed) {
		if (wait)
			*wait = 0;
		return 1;
	}

	/* Calc approx time to dispatch */
	extra_bytes = tg->bytes_disp[rw] + bio->bi_size - bytes_allowed;
	jiffy_wait = div64_u64(extra_bytes * HZ, tg->bps[rw]);

	if (!jiffy_wait)
		jiffy_wait = 1;

	/*
	 * This wait time is without taking into consideration the rounding
	 * up we did. Add that time also.
	 */
	jiffy_wait = jiffy_wait + (jiffy_elapsed_rnd - jiffy_elapsed);
	if (wait)
		*wait = jiffy_wait;
	return 0;
}

static bool tg_no_rule_group(struct throtl_grp *tg, bool rw) {
	if (tg->bps[rw] == -1 && tg->iops[rw] == -1)
		return 1;
	return 0;
}

/*
 * Returns whether one can dispatch a bio or not. Also returns approx number
 * of jiffies to wait before this bio is with-in IO rate and can be dispatched
 */
static bool tg_may_dispatch(struct throtl_data *td, struct throtl_grp *tg,
				struct bio *bio, unsigned long *wait)
{
	bool rw = bio_data_dir(bio);
	unsigned long bps_wait = 0, iops_wait = 0, max_wait = 0;

	/*
 	 * Currently whole state machine of group depends on first bio
	 * queued in the group bio list. So one should not be calling
	 * this function with a different bio if there are other bios
	 * queued.
	 */
	BUG_ON(tg->nr_queued[rw] && bio != bio_list_peek(&tg->bio_lists[rw]));

	/* If tg->bps = -1, then BW is unlimited */
	if (tg->bps[rw] == -1 && tg->iops[rw] == -1) {
		if (wait)
			*wait = 0;
		return 1;
	}

	/*
	 * If previous slice expired, start a new one otherwise renew/extend
	 * existing slice to make sure it is at least throtl_slice interval
	 * long since now.
	 */
	if (throtl_slice_used(td, tg, rw))
		throtl_start_new_slice(td, tg, rw);
	else {
		if (time_before(tg->slice_end[rw], jiffies + throtl_slice))
			throtl_extend_slice(td, tg, rw, jiffies + throtl_slice);
	}

	if (tg_with_in_bps_limit(td, tg, bio, &bps_wait)
	    && tg_with_in_iops_limit(td, tg, bio, &iops_wait)) {
		if (wait)
			*wait = 0;
		return 1;
	}

	max_wait = max(bps_wait, iops_wait);

	if (wait)
		*wait = max_wait;

	if (time_before(tg->slice_end[rw], jiffies + max_wait))
		throtl_extend_slice(td, tg, rw, jiffies + max_wait);

	return 0;
}

static void throtl_charge_bio(struct throtl_grp *tg, struct bio *bio)
{
	bool rw = bio_data_dir(bio);
	bool sync = rw_is_sync(bio->bi_rw);

	/* Charge the bio to the group */
	tg->bytes_disp[rw] += bio->bi_size;
	tg->io_disp[rw]++;

	blkiocg_update_dispatch_stats(tg_to_blkg(tg), bio->bi_size, rw, sync);
}

static void throtl_add_bio_tg(struct throtl_data *td, struct throtl_grp *tg,
			struct bio *bio)
{
	bool rw = bio_data_dir(bio);

	bio_list_add(&tg->bio_lists[rw], bio);
	/* Take a bio reference on tg */
	blkg_get(tg_to_blkg(tg));
	tg->nr_queued[rw]++;
	td->nr_queued[rw]++;
	throtl_enqueue_tg(td, tg);
}

static void tg_update_disptime(struct throtl_data *td, struct throtl_grp *tg)
{
	unsigned long read_wait = -1, write_wait = -1, min_wait = -1, disptime;
	struct bio *bio;

	if ((bio = bio_list_peek(&tg->bio_lists[READ])))
		tg_may_dispatch(td, tg, bio, &read_wait);

	if ((bio = bio_list_peek(&tg->bio_lists[WRITE])))
		tg_may_dispatch(td, tg, bio, &write_wait);

	min_wait = min(read_wait, write_wait);
	disptime = jiffies + min_wait;

	/* Update dispatch time */
	throtl_dequeue_tg(td, tg);
	tg->disptime = disptime;
	throtl_enqueue_tg(td, tg);
}

static void tg_dispatch_one_bio(struct throtl_data *td, struct throtl_grp *tg,
				bool rw, struct bio_list *bl)
{
	struct bio *bio;

	bio = bio_list_pop(&tg->bio_lists[rw]);
	tg->nr_queued[rw]--;
	/* Drop bio reference on blkg */
	blkg_put(tg_to_blkg(tg));

	BUG_ON(td->nr_queued[rw] <= 0);
	td->nr_queued[rw]--;

	throtl_charge_bio(tg, bio);
	bio_list_add(bl, bio);
	bio->bi_rw |= REQ_THROTTLED;

	throtl_trim_slice(td, tg, rw);
}

static int throtl_dispatch_tg(struct throtl_data *td, struct throtl_grp *tg,
				struct bio_list *bl)
{
	unsigned int nr_reads = 0, nr_writes = 0;
	unsigned int max_nr_reads = throtl_grp_quantum*3/4;
	unsigned int max_nr_writes = throtl_grp_quantum - max_nr_reads;
	struct bio *bio;

	/* Try to dispatch 75% READS and 25% WRITES */

	while ((bio = bio_list_peek(&tg->bio_lists[READ]))
		&& tg_may_dispatch(td, tg, bio, NULL)) {

		tg_dispatch_one_bio(td, tg, bio_data_dir(bio), bl);
		nr_reads++;

		if (nr_reads >= max_nr_reads)
			break;
	}

	while ((bio = bio_list_peek(&tg->bio_lists[WRITE]))
		&& tg_may_dispatch(td, tg, bio, NULL)) {

		tg_dispatch_one_bio(td, tg, bio_data_dir(bio), bl);
		nr_writes++;

		if (nr_writes >= max_nr_writes)
			break;
	}

	return nr_reads + nr_writes;
}

static int throtl_select_dispatch(struct throtl_data *td, struct bio_list *bl)
{
	unsigned int nr_disp = 0;
	struct throtl_grp *tg;
	struct throtl_rb_root *st = &td->tg_service_tree;

	while (1) {
		tg = throtl_rb_first(st);

		if (!tg)
			break;

		if (time_before(jiffies, tg->disptime))
			break;

		throtl_dequeue_tg(td, tg);

		nr_disp += throtl_dispatch_tg(td, tg, bl);

		if (tg->nr_queued[0] || tg->nr_queued[1]) {
			tg_update_disptime(td, tg);
			throtl_enqueue_tg(td, tg);
		}

		if (nr_disp >= throtl_quantum)
			break;
	}

	return nr_disp;
}

static void throtl_process_limit_change(struct throtl_data *td)
{
	struct throtl_grp *tg;
	struct hlist_node *pos, *n;

	if (!td->limits_changed)
		return;

	xchg(&td->limits_changed, false);

	throtl_log(td, "limits changed");

	hlist_for_each_entry_safe(tg, pos, n, &td->tg_list, tg_node) {
		if (!tg->limits_changed)
			continue;

		if (!xchg(&tg->limits_changed, false))
			continue;

		throtl_log_tg(td, tg, "limit change rbps=%llu wbps=%llu"
			" riops=%u wiops=%u", tg->bps[READ], tg->bps[WRITE],
			tg->iops[READ], tg->iops[WRITE]);

		/*
		 * Restart the slices for both READ and WRITES. It
		 * might happen that a group's limit are dropped
		 * suddenly and we don't want to account recently
		 * dispatched IO with new low rate
		 */
		throtl_start_new_slice(td, tg, 0);
		throtl_start_new_slice(td, tg, 1);

		if (throtl_tg_on_rr(tg))
			tg_update_disptime(td, tg);
	}
}

/* Dispatch throttled bios. Should be called without queue lock held. */
static int throtl_dispatch(struct request_queue *q)
{
	struct throtl_data *td = q->td;
	unsigned int nr_disp = 0;
	struct bio_list bio_list_on_stack;
	struct bio *bio;
	struct blk_plug plug;

	spin_lock_irq(q->queue_lock);

	throtl_process_limit_change(td);

	if (!total_nr_queued(td))
		goto out;

	bio_list_init(&bio_list_on_stack);

	throtl_log(td, "dispatch nr_queued=%u read=%u write=%u",
			total_nr_queued(td), td->nr_queued[READ],
			td->nr_queued[WRITE]);

	nr_disp = throtl_select_dispatch(td, &bio_list_on_stack);

	if (nr_disp)
		throtl_log(td, "bios disp=%u", nr_disp);

	throtl_schedule_next_dispatch(td);
out:
	spin_unlock_irq(q->queue_lock);

	/*
	 * If we dispatched some requests, unplug the queue to make sure
	 * immediate dispatch
	 */
	if (nr_disp) {
		blk_start_plug(&plug);
		while((bio = bio_list_pop(&bio_list_on_stack)))
			generic_make_request(bio);
		blk_finish_plug(&plug);
	}
	return nr_disp;
}

void blk_throtl_work(struct work_struct *work)
{
	struct throtl_data *td = container_of(work, struct throtl_data,
					throtl_work.work);
	struct request_queue *q = td->queue;

	throtl_dispatch(q);
}

/* Call with queue lock held */
static void
throtl_schedule_delayed_work(struct throtl_data *td, unsigned long delay)
{

	struct delayed_work *dwork = &td->throtl_work;

	/* schedule work if limits changed even if no bio is queued */
	if (total_nr_queued(td) || td->limits_changed) {
		/*
		 * We might have a work scheduled to be executed in future.
		 * Cancel that and schedule a new one.
		 */
		__cancel_delayed_work(dwork);
		queue_delayed_work(kthrotld_workqueue, dwork, delay);
		throtl_log(td, "schedule work. delay=%lu jiffies=%lu",
				delay, jiffies);
	}
}

static void
throtl_destroy_tg(struct throtl_data *td, struct throtl_grp *tg)
{
	/* Something wrong if we are trying to remove same group twice */
	BUG_ON(hlist_unhashed(&tg->tg_node));

	hlist_del_init(&tg->tg_node);

	/*
	 * Put the reference taken at the time of creation so that when all
	 * queues are gone, group can be destroyed.
	 */
	blkg_put(tg_to_blkg(tg));
	td->nr_undestroyed_grps--;
}

static bool throtl_release_tgs(struct throtl_data *td, bool release_root)
{
	struct hlist_node *pos, *n;
	struct throtl_grp *tg;
	bool empty = true;

	hlist_for_each_entry_safe(tg, pos, n, &td->tg_list, tg_node) {
		/* skip root? */
		if (!release_root && tg == td->root_tg)
			continue;

		/*
		 * If cgroup removal path got to blk_group first and removed
		 * it from cgroup list, then it will take care of destroying
		 * cfqg also.
		 */
		if (!blkiocg_del_blkio_group(tg_to_blkg(tg)))
			throtl_destroy_tg(td, tg);
		else
			empty = false;
	}
	return empty;
}

/*
 * Blk cgroup controller notification saying that blkio_group object is being
 * delinked as associated cgroup object is going away. That also means that
 * no new IO will come in this group. So get rid of this group as soon as
 * any pending IO in the group is finished.
 *
 * This function is called under rcu_read_lock(). @q is the rcu protected
 * pointer. That means @q is a valid request_queue pointer as long as we
 * are rcu read lock.
 *
 * @q was fetched from blkio_group under blkio_cgroup->lock. That means
 * it should not be NULL as even if queue was going away, cgroup deltion
 * path got to it first.
 */
void throtl_unlink_blkio_group(struct request_queue *q,
			       struct blkio_group *blkg)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	throtl_destroy_tg(q->td, blkg_to_tg(blkg));
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static bool throtl_clear_queue(struct request_queue *q)
{
	lockdep_assert_held(q->queue_lock);

	/*
	 * Clear tgs but leave the root one alone.  This is necessary
	 * because root_tg is expected to be persistent and safe because
	 * blk-throtl can never be disabled while @q is alive.  This is a
	 * kludge to prepare for unified blkg.  This whole function will be
	 * removed soon.
	 */
	return throtl_release_tgs(q->td, false);
}

static void throtl_update_blkio_group_common(struct throtl_data *td,
				struct throtl_grp *tg)
{
	xchg(&tg->limits_changed, true);
	xchg(&td->limits_changed, true);
	/* Schedule a work now to process the limit change */
	throtl_schedule_delayed_work(td, 0);
}

/*
 * For all update functions, @q should be a valid pointer because these
 * update functions are called under blkcg_lock, that means, blkg is
 * valid and in turn @q is valid. queue exit path can not race because
 * of blkcg_lock
 *
 * Can not take queue lock in update functions as queue lock under blkcg_lock
 * is not allowed. Under other paths we take blkcg_lock under queue_lock.
 */
static void throtl_update_blkio_group_read_bps(struct request_queue *q,
				struct blkio_group *blkg, u64 read_bps)
{
	struct throtl_grp *tg = blkg_to_tg(blkg);

	tg->bps[READ] = read_bps;
	throtl_update_blkio_group_common(q->td, tg);
}

static void throtl_update_blkio_group_write_bps(struct request_queue *q,
				struct blkio_group *blkg, u64 write_bps)
{
	struct throtl_grp *tg = blkg_to_tg(blkg);

	tg->bps[WRITE] = write_bps;
	throtl_update_blkio_group_common(q->td, tg);
}

static void throtl_update_blkio_group_read_iops(struct request_queue *q,
			struct blkio_group *blkg, unsigned int read_iops)
{
	struct throtl_grp *tg = blkg_to_tg(blkg);

	tg->iops[READ] = read_iops;
	throtl_update_blkio_group_common(q->td, tg);
}

static void throtl_update_blkio_group_write_iops(struct request_queue *q,
			struct blkio_group *blkg, unsigned int write_iops)
{
	struct throtl_grp *tg = blkg_to_tg(blkg);

	tg->iops[WRITE] = write_iops;
	throtl_update_blkio_group_common(q->td, tg);
}

static void throtl_shutdown_wq(struct request_queue *q)
{
	struct throtl_data *td = q->td;

	cancel_delayed_work_sync(&td->throtl_work);
}

static struct blkio_policy_type blkio_policy_throtl = {
	.ops = {
		.blkio_init_group_fn = throtl_init_blkio_group,
		.blkio_link_group_fn = throtl_link_blkio_group,
		.blkio_unlink_group_fn = throtl_unlink_blkio_group,
		.blkio_clear_queue_fn = throtl_clear_queue,
		.blkio_update_group_read_bps_fn =
					throtl_update_blkio_group_read_bps,
		.blkio_update_group_write_bps_fn =
					throtl_update_blkio_group_write_bps,
		.blkio_update_group_read_iops_fn =
					throtl_update_blkio_group_read_iops,
		.blkio_update_group_write_iops_fn =
					throtl_update_blkio_group_write_iops,
	},
	.plid = BLKIO_POLICY_THROTL,
	.pdata_size = sizeof(struct throtl_grp),
};

bool blk_throtl_bio(struct request_queue *q, struct bio *bio)
{
	struct throtl_data *td = q->td;
	struct throtl_grp *tg;
	bool rw = bio_data_dir(bio), update_disptime = true;
	struct blkio_cgroup *blkcg;
	bool throttled = false;

	if (bio->bi_rw & REQ_THROTTLED) {
		bio->bi_rw &= ~REQ_THROTTLED;
		goto out;
	}

	/*
	 * A throtl_grp pointer retrieved under rcu can be used to access
	 * basic fields like stats and io rates. If a group has no rules,
	 * just update the dispatch stats in lockless manner and return.
	 */
	rcu_read_lock();
	blkcg = task_blkio_cgroup(current);
	tg = throtl_lookup_tg(td, blkcg);
	if (tg) {
		if (tg_no_rule_group(tg, rw)) {
			blkiocg_update_dispatch_stats(tg_to_blkg(tg),
						      bio->bi_size, rw,
						      rw_is_sync(bio->bi_rw));
			goto out_unlock_rcu;
		}
	}

	/*
	 * Either group has not been allocated yet or it is not an unlimited
	 * IO group
	 */
	spin_lock_irq(q->queue_lock);
	tg = throtl_lookup_create_tg(td, blkcg);
	if (unlikely(!tg))
		goto out_unlock;

	if (tg->nr_queued[rw]) {
		/*
		 * There is already another bio queued in same dir. No
		 * need to update dispatch time.
		 */
		update_disptime = false;
		goto queue_bio;

	}

	/* Bio is with-in rate limit of group */
	if (tg_may_dispatch(td, tg, bio, NULL)) {
		throtl_charge_bio(tg, bio);

		/*
		 * We need to trim slice even when bios are not being queued
		 * otherwise it might happen that a bio is not queued for
		 * a long time and slice keeps on extending and trim is not
		 * called for a long time. Now if limits are reduced suddenly
		 * we take into account all the IO dispatched so far at new
		 * low rate and * newly queued IO gets a really long dispatch
		 * time.
		 *
		 * So keep on trimming slice even if bio is not queued.
		 */
		throtl_trim_slice(td, tg, rw);
		goto out_unlock;
	}

queue_bio:
	throtl_log_tg(td, tg, "[%c] bio. bdisp=%llu sz=%u bps=%llu"
			" iodisp=%u iops=%u queued=%d/%d",
			rw == READ ? 'R' : 'W',
			tg->bytes_disp[rw], bio->bi_size, tg->bps[rw],
			tg->io_disp[rw], tg->iops[rw],
			tg->nr_queued[READ], tg->nr_queued[WRITE]);

	throtl_add_bio_tg(q->td, tg, bio);
	throttled = true;

	if (update_disptime) {
		tg_update_disptime(td, tg);
		throtl_schedule_next_dispatch(td);
	}

out_unlock:
	spin_unlock_irq(q->queue_lock);
out_unlock_rcu:
	rcu_read_unlock();
out:
	return throttled;
}

/**
 * blk_throtl_drain - drain throttled bios
 * @q: request_queue to drain throttled bios for
 *
 * Dispatch all currently throttled bios on @q through ->make_request_fn().
 */
void blk_throtl_drain(struct request_queue *q)
	__releases(q->queue_lock) __acquires(q->queue_lock)
{
	struct throtl_data *td = q->td;
	struct throtl_rb_root *st = &td->tg_service_tree;
	struct throtl_grp *tg;
	struct bio_list bl;
	struct bio *bio;

	WARN_ON_ONCE(!queue_is_locked(q));

	bio_list_init(&bl);

	while ((tg = throtl_rb_first(st))) {
		throtl_dequeue_tg(td, tg);

		while ((bio = bio_list_peek(&tg->bio_lists[READ])))
			tg_dispatch_one_bio(td, tg, bio_data_dir(bio), &bl);
		while ((bio = bio_list_peek(&tg->bio_lists[WRITE])))
			tg_dispatch_one_bio(td, tg, bio_data_dir(bio), &bl);
	}
	spin_unlock_irq(q->queue_lock);

	while ((bio = bio_list_pop(&bl)))
		generic_make_request(bio);

	spin_lock_irq(q->queue_lock);
}

int blk_throtl_init(struct request_queue *q)
{
	struct throtl_data *td;
	struct blkio_group *blkg;

	td = kzalloc_node(sizeof(*td), GFP_KERNEL, q->node);
	if (!td)
		return -ENOMEM;

	INIT_HLIST_HEAD(&td->tg_list);
	td->tg_service_tree = THROTL_RB_ROOT;
	td->limits_changed = false;
	INIT_DELAYED_WORK(&td->throtl_work, blk_throtl_work);

	q->td = td;
	td->queue = q;

	/* alloc and init root group. */
	rcu_read_lock();
	spin_lock_irq(q->queue_lock);

	blkg = blkg_lookup_create(&blkio_root_cgroup, q, BLKIO_POLICY_THROTL,
				  true);
	if (!IS_ERR(blkg))
		td->root_tg = blkg_to_tg(blkg);

	spin_unlock_irq(q->queue_lock);
	rcu_read_unlock();

	if (!td->root_tg) {
		kfree(td);
		return -ENOMEM;
	}
	return 0;
}

void blk_throtl_exit(struct request_queue *q)
{
	struct throtl_data *td = q->td;
	bool wait = false;

	BUG_ON(!td);

	throtl_shutdown_wq(q);

	spin_lock_irq(q->queue_lock);
	throtl_release_tgs(td, true);

	/* If there are other groups */
	if (td->nr_undestroyed_grps > 0)
		wait = true;

	spin_unlock_irq(q->queue_lock);

	/*
	 * Wait for tg_to_blkg(tg)->q accessors to exit their grace periods.
	 * Do this wait only if there are other undestroyed groups out
	 * there (other than root group). This can happen if cgroup deletion
	 * path claimed the responsibility of cleaning up a group before
	 * queue cleanup code get to the group.
	 *
	 * Do not call synchronize_rcu() unconditionally as there are drivers
	 * which create/delete request queue hundreds of times during scan/boot
	 * and synchronize_rcu() can take significant time and slow down boot.
	 */
	if (wait)
		synchronize_rcu();

	/*
	 * Just being safe to make sure after previous flush if some body did
	 * update limits through cgroup and another work got queued, cancel
	 * it.
	 */
	throtl_shutdown_wq(q);

	kfree(q->td);
}

static int __init throtl_init(void)
{
	kthrotld_workqueue = alloc_workqueue("kthrotld", WQ_MEM_RECLAIM, 0);
	if (!kthrotld_workqueue)
		panic("Failed to create kthrotld\n");

	blkio_policy_register(&blkio_policy_throtl);
	return 0;
}

module_init(throtl_init);
