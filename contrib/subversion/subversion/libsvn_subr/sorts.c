/*
 * sorts.c:   all sorts of sorts
 *
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
 */



#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <stdlib.h>       /* for qsort()   */
#include <assert.h>
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_sorts.h"
#include "svn_error.h"
#include "private/svn_sorts_private.h"



/*** svn_sort__hash() ***/

/* (Should this be a permanent part of APR?)

   OK, folks, here's what's going on.  APR hash tables hash on
   key/klen objects, and store associated generic values.  They work
   great, but they have no ordering.

   The point of this exercise is to somehow arrange a hash's keys into
   an "ordered list" of some kind -- in this case, a nicely sorted
   one.

   We're using APR arrays, therefore, because that's what they are:
   ordered lists.  However, what "keys" should we put in the array?
   Clearly, (const char *) objects aren't general enough.  Or rather,
   they're not as general as APR's hash implementation, which stores
   (void *)/length as keys.  We don't want to lose this information.

   Therefore, it makes sense to store pointers to {void *, size_t}
   structures in our array.  No such apr object exists... BUT... if we
   can use a new type svn_sort__item_t which contains {char *, size_t, void
   *}.  If store these objects in our array, we get the hash value
   *for free*.  When looping over the final array, we don't need to
   call apr_hash_get().  Major bonus!
 */


int
svn_sort_compare_items_as_paths(const svn_sort__item_t *a,
                                const svn_sort__item_t *b)
{
  const char *astr, *bstr;

  astr = a->key;
  bstr = b->key;
  assert(astr[a->klen] == '\0');
  assert(bstr[b->klen] == '\0');
  return svn_path_compare_paths(astr, bstr);
}


int
svn_sort_compare_items_lexically(const svn_sort__item_t *a,
                                 const svn_sort__item_t *b)
{
  int val;
  apr_size_t len;

  /* Compare bytes of a's key and b's key up to the common length. */
  len = (a->klen < b->klen) ? a->klen : b->klen;
  val = memcmp(a->key, b->key, len);
  if (val != 0)
    return val;

  /* They match up until one of them ends; whichever is longer is greater. */
  return (a->klen < b->klen) ? -1 : (a->klen > b->klen) ? 1 : 0;
}


int
svn_sort_compare_revisions(const void *a, const void *b)
{
  svn_revnum_t a_rev = *(const svn_revnum_t *)a;
  svn_revnum_t b_rev = *(const svn_revnum_t *)b;

  if (a_rev == b_rev)
    return 0;

  return a_rev < b_rev ? 1 : -1;
}


int
svn_sort_compare_paths(const void *a, const void *b)
{
  const char *item1 = *((const char * const *) a);
  const char *item2 = *((const char * const *) b);

  return svn_path_compare_paths(item1, item2);
}


int
svn_sort_compare_ranges(const void *a, const void *b)
{
  const svn_merge_range_t *item1 = *((const svn_merge_range_t * const *) a);
  const svn_merge_range_t *item2 = *((const svn_merge_range_t * const *) b);

  if (item1->start == item2->start
      && item1->end == item2->end)
    return 0;

  if (item1->start == item2->start)
    return item1->end < item2->end ? -1 : 1;

  return item1->start < item2->start ? -1 : 1;
}

void
svn_sort__array(apr_array_header_t *array,
                int (*comparison_func)(const void *,
                                       const void *))
{
  qsort(array->elts, array->nelts, array->elt_size, comparison_func);
}

apr_array_header_t *
svn_sort__hash(apr_hash_t *ht,
               int (*comparison_func)(const svn_sort__item_t *,
                                      const svn_sort__item_t *),
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary;
  svn_boolean_t sorted;
  svn_sort__item_t *prev_item;

  /* allocate an array with enough elements to hold all the keys. */
  ary = apr_array_make(pool, apr_hash_count(ht), sizeof(svn_sort__item_t));

  /* loop over hash table and push all keys into the array */
  sorted = TRUE;
  prev_item = NULL;
  for (hi = apr_hash_first(pool, ht); hi; hi = apr_hash_next(hi))
    {
      svn_sort__item_t *item = apr_array_push(ary);

      apr_hash_this(hi, &item->key, &item->klen, &item->value);

      if (prev_item == NULL)
        {
          prev_item = item;
          continue;
        }

      if (sorted)
        {
          sorted = (comparison_func(prev_item, item) < 0);
          prev_item = item;
        }
    }

  /* quicksort the array if it isn't already sorted.  */
  if (!sorted)
    svn_sort__array(ary,
          (int (*)(const void *, const void *))comparison_func);

  return ary;
}

/* Return the lowest index at which the element *KEY should be inserted into
   the array at BASE which has NELTS elements of size ELT_SIZE bytes each,
   according to the ordering defined by COMPARE_FUNC.
   0 <= NELTS <= INT_MAX, 1 <= ELT_SIZE <= INT_MAX.
   The array must already be sorted in the ordering defined by COMPARE_FUNC.
   COMPARE_FUNC is defined as for the C stdlib function bsearch().
   Note: This function is modeled on bsearch() and on lower_bound() in the
   C++ STL.
 */
static int
bsearch_lower_bound(const void *key,
                    const void *base,
                    int nelts,
                    int elt_size,
                    int (*compare_func)(const void *, const void *))
{
  int lower = 0;
  int upper = nelts - 1;

  /* Binary search for the lowest position at which to insert KEY. */
  while (lower <= upper)
    {
      int try = lower + (upper - lower) / 2;  /* careful to avoid overflow */
      int cmp = compare_func((const char *)base + try * elt_size, key);

      if (cmp < 0)
        lower = try + 1;
      else
        upper = try - 1;
    }
  assert(lower == upper + 1);

  return lower;
}

int
svn_sort__bsearch_lower_bound(const apr_array_header_t *array,
                              const void *key,
                              int (*compare_func)(const void *, const void *))
{
  return bsearch_lower_bound(key,
                             array->elts, array->nelts, array->elt_size,
                             compare_func);
}

void *
svn_sort__array_lookup(const apr_array_header_t *array,
                       const void *key,
                       int *hint,
                       int (*compare_func)(const void *, const void *))
{
  void *result;
  int idx;

  /* If provided, try the index following *HINT (i.e. probably the last
   * hit location) first.  This speeds up linear scans. */
  if (hint)
    {
      /* We intend to insert right behind *HINT.
       * Exit this function early, if we actually can. */
      idx = *hint + 1;
      if (idx >= array->nelts)
        {
          /* We intend to insert after the last entry.
           * That is only allowed if that last entry is smaller than KEY.
           * In that case, there will be no current entry, i.e. we must
           * return NULL. */
          apr_size_t offset;

          *hint = array->nelts;
          if (array->nelts == 0)
            return NULL;

          offset = (array->nelts - 1) * array->elt_size;
          if (compare_func(array->elts + offset, key) < 0)
            return NULL;
        }
      else if (idx > 0)
        {
          /* Intend to insert at a position inside the array, i.e. not
           * at one of the boundaries.  The predecessor must be smaller
           * and the current entry at IDX must be larger than KEY. */
          void *previous;

          *hint = idx;
          previous = array->elts + (idx-1) * array->elt_size;
          result = array->elts + idx * array->elt_size;
          if (compare_func(previous, key) && !compare_func(result, key))
            return result;
        }
      else if (idx <= 0)
        {
          /* Intend to insert at the beginning of an non-empty array.
           * That requires the first entry to be larger than KEY. */
          *hint = 0;
          if (!compare_func(array->elts, key))
            return array->elts;
        }

      /* The HINT did not help. */
    }

  idx = bsearch_lower_bound(key, array->elts, array->nelts, array->elt_size,
                            compare_func);
  if (hint)
    *hint = idx;
  if (idx >= array->nelts)
    return NULL;

  result = array->elts + idx * array->elt_size;
  return compare_func(result, key) ? NULL : result;
}

void
svn_sort__array_insert(apr_array_header_t *array,
                       const void *new_element,
                       int insert_index)
{
  int elements_to_move;
  char *new_position;

  assert(0 <= insert_index && insert_index <= array->nelts);
  elements_to_move = array->nelts - insert_index;  /* before bumping nelts */

  /* Grow the array, allocating a new space at the end. Note: this can
     reallocate the array's "elts" at a different address. */
  apr_array_push(array);

  /* Move the elements after INSERT_INDEX along. (When elements_to_move == 0,
     this is a no-op.) */
  new_position = (char *)array->elts + insert_index * array->elt_size;
  memmove(new_position + array->elt_size, new_position,
          array->elt_size * elements_to_move);

  /* Copy in the new element */
  memcpy(new_position, new_element, array->elt_size);
}

void
svn_sort__array_delete(apr_array_header_t *arr,
                       int delete_index,
                       int elements_to_delete)
{
  /* Do we have a valid index and are there enough elements? */
  if (delete_index >= 0
      && delete_index < arr->nelts
      && elements_to_delete > 0
      && (arr->nelts - delete_index) >= elements_to_delete)
    {
      /* If we are not deleting a block of elements that extends to the end
         of the array, then we need to move the remaining elements to keep
         the array contiguous. */
      if ((elements_to_delete + delete_index) < arr->nelts)
        memmove(
          arr->elts + arr->elt_size * delete_index,
          arr->elts + (arr->elt_size * (delete_index + elements_to_delete)),
          arr->elt_size * (arr->nelts - elements_to_delete - delete_index));

      /* Delete the last ELEMENTS_TO_DELETE elements. */
      arr->nelts -= elements_to_delete;
    }
}

void
svn_sort__array_reverse(apr_array_header_t *array,
                        apr_pool_t *scratch_pool)
{
  int i;

  if (array->elt_size == sizeof(void *))
    {
      for (i = 0; i < array->nelts / 2; i++)
        {
          int swap_index = array->nelts - i - 1;
          void *tmp = APR_ARRAY_IDX(array, i, void *);

          APR_ARRAY_IDX(array, i, void *) =
            APR_ARRAY_IDX(array, swap_index, void *);
          APR_ARRAY_IDX(array, swap_index, void *) = tmp;
        }
    }
  else
    {
      apr_size_t sz = array->elt_size;
      char *tmp = apr_palloc(scratch_pool, sz);

      for (i = 0; i < array->nelts / 2; i++)
        {
          int swap_index = array->nelts - i - 1;
          char *x = array->elts + (sz * i);
          char *y = array->elts + (sz * swap_index);

          memcpy(tmp, x, sz);
          memcpy(x, y, sz);
          memcpy(y, tmp, sz);
        }
    }
}

/* Our priority queue data structure:
 * Simply remember the constructor parameters.
 */
struct svn_priority_queue__t
{
  /* the queue elements, ordered as a heap according to COMPARE_FUNC */
  apr_array_header_t *elements;

  /* predicate used to order the heap */
  int (*compare_func)(const void *, const void *);
};

/* Return TRUE, if heap element number LHS in QUEUE is smaller than element
 * number RHS according to QUEUE->COMPARE_FUNC
 */
static int
heap_is_less(svn_priority_queue__t *queue,
             apr_size_t lhs,
             apr_size_t rhs)
{
  char *lhs_value = queue->elements->elts + lhs * queue->elements->elt_size;
  char *rhs_value = queue->elements->elts + rhs * queue->elements->elt_size;

  /* nelts is never negative */
  assert(lhs < (apr_size_t)queue->elements->nelts);
  assert(rhs < (apr_size_t)queue->elements->nelts);
  return queue->compare_func(lhs_value, rhs_value) < 0;
}

/* Exchange elements number LHS and RHS in QUEUE.
 */
static void
heap_swap(svn_priority_queue__t *queue,
          apr_size_t lhs,
          apr_size_t rhs)
{
  int i;
  char *lhs_value = queue->elements->elts + lhs * queue->elements->elt_size;
  char *rhs_value = queue->elements->elts + rhs * queue->elements->elt_size;

  for (i = 0; i < queue->elements->elt_size; ++i)
    {
      char temp = lhs_value[i];
      lhs_value[i] = rhs_value[i];
      rhs_value[i] = temp;
    }
}

/* Move element number IDX to lower indexes until the heap criterion is
 * fulfilled again.
 */
static void
heap_bubble_down(svn_priority_queue__t *queue,
                 int idx)
{
  while (idx > 0 && heap_is_less(queue, idx, (idx - 1) / 2))
    {
      heap_swap(queue, idx, (idx - 1) / 2);
      idx = (idx - 1) / 2;
    }
}

/* Move element number IDX to higher indexes until the heap criterion is
 * fulfilled again.
 */
static void
heap_bubble_up(svn_priority_queue__t *queue,
               int idx)
{
  while (2 * idx + 2 < queue->elements->nelts)
    {
      int child = heap_is_less(queue, 2 * idx + 1, 2 * idx + 2)
                ? 2 * idx + 1
                : 2 * idx + 2;

      if (heap_is_less(queue, idx, child))
        return;

      heap_swap(queue, idx, child);
      idx = child;
    }

  if (   2 * idx + 1 < queue->elements->nelts
      && heap_is_less(queue, 2 * idx + 1, idx))
    heap_swap(queue, 2 * idx + 1, idx);
}

svn_priority_queue__t *
svn_priority_queue__create(apr_array_header_t *elements,
                           int (*compare_func)(const void *, const void *))
{
  int i;

  svn_priority_queue__t *queue = apr_pcalloc(elements->pool, sizeof(*queue));
  queue->elements = elements;
  queue->compare_func = compare_func;

  for (i = elements->nelts / 2; i >= 0; --i)
    heap_bubble_up(queue, i);

  return queue;
}

apr_size_t
svn_priority_queue__size(svn_priority_queue__t *queue)
{
  return queue->elements->nelts;
}

void *
svn_priority_queue__peek(svn_priority_queue__t *queue)
{
  return queue->elements->nelts ? queue->elements->elts : NULL;
}

void
svn_priority_queue__update(svn_priority_queue__t *queue)
{
  heap_bubble_up(queue, 0);
}

void
svn_priority_queue__pop(svn_priority_queue__t *queue)
{
  if (queue->elements->nelts)
    {
      memmove(queue->elements->elts,
              queue->elements->elts
              + (queue->elements->nelts - 1) * queue->elements->elt_size,
              queue->elements->elt_size);
      --queue->elements->nelts;
      heap_bubble_up(queue, 0);
    }
}

void
svn_priority_queue__push(svn_priority_queue__t *queue,
                         const void *element)
{
  /* we cannot duplicate elements due to potential array re-allocs */
  assert(element && element != queue->elements->elts);

  memcpy(apr_array_push(queue->elements), element, queue->elements->elt_size);
  heap_bubble_down(queue, queue->elements->nelts - 1);
}
