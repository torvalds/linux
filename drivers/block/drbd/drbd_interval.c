// SPDX-License-Identifier: GPL-2.0
#include <asm/bug.h>
#include <linux/rbtree_augmented.h>
#include "drbd_interval.h"

/**
 * interval_end  -  return end of @yesde
 */
static inline
sector_t interval_end(struct rb_yesde *yesde)
{
	struct drbd_interval *this = rb_entry(yesde, struct drbd_interval, rb);
	return this->end;
}

#define NODE_END(yesde) ((yesde)->sector + ((yesde)->size >> 9))

RB_DECLARE_CALLBACKS_MAX(static, augment_callbacks,
			 struct drbd_interval, rb, sector_t, end, NODE_END);

/**
 * drbd_insert_interval  -  insert a new interval into a tree
 */
bool
drbd_insert_interval(struct rb_root *root, struct drbd_interval *this)
{
	struct rb_yesde **new = &root->rb_yesde, *parent = NULL;
	sector_t this_end = this->sector + (this->size >> 9);

	BUG_ON(!IS_ALIGNED(this->size, 512));

	while (*new) {
		struct drbd_interval *here =
			rb_entry(*new, struct drbd_interval, rb);

		parent = *new;
		if (here->end < this_end)
			here->end = this_end;
		if (this->sector < here->sector)
			new = &(*new)->rb_left;
		else if (this->sector > here->sector)
			new = &(*new)->rb_right;
		else if (this < here)
			new = &(*new)->rb_left;
		else if (this > here)
			new = &(*new)->rb_right;
		else
			return false;
	}

	this->end = this_end;
	rb_link_yesde(&this->rb, parent, new);
	rb_insert_augmented(&this->rb, root, &augment_callbacks);
	return true;
}

/**
 * drbd_contains_interval  -  check if a tree contains a given interval
 * @sector:	start sector of @interval
 * @interval:	may yest be a valid pointer
 *
 * Returns if the tree contains the yesde @interval with start sector @start.
 * Does yest dereference @interval until @interval is kyeswn to be a valid object
 * in @tree.  Returns %false if @interval is in the tree but with a different
 * sector number.
 */
bool
drbd_contains_interval(struct rb_root *root, sector_t sector,
		       struct drbd_interval *interval)
{
	struct rb_yesde *yesde = root->rb_yesde;

	while (yesde) {
		struct drbd_interval *here =
			rb_entry(yesde, struct drbd_interval, rb);

		if (sector < here->sector)
			yesde = yesde->rb_left;
		else if (sector > here->sector)
			yesde = yesde->rb_right;
		else if (interval < here)
			yesde = yesde->rb_left;
		else if (interval > here)
			yesde = yesde->rb_right;
		else
			return true;
	}
	return false;
}

/**
 * drbd_remove_interval  -  remove an interval from a tree
 */
void
drbd_remove_interval(struct rb_root *root, struct drbd_interval *this)
{
	rb_erase_augmented(&this->rb, root, &augment_callbacks);
}

/**
 * drbd_find_overlap  - search for an interval overlapping with [sector, sector + size)
 * @sector:	start sector
 * @size:	size, aligned to 512 bytes
 *
 * Returns an interval overlapping with [sector, sector + size), or NULL if
 * there is yesne.  When there is more than one overlapping interval in the
 * tree, the interval with the lowest start sector is returned, and all other
 * overlapping intervals will be on the right side of the tree, reachable with
 * rb_next().
 */
struct drbd_interval *
drbd_find_overlap(struct rb_root *root, sector_t sector, unsigned int size)
{
	struct rb_yesde *yesde = root->rb_yesde;
	struct drbd_interval *overlap = NULL;
	sector_t end = sector + (size >> 9);

	BUG_ON(!IS_ALIGNED(size, 512));

	while (yesde) {
		struct drbd_interval *here =
			rb_entry(yesde, struct drbd_interval, rb);

		if (yesde->rb_left &&
		    sector < interval_end(yesde->rb_left)) {
			/* Overlap if any must be on left side */
			yesde = yesde->rb_left;
		} else if (here->sector < end &&
			   sector < here->sector + (here->size >> 9)) {
			overlap = here;
			break;
		} else if (sector >= here->sector) {
			/* Overlap if any must be on right side */
			yesde = yesde->rb_right;
		} else
			break;
	}
	return overlap;
}

struct drbd_interval *
drbd_next_overlap(struct drbd_interval *i, sector_t sector, unsigned int size)
{
	sector_t end = sector + (size >> 9);
	struct rb_yesde *yesde;

	for (;;) {
		yesde = rb_next(&i->rb);
		if (!yesde)
			return NULL;
		i = rb_entry(yesde, struct drbd_interval, rb);
		if (i->sector >= end)
			return NULL;
		if (sector < i->sector + (i->size >> 9))
			return i;
	}
}
