/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1997-2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: heap.h,v 1.26 2009/01/17 23:47:43 tbox Exp $ */

#ifndef ISC_HEAP_H
#define ISC_HEAP_H 1

/*! \file isc/heap.h */

#include <isc/lang.h>
#include <isc/types.h>

ISC_LANG_BEGINDECLS

/*%
 * The comparison function returns ISC_TRUE if the first argument has
 * higher priority than the second argument, and ISC_FALSE otherwise.
 */
typedef isc_boolean_t (*isc_heapcompare_t)(void *, void *);

/*%
 * The index function allows the client of the heap to receive a callback
 * when an item's index number changes.  This allows it to maintain
 * sync with its external state, but still delete itself, since deletions
 * from the heap require the index be provided.
 */
typedef void (*isc_heapindex_t)(void *, unsigned int);

/*%
 * The heapaction function is used when iterating over the heap.
 *
 * NOTE:  The heap structure CANNOT BE MODIFIED during the call to
 * isc_heap_foreach().
 */
typedef void (*isc_heapaction_t)(void *, void *);

typedef struct isc_heap isc_heap_t;

isc_result_t
isc_heap_create(isc_mem_t *mctx, isc_heapcompare_t compare,
		isc_heapindex_t index, unsigned int size_increment,
		isc_heap_t **heapp);
/*!<
 * \brief Create a new heap.  The heap is implemented using a space-efficient
 * storage method.  When the heap elements are deleted space is not freed
 * but will be reused when new elements are inserted.
 *
 * Requires:
 *\li	"mctx" is valid.
 *\li	"compare" is a function which takes two void * arguments and
 *	returns ISC_TRUE if the first argument has a higher priority than
 *	the second, and ISC_FALSE otherwise.
 *\li	"index" is a function which takes a void *, and an unsigned int
 *	argument.  This function will be called whenever an element's
 *	index value changes, so it may continue to delete itself from the
 *	heap.  This option may be NULL if this functionality is unneeded.
 *\li	"size_increment" is a hint about how large the heap should grow
 *	when resizing is needed.  If this is 0, a default size will be
 *	used, which is currently 1024, allowing space for an additional 1024
 *	heap elements to be inserted before adding more space.
 *\li	"heapp" is not NULL, and "*heap" is NULL.
 *
 * Returns:
 *\li	ISC_R_SUCCESS		- success
 *\li	ISC_R_NOMEMORY		- insufficient memory
 */

void
isc_heap_destroy(isc_heap_t **heapp);
/*!<
 * \brief Destroys a heap.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 */

isc_result_t
isc_heap_insert(isc_heap_t *heap, void *elt);
/*!<
 * \brief Inserts a new element into a heap.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 */

void
isc_heap_delete(isc_heap_t *heap, unsigned int index);
/*!<
 * \brief Deletes an element from a heap, by element index.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 *\li	"index" is a valid element index, as provided by the "index" callback
 *	provided during heap creation.
 */

void
isc_heap_increased(isc_heap_t *heap, unsigned int index);
/*!<
 * \brief Indicates to the heap that an element's priority has increased.
 * This function MUST be called whenever an element has increased in priority.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 *\li	"index" is a valid element index, as provided by the "index" callback
 *	provided during heap creation.
 */

void
isc_heap_decreased(isc_heap_t *heap, unsigned int index);
/*!<
 * \brief Indicates to the heap that an element's priority has decreased.
 * This function MUST be called whenever an element has decreased in priority.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 *\li	"index" is a valid element index, as provided by the "index" callback
 *	provided during heap creation.
 */

void *
isc_heap_element(isc_heap_t *heap, unsigned int index);
/*!<
 * \brief Returns the element for a specific element index.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 *\li	"index" is a valid element index, as provided by the "index" callback
 *	provided during heap creation.
 *
 * Returns:
 *\li	A pointer to the element for the element index.
 */

void
isc_heap_foreach(isc_heap_t *heap, isc_heapaction_t action, void *uap);
/*!<
 * \brief Iterate over the heap, calling an action for each element.  The
 * order of iteration is not sorted.
 *
 * Requires:
 *\li	"heapp" is not NULL and "*heap" points to a valid isc_heap_t.
 *\li	"action" is not NULL, and is a function which takes two arguments.
 *	The first is a void *, representing the element, and the second is
 *	"uap" as provided to isc_heap_foreach.
 *\li	"uap" is a caller-provided argument, and may be NULL.
 *
 * Note:
 *\li	The heap structure CANNOT be modified during this iteration.  The only
 *	safe function to call while iterating the heap is isc_heap_element().
 */

ISC_LANG_ENDDECLS

#endif /* ISC_HEAP_H */
