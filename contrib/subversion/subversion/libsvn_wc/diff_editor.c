/*
 * diff_editor.c -- The diff editor for comparing the working copy against the
 *                  repository.
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

/*
 * This code uses an svn_delta_editor_t editor driven by
 * svn_wc_crawl_revisions (like the update command) to retrieve the
 * differences between the working copy and the requested repository
 * version. Rather than updating the working copy, this new editor creates
 * temporary files that contain the pristine repository versions. When the
 * crawler closes the files the editor calls back to a client layer
 * function to compare the working copy and the temporary file. There is
 * only ever one temporary file in existence at any time.
 *
 * When the crawler closes a directory, the editor then calls back to the
 * client layer to compare any remaining files that may have been modified
 * locally. Added directories do not have corresponding temporary
 * directories created, as they are not needed.
 *
 * The diff result from this editor is a combination of the restructuring
 * operations from the repository with the local restructurings since checking
 * out.
 *
 * ### TODO: Make sure that we properly support and report multi layered
 *           operations instead of only simple file replacements.
 *
 * ### TODO: Replacements where the node kind changes needs support. It
 * mostly works when the change is in the repository, but not when it is
 * in the working copy.
 *
 * ### TODO: Do we need to support copyfrom?
 *
 */

#include <apr_hash.h>
#include <apr_md5.h>

#include <assert.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_sorts.h"

#include "private/svn_diff_tree.h"
#include "private/svn_editor.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"

#include "wc.h"
#include "props.h"
#include "adm_files.h"
#include "translate.h"
#include "diff.h"

#include "svn_private_config.h"

/*-------------------------------------------------------------------------*/


/* Overall crawler editor baton.
 */
struct edit_baton_t
{
  /* A wc db. */
  svn_wc__db_t *db;

  /* A diff tree processor, receiving the result of the diff. */
  const svn_diff_tree_processor_t *processor;

  /* A boolean indicating whether local additions should be reported before
     remote deletes. The processor can transform adds in deletes and deletes
     in adds, but it can't reorder the output. */
  svn_boolean_t local_before_remote;

  /* ANCHOR/TARGET represent the base of the hierarchy to be compared. */
  const char *target;
  const char *anchor_abspath;

  /* Target revision */
  svn_revnum_t revnum;

  /* Was the root opened? */
  svn_boolean_t root_opened;

  /* How does this diff descend as seen from target? */
  svn_depth_t depth;

  /* Should this diff ignore node ancestry? */
  svn_boolean_t ignore_ancestry;

  /* Possibly diff repos against text-bases instead of working files. */
  svn_boolean_t diff_pristine;

  /* Cancel function/baton */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  apr_pool_t *pool;
};

/* Directory level baton.
 */
struct dir_baton_t
{
  /* Reference to parent directory baton (or NULL for the root) */
  struct dir_baton_t *parent_baton;

  /* The depth at which this directory should be diffed. */
  svn_depth_t depth;

  /* The name and path of this directory as if they would be/are in the
      local working copy. */
  const char *name;
  const char *relpath;
  const char *local_abspath;

  /* TRUE if the file is added by the editor drive. */
  svn_boolean_t added;
  /* TRUE if the node exists only on the repository side (op_depth 0 or added) */
  svn_boolean_t repos_only;
  /* TRUE if the node is to be compared with an unrelated node*/
  svn_boolean_t ignoring_ancestry;

  /* TRUE if the directory was reported incomplete to the repository */
  svn_boolean_t is_incomplete;

  /* Processor state */
  void *pdb;
  svn_boolean_t skip;
  svn_boolean_t skip_children;

  svn_diff_source_t *left_src;
  svn_diff_source_t *right_src;

  apr_hash_t *local_info;

  /* A hash containing the basenames of the nodes reported deleted by the
     repository (or NULL for no values). */
  apr_hash_t *deletes;

  /* Identifies those directory elements that get compared while running
     the crawler.  These elements should not be compared again when
     recursively looking for local modifications.

     This hash maps the basename of the node to an unimportant value.

     If the directory's properties have been compared, an item with hash
     key of "" will be present in the hash. */
  apr_hash_t *compared;

  /* The list of incoming BASE->repos propchanges. */
  apr_array_header_t *propchanges;

  /* Has a change on regular properties */
  svn_boolean_t has_propchange;

  /* The overall crawler editor baton. */
  struct edit_baton_t *eb;

  apr_pool_t *pool;
  int users;
};

/* File level baton.
 */
struct file_baton_t
{
  struct dir_baton_t *parent_baton;

  /* The name and path of this file as if they would be/are in the
     parent directory, diff session and local working copy. */
  const char *name;
  const char *relpath;
  const char *local_abspath;

  /* Processor state */
  void *pfb;
  svn_boolean_t skip;

  /* TRUE if the file is added by the editor drive. */
  svn_boolean_t added;
  /* TRUE if the node exists only on the repository side (op_depth 0 or added) */
  svn_boolean_t repos_only;
  /* TRUE if the node is to be compared with an unrelated node*/
  svn_boolean_t ignoring_ancestry;

  const svn_diff_source_t *left_src;
  const svn_diff_source_t *right_src;

  /* The list of incoming BASE->repos propchanges. */
  apr_array_header_t *propchanges;

  /* Has a change on regular properties */
  svn_boolean_t has_propchange;

  /* The current BASE checksum and props */
  const svn_checksum_t *base_checksum;
  apr_hash_t *base_props;

  /* The resulting from apply_textdelta */
  const char *temp_file_path;
  unsigned char result_digest[APR_MD5_DIGESTSIZE];

  /* The overall crawler editor baton. */
  struct edit_baton_t *eb;

  apr_pool_t *pool;
};

/* Create a new edit baton. TARGET_PATH/ANCHOR are working copy paths
 * that describe the root of the comparison. CALLBACKS/CALLBACK_BATON
 * define the callbacks to compare files. DEPTH defines if and how to
 * descend into subdirectories; see public doc string for exactly how.
 * IGNORE_ANCESTRY defines whether to utilize node ancestry when
 * calculating diffs.  USE_TEXT_BASE defines whether to compare
 * against working files or text-bases.  REVERSE_ORDER defines which
 * direction to perform the diff.
 */
static svn_error_t *
make_edit_baton(struct edit_baton_t **edit_baton,
                svn_wc__db_t *db,
                const char *anchor_abspath,
                const char *target,
                const svn_diff_tree_processor_t *diff_processor,
                svn_depth_t depth,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t use_text_base,
                svn_boolean_t reverse_order,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  struct edit_baton_t *eb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(anchor_abspath));

  eb = apr_pcalloc(pool, sizeof(*eb));
  eb->db = db;
  eb->anchor_abspath = apr_pstrdup(pool, anchor_abspath);
  eb->target = apr_pstrdup(pool, target);
  eb->processor = diff_processor;
  eb->depth = depth;
  eb->ignore_ancestry = ignore_ancestry;
  eb->local_before_remote = reverse_order;
  eb->diff_pristine = use_text_base;
  eb->cancel_func = cancel_func;
  eb->cancel_baton = cancel_baton;
  eb->pool = pool;

  *edit_baton = eb;
  return SVN_NO_ERROR;
}

/* Create a new directory baton.  PATH is the directory path,
 * including anchor_path.  ADDED is set if this directory is being
 * added rather than replaced.  PARENT_BATON is the baton of the
 * parent directory, it will be null if this is the root of the
 * comparison hierarchy.  The directory and its parent may or may not
 * exist in the working copy.  EDIT_BATON is the overall crawler
 * editor baton.
 */
static struct dir_baton_t *
make_dir_baton(const char *path,
               struct dir_baton_t *parent_baton,
               struct edit_baton_t *eb,
               svn_boolean_t added,
               svn_depth_t depth,
               apr_pool_t *result_pool)
{
  apr_pool_t *dir_pool = svn_pool_create(parent_baton ? parent_baton->pool
                                                      : eb->pool);
  struct dir_baton_t *db = apr_pcalloc(dir_pool, sizeof(*db));

  db->parent_baton = parent_baton;

  /* Allocate 1 string for using as 3 strings */
  db->local_abspath = svn_dirent_join(eb->anchor_abspath, path, dir_pool);
  db->relpath = svn_dirent_skip_ancestor(eb->anchor_abspath, db->local_abspath);
  db->name = svn_dirent_basename(db->relpath, NULL);

  db->eb = eb;
  db->added = added;
  db->depth = depth;
  db->pool = dir_pool;
  db->propchanges = apr_array_make(dir_pool, 8, sizeof(svn_prop_t));
  db->compared = apr_hash_make(dir_pool);

  if (parent_baton != NULL)
    {
      parent_baton->users++;
    }

  db->users = 1;

  return db;
}

/* Create a new file baton.  PATH is the file path, including
 * anchor_path.  ADDED is set if this file is being added rather than
 * replaced.  PARENT_BATON is the baton of the parent directory.
 * The directory and its parent may or may not exist in the working copy.
 */
static struct file_baton_t *
make_file_baton(const char *path,
                svn_boolean_t added,
                struct dir_baton_t *parent_baton,
                apr_pool_t *result_pool)
{
  apr_pool_t *file_pool = svn_pool_create(result_pool);
  struct file_baton_t *fb = apr_pcalloc(file_pool, sizeof(*fb));
  struct edit_baton_t *eb = parent_baton->eb;

  fb->eb = eb;
  fb->parent_baton = parent_baton;
  fb->parent_baton->users++;

  /* Allocate 1 string for using as 3 strings */
  fb->local_abspath = svn_dirent_join(eb->anchor_abspath, path, file_pool);
  fb->relpath = svn_dirent_skip_ancestor(eb->anchor_abspath, fb->local_abspath);
  fb->name = svn_dirent_basename(fb->relpath, NULL);

  fb->added = added;
  fb->pool = file_pool;
  fb->propchanges  = apr_array_make(file_pool, 8, sizeof(svn_prop_t));

  return fb;
}

/* Destroy DB when there are no more registered users */
static svn_error_t *
maybe_done(struct dir_baton_t *db)
{
  db->users--;

  if (!db->users)
    {
      struct dir_baton_t *pb = db->parent_baton;

      svn_pool_clear(db->pool);

      if (pb != NULL)
        SVN_ERR(maybe_done(pb));
    }

  return SVN_NO_ERROR;
}

/* Standard check to see if a node is represented in the local working copy */
#define NOT_PRESENT(status)                                    \
            ((status) == svn_wc__db_status_not_present          \
             || (status) == svn_wc__db_status_excluded          \
             || (status) == svn_wc__db_status_server_excluded)

svn_error_t *
svn_wc__diff_base_working_diff(svn_wc__db_t *db,
                               const char *local_abspath,
                               const char *relpath,
                               svn_revnum_t revision,
                               const svn_diff_tree_processor_t *processor,
                               void *processor_dir_baton,
                               svn_boolean_t diff_pristine,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool)
{
  void *file_baton = NULL;
  svn_boolean_t skip = FALSE;
  svn_wc__db_status_t status;
  svn_revnum_t db_revision;
  svn_boolean_t had_props;
  svn_boolean_t props_mod;
  svn_boolean_t files_same = FALSE;
  svn_wc__db_status_t base_status;
  const svn_checksum_t *working_checksum;
  const svn_checksum_t *checksum;
  svn_filesize_t recorded_size;
  apr_time_t recorded_time;
  const char *pristine_file;
  const char *local_file;
  svn_diff_source_t *left_src;
  svn_diff_source_t *right_src;
  apr_hash_t *base_props;
  apr_hash_t *local_props;
  apr_array_header_t *prop_changes;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, &db_revision, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, &working_checksum, NULL,
                               NULL, NULL, NULL, NULL, NULL, &recorded_size,
                               &recorded_time, NULL, NULL, NULL,
                               &had_props, &props_mod, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));
  checksum = working_checksum;

  assert(status == svn_wc__db_status_normal
         || status == svn_wc__db_status_added
         || (status == svn_wc__db_status_deleted && diff_pristine));

  if (status != svn_wc__db_status_normal)
    {
      SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, &db_revision,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, &checksum, NULL, NULL, &had_props,
                                       NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));
      recorded_size = SVN_INVALID_FILESIZE;
      recorded_time = 0;
      props_mod = TRUE; /* Requires compare */
    }
  else if (diff_pristine)
    files_same = TRUE;
  else
    {
      const svn_io_dirent2_t *dirent;

      /* Verify truename to mimic status for iota/IOTA difference on Windows */
      SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath,
                                  TRUE /* verify truename */,
                                  TRUE /* ingore_enoent */,
                                  scratch_pool, scratch_pool));

      /* If a file does not exist on disk (missing/obstructed) then we
         can't provide a text diff */
      if (dirent->kind != svn_node_file
          || (dirent->kind == svn_node_file
              && dirent->filesize == recorded_size
              && dirent->mtime == recorded_time))
        {
          files_same = TRUE;
        }
    }

  if (files_same && !props_mod)
    return SVN_NO_ERROR; /* Cheap exit */

  assert(checksum);

  if (!SVN_IS_VALID_REVNUM(revision))
    revision = db_revision;

  left_src = svn_diff__source_create(revision, scratch_pool);
  right_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);

  SVN_ERR(processor->file_opened(&file_baton, &skip, relpath,
                                 left_src,
                                 right_src,
                                 NULL /* copyfrom_src */,
                                 processor_dir_baton,
                                 processor,
                                 scratch_pool, scratch_pool));

  if (skip)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_pristine_get_path(&pristine_file,
                                       db, local_abspath, checksum,
                                       scratch_pool, scratch_pool));

  if (diff_pristine)
    SVN_ERR(svn_wc__db_pristine_get_path(&local_file,
                                         db, local_abspath,
                                         working_checksum,
                                         scratch_pool, scratch_pool));
  else if (! (had_props || props_mod))
    local_file = local_abspath;
  else if (files_same)
    local_file = pristine_file;
  else
    SVN_ERR(svn_wc__internal_translated_file(
                            &local_file, local_abspath,
                            db, local_abspath,
                            SVN_WC_TRANSLATE_TO_NF
                                | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
                            cancel_func, cancel_baton,
                            scratch_pool, scratch_pool));

  if (! files_same)
    SVN_ERR(svn_io_files_contents_same_p(&files_same, local_file,
                                         pristine_file, scratch_pool));

  if (had_props)
    SVN_ERR(svn_wc__db_base_get_props(&base_props, db, local_abspath,
                                      scratch_pool, scratch_pool));
  else
    base_props = apr_hash_make(scratch_pool);

  if (status == svn_wc__db_status_normal && (diff_pristine || !props_mod))
    local_props = base_props;
  else if (diff_pristine)
    SVN_ERR(svn_wc__db_read_pristine_props(&local_props, db, local_abspath,
                                           scratch_pool, scratch_pool));
  else
    SVN_ERR(svn_wc__db_read_props(&local_props, db, local_abspath,
                                  scratch_pool, scratch_pool));

  SVN_ERR(svn_prop_diffs(&prop_changes, local_props, base_props, scratch_pool));

  if (prop_changes->nelts || !files_same)
    {
      SVN_ERR(processor->file_changed(relpath,
                                      left_src,
                                      right_src,
                                      pristine_file,
                                      local_file,
                                      base_props,
                                      local_props,
                                      ! files_same,
                                      prop_changes,
                                      file_baton,
                                      processor,
                                      scratch_pool));
    }
  else
    {
      SVN_ERR(processor->file_closed(relpath,
                                     left_src,
                                     right_src,
                                     file_baton,
                                     processor,
                                     scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_local_info(struct dir_baton_t *db,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *conflicts;

  if (db->local_info)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_read_children_info(&db->local_info, &conflicts,
                                        db->eb->db, db->local_abspath,
                                        FALSE /* base_tree_only */,
                                        db->pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Called when the directory is closed to compare any elements that have
 * not yet been compared.  This identifies local, working copy only
 * changes.  At this stage we are dealing with files/directories that do
 * exist in the working copy.
 *
 * DIR_BATON is the baton for the directory.
 */
static svn_error_t *
walk_local_nodes_diff(struct edit_baton_t *eb,
                      const char *local_abspath,
                      const char *path,
                      svn_depth_t depth,
                      apr_hash_t *compared,
                      void *parent_baton,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = eb->db;
  svn_boolean_t in_anchor_not_target;
  apr_pool_t *iterpool;
  void *dir_baton = NULL;
  svn_boolean_t skip = FALSE;
  svn_boolean_t skip_children = FALSE;
  svn_revnum_t revision;
  svn_boolean_t props_mod;
  svn_diff_source_t *left_src;
  svn_diff_source_t *right_src;

  /* Everything we do below is useless if we are comparing to BASE. */
  if (eb->diff_pristine)
    return SVN_NO_ERROR;

  /* Determine if this is the anchor directory if the anchor is different
     to the target. When the target is a file, the anchor is the parent
     directory and if this is that directory the non-target entries must be
     skipped. */
  in_anchor_not_target = ((*path == '\0') && (*eb->target != '\0'));

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_read_info(NULL, NULL, &revision, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, &props_mod, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  left_src = svn_diff__source_create(revision, scratch_pool);
  right_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);

  if (compared)
    {
      dir_baton = parent_baton;
      skip = TRUE;
    }
  else if (!in_anchor_not_target)
    SVN_ERR(eb->processor->dir_opened(&dir_baton, &skip, &skip_children,
                                      path,
                                      left_src,
                                      right_src,
                                      NULL /* copyfrom_src */,
                                      parent_baton,
                                      eb->processor,
                                      scratch_pool, scratch_pool));


  if (!skip_children && depth != svn_depth_empty)
    {
      apr_hash_t *nodes;
      apr_hash_t *conflicts;
      apr_array_header_t *children;
      svn_depth_t depth_below_here = depth;
      svn_boolean_t diff_files;
      svn_boolean_t diff_dirs;
      int i;

      if (depth_below_here == svn_depth_immediates)
        depth_below_here = svn_depth_empty;

      diff_files = (depth == svn_depth_unknown
                   || depth >= svn_depth_files);
      diff_dirs = (depth == svn_depth_unknown
                   || depth >= svn_depth_immediates);

      SVN_ERR(svn_wc__db_read_children_info(&nodes, &conflicts,
                                            db, local_abspath,
                                            FALSE /* base_tree_only */,
                                            scratch_pool, iterpool));

      children = svn_sort__hash(nodes, svn_sort_compare_items_lexically,
                            scratch_pool);

      for (i = 0; i < children->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(children, i,
                                                  svn_sort__item_t);
          const char *name = item->key;
          struct svn_wc__db_info_t *info = item->value;

          const char *child_abspath;
          const char *child_relpath;
          svn_boolean_t repos_only;
          svn_boolean_t local_only;
          svn_node_kind_t base_kind;

          if (eb->cancel_func)
            SVN_ERR(eb->cancel_func(eb->cancel_baton));

          /* In the anchor directory, if the anchor is not the target then all
             entries other than the target should not be diff'd. Running diff
             on one file in a directory should not diff other files in that
             directory. */
          if (in_anchor_not_target && strcmp(eb->target, name))
            continue;

          if (compared && svn_hash_gets(compared, name))
            continue;

          if (NOT_PRESENT(info->status))
            continue;

          assert(info->status == svn_wc__db_status_normal
                 || info->status == svn_wc__db_status_added
                 || info->status == svn_wc__db_status_deleted);

          svn_pool_clear(iterpool);
          child_abspath = svn_dirent_join(local_abspath, name, iterpool);
          child_relpath = svn_relpath_join(path, name, iterpool);

          repos_only = FALSE;
          local_only = FALSE;

          if (!info->have_base)
            {
              local_only = TRUE; /* Only report additions */

              if (info->status == svn_wc__db_status_deleted)
                continue; /* Nothing added (deleted copy) */
            }
          else if (info->status == svn_wc__db_status_normal)
            {
              /* Simple diff */
              base_kind = info->kind;
            }
          else if (info->status == svn_wc__db_status_deleted
                   && (!eb->diff_pristine || !info->have_more_work))
            {
              svn_wc__db_status_t base_status;
              repos_only = TRUE;
              SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, child_abspath,
                                               iterpool, iterpool));

              if (NOT_PRESENT(base_status))
                continue;
            }
          else
            {
              /* working status is either added or deleted */
              svn_wc__db_status_t base_status;

              SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, child_abspath,
                                               iterpool, iterpool));

              if (NOT_PRESENT(base_status))
                local_only = TRUE;
              else if (base_kind != info->kind || !eb->ignore_ancestry)
                {
                  repos_only = TRUE;
                  local_only = TRUE;
                }
            }

          if (eb->local_before_remote && local_only)
            {
              const char *moved_from_relpath;

              if (info->moved_here)
                {
                  const char *moved_from_abspath;

                  SVN_ERR(svn_wc__db_scan_moved(&moved_from_abspath,
                                                NULL, NULL, NULL,
                                                db, child_abspath,
                                                iterpool, iterpool));
                  SVN_ERR_ASSERT(moved_from_abspath != NULL);

                  moved_from_relpath = svn_dirent_skip_ancestor(
                                                        eb->anchor_abspath,
                                                        moved_from_abspath);
                }
              else
                moved_from_relpath = NULL;

              if (info->kind == svn_node_file && diff_files)
                SVN_ERR(svn_wc__diff_local_only_file(db, child_abspath,
                                                     child_relpath,
                                                     moved_from_relpath,
                                                     eb->processor, dir_baton,
                                                     eb->diff_pristine,
                                                     eb->cancel_func,
                                                     eb->cancel_baton,
                                                     iterpool));
              else if (info->kind == svn_node_dir && diff_dirs)
                SVN_ERR(svn_wc__diff_local_only_dir(db, child_abspath,
                                                    child_relpath,
                                                    depth_below_here,
                                                    moved_from_relpath,
                                                    eb->processor, dir_baton,
                                                    eb->diff_pristine,
                                                    eb->cancel_func,
                                                    eb->cancel_baton,
                                                    iterpool));
            }

          if (repos_only)
            {
              /* Report repository form deleted */
              if (base_kind == svn_node_file && diff_files)
                SVN_ERR(svn_wc__diff_base_only_file(db, child_abspath,
                                                    child_relpath, eb->revnum,
                                                    eb->processor, dir_baton,
                                                    iterpool));
              else if (base_kind == svn_node_dir && diff_dirs)
                SVN_ERR(svn_wc__diff_base_only_dir(db, child_abspath,
                                                   child_relpath, eb->revnum,
                                                   depth_below_here,
                                                   eb->processor, dir_baton,
                                                   eb->cancel_func,
                                                   eb->cancel_baton,
                                                   iterpool));
            }
          else if (!local_only) /* Not local only nor remote only */
            {
              /* Diff base against actual */
              if (info->kind == svn_node_file && diff_files)
                {
                  if (info->status != svn_wc__db_status_normal
                      || !eb->diff_pristine)
                    {
                      SVN_ERR(svn_wc__diff_base_working_diff(
                                                db, child_abspath,
                                                child_relpath,
                                                eb->revnum,
                                                eb->processor, dir_baton,
                                                eb->diff_pristine,
                                                eb->cancel_func,
                                                eb->cancel_baton,
                                                scratch_pool));
                    }
                }
              else if (info->kind == svn_node_dir && diff_dirs)
                SVN_ERR(walk_local_nodes_diff(eb, child_abspath,
                                              child_relpath,
                                              depth_below_here,
                                              NULL /* compared */,
                                              dir_baton,
                                              scratch_pool));
            }

          if (!eb->local_before_remote && local_only)
            {
              const char *moved_from_relpath;

              if (info->moved_here)
                {
                  const char *moved_from_abspath;

                  SVN_ERR(svn_wc__db_scan_moved(&moved_from_abspath,
                                                NULL, NULL, NULL,
                                                db, child_abspath,
                                                iterpool, iterpool));
                  SVN_ERR_ASSERT(moved_from_abspath != NULL);

                  moved_from_relpath = svn_dirent_skip_ancestor(
                                                        eb->anchor_abspath,
                                                        moved_from_abspath);
                }
              else
                moved_from_relpath = NULL;

              if (info->kind == svn_node_file && diff_files)
                SVN_ERR(svn_wc__diff_local_only_file(db, child_abspath,
                                                     child_relpath,
                                                     moved_from_relpath,
                                                     eb->processor, dir_baton,
                                                     eb->diff_pristine,
                                                     eb->cancel_func,
                                                     eb->cancel_baton,
                                                     iterpool));
              else if (info->kind == svn_node_dir && diff_dirs)
                SVN_ERR(svn_wc__diff_local_only_dir(db, child_abspath,
                                                    child_relpath, depth_below_here,
                                                    moved_from_relpath,
                                                    eb->processor, dir_baton,
                                                    eb->diff_pristine,
                                                    eb->cancel_func,
                                                    eb->cancel_baton,
                                                    iterpool));
            }
        }
    }

  if (compared)
    return SVN_NO_ERROR;

  /* Check for local property mods on this directory, if we haven't
     already reported them. */
  if (! skip
      && ! in_anchor_not_target
      && props_mod)
    {
      apr_array_header_t *propchanges;
      apr_hash_t *left_props;
      apr_hash_t *right_props;

      SVN_ERR(svn_wc__internal_propdiff(&propchanges, &left_props,
                                        db, local_abspath,
                                        scratch_pool, scratch_pool));

      right_props = svn_prop__patch(left_props, propchanges, scratch_pool);

      SVN_ERR(eb->processor->dir_changed(path,
                                         left_src,
                                         right_src,
                                         left_props,
                                         right_props,
                                         propchanges,
                                         dir_baton,
                                         eb->processor,
                                         scratch_pool));
    }
  else if (! skip)
    SVN_ERR(eb->processor->dir_closed(path,
                                      left_src,
                                      right_src,
                                      dir_baton,
                                      eb->processor,
                                      scratch_pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__diff_local_only_file(svn_wc__db_t *db,
                             const char *local_abspath,
                             const char *relpath,
                             const char *moved_from_relpath,
                             const svn_diff_tree_processor_t *processor,
                             void *processor_parent_baton,
                             svn_boolean_t diff_pristine,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool)
{
  svn_diff_source_t *right_src;
  svn_diff_source_t *copyfrom_src = NULL;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const svn_checksum_t *checksum;
  const char *original_repos_relpath;
  svn_revnum_t original_revision;
  svn_boolean_t had_props;
  svn_boolean_t props_mod;
  apr_hash_t *pristine_props;
  apr_hash_t *right_props = NULL;
  const char *pristine_file;
  const char *translated_file;
  svn_revnum_t revision;
  void *file_baton = NULL;
  svn_boolean_t skip = FALSE;
  svn_boolean_t file_mod = TRUE;

  SVN_ERR(svn_wc__db_read_info(&status, &kind, &revision, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, &checksum, NULL,
                               &original_repos_relpath, NULL, NULL,
                               &original_revision, NULL, NULL, NULL,
                               NULL, NULL, NULL, &had_props,
                               &props_mod, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  assert(kind == svn_node_file
         && (status == svn_wc__db_status_normal
             || status == svn_wc__db_status_added
             || (status == svn_wc__db_status_deleted && diff_pristine)));


  if (status == svn_wc__db_status_deleted)
    {
      assert(diff_pristine);

      SVN_ERR(svn_wc__db_read_pristine_info(&status, &kind, NULL, NULL, NULL,
                                            NULL, &checksum, NULL, &had_props,
                                            &pristine_props,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
      props_mod = FALSE;
    }
  else if (!had_props)
    pristine_props = apr_hash_make(scratch_pool);
  else
    SVN_ERR(svn_wc__db_read_pristine_props(&pristine_props,
                                           db, local_abspath,
                                           scratch_pool, scratch_pool));

  if (original_repos_relpath)
    {
      copyfrom_src = svn_diff__source_create(original_revision, scratch_pool);
      copyfrom_src->repos_relpath = original_repos_relpath;
      copyfrom_src->moved_from_relpath = moved_from_relpath;
    }

  if (props_mod || !SVN_IS_VALID_REVNUM(revision))
    right_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
  else
    {
      if (diff_pristine)
        file_mod = FALSE;
      else
        SVN_ERR(svn_wc__internal_file_modified_p(&file_mod, db, local_abspath,
                                                 FALSE, scratch_pool));

      if (!file_mod)
        right_src = svn_diff__source_create(revision, scratch_pool);
      else
        right_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
    }

  SVN_ERR(processor->file_opened(&file_baton, &skip,
                                 relpath,
                                 NULL /* left_source */,
                                 right_src,
                                 copyfrom_src,
                                 processor_parent_baton,
                                 processor,
                                 scratch_pool, scratch_pool));

  if (skip)
    return SVN_NO_ERROR;

  if (props_mod && !diff_pristine)
    SVN_ERR(svn_wc__db_read_props(&right_props, db, local_abspath,
                                  scratch_pool, scratch_pool));
  else
    right_props = svn_prop_hash_dup(pristine_props, scratch_pool);

  if (checksum)
    SVN_ERR(svn_wc__db_pristine_get_path(&pristine_file, db, local_abspath,
                                         checksum, scratch_pool, scratch_pool));
  else
    pristine_file = NULL;

  if (diff_pristine)
    {
      translated_file = pristine_file; /* No translation needed */
    }
  else
    {
      SVN_ERR(svn_wc__internal_translated_file(
           &translated_file, local_abspath, db, local_abspath,
           SVN_WC_TRANSLATE_TO_NF | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
           cancel_func, cancel_baton,
           scratch_pool, scratch_pool));
    }

  SVN_ERR(processor->file_added(relpath,
                                copyfrom_src,
                                right_src,
                                copyfrom_src
                                  ? pristine_file
                                  : NULL,
                                translated_file,
                                copyfrom_src
                                  ? pristine_props
                                  : NULL,
                                right_props,
                                file_baton,
                                processor,
                                scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__diff_local_only_dir(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *relpath,
                            svn_depth_t depth,
                            const char *moved_from_relpath,
                            const svn_diff_tree_processor_t *processor,
                            void *processor_parent_baton,
                            svn_boolean_t diff_pristine,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_boolean_t had_props;
  svn_boolean_t props_mod;
  const char *original_repos_relpath;
  svn_revnum_t original_revision;
  svn_diff_source_t *copyfrom_src = NULL;
  apr_hash_t *pristine_props;
  const apr_array_header_t *children;
  int i;
  apr_pool_t *iterpool;
  void *pdb = NULL;
  svn_boolean_t skip = FALSE;
  svn_boolean_t skip_children = FALSE;
  svn_diff_source_t *right_src = svn_diff__source_create(SVN_INVALID_REVNUM,
                                                         scratch_pool);

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               &original_repos_relpath, NULL, NULL,
                               &original_revision, NULL, NULL, NULL,
                               NULL, NULL, NULL, &had_props,
                               &props_mod, NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));
  if (original_repos_relpath)
    {
      copyfrom_src = svn_diff__source_create(original_revision, scratch_pool);
      copyfrom_src->repos_relpath = original_repos_relpath;
      copyfrom_src->moved_from_relpath = moved_from_relpath;
    }

  /* svn_wc__db_status_incomplete should never happen, as the result won't be
     stable or guaranteed related to what is in the repository for this
     revision, but without this it would be hard to diagnose that status... */
  assert(kind == svn_node_dir
         && (status == svn_wc__db_status_normal
             || status == svn_wc__db_status_incomplete
             || status == svn_wc__db_status_added
             || (status == svn_wc__db_status_deleted && diff_pristine)));

  if (status == svn_wc__db_status_deleted)
    {
      assert(diff_pristine);

      SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, &had_props,
                                            &pristine_props,
                                            db, local_abspath,
                                            scratch_pool, scratch_pool));
      props_mod = FALSE;
    }
  else if (!had_props)
    pristine_props = apr_hash_make(scratch_pool);
  else
    SVN_ERR(svn_wc__db_read_pristine_props(&pristine_props,
                                           db, local_abspath,
                                           scratch_pool, scratch_pool));

  /* Report the addition of the directory's contents. */
  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(processor->dir_opened(&pdb, &skip, &skip_children,
                                relpath,
                                NULL,
                                right_src,
                                copyfrom_src,
                                processor_parent_baton,
                                processor,
                                scratch_pool, iterpool));

  if ((depth > svn_depth_empty || depth == svn_depth_unknown)
      && ! skip_children)
    {
      svn_depth_t depth_below_here = depth;
      apr_hash_t *nodes;
      apr_hash_t *conflicts;

      if (depth_below_here == svn_depth_immediates)
        depth_below_here = svn_depth_empty;

      SVN_ERR(svn_wc__db_read_children_info(&nodes, &conflicts,
                                            db, local_abspath,
                                            FALSE /* base_tree_only */,
                                            scratch_pool, iterpool));


      children = svn_sort__hash(nodes, svn_sort_compare_items_lexically,
                                scratch_pool);

      for (i = 0; i < children->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(children, i, svn_sort__item_t);
          const char *name = item->key;
          struct svn_wc__db_info_t *info = item->value;
          const char *child_abspath;
          const char *child_relpath;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          child_abspath = svn_dirent_join(local_abspath, name, iterpool);

          if (NOT_PRESENT(info->status))
            {
              continue;
            }

          /* If comparing against WORKING, skip entries that are
             schedule-deleted - they don't really exist. */
          if (!diff_pristine && info->status == svn_wc__db_status_deleted)
            continue;

          child_relpath = svn_relpath_join(relpath, name, iterpool);

          if (info->moved_here)
            {
              const char *moved_from_abspath;
              const char *a_abspath;
              const char *a_relpath;

              a_relpath = relpath;
              a_abspath = local_abspath;
              while (*a_relpath)
                {
                  a_relpath = svn_relpath_dirname(a_relpath, iterpool);
                  a_abspath = svn_dirent_dirname(a_abspath, iterpool);
                }

              SVN_ERR(svn_wc__db_scan_moved(&moved_from_abspath,
                                            NULL, NULL, NULL,
                                            db, child_abspath,
                                            iterpool, iterpool));
              SVN_ERR_ASSERT(moved_from_abspath != NULL);

              moved_from_relpath = svn_dirent_skip_ancestor(
                                                        a_abspath,
                                                        moved_from_abspath);
            }
          else
            moved_from_relpath = NULL;

          switch (info->kind)
            {
            case svn_node_file:
            case svn_node_symlink:
              SVN_ERR(svn_wc__diff_local_only_file(db, child_abspath,
                                                   child_relpath,
                                                   moved_from_relpath,
                                                   processor, pdb,
                                                   diff_pristine,
                                                   cancel_func, cancel_baton,
                                                   scratch_pool));
              break;

            case svn_node_dir:
              if (depth > svn_depth_files || depth == svn_depth_unknown)
                {
                  SVN_ERR(svn_wc__diff_local_only_dir(db, child_abspath,
                                                      child_relpath,
                                                      depth_below_here,
                                                      moved_from_relpath,
                                                      processor, pdb,
                                                      diff_pristine,
                                                      cancel_func,
                                                      cancel_baton,
                                                      iterpool));
                }
              break;

            default:
              break;
            }
        }
    }

  if (!skip)
    {
      apr_hash_t *right_props;

      if (props_mod && !diff_pristine)
        SVN_ERR(svn_wc__db_read_props(&right_props, db, local_abspath,
                                      scratch_pool, scratch_pool));
      else
        right_props = svn_prop_hash_dup(pristine_props, scratch_pool);

      SVN_ERR(processor->dir_added(relpath,
                                   copyfrom_src,
                                   right_src,
                                   copyfrom_src
                                     ? pristine_props
                                     : NULL,
                                   right_props,
                                   pdb,
                                   processor,
                                   iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Reports local changes. */
static svn_error_t *
handle_local_only(struct dir_baton_t *pb,
                  const char *name,
                  apr_pool_t *scratch_pool)
{
  struct edit_baton_t *eb = pb->eb;
  const struct svn_wc__db_info_t *info;
  const char *child_abspath;
  const char *moved_from_relpath;
  svn_boolean_t repos_delete = (pb->deletes
                                && svn_hash_gets(pb->deletes, name));

  assert(!strchr(name, '/'));
  assert(!pb->added || eb->ignore_ancestry);

  if (pb->skip_children)
    return SVN_NO_ERROR;

  SVN_ERR(ensure_local_info(pb, scratch_pool));

  info = svn_hash_gets(pb->local_info, name);

  if (info == NULL || NOT_PRESENT(info->status))
    return SVN_NO_ERROR;

  switch (info->status)
    {
      case svn_wc__db_status_normal:
      case svn_wc__db_status_incomplete:
        if (!repos_delete)
          return SVN_NO_ERROR; /* Local and remote */
        svn_hash_sets(pb->deletes, name, NULL);
        break;

      case svn_wc__db_status_deleted:
        if (!(eb->diff_pristine && repos_delete))
          return SVN_NO_ERROR;
        break;

      case svn_wc__db_status_added:
      default:
        break;
    }

  child_abspath = svn_dirent_join(pb->local_abspath, name, scratch_pool);

  if (info->moved_here)
    {
      const char *moved_from_abspath;

      SVN_ERR(svn_wc__db_scan_moved(&moved_from_abspath,
                                    NULL, NULL, NULL,
                                    eb->db, child_abspath,
                                    scratch_pool, scratch_pool));
      SVN_ERR_ASSERT(moved_from_abspath != NULL);

      moved_from_relpath = svn_dirent_skip_ancestor(
                                    eb->anchor_abspath,
                                    moved_from_abspath);
    }
  else
    moved_from_relpath = NULL;

  if (info->kind == svn_node_dir)
    {
      svn_depth_t depth ;

      if (pb->depth == svn_depth_infinity || pb->depth == svn_depth_unknown)
        depth = pb->depth;
      else
        depth = svn_depth_empty;

      SVN_ERR(svn_wc__diff_local_only_dir(
                      eb->db,
                      child_abspath,
                      svn_relpath_join(pb->relpath, name, scratch_pool),
                      repos_delete ? svn_depth_infinity : depth,
                      moved_from_relpath,
                      eb->processor, pb->pdb,
                      eb->diff_pristine,
                      eb->cancel_func, eb->cancel_baton,
                      scratch_pool));
    }
  else
    SVN_ERR(svn_wc__diff_local_only_file(
                      eb->db,
                      child_abspath,
                      svn_relpath_join(pb->relpath, name, scratch_pool),
                      moved_from_relpath,
                      eb->processor, pb->pdb,
                      eb->diff_pristine,
                      eb->cancel_func, eb->cancel_baton,
                      scratch_pool));

  return SVN_NO_ERROR;
}

/* Reports a file LOCAL_ABSPATH in BASE as deleted */
svn_error_t *
svn_wc__diff_base_only_file(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *relpath,
                            svn_revnum_t revision,
                            const svn_diff_tree_processor_t *processor,
                            void *processor_parent_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const svn_checksum_t *checksum;
  apr_hash_t *props;
  void *file_baton = NULL;
  svn_boolean_t skip = FALSE;
  svn_diff_source_t *left_src;
  const char *pristine_file;

  SVN_ERR(svn_wc__db_base_get_info(&status, &kind,
                                   SVN_IS_VALID_REVNUM(revision)
                                        ? NULL : &revision,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   &checksum, NULL, NULL, NULL, &props, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(status == svn_wc__db_status_normal
                 && kind == svn_node_file
                 && checksum);

  left_src = svn_diff__source_create(revision, scratch_pool);

  SVN_ERR(processor->file_opened(&file_baton, &skip,
                                 relpath,
                                 left_src,
                                 NULL /* right_src */,
                                 NULL /* copyfrom_source */,
                                 processor_parent_baton,
                                 processor,
                                 scratch_pool, scratch_pool));

  if (skip)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_pristine_get_path(&pristine_file,
                                       db, local_abspath, checksum,
                                       scratch_pool, scratch_pool));

  SVN_ERR(processor->file_deleted(relpath,
                                  left_src,
                                  pristine_file,
                                  props,
                                  file_baton,
                                  processor,
                                  scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__diff_base_only_dir(svn_wc__db_t *db,
                           const char *local_abspath,
                           const char *relpath,
                           svn_revnum_t revision,
                           svn_depth_t depth,
                           const svn_diff_tree_processor_t *processor,
                           void *processor_parent_baton,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool)
{
  void *dir_baton = NULL;
  svn_boolean_t skip = FALSE;
  svn_boolean_t skip_children = FALSE;
  svn_diff_source_t *left_src;
  svn_revnum_t report_rev = revision;

  if (!SVN_IS_VALID_REVNUM(report_rev))
    SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &report_rev, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL,
                                     db, local_abspath,
                                     scratch_pool, scratch_pool));

  left_src = svn_diff__source_create(report_rev, scratch_pool);

  SVN_ERR(processor->dir_opened(&dir_baton, &skip, &skip_children,
                                relpath,
                                left_src,
                                NULL /* right_src */,
                                NULL /* copyfrom_src */,
                                processor_parent_baton,
                                processor,
                                scratch_pool, scratch_pool));

  if (!skip_children && (depth == svn_depth_unknown || depth > svn_depth_empty))
    {
      apr_hash_t *nodes;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_array_header_t *children;
      int i;

      SVN_ERR(svn_wc__db_base_get_children_info(&nodes, db, local_abspath,
                                                scratch_pool, iterpool));

      children = svn_sort__hash(nodes, svn_sort_compare_items_lexically,
                                scratch_pool);

      for (i = 0; i < children->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(children, i,
                                                  svn_sort__item_t);
          const char *name = item->key;
          struct svn_wc__db_base_info_t *info = item->value;
          const char *child_abspath;
          const char *child_relpath;

          if (info->status != svn_wc__db_status_normal)
            continue;

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          svn_pool_clear(iterpool);

          child_abspath = svn_dirent_join(local_abspath, name, iterpool);
          child_relpath = svn_relpath_join(relpath, name, iterpool);

          switch (info->kind)
            {
              case svn_node_file:
              case svn_node_symlink:
                SVN_ERR(svn_wc__diff_base_only_file(db, child_abspath,
                                                    child_relpath,
                                                    revision,
                                                    processor, dir_baton,
                                                    iterpool));
                break;
              case svn_node_dir:
                if (depth > svn_depth_files || depth == svn_depth_unknown)
                  {
                    svn_depth_t depth_below_here = depth;

                    if (depth_below_here == svn_depth_immediates)
                      depth_below_here = svn_depth_empty;

                    SVN_ERR(svn_wc__diff_base_only_dir(db, child_abspath,
                                                       child_relpath,
                                                       revision,
                                                       depth_below_here,
                                                       processor, dir_baton,
                                                       cancel_func,
                                                       cancel_baton,
                                                       iterpool));
                  }
                break;

              default:
                break;
            }
        }
    }

  if (!skip)
    {
      apr_hash_t *props;
      SVN_ERR(svn_wc__db_base_get_props(&props, db, local_abspath,
                                        scratch_pool, scratch_pool));

      SVN_ERR(processor->dir_deleted(relpath,
                                     left_src,
                                     props,
                                     dir_baton,
                                     processor,
                                     scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton_t *eb = edit_baton;
  eb->revnum = target_revision;

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. The root of the comparison hierarchy */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  struct edit_baton_t *eb = edit_baton;
  struct dir_baton_t *db;

  eb->root_opened = TRUE;
  db = make_dir_baton("", NULL, eb, FALSE, eb->depth, dir_pool);
  *root_baton = db;

  if (eb->target[0] == '\0')
    {
      db->left_src = svn_diff__source_create(eb->revnum, db->pool);
      db->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, db->pool);

      SVN_ERR(eb->processor->dir_opened(&db->pdb, &db->skip,
                                        &db->skip_children,
                                        "",
                                        db->left_src,
                                        db->right_src,
                                        NULL /* copyfrom_source */,
                                        NULL /* parent_baton */,
                                        eb->processor,
                                        db->pool, db->pool));
    }
  else
    db->skip = TRUE; /* Skip this, but not the children */

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton_t *pb = parent_baton;
  const char *name = svn_dirent_basename(path, pb->pool);

  if (!pb->deletes)
    pb->deletes = apr_hash_make(pb->pool);

  svn_hash_sets(pb->deletes, name, "");
  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  struct dir_baton_t *db;
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  db = make_dir_baton(path, pb, pb->eb, TRUE, subdir_depth,
                      dir_pool);
  *child_baton = db;

  if (pb->repos_only || !eb->ignore_ancestry)
    db->repos_only = TRUE;
  else
    {
      struct svn_wc__db_info_t *info;
      SVN_ERR(ensure_local_info(pb, dir_pool));

      info = svn_hash_gets(pb->local_info, db->name);

      if (!info || info->kind != svn_node_dir || NOT_PRESENT(info->status))
        db->repos_only = TRUE;

      if (!db->repos_only && info->status != svn_wc__db_status_added)
        db->repos_only = TRUE;

      if (!db->repos_only)
        {
          db->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, db->pool);
          db->ignoring_ancestry = TRUE;

          svn_hash_sets(pb->compared, apr_pstrdup(pb->pool, db->name), "");
        }
    }

  db->left_src = svn_diff__source_create(eb->revnum, db->pool);

  if (eb->local_before_remote && !db->repos_only && !db->ignoring_ancestry)
    SVN_ERR(handle_local_only(pb, db->name, dir_pool));

  SVN_ERR(eb->processor->dir_opened(&db->pdb, &db->skip, &db->skip_children,
                                    db->relpath,
                                    db->left_src,
                                    db->right_src,
                                    NULL /* copyfrom src */,
                                    pb->pdb,
                                    eb->processor,
                                    db->pool, db->pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  struct dir_baton_t *db;
  svn_depth_t subdir_depth = (pb->depth == svn_depth_immediates)
                              ? svn_depth_empty : pb->depth;

  /* Allocate path from the parent pool since the memory is used in the
     parent's compared hash */
  db = make_dir_baton(path, pb, pb->eb, FALSE, subdir_depth, dir_pool);
  *child_baton = db;

  if (pb->repos_only)
    db->repos_only = TRUE;
  else
    {
      struct svn_wc__db_info_t *info;
      SVN_ERR(ensure_local_info(pb, dir_pool));

      info = svn_hash_gets(pb->local_info, db->name);

      if (!info || info->kind != svn_node_dir || NOT_PRESENT(info->status))
        db->repos_only = TRUE;

      if (!db->repos_only)
        {
          switch (info->status)
            {
              case svn_wc__db_status_normal:
              case svn_wc__db_status_incomplete:
                db->is_incomplete = (info->status ==
                                     svn_wc__db_status_incomplete);
                break;
              case svn_wc__db_status_deleted:
                db->repos_only = TRUE;

                if (!info->have_more_work)
                  svn_hash_sets(pb->compared,
                                apr_pstrdup(pb->pool, db->name), "");
                break;
              case svn_wc__db_status_added:
                if (eb->ignore_ancestry)
                  db->ignoring_ancestry = TRUE;
                else
                  db->repos_only = TRUE;
                break;
              default:
                SVN_ERR_MALFUNCTION();
          }

          if (info->status == svn_wc__db_status_added
              || info->status == svn_wc__db_status_deleted)
            {
              svn_wc__db_status_t base_status;

              /* Node is shadowed; check BASE */
              SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               eb->db, db->local_abspath,
                                               dir_pool, dir_pool));

              if (base_status == svn_wc__db_status_incomplete)
                db->is_incomplete = TRUE;
            }
        }

      if (!db->repos_only)
        {
          db->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, db->pool);
          svn_hash_sets(pb->compared, apr_pstrdup(pb->pool, db->name), "");
        }
    }

  db->left_src = svn_diff__source_create(eb->revnum, db->pool);

  if (eb->local_before_remote && !db->repos_only && !db->ignoring_ancestry)
    SVN_ERR(handle_local_only(pb, db->name, dir_pool));

  SVN_ERR(eb->processor->dir_opened(&db->pdb, &db->skip, &db->skip_children,
                                    db->relpath,
                                    db->left_src,
                                    db->right_src,
                                    NULL /* copyfrom src */,
                                    pb->pdb,
                                    eb->processor,
                                    db->pool, db->pool));

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function.  When a directory is closed, all the
 * directory elements that have been added or replaced will already have been
 * diff'd. However there may be other elements in the working copy
 * that have not yet been considered.  */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton_t *db = dir_baton;
  struct dir_baton_t *pb = db->parent_baton;
  struct edit_baton_t *eb = db->eb;
  apr_pool_t *scratch_pool = db->pool;
  svn_boolean_t reported_closed = FALSE;

  if (!db->skip_children && db->deletes && apr_hash_count(db->deletes))
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_array_header_t *children;
      int i;
      children = svn_sort__hash(db->deletes, svn_sort_compare_items_lexically,
                                scratch_pool);

      for (i = 0; i < children->nelts; i++)
        {
          svn_sort__item_t *item = &APR_ARRAY_IDX(children, i,
                                                  svn_sort__item_t);
          const char *name = item->key;

          svn_pool_clear(iterpool);
          SVN_ERR(handle_local_only(db, name, iterpool));

          svn_hash_sets(db->compared, name, "");
        }

      svn_pool_destroy(iterpool);
    }

  /* Report local modifications for this directory.  Skip added
     directories since they can only contain added elements, all of
     which have already been diff'd. */
  if (!db->repos_only && !db->skip_children)
  {
    SVN_ERR(walk_local_nodes_diff(eb,
                                  db->local_abspath,
                                  db->relpath,
                                  db->depth,
                                  db->compared,
                                  db->pdb,
                                  scratch_pool));
  }

  /* Report the property changes on the directory itself, if necessary. */
  if (db->skip)
    {
      /* Diff processor requested no directory details */
    }
  else if (db->propchanges->nelts > 0 || db->repos_only)
    {
      apr_hash_t *repos_props;

      if (db->added || db->is_incomplete)
        {
          repos_props = apr_hash_make(scratch_pool);
        }
      else
        {
          SVN_ERR(svn_wc__db_base_get_props(&repos_props,
                                            eb->db, db->local_abspath,
                                            scratch_pool, scratch_pool));
        }

      /* Add received property changes and entry props */
      if (db->propchanges->nelts)
        repos_props = svn_prop__patch(repos_props, db->propchanges,
                                      scratch_pool);

      if (db->repos_only)
        {
          SVN_ERR(eb->processor->dir_deleted(db->relpath,
                                             db->left_src,
                                             repos_props,
                                             db->pdb,
                                             eb->processor,
                                             scratch_pool));
          reported_closed = TRUE;
        }
      else
        {
          apr_hash_t *local_props;
          apr_array_header_t *prop_changes;

          if (eb->diff_pristine)
            SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, NULL, NULL, NULL,
                                                  NULL, NULL, NULL, NULL,
                                                  &local_props,
                                                  eb->db, db->local_abspath,
                                                  scratch_pool, scratch_pool));
          else
            SVN_ERR(svn_wc__db_read_props(&local_props,
                                          eb->db, db->local_abspath,
                                          scratch_pool, scratch_pool));

          SVN_ERR(svn_prop_diffs(&prop_changes, local_props, repos_props,
                                 scratch_pool));

          /* ### as a good diff processor we should now only report changes
                 if there are non-entry changes, but for now we stick to
                 compatibility */

          if (prop_changes->nelts)
            {
              SVN_ERR(eb->processor->dir_changed(db->relpath,
                                                 db->left_src,
                                                 db->right_src,
                                                 repos_props,
                                                 local_props,
                                                 prop_changes,
                                                 db->pdb,
                                                 eb->processor,
                                                 scratch_pool));
              reported_closed = TRUE;
          }
        }
    }

  /* Mark this directory as compared in the parent directory's baton,
     unless this is the root of the comparison. */
  if (!reported_closed && !db->skip)
    SVN_ERR(eb->processor->dir_closed(db->relpath,
                                      db->left_src,
                                      db->right_src,
                                      db->pdb,
                                      eb->processor,
                                      scratch_pool));

  if (pb && !eb->local_before_remote && !db->repos_only && !db->ignoring_ancestry)
    SVN_ERR(handle_local_only(pb, db->name, scratch_pool));

  SVN_ERR(maybe_done(db)); /* destroys scratch_pool */

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  struct file_baton_t *fb;

  fb = make_file_baton(path, TRUE, pb, file_pool);
  *file_baton = fb;

  if (pb->skip_children)
    {
      fb->skip = TRUE;
      return SVN_NO_ERROR;
    }
  else if (pb->repos_only || !eb->ignore_ancestry)
    fb->repos_only = TRUE;
  else
    {
      struct svn_wc__db_info_t *info;
      SVN_ERR(ensure_local_info(pb, file_pool));

      info = svn_hash_gets(pb->local_info, fb->name);

      if (!info || info->kind != svn_node_file || NOT_PRESENT(info->status))
        fb->repos_only = TRUE;

      if (!fb->repos_only && info->status != svn_wc__db_status_added)
        fb->repos_only = TRUE;

      if (!fb->repos_only)
        {
          /* Add this path to the parent directory's list of elements that
             have been compared. */
          fb->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, fb->pool);
          fb->ignoring_ancestry = TRUE;

          svn_hash_sets(pb->compared, apr_pstrdup(pb->pool, fb->name), "");
        }
    }

  fb->left_src = svn_diff__source_create(eb->revnum, fb->pool);

  SVN_ERR(eb->processor->file_opened(&fb->pfb, &fb->skip,
                                     fb->relpath,
                                     fb->left_src,
                                     fb->right_src,
                                     NULL /* copyfrom src */,
                                     pb->pdb,
                                     eb->processor,
                                     fb->pool, fb->pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  struct file_baton_t *fb;

  fb = make_file_baton(path, FALSE, pb, file_pool);
  *file_baton = fb;

  if (pb->skip_children)
    fb->skip = TRUE;
  else if (pb->repos_only)
    fb->repos_only = TRUE;
  else
    {
      struct svn_wc__db_info_t *info;
      SVN_ERR(ensure_local_info(pb, file_pool));

      info = svn_hash_gets(pb->local_info, fb->name);

      if (!info || info->kind != svn_node_file || NOT_PRESENT(info->status))
        fb->repos_only = TRUE;

      if (!fb->repos_only)
        switch (info->status)
          {
            case svn_wc__db_status_normal:
            case svn_wc__db_status_incomplete:
              break;
            case svn_wc__db_status_deleted:
              fb->repos_only = TRUE;
              if (!info->have_more_work)
                svn_hash_sets(pb->compared,
                              apr_pstrdup(pb->pool, fb->name), "");
              break;
            case svn_wc__db_status_added:
              if (eb->ignore_ancestry)
                fb->ignoring_ancestry = TRUE;
              else
                fb->repos_only = TRUE;
              break;
            default:
              SVN_ERR_MALFUNCTION();
        }

      if (!fb->repos_only)
        {
          /* Add this path to the parent directory's list of elements that
             have been compared. */
          fb->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, fb->pool);
          svn_hash_sets(pb->compared, apr_pstrdup(pb->pool, fb->name), "");
        }
    }

  fb->left_src = svn_diff__source_create(eb->revnum, fb->pool);

  SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, &fb->base_checksum, NULL,
                                   NULL, NULL, &fb->base_props, NULL,
                                   eb->db, fb->local_abspath,
                                   fb->pool, fb->pool));

  SVN_ERR(eb->processor->file_opened(&fb->pfb, &fb->skip,
                                     fb->relpath,
                                     fb->left_src,
                                     fb->right_src,
                                     NULL /* copyfrom src */,
                                     pb->pdb,
                                     eb->processor,
                                     fb->pool, fb->pool));

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function. */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum_hex,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton_t *fb = file_baton;
  struct edit_baton_t *eb = fb->eb;
  svn_stream_t *source;
  svn_stream_t *temp_stream;
  svn_checksum_t *repos_checksum = NULL;

  if (fb->skip)
    {
      *handler = svn_delta_noop_window_handler;
      *handler_baton = NULL;
      return SVN_NO_ERROR;
    }

  if (base_checksum_hex && fb->base_checksum)
    {
      const svn_checksum_t *base_md5;
      SVN_ERR(svn_checksum_parse_hex(&repos_checksum, svn_checksum_md5,
                                     base_checksum_hex, pool));

      SVN_ERR(svn_wc__db_pristine_get_md5(&base_md5,
                                          eb->db, eb->anchor_abspath,
                                          fb->base_checksum,
                                          pool, pool));

      if (! svn_checksum_match(repos_checksum, base_md5))
        {
          /* ### I expect that there are some bad drivers out there
             ### that used to give bad results. We could look in
             ### working to see if the expected checksum matches and
             ### then return the pristine of that... But that only moves
             ### the problem */

          /* If needed: compare checksum obtained via md5 of working.
             And if they match set fb->base_checksum and fb->base_props */

          return svn_checksum_mismatch_err(
                        base_md5,
                        repos_checksum,
                        pool,
                        _("Checksum mismatch for '%s'"),
                        svn_dirent_local_style(fb->local_abspath,
                                               pool));
        }

      SVN_ERR(svn_wc__db_pristine_read(&source, NULL,
                                       eb->db, fb->local_abspath,
                                       fb->base_checksum,
                                       pool, pool));
    }
  else if (fb->base_checksum)
    {
      SVN_ERR(svn_wc__db_pristine_read(&source, NULL,
                                       eb->db, fb->local_abspath,
                                       fb->base_checksum,
                                       pool, pool));
    }
  else
    source = svn_stream_empty(pool);

  /* This is the file that will contain the pristine repository version. */
  SVN_ERR(svn_stream_open_unique(&temp_stream, &fb->temp_file_path, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 fb->pool, fb->pool));

  svn_txdelta_apply(source, temp_stream,
                    fb->result_digest,
                    fb->local_abspath /* error_info */,
                    fb->pool,
                    handler, handler_baton);

  return SVN_NO_ERROR;
}

/* An svn_delta_editor_t function.  When the file is closed we have a temporary
 * file containing a pristine version of the repository file. This can
 * be compared against the working copy.
 *
 * Ignore TEXT_CHECKSUM.
 */
static svn_error_t *
close_file(void *file_baton,
           const char *expected_md5_digest,
           apr_pool_t *pool)
{
  struct file_baton_t *fb = file_baton;
  struct dir_baton_t *pb = fb->parent_baton;
  struct edit_baton_t *eb = fb->eb;
  apr_pool_t *scratch_pool = fb->pool;

  /* The repository information; constructed from BASE + Changes */
  const char *repos_file;
  apr_hash_t *repos_props;

  if (fb->skip)
    {
      svn_pool_destroy(fb->pool); /* destroys scratch_pool and fb */
      SVN_ERR(maybe_done(pb));
      return SVN_NO_ERROR;
    }

  if (expected_md5_digest != NULL)
    {
      svn_checksum_t *expected_checksum;
      const svn_checksum_t *result_checksum;

      if (fb->temp_file_path)
        result_checksum = svn_checksum__from_digest_md5(fb->result_digest,
                                                        scratch_pool);
      else
        result_checksum = fb->base_checksum;

      SVN_ERR(svn_checksum_parse_hex(&expected_checksum, svn_checksum_md5,
                                     expected_md5_digest, scratch_pool));

      if (result_checksum->kind != svn_checksum_md5)
        SVN_ERR(svn_wc__db_pristine_get_md5(&result_checksum,
                                            eb->db, fb->local_abspath,
                                            result_checksum,
                                            scratch_pool, scratch_pool));

      if (!svn_checksum_match(expected_checksum, result_checksum))
        return svn_checksum_mismatch_err(
                            expected_checksum,
                            result_checksum,
                            pool,
                            _("Checksum mismatch for '%s'"),
                            svn_dirent_local_style(fb->local_abspath,
                                                   scratch_pool));
    }

  if (eb->local_before_remote && !fb->repos_only && !fb->ignoring_ancestry)
    SVN_ERR(handle_local_only(pb, fb->name, scratch_pool));

  {
    apr_hash_t *prop_base;

    if (fb->added)
      prop_base = apr_hash_make(scratch_pool);
    else
      prop_base = fb->base_props;

    /* includes entry props */
    repos_props = svn_prop__patch(prop_base, fb->propchanges, scratch_pool);

    repos_file = fb->temp_file_path;
    if (! repos_file)
      {
        assert(fb->base_checksum);
        SVN_ERR(svn_wc__db_pristine_get_path(&repos_file,
                                             eb->db, eb->anchor_abspath,
                                             fb->base_checksum,
                                             scratch_pool, scratch_pool));
      }
  }

  if (fb->repos_only)
    {
      SVN_ERR(eb->processor->file_deleted(fb->relpath,
                                          fb->left_src,
                                          fb->temp_file_path,
                                          repos_props,
                                          fb->pfb,
                                          eb->processor,
                                          scratch_pool));
    }
  else
    {
      /* Produce a diff of actual or pristine against repos */
      apr_hash_t *local_props;
      apr_array_header_t *prop_changes;
      const char *localfile;

      /* pb->local_info contains some information that might allow optimizing
         this a bit */

      if (eb->diff_pristine)
        {
          const svn_checksum_t *checksum;
          SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, NULL, NULL, NULL,
                                                NULL, &checksum, NULL, NULL,
                                                &local_props,
                                                eb->db, fb->local_abspath,
                                                scratch_pool, scratch_pool));
          assert(checksum);
          SVN_ERR(svn_wc__db_pristine_get_path(&localfile,
                                               eb->db, eb->anchor_abspath,
                                               checksum,
                                               scratch_pool, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_wc__db_read_props(&local_props,
                                        eb->db, fb->local_abspath,
                                        scratch_pool, scratch_pool));

          /* a detranslated version of the working file */
          SVN_ERR(svn_wc__internal_translated_file(
                    &localfile, fb->local_abspath, eb->db, fb->local_abspath,
                    SVN_WC_TRANSLATE_TO_NF | SVN_WC_TRANSLATE_USE_GLOBAL_TMP,
                    eb->cancel_func, eb->cancel_baton,
                    scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_prop_diffs(&prop_changes, local_props, repos_props,
                             scratch_pool));


      /* ### as a good diff processor we should now only report changes, and
             report file_closed() in other cases */
      SVN_ERR(eb->processor->file_changed(fb->relpath,
                                          fb->left_src,
                                          fb->right_src,
                                          repos_file /* left file */,
                                          localfile /* right file */,
                                          repos_props /* left_props */,
                                          local_props /* right props */,
                                          TRUE /* ### file_modified */,
                                          prop_changes,
                                          fb->pfb,
                                          eb->processor,
                                          scratch_pool));
    }

  if (!eb->local_before_remote && !fb->repos_only && !fb->ignoring_ancestry)
    SVN_ERR(handle_local_only(pb, fb->name, scratch_pool));

  svn_pool_destroy(fb->pool); /* destroys scratch_pool and fb */
  SVN_ERR(maybe_done(pb));
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton_t *fb = file_baton;
  svn_prop_t *propchange;
  svn_prop_kind_t propkind;

  propkind = svn_property_kind2(name);
  if (propkind == svn_prop_wc_kind)
    return SVN_NO_ERROR;
  else if (propkind == svn_prop_regular_kind)
    fb->has_propchange = TRUE;

  propchange = apr_array_push(fb->propchanges);
  propchange->name = apr_pstrdup(fb->pool, name);
  propchange->value = svn_string_dup(value, fb->pool);

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton_t *db = dir_baton;
  svn_prop_t *propchange;
  svn_prop_kind_t propkind;

  propkind = svn_property_kind2(name);
  if (propkind == svn_prop_wc_kind)
    return SVN_NO_ERROR;
  else if (propkind == svn_prop_regular_kind)
    db->has_propchange = TRUE;

  propchange = apr_array_push(db->propchanges);
  propchange->name = apr_pstrdup(db->pool, name);
  propchange->value = svn_string_dup(value, db->pool);

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton_t *eb = edit_baton;

  if (!eb->root_opened)
    {
      SVN_ERR(walk_local_nodes_diff(eb,
                                    eb->anchor_abspath,
                                    "",
                                    eb->depth,
                                    NULL /* compared */,
                                    NULL /* No parent_baton */,
                                    eb->pool));
    }

  return SVN_NO_ERROR;
}

/* Public Interface */


/* Create a diff editor and baton. */
svn_error_t *
svn_wc__get_diff_editor(const svn_delta_editor_t **editor,
                        void **edit_baton,
                        svn_wc_context_t *wc_ctx,
                        const char *anchor_abspath,
                        const char *target,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_boolean_t server_performs_filtering,
                        const apr_array_header_t *changelist_filter,
                        const svn_diff_tree_processor_t *diff_processor,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  struct edit_baton_t *eb;
  void *inner_baton;
  svn_delta_editor_t *tree_editor;
  const svn_delta_editor_t *inner_editor;
  struct svn_wc__shim_fetch_baton_t *sfb;
  svn_delta_shim_callbacks_t *shim_callbacks =
                                svn_delta_shim_callbacks_default(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(anchor_abspath));

  /* Apply changelist filtering to the output */
  if (changelist_filter && changelist_filter->nelts)
    {
      apr_hash_t *changelist_hash;

      SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                         result_pool));
      diff_processor = svn_wc__changelist_filter_tree_processor_create(
                         diff_processor, wc_ctx, anchor_abspath,
                         changelist_hash, result_pool);
    }

  SVN_ERR(make_edit_baton(&eb,
                          wc_ctx->db,
                          anchor_abspath, target,
                          diff_processor,
                          depth, ignore_ancestry,
                          use_text_base, reverse_order,
                          cancel_func, cancel_baton,
                          result_pool));

  tree_editor = svn_delta_default_editor(eb->pool);

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  inner_editor = tree_editor;
  inner_baton = eb;

  if (!server_performs_filtering
      && depth == svn_depth_unknown)
    SVN_ERR(svn_wc__ambient_depth_filter_editor(&inner_editor,
                                                &inner_baton,
                                                wc_ctx->db,
                                                anchor_abspath,
                                                target,
                                                inner_editor,
                                                inner_baton,
                                                result_pool));

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func,
                                            cancel_baton,
                                            inner_editor,
                                            inner_baton,
                                            editor,
                                            edit_baton,
                                            result_pool));

  sfb = apr_palloc(result_pool, sizeof(*sfb));
  sfb->db = wc_ctx->db;
  sfb->base_abspath = eb->anchor_abspath;
  sfb->fetch_base = TRUE;

  shim_callbacks->fetch_kind_func = svn_wc__fetch_kind_func;
  shim_callbacks->fetch_props_func = svn_wc__fetch_props_func;
  shim_callbacks->fetch_base_func = svn_wc__fetch_base_func;
  shim_callbacks->fetch_baton = sfb;


  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   NULL, NULL, shim_callbacks,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Wrapping svn_wc_diff_callbacks4_t as svn_diff_tree_processor_t */

/* baton for the svn_diff_tree_processor_t wrapper */
typedef struct wc_diff_wrap_baton_t
{
  const svn_wc_diff_callbacks4_t *callbacks;
  void *callback_baton;

  svn_boolean_t walk_deleted_dirs;

  apr_pool_t *result_pool;
  const char *empty_file;

} wc_diff_wrap_baton_t;

static svn_error_t *
wrap_ensure_empty_file(wc_diff_wrap_baton_t *wb,
                       apr_pool_t *scratch_pool)
{
  if (wb->empty_file)
    return SVN_NO_ERROR;

  /* Create a unique file in the tempdir */
  SVN_ERR(svn_io_open_unique_file3(NULL, &wb->empty_file, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   wb->result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_dir_opened(void **new_dir_baton,
                svn_boolean_t *skip,
                svn_boolean_t *skip_children,
                const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                const svn_diff_source_t *copyfrom_source,
                void *parent_dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;

  assert(left_source || right_source);      /* Must exist at one point. */
  assert(!left_source || !copyfrom_source); /* Either existed or added. */

  /* Maybe store state and tree_conflicted in baton? */
  if (left_source != NULL)
    {
      /* Open for change or delete */
      SVN_ERR(wb->callbacks->dir_opened(&tree_conflicted, skip, skip_children,
                                        relpath,
                                        right_source
                                            ? right_source->revision
                                            : (left_source
                                                    ? left_source->revision
                                                    : SVN_INVALID_REVNUM),
                                        wb->callback_baton,
                                        scratch_pool));

      if (! right_source && !wb->walk_deleted_dirs)
        *skip_children = TRUE;
    }
  else /* left_source == NULL -> Add */
    {
      svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
      SVN_ERR(wb->callbacks->dir_added(&state, &tree_conflicted,
                                       skip, skip_children,
                                       relpath,
                                       right_source->revision,
                                       copyfrom_source
                                            ? copyfrom_source->repos_relpath
                                            : NULL,
                                       copyfrom_source
                                            ? copyfrom_source->revision
                                            : SVN_INVALID_REVNUM,
                                       wb->callback_baton,
                                       scratch_pool));
    }

  *new_dir_baton = NULL;

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t state = svn_wc_notify_state_unknown;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_unknown;
  apr_hash_t *pristine_props = copyfrom_props;
  apr_array_header_t *prop_changes = NULL;

  if (right_props && apr_hash_count(right_props))
    {
      if (!pristine_props)
        pristine_props = apr_hash_make(scratch_pool);

      SVN_ERR(svn_prop_diffs(&prop_changes, right_props, pristine_props,
                             scratch_pool));

      SVN_ERR(wb->callbacks->dir_props_changed(&prop_state,
                                               &tree_conflicted,
                                               relpath,
                                               TRUE /* dir_was_added */,
                                               prop_changes, pristine_props,
                                               wb->callback_baton,
                                               scratch_pool));
    }

  SVN_ERR(wb->callbacks->dir_closed(&state, &prop_state,
                                   &tree_conflicted,
                                   relpath,
                                   TRUE /* dir_was_added */,
                                   wb->callback_baton,
                                   scratch_pool));
  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;

  SVN_ERR(wb->callbacks->dir_deleted(&state, &tree_conflicted,
                                     relpath,
                                     wb->callback_baton,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_dir_closed(const char *relpath,
                const svn_diff_source_t *left_source,
                const svn_diff_source_t *right_source,
                void *dir_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;

  /* No previous implementations provided these arguments, so we
     are not providing them either */
  SVN_ERR(wb->callbacks->dir_closed(NULL, NULL, NULL,
                                    relpath,
                                    FALSE /* added */,
                                    wb->callback_baton,
                                    scratch_pool));

return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_inapplicable;

  assert(left_source && right_source);

  SVN_ERR(wb->callbacks->dir_props_changed(&prop_state, &tree_conflicted,
                                           relpath,
                                           FALSE /* dir_was_added */,
                                           prop_changes,
                                           left_props,
                                           wb->callback_baton,
                                           scratch_pool));

  /* And call dir_closed, etc */
  SVN_ERR(wrap_dir_closed(relpath, left_source, right_source,
                          dir_baton, processor,
                          scratch_pool));
  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_file_opened(void **new_file_baton,
                 svn_boolean_t *skip,
                 const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 const svn_diff_source_t *copyfrom_source,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;

  if (left_source) /* If ! added */
    SVN_ERR(wb->callbacks->file_opened(&tree_conflicted, skip, relpath,
                                       right_source
                                            ? right_source->revision
                                            : (left_source
                                                    ? left_source->revision
                                                    : SVN_INVALID_REVNUM),
                                       wb->callback_baton, scratch_pool));

  /* No old implementation used the output arguments for notify */

  *new_file_baton = NULL;
  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_inapplicable;
  apr_array_header_t *prop_changes;

  if (! copyfrom_props)
    copyfrom_props = apr_hash_make(scratch_pool);

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, copyfrom_props,
                         scratch_pool));

  if (! copyfrom_source)
    SVN_ERR(wrap_ensure_empty_file(wb, scratch_pool));

  SVN_ERR(wb->callbacks->file_added(&state, &prop_state, &tree_conflicted,
                                    relpath,
                                    copyfrom_source
                                        ? copyfrom_file
                                        : wb->empty_file,
                                    right_file,
                                    0,
                                    right_source->revision,
                                    copyfrom_props
                                     ? svn_prop_get_value(copyfrom_props,
                                                          SVN_PROP_MIME_TYPE)
                                     : NULL,
                                    right_props
                                     ? svn_prop_get_value(right_props,
                                                          SVN_PROP_MIME_TYPE)
                                     : NULL,
                                    copyfrom_source
                                            ? copyfrom_source->repos_relpath
                                            : NULL,
                                    copyfrom_source
                                            ? copyfrom_source->revision
                                            : SVN_INVALID_REVNUM,
                                    prop_changes, copyfrom_props,
                                    wb->callback_baton,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
wrap_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  apr_hash_t *left_props,
                  void *file_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;

  SVN_ERR(wrap_ensure_empty_file(wb, scratch_pool));

  SVN_ERR(wb->callbacks->file_deleted(&state, &tree_conflicted,
                                      relpath,
                                      left_file, wb->empty_file,
                                      left_props
                                       ? svn_prop_get_value(left_props,
                                                            SVN_PROP_MIME_TYPE)
                                       : NULL,
                                      NULL,
                                      left_props,
                                      wb->callback_baton,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

/* svn_diff_tree_processor_t function */
static svn_error_t *
wrap_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wb = processor->baton;
  svn_boolean_t tree_conflicted = FALSE;
  svn_wc_notify_state_t state = svn_wc_notify_state_inapplicable;
  svn_wc_notify_state_t prop_state = svn_wc_notify_state_inapplicable;

  SVN_ERR(wrap_ensure_empty_file(wb, scratch_pool));

  assert(left_source && right_source);

  SVN_ERR(wb->callbacks->file_changed(&state, &prop_state, &tree_conflicted,
                                      relpath,
                                      file_modified ? left_file : NULL,
                                      file_modified ? right_file : NULL,
                                      left_source->revision,
                                      right_source->revision,
                                      left_props
                                       ? svn_prop_get_value(left_props,
                                                            SVN_PROP_MIME_TYPE)
                                       : NULL,
                                      right_props
                                       ? svn_prop_get_value(right_props,
                                                            SVN_PROP_MIME_TYPE)
                                       : NULL,
                                       prop_changes,
                                      left_props,
                                      wb->callback_baton,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__wrap_diff_callbacks(const svn_diff_tree_processor_t **diff_processor,
                            const svn_wc_diff_callbacks4_t *callbacks,
                            void *callback_baton,
                            svn_boolean_t walk_deleted_dirs,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  wc_diff_wrap_baton_t *wrap_baton;
  svn_diff_tree_processor_t *processor;

  wrap_baton = apr_pcalloc(result_pool, sizeof(*wrap_baton));

  wrap_baton->result_pool = result_pool;
  wrap_baton->callbacks = callbacks;
  wrap_baton->callback_baton = callback_baton;
  wrap_baton->empty_file = NULL;
  wrap_baton->walk_deleted_dirs = walk_deleted_dirs;

  processor = svn_diff__tree_processor_create(wrap_baton, result_pool);

  processor->dir_opened   = wrap_dir_opened;
  processor->dir_added    = wrap_dir_added;
  processor->dir_deleted  = wrap_dir_deleted;
  processor->dir_changed  = wrap_dir_changed;
  processor->dir_closed   = wrap_dir_closed;

  processor->file_opened   = wrap_file_opened;
  processor->file_added    = wrap_file_added;
  processor->file_deleted  = wrap_file_deleted;
  processor->file_changed  = wrap_file_changed;
  /*processor->file_closed   = wrap_file_closed*/; /* Not needed */

  *diff_processor = processor;
  return SVN_NO_ERROR;
}

/* =====================================================================
 * A tree processor filter that filters by changelist membership
 * =====================================================================
 *
 * The current implementation queries the WC for the changelist of each
 * file as it comes through, and sets the 'skip' flag for a non-matching
 * file.
 *
 * (It doesn't set the 'skip' flag for a directory, as we need to receive
 * the changed/added/deleted/closed call to know when it is closed, in
 * order to preserve the strict open-close semantics for the wrapped tree
 * processor.)
 *
 * It passes on the opening and closing of every directory, even if there
 * are no file changes to be passed on inside that directory.
 */

typedef struct filter_tree_baton_t
{
  const svn_diff_tree_processor_t *processor;
  svn_wc_context_t *wc_ctx;
  /* WC path of the root of the diff (where relpath = "") */
  const char *root_local_abspath;
  /* Hash whose keys are const char * changelist names. */
  apr_hash_t *changelist_hash;
} filter_tree_baton_t;

static svn_error_t *
filter_dir_opened(void **new_dir_baton,
                  svn_boolean_t *skip,
                  svn_boolean_t *skip_children,
                  const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const svn_diff_source_t *copyfrom_source,
                  void *parent_dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->dir_opened(new_dir_baton, skip, skip_children,
                                    relpath,
                                    left_source, right_source,
                                    copyfrom_source,
                                    parent_dir_baton,
                                    fb->processor,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_added(const char *relpath,
                 const svn_diff_source_t *copyfrom_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *copyfrom_props,
                 /*const*/ apr_hash_t *right_props,
                 void *dir_baton,
                 const svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    NULL,
                                    right_source,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_deleted(const char *relpath,
                   const svn_diff_source_t *left_source,
                   /*const*/ apr_hash_t *left_props,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    left_source,
                                    NULL,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_changed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   /*const*/ apr_hash_t *left_props,
                   /*const*/ apr_hash_t *right_props,
                   const apr_array_header_t *prop_changes,
                   void *dir_baton,
                   const struct svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_dir_closed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->dir_closed(relpath,
                                    left_source,
                                    right_source,
                                    dir_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_opened(void **new_file_baton,
                   svn_boolean_t *skip,
                   const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   const svn_diff_source_t *copyfrom_source,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;
  const char *local_abspath
    = svn_dirent_join(fb->root_local_abspath, relpath, scratch_pool);

  /* Skip if not a member of a given changelist */
  if (! svn_wc__changelist_match(fb->wc_ctx, local_abspath,
                                 fb->changelist_hash, scratch_pool))
    {
      *skip = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(fb->processor->file_opened(new_file_baton,
                                     skip,
                                     relpath,
                                     left_source,
                                     right_source,
                                     copyfrom_source,
                                     dir_baton,
                                     fb->processor,
                                     result_pool,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_added(const char *relpath,
                  const svn_diff_source_t *copyfrom_source,
                  const svn_diff_source_t *right_source,
                  const char *copyfrom_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *copyfrom_props,
                  /*const*/ apr_hash_t *right_props,
                  void *file_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->file_added(relpath,
                                    copyfrom_source,
                                    right_source,
                                    copyfrom_file,
                                    right_file,
                                    copyfrom_props,
                                    right_props,
                                    file_baton,
                                    fb->processor,
                                    scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_deleted(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const char *left_file,
                    /*const*/ apr_hash_t *left_props,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->file_deleted(relpath,
                                      left_source,
                                      left_file,
                                      left_props,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_changed(const char *relpath,
                    const svn_diff_source_t *left_source,
                    const svn_diff_source_t *right_source,
                    const char *left_file,
                    const char *right_file,
                    /*const*/ apr_hash_t *left_props,
                    /*const*/ apr_hash_t *right_props,
                    svn_boolean_t file_modified,
                    const apr_array_header_t *prop_changes,
                    void *file_baton,
                    const svn_diff_tree_processor_t *processor,
                    apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->file_changed(relpath,
                                      left_source,
                                      right_source,
                                      left_file,
                                      right_file,
                                      left_props,
                                      right_props,
                                      file_modified,
                                      prop_changes,
                                      file_baton,
                                      fb->processor,
                                      scratch_pool));
  return SVN_NO_ERROR;
}

static svn_error_t *
filter_file_closed(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const svn_diff_source_t *right_source,
                   void *file_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->file_closed(relpath,
                                     left_source,
                                     right_source,
                                     file_baton,
                                     fb->processor,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
filter_node_absent(const char *relpath,
                   void *dir_baton,
                   const svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  struct filter_tree_baton_t *fb = processor->baton;

  SVN_ERR(fb->processor->node_absent(relpath,
                                     dir_baton,
                                     fb->processor,
                                     scratch_pool));
  return SVN_NO_ERROR;
}

const svn_diff_tree_processor_t *
svn_wc__changelist_filter_tree_processor_create(
                                const svn_diff_tree_processor_t *processor,
                                svn_wc_context_t *wc_ctx,
                                const char *root_local_abspath,
                                apr_hash_t *changelist_hash,
                                apr_pool_t *result_pool)
{
  struct filter_tree_baton_t *fb;
  svn_diff_tree_processor_t *filter;

  if (! changelist_hash)
    return processor;

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->processor = processor;
  fb->wc_ctx = wc_ctx;
  fb->root_local_abspath = root_local_abspath;
  fb->changelist_hash = changelist_hash;

  filter = svn_diff__tree_processor_create(fb, result_pool);
  filter->dir_opened   = filter_dir_opened;
  filter->dir_added    = filter_dir_added;
  filter->dir_deleted  = filter_dir_deleted;
  filter->dir_changed  = filter_dir_changed;
  filter->dir_closed   = filter_dir_closed;

  filter->file_opened   = filter_file_opened;
  filter->file_added    = filter_file_added;
  filter->file_deleted  = filter_file_deleted;
  filter->file_changed  = filter_file_changed;
  filter->file_closed   = filter_file_closed;

  filter->node_absent   = filter_node_absent;

  return filter;
}

