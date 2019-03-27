/* dbt.c --- DBT-frobbing functions
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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_md5.h>
#include <apr_sha1.h>

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "../id.h"
#include "dbt.h"


DBT *
svn_fs_base__clear_dbt(DBT *dbt)
{
  memset(dbt, 0, sizeof(*dbt));

  return dbt;
}


DBT *svn_fs_base__nodata_dbt(DBT *dbt)
{
  svn_fs_base__clear_dbt(dbt);

  /* A `nodata' dbt is one which retrieves zero bytes from offset zero,
     and stores them in a zero-byte buffer in user-allocated memory.  */
  dbt->flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);
  dbt->doff = dbt->dlen = 0;

  return dbt;
}


DBT *
svn_fs_base__set_dbt(DBT *dbt, const void *data, apr_size_t size)
{
  svn_fs_base__clear_dbt(dbt);

  dbt->data = (void *) data;
  dbt->size = (u_int32_t) size;

  return dbt;
}


DBT *
svn_fs_base__result_dbt(DBT *dbt)
{
  svn_fs_base__clear_dbt(dbt);
  dbt->flags |= DB_DBT_MALLOC;

  return dbt;
}


/* An APR pool cleanup function that simply applies `free' to its
   argument.  */
static apr_status_t
apr_free_cleanup(void *arg)
{
  free(arg);

  return 0;
}


DBT *
svn_fs_base__track_dbt(DBT *dbt, apr_pool_t *pool)
{
  if (dbt->data)
    apr_pool_cleanup_register(pool, dbt->data, apr_free_cleanup,
                              apr_pool_cleanup_null);

  return dbt;
}


DBT *
svn_fs_base__recno_dbt(DBT *dbt, db_recno_t *recno)
{
  svn_fs_base__set_dbt(dbt, recno, sizeof(*recno));
  dbt->ulen = dbt->size;
  dbt->flags |= DB_DBT_USERMEM;

  return dbt;
}


int
svn_fs_base__compare_dbt(const DBT *a, const DBT *b)
{
  int common_size = a->size > b->size ? b->size : a->size;
  int cmp = memcmp(a->data, b->data, common_size);

  if (cmp)
    return cmp;
  else
    return a->size - b->size;
}



/* Building DBT's from interesting things.  */


/* Set DBT to the unparsed form of ID; allocate memory from POOL.
   Return DBT.  */
DBT *
svn_fs_base__id_to_dbt(DBT *dbt,
                       const svn_fs_id_t *id,
                       apr_pool_t *pool)
{
  svn_string_t *unparsed_id = svn_fs_base__id_unparse(id, pool);
  svn_fs_base__set_dbt(dbt, unparsed_id->data, unparsed_id->len);
  return dbt;
}


/* Set DBT to the unparsed form of SKEL; allocate memory from POOL.  */
DBT *
svn_fs_base__skel_to_dbt(DBT *dbt,
                         svn_skel_t *skel,
                         apr_pool_t *pool)
{
  svn_stringbuf_t *unparsed_skel = svn_skel__unparse(skel, pool);
  svn_fs_base__set_dbt(dbt, unparsed_skel->data, unparsed_skel->len);
  return dbt;
}


/* Set DBT to the text of the null-terminated string STR.  DBT will
   refer to STR's storage.  Return DBT.  */
DBT *
svn_fs_base__str_to_dbt(DBT *dbt, const char *str)
{
  svn_fs_base__set_dbt(dbt, str, strlen(str));
  return dbt;
}

DBT *
svn_fs_base__checksum_to_dbt(DBT *dbt, svn_checksum_t *checksum)
{
  svn_fs_base__set_dbt(dbt, checksum->digest, svn_checksum_size(checksum));

  return dbt;
}
