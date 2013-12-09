/*
 * lttng_prio_heap.c
 *
 * Priority heap containing pointers. Based on CLRS, chapter 6.
 *
 * Copyright 2011 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/slab.h>
#include "lttng_prio_heap.h"

#ifdef DEBUG_HEAP
void lttng_check_heap(const struct lttng_ptr_heap *heap)
{
	size_t i;

	if (!heap->len)
		return;

	for (i = 1; i < heap->len; i++)
		WARN_ON_ONCE(!heap->gt(heap->ptrs[i], heap->ptrs[0]));
}
#endif

static
size_t parent(size_t i)
{
	return (i -1) >> 1;
}

static
size_t left(size_t i)
{
	return (i << 1) + 1;
}

static
size_t right(size_t i)
{
	return (i << 1) + 2;
}

/*
 * Copy of heap->ptrs pointer is invalid after heap_grow.
 */
static
int heap_grow(struct lttng_ptr_heap *heap, size_t new_len)
{
	void **new_ptrs;

	if (heap->alloc_len >= new_len)
		return 0;

	heap->alloc_len = max_t(size_t, new_len, heap->alloc_len << 1);
	new_ptrs = kmalloc(heap->alloc_len * sizeof(void *), heap->gfpmask);
	if (!new_ptrs)
		return -ENOMEM;
	if (heap->ptrs)
		memcpy(new_ptrs, heap->ptrs, heap->len * sizeof(void *));
	kfree(heap->ptrs);
	heap->ptrs = new_ptrs;
	return 0;
}

static
int heap_set_len(struct lttng_ptr_heap *heap, size_t new_len)
{
	int ret;

	ret = heap_grow(heap, new_len);
	if (ret)
		return ret;
	heap->len = new_len;
	return 0;
}

int lttng_heap_init(struct lttng_ptr_heap *heap, size_t alloc_len,
	      gfp_t gfpmask, int gt(void *a, void *b))
{
	heap->ptrs = NULL;
	heap->len = 0;
	heap->alloc_len = 0;
	heap->gt = gt;
	heap->gfpmask = gfpmask;
	/*
	 * Minimum size allocated is 1 entry to ensure memory allocation
	 * never fails within heap_replace_max.
	 */
	return heap_grow(heap, max_t(size_t, 1, alloc_len));
}

void lttng_heap_free(struct lttng_ptr_heap *heap)
{
	kfree(heap->ptrs);
}

static void heapify(struct lttng_ptr_heap *heap, size_t i)
{
	void **ptrs = heap->ptrs;
	size_t l, r, largest;

	for (;;) {
		void *tmp;

		l = left(i);
		r = right(i);
		if (l < heap->len && heap->gt(ptrs[l], ptrs[i]))
			largest = l;
		else
			largest = i;
		if (r < heap->len && heap->gt(ptrs[r], ptrs[largest]))
			largest = r;
		if (largest == i)
			break;
		tmp = ptrs[i];
		ptrs[i] = ptrs[largest];
		ptrs[largest] = tmp;
		i = largest;
	}
	lttng_check_heap(heap);
}

void *lttng_heap_replace_max(struct lttng_ptr_heap *heap, void *p)
{
	void *res;

	if (!heap->len) {
		(void) heap_set_len(heap, 1);
		heap->ptrs[0] = p;
		lttng_check_heap(heap);
		return NULL;
	}

	/* Replace the current max and heapify */
	res = heap->ptrs[0];
	heap->ptrs[0] = p;
	heapify(heap, 0);
	return res;
}

int lttng_heap_insert(struct lttng_ptr_heap *heap, void *p)
{
	void **ptrs;
	size_t pos;
	int ret;

	ret = heap_set_len(heap, heap->len + 1);
	if (ret)
		return ret;
	ptrs = heap->ptrs;
	pos = heap->len - 1;
	while (pos > 0 && heap->gt(p, ptrs[parent(pos)])) {
		/* Move parent down until we find the right spot */
		ptrs[pos] = ptrs[parent(pos)];
		pos = parent(pos);
	}
	ptrs[pos] = p;
	lttng_check_heap(heap);
	return 0;
}

void *lttng_heap_remove(struct lttng_ptr_heap *heap)
{
	switch (heap->len) {
	case 0:
		return NULL;
	case 1:
		(void) heap_set_len(heap, 0);
		return heap->ptrs[0];
	}
	/* Shrink, replace the current max by previous last entry and heapify */
	heap_set_len(heap, heap->len - 1);
	/* len changed. previous last entry is at heap->len */
	return lttng_heap_replace_max(heap, heap->ptrs[heap->len]);
}

void *lttng_heap_cherrypick(struct lttng_ptr_heap *heap, void *p)
{
	size_t pos, len = heap->len;

	for (pos = 0; pos < len; pos++)
		if (heap->ptrs[pos] == p)
			goto found;
	return NULL;
found:
	if (heap->len == 1) {
		(void) heap_set_len(heap, 0);
		lttng_check_heap(heap);
		return heap->ptrs[0];
	}
	/* Replace p with previous last entry and heapify. */
	heap_set_len(heap, heap->len - 1);
	/* len changed. previous last entry is at heap->len */
	heap->ptrs[pos] = heap->ptrs[heap->len];
	heapify(heap, pos);
	return p;
}
