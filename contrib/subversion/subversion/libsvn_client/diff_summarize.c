/*
 * repos_diff_summarize.c -- The diff callbacks for summarizing
 * the differences of two repository versions
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


#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_wc_private.h"

#include "client.h"


/* Diff callbacks baton.  */
struct summarize_baton_t {
  apr_pool_t *baton_pool; /* For allocating skip_path */

  /* The target path of the diff, relative to the anchor; "" if target == anchor. */
  const char *skip_relpath;

  /* The summarize callback passed down from the API */
  svn_client_diff_summarize_func_t summarize_func;

  /* The summarize callback baton */
  void *summarize_func_baton;
};

/* Call B->summarize_func with B->summarize_func_baton, passing it a
 * summary object composed from PATH (but made to be relative to the target
 * of the diff), SUMMARIZE_KIND, PROP_CHANGED (or FALSE if the action is an
 * add or delete) and NODE_KIND. */
static svn_error_t *
send_summary(struct summarize_baton_t *b,
             const char *path,
             svn_client_diff_summarize_kind_t summarize_kind,
             svn_boolean_t prop_changed,
             svn_node_kind_t node_kind,
             apr_pool_t *scratch_pool)
{
  svn_client_diff_summarize_t *sum = apr_pcalloc(scratch_pool, sizeof(*sum));

  SVN_ERR_ASSERT(summarize_kind != svn_client_diff_summarize_kind_normal
                 || prop_changed);

  /* PATH is relative to the anchor of the diff, but SUM->path needs to be
     relative to the target of the diff. */
  sum->path = svn_relpath_skip_ancestor(b->skip_relpath, path);
  sum->summarize_kind = summarize_kind;
  if (summarize_kind == svn_client_diff_summarize_kind_modified
      || summarize_kind == svn_client_diff_summarize_kind_normal)
    sum->prop_changed = prop_changed;
  sum->node_kind = node_kind;

  SVN_ERR(b->summarize_func(sum, b->summarize_func_baton, scratch_pool));
  return SVN_NO_ERROR;
}

/* Are there any changes to relevant (normal) props in PROPS? */
static svn_boolean_t
props_changed_hash(apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  if (!props)
    return FALSE;

  for (hi = apr_hash_first(scratch_pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);

      if (svn_property_kind2(name) == svn_prop_regular_kind)
        {
          return TRUE;
        }
    }

  return FALSE;
}

/* Are there any changes to relevant (normal) props in PROPCHANGES? */
static svn_boolean_t
props_changed(const apr_array_header_t *propchanges,
              apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;

  svn_error_clear(svn_categorize_props(propchanges, NULL, NULL, &props,
                                       scratch_pool));
  return (props->nelts != 0);
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_dir_opened(void **new_dir_baton,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                const svn_diff_source_t *copyfrom_source,
                void *parent_dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  /* struct summarize_baton_t *b = processor->baton; */

  /* ### Send here instead of from dir_added() ? */
  /*if (!left_source)
    {
      SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_added,
                           FALSE, svn_node_dir, scratch_pool));
    }*/

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_normal,
                       TRUE, svn_node_dir, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  /* ### Send from dir_opened without prop info? */
  SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_added,
                       props_changed_hash(right_props, scratch_pool),
                       svn_node_dir, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_deleted,
                       FALSE, svn_node_dir, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_added,
                       props_changed_hash(right_props, scratch_pool),
                       svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  SVN_ERR(send_summary(b, relpath,
                       file_modified ? svn_client_diff_summarize_kind_modified
                                     : svn_client_diff_summarize_kind_normal,
                       props_changed(prop_changes, scratch_pool),
                       svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t callback */
static svn_error_t *
diff_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct summarize_baton_t *b = processor->baton;

  SVN_ERR(send_summary(b, relpath, svn_client_diff_summarize_kind_deleted,
                       FALSE, svn_node_file, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_diff_summarize_callbacks(
                        const svn_diff_tree_processor_t **diff_processor,
                        const char ***p_root_relpath,
                        svn_client_diff_summarize_func_t summarize_func,
                        void *summarize_baton,
                        const char *original_target,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_diff_tree_processor_t *dp;
  struct summarize_baton_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->baton_pool = result_pool;
  b->summarize_func = summarize_func;
  b->summarize_func_baton = summarize_baton;

  dp = svn_diff__tree_processor_create(b, result_pool);

  /*dp->file_opened = diff_file_opened;*/
  dp->file_added = diff_file_added;
  dp->file_deleted = diff_file_deleted;
  dp->file_changed = diff_file_changed;

  dp->dir_opened = diff_dir_opened;
  dp->dir_changed = diff_dir_changed;
  dp->dir_deleted = diff_dir_deleted;
  dp->dir_added = diff_dir_added;

  *diff_processor = dp;
  *p_root_relpath = &b->skip_relpath;

  return SVN_NO_ERROR;
}

svn_client_diff_summarize_t *
svn_client_diff_summarize_dup(const svn_client_diff_summarize_t *diff,
                              apr_pool_t *pool)
{
  svn_client_diff_summarize_t *dup_diff = apr_palloc(pool, sizeof(*dup_diff));

  *dup_diff = *diff;

  if (diff->path)
    dup_diff->path = apr_pstrdup(pool, diff->path);

  return dup_diff;
}
