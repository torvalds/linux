/* strings-table.c : operations on the `strings' table
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
#include "svn_pools.h"
#include "../fs.h"
#include "../err.h"
#include "dbt.h"
#include "../trail.h"
#include "../key-gen.h"
#include "../../libsvn_fs/fs-loader.h"
#include "bdb-err.h"
#include "strings-table.h"

#include "svn_private_config.h"


/*** Creating and opening the strings table. ***/

int
svn_fs_bdb__open_strings_table(DB **strings_p,
                               DB_ENV *env,
                               svn_boolean_t create)
{
  const u_int32_t open_flags = (create ? (DB_CREATE | DB_EXCL) : 0);
  DB *strings;

  BDB_ERR(svn_fs_bdb__check_version());
  BDB_ERR(db_create(&strings, env, 0));

  /* Enable duplicate keys. This allows the data to be spread out across
     multiple records. Note: this must occur before ->open().  */
  BDB_ERR(strings->set_flags(strings, DB_DUP));

  BDB_ERR((strings->open)(SVN_BDB_OPEN_PARAMS(strings, NULL),
                          "strings", 0, DB_BTREE,
                          open_flags, 0666));

  if (create)
    {
      DBT key, value;

      /* Create the `next-key' table entry.  */
      BDB_ERR(strings->put
              (strings, 0,
               svn_fs_base__str_to_dbt(&key, NEXT_KEY_KEY),
               svn_fs_base__str_to_dbt(&value, "0"), 0));
    }

  *strings_p = strings;
  return 0;
}



/*** Storing and retrieving strings.  ***/

/* Allocate *CURSOR and advance it to first row in the set of rows
   whose key is defined by QUERY.  Set *LENGTH to the size of that
   first row.  */
static svn_error_t *
locate_key(apr_size_t *length,
           DBC **cursor,
           DBT *query,
           svn_fs_t *fs,
           trail_t *trail,
           apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  int db_err;
  DBT result;

  svn_fs_base__trail_debug(trail, "strings", "cursor");
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading a string"),
                   bfd->strings->cursor(bfd->strings, trail->db_txn,
                                        cursor, 0)));

  /* Set up the DBT for reading the length of the record. */
  svn_fs_base__clear_dbt(&result);
  result.ulen = 0;
  result.flags |= DB_DBT_USERMEM;

  /* Advance the cursor to the key that we're looking for. */
  db_err = svn_bdb_dbc_get(*cursor, query, &result, DB_SET);

  /* We don't need to svn_fs_base__track_dbt() the result, because nothing
     was allocated in it. */

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    {
      svn_bdb_dbc_close(*cursor);
      return svn_error_createf
        (SVN_ERR_FS_NO_SUCH_STRING, 0,
         "No such string '%s'", (const char *)query->data);
    }
  if (db_err)
    {
      DBT rerun;

      if (db_err != SVN_BDB_DB_BUFFER_SMALL)
        {
          svn_bdb_dbc_close(*cursor);
          return BDB_WRAP(fs, N_("moving cursor"), db_err);
        }

      /* We got an SVN_BDB_DB_BUFFER_SMALL (typical since we have a
         zero length buf), so we need to re-run the operation to make
         it happen. */
      svn_fs_base__clear_dbt(&rerun);
      rerun.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;
      db_err = svn_bdb_dbc_get(*cursor, query, &rerun, DB_SET);
      if (db_err)
        {
          svn_bdb_dbc_close(*cursor);
          return BDB_WRAP(fs, N_("rerunning cursor move"), db_err);
        }
    }

  /* ### this cast might not be safe? */
  *length = (apr_size_t) result.size;

  return SVN_NO_ERROR;
}


/* Advance CURSOR by a single row in the set of rows whose keys match
   CURSOR's current location.  Set *LENGTH to the size of that next
   row.  If any error occurs, CURSOR will be destroyed.  */
static int
get_next_length(apr_size_t *length, DBC *cursor, DBT *query)
{
  DBT result;
  int db_err;

  /* Set up the DBT for reading the length of the record. */
  svn_fs_base__clear_dbt(&result);
  result.ulen = 0;
  result.flags |= DB_DBT_USERMEM;

  /* Note: this may change the QUERY DBT, but that's okay: we're going
     to be sticking with the same key anyways.  */
  db_err = svn_bdb_dbc_get(cursor, query, &result, DB_NEXT_DUP);

  /* Note that we exit on DB_NOTFOUND. The caller uses that to end a loop. */
  if (db_err)
    {
      DBT rerun;

      if (db_err != SVN_BDB_DB_BUFFER_SMALL)
        {
          svn_bdb_dbc_close(cursor);
          return db_err;
        }

      /* We got an SVN_BDB_DB_BUFFER_SMALL (typical since we have a
         zero length buf), so we need to re-run the operation to make
         it happen. */
      svn_fs_base__clear_dbt(&rerun);
      rerun.flags |= DB_DBT_USERMEM | DB_DBT_PARTIAL;
      db_err = svn_bdb_dbc_get(cursor, query, &rerun, DB_NEXT_DUP);
      if (db_err)
        svn_bdb_dbc_close(cursor);
    }

  /* ### this cast might not be safe? */
  *length = (apr_size_t) result.size;
  return db_err;
}


svn_error_t *
svn_fs_bdb__string_read(svn_fs_t *fs,
                        const char *key,
                        char *buf,
                        svn_filesize_t offset,
                        apr_size_t *len,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  int db_err;
  DBT query, result;
  DBC *cursor;
  apr_size_t length, bytes_read = 0;

  svn_fs_base__str_to_dbt(&query, key);

  SVN_ERR(locate_key(&length, &cursor, &query, fs, trail, pool));

  /* Seek through the records for this key, trying to find the record that
     includes OFFSET. Note that we don't require reading from more than
     one record since we're allowed to return partial reads.  */
  while (length <= offset)
    {
      offset -= length;

      /* Remember, if any error happens, our cursor has been closed
         for us. */
      db_err = get_next_length(&length, cursor, &query);

      /* No more records? They tried to read past the end. */
      if (db_err == DB_NOTFOUND)
        {
          *len = 0;
          return SVN_NO_ERROR;
        }
      if (db_err)
        return BDB_WRAP(fs, N_("reading string"), db_err);
    }

  /* The current record contains OFFSET. Fetch the contents now. Note that
     OFFSET has been moved to be relative to this record. The length could
     quite easily extend past this record, so we use DB_DBT_PARTIAL and
     read successive records until we've filled the request.  */
  while (1)
    {
      svn_fs_base__clear_dbt(&result);
      result.data = buf + bytes_read;
      result.ulen = (u_int32_t)(*len - bytes_read);
      result.doff = (u_int32_t)offset;
      result.dlen = result.ulen;
      result.flags |= (DB_DBT_USERMEM | DB_DBT_PARTIAL);
      db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_CURRENT);
      if (db_err)
        {
          svn_bdb_dbc_close(cursor);
          return BDB_WRAP(fs, N_("reading string"), db_err);
        }

      bytes_read += result.size;
      if (bytes_read == *len)
        {
          /* Done with the cursor. */
          SVN_ERR(BDB_WRAP(fs, N_("closing string-reading cursor"),
                           svn_bdb_dbc_close(cursor)));
          break;
        }

      /* Remember, if any error happens, our cursor has been closed
         for us. */
      db_err = get_next_length(&length, cursor, &query);
      if (db_err == DB_NOTFOUND)
        break;
      if (db_err)
        return BDB_WRAP(fs, N_("reading string"), db_err);

      /* We'll be reading from the beginning of the next record */
      offset = 0;
    }

  *len = bytes_read;
  return SVN_NO_ERROR;
}


/* Get the current 'next-key' value and bump the record. */
static svn_error_t *
get_key_and_bump(svn_fs_t *fs,
                 const char **key,
                 trail_t *trail,
                 apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBC *cursor;
  char next_key[MAX_KEY_SIZE];
  apr_size_t key_len;
  int db_err;
  DBT query;
  DBT result;

  /* ### todo: see issue #409 for why bumping the key as part of this
     trail is problematic. */

  /* Open a cursor and move it to the 'next-key' value. We can then fetch
     the contents and use the cursor to overwrite those contents. Since
     this database allows duplicates, we can't do an arbitrary 'put' to
     write the new value -- that would append, not overwrite.  */

  svn_fs_base__trail_debug(trail, "strings", "cursor");
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading a string"),
                   bfd->strings->cursor(bfd->strings, trail->db_txn,
                                        &cursor, 0)));

  /* Advance the cursor to 'next-key' and read it. */

  db_err = svn_bdb_dbc_get(cursor,
                           svn_fs_base__str_to_dbt(&query, NEXT_KEY_KEY),
                           svn_fs_base__result_dbt(&result),
                           DB_SET);
  if (db_err)
    {
      svn_bdb_dbc_close(cursor);
      return BDB_WRAP(fs, N_("getting next-key value"), db_err);
    }

  svn_fs_base__track_dbt(&result, pool);
  *key = apr_pstrmemdup(pool, result.data, result.size);

  /* Bump to future key. */
  key_len = result.size;
  svn_fs_base__next_key(result.data, &key_len, next_key);

  /* Shove the new key back into the database, at the cursor position. */
  db_err = svn_bdb_dbc_put(cursor, &query,
                           svn_fs_base__str_to_dbt(&result, next_key),
                           DB_CURRENT);
  if (db_err)
    {
      svn_bdb_dbc_close(cursor); /* ignore the error, the original is
                                    more important. */
      return BDB_WRAP(fs, N_("bumping next string key"), db_err);
    }

  return BDB_WRAP(fs, N_("closing string-reading cursor"),
                  svn_bdb_dbc_close(cursor));
}

svn_error_t *
svn_fs_bdb__string_append(svn_fs_t *fs,
                          const char **key,
                          apr_size_t len,
                          const char *buf,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query, result;

  /* If the passed-in key is NULL, we graciously generate a new string
     using the value of the `next-key' record in the strings table. */
  if (*key == NULL)
    {
      SVN_ERR(get_key_and_bump(fs, key, trail, pool));
    }

  /* Store a new record into the database. */
  svn_fs_base__trail_debug(trail, "strings", "put");
  return BDB_WRAP(fs, N_("appending string"),
                  bfd->strings->put
                  (bfd->strings, trail->db_txn,
                   svn_fs_base__str_to_dbt(&query, *key),
                   svn_fs_base__set_dbt(&result, buf, len),
                   0));
}


svn_error_t *
svn_fs_bdb__string_clear(svn_fs_t *fs,
                         const char *key,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  int db_err;
  DBT query, result;

  svn_fs_base__str_to_dbt(&query, key);

  /* Torch the prior contents */
  svn_fs_base__trail_debug(trail, "strings", "del");
  db_err = bfd->strings->del(bfd->strings, trail->db_txn, &query, 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0,
       "No such string '%s'", key);

  /* Handle any other error conditions.  */
  SVN_ERR(BDB_WRAP(fs, N_("clearing string"), db_err));

  /* Shove empty data back in for this key. */
  svn_fs_base__clear_dbt(&result);
  result.data = 0;
  result.size = 0;
  result.flags |= DB_DBT_USERMEM;

  svn_fs_base__trail_debug(trail, "strings", "put");
  return BDB_WRAP(fs, N_("storing empty contents"),
                  bfd->strings->put(bfd->strings, trail->db_txn,
                                    &query, &result, 0));
}


svn_error_t *
svn_fs_bdb__string_size(svn_filesize_t *size,
                        svn_fs_t *fs,
                        const char *key,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  int db_err;
  DBT query;
  DBC *cursor;
  apr_size_t length;
  svn_filesize_t total;

  svn_fs_base__str_to_dbt(&query, key);

  SVN_ERR(locate_key(&length, &cursor, &query, fs, trail, pool));

  total = length;
  while (1)
    {
      /* Remember, if any error happens, our cursor has been closed
         for us. */
      db_err = get_next_length(&length, cursor, &query);

      /* No more records? Then return the total length. */
      if (db_err == DB_NOTFOUND)
        {
          *size = total;
          return SVN_NO_ERROR;
        }
      if (db_err)
        return BDB_WRAP(fs, N_("fetching string length"), db_err);

      total += length;
    }

  /* NOTREACHED */
}


svn_error_t *
svn_fs_bdb__string_delete(svn_fs_t *fs,
                          const char *key,
                          trail_t *trail,
                          apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  int db_err;
  DBT query;

  svn_fs_base__trail_debug(trail, "strings", "del");
  db_err = bfd->strings->del(bfd->strings, trail->db_txn,
                             svn_fs_base__str_to_dbt(&query, key), 0);

  /* If there's no such node, return an appropriately specific error.  */
  if (db_err == DB_NOTFOUND)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_STRING, 0,
       "No such string '%s'", key);

  /* Handle any other error conditions.  */
  return BDB_WRAP(fs, N_("deleting string"), db_err);
}


svn_error_t *
svn_fs_bdb__string_copy(svn_fs_t *fs,
                        const char **new_key,
                        const char *key,
                        trail_t *trail,
                        apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  DBT query;
  DBT result;
  DBT copykey;
  DBC *cursor;
  int db_err;

  /* Copy off the old key in case the caller is sharing storage
     between the old and new keys. */
  const char *old_key = apr_pstrdup(pool, key);

  SVN_ERR(get_key_and_bump(fs, new_key, trail, pool));

  svn_fs_base__trail_debug(trail, "strings", "cursor");
  SVN_ERR(BDB_WRAP(fs, N_("creating cursor for reading a string"),
                   bfd->strings->cursor(bfd->strings, trail->db_txn,
                                        &cursor, 0)));

  svn_fs_base__str_to_dbt(&query, old_key);
  svn_fs_base__str_to_dbt(&copykey, *new_key);

  svn_fs_base__clear_dbt(&result);

  /* Move to the first record and fetch its data (under BDB's mem mgmt). */
  db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_SET);
  if (db_err)
    {
      svn_bdb_dbc_close(cursor);
      return BDB_WRAP(fs, N_("getting next-key value"), db_err);
    }

  while (1)
    {
      /* ### can we pass a BDB-provided buffer to another BDB function?
         ### they are supposed to have a duration up to certain points
         ### of calling back into BDB, but I'm not sure what the exact
         ### rules are. it is definitely nicer to use BDB buffers here
         ### to simplify things and reduce copies, but... hrm.
      */

      /* Write the data to the database */
      svn_fs_base__trail_debug(trail, "strings", "put");
      db_err = bfd->strings->put(bfd->strings, trail->db_txn,
                                 &copykey, &result, 0);
      if (db_err)
        {
          svn_bdb_dbc_close(cursor);
          return BDB_WRAP(fs, N_("writing copied data"), db_err);
        }

      /* Read the next chunk. Terminate loop if we're done. */
      svn_fs_base__clear_dbt(&result);
      db_err = svn_bdb_dbc_get(cursor, &query, &result, DB_NEXT_DUP);
      if (db_err == DB_NOTFOUND)
        break;
      if (db_err)
        {
          svn_bdb_dbc_close(cursor);
          return BDB_WRAP(fs, N_("fetching string data for a copy"), db_err);
        }
    }

  return BDB_WRAP(fs, N_("closing string-reading cursor"),
                  svn_bdb_dbc_close(cursor));
}
