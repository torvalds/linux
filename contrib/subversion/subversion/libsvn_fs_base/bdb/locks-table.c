/* locks-table.c : operations on the `locks' table
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

#include "bdb_compat.h"

#include "svn_pools.h"
#include "svn_path.h"
#include "private/svn_skel.h"

#include "dbt.h"
#include "../err.h"
#include "../fs.h"
#include "../util/fs_skels.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "locks-table.h"
#include "lock-tokens-table.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"


int
svn_fs_bdb__open_locks_table(DB **locks_p,
                             DB_ENV *env,
                             svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *locks;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&locks, env, 0));
  error = (locks->open)(SVN_BDB_OPEN_PARAMS(locks, NULL),
                        "locks", 0, DB_BTREE,
                        open_flags, 0666);

  /* Create the table if it doesn't yet exist.  This is a form of
     automagical repository upgrading. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(locks->close(locks, 0));
      return svn_fs_bdb__open_locks_table(locks_p, env, TRUE);
    }
  BDB_ERR(error);

  *locks_p = locks;
  return 0;
}



svn_error_t *
svn_fs_bdb__lock_add(svn_fs_t *fs,
                     const char *lock_token,
                     svn_lock_t *lock,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  svn_skel_t *lock_skel;
  DBT key, value;

  /* Convert native type to skel. */
  SVN_ERR(svn_fs_base__unparse_lock_skel(&lock_skel, lock, pool));

  svn_fs_base__str_to_dbt(&key, lock_token);
  svn_fs_base__skel_to_dbt(&value, lock_skel, pool);
  svn_fs_base__trail_debug(trail, "lock", "add");
  return BDB_WRAP(fs, N_("storing lock record"),
                  bfd->locks->put(bfd->locks, trail->db_txn,
                                  &key, &value, 0));
}



svn_error_t *
svn_fs_bdb__lock_delete(svn_fs_t *fs,
                        const char *lock_token,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key;
  int db_err;

  svn_fs_base__str_to_dbt(&key, lock_token);
  svn_fs_base__trail_debug(trail, "locks", "del");
  db_err = bfd->locks->del(bfd->locks, trail->db_txn, &key, 0);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_bad_lock_token(fs, lock_token);
  return BDB_WRAP(fs, N_("deleting lock from 'locks' table"), db_err);
}



svn_error_t *
svn_fs_bdb__lock_get(svn_lock_t **lock_p,
                     svn_fs_t *fs,
                     const char *lock_token,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;
  svn_skel_t *skel;
  svn_lock_t *lock;

  svn_fs_base__trail_debug(trail, "lock", "get");
  db_err = bfd->locks->get(bfd->locks, trail->db_txn,
                           svn_fs_base__str_to_dbt(&key, lock_token),
                           svn_fs_base__result_dbt(&value),
                           0);
  svn_fs_base__track_dbt(&value, pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_bad_lock_token(fs, lock_token);
  SVN_ERR(BDB_WRAP(fs, N_("reading lock"), db_err));

  /* Parse TRANSACTION skel */
  skel = svn_skel__parse(value.data, value.size, pool);
  if (! skel)
    return svn_fs_base__err_corrupt_lock(fs, lock_token);

  /* Convert skel to native type. */
  SVN_ERR(svn_fs_base__parse_lock_skel(&lock, skel, pool));

  /* Possibly auto-expire the lock. */
  if (lock->expiration_date && (apr_time_now() > lock->expiration_date))
    {
      SVN_ERR(svn_fs_bdb__lock_delete(fs, lock_token, trail, pool));
      return SVN_FS__ERR_LOCK_EXPIRED(fs, lock_token);
    }

  *lock_p = lock;
  return SVN_NO_ERROR;
}


static svn_error_t *
get_lock(svn_lock_t **lock_p,
         svn_fs_t *fs,
         const char *path,
         const char *lock_token,
         trail_t *trail,
         apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  *lock_p = NULL;

  /* Make sure the token points to an existing, non-expired lock, by
     doing a lookup in the `locks' table.  Use 'pool'. */
  err = svn_fs_bdb__lock_get(lock_p, fs, lock_token, trail, pool);
  if (err && ((err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
              || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)))
    {
      svn_error_clear(err);

      /* If `locks' doesn't have the lock, then we should lose it
         from `lock-tokens' table as well, then skip to the next
         matching path-key. */
      err = svn_fs_bdb__lock_token_delete(fs, path, trail, pool);
    }
  return svn_error_trace(err);
}


svn_error_t *
svn_fs_bdb__locks_get(svn_fs_t *fs,
                      const char *path,
                      svn_depth_t depth,
                      svn_fs_get_locks_callback_t get_locks_func,
                      void *get_locks_baton,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  DBT key, value;
  int db_err, db_c_err;
  apr_pool_t *subpool = svn_pool_create(pool);
  const char *lock_token;
  svn_lock_t *lock;
  svn_error_t *err;
  const char *lookup_path = path;
  apr_size_t lookup_len;

  /* First, try to lookup PATH itself. */
  err = svn_fs_bdb__lock_token_get(&lock_token, fs, path, trail, pool);
  if (err && ((err->apr_err == SVN_ERR_FS_LOCK_EXPIRED)
              || (err->apr_err == SVN_ERR_FS_BAD_LOCK_TOKEN)
              || (err->apr_err == SVN_ERR_FS_NO_SUCH_LOCK)))
    {
      svn_error_clear(err);
    }
  else if (err)
    {
      return svn_error_trace(err);
    }
  else
    {
      SVN_ERR(get_lock(&lock, fs, path, lock_token, trail, pool));
      if (lock && get_locks_func)
        {
          SVN_ERR(get_locks_func(get_locks_baton, lock, pool));

          /* Found a lock so PATH is a file and we can ignore depth */
          return SVN_NO_ERROR;
        }
    }

  /* If we're only looking at PATH itself (depth = empty), stop here. */
  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Now go hunt for possible children of PATH. */

  svn_fs_base__trail_debug(trail, "lock-tokens", "cursor");
  db_err = bfd->lock_tokens->cursor(bfd->lock_tokens, trail->db_txn,
                                    &cursor, 0);
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading lock tokens"),
                   db_err));

  /* Since the key is going to be returned as well as the value make
     sure BDB malloc's the returned key.  */
  svn_fs_base__str_to_dbt(&key, lookup_path);
  key.flags |= DB_DBT_MALLOC;

  /* Get the first matching key that is either equal or greater than
     the one passed in, by passing in the DB_RANGE_SET flag.  */
  db_err = svn_bdb_dbc_get(cursor, &key, svn_fs_base__result_dbt(&value),
                           DB_SET_RANGE);

  if (!svn_fspath__is_root(path, strlen(path)))
    lookup_path = apr_pstrcat(pool, path, "/", SVN_VA_NULL);
  lookup_len = strlen(lookup_path);

  /* As long as the prefix of the returned KEY matches LOOKUP_PATH we
     know it is either LOOKUP_PATH or a decendant thereof.  */
  while ((! db_err)
         && lookup_len < key.size
         && strncmp(lookup_path, key.data, lookup_len) == 0)
    {
      const char *child_path;

      svn_pool_clear(subpool);

      svn_fs_base__track_dbt(&key, subpool);
      svn_fs_base__track_dbt(&value, subpool);

      /* Create a usable path and token in temporary memory. */
      child_path = apr_pstrmemdup(subpool, key.data, key.size);
      lock_token = apr_pstrmemdup(subpool, value.data, value.size);

      if ((depth == svn_depth_files) || (depth == svn_depth_immediates))
        {
          /* On the assumption that we only store locks for files,
             depth=files and depth=immediates should boil down to the
             same set of results.  So just see if CHILD_PATH is an
             immediate child of PATH.  If not, we don't care about
             this item.   */
          const char *rel_path = svn_fspath__skip_ancestor(path, child_path);
          if (!rel_path || (svn_path_component_count(rel_path) != 1))
            goto loop_it;
        }

      /* Get the lock for CHILD_PATH.  */
      err = get_lock(&lock, fs, child_path, lock_token, trail, subpool);
      if (err)
        {
          svn_bdb_dbc_close(cursor);
          return svn_error_trace(err);
        }

      /* Lock is verified, hand it off to our callback. */
      if (lock && get_locks_func)
        {
          err = get_locks_func(get_locks_baton, lock, subpool);
          if (err)
            {
              svn_bdb_dbc_close(cursor);
              return svn_error_trace(err);
            }
        }

    loop_it:
      svn_fs_base__result_dbt(&key);
      svn_fs_base__result_dbt(&value);
      db_err = svn_bdb_dbc_get(cursor, &key, &value, DB_NEXT);
    }

  svn_pool_destroy(subpool);
  db_c_err = svn_bdb_dbc_close(cursor);

  if (db_err && (db_err != DB_NOTFOUND))
    SVN_ERR(BDB_WRAP(fs, N_("fetching lock tokens"), db_err));
  if (db_c_err)
    SVN_ERR(BDB_WRAP(fs, N_("fetching lock tokens (closing cursor)"),
                     db_c_err));

  return SVN_NO_ERROR;
}

