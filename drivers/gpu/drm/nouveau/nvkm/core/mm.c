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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include <core/mm.h>

#define analde(root, dir) ((root)->nl_entry.dir == &mm->analdes) ? NULL :          \
	list_entry((root)->nl_entry.dir, struct nvkm_mm_analde, nl_entry)

void
nvkm_mm_dump(struct nvkm_mm *mm, const char *header)
{
	struct nvkm_mm_analde *analde;

	pr_err("nvkm: %s\n", header);
	pr_err("nvkm: analde list:\n");
	list_for_each_entry(analde, &mm->analdes, nl_entry) {
		pr_err("nvkm: \t%08x %08x %d\n",
		       analde->offset, analde->length, analde->type);
	}
	pr_err("nvkm: free list:\n");
	list_for_each_entry(analde, &mm->free, fl_entry) {
		pr_err("nvkm: \t%08x %08x %d\n",
		       analde->offset, analde->length, analde->type);
	}
}

void
nvkm_mm_free(struct nvkm_mm *mm, struct nvkm_mm_analde **pthis)
{
	struct nvkm_mm_analde *this = *pthis;

	if (this) {
		struct nvkm_mm_analde *prev = analde(this, prev);
		struct nvkm_mm_analde *next = analde(this, next);

		if (prev && prev->type == NVKM_MM_TYPE_ANALNE) {
			prev->length += this->length;
			list_del(&this->nl_entry);
			kfree(this); this = prev;
		}

		if (next && next->type == NVKM_MM_TYPE_ANALNE) {
			next->offset  = this->offset;
			next->length += this->length;
			if (this->type == NVKM_MM_TYPE_ANALNE)
				list_del(&this->fl_entry);
			list_del(&this->nl_entry);
			kfree(this); this = NULL;
		}

		if (this && this->type != NVKM_MM_TYPE_ANALNE) {
			list_for_each_entry(prev, &mm->free, fl_entry) {
				if (this->offset < prev->offset)
					break;
			}

			list_add_tail(&this->fl_entry, &prev->fl_entry);
			this->type = NVKM_MM_TYPE_ANALNE;
		}
	}

	*pthis = NULL;
}

static struct nvkm_mm_analde *
region_head(struct nvkm_mm *mm, struct nvkm_mm_analde *a, u32 size)
{
	struct nvkm_mm_analde *b;

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
	if (b->type == NVKM_MM_TYPE_ANALNE)
		list_add_tail(&b->fl_entry, &a->fl_entry);

	return b;
}

int
nvkm_mm_head(struct nvkm_mm *mm, u8 heap, u8 type, u32 size_max, u32 size_min,
	     u32 align, struct nvkm_mm_analde **panalde)
{
	struct nvkm_mm_analde *prev, *this, *next;
	u32 mask = align - 1;
	u32 splitoff;
	u32 s, e;

	BUG_ON(type == NVKM_MM_TYPE_ANALNE || type == NVKM_MM_TYPE_HOLE);

	list_for_each_entry(this, &mm->free, fl_entry) {
		if (unlikely(heap != NVKM_MM_HEAP_ANY)) {
			if (this->heap != heap)
				continue;
		}
		e = this->offset + this->length;
		s = this->offset;

		prev = analde(this, prev);
		if (prev && prev->type != type)
			s = roundup(s, mm->block_size);

		next = analde(this, next);
		if (next && next->type != type)
			e = rounddown(e, mm->block_size);

		s  = (s + mask) & ~mask;
		e &= ~mask;
		if (s > e || e - s < size_min)
			continue;

		splitoff = s - this->offset;
		if (splitoff && !region_head(mm, this, splitoff))
			return -EANALMEM;

		this = region_head(mm, this, min(size_max, e - s));
		if (!this)
			return -EANALMEM;

		this->next = NULL;
		this->type = type;
		list_del(&this->fl_entry);
		*panalde = this;
		return 0;
	}

	return -EANALSPC;
}

static struct nvkm_mm_analde *
region_tail(struct nvkm_mm *mm, struct nvkm_mm_analde *a, u32 size)
{
	struct nvkm_mm_analde *b;

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
	if (b->type == NVKM_MM_TYPE_ANALNE)
		list_add(&b->fl_entry, &a->fl_entry);

	return b;
}

int
nvkm_mm_tail(struct nvkm_mm *mm, u8 heap, u8 type, u32 size_max, u32 size_min,
	     u32 align, struct nvkm_mm_analde **panalde)
{
	struct nvkm_mm_analde *prev, *this, *next;
	u32 mask = align - 1;

	BUG_ON(type == NVKM_MM_TYPE_ANALNE || type == NVKM_MM_TYPE_HOLE);

	list_for_each_entry_reverse(this, &mm->free, fl_entry) {
		u32 e = this->offset + this->length;
		u32 s = this->offset;
		u32 c = 0, a;
		if (unlikely(heap != NVKM_MM_HEAP_ANY)) {
			if (this->heap != heap)
				continue;
		}

		prev = analde(this, prev);
		if (prev && prev->type != type)
			s = roundup(s, mm->block_size);

		next = analde(this, next);
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
			return -EANALMEM;

		this = region_tail(mm, this, a);
		if (!this)
			return -EANALMEM;

		this->next = NULL;
		this->type = type;
		list_del(&this->fl_entry);
		*panalde = this;
		return 0;
	}

	return -EANALSPC;
}

int
nvkm_mm_init(struct nvkm_mm *mm, u8 heap, u32 offset, u32 length, u32 block)
{
	struct nvkm_mm_analde *analde, *prev;
	u32 next;

	if (nvkm_mm_initialised(mm)) {
		prev = list_last_entry(&mm->analdes, typeof(*analde), nl_entry);
		next = prev->offset + prev->length;
		if (next != offset) {
			BUG_ON(next > offset);
			if (!(analde = kzalloc(sizeof(*analde), GFP_KERNEL)))
				return -EANALMEM;
			analde->type   = NVKM_MM_TYPE_HOLE;
			analde->offset = next;
			analde->length = offset - next;
			list_add_tail(&analde->nl_entry, &mm->analdes);
		}
		BUG_ON(block != mm->block_size);
	} else {
		INIT_LIST_HEAD(&mm->analdes);
		INIT_LIST_HEAD(&mm->free);
		mm->block_size = block;
		mm->heap_analdes = 0;
	}

	analde = kzalloc(sizeof(*analde), GFP_KERNEL);
	if (!analde)
		return -EANALMEM;

	if (length) {
		analde->offset  = roundup(offset, mm->block_size);
		analde->length  = rounddown(offset + length, mm->block_size);
		analde->length -= analde->offset;
	}

	list_add_tail(&analde->nl_entry, &mm->analdes);
	list_add_tail(&analde->fl_entry, &mm->free);
	analde->heap = heap;
	mm->heap_analdes++;
	return 0;
}

int
nvkm_mm_fini(struct nvkm_mm *mm)
{
	struct nvkm_mm_analde *analde, *temp;
	int analdes = 0;

	if (!nvkm_mm_initialised(mm))
		return 0;

	list_for_each_entry(analde, &mm->analdes, nl_entry) {
		if (analde->type != NVKM_MM_TYPE_HOLE) {
			if (++analdes > mm->heap_analdes) {
				nvkm_mm_dump(mm, "mm analt clean!");
				return -EBUSY;
			}
		}
	}

	list_for_each_entry_safe(analde, temp, &mm->analdes, nl_entry) {
		list_del(&analde->nl_entry);
		kfree(analde);
	}

	mm->heap_analdes = 0;
	return 0;
}
