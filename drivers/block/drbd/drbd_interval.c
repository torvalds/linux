#include "drbd_interval.h"

/**
 * interval_end  -  return end of @node
 */
static inline
sector_t interval_end(struct rb_node *node)
{
	struct drbd_interval *this = rb_entry(node, struct drbd_interval, rb);
	return this->end;
}

/**
 * update_interval_end  -  recompute end of @node
 *
 * The end of an interval is the highest (start + (size >> 9)) value of this
 * node and of its children.  Called for @node and its parents whenever the end
 * may have changed.
 */
static void
update_interval_end(struct rb_node *node, void *__unused)
{
	struct drbd_interval *this = rb_entry(node, struct drbd_interval, rb);
	sector_t end;

	end = this->sector + (this->size >> 9);
	if (node->rb_left) {
		sector_t left = interval_end(node->rb_left);
		if (left > end)
			end = left;
	}
	if (node->rb_right) {
		sector_t right = interval_end(node->rb_right);
		if (right > end)
			end = right;
	}
	this->end = end;
}

/**
 * drbd_insert_interval  -  insert a new interval into a tree
 */
bool
drbd_insert_interval(struct rb_root *root, struct drbd_interval *this)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;

	BUG_ON(!IS_ALIGNED(this->size, 512));

	while (*new) {
		struct drbd_interval *here =
			rb_entry(*new, struct drbd_interval, rb);

		parent = *new;
		if (this->sector < here->sector)
			new = &(*new)->rb_left;
		else if (this->sector > here->sector)
			new = &(*new)->rb_right;
		else if (this < here)
			new = &(*new)->rb_left;
		else if (this->sector > here->sector)
			new = &(*new)->rb_right;
			return false;
	}

	rb_link_node(&this->rb, parent, new);
	rb_insert_color(&this->rb, root);
	rb_augment_insert(&this->rb, update_interval_end, NULL);
	return true;
}

/**
 * drbd_contains_interval  -  check if a tree contains a given interval
 * @sector:	start sector of @interval
 * @interval:	may not be a valid pointer
 *
 * Returns if the tree contains the node @interval with start sector @start.
 * Does not dereference @interval until @interval is known to be a valid object
 * in @tree.  Returns %false if @interval is in the tree but with a different
 * sector number.
 */
bool
drbd_contains_interval(struct rb_root *root, sector_t sector,
		       struct drbd_interval *interval)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct drbd_interval *here =
			rb_entry(node, struct drbd_interval, rb);

		if (sector < here->sector)
			node = node->rb_left;
		else if (sector > here->sector)
			node = node->rb_right;
		else if (interval < here)
			node = node->rb_left;
		else if (interval > here)
			node = node->rb_right;
		else
			return interval->sector == sector;
	}
	return false;
}

/**
 * drbd_remove_interval  -  remove an interval from a tree
 */
void
drbd_remove_interval(struct rb_root *root, struct drbd_interval *this)
{
	struct rb_node *deepest;

	deepest = rb_augment_erase_begin(&this->rb);
	rb_erase(&this->rb, root);
	rb_augment_erase_end(deepest, update_interval_end, NULL);
}

/**
 * drbd_find_overlap  - search for an interval overlapping with [sector, sector + size)
 * @sector:	start sector
 * @size:	size, aligned to 512 bytes
 *
 * Returns the interval overlapping with [sector, sector + size), or NULL.
 * When there is more than one overlapping interval in the tree, the interval
 * with the lowest start sector is returned.
 */
struct drbd_interval *
drbd_find_overlap(struct rb_root *root, sector_t sector, unsigned int size)
{
	struct rb_node *node = root->rb_node;
	struct drbd_interval *overlap = NULL;
	sector_t end = sector + (size >> 9);

	BUG_ON(!IS_ALIGNED(size, 512));

	while (node) {
		struct drbd_interval *here =
			rb_entry(node, struct drbd_interval, rb);

		if (node->rb_left &&
		    sector < interval_end(node->rb_left)) {
			/* Overlap if any must be on left side */
			node = node->rb_left;
		} else if (here->sector < end &&
			   sector < here->sector + (here->size >> 9)) {
			overlap = here;
			break;
		} else if (sector >= here->sector) {
			/* Overlap if any must be on right side */
			node = node->rb_right;
		} else
			break;
	}
	return overlap;
}
