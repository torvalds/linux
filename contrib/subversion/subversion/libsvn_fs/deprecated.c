/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include <apr_md5.h>
#include "svn_fs.h"
#include "private/svn_subr_private.h"


/*** From fs-loader.c ***/
svn_error_t *
svn_fs_upgrade(const char *path, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_upgrade2(path, NULL, NULL, NULL, NULL, pool));
}

svn_error_t *
svn_fs_hotcopy2(const char *src_path, const char *dest_path,
                svn_boolean_t clean, svn_boolean_t incremental,
                svn_cancel_func_t cancel_func, void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_fs_hotcopy3(src_path, dest_path, clean,
                                         incremental, NULL, NULL,
                                         cancel_func, cancel_baton,
                                         scratch_pool));
}

svn_error_t *
svn_fs_hotcopy(const char *src_path, const char *dest_path,
               svn_boolean_t clean, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_hotcopy2(src_path, dest_path, clean,
                                         FALSE, NULL, NULL, pool));
}

svn_error_t *
svn_fs_begin_txn(svn_fs_txn_t **txn_p, svn_fs_t *fs, svn_revnum_t rev,
                 apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_begin_txn2(txn_p, fs, rev, 0, pool));
}

svn_error_t *
svn_fs_revision_prop(svn_string_t **value_p,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     const char *propname,
                     apr_pool_t *pool)
{
  return svn_error_trace(
           svn_fs_revision_prop2(value_p, fs, rev, propname, TRUE, pool,
                                 pool));
}

svn_error_t *
svn_fs_revision_proplist(apr_hash_t **table_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  return svn_error_trace(
           svn_fs_revision_proplist2(table_p, fs, rev, TRUE, pool, pool));
}

svn_error_t *
svn_fs_change_rev_prop(svn_fs_t *fs, svn_revnum_t rev, const char *name,
                       const svn_string_t *value, apr_pool_t *pool)
{
  return svn_error_trace(
           svn_fs_change_rev_prop2(fs, rev, name, NULL, value, pool));
}

svn_error_t *
svn_fs_get_locks(svn_fs_t *fs, const char *path,
                 svn_fs_get_locks_callback_t get_locks_func,
                 void *get_locks_baton, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_get_locks2(fs, path, svn_depth_infinity,
                                           get_locks_func, get_locks_baton,
                                           pool));
}

svn_error_t *
svn_fs_create(svn_fs_t **fs_p,
              const char *path,
              apr_hash_t *fs_config,
              apr_pool_t *pool)
{
  return svn_fs_create2(fs_p, path, fs_config, pool, pool);
}

svn_error_t *
svn_fs_open(svn_fs_t **fs_p,
            const char *path,
            apr_hash_t *fs_config,
            apr_pool_t *pool)
{
  return svn_fs_open2(fs_p, path, fs_config, pool, pool);
}

svn_error_t *
svn_fs_node_history(svn_fs_history_t **history_p, svn_fs_root_t *root,
                    const char *path, apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_node_history2(history_p, root, path,
                                              pool, pool));
}

static svn_error_t *
mergeinfo_receiver(const char *path,
                   svn_mergeinfo_t mergeinfo,
                   void *baton,
                   apr_pool_t *scratch_pool)
{
  svn_mergeinfo_catalog_t catalog = baton;
  apr_pool_t *result_pool = apr_hash_pool_get(catalog);
  apr_size_t len = strlen(path);

  apr_hash_set(catalog,
               apr_pstrmemdup(result_pool, path, len),
               len,
               svn_mergeinfo_dup(mergeinfo, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_get_mergeinfo2(svn_mergeinfo_catalog_t *catalog,
                      svn_fs_root_t *root,
                      const apr_array_header_t *paths,
                      svn_mergeinfo_inheritance_t inherit,
                      svn_boolean_t include_descendants,
                      svn_boolean_t adjust_inherited_mergeinfo,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_mergeinfo_catalog_t result_catalog = svn_hash__make(result_pool);
  SVN_ERR(svn_fs_get_mergeinfo3(root, paths, inherit,
                                include_descendants,
                                adjust_inherited_mergeinfo,
                                mergeinfo_receiver, result_catalog,
                                scratch_pool));
  *catalog = result_catalog;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_get_mergeinfo(svn_mergeinfo_catalog_t *catalog,
                     svn_fs_root_t *root,
                     const apr_array_header_t *paths,
                     svn_mergeinfo_inheritance_t inherit,
                     svn_boolean_t include_descendants,
                     apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_get_mergeinfo2(catalog, root, paths,
                                               inherit,
                                               include_descendants,
                                               TRUE, pool, pool));
}

svn_error_t *
svn_fs_paths_changed(apr_hash_t **changed_paths_p, svn_fs_root_t *root,
                     apr_pool_t *pool)
{
  apr_hash_t *changed_paths_new_structs;
  apr_hash_index_t *hi;

  SVN_ERR(svn_fs_paths_changed2(&changed_paths_new_structs, root, pool));
  *changed_paths_p = apr_hash_make(pool);
  for (hi = apr_hash_first(pool, changed_paths_new_structs);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *vkey;
      apr_ssize_t klen;
      void *vval;
      svn_fs_path_change2_t *val;
      svn_fs_path_change_t *change;
      apr_hash_this(hi, &vkey, &klen, &vval);
      val = vval;
      change = apr_palloc(pool, sizeof(*change));
      change->node_rev_id = val->node_rev_id;
      change->change_kind = val->change_kind;
      change->text_mod = val->text_mod;
      change->prop_mod = val->prop_mod;
      apr_hash_set(*changed_paths_p, vkey, klen, change);
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_file_md5_checksum(unsigned char digest[],
                         svn_fs_root_t *root,
                         const char *path,
                         apr_pool_t *pool)
{
  svn_checksum_t *md5sum;

  SVN_ERR(svn_fs_file_checksum(&md5sum, svn_checksum_md5, root, path, TRUE,
                               pool));
  memcpy(digest, md5sum->digest, APR_MD5_DIGESTSIZE);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_history_prev(svn_fs_history_t **prev_history_p,
                    svn_fs_history_t *history, svn_boolean_t cross_copies,
                    apr_pool_t *pool)
{
  return svn_error_trace(svn_fs_history_prev2(prev_history_p, history,
                                              cross_copies, pool, pool));
}

/*** From access.c ***/
svn_error_t *
svn_fs_access_add_lock_token(svn_fs_access_t *access_ctx,
                             const char *token)
{
  return svn_fs_access_add_lock_token2(access_ctx, (const char *) 1, token);
}
