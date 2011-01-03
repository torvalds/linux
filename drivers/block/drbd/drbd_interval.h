#ifndef __DRBD_INTERVAL_H
#define __DRBD_INTERVAL_H

#include <linux/types.h>
#include <linux/rbtree.h>

struct drbd_interval {
	struct rb_node rb;
	sector_t sector;	/* start sector of the interval */
	unsigned int size;	/* size in bytes */
	sector_t end;		/* highest interval end in subtree */
};

static inline void drbd_clear_interval(struct drbd_interval *i)
{
	RB_CLEAR_NODE(&i->rb);
}

static inline bool drbd_interval_empty(struct drbd_interval *i)
{
	return RB_EMPTY_NODE(&i->rb);
}

bool drbd_insert_interval(struct rb_root *, struct drbd_interval *);
struct drbd_interval *drbd_find_interval(struct rb_root *, sector_t,
					 struct drbd_interval *);
void drbd_remove_interval(struct rb_root *, struct drbd_interval *);
struct drbd_interval *drbd_find_overlap(struct rb_root *, sector_t,
					unsigned int);

#endif  /* __DRBD_INTERVAL_H */
