// SPDX-License-Identifier: GPL-2.0
/*
 * NUMA support for s390
 *
 * A tree structure used for machine topology mangling
 *
 * Copyright IBM Corp. 2015
 */

#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <asm/numa.h>

#include "toptree.h"

/**
 * toptree_alloc - Allocate and initialize a new tree node.
 * @level: The node's vertical level; level 0 contains the leaves.
 * @id: ID number, explicitly not unique beyond scope of node's siblings
 *
 * Allocate a new tree node and initialize it.
 *
 * RETURNS:
 * Pointer to the new tree node or NULL on error
 */
struct toptree __ref *toptree_alloc(int level, int id)
{
	struct toptree *res;

	if (slab_is_available())
		res = kzalloc(sizeof(*res), GFP_KERNEL);
	else
		res = memblock_alloc(sizeof(*res), 8);
	if (!res)
		return res;

	INIT_LIST_HEAD(&res->children);
	INIT_LIST_HEAD(&res->sibling);
	cpumask_clear(&res->mask);
	res->level = level;
	res->id = id;
	return res;
}

/**
 * toptree_remove - Remove a tree node from a tree
 * @cand: Pointer to the node to remove
 *
 * The node is detached from its parent node. The parent node's
 * masks will be updated to reflect the loss of the child.
 */
static void toptree_remove(struct toptree *cand)
{
	struct toptree *oldparent;

	list_del_init(&cand->sibling);
	oldparent = cand->parent;
	cand->parent = NULL;
	toptree_update_mask(oldparent);
}

/**
 * toptree_free - discard a tree node
 * @cand: Pointer to the tree node to discard
 *
 * Checks if @cand is attached to a parent node. Detaches it
 * cleanly using toptree_remove. Possible children are freed
 * recursively. In the end @cand itself is freed.
 */
void __ref toptree_free(struct toptree *cand)
{
	struct toptree *child, *tmp;

	if (cand->parent)
		toptree_remove(cand);
	toptree_for_each_child_safe(child, tmp, cand)
		toptree_free(child);
	if (slab_is_available())
		kfree(cand);
	else
		memblock_free_early((unsigned long)cand, sizeof(*cand));
}

/**
 * toptree_update_mask - Update node bitmasks
 * @cand: Pointer to a tree node
 *
 * The node's cpumask will be updated by combining all children's
 * masks. Then toptree_update_mask is called recursively for the
 * parent if applicable.
 *
 * NOTE:
 * This must not be called on leaves. If called on a leaf, its
 * CPU mask is cleared and lost.
 */
void toptree_update_mask(struct toptree *cand)
{
	struct toptree *child;

	cpumask_clear(&cand->mask);
	list_for_each_entry(child, &cand->children, sibling)
		cpumask_or(&cand->mask, &cand->mask, &child->mask);
	if (cand->parent)
		toptree_update_mask(cand->parent);
}

/**
 * toptree_insert - Insert a tree node into tree
 * @cand: Pointer to the node to insert
 * @target: Pointer to the node to which @cand will added as a child
 *
 * Insert a tree node into a tree. Masks will be updated automatically.
 *
 * RETURNS:
 * 0 on success, -1 if NULL is passed as argument or the node levels
 * don't fit.
 */
static int toptree_insert(struct toptree *cand, struct toptree *target)
{
	if (!cand || !target)
		return -1;
	if (target->level != (cand->level + 1))
		return -1;
	list_add_tail(&cand->sibling, &target->children);
	cand->parent = target;
	toptree_update_mask(target);
	return 0;
}

/**
 * toptree_move_children - Move all child nodes of a node to a new place
 * @cand: Pointer to the node whose children are to be moved
 * @target: Pointer to the node to which @cand's children will be attached
 *
 * Take all child nodes of @cand and move them using toptree_move.
 */
static void toptree_move_children(struct toptree *cand, struct toptree *target)
{
	struct toptree *child, *tmp;

	toptree_for_each_child_safe(child, tmp, cand)
		toptree_move(child, target);
}

/**
 * toptree_unify - Merge children with same ID
 * @cand: Pointer to node whose direct children should be made unique
 *
 * When mangling the tree it is possible that a node has two or more children
 * which have the same ID. This routine merges these children into one and
 * moves all children of the merged nodes into the unified node.
 */
void toptree_unify(struct toptree *cand)
{
	struct toptree *child, *tmp, *cand_copy;

	/* Threads cannot be split, cores are not split */
	if (cand->level < 2)
		return;

	cand_copy = toptree_alloc(cand->level, 0);
	toptree_for_each_child_safe(child, tmp, cand) {
		struct toptree *tmpchild;

		if (!cpumask_empty(&child->mask)) {
			tmpchild = toptree_get_child(cand_copy, child->id);
			toptree_move_children(child, tmpchild);
		}
		toptree_free(child);
	}
	toptree_move_children(cand_copy, cand);
	toptree_free(cand_copy);

	toptree_for_each_child(child, cand)
		toptree_unify(child);
}

/**
 * toptree_move - Move a node to another context
 * @cand: Pointer to the node to move
 * @target: Pointer to the node where @cand should go
 *
 * In the easiest case @cand is exactly on the level below @target
 * and will be immediately moved to the target.
 *
 * If @target's level is not the direct parent level of @cand,
 * nodes for the missing levels are created and put between
 * @cand and @target. The "stacking" nodes' IDs are taken from
 * @cand's parents.
 *
 * After this it is likely to have redundant nodes in the tree
 * which are addressed by means of toptree_unify.
 */
void toptree_move(struct toptree *cand, struct toptree *target)
{
	struct toptree *stack_target, *real_insert_point, *ptr, *tmp;

	if (cand->level + 1 == target->level) {
		toptree_remove(cand);
		toptree_insert(cand, target);
		return;
	}

	real_insert_point = NULL;
	ptr = cand;
	stack_target = NULL;

	do {
		tmp = stack_target;
		stack_target = toptree_alloc(ptr->level + 1,
					     ptr->parent->id);
		toptree_insert(tmp, stack_target);
		if (!real_insert_point)
			real_insert_point = stack_target;
		ptr = ptr->parent;
	} while (stack_target->level < (target->level - 1));

	toptree_remove(cand);
	toptree_insert(cand, real_insert_point);
	toptree_insert(stack_target, target);
}

/**
 * toptree_get_child - Access a tree node's child by its ID
 * @cand: Pointer to tree node whose child is to access
 * @id: The desired child's ID
 *
 * @cand's children are searched for a child with matching ID.
 * If no match can be found, a new child with the desired ID
 * is created and returned.
 */
struct toptree *toptree_get_child(struct toptree *cand, int id)
{
	struct toptree *child;

	toptree_for_each_child(child, cand)
		if (child->id == id)
			return child;
	child = toptree_alloc(cand->level-1, id);
	toptree_insert(child, cand);
	return child;
}

/**
 * toptree_first - Find the first descendant on specified level
 * @context: Pointer to tree node whose descendants are to be used
 * @level: The level of interest
 *
 * RETURNS:
 * @context's first descendant on the specified level, or NULL
 * if there is no matching descendant
 */
struct toptree *toptree_first(struct toptree *context, int level)
{
	struct toptree *child, *tmp;

	if (context->level == level)
		return context;

	if (!list_empty(&context->children)) {
		list_for_each_entry(child, &context->children, sibling) {
			tmp = toptree_first(child, level);
			if (tmp)
				return tmp;
		}
	}
	return NULL;
}

/**
 * toptree_next_sibling - Return next sibling
 * @cur: Pointer to a tree node
 *
 * RETURNS:
 * If @cur has a parent and is not the last in the parent's children list,
 * the next sibling is returned. Or NULL when there are no siblings left.
 */
static struct toptree *toptree_next_sibling(struct toptree *cur)
{
	if (cur->parent == NULL)
		return NULL;

	if (cur == list_last_entry(&cur->parent->children,
				   struct toptree, sibling))
		return NULL;
	return (struct toptree *) list_next_entry(cur, sibling);
}

/**
 * toptree_next - Tree traversal function
 * @cur: Pointer to current element
 * @context: Pointer to the root node of the tree or subtree to
 * be traversed.
 * @level: The level of interest.
 *
 * RETURNS:
 * Pointer to the next node on level @level
 * or NULL when there is no next node.
 */
struct toptree *toptree_next(struct toptree *cur, struct toptree *context,
			     int level)
{
	struct toptree *cur_context, *tmp;

	if (!cur)
		return NULL;

	if (context->level == level)
		return NULL;

	tmp = toptree_next_sibling(cur);
	if (tmp != NULL)
		return tmp;

	cur_context = cur;
	while (cur_context->level < context->level - 1) {
		/* Step up */
		cur_context = cur_context->parent;
		/* Step aside */
		tmp = toptree_next_sibling(cur_context);
		if (tmp != NULL) {
			/* Step down */
			tmp = toptree_first(tmp, level);
			if (tmp != NULL)
				return tmp;
		}
	}
	return NULL;
}

/**
 * toptree_count - Count descendants on specified level
 * @context: Pointer to node whose descendants are to be considered
 * @level: Only descendants on the specified level will be counted
 *
 * RETURNS:
 * Number of descendants on the specified level
 */
int toptree_count(struct toptree *context, int level)
{
	struct toptree *cur;
	int cnt = 0;

	toptree_for_each(cur, context, level)
		cnt++;
	return cnt;
}
