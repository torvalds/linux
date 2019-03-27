/*
 * wc_db.c :  manipulating the administrative database
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

#define SVN_WC__I_AM_WC_DB

#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_private_config.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "svn_wc.h"
#include "svn_checksum.h"
#include "svn_pools.h"

#include "wc.h"
#include "wc_db.h"
#include "adm_files.h"
#include "wc-queries.h"
#include "entries.h"
#include "lock.h"
#include "conflicts.h"
#include "wc_db_private.h"
#include "workqueue.h"
#include "token-map.h"

#include "private/svn_sorts_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_skel.h"
#include "private/svn_wc_private.h"
#include "private/svn_token.h"


#define NOT_IMPLEMENTED() SVN__NOT_IMPLEMENTED()


/*
 * Some filename constants.
 */
#define SDB_FILE  "wc.db"

#define WCROOT_TEMPDIR_RELPATH   "tmp"


/*
 * PARAMETER ASSERTIONS
 *
 * Every (semi-)public entrypoint in this file has a set of assertions on
 * the parameters passed into the function. Since this is a brand new API,
 * we want to make sure that everybody calls it properly. The original WC
 * code had years to catch stray bugs, but we do not have that luxury in
 * the wc-nb rewrite. Any extra assurances that we can find will be
 * welcome. The asserts will ensure we have no doubt about the values
 * passed into the function.
 *
 * Some parameters are *not* specifically asserted. Typically, these are
 * params that will be used immediately, so something like a NULL value
 * will be obvious.
 *
 * ### near 1.7 release, it would be a Good Thing to review the assertions
 * ### and decide if any can be removed or switched to assert() in order
 * ### to remove their runtime cost in the production release.
 *
 *
 * DATABASE OPERATIONS
 *
 * Each function should leave the database in a consistent state. If it
 * does *not*, then the implication is some other function needs to be
 * called to restore consistency. Subtle requirements like that are hard
 * to maintain over a long period of time, so this API will not allow it.
 *
 *
 * STANDARD VARIABLE NAMES
 *
 * db     working copy database (this module)
 * sdb    SQLite database (not to be confused with 'db')
 * wc_id  a WCROOT id associated with a node
 */

#define INVALID_REPOS_ID ((apr_int64_t) -1)
#define UNKNOWN_WC_ID ((apr_int64_t) -1)
#define FORMAT_FROM_SDB (-1)

/* Check if column number I, a property-skel column, contains a non-empty
   set of properties. The empty set of properties is stored as "()", so we
   have properties if the size of the column is larger than 2. */
#define SQLITE_PROPERTIES_AVAILABLE(stmt, i) \
                 (svn_sqlite__column_bytes(stmt, i) > 2)

int
svn_wc__db_op_depth_for_upgrade(const char *local_relpath)
{
  return relpath_depth(local_relpath);
}


/* Representation of a new base row for the NODES table */
typedef struct insert_base_baton_t {
  /* common to all insertions into BASE */
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  apr_int64_t repos_id;
  const char *repos_relpath;
  svn_revnum_t revision;

  /* Only used when repos_id == INVALID_REPOS_ID */
  const char *repos_root_url;
  const char *repos_uuid;

  /* common to all "normal" presence insertions */
  const apr_hash_t *props;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const apr_hash_t *dav_cache;

  /* for inserting directories */
  const apr_array_header_t *children;
  svn_depth_t depth;

  /* for inserting files */
  const svn_checksum_t *checksum;

  /* for inserting symlinks */
  const char *target;

  svn_boolean_t file_external;

  /* may need to insert/update ACTUAL to record a conflict  */
  const svn_skel_t *conflict;

  /* may need to insert/update ACTUAL to record new properties */
  svn_boolean_t update_actual_props;
  const apr_hash_t *new_actual_props;

  /* A depth-first ordered array of svn_prop_inherited_item_t *
     structures representing the properties inherited by the base
     node. */
  apr_array_header_t *iprops;

  /* maybe we should copy information from a previous record? */
  svn_boolean_t keep_recorded_info;

  /* insert a base-deleted working node as well as a base node */
  svn_boolean_t insert_base_deleted;

  /* delete the current working nodes above BASE */
  svn_boolean_t delete_working;

  /* may have work items to queue in this transaction  */
  const svn_skel_t *work_items;

} insert_base_baton_t;


/* Representation of a new working row for the NODES table */
typedef struct insert_working_baton_t {
  /* common to all insertions into WORKING (including NODE_DATA) */
  svn_wc__db_status_t presence;
  svn_node_kind_t kind;
  int op_depth;

  /* common to all "normal" presence insertions */
  const apr_hash_t *props;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_int64_t original_repos_id;
  const char *original_repos_relpath;
  svn_revnum_t original_revnum;
  svn_boolean_t moved_here;

  /* for inserting directories */
  const apr_array_header_t *children;
  svn_depth_t depth;

  /* for inserting (copied/moved-here) files */
  const svn_checksum_t *checksum;

  /* for inserting symlinks */
  const char *target;

  svn_boolean_t update_actual_props;
  const apr_hash_t *new_actual_props;

  /* may have work items to queue in this transaction  */
  const svn_skel_t *work_items;

  /* may have conflict to install in this transaction */
  const svn_skel_t *conflict;

  /* If the value is > 0 and < op_depth, also insert a not-present
     at op-depth NOT_PRESENT_OP_DEPTH, based on this same information */
  int not_present_op_depth;

} insert_working_baton_t;

/* Representation of a new row for the EXTERNALS table */
typedef struct insert_external_baton_t {
  /* common to all insertions into EXTERNALS */
  svn_node_kind_t kind;
  svn_wc__db_status_t presence;

  /* The repository of the external */
  apr_int64_t repos_id;
  /* for file and symlink externals */
  const char *repos_relpath;
  svn_revnum_t revision;

  /* Only used when repos_id == INVALID_REPOS_ID */
  const char *repos_root_url;
  const char *repos_uuid;

  /* for file and symlink externals */
  const apr_hash_t *props;
  apr_array_header_t *iprops;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const apr_hash_t *dav_cache;

  /* for inserting files */
  const svn_checksum_t *checksum;

  /* for inserting symlinks */
  const char *target;

  const char *record_ancestor_relpath;
  const char *recorded_repos_relpath;
  svn_revnum_t recorded_peg_revision;
  svn_revnum_t recorded_revision;

  /* may need to insert/update ACTUAL to record a conflict  */
  const svn_skel_t *conflict;

  /* may need to insert/update ACTUAL to record new properties */
  svn_boolean_t update_actual_props;
  const apr_hash_t *new_actual_props;

  /* maybe we should copy information from a previous record? */
  svn_boolean_t keep_recorded_info;

  /* may have work items to queue in this transaction  */
  const svn_skel_t *work_items;

} insert_external_baton_t;


/* Forward declarations  */
static svn_error_t *
add_work_items(svn_sqlite__db_t *sdb,
               const svn_skel_t *skel,
               apr_pool_t *scratch_pool);

static svn_error_t *
set_actual_props(svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 apr_hash_t *props,
                 apr_pool_t *scratch_pool);

static svn_error_t *
insert_incomplete_children(svn_sqlite__db_t *sdb,
                           apr_int64_t wc_id,
                           const char *local_relpath,
                           apr_int64_t repos_id,
                           const char *repos_relpath,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           int op_depth,
                           apr_pool_t *scratch_pool);

static svn_error_t *
db_read_pristine_props(apr_hash_t **props,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       svn_boolean_t deleted_ok,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

static svn_error_t *
read_info(svn_wc__db_status_t *status,
          svn_node_kind_t *kind,
          svn_revnum_t *revision,
          const char **repos_relpath,
          apr_int64_t *repos_id,
          svn_revnum_t *changed_rev,
          apr_time_t *changed_date,
          const char **changed_author,
          svn_depth_t *depth,
          const svn_checksum_t **checksum,
          const char **target,
          const char **original_repos_relpath,
          apr_int64_t *original_repos_id,
          svn_revnum_t *original_revision,
          svn_wc__db_lock_t **lock,
          svn_filesize_t *recorded_size,
          apr_time_t *recorded_time,
          const char **changelist,
          svn_boolean_t *conflicted,
          svn_boolean_t *op_root,
          svn_boolean_t *had_props,
          svn_boolean_t *props_mod,
          svn_boolean_t *have_base,
          svn_boolean_t *have_more_work,
          svn_boolean_t *have_work,
          svn_wc__db_wcroot_t *wcroot,
          const char *local_relpath,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool);

static svn_error_t *
scan_addition(svn_wc__db_status_t *status,
              const char **op_root_relpath,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              const char **original_repos_relpath,
              apr_int64_t *original_repos_id,
              svn_revnum_t *original_revision,
              const char **moved_from_relpath,
              const char **moved_from_op_root_relpath,
              int *moved_from_op_depth,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool);

static svn_error_t *
convert_to_working_status(svn_wc__db_status_t *working_status,
                          svn_wc__db_status_t status);

static svn_error_t *
db_is_switched(svn_boolean_t *is_switched,
               svn_node_kind_t *kind,
               svn_wc__db_wcroot_t *wcroot,
               const char *local_relpath,
               apr_pool_t *scratch_pool);


/* Return the absolute path, in local path style, of LOCAL_RELPATH
   in WCROOT.  */
static const char *
path_for_error_message(const svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool)
{
  const char *local_abspath
    = svn_dirent_join(wcroot->abspath, local_relpath, result_pool);

  return svn_dirent_local_style(local_abspath, result_pool);
}


/* Return a file size from column SLOT of the SQLITE statement STMT, or
   SVN_INVALID_FILESIZE if the column value is NULL.  */
static svn_filesize_t
get_recorded_size(svn_sqlite__stmt_t *stmt, int slot)
{
  if (svn_sqlite__column_is_null(stmt, slot))
    return SVN_INVALID_FILESIZE;
  return svn_sqlite__column_int64(stmt, slot);
}


/* Return a lock info structure constructed from the given columns of the
   SQLITE statement STMT, or return NULL if the token column value is null.  */
static svn_wc__db_lock_t *
lock_from_columns(svn_sqlite__stmt_t *stmt,
                  int col_token,
                  int col_owner,
                  int col_comment,
                  int col_date,
                  apr_pool_t *result_pool)
{
  svn_wc__db_lock_t *lock;

  if (svn_sqlite__column_is_null(stmt, col_token))
    {
      lock = NULL;
    }
  else
    {
      lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
      lock->token = svn_sqlite__column_text(stmt, col_token, result_pool);
      lock->owner = svn_sqlite__column_text(stmt, col_owner, result_pool);
      lock->comment = svn_sqlite__column_text(stmt, col_comment, result_pool);
      lock->date = svn_sqlite__column_int64(stmt, col_date);
    }
  return lock;
}


svn_error_t *
svn_wc__db_fetch_repos_info(const char **repos_root_url,
                            const char **repos_uuid,
                            svn_wc__db_wcroot_t *wcroot,
                            apr_int64_t repos_id,
                            apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  if (!repos_root_url && !repos_uuid)
    return SVN_NO_ERROR;

  if (repos_id == INVALID_REPOS_ID)
    {
      if (repos_root_url)
        *repos_root_url = NULL;
      if (repos_uuid)
        *repos_uuid = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REPOSITORY_BY_ID));
  SVN_ERR(svn_sqlite__bindf(stmt, "i", repos_id));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                             _("No REPOSITORY table entry for id '%ld'"),
                             (long int)repos_id);

  if (repos_root_url)
    *repos_root_url = svn_sqlite__column_text(stmt, 0, result_pool);
  if (repos_uuid)
    *repos_uuid = svn_sqlite__column_text(stmt, 1, result_pool);

  return svn_error_trace(svn_sqlite__reset(stmt));
}

/* Set *REPOS_ID, *REVISION and *REPOS_RELPATH from the given columns of the
   SQLITE statement STMT, or to NULL/SVN_INVALID_REVNUM if the respective
   column value is null.  Any of the output parameters may be NULL if not
   required.  */
static void
repos_location_from_columns(apr_int64_t *repos_id,
                            svn_revnum_t *revision,
                            const char **repos_relpath,
                            svn_sqlite__stmt_t *stmt,
                            int col_repos_id,
                            int col_revision,
                            int col_repos_relpath,
                            apr_pool_t *result_pool)
{
  if (repos_id)
    {
      /* Fetch repository information via REPOS_ID. */
      if (svn_sqlite__column_is_null(stmt, col_repos_id))
        *repos_id = INVALID_REPOS_ID;
      else
        *repos_id = svn_sqlite__column_int64(stmt, col_repos_id);
    }
  if (revision)
    {
      *revision = svn_sqlite__column_revnum(stmt, col_revision);
    }
  if (repos_relpath)
    {
      *repos_relpath = svn_sqlite__column_text(stmt, col_repos_relpath,
                                               result_pool);
    }
}

/* For a given REPOS_ROOT_URL/REPOS_UUID pair, return the existing REPOS_ID
   value. If one does not exist, then create a new one. */
static svn_error_t *
create_repos_id(apr_int64_t *repos_id,
                const char *repos_root_url,
                const char *repos_uuid,
                svn_sqlite__db_t *sdb,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *get_stmt;
  svn_sqlite__stmt_t *insert_stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&get_stmt, sdb, STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(get_stmt, "s", repos_root_url));
  SVN_ERR(svn_sqlite__step(&have_row, get_stmt));

  if (have_row)
    {
      *repos_id = svn_sqlite__column_int64(get_stmt, 0);
      return svn_error_trace(svn_sqlite__reset(get_stmt));
    }
  SVN_ERR(svn_sqlite__reset(get_stmt));

  /* NOTE: strictly speaking, there is a race condition between the
     above query and the insertion below. We're simply going to ignore
     that, as it means two processes are *modifying* the working copy
     at the same time, *and* new repositores are becoming visible.
     This is rare enough, let alone the miniscule chance of hitting
     this race condition. Further, simply failing out will leave the
     database in a consistent state, and the user can just re-run the
     failed operation. */

  SVN_ERR(svn_sqlite__get_statement(&insert_stmt, sdb,
                                    STMT_INSERT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(insert_stmt, "ss", repos_root_url, repos_uuid));
  return svn_error_trace(svn_sqlite__insert(repos_id, insert_stmt));
}


/* Initialize the baton with appropriate "blank" values. This allows the
   insertion function to leave certain columns null.  */
static void
blank_ibb(insert_base_baton_t *pibb)
{
  memset(pibb, 0, sizeof(*pibb));
  pibb->revision = SVN_INVALID_REVNUM;
  pibb->changed_rev = SVN_INVALID_REVNUM;
  pibb->depth = svn_depth_infinity;
  pibb->repos_id = INVALID_REPOS_ID;
}


/* Extend any delete of the parent of LOCAL_RELPATH to LOCAL_RELPATH.

   ### What about KIND and OP_DEPTH?  KIND ought to be redundant; I'm
       discussing on dev@ whether we can let that be null for presence
       == base-deleted.  OP_DEPTH is the op-depth of what, and why?
       It is used to select the lowest working node higher than OP_DEPTH,
       so, in terms of the API, OP_DEPTH means ...?

   Given a wc:

              0         1         2         3         4
              normal
   A          normal
   A/B        normal              normal
   A/B/C                          not-pres  normal
   A/B/C/D                                            normal

   That is checkout, delete A/B, copy a replacement A/B, delete copied
   child A/B/C, add replacement A/B/C, add A/B/C/D.

   Now an update that adds base nodes for A/B/C, A/B/C/D and A/B/C/D/E
   must extend the A/B deletion:

              0         1         2         3         4
              normal
   A          normal
   A/B        normal              normal
   A/B/C      normal              not-pres  normal
   A/B/C/D    normal              base-del            normal
   A/B/C/D/E  normal              base-del

   When adding a node if the parent has a higher working node then the
   parent node is deleted (or replaced) and the delete must be extended
   to cover new node.

   In the example above A/B/C/D and A/B/C/D/E are the nodes that get
   the extended delete, A/B/C is already deleted.

   If ADDED_DELETE is not NULL, set *ADDED_DELETE to TRUE if a new delete
   was recorded, otherwise to FALSE.
 */
static svn_error_t *
db_extend_parent_delete(svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        svn_node_kind_t kind,
                        int op_depth,
                        apr_pool_t *scratch_pool)
{
  svn_boolean_t have_row;
  svn_sqlite__stmt_t *stmt;
  int parent_op_depth;
  const char *parent_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

  SVN_ERR_ASSERT(local_relpath[0]);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, parent_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    parent_op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    {
      int existing_op_depth;

      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        existing_op_depth = svn_sqlite__column_int(stmt, 0);
      SVN_ERR(svn_sqlite__reset(stmt));
      if (!have_row || parent_op_depth < existing_op_depth)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                              STMT_INSTALL_WORKING_NODE_FOR_DELETE));
          SVN_ERR(svn_sqlite__bindf(stmt, "isdst", wcroot->wc_id,
                                    local_relpath, parent_op_depth,
                                    parent_relpath, kind_map, kind));
          SVN_ERR(svn_sqlite__update(NULL, stmt));
        }
    }

  return SVN_NO_ERROR;
}


/* This is the reverse of db_extend_parent_delete.

   When removing a node if the parent has a higher working node then
   the parent node and this node are both deleted or replaced and any
   delete over this node must be removed.

   This function (like most wcroot functions) assumes that its caller
   only uses this function within an sqlite transaction if atomic
   behavior is needed.
 */
static svn_error_t *
db_retract_parent_delete(svn_wc__db_wcroot_t *wcroot,
                         const char *local_relpath,
                         int op_depth,
                         apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int working_depth;
  svn_wc__db_status_t presence;
  const char *moved_to;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_trace(svn_sqlite__reset(stmt));

  working_depth = svn_sqlite__column_int(stmt, 0);
  presence = svn_sqlite__column_token(stmt, 1, presence_map);
  moved_to = svn_sqlite__column_text(stmt, 3, scratch_pool);

  SVN_ERR(svn_sqlite__reset(stmt));

  if (moved_to)
    {
      /* Turn the move into a copy to keep the NODES table valid */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_CLEAR_MOVED_HERE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                moved_to, relpath_depth(moved_to)));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* This leaves just the moved_to information on the origin,
         which we will remove in the next step */
    }

  if (presence == svn_wc__db_status_base_deleted)
    {
      /* Nothing left to shadow; remove the base-deleted node */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_DELETE_NODE));
    }
  else if (moved_to)
    {
      /* Clear moved to information, as this node is no longer base-deleted */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_CLEAR_MOVED_TO_RELPATH));
      }
  else
    {
      /* Nothing to update */
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            working_depth));

  return svn_error_trace(svn_sqlite__update(NULL, stmt));
}



/* Insert the base row represented by (insert_base_baton_t *) BATON. */
static svn_error_t *
insert_base_node(const insert_base_baton_t *pibb,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 apr_pool_t *scratch_pool)
{
  apr_int64_t repos_id = pibb->repos_id;
  svn_sqlite__stmt_t *stmt;
  svn_filesize_t recorded_size = SVN_INVALID_FILESIZE;
  apr_int64_t recorded_time;
  svn_boolean_t present;

  /* The directory at the WCROOT has a NULL parent_relpath. Otherwise,
     bind the appropriate parent_relpath. */
  const char *parent_relpath =
    (*local_relpath == '\0') ? NULL
    : svn_relpath_dirname(local_relpath, scratch_pool);

  if (pibb->repos_id == INVALID_REPOS_ID)
    SVN_ERR(create_repos_id(&repos_id, pibb->repos_root_url, pibb->repos_uuid,
                            wcroot->sdb, scratch_pool));

  SVN_ERR_ASSERT(repos_id != INVALID_REPOS_ID);
  SVN_ERR_ASSERT(pibb->repos_relpath != NULL);

  if (pibb->keep_recorded_info)
    {
      svn_boolean_t have_row;
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_BASE_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        {
          /* Preserve size and modification time if caller asked us to. */
          recorded_size = get_recorded_size(stmt, 6);
          recorded_time = svn_sqlite__column_int64(stmt, 12);
        }
      SVN_ERR(svn_sqlite__reset(stmt));
    }

  present = (pibb->status == svn_wc__db_status_normal
             || pibb->status == svn_wc__db_status_incomplete);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isdsisr"
                            "tstr"               /* 8 - 11 */
                            "isnnnnns",          /* 12 - 19 */
                            wcroot->wc_id,       /* 1 */
                            local_relpath,       /* 2 */
                            0,              /* op_depth is 0 for base */
                            parent_relpath,      /* 4 */
                            repos_id,
                            pibb->repos_relpath,
                            pibb->revision,
                            presence_map, pibb->status, /* 8 */
                            (pibb->kind == svn_node_dir && present) /* 9 */
                              ? svn_token__to_word(depth_map, pibb->depth)
                              : NULL,
                            kind_map, pibb->kind, /* 10 */
                            pibb->changed_rev,    /* 11 */
                            pibb->changed_date,   /* 12 */
                            pibb->changed_author, /* 13 */
                            (pibb->kind == svn_node_symlink && present) ?
                                pibb->target : NULL)); /* 19 */
  if (pibb->kind == svn_node_file && present)
    {
      if (!pibb->checksum
          && pibb->status != svn_wc__db_status_not_present
          && pibb->status != svn_wc__db_status_excluded
          && pibb->status != svn_wc__db_status_server_excluded)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                                 _("The file '%s' has no checksum."),
                                 path_for_error_message(wcroot, local_relpath,
                                                        scratch_pool));

      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, pibb->checksum,
                                        scratch_pool));

      if (recorded_size != SVN_INVALID_FILESIZE)
        {
          SVN_ERR(svn_sqlite__bind_int64(stmt, 16, recorded_size));
          SVN_ERR(svn_sqlite__bind_int64(stmt, 17, recorded_time));
        }
    }

  /* Set properties.  Must be null if presence not normal or incomplete. */
  assert(pibb->status == svn_wc__db_status_normal
         || pibb->status == svn_wc__db_status_incomplete
         || pibb->props == NULL);
  if (present)
    {
      SVN_ERR(svn_sqlite__bind_properties(stmt, 15, pibb->props,
                                          scratch_pool));

      SVN_ERR(svn_sqlite__bind_iprops(stmt, 23, pibb->iprops,
                                      scratch_pool));
    }

  if (pibb->dav_cache)
    SVN_ERR(svn_sqlite__bind_properties(stmt, 18, pibb->dav_cache,
                                        scratch_pool));

  if (pibb->file_external)
    SVN_ERR(svn_sqlite__bind_int(stmt, 20, 1));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  if (pibb->update_actual_props)
    {
      /* Cast away const, to allow calling property helpers */
      apr_hash_t *base_props = (apr_hash_t *)pibb->props;
      apr_hash_t *new_actual_props = (apr_hash_t *)pibb->new_actual_props;

      if (base_props != NULL
          && new_actual_props != NULL
          && (apr_hash_count(base_props) == apr_hash_count(new_actual_props)))
        {
          apr_array_header_t *diffs;

          SVN_ERR(svn_prop_diffs(&diffs, new_actual_props, base_props,
                                 scratch_pool));

          if (diffs->nelts == 0)
            new_actual_props = NULL;
        }

      SVN_ERR(set_actual_props(wcroot, local_relpath, new_actual_props,
                               scratch_pool));
    }

  if (pibb->kind == svn_node_dir && pibb->children)
    SVN_ERR(insert_incomplete_children(wcroot->sdb, wcroot->wc_id,
                                       local_relpath,
                                       repos_id,
                                       pibb->repos_relpath,
                                       pibb->revision,
                                       pibb->children,
                                       0 /* BASE */,
                                       scratch_pool));

  /* When this is not the root node, check shadowing behavior */
  if (*local_relpath)
    {
      if (parent_relpath
          && ((pibb->status == svn_wc__db_status_normal)
              || (pibb->status == svn_wc__db_status_incomplete))
          && ! pibb->file_external)
        {
          SVN_ERR(db_extend_parent_delete(wcroot, local_relpath,
                                          pibb->kind, 0,
                                          scratch_pool));
        }
      else if (pibb->status == svn_wc__db_status_not_present
               || pibb->status == svn_wc__db_status_server_excluded
               || pibb->status == svn_wc__db_status_excluded)
        {
          SVN_ERR(db_retract_parent_delete(wcroot, local_relpath, 0,
                                           scratch_pool));
        }
    }

  if (pibb->delete_working)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  if (pibb->insert_base_deleted)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_DELETE_FROM_BASE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd",
                                wcroot->wc_id, local_relpath,
                                relpath_depth(local_relpath)));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(add_work_items(wcroot->sdb, pibb->work_items, scratch_pool));
  if (pibb->conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              pibb->conflict, scratch_pool));

  return SVN_NO_ERROR;
}


/* Initialize the baton with appropriate "blank" values. This allows the
   insertion function to leave certain columns null.  */
static void
blank_iwb(insert_working_baton_t *piwb)
{
  memset(piwb, 0, sizeof(*piwb));
  piwb->changed_rev = SVN_INVALID_REVNUM;
  piwb->depth = svn_depth_infinity;

  /* ORIGINAL_REPOS_ID and ORIGINAL_REVNUM could use some kind of "nil"
     value, but... meh. We'll avoid them if ORIGINAL_REPOS_RELPATH==NULL.  */
}


/* Insert a row in NODES for each (const char *) child name in CHILDREN,
   whose parent directory is LOCAL_RELPATH, at op_depth=OP_DEPTH.  Set each
   child's presence to 'incomplete', kind to 'unknown', repos_id to REPOS_ID,
   repos_path by appending the child name to REPOS_PATH, and revision to
   REVISION (which should match the parent's revision).

   If REPOS_ID is INVALID_REPOS_ID, set each child's repos_id to null. */
static svn_error_t *
insert_incomplete_children(svn_sqlite__db_t *sdb,
                           apr_int64_t wc_id,
                           const char *local_relpath,
                           apr_int64_t repos_id,
                           const char *repos_path,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           int op_depth,
                           apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *moved_to_relpaths = apr_hash_make(scratch_pool);

  SVN_ERR_ASSERT(repos_path != NULL || op_depth > 0);
  SVN_ERR_ASSERT((repos_id != INVALID_REPOS_ID)
                 == (repos_path != NULL));

  /* If we're inserting WORKING nodes, we might be replacing existing
   * nodes which were moved-away. We need to retain the moved-to relpath of
   * such nodes in order not to lose move information during replace. */
  if (op_depth > 0)
    {
      for (i = children->nelts; i--; )
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          svn_boolean_t have_row;

          svn_pool_clear(iterpool);

          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                            STMT_SELECT_WORKING_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id,
                                    svn_relpath_join(local_relpath, name,
                                                     iterpool)));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          if (have_row && !svn_sqlite__column_is_null(stmt, 14))
            svn_hash_sets(moved_to_relpaths, name,
                          svn_sqlite__column_text(stmt, 14, scratch_pool));

          SVN_ERR(svn_sqlite__reset(stmt));
        }
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));

  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_sqlite__bindf(stmt, "isdsnnrsnsnnnnnnnnnnsn",
                                wc_id,
                                svn_relpath_join(local_relpath, name,
                                                 iterpool),
                                op_depth,
                                local_relpath,
                                revision,
                                "incomplete", /* 8, presence */
                                "unknown",    /* 10, kind */
                                /* 21, moved_to */
                                svn_hash_gets(moved_to_relpaths, name)));
      if (repos_id != INVALID_REPOS_ID)
        {
          SVN_ERR(svn_sqlite__bind_int64(stmt, 5, repos_id));
          SVN_ERR(svn_sqlite__bind_text(stmt, 6,
                                        svn_relpath_join(repos_path, name,
                                                         iterpool)));
        }

      SVN_ERR(svn_sqlite__insert(NULL, stmt));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Insert the working row represented by (insert_working_baton_t *) BATON. */
static svn_error_t *
insert_working_node(const insert_working_baton_t *piwb,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    apr_pool_t *scratch_pool)
{
  const char *parent_relpath;
  const char *moved_to_relpath = NULL;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_boolean_t present;

  SVN_ERR_ASSERT(piwb->op_depth > 0);

  /* We cannot insert a WORKING_NODE row at the wcroot.  */
  SVN_ERR_ASSERT(*local_relpath != '\0');
  parent_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

  /* Preserve existing moved-to information for this relpath,
   * which might exist in case we're replacing an existing base-deleted
   * node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_SELECT_MOVED_TO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            piwb->op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    moved_to_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));

  present = (piwb->presence == svn_wc__db_status_normal
             || piwb->presence == svn_wc__db_status_incomplete);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isdsnnntstrisn"
                "nnnn" /* properties translated_size last_mod_time dav_cache */
                "sns", /* symlink_target, file_external, moved_to */
                wcroot->wc_id, local_relpath,
                piwb->op_depth,
                parent_relpath,
                presence_map, piwb->presence,
                (piwb->kind == svn_node_dir && present)
                            ? svn_token__to_word(depth_map, piwb->depth) : NULL,
                kind_map, piwb->kind,
                piwb->changed_rev,
                piwb->changed_date,
                piwb->changed_author,
                /* Note: incomplete nodes may have a NULL target.  */
                (piwb->kind == svn_node_symlink && present)
                            ? piwb->target : NULL,
                moved_to_relpath));

  if (piwb->moved_here)
    {
      SVN_ERR(svn_sqlite__bind_int(stmt, 8, TRUE));
    }

  if (piwb->kind == svn_node_file && present)
    {
      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, piwb->checksum,
                                        scratch_pool));
    }

  if (piwb->original_repos_relpath != NULL)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 5, piwb->original_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6, piwb->original_repos_relpath));
      SVN_ERR(svn_sqlite__bind_revnum(stmt, 7, piwb->original_revnum));
    }

  /* Set properties.  Must be null if presence not normal or incomplete. */
  assert(piwb->presence == svn_wc__db_status_normal
         || piwb->presence == svn_wc__db_status_incomplete
         || piwb->props == NULL);
  if (present && piwb->original_repos_relpath)
    SVN_ERR(svn_sqlite__bind_properties(stmt, 15, piwb->props, scratch_pool));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  /* Insert incomplete children, if specified.
     The children are part of the same op and so have the same op_depth.
     (The only time we'd want a different depth is during a recursive
     simple add, but we never insert children here during a simple add.) */
  if (piwb->kind == svn_node_dir && piwb->children)
    SVN_ERR(insert_incomplete_children(wcroot->sdb, wcroot->wc_id,
                                       local_relpath,
                                       INVALID_REPOS_ID /* inherit repos_id */,
                                       NULL /* inherit repos_path */,
                                       piwb->original_revnum,
                                       piwb->children,
                                       piwb->op_depth,
                                       scratch_pool));

  if (piwb->update_actual_props)
    {
      /* Cast away const, to allow calling property helpers */
      apr_hash_t *base_props = (apr_hash_t *)piwb->props;
      apr_hash_t *new_actual_props = (apr_hash_t *)piwb->new_actual_props;

      if (base_props != NULL
          && new_actual_props != NULL
          && (apr_hash_count(base_props) == apr_hash_count(new_actual_props)))
        {
          apr_array_header_t *diffs;

          SVN_ERR(svn_prop_diffs(&diffs, new_actual_props, base_props,
                                 scratch_pool));

          if (diffs->nelts == 0)
            new_actual_props = NULL;
        }

      SVN_ERR(set_actual_props(wcroot, local_relpath, new_actual_props,
                               scratch_pool));
    }

  if (piwb->kind == svn_node_dir)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_ACTUAL_CLEAR_CHANGELIST));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_EMPTY));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (piwb->not_present_op_depth > 0
      && piwb->not_present_op_depth < piwb->op_depth)
    {
      /* And also insert a not-present node to tell the commit processing that
         a child of the parent node was not copied. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "isdsisrtnt",
                                wcroot->wc_id, local_relpath,
                                piwb->not_present_op_depth, parent_relpath,
                                piwb->original_repos_id,
                                piwb->original_repos_relpath,
                                piwb->original_revnum,
                                presence_map, svn_wc__db_status_not_present,
                                /* NULL */
                                kind_map, piwb->kind));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(add_work_items(wcroot->sdb, piwb->work_items, scratch_pool));
  if (piwb->conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              piwb->conflict, scratch_pool));

  return SVN_NO_ERROR;
}


/* Return in *CHILDREN all of the children of the directory LOCAL_RELPATH,
   of any status, in all op-depths in the NODES table. */
static svn_error_t *
gather_children(const apr_array_header_t **children,
                svn_wc__db_wcroot_t *wcroot,
                const char *parent_relpath,
                int stmt_idx,
                int op_depth,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *result;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  result = apr_array_make(result_pool, 16, sizeof(const char*));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, parent_relpath));
  if (op_depth >= 0)
    SVN_ERR(svn_sqlite__bind_int(stmt, 3, op_depth));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *name = svn_relpath_basename(child_relpath, result_pool);

      APR_ARRAY_PUSH(result, const char *) = name;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));
  *children = result;
  return SVN_NO_ERROR;
}

/* Return TRUE if CHILD_ABSPATH is an immediate child of PARENT_ABSPATH.
 * Else, return FALSE. */
static svn_boolean_t
is_immediate_child_path(const char *parent_abspath, const char *child_abspath)
{
  const char *local_relpath = svn_dirent_skip_ancestor(parent_abspath,
                                                       child_abspath);

  /* To be an immediate child local_relpath should have one (not empty)
     component */
  return local_relpath && *local_relpath && !strchr(local_relpath, '/');
}


/* Remove the access baton for LOCAL_ABSPATH from ACCESS_CACHE. */
static void
remove_from_access_cache(apr_hash_t *access_cache,
                         const char *local_abspath)
{
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_hash_gets(access_cache, local_abspath);
  if (adm_access)
    svn_wc__adm_access_set_entries(adm_access, NULL);
}


/* Flush the access baton for LOCAL_ABSPATH, and any of its children up to
 * the specified DEPTH, from the access baton cache in WCROOT.
 * Also flush the access baton for the parent of LOCAL_ABSPATH.I
 *
 * This function must be called when the access baton cache goes stale,
 * i.e. data about LOCAL_ABSPATH will need to be read again from disk.
 *
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
flush_entries(svn_wc__db_wcroot_t *wcroot,
              const char *local_abspath,
              svn_depth_t depth,
              apr_pool_t *scratch_pool)
{
  const char *parent_abspath;

  if (apr_hash_count(wcroot->access_cache) == 0)
    return SVN_NO_ERROR;

  remove_from_access_cache(wcroot->access_cache, local_abspath);

  if (depth > svn_depth_empty)
    {
      apr_hash_index_t *hi;

      /* Flush access batons of children within the specified depth. */
      for (hi = apr_hash_first(scratch_pool, wcroot->access_cache);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *item_abspath = apr_hash_this_key(hi);

          if ((depth == svn_depth_files || depth == svn_depth_immediates) &&
              is_immediate_child_path(local_abspath, item_abspath))
            {
              remove_from_access_cache(wcroot->access_cache, item_abspath);
            }
          else if (depth == svn_depth_infinity &&
                   svn_dirent_is_ancestor(local_abspath, item_abspath))
            {
              remove_from_access_cache(wcroot->access_cache, item_abspath);
            }
        }
    }

  /* We're going to be overly aggressive here and just flush the parent
     without doing much checking.  This may hurt performance for
     legacy API consumers, but that's not our problem. :) */
  parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  remove_from_access_cache(wcroot->access_cache, parent_abspath);

  return SVN_NO_ERROR;
}


/* Add a single WORK_ITEM into the given SDB's WORK_QUEUE table. This does
   not perform its work within a transaction, assuming the caller will
   manage that.  */
static svn_error_t *
add_single_work_item(svn_sqlite__db_t *sdb,
                     const svn_skel_t *work_item,
                     apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *serialized;
  svn_sqlite__stmt_t *stmt;

  serialized = svn_skel__unparse(work_item, scratch_pool);
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_WORK_ITEM));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, serialized->data, serialized->len));
  return svn_error_trace(svn_sqlite__insert(NULL, stmt));
}


/* Add work item(s) to the given SDB. Also see add_single_work_item(). This
   SKEL is usually passed to the various wc_db operation functions. It may
   be NULL, indicating no additional work items are needed, it may be a
   single work item, or it may be a list of work items.  */
static svn_error_t *
add_work_items(svn_sqlite__db_t *sdb,
               const svn_skel_t *skel,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;

  /* Maybe there are no work items to insert.  */
  if (skel == NULL)
    return SVN_NO_ERROR;

  /* Should have a list.  */
  SVN_ERR_ASSERT(!skel->is_atom);

  /* Is the list a single work item? Or a list of work items?  */
  if (SVN_WC__SINGLE_WORK_ITEM(skel))
    return svn_error_trace(add_single_work_item(sdb, skel, scratch_pool));

  /* SKEL is a list-of-lists, aka list of work items.  */

  iterpool = svn_pool_create(scratch_pool);
  for (skel = skel->children; skel; skel = skel->next)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(add_single_work_item(sdb, skel, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Determine whether the node exists for a given WCROOT and LOCAL_RELPATH.  */
static svn_error_t *
does_node_exist(svn_boolean_t *exists,
                const svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_DOES_NODE_EXIST));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(exists, stmt));

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_install_schema_statistics(svn_sqlite__db_t *sdb,
                                     apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_sqlite__exec_statements(sdb, STMT_INSTALL_SCHEMA_STATISTICS));

  return SVN_NO_ERROR;
}

/* Helper for create_db(). Initializes our wc.db schema.
 */
static svn_error_t *
init_db(/* output values */
        apr_int64_t *repos_id,
        apr_int64_t *wc_id,
        /* input values */
        svn_sqlite__db_t *db,
        const char *repos_root_url,
        const char *repos_uuid,
        const char *root_node_repos_relpath,
        svn_revnum_t root_node_revision,
        svn_depth_t root_node_depth,
        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* Create the database's schema.  */
  SVN_ERR(svn_sqlite__exec_statements(db, STMT_CREATE_SCHEMA));

  SVN_ERR(svn_wc__db_install_schema_statistics(db, scratch_pool));

  /* Insert the repository. */
  SVN_ERR(create_repos_id(repos_id, repos_root_url, repos_uuid,
                          db, scratch_pool));

  /* Insert the wcroot. */
  /* ### Right now, this just assumes wc metadata is being stored locally. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_INSERT_WCROOT));
  SVN_ERR(svn_sqlite__insert(wc_id, stmt));

  if (root_node_repos_relpath)
    {
      svn_wc__db_status_t status = svn_wc__db_status_normal;

      if (root_node_revision > 0)
        status = svn_wc__db_status_incomplete; /* Will be filled by update */

      SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_INSERT_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isdsisrtst",
                                *wc_id,              /* 1 */
                                "",                  /* 2 */
                                0,                   /* op_depth is 0 for base */
                                SVN_VA_NULL,         /* 4 */
                                *repos_id,
                                root_node_repos_relpath,
                                root_node_revision,
                                presence_map, status, /* 8 */
                                svn_token__to_word(depth_map,
                                                   root_node_depth),
                                kind_map, svn_node_dir /* 10 */));

      SVN_ERR(svn_sqlite__insert(NULL, stmt));
    }

  return SVN_NO_ERROR;
}

/* Create an sqlite database at DIR_ABSPATH/SDB_FNAME and insert
   records for REPOS_ID (using REPOS_ROOT_URL and REPOS_UUID) into
   REPOSITORY and for WC_ID into WCROOT.  Return the DB connection
   in *SDB.

   If ROOT_NODE_REPOS_RELPATH is not NULL, insert a BASE node at
   the working copy root with repository relpath ROOT_NODE_REPOS_RELPATH,
   revision ROOT_NODE_REVISION and depth ROOT_NODE_DEPTH.
   */
static svn_error_t *
create_db(svn_sqlite__db_t **sdb,
          apr_int64_t *repos_id,
          apr_int64_t *wc_id,
          const char *dir_abspath,
          const char *repos_root_url,
          const char *repos_uuid,
          const char *sdb_fname,
          const char *root_node_repos_relpath,
          svn_revnum_t root_node_revision,
          svn_depth_t root_node_depth,
          svn_boolean_t exclusive,
          apr_int32_t timeout,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_util_open_db(sdb, dir_abspath, sdb_fname,
                                  svn_sqlite__mode_rwcreate, exclusive,
                                  timeout,
                                  NULL /* my_statements */,
                                  result_pool, scratch_pool));

  SVN_SQLITE__WITH_LOCK(init_db(repos_id, wc_id,
                                *sdb, repos_root_url, repos_uuid,
                                root_node_repos_relpath, root_node_revision,
                                root_node_depth, scratch_pool),
                        *sdb);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_init(svn_wc__db_t *db,
                const char *local_abspath,
                const char *repos_relpath,
                const char *repos_root_url,
                const char *repos_uuid,
                svn_revnum_t initial_rev,
                svn_depth_t depth,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  apr_int64_t repos_id;
  apr_int64_t wc_id;
  svn_wc__db_wcroot_t *wcroot;
  svn_boolean_t sqlite_exclusive = FALSE;
  apr_int32_t sqlite_timeout = 0; /* default timeout */
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(depth == svn_depth_empty
                 || depth == svn_depth_files
                 || depth == svn_depth_immediates
                 || depth == svn_depth_infinity);

  /* ### REPOS_ROOT_URL and REPOS_UUID may be NULL. ... more doc: tbd  */

  SVN_ERR(svn_config_get_bool(db->config, &sqlite_exclusive,
                              SVN_CONFIG_SECTION_WORKING_COPY,
                              SVN_CONFIG_OPTION_SQLITE_EXCLUSIVE,
                              FALSE));

  /* Create the SDB and insert the basic rows.  */
  SVN_ERR(create_db(&sdb, &repos_id, &wc_id, local_abspath, repos_root_url,
                    repos_uuid, SDB_FILE,
                    repos_relpath, initial_rev, depth, sqlite_exclusive,
                    sqlite_timeout,
                    db->state_pool, scratch_pool));

  /* Create the WCROOT for this directory.  */
  SVN_ERR(svn_wc__db_pdh_create_wcroot(&wcroot,
                        apr_pstrdup(db->state_pool, local_abspath),
                        sdb, wc_id, FORMAT_FROM_SDB,
                        FALSE /* auto-upgrade */,
                        db->state_pool, scratch_pool));

  /* Any previously cached children may now have a new WCROOT, most likely that
     of the new WCROOT, but there might be descendant directories that are their
     own working copy, in which case setting WCROOT to our new WCROOT might
     actually break things for those.

     Clearing is the safest thing we can do in this case, as a test would lead
     to unnecessary probing, while the standard code probes later anyway. So we
     only lose a bit of memory

     ### Perhaps we could check wcroot->abspath to detect which case we have
         where, but currently it is already very hard to trigger this from
         the short living 'svn' client. (GUI clients like TortoiseSVN are far
         more likely to get in these cases)
     */
  for (hi = apr_hash_first(scratch_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *abspath = apr_hash_this_key(hi);
      if (svn_dirent_is_ancestor(wcroot->abspath, abspath))
        svn_hash_sets(db->dir_data, abspath, NULL);
    }

  /* The WCROOT is complete. Stash it into DB.  */
  svn_hash_sets(db->dir_data, wcroot->abspath, wcroot);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_to_relpath(const char **local_relpath,
                      svn_wc__db_t *db,
                      const char *wri_abspath,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &relpath, db,
                              wri_abspath, result_pool, scratch_pool));

  /* This function is indirectly called from the upgrade code, so we
     can't verify the wcroot here. Just check that it is not NULL */
  CHECK_MINIMAL_WCROOT(wcroot, wri_abspath, scratch_pool);

  if (svn_dirent_is_ancestor(wcroot->abspath, local_abspath))
    {
      *local_relpath = apr_pstrdup(result_pool,
                                   svn_dirent_skip_ancestor(wcroot->abspath,
                                                            local_abspath));
    }
  else
    /* Probably moving from $TMP. Should we allow this? */
    *local_relpath = apr_pstrdup(result_pool, local_abspath);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_from_relpath(const char **local_abspath,
                        svn_wc__db_t *db,
                        const char *wri_abspath,
                        const char *local_relpath,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *unused_relpath;
#if 0
  SVN_ERR_ASSERT(svn_relpath_is_canonical(local_relpath));
#endif

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &unused_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));

  /* This function is indirectly called from the upgrade code, so we
     can't verify the wcroot here. Just check that it is not NULL */
  CHECK_MINIMAL_WCROOT(wcroot, wri_abspath, scratch_pool);


  *local_abspath = svn_dirent_join(wcroot->abspath,
                                   local_relpath,
                                   result_pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_get_wcroot(const char **wcroot_abspath,
                      svn_wc__db_t *db,
                      const char *wri_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *unused_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &unused_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));

  /* Can't use VERIFY_USABLE_WCROOT, as this should be usable to detect
     where call upgrade */
  CHECK_MINIMAL_WCROOT(wcroot, wri_abspath, scratch_pool);

  *wcroot_abspath = apr_pstrdup(result_pool, wcroot->abspath);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_directory(svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *wri_abspath,
                              const char *repos_relpath,
                              const char *repos_root_url,
                              const char *repos_uuid,
                              svn_revnum_t revision,
                              const apr_hash_t *props,
                              svn_revnum_t changed_rev,
                              apr_time_t changed_date,
                              const char *changed_author,
                              const apr_array_header_t *children,
                              svn_depth_t depth,
                              apr_hash_t *dav_cache,
                              svn_boolean_t update_actual_props,
                              apr_hash_t *new_actual_props,
                              apr_array_header_t *new_iprops,
                              const svn_skel_t *conflict,
                              const svn_skel_t *work_items,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
#if 0
  SVN_ERR_ASSERT(children != NULL);
#endif

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  blank_ibb(&ibb);

  /* Calculate repos_id in insert_base_node() to avoid extra transaction */
  ibb.repos_root_url = repos_root_url;
  ibb.repos_uuid = repos_uuid;

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_node_dir;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.iprops = new_iprops;
  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.children = children;
  ibb.depth = depth;

  ibb.dav_cache = dav_cache;
  ibb.conflict = conflict;
  ibb.work_items = work_items;

  if (update_actual_props)
    {
      ibb.update_actual_props = TRUE;
      ibb.new_actual_props = new_actual_props;
    }

  /* Insert the directory and all its children transactionally.

     Note: old children can stick around, even if they are no longer present
     in this directory's revision.  */
  SVN_WC__DB_WITH_TXN(
            insert_base_node(&ibb, wcroot, local_relpath, scratch_pool),
            wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, depth, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_base_add_incomplete_directory(svn_wc__db_t *db,
                                         const char *local_abspath,
                                         const char *repos_relpath,
                                         const char *repos_root_url,
                                         const char *repos_uuid,
                                         svn_revnum_t revision,
                                         svn_depth_t depth,
                                         svn_boolean_t insert_base_deleted,
                                         svn_boolean_t delete_working,
                                         svn_skel_t *conflict,
                                         svn_skel_t *work_items,
                                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  struct insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(repos_relpath && repos_root_url && repos_uuid);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  blank_ibb(&ibb);

  /* Calculate repos_id in insert_base_node() to avoid extra transaction */
  ibb.repos_root_url = repos_root_url;
  ibb.repos_uuid = repos_uuid;

  ibb.status = svn_wc__db_status_incomplete;
  ibb.kind = svn_node_dir;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;
  ibb.depth = depth;
  ibb.insert_base_deleted = insert_base_deleted;
  ibb.delete_working = delete_working;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_base_node(&ibb, wcroot, local_relpath, scratch_pool),
            wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_file(svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *wri_abspath,
                         const char *repos_relpath,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         svn_revnum_t revision,
                         const apr_hash_t *props,
                         svn_revnum_t changed_rev,
                         apr_time_t changed_date,
                         const char *changed_author,
                         const svn_checksum_t *checksum,
                         apr_hash_t *dav_cache,
                         svn_boolean_t delete_working,
                         svn_boolean_t update_actual_props,
                         apr_hash_t *new_actual_props,
                         apr_array_header_t *new_iprops,
                         svn_boolean_t keep_recorded_info,
                         svn_boolean_t insert_base_deleted,
                         const svn_skel_t *conflict,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(checksum != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  blank_ibb(&ibb);

  /* Calculate repos_id in insert_base_node() to avoid extra transaction */
  ibb.repos_root_url = repos_root_url;
  ibb.repos_uuid = repos_uuid;

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_node_file;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.checksum = checksum;

  ibb.dav_cache = dav_cache;
  ibb.iprops = new_iprops;

  if (update_actual_props)
    {
      ibb.update_actual_props = TRUE;
      ibb.new_actual_props = new_actual_props;
    }

  ibb.keep_recorded_info = keep_recorded_info;
  ibb.insert_base_deleted = insert_base_deleted;
  ibb.delete_working = delete_working;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_base_node(&ibb, wcroot, local_relpath, scratch_pool),
            wcroot);

  /* If this used to be a directory we should remove children so pass
   * depth infinity. */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_symlink(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *wri_abspath,
                            const char *repos_relpath,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            svn_revnum_t revision,
                            const apr_hash_t *props,
                            svn_revnum_t changed_rev,
                            apr_time_t changed_date,
                            const char *changed_author,
                            const char *target,
                            apr_hash_t *dav_cache,
                            svn_boolean_t delete_working,
                            svn_boolean_t update_actual_props,
                            apr_hash_t *new_actual_props,
                            apr_array_header_t *new_iprops,
                            svn_boolean_t keep_recorded_info,
                            svn_boolean_t insert_base_deleted,
                            const svn_skel_t *conflict,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);
  blank_ibb(&ibb);

  /* Calculate repos_id in insert_base_node() to avoid extra transaction */
  ibb.repos_root_url = repos_root_url;
  ibb.repos_uuid = repos_uuid;

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_node_symlink;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.target = target;

  ibb.dav_cache = dav_cache;
  ibb.iprops = new_iprops;

  if (update_actual_props)
    {
      ibb.update_actual_props = TRUE;
      ibb.new_actual_props = new_actual_props;
    }

  ibb.keep_recorded_info = keep_recorded_info;
  ibb.insert_base_deleted = insert_base_deleted;
  ibb.delete_working = delete_working;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_base_node(&ibb, wcroot, local_relpath, scratch_pool),
            wcroot);

  /* If this used to be a directory we should remove children so pass
   * depth infinity. */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
add_excluded_or_not_present_node(svn_wc__db_t *db,
                                 const char *local_abspath,
                                 const char *repos_relpath,
                                 const char *repos_root_url,
                                 const char *repos_uuid,
                                 svn_revnum_t revision,
                                 svn_node_kind_t kind,
                                 svn_wc__db_status_t status,
                                 const svn_skel_t *conflict,
                                 const svn_skel_t *work_items,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_base_baton_t ibb;
  const char *dir_abspath, *name;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(status == svn_wc__db_status_server_excluded
                 || status == svn_wc__db_status_excluded
                 || status == svn_wc__db_status_not_present);

  /* These absent presence nodes are only useful below a parent node that is
     present. To avoid problems with working copies obstructing the child
     we calculate the wcroot and local_relpath of the parent and then add
     our own relpath. */

  svn_dirent_split(&dir_abspath, &name, local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  local_relpath = svn_relpath_join(local_relpath, name, scratch_pool);

  blank_ibb(&ibb);

  /* Calculate repos_id in insert_base_node() to avoid extra transaction */
  ibb.repos_root_url = repos_root_url;
  ibb.repos_uuid = repos_uuid;

  ibb.status = status;
  ibb.kind = kind;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  /* Depending upon KIND, any of these might get used. */
  ibb.children = NULL;
  ibb.depth = svn_depth_unknown;
  ibb.checksum = NULL;
  ibb.target = NULL;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_base_node(&ibb, wcroot, local_relpath, scratch_pool),
            wcroot);

  /* If this used to be a directory we should remove children so pass
   * depth infinity. */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_excluded_node(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const char *repos_relpath,
                                  const char *repos_root_url,
                                  const char *repos_uuid,
                                  svn_revnum_t revision,
                                  svn_node_kind_t kind,
                                  svn_wc__db_status_t status,
                                  const svn_skel_t *conflict,
                                  const svn_skel_t *work_items,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(status == svn_wc__db_status_server_excluded
                 || status == svn_wc__db_status_excluded);

  return add_excluded_or_not_present_node(
    db, local_abspath, repos_relpath, repos_root_url, repos_uuid, revision,
    kind, status, conflict, work_items, scratch_pool);
}


svn_error_t *
svn_wc__db_base_add_not_present_node(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const char *repos_relpath,
                                     const char *repos_root_url,
                                     const char *repos_uuid,
                                     svn_revnum_t revision,
                                     svn_node_kind_t kind,
                                     const svn_skel_t *conflict,
                                     const svn_skel_t *work_items,
                                     apr_pool_t *scratch_pool)
{
  return add_excluded_or_not_present_node(
    db, local_abspath, repos_relpath, repos_root_url, repos_uuid, revision,
    kind, svn_wc__db_status_not_present, conflict, work_items, scratch_pool);
}

/* Recursively clear moved-here information at the copy-half of the move
 * which moved a node to MOVED_TO_RELPATH. This transforms this side of the
 * move into a simple copy.
 */
static svn_error_t *
clear_moved_here(svn_wc__db_wcroot_t *wcroot,
                 const char *moved_to_relpath,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_MOVED_HERE_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, moved_to_relpath,
                            relpath_depth(moved_to_relpath)));

  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 0)
     return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, moved_to_relpath,
                                                     scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_break_move_internal(svn_wc__db_wcroot_t *wcroot,
                                  const char *src_relpath,
                                  int delete_op_depth,
                                  const char *dst_relpath,
                                  const svn_skel_t *work_items,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_MOVED_TO_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, src_relpath,
                            delete_op_depth));
  SVN_ERR(svn_sqlite__update(&affected, stmt));

  if (affected != 1)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Path '%s' is not moved"),
                             path_for_error_message(wcroot, src_relpath,
                                                    scratch_pool));

  SVN_ERR(clear_moved_here(wcroot, dst_relpath, scratch_pool));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));
  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_base_remove().
 */
static svn_error_t *
db_base_remove(svn_wc__db_wcroot_t *wcroot,
               const char *local_relpath,
               svn_wc__db_t *db, /* For checking conflicts */
               svn_boolean_t keep_as_working,
               svn_boolean_t mark_not_present,
               svn_boolean_t mark_excluded,
               svn_revnum_t marker_revision,
               svn_skel_t *conflict,
               svn_skel_t *work_items,
               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_wc__db_status_t status;
  svn_revnum_t revision;
  apr_int64_t repos_id;
  const char *repos_relpath;
  svn_node_kind_t kind;
  svn_boolean_t keep_working;
  int op_depth;
  svn_node_kind_t wrk_kind;
  svn_boolean_t no_delete_wc = FALSE;
  svn_boolean_t file_external;

  SVN_ERR(svn_wc__db_base_get_info_internal(&status, &kind, &revision,
                                            &repos_relpath, &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL,
                                            &file_external,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  /* Check if there is already a working node */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_trace(svn_sqlite__reset(stmt)); /* No BASE */

  op_depth = svn_sqlite__column_int(stmt, 0);
  wrk_kind = svn_sqlite__column_token(stmt, 4, kind_map);

  if (op_depth > 0
      && op_depth == relpath_depth(local_relpath))
    {
      svn_wc__db_status_t presence;
      presence = svn_sqlite__column_token(stmt, 3, presence_map);

      if (presence == svn_wc__db_status_base_deleted)
        {
          keep_working = FALSE;
          no_delete_wc = TRUE;
        }
      else
        {
          keep_working = TRUE;
        }
    }
  else
    keep_working = FALSE;
  SVN_ERR(svn_sqlite__reset(stmt));

  if (keep_as_working && op_depth == 0)
    {
      if (status == svn_wc__db_status_normal
          || status == svn_wc__db_status_incomplete)
        {
          SVN_ERR(svn_wc__db_op_make_copy_internal(wcroot, local_relpath, TRUE,
                                                   NULL, NULL,
                                                   scratch_pool));
        }
      keep_working = TRUE;
    }

  /* Step 1: Create workqueue operations to remove files and dirs in the
     local-wc */
  if (!keep_working && !no_delete_wc)
    {
      svn_skel_t *work_item;
      const char *local_abspath;

      local_abspath = svn_dirent_join(wcroot->abspath, local_relpath,
                                      scratch_pool);
      if (wrk_kind == svn_node_dir)
        {
          apr_pool_t *iterpool;
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_SELECT_WORKING_PRESENT));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

          iterpool = svn_pool_create(scratch_pool);

          SVN_ERR(svn_sqlite__step(&have_row, stmt));

          while (have_row)
            {
              const char *node_relpath = svn_sqlite__column_text(stmt, 0, NULL);
              svn_node_kind_t node_kind = svn_sqlite__column_token(stmt, 1,
                                                              kind_map);
              const char *node_abspath;
              svn_error_t *err;

              svn_pool_clear(iterpool);

              node_abspath = svn_dirent_join(wcroot->abspath, node_relpath,
                                             iterpool);

              if (node_kind == svn_node_dir)
                err = svn_wc__wq_build_dir_remove(&work_item,
                                                  db, wcroot->abspath,
                                                  node_abspath, FALSE,
                                                  iterpool, iterpool);
              else
                err = svn_wc__wq_build_file_remove(&work_item,
                                                   db,
                                                   wcroot->abspath,
                                                   node_abspath,
                                                   iterpool, iterpool);

              if (!err)
                err = add_work_items(wcroot->sdb, work_item, iterpool);
              if (err)
                return svn_error_compose_create(err, svn_sqlite__reset(stmt));

              SVN_ERR(svn_sqlite__step(&have_row, stmt));
           }

          SVN_ERR(svn_sqlite__reset(stmt));

          SVN_ERR(svn_wc__wq_build_dir_remove(&work_item,
                                              db, wcroot->abspath,
                                              local_abspath, FALSE,
                                              scratch_pool, iterpool));
          svn_pool_destroy(iterpool);
        }
      else
        SVN_ERR(svn_wc__wq_build_file_remove(&work_item,
                                             db, wcroot->abspath,
                                             local_abspath,
                                             scratch_pool, scratch_pool));

      SVN_ERR(add_work_items(wcroot->sdb, work_item, scratch_pool));
    }

  /* Step 2: Delete ACTUAL nodes */
  if (! keep_working)
    {
      /* There won't be a record in NODE left for this node, so we want
         to remove *all* ACTUAL nodes, including ACTUAL ONLY. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_NODE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else if (! keep_as_working)
    {
      /* Delete only the ACTUAL nodes that apply to a delete of a BASE node */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                       STMT_DELETE_ACTUAL_FOR_BASE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  /* Else: Everything has been turned into a copy, so we want to keep all
           ACTUAL_NODE records */

  /* Step 3: Delete WORKING nodes */
  if (!keep_working)
    {
      apr_pool_t *iterpool;

      /* When deleting everything in working we should break moves from
         here and to here.
       */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_MOVED_OUTSIDE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                             local_relpath,
                                             relpath_depth(local_relpath)));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      iterpool = svn_pool_create(scratch_pool);
      while (have_row)
        {
          const char *moved_to_relpath;
          svn_error_t *err;

          svn_pool_clear(iterpool);
          moved_to_relpath = svn_sqlite__column_text(stmt, 1, iterpool);
          err = clear_moved_here(wcroot, moved_to_relpath, iterpool);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      svn_pool_destroy(iterpool);
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else
    {
      /* We are keeping things that are in WORKING, but we should still
         break moves of things in BASE. (Mixed revisions make it
         impossible to guarantee that we can keep everything moved) */

      apr_pool_t *iterpool;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_MOVED_DESCENDANTS_SRC));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath, 0));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      iterpool = svn_pool_create(scratch_pool);
      while (have_row)
        {
          int delete_op_depth = svn_sqlite__column_int(stmt, 0);
          const char *src_relpath;
          const char *dst_relpath;
          svn_error_t *err;

          svn_pool_clear(iterpool);

          src_relpath = svn_sqlite__column_text(stmt, 1, iterpool);
          dst_relpath = svn_sqlite__column_text(stmt, 4, iterpool);

          err = svn_wc__db_op_break_move_internal(wcroot, src_relpath,
                                                  delete_op_depth,
                                                  dst_relpath,
                                                  NULL,
                                                  iterpool);

          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      svn_pool_destroy(iterpool);
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  if (keep_working)
    {
      SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb,
                    STMT_DELETE_WORKING_BASE_DELETE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath, 0));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_WORKING_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Step 4: Delete the BASE node descendants */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_BASE_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(db_retract_parent_delete(wcroot, local_relpath, 0, scratch_pool));

  if (mark_not_present || mark_excluded)
    {
      struct insert_base_baton_t ibb;
      svn_boolean_t no_marker = FALSE;

      if (file_external)
        {
          const char *parent_local_relpath;
          const char *name;
          svn_error_t *err;

          /* For file externals we only want to place a not present marker
             if there is a BASE parent */
          
          svn_relpath_split(&parent_local_relpath, &name, local_relpath,
                            scratch_pool);

          err = svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                                  &repos_relpath, &repos_id,
                                                  NULL, NULL, NULL, NULL, NULL,
                                                  NULL, NULL, NULL, NULL, NULL,
                                                  wcroot, parent_local_relpath,
                                                  scratch_pool, scratch_pool);

          if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
            return svn_error_trace(err);
          else if (err)
            {
              svn_error_clear(err);
              no_marker = TRUE;
            }
          else
            {
              /* Replace the repos_relpath with something more expected than
                 the unrelated old file external repository relpath, which
                 one day may come from a different repository */
              repos_relpath = svn_relpath_join(repos_relpath, name, scratch_pool);
            }
        }

      if (!no_marker)
        {
          blank_ibb(&ibb);

          ibb.repos_id = repos_id;
          ibb.status = mark_excluded ? svn_wc__db_status_excluded
                                     : svn_wc__db_status_not_present;
          ibb.kind = kind;
          ibb.repos_relpath = repos_relpath;
          ibb.revision = SVN_IS_VALID_REVNUM(marker_revision)
                            ? marker_revision
                            : revision;

          /* Depending upon KIND, any of these might get used. */
          ibb.children = NULL;
          ibb.depth = svn_depth_unknown;
          ibb.checksum = NULL;
          ibb.target = NULL;

          SVN_ERR(insert_base_node(&ibb, wcroot, local_relpath, scratch_pool));
        }
    }

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));
  if (conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              conflict, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_boolean_t keep_as_working,
                       svn_boolean_t mark_not_present,
                       svn_boolean_t mark_excluded,
                       svn_revnum_t marker_revision,
                       svn_skel_t *conflict,
                       svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(db_base_remove(wcroot, local_relpath,
                                     db, keep_as_working,
                                     mark_not_present, mark_excluded,
                                     marker_revision,
                                     conflict, work_items, scratch_pool),
                      wcroot);

  /* If this used to be a directory we should remove children so pass
   * depth infinity. */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_info_internal(svn_wc__db_status_t *status,
                                  svn_node_kind_t *kind,
                                  svn_revnum_t *revision,
                                  const char **repos_relpath,
                                  apr_int64_t *repos_id,
                                  svn_revnum_t *changed_rev,
                                  apr_time_t *changed_date,
                                  const char **changed_author,
                                  svn_depth_t *depth,
                                  const svn_checksum_t **checksum,
                                  const char **target,
                                  svn_wc__db_lock_t **lock,
                                  svn_boolean_t *had_props,
                                  apr_hash_t **props,
                                  svn_boolean_t *update_root,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *local_relpath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    lock ? STMT_SELECT_BASE_NODE_WITH_LOCK
                                         : STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_status_t node_status = svn_sqlite__column_token(stmt, 2,
                                                                 presence_map);
      svn_node_kind_t node_kind = svn_sqlite__column_token(stmt, 3, kind_map);

      if (kind)
        {
          *kind = node_kind;
        }
      if (status)
        {
          *status = node_status;
        }
      repos_location_from_columns(repos_id, revision, repos_relpath,
                                  stmt, 0, 4, 1, result_pool);
      SVN_ERR_ASSERT(!repos_id || *repos_id != INVALID_REPOS_ID);
      SVN_ERR_ASSERT(!repos_relpath || *repos_relpath);
      if (lock)
        {
          *lock = lock_from_columns(stmt, 15, 16, 17, 18, result_pool);
        }
      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt, 7);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt, 8);
        }
      if (changed_author)
        {
          /* Result may be NULL. */
          *changed_author = svn_sqlite__column_text(stmt, 9, result_pool);
        }
      if (depth)
        {
          if (node_kind != svn_node_dir)
            {
              *depth = svn_depth_unknown;
            }
          else
            {
              *depth = svn_sqlite__column_token_null(stmt, 10, depth_map,
                                                     svn_depth_unknown);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_node_file)
            {
              *checksum = NULL;
            }
          else
            {
              err = svn_sqlite__column_checksum(checksum, stmt, 5,
                                                result_pool);
              if (err != NULL)
                err = svn_error_createf(
                        err->apr_err, err,
                        _("The node '%s' has a corrupt checksum value."),
                        path_for_error_message(wcroot, local_relpath,
                                               scratch_pool));
            }
        }
      if (target)
        {
          if (node_kind != svn_node_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt, 11, result_pool);
        }
      if (had_props)
        {
          *had_props = SQLITE_PROPERTIES_AVAILABLE(stmt, 13);
        }
      if (props)
        {
          if (node_status == svn_wc__db_status_normal
              || node_status == svn_wc__db_status_incomplete)
            {
              SVN_ERR(svn_sqlite__column_properties(props, stmt, 13,
                                                    result_pool, scratch_pool));
              if (*props == NULL)
                *props = apr_hash_make(result_pool);
            }
          else
            {
              assert(svn_sqlite__column_is_null(stmt, 13));
              *props = NULL;
            }
        }
      if (update_root)
        {
          /* It's an update root iff it's a file external. */
          *update_root = svn_sqlite__column_boolean(stmt, 14);
        }
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool));
    }

  /* Note: given the composition, no need to wrap for tracing.  */
  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_base_get_info(svn_wc__db_status_t *status,
                         svn_node_kind_t *kind,
                         svn_revnum_t *revision,
                         const char **repos_relpath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         svn_revnum_t *changed_rev,
                         apr_time_t *changed_date,
                         const char **changed_author,
                         svn_depth_t *depth,
                         const svn_checksum_t **checksum,
                         const char **target,
                         svn_wc__db_lock_t **lock,
                         svn_boolean_t *had_props,
                         apr_hash_t **props,
                         svn_boolean_t *update_root,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN4(
          svn_wc__db_base_get_info_internal(status, kind, revision,
                                            repos_relpath, &repos_id,
                                            changed_rev, changed_date,
                                            changed_author, depth,
                                            checksum, target, lock,
                                            had_props, props, update_root,
                                            wcroot, local_relpath,
                                            result_pool, scratch_pool),
          svn_wc__db_fetch_repos_info(repos_root_url, repos_uuid,
                                      wcroot, repos_id, result_pool),
          SVN_NO_ERROR,
          SVN_NO_ERROR,
          wcroot);
  SVN_ERR_ASSERT(repos_id != INVALID_REPOS_ID);

  return SVN_NO_ERROR;
}

/* The implementation of svn_wc__db_base_get_children_info */
static svn_error_t *
base_get_children_info(apr_hash_t **nodes,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       svn_boolean_t obtain_locks,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t last_repos_id = INVALID_REPOS_ID;
  const char *last_repos_root_url = NULL;

  *nodes = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    obtain_locks
                                      ? STMT_SELECT_BASE_CHILDREN_INFO_LOCK
                                      : STMT_SELECT_BASE_CHILDREN_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      struct svn_wc__db_base_info_t *info;
      apr_int64_t repos_id;
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *name = svn_relpath_basename(child_relpath, result_pool);

      info = apr_pcalloc(result_pool, sizeof(*info));

      repos_id = svn_sqlite__column_int64(stmt, 1);
      info->repos_relpath = svn_sqlite__column_text(stmt, 2, result_pool);
      info->status = svn_sqlite__column_token(stmt, 3, presence_map);
      info->kind = svn_sqlite__column_token(stmt, 4, kind_map);
      info->revnum = svn_sqlite__column_revnum(stmt, 5);

      info->depth = svn_sqlite__column_token_null(stmt, 6, depth_map,
                                                  svn_depth_unknown);

      info->update_root = svn_sqlite__column_boolean(stmt, 7);

      if (obtain_locks)
        info->lock = lock_from_columns(stmt, 8, 9, 10, 11, result_pool);

      if (repos_id != last_repos_id)
        {
          svn_error_t *err;

          err = svn_wc__db_fetch_repos_info(&last_repos_root_url, NULL,
                                            wcroot, repos_id,
                                            result_pool);

          if (err)
            return svn_error_trace(
                     svn_error_compose_create(err,
                                              svn_sqlite__reset(stmt)));

          last_repos_id = repos_id;
        }

      info->repos_root_url = last_repos_root_url;

      svn_hash_sets(*nodes, name, info);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_base_get_children_info(apr_hash_t **nodes,
                                  svn_wc__db_t *db,
                                  const char *dir_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(base_get_children_info(nodes,
                                                wcroot,
                                                local_relpath,
                                                TRUE /* obtain_locks */,
                                                result_pool,
                                                scratch_pool));
}


svn_error_t *
svn_wc__db_base_get_props(apr_hash_t **props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t presence;

  SVN_ERR(svn_wc__db_base_get_info(&presence, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, props, NULL,
                                   db, local_abspath,
                                   result_pool, scratch_pool));
  if (presence != svn_wc__db_status_normal
      && presence != svn_wc__db_status_incomplete)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("The node '%s' has a BASE status that"
                                  " has no properties."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_children(const apr_array_header_t **children,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(
              gather_children(children, wcroot, local_relpath,
                              STMT_SELECT_OP_DEPTH_CHILDREN, 0,
                              result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_base_set_dav_cache(svn_wc__db_t *db,
                              const char *local_abspath,
                              const apr_hash_t *props,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_BASE_NODE_DAV_CACHE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));

  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows != 1)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_dav_cache(apr_hash_t **props,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_BASE_DAV_CACHE));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                        scratch_pool));
  return svn_error_trace(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_base_clear_dav_cache_recursive(svn_wc__db_t *db,
                                          const char *local_abspath,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                             db, local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_depth_get_info(svn_wc__db_status_t *status,
                          svn_node_kind_t *kind,
                          svn_revnum_t *revision,
                          const char **repos_relpath,
                          apr_int64_t *repos_id,
                          svn_revnum_t *changed_rev,
                          apr_time_t *changed_date,
                          const char **changed_author,
                          svn_depth_t *depth,
                          const svn_checksum_t **checksum,
                          const char **target,
                          svn_boolean_t *had_props,
                          apr_hash_t **props,
                          svn_wc__db_wcroot_t *wcroot,
                          const char *local_relpath,
                          int op_depth,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_DEPTH_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd",
                            wcroot->wc_id, local_relpath, op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_status_t node_status = svn_sqlite__column_token(stmt, 2,
                                                                 presence_map);
      svn_node_kind_t node_kind = svn_sqlite__column_token(stmt, 3, kind_map);

      if (kind)
        {
          *kind = node_kind;
        }
      if (status)
        {
          *status = node_status;

          if (op_depth > 0)
            SVN_ERR(convert_to_working_status(status, *status));
        }
      repos_location_from_columns(repos_id, revision, repos_relpath,
                                  stmt, 0, 4, 1, result_pool);

      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt, 7);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt, 8);
        }
      if (changed_author)
        {
          /* Result may be NULL. */
          *changed_author = svn_sqlite__column_text(stmt, 9, result_pool);
        }
      if (depth)
        {
          if (node_kind != svn_node_dir)
            {
              *depth = svn_depth_unknown;
            }
          else
            {
              *depth = svn_sqlite__column_token_null(stmt, 10, depth_map,
                                                     svn_depth_unknown);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_node_file)
            {
              *checksum = NULL;
            }
          else
            {
              err = svn_sqlite__column_checksum(checksum, stmt, 5,
                                                result_pool);
              if (err != NULL)
                err = svn_error_createf(
                        err->apr_err, err,
                        _("The node '%s' has a corrupt checksum value."),
                        path_for_error_message(wcroot, local_relpath,
                                               scratch_pool));
            }
        }
      if (target)
        {
          if (node_kind != svn_node_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt, 11, result_pool);
        }
      if (had_props)
        {
          *had_props = SQLITE_PROPERTIES_AVAILABLE(stmt, 12);
        }
      if (props)
        {
          if (node_status == svn_wc__db_status_normal
              || node_status == svn_wc__db_status_incomplete)
            {
              SVN_ERR(svn_sqlite__column_properties(props, stmt, 12,
                                                    result_pool, scratch_pool));
              if (*props == NULL)
                *props = apr_hash_make(result_pool);
            }
          else
            {
              assert(svn_sqlite__column_is_null(stmt, 12));
              *props = NULL;
            }
        }
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool));
    }

  /* Note: given the composition, no need to wrap for tracing.  */
  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
}

/* A callback which supplies WCROOTs and LOCAL_RELPATHs. */
typedef svn_error_t *(*svn_wc__db_txn_callback_t)(void *baton,
                                          svn_wc__db_wcroot_t *wcroot,
                                          const char *local_relpath,
                                          apr_pool_t *scratch_pool);

/* Baton for passing args to with_triggers(). */
struct with_triggers_baton_t {
  int create_trigger;
  int drop_trigger;
  svn_wc__db_txn_callback_t cb_func;
  void *cb_baton;
};

/* Helper for creating SQLite triggers, running the main transaction
   callback, and then dropping the triggers.  It guarantees that the
   triggers will not survive the transaction.  This could be used for
   any general prefix/postscript statements where the postscript
   *must* be executed if the transaction completes.

   Implements svn_wc__db_txn_callback_t. */
static svn_error_t *
with_triggers(void *baton,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *scratch_pool)
{
  struct with_triggers_baton_t *b = baton;
  svn_error_t *err1;
  svn_error_t *err2;

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb, b->create_trigger));

  err1 = b->cb_func(b->cb_baton, wcroot, local_relpath, scratch_pool);

  err2 = svn_sqlite__exec_statements(wcroot->sdb, b->drop_trigger);

  return svn_error_trace(svn_error_compose_create(err1, err2));
}


/* Prototype for the "work callback" used by with_finalization().  */
typedef svn_error_t * (*work_callback_t)(
                          void *baton,
                          svn_wc__db_wcroot_t *wcroot,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool);

/* Utility function to provide several features, with a guaranteed
   finalization (ie. to drop temporary tables).

   1) for WCROOT and LOCAL_RELPATH, run TXN_CB(TXN_BATON) within a
      sqlite transaction
   2) if (1) is successful and a NOTIFY_FUNC is provided, then run
      the "work" step: WORK_CB(WORK_BATON).
   3) execute FINALIZE_STMT_IDX no matter what errors may be thrown
      from the above two steps.

   CANCEL_FUNC, CANCEL_BATON, NOTIFY_FUNC and NOTIFY_BATON are their
   typical values. These are passed to the work callback, which typically
   provides notification about the work done by TXN_CB.  */
static svn_error_t *
with_finalization(svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  svn_wc__db_txn_callback_t txn_cb,
                  void *txn_baton,
                  work_callback_t work_cb,
                  void *work_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  int finalize_stmt_idx,
                  apr_pool_t *scratch_pool)
{
  svn_error_t *err1;
  svn_error_t *err2;

  err1 = svn_sqlite__begin_savepoint(wcroot->sdb);
  if (!err1)
    {
      err1 = txn_cb(txn_baton, wcroot, local_relpath, scratch_pool);

      err1 = svn_sqlite__finish_savepoint(wcroot->sdb, err1);
    }

  if (err1 == NULL && notify_func != NULL)
    {
      err2 = work_cb(work_baton, wcroot,
                     cancel_func, cancel_baton,
                     notify_func, notify_baton,
                     scratch_pool);
      err1 = svn_error_compose_create(err1, err2);
    }

  err2 = svn_sqlite__exec_statements(wcroot->sdb, finalize_stmt_idx);

  return svn_error_trace(svn_error_compose_create(err1, err2));
}


/* Initialize the baton with appropriate "blank" values. This allows the
   insertion function to leave certain columns null.  */
static void
blank_ieb(insert_external_baton_t *ieb)
{
  memset(ieb, 0, sizeof(*ieb));
  ieb->revision = SVN_INVALID_REVNUM;
  ieb->changed_rev = SVN_INVALID_REVNUM;
  ieb->repos_id = INVALID_REPOS_ID;

  ieb->recorded_peg_revision = SVN_INVALID_REVNUM;
  ieb->recorded_revision = SVN_INVALID_REVNUM;
}

/* Insert the externals row represented by (insert_external_baton_t *) BATON.
 *
 * Implements svn_wc__db_txn_callback_t. */
static svn_error_t *
insert_external_node(const insert_external_baton_t *ieb,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_error_t *err;
  svn_boolean_t update_root;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  if (ieb->repos_id != INVALID_REPOS_ID)
    repos_id = ieb->repos_id;
  else
    SVN_ERR(create_repos_id(&repos_id, ieb->repos_root_url, ieb->repos_uuid,
                            wcroot->sdb, scratch_pool));

  /* And there must be no existing BASE node or it must be a file external */
  err = svn_wc__db_base_get_info_internal(&status, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL, NULL, &update_root,
                                          wcroot, local_relpath,
                                          scratch_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
    }
  else if (status == svn_wc__db_status_normal && !update_root)
    return svn_error_create(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL, NULL);

  if (ieb->kind == svn_node_file
      || ieb->kind == svn_node_symlink)
    {
      struct insert_base_baton_t ibb;

      blank_ibb(&ibb);

      ibb.status          = svn_wc__db_status_normal;
      ibb.kind            = ieb->kind;

      ibb.repos_id        = repos_id;
      ibb.repos_relpath   = ieb->repos_relpath;
      ibb.revision        = ieb->revision;

      ibb.props           = ieb->props;
      ibb.iprops          = ieb->iprops;
      ibb.changed_rev     = ieb->changed_rev;
      ibb.changed_date    = ieb->changed_date;
      ibb.changed_author  = ieb->changed_author;

      ibb.dav_cache       = ieb->dav_cache;

      ibb.checksum        = ieb->checksum;
      ibb.target          = ieb->target;

      ibb.conflict        = ieb->conflict;

      ibb.update_actual_props = ieb->update_actual_props;
      ibb.new_actual_props    = ieb->new_actual_props;

      ibb.keep_recorded_info  = ieb->keep_recorded_info;

      ibb.work_items      = ieb->work_items;

      ibb.file_external = TRUE;

      SVN_ERR(insert_base_node(&ibb, wcroot, local_relpath, scratch_pool));
    }
  else
    SVN_ERR(add_work_items(wcroot->sdb, ieb->work_items, scratch_pool));

  /* The externals table only support presence normal and excluded */
  SVN_ERR_ASSERT(ieb->presence == svn_wc__db_status_normal
                 || ieb->presence == svn_wc__db_status_excluded);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_EXTERNAL));

  SVN_ERR(svn_sqlite__bindf(stmt, "issttsis",
                            wcroot->wc_id,
                            local_relpath,
                            svn_relpath_dirname(local_relpath,
                                                scratch_pool),
                            presence_map, ieb->presence,
                            kind_map, ieb->kind,
                            ieb->record_ancestor_relpath,
                            repos_id,
                            ieb->recorded_repos_relpath));

  if (SVN_IS_VALID_REVNUM(ieb->recorded_peg_revision))
    SVN_ERR(svn_sqlite__bind_revnum(stmt, 9, ieb->recorded_peg_revision));

  if (SVN_IS_VALID_REVNUM(ieb->recorded_revision))
    SVN_ERR(svn_sqlite__bind_revnum(stmt, 10, ieb->recorded_revision));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_external_add_file(svn_wc__db_t *db,
                             const char *local_abspath,
                             const char *wri_abspath,

                             const char *repos_relpath,
                             const char *repos_root_url,
                             const char *repos_uuid,
                             svn_revnum_t revision,

                             const apr_hash_t *props,
                             apr_array_header_t *iprops,

                             svn_revnum_t changed_rev,
                             apr_time_t changed_date,
                             const char *changed_author,

                             const svn_checksum_t *checksum,

                             const apr_hash_t *dav_cache,

                             const char *record_ancestor_abspath,
                             const char *recorded_repos_relpath,
                             svn_revnum_t recorded_peg_revision,
                             svn_revnum_t recorded_revision,

                             svn_boolean_t update_actual_props,
                             apr_hash_t *new_actual_props,

                             svn_boolean_t keep_recorded_info,
                             const svn_skel_t *conflict,
                             const svn_skel_t *work_items,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_external_baton_t ieb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (! wri_abspath)
    wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath,
                                        record_ancestor_abspath));

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath, local_abspath));

  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  blank_ieb(&ieb);

  ieb.kind = svn_node_file;
  ieb.presence = svn_wc__db_status_normal;

  ieb.repos_root_url = repos_root_url;
  ieb.repos_uuid = repos_uuid;

  ieb.repos_relpath = repos_relpath;
  ieb.revision = revision;

  ieb.props = props;
  ieb.iprops = iprops;

  ieb.changed_rev = changed_rev;
  ieb.changed_date = changed_date;
  ieb.changed_author = changed_author;

  ieb.checksum = checksum;

  ieb.dav_cache = dav_cache;

  ieb.record_ancestor_relpath = svn_dirent_skip_ancestor(
                                                wcroot->abspath,
                                                record_ancestor_abspath);
  ieb.recorded_repos_relpath = recorded_repos_relpath;
  ieb.recorded_peg_revision = recorded_peg_revision;
  ieb.recorded_revision = recorded_revision;

  ieb.update_actual_props = update_actual_props;
  ieb.new_actual_props = new_actual_props;

  ieb.keep_recorded_info = keep_recorded_info;

  ieb.conflict = conflict;
  ieb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_external_node(&ieb, wcroot, local_relpath, scratch_pool),
            wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_external_add_symlink(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *wri_abspath,
                                const char *repos_relpath,
                                const char *repos_root_url,
                                const char *repos_uuid,
                                svn_revnum_t revision,
                                const apr_hash_t *props,
                                svn_revnum_t changed_rev,
                                apr_time_t changed_date,
                                const char *changed_author,
                                const char *target,
                                const apr_hash_t *dav_cache,
                                const char *record_ancestor_abspath,
                                const char *recorded_repos_relpath,
                                svn_revnum_t recorded_peg_revision,
                                svn_revnum_t recorded_revision,
                                svn_boolean_t update_actual_props,
                                apr_hash_t *new_actual_props,
                                svn_boolean_t keep_recorded_info,
                                const svn_skel_t *work_items,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_external_baton_t ieb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (! wri_abspath)
    wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath,
                                        record_ancestor_abspath));

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath, local_abspath));

  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  blank_ieb(&ieb);

  ieb.kind = svn_node_symlink;
  ieb.presence = svn_wc__db_status_normal;

  ieb.repos_root_url = repos_root_url;
  ieb.repos_uuid = repos_uuid;

  ieb.repos_relpath = repos_relpath;
  ieb.revision = revision;

  ieb.props = props;

  ieb.changed_rev = changed_rev;
  ieb.changed_date = changed_date;
  ieb.changed_author = changed_author;

  ieb.target = target;

  ieb.dav_cache = dav_cache;

  ieb.record_ancestor_relpath = svn_dirent_skip_ancestor(
                                                wcroot->abspath,
                                                record_ancestor_abspath);
  ieb.recorded_repos_relpath = recorded_repos_relpath;
  ieb.recorded_peg_revision = recorded_peg_revision;
  ieb.recorded_revision = recorded_revision;

  ieb.update_actual_props = update_actual_props;
  ieb.new_actual_props = new_actual_props;

  ieb.keep_recorded_info = keep_recorded_info;

  ieb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_external_node(&ieb, wcroot, local_relpath, scratch_pool),
            wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_external_add_dir(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *wri_abspath,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            const char *record_ancestor_abspath,
                            const char *recorded_repos_relpath,
                            svn_revnum_t recorded_peg_revision,
                            svn_revnum_t recorded_revision,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_external_baton_t ieb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (! wri_abspath)
    wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath,
                                        record_ancestor_abspath));

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath, local_abspath));

  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  blank_ieb(&ieb);

  ieb.kind = svn_node_dir;
  ieb.presence = svn_wc__db_status_normal;

  ieb.repos_root_url = repos_root_url;
  ieb.repos_uuid = repos_uuid;

  ieb.record_ancestor_relpath = svn_dirent_skip_ancestor(
                                                wcroot->abspath,
                                                record_ancestor_abspath);
  ieb.recorded_repos_relpath = recorded_repos_relpath;
  ieb.recorded_peg_revision = recorded_peg_revision;
  ieb.recorded_revision = recorded_revision;

  ieb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_external_node(&ieb, wcroot, local_relpath, scratch_pool),
            wcroot);

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_external_remove(). */
static svn_error_t *
db_external_remove(const svn_skel_t *work_items,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_EXTERNAL));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (!affected_rows)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' is not an external."),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  /* ### What about actual? */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_external_remove(svn_wc__db_t *db,
                           const char *local_abspath,
                           const char *wri_abspath,
                           const svn_skel_t *work_items,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (! wri_abspath)
    wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath, local_abspath));

  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  SVN_WC__DB_WITH_TXN(db_external_remove(work_items, wcroot, local_relpath,
                                         scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_external_read(svn_wc__db_status_t *status,
                         svn_node_kind_t *kind,
                         const char **definining_abspath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         const char **recorded_repos_relpath,
                         svn_revnum_t *recorded_peg_revision,
                         svn_revnum_t *recorded_revision,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *wri_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_info;
  svn_error_t *err = NULL;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (! wri_abspath)
    wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR_ASSERT(svn_dirent_is_ancestor(wcroot->abspath, local_abspath));

  local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_EXTERNAL_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_info, stmt));

  if (have_info)
    {
      if (status)
        *status = svn_sqlite__column_token(stmt, 0, presence_map);

      if (kind)
        *kind = svn_sqlite__column_token(stmt, 1, kind_map);

      if (definining_abspath)
        {
          const char *record_relpath = svn_sqlite__column_text(stmt, 2, NULL);

          *definining_abspath = svn_dirent_join(wcroot->abspath,
                                                record_relpath, result_pool);
        }

      if (repos_root_url || repos_uuid)
        {
          apr_int64_t repos_id;

          repos_id = svn_sqlite__column_int64(stmt, 3);

          err = svn_error_compose_create(
                        err,
                        svn_wc__db_fetch_repos_info(repos_root_url, repos_uuid,
                                                    wcroot, repos_id,
                                                    result_pool));
        }

      if (recorded_repos_relpath)
        *recorded_repos_relpath = svn_sqlite__column_text(stmt, 4,
                                                          result_pool);

      if (recorded_peg_revision)
        *recorded_peg_revision = svn_sqlite__column_revnum(stmt, 5);

      if (recorded_revision)
        *recorded_revision = svn_sqlite__column_revnum(stmt, 6);
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' is not an external."),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }

  return svn_error_trace(
                svn_error_compose_create(err, svn_sqlite__reset(stmt)));
}

svn_error_t *
svn_wc__db_committable_externals_below(apr_array_header_t **externals,
                                       svn_wc__db_t *db,
                                       const char *local_abspath,
                                       svn_boolean_t immediates_only,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;
  svn_boolean_t have_row;
  svn_wc__committable_external_info_t *info;
  svn_node_kind_t db_kind;
  apr_array_header_t *result = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(
                &stmt, wcroot->sdb,
                immediates_only
                    ? STMT_SELECT_COMMITTABLE_EXTERNALS_IMMEDIATELY_BELOW
                    : STMT_SELECT_COMMITTABLE_EXTERNALS_BELOW));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    result = apr_array_make(result_pool, 0,
                            sizeof(svn_wc__committable_external_info_t *));

  while (have_row)
    {
      info = apr_palloc(result_pool, sizeof(*info));

      local_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      info->local_abspath = svn_dirent_join(wcroot->abspath, local_relpath,
                                            result_pool);

      db_kind = svn_sqlite__column_token(stmt, 1, kind_map);
      SVN_ERR_ASSERT(db_kind == svn_node_file || db_kind == svn_node_dir);
      info->kind = db_kind;

      info->repos_relpath = svn_sqlite__column_text(stmt, 2, result_pool);
      info->repos_root_url = svn_sqlite__column_text(stmt, 3, result_pool);

      APR_ARRAY_PUSH(result, svn_wc__committable_external_info_t *) = info;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *externals = result;
  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_externals_defined_below(apr_hash_t **externals,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_EXTERNALS_DEFINED));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  *externals = apr_hash_make(result_pool);
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      const char *def_local_relpath;

      local_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      def_local_relpath = svn_sqlite__column_text(stmt, 1, NULL);

      svn_hash_sets(*externals,
                    svn_dirent_join(wcroot->abspath, local_relpath,
                                    result_pool),
                    svn_dirent_join(wcroot->abspath, def_local_relpath,
                                    result_pool));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_externals_gather_definitions(apr_hash_t **externals,
                                        apr_hash_t **depths,
                                        svn_wc__db_t *db,
                                        const char *local_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, iterpool));
  VERIFY_USABLE_WCROOT(wcroot);

  *externals = apr_hash_make(result_pool);
  if (depths != NULL)
    *depths = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_EXTERNAL_PROPERTIES));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      apr_hash_t *node_props;
      const char *external_value;

      svn_pool_clear(iterpool);
      err = svn_sqlite__column_properties(&node_props, stmt, 0, iterpool,
                                          iterpool);

      if (err)
        break;

      external_value = svn_prop_get_value(node_props, SVN_PROP_EXTERNALS);

      if (external_value)
        {
          const char *node_abspath;
          const char *node_relpath = svn_sqlite__column_text(stmt, 1, NULL);

          node_abspath = svn_dirent_join(wcroot->abspath, node_relpath,
                                         result_pool);

          svn_hash_sets(*externals, node_abspath,
                        apr_pstrdup(result_pool, external_value));

          if (depths)
            {
              svn_depth_t depth
                = svn_sqlite__column_token_null(stmt, 2, depth_map,
                                                svn_depth_unknown);

              svn_hash_sets(*depths, node_abspath,
                            /* Use static string */
                            svn_token__to_word(depth_map, depth));
            }
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  svn_pool_destroy(iterpool);

  return svn_error_trace(svn_error_compose_create(err,
                                                  svn_sqlite__reset(stmt)));
}

/* Copy the ACTUAL data for SRC_RELPATH and tweak it to refer to DST_RELPATH.
   The new ACTUAL data won't have any conflicts. */
static svn_error_t *
copy_actual(svn_wc__db_wcroot_t *src_wcroot,
            const char *src_relpath,
            svn_wc__db_wcroot_t *dst_wcroot,
            const char *dst_relpath,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", src_wcroot->wc_id, src_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      apr_size_t props_size;
      const char *changelist;
      const char *properties;

      /* Skipping conflict data... */
      changelist = svn_sqlite__column_text(stmt, 0, scratch_pool);
      /* No need to parse the properties when simply copying. */
      properties = svn_sqlite__column_blob(stmt, 1, &props_size, scratch_pool);

      if (changelist || properties)
        {
          SVN_ERR(svn_sqlite__reset(stmt));

          SVN_ERR(svn_sqlite__get_statement(&stmt, dst_wcroot->sdb,
                                            STMT_INSERT_ACTUAL_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "issbs",
                                    dst_wcroot->wc_id, dst_relpath,
                                svn_relpath_dirname(dst_relpath, scratch_pool),
                                    properties, props_size, changelist));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_op_copy to handle copying from one db to
   another */
static svn_error_t *
cross_db_copy(svn_wc__db_wcroot_t *src_wcroot,
              const char *src_relpath,
              svn_wc__db_wcroot_t *dst_wcroot,
              const char *dst_relpath,
              svn_wc__db_status_t dst_status,
              int dst_op_depth,
              int dst_np_op_depth,
              svn_node_kind_t kind,
              const apr_array_header_t *children,
              apr_int64_t copyfrom_id,
              const char *copyfrom_relpath,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *scratch_pool)
{
  insert_working_baton_t iwb;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const svn_checksum_t *checksum;
  apr_hash_t *props;
  svn_depth_t depth;

  SVN_ERR_ASSERT(kind == svn_node_file
                 || kind == svn_node_dir
                 );

  SVN_ERR(read_info(NULL, NULL, NULL, NULL, NULL,
                    &changed_rev, &changed_date, &changed_author, &depth,
                    &checksum, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    src_wcroot, src_relpath, scratch_pool, scratch_pool));

  if (dst_status != svn_wc__db_status_not_present
      && dst_status != svn_wc__db_status_excluded
      && dst_status != svn_wc__db_status_server_excluded)
    {
      SVN_ERR(db_read_pristine_props(&props, src_wcroot, src_relpath, FALSE,
                                     scratch_pool, scratch_pool));
    }
  else
    props = NULL;

  blank_iwb(&iwb);
  iwb.presence = dst_status;
  iwb.kind = kind;

  iwb.props = props;
  iwb.changed_rev = changed_rev;
  iwb.changed_date = changed_date;
  iwb.changed_author = changed_author;
  iwb.original_repos_id = copyfrom_id;
  iwb.original_repos_relpath = copyfrom_relpath;
  iwb.original_revnum = copyfrom_rev;
  iwb.moved_here = FALSE;

  iwb.op_depth = dst_op_depth;

  iwb.checksum = checksum;
  iwb.children = children;
  iwb.depth = depth;

  iwb.not_present_op_depth = dst_np_op_depth;

  SVN_ERR(insert_working_node(&iwb, dst_wcroot, dst_relpath, scratch_pool));

  SVN_ERR(copy_actual(src_wcroot, src_relpath,
                      dst_wcroot, dst_relpath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Helper for scan_deletion_txn. Extracts the moved-to information, if
   any, from STMT.  Sets *SCAN to FALSE if moved-to was available. */
static svn_error_t *
get_moved_to(const char **moved_to_relpath_p,
             const char **moved_to_op_root_relpath_p,
             svn_boolean_t *scan,
             svn_sqlite__stmt_t *stmt,
             const char *current_relpath,
             svn_wc__db_wcroot_t *wcroot,
             const char *local_relpath,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  const char *moved_to_relpath = svn_sqlite__column_text(stmt, 3, NULL);

  if (moved_to_relpath)
    {
      const char *moved_to_op_root_relpath = moved_to_relpath;

      if (strcmp(current_relpath, local_relpath))
        {
          /* LOCAL_RELPATH is a child inside the move op-root. */
          const char *moved_child_relpath;

          /* The CURRENT_RELPATH is the op_root of the delete-half of
           * the move. LOCAL_RELPATH is a child that was moved along.
           * Compute the child's new location within the move target. */
          moved_child_relpath = svn_relpath_skip_ancestor(current_relpath,
                                                          local_relpath);
          SVN_ERR_ASSERT(moved_child_relpath &&
                         strlen(moved_child_relpath) > 0);
          moved_to_relpath = svn_relpath_join(moved_to_op_root_relpath,
                                              moved_child_relpath,
                                              result_pool);
        }

      if (moved_to_op_root_relpath && moved_to_op_root_relpath_p)
        *moved_to_op_root_relpath_p
          = apr_pstrdup(result_pool, moved_to_op_root_relpath);

      if (moved_to_relpath && moved_to_relpath_p)
        *moved_to_relpath_p
          = apr_pstrdup(result_pool, moved_to_relpath);

      *scan = FALSE;
    }

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_scan_deletion().
 */
static svn_error_t *
scan_deletion(const char **base_del_relpath,
              const char **moved_to_relpath,
              const char **work_del_relpath,
              const char **moved_to_op_root_relpath,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *current_relpath = local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_status_t work_presence;
  svn_boolean_t have_row, scan, have_base;
  int op_depth;

  /* Initialize all the OUT parameters.  */
  if (base_del_relpath != NULL)
    *base_del_relpath = NULL;
  if (moved_to_relpath != NULL)
    *moved_to_relpath = NULL;
  if (work_del_relpath != NULL)
    *work_del_relpath = NULL;
  if (moved_to_op_root_relpath != NULL)
    *moved_to_op_root_relpath = NULL;

  /* If looking for moved-to info then we need to scan every path
     until we find it.  If not looking for moved-to we only need to
     check op-roots and parents of op-roots. */
  scan = (moved_to_op_root_relpath || moved_to_relpath);

  SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb, STMT_SELECT_DELETION_INFO));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, current_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  work_presence = svn_sqlite__column_token(stmt, 1, presence_map);
  have_base = !svn_sqlite__column_is_null(stmt, 0);
  if (work_presence != svn_wc__db_status_not_present
      && work_presence != svn_wc__db_status_base_deleted)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                             svn_sqlite__reset(stmt),
                             _("Expected node '%s' to be deleted."),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  op_depth = svn_sqlite__column_int(stmt, 2);

  /* Special case: LOCAL_RELPATH not-present within a WORKING tree, we
     treat this as an op-root.  At commit time we need to explicitly
     delete such nodes otherwise they will be present in the
     repository copy. */
  if (work_presence == svn_wc__db_status_not_present
      && work_del_relpath && !*work_del_relpath)
    {
      *work_del_relpath = apr_pstrdup(result_pool, current_relpath);

      if (!scan && !base_del_relpath)
        {
          /* We have all we need, exit early */
          SVN_ERR(svn_sqlite__reset(stmt));
          return SVN_NO_ERROR;
        }
    }


  while (TRUE)
    {
      svn_error_t *err;
      const char *parent_relpath;
      int current_depth = relpath_depth(current_relpath);

      /* Step CURRENT_RELPATH to op-root */

      while (TRUE)
        {
          if (scan)
            {
              err = get_moved_to(moved_to_relpath, moved_to_op_root_relpath,
                                 &scan, stmt, current_relpath,
                                 wcroot, local_relpath,
                                 result_pool, scratch_pool);
              if (err || (!scan
                          && !base_del_relpath
                          && !work_del_relpath))
                {
                  /* We have all we need (or an error occurred) */
                  SVN_ERR(svn_sqlite__reset(stmt));
                  return svn_error_trace(err);
                }
            }

          if (current_depth <= op_depth)
            break;

          current_relpath = svn_relpath_dirname(current_relpath, scratch_pool);
          --current_depth;

          if (scan || current_depth == op_depth)
            {
              SVN_ERR(svn_sqlite__reset(stmt));
              SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                        current_relpath));
              SVN_ERR(svn_sqlite__step(&have_row, stmt));
              SVN_ERR_ASSERT(have_row);
              have_base = !svn_sqlite__column_is_null(stmt, 0);
            }
        }
      SVN_ERR(svn_sqlite__reset(stmt));

      /* Now CURRENT_RELPATH is an op-root, have a look at the parent. */

      SVN_ERR_ASSERT(current_relpath[0] != '\0'); /* Catch invalid data */
      parent_relpath = svn_relpath_dirname(current_relpath, scratch_pool);
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, parent_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        {
          /* No row means no WORKING node which mean we just fell off
             the WORKING tree, so CURRENT_RELPATH is the op-root
             closest to the wc root. */
          if (have_base && base_del_relpath)
            *base_del_relpath = apr_pstrdup(result_pool, current_relpath);
          break;
        }

      /* Still in the WORKING tree so the first time we get here
         CURRENT_RELPATH is a delete op-root in the WORKING tree. */
      if (work_del_relpath && !*work_del_relpath)
        {
          *work_del_relpath = apr_pstrdup(result_pool, current_relpath);

          if (!scan && !base_del_relpath)
            break; /* We have all we need */
        }

      current_relpath = parent_relpath;
      op_depth = svn_sqlite__column_int(stmt, 2);
      have_base = !svn_sqlite__column_is_null(stmt, 0);
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_scan_deletion_internal(
              const char **base_del_relpath,
              const char **moved_to_relpath,
              const char **work_del_relpath,
              const char **moved_to_op_root_relpath,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            scan_deletion(base_del_relpath, moved_to_relpath, work_del_relpath,
                          moved_to_op_root_relpath,
                          wcroot, local_relpath,
                          result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_scan_deletion(const char **base_del_abspath,
                         const char **moved_to_abspath,
                         const char **work_del_abspath,
                         const char **moved_to_op_root_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *base_del_relpath, *moved_to_relpath, *work_del_relpath;
  const char *moved_to_op_root_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    scan_deletion(&base_del_relpath, &moved_to_relpath,
                  &work_del_relpath, &moved_to_op_root_relpath,
                  wcroot, local_relpath, result_pool, scratch_pool),
    wcroot);

  if (base_del_abspath)
    {
      *base_del_abspath = (base_del_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             base_del_relpath, result_pool)
                           : NULL);
    }
  if (moved_to_abspath)
    {
      *moved_to_abspath = (moved_to_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             moved_to_relpath, result_pool)
                           : NULL);
    }
  if (work_del_abspath)
    {
      *work_del_abspath = (work_del_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             work_del_relpath, result_pool)
                           : NULL);
    }
  if (moved_to_op_root_abspath)
    {
      *moved_to_op_root_abspath = (moved_to_op_root_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             moved_to_op_root_relpath,
                                             result_pool)
                           : NULL);
    }

  return SVN_NO_ERROR;
}


/* Set *COPYFROM_ID, *COPYFROM_RELPATH, *COPYFROM_REV to the values
   appropriate for the copy. Also return *STATUS, *KIND and *HAVE_WORK, *OP_ROOT
   since they are available.  This is a helper for
   svn_wc__db_op_copy. */
static svn_error_t *
get_info_for_copy(apr_int64_t *copyfrom_id,
                  const char **copyfrom_relpath,
                  svn_revnum_t *copyfrom_rev,
                  svn_wc__db_status_t *status,
                  svn_node_kind_t *kind,
                  svn_boolean_t *op_root,
                  svn_wc__db_wcroot_t *src_wcroot,
                  const char *local_relpath,
                  svn_wc__db_wcroot_t *dst_wcroot,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *repos_relpath;
  svn_revnum_t revision;
  svn_wc__db_status_t node_status;
  apr_int64_t repos_id;
  svn_boolean_t is_op_root;

  SVN_ERR(read_info(&node_status, kind, &revision, &repos_relpath, &repos_id,
                    NULL, NULL, NULL, NULL, NULL, NULL, copyfrom_relpath,
                    copyfrom_id, copyfrom_rev, NULL, NULL, NULL, NULL,
                    NULL, &is_op_root, NULL, NULL,
                    NULL /* have_base */,
                    NULL /* have_more_work */,
                    NULL /* have_work */,
                    src_wcroot, local_relpath, result_pool, scratch_pool));

  if (op_root)
    *op_root = is_op_root;

  if (node_status == svn_wc__db_status_excluded)
    {
      /* The parent cannot be excluded, so look at the parent and then
         adjust the relpath */
      const char *parent_relpath, *base_name;

      svn_dirent_split(&parent_relpath, &base_name, local_relpath,
                       scratch_pool);
      SVN_ERR(get_info_for_copy(copyfrom_id, copyfrom_relpath, copyfrom_rev,
                                NULL, NULL, NULL,
                                src_wcroot, parent_relpath, dst_wcroot,
                                scratch_pool, scratch_pool));
      if (*copyfrom_relpath)
        *copyfrom_relpath = svn_relpath_join(*copyfrom_relpath, base_name,
                                             result_pool);
    }
  else if (node_status == svn_wc__db_status_added)
    {
      SVN_ERR(scan_addition(&node_status, NULL, NULL, NULL, NULL, NULL, NULL,
                            NULL, NULL, NULL, src_wcroot, local_relpath,
                            scratch_pool, scratch_pool));
    }
  else if (node_status == svn_wc__db_status_deleted && is_op_root)
    {
      const char *base_del_relpath, *work_del_relpath;

      SVN_ERR(scan_deletion(&base_del_relpath, NULL,
                            &work_del_relpath,
                            NULL, src_wcroot, local_relpath,
                            scratch_pool, scratch_pool));
      if (work_del_relpath)
        {
          const char *op_root_relpath;
          const char *parent_del_relpath = svn_relpath_dirname(work_del_relpath,
                                                               scratch_pool);

          /* Similar to, but not the same as, the _scan_addition and
             _join above.  Can we use get_copyfrom here? */
          SVN_ERR(scan_addition(NULL, &op_root_relpath,
                                NULL, NULL, /* repos_* */
                                copyfrom_relpath, copyfrom_id, copyfrom_rev,
                                NULL, NULL, NULL,
                                src_wcroot, parent_del_relpath,
                                scratch_pool, scratch_pool));
          *copyfrom_relpath
            = svn_relpath_join(*copyfrom_relpath,
                               svn_relpath_skip_ancestor(op_root_relpath,
                                                         local_relpath),
                               result_pool);
        }
      else if (base_del_relpath)
        {
          SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, copyfrom_rev,
                                                    copyfrom_relpath,
                                                    copyfrom_id, NULL, NULL,
                                                    NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, NULL,
                                                    src_wcroot, local_relpath,
                                                    result_pool,
                                                    scratch_pool));
        }
      else
        SVN_ERR_MALFUNCTION();
    }
  else if (node_status == svn_wc__db_status_deleted)
    {
      /* Keep original_* from read_info() to allow seeing the difference
         between base-deleted and not present */
    }
  else
    {
      *copyfrom_relpath = repos_relpath;
      *copyfrom_rev = revision;
      *copyfrom_id = repos_id;
    }

  if (status)
    *status = node_status;

  if (src_wcroot != dst_wcroot && *copyfrom_relpath)
    {
      const char *repos_root_url;
      const char *repos_uuid;

      /* Pass the right repos-id for the destination db. We can't just use
         the id of the source database, as this value can change after
         relocation (and perhaps also when we start storing multiple
         working copies in a single db)! */

      SVN_ERR(svn_wc__db_fetch_repos_info(&repos_root_url, &repos_uuid,
                                          src_wcroot, *copyfrom_id,
                                          scratch_pool));

      SVN_ERR(create_repos_id(copyfrom_id, repos_root_url, repos_uuid,
                              dst_wcroot->sdb, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* Set *OP_DEPTH to the highest op depth of WCROOT:LOCAL_RELPATH. */
static svn_error_t *
op_depth_of(int *op_depth,
            svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR_ASSERT(have_row);
  *op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


/* Determine at which OP_DEPTH a copy of COPYFROM_REPOS_ID, COPYFROM_RELPATH at
   revision COPYFROM_REVISION should be inserted as LOCAL_RELPATH. Do this
   by checking if this would be a direct child of a copy of its parent
   directory. If it is then set *OP_DEPTH to the op_depth of its parent.

   If the node is not a direct copy at the same revision of the parent
   *NP_OP_DEPTH will be set to the op_depth of the parent when a not-present
   node should be inserted at this op_depth. This will be the case when the
   parent already defined an incomplete child with the same name. Otherwise
   *NP_OP_DEPTH will be set to -1.

   If the parent node is not the parent of the to be copied node, then
   *OP_DEPTH will be set to the proper op_depth for a new operation root.

   Set *PARENT_OP_DEPTH to the op_depth of the parent.

 */
static svn_error_t *
op_depth_for_copy(int *op_depth,
                  int *np_op_depth,
                  int *parent_op_depth,
                  apr_int64_t copyfrom_repos_id,
                  const char *copyfrom_relpath,
                  svn_revnum_t copyfrom_revision,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  const char *parent_relpath, *name;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int incomplete_op_depth = -1;
  int min_op_depth = 1; /* Never touch BASE */

  *op_depth = relpath_depth(local_relpath);
  *np_op_depth = -1;

  svn_relpath_split(&parent_relpath, &name, local_relpath, scratch_pool);
  *parent_op_depth = relpath_depth(parent_relpath);

  if (!copyfrom_relpath)
    return SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      svn_wc__db_status_t status = svn_sqlite__column_token(stmt, 1,
                                                            presence_map);

      min_op_depth = svn_sqlite__column_int(stmt, 0);
      if (status == svn_wc__db_status_incomplete)
        incomplete_op_depth = min_op_depth;
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, parent_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      svn_wc__db_status_t presence = svn_sqlite__column_token(stmt, 1,
                                                              presence_map);

      *parent_op_depth = svn_sqlite__column_int(stmt, 0);
      if (*parent_op_depth < min_op_depth)
        {
          /* We want to create a copy; not overwrite the lower layers */
          SVN_ERR(svn_sqlite__reset(stmt));
          return SVN_NO_ERROR;
        }

      /* You can only add children below a node that exists.
         In WORKING that must be status added, which is represented
         as presence normal */
      SVN_ERR_ASSERT(presence == svn_wc__db_status_normal);

      if ((incomplete_op_depth < 0)
          || (incomplete_op_depth == *parent_op_depth))
        {
          apr_int64_t parent_copyfrom_repos_id
            = svn_sqlite__column_int64(stmt, 10);
          const char *parent_copyfrom_relpath
            = svn_sqlite__column_text(stmt, 11, NULL);
          svn_revnum_t parent_copyfrom_revision
            = svn_sqlite__column_revnum(stmt, 12);

          if (parent_copyfrom_repos_id == copyfrom_repos_id)
            {
              if (copyfrom_revision == parent_copyfrom_revision
                  && !strcmp(copyfrom_relpath,
                             svn_relpath_join(parent_copyfrom_relpath, name,
                                              scratch_pool)))
                *op_depth = *parent_op_depth;
              else if (incomplete_op_depth > 0)
                *np_op_depth = incomplete_op_depth;
            }
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


/* Like svn_wc__db_op_copy(), but with WCROOT+LOCAL_RELPATH
 * instead of DB+LOCAL_ABSPATH. A non-zero MOVE_OP_DEPTH implies that the
 * copy operation is part of a move, and indicates the op-depth of the
 * move destination op-root. */
static svn_error_t *
db_op_copy(svn_wc__db_wcroot_t *src_wcroot,
           const char *src_relpath,
           svn_wc__db_wcroot_t *dst_wcroot,
           const char *dst_relpath,
           const svn_skel_t *work_items,
           int move_op_depth,
           apr_pool_t *scratch_pool)
{
  const char *copyfrom_relpath;
  svn_revnum_t copyfrom_rev;
  svn_wc__db_status_t status;
  svn_wc__db_status_t dst_presence;
  svn_boolean_t op_root;
  apr_int64_t copyfrom_id;
  int dst_op_depth;
  int dst_np_op_depth;
  int dst_parent_op_depth;
  svn_node_kind_t kind;
  const apr_array_header_t *children;

  SVN_ERR(get_info_for_copy(&copyfrom_id, &copyfrom_relpath, &copyfrom_rev,
                            &status, &kind, &op_root,
                            src_wcroot, src_relpath, dst_wcroot,
                            scratch_pool, scratch_pool));

  SVN_ERR(op_depth_for_copy(&dst_op_depth, &dst_np_op_depth,
                            &dst_parent_op_depth,
                            copyfrom_id, copyfrom_relpath, copyfrom_rev,
                            dst_wcroot, dst_relpath, scratch_pool));

  SVN_ERR_ASSERT(kind == svn_node_file || kind == svn_node_dir);

  /* ### New status, not finished, see notes/wc-ng/copying */
  switch (status)
    {
    case svn_wc__db_status_normal:
    case svn_wc__db_status_added:
    case svn_wc__db_status_moved_here:
    case svn_wc__db_status_copied:
      dst_presence = svn_wc__db_status_normal;
      break;
    case svn_wc__db_status_deleted:
      if (op_root)
        {
          /* If the lower layer is already shadowcopied we can skip adding
             a not present node. */
          svn_error_t *err;
          svn_wc__db_status_t dst_status;

          err = read_info(&dst_status, NULL, NULL, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                          NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                          dst_wcroot, dst_relpath, scratch_pool, scratch_pool);

          if (err)
            {
              if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
                svn_error_clear(err);
              else
                return svn_error_trace(err);
            }
          else if (dst_status == svn_wc__db_status_deleted)
            {
              /* Node is already deleted; skip the NODES work, but do
                 install wq items if requested */
              SVN_ERR(add_work_items(dst_wcroot->sdb, work_items,
                                     scratch_pool));
              return SVN_NO_ERROR;
            }
        }
      else
        {
          /* This node is either a not-present node (which should be copied), or
             a base-delete of some lower layer (which shouldn't).
             Subversion <= 1.7 always added a not-present node here, which is
             safe (as it postpones the hard work until commit time and then we
             ask the repository), but it breaks some move scenarios.
             */

           if (! copyfrom_relpath)
             {
               SVN_ERR(add_work_items(dst_wcroot->sdb, work_items,
                                     scratch_pool));
               return SVN_NO_ERROR;
             }

           /* Fall through. Install not present node */
        }
    case svn_wc__db_status_not_present:
    case svn_wc__db_status_excluded:
      /* These presence values should not create a new op depth */
      if (dst_np_op_depth > 0)
        {
          dst_op_depth = dst_np_op_depth;
          dst_np_op_depth = -1;
        }
      if (status == svn_wc__db_status_excluded)
        dst_presence = svn_wc__db_status_excluded;
      else
        dst_presence = svn_wc__db_status_not_present;
      break;
    case svn_wc__db_status_server_excluded:
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot copy '%s' excluded by server"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    default:
      /* Perhaps we should allow incomplete to incomplete? We can't
         avoid incomplete working nodes as one step in copying a
         directory is to add incomplete children. */
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot handle status of '%s'"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    }

  if (kind == svn_node_dir)
    {
      int src_op_depth;

      SVN_ERR(op_depth_of(&src_op_depth, src_wcroot, src_relpath));
      SVN_ERR(gather_children(&children, src_wcroot, src_relpath,
                              STMT_SELECT_OP_DEPTH_CHILDREN, src_op_depth,
                              scratch_pool, scratch_pool));
    }
  else
    children = NULL;

  if (src_wcroot == dst_wcroot)
    {
      svn_sqlite__stmt_t *stmt;
      const char *dst_parent_relpath = svn_relpath_dirname(dst_relpath,
                                                           scratch_pool);

      SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                                        STMT_INSERT_WORKING_NODE_COPY_FROM));

      SVN_ERR(svn_sqlite__bindf(stmt, "issdst",
                    src_wcroot->wc_id, src_relpath,
                    dst_relpath,
                    dst_op_depth,
                    dst_parent_relpath,
                    presence_map, dst_presence));

      if (move_op_depth > 0)
        {
          if (relpath_depth(dst_relpath) == move_op_depth)
            {
              /* We're moving the root of the move operation.
               *
               * When an added node or the op-root of a copy is moved,
               * there is no 'moved-from' corresponding to the moved-here
               * node. So the net effect is the same as copy+delete.
               * Perform a normal copy operation in these cases. */
              if (!(status == svn_wc__db_status_added ||
                    (status == svn_wc__db_status_copied && op_root)))
                SVN_ERR(svn_sqlite__bind_int(stmt, 7, 1));
            }
          else
            {
              svn_sqlite__stmt_t *info_stmt;
              svn_boolean_t have_row;

              /* We're moving a child along with the root of the move.
               *
               * Set moved-here depending on dst_parent, propagating the
               * above decision to moved-along children at the same op_depth.
               * We can't use scan_addition() to detect moved-here because
               * the delete-half of the move might not yet exist. */
              SVN_ERR(svn_sqlite__get_statement(&info_stmt, dst_wcroot->sdb,
                                                STMT_SELECT_NODE_INFO));
              SVN_ERR(svn_sqlite__bindf(info_stmt, "is", dst_wcroot->wc_id,
                                        dst_parent_relpath));
              SVN_ERR(svn_sqlite__step(&have_row, info_stmt));
              SVN_ERR_ASSERT(have_row);
              if (svn_sqlite__column_boolean(info_stmt, 15) &&
                  dst_op_depth == dst_parent_op_depth)
                {
                  SVN_ERR(svn_sqlite__bind_int(stmt, 7, 1));
                  SVN_ERR(svn_sqlite__reset(info_stmt));
                }
              else
                {
                  SVN_ERR(svn_sqlite__reset(info_stmt));

                  /* If the child has been moved into the tree we're moving,
                   * keep its moved-here bit set. */
                  SVN_ERR(svn_sqlite__get_statement(&info_stmt,
                                                    dst_wcroot->sdb,
                                                    STMT_SELECT_NODE_INFO));
                  SVN_ERR(svn_sqlite__bindf(info_stmt, "is",
                                            dst_wcroot->wc_id, src_relpath));
                  SVN_ERR(svn_sqlite__step(&have_row, info_stmt));
                  SVN_ERR_ASSERT(have_row);
                  if (svn_sqlite__column_boolean(info_stmt, 15))
                    SVN_ERR(svn_sqlite__bind_int(stmt, 7, 1));
                  SVN_ERR(svn_sqlite__reset(info_stmt));
                }
            }
        }

      SVN_ERR(svn_sqlite__step_done(stmt));

      /* ### Copying changelist is OK for a move but what about a copy? */
      SVN_ERR(copy_actual(src_wcroot, src_relpath,
                          dst_wcroot, dst_relpath, scratch_pool));

      if (dst_np_op_depth > 0)
        {
          /* We introduce a not-present node at the parent's op_depth to
             properly start a new op-depth at our own op_depth. This marks
             us as an op_root for commit and allows reverting just this
             operation */

          SVN_ERR(svn_sqlite__get_statement(&stmt, dst_wcroot->sdb,
                                            STMT_INSERT_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "isdsisrtnt",
                                    src_wcroot->wc_id, dst_relpath,
                                    dst_np_op_depth, dst_parent_relpath,
                                    copyfrom_id, copyfrom_relpath,
                                    copyfrom_rev,
                                    presence_map,
                                       svn_wc__db_status_not_present,
                                    /* NULL */
                                    kind_map, kind));

          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      /* Insert incomplete children, if relevant.
         The children are part of the same op and so have the same op_depth.
         (The only time we'd want a different depth is during a recursive
         simple add, but we never insert children here during a simple add.) */
      if (kind == svn_node_dir
          && dst_presence == svn_wc__db_status_normal)
        SVN_ERR(insert_incomplete_children(
                  dst_wcroot->sdb,
                  dst_wcroot->wc_id,
                  dst_relpath,
                  copyfrom_id,
                  copyfrom_relpath,
                  copyfrom_rev,
                  children,
                  dst_op_depth,
                  scratch_pool));
    }
  else
    {
      SVN_ERR(cross_db_copy(src_wcroot, src_relpath, dst_wcroot,
                            dst_relpath, dst_presence, dst_op_depth,
                            dst_np_op_depth, kind,
                            children, copyfrom_id, copyfrom_relpath,
                            copyfrom_rev, scratch_pool));
    }

  SVN_ERR(add_work_items(dst_wcroot->sdb, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton for passing args to op_copy_txn(). */
struct op_copy_baton
{
  svn_wc__db_wcroot_t *src_wcroot;
  const char *src_relpath;

  svn_wc__db_wcroot_t *dst_wcroot;
  const char *dst_relpath;

  const svn_skel_t *work_items;

  svn_boolean_t is_move;
  const char *dst_op_root_relpath;
};

/* Helper for svn_wc__db_op_copy(). */
static svn_error_t *
op_copy_txn(svn_wc__db_wcroot_t *wcroot,
            struct op_copy_baton *ocb,
            apr_pool_t *scratch_pool)
{
  int move_op_depth;

  if (wcroot != ocb->dst_wcroot)
    {
       /* Source and destination databases differ; so also start a lock
          in the destination database, by calling ourself in an extra lock. */

      SVN_WC__DB_WITH_TXN(op_copy_txn(ocb->dst_wcroot, ocb, scratch_pool),
                          ocb->dst_wcroot);

      return SVN_NO_ERROR;
    }

  /* From this point we can assume a lock in the src and dst databases */

  if (ocb->is_move)
    move_op_depth = relpath_depth(ocb->dst_op_root_relpath);
  else
    move_op_depth = 0;

  SVN_ERR(db_op_copy(ocb->src_wcroot, ocb->src_relpath,
                     ocb->dst_wcroot, ocb->dst_relpath,
                     ocb->work_items, move_op_depth, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_copy(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   const char *dst_op_root_abspath,
                   svn_boolean_t is_move,
                   const svn_skel_t *work_items,
                   apr_pool_t *scratch_pool)
{
  struct op_copy_baton ocb = {0};

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_op_root_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&ocb.src_wcroot,
                                                &ocb.src_relpath, db,
                                                src_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(ocb.src_wcroot);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&ocb.dst_wcroot,
                                                &ocb.dst_relpath,
                                                db, dst_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(ocb.dst_wcroot);

  ocb.work_items = work_items;
  ocb.is_move = is_move;
  ocb.dst_op_root_relpath = svn_dirent_skip_ancestor(ocb.dst_wcroot->abspath,
                                                     dst_op_root_abspath);

  /* Call with the sdb in src_wcroot. It might call itself again to
     also obtain a lock in dst_wcroot */
  SVN_WC__DB_WITH_TXN(op_copy_txn(ocb.src_wcroot, &ocb, scratch_pool),
                      ocb.src_wcroot);

  return SVN_NO_ERROR;
}

/* Remove unneeded actual nodes for svn_wc__db_op_copy_layer_internal */
static svn_error_t *
clear_or_remove_actual(svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       int op_depth,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row, shadowed;
  svn_boolean_t keep_conflict = FALSE;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_status_t presence;

      shadowed = (svn_sqlite__column_int(stmt, 0) > op_depth);
      presence = svn_sqlite__column_token(stmt, 3, presence_map);

      if (shadowed && presence == svn_wc__db_status_base_deleted)
        {
          keep_conflict = TRUE;
          SVN_ERR(svn_sqlite__step(&have_row, stmt));

          if (have_row)
            shadowed = (svn_sqlite__column_int(stmt, 0) > op_depth);
          else
            shadowed = FALSE;
        }
    }
  else
    shadowed = FALSE;

  SVN_ERR(svn_sqlite__reset(stmt));
  if (shadowed)
    return SVN_NO_ERROR;

  if (keep_conflict)
    {
      /* We don't want to accidentally remove delete-delete conflicts */
      SVN_ERR(svn_sqlite__get_statement(
                          &stmt, wcroot->sdb,
                          STMT_CLEAR_ACTUAL_NODE_LEAVING_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_EMPTY));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_copy_layer_internal(svn_wc__db_wcroot_t *wcroot,
                                  const char *src_op_relpath,
                                  int src_op_depth,
                                  const char *dst_op_relpath,
                                  svn_skel_t *conflict,
                                  svn_skel_t *work_items,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt, *stmt2;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int dst_op_depth = relpath_depth(dst_op_relpath);
  svn_boolean_t locked;
  svn_error_t *err = NULL;

  SVN_ERR(svn_wc__db_wclock_owns_lock_internal(&locked, wcroot, dst_op_relpath,
                                               FALSE, scratch_pool));

  if (!locked)
    return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                             _("No write-lock in '%s'"),
                             path_for_error_message(wcroot, dst_op_relpath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                    STMT_COPY_NODE_MOVE));

  /* Replace entire subtree at one op-depth. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_LAYER_FOR_REPLACE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isdsd", wcroot->wc_id,
                            src_op_relpath, src_op_depth,
                            dst_op_relpath, dst_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *src_relpath;
      const char *dst_relpath;

      svn_pool_clear(iterpool);

      src_relpath = svn_sqlite__column_text(stmt, 0, iterpool);
      dst_relpath = svn_sqlite__column_text(stmt, 2, iterpool);

      err = svn_sqlite__bindf(stmt2, "isdsds", wcroot->wc_id,
                              src_relpath, src_op_depth,
                              dst_relpath, dst_op_depth,
                              svn_relpath_dirname(dst_relpath, iterpool));
      if (!err)
        err = svn_sqlite__step_done(stmt2);

      /* stmt2 is reset (never modified or by step_done) */

      if (err)
        break;

      /* The node can't be deleted where it is added, so extension of
         an existing shadowing is only interesting 2 levels deep. */
      if (relpath_depth(dst_relpath) > (dst_op_depth+1))
        {
          svn_boolean_t exists = !svn_sqlite__column_is_null(stmt, 3);

          if (exists)
            {
              svn_wc__db_status_t presence;

              presence = svn_sqlite__column_token(stmt, 3, presence_map);

              if (presence != svn_wc__db_status_normal)
                exists = FALSE;
            }

          if (!exists)
            {
              svn_node_kind_t kind = svn_sqlite__column_token(stmt, 1, kind_map);

              err = db_extend_parent_delete(wcroot, dst_relpath,
                                            kind, dst_op_depth, iterpool);

              if (err)
                break;
            }
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

  /* And now remove the records that are no longer needed */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NO_LONGER_MOVED_RV));
  SVN_ERR(svn_sqlite__bindf(stmt, "isdsd", wcroot->wc_id,
                            dst_op_relpath, dst_op_depth,
                            src_op_relpath, src_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *dst_relpath;
      svn_wc__db_status_t shadowed_presence;

      svn_pool_clear(iterpool);

      dst_relpath = svn_sqlite__column_text(stmt, 0, iterpool);

      if (!svn_sqlite__column_is_null(stmt, 2))
        shadowed_presence = svn_sqlite__column_token(stmt, 2, presence_map);
      else
        shadowed_presence = svn_wc__db_status_not_present;

      if (shadowed_presence != svn_wc__db_status_normal
          && shadowed_presence != svn_wc__db_status_incomplete)
        {
          err = svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                            STMT_DELETE_NODE);
        }
      else
        {
          err =svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                         STMT_REPLACE_WITH_BASE_DELETED);
        }

      if (!err)
        err = svn_sqlite__bindf(stmt2, "isd", wcroot->wc_id, dst_relpath,
                                             dst_op_depth);

      if (!err)
        err = svn_sqlite__step_done(stmt2);

      /* stmt2 is reset (never modified or by step_done) */
      if (err)
        break;

      /* Delete ACTUAL information about this node that we just deleted */
      err = clear_or_remove_actual(wcroot, dst_relpath, dst_op_depth,
                                   scratch_pool);

      if (err)
        break;

      /* Retract base-delete for the node itself */
      err = db_retract_parent_delete(wcroot, dst_relpath, dst_op_depth,
                                     scratch_pool);

      if (err)
        break;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  if (conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, dst_op_relpath /* ## */,
                                              conflict, scratch_pool));

  return SVN_NO_ERROR;
}

/* The txn body of svn_wc__db_op_handle_move_back */
static svn_error_t *
handle_move_back(svn_boolean_t *moved_back,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 const char *moved_from_relpath,
                 const svn_skel_t *work_items,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_status_t status;
  svn_boolean_t op_root;
  svn_boolean_t have_more_work;
  int from_op_depth = 0;
  svn_boolean_t have_row;
  svn_boolean_t different = FALSE;

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  SVN_ERR(svn_wc__db_read_info_internal(&status, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                        &op_root, NULL, NULL, NULL,
                                        &have_more_work, NULL,
                                        wcroot, local_relpath,
                                        scratch_pool, scratch_pool));

  if (status != svn_wc__db_status_added || !op_root)
    return SVN_NO_ERROR;

  /* We have two cases here: BASE-move-back and WORKING-move-back */
  if (have_more_work)
    SVN_ERR(op_depth_of(&from_op_depth, wcroot,
                        svn_relpath_dirname(local_relpath, scratch_pool)));
  else
    from_op_depth = 0;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_BACK));

  SVN_ERR(svn_sqlite__bindf(stmt, "isdd", wcroot->wc_id,
                                          local_relpath,
                                          from_op_depth,
                                          relpath_depth(local_relpath)));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  SVN_ERR_ASSERT(have_row); /* We checked that the node is an op-root */

  {
    svn_boolean_t moved_here = svn_sqlite__column_boolean(stmt, 9);
    const char *moved_to = svn_sqlite__column_text(stmt, 10, NULL);

    if (!moved_here
        || !moved_to
        || strcmp(moved_to, moved_from_relpath))
      {
        different = TRUE;
        have_row = FALSE;
      }
  }

  while (have_row)
    {
      svn_wc__db_status_t upper_status;
      svn_wc__db_status_t lower_status;

      upper_status = svn_sqlite__column_token(stmt, 1, presence_map);

      if (svn_sqlite__column_is_null(stmt, 5))
        {
          /* No lower layer replaced. */
          if (upper_status != svn_wc__db_status_not_present)
            {
              different = TRUE;
              break;
            }
          continue;
        }

      lower_status = svn_sqlite__column_token(stmt, 5, presence_map);

      if (upper_status != lower_status)
        {
          different = TRUE;
          break;
        }

      if (upper_status == svn_wc__db_status_not_present
          || upper_status == svn_wc__db_status_excluded)
        {
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          continue; /* Nothing to check */
        }
      else if (upper_status != svn_wc__db_status_normal)
        {
          /* Not a normal move. Mixed revision move? */
          different = TRUE;
          break;
        }

      {
        const char *upper_repos_relpath;
        const char *lower_repos_relpath;

        upper_repos_relpath = svn_sqlite__column_text(stmt, 3, NULL);
        lower_repos_relpath = svn_sqlite__column_text(stmt, 7, NULL);

        if (! upper_repos_relpath
            || strcmp(upper_repos_relpath, lower_repos_relpath))
          {
            different = TRUE;
            break;
          }
      }

      {
        svn_revnum_t upper_rev;
        svn_revnum_t lower_rev;

        upper_rev = svn_sqlite__column_revnum(stmt, 4);
        lower_rev = svn_sqlite__column_revnum(stmt, 8);

        if (upper_rev != lower_rev)
          {
            different = TRUE;
            break;
          }
      }

      {
        apr_int64_t upper_repos_id;
        apr_int64_t lower_repos_id;

        upper_repos_id = svn_sqlite__column_int64(stmt, 2);
        lower_repos_id = svn_sqlite__column_int64(stmt, 6);

        if (upper_repos_id != lower_repos_id)
          {
            different = TRUE;
            break;
          }
      }

      /* Check moved_here? */

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (! different)
    {
      /* Ok, we can now safely remove this complete move, because we
         determined that it 100% matches the layer below it. */

      /* ### We could copy the recorded timestamps from the higher to the
             lower layer in an attempt to improve status performance, but
             generally these values should be the same anyway as it was
             a no-op move. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_WORKING_OP_DEPTH));

      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                             local_relpath,
                                             relpath_depth(local_relpath)));

      SVN_ERR(svn_sqlite__step_done(stmt));

      if (moved_back)
        *moved_back = TRUE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_handle_move_back(svn_boolean_t *moved_back,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               const char *moved_from_abspath,
                               const svn_skel_t *work_items,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *moved_from_relpath;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (moved_back)
    *moved_back = FALSE;

  moved_from_relpath = svn_dirent_skip_ancestor(wcroot->abspath,
                                                moved_from_abspath);

  if (! local_relpath[0]
      || !moved_from_relpath)
    {
       /* WC-Roots can't be moved */
      SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));
      return SVN_NO_ERROR;
    }

  SVN_WC__DB_WITH_TXN(handle_move_back(moved_back, wcroot, local_relpath,
                                       moved_from_relpath, work_items,
                                       scratch_pool),
                      wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


/* The recursive implementation of svn_wc__db_op_copy_shadowed_layer.
 *
 * A non-zero MOVE_OP_DEPTH implies that the copy operation is part of
 * a move, and indicates the op-depth of the move destination op-root. */
static svn_error_t *
db_op_copy_shadowed_layer(svn_wc__db_wcroot_t *src_wcroot,
                          const char *src_relpath,
                          int src_op_depth,
                          svn_wc__db_wcroot_t *dst_wcroot,
                          const char *dst_relpath,
                          int dst_op_depth,
                          int del_op_depth,
                          apr_int64_t repos_id,
                          const char *repos_relpath,
                          svn_revnum_t revision,
                          int move_op_depth,
                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *children;
  apr_pool_t *iterpool;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_revnum_t node_revision;
  const char *node_repos_relpath;
  apr_int64_t node_repos_id;
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_status_t dst_presence;
  int i;

  {
    svn_error_t *err;
    err = svn_wc__db_depth_get_info(&status, &kind, &node_revision,
                                    &node_repos_relpath, &node_repos_id,
                                    NULL, NULL, NULL, NULL, NULL, NULL,
                                    NULL, NULL,
                                    src_wcroot, src_relpath, src_op_depth,
                                    scratch_pool, scratch_pool);

    if (err)
      {
        if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
          return svn_error_trace(err);

        svn_error_clear(err);
        return SVN_NO_ERROR; /* There is no shadowed node at src_op_depth */
      }
  }

  if (src_op_depth == 0)
    {
      /* If the node is switched or has a different revision then its parent
         we shouldn't copy it. (We can't as we would have to insert it at
         an unshadowed depth) */
      if (status == svn_wc__db_status_not_present
          || status == svn_wc__db_status_excluded
          || status == svn_wc__db_status_server_excluded
          || node_revision != revision
          || node_repos_id != repos_id
          || strcmp(node_repos_relpath, repos_relpath))
        {
          /* Add a not-present node in the destination wcroot */
          struct insert_working_baton_t iwb;
          const char *repos_root_url;
          const char *repos_uuid;

          SVN_ERR(svn_wc__db_fetch_repos_info(&repos_root_url, &repos_uuid,
                                              src_wcroot, node_repos_id,
                                              scratch_pool));

          SVN_ERR(create_repos_id(&node_repos_id, repos_root_url, repos_uuid,
                                  dst_wcroot->sdb, scratch_pool));

          blank_iwb(&iwb);

          iwb.op_depth = dst_op_depth;
          if (status != svn_wc__db_status_excluded)
            iwb.presence = svn_wc__db_status_not_present;
          else
            iwb.presence = svn_wc__db_status_excluded;

          iwb.kind = kind;

          iwb.original_repos_id = node_repos_id;
          iwb.original_revnum = node_revision;
          iwb.original_repos_relpath = node_repos_relpath;

          SVN_ERR(insert_working_node(&iwb, dst_wcroot, dst_relpath,
                                      scratch_pool));

          return SVN_NO_ERROR;
        }
    }

  iterpool = svn_pool_create(scratch_pool);

  switch (status)
    {
    case svn_wc__db_status_normal:
    case svn_wc__db_status_added:
    case svn_wc__db_status_moved_here:
    case svn_wc__db_status_copied:
      dst_presence = svn_wc__db_status_normal;
      break;
    case svn_wc__db_status_deleted:
    case svn_wc__db_status_not_present:
      dst_presence = svn_wc__db_status_not_present;
      break;
    case svn_wc__db_status_excluded:
      dst_presence = svn_wc__db_status_excluded;
      break;
    case svn_wc__db_status_server_excluded:
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot copy '%s' excluded by server"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    default:
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot handle status of '%s'"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    }

  if (dst_presence == svn_wc__db_status_normal
      && src_wcroot == dst_wcroot) /* ### Remove limitation */
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                             STMT_INSERT_WORKING_NODE_COPY_FROM_DEPTH));

      SVN_ERR(svn_sqlite__bindf(stmt, "issdstd",
                        src_wcroot->wc_id, src_relpath,
                        dst_relpath,
                        dst_op_depth,
                        svn_relpath_dirname(dst_relpath, iterpool),
                        presence_map, dst_presence,
                        src_op_depth));

      /* moved_here */
      if (dst_op_depth == move_op_depth)
        SVN_ERR(svn_sqlite__bind_int(stmt, 8, TRUE));

      SVN_ERR(svn_sqlite__step_done(stmt));

      {
        /* And mark it deleted to allow proper shadowing */
        struct insert_working_baton_t iwb;

        blank_iwb(&iwb);

        iwb.op_depth = del_op_depth;
        iwb.presence = svn_wc__db_status_base_deleted;

        iwb.kind = kind;

        SVN_ERR(insert_working_node(&iwb, dst_wcroot, dst_relpath,
                                    scratch_pool));
      }
    }
  else
    {
      struct insert_working_baton_t iwb;
      if (dst_presence == svn_wc__db_status_normal) /* Fallback for multi-db */
        dst_presence = svn_wc__db_status_not_present;

      /* And mark it deleted to allow proper shadowing */

      blank_iwb(&iwb);

      iwb.op_depth = dst_op_depth;
      iwb.presence = dst_presence;
      iwb.kind = kind;

      SVN_ERR(insert_working_node(&iwb, dst_wcroot, dst_relpath,
                                    scratch_pool));
    }

  if (dst_presence == svn_wc__db_status_not_present)
    {
      /* Don't create descendants of a not present node! */

      /* This code is currently still triggered by copying deleted nodes
         between separate working copies. See ### comment above. */

      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  SVN_ERR(gather_children(&children, src_wcroot, src_relpath,
                          STMT_SELECT_OP_DEPTH_CHILDREN, src_op_depth,
                          scratch_pool, iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *child_src_relpath;
      const char *child_dst_relpath;
      const char *child_repos_relpath = NULL;

      svn_pool_clear(iterpool);
      child_src_relpath = svn_relpath_join(src_relpath, name, iterpool);
      child_dst_relpath = svn_relpath_join(dst_relpath, name, iterpool);

      if (repos_relpath)
        child_repos_relpath = svn_relpath_join(repos_relpath, name, iterpool);

      SVN_ERR(db_op_copy_shadowed_layer(
                         src_wcroot, child_src_relpath, src_op_depth,
                         dst_wcroot, child_dst_relpath, dst_op_depth,
                         del_op_depth,
                         repos_id, child_repos_relpath, revision,
                         move_op_depth, scratch_pool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_op_copy_shadowed_layer(). */
static svn_error_t *
op_copy_shadowed_layer_txn(svn_wc__db_wcroot_t *wcroot,
                           struct op_copy_baton *ocb,
                           apr_pool_t *scratch_pool)
{
  const char *src_parent_relpath;
  const char *dst_parent_relpath;
  int src_op_depth;
  int dst_op_depth;
  int del_op_depth;
  const char *repos_relpath = NULL;
  apr_int64_t repos_id = INVALID_REPOS_ID;
  svn_revnum_t revision = SVN_INVALID_REVNUM;

  if (wcroot != ocb->dst_wcroot)
    {
      /* Source and destination databases differ; so also start a lock
         in the destination database, by calling ourself in an extra lock. */

      SVN_WC__DB_WITH_TXN(op_copy_shadowed_layer_txn(ocb->dst_wcroot, ocb,
                                                     scratch_pool),
                          ocb->dst_wcroot);

      return SVN_NO_ERROR;
    }

  /* From this point we can assume a lock in the src and dst databases */


  /* src_relpath and dst_relpath can't be wcroot as we need their parents */
  SVN_ERR_ASSERT(*ocb->src_relpath && *ocb->dst_relpath);

  src_parent_relpath = svn_relpath_dirname(ocb->src_relpath, scratch_pool);
  dst_parent_relpath = svn_relpath_dirname(ocb->dst_relpath, scratch_pool);

  /* src_parent must be status normal or added; get its op-depth */
  SVN_ERR(op_depth_of(&src_op_depth, ocb->src_wcroot, src_parent_relpath));

  /* dst_parent must be status added; get its op-depth */
  SVN_ERR(op_depth_of(&dst_op_depth, ocb->dst_wcroot, dst_parent_relpath));

  del_op_depth = relpath_depth(ocb->dst_relpath);

  /* Get some information from the parent */
  SVN_ERR(svn_wc__db_depth_get_info(NULL, NULL, &revision, &repos_relpath,
                                    &repos_id, NULL, NULL, NULL, NULL, NULL,
                                    NULL, NULL, NULL,
                                    ocb->src_wcroot,
                                    src_parent_relpath, src_op_depth,
                                    scratch_pool, scratch_pool));

  if (repos_relpath == NULL)
    {
      /* The node is a local addition and has no shadowed information */
      return SVN_NO_ERROR;
    }

  /* And calculate the child repos relpath */
  repos_relpath = svn_relpath_join(repos_relpath,
                                   svn_relpath_basename(ocb->src_relpath,
                                                        NULL),
                                   scratch_pool);

  SVN_ERR(db_op_copy_shadowed_layer(
                        ocb->src_wcroot, ocb->src_relpath, src_op_depth,
                        ocb->dst_wcroot, ocb->dst_relpath, dst_op_depth,
                        del_op_depth,
                        repos_id, repos_relpath, revision,
                        (ocb->is_move ? dst_op_depth : 0),
                        scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_copy_shadowed_layer(svn_wc__db_t *db,
                                  const char *src_abspath,
                                  const char *dst_abspath,
                                  svn_boolean_t is_move,
                                  apr_pool_t *scratch_pool)
{
  struct op_copy_baton ocb = {0};

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&ocb.src_wcroot,
                                                &ocb.src_relpath, db,
                                                src_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(ocb.src_wcroot);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&ocb.dst_wcroot,
                                                &ocb.dst_relpath,
                                                db, dst_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(ocb.dst_wcroot);

  ocb.is_move = is_move;
  ocb.dst_op_root_relpath = NULL; /* not used by op_copy_shadowed_layer_txn */

  ocb.work_items = NULL;

  /* Call with the sdb in src_wcroot. It might call itself again to
     also obtain a lock in dst_wcroot */
  SVN_WC__DB_WITH_TXN(op_copy_shadowed_layer_txn(ocb.src_wcroot, &ocb,
                                                 scratch_pool),
                      ocb.src_wcroot);

  return SVN_NO_ERROR;
}


/* If there are any server-excluded base nodes then the copy must fail
   as it's not possible to commit such a copy.
   Return an error if there are any server-excluded nodes. */
static svn_error_t *
catch_copy_of_server_excluded(svn_wc__db_wcroot_t *wcroot,
                              const char *local_relpath,
                              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *server_excluded_relpath;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_HAS_SERVER_EXCLUDED_DESCENDANTS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    server_excluded_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    return svn_error_createf(SVN_ERR_AUTHZ_UNREADABLE, NULL,
                             _("Cannot copy '%s' excluded by server"),
                             path_for_error_message(wcroot,
                                                    server_excluded_relpath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_copy_dir(svn_wc__db_t *db,
                       const char *local_abspath,
                       const apr_hash_t *props,
                       svn_revnum_t changed_rev,
                       apr_time_t changed_date,
                       const char *changed_author,
                       const char *original_repos_relpath,
                       const char *original_root_url,
                       const char *original_uuid,
                       svn_revnum_t original_revision,
                       const apr_array_header_t *children,
                       svn_depth_t depth,
                       svn_boolean_t is_move,
                       const svn_skel_t *conflict,
                       const svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;
  int parent_op_depth;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  /* ### any assertions for ORIGINAL_* ?  */
#if 0
  SVN_ERR_ASSERT(children != NULL);
#endif

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_dir;

  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;

      iwb.props = props;
      iwb.changed_rev = changed_rev;
      iwb.changed_date = changed_date;
      iwb.changed_author = changed_author;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, &iwb.not_present_op_depth,
                            &parent_op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.children = children;
  iwb.depth = depth;
  iwb.moved_here = is_move && (parent_op_depth == 0 ||
                               iwb.op_depth == parent_op_depth);

  iwb.work_items = work_items;
  iwb.conflict = conflict;

  SVN_WC__DB_WITH_TXN(
                insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
                wcroot);
  SVN_ERR(flush_entries(wcroot, local_abspath, depth, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_copy_file(svn_wc__db_t *db,
                        const char *local_abspath,
                        const apr_hash_t *props,
                        svn_revnum_t changed_rev,
                        apr_time_t changed_date,
                        const char *changed_author,
                        const char *original_repos_relpath,
                        const char *original_root_url,
                        const char *original_uuid,
                        svn_revnum_t original_revision,
                        const svn_checksum_t *checksum,
                        svn_boolean_t update_actual_props,
                        const apr_hash_t *new_actual_props,
                        svn_boolean_t is_move,
                        const svn_skel_t *conflict,
                        const svn_skel_t *work_items,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;
  int parent_op_depth;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  SVN_ERR_ASSERT((! original_repos_relpath && ! original_root_url
                  && ! original_uuid && ! checksum
                  && original_revision == SVN_INVALID_REVNUM)
                 || (original_repos_relpath && original_root_url
                     && original_uuid && checksum
                     && original_revision != SVN_INVALID_REVNUM));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_file;

  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;

      iwb.props = props;
      iwb.changed_rev = changed_rev;
      iwb.changed_date = changed_date;
      iwb.changed_author = changed_author;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, &iwb.not_present_op_depth,
                            &parent_op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.checksum = checksum;
  iwb.moved_here = is_move && (parent_op_depth == 0 ||
                               iwb.op_depth == parent_op_depth);

  if (update_actual_props)
    {
      iwb.update_actual_props = update_actual_props;
      iwb.new_actual_props = new_actual_props;
    }

  iwb.work_items = work_items;
  iwb.conflict = conflict;

  SVN_WC__DB_WITH_TXN(
          insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
          wcroot);
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_copy_symlink(svn_wc__db_t *db,
                           const char *local_abspath,
                           const apr_hash_t *props,
                           svn_revnum_t changed_rev,
                           apr_time_t changed_date,
                           const char *changed_author,
                           const char *original_repos_relpath,
                           const char *original_root_url,
                           const char *original_uuid,
                           svn_revnum_t original_revision,
                           const char *target,
                           svn_boolean_t is_move,
                           const svn_skel_t *conflict,
                           const svn_skel_t *work_items,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;
  int parent_op_depth;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  /* ### any assertions for ORIGINAL_* ?  */
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_symlink;


  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;

      iwb.props = props;
      iwb.changed_rev = changed_rev;
      iwb.changed_date = changed_date;
      iwb.changed_author = changed_author;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, &iwb.not_present_op_depth,
                            &parent_op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.target = target;
  iwb.moved_here = is_move && (parent_op_depth == 0 ||
                               iwb.op_depth == parent_op_depth);

  iwb.work_items = work_items;
  iwb.conflict = conflict;

  SVN_WC__DB_WITH_TXN(
            insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
            wcroot);
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_directory(svn_wc__db_t *db,
                            const char *local_abspath,
                            const apr_hash_t *props,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *dir_abspath;
  const char *name;
  insert_working_baton_t iwb;

  /* Resolve wcroot via parent directory to avoid obstruction handling */
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  svn_dirent_split(&dir_abspath, &name, local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  local_relpath = svn_relpath_join(local_relpath, name, scratch_pool);
  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_dir;
  iwb.op_depth = relpath_depth(local_relpath);
  if (props && apr_hash_count((apr_hash_t *)props))
    {
      iwb.update_actual_props = TRUE;
      iwb.new_actual_props = props;
    }

  iwb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
            wcroot);
  /* Use depth infinity to make sure we have no invalid cached information
   * about children of this dir. */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       const apr_hash_t *props,
                       const svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;
  const char *dir_abspath;
  const char *name;

  /* Resolve wcroot via parent directory to avoid obstruction handling */
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  svn_dirent_split(&dir_abspath, &name, local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  local_relpath = svn_relpath_join(local_relpath, name, scratch_pool);
  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_file;
  iwb.op_depth = relpath_depth(local_relpath);
  if (props && apr_hash_count((apr_hash_t *)props))
    {
      iwb.update_actual_props = TRUE;
      iwb.new_actual_props = props;
    }

  iwb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
            wcroot);
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          const char *target,
                          const apr_hash_t *props,
                          const svn_skel_t *work_items,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;
  const char *dir_abspath;
  const char *name;

  /* Resolve wcroot via parent directory to avoid obstruction handling */
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(target != NULL);

  svn_dirent_split(&dir_abspath, &name, local_abspath, scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              dir_abspath, scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  local_relpath = svn_relpath_join(local_relpath, name, scratch_pool);
  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_node_symlink;
  iwb.op_depth = relpath_depth(local_relpath);
  if (props && apr_hash_count((apr_hash_t *)props))
    {
      iwb.update_actual_props = TRUE;
      iwb.new_actual_props = props;
    }

  iwb.target = target;

  iwb.work_items = work_items;

  SVN_WC__DB_WITH_TXN(
            insert_working_node(&iwb, wcroot, local_relpath, scratch_pool),
            wcroot);
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}

/* Record RECORDED_SIZE and RECORDED_TIME into top layer in NODES */
static svn_error_t *
db_record_fileinfo(svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_int64_t recorded_size,
                   apr_int64_t recorded_time,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_NODE_FILEINFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isii", wcroot->wc_id, local_relpath,
                            recorded_size, recorded_time));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  SVN_ERR_ASSERT(affected_rows == 1);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_record_fileinfo(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  svn_filesize_t recorded_size,
                                  apr_time_t recorded_time,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(db_record_fileinfo(wcroot, local_relpath,
                             recorded_size, recorded_time, scratch_pool));

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


/* Set the ACTUAL_NODE properties column for (WC_ID, LOCAL_RELPATH) to
 * PROPS.
 *
 * Note: PROPS=NULL means the actual props are the same as the pristine
 * props; to indicate no properties when the pristine has some props,
 * PROPS must be an empty hash. */
static svn_error_t *
set_actual_props(svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 apr_hash_t *props,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 1 || !props)
    {
      /* Perhaps the entire ACTUAL record is unneeded now? */
      if (!props && affected_rows)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_DELETE_ACTUAL_EMPTY));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }

      return SVN_NO_ERROR; /* We are done */
    }

  /* We have to insert a row in ACTUAL */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_INSERT_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  if (*local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 3,
                                  svn_relpath_dirname(local_relpath,
                                                      scratch_pool)));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 4, props, scratch_pool));
  return svn_error_trace(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_wc__db_op_set_props_internal(svn_wc__db_wcroot_t *wcroot,
                                 const char *local_relpath,
                                 apr_hash_t *props,
                                 svn_boolean_t clear_recorded_info,
                                 apr_pool_t *scratch_pool)
{
  SVN_ERR(set_actual_props(wcroot, local_relpath, props, scratch_pool));

  if (clear_recorded_info)
    {
      SVN_ERR(db_record_fileinfo(wcroot, local_relpath,
                                 SVN_INVALID_FILESIZE, 0,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_op_set_props().

   Set the 'properties' column in the 'ACTUAL_NODE' table to BATON->props.
   Create an entry in the ACTUAL table for the node if it does not yet
   have one.
   To specify no properties, BATON->props must be an empty hash, not NULL.
   BATON is of type 'struct set_props_baton_t'.
*/
static svn_error_t *
set_props_txn(svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_hash_t *props,
              svn_boolean_t clear_recorded_info,
              const svn_skel_t *conflict,
              const svn_skel_t *work_items,
              apr_pool_t *scratch_pool)
{
  apr_hash_t *pristine_props;

  /* Check if the props are modified. If no changes, then wipe out the
     ACTUAL props.  PRISTINE_PROPS==NULL means that any
     ACTUAL props are okay as provided, so go ahead and set them.  */
  SVN_ERR(db_read_pristine_props(&pristine_props, wcroot, local_relpath, FALSE,
                                 scratch_pool, scratch_pool));
  if (props && pristine_props)
    {
      apr_array_header_t *prop_diffs;

      SVN_ERR(svn_prop_diffs(&prop_diffs, props, pristine_props,
                             scratch_pool));
      if (prop_diffs->nelts == 0)
        props = NULL;
    }

  SVN_ERR(svn_wc__db_op_set_props_internal(wcroot, local_relpath, props,
                                           clear_recorded_info, scratch_pool));

  /* And finally.  */
  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));
  if (conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              conflict, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_hash_t *props,
                        svn_boolean_t clear_recorded_info,
                        const svn_skel_t *conflict,
                        const svn_skel_t *work_items,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(set_props_txn(wcroot, local_relpath, props,
                                    clear_recorded_info, conflict, work_items,
                                    scratch_pool),
                      wcroot);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_modified(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}

/* */
static svn_error_t *
populate_targets_tree(svn_wc__db_wcroot_t *wcroot,
                      const char *local_relpath,
                      svn_depth_t depth,
                      const apr_array_header_t *changelist_filter,
                      apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows = 0;
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_TARGETS_LIST));

  if (changelist_filter && changelist_filter->nelts > 0)
    {
      /* Iterate over the changelists, adding the nodes which match.
         Common case: we only have one changelist, so this only
         happens once. */
      int i;
      int stmt_idx;

      switch (depth)
        {
          case svn_depth_empty:
            stmt_idx = STMT_INSERT_TARGET_WITH_CHANGELIST;
            break;

          case svn_depth_files:
            stmt_idx = STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_FILES;
            break;

          case svn_depth_immediates:
            stmt_idx = STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_IMMEDIATES;
            break;

          case svn_depth_infinity:
            stmt_idx = STMT_INSERT_TARGET_WITH_CHANGELIST_DEPTH_INFINITY;
            break;

          default:
            /* We don't know how to handle unknown or exclude. */
            SVN_ERR_MALFUNCTION();
            break;
        }

      for (i = 0; i < changelist_filter->nelts; i++)
        {
          int sub_affected;
          const char *changelist = APR_ARRAY_IDX(changelist_filter, i,
                                                 const char *);

          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_TARGET_WITH_CHANGELIST));
          SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id,
                                    local_relpath, changelist));
          SVN_ERR(svn_sqlite__update(&sub_affected, stmt));

          /* If the root is matched by the changelist, we don't have to match
             the children. As that tells us the root is a file */
          if (!sub_affected && depth > svn_depth_empty)
            {
              SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, stmt_idx));
              SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id,
                                        local_relpath, changelist));
              SVN_ERR(svn_sqlite__update(&sub_affected, stmt));
            }

          affected_rows += sub_affected;
        }
    }
  else /* No changelist filtering */
    {
      int stmt_idx;
      int sub_affected;

      switch (depth)
        {
          case svn_depth_empty:
            stmt_idx = STMT_INSERT_TARGET;
            break;

          case svn_depth_files:
            stmt_idx = STMT_INSERT_TARGET_DEPTH_FILES;
            break;

          case svn_depth_immediates:
            stmt_idx = STMT_INSERT_TARGET_DEPTH_IMMEDIATES;
            break;

          case svn_depth_infinity:
            stmt_idx = STMT_INSERT_TARGET_DEPTH_INFINITY;
            break;

          default:
            /* We don't know how to handle unknown or exclude. */
            SVN_ERR_MALFUNCTION();
            break;
        }

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_TARGET));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__update(&sub_affected, stmt));
      affected_rows += sub_affected;

      if (depth > svn_depth_empty)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, stmt_idx));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__update(&sub_affected, stmt));
          affected_rows += sub_affected;
        }
    }

  /* Does the target exist? */
  if (affected_rows == 0)
    {
      svn_boolean_t exists;
      SVN_ERR(does_node_exist(&exists, wcroot, local_relpath));

      if (!exists)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));
    }

  return SVN_NO_ERROR;
}


#if 0
static svn_error_t *
dump_targets(svn_wc__db_wcroot_t *wcroot,
             apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_TARGETS));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *target = svn_sqlite__column_text(stmt, 0, NULL);
      SVN_DBG(("Target: '%s'\n", target));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}
#endif


struct set_changelist_baton_t
{
  const char *new_changelist;
  const apr_array_header_t *changelist_filter;
  svn_depth_t depth;
};


/* The main part of svn_wc__db_op_set_changelist().
 *
 * Implements svn_wc__db_txn_callback_t. */
static svn_error_t *
set_changelist_txn(void *baton,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *scratch_pool)
{
  struct set_changelist_baton_t *scb = baton;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(populate_targets_tree(wcroot, local_relpath, scb->depth,
                                scb->changelist_filter, scratch_pool));

  /* Ensure we have actual nodes for our targets. */
  if (scb->new_changelist)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_ACTUAL_EMPTIES_FILES));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Now create our notification table. */
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_CHANGELIST_LIST));
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_CHANGELIST_TRIGGER));

  /* Update our changelists. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_ACTUAL_CHANGELISTS));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath,
                            scb->new_changelist));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (scb->new_changelist)
    {
      /* We have to notify that we skipped directories, so do that now. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_MARK_SKIPPED_CHANGELIST_DIRS));
      SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath,
                                scb->new_changelist));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* We may have left empty ACTUAL nodes, so remove them.  This is only a
     potential problem if we removed changelists. */
  if (!scb->new_changelist)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_EMPTIES));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}


/* Send notifications for svn_wc__db_op_set_changelist().
 *
 * Implements work_callback_t. */
static svn_error_t *
do_changelist_notify(void *baton,
                     svn_wc__db_wcroot_t *wcroot,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_CHANGELIST_LIST));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
    {
      /* ### wc_id is column 0. use it one day...  */
      const char *notify_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      svn_wc_notify_action_t action = svn_sqlite__column_int(stmt, 2);
      svn_wc_notify_t *notify;
      const char *notify_abspath;

      svn_pool_clear(iterpool);

      if (cancel_func)
        {
          svn_error_t *err = cancel_func(cancel_baton);

          if (err)
            return svn_error_trace(svn_error_compose_create(
                                                    err,
                                                    svn_sqlite__reset(stmt)));
        }

      notify_abspath = svn_dirent_join(wcroot->abspath, notify_relpath,
                                       iterpool);
      notify = svn_wc_create_notify(notify_abspath, action, iterpool);
      notify->changelist_name = svn_sqlite__column_text(stmt, 3, NULL);
      notify_func(notify_baton, notify, iterpool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  svn_pool_destroy(iterpool);

  return svn_error_trace(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_op_set_changelist(svn_wc__db_t *db,
                             const char *local_abspath,
                             const char *new_changelist,
                             const apr_array_header_t *changelist_filter,
                             svn_depth_t depth,
                             svn_wc_notify_func2_t notify_func,
                             void *notify_baton,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  struct set_changelist_baton_t scb;

  scb.new_changelist = new_changelist;
  scb.changelist_filter = changelist_filter;
  scb.depth = depth;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* Flush the entries before we do the work. Even if no work is performed,
     the flush isn't a problem. */
  SVN_ERR(flush_entries(wcroot, local_abspath, depth, scratch_pool));

  /* Perform the set-changelist operation (transactionally), perform any
     notifications necessary, and then clean out our temporary tables.  */
  return svn_error_trace(with_finalization(wcroot, local_relpath,
                                           set_changelist_txn, &scb,
                                           do_changelist_notify, NULL,
                                           cancel_func, cancel_baton,
                                           notify_func, notify_baton,
                                           STMT_FINALIZE_CHANGELIST,
                                           scratch_pool));
}

/* Implementation of svn_wc__db_op_mark_conflict() */
svn_error_t *
svn_wc__db_mark_conflict_internal(svn_wc__db_wcroot_t *wcroot,
                                  const char *local_relpath,
                                  const svn_skel_t *conflict_skel,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;
  svn_boolean_t is_complete;

  SVN_ERR(svn_wc__conflict_skel_is_complete(&is_complete, conflict_skel));
  SVN_ERR_ASSERT(is_complete);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (got_row)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_ACTUAL_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_ACTUAL_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      if (*local_relpath != '\0')
        SVN_ERR(svn_sqlite__bind_text(stmt, 4,
                                      svn_relpath_dirname(local_relpath,
                                                          scratch_pool)));
    }

  {
    svn_stringbuf_t *sb = svn_skel__unparse(conflict_skel, scratch_pool);

    SVN_ERR(svn_sqlite__bind_blob(stmt, 3, sb->data, sb->len));
  }

  SVN_ERR(svn_sqlite__update(NULL, stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_mark_conflict(svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_skel_t *conflict_skel,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                            conflict_skel, scratch_pool));

  /* ### Should be handled in the same transaction as setting the conflict */
  if (work_items)
    SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;

}

/* The body of svn_wc__db_op_mark_resolved().
 */
svn_error_t *
svn_wc__db_op_mark_resolved_internal(svn_wc__db_wcroot_t *wcroot,
                                     const char *local_relpath,
                                     svn_wc__db_t *db,
                                     svn_boolean_t resolved_text,
                                     svn_boolean_t resolved_props,
                                     svn_boolean_t resolved_tree,
                                     const svn_skel_t *work_items,
                                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int total_affected_rows = 0;
  svn_boolean_t resolved_all;
  apr_size_t conflict_len;
  const void *conflict_data;
  svn_skel_t *conflicts;

  /* Check if we have a conflict in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (! have_row)
    {
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_NODE_INFO));

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      SVN_ERR(svn_sqlite__reset(stmt));

      if (have_row)
        return SVN_NO_ERROR;

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                                   path_for_error_message(wcroot,
                                                          local_relpath,
                                                          scratch_pool));
    }

  conflict_data = svn_sqlite__column_blob(stmt, 2, &conflict_len,
                                          scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));
  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  if (!conflict_data)
    return SVN_NO_ERROR;

  conflicts = svn_skel__parse(conflict_data, conflict_len, scratch_pool);
  

  SVN_ERR(svn_wc__conflict_skel_resolve(&resolved_all, conflicts,
                                        db, wcroot->abspath,
                                        resolved_text,
                                        resolved_props ? "" : NULL,
                                        resolved_tree,
                                        scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_ACTUAL_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  if (! resolved_all)
    {
      svn_stringbuf_t *sb = svn_skel__unparse(conflicts, scratch_pool);

      SVN_ERR(svn_sqlite__bind_blob(stmt, 3, sb->data, sb->len));
    }

  SVN_ERR(svn_sqlite__update(&total_affected_rows, stmt));

  /* Now, remove the actual node if it doesn't have any more useful
     information.  We only need to do this if we've remove data ourselves. */
  if (total_affected_rows > 0)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_EMPTY));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_mark_resolved(svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t resolved_text,
                            svn_boolean_t resolved_props,
                            svn_boolean_t resolved_tree,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    svn_wc__db_op_mark_resolved_internal(
                        wcroot, local_relpath, db,
                        resolved_text, resolved_props, resolved_tree,
                        work_items, scratch_pool),
    wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));
  return SVN_NO_ERROR;
}

/* Clear moved-to information at the delete-half of the move which moved
 * MOVED_TO_RELPATH here. This transforms the delete part of the move into a
 * normal delete.
 *
 * Note that the moved-to location is always an op-root, while this is not the
 * case for a moved-from location.
 */
static svn_error_t *
clear_moved_to(svn_wc__db_wcroot_t *wcroot,
               const char *moved_to_relpath,
               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  const char *moved_from_relpath;
  int moved_from_op_depth;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_FROM_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, moved_to_relpath));
  SVN_ERR(svn_sqlite__step_row(stmt));

  moved_from_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
  moved_from_op_depth = svn_sqlite__column_int(stmt, 1);
  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_MOVED_TO_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                            moved_from_relpath, moved_from_op_depth));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  return SVN_NO_ERROR;
}

/* Helper function for op_revert_txn. Raises move tree conflicts on
   descendants to ensure database stability on a non recursive revert
   of an ancestor that contains a possible move related tree conflict.
 */
static svn_error_t *
revert_maybe_raise_moved_away(svn_wc__db_wcroot_t * wcroot,
                              svn_wc__db_t *db,
                              const char *local_relpath,
                              int op_depth_below,
                              apr_pool_t *scratch_pool)
{
  svn_skel_t *conflict;
  svn_wc_operation_t operation;
  svn_boolean_t tree_conflicted;
  const apr_array_header_t *locations;
  svn_wc_conflict_reason_t reason;
  svn_wc_conflict_action_t action;

  SVN_ERR(svn_wc__db_read_conflict_internal(&conflict, NULL, NULL, wcroot,
                                            local_relpath,
                                            scratch_pool, scratch_pool));

  if (!conflict)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__conflict_read_info(&operation, &locations, NULL, NULL,
                                     &tree_conflicted,
                                     db, wcroot->abspath,
                                     conflict,
                                     scratch_pool, scratch_pool));

  if (!tree_conflicted
      || (operation != svn_wc_operation_update
          && operation != svn_wc_operation_switch))
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action,
                                              NULL,
                                              db, wcroot->abspath,
                                              conflict,
                                              scratch_pool,
                                              scratch_pool));

  if (reason == svn_wc_conflict_reason_deleted
      || reason == svn_wc_conflict_reason_replaced)
    {
      SVN_ERR(svn_wc__db_op_raise_moved_away_internal(
        wcroot, local_relpath, op_depth_below, db,
        operation, action,
        (locations && locations->nelts > 0)
        ? APR_ARRAY_IDX(locations, 0,
                        const svn_wc_conflict_version_t *)
        : NULL,
        (locations && locations->nelts > 1)
        ? APR_ARRAY_IDX(locations, 1,
                        const svn_wc_conflict_version_t *)
        : NULL,
        scratch_pool));

      /* Transform the move information into revert information */
      SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                          STMT_MOVE_NOTIFY_TO_REVERT));
    }

  return SVN_NO_ERROR;
}

/* Baton for op_revert_txn and op_revert_recursive_txn */
struct revert_baton_t
{
  svn_wc__db_t *db;
  svn_boolean_t clear_changelists;
};

/* One of the two alternative bodies of svn_wc__db_op_revert().
 *
 * Implements svn_wc__db_txn_callback_t. */
static svn_error_t *
op_revert_txn(void *baton,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *scratch_pool)
{
  struct revert_baton_t *rvb = baton;
  svn_wc__db_t *db = rvb->db;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int op_depth;
  svn_boolean_t moved_here;
  int affected_rows;
  const char *moved_to;
  int op_depth_below;

  /* ### Similar structure to op_revert_recursive_txn, should they be
         combined? */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    {
      SVN_ERR(svn_sqlite__reset(stmt));

      /* There was no NODE row, so attempt to delete an ACTUAL_NODE row.  */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
      if (affected_rows)
        {
          /* Can't do non-recursive actual-only revert if actual-only
             children exist. Raise an error to cancel the transaction.  */
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_ACTUAL_HAS_CHILDREN));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          SVN_ERR(svn_sqlite__reset(stmt));
          if (have_row)
            return svn_error_createf(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                                     _("Can't revert '%s' without"
                                       " reverting children"),
                                     path_for_error_message(wcroot,
                                                            local_relpath,
                                                            scratch_pool));
          return SVN_NO_ERROR;
        }

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }

  op_depth = svn_sqlite__column_int(stmt, 0);
  moved_here = svn_sqlite__column_boolean(stmt, 15);
  moved_to = svn_sqlite__column_text(stmt, 17, scratch_pool);

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    op_depth_below = svn_sqlite__column_int(stmt, 0);
  else
    op_depth_below = -1;

  SVN_ERR(svn_sqlite__reset(stmt));

  if (moved_to)
    {
      SVN_ERR(svn_wc__db_op_break_move_internal(wcroot,
                                                local_relpath, op_depth,
                                                moved_to, NULL, scratch_pool));
    }

  if (op_depth > 0 && op_depth == relpath_depth(local_relpath))
    {
      int op_depth_increased;

      /* Can't do non-recursive revert if children exist */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_GE_OP_DEPTH_CHILDREN));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath, op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      SVN_ERR(svn_sqlite__reset(stmt));
      if (have_row)
        return svn_error_createf(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                                 _("Can't revert '%s' without"
                                   " reverting children"),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));

      /* Rewrite the op-depth of all deleted children making the
         direct children into roots of deletes. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                     STMT_UPDATE_OP_DEPTH_INCREASE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                local_relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__update(&op_depth_increased, stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* ### This removes the lock, but what about the access baton? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_WC_LOCK_ORPHAN));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* If this node was moved-here, clear moved-to at the move source. */
      if (moved_here)
        SVN_ERR(clear_moved_to(wcroot, local_relpath, scratch_pool));

      /* If the node was moved itself, we don't have interesting moved
         children (and the move itself was already broken) */
      if (op_depth_increased && !moved_to)
        SVN_ERR(revert_maybe_raise_moved_away(wcroot, db, local_relpath,
                                              op_depth_below, scratch_pool));
    }

  if (rvb->clear_changelists)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                  STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
      if (!affected_rows)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                  STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
        }
    }

  return SVN_NO_ERROR;
}


/* One of the two alternative bodies of svn_wc__db_op_revert().
 *
 * Implements svn_wc__db_txn_callback_t. */
static svn_error_t *
op_revert_recursive_txn(void *baton,
                        svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        apr_pool_t *scratch_pool)
{
  struct revert_baton_t *rvb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int op_depth;
  int select_op_depth;
  svn_boolean_t moved_here;
  int affected_rows;
  apr_pool_t *iterpool;

  /* ### Similar structure to op_revert_txn, should they be
         combined? */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    {
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                local_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

      if (affected_rows)
        return SVN_NO_ERROR;  /* actual-only revert */

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }

  op_depth = svn_sqlite__column_int(stmt, 0);
  moved_here = svn_sqlite__column_boolean(stmt, 15);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (op_depth > 0 && op_depth != relpath_depth(local_relpath))
    return svn_error_createf(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                             _("Can't revert '%s' without"
                               " reverting parent"),
                             path_for_error_message(wcroot,
                                                    local_relpath,
                                                    scratch_pool));

  /* Remove moved-here from move destinations outside the tree. */
  SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb, STMT_SELECT_MOVED_OUTSIDE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *src_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *dst_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      int move_op_depth = svn_sqlite__column_int(stmt, 2);
      svn_error_t *err;

      err = svn_wc__db_op_break_move_internal(wcroot,
                                              src_relpath, move_op_depth,
                                              dst_relpath, NULL, scratch_pool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Don't delete BASE nodes */
  select_op_depth = op_depth ? op_depth : 1;

  /* Reverting any non wc-root node */
  SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb,
                    STMT_DELETE_NODES_ABOVE_DEPTH_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                            local_relpath, select_op_depth));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (rvb->clear_changelists)
    {
      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, wcroot->sdb,
                        STMT_DELETE_ACTUAL_NODE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, wcroot->sdb,
                        STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, wcroot->sdb,
                        STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* ### This removes the locks, but what about the access batons? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WC_LOCK_ORPHAN_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_HERE_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
    {
      const char *moved_here_child_relpath;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      moved_here_child_relpath = svn_sqlite__column_text(stmt, 0, iterpool);
      err = clear_moved_to(wcroot, moved_here_child_relpath, iterpool);
      if (err)
        return svn_error_trace(svn_error_compose_create(
                                        err,
                                        svn_sqlite__reset(stmt)));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));
  svn_pool_destroy(iterpool);

  /* Clear potential moved-to pointing at the target node itself. */
  if (op_depth > 0 && op_depth == relpath_depth(local_relpath)
      && moved_here)
    SVN_ERR(clear_moved_to(wcroot, local_relpath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_revert(svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_depth_t depth,
                     svn_boolean_t clear_changelists,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  struct revert_baton_t rvb;
  struct with_triggers_baton_t wtb = { STMT_CREATE_REVERT_LIST,
                                       STMT_DROP_REVERT_LIST_TRIGGERS,
                                       NULL, NULL};

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  rvb.db = db;
  rvb.clear_changelists = clear_changelists;
  wtb.cb_baton = &rvb;

  switch (depth)
    {
    case svn_depth_empty:
      wtb.cb_func = op_revert_txn;
      break;
    case svn_depth_infinity:
      wtb.cb_func = op_revert_recursive_txn;
      break;
    default:
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Unsupported depth for revert of '%s'"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(with_triggers(&wtb, wcroot, local_relpath, scratch_pool),
                      wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, depth, scratch_pool));

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_revert_list_read().
 */
static svn_error_t *
revert_list_read(svn_boolean_t *reverted,
                 const apr_array_header_t **marker_paths,
                 svn_boolean_t *copied_here,
                 svn_node_kind_t *kind,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 svn_wc__db_t *db,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *reverted = FALSE;
  *marker_paths = NULL;
  *copied_here = FALSE;
  *kind = svn_node_unknown;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REVERT_LIST));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      svn_boolean_t is_actual = svn_sqlite__column_boolean(stmt, 0);
      svn_boolean_t another_row = FALSE;

      if (is_actual)
        {
          apr_size_t conflict_len;
          const void *conflict_data;

          conflict_data = svn_sqlite__column_blob(stmt, 5, &conflict_len,
                                                  scratch_pool);
          if (conflict_data)
            {
              svn_skel_t *conflicts = svn_skel__parse(conflict_data,
                                                      conflict_len,
                                                      scratch_pool);

              SVN_ERR(svn_wc__conflict_read_markers(marker_paths,
                                                    db, wcroot->abspath,
                                                    conflicts,
                                                    result_pool,
                                                    scratch_pool));
            }

          if (!svn_sqlite__column_is_null(stmt, 1)) /* notify */
            *reverted = TRUE;

          SVN_ERR(svn_sqlite__step(&another_row, stmt));
        }

      if (!is_actual || another_row)
        {
          *reverted = TRUE;
          if (!svn_sqlite__column_is_null(stmt, 4)) /* repos_id */
            {
              int op_depth = svn_sqlite__column_int(stmt, 3);
              *copied_here = (op_depth == relpath_depth(local_relpath));
            }
          *kind = svn_sqlite__column_token(stmt, 2, kind_map);
        }

    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_REVERT_LIST));
      SVN_ERR(svn_sqlite__bindf(stmt, "s", local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_revert_list_read(svn_boolean_t *reverted,
                            const apr_array_header_t **marker_files,
                            svn_boolean_t *copied_here,
                            svn_node_kind_t *kind,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    revert_list_read(reverted, marker_files, copied_here, kind,
                     wcroot, local_relpath, db,
                     result_pool, scratch_pool),
    wcroot);
  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_revert_list_read_copied_children().
 */
static svn_error_t *
revert_list_read_copied_children(svn_wc__db_wcroot_t *wcroot,
                                 const char *local_relpath,
                                 apr_array_header_t **children_p,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *children;

  children =
    apr_array_make(result_pool, 0,
                  sizeof(svn_wc__db_revert_list_copied_child_info_t *));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REVERT_LIST_COPIED_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "sd",
                            local_relpath, relpath_depth(local_relpath)));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      svn_wc__db_revert_list_copied_child_info_t *child_info;
      const char *child_relpath;

      child_info = apr_palloc(result_pool, sizeof(*child_info));

      child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      child_info->abspath = svn_dirent_join(wcroot->abspath, child_relpath,
                                            result_pool);
      child_info->kind = svn_sqlite__column_token(stmt, 1, kind_map);
      APR_ARRAY_PUSH(
        children,
        svn_wc__db_revert_list_copied_child_info_t *) = child_info;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
   SVN_ERR(svn_sqlite__reset(stmt));

  *children_p = children;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_revert_list_read_copied_children(apr_array_header_t **children,
                                            svn_wc__db_t *db,
                                            const char *local_abspath,
                                            apr_pool_t *result_pool,
                                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    revert_list_read_copied_children(wcroot, local_relpath, children,
                                     result_pool, scratch_pool),
    wcroot);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_revert_list_notify(svn_wc_notify_func2_t notify_func,
                              void *notify_baton,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, iterpool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REVERT_LIST_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_trace(svn_sqlite__reset(stmt)); /* optimise for no row */
  while (have_row)
    {
      const char *notify_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      svn_wc_notify_t *notify;

      svn_pool_clear(iterpool);

      notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                    notify_relpath,
                                                    iterpool),
                                    svn_wc_notify_revert,
                                    iterpool);

      if (!svn_sqlite__column_is_null(stmt, 1))
        notify->kind = svn_sqlite__column_token(stmt, 1, kind_map);
      else
        {
          if (!svn_sqlite__column_is_null(stmt, 3))
            notify->kind = svn_sqlite__column_token(stmt, 3, kind_map_none);

          switch (svn_sqlite__column_int(stmt, 2))
            {
              case 0:
                continue;
              case 1:
                /* standard revert */
                break;
              case 2:
                notify->action = svn_wc_notify_tree_conflict;
                break;
              default:
                SVN_ERR_MALFUNCTION();
            }
        }

      notify_func(notify_baton, notify, iterpool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_REVERT_LIST_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_revert_list_done(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb, STMT_DROP_REVERT_LIST));

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_op_remove_node().
 */
static svn_error_t *
remove_node_txn(svn_boolean_t *left_changes,
                svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_wc__db_t *db,
                svn_boolean_t destroy_wc,
                svn_boolean_t destroy_changes,
                const svn_skel_t *conflict,
                const svn_skel_t *work_items,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* Note that unlike many similar functions it is a valid scenario for this
     function to be called on a wcroot! */

   /* db set when destroying wc */
  SVN_ERR_ASSERT(!destroy_wc || db != NULL);

  if (left_changes)
    *left_changes = FALSE;

  if (destroy_wc
      && (!destroy_changes || *local_relpath == '\0'))
    {
      svn_boolean_t have_row;
      apr_pool_t *iterpool;
      svn_error_t *err = NULL;

      /* Install WQ items for deleting the unmodified files and all dirs */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_WORKING_PRESENT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                wcroot->wc_id, local_relpath));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      iterpool = svn_pool_create(scratch_pool);

      while (have_row)
        {
          const char *child_relpath;
          const char *child_abspath;
          svn_node_kind_t child_kind;
          svn_boolean_t have_checksum;
          svn_filesize_t recorded_size;
          apr_int64_t recorded_time;
          const svn_io_dirent2_t *dirent;
          svn_boolean_t modified_p = TRUE;
          svn_skel_t *work_item = NULL;

          svn_pool_clear(iterpool);

          child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
          child_kind = svn_sqlite__column_token(stmt, 1, kind_map);

          child_abspath = svn_dirent_join(wcroot->abspath, child_relpath,
                                          iterpool);

          if (child_kind == svn_node_file)
            {
              have_checksum = !svn_sqlite__column_is_null(stmt, 2);
              recorded_size = get_recorded_size(stmt, 3);
              recorded_time = svn_sqlite__column_int64(stmt, 4);
            }

          if (cancel_func)
            err = cancel_func(cancel_baton);

          if (err)
            break;

          err = svn_io_stat_dirent2(&dirent, child_abspath, FALSE, TRUE,
                                    iterpool, iterpool);

          if (err)
            break;

          if (destroy_changes
              || dirent->kind != svn_node_file
              || child_kind != svn_node_file)
            {
              /* Not interested in keeping changes */
              modified_p = FALSE;
            }
          else if (child_kind == svn_node_file
                   && dirent->kind == svn_node_file
                   && dirent->filesize == recorded_size
                   && dirent->mtime == recorded_time)
            {
              modified_p = FALSE; /* File matches recorded state */
            }
          else if (have_checksum)
            err = svn_wc__internal_file_modified_p(&modified_p,
                                                   db, child_abspath,
                                                   FALSE, iterpool);

          if (err)
            break;

          if (modified_p)
            {
              if (left_changes)
                *left_changes = TRUE;
            }
          else if (child_kind == svn_node_dir)
            {
              err = svn_wc__wq_build_dir_remove(&work_item,
                                                db, wcroot->abspath,
                                                child_abspath, FALSE,
                                                iterpool, iterpool);
            }
          else /* svn_node_file || svn_node_symlink */
            {
              err = svn_wc__wq_build_file_remove(&work_item,
                                                 db, wcroot->abspath,
                                                 child_abspath,
                                                 iterpool, iterpool);
            }

          if (err)
            break;

          if (work_item)
            {
              err = add_work_items(wcroot->sdb, work_item, iterpool);
              if (err)
                break;
            }

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      svn_pool_destroy(iterpool);

      SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
    }

  if (destroy_wc && *local_relpath != '\0')
    {
      /* Create work item for destroying the root */
      svn_wc__db_status_t status;
      svn_node_kind_t kind;
      SVN_ERR(read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        wcroot, local_relpath,
                        scratch_pool, scratch_pool));

      if (status == svn_wc__db_status_normal
          || status == svn_wc__db_status_added
          || status == svn_wc__db_status_incomplete)
        {
          svn_skel_t *work_item = NULL;
          const char *local_abspath = svn_dirent_join(wcroot->abspath,
                                                          local_relpath,
                                                          scratch_pool);

          if (kind == svn_node_dir)
            {
              SVN_ERR(svn_wc__wq_build_dir_remove(&work_item,
                                                  db, wcroot->abspath,
                                                  local_abspath,
                                                  destroy_changes
                                                      /* recursive */,
                                                  scratch_pool, scratch_pool));
            }
          else
            {
              svn_boolean_t modified_p = FALSE;

              if (!destroy_changes)
                {
                  SVN_ERR(svn_wc__internal_file_modified_p(&modified_p,
                                                           db, local_abspath,
                                                           FALSE,
                                                           scratch_pool));
                }

              if (!modified_p)
                SVN_ERR(svn_wc__wq_build_file_remove(&work_item,
                                                     db, wcroot->abspath,
                                                     local_abspath,
                                                     scratch_pool,
                                                     scratch_pool));
              else
                {
                  if (left_changes)
                    *left_changes = TRUE;
                }
            }

          SVN_ERR(add_work_items(wcroot->sdb, work_item, scratch_pool));
        }
    }

  /* Remove all nodes below local_relpath */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_NODE_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Delete the root NODE when this is not the working copy root */
  if (local_relpath[0] != '\0')
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_NODE_ALL_LAYERS));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE_RECURSIVE));

  /* Delete all actual nodes at or below local_relpath */
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                         local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));
  if (conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              conflict, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_remove_node(svn_boolean_t *left_changes,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          svn_boolean_t destroy_wc,
                          svn_boolean_t destroy_changes,
                          const svn_skel_t *conflict,
                          const svn_skel_t *work_items,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(remove_node_txn(left_changes,
                                      wcroot, local_relpath, db,
                                      destroy_wc, destroy_changes,
                                      conflict, work_items,
                                      cancel_func, cancel_baton, scratch_pool),
                      wcroot);

  /* Flush everything below this node in all ways */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_op_set_base_depth().
 */
static svn_error_t *
db_op_set_base_depth(svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     svn_depth_t depth,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  /* Flush any entries before we start monkeying the database.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_NODE_BASE_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath,
                            svn_token__to_word(depth_map, depth)));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 0)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' is not a committed directory"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_set_base_depth(svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_depth_t depth,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(depth >= svn_depth_empty && depth <= svn_depth_infinity);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### We set depth on working and base to match entry behavior.
         Maybe these should be separated later? */
  SVN_WC__DB_WITH_TXN(db_op_set_base_depth(wcroot, local_relpath, depth,
                                           scratch_pool),
                      wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
info_below_working(svn_boolean_t *have_base,
                   svn_boolean_t *have_work,
                   svn_wc__db_status_t *status,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   int below_op_depth, /* < 0 is ignored */
                   apr_pool_t *scratch_pool);


/* Convert STATUS, the raw status obtained from the presence map, to
   the status appropriate for a working (op_depth > 0) node and return
   it in *WORKING_STATUS. */
static svn_error_t *
convert_to_working_status(svn_wc__db_status_t *working_status,
                          svn_wc__db_status_t status)
{
  svn_wc__db_status_t work_status = status;

  SVN_ERR_ASSERT(work_status == svn_wc__db_status_normal
                 || work_status == svn_wc__db_status_not_present
                 || work_status == svn_wc__db_status_base_deleted
                 || work_status == svn_wc__db_status_incomplete
                 || work_status == svn_wc__db_status_excluded);

  if (work_status == svn_wc__db_status_excluded)
    {
      *working_status = svn_wc__db_status_excluded;
    }
  else if (work_status == svn_wc__db_status_not_present
           || work_status == svn_wc__db_status_base_deleted)
    {
      /* The caller should scan upwards to detect whether this
         deletion has occurred because this node has been moved
         away, or it is a regular deletion. Also note that the
         deletion could be of the BASE tree, or a child of
         something that has been copied/moved here. */

      *working_status = svn_wc__db_status_deleted;
    }
  else /* normal or incomplete */
    {
      /* The caller should scan upwards to detect whether this
         addition has occurred because of a simple addition,
         a copy, or is the destination of a move. */
      *working_status = svn_wc__db_status_added;
    }

  return SVN_NO_ERROR;
}


/* Return the status of the node, if any, below the "working" node (or
   below BELOW_OP_DEPTH if >= 0).
   Set *HAVE_BASE or *HAVE_WORK to indicate if a base node or lower
   working node is present, and *STATUS to the status of the first
   layer below the selected node. */
static svn_error_t *
info_below_working(svn_boolean_t *have_base,
                   svn_boolean_t *have_work,
                   svn_wc__db_status_t *status,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   int below_op_depth,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *have_base = *have_work =  FALSE;
  *status = svn_wc__db_status_normal;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (below_op_depth >= 0)
    {
      while (have_row &&
             (svn_sqlite__column_int(stmt, 0) > below_op_depth))
        {
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
    }
  if (have_row)
    {
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        *status = svn_sqlite__column_token(stmt, 3, presence_map);

      while (have_row)
        {
          int op_depth = svn_sqlite__column_int(stmt, 0);

          if (op_depth > 0)
            *have_work = TRUE;
          else
            *have_base = TRUE;

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (*have_work)
    SVN_ERR(convert_to_working_status(status, *status));

  return SVN_NO_ERROR;
}

/* Helper function for op_delete_txn */
static svn_error_t *
delete_update_movedto(svn_wc__db_wcroot_t *wcroot,
                      const char *child_moved_from_relpath,
                      int op_depth,
                      const char *new_moved_to_relpath,
                      apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_MOVED_TO_RELPATH));

  SVN_ERR(svn_sqlite__bindf(stmt, "isds",
                            wcroot->wc_id,
                            child_moved_from_relpath,
                            op_depth,
                            new_moved_to_relpath));
  SVN_ERR(svn_sqlite__update(&affected, stmt));
#ifdef SVN_DEBUG
  /* Not fatal in release mode. The move recording is broken,
     but the rest of the working copy can handle this. */
  SVN_ERR_ASSERT(affected == 1);
#endif

  return SVN_NO_ERROR;
}


struct op_delete_baton_t {
  const char *moved_to_relpath; /* NULL if delete is not part of a move */
  svn_skel_t *conflict;
  svn_skel_t *work_items;
  svn_boolean_t delete_dir_externals;
  svn_boolean_t notify;
};

/* This structure is used while rewriting move information for nodes.
 *
 * The most simple case of rewriting move information happens when
 * a moved-away subtree is moved again:  mv A B; mv B C
 * The second move requires rewriting moved-to info at or within A.
 *
 * Another example is a move of a subtree which had nodes moved into it:
 *   mv A B/F; mv B G
 * This requires rewriting such that A/F is marked has having moved to G/F.
 *
 * Another case is where a node becomes a nested moved node.
 * A nested move happens when a subtree child is moved before or after
 * the subtree itself is moved. For example:
 *   mv A/F A/G; mv A B
 * In this case, the move A/F -> A/G is rewritten to B/F -> B/G.
 * Note that the following sequence results in the same DB state:
 *   mv A B; mv B/F B/G
 * We do not care about the order the moves were performed in.
 * For details, see http://wiki.apache.org/subversion/MultiLayerMoves
 */
struct moved_node_t {
  /* The source of the move. */
  const char *local_relpath;

  /* The move destination. */
  const char *moved_to_relpath;

  /* The op-depth of the deleted node at the source of the move. */
  int op_depth;

  /* When >= 1 the op_depth at which local_relpath was moved to its
     location. Used to find its original location outside the delete */
  int moved_from_depth;
};

/* Helper function to resolve the original location of local_relpath at OP_DEPTH
   before it was moved into the tree rooted at ROOT_RELPATH. */
static svn_error_t *
resolve_moved_from(const char **moved_from_relpath,
                   int *moved_from_op_depth,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *root_relpath,
                   const char *local_relpath,
                   int op_depth,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *suffix = "";
  svn_sqlite__stmt_t *stmt;
  const char *m_from_relpath;
  int m_from_op_depth;
  int m_move_from_depth;
  svn_boolean_t have_row;

  while (relpath_depth(local_relpath) > op_depth)
    {
      const char *name;
      svn_relpath_split(&local_relpath, &name, local_relpath, scratch_pool);
      suffix = svn_relpath_join(suffix, name, scratch_pool);
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_FROM_FOR_DELETE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      /* assert(have_row); */
      *moved_from_relpath = NULL;
      *moved_from_op_depth = -1;

      SVN_ERR(svn_sqlite__reset(stmt));

      return SVN_NO_ERROR;
    }

  m_from_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
  m_from_op_depth = svn_sqlite__column_int(stmt, 1);
  m_move_from_depth = svn_sqlite__column_int(stmt, 2);

  SVN_ERR(svn_sqlite__reset(stmt));

  if (! svn_relpath_skip_ancestor(root_relpath, m_from_relpath))
    {
      *moved_from_relpath = svn_relpath_join(m_from_relpath, suffix,
                                             result_pool);
      *moved_from_op_depth = m_from_op_depth; /* ### Ok? */
      return SVN_NO_ERROR;
    }
  else if (!m_move_from_depth)
    {
      *moved_from_relpath = NULL;
      *moved_from_op_depth = -1;
      return SVN_NO_ERROR;
    }

  return svn_error_trace(
        resolve_moved_from(moved_from_relpath,
                           moved_from_op_depth,
                           wcroot,
                           root_relpath,
                           svn_relpath_join(m_from_relpath, suffix,
                                            scratch_pool),
                           m_move_from_depth,
                           result_pool, scratch_pool));
}

static svn_error_t *
delete_node(void *baton,
            svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath,
            apr_pool_t *scratch_pool)
{
  struct op_delete_baton_t *b = baton;
  svn_wc__db_status_t status;
  svn_boolean_t have_row, op_root;
  svn_boolean_t add_work = FALSE;
  svn_sqlite__stmt_t *stmt;
  int working_op_depth; /* Depth of what is to be deleted */
  int keep_op_depth = 0; /* Depth of what is below what is deleted */
  svn_node_kind_t kind;
  apr_array_header_t *moved_nodes = NULL;
  int delete_op_depth = relpath_depth(local_relpath);

  assert(*local_relpath); /* Can't delete wcroot */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                               svn_sqlite__reset(stmt),
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }

  working_op_depth = svn_sqlite__column_int(stmt, 0);
  status = svn_sqlite__column_token(stmt, 3, presence_map);
  kind = svn_sqlite__column_token(stmt, 4, kind_map);

  if (working_op_depth < delete_op_depth)
    {
      op_root = FALSE;
      add_work = TRUE;
      keep_op_depth = working_op_depth;
    }
  else
    {
      op_root = TRUE;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (have_row)
        {
          svn_wc__db_status_t below_status;
          int below_op_depth;

          below_op_depth = svn_sqlite__column_int(stmt, 0);
          below_status = svn_sqlite__column_token(stmt, 3, presence_map);

          if (below_status != svn_wc__db_status_not_present
              && below_status != svn_wc__db_status_base_deleted)
            {
              add_work = TRUE;
              keep_op_depth = below_op_depth;
            }
          else
            keep_op_depth = 0;
        }
      else
        keep_op_depth = -1;
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (working_op_depth != 0) /* WORKING */
    SVN_ERR(convert_to_working_status(&status, status));

  if (status == svn_wc__db_status_deleted
      || status == svn_wc__db_status_not_present)
    return SVN_NO_ERROR;

  /* Don't copy BASE directories with server excluded nodes */
  if (status == svn_wc__db_status_normal && kind == svn_node_dir)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_HAS_SERVER_EXCLUDED_DESCENDANTS));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        {
          const char *absent_path = svn_sqlite__column_text(stmt, 0,
                                                            scratch_pool);

          return svn_error_createf(
                               SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                               svn_sqlite__reset(stmt),
                          _("Cannot delete '%s' as '%s' is excluded by server"),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool),
                               path_for_error_message(wcroot, absent_path,
                                                      scratch_pool));
        }
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else if (status == svn_wc__db_status_server_excluded)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                          _("Cannot delete '%s' as it is excluded by server"),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool));
    }
  else if (status == svn_wc__db_status_excluded)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                          _("Cannot delete '%s' as it is excluded"),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool));
    }

  if (b->moved_to_relpath)
    {
      const char *moved_from_relpath = NULL;
      struct moved_node_t *moved_node;
      int move_op_depth;

      moved_nodes = apr_array_make(scratch_pool, 1,
                                   sizeof(struct moved_node_t *));

      /* The node is being moved-away.
       * Figure out if the node was moved-here before, or whether this
       * is the first time the node is moved. */
      if (status == svn_wc__db_status_added)
        SVN_ERR(scan_addition(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                              &moved_from_relpath,
                              NULL,
                              &move_op_depth,
                              wcroot, local_relpath,
                              scratch_pool, scratch_pool));

      if (op_root && moved_from_relpath)
        {
          const char *part = svn_relpath_skip_ancestor(local_relpath,
                                                       moved_from_relpath);

          /* Existing move-root is moved to another location */
          moved_node = apr_palloc(scratch_pool, sizeof(struct moved_node_t));
          if (!part)
            moved_node->local_relpath = moved_from_relpath;
          else
            moved_node->local_relpath = svn_relpath_join(b->moved_to_relpath,
                                                         part, scratch_pool);
          moved_node->op_depth = move_op_depth;
          moved_node->moved_to_relpath = b->moved_to_relpath;
          moved_node->moved_from_depth = -1;

          APR_ARRAY_PUSH(moved_nodes, const struct moved_node_t *) = moved_node;
        }
      else if (!op_root && (status == svn_wc__db_status_normal
                            || status == svn_wc__db_status_copied
                            || status == svn_wc__db_status_moved_here))
        {
          /* The node is becoming a move-root for the first time,
           * possibly because of a nested move operation. */
          moved_node = apr_palloc(scratch_pool, sizeof(struct moved_node_t));
          moved_node->local_relpath = local_relpath;
          moved_node->op_depth = delete_op_depth;
          moved_node->moved_to_relpath = b->moved_to_relpath;
          moved_node->moved_from_depth = -1;

          APR_ARRAY_PUSH(moved_nodes, const struct moved_node_t *) = moved_node;
        }
      /* Else: We can't track history of local additions and/or of things we are
               about to delete. */

      /* And update all moved_to values still pointing to this location */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_MOVED_TO_DESCENDANTS));
      SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id,
                                             local_relpath,
                                             b->moved_to_relpath));
      SVN_ERR(svn_sqlite__update(NULL, stmt));
    }

  /* Find children that were moved out of the subtree rooted at this node.
   * We'll need to update their op-depth columns because their deletion
   * is now implied by the deletion of their parent (i.e. this node). */
    {
      apr_pool_t *iterpool;
      int i;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_MOVED_FOR_DELETE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                delete_op_depth));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      iterpool = svn_pool_create(scratch_pool);
      while (have_row)
        {
          struct moved_node_t *mn;
          const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
          const char *mv_to_relpath = svn_sqlite__column_text(stmt, 1, NULL);
          int child_op_depth = svn_sqlite__column_int(stmt, 2);
          int moved_from_depth = -1;
          svn_boolean_t fixup = FALSE;

          if (! b->moved_to_relpath
              && ! svn_relpath_skip_ancestor(local_relpath, mv_to_relpath))
            {
              /* a NULL moved_here_depth will be reported as 0 */
              int moved_here_depth = svn_sqlite__column_int(stmt, 3);

              /* Plain delete. Fixup move information of descendants that were
                 moved here, or that were moved out */

              if (moved_here_depth >= delete_op_depth)
                {
                  /* The move we recorded here must be moved to the location
                     this node had before it was moved here.

                     This might contain multiple steps when the node was moved
                     in several places within the to be deleted tree */

                  /* ### TODO: Add logic */
                  fixup = TRUE;
                  moved_from_depth = moved_here_depth;
                }
              else
                {
                  /* Update the op-depth of an moved away node that was
                     registered as moved by the records that we are about
                     to delete */
                  fixup = TRUE;
                  child_op_depth = delete_op_depth;
                }
            }
          else if (b->moved_to_relpath)
            {
              /* The node is moved to a new location */

              if (delete_op_depth == child_op_depth)
                {
                  /* Update the op-depth of a tree shadowed by this tree */
                  fixup = TRUE;
                  /*child_op_depth = delete_depth;*/
                }
              else if (child_op_depth >= delete_op_depth
                       && !svn_relpath_skip_ancestor(local_relpath,
                                                     mv_to_relpath))
                {
                  /* Update the move destination of something that is now moved
                     away further */

                  child_relpath = svn_relpath_skip_ancestor(local_relpath,
                                                            child_relpath);

                  if (child_relpath)
                    {
                      child_relpath = svn_relpath_join(b->moved_to_relpath,
                                                       child_relpath,
                                                       scratch_pool);

                      if (child_op_depth > delete_op_depth
                           && svn_relpath_skip_ancestor(local_relpath,
                                                        child_relpath))
                        child_op_depth = delete_op_depth;
                      else
                        {
                          /* Calculate depth of the shadowing at the new location */
                          child_op_depth = child_op_depth
                                                - relpath_depth(local_relpath)
                                                + relpath_depth(b->moved_to_relpath);
                        }

                      fixup = TRUE;
                    }
                }
            }

          if (fixup)
            {
              mn = apr_palloc(scratch_pool, sizeof(struct moved_node_t));

              mn->local_relpath = apr_pstrdup(scratch_pool, child_relpath);
              mn->moved_to_relpath = apr_pstrdup(scratch_pool, mv_to_relpath);
              mn->op_depth = child_op_depth;
              mn->moved_from_depth = moved_from_depth;

              if (!moved_nodes)
                moved_nodes = apr_array_make(scratch_pool, 1,
                                             sizeof(struct moved_node_t *));
              APR_ARRAY_PUSH(moved_nodes, struct moved_node_t *) = mn;
            }

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      SVN_ERR(svn_sqlite__reset(stmt));

      for (i = 0; moved_nodes && (i < moved_nodes->nelts); i++)
        {
          struct moved_node_t *mn = APR_ARRAY_IDX(moved_nodes, i,
                                                  struct moved_node_t *);

          if (mn->moved_from_depth > 0)
            {
              svn_pool_clear(iterpool);

              SVN_ERR(resolve_moved_from(&mn->local_relpath, &mn->op_depth,
                                         wcroot, local_relpath,
                                         mn->local_relpath,
                                         mn->moved_from_depth,
                                         scratch_pool, iterpool));

              if (!mn->local_relpath)
                svn_sort__array_delete(moved_nodes, i--, 1);
            }
        }

      svn_pool_destroy(iterpool);
    }

  if (!b->moved_to_relpath)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_CLEAR_MOVED_TO_DESCENDANTS));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                local_relpath));
      SVN_ERR(svn_sqlite__update(NULL, stmt));

      if (op_root)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_CLEAR_MOVED_TO_FROM_DEST));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                    local_relpath));

          SVN_ERR(svn_sqlite__update(NULL, stmt));
        }
    }


  /* ### Put actual-only nodes into the list? */
  if (b->notify)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_DELETE_LIST));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd",
                                wcroot->wc_id, local_relpath, working_op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_NODES_ABOVE_DEPTH_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd",
                            wcroot->wc_id, local_relpath, delete_op_depth));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Delete ACTUAL_NODE rows, but leave those that have changelist
     and a NODES row. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                         STMT_DELETE_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                         STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WC_LOCK_ORPHAN_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (add_work)
    {
      /* Delete the node at LOCAL_RELPATH, and possibly mark it as moved. */

      /* Delete the node and possible descendants. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                 STMT_INSERT_DELETE_FROM_NODE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isdd",
                                wcroot->wc_id, local_relpath,
                                keep_op_depth, delete_op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (moved_nodes)
    {
      int i;

      for (i = 0; i < moved_nodes->nelts; ++i)
        {
          const struct moved_node_t *moved_node
            = APR_ARRAY_IDX(moved_nodes, i, void *);

          SVN_ERR(delete_update_movedto(wcroot,
                                        moved_node->local_relpath,
                                        moved_node->op_depth,
                                        moved_node->moved_to_relpath,
                                        scratch_pool));
        }
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_FILE_EXTERNALS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    b->delete_dir_externals
                                    ? STMT_DELETE_EXTERNAL_REGISTATIONS
                                    : STMT_DELETE_FILE_EXTERNAL_REGISTATIONS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(add_work_items(wcroot->sdb, b->work_items, scratch_pool));
  if (b->conflict)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              b->conflict, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
op_delete_txn(void *baton,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *scratch_pool)
{

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb, STMT_CREATE_DELETE_LIST));
  SVN_ERR(delete_node(baton, wcroot, local_relpath, scratch_pool));
  return SVN_NO_ERROR;
}


struct op_delete_many_baton_t {
  apr_array_header_t *rel_targets;
  svn_boolean_t delete_dir_externals;
  const svn_skel_t *work_items;
};

static svn_error_t *
op_delete_many_txn(void *baton,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *scratch_pool)
{
  struct op_delete_many_baton_t *odmb = baton;
  struct op_delete_baton_t odb;
  int i;
  apr_pool_t *iterpool;

  odb.moved_to_relpath = NULL;
  odb.conflict = NULL;
  odb.work_items = NULL;
  odb.delete_dir_externals = odmb->delete_dir_externals;
  odb.notify = TRUE;

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb, STMT_CREATE_DELETE_LIST));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < odmb->rel_targets->nelts; i++)
    {
      const char *target_relpath = APR_ARRAY_IDX(odmb->rel_targets, i,
                                                 const char *);


      svn_pool_clear(iterpool);
      SVN_ERR(delete_node(&odb, wcroot, target_relpath, iterpool));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(add_work_items(wcroot->sdb, odmb->work_items, scratch_pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
do_delete_notify(void *baton,
                 svn_wc__db_wcroot_t *wcroot,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_DELETE_LIST));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
    {
      const char *notify_relpath;
      const char *notify_abspath;

      svn_pool_clear(iterpool);

      notify_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      notify_abspath = svn_dirent_join(wcroot->abspath,
                                       notify_relpath,
                                       iterpool);

      notify_func(notify_baton,
                  svn_wc_create_notify(notify_abspath,
                                       svn_wc_notify_delete,
                                       iterpool),
                  iterpool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_sqlite__reset(stmt));

  /* We only allow cancellation after notification for all deleted nodes
   * has happened. The nodes are already deleted so we should notify for
   * all of them. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_delete(svn_wc__db_t *db,
                     const char *local_abspath,
                     const char *moved_to_abspath,
                     svn_boolean_t delete_dir_externals,
                     svn_skel_t *conflict,
                     svn_skel_t *work_items,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_wc__db_wcroot_t *moved_to_wcroot;
  const char *local_relpath;
  const char *moved_to_relpath;
  struct op_delete_baton_t odb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (moved_to_abspath)
    {
      SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&moved_to_wcroot,
                                                    &moved_to_relpath,
                                                    db, moved_to_abspath,
                                                    scratch_pool,
                                                    scratch_pool));
      VERIFY_USABLE_WCROOT(moved_to_wcroot);

      if (strcmp(wcroot->abspath, moved_to_wcroot->abspath) != 0)
        return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                 _("Cannot move '%s' to '%s' because they "
                                   "are not in the same working copy"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool),
                                 svn_dirent_local_style(moved_to_abspath,
                                                        scratch_pool));
    }
  else
    moved_to_relpath = NULL;

  odb.moved_to_relpath = moved_to_relpath;
  odb.conflict = conflict;
  odb.work_items = work_items;
  odb.delete_dir_externals = delete_dir_externals;

  if (notify_func)
    {
      /* Perform the deletion operation (transactionally), perform any
         notifications necessary, and then clean out our temporary tables.  */
      odb.notify = TRUE;
      SVN_ERR(with_finalization(wcroot, local_relpath,
                                op_delete_txn, &odb,
                                do_delete_notify, NULL,
                                cancel_func, cancel_baton,
                                notify_func, notify_baton,
                                STMT_FINALIZE_DELETE,
                                scratch_pool));
    }
  else
    {
      /* Avoid the trigger work */
      odb.notify = FALSE;
      SVN_WC__DB_WITH_TXN(
                    delete_node(&odb, wcroot, local_relpath, scratch_pool),
                    wcroot);
    }

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_delete_many(svn_wc__db_t *db,
                          apr_array_header_t *targets,
                          svn_boolean_t delete_dir_externals,
                          const svn_skel_t *work_items,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  struct op_delete_many_baton_t odmb;
  int i;
  apr_pool_t *iterpool;

  odmb.rel_targets = apr_array_make(scratch_pool, targets->nelts,
                                    sizeof(const char *));
  odmb.work_items = work_items;
  odmb.delete_dir_externals = delete_dir_externals;
  iterpool = svn_pool_create(scratch_pool);
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db,
                                                APR_ARRAY_IDX(targets, 0,
                                                              const char *),
                                                scratch_pool, iterpool));
  VERIFY_USABLE_WCROOT(wcroot);
  for (i = 0; i < targets->nelts; i++)
    {
      const char *local_abspath = APR_ARRAY_IDX(targets, i, const char*);
      svn_wc__db_wcroot_t *target_wcroot;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&target_wcroot,
                                                    &local_relpath, db,
                                                    APR_ARRAY_IDX(targets, i,
                                                                  const char *),
                                                    scratch_pool, iterpool));
      VERIFY_USABLE_WCROOT(target_wcroot);
      SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

      /* Assert that all targets are within the same working copy. */
      SVN_ERR_ASSERT(wcroot->wc_id == target_wcroot->wc_id);

      APR_ARRAY_PUSH(odmb.rel_targets, const char *) = local_relpath;
      SVN_ERR(flush_entries(target_wcroot, local_abspath, svn_depth_infinity,
                            iterpool));

    }
  svn_pool_destroy(iterpool);

  /* Perform the deletion operation (transactionally), perform any
     notifications necessary, and then clean out our temporary tables.  */
  return svn_error_trace(with_finalization(wcroot, wcroot->abspath,
                                           op_delete_many_txn, &odmb,
                                           do_delete_notify, NULL,
                                           cancel_func, cancel_baton,
                                           notify_func, notify_baton,
                                           STMT_FINALIZE_DELETE,
                                           scratch_pool));
}

/* Helper function for read_info() to provide better diagnostics than just
   asserting.

   ### BH: Yes this code is ugly, and that is why I only introduce it in
   ### read_info(). But we really need something to determine the root cause
   ### of this problem to diagnose why TortoiseSVN users were seeing all those
   ### assertions.

   Adds an error to the *err chain if invalid values are encountered. In that
   case the value is set to the first value in the map, assuming that caller
   will just return the combined error.
 */
static int
column_token_err(svn_error_t **err,
                 svn_sqlite__stmt_t *stmt,
                 int column,
                 const svn_token_map_t *map)
{
  svn_error_t *err2;
  const char *word = svn_sqlite__column_text(stmt, column, NULL);
  int value;

  /* svn_token__from_word_err() handles NULL for us */
  err2 = svn_token__from_word_err(&value, map, word);

  if (err2)
    {
      *err = svn_error_compose_create(
                *err,
                svn_error_createf(
                    SVN_ERR_WC_CORRUPT, err2,
                    _("Encountered invalid node state in column %d of "
                      "info query to working copy database"),
                    column));
      value = map[0].val;
    }

  return value;
}

/* Like svn_wc__db_read_info(), but taking WCROOT+LOCAL_RELPATH instead of
   DB+LOCAL_ABSPATH, and outputting repos ids instead of URL+UUID. */
static svn_error_t *
read_info(svn_wc__db_status_t *status,
          svn_node_kind_t *kind,
          svn_revnum_t *revision,
          const char **repos_relpath,
          apr_int64_t *repos_id,
          svn_revnum_t *changed_rev,
          apr_time_t *changed_date,
          const char **changed_author,
          svn_depth_t *depth,
          const svn_checksum_t **checksum,
          const char **target,
          const char **original_repos_relpath,
          apr_int64_t *original_repos_id,
          svn_revnum_t *original_revision,
          svn_wc__db_lock_t **lock,
          svn_filesize_t *recorded_size,
          apr_time_t *recorded_time,
          const char **changelist,
          svn_boolean_t *conflicted,
          svn_boolean_t *op_root,
          svn_boolean_t *had_props,
          svn_boolean_t *props_mod,
          svn_boolean_t *have_base,
          svn_boolean_t *have_more_work,
          svn_boolean_t *have_work,
          svn_wc__db_wcroot_t *wcroot,
          const char *local_relpath,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt_info;
  svn_sqlite__stmt_t *stmt_act;
  svn_boolean_t have_info;
  svn_boolean_t have_act;
  svn_error_t *err = NULL;

  /* Obtain the most likely to exist record first, to make sure we don't
     have to obtain the SQLite read-lock multiple times */
  SVN_ERR(svn_sqlite__get_statement(&stmt_info, wcroot->sdb,
                                    lock ? STMT_SELECT_NODE_INFO_WITH_LOCK
                                         : STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt_info, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_info, stmt_info));

  if (changelist || conflicted || props_mod)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt_act, wcroot->sdb,
                                        STMT_SELECT_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt_act, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_act, stmt_act));
    }
  else
    {
      have_act = FALSE;
      stmt_act = NULL;
    }

  if (have_info)
    {
      int op_depth;
      svn_node_kind_t node_kind;

      op_depth = svn_sqlite__column_int(stmt_info, 0);
      node_kind = column_token_err(&err, stmt_info, 4, kind_map);

      if (status)
        {
          *status = column_token_err(&err, stmt_info, 3, presence_map);

          if (op_depth != 0) /* WORKING */
            err = svn_error_compose_create(err,
                                           convert_to_working_status(status,
                                                                     *status));
        }
      if (kind)
        {
          *kind = node_kind;
        }
      if (op_depth != 0)
        {
          if (repos_id)
            *repos_id = INVALID_REPOS_ID;
          if (revision)
            *revision = SVN_INVALID_REVNUM;
          if (repos_relpath)
            /* Our path is implied by our parent somewhere up the tree.
               With the NULL value and status, the caller will know to
               search up the tree for the base of our path.  */
            *repos_relpath = NULL;
        }
      else
        {
          /* Fetch repository information. If we have a
             WORKING_NODE (and have been added), then the repository
             we're being added to will be dependent upon a parent. The
             caller can scan upwards to locate the repository.  */
          repos_location_from_columns(repos_id, revision, repos_relpath,
                                      stmt_info, 1, 5, 2, result_pool);
        }
      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt_info, 8);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt_info, 9);
        }
      if (changed_author)
        {
          *changed_author = svn_sqlite__column_text(stmt_info, 10,
                                                    result_pool);
        }
      if (recorded_time)
        {
          *recorded_time = svn_sqlite__column_int64(stmt_info, 13);
        }
      if (depth)
        {
          if (node_kind != svn_node_dir)
            *depth = svn_depth_unknown;
          else if (svn_sqlite__column_is_null(stmt_info, 11))
            *depth = svn_depth_unknown;
          else
            *depth = column_token_err(&err, stmt_info, 11, depth_map);
        }
      if (checksum)
        {
          if (node_kind != svn_node_file)
            {
              *checksum = NULL;
            }
          else
            {

              err = svn_error_compose_create(
                        err, svn_sqlite__column_checksum(checksum, stmt_info, 6,
                                                         result_pool));
            }
        }
      if (recorded_size)
        {
          *recorded_size = get_recorded_size(stmt_info, 7);
        }
      if (target)
        {
          if (node_kind != svn_node_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt_info, 12, result_pool);
        }
      if (changelist)
        {
          if (have_act)
            *changelist = svn_sqlite__column_text(stmt_act, 0, result_pool);
          else
            *changelist = NULL;
        }
      if (op_depth == 0)
        {
          if (original_repos_id)
            *original_repos_id = INVALID_REPOS_ID;
          if (original_revision)
            *original_revision = SVN_INVALID_REVNUM;
          if (original_repos_relpath)
            *original_repos_relpath = NULL;
        }
      else
        {
          repos_location_from_columns(original_repos_id,
                                      original_revision,
                                      original_repos_relpath,
                                      stmt_info, 1, 5, 2, result_pool);
        }
      if (props_mod)
        {
          *props_mod = have_act && !svn_sqlite__column_is_null(stmt_act, 1);
        }
      if (had_props)
        {
          *had_props = SQLITE_PROPERTIES_AVAILABLE(stmt_info, 14);
        }
      if (conflicted)
        {
          if (have_act)
            {
              *conflicted =
                 !svn_sqlite__column_is_null(stmt_act, 2); /* conflict_data */
            }
          else
            *conflicted = FALSE;
        }

      if (lock)
        {
          if (op_depth != 0)
            *lock = NULL;
          else
            *lock = lock_from_columns(stmt_info, 17, 18, 19, 20, result_pool);
        }

      if (have_work)
        *have_work = (op_depth != 0);

      if (op_root)
        {
          *op_root = ((op_depth > 0)
                      && (op_depth == relpath_depth(local_relpath)));
        }

      if (have_base || have_more_work)
        {
          if (have_more_work)
            *have_more_work = FALSE;

          while (!err && op_depth != 0)
            {
              err = svn_sqlite__step(&have_info, stmt_info);

              if (err || !have_info)
                break;

              op_depth = svn_sqlite__column_int(stmt_info, 0);

              if (have_more_work)
                {
                  if (op_depth > 0)
                    *have_more_work = TRUE;

                  if (!have_base)
                   break;
                }
            }

          if (have_base)
            *have_base = (op_depth == 0);
        }
    }
  else if (have_act)
    {
      /* A row in ACTUAL_NODE should never exist without a corresponding
         node in BASE_NODE and/or WORKING_NODE unless it flags a tree conflict. */
      if (svn_sqlite__column_is_null(stmt_act, 2)) /* conflict_data */
          err = svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                  _("Corrupt data for '%s'"),
                                  path_for_error_message(wcroot, local_relpath,
                                                         scratch_pool));
      /* ### What should we return?  Should we have a separate
             function for reading actual-only nodes? */

      /* As a safety measure, until we decide if we want to use
         read_info for actual-only nodes, make sure the caller asked
         for the conflict status. */
      SVN_ERR_ASSERT(conflicted);

      if (status)
        *status = svn_wc__db_status_normal;  /* What! No it's not! */
      if (kind)
        *kind = svn_node_unknown;
      if (revision)
        *revision = SVN_INVALID_REVNUM;
      if (repos_relpath)
        *repos_relpath = NULL;
      if (repos_id)
        *repos_id = INVALID_REPOS_ID;
      if (changed_rev)
        *changed_rev = SVN_INVALID_REVNUM;
      if (changed_date)
        *changed_date = 0;
      if (depth)
        *depth = svn_depth_unknown;
      if (checksum)
        *checksum = NULL;
      if (target)
        *target = NULL;
      if (original_repos_relpath)
        *original_repos_relpath = NULL;
      if (original_repos_id)
        *original_repos_id = INVALID_REPOS_ID;
      if (original_revision)
        *original_revision = SVN_INVALID_REVNUM;
      if (lock)
        *lock = NULL;
      if (recorded_size)
        *recorded_size = 0;
      if (recorded_time)
        *recorded_time = 0;
      if (changelist)
        *changelist = svn_sqlite__column_text(stmt_act, 0, result_pool);
      if (op_root)
        *op_root = FALSE;
      if (had_props)
        *had_props = FALSE;
      if (props_mod)
        *props_mod = FALSE;
      if (conflicted)
        *conflicted = TRUE;
      if (have_base)
        *have_base = FALSE;
      if (have_more_work)
        *have_more_work = FALSE;
      if (have_work)
        *have_work = FALSE;
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool));
    }

  if (stmt_act != NULL)
    err = svn_error_compose_create(err, svn_sqlite__reset(stmt_act));

  if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
    err = svn_error_quick_wrapf(err, _("Error reading node '%s'"),
                                local_relpath);

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt_info)));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_info_internal(svn_wc__db_status_t *status,
                              svn_node_kind_t *kind,
                              svn_revnum_t *revision,
                              const char **repos_relpath,
                              apr_int64_t *repos_id,
                              svn_revnum_t *changed_rev,
                              apr_time_t *changed_date,
                              const char **changed_author,
                              svn_depth_t *depth,
                              const svn_checksum_t **checksum,
                              const char **target,
                              const char **original_repos_relpath,
                              apr_int64_t *original_repos_id,
                              svn_revnum_t *original_revision,
                              svn_wc__db_lock_t **lock,
                              svn_filesize_t *recorded_size,
                              apr_time_t *recorded_time,
                              const char **changelist,
                              svn_boolean_t *conflicted,
                              svn_boolean_t *op_root,
                              svn_boolean_t *had_props,
                              svn_boolean_t *props_mod,
                              svn_boolean_t *have_base,
                              svn_boolean_t *have_more_work,
                              svn_boolean_t *have_work,
                              svn_wc__db_wcroot_t *wcroot,
                              const char *local_relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           read_info(status, kind, revision, repos_relpath, repos_id,
                     changed_rev, changed_date, changed_author,
                     depth, checksum, target, original_repos_relpath,
                     original_repos_id, original_revision, lock,
                     recorded_size, recorded_time, changelist, conflicted,
                     op_root, had_props, props_mod,
                     have_base, have_more_work, have_work,
                     wcroot, local_relpath, result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_read_info(svn_wc__db_status_t *status,
                     svn_node_kind_t *kind,
                     svn_revnum_t *revision,
                     const char **repos_relpath,
                     const char **repos_root_url,
                     const char **repos_uuid,
                     svn_revnum_t *changed_rev,
                     apr_time_t *changed_date,
                     const char **changed_author,
                     svn_depth_t *depth,
                     const svn_checksum_t **checksum,
                     const char **target,
                     const char **original_repos_relpath,
                     const char **original_root_url,
                     const char **original_uuid,
                     svn_revnum_t *original_revision,
                     svn_wc__db_lock_t **lock,
                     svn_filesize_t *recorded_size,
                     apr_time_t *recorded_time,
                     const char **changelist,
                     svn_boolean_t *conflicted,
                     svn_boolean_t *op_root,
                     svn_boolean_t *have_props,
                     svn_boolean_t *props_mod,
                     svn_boolean_t *have_base,
                     svn_boolean_t *have_more_work,
                     svn_boolean_t *have_work,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id, original_repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN4(
          read_info(status, kind, revision, repos_relpath, &repos_id,
                    changed_rev, changed_date, changed_author,
                    depth, checksum, target, original_repos_relpath,
                    &original_repos_id, original_revision, lock,
                    recorded_size, recorded_time, changelist, conflicted,
                    op_root, have_props, props_mod,
                    have_base, have_more_work, have_work,
                    wcroot, local_relpath, result_pool, scratch_pool),
          svn_wc__db_fetch_repos_info(repos_root_url, repos_uuid,
                                      wcroot, repos_id, result_pool),
          svn_wc__db_fetch_repos_info(original_root_url, original_uuid,
                                      wcroot, original_repos_id,
                                      result_pool),
        SVN_NO_ERROR,
        wcroot);

  return SVN_NO_ERROR;
}

static svn_error_t *
is_wclocked(svn_boolean_t *locked,
            svn_wc__db_wcroot_t *wcroot,
            const char *dir_relpath,
            apr_pool_t *scratch_pool);

/* Helper for read_children_info and single variant */
static svn_error_t *
find_conflict_descendants(svn_boolean_t *conflict_exists,
                          svn_wc__db_wcroot_t *wcroot,
                          const char *local_relpath,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* Only used on files, so certainly not wcroot*/
  assert(local_relpath[0] != '\0');

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_FIND_CONFLICT_DESCENDANT));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(conflict_exists, stmt));

  return svn_error_trace(svn_sqlite__reset(stmt));
}

/* What we really want to store about a node.  This relies on the
   offset of svn_wc__db_info_t being zero. */
struct read_children_info_item_t
{
  struct svn_wc__db_info_t info;
  int op_depth;
  int nr_layers;
  svn_boolean_t was_dir;
};

/* Implementation of svn_wc__db_read_children_info */
static svn_error_t *
read_children_info(svn_wc__db_wcroot_t *wcroot,
                   const char *dir_relpath,
                   apr_hash_t *conflicts,
                   apr_hash_t *nodes,
                   svn_boolean_t base_tree_only,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *repos_root_url = NULL;
  const char *repos_uuid = NULL;
  apr_int64_t last_repos_id = INVALID_REPOS_ID;
  const char *last_repos_root_url = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    (base_tree_only
                                     ? STMT_SELECT_BASE_NODE_CHILDREN_INFO
                                     : STMT_SELECT_NODE_CHILDREN_INFO)));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      /* CHILD item points to what we have about the node. We only provide
         CHILD->item to our caller. */
      struct read_children_info_item_t *child_item;
      const char *child_relpath = svn_sqlite__column_text(stmt, 19, NULL);
      const char *name = svn_relpath_basename(child_relpath, NULL);
      svn_error_t *err;
      int op_depth;
      svn_boolean_t new_child;

      child_item = (base_tree_only ? NULL : svn_hash_gets(nodes, name));
      if (child_item)
        new_child = FALSE;
      else
        {
          child_item = apr_pcalloc(result_pool, sizeof(*child_item));
          new_child = TRUE;
        }

      op_depth = svn_sqlite__column_int(stmt, 0);

      /* Do we have new or better information? */
      if (new_child)
        {
          struct svn_wc__db_info_t *child = &child_item->info;
          child_item->op_depth = op_depth;

          child->kind = svn_sqlite__column_token(stmt, 4, kind_map);

          child->status = svn_sqlite__column_token(stmt, 3, presence_map);
          if (op_depth != 0)
            {
              if (child->status == svn_wc__db_status_incomplete)
                child->incomplete = TRUE;
              err = convert_to_working_status(&child->status, child->status);
              if (err)
                SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
            }

          if (op_depth != 0)
            child->revnum = SVN_INVALID_REVNUM;
          else
            child->revnum = svn_sqlite__column_revnum(stmt, 5);

          if (op_depth != 0)
            child->repos_relpath = NULL;
          else
            child->repos_relpath = svn_sqlite__column_text(stmt, 2,
                                                           result_pool);

          if (op_depth != 0 || svn_sqlite__column_is_null(stmt, 1))
            {
              child->repos_root_url = NULL;
              child->repos_uuid = NULL;
            }
          else
            {
              apr_int64_t repos_id = svn_sqlite__column_int64(stmt, 1);
              if (!repos_root_url ||
                  (last_repos_id != INVALID_REPOS_ID &&
                   repos_id != last_repos_id))
                {
                  last_repos_root_url = repos_root_url;
                  err = svn_wc__db_fetch_repos_info(&repos_root_url,
                                                    &repos_uuid,
                                                    wcroot, repos_id,
                                                    result_pool);
                  if (err)
                    SVN_ERR(svn_error_compose_create(err,
                                                 svn_sqlite__reset(stmt)));
                }

              if (last_repos_id == INVALID_REPOS_ID)
                last_repos_id = repos_id;

              /* Assume working copy is all one repos_id so that a
                 single cached value is sufficient. */
              if (repos_id != last_repos_id)
                {
                  err= svn_error_createf(
                         SVN_ERR_WC_DB_ERROR, NULL,
                         _("The node '%s' comes from unexpected repository "
                           "'%s', expected '%s'; if this node is a file "
                           "external using the correct URL in the external "
                           "definition can fix the problem, see issue #4087"),
                         child_relpath, repos_root_url, last_repos_root_url);
                  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
                }
              child->repos_root_url = repos_root_url;
              child->repos_uuid = repos_uuid;
            }

          child->changed_rev = svn_sqlite__column_revnum(stmt, 8);

          child->changed_date = svn_sqlite__column_int64(stmt, 9);

          child->changed_author = svn_sqlite__column_text(stmt, 10,
                                                          result_pool);

          if (child->kind != svn_node_dir)
            child->depth = svn_depth_unknown;
          else
            {
              child->has_descendants = TRUE;
              child_item->was_dir = TRUE;
              child->depth = svn_sqlite__column_token_null(stmt, 11, depth_map,
                                                           svn_depth_unknown);
              if (new_child)
                {
                  err = is_wclocked(&child->locked, wcroot, child_relpath,
                                    scratch_pool);

                  if (err)
                    SVN_ERR(svn_error_compose_create(err,
                                                     svn_sqlite__reset(stmt)));
                }
            }

          child->recorded_time = svn_sqlite__column_int64(stmt, 13);
          child->recorded_size = get_recorded_size(stmt, 7);
          child->has_checksum = !svn_sqlite__column_is_null(stmt, 6);
          child->copied = op_depth > 0 && !svn_sqlite__column_is_null(stmt, 2);
          child->had_props = SQLITE_PROPERTIES_AVAILABLE(stmt, 14);
#ifdef HAVE_SYMLINK
          if (child->had_props)
            {
              apr_hash_t *properties;
              err = svn_sqlite__column_properties(&properties, stmt, 14,
                                                  scratch_pool, scratch_pool);
              if (err)
                SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

              child->special = (child->had_props
                                && svn_hash_gets(properties, SVN_PROP_SPECIAL));
            }
#endif
          if (op_depth == 0)
            child->op_root = FALSE;
          else
            child->op_root = (op_depth == relpath_depth(child_relpath));

          if (op_depth && child->op_root)
            child_item->info.moved_here = svn_sqlite__column_boolean(stmt, 20);

          if (new_child)
            svn_hash_sets(nodes, apr_pstrdup(result_pool, name), child);
        }
      else if (!child_item->was_dir
               && svn_sqlite__column_token(stmt, 4, kind_map) == svn_node_dir)
        {
          child_item->was_dir = TRUE;

          err = find_conflict_descendants(&child_item->info.has_descendants,
                                          wcroot, child_relpath,
                                          scratch_pool);
          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
        }

      if (op_depth == 0)
        {
          child_item->info.have_base = TRUE;

          /* Get the lock info, available only at op_depth 0. */
          child_item->info.lock = lock_from_columns(stmt, 15, 16, 17, 18,
                                                    result_pool);

          /* FILE_EXTERNAL flag only on op_depth 0. */
          child_item->info.file_external = svn_sqlite__column_boolean(stmt,
                                                                      22);
        }
      else
        {
          const char *moved_to_relpath;

          child_item->nr_layers++;
          child_item->info.have_more_work = (child_item->nr_layers > 1);


          /* A local_relpath can be moved multiple times at different op
             depths and it really depends on the caller what is interesting.
             We provide a simple linked list with the moved_from information */

          moved_to_relpath = svn_sqlite__column_text(stmt, 21, NULL);
          if (moved_to_relpath)
            {
              struct svn_wc__db_moved_to_info_t *moved_to;
              struct svn_wc__db_moved_to_info_t **next;
              const char *shadow_op_relpath;

              moved_to = apr_pcalloc(result_pool, sizeof(*moved_to));
              moved_to->moved_to_abspath = svn_dirent_join(wcroot->abspath,
                                                           moved_to_relpath,
                                                           result_pool);

              shadow_op_relpath = svn_relpath_prefix(child_relpath, op_depth,
                                                     scratch_pool);

              moved_to->shadow_op_root_abspath =
                        svn_dirent_join(wcroot->abspath, shadow_op_relpath,
                                        result_pool);

              next = &child_item->info.moved_to;

              while (*next &&
                     0 < strcmp((*next)->shadow_op_root_abspath,
                                moved_to->shadow_op_root_abspath))
                next = &((*next)->next);

              moved_to->next = *next;
              *next = moved_to;
            }
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (!base_tree_only)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_ACTUAL_CHILDREN_INFO));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      while (have_row)
        {
          struct read_children_info_item_t *child_item;
          struct svn_wc__db_info_t *child;
          const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
          const char *name = svn_relpath_basename(child_relpath, NULL);

          child_item = svn_hash_gets(nodes, name);
          if (!child_item)
            {
              child_item = apr_pcalloc(result_pool, sizeof(*child_item));
              child_item->info.status = svn_wc__db_status_not_present;
            }

          child = &child_item->info;

          child->changelist = svn_sqlite__column_text(stmt, 1, result_pool);

          child->props_mod = !svn_sqlite__column_is_null(stmt, 2);
#ifdef HAVE_SYMLINK
          if (child->props_mod)
            {
              svn_error_t *err;
              apr_hash_t *properties;

              err = svn_sqlite__column_properties(&properties, stmt, 2,
                                                  scratch_pool, scratch_pool);
              if (err)
                SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
              child->special = (NULL != svn_hash_gets(properties,
                                                      SVN_PROP_SPECIAL));
            }
#endif

          /* conflict */
          child->conflicted = !svn_sqlite__column_is_null(stmt, 3);

          if (child->conflicted)
            svn_hash_sets(conflicts, apr_pstrdup(result_pool, name), "");

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }

      SVN_ERR(svn_sqlite__reset(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_children_info(apr_hash_t **nodes,
                              apr_hash_t **conflicts,
                              svn_wc__db_t *db,
                              const char *dir_abspath,
                              svn_boolean_t base_tree_only,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;

  *conflicts = apr_hash_make(result_pool);
  *nodes = apr_hash_make(result_pool);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath, db,
                                                dir_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    read_children_info(wcroot, dir_relpath, *conflicts, *nodes,
                       base_tree_only, result_pool, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

/* Implementation of svn_wc__db_read_single_info.

   ### This function is very similar to a lot of code inside
   read_children_info, but that performs some tricks to re-use data between
   different siblings.

   ### We read the same few NODES records a few times via different helper
   functions, so this could be optimized bit, but everything is within
   a sqlite transaction and all queries are backed by an index, so generally
   everything (including the used indexes) should be in the sqlite page cache
   after the first query.
*/
static svn_error_t *
read_single_info(const struct svn_wc__db_info_t **info,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 svn_boolean_t base_tree_only,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct svn_wc__db_info_t *mtb;
  apr_int64_t repos_id;
  const svn_checksum_t *checksum;
  const char *original_repos_relpath;
  svn_boolean_t have_work;
  apr_hash_t *properties;

  mtb = apr_pcalloc(result_pool, sizeof(*mtb));

  if (!base_tree_only)
    SVN_ERR(read_info(&mtb->status, &mtb->kind, &mtb->revnum,
                      &mtb->repos_relpath, &repos_id, &mtb->changed_rev,
                      &mtb->changed_date, &mtb->changed_author, &mtb->depth,
                      &checksum, NULL, &original_repos_relpath, NULL, NULL,
                      &mtb->lock, &mtb->recorded_size, &mtb->recorded_time,
                      &mtb->changelist, &mtb->conflicted, &mtb->op_root,
                      &mtb->had_props, &mtb->props_mod, &mtb->have_base,
                      &mtb->have_more_work, &have_work,
                      wcroot, local_relpath, result_pool, scratch_pool));
  else
    {
      svn_boolean_t update_root;

      have_work = FALSE;
      original_repos_relpath = NULL;

      SVN_ERR(svn_wc__db_base_get_info_internal(
                  &mtb->status, &mtb->kind, &mtb->revnum, &mtb->repos_relpath,
                  &repos_id, &mtb->changed_rev, &mtb->changed_date,
                  &mtb->changed_author, &mtb->depth, &checksum, NULL,
                  &mtb->lock, &mtb->had_props, &properties, &update_root,
                  wcroot, local_relpath, scratch_pool, scratch_pool));

      mtb->have_base = TRUE;
      mtb->file_external = (update_root && mtb->kind == svn_node_file);
    }

  /* Query the same rows in the database again for move information */
  if (have_work && (mtb->have_base || mtb->have_more_work))
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_MOVED_TO_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      while (have_row)
        {
          struct svn_wc__db_moved_to_info_t *move;
          int op_depth = svn_sqlite__column_int(stmt, 0);
          const char *moved_to_relpath = svn_sqlite__column_text(stmt, 1, NULL);
          const char *cur_relpath;

          move = apr_pcalloc(result_pool, sizeof(*move));
          move->moved_to_abspath = svn_dirent_join(wcroot->abspath,
                                                   moved_to_relpath,
                                                   result_pool);

          cur_relpath = svn_relpath_prefix(local_relpath, op_depth,
                                           scratch_pool);

          move->shadow_op_root_abspath = svn_dirent_join(wcroot->abspath,
                                                         cur_relpath,
                                                         result_pool);

          move->next = mtb->moved_to;
          mtb->moved_to = move;

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }

      SVN_ERR(svn_sqlite__reset(stmt));
    }

  /* Maybe we have to get some shadowed lock from BASE to make our test suite
     happy... (It might be completely unrelated, but...)
     This queries the same BASE row again, joined to the lock table */
  if (!base_tree_only && mtb->have_base
      && (have_work || mtb->kind == svn_node_file))
    {
      svn_boolean_t update_root;
      svn_wc__db_lock_t **lock_arg = NULL;

      if (have_work)
        lock_arg = &mtb->lock;

      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, lock_arg, NULL, NULL,
                                                &update_root,
                                                wcroot, local_relpath,
                                                result_pool, scratch_pool));

      mtb->file_external = (update_root && mtb->kind == svn_node_file);
    }

  if (mtb->status == svn_wc__db_status_added)
    {
      svn_wc__db_status_t status;

      SVN_ERR(scan_addition(&status, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                            NULL, NULL,
                            wcroot, local_relpath,
                            result_pool, scratch_pool));

      mtb->moved_here = (status == svn_wc__db_status_moved_here);
      mtb->incomplete = (status == svn_wc__db_status_incomplete);
    }

#ifdef HAVE_SYMLINK
  if (mtb->kind == svn_node_file
      && (mtb->had_props || mtb->props_mod
          || (base_tree_only && properties)))
    {
      if (!base_tree_only)
        {
          if (mtb->props_mod)
            SVN_ERR(svn_wc__db_read_props_internal(&properties,
                                                   wcroot, local_relpath,
                                                   scratch_pool, scratch_pool));
          else
            SVN_ERR(db_read_pristine_props(&properties, wcroot, local_relpath,
                                           TRUE /* deleted_ok */,
                                           scratch_pool, scratch_pool));
        }

      mtb->special = (NULL != svn_hash_gets(properties, SVN_PROP_SPECIAL));
    }
#endif

  mtb->has_checksum = (checksum != NULL);
  mtb->copied = (original_repos_relpath != NULL);

  SVN_ERR(svn_wc__db_fetch_repos_info(&mtb->repos_root_url, &mtb->repos_uuid,
                                      wcroot, repos_id, result_pool));

  if (!base_tree_only && mtb->kind == svn_node_dir)
    SVN_ERR(is_wclocked(&mtb->locked, wcroot, local_relpath, scratch_pool));

  if (mtb->kind == svn_node_dir)
    mtb->has_descendants = TRUE;
  else
    SVN_ERR(find_conflict_descendants(&mtb->has_descendants,
                                      wcroot, local_relpath, scratch_pool));

  *info = mtb;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_single_info(const struct svn_wc__db_info_t **info,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t base_tree_only,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(read_single_info(info, wcroot, local_relpath,
                                       base_tree_only,
                                       result_pool, scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_pristine_info(svn_wc__db_status_t *status,
                              svn_node_kind_t *kind,
                              svn_revnum_t *changed_rev,
                              apr_time_t *changed_date,
                              const char **changed_author,
                              svn_depth_t *depth,  /* dirs only */
                              const svn_checksum_t **checksum, /* files only */
                              const char **target, /* symlinks only */
                              svn_boolean_t *had_props,
                              apr_hash_t **props,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;
  int op_depth;
  svn_wc__db_status_t raw_status;
  svn_node_kind_t node_kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* Obtain the most likely to exist record first, to make sure we don't
     have to obtain the SQLite read-lock multiple times */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                               svn_sqlite__reset(stmt),
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }

  op_depth = svn_sqlite__column_int(stmt, 0);
  raw_status = svn_sqlite__column_token(stmt, 3, presence_map);

  if (op_depth > 0 && raw_status == svn_wc__db_status_base_deleted)
    {
      SVN_ERR(svn_sqlite__step_row(stmt));

      op_depth = svn_sqlite__column_int(stmt, 0);
      raw_status = svn_sqlite__column_token(stmt, 3, presence_map);
    }

  node_kind = svn_sqlite__column_token(stmt, 4, kind_map);

  if (status)
    {
      if (op_depth > 0)
        {
          err = svn_error_compose_create(err,
                                         convert_to_working_status(
                                                    status,
                                                    raw_status));
        }
      else
        *status = raw_status;
    }
  if (kind)
    {
      *kind = node_kind;
    }
  if (changed_rev)
    {
      *changed_rev = svn_sqlite__column_revnum(stmt, 8);
    }
  if (changed_date)
    {
      *changed_date = svn_sqlite__column_int64(stmt, 9);
    }
  if (changed_author)
    {
      *changed_author = svn_sqlite__column_text(stmt, 10,
                                                result_pool);
    }
  if (depth)
    {
      if (node_kind != svn_node_dir)
        {
          *depth = svn_depth_unknown;
        }
      else
        {
          *depth = svn_sqlite__column_token_null(stmt, 11, depth_map,
                                                 svn_depth_unknown);
        }
    }
  if (checksum)
    {
      if (node_kind != svn_node_file)
        {
          *checksum = NULL;
        }
      else
        {
          svn_error_t *err2;
          err2 = svn_sqlite__column_checksum(checksum, stmt, 6, result_pool);

          if (err2 != NULL)
            {
              if (err)
                err = svn_error_compose_create(
                         err,
                         svn_error_createf(
                               err->apr_err, err2,
                              _("The node '%s' has a corrupt checksum value."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool)));
              else
                err = err2;
            }
        }
    }
  if (target)
    {
      if (node_kind != svn_node_symlink)
        *target = NULL;
      else
        *target = svn_sqlite__column_text(stmt, 12, result_pool);
    }
  if (had_props)
    {
      *had_props = SQLITE_PROPERTIES_AVAILABLE(stmt, 14);
    }
  if (props)
    {
      if (raw_status == svn_wc__db_status_normal
          || raw_status == svn_wc__db_status_incomplete)
        {
          SVN_ERR(svn_sqlite__column_properties(props, stmt, 14,
                                                result_pool, scratch_pool));
          if (*props == NULL)
            *props = apr_hash_make(result_pool);
        }
      else
        {
          assert(svn_sqlite__column_is_null(stmt, 14));
          *props = NULL;
        }
    }

  return svn_error_trace(
            svn_error_compose_create(err,
                                     svn_sqlite__reset(stmt)));
}

svn_error_t *
svn_wc__db_read_children_walker_info(const apr_array_header_t **items,
                                     svn_wc__db_t *db,
                                     const char *dir_abspath,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *nodes;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath, db,
                                             dir_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_CHILDREN_WALKER_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  nodes = apr_array_make(result_pool, 16,
                          sizeof(struct svn_wc__db_walker_info_t *));
  while (have_row)
    {
      struct svn_wc__db_walker_info_t *child;
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *name = svn_relpath_basename(child_relpath, result_pool);
      int op_depth = svn_sqlite__column_int(stmt, 1);
      svn_error_t *err;

      child = apr_palloc(result_pool, sizeof(*child));
      child->name = name;
      child->status = svn_sqlite__column_token(stmt, 2, presence_map);
      if (op_depth > 0)
        {
          err = convert_to_working_status(&child->status, child->status);
          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
        }
      child->kind = svn_sqlite__column_token(stmt, 3, kind_map);

      APR_ARRAY_PUSH(nodes, struct svn_wc__db_walker_info_t *) = child;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  *items = nodes;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_node_install_info(const char **wcroot_abspath,
                                  const svn_checksum_t **sha1_checksum,
                                  apr_hash_t **pristine_props,
                                  apr_time_t *changed_date,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const char *wri_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err = NULL;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  if (!wri_abspath)
    wri_abspath = local_abspath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (local_abspath != wri_abspath
      && strcmp(local_abspath, wri_abspath))
    {
      if (!svn_dirent_is_ancestor(wcroot->abspath, local_abspath))
        return svn_error_createf(
                    SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                    _("The node '%s' is not in working copy '%s'"),
                    svn_dirent_local_style(local_abspath, scratch_pool),
                    svn_dirent_local_style(wcroot->abspath, scratch_pool));

      local_relpath = svn_dirent_skip_ancestor(wcroot->abspath, local_abspath);
    }

  if (wcroot_abspath != NULL)
    *wcroot_abspath = apr_pstrdup(result_pool, wcroot->abspath);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      if (sha1_checksum)
        err = svn_sqlite__column_checksum(sha1_checksum, stmt, 6, result_pool);

      if (!err && pristine_props)
        {
          err = svn_sqlite__column_properties(pristine_props, stmt, 14,
                                              result_pool, scratch_pool);
          /* Null means no props (assuming presence normal or incomplete). */
          if (*pristine_props == NULL)
            *pristine_props = apr_hash_make(result_pool);
        }

      if (changed_date)
        *changed_date = svn_sqlite__column_int64(stmt, 9);
    }
  else
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("The node '%s' is not installable"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

  return SVN_NO_ERROR;
}



/* The body of svn_wc__db_read_repos_info().
 */
static svn_error_t *
db_read_repos_info(svn_revnum_t *revision,
                   const char **repos_relpath,
                   apr_int64_t *repos_id,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  SVN_ERR(read_info(&status, NULL, revision, repos_relpath, repos_id, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL,
                    wcroot, local_relpath, result_pool, scratch_pool));

  if ((repos_relpath && !*repos_relpath)
      || (repos_id && *repos_id == INVALID_REPOS_ID))
    {
      if (status == svn_wc__db_status_added)
        {
          SVN_ERR(scan_addition(NULL, NULL, repos_relpath, repos_id, NULL,
                                NULL, NULL, NULL, NULL, NULL,
                                wcroot, local_relpath,
                                result_pool, scratch_pool));
        }
      else if (status == svn_wc__db_status_deleted)
        {
          const char *base_del_relpath;
          const char *work_del_relpath;

          SVN_ERR(scan_deletion(&base_del_relpath, NULL,
                                &work_del_relpath,
                                NULL, wcroot,
                                local_relpath,
                                scratch_pool,
                                scratch_pool));

          if (work_del_relpath)
            {
              /* The parent of the WORKING delete, must be an addition */
              const char *work_relpath = NULL;

              /* work_del_relpath should not be NULL. However, we have
               * observed instances where that assumption was not met.
               * Bail out in that case instead of crashing with a segfault.
               */
              SVN_ERR_ASSERT(work_del_relpath != NULL);
              work_relpath = svn_relpath_dirname(work_del_relpath,
                                                 scratch_pool);

              SVN_ERR(scan_addition(NULL, NULL, repos_relpath, repos_id,
                                    NULL, NULL, NULL, NULL, NULL, NULL,
                                    wcroot, work_relpath,
                                    scratch_pool, scratch_pool));

              if (repos_relpath)
                *repos_relpath = svn_relpath_join(
                                    *repos_relpath,
                                    svn_dirent_skip_ancestor(work_relpath,
                                                             local_relpath),
                                    result_pool);
            }
          else if (base_del_relpath)
            {
              SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, revision,
                                                        repos_relpath,
                                                        repos_id,
                                                        NULL, NULL, NULL,
                                                        NULL, NULL, NULL,
                                                        NULL, NULL, NULL, NULL,
                                                        wcroot,
                                                        base_del_relpath,
                                                        scratch_pool,
                                                        scratch_pool));

              if (repos_relpath)
                *repos_relpath = svn_relpath_join(
                                    *repos_relpath,
                                    svn_dirent_skip_ancestor(base_del_relpath,
                                                             local_relpath),
                                    result_pool);
            }
          else
            SVN_ERR_MALFUNCTION();
        }
      else if (status == svn_wc__db_status_excluded)
        {
          const char *parent_relpath;
          const char *name;

          /* A BASE excluded would have had repository information, so
             we have a working exclude, which must be below an addition */

          svn_relpath_split(&parent_relpath, &name, local_relpath,
                            scratch_pool);
          SVN_ERR(scan_addition(NULL, NULL, repos_relpath, repos_id, NULL,
                                NULL, NULL, NULL, NULL, NULL,
                                wcroot, parent_relpath,
                                scratch_pool, scratch_pool));

          if (repos_relpath)
            *repos_relpath = svn_relpath_join(*repos_relpath, name,
                                              result_pool);

          return SVN_NO_ERROR;
        }
      else
        {
          /* All working statee are explicitly handled and all base statee
             have a repos_relpath */
          SVN_ERR_MALFUNCTION();
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_repos_info(svn_revnum_t *revision,
                           const char **repos_relpath,
                           const char **repos_root_url,
                           const char **repos_uuid,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id = INVALID_REPOS_ID;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN4(db_read_repos_info(revision, repos_relpath,
                                          (repos_root_url || repos_uuid)
                                            ? &repos_id : NULL,
                                          wcroot, local_relpath,
                                          result_pool, scratch_pool),
                       svn_wc__db_fetch_repos_info(repos_root_url,
                                                   repos_uuid,
                                                   wcroot, repos_id,
                                                   result_pool),
                       SVN_NO_ERROR, SVN_NO_ERROR,
                       wcroot);

  return SVN_NO_ERROR;
}


/* Call RECEIVER_FUNC, passing RECEIVER_BATON, an absolute path, and
   a hash table mapping <tt>char *</tt> names onto svn_string_t *
   values for any properties of immediate or recursive child nodes of
   LOCAL_ABSPATH, the actual query being determined by STMT_IDX.
   If FILES_ONLY is true, only report properties for file child nodes.
   Check for cancellation between calls of RECEIVER_FUNC.
*/
typedef struct cache_props_baton_t
{
  svn_depth_t depth;
  svn_boolean_t pristine;
  const apr_array_header_t *changelists;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} cache_props_baton_t;


static svn_error_t *
cache_props_recursive(void *cb_baton,
                      svn_wc__db_wcroot_t *wcroot,
                      const char *local_relpath,
                      apr_pool_t *scratch_pool)
{
  cache_props_baton_t *baton = cb_baton;
  svn_sqlite__stmt_t *stmt;
  int stmt_idx;

  SVN_ERR(populate_targets_tree(wcroot, local_relpath, baton->depth,
                                baton->changelists, scratch_pool));

  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_TARGET_PROP_CACHE));

  if (baton->pristine)
    stmt_idx = STMT_CACHE_TARGET_PRISTINE_PROPS;
  else
    stmt_idx = STMT_CACHE_TARGET_PROPS;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, wcroot->wc_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_props_streamily(svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_depth_t depth,
                                svn_boolean_t pristine,
                                const apr_array_header_t *changelists,
                                svn_wc__proplist_receiver_t receiver_func,
                                void *receiver_baton,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  cache_props_baton_t baton;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;
  svn_error_t *err = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(receiver_func);
  SVN_ERR_ASSERT((depth == svn_depth_files) ||
                 (depth == svn_depth_immediates) ||
                 (depth == svn_depth_infinity));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  baton.depth = depth;
  baton.pristine = pristine;
  baton.changelists = changelists;
  baton.cancel_func = cancel_func;
  baton.cancel_baton = cancel_baton;

  SVN_ERR(with_finalization(wcroot, local_relpath,
                            cache_props_recursive, &baton,
                            NULL, NULL,
                            cancel_func, cancel_baton,
                            NULL, NULL,
                            STMT_DROP_TARGETS_LIST,
                            scratch_pool));

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ALL_TARGET_PROP_CACHE));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (!err && have_row)
    {
      apr_hash_t *props;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_sqlite__column_properties(&props, stmt, 1, iterpool,
                                            iterpool));

      /* see if someone wants to cancel this operation. */
      if (cancel_func)
        err = cancel_func(cancel_baton);

      if (!err && props && apr_hash_count(props) != 0)
        {
          const char *child_relpath;
          const char *child_abspath;

          child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
          child_abspath = svn_dirent_join(wcroot->abspath,
                                          child_relpath, iterpool);

          err = receiver_func(receiver_baton, child_abspath, props, iterpool);
        }

      err = svn_error_compose_create(err, svn_sqlite__step(&have_row, stmt));
    }

  err = svn_error_compose_create(err, svn_sqlite__reset(stmt));

  svn_pool_destroy(iterpool);

  SVN_ERR(svn_error_compose_create(
                    err,
                    svn_sqlite__exec_statements(wcroot->sdb,
                                                STMT_DROP_TARGET_PROP_CACHE)));
  return SVN_NO_ERROR;
}


/* Helper for svn_wc__db_read_props().
 */
svn_error_t *
svn_wc__db_read_props_internal(apr_hash_t **props,
                               svn_wc__db_wcroot_t *wcroot,
                               const char *local_relpath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row && !svn_sqlite__column_is_null(stmt, 0))
    {
      err = svn_sqlite__column_properties(props, stmt, 0,
                                          result_pool, scratch_pool);
    }
  else
    have_row = FALSE;

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

  if (have_row)
    return SVN_NO_ERROR;

  /* No local changes. Return the pristine props for this node.  */
  SVN_ERR(db_read_pristine_props(props, wcroot, local_relpath, FALSE,
                                 result_pool, scratch_pool));
  if (*props == NULL)
    {
      /* Pristine properties are not defined for this node.
         ### we need to determine whether this node is in a state that
         ### allows for ACTUAL properties (ie. not deleted). for now,
         ### just say all nodes, no matter the state, have at least an
         ### empty set of props.  */
      *props = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_props(apr_hash_t **props,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(svn_wc__db_read_props_internal(props, wcroot,
                                                     local_relpath,
                                                     result_pool,
                                                     scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}


static svn_error_t *
db_read_pristine_props(apr_hash_t **props,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       svn_boolean_t deleted_ok,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_wc__db_status_t presence;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_SELECT_NODE_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                               svn_sqlite__reset(stmt),
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }


  /* Examine the presence: */
  presence = svn_sqlite__column_token(stmt, 1, presence_map);

  /* For "base-deleted", it is obvious the pristine props are located
     below the current node. Fetch the NODE from the next record. */
  if (presence == svn_wc__db_status_base_deleted && deleted_ok)
    {
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      SVN_ERR_ASSERT(have_row);

      presence = svn_sqlite__column_token(stmt, 1, presence_map);
    }

  /* normal or copied: Fetch properties (during update we want
     properties for incomplete as well) */
  if (presence == svn_wc__db_status_normal
      || presence == svn_wc__db_status_incomplete)
    {
      svn_error_t *err;

      err = svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool);
      SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

      if (!*props)
        *props = apr_hash_make(result_pool);

      return SVN_NO_ERROR;
    }

  return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                           svn_sqlite__reset(stmt),
                           _("The node '%s' has a status that"
                             " has no properties."),
                           path_for_error_message(wcroot,
                                                  local_relpath,
                                                  scratch_pool));
}


svn_error_t *
svn_wc__db_read_pristine_props(apr_hash_t **props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(db_read_pristine_props(props, wcroot, local_relpath, TRUE,
                                 result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_prop_retrieve_recursive(apr_hash_t **values,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   const char *propname,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_CURRENT_PROPS_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  *values = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
  {
    apr_hash_t *node_props;
    svn_string_t *value;

    svn_pool_clear(iterpool);

    SVN_ERR(svn_sqlite__column_properties(&node_props, stmt, 0,
                                          iterpool, iterpool));

    value = (node_props
                ? svn_hash_gets(node_props, propname)
                : NULL);

    if (value)
      {
        svn_hash_sets(*values,
                      svn_dirent_join(wcroot->abspath,
                                      svn_sqlite__column_text(stmt, 1, NULL),
                                      result_pool),
                      svn_string_dup(value, result_pool));
      }

    SVN_ERR(svn_sqlite__step(&have_row, stmt));
  }

  svn_pool_destroy(iterpool);

  return svn_error_trace(svn_sqlite__reset(stmt));
}

/* Remove all prop name value pairs from PROP_HASH where the property
   name is not PROPNAME. */
static void
filter_unwanted_props(apr_hash_t *prop_hash,
                      const char * propname,
                      apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, prop_hash);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *ipropname = apr_hash_this_key(hi);

      if (strcmp(ipropname, propname) != 0)
        svn_hash_sets(prop_hash, ipropname, NULL);
    }
  return;
}

/* Get the changed properties as stored in the ACTUAL table */
static svn_error_t *
db_get_changed_props(apr_hash_t **actual_props,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                STMT_SELECT_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row && !svn_sqlite__column_is_null(stmt, 0))
    SVN_ERR(svn_sqlite__column_properties(actual_props, stmt, 0,
                                          result_pool, scratch_pool));
  else
    *actual_props = NULL; /* Cached when we read that record */

  return svn_error_trace(svn_sqlite__reset(stmt));
}

/* The body of svn_wc__db_read_inherited_props().  */
static svn_error_t *
db_read_inherited_props(apr_array_header_t **inherited_props,
                        apr_hash_t **actual_props,
                        svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        const char *propname,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *cached_iprops = NULL;
  apr_array_header_t *iprops;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_sqlite__stmt_t *stmt;
  const char *relpath;
  const char *expected_parent_repos_relpath = NULL;
  const char *parent_relpath;

  iprops = apr_array_make(result_pool, 1,
                           sizeof(svn_prop_inherited_item_t *));
  *inherited_props = iprops;

  if (actual_props)
    *actual_props = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));

  relpath = local_relpath;

  /* Walk up to the root of the WC looking for inherited properties.  When we
     reach the WC root also check for cached inherited properties. */
  for (relpath = local_relpath; relpath; relpath = parent_relpath)
    {
      svn_boolean_t have_row;
      int op_depth;
      svn_wc__db_status_t status;
      apr_hash_t *node_props;

      parent_relpath = relpath[0] ? svn_relpath_dirname(relpath, scratch_pool)
                                  : NULL;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, relpath));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        return svn_error_createf(
                    SVN_ERR_WC_PATH_NOT_FOUND, svn_sqlite__reset(stmt),
                    _("The node '%s' was not found."),
                    path_for_error_message(wcroot, relpath,
                                           scratch_pool));

      op_depth = svn_sqlite__column_int(stmt, 0);

      status = svn_sqlite__column_token(stmt, 3, presence_map);

      if (status != svn_wc__db_status_normal
          && status != svn_wc__db_status_incomplete)
        return svn_error_createf(
                    SVN_ERR_WC_PATH_UNEXPECTED_STATUS, svn_sqlite__reset(stmt),
                    _("The node '%s' has a status that has no properties."),
                    path_for_error_message(wcroot, relpath,
                                           scratch_pool));

      if (op_depth > 0)
        {
          /* WORKING node. Nothing to check */
        }
      else if (expected_parent_repos_relpath)
        {
          const char *repos_relpath = svn_sqlite__column_text(stmt, 2, NULL);

          if (strcmp(expected_parent_repos_relpath, repos_relpath) != 0)
            {
              /* The child of this node has a different parent than this node
                 (It is "switched"), so we can stop here. Note that switched
                 with the same parent is not interesting for us here. */
              SVN_ERR(svn_sqlite__reset(stmt));
              break;
            }

          expected_parent_repos_relpath =
              svn_relpath_dirname(expected_parent_repos_relpath, scratch_pool);
        }
      else
        {
          const char *repos_relpath = svn_sqlite__column_text(stmt, 2, NULL);

          expected_parent_repos_relpath =
              svn_relpath_dirname(repos_relpath, scratch_pool);
        }

      if (op_depth == 0
          && !svn_sqlite__column_is_null(stmt, 16))
        {
          /* The node contains a cache. No reason to look further */
          SVN_ERR(svn_sqlite__column_iprops(&cached_iprops, stmt, 16,
                                            result_pool, iterpool));

          parent_relpath = NULL; /* Stop after this */
        }

      SVN_ERR(svn_sqlite__column_properties(&node_props, stmt, 14,
                                            iterpool, iterpool));

      SVN_ERR(svn_sqlite__reset(stmt));

      /* If PARENT_ABSPATH is a parent of LOCAL_ABSPATH, then LOCAL_ABSPATH
         can inherit properties from it. */
      if (relpath != local_relpath)
        {
          apr_hash_t *changed_props;

          SVN_ERR(db_get_changed_props(&changed_props, wcroot, relpath,
                                       result_pool, iterpool));

          if (changed_props)
            node_props = changed_props;
          else if (node_props)
            node_props = svn_prop_hash_dup(node_props, result_pool);

          if (node_props && apr_hash_count(node_props))
            {
              /* If we only want PROPNAME filter out any other properties. */
              if (propname)
                filter_unwanted_props(node_props, propname, iterpool);

              if (apr_hash_count(node_props))
                {
                  svn_prop_inherited_item_t *iprop_elt =
                    apr_pcalloc(result_pool,
                                sizeof(svn_prop_inherited_item_t));
                  iprop_elt->path_or_url = svn_dirent_join(wcroot->abspath,
                                                           relpath,
                                                           result_pool);

                  iprop_elt->prop_hash = node_props;
                  /* Build the output array in depth-first order. */
                  svn_sort__array_insert(iprops, &iprop_elt, 0);
                }
            }
        }
      else if (actual_props)
        {
          apr_hash_t *changed_props;

          SVN_ERR(db_get_changed_props(&changed_props, wcroot, relpath,
                                       result_pool, iterpool));

          if (changed_props)
            *actual_props = changed_props;
          else if (node_props)
            *actual_props = svn_prop_hash_dup(node_props, result_pool);
        }
    }

  if (cached_iprops)
    {
      for (i = cached_iprops->nelts - 1; i >= 0; i--)
        {
          svn_prop_inherited_item_t *cached_iprop =
            APR_ARRAY_IDX(cached_iprops, i, svn_prop_inherited_item_t *);

          /* An empty property hash in the iprops cache means there are no
             inherited properties. */
          if (apr_hash_count(cached_iprop->prop_hash) == 0)
            continue;

          if (propname)
            filter_unwanted_props(cached_iprop->prop_hash, propname,
                                  scratch_pool);

          /* If we didn't filter everything then keep this iprop. */
          if (apr_hash_count(cached_iprop->prop_hash))
            svn_sort__array_insert(iprops, &cached_iprop, 0);
        }
    }

  if (actual_props && !*actual_props)
    *actual_props = apr_hash_make(result_pool);

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_inherited_props(apr_array_header_t **iprops,
                                apr_hash_t **actual_props,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *propname,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(db_read_inherited_props(iprops, actual_props,
                                              wcroot, local_relpath, propname,
                                              result_pool, scratch_pool),
                      wcroot);

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_get_children_with_cached_iprops().
 */
static svn_error_t *
get_children_with_cached_iprops(apr_hash_t **iprop_paths,
                                svn_wc__db_wcroot_t *wcroot,
                                const char *local_relpath,
                                svn_depth_t depth,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *iprop_paths = apr_hash_make(result_pool);

  /* First check if LOCAL_RELPATH itself has iprops */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_IPROPS_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
   {
      const char *relpath_with_cache = svn_sqlite__column_text(stmt, 0,
                                                               NULL);
      const char *abspath_with_cache = svn_dirent_join(wcroot->abspath,
                                                       relpath_with_cache,
                                                       result_pool);
      svn_hash_sets(*iprop_paths, abspath_with_cache,
                    svn_sqlite__column_text(stmt, 1, result_pool));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Now fetch information for children or all descendants */
  if (depth == svn_depth_files
      || depth == svn_depth_immediates)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_IPROPS_CHILDREN));
    }
  else /* Default to svn_depth_infinity. */
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_IPROPS_RECURSIVE));
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      const char *relpath_with_cache = svn_sqlite__column_text(stmt, 0,
                                                               NULL);
      const char *abspath_with_cache = svn_dirent_join(wcroot->abspath,
                                                       relpath_with_cache,
                                                       result_pool);
      svn_hash_sets(*iprop_paths, abspath_with_cache,
                    svn_sqlite__column_text(stmt, 1, result_pool));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* For depth files we should filter non files */
  if (depth == svn_depth_files)
    {
      apr_hash_index_t *hi;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      for (hi = apr_hash_first(scratch_pool, *iprop_paths);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *child_abspath = apr_hash_this_key(hi);
          const char *child_relpath;
          svn_node_kind_t child_kind;

          svn_pool_clear(iterpool);

          child_relpath = svn_dirent_is_child(local_relpath, child_abspath,
                                              NULL);

          if (! child_relpath)
            {
              continue; /* local_relpath itself */
            }

          SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &child_kind, NULL,
                                                    NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, NULL,
                                                    NULL, NULL, NULL, NULL,
                                                    wcroot, child_relpath,
                                                    scratch_pool,
                                                    scratch_pool));

          /* Filter if not a file */
          if (child_kind != svn_node_file)
            {
              svn_hash_sets(*iprop_paths, child_abspath, NULL);
            }
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_get_children_with_cached_iprops(apr_hash_t **iprop_paths,
                                           svn_depth_t depth,
                                           const char *local_abspath,
                                           svn_wc__db_t *db,
                                           apr_pool_t *result_pool,
                                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                                local_abspath, scratch_pool,
                                                scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    get_children_with_cached_iprops(iprop_paths, wcroot, local_relpath,
                                    depth, result_pool, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_children_of_working_node(const apr_array_header_t **children,
                                         svn_wc__db_t *db,
                                         const char *local_abspath,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(
          gather_children(children, wcroot, local_relpath,
                          STMT_SELECT_WORKING_CHILDREN, -1,
                          result_pool, scratch_pool));
}

svn_error_t *
svn_wc__db_base_read_not_present_children(
                                const apr_array_header_t **children,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(
          gather_children(children, wcroot, local_relpath,
                          STMT_SELECT_BASE_NOT_PRESENT_CHILDREN, -1,
                          result_pool, scratch_pool));
}

/* Helper for svn_wc__db_node_check_replace().
 */
static svn_error_t *
check_replace_txn(svn_boolean_t *is_replace_root_p,
                  svn_boolean_t *base_replace_p,
                  svn_boolean_t *is_replace_p,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_boolean_t is_replace = FALSE;
  int replaced_op_depth;
  svn_wc__db_status_t replaced_status;

  /* Our caller initialized the output values to FALSE */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  {
    svn_wc__db_status_t status;

    status = svn_sqlite__column_token(stmt, 3, presence_map);

    if (status != svn_wc__db_status_normal)
      return svn_error_trace(svn_sqlite__reset(stmt));
  }

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_trace(svn_sqlite__reset(stmt));

  replaced_status = svn_sqlite__column_token(stmt, 3, presence_map);

  /* If the layer below the add describes a not present or a deleted node,
     this is not a replacement. Deleted can only occur if an ancestor is
     the delete root. */
  if (replaced_status != svn_wc__db_status_not_present
      && replaced_status != svn_wc__db_status_excluded
      && replaced_status != svn_wc__db_status_server_excluded
      && replaced_status != svn_wc__db_status_base_deleted)
    {
      is_replace = TRUE;
      if (is_replace_p)
        *is_replace_p = TRUE;
    }

  replaced_op_depth = svn_sqlite__column_int(stmt, 0);

  if (base_replace_p)
    {
      int op_depth = svn_sqlite__column_int(stmt, 0);

      while (op_depth != 0 && have_row)
        {
          SVN_ERR(svn_sqlite__step(&have_row, stmt));

          if (have_row)
            op_depth = svn_sqlite__column_int(stmt, 0);
        }

      if (have_row && op_depth == 0)
        {
          svn_wc__db_status_t base_status;

          base_status = svn_sqlite__column_token(stmt, 3, presence_map);

          *base_replace_p = (base_status != svn_wc__db_status_not_present);
        }
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (!is_replace_root_p || !is_replace)
    return SVN_NO_ERROR;

  if (replaced_status != svn_wc__db_status_base_deleted)
    {
      int parent_op_depth;

      /* Check the current op-depth of the parent to see if we are a replacement
         root */
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                svn_relpath_dirname(local_relpath,
                                                    scratch_pool)));

      SVN_ERR(svn_sqlite__step_row(stmt)); /* Parent must exist as 'normal' */

      parent_op_depth = svn_sqlite__column_int(stmt, 0);

      if (parent_op_depth >= replaced_op_depth)
        {
          /* Did we replace inside our directory? */

          *is_replace_root_p = (parent_op_depth == replaced_op_depth);
          SVN_ERR(svn_sqlite__reset(stmt));
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (have_row)
        parent_op_depth = svn_sqlite__column_int(stmt, 0);

      SVN_ERR(svn_sqlite__reset(stmt));

      if (!have_row)
        *is_replace_root_p = TRUE; /* Parent is no replacement */
      else if (parent_op_depth < replaced_op_depth)
        *is_replace_root_p = TRUE; /* Parent replaces a lower layer */
      /*else // No replacement root */
  }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_node_check_replace(svn_boolean_t *is_replace_root,
                              svn_boolean_t *base_replace,
                              svn_boolean_t *is_replace,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (is_replace_root)
    *is_replace_root = FALSE;
  if (base_replace)
    *base_replace = FALSE;
  if (is_replace)
    *is_replace = FALSE;

  if (local_relpath[0] == '\0')
    return SVN_NO_ERROR; /* Working copy root can't be replaced */

  SVN_WC__DB_WITH_TXN(
    check_replace_txn(is_replace_root, base_replace, is_replace,
                      wcroot, local_relpath, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_children(const apr_array_header_t **children,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return gather_children(children, wcroot, local_relpath,
                         STMT_SELECT_NODE_CHILDREN, -1,
                         result_pool, scratch_pool);
}


/* Implementation of svn_wc__db_global_relocate */
static svn_error_t *
relocate_txn(svn_wc__db_wcroot_t *wcroot,
             const char *local_relpath,
             const char *repos_root_url,
             apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  apr_int64_t new_repos_id;
  const char *local_dir_relpath;
  svn_wc__db_status_t status;
  const char *repos_uuid;
  svn_boolean_t have_base_node;
  apr_int64_t old_repos_id;

  local_dir_relpath = local_relpath;

  SVN_ERR(read_info(&status,
                    NULL, NULL, NULL, &old_repos_id,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL,
                    &have_base_node, NULL, NULL,
                    wcroot, local_relpath,
                    scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_excluded)
    {
      /* The parent cannot be excluded, so look at the parent and then
         adjust the relpath */
      const char *parent_relpath = svn_relpath_dirname(local_dir_relpath,
                                                       scratch_pool);
      SVN_ERR(read_info(&status,
                        NULL, NULL, NULL, &old_repos_id,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL,
                        NULL, NULL, NULL,
                        wcroot, parent_relpath,
                        scratch_pool, scratch_pool));
      local_dir_relpath = parent_relpath;
    }

  if (old_repos_id == INVALID_REPOS_ID)
    {
      /* Do we need to support relocating something that is
         added/deleted/excluded without relocating the parent?  If not
         then perhaps relpath, root_url and uuid should be passed down
         to the children so that they don't have to scan? */

      if (status == svn_wc__db_status_deleted)
        {
          const char *work_del_relpath;

          SVN_ERR(scan_deletion(NULL, NULL,
                                &work_del_relpath, NULL,
                                wcroot, local_dir_relpath,
                                scratch_pool,
                                scratch_pool));
          if (work_del_relpath)
            {
              /* Deleted within a copy/move */

              /* The parent of the delete is added. */
              status = svn_wc__db_status_added;
              local_dir_relpath = svn_relpath_dirname(work_del_relpath,
                                                      scratch_pool);
            }
        }

      if (status == svn_wc__db_status_added)
        {
          SVN_ERR(scan_addition(NULL, NULL, NULL, &old_repos_id,
                                NULL, NULL, NULL, NULL, NULL, NULL,
                                wcroot, local_dir_relpath,
                                scratch_pool, scratch_pool));
        }
      else
        SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL, NULL,
                                                  &old_repos_id,
                                                  NULL, NULL, NULL, NULL, NULL,
                                                  NULL, NULL, NULL, NULL, NULL,
                                                  wcroot, local_dir_relpath,
                                                  scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_wc__db_fetch_repos_info(NULL, &repos_uuid, wcroot,
                                      old_repos_id, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid);

  /* This function affects all the children of the given local_relpath,
     but the way that it does this is through the repos inheritance mechanism.
     So, we only need to rewrite the repos_id of the given local_relpath,
     as well as any children with a non-null repos_id, as well as various
     repos_id fields in the locks and working_node tables.
   */

  /* Get the repos_id for the new repository. */
  SVN_ERR(create_repos_id(&new_repos_id, repos_root_url, repos_uuid,
                          wcroot->sdb, scratch_pool));

  /* Set the (base and working) repos_ids and clear the dav_caches */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_RECURSIVE_UPDATE_NODE_REPO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isii", wcroot->wc_id, local_relpath,
                            old_repos_id, new_repos_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (have_base_node)
    {
      /* Update any locks for the root or its children. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_LOCK_REPOS_ID));
      SVN_ERR(svn_sqlite__bindf(stmt, "ii", old_repos_id, new_repos_id));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* ### TODO: Update urls stored in inherited properties...
               What about urls in conflicts?
                 # We can probably keep these as they are only used
                   for showing full urls to the user */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_relocate(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           const char *repos_root_url,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                           db, local_dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    relocate_txn(wcroot, local_relpath, repos_root_url, scratch_pool),
    wcroot);

  SVN_ERR(flush_entries(wcroot, local_dir_abspath, svn_depth_infinity,
                        scratch_pool));

  return SVN_NO_ERROR;
}


/* Helper for commit_node()
   Set *REPOS_ID and *REPOS_RELPATH to the BASE repository location of
   (WCROOT, LOCAL_RELPATH), directly if its BASE row exists or implied from
   its parent's BASE row if not. In the latter case, error if the parent
   BASE row does not exist.  */
static svn_error_t *
determine_commit_repos_info(apr_int64_t *repos_id,
                            const char **repos_relpath,
                            svn_wc__db_wcroot_t *wcroot,
                            const char *local_relpath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int op_depth;

  /* Prefer the current node's repository information.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  op_depth = svn_sqlite__column_int(stmt, 0);

  if (op_depth > 0)
    {
      svn_wc__db_status_t presence = svn_sqlite__column_token(stmt, 3,
                                                              presence_map);

      if (presence == svn_wc__db_status_base_deleted)
        {
          SVN_ERR(svn_sqlite__step_row(stmt)); /* There must be a row */
          op_depth = svn_sqlite__column_int(stmt, 0);
        }
      else
        {
          const char *parent_repos_relpath;
          const char *parent_relpath;
          const char *name;

          SVN_ERR(svn_sqlite__reset(stmt));

          /* The repository relative path of an add/copy is based on its
             ancestor, not on the shadowed base layer.

             As this function is only used from the commit processing we know
             the parent directory has only a BASE row, so we can just obtain
             the information directly by recursing (once!)  */

          svn_relpath_split(&parent_relpath, &name, local_relpath,
                            scratch_pool);

          SVN_ERR(determine_commit_repos_info(repos_id, &parent_repos_relpath,
                                              wcroot, parent_relpath,
                                              scratch_pool, scratch_pool));

          *repos_relpath = svn_relpath_join(parent_repos_relpath, name,
                                            result_pool);
          return SVN_NO_ERROR;
        }
    }


  SVN_ERR_ASSERT(op_depth == 0); /* And that row must be BASE */

  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 1));
  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 2));

  *repos_id = svn_sqlite__column_int64(stmt, 1);
  *repos_relpath = svn_sqlite__column_text(stmt, 2, result_pool);

  return svn_error_trace(svn_sqlite__reset(stmt));
}

static svn_error_t *
moved_descendant_collect(apr_hash_t **map,
                        svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        int op_depth,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *map = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_DESCENDANTS_SRC));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                                         local_relpath,
                                         op_depth));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (! have_row)
    return svn_error_trace(svn_sqlite__reset(stmt));

  /* Find all moved descendants. Key them on target, because that is
     always unique */
  while (have_row)
    {
      const char *src_relpath = svn_sqlite__column_text(stmt, 1, result_pool);
      const char *to_relpath = svn_sqlite__column_text(stmt, 4, result_pool);

      if (!*map)
        *map = apr_hash_make(result_pool);

      svn_hash_sets(*map, to_relpath, src_relpath);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_global_commit()

   Makes local_relpath and all its descendants at the same op-depth represent
   the copy origin repos_id:repos_relpath@revision.

   This code is only valid to fix-up a move from an old location, to a new
   location during a commit.

   Assumptions:
     * local_relpath is not the working copy root (can't be moved)
     * repos_relpath is not the repository root (can't be moved)
 */
static svn_error_t *
moved_descendant_commit(svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        apr_int64_t repos_id,
                        const char *repos_relpath,
                        svn_revnum_t revision,
                        apr_hash_t *children,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  svn_sqlite__stmt_t *stmt;
  apr_hash_index_t *hi;

  SVN_ERR_ASSERT(*local_relpath != '\0'
                 && *repos_relpath != '\0');

  if (!children)
    return SVN_NO_ERROR;

  /* Then update them */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_COMMIT_UPDATE_ORIGIN));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, children); hi; hi = apr_hash_next(hi))
    {
      const char *src_relpath = apr_hash_this_val(hi);
      const char *to_relpath = apr_hash_this_key(hi);
      const char *new_repos_relpath;
      int to_op_depth = relpath_depth(to_relpath);
      int affected;
      apr_hash_t *map;

      svn_pool_clear(iterpool);

      SVN_ERR_ASSERT(to_op_depth > 0);

      new_repos_relpath = svn_relpath_join(
                            repos_relpath,
                            svn_relpath_skip_ancestor(local_relpath,
                                                      src_relpath),
                            iterpool);

      SVN_ERR(svn_sqlite__bindf(stmt, "isdisr", wcroot->wc_id,
                                                to_relpath,
                                                to_op_depth,
                                                repos_id,
                                                new_repos_relpath,
                                                revision));
      SVN_ERR(svn_sqlite__update(&affected, stmt));

#ifdef SVN_DEBUG
      /* Enable in release code?
         Broken moves are not fatal yet, but this assertion would break
         committing them */
      SVN_ERR_ASSERT(affected >= 1); /* If this fails there is no move dest */
#endif

      SVN_ERR(moved_descendant_collect(&map, wcroot, to_relpath, to_op_depth,
                                       iterpool, iterpool));
      SVN_ERR(moved_descendant_commit(wcroot, to_relpath,
                                      repos_id, new_repos_relpath, revision,
                                      map, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_global_commit()

   Moves all nodes below LOCAL_RELPATH from op-depth OP_DEPTH to op-depth 0
   (BASE), setting their presence to 'not-present' if their presence wasn't
   'normal'.

   Makes all nodes below LOCAL_RELPATH represent the descendants of repository
   location repos_id:repos_relpath@revision.

   Assumptions:
     * local_relpath is not the working copy root (can't be replaced)
     * repos_relpath is not the repository root (can't be replaced)
   */
static svn_error_t *
descendant_commit(svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  int op_depth,
                  apr_int64_t repos_id,
                  const char *repos_relpath,
                  svn_revnum_t revision,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(*local_relpath != '\0'
                 && *repos_relpath != '\0');

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_COMMIT_DESCENDANTS_TO_BASE));

  SVN_ERR(svn_sqlite__bindf(stmt, "isdisr", wcroot->wc_id,
                                            local_relpath,
                                            op_depth,
                                            repos_id,
                                            repos_relpath,
                                            revision));

  SVN_ERR(svn_sqlite__update(NULL, stmt));

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_global_commit().
 */
static svn_error_t *
commit_node(svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath,
            svn_revnum_t new_revision,
            svn_revnum_t changed_rev,
            apr_time_t changed_date,
            const char *changed_author,
            const svn_checksum_t *new_checksum,
            apr_hash_t *new_dav_cache,
            svn_boolean_t keep_changelist,
            svn_boolean_t no_unlock,
            const svn_skel_t *work_items,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt_info;
  svn_sqlite__stmt_t *stmt_act;
  svn_boolean_t have_act;
  svn_string_t prop_blob = { 0 };
  svn_string_t inherited_prop_blob = { 0 };
  const char *changelist = NULL;
  const char *parent_relpath;
  svn_wc__db_status_t new_presence;
  svn_node_kind_t new_kind;
  const char *new_depth_str = NULL;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t repos_id;
  const char *repos_relpath;
  int op_depth;
  svn_wc__db_status_t old_presence;
  svn_boolean_t moved_here;

    /* If we are adding a file or directory, then we need to get
     repository information from the parent node since "this node" does
     not have a BASE).

     For existing nodes, we should retain the (potentially-switched)
     repository information.  */
  SVN_ERR(determine_commit_repos_info(&repos_id, &repos_relpath,
                                      wcroot, local_relpath,
                                      scratch_pool, scratch_pool));

  /* ### is it better to select only the data needed?  */
  SVN_ERR(svn_sqlite__get_statement(&stmt_info, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt_info, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_row(stmt_info));

  SVN_ERR(svn_sqlite__get_statement(&stmt_act, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_act, "is",
                            wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_act, stmt_act));

  /* There should be something to commit!  */

  op_depth = svn_sqlite__column_int(stmt_info, 0);

  /* Figure out the new node's kind. It will be whatever is in WORKING_NODE,
     or there will be a BASE_NODE that has it.  */
  old_presence = svn_sqlite__column_token(stmt_info, 3, presence_map);
  new_kind = svn_sqlite__column_token(stmt_info, 4, kind_map);

  /* What will the new depth be?  */
  if (new_kind == svn_node_dir)
    new_depth_str = svn_sqlite__column_text(stmt_info, 11, scratch_pool);

  /* Check that the repository information is not being changed.  */
  if (op_depth == 0)
    {
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt_info, 1));
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt_info, 2));

      /* A commit cannot change these values.  */
      SVN_ERR_ASSERT(repos_id == svn_sqlite__column_int64(stmt_info, 1));
      SVN_ERR_ASSERT(strcmp(repos_relpath,
                            svn_sqlite__column_text(stmt_info, 2, NULL)) == 0);
    }

  if (old_presence != svn_wc__db_status_base_deleted)
    {
      /* Find the appropriate new properties -- ACTUAL overrides any properties
         in WORKING that arrived as part of a copy/move.

         Note: we'll keep them as a big blob of data, rather than
         deserialize/serialize them.  */
      if (have_act)
        prop_blob.data = svn_sqlite__column_blob(stmt_act, 1, &prop_blob.len,
                                                 scratch_pool);
      if (prop_blob.data == NULL)
        prop_blob.data = svn_sqlite__column_blob(stmt_info, 14, &prop_blob.len,
                                                 scratch_pool);

      inherited_prop_blob.data = svn_sqlite__column_blob(
                                            stmt_info, 16,
                                            &inherited_prop_blob.len,
                                            scratch_pool);

      if (keep_changelist && have_act)
        changelist = svn_sqlite__column_text(stmt_act, 0, scratch_pool);

      moved_here = svn_sqlite__column_int(stmt_info, 15);
    }
  else
    {
      moved_here = FALSE;
      changelist = NULL;
    }

  /* ### other stuff?  */

  SVN_ERR(svn_sqlite__reset(stmt_info));
  SVN_ERR(svn_sqlite__reset(stmt_act));

  if (op_depth > 0)
    {
      int affected_rows;

      SVN_ERR_ASSERT(op_depth == relpath_depth(local_relpath));

      /* First clear the moves that we are going to delete in a bit */
      {
        apr_hash_t *old_moves;
        apr_hash_index_t *hi;
        SVN_ERR(moved_descendant_collect(&old_moves, wcroot, local_relpath, 0,
                                         scratch_pool, scratch_pool));

        if (old_moves)
          for (hi = apr_hash_first(scratch_pool, old_moves);
                hi; hi = apr_hash_next(hi))
            {
              SVN_ERR(clear_moved_here(wcroot, apr_hash_this_key(hi),
                                        scratch_pool));
            }
      }

      /* This removes all layers of this node and at the same time determines
         if we need to remove shadowed layers below our descendants. */

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_NODE_ALL_LAYERS));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

      if (affected_rows > 1)
        {
          /* We commit a shadowing operation

           1) Remove all shadowed nodes
           2) And remove all nodes that have a base-deleted as lowest layer,
              because 1) removed that layer */

          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_DELETE_SHADOWED_RECURSIVE));

          SVN_ERR(svn_sqlite__bindf(stmt,
                                    "isd",
                                    wcroot->wc_id,
                                    local_relpath,
                                    op_depth));

          SVN_ERR(svn_sqlite__step_done(stmt));
        }

      /* Note that while these two calls look so similar that they might
         be integrated, they really affect a different op-depth and
         completely different nodes (via a different recursion pattern). */

      if (old_presence != svn_wc__db_status_base_deleted)
        {
          /* Collapse descendants of the current op_depth to layer 0,
             this includes moved-from/to clearing */
          SVN_ERR(descendant_commit(wcroot, local_relpath, op_depth,
                                    repos_id, repos_relpath, new_revision,
                                    scratch_pool));
        }

      if (old_presence != svn_wc__db_status_base_deleted)
        {
          apr_hash_t *moves = NULL;

          SVN_ERR(moved_descendant_collect(&moves, wcroot, local_relpath, 0,
                                           scratch_pool, scratch_pool));

          /* And make the recorded local moves represent moves of the node we
             just committed. */
          SVN_ERR(moved_descendant_commit(wcroot, local_relpath,
                                      repos_id, repos_relpath, new_revision,
                                      moves, scratch_pool));
        }

      if (moved_here)
        {
          /* This node is no longer modified, so no node was moved here */
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_CLEAR_MOVED_TO_FROM_DEST));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                                local_relpath));

          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }
  /* Update or add the BASE_NODE row with all the new information.  */

  if (*local_relpath == '\0')
    parent_relpath = NULL;
  else
    parent_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

  /* Preserve any incomplete status */
  if (old_presence != svn_wc__db_status_base_deleted)
    {
      new_presence = (old_presence == svn_wc__db_status_incomplete
                      ? svn_wc__db_status_incomplete
                      : svn_wc__db_status_normal);

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_APPLY_CHANGES_TO_BASE_NODE));
      /* symlink_target not yet used */
      SVN_ERR(svn_sqlite__bindf(stmt, "issisrtstrisnbn",
                                wcroot->wc_id, local_relpath,
                                parent_relpath,
                                repos_id,
                                repos_relpath,
                                new_revision,
                                presence_map, new_presence,
                                new_depth_str,
                                kind_map, new_kind,
                                changed_rev,
                                changed_date,
                                changed_author,
                                prop_blob.data, prop_blob.len));

      SVN_ERR(svn_sqlite__bind_checksum(stmt, 13, new_checksum,
                                        scratch_pool));
      SVN_ERR(svn_sqlite__bind_properties(stmt, 15, new_dav_cache,
                                          scratch_pool));
      if (inherited_prop_blob.data != NULL)
        {
          SVN_ERR(svn_sqlite__bind_blob(stmt, 17, inherited_prop_blob.data,
                                        inherited_prop_blob.len));
        }

      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      struct insert_base_baton_t ibb;
      blank_ibb(&ibb);

      ibb.repos_id = repos_id;
      ibb.status = svn_wc__db_status_not_present;
      ibb.kind = new_kind;
      ibb.repos_relpath = repos_relpath;
      ibb.revision = new_revision;

      SVN_ERR(insert_base_node(&ibb, wcroot, local_relpath, scratch_pool));

      keep_changelist = FALSE; /* Nothing there */
    }

  if (have_act)
    {
      if (keep_changelist && changelist != NULL)
        {
          /* The user told us to keep the changelist. Replace the row in
             ACTUAL_NODE with the basic keys and the changelist.  */
          SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb,
                    STMT_RESET_ACTUAL_WITH_CHANGELIST));
          SVN_ERR(svn_sqlite__bindf(stmt, "isss",
                                    wcroot->wc_id, local_relpath,
                                    svn_relpath_dirname(local_relpath,
                                                        scratch_pool),
                                    changelist));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      else
        {
          /* Toss the ACTUAL_NODE row.  */
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                            STMT_DELETE_ACTUAL_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  if (!no_unlock)
    {
      svn_sqlite__stmt_t *lock_stmt;
      svn_boolean_t op_root = (op_depth > 0
                               && (relpath_depth(local_relpath) == op_depth));

      /* If we are committing an add of a delete, we can assume we own
         all locks at or below REPOS_RELPATH (or the server would have
         denied the commit). As we must have passed these to the server
         we can now safely remove them.
       */
      SVN_ERR(svn_sqlite__get_statement(&lock_stmt, wcroot->sdb,
                                        op_root
                                          ? STMT_DELETE_LOCK_RECURSIVELY
                                          : STMT_DELETE_LOCK));
      SVN_ERR(svn_sqlite__bindf(lock_stmt, "is", repos_id, repos_relpath));
      SVN_ERR(svn_sqlite__step_done(lock_stmt));
    }

  /* Install any work items into the queue, as part of this transaction.  */
  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_commit(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_revnum_t new_revision,
                         svn_revnum_t changed_revision,
                         apr_time_t changed_date,
                         const char *changed_author,
                         const svn_checksum_t *new_checksum,
                         apr_hash_t *new_dav_cache,
                         svn_boolean_t keep_changelist,
                         svn_boolean_t no_unlock,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    commit_node(wcroot, local_relpath,
                new_revision, changed_revision, changed_date, changed_author,
                new_checksum, new_dav_cache, keep_changelist,
                no_unlock, work_items, scratch_pool),
    wcroot);

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_update(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_node_kind_t new_kind,
                         const char *new_repos_relpath,
                         svn_revnum_t new_revision,
                         const apr_hash_t *new_props,
                         svn_revnum_t new_changed_rev,
                         apr_time_t new_changed_date,
                         const char *new_changed_author,
                         const apr_array_header_t *new_children,
                         const svn_checksum_t *new_checksum,
                         const char *new_target,
                         const apr_hash_t *new_dav_cache,
                         const svn_skel_t *conflict,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();

#if 0
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  /* ### allow NULL for NEW_REPOS_RELPATH to indicate "no change"?  */
  SVN_ERR_ASSERT(svn_relpath_is_canonical(new_repos_relpath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_changed_rev));
  SVN_ERR_ASSERT((new_children != NULL
                  && new_checksum == NULL
                  && new_target == NULL)
                 || (new_children == NULL
                     && new_checksum != NULL
                     && new_target == NULL)
                 || (new_children == NULL
                     && new_checksum == NULL
                     && new_target != NULL));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    update_node(wcroot, local_relpath,
                new_repos_relpath, new_revision, new_props,
                new_changed_rev, new_changed_date, new_changed_author,
                new_children, new_checksum, new_target,
                conflict, work_items, scratch_pool),
    wcroot);

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(wcroot, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
#endif
}

/* Sets a base nodes revision, repository relative path, and/or inherited
   propertis. If LOCAL_ABSPATH's rev (REV) is valid, set its revision.  If
   SET_REPOS_RELPATH is TRUE set its repository relative path to REPOS_RELPATH
   (and make sure its REPOS_ID is still valid).  If IPROPS is not NULL set its
   inherited properties to IPROPS, if IPROPS is NULL then clear any the iprops
   cache for the base node.
 */
static svn_error_t *
db_op_set_rev_repos_relpath_iprops(svn_wc__db_wcroot_t *wcroot,
                                   const char *local_relpath,
                                   apr_array_header_t *iprops,
                                   svn_revnum_t rev,
                                   svn_boolean_t set_repos_relpath,
                                   const char *repos_relpath,
                                   apr_int64_t repos_id,
                                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(flush_entries(wcroot,
                        svn_dirent_join(wcroot->abspath, local_relpath,
                                        scratch_pool),
                        svn_depth_empty, scratch_pool));


  if (SVN_IS_VALID_REVNUM(rev))
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_BASE_REVISION));

      SVN_ERR(svn_sqlite__bindf(stmt, "isr", wcroot->wc_id, local_relpath,
                                rev));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (set_repos_relpath)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_BASE_REPOS));

      SVN_ERR(svn_sqlite__bindf(stmt, "isis", wcroot->wc_id, local_relpath,
                                repos_id, repos_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Set or clear iprops. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_IPROP));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__bind_iprops(stmt, 3, iprops, scratch_pool));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* The main body of bump_revisions_post_update().
 *
 * Tweak the information for LOCAL_RELPATH in WCROOT.  If NEW_REPOS_RELPATH is
 * non-NULL update the entry to the new url specified by NEW_REPOS_RELPATH,
 * NEW_REPOS_ID.  If NEW_REV is valid, make this the node's working revision.
 *
 * NODE_STATUS, NODE_KIND, NODE_REVISION and NODE_REPOS_RELPATH represent the
 * values as stored currently in WCROOT for LOCAL_RELPATH.
 *
 * If WCROOT_IPROPS is not NULL it is a hash mapping const char * absolute
 * working copy paths to depth-first ordered arrays of
 * svn_prop_inherited_item_t * structures.  If the absolute path equivalent
 * of LOCAL_RELPATH exists in WCROOT_IPROPS, then set the hashed value as the
 * node's inherited properties.
 *
 * Unless S_ROOT is TRUE the tweaks might cause the node for LOCAL_ABSPATH to
 * be removed from the WC; if IS_ROOT is TRUE this will not happen.
 */
static svn_error_t *
bump_node_revision(svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   svn_wc__db_status_t node_status,
                   svn_node_kind_t node_kind,
                   svn_revnum_t node_revision,
                   const char *node_repos_relpath,
                   apr_int64_t new_repos_id,
                   const char *new_repos_relpath,
                   svn_revnum_t new_rev,
                   svn_depth_t depth,
                   apr_hash_t *exclude_relpaths,
                   apr_hash_t *wcroot_iprops,
                   svn_boolean_t is_root,
                   svn_boolean_t skip_when_dir,
                   svn_wc__db_t *db,
                   apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_hash_t *children;
  apr_hash_index_t *hi;
  svn_boolean_t set_repos_relpath = FALSE;
  svn_depth_t depth_below_here = depth;
  apr_array_header_t *iprops = NULL;

  if (new_repos_relpath != NULL
      && strcmp(node_repos_relpath, new_repos_relpath))
    set_repos_relpath = TRUE;

  if (wcroot_iprops)
    iprops = svn_hash_gets(wcroot_iprops,
                           svn_dirent_join(wcroot->abspath, local_relpath,
                                           scratch_pool));

  if (iprops
      || set_repos_relpath
      || (SVN_IS_VALID_REVNUM(new_rev) && new_rev != node_revision))
    {
      SVN_ERR(db_op_set_rev_repos_relpath_iprops(wcroot, local_relpath,
                                                 iprops, new_rev,
                                                 set_repos_relpath,
                                                 new_repos_relpath,
                                                 new_repos_id,
                                                 scratch_pool));
    }

  /* Early out */
  if (depth <= svn_depth_empty
      || node_kind != svn_node_dir
      || node_status == svn_wc__db_status_server_excluded
      || node_status == svn_wc__db_status_excluded
      || node_status == svn_wc__db_status_not_present)
    return SVN_NO_ERROR;

  /* And now recurse over the children */

  depth_below_here = depth;

  if (depth == svn_depth_immediates || depth == svn_depth_files)
    depth_below_here = svn_depth_empty;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(base_get_children_info(&children, wcroot, local_relpath, 0,
                                 scratch_pool, iterpool));
  for (hi = apr_hash_first(scratch_pool, children); hi; hi = apr_hash_next(hi))
    {
      const char *child_basename = apr_hash_this_key(hi);
      const struct svn_wc__db_base_info_t *child_info;
      const char *child_local_relpath;
      const char *child_repos_relpath = NULL;

      svn_pool_clear(iterpool);

      child_info = apr_hash_this_val(hi);

      if (child_info->update_root && child_info->kind == svn_node_file)
        continue; /* Skip file externals */

      if (depth < svn_depth_immediates && child_info->kind == svn_node_dir)
          continue; /* Skip directories */

      child_local_relpath = svn_relpath_join(local_relpath, child_basename,
                                             iterpool);

      /* Don't touch nodes that can't be touched via the exclude list */
      if (svn_hash_gets(exclude_relpaths, child_local_relpath))
          continue;

      /* If the node is still marked 'not-present', then the server did not
          re-add it.  So it's really gone in this revision, thus we remove the
          node.

          If the node is still marked 'server-excluded' and yet is not the same
          revision as new_rev, then the server did not re-add it, nor
          re-server-exclude it, so we can remove the node. */
      if (child_info->status == svn_wc__db_status_not_present
          || (child_info->status == svn_wc__db_status_server_excluded &&
              child_info->revnum != new_rev))
        {
          svn_sqlite__stmt_t *stmt;
          SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_BASE_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, child_local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
          continue;
        }

      /* Derive the new URL for the current (child) entry */
      if (new_repos_relpath)
        child_repos_relpath = svn_relpath_join(new_repos_relpath,
                                               child_basename, iterpool);

      SVN_ERR(bump_node_revision(wcroot, child_local_relpath,
                                 child_info->status,
                                 child_info->kind,
                                 child_info->revnum,
                                 child_info->repos_relpath,
                                 new_repos_id,
                                 child_repos_relpath, new_rev,
                                 depth_below_here,
                                 exclude_relpaths, wcroot_iprops,
                                 FALSE /* is_root */,
                                 (depth < svn_depth_immediates), db,
                                 iterpool));
    }

  /* Cleanup */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_op_bump_revisions_post_update().
 */
static svn_error_t *
bump_revisions_post_update(svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           svn_wc__db_t *db,
                           svn_depth_t depth,
                           const char *new_repos_relpath,
                           const char *new_repos_root_url,
                           const char *new_repos_uuid,
                           svn_revnum_t new_revision,
                           apr_hash_t *exclude_relpaths,
                           apr_hash_t *wcroot_iprops,
                           svn_boolean_t empty_update,
                           svn_wc_notify_func2_t notify_func,
                           void *notify_baton,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_error_t *err;
  apr_int64_t new_repos_id = INVALID_REPOS_ID;
  svn_revnum_t revision;
  const char *repos_relpath;

  err = svn_wc__db_base_get_info_internal(&status, &kind, &revision,
                                          &repos_relpath, NULL,
                                          NULL, NULL, NULL, NULL, NULL, NULL,
                                          NULL, NULL, NULL, NULL,
                                          wcroot, local_relpath,
                                          scratch_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  switch (status)
    {
      case svn_wc__db_status_excluded:
      case svn_wc__db_status_server_excluded:
      case svn_wc__db_status_not_present:
        return SVN_NO_ERROR;

      /* Explicitly ignore other statii */
      default:
        break;
    }

  if (new_repos_root_url != NULL)
    SVN_ERR(create_repos_id(&new_repos_id, new_repos_root_url,
                            new_repos_uuid,
                            wcroot->sdb, scratch_pool));

  SVN_ERR(bump_node_revision(wcroot, local_relpath,
                             status, kind,  revision, repos_relpath,
                             new_repos_id,
                             new_repos_relpath, new_revision,
                             depth, exclude_relpaths,
                             wcroot_iprops,
                             TRUE /* is_root */, FALSE, db,
                             scratch_pool));

  /* ### TODO: Use empty_update flag for change knowledge */
  SVN_ERR(svn_wc__db_bump_moved_away(wcroot, local_relpath, depth, db,
                                     scratch_pool));

  SVN_ERR(svn_wc__db_update_move_list_notify(wcroot, SVN_INVALID_REVNUM,
                                             SVN_INVALID_REVNUM, notify_func,
                                             notify_baton, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_bump_revisions_post_update(svn_wc__db_t *db,
                                         const char *local_abspath,
                                         svn_depth_t depth,
                                         const char *new_repos_relpath,
                                         const char *new_repos_root_url,
                                         const char *new_repos_uuid,
                                         svn_revnum_t new_revision,
                                         apr_hash_t *exclude_relpaths,
                                         apr_hash_t *wcroot_iprops,
                                         svn_boolean_t empty_update,
                                         svn_wc_notify_func2_t notify_func,
                                         void *notify_baton,
                                         apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  if (svn_hash_gets(exclude_relpaths, local_relpath))
    return SVN_NO_ERROR;

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  SVN_WC__DB_WITH_TXN(
    bump_revisions_post_update(wcroot, local_relpath, db,
                               depth, new_repos_relpath, new_repos_root_url,
                               new_repos_uuid, new_revision,
                               exclude_relpaths, wcroot_iprops, empty_update,
                               notify_func, notify_baton, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_lock_add().
 */
static svn_error_t *
lock_add_txn(svn_wc__db_wcroot_t *wcroot,
             const char *local_relpath,
             const svn_wc__db_lock_t *lock,
             apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  const char *repos_relpath;
  apr_int64_t repos_id;

  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                            &repos_relpath, &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                            repos_id, repos_relpath, lock->token));

  if (lock->owner != NULL)
    SVN_ERR(svn_sqlite__bind_text(stmt, 4, lock->owner));

  if (lock->comment != NULL)
    SVN_ERR(svn_sqlite__bind_text(stmt, 5, lock->comment));

  if (lock->date != 0)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 6, lock->date));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_add(svn_wc__db_t *db,
                    const char *local_abspath,
                    const svn_wc__db_lock_t *lock,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(lock != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    lock_add_txn(wcroot, local_relpath, lock, scratch_pool),
    wcroot);

  /* There may be some entries, and the lock info is now out of date.  */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_lock_remove().
 */
static svn_error_t *
lock_remove_txn(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_skel_t *work_items,
                apr_pool_t *scratch_pool)
{
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                            &repos_relpath, &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", repos_id, repos_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    lock_remove_txn(wcroot, local_relpath, work_items, scratch_pool),
    wcroot);

  /* There may be some entries, and the lock info is now out of date.  */
  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}

/* A helper for scan_addition().
 * Compute moved-from information for the node at LOCAL_RELPATH which
 * has been determined as having been moved-here.
 * If MOVED_FROM_RELPATH is not NULL, set *MOVED_FROM_RELPATH to the
 * path of the move-source node in *MOVED_FROM_RELPATH.
 * If DELETE_OP_ROOT_RELPATH is not NULL, set *DELETE_OP_ROOT_RELPATH
 * to the path of the op-root of the delete-half of the move.
 * If moved-from information cannot be derived, set both *MOVED_FROM_RELPATH
 * and *DELETE_OP_ROOT_RELPATH to NULL, and return a "copied" status.
 * COPY_OPT_ROOT_RELPATH is the relpath of the op-root of the copied-half
 * of the move. */
static svn_error_t *
get_moved_from_info(const char **moved_from_relpath,
                    const char **moved_from_op_root_relpath,
                    const char *moved_to_op_root_relpath,
                    int *op_depth,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* Run a query to get the moved-from path from the DB. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_FROM_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id, moved_to_op_root_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      /* The move was only recorded at the copy-half, possibly because
       * the move operation was interrupted mid-way between the copy
       * and the delete. Treat this node as a normal copy. */
      if (moved_from_relpath)
        *moved_from_relpath = NULL;
      if (moved_from_op_root_relpath)
        *moved_from_op_root_relpath = NULL;

      SVN_ERR(svn_sqlite__reset(stmt));
      return SVN_NO_ERROR;
    }

  if (op_depth)
    *op_depth = svn_sqlite__column_int(stmt, 1);

  if (moved_from_relpath || moved_from_op_root_relpath)
    {
      const char *db_delete_op_root_relpath;

      /* The moved-from path from the DB is the relpath of
       * the op_root of the delete-half of the move. */
      db_delete_op_root_relpath = svn_sqlite__column_text(stmt, 0,
                                                          result_pool);
      if (moved_from_op_root_relpath)
        *moved_from_op_root_relpath = db_delete_op_root_relpath;

      if (moved_from_relpath)
        {
          if (strcmp(moved_to_op_root_relpath, local_relpath) == 0)
            {
              /* LOCAL_RELPATH is the op_root of the copied-half of the
               * move, so the correct MOVED_FROM_ABSPATH is the op-root
               * of the delete-half. */
              *moved_from_relpath = db_delete_op_root_relpath;
            }
          else
            {
              const char *child_relpath;

              /* LOCAL_RELPATH is a child that was copied along with the
               * op_root of the copied-half of the move. Construct the
               * corresponding path beneath the op_root of the delete-half. */

              /* Grab the child path relative to the op_root of the move
               * destination. */
              child_relpath = svn_relpath_skip_ancestor(
                                moved_to_op_root_relpath, local_relpath);

              SVN_ERR_ASSERT(child_relpath && strlen(child_relpath) > 0);

              /* This join is valid because LOCAL_RELPATH has not been moved
               * within the copied-half of the move yet -- else, it would
               * be its own op_root. */
              *moved_from_relpath = svn_relpath_join(db_delete_op_root_relpath,
                                                     child_relpath,
                                                     result_pool);
            }
        }
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Like svn_wc__db_scan_addition(), but with WCROOT+LOCAL_RELPATH instead of
   DB+LOCAL_ABSPATH.

   The output value of *ORIGINAL_REPOS_ID will be INVALID_REPOS_ID if there
   is no 'copy-from' repository.  */
static svn_error_t *
scan_addition(svn_wc__db_status_t *status,
              const char **op_root_relpath_p,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              const char **original_repos_relpath,
              apr_int64_t *original_repos_id,
              svn_revnum_t *original_revision,
              const char **moved_from_relpath,
              const char **moved_from_op_root_relpath,
              int *moved_from_op_depth,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *op_root_relpath;
  const char *build_relpath = "";

  /* Initialize most of the OUT parameters. Generally, we'll only be filling
     in a subset of these, so it is easier to init all up front. Note that
     the STATUS parameter will be initialized once we read the status of
     the specified node.  */
  if (op_root_relpath_p)
    *op_root_relpath_p = NULL;
  if (original_repos_relpath)
    *original_repos_relpath = NULL;
  if (original_repos_id)
    *original_repos_id = INVALID_REPOS_ID;
  if (original_revision)
    *original_revision = SVN_INVALID_REVNUM;
  if (moved_from_relpath)
    *moved_from_relpath = NULL;
  if (moved_from_op_root_relpath)
    *moved_from_op_root_relpath = NULL;
  if (moved_from_op_depth)
    *moved_from_op_depth = 0;

  {
    svn_sqlite__stmt_t *stmt;
    svn_boolean_t have_row;
    svn_wc__db_status_t presence;
    int op_depth;
    const char *repos_prefix_path;

    /* ### is it faster to fetch fewer columns? */
    SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                      STMT_SELECT_WORKING_NODE));
    SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));

    if (!have_row)
      {
        /* Reset statement before returning */
        SVN_ERR(svn_sqlite__reset(stmt));

        /* ### maybe we should return a usage error instead?  */
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));
      }

    presence = svn_sqlite__column_token(stmt, 1, presence_map);

    /* The starting node should exist normally.  */
    op_depth = svn_sqlite__column_int(stmt, 0);
    if (op_depth == 0 || (presence != svn_wc__db_status_normal
                          && presence != svn_wc__db_status_incomplete))
      /* reset the statement as part of the error generation process */
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                               svn_sqlite__reset(stmt),
                               _("Expected node '%s' to be added."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));

    if (original_revision)
      *original_revision = svn_sqlite__column_revnum(stmt, 12);

    /* Provide the default status; we'll override as appropriate. */
    if (status)
      {
        if (presence == svn_wc__db_status_normal)
          *status = svn_wc__db_status_added;
        else
          *status = svn_wc__db_status_incomplete;
      }


    /* Calculate the op root local path components */
    op_root_relpath = svn_relpath_prefix(local_relpath, op_depth,
                                         scratch_pool);
    repos_prefix_path = svn_relpath_skip_ancestor(op_root_relpath,
                                                  local_relpath);

    if (op_root_relpath_p)
      *op_root_relpath_p = apr_pstrdup(result_pool, op_root_relpath);

    /* ### This if-statement is quite redundant.
     * ### We're checking all these values again within the body anyway.
     * ### The body should be broken up appropriately and move into the
     * ### outer scope. */
    if (original_repos_relpath
        || original_repos_id
        || (original_revision
                && *original_revision == SVN_INVALID_REVNUM)
        || status
        || moved_from_relpath || moved_from_op_root_relpath)
      {
        if (local_relpath != op_root_relpath)
          /* requery to get the add/copy root */
          {
            SVN_ERR(svn_sqlite__reset(stmt));

            SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                      wcroot->wc_id, op_root_relpath));
            SVN_ERR(svn_sqlite__step(&have_row, stmt));

            if (!have_row)
              {
                /* Reset statement before returning */
                SVN_ERR(svn_sqlite__reset(stmt));

                /* ### maybe we should return a usage error instead?  */
                return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                         _("The node '%s' was not found."),
                                         path_for_error_message(wcroot,
                                                                op_root_relpath,
                                                                scratch_pool));
              }

            if (original_revision
                    && *original_revision == SVN_INVALID_REVNUM)
              *original_revision = svn_sqlite__column_revnum(stmt, 12);
          }

        if (original_repos_relpath)
          *original_repos_relpath = svn_sqlite__column_text(stmt, 11,
                                                            result_pool);

        if (!svn_sqlite__column_is_null(stmt, 10)
            && (status
                || original_repos_id
                || moved_from_relpath || moved_from_op_root_relpath))
          /* If column 10 (original_repos_id) is NULL,
             this is a plain add, not a copy or a move */
          {
            svn_boolean_t moved_here;
            if (original_repos_id)
              *original_repos_id = svn_sqlite__column_int64(stmt, 10);

            moved_here = svn_sqlite__column_boolean(stmt, 13 /* moved_here */);
            if (status)
              *status = moved_here ? svn_wc__db_status_moved_here
                                   : svn_wc__db_status_copied;

            if (moved_here
                && (moved_from_relpath || moved_from_op_root_relpath))
              {
                svn_error_t *err;

                err = get_moved_from_info(moved_from_relpath,
                                          moved_from_op_root_relpath,
                                          op_root_relpath,
                                          moved_from_op_depth,
                                          wcroot, local_relpath,
                                          result_pool,
                                          scratch_pool);

                if (err)
                  return svn_error_compose_create(
                                err, svn_sqlite__reset(stmt));
              }
          }
      }


    /* ### This loop here is to skip up to the first node which is a BASE node,
       because base_get_info() doesn't accommodate the scenario that
       we're looking at here; we found the true op_root, which may be inside
       further changed trees. */
    if (repos_relpath || repos_id)
      {
        const char *base_relpath;

        while (TRUE)
          {
            const char *tmp;

            SVN_ERR(svn_sqlite__reset(stmt));

            /* Pointing at op_depth, look at the parent */
            repos_prefix_path =
                svn_relpath_join(svn_relpath_basename(op_root_relpath, NULL),
                                 repos_prefix_path,
                                 scratch_pool);
            op_root_relpath = svn_relpath_dirname(op_root_relpath, scratch_pool);


            SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, op_root_relpath));
            SVN_ERR(svn_sqlite__step(&have_row, stmt));

            if (! have_row)
              break;

            op_depth = svn_sqlite__column_int(stmt, 0);

            /* Skip to op_depth */
            tmp = op_root_relpath;

            op_root_relpath = svn_relpath_prefix(op_root_relpath, op_depth,
                                                 scratch_pool);
            repos_prefix_path = svn_relpath_join(
                                                 svn_relpath_skip_ancestor(op_root_relpath, tmp),
                                                 repos_prefix_path, scratch_pool);
          }

      SVN_ERR(svn_sqlite__reset(stmt));

      build_relpath = repos_prefix_path;

      /* If we're here, then we have an added/copied/moved (start) node, and
         CURRENT_ABSPATH now points to a BASE node. Figure out the repository
         information for the current node, and use that to compute the start
         node's repository information.  */
      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                                &base_relpath, repos_id,
                                                NULL, NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL,
                                                wcroot, op_root_relpath,
                                                scratch_pool, scratch_pool));

        if (repos_relpath)
          *repos_relpath = svn_relpath_join(base_relpath, build_relpath,
                                            result_pool);
      }
    else
      SVN_ERR(svn_sqlite__reset(stmt));
  }
  /* Postconditions */
#ifdef SVN_DEBUG
  if (status)
    {
      SVN_ERR_ASSERT(*status == svn_wc__db_status_added
                     || *status == svn_wc__db_status_copied
                     || *status == svn_wc__db_status_incomplete
                     || *status == svn_wc__db_status_moved_here);
      if (*status == svn_wc__db_status_added)
        {
          SVN_ERR_ASSERT(!original_repos_relpath
                         || *original_repos_relpath == NULL);
          SVN_ERR_ASSERT(!original_revision
                         || *original_revision == SVN_INVALID_REVNUM);
          SVN_ERR_ASSERT(!original_repos_id
                         || *original_repos_id == INVALID_REPOS_ID);
        }
      /* An upgrade with a missing directory can leave INCOMPLETE working
         op-roots. See upgrade_tests.py 29: upgrade with missing replaced dir
       */
      else if (*status != svn_wc__db_status_incomplete)
        {
          SVN_ERR_ASSERT(!original_repos_relpath
                         || *original_repos_relpath != NULL);
          SVN_ERR_ASSERT(!original_revision
                         || *original_revision != SVN_INVALID_REVNUM);
          SVN_ERR_ASSERT(!original_repos_id
                         || *original_repos_id != INVALID_REPOS_ID);
        }
    }
  SVN_ERR_ASSERT(!op_root_relpath_p || *op_root_relpath_p != NULL);
#endif

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_scan_addition_internal(
              svn_wc__db_status_t *status,
              const char **op_root_relpath_p,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              const char **original_repos_relpath,
              apr_int64_t *original_repos_id,
              svn_revnum_t *original_revision,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
      scan_addition(status, op_root_relpath_p, repos_relpath, repos_id,
                    original_repos_relpath, original_repos_id,
                    original_revision, NULL, NULL, NULL,
                    wcroot, local_relpath, result_pool, scratch_pool));
}

svn_error_t *
svn_wc__db_scan_addition(svn_wc__db_status_t *status,
                         const char **op_root_abspath,
                         const char **repos_relpath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         const char **original_repos_relpath,
                         const char **original_root_url,
                         const char **original_uuid,
                         svn_revnum_t *original_revision,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *op_root_relpath = NULL;
  apr_int64_t repos_id = INVALID_REPOS_ID;
  apr_int64_t original_repos_id = INVALID_REPOS_ID;
  apr_int64_t *repos_id_p
    = (repos_root_url || repos_uuid) ? &repos_id : NULL;
  apr_int64_t *original_repos_id_p
    = (original_root_url || original_uuid) ? &original_repos_id : NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN4(
          scan_addition(status,
                        op_root_abspath
                                ? &op_root_relpath
                                : NULL,
                        repos_relpath, repos_id_p,
                        original_repos_relpath, original_repos_id_p,
                        original_revision,
                        NULL, NULL, NULL,
                        wcroot, local_relpath, result_pool, scratch_pool),
          svn_wc__db_fetch_repos_info(repos_root_url, repos_uuid, wcroot,
                                      repos_id, result_pool),
          svn_wc__db_fetch_repos_info(original_root_url, original_uuid,
                                      wcroot, original_repos_id,
                                      result_pool),
          SVN_NO_ERROR,
          wcroot);

  if (op_root_abspath)
    *op_root_abspath = svn_dirent_join(wcroot->abspath, op_root_relpath,
                                       result_pool);
  /* REPOS_ID must be valid if requested; ORIGINAL_REPOS_ID need not be. */
  SVN_ERR_ASSERT(repos_id_p == NULL || repos_id != INVALID_REPOS_ID);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_scan_moved(const char **moved_from_abspath,
                      const char **op_root_abspath,
                      const char **op_root_moved_from_abspath,
                      const char **moved_from_delete_abspath,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc__db_status_t status;
  const char *op_root_relpath = NULL;
  const char *moved_from_relpath = NULL;
  const char *moved_from_op_root_relpath = NULL;
  int moved_from_op_depth = -1;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
          scan_addition(&status,
                        op_root_abspath
                                ? &op_root_relpath
                                : NULL,
                        NULL, NULL,
                        NULL, NULL, NULL,
                        moved_from_abspath
                            ? &moved_from_relpath
                            : NULL,
                        (op_root_moved_from_abspath
                         || moved_from_delete_abspath)
                            ? &moved_from_op_root_relpath
                            : NULL,
                        moved_from_delete_abspath
                            ? &moved_from_op_depth
                            : NULL,
                        wcroot, local_relpath, scratch_pool, scratch_pool),
          wcroot);

  if (status != svn_wc__db_status_moved_here || !moved_from_relpath)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Path '%s' was not moved here"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

  if (op_root_abspath)
    *op_root_abspath = svn_dirent_join(wcroot->abspath, op_root_relpath,
                                       result_pool);

  if (moved_from_abspath)
    *moved_from_abspath = svn_dirent_join(wcroot->abspath, moved_from_relpath,
                                          result_pool);

  if (op_root_moved_from_abspath)
    *op_root_moved_from_abspath = svn_dirent_join(wcroot->abspath,
                                                  moved_from_op_root_relpath,
                                                  result_pool);

  /* The deleted node is either where we moved from, or one of its ancestors */
  if (moved_from_delete_abspath)
    {
      const char *tmp = svn_relpath_prefix(moved_from_op_root_relpath,
                                           moved_from_op_depth, scratch_pool);

      *moved_from_delete_abspath = svn_dirent_join(wcroot->abspath, tmp,
                                                   scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* ### Recursive helper for svn_wc__db_follow_moved_to()
 */
static svn_error_t *
follow_moved_to(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                int op_depth,
                apr_array_header_t **moved_tos,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int shadowing_op_depth;
  const char *ancestor_relpath;
  const char *node_moved_to = NULL;
  int i;

  /* Obtain the depth of the node directly shadowing local_relpath
     as it exists at OP_DEPTH, and perhaps moved to info */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_OP_DEPTH_MOVED_TO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      shadowing_op_depth = svn_sqlite__column_int(stmt, 0);
      node_moved_to = svn_sqlite__column_text(stmt, 1, result_pool);

      if (node_moved_to)
        {
          struct svn_wc__db_moved_to_t *moved_to;

          moved_to = apr_palloc(result_pool, sizeof(*moved_to));
          moved_to->op_depth = shadowing_op_depth;
          moved_to->local_relpath = node_moved_to;
          APR_ARRAY_PUSH(*moved_tos, struct svn_wc__db_moved_to_t *) = moved_to;
        }
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      /* Node is not shadowed, so not moved */
      return SVN_NO_ERROR;
    }
  else if (node_moved_to)
    {
      /* Moved directly, so we have the final location */
      return SVN_NO_ERROR;
    }
  /* Need to handle being moved via an ancestor. */
  ancestor_relpath = local_relpath;
  for (i = relpath_depth(local_relpath); i > shadowing_op_depth; --i)
    {
      const char *ancestor_moved_to;

      ancestor_relpath = svn_relpath_dirname(ancestor_relpath, scratch_pool);

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_MOVED_TO));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, ancestor_relpath,
                                shadowing_op_depth));
      SVN_ERR(svn_sqlite__step_row(stmt));

      ancestor_moved_to = svn_sqlite__column_text(stmt, 0, scratch_pool);
      SVN_ERR(svn_sqlite__reset(stmt));
      if (ancestor_moved_to)
        {
          struct svn_wc__db_moved_to_t *moved_to;

          node_moved_to
              = svn_relpath_join(ancestor_moved_to,
                                 svn_relpath_skip_ancestor(ancestor_relpath,
                                                           local_relpath),
                                 result_pool);

          moved_to = apr_palloc(result_pool, sizeof(*moved_to));
          moved_to->op_depth = shadowing_op_depth;
          moved_to->local_relpath = node_moved_to;
          APR_ARRAY_PUSH(*moved_tos, struct svn_wc__db_moved_to_t *) = moved_to;

          SVN_ERR(follow_moved_to(wcroot, node_moved_to,
                                  relpath_depth(ancestor_moved_to),
                                  moved_tos, result_pool, scratch_pool));

          break;
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_follow_moved_to(apr_array_header_t **moved_tos,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *moved_tos = apr_array_make(result_pool, 0,
                              sizeof(struct svn_wc__db_moved_to_t *));

  /* ### Wrap in a transaction */
  SVN_WC__DB_WITH_TXN(follow_moved_to(wcroot, local_relpath, 0, moved_tos,
                                      result_pool, scratch_pool),
                      wcroot);

  /* ### Convert moved_to to abspath */

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_scan_moved_to_internal(const char **move_src_relpath,
                                  const char **move_dst_relpath,
                                  const char **delete_relpath,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *local_relpath,
                                  int op_depth,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int delete_op_depth;
  const char *relpath = local_relpath;
  const char *dst_relpath;

  SVN_ERR_ASSERT(local_relpath[0]); /* Not valid on the WC root */

  if (move_src_relpath)
    *move_src_relpath = NULL;
  if (move_dst_relpath)
    *move_dst_relpath = NULL;
  if (delete_relpath)
    *delete_relpath = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_OP_DEPTH_MOVED_TO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, relpath, op_depth));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                               svn_sqlite__reset(stmt),
                               _("Node '%s' is not shadowed"),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool));
    }

  delete_op_depth = svn_sqlite__column_int(stmt, 0);
  dst_relpath = svn_sqlite__column_text(stmt, 1, scratch_pool);

  SVN_ERR(svn_sqlite__reset(stmt));

  while (!dst_relpath && have_row)
    {
      relpath = svn_relpath_dirname(relpath, scratch_pool);

      if (relpath_depth(relpath) < delete_op_depth)
        break;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_DEPTH_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, relpath,
                                delete_op_depth));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (have_row)
        dst_relpath = svn_sqlite__column_text(stmt, 13, scratch_pool);

      SVN_ERR(svn_sqlite__reset(stmt));
    }

  if (dst_relpath)
    {
      if (move_src_relpath)
        *move_src_relpath = apr_pstrdup(result_pool, relpath);

      if (move_dst_relpath)
        *move_dst_relpath = apr_pstrdup(result_pool, dst_relpath);

      if (delete_relpath)
        *delete_relpath = svn_relpath_prefix(local_relpath, delete_op_depth,
                                             result_pool);
    }

  return SVN_NO_ERROR;
}

/* Public (within libsvn_wc) absolute path version of
   svn_wc__db_op_depth_moved_to with the op-depth hard-coded to
   BASE. */
svn_error_t *
svn_wc__db_base_moved_to(const char **move_dst_abspath,
                         const char **move_dst_op_root_abspath,
                         const char **move_src_root_abspath,
                         const char **delete_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *dst_root_relpath;
  const char *src_root_relpath, *delete_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));


  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(svn_wc__db_scan_moved_to_internal(&src_root_relpath,
                                                        &dst_root_relpath,
                                                        &delete_relpath,
                                                        wcroot, local_relpath,
                                                        0 /* BASE */,
                                                        scratch_pool,
                                                        scratch_pool),
                      wcroot);

  if (move_dst_abspath)
    *move_dst_abspath =
        dst_root_relpath
          ? svn_dirent_join(wcroot->abspath,
                            svn_dirent_join(
                                    dst_root_relpath,
                                    svn_relpath_skip_ancestor(src_root_relpath,
                                                              local_relpath),
                                    scratch_pool),
                            result_pool)
          : NULL;

  if (move_dst_op_root_abspath)
    *move_dst_op_root_abspath =
          dst_root_relpath
              ? svn_dirent_join(wcroot->abspath, dst_root_relpath, result_pool)
              : NULL;

  if (move_src_root_abspath)
    *move_src_root_abspath =
          src_root_relpath
              ? svn_dirent_join(wcroot->abspath, src_root_relpath, result_pool)
              : NULL;

  if (delete_abspath)
    *delete_abspath =
          delete_relpath
              ? svn_dirent_join(wcroot->abspath, delete_relpath, result_pool)
              : NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_upgrade_begin(svn_sqlite__db_t **sdb,
                         apr_int64_t *repos_id,
                         apr_int64_t *wc_id,
                         svn_wc__db_t *wc_db,
                         const char *dir_abspath,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;

  /* Upgrade is inherently exclusive so specify exclusive locking. */
  SVN_ERR(create_db(sdb, repos_id, wc_id, dir_abspath,
                    repos_root_url, repos_uuid,
                    SDB_FILE,
                    NULL, SVN_INVALID_REVNUM, svn_depth_unknown,
                    TRUE /* exclusive */,
                    0 /* timeout */,
                    wc_db->state_pool, scratch_pool));

  SVN_ERR(svn_wc__db_pdh_create_wcroot(&wcroot,
                                       apr_pstrdup(wc_db->state_pool,
                                                   dir_abspath),
                                       *sdb, *wc_id, FORMAT_FROM_SDB,
                                       FALSE /* auto-upgrade */,
                                       wc_db->state_pool, scratch_pool));

  /* The WCROOT is complete. Stash it into DB.  */
  svn_hash_sets(wc_db->dir_data, wcroot->abspath, wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_upgrade_insert_external(svn_wc__db_t *db,
                                   const char *local_abspath,
                                   svn_node_kind_t kind,
                                   const char *parent_abspath,
                                   const char *def_local_abspath,
                                   const char *repos_relpath,
                                   const char *repos_root_url,
                                   const char *repos_uuid,
                                   svn_revnum_t def_peg_revision,
                                   svn_revnum_t def_revision,
                                   apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *def_local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We know only of DEF_LOCAL_ABSPATH that it definitely belongs to "this"
   * WC, i.e. where the svn:externals prop is set. The external target path
   * itself may be "hidden behind" other working copies. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &def_local_relpath,
                                                db, def_local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);


  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", repos_root_url));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    repos_id = svn_sqlite__column_int64(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      /* Need to set up a new repository row. */
      SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                              wcroot->sdb, scratch_pool));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_INSERT_EXTERNAL));

  /* wc_id, local_relpath, parent_relpath, presence, kind, def_local_relpath,
   * repos_id, def_repos_relpath, def_operational_revision, def_revision */
  SVN_ERR(svn_sqlite__bindf(stmt, "issstsis",
                            wcroot->wc_id,
                            svn_dirent_skip_ancestor(wcroot->abspath,
                                                     local_abspath),
                            svn_dirent_skip_ancestor(wcroot->abspath,
                                                     parent_abspath),
                            "normal",
                            kind_map, kind,
                            def_local_relpath,
                            repos_id,
                            repos_relpath));

  if (SVN_IS_VALID_REVNUM(def_peg_revision))
    SVN_ERR(svn_sqlite__bind_revnum(stmt, 9, def_peg_revision));

  if (SVN_IS_VALID_REVNUM(def_revision))
    SVN_ERR(svn_sqlite__bind_revnum(stmt, 10, def_revision));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_wq_add_internal(svn_wc__db_wcroot_t *wcroot,
                           const svn_skel_t *work_item,
                           apr_pool_t *scratch_pool)
{
  /* Add the work item(s) to the WORK_QUEUE.  */
  return svn_error_trace(add_work_items(wcroot->sdb, work_item,
                                        scratch_pool));
}

svn_error_t *
svn_wc__db_wq_add(svn_wc__db_t *db,
                  const char *wri_abspath,
                  const svn_skel_t *work_item,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  /* Quick exit, if there are no work items to queue up.  */
  if (work_item == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* Add the work item(s) to the WORK_QUEUE.  */
  return svn_error_trace(add_work_items(wcroot->sdb, work_item,
                                        scratch_pool));
}

/* The body of svn_wc__db_wq_fetch_next().
 */
static svn_error_t *
wq_fetch_next(apr_uint64_t *id,
              svn_skel_t **work_item,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_uint64_t completed_id,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  if (completed_id != 0)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_DELETE_WORK_ITEM));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 1, completed_id));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORK_ITEM));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      *id = 0;
      *work_item = NULL;
    }
  else
    {
      apr_size_t len;
      const void *val;

      *id = svn_sqlite__column_int64(stmt, 0);

      val = svn_sqlite__column_blob(stmt, 1, &len, result_pool);

      *work_item = svn_skel__parse(val, len, result_pool);
    }

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_wq_fetch_next(apr_uint64_t *id,
                         svn_skel_t **work_item,
                         svn_wc__db_t *db,
                         const char *wri_abspath,
                         apr_uint64_t completed_id,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(id != NULL);
  SVN_ERR_ASSERT(work_item != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    wq_fetch_next(id, work_item,
                  wcroot, local_relpath, completed_id,
                  result_pool, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

/* Records timestamp and date for one or more files in wcroot */
static svn_error_t *
wq_record(svn_wc__db_wcroot_t *wcroot,
          apr_hash_t *record_map,
          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  for (hi = apr_hash_first(scratch_pool, record_map); hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath = apr_hash_this_key(hi);
      const svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
      const char *local_relpath = svn_dirent_skip_ancestor(wcroot->abspath,
                                                           local_abspath);

      svn_pool_clear(iterpool);

      if (! local_relpath)
        continue;

      SVN_ERR(db_record_fileinfo(wcroot, local_relpath,
                                 dirent->filesize, dirent->mtime,
                                 iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_wq_record_and_fetch_next(apr_uint64_t *id,
                                    svn_skel_t **work_item,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    apr_uint64_t completed_id,
                                    apr_hash_t *record_map,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(id != NULL);
  SVN_ERR_ASSERT(work_item != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    svn_error_compose_create(
            wq_fetch_next(id, work_item,
                          wcroot, local_relpath, completed_id,
                          result_pool, scratch_pool),
            wq_record(wcroot, record_map, scratch_pool)),
    wcroot);

  return SVN_NO_ERROR;
}



/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_get_format(int *format,
                           svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  err = svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                local_dir_abspath, scratch_pool, scratch_pool);

  /* If we hit an error examining this directory, then declare this
     directory to not be a working copy.  */
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
        return svn_error_trace(err);
      svn_error_clear(err);

      /* Remap the returned error.  */
      *format = 0;
      return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                               _("'%s' is not a working copy"),
                               svn_dirent_local_style(local_dir_abspath,
                                                      scratch_pool));
    }

  SVN_ERR_ASSERT(wcroot != NULL);
  SVN_ERR_ASSERT(wcroot->format >= 1);

  *format = wcroot->format;

  return SVN_NO_ERROR;
}

/* ### temporary API. remove before release.  */
svn_wc_adm_access_t *
svn_wc__db_temp_get_access(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));

  /* ### we really need to assert that we were passed a directory. sometimes
     ### adm_retrieve_internal is asked about a file, and then it asks us
     ### for an access baton for it. we should definitely return NULL, but
     ### ideally: the caller would never ask us about a non-directory.  */

  err = svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

  if (!wcroot)
    return NULL;

  return svn_hash_gets(wcroot->access_cache, local_dir_abspath);
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_set_access(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  err = svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, scratch_pool, scratch_pool);
  if (err)
    {
      /* We don't even have a wcroot, so just bail. */
      svn_error_clear(err);
      return;
    }

  /* Better not override something already there.  */
  SVN_ERR_ASSERT_NO_RETURN(
    svn_hash_gets(wcroot->access_cache, local_dir_abspath) == NULL
  );
  svn_hash_sets(wcroot->access_cache, local_dir_abspath, adm_access);
}


/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_close_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             svn_wc_adm_access_t *adm_access,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_dir_abspath, scratch_pool, scratch_pool));
  svn_hash_sets(wcroot->access_cache, local_dir_abspath, NULL);

  return SVN_NO_ERROR;
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_clear_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  err = svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return;
    }

  svn_hash_sets(wcroot->access_cache, local_dir_abspath, NULL);
}


apr_hash_t *
svn_wc__db_temp_get_all_access(svn_wc__db_t *db,
                               apr_pool_t *result_pool)
{
  apr_hash_t *result = apr_hash_make(result_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(result_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      const svn_wc__db_wcroot_t *wcroot = apr_hash_this_val(hi);

      /* This is highly redundant, 'cause the same WCROOT will appear many
         times in dir_data. */
      result = apr_hash_overlay(result_pool, result, wcroot->access_cache);
    }

  return result;
}


svn_error_t *
svn_wc__db_temp_borrow_sdb(svn_sqlite__db_t **sdb,
                           svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                            local_dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *sdb = wcroot->sdb;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_conflict_victims(const apr_array_header_t **victims,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *new_victims;

  /* The parent should be a working copy directory. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### This will be much easier once we have all conflicts in one
         field of actual*/

  /* Look for text, tree and property conflicts in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_CONFLICT_VICTIMS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  new_victims = apr_array_make(result_pool, 0, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      APR_ARRAY_PUSH(new_victims, const char *) =
                            svn_relpath_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  *victims = new_victims;
  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_get_conflict_marker_files().
 */
static svn_error_t *
get_conflict_marker_files(apr_hash_t **marker_files_p,
                          svn_wc__db_wcroot_t *wcroot,
                          const char *local_relpath,
                          svn_wc__db_t *db,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_t *marker_files = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row && !svn_sqlite__column_is_null(stmt, 2))
    {
      apr_size_t len;
      const void *data = svn_sqlite__column_blob(stmt, 2, &len, NULL);
      svn_skel_t *conflicts;
      const apr_array_header_t *markers;
      int i;

      conflicts = svn_skel__parse(data, len, scratch_pool);

      /* ### ADD markers to *marker_files */
      SVN_ERR(svn_wc__conflict_read_markers(&markers, db, wcroot->abspath,
                                            conflicts,
                                            result_pool, scratch_pool));

      for (i = 0; markers && (i < markers->nelts); i++)
        {
          const char *marker_abspath = APR_ARRAY_IDX(markers, i, const char*);

          svn_hash_sets(marker_files, marker_abspath, "");
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_CONFLICT_VICTIMS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      apr_size_t len;
      const void *data = svn_sqlite__column_blob(stmt, 1, &len, NULL);

      const apr_array_header_t *markers;
      int i;

      if (data)
        {
          svn_skel_t *conflicts;
          conflicts = svn_skel__parse(data, len, scratch_pool);

          SVN_ERR(svn_wc__conflict_read_markers(&markers, db, wcroot->abspath,
                                                conflicts,
                                                result_pool, scratch_pool));

          for (i = 0; markers && (i < markers->nelts); i++)
            {
              const char *marker_abspath = APR_ARRAY_IDX(markers, i, const char*);

              svn_hash_sets(marker_files, marker_abspath, "");
            }
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  if (apr_hash_count(marker_files))
    *marker_files_p = marker_files;
  else
    *marker_files_p = NULL;

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_get_conflict_marker_files(apr_hash_t **marker_files,
                                     svn_wc__db_t *db,
                                     const char *local_abspath,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  /* The parent should be a working copy directory. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    get_conflict_marker_files(marker_files, wcroot, local_relpath, db,
                              result_pool, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_conflict(svn_skel_t **conflict,
                         svn_node_kind_t *kind,
                         apr_hash_t **props,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  /* The parent should be a working copy directory. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(svn_wc__db_read_conflict_internal(conflict, kind, props,
                                                           wcroot, local_relpath,
                                                           result_pool,
                                                           scratch_pool));
}

svn_error_t *
svn_wc__db_read_conflict_internal(svn_skel_t **conflict,
                                  svn_node_kind_t *kind,
                                  apr_hash_t **props,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *local_relpath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  if (kind)
    *kind = svn_node_none;
  if (props)
    *props = NULL;

  /* Check if we have a conflict in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      apr_size_t cfl_len;
      const void *cfl_data;

      /* svn_skel__parse doesn't copy data, so store in result_pool */
      cfl_data = svn_sqlite__column_blob(stmt, 2, &cfl_len, result_pool);

      if (cfl_data)
        *conflict = svn_skel__parse(cfl_data, cfl_len, result_pool);
      else
        *conflict = NULL;

      if (props)
        {
          svn_error_t *err;

          err = svn_error_trace(svn_sqlite__column_properties(props, stmt, 1,
                                                              result_pool,
                                                              scratch_pool));

          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }
    }
  else
    *conflict = NULL;

  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row || kind || (props && !*props))
    {
      svn_error_t *err = NULL;
      svn_boolean_t have_info = FALSE;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_NODE_INFO));

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id,
                                local_relpath));

      SVN_ERR(svn_sqlite__step(&have_info, stmt));

      if (have_info)
        {
          if (kind)
            {
              svn_wc__db_status_t status;
              int op_depth = svn_sqlite__column_int(stmt, 0);

              status = svn_sqlite__column_token(stmt, 3, presence_map);

              if (op_depth > 0)
                err = convert_to_working_status(&status, status);

              if (!err && (status == svn_wc__db_status_normal
                           || status == svn_wc__db_status_added
                           || status == svn_wc__db_status_deleted
                           || status == svn_wc__db_status_incomplete))
                {
                  *kind = svn_sqlite__column_token(stmt, 4, kind_map);
                }
            }

          /* Need props, and no props in ACTUAL? */
          if (!err && (props && !*props))
            {
              err = svn_sqlite__column_properties(props, stmt, 14,
                                                  result_pool, scratch_pool);
            }
        }

      SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

      if (!have_row && !have_info)
        {
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("The node '%s' was not found."),
                                   path_for_error_message(wcroot,
                                                          local_relpath,
                                                          scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_kind(svn_node_kind_t *kind,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_boolean_t allow_missing,
                     svn_boolean_t show_deleted,
                     svn_boolean_t show_hidden,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt_info;
  svn_boolean_t have_info;
  svn_wc__db_status_t status;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt_info, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt_info, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_info, stmt_info));

  if (!have_info)
    {
      if (allow_missing)
        {
          *kind = svn_node_unknown;
          SVN_ERR(svn_sqlite__reset(stmt_info));
          return SVN_NO_ERROR;
        }
      else
        {
          SVN_ERR(svn_sqlite__reset(stmt_info));
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("The node '%s' was not found."),
                                   path_for_error_message(wcroot,
                                                          local_relpath,
                                                          scratch_pool));
        }
    }

  status = svn_sqlite__column_token(stmt_info, 3, presence_map);

  if (show_deleted && status == svn_wc__db_status_base_deleted)
    {
      /* Let's return the kind of what is really deleted insead of what
         we have cached in the base-deleted record */

      SVN_ERR(svn_sqlite__step(&have_info, stmt_info));

      if (!have_info)
        {
          /* No lower layer deleted? Database inconsistency! */
          *kind = svn_node_none;
          return svn_error_trace(svn_sqlite__reset(stmt_info));
        }
    }

  if (!(show_deleted && show_hidden))
    {
      int op_depth = svn_sqlite__column_int(stmt_info, 0);
      svn_boolean_t report_none = FALSE;

      if (op_depth > 0)
        SVN_ERR(convert_to_working_status(&status, status));

      switch (status)
        {
          case svn_wc__db_status_not_present:
            if (! (show_hidden && show_deleted))
              report_none = TRUE;
            break;
          case svn_wc__db_status_excluded:
          case svn_wc__db_status_server_excluded:
            if (! show_hidden)
              report_none = TRUE;
            break;
          case svn_wc__db_status_deleted:
            if (! show_deleted)
              report_none = TRUE;
            break;
          default:
            break;
        }

      if (report_none)
        {
          *kind = svn_node_none;
          return svn_error_trace(svn_sqlite__reset(stmt_info));
        }
    }

  *kind = svn_sqlite__column_token(stmt_info, 4, kind_map);

  return svn_error_trace(svn_sqlite__reset(stmt_info));
}

svn_error_t *
svn_wc__db_is_wcroot(svn_boolean_t *is_wcroot,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (*local_relpath != '\0')
    {
      *is_wcroot = FALSE; /* Node is a file, or has a parent directory within
                           the same wcroot */
      return SVN_NO_ERROR;
    }

   *is_wcroot = TRUE;

   return SVN_NO_ERROR;
}

/* Find a node's kind and whether it is switched, putting the outputs in
 * *IS_SWITCHED and *KIND. Either of the outputs may be NULL if not wanted.
 */
static svn_error_t *
db_is_switched(svn_boolean_t *is_switched,
               svn_node_kind_t *kind,
               svn_wc__db_wcroot_t *wcroot,
               const char *local_relpath,
               apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  apr_int64_t repos_id;
  const char *repos_relpath;
  const char *name;
  const char *parent_local_relpath;
  apr_int64_t parent_repos_id;
  const char *parent_repos_relpath;

  SVN_ERR_ASSERT(*local_relpath != '\0'); /* Handled in wrapper */

  SVN_ERR(read_info(&status, kind, NULL, &repos_relpath, &repos_id, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    wcroot, local_relpath, scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_server_excluded
      || status == svn_wc__db_status_excluded
      || status == svn_wc__db_status_not_present)
    {
      return svn_error_createf(
                    SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                    _("The node '%s' was not found."),
                    path_for_error_message(wcroot, local_relpath,
                                           scratch_pool));
    }
  else if (! repos_relpath)
    {
      /* Node is shadowed; easy out */
      if (is_switched)
        *is_switched = FALSE;

      return SVN_NO_ERROR;
    }

  if (! is_switched)
    return SVN_NO_ERROR;

  svn_relpath_split(&parent_local_relpath, &name, local_relpath, scratch_pool);

  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                            &parent_repos_relpath,
                                            &parent_repos_id, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL,
                                            wcroot, parent_local_relpath,
                                            scratch_pool, scratch_pool));

  if (repos_id != parent_repos_id)
    *is_switched = TRUE;
  else
    {
      const char *expected_relpath;

      expected_relpath = svn_relpath_join(parent_repos_relpath, name,
                                          scratch_pool);

      *is_switched = (strcmp(expected_relpath, repos_relpath) != 0);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_is_switched(svn_boolean_t *is_wcroot,
                       svn_boolean_t *is_switched,
                       svn_node_kind_t *kind,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (is_switched)
    *is_switched = FALSE;

  if (*local_relpath == '\0')
    {
      /* Easy out */
      if (is_wcroot)
        *is_wcroot = TRUE;

      if (kind)
        *kind = svn_node_dir;
      return SVN_NO_ERROR;
    }

  if (is_wcroot)
    *is_wcroot = FALSE;

  if (! is_switched && ! kind)
    return SVN_NO_ERROR;

  SVN_WC__DB_WITH_TXN(
    db_is_switched(is_switched, kind, wcroot, local_relpath, scratch_pool),
    wcroot);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_wcroot_tempdir(const char **temp_dir_abspath,
                               svn_wc__db_t *db,
                               const char *wri_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(temp_dir_abspath != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *temp_dir_abspath = svn_dirent_join_many(result_pool,
                                           wcroot->abspath,
                                           svn_wc_get_adm_dir(scratch_pool),
                                           WCROOT_TEMPDIR_RELPATH,
                                           SVN_VA_NULL);
  return SVN_NO_ERROR;
}


/* Helper for wclock_obtain_cb() to steal an existing lock */
static svn_error_t *
wclock_steal(svn_wc__db_wcroot_t *wcroot,
             const char *local_relpath,
             apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_DELETE_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_wclock_obtain().
 */
static svn_error_t *
wclock_obtain_cb(svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 int levels_to_lock,
                 svn_boolean_t steal_lock,
                 svn_boolean_t enforce_empty_wq,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;
  const char *lock_relpath;
  int max_depth;
  int lock_depth;
  svn_boolean_t got_row;

  svn_wc__db_wclock_t lock;

  /* Upgrade locks the root before the node exists.  Apart from that
     the root node always exists so we will just skip the check.

     ### Perhaps the lock for upgrade should be created when the db is
         created?  1.6 used to lock .svn on creation. */
  if (local_relpath[0])
    {
      svn_boolean_t exists;

      SVN_ERR(does_node_exist(&exists, wcroot, local_relpath));
      if (!exists)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));
    }

  if (enforce_empty_wq)
    SVN_ERR(svn_wc__db_verify_no_work(wcroot->sdb));

  /* Check if there are nodes locked below the new lock root */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_FIND_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  lock_depth = relpath_depth(local_relpath);
  max_depth = lock_depth + levels_to_lock;

  SVN_ERR(svn_sqlite__step(&got_row, stmt));

  while (got_row)
    {
      svn_boolean_t own_lock;

      lock_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);

      /* If we are not locking with depth infinity, check if this lock
         voids our lock request */
      if (levels_to_lock >= 0
          && relpath_depth(lock_relpath) > max_depth)
        {
          SVN_ERR(svn_sqlite__step(&got_row, stmt));
          continue;
        }

      /* Check if we are the lock owner, because we should be able to
         extend our lock. */
      err = svn_wc__db_wclock_owns_lock_internal(&own_lock, wcroot,
                                                 lock_relpath,
                                                 TRUE, scratch_pool);

      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

      if (!own_lock && !steal_lock)
        {
          SVN_ERR(svn_sqlite__reset(stmt));
          err = svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                                   _("'%s' is already locked."),
                                   path_for_error_message(wcroot,
                                                          lock_relpath,
                                                          scratch_pool));
          return svn_error_createf(SVN_ERR_WC_LOCKED, err,
                                   _("Working copy '%s' locked."),
                                   path_for_error_message(wcroot,
                                                          local_relpath,
                                                          scratch_pool));
        }
      else if (!own_lock)
        {
          err = wclock_steal(wcroot, lock_relpath, scratch_pool);

          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
        }

      SVN_ERR(svn_sqlite__step(&got_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (steal_lock)
    SVN_ERR(wclock_steal(wcroot, local_relpath, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_SELECT_WC_LOCK));
  lock_relpath = local_relpath;

  while (TRUE)
    {
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, lock_relpath));

      SVN_ERR(svn_sqlite__step(&got_row, stmt));

      if (got_row)
        {
          int levels = svn_sqlite__column_int(stmt, 0);
          if (levels >= 0)
            levels += relpath_depth(lock_relpath);

          SVN_ERR(svn_sqlite__reset(stmt));

          if (levels == -1 || levels >= lock_depth)
            {

              err = svn_error_createf(
                              SVN_ERR_WC_LOCKED, NULL,
                              _("'%s' is already locked."),
                              svn_dirent_local_style(
                                       svn_dirent_join(wcroot->abspath,
                                                       lock_relpath,
                                                       scratch_pool),
                              scratch_pool));
              return svn_error_createf(
                              SVN_ERR_WC_LOCKED, err,
                              _("Working copy '%s' locked."),
                              path_for_error_message(wcroot,
                                                     local_relpath,
                                                     scratch_pool));
            }

          break; /* There can't be interesting locks on higher nodes */
        }
      else
        SVN_ERR(svn_sqlite__reset(stmt));

      if (!*lock_relpath)
        break;

      lock_relpath = svn_relpath_dirname(lock_relpath, scratch_pool);
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            levels_to_lock));
  err = svn_sqlite__insert(NULL, stmt);
  if (err)
    return svn_error_createf(SVN_ERR_WC_LOCKED, err,
                             _("Failed to lock working copy '%s'."),
                             path_for_error_message(wcroot,
                                                    local_relpath,
                                                    scratch_pool));

  /* And finally store that we obtained the lock */
  lock.local_relpath = apr_pstrdup(wcroot->owned_locks->pool, local_relpath);
  lock.levels = levels_to_lock;
  APR_ARRAY_PUSH(wcroot->owned_locks, svn_wc__db_wclock_t) = lock;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wclock_obtain(svn_wc__db_t *db,
                         const char *local_abspath,
                         int levels_to_lock,
                         svn_boolean_t steal_lock,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(levels_to_lock >= -1);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                             db, local_abspath,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (!steal_lock)
    {
      int i;
      int depth = relpath_depth(local_relpath);

      for (i = 0; i < wcroot->owned_locks->nelts; i++)
        {
          svn_wc__db_wclock_t* lock = &APR_ARRAY_IDX(wcroot->owned_locks,
                                                     i, svn_wc__db_wclock_t);

          if (svn_relpath_skip_ancestor(lock->local_relpath, local_relpath)
              && (lock->levels == -1
                  || (lock->levels + relpath_depth(lock->local_relpath))
                            >= depth))
            {
              return svn_error_createf(
                SVN_ERR_WC_LOCKED, NULL,
                _("'%s' is already locked via '%s'."),
                svn_dirent_local_style(local_abspath, scratch_pool),
                path_for_error_message(wcroot, lock->local_relpath,
                                       scratch_pool));
            }
        }
    }

  SVN_WC__DB_WITH_TXN(
    wclock_obtain_cb(wcroot, local_relpath, levels_to_lock, steal_lock,
                     db->enforce_empty_wq, scratch_pool),
    wcroot);
  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_wclock_find_root() and svn_wc__db_wclocked(). */
static svn_error_t *
find_wclock(const char **lock_relpath,
            svn_wc__db_wcroot_t *wcroot,
            const char *dir_relpath,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int dir_depth = relpath_depth(dir_relpath);
  const char *first_relpath;

  /* Check for locks on all directories that might be ancestors.
     As our new apis only use recursive locks the number of locks stored
     in the DB will be very low */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ANCESTOR_WCLOCKS));

  /* Get the top level relpath to reduce the worst case number of results
     to the number of directories below this node plus two.
     (1: the node itself and 2: the wcroot). */
  first_relpath = strchr(dir_relpath, '/');

  if (first_relpath != NULL)
    first_relpath = apr_pstrndup(scratch_pool, dir_relpath,
                                 first_relpath - dir_relpath);
  else
    first_relpath = dir_relpath;

  SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                            wcroot->wc_id,
                            dir_relpath,
                            first_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      const char *relpath = svn_sqlite__column_text(stmt, 0, NULL);

      if (svn_relpath_skip_ancestor(relpath, dir_relpath))
        {
          int locked_levels = svn_sqlite__column_int(stmt, 1);
          int row_depth = relpath_depth(relpath);

          if (locked_levels == -1
              || locked_levels + row_depth >= dir_depth)
            {
              *lock_relpath = apr_pstrdup(result_pool, relpath);
              SVN_ERR(svn_sqlite__reset(stmt));
              return SVN_NO_ERROR;
            }
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *lock_relpath = NULL;

  return svn_error_trace(svn_sqlite__reset(stmt));
}

static svn_error_t *
is_wclocked(svn_boolean_t *locked,
            svn_wc__db_wcroot_t *wcroot,
            const char *dir_relpath,
            apr_pool_t *scratch_pool)
{
  const char *lock_relpath;

  SVN_ERR(find_wclock(&lock_relpath, wcroot, dir_relpath,
                      scratch_pool, scratch_pool));
  *locked = (lock_relpath != NULL);
  return SVN_NO_ERROR;
}


svn_error_t*
svn_wc__db_wclock_find_root(const char **lock_abspath,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *lock_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    find_wclock(&lock_relpath, wcroot, local_relpath,
                scratch_pool, scratch_pool),
    wcroot);

  if (!lock_relpath)
    *lock_abspath = NULL;
  else
    SVN_ERR(svn_wc__db_from_relpath(lock_abspath, db, wcroot->abspath,
                                    lock_relpath, result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wclocked(svn_boolean_t *locked,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    is_wclocked(locked, wcroot, local_relpath, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wclock_release(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  int i;
  apr_array_header_t *owned_locks;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  /* First check and remove the owns-lock information as failure in
     removing the db record implies that we have to steal the lock later. */
  owned_locks = wcroot->owned_locks;
  for (i = 0; i < owned_locks->nelts; i++)
    {
      svn_wc__db_wclock_t *lock = &APR_ARRAY_IDX(owned_locks, i,
                                                 svn_wc__db_wclock_t);

      if (strcmp(lock->local_relpath, local_relpath) == 0)
        break;
    }

  if (i >= owned_locks->nelts)
    return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                             _("Working copy not locked at '%s'."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (i < owned_locks->nelts)
    {
      owned_locks->nelts--;

      /* Move the last item in the array to the deleted place */
      if (owned_locks->nelts > 0)
        APR_ARRAY_IDX(owned_locks, i, svn_wc__db_wclock_t) =
           APR_ARRAY_IDX(owned_locks, owned_locks->nelts, svn_wc__db_wclock_t);
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WC_LOCK));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


/* Like svn_wc__db_wclock_owns_lock() but taking WCROOT+LOCAL_RELPATH instead
   of DB+LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__db_wclock_owns_lock_internal(svn_boolean_t *own_lock,
                                     svn_wc__db_wcroot_t *wcroot,
                                     const char *local_relpath,
                                     svn_boolean_t exact,
                                     apr_pool_t *scratch_pool)
{
  apr_array_header_t *owned_locks;
  int lock_level;
  int i;

  *own_lock = FALSE;
  owned_locks = wcroot->owned_locks;
  lock_level = relpath_depth(local_relpath);

  if (exact)
    {
      for (i = 0; i < owned_locks->nelts; i++)
        {
          svn_wc__db_wclock_t *lock = &APR_ARRAY_IDX(owned_locks, i,
                                                     svn_wc__db_wclock_t);

          if (strcmp(lock->local_relpath, local_relpath) == 0)
            {
              *own_lock = TRUE;
              return SVN_NO_ERROR;
            }
        }
    }
  else
    {
      for (i = 0; i < owned_locks->nelts; i++)
        {
          svn_wc__db_wclock_t *lock = &APR_ARRAY_IDX(owned_locks, i,
                                                     svn_wc__db_wclock_t);

          if (svn_relpath_skip_ancestor(lock->local_relpath, local_relpath)
              && (lock->levels == -1
                  || ((relpath_depth(lock->local_relpath) + lock->levels)
                      >= lock_level)))
            {
              *own_lock = TRUE;
              return SVN_NO_ERROR;
            }
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wclock_owns_lock(svn_boolean_t *own_lock,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t exact,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));

  if (!wcroot)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_wc__db_wclock_owns_lock_internal(own_lock, wcroot, local_relpath,
                                               exact, scratch_pool));

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_temp_op_end_directory_update().
 */
static svn_error_t *
end_directory_update(svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_status_t base_status;

  SVN_ERR(svn_wc__db_base_get_info_internal(&base_status, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  if (base_status == svn_wc__db_status_normal)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(base_status == svn_wc__db_status_incomplete);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_NODE_BASE_PRESENCE));
  SVN_ERR(svn_sqlite__bindf(stmt, "ist", wcroot->wc_id, local_relpath,
                            presence_map, svn_wc__db_status_normal));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_end_directory_update(svn_wc__db_t *db,
                                        const char *local_dir_abspath,
                                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_dir_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    end_directory_update(wcroot, local_relpath, scratch_pool),
    wcroot);

  SVN_ERR(flush_entries(wcroot, local_dir_abspath, svn_depth_empty,
                        scratch_pool));

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_temp_op_start_directory_update().
 */
static svn_error_t *
start_directory_update_txn(svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           const char *new_repos_relpath,
                           svn_revnum_t new_rev,
                           apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* Note: In the majority of calls, the repos_relpath is unchanged. */
  /* ### TODO: Maybe check if we can make repos_relpath NULL. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                    STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH));

  SVN_ERR(svn_sqlite__bindf(stmt, "istrs",
                            wcroot->wc_id,
                            local_relpath,
                            presence_map, svn_wc__db_status_incomplete,
                            new_rev,
                            new_repos_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;

}

svn_error_t *
svn_wc__db_temp_op_start_directory_update(svn_wc__db_t *db,
                                          const char *local_abspath,
                                          const char *new_repos_relpath,
                                          svn_revnum_t new_rev,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_rev));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(new_repos_relpath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    start_directory_update_txn(wcroot, local_relpath,
                               new_repos_relpath, new_rev, scratch_pool),
    wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath, svn_depth_empty, scratch_pool));

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_op_make_copy_internal */
static svn_error_t *
db_move_moved_to(svn_wc__db_wcroot_t *wcroot,
                 const char *src1_relpath,
                 int src1_op_depth,
                 const char *src2_relpath,
                 int src2_op_depth,
                 const char *dst_relpath,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                     STMT_UPDATE_MOVED_TO_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                            src1_relpath, src1_op_depth));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 1)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                     STMT_UPDATE_MOVED_TO_RELPATH));
      SVN_ERR(svn_sqlite__bindf(stmt, "isds", wcroot->wc_id,
                                src2_relpath, src2_op_depth,
                                dst_relpath));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
    }
  if (affected_rows != 1)
    return svn_error_create(SVN_ERR_WC_PATH_NOT_FOUND, NULL, NULL);

  return SVN_NO_ERROR;
}

static svn_error_t *
db_move_moved_to_down_recursive(svn_wc__db_wcroot_t *wcroot,
                                const char *local_relpath,
                                int new_shadow_layer,
                                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                        STMT_SELECT_MOVED_DESCENDANTS_SRC));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            new_shadow_layer));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      int del_op_depth;
      const char *src_relpath;
      const char *dst_relpath;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      del_op_depth = svn_sqlite__column_int(stmt, 0);
      src_relpath = svn_sqlite__column_text(stmt, 1, iterpool);
      dst_relpath = svn_sqlite__column_text(stmt, 4, iterpool);

      err = svn_error_trace(
               db_move_moved_to(
                             wcroot,
                             src_relpath, del_op_depth,
                             src_relpath, new_shadow_layer,
                             dst_relpath, iterpool));

      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


/* The body of svn_wc__db_temp_op_make_copy().  This is
   used by the update editor when deleting a base node tree would be a
   tree-conflict because there are changes to subtrees.  This function
   inserts a copy of the base node tree below any existing working
   subtrees.  Given a tree:

             0            1           2            3
    /     normal          -
    A     normal          -
    A/B   normal          -         normal
    A/B/C normal          -         base-del       normal
    A/F   normal          -         normal
    A/F/G normal          -         normal
    A/F/H normal          -         base-deleted   normal
    A/F/E normal          -         not-present
    A/X   normal          -
    A/X/Y incomplete      -

    This function adds layers to A and some of its descendants in an attempt
    to make the working copy look like as if it were a copy of the BASE nodes.

             0            1              2            3
    /     normal        -
    A     normal        norm
    A/B   normal        norm        norm
    A/B/C normal        norm        base-del       normal
    A/F   normal        norm        norm
    A/F/G normal        norm        norm
    A/F/H normal        norm        not-pres
    A/F/E normal        norm        base-del
    A/X   normal        norm
    A/X/Y incomplete  incomplete
 */
static svn_error_t *
make_copy_txn(svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_int64_t last_repos_id,
              const char *last_repos_relpath,
              svn_revnum_t last_revision,
              int last_op_depth,
              svn_boolean_t shadowed,
              int root_shadow_depth,
              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row = FALSE;
  svn_revnum_t revision;
  apr_int64_t repos_id;
  const char *repos_relpath;
  svn_node_kind_t kind;
  int op_depth = relpath_depth(local_relpath);

  if (last_op_depth != op_depth)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_DEPTH_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      SVN_ERR(svn_sqlite__reset(stmt));
      if (have_row)
        shadowed = TRUE;
    }

  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &kind, &revision,
                                            &repos_relpath, &repos_id, NULL,
                                            NULL, NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  if (last_repos_relpath
      && repos_id == last_repos_id
      && revision == last_revision)
    {
      const char *name = svn_relpath_skip_ancestor(last_repos_relpath,
                                                   repos_relpath);

      if (name && strcmp(name, svn_relpath_basename(local_relpath, NULL)) == 0)
        op_depth = last_op_depth;
    }

  /* Can we add a new copy node at the wanted op-depth? */
  if (!have_row || op_depth == last_op_depth)
    {
      int i;

      SVN_ERR(svn_sqlite__get_statement(
                    &stmt, wcroot->sdb,
                    STMT_INSERT_WORKING_NODE_FROM_BASE_COPY));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));

      if (shadowed)
        SVN_ERR(db_extend_parent_delete(wcroot, local_relpath, kind,
                                        op_depth, scratch_pool));

      if (kind == svn_node_dir)
        {
          const apr_array_header_t *children;
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);

          SVN_ERR(gather_children(&children, wcroot, local_relpath,
                                  STMT_SELECT_OP_DEPTH_CHILDREN, 0,
                                  scratch_pool, iterpool));

          for (i = 0; i < children->nelts; i++)
            {
              const char *name = APR_ARRAY_IDX(children, i, const char *);
              const char *copy_relpath;

              svn_pool_clear(iterpool);

              copy_relpath = svn_relpath_join(local_relpath, name, iterpool);

              SVN_ERR(make_copy_txn(wcroot, copy_relpath,
                                    repos_id, repos_relpath, revision,
                                    op_depth, shadowed, root_shadow_depth,
                                    scratch_pool));
            }
          svn_pool_destroy(iterpool);
        }
    }
  else
    {
      /* Auch... we can't make a copy of whatever comes deeper, as this
         op-depth is already filled by something else. Let's hope
         the user doesn't mind.

         Luckily we know that the moves are already moved to the shadowing
         layer, so we can just remove dangling base-deletes if there are
         any.
       */
      /* BASE_DELETED may be at op_depth, so let's use last_op_depth! */
      SVN_ERR(db_move_moved_to_down_recursive(wcroot, local_relpath,
                                              root_shadow_depth,
                                              scratch_pool));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                    STMT_DELETE_WORKING_BASE_DELETE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                last_op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                    STMT_DELETE_WORKING_BASE_DELETE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                                last_op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Insert a not-present node to mark that we don't know what exists here.

     We do this last (after recursing), to allow the move fix-up code to
     see the original moves. */
  if (last_op_depth > 0 && last_op_depth != op_depth)
    {
      insert_working_baton_t iwb;

      blank_iwb(&iwb);
      iwb.presence = svn_wc__db_status_not_present;
      iwb.op_depth = last_op_depth;

      iwb.original_repos_id = repos_id;
      iwb.original_repos_relpath = repos_relpath;
      iwb.original_revnum = revision;
      iwb.kind = kind;

      SVN_ERR(insert_working_node(&iwb, wcroot, local_relpath, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_make_copy_internal(svn_wc__db_wcroot_t *wcroot,
                                 const char *local_relpath,
                                 svn_boolean_t move_move_info,
                                 const svn_skel_t *conflicts,
                                 const svn_skel_t *work_items,
                                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int op_depth = -1;

  /* The update editor is supposed to call this function when there is
     no working node for LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    {
      if (op_depth == relpath_depth(local_relpath))
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Modification of '%s' already exists"),
                             path_for_error_message(wcroot,
                                                    local_relpath,
                                                    scratch_pool));

      /* We have a working layer, but not one at the op-depth of local-relpath,
         so we can create a copy by just copying the lower layer */

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_COPY_OP_DEPTH_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isdd", wcroot->wc_id, local_relpath,
                                op_depth, relpath_depth(local_relpath)));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      int affected_rows;

      op_depth = relpath_depth(local_relpath);
      /* We don't allow copies to contain server-excluded nodes;
         the update editor is going to have to bail out. */
      SVN_ERR(catch_copy_of_server_excluded(wcroot, local_relpath,
                                            scratch_pool));

      /* Insert a shadowing layer */
      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, wcroot->sdb,
                        STMT_INSERT_DELETE_FROM_NODE_RECURSIVE));

      /* As we are keeping whatever is below, move the*/

      SVN_ERR(svn_sqlite__bindf(stmt, "isdd",
                                wcroot->wc_id, local_relpath,
                                0, op_depth));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));
      SVN_ERR_ASSERT(affected_rows > 0);

      if (!move_move_info)
        SVN_ERR(db_move_moved_to_down_recursive(wcroot, local_relpath,
                                                op_depth, scratch_pool));


      SVN_ERR(make_copy_txn(wcroot, local_relpath,
                            INVALID_REPOS_ID, NULL, SVN_INVALID_REVNUM,
                            op_depth, FALSE, op_depth,
                            scratch_pool));
    }

  if (conflicts)
    SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                              conflicts, scratch_pool));

  SVN_ERR(add_work_items(wcroot->sdb, work_items, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_make_copy(svn_wc__db_t *db,
                        const char *local_abspath,
                        const svn_skel_t *conflicts,
                        const svn_skel_t *work_items,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    svn_wc__db_op_make_copy_internal(wcroot, local_relpath, FALSE,
                                     conflicts, work_items,
                                     scratch_pool),
    wcroot);

  SVN_ERR(flush_entries(wcroot, local_abspath,
                        svn_depth_infinity, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_info_below_working(svn_boolean_t *have_base,
                              svn_boolean_t *have_work,
                              svn_wc__db_status_t *status,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  SVN_ERR(info_below_working(have_base, have_work, status,
                             wcroot, local_relpath, -1, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_get_not_present_descendants(const apr_array_header_t **descendants,
                                       svn_wc__db_t *db,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NOT_PRESENT_DESCENDANTS));

  SVN_ERR(svn_sqlite__bindf(stmt, "isd",
                            wcroot->wc_id,
                            local_relpath,
                            relpath_depth(local_relpath)));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      apr_array_header_t *paths;

      paths = apr_array_make(result_pool, 4, sizeof(const char*));
      while (have_row)
        {
          const char *found_relpath = svn_sqlite__column_text(stmt, 0, NULL);

          APR_ARRAY_PUSH(paths, const char *)
              = apr_pstrdup(result_pool, svn_relpath_skip_ancestor(
                                           local_relpath, found_relpath));

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }

      *descendants = paths;
    }
  else
    *descendants = apr_array_make(result_pool, 0, sizeof(const char*));

  return svn_error_trace(svn_sqlite__reset(stmt));
}


/* Like svn_wc__db_min_max_revisions(),
 * but accepts a WCROOT/LOCAL_RELPATH pair. */
static svn_error_t *
get_min_max_revisions(svn_revnum_t *min_revision,
                      svn_revnum_t *max_revision,
                      svn_wc__db_wcroot_t *wcroot,
                      const char *local_relpath,
                      svn_boolean_t committed,
                      apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_revnum_t min_rev, max_rev;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MIN_MAX_REVISIONS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_row(stmt));

  if (committed)
    {
      min_rev = svn_sqlite__column_revnum(stmt, 2);
      max_rev = svn_sqlite__column_revnum(stmt, 3);
    }
  else
    {
      min_rev = svn_sqlite__column_revnum(stmt, 0);
      max_rev = svn_sqlite__column_revnum(stmt, 1);
    }

  /* The statement returns exactly one row. */
  SVN_ERR(svn_sqlite__reset(stmt));

  if (min_revision)
    *min_revision = min_rev;
  if (max_revision)
    *max_revision = max_rev;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_min_max_revisions(svn_revnum_t *min_revision,
                             svn_revnum_t *max_revision,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t committed,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(get_min_max_revisions(min_revision, max_revision,
                                               wcroot, local_relpath,
                                               committed, scratch_pool));
}


/* Set *IS_SPARSE_CHECKOUT TRUE if LOCAL_RELPATH or any of the nodes
 * within LOCAL_RELPATH is sparse, FALSE otherwise. */
static svn_error_t *
is_sparse_checkout_internal(svn_boolean_t *is_sparse_checkout,
                            svn_wc__db_wcroot_t *wcroot,
                            const char *local_relpath,
                            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_HAS_SPARSE_NODES));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id,
                            local_relpath));
  /* If this query returns a row, the working copy is sparse. */
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  *is_sparse_checkout = have_row;
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


/* Like svn_wc__db_has_switched_subtrees(),
 * but accepts a WCROOT/LOCAL_RELPATH pair. */
static svn_error_t *
has_switched_subtrees(svn_boolean_t *is_switched,
                      svn_wc__db_wcroot_t *wcroot,
                      const char *local_relpath,
                      const char *trail_url,
                      apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t repos_id;
  const char *repos_relpath;

  /* Optional argument handling for caller */
  if (!is_switched)
    return SVN_NO_ERROR;

  *is_switched = FALSE;

  SVN_ERR(svn_wc__db_base_get_info_internal(NULL, NULL, NULL,
                                            &repos_relpath, &repos_id,
                                            NULL, NULL, NULL, NULL, NULL,
                                            NULL, NULL, NULL, NULL, NULL,
                                            wcroot, local_relpath,
                                            scratch_pool, scratch_pool));

  /* First do the cheap check where we only need info on the origin itself */
  if (trail_url != NULL)
    {
      const char *repos_root_url;
      const char *url;
      apr_size_t len1, len2;

      /* If the trailing part of the URL of the working copy directory
         does not match the given trailing URL then the whole working
         copy is switched. */

      SVN_ERR(svn_wc__db_fetch_repos_info(&repos_root_url, NULL, wcroot,
                                          repos_id, scratch_pool));
      url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                        scratch_pool);

      len1 = strlen(trail_url);
      len2 = strlen(url);
      if ((len1 > len2) || strcmp(url + len2 - len1, trail_url))
        {
          *is_switched = TRUE;
          return SVN_NO_ERROR;
        }
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_HAS_SWITCHED));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath, repos_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    *is_switched = TRUE;
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_has_switched_subtrees(svn_boolean_t *is_switched,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 const char *trail_url,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(has_switched_subtrees(is_switched, wcroot,
                                               local_relpath, trail_url,
                                               scratch_pool));
}

svn_error_t *
svn_wc__db_get_excluded_subtrees(apr_hash_t **excluded_subtrees,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ALL_EXCLUDED_DESCENDANTS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    *excluded_subtrees = apr_hash_make(result_pool);
  else
    *excluded_subtrees = NULL;

  while (have_row)
    {
      const char *abs_path =
        svn_dirent_join(wcroot->abspath,
                        svn_sqlite__column_text(stmt, 0, NULL),
                        result_pool);
      svn_hash_sets(*excluded_subtrees, abs_path, abs_path);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));
  return SVN_NO_ERROR;
}

/* Like svn_wc__db_has_db_mods(),
 * but accepts a WCROOT/LOCAL_RELPATH pair.
 * ### This needs a DB as well as a WCROOT/RELPATH pair... */
static svn_error_t *
has_db_mods(svn_boolean_t *is_modified,
            svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* Check for additions or deletions. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SUBTREE_HAS_TREE_MODIFICATIONS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  /* If this query returns a row, the working copy is modified. */
  SVN_ERR(svn_sqlite__step(is_modified, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (! *is_modified)
    {
      /* Check for property modifications. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SUBTREE_HAS_PROP_MODIFICATIONS));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      /* If this query returns a row, the working copy is modified. */
      SVN_ERR(svn_sqlite__step(is_modified, stmt));
      SVN_ERR(svn_sqlite__reset(stmt));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_has_db_mods(svn_boolean_t *is_modified,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(has_db_mods(is_modified, wcroot, local_relpath,
                                     scratch_pool));
}


/* The body of svn_wc__db_revision_status().
 */
static svn_error_t *
revision_status_txn(svn_revnum_t *min_revision,
                    svn_revnum_t *max_revision,
                    svn_boolean_t *is_sparse_checkout,
                    svn_boolean_t *is_modified,
                    svn_boolean_t *is_switched,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    svn_wc__db_t *db,
                    const char *trail_url,
                    svn_boolean_t committed,
                    apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t exists;

  SVN_ERR(does_node_exist(&exists, wcroot, local_relpath));

  if (!exists)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot, local_relpath,
                                                      scratch_pool));
    }

  /* Determine mixed-revisionness. */
  SVN_ERR(get_min_max_revisions(min_revision, max_revision, wcroot,
                                local_relpath, committed, scratch_pool));

  /* Determine sparseness. */
  SVN_ERR(is_sparse_checkout_internal(is_sparse_checkout, wcroot,
                                      local_relpath, scratch_pool));

  /* Check for switched nodes. */
  {
    err = has_switched_subtrees(is_switched, wcroot, local_relpath,
                                trail_url, scratch_pool);

    if (err)
      {
        if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
          return svn_error_trace(err);

        svn_error_clear(err); /* No Base node, but no fatal error */
        *is_switched = FALSE;
      }
  }

  /* Check for db mods. */
  SVN_ERR(has_db_mods(is_modified, wcroot, local_relpath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_revision_status(svn_revnum_t *min_revision,
                           svn_revnum_t *max_revision,
                           svn_boolean_t *is_sparse_checkout,
                           svn_boolean_t *is_modified,
                           svn_boolean_t *is_switched,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           const char *trail_url,
                           svn_boolean_t committed,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    revision_status_txn(min_revision, max_revision,
                        is_sparse_checkout, is_modified, is_switched,
                        wcroot, local_relpath, db,
                        trail_url, committed,
                        scratch_pool),
    wcroot);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_lock_tokens_recursive(apr_hash_t **lock_tokens,
                                          svn_wc__db_t *db,
                                          const char *local_abspath,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t last_repos_id = INVALID_REPOS_ID;
  const char *last_repos_root_url = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *lock_tokens = apr_hash_make(result_pool);

  /* Fetch all the lock tokens in and under LOCAL_RELPATH. */
  SVN_ERR(svn_sqlite__get_statement(
              &stmt, wcroot->sdb,
              STMT_SELECT_BASE_NODE_LOCK_TOKENS_RECURSIVE));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      apr_int64_t child_repos_id = svn_sqlite__column_int64(stmt, 0);
      const char *child_relpath = svn_sqlite__column_text(stmt, 1, NULL);
      const char *lock_token = svn_sqlite__column_text(stmt, 2, result_pool);

      if (child_repos_id != last_repos_id)
        {
          svn_error_t *err = svn_wc__db_fetch_repos_info(&last_repos_root_url,
                                                         NULL, wcroot,
                                                         child_repos_id,
                                                         scratch_pool);

          if (err)
            {
              return svn_error_trace(
                            svn_error_compose_create(err,
                                                     svn_sqlite__reset(stmt)));
            }

          last_repos_id = child_repos_id;
        }

      SVN_ERR_ASSERT(last_repos_root_url != NULL);
      svn_hash_sets(*lock_tokens,
                    svn_path_url_add_component2(last_repos_root_url,
                                                child_relpath, result_pool),
                    lock_token);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  return svn_sqlite__reset(stmt);
}


/* If EXPRESSION is false, cause the caller to return an SVN_ERR_WC_CORRUPT
 * error, showing EXPRESSION and the caller's LOCAL_RELPATH in the message. */
#define VERIFY(expression) \
  do { \
    if (! (expression)) \
      return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL, \
        _("database inconsistency at local_relpath='%s' verifying " \
          "expression '%s'"), local_relpath, #expression); \
  } while (0)


/* Verify consistency of the metadata concerning WCROOT.  This is intended
 * for use only during testing and debugging, so is not intended to be
 * blazingly fast.
 *
 * This code is a complement to any verification that we can do in SQLite
 * triggers.  See, for example, 'wc-checks.sql'.
 *
 * Some more verification steps we might want to add are:
 *
 *   * on every ACTUAL row (except root): a NODES row exists at its parent path
 *   * the op-depth root must always exist and every intermediate too
 */
static svn_error_t *
verify_wcroot(svn_wc__db_wcroot_t *wcroot,
              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ALL_NODES));
  SVN_ERR(svn_sqlite__bindf(stmt, "i", wcroot->wc_id));
  while (TRUE)
    {
      svn_boolean_t have_row;
      const char *local_relpath, *parent_relpath;
      int op_depth;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        break;

      op_depth = svn_sqlite__column_int(stmt, 0);
      local_relpath = svn_sqlite__column_text(stmt, 1, iterpool);
      parent_relpath = svn_sqlite__column_text(stmt, 2, iterpool);

      /* Verify parent_relpath is the parent path of local_relpath */
      VERIFY((parent_relpath == NULL)
             ? (local_relpath[0] == '\0')
             : (strcmp(svn_relpath_dirname(local_relpath, iterpool),
                       parent_relpath) == 0));

      /* Verify op_depth <= the tree depth of local_relpath */
      VERIFY(op_depth <= relpath_depth(local_relpath));

      /* Verify parent_relpath refers to a row that exists */
      /* TODO: Verify there is a suitable parent row - e.g. has op_depth <=
       * the child's and a suitable presence */
      if (parent_relpath && svn_sqlite__column_is_null(stmt, 3))
        {
          svn_sqlite__stmt_t *stmt2;
          svn_boolean_t have_a_parent_row;

          SVN_ERR(svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                            STMT_SELECT_NODE_INFO));
          SVN_ERR(svn_sqlite__bindf(stmt2, "is", wcroot->wc_id,
                                    parent_relpath));
          SVN_ERR(svn_sqlite__step(&have_a_parent_row, stmt2));
          VERIFY(have_a_parent_row);
          SVN_ERR(svn_sqlite__reset(stmt2));
        }
    }
  svn_pool_destroy(iterpool);

  return svn_error_trace(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_verify(svn_wc__db_t *db,
                  const char *wri_abspath,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, wri_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(verify_wcroot(wcroot, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_verify_db_full_internal(svn_wc__db_wcroot_t *wcroot,
                                   svn_wc__db_verify_cb_t callback,
                                   void *baton,
                                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_STATIC_VERIFY));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      const char *local_relpath;
      int op_depth = svn_sqlite__column_int(stmt, 1);
      int id = svn_sqlite__column_int(stmt, 2);
      const char *msg;

      svn_pool_clear(iterpool);

      local_relpath =  svn_sqlite__column_text(stmt, 0, iterpool);
      msg = svn_sqlite__column_text(stmt, 3, scratch_pool);

      err = callback(baton, wcroot->abspath, local_relpath, op_depth,
                     id, msg, iterpool);

      if (err)
        break;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  svn_pool_destroy(iterpool);

  return svn_error_trace(
            svn_error_compose_create(err, svn_sqlite__reset(stmt)));
}

svn_error_t *
svn_wc__db_verify_db_full(svn_wc__db_t *db,
                          const char *wri_abspath,
                          svn_wc__db_verify_cb_t callback,
                          void *baton,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return svn_error_trace(
            svn_wc__db_verify_db_full_internal(wcroot, callback, baton,
                                               scratch_pool));
}

svn_error_t *
svn_wc__db_bump_format(int *result_format,
                       svn_boolean_t *bumped_format,
                       svn_wc__db_t *db,
                       const char *wcroot_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  svn_error_t *err;
  int format;

  if (bumped_format)
    *bumped_format = FALSE;

  /* Do not scan upwards for a working copy root here to prevent accidental
   * upgrades of any working copies the WCROOT might be nested in.
   * Just try to open a DB at the specified path instead. */
  err = svn_wc__db_util_open_db(&sdb, wcroot_abspath, SDB_FILE,
                                svn_sqlite__mode_readwrite,
                                TRUE, /* exclusive */
                                0, /* default timeout */
                                NULL, /* my statements */
                                scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_t *err2;
      apr_hash_t *entries;

      /* Could not open an sdb. Check for an entries file instead. */
      err2 = svn_wc__read_entries_old(&entries, wcroot_abspath,
                                      scratch_pool, scratch_pool);
      if (err2 || apr_hash_count(entries) == 0)
        return svn_error_createf(SVN_ERR_WC_INVALID_OP_ON_CWD,
                  svn_error_compose_create(err, err2),
                  _("Can't upgrade '%s' as it is not a working copy root"),
                  svn_dirent_local_style(wcroot_abspath, scratch_pool));

      /* An entries file was found. This is a pre-wc-ng working copy
       * so suggest an upgrade. */
      return svn_error_createf(SVN_ERR_WC_UPGRADE_REQUIRED, err,
                _("Working copy '%s' is too old and must be upgraded to "
                  "at least format %d, as created by Subversion %s"),
                svn_dirent_local_style(wcroot_abspath, scratch_pool),
                SVN_WC__WC_NG_VERSION,
                svn_wc__version_string_from_format(SVN_WC__WC_NG_VERSION));
    }

  SVN_ERR(svn_sqlite__read_schema_version(&format, sdb, scratch_pool));
  err = svn_wc__upgrade_sdb(result_format, wcroot_abspath,
                            sdb, format, scratch_pool);

  if (err == SVN_NO_ERROR && bumped_format)
    *bumped_format = (*result_format > format);

  /* Make sure we return a different error than expected for upgrades from
     entries */
  if (err && err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED)
    err = svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, err,
                           _("Working copy upgrade failed"));

  err = svn_error_compose_create(err, svn_sqlite__close(sdb));

  return svn_error_trace(err);
}

svn_error_t *
svn_wc__db_vacuum(svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, local_abspath,
                                                scratch_pool, scratch_pool));
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb, STMT_VACUUM));

  return SVN_NO_ERROR;
}

/* Item queued with svn_wc__db_commit_queue_add */
typedef struct commit_queue_item_t
{
  const char *local_relpath;
  svn_boolean_t recurse; /* Use legacy recursion */
  svn_boolean_t committed; /* Process the node as committed */
  svn_boolean_t remove_lock; /* Remove existing lock on node */
  svn_boolean_t remove_changelist; /* Remove changelist on node */

  /* The pristine text checksum. NULL if the old value should be kept
     and for directories */
  const svn_checksum_t *new_sha1_checksum;

  apr_hash_t *new_dav_cache; /* New DAV cache for the node */
} commit_queue_item_t;

/* The queue definition for vn_wc__db_create_commit_queue,
   svn_wc__db_commit_queue_add and finally svn_wc__db_process_commit_queue */
struct svn_wc__db_commit_queue_t
{
  svn_wc__db_wcroot_t *wcroot; /* Wcroot for ITEMS */
  apr_array_header_t *items; /* List of commit_queue_item_t* */
  svn_boolean_t have_recurse; /* Is one or more item[x]->recurse TRUE? */
};

/* Create a new svn_wc__db_commit_queue_t instance in RESULT_POOL for the
   working copy specified with WRI_ABSPATH */
svn_error_t *
svn_wc__db_create_commit_queue(svn_wc__db_commit_queue_t **queue,
                               svn_wc__db_t *db,
                               const char *wri_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc__db_commit_queue_t *q;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, result_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  q = apr_pcalloc(result_pool, sizeof(*q));

  SVN_ERR_ASSERT(wcroot->sdb);

  q->wcroot = wcroot;
  q->items = apr_array_make(result_pool, 64,
                            sizeof(commit_queue_item_t*));
  q->have_recurse = FALSE;

  *queue = q;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_commit_queue_add(svn_wc__db_commit_queue_t *queue,
                            const char *local_abspath,
                            svn_boolean_t recurse,
                            svn_boolean_t is_commited,
                            svn_boolean_t remove_lock,
                            svn_boolean_t remove_changelist,
                            const svn_checksum_t *new_sha1_checksum,
                            apr_hash_t *new_dav_cache,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  commit_queue_item_t *cqi;
  const char *local_relpath;

  local_relpath = svn_dirent_skip_ancestor(queue->wcroot->abspath,
                                           local_abspath);

  if (! local_relpath)
    return svn_error_createf(
                SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                _("The path '%s' is not in the working copy '%s'"),
                svn_dirent_local_style(local_abspath, scratch_pool),
                svn_dirent_local_style(queue->wcroot->abspath, scratch_pool));

  cqi = apr_pcalloc(result_pool, sizeof(*cqi));
  cqi->local_relpath = local_relpath;
  cqi->recurse = recurse;
  cqi->committed = is_commited;
  cqi->remove_lock = remove_lock;
  cqi->remove_changelist = remove_changelist;
  cqi->new_sha1_checksum = new_sha1_checksum;
  cqi->new_dav_cache = new_dav_cache;

  queue->have_recurse |= recurse;

  APR_ARRAY_PUSH(queue->items, commit_queue_item_t *) = cqi;
  return SVN_NO_ERROR;
}

/*** Finishing updates and commits. ***/

/* Post process an item that is committed in the repository. Collapse layers into
 * BASE. Queue work items that will finish a commit of the file or directory
 * LOCAL_ABSPATH in DB:
 */
static svn_error_t *
process_committed_leaf(svn_wc__db_t *db,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       svn_boolean_t via_recurse,
                       svn_wc__db_status_t status,
                       svn_node_kind_t kind,
                       svn_boolean_t prop_mods,
                       const svn_checksum_t *old_checksum,
                       svn_revnum_t new_revnum,
                       apr_time_t new_changed_date,
                       const char *new_changed_author,
                       apr_hash_t *new_dav_cache,
                       svn_boolean_t remove_lock,
                       svn_boolean_t remove_changelist,
                       const svn_checksum_t *checksum,
                       apr_pool_t *scratch_pool)
{
  svn_revnum_t new_changed_rev = new_revnum;
  svn_skel_t *work_item = NULL;

  {
    const char *lock_relpath;
    svn_boolean_t locked;

    if (kind == svn_node_dir)
      lock_relpath = local_relpath;
    else
      lock_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

    SVN_ERR(svn_wc__db_wclock_owns_lock_internal(&locked, wcroot,
                                                 lock_relpath, FALSE,
                                                 scratch_pool));

    if (!locked)
      return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                             _("No write-lock in '%s'"),
                             path_for_error_message(wcroot, local_relpath,
                                                    scratch_pool));

    SVN_ERR(flush_entries(wcroot, lock_relpath, svn_depth_empty,
                          scratch_pool));
  }

  if (status == svn_wc__db_status_not_present)
    {
      /* We are committing the leaf of a copy operation.
         We leave the not-present marker to allow pulling in excluded
         children of a copy.

         The next update will remove the not-present marker. */

      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(status == svn_wc__db_status_normal
                 || status == svn_wc__db_status_incomplete
                 || status == svn_wc__db_status_added
                 || status == svn_wc__db_status_deleted);

  if (kind != svn_node_dir
      && status != svn_wc__db_status_deleted)
    {
      /* If we sent a delta (meaning: post-copy modification),
         then this file will appear in the queue and so we should have
         its checksum already. */
      if (checksum == NULL)
        {
          /* It was copied and not modified. We must have a text
             base for it. And the node should have a checksum. */
          SVN_ERR_ASSERT(old_checksum != NULL);

          checksum = old_checksum;

          /* Is the node completely unmodified and are we recursing? */
          if (via_recurse && !prop_mods)
            {
              /* If a copied node itself is not modified, but the op_root of
                 the copy is committed we have to make sure that changed_rev,
                 changed_date and changed_author don't change or the working
                 copy used for committing will show different last modified
                 information then a clean checkout of exactly the same
                 revisions. (Issue #3676) */

              SVN_ERR(svn_wc__db_read_info_internal(
                                           NULL, NULL, NULL, NULL, NULL,
                                           &new_changed_rev,
                                           &new_changed_date,
                                           &new_changed_author, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL,
                                           NULL, NULL,
                                           wcroot, local_relpath,
                                           scratch_pool, scratch_pool));
            }
        }

      SVN_ERR(svn_wc__wq_build_file_commit(&work_item,
                                           db, svn_dirent_join(wcroot->abspath,
                                                               local_relpath,
                                                               scratch_pool),
                                           prop_mods,
                                           scratch_pool, scratch_pool));
    }

  /* The new text base will be found in the pristine store by its checksum. */
  SVN_ERR(commit_node(wcroot, local_relpath,
                      new_revnum, new_changed_rev,
                      new_changed_date, new_changed_author,
                      checksum,
                      new_dav_cache,
                      !remove_changelist,
                      !remove_lock,
                      work_item,
                      scratch_pool));

  return SVN_NO_ERROR;
}

/** Internal helper for svn_wc_process_committed_queue2().
 * Bump a commit item, collapsing local changes with the new repository
 * information to a new BASE node.
 *
 * @a new_date is the (server-side) date of the new revision, or 0.
 *
 * @a rev_author is the (server-side) author of the new
 * revision; it may be @c NULL.
 *
 * @a new_dav_cache is a hash of all the new dav properties for LOCAL_RELPATH.
 *
 * If @a remove_lock is set, release any user locks on @a
 * local_abspath; otherwise keep them during processing.
 *
 * If @a remove_changelist is set, clear any changeset assignments
 * from @a local_abspath; otherwise, keep such assignments.
 *
 * If @a new_sha1_checksum is non-NULL, use it to identify the node's pristine
 * text.
 *
 * Set TOP_OF_RECURSE to TRUE to show that this the top of a possibly
 * recursive commit operation. (Part of the legacy recurse handling)
 */
static svn_error_t *
process_committed_internal(svn_wc__db_t *db,
                           svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           svn_boolean_t recurse,
                           svn_boolean_t top_of_recurse,
                           svn_revnum_t new_revnum,
                           apr_time_t new_date,
                           const char *rev_author,
                           apr_hash_t *new_dav_cache,
                           svn_boolean_t remove_lock,
                           svn_boolean_t remove_changelist,
                           const svn_checksum_t *new_sha1_checksum,
                           apr_hash_t *items_by_relpath,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const svn_checksum_t *old_checksum;
  svn_boolean_t prop_mods;

  SVN_ERR(svn_wc__db_read_info_internal(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, &old_checksum, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, &prop_mods, NULL, NULL, NULL,
                                        wcroot, local_relpath,
                                        scratch_pool, scratch_pool));

  /* NOTE: be wary of making crazy semantic changes in this function, since
     svn_wc_process_committed4() calls this.  */

  SVN_ERR(process_committed_leaf(db, wcroot, local_relpath, !top_of_recurse,
                                 status, kind, prop_mods, old_checksum,
                                 new_revnum, new_date, rev_author,
                                 new_dav_cache,
                                 remove_lock, remove_changelist,
                                 new_sha1_checksum,
                                 scratch_pool));

  /* Only check for recursion on nodes that have children */
  if (kind != svn_node_dir
      || status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_excluded
      || status == svn_wc__db_status_server_excluded
      /* Node deleted -> then no longer a directory */
      || status == svn_wc__db_status_deleted)
    {
      return SVN_NO_ERROR;
    }

  if (recurse)
    {
      const apr_array_header_t *children;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      /* Read PATH's entries;  this is the absolute path. */
      SVN_ERR(gather_children(&children, wcroot, local_relpath,
                              STMT_SELECT_NODE_CHILDREN, -1,
                              scratch_pool, iterpool));

      /* Recursively loop over all children. */
      for (i = 0; i < children->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          const char *this_relpath;
          const commit_queue_item_t *cqi;

          svn_pool_clear(iterpool);

          this_relpath = svn_dirent_join(local_relpath, name, iterpool);

          new_sha1_checksum = NULL;
          cqi = svn_hash_gets(items_by_relpath, this_relpath);

          if (cqi != NULL)
            new_sha1_checksum = cqi->new_sha1_checksum;

          /* Recurse.  Pass NULL for NEW_DAV_CACHE, because the
             ones present in the current call are only applicable to
             this one committed item. */
          SVN_ERR(process_committed_internal(
                    db, wcroot, this_relpath,
                    TRUE /* recurse */,
                    FALSE /* top_of_recurse */,
                    new_revnum, new_date,
                    rev_author,
                    NULL /* new_dav_cache */,
                    FALSE /* remove_lock */,
                    remove_changelist,
                    new_sha1_checksum,
                    items_by_relpath,
                    iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}

/* Return TRUE if any item of QUEUE is a parent of ITEM and will be
   processed recursively, return FALSE otherwise.

   The algorithmic complexity of this search implementation is O(queue
   length), but it's quite quick.
*/
static svn_boolean_t
have_recursive_parent(const apr_array_header_t *all_items,
                      const commit_queue_item_t *item,
                      apr_pool_t *scratch_pool)
{
  const char *local_relpath = item->local_relpath;
  int i;

  for (i = 0; i < all_items->nelts; i++)
    {
      const commit_queue_item_t *qi
        = APR_ARRAY_IDX(all_items, i, const commit_queue_item_t *);

      if (qi == item)
        continue;

      if (qi->recurse && svn_relpath_skip_ancestor(qi->local_relpath,
                                                   local_relpath))
        {
          return TRUE;
        }
    }

  return FALSE;
}

/* Compare function for svn_sort__array */
static int
compare_queue_items(const void *v1,
                    const void *v2)
{
  const commit_queue_item_t *cqi1
              = *(const commit_queue_item_t **)v1;
  const commit_queue_item_t *cqi2
              = *(const commit_queue_item_t **)v2;

  return svn_path_compare_paths(cqi1->local_relpath, cqi2->local_relpath);
}

/* Internal, locked version of svn_wc__db_process_commit_queue */
static svn_error_t *
db_process_commit_queue(svn_wc__db_t *db,
                        svn_wc__db_commit_queue_t *queue,
                        svn_revnum_t new_revnum,
                        apr_time_t new_date,
                        const char *new_author,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *items_by_relpath = NULL;
  int j;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  svn_sort__array(queue->items, compare_queue_items);

  if (queue->have_recurse)
    {
      items_by_relpath = apr_hash_make(scratch_pool);

      for (j = 0; j < queue->items->nelts; j++)
        {
          commit_queue_item_t *cqi
            = APR_ARRAY_IDX(queue->items, j, commit_queue_item_t *);

          svn_hash_sets(items_by_relpath, cqi->local_relpath, cqi);
        }
    }

  for (j = 0; j < queue->items->nelts; j++)
    {
      commit_queue_item_t *cqi
        = APR_ARRAY_IDX(queue->items, j, commit_queue_item_t *);

      svn_pool_clear(iterpool);

      /* Skip this item if it is a child of a recursive item, because it has
         been (or will be) accounted for when that recursive item was (or
         will be) processed. */
      if (queue->have_recurse && have_recursive_parent(queue->items, cqi,
                                                       iterpool))
        continue;

      if (!cqi->committed)
        {
          if (cqi->remove_lock)
            {
              svn_skel_t *work_item;

              SVN_ERR(svn_wc__wq_build_sync_file_flags(
                                                    &work_item,
                                                    db,
                                                    svn_dirent_join(
                                                        queue->wcroot->abspath,
                                                        cqi->local_relpath,
                                                        iterpool),
                                                    iterpool, iterpool));

              lock_remove_txn(queue->wcroot, cqi->local_relpath, work_item,
                              iterpool);
            }
          if (cqi->remove_changelist)
            SVN_ERR(svn_wc__db_op_set_changelist(db,
                                                 svn_dirent_join(
                                                        queue->wcroot->abspath,
                                                        cqi->local_relpath,
                                                        iterpool),
                                                 NULL, NULL,
                                                 svn_depth_empty,
                                                 NULL, NULL, /* notify */
                                                 NULL, NULL, /* cancel */
                                                 iterpool));
        }
      else
        {
          SVN_ERR(process_committed_internal(
                                  db, queue->wcroot, cqi->local_relpath,
                                  cqi->recurse,
                                  TRUE /* top_of_recurse */,
                                  new_revnum, new_date, new_author,
                                  cqi->new_dav_cache,
                                  cqi->remove_lock,
                                  cqi->remove_changelist,
                                  cqi->new_sha1_checksum,
                                  items_by_relpath,
                                  iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_process_commit_queue(svn_wc__db_t *db,
                                svn_wc__db_commit_queue_t *queue,
                                svn_revnum_t new_revnum,
                                apr_time_t new_date,
                                const char *new_author,
                                apr_pool_t *scratch_pool)
{
  SVN_WC__DB_WITH_TXN(db_process_commit_queue(db, queue,
                                              new_revnum, new_date,
                                              new_author, scratch_pool),
                        queue->wcroot);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__find_repos_node_in_wc(apr_array_header_t **local_abspath_list,
                              svn_wc__db_t *db,
                              const char *wri_abspath,
                              const char *repos_relpath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *wri_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &wri_relpath, db,
                                                 wri_abspath, scratch_pool,
                                                 scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_FIND_REPOS_PATH_IN_WC));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, repos_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  *local_abspath_list = apr_array_make(result_pool, have_row ? 1 : 0,
                                       sizeof(const char*));
  while (have_row)
    {
      const char *local_relpath;
      const char *local_abspath;

      local_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      local_abspath = svn_dirent_join(wcroot->abspath, local_relpath,
                                      result_pool);
      APR_ARRAY_PUSH(*local_abspath_list, const char *) = local_abspath;

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
    
  return svn_error_trace(svn_sqlite__reset(stmt));
}

