/* miscellaneous-table.c : operations on the `miscellaneous' table
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
#include "dbt.h"
#include "../err.h"
#include "../fs.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "miscellaneous-table.h"

#include "private/svn_fs_util.h"


int
svn_fs_bdb__open_miscellaneous_table(DB **miscellaneous_p,
                                     DB_ENV *env,
                                     svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *miscellaneous;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&miscellaneous, env, 0));
  error = (miscellaneous->open)(SVN_BDB_OPEN_PARAMS(miscellaneous, NULL),
                                "miscellaneous", 0, DB_BTREE,
                                open_flags, 0666);

  /* Create the table if it doesn't yet exist.  This is a form of
     automagical repository upgrading. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(miscellaneous->close(miscellaneous, 0));
      return svn_fs_bdb__open_miscellaneous_table(miscellaneous_p, env, TRUE);
    }
  BDB_ERR(error);

  /* If we're creating the table from scratch (not upgrading), record the
     upgrade rev as 0. */
  if (create)
    {
      DBT key, value;

      BDB_ERR(miscellaneous->put
              (miscellaneous, 0,
               svn_fs_base__str_to_dbt
               (&key, SVN_FS_BASE__MISC_FORWARD_DELTA_UPGRADE),
               svn_fs_base__str_to_dbt(&value, "0"), 0));
    }

  *miscellaneous_p = miscellaneous;
  return 0;
}


svn_error_t *
svn_fs_bdb__miscellaneous_set(svn_fs_t *fs,
                              const char *key_str,
                              const char *val,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;

  svn_fs_base__str_to_dbt(&key, key_str);
  if (val == NULL)
    {
      svn_fs_base__trail_debug(trail, "miscellaneous", "del");
      return BDB_WRAP(fs, N_("deleting record from 'miscellaneous' table"),
                      bfd->miscellaneous->del(bfd->miscellaneous,
                                              trail->db_txn, &key, 0));
    }
  else
    {
      svn_fs_base__str_to_dbt(&value, val);
      svn_fs_base__trail_debug(trail, "miscellaneous", "add");
      return BDB_WRAP(fs, N_("storing miscellaneous record"),
                      bfd->miscellaneous->put(bfd->miscellaneous,
                                              trail->db_txn,
                                              &key, &value, 0));
    }
}


svn_error_t *
svn_fs_bdb__miscellaneous_get(const char **val,
                              svn_fs_t *fs,
                              const char *key_str,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT key, value;
  int db_err;

  *val = NULL;
  svn_fs_base__trail_debug(trail, "miscellaneous", "get");
  db_err = bfd->miscellaneous->get(bfd->miscellaneous, trail->db_txn,
                                   svn_fs_base__str_to_dbt(&key, key_str),
                                   svn_fs_base__result_dbt(&value), 0);
  svn_fs_base__track_dbt(&value, pool);

  if (db_err != DB_NOTFOUND)
    {
      SVN_ERR(BDB_WRAP(fs, N_("fetching miscellaneous record"), db_err));
      *val = apr_pstrmemdup(pool, value.data, value.size);
    }
  return SVN_NO_ERROR;
}
