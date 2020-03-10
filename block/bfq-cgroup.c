// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cgroups support for the BFQ I/O scheduler.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/cgroup.h>
#include <linux/elevator.h>
#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include <linux/sbitmap.h>
#include <linux/delay.h>

#include "bfq-iosched.h"

#ifdef CONFIG_BFQ_CGROUP_DEBUG
static int bfq_stat_init(struct bfq_stat *stat, gfp_t gfp)
{
	int ret;

	ret = percpu_counter_init(&stat->cpu_cnt, 0, gfp);
	if (ret)
		return ret;

	atomic64_set(&stat->aux_cnt, 0);
	return 0;
}

static void bfq_stat_exit(struct bfq_stat *stat)
{
	percpu_counter_destroy(&stat->cpu_cnt);
}

/**
 * bfq_stat_add - add a value to a bfq_stat
 * @stat: target bfq_stat
 * @val: value to add
 *
 * Add @val to @stat.  The caller must ensure that IRQ on the same CPU
 * don't re-enter this function for the same counter.
 */
static inline void bfq_stat_add(struct bfq_stat *stat, uint64_t val)
{
	percpu_counter_add_batch(&stat->cpu_cnt, val, BLKG_STAT_CPU_BATCH);
}

/**
 * bfq_stat_read - read the current value of a bfq_stat
 * @stat: bfq_stat to read
 */
static inline uint64_t bfq_stat_read(struct bfq_stat *stat)
{
	return percpu_counter_sum_positive(&stat->cpu_cnt);
}

/**
 * bfq_stat_reset - reset a bfq_stat
 * @stat: bfq_stat to reset
 */
static inline void bfq_stat_reset(struct bfq_stat *stat)
{
	percpu_counter_set(&stat->cpu_cnt, 0);
	atomic64_set(&stat->aux_cnt, 0);
}

/**
 * bfq_stat_add_aux - add a bfq_stat into another's aux count
 * @to: the destination bfq_stat
 * @from: the source
 *
 * Add @from's count including the aux one to @to's aux count.
 */
static inline void bfq_stat_add_aux(struct bfq_stat *to,
				     struct bfq_stat *from)
{
	atomic64_add(bfq_stat_read(from) + atomic64_read(&from->aux_cnt),
		     &to->aux_cnt);
}

/**
 * blkg_prfill_stat - prfill callback for bfq_stat
 * @sf: seq_file to print to
 * @pd: policy private data of interest
 * @off: offset to the bfq_stat in @pd
 *
 * prfill callback for printing a bfq_stat.
 */
static u64 blkg_prfill_stat(struct seq_file *sf, struct blkg_policy_data *pd,
		int off)
{
	return __blkg_prfill_u64(sf, pd, bfq_stat_read((void *)pd + off));
}

/* bfqg stats flags */
enum bfqg_stats_flags {
	BFQG_stats_waiting = 0,
	BFQG_stats_idling,
	BFQG_stats_empty,
};

#define BFQG_FLAG_FNS(name)						\
static void bfqg_stats_mark_##name(struct bfqg_stats *stats)	\
{									\
	stats->flags |= (1 << BFQG_stats_##name);			\
}									\
static void bfqg_stats_clear_##name(struct bfqg_stats *stats)	\
{									\
	stats->flags &= ~(1 << BFQG_stats_##name);			\
}									\
static int bfqg_stats_##name(struct bfqg_stats *stats)		\
{									\
	return (stats->flags & (1 << BFQG_stats_##name)) != 0;		\
}									\

BFQG_FLAG_FNS(waiting)
BFQG_FLAG_FNS(idling)
BFQG_FLAG_FNS(empty)
#undef BFQG_FLAG_FNS

/* This should be called with the scheduler lock held. */
static void bfqg_stats_update_group_wait_time(struct bfqg_stats *stats)
{
	u64 now;

	if (!bfqg_stats_waiting(stats))
		return;

	now = ktime_get_ns();
	if (now > stats->start_group_wait_time)
		bfq_stat_add(&stats->group_wait_time,
			      now - stats->start_group_wait_time);
	bfqg_stats_clear_waiting(stats);
}

/* This should be called with the scheduler lock held. */
static void bfqg_stats_set_start_group_wait_time(struct bfq_group *bfqg,
						 struct bfq_group *curr_bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_waiting(stats))
		return;
	if (bfqg == curr_bfqg)
		return;
	stats->start_group_wait_time = ktime_get_ns();
	bfqg_stats_mark_waiting(stats);
}

/* This should be called with the scheduler lock held. */
static void bfqg_stats_end_empty_time(struct bfqg_stats *stats)
{
	u64 now;

	if (!bfqg_stats_empty(stats))
		return;

	now = ktime_get_ns();
	if (now > stats->start_empty_time)
		bfq_stat_add(&stats->empty_time,
			      now - stats->start_empty_time);
	bfqg_stats_clear_empty(stats);
}

void bfqg_stats_update_dequeue(struct bfq_group *bfqg)
{
	bfq_stat_add(&bfqg->stats.dequeue, 1);
}

void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (blkg_rwstat_total(&stats->queued))
		return;

	/*
	 * group is already marked empty. This can happen if bfqq got new
	 * request in parent group and moved to this group while being added
	 * to service tree. Just ignore the event and move on.
	 */
	if (bfqg_stats_empty(stats))
		return;

	stats->start_empty_time = ktime_get_ns();
	bfqg_stats_mark_empty(stats);
}

void bfqg_stats_update_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_idling(stats)) {
		u64 now = ktime_get_ns();

		if (now > stats->start_idle_time)
			bfq_stat_add(&stats->idle_time,
				      now - stats->start_idle_time);
		bfqg_stats_clear_idling(stats);
	}
}

void bfqg_stats_set_start_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	stats->start_idle_time = ktime_get_ns();
	bfqg_stats_mark_idling(stats);
}

void bfqg_stats_update_avg_queue_size(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	bfq_stat_add(&stats->avg_queue_size_sum,
		      blkg_rwstat_total(&stats->queued));
	bfq_stat_add(&stats->avg_queue_size_samples, 1);
	bfqg_stats_update_group_wait_time(stats);
}

void bfqg_stats_update_io_add(struct bfq_group *bfqg, struct bfq_queue *bfqq,
			      unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.queued, op, 1);
	bfqg_stats_end_empty_time(&bfqg->stats);
	if (!(bfqq == ((struct bfq_data *)bfqg->bfqd)->in_service_queue))
		bfqg_stats_set_start_group_wait_time(bfqg, bfqq_group(bfqq));
}

void bfqg_stats_update_io_remove(struct bfq_group *bfqg, unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.queued, op, -1);
}

void bfqg_stats_update_io_merged(struct bfq_group *bfqg, unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.merged, op, 1);
}

void bfqg_stats_update_completion(struct bfq_group *bfqg, u64 start_time_ns,
				  u64 io_start_time_ns, unsigned int op)
{
	struct bfqg_stats *stats = &bfqg->stats;
	u64 now = ktime_get_ns();

	if (now > io_start_time_ns)
		blkg_rwstat_add(&stats->service_time, op,
				now - io_start_time_ns);
	if (io_start_time_ns > start_time_ns)
		blkg_rwstat_add(&stats->wait_time, op,
				io_start_time_ns - start_time_ns);
}

#else /* CONFIG_BFQ_CGROUP_DEBUG */

void bfqg_stats_update_io_add(struct bfq_group *bfqg, struct bfq_queue *bfqq,
			      unsigned int op) { }
void bfqg_stats_update_io_remove(struct bfq_group *bfqg, unsigned int op) { }
void bfqg_stats_update_io_merged(struct bfq_group *bfqg, unsigned int op) { }
void bfqg_stats_update_completion(struct bfq_group *bfqg, u64 start_time_ns,
				  u64 io_start_time_ns, unsigned int op) { }
void bfqg_stats_update_dequeue(struct bfq_group *bfqg) { }
void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg) { }
void bfqg_stats_update_idle_time(struct bfq_group *bfqg) { }
void bfqg_stats_set_start_idle_time(struct bfq_group *bfqg) { }
void bfqg_stats_update_avg_queue_size(struct bfq_group *bfqg) { }

#endif /* CONFIG_BFQ_CGROUP_DEBUG */

#ifdef CONFIG_BFQ_GROUP_IOSCHED

/*
 * blk-cgroup policy-related handlers
 * The following functions help in converting between blk-cgroup
 * internal structures and BFQ-specific structures.
 */

static struct bfq_group *pd_to_bfqg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct bfq_group, pd) : NULL;
}

struct blkcg_gq *bfqg_to_blkg(struct bfq_group *bfqg)
{
	return pd_to_blkg(&bfqg->pd);
}

static struct bfq_group *blkg_to_bfqg(struct blkcg_gq *blkg)
{
	return pd_to_bfqg(blkg_to_pd(blkg, &blkcg_policy_bfq));
}

/*
 * bfq_group handlers
 * The following functions help in navigating the bfq_group hierarchy
 * by allowing to find the parent of a bfq_group or the bfq_group
 * associated to a bfq_queue.
 */

static struct bfq_group *bfqg_parent(struct bfq_group *bfqg)
{
	struct blkcg_gq *pblkg = bfqg_to_blkg(bfqg)->parent;

	return pblkg ? blkg_to_bfqg(pblkg) : NULL;
}

struct bfq_group *bfqq_group(struct bfq_queue *bfqq)
{
	struct bfq_entity *group_entity = bfqq->entity.parent;

	return group_entity ? container_of(group_entity, struct bfq_group,
					   entity) :
			      bfqq->bfqd->root_group;
}

/*
 * The following two functions handle get and put of a bfq_group by
 * wrapping the related blk-cgroup hooks.
 */

static void bfqg_get(struct bfq_group *bfqg)
{
	bfqg->ref++;
}

static void bfqg_put(struct bfq_group *bfqg)
{
	bfqg->ref--;

	if (bfqg->ref == 0)
		kfree(bfqg);
}

void bfqg_and_blkg_get(struct bfq_group *bfqg)
{
	/* see comments in bfq_bic_update_cgroup for why refcounting bfqg */
	bfqg_get(bfqg);

	blkg_get(bfqg_to_blkg(bfqg));
}

void bfqg_and_blkg_put(struct bfq_group *bfqg)
{
	blkg_put(bfqg_to_blkg(bfqg));

	bfqg_put(bfqg);
}

void bfqg_stats_update_legacy_io(struct request_queue *q, struct request *rq)
{
	struct bfq_group *bfqg = blkg_to_bfqg(rq->bio->bi_blkg);

	if (!bfqg)
		return;

	blkg_rwstat_add(&bfqg->stats.bytes, rq->cmd_flags, blk_rq_bytes(rq));
	blkg_rwstat_add(&bfqg->stats.ios, rq->cmd_flags, 1);
}

/* @stats = 0 */
static void bfqg_stats_reset(struct bfqg_stats *stats)
{
#ifdef CONFIG_BFQ_CGROUP_DEBUG
	/* queued stats shouldn't be cleared */
	blkg_rwstat_reset(&stats->merged);
	blkg_rwstat_reset(&stats->service_time);
	blkg_rwstat_reset(&stats->wait_time);
	bfq_stat_reset(&stats->time);
	bfq_stat_reset(&stats->avg_queue_size_sum);
	bfq_stat_reset(&stats->avg_queue_size_samples);
	bfq_stat_reset(&stats->dequeue);
	bfq_stat_reset(&stats->group_wait_time);
	bfq_stat_reset(&stats->idle_time);
	bfq_stat_reset(&stats->empty_time);
#endif
}

/* @to += @from */
static void bfqg_stats_add_aux(struct bfqg_stats *to, struct bfqg_stats *from)
{
	if (!to || !from)
		return;

#ifdef CONFIG_BFQ_CGROUP_DEBUG
	/* queued stats shouldn't be cleared */
	blkg_rwstat_add_aux(&to->merged, &from->merged);
	blkg_rwstat_add_aux(&to->service_time, &from->service_time);
	blkg_rwstat_add_aux(&to->wait_time, &from->wait_time);
	bfq_stat_add_aux(&from->time, &from->time);
	bfq_stat_add_aux(&to->avg_queue_size_sum, &from->avg_queue_size_sum);
	bfq_stat_add_aux(&to->avg_queue_size_samples,
			  &from->avg_queue_size_samples);
	bfq_stat_add_aux(&to->dequeue, &from->dequeue);
	bfq_stat_add_aux(&to->group_wait_time, &from->group_wait_time);
	bfq_stat_add_aux(&to->idle_time, &from->idle_time);
	bfq_stat_add_aux(&to->empty_time, &from->empty_time);
#endif
}

/*
 * Transfer @bfqg's stats to its parent's aux counts so that the ancestors'
 * recursive stats can still account for the amount used by this bfqg after
 * it's gone.
 */
static void bfqg_stats_xfer_dead(struct bfq_group *bfqg)
{
	struct bfq_group *parent;

	if (!bfqg) /* root_group */
		return;

	parent = bfqg_parent(bfqg);

	lockdep_assert_held(&bfqg_to_blkg(bfqg)->q->queue_lock);

	if (unlikely(!parent))
		return;

	bfqg_stats_add_aux(&parent->stats, &bfqg->stats);
	bfqg_stats_reset(&bfqg->stats);
}

void bfq_init_entity(struct bfq_entity *entity, struct bfq_group *bfqg)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	if (bfqq) {
		bfqq->ioprio = bfqq->new_ioprio;
		bfqq->ioprio_class = bfqq->new_ioprio_class;
		/*
		 * Make sure that bfqg and its associated blkg do not
		 * disappear before entity.
		 */
		bfqg_and_blkg_get(bfqg);
	}
	entity->parent = bfqg->my_entity; /* NULL for root group */
	entity->sched_data = &bfqg->sched_data;
}

static void bfqg_stats_exit(struct bfqg_stats *stats)
{
	blkg_rwstat_exit(&stats->bytes);
	blkg_rwstat_exit(&stats->ios);
#ifdef CONFIG_BFQ_CGROUP_DEBUG
	blkg_rwstat_exit(&stats->merged);
	blkg_rwstat_exit(&stats->service_time);
	blkg_rwstat_exit(&stats->wait_time);
	blkg_rwstat_exit(&stats->queued);
	bfq_stat_exit(&stats->time);
	bfq_stat_exit(&stats->avg_queue_size_sum);
	bfq_stat_exit(&stats->avg_queue_size_samples);
	bfq_stat_exit(&stats->dequeue);
	bfq_stat_exit(&stats->group_wait_time);
	bfq_stat_exit(&stats->idle_time);
	bfq_stat_exit(&stats->empty_time);
#endif
}

static int bfqg_stats_init(struct bfqg_stats *stats, gfp_t gfp)
{
	if (blkg_rwstat_init(&stats->bytes, gfp) ||
	    blkg_rwstat_init(&stats->ios, gfp))
		return -ENOMEM;

#ifdef CONFIG_BFQ_CGROUP_DEBUG
	if (blkg_rwstat_init(&stats->merged, gfp) ||
	    blkg_rwstat_init(&stats->service_time, gfp) ||
	    blkg_rwstat_init(&stats->wait_time, gfp) ||
	    blkg_rwstat_init(&stats->queued, gfp) ||
	    bfq_stat_init(&stats->time, gfp) ||
	    bfq_stat_init(&stats->avg_queue_size_sum, gfp) ||
	    bfq_stat_init(&stats->avg_queue_size_samples, gfp) ||
	    bfq_stat_init(&stats->dequeue, gfp) ||
	    bfq_stat_init(&stats->group_wait_time, gfp) ||
	    bfq_stat_init(&stats->idle_time, gfp) ||
	    bfq_stat_init(&stats->empty_time, gfp)) {
		bfqg_stats_exit(stats);
		return -ENOMEM;
	}
#endif

	return 0;
}

static struct bfq_group_data *cpd_to_bfqgd(struct blkcg_policy_data *cpd)
{
	return cpd ? container_of(cpd, struct bfq_group_data, pd) : NULL;
}

static struct bfq_group_data *blkcg_to_bfqgd(struct blkcg *blkcg)
{
	return cpd_to_bfqgd(blkcg_to_cpd(blkcg, &blkcg_policy_bfq));
}

static struct blkcg_policy_data *bfq_cpd_alloc(gfp_t gfp)
{
	struct bfq_group_data *bgd;

	bgd = kzalloc(sizeof(*bgd), gfp);
	if (!bgd)
		return NULL;
	return &bgd->pd;
}

static void bfq_cpd_init(struct blkcg_policy_data *cpd)
{
	struct bfq_group_data *d = cpd_to_bfqgd(cpd);

	d->weight = cgroup_subsys_on_dfl(io_cgrp_subsys) ?
		CGROUP_WEIGHT_DFL : BFQ_WEIGHT_LEGACY_DFL;
}

static void bfq_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(cpd_to_bfqgd(cpd));
}

static struct blkg_policy_data *bfq_pd_alloc(gfp_t gfp, struct request_queue *q,
					     struct blkcg *blkcg)
{
	struct bfq_group *bfqg;

	bfqg = kzalloc_node(sizeof(*bfqg), gfp, q->node);
	if (!bfqg)
		return NULL;

	if (bfqg_stats_init(&bfqg->stats, gfp)) {
		kfree(bfqg);
		return NULL;
	}

	/* see comments in bfq_bic_update_cgroup for why refcounting */
	bfqg_get(bfqg);
	return &bfqg->pd;
}

static void bfq_pd_init(struct blkg_policy_data *pd)
{
	struct blkcg_gq *blkg = pd_to_blkg(pd);
	struct bfq_group *bfqg = blkg_to_bfqg(blkg);
	struct bfq_data *bfqd = blkg->q->elevator->elevator_data;
	struct bfq_entity *entity = &bfqg->entity;
	struct bfq_group_data *d = blkcg_to_bfqgd(blkg->blkcg);

	entity->orig_weight = entity->weight = entity->new_weight = d->weight;
	entity->my_sched_data = &bfqg->sched_data;
	bfqg->my_entity = entity; /*
				   * the root_group's will be set to NULL
				   * in bfq_init_queue()
				   */
	bfqg->bfqd = bfqd;
	bfqg->active_entities = 0;
	bfqg->rq_pos_tree = RB_ROOT;
}

static void bfq_pd_free(struct blkg_policy_data *pd)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	bfqg_stats_exit(&bfqg->stats);
	bfqg_put(bfqg);
}

static void bfq_pd_reset_stats(struct blkg_policy_data *pd)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	bfqg_stats_reset(&bfqg->stats);
}

static void bfq_group_set_parent(struct bfq_group *bfqg,
					struct bfq_group *parent)
{
	struct bfq_entity *entity;

	entity = &bfqg->entity;
	entity->parent = parent->my_entity;
	entity->sched_data = &parent->sched_data;
}

static struct bfq_group *bfq_lookup_bfqg(struct bfq_data *bfqd,
					 struct blkcg *blkcg)
{
	struct blkcg_gq *blkg;

	blkg = blkg_lookup(blkcg, bfqd->queue);
	if (likely(blkg))
		return blkg_to_bfqg(blkg);
	return NULL;
}

struct bfq_group *bfq_find_set_group(struct bfq_data *bfqd,
				     struct blkcg *blkcg)
{
	struct bfq_group *bfqg, *parent;
	struct bfq_entity *entity;

	bfqg = bfq_lookup_bfqg(bfqd, blkcg);

	if (unlikely(!bfqg))
		return NULL;

	/*
	 * Update chain of bfq_groups as we might be handling a leaf group
	 * which, along with some of its relatives, has not been hooked yet
	 * to the private hierarchy of BFQ.
	 */
	entity = &bfqg->entity;
	for_each_entity(entity) {
		struct bfq_group *curr_bfqg = container_of(entity,
						struct bfq_group, entity);
		if (curr_bfqg != bfqd->root_group) {
			parent = bfqg_parent(curr_bfqg);
			if (!parent)
				parent = bfqd->root_group;
			bfq_group_set_parent(curr_bfqg, parent);
		}
	}

	return bfqg;
}

/**
 * bfq_bfqq_move - migrate @bfqq to @bfqg.
 * @bfqd: queue descriptor.
 * @bfqq: the queue to move.
 * @bfqg: the group to move to.
 *
 * Move @bfqq to @bfqg, deactivating it from its old group and reactivating
 * it on the new one.  Avoid putting the entity on the old group idle tree.
 *
 * Must be called under the scheduler lock, to make sure that the blkg
 * owning @bfqg does not disappear (see comments in
 * bfq_bic_update_cgroup on guaranteeing the consistency of blkg
 * objects).
 */
void bfq_bfqq_move(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		   struct bfq_group *bfqg)
{
	struct bfq_entity *entity = &bfqq->entity;

	/* If bfqq is empty, then bfq_bfqq_expire also invokes
	 * bfq_del_bfqq_busy, thereby removing bfqq and its entity
	 * from data structures related to current group. Otherwise we
	 * need to remove bfqq explicitly with bfq_deactivate_bfqq, as
	 * we do below.
	 */
	if (bfqq == bfqd->in_service_queue)
		bfq_bfqq_expire(bfqd, bfqd->in_service_queue,
				false, BFQQE_PREEMPTED);

	/*
	 * get extra reference to prevent bfqq from being freed in
	 * next possible deactivate
	 */
	bfqq->ref++;

	if (bfq_bfqq_busy(bfqq))
		bfq_deactivate_bfqq(bfqd, bfqq, false, false);
	else if (entity->on_st_or_in_serv)
		bfq_put_idle_entity(bfq_entity_service_tree(entity), entity);
	bfqg_and_blkg_put(bfqq_group(bfqq));

	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;
	/* pin down bfqg and its associated blkg  */
	bfqg_and_blkg_get(bfqg);

	if (bfq_bfqq_busy(bfqq)) {
		if (unlikely(!bfqd->nonrot_with_queueing))
			bfq_pos_tree_add_move(bfqd, bfqq);
		bfq_activate_bfqq(bfqd, bfqq);
	}

	if (!bfqd->in_service_queue && !bfqd->rq_in_driver)
		bfq_schedule_dispatch(bfqd);
	/* release extra ref taken above */
	bfq_put_queue(bfqq);
}

/**
 * __bfq_bic_change_cgroup - move @bic to @cgroup.
 * @bfqd: the queue descriptor.
 * @bic: the bic to move.
 * @blkcg: the blk-cgroup to move to.
 *
 * Move bic to blkcg, assuming that bfqd->lock is held; which makes
 * sure that the reference to cgroup is valid across the call (see
 * comments in bfq_bic_update_cgroup on this issue)
 *
 * NOTE: an alternative approach might have been to store the current
 * cgroup in bfqq and getting a reference to it, reducing the lookup
 * time here, at the price of slightly more complex code.
 */
static struct bfq_group *__bfq_bic_change_cgroup(struct bfq_data *bfqd,
						struct bfq_io_cq *bic,
						struct blkcg *blkcg)
{
	struct bfq_queue *async_bfqq = bic_to_bfqq(bic, 0);
	struct bfq_queue *sync_bfqq = bic_to_bfqq(bic, 1);
	struct bfq_group *bfqg;
	struct bfq_entity *entity;

	bfqg = bfq_find_set_group(bfqd, blkcg);

	if (unlikely(!bfqg))
		bfqg = bfqd->root_group;

	if (async_bfqq) {
		entity = &async_bfqq->entity;

		if (entity->sched_data != &bfqg->sched_data) {
			bic_set_bfqq(bic, NULL, 0);
			bfq_log_bfqq(bfqd, async_bfqq,
				     "bic_change_group: %p %d",
				     async_bfqq, async_bfqq->ref);
			bfq_put_queue(async_bfqq);
		}
	}

	if (sync_bfqq) {
		entity = &sync_bfqq->entity;
		if (entity->sched_data != &bfqg->sched_data)
			bfq_bfqq_move(bfqd, sync_bfqq, bfqg);
	}

	return bfqg;
}

void bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	struct bfq_group *bfqg = NULL;
	uint64_t serial_nr;

	rcu_read_lock();
	serial_nr = __bio_blkcg(bio)->css.serial_nr;

	/*
	 * Check whether blkcg has changed.  The condition may trigger
	 * spuriously on a newly created cic but there's no harm.
	 */
	if (unlikely(!bfqd) || likely(bic->blkcg_serial_nr == serial_nr))
		goto out;

	bfqg = __bfq_bic_change_cgroup(bfqd, bic, __bio_blkcg(bio));
	/*
	 * Update blkg_path for bfq_log_* functions. We cache this
	 * path, and update it here, for the following
	 * reasons. Operations on blkg objects in blk-cgroup are
	 * protected with the request_queue lock, and not with the
	 * lock that protects the instances of this scheduler
	 * (bfqd->lock). This exposes BFQ to the following sort of
	 * race.
	 *
	 * The blkg_lookup performed in bfq_get_queue, protected
	 * through rcu, may happen to return the address of a copy of
	 * the original blkg. If this is the case, then the
	 * bfqg_and_blkg_get performed in bfq_get_queue, to pin down
	 * the blkg, is useless: it does not prevent blk-cgroup code
	 * from destroying both the original blkg and all objects
	 * directly or indirectly referred by the copy of the
	 * blkg.
	 *
	 * On the bright side, destroy operations on a blkg invoke, as
	 * a first step, hooks of the scheduler associated with the
	 * blkg. And these hooks are executed with bfqd->lock held for
	 * BFQ. As a consequence, for any blkg associated with the
	 * request queue this instance of the scheduler is attached
	 * to, we are guaranteed that such a blkg is not destroyed, and
	 * that all the pointers it contains are consistent, while we
	 * are holding bfqd->lock. A blkg_lookup performed with
	 * bfqd->lock held then returns a fully consistent blkg, which
	 * remains consistent until this lock is held.
	 *
	 * Thanks to the last fact, and to the fact that: (1) bfqg has
	 * been obtained through a blkg_lookup in the above
	 * assignment, and (2) bfqd->lock is being held, here we can
	 * safely use the policy data for the involved blkg (i.e., the
	 * field bfqg->pd) to get to the blkg associated with bfqg,
	 * and then we can safely use any field of blkg. After we
	 * release bfqd->lock, even just getting blkg through this
	 * bfqg may cause dangling references to be traversed, as
	 * bfqg->pd may not exist any more.
	 *
	 * In view of the above facts, here we cache, in the bfqg, any
	 * blkg data we may need for this bic, and for its associated
	 * bfq_queue. As of now, we need to cache only the path of the
	 * blkg, which is used in the bfq_log_* functions.
	 *
	 * Finally, note that bfqg itself needs to be protected from
	 * destruction on the blkg_free of the original blkg (which
	 * invokes bfq_pd_free). We use an additional private
	 * refcounter for bfqg, to let it disappear only after no
	 * bfq_queue refers to it any longer.
	 */
	blkg_path(bfqg_to_blkg(bfqg), bfqg->blkg_path, sizeof(bfqg->blkg_path));
	bic->blkcg_serial_nr = serial_nr;
out:
	rcu_read_unlock();
}

/**
 * bfq_flush_idle_tree - deactivate any entity on the idle tree of @st.
 * @st: the service tree being flushed.
 */
static void bfq_flush_idle_tree(struct bfq_service_tree *st)
{
	struct bfq_entity *entity = st->first_idle;

	for (; entity ; entity = st->first_idle)
		__bfq_deactivate_entity(entity, false);
}

/**
 * bfq_reparent_leaf_entity - move leaf entity to the root_group.
 * @bfqd: the device data structure with the root group.
 * @entity: the entity to move.
 */
static void bfq_reparent_leaf_entity(struct bfq_data *bfqd,
				     struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	bfq_bfqq_move(bfqd, bfqq, bfqd->root_group);
}

/**
 * bfq_reparent_active_entities - move to the root group all active
 *                                entities.
 * @bfqd: the device data structure with the root group.
 * @bfqg: the group to move from.
 * @st: the service tree with the entities.
 */
static void bfq_reparent_active_entities(struct bfq_data *bfqd,
					 struct bfq_group *bfqg,
					 struct bfq_service_tree *st)
{
	struct rb_root *active = &st->active;
	struct bfq_entity *entity = NULL;

	if (!RB_EMPTY_ROOT(&st->active))
		entity = bfq_entity_of(rb_first(active));

	for (; entity ; entity = bfq_entity_of(rb_first(active)))
		bfq_reparent_leaf_entity(bfqd, entity);

	if (bfqg->sched_data.in_service_entity)
		bfq_reparent_leaf_entity(bfqd,
			bfqg->sched_data.in_service_entity);
}

/**
 * bfq_pd_offline - deactivate the entity associated with @pd,
 *		    and reparent its children entities.
 * @pd: descriptor of the policy going offline.
 *
 * blkio already grabs the queue_lock for us, so no need to use
 * RCU-based magic
 */
static void bfq_pd_offline(struct blkg_policy_data *pd)
{
	struct bfq_service_tree *st;
	struct bfq_group *bfqg = pd_to_bfqg(pd);
	struct bfq_data *bfqd = bfqg->bfqd;
	struct bfq_entity *entity = bfqg->my_entity;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&bfqd->lock, flags);

	if (!entity) /* root group */
		goto put_async_queues;

	/*
	 * Empty all service_trees belonging to this group before
	 * deactivating the group itself.
	 */
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++) {
		st = bfqg->sched_data.service_tree + i;

		/*
		 * The idle tree may still contain bfq_queues belonging
		 * to exited task because they never migrated to a different
		 * cgroup from the one being destroyed now.
		 */
		bfq_flush_idle_tree(st);

		/*
		 * It may happen that some queues are still active
		 * (busy) upon group destruction (if the corresponding
		 * processes have been forced to terminate). We move
		 * all the leaf entities corresponding to these queues
		 * to the root_group.
		 * Also, it may happen that the group has an entity
		 * in service, which is disconnected from the active
		 * tree: it must be moved, too.
		 * There is no need to put the sync queues, as the
		 * scheduler has taken no reference.
		 */
		bfq_reparent_active_entities(bfqd, bfqg, st);
	}

	__bfq_deactivate_entity(entity, false);

put_async_queues:
	bfq_put_async_queues(bfqd, bfqg);

	spin_unlock_irqrestore(&bfqd->lock, flags);
	/*
	 * @blkg is going offline and will be ignored by
	 * blkg_[rw]stat_recursive_sum().  Transfer stats to the parent so
	 * that they don't get lost.  If IOs complete after this point, the
	 * stats for them will be lost.  Oh well...
	 */
	bfqg_stats_xfer_dead(bfqg);
}

void bfq_end_wr_async(struct bfq_data *bfqd)
{
	struct blkcg_gq *blkg;

	list_for_each_entry(blkg, &bfqd->queue->blkg_list, q_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);

		bfq_end_wr_async_queues(bfqd, bfqg);
	}
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

static int bfq_io_show_weight_legacy(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	unsigned int val = 0;

	if (bfqgd)
		val = bfqgd->weight;

	seq_printf(sf, "%u\n", val);

	return 0;
}

static u64 bfqg_prfill_weight_device(struct seq_file *sf,
				     struct blkg_policy_data *pd, int off)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	if (!bfqg->entity.dev_weight)
		return 0;
	return __blkg_prfill_u64(sf, pd, bfqg->entity.dev_weight);
}

static int bfq_io_show_weight(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);

	seq_printf(sf, "default %u\n", bfqgd->weight);
	blkcg_print_blkgs(sf, blkcg, bfqg_prfill_weight_device,
			  &blkcg_policy_bfq, 0, false);
	return 0;
}

static void bfq_group_set_weight(struct bfq_group *bfqg, u64 weight, u64 dev_weight)
{
	weight = dev_weight ?: weight;

	bfqg->entity.dev_weight = dev_weight;
	/*
	 * Setting the prio_changed flag of the entity
	 * to 1 with new_weight == weight would re-set
	 * the value of the weight to its ioprio mapping.
	 * Set the flag only if necessary.
	 */
	if ((unsigned short)weight != bfqg->entity.new_weight) {
		bfqg->entity.new_weight = (unsigned short)weight;
		/*
		 * Make sure that the above new value has been
		 * stored in bfqg->entity.new_weight before
		 * setting the prio_changed flag. In fact,
		 * this flag may be read asynchronously (in
		 * critical sections protected by a different
		 * lock than that held here), and finding this
		 * flag set may cause the execution of the code
		 * for updating parameters whose value may
		 * depend also on bfqg->entity.new_weight (in
		 * __bfq_entity_update_weight_prio).
		 * This barrier makes sure that the new value
		 * of bfqg->entity.new_weight is correctly
		 * seen in that code.
		 */
		smp_wmb();
		bfqg->entity.prio_changed = 1;
	}
}

static int bfq_io_set_weight_legacy(struct cgroup_subsys_state *css,
				    struct cftype *cftype,
				    u64 val)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	struct blkcg_gq *blkg;
	int ret = -ERANGE;

	if (val < BFQ_MIN_WEIGHT || val > BFQ_MAX_WEIGHT)
		return ret;

	ret = 0;
	spin_lock_irq(&blkcg->lock);
	bfqgd->weight = (unsigned short)val;
	hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);

		if (bfqg)
			bfq_group_set_weight(bfqg, val, 0);
	}
	spin_unlock_irq(&blkcg->lock);

	return ret;
}

static ssize_t bfq_io_set_device_weight(struct kernfs_open_file *of,
					char *buf, size_t nbytes,
					loff_t off)
{
	int ret;
	struct blkg_conf_ctx ctx;
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	struct bfq_group *bfqg;
	u64 v;

	ret = blkg_conf_prep(blkcg, &blkcg_policy_bfq, buf, &ctx);
	if (ret)
		return ret;

	if (sscanf(ctx.body, "%llu", &v) == 1) {
		/* require "default" on dfl */
		ret = -ERANGE;
		if (!v)
			goto out;
	} else if (!strcmp(strim(ctx.body), "default")) {
		v = 0;
	} else {
		ret = -EINVAL;
		goto out;
	}

	bfqg = blkg_to_bfqg(ctx.blkg);

	ret = -ERANGE;
	if (!v || (v >= BFQ_MIN_WEIGHT && v <= BFQ_MAX_WEIGHT)) {
		bfq_group_set_weight(bfqg, bfqg->entity.weight, v);
		ret = 0;
	}
out:
	blkg_conf_finish(&ctx);
	return ret ?: nbytes;
}

static ssize_t bfq_io_set_weight(struct kernfs_open_file *of,
				 char *buf, size_t nbytes,
				 loff_t off)
{
	char *endp;
	int ret;
	u64 v;

	buf = strim(buf);

	/* "WEIGHT" or "default WEIGHT" sets the default weight */
	v = simple_strtoull(buf, &endp, 0);
	if (*endp == '\0' || sscanf(buf, "default %llu", &v) == 1) {
		ret = bfq_io_set_weight_legacy(of_css(of), NULL, v);
		return ret ?: nbytes;
	}

	return bfq_io_set_device_weight(of, buf, nbytes, off);
}

static int bfqg_print_rwstat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_rwstat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, true);
	return 0;
}

static u64 bfqg_prfill_rwstat_recursive(struct seq_file *sf,
					struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat_sample sum;

	blkg_rwstat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_bfq, off, &sum);
	return __blkg_prfill_rwstat(sf, pd, &sum);
}

static int bfqg_print_rwstat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_rwstat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, true);
	return 0;
}

#ifdef CONFIG_BFQ_CGROUP_DEBUG
static int bfqg_print_stat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_stat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, false);
	return 0;
}

static u64 bfqg_prfill_stat_recursive(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct blkcg_gq *blkg = pd_to_blkg(pd);
	struct blkcg_gq *pos_blkg;
	struct cgroup_subsys_state *pos_css;
	u64 sum = 0;

	lockdep_assert_held(&blkg->q->queue_lock);

	rcu_read_lock();
	blkg_for_each_descendant_pre(pos_blkg, pos_css, blkg) {
		struct bfq_stat *stat;

		if (!pos_blkg->online)
			continue;

		stat = (void *)blkg_to_pd(pos_blkg, &blkcg_policy_bfq) + off;
		sum += bfq_stat_read(stat) + atomic64_read(&stat->aux_cnt);
	}
	rcu_read_unlock();

	return __blkg_prfill_u64(sf, pd, sum);
}

static int bfqg_print_stat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_stat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, false);
	return 0;
}

static u64 bfqg_prfill_sectors(struct seq_file *sf, struct blkg_policy_data *pd,
			       int off)
{
	struct bfq_group *bfqg = blkg_to_bfqg(pd->blkg);
	u64 sum = blkg_rwstat_total(&bfqg->stats.bytes);

	return __blkg_prfill_u64(sf, pd, sum >> 9);
}

static int bfqg_print_stat_sectors(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_sectors, &blkcg_policy_bfq, 0, false);
	return 0;
}

static u64 bfqg_prfill_sectors_recursive(struct seq_file *sf,
					 struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat_sample tmp;

	blkg_rwstat_recursive_sum(pd->blkg, &blkcg_policy_bfq,
			offsetof(struct bfq_group, stats.bytes), &tmp);

	return __blkg_prfill_u64(sf, pd,
		(tmp.cnt[BLKG_RWSTAT_READ] + tmp.cnt[BLKG_RWSTAT_WRITE]) >> 9);
}

static int bfqg_print_stat_sectors_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_sectors_recursive, &blkcg_policy_bfq, 0,
			  false);
	return 0;
}

static u64 bfqg_prfill_avg_queue_size(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);
	u64 samples = bfq_stat_read(&bfqg->stats.avg_queue_size_samples);
	u64 v = 0;

	if (samples) {
		v = bfq_stat_read(&bfqg->stats.avg_queue_size_sum);
		v = div64_u64(v, samples);
	}
	__blkg_prfill_u64(sf, pd, v);
	return 0;
}

/* print avg_queue_size */
static int bfqg_print_avg_queue_size(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_avg_queue_size, &blkcg_policy_bfq,
			  0, false);
	return 0;
}
#endif /* CONFIG_BFQ_CGROUP_DEBUG */

struct bfq_group *bfq_create_group_hierarchy(struct bfq_data *bfqd, int node)
{
	int ret;

	ret = blkcg_activate_policy(bfqd->queue, &blkcg_policy_bfq);
	if (ret)
		return NULL;

	return blkg_to_bfqg(bfqd->queue->root_blkg);
}

struct blkcg_policy blkcg_policy_bfq = {
	.dfl_cftypes		= bfq_blkg_files,
	.legacy_cftypes		= bfq_blkcg_legacy_files,

	.cpd_alloc_fn		= bfq_cpd_alloc,
	.cpd_init_fn		= bfq_cpd_init,
	.cpd_bind_fn	        = bfq_cpd_init,
	.cpd_free_fn		= bfq_cpd_free,

	.pd_alloc_fn		= bfq_pd_alloc,
	.pd_init_fn		= bfq_pd_init,
	.pd_offline_fn		= bfq_pd_offline,
	.pd_free_fn		= bfq_pd_free,
	.pd_reset_stats_fn	= bfq_pd_reset_stats,
};

struct cftype bfq_blkcg_legacy_files[] = {
	{
		.name = "bfq.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfq_io_show_weight_legacy,
		.write_u64 = bfq_io_set_weight_legacy,
	},
	{
		.name = "bfq.weight_device",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfq_io_show_weight,
		.write = bfq_io_set_weight,
	},

	/* statistics, covers only the tasks in the bfqg */
	{
		.name = "bfq.io_service_bytes",
		.private = offsetof(struct bfq_group, stats.bytes),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_serviced",
		.private = offsetof(struct bfq_group, stats.ios),
		.seq_show = bfqg_print_rwstat,
	},
#ifdef CONFIG_BFQ_CGROUP_DEBUG
	{
		.name = "bfq.time",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.sectors",
		.seq_show = bfqg_print_stat_sectors,
	},
	{
		.name = "bfq.io_service_time",
		.private = offsetof(struct bfq_group, stats.service_time),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_wait_time",
		.private = offsetof(struct bfq_group, stats.wait_time),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_merged",
		.private = offsetof(struct bfq_group, stats.merged),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_queued",
		.private = offsetof(struct bfq_group, stats.queued),
		.seq_show = bfqg_print_rwstat,
	},
#endif /* CONFIG_BFQ_CGROUP_DEBUG */

	/* the same statistics which cover the bfqg and its descendants */
	{
		.name = "bfq.io_service_bytes_recursive",
		.private = offsetof(struct bfq_group, stats.bytes),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_serviced_recursive",
		.private = offsetof(struct bfq_group, stats.ios),
		.seq_show = bfqg_print_rwstat_recursive,
	},
#ifdef CONFIG_BFQ_CGROUP_DEBUG
	{
		.name = "bfq.time_recursive",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat_recursive,
	},
	{
		.name = "bfq.sectors_recursive",
		.seq_show = bfqg_print_stat_sectors_recursive,
	},
	{
		.name = "bfq.io_service_time_recursive",
		.private = offsetof(struct bfq_group, stats.service_time),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_wait_time_recursive",
		.private = offsetof(struct bfq_group, stats.wait_time),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_merged_recursive",
		.private = offsetof(struct bfq_group, stats.merged),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_queued_recursive",
		.private = offsetof(struct bfq_group, stats.queued),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.avg_queue_size",
		.seq_show = bfqg_print_avg_queue_size,
	},
	{
		.name = "bfq.group_wait_time",
		.private = offsetof(struct bfq_group, stats.group_wait_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.idle_time",
		.private = offsetof(struct bfq_group, stats.idle_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.empty_time",
		.private = offsetof(struct bfq_group, stats.empty_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.dequeue",
		.private = offsetof(struct bfq_group, stats.dequeue),
		.seq_show = bfqg_print_stat,
	},
#endif	/* CONFIG_BFQ_CGROUP_DEBUG */
	{ }	/* terminate */
};

struct cftype bfq_blkg_files[] = {
	{
		.name = "bfq.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfq_io_show_weight,
		.write = bfq_io_set_weight,
	},
	{} /* terminate */
};

#else	/* CONFIG_BFQ_GROUP_IOSCHED */

void bfq_bfqq_move(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		   struct bfq_group *bfqg) {}

void bfq_init_entity(struct bfq_entity *entity, struct bfq_group *bfqg)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	if (bfqq) {
		bfqq->ioprio = bfqq->new_ioprio;
		bfqq->ioprio_class = bfqq->new_ioprio_class;
	}
	entity->sched_data = &bfqg->sched_data;
}

void bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio) {}

void bfq_end_wr_async(struct bfq_data *bfqd)
{
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

struct bfq_group *bfq_find_set_group(struct bfq_data *bfqd, struct blkcg *blkcg)
{
	return bfqd->root_group;
}

struct bfq_group *bfqq_group(struct bfq_queue *bfqq)
{
	return bfqq->bfqd->root_group;
}

void bfqg_and_blkg_get(struct bfq_group *bfqg) {}

void bfqg_and_blkg_put(struct bfq_group *bfqg) {}

struct bfq_group *bfq_create_group_hierarchy(struct bfq_data *bfqd, int node)
{
	struct bfq_group *bfqg;
	int i;

	bfqg = kmalloc_node(sizeof(*bfqg), GFP_KERNEL | __GFP_ZERO, node);
	if (!bfqg)
		return NULL;

	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		bfqg->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;

	return bfqg;
}
#endif	/* CONFIG_BFQ_GROUP_IOSCHED */
