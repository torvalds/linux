/*
 * cache-inprocess.c: in-memory caching for Subversion
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

#include <assert.h>

#include "svn_pools.h"

#include "svn_private_config.h"

#include "cache.h"
#include "private/svn_mutex.h"

/* The (internal) cache object. */
typedef struct inprocess_cache_t {
  /* A user-defined identifier for this cache instance. */
  const char *id;

  /* HASH maps a key (of size KLEN) to a struct cache_entry. */
  apr_hash_t *hash;
  apr_ssize_t klen;

  /* Used to copy values into the cache. */
  svn_cache__serialize_func_t serialize_func;

  /* Used to copy values out of the cache. */
  svn_cache__deserialize_func_t deserialize_func;

  /* Maximum number of pages that this cache instance may allocate */
  apr_uint64_t total_pages;
  /* The number of pages we're allowed to allocate before having to
   * try to reuse one. */
  apr_uint64_t unallocated_pages;
  /* Number of cache entries stored on each page.  Must be at least 1. */
  apr_uint64_t items_per_page;

  /* A dummy cache_page serving as the head of a circular doubly
   * linked list of cache_pages.  SENTINEL->NEXT is the most recently
   * used page, and SENTINEL->PREV is the least recently used page.
   * All pages in this list are "full"; the page currently being
   * filled (PARTIAL_PAGE) is not in the list. */
  struct cache_page *sentinel;

  /* A page currently being filled with entries, or NULL if there's no
   * partially-filled page.  This page is not in SENTINEL's list. */
  struct cache_page *partial_page;
  /* If PARTIAL_PAGE is not null, this is the number of entries
   * currently on PARTIAL_PAGE. */
  apr_uint64_t partial_page_number_filled;

  /* The pool that the svn_cache__t itself, HASH, and all pages are
   * allocated in; subpools of this pool are used for the cache_entry
   * structs, as well as the dup'd values and hash keys.
   */
  apr_pool_t *cache_pool;

  /* Sum of the SIZE members of all cache_entry elements that are
   * accessible from HASH. This is used to make statistics available
   * even if the sub-pools have already been destroyed.
   */
  apr_size_t data_size;

  /* A lock for intra-process synchronization to the cache, or NULL if
   * the cache's creator doesn't feel the cache needs to be
   * thread-safe. */
  svn_mutex__t *mutex;
} inprocess_cache_t;

/* A cache page; all items on the page are allocated from the same
 * pool. */
struct cache_page {
  /* Pointers for the LRU list anchored at the cache's SENTINEL.
   * (NULL for the PARTIAL_PAGE.) */
  struct cache_page *prev;
  struct cache_page *next;

  /* The pool in which cache_entry structs, hash keys, and dup'd
   * values are allocated.  The CACHE_PAGE structs are allocated
   * in CACHE_POOL and have the same lifetime as the cache itself.
   * (The cache will never allocate more than TOTAL_PAGES page
   * structs (inclusive of the sentinel) from CACHE_POOL.)
   */
  apr_pool_t *page_pool;

  /* A singly linked list of the entries on this page; used to remove
   * them from the cache's HASH before reusing the page. */
  struct cache_entry *first_entry;
};

/* An cache entry. */
struct cache_entry {
  const void *key;

  /* serialized value */
  void *value;

  /* length of the serialized value in bytes */
  apr_size_t size;

  /* The page it's on (needed so that the LRU list can be
   * maintained). */
  struct cache_page *page;

  /* Next entry on the page. */
  struct cache_entry *next_entry;
};


/* Removes PAGE from the doubly-linked list it is in (leaving its PREV
 * and NEXT fields undefined). */
static void
remove_page_from_list(struct cache_page *page)
{
  page->prev->next = page->next;
  page->next->prev = page->prev;
}

/* Inserts PAGE after CACHE's sentinel. */
static void
insert_page(inprocess_cache_t *cache,
            struct cache_page *page)
{
  struct cache_page *pred = cache->sentinel;

  page->prev = pred;
  page->next = pred->next;
  page->prev->next = page;
  page->next->prev = page;
}

/* If PAGE is in the circularly linked list (eg, its NEXT isn't NULL),
 * move it to the front of the list. */
static svn_error_t *
move_page_to_front(inprocess_cache_t *cache,
                   struct cache_page *page)
{
  /* This function is called whilst CACHE is locked. */

  SVN_ERR_ASSERT(page != cache->sentinel);

  if (! page->next)
    return SVN_NO_ERROR;

  remove_page_from_list(page);
  insert_page(cache, page);

  return SVN_NO_ERROR;
}

/* Return a copy of KEY inside POOL, using CACHE->KLEN to figure out
 * how. */
static const void *
duplicate_key(inprocess_cache_t *cache,
              const void *key,
              apr_pool_t *pool)
{
  if (cache->klen == APR_HASH_KEY_STRING)
    return apr_pstrdup(pool, key);
  else
    return apr_pmemdup(pool, key, cache->klen);
}

static svn_error_t *
inprocess_cache_get_internal(char **buffer,
                             apr_size_t *size,
                             inprocess_cache_t *cache,
                             const void *key,
                             apr_pool_t *result_pool)
{
  struct cache_entry *entry = apr_hash_get(cache->hash, key, cache->klen);

  if (entry)
    {
      SVN_ERR(move_page_to_front(cache, entry->page));

      /* duplicate the buffer entry */
      *buffer = apr_palloc(result_pool, entry->size);
      if (entry->size)
        memcpy(*buffer, entry->value, entry->size);

      *size = entry->size;
    }
  else
    {
      *buffer = NULL;
      *size = 0;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_get(void **value_p,
                    svn_boolean_t *found,
                    void *cache_void,
                    const void *key,
                    apr_pool_t *result_pool)
{
  inprocess_cache_t *cache = cache_void;

  if (key)
    {
      char* buffer;
      apr_size_t size;

      SVN_MUTEX__WITH_LOCK(cache->mutex,
                           inprocess_cache_get_internal(&buffer,
                                                        &size,
                                                        cache,
                                                        key,
                                                        result_pool));
      /* deserialize the buffer content. Usually, this will directly
         modify the buffer content directly. */
      *found = (buffer != NULL);
      if (!buffer || !size)
        *value_p = NULL;
      else
        return cache->deserialize_func(value_p, buffer, size, result_pool);
    }
  else
    {
      *value_p = NULL;
      *found = FALSE;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_has_key_internal(svn_boolean_t *found,
                                 inprocess_cache_t *cache,
                                 const void *key,
                                 apr_pool_t *scratch_pool)
{
  *found = apr_hash_get(cache->hash, key, cache->klen) != NULL;

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_has_key(svn_boolean_t *found,
                        void *cache_void,
                        const void *key,
                        apr_pool_t *scratch_pool)
{
  inprocess_cache_t *cache = cache_void;

  if (key)
    SVN_MUTEX__WITH_LOCK(cache->mutex,
                         inprocess_cache_has_key_internal(found,
                                                          cache,
                                                          key,
                                                          scratch_pool));
  else
    *found = FALSE;

  return SVN_NO_ERROR;
}

/* Removes PAGE from the LRU list, removes all of its entries from
 * CACHE's hash, clears its pool, and sets its entry pointer to NULL.
 * Finally, puts it in the "partial page" slot in the cache and sets
 * partial_page_number_filled to 0.  Must be called on a page actually
 * in the list. */
static void
erase_page(inprocess_cache_t *cache,
           struct cache_page *page)
{
  struct cache_entry *e;

  remove_page_from_list(page);

  for (e = page->first_entry;
       e;
       e = e->next_entry)
    {
      cache->data_size -= e->size;
      apr_hash_set(cache->hash, e->key, cache->klen, NULL);
    }

  svn_pool_clear(page->page_pool);

  page->first_entry = NULL;
  page->prev = NULL;
  page->next = NULL;

  cache->partial_page = page;
  cache->partial_page_number_filled = 0;
}


static svn_error_t *
inprocess_cache_set_internal(inprocess_cache_t *cache,
                             const void *key,
                             void *value,
                             apr_pool_t *scratch_pool)
{
  struct cache_entry *existing_entry;

  existing_entry = apr_hash_get(cache->hash, key, cache->klen);

  /* Is it already here, but we can do the one-item-per-page
   * optimization? */
  if (existing_entry && cache->items_per_page == 1)
    {
      /* Special case!  ENTRY is the *only* entry on this page, so
       * why not wipe it (so as not to leak the previous value).
       */
      struct cache_page *page = existing_entry->page;

      /* This can't be the partial page: items_per_page == 1
       * *never* has a partial page (except for in the temporary state
       * that we're about to fake). */
      SVN_ERR_ASSERT(page->next != NULL);
      SVN_ERR_ASSERT(cache->partial_page == NULL);

      erase_page(cache, page);
      existing_entry = NULL;
    }

  /* Is it already here, and we just have to leak the old value? */
  if (existing_entry)
    {
      struct cache_page *page = existing_entry->page;

      SVN_ERR(move_page_to_front(cache, page));
      cache->data_size -= existing_entry->size;
      if (value)
        {
          SVN_ERR(cache->serialize_func(&existing_entry->value,
                                        &existing_entry->size,
                                        value,
                                        page->page_pool));
          cache->data_size += existing_entry->size;
          if (existing_entry->size == 0)
            existing_entry->value = NULL;
        }
      else
        {
          existing_entry->value = NULL;
          existing_entry->size = 0;
        }

      return SVN_NO_ERROR;
    }

  /* Do we not have a partial page to put it on, but we are allowed to
   * allocate more? */
  if (cache->partial_page == NULL && cache->unallocated_pages > 0)
    {
      cache->partial_page = apr_pcalloc(cache->cache_pool,
                                        sizeof(*(cache->partial_page)));
      cache->partial_page->page_pool = svn_pool_create(cache->cache_pool);
      cache->partial_page_number_filled = 0;
      (cache->unallocated_pages)--;
    }

  /* Do we really not have a partial page to put it on, even after the
   * one-item-per-page optimization and checking the unallocated page
   * count? */
  if (cache->partial_page == NULL)
    {
      struct cache_page *oldest_page = cache->sentinel->prev;

      SVN_ERR_ASSERT(oldest_page != cache->sentinel);

      /* Erase the page and put it in cache->partial_page. */
      erase_page(cache, oldest_page);
    }

  SVN_ERR_ASSERT(cache->partial_page != NULL);

  {
    struct cache_page *page = cache->partial_page;
    struct cache_entry *new_entry = apr_pcalloc(page->page_pool,
                                                sizeof(*new_entry));

    /* Copy the key and value into the page's pool.  */
    new_entry->key = duplicate_key(cache, key, page->page_pool);
    if (value)
      {
        SVN_ERR(cache->serialize_func(&new_entry->value,
                                      &new_entry->size,
                                      value,
                                      page->page_pool));
        cache->data_size += new_entry->size;
        if (new_entry->size == 0)
          new_entry->value = NULL;
      }
    else
      {
        new_entry->value = NULL;
        new_entry->size = 0;
      }

    /* Add the entry to the page's list. */
    new_entry->page = page;
    new_entry->next_entry = page->first_entry;
    page->first_entry = new_entry;

    /* Add the entry to the hash, using the *entry's* copy of the
     * key. */
    apr_hash_set(cache->hash, new_entry->key, cache->klen, new_entry);

    /* We've added something else to the partial page. */
    (cache->partial_page_number_filled)++;

    /* Is it full? */
    if (cache->partial_page_number_filled >= cache->items_per_page)
      {
        insert_page(cache, page);
        cache->partial_page = NULL;
      }
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_set(void *cache_void,
                    const void *key,
                    void *value,
                    apr_pool_t *scratch_pool)
{
  inprocess_cache_t *cache = cache_void;

  if (key)
    SVN_MUTEX__WITH_LOCK(cache->mutex,
                         inprocess_cache_set_internal(cache,
                                                      key,
                                                      value,
                                                      scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton type for svn_cache__iter. */
struct cache_iter_baton {
  svn_iter_apr_hash_cb_t user_cb;
  void *user_baton;
};

/* Call the user's callback with the actual value, not the
   cache_entry.  Implements the svn_iter_apr_hash_cb_t
   prototype. */
static svn_error_t *
iter_cb(void *baton,
        const void *key,
        apr_ssize_t klen,
        void *val,
        apr_pool_t *pool)
{
  struct cache_iter_baton *b = baton;
  struct cache_entry *entry = val;
  return (b->user_cb)(b->user_baton, key, klen, entry->value, pool);
}

static svn_error_t *
inprocess_cache_iter(svn_boolean_t *completed,
                     void *cache_void,
                     svn_iter_apr_hash_cb_t user_cb,
                     void *user_baton,
                     apr_pool_t *scratch_pool)
{
  inprocess_cache_t *cache = cache_void;
  struct cache_iter_baton b;
  b.user_cb = user_cb;
  b.user_baton = user_baton;

  SVN_MUTEX__WITH_LOCK(cache->mutex,
                       svn_iter_apr_hash(completed, cache->hash,
                                         iter_cb, &b, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_get_partial_internal(void **value_p,
                                     svn_boolean_t *found,
                                     inprocess_cache_t *cache,
                                     const void *key,
                                     svn_cache__partial_getter_func_t func,
                                     void *baton,
                                     apr_pool_t *result_pool)
{
  struct cache_entry *entry = apr_hash_get(cache->hash, key, cache->klen);
  if (! entry)
    {
      *found = FALSE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(move_page_to_front(cache, entry->page));

  *found = TRUE;
  return func(value_p, entry->value, entry->size, baton, result_pool);
}

static svn_error_t *
inprocess_cache_get_partial(void **value_p,
                            svn_boolean_t *found,
                            void *cache_void,
                            const void *key,
                            svn_cache__partial_getter_func_t func,
                            void *baton,
                            apr_pool_t *result_pool)
{
  inprocess_cache_t *cache = cache_void;

  if (key)
    SVN_MUTEX__WITH_LOCK(cache->mutex,
                         inprocess_cache_get_partial_internal(value_p,
                                                              found,
                                                              cache,
                                                              key,
                                                              func,
                                                              baton,
                                                              result_pool));
  else
    *found = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_set_partial_internal(inprocess_cache_t *cache,
                                     const void *key,
                                     svn_cache__partial_setter_func_t func,
                                     void *baton,
                                     apr_pool_t *scratch_pool)
{
  struct cache_entry *entry = apr_hash_get(cache->hash, key, cache->klen);
  if (entry)
    {
      SVN_ERR(move_page_to_front(cache, entry->page));

      cache->data_size -= entry->size;
      SVN_ERR(func(&entry->value,
                   &entry->size,
                   baton,
                   entry->page->page_pool));
      cache->data_size += entry->size;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
inprocess_cache_set_partial(void *cache_void,
                            const void *key,
                            svn_cache__partial_setter_func_t func,
                            void *baton,
                            apr_pool_t *scratch_pool)
{
  inprocess_cache_t *cache = cache_void;

  if (key)
    SVN_MUTEX__WITH_LOCK(cache->mutex,
                         inprocess_cache_set_partial_internal(cache,
                                                              key,
                                                              func,
                                                              baton,
                                                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_boolean_t
inprocess_cache_is_cachable(void *cache_void, apr_size_t size)
{
  /* Be relatively strict: per page we should not allocate more than
   * we could spare as "unused" memory.
   * But in most cases, nobody will ask anyway. And in no case, this
   * will be used for checks _inside_ the cache code.
   */
  inprocess_cache_t *cache = cache_void;
  return size < SVN_ALLOCATOR_RECOMMENDED_MAX_FREE / cache->items_per_page;
}

static svn_error_t *
inprocess_cache_get_info_internal(inprocess_cache_t *cache,
                                  svn_cache__info_t *info,
                                  apr_pool_t *result_pool)
{
  info->id = apr_pstrdup(result_pool, cache->id);

  info->used_entries = apr_hash_count(cache->hash);
  info->total_entries = cache->items_per_page * cache->total_pages;

  info->used_size = cache->data_size;
  info->data_size = cache->data_size;
  info->total_size = cache->data_size
                   + cache->items_per_page * sizeof(struct cache_page)
                   + info->used_entries * sizeof(struct cache_entry);

  return SVN_NO_ERROR;
}


static svn_error_t *
inprocess_cache_get_info(void *cache_void,
                         svn_cache__info_t *info,
                         svn_boolean_t reset,
                         apr_pool_t *result_pool)
{
  inprocess_cache_t *cache = cache_void;

  SVN_MUTEX__WITH_LOCK(cache->mutex,
                       inprocess_cache_get_info_internal(cache,
                                                         info,
                                                         result_pool));

  return SVN_NO_ERROR;
}

static svn_cache__vtable_t inprocess_cache_vtable = {
  inprocess_cache_get,
  inprocess_cache_has_key,
  inprocess_cache_set,
  inprocess_cache_iter,
  inprocess_cache_is_cachable,
  inprocess_cache_get_partial,
  inprocess_cache_set_partial,
  inprocess_cache_get_info
};

svn_error_t *
svn_cache__create_inprocess(svn_cache__t **cache_p,
                            svn_cache__serialize_func_t serialize,
                            svn_cache__deserialize_func_t deserialize,
                            apr_ssize_t klen,
                            apr_int64_t pages,
                            apr_int64_t items_per_page,
                            svn_boolean_t thread_safe,
                            const char *id,
                            apr_pool_t *pool)
{
  svn_cache__t *wrapper = apr_pcalloc(pool, sizeof(*wrapper));
  inprocess_cache_t *cache = apr_pcalloc(pool, sizeof(*cache));

  cache->id = apr_pstrdup(pool, id);

  SVN_ERR_ASSERT(klen == APR_HASH_KEY_STRING || klen >= 1);

  cache->hash = apr_hash_make(pool);
  cache->klen = klen;

  cache->serialize_func = serialize;
  cache->deserialize_func = deserialize;

  SVN_ERR_ASSERT(pages >= 1);
  cache->total_pages = pages;
  cache->unallocated_pages = pages;
  SVN_ERR_ASSERT(items_per_page >= 1);
  cache->items_per_page = items_per_page;

  cache->sentinel = apr_pcalloc(pool, sizeof(*(cache->sentinel)));
  cache->sentinel->prev = cache->sentinel;
  cache->sentinel->next = cache->sentinel;
  /* The sentinel doesn't need a pool.  (We're happy to crash if we
   * accidentally try to treat it like a real page.) */

  SVN_ERR(svn_mutex__init(&cache->mutex, thread_safe, pool));

  cache->cache_pool = pool;

  wrapper->vtable = &inprocess_cache_vtable;
  wrapper->cache_internal = cache;
  wrapper->pretend_empty = !!getenv("SVN_X_DOES_NOT_MARK_THE_SPOT");

  *cache_p = wrapper;
  return SVN_NO_ERROR;
}
