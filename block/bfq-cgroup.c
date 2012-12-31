/*
 * BFQ: CGROUPS support.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Licensed under the GPL-2 as detailed in the accompanying COPYING.BFQ file.
 */

#ifdef CONFIG_CGROUP_BFQIO
static struct bfqio_cgroup bfqio_root_cgroup = {
	.weight = BFQ_DEFAULT_GRP_WEIGHT,
	.ioprio = BFQ_DEFAULT_GRP_IOPRIO,
	.ioprio_class = BFQ_DEFAULT_GRP_CLASS,
};

static inline void bfq_init_entity(struct bfq_entity *entity,
				   struct bfq_group *bfqg)
{
	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	entity->ioprio = entity->new_ioprio;
	entity->ioprio_class = entity->new_ioprio_class;
	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;
}

static struct bfqio_cgroup *cgroup_to_bfqio(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, bfqio_subsys_id),
			    struct bfqio_cgroup, css);
}

/*
 * Search the bfq_group for bfqd into the hash table (by now only a list)
 * of bgrp.  Must be called under rcu_read_lock().
 */
static struct bfq_group *bfqio_lookup_group(struct bfqio_cgroup *bgrp,
					    struct bfq_data *bfqd)
{
	struct bfq_group *bfqg;
	struct hlist_node *n;
	void *key;

	hlist_for_each_entry_rcu(bfqg, n, &bgrp->group_data, group_node) {
		key = rcu_dereference(bfqg->bfqd);
		if (key == bfqd)
			return bfqg;
	}

	return NULL;
}

static inline void bfq_group_init_entity(struct bfqio_cgroup *bgrp,
					 struct bfq_group *bfqg)
{
	struct bfq_entity *entity = &bfqg->entity;

	entity->weight = entity->new_weight = bgrp->weight;
	entity->orig_weight = entity->new_weight;
	entity->ioprio = entity->new_ioprio = bgrp->ioprio;
	entity->ioprio_class = entity->new_ioprio_class = bgrp->ioprio_class;
	entity->ioprio_changed = 1;
	entity->my_sched_data = &bfqg->sched_data;
}

static inline void bfq_group_set_parent(struct bfq_group *bfqg,
					struct bfq_group *parent)
{
	struct bfq_entity *entity;

	BUG_ON(parent == NULL);
	BUG_ON(bfqg == NULL);

	entity = &bfqg->entity;
	entity->parent = parent->my_entity;
	entity->sched_data = &parent->sched_data;
}

/**
 * bfq_group_chain_alloc - allocate a chain of groups.
 * @bfqd: queue descriptor.
 * @cgroup: the leaf cgroup this chain starts from.
 *
 * Allocate a chain of groups starting from the one belonging to
 * @cgroup up to the root cgroup.  Stop if a cgroup on the chain
 * to the root has already an allocated group on @bfqd.
 */
static struct bfq_group *bfq_group_chain_alloc(struct bfq_data *bfqd,
					       struct cgroup *cgroup)
{
	struct bfqio_cgroup *bgrp;
	struct bfq_group *bfqg, *prev = NULL, *leaf = NULL;

	for (; cgroup != NULL; cgroup = cgroup->parent) {
		bgrp = cgroup_to_bfqio(cgroup);

		bfqg = bfqio_lookup_group(bgrp, bfqd);
		if (bfqg != NULL) {
			/*
			 * All the cgroups in the path from there to the
			 * root must have a bfq_group for bfqd, so we don't
			 * need any more allocations.
			 */
			break;
		}

		bfqg = kzalloc(sizeof(*bfqg), GFP_ATOMIC);
		if (bfqg == NULL)
			goto cleanup;

		bfq_group_init_entity(bgrp, bfqg);
		bfqg->my_entity = &bfqg->entity;

		if (leaf == NULL) {
			leaf = bfqg;
			prev = leaf;
		} else {
			bfq_group_set_parent(prev, bfqg);
			/*
			 * Build a list of allocated nodes using the bfqd
			 * filed, that is still unused and will be initialized
			 * only after the node will be connected.
			 */
			prev->bfqd = bfqg;
			prev = bfqg;
		}
	}

	return leaf;

cleanup:
	while (leaf != NULL) {
		prev = leaf;
		leaf = leaf->bfqd;
		kfree(prev);
	}

	return NULL;
}

/**
 * bfq_group_chain_link - link an allocatd group chain to a cgroup hierarchy.
 * @bfqd: the queue descriptor.
 * @cgroup: the leaf cgroup to start from.
 * @leaf: the leaf group (to be associated to @cgroup).
 *
 * Try to link a chain of groups to a cgroup hierarchy, connecting the
 * nodes bottom-up, so we can be sure that when we find a cgroup in the
 * hierarchy that already as a group associated to @bfqd all the nodes
 * in the path to the root cgroup have one too.
 *
 * On locking: the queue lock protects the hierarchy (there is a hierarchy
 * per device) while the bfqio_cgroup lock protects the list of groups
 * belonging to the same cgroup.
 */
static void bfq_group_chain_link(struct bfq_data *bfqd, struct cgroup *cgroup,
				 struct bfq_group *leaf)
{
	struct bfqio_cgroup *bgrp;
	struct bfq_group *bfqg, *next, *prev = NULL;
	unsigned long flags;

	assert_spin_locked(bfqd->queue->queue_lock);

	for (; cgroup != NULL && leaf != NULL; cgroup = cgroup->parent) {
		bgrp = cgroup_to_bfqio(cgroup);
		next = leaf->bfqd;

		bfqg = bfqio_lookup_group(bgrp, bfqd);
		BUG_ON(bfqg != NULL);

		spin_lock_irqsave(&bgrp->lock, flags);

		rcu_assign_pointer(leaf->bfqd, bfqd);
		hlist_add_head_rcu(&leaf->group_node, &bgrp->group_data);
		hlist_add_head(&leaf->bfqd_node, &bfqd->group_list);

		spin_unlock_irqrestore(&bgrp->lock, flags);

		prev = leaf;
		leaf = next;
	}

	BUG_ON(cgroup == NULL && leaf != NULL);
	if (cgroup != NULL && prev != NULL) {
		bgrp = cgroup_to_bfqio(cgroup);
		bfqg = bfqio_lookup_group(bgrp, bfqd);
		bfq_group_set_parent(prev, bfqg);
	}
}

/**
 * bfq_find_alloc_group - return the group associated to @bfqd in @cgroup.
 * @bfqd: queue descriptor.
 * @cgroup: cgroup being searched for.
 *
 * Return a group associated to @bfqd in @cgroup, allocating one if
 * necessary.  When a group is returned all the cgroups in the path
 * to the root have a group associated to @bfqd.
 *
 * If the allocation fails, return the root group: this breaks guarantees
 * but is a safe fallbak.  If this loss becames a problem it can be
 * mitigated using the equivalent weight (given by the product of the
 * weights of the groups in the path from @group to the root) in the
 * root scheduler.
 *
 * We allocate all the missing nodes in the path from the leaf cgroup
 * to the root and we connect the nodes only after all the allocations
 * have been successful.
 */
static struct bfq_group *bfq_find_alloc_group(struct bfq_data *bfqd,
					      struct cgroup *cgroup)
{
	struct bfqio_cgroup *bgrp = cgroup_to_bfqio(cgroup);
	struct bfq_group *bfqg;

	bfqg = bfqio_lookup_group(bgrp, bfqd);
	if (bfqg != NULL)
		return bfqg;

	bfqg = bfq_group_chain_alloc(bfqd, cgroup);
	if (bfqg != NULL)
		bfq_group_chain_link(bfqd, cgroup, bfqg);
	else
		bfqg = bfqd->root_group;

	return bfqg;
}

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
	BUG_ON(busy && !resume && entity->on_st && bfqq != bfqd->active_queue);

	if (busy) {
		BUG_ON(atomic_read(&bfqq->ref) < 2);

		if (!resume)
			bfq_del_bfqq_busy(bfqd, bfqq, 0);
		else
			bfq_deactivate_bfqq(bfqd, bfqq, 0);
	} else if (entity->on_st)
		bfq_put_idle_entity(bfq_entity_service_tree(entity), entity);

	/*
	 * Here we use a reference to bfqg.  We don't need a refcounter
	 * as the cgroup reference will not be dropped, so that its
	 * destroy() callback will not be invoked.
	 */
	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;

	if (busy && resume)
		bfq_activate_bfqq(bfqd, bfqq);
}

/**
 * __bfq_cic_change_cgroup - move @cic to @cgroup.
 * @bfqd: the queue descriptor.
 * @cic: the cic to move.
 * @cgroup: the cgroup to move to.
 *
 * Move cic to cgroup, assuming that bfqd->queue is locked; the caller
 * has to make sure that the reference to cgroup is valid across the call.
 *
 * NOTE: an alternative approach might have been to store the current
 * cgroup in bfqq and getting a reference to it, reducing the lookup
 * time here, at the price of slightly more complex code.
 */
static struct bfq_group *__bfq_cic_change_cgroup(struct bfq_data *bfqd,
						 struct cfq_io_context *cic,
						 struct cgroup *cgroup)
{
	struct bfq_queue *async_bfqq = cic_to_bfqq(cic, 0);
	struct bfq_queue *sync_bfqq = cic_to_bfqq(cic, 1);
	struct bfq_entity *entity;
	struct bfq_group *bfqg;

	bfqg = bfq_find_alloc_group(bfqd, cgroup);
	if (async_bfqq != NULL) {
		entity = &async_bfqq->entity;

		if (entity->sched_data != &bfqg->sched_data) {
			cic_set_bfqq(cic, NULL, 0);
			bfq_log_bfqq(bfqd, async_bfqq,
				     "cic_change_group: %p %d",
				     async_bfqq, atomic_read(&async_bfqq->ref));
			bfq_put_queue(async_bfqq);
		}
	}

	if (sync_bfqq != NULL) {
		entity = &sync_bfqq->entity;
		if (entity->sched_data != &bfqg->sched_data)
			bfq_bfqq_move(bfqd, sync_bfqq, entity, bfqg);
	}

	return bfqg;
}

/**
 * bfq_cic_change_cgroup - move @cic to @cgroup.
 * @cic: the cic being migrated.
 * @cgroup: the destination cgroup.
 *
 * When the task owning @cic is moved to @cgroup, @cic is immediately
 * moved into its new parent group.
 */
static void bfq_cic_change_cgroup(struct cfq_io_context *cic,
				  struct cgroup *cgroup)
{
	struct bfq_data *bfqd;
	unsigned long uninitialized_var(flags);

	bfqd = bfq_get_bfqd_locked(&cic->key, &flags);
	if (bfqd != NULL &&
	    !strncmp(bfqd->queue->elevator->elevator_type->elevator_name,
		     "bfq", ELV_NAME_MAX)) {
		__bfq_cic_change_cgroup(bfqd, cic, cgroup);
		bfq_put_bfqd_unlock(bfqd, &flags);
	}
}

/**
 * bfq_cic_update_cgroup - update the cgroup of @cic.
 * @cic: the @cic to update.
 *
 * Make sure that @cic is enqueued in the cgroup of the current task.
 * We need this in addition to moving cics during the cgroup attach
 * phase because the task owning @cic could be at its first disk
 * access or we may end up in the root cgroup as the result of a
 * memory allocation failure and here we try to move to the right
 * group.
 *
 * Must be called under the queue lock.  It is safe to use the returned
 * value even after the rcu_read_unlock() as the migration/destruction
 * paths act under the queue lock too.  IOW it is impossible to race with
 * group migration/destruction and end up with an invalid group as:
 *   a) here cgroup has not yet been destroyed, nor its destroy callback
 *      has started execution, as current holds a reference to it,
 *   b) if it is destroyed after rcu_read_unlock() [after current is
 *      migrated to a different cgroup] its attach() callback will have
 *      taken care of remove all the references to the old cgroup data.
 */
static struct bfq_group *bfq_cic_update_cgroup(struct cfq_io_context *cic)
{
	struct bfq_data *bfqd = cic->key;
	struct bfq_group *bfqg;
	struct cgroup *cgroup;

	BUG_ON(bfqd == NULL);

	rcu_read_lock();
	cgroup = task_cgroup(current, bfqio_subsys_id);
	bfqg = __bfq_cic_change_cgroup(bfqd, cic, cgroup);
	rcu_read_unlock();

	return bfqg;
}

/**
 * bfq_flush_idle_tree - deactivate any entity on the idle tree of @st.
 * @st: the service tree being flushed.
 */
static inline void bfq_flush_idle_tree(struct bfq_service_tree *st)
{
	struct bfq_entity *entity = st->first_idle;

	for (; entity != NULL; entity = st->first_idle)
		__bfq_deactivate_entity(entity, 0);
}

/**
 * bfq_reparent_leaf_entity - move leaf entity to the root_group.
 * @bfqd: the device data structure with the root group.
 * @entity: the entity to move.
 */
static inline void bfq_reparent_leaf_entity(struct bfq_data *bfqd,
					    struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	BUG_ON(bfqq == NULL);
	bfq_bfqq_move(bfqd, bfqq, entity, bfqd->root_group);
	return;
}

/**
 * bfq_reparent_active_entities - move to the root group all active entities.
 * @bfqd: the device data structure with the root group.
 * @bfqg: the group to move from.
 * @st: the service tree with the entities.
 *
 * Needs queue_lock to be taken and reference to be valid over the call.
 */
static inline void bfq_reparent_active_entities(struct bfq_data *bfqd,
						struct bfq_group *bfqg,
						struct bfq_service_tree *st)
{
	struct rb_root *active = &st->active;
	struct bfq_entity *entity = NULL;

	if (!RB_EMPTY_ROOT(&st->active))
		entity = bfq_entity_of(rb_first(active));

	for (; entity != NULL ; entity = bfq_entity_of(rb_first(active)))
		bfq_reparent_leaf_entity(bfqd, entity);

	if (bfqg->sched_data.active_entity != NULL)
		bfq_reparent_leaf_entity(bfqd, bfqg->sched_data.active_entity);

	return;
}

/**
 * bfq_destroy_group - destroy @bfqg.
 * @bgrp: the bfqio_cgroup containing @bfqg.
 * @bfqg: the group being destroyed.
 *
 * Destroy @bfqg, making sure that it is not referenced from its parent.
 */
static void bfq_destroy_group(struct bfqio_cgroup *bgrp, struct bfq_group *bfqg)
{
	struct bfq_data *bfqd;
	struct bfq_service_tree *st;
	struct bfq_entity *entity = bfqg->my_entity;
	unsigned long uninitialized_var(flags);
	int i;

	hlist_del(&bfqg->group_node);

	/*
	 * Empty all service_trees belonging to this group before deactivating
	 * the group itself.
	 */
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++) {
		st = bfqg->sched_data.service_tree + i;

		/*
		 * The idle tree may still contain bfq_queues belonging
		 * to exited task because they never migrated to a different
		 * cgroup from the one being destroyed now.  Noone else
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
		 * under service, which is disconnected from the active
		 * tree: it must be moved, too.
		 * There is no need to put the sync queues, as the
		 * scheduler has taken no reference.
		 */
		bfqd = bfq_get_bfqd_locked(&bfqg->bfqd, &flags);
		if (bfqd != NULL) {
			bfq_reparent_active_entities(bfqd, bfqg, st);
			bfq_put_bfqd_unlock(bfqd, &flags);
		}
		BUG_ON(!RB_EMPTY_ROOT(&st->active));
		BUG_ON(!RB_EMPTY_ROOT(&st->idle));
	}
	BUG_ON(bfqg->sched_data.next_active != NULL);
	BUG_ON(bfqg->sched_data.active_entity != NULL);

	/*
	 * We may race with device destruction, take extra care when
	 * dereferencing bfqg->bfqd.
	 */
	bfqd = bfq_get_bfqd_locked(&bfqg->bfqd, &flags);
	if (bfqd != NULL) {
		hlist_del(&bfqg->bfqd_node);
		__bfq_deactivate_entity(entity, 0);
		bfq_put_async_queues(bfqd, bfqg);
		bfq_put_bfqd_unlock(bfqd, &flags);
	}
	BUG_ON(entity->tree != NULL);

	/*
	 * No need to defer the kfree() to the end of the RCU grace
	 * period: we are called from the destroy() callback of our
	 * cgroup, so we can be sure that noone is a) still using
	 * this cgroup or b) doing lookups in it.
	 */
	kfree(bfqg);
}

/**
 * bfq_disconnect_groups - diconnect @bfqd from all its groups.
 * @bfqd: the device descriptor being exited.
 *
 * When the device exits we just make sure that no lookup can return
 * the now unused group structures.  They will be deallocated on cgroup
 * destruction.
 */
static void bfq_disconnect_groups(struct bfq_data *bfqd)
{
	struct hlist_node *pos, *n;
	struct bfq_group *bfqg;

	bfq_log(bfqd, "disconnect_groups beginning") ;
	hlist_for_each_entry_safe(bfqg, pos, n, &bfqd->group_list, bfqd_node) {
		hlist_del(&bfqg->bfqd_node);

		__bfq_deactivate_entity(bfqg->my_entity, 0);

		/*
		 * Don't remove from the group hash, just set an
		 * invalid key.  No lookups can race with the
		 * assignment as bfqd is being destroyed; this
		 * implies also that new elements cannot be added
		 * to the list.
		 */
		rcu_assign_pointer(bfqg->bfqd, NULL);

		bfq_log(bfqd, "disconnect_groups: put async for group %p",
			bfqg) ;
		bfq_put_async_queues(bfqd, bfqg);
	}
}

static inline void bfq_free_root_group(struct bfq_data *bfqd)
{
	struct bfqio_cgroup *bgrp = &bfqio_root_cgroup;
	struct bfq_group *bfqg = bfqd->root_group;

	bfq_put_async_queues(bfqd, bfqg);

	spin_lock_irq(&bgrp->lock);
	hlist_del_rcu(&bfqg->group_node);
	spin_unlock_irq(&bgrp->lock);

	/*
	 * No need to synchronize_rcu() here: since the device is gone
	 * there cannot be any read-side access to its root_group.
	 */
	kfree(bfqg);
}

static struct bfq_group *bfq_alloc_root_group(struct bfq_data *bfqd, int node)
{
	struct bfq_group *bfqg;
	struct bfqio_cgroup *bgrp;
	int i;

	bfqg = kmalloc_node(sizeof(*bfqg), GFP_KERNEL | __GFP_ZERO, node);
	if (bfqg == NULL)
		return NULL;

	bfqg->entity.parent = NULL;
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		bfqg->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;

	bgrp = &bfqio_root_cgroup;
	spin_lock_irq(&bgrp->lock);
	rcu_assign_pointer(bfqg->bfqd, bfqd);
	hlist_add_head_rcu(&bfqg->group_node, &bgrp->group_data);
	spin_unlock_irq(&bgrp->lock);

	return bfqg;
}

#define SHOW_FUNCTION(__VAR)						\
static u64 bfqio_cgroup_##__VAR##_read(struct cgroup *cgroup,		\
				       struct cftype *cftype)		\
{									\
	struct bfqio_cgroup *bgrp;					\
	u64 ret;							\
									\
	if (!cgroup_lock_live_group(cgroup))				\
		return -ENODEV;						\
									\
	bgrp = cgroup_to_bfqio(cgroup);					\
	spin_lock_irq(&bgrp->lock);					\
	ret = bgrp->__VAR;						\
	spin_unlock_irq(&bgrp->lock);					\
									\
	cgroup_unlock();						\
									\
	return ret;							\
}

SHOW_FUNCTION(weight);
SHOW_FUNCTION(ioprio);
SHOW_FUNCTION(ioprio_class);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__VAR, __MIN, __MAX)				\
static int bfqio_cgroup_##__VAR##_write(struct cgroup *cgroup,		\
					struct cftype *cftype,		\
					u64 val)			\
{									\
	struct bfqio_cgroup *bgrp;					\
	struct bfq_group *bfqg;						\
	struct hlist_node *n;						\
									\
	if (val < (__MIN) || val > (__MAX))				\
		return -EINVAL;						\
									\
	if (!cgroup_lock_live_group(cgroup))				\
		return -ENODEV;						\
									\
	bgrp = cgroup_to_bfqio(cgroup);					\
									\
	spin_lock_irq(&bgrp->lock);					\
	bgrp->__VAR = (unsigned short)val;				\
	hlist_for_each_entry(bfqg, n, &bgrp->group_data, group_node) {	\
		bfqg->entity.new_##__VAR = (unsigned short)val;		\
		smp_wmb();						\
		bfqg->entity.ioprio_changed = 1;			\
	}								\
	spin_unlock_irq(&bgrp->lock);					\
									\
	cgroup_unlock();						\
									\
	return 0;							\
}

STORE_FUNCTION(weight, BFQ_MIN_WEIGHT, BFQ_MAX_WEIGHT);
STORE_FUNCTION(ioprio, 0, IOPRIO_BE_NR - 1);
STORE_FUNCTION(ioprio_class, IOPRIO_CLASS_RT, IOPRIO_CLASS_IDLE);
#undef STORE_FUNCTION

static struct cftype bfqio_files[] = {
	{
		.name = "weight",
		.read_u64 = bfqio_cgroup_weight_read,
		.write_u64 = bfqio_cgroup_weight_write,
	},
	{
		.name = "ioprio",
		.read_u64 = bfqio_cgroup_ioprio_read,
		.write_u64 = bfqio_cgroup_ioprio_write,
	},
	{
		.name = "ioprio_class",
		.read_u64 = bfqio_cgroup_ioprio_class_read,
		.write_u64 = bfqio_cgroup_ioprio_class_write,
	},
};

static int bfqio_populate(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, subsys, bfqio_files,
				ARRAY_SIZE(bfqio_files));
}

static struct cgroup_subsys_state *bfqio_create(struct cgroup_subsys *subsys,
						struct cgroup *cgroup)
{
	struct bfqio_cgroup *bgrp;

	if (cgroup->parent != NULL) {
		bgrp = kzalloc(sizeof(*bgrp), GFP_KERNEL);
		if (bgrp == NULL)
			return ERR_PTR(-ENOMEM);
	} else
		bgrp = &bfqio_root_cgroup;

	spin_lock_init(&bgrp->lock);
	INIT_HLIST_HEAD(&bgrp->group_data);
	bgrp->ioprio = BFQ_DEFAULT_GRP_IOPRIO;
	bgrp->ioprio_class = BFQ_DEFAULT_GRP_CLASS;

	return &bgrp->css;
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic/bfqq data structures.  By now we allow a task to change
 * its cgroup only if it's the only owner of its ioc; the drawback of this
 * behavior is that a group containing a task that forked using CLONE_IO
 * will not be destroyed until the tasks sharing the ioc die.
 */
static int bfqio_can_attach(struct cgroup_subsys *subsys, struct cgroup *cgroup,
			    struct task_struct *tsk)
{
	struct io_context *ioc;
	int ret = 0;

	/* task_lock() is needed to avoid races with exit_io_context() */
	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc != NULL && atomic_read(&ioc->nr_tasks) > 1)
		/*
		 * ioc == NULL means that the task is either too young or
		 * exiting: if it has still no ioc the ioc can't be shared,
		 * if the task is exiting the attach will fail anyway, no
		 * matter what we return here.
		 */
		ret = -EINVAL;
	task_unlock(tsk);

	return ret;
}

static void bfqio_attach(struct cgroup_subsys *subsys, struct cgroup *cgroup,
			 struct cgroup *prev, struct task_struct *tsk)
{
	struct io_context *ioc;
	struct cfq_io_context *cic;
	struct hlist_node *n;

	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc != NULL) {
		BUG_ON(atomic_long_read(&ioc->refcount) == 0);
		atomic_long_inc(&ioc->refcount);
	}
	task_unlock(tsk);

	if (ioc == NULL)
		return;

	rcu_read_lock();
	hlist_for_each_entry_rcu(cic, n, &ioc->bfq_cic_list, cic_list)
		bfq_cic_change_cgroup(cic, cgroup);
	rcu_read_unlock();

	put_io_context(ioc);
}

static void bfqio_destroy(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct bfqio_cgroup *bgrp = cgroup_to_bfqio(cgroup);
	struct hlist_node *n, *tmp;
	struct bfq_group *bfqg;

	/*
	 * Since we are destroying the cgroup, there are no more tasks
	 * referencing it, and all the RCU grace periods that may have
	 * referenced it are ended (as the destruction of the parent
	 * cgroup is RCU-safe); bgrp->group_data will not be accessed by
	 * anything else and we don't need any synchronization.
	 */
	hlist_for_each_entry_safe(bfqg, n, tmp, &bgrp->group_data, group_node)
		bfq_destroy_group(bgrp, bfqg);

	BUG_ON(!hlist_empty(&bgrp->group_data));

	kfree(bgrp);
}

struct cgroup_subsys bfqio_subsys = {
	.name = "bfqio",
	.create = bfqio_create,
	.can_attach = bfqio_can_attach,
	.attach = bfqio_attach,
	.destroy = bfqio_destroy,
	.populate = bfqio_populate,
	.subsys_id = bfqio_subsys_id,
};
#else
static inline void bfq_init_entity(struct bfq_entity *entity,
				   struct bfq_group *bfqg)
{
	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	entity->ioprio = entity->new_ioprio;
	entity->ioprio_class = entity->new_ioprio_class;
	entity->sched_data = &bfqg->sched_data;
}

static inline struct bfq_group *
bfq_cic_update_cgroup(struct cfq_io_context *cic)
{
	struct bfq_data *bfqd = cic->key;
	return bfqd->root_group;
}

static inline void bfq_bfqq_move(struct bfq_data *bfqd,
				 struct bfq_queue *bfqq,
				 struct bfq_entity *entity,
				 struct bfq_group *bfqg)
{
}

static inline void bfq_disconnect_groups(struct bfq_data *bfqd)
{
	bfq_put_async_queues(bfqd, bfqd->root_group);
}

static inline void bfq_free_root_group(struct bfq_data *bfqd)
{
	kfree(bfqd->root_group);
}

static struct bfq_group *bfq_alloc_root_group(struct bfq_data *bfqd, int node)
{
	struct bfq_group *bfqg;
	int i;

	bfqg = kmalloc_node(sizeof(*bfqg), GFP_KERNEL | __GFP_ZERO, node);
	if (bfqg == NULL)
		return NULL;

	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		bfqg->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;

	return bfqg;
}
#endif
