#ifndef _LTTNG_PRIO_HEAP_H
#define _LTTNG_PRIO_HEAP_H

/*
 * lttng_prio_heap.h
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
 */

#include <linux/gfp.h>

struct lttng_ptr_heap {
	size_t len, alloc_len;
	void **ptrs;
	int (*gt)(void *a, void *b);
	gfp_t gfpmask;
};

#ifdef DEBUG_HEAP
void lttng_check_heap(const struct lttng_ptr_heap *heap);
#else
static inline
void lttng_check_heap(const struct lttng_ptr_heap *heap)
{
}
#endif

/**
 * lttng_heap_maximum - return the largest element in the heap
 * @heap: the heap to be operated on
 *
 * Returns the largest element in the heap, without performing any modification
 * to the heap structure. Returns NULL if the heap is empty.
 */
static inline void *lttng_heap_maximum(const struct lttng_ptr_heap *heap)
{
	lttng_check_heap(heap);
	return heap->len ? heap->ptrs[0] : NULL;
}

/**
 * lttng_heap_init - initialize the heap
 * @heap: the heap to initialize
 * @alloc_len: number of elements initially allocated
 * @gfp: allocation flags
 * @gt: function to compare the elements
 *
 * Returns -ENOMEM if out of memory.
 */
extern int lttng_heap_init(struct lttng_ptr_heap *heap,
		     size_t alloc_len, gfp_t gfpmask,
		     int gt(void *a, void *b));

/**
 * lttng_heap_free - free the heap
 * @heap: the heap to free
 */
extern void lttng_heap_free(struct lttng_ptr_heap *heap);

/**
 * lttng_heap_insert - insert an element into the heap
 * @heap: the heap to be operated on
 * @p: the element to add
 *
 * Insert an element into the heap.
 *
 * Returns -ENOMEM if out of memory.
 */
extern int lttng_heap_insert(struct lttng_ptr_heap *heap, void *p);

/**
 * lttng_heap_remove - remove the largest element from the heap
 * @heap: the heap to be operated on
 *
 * Returns the largest element in the heap. It removes this element from the
 * heap. Returns NULL if the heap is empty.
 */
extern void *lttng_heap_remove(struct lttng_ptr_heap *heap);

/**
 * lttng_heap_cherrypick - remove a given element from the heap
 * @heap: the heap to be operated on
 * @p: the element
 *
 * Remove the given element from the heap. Return the element if present, else
 * return NULL. This algorithm has a complexity of O(n), which is higher than
 * O(log(n)) provided by the rest of this API.
 */
extern void *lttng_heap_cherrypick(struct lttng_ptr_heap *heap, void *p);

/**
 * lttng_heap_replace_max - replace the the largest element from the heap
 * @heap: the heap to be operated on
 * @p: the pointer to be inserted as topmost element replacement
 *
 * Returns the largest element in the heap. It removes this element from the
 * heap. The heap is rebalanced only once after the insertion. Returns NULL if
 * the heap is empty.
 *
 * This is the equivalent of calling heap_remove() and then heap_insert(), but
 * it only rebalances the heap once. It never allocates memory.
 */
extern void *lttng_heap_replace_max(struct lttng_ptr_heap *heap, void *p);

#endif /* _LTTNG_PRIO_HEAP_H */
