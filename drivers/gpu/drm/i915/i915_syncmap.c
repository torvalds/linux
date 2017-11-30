/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/slab.h>

#include "i915_syncmap.h"

#include "i915_gem.h" /* GEM_BUG_ON() */
#include "i915_selftest.h"

#define SHIFT ilog2(KSYNCMAP)
#define MASK (KSYNCMAP - 1)

/*
 * struct i915_syncmap is a layer of a radixtree that maps a u64 fence
 * context id to the last u32 fence seqno waited upon from that context.
 * Unlike lib/radixtree it uses a parent pointer that allows traversal back to
 * the root. This allows us to access the whole tree via a single pointer
 * to the most recently used layer. We expect fence contexts to be dense
 * and most reuse to be on the same i915_gem_context but on neighbouring
 * engines (i.e. on adjacent contexts) and reuse the same leaf, a very
 * effective lookup cache. If the new lookup is not on the same leaf, we
 * expect it to be on the neighbouring branch.
 *
 * A leaf holds an array of u32 seqno, and has height 0. The bitmap field
 * allows us to store whether a particular seqno is valid (i.e. allows us
 * to distinguish unset from 0).
 *
 * A branch holds an array of layer pointers, and has height > 0, and always
 * has at least 2 layers (either branches or leaves) below it.
 *
 * For example,
 *	for x in
 *	  0 1 2 0x10 0x11 0x200 0x201
 *	  0x500000 0x500001 0x503000 0x503001
 *	  0xE<<60:
 *		i915_syncmap_set(&sync, x, lower_32_bits(x));
 * will build a tree like:
 *	0xXXXXXXXXXXXXXXXX
 *	0-> 0x0000000000XXXXXX
 *	|   0-> 0x0000000000000XXX
 *	|   |   0-> 0x00000000000000XX
 *	|   |   |   0-> 0x000000000000000X 0:0, 1:1, 2:2
 *	|   |   |   1-> 0x000000000000001X 0:10, 1:11
 *	|   |   2-> 0x000000000000020X 0:200, 1:201
 *	|   5-> 0x000000000050XXXX
 *	|       0-> 0x000000000050000X 0:500000, 1:500001
 *	|       3-> 0x000000000050300X 0:503000, 1:503001
 *	e-> 0xe00000000000000X e:e
 */

struct i915_syncmap {
	u64 prefix;
	unsigned int height;
	unsigned int bitmap;
	struct i915_syncmap *parent;
	/*
	 * Following this header is an array of either seqno or child pointers:
	 * union {
	 *	u32 seqno[KSYNCMAP];
	 *	struct i915_syncmap *child[KSYNCMAP];
	 * };
	 */
};

/**
 * i915_syncmap_init -- initialise the #i915_syncmap
 * @root - pointer to the #i915_syncmap
 */
void i915_syncmap_init(struct i915_syncmap **root)
{
	BUILD_BUG_ON_NOT_POWER_OF_2(KSYNCMAP);
	BUILD_BUG_ON_NOT_POWER_OF_2(SHIFT);
	BUILD_BUG_ON(KSYNCMAP > BITS_PER_BYTE * sizeof((*root)->bitmap));
	*root = NULL;
}

static inline u32 *__sync_seqno(struct i915_syncmap *p)
{
	GEM_BUG_ON(p->height);
	return (u32 *)(p + 1);
}

static inline struct i915_syncmap **__sync_child(struct i915_syncmap *p)
{
	GEM_BUG_ON(!p->height);
	return (struct i915_syncmap **)(p + 1);
}

static inline unsigned int
__sync_branch_idx(const struct i915_syncmap *p, u64 id)
{
	return (id >> p->height) & MASK;
}

static inline unsigned int
__sync_leaf_idx(const struct i915_syncmap *p, u64 id)
{
	GEM_BUG_ON(p->height);
	return id & MASK;
}

static inline u64 __sync_branch_prefix(const struct i915_syncmap *p, u64 id)
{
	return id >> p->height >> SHIFT;
}

static inline u64 __sync_leaf_prefix(const struct i915_syncmap *p, u64 id)
{
	GEM_BUG_ON(p->height);
	return id >> SHIFT;
}

static inline bool seqno_later(u32 a, u32 b)
{
	return (s32)(a - b) >= 0;
}

/**
 * i915_syncmap_is_later -- compare against the last know sync point
 * @root - pointer to the #i915_syncmap
 * @id - the context id (other timeline) we are synchronising to
 * @seqno - the sequence number along the other timeline
 *
 * If we have already synchronised this @root timeline with another (@id) then
 * we can omit any repeated or earlier synchronisation requests. If the two
 * timelines are already coupled, we can also omit the dependency between the
 * two as that is already known via the timeline.
 *
 * Returns true if the two timelines are already synchronised wrt to @seqno,
 * false if not and the synchronisation must be emitted.
 */
bool i915_syncmap_is_later(struct i915_syncmap **root, u64 id, u32 seqno)
{
	struct i915_syncmap *p;
	unsigned int idx;

	p = *root;
	if (!p)
		return false;

	if (likely(__sync_leaf_prefix(p, id) == p->prefix))
		goto found;

	/* First climb the tree back to a parent branch */
	do {
		p = p->parent;
		if (!p)
			return false;

		if (__sync_branch_prefix(p, id) == p->prefix)
			break;
	} while (1);

	/* And then descend again until we find our leaf */
	do {
		if (!p->height)
			break;

		p = __sync_child(p)[__sync_branch_idx(p, id)];
		if (!p)
			return false;

		if (__sync_branch_prefix(p, id) != p->prefix)
			return false;
	} while (1);

	*root = p;
found:
	idx = __sync_leaf_idx(p, id);
	if (!(p->bitmap & BIT(idx)))
		return false;

	return seqno_later(__sync_seqno(p)[idx], seqno);
}

static struct i915_syncmap *
__sync_alloc_leaf(struct i915_syncmap *parent, u64 id)
{
	struct i915_syncmap *p;

	p = kmalloc(sizeof(*p) + KSYNCMAP * sizeof(u32), GFP_KERNEL);
	if (unlikely(!p))
		return NULL;

	p->parent = parent;
	p->height = 0;
	p->bitmap = 0;
	p->prefix = __sync_leaf_prefix(p, id);
	return p;
}

static inline void __sync_set_seqno(struct i915_syncmap *p, u64 id, u32 seqno)
{
	unsigned int idx = __sync_leaf_idx(p, id);

	p->bitmap |= BIT(idx);
	__sync_seqno(p)[idx] = seqno;
}

static inline void __sync_set_child(struct i915_syncmap *p,
				    unsigned int idx,
				    struct i915_syncmap *child)
{
	p->bitmap |= BIT(idx);
	__sync_child(p)[idx] = child;
}

static noinline int __sync_set(struct i915_syncmap **root, u64 id, u32 seqno)
{
	struct i915_syncmap *p = *root;
	unsigned int idx;

	if (!p) {
		p = __sync_alloc_leaf(NULL, id);
		if (unlikely(!p))
			return -ENOMEM;

		goto found;
	}

	/* Caller handled the likely cached case */
	GEM_BUG_ON(__sync_leaf_prefix(p, id) == p->prefix);

	/* Climb back up the tree until we find a common prefix */
	do {
		if (!p->parent)
			break;

		p = p->parent;

		if (__sync_branch_prefix(p, id) == p->prefix)
			break;
	} while (1);

	/*
	 * No shortcut, we have to descend the tree to find the right layer
	 * containing this fence.
	 *
	 * Each layer in the tree holds 16 (KSYNCMAP) pointers, either fences
	 * or lower layers. Leaf nodes (height = 0) contain the fences, all
	 * other nodes (height > 0) are internal layers that point to a lower
	 * node. Each internal layer has at least 2 descendents.
	 *
	 * Starting at the top, we check whether the current prefix matches. If
	 * it doesn't, we have gone past our target and need to insert a join
	 * into the tree, and a new leaf node for the target as a descendent
	 * of the join, as well as the original layer.
	 *
	 * The matching prefix means we are still following the right branch
	 * of the tree. If it has height 0, we have found our leaf and just
	 * need to replace the fence slot with ourselves. If the height is
	 * not zero, our slot contains the next layer in the tree (unless
	 * it is empty, in which case we can add ourselves as a new leaf).
	 * As descend the tree the prefix grows (and height decreases).
	 */
	do {
		struct i915_syncmap *next;

		if (__sync_branch_prefix(p, id) != p->prefix) {
			unsigned int above;

			/* Insert a join above the current layer */
			next = kzalloc(sizeof(*next) + KSYNCMAP * sizeof(next),
				       GFP_KERNEL);
			if (unlikely(!next))
				return -ENOMEM;

			/* Compute the height at which these two diverge */
			above = fls64(__sync_branch_prefix(p, id) ^ p->prefix);
			above = round_up(above, SHIFT);
			next->height = above + p->height;
			next->prefix = __sync_branch_prefix(next, id);

			/* Insert the join into the parent */
			if (p->parent) {
				idx = __sync_branch_idx(p->parent, id);
				__sync_child(p->parent)[idx] = next;
				GEM_BUG_ON(!(p->parent->bitmap & BIT(idx)));
			}
			next->parent = p->parent;

			/* Compute the idx of the other branch, not our id! */
			idx = p->prefix >> (above - SHIFT) & MASK;
			__sync_set_child(next, idx, p);
			p->parent = next;

			/* Ascend to the join */
			p = next;
		} else {
			if (!p->height)
				break;
		}

		/* Descend into the next layer */
		GEM_BUG_ON(!p->height);
		idx = __sync_branch_idx(p, id);
		next = __sync_child(p)[idx];
		if (!next) {
			next = __sync_alloc_leaf(p, id);
			if (unlikely(!next))
				return -ENOMEM;

			__sync_set_child(p, idx, next);
			p = next;
			break;
		}

		p = next;
	} while (1);

found:
	GEM_BUG_ON(p->prefix != __sync_leaf_prefix(p, id));
	__sync_set_seqno(p, id, seqno);
	*root = p;
	return 0;
}

/**
 * i915_syncmap_set -- mark the most recent syncpoint between contexts
 * @root - pointer to the #i915_syncmap
 * @id - the context id (other timeline) we have synchronised to
 * @seqno - the sequence number along the other timeline
 *
 * When we synchronise this @root timeline with another (@id), we also know
 * that we have synchronized with all previous seqno along that timeline. If
 * we then have a request to synchronise with the same seqno or older, we can
 * omit it, see i915_syncmap_is_later()
 *
 * Returns 0 on success, or a negative error code.
 */
int i915_syncmap_set(struct i915_syncmap **root, u64 id, u32 seqno)
{
	struct i915_syncmap *p = *root;

	/*
	 * We expect to be called in sequence following is_later(id), which
	 * should have preloaded the root for us.
	 */
	if (likely(p && __sync_leaf_prefix(p, id) == p->prefix)) {
		__sync_set_seqno(p, id, seqno);
		return 0;
	}

	return __sync_set(root, id, seqno);
}

static void __sync_free(struct i915_syncmap *p)
{
	if (p->height) {
		unsigned int i;

		while ((i = ffs(p->bitmap))) {
			p->bitmap &= ~0u << i;
			__sync_free(__sync_child(p)[i - 1]);
		}
	}

	kfree(p);
}

/**
 * i915_syncmap_free -- free all memory associated with the syncmap
 * @root - pointer to the #i915_syncmap
 *
 * Either when the timeline is to be freed and we no longer need the sync
 * point tracking, or when the fences are all known to be signaled and the
 * sync point tracking is redundant, we can free the #i915_syncmap to recover
 * its allocations.
 *
 * Will reinitialise the @root pointer so that the #i915_syncmap is ready for
 * reuse.
 */
void i915_syncmap_free(struct i915_syncmap **root)
{
	struct i915_syncmap *p;

	p = *root;
	if (!p)
		return;

	while (p->parent)
		p = p->parent;

	__sync_free(p);
	*root = NULL;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/i915_syncmap.c"
#endif
