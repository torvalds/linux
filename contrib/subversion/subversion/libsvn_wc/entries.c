/*
 * entries.c :  manipulating the administrative `entries' file.
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

#include <string.h>
#include <assert.h>

#include <apr_strings.h>

#include "svn_error.h"
#include "svn_types.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_string.h"
#include "svn_hash.h"

#include "wc.h"
#include "adm_files.h"
#include "conflicts.h"
#include "entries.h"
#include "lock.h"
#include "tree_conflicts.h"
#include "wc_db.h"
#include "wc-queries.h"  /* for STMT_*  */

#define SVN_WC__I_AM_WC_DB

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_sqlite.h"
#include "token-map.h"

#include "wc_db_private.h"

#define MAYBE_ALLOC(x,p) ((x) ? (x) : apr_pcalloc((p), sizeof(*(x))))


/* Temporary structures which mirror the tables in wc-metadata.sql.
   For detailed descriptions of each field, see that file. */
typedef struct db_node_t {
  apr_int64_t wc_id;
  const char *local_relpath;
  int op_depth;
  apr_int64_t repos_id;
  const char *repos_relpath;
  const char *parent_relpath;
  svn_wc__db_status_t presence;
  svn_revnum_t revision;
  svn_node_kind_t kind;
  svn_checksum_t *checksum;
  svn_filesize_t recorded_size;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  svn_depth_t depth;
  apr_time_t recorded_time;
  apr_hash_t *properties;
  svn_boolean_t file_external;
  apr_array_header_t *inherited_props;
} db_node_t;

typedef struct db_actual_node_t {
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *parent_relpath;
  apr_hash_t *properties;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_working;
  const char *prop_reject;
  const char *changelist;
  /* ### enum for text_mod */
  const char *tree_conflict_data;
} db_actual_node_t;



/*** reading and writing the entries file ***/


/* */
static svn_wc_entry_t *
alloc_entry(apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc(pool, sizeof(*entry));
  entry->revision = SVN_INVALID_REVNUM;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->cmt_rev = SVN_INVALID_REVNUM;
  entry->kind = svn_node_none;
  entry->working_size = SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN;
  entry->depth = svn_depth_infinity;
  entry->file_external_peg_rev.kind = svn_opt_revision_unspecified;
  entry->file_external_rev.kind = svn_opt_revision_unspecified;
  return entry;
}


/* Is the entry in a 'hidden' state in the sense of the 'show_hidden'
 * switches on svn_wc_entries_read(), svn_wc_walk_entries*(), etc.? */
svn_error_t *
svn_wc__entry_is_hidden(svn_boolean_t *hidden, const svn_wc_entry_t *entry)
{
  /* In English, the condition is: "the entry is not present, and I haven't
     scheduled something over the top of it."  */
  if (entry->deleted
      || entry->absent
      || entry->depth == svn_depth_exclude)
    {
      /* These kinds of nodes cannot be marked for deletion (which also
         means no "replace" either).  */
      SVN_ERR_ASSERT(entry->schedule == svn_wc_schedule_add
                     || entry->schedule == svn_wc_schedule_normal);

      /* Hidden if something hasn't been added over it.

         ### is this even possible with absent or excluded nodes?  */
      *hidden = entry->schedule != svn_wc_schedule_add;
    }
  else
    *hidden = FALSE;

  return SVN_NO_ERROR;
}


/* Hit the database to check the file external information for the given
   entry.  The entry will be modified in place. */
static svn_error_t *
check_file_external(svn_wc_entry_t *entry,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    const char *wri_abspath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const char *repos_relpath;
  svn_revnum_t peg_revision;
  svn_revnum_t revision;
  svn_error_t *err;

  err = svn_wc__db_external_read(&status, &kind, NULL, NULL, NULL,
                                 &repos_relpath, &peg_revision, &revision,
                                 db, local_abspath, wri_abspath,
                                 result_pool, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  if (status == svn_wc__db_status_normal
      && kind == svn_node_file)
    {
      entry->file_external_path = repos_relpath;
      if (SVN_IS_VALID_REVNUM(peg_revision))
        {
          entry->file_external_peg_rev.kind = svn_opt_revision_number;
          entry->file_external_peg_rev.value.number = peg_revision;
          entry->file_external_rev = entry->file_external_peg_rev;
        }
      if (SVN_IS_VALID_REVNUM(revision))
        {
          entry->file_external_rev.kind = svn_opt_revision_number;
          entry->file_external_rev.value.number = revision;
        }
    }

  return SVN_NO_ERROR;
}


/* Fill in the following fields of ENTRY:

     REVISION
     REPOS
     UUID
     CMT_REV
     CMT_DATE
     CMT_AUTHOR
     DEPTH
     DELETED

   Return: KIND, REPOS_RELPATH, CHECKSUM
*/
static svn_error_t *
get_info_for_deleted(svn_wc_entry_t *entry,
                     svn_node_kind_t *kind,
                     const char **repos_relpath,
                     const svn_checksum_t **checksum,
                     svn_wc__db_lock_t **lock,
                     svn_wc__db_t *db,
                     const char *entry_abspath,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *entry_relpath,
                     const svn_wc_entry_t *parent_entry,
                     svn_boolean_t have_base,
                     svn_boolean_t have_more_work,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  if (have_base && !have_more_work)
    {
      apr_int64_t repos_id;
      /* This is the delete of a BASE node */
      SVN_ERR(svn_wc__db_base_get_info_internal(
                                       NULL, kind,
                                       &entry->revision,
                                       repos_relpath,
                                       &repos_id,
                                       &entry->cmt_rev,
                                       &entry->cmt_date,
                                       &entry->cmt_author,
                                       &entry->depth,
                                       checksum,
                                       NULL,
                                       lock,
                                       &entry->has_props, NULL,
                                       NULL,
                                       wcroot, entry_relpath,
                                       result_pool,
                                       scratch_pool));
      SVN_ERR(svn_wc__db_fetch_repos_info(&entry->repos, &entry->uuid,
                                          wcroot, repos_id, result_pool));
    }
  else
    {
      const char *work_del_relpath;
      const char *parent_repos_relpath;
      const char *parent_relpath;
      apr_int64_t repos_id;

      /* This is a deleted child of a copy/move-here,
         so we need to scan up the WORKING tree to find the root of
         the deletion. Then examine its parent to discover its
         future location in the repository.  */
      SVN_ERR(svn_wc__db_read_pristine_info(NULL, kind,
                                            &entry->cmt_rev,
                                            &entry->cmt_date,
                                            &entry->cmt_author,
                                            &entry->depth,
                                            checksum,
                                            NULL,
                                            &entry->has_props, NULL,
                                            db,
                                            entry_abspath,
                                            result_pool,
                                            scratch_pool));
      /* working_size and text_time unavailable */

     SVN_ERR(svn_wc__db_scan_deletion_internal(
                                      NULL,
                                      NULL,
                                      &work_del_relpath, NULL,
                                      wcroot, entry_relpath,
                                      scratch_pool, scratch_pool));

      SVN_ERR_ASSERT(work_del_relpath != NULL);
      parent_relpath = svn_relpath_dirname(work_del_relpath, scratch_pool);

      /* The parent directory of the delete root must be added, so we
         can find the required information there */
      SVN_ERR(svn_wc__db_scan_addition_internal(
                                       NULL, NULL,
                                       &parent_repos_relpath,
                                       &repos_id,
                                       NULL, NULL, NULL,
                                       wcroot, parent_relpath,
                                       result_pool, scratch_pool));
      SVN_ERR(svn_wc__db_fetch_repos_info(&entry->repos, &entry->uuid,
                                          wcroot, repos_id, result_pool));

      /* Now glue it all together */
      *repos_relpath = svn_relpath_join(parent_repos_relpath,
                                        svn_relpath_skip_ancestor(
                                                            parent_relpath,
                                                            entry_relpath),
                                        result_pool);


      /* Even though this is the delete of a WORKING node, there might still
         be a BASE node somewhere below with an interesting revision */
      if (have_base)
        {
          svn_wc__db_status_t status;
          SVN_ERR(svn_wc__db_base_get_info_internal(
                                           &status, NULL, &entry->revision,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, lock, NULL, NULL,
                                           NULL,
                                           wcroot, entry_relpath,
                                           result_pool, scratch_pool));

          if (status == svn_wc__db_status_not_present)
            entry->deleted = TRUE;
        }
    }

  /* Do some extra work for the child nodes.  */
  if (!SVN_IS_VALID_REVNUM(entry->revision) && parent_entry != NULL)
    {
      /* For child nodes without a revision, pick up the parent's
         revision.  */
      entry->revision = parent_entry->revision;
    }

  return SVN_NO_ERROR;
}


/*
 * Encode tree conflict descriptions into a single string.
 *
 * Set *CONFLICT_DATA to a string, allocated in POOL, that encodes the tree
 * conflicts in CONFLICTS in a form suitable for storage in a single string
 * field in a WC entry. CONFLICTS is a hash of zero or more pointers to
 * svn_wc_conflict_description2_t objects, index by their basenames. All of the
 * conflict victim paths must be siblings.
 *
 * Do all allocations in POOL.
 *
 * @see svn_wc__read_tree_conflicts()
 */
static svn_error_t *
write_tree_conflicts(const char **conflict_data,
                     apr_hash_t *conflicts,
                     apr_pool_t *pool)
{
  svn_skel_t *skel = svn_skel__make_empty_list(pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, conflicts); hi; hi = apr_hash_next(hi))
    {
      svn_skel_t *c_skel;

      SVN_ERR(svn_wc__serialize_conflict(&c_skel, apr_hash_this_val(hi),
                                         pool, pool));
      svn_skel__prepend(c_skel, skel);
    }

  *conflict_data = svn_skel__unparse(skel, pool)->data;

  return SVN_NO_ERROR;
}


/* Read one entry from wc_db. It will be allocated in RESULT_POOL and
   returned in *NEW_ENTRY.

   DIR_ABSPATH is the name of the directory to read this entry from, and
   it will be named NAME (use "" for "this dir").

   DB specifies the wc_db database, and WC_ID specifies which working copy
   this information is being read from.

   If this node is "this dir", then PARENT_ENTRY should be NULL. Otherwise,
   it should refer to the entry for the child's parent directory.

   ### All database read operations should really use wcroot, dir_relpath,
       as that restores obstruction compatibility with <= 1.6.0
       but that has been the case since the introduction of WC-NG in 1.7.0

   Temporary allocations are made in SCRATCH_POOL.  */
static svn_error_t *
read_one_entry(const svn_wc_entry_t **new_entry,
               svn_wc__db_t *db,
               const char *dir_abspath,
               svn_wc__db_wcroot_t *wcroot,
               const char *dir_relpath,
               const char *name,
               const svn_wc_entry_t *parent_entry,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  svn_wc__db_status_t status;
  svn_wc__db_lock_t *lock;
  const char *repos_relpath;
  const svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  svn_wc_entry_t *entry = alloc_entry(result_pool);
  const char *entry_relpath;
  const char *entry_abspath;
  apr_int64_t repos_id;
  apr_int64_t original_repos_id;
  const char *original_repos_relpath;
  const char *original_root_url;
  svn_boolean_t conflicted;
  svn_boolean_t have_base;
  svn_boolean_t have_more_work;
  svn_boolean_t op_root;

  entry->name = apr_pstrdup(result_pool, name);

  entry_relpath = svn_relpath_join(dir_relpath, entry->name, scratch_pool);
  entry_abspath = svn_dirent_join(dir_abspath, entry->name, scratch_pool);

  SVN_ERR(svn_wc__db_read_info_internal(
            &status,
            &kind,
            &entry->revision,
            &repos_relpath,
            &repos_id,
            &entry->cmt_rev,
            &entry->cmt_date,
            &entry->cmt_author,
            &entry->depth,
            &checksum,
            NULL,
            &original_repos_relpath,
            &original_repos_id,
            &entry->copyfrom_rev,
            &lock,
            &translated_size,
            &entry->text_time,
            &entry->changelist,
            &conflicted,
            &op_root,
            &entry->has_props /* have_props */,
            &entry->has_prop_mods /* props_mod */,
            &have_base,
            &have_more_work,
            NULL /* have_work */,
            wcroot, entry_relpath,
            result_pool, scratch_pool));

  SVN_ERR(svn_wc__db_fetch_repos_info(&entry->repos, &entry->uuid,
                                      wcroot, repos_id, result_pool));
  SVN_ERR(svn_wc__db_fetch_repos_info(&original_root_url, NULL,
                                      wcroot, original_repos_id,
                                      result_pool));

  if (entry->has_prop_mods)
    entry->has_props = TRUE;

  if (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
    {
      /* get the tree conflict data. */
      apr_hash_t *tree_conflicts = NULL;
      const apr_array_header_t *conflict_victims;
      int k;

      SVN_ERR(svn_wc__db_read_conflict_victims(&conflict_victims, db,
                                               dir_abspath,
                                               scratch_pool,
                                               scratch_pool));

      for (k = 0; k < conflict_victims->nelts; k++)
        {
          int j;
          const apr_array_header_t *child_conflicts;
          const char *child_name;
          const char *child_abspath;

          child_name = APR_ARRAY_IDX(conflict_victims, k, const char *);
          child_abspath = svn_dirent_join(dir_abspath, child_name,
                                          scratch_pool);

          SVN_ERR(svn_wc__read_conflicts(&child_conflicts, NULL,
                                         db, child_abspath,
                                         FALSE /* create tempfiles */,
                                         TRUE /* tree_conflicts_only */,
                                         scratch_pool, scratch_pool));

          for (j = 0; j < child_conflicts->nelts; j++)
            {
              const svn_wc_conflict_description2_t *conflict =
                APR_ARRAY_IDX(child_conflicts, j,
                              svn_wc_conflict_description2_t *);

              if (conflict->kind == svn_wc_conflict_kind_tree)
                {
                  if (!tree_conflicts)
                    tree_conflicts = apr_hash_make(scratch_pool);
                  svn_hash_sets(tree_conflicts, child_name, conflict);
                }
            }
        }

      if (tree_conflicts)
        {
          SVN_ERR(write_tree_conflicts(&entry->tree_conflict_data,
                                       tree_conflicts, result_pool));
        }
    }

  if (status == svn_wc__db_status_normal
      || status == svn_wc__db_status_incomplete)
    {
      /* Plain old BASE node.  */
      entry->schedule = svn_wc_schedule_normal;

      /* Grab inherited repository information, if necessary. */
      if (repos_relpath == NULL)
        {
          SVN_ERR(svn_wc__db_base_get_info_internal(
                                           NULL, NULL, NULL, &repos_relpath,
                                           &repos_id, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL,
                                           wcroot, entry_relpath,
                                           result_pool, scratch_pool));
          SVN_ERR(svn_wc__db_fetch_repos_info(&entry->repos, &entry->uuid,
                                              wcroot, repos_id, result_pool));
        }

      entry->incomplete = (status == svn_wc__db_status_incomplete);
    }
  else if (status == svn_wc__db_status_deleted)
    {
      svn_node_kind_t path_kind;

      /* ### we don't have to worry about moves, so this is a delete. */
      entry->schedule = svn_wc_schedule_delete;

      /* If there are multiple working layers or no BASE layer, then
         this is a WORKING delete or WORKING not-present. */
      if (have_more_work || !have_base)
        entry->copied = TRUE;
      else if (have_base && !have_more_work)
        entry->copied = FALSE;
      else
        {
          const char *work_del_relpath;
          SVN_ERR(svn_wc__db_scan_deletion_internal(
                                           NULL, NULL,
                                           &work_del_relpath, NULL,
                                           wcroot, entry_relpath,
                                           scratch_pool, scratch_pool));

          if (work_del_relpath)
            entry->copied = TRUE;
        }

      /* If there is still a directory on-disk we keep it, if not it is
         already deleted. Simple, isn't it?

         Before single-db we had to keep the administative area alive until
         after the commit really deletes it. Setting keep alive stopped the
         commit processing from deleting the directory. We don't delete it
         any more, so all we have to do is provide some 'sane' value.
       */
      SVN_ERR(svn_io_check_path(entry_abspath, &path_kind, scratch_pool));
      entry->keep_local = (path_kind == svn_node_dir);
    }
  else if (status == svn_wc__db_status_added)
    {
      svn_wc__db_status_t work_status;
      const char *op_root_abspath;
      const char *scanned_original_relpath;
      svn_revnum_t original_revision;

      /* For child nodes, pick up the parent's revision.  */
      if (*entry->name != '\0')
        {
          assert(parent_entry != NULL);
          assert(entry->revision == SVN_INVALID_REVNUM);

          entry->revision = parent_entry->revision;
        }

      if (have_base)
        {
          svn_wc__db_status_t base_status;

          /* ENTRY->REVISION is overloaded. When a node is schedule-add
             or -replace, then REVISION refers to the BASE node's revision
             that is being overwritten. We need to fetch it now.  */
          SVN_ERR(svn_wc__db_base_get_info_internal(
                                           &base_status, NULL,
                                           &entry->revision,
                                           NULL, NULL, NULL,
                                           NULL, NULL, NULL,
                                           NULL, NULL, NULL,
                                           NULL, NULL, NULL,
                                           wcroot, entry_relpath,
                                           scratch_pool,
                                           scratch_pool));

          if (base_status == svn_wc__db_status_not_present)
            {
              /* The underlying node is DELETED in this revision.  */
              entry->deleted = TRUE;

              /* This is an add since there isn't a node to replace.  */
              entry->schedule = svn_wc_schedule_add;
            }
          else
            entry->schedule = svn_wc_schedule_replace;
        }
      else
        {
          /* There is NO 'not-present' BASE_NODE for this node.
             Therefore, we are looking at some kind of add/copy
             rather than a replace.  */

          /* ### if this looks like a plain old add, then rev=0.  */
          if (!SVN_IS_VALID_REVNUM(entry->copyfrom_rev)
              && !SVN_IS_VALID_REVNUM(entry->cmt_rev))
            entry->revision = 0;

          entry->schedule = svn_wc_schedule_add;
        }

      /* If we don't have "real" data from the entry (obstruction),
         then we cannot begin a scan for data. The original node may
         have important data. Set up stuff to kill that idea off,
         and finish up this entry.  */
        {
          const char *op_root_relpath;
          SVN_ERR(svn_wc__db_scan_addition_internal(
                                           &work_status,
                                           &op_root_relpath,
                                           &repos_relpath,
                                           &repos_id,
                                           &scanned_original_relpath,
                                           NULL /* original_repos_id */,
                                           &original_revision,
                                           wcroot, entry_relpath,
                                           result_pool, scratch_pool));

          SVN_ERR(svn_wc__db_fetch_repos_info(&entry->repos, &entry->uuid,
                                      wcroot, repos_id, result_pool));

          if (!op_root_relpath)
            op_root_abspath = NULL;
          else
            op_root_abspath = svn_dirent_join(wcroot->abspath, op_root_relpath,
                                              scratch_pool);

          /* In wc.db we want to keep the valid revision of the not-present
             BASE_REV, but when we used entries we set the revision to 0
             when adding a new node over a not present base node. */
          if (work_status == svn_wc__db_status_added
              && entry->deleted)
            entry->revision = 0;
        }

      if (!SVN_IS_VALID_REVNUM(entry->cmt_rev)
          && scanned_original_relpath == NULL)
        {
          /* There is NOT a last-changed revision (last-changed date and
             author may be unknown, but we can always check the rev).
             The absence of a revision implies this node was added WITHOUT
             any history. Avoid the COPIED checks in the else block.  */
          /* ### scan_addition may need to be updated to avoid returning
             ### status_copied in this case.  */
        }
      /* For backwards-compatibility purposes we treat moves just like
       * regular copies. */
      else if (work_status == svn_wc__db_status_copied ||
               work_status == svn_wc__db_status_moved_here)
        {
          entry->copied = TRUE;

          /* If this is a child of a copied subtree, then it should be
             schedule_normal.  */
          if (original_repos_relpath == NULL)
            {
              /* ### what if there is a BASE node under there? */
              entry->schedule = svn_wc_schedule_normal;
            }

          /* Copied nodes need to mirror their copyfrom_rev, if they
             don't have a revision of their own already. */
          if (!SVN_IS_VALID_REVNUM(entry->revision)
              || entry->revision == 0 /* added */)
            entry->revision = original_revision;
        }

      /* Does this node have copyfrom_* information?  */
      if (scanned_original_relpath != NULL)
        {
          svn_boolean_t is_copied_child;
          svn_boolean_t is_mixed_rev = FALSE;

          SVN_ERR_ASSERT(work_status == svn_wc__db_status_copied ||
                         work_status == svn_wc__db_status_moved_here);

          /* If this node inherits copyfrom information from an
             ancestor node, then it must be a copied child.  */
          is_copied_child = (original_repos_relpath == NULL);

          /* If this node has copyfrom information on it, then it may
             be an actual copy-root, or it could be participating in
             a mixed-revision copied tree. So if we don't already know
             this is a copied child, then we need to look for this
             mixed-revision situation.  */
          if (!is_copied_child)
            {
              const char *parent_relpath;
              svn_error_t *err;
              const char *parent_repos_relpath;
              const char *parent_root_url;
              apr_int64_t parent_repos_id;
              const char *op_root_relpath;

              /* When we insert entries into the database, we will
                 construct additional copyfrom records for mixed-revision
                 copies. The old entries would simply record the different
                 revision in the entry->revision field. That is not
                 available within wc-ng, so additional copies are made
                 (see the logic inside write_entry()). However, when
                 reading these back *out* of the database, the additional
                 copies look like new "Added" nodes rather than a simple
                 mixed-rev working copy.

                 That would be a behavior change if we did not compensate.
                 If there is copyfrom information for this node, then the
                 code below looks at the parent to detect if it *also* has
                 copyfrom information, and if the copyfrom_url would align
                 properly. If it *does*, then we omit storing copyfrom_url
                 and copyfrom_rev (ie. inherit the copyfrom info like a
                 normal child), and update entry->revision with the
                 copyfrom_rev in order to (re)create the mixed-rev copied
                 subtree that was originally presented for storage.  */

              /* Get the copyfrom information from our parent.

                 Note that the parent could be added/copied/moved-here.
                 There is no way for it to be deleted/moved-away and
                 have *this* node appear as copied.  */
              parent_relpath = svn_relpath_dirname(entry_relpath,
                                                   scratch_pool);
              err = svn_wc__db_scan_addition_internal(
                                             NULL,
                                             &op_root_relpath,
                                             NULL, NULL,
                                             &parent_repos_relpath,
                                             &parent_repos_id,
                                             NULL,
                                             wcroot, parent_relpath,
                                             scratch_pool,
                                             scratch_pool);
              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
                    return svn_error_trace(err);
                  svn_error_clear(err);
                  op_root_abspath = NULL;
                  parent_repos_relpath = NULL;
                  parent_root_url = NULL;
                }
              else
                {
                  SVN_ERR(svn_wc__db_fetch_repos_info(&parent_root_url, NULL,
                                                      wcroot, parent_repos_id,
                                                      scratch_pool));
                  op_root_abspath = svn_dirent_join(wcroot->abspath,
                                                    op_root_relpath,
                                                    scratch_pool);
                }

              if (parent_root_url != NULL
                       && strcmp(original_root_url, parent_root_url) == 0)
                {
                  
                  const char *relpath_to_entry = svn_dirent_is_child(
                    op_root_abspath, entry_abspath, NULL);
                  const char *entry_repos_relpath = svn_relpath_join(
                    parent_repos_relpath, relpath_to_entry, scratch_pool);

                  /* The copyfrom repos roots matched.

                     Now we look to see if the copyfrom path of the parent
                     would align with our own path. If so, then it means
                     this copyfrom was spontaneously created and inserted
                     for mixed-rev purposes and can be eliminated without
                     changing the semantics of a mixed-rev copied subtree.

                     See notes/api-errata/wc003.txt for some additional
                     detail, and potential issues.  */
                  if (strcmp(entry_repos_relpath,
                             original_repos_relpath) == 0)
                    {
                      is_copied_child = TRUE;
                      is_mixed_rev = TRUE;
                    }
                }
            }

          if (is_copied_child)
            {
              /* We won't be settig the  copyfrom_url, yet need to
                 clear out the copyfrom_rev. Thus, this node becomes a
                 child of a copied subtree (rather than its own root).  */
              entry->copyfrom_rev = SVN_INVALID_REVNUM;

              /* Children in a copied subtree are schedule normal
                 since we don't plan to actually *do* anything with
                 them. Their operation is implied by ancestors.  */
              entry->schedule = svn_wc_schedule_normal;

              /* And *finally* we turn this entry into the mixed
                 revision node that it was intended to be. This
                 node's revision is taken from the copyfrom record
                 that we spontaneously constructed.  */
              if (is_mixed_rev)
                entry->revision = original_revision;
            }
          else if (original_repos_relpath != NULL)
            {
              entry->copyfrom_url =
                svn_path_url_add_component2(original_root_url,
                                            original_repos_relpath,
                                            result_pool);
            }
          else
            {
              /* NOTE: if original_repos_relpath == NULL, then the
                 second call to scan_addition() will not have occurred.
                 Thus, this use of OP_ROOT_ABSPATH still contains the
                 original value where we fetched a value for
                 SCANNED_REPOS_RELPATH.  */
              const char *relpath_to_entry = svn_dirent_is_child(
                op_root_abspath, entry_abspath, NULL);
              const char *entry_repos_relpath = svn_relpath_join(
                scanned_original_relpath, relpath_to_entry, scratch_pool);

              entry->copyfrom_url =
                svn_path_url_add_component2(original_root_url,
                                            entry_repos_relpath,
                                            result_pool);
            }
        }
    }
  else if (status == svn_wc__db_status_not_present)
    {
      /* ### buh. 'deleted' nodes are actually supposed to be
         ### schedule "normal" since we aren't going to actually *do*
         ### anything to this node at commit time.  */
      entry->schedule = svn_wc_schedule_normal;
      entry->deleted = TRUE;
    }
  else if (status == svn_wc__db_status_server_excluded)
    {
      entry->absent = TRUE;
    }
  else if (status == svn_wc__db_status_excluded)
    {
      entry->schedule = svn_wc_schedule_normal;
      entry->depth = svn_depth_exclude;
    }
  else
    {
      /* ### we should have handled all possible status values.  */
      SVN_ERR_MALFUNCTION();
    }

  /* ### higher levels want repos information about deleted nodes, even
     ### tho they are not "part of" a repository any more.  */
  if (entry->schedule == svn_wc_schedule_delete)
    {
      SVN_ERR(get_info_for_deleted(entry,
                                   &kind,
                                   &repos_relpath,
                                   &checksum,
                                   &lock,
                                   db, entry_abspath,
                                   wcroot, entry_relpath,
                                   parent_entry,
                                   have_base, have_more_work,
                                   result_pool, scratch_pool));
    }

  /* ### default to the infinite depth if we don't know it. */
  if (entry->depth == svn_depth_unknown)
    entry->depth = svn_depth_infinity;

  if (kind == svn_node_dir)
    entry->kind = svn_node_dir;
  else if (kind == svn_node_file)
    entry->kind = svn_node_file;
  else if (kind == svn_node_symlink)
    entry->kind = svn_node_file;  /* ### no symlink kind */
  else
    entry->kind = svn_node_unknown;

  /* We should always have a REPOS_RELPATH, except for:
     - deleted nodes
     - certain obstructed nodes
     - not-present nodes
     - absent nodes
     - excluded nodes

     ### the last three should probably have an "implied" REPOS_RELPATH
  */
  SVN_ERR_ASSERT(repos_relpath != NULL
                 || entry->schedule == svn_wc_schedule_delete
                 || status == svn_wc__db_status_not_present
                 || status == svn_wc__db_status_server_excluded
                 || status == svn_wc__db_status_excluded);
  if (repos_relpath)
    entry->url = svn_path_url_add_component2(entry->repos,
                                             repos_relpath,
                                             result_pool);

  if (checksum)
    {
      /* We got a SHA-1, get the corresponding MD-5. */
      if (checksum->kind != svn_checksum_md5)
        SVN_ERR(svn_wc__db_pristine_get_md5(&checksum, db,
                                            dir_abspath, checksum,
                                            scratch_pool, scratch_pool));

      SVN_ERR_ASSERT(checksum->kind == svn_checksum_md5);
      entry->checksum = svn_checksum_to_cstring(checksum, result_pool);
    }

  if (conflicted)
    {
      svn_skel_t *conflict;
      svn_boolean_t text_conflicted;
      svn_boolean_t prop_conflicted;
      SVN_ERR(svn_wc__db_read_conflict_internal(&conflict, NULL, NULL,
                                                wcroot, entry_relpath,
                                                scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__conflict_read_info(NULL, NULL, &text_conflicted,
                                         &prop_conflicted, NULL,
                                         db, dir_abspath, conflict,
                                         scratch_pool, scratch_pool));

      if (text_conflicted)
        {
          const char *my_abspath;
          const char *their_old_abspath;
          const char *their_abspath;
          SVN_ERR(svn_wc__conflict_read_text_conflict(&my_abspath,
                                                      &their_old_abspath,
                                                      &their_abspath,
                                                      db, dir_abspath,
                                                      conflict, scratch_pool,
                                                      scratch_pool));

          if (my_abspath)
            entry->conflict_wrk = svn_dirent_basename(my_abspath, result_pool);

          if (their_old_abspath)
            entry->conflict_old = svn_dirent_basename(their_old_abspath,
                                                      result_pool);

          if (their_abspath)
            entry->conflict_new = svn_dirent_basename(their_abspath,
                                                      result_pool);
        }

      if (prop_conflicted)
        {
          const char *prej_abspath;

          SVN_ERR(svn_wc__conflict_read_prop_conflict(&prej_abspath, NULL,
                                                      NULL, NULL, NULL,
                                                      db, dir_abspath,
                                                      conflict, scratch_pool,
                                                      scratch_pool));

          if (prej_abspath)
            entry->prejfile = svn_dirent_basename(prej_abspath, result_pool);
        }
    }

  if (lock)
    {
      entry->lock_token = lock->token;
      entry->lock_owner = lock->owner;
      entry->lock_comment = lock->comment;
      entry->lock_creation_date = lock->date;
    }

  /* Let's check for a file external.  ugh.  */
  if (status == svn_wc__db_status_normal
      && kind == svn_node_file)
    SVN_ERR(check_file_external(entry, db, entry_abspath, dir_abspath,
                                result_pool, scratch_pool));

  entry->working_size = translated_size;

  *new_entry = entry;

  return SVN_NO_ERROR;
}

/* Read entries for PATH/LOCAL_ABSPATH from DB. The entries
   will be allocated in RESULT_POOL, with temporary allocations in
   SCRATCH_POOL. The entries are returned in RESULT_ENTRIES.  */
static svn_error_t *
read_entries_new(apr_hash_t **result_entries,
                 svn_wc__db_t *db,
                 const char *dir_abspath,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *dir_relpath,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_hash_t *entries;
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  const svn_wc_entry_t *parent_entry;

  entries = apr_hash_make(result_pool);

  SVN_ERR(read_one_entry(&parent_entry,
                         db, dir_abspath,
                         wcroot, dir_relpath,
                         "" /* name */,
                         NULL /* parent_entry */,
                         result_pool, iterpool));
  svn_hash_sets(entries, "", parent_entry);

  /* Use result_pool so that the child names (used by reference, rather
     than copied) appear in result_pool.  */
  SVN_ERR(svn_wc__db_read_children(&children, db,
                                   dir_abspath,
                                   scratch_pool, iterpool));
  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const svn_wc_entry_t *entry;

      svn_pool_clear(iterpool);

      SVN_ERR(read_one_entry(&entry,
                             db, dir_abspath, 
                             wcroot, dir_relpath,
                             name, parent_entry,
                             result_pool, iterpool));
      svn_hash_sets(entries, entry->name, entry);
    }

  svn_pool_destroy(iterpool);

  *result_entries = entries;

  return SVN_NO_ERROR;
}


static svn_error_t *
read_entry_pair_txn(const svn_wc_entry_t **parent_entry,
                    const svn_wc_entry_t **entry,
                    svn_wc__db_t *db,
                    const char *dir_abspath,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *dir_relpath,
                    const char *name,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(read_one_entry(parent_entry,
                         db, dir_abspath,
                         wcroot, dir_relpath,
                         "" /* name */,
                         NULL /* parent_entry */,
                         result_pool, scratch_pool));

  /* If we need the entry for "this dir", then return the parent_entry
     in both outputs. Otherwise, read the child node.  */
  if (*name == '\0')
    {
      /* If the retrieved node is a FILE, then we have a problem. We asked
         for a directory. This implies there is an obstructing, unversioned
         directory where a FILE should be. We navigated from the obstructing
         subdir up to the parent dir, then returned the FILE found there.

         Let's return WC_MISSING cuz the caller thought we had a dir, but
         that (versioned subdir) isn't there.  */
      if ((*parent_entry)->kind == svn_node_file)
        {
          *parent_entry = NULL;
          return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                                 _("'%s' is not a versioned working copy"),
                                 svn_dirent_local_style(dir_abspath,
                                                        scratch_pool));
        }

      *entry = *parent_entry;
    }
  else
    {
      const apr_array_header_t *children;
      int i;

      /* Default to not finding the child.  */
      *entry = NULL;

      /* Determine whether the parent KNOWS about this child. If it does
         not, then we should not attempt to look for it.

         For example: the parent doesn't "know" about the child, but the
         versioned directory *does* exist on disk. We don't want to look
         into that subdir.  */
      SVN_ERR(svn_wc__db_read_children(&children, db, dir_abspath,
                                       scratch_pool, scratch_pool));
      for (i = children->nelts; i--; )
        {
          const char *child = APR_ARRAY_IDX(children, i, const char *);

          if (strcmp(child, name) == 0)
            {
              svn_error_t *err;

              err = read_one_entry(entry,
                                   db, dir_abspath,
                                   wcroot, dir_relpath,
                                   name, *parent_entry,
                                   result_pool, scratch_pool);
              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
                    return svn_error_trace(err);

                  /* No problem. Clear the error and leave the default value
                     of "missing".  */
                  svn_error_clear(err);
                }

              /* Found it. No need to keep searching.  */
              break;
            }
        }
      /* if the loop ends without finding a child, then we have the default
         ENTRY value of NULL.  */
    }

  return SVN_NO_ERROR;
}

/* Read a pair of entries from wc_db in the directory DIR_ABSPATH. Return
   the directory's entry in *PARENT_ENTRY and NAME's entry in *ENTRY. The
   two returned pointers will be the same if NAME=="" ("this dir").

   The parent entry must exist.

   The requested entry MAY exist. If it does not, then NULL will be returned.

   The resulting entries are allocated in RESULT_POOL, and all temporary
   allocations are made in SCRATCH_POOL.  */
static svn_error_t *
read_entry_pair(const svn_wc_entry_t **parent_entry,
                const svn_wc_entry_t **entry,
                svn_wc__db_t *db,
                const char *dir_abspath,
                const char *name,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath,
                                                db, dir_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(read_entry_pair_txn(parent_entry, entry,
                                          db, dir_abspath,
                                          wcroot, dir_relpath,
                                          name,
                                          result_pool, scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
read_entries(apr_hash_t **entries,
             svn_wc__db_t *db,
             const char *dir_abspath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;
  int wc_format;

  SVN_ERR(svn_wc__db_temp_get_format(&wc_format, db, dir_abspath,
                                     scratch_pool));

  if (wc_format < SVN_WC__WC_NG_VERSION)
    return svn_error_trace(svn_wc__read_entries_old(entries,
                                                    dir_abspath,
                                                    result_pool,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath,
                                                db, dir_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(read_entries_new(entries,
                                       db, dir_abspath,
                                       wcroot, dir_relpath,
                                       result_pool, scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}


/* For a given LOCAL_ABSPATH, using DB, set *ADM_ABSPATH to the directory in
   which the entry information is located, and *ENTRY_NAME to the entry name
   to access that entry.

   KIND is as in svn_wc__get_entry().

   Return the results in RESULT_POOL and use SCRATCH_POOL for temporary
   allocations. */
static svn_error_t *
get_entry_access_info(const char **adm_abspath,
                      const char **entry_name,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_node_kind_t kind,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t read_from_subdir = FALSE;

  /* If the caller didn't know the node kind, then stat the path. Maybe
     it is really there, and we can speed up the steps below.  */
  if (kind == svn_node_unknown)
    {
      svn_node_kind_t on_disk;

      /* Do we already have an access baton for LOCAL_ABSPATH?  */
      adm_access = svn_wc__adm_retrieve_internal2(db, local_abspath,
                                                  scratch_pool);
      if (adm_access)
        {
          /* Sweet. The node is a directory.  */
          on_disk = svn_node_dir;
        }
      else
        {
          svn_boolean_t special;

          /* What's on disk?  */
          SVN_ERR(svn_io_check_special_path(local_abspath, &on_disk, &special,
                                            scratch_pool));
        }

      if (on_disk != svn_node_dir)
        {
          /* If this is *anything* besides a directory (FILE, NONE, or
             UNKNOWN), then we cannot treat it as a versioned directory
             containing entries to read. Leave READ_FROM_SUBDIR as FALSE,
             so that the parent will be examined.

             For NONE and UNKNOWN, it may be that metadata exists for the
             node, even though on-disk is unhelpful.

             If NEED_PARENT_STUB is TRUE, and the entry is not a DIRECTORY,
             then we'll error.

             If NEED_PARENT_STUB if FALSE, and we successfully read a stub,
             then this on-disk node is obstructing the read.  */
        }
      else
        {
          /* We found a directory for this UNKNOWN node. Determine whether
             we need to read inside it.  */
          read_from_subdir = TRUE;
        }
    }
  else if (kind == svn_node_dir)
    {
      read_from_subdir = TRUE;
    }

  if (read_from_subdir)
    {
      /* KIND must be a DIR or UNKNOWN (and we found a subdir). We want
         the "real" data, so treat LOCAL_ABSPATH as a versioned directory.  */
      *adm_abspath = apr_pstrdup(result_pool, local_abspath);
      *entry_name = "";
    }
  else
    {
      /* FILE node needs to read the parent directory. Or a DIR node
         needs to read from the parent to get at the stub entry. Or this
         is an UNKNOWN node, and we need to examine the parent.  */
      svn_dirent_split(adm_abspath, entry_name, local_abspath, result_pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__get_entry(const svn_wc_entry_t **entry,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  svn_boolean_t allow_unversioned,
                  svn_node_kind_t kind,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *dir_abspath;
  const char *entry_name;

  SVN_ERR(get_entry_access_info(&dir_abspath, &entry_name, db, local_abspath,
                                kind, scratch_pool, scratch_pool));

    {
      const svn_wc_entry_t *parent_entry;
      svn_error_t *err;

      /* NOTE: if KIND is UNKNOWN and we decided to examine the *parent*
         directory, then it is possible we moved out of the working copy.
         If the on-disk node is a DIR, and we asked for a stub, then we
         obviously can't provide that (parent has no info). If the on-disk
         node is a FILE/NONE/UNKNOWN, then it is obstructing the real
         LOCAL_ABSPATH (or it was never a versioned item). In all these
         cases, the read_entries() will (properly) throw an error.

         NOTE: if KIND is a DIR and we asked for the real data, but it is
         obstructed on-disk by some other node kind (NONE, FILE, UNKNOWN),
         then this will throw an error.  */

      err = read_entry_pair(&parent_entry, entry,
                            db, dir_abspath, entry_name,
                            result_pool, scratch_pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_MISSING || kind != svn_node_unknown
              || *entry_name != '\0')
            return svn_error_trace(err);
          svn_error_clear(err);

          /* The caller didn't know the node type, we saw a directory there,
             we attempted to read IN that directory, and then wc_db reports
             that it is NOT a working copy directory. It is possible that
             one of two things has happened:

             1) a directory is obstructing a file in the parent
             2) the (versioned) directory's contents have been removed

             Let's assume situation (1); if that is true, then we can just
             return the newly-found data.

             If we assumed (2), then a valid result still won't help us
             since the caller asked for the actual contents, not the stub
             (which is why we read *into* the directory). However, if we
             assume (1) and get back a stub, then we have verified a
             missing, versioned directory, and can return an error
             describing that.

             Redo the fetch, but "insist" we are trying to find a file.
             This will read from the parent directory of the "file".  */
          err = svn_wc__get_entry(entry, db, local_abspath, allow_unversioned,
                                  svn_node_file, result_pool, scratch_pool);
          if (err == SVN_NO_ERROR)
            return SVN_NO_ERROR;
          if (err->apr_err != SVN_ERR_NODE_UNEXPECTED_KIND)
            return svn_error_trace(err);
          svn_error_clear(err);

          /* We asked for a FILE, but the node found is a DIR. Thus, we
             are looking at a stub. Originally, we tried to read into the
             subdir because NEED_PARENT_STUB is FALSE. The stub we just
             read is not going to work for the caller, so inform them of
             the missing subdirectory.  */
          SVN_ERR_ASSERT(*entry != NULL && (*entry)->kind == svn_node_dir);
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("Admin area of '%s' is missing"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
        }
    }

  if (*entry == NULL)
    {
      if (allow_unversioned)
        return SVN_NO_ERROR;
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("'%s' is not under version control"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  /* The caller had the wrong information.  */
  if ((kind == svn_node_file && (*entry)->kind != svn_node_file)
      || (kind == svn_node_dir && (*entry)->kind != svn_node_dir))
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is not of the right kind"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

/* TODO ### Rewrite doc string to mention ENTRIES_ALL; not ADM_ACCESS.

   Prune the deleted entries from the cached entries in ADM_ACCESS, and
   return that collection in *ENTRIES_PRUNED.  SCRATCH_POOL is used for local,
   short term, memory allocation, RESULT_POOL for permanent stuff.  */
static svn_error_t *
prune_deleted(apr_hash_t **entries_pruned,
              apr_hash_t *entries_all,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  if (!entries_all)
    {
      *entries_pruned = NULL;
      return SVN_NO_ERROR;
    }

  /* I think it will be common for there to be no deleted entries, so
     it is worth checking for that case as we can optimise it. */
  for (hi = apr_hash_first(scratch_pool, entries_all);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_boolean_t hidden;

      SVN_ERR(svn_wc__entry_is_hidden(&hidden,
                                      apr_hash_this_val(hi)));
      if (hidden)
        break;
    }

  if (! hi)
    {
      /* There are no deleted entries, so we can use the full hash */
      *entries_pruned = entries_all;
      return SVN_NO_ERROR;
    }

  /* Construct pruned hash without deleted entries */
  *entries_pruned = apr_hash_make(result_pool);
  for (hi = apr_hash_first(scratch_pool, entries_all);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key = apr_hash_this_key(hi);
      const svn_wc_entry_t *entry = apr_hash_this_val(hi);
      svn_boolean_t hidden;

      SVN_ERR(svn_wc__entry_is_hidden(&hidden, entry));
      if (!hidden)
        svn_hash_sets(*entries_pruned, key, entry);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__entries_read_internal(apr_hash_t **entries,
                              svn_wc_adm_access_t *adm_access,
                              svn_boolean_t show_hidden,
                              apr_pool_t *pool)
{
  apr_hash_t *new_entries;

  new_entries = svn_wc__adm_access_entries(adm_access);
  if (! new_entries)
    {
      svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
      const char *local_abspath = svn_wc__adm_access_abspath(adm_access);
      apr_pool_t *result_pool = svn_wc__adm_access_pool_internal(adm_access);

      SVN_ERR(read_entries(&new_entries, db, local_abspath,
                           result_pool, pool));

      svn_wc__adm_access_set_entries(adm_access, new_entries);
    }

  if (show_hidden)
    *entries = new_entries;
  else
    SVN_ERR(prune_deleted(entries, new_entries,
                          svn_wc__adm_access_pool_internal(adm_access),
                          pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_entries_read(apr_hash_t **entries,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  return svn_error_trace(svn_wc__entries_read_internal(entries, adm_access,
                                                       show_hidden, pool));
}

/* No transaction required: called from write_entry which is itself
   transaction-wrapped. */
static svn_error_t *
insert_node(svn_sqlite__db_t *sdb,
            const db_node_t *node,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t present = (node->presence == svn_wc__db_status_normal
                           || node->presence == svn_wc__db_status_incomplete);

  SVN_ERR_ASSERT(node->op_depth > 0 || node->repos_relpath);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isdsnnnnsn",
                            node->wc_id,
                            node->local_relpath,
                            node->op_depth,
                            node->parent_relpath,
                            /* Setting depth for files? */
                            (node->kind == svn_node_dir && present)
                              ? svn_depth_to_word(node->depth)
                              : NULL));

  if (present && node->repos_relpath)
    {
      SVN_ERR(svn_sqlite__bind_revnum(stmt, 11, node->changed_rev));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 12, node->changed_date));
      SVN_ERR(svn_sqlite__bind_text(stmt, 13, node->changed_author));
    }

  if (node->repos_relpath
      && node->presence != svn_wc__db_status_base_deleted)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 5,
                                     node->repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6,
                                    node->repos_relpath));
      SVN_ERR(svn_sqlite__bind_revnum(stmt, 7, node->revision));
    }

  SVN_ERR(svn_sqlite__bind_token(stmt, 8, presence_map, node->presence));

  if (node->kind == svn_node_none)
    SVN_ERR(svn_sqlite__bind_text(stmt, 10, "unknown"));
  else
    SVN_ERR(svn_sqlite__bind_token(stmt, 10, kind_map, node->kind));

  if (node->kind == svn_node_file && present)
    {
      if (!node->checksum
          && node->op_depth == 0
          && node->presence != svn_wc__db_status_not_present
          && node->presence != svn_wc__db_status_excluded
          && node->presence != svn_wc__db_status_server_excluded)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("The file '%s' has no checksum"),
                                 svn_dirent_local_style(node->local_relpath,
                                                        scratch_pool));

      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, node->checksum,
                                        scratch_pool));

      if (node->repos_relpath)
        {
          if (node->recorded_size != SVN_INVALID_FILESIZE)
            SVN_ERR(svn_sqlite__bind_int64(stmt, 16, node->recorded_size));

          SVN_ERR(svn_sqlite__bind_int64(stmt, 17, node->recorded_time));
        }
    }

   /* ### Never set, props done later */
  if (node->properties && present && node->repos_relpath)
    SVN_ERR(svn_sqlite__bind_properties(stmt, 15, node->properties,
                                        scratch_pool));

  if (node->file_external)
    SVN_ERR(svn_sqlite__bind_int(stmt, 20, 1));

  if (node->inherited_props && present)
    SVN_ERR(svn_sqlite__bind_iprops(stmt, 23, node->inherited_props,
                                    scratch_pool));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
insert_actual_node(svn_sqlite__db_t *sdb,
                   svn_wc__db_t *db,
                   const char *wri_abspath,
                   const db_actual_node_t *actual_node,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_skel_t *conflict_data = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_ACTUAL_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, actual_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, actual_node->local_relpath));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, actual_node->parent_relpath));

  if (actual_node->properties)
    SVN_ERR(svn_sqlite__bind_properties(stmt, 4, actual_node->properties,
                                        scratch_pool));

  if (actual_node->changelist)
    SVN_ERR(svn_sqlite__bind_text(stmt, 5, actual_node->changelist));

  SVN_ERR(svn_wc__upgrade_conflict_skel_from_raw(
                                &conflict_data,
                                db, wri_abspath,
                                actual_node->local_relpath,
                                actual_node->conflict_old,
                                actual_node->conflict_working,
                                actual_node->conflict_new,
                                actual_node->prop_reject,
                                actual_node->tree_conflict_data,
                                actual_node->tree_conflict_data
                                    ? strlen(actual_node->tree_conflict_data)
                                    : 0,
                                scratch_pool, scratch_pool));

  if (conflict_data)
    {
      svn_stringbuf_t *data = svn_skel__unparse(conflict_data, scratch_pool);

      SVN_ERR(svn_sqlite__bind_blob(stmt, 6, data->data, data->len));
    }

  /* Execute and reset the insert clause. */
  return svn_error_trace(svn_sqlite__insert(NULL, stmt));
}

static svn_boolean_t
is_switched(db_node_t *parent,
            db_node_t *child,
            apr_pool_t *scratch_pool)
{
  if (parent && child)
    {
      if (parent->repos_id != child->repos_id)
        return TRUE;

      if (parent->repos_relpath && child->repos_relpath)
        {
          const char *unswitched
            = svn_relpath_join(parent->repos_relpath,
                               svn_relpath_basename(child->local_relpath,
                                                    scratch_pool),
                               scratch_pool);
          if (strcmp(unswitched, child->repos_relpath))
            return TRUE;
        }
    }

  return FALSE;
}

struct write_baton {
  db_node_t *base;
  db_node_t *work;
  db_node_t *below_work;
  apr_hash_t *tree_conflicts;
};

#define WRITE_ENTRY_ASSERT(expr) \
  if (!(expr)) \
    return svn_error_createf(SVN_ERR_ASSERTION_FAIL, NULL,  \
                             _("Unable to upgrade '%s' at line %d"),    \
                             svn_dirent_local_style( \
                               svn_dirent_join(root_abspath, \
                                               local_relpath,           \
                                               scratch_pool),           \
                               scratch_pool), __LINE__)

/* Write the information for ENTRY to WC_DB.  The WC_ID, REPOS_ID and
   REPOS_ROOT will all be used for writing ENTRY.
   ### transitioning from straight sql to using the wc_db APIs.  For the
   ### time being, we'll need both parameters. */
static svn_error_t *
write_entry(struct write_baton **entry_node,
            const struct write_baton *parent_node,
            svn_wc__db_t *db,
            svn_sqlite__db_t *sdb,
            apr_int64_t wc_id,
            apr_int64_t repos_id,
            const svn_wc_entry_t *entry,
            const svn_wc__text_base_info_t *text_base_info,
            const char *local_relpath,
            const char *tmp_entry_abspath,
            const char *root_abspath,
            const svn_wc_entry_t *this_dir,
            svn_boolean_t create_locks,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  db_node_t *base_node = NULL;
  db_node_t *working_node = NULL, *below_working_node = NULL;
  db_actual_node_t *actual_node = NULL;
  const char *parent_relpath;
  apr_hash_t *tree_conflicts;

  if (*local_relpath == '\0')
    parent_relpath = NULL;
  else
    parent_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

  /* This is how it should work, it doesn't work like this yet because
     we need proper op_depth to layer the working nodes.

     Using "svn add", "svn rm", "svn cp" only files can be replaced
     pre-wcng; directories can only be normal, deleted or added.
     Files cannot be replaced within a deleted directory, so replaced
     files can only exist in a normal directory, or a directory that
     is added+copied.  In a normal directory a replaced file needs a
     base node and a working node, in an added+copied directory a
     replaced file needs two working nodes at different op-depths.

     With just the above operations the conversion for files and
     directories is straightforward:

           pre-wcng                             wcng
     parent         child                 parent     child

     normal         normal                base       base
     add+copied     normal+copied         work       work
     normal+copied  normal+copied         work       work
     normal         delete                base       base+work
     delete         delete                base+work  base+work
     add+copied     delete                work       work
     normal         add                   base       work
     add            add                   work       work
     add+copied     add                   work       work
     normal         add+copied            base       work
     add            add+copied            work       work
     add+copied     add+copied            work       work
     normal         replace               base       base+work
     add+copied     replace               work       work+work
     normal         replace+copied        base       base+work
     add+copied     replace+copied        work       work+work

     However "svn merge" make this more complicated.  The pre-wcng
     "svn merge" is capable of replacing a directory, that is it can
     mark the whole tree deleted, and then copy another tree on top.
     The entries then represent the replacing tree overlayed on the
     deleted tree.

       original       replace          schedule in
       tree           tree             combined tree

       A              A                replace+copied
       A/f                             delete+copied
       A/g            A/g              replace+copied
                      A/h              add+copied
       A/B            A/B              replace+copied
       A/B/f                           delete+copied
       A/B/g          A/B/g            replace+copied
                      A/B/h            add+copied
       A/C                             delete+copied
       A/C/f                           delete+copied
                      A/D              add+copied
                      A/D/f            add+copied

     The original tree could be normal tree, or an add+copied tree.
     Committing such a merge generally worked, but making further tree
     modifications before commit sometimes failed.

     The root of the replace is handled like the file replace:

           pre-wcng                             wcng
     parent         child                 parent     child

     normal         replace+copied        base       base+work
     add+copied     replace+copied        work       work+work

     although obviously the node is a directory rather than a file.
     There are then more conversion states where the parent is
     replaced.

           pre-wcng                                wcng
     parent           child              parent            child

     replace+copied   add                [base|work]+work  work
     replace+copied   add+copied         [base|work]+work  work
     replace+copied   delete+copied      [base|work]+work  [base|work]+work
     delete+copied    delete+copied      [base|work]+work  [base|work]+work
     replace+copied   replace+copied     [base|work]+work  [base|work]+work
  */

  WRITE_ENTRY_ASSERT(parent_node || entry->schedule == svn_wc_schedule_normal);

  WRITE_ENTRY_ASSERT(!parent_node || parent_node->base
                     || parent_node->below_work || parent_node->work);

  switch (entry->schedule)
    {
      case svn_wc_schedule_normal:
        if (entry->copied ||
            (entry->depth == svn_depth_exclude
             && parent_node && !parent_node->base && parent_node->work))
          working_node = MAYBE_ALLOC(working_node, result_pool);
        else
          base_node = MAYBE_ALLOC(base_node, result_pool);
        break;

      case svn_wc_schedule_add:
        working_node = MAYBE_ALLOC(working_node, result_pool);
        if (entry->deleted)
          {
            if (parent_node->base)
              base_node = MAYBE_ALLOC(base_node, result_pool);
            else
              below_working_node = MAYBE_ALLOC(below_working_node, result_pool);
          }
        break;

      case svn_wc_schedule_delete:
        working_node = MAYBE_ALLOC(working_node, result_pool);
        if (parent_node->base)
          base_node = MAYBE_ALLOC(base_node, result_pool);
        if (parent_node->work)
          below_working_node = MAYBE_ALLOC(below_working_node, result_pool);
        break;

      case svn_wc_schedule_replace:
        working_node = MAYBE_ALLOC(working_node, result_pool);
        if (parent_node->base)
          base_node = MAYBE_ALLOC(base_node, result_pool);
        else
          below_working_node = MAYBE_ALLOC(below_working_node, result_pool);
        break;
    }

  /* Something deleted in this revision means there should always be a
     BASE node to indicate the not-present node.  */
  if (entry->deleted)
    {
      WRITE_ENTRY_ASSERT(base_node || below_working_node);
      WRITE_ENTRY_ASSERT(!entry->incomplete);
      if (base_node)
        base_node->presence = svn_wc__db_status_not_present;
      else
        below_working_node->presence = svn_wc__db_status_not_present;
    }
  else if (entry->absent)
    {
      WRITE_ENTRY_ASSERT(base_node && !working_node && !below_working_node);
      WRITE_ENTRY_ASSERT(!entry->incomplete);
      base_node->presence = svn_wc__db_status_server_excluded;
    }

  if (entry->copied)
    {
      db_node_t *work = parent_node->work
                              ? parent_node->work
                              : parent_node->below_work;

      if (entry->copyfrom_url)
        {
          working_node->repos_id = repos_id;
          working_node->repos_relpath = svn_uri_skip_ancestor(
                                          this_dir->repos, entry->copyfrom_url,
                                          result_pool);
          working_node->revision = entry->copyfrom_rev;
          working_node->op_depth
            = svn_wc__db_op_depth_for_upgrade(local_relpath);

          if (work && work->repos_relpath
              && work->repos_id == repos_id
              && work->revision == entry->copyfrom_rev)
            {
              const char *name;

              name = svn_relpath_skip_ancestor(work->repos_relpath,
                                               working_node->repos_relpath);

              if (name
                  && !strcmp(name, svn_relpath_basename(local_relpath, NULL)))
                {
                  working_node->op_depth = work->op_depth;
                }
            }
        }
      else if (work && work->repos_relpath)
        {
          working_node->repos_id = repos_id;
          working_node->repos_relpath
            = svn_relpath_join(work->repos_relpath,
                               svn_relpath_basename(local_relpath, NULL),
                               result_pool);
          working_node->revision = work->revision;
          working_node->op_depth = work->op_depth;
        }
      else if (parent_node->below_work
                && parent_node->below_work->repos_relpath)
        {
          /* Parent deleted, this not-present or similar */
          working_node->repos_id = repos_id;
          working_node->repos_relpath
            = svn_relpath_join(parent_node->below_work->repos_relpath,
                               svn_relpath_basename(local_relpath, NULL),
                               result_pool);
          working_node->revision = parent_node->below_work->revision;
          working_node->op_depth = parent_node->below_work->op_depth;
        }
      else
        return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                 _("No copyfrom URL for '%s'"),
                                 svn_dirent_local_style(local_relpath,
                                                        scratch_pool));

      if (work && work->op_depth != working_node->op_depth
          && work->repos_relpath
          && work->repos_id == working_node->repos_id
          && work->presence == svn_wc__db_status_normal
          && !below_working_node)
        {
          /* Introduce a not-present node! */
          below_working_node = MAYBE_ALLOC(below_working_node, scratch_pool);

          below_working_node->wc_id = wc_id;
          below_working_node->op_depth = work->op_depth;
          below_working_node->local_relpath = local_relpath;
          below_working_node->parent_relpath = parent_relpath;

          below_working_node->presence = svn_wc__db_status_not_present;
          below_working_node->repos_id = repos_id;
          below_working_node->repos_relpath = working_node->local_relpath;

          SVN_ERR(insert_node(sdb, below_working_node, scratch_pool));

          below_working_node = NULL; /* Don't write a present intermediate! */
        }
    }

  if (entry->conflict_old)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      if (parent_relpath && entry->conflict_old)
        actual_node->conflict_old = svn_relpath_join(parent_relpath,
                                                     entry->conflict_old,
                                                     scratch_pool);
      else
        actual_node->conflict_old = entry->conflict_old;
      if (parent_relpath && entry->conflict_new)
        actual_node->conflict_new = svn_relpath_join(parent_relpath,
                                                     entry->conflict_new,
                                                     scratch_pool);
      else
        actual_node->conflict_new = entry->conflict_new;
      if (parent_relpath && entry->conflict_wrk)
        actual_node->conflict_working = svn_relpath_join(parent_relpath,
                                                         entry->conflict_wrk,
                                                         scratch_pool);
      else
        actual_node->conflict_working = entry->conflict_wrk;
    }

  if (entry->prejfile)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->prop_reject = svn_relpath_join((entry->kind == svn_node_dir
                                                   ? local_relpath
                                                   : parent_relpath),
                                                  entry->prejfile,
                                                  scratch_pool);
    }

  if (entry->changelist)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->changelist = entry->changelist;
    }

  /* ### set the text_mod value? */

  if (entry_node && entry->tree_conflict_data)
    {
      /* Issues #3840/#3916: 1.6 stores multiple tree conflicts on the
         parent node, 1.7 stores them directly on the conflicted nodes.
         So "((skel1) (skel2))" becomes "(skel1)" and "(skel2)" */
      svn_skel_t *skel;

      skel = svn_skel__parse(entry->tree_conflict_data,
                             strlen(entry->tree_conflict_data),
                             scratch_pool);
      tree_conflicts = apr_hash_make(result_pool);
      skel = skel->children;
      while (skel)
        {
          svn_wc_conflict_description2_t *conflict;
          svn_skel_t *new_skel;
          const char *key;

          /* *CONFLICT is allocated so it is safe to use a non-const pointer */
          SVN_ERR(svn_wc__deserialize_conflict(
                             (const svn_wc_conflict_description2_t**)&conflict,
                                               skel,
                                               svn_dirent_join(root_abspath,
                                                               local_relpath,
                                                               scratch_pool),
                                               scratch_pool, scratch_pool));

          WRITE_ENTRY_ASSERT(conflict->kind == svn_wc_conflict_kind_tree);

          SVN_ERR(svn_wc__serialize_conflict(&new_skel, conflict,
                                             scratch_pool, scratch_pool));

          /* Store in hash to be retrieved when writing the child
             row. */
          key = svn_dirent_skip_ancestor(root_abspath, conflict->local_abspath);
          svn_hash_sets(tree_conflicts, apr_pstrdup(result_pool, key),
                        svn_skel__unparse(new_skel, result_pool)->data);
          skel = skel->next;
        }
    }
  else
    tree_conflicts = NULL;

  if (parent_node && parent_node->tree_conflicts)
    {
      const char *tree_conflict_data =
          svn_hash_gets(parent_node->tree_conflicts, local_relpath);
      if (tree_conflict_data)
        {
          actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
          actual_node->tree_conflict_data = tree_conflict_data;
        }

      /* Reset hash so that we don't write the row again when writing
         actual-only nodes */
      svn_hash_sets(parent_node->tree_conflicts, local_relpath, NULL);
    }

  if (entry->file_external_path != NULL)
    {
      base_node = MAYBE_ALLOC(base_node, result_pool);
    }


  /* Insert the base node. */
  if (base_node)
    {
      base_node->wc_id = wc_id;
      base_node->local_relpath = local_relpath;
      base_node->op_depth = 0;
      base_node->parent_relpath = parent_relpath;
      base_node->revision = entry->revision;
      base_node->recorded_time = entry->text_time;
      base_node->recorded_size = entry->working_size;

      if (entry->depth != svn_depth_exclude)
        base_node->depth = entry->depth;
      else
        {
          base_node->presence = svn_wc__db_status_excluded;
          base_node->depth = svn_depth_infinity;
        }

      if (entry->deleted)
        {
          WRITE_ENTRY_ASSERT(base_node->presence
                             == svn_wc__db_status_not_present);
          /* ### should be svn_node_unknown, but let's store what we have. */
          base_node->kind = entry->kind;
        }
      else if (entry->absent)
        {
          WRITE_ENTRY_ASSERT(base_node->presence
                             == svn_wc__db_status_server_excluded);
          /* ### should be svn_node_unknown, but let's store what we have. */
          base_node->kind = entry->kind;

          /* Store the most likely revision in the node to avoid
             base nodes without a valid revision. Of course
             we remember that the data is still incomplete. */
          if (!SVN_IS_VALID_REVNUM(base_node->revision) && parent_node->base)
            base_node->revision = parent_node->base->revision;
        }
      else
        {
          base_node->kind = entry->kind;

          if (base_node->presence != svn_wc__db_status_excluded)
            {
              /* All subdirs are initially incomplete, they stop being
                 incomplete when the entries file in the subdir is
                 upgraded and remain incomplete if that doesn't happen. */
              if (entry->kind == svn_node_dir
                  && strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR))
                {
                  base_node->presence = svn_wc__db_status_incomplete;

                  /* Store the most likely revision in the node to avoid
                     base nodes without a valid revision. Of course
                     we remember that the data is still incomplete. */
                  if (parent_node->base)
                    base_node->revision = parent_node->base->revision;
                }
              else if (entry->incomplete)
                {
                  /* ### nobody should have set the presence.  */
                  WRITE_ENTRY_ASSERT(base_node->presence
                                     == svn_wc__db_status_normal);
                  base_node->presence = svn_wc__db_status_incomplete;
                }
            }
        }

      if (entry->kind == svn_node_dir)
        base_node->checksum = NULL;
      else
        {
          if (text_base_info && text_base_info->revert_base.sha1_checksum)
            base_node->checksum = text_base_info->revert_base.sha1_checksum;
          else if (text_base_info && text_base_info->normal_base.sha1_checksum)
            base_node->checksum = text_base_info->normal_base.sha1_checksum;
          else
            base_node->checksum = NULL;

          /* The base MD5 checksum is available in the entry, unless there
           * is a copied WORKING node.  If possible, verify that the entry
           * checksum matches the base file that we found. */
          if (! (working_node && entry->copied))
            {
              svn_checksum_t *entry_md5_checksum, *found_md5_checksum;
              SVN_ERR(svn_checksum_parse_hex(&entry_md5_checksum,
                                             svn_checksum_md5,
                                             entry->checksum, scratch_pool));
              if (text_base_info && text_base_info->revert_base.md5_checksum)
                found_md5_checksum = text_base_info->revert_base.md5_checksum;
              else if (text_base_info
                       && text_base_info->normal_base.md5_checksum)
                found_md5_checksum = text_base_info->normal_base.md5_checksum;
              else
                found_md5_checksum = NULL;
              if (entry_md5_checksum && found_md5_checksum &&
                  !svn_checksum_match(entry_md5_checksum, found_md5_checksum))
                return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                         _("Bad base MD5 checksum for '%s'; "
                                           "expected: '%s'; found '%s'; "),
                                       svn_dirent_local_style(
                                         svn_dirent_join(root_abspath,
                                                         local_relpath,
                                                         scratch_pool),
                                         scratch_pool),
                                       svn_checksum_to_cstring_display(
                                         entry_md5_checksum, scratch_pool),
                                       svn_checksum_to_cstring_display(
                                         found_md5_checksum, scratch_pool));
              else
                {
                  /* ### Not sure what conditions this should cover. */
                  /* SVN_ERR_ASSERT(entry->deleted || ...); */
                }
            }
        }

      if (this_dir->repos)
        {
          base_node->repos_id = repos_id;

          if (entry->url != NULL)
            {
              base_node->repos_relpath = svn_uri_skip_ancestor(
                                           this_dir->repos, entry->url,
                                           result_pool);
            }
          else
            {
              const char *relpath = svn_uri_skip_ancestor(this_dir->repos,
                                                          this_dir->url,
                                                          scratch_pool);
              if (relpath == NULL || *relpath == '\0')
                base_node->repos_relpath = entry->name;
              else
                base_node->repos_relpath =
                  svn_dirent_join(relpath, entry->name, result_pool);
            }
        }

      /* TODO: These values should always be present, if they are missing
         during an upgrade, set a flag, and then ask the user to talk to the
         server.

         Note: cmt_rev is the distinguishing value. The others may be 0 or
         NULL if the corresponding revprop has been deleted.  */
      base_node->changed_rev = entry->cmt_rev;
      base_node->changed_date = entry->cmt_date;
      base_node->changed_author = entry->cmt_author;

      if (entry->file_external_path)
        base_node->file_external = TRUE;

      /* Switched nodes get an empty iprops cache. */
      if (parent_node
          && is_switched(parent_node->base, base_node, scratch_pool))
        base_node->inherited_props
          = apr_array_make(scratch_pool, 0, sizeof(svn_prop_inherited_item_t*));

      SVN_ERR(insert_node(sdb, base_node, scratch_pool));

      /* We have to insert the lock after the base node, because the node
         must exist to lookup various bits of repos related information for
         the abs path. */
      if (entry->lock_token && create_locks)
        {
          svn_wc__db_lock_t lock;

          lock.token = entry->lock_token;
          lock.owner = entry->lock_owner;
          lock.comment = entry->lock_comment;
          lock.date = entry->lock_creation_date;

          SVN_ERR(svn_wc__db_lock_add(db, tmp_entry_abspath, &lock,
                                      scratch_pool));
        }
    }

  if (below_working_node)
    {
      db_node_t *work
        = parent_node->below_work ? parent_node->below_work : parent_node->work;

      below_working_node->wc_id = wc_id;
      below_working_node->local_relpath = local_relpath;
      below_working_node->op_depth = work->op_depth;
      below_working_node->parent_relpath = parent_relpath;
      below_working_node->presence = svn_wc__db_status_normal;
      below_working_node->kind = entry->kind;
      below_working_node->repos_id = work->repos_id;
      below_working_node->revision = work->revision;

      /* This is just guessing. If the node below would have been switched
         or if it was updated to a different version, the guess would
         fail. But we don't have better information pre wc-ng :( */
      if (work->repos_relpath)
        below_working_node->repos_relpath
          = svn_relpath_join(work->repos_relpath,
                             svn_relpath_basename(local_relpath, NULL),
                             result_pool);
      else
        below_working_node->repos_relpath = NULL;

      /* The revert_base checksum isn't available in the entry structure,
         so the caller provides it. */

      /* text_base_info is NULL for files scheduled to be added. */
      below_working_node->checksum = NULL;
      if (text_base_info)
        {
          if (entry->schedule == svn_wc_schedule_delete)
            below_working_node->checksum =
              text_base_info->normal_base.sha1_checksum;
          else
            below_working_node->checksum =
              text_base_info->revert_base.sha1_checksum;
        }
      below_working_node->recorded_size = 0;
      below_working_node->changed_rev = SVN_INVALID_REVNUM;
      below_working_node->changed_date = 0;
      below_working_node->changed_author = NULL;
      below_working_node->depth = svn_depth_infinity;
      below_working_node->recorded_time = 0;
      below_working_node->properties = NULL;

      if (working_node
          && entry->schedule == svn_wc_schedule_delete
          && working_node->repos_relpath)
        {
          /* We are lucky, our guesses above are not necessary. The known
             correct information is in working. But our op_depth design
             expects more information here */
          below_working_node->repos_relpath = working_node->repos_relpath;
          below_working_node->repos_id = working_node->repos_id;
          below_working_node->revision = working_node->revision;

          /* Nice for 'svn status' */
          below_working_node->changed_rev = entry->cmt_rev;
          below_working_node->changed_date = entry->cmt_date;
          below_working_node->changed_author = entry->cmt_author;

          /* And now remove it from WORKING, because in wc-ng code
             should read it from the lower layer */
          working_node->repos_relpath = NULL;
          working_node->repos_id = 0;
          working_node->revision = SVN_INVALID_REVNUM;
        }

      SVN_ERR(insert_node(sdb, below_working_node, scratch_pool));
    }

  /* Insert the working node. */
  if (working_node)
    {
      working_node->wc_id = wc_id;
      working_node->local_relpath = local_relpath;
      working_node->parent_relpath = parent_relpath;
      working_node->changed_rev = SVN_INVALID_REVNUM;
      working_node->recorded_time = entry->text_time;
      working_node->recorded_size = entry->working_size;

      if (entry->depth != svn_depth_exclude)
        working_node->depth = entry->depth;
      else
        {
          working_node->presence = svn_wc__db_status_excluded;
          working_node->depth = svn_depth_infinity;
        }

      if (entry->kind == svn_node_dir)
        working_node->checksum = NULL;
      else
        {
          working_node->checksum = NULL;
          /* text_base_info is NULL for files scheduled to be added. */
          if (text_base_info)
            working_node->checksum = text_base_info->normal_base.sha1_checksum;


          /* If an MD5 checksum is present in the entry, we can verify that
           * it matches the MD5 of the base file we found earlier. */
#ifdef SVN_DEBUG
          if (entry->checksum && text_base_info)
          {
            svn_checksum_t *md5_checksum;
            SVN_ERR(svn_checksum_parse_hex(&md5_checksum, svn_checksum_md5,
                                           entry->checksum, result_pool));
            SVN_ERR_ASSERT(
              md5_checksum && text_base_info->normal_base.md5_checksum);
            SVN_ERR_ASSERT(svn_checksum_match(
              md5_checksum, text_base_info->normal_base.md5_checksum));
          }
#endif
        }

      working_node->kind = entry->kind;
      if (working_node->presence != svn_wc__db_status_excluded)
        {
          /* All subdirs start of incomplete, and stop being incomplete
             when the entries file in the subdir is upgraded. */
          if (entry->kind == svn_node_dir
              && strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR))
            {
              working_node->presence = svn_wc__db_status_incomplete;
              working_node->kind = svn_node_dir;
            }
          else if (entry->schedule == svn_wc_schedule_delete)
            {
              working_node->presence = svn_wc__db_status_base_deleted;
              working_node->kind = entry->kind;
            }
          else
            {
              /* presence == normal  */
              working_node->kind = entry->kind;

              if (entry->incomplete)
                {
                  /* We shouldn't be overwriting another status.  */
                  WRITE_ENTRY_ASSERT(working_node->presence
                                     == svn_wc__db_status_normal);
                  working_node->presence = svn_wc__db_status_incomplete;
                }
            }
        }

      /* These should generally be unset for added and deleted files,
         and contain whatever information we have for copied files. Let's
         just store whatever we have.

         Note: cmt_rev is the distinguishing value. The others may be 0 or
         NULL if the corresponding revprop has been deleted.  */
      if (working_node->presence != svn_wc__db_status_base_deleted)
        {
          working_node->changed_rev = entry->cmt_rev;
          working_node->changed_date = entry->cmt_date;
          working_node->changed_author = entry->cmt_author;
        }

      if (entry->schedule == svn_wc_schedule_delete
          && parent_node->work
          && parent_node->work->presence == svn_wc__db_status_base_deleted)
        {
          working_node->op_depth = parent_node->work->op_depth;
        }
      else if (working_node->presence == svn_wc__db_status_excluded
               && parent_node->work)
        {
          working_node->op_depth = parent_node->work->op_depth;
        }
      else if (!entry->copied)
        {
          working_node->op_depth
            = svn_wc__db_op_depth_for_upgrade(local_relpath);
        }

      SVN_ERR(insert_node(sdb, working_node, scratch_pool));
    }

  /* Insert the actual node. */
  if (actual_node)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);

      actual_node->wc_id = wc_id;
      actual_node->local_relpath = local_relpath;
      actual_node->parent_relpath = parent_relpath;

      SVN_ERR(insert_actual_node(sdb, db, tmp_entry_abspath,
                                 actual_node, scratch_pool));
    }

  if (entry_node)
    {
      *entry_node = apr_palloc(result_pool, sizeof(**entry_node));
      (*entry_node)->base = base_node;
      (*entry_node)->work = working_node;
      (*entry_node)->below_work = below_working_node;
      (*entry_node)->tree_conflicts = tree_conflicts;
    }

  if (entry->file_external_path)
    {
      /* TODO: Maybe add a file external registration inside EXTERNALS here,
               to allow removing file externals that aren't referenced from
               svn:externals.

         The svn:externals values are processed anyway after everything is
         upgraded */
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
write_actual_only_entries(apr_hash_t *tree_conflicts,
                          svn_sqlite__db_t *sdb,
                          svn_wc__db_t *db,
                          const char *wri_abspath,
                          apr_int64_t wc_id,
                          const char *parent_relpath,
                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, tree_conflicts);
       hi;
       hi = apr_hash_next(hi))
    {
      db_actual_node_t *actual_node = NULL;

      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->wc_id = wc_id;
      actual_node->local_relpath = apr_hash_this_key(hi);
      actual_node->parent_relpath = parent_relpath;
      actual_node->tree_conflict_data = apr_hash_this_val(hi);

      SVN_ERR(insert_actual_node(sdb, db, wri_abspath, actual_node,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__write_upgraded_entries(void **dir_baton,
                               void *parent_baton,
                               svn_wc__db_t *db,
                               svn_sqlite__db_t *sdb,
                               apr_int64_t repos_id,
                               apr_int64_t wc_id,
                               const char *dir_abspath,
                               const char *new_root_abspath,
                               apr_hash_t *entries,
                               apr_hash_t *text_bases_info,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *this_dir;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *old_root_abspath, *dir_relpath;
  struct write_baton *parent_node = parent_baton;
  struct write_baton *dir_node;

  /* Get a copy of the "this dir" entry for comparison purposes. */
  this_dir = svn_hash_gets(entries, SVN_WC_ENTRY_THIS_DIR);

  /* If there is no "this dir" entry, something is wrong. */
  if (! this_dir)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No default entry in directory '%s'"),
                             svn_dirent_local_style(dir_abspath,
                                                    iterpool));
  old_root_abspath = svn_dirent_get_longest_ancestor(dir_abspath,
                                                     new_root_abspath,
                                                     scratch_pool);

  SVN_ERR_ASSERT(old_root_abspath[0]);

  dir_relpath = svn_dirent_skip_ancestor(old_root_abspath, dir_abspath);

  /* Write out "this dir" */
  SVN_ERR(write_entry(&dir_node, parent_node, db, sdb,
                      wc_id, repos_id, this_dir, NULL, dir_relpath,
                      svn_dirent_join(new_root_abspath, dir_relpath,
                                      iterpool),
                      old_root_abspath,
                      this_dir, FALSE, result_pool, iterpool));

  for (hi = apr_hash_first(scratch_pool, entries); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      const svn_wc_entry_t *this_entry = apr_hash_this_val(hi);
      const char *child_abspath, *child_relpath;
      svn_wc__text_base_info_t *text_base_info
        = svn_hash_gets(text_bases_info, name);

      svn_pool_clear(iterpool);

      /* Don't rewrite the "this dir" entry! */
      if (strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Write the entry. Pass TRUE for create locks, because we still
         use this function for upgrading old working copies. */
      child_abspath = svn_dirent_join(dir_abspath, name, iterpool);
      child_relpath = svn_dirent_skip_ancestor(old_root_abspath, child_abspath);
      SVN_ERR(write_entry(NULL, dir_node, db, sdb,
                          wc_id, repos_id,
                          this_entry, text_base_info, child_relpath,
                          svn_dirent_join(new_root_abspath, child_relpath,
                                          iterpool),
                          old_root_abspath,
                          this_dir, TRUE, iterpool, iterpool));
    }

  if (dir_node->tree_conflicts)
    SVN_ERR(write_actual_only_entries(dir_node->tree_conflicts, sdb, db,
                                      new_root_abspath, wc_id, dir_relpath,
                                      iterpool));

  *dir_baton = dir_node;
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_wc_entry_t *
svn_wc_entry_dup(const svn_wc_entry_t *entry, apr_pool_t *pool)
{
  svn_wc_entry_t *dupentry = apr_palloc(pool, sizeof(*dupentry));

  /* Perform a trivial copy ... */
  *dupentry = *entry;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (entry->name)
    dupentry->name = apr_pstrdup(pool, entry->name);
  if (entry->url)
    dupentry->url = apr_pstrdup(pool, entry->url);
  if (entry->repos)
    dupentry->repos = apr_pstrdup(pool, entry->repos);
  if (entry->uuid)
    dupentry->uuid = apr_pstrdup(pool, entry->uuid);
  if (entry->copyfrom_url)
    dupentry->copyfrom_url = apr_pstrdup(pool, entry->copyfrom_url);
  if (entry->conflict_old)
    dupentry->conflict_old = apr_pstrdup(pool, entry->conflict_old);
  if (entry->conflict_new)
    dupentry->conflict_new = apr_pstrdup(pool, entry->conflict_new);
  if (entry->conflict_wrk)
    dupentry->conflict_wrk = apr_pstrdup(pool, entry->conflict_wrk);
  if (entry->prejfile)
    dupentry->prejfile = apr_pstrdup(pool, entry->prejfile);
  if (entry->checksum)
    dupentry->checksum = apr_pstrdup(pool, entry->checksum);
  if (entry->cmt_author)
    dupentry->cmt_author = apr_pstrdup(pool, entry->cmt_author);
  if (entry->lock_token)
    dupentry->lock_token = apr_pstrdup(pool, entry->lock_token);
  if (entry->lock_owner)
    dupentry->lock_owner = apr_pstrdup(pool, entry->lock_owner);
  if (entry->lock_comment)
    dupentry->lock_comment = apr_pstrdup(pool, entry->lock_comment);
  if (entry->changelist)
    dupentry->changelist = apr_pstrdup(pool, entry->changelist);

  /* NOTE: we do not dup cachable_props or present_props since they
     are deprecated. Use "" to indicate "nothing cachable or cached". */
  dupentry->cachable_props = "";
  dupentry->present_props = "";

  if (entry->tree_conflict_data)
    dupentry->tree_conflict_data = apr_pstrdup(pool,
                                               entry->tree_conflict_data);
  if (entry->file_external_path)
    dupentry->file_external_path = apr_pstrdup(pool,
                                               entry->file_external_path);
  return dupentry;
}


/*** Generic Entry Walker */

/* A recursive entry-walker, helper for svn_wc_walk_entries3().
 *
 * For this directory (DIRPATH, ADM_ACCESS), call the "found_entry" callback
 * in WALK_CALLBACKS, passing WALK_BATON to it. Then, for each versioned
 * entry in this directory, call the "found entry" callback and then recurse
 * (if it is a directory and if DEPTH allows).
 *
 * If SHOW_HIDDEN is true, include entries that are in a 'deleted' or
 * 'absent' state (and not scheduled for re-addition), else skip them.
 *
 * Call CANCEL_FUNC with CANCEL_BATON to allow cancellation.
 */
static svn_error_t *
walker_helper(const char *dirpath,
              svn_wc_adm_access_t *adm_access,
              const svn_wc_entry_callbacks2_t *walk_callbacks,
              void *walk_baton,
              svn_depth_t depth,
              svn_boolean_t show_hidden,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *dot_entry;
  svn_error_t *err;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);

  err = svn_wc__entries_read_internal(&entries, adm_access, show_hidden,
                                      pool);

  if (err)
    SVN_ERR(walk_callbacks->handle_error(dirpath, err, walk_baton, pool));

  /* As promised, always return the '.' entry first. */
  dot_entry = svn_hash_gets(entries, SVN_WC_ENTRY_THIS_DIR);
  if (! dot_entry)
    return walk_callbacks->handle_error
      (dirpath, svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                  _("Directory '%s' has no THIS_DIR entry"),
                                  svn_dirent_local_style(dirpath, pool)),
       walk_baton, pool);

  /* Call the "found entry" callback for this directory as a "this dir"
   * entry. Note that if this directory has been reached by recursion, this
   * is the second visit as it will already have been visited once as a
   * child entry of its parent. */

  err = walk_callbacks->found_entry(dirpath, dot_entry, walk_baton, subpool);


  if(err)
    SVN_ERR(walk_callbacks->handle_error(dirpath, err, walk_baton, pool));

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Loop over each of the other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      const svn_wc_entry_t *current_entry = apr_hash_this_val(hi);
      const char *entrypath;
      const char *entry_abspath;
      svn_boolean_t hidden;

      svn_pool_clear(subpool);

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Skip the "this dir" entry. */
      if (strcmp(current_entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      entrypath = svn_dirent_join(dirpath, name, subpool);
      SVN_ERR(svn_wc__entry_is_hidden(&hidden, current_entry));
      SVN_ERR(svn_dirent_get_absolute(&entry_abspath, entrypath, subpool));

      /* Call the "found entry" callback for this entry. (For a directory,
       * this is the first visit: as a child.) */
      if (current_entry->kind == svn_node_file
          || depth >= svn_depth_immediates)
        {
          err = walk_callbacks->found_entry(entrypath, current_entry,
                                            walk_baton, subpool);

          if (err)
            SVN_ERR(walk_callbacks->handle_error(entrypath, err,
                                                 walk_baton, pool));
        }

      /* Recurse into this entry if appropriate. */
      if (current_entry->kind == svn_node_dir
          && !hidden
          && depth >= svn_depth_immediates)
        {
          svn_wc_adm_access_t *entry_access;
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          entry_access = svn_wc__adm_retrieve_internal2(db, entry_abspath,
                                                        subpool);

          if (entry_access)
            SVN_ERR(walker_helper(entrypath, entry_access,
                                  walk_callbacks, walk_baton,
                                  depth_below_here, show_hidden,
                                  cancel_func, cancel_baton,
                                  subpool));
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__walker_default_error_handler(const char *path,
                                     svn_error_t *err,
                                     void *walk_baton,
                                     apr_pool_t *pool)
{
  /* Note: don't trace this. We don't want to insert a false "stack frame"
     onto an error generated elsewhere.  */
  return svn_error_trace(err);
}


/* The public API. */
svn_error_t *
svn_wc_walk_entries3(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks2_t *walk_callbacks,
                     void *walk_baton,
                     svn_depth_t walk_depth,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  svn_error_t *err;
  svn_node_kind_t kind;
  svn_wc__db_status_t status;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  err = svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL,
                             db, local_abspath,
                             pool, pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);
      /* Remap into SVN_ERR_UNVERSIONED_RESOURCE.  */
      svn_error_clear(err);
      return walk_callbacks->handle_error(
        path, svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                _("'%s' is not under version control"),
                                svn_dirent_local_style(local_abspath, pool)),
        walk_baton, pool);
    }

  if (kind == svn_node_file
      || status == svn_wc__db_status_excluded
      || status == svn_wc__db_status_server_excluded)
    {
      const svn_wc_entry_t *entry;

      /* ### we should stop passing out entry structures.
         ###
         ### we should not call handle_error for an error the *callback*
         ###   gave us. let it deal with the problem before returning.  */

      if (!show_hidden
          && (status == svn_wc__db_status_not_present
              || status == svn_wc__db_status_excluded
              || status == svn_wc__db_status_server_excluded))
        {
          /* The fool asked to walk a "hidden" node. Report the node as
              unversioned.

              ### this is incorrect behavior. see depth_test 36. the walk
              ### API will be revamped to avoid entry structures. we should
              ### be able to solve the problem with the new API. (since we
              ### shouldn't return a hidden entry here)  */
          return walk_callbacks->handle_error(
                            path, svn_error_createf(
                              SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              _("'%s' is not under version control"),
                              svn_dirent_local_style(local_abspath, pool)),
                            walk_baton, pool);
        }

      SVN_ERR(svn_wc__get_entry(&entry, db, local_abspath, FALSE,
                                svn_node_file, pool, pool));

      err = walk_callbacks->found_entry(path, entry, walk_baton, pool);
      if (err)
        return walk_callbacks->handle_error(path, err, walk_baton, pool);

      return SVN_NO_ERROR;
    }

  if (kind == svn_node_dir)
    return walker_helper(path, adm_access, walk_callbacks, walk_baton,
                         walk_depth, show_hidden, cancel_func, cancel_baton,
                         pool);

  return walk_callbacks->handle_error(
       path, svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' has an unrecognized node kind"),
                               svn_dirent_local_style(local_abspath, pool)),
       walk_baton, pool);
}
