/* checksum-reps-table.c : operations on the `checksum-reps' table
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

#include <apr_strings.h>

#include "bdb_compat.h"
#include "../fs.h"
#include "../err.h"
#include "../key-gen.h"
#include "dbt.h"
#include "../trail.h"
#include "bdb-err.h"
#include "../../libsvn_fs/fs-loader.h"
#include "checksum-reps-table.h"

#include "svn_private_config.h"


int svn_fs_bdb__open_checksum_reps_table(DB **checksum_reps_p,
                                         DB_ENV *env,
                                         svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *checksum_reps;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&checksum_reps, env, 0));
  error = (checksum_reps->open)(SVN_BDB_OPEN_PARAMS(checksum_reps, NULL),
                                "checksum-reps", 0, DB_BTREE,
                                open_flags, 0666);

  /* Create the checksum-reps table if it doesn't exist. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(checksum_reps->close(checksum_reps, 0));
      return svn_fs_bdb__open_checksum_reps_table(checksum_reps_p, env, TRUE);
    }

  /* Create the initial `next-key' table entry.  */
  if (create)
    {
      DBT key, value;
      BDB_ERR(checksum_reps->put(checksum_reps, 0,
                                 svn_fs_base__str_to_dbt(&key, NEXT_KEY_KEY),
                                 svn_fs_base__str_to_dbt(&value, "0"), 0));
    }

  BDB_ERR(error);

  *checksum_reps_p = checksum_reps;
  return 0;
}

svn_error_t *svn_fs_bdb__get_checksum_rep(const char **rep_key,
                                          svn_fs_t *fs,
                                          svn_checksum_t *checksum,
                                          trail_t *trail,
                                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;

  /* We only allow SHA1 checksums in this table. */
  if (checksum->kind != svn_checksum_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "checksum-reps table.\n"));

  svn_fs_base__trail_debug(trail, "checksum-reps", "get");
  db_err = bfd->checksum_reps->get(bfd->checksum_reps, trail->db_txn,
                                   svn_fs_base__checksum_to_dbt(&key, checksum),
                                   svn_fs_base__result_dbt(&value), 0);
  svn_fs_base__track_dbt(&value, pool);

  if (db_err == DB_NOTFOUND)
    return svn_fs_base__err_no_such_checksum_rep(fs, checksum);

  *rep_key = apr_pstrmemdup(pool, value.data, value.size);
  return SVN_NO_ERROR;
}

svn_error_t *svn_fs_bdb__set_checksum_rep(svn_fs_t *fs,
                                          svn_checksum_t *checksum,
                                          const char *rep_key,
                                          trail_t *trail,
                                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;

  /* We only allow SHA1 checksums in this table. */
  if (checksum->kind != svn_checksum_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "checksum-reps table.\n"));

  /* Create a key from our CHECKSUM. */
  svn_fs_base__checksum_to_dbt(&key, checksum);

  /* Check to see if we already have a mapping for CHECKSUM.  If so,
     and the value is the same one we were about to write, that's
     cool -- just do nothing.  If, however, the value is *different*,
     that's a red flag!  */
  svn_fs_base__trail_debug(trail, "checksum-reps", "get");
  db_err = bfd->checksum_reps->get(bfd->checksum_reps, trail->db_txn,
                                   &key, svn_fs_base__result_dbt(&value), 0);
  svn_fs_base__track_dbt(&value, pool);
  if (db_err != DB_NOTFOUND)
    {
      const char *sum_str = svn_checksum_to_cstring_display(checksum, pool);
      return svn_error_createf
        (SVN_ERR_FS_ALREADY_EXISTS, NULL,
         _("Representation key for checksum '%s' exists in filesystem '%s'."),
         sum_str, fs->path);
    }

  /* Create a value from our REP_KEY, and add this record to the table. */
  svn_fs_base__str_to_dbt(&value, rep_key);
  svn_fs_base__trail_debug(trail, "checksum-reps", "put");
  SVN_ERR(BDB_WRAP(fs, N_("storing checksum-reps record"),
                   bfd->checksum_reps->put(bfd->checksum_reps, trail->db_txn,
                                           &key, &value, 0)));
  return SVN_NO_ERROR;
}

svn_error_t *svn_fs_bdb__delete_checksum_rep(svn_fs_t *fs,
                                             svn_checksum_t *checksum,
                                             trail_t *trail,
                                             apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key;

  /* We only allow SHA1 checksums in this table. */
  if (checksum->kind != svn_checksum_sha1)
    return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,
                            _("Only SHA1 checksums can be used as keys in the "
                              "checksum-reps table.\n"));

  svn_fs_base__checksum_to_dbt(&key, checksum);
  svn_fs_base__trail_debug(trail, "checksum-reps", "del");
  SVN_ERR(BDB_WRAP(fs, N_("deleting entry from 'checksum-reps' table"),
                   bfd->checksum_reps->del(bfd->checksum_reps,
                                           trail->db_txn, &key, 0)));
  return SVN_NO_ERROR;
}

svn_error_t *svn_fs_bdb__reserve_rep_reuse_id(const char **id_p,
                                              svn_fs_t *fs,
                                              trail_t *trail,
                                              apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;
  apr_size_t len;
  char next_key[MAX_KEY_SIZE];
  int db_err;

  svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY);

  /* Get the current value associated with the `next-key' key in the
     `checksum-reps' table.  */
  svn_fs_base__trail_debug(trail, "checksum-reps", "get");
  SVN_ERR(BDB_WRAP(fs, N_("allocating new representation reuse ID "
                         "(getting 'next-key')"),
                   bfd->checksum_reps->get(bfd->checksum_reps, trail->db_txn,
                                           &query,
                                           svn_fs_base__result_dbt(&result),
                                           0)));
  svn_fs_base__track_dbt(&result, pool);

  /* Set our return value. */
  *id_p = apr_pstrmemdup(pool, result.data, result.size);

  /* Bump to future key. */
  len = result.size;
  svn_fs_base__next_key(result.data, &len, next_key);
  svn_fs_base__trail_debug(trail, "checksum_reps", "put");
  db_err = bfd->checksum_reps->put(bfd->checksum_reps, trail->db_txn,
                                   svn_fs_base__str_to_dbt(&query,
                                                           NEXT_KEY_KEY),
                                   svn_fs_base__str_to_dbt(&result, next_key),
                                   0);

  return BDB_WRAP(fs, N_("bumping next representation reuse ID"), db_err);
}
