/*
 * BFQ: Hierarchical B-WF2Q+ scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 */

#ifdef CONFIG_CGROUP_BFQIO
#define for_each_entity(entity)	\
	for (; entity != NULL; entity = entity->parent)

#define for_each_entity_safe(entity, parent) \
	for (; entity && ({ parent = entity->parent; 1; }); entity = parent)

static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd,
						 int extract,
						 struct bfq_data *bfqd);

static inline void bfq_update_budget(struct bfq_entity *next_active)
{
	struct bfq_entity *bfqg_entity;
	struct bfq_group *bfqg;
	struct bfq_sched_data *group_sd;

	BUG_ON(next_active == NULL);

	group_sd = next_active->sched_data;

	bfqg = container_of(group_sd, struct bfq_group, sched_data);
	/*
	 * bfq_group's my_entity field is not NULL only if the group
	 * is not the root group. We must not touch the root entity
	 * as it must never become an active entity.
	 */
	bfqg_entity = bfqg->my_entity;
	if (bfqg_entity != NULL)
		bfqg_entity->budget = next_active->budget;
}

static int bfq_update_next_active(struct bfq_sched_data *sd)
{
	struct bfq_entity *next_active;

	if (sd->active_entity != NULL)
		/* will update/requeue at the end of service */
		return 0;

	/*
	 * NOTE: this can be improved in many ways, such as returning
	 * 1 (and thus propagating upwards the update) only when the
	 * budget changes, or caching the bfqq that will be scheduled
	 * next from this subtree.  By now we worry more about
	 * correctness than about performance...
	 */
	next_active = bfq_lookup_next_entity(sd, 0, NULL);
	sd->next_active = next_active;

	if (next_active != NULL)
		bfq_update_budget(next_active);

	return 1;
}

static inline void bfq_check_next_active(struct bfq_sched_data *sd,
					 struct bfq_entity *entity)
{
	BUG_ON(sd->next_active != entity);
}
#else
#define for_each_entity(entity)	\
	for (; entity != NULL; entity = NULL)

#define for_each_entity_safe(entity, parent) \
	for (parent = NULL; entity != NULL; entity = parent)

static inline int bfq_update_next_active(struct bfq_sched_data *sd)
{
	return 0;
}

static inline void bfq_check_next_active(struct bfq_sched_data *sd,
					 struct bfq_entity *entity)
{
}

static inline void bfq_update_budget(struct bfq_entity *next_active)
{
}
#endif

/*
 * Shift for timestamp calculations.  This actually limits the maximum
 * service allowed in one timestamp delta (small shift values increase it),
 * the maximum total weight that can be used for the queues in the system
 * (big shift values increase it), and the period of virtual time wraparounds.
 */
#define WFQ_SERVICE_SHIFT	22

/**
 * bfq_gt - compare two timestamps.
 * @a: first ts.
 * @b: second ts.
 *
 * Return @a > @b, dealing with wrapping correctly.
 */
static inline int bfq_gt(u64 a, u64 b)
{
	return (s64)(a - b) > 0;
}

static inline struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = NULL;

	BUG_ON(entity == NULL);

	if (entity->my_sched_data == NULL)
		bfqq = container_of(entity, struct bfq_queue, entity);

	return bfqq;
}


/**
 * bfq_delta - map service into the virtual time domain.
 * @service: amount of service.
 * @weight: scale factor (weight of an entity or weight sum).
 */
static inline u64 bfq_delta(unsigned long service,
					unsigned long weight)
{
	u64 d = (u64)service << WFQ_SERVICE_SHIFT;

	do_div(d, weight);
	return d;
}

/**
 * bfq_calc_finish - assign the finish time to an entity.
 * @entity: the entity to act upon.
 * @service: the service to be charged to the entity.
 */
static inline void bfq_calc_finish(struct bfq_entity *entity,
				   unsigned long service)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	BUG_ON(entity->weight == 0);

	entity->finish = entity->start +
		bfq_delta(service, entity->weight);

	if (bfqq != NULL) {
		bfq_log_bfqq(bfqq->bfqd, bfqq,
			"calc_finish: serv %lu, w %d",
			service, entity->weight);
		bfq_log_bfqq(bfqq->bfqd, bfqq,
			"calc_finish: start %llu, finish %llu, delta %llu",
			entity->start, entity->finish,
			bfq_delta(service, entity->weight));
	}
}

/**
 * bfq_entity_of - get an entity from a node.
 * @node: the node field of the entity.
 *
 * Convert a node pointer to the relative entity.  This is used only
 * to simplify the logic of some functions and not as the generic
 * conversion mechanism because, e.g., in the tree walking functions,
 * the check for a %NULL value would be redundant.
 */
static inline struct bfq_entity *bfq_entity_of(struct rb_node *node)
{
	struct bfq_entity *entity = NULL;

	if (node != NULL)
		entity = rb_entry(node, struct bfq_entity, rb_node);

	return entity;
}

/**
 * bfq_extract - remove an entity from a tree.
 * @root: the tree root.
 * @entity: the entity to remove.
 */
static inline void bfq_extract(struct rb_root *root,
			       struct bfq_entity *entity)
{
	BUG_ON(entity->tree != root);

	entity->tree = NULL;
	rb_erase(&entity->rb_node, root);
}

/**
 * bfq_idle_extract - extract an entity from the idle tree.
 * @st: the service tree of the owning @entity.
 * @entity: the entity being removed.
 */
static void bfq_idle_extract(struct bfq_service_tree *st,
			     struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *next;

	BUG_ON(entity->tree != &st->idle);

	if (entity == st->first_idle) {
		next = rb_next(&entity->rb_node);
		st->first_idle = bfq_entity_of(next);
	}

	if (entity == st->last_idle) {
		next = rb_prev(&entity->rb_node);
		st->last_idle = bfq_entity_of(next);
	}

	bfq_extract(&st->idle, entity);

	if (bfqq != NULL)
		list_del(&bfqq->bfqq_list);
}

/**
 * bfq_insert - generic tree insertion.
 * @root: tree root.
 * @entity: entity to insert.
 *
 * This is used for the idle and the active tree, since they are both
 * ordered by finish time.
 */
static void bfq_insert(struct rb_root *root, struct bfq_entity *entity)
{
	struct bfq_entity *entry;
	struct rb_node **node = &root->rb_node;
	struct rb_node *parent = NULL;

	BUG_ON(entity->tree != NULL);

	while (*node != NULL) {
		parent = *node;
		entry = rb_entry(parent, struct bfq_entity, rb_node);

		if (bfq_gt(entry->finish, entity->finish))
			node = &parent->rb_left;
		else
			node = &parent->rb_right;
	}

	rb_link_node(&entity->rb_node, parent, node);
	rb_insert_color(&entity->rb_node, root);

	entity->tree = root;
}

/**
 * bfq_update_min - update the min_start field of a entity.
 * @entity: the entity to update.
 * @node: one of its children.
 *
 * This function is called when @entity may store an invalid value for
 * min_start due to updates to the active tree.  The function  assumes
 * that the subtree rooted at @node (which may be its left or its right
 * child) has a valid min_start value.
 */
static inline void bfq_update_min(struct bfq_entity *entity,
				  struct rb_node *node)
{
	struct bfq_entity *child;

	if (node != NULL) {
		child = rb_entry(node, struct bfq_entity, rb_node);
		if (bfq_gt(entity->min_start, child->min_start))
			entity->min_start = child->min_start;
	}
}

/**
 * bfq_update_active_node - recalculate min_start.
 * @node: the node to update.
 *
 * @node may have changed position or one of its children may have moved,
 * this function updates its min_start value.  The left and right subtrees
 * are assumed to hold a correct min_start value.
 */
static inline void bfq_update_active_node(struct rb_node *node)
{
	struct bfq_entity *entity = rb_entry(node, struct bfq_entity, rb_node);

	entity->min_start = entity->start;
	bfq_update_min(entity, node->rb_right);
	bfq_update_min(entity, node->rb_left);
}

/**
 * bfq_update_active_tree - update min_start for the whole active tree.
 * @node: the starting node.
 *
 * @node must be the deepest modified node after an update.  This function
 * updates its min_start using the values held by its children, assuming
 * that they did not change, and then updates all the nodes that may have
 * changed in the path to the root.  The only nodes that may have changed
 * are the ones in the path or their siblings.
 */
static void bfq_update_active_tree(struct rb_node *node)
{
	struct rb_node *parent;

up:
	bfq_update_active_node(node);

	parent = rb_parent(node);
	if (parent == NULL)
		return;

	if (node == parent->rb_left && parent->rb_right != NULL)
		bfq_update_active_node(parent->rb_right);
	else if (parent->rb_left != NULL)
		bfq_update_active_node(parent->rb_left);

	node = parent;
	goto up;
}

/**
 * bfq_active_insert - insert an entity in the active tree of its group/device.
 * @st: the service tree of the entity.
 * @entity: the entity being inserted.
 *
 * The active tree is ordered by finish time, but an extra key is kept
 * per each node, containing the minimum value for the start times of
 * its children (and the node itself), so it's possible to search for
 * the eligible node with the lowest finish time in logarithmic time.
 */
static void bfq_active_insert(struct bfq_service_tree *st,
			      struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *node = &entity->rb_node;

	bfq_insert(&st->active, entity);

	if (node->rb_left != NULL)
		node = node->rb_left;
	else if (node->rb_right != NULL)
		node = node->rb_right;

	bfq_update_active_tree(node);

	if (bfqq != NULL)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->active_list);
}

/**
 * bfq_ioprio_to_weight - calc a weight from an ioprio.
 * @ioprio: the ioprio value to convert.
 */
static unsigned short bfq_ioprio_to_weight(int ioprio)
{
	WARN_ON(ioprio < 0 || ioprio >= IOPRIO_BE_NR);
	return IOPRIO_BE_NR - ioprio;
}

/**
 * bfq_weight_to_ioprio - calc an ioprio from a weight.
 * @weight: the weight value to convert.
 *
 * To preserve as mush as possible the old only-ioprio user interface,
 * 0 is used as an escape ioprio value for weights (numerically) equal or
 * larger than IOPRIO_BE_NR
 */
static unsigned short bfq_weight_to_ioprio(int weight)
{
	WARN_ON(weight < BFQ_MIN_WEIGHT || weight > BFQ_MAX_WEIGHT);
	return IOPRIO_BE_NR - weight < 0 ? 0 : IOPRIO_BE_NR - weight;
}

static inline void bfq_get_entity(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	if (bfqq != NULL) {
		atomic_inc(&bfqq->ref);
		bfq_log_bfqq(bfqq->bfqd, bfqq, "get_entity: %p %d",
			     bfqq, atomic_read(&bfqq->ref));
	}
}

/**
 * bfq_find_deepest - find the deepest node that an extraction can modify.
 * @node: the node being removed.
 *
 * Do the first step of an extraction in an rb tree, looking for the
 * node that will replace @node, and returning the deepest node that
 * the following modifications to the tree can touch.  If @node is the
 * last node in the tree return %NULL.
 */
static struct rb_node *bfq_find_deepest(struct rb_node *node)
{
	struct rb_node *deepest;

	if (node->rb_right == NULL && node->rb_left == NULL)
		deepest = rb_parent(node);
	else if (node->rb_right == NULL)
		deepest = node->rb_left;
	else if (node->rb_left == NULL)
		deepest = node->rb_right;
	else {
		deepest = rb_next(node);
		if (deepest->rb_right != NULL)
			deepest = deepest->rb_right;
		else if (rb_parent(deepest) != node)
			deepest = rb_parent(deepest);
	}

	return deepest;
}

/**
 * bfq_active_extract - remove an entity from the active tree.
 * @st: the service_tree containing the tree.
 * @entity: the entity being removed.
 */
static void bfq_active_extract(struct bfq_service_tree *st,
			       struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *node;

	node = bfq_find_deepest(&entity->rb_node);
	bfq_extract(&st->active, entity);

	if (node != NULL)
		bfq_update_active_tree(node);

	if (bfqq != NULL)
		list_del(&bfqq->bfqq_list);
}

/**
 * bfq_idle_insert - insert an entity into the idle tree.
 * @st: the service tree containing the tree.
 * @entity: the entity to insert.
 */
static void bfq_idle_insert(struct bfq_service_tree *st,
			    struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct bfq_entity *first_idle = st->first_idle;
	struct bfq_entity *last_idle = st->last_idle;

	if (first_idle == NULL || bfq_gt(first_idle->finish, entity->finish))
		st->first_idle = entity;
	if (last_idle == NULL || bfq_gt(entity->finish, last_idle->finish))
		st->last_idle = entity;

	bfq_insert(&st->idle, entity);

	if (bfqq != NULL)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->idle_list);
}

/**
 * bfq_forget_entity - remove an entity from the wfq trees.
 * @st: the service tree.
 * @entity: the entity being removed.
 *
 * Update the device status and forget everything about @entity, putting
 * the device reference to it, if it is a queue.  Entities belonging to
 * groups are not refcounted.
 */
static void bfq_forget_entity(struct bfq_service_tree *st,
			      struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	BUG_ON(!entity->on_st);

	entity->on_st = 0;
	st->wsum -= entity->weight;
	if (bfqq != NULL) {
		bfq_log_bfqq(bfqq->bfqd, bfqq, "forget_entity: %p %d",
			     bfqq, atomic_read(&bfqq->ref));
		bfq_put_queue(bfqq);
	}
}

/**
 * bfq_put_idle_entity - release the idle tree ref of an entity.
 * @st: service tree for the entity.
 * @entity: the entity being released.
 */
static void bfq_put_idle_entity(struct bfq_service_tree *st,
				struct bfq_entity *entity)
{
	bfq_idle_extract(st, entity);
	bfq_forget_entity(st, entity);
}

/**
 * bfq_forget_idle - update the idle tree if necessary.
 * @st: the service tree to act upon.
 *
 * To preserve the global O(log N) complexity we only remove one entry here;
 * as the idle tree will not grow indefinitely this can be done safely.
 */
static void bfq_forget_idle(struct bfq_service_tree *st)
{
	struct bfq_entity *first_idle = st->first_idle;
	struct bfq_entity *last_idle = st->last_idle;

	if (RB_EMPTY_ROOT(&st->active) && last_idle != NULL &&
	    !bfq_gt(last_idle->finish, st->vtime)) {
		/*
		 * Forget the whole idle tree, increasing the vtime past
		 * the last finish time of idle entities.
		 */
		st->vtime = last_idle->finish;
	}

	if (first_idle != NULL && !bfq_gt(first_idle->finish, st->vtime))
		bfq_put_idle_entity(st, first_idle);
}

static struct bfq_service_tree *
__bfq_entity_update_weight_prio(struct bfq_service_tree *old_st,
			 struct bfq_entity *entity)
{
	struct bfq_service_tree *new_st = old_st;

	if (entity->ioprio_changed) {
		struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

		BUG_ON(old_st->wsum < entity->weight);
		old_st->wsum -= entity->weight;

		if (entity->new_weight != entity->orig_weight) {
			entity->orig_weight = entity->new_weight;
			entity->ioprio =
				bfq_weight_to_ioprio(entity->orig_weight);
		} else if (entity->new_ioprio != entity->ioprio) {
			entity->ioprio = entity->new_ioprio;
			entity->orig_weight =
					bfq_ioprio_to_weight(entity->ioprio);
		} else
			entity->new_weight = entity->orig_weight =
				bfq_ioprio_to_weight(entity->ioprio);

		entity->ioprio_class = entity->new_ioprio_class;
		entity->ioprio_changed = 0;

		/*
		 * NOTE: here we may be changing the weight too early,
		 * this will cause unfairness.  The correct approach
		 * would have required additional complexity to defer
		 * weight changes to the proper time instants (i.e.,
		 * when entity->finish <= old_st->vtime).
		 */
		new_st = bfq_entity_service_tree(entity);
		entity->weight = entity->orig_weight *
			(bfqq != NULL ? bfqq->raising_coeff : 1);
		new_st->wsum += entity->weight;

		if (new_st != old_st)
			entity->start = new_st->vtime;
	}

	return new_st;
}

/**
 * bfq_bfqq_served - update the scheduler status after selection for service.
 * @bfqq: the queue being served.
 * @served: bytes to transfer.
 *
 * NOTE: this can be optimized, as the timestamps of upper level entities
 * are synchronized every time a new bfqq is selected for service.  By now,
 * we keep it to better check consistency.
 */
static void bfq_bfqq_served(struct bfq_queue *bfqq, unsigned long served)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_service_tree *st;

	for_each_entity(entity) {
		st = bfq_entity_service_tree(entity);

		entity->service += served;
		BUG_ON(entity->service > entity->budget);
		BUG_ON(st->wsum == 0);

		st->vtime += bfq_delta(served, st->wsum);
		bfq_forget_idle(st);
	}
	bfq_log_bfqq(bfqq->bfqd, bfqq, "bfqq_served %lu secs", served);
}

/**
 * bfq_bfqq_charge_full_budget - set the service to the entity budget.
 * @bfqq: the queue that needs a service update.
 *
 * When it's not possible to be fair in the service domain, because
 * a queue is not consuming its budget fast enough (the meaning of
 * fast depends on the timeout parameter), we charge it a full
 * budget.  In this way we should obtain a sort of time-domain
 * fairness among all the seeky/slow queues.
 */
static inline void bfq_bfqq_charge_full_budget(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "charge_full_budget");

	bfq_bfqq_served(bfqq, entity->budget - entity->service);
}

/**
 * __bfq_activate_entity - activate an entity.
 * @entity: the entity being activated.
 *
 * Called whenever an entity is activated, i.e., it is not active and one
 * of its children receives a new request, or has to be reactivated due to
 * budget exhaustion.  It uses the current budget of the entity (and the
 * service received if @entity is active) of the queue to calculate its
 * timestamps.
 */
static void __bfq_activate_entity(struct bfq_entity *entity)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);

	if (entity == sd->active_entity) {
		BUG_ON(entity->tree != NULL);
		/*
		 * If we are requeueing the current entity we have
		 * to take care of not charging to it service it has
		 * not received.
		 */
		bfq_calc_finish(entity, entity->service);
		entity->start = entity->finish;
		sd->active_entity = NULL;
	} else if (entity->tree == &st->active) {
		/*
		 * Requeueing an entity due to a change of some
		 * next_active entity below it.  We reuse the old
		 * start time.
		 */
		bfq_active_extract(st, entity);
	} else if (entity->tree == &st->idle) {
		/*
		 * Must be on the idle tree, bfq_idle_extract() will
		 * check for that.
		 */
		bfq_idle_extract(st, entity);
		entity->start = bfq_gt(st->vtime, entity->finish) ?
				       st->vtime : entity->finish;
	} else {
		/*
		 * The finish time of the entity may be invalid, and
		 * it is in the past for sure, otherwise the queue
		 * would have been on the idle tree.
		 */
		entity->start = st->vtime;
		st->wsum += entity->weight;
		bfq_get_entity(entity);

		BUG_ON(entity->on_st);
		entity->on_st = 1;
	}

	st = __bfq_entity_update_weight_prio(st, entity);
	bfq_calc_finish(entity, entity->budget);
	bfq_active_insert(st, entity);
}

/**
 * bfq_activate_entity - activate an entity and its ancestors if necessary.
 * @entity: the entity to activate.
 *
 * Activate @entity and all the entities on the path from it to the root.
 */
static void bfq_activate_entity(struct bfq_entity *entity)
{
	struct bfq_sched_data *sd;

	for_each_entity(entity) {
		__bfq_activate_entity(entity);

		sd = entity->sched_data;
		if (!bfq_update_next_active(sd))
			/*
			 * No need to propagate the activation to the
			 * upper entities, as they will be updated when
			 * the active entity is rescheduled.
			 */
			break;
	}
}

/**
 * __bfq_deactivate_entity - deactivate an entity from its service tree.
 * @entity: the entity to deactivate.
 * @requeue: if false, the entity will not be put into the idle tree.
 *
 * Deactivate an entity, independently from its previous state.  If the
 * entity was not on a service tree just return, otherwise if it is on
 * any scheduler tree, extract it from that tree, and if necessary
 * and if the caller did not specify @requeue, put it on the idle tree.
 *
 * Return %1 if the caller should update the entity hierarchy, i.e.,
 * if the entity was under service or if it was the next_active for
 * its sched_data; return %0 otherwise.
 */
static int __bfq_deactivate_entity(struct bfq_entity *entity, int requeue)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	int was_active = entity == sd->active_entity;
	int ret = 0;

	if (!entity->on_st)
		return 0;

	BUG_ON(was_active && entity->tree != NULL);

	if (was_active) {
		bfq_calc_finish(entity, entity->service);
		sd->active_entity = NULL;
	} else if (entity->tree == &st->active)
		bfq_active_extract(st, entity);
	else if (entity->tree == &st->idle)
		bfq_idle_extract(st, entity);
	else if (entity->tree != NULL)
		BUG();

	if (was_active || sd->next_active == entity)
		ret = bfq_update_next_active(sd);

	if (!requeue || !bfq_gt(entity->finish, st->vtime))
		bfq_forget_entity(st, entity);
	else
		bfq_idle_insert(st, entity);

	BUG_ON(sd->active_entity == entity);
	BUG_ON(sd->next_active == entity);

	return ret;
}

/**
 * bfq_deactivate_entity - deactivate an entity.
 * @entity: the entity to deactivate.
 * @requeue: true if the entity can be put on the idle tree
 */
static void bfq_deactivate_entity(struct bfq_entity *entity, int requeue)
{
	struct bfq_sched_data *sd;
	struct bfq_entity *parent;

	for_each_entity_safe(entity, parent) {
		sd = entity->sched_data;

		if (!__bfq_deactivate_entity(entity, requeue))
			/*
			 * The parent entity is still backlogged, and
			 * we don't need to update it as it is still
			 * under service.
			 */
			break;

		if (sd->next_active != NULL)
			/*
			 * The parent entity is still backlogged and
			 * the budgets on the path towards the root
			 * need to be updated.
			 */
			goto update;

		/*
		 * If we reach there the parent is no more backlogged and
		 * we want to propagate the dequeue upwards.
		 */
		requeue = 1;
	}

	return;

update:
	entity = parent;
	for_each_entity(entity) {
		__bfq_activate_entity(entity);

		sd = entity->sched_data;
		if (!bfq_update_next_active(sd))
			break;
	}
}

/**
 * bfq_update_vtime - update vtime if necessary.
 * @st: the service tree to act upon.
 *
 * If necessary update the service tree vtime to have at least one
 * eligible entity, skipping to its start time.  Assumes that the
 * active tree of the device is not empty.
 *
 * NOTE: this hierarchical implementation updates vtimes quite often,
 * we may end up with reactivated tasks getting timestamps after a
 * vtime skip done because we needed a ->first_active entity on some
 * intermediate node.
 */
static void bfq_update_vtime(struct bfq_service_tree *st)
{
	struct bfq_entity *entry;
	struct rb_node *node = st->active.rb_node;

	entry = rb_entry(node, struct bfq_entity, rb_node);
	if (bfq_gt(entry->min_start, st->vtime)) {
		st->vtime = entry->min_start;
		bfq_forget_idle(st);
	}
}

/**
 * bfq_first_active - find the eligible entity with the smallest finish time
 * @st: the service tree to select from.
 *
 * This function searches the first schedulable entity, starting from the
 * root of the tree and going on the left every time on this side there is
 * a subtree with at least one eligible (start >= vtime) entity.  The path
 * on the right is followed only if a) the left subtree contains no eligible
 * entities and b) no eligible entity has been found yet.
 */
static struct bfq_entity *bfq_first_active_entity(struct bfq_service_tree *st)
{
	struct bfq_entity *entry, *first = NULL;
	struct rb_node *node = st->active.rb_node;

	while (node != NULL) {
		entry = rb_entry(node, struct bfq_entity, rb_node);
left:
		if (!bfq_gt(entry->start, st->vtime))
			first = entry;

		BUG_ON(bfq_gt(entry->min_start, st->vtime));

		if (node->rb_left != NULL) {
			entry = rb_entry(node->rb_left,
					 struct bfq_entity, rb_node);
			if (!bfq_gt(entry->min_start, st->vtime)) {
				node = node->rb_left;
				goto left;
			}
		}
		if (first != NULL)
			break;
		node = node->rb_right;
	}

	BUG_ON(first == NULL && !RB_EMPTY_ROOT(&st->active));
	return first;
}

/**
 * __bfq_lookup_next_entity - return the first eligible entity in @st.
 * @st: the service tree.
 *
 * Update the virtual time in @st and return the first eligible entity
 * it contains.
 */
static struct bfq_entity *__bfq_lookup_next_entity(struct bfq_service_tree *st,
						   bool force)
{
	struct bfq_entity *entity, *new_next_active = NULL;

	if (RB_EMPTY_ROOT(&st->active))
		return NULL;

	bfq_update_vtime(st);
	entity = bfq_first_active_entity(st);
	BUG_ON(bfq_gt(entity->start, st->vtime));

	/*
	 * If the chosen entity does not match with the sched_data's
	 * next_active and we are forcedly serving the IDLE priority
	 * class tree, bubble up budget update.
	 */
	if (unlikely(force && entity != entity->sched_data->next_active)) {
		new_next_active = entity;
		for_each_entity(new_next_active)
			bfq_update_budget(new_next_active);
	}

	return entity;
}

/**
 * bfq_lookup_next_entity - return the first eligible entity in @sd.
 * @sd: the sched_data.
 * @extract: if true the returned entity will be also extracted from @sd.
 *
 * NOTE: since we cache the next_active entity at each level of the
 * hierarchy, the complexity of the lookup can be decreased with
 * absolutely no effort just returning the cached next_active value;
 * we prefer to do full lookups to test the consistency of * the data
 * structures.
 */
static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd,
						 int extract,
						 struct bfq_data *bfqd)
{
	struct bfq_service_tree *st = sd->service_tree;
	struct bfq_entity *entity;
	int i=0;

	BUG_ON(sd->active_entity != NULL);

	if (bfqd != NULL &&
	    jiffies - bfqd->bfq_class_idle_last_service > BFQ_CL_IDLE_TIMEOUT) {
		entity = __bfq_lookup_next_entity(st + BFQ_IOPRIO_CLASSES - 1, true);
		if (entity != NULL) {
			i = BFQ_IOPRIO_CLASSES - 1;
			bfqd->bfq_class_idle_last_service = jiffies;
			sd->next_active = entity;
		}
	}
	for (; i < BFQ_IOPRIO_CLASSES; i++) {
		entity = __bfq_lookup_next_entity(st + i, false);
		if (entity != NULL) {
			if (extract) {
				bfq_check_next_active(sd, entity);
				bfq_active_extract(st + i, entity);
				sd->active_entity = entity;
				sd->next_active = NULL;
			}
			break;
		}
	}

	return entity;
}

/*
 * Get next queue for service.
 */
static struct bfq_queue *bfq_get_next_queue(struct bfq_data *bfqd)
{
	struct bfq_entity *entity = NULL;
	struct bfq_sched_data *sd;
	struct bfq_queue *bfqq;

	BUG_ON(bfqd->active_queue != NULL);

	if (bfqd->busy_queues == 0)
		return NULL;

	sd = &bfqd->root_group->sched_data;
	for (; sd != NULL; sd = entity->my_sched_data) {
		entity = bfq_lookup_next_entity(sd, 1, bfqd);
		BUG_ON(entity == NULL);
		entity->service = 0;
	}

	bfqq = bfq_entity_to_bfqq(entity);
	BUG_ON(bfqq == NULL);

	return bfqq;
}

/*
 * Forced extraction of the given queue.
 */
static void bfq_get_next_queue_forced(struct bfq_data *bfqd,
				      struct bfq_queue *bfqq)
{
	struct bfq_entity *entity;
	struct bfq_sched_data *sd;

	BUG_ON(bfqd->active_queue != NULL);

	entity = &bfqq->entity;
	/*
	 * Bubble up extraction/update from the leaf to the root.
	*/
	for_each_entity(entity) {
		sd = entity->sched_data;
		bfq_update_budget(entity);
		bfq_update_vtime(bfq_entity_service_tree(entity));
		bfq_active_extract(bfq_entity_service_tree(entity), entity);
		sd->active_entity = entity;
		sd->next_active = NULL;
		entity->service = 0;
	}

	return;
}

static void __bfq_bfqd_reset_active(struct bfq_data *bfqd)
{
	if (bfqd->active_cic != NULL) {
		put_io_context(bfqd->active_cic->ioc);
		bfqd->active_cic = NULL;
	}

	bfqd->active_queue = NULL;
	del_timer(&bfqd->idle_slice_timer);
}

static void bfq_deactivate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				int requeue)
{
	struct bfq_entity *entity = &bfqq->entity;

	if (bfqq == bfqd->active_queue)
		__bfq_bfqd_reset_active(bfqd);

	bfq_deactivate_entity(entity, requeue);
}

static void bfq_activate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_entity(entity);
}

/*
 * Called when the bfqq no longer has requests pending, remove it from
 * the service tree.
 */
static void bfq_del_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			      int requeue)
{
	BUG_ON(!bfq_bfqq_busy(bfqq));
	BUG_ON(!RB_EMPTY_ROOT(&bfqq->sort_list));

	bfq_log_bfqq(bfqd, bfqq, "del from busy");

	bfq_clear_bfqq_busy(bfqq);

	BUG_ON(bfqd->busy_queues == 0);
	bfqd->busy_queues--;

	bfq_deactivate_bfqq(bfqd, bfqq, requeue);
}

/*
 * Called when an inactive queue receives a new request.
 */
static void bfq_add_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	BUG_ON(bfq_bfqq_busy(bfqq));
	BUG_ON(bfqq == bfqd->active_queue);

	bfq_log_bfqq(bfqd, bfqq, "add to busy");

	bfq_activate_bfqq(bfqd, bfqq);

	bfq_mark_bfqq_busy(bfqq);
	bfqd->busy_queues++;
}
