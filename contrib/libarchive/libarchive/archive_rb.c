/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas <matt@3am-software.com>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Based on: NetBSD: rb.c,v 1.6 2010/04/30 13:58:09 joerg Exp
 */

#include "archive_platform.h"

#include <stddef.h>

#include "archive_rb.h"

/* Keep in sync with archive_rb.h */
#define	RB_DIR_LEFT		0
#define	RB_DIR_RIGHT		1
#define	RB_DIR_OTHER		1
#define	rb_left			rb_nodes[RB_DIR_LEFT]
#define	rb_right		rb_nodes[RB_DIR_RIGHT]

#define	RB_FLAG_POSITION	0x2
#define	RB_FLAG_RED		0x1
#define	RB_FLAG_MASK		(RB_FLAG_POSITION|RB_FLAG_RED)
#define	RB_FATHER(rb) \
    ((struct archive_rb_node *)((rb)->rb_info & ~RB_FLAG_MASK))
#define	RB_SET_FATHER(rb, father) \
    ((void)((rb)->rb_info = (uintptr_t)(father)|((rb)->rb_info & RB_FLAG_MASK)))

#define	RB_SENTINEL_P(rb)	((rb) == NULL)
#define	RB_LEFT_SENTINEL_P(rb)	RB_SENTINEL_P((rb)->rb_left)
#define	RB_RIGHT_SENTINEL_P(rb)	RB_SENTINEL_P((rb)->rb_right)
#define	RB_FATHER_SENTINEL_P(rb) RB_SENTINEL_P(RB_FATHER((rb)))
#define	RB_CHILDLESS_P(rb) \
    (RB_SENTINEL_P(rb) || (RB_LEFT_SENTINEL_P(rb) && RB_RIGHT_SENTINEL_P(rb)))
#define	RB_TWOCHILDREN_P(rb) \
    (!RB_SENTINEL_P(rb) && !RB_LEFT_SENTINEL_P(rb) && !RB_RIGHT_SENTINEL_P(rb))

#define	RB_POSITION(rb)	\
    (((rb)->rb_info & RB_FLAG_POSITION) ? RB_DIR_RIGHT : RB_DIR_LEFT)
#define	RB_RIGHT_P(rb)		(RB_POSITION(rb) == RB_DIR_RIGHT)
#define	RB_LEFT_P(rb)		(RB_POSITION(rb) == RB_DIR_LEFT)
#define	RB_RED_P(rb) 		(!RB_SENTINEL_P(rb) && ((rb)->rb_info & RB_FLAG_RED) != 0)
#define	RB_BLACK_P(rb) 		(RB_SENTINEL_P(rb) || ((rb)->rb_info & RB_FLAG_RED) == 0)
#define	RB_MARK_RED(rb) 	((void)((rb)->rb_info |= RB_FLAG_RED))
#define	RB_MARK_BLACK(rb) 	((void)((rb)->rb_info &= ~RB_FLAG_RED))
#define	RB_INVERT_COLOR(rb) 	((void)((rb)->rb_info ^= RB_FLAG_RED))
#define	RB_ROOT_P(rbt, rb)	((rbt)->rbt_root == (rb))
#define	RB_SET_POSITION(rb, position) \
    ((void)((position) ? ((rb)->rb_info |= RB_FLAG_POSITION) : \
    ((rb)->rb_info &= ~RB_FLAG_POSITION)))
#define	RB_ZERO_PROPERTIES(rb)	((void)((rb)->rb_info &= ~RB_FLAG_MASK))
#define	RB_COPY_PROPERTIES(dst, src) \
    ((void)((dst)->rb_info ^= ((dst)->rb_info ^ (src)->rb_info) & RB_FLAG_MASK))
#define RB_SWAP_PROPERTIES(a, b) do { \
    uintptr_t xorinfo = ((a)->rb_info ^ (b)->rb_info) & RB_FLAG_MASK; \
    (a)->rb_info ^= xorinfo; \
    (b)->rb_info ^= xorinfo; \
  } while (/*CONSTCOND*/ 0)

static void __archive_rb_tree_insert_rebalance(struct archive_rb_tree *,
    struct archive_rb_node *);
static void __archive_rb_tree_removal_rebalance(struct archive_rb_tree *,
    struct archive_rb_node *, unsigned int);

#define	RB_SENTINEL_NODE	NULL

#define T	1
#define	F	0

void
__archive_rb_tree_init(struct archive_rb_tree *rbt,
    const struct archive_rb_tree_ops *ops)
{
	rbt->rbt_ops = ops;
	*((struct archive_rb_node **)&rbt->rbt_root) = RB_SENTINEL_NODE;
}

struct archive_rb_node *
__archive_rb_tree_find_node(struct archive_rb_tree *rbt, const void *key)
{
	archive_rbto_compare_key_fn compare_key = rbt->rbt_ops->rbto_compare_key;
	struct archive_rb_node *parent = rbt->rbt_root;

	while (!RB_SENTINEL_P(parent)) {
		const signed int diff = (*compare_key)(parent, key);
		if (diff == 0)
			return parent;
		parent = parent->rb_nodes[diff > 0];
	}

	return NULL;
}
 
struct archive_rb_node *
__archive_rb_tree_find_node_geq(struct archive_rb_tree *rbt, const void *key)
{
	archive_rbto_compare_key_fn compare_key = rbt->rbt_ops->rbto_compare_key;
	struct archive_rb_node *parent = rbt->rbt_root;
	struct archive_rb_node *last = NULL;

	while (!RB_SENTINEL_P(parent)) {
		const signed int diff = (*compare_key)(parent, key);
		if (diff == 0)
			return parent;
		if (diff < 0)
			last = parent;
		parent = parent->rb_nodes[diff > 0];
	}

	return last;
}
 
struct archive_rb_node *
__archive_rb_tree_find_node_leq(struct archive_rb_tree *rbt, const void *key)
{
	archive_rbto_compare_key_fn compare_key = rbt->rbt_ops->rbto_compare_key;
	struct archive_rb_node *parent = rbt->rbt_root;
	struct archive_rb_node *last = NULL;

	while (!RB_SENTINEL_P(parent)) {
		const signed int diff = (*compare_key)(parent, key);
		if (diff == 0)
			return parent;
		if (diff > 0)
			last = parent;
		parent = parent->rb_nodes[diff > 0];
	}

	return last;
}

int
__archive_rb_tree_insert_node(struct archive_rb_tree *rbt,
    struct archive_rb_node *self)
{
	archive_rbto_compare_nodes_fn compare_nodes = rbt->rbt_ops->rbto_compare_nodes;
	struct archive_rb_node *parent, *tmp;
	unsigned int position;
	int rebalance;

	tmp = rbt->rbt_root;
	/*
	 * This is a hack.  Because rbt->rbt_root is just a
	 * struct archive_rb_node *, just like rb_node->rb_nodes[RB_DIR_LEFT],
	 * we can use this fact to avoid a lot of tests for root and know
	 * that even at root, updating
	 * RB_FATHER(rb_node)->rb_nodes[RB_POSITION(rb_node)] will
	 * update rbt->rbt_root.
	 */
	parent = (struct archive_rb_node *)(void *)&rbt->rbt_root;
	position = RB_DIR_LEFT;

	/*
	 * Find out where to place this new leaf.
	 */
	while (!RB_SENTINEL_P(tmp)) {
		const signed int diff = (*compare_nodes)(tmp, self);
		if (diff == 0) {
			/*
			 * Node already exists; don't insert.
			 */
			return F;
		}
		parent = tmp;
		position = (diff > 0);
		tmp = parent->rb_nodes[position];
	}

	/*
	 * Initialize the node and insert as a leaf into the tree.
	 */
	RB_SET_FATHER(self, parent);
	RB_SET_POSITION(self, position);
	if (parent == (struct archive_rb_node *)(void *)&rbt->rbt_root) {
		RB_MARK_BLACK(self);		/* root is always black */
		rebalance = F;
	} else {
		/*
		 * All new nodes are colored red.  We only need to rebalance
		 * if our parent is also red.
		 */
		RB_MARK_RED(self);
		rebalance = RB_RED_P(parent);
	}
	self->rb_left = parent->rb_nodes[position];
	self->rb_right = parent->rb_nodes[position];
	parent->rb_nodes[position] = self;

	/*
	 * Rebalance tree after insertion
	 */
	if (rebalance)
		__archive_rb_tree_insert_rebalance(rbt, self);

	return T;
}

/*
 * Swap the location and colors of 'self' and its child @ which.  The child
 * can not be a sentinel node.  This is our rotation function.  However,
 * since it preserves coloring, it great simplifies both insertion and
 * removal since rotation almost always involves the exchanging of colors
 * as a separate step.
 */
/*ARGSUSED*/
static void
__archive_rb_tree_reparent_nodes(
    struct archive_rb_node *old_father, const unsigned int which)
{
	const unsigned int other = which ^ RB_DIR_OTHER;
	struct archive_rb_node * const grandpa = RB_FATHER(old_father);
	struct archive_rb_node * const old_child = old_father->rb_nodes[which];
	struct archive_rb_node * const new_father = old_child;
	struct archive_rb_node * const new_child = old_father;

	if (new_father == NULL)
		return;
	/*
	 * Exchange descendant linkages.
	 */
	grandpa->rb_nodes[RB_POSITION(old_father)] = new_father;
	new_child->rb_nodes[which] = old_child->rb_nodes[other];
	new_father->rb_nodes[other] = new_child;

	/*
	 * Update ancestor linkages
	 */
	RB_SET_FATHER(new_father, grandpa);
	RB_SET_FATHER(new_child, new_father);

	/*
	 * Exchange properties between new_father and new_child.  The only
	 * change is that new_child's position is now on the other side.
	 */
	RB_SWAP_PROPERTIES(new_father, new_child);
	RB_SET_POSITION(new_child, other);

	/*
	 * Make sure to reparent the new child to ourself.
	 */
	if (!RB_SENTINEL_P(new_child->rb_nodes[which])) {
		RB_SET_FATHER(new_child->rb_nodes[which], new_child);
		RB_SET_POSITION(new_child->rb_nodes[which], which);
	}

}

static void
__archive_rb_tree_insert_rebalance(struct archive_rb_tree *rbt,
    struct archive_rb_node *self)
{
	struct archive_rb_node * father = RB_FATHER(self);
	struct archive_rb_node * grandpa;
	struct archive_rb_node * uncle;
	unsigned int which;
	unsigned int other;

	for (;;) {
		/*
		 * We are red and our parent is red, therefore we must have a
		 * grandfather and he must be black.
		 */
		grandpa = RB_FATHER(father);
		which = (father == grandpa->rb_right);
		other = which ^ RB_DIR_OTHER;
		uncle = grandpa->rb_nodes[other];

		if (RB_BLACK_P(uncle))
			break;

		/*
		 * Case 1: our uncle is red
		 *   Simply invert the colors of our parent and
		 *   uncle and make our grandparent red.  And
		 *   then solve the problem up at his level.
		 */
		RB_MARK_BLACK(uncle);
		RB_MARK_BLACK(father);
		if (RB_ROOT_P(rbt, grandpa)) {
			/*
			 * If our grandpa is root, don't bother
			 * setting him to red, just return.
			 */
			return;
		}
		RB_MARK_RED(grandpa);
		self = grandpa;
		father = RB_FATHER(self);
		if (RB_BLACK_P(father)) {
			/*
			 * If our great-grandpa is black, we're done.
			 */
			return;
		}
	}

	/*
	 * Case 2&3: our uncle is black.
	 */
	if (self == father->rb_nodes[other]) {
		/*
		 * Case 2: we are on the same side as our uncle
		 *   Swap ourselves with our parent so this case
		 *   becomes case 3.  Basically our parent becomes our
		 *   child.
		 */
		__archive_rb_tree_reparent_nodes(father, other);
	}
	/*
	 * Case 3: we are opposite a child of a black uncle.
	 *   Swap our parent and grandparent.  Since our grandfather
	 *   is black, our father will become black and our new sibling
	 *   (former grandparent) will become red.
	 */
	__archive_rb_tree_reparent_nodes(grandpa, which);

	/*
	 * Final step: Set the root to black.
	 */
	RB_MARK_BLACK(rbt->rbt_root);
}

static void
__archive_rb_tree_prune_node(struct archive_rb_tree *rbt,
    struct archive_rb_node *self, int rebalance)
{
	const unsigned int which = RB_POSITION(self);
	struct archive_rb_node *father = RB_FATHER(self);

	/*
	 * Since we are childless, we know that self->rb_left is pointing
	 * to the sentinel node.
	 */
	father->rb_nodes[which] = self->rb_left;

	/*
	 * Rebalance if requested.
	 */
	if (rebalance)
		__archive_rb_tree_removal_rebalance(rbt, father, which);
}

/*
 * When deleting an interior node
 */
static void
__archive_rb_tree_swap_prune_and_rebalance(struct archive_rb_tree *rbt,
    struct archive_rb_node *self, struct archive_rb_node *standin)
{
	const unsigned int standin_which = RB_POSITION(standin);
	unsigned int standin_other = standin_which ^ RB_DIR_OTHER;
	struct archive_rb_node *standin_son;
	struct archive_rb_node *standin_father = RB_FATHER(standin);
	int rebalance = RB_BLACK_P(standin);

	if (standin_father == self) {
		/*
		 * As a child of self, any children would be opposite of
		 * our parent.
		 */
		standin_son = standin->rb_nodes[standin_which];
	} else {
		/*
		 * Since we aren't a child of self, any children would be
		 * on the same side as our parent.
		 */
		standin_son = standin->rb_nodes[standin_other];
	}

	if (RB_RED_P(standin_son)) {
		/*
		 * We know we have a red child so if we flip it to black
		 * we don't have to rebalance.
		 */
		RB_MARK_BLACK(standin_son);
		rebalance = F;

		if (standin_father != self) {
			/*
			 * Change the son's parentage to point to his grandpa.
			 */
			RB_SET_FATHER(standin_son, standin_father);
			RB_SET_POSITION(standin_son, standin_which);
		}
	}

	if (standin_father == self) {
		/*
		 * If we are about to delete the standin's father, then when
		 * we call rebalance, we need to use ourselves as our father.
		 * Otherwise remember our original father.  Also, since we are
		 * our standin's father we only need to reparent the standin's
		 * brother.
		 *
		 * |    R      -->     S    |
		 * |  Q   S    -->   Q   T  |
		 * |        t  -->          |
		 *
		 * Have our son/standin adopt his brother as his new son.
		 */
		standin_father = standin;
	} else {
		/*
		 * |    R          -->    S       .  |
		 * |   / \  |   T  -->   / \  |  /   |
		 * |  ..... | S    -->  ..... | T    |
		 *
		 * Sever standin's connection to his father.
		 */
		standin_father->rb_nodes[standin_which] = standin_son;
		/*
		 * Adopt the far son.
		 */
		standin->rb_nodes[standin_other] = self->rb_nodes[standin_other];
		RB_SET_FATHER(standin->rb_nodes[standin_other], standin);
		/*
		 * Use standin_other because we need to preserve standin_which
		 * for the removal_rebalance.
		 */
		standin_other = standin_which;
	}

	/*
	 * Move the only remaining son to our standin.  If our standin is our
	 * son, this will be the only son needed to be moved.
	 */
	standin->rb_nodes[standin_other] = self->rb_nodes[standin_other];
	RB_SET_FATHER(standin->rb_nodes[standin_other], standin);

	/*
	 * Now copy the result of self to standin and then replace
	 * self with standin in the tree.
	 */
	RB_COPY_PROPERTIES(standin, self);
	RB_SET_FATHER(standin, RB_FATHER(self));
	RB_FATHER(standin)->rb_nodes[RB_POSITION(standin)] = standin;

	if (rebalance)
		__archive_rb_tree_removal_rebalance(rbt, standin_father, standin_which);
}

/*
 * We could do this by doing
 *	__archive_rb_tree_node_swap(rbt, self, which);
 *	__archive_rb_tree_prune_node(rbt, self, F);
 *
 * But it's more efficient to just evaluate and recolor the child.
 */
static void
__archive_rb_tree_prune_blackred_branch(
    struct archive_rb_node *self, unsigned int which)
{
	struct archive_rb_node *father = RB_FATHER(self);
	struct archive_rb_node *son = self->rb_nodes[which];

	/*
	 * Remove ourselves from the tree and give our former child our
	 * properties (position, color, root).
	 */
	RB_COPY_PROPERTIES(son, self);
	father->rb_nodes[RB_POSITION(son)] = son;
	RB_SET_FATHER(son, father);
}
/*
 *
 */
void
__archive_rb_tree_remove_node(struct archive_rb_tree *rbt,
    struct archive_rb_node *self)
{
	struct archive_rb_node *standin;
	unsigned int which;

	/*
	 * In the following diagrams, we (the node to be removed) are S.  Red
	 * nodes are lowercase.  T could be either red or black.
	 *
	 * Remember the major axiom of the red-black tree: the number of
	 * black nodes from the root to each leaf is constant across all
	 * leaves, only the number of red nodes varies.
	 *
	 * Thus removing a red leaf doesn't require any other changes to a
	 * red-black tree.  So if we must remove a node, attempt to rearrange
	 * the tree so we can remove a red node.
	 *
	 * The simplest case is a childless red node or a childless root node:
	 *
	 * |    T  -->    T  |    or    |  R  -->  *  |
	 * |  s    -->  *    |
	 */
	if (RB_CHILDLESS_P(self)) {
		const int rebalance = RB_BLACK_P(self) && !RB_ROOT_P(rbt, self);
		__archive_rb_tree_prune_node(rbt, self, rebalance);
		return;
	}
	if (!RB_TWOCHILDREN_P(self)) {
		/*
		 * The next simplest case is the node we are deleting is
		 * black and has one red child.
		 *
		 * |      T  -->      T  -->      T  |
		 * |    S    -->  R      -->  R      |
		 * |  r      -->    s    -->    *    |
		 */
		which = RB_LEFT_SENTINEL_P(self) ? RB_DIR_RIGHT : RB_DIR_LEFT;
		__archive_rb_tree_prune_blackred_branch(self, which);
		return;
	}

	/*
	 * We invert these because we prefer to remove from the inside of
	 * the tree.
	 */
	which = RB_POSITION(self) ^ RB_DIR_OTHER;

	/*
	 * Let's find the node closes to us opposite of our parent
	 * Now swap it with ourself, "prune" it, and rebalance, if needed.
	 */
	standin = __archive_rb_tree_iterate(rbt, self, which);
	__archive_rb_tree_swap_prune_and_rebalance(rbt, self, standin);
}

static void
__archive_rb_tree_removal_rebalance(struct archive_rb_tree *rbt,
    struct archive_rb_node *parent, unsigned int which)
{

	while (RB_BLACK_P(parent->rb_nodes[which])) {
		unsigned int other = which ^ RB_DIR_OTHER;
		struct archive_rb_node *brother = parent->rb_nodes[other];

		if (brother == NULL)
			return;/* The tree may be broken. */
		/*
		 * For cases 1, 2a, and 2b, our brother's children must
		 * be black and our father must be black
		 */
		if (RB_BLACK_P(parent)
		    && RB_BLACK_P(brother->rb_left)
		    && RB_BLACK_P(brother->rb_right)) {
			if (RB_RED_P(brother)) {
				/*
				 * Case 1: Our brother is red, swap its
				 * position (and colors) with our parent. 
				 * This should now be case 2b (unless C or E
				 * has a red child which is case 3; thus no
				 * explicit branch to case 2b).
				 *
				 *    B         ->        D
				 *  A     d     ->    b     E
				 *      C   E   ->  A   C
				 */
				__archive_rb_tree_reparent_nodes(parent, other);
				brother = parent->rb_nodes[other];
				if (brother == NULL)
					return;/* The tree may be broken. */
			} else {
				/*
				 * Both our parent and brother are black.
				 * Change our brother to red, advance up rank
				 * and go through the loop again.
				 *
				 *    B         ->   *B
				 * *A     D     ->  A     d
				 *      C   E   ->      C   E
				 */
				RB_MARK_RED(brother);
				if (RB_ROOT_P(rbt, parent))
					return;	/* root == parent == black */
				which = RB_POSITION(parent);
				parent = RB_FATHER(parent);
				continue;
			}
		}
		/*
		 * Avoid an else here so that case 2a above can hit either
		 * case 2b, 3, or 4.
		 */
		if (RB_RED_P(parent)
		    && RB_BLACK_P(brother)
		    && RB_BLACK_P(brother->rb_left)
		    && RB_BLACK_P(brother->rb_right)) {
			/*
			 * We are black, our father is red, our brother and
			 * both nephews are black.  Simply invert/exchange the
			 * colors of our father and brother (to black and red
			 * respectively).
			 *
			 *	|    f        -->    F        |
			 *	|  *     B    -->  *     b    |
			 *	|      N   N  -->      N   N  |
			 */
			RB_MARK_BLACK(parent);
			RB_MARK_RED(brother);
			break;		/* We're done! */
		} else {
			/*
			 * Our brother must be black and have at least one
			 * red child (it may have two).
			 */
			if (RB_BLACK_P(brother->rb_nodes[other])) {
				/*
				 * Case 3: our brother is black, our near
				 * nephew is red, and our far nephew is black.
				 * Swap our brother with our near nephew.  
				 * This result in a tree that matches case 4.
				 * (Our father could be red or black).
				 *
				 *	|    F      -->    F      |
				 *	|  x     B  -->  x   B    |
				 *	|      n    -->        n  |
				 */
				__archive_rb_tree_reparent_nodes(brother, which);
				brother = parent->rb_nodes[other];
			}
			/*
			 * Case 4: our brother is black and our far nephew
			 * is red.  Swap our father and brother locations and
			 * change our far nephew to black.  (these can be
			 * done in either order so we change the color first).
			 * The result is a valid red-black tree and is a
			 * terminal case.  (again we don't care about the
			 * father's color)
			 *
			 * If the father is red, we will get a red-black-black
			 * tree:
			 *	|  f      ->  f      -->    b    |
			 *	|    B    ->    B    -->  F   N  |
			 *	|      n  ->      N  -->         |
			 *
			 * If the father is black, we will get an all black
			 * tree:
			 *	|  F      ->  F      -->    B    |
			 *	|    B    ->    B    -->  F   N  |
			 *	|      n  ->      N  -->         |
			 *
			 * If we had two red nephews, then after the swap,
			 * our former father would have a red grandson. 
			 */
			if (brother->rb_nodes[other] == NULL)
				return;/* The tree may be broken. */
			RB_MARK_BLACK(brother->rb_nodes[other]);
			__archive_rb_tree_reparent_nodes(parent, other);
			break;		/* We're done! */
		}
	}
}

struct archive_rb_node *
__archive_rb_tree_iterate(struct archive_rb_tree *rbt,
    struct archive_rb_node *self, const unsigned int direction)
{
	const unsigned int other = direction ^ RB_DIR_OTHER;

	if (self == NULL) {
		self = rbt->rbt_root;
		if (RB_SENTINEL_P(self))
			return NULL;
		while (!RB_SENTINEL_P(self->rb_nodes[direction]))
			self = self->rb_nodes[direction];
		return self;
	}
	/*
	 * We can't go any further in this direction.  We proceed up in the
	 * opposite direction until our parent is in direction we want to go.
	 */
	if (RB_SENTINEL_P(self->rb_nodes[direction])) {
		while (!RB_ROOT_P(rbt, self)) {
			if (other == (unsigned int)RB_POSITION(self))
				return RB_FATHER(self);
			self = RB_FATHER(self);
		}
		return NULL;
	}

	/*
	 * Advance down one in current direction and go down as far as possible
	 * in the opposite direction.
	 */
	self = self->rb_nodes[direction];
	while (!RB_SENTINEL_P(self->rb_nodes[other]))
		self = self->rb_nodes[other];
	return self;
}
