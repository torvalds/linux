/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND., USA.
 * Copyright 2016 Intel Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright yestice and this permission yestice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/

/*
 * Generic simple memory manager implementation. Intended to be used as a base
 * class implementation for more advanced memory managers.
 *
 * Note that the algorithm used is quite simple and there might be substantial
 * performance gains if a smarter free list is implemented. Currently it is
 * just an uyesrdered stack of free regions. This could easily be improved if
 * an RB-tree is used instead. At least if we expect heavy fragmentation.
 *
 * Aligned allocations can also see improvement.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <linux/export.h>
#include <linux/interval_tree_generic.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>

#include <drm/drm_mm.h>

/**
 * DOC: Overview
 *
 * drm_mm provides a simple range allocator. The drivers are free to use the
 * resource allocator from the linux core if it suits them, the upside of drm_mm
 * is that it's in the DRM core. Which means that it's easier to extend for
 * some of the crazier special purpose needs of gpus.
 *
 * The main data struct is &drm_mm, allocations are tracked in &drm_mm_yesde.
 * Drivers are free to embed either of them into their own suitable
 * datastructures. drm_mm itself will yest do any memory allocations of its own,
 * so if drivers choose yest to embed yesdes they need to still allocate them
 * themselves.
 *
 * The range allocator also supports reservation of preallocated blocks. This is
 * useful for taking over initial mode setting configurations from the firmware,
 * where an object needs to be created which exactly matches the firmware's
 * scayesut target. As long as the range is still free it can be inserted anytime
 * after the allocator is initialized, which helps with avoiding looped
 * dependencies in the driver load sequence.
 *
 * drm_mm maintains a stack of most recently freed holes, which of all
 * simplistic datastructures seems to be a fairly decent approach to clustering
 * allocations and avoiding too much fragmentation. This means free space
 * searches are O(num_holes). Given that all the fancy features drm_mm supports
 * something better would be fairly complex and since gfx thrashing is a fairly
 * steep cliff yest a real concern. Removing a yesde again is O(1).
 *
 * drm_mm supports a few features: Alignment and range restrictions can be
 * supplied. Furthermore every &drm_mm_yesde has a color value (which is just an
 * opaque unsigned long) which in conjunction with a driver callback can be used
 * to implement sophisticated placement restrictions. The i915 DRM driver uses
 * this to implement guard pages between incompatible caching domains in the
 * graphics TT.
 *
 * Two behaviors are supported for searching and allocating: bottom-up and
 * top-down. The default is bottom-up. Top-down allocation can be used if the
 * memory area has different restrictions, or just to reduce fragmentation.
 *
 * Finally iteration helpers to walk all yesdes and all holes are provided as are
 * some basic allocator dumpers for debugging.
 *
 * Note that this range allocator is yest thread-safe, drivers need to protect
 * modifications with their own locking. The idea behind this is that for a full
 * memory manager additional data needs to be protected anyway, hence internal
 * locking would be fully redundant.
 */

#ifdef CONFIG_DRM_DEBUG_MM
#include <linux/stackdepot.h>

#define STACKDEPTH 32
#define BUFSZ 4096

static yesinline void save_stack(struct drm_mm_yesde *yesde)
{
	unsigned long entries[STACKDEPTH];
	unsigned int n;

	n = stack_trace_save(entries, ARRAY_SIZE(entries), 1);

	/* May be called under spinlock, so avoid sleeping */
	yesde->stack = stack_depot_save(entries, n, GFP_NOWAIT);
}

static void show_leaks(struct drm_mm *mm)
{
	struct drm_mm_yesde *yesde;
	unsigned long *entries;
	unsigned int nr_entries;
	char *buf;

	buf = kmalloc(BUFSZ, GFP_KERNEL);
	if (!buf)
		return;

	list_for_each_entry(yesde, drm_mm_yesdes(mm), yesde_list) {
		if (!yesde->stack) {
			DRM_ERROR("yesde [%08llx + %08llx]: unkyeswn owner\n",
				  yesde->start, yesde->size);
			continue;
		}

		nr_entries = stack_depot_fetch(yesde->stack, &entries);
		stack_trace_snprint(buf, BUFSZ, entries, nr_entries, 0);
		DRM_ERROR("yesde [%08llx + %08llx]: inserted at\n%s",
			  yesde->start, yesde->size, buf);
	}

	kfree(buf);
}

#undef STACKDEPTH
#undef BUFSZ
#else
static void save_stack(struct drm_mm_yesde *yesde) { }
static void show_leaks(struct drm_mm *mm) { }
#endif

#define START(yesde) ((yesde)->start)
#define LAST(yesde)  ((yesde)->start + (yesde)->size - 1)

INTERVAL_TREE_DEFINE(struct drm_mm_yesde, rb,
		     u64, __subtree_last,
		     START, LAST, static inline, drm_mm_interval_tree)

struct drm_mm_yesde *
__drm_mm_interval_first(const struct drm_mm *mm, u64 start, u64 last)
{
	return drm_mm_interval_tree_iter_first((struct rb_root_cached *)&mm->interval_tree,
					       start, last) ?: (struct drm_mm_yesde *)&mm->head_yesde;
}
EXPORT_SYMBOL(__drm_mm_interval_first);

static void drm_mm_interval_tree_add_yesde(struct drm_mm_yesde *hole_yesde,
					  struct drm_mm_yesde *yesde)
{
	struct drm_mm *mm = hole_yesde->mm;
	struct rb_yesde **link, *rb;
	struct drm_mm_yesde *parent;
	bool leftmost;

	yesde->__subtree_last = LAST(yesde);

	if (drm_mm_yesde_allocated(hole_yesde)) {
		rb = &hole_yesde->rb;
		while (rb) {
			parent = rb_entry(rb, struct drm_mm_yesde, rb);
			if (parent->__subtree_last >= yesde->__subtree_last)
				break;

			parent->__subtree_last = yesde->__subtree_last;
			rb = rb_parent(rb);
		}

		rb = &hole_yesde->rb;
		link = &hole_yesde->rb.rb_right;
		leftmost = false;
	} else {
		rb = NULL;
		link = &mm->interval_tree.rb_root.rb_yesde;
		leftmost = true;
	}

	while (*link) {
		rb = *link;
		parent = rb_entry(rb, struct drm_mm_yesde, rb);
		if (parent->__subtree_last < yesde->__subtree_last)
			parent->__subtree_last = yesde->__subtree_last;
		if (yesde->start < parent->start) {
			link = &parent->rb.rb_left;
		} else {
			link = &parent->rb.rb_right;
			leftmost = false;
		}
	}

	rb_link_yesde(&yesde->rb, rb, link);
	rb_insert_augmented_cached(&yesde->rb, &mm->interval_tree, leftmost,
				   &drm_mm_interval_tree_augment);
}

#define RB_INSERT(root, member, expr) do { \
	struct rb_yesde **link = &root.rb_yesde, *rb = NULL; \
	u64 x = expr(yesde); \
	while (*link) { \
		rb = *link; \
		if (x < expr(rb_entry(rb, struct drm_mm_yesde, member))) \
			link = &rb->rb_left; \
		else \
			link = &rb->rb_right; \
	} \
	rb_link_yesde(&yesde->member, rb, link); \
	rb_insert_color(&yesde->member, &root); \
} while (0)

#define HOLE_SIZE(NODE) ((NODE)->hole_size)
#define HOLE_ADDR(NODE) (__drm_mm_hole_yesde_start(NODE))

static u64 rb_to_hole_size(struct rb_yesde *rb)
{
	return rb_entry(rb, struct drm_mm_yesde, rb_hole_size)->hole_size;
}

static void insert_hole_size(struct rb_root_cached *root,
			     struct drm_mm_yesde *yesde)
{
	struct rb_yesde **link = &root->rb_root.rb_yesde, *rb = NULL;
	u64 x = yesde->hole_size;
	bool first = true;

	while (*link) {
		rb = *link;
		if (x > rb_to_hole_size(rb)) {
			link = &rb->rb_left;
		} else {
			link = &rb->rb_right;
			first = false;
		}
	}

	rb_link_yesde(&yesde->rb_hole_size, rb, link);
	rb_insert_color_cached(&yesde->rb_hole_size, root, first);
}

static void add_hole(struct drm_mm_yesde *yesde)
{
	struct drm_mm *mm = yesde->mm;

	yesde->hole_size =
		__drm_mm_hole_yesde_end(yesde) - __drm_mm_hole_yesde_start(yesde);
	DRM_MM_BUG_ON(!drm_mm_hole_follows(yesde));

	insert_hole_size(&mm->holes_size, yesde);
	RB_INSERT(mm->holes_addr, rb_hole_addr, HOLE_ADDR);

	list_add(&yesde->hole_stack, &mm->hole_stack);
}

static void rm_hole(struct drm_mm_yesde *yesde)
{
	DRM_MM_BUG_ON(!drm_mm_hole_follows(yesde));

	list_del(&yesde->hole_stack);
	rb_erase_cached(&yesde->rb_hole_size, &yesde->mm->holes_size);
	rb_erase(&yesde->rb_hole_addr, &yesde->mm->holes_addr);
	yesde->hole_size = 0;

	DRM_MM_BUG_ON(drm_mm_hole_follows(yesde));
}

static inline struct drm_mm_yesde *rb_hole_size_to_yesde(struct rb_yesde *rb)
{
	return rb_entry_safe(rb, struct drm_mm_yesde, rb_hole_size);
}

static inline struct drm_mm_yesde *rb_hole_addr_to_yesde(struct rb_yesde *rb)
{
	return rb_entry_safe(rb, struct drm_mm_yesde, rb_hole_addr);
}

static inline u64 rb_hole_size(struct rb_yesde *rb)
{
	return rb_entry(rb, struct drm_mm_yesde, rb_hole_size)->hole_size;
}

static struct drm_mm_yesde *best_hole(struct drm_mm *mm, u64 size)
{
	struct rb_yesde *rb = mm->holes_size.rb_root.rb_yesde;
	struct drm_mm_yesde *best = NULL;

	do {
		struct drm_mm_yesde *yesde =
			rb_entry(rb, struct drm_mm_yesde, rb_hole_size);

		if (size <= yesde->hole_size) {
			best = yesde;
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	} while (rb);

	return best;
}

static struct drm_mm_yesde *find_hole(struct drm_mm *mm, u64 addr)
{
	struct rb_yesde *rb = mm->holes_addr.rb_yesde;
	struct drm_mm_yesde *yesde = NULL;

	while (rb) {
		u64 hole_start;

		yesde = rb_hole_addr_to_yesde(rb);
		hole_start = __drm_mm_hole_yesde_start(yesde);

		if (addr < hole_start)
			rb = yesde->rb_hole_addr.rb_left;
		else if (addr > hole_start + yesde->hole_size)
			rb = yesde->rb_hole_addr.rb_right;
		else
			break;
	}

	return yesde;
}

static struct drm_mm_yesde *
first_hole(struct drm_mm *mm,
	   u64 start, u64 end, u64 size,
	   enum drm_mm_insert_mode mode)
{
	switch (mode) {
	default:
	case DRM_MM_INSERT_BEST:
		return best_hole(mm, size);

	case DRM_MM_INSERT_LOW:
		return find_hole(mm, start);

	case DRM_MM_INSERT_HIGH:
		return find_hole(mm, end);

	case DRM_MM_INSERT_EVICT:
		return list_first_entry_or_null(&mm->hole_stack,
						struct drm_mm_yesde,
						hole_stack);
	}
}

static struct drm_mm_yesde *
next_hole(struct drm_mm *mm,
	  struct drm_mm_yesde *yesde,
	  enum drm_mm_insert_mode mode)
{
	switch (mode) {
	default:
	case DRM_MM_INSERT_BEST:
		return rb_hole_size_to_yesde(rb_prev(&yesde->rb_hole_size));

	case DRM_MM_INSERT_LOW:
		return rb_hole_addr_to_yesde(rb_next(&yesde->rb_hole_addr));

	case DRM_MM_INSERT_HIGH:
		return rb_hole_addr_to_yesde(rb_prev(&yesde->rb_hole_addr));

	case DRM_MM_INSERT_EVICT:
		yesde = list_next_entry(yesde, hole_stack);
		return &yesde->hole_stack == &mm->hole_stack ? NULL : yesde;
	}
}

/**
 * drm_mm_reserve_yesde - insert an pre-initialized yesde
 * @mm: drm_mm allocator to insert @yesde into
 * @yesde: drm_mm_yesde to insert
 *
 * This functions inserts an already set-up &drm_mm_yesde into the allocator,
 * meaning that start, size and color must be set by the caller. All other
 * fields must be cleared to 0. This is useful to initialize the allocator with
 * preallocated objects which must be set-up before the range allocator can be
 * set-up, e.g. when taking over a firmware framebuffer.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's yes hole where @yesde is.
 */
int drm_mm_reserve_yesde(struct drm_mm *mm, struct drm_mm_yesde *yesde)
{
	u64 end = yesde->start + yesde->size;
	struct drm_mm_yesde *hole;
	u64 hole_start, hole_end;
	u64 adj_start, adj_end;

	end = yesde->start + yesde->size;
	if (unlikely(end <= yesde->start))
		return -ENOSPC;

	/* Find the relevant hole to add our yesde to */
	hole = find_hole(mm, yesde->start);
	if (!hole)
		return -ENOSPC;

	adj_start = hole_start = __drm_mm_hole_yesde_start(hole);
	adj_end = hole_end = hole_start + hole->hole_size;

	if (mm->color_adjust)
		mm->color_adjust(hole, yesde->color, &adj_start, &adj_end);

	if (adj_start > yesde->start || adj_end < end)
		return -ENOSPC;

	yesde->mm = mm;

	__set_bit(DRM_MM_NODE_ALLOCATED_BIT, &yesde->flags);
	list_add(&yesde->yesde_list, &hole->yesde_list);
	drm_mm_interval_tree_add_yesde(hole, yesde);
	yesde->hole_size = 0;

	rm_hole(hole);
	if (yesde->start > hole_start)
		add_hole(hole);
	if (end < hole_end)
		add_hole(yesde);

	save_stack(yesde);
	return 0;
}
EXPORT_SYMBOL(drm_mm_reserve_yesde);

static u64 rb_to_hole_size_or_zero(struct rb_yesde *rb)
{
	return rb ? rb_to_hole_size(rb) : 0;
}

/**
 * drm_mm_insert_yesde_in_range - ranged search for space and insert @yesde
 * @mm: drm_mm to allocate from
 * @yesde: preallocate yesde to insert
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for this yesde
 * @range_start: start of the allowed range for this yesde
 * @range_end: end of the allowed range for this yesde
 * @mode: fine-tune the allocation search and placement
 *
 * The preallocated @yesde must be cleared to 0.
 *
 * Returns:
 * 0 on success, -ENOSPC if there's yes suitable hole.
 */
int drm_mm_insert_yesde_in_range(struct drm_mm * const mm,
				struct drm_mm_yesde * const yesde,
				u64 size, u64 alignment,
				unsigned long color,
				u64 range_start, u64 range_end,
				enum drm_mm_insert_mode mode)
{
	struct drm_mm_yesde *hole;
	u64 remainder_mask;
	bool once;

	DRM_MM_BUG_ON(range_start > range_end);

	if (unlikely(size == 0 || range_end - range_start < size))
		return -ENOSPC;

	if (rb_to_hole_size_or_zero(rb_first_cached(&mm->holes_size)) < size)
		return -ENOSPC;

	if (alignment <= 1)
		alignment = 0;

	once = mode & DRM_MM_INSERT_ONCE;
	mode &= ~DRM_MM_INSERT_ONCE;

	remainder_mask = is_power_of_2(alignment) ? alignment - 1 : 0;
	for (hole = first_hole(mm, range_start, range_end, size, mode);
	     hole;
	     hole = once ? NULL : next_hole(mm, hole, mode)) {
		u64 hole_start = __drm_mm_hole_yesde_start(hole);
		u64 hole_end = hole_start + hole->hole_size;
		u64 adj_start, adj_end;
		u64 col_start, col_end;

		if (mode == DRM_MM_INSERT_LOW && hole_start >= range_end)
			break;

		if (mode == DRM_MM_INSERT_HIGH && hole_end <= range_start)
			break;

		col_start = hole_start;
		col_end = hole_end;
		if (mm->color_adjust)
			mm->color_adjust(hole, color, &col_start, &col_end);

		adj_start = max(col_start, range_start);
		adj_end = min(col_end, range_end);

		if (adj_end <= adj_start || adj_end - adj_start < size)
			continue;

		if (mode == DRM_MM_INSERT_HIGH)
			adj_start = adj_end - size;

		if (alignment) {
			u64 rem;

			if (likely(remainder_mask))
				rem = adj_start & remainder_mask;
			else
				div64_u64_rem(adj_start, alignment, &rem);
			if (rem) {
				adj_start -= rem;
				if (mode != DRM_MM_INSERT_HIGH)
					adj_start += alignment;

				if (adj_start < max(col_start, range_start) ||
				    min(col_end, range_end) - adj_start < size)
					continue;

				if (adj_end <= adj_start ||
				    adj_end - adj_start < size)
					continue;
			}
		}

		yesde->mm = mm;
		yesde->size = size;
		yesde->start = adj_start;
		yesde->color = color;
		yesde->hole_size = 0;

		__set_bit(DRM_MM_NODE_ALLOCATED_BIT, &yesde->flags);
		list_add(&yesde->yesde_list, &hole->yesde_list);
		drm_mm_interval_tree_add_yesde(hole, yesde);

		rm_hole(hole);
		if (adj_start > hole_start)
			add_hole(hole);
		if (adj_start + size < hole_end)
			add_hole(yesde);

		save_stack(yesde);
		return 0;
	}

	return -ENOSPC;
}
EXPORT_SYMBOL(drm_mm_insert_yesde_in_range);

static inline bool drm_mm_yesde_scanned_block(const struct drm_mm_yesde *yesde)
{
	return test_bit(DRM_MM_NODE_SCANNED_BIT, &yesde->flags);
}

/**
 * drm_mm_remove_yesde - Remove a memory yesde from the allocator.
 * @yesde: drm_mm_yesde to remove
 *
 * This just removes a yesde from its drm_mm allocator. The yesde does yest need to
 * be cleared again before it can be re-inserted into this or any other drm_mm
 * allocator. It is a bug to call this function on a unallocated yesde.
 */
void drm_mm_remove_yesde(struct drm_mm_yesde *yesde)
{
	struct drm_mm *mm = yesde->mm;
	struct drm_mm_yesde *prev_yesde;

	DRM_MM_BUG_ON(!drm_mm_yesde_allocated(yesde));
	DRM_MM_BUG_ON(drm_mm_yesde_scanned_block(yesde));

	prev_yesde = list_prev_entry(yesde, yesde_list);

	if (drm_mm_hole_follows(yesde))
		rm_hole(yesde);

	drm_mm_interval_tree_remove(yesde, &mm->interval_tree);
	list_del(&yesde->yesde_list);

	if (drm_mm_hole_follows(prev_yesde))
		rm_hole(prev_yesde);
	add_hole(prev_yesde);

	clear_bit_unlock(DRM_MM_NODE_ALLOCATED_BIT, &yesde->flags);
}
EXPORT_SYMBOL(drm_mm_remove_yesde);

/**
 * drm_mm_replace_yesde - move an allocation from @old to @new
 * @old: drm_mm_yesde to remove from the allocator
 * @new: drm_mm_yesde which should inherit @old's allocation
 *
 * This is useful for when drivers embed the drm_mm_yesde structure and hence
 * can't move allocations by reassigning pointers. It's a combination of remove
 * and insert with the guarantee that the allocation start will match.
 */
void drm_mm_replace_yesde(struct drm_mm_yesde *old, struct drm_mm_yesde *new)
{
	struct drm_mm *mm = old->mm;

	DRM_MM_BUG_ON(!drm_mm_yesde_allocated(old));

	*new = *old;

	__set_bit(DRM_MM_NODE_ALLOCATED_BIT, &new->flags);
	list_replace(&old->yesde_list, &new->yesde_list);
	rb_replace_yesde_cached(&old->rb, &new->rb, &mm->interval_tree);

	if (drm_mm_hole_follows(old)) {
		list_replace(&old->hole_stack, &new->hole_stack);
		rb_replace_yesde_cached(&old->rb_hole_size,
				       &new->rb_hole_size,
				       &mm->holes_size);
		rb_replace_yesde(&old->rb_hole_addr,
				&new->rb_hole_addr,
				&mm->holes_addr);
	}

	clear_bit_unlock(DRM_MM_NODE_ALLOCATED_BIT, &old->flags);
}
EXPORT_SYMBOL(drm_mm_replace_yesde);

/**
 * DOC: lru scan roster
 *
 * Very often GPUs need to have continuous allocations for a given object. When
 * evicting objects to make space for a new one it is therefore yest most
 * efficient when we simply start to select all objects from the tail of an LRU
 * until there's a suitable hole: Especially for big objects or yesdes that
 * otherwise have special allocation constraints there's a good chance we evict
 * lots of (smaller) objects unnecessarily.
 *
 * The DRM range allocator supports this use-case through the scanning
 * interfaces. First a scan operation needs to be initialized with
 * drm_mm_scan_init() or drm_mm_scan_init_with_range(). The driver adds
 * objects to the roster, probably by walking an LRU list, but this can be
 * freely implemented. Eviction candiates are added using
 * drm_mm_scan_add_block() until a suitable hole is found or there are yes
 * further evictable objects. Eviction roster metadata is tracked in &struct
 * drm_mm_scan.
 *
 * The driver must walk through all objects again in exactly the reverse
 * order to restore the allocator state. Note that while the allocator is used
 * in the scan mode yes other operation is allowed.
 *
 * Finally the driver evicts all objects selected (drm_mm_scan_remove_block()
 * reported true) in the scan, and any overlapping yesdes after color adjustment
 * (drm_mm_scan_color_evict()). Adding and removing an object is O(1), and
 * since freeing a yesde is also O(1) the overall complexity is
 * O(scanned_objects). So like the free stack which needs to be walked before a
 * scan operation even begins this is linear in the number of objects. It
 * doesn't seem to hurt too badly.
 */

/**
 * drm_mm_scan_init_with_range - initialize range-restricted lru scanning
 * @scan: scan state
 * @mm: drm_mm to scan
 * @size: size of the allocation
 * @alignment: alignment of the allocation
 * @color: opaque tag value to use for the allocation
 * @start: start of the allowed range for the allocation
 * @end: end of the allowed range for the allocation
 * @mode: fine-tune the allocation search and placement
 *
 * This simply sets up the scanning routines with the parameters for the desired
 * hole.
 *
 * Warning:
 * As long as the scan list is yesn-empty, yes other operations than
 * adding/removing yesdes to/from the scan list are allowed.
 */
void drm_mm_scan_init_with_range(struct drm_mm_scan *scan,
				 struct drm_mm *mm,
				 u64 size,
				 u64 alignment,
				 unsigned long color,
				 u64 start,
				 u64 end,
				 enum drm_mm_insert_mode mode)
{
	DRM_MM_BUG_ON(start >= end);
	DRM_MM_BUG_ON(!size || size > end - start);
	DRM_MM_BUG_ON(mm->scan_active);

	scan->mm = mm;

	if (alignment <= 1)
		alignment = 0;

	scan->color = color;
	scan->alignment = alignment;
	scan->remainder_mask = is_power_of_2(alignment) ? alignment - 1 : 0;
	scan->size = size;
	scan->mode = mode;

	DRM_MM_BUG_ON(end <= start);
	scan->range_start = start;
	scan->range_end = end;

	scan->hit_start = U64_MAX;
	scan->hit_end = 0;
}
EXPORT_SYMBOL(drm_mm_scan_init_with_range);

/**
 * drm_mm_scan_add_block - add a yesde to the scan list
 * @scan: the active drm_mm scanner
 * @yesde: drm_mm_yesde to add
 *
 * Add a yesde to the scan list that might be freed to make space for the desired
 * hole.
 *
 * Returns:
 * True if a hole has been found, false otherwise.
 */
bool drm_mm_scan_add_block(struct drm_mm_scan *scan,
			   struct drm_mm_yesde *yesde)
{
	struct drm_mm *mm = scan->mm;
	struct drm_mm_yesde *hole;
	u64 hole_start, hole_end;
	u64 col_start, col_end;
	u64 adj_start, adj_end;

	DRM_MM_BUG_ON(yesde->mm != mm);
	DRM_MM_BUG_ON(!drm_mm_yesde_allocated(yesde));
	DRM_MM_BUG_ON(drm_mm_yesde_scanned_block(yesde));
	__set_bit(DRM_MM_NODE_SCANNED_BIT, &yesde->flags);
	mm->scan_active++;

	/* Remove this block from the yesde_list so that we enlarge the hole
	 * (distance between the end of our previous yesde and the start of
	 * or next), without poisoning the link so that we can restore it
	 * later in drm_mm_scan_remove_block().
	 */
	hole = list_prev_entry(yesde, yesde_list);
	DRM_MM_BUG_ON(list_next_entry(hole, yesde_list) != yesde);
	__list_del_entry(&yesde->yesde_list);

	hole_start = __drm_mm_hole_yesde_start(hole);
	hole_end = __drm_mm_hole_yesde_end(hole);

	col_start = hole_start;
	col_end = hole_end;
	if (mm->color_adjust)
		mm->color_adjust(hole, scan->color, &col_start, &col_end);

	adj_start = max(col_start, scan->range_start);
	adj_end = min(col_end, scan->range_end);
	if (adj_end <= adj_start || adj_end - adj_start < scan->size)
		return false;

	if (scan->mode == DRM_MM_INSERT_HIGH)
		adj_start = adj_end - scan->size;

	if (scan->alignment) {
		u64 rem;

		if (likely(scan->remainder_mask))
			rem = adj_start & scan->remainder_mask;
		else
			div64_u64_rem(adj_start, scan->alignment, &rem);
		if (rem) {
			adj_start -= rem;
			if (scan->mode != DRM_MM_INSERT_HIGH)
				adj_start += scan->alignment;
			if (adj_start < max(col_start, scan->range_start) ||
			    min(col_end, scan->range_end) - adj_start < scan->size)
				return false;

			if (adj_end <= adj_start ||
			    adj_end - adj_start < scan->size)
				return false;
		}
	}

	scan->hit_start = adj_start;
	scan->hit_end = adj_start + scan->size;

	DRM_MM_BUG_ON(scan->hit_start >= scan->hit_end);
	DRM_MM_BUG_ON(scan->hit_start < hole_start);
	DRM_MM_BUG_ON(scan->hit_end > hole_end);

	return true;
}
EXPORT_SYMBOL(drm_mm_scan_add_block);

/**
 * drm_mm_scan_remove_block - remove a yesde from the scan list
 * @scan: the active drm_mm scanner
 * @yesde: drm_mm_yesde to remove
 *
 * Nodes **must** be removed in exactly the reverse order from the scan list as
 * they have been added (e.g. using list_add() as they are added and then
 * list_for_each() over that eviction list to remove), otherwise the internal
 * state of the memory manager will be corrupted.
 *
 * When the scan list is empty, the selected memory yesdes can be freed. An
 * immediately following drm_mm_insert_yesde_in_range_generic() or one of the
 * simpler versions of that function with !DRM_MM_SEARCH_BEST will then return
 * the just freed block (because it's at the top of the free_stack list).
 *
 * Returns:
 * True if this block should be evicted, false otherwise. Will always
 * return false when yes hole has been found.
 */
bool drm_mm_scan_remove_block(struct drm_mm_scan *scan,
			      struct drm_mm_yesde *yesde)
{
	struct drm_mm_yesde *prev_yesde;

	DRM_MM_BUG_ON(yesde->mm != scan->mm);
	DRM_MM_BUG_ON(!drm_mm_yesde_scanned_block(yesde));
	__clear_bit(DRM_MM_NODE_SCANNED_BIT, &yesde->flags);

	DRM_MM_BUG_ON(!yesde->mm->scan_active);
	yesde->mm->scan_active--;

	/* During drm_mm_scan_add_block() we decoupled this yesde leaving
	 * its pointers intact. Now that the caller is walking back along
	 * the eviction list we can restore this block into its rightful
	 * place on the full yesde_list. To confirm that the caller is walking
	 * backwards correctly we check that prev_yesde->next == yesde->next,
	 * i.e. both believe the same yesde should be on the other side of the
	 * hole.
	 */
	prev_yesde = list_prev_entry(yesde, yesde_list);
	DRM_MM_BUG_ON(list_next_entry(prev_yesde, yesde_list) !=
		      list_next_entry(yesde, yesde_list));
	list_add(&yesde->yesde_list, &prev_yesde->yesde_list);

	return (yesde->start + yesde->size > scan->hit_start &&
		yesde->start < scan->hit_end);
}
EXPORT_SYMBOL(drm_mm_scan_remove_block);

/**
 * drm_mm_scan_color_evict - evict overlapping yesdes on either side of hole
 * @scan: drm_mm scan with target hole
 *
 * After completing an eviction scan and removing the selected yesdes, we may
 * need to remove a few more yesdes from either side of the target hole if
 * mm.color_adjust is being used.
 *
 * Returns:
 * A yesde to evict, or NULL if there are yes overlapping yesdes.
 */
struct drm_mm_yesde *drm_mm_scan_color_evict(struct drm_mm_scan *scan)
{
	struct drm_mm *mm = scan->mm;
	struct drm_mm_yesde *hole;
	u64 hole_start, hole_end;

	DRM_MM_BUG_ON(list_empty(&mm->hole_stack));

	if (!mm->color_adjust)
		return NULL;

	/*
	 * The hole found during scanning should ideally be the first element
	 * in the hole_stack list, but due to side-effects in the driver it
	 * may yest be.
	 */
	list_for_each_entry(hole, &mm->hole_stack, hole_stack) {
		hole_start = __drm_mm_hole_yesde_start(hole);
		hole_end = hole_start + hole->hole_size;

		if (hole_start <= scan->hit_start &&
		    hole_end >= scan->hit_end)
			break;
	}

	/* We should only be called after we found the hole previously */
	DRM_MM_BUG_ON(&hole->hole_stack == &mm->hole_stack);
	if (unlikely(&hole->hole_stack == &mm->hole_stack))
		return NULL;

	DRM_MM_BUG_ON(hole_start > scan->hit_start);
	DRM_MM_BUG_ON(hole_end < scan->hit_end);

	mm->color_adjust(hole, scan->color, &hole_start, &hole_end);
	if (hole_start > scan->hit_start)
		return hole;
	if (hole_end < scan->hit_end)
		return list_next_entry(hole, yesde_list);

	return NULL;
}
EXPORT_SYMBOL(drm_mm_scan_color_evict);

/**
 * drm_mm_init - initialize a drm-mm allocator
 * @mm: the drm_mm structure to initialize
 * @start: start of the range managed by @mm
 * @size: end of the range managed by @mm
 *
 * Note that @mm must be cleared to 0 before calling this function.
 */
void drm_mm_init(struct drm_mm *mm, u64 start, u64 size)
{
	DRM_MM_BUG_ON(start + size <= start);

	mm->color_adjust = NULL;

	INIT_LIST_HEAD(&mm->hole_stack);
	mm->interval_tree = RB_ROOT_CACHED;
	mm->holes_size = RB_ROOT_CACHED;
	mm->holes_addr = RB_ROOT;

	/* Clever trick to avoid a special case in the free hole tracking. */
	INIT_LIST_HEAD(&mm->head_yesde.yesde_list);
	mm->head_yesde.flags = 0;
	mm->head_yesde.mm = mm;
	mm->head_yesde.start = start + size;
	mm->head_yesde.size = -size;
	add_hole(&mm->head_yesde);

	mm->scan_active = 0;
}
EXPORT_SYMBOL(drm_mm_init);

/**
 * drm_mm_takedown - clean up a drm_mm allocator
 * @mm: drm_mm allocator to clean up
 *
 * Note that it is a bug to call this function on an allocator which is yest
 * clean.
 */
void drm_mm_takedown(struct drm_mm *mm)
{
	if (WARN(!drm_mm_clean(mm),
		 "Memory manager yest clean during takedown.\n"))
		show_leaks(mm);
}
EXPORT_SYMBOL(drm_mm_takedown);

static u64 drm_mm_dump_hole(struct drm_printer *p, const struct drm_mm_yesde *entry)
{
	u64 start, size;

	size = entry->hole_size;
	if (size) {
		start = drm_mm_hole_yesde_start(entry);
		drm_printf(p, "%#018llx-%#018llx: %llu: free\n",
			   start, start + size, size);
	}

	return size;
}
/**
 * drm_mm_print - print allocator state
 * @mm: drm_mm allocator to print
 * @p: DRM printer to use
 */
void drm_mm_print(const struct drm_mm *mm, struct drm_printer *p)
{
	const struct drm_mm_yesde *entry;
	u64 total_used = 0, total_free = 0, total = 0;

	total_free += drm_mm_dump_hole(p, &mm->head_yesde);

	drm_mm_for_each_yesde(entry, mm) {
		drm_printf(p, "%#018llx-%#018llx: %llu: used\n", entry->start,
			   entry->start + entry->size, entry->size);
		total_used += entry->size;
		total_free += drm_mm_dump_hole(p, entry);
	}
	total = total_free + total_used;

	drm_printf(p, "total: %llu, used %llu free %llu\n", total,
		   total_used, total_free);
}
EXPORT_SYMBOL(drm_mm_print);
