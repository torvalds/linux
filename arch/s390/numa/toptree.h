/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NUMA support for s390
 *
 * A tree structure used for machine topology mangling
 *
 * Copyright IBM Corp. 2015
 */
#ifndef S390_TOPTREE_H
#define S390_TOPTREE_H

#include <linux/cpumask.h>
#include <linux/list.h>

struct toptree {
	int level;
	int id;
	cpumask_t mask;
	struct toptree *parent;
	struct list_head sibling;
	struct list_head children;
};

struct toptree *toptree_alloc(int level, int id);
void toptree_free(struct toptree *cand);
void toptree_update_mask(struct toptree *cand);
void toptree_unify(struct toptree *cand);
struct toptree *toptree_get_child(struct toptree *cand, int id);
void toptree_move(struct toptree *cand, struct toptree *target);
int toptree_count(struct toptree *context, int level);

struct toptree *toptree_first(struct toptree *context, int level);
struct toptree *toptree_next(struct toptree *cur, struct toptree *context,
			     int level);

#define toptree_for_each_child(child, ptree)				\
	list_for_each_entry(child,  &ptree->children, sibling)

#define toptree_for_each_child_safe(child, ptmp, ptree)			\
	list_for_each_entry_safe(child, ptmp, &ptree->children, sibling)

#define toptree_is_last(ptree)					\
	((ptree->parent == NULL) ||				\
	 (ptree->parent->children.prev == &ptree->sibling))

#define toptree_for_each(ptree, cont, ttype)		\
	for (ptree = toptree_first(cont, ttype);	\
	     ptree != NULL;				\
	     ptree = toptree_next(ptree, cont, ttype))

#define toptree_for_each_safe(ptree, tmp, cont, ttype)		\
	for (ptree = toptree_first(cont, ttype),		\
		     tmp = toptree_next(ptree, cont, ttype);	\
	     ptree != NULL;					\
	     ptree = tmp,					\
		     tmp = toptree_next(ptree, cont, ttype))

#define toptree_for_each_sibling(ptree, start)			\
	toptree_for_each(ptree, start->parent, start->level)

#endif /* S390_TOPTREE_H */
