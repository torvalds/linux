/*
 * status.c: construct a status structure from an entry structure
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
#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>

#include "svn_pools.h"
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_config.h"
#include "svn_time.h"
#include "svn_hash.h"
#include "svn_sorts.h"

#include "svn_private_config.h"

#include "wc.h"
#include "props.h"

#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_fspath.h"
#include "private/svn_editor.h"


/* The file internal variant of svn_wc_status3_t, with slightly more
   data.

   Instead of directly creating svn_wc_status3_t instances, we really
   create instances of this struct with slightly more data for processing
   by the status walker and status editor.

   svn_wc_status3_dup() allocates space for this struct, but doesn't
   copy the actual data. The remaining fields are copied by hash_stash(),
   which is where the status editor stashes information for producing
   later. */
typedef struct svn_wc__internal_status_t
{
  svn_wc_status3_t s; /* First member; same pointer*/

  svn_boolean_t has_descendants;
  svn_boolean_t op_root;

  /* Make sure to update hash_stash() when adding values here */
} svn_wc__internal_status_t;


/*** Baton used for walking the local status */
struct walk_status_baton
{
  /* The DB handle for managing the working copy state. */
  svn_wc__db_t *db;

  /*** External handling ***/
  /* Target of the status */
  const char *target_abspath;

  /* Should we ignore text modifications? */
  svn_boolean_t ignore_text_mods;

  /* Scan the working copy for local modifications and missing nodes. */
  svn_boolean_t check_working_copy;

  /* Externals info harvested during the status run. */
  apr_hash_t *externals;

  /*** Repository lock handling ***/
  /* The repository root URL, if set. */
  const char *repos_root;

  /* Repository locks, if set. */
  apr_hash_t *repos_locks;
};

/*** Editor batons ***/

struct edit_baton
{
  /* For status, the "destination" of the edit.  */
  const char *anchor_abspath;
  const char *target_abspath;
  const char *target_basename;

  /* The DB handle for managing the working copy state.  */
  svn_wc__db_t *db;

  /* The overall depth of this edit (a dir baton may override this).
   *
   * If this is svn_depth_unknown, the depths found in the working
   * copy will govern the edit; or if the edit depth indicates a
   * descent deeper than the found depths are capable of, the found
   * depths also govern, of course (there's no point descending into
   * something that's not there).
   */
  svn_depth_t default_depth;

  /* Do we want all statuses (instead of just the interesting ones) ? */
  svn_boolean_t get_all;

  /* Ignore the svn:ignores. */
  svn_boolean_t no_ignore;

  /* The comparison revision in the repository.  This is a reference
     because this editor returns this rev to the driver directly, as
     well as in each statushash entry. */
  svn_revnum_t *target_revision;

  /* Status function/baton. */
  svn_wc_status_func4_t status_func;
  void *status_baton;

  /* Cancellation function/baton. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The configured set of default ignores. */
  const apr_array_header_t *ignores;

  /* Status item for the path represented by the anchor of the edit. */
  svn_wc__internal_status_t *anchor_status;

  /* Was open_root() called for this edit drive? */
  svn_boolean_t root_opened;

  /* The local status baton */
  struct walk_status_baton wb;
};


struct dir_baton
{
  /* The path to this directory. */
  const char *local_abspath;

  /* Basename of this directory. */
  const char *name;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this directory's parent, or NULL if this is the root
     directory. */
  struct dir_baton *parent_baton;

  /* The ambient requested depth below this point in the edit.  This
     can differ from the parent baton's depth (with the edit baton
     considered the ultimate parent baton).  For example, if the
     parent baton has svn_depth_immediates, then here we should have
     svn_depth_empty, because there would be no further recursion, not
     even to file children. */
  svn_depth_t depth;

  /* Is this directory filtered out due to depth?  (Note that if this
     is TRUE, the depth field is undefined.) */
  svn_boolean_t excluded;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* Gets set iff there's a change to this directory's properties, to
     guide us when syncing adm files later. */
  svn_boolean_t prop_changed;

  /* This means (in terms of 'svn status') that some child was deleted
     or added to the directory */
  svn_boolean_t text_changed;

  /* Working copy status structures for children of this directory.
     This hash maps const char * abspaths  to svn_wc_status3_t *
     status items. */
  apr_hash_t *statii;

  /* The pool in which this baton itself is allocated. */
  apr_pool_t *pool;

  /* The repository root relative path to this item in the repository. */
  const char *repos_relpath;

  /* out-of-date info corresponding to ood_* fields in svn_wc_status3_t. */
  svn_node_kind_t ood_kind;
  svn_revnum_t ood_changed_rev;
  apr_time_t ood_changed_date;
  const char *ood_changed_author;
};


struct file_baton
{
/* Absolute local path to this file */
  const char *local_abspath;

  /* The global edit baton. */
  struct edit_baton *edit_baton;

  /* Baton for this file's parent directory. */
  struct dir_baton *dir_baton;

  /* Pool specific to this file_baton. */
  apr_pool_t *pool;

  /* Basename of this file */
  const char *name;

  /* 'svn status' shouldn't print status lines for things that are
     added;  we're only interest in asking if objects that the user
     *already* has are up-to-date or not.  Thus if this flag is set,
     the next two will be ignored.  :-)  */
  svn_boolean_t added;

  /* This gets set if the file underwent a text change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t text_changed;

  /* This gets set if the file underwent a prop change, which guides
     the code that syncs up the adm dir and working copy. */
  svn_boolean_t prop_changed;

  /* The repository root relative path to this item in the repository. */
  const char *repos_relpath;

  /* out-of-date info corresponding to ood_* fields in svn_wc_status3_t. */
  svn_node_kind_t ood_kind;
  svn_revnum_t ood_changed_rev;
  apr_time_t ood_changed_date;

  const char *ood_changed_author;
};


/** Code **/



/* Return *REPOS_RELPATH and *REPOS_ROOT_URL for LOCAL_ABSPATH using
   information in INFO if available, falling back on
   PARENT_REPOS_RELPATH and PARENT_REPOS_ROOT_URL if available, and
   finally falling back on querying DB. */
static svn_error_t *
get_repos_root_url_relpath(const char **repos_relpath,
                           const char **repos_root_url,
                           const char **repos_uuid,
                           const struct svn_wc__db_info_t *info,
                           const char *parent_repos_relpath,
                           const char *parent_repos_root_url,
                           const char *parent_repos_uuid,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  if (info->repos_relpath && info->repos_root_url)
    {
      *repos_relpath = apr_pstrdup(result_pool, info->repos_relpath);
      *repos_root_url = apr_pstrdup(result_pool, info->repos_root_url);
      *repos_uuid = apr_pstrdup(result_pool, info->repos_uuid);
    }
  else if (parent_repos_relpath && parent_repos_root_url)
    {
      *repos_relpath = svn_relpath_join(parent_repos_relpath,
                                        svn_dirent_basename(local_abspath,
                                                            NULL),
                                        result_pool);
      *repos_root_url = apr_pstrdup(result_pool, parent_repos_root_url);
      *repos_uuid = apr_pstrdup(result_pool, parent_repos_uuid);
    }
  else
    {
      SVN_ERR(svn_wc__db_read_repos_info(NULL,
                                         repos_relpath, repos_root_url,
                                         repos_uuid,
                                         db, local_abspath,
                                         result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
internal_status(svn_wc__internal_status_t **status,
                svn_wc__db_t *db,
                const char *local_abspath,
                svn_boolean_t check_working_copy,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool);

/* Fill in *STATUS for LOCAL_ABSPATH, using DB. Allocate *STATUS in
   RESULT_POOL and use SCRATCH_POOL for temporary allocations.

   PARENT_REPOS_ROOT_URL and PARENT_REPOS_RELPATH are the repository root
   and repository relative path of the parent of LOCAL_ABSPATH or NULL if
   LOCAL_ABSPATH doesn't have a versioned parent directory.

   DIRENT is the local representation of LOCAL_ABSPATH in the working copy or
   NULL if the node does not exist on disk.

   If GET_ALL is FALSE, and LOCAL_ABSPATH is not locally modified, then
   *STATUS will be set to NULL.  If GET_ALL is non-zero, then *STATUS will be
   allocated and returned no matter what.  If IGNORE_TEXT_MODS is TRUE then
   don't check for text mods, assume there are none and set and *STATUS
   returned to reflect that assumption. If CHECK_WORKING_COPY is FALSE,
   do not adjust the result for missing working copy files.

   The status struct's repos_lock field will be set to REPOS_LOCK.
*/
static svn_error_t *
assemble_status(svn_wc__internal_status_t **status,
                svn_wc__db_t *db,
                const char *local_abspath,
                const char *parent_repos_root_url,
                const char *parent_repos_relpath,
                const char *parent_repos_uuid,
                const struct svn_wc__db_info_t *info,
                const svn_io_dirent2_t *dirent,
                svn_boolean_t get_all,
                svn_boolean_t ignore_text_mods,
                svn_boolean_t check_working_copy,
                const svn_lock_t *repos_lock,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_wc__internal_status_t *inner_stat;
  svn_wc_status3_t *stat;
  svn_boolean_t switched_p = FALSE;
  svn_boolean_t copied = FALSE;
  svn_boolean_t conflicted;
  const char *moved_from_abspath = NULL;

  /* Defaults for two main variables. */
  enum svn_wc_status_kind node_status = svn_wc_status_normal;
  enum svn_wc_status_kind text_status = svn_wc_status_normal;
  enum svn_wc_status_kind prop_status = svn_wc_status_none;


  if (!info->repos_relpath || !parent_repos_relpath)
    switched_p = FALSE;
  else
    {
      /* A node is switched if it doesn't have the implied repos_relpath */
      const char *name = svn_relpath_skip_ancestor(parent_repos_relpath,
                                                   info->repos_relpath);
      switched_p = !name || (strcmp(name,
                                    svn_dirent_basename(local_abspath, NULL))
                             != 0);
    }

  if (info->status == svn_wc__db_status_incomplete || info->incomplete)
    {
      /* Highest precedence.  */
      node_status = svn_wc_status_incomplete;
    }
  else if (info->status == svn_wc__db_status_deleted)
    {
      node_status = svn_wc_status_deleted;

      if (!info->have_base || info->have_more_work || info->copied)
        copied = TRUE;
      else if (!info->have_more_work && info->have_base)
        copied = FALSE;
      else
        {
          const char *work_del_abspath;

          /* Find out details of our deletion.  */
          SVN_ERR(svn_wc__db_scan_deletion(NULL, NULL,
                                           &work_del_abspath, NULL,
                                           db, local_abspath,
                                           scratch_pool, scratch_pool));
          if (work_del_abspath)
            copied = TRUE; /* Working deletion */
        }
    }
  else if (check_working_copy)
    {
      /* Examine whether our target is missing or obstructed. To detect
       * obstructions, we have to look at the on-disk status in DIRENT. */
      svn_node_kind_t expected_kind = (info->kind == svn_node_dir)
                                        ? svn_node_dir
                                        : svn_node_file;

      if (!dirent || dirent->kind != expected_kind)
        {
          /* A present or added node should be on disk, so it is
             reported missing or obstructed.  */
          if (!dirent || dirent->kind == svn_node_none)
            node_status = svn_wc_status_missing;
          else
            node_status = svn_wc_status_obstructed;
        }
    }

  /* Does the node have props? */
  if (info->status != svn_wc__db_status_deleted)
    {
      if (info->props_mod)
        prop_status = svn_wc_status_modified;
      else if (info->had_props)
        prop_status = svn_wc_status_normal;
    }

  /* If NODE_STATUS is still normal, after the above checks, then
     we should proceed to refine the status.

     If it was changed, then the subdir is incomplete or missing/obstructed.
   */
  if (info->kind != svn_node_dir
      && node_status == svn_wc_status_normal)
    {
      svn_boolean_t text_modified_p = FALSE;

      /* Implement predecence rules: */

      /* 1. Set the two main variables to "discovered" values first (M, C).
            Together, these two stati are of lowest precedence, and C has
            precedence over M. */

      /* If the entry is a file, check for textual modifications */
      if ((info->kind == svn_node_file
          || info->kind == svn_node_symlink)
#ifdef HAVE_SYMLINK
             && (info->special == (dirent && dirent->special))
#endif /* HAVE_SYMLINK */
          )
        {
          /* If the on-disk dirent exactly matches the expected state
             skip all operations in svn_wc__internal_text_modified_p()
             to avoid an extra filestat for every file, which can be
             expensive on network drives as a filestat usually can't
             be cached there */
          if (!info->has_checksum)
            text_modified_p = TRUE; /* Local addition -> Modified */
          else if (ignore_text_mods
                  ||(dirent
                     && info->recorded_size != SVN_INVALID_FILESIZE
                     && info->recorded_time != 0
                     && info->recorded_size == dirent->filesize
                     && info->recorded_time == dirent->mtime))
            text_modified_p = FALSE;
          else
            {
              svn_error_t *err;
              err = svn_wc__internal_file_modified_p(&text_modified_p,
                                                     db, local_abspath,
                                                     FALSE, scratch_pool);

              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_PATH_ACCESS_DENIED)
                    return svn_error_trace(err);

                  /* An access denied is very common on Windows when another
                     application has the file open.  Previously we ignored
                     this error in svn_wc__text_modified_internal_p, where it
                     should have really errored. */
                  svn_error_clear(err);
                  text_modified_p = TRUE;
                }
            }
        }
#ifdef HAVE_SYMLINK
      else if (info->special != (dirent && dirent->special))
        node_status = svn_wc_status_obstructed;
#endif /* HAVE_SYMLINK */

      if (text_modified_p)
        text_status = svn_wc_status_modified;
    }

  conflicted = info->conflicted;
  if (conflicted)
    {
      svn_boolean_t text_conflicted, prop_conflicted, tree_conflicted;

      /* ### Check if the conflict was resolved by removing the marker files.
         ### This should really be moved to the users of this API */
      SVN_ERR(svn_wc__internal_conflicted_p(&text_conflicted, &prop_conflicted,
                                            &tree_conflicted,
                                            db, local_abspath, scratch_pool));

      if (!text_conflicted && !prop_conflicted && !tree_conflicted)
        conflicted = FALSE;
    }

  if (node_status == svn_wc_status_normal)
    {
      /* 2. Possibly overwrite the text_status variable with "scheduled"
            states from the entry (A, D, R).  As a group, these states are
            of medium precedence.  They also override any C or M that may
            be in the prop_status field at this point, although they do not
            override a C text status.*/
      if (info->status == svn_wc__db_status_added)
        {
          copied = info->copied;
          if (!info->op_root)
            { /* Keep status normal */ }
          else if (!info->have_base && !info->have_more_work)
            {
              /* Simple addition or copy, no replacement */
              node_status = svn_wc_status_added;
            }
          else
            {
              svn_wc__db_status_t below_working;
              svn_boolean_t have_base, have_work;

              SVN_ERR(svn_wc__db_info_below_working(&have_base, &have_work,
                                                    &below_working,
                                                    db, local_abspath,
                                                    scratch_pool));

              /* If the node is not present or deleted (read: not present
                 in working), then the node is not a replacement */
              if (below_working != svn_wc__db_status_not_present
                  && below_working != svn_wc__db_status_deleted)
                {
                  node_status = svn_wc_status_replaced;
                }
              else
                node_status = svn_wc_status_added;
            }

          /* Get moved-from info (only for potential op-roots of a move). */
          if (info->moved_here && info->op_root)
            {
              svn_error_t *err;
              err = svn_wc__db_scan_moved(&moved_from_abspath, NULL, NULL, NULL,
                                          db, local_abspath,
                                          result_pool, scratch_pool);

              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
                    return svn_error_trace(err);

                  svn_error_clear(err);
                  /* We are no longer moved... So most likely we are somehow
                     changing the db for things like resolving conflicts. */

                  moved_from_abspath = NULL;
                }
            }
        }
    }


  if (node_status == svn_wc_status_normal)
    node_status = text_status;

  if (node_status == svn_wc_status_normal
      && prop_status != svn_wc_status_none)
    node_status = prop_status;

  /* 5. Easy out:  unless we're fetching -every- node, don't bother
     to allocate a struct for an uninteresting node.

     This filter should match the filter in is_sendable_status() */
  if (! get_all)
    if (((node_status == svn_wc_status_none)
         || (node_status == svn_wc_status_normal)
         || (node_status == svn_wc_status_deleted && !info->op_root))

        && (! switched_p)
        && (! info->locked)
        && (! info->lock)
        && (! repos_lock)
        && (! info->changelist)
        && (! conflicted)
        && (! info->moved_to))
      {
        *status = NULL;
        return SVN_NO_ERROR;
      }

  /* 6. Build and return a status structure. */

  inner_stat = apr_pcalloc(result_pool, sizeof(*inner_stat));
  stat = &inner_stat->s;
  inner_stat->has_descendants = info->has_descendants;
  inner_stat->op_root = info->op_root;

  switch (info->kind)
    {
      case svn_node_dir:
        stat->kind = svn_node_dir;
        break;
      case svn_node_file:
      case svn_node_symlink:
        stat->kind = svn_node_file;
        break;
      case svn_node_unknown:
      default:
        stat->kind = svn_node_unknown;
    }
  stat->depth = info->depth;

  if (dirent)
    {
      stat->filesize = (dirent->kind == svn_node_file)
                            ? dirent->filesize
                            : SVN_INVALID_FILESIZE;
      stat->actual_kind = dirent->special ? svn_node_symlink
                                          : dirent->kind;
    }
  else
    {
      stat->filesize = SVN_INVALID_FILESIZE;
      stat->actual_kind = ignore_text_mods ? svn_node_unknown
                                           : svn_node_none;
    }

  stat->node_status = node_status;
  stat->text_status = text_status;
  stat->prop_status = prop_status;
  stat->repos_node_status = svn_wc_status_none;   /* default */
  stat->repos_text_status = svn_wc_status_none;   /* default */
  stat->repos_prop_status = svn_wc_status_none;   /* default */
  stat->switched = switched_p;
  stat->copied = copied;
  stat->repos_lock = repos_lock;
  stat->revision = info->revnum;
  stat->changed_rev = info->changed_rev;
  if (info->changed_author)
    stat->changed_author = apr_pstrdup(result_pool, info->changed_author);
  stat->changed_date = info->changed_date;

  stat->ood_kind = svn_node_none;
  stat->ood_changed_rev = SVN_INVALID_REVNUM;
  stat->ood_changed_date = 0;
  stat->ood_changed_author = NULL;

  SVN_ERR(get_repos_root_url_relpath(&stat->repos_relpath,
                                     &stat->repos_root_url,
                                     &stat->repos_uuid, info,
                                     parent_repos_relpath,
                                     parent_repos_root_url,
                                     parent_repos_uuid,
                                     db, local_abspath,
                                     result_pool, scratch_pool));

  if (info->lock)
    {
      svn_lock_t *lck = svn_lock_create(result_pool);
      lck->path = stat->repos_relpath;
      lck->token = info->lock->token;
      lck->owner = info->lock->owner;
      lck->comment = info->lock->comment;
      lck->creation_date = info->lock->date;
      stat->lock = lck;
    }
  else
    stat->lock = NULL;

  stat->locked = info->locked;
  stat->conflicted = conflicted;
  stat->versioned = TRUE;
  if (info->changelist)
    stat->changelist = apr_pstrdup(result_pool, info->changelist);

  stat->moved_from_abspath = moved_from_abspath;

  /* ### TODO: Handle multiple moved_to values properly */
  if (info->moved_to)
    stat->moved_to_abspath = apr_pstrdup(result_pool,
                                         info->moved_to->moved_to_abspath);

  stat->file_external = info->file_external;

  *status = inner_stat;

  return SVN_NO_ERROR;
}

/* Fill in *STATUS for the unversioned path LOCAL_ABSPATH, using data
   available in DB. Allocate *STATUS in POOL. Use SCRATCH_POOL for
   temporary allocations.

   If IS_IGNORED is non-zero and this is a non-versioned entity, set
   the node_status to svn_wc_status_none.  Otherwise set the
   node_status to svn_wc_status_unversioned.
 */
static svn_error_t *
assemble_unversioned(svn_wc__internal_status_t **status,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const svn_io_dirent2_t *dirent,
                     svn_boolean_t tree_conflicted,
                     svn_boolean_t is_ignored,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__internal_status_t *inner_status;
  svn_wc_status3_t *stat;

  /* return a fairly blank structure. */
  inner_status = apr_pcalloc(result_pool, sizeof(*inner_status));
  stat = &inner_status->s;

  /*stat->versioned = FALSE;*/
  stat->kind = svn_node_unknown; /* not versioned */
  stat->depth = svn_depth_unknown;
  if (dirent)
    {
      stat->actual_kind = dirent->special ? svn_node_symlink
                                           : dirent->kind;
      stat->filesize = (dirent->kind == svn_node_file)
                            ? dirent->filesize
                            : SVN_INVALID_FILESIZE;
    }
  else
    {
       stat->actual_kind = svn_node_none;
       stat->filesize = SVN_INVALID_FILESIZE;
    }

  stat->node_status = svn_wc_status_none;
  stat->text_status = svn_wc_status_none;
  stat->prop_status = svn_wc_status_none;
  stat->repos_node_status = svn_wc_status_none;
  stat->repos_text_status = svn_wc_status_none;
  stat->repos_prop_status = svn_wc_status_none;

  /* If this path has no entry, but IS present on disk, it's
     unversioned.  If this file is being explicitly ignored (due
     to matching an ignore-pattern), the node_status is set to
     svn_wc_status_ignored.  Otherwise the node_status is set to
     svn_wc_status_unversioned. */
  if (dirent && dirent->kind != svn_node_none)
    {
      if (is_ignored)
        stat->node_status = svn_wc_status_ignored;
      else
        stat->node_status = svn_wc_status_unversioned;
    }
  else if (tree_conflicted)
    {
      /* If this path has no entry, is NOT present on disk, and IS a
         tree conflict victim, report it as conflicted. */
      stat->node_status = svn_wc_status_conflicted;
    }

  stat->revision = SVN_INVALID_REVNUM;
  stat->changed_rev = SVN_INVALID_REVNUM;
  stat->ood_changed_rev = SVN_INVALID_REVNUM;
  stat->ood_kind = svn_node_none;

  /* For the case of an incoming delete to a locally deleted path during
     an update, we get a tree conflict. */
  stat->conflicted = tree_conflicted;
  stat->changelist = NULL;

  *status = inner_status;
  return SVN_NO_ERROR;
}


/* Given an ENTRY object representing PATH, build a status structure
   and pass it off to the STATUS_FUNC/STATUS_BATON.  All other
   arguments are the same as those passed to assemble_status().  */
static svn_error_t *
send_status_structure(const struct walk_status_baton *wb,
                      const char *local_abspath,
                      const char *parent_repos_root_url,
                      const char *parent_repos_relpath,
                      const char *parent_repos_uuid,
                      const struct svn_wc__db_info_t *info,
                      const svn_io_dirent2_t *dirent,
                      svn_boolean_t get_all,
                      svn_wc_status_func4_t status_func,
                      void *status_baton,
                      apr_pool_t *scratch_pool)
{
  svn_wc__internal_status_t *statstruct;
  const svn_lock_t *repos_lock = NULL;

  /* Check for a repository lock. */
  if (wb->repos_locks)
    {
      const char *repos_relpath, *repos_root_url, *repos_uuid;

      SVN_ERR(get_repos_root_url_relpath(&repos_relpath, &repos_root_url,
                                         &repos_uuid,
                                         info, parent_repos_relpath,
                                         parent_repos_root_url,
                                         parent_repos_uuid,
                                         wb->db, local_abspath,
                                         scratch_pool, scratch_pool));
      if (repos_relpath)
        {
          /* repos_lock still uses the deprecated filesystem absolute path
             format */
          repos_lock = svn_hash_gets(wb->repos_locks,
                                     svn_fspath__join("/", repos_relpath,
                                                      scratch_pool));
        }
    }

  SVN_ERR(assemble_status(&statstruct, wb->db, local_abspath,
                          parent_repos_root_url, parent_repos_relpath,
                          parent_repos_uuid,
                          info, dirent, get_all,
                          wb->ignore_text_mods, wb->check_working_copy,
                          repos_lock, scratch_pool, scratch_pool));

  if (statstruct && status_func)
    return svn_error_trace((*status_func)(status_baton, local_abspath,
                                          &statstruct->s,
                                          scratch_pool));

  return SVN_NO_ERROR;
}


/* Store in *PATTERNS a list of ignores collected from svn:ignore properties
   on LOCAL_ABSPATH and svn:global-ignores on LOCAL_ABSPATH and its
   repository ancestors (as cached in the working copy), including the default
   ignores passed in as IGNORES.

   Upon return, *PATTERNS will contain zero or more (const char *)
   patterns from the value of the SVN_PROP_IGNORE property set on
   the working directory path.

   IGNORES is a list of patterns to include; typically this will
   be the default ignores as, for example, specified in a config file.

   DB, LOCAL_ABSPATH is used to access the working copy.

   Allocate results in RESULT_POOL, temporary stuffs in SCRATCH_POOL.

   None of the arguments may be NULL.
*/
static svn_error_t *
collect_ignore_patterns(apr_array_header_t **patterns,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const apr_array_header_t *ignores,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  int i;
  apr_hash_t *props;
  apr_array_header_t *inherited_props;
  svn_error_t *err;

  /* ### assert we are passed a directory? */

  *patterns = apr_array_make(result_pool, 1, sizeof(const char *));

  /* Copy default ignores into the local PATTERNS array. */
  for (i = 0; i < ignores->nelts; i++)
    {
      const char *ignore = APR_ARRAY_IDX(ignores, i, const char *);
      APR_ARRAY_PUSH(*patterns, const char *) = apr_pstrdup(result_pool,
                                                            ignore);
    }

  err = svn_wc__db_read_inherited_props(&inherited_props, &props,
                                        db, local_abspath,
                                        SVN_PROP_INHERITABLE_IGNORES,
                                        scratch_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (props)
    {
      const svn_string_t *value;

      value = svn_hash_gets(props, SVN_PROP_IGNORE);
      if (value)
        svn_cstring_split_append(*patterns, value->data, "\n\r", FALSE,
                                 result_pool);

      value = svn_hash_gets(props, SVN_PROP_INHERITABLE_IGNORES);
      if (value)
        svn_cstring_split_append(*patterns, value->data, "\n\r", FALSE,
                                 result_pool);
    }

  for (i = 0; i < inherited_props->nelts; i++)
    {
      svn_prop_inherited_item_t *elt = APR_ARRAY_IDX(
        inherited_props, i, svn_prop_inherited_item_t *);
      const svn_string_t *value;

      value = svn_hash_gets(elt->prop_hash, SVN_PROP_INHERITABLE_IGNORES);

      if (value)
        svn_cstring_split_append(*patterns, value->data,
                                 "\n\r", FALSE, result_pool);
    }

  return SVN_NO_ERROR;
}


/* Compare LOCAL_ABSPATH with items in the EXTERNALS hash to see if
   LOCAL_ABSPATH is the drop location for, or an intermediate directory
   of the drop location for, an externals definition.  Use SCRATCH_POOL
   for scratchwork.  */
static svn_boolean_t
is_external_path(apr_hash_t *externals,
                 const char *local_abspath,
                 apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* First try: does the path exist as a key in the hash? */
  if (svn_hash_gets(externals, local_abspath))
    return TRUE;

  /* Failing that, we need to check if any external is a child of
     LOCAL_ABSPATH.  */
  for (hi = apr_hash_first(scratch_pool, externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *external_abspath = apr_hash_this_key(hi);

      if (svn_dirent_is_child(local_abspath, external_abspath, NULL))
        return TRUE;
    }

  return FALSE;
}


/* Assuming that LOCAL_ABSPATH is unversioned, send a status structure
   for it through STATUS_FUNC/STATUS_BATON unless this path is being
   ignored.  This function should never be called on a versioned entry.

   LOCAL_ABSPATH is the path to the unversioned file whose status is being
   requested.  PATH_KIND is the node kind of NAME as determined by the
   caller.  PATH_SPECIAL is the special status of the path, also determined
   by the caller.
   PATTERNS points to a list of filename patterns which are marked as ignored.
   None of these parameter may be NULL.

   If NO_IGNORE is TRUE, the item will be added regardless of
   whether it is ignored; otherwise we will only add the item if it
   does not match any of the patterns in PATTERN or INHERITED_IGNORES.

   Allocate everything in POOL.
*/
static svn_error_t *
send_unversioned_item(const struct walk_status_baton *wb,
                      const char *local_abspath,
                      const svn_io_dirent2_t *dirent,
                      svn_boolean_t tree_conflicted,
                      const apr_array_header_t *patterns,
                      svn_boolean_t no_ignore,
                      svn_wc_status_func4_t status_func,
                      void *status_baton,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t is_ignored;
  svn_boolean_t is_external;
  svn_wc__internal_status_t *status;
  const char *base_name = svn_dirent_basename(local_abspath, NULL);

  is_ignored = svn_wc_match_ignore_list(base_name, patterns, scratch_pool);
  SVN_ERR(assemble_unversioned(&status,
                               wb->db, local_abspath,
                               dirent, tree_conflicted,
                               is_ignored,
                               scratch_pool, scratch_pool));

  is_external = is_external_path(wb->externals, local_abspath, scratch_pool);
  if (is_external)
    status->s.node_status = svn_wc_status_external;

  /* We can have a tree conflict on an unversioned path, i.e. an incoming
   * delete on a locally deleted path during an update. Don't ever ignore
   * those! */
  if (status->s.conflicted)
    is_ignored = FALSE;

  /* If we aren't ignoring it, or if it's an externals path, pass this
     entry to the status func. */
  if (no_ignore
      || !is_ignored
      || is_external)
    return svn_error_trace((*status_func)(status_baton, local_abspath,
                                          &status->s, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
get_dir_status(const struct walk_status_baton *wb,
               const char *local_abspath,
               svn_boolean_t skip_this_dir,
               const char *parent_repos_root_url,
               const char *parent_repos_relpath,
               const char *parent_repos_uuid,
               const struct svn_wc__db_info_t *dir_info,
               const svn_io_dirent2_t *dirent,
               const apr_array_header_t *ignore_patterns,
               svn_depth_t depth,
               svn_boolean_t get_all,
               svn_boolean_t no_ignore,
               svn_wc_status_func4_t status_func,
               void *status_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool);

/* Send out a status structure according to the information gathered on one
 * child node. (Basically this function is the guts of the loop in
 * get_dir_status() and of get_child_status().)
 *
 * Send a status structure of LOCAL_ABSPATH. PARENT_ABSPATH must be the
 * dirname of LOCAL_ABSPATH.
 *
 * INFO should reflect the information on LOCAL_ABSPATH; LOCAL_ABSPATH must
 * be an unversioned file or dir, or a versioned file.  For versioned
 * directories use get_dir_status() instead.
 *
 * INFO may be NULL for an unversioned node. If such node has a tree conflict,
 * UNVERSIONED_TREE_CONFLICTED may be set to TRUE. If INFO is non-NULL,
 * UNVERSIONED_TREE_CONFLICTED is ignored.
 *
 * DIRENT should reflect LOCAL_ABSPATH's dirent information.
 *
 * DIR_REPOS_* should reflect LOCAL_ABSPATH's parent URL, i.e. LOCAL_ABSPATH's
 * URL treated with svn_uri_dirname(). ### TODO verify this (externals)
 *
 * If *COLLECTED_IGNORE_PATTERNS is NULL and ignore patterns are needed in this
 * call, then *COLLECTED_IGNORE_PATTERNS will be set to an apr_array_header_t*
 * containing all ignore patterns, as returned by collect_ignore_patterns() on
 * PARENT_ABSPATH and IGNORE_PATTERNS. If *COLLECTED_IGNORE_PATTERNS is passed
 * non-NULL, it is assumed it already holds those results.
 * This speeds up repeated calls with the same PARENT_ABSPATH.
 *
 * *COLLECTED_IGNORE_PATTERNS will be allocated in RESULT_POOL. All other
 * allocations are made in SCRATCH_POOL.
 *
 * The remaining parameters correspond to get_dir_status(). */
static svn_error_t *
one_child_status(const struct walk_status_baton *wb,
                 const char *local_abspath,
                 const char *parent_abspath,
                 const struct svn_wc__db_info_t *info,
                 const svn_io_dirent2_t *dirent,
                 const char *dir_repos_root_url,
                 const char *dir_repos_relpath,
                 const char *dir_repos_uuid,
                 svn_boolean_t unversioned_tree_conflicted,
                 apr_array_header_t **collected_ignore_patterns,
                 const apr_array_header_t *ignore_patterns,
                 svn_depth_t depth,
                 svn_boolean_t get_all,
                 svn_boolean_t no_ignore,
                 svn_wc_status_func4_t status_func,
                 void *status_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_boolean_t conflicted = info ? info->conflicted
                                  : unversioned_tree_conflicted;

  if (info
      && info->status != svn_wc__db_status_not_present
      && info->status != svn_wc__db_status_excluded
      && info->status != svn_wc__db_status_server_excluded
      && !(info->kind == svn_node_unknown
           && info->status == svn_wc__db_status_normal))
    {
      if (depth == svn_depth_files
          && info->kind == svn_node_dir)
        {
          return SVN_NO_ERROR;
        }

      SVN_ERR(send_status_structure(wb, local_abspath,
                                    dir_repos_root_url,
                                    dir_repos_relpath,
                                    dir_repos_uuid,
                                    info, dirent, get_all,
                                    status_func, status_baton,
                                    scratch_pool));

      /* Descend in subdirectories. */
      if (depth == svn_depth_infinity
          && info->has_descendants /* is dir, or was dir and tc descendants */)
        {
          SVN_ERR(get_dir_status(wb, local_abspath, TRUE,
                                 dir_repos_root_url, dir_repos_relpath,
                                 dir_repos_uuid, info,
                                 dirent, ignore_patterns,
                                 svn_depth_infinity, get_all,
                                 no_ignore,
                                 status_func, status_baton,
                                 cancel_func, cancel_baton,
                                 scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  /* If conflicted, fall right through to unversioned.
   * With depth_files, show all conflicts, even if their report is only
   * about directories. A tree conflict may actually report two different
   * kinds, so it's not so easy to define what depth=files means. We could go
   * look up the kinds in the conflict ... just show all. */
  if (! conflicted)
    {
      /* We have a node, but its not visible in the WC. It can be a marker
         node (not present, (server) excluded), *or* it can be the explictly
         passed target of the status walk operation that doesn't exist.

         We only report the node when the caller explicitly as
      */
      if (dirent == NULL && strcmp(wb->target_abspath, local_abspath) != 0)
        return SVN_NO_ERROR; /* Marker node */

      if (depth == svn_depth_files && dirent && dirent->kind == svn_node_dir)
        return SVN_NO_ERROR;

      if (svn_wc_is_adm_dir(svn_dirent_basename(local_abspath, NULL),
                            scratch_pool))
        return SVN_NO_ERROR;
    }

  /* The node exists on disk but there is no versioned information about it,
   * or it doesn't exist but is a tree conflicted path or should be
   * reported not-present. */

  /* Why pass ignore patterns on a tree conflicted node, even if it should
   * always show up in clients' status reports anyway? Because the calling
   * client decides whether to ignore, and thus this flag needs to be
   * determined.  For example, in 'svn status', plain unversioned nodes show
   * as '?  C', where ignored ones show as 'I  C'. */

  if (ignore_patterns && ! *collected_ignore_patterns)
    SVN_ERR(collect_ignore_patterns(collected_ignore_patterns,
                                    wb->db, parent_abspath, ignore_patterns,
                                    result_pool, scratch_pool));

  SVN_ERR(send_unversioned_item(wb,
                                local_abspath,
                                dirent,
                                conflicted,
                                *collected_ignore_patterns,
                                no_ignore,
                                status_func, status_baton,
                                scratch_pool));

  return SVN_NO_ERROR;
}

/* Send svn_wc_status3_t * structures for the directory LOCAL_ABSPATH and
   for all its child nodes (according to DEPTH) through STATUS_FUNC /
   STATUS_BATON.

   If SKIP_THIS_DIR is TRUE, the directory's own status will not be reported.
   All subdirs reached by recursion will be reported regardless of this
   parameter's value.

   PARENT_REPOS_* parameters can be set to refer to LOCAL_ABSPATH's parent's
   URL, i.e. the URL the WC reflects at the dirname of LOCAL_ABSPATH, to avoid
   retrieving them again. Otherwise they must be NULL.

   DIR_INFO can be set to the information of LOCAL_ABSPATH, to avoid retrieving
   it again. Otherwise it must be NULL.

   DIRENT is LOCAL_ABSPATH's own dirent and is only needed if it is reported,
   so if SKIP_THIS_DIR is TRUE, DIRENT can be left NULL.

   Other arguments are the same as those passed to
   svn_wc_get_status_editor5().  */
static svn_error_t *
get_dir_status(const struct walk_status_baton *wb,
               const char *local_abspath,
               svn_boolean_t skip_this_dir,
               const char *parent_repos_root_url,
               const char *parent_repos_relpath,
               const char *parent_repos_uuid,
               const struct svn_wc__db_info_t *dir_info,
               const svn_io_dirent2_t *dirent,
               const apr_array_header_t *ignore_patterns,
               svn_depth_t depth,
               svn_boolean_t get_all,
               svn_boolean_t no_ignore,
               svn_wc_status_func4_t status_func,
               void *status_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  const char *dir_repos_root_url;
  const char *dir_repos_relpath;
  const char *dir_repos_uuid;
  apr_hash_t *dirents, *nodes, *conflicts, *all_children;
  apr_array_header_t *sorted_children;
  apr_array_header_t *collected_ignore_patterns = NULL;
  apr_pool_t *iterpool;
  svn_error_t *err;
  int i;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  iterpool = svn_pool_create(scratch_pool);

  if (wb->check_working_copy)
    {
      err = svn_io_get_dirents3(&dirents, local_abspath,
                                wb->ignore_text_mods /* only_check_type*/,
                                scratch_pool, iterpool);
      if (err
          && (APR_STATUS_IS_ENOENT(err->apr_err)
              || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          svn_error_clear(err);
          dirents = apr_hash_make(scratch_pool);
        }
      else
        SVN_ERR(err);
    }
  else
    dirents = apr_hash_make(scratch_pool);

  if (!dir_info)
    SVN_ERR(svn_wc__db_read_single_info(&dir_info, wb->db, local_abspath,
                                        !wb->check_working_copy,
                                        scratch_pool, iterpool));

  SVN_ERR(get_repos_root_url_relpath(&dir_repos_relpath, &dir_repos_root_url,
                                     &dir_repos_uuid, dir_info,
                                     parent_repos_relpath,
                                     parent_repos_root_url, parent_repos_uuid,
                                     wb->db, local_abspath,
                                     scratch_pool, iterpool));

  /* Create a hash containing all children.  The source hashes
     don't all map the same types, but only the keys of the result
     hash are subsequently used. */
  SVN_ERR(svn_wc__db_read_children_info(&nodes, &conflicts,
                                        wb->db, local_abspath,
                                        !wb->check_working_copy,
                                        scratch_pool, iterpool));

  all_children = apr_hash_overlay(scratch_pool, nodes, dirents);
  if (apr_hash_count(conflicts) > 0)
    all_children = apr_hash_overlay(scratch_pool, conflicts, all_children);

  /* Handle "this-dir" first. */
  if (! skip_this_dir)
    {
      /* This code is not conditional on HAVE_SYMLINK as some systems that do
         not allow creating symlinks (!HAVE_SYMLINK) can still encounter
         symlinks (or in case of Windows also 'Junctions') created by other
         methods.

         Without this block a working copy in the root of a junction is
         reported as an obstruction, because the junction itself is reported as
         special.

         Systems that have no symlink support at all, would always see
         dirent->special as FALSE, so even there enabling this code shouldn't
         produce problems.
       */
      if (dirent->special)
        {
          svn_io_dirent2_t *this_dirent = svn_io_dirent2_dup(dirent, iterpool);

          /* We're being pointed to "this-dir" via a symlink.
           * Get the real node kind and pretend the path is not a symlink.
           * This prevents send_status_structure() from treating this-dir
           * as a directory obstructed by a file. */
          SVN_ERR(svn_io_check_resolved_path(local_abspath,
                                             &this_dirent->kind, iterpool));
          this_dirent->special = FALSE;
          SVN_ERR(send_status_structure(wb, local_abspath,
                                        parent_repos_root_url,
                                        parent_repos_relpath,
                                        parent_repos_uuid,
                                        dir_info, this_dirent, get_all,
                                        status_func, status_baton,
                                        iterpool));
        }
     else
        SVN_ERR(send_status_structure(wb, local_abspath,
                                      parent_repos_root_url,
                                      parent_repos_relpath,
                                      parent_repos_uuid,
                                      dir_info, dirent, get_all,
                                      status_func, status_baton,
                                      iterpool));
    }

  /* If the requested depth is empty, we only need status on this-dir. */
  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Walk all the children of this directory. */
  sorted_children = svn_sort__hash(all_children,
                                   svn_sort_compare_items_lexically,
                                   scratch_pool);
  for (i = 0; i < sorted_children->nelts; i++)
    {
      const void *key;
      apr_ssize_t klen;
      svn_sort__item_t item;
      const char *child_abspath;
      svn_io_dirent2_t *child_dirent;
      const struct svn_wc__db_info_t *child_info;

      svn_pool_clear(iterpool);

      item = APR_ARRAY_IDX(sorted_children, i, svn_sort__item_t);
      key = item.key;
      klen = item.klen;

      child_abspath = svn_dirent_join(local_abspath, key, iterpool);
      child_dirent = apr_hash_get(dirents, key, klen);
      child_info = apr_hash_get(nodes, key, klen);

      SVN_ERR(one_child_status(wb,
                               child_abspath,
                               local_abspath,
                               child_info,
                               child_dirent,
                               dir_repos_root_url,
                               dir_repos_relpath,
                               dir_repos_uuid,
                               apr_hash_get(conflicts, key, klen) != NULL,
                               &collected_ignore_patterns,
                               ignore_patterns,
                               depth,
                               get_all,
                               no_ignore,
                               status_func,
                               status_baton,
                               cancel_func,
                               cancel_baton,
                               scratch_pool,
                               iterpool));
    }

  /* Destroy our subpools. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Send an svn_wc_status3_t * structure for the versioned file, or for the
 * unversioned file or directory, LOCAL_ABSPATH, which is not ignored (an
 * explicit target). Does not recurse.
 *
 * INFO should reflect LOCAL_ABSPATH's information, but should be NULL for
 * unversioned nodes. An unversioned and tree-conflicted node however should
 * pass a non-NULL INFO as returned by read_info() (INFO->CONFLICTED = TRUE).
 *
 * DIRENT should reflect LOCAL_ABSPATH.
 *
 * All allocations made in SCRATCH_POOL.
 *
 * The remaining parameters correspond to get_dir_status(). */
static svn_error_t *
get_child_status(const struct walk_status_baton *wb,
                 const char *local_abspath,
                 const struct svn_wc__db_info_t *info,
                 const svn_io_dirent2_t *dirent,
                 const apr_array_header_t *ignore_patterns,
                 svn_boolean_t get_all,
                 svn_wc_status_func4_t status_func,
                 void *status_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const char *dir_repos_root_url;
  const char *dir_repos_relpath;
  const char *dir_repos_uuid;
  const struct svn_wc__db_info_t *dir_info;
  apr_array_header_t *collected_ignore_patterns = NULL;
  const char *parent_abspath = svn_dirent_dirname(local_abspath,
                                                  scratch_pool);

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (dirent->kind == svn_node_none)
    dirent = NULL;

  SVN_ERR(svn_wc__db_read_single_info(&dir_info,
                                      wb->db, parent_abspath,
                                      !wb->check_working_copy,
                                      scratch_pool, scratch_pool));

  SVN_ERR(get_repos_root_url_relpath(&dir_repos_relpath, &dir_repos_root_url,
                                     &dir_repos_uuid, dir_info,
                                     NULL, NULL, NULL,
                                     wb->db, parent_abspath,
                                     scratch_pool, scratch_pool));

  /* An unversioned node with a tree conflict will see an INFO != NULL here,
   * in which case the FALSE passed for UNVERSIONED_TREE_CONFLICTED has no
   * effect and INFO->CONFLICTED counts.
   * ### Maybe svn_wc__db_read_children_info() and read_info() should be more
   * ### alike? */
  SVN_ERR(one_child_status(wb,
                           local_abspath,
                           parent_abspath,
                           info,
                           dirent,
                           dir_repos_root_url,
                           dir_repos_relpath,
                           dir_repos_uuid,
                           FALSE, /* unversioned_tree_conflicted */
                           &collected_ignore_patterns,
                           ignore_patterns,
                           svn_depth_empty,
                           get_all,
                           TRUE, /* no_ignore. This is an explicit target. */
                           status_func,
                           status_baton,
                           cancel_func,
                           cancel_baton,
                           scratch_pool,
                           scratch_pool));
  return SVN_NO_ERROR;
}



/*** Helpers ***/

/* A faux status callback function for stashing STATUS item in an hash
   (which is the BATON), keyed on PATH.  This implements the
   svn_wc_status_func4_t interface. */
static svn_error_t *
hash_stash(void *baton,
           const char *path,
           const svn_wc_status3_t *status,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *stat_hash = baton;
  apr_pool_t *hash_pool = apr_hash_pool_get(stat_hash);
  void *new_status = svn_wc_dup_status3(status, hash_pool);
  const svn_wc__internal_status_t *old_status = (const void*)status;

  /* Copy the internal/private data. */
  svn_wc__internal_status_t *is = new_status;
  is->has_descendants = old_status->has_descendants;
  is->op_root = old_status->op_root;

  assert(! svn_hash_gets(stat_hash, path));
  svn_hash_sets(stat_hash, apr_pstrdup(hash_pool, path), new_status);

  return SVN_NO_ERROR;
}


/* Look up the key PATH in BATON->STATII.  IS_DIR_BATON indicates whether
   baton is a struct *dir_baton or struct *file_baton.  If the value doesn't
   yet exist, and the REPOS_NODE_STATUS indicates that this is an addition,
   create a new status struct using the hash's pool.

   If IS_DIR_BATON is true, THIS_DIR_BATON is a *dir_baton cotaining the out
   of date (ood) information we want to set in BATON.  This is necessary
   because this function tweaks the status of out-of-date directories
   (BATON == THIS_DIR_BATON) and out-of-date directories' parents
   (BATON == THIS_DIR_BATON->parent_baton).  In the latter case THIS_DIR_BATON
   contains the ood info we want to bubble up to ancestor directories so these
   accurately reflect the fact they have an ood descendant.

   Merge REPOS_NODE_STATUS, REPOS_TEXT_STATUS and REPOS_PROP_STATUS into the
   status structure's "network" fields.

   Iff IS_DIR_BATON is true, DELETED_REV is used as follows, otherwise it
   is ignored:

       If REPOS_NODE_STATUS is svn_wc_status_deleted then DELETED_REV is
       optionally the revision path was deleted, in all other cases it must
       be set to SVN_INVALID_REVNUM.  If DELETED_REV is not
       SVN_INVALID_REVNUM and REPOS_TEXT_STATUS is svn_wc_status_deleted,
       then use DELETED_REV to set PATH's ood_last_cmt_rev field in BATON.
       If DELETED_REV is SVN_INVALID_REVNUM and REPOS_NODE_STATUS is
       svn_wc_status_deleted, set PATH's ood_last_cmt_rev to its parent's
       ood_last_cmt_rev value - see comment below.

   If a new struct was added, set the repos_lock to REPOS_LOCK. */
static svn_error_t *
tweak_statushash(void *baton,
                 void *this_dir_baton,
                 svn_boolean_t is_dir_baton,
                 svn_wc__db_t *db,
                 svn_boolean_t check_working_copy,
                 const char *local_abspath,
                 enum svn_wc_status_kind repos_node_status,
                 enum svn_wc_status_kind repos_text_status,
                 enum svn_wc_status_kind repos_prop_status,
                 svn_revnum_t deleted_rev,
                 const svn_lock_t *repos_lock,
                 apr_pool_t *scratch_pool)
{
  svn_wc_status3_t *statstruct;
  apr_pool_t *pool;
  apr_hash_t *statushash;

  if (is_dir_baton)
    statushash = ((struct dir_baton *) baton)->statii;
  else
    statushash = ((struct file_baton *) baton)->dir_baton->statii;
  pool = apr_hash_pool_get(statushash);

  /* Is PATH already a hash-key? */
  statstruct = svn_hash_gets(statushash, local_abspath);

  /* If not, make it so. */
  if (! statstruct)
    {
      svn_wc__internal_status_t *i_stat;
      /* If this item isn't being added, then we're most likely
         dealing with a non-recursive (or at least partially
         non-recursive) working copy.  Due to bugs in how the client
         reports the state of non-recursive working copies, the
         repository can send back responses about paths that don't
         even exist locally.  Our best course here is just to ignore
         those responses.  After all, if the client had reported
         correctly in the first, that path would either be mentioned
         as an 'add' or not mentioned at all, depending on how we
         eventually fix the bugs in non-recursivity.  See issue
         #2122 for details. */
      if (repos_node_status != svn_wc_status_added)
        return SVN_NO_ERROR;

      /* Use the public API to get a statstruct, and put it into the hash. */
      SVN_ERR(internal_status(&i_stat, db, local_abspath,
                              check_working_copy, pool, scratch_pool));
      statstruct = &i_stat->s;
      statstruct->repos_lock = repos_lock;
      svn_hash_sets(statushash, apr_pstrdup(pool, local_abspath), statstruct);
    }

  /* Merge a repos "delete" + "add" into a single "replace". */
  if ((repos_node_status == svn_wc_status_added)
      && (statstruct->repos_node_status == svn_wc_status_deleted))
    repos_node_status = svn_wc_status_replaced;

  /* Tweak the structure's repos fields. */
  if (repos_node_status)
    statstruct->repos_node_status = repos_node_status;
  if (repos_text_status)
    statstruct->repos_text_status = repos_text_status;
  if (repos_prop_status)
    statstruct->repos_prop_status = repos_prop_status;

  /* Copy out-of-date info. */
  if (is_dir_baton)
    {
      struct dir_baton *b = this_dir_baton;

      if (!statstruct->repos_relpath && b->repos_relpath)
        {
          if (statstruct->repos_node_status == svn_wc_status_deleted)
            {
              /* When deleting PATH, BATON is for PATH's parent,
                 so we must construct PATH's real statstruct->url. */
              statstruct->repos_relpath =
                            svn_relpath_join(b->repos_relpath,
                                             svn_dirent_basename(local_abspath,
                                                                 NULL),
                                             pool);
            }
          else
            statstruct->repos_relpath = apr_pstrdup(pool, b->repos_relpath);

          statstruct->repos_root_url =
                              b->edit_baton->anchor_status->s.repos_root_url;
          statstruct->repos_uuid =
                              b->edit_baton->anchor_status->s.repos_uuid;
        }

      /* The last committed date, and author for deleted items
         isn't available. */
      if (statstruct->repos_node_status == svn_wc_status_deleted)
        {
          statstruct->ood_kind = statstruct->kind;

          /* Pre 1.5 servers don't provide the revision a path was deleted.
             So we punt and use the last committed revision of the path's
             parent, which has some chance of being correct.  At worse it
             is a higher revision than the path was deleted, but this is
             better than nothing... */
          if (deleted_rev == SVN_INVALID_REVNUM)
            statstruct->ood_changed_rev =
              ((struct dir_baton *) baton)->ood_changed_rev;
          else
            statstruct->ood_changed_rev = deleted_rev;
        }
      else
        {
          statstruct->ood_kind = b->ood_kind;
          statstruct->ood_changed_rev = b->ood_changed_rev;
          statstruct->ood_changed_date = b->ood_changed_date;
          if (b->ood_changed_author)
            statstruct->ood_changed_author =
              apr_pstrdup(pool, b->ood_changed_author);
        }

    }
  else
    {
      struct file_baton *b = baton;
      statstruct->ood_changed_rev = b->ood_changed_rev;
      statstruct->ood_changed_date = b->ood_changed_date;
      if (!statstruct->repos_relpath && b->repos_relpath)
        {
          statstruct->repos_relpath = apr_pstrdup(pool, b->repos_relpath);
          statstruct->repos_root_url =
                          b->edit_baton->anchor_status->s.repos_root_url;
          statstruct->repos_uuid =
                          b->edit_baton->anchor_status->s.repos_uuid;
        }
      statstruct->ood_kind = b->ood_kind;
      if (b->ood_changed_author)
        statstruct->ood_changed_author =
          apr_pstrdup(pool, b->ood_changed_author);
    }
  return SVN_NO_ERROR;
}

/* Returns the URL for DB */
static const char *
find_dir_repos_relpath(const struct dir_baton *db, apr_pool_t *pool)
{
  /* If we have no name, we're the root, return the anchor URL. */
  if (! db->name)
    return db->edit_baton->anchor_status->s.repos_relpath;
  else
    {
      const char *repos_relpath;
      struct dir_baton *pb = db->parent_baton;
      const svn_wc_status3_t *status = svn_hash_gets(pb->statii,
                                                     db->local_abspath);
      /* Note that status->repos_relpath could be NULL in the case of a missing
       * directory, which means we need to recurse up another level to get
       * a useful relpath. */
      if (status && status->repos_relpath)
        return status->repos_relpath;

      repos_relpath = find_dir_repos_relpath(pb, pool);
      return svn_relpath_join(repos_relpath, db->name, pool);
    }
}



/* Create a new dir_baton for subdir PATH. */
static svn_error_t *
make_dir_baton(void **dir_baton,
               const char *path,
               struct edit_baton *edit_baton,
               struct dir_baton *parent_baton,
               apr_pool_t *result_pool)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = edit_baton;
  struct dir_baton *d;
  const char *local_abspath;
  const svn_wc__internal_status_t *status_in_parent;
  apr_pool_t *dir_pool;

  if (parent_baton)
    dir_pool = svn_pool_create(parent_baton->pool);
  else
    dir_pool = svn_pool_create(result_pool);

  d = apr_pcalloc(dir_pool, sizeof(*d));

  SVN_ERR_ASSERT(path || (! pb));

  /* Construct the absolute path of this directory. */
  if (pb)
    local_abspath = svn_dirent_join(eb->anchor_abspath, path, dir_pool);
  else
    local_abspath = eb->anchor_abspath;

  /* Finish populating the baton members. */
  d->pool = dir_pool;
  d->local_abspath = local_abspath;
  d->name = path ? svn_dirent_basename(path, dir_pool) : NULL;
  d->edit_baton = edit_baton;
  d->parent_baton = parent_baton;
  d->statii = apr_hash_make(dir_pool);
  d->ood_changed_rev = SVN_INVALID_REVNUM;
  d->ood_changed_date = 0;
  d->repos_relpath = find_dir_repos_relpath(d, dir_pool);
  d->ood_kind = svn_node_dir;
  d->ood_changed_author = NULL;

  if (pb)
    {
      if (pb->excluded)
        d->excluded = TRUE;
      else if (pb->depth == svn_depth_immediates)
        d->depth = svn_depth_empty;
      else if (pb->depth == svn_depth_files || pb->depth == svn_depth_empty)
        d->excluded = TRUE;
      else if (pb->depth == svn_depth_unknown)
        /* This is only tentative, it can be overridden from d's entry
           later. */
        d->depth = svn_depth_unknown;
      else
        d->depth = svn_depth_infinity;
    }
  else
    {
      d->depth = eb->default_depth;
    }

  /* Get the status for this path's children.  Of course, we only want
     to do this if the path is versioned as a directory. */
  if (pb)
    status_in_parent = svn_hash_gets(pb->statii, d->local_abspath);
  else
    status_in_parent = eb->anchor_status;

  if (status_in_parent
      && (status_in_parent->has_descendants)
      && (! d->excluded)
      && (d->depth == svn_depth_unknown
          || d->depth == svn_depth_infinity
          || d->depth == svn_depth_files
          || d->depth == svn_depth_immediates)
          )
    {
      const svn_wc_status3_t *this_dir_status;
      const apr_array_header_t *ignores = eb->ignores;

      SVN_ERR(get_dir_status(&eb->wb, local_abspath, TRUE,
                             status_in_parent->s.repos_root_url,
                             NULL /*parent_repos_relpath*/,
                             status_in_parent->s.repos_uuid,
                             NULL,
                             NULL /* dirent */, ignores,
                             d->depth == svn_depth_files
                                      ? svn_depth_files
                                      : svn_depth_immediates,
                             TRUE, TRUE,
                             hash_stash, d->statii,
                             eb->cancel_func, eb->cancel_baton,
                             dir_pool));

      /* If we found a depth here, it should govern. */
      this_dir_status = svn_hash_gets(d->statii, d->local_abspath);
      if (this_dir_status && this_dir_status->versioned
          && (d->depth == svn_depth_unknown
              || d->depth > status_in_parent->s.depth))
        {
          d->depth = this_dir_status->depth;
        }
    }

  *dir_baton = d;
  return SVN_NO_ERROR;
}


/* Make a file baton, using a new subpool of PARENT_DIR_BATON's pool.
   NAME is just one component, not a path. */
static struct file_baton *
make_file_baton(struct dir_baton *parent_dir_baton,
                const char *path,
                apr_pool_t *pool)
{
  struct dir_baton *pb = parent_dir_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct file_baton *f = apr_pcalloc(pool, sizeof(*f));

  /* Finish populating the baton members. */
  f->local_abspath = svn_dirent_join(eb->anchor_abspath, path, pool);
  f->name = svn_dirent_basename(f->local_abspath, NULL);
  f->pool = pool;
  f->dir_baton = pb;
  f->edit_baton = eb;
  f->ood_changed_rev = SVN_INVALID_REVNUM;
  f->ood_changed_date = 0;
  f->repos_relpath = svn_relpath_join(find_dir_repos_relpath(pb, pool),
                                      f->name, pool);
  f->ood_kind = svn_node_file;
  f->ood_changed_author = NULL;
  return f;
}


/**
 * Return a boolean answer to the question "Is @a status something that
 * should be reported?".  @a no_ignore and @a get_all are the same as
 * svn_wc_get_status_editor4().
 *
 * This implementation should match the filter in assemble_status()
 */
static svn_boolean_t
is_sendable_status(const svn_wc__internal_status_t *i_status,
                   svn_boolean_t no_ignore,
                   svn_boolean_t get_all)
{
  const svn_wc_status3_t *status = &i_status->s;
  /* If the repository status was touched at all, it's interesting. */
  if (status->repos_node_status != svn_wc_status_none)
    return TRUE;

  /* If there is a lock in the repository, send it. */
  if (status->repos_lock)
    return TRUE;

  if (status->conflicted)
    return TRUE;

  /* If the item is ignored, and we don't want ignores, skip it. */
  if ((status->node_status == svn_wc_status_ignored) && (! no_ignore))
    return FALSE;

  /* If we want everything, we obviously want this single-item subset
     of everything. */
  if (get_all)
    return TRUE;

  /* If the item is unversioned, display it. */
  if (status->node_status == svn_wc_status_unversioned)
    return TRUE;

  /* If the text, property or tree state is interesting, send it. */
  if ((status->node_status != svn_wc_status_none)
       && (status->node_status != svn_wc_status_normal)
       && !(status->node_status == svn_wc_status_deleted
            && !i_status->op_root))
    return TRUE;

  /* If it's switched, send it. */
  if (status->switched)
    return TRUE;

  /* If there is a lock token, send it. */
  if (status->versioned && status->lock)
    return TRUE;

  /* If the entry is associated with a changelist, send it. */
  if (status->changelist)
    return TRUE;

  if (status->moved_to_abspath)
    return TRUE;

  /* Otherwise, don't send it. */
  return FALSE;
}


/* Baton for mark_status. */
struct status_baton
{
  svn_wc_status_func4_t real_status_func;  /* real status function */
  void *real_status_baton;                 /* real status baton */
};

/* A status callback function which wraps the *real* status
   function/baton.   It simply sets the "repos_node_status" field of the
   STATUS to svn_wc_status_deleted and passes it off to the real
   status func/baton. Implements svn_wc_status_func4_t */
static svn_error_t *
mark_deleted(void *baton,
             const char *local_abspath,
             const svn_wc_status3_t *status,
             apr_pool_t *scratch_pool)
{
  struct status_baton *sb = baton;
  svn_wc_status3_t *new_status = svn_wc_dup_status3(status, scratch_pool);
  new_status->repos_node_status = svn_wc_status_deleted;
  return sb->real_status_func(sb->real_status_baton, local_abspath,
                              new_status, scratch_pool);
}


/* Handle a directory's STATII hash.  EB is the edit baton.  DIR_PATH
   and DIR_ENTRY are the on-disk path and entry, respectively, for the
   directory itself.  Descend into subdirectories according to DEPTH.
   Also, if DIR_WAS_DELETED is set, each status that is reported
   through this function will have its repos_text_status field showing
   a deletion.  Use POOL for all allocations. */
static svn_error_t *
handle_statii(struct edit_baton *eb,
              const char *dir_repos_root_url,
              const char *dir_repos_relpath,
              const char *dir_repos_uuid,
              apr_hash_t *statii,
              svn_boolean_t dir_was_deleted,
              svn_depth_t depth,
              apr_pool_t *pool)
{
  const apr_array_header_t *ignores = eb->ignores;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_wc_status_func4_t status_func = eb->status_func;
  void *status_baton = eb->status_baton;
  struct status_baton sb;

  if (dir_was_deleted)
    {
      sb.real_status_func = eb->status_func;
      sb.real_status_baton = eb->status_baton;
      status_func = mark_deleted;
      status_baton = &sb;
    }

  /* Loop over all the statii still in our hash, handling each one. */
  for (hi = apr_hash_first(pool, statii); hi; hi = apr_hash_next(hi))
    {
      const char *local_abspath = apr_hash_this_key(hi);
      svn_wc__internal_status_t *status = apr_hash_this_val(hi);

      /* Clear the subpool. */
      svn_pool_clear(iterpool);

      /* Now, handle the status.  We don't recurse for svn_depth_immediates
         because we already have the subdirectories' statii. */
      if (status->has_descendants
          && (depth == svn_depth_unknown
              || depth == svn_depth_infinity))
        {
          SVN_ERR(get_dir_status(&eb->wb,
                                 local_abspath, TRUE,
                                 dir_repos_root_url, dir_repos_relpath,
                                 dir_repos_uuid,
                                 NULL,
                                 NULL /* dirent */,
                                 ignores, depth, eb->get_all, eb->no_ignore,
                                 status_func, status_baton,
                                 eb->cancel_func, eb->cancel_baton,
                                 iterpool));
        }
      if (dir_was_deleted)
        status->s.repos_node_status = svn_wc_status_deleted;
      if (is_sendable_status(status, eb->no_ignore, eb->get_all))
        SVN_ERR((eb->status_func)(eb->status_baton, local_abspath, &status->s,
                                  iterpool));
    }

  /* Destroy the subpool. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/*** The callbacks we'll plug into an svn_delta_editor_t structure. ***/

/* An svn_delta_editor_t function. */
static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  *(eb->target_revision) = target_revision;
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **dir_baton)
{
  struct edit_baton *eb = edit_baton;
  eb->root_opened = TRUE;
  return make_dir_baton(dir_baton, NULL, eb, NULL, pool);
}


/* An svn_delta_editor_t function. */
static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;
  const char *local_abspath = svn_dirent_join(eb->anchor_abspath, path, pool);

  /* Note:  when something is deleted, it's okay to tweak the
     statushash immediately.  No need to wait until close_file or
     close_dir, because there's no risk of having to honor the 'added'
     flag.  We already know this item exists in the working copy. */
  SVN_ERR(tweak_statushash(db, db, TRUE, eb->db, eb->wb.check_working_copy,
                           local_abspath,
                           svn_wc_status_deleted, 0, 0, revision, NULL, pool));

  /* Mark the parent dir -- it lost an entry (unless that parent dir
     is the root node and we're not supposed to report on the root
     node).  */
  if (db->parent_baton && (! *eb->target_basename))
    SVN_ERR(tweak_statushash(db->parent_baton, db, TRUE,
                             eb->db, eb->wb.check_working_copy,
                             db->local_abspath,
                             svn_wc_status_modified, svn_wc_status_modified,
                             0, SVN_INVALID_REVNUM, NULL, pool));

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db;

  SVN_ERR(make_dir_baton(child_baton, path, eb, pb, pool));

  /* Make this dir as added. */
  new_db = *child_baton;
  new_db->added = TRUE;

  /* Mark the parent as changed;  it gained an entry. */
  pb->text_changed = TRUE;

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  return make_dir_baton(child_baton, path, pb->edit_baton, pb, pool);
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  if (svn_wc_is_normal_prop(name))
    db->prop_changed = TRUE;

  /* Note any changes to the repository. */
  if (value != NULL)
    {
      if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV) == 0)
        db->ood_changed_rev = SVN_STR_TO_REV(value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR) == 0)
        db->ood_changed_author = apr_pstrdup(db->pool, value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
        {
          apr_time_t tm;
          SVN_ERR(svn_time_from_cstring(&tm, value->data, db->pool));
          db->ood_changed_date = tm;
        }
    }

  return SVN_NO_ERROR;
}



/* An svn_delta_editor_t function. */
static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct dir_baton *pb = db->parent_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_pool_t *scratch_pool = db->pool;

  /* If nothing has changed and directory has no out of
     date descendants, return. */
  if (db->added || db->prop_changed || db->text_changed
      || db->ood_changed_rev != SVN_INVALID_REVNUM)
    {
      enum svn_wc_status_kind repos_node_status;
      enum svn_wc_status_kind repos_text_status;
      enum svn_wc_status_kind repos_prop_status;

      /* If this is a new directory, add it to the statushash. */
      if (db->added)
        {
          repos_node_status = svn_wc_status_added;
          repos_text_status = svn_wc_status_none;
          repos_prop_status = db->prop_changed ? svn_wc_status_added
                              : svn_wc_status_none;
        }
      else
        {
          repos_node_status = (db->text_changed || db->prop_changed)
                                       ? svn_wc_status_modified
                                       : svn_wc_status_none;
          repos_text_status = db->text_changed ? svn_wc_status_modified
                              : svn_wc_status_none;
          repos_prop_status = db->prop_changed ? svn_wc_status_modified
                              : svn_wc_status_none;
        }

      /* Maybe add this directory to its parent's status hash.  Note
         that tweak_statushash won't do anything if repos_text_status
         is not svn_wc_status_added. */
      if (pb)
        {
          /* ### When we add directory locking, we need to find a
             ### directory lock here. */
          SVN_ERR(tweak_statushash(pb, db, TRUE,
                                   eb->db,  eb->wb.check_working_copy,
                                   db->local_abspath,
                                   repos_node_status, repos_text_status,
                                   repos_prop_status, SVN_INVALID_REVNUM, NULL,
                                   scratch_pool));
        }
      else
        {
          /* We're editing the root dir of the WC.  As its repos
             status info isn't otherwise set, set it directly to
             trigger invocation of the status callback below. */
          eb->anchor_status->s.repos_node_status = repos_node_status;
          eb->anchor_status->s.repos_prop_status = repos_prop_status;
          eb->anchor_status->s.repos_text_status = repos_text_status;

          /* If the root dir is out of date set the ood info directly too. */
          if (db->ood_changed_rev != eb->anchor_status->s.revision)
            {
              eb->anchor_status->s.ood_changed_rev = db->ood_changed_rev;
              eb->anchor_status->s.ood_changed_date = db->ood_changed_date;
              eb->anchor_status->s.ood_kind = db->ood_kind;
              eb->anchor_status->s.ood_changed_author =
                apr_pstrdup(pool, db->ood_changed_author);
            }
        }
    }

  /* Handle this directory's statuses, and then note in the parent
     that this has been done. */
  if (pb && ! db->excluded)
    {
      svn_boolean_t was_deleted = FALSE;
      svn_wc__internal_status_t *dir_status;

      /* See if the directory was deleted or replaced. */
      dir_status = svn_hash_gets(pb->statii, db->local_abspath);
      if (dir_status &&
          ((dir_status->s.repos_node_status == svn_wc_status_deleted)
           || (dir_status->s.repos_node_status == svn_wc_status_replaced)))
        was_deleted = TRUE;

      /* Now do the status reporting. */
      SVN_ERR(handle_statii(eb,
                            dir_status ? dir_status->s.repos_root_url : NULL,
                            dir_status ? dir_status->s.repos_relpath : NULL,
                            dir_status ? dir_status->s.repos_uuid : NULL,
                            db->statii, was_deleted, db->depth, scratch_pool));
      if (dir_status && is_sendable_status(dir_status, eb->no_ignore,
                                           eb->get_all))
        SVN_ERR((eb->status_func)(eb->status_baton, db->local_abspath,
                                  &dir_status->s, scratch_pool));
      svn_hash_sets(pb->statii, db->local_abspath, NULL);
    }
  else if (! pb)
    {
      /* If this is the top-most directory, and the operation had a
         target, we should only report the target. */
      if (*eb->target_basename)
        {
          const svn_wc__internal_status_t *tgt_status;

          tgt_status = svn_hash_gets(db->statii, eb->target_abspath);
          if (tgt_status)
            {
              if (tgt_status->has_descendants)
                {
                  SVN_ERR(get_dir_status(&eb->wb,
                                         eb->target_abspath, TRUE,
                                         NULL, NULL, NULL, NULL,
                                         NULL /* dirent */,
                                         eb->ignores,
                                         eb->default_depth,
                                         eb->get_all, eb->no_ignore,
                                         eb->status_func, eb->status_baton,
                                         eb->cancel_func, eb->cancel_baton,
                                         scratch_pool));
                }
              if (is_sendable_status(tgt_status, eb->no_ignore, eb->get_all))
                SVN_ERR((eb->status_func)(eb->status_baton, eb->target_abspath,
                                          &tgt_status->s, scratch_pool));
            }
        }
      else
        {
          /* Otherwise, we report on all our children and ourself.
             Note that our directory couldn't have been deleted,
             because it is the root of the edit drive. */
          SVN_ERR(handle_statii(eb,
                                eb->anchor_status->s.repos_root_url,
                                eb->anchor_status->s.repos_relpath,
                                eb->anchor_status->s.repos_uuid,
                                db->statii, FALSE, eb->default_depth,
                                scratch_pool));
          if (is_sendable_status(eb->anchor_status, eb->no_ignore,
                                 eb->get_all))
            SVN_ERR((eb->status_func)(eb->status_baton, db->local_abspath,
                                      &eb->anchor_status->s, scratch_pool));
          eb->anchor_status = NULL;
        }
    }

  svn_pool_clear(scratch_pool); /* Clear baton and its pool */

  return SVN_NO_ERROR;
}



/* An svn_delta_editor_t function. */
static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton(pb, path, pool);

  /* Mark parent dir as changed */
  pb->text_changed = TRUE;

  /* Make this file as added. */
  new_fb->added = TRUE;

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct file_baton *new_fb = make_file_baton(pb, path, pool);

  *file_baton = new_fb;
  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct file_baton *fb = file_baton;

  /* Mark file as having textual mods. */
  fb->text_changed = TRUE;

  /* Send back a NULL window handler -- we don't need the actual diffs. */
  *handler_baton = NULL;
  *handler = svn_delta_noop_window_handler;

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  if (svn_wc_is_normal_prop(name))
    fb->prop_changed = TRUE;

  /* Note any changes to the repository. */
  if (value != NULL)
    {
      if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_REV) == 0)
        fb->ood_changed_rev = SVN_STR_TO_REV(value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_LAST_AUTHOR) == 0)
        fb->ood_changed_author = apr_pstrdup(fb->dir_baton->pool,
                                              value->data);
      else if (strcmp(name, SVN_PROP_ENTRY_COMMITTED_DATE) == 0)
        {
          apr_time_t tm;
          SVN_ERR(svn_time_from_cstring(&tm, value->data,
                                        fb->dir_baton->pool));
          fb->ood_changed_date = tm;
        }
    }

  return SVN_NO_ERROR;
}


/* An svn_delta_editor_t function. */
static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,  /* ignored, as we receive no data */
           apr_pool_t *pool)
{
  struct file_baton *fb = file_baton;
  enum svn_wc_status_kind repos_node_status;
  enum svn_wc_status_kind repos_text_status;
  enum svn_wc_status_kind repos_prop_status;
  const svn_lock_t *repos_lock = NULL;

  /* If nothing has changed, return. */
  if (! (fb->added || fb->prop_changed || fb->text_changed))
    return SVN_NO_ERROR;

  /* If this is a new file, add it to the statushash. */
  if (fb->added)
    {
      repos_node_status = svn_wc_status_added;
      repos_text_status = fb->text_changed ? svn_wc_status_modified
                                           : 0 /* don't tweak */;
      repos_prop_status = fb->prop_changed ? svn_wc_status_modified
                                           : 0 /* don't tweak */;

      if (fb->edit_baton->wb.repos_locks)
        {
          const char *dir_repos_relpath = find_dir_repos_relpath(fb->dir_baton,
                                                                 pool);

          /* repos_lock still uses the deprecated filesystem absolute path
             format */
          const char *repos_relpath = svn_relpath_join(dir_repos_relpath,
                                                       fb->name, pool);

          repos_lock = svn_hash_gets(fb->edit_baton->wb.repos_locks,
                                     svn_fspath__join("/", repos_relpath,
                                                      pool));
        }
    }
  else
    {
      repos_node_status = (fb->text_changed || fb->prop_changed)
                                 ? svn_wc_status_modified
                                 : 0 /* don't tweak */;
      repos_text_status = fb->text_changed ? svn_wc_status_modified
                                           : 0 /* don't tweak */;
      repos_prop_status = fb->prop_changed ? svn_wc_status_modified
                                           : 0 /* don't tweak */;
    }

  return tweak_statushash(fb, NULL, FALSE, fb->edit_baton->db,
                          fb->edit_baton->wb.check_working_copy,
                          fb->local_abspath, repos_node_status,
                          repos_text_status, repos_prop_status,
                          SVN_INVALID_REVNUM, repos_lock, pool);
}

/* An svn_delta_editor_t function. */
static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  /* If we get here and the root was not opened as part of the edit,
     we need to transmit statuses for everything.  Otherwise, we
     should be done. */
  if (eb->root_opened)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__internal_walk_status(eb->db,
                                       eb->target_abspath,
                                       eb->default_depth,
                                       eb->get_all,
                                       eb->no_ignore,
                                       FALSE,
                                       eb->ignores,
                                       eb->status_func,
                                       eb->status_baton,
                                       eb->cancel_func,
                                       eb->cancel_baton,
                                       pool));

  return SVN_NO_ERROR;
}



/*** Public API ***/

svn_error_t *
svn_wc__get_status_editor(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t check_working_copy,
                          svn_boolean_t no_ignore,
                          svn_boolean_t depth_as_sticky,
                          svn_boolean_t server_performs_filtering,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func4_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  struct edit_baton *eb;
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(result_pool);
  void *inner_baton;
  struct svn_wc__shim_fetch_baton_t *sfb;
  const svn_delta_editor_t *inner_editor;
  svn_delta_shim_callbacks_t *shim_callbacks =
                                svn_delta_shim_callbacks_default(result_pool);

  /* Construct an edit baton. */
  eb = apr_pcalloc(result_pool, sizeof(*eb));
  eb->default_depth     = depth;
  eb->target_revision   = edit_revision;
  eb->db                = wc_ctx->db;
  eb->get_all           = get_all;
  eb->no_ignore         = no_ignore;
  eb->status_func       = status_func;
  eb->status_baton      = status_baton;
  eb->cancel_func       = cancel_func;
  eb->cancel_baton      = cancel_baton;
  eb->anchor_abspath    = apr_pstrdup(result_pool, anchor_abspath);
  eb->target_abspath    = svn_dirent_join(anchor_abspath, target_basename,
                                          result_pool);

  eb->target_basename   = apr_pstrdup(result_pool, target_basename);
  eb->root_opened       = FALSE;

  eb->wb.db               = wc_ctx->db;
  eb->wb.target_abspath   = eb->target_abspath;
  eb->wb.ignore_text_mods = !check_working_copy;
  eb->wb.check_working_copy = check_working_copy;
  eb->wb.repos_locks      = NULL;
  eb->wb.repos_root       = NULL;

  SVN_ERR(svn_wc__db_externals_defined_below(&eb->wb.externals,
                                             wc_ctx->db, eb->target_abspath,
                                             result_pool, scratch_pool));

  /* Use the caller-provided ignore patterns if provided; the build-time
     configured defaults otherwise. */
  if (ignore_patterns)
    {
      eb->ignores = ignore_patterns;
    }
  else
    {
      apr_array_header_t *ignores;

      SVN_ERR(svn_wc_get_default_ignores(&ignores, NULL, result_pool));
      eb->ignores = ignores;
    }

  /* The edit baton's status structure maps to PATH, and the editor
     have to be aware of whether that is the anchor or the target. */
  SVN_ERR(internal_status(&(eb->anchor_status), wc_ctx->db, anchor_abspath,
                          check_working_copy, result_pool, scratch_pool));

  /* Construct an editor. */
  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->close_edit = close_edit;

  inner_editor = tree_editor;
  inner_baton = eb;

  if (!server_performs_filtering
      && !depth_as_sticky)
    SVN_ERR(svn_wc__ambient_depth_filter_editor(&inner_editor,
                                                &inner_baton,
                                                wc_ctx->db,
                                                anchor_abspath,
                                                target_basename,
                                                inner_editor,
                                                inner_baton,
                                                result_pool));

  /* Conjoin a cancellation editor with our status editor. */
  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            inner_editor, inner_baton,
                                            editor, edit_baton,
                                            result_pool));

  if (set_locks_baton)
    *set_locks_baton = eb;

  sfb = apr_palloc(result_pool, sizeof(*sfb));
  sfb->db = wc_ctx->db;
  sfb->base_abspath = eb->anchor_abspath;
  sfb->fetch_base = FALSE;

  shim_callbacks->fetch_kind_func = svn_wc__fetch_kind_func;
  shim_callbacks->fetch_props_func = svn_wc__fetch_props_func;
  shim_callbacks->fetch_base_func = svn_wc__fetch_base_func;
  shim_callbacks->fetch_baton = sfb;

  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   NULL, NULL, shim_callbacks,
                                   result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Like svn_io_stat_dirent, but works case sensitive inside working
   copies. Before 1.8 we handled this with a selection filter inside
   a directory */
static svn_error_t *
stat_wc_dirent_case_sensitive(const svn_io_dirent2_t **dirent,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_boolean_t is_wcroot;

  /* The wcroot is "" inside the wc; handle it as not in the wc, as
     the case of the root is indifferent to us. */

  /* Note that for performance this is really just a few hashtable lookups,
     as we just used local_abspath for a db call in both our callers */
  SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, local_abspath,
                               scratch_pool));

  return svn_error_trace(
            svn_io_stat_dirent2(dirent, local_abspath,
                                ! is_wcroot /* verify_truename */,
                                TRUE        /* ignore_enoent */,
                                result_pool, scratch_pool));
}

svn_error_t *
svn_wc__internal_walk_status(svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_depth_t depth,
                             svn_boolean_t get_all,
                             svn_boolean_t no_ignore,
                             svn_boolean_t ignore_text_mods,
                             const apr_array_header_t *ignore_patterns,
                             svn_wc_status_func4_t status_func,
                             void *status_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool)
{
  struct walk_status_baton wb;
  const svn_io_dirent2_t *dirent;
  const struct svn_wc__db_info_t *info;
  svn_error_t *err;

  wb.db = db;
  wb.target_abspath = local_abspath;
  wb.ignore_text_mods = ignore_text_mods;
  wb.check_working_copy = TRUE;
  wb.repos_root = NULL;
  wb.repos_locks = NULL;

  /* Use the caller-provided ignore patterns if provided; the build-time
     configured defaults otherwise. */
  if (!ignore_patterns)
    {
      apr_array_header_t *ignores;

      SVN_ERR(svn_wc_get_default_ignores(&ignores, NULL, scratch_pool));
      ignore_patterns = ignores;
    }

  err = svn_wc__db_read_single_info(&info, db, local_abspath,
                                    FALSE /* base_tree_only */,
                                    scratch_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          info = NULL;
        }
      else
        return svn_error_trace(err);

      wb.externals = apr_hash_make(scratch_pool);

      SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, TRUE,
                                  scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__db_externals_defined_below(&wb.externals,
                                                 db, local_abspath,
                                                 scratch_pool, scratch_pool));

      SVN_ERR(stat_wc_dirent_case_sensitive(&dirent, db, local_abspath,
                                            scratch_pool, scratch_pool));
    }

  if (info
      && info->has_descendants /* is dir, or was dir and has tc descendants */
      && info->status != svn_wc__db_status_not_present
      && info->status != svn_wc__db_status_excluded
      && info->status != svn_wc__db_status_server_excluded)
    {
      SVN_ERR(get_dir_status(&wb,
                             local_abspath,
                             FALSE /* skip_root */,
                             NULL, NULL, NULL,
                             info,
                             dirent,
                             ignore_patterns,
                             depth,
                             get_all,
                             no_ignore,
                             status_func, status_baton,
                             cancel_func, cancel_baton,
                             scratch_pool));
    }
  else
    {
      /* It may be a file or an unversioned item. And this is an explicit
       * target, so no ignoring. An unversioned item (file or dir) shows a
       * status like '?', and can yield a tree conflicted path. */
      err = get_child_status(&wb,
                             local_abspath,
                             info,
                             dirent,
                             ignore_patterns,
                             get_all,
                             status_func, status_baton,
                             cancel_func, cancel_baton,
                             scratch_pool);

      if (!info && err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          /* The parent is also not versioned, but it is not nice to show
             an error about a path a user didn't intend to touch. */
          svn_error_clear(err);
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("The node '%s' was not found."),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));
        }
      SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_walk_status(svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_text_mods,
                   const apr_array_header_t *ignore_patterns,
                   svn_wc_status_func4_t status_func,
                   void *status_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           svn_wc__internal_walk_status(wc_ctx->db,
                                        local_abspath,
                                        depth,
                                        get_all,
                                        no_ignore,
                                        ignore_text_mods,
                                        ignore_patterns,
                                        status_func,
                                        status_baton,
                                        cancel_func,
                                        cancel_baton,
                                        scratch_pool));
}


svn_error_t *
svn_wc_status_set_repos_locks(void *edit_baton,
                              apr_hash_t *locks,
                              const char *repos_root,
                              apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;

  eb->wb.repos_locks = locks;
  eb->wb.repos_root = apr_pstrdup(pool, repos_root);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_get_default_ignores(apr_array_header_t **patterns,
                           apr_hash_t *config,
                           apr_pool_t *pool)
{
  svn_config_t *cfg = config
                      ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG)
                      : NULL;
  const char *val;

  /* Check the Subversion run-time configuration for global ignores.
     If no configuration value exists, we fall back to our defaults. */
  svn_config_get(cfg, &val, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_GLOBAL_IGNORES,
                 SVN_CONFIG_DEFAULT_GLOBAL_IGNORES);
  *patterns = apr_array_make(pool, 16, sizeof(const char *));

  /* Split the patterns on whitespace, and stuff them into *PATTERNS. */
  svn_cstring_split_append(*patterns, val, "\n\r\t\v ", FALSE, pool);
  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
internal_status(svn_wc__internal_status_t **status,
                svn_wc__db_t *db,
                const char *local_abspath,
                svn_boolean_t check_working_copy,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const svn_io_dirent2_t *dirent = NULL;
  const char *parent_repos_relpath;
  const char *parent_repos_root_url;
  const char *parent_repos_uuid;
  const struct svn_wc__db_info_t *info;
  svn_boolean_t is_root = FALSE;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  err = svn_wc__db_read_single_info(&info, db, local_abspath,
                                    !check_working_copy,
                                    scratch_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
      info = NULL;

      if (check_working_copy)
        SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, TRUE,
                                    scratch_pool, scratch_pool));
    }
  else if (check_working_copy)
    SVN_ERR(stat_wc_dirent_case_sensitive(&dirent, db, local_abspath,
                                          scratch_pool, scratch_pool));

  if (!info
      || info->kind == svn_node_unknown
      || info->status == svn_wc__db_status_not_present
      || info->status == svn_wc__db_status_server_excluded
      || info->status == svn_wc__db_status_excluded)
    return svn_error_trace(assemble_unversioned(status,
                                                db, local_abspath,
                                                dirent,
                                                info ? info->conflicted : FALSE,
                                                FALSE /* is_ignored */,
                                                result_pool, scratch_pool));

  if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
    is_root = TRUE;
  else
    SVN_ERR(svn_wc__db_is_wcroot(&is_root, db, local_abspath, scratch_pool));

  /* Even though passing parent_repos_* is not required, assemble_status needs
     these values to determine if a node is switched */
  if (!is_root)
    {
      const char *const parent_abspath = svn_dirent_dirname(local_abspath,
                                                            scratch_pool);
      if (check_working_copy)
        SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL,
                                     &parent_repos_relpath,
                                     &parent_repos_root_url,
                                     &parent_repos_uuid,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     db, parent_abspath,
                                     result_pool, scratch_pool));
      else
        SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL,
                                         &parent_repos_relpath,
                                         &parent_repos_root_url,
                                         &parent_repos_uuid,
                                         NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL, NULL,
                                         db, parent_abspath,
                                         result_pool, scratch_pool));
    }
  else
    {
      parent_repos_root_url = NULL;
      parent_repos_relpath = NULL;
      parent_repos_uuid = NULL;
    }

  return svn_error_trace(assemble_status(status, db, local_abspath,
                                         parent_repos_root_url,
                                         parent_repos_relpath,
                                         parent_repos_uuid,
                                         info,
                                         dirent,
                                         TRUE /* get_all */,
                                         FALSE, check_working_copy,
                                         NULL /* repos_lock */,
                                         result_pool, scratch_pool));
}


svn_error_t *
svn_wc_status3(svn_wc_status3_t **status,
               svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_wc__internal_status_t *stat;
  SVN_ERR(internal_status(&stat, wc_ctx->db, local_abspath,
                          TRUE /* check_working_copy */,
                          result_pool, scratch_pool));
  *status = &stat->s;
  return SVN_NO_ERROR;
}

svn_wc_status3_t *
svn_wc_dup_status3(const svn_wc_status3_t *orig_stat,
                   apr_pool_t *pool)
{
  /* Allocate slightly more room */
  svn_wc__internal_status_t *new_istat = apr_palloc(pool, sizeof(*new_istat));
  svn_wc_status3_t *new_stat = &new_istat->s;

  /* Shallow copy all members. */
  *new_stat = *orig_stat;

  /* Now go back and dup the deep items into this pool. */
  if (orig_stat->repos_lock)
    new_stat->repos_lock = svn_lock_dup(orig_stat->repos_lock, pool);

  if (orig_stat->changed_author)
    new_stat->changed_author = apr_pstrdup(pool, orig_stat->changed_author);

  if (orig_stat->ood_changed_author)
    new_stat->ood_changed_author
      = apr_pstrdup(pool, orig_stat->ood_changed_author);

  if (orig_stat->lock)
    new_stat->lock = svn_lock_dup(orig_stat->lock, pool);

  if (orig_stat->changelist)
    new_stat->changelist
      = apr_pstrdup(pool, orig_stat->changelist);

  if (orig_stat->repos_root_url)
    new_stat->repos_root_url
      = apr_pstrdup(pool, orig_stat->repos_root_url);

  if (orig_stat->repos_relpath)
    new_stat->repos_relpath
      = apr_pstrdup(pool, orig_stat->repos_relpath);

  if (orig_stat->repos_uuid)
    new_stat->repos_uuid
      = apr_pstrdup(pool, orig_stat->repos_uuid);

  if (orig_stat->moved_from_abspath)
    new_stat->moved_from_abspath
      = apr_pstrdup(pool, orig_stat->moved_from_abspath);

  if (orig_stat->moved_to_abspath)
    new_stat->moved_to_abspath
      = apr_pstrdup(pool, orig_stat->moved_to_abspath);

  /* Return the new hotness. */
  return new_stat;
}

svn_error_t *
svn_wc_get_ignores2(apr_array_header_t **patterns,
                    svn_wc_context_t *wc_ctx,
                    const char *local_abspath,
                    apr_hash_t *config,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_array_header_t *default_ignores;

  SVN_ERR(svn_wc_get_default_ignores(&default_ignores, config, scratch_pool));
  return svn_error_trace(collect_ignore_patterns(patterns, wc_ctx->db,
                                                 local_abspath,
                                                 default_ignores,
                                                 result_pool, scratch_pool));
}
