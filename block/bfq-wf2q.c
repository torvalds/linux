/*
 * Hierarchical Budget Worst-case Fair Weighted Fair Queueing
 * (B-WF2Q+): hierarchical scheduling algorithm by which the BFQ I/O
 * scheduler schedules generic entities. The latter can represent
 * either single bfq queues (associated with processes) or groups of
 * bfq queues (associated with cgroups).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 */
#include "bfq-iosched.h"

/**
 * bfq_gt - compare two timestamps.
 * @a: first ts.
 * @b: second ts.
 *
 * Return @a > @b, dealing with wrapping correctly.
 */
static int bfq_gt(u64 a, u64 b)
{
	return (s64)(a - b) > 0;
}

static struct bfq_entity *bfq_root_active_entity(struct rb_root *tree)
{
	struct rb_node *node = tree->rb_node;

	return rb_entry(node, struct bfq_entity, rb_node);
}

static unsigned int bfq_class_idx(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	return bfqq ? bfqq->ioprio_class - 1 :
		BFQ_DEFAULT_GRP_CLASS - 1;
}

static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd);

static bool bfq_update_parent_budget(struct bfq_entity *next_in_service);

/**
 * bfq_update_next_in_service - update sd->next_in_service
 * @sd: sched_data for which to perform the update.
 * @new_entity: if not NULL, pointer to the entity whose activation,
 *		requeueing or repositionig triggered the invocation of
 *		this function.
 *
 * This function is called to update sd->next_in_service, which, in
 * its turn, may change as a consequence of the insertion or
 * extraction of an entity into/from one of the active trees of
 * sd. These insertions/extractions occur as a consequence of
 * activations/deactivations of entities, with some activations being
 * 'true' activations, and other activations being requeueings (i.e.,
 * implementing the second, requeueing phase of the mechanism used to
 * reposition an entity in its active tree; see comments on
 * __bfq_activate_entity and __bfq_requeue_entity for details). In
 * both the last two activation sub-cases, new_entity points to the
 * just activated or requeued entity.
 *
 * Returns true if sd->next_in_service changes in such a way that
 * entity->parent may become the next_in_service for its parent
 * entity.
 */
static bool bfq_update_next_in_service(struct bfq_sched_data *sd,
				       struct bfq_entity *new_entity)
{
	struct bfq_entity *next_in_service = sd->next_in_service;
	bool parent_sched_may_change = false;

	/*
	 * If this update is triggered by the activation, requeueing
	 * or repositiong of an entity that does not coincide with
	 * sd->next_in_service, then a full lookup in the active tree
	 * can be avoided. In fact, it is enough to check whether the
	 * just-modified entity has a higher priority than
	 * sd->next_in_service, or, even if it has the same priority
	 * as sd->next_in_service, is eligible and has a lower virtual
	 * finish time than sd->next_in_service. If this compound
	 * condition holds, then the new entity becomes the new
	 * next_in_service. Otherwise no change is needed.
	 */
	if (new_entity && new_entity != sd->next_in_service) {
		/*
		 * Flag used to decide whether to replace
		 * sd->next_in_service with new_entity. Tentatively
		 * set to true, and left as true if
		 * sd->next_in_service is NULL.
		 */
		bool replace_next = true;

		/*
		 * If there is already a next_in_service candidate
		 * entity, then compare class priorities or timestamps
		 * to decide whether to replace sd->service_tree with
		 * new_entity.
		 */
		if (next_in_service) {
			unsigned int new_entity_class_idx =
				bfq_class_idx(new_entity);
			struct bfq_service_tree *st =
				sd->service_tree + new_entity_class_idx;

			/*
			 * For efficiency, evaluate the most likely
			 * sub-condition first.
			 */
			replace_next =
				(new_entity_class_idx ==
				 bfq_class_idx(next_in_service)
				 &&
				 !bfq_gt(new_entity->start, st->vtime)
				 &&
				 bfq_gt(next_in_service->finish,
					new_entity->finish))
				||
				new_entity_class_idx <
				bfq_class_idx(next_in_service);
		}

		if (replace_next)
			next_in_service = new_entity;
	} else /* invoked because of a deactivation: lookup needed */
		next_in_service = bfq_lookup_next_entity(sd);

	if (next_in_service) {
		parent_sched_may_change = !sd->next_in_service ||
			bfq_update_parent_budget(next_in_service);
	}

	sd->next_in_service = next_in_service;

	if (!next_in_service)
		return parent_sched_may_change;

	return parent_sched_may_change;
}

#ifdef CONFIG_BFQ_GROUP_IOSCHED

struct bfq_group *bfq_bfqq_to_bfqg(struct bfq_queue *bfqq)
{
	struct bfq_entity *group_entity = bfqq->entity.parent;

	if (!group_entity)
		group_entity = &bfqq->bfqd->root_group->entity;

	return container_of(group_entity, struct bfq_group, entity);
}

/*
 * Returns true if this budget changes may let next_in_service->parent
 * become the next_in_service entity for its parent entity.
 */
static bool bfq_update_parent_budget(struct bfq_entity *next_in_service)
{
	struct bfq_entity *bfqg_entity;
	struct bfq_group *bfqg;
	struct bfq_sched_data *group_sd;
	bool ret = false;

	group_sd = next_in_service->sched_data;

	bfqg = container_of(group_sd, struct bfq_group, sched_data);
	/*
	 * bfq_group's my_entity field is not NULL only if the group
	 * is not the root group. We must not touch the root entity
	 * as it must never become an in-service entity.
	 */
	bfqg_entity = bfqg->my_entity;
	if (bfqg_entity) {
		if (bfqg_entity->budget > next_in_service->budget)
			ret = true;
		bfqg_entity->budget = next_in_service->budget;
	}

	return ret;
}

/*
 * This function tells whether entity stops being a candidate for next
 * service, according to the following logic.
 *
 * This function is invoked for an entity that is about to be set in
 * service. If such an entity is a queue, then the entity is no longer
 * a candidate for next service (i.e, a candidate entity to serve
 * after the in-service entity is expired). The function then returns
 * true.
 *
 * In contrast, the entity could stil be a candidate for next service
 * if it is not a queue, and has more than one child. In fact, even if
 * one of its children is about to be set in service, other children
 * may still be the next to serve. As a consequence, a non-queue
 * entity is not a candidate for next-service only if it has only one
 * child. And only if this condition holds, then the function returns
 * true for a non-queue entity.
 */
static bool bfq_no_longer_next_in_service(struct bfq_entity *entity)
{
	struct bfq_group *bfqg;

	if (bfq_entity_to_bfqq(entity))
		return true;

	bfqg = container_of(entity, struct bfq_group, entity);

	if (bfqg->active_entities == 1)
		return true;

	return false;
}

#else /* CONFIG_BFQ_GROUP_IOSCHED */

struct bfq_group *bfq_bfqq_to_bfqg(struct bfq_queue *bfqq)
{
	return bfqq->bfqd->root_group;
}

static bool bfq_update_parent_budget(struct bfq_entity *next_in_service)
{
	return false;
}

static bool bfq_no_longer_next_in_service(struct bfq_entity *entity)
{
	return true;
}

#endif /* CONFIG_BFQ_GROUP_IOSCHED */

/*
 * Shift for timestamp calculations.  This actually limits the maximum
 * service allowed in one timestamp delta (small shift values increase it),
 * the maximum total weight that can be used for the queues in the system
 * (big shift values increase it), and the period of virtual time
 * wraparounds.
 */
#define WFQ_SERVICE_SHIFT	22

struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = NULL;

	if (!entity->my_sched_data)
		bfqq = container_of(entity, struct bfq_queue, entity);

	return bfqq;
}


/**
 * bfq_delta - map service into the virtual time domain.
 * @service: amount of service.
 * @weight: scale factor (weight of an entity or weight sum).
 */
static u64 bfq_delta(unsigned long service, unsigned long weight)
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
static void bfq_calc_finish(struct bfq_entity *entity, unsigned long service)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->finish = entity->start +
		bfq_delta(service, entity->weight);

	if (bfqq) {
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
struct bfq_entity *bfq_entity_of(struct rb_node *node)
{
	struct bfq_entity *entity = NULL;

	if (node)
		entity = rb_entry(node, struct bfq_entity, rb_node);

	return entity;
}

/**
 * bfq_extract - remove an entity from a tree.
 * @root: the tree root.
 * @entity: the entity to remove.
 */
static void bfq_extract(struct rb_root *root, struct bfq_entity *entity)
{
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

	if (entity == st->first_idle) {
		next = rb_next(&entity->rb_node);
		st->first_idle = bfq_entity_of(next);
	}

	if (entity == st->last_idle) {
		next = rb_prev(&entity->rb_node);
		st->last_idle = bfq_entity_of(next);
	}

	bfq_extract(&st->idle, entity);

	if (bfqq)
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

	while (*node) {
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
static void bfq_update_min(struct bfq_entity *entity, struct rb_node *node)
{
	struct bfq_entity *child;

	if (node) {
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
static void bfq_update_active_node(struct rb_node *node)
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
	if (!parent)
		return;

	if (node == parent->rb_left && parent->rb_right)
		bfq_update_active_node(parent->rb_right);
	else if (parent->rb_left)
		bfq_update_active_node(parent->rb_left);

	node = parent;
	goto up;
}

/**
 * bfq_active_insert - insert an entity in the active tree of its
 *                     group/device.
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
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_sched_data *sd = NULL;
	struct bfq_group *bfqg = NULL;
	struct bfq_data *bfqd = NULL;
#endif

	bfq_insert(&st->active, entity);

	if (node->rb_left)
		node = node->rb_left;
	else if (node->rb_right)
		node = node->rb_right;

	bfq_update_active_tree(node);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	sd = entity->sched_data;
	bfqg = container_of(sd, struct bfq_group, sched_data);
	bfqd = (struct bfq_data *)bfqg->bfqd;
#endif
	if (bfqq)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->active_list);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	else /* bfq_group */
		bfq_weights_tree_add(bfqd, entity, &bfqd->group_weights_tree);

	if (bfqg != bfqd->root_group)
		bfqg->active_entities++;
#endif
}

/**
 * bfq_ioprio_to_weight - calc a weight from an ioprio.
 * @ioprio: the ioprio value to convert.
 */
unsigned short bfq_ioprio_to_weight(int ioprio)
{
	return (IOPRIO_BE_NR - ioprio) * BFQ_WEIGHT_CONVERSION_COEFF;
}

/**
 * bfq_weight_to_ioprio - calc an ioprio from a weight.
 * @weight: the weight value to convert.
 *
 * To preserve as much as possible the old only-ioprio user interface,
 * 0 is used as an escape ioprio value for weights (numerically) equal or
 * larger than IOPRIO_BE_NR * BFQ_WEIGHT_CONVERSION_COEFF.
 */
static unsigned short bfq_weight_to_ioprio(int weight)
{
	return max_t(int, 0,
		     IOPRIO_BE_NR * BFQ_WEIGHT_CONVERSION_COEFF - weight);
}

static void bfq_get_entity(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	if (bfqq) {
		bfqq->ref++;
		bfq_log_bfqq(bfqq->bfqd, bfqq, "get_entity: %p %d",
			     bfqq, bfqq->ref);
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

	if (!node->rb_right && !node->rb_left)
		deepest = rb_parent(node);
	else if (!node->rb_right)
		deepest = node->rb_left;
	else if (!node->rb_left)
		deepest = node->rb_right;
	else {
		deepest = rb_next(node);
		if (deepest->rb_right)
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
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_sched_data *sd = NULL;
	struct bfq_group *bfqg = NULL;
	struct bfq_data *bfqd = NULL;
#endif

	node = bfq_find_deepest(&entity->rb_node);
	bfq_extract(&st->active, entity);

	if (node)
		bfq_update_active_tree(node);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	sd = entity->sched_data;
	bfqg = container_of(sd, struct bfq_group, sched_data);
	bfqd = (struct bfq_data *)bfqg->bfqd;
#endif
	if (bfqq)
		list_del(&bfqq->bfqq_list);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	else /* bfq_group */
		bfq_weights_tree_remove(bfqd, entity,
					&bfqd->group_weights_tree);

	if (bfqg != bfqd->root_group)
		bfqg->active_entities--;
#endif
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

	if (!first_idle || bfq_gt(first_idle->finish, entity->finish))
		st->first_idle = entity;
	if (!last_idle || bfq_gt(entity->finish, last_idle->finish))
		st->last_idle = entity;

	bfq_insert(&st->idle, entity);

	if (bfqq)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->idle_list);
}

/**
 * bfq_forget_entity - do not consider entity any longer for scheduling
 * @st: the service tree.
 * @entity: the entity being removed.
 * @is_in_service: true if entity is currently the in-service entity.
 *
 * Forget everything about @entity. In addition, if entity represents
 * a queue, and the latter is not in service, then release the service
 * reference to the queue (the one taken through bfq_get_entity). In
 * fact, in this case, there is really no more service reference to
 * the queue, as the latter is also outside any service tree. If,
 * instead, the queue is in service, then __bfq_bfqd_reset_in_service
 * will take care of putting the reference when the queue finally
 * stops being served.
 */
static void bfq_forget_entity(struct bfq_service_tree *st,
			      struct bfq_entity *entity,
			      bool is_in_service)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->on_st = false;
	st->wsum -= entity->weight;
	if (bfqq && !is_in_service)
		bfq_put_queue(bfqq);
}

/**
 * bfq_put_idle_entity - release the idle tree ref of an entity.
 * @st: service tree for the entity.
 * @entity: the entity being released.
 */
void bfq_put_idle_entity(struct bfq_service_tree *st, struct bfq_entity *entity)
{
	bfq_idle_extract(st, entity);
	bfq_forget_entity(st, entity,
			  entity == entity->sched_data->in_service_entity);
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

	if (RB_EMPTY_ROOT(&st->active) && last_idle &&
	    !bfq_gt(last_idle->finish, st->vtime)) {
		/*
		 * Forget the whole idle tree, increasing the vtime past
		 * the last finish time of idle entities.
		 */
		st->vtime = last_idle->finish;
	}

	if (first_idle && !bfq_gt(first_idle->finish, st->vtime))
		bfq_put_idle_entity(st, first_idle);
}

struct bfq_service_tree *bfq_entity_service_tree(struct bfq_entity *entity)
{
	struct bfq_sched_data *sched_data = entity->sched_data;
	unsigned int idx = bfq_class_idx(entity);

	return sched_data->service_tree + idx;
}


struct bfq_service_tree *
__bfq_entity_update_weight_prio(struct bfq_service_tree *old_st,
				struct bfq_entity *entity)
{
	struct bfq_service_tree *new_st = old_st;

	if (entity->prio_changed) {
		struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
		unsigned int prev_weight, new_weight;
		struct bfq_data *bfqd = NULL;
		struct rb_root *root;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		struct bfq_sched_data *sd;
		struct bfq_group *bfqg;
#endif

		if (bfqq)
			bfqd = bfqq->bfqd;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		else {
			sd = entity->my_sched_data;
			bfqg = container_of(sd, struct bfq_group, sched_data);
			bfqd = (struct bfq_data *)bfqg->bfqd;
		}
#endif

		old_st->wsum -= entity->weight;

		if (entity->new_weight != entity->orig_weight) {
			if (entity->new_weight < BFQ_MIN_WEIGHT ||
			    entity->new_weight > BFQ_MAX_WEIGHT) {
				pr_crit("update_weight_prio: new_weight %d\n",
					entity->new_weight);
				if (entity->new_weight < BFQ_MIN_WEIGHT)
					entity->new_weight = BFQ_MIN_WEIGHT;
				else
					entity->new_weight = BFQ_MAX_WEIGHT;
			}
			entity->orig_weight = entity->new_weight;
			if (bfqq)
				bfqq->ioprio =
				  bfq_weight_to_ioprio(entity->orig_weight);
		}

		if (bfqq)
			bfqq->ioprio_class = bfqq->new_ioprio_class;
		entity->prio_changed = 0;

		/*
		 * NOTE: here we may be changing the weight too early,
		 * this will cause unfairness.  The correct approach
		 * would have required additional complexity to defer
		 * weight changes to the proper time instants (i.e.,
		 * when entity->finish <= old_st->vtime).
		 */
		new_st = bfq_entity_service_tree(entity);

		prev_weight = entity->weight;
		new_weight = entity->orig_weight *
			     (bfqq ? bfqq->wr_coeff : 1);
		/*
		 * If the weight of the entity changes, remove the entity
		 * from its old weight counter (if there is a counter
		 * associated with the entity), and add it to the counter
		 * associated with its new weight.
		 */
		if (prev_weight != new_weight) {
			root = bfqq ? &bfqd->queue_weights_tree :
				      &bfqd->group_weights_tree;
			bfq_weights_tree_remove(bfqd, entity, root);
		}
		entity->weight = new_weight;
		/*
		 * Add the entity to its weights tree only if it is
		 * not associated with a weight-raised queue.
		 */
		if (prev_weight != new_weight &&
		    (bfqq ? bfqq->wr_coeff == 1 : 1))
			/* If we get here, root has been initialized. */
			bfq_weights_tree_add(bfqd, entity, root);

		new_st->wsum += entity->weight;

		if (new_st != old_st)
			entity->start = new_st->vtime;
	}

	return new_st;
}

/**
 * bfq_bfqq_served - update the scheduler status after selection for
 *                   service.
 * @bfqq: the queue being served.
 * @served: bytes to transfer.
 *
 * NOTE: this can be optimized, as the timestamps of upper level entities
 * are synchronized every time a new bfqq is selected for service.  By now,
 * we keep it to better check consistency.
 */
void bfq_bfqq_served(struct bfq_queue *bfqq, int served)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_service_tree *st;

	for_each_entity(entity) {
		st = bfq_entity_service_tree(entity);

		entity->service += served;

		st->vtime += bfq_delta(served, st->wsum);
		bfq_forget_idle(st);
	}
	bfqg_stats_set_start_empty_time(bfqq_group(bfqq));
	bfq_log_bfqq(bfqq->bfqd, bfqq, "bfqq_served %d secs", served);
}

/**
 * bfq_bfqq_charge_time - charge an amount of service equivalent to the length
 *			  of the time interval during which bfqq has been in
 *			  service.
 * @bfqd: the device
 * @bfqq: the queue that needs a service update.
 * @time_ms: the amount of time during which the queue has received service
 *
 * If a queue does not consume its budget fast enough, then providing
 * the queue with service fairness may impair throughput, more or less
 * severely. For this reason, queues that consume their budget slowly
 * are provided with time fairness instead of service fairness. This
 * goal is achieved through the BFQ scheduling engine, even if such an
 * engine works in the service, and not in the time domain. The trick
 * is charging these queues with an inflated amount of service, equal
 * to the amount of service that they would have received during their
 * service slot if they had been fast, i.e., if their requests had
 * been dispatched at a rate equal to the estimated peak rate.
 *
 * It is worth noting that time fairness can cause important
 * distortions in terms of bandwidth distribution, on devices with
 * internal queueing. The reason is that I/O requests dispatched
 * during the service slot of a queue may be served after that service
 * slot is finished, and may have a total processing time loosely
 * correlated with the duration of the service slot. This is
 * especially true for short service slots.
 */
void bfq_bfqq_charge_time(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  unsigned long time_ms)
{
	struct bfq_entity *entity = &bfqq->entity;
	int tot_serv_to_charge = entity->service;
	unsigned int timeout_ms = jiffies_to_msecs(bfq_timeout);

	if (time_ms > 0 && time_ms < timeout_ms)
		tot_serv_to_charge =
			(bfqd->bfq_max_budget * time_ms) / timeout_ms;

	if (tot_serv_to_charge < entity->service)
		tot_serv_to_charge = entity->service;

	/* Increase budget to avoid inconsistencies */
	if (tot_serv_to_charge > entity->budget)
		entity->budget = tot_serv_to_charge;

	bfq_bfqq_served(bfqq,
			max_t(int, 0, tot_serv_to_charge - entity->service));
}

static void bfq_update_fin_time_enqueue(struct bfq_entity *entity,
					struct bfq_service_tree *st,
					bool backshifted)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	st = __bfq_entity_update_weight_prio(st, entity);
	bfq_calc_finish(entity, entity->budget);

	/*
	 * If some queues enjoy backshifting for a while, then their
	 * (virtual) finish timestamps may happen to become lower and
	 * lower than the system virtual time.	In particular, if
	 * these queues often happen to be idle for short time
	 * periods, and during such time periods other queues with
	 * higher timestamps happen to be busy, then the backshifted
	 * timestamps of the former queues can become much lower than
	 * the system virtual time. In fact, to serve the queues with
	 * higher timestamps while the ones with lower timestamps are
	 * idle, the system virtual time may be pushed-up to much
	 * higher values than the finish timestamps of the idle
	 * queues. As a consequence, the finish timestamps of all new
	 * or newly activated queues may end up being much larger than
	 * those of lucky queues with backshifted timestamps. The
	 * latter queues may then monopolize the device for a lot of
	 * time. This would simply break service guarantees.
	 *
	 * To reduce this problem, push up a little bit the
	 * backshifted timestamps of the queue associated with this
	 * entity (only a queue can happen to have the backshifted
	 * flag set): just enough to let the finish timestamp of the
	 * queue be equal to the current value of the system virtual
	 * time. This may introduce a little unfairness among queues
	 * with backshifted timestamps, but it does not break
	 * worst-case fairness guarantees.
	 *
	 * As a special case, if bfqq is weight-raised, push up
	 * timestamps much less, to keep very low the probability that
	 * this push up causes the backshifted finish timestamps of
	 * weight-raised queues to become higher than the backshifted
	 * finish timestamps of non weight-raised queues.
	 */
	if (backshifted && bfq_gt(st->vtime, entity->finish)) {
		unsigned long delta = st->vtime - entity->finish;

		if (bfqq)
			delta /= bfqq->wr_coeff;

		entity->start += delta;
		entity->finish += delta;
	}

	bfq_active_insert(st, entity);
}

/**
 * __bfq_activate_entity - handle activation of entity.
 * @entity: the entity being activated.
 * @non_blocking_wait_rq: true if entity was waiting for a request
 *
 * Called for a 'true' activation, i.e., if entity is not active and
 * one of its children receives a new request.
 *
 * Basically, this function updates the timestamps of entity and
 * inserts entity into its active tree, ater possible extracting it
 * from its idle tree.
 */
static void __bfq_activate_entity(struct bfq_entity *entity,
				  bool non_blocking_wait_rq)
{
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	bool backshifted = false;
	unsigned long long min_vstart;

	/* See comments on bfq_fqq_update_budg_for_activation */
	if (non_blocking_wait_rq && bfq_gt(st->vtime, entity->finish)) {
		backshifted = true;
		min_vstart = entity->finish;
	} else
		min_vstart = st->vtime;

	if (entity->tree == &st->idle) {
		/*
		 * Must be on the idle tree, bfq_idle_extract() will
		 * check for that.
		 */
		bfq_idle_extract(st, entity);
		entity->start = bfq_gt(min_vstart, entity->finish) ?
			min_vstart : entity->finish;
	} else {
		/*
		 * The finish time of the entity may be invalid, and
		 * it is in the past for sure, otherwise the queue
		 * would have been on the idle tree.
		 */
		entity->start = min_vstart;
		st->wsum += entity->weight;
		/*
		 * entity is about to be inserted into a service tree,
		 * and then set in service: get a reference to make
		 * sure entity does not disappear until it is no
		 * longer in service or scheduled for service.
		 */
		bfq_get_entity(entity);

		entity->on_st = true;
	}

	bfq_update_fin_time_enqueue(entity, st, backshifted);
}

/**
 * __bfq_requeue_entity - handle requeueing or repositioning of an entity.
 * @entity: the entity being requeued or repositioned.
 *
 * Requeueing is needed if this entity stops being served, which
 * happens if a leaf descendant entity has expired. On the other hand,
 * repositioning is needed if the next_inservice_entity for the child
 * entity has changed. See the comments inside the function for
 * details.
 *
 * Basically, this function: 1) removes entity from its active tree if
 * present there, 2) updates the timestamps of entity and 3) inserts
 * entity back into its active tree (in the new, right position for
 * the new values of the timestamps).
 */
static void __bfq_requeue_entity(struct bfq_entity *entity)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);

	if (entity == sd->in_service_entity) {
		/*
		 * We are requeueing the current in-service entity,
		 * which may have to be done for one of the following
		 * reasons:
		 * - entity represents the in-service queue, and the
		 *   in-service queue is being requeued after an
		 *   expiration;
		 * - entity represents a group, and its budget has
		 *   changed because one of its child entities has
		 *   just been either activated or requeued for some
		 *   reason; the timestamps of the entity need then to
		 *   be updated, and the entity needs to be enqueued
		 *   or repositioned accordingly.
		 *
		 * In particular, before requeueing, the start time of
		 * the entity must be moved forward to account for the
		 * service that the entity has received while in
		 * service. This is done by the next instructions. The
		 * finish time will then be updated according to this
		 * new value of the start time, and to the budget of
		 * the entity.
		 */
		bfq_calc_finish(entity, entity->service);
		entity->start = entity->finish;
		/*
		 * In addition, if the entity had more than one child
		 * when set in service, then was not extracted from
		 * the active tree. This implies that the position of
		 * the entity in the active tree may need to be
		 * changed now, because we have just updated the start
		 * time of the entity, and we will update its finish
		 * time in a moment (the requeueing is then, more
		 * precisely, a repositioning in this case). To
		 * implement this repositioning, we: 1) dequeue the
		 * entity here, 2) update the finish time and
		 * requeue the entity according to the new
		 * timestamps below.
		 */
		if (entity->tree)
			bfq_active_extract(st, entity);
	} else { /* The entity is already active, and not in service */
		/*
		 * In this case, this function gets called only if the
		 * next_in_service entity below this entity has
		 * changed, and this change has caused the budget of
		 * this entity to change, which, finally implies that
		 * the finish time of this entity must be
		 * updated. Such an update may cause the scheduling,
		 * i.e., the position in the active tree, of this
		 * entity to change. We handle this change by: 1)
		 * dequeueing the entity here, 2) updating the finish
		 * time and requeueing the entity according to the new
		 * timestamps below. This is the same approach as the
		 * non-extracted-entity sub-case above.
		 */
		bfq_active_extract(st, entity);
	}

	bfq_update_fin_time_enqueue(entity, st, false);
}

static void __bfq_activate_requeue_entity(struct bfq_entity *entity,
					  struct bfq_sched_data *sd,
					  bool non_blocking_wait_rq)
{
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);

	if (sd->in_service_entity == entity || entity->tree == &st->active)
		 /*
		  * in service or already queued on the active tree,
		  * requeue or reposition
		  */
		__bfq_requeue_entity(entity);
	else
		/*
		 * Not in service and not queued on its active tree:
		 * the activity is idle and this is a true activation.
		 */
		__bfq_activate_entity(entity, non_blocking_wait_rq);
}


/**
 * bfq_activate_entity - activate or requeue an entity representing a bfq_queue,
 *			 and activate, requeue or reposition all ancestors
 *			 for which such an update becomes necessary.
 * @entity: the entity to activate.
 * @non_blocking_wait_rq: true if this entity was waiting for a request
 * @requeue: true if this is a requeue, which implies that bfqq is
 *	     being expired; thus ALL its ancestors stop being served and must
 *	     therefore be requeued
 */
static void bfq_activate_requeue_entity(struct bfq_entity *entity,
					bool non_blocking_wait_rq,
					bool requeue)
{
	struct bfq_sched_data *sd;

	for_each_entity(entity) {
		sd = entity->sched_data;
		__bfq_activate_requeue_entity(entity, sd, non_blocking_wait_rq);

		if (!bfq_update_next_in_service(sd, entity) && !requeue)
			break;
	}
}

/**
 * __bfq_deactivate_entity - deactivate an entity from its service tree.
 * @entity: the entity to deactivate.
 * @ins_into_idle_tree: if false, the entity will not be put into the
 *			idle tree.
 *
 * Deactivates an entity, independently from its previous state.  Must
 * be invoked only if entity is on a service tree. Extracts the entity
 * from that tree, and if necessary and allowed, puts it on the idle
 * tree.
 */
bool __bfq_deactivate_entity(struct bfq_entity *entity, bool ins_into_idle_tree)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	int is_in_service = entity == sd->in_service_entity;

	if (!entity->on_st) /* entity never activated, or already inactive */
		return false;

	if (is_in_service)
		bfq_calc_finish(entity, entity->service);

	if (entity->tree == &st->active)
		bfq_active_extract(st, entity);
	else if (!is_in_service && entity->tree == &st->idle)
		bfq_idle_extract(st, entity);

	if (!ins_into_idle_tree || !bfq_gt(entity->finish, st->vtime))
		bfq_forget_entity(st, entity, is_in_service);
	else
		bfq_idle_insert(st, entity);

	return true;
}

/**
 * bfq_deactivate_entity - deactivate an entity representing a bfq_queue.
 * @entity: the entity to deactivate.
 * @ins_into_idle_tree: true if the entity can be put on the idle tree
 */
static void bfq_deactivate_entity(struct bfq_entity *entity,
				  bool ins_into_idle_tree,
				  bool expiration)
{
	struct bfq_sched_data *sd;
	struct bfq_entity *parent = NULL;

	for_each_entity_safe(entity, parent) {
		sd = entity->sched_data;

		if (!__bfq_deactivate_entity(entity, ins_into_idle_tree)) {
			/*
			 * entity is not in any tree any more, so
			 * this deactivation is a no-op, and there is
			 * nothing to change for upper-level entities
			 * (in case of expiration, this can never
			 * happen).
			 */
			return;
		}

		if (sd->next_in_service == entity)
			/*
			 * entity was the next_in_service entity,
			 * then, since entity has just been
			 * deactivated, a new one must be found.
			 */
			bfq_update_next_in_service(sd, NULL);

		if (sd->next_in_service)
			/*
			 * The parent entity is still backlogged,
			 * because next_in_service is not NULL. So, no
			 * further upwards deactivation must be
			 * performed.  Yet, next_in_service has
			 * changed.  Then the schedule does need to be
			 * updated upwards.
			 */
			break;

		/*
		 * If we get here, then the parent is no more
		 * backlogged and we need to propagate the
		 * deactivation upwards. Thus let the loop go on.
		 */

		/*
		 * Also let parent be queued into the idle tree on
		 * deactivation, to preserve service guarantees, and
		 * assuming that who invoked this function does not
		 * need parent entities too to be removed completely.
		 */
		ins_into_idle_tree = true;
	}

	/*
	 * If the deactivation loop is fully executed, then there are
	 * no more entities to touch and next loop is not executed at
	 * all. Otherwise, requeue remaining entities if they are
	 * about to stop receiving service, or reposition them if this
	 * is not the case.
	 */
	entity = parent;
	for_each_entity(entity) {
		/*
		 * Invoke __bfq_requeue_entity on entity, even if
		 * already active, to requeue/reposition it in the
		 * active tree (because sd->next_in_service has
		 * changed)
		 */
		__bfq_requeue_entity(entity);

		sd = entity->sched_data;
		if (!bfq_update_next_in_service(sd, entity) &&
		    !expiration)
			/*
			 * next_in_service unchanged or not causing
			 * any change in entity->parent->sd, and no
			 * requeueing needed for expiration: stop
			 * here.
			 */
			break;
	}
}

/**
 * bfq_calc_vtime_jump - compute the value to which the vtime should jump,
 *                       if needed, to have at least one entity eligible.
 * @st: the service tree to act upon.
 *
 * Assumes that st is not empty.
 */
static u64 bfq_calc_vtime_jump(struct bfq_service_tree *st)
{
	struct bfq_entity *root_entity = bfq_root_active_entity(&st->active);

	if (bfq_gt(root_entity->min_start, st->vtime))
		return root_entity->min_start;

	return st->vtime;
}

static void bfq_update_vtime(struct bfq_service_tree *st, u64 new_value)
{
	if (new_value > st->vtime) {
		st->vtime = new_value;
		bfq_forget_idle(st);
	}
}

/**
 * bfq_first_active_entity - find the eligible entity with
 *                           the smallest finish time
 * @st: the service tree to select from.
 * @vtime: the system virtual to use as a reference for eligibility
 *
 * This function searches the first schedulable entity, starting from the
 * root of the tree and going on the left every time on this side there is
 * a subtree with at least one eligible (start >= vtime) entity. The path on
 * the right is followed only if a) the left subtree contains no eligible
 * entities and b) no eligible entity has been found yet.
 */
static struct bfq_entity *bfq_first_active_entity(struct bfq_service_tree *st,
						  u64 vtime)
{
	struct bfq_entity *entry, *first = NULL;
	struct rb_node *node = st->active.rb_node;

	while (node) {
		entry = rb_entry(node, struct bfq_entity, rb_node);
left:
		if (!bfq_gt(entry->start, vtime))
			first = entry;

		if (node->rb_left) {
			entry = rb_entry(node->rb_left,
					 struct bfq_entity, rb_node);
			if (!bfq_gt(entry->min_start, vtime)) {
				node = node->rb_left;
				goto left;
			}
		}
		if (first)
			break;
		node = node->rb_right;
	}

	return first;
}

/**
 * __bfq_lookup_next_entity - return the first eligible entity in @st.
 * @st: the service tree.
 *
 * If there is no in-service entity for the sched_data st belongs to,
 * then return the entity that will be set in service if:
 * 1) the parent entity this st belongs to is set in service;
 * 2) no entity belonging to such parent entity undergoes a state change
 * that would influence the timestamps of the entity (e.g., becomes idle,
 * becomes backlogged, changes its budget, ...).
 *
 * In this first case, update the virtual time in @st too (see the
 * comments on this update inside the function).
 *
 * In constrast, if there is an in-service entity, then return the
 * entity that would be set in service if not only the above
 * conditions, but also the next one held true: the currently
 * in-service entity, on expiration,
 * 1) gets a finish time equal to the current one, or
 * 2) is not eligible any more, or
 * 3) is idle.
 */
static struct bfq_entity *
__bfq_lookup_next_entity(struct bfq_service_tree *st, bool in_service)
{
	struct bfq_entity *entity;
	u64 new_vtime;

	if (RB_EMPTY_ROOT(&st->active))
		return NULL;

	/*
	 * Get the value of the system virtual time for which at
	 * least one entity is eligible.
	 */
	new_vtime = bfq_calc_vtime_jump(st);

	/*
	 * If there is no in-service entity for the sched_data this
	 * active tree belongs to, then push the system virtual time
	 * up to the value that guarantees that at least one entity is
	 * eligible. If, instead, there is an in-service entity, then
	 * do not make any such update, because there is already an
	 * eligible entity, namely the in-service one (even if the
	 * entity is not on st, because it was extracted when set in
	 * service).
	 */
	if (!in_service)
		bfq_update_vtime(st, new_vtime);

	entity = bfq_first_active_entity(st, new_vtime);

	return entity;
}

/**
 * bfq_lookup_next_entity - return the first eligible entity in @sd.
 * @sd: the sched_data.
 *
 * This function is invoked when there has been a change in the trees
 * for sd, and we need know what is the new next entity after this
 * change.
 */
static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd)
{
	struct bfq_service_tree *st = sd->service_tree;
	struct bfq_service_tree *idle_class_st = st + (BFQ_IOPRIO_CLASSES - 1);
	struct bfq_entity *entity = NULL;
	int class_idx = 0;

	/*
	 * Choose from idle class, if needed to guarantee a minimum
	 * bandwidth to this class (and if there is some active entity
	 * in idle class). This should also mitigate
	 * priority-inversion problems in case a low priority task is
	 * holding file system resources.
	 */
	if (time_is_before_jiffies(sd->bfq_class_idle_last_service +
				   BFQ_CL_IDLE_TIMEOUT)) {
		if (!RB_EMPTY_ROOT(&idle_class_st->active))
			class_idx = BFQ_IOPRIO_CLASSES - 1;
		/* About to be served if backlogged, or not yet backlogged */
		sd->bfq_class_idle_last_service = jiffies;
	}

	/*
	 * Find the next entity to serve for the highest-priority
	 * class, unless the idle class needs to be served.
	 */
	for (; class_idx < BFQ_IOPRIO_CLASSES; class_idx++) {
		entity = __bfq_lookup_next_entity(st + class_idx,
						  sd->in_service_entity);

		if (entity)
			break;
	}

	if (!entity)
		return NULL;

	return entity;
}

bool next_queue_may_preempt(struct bfq_data *bfqd)
{
	struct bfq_sched_data *sd = &bfqd->root_group->sched_data;

	return sd->next_in_service != sd->in_service_entity;
}

/*
 * Get next queue for service.
 */
struct bfq_queue *bfq_get_next_queue(struct bfq_data *bfqd)
{
	struct bfq_entity *entity = NULL;
	struct bfq_sched_data *sd;
	struct bfq_queue *bfqq;

	if (bfqd->busy_queues == 0)
		return NULL;

	/*
	 * Traverse the path from the root to the leaf entity to
	 * serve. Set in service all the entities visited along the
	 * way.
	 */
	sd = &bfqd->root_group->sched_data;
	for (; sd ; sd = entity->my_sched_data) {
		/*
		 * WARNING. We are about to set the in-service entity
		 * to sd->next_in_service, i.e., to the (cached) value
		 * returned by bfq_lookup_next_entity(sd) the last
		 * time it was invoked, i.e., the last time when the
		 * service order in sd changed as a consequence of the
		 * activation or deactivation of an entity. In this
		 * respect, if we execute bfq_lookup_next_entity(sd)
		 * in this very moment, it may, although with low
		 * probability, yield a different entity than that
		 * pointed to by sd->next_in_service. This rare event
		 * happens in case there was no CLASS_IDLE entity to
		 * serve for sd when bfq_lookup_next_entity(sd) was
		 * invoked for the last time, while there is now one
		 * such entity.
		 *
		 * If the above event happens, then the scheduling of
		 * such entity in CLASS_IDLE is postponed until the
		 * service of the sd->next_in_service entity
		 * finishes. In fact, when the latter is expired,
		 * bfq_lookup_next_entity(sd) gets called again,
		 * exactly to update sd->next_in_service.
		 */

		/* Make next_in_service entity become in_service_entity */
		entity = sd->next_in_service;
		sd->in_service_entity = entity;

		/*
		 * Reset the accumulator of the amount of service that
		 * the entity is about to receive.
		 */
		entity->service = 0;

		/*
		 * If entity is no longer a candidate for next
		 * service, then we extract it from its active tree,
		 * for the following reason. To further boost the
		 * throughput in some special case, BFQ needs to know
		 * which is the next candidate entity to serve, while
		 * there is already an entity in service. In this
		 * respect, to make it easy to compute/update the next
		 * candidate entity to serve after the current
		 * candidate has been set in service, there is a case
		 * where it is necessary to extract the current
		 * candidate from its service tree. Such a case is
		 * when the entity just set in service cannot be also
		 * a candidate for next service. Details about when
		 * this conditions holds are reported in the comments
		 * on the function bfq_no_longer_next_in_service()
		 * invoked below.
		 */
		if (bfq_no_longer_next_in_service(entity))
			bfq_active_extract(bfq_entity_service_tree(entity),
					   entity);

		/*
		 * For the same reason why we may have just extracted
		 * entity from its active tree, we may need to update
		 * next_in_service for the sched_data of entity too,
		 * regardless of whether entity has been extracted.
		 * In fact, even if entity has not been extracted, a
		 * descendant entity may get extracted. Such an event
		 * would cause a change in next_in_service for the
		 * level of the descendant entity, and thus possibly
		 * back to upper levels.
		 *
		 * We cannot perform the resulting needed update
		 * before the end of this loop, because, to know which
		 * is the correct next-to-serve candidate entity for
		 * each level, we need first to find the leaf entity
		 * to set in service. In fact, only after we know
		 * which is the next-to-serve leaf entity, we can
		 * discover whether the parent entity of the leaf
		 * entity becomes the next-to-serve, and so on.
		 */

	}

	bfqq = bfq_entity_to_bfqq(entity);

	/*
	 * We can finally update all next-to-serve entities along the
	 * path from the leaf entity just set in service to the root.
	 */
	for_each_entity(entity) {
		struct bfq_sched_data *sd = entity->sched_data;

		if (!bfq_update_next_in_service(sd, NULL))
			break;
	}

	return bfqq;
}

void __bfq_bfqd_reset_in_service(struct bfq_data *bfqd)
{
	struct bfq_queue *in_serv_bfqq = bfqd->in_service_queue;
	struct bfq_entity *in_serv_entity = &in_serv_bfqq->entity;
	struct bfq_entity *entity = in_serv_entity;

	bfq_clear_bfqq_wait_request(in_serv_bfqq);
	hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
	bfqd->in_service_queue = NULL;

	/*
	 * When this function is called, all in-service entities have
	 * been properly deactivated or requeued, so we can safely
	 * execute the final step: reset in_service_entity along the
	 * path from entity to the root.
	 */
	for_each_entity(entity)
		entity->sched_data->in_service_entity = NULL;

	/*
	 * in_serv_entity is no longer in service, so, if it is in no
	 * service tree either, then release the service reference to
	 * the queue it represents (taken with bfq_get_entity).
	 */
	if (!in_serv_entity->on_st)
		bfq_put_queue(in_serv_bfqq);
}

void bfq_deactivate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			 bool ins_into_idle_tree, bool expiration)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_deactivate_entity(entity, ins_into_idle_tree, expiration);
}

void bfq_activate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_requeue_entity(entity, bfq_bfqq_non_blocking_wait_rq(bfqq),
				    false);
	bfq_clear_bfqq_non_blocking_wait_rq(bfqq);
}

void bfq_requeue_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_requeue_entity(entity, false,
				    bfqq == bfqd->in_service_queue);
}

/*
 * Called when the bfqq no longer has requests pending, remove it from
 * the service tree. As a special case, it can be invoked during an
 * expiration.
 */
void bfq_del_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		       bool expiration)
{
	bfq_log_bfqq(bfqd, bfqq, "del from busy");

	bfq_clear_bfqq_busy(bfqq);

	bfqd->busy_queues--;

	if (!bfqq->dispatched)
		bfq_weights_tree_remove(bfqd, &bfqq->entity,
					&bfqd->queue_weights_tree);

	if (bfqq->wr_coeff > 1)
		bfqd->wr_busy_queues--;

	bfqg_stats_update_dequeue(bfqq_group(bfqq));

	bfq_deactivate_bfqq(bfqd, bfqq, true, expiration);
}

/*
 * Called when an inactive queue receives a new request.
 */
void bfq_add_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqd, bfqq, "add to busy");

	bfq_activate_bfqq(bfqd, bfqq);

	bfq_mark_bfqq_busy(bfqq);
	bfqd->busy_queues++;

	if (!bfqq->dispatched)
		if (bfqq->wr_coeff == 1)
			bfq_weights_tree_add(bfqd, &bfqq->entity,
					     &bfqd->queue_weights_tree);

	if (bfqq->wr_coeff > 1)
		bfqd->wr_busy_queues++;
}
