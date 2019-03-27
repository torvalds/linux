/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svn_sorts_private.h
 * @brief all sorts of sorts.
 */


#ifndef SVN_SORTS_PRIVATE_H
#define SVN_SORTS_PRIVATE_H

#include "../svn_sorts.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** This structure is used to hold a key/value from a hash table.
 * @note Private. For use by Subversion's own code only. See issue #1644.
 */
struct svn_sort__item_t {
  /** pointer to the key */
  const void *key;

  /** size of the key */
  apr_ssize_t klen;

  /** pointer to the value */
  void *value;
};

/** Sort @a ht according to its keys, return an @c apr_array_header_t
 * containing @c svn_sort__item_t structures holding those keys and values
 * (i.e. for each @c svn_sort__item_t @a item in the returned array,
 * @a item.key and @a item.size are the hash key, and @a item.value points to
 * the hash value).
 *
 * Storage is shared with the original hash, not copied.
 *
 * @a comparison_func should take pointers to two items and return an
 * integer greater than, equal to, or less than 0, according as the first item
 * is greater than, equal to, or less than the second.
 *
 * @note Private. For use by Subversion's own code only. See issue #1644.
 *
 * @note This function and the @c svn_sort__item_t should go over to APR.
 */
apr_array_header_t *
svn_sort__hash(apr_hash_t *ht,
               int (*comparison_func)(const svn_sort__item_t *,
                                      const svn_sort__item_t *),
               apr_pool_t *pool);

/* Sort APR array @a array using ordering defined by @a comparison_func.
 * @a comparison_func is defined as for the C stdlib function qsort().
 */
void
svn_sort__array(apr_array_header_t *array,
                int (*comparison_func)(const void *,
                                       const void *));

/* Return the lowest index at which the element @a *key should be inserted into
 * the array @a array, according to the ordering defined by @a compare_func.
 * The array must already be sorted in the ordering defined by @a compare_func.
 * @a compare_func is defined as for the C stdlib function bsearch(); the
 * @a key will always passed to it as the second parameter.
 *
 * @note Private. For use by Subversion's own code only.
 */
int
svn_sort__bsearch_lower_bound(const apr_array_header_t *array,
                              const void *key,
                              int (*compare_func)(const void *, const void *));

/* Find the lowest index at which the element @a *key should be inserted into
 * the array @a array, according to the ordering defined by @a compare_func.
 * The array must already be sorted in the ordering defined by @a compare_func.
 * @a compare_func is defined as for the C stdlib function bsearch(); the
 * @a key will always passed to it as the second parameter.
 *
 * Returns a reference to the array element at the insertion location if
 * that matches @a key and return NULL otherwise.  If you call this function
 * multiple times for the same array and expect the results to often be
 * consecutive array elements, provide @a hint.  It should be initialized
 * with -1 for the first call and receives the array index if the returned
 * element.  If the return value is NULL, @a *hint is the location where
 * the respective key would be inserted.
 *
 * @note Private. For use by Subversion's own code only.
 */
void *
svn_sort__array_lookup(const apr_array_header_t *array,
                       const void *key,
                       int *hint,
                       int (*compare_func)(const void *, const void *));


/* Insert a shallow copy of @a *new_element into the array @a array at the index
 * @a insert_index, growing the array and shuffling existing elements along to
 * make room.
 *
 * @note Private. For use by Subversion's own code only.
 */
void
svn_sort__array_insert(apr_array_header_t *array,
                       const void *new_element,
                       int insert_index);


/* Remove @a elements_to_delete elements starting at @a delete_index from the
 * array @a arr. If @a delete_index is not a valid element of @a arr,
 * @a elements_to_delete is not greater than zero, or
 * @a delete_index + @a elements_to_delete is greater than @a arr->nelts,
 * then do nothing.
 *
 * @note Private. For use by Subversion's own code only.
 */
void
svn_sort__array_delete(apr_array_header_t *arr,
                       int delete_index,
                       int elements_to_delete);

/* Reverse the order of elements in @a array, in place.
 *
 * @note Private. For use by Subversion's own code only.
 */
void
svn_sort__array_reverse(apr_array_header_t *array,
                        apr_pool_t *scratch_pool);

/** Priority queues.
 *
 * @defgroup svn_priority_queue__t Priority Queues
 * @{
 */

/**
 * We implement priority queues on top of existing ELEMENTS arrays.  They
 * provide us with memory management and very basic element type information.
 *
 * The extraction order is being defined by a comparison function similar
 * to the ones used with qsort.  The first element in the queue is always
 * on with COMPARISON_FUNC(first,element) <= 0, for all elements in the
 * queue.
 */

/**
 * Opaque data type for priority queues.
 */
typedef struct svn_priority_queue__t svn_priority_queue__t;

/**
 * Return a priority queue containing all provided @a elements and prioritize
 * them according to @a compare_func.
 *
 * @note The priority queue will use the existing @a elements array for data
 * storage.  So, you must not manipulate that array while using the queue.
 * Also, the lifetime of the queue is bound to that of the array.
 */
svn_priority_queue__t *
svn_priority_queue__create(apr_array_header_t *elements,
                           int (*compare_func)(const void *, const void *));

/**
 * Returns the number of elements in the @a queue.
 */
apr_size_t
svn_priority_queue__size(svn_priority_queue__t *queue);

/**
 * Returns a reference to the first element in the @a queue.  The queue
 * contents remains unchanged.  If the @a queue is empty, #NULL will be
 * returned.
 */
void *
svn_priority_queue__peek(svn_priority_queue__t *queue);

/**
 * Notify the @a queue after modifying the first item as returned by
 * #svn_priority_queue__peek.
 */
void
svn_priority_queue__update(svn_priority_queue__t *queue);

/**
 * Remove the first element from the @a queue.  This is a no-op for empty
 * queues.
 */
void
svn_priority_queue__pop(svn_priority_queue__t *queue);

/**
 * Append the new @a element to the @a queue.  @a element must neither be
 * #NULL nor the first element as returned by #svn_priority_queue__peek.
 */
void
svn_priority_queue__push(svn_priority_queue__t *queue, const void *element);

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SORTS_PRIVATE_H */
