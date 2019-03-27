/* rep-sharing.c --- the rep-sharing cache for fsfs
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

#include "svn_pools.h"

#include "svn_private_config.h"

#include "cached_data.h"
#include "fs_fs.h"
#include "fs.h"
#include "rep-cache.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_path.h"

#include "private/svn_sqlite.h"

#include "rep-cache-db.h"

REP_CACHE_DB_SQL_DECLARE_STATEMENTS(statements);



/** Helper functions. **/
static APR_INLINE const char *
path_rep_cache_db(const char *fs_path,
                  apr_pool_t *result_pool)
{
  return svn_dirent_join(fs_path, REP_CACHE_DB_NAME, result_pool);
}


/** Library-private API's. **/

/* Body of svn_fs_fs__open_rep_cache().
   Implements svn_atomic__init_once().init_func.
 */
static svn_error_t *
open_rep_cache(void *baton,
               apr_pool_t *pool)
{
  svn_fs_t *fs = baton;
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__db_t *sdb;
  const char *db_path;
  int version;

  /* Open (or create) the sqlite database.  It will be automatically
     closed when fs->pool is destroyed. */
  db_path = path_rep_cache_db(fs->path, pool);
#ifndef WIN32
  {
    /* We want to extend the permissions that apply to the repository
       as a whole when creating a new rep cache and not simply default
       to umask. */
    svn_boolean_t exists;

    SVN_ERR(svn_fs_fs__exists_rep_cache(&exists, fs, pool));
    if (!exists)
      {
        const char *current = svn_fs_fs__path_current(fs, pool);
        svn_error_t *err = svn_io_file_create_empty(db_path, pool);

        if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
          /* A real error. */
          return svn_error_trace(err);
        else if (err)
          /* Some other thread/process created the file. */
          svn_error_clear(err);
        else
          /* We created the file. */
          SVN_ERR(svn_io_copy_perms(current, db_path, pool));
      }
  }
#endif
  SVN_ERR(svn_sqlite__open(&sdb, db_path,
                           svn_sqlite__mode_rwcreate, statements,
                           0, NULL, 0,
                           fs->pool, pool));

  SVN_SQLITE__ERR_CLOSE(svn_sqlite__read_schema_version(&version, sdb, pool),
                        sdb);
  /* If we have an uninitialized database, go ahead and create the schema. */
  if (version <= 0)
    {
      int stmt;

      if (ffd->format >= SVN_FS_FS__MIN_REP_CACHE_SCHEMA_V2_FORMAT)
        stmt = STMT_CREATE_SCHEMA_V2;
      else
        stmt = STMT_CREATE_SCHEMA_V1;

      SVN_SQLITE__ERR_CLOSE(svn_sqlite__exec_statements(sdb, stmt), sdb);
    }

  /* This is used as a flag that the database is available so don't
     set it earlier. */
  ffd->rep_cache_db = sdb;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_rep_cache(svn_fs_t *fs,
                          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err = svn_atomic__init_once(&ffd->rep_cache_db_opened,
                                           open_rep_cache, fs, pool);
  return svn_error_quick_wrapf(err,
                               _("Couldn't open rep-cache database '%s'"),
                               svn_dirent_local_style(
                                 path_rep_cache_db(fs->path, pool), pool));
}

svn_error_t *
svn_fs_fs__close_rep_cache(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->rep_cache_db)
    {
      SVN_ERR(svn_sqlite__close(ffd->rep_cache_db));
      ffd->rep_cache_db = NULL;
      ffd->rep_cache_db_opened = 0;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__exists_rep_cache(svn_boolean_t *exists,
                            svn_fs_t *fs, apr_pool_t *pool)
{
  svn_node_kind_t kind;

  SVN_ERR(svn_io_check_path(path_rep_cache_db(fs->path, pool),
                            &kind, pool));

  *exists = (kind != svn_node_none);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__walk_rep_reference(svn_fs_t *fs,
                              svn_revnum_t start,
                              svn_revnum_t end,
                              svn_error_t *(*walker)(representation_t *,
                                                     void *,
                                                     svn_fs_t *,
                                                     apr_pool_t *),
                              void *walker_baton,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int iterations = 0;

  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Don't check ffd->rep_sharing_allowed. */
  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT);

  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  /* Check global invariants. */
  if (start == 0)
    {
      svn_revnum_t max;

      SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db,
                                        STMT_GET_MAX_REV));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      max = svn_sqlite__column_revnum(stmt, 0);
      SVN_ERR(svn_sqlite__reset(stmt));
      if (SVN_IS_VALID_REVNUM(max))  /* The rep-cache could be empty. */
        SVN_ERR(svn_fs_fs__ensure_revision_exists(max, fs, iterpool));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db,
                                    STMT_GET_REPS_FOR_RANGE));
  SVN_ERR(svn_sqlite__bindf(stmt, "rr",
                            start, end));

  /* Walk the cache entries. */
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      representation_t *rep;
      const char *sha1_digest;
      svn_error_t *err;
      svn_checksum_t *checksum;

      /* Clear ITERPOOL occasionally. */
      if (iterations++ % 16 == 0)
        svn_pool_clear(iterpool);

      /* Check for cancellation. */
      if (cancel_func)
        {
          err = cancel_func(cancel_baton);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }

      /* Construct a representation_t. */
      rep = apr_pcalloc(iterpool, sizeof(*rep));
      svn_fs_fs__id_txn_reset(&rep->txn_id);
      sha1_digest = svn_sqlite__column_text(stmt, 0, iterpool);
      err = svn_checksum_parse_hex(&checksum, svn_checksum_sha1,
                                   sha1_digest, iterpool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      rep->has_sha1 = TRUE;
      memcpy(rep->sha1_digest, checksum->digest, sizeof(rep->sha1_digest));
      rep->revision = svn_sqlite__column_revnum(stmt, 1);
      rep->item_index = svn_sqlite__column_int64(stmt, 2);
      rep->size = svn_sqlite__column_int64(stmt, 3);
      rep->expanded_size = svn_sqlite__column_int64(stmt, 4);

      /* Walk. */
      err = walker(rep, walker_baton, fs, iterpool);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* This function's caller ignores most errors it returns.
   If you extend this function, check the callsite to see if you have
   to make it not-ignore additional error codes.  */
svn_error_t *
svn_fs_fs__get_rep_reference(representation_t **rep_p,
                             svn_fs_t *fs,
                             svn_checksum_t *checksum,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  representation_t *rep;

  SVN_ERR_ASSERT(ffd->rep_sharing_allowed);
  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  /* We only allow SHA1 checksums in this table. */
  if (checksum->kind != svn_checksum_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "rep_cache table.\n"));

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db, STMT_GET_REP));
  SVN_ERR(svn_sqlite__bindf(stmt, "s",
                            svn_checksum_to_cstring(checksum, pool)));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      rep = apr_pcalloc(pool, sizeof(*rep));
      svn_fs_fs__id_txn_reset(&(rep->txn_id));
      memcpy(rep->sha1_digest, checksum->digest, sizeof(rep->sha1_digest));
      rep->has_sha1 = TRUE;
      rep->revision = svn_sqlite__column_revnum(stmt, 0);
      rep->item_index = svn_sqlite__column_int64(stmt, 1);
      rep->size = svn_sqlite__column_int64(stmt, 2);
      rep->expanded_size = svn_sqlite__column_int64(stmt, 3);
    }
  else
    rep = NULL;

  SVN_ERR(svn_sqlite__reset(stmt));

  if (rep)
    {
      svn_error_t *err;

      SVN_ERR(svn_fs_fs__fixup_expanded_size(fs, rep, pool));

      /* Check that REP refers to a revision that exists in FS. */
      err = svn_fs_fs__ensure_revision_exists(rep->revision, fs, pool);
      if (err)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                                 "Checksum '%s' in rep-cache is beyond HEAD",
                                 svn_checksum_to_cstring_display(checksum,
                                                                 pool));
    }

  *rep_p = rep;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_rep_reference(svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;
  svn_checksum_t checksum;
  checksum.kind = svn_checksum_sha1;
  checksum.digest = rep->sha1_digest;

  SVN_ERR_ASSERT(ffd->rep_sharing_allowed);
  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  /* We only allow SHA1 checksums in this table. */
  if (! rep->has_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "rep_cache table.\n"));

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db, STMT_SET_REP));
  SVN_ERR(svn_sqlite__bindf(stmt, "siiii",
                            svn_checksum_to_cstring(&checksum, pool),
                            (apr_int64_t) rep->revision,
                            (apr_int64_t) rep->item_index,
                            (apr_int64_t) rep->size,
                            (apr_int64_t) rep->expanded_size));

  err = svn_sqlite__insert(NULL, stmt);
  if (err)
    {
      representation_t *old_rep;

      if (err->apr_err != SVN_ERR_SQLITE_CONSTRAINT)
        return svn_error_trace(err);

      svn_error_clear(err);

      /* Constraint failed so the mapping for SHA1_CHECKSUM->REP
         should exist.  If so that's cool -- just do nothing.  If not,
         that's a red flag!  */
      SVN_ERR(svn_fs_fs__get_rep_reference(&old_rep, fs, &checksum, pool));

      if (!old_rep)
        {
          /* Something really odd at this point, we failed to insert the
             checksum AND failed to read an existing checksum.  Do we need
             to flag this? */
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__del_rep_reference(svn_fs_t *fs,
                             svn_revnum_t youngest,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT);
  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, ffd->rep_cache_db,
                                    STMT_DEL_REPS_YOUNGER_THAN_REV));
  SVN_ERR(svn_sqlite__bindf(stmt, "r", youngest));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* Start a transaction to take an SQLite reserved lock that prevents
   other writes.

   See unlock_rep_cache(). */
static svn_error_t *
lock_rep_cache(svn_fs_t *fs,
               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (! ffd->rep_cache_db)
    SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

  SVN_ERR(svn_sqlite__exec_statements(ffd->rep_cache_db, STMT_LOCK_REP));

  return SVN_NO_ERROR;
}

/* End the transaction started by lock_rep_cache(). */
static svn_error_t *
unlock_rep_cache(svn_fs_t *fs,
                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR_ASSERT(ffd->rep_cache_db); /* was opened by lock_rep_cache() */

  SVN_ERR(svn_sqlite__exec_statements(ffd->rep_cache_db, STMT_UNLOCK_REP));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__with_rep_cache_lock(svn_fs_t *fs,
                               svn_error_t *(*body)(void *,
                                                    apr_pool_t *),
                               void *baton,
                               apr_pool_t *pool)
{
  svn_error_t *err;

  SVN_ERR(lock_rep_cache(fs, pool));
  err = body(baton, pool);
  return svn_error_compose_create(err, unlock_rep_cache(fs, pool));
}
