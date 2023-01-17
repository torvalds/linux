/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __DRBD_INTERVAL_H
#define __DRBD_INTERVAL_H

#include <linux/types.h>
#include <linux/rbtree.h>

struct drbd_interval {
	struct rb_node rb;
	sector_t sector;		/* start sector of the interval */
	unsigned int size;		/* size in bytes */
	sector_t end;			/* highest interval end in subtree */
	unsigned int local:1		/* local or remote request? */;
	unsigned int waiting:1;		/* someone is waiting for completion */
	unsigned int completed:1;	/* this has been completed already;
					 * ignore for conflict detection */
};

static inline void drbd_clear_interval(struct drbd_interval *i)
{
	RB_CLEAR_NODE(&i->rb);
}

static inline bool drbd_interval_empty(struct drbd_interval *i)
{
	return RB_EMPTY_NODE(&i->rb);
}

extern bool drbd_insert_interval(struct rb_root *, struct drbd_interval *);
extern bool drbd_contains_interval(struct rb_root *, sector_t,
				   struct drbd_interval *);
extern void drbd_remove_interval(struct rb_root *, struct drbd_interval *);
extern struct drbd_interval *drbd_find_overlap(struct rb_root *, sector_t,
					unsigned int);
extern struct drbd_interval *drbd_next_overlap(struct drbd_interval *, sector_t,
					unsigned int);

#define drbd_for_each_overlap(i, root, sector, size)		\
	for (i = drbd_find_overlap(root, sector, size);		\
	     i;							\
	     i = drbd_next_overlap(i, sector, size))

#endif  /* __DRBD_INTERVAL_H */
