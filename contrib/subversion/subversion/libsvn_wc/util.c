/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
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
#include <apr_file_io.h>

#include "svn_io.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_version.h"

#include "wc.h"   /* just for prototypes of things in this .c file */
#include "entries.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


svn_error_t *
svn_wc__ensure_directory(const char *path,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(path, &kind, pool));

  if (kind != svn_node_none && kind != svn_node_dir)
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_error_createf(APR_ENOTDIR, NULL,
                               _("'%s' is not a directory"),
                               svn_dirent_local_style(path, pool));
    }
  else if (kind == svn_node_none)
    {
      /* The dir doesn't exist, and it's our job to change that. */
      SVN_ERR(svn_io_make_dir_recursively(path, pool));
    }
  else  /* No problem, the dir already existed, so just leave. */
    SVN_ERR_ASSERT(kind == svn_node_dir);

  return SVN_NO_ERROR;
}

/* Return the library version number. */
const svn_version_t *
svn_wc_version(void)
{
  SVN_VERSION_BODY;
}

svn_wc_notify_t *
svn_wc_create_notify(const char *path,
                     svn_wc_notify_action_t action,
                     apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_pcalloc(pool, sizeof(*ret));
  ret->path = path;
  ret->action = action;
  ret->kind = svn_node_unknown;
  ret->content_state = ret->prop_state = svn_wc_notify_state_unknown;
  ret->lock_state = svn_wc_notify_lock_state_unknown;
  ret->revision = SVN_INVALID_REVNUM;
  ret->old_revision = SVN_INVALID_REVNUM;

  return ret;
}

svn_wc_notify_t *
svn_wc_create_notify_url(const char *url,
                         svn_wc_notify_action_t action,
                         apr_pool_t *pool)
{
  svn_wc_notify_t *ret = svn_wc_create_notify(".", action, pool);
  ret->url = url;

  return ret;
}

/* Pool cleanup function to clear an svn_error_t *. */
static apr_status_t err_cleanup(void *data)
{
  svn_error_clear(data);

  return APR_SUCCESS;
}

svn_wc_notify_t *
svn_wc_dup_notify(const svn_wc_notify_t *notify,
                  apr_pool_t *pool)
{
  svn_wc_notify_t *ret = apr_palloc(pool, sizeof(*ret));

  *ret = *notify;

  if (ret->path)
    ret->path = apr_pstrdup(pool, ret->path);
  if (ret->mime_type)
    ret->mime_type = apr_pstrdup(pool, ret->mime_type);
  if (ret->lock)
    ret->lock = svn_lock_dup(ret->lock, pool);
  if (ret->err)
    {
      ret->err = svn_error_dup(ret->err);
      apr_pool_cleanup_register(pool, ret->err, err_cleanup,
                                apr_pool_cleanup_null);
    }
  if (ret->changelist_name)
    ret->changelist_name = apr_pstrdup(pool, ret->changelist_name);
  if (ret->merge_range)
    ret->merge_range = svn_merge_range_dup(ret->merge_range, pool);
  if (ret->url)
    ret->url = apr_pstrdup(pool, ret->url);
  if (ret->path_prefix)
    ret->path_prefix = apr_pstrdup(pool, ret->path_prefix);
  if (ret->prop_name)
    ret->prop_name = apr_pstrdup(pool, ret->prop_name);
  if (ret->rev_props)
    ret->rev_props = svn_prop_hash_dup(ret->rev_props, pool);

  return ret;
}

svn_error_t *
svn_wc_external_item2_create(svn_wc_external_item2_t **item,
                             apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_wc_external_item2_t));
  return SVN_NO_ERROR;
}


svn_wc_external_item2_t *
svn_wc_external_item2_dup(const svn_wc_external_item2_t *item,
                          apr_pool_t *pool)
{
  svn_wc_external_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}


svn_boolean_t
svn_wc_match_ignore_list(const char *str, const apr_array_header_t *list,
                         apr_pool_t *pool)
{
  /* For now, we simply forward to svn_cstring_match_glob_list. In the
     future, if we support more complex ignore patterns, we would iterate
     over 'list' ourselves, and decide for each pattern how to handle
     it. */

  return svn_cstring_match_glob_list(str, list);
}

svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_text2(const char *local_abspath,
                                         apr_pool_t *result_pool)
{
  svn_wc_conflict_description2_t *conflict;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_abspath));

  conflict = apr_pcalloc(result_pool, sizeof(*conflict));
  conflict->local_abspath = apr_pstrdup(result_pool, local_abspath);
  conflict->node_kind = svn_node_file;
  conflict->kind = svn_wc_conflict_kind_text;
  conflict->action = svn_wc_conflict_action_edit;
  conflict->reason = svn_wc_conflict_reason_edited;
  return conflict;
}

svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_prop2(const char *local_abspath,
                                         svn_node_kind_t node_kind,
                                         const char *property_name,
                                         apr_pool_t *result_pool)
{
  svn_wc_conflict_description2_t *conflict;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_abspath));

  conflict = apr_pcalloc(result_pool, sizeof(*conflict));
  conflict->local_abspath = apr_pstrdup(result_pool, local_abspath);
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_property;
  conflict->property_name = apr_pstrdup(result_pool, property_name);
  return conflict;
}

svn_wc_conflict_description2_t *
svn_wc_conflict_description_create_tree2(
  const char *local_abspath,
  svn_node_kind_t node_kind,
  svn_wc_operation_t operation,
  const svn_wc_conflict_version_t *src_left_version,
  const svn_wc_conflict_version_t *src_right_version,
  apr_pool_t *result_pool)
{
  svn_wc_conflict_description2_t *conflict;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_abspath));

  conflict = apr_pcalloc(result_pool, sizeof(*conflict));
  conflict->local_abspath = apr_pstrdup(result_pool, local_abspath);
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_tree;
  conflict->operation = operation;
  conflict->src_left_version = svn_wc_conflict_version_dup(src_left_version,
                                                           result_pool);
  conflict->src_right_version = svn_wc_conflict_version_dup(src_right_version,
                                                            result_pool);
  return conflict;
}

svn_wc_conflict_version_t *
svn_wc_conflict_version_create2(const char *repos_url,
                                const char *repos_uuid,
                                const char *repos_relpath,
                                svn_revnum_t revision,
                                svn_node_kind_t kind,
                                apr_pool_t *result_pool)
{
  svn_wc_conflict_version_t *version;

  version = apr_pcalloc(result_pool, sizeof(*version));

    SVN_ERR_ASSERT_NO_RETURN(svn_uri_is_canonical(repos_url, result_pool)
                             && svn_relpath_is_canonical(repos_relpath)
                             && SVN_IS_VALID_REVNUM(revision)
                             /* ### repos_uuid can be NULL :( */);

  version->repos_url = repos_url;
  version->peg_rev = revision;
  version->path_in_repos = repos_relpath;
  version->node_kind = kind;
  version->repos_uuid = repos_uuid;

  return version;
}

svn_wc_conflict_description2_t *
svn_wc_conflict_description2_dup(const svn_wc_conflict_description2_t *conflict,
                                  apr_pool_t *pool)
{
  svn_wc_conflict_description2_t *new_conflict;

  new_conflict = apr_pcalloc(pool, sizeof(*new_conflict));

  /* Shallow copy all members. */
  *new_conflict = *conflict;

  if (conflict->local_abspath)
    new_conflict->local_abspath = apr_pstrdup(pool, conflict->local_abspath);
  if (conflict->property_name)
    new_conflict->property_name = apr_pstrdup(pool, conflict->property_name);
  if (conflict->mime_type)
    new_conflict->mime_type = apr_pstrdup(pool, conflict->mime_type);
  if (conflict->base_abspath)
    new_conflict->base_abspath = apr_pstrdup(pool, conflict->base_abspath);
  if (conflict->their_abspath)
    new_conflict->their_abspath = apr_pstrdup(pool, conflict->their_abspath);
  if (conflict->my_abspath)
    new_conflict->my_abspath = apr_pstrdup(pool, conflict->my_abspath);
  if (conflict->merged_file)
    new_conflict->merged_file = apr_pstrdup(pool, conflict->merged_file);
  if (conflict->src_left_version)
    new_conflict->src_left_version =
      svn_wc_conflict_version_dup(conflict->src_left_version, pool);
  if (conflict->src_right_version)
    new_conflict->src_right_version =
      svn_wc_conflict_version_dup(conflict->src_right_version, pool);

  /* ### For property conflicts, cd2 stores prop_reject_abspath in
   * ### their_abspath, and stores theirs_abspath in merged_file. */
  if (conflict->prop_reject_abspath)
    new_conflict->prop_reject_abspath = new_conflict->their_abspath;

  if (conflict->prop_value_base)
    new_conflict->prop_value_base =
      svn_string_dup(conflict->prop_value_base, pool);
  if (conflict->prop_value_working)
    new_conflict->prop_value_working =
      svn_string_dup(conflict->prop_value_working, pool);
  if (conflict->prop_value_incoming_old)
    new_conflict->prop_value_incoming_old =
      svn_string_dup(conflict->prop_value_incoming_old, pool);
  if (conflict->prop_value_incoming_new)
    new_conflict->prop_value_incoming_new =
      svn_string_dup(conflict->prop_value_incoming_new, pool);

  return new_conflict;
}

svn_wc_conflict_version_t *
svn_wc_conflict_version_dup(const svn_wc_conflict_version_t *version,
                            apr_pool_t *result_pool)
{

  svn_wc_conflict_version_t *new_version;

  if (version == NULL)
    return NULL;

  new_version = apr_pcalloc(result_pool, sizeof(*new_version));

  /* Shallow copy all members. */
  *new_version = *version;

  if (version->repos_url)
    new_version->repos_url = apr_pstrdup(result_pool, version->repos_url);

  if (version->path_in_repos)
    new_version->path_in_repos = apr_pstrdup(result_pool,
                                             version->path_in_repos);

  if (version->repos_uuid)
    new_version->repos_uuid = apr_pstrdup(result_pool, version->repos_uuid);

  return new_version;
}

svn_wc_conflict_description_t *
svn_wc__cd2_to_cd(const svn_wc_conflict_description2_t *conflict,
                  apr_pool_t *result_pool)
{
  svn_wc_conflict_description_t *new_conflict;

  if (conflict == NULL)
    return NULL;

  new_conflict = apr_pcalloc(result_pool, sizeof(*new_conflict));

  new_conflict->path = apr_pstrdup(result_pool, conflict->local_abspath);
  new_conflict->node_kind = conflict->node_kind;
  new_conflict->kind = conflict->kind;
  new_conflict->action = conflict->action;
  new_conflict->reason = conflict->reason;
  if (conflict->src_left_version)
    new_conflict->src_left_version =
          svn_wc_conflict_version_dup(conflict->src_left_version, result_pool);
  if (conflict->src_right_version)
    new_conflict->src_right_version =
          svn_wc_conflict_version_dup(conflict->src_right_version, result_pool);

  switch (conflict->kind)
    {

      case svn_wc_conflict_kind_property:
        new_conflict->property_name = apr_pstrdup(result_pool,
                                                  conflict->property_name);
        /* Falling through. */

      case svn_wc_conflict_kind_text:
        new_conflict->is_binary = conflict->is_binary;
        if (conflict->mime_type)
          new_conflict->mime_type = apr_pstrdup(result_pool,
                                                conflict->mime_type);
        if (conflict->base_abspath)
          new_conflict->base_file = apr_pstrdup(result_pool,
                                                conflict->base_abspath);
        if (conflict->their_abspath)
          new_conflict->their_file = apr_pstrdup(result_pool,
                                                 conflict->their_abspath);
        if (conflict->my_abspath)
          new_conflict->my_file = apr_pstrdup(result_pool,
                                              conflict->my_abspath);
        if (conflict->merged_file)
          new_conflict->merged_file = apr_pstrdup(result_pool,
                                                  conflict->merged_file);
        break;

      case svn_wc_conflict_kind_tree:
        new_conflict->operation = conflict->operation;
        break;
    }

  /* A NULL access baton is allowable by the API. */
  new_conflict->access = NULL;

  return new_conflict;
}


svn_error_t *
svn_wc__fetch_kind_func(svn_node_kind_t *kind,
                        void *baton,
                        const char *path,
                        svn_revnum_t base_revision,
                        apr_pool_t *scratch_pool)
{
  struct svn_wc__shim_fetch_baton_t *sfb = baton;
  const char *local_abspath = svn_dirent_join(sfb->base_abspath, path,
                                              scratch_pool);

  SVN_ERR(svn_wc__db_read_kind(kind, sfb->db, local_abspath,
                               FALSE /* allow_missing */,
                               TRUE /* show_deleted */,
                               FALSE /* show_hidden */,
                               scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__fetch_props_func(apr_hash_t **props,
                         void *baton,
                         const char *path,
                         svn_revnum_t base_revision,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  struct svn_wc__shim_fetch_baton_t *sfb = baton;
  const char *local_abspath = svn_dirent_join(sfb->base_abspath, path,
                                              scratch_pool);
  svn_error_t *err;

  if (sfb->fetch_base)
    err = svn_wc__db_base_get_props(props, sfb->db, local_abspath, result_pool,
                                    scratch_pool);
  else
    err = svn_wc__db_read_props(props, sfb->db, local_abspath,
                                result_pool, scratch_pool);

  /* If the path doesn't exist, just return an empty set of props. */
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *props = apr_hash_make(result_pool);
    }
  else if (err)
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__fetch_base_func(const char **filename,
                        void *baton,
                        const char *path,
                        svn_revnum_t base_revision,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  struct svn_wc__shim_fetch_baton_t *sfb = baton;
  const svn_checksum_t *checksum;
  svn_error_t *err;
  const char *local_abspath = svn_dirent_join(sfb->base_abspath, path,
                                              scratch_pool);

  err = svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, &checksum,
                                 NULL, NULL, NULL, NULL, NULL,
                                 sfb->db, local_abspath,
                                 scratch_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  if (checksum == NULL)
    {
      *filename = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__db_pristine_get_path(filename, sfb->db, local_abspath,
                                       checksum, scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}
