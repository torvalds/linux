/* trail.c : backing out of aborted Berkeley DB transactions
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

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include <apr_pools.h>
#include "svn_pools.h"
#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "bdb/bdb-err.h"
#include "bdb/bdb_compat.h"
#include "trail.h"
#include "../libsvn_fs/fs-loader.h"


#if defined(SVN_FS__TRAIL_DEBUG)

struct trail_debug_t
{
  struct trail_debug_t *prev;
  const char *table;
  const char *op;
};

void
svn_fs_base__trail_debug(trail_t *trail, const char *table, const char *op)
{
  struct trail_debug_t *trail_debug;

  trail_debug = apr_palloc(trail->pool, sizeof(*trail_debug));
  trail_debug->prev = trail->trail_debug;
  trail_debug->table = table;
  trail_debug->op = op;
  trail->trail_debug = trail_debug;
}

static void
print_trail_debug(trail_t *trail,
                  const char *txn_body_fn_name,
                  const char *filename, int line)
{
  struct trail_debug_t *trail_debug;

  fprintf(stderr, "(%s, %s, %u, %u): ",
          txn_body_fn_name, filename, line, trail->db_txn ? 1 : 0);

  trail_debug = trail->trail_debug;
  while (trail_debug)
    {
      fprintf(stderr, "(%s, %s) ", trail_debug->table, trail_debug->op);
      trail_debug = trail_debug->prev;
    }
  fprintf(stderr, "\n");
}
#else
#define print_trail_debug(trail, txn_body_fn_name, filename, line)
#endif /* defined(SVN_FS__TRAIL_DEBUG) */


static svn_error_t *
begin_trail(trail_t **trail_p,
            svn_fs_t *fs,
            svn_boolean_t use_txn,
            apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;
  trail_t *trail = apr_pcalloc(pool, sizeof(*trail));

  trail->pool = svn_pool_create(pool);
  trail->fs = fs;
  if (use_txn)
    {
      /* [*]
         If we're already inside a trail operation, abort() -- this is
         a coding problem (and will likely hang the repository anyway). */
      SVN_ERR_ASSERT(! bfd->in_txn_trail);

      SVN_ERR(BDB_WRAP(fs, N_("beginning Berkeley DB transaction"),
                       bfd->bdb->env->txn_begin(bfd->bdb->env, 0,
                                                &trail->db_txn, 0)));
      bfd->in_txn_trail = TRUE;
    }
  else
    {
      trail->db_txn = NULL;
    }

  *trail_p = trail;
  return SVN_NO_ERROR;
}


static svn_error_t *
abort_trail(trail_t *trail)
{
  svn_fs_t *fs = trail->fs;
  base_fs_data_t *bfd = fs->fsap_data;

  if (trail->db_txn)
    {
      /* [**]
         We have to reset the in_txn_trail flag *before* calling
         DB_TXN->abort().  If we did it the other way around, the next
         call to begin_trail() (e.g., as part of a txn retry) would
         cause an abort, even though there's strictly speaking no
         programming error involved (see comment [*] above).

         In any case, if aborting the txn fails, restarting it will
         most likely fail for the same reason, and so it's better to
         see the returned error than to abort.  An obvious example is
         when DB_TXN->abort() returns DB_RUNRECOVERY. */
      bfd->in_txn_trail = FALSE;
      SVN_ERR(BDB_WRAP(fs, N_("aborting Berkeley DB transaction"),
                       trail->db_txn->abort(trail->db_txn)));
    }
  svn_pool_destroy(trail->pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
commit_trail(trail_t *trail)
{
  int db_err;
  svn_fs_t *fs = trail->fs;
  base_fs_data_t *bfd = fs->fsap_data;

  /* According to the example in the Berkeley DB manual, txn_commit
     doesn't return DB_LOCK_DEADLOCK --- all deadlocks are reported
     earlier.  */
  if (trail->db_txn)
    {
      /* See comment [**] in abort_trail() above.
         An error during txn commit will abort the transaction anyway. */
      bfd->in_txn_trail = FALSE;
      SVN_ERR(BDB_WRAP(fs, N_("committing Berkeley DB transaction"),
                       trail->db_txn->commit(trail->db_txn, 0)));
    }

  /* Do a checkpoint here, if enough has gone on.
     The checkpoint parameters below are pretty arbitrary.  Perhaps
     there should be an svn_fs_berkeley_mumble function to set them.  */
  db_err = bfd->bdb->env->txn_checkpoint(bfd->bdb->env, 1024, 5, 0);

  /* Pre-4.1 Berkeley documentation says:

        The DB_ENV->txn_checkpoint function returns a non-zero error
        value on failure, 0 on success, and returns DB_INCOMPLETE if
        there were pages that needed to be written to complete the
        checkpoint but that DB_ENV->memp_sync was unable to write
        immediately.

     It's safe to ignore DB_INCOMPLETE if we get it while
     checkpointing.  (Post-4.1 Berkeley doesn't have DB_INCOMPLETE
     anymore, so it's not an issue there.)  */
  if (db_err)
    {
#if SVN_BDB_HAS_DB_INCOMPLETE
      if (db_err != DB_INCOMPLETE)
#endif /* SVN_BDB_HAS_DB_INCOMPLETE */
        {
          return svn_fs_bdb__wrap_db
            (fs, "checkpointing after Berkeley DB transaction", db_err);
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
do_retry(svn_fs_t *fs,
         svn_error_t *(*txn_body)(void *baton, trail_t *trail),
         void *baton,
         svn_boolean_t use_txn,
         svn_boolean_t destroy_trail_pool,
         apr_pool_t *pool,
         const char *txn_body_fn_name,
         const char *filename,
         int line)
{
  for (;;)
    {
      trail_t *trail;
      svn_error_t *svn_err, *err;
      svn_boolean_t deadlocked = FALSE;

      SVN_ERR(begin_trail(&trail, fs, use_txn, pool));

      /* Do the body of the transaction.  */
      svn_err = (*txn_body)(baton, trail);

      if (! svn_err)
        {
          /* The transaction succeeded!  Commit it.  */
          SVN_ERR(commit_trail(trail));

          if (use_txn)
            print_trail_debug(trail, txn_body_fn_name, filename, line);

          /* If our caller doesn't want us to keep trail memory
             around, destroy our subpool. */
          if (destroy_trail_pool)
            svn_pool_destroy(trail->pool);

          return SVN_NO_ERROR;
        }

      /* Search for a deadlock error on the stack. */
      for (err = svn_err; err; err = err->child)
        if (err->apr_err == SVN_ERR_FS_BERKELEY_DB_DEADLOCK)
          deadlocked = TRUE;

      /* Is this a real error, or do we just need to retry?  */
      if (! deadlocked)
        {
          /* Ignore any error returns.  The first error is more valuable.  */
          svn_error_clear(abort_trail(trail));
          return svn_err;
        }

      svn_error_clear(svn_err);

      /* We deadlocked.  Abort the transaction, and try again.  */
      SVN_ERR(abort_trail(trail));
    }
}


svn_error_t *
svn_fs_base__retry_debug(svn_fs_t *fs,
                         svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                         void *baton,
                         svn_boolean_t destroy_trail_pool,
                         apr_pool_t *pool,
                         const char *txn_body_fn_name,
                         const char *filename,
                         int line)
{
  return do_retry(fs, txn_body, baton, TRUE, destroy_trail_pool, pool,
                  txn_body_fn_name, filename, line);
}


#if defined(SVN_FS__TRAIL_DEBUG)
#undef svn_fs_base__retry_txn
#endif

svn_error_t *
svn_fs_base__retry_txn(svn_fs_t *fs,
                       svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                       void *baton,
                       svn_boolean_t destroy_trail_pool,
                       apr_pool_t *pool)
{
  return do_retry(fs, txn_body, baton, TRUE, destroy_trail_pool, pool,
                  "unknown", "", 0);
}


svn_error_t *
svn_fs_base__retry(svn_fs_t *fs,
                   svn_error_t *(*txn_body)(void *baton, trail_t *trail),
                   void *baton,
                   svn_boolean_t destroy_trail_pool,
                   apr_pool_t *pool)
{
  return do_retry(fs, txn_body, baton, FALSE, destroy_trail_pool, pool,
                  NULL, NULL, 0);
}
