/*
 * Copyright 2012 Red Hat Inc.
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
#include <core/mm.h>

#define node(root, dir) ((root)->nl_entry.dir == &mm->nodes) ? NULL :          \
	list_entry((root)->nl_entry.dir, struct nvkm_mm_node, nl_entry)

static void
nvkm_mm_dump(struct nvkm_mm *mm, const char *header)
{
	struct nvkm_mm_node *node;

	printk(KERN_ERR "nvkm: %s\n", header);
	printk(KERN_ERR "nvkm: node list:\n");
	list_for_each_entry(node, &mm->nodes, nl_entry) {
		printk(KERN_ERR "nvkm: \t%08x %08x %d\n",
		       node->offset, node->length, node->type);
	}
	printk(KERN_ERR "nvkm: free list:\n");
	list_for_each_entry(node, &mm->free, fl_entry) {
		printk(KERN_ERR "nvkm: \t%08x %08x %d\n",
		       node->offset, node->length, node->type);
	}
}

void
nvkm_mm_free(struct nvkm_mm *mm, struct nvkm_mm_node **pthis)
{
	struct nvkm_mm_node *this = *pthis;

	if (this) {
		struct nvkm_mm_node *prev = node(this, prev);
		struct nvkm_mm_node *next = node(this, next);

		if (prev && prev->type == NVKM_MM_TYPE_NONE) {
			prev->length += this->length;
			list_del(&this->nl_entry);
			kfree(this); this = prev;
		}

		if (next && next->type == NVKM_MM_TYPE_NONE) {
			next->offset  = this->offset;
			next->length += this->length;
			if (this->type == NVKM_MM_TYPE_NONE)
				list_del(&this->fl_entry);
			list_del(&this->nl_entry);
			kfree(this); this = NULL;
		}

		if (this && this->type != NVKM_MM_TYPE_NONE) {
			list_for_each_entry(prev, &mm->free, fl_entry) {
				if (this->offset < prev->offset)
					break;
			}

			list_add_tail(&this->fl_entry, &prev->fl_entry);
			this->type = NVKM_MM_TYPE_NONE;
		}
	}

	*pthis = NULL;
}

static struct nvkm_mm_node *
region_head(struct nvkm_mm *mm, struct nvkm_mm_node *a, u32 size)
{
	struct nvkm_mm_node *b;

	if (a->length == size)
		return a;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (unlikely(b == NULL))
		return NULL;

	b->offset = a->offset;
	b->length = size;
	b->heap   = a->heap;
	b->type   = a->type;
	a->offset += size;
	a->length -= size;
	list_add_tail(&b->nl_entry, &a->nl_entry);
	if (b->type == NVKM_MM_TYPE_NONE)
		list_add_tail(&b->fl_entry, &a->fl_entry);

	return b;
}

int
nvkm_mm_head(struct nvkm_mm *mm, u8 heap, u8 type, u32 size_max, u32 size_min,
	     u32 align, struct nvkm_mm_node **pnode)
{
	struct nvkm_mm_node *prev, *this, *next;
	u32 mask = align - 1;
	u32 splitoff;
	u32 s, e;

	BUG_ON(type == NVKM_MM_TYPE_NONE || type == NVKM_MM_TYPE_HOLE);

	list_for_each_entry(this, &mm->free, fl_entry) {
		if (unlikely(heap != NVKM_MM_HEAP_ANY)) {
			if (this->heap != heap)
				continue;
		}
		e = this->offset + this->length;
		s = this->offset;

		prev = node(this, prev);
		if (prev && prev->type != type)
			s = roundup(s, mm->block_size);

		next = node(this, next);
		if (next && next->type != type)
			e = rounddown(e, mm->block_size);

		s  = (s + mask) & ~mask;
		e &= ~mask;
		if (s > e || e - s < size_min)
			continue;

		splitoff = s - this->offset;
		if (splitoff && !region_head(mm, this, splitoff))
			return -ENOMEM;

		this = region_head(mm, this, min(size_max, e - s));
		if (!this)
			return -ENOMEM;

		this->type = type;
		list_del(&this->fl_entry);
		*pnode = this;
		return 0;
	}

	return -ENOSPC;
}

static struct nvkm_mm_node *
region_tail(struct nvkm_mm *mm, struct nvkm_mm_node *a, u32 size)
{
	struct nvkm_mm_node *b;

	if (a->length == size)
		return a;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (unlikely(b == NULL))
		return NULL;

	a->length -= size;
	b->offset  = a->offset + a->length;
	b->length  = size;
	b->heap    = a->heap;
	b->type    = a->type;

	list_add(&b->nl_entry, &a->nl_entry);
	if (b->type == NVKM_MM_TYPE_NONE)
		list_add(&b->fl_entry, &a->fl_entry);

	return b;
}

int
nvkm_mm_tail(struct nvkm_mm *mm, u8 heap, u8 type, u32 size_max, u32 size_min,
	     u32 align, struct nvkm_mm_node **pnode)
{
	struct nvkm_mm_node *prev, *this, *next;
	u32 mask = align - 1;

	BUG_ON(type == NVKM_MM_TYPE_NONE || type == NVKM_MM_TYPE_HOLE);

	list_for_each_entry_reverse(this, &mm->free, fl_entry) {
		u32 e = this->offset + this->length;
		u32 s = this->offset;
		u32 c = 0, a;
		if (unlikely(heap != NVKM_MM_HEAP_ANY)) {
			if (this->heap != heap)
				continue;
		}

		prev = node(this, prev);
		if (prev && prev->type != type)
			s = roundup(s, mm->block_size);

		next = node(this, next);
		if (next && next->type != type) {
			e = rounddown(e, mm->block_size);
			c = next->offset - e;
		}

		s = (s + mask) & ~mask;
		a = e - s;
		if (s > e || a < size_min)
			continue;

		a  = min(a, size_max);
		s  = (e - a) & ~mask;
		c += (e - s) - a;

		if (c && !region_tail(mm, this, c))
			return -ENOMEM;

		this = region_tail(mm, this, a);
		if (!this)
			return -ENOMEM;

		this->type = type;
		list_del(&this->fl_entry);
		*pnode = this;
		return 0;
	}

	return -ENOSPC;
}

int
nvkm_mm_init(struct nvkm_mm *mm, u32 offset, u32 length, u32 block)
{
	struct nvkm_mm_node *node, *prev;
	u32 next;

	if (nvkm_mm_initialised(mm)) {
		prev = list_last_entry(&mm->nodes, typeof(*node), nl_entry);
		next = prev->offset + prev->length;
		if (next != offset) {
			BUG_ON(next > offset);
			if (!(node = kzalloc(sizeof(*node), GFP_KERNEL)))
				return -ENOMEM;
			node->type   = NVKM_MM_TYPE_HOLE;
			node->offset = next;
			node->length = offset - next;
			list_add_tail(&node->nl_entry, &mm->nodes);
		}
		BUG_ON(block != mm->block_size);
	} else {
		INIT_LIST_HEAD(&mm->nodes);
		INIT_LIST_HEAD(&mm->free);
		mm->block_size = block;
		mm->heap_nodes = 0;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	if (length) {
		node->offset  = roundup(offset, mm->block_size);
		node->length  = rounddown(offset + length, mm->block_size);
		node->length -= node->offset;
	}

	list_add_tail(&node->nl_entry, &mm->nodes);
	list_add_tail(&node->fl_entry, &mm->free);
	node->heap = ++mm->heap_nodes;
	return 0;
}

int
nvkm_mm_fini(struct nvkm_mm *mm)
{
	struct nvkm_mm_node *node, *temp;
	int nodes = 0;

	if (!nvkm_mm_initialised(mm))
		return 0;

	list_for_each_entry(node, &mm->nodes, nl_entry) {
		if (node->type != NVKM_MM_TYPE_HOLE) {
			if (++nodes > mm->heap_nodes) {
				nvkm_mm_dump(mm, "mm not clean!");
				return -EBUSY;
			}
		}
	}

	list_for_each_entry_safe(node, temp, &mm->nodes, nl_entry) {
		list_del(&node->nl_entry);
		kfree(node);
	}

	mm->heap_nodes = 0;
	return 0;
}
