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
region_put(struct nouveau_mm *mm, struct nouveau_mm_node *a)
{
	list_del(&a->nl_entry);
	list_del(&a->fl_entry);
	kfree(a);
}

static struct nouveau_mm_node *
region_split(struct nouveau_mm *mm, struct nouveau_mm_node *a, u32 size)
{
	struct nouveau_mm_node *b;

	if (a->length == size)
		return a;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (unlikely(b == NULL))
		return NULL;

	b->offset = a->offset;
	b->length = size;
	b->type   = a->type;
	a->offset += size;
	a->length -= size;
	list_add_tail(&b->nl_entry, &a->nl_entry);
	if (b->type == 0)
		list_add_tail(&b->fl_entry, &a->fl_entry);
	return b;
}

#define node(root, dir) ((root)->nl_entry.dir == &mm->nodes) ? NULL : \
	list_entry((root)->nl_entry.dir, struct nouveau_mm_node, nl_entry)

void
nouveau_mm_put(struct nouveau_mm *mm, struct nouveau_mm_node *this)
{
	struct nouveau_mm_node *prev = node(this, prev);
	struct nouveau_mm_node *next = node(this, next);

	list_add(&this->fl_entry, &mm->free);
	this->type = 0;

	if (prev && prev->type == 0) {
		prev->length += this->length;
		region_put(mm, this);
		this = prev;
	}

	if (next && next->type == 0) {
		next->offset  = this->offset;
		next->length += this->length;
		region_put(mm, this);
	}
}

int
nouveau_mm_get(struct nouveau_mm *mm, int type, u32 size, u32 size_nc,
	       u32 align, struct nouveau_mm_node **pnode)
{
	struct nouveau_mm_node *prev, *this, *next;
	u32 min = size_nc ? size_nc : size;
	u32 align_mask = align - 1;
	u32 splitoff;
	u32 s, e;

	list_for_each_entry(this, &mm->free, fl_entry) {
		e = this->offset + this->length;
		s = this->offset;

		prev = node(this, prev);
		if (prev && prev->type != type)
			s = roundup(s, mm->block_size);

		next = node(this, next);
		if (next && next->type != type)
			e = rounddown(e, mm->block_size);

		s  = (s + align_mask) & ~align_mask;
		e &= ~align_mask;
		if (s > e || e - s < min)
			continue;

		splitoff = s - this->offset;
		if (splitoff && !region_split(mm, this, splitoff))
			return -ENOMEM;

		this = region_split(mm, this, min(size, e - s));
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
nouveau_mm_init(struct nouveau_mm *mm, u32 offset, u32 length, u32 block)
{
	struct nouveau_mm_node *node;

	if (block) {
		mutex_init(&mm->mutex);
		INIT_LIST_HEAD(&mm->nodes);
		INIT_LIST_HEAD(&mm->free);
		mm->block_size = block;
		mm->heap_nodes = 0;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->offset = roundup(offset, mm->block_size);
	node->length = rounddown(offset + length, mm->block_size) - node->offset;

	list_add_tail(&node->nl_entry, &mm->nodes);
	list_add_tail(&node->fl_entry, &mm->free);
	mm->heap_nodes++;
	return 0;
}

int
nouveau_mm_fini(struct nouveau_mm *mm)
{
	struct nouveau_mm_node *node, *heap =
		list_first_entry(&mm->nodes, struct nouveau_mm_node, nl_entry);
	int nodes = 0;

	list_for_each_entry(node, &mm->nodes, nl_entry) {
		if (nodes++ == mm->heap_nodes) {
			printk(KERN_ERR "nouveau_mm in use at destroy time!\n");
			list_for_each_entry(node, &mm->nodes, nl_entry) {
				printk(KERN_ERR "0x%02x: 0x%08x 0x%08x\n",
				       node->type, node->offset, node->length);
			}
			WARN_ON(1);
			return -EBUSY;
		}
	}

	kfree(heap);
	return 0;
}
