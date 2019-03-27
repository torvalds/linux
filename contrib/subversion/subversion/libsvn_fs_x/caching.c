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
#include "fs_x.h"
#include "id.h"
#include "dag_cache.h"
#include "index.h"
#include "changes.h"
#include "noderevs.h"
#include "temp_serializer.h"
#include "reps.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_config.h"
#include "svn_cache_config.h"

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"

#include "private/svn_debug.h"
#include "private/svn_subr_private.h"

/* Take the ORIGINAL string and replace all occurrences of ":" without
 * limiting the key space.  Allocate the result in RESULT_POOL.
 */
static const char *
normalize_key_part(const char *original,
                   apr_pool_t *result_pool)
{
  apr_size_t i;
  apr_size_t len = strlen(original);
  svn_stringbuf_t *normalized = svn_stringbuf_create_ensure(len,
                                                            result_pool);

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

/* *CACHE_TXDELTAS, *CACHE_FULLTEXTS, *CACHE_REVPROPS and *CACHE_NODEPROPS
   flags will be set according to FS->CONFIG.  *CACHE_NAMESPACE receives
   the cache prefix to use.

   Allocate CACHE_NAMESPACE in RESULT_POOL. */
static svn_error_t *
read_config(const char **cache_namespace,
            svn_boolean_t *cache_txdeltas,
            svn_boolean_t *cache_fulltexts,
            svn_boolean_t *cache_revprops,
            svn_boolean_t *cache_nodeprops,
            svn_fs_t *fs,
            apr_pool_t *result_pool)
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
                         result_pool);

  /* don't cache text deltas by default.
   * Once we reconstructed the fulltexts from the deltas,
   * these deltas are rarely re-used. Therefore, only tools
   * like svnadmin will activate this to speed up operations
   * dump and verify.
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

  /* don't cache revprops by default.
   * Revprop caching significantly speeds up operations like
   * svn ls -v. However, it requires synchronization that may
   * not be available or efficient in the current server setup.
   * Option "2" is equivalent to "1".
   */
  if (strcmp(svn_hash__get_cstring(fs->config,
                                   SVN_FS_CONFIG_FSFS_CACHE_REVPROPS,
                                   ""), "2"))
    *cache_revprops
      = svn_hash__get_bool(fs->config,
                          SVN_FS_CONFIG_FSFS_CACHE_REVPROPS,
                          FALSE);
  else
    *cache_revprops = TRUE;

  /* by default, cache nodeprops: this will match pre-1.10
   * behavior where node properties caching was controlled
   * by SVN_FS_CONFIG_FSFS_CACHE_FULLTEXTS configuration option.
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
typedef struct dump_cache_baton_t
{
  /* the pool about to be cleaned up. Will be used for temp. allocations. */
  apr_pool_t *pool;

  /* the cache to dump the statistics for */
  svn_cache__t *cache;
} dump_cache_baton_t;

/* APR pool cleanup handler that will printf the statistics of the
   cache referenced by the baton in BATON_VOID. */
static apr_status_t
dump_cache_statistics(void *baton_void)
{
  dump_cache_baton_t *baton = baton_void;

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
#ifdef SVN_DEBUG_CACHE_DUMP_STATS

  /* schedule printing the access statistics upon pool cleanup,
   * i.e. end of FSX session.
   */
  dump_cache_baton_t *baton;

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

  return SVN_NO_ERROR;
}

/* Sets *CACHE_P to cache instance based on provided options.
 *
 * If DUMMY_CACHE is set, create a null cache.  Otherwise, creates a memcache
 * if MEMCACHE is not NULL or a membuffer cache if MEMBUFFER is not NULL.
 * Falls back to inprocess cache if no other cache type has been selected
 * and PAGES is not 0.  Create a null cache otherwise.
 *
 * Use the given PRIORITY class for the new cache.  If PRIORITY is 0, then
 * use the default priority class. HAS_NAMESPACE indicates whether we
 * prefixed this cache instance with a namespace.
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
             svn_boolean_t dummy_cache,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_cache__error_handler_t error_handler = no_handler
                                           ? NULL
                                           : warn_and_fail_on_cache_errors;
  if (priority == 0)
    priority = SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY;

  if (dummy_cache)
    {
      SVN_ERR(svn_cache__create_null(cache_p, prefix, result_pool));
    }
  else if (memcache)
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
      SVN_ERR(svn_cache__create_null(cache_p, prefix, result_pool));
    }

  SVN_ERR(init_callbacks(*cache_p, fs, error_handler, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__initialize_caches(svn_fs_t *fs,
                            apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  const char *prefix = apr_pstrcat(scratch_pool,
                                   "fsx:", fs->uuid,
                                   "--", ffd->instance_id,
                                   "/", normalize_key_part(fs->path,
                                                           scratch_pool),
                                   ":",
                                   SVN_VA_NULL);
  svn_membuffer_t *membuffer;
  svn_boolean_t no_handler = ffd->fail_stop;
  svn_boolean_t cache_txdeltas;
  svn_boolean_t cache_fulltexts;
  svn_boolean_t cache_revprops;
  svn_boolean_t cache_nodeprops;
  const char *cache_namespace;
  svn_boolean_t has_namespace;

  /* Evaluating the cache configuration. */
  SVN_ERR(read_config(&cache_namespace,
                      &cache_txdeltas,
                      &cache_fulltexts,
                      &cache_revprops,
                      &cache_nodeprops,
                      fs,
                      scratch_pool));

  prefix = apr_pstrcat(scratch_pool, "ns:", cache_namespace, ":", prefix,
                       SVN_VA_NULL);
  has_namespace = strlen(cache_namespace) > 0;

  membuffer = svn_cache__get_global_membuffer_cache();

  /* General rules for assigning cache priorities:
   *
   * - Data that can be reconstructed from other elements has low prio
   *   (e.g. fulltexts etc.)
   * - Index data required to find any of the other data has high prio
   *   (e.g. noderevs, L2P and P2L index pages)
   * - everthing else should use default prio
   */

#ifdef SVN_DEBUG_CACHE_DUMP_STATS

  /* schedule printing the global access statistics upon pool cleanup,
   * i.e. end of FSX session.
   */
  if (membuffer)
    apr_pool_cleanup_register(fs->pool,
                              fs->pool,
                              dump_global_cache_statistics,
                              apr_pool_cleanup_null);
#endif

  /* 1st level DAG node cache */
  ffd->dag_node_cache = svn_fs_x__create_dag_cache(fs->pool);

  /* Very rough estimate: 1K per directory. */
  SVN_ERR(create_cache(&(ffd->dir_cache),
                       NULL,
                       membuffer,
                       1024, 8,
                       svn_fs_x__serialize_dir_entries,
                       svn_fs_x__deserialize_dir_entries,
                       sizeof(svn_fs_x__id_t),
                       apr_pstrcat(scratch_pool, prefix, "DIR", SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  /* initialize node revision cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->node_revision_cache),
                       NULL,
                       membuffer,
                       32, 32, /* ~200 byte / entry; 1k entries total */
                       svn_fs_x__serialize_node_revision,
                       svn_fs_x__deserialize_node_revision,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "NODEREVS",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  /* initialize representation header cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->rep_header_cache),
                       NULL,
                       membuffer,
                       1, 1000, /* ~8 bytes / entry; 1k entries total */
                       svn_fs_x__serialize_rep_header,
                       svn_fs_x__deserialize_rep_header,
                       sizeof(svn_fs_x__representation_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "REPHEADER",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  /* initialize node change list cache, if caching has been enabled */
  SVN_ERR(create_cache(&(ffd->changes_cache),
                       NULL,
                       membuffer,
                       1, 8, /* 1k / entry; 8 entries total, rarely used */
                       svn_fs_x__serialize_changes,
                       svn_fs_x__deserialize_changes,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "CHANGES",
                                   SVN_VA_NULL),
                       0,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  /* if enabled, cache fulltext and other derived information */
  SVN_ERR(create_cache(&(ffd->fulltext_cache),
                       ffd->memcache,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       /* Values are svn_stringbuf_t */
                       NULL, NULL,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "TEXT",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, !cache_fulltexts,
                       fs->pool, scratch_pool));

  SVN_ERR(create_cache(&(ffd->properties_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       svn_fs_x__serialize_properties,
                       svn_fs_x__deserialize_properties,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "PROP",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, !cache_nodeprops,
                       fs->pool, scratch_pool));

  /* if enabled, cache revprops */
  SVN_ERR(create_cache(&(ffd->revprop_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       svn_fs_x__serialize_properties,
                       svn_fs_x__deserialize_properties,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "REVPROP",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_DEFAULT_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, !cache_revprops,
                       fs->pool, scratch_pool));

  /* if enabled, cache text deltas and their combinations */
  SVN_ERR(create_cache(&(ffd->txdelta_window_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       svn_fs_x__serialize_txdelta_window,
                       svn_fs_x__deserialize_txdelta_window,
                       sizeof(svn_fs_x__window_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "TXDELTA_WINDOW",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, !cache_txdeltas,
                       fs->pool, scratch_pool));

  SVN_ERR(create_cache(&(ffd->combined_window_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       /* Values are svn_stringbuf_t */
                       NULL, NULL,
                       sizeof(svn_fs_x__window_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "COMBINED_WINDOW",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_LOW_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, !cache_txdeltas,
                       fs->pool, scratch_pool));

  /* Caches for our various container types. */
  SVN_ERR(create_cache(&(ffd->noderevs_container_cache),
                       NULL,
                       membuffer,
                       16, 4, /* Important, largish objects */
                       svn_fs_x__serialize_noderevs_container,
                       svn_fs_x__deserialize_noderevs_container,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "NODEREVSCNT",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));
  SVN_ERR(create_cache(&(ffd->changes_container_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       svn_fs_x__serialize_changes_container,
                       svn_fs_x__deserialize_changes_container,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "CHANGESCNT",
                                   SVN_VA_NULL),
                       0,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));
  SVN_ERR(create_cache(&(ffd->reps_container_cache),
                       NULL,
                       membuffer,
                       0, 0, /* Do not use inprocess cache */
                       svn_fs_x__serialize_reps_container,
                       svn_fs_x__deserialize_reps_container,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "REPSCNT",
                                   SVN_VA_NULL),
                       0,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  /* Cache index info. */
  SVN_ERR(create_cache(&(ffd->l2p_header_cache),
                       NULL,
                       membuffer,
                       64, 16, /* entry size varies but we must cover
                                  a reasonable number of revisions (1k) */
                       svn_fs_x__serialize_l2p_header,
                       svn_fs_x__deserialize_l2p_header,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "L2P_HEADER",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));
  SVN_ERR(create_cache(&(ffd->l2p_page_cache),
                       NULL,
                       membuffer,
                       64, 16, /* entry size varies but we must cover
                                  a reasonable number of revisions (1k) */
                       svn_fs_x__serialize_l2p_page,
                       svn_fs_x__deserialize_l2p_page,
                       sizeof(svn_fs_x__page_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "L2P_PAGE",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));
  SVN_ERR(create_cache(&(ffd->p2l_header_cache),
                       NULL,
                       membuffer,
                       4, 1, /* Large entries. Rarely used. */
                       svn_fs_x__serialize_p2l_header,
                       svn_fs_x__deserialize_p2l_header,
                       sizeof(svn_fs_x__pair_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "P2L_HEADER",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));
  SVN_ERR(create_cache(&(ffd->p2l_page_cache),
                       NULL,
                       membuffer,
                       4, 16, /* Variably sized entries. Rarely used. */
                       svn_fs_x__serialize_p2l_page,
                       svn_fs_x__deserialize_p2l_page,
                       sizeof(svn_fs_x__page_cache_key_t),
                       apr_pstrcat(scratch_pool, prefix, "P2L_PAGE",
                                   SVN_VA_NULL),
                       SVN_CACHE__MEMBUFFER_HIGH_PRIORITY,
                       has_namespace,
                       fs,
                       no_handler, FALSE,
                       fs->pool, scratch_pool));

  return SVN_NO_ERROR;
}
