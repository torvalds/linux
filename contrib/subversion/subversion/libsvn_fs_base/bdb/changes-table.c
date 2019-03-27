/* changes-table.c : operations on the `changes' table
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

#include "bdb_compat.h"

#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_hash.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_path.h"
#include "../fs.h"
#include "../err.h"
#include "../trail.h"
#include "../id.h"
#include "../util/fs_skels.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "dbt.h"
#include "changes-table.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "svn_private_config.h"


/*** Creating and opening the changes table. ***/

int
svn_fs_bdb__open_changes_table(DB **changes_p,
                               DB_ENV *env,
                               svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *changes;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&changes, env, 0));

  /* Enable duplicate keys. This allows us to store the changes
     one-per-row.  Note: this must occur before ->open().  */
  BDB_ERR(changes->set_flags(changes, DB_DUP));

  BDB_ERR((changes->open)(SVN_BDB_OPEN_PARAMS(changes, NULL),
                          "changes", 0, DB_BTREE,
                          open_flags, 0666));

  *changes_p = changes;
  return 0;
}



/*** Storing and retrieving changes.  ***/

svn_error_t *
svn_fs_bdb__changes_add(svn_fs_t *fs,
                        const char *key,
                        change_t *change,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, value;
  svn_skel_t *skel;

  /* Convert native type to skel. */
  SVN_ERR(svn_fs_base__unparse_change_skel(&skel, change, pool));

  /* Store a new record into the database. */
  svn_fs_base__str_to_dbt(&query, key);
  svn_fs_base__skel_to_dbt(&value, skel, pool);
  svn_fs_base__trail_debug(trail, "changes", "put");
  return BDB_WRAP(fs, N_("creating change"),
                  bfd->changes->put(bfd->changes, trail->db_txn,
                                    &query, &value, 0));
}


svn_error_t *
svn_fs_bdb__changes_delete(svn_fs_t *fs,
                           const char *key,
                           trail_t *trail,
                           apr_pool_t *pool)
{
  int db_err;
  DBT query;
  base_fs_data_t *bfd = fs->fsap_data;

  svn_fs_base__trail_debug(trail, "changes", "del");
  db_err = bfd->changes->del(bfd->changes, trail->db_txn,
                             svn_fs_base__str_to_dbt(&query, key), 0);

  /* If there're no changes for KEY, that is acceptable.  Any other
     error should be propagated to the caller, though.  */
  if ((db_err) && (db_err != DB_NOTFOUND))
    {
      SVN_ERR(BDB_WRAP(fs, N_("deleting changes"), db_err));
    }

  return SVN_NO_ERROR;
}

/* Return a deep FS API type copy of SOURCE in internal format and allocate
 * the result in RESULT_POOL.
 */
static svn_fs_path_change2_t *
change_to_fs_change(const change_t *change,
                    apr_pool_t *result_pool)
{
  svn_fs_path_change2_t *result = svn_fs__path_change_create_internal(
                                    svn_fs_base__id_copy(change->noderev_id,
                                                         result_pool),
                                    change->kind,
                                    result_pool);
  result->text_mod = change->text_mod;
  result->prop_mod = change->prop_mod;
  result->node_kind = svn_node_unknown;
  result->copyfrom_known = FALSE;

  return result;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change2_t CHANGES, collapsing multiple changes into a
   single succinct change per path. */
static svn_error_t *
fold_change(apr_hash_t *changes,
            apr_hash_t *deletions,
            const change_t *change)
{
  apr_pool_t *pool = apr_hash_pool_get(changes);
  svn_fs_path_change2_t *old_change, *new_change;
  const char *path;

  if ((old_change = svn_hash_gets(changes, change->path)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Since the path already exists in the hash, we don't have to
         dup the allocation for the path itself. */
      path = change->path;

      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! change->noderev_id) && (change->kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Missing required node revision ID"));

      /* Sanity check:  we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (change->noderev_id
          && (! svn_fs_base__id_eq(old_change->node_rev_id,
                                   change->noderev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: new node revision ID without delete"));

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->kind == svn_fs_path_change_replace)
                 || (change->kind == svn_fs_path_change_reset)
                 || (change->kind == svn_fs_path_change_add))))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: non-add change on deleted path"));

      /* Sanity check: an add can't follow anything except
         a delete or reset.  */
      if ((change->kind == svn_fs_path_change_add)
          && (old_change->change_kind != svn_fs_path_change_delete)
          && (old_change->change_kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: add change on preexisting path"));

      /* Now, merge that change in. */
      switch (change->kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          new_change = NULL;
          break;

        case svn_fs_path_change_delete:
          if (old_change->change_kind == svn_fs_path_change_add)
            {
              /* If the path was introduced in this transaction via an
                 add, and we are deleting it, just remove the path
                 altogether. */
              new_change = NULL;
            }
          else if (old_change->change_kind == svn_fs_path_change_replace)
            {
              /* A deleting a 'replace' restore the original deletion. */
              new_change = svn_hash_gets(deletions, path);
              SVN_ERR_ASSERT(new_change);
            }
          else
            {
              /* A deletion overrules all previous changes. */
              new_change = old_change;
              new_change->change_kind = svn_fs_path_change_delete;
              new_change->text_mod = change->text_mod;
              new_change->prop_mod = change->prop_mod;
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace. */

          new_change = change_to_fs_change(change, pool);
          new_change->change_kind = svn_fs_path_change_replace;

          /* Remember the original deletion.
           * Make sure to allocate the hash key in a durable pool. */
          svn_hash_sets(deletions,
                        apr_pstrdup(apr_hash_pool_get(deletions), path),
                        old_change);
          break;

        case svn_fs_path_change_modify:
        default:
          new_change = old_change;
          if (change->text_mod)
            new_change->text_mod = TRUE;
          if (change->prop_mod)
            new_change->prop_mod = TRUE;
          break;
        }
    }
  else
    {
      /* This change is new to the hash, so make a new public change
         structure from the internal one (in the hash's pool), and dup
         the path into the hash's pool, too. */
      new_change = change_to_fs_change(change, pool);
      path = apr_pstrdup(pool, change->path);
    }

  /* Add (or update) this path. */
  svn_hash_sets(changes, path, new_change);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__changes_fetch(apr_hash_t **changes_p,
                          svn_fs_t *fs,
                          const char *key,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT query, result;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;
  apr_hash_t *changes = apr_hash_make(pool);
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_hash_t *deletions = apr_hash_make(subpool);

  /* Get a cursor on the first record matching KEY, and then loop over
     the records, adding them to the return array. */
  svn_fs_base__trail_debug(trail, "changes", "cursor");
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading changes"),
                   bfd->changes->cursor(bfd->changes, trail->db_txn,
                                        &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs_base__str_to_dbt(&query, key);
  svn_fs_base__result_dbt(&result);
  db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_SET);
  if (! db_err)
    svn_fs_base__track_dbt(&result, pool);

  while (! db_err)
    {
      change_t *change;
      svn_skel_t *result_skel;

      /* Clear the per-iteration subpool. */
      svn_pool_clear(iterpool);

      /* RESULT now contains a change record associated with KEY.  We
         need to parse that skel into an change_t structure ...  */
      result_skel = svn_skel__parse(result.data, result.size, iterpool);
      if (! result_skel)
        {
          err = svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                  _("Error reading changes for key '%s'"),
                                  key);
          goto cleanup;
        }
      err = svn_fs_base__parse_change_skel(&change, result_skel, iterpool);
      if (err)
        goto cleanup;

      /* ... and merge it with our return hash.  */
      err = fold_change(changes, deletions, change);
      if (err)
        goto cleanup;

      /* Now, if our change was a deletion or replacement, we have to
         blow away any changes thus far on paths that are (or, were)
         children of this path.
         ### i won't bother with another iteration pool here -- at
             most we talking about a few extra dups of paths into what
             is already a temporary subpool.
      */
      if ((change->kind == svn_fs_path_change_delete)
          || (change->kind == svn_fs_path_change_replace))
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(iterpool, changes);
               hi;
               hi = apr_hash_next(hi))
            {
              /* KEY is the path. */
              const void *hashkey;
              apr_ssize_t klen;
              const char *child_relpath;

              apr_hash_this(hi, &hashkey, &klen, NULL);

              /* If we come across our own path, ignore it.
                 If we come across a child of our path, remove it. */
              child_relpath = svn_fspath__skip_ancestor(change->path, hashkey);
              if (child_relpath && *child_relpath)
                apr_hash_set(changes, hashkey, klen, NULL);
            }
        }

      /* Advance the cursor to the next record with this same KEY, and
         fetch that record. */
      svn_fs_base__result_dbt(&result);
      db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_NEXT_DUP);
      if (! db_err)
        svn_fs_base__track_dbt(&result, pool);
    }

  /* Destroy the per-iteration subpool. */
  svn_pool_destroy(iterpool);
  svn_pool_destroy(subpool);

  /* If there are no (more) change records for this KEY, we're
     finished.  Just return the (possibly empty) array.  Any other
     error, however, needs to get handled appropriately.  */
  if (db_err && (db_err != DB_NOTFOUND))
    err = BDB_WRAP(fs, N_("fetching changes"), db_err);

 cleanup:
  /* Close the cursor. */
  db_c_err = svn_bdb_dbc_close(cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return svn_error_trace(err);

  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR(BDB_WRAP(fs, N_("closing changes cursor"), db_c_err));

  /* Finally, set our return variable and get outta here. */
  *changes_p = changes;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_bdb__changes_fetch_raw(apr_array_header_t **changes_p,
                              svn_fs_t *fs,
                              const char *key,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT query, result;
  int db_err = 0, db_c_err = 0;
  svn_error_t *err = SVN_NO_ERROR;
  change_t *change;
  apr_array_header_t *changes = apr_array_make(pool, 4, sizeof(change));

  /* Get a cursor on the first record matching KEY, and then loop over
     the records, adding them to the return array. */
  svn_fs_base__trail_debug(trail, "changes", "cursor");
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading changes"),
                   bfd->changes->cursor(bfd->changes, trail->db_txn,
                                        &cursor, 0)));

  /* Advance the cursor to the key that we're looking for. */
  svn_fs_base__str_to_dbt(&query, key);
  svn_fs_base__result_dbt(&result);
  db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_SET);
  if (! db_err)
    svn_fs_base__track_dbt(&result, pool);

  while (! db_err)
    {
      svn_skel_t *result_skel;

      /* RESULT now contains a change record associated with KEY.  We
         need to parse that skel into an change_t structure ...  */
      result_skel = svn_skel__parse(result.data, result.size, pool);
      if (! result_skel)
        {
          err = svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                  _("Error reading changes for key '%s'"),
                                  key);
          goto cleanup;
        }
      err = svn_fs_base__parse_change_skel(&change, result_skel, pool);
      if (err)
        goto cleanup;

      /* ... and add it to our return array.  */
      APR_ARRAY_PUSH(changes, change_t *) = change;

      /* Advance the cursor to the next record with this same KEY, and
         fetch that record. */
      svn_fs_base__result_dbt(&result);
      db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_NEXT_DUP);
      if (! db_err)
        svn_fs_base__track_dbt(&result, pool);
    }

  /* If there are no (more) change records for this KEY, we're
     finished.  Just return the (possibly empty) array.  Any other
     error, however, needs to get handled appropriately.  */
  if (db_err && (db_err != DB_NOTFOUND))
    err = BDB_WRAP(fs, N_("fetching changes"), db_err);

 cleanup:
  /* Close the cursor. */
  db_c_err = svn_bdb_dbc_close(cursor);

  /* If we had an error prior to closing the cursor, return the error. */
  if (err)
    return svn_error_trace(err);

  /* If our only error thus far was when we closed the cursor, return
     that error. */
  if (db_c_err)
    SVN_ERR(BDB_WRAP(fs, N_("closing changes cursor"), db_c_err));

  /* Finally, set our return variable and get outta here. */
  *changes_p = changes;
  return SVN_NO_ERROR;
}
