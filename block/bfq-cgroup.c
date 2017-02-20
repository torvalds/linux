/*
 * BFQ: CGROUPS support.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 *
 * Licensed under the GPL-2 as detailed in the accompanying COPYING.BFQ
 * file.
 */

#ifdef CONFIG_BFQ_GROUP_IOSCHED

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

/* This should be called with the queue_lock held. */
static void bfqg_stats_update_group_wait_time(struct bfqg_stats *stats)
{
	unsigned long long now;

	if (!bfqg_stats_waiting(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_group_wait_time))
		blkg_stat_add(&stats->group_wait_time,
			      now - stats->start_group_wait_time);
	bfqg_stats_clear_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void bfqg_stats_set_start_group_wait_time(struct bfq_group *bfqg,
						 struct bfq_group *curr_bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_waiting(stats))
		return;
	if (bfqg == curr_bfqg)
		return;
	stats->start_group_wait_time = sched_clock();
	bfqg_stats_mark_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void bfqg_stats_end_empty_time(struct bfqg_stats *stats)
{
	unsigned long long now;

	if (!bfqg_stats_empty(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_empty_time))
		blkg_stat_add(&stats->empty_time,
			      now - stats->start_empty_time);
	bfqg_stats_clear_empty(stats);
}

static void bfqg_stats_update_dequeue(struct bfq_group *bfqg)
{
	blkg_stat_add(&bfqg->stats.dequeue, 1);
}

static void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg)
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

	stats->start_empty_time = sched_clock();
	bfqg_stats_mark_empty(stats);
}

static void bfqg_stats_update_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_idling(stats)) {
		unsigned long long now = sched_clock();

		if (time_after64(now, stats->start_idle_time))
			blkg_stat_add(&stats->idle_time,
				      now - stats->start_idle_time);
		bfqg_stats_clear_idling(stats);
	}
}

static void bfqg_stats_set_start_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	stats->start_idle_time = sched_clock();
	bfqg_stats_mark_idling(stats);
}

static void bfqg_stats_update_avg_queue_size(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	blkg_stat_add(&stats->avg_queue_size_sum,
		      blkg_rwstat_total(&stats->queued));
	blkg_stat_add(&stats->avg_queue_size_samples, 1);
	bfqg_stats_update_group_wait_time(stats);
}

static struct blkcg_policy blkcg_policy_bfq;

/*
 * blk-cgroup policy-related handlers
 * The following functions help in converting between blk-cgroup
 * internal structures and BFQ-specific structures.
 */

static struct bfq_group *pd_to_bfqg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct bfq_group, pd) : NULL;
}

static struct blkcg_gq *bfqg_to_blkg(struct bfq_group *bfqg)
{
	return pd_to_blkg(&bfqg->pd);
}

static struct bfq_group *blkg_to_bfqg(struct blkcg_gq *blkg)
{
	struct blkg_policy_data *pd = blkg_to_pd(blkg, &blkcg_policy_bfq);
	BUG_ON(!pd);
	return pd_to_bfqg(pd);
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

static struct bfq_group *bfqq_group(struct bfq_queue *bfqq)
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
	return blkg_get(bfqg_to_blkg(bfqg));
}

static void bfqg_put(struct bfq_group *bfqg)
{
	return blkg_put(bfqg_to_blkg(bfqg));
}

static void bfqg_stats_update_io_add(struct bfq_group *bfqg,
				     struct bfq_queue *bfqq,
				     int rw)
{
	blkg_rwstat_add(&bfqg->stats.queued, rw, 1);
	bfqg_stats_end_empty_time(&bfqg->stats);
	if (!(bfqq == ((struct bfq_data *)bfqg->bfqd)->in_service_queue))
		bfqg_stats_set_start_group_wait_time(bfqg, bfqq_group(bfqq));
}

static void bfqg_stats_update_io_remove(struct bfq_group *bfqg, int rw)
{
	blkg_rwstat_add(&bfqg->stats.queued, rw, -1);
}

static void bfqg_stats_update_io_merged(struct bfq_group *bfqg, int rw)
{
	blkg_rwstat_add(&bfqg->stats.merged, rw, 1);
}

static void bfqg_stats_update_dispatch(struct bfq_group *bfqg,
					      uint64_t bytes, int rw)
{
	blkg_stat_add(&bfqg->stats.sectors, bytes >> 9);
	blkg_rwstat_add(&bfqg->stats.serviced, rw, 1);
	blkg_rwstat_add(&bfqg->stats.service_bytes, rw, bytes);
}

static void bfqg_stats_update_completion(struct bfq_group *bfqg,
			uint64_t start_time, uint64_t io_start_time, int rw)
{
	struct bfqg_stats *stats = &bfqg->stats;
	unsigned long long now = sched_clock();

	if (time_after64(now, io_start_time))
		blkg_rwstat_add(&stats->service_time, rw, now - io_start_time);
	if (time_after64(io_start_time, start_time))
		blkg_rwstat_add(&stats->wait_time, rw,
				io_start_time - start_time);
}

/* @stats = 0 */
static void bfqg_stats_reset(struct bfqg_stats *stats)
{
	if (!stats)
		return;

	/* queued stats shouldn't be cleared */
	blkg_rwstat_reset(&stats->service_bytes);
	blkg_rwstat_reset(&stats->serviced);
	blkg_rwstat_reset(&stats->merged);
	blkg_rwstat_reset(&stats->service_time);
	blkg_rwstat_reset(&stats->wait_time);
	blkg_stat_reset(&stats->time);
	blkg_stat_reset(&stats->unaccounted_time);
	blkg_stat_reset(&stats->avg_queue_size_sum);
	blkg_stat_reset(&stats->avg_queue_size_samples);
	blkg_stat_reset(&stats->dequeue);
	blkg_stat_reset(&stats->group_wait_time);
	blkg_stat_reset(&stats->idle_time);
	blkg_stat_reset(&stats->empty_time);
}

/* @to += @from */
static void bfqg_stats_merge(struct bfqg_stats *to, struct bfqg_stats *from)
{
	if (!to || !from)
		return;

	/* queued stats shouldn't be cleared */
	blkg_rwstat_add_aux(&to->service_bytes, &from->service_bytes);
	blkg_rwstat_add_aux(&to->serviced, &from->serviced);
	blkg_rwstat_add_aux(&to->merged, &from->merged);
	blkg_rwstat_add_aux(&to->service_time, &from->service_time);
	blkg_rwstat_add_aux(&to->wait_time, &from->wait_time);
	blkg_stat_add_aux(&from->time, &from->time);
	blkg_stat_add_aux(&to->unaccounted_time, &from->unaccounted_time);
	blkg_stat_add_aux(&to->avg_queue_size_sum, &from->avg_queue_size_sum);
	blkg_stat_add_aux(&to->avg_queue_size_samples, &from->avg_queue_size_samples);
	blkg_stat_add_aux(&to->dequeue, &from->dequeue);
	blkg_stat_add_aux(&to->group_wait_time, &from->group_wait_time);
	blkg_stat_add_aux(&to->idle_time, &from->idle_time);
	blkg_stat_add_aux(&to->empty_time, &from->empty_time);
}

/*
 * Transfer @bfqg's stats to its parent's dead_stats so that the ancestors'
 * recursive stats can still account for the amount used by this bfqg after
 * it's gone.
 */
static void bfqg_stats_xfer_dead(struct bfq_group *bfqg)
{
	struct bfq_group *parent;

	if (!bfqg) /* root_group */
		return;

	parent = bfqg_parent(bfqg);

	lockdep_assert_held(bfqg_to_blkg(bfqg)->q->queue_lock);

	if (unlikely(!parent))
		return;

	bfqg_stats_merge(&parent->dead_stats, &bfqg->stats);
	bfqg_stats_merge(&parent->dead_stats, &bfqg->dead_stats);
	bfqg_stats_reset(&bfqg->stats);
	bfqg_stats_reset(&bfqg->dead_stats);
}

static void bfq_init_entity(struct bfq_entity *entity,
			    struct bfq_group *bfqg)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	if (bfqq) {
		bfqq->ioprio = bfqq->new_ioprio;
		bfqq->ioprio_class = bfqq->new_ioprio_class;
		bfqg_get(bfqg);
	}
	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;
}

static void bfqg_stats_exit(struct bfqg_stats *stats)
{
	blkg_rwstat_exit(&stats->service_bytes);
	blkg_rwstat_exit(&stats->serviced);
	blkg_rwstat_exit(&stats->merged);
	blkg_rwstat_exit(&stats->service_time);
	blkg_rwstat_exit(&stats->wait_time);
	blkg_rwstat_exit(&stats->queued);
	blkg_stat_exit(&stats->sectors);
	blkg_stat_exit(&stats->time);
	blkg_stat_exit(&stats->unaccounted_time);
	blkg_stat_exit(&stats->avg_queue_size_sum);
	blkg_stat_exit(&stats->avg_queue_size_samples);
	blkg_stat_exit(&stats->dequeue);
	blkg_stat_exit(&stats->group_wait_time);
	blkg_stat_exit(&stats->idle_time);
	blkg_stat_exit(&stats->empty_time);
}

static int bfqg_stats_init(struct bfqg_stats *stats, gfp_t gfp)
{
	if (blkg_rwstat_init(&stats->service_bytes, gfp) ||
	    blkg_rwstat_init(&stats->serviced, gfp) ||
	    blkg_rwstat_init(&stats->merged, gfp) ||
	    blkg_rwstat_init(&stats->service_time, gfp) ||
	    blkg_rwstat_init(&stats->wait_time, gfp) ||
	    blkg_rwstat_init(&stats->queued, gfp) ||
	    blkg_stat_init(&stats->sectors, gfp) ||
	    blkg_stat_init(&stats->time, gfp) ||
	    blkg_stat_init(&stats->unaccounted_time, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_sum, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_samples, gfp) ||
	    blkg_stat_init(&stats->dequeue, gfp) ||
	    blkg_stat_init(&stats->group_wait_time, gfp) ||
	    blkg_stat_init(&stats->idle_time, gfp) ||
	    blkg_stat_init(&stats->empty_time, gfp)) {
		bfqg_stats_exit(stats);
		return -ENOMEM;
	}

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

static void bfq_cpd_init(struct blkcg_policy_data *cpd)
{
	struct bfq_group_data *d = cpd_to_bfqgd(cpd);

	d->weight = BFQ_DEFAULT_GRP_WEIGHT;
}

static struct blkg_policy_data *bfq_pd_alloc(gfp_t gfp, int node)
{
	struct bfq_group *bfqg;

	bfqg = kzalloc_node(sizeof(*bfqg), gfp, node);
	if (!bfqg)
		return NULL;

	if (bfqg_stats_init(&bfqg->stats, gfp) ||
	    bfqg_stats_init(&bfqg->dead_stats, gfp)) {
		kfree(bfqg);
		return NULL;
	}

	return &bfqg->pd;
}

static void bfq_group_set_parent(struct bfq_group *bfqg,
					struct bfq_group *parent)
{
	struct bfq_entity *entity;

	BUG_ON(!parent);
	BUG_ON(!bfqg);
	BUG_ON(bfqg == parent);

	entity = &bfqg->entity;
	entity->parent = parent->my_entity;
	entity->sched_data = &parent->sched_data;
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
	bfqg_stats_exit(&bfqg->dead_stats);

	return kfree(bfqg);
}

/* offset delta from bfqg->stats to bfqg->dead_stats */
static const int dead_stats_off_delta = offsetof(struct bfq_group, dead_stats) -
					offsetof(struct bfq_group, stats);

/* to be used by recursive prfill, sums live and dead stats recursively */
static u64 bfqg_stat_pd_recursive_sum(struct blkg_policy_data *pd, int off)
{
	u64 sum = 0;

	sum += blkg_stat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_bfq, off);
	sum += blkg_stat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_bfq,
				       off + dead_stats_off_delta);
	return sum;
}

/* to be used by recursive prfill, sums live and dead rwstats recursively */
static struct blkg_rwstat bfqg_rwstat_pd_recursive_sum(struct blkg_policy_data *pd,
						       int off)
{
	struct blkg_rwstat a, b;

	a = blkg_rwstat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_bfq, off);
	b = blkg_rwstat_recursive_sum(pd_to_blkg(pd), &blkcg_policy_bfq,
				      off + dead_stats_off_delta);
	blkg_rwstat_add_aux(&a, &b);
	return a;
}

static void bfq_pd_reset_stats(struct blkg_policy_data *pd)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	bfqg_stats_reset(&bfqg->stats);
	bfqg_stats_reset(&bfqg->dead_stats);
}

static struct bfq_group *bfq_find_alloc_group(struct bfq_data *bfqd,
					      struct blkcg *blkcg)
{
	struct request_queue *q = bfqd->queue;
	struct bfq_group *bfqg = NULL, *parent;
	struct bfq_entity *entity = NULL;

	assert_spin_locked(bfqd->queue->queue_lock);

	/* avoid lookup for the common case where there's no blkcg */
	if (blkcg == &blkcg_root) {
		bfqg = bfqd->root_group;
	} else {
		struct blkcg_gq *blkg;

		blkg = blkg_lookup_create(blkcg, q);
		if (!IS_ERR(blkg))
			bfqg = blkg_to_bfqg(blkg);
		else /* fallback to root_group */
			bfqg = bfqd->root_group;
	}

	BUG_ON(!bfqg);

	/*
	 * Update chain of bfq_groups as we might be handling a leaf group
	 * which, along with some of its relatives, has not been hooked yet
	 * to the private hierarchy of BFQ.
	 */
	entity = &bfqg->entity;
	for_each_entity(entity) {
		bfqg = container_of(entity, struct bfq_group, entity);
		BUG_ON(!bfqg);
		if (bfqg != bfqd->root_group) {
			parent = bfqg_parent(bfqg);
			if (!parent)
				parent = bfqd->root_group;
			BUG_ON(!parent);
			bfq_group_set_parent(bfqg, parent);
		}
	}

	return bfqg;
}

static void bfq_pos_tree_add_move(struct bfq_data *bfqd, struct bfq_queue *bfqq);

/**
 * bfq_bfqq_move - migrate @bfqq to @bfqg.
 * @bfqd: queue descriptor.
 * @bfqq: the queue to move.
 * @entity: @bfqq's entity.
 * @bfqg: the group to move to.
 *
 * Move @bfqq to @bfqg, deactivating it from its old group and reactivating
 * it on the new one.  Avoid putting the entity on the old group idle tree.
 *
 * Must be called under the queue lock; the cgroup owning @bfqg must
 * not disappear (by now this just means that we are called under
 * rcu_read_lock()).
 */
static void bfq_bfqq_move(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_entity *entity, struct bfq_group *bfqg)
{
	int busy, resume;

	busy = bfq_bfqq_busy(bfqq);
	resume = !RB_EMPTY_ROOT(&bfqq->sort_list);

	BUG_ON(resume && !entity->on_st);
	BUG_ON(busy && !resume && entity->on_st &&
	       bfqq != bfqd->in_service_queue);

	if (busy) {
		BUG_ON(atomic_read(&bfqq->ref) < 2);

		if (!resume)
			bfq_del_bfqq_busy(bfqd, bfqq, 0);
		else
			bfq_deactivate_bfqq(bfqd, bfqq, 0);
	} else if (entity->on_st)
		bfq_put_idle_entity(bfq_entity_service_tree(entity), entity);
	bfqg_put(bfqq_group(bfqq));

	/*
	 * Here we use a reference to bfqg.  We don't need a refcounter
	 * as the cgroup reference will not be dropped, so that its
	 * destroy() callback will not be invoked.
	 */
	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;
	bfqg_get(bfqg);

	if (busy) {
		bfq_pos_tree_add_move(bfqd, bfqq);
		if (resume)
			bfq_activate_bfqq(bfqd, bfqq);
	}

	if (!bfqd->in_service_queue && !bfqd->rq_in_driver)
		bfq_schedule_dispatch(bfqd);
}

/**
 * __bfq_bic_change_cgroup - move @bic to @cgroup.
 * @bfqd: the queue descriptor.
 * @bic: the bic to move.
 * @blkcg: the blk-cgroup to move to.
 *
 * Move bic to blkcg, assuming that bfqd->queue is locked; the caller
 * has to make sure that the reference to cgroup is valid across the call.
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

	lockdep_assert_held(bfqd->queue->queue_lock);

	bfqg = bfq_find_alloc_group(bfqd, blkcg);
	if (async_bfqq) {
		entity = &async_bfqq->entity;

		if (entity->sched_data != &bfqg->sched_data) {
			bic_set_bfqq(bic, NULL, 0);
			bfq_log_bfqq(bfqd, async_bfqq,
				     "bic_change_group: %p %d",
				     async_bfqq, atomic_read(&async_bfqq->ref));
			bfq_put_queue(async_bfqq);
		}
	}

	if (sync_bfqq) {
		entity = &sync_bfqq->entity;
		if (entity->sched_data != &bfqg->sched_data)
			bfq_bfqq_move(bfqd, sync_bfqq, entity, bfqg);
	}

	return bfqg;
}

static void bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	struct blkcg *blkcg;
	struct bfq_group *bfqg = NULL;
	uint64_t id;

	rcu_read_lock();
	blkcg = bio_blkcg(bio);
	id = blkcg->css.serial_nr;
	rcu_read_unlock();

	/*
	 * Check whether blkcg has changed.  The condition may trigger
	 * spuriously on a newly created cic but there's no harm.
	 */
	if (unlikely(!bfqd) || likely(bic->blkcg_id == id))
		return;

	bfqg = __bfq_bic_change_cgroup(bfqd, bic, blkcg);
	BUG_ON(!bfqg);
	bic->blkcg_id = id;
}

/**
 * bfq_flush_idle_tree - deactivate any entity on the idle tree of @st.
 * @st: the service tree being flushed.
 */
static void bfq_flush_idle_tree(struct bfq_service_tree *st)
{
	struct bfq_entity *entity = st->first_idle;

	for (; entity ; entity = st->first_idle)
		__bfq_deactivate_entity(entity, 0);
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

	BUG_ON(!bfqq);
	bfq_bfqq_move(bfqd, bfqq, entity, bfqd->root_group);
	return;
}

/**
 * bfq_reparent_active_entities - move to the root group all active
 *                                entities.
 * @bfqd: the device data structure with the root group.
 * @bfqg: the group to move from.
 * @st: the service tree with the entities.
 *
 * Needs queue_lock to be taken and reference to be valid over the call.
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

	return;
}

/**
 * bfq_destroy_group - destroy @bfqg.
 * @bfqg: the group being destroyed.
 *
 * Destroy @bfqg, making sure that it is not referenced from its parent.
 * blkio already grabs the queue_lock for us, so no need to use RCU-based magic
 */
static void bfq_pd_offline(struct blkg_policy_data *pd)
{
	struct bfq_service_tree *st;
	struct bfq_group *bfqg;
	struct bfq_data *bfqd;
	struct bfq_entity *entity;
	int i;

	BUG_ON(!pd);
	bfqg = pd_to_bfqg(pd);
	BUG_ON(!bfqg);
	bfqd = bfqg->bfqd;
	BUG_ON(bfqd && !bfqd->root_group);

	entity = bfqg->my_entity;

	if (!entity) /* root group */
		return;

	/*
	 * Empty all service_trees belonging to this group before
	 * deactivating the group itself.
	 */
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++) {
		BUG_ON(!bfqg->sched_data.service_tree);
		st = bfqg->sched_data.service_tree + i;
		/*
		 * The idle tree may still contain bfq_queues belonging
		 * to exited task because they never migrated to a different
		 * cgroup from the one being destroyed now.  No one else
		 * can access them so it's safe to act without any lock.
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
		BUG_ON(!RB_EMPTY_ROOT(&st->active));
		BUG_ON(!RB_EMPTY_ROOT(&st->idle));
	}
	BUG_ON(bfqg->sched_data.next_in_service);
	BUG_ON(bfqg->sched_data.in_service_entity);

	__bfq_deactivate_entity(entity, 0);
	bfq_put_async_queues(bfqd, bfqg);
	BUG_ON(entity->tree);

	bfqg_stats_xfer_dead(bfqg);
}

static void bfq_end_wr_async(struct bfq_data *bfqd)
{
	struct blkcg_gq *blkg;

	list_for_each_entry(blkg, &bfqd->queue->blkg_list, q_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);

		bfq_end_wr_async_queues(bfqd, bfqg);
	}
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

static u64 bfqio_cgroup_weight_read(struct cgroup_subsys_state *css,
				       struct cftype *cftype)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	int ret = -EINVAL;

	spin_lock_irq(&blkcg->lock);
	ret = bfqgd->weight;
	spin_unlock_irq(&blkcg->lock);

	return ret;
}

static int bfqio_cgroup_weight_read_dfl(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);

	spin_lock_irq(&blkcg->lock);
	seq_printf(sf, "%u\n", bfqgd->weight);
	spin_unlock_irq(&blkcg->lock);

	return 0;
}

static int bfqio_cgroup_weight_write(struct cgroup_subsys_state *css,
					struct cftype *cftype,
					u64 val)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	struct blkcg_gq *blkg;
	int ret = -EINVAL;

	if (val < BFQ_MIN_WEIGHT || val > BFQ_MAX_WEIGHT)
		return ret;

	ret = 0;
	spin_lock_irq(&blkcg->lock);
	bfqgd->weight = (unsigned short)val;
	hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);
		if (!bfqg)
			continue;
		/*
		 * Setting the prio_changed flag of the entity
		 * to 1 with new_weight == weight would re-set
		 * the value of the weight to its ioprio mapping.
		 * Set the flag only if necessary.
		 */
		if ((unsigned short)val != bfqg->entity.new_weight) {
			bfqg->entity.new_weight = (unsigned short)val;
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
	spin_unlock_irq(&blkcg->lock);

	return ret;
}

static ssize_t bfqio_cgroup_weight_write_dfl(struct kernfs_open_file *of,
					     char *buf, size_t nbytes,
					     loff_t off)
{
	/* First unsigned long found in the file is used */
	return bfqio_cgroup_weight_write(of_css(of), NULL,
					 simple_strtoull(strim(buf), NULL, 0));
}

static int bfqg_print_stat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_stat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, false);
	return 0;
}

static int bfqg_print_rwstat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_rwstat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, true);
	return 0;
}

static u64 bfqg_prfill_stat_recursive(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	u64 sum = bfqg_stat_pd_recursive_sum(pd, off);

	return __blkg_prfill_u64(sf, pd, sum);
}

static u64 bfqg_prfill_rwstat_recursive(struct seq_file *sf,
					struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat sum = bfqg_rwstat_pd_recursive_sum(pd, off);

	return __blkg_prfill_rwstat(sf, pd, &sum);
}

static int bfqg_print_stat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_stat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, false);
	return 0;
}

static int bfqg_print_rwstat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_rwstat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, true);
	return 0;
}

static u64 bfqg_prfill_avg_queue_size(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);
	u64 samples = blkg_stat_read(&bfqg->stats.avg_queue_size_samples);
	u64 v = 0;

	if (samples) {
		v = blkg_stat_read(&bfqg->stats.avg_queue_size_sum);
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

static struct bfq_group *bfq_create_group_hierarchy(struct bfq_data *bfqd, int node)
{
	int ret;

	ret = blkcg_activate_policy(bfqd->queue, &blkcg_policy_bfq);
	if (ret)
		return NULL;

        return blkg_to_bfqg(bfqd->queue->root_blkg);
}

static struct blkcg_policy_data *bfq_cpd_alloc(gfp_t gfp)
{
        struct bfq_group_data *bgd;

        bgd = kzalloc(sizeof(*bgd), GFP_KERNEL);
        if (!bgd)
                return NULL;
        return &bgd->pd;
}

static void bfq_cpd_free(struct blkcg_policy_data *cpd)
{
        kfree(cpd_to_bfqgd(cpd));
}

static struct cftype bfqio_files_dfl[] = {
	{
		.name = "weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfqio_cgroup_weight_read_dfl,
		.write = bfqio_cgroup_weight_write_dfl,
	},
	{} /* terminate */
};

static struct cftype bfqio_files[] = {
	{
		.name = "bfq.weight",
		.read_u64 = bfqio_cgroup_weight_read,
		.write_u64 = bfqio_cgroup_weight_write,
	},
	/* statistics, cover only the tasks in the bfqg */
	{
		.name = "bfq.time",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.sectors",
		.private = offsetof(struct bfq_group, stats.sectors),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.io_service_bytes",
		.private = offsetof(struct bfq_group, stats.service_bytes),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_serviced",
		.private = offsetof(struct bfq_group, stats.serviced),
		.seq_show = bfqg_print_rwstat,
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

	/* the same statictics which cover the bfqg and its descendants */
	{
		.name = "bfq.time_recursive",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat_recursive,
	},
	{
		.name = "bfq.sectors_recursive",
		.private = offsetof(struct bfq_group, stats.sectors),
		.seq_show = bfqg_print_stat_recursive,
	},
	{
		.name = "bfq.io_service_bytes_recursive",
		.private = offsetof(struct bfq_group, stats.service_bytes),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_serviced_recursive",
		.private = offsetof(struct bfq_group, stats.serviced),
		.seq_show = bfqg_print_rwstat_recursive,
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
	{
		.name = "bfq.unaccounted_time",
		.private = offsetof(struct bfq_group, stats.unaccounted_time),
		.seq_show = bfqg_print_stat,
	},
	{ }	/* terminate */
};

static struct blkcg_policy blkcg_policy_bfq = {
       .dfl_cftypes            = bfqio_files_dfl,
       .legacy_cftypes         = bfqio_files,

       .pd_alloc_fn            = bfq_pd_alloc,
       .pd_init_fn             = bfq_pd_init,
       .pd_offline_fn          = bfq_pd_offline,
       .pd_free_fn             = bfq_pd_free,
       .pd_reset_stats_fn      = bfq_pd_reset_stats,

       .cpd_alloc_fn           = bfq_cpd_alloc,
       .cpd_init_fn            = bfq_cpd_init,
       .cpd_bind_fn	       = bfq_cpd_init,
       .cpd_free_fn            = bfq_cpd_free,

};

#else

static void bfq_init_entity(struct bfq_entity *entity,
			    struct bfq_group *bfqg)
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

static struct bfq_group *
bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	return bfqd->root_group;
}

static void bfq_bfqq_move(struct bfq_data *bfqd,
			  struct bfq_queue *bfqq,
			  struct bfq_entity *entity,
			  struct bfq_group *bfqg)
{
}

static void bfq_end_wr_async(struct bfq_data *bfqd)
{
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

static void bfq_disconnect_groups(struct bfq_data *bfqd)
{
	bfq_put_async_queues(bfqd, bfqd->root_group);
}

static struct bfq_group *bfq_find_alloc_group(struct bfq_data *bfqd,
                                              struct blkcg *blkcg)
{
	return bfqd->root_group;
}

static struct bfq_group *bfq_create_group_hierarchy(struct bfq_data *bfqd, int node)
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
#endif
