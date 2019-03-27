/* uuids-table.c : operations on the `uuids' table
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

#include <apr_uuid.h>

#include "bdb_compat.h"
#include "svn_fs.h"
#include "../fs.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "uuids-table.h"

#include "svn_private_config.h"


/*** Creating and opening the uuids table.
     When the table is created, the repository's uuid is
     generated and stored as record #1. ***/

int
svn_fs_bdb__open_uuids_table(DB **uuids_p,
                             DB_ENV *env,
                             svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *uuids;
  int error;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&uuids, env, 0));
  BDB_ERR(uuids->set_re_len(uuids, APR_UUID_FORMATTED_LENGTH));

  error = (uuids->open)(SVN_BDB_OPEN_PARAMS(uuids, NULL),
                        "uuids", 0, DB_RECNO,
                        open_flags, 0666);

  /* This is a temporary compatibility check; it creates the
     UUIDs table if one does not already exist. */
  if (error == ENOENT && (! create))
    {
      BDB_ERR(uuids->close(uuids, 0));
      return svn_fs_bdb__open_uuids_table(uuids_p, env, TRUE);
    }

  BDB_ERR(error);

  if (create)
    {
      char buffer[APR_UUID_FORMATTED_LENGTH + 1];
      DBT key, value;
      apr_uuid_t uuid;
      int recno = 0;

      svn_fs_base__clear_dbt(&key);
      key.data = &recno;
      key.size = sizeof(recno);
      key.ulen = key.size;
      key.flags |= DB_DBT_USERMEM;

      svn_fs_base__clear_dbt(&value);
      value.data = buffer;
      value.size = sizeof(buffer) - 1;

      apr_uuid_get(&uuid);
      apr_uuid_format(buffer, &uuid);

      BDB_ERR(uuids->put(uuids, 0, &key, &value, DB_APPEND));
    }

  *uuids_p = uuids;
  return 0;
}

svn_error_t *svn_fs_bdb__get_uuid(svn_fs_t *fs,
                                  int idx,
                                  const char **uuid,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  char buffer[APR_UUID_FORMATTED_LENGTH + 1];
  DB *uuids = bfd->uuids;
  DBT key;
  DBT value;

  svn_fs_base__clear_dbt(&key);
  key.data = &idx;
  key.size = sizeof(idx);

  svn_fs_base__clear_dbt(&value);
  value.data = buffer;
  value.size = sizeof(buffer) - 1;
  value.ulen = value.size;
  value.flags |= DB_DBT_USERMEM;

  svn_fs_base__trail_debug(trail, "uuids", "get");
  SVN_ERR(BDB_WRAP(fs, N_("get repository uuid"),
                   uuids->get(uuids, trail->db_txn, &key, &value, 0)));

  *uuid = apr_pstrmemdup(pool, value.data, value.size);

  return SVN_NO_ERROR;
}

svn_error_t *svn_fs_bdb__set_uuid(svn_fs_t *fs,
                                  int idx,
                                  const char *uuid,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DB *uuids = bfd->uuids;
  DBT key;
  DBT value;

  svn_fs_base__clear_dbt(&key);
  key.data = &idx;
  key.size = sizeof(idx);

  svn_fs_base__clear_dbt(&value);
  value.size = (u_int32_t) strlen(uuid);
  value.data = apr_pstrmemdup(pool, uuid, value.size + 1);

  svn_fs_base__trail_debug(trail, "uuids", "put");
  return BDB_WRAP(fs, N_("set repository uuid"),
                  uuids->put(uuids, trail->db_txn, &key, &value, 0));
}
