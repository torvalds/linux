/*
 * compat.c:  compatibility shims to adapt between different API versions.
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

#include "svn_repos.h"
#include "svn_compat.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "svn_private_config.h"

#include "repos.h"

#include "private/svn_repos_private.h"
#include "private/svn_subr_private.h"



/*** log4 -> log5 ***/

/* Baton type to be used with both log4 compatibility callbacks.
 * For each revision, we collect the CHANGES and then pass them
 * on to INNER. */
typedef struct log_entry_receiver_baton_t
{
  /* Pool to use to allocate CHANGES and its entries.
   * Gets cleared after each revision. */
  apr_pool_t *changes_pool;

  /* Path changes reported so far for the current revision.
   * Will be NULL before the first item gets added and will be reset
   * to NULL after the INNER callback has returned. */
  apr_hash_t *changes;

  /* User-provided callback to send the log entry to. */
  svn_log_entry_receiver_t inner;
  void *inner_baton;
} log_entry_receiver_baton_t;

/* Return the action character (see svn_log_changed_path2_t) for KIND.
 * Returns 0 for invalid KINDs. */
static char
path_change_kind_to_char(svn_fs_path_change_kind_t kind)
{
  const char symbol[] = "MADR";

  if (kind < svn_fs_path_change_modify || kind > svn_fs_path_change_replace)
    return 0;

  return symbol[kind];
}

/* Implement svn_repos_path_change_receiver_t.
 * Convert CHANGE and add it to the CHANGES list in *BATON. */
static svn_error_t *
log4_path_change_receiver(void *baton,
                          svn_repos_path_change_t *change,
                          apr_pool_t *scratch_pool)
{
  log_entry_receiver_baton_t *b = baton;
  svn_log_changed_path2_t *change_copy;
  const char *path = apr_pstrmemdup(b->changes_pool, change->path.data,
                                    change->path.len);

  /* Create a deep copy of the temporary CHANGE struct. */
  change_copy = svn_log_changed_path2_create(b->changes_pool);
  change_copy->action = path_change_kind_to_char(change->change_kind);

  if (change->copyfrom_path)
    change_copy->copyfrom_path = apr_pstrdup(b->changes_pool,
                                             change->copyfrom_path);

  change_copy->copyfrom_rev = change->copyfrom_rev;
  change_copy->node_kind = change->node_kind;
  change_copy->text_modified = change->text_mod ? svn_tristate_true
                                                : svn_tristate_false;
  change_copy->props_modified = change->prop_mod ? svn_tristate_true
                                                 : svn_tristate_false;

  /* Auto-create the CHANGES container (happens for each first change
   * in any revison. */
  if (b->changes == NULL)
    b->changes = svn_hash__make(b->changes_pool);

  /* Add change to per-revision collection. */
  apr_hash_set(b->changes, path, change->path.len, change_copy);

  return SVN_NO_ERROR;
}

/* Implement svn_log_entry_receiver_t.
 * Combine the data gathered in BATON for this revision and send it
 * to the user-provided log4-compatible callback. */
static svn_error_t *
log4_entry_receiver(void *baton,
                    svn_repos_log_entry_t *log_entry,
                    apr_pool_t *scratch_pool)
{
  log_entry_receiver_baton_t *b = baton;
  svn_log_entry_t *entry = svn_log_entry_create(scratch_pool);

  /* Complete the ENTRY. */
  entry->changed_paths = b->changes;
  entry->revision = log_entry->revision;
  entry->revprops = log_entry->revprops;
  entry->has_children = log_entry->has_children;
  entry->changed_paths2 = b->changes;
  entry->non_inheritable = log_entry->non_inheritable;
  entry->subtractive_merge = log_entry->subtractive_merge;

  /* Invoke the log4-compatible callback. */
  SVN_ERR(b->inner(b->inner_baton, entry, scratch_pool));

  /* Release per-revision data. */
  svn_pool_clear(b->changes_pool);
  b->changes = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__get_logs_compat(svn_repos_t *repos,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           int limit,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_boolean_t include_merged_revisions,
                           const apr_array_header_t *revprops,
                           svn_repos_authz_func_t authz_read_func,
                           void *authz_read_baton,
                           svn_log_entry_receiver_t receiver,
                           void *receiver_baton,
                           apr_pool_t *pool)
{
  apr_pool_t *changes_pool = svn_pool_create(pool);

  log_entry_receiver_baton_t baton;
  baton.changes_pool = changes_pool;
  baton.changes = NULL;
  baton.inner = receiver;
  baton.inner_baton = receiver_baton;

  SVN_ERR(svn_repos_get_logs5(repos, paths, start, end, limit,
                              strict_node_history,
                              include_merged_revisions,
                              revprops,
                              authz_read_func, authz_read_baton,
                              discover_changed_paths
                                ? log4_path_change_receiver
                                : NULL,
                              &baton,
                              log4_entry_receiver, &baton,
                              pool));

  svn_pool_destroy(changes_pool);
  return SVN_NO_ERROR;
}
