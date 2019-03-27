/* caching.c : in-memory caching
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

#include "fs.h"
#include "fs_fs.h"
#include "id.h"
#include "dag.h"
#include "tree.h"
#include "index.h"
#include "temp_serializer.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_config.h"
#include "svn_cache_config.h"

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"

#include "private/svn_debug.h"
#include "private/svn_subr_private.h"

/* Take the ORIGINAL string and replace all occurrences of ":" without
 * limiting the key space.  Allocate the result in POOL.
 */
static const char *
normalize_key_part(const char *original,
                   apr_pool_t *pool)
{
  apr_size_t i;
  apr_size_t len = strlen(original);
  svn_stringbuf_t *normalized = svn_stringbuf_create_ensure(len, pool);

  for (i = 0; i < len; ++i)
    {
      char c = original[i];
      switch (c)
        {
        case ':': svn_stringbuf_appendbytes(normalized, "%_", 2);
                  break;
        case '%': svn_stringbuf_appendbytes(normalized, "%%", 2);
                  break;
        default : svn_stringbuf_appendbyte(normalized, c);
        }
    }

  return normalized->data;
}

/* *CACHE_TXDELTAS, *CACHE_FULLTEXTS, *CACHE_NODEPROPS flags will be set
   according to FS->CONFIG. *CACHE_NAMESPACE receives the cache prefix to
   use.

   Use FS->pool for allocating the memcache and CACHE_NAMESPACE, and POOL
   for temporary allocations. */
static svn_error_t *
read_config(const char **cache_namespace,
            svn_boolean_t *cache_txdeltas,
            svn_boolean_t *cache_fulltexts,
            svn_boolean_t *cache_nodeprops,
            svn_fs_t *fs,
            apr_pool_t *pool)
{
  /* No cache namespace by default.  I.e. all FS instances share the
   * cached data.  If you specify different namespaces, the data will
   * share / compete for the same cache memory but keys will not match
   * across namespaces and, thus, cached data will not be shared between
   * namespaces.
   *
   * Since the namespace will be concatenated with other elements to form
   * the complete key prefix, we must make sure that the resulting string
   * is unique and cannot be created by any other combination of elements.
   */
  *cache_namespace
    = normalize_key_part(svn_hash__get_cstring(fs->config,
                                               SVN_FS_CONFIG_FSFS_CACHE_NS,
                                               ""),
                         pool);

  /* Cache text deltas by default.
   * They tend to be smaller and have finer granularity than fulltexts.
   */
  *cache_txdeltas
    = svn_hash__get_bool(fs->config,
                         SVN_FS_CONFIG_FSFS_CACHE_DELTAS,
                         TRUE);

  /* by default, cache fulltexts.
   * Most SVN tools care about reconstructed file content.
   * Thus, this is a reasonable default.
   * SVN admin tools may set that to FALSE because fulltexts
   * won't be re-used rendering the cache less effective
   * by squeezing wanted data out.
   */
  *cache_fulltexts
    = svn_hash__get_bool(fs->config,
                         SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS,
                         TRUE);

  /* by default, cache nodeprops.
   * Pre-1.10, this was controlled by the SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS
   * configuration option which defaulted to TRUE.
   */
  *cache_nodeprops
    = svn_hash__get_bool(fs->config,
                         SVN_FS_CONFIG_FSFS_CACHE_NODEPROPS,
                         TRUE);
  return SVN_NO_ERROR;
}


/* Implements svn_cache__error_handler_t
 * This variant clears the error after logging it.
 */
static svn_error_t *
warn_and_continue_on_cache_errors(svn_error_t *err,
                                  void *baton,
                                  apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  (fs->warning)(fs->warning_baton, err);
  svn_error_clear(err);

  return SVN_NO_ERROR;
}

/* Implements svn_cache__error_handler_t
 * This variant logs the error and passes it on to the callers.
 */
static svn_error_t *
warn_and_fail_on_cache_errors(svn_error_t *err,
                              void *baton,
                              apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  (fs->warning)(fs->warning_baton, err);
  return err;
}

#ifdef SVN_DEBUG_CACHE_DUMP_STATS
/* Baton to be used for the dump_cache_statistics() pool cleanup function, */
struct dump_cache_baton_t
{
  /* the pool about to be cleaned up. Will be used for temp. allocations. */
  apr_pool_t *pool;

  /* the cache to dump the statistics for */
  svn_cache__t *cache;
};

/* APR pool cleanup handler that will printf the statistics of the
   cache referenced by the baton in BATON_VOID. */
static apr_status_t
dump_cache_statistics(void *baton_void)
{
  struct dump_cache_baton_t *baton = baton_void;

  apr_status_t result = APR_SUCCESS;
  svn_cache__info_t info;
  svn_string_t *text_stats;
  apr_array_header_t *lines;
  int i;

  svn_error_t *err = svn_cache__get_info(baton->cache,
                                         &info,
                                         TRUE,
                                         baton->pool);

  /* skip unused caches */
  if (! err && (info.gets > 0 || info.sets > 0))
    {
      text_stats = svn_cache__format_info(&info, TRUE, baton->pool);
      lines = svn_cstring_split(text_stats->data, "\n", FALSE, baton->pool);

      for (i = 0; i < lines->nelts; ++i)
        {
          const char *line = APR_ARRAY_IDX(lines, i, const char *);
#ifdef SVN_DEBUG
          SVN_DBG(("%s\n", line));
#endif
        }
    }

  /* process error returns */
  if (err)
    {
      result = err->apr_err;
      svn_error_clear(err);
    }

  return result;
}

static apr_status_t
dump_global_cache_statistics(void *baton_void)
{
  apr_pool_t *pool = baton_void;

  svn_cache__info_t *info = svn_cache__membuffer_get_global_info(pool);
  svn_string_t *text_stats = svn_cache__format_info(info, FALSE, pool);
  apr_array_header_t *lines = svn_cstring_split(text_stats->data, "\n",
                                                FALSE, pool);

  int i;
  for (i = 0; i < lines->nelts; ++i)
    {
      const char *line = APR_ARRAY_IDX(lines, i, const char *);
#ifdef SVN_DEBUG
      SVN_DBG(("%s\n", line));
#endif
    }

  return APR_SUCCESS;
}

#endif /* SVN_DEBUG_CACHE_DUMP_STATS */

/* This function sets / registers the required callbacks for a given
 * not transaction-specific CACHE object in FS, if CACHE is not NULL.
 *
 * All these svn_cache__t instances shall be handled uniformly. Unless
 * ERROR_HANDLER is NULL, register it for the given CACHE in FS.
 */
static svn_error_t *
init_callbacks(svn_cache__t *cache,
               svn_fs_t *fs,
               svn_cache__error_handler_t error_handler,
               apr_pool_t *pool)
{
  if (cache != NULL)
    {
#ifdef SVN_DEBUG_CACHE_DUMP_STATS

      /* schedule printing the access statistics upon pool cleanup,
       * i.e. end of FSFS session.
       */
      struct dump_cache_baton_t *baton;

      baton = apr_palloc(pool, sizeof(*baton));
      baton->pool = pool;
      baton->cache = cache;

      apr_pool_cleanup_register(pool,
                                baton,
                                dump_cache_statistics,
                                apr_pool_cleanup_null);
#endif

      if (error_handler)
        SVN_ERR(svn_cache__set_error_handler(cache,
                                             error_handler,
                                             fs,
                                             pool));

    }

  return SVN_NO_ERROR;
}

/* Sets *CACHE_P to cache instance based on provided options.
 * Creates memcache if MEMCACHE is not NULL. Creates membuffer cache if
 * MEMBUFFER is not NULL. Fallbacks to inprocess cache if MEMCACHE and
 * MEMBUFFER are NULL and pages is non-zero.  Sets *CACHE_P to NULL
 * otherwise.  Use the given PRIORITY class for the new cache.  If it
 * is 0, then use the default priority class.  HAS_NAMESPACE indicates
 * whether we prefixed this cache instance with a namespace.
 *
 * Unless NO_HANDLER is true, register an error handler that reports errors
 * as warnings to the FS warning callback.
 *
 * Cache is allocated in RESULT_POOL, temporaries in SCRATCH_POOL.
 * */
static svn_error_t *
create_cache(svn_cache__t **cache_p,
             svn_memcache_t *memcache,
             svn_membuffer_t *membuffer,
             apr_int64_t pages,
             apr_int64_t items_per_page,
             svn_cache__serialize_func_t serializer,
             svn_cache__deserialize_func_t deserializer,
             apr_ssize_t klen,
             const char *prefix,
             apr_uint32_t priority,
             svn_boolean_t has_namespace,
             svn_fs_t *fs,
             svn_boolean_t no_handler,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_cache__error_handler_t error_handler = no_handler
                                           ? NULL
                                           : warn_and_fail_on_cache_errors;
  if (priority == 0)
    priority = SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY;

  if (memcache)
    {
      SVN_ERR(svn_cache__create_memcache(cache_p, memcache,
                                         serializer, deserializer, klen,
                                         prefix, result_pool));
      error_handler = no_handler
                    ? NULL
                    : warn_and_continue_on_cache_errors;
    }
  else if (membuffer)
    {
      /* We assume caches with namespaces to be relatively short-lived,
       * i.e. their data will not be needed after a while. */
      SVN_ERR(svn_cache__create_membuffer_cache(
                cache_p, membuffer, serializer, deserializer,
                klen, prefix, priority, FALSE, has_namespace,
                result_pool, scratch_pool));
    }
  else if (pages)
    {
      SVN_ERR(svn_cache__create_inprocess(
                cache_p, serializer, deserializer, klen, pages,
                items_per_page, FALSE, prefix, result_pool));
    }
  else
    {
      *cache_p = NULL;
    }

  SVN_ERR(init_callbacks(*cache_p, fs, error_handler, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__initialize_caches(svn_fs_t *fs,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *prefix = apr_pstrcat(pool,
                                   "fsfs:", fs->uuid,
                                   "/", normalize_key_part(fs->path, pool),
                                   ":",
                                   SVN_VA_NULL);
  svn_membuffer_t *membuffer;
  svn_boolean_t no_handler = ffd->fail_stop;
  svn_boolean_t cache_txdeltas;
  svn_boolean_t cache_fulltexts;
  svn_boolean_t cache_nodeprops;
  const char *cache_namespace;
  svn_boolean_t has_namespace;

  /* Evaluating the cache configuration. */
  SVN_ERR(read_config(&cache_namespace,
                      &cache_txdeltas,
                      &cache_fulltexts,
                      &cache_nodeprops,
                      fs,
                      pool));

  prefix = apr_pstrcat(pool, "ns:", cache_namespace, ":", prefix, SVN_VA_NULL);
  has_namespace = strlen(cache_namespace) > 0;

  membuffer = svn_cache__get_global_membuffer_cache();

  /* General rules for assigning cache priorities:
   *
   * - Data that can be reconstructed from other elements has low prio
   *   (e.g. fulltexts etc.)
   * - Index data required to find any of the other data has high prio
   *   (e.g. noderevs, L2P and P2L index pages)
   * - everything else should use default prio
   */

#ifdef SVN_DEBUG_CACHE_DUMP_STATS

  /* schedule printing the global access statistics upon pool cleanup,
   * i.e. when the repo instance gets closed / cleaned up.
   */
  if (membuffer)
    apr_pool_cleanup_register(fs->pool,
                              fs->pool,
                              dump_global_cache_statistics,
                              apr_pool_cleanup_null);
#endif

  /* Make the cache for revision roots.  For the vast majority of
   * commands, this is only going to contain a few entries (svnadmin
   * dump/verify is an exception here), so to reduce overhead let's
   * try to keep it to just one page.  I estimate each entry has about
   * 130 bytes of overhead (svn_revnum_t key, ID struct, and the cache_entry);
   * the default pool size is 8192, so about a fifty should fit comfortably.
   */
  SVN_ERR(create_cache(&(ffd->rev_root_id_cache),
                       NULL,
                       membuffer,
                       1, 50,
                       svn_fs_fs__serialize_id,
                       svn_fs_fs__deserialize_id,
                       sizeof(svn_revnum_t),
                       apr_pstrcat(pool, prefix, "RRI", SVN_VA_NULL),
                       0,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* Rough estimate: revision DAG nodes have size around 1kBytes, so
   * let's put 8 on a page. */
  SVN_ERR(create_cache(&(ffd->rev_node_cache),
                       NULL,
                       membuffer,
                       1, 8,
                       svn_fs_fs__dag_serialize,
                       svn_fs_fs__dag_deserialize,
                       APR_HASH_KEY_STRING,
                       apr_pstrcat(pool, prefix, "DAG", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* 1st level DAG node cache */
  ffd->dag_node_cache = svn_fs_fs__create_dag_cache(fs->pool);

  /* Very rough estimate: 1K per directory. */
  SVN_ERR(create_cache(&(ffd->dir_cache),
                       NULL,
                       membuffer,
                       1, 8,
                       svn_fs_fs__serialize_dir_entries,
                       svn_fs_fs__deserialize_dir_entries,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "DIR", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* 8 kBytes per entry (1000 revs / shared, one file offset per rev).
     Covering about 8 pack files gives us an "o.k." hit rate. */
  SVN_ERR(create_cache(&(ffd->packed_offset_cache),
                       NULL,
                       membuffer,
                       8, 1,
                       svn_fs_fs__serialize_manifest,
                       svn_fs_fs__deserialize_manifest,
                       sizeof(svn_revnum_t),
                       apr_pstrcat(pool, prefix, "PACK-MANIFEST",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* initialize node revision cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->node_revision_cache),
                       NULL,
                       membuffer,
                       2, 16, /* ~500 byte / entry; 32 entries total */
                       svn_fs_fs__serialize_node_revision,
                       svn_fs_fs__deserialize_node_revision,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "NODEREVS", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* initialize representation header cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->rep_header_cache),
                       NULL,
                       membuffer,
                       1, 200, /* ~40 bytes / entry; 200 entries total */
                       svn_fs_fs__serialize_rep_header,
                       svn_fs_fs__deserialize_rep_header,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "REPHEADER", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* initialize node change list cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->changes_cache),
                       NULL,
                       membuffer,
                       1, 8, /* 1k / entry; 8 entries total, rarely used */
                       svn_fs_fs__serialize_changes,
                       svn_fs_fs__deserialize_changes,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "CHANGES", SVN_VA_NULL),
                       0,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* if enabled, cache revprops */
  SVN_ERR(create_cache(&(ffd->revprop_cache),
                       NULL,
                       membuffer,
                       8, 20, /* ~400 bytes / entry, capa for ~2 packs */
                       svn_fs_fs__serialize_revprops,
                       svn_fs_fs__deserialize_revprops,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "REVPROP", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       TRUE, /* contents is short-lived */
                       fs,
                       no_handler,
                       fs->pool, pool));

  /* if enabled, cache fulltext and other derived information */
  if (cache_fulltexts)
    {
      SVN_ERR(create_cache(&(ffd->fulltext_cache),
                           ffd->memcache,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           /* Values are svn_stringbuf_t */
                           NULL, NULL,
                           sizeof(pair_cache_key_t),
                           apr_pstrcat(pool, prefix, "TEXT", SVN_VA_NULL),
                           SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));

      SVN_ERR(create_cache(&(ffd->mergeinfo_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           svn_fs_fs__serialize_mergeinfo,
                           svn_fs_fs__deserialize_mergeinfo,
                           APR_HASH_KEY_STRING,
                           apr_pstrcat(pool, prefix, "MERGEINFO",
                                       SVN_VA_NULL),
                           0,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));

      SVN_ERR(create_cache(&(ffd->mergeinfo_existence_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           /* Values are svn_stringbuf_t */
                           NULL, NULL,
                           APR_HASH_KEY_STRING,
                           apr_pstrcat(pool, prefix, "HAS_MERGEINFO",
                                       SVN_VA_NULL),
                           0,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));
    }
  else
    {
      ffd->fulltext_cache = NULL;
      ffd->mergeinfo_cache = NULL;
      ffd->mergeinfo_existence_cache = NULL;
    }

  /* if enabled, cache node properties */
  if (cache_nodeprops)
    {
      SVN_ERR(create_cache(&(ffd->properties_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           svn_fs_fs__serialize_properties,
                           svn_fs_fs__deserialize_properties,
                           sizeof(pair_cache_key_t),
                           apr_pstrcat(pool, prefix, "PROP",
                                       SVN_VA_NULL),
                           SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));
    }
  else
    {
      ffd->properties_cache = NULL;
    }

  /* if enabled, cache text deltas and their combinations */
  if (cache_txdeltas)
    {
      SVN_ERR(create_cache(&(ffd->raw_window_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           svn_fs_fs__serialize_raw_window,
                           svn_fs_fs__deserialize_raw_window,
                           sizeof(window_cache_key_t),
                           apr_pstrcat(pool, prefix, "RAW_WINDOW",
                                       SVN_VA_NULL),
                           SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));

      SVN_ERR(create_cache(&(ffd->txdelta_window_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           svn_fs_fs__serialize_txdelta_window,
                           svn_fs_fs__deserialize_txdelta_window,
                           sizeof(window_cache_key_t),
                           apr_pstrcat(pool, prefix, "TXDELTA_WINDOW",
                                       SVN_VA_NULL),
                           SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));

      SVN_ERR(create_cache(&(ffd->combined_window_cache),
                           NULL,
                           membuffer,
                           0, 0, /* Do not use the inprocess cache */
                           /* Values are svn_stringbuf_t */
                           NULL, NULL,
                           sizeof(window_cache_key_t),
                           apr_pstrcat(pool, prefix, "COMBINED_WINDOW",
                                       SVN_VA_NULL),
                           SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                           has_namespace,
                           fs,
                           no_handler,
                           fs->pool, pool));
    }
  else
    {
      ffd->txdelta_window_cache = NULL;
      ffd->combined_window_cache = NULL;
    }

  SVN_ERR(create_cache(&(ffd->l2p_header_cache),
                       NULL,
                       membuffer,
                       8, 16, /* entry size varies but we must cover a
                                 reasonable number of rev / pack files
                                 to allow for delta chains to be walked
                                 efficiently etc. */
                       svn_fs_fs__serialize_l2p_header,
                       svn_fs_fs__deserialize_l2p_header,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "L2P_HEADER",
                                   (char *)NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));
  SVN_ERR(create_cache(&(ffd->l2p_page_cache),
                       NULL,
                       membuffer,
                       8, 16, /* entry size varies but we must cover a
                                 reasonable number of rev / pack files
                                 to allow for delta chains to be walked
                                 efficiently etc. */
                       svn_fs_fs__serialize_l2p_page,
                       svn_fs_fs__deserialize_l2p_page,
                       sizeof(svn_fs_fs__page_cache_key_t),
                       apr_pstrcat(pool, prefix, "L2P_PAGE",
                                   (char *)NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));
  SVN_ERR(create_cache(&(ffd->p2l_header_cache),
                       NULL,
                       membuffer,
                       4, 1, /* Large entries. Rarely used. */
                       svn_fs_fs__serialize_p2l_header,
                       svn_fs_fs__deserialize_p2l_header,
                       sizeof(pair_cache_key_t),
                       apr_pstrcat(pool, prefix, "P2L_HEADER",
                                   (char *)NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));
  SVN_ERR(create_cache(&(ffd->p2l_page_cache),
                       NULL,
                       membuffer,
                       4, 1, /* Variably sized entries. Rarely used. */
                       svn_fs_fs__serialize_p2l_page,
                       svn_fs_fs__deserialize_p2l_page,
                       sizeof(svn_fs_fs__page_cache_key_t),
                       apr_pstrcat(pool, prefix, "P2L_PAGE",
                                   (char *)NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler,
                       fs->pool, pool));

  return SVN_NO_ERROR;
}

/* Baton to be used for the remove_txn_cache() pool cleanup function, */
struct txn_cleanup_baton_t
{
  /* the cache to reset */
  svn_cache__t *txn_cache;

  /* the position where to reset it */
  svn_cache__t **to_reset;

  /* pool that TXN_CACHE was allocated in */
  apr_pool_t *txn_pool;

  /* pool that the FS containing the TO_RESET pointer was allocator */
  apr_pool_t *fs_pool;
};

/* Forward declaration. */
static apr_status_t
remove_txn_cache_fs(void *baton_void);

/* APR pool cleanup handler that will reset the cache pointer given in
   BATON_VOID when the TXN_POOL gets cleaned up. */
static apr_status_t
remove_txn_cache_txn(void *baton_void)
{
  struct txn_cleanup_baton_t *baton = baton_void;

  /* be careful not to hurt performance by resetting newer txn's caches. */
  if (*baton->to_reset == baton->txn_cache)
    {
      /* This is equivalent to calling svn_fs_fs__reset_txn_caches(). */
      *baton->to_reset = NULL;
    }

  /* It's cleaned up now. Prevent double cleanup. */
  apr_pool_cleanup_kill(baton->fs_pool,
                        baton,
                        remove_txn_cache_fs);

  return  APR_SUCCESS;
}

/* APR pool cleanup handler that will reset the cache pointer given in
   BATON_VOID when the FS_POOL gets cleaned up. */
static apr_status_t
remove_txn_cache_fs(void *baton_void)
{
  struct txn_cleanup_baton_t *baton = baton_void;

  /* be careful not to hurt performance by resetting newer txn's caches. */
  if (*baton->to_reset == baton->txn_cache)
    {
     /* This is equivalent to calling svn_fs_fs__reset_txn_caches(). */
      *baton->to_reset = NULL;
    }

  /* It's cleaned up now. Prevent double cleanup. */
  apr_pool_cleanup_kill(baton->txn_pool,
                        baton,
                        remove_txn_cache_txn);

  return  APR_SUCCESS;
}

/* This function sets / registers the required callbacks for a given
 * transaction-specific *CACHE object in FS, if CACHE is not NULL and
 * a no-op otherwise. In particular, it will ensure that *CACHE gets
 * reset to NULL upon POOL or FS->POOL destruction latest.
 */
static void
init_txn_callbacks(svn_fs_t *fs,
                   svn_cache__t **cache,
                   apr_pool_t *pool)
{
  if (*cache != NULL)
    {
      struct txn_cleanup_baton_t *baton;

      baton = apr_palloc(pool, sizeof(*baton));
      baton->txn_cache = *cache;
      baton->to_reset = cache;
      baton->txn_pool = pool;
      baton->fs_pool = fs->pool;

      /* If any of these pools gets cleaned, we must reset the cache.
       * We don't know which one will get cleaned up first, so register
       * cleanup actions for both and during the cleanup action, unregister
       * the respective other action. */
      apr_pool_cleanup_register(pool,
                                baton,
                                remove_txn_cache_txn,
                                apr_pool_cleanup_null);
      apr_pool_cleanup_register(fs->pool,
                                baton,
                                remove_txn_cache_fs,
                                apr_pool_cleanup_null);
    }
}

svn_error_t *
svn_fs_fs__initialize_txn_caches(svn_fs_t *fs,
                                 const char *txn_id,
                                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *prefix;

  /* We don't support caching for concurrent transactions in the SAME
   * FSFS session. Maybe, you forgot to clean POOL. */
  if (ffd->txn_dir_cache != NULL || ffd->concurrent_transactions)
    {
      ffd->txn_dir_cache = NULL;
      ffd->concurrent_transactions = TRUE;

      return SVN_NO_ERROR;
    }

  /* Transaction content needs to be carefully prefixed to virtually
     eliminate any chance for conflicts. The (repo, txn_id) pair
     should be unique but if the filesystem format doesn't store the
     global transaction ID via the txn-current file, and a transaction
     fails, it might be possible to start a new transaction later that
     receives the same id.  For such older formats, throw in an uuid as
     well -- just to be sure. */
  if (ffd->format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    prefix = apr_pstrcat(pool,
                         "fsfs:", fs->uuid,
                         "/", fs->path,
                         ":", txn_id,
                         ":", "TXNDIR",
                         SVN_VA_NULL);
  else
    prefix = apr_pstrcat(pool,
                         "fsfs:", fs->uuid,
                         "/", fs->path,
                         ":", txn_id,
                         ":", svn_uuid_generate(pool),
                         ":", "TXNDIR",
                         SVN_VA_NULL);

  /* create a txn-local directory cache */
  SVN_ERR(create_cache(&ffd->txn_dir_cache,
                       NULL,
                       svn_cache__get_global_membuffer_cache(),
                       1024, 8,
                       svn_fs_fs__serialize_txndir_entries,
                       svn_fs_fs__deserialize_dir_entries,
                       APR_HASH_KEY_STRING,
                       prefix,
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       TRUE, /* The TXN-ID is our namespace. */
                       fs,
                       TRUE,
                       pool, pool));

  /* reset the transaction-specific cache if the pool gets cleaned up. */
  init_txn_callbacks(fs, &(ffd->txn_dir_cache), pool);

  return SVN_NO_ERROR;
}

void
svn_fs_fs__reset_txn_caches(svn_fs_t *fs)
{
  /* we can always just reset the caches. This may degrade performance but
   * can never cause in incorrect behavior. */

  fs_fs_data_t *ffd = fs->fsap_data;
  ffd->txn_dir_cache = NULL;
}
