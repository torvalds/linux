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

#include "core/os.h"
#include "core/mm.h"

#define node(root, dir) ((root)->nl_entry.dir == &mm->nodes) ? NULL : \
	list_entry((root)->nl_entry.dir, struct nouveau_mm_node, nl_entry)

void
nouveau_mm_free(struct nouveau_mm *mm, struct nouveau_mm_node **pthis)
{
	struct nouveau_mm_node *this = *pthis;

	if (this) {
		struct nouveau_mm_node *prev = node(this, prev);
		struct nouveau_mm_node *next = node(this, next);

		if (prev && prev->type == 0) {
			prev->length += this->length;
			list_del(&this->nl_entry);
			kfree(this); this = prev;
		}

		if (next && next->type == 0) {
			next->offset  = this->offset;
			next->length += this->length;
			if (this->type == 0)
				list_del(&this->fl_entry);
			list_del(&this->nl_entry);
			kfree(this); this = NULL;
		}

		if (this && this->type != 0) {
			list_for_each_entry(prev, &mm->free, fl_entry) {
				if (this->offset < prev->offset)
					break;
			}

			list_add_tail(&this->fl_entry, &prev->fl_entry);
			this->type = 0;
		}
	}

	*pthis = NULL;
}

static struct nouveau_mm_node *
region_head(struct nouveau_mm *mm, struct nouveau_mm_node *a, u32 size)
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

int
nouveau_mm_head(struct nouveau_mm *mm, u8 type, u32 size_max, u32 size_min,
		u32 align, struct nouveau_mm_node **pnode)
{
	struct nouveau_mm_node *prev, *this, *next;
	u32 mask = align - 1;
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

static struct nouveau_mm_node *
region_tail(struct nouveau_mm *mm, struct nouveau_mm_node *a, u32 size)
{
	struct nouveau_mm_node *b;

	if (a->length == size)
		return a;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (unlikely(b == NULL))
		return NULL;

	a->length -= size;
	b->offset  = a->offset + a->length;
	b->length  = size;
	b->type    = a->type;

	list_add(&b->nl_entry, &a->nl_entry);
	if (b->type == 0)
		list_add(&b->fl_entry, &a->fl_entry);
	return b;
}

int
nouveau_mm_tail(struct nouveau_mm *mm, u8 type, u32 size_max, u32 size_min,
		u32 align, struct nouveau_mm_node **pnode)
{
	struct nouveau_mm_node *prev, *this, *next;
	u32 mask = align - 1;

	list_for_each_entry_reverse(this, &mm->free, fl_entry) {
		u32 e = this->offset + this->length;
		u32 s = this->offset;
		u32 c = 0, a;

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
	mm->heap_size += length;
	return 0;
}

int
nouveau_mm_fini(struct nouveau_mm *mm)
{
	struct nouveau_mm_node *node, *heap =
		list_first_entry(&mm->nodes, struct nouveau_mm_node, nl_entry);
	int nodes = 0;

	list_for_each_entry(node, &mm->nodes, nl_entry) {
		if (WARN_ON(nodes++ == mm->heap_nodes))
			return -EBUSY;
	}

	kfree(heap);
	return 0;
}
