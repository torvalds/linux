/* reps-table.c : operations on the `representations' table
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
#include "svn_fs.h"
#include "../fs.h"
#include "../util/fs_skels.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "../key-gen.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "reps-table.h"
#include "strings-table.h"


#include "svn_private_config.h"


/*** Creating and opening the representations table. ***/

int
svn_fs_bdb__open_reps_table(DB **reps_p,
                            DB_ENV *env,
                            svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *reps;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&reps, env, 0));
  BDB_ERR((reps->open)(SVN_BDB_OPEN_PARAMS(reps, NULL),
                       "representations", 0, DB_BTREE,
                       open_flags, 0666));

  /* Create the `next-key' table entry.  */
  if (create)
  {
    DBT key, value;

    BDB_ERR(reps->put
            (reps, 0,
             svn_fs_base__str_to_dbt(&key, NEXT_KEY_KEY),
             svn_fs_base__str_to_dbt(&value, "0"), 0));
  }

  *reps_p = reps;
  return 0;
}



/*** Storing and retrieving reps.  ***/

svn_error_t *
svn_fs_bdb__read_rep(representation_t **rep_p,
                     svn_fs_t *fs,
                     const char *key,
                     trail_t *trail,
                     apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  svn_skel_t *skel;
  int db_err;
  DBT query, result;

  svn_fs_base__trail_debug(trail, "representations", "get");
  db_err = bfd->representations->get(bfd->representations,
                                     trail->db_txn,
                                     svn_fs_base__str_to_dbt(&query, key),
                                     svn_fs_base__result_dbt(&result), 0);
  svn_fs_base__track_dbt(&result, pool);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0,
       _("No such representation '%s'"), key);

  /* Handle any other error conditions.  */
  SVN_ERR(BDB_WRAP(fs, N_("reading representation"), db_err));

  /* Parse the REPRESENTATION skel.  */
  skel = svn_skel__parse(result.data, result.size, pool);

  /* Convert to a native type.  */
  return svn_fs_base__parse_representation_skel(rep_p, skel, pool);
}


svn_error_t *
svn_fs_bdb__write_rep(svn_fs_t *fs,
                      const char *key,
                      const representation_t *rep,
                      trail_t *trail,
                      apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;
  svn_skel_t *skel;

  /* Convert from native type to skel. */
  SVN_ERR(svn_fs_base__unparse_representation_skel(&skel, rep,
                                                   bfd->format, pool));

  /* Now write the record. */
  svn_fs_base__trail_debug(trail, "representations", "put");
  return BDB_WRAP(fs, N_("storing representation"),
                  bfd->representations->put
                  (bfd->representations, trail->db_txn,
                   svn_fs_base__str_to_dbt(&query, key),
                   svn_fs_base__skel_to_dbt(&result, skel, pool),
                   0));
}


svn_error_t *
svn_fs_bdb__write_new_rep(const char **key,
                          svn_fs_t *fs,
                          const representation_t *rep,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;
  int db_err;
  apr_size_t len;
  char next_key[MAX_KEY_SIZE];

  /* ### todo: see issue #409 for why bumping the key as part of this
     trail is problematic. */

  /* Get the current value associated with `next-key'.  */
  svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY);
  svn_fs_base__trail_debug(trail, "representations", "get");
  SVN_ERR(BDB_WRAP(fs, N_("allocating new representation (getting next-key)"),
                   bfd->representations->get
                   (bfd->representations, trail->db_txn, &query,
                    svn_fs_base__result_dbt(&result), 0)));

  svn_fs_base__track_dbt(&result, pool);

  /* Store the new rep. */
  *key = apr_pstrmemdup(pool, result.data, result.size);
  SVN_ERR(svn_fs_bdb__write_rep(fs, *key, rep, trail, pool));

  /* Bump to future key. */
  len = result.size;
  svn_fs_base__next_key(result.data, &len, next_key);
  svn_fs_base__trail_debug(trail, "representations", "put");
  db_err = bfd->representations->put
    (bfd->representations, trail->db_txn,
     svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY),
     svn_fs_base__str_to_dbt(&result, next_key),
     0);

  return BDB_WRAP(fs, N_("bumping next representation key"), db_err);
}


svn_error_t *
svn_fs_bdb__delete_rep(svn_fs_t *fs,
                       const char *key,
                       trail_t *trail,
                       apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  int db_err;
  DBT query;

  svn_fs_base__trail_debug(trail, "representations", "del");
  db_err = bfd->representations->del
    (bfd->representations, trail->db_txn,
     svn_fs_base__str_to_dbt(&query, key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REPRESENTATION, 0,
       _("No such representation '%s'"), key);

  /* Handle any other error conditions.  */
  return BDB_WRAP(fs, N_("deleting representation"), db_err);
}
