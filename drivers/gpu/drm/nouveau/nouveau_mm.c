/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_mm.h"

static inline void
region_put(struct nouveau_mm *rmm, struct nouveau_mm_node *a)
{
	list_del(&a->nl_entry);
	list_del(&a->fl_entry);
	kfree(a);
}

static struct nouveau_mm_node *
region_split(struct nouveau_mm *rmm, struct nouveau_mm_node *a, u32 size)
{
	struct nouveau_mm_node *b;

	if (a->length == size)
		return a;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (unlikely(b == NULL))
		return NULL;

	b->offset = a->offset;
	b->length = size;
	b->free   = a->free;
	b->type   = a->type;
	a->offset += size;
	a->length -= size;
	list_add_tail(&b->nl_entry, &a->nl_entry);
	if (b->free)
		list_add_tail(&b->fl_entry, &a->fl_entry);
	return b;
}

static struct nouveau_mm_node *
nouveau_mm_merge(struct nouveau_mm *rmm, struct nouveau_mm_node *this)
{
	struct nouveau_mm_node *prev, *next;

	/* try to merge with free adjacent entries of same type */
	prev = list_entry(this->nl_entry.prev, struct nouveau_mm_node, nl_entry);
	if (this->nl_entry.prev != &rmm->nodes) {
		if (prev->free && prev->type == this->type) {
			prev->length += this->length;
			region_put(rmm, this);
			this = prev;
		}
	}

	next = list_entry(this->nl_entry.next, struct nouveau_mm_node, nl_entry);
	if (this->nl_entry.next != &rmm->nodes) {
		if (next->free && next->type == this->type) {
			next->offset  = this->offset;
			next->length += this->length;
			region_put(rmm, this);
			this = next;
		}
	}

	return this;
}

void
nouveau_mm_put(struct nouveau_mm *rmm, struct nouveau_mm_node *this)
{
	u32 block_s, block_l;

	this->free = true;
	list_add(&this->fl_entry, &rmm->free);
	this = nouveau_mm_merge(rmm, this);

	/* any entirely free blocks now?  we'll want to remove typing
	 * on them now so they can be use for any memory allocation
	 */
	block_s = roundup(this->offset, rmm->block_size);
	if (block_s + rmm->block_size > this->offset + this->length)
		return;

	/* split off any still-typed region at the start */
	if (block_s != this->offset) {
		if (!region_split(rmm, this, block_s - this->offset))
			return;
	}

	/* split off the soon-to-be-untyped block(s) */
	block_l = rounddown(this->length, rmm->block_size);
	if (block_l != this->length) {
		this = region_split(rmm, this, block_l);
		if (!this)
			return;
	}

	/* mark as having no type, and retry merge with any adjacent
	 * untyped blocks
	 */
	this->type = 0;
	nouveau_mm_merge(rmm, this);
}

int
nouveau_mm_get(struct nouveau_mm *rmm, int type, u32 size, u32 size_nc,
	       u32 align, struct nouveau_mm_node **pnode)
{
	struct nouveau_mm_node *this, *tmp, *next;
	u32 splitoff, avail, alloc;

	list_for_each_entry_safe(this, tmp, &rmm->free, fl_entry) {
		next = list_entry(this->nl_entry.next, struct nouveau_mm_node, nl_entry);
		if (this->nl_entry.next == &rmm->nodes)
			next = NULL;

		/* skip wrongly typed blocks */
		if (this->type && this->type != type)
			continue;

		/* account for alignment */
		splitoff = this->offset & (align - 1);
		if (splitoff)
			splitoff = align - splitoff;

		if (this->length <= splitoff)
			continue;

		/* determine total memory available from this, and
		 * the next block (if appropriate)
		 */
		avail = this->length;
		if (next && next->free && (!next->type || next->type == type))
			avail += next->length;

		avail -= splitoff;

		/* determine allocation size */
		if (size_nc) {
			alloc = min(avail, size);
			alloc = rounddown(alloc, size_nc);
			if (alloc == 0)
				continue;
		} else {
			alloc = size;
			if (avail < alloc)
				continue;
		}

		/* untyped block, split off a chunk that's a multiple
		 * of block_size and type it
		 */
		if (!this->type) {
			u32 block = roundup(alloc + splitoff, rmm->block_size);
			if (this->length < block)
				continue;

			this = region_split(rmm, this, block);
			if (!this)
				return -ENOMEM;

			this->type = type;
		}

		/* stealing memory from adjacent block */
		if (alloc > this->length) {
			u32 amount = alloc - (this->length - splitoff);

			if (!next->type) {
				amount = roundup(amount, rmm->block_size);

				next = region_split(rmm, next, amount);
				if (!next)
					return -ENOMEM;

				next->type = type;
			}

			this->length += amount;
			next->offset += amount;
			next->length -= amount;
			if (!next->length) {
				list_del(&next->nl_entry);
				list_del(&next->fl_entry);
				kfree(next);
			}
		}

		if (splitoff) {
			if (!region_split(rmm, this, splitoff))
				return -ENOMEM;
		}

		this = region_split(rmm, this, alloc);
		if (this == NULL)
			return -ENOMEM;

		this->free = false;
		list_del(&this->fl_entry);
		*pnode = this;
		return 0;
	}

	return -ENOMEM;
}

int
nouveau_mm_init(struct nouveau_mm **prmm, u32 offset, u32 length, u32 block)
{
	struct nouveau_mm *rmm;
	struct nouveau_mm_node *heap;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return -ENOMEM;
	heap->free = true;
	heap->offset = roundup(offset, block);
	heap->length = rounddown(offset + length, block) - heap->offset;

	rmm = kzalloc(sizeof(*rmm), GFP_KERNEL);
	if (!rmm) {
		kfree(heap);
		return -ENOMEM;
	}
	rmm->block_size = block;
	mutex_init(&rmm->mutex);
	INIT_LIST_HEAD(&rmm->nodes);
	INIT_LIST_HEAD(&rmm->free);
	list_add(&heap->nl_entry, &rmm->nodes);
	list_add(&heap->fl_entry, &rmm->free);

	*prmm = rmm;
	return 0;
}

int
nouveau_mm_fini(struct nouveau_mm **prmm)
{
	struct nouveau_mm *rmm = *prmm;
	struct nouveau_mm_node *heap =
		list_first_entry(&rmm->nodes, struct nouveau_mm_node, nl_entry);

	if (!list_is_singular(&rmm->nodes))
		return -EBUSY;

	kfree(heap);
	kfree(rmm);
	*prmm = NULL;
	return 0;
}
