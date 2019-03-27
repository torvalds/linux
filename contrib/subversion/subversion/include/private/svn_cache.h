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
 * @file svn_cache.h
 * @brief In-memory cache implementation.
 */


#ifndef SVN_CACHE_H
#define SVN_CACHE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_iter.h"
#include "svn_config.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * @defgroup svn_cache__support In-memory caching
 * @{
 */

/**
 * A function type for deserializing an object @a *out from the string
 * @a data of length @a data_len into @a result_pool. It is legal and
 * generally suggested that the deserialization will be done in-place,
 * i.e. modify @a data directly and return it in @a *out.
 */
typedef svn_error_t *(*svn_cache__deserialize_func_t)(void **out,
                                                      void *data,
                                                      apr_size_t data_len,
                                                      apr_pool_t *result_pool);

/**
 * A function type for deserializing an object @a *out from the string
 * @a data of length @a data_len into @a result_pool. The extra information
 * @a baton passed into can be used to deserialize only a specific part or
 * sub-structure or to perform any other non-modifying operation that may
 * not require the whole structure to be processed.
 */
typedef svn_error_t *(*svn_cache__partial_getter_func_t)(void **out,
                                                         const void *data,
                                                         apr_size_t data_len,
                                                         void *baton,
                                                         apr_pool_t *result_pool);

/**
 * A function type for modifying an already deserialized in the @a *data
 * buffer of length @a *data_len. Additional information of the modification
 * to do will be provided in @a baton. The function may change the size of
 * data buffer and may re-allocate it if necessary. In that case, the new
 * values must be passed back in @a *data_len and @a *data, respectively.
 * Allocations will be done from @a result_pool.
 */
typedef svn_error_t *(*svn_cache__partial_setter_func_t)(void **data,
                                                         apr_size_t *data_len,
                                                         void *baton,
                                                         apr_pool_t *result_pool);

/**
 * A function type for serializing an object @a in into bytes.  The
 * function should allocate the serialized value in @a result_pool, set
 * @a *data to the serialized value, and set @a *data_len to its length.
 */
typedef svn_error_t *(*svn_cache__serialize_func_t)(void **data,
                                                    apr_size_t *data_len,
                                                    void *in,
                                                    apr_pool_t *result_pool);

/**
 * A function type for transforming or ignoring errors.  @a scratch_pool may
 * be used for temporary allocations.
 */
typedef svn_error_t *(*svn_cache__error_handler_t)(svn_error_t *err,
                                                   void *baton,
                                                   apr_pool_t *scratch_pool);

/**
 * A wrapper around apr_memcache_t, provided essentially so that the
 * Subversion public API doesn't depend on whether or not you have
 * access to the APR memcache libraries.
 */
typedef struct svn_memcache_t svn_memcache_t;

/**
 * An opaque structure representing a membuffer cache object.
 */
typedef struct svn_membuffer_t svn_membuffer_t;

/**
 * Opaque type for an in-memory cache.
 */
typedef struct svn_cache__t svn_cache__t;

/**
 * A structure containing typical statistics about a given cache instance.
 * Use svn_cache__get_info() to get this data. Note that not all types
 * of caches will be able to report complete and correct information.
 */
typedef struct svn_cache__info_t
{
  /** A string identifying the cache instance. Usually a copy of the @a id
   * or @a prefix parameter passed to the cache constructor.
   */
  const char* id;

  /** Number of getter calls (svn_cache__get() or svn_cache__get()).
   */
  apr_uint64_t gets;

  /** Number of getter calls that return data.
   */
  apr_uint64_t hits;

  /** Number of setter calls (svn_cache__set()).
   */
  apr_uint64_t sets;

  /** Number of function calls that returned an error.
   */
  apr_uint64_t failures;

  /** Size of the data currently stored in the cache.
   * May be 0 if that information is not available.
   */
  apr_uint64_t used_size;

  /** Amount of memory currently reserved for cached data.
   * Will be equal to @a used_size if no precise information is available.
   */
  apr_uint64_t data_size;

  /** Lower threshold of the total size of memory allocated to the cache and
   * its index as well as management structures. The actual memory allocated
   * by the cache may be larger.
   */
  apr_uint64_t total_size;

  /** Number of cache entries.
   * May be 0 if that information is not available.
   */
  apr_uint64_t used_entries;

  /** Maximum numbers of cache entries.
   * May be 0 if that information is not available.
   */
  apr_uint64_t total_entries;

  /** Number of index buckets with the given number of entries.
   * Bucket sizes larger than the array will saturate into the
   * highest array index.
   */
  apr_uint64_t histogram[32];
} svn_cache__info_t;

/**
 * Creates a new cache in @a *cache_p.  This cache will use @a pool
 * for all of its storage needs.  The elements in the cache will be
 * indexed by keys of length @a klen, which may be APR_HASH_KEY_STRING
 * if they are strings.  Cached values will be copied in and out of
 * the cache using @a serialize_func and @a deserialize_func, respectively.
 *
 * If @a deserialize_func is NULL, then the data is returned as an
 * svn_stringbuf_t; if @a serialize_func is NULL, then the data is
 * assumed to be an svn_stringbuf_t.
 *
 * The cache stores up to @a pages * @a items_per_page items at a
 * time.  The exact cache invalidation strategy is not defined here,
 * but in general, a lower value for @a items_per_page means more
 * memory overhead for the same number of items, but a higher value
 * for @a items_per_page means more items are cleared at once.  Both
 * @a pages and @a items_per_page must be positive (though they both
 * may certainly be 1).
 *
 * If @a thread_safe is true, and APR is compiled with threads, all
 * accesses to the cache will be protected with a mutex. The @a id
 * is a purely user-visible information that will allow coders to
 * identify this cache instance in a #svn_cache__info_t struct.
 * It does not influence the behavior of the cache itself.
 *
 * Note that NULL is a legitimate value for cache entries (and
 * @a serialize_func will not be called on it).
 *
 * It is not safe for @a serialize_func nor @a deserialize_func to
 * interact with the cache itself.
 */
svn_error_t *
svn_cache__create_inprocess(svn_cache__t **cache_p,
                            svn_cache__serialize_func_t serialize_func,
                            svn_cache__deserialize_func_t deserialize_func,
                            apr_ssize_t klen,
                            apr_int64_t pages,
                            apr_int64_t items_per_page,
                            svn_boolean_t thread_safe,
                            const char *id,
                            apr_pool_t *pool);

/**
 * Creates a new cache in @a *cache_p, communicating to a memcached
 * process via @a memcache.  The elements in the cache will be indexed
 * by keys of length @a klen, which may be APR_HASH_KEY_STRING if they
 * are strings.  Values will be serialized for memcached using @a
 * serialize_func and deserialized using @a deserialize_func.  Because
 * the same memcached server may cache many different kinds of values,
 * @a prefix should be specified to differentiate this cache from
 * other caches.  @a *cache_p will be allocated in @a result_pool.
 *
 * If @a deserialize_func is NULL, then the data is returned as an
 * svn_stringbuf_t; if @a serialize_func is NULL, then the data is
 * assumed to be an svn_stringbuf_t.
 *
 * These caches are always thread safe.
 *
 * These caches do not support svn_cache__iter.
 *
 * If Subversion was not built with apr_memcache support, always
 * raises SVN_ERR_NO_APR_MEMCACHE.
 */
svn_error_t *
svn_cache__create_memcache(svn_cache__t **cache_p,
                           svn_memcache_t *memcache,
                           svn_cache__serialize_func_t serialize_func,
                           svn_cache__deserialize_func_t deserialize_func,
                           apr_ssize_t klen,
                           const char *prefix,
                           apr_pool_t *result_pool);

/**
 * Given @a config, returns an APR memcached interface in @a
 * *memcache_p allocated in @a result_pool if @a config contains entries in
 * the SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS section describing
 * memcached servers; otherwise, sets @a *memcache_p to NULL.  Use
 * @a scratch_pool for temporary allocations.
 *
 * If Subversion was not built with apr_memcache_support, then raises
 * SVN_ERR_NO_APR_MEMCACHE if and only if @a config is configured to
 * use memcache.
 */
svn_error_t *
svn_cache__make_memcache_from_config(svn_memcache_t **memcache_p,
                                     svn_config_t *config,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool);

/**
 * Creates a new membuffer cache object in @a *cache. It will contain
 * up to @a total_size bytes of data, using @a directory_size bytes
 * for index information and the remainder for serialized objects.
 *
 * Since each index entry is about 50 bytes long, 1 to 10 percent of
 * the @a total_size should be allocated to the @a directory_size,
 * depending on the average serialized object size. Higher percentages
 * will generally result in higher hit rates and reduced conflict
 * resolution overhead.
 *
 * The cache will be split into @a segment_count segments of equal size.
 * A higher number reduces lock contention but also limits the maximum
 * cachable item size.  If it is not a power of two, it will be rounded
 * down to next lower power of two. Also, there is an implementation
 * specific upper limit and the setting will be capped there automatically.
 * If the number is 0, a default will be derived from @a total_size.
 *
 * If access to the resulting cache object is guaranteed to be serialized,
 * @a thread_safe may be set to @c FALSE for maximum performance.
 *
 * There is no limit on the number of threads reading a given cache segment
 * concurrently.  Writes, however, need an exclusive lock on the respective
 * segment.  @a allow_blocking_writes controls contention is handled here.
 * If set to TRUE, writes will wait until the lock becomes available, i.e.
 * reads should be short.  If set to FALSE, write attempts will be ignored
 * (no data being written to the cache) if some reader or another writer
 * currently holds the segment lock.
 *
 * Allocations will be made in @a result_pool, in particular the data buffers.
 */
svn_error_t *
svn_cache__membuffer_cache_create(svn_membuffer_t **cache,
                                  apr_size_t total_size,
                                  apr_size_t directory_size,
                                  apr_size_t segment_count,
                                  svn_boolean_t thread_safe,
                                  svn_boolean_t allow_blocking_writes,
                                  apr_pool_t *result_pool);

/**
 * @defgroup Standard priority classes for #svn_cache__create_membuffer_cache.
 * @{
 */

/**
 * Data in this priority class should not be removed from the cache unless
 * absolutely necessary.  Use of this should be very restricted.
 */
#define SVN_CACHE__MEMBUFFER_HIGH_PRIORITY   10000

/**
 * Data in this priority class has a good chance to remain in cache unless
 * there is more data in this class than the cache's capacity.  Use of this
 * as the default for all information that is costly to fetch from disk.
 */
#define SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY 1000

/**
 * Data in this priority class will be removed as soon as the cache starts
 * filling up.  Use of this for ephemeral data that can easily be acquired
 * again from other sources.
 */
#define SVN_CACHE__MEMBUFFER_LOW_PRIORITY      100

/** @} */

/**
 * Creates a new cache in @a *cache_p, storing the data in a potentially
 * shared @a membuffer object.  The elements in the cache will be indexed
 * by keys of length @a klen, which may be APR_HASH_KEY_STRING if they
 * are strings.  Values will be serialized for the memcache using @a
 * serialize_func and deserialized using @a deserialize_func.  Because
 * the same memcache object may cache many different kinds of values
 * form multiple caches, @a prefix should be specified to differentiate
 * this cache from other caches.  All entries written through this cache
 * interface will be assigned into the given @a priority class.  @a *cache_p
 * will be allocated in @a result_pool.  @a scratch_pool is used for
 * temporary allocations.
 *
 * If @a deserialize_func is NULL, then the data is returned as an
 * svn_stringbuf_t; if @a serialize_func is NULL, then the data is
 * assumed to be an svn_stringbuf_t.
 *
 * If @a thread_safe is true, and APR is compiled with threads, all
 * accesses to the cache will be protected with a mutex, if the shared
 * @a membuffer has also been created with thread_safe flag set.
 *
 * If @a short_lived is set, assume that the data stored through this
 * cache will probably only be needed for a short period of time.
 * Typically, some UUID is used as part of the prefix in that scenario.
 * This flag is a mere hint and does not affect functionality.
 *
 * These caches do not support svn_cache__iter.
 */
svn_error_t *
svn_cache__create_membuffer_cache(svn_cache__t **cache_p,
                                  svn_membuffer_t *membuffer,
                                  svn_cache__serialize_func_t serialize,
                                  svn_cache__deserialize_func_t deserialize,
                                  apr_ssize_t klen,
                                  const char *prefix,
                                  apr_uint32_t priority,
                                  svn_boolean_t thread_safe,
                                  svn_boolean_t short_lived,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/**
 * Creates a null-cache instance in @a *cache_p, allocated from
 * @a result_pool.  The given @c id is the only data stored in it and can
 * be retrieved through svn_cache__get_info().
 *
 * The cache object will immediately evict (reject) any data being added
 * to it and will always report as empty.
 */
svn_error_t *
svn_cache__create_null(svn_cache__t **cache_p,
                       const char *id,
                       apr_pool_t *result_pool);

/**
 * Sets @a handler to be @a cache's error handling routine.  If any
 * error is returned from a call to svn_cache__get or svn_cache__set, @a
 * handler will be called with @a baton and the error, and the
 * original function will return whatever error @a handler returns
 * instead (possibly SVN_NO_ERROR); @a handler will receive the pool
 * passed to the svn_cache_* function.  @a scratch_pool is used for temporary
 * allocations.
 */
svn_error_t *
svn_cache__set_error_handler(svn_cache__t *cache,
                             svn_cache__error_handler_t handler,
                             void *baton,
                             apr_pool_t *scratch_pool);

/**
 * Returns @c TRUE if the @a cache supports objects of the given @a size.
 * There is no guarantee, that svn_cache__set() will actually store the
 * respective object in that case. However, a @c FALSE return value indicates
 * that an attempt to cache the item will either fail or impair the overall
 * cache performance. @c FALSE will also be returned if @a cache is @c NULL.
 */
svn_boolean_t
svn_cache__is_cachable(svn_cache__t *cache,
                       apr_size_t size);

#define SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS "memcached-servers"

/**
 * Fetches a value indexed by @a key from @a cache into @a *value,
 * setting @a *found to TRUE iff it is in the cache and FALSE if it is
 * not found.  @a key may be NULL in which case @a *found will be
 * FALSE.  The value is copied into @a result_pool using the deserialize
 * function provided to the cache's constructor.
 */
svn_error_t *
svn_cache__get(void **value,
               svn_boolean_t *found,
               svn_cache__t *cache,
               const void *key,
               apr_pool_t *result_pool);

/**
 * Looks for an entry indexed by @a key in @a cache,  setting @a *found
 * to TRUE if an entry has been found and FALSE otherwise.  @a key may be
 * NULL in which case @a *found will be FALSE.  Temporary allocations will
 * be made from @a scratch_pool.
 */
svn_error_t *
svn_cache__has_key(svn_boolean_t *found,
                   svn_cache__t *cache,
                   const void *key,
                   apr_pool_t *scratch_pool);

/**
 * Stores the value @a value under the key @a key in @a cache.  Uses @a
 * scratch_pool for temporary allocations.  The cache makes copies of
 * @a key and @a value if necessary (that is, @a key and @a value may
 * have shorter lifetimes than the cache).  @a key may be NULL in which
 * case the cache will remain unchanged.
 *
 * If there is already a value for @a key, this will replace it.  Bear
 * in mind that in some circumstances this may leak memory (that is,
 * the cache's copy of the previous value may not be immediately
 * cleared); it is only guaranteed to not leak for caches created with
 * @a items_per_page equal to 1.
 */
svn_error_t *
svn_cache__set(svn_cache__t *cache,
               const void *key,
               void *value,
               apr_pool_t *scratch_pool);

/**
 * Iterates over the elements currently in @a cache, calling @a func
 * for each one until there are no more elements or @a func returns an
 * error.  Uses @a scratch_pool for temporary allocations.
 *
 * If @a completed is not NULL, then on return - if @a func returns no
 * errors - @a *completed will be set to @c TRUE.
 *
 * If @a func returns an error other than @c SVN_ERR_ITER_BREAK, that
 * error is returned.  When @a func returns @c SVN_ERR_ITER_BREAK,
 * iteration is interrupted, but no error is returned and @a
 * *completed is set to @c FALSE.  (The error handler set by
 * svn_cache__set_error_handler is not used for svn_cache__iter.)
 *
 * It is not legal to perform any other cache operations on @a cache
 * inside @a func.
 *
 * svn_cache__iter is not supported by all cache implementations; see
 * the svn_cache__create_* function for details.
 */
svn_error_t *
svn_cache__iter(svn_boolean_t *completed,
                svn_cache__t *cache,
                svn_iter_apr_hash_cb_t func,
                void *baton,
                apr_pool_t *scratch_pool);

/**
 * Similar to svn_cache__get() but will call a specific de-serialization
 * function @a func. @a found will be set depending on whether the @a key
 * has been found. Even if that reports @c TRUE, @a value may still return
 * a @c NULL pointer depending on the logic inside @a func.  For a @a NULL
 * @a key, no data will be found.  @a value will be allocated in
 * @a result_pool.
 */
svn_error_t *
svn_cache__get_partial(void **value,
                       svn_boolean_t *found,
                       svn_cache__t *cache,
                       const void *key,
                       svn_cache__partial_getter_func_t func,
                       void *baton,
                       apr_pool_t *result_pool);

/**
 * Find the item identified by @a key in the @a cache. If it has been found,
 * call @a func for it and @a baton to potentially modify the data. Changed
 * data will be written back to the cache. If the item cannot be found,
 * or if @a key is NULL, @a func does not get called. @a scratch_pool is
 * used for temporary allocations.
 */
svn_error_t *
svn_cache__set_partial(svn_cache__t *cache,
                       const void *key,
                       svn_cache__partial_setter_func_t func,
                       void *baton,
                       apr_pool_t *scratch_pool);

/**
 * Collect all available usage statistics on the cache instance @a cache
 * and write the data into @a info. If @a reset has been set, access
 * counters will be reset right after copying the statistics info.
 * @a result_pool will be used for allocations.
 */
svn_error_t *
svn_cache__get_info(svn_cache__t *cache,
                    svn_cache__info_t *info,
                    svn_boolean_t reset,
                    apr_pool_t *result_pool);

/**
 * Return the information given in @a info formatted as a multi-line string.
 * If @a access_only has been set, size and fill-level statistics will be
 * omitted.  Allocations take place in @a result_pool.
 */
svn_string_t *
svn_cache__format_info(const svn_cache__info_t *info,
                       svn_boolean_t access_only,
                       apr_pool_t *result_pool);

/**
 * Access the process-global (singleton) membuffer cache. The first call
 * will automatically allocate the cache using the current cache config.
 * NULL will be returned if the desired cache size is 0.
 *
 * @since New in 1.7.
 */
struct svn_membuffer_t *
svn_cache__get_global_membuffer_cache(void);

/**
 * Return total access and size stats over all membuffer caches as they
 * share the underlying data buffer.  The result will be allocated in POOL.
 */
svn_cache__info_t *
svn_cache__membuffer_get_global_info(apr_pool_t *pool);

/**
 * Remove all current contents from CACHE.
 *
 * NOTE:  In a multi-threaded environment, new contents may have been put
 * into the cache by the time this function returns.
 */
svn_error_t *
svn_cache__membuffer_clear(svn_membuffer_t *cache);

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CACHE_H */
