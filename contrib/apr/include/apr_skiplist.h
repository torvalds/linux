/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APR_SKIPLIST_H
#define APR_SKIPLIST_H
/**
 * @file apr_skiplist.h
 * @brief APR skip list implementation
 */

#include "apr.h"
#include "apr_portable.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup apr_skiplist Skip list implementation
 * Refer to http://en.wikipedia.org/wiki/Skip_list for information
 * about the purpose of and ideas behind skip lists.
 * @ingroup APR
 * @{
 */

/**
 * apr_skiplist_compare is the function type that must be implemented 
 * per object type that is used in a skip list for comparisons to maintain
 * order. A value <0 indicates placement after this node; a value of 0
 * indicates collision with this exact node; a value >0 indicates placement
 * before this node.
 * */
typedef int (*apr_skiplist_compare) (void *, void *);

/**
 * apr_skiplist_freefunc is the function type that must be implemented
 * to handle elements as they are removed from a skip list.
 */
typedef void (*apr_skiplist_freefunc) (void *);

/** Opaque structure used to represent the skip list */
struct apr_skiplist;
/** Opaque structure used to represent the skip list */
typedef struct apr_skiplist apr_skiplist;

/** 
 * Opaque structure used to represent abstract nodes in the skip list
 * (an abstraction above the raw elements which are collected in the
 * skip list).
 */
struct apr_skiplistnode;
/** Opaque structure */
typedef struct apr_skiplistnode apr_skiplistnode;

/**
 * Allocate memory using the same mechanism as the skip list.
 * @param sl The skip list
 * @param size The amount to allocate
 * @remark If a pool was provided to apr_skiplist_init(), memory will
 * be allocated from the pool or from a free list maintained with
 * the skip list.  Otherwise, memory will be allocated using the
 * C standard library heap functions.
 */
APR_DECLARE(void *) apr_skiplist_alloc(apr_skiplist *sl, size_t size);

/**
 * Free memory using the same mechanism as the skip list.
 * @param sl The skip list
 * @param mem The object to free
 * @remark If a pool was provided to apr_skiplist_init(), memory will
 * be added to a free list maintained with the skip list and be available
 * to operations on the skip list or to other calls to apr_skiplist_alloc().
 * Otherwise, memory will be freed using the  C standard library heap
 * functions.
 */
APR_DECLARE(void) apr_skiplist_free(apr_skiplist *sl, void *mem);

/**
 * Allocate a new skip list
 * @param sl The pointer in which to return the newly created skip list
 * @param p The pool from which to allocate the skip list (optional).
 * @remark Unlike most APR functions, a pool is optional.  If no pool
 * is provided, the C standard library heap functions will be used instead.
 */
APR_DECLARE(apr_status_t) apr_skiplist_init(apr_skiplist **sl, apr_pool_t *p);

/**
 * Set the comparison functions to be used for searching the skip list.
 * @param sl The skip list
 * @param XXX1 FIXME
 * @param XXX2 FIXME
 *
 * @remark If existing comparison functions are being replaced, the index
 * will be replaced during this call.  That is a potentially expensive
 * operation.
 */
APR_DECLARE(void) apr_skiplist_set_compare(apr_skiplist *sl, apr_skiplist_compare XXX1,
                             apr_skiplist_compare XXX2);

/**
 * Set the indexing functions to the specified comparison functions and
 * rebuild the index.
 * @param sl The skip list
 * @param XXX1 FIXME
 * @param XXX2 FIXME
 *
 * @remark If an index already exists, it will not be replaced and the
 * comparison functions will not be changed.
 */
APR_DECLARE(void) apr_skiplist_add_index(apr_skiplist *sl, apr_skiplist_compare XXX1,
                        apr_skiplist_compare XXX2);

/**
 * Return the list maintained by the skip list abstraction.
 * @param sl The skip list
 */
APR_DECLARE(apr_skiplistnode *) apr_skiplist_getlist(apr_skiplist *sl);

/**
 * Return the next matching element in the skip list using the specified
 * comparison function.
 * @param sl The skip list
 * @param data The value to search for
 * @param iter A pointer to the returned skip list node representing the element
 * found
 * @param func The comparison function to use
 */
APR_DECLARE(void *) apr_skiplist_find_compare(apr_skiplist *sl,
                               void *data,
                               apr_skiplistnode **iter,
                               apr_skiplist_compare func);

/**
 * Return the next matching element in the skip list using the current comparison
 * function.
 * @param sl The skip list
 * @param data The value to search for
 * @param iter A pointer to the returned skip list node representing the element
 * found
 */
APR_DECLARE(void *) apr_skiplist_find(apr_skiplist *sl, void *data, apr_skiplistnode **iter);

/**
 * Return the next element in the skip list.
 * @param sl The skip list
 * @param iter On entry, a pointer to the skip list node to start with; on return,
 * a pointer to the skip list node representing the element returned
 * @remark If iter points to a NULL value on entry, NULL will be returned.
 */
APR_DECLARE(void *) apr_skiplist_next(apr_skiplist *sl, apr_skiplistnode **iter);

/**
 * Return the previous element in the skip list.
 * @param sl The skip list
 * @param iter On entry, a pointer to the skip list node to start with; on return,
 * a pointer to the skip list node representing the element returned
 * @remark If iter points to a NULL value on entry, NULL will be returned.
 */
APR_DECLARE(void *) apr_skiplist_previous(apr_skiplist *sl, apr_skiplistnode **iter);

/**
 * Insert an element into the skip list using the specified comparison function
 * if it does not already exist.
 * @param sl The skip list
 * @param data The element to insert
 * @param comp The comparison function to use for placement into the skip list
 */
APR_DECLARE(apr_skiplistnode *) apr_skiplist_insert_compare(apr_skiplist *sl,
                                          void *data, apr_skiplist_compare comp);

/**
 * Insert an element into the skip list using the existing comparison function
 * if it does not already exist (as determined by the comparison function)
 * @param sl The skip list
 * @param data The element to insert
 * @remark If no comparison function has been set for the skip list, the element
 * will not be inserted and NULL will be returned.
 */
APR_DECLARE(apr_skiplistnode *) apr_skiplist_insert(apr_skiplist* sl, void *data);

/**
 * Remove an element from the skip list using the specified comparison function for
 * locating the element. In the case of duplicates, the 1st entry will be removed.
 * @param sl The skip list
 * @param data The element to remove
 * @param myfree A function to be called for each removed element
 * @param comp The comparison function to use for placement into the skip list
 * @remark If the element is not found, 0 will be returned.  Otherwise, the heightXXX
 * will be returned.
 */
APR_DECLARE(int) apr_skiplist_remove_compare(apr_skiplist *sl, void *data,
                               apr_skiplist_freefunc myfree, apr_skiplist_compare comp);

/**
 * Remove an element from the skip list using the existing comparison function for
 * locating the element. In the case of duplicates, the 1st entry will be removed.
 * @param sl The skip list
 * @param data The element to remove
 * @param myfree A function to be called for each removed element
 * @remark If the element is not found, 0 will be returned.  Otherwise, the heightXXX
 * will be returned.
 * @remark If no comparison function has been set for the skip list, the element
 * will not be removed and 0 will be returned.
 */
APR_DECLARE(int) apr_skiplist_remove(apr_skiplist *sl, void *data, apr_skiplist_freefunc myfree);

/**
 * Remove all elements from the skip list.
 * @param sl The skip list
 * @param myfree A function to be called for each removed element
 */
APR_DECLARE(void) apr_skiplist_remove_all(apr_skiplist *sl, apr_skiplist_freefunc myfree);

/**
 * Remove each element from the skip list.
 * @param sl The skip list
 * @param myfree A function to be called for each removed element
 */
APR_DECLARE(void) apr_skiplist_destroy(apr_skiplist *sl, apr_skiplist_freefunc myfree);

/**
 * Return the first element in the skip list, removing the element from the skip list.
 * @param sl The skip list
 * @param myfree A function to be called for the removed element
 * @remark NULL will be returned if there are no elements
 */
APR_DECLARE(void *) apr_skiplist_pop(apr_skiplist *sl, apr_skiplist_freefunc myfree);

/**
 * Return the first element in the skip list, leaving the element in the skip list.
 * @param sl The skip list
 * @remark NULL will be returned if there are no elements
 */
APR_DECLARE(void *) apr_skiplist_peek(apr_skiplist *sl);

/**
 * Merge two skip lists.  XXX SEMANTICS
 * @param sl1 One of two skip lists to be merged
 * @param sl2 The other of two skip lists to be merged
 */
APR_DECLARE(apr_skiplist *) apr_skiplist_merge(apr_skiplist *sl1, apr_skiplist *sl2);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ! APR_SKIPLIST_H */
