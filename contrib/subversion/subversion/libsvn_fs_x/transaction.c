/* transaction.c --- transaction-related functions of FSX
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

#include "transaction.h"

#include <assert.h>
#include <apr_sha1.h>

#include "svn_error_codes.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"

#include "fs_x.h"
#include "tree.h"
#include "util.h"
#include "id.h"
#include "low_level.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "lock.h"
#include "rep-cache.h"
#include "index.h"
#include "batch_fsync.h"
#include "revprops.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_io_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* The vtable associated with an open transaction object. */
static txn_vtable_t txn_vtable = {
  svn_fs_x__commit_txn,
  svn_fs_x__abort_txn,
  svn_fs_x__txn_prop,
  svn_fs_x__txn_proplist,
  svn_fs_x__change_txn_prop,
  svn_fs_x__txn_root,
  svn_fs_x__change_txn_props
};

/* FSX-specific data being attached to svn_fs_txn_t.
 */
typedef struct fs_txn_data_t
{
  /* Strongly typed representation of the TXN's ID member. */
  svn_fs_x__txn_id_t txn_id;
} fs_txn_data_t;

svn_fs_x__txn_id_t
svn_fs_x__txn_get_id(svn_fs_txn_t *txn)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  return ftd->txn_id;
}

/* Functions for working with shared transaction data. */

/* Return the transaction object for transaction TXN_ID from the
   transaction list of filesystem FS (which must already be locked via the
   txn_list_lock mutex).  If the transaction does not exist in the list,
   then create a new transaction object and return it (if CREATE_NEW is
   true) or return NULL (otherwise). */
static svn_fs_x__shared_txn_data_t *
get_shared_txn(svn_fs_t *fs,
               svn_fs_x__txn_id_t txn_id,
               svn_boolean_t create_new)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__shared_data_t *ffsd = ffd->shared;
  svn_fs_x__shared_txn_data_t *txn;

  for (txn = ffsd->txns; txn; txn = txn->next)
    if (txn->txn_id == txn_id)
      break;

  if (txn || !create_new)
    return txn;

  /* Use the transaction object from the (single-object) freelist,
     if one is available, or otherwise create a new object. */
  if (ffsd->free_txn)
    {
      txn = ffsd->free_txn;
      ffsd->free_txn = NULL;
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create(ffsd->common_pool);
      txn = apr_palloc(subpool, sizeof(*txn));
      txn->pool = subpool;
    }

  txn->txn_id = txn_id;
  txn->being_written = FALSE;

  /* Link this transaction into the head of the list.  We will typically
     be dealing with only one active transaction at a time, so it makes
     sense for searches through the transaction list to look at the
     newest transactions first.  */
  txn->next = ffsd->txns;
  ffsd->txns = txn;

  return txn;
}

/* Free the transaction object for transaction TXN_ID, and remove it
   from the transaction list of filesystem FS (which must already be
   locked via the txn_list_lock mutex).  Do nothing if the transaction
   does not exist. */
static void
free_shared_txn(svn_fs_t *fs, svn_fs_x__txn_id_t txn_id)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__shared_data_t *ffsd = ffd->shared;
  svn_fs_x__shared_txn_data_t *txn, *prev = NULL;

  for (txn = ffsd->txns; txn; prev = txn, txn = txn->next)
    if (txn->txn_id == txn_id)
      break;

  if (!txn)
    return;

  if (prev)
    prev->next = txn->next;
  else
    ffsd->txns = txn->next;

  /* As we typically will be dealing with one transaction after another,
     we will maintain a single-object free list so that we can hopefully
     keep reusing the same transaction object. */
  if (!ffsd->free_txn)
    ffsd->free_txn = txn;
  else
    svn_pool_destroy(txn->pool);
}


/* Obtain a lock on the transaction list of filesystem FS, call BODY
   with FS, BATON, and POOL, and then unlock the transaction list.
   Return what BODY returned. */
static svn_error_t *
with_txnlist_lock(svn_fs_t *fs,
                  svn_error_t *(*body)(svn_fs_t *fs,
                                       const void *baton,
                                       apr_pool_t *pool),
                  const void *baton,
                  apr_pool_t *pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__shared_data_t *ffsd = ffd->shared;

  SVN_MUTEX__WITH_LOCK(ffsd->txn_list_lock,
                       body(fs, baton, pool));

  return SVN_NO_ERROR;
}


/* Get a lock on empty file LOCK_FILENAME, creating it in RESULT_POOL. */
static svn_error_t *
get_lock_on_filesystem(const char *lock_filename,
                       apr_pool_t *result_pool)
{
  return svn_error_trace(svn_io__file_lock_autocreate(lock_filename,
                                                      result_pool));
}

/* Reset the HAS_WRITE_LOCK member in the FFD given as BATON_VOID.
   When registered with the pool holding the lock on the lock file,
   this makes sure the flag gets reset just before we release the lock. */
static apr_status_t
reset_lock_flag(void *baton_void)
{
  svn_fs_x__data_t *ffd = baton_void;
  ffd->has_write_lock = FALSE;
  return APR_SUCCESS;
}

/* Structure defining a file system lock to be acquired and the function
   to be executed while the lock is held.

   Instances of this structure may be nested to allow for multiple locks to
   be taken out before executing the user-provided body.  In that case, BODY
   and BATON of the outer instances will be with_lock and a with_lock_baton_t
   instance (transparently, no special treatment is required.).  It is
   illegal to attempt to acquire the same lock twice within the same lock
   chain or via nesting calls using separate lock chains.

   All instances along the chain share the same LOCK_POOL such that only one
   pool needs to be created and cleared for all locks.  We also allocate as
   much data from that lock pool as possible to minimize memory usage in
   caller pools. */
typedef struct with_lock_baton_t
{
  /* The filesystem we operate on.  Same for all instances along the chain. */
  svn_fs_t *fs;

  /* Mutex to complement the lock file in an APR threaded process.
     No-op object for non-threaded processes but never NULL. */
  svn_mutex__t *mutex;

  /* Path to the file to lock. */
  const char *lock_path;

  /* If true, set FS->HAS_WRITE_LOCK after we acquired the lock. */
  svn_boolean_t is_global_lock;

  /* Function body to execute after we acquired the lock.
     This may be user-provided or a nested call to with_lock(). */
  svn_error_t *(*body)(void *baton,
                       apr_pool_t *scratch_pool);

  /* Baton to pass to BODY; possibly NULL.
     This may be user-provided or a nested lock baton instance. */
  void *baton;

  /* Pool for all allocations along the lock chain and BODY.  Will hold the
     file locks and gets destroyed after the outermost BODY returned,
     releasing all file locks.
     Same for all instances along the chain. */
  apr_pool_t *lock_pool;

  /* TRUE, iff BODY is the user-provided body. */
  svn_boolean_t is_inner_most_lock;

  /* TRUE, iff this is not a nested lock.
     Then responsible for destroying LOCK_POOL. */
  svn_boolean_t is_outer_most_lock;
} with_lock_baton_t;

/* Obtain a write lock on the file BATON->LOCK_PATH and call BATON->BODY
   with BATON->BATON.  If this is the outermost lock call, release all file
   locks after the body returned.  If BATON->IS_GLOBAL_LOCK is set, set the
   HAS_WRITE_LOCK flag while we keep the write lock. */
static svn_error_t *
with_some_lock_file(with_lock_baton_t *baton)
{
  apr_pool_t *pool = baton->lock_pool;
  svn_error_t *err = get_lock_on_filesystem(baton->lock_path, pool);

  if (!err)
    {
      svn_fs_t *fs = baton->fs;
      svn_fs_x__data_t *ffd = fs->fsap_data;

      if (baton->is_global_lock)
        {
          /* set the "got the lock" flag and register reset function */
          apr_pool_cleanup_register(pool,
                                    ffd,
                                    reset_lock_flag,
                                    apr_pool_cleanup_null);
          ffd->has_write_lock = TRUE;
        }

      if (baton->is_inner_most_lock)
        {
          /* Use a separate sub-pool for the actual function body and a few
           * file accesses. So, the lock-pool only contains the file locks.
           */
          apr_pool_t *subpool = svn_pool_create(pool);

          /* nobody else will modify the repo state
             => read HEAD & pack info once */
          err = svn_fs_x__update_min_unpacked_rev(fs, subpool);
          if (!err)
            err = svn_fs_x__youngest_rev(&ffd->youngest_rev_cache, fs,
                                         subpool);

          /* We performed a few file operations. Clean the pool. */
          svn_pool_clear(subpool);

          if (!err)
            err = baton->body(baton->baton, subpool);

          svn_pool_destroy(subpool);
        }
      else
        {
          /* Nested lock level */
          err = baton->body(baton->baton, pool);
        }
    }

  if (baton->is_outer_most_lock)
    svn_pool_destroy(pool);

  return svn_error_trace(err);
}

/* Wraps with_some_lock_file, protecting it with BATON->MUTEX.

   SCRATCH_POOL is unused here and only provided for signature compatibility
   with WITH_LOCK_BATON_T.BODY. */
static svn_error_t *
with_lock(void *baton,
          apr_pool_t *scratch_pool)
{
  with_lock_baton_t *lock_baton = baton;
  SVN_MUTEX__WITH_LOCK(lock_baton->mutex, with_some_lock_file(lock_baton));

  return SVN_NO_ERROR;
}

/* Enum identifying a filesystem lock. */
typedef enum lock_id_t
{
  txn_lock,
  write_lock,
  pack_lock
} lock_id_t;

/* Initialize BATON->MUTEX, BATON->LOCK_PATH and BATON->IS_GLOBAL_LOCK
   according to the LOCK_ID.  All other members of BATON must already be
   valid. */
static void
init_lock_baton(with_lock_baton_t *baton,
                lock_id_t lock_id)
{
  svn_fs_x__data_t *ffd = baton->fs->fsap_data;
  svn_fs_x__shared_data_t *ffsd = ffd->shared;

  switch (lock_id)
    {
    case txn_lock:
      baton->mutex = ffsd->txn_current_lock;
      baton->lock_path = svn_fs_x__path_txn_current_lock(baton->fs,
                                                         baton->lock_pool);
      baton->is_global_lock = FALSE;
      break;

    case write_lock:
      baton->mutex = ffsd->fs_write_lock;
      baton->lock_path = svn_fs_x__path_lock(baton->fs, baton->lock_pool);
      baton->is_global_lock = TRUE;
      break;

    case pack_lock:
      baton->mutex = ffsd->fs_pack_lock;
      baton->lock_path = svn_fs_x__path_pack_lock(baton->fs,
                                                  baton->lock_pool);
      baton->is_global_lock = FALSE;
      break;
    }
}

/* Return the  baton for the innermost lock of a (potential) lock chain.
   The baton shall take out LOCK_ID from FS and execute BODY with BATON
   while the lock is being held.  Allocate the result in a sub-pool of
   RESULT_POOL.
 */
static with_lock_baton_t *
create_lock_baton(svn_fs_t *fs,
                  lock_id_t lock_id,
                  svn_error_t *(*body)(void *baton,
                                       apr_pool_t *scratch_pool),
                  void *baton,
                  apr_pool_t *result_pool)
{
  /* Allocate everything along the lock chain into a single sub-pool.
     This minimizes memory usage and cleanup overhead. */
  apr_pool_t *lock_pool = svn_pool_create(result_pool);
  with_lock_baton_t *result = apr_pcalloc(lock_pool, sizeof(*result));

  /* Store parameters. */
  result->fs = fs;
  result->body = body;
  result->baton = baton;

  /* File locks etc. will use this pool as well for easy cleanup. */
  result->lock_pool = lock_pool;

  /* Right now, we are the first, (only, ) and last struct in the chain. */
  result->is_inner_most_lock = TRUE;
  result->is_outer_most_lock = TRUE;

  /* Select mutex and lock file path depending on LOCK_ID.
     Also, initialize dependent members (IS_GLOBAL_LOCK only, ATM). */
  init_lock_baton(result, lock_id);

  return result;
}

/* Return a baton that wraps NESTED and requests LOCK_ID as additional lock.
 *
 * That means, when you create a lock chain, start with the last / innermost
 * lock to take out and add the first / outermost lock last.
 */
static with_lock_baton_t *
chain_lock_baton(lock_id_t lock_id,
                 with_lock_baton_t *nested)
{
  /* Use the same pool for batons along the lock chain. */
  apr_pool_t *lock_pool = nested->lock_pool;
  with_lock_baton_t *result = apr_pcalloc(lock_pool, sizeof(*result));

  /* All locks along the chain operate on the same FS. */
  result->fs = nested->fs;

  /* Execution of this baton means acquiring the nested lock and its
     execution. */
  result->body = with_lock;
  result->baton = nested;

  /* Shared among all locks along the chain. */
  result->lock_pool = lock_pool;

  /* We are the new outermost lock but surely not the innermost lock. */
  result->is_inner_most_lock = FALSE;
  result->is_outer_most_lock = TRUE;
  nested->is_outer_most_lock = FALSE;

  /* Select mutex and lock file path depending on LOCK_ID.
     Also, initialize dependent members (IS_GLOBAL_LOCK only, ATM). */
  init_lock_baton(result, lock_id);

  return result;
}

svn_error_t *
svn_fs_x__with_write_lock(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *scratch_pool),
                          void *baton,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, write_lock, body, baton,
                                       scratch_pool),
                     scratch_pool));
}

svn_error_t *
svn_fs_x__with_pack_lock(svn_fs_t *fs,
                         svn_error_t *(*body)(void *baton,
                                              apr_pool_t *scratch_pool),
                         void *baton,
                         apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, pack_lock, body, baton,
                                       scratch_pool),
                     scratch_pool));
}

svn_error_t *
svn_fs_x__with_txn_current_lock(svn_fs_t *fs,
                                svn_error_t *(*body)(void *baton,
                                                     apr_pool_t *scratch_pool),
                                void *baton,
                                apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, txn_lock, body, baton,
                                       scratch_pool),
                     scratch_pool));
}

svn_error_t *
svn_fs_x__with_all_locks(svn_fs_t *fs,
                         svn_error_t *(*body)(void *baton,
                                              apr_pool_t *scratch_pool),
                         void *baton,
                         apr_pool_t *scratch_pool)
{
  /* Be sure to use the correct lock ordering as documented in
     fs_fs_shared_data_t.  The lock chain is being created in
     innermost (last to acquire) -> outermost (first to acquire) order. */
  with_lock_baton_t *lock_baton
    = create_lock_baton(fs, txn_lock, body, baton, scratch_pool);

  lock_baton = chain_lock_baton(write_lock, lock_baton);
  lock_baton = chain_lock_baton(pack_lock, lock_baton);

  return svn_error_trace(with_lock(lock_baton, scratch_pool));
}


/* A structure used by unlock_proto_rev() and unlock_proto_rev_body(),
   which see. */
typedef struct unlock_proto_rev_baton_t
{
  svn_fs_x__txn_id_t txn_id;
  void *lockcookie;
} unlock_proto_rev_baton_t;

/* Callback used in the implementation of unlock_proto_rev(). */
static svn_error_t *
unlock_proto_rev_body(svn_fs_t *fs,
                      const void *baton,
                      apr_pool_t *scratch_pool)
{
  const unlock_proto_rev_baton_t *b = baton;
  apr_file_t *lockfile = b->lockcookie;
  svn_fs_x__shared_txn_data_t *txn = get_shared_txn(fs, b->txn_id, FALSE);
  apr_status_t apr_err;

  if (!txn)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock unknown transaction '%s'"),
                             svn_fs_x__txn_name(b->txn_id, scratch_pool));
  if (!txn->being_written)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock nonlocked transaction '%s'"),
                             svn_fs_x__txn_name(b->txn_id, scratch_pool));

  apr_err = apr_file_unlock(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't unlock prototype revision lockfile for transaction '%s'"),
       svn_fs_x__txn_name(b->txn_id, scratch_pool));
  apr_err = apr_file_close(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't close prototype revision lockfile for transaction '%s'"),
       svn_fs_x__txn_name(b->txn_id, scratch_pool));

  txn->being_written = FALSE;

  return SVN_NO_ERROR;
}

/* Unlock the prototype revision file for transaction TXN_ID in filesystem
   FS using cookie LOCKCOOKIE.  The original prototype revision file must
   have been closed _before_ calling this function.

   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
unlock_proto_rev(svn_fs_t *fs,
                 svn_fs_x__txn_id_t txn_id,
                 void *lockcookie,
                 apr_pool_t *scratch_pool)
{
  unlock_proto_rev_baton_t b;

  b.txn_id = txn_id;
  b.lockcookie = lockcookie;
  return with_txnlist_lock(fs, unlock_proto_rev_body, &b, scratch_pool);
}

/* A structure used by get_writable_proto_rev() and
   get_writable_proto_rev_body(), which see. */
typedef struct get_writable_proto_rev_baton_t
{
  void **lockcookie;
  svn_fs_x__txn_id_t txn_id;
} get_writable_proto_rev_baton_t;

/* Callback used in the implementation of get_writable_proto_rev(). */
static svn_error_t *
get_writable_proto_rev_body(svn_fs_t *fs,
                            const void *baton,
                            apr_pool_t *scratch_pool)
{
  const get_writable_proto_rev_baton_t *b = baton;
  void **lockcookie = b->lockcookie;
  svn_fs_x__shared_txn_data_t *txn = get_shared_txn(fs, b->txn_id, TRUE);

  /* First, ensure that no thread in this process (including this one)
     is currently writing to this transaction's proto-rev file. */
  if (txn->being_written)
    return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                             _("Cannot write to the prototype revision file "
                               "of transaction '%s' because a previous "
                               "representation is currently being written by "
                               "this process"),
                             svn_fs_x__txn_name(b->txn_id, scratch_pool));


  /* We know that no thread in this process is writing to the proto-rev
     file, and by extension, that no thread in this process is holding a
     lock on the prototype revision lock file.  It is therefore safe
     for us to attempt to lock this file, to see if any other process
     is holding a lock. */

  {
    apr_file_t *lockfile;
    apr_status_t apr_err;
    const char *lockfile_path
      = svn_fs_x__path_txn_proto_rev_lock(fs, b->txn_id, scratch_pool);

    /* Open the proto-rev lockfile, creating it if necessary, as it may
       not exist if the transaction dates from before the lockfiles were
       introduced.

       ### We'd also like to use something like svn_io_file_lock2(), but
           that forces us to create a subpool just to be able to unlock
           the file, which seems a waste. */
    SVN_ERR(svn_io_file_open(&lockfile, lockfile_path,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                             scratch_pool));

    apr_err = apr_file_lock(lockfile,
                            APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK);
    if (apr_err)
      {
        svn_error_clear(svn_io_file_close(lockfile, scratch_pool));

        if (APR_STATUS_IS_EAGAIN(apr_err))
          return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                                   _("Cannot write to the prototype revision "
                                     "file of transaction '%s' because a "
                                     "previous representation is currently "
                                     "being written by another process"),
                                   svn_fs_x__txn_name(b->txn_id,
                                                      scratch_pool));

        return svn_error_wrap_apr(apr_err,
                                  _("Can't get exclusive lock on file '%s'"),
                                  svn_dirent_local_style(lockfile_path,
                                                         scratch_pool));
      }

    *lockcookie = lockfile;
  }

  /* We've successfully locked the transaction; mark it as such. */
  txn->being_written = TRUE;

  return SVN_NO_ERROR;
}

/* Make sure the length ACTUAL_LENGTH of the proto-revision file PROTO_REV
   of transaction TXN_ID in filesystem FS matches the proto-index file.
   Trim any crash / failure related extra data from the proto-rev file.

   If the prototype revision file is too short, we can't do much but bail out.

   Perform all allocations in SCRATCH_POOL. */
static svn_error_t *
auto_truncate_proto_rev(svn_fs_t *fs,
                        apr_file_t *proto_rev,
                        apr_off_t actual_length,
                        svn_fs_x__txn_id_t txn_id,
                        apr_pool_t *scratch_pool)
{
  /* Determine file range covered by the proto-index so far.  Note that
     we always append to both file, i.e. the last index entry also
     corresponds to the last addition in the rev file. */
  const char *path = svn_fs_x__path_p2l_proto_index(fs, txn_id, scratch_pool);
  apr_file_t *file;
  apr_off_t indexed_length;

  SVN_ERR(svn_fs_x__p2l_proto_index_open(&file, path, scratch_pool));
  SVN_ERR(svn_fs_x__p2l_proto_index_next_offset(&indexed_length, file,
                                                scratch_pool));
  SVN_ERR(svn_io_file_close(file, scratch_pool));

  /* Handle mismatches. */
  if (indexed_length < actual_length)
    SVN_ERR(svn_io_file_trunc(proto_rev, indexed_length, scratch_pool));
  else if (indexed_length > actual_length)
    return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                             NULL,
                             _("p2l proto index offset %s beyond proto"
                               "rev file size %s for TXN %s"),
                             apr_off_t_toa(scratch_pool, indexed_length),
                             apr_off_t_toa(scratch_pool, actual_length),
                             svn_fs_x__txn_name(txn_id, scratch_pool));

  return SVN_NO_ERROR;
}

/* Get a handle to the prototype revision file for transaction TXN_ID in
   filesystem FS, and lock it for writing.  Return FILE, a file handle
   positioned at the end of the file, and LOCKCOOKIE, a cookie that
   should be passed to unlock_proto_rev() to unlock the file once FILE
   has been closed.

   If the prototype revision file is already locked, return error
   SVN_ERR_FS_REP_BEING_WRITTEN.

   Perform all allocations in POOL. */
static svn_error_t *
get_writable_proto_rev(apr_file_t **file,
                       void **lockcookie,
                       svn_fs_t *fs,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *pool)
{
  get_writable_proto_rev_baton_t b;
  svn_error_t *err;
  apr_off_t end_offset = 0;

  b.lockcookie = lockcookie;
  b.txn_id = txn_id;

  SVN_ERR(with_txnlist_lock(fs, get_writable_proto_rev_body, &b, pool));

  /* Now open the prototype revision file and seek to the end. */
  err = svn_io_file_open(file,
                         svn_fs_x__path_txn_proto_rev(fs, txn_id, pool),
                         APR_READ | APR_WRITE | APR_BUFFERED, APR_OS_DEFAULT,
                         pool);

  /* You might expect that we could dispense with the following seek
     and achieve the same thing by opening the file using APR_APPEND.
     Unfortunately, APR's buffered file implementation unconditionally
     places its initial file pointer at the start of the file (even for
     files opened with APR_APPEND), so we need this seek to reconcile
     the APR file pointer to the OS file pointer (since we need to be
     able to read the current file position later). */
  if (!err)
    err = svn_io_file_seek(*file, APR_END, &end_offset, pool);

  /* We don't want unused sections (such as leftovers from failed delta
     stream) in our file.  If we use log addressing, we would need an
     index entry for the unused section and that section would need to
     be all NUL by convention.  So, detect and fix those cases by truncating
     the protorev file. */
  if (!err)
    err = auto_truncate_proto_rev(fs, *file, end_offset, txn_id, pool);

  if (err)
    {
      err = svn_error_compose_create(
              err,
              unlock_proto_rev(fs, txn_id, *lockcookie, pool));

      *lockcookie = NULL;
    }

  return svn_error_trace(err);
}

/* Callback used in the implementation of purge_shared_txn(). */
static svn_error_t *
purge_shared_txn_body(svn_fs_t *fs,
                      const void *baton,
                      apr_pool_t *scratch_pool)
{
  svn_fs_x__txn_id_t txn_id = *(const svn_fs_x__txn_id_t *)baton;

  free_shared_txn(fs, txn_id);

  return SVN_NO_ERROR;
}

/* Purge the shared data for transaction TXN_ID in filesystem FS.
   Perform all temporary allocations in SCRATCH_POOL. */
static svn_error_t *
purge_shared_txn(svn_fs_t *fs,
                 svn_fs_x__txn_id_t txn_id,
                 apr_pool_t *scratch_pool)
{
  return with_txnlist_lock(fs, purge_shared_txn_body, &txn_id, scratch_pool);
}


svn_boolean_t
svn_fs_x__is_fresh_txn_root(svn_fs_x__noderev_t *noderev)
{
  /* Is it a root node? */
  if (noderev->noderev_id.number != SVN_FS_X__ITEM_INDEX_ROOT_NODE)
    return FALSE;

  /* ... in a transaction? */
  if (!svn_fs_x__is_txn(noderev->noderev_id.change_set))
    return FALSE;

  /* ... with no prop change in that txn?
     (Once we set a property, the prop rep will never become NULL again.) */
  if (noderev->prop_rep && svn_fs_x__is_txn(noderev->prop_rep->id.change_set))
    return FALSE;

  /* ... and no sub-tree change?
     (Once we set a text, the data rep will never become NULL again.) */
  if (noderev->data_rep && svn_fs_x__is_txn(noderev->data_rep->id.change_set))
    return FALSE;

  /* Root node of a txn with no changes. */
  return TRUE;
}

svn_error_t *
svn_fs_x__put_node_revision(svn_fs_t *fs,
                            svn_fs_x__noderev_t *noderev,
                            apr_pool_t *scratch_pool)
{
  apr_file_t *noderev_file;
  const svn_fs_x__id_t *id = &noderev->noderev_id;

  if (! svn_fs_x__is_txn(id->change_set))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_x__id_unparse(id, scratch_pool)->data);

  SVN_ERR(svn_io_file_open(&noderev_file,
                           svn_fs_x__path_txn_node_rev(fs, id, scratch_pool,
                                                       scratch_pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, scratch_pool));

  SVN_ERR(svn_fs_x__write_noderev(svn_stream_from_aprfile2(noderev_file, TRUE,
                                                           scratch_pool),
                                  noderev, scratch_pool));

  SVN_ERR(svn_io_file_close(noderev_file, scratch_pool));

  return SVN_NO_ERROR;
}

/* For the in-transaction NODEREV within FS, write the sha1->rep mapping
 * file in the respective transaction, if rep sharing has been enabled etc.
 * Use SCATCH_POOL for temporary allocations.
 */
static svn_error_t *
store_sha1_rep_mapping(svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  /* if rep sharing has been enabled and the noderev has a data rep and
   * its SHA-1 is known, store the rep struct under its SHA1. */
  if (   ffd->rep_sharing_allowed
      && noderev->data_rep
      && noderev->data_rep->has_sha1)
    {
      apr_file_t *rep_file;
      apr_int64_t txn_id
        = svn_fs_x__get_txn_id(noderev->data_rep->id.change_set);
      const char *file_name
        = svn_fs_x__path_txn_sha1(fs, txn_id,
                                  noderev->data_rep->sha1_digest,
                                  scratch_pool);
      svn_stringbuf_t *rep_string
        = svn_fs_x__unparse_representation(noderev->data_rep,
                                           (noderev->kind == svn_node_dir),
                                           scratch_pool, scratch_pool);

      SVN_ERR(svn_io_file_open(&rep_file, file_name,
                               APR_WRITE | APR_CREATE | APR_TRUNCATE
                               | APR_BUFFERED, APR_OS_DEFAULT, scratch_pool));

      SVN_ERR(svn_io_file_write_full(rep_file, rep_string->data,
                                     rep_string->len, NULL, scratch_pool));

      SVN_ERR(svn_io_file_close(rep_file, scratch_pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
unparse_dir_entry(svn_fs_x__dirent_t *dirent,
                  svn_stream_t *stream,
                  apr_pool_t *scratch_pool)
{
  apr_size_t to_write;
  apr_size_t name_len = strlen(dirent->name);

  /* A buffer with sufficient space for 
   * - entry name + 1 terminating NUL
   * - 1 byte for the node kind
   * - 2 numbers in 7b/8b encoding for the noderev-id
   */
  apr_byte_t *buffer = apr_palloc(scratch_pool,
                                  name_len + 2 + 2 * SVN__MAX_ENCODED_UINT_LEN);

  /* Now construct the value. */
  apr_byte_t *p = buffer;

  /* The entry name, terminated by NUL. */
  memcpy(p, dirent->name, name_len + 1);
  p += name_len + 1;

  /* The entry type. */
  p = svn__encode_uint(p, dirent->kind);

  /* The ID. */
  p = svn__encode_int(p, dirent->id.change_set);
  p = svn__encode_uint(p, dirent->id.number);

  /* Add the entry to the output stream. */
  to_write = p - buffer;
  SVN_ERR(svn_stream_write(stream, (const char *)buffer, &to_write));

  return SVN_NO_ERROR;
}

/* Write the directory given as array of dirent structs in ENTRIES to STREAM.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
unparse_dir_entries(apr_array_header_t *entries,
                    svn_stream_t *stream,
                    apr_pool_t *scratch_pool)
{
  apr_byte_t buffer[SVN__MAX_ENCODED_UINT_LEN];
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  /* Write the number of entries. */
  apr_size_t to_write = svn__encode_uint(buffer, entries->nelts) - buffer;
  SVN_ERR(svn_stream_write(stream, (const char *)buffer, &to_write));

  /* Write all entries */
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_x__dirent_t *dirent;

      svn_pool_clear(iterpool);
      dirent = APR_ARRAY_IDX(entries, i, svn_fs_x__dirent_t *);
      SVN_ERR(unparse_dir_entry(dirent, stream, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Return a deep copy of SOURCE and allocate it in RESULT_POOL.
 */
static svn_fs_x__change_t *
path_change_dup(const svn_fs_x__change_t *source,
                apr_pool_t *result_pool)
{
  svn_fs_x__change_t *result
    = apr_pmemdup(result_pool, source, sizeof(*source));
  result->path.data
    = apr_pstrmemdup(result_pool, source->path.data, source->path.len);

  if (source->copyfrom_path)
    result->copyfrom_path = apr_pstrdup(result_pool, source->copyfrom_path);

  return result;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_x__change_t CHANGED_PATHS, collapsing multiple changes into a
   single summarical (is that real word?) change per path.  DELETIONS is
   also a path->svn_fs_x__change_t hash and contains all the deletions
   that got turned into a replacement. */
static svn_error_t *
fold_change(apr_hash_t *changed_paths,
            apr_hash_t *deletions,
            const svn_fs_x__change_t *change)
{
  apr_pool_t *pool = apr_hash_pool_get(changed_paths);
  svn_fs_x__change_t *old_change, *new_change;
  const svn_string_t *path = &change->path;

  if ((old_change = apr_hash_get(changed_paths, path->data, path->len)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((change->change_kind == svn_fs_path_change_replace)
                 || (change->change_kind == svn_fs_path_change_add))))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: non-add change on deleted path"));

      /* Sanity check: an add can't follow anything except
         a delete or reset.  */
      if ((change->change_kind == svn_fs_path_change_add)
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: add change on preexisting path"));

      /* Now, merge that change in. */
      switch (change->change_kind)
        {
        case svn_fs_path_change_delete:
          if (old_change->change_kind == svn_fs_path_change_add)
            {
              /* If the path was introduced in this transaction via an
                 add, and we are deleting it, just remove the path
                 altogether.  (The caller will delete any child paths.) */
              apr_hash_set(changed_paths, path->data, path->len, NULL);
            }
          else if (old_change->change_kind == svn_fs_path_change_replace)
            {
              /* A deleting a 'replace' restore the original deletion. */
              new_change = apr_hash_get(deletions, path->data, path->len);
              SVN_ERR_ASSERT(new_change);
              apr_hash_set(changed_paths, path->data, path->len, new_change);
            }
          else
            {
              /* A deletion overrules a previous change (modify). */
              new_change = path_change_dup(change, pool);
              apr_hash_set(changed_paths, path->data, path->len, new_change);
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace.  Remember the original
             deletion such that we are able to delete this path again
             (the replacement may have changed node kind and id). */
          new_change = path_change_dup(change, pool);
          new_change->change_kind = svn_fs_path_change_replace;

          apr_hash_set(changed_paths, path->data, path->len, new_change);

          /* Remember the original change.
           * Make sure to allocate the hash key in a durable pool. */
          apr_hash_set(deletions,
                       apr_pstrmemdup(apr_hash_pool_get(deletions),
                                      path->data, path->len),
                       path->len, old_change);
          break;

        case svn_fs_path_change_modify:
        default:
          /* If the new change modifies some attribute of the node, set
             the corresponding flag, whether it already was set or not.
             Note: We do not reset a flag to FALSE if a change is undone. */
          if (change->text_mod)
            old_change->text_mod = TRUE;
          if (change->prop_mod)
            old_change->prop_mod = TRUE;
          if (change->mergeinfo_mod == svn_tristate_true)
            old_change->mergeinfo_mod = svn_tristate_true;
          break;
        }
    }
  else
    {
      /* Add this path.  The API makes no guarantees that this (new) key
         will not be retained.  Thus, we copy the key into the target pool
         to ensure a proper lifetime.  */
      new_change = path_change_dup(change, pool);
      apr_hash_set(changed_paths, new_change->path.data,
                   new_change->path.len, new_change);
    }

  return SVN_NO_ERROR;
}

/* Baton type to be used with process_changes(). */
typedef struct process_changes_baton_t
{
  /* Folded list of path changes. */
  apr_hash_t *changed_paths;

  /* Path changes that are deletions and have been turned into
     replacements.  If those replacements get deleted again, this
     container contains the record that we have to revert to. */
  apr_hash_t *deletions;
} process_changes_baton_t;

/* An implementation of svn_fs_x__change_receiver_t.
   Examine all the changed path entries in CHANGES and store them in
   *CHANGED_PATHS.  Folding is done to remove redundant or unnecessary
   data. Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
process_changes(void *baton_p,
                svn_fs_x__change_t *change,
                apr_pool_t *scratch_pool)
{
  process_changes_baton_t *baton = baton_p;

  SVN_ERR(fold_change(baton->changed_paths, baton->deletions, change));

  /* Now, if our change was a deletion or replacement, we have to
     blow away any changes thus far on paths that are (or, were)
     children of this path.
     ### i won't bother with another iteration pool here -- at
     most we talking about a few extra dups of paths into what
     is already a temporary subpool.
  */

  if ((change->change_kind == svn_fs_path_change_delete)
       || (change->change_kind == svn_fs_path_change_replace))
    {
      apr_hash_index_t *hi;

      /* a potential child path must contain at least 2 more chars
         (the path separator plus at least one char for the name).
         Also, we should not assume that all paths have been normalized
         i.e. some might have trailing path separators.
      */
      apr_ssize_t path_len = change->path.len;
      apr_ssize_t min_child_len = path_len == 0
                                ? 1
                                : change->path.data[path_len-1] == '/'
                                    ? path_len + 1
                                    : path_len + 2;

      /* CAUTION: This is the inner loop of an O(n^2) algorithm.
         The number of changes to process may be >> 1000.
         Therefore, keep the inner loop as tight as possible.
      */
      for (hi = apr_hash_first(scratch_pool, baton->changed_paths);
           hi;
           hi = apr_hash_next(hi))
        {
          /* KEY is the path. */
          const void *path;
          apr_ssize_t klen;
          apr_hash_this(hi, &path, &klen, NULL);

          /* If we come across a child of our path, remove it.
             Call svn_fspath__skip_ancestor only if there is a chance that
             this is actually a sub-path.
           */
          if (klen >= min_child_len)
            {
              const char *child;

              child = svn_fspath__skip_ancestor(change->path.data, path);
              if (child && child[0] != '\0')
                apr_hash_set(baton->changed_paths, path, klen, NULL);
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__txn_changes_fetch(apr_hash_t **changed_paths_p,
                            svn_fs_t *fs,
                            svn_fs_x__txn_id_t txn_id,
                            apr_pool_t *pool)
{
  apr_file_t *file;
  apr_hash_t *changed_paths = apr_hash_make(pool);
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  process_changes_baton_t baton;

  baton.changed_paths = changed_paths;
  baton.deletions = apr_hash_make(scratch_pool);

  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_x__path_txn_changes(fs, txn_id, scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  SVN_ERR(svn_fs_x__read_changes_incrementally(
                                  svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  process_changes, &baton,
                                  scratch_pool));
  svn_pool_destroy(scratch_pool);

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

/* Copy a revision node-rev SRC into the current transaction TXN_ID in
   the filesystem FS.  This is only used to create the root of a transaction.
   Temporary allocations are from SCRATCH_POOL.  */
static svn_error_t *
create_new_txn_noderev_from_rev(svn_fs_t *fs,
                                svn_fs_x__txn_id_t txn_id,
                                svn_fs_x__id_t *src,
                                apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev;
  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, src, scratch_pool,
                                      scratch_pool));

  /* This must be a root node. */
  SVN_ERR_ASSERT(   noderev->node_id.number == 0
                 && noderev->copy_id.number == 0);

  if (svn_fs_x__is_txn(noderev->noderev_id.change_set))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Copying from transactions not allowed"));

  noderev->predecessor_id = noderev->noderev_id;
  noderev->predecessor_count++;
  noderev->copyfrom_path = NULL;
  noderev->copyfrom_rev = SVN_INVALID_REVNUM;

  /* For the transaction root, the copyroot never changes. */
  svn_fs_x__init_txn_root(&noderev->noderev_id, txn_id);

  return svn_fs_x__put_node_revision(fs, noderev, scratch_pool);
}

/* A structure used by get_and_increment_txn_key_body(). */
typedef struct get_and_increment_txn_key_baton_t
{
  svn_fs_t *fs;
  apr_uint64_t txn_number;
} get_and_increment_txn_key_baton_t;

/* Callback used in the implementation of create_txn_dir().  This gets
   the current base 36 value in PATH_TXN_CURRENT and increments it.
   It returns the original value by the baton. */
static svn_error_t *
get_and_increment_txn_key_body(void *baton,
                               apr_pool_t *scratch_pool)
{
  get_and_increment_txn_key_baton_t *cb = baton;
  svn_fs_t *fs = cb->fs;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *txn_current_path = svn_fs_x__path_txn_current(fs, scratch_pool);
  char new_id_str[SVN_INT64_BUFFER_SIZE];

  svn_stringbuf_t *buf;
  SVN_ERR(svn_fs_x__read_content(&buf, txn_current_path, scratch_pool));

  /* Parse the txn number, stopping at the next non-digit.
   *
   * Note that an empty string is being interpreted as "0".
   * This gives us implicit recovery if the file contents should be lost
   * due to e.g. power failure.
   */
  cb->txn_number = svn__base36toui64(NULL, buf->data);
  if (cb->txn_number == 0)
    ++cb->txn_number;

  /* Check for conflicts.  Those might happen if the server crashed and we
   * had 'svnadmin recover' reset the txn counter.
   *
   * Once we found an unused txn id, claim it by creating the respective
   * txn directory.
   *
   * Note that this is not racy because we hold the txn-current-lock.
   */
  while (TRUE)
    {
      const char *txn_dir;
      svn_node_kind_t kind;
      svn_pool_clear(iterpool);

      txn_dir = svn_fs_x__path_txn_dir(fs, cb->txn_number, iterpool);
      SVN_ERR(svn_io_check_path(txn_dir, &kind, iterpool));
      if (kind == svn_node_none)
        {
          svn_io_dir_make(txn_dir, APR_OS_DEFAULT, iterpool);
          break;
        }

      ++cb->txn_number;
    }

  /* Increment the key and add a trailing \n to the string so the
     txn-current file has a newline in it. */
  SVN_ERR(svn_io_write_atomic2(txn_current_path, new_id_str,
                               svn__ui64tobase36(new_id_str,
                                                 cb->txn_number + 1),
                               txn_current_path, FALSE, scratch_pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Create a unique directory for a transaction in FS based on revision REV.
   Return the ID for this transaction in *ID_P, allocated from RESULT_POOL
   and *TXN_ID.  Use a sequence value in the transaction ID to prevent reuse
   of transaction IDs.  Allocate temporaries from SCRATCH_POOL. */
static svn_error_t *
create_txn_dir(const char **id_p,
               svn_fs_x__txn_id_t *txn_id,
               svn_fs_t *fs,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  get_and_increment_txn_key_baton_t cb;

  /* Get the current transaction sequence value, which is a base-36
    number, from the txn-current file, and write an
    incremented value back out to the file.  Place the revision
    number the transaction is based off into the transaction id. */
  cb.fs = fs;
  SVN_ERR(svn_fs_x__with_txn_current_lock(fs,
                                          get_and_increment_txn_key_body,
                                          &cb,
                                          scratch_pool));
  *txn_id = cb.txn_number;
  *id_p = svn_fs_x__txn_name(*txn_id, result_pool);

  return SVN_NO_ERROR;
}

/* Create a new transaction in filesystem FS, based on revision REV,
   and store it in *TXN_P, allocated in RESULT_POOL.  Allocate necessary
   temporaries from SCRATCH_POOL. */
static svn_error_t *
create_txn(svn_fs_txn_t **txn_p,
           svn_fs_t *fs,
           svn_revnum_t rev,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_fs_x__id_t root_id;

  txn = apr_pcalloc(result_pool, sizeof(*txn));
  ftd = apr_pcalloc(result_pool, sizeof(*ftd));

  /* Valid revision number? */
  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, scratch_pool));

  /* Get the txn_id. */
  SVN_ERR(create_txn_dir(&txn->id, &ftd->txn_id, fs, result_pool,
                         scratch_pool));

  txn->fs = fs;
  txn->base_rev = rev;

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  /* Create a new root node for this transaction. */
  svn_fs_x__init_rev_root(&root_id, rev);
  SVN_ERR(create_new_txn_noderev_from_rev(fs, ftd->txn_id, &root_id,
                                          scratch_pool));

  /* Create an empty rev file. */
  SVN_ERR(svn_io_file_create_empty(
              svn_fs_x__path_txn_proto_rev(fs, ftd->txn_id, scratch_pool),
              scratch_pool));

  /* Create an empty rev-lock file. */
  SVN_ERR(svn_io_file_create_empty(
              svn_fs_x__path_txn_proto_rev_lock(fs, ftd->txn_id, scratch_pool),
              scratch_pool));

  /* Create an empty changes file. */
  SVN_ERR(svn_io_file_create_empty(
              svn_fs_x__path_txn_changes(fs, ftd->txn_id, scratch_pool),
              scratch_pool));

  /* Create the next-ids file. */
  SVN_ERR(svn_io_file_create(
              svn_fs_x__path_txn_next_ids(fs, ftd->txn_id, scratch_pool),
              "0 0\n", scratch_pool));

  return SVN_NO_ERROR;
}

/* Store the property list for transaction TXN_ID in *PROPLIST, allocated
   from RESULT_POOL. Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
get_txn_proplist(apr_hash_t **proplist,
                 svn_fs_t *fs,
                 svn_fs_x__txn_id_t txn_id,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *content;

  /* Check for issue #3696. (When we find and fix the cause, we can change
   * this to an assertion.) */
  if (txn_id == SVN_FS_X__INVALID_TXN_ID)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Internal error: a null transaction id was "
                              "passed to get_txn_proplist()"));

  /* Open the transaction properties file. */
  SVN_ERR(svn_stringbuf_from_file2(&content,
                                   svn_fs_x__path_txn_props(fs, txn_id,
                                                            scratch_pool),
                                   result_pool));

  /* Read in the property list. */
  SVN_ERR_W(svn_fs_x__parse_properties(proplist,
                                   svn_stringbuf__morph_into_string(content),
                                   result_pool),
            apr_psprintf(scratch_pool,
                         _("malformed property list in transaction '%s'"),
                         svn_fs_x__path_txn_props(fs, txn_id, scratch_pool)));

  return SVN_NO_ERROR;
}

/* Save the property list PROPS as the revprops for transaction TXN_ID
   in FS.  Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
set_txn_proplist(svn_fs_t *fs,
                 svn_fs_x__txn_id_t txn_id,
                 apr_hash_t *props,
                 apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  const char *temp_path;

  /* Write the new contents into a temporary file. */
  SVN_ERR(svn_stream_open_unique(&stream, &temp_path,
                                 svn_fs_x__path_txn_dir(fs, txn_id,
                                                        scratch_pool),
                                 svn_io_file_del_none,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_x__write_properties(stream, props, scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  /* Replace the old file with the new one. */
  SVN_ERR(svn_io_file_rename2(temp_path,
                              svn_fs_x__path_txn_props(fs, txn_id,
                                                       scratch_pool),
                              FALSE,
                              scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__change_txn_prop(svn_fs_txn_t *txn,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *props = apr_array_make(scratch_pool, 1,
                                             sizeof(svn_prop_t));
  svn_prop_t prop;

  prop.name = name;
  prop.value = value;
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;

  return svn_fs_x__change_txn_props(txn, props, scratch_pool);
}

svn_error_t *
svn_fs_x__change_txn_props(svn_fs_txn_t *txn,
                           const apr_array_header_t *props,
                           apr_pool_t *scratch_pool)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  apr_hash_t *txn_prop;
  int i;
  svn_error_t *err;

  err = get_txn_proplist(&txn_prop, txn->fs, ftd->txn_id, subpool, subpool);
  /* Here - and here only - we need to deal with the possibility that the
     transaction property file doesn't yet exist.  The rest of the
     implementation assumes that the file exists, but we're called to set the
     initial transaction properties as the transaction is being created. */
  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)))
    svn_error_clear(err);
  else if (err)
    return svn_error_trace(err);

  for (i = 0; i < props->nelts; i++)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

      if (svn_hash_gets(txn_prop, SVN_FS__PROP_TXN_CLIENT_DATE)
          && !strcmp(prop->name, SVN_PROP_REVISION_DATE))
        svn_hash_sets(txn_prop, SVN_FS__PROP_TXN_CLIENT_DATE,
                      svn_string_create("1", subpool));

      svn_hash_sets(txn_prop, prop->name, prop->value);
    }

  /* Create a new version of the file and write out the new props. */
  /* Open the transaction properties file. */
  SVN_ERR(set_txn_proplist(txn->fs, ftd->txn_id, txn_prop, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_txn(svn_fs_x__transaction_t **txn_p,
                  svn_fs_t *fs,
                  svn_fs_x__txn_id_t txn_id,
                  apr_pool_t *pool)
{
  svn_fs_x__transaction_t *txn;
  svn_fs_x__noderev_t *noderev;
  svn_fs_x__id_t root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  svn_fs_x__init_txn_root(&root_id, txn_id);

  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, &root_id, pool, pool));

  txn->base_rev = svn_fs_x__get_revnum(noderev->predecessor_id.change_set);
  txn->copies = NULL;

  *txn_p = txn;

  return SVN_NO_ERROR;
}

/* Store the (ITEM_INDEX, OFFSET) pair in the log-to-phys proto index file
 * of transaction TXN_ID in filesystem FS.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
store_l2p_index_entry(svn_fs_t *fs,
                      svn_fs_x__txn_id_t txn_id,
                      apr_off_t offset,
                      apr_uint64_t item_index,
                      apr_pool_t *scratch_pool)
{
  const char *path = svn_fs_x__path_l2p_proto_index(fs, txn_id, scratch_pool);
  apr_file_t *file;
  SVN_ERR(svn_fs_x__l2p_proto_index_open(&file, path, scratch_pool));
  SVN_ERR(svn_fs_x__l2p_proto_index_add_entry(file, offset, 0,
                                              item_index, scratch_pool));
  SVN_ERR(svn_io_file_close(file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Store ENTRY in the phys-to-log proto index file of transaction TXN_ID
 * in filesystem FS.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
store_p2l_index_entry(svn_fs_t *fs,
                      svn_fs_x__txn_id_t txn_id,
                      const svn_fs_x__p2l_entry_t *entry,
                      apr_pool_t *scratch_pool)
{
  const char *path = svn_fs_x__path_p2l_proto_index(fs, txn_id, scratch_pool);
  apr_file_t *file;
  SVN_ERR(svn_fs_x__p2l_proto_index_open(&file, path, scratch_pool));
  SVN_ERR(svn_fs_x__p2l_proto_index_add_entry(file, entry, scratch_pool));
  SVN_ERR(svn_io_file_close(file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Allocate an item index in the transaction TXN_ID of file system FS and
 * return it in *ITEM_INDEX.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
allocate_item_index(apr_uint64_t *item_index,
                    svn_fs_t *fs,
                    svn_fs_x__txn_id_t txn_id,
                    apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  char buffer[SVN_INT64_BUFFER_SIZE] = { 0 };
  svn_boolean_t eof = FALSE;
  apr_size_t to_write;
  apr_size_t bytes_read;
  apr_off_t offset = 0;

  /* read number */
  SVN_ERR(svn_io_file_open(&file,
                            svn_fs_x__path_txn_item_index(fs, txn_id,
                                                          scratch_pool),
                            APR_READ | APR_WRITE | APR_CREATE,
                            APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_read_full2(file, buffer, sizeof(buffer)-1,
                                  &bytes_read, &eof, scratch_pool));

  /* Item index file should be shorter than SVN_INT64_BUFFER_SIZE,
     otherwise we truncate data. */
  if (!eof)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Unexpected itemidx file length"));
  else if (bytes_read)
    SVN_ERR(svn_cstring_atoui64(item_index, buffer));
  else
    *item_index = SVN_FS_X__ITEM_INDEX_FIRST_USER;

  /* increment it */
  to_write = svn__ui64toa(buffer, *item_index + 1);

  /* write it back to disk */
  SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, scratch_pool));
  SVN_ERR(svn_io_file_write_full(file, buffer, to_write, NULL, scratch_pool));
  SVN_ERR(svn_io_file_close(file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Write out the currently available next node_id NODE_ID and copy_id
   COPY_ID for transaction TXN_ID in filesystem FS.  The next node-id is
   used both for creating new unique nodes for the given transaction, as
   well as uniquifying representations.  Perform temporary allocations in
   SCRATCH_POOL. */
static svn_error_t *
write_next_ids(svn_fs_t *fs,
               svn_fs_x__txn_id_t txn_id,
               apr_uint64_t node_id,
               apr_uint64_t copy_id,
               apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  char buffer[2 * SVN_INT64_BUFFER_SIZE + 2];
  char *p = buffer;

  p += svn__ui64tobase36(p, node_id);
  *(p++) = ' ';
  p += svn__ui64tobase36(p, copy_id);
  *(p++) = '\n';
  *(p++) = '\0';

  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_x__path_txn_next_ids(fs, txn_id,
                                                       scratch_pool),
                           APR_WRITE | APR_TRUNCATE,
                           APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_io_file_write_full(file, buffer, p - buffer, NULL,
                                 scratch_pool));
  return svn_io_file_close(file, scratch_pool);
}

/* Find out what the next unique node-id and copy-id are for
   transaction TXN_ID in filesystem FS.  Store the results in *NODE_ID
   and *COPY_ID.  The next node-id is used both for creating new unique
   nodes for the given transaction, as well as uniquifying representations.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
read_next_ids(apr_uint64_t *node_id,
              apr_uint64_t *copy_id,
              svn_fs_t *fs,
              svn_fs_x__txn_id_t txn_id,
              apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buf;
  const char *str;
  SVN_ERR(svn_fs_x__read_content(&buf,
                                 svn_fs_x__path_txn_next_ids(fs, txn_id,
                                                             scratch_pool),
                                 scratch_pool));

  /* Parse this into two separate strings. */

  str = buf->data;
  *node_id = svn__base36toui64(&str, str);
  if (*str != ' ')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  ++str;
  *copy_id = svn__base36toui64(&str, str);
  if (*str != '\n')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("next-id file corrupt"));

  return SVN_NO_ERROR;
}

/* Get a new and unique to this transaction node-id for transaction
   TXN_ID in filesystem FS.  Store the new node-id in *NODE_ID_P.
   Node-ids are guaranteed to be unique to this transction, but may
   not necessarily be sequential.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
get_new_txn_node_id(svn_fs_x__id_t *node_id_p,
                    svn_fs_t *fs,
                    svn_fs_x__txn_id_t txn_id,
                    apr_pool_t *scratch_pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, scratch_pool));

  node_id_p->change_set = svn_fs_x__change_set_by_txn(txn_id);
  node_id_p->number = node_id;

  SVN_ERR(write_next_ids(fs, txn_id, ++node_id, copy_id, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__reserve_copy_id(svn_fs_x__id_t *copy_id_p,
                          svn_fs_t *fs,
                          svn_fs_x__txn_id_t txn_id,
                          apr_pool_t *scratch_pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, scratch_pool));

  copy_id_p->change_set = svn_fs_x__change_set_by_txn(txn_id);
  copy_id_p->number = copy_id;

  SVN_ERR(write_next_ids(fs, txn_id, node_id, ++copy_id, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__create_node(svn_fs_t *fs,
                      svn_fs_x__noderev_t *noderev,
                      const svn_fs_x__id_t *copy_id,
                      svn_fs_x__txn_id_t txn_id,
                      apr_pool_t *scratch_pool)
{
  /* Get a new node-id for this node. */
  SVN_ERR(get_new_txn_node_id(&noderev->node_id, fs, txn_id, scratch_pool));

  /* Assign copy-id. */
  noderev->copy_id = *copy_id;

  /* Noderev-id = Change set and item number within this change set. */
  noderev->noderev_id.change_set = svn_fs_x__change_set_by_txn(txn_id);
  SVN_ERR(allocate_item_index(&noderev->noderev_id.number, fs, txn_id,
                              scratch_pool));

  SVN_ERR(svn_fs_x__put_node_revision(fs, noderev, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__purge_txn(svn_fs_t *fs,
                    const char *txn_id_str,
                    apr_pool_t *scratch_pool)
{
  svn_fs_x__txn_id_t txn_id;

  /* The functions we are calling open files and operate on the OS FS.
     Since these may allocate a non-trivial amount of memory, do that
     in a SUBPOOL and clear that one up before returning. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  SVN_ERR(svn_fs_x__txn_by_name(&txn_id, txn_id_str));

  /* Remove the shared transaction object associated with this transaction. */
  SVN_ERR(purge_shared_txn(fs, txn_id, subpool));
  /* Remove the directory associated with this transaction. */
  SVN_ERR(svn_io_remove_dir2(svn_fs_x__path_txn_dir(fs, txn_id, subpool),
                             FALSE, NULL, NULL, subpool));

  /* Delete protorev and its lock, which aren't in the txn directory.
     It's OK if they don't exist (for example, if this is post-commit
     and the proto-rev has been moved into place). */
  SVN_ERR(svn_io_remove_file2(
                svn_fs_x__path_txn_proto_rev(fs, txn_id, subpool),
                TRUE, subpool));
  SVN_ERR(svn_io_remove_file2(
                svn_fs_x__path_txn_proto_rev_lock(fs, txn_id, subpool),
                TRUE, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__abort_txn(svn_fs_txn_t *txn,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_fs__check_fs(txn->fs, TRUE));

  /* Now, purge the transaction. */
  SVN_ERR_W(svn_fs_x__purge_txn(txn->fs, txn->id, scratch_pool),
            apr_psprintf(scratch_pool, _("Transaction '%s' cleanup failed"),
                         txn->id));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__set_entry(svn_fs_t *fs,
                    svn_fs_x__txn_id_t txn_id,
                    svn_fs_x__noderev_t *parent_noderev,
                    const char *name,
                    const svn_fs_x__id_t *id,
                    svn_node_kind_t kind,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_t *rep = parent_noderev->data_rep;
  const char *filename
    = svn_fs_x__path_txn_node_children(fs, &parent_noderev->noderev_id,
                                       scratch_pool, scratch_pool);
  apr_file_t *file;
  svn_stream_t *out;
  svn_filesize_t filesize;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  const svn_fs_x__id_t *key = &(parent_noderev->noderev_id);
  svn_fs_x__dirent_t entry;

  if (!rep || !svn_fs_x__is_txn(rep->id.change_set))
    {
      apr_array_header_t *entries;
      svn_fs_x__dir_data_t dir_data;

      /* Before we can modify the directory, we need to dump its old
         contents into a mutable representation file. */
      SVN_ERR(svn_fs_x__rep_contents_dir(&entries, fs, parent_noderev,
                                         subpool, subpool));
      SVN_ERR(svn_io_file_open(&file, filename,
                               APR_WRITE | APR_CREATE | APR_BUFFERED,
                               APR_OS_DEFAULT, scratch_pool));
      out = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
      SVN_ERR(unparse_dir_entries(entries, out, subpool));

      /* Provide the parent with a data rep if it had none before
         (directories so far empty). */
      if (!rep)
        {
          rep = apr_pcalloc(result_pool, sizeof(*rep));
          parent_noderev->data_rep = rep;
        }

      /* Mark the node-rev's data rep as mutable. */
      rep->id.change_set = svn_fs_x__change_set_by_txn(txn_id);
      rep->id.number = SVN_FS_X__ITEM_INDEX_UNUSED;

      /* Save noderev to disk. */
      SVN_ERR(svn_fs_x__put_node_revision(fs, parent_noderev, subpool));

      /* Immediately populate the txn dir cache to avoid re-reading
       * the file we just wrote. */

      /* Flush APR buffers. */
      SVN_ERR(svn_io_file_flush(file, subpool));

      /* Obtain final file size to update txn_dir_cache. */
      SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

      /* Store in the cache. */
      dir_data.entries = entries;
      dir_data.txn_filesize = filesize;
      SVN_ERR(svn_cache__set(ffd->dir_cache, key, &dir_data, subpool));

      svn_pool_clear(subpool);
    }
  else
    {
      svn_boolean_t found;
      svn_filesize_t cached_filesize;

      /* The directory rep is already mutable, so just open it for append. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_WRITE | APR_APPEND,
                               APR_OS_DEFAULT, subpool));
      out = svn_stream_from_aprfile2(file, TRUE, subpool);

      /* If the cache contents is stale, drop it.
       *
       * Note that the directory file is append-only, i.e. if the size
       * did not change, the contents didn't either. */

      /* Get the file size that corresponds to the cached contents
       * (if any). */
      SVN_ERR(svn_cache__get_partial((void **)&cached_filesize, &found,
                                     ffd->dir_cache, key,
                                     svn_fs_x__extract_dir_filesize,
                                     NULL, subpool));

      /* File size info still matches?
       * If not, we need to drop the cache entry. */
      if (found)
        {
          SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

          if (cached_filesize != filesize)
            SVN_ERR(svn_cache__set(ffd->dir_cache, key, NULL, subpool));
        }
    }

  /* Append an incremental hash entry for the entry change.
     A deletion is represented by an "unused" noderev-id. */
  if (id)
    entry.id = *id;
  else
    svn_fs_x__id_reset(&entry.id);

  entry.name = name;
  entry.kind = kind;

  SVN_ERR(unparse_dir_entry(&entry, out, subpool));

  /* Flush APR buffers. */
  SVN_ERR(svn_io_file_flush(file, subpool));

  /* Obtain final file size to update txn_dir_cache. */
  SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

  /* Close file. */
  SVN_ERR(svn_io_file_close(file, subpool));
  svn_pool_clear(subpool);

  /* update directory cache */
    {
      /* build parameters: name, new entry, new file size  */
      replace_baton_t baton;

      baton.name = name;
      baton.new_entry = NULL;
      baton.txn_filesize = filesize;

      if (id)
        {
          baton.new_entry = apr_pcalloc(subpool, sizeof(*baton.new_entry));
          baton.new_entry->name = name;
          baton.new_entry->kind = kind;
          baton.new_entry->id = *id;
        }

      /* actually update the cached directory (if cached) */
      SVN_ERR(svn_cache__set_partial(ffd->dir_cache, key,
                                     svn_fs_x__replace_dir_entry, &baton,
                                     subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__add_change(svn_fs_t *fs,
                     svn_fs_x__txn_id_t txn_id,
                     const char *path,
                     svn_fs_path_change_kind_t change_kind,
                     svn_boolean_t text_mod,
                     svn_boolean_t prop_mod,
                     svn_boolean_t mergeinfo_mod,
                     svn_node_kind_t node_kind,
                     svn_revnum_t copyfrom_rev,
                     const char *copyfrom_path,
                     apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  svn_fs_x__change_t change;
  apr_hash_t *changes = apr_hash_make(scratch_pool);

  /* Not using APR_BUFFERED to append change in one atomic write operation. */
  SVN_ERR(svn_io_file_open(&file,
                           svn_fs_x__path_txn_changes(fs, txn_id,
                                                      scratch_pool),
                           APR_APPEND | APR_WRITE | APR_CREATE,
                           APR_OS_DEFAULT, scratch_pool));

  change.path.data = path;
  change.path.len = strlen(path);
  change.change_kind = change_kind;
  change.text_mod = text_mod;
  change.prop_mod = prop_mod;
  change.mergeinfo_mod = mergeinfo_mod ? svn_tristate_true
                                       : svn_tristate_false;
  change.node_kind = node_kind;
  change.copyfrom_known = TRUE;
  change.copyfrom_rev = copyfrom_rev;
  if (copyfrom_path)
    change.copyfrom_path = apr_pstrdup(scratch_pool, copyfrom_path);

  svn_hash_sets(changes, path, &change);
  SVN_ERR(svn_fs_x__write_changes(svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  fs, changes, FALSE, scratch_pool));

  return svn_io_file_close(file, scratch_pool);
}

/* This baton is used by the representation writing streams.  It keeps
   track of the checksum information as well as the total size of the
   representation so far. */
typedef struct rep_write_baton_t
{
  /* The FS we are writing to. */
  svn_fs_t *fs;

  /* Actual file to which we are writing. */
  svn_stream_t *rep_stream;

  /* A stream from the delta combiner.  Data written here gets
     deltified, then eventually written to rep_stream. */
  svn_stream_t *delta_stream;

  /* Where is this representation header stored. */
  apr_off_t rep_offset;

  /* Start of the actual data. */
  apr_off_t delta_start;

  /* How many bytes have been written to this rep already. */
  svn_filesize_t rep_size;

  /* The node revision for which we're writing out info. */
  svn_fs_x__noderev_t *noderev;

  /* Actual output file. */
  apr_file_t *file;
  /* Lock 'cookie' used to unlock the output file once we've finished
     writing to it. */
  void *lockcookie;

  svn_checksum_ctx_t *md5_checksum_ctx;
  svn_checksum_ctx_t *sha1_checksum_ctx;

  /* Receives the low-level checksum when closing REP_STREAM. */
  apr_uint32_t fnv1a_checksum;

  /* Local pool, available for allocations that must remain valid as long
     as this baton is used but may be cleaned up immediately afterwards. */
  apr_pool_t *local_pool;

  /* Outer / result pool. */
  apr_pool_t *result_pool;
} rep_write_baton_t;

/* Handler for the write method of the representation writable stream.
   BATON is a rep_write_baton_t, DATA is the data to write, and *LEN is
   the length of this data. */
static svn_error_t *
rep_write_contents(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  rep_write_baton_t *b = baton;

  SVN_ERR(svn_checksum_update(b->md5_checksum_ctx, data, *len));
  SVN_ERR(svn_checksum_update(b->sha1_checksum_ctx, data, *len));
  b->rep_size += *len;

  return svn_stream_write(b->delta_stream, data, len);
}

/* Set *SPANNED to the number of shards touched when walking WALK steps on
 * NODEREV's predecessor chain in FS.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
shards_spanned(int *spanned,
               svn_fs_t *fs,
               svn_fs_x__noderev_t *noderev,
               int walk,
               apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  int shard_size = ffd->max_files_per_dir;
  apr_pool_t *iterpool;

  int count = walk ? 1 : 0; /* The start of a walk already touches a shard. */
  svn_revnum_t shard, last_shard = ffd->youngest_rev_cache / shard_size;
  iterpool = svn_pool_create(scratch_pool);
  while (walk-- && noderev->predecessor_count)
    {
      svn_fs_x__id_t id = noderev->predecessor_id;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, &id, scratch_pool,
                                          iterpool));
      shard = svn_fs_x__get_revnum(id.change_set) / shard_size;
      if (shard != last_shard)
        {
          ++count;
          last_shard = shard;
        }
    }
  svn_pool_destroy(iterpool);

  *spanned = count;
  return SVN_NO_ERROR;
}

/* Given a node-revision NODEREV in filesystem FS, return the
   representation in *REP to use as the base for a text representation
   delta if PROPS is FALSE.  If PROPS has been set, a suitable props
   base representation will be returned.  Perform allocations in POOL. */
static svn_error_t *
choose_delta_base(svn_fs_x__representation_t **rep,
                  svn_fs_t *fs,
                  svn_fs_x__noderev_t *noderev,
                  svn_boolean_t props,
                  apr_pool_t *pool)
{
  /* The zero-based index (counting from the "oldest" end), along NODEREVs
   * line predecessors, of the node-rev we will use as delta base. */
  int count;

  /* The length of the linear part of a delta chain.  (Delta chains use
   * skip-delta bits for the high-order bits and are linear in the low-order
   * bits.) */
  int walk;
  svn_fs_x__noderev_t *base;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_pool_t *iterpool;

  /* If we have no predecessors, or that one is empty, then use the empty
   * stream as a base. */
  if (! noderev->predecessor_count)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* Flip the rightmost '1' bit of the predecessor count to determine
     which file rev (counting from 0) we want to use.  (To see why
     count & (count - 1) unsets the rightmost set bit, think about how
     you decrement a binary number.) */
  count = noderev->predecessor_count;
  count = count & (count - 1);

  /* Finding the delta base over a very long distance can become extremely
     expensive for very deep histories, possibly causing client timeouts etc.
     OTOH, this is a rare operation and its gains are minimal. Lets simply
     start deltification anew close every other 1000 changes or so.  */
  walk = noderev->predecessor_count - count;
  if (walk > (int)ffd->max_deltification_walk)
    {
      *rep = NULL;
      return SVN_NO_ERROR;
    }

  /* We use skip delta for limiting the number of delta operations
     along very long node histories.  Close to HEAD however, we create
     a linear history to minimize delta size.  */
  if (walk < (int)ffd->max_linear_deltification)
    {
      int shards;
      SVN_ERR(shards_spanned(&shards, fs, noderev, walk, pool));

      /* We also don't want the linear deltification to span more shards
         than if deltas we used in a simple skip-delta scheme. */
      if ((1 << (--shards)) <= walk)
        count = noderev->predecessor_count - 1;
    }

  /* Walk back a number of predecessors equal to the difference
     between count and the original predecessor count.  (For example,
     if noderev has ten predecessors and we want the eighth file rev,
     walk back two predecessors.) */
  base = noderev;
  iterpool = svn_pool_create(pool);
  while ((count++) < noderev->predecessor_count)
    {
      svn_fs_x__id_t id = noderev->predecessor_id;
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_x__get_node_revision(&base, fs, &id, pool, iterpool));
    }
  svn_pool_destroy(iterpool);

  /* return a suitable base representation */
  *rep = props ? base->prop_rep : base->data_rep;

  /* if we encountered a shared rep, its parent chain may be different
   * from the node-rev parent chain. */
  if (*rep)
    {
      int chain_length = 0;
      int shard_count = 0;

      /* Very short rep bases are simply not worth it as we are unlikely
       * to re-coup the deltification space overhead of 20+ bytes. */
      svn_filesize_t rep_size = (*rep)->expanded_size
                              ? (*rep)->expanded_size
                              : (*rep)->size;
      if (rep_size < 64)
        {
          *rep = NULL;
          return SVN_NO_ERROR;
        }

      /* Check whether the length of the deltification chain is acceptable.
       * Otherwise, shared reps may form a non-skipping delta chain in
       * extreme cases. */
      SVN_ERR(svn_fs_x__rep_chain_length(&chain_length, &shard_count,
                                          *rep, fs, pool));

      /* Some reasonable limit, depending on how acceptable longer linear
       * chains are in this repo.  Also, allow for some minimal chain. */
      if (chain_length >= 2 * (int)ffd->max_linear_deltification + 2)
        *rep = NULL;
      else
        /* To make it worth opening additional shards / pack files, we
         * require that the reps have a certain minimal size.  To deltify
         * against a rep in different shard, the lower limit is 512 bytes
         * and doubles with every extra shard to visit along the delta
         * chain. */
        if (   shard_count > 1
            && ((svn_filesize_t)128 << shard_count) >= rep_size)
          *rep = NULL;
    }

  return SVN_NO_ERROR;
}

/* Something went wrong and the pool for the rep write is being
   cleared before we've finished writing the rep.  So we need
   to remove the rep from the protorevfile and we need to unlock
   the protorevfile. */
static apr_status_t
rep_write_cleanup(void *data)
{
  svn_error_t *err;
  rep_write_baton_t *b = data;
  svn_fs_x__txn_id_t txn_id
    = svn_fs_x__get_txn_id(b->noderev->noderev_id.change_set);

  /* Truncate and close the protorevfile. */
  err = svn_io_file_trunc(b->file, b->rep_offset, b->local_pool);
  err = svn_error_compose_create(err, svn_io_file_close(b->file,
                                                        b->local_pool));

  /* Remove our lock regardless of any preceding errors so that the
     being_written flag is always removed and stays consistent with the
     file lock which will be removed no matter what since the pool is
     going away. */
  err = svn_error_compose_create(err,
                                 unlock_proto_rev(b->fs, txn_id,
                                                  b->lockcookie,
                                                  b->local_pool));
  if (err)
    {
      apr_status_t rc = err->apr_err;
      svn_error_clear(err);
      return rc;
    }

  return APR_SUCCESS;
}

/* Get a rep_write_baton_t, allocated from RESULT_POOL, and store it in
   WB_P for the representation indicated by NODEREV in filesystem FS.
   Only appropriate for file contents, not for props or directory contents.
 */
static svn_error_t *
rep_write_get_baton(rep_write_baton_t **wb_p,
                    svn_fs_t *fs,
                    svn_fs_x__noderev_t *noderev,
                    apr_pool_t *result_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  rep_write_baton_t *b;
  apr_file_t *file;
  svn_fs_x__representation_t *base_rep;
  svn_stream_t *source;
  svn_txdelta_window_handler_t wh;
  void *whb;
  int diff_version = 1;
  svn_fs_x__rep_header_t header = { 0 };
  svn_fs_x__txn_id_t txn_id
    = svn_fs_x__get_txn_id(noderev->noderev_id.change_set);

  b = apr_pcalloc(result_pool, sizeof(*b));

  b->sha1_checksum_ctx = svn_checksum_ctx_create(svn_checksum_sha1,
                                                 result_pool);
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5,
                                                result_pool);

  b->fs = fs;
  b->result_pool = result_pool;
  b->local_pool = svn_pool_create(result_pool);
  b->rep_size = 0;
  b->noderev = noderev;

  /* Open the prototype rev file and seek to its end. */
  SVN_ERR(get_writable_proto_rev(&file, &b->lockcookie, fs, txn_id,
                                 b->local_pool));

  b->file = file;
  b->rep_stream = svn_checksum__wrap_write_stream_fnv1a_32x4(
                              &b->fnv1a_checksum,
                              svn_stream_from_aprfile2(file, TRUE,
                                                       b->local_pool),
                              b->local_pool);

  SVN_ERR(svn_io_file_get_offset(&b->rep_offset, file, b->local_pool));

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, FALSE, b->local_pool));
  SVN_ERR(svn_fs_x__get_contents(&source, fs, base_rep, TRUE,
                                 b->local_pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = svn_fs_x__get_revnum(base_rep->id.change_set);
      header.base_item_index = base_rep->id.number;
      header.base_length = base_rep->size;
      header.type = svn_fs_x__rep_delta;
    }
  else
    {
      header.type = svn_fs_x__rep_self_delta;
    }
  SVN_ERR(svn_fs_x__write_rep_header(&header, b->rep_stream,
                                     b->local_pool));

  /* Now determine the offset of the actual svndiff data. */
  SVN_ERR(svn_io_file_get_offset(&b->delta_start, file, b->local_pool));

  /* Cleanup in case something goes wrong. */
  apr_pool_cleanup_register(b->local_pool, b, rep_write_cleanup,
                            apr_pool_cleanup_null);

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff3(&wh,
                          &whb,
                          svn_stream_disown(b->rep_stream, b->result_pool),
                          diff_version,
                          ffd->delta_compression_level,
                          result_pool);

  b->delta_stream = svn_txdelta_target_push(wh, whb, source,
                                            b->result_pool);

  *wb_p = b;

  return SVN_NO_ERROR;
}

/* For REP->SHA1_CHECKSUM, try to find an already existing representation
   in FS and return it in *OLD_REP.  If no such representation exists or
   if rep sharing has been disabled for FS, NULL will be returned.  Since
   there may be new duplicate representations within the same uncommitted
   revision, those can be passed in REPS_HASH (maps a sha1 digest onto
   svn_fs_x__representation_t*), otherwise pass in NULL for REPS_HASH.

   The content of both representations will be compared, taking REP's content
   from FILE at OFFSET.  Only if they actually match, will *OLD_REP not be
   NULL.

   Use RESULT_POOL for *OLD_REP  allocations and SCRATCH_POOL for temporaries.
   The lifetime of *OLD_REP is limited by both, RESULT_POOL and REP lifetime.
 */
static svn_error_t *
get_shared_rep(svn_fs_x__representation_t **old_rep,
               svn_fs_t *fs,
               svn_fs_x__txn_id_t txn_id,
               svn_fs_x__representation_t *rep,
               apr_file_t *file,
               apr_off_t offset,
               apr_hash_t *reps_hash,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  svn_checksum_t checksum;
  checksum.digest = rep->sha1_digest;
  checksum.kind = svn_checksum_sha1;

  /* Return NULL, if rep sharing has been disabled. */
  *old_rep = NULL;
  if (!ffd->rep_sharing_allowed)
    return SVN_NO_ERROR;

  /* Can't look up if we don't know the key (happens for directories). */
  if (!rep->has_sha1)
    return SVN_NO_ERROR;

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out.  Start with the hash lookup
     because it is cheapest. */
  if (reps_hash)
    *old_rep = apr_hash_get(reps_hash,
                            rep->sha1_digest,
                            APR_SHA1_DIGESTSIZE);

  /* If we haven't found anything yet, try harder and consult our DB. */
  if (*old_rep == NULL)
    {
      err = svn_fs_x__get_rep_reference(old_rep, fs, &checksum, result_pool,
                                        scratch_pool);

      /* ### Other error codes that we shouldn't mask out? */
      if (err == SVN_NO_ERROR)
        {
          if (*old_rep)
            SVN_ERR(svn_fs_x__check_rep(*old_rep, fs, scratch_pool));
        }
      else if (err->apr_err == SVN_ERR_FS_CORRUPT
               || SVN_ERROR_IN_CATEGORY(err->apr_err,
                                        SVN_ERR_MALFUNC_CATEGORY_START))
        {
          /* Fatal error; don't mask it.

             In particular, this block is triggered when the rep-cache refers
             to revisions in the future.  We signal that as a corruption situation
             since, once those revisions are less than youngest (because of more
             commits), the rep-cache would be invalid.
           */
          SVN_ERR(err);
        }
      else
        {
          /* Something's wrong with the rep-sharing index.  We can continue
             without rep-sharing, but warn.
           */
          (fs->warning)(fs->warning_baton, err);
          svn_error_clear(err);
          *old_rep = NULL;
        }
    }

  /* look for intra-revision matches (usually data reps but not limited
     to them in case props happen to look like some data rep)
   */
  if (*old_rep == NULL && svn_fs_x__is_txn(rep->id.change_set))
    {
      svn_node_kind_t kind;
      const char *file_name
        = svn_fs_x__path_txn_sha1(fs,
                                  svn_fs_x__get_txn_id(rep->id.change_set),
                                  rep->sha1_digest, scratch_pool);

      /* in our txn, is there a rep file named with the wanted SHA1?
         If so, read it and use that rep.
       */
      SVN_ERR(svn_io_check_path(file_name, &kind, scratch_pool));
      if (kind == svn_node_file)
        {
          svn_stringbuf_t *rep_string;
          SVN_ERR(svn_stringbuf_from_file2(&rep_string, file_name,
                                           scratch_pool));
          SVN_ERR(svn_fs_x__parse_representation(old_rep, rep_string,
                                                 result_pool, scratch_pool));
        }
    }

  if (!*old_rep)
    return SVN_NO_ERROR;

  /* A simple guard against general rep-cache induced corruption. */
  if ((*old_rep)->expanded_size != rep->expanded_size)
    {
      /* Make the problem show up in the server log.

         Because not sharing reps is always a safe option,
         terminating the request would be inappropriate.
       */
      err = svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                              "Rep size %s mismatches rep-cache.db value %s "
                              "for SHA1 %s.\n"
                              "You should delete the rep-cache.db and "
                              "verify the repository. The cached rep will "
                              "not be shared.",
                              apr_psprintf(scratch_pool,
                                           "%" SVN_FILESIZE_T_FMT,
                                           rep->expanded_size),
                              apr_psprintf(scratch_pool,
                                           "%" SVN_FILESIZE_T_FMT,
                                           (*old_rep)->expanded_size),
                              svn_checksum_to_cstring_display(&checksum,
                                                              scratch_pool));

      (fs->warning)(fs->warning_baton, err);
      svn_error_clear(err);

      /* Ignore the shared rep. */
      *old_rep = NULL;
    }
  else
    {
      /* Add information that is missing in the cached data.
         Use the old rep for this content. */
      memcpy((*old_rep)->md5_digest, rep->md5_digest, sizeof(rep->md5_digest));
    }

  /* If we (very likely) found a matching representation, compare the actual
   * contents such that we can be sure that no rep-cache.db corruption or
   * hash collision produced a false positive. */
  if (*old_rep)
    {
      apr_off_t old_position;
      svn_stream_t *contents;
      svn_stream_t *old_contents;
      svn_boolean_t same;

      /* Make sure we can later restore FILE's current position. */
      SVN_ERR(svn_io_file_get_offset(&old_position, file, scratch_pool));

      /* Compare the two representations.
       * Note that the stream comparison might also produce MD5 checksum
       * errors or other failures in case of SHA1 collisions. */
      SVN_ERR(svn_fs_x__get_contents_from_file(&contents, fs, rep, file,
                                               offset, scratch_pool));
      if ((*old_rep)->id.change_set == rep->id.change_set)
        {
          /* Comparing with contents from the same transaction means
           * reading the same prote-rev FILE.  In the commit stage,
           * the file will already have been moved and the IDs already
           * bumped to the final revision.  Hence, we must determine
           * the OFFSET "manually". */
          svn_fs_x__revision_file_t *rev_file;
          apr_uint32_t sub_item = 0;
          svn_fs_x__id_t id;
          id.change_set = svn_fs_x__change_set_by_txn(txn_id);
          id.number = (*old_rep)->id.number;

          SVN_ERR(svn_fs_x__rev_file_wrap_temp(&rev_file, fs, file,
                                               scratch_pool));
          SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, rev_file,
                                        &id, scratch_pool));

          SVN_ERR(svn_fs_x__get_contents_from_file(&old_contents, fs,
                                                   *old_rep, file,
                                                   offset, scratch_pool));
        }
      else
        {
          SVN_ERR(svn_fs_x__get_contents(&old_contents, fs, *old_rep,
                                         FALSE, scratch_pool));
        }
      err = svn_stream_contents_same2(&same, contents, old_contents,
                                      scratch_pool);

      /* A mismatch should be extremely rare.
       * If it does happen, reject the commit. */
      if (!same || err)
        {
          /* SHA1 collision or worse. */
          svn_stringbuf_t *old_rep_str
            = svn_fs_x__unparse_representation(*old_rep, FALSE,
                                               scratch_pool,
                                               scratch_pool);
          svn_stringbuf_t *rep_str
            = svn_fs_x__unparse_representation(rep, FALSE,
                                               scratch_pool,
                                               scratch_pool);
          const char *checksum__str
            = svn_checksum_to_cstring_display(&checksum, scratch_pool);

          return svn_error_createf(SVN_ERR_FS_AMBIGUOUS_CHECKSUM_REP,
                                   err, "SHA1 of reps '%s' and '%s' "
                                   "matches (%s) but contents differ",
                                   old_rep_str->data, rep_str->data,
                                   checksum__str);
        }

      /* Restore FILE's read / write position. */
      SVN_ERR(svn_io_file_seek(file, APR_SET, &old_position, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Copy the hash sum calculation results from MD5_CTX, SHA1_CTX into REP.
 * SHA1 results are only be set if SHA1_CTX is not NULL.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
digests_final(svn_fs_x__representation_t *rep,
              const svn_checksum_ctx_t *md5_ctx,
              const svn_checksum_ctx_t *sha1_ctx,
              apr_pool_t *scratch_pool)
{
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum_final(&checksum, md5_ctx, scratch_pool));
  memcpy(rep->md5_digest, checksum->digest, svn_checksum_size(checksum));
  rep->has_sha1 = sha1_ctx != NULL;
  if (rep->has_sha1)
    {
      SVN_ERR(svn_checksum_final(&checksum, sha1_ctx, scratch_pool));
      memcpy(rep->sha1_digest, checksum->digest, svn_checksum_size(checksum));
    }

  return SVN_NO_ERROR;
}

/* Close handler for the representation write stream.  BATON is a
   rep_write_baton_t.  Writes out a new node-rev that correctly
   references the representation we just finished writing. */
static svn_error_t *
rep_write_contents_close(void *baton)
{
  rep_write_baton_t *b = baton;
  svn_fs_x__representation_t *rep;
  svn_fs_x__representation_t *old_rep;
  apr_off_t offset;
  apr_int64_t txn_id;

  rep = apr_pcalloc(b->result_pool, sizeof(*rep));

  /* Close our delta stream so the last bits of svndiff are written
     out. */
  SVN_ERR(svn_stream_close(b->delta_stream));

  /* Determine the length of the svndiff data. */
  SVN_ERR(svn_io_file_get_offset(&offset, b->file, b->local_pool));
  rep->size = offset - b->delta_start;

  /* Fill in the rest of the representation field. */
  rep->expanded_size = b->rep_size;
  txn_id = svn_fs_x__get_txn_id(b->noderev->noderev_id.change_set);
  rep->id.change_set = svn_fs_x__change_set_by_txn(txn_id);

  /* Finalize the checksum. */
  SVN_ERR(digests_final(rep, b->md5_checksum_ctx, b->sha1_checksum_ctx,
                        b->result_pool));

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  SVN_ERR(get_shared_rep(&old_rep, b->fs, txn_id, rep, b->file, b->rep_offset,
                         NULL, b->result_pool, b->local_pool));

  if (old_rep)
    {
      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(b->file, b->rep_offset, b->local_pool));

      /* Use the old rep for this content. */
      b->noderev->data_rep = old_rep;
    }
  else
    {
      /* Write out our cosmetic end marker. */
      SVN_ERR(svn_stream_puts(b->rep_stream, "ENDREP\n"));
      SVN_ERR(allocate_item_index(&rep->id.number, b->fs, txn_id,
                                  b->local_pool));
      SVN_ERR(store_l2p_index_entry(b->fs, txn_id, b->rep_offset,
                                    rep->id.number, b->local_pool));

      b->noderev->data_rep = rep;
    }

  SVN_ERR(svn_stream_close(b->rep_stream));

  /* Remove cleanup callback. */
  apr_pool_cleanup_kill(b->local_pool, b, rep_write_cleanup);

  /* Write out the new node-rev information. */
  SVN_ERR(svn_fs_x__put_node_revision(b->fs, b->noderev, b->local_pool));
  if (!old_rep)
    {
      svn_fs_x__p2l_entry_t entry;
      svn_fs_x__id_t noderev_id;
      noderev_id.change_set = SVN_FS_X__INVALID_CHANGE_SET;
      noderev_id.number = rep->id.number;

      entry.offset = b->rep_offset;
      SVN_ERR(svn_io_file_get_offset(&offset, b->file, b->local_pool));
      entry.size = offset - b->rep_offset;
      entry.type = SVN_FS_X__ITEM_TYPE_FILE_REP;
      entry.item_count = 1;
      entry.items = &noderev_id;
      entry.fnv1_checksum = b->fnv1a_checksum;

      SVN_ERR(store_p2l_index_entry(b->fs, txn_id, &entry, b->local_pool));
    }

  SVN_ERR(svn_io_file_close(b->file, b->local_pool));

  /* Write the sha1->rep mapping *after* we successfully written node
   * revision to disk. */
  if (!old_rep)
    SVN_ERR(store_sha1_rep_mapping(b->fs, b->noderev, b->local_pool));

  SVN_ERR(unlock_proto_rev(b->fs, txn_id, b->lockcookie, b->local_pool));
  svn_pool_destroy(b->local_pool);

  return SVN_NO_ERROR;
}

/* Store a writable stream in *CONTENTS_P, allocated in RESULT_POOL, that
   will receive all data written and store it as the file data representation
   referenced by NODEREV in filesystem FS.  Only appropriate for file data,
   not props or directory contents. */
static svn_error_t *
set_representation(svn_stream_t **contents_p,
                   svn_fs_t *fs,
                   svn_fs_x__noderev_t *noderev,
                   apr_pool_t *result_pool)
{
  rep_write_baton_t *wb;

  if (! svn_fs_x__is_txn(noderev->noderev_id.change_set))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_x__id_unparse(&noderev->noderev_id,
                                                  result_pool)->data);

  SVN_ERR(rep_write_get_baton(&wb, fs, noderev, result_pool));

  *contents_p = svn_stream_create(wb, result_pool);
  svn_stream_set_write(*contents_p, rep_write_contents);
  svn_stream_set_close(*contents_p, rep_write_contents_close);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__set_contents(svn_stream_t **stream,
                       svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_pool_t *result_pool)
{
  if (noderev->kind != svn_node_file)
    return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL,
                            _("Can't set text contents of a directory"));

  return set_representation(stream, fs, noderev, result_pool);
}

svn_error_t *
svn_fs_x__create_successor(svn_fs_t *fs,
                           svn_fs_x__noderev_t *new_noderev,
                           const svn_fs_x__id_t *copy_id,
                           svn_fs_x__txn_id_t txn_id,
                           apr_pool_t *scratch_pool)
{
  new_noderev->copy_id = *copy_id;
  new_noderev->noderev_id.change_set = svn_fs_x__change_set_by_txn(txn_id);
  SVN_ERR(allocate_item_index(&new_noderev->noderev_id.number, fs, txn_id,
                              scratch_pool));

  if (! new_noderev->copyroot_path)
    {
      new_noderev->copyroot_path
        = apr_pstrdup(scratch_pool, new_noderev->created_path);
      new_noderev->copyroot_rev
        = svn_fs_x__get_revnum(new_noderev->noderev_id.change_set);
    }

  SVN_ERR(svn_fs_x__put_node_revision(fs, new_noderev, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__set_proplist(svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_hash_t *proplist,
                       apr_pool_t *scratch_pool)
{
  const svn_fs_x__id_t *id = &noderev->noderev_id;
  const char *filename = svn_fs_x__path_txn_node_props(fs, id, scratch_pool,
                                                       scratch_pool);
  apr_file_t *file;
  svn_stream_t *out;

  /* Dump the property list to the mutable property file. */
  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, scratch_pool));
  out = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  SVN_ERR(svn_fs_x__write_properties(out, proplist, scratch_pool));
  SVN_ERR(svn_io_file_close(file, scratch_pool));

  /* Mark the node-rev's prop rep as mutable, if not already done. */
  if (!noderev->prop_rep
      || svn_fs_x__is_revision(noderev->prop_rep->id.change_set))
    {
      svn_fs_x__txn_id_t txn_id
        = svn_fs_x__get_txn_id(noderev->noderev_id.change_set);
      noderev->prop_rep = apr_pcalloc(scratch_pool,
                                      sizeof(*noderev->prop_rep));
      noderev->prop_rep->id.change_set = id->change_set;
      SVN_ERR(allocate_item_index(&noderev->prop_rep->id.number, fs,
                                  txn_id, scratch_pool));
      SVN_ERR(svn_fs_x__put_node_revision(fs, noderev, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* This baton is used by the stream created for write_container_rep. */
typedef struct write_container_baton_t
{
  svn_stream_t *stream;

  apr_size_t size;

  svn_checksum_ctx_t *md5_ctx;

  /* SHA1 calculation is optional. If not needed, this will be NULL. */
  svn_checksum_ctx_t *sha1_ctx;
} write_container_baton_t;

/* The handler for the write_container_rep stream.  BATON is a
   write_container_baton_t, DATA has the data to write and *LEN is the number
   of bytes to write. */
static svn_error_t *
write_container_handler(void *baton,
                        const char *data,
                        apr_size_t *len)
{
  write_container_baton_t *whb = baton;

  SVN_ERR(svn_checksum_update(whb->md5_ctx, data, *len));
  if (whb->sha1_ctx)
    SVN_ERR(svn_checksum_update(whb->sha1_ctx, data, *len));

  SVN_ERR(svn_stream_write(whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

/* Callback function type.  Write the data provided by BATON into STREAM. */
typedef svn_error_t *
(* collection_writer_t)(svn_stream_t *stream,
                        void *baton,
                        apr_pool_t *scratch_pool);

/* Implement collection_writer_t writing the C string->svn_string_t hash
   given as BATON. */
static svn_error_t *
write_hash_to_stream(svn_stream_t *stream,
                     void *baton,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *hash = baton;
  SVN_ERR(svn_fs_x__write_properties(stream, hash, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implement collection_writer_t writing the svn_fs_x__dirent_t* array given
   as BATON. */
static svn_error_t *
write_directory_to_stream(svn_stream_t *stream,
                          void *baton,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *dir = baton;
  SVN_ERR(unparse_dir_entries(dir, stream, scratch_pool));

  return SVN_NO_ERROR;
}


/* Write out the COLLECTION pertaining to the NODEREV in FS as a deltified
   text representation to file FILE using WRITER.  In the process, record the
   total size and the md5 digest in REP and add the representation of type
   ITEM_TYPE to the indexes if necessary.

   If ALLOW_REP_SHARING is FALSE, rep-sharing will not be used, regardless
   of any other option and rep-sharing settings.  If rep sharing has been
   enabled and REPS_HASH is not NULL, it will be used in addition to the
   on-disk cache to find earlier reps with the same content.  If such
   existing reps can be found, we will truncate the one just written from
   the file and return the existing rep.

   If ITEM_TYPE is IS_PROPS equals SVN_FS_FS__ITEM_TYPE_*_PROPS, assume
   that we want to a props representation as the base for our delta.
   If FINAL_REVISION is not SVN_INVALID_REVNUM, use it to determine whether
   to write to the proto-index files.
   Perform temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
write_container_delta_rep(svn_fs_x__representation_t *rep,
                          apr_file_t *file,
                          void *collection,
                          collection_writer_t writer,
                          svn_fs_t *fs,
                          svn_fs_x__txn_id_t txn_id,
                          svn_fs_x__noderev_t *noderev,
                          apr_hash_t *reps_hash,
                          svn_boolean_t allow_rep_sharing,
                          apr_uint32_t item_type,
                          svn_revnum_t final_revision,
                          apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_txdelta_window_handler_t diff_wh;
  void *diff_whb;

  svn_stream_t *file_stream;
  svn_stream_t *stream;
  svn_fs_x__representation_t *base_rep;
  svn_fs_x__representation_t *old_rep = NULL;
  svn_fs_x__p2l_entry_t entry;
  svn_stream_t *source;
  svn_fs_x__rep_header_t header = { 0 };

  apr_off_t rep_end = 0;
  apr_off_t delta_start = 0;
  apr_off_t offset = 0;

  write_container_baton_t *whb;
  int diff_version = 1;
  svn_boolean_t is_props = (item_type == SVN_FS_X__ITEM_TYPE_FILE_PROPS)
                        || (item_type == SVN_FS_X__ITEM_TYPE_DIR_PROPS);

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, is_props, scratch_pool));
  SVN_ERR(svn_fs_x__get_contents(&source, fs, base_rep, FALSE, scratch_pool));

  SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = svn_fs_x__get_revnum(base_rep->id.change_set);
      header.base_item_index = base_rep->id.number;
      header.base_length = base_rep->size;
      header.type = svn_fs_x__rep_delta;
    }
  else
    {
      header.type = svn_fs_x__rep_self_delta;
    }

  file_stream = svn_checksum__wrap_write_stream_fnv1a_32x4(
                                  &entry.fnv1_checksum,
                                  svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  scratch_pool);
  SVN_ERR(svn_fs_x__write_rep_header(&header, file_stream, scratch_pool));
  SVN_ERR(svn_io_file_get_offset(&delta_start, file, scratch_pool));

  /* Prepare to write the svndiff data. */
  svn_txdelta_to_svndiff3(&diff_wh,
                          &diff_whb,
                          svn_stream_disown(file_stream, scratch_pool),
                          diff_version,
                          ffd->delta_compression_level,
                          scratch_pool);

  whb = apr_pcalloc(scratch_pool, sizeof(*whb));
  whb->stream = svn_txdelta_target_push(diff_wh, diff_whb, source,
                                        scratch_pool);
  whb->size = 0;
  whb->md5_ctx = svn_checksum_ctx_create(svn_checksum_md5, scratch_pool);
  if (item_type != SVN_FS_X__ITEM_TYPE_DIR_REP)
    whb->sha1_ctx = svn_checksum_ctx_create(svn_checksum_sha1, scratch_pool);

  /* serialize the hash */
  stream = svn_stream_create(whb, scratch_pool);
  svn_stream_set_write(stream, write_container_handler);

  SVN_ERR(writer(stream, collection, scratch_pool));
  SVN_ERR(svn_stream_close(whb->stream));

  /* Store the results. */
  SVN_ERR(digests_final(rep, whb->md5_ctx, whb->sha1_ctx, scratch_pool));

  /* Update size info. */
  SVN_ERR(svn_io_file_get_offset(&rep_end, file, scratch_pool));
  rep->size = rep_end - delta_start;
  rep->expanded_size = whb->size;

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  if (allow_rep_sharing)
    SVN_ERR(get_shared_rep(&old_rep, fs, txn_id, rep, file, offset, reps_hash,
                           scratch_pool, scratch_pool));

  if (old_rep)
    {
      SVN_ERR(svn_stream_close(file_stream));

      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(file, offset, scratch_pool));

      /* Use the old rep for this content. */
      memcpy(rep, old_rep, sizeof (*rep));
    }
  else
    {
      svn_fs_x__id_t noderev_id;

      /* Write out our cosmetic end marker. */
      SVN_ERR(svn_stream_puts(file_stream, "ENDREP\n"));
      SVN_ERR(svn_stream_close(file_stream));

      SVN_ERR(allocate_item_index(&rep->id.number, fs, txn_id,
                                  scratch_pool));
      SVN_ERR(store_l2p_index_entry(fs, txn_id, offset, rep->id.number,
                                    scratch_pool));

      noderev_id.change_set = SVN_FS_X__INVALID_CHANGE_SET;
      noderev_id.number = rep->id.number;

      entry.offset = offset;
      SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));
      entry.size = offset - entry.offset;
      entry.type = item_type;
      entry.item_count = 1;
      entry.items = &noderev_id;

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, scratch_pool));

      /* update the representation */
      rep->size = rep_end - delta_start;
    }

  return SVN_NO_ERROR;
}

/* Sanity check ROOT_NODEREV, a candidate for being the root node-revision
   of (not yet committed) revision REV in FS.  Use SCRATCH_POOL for temporary
   allocations.

   If you change this function, consider updating svn_fs_x__verify() too.
 */
static svn_error_t *
validate_root_noderev(svn_fs_t *fs,
                      svn_fs_x__noderev_t *root_noderev,
                      svn_revnum_t rev,
                      apr_pool_t *scratch_pool)
{
  svn_revnum_t head_revnum = rev-1;
  int head_predecessor_count;

  SVN_ERR_ASSERT(rev > 0);

  /* Compute HEAD_PREDECESSOR_COUNT. */
  {
    svn_fs_x__id_t head_root_id;
    svn_fs_x__noderev_t *head_root_noderev;

    /* Get /@HEAD's noderev. */
    svn_fs_x__init_rev_root(&head_root_id, head_revnum);
    SVN_ERR(svn_fs_x__get_node_revision(&head_root_noderev, fs,
                                        &head_root_id, scratch_pool,
                                        scratch_pool));

    head_predecessor_count = head_root_noderev->predecessor_count;
  }

  /* Check that the root noderev's predecessor count equals REV.

     This kind of corruption was seen on svn.apache.org (both on
     the root noderev and on other fspaths' noderevs); see
     issue #4129.

     Normally (rev == root_noderev->predecessor_count), but here we
     use a more roundabout check that should only trigger on new instances
     of the corruption, rather than trigger on each and every new commit
     to a repository that has triggered the bug somewhere in its root
     noderev's history.
   */
  if ((root_noderev->predecessor_count - head_predecessor_count)
      != (rev - head_revnum))
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("predecessor count for "
                                 "the root node-revision is wrong: "
                                 "found (%d+%ld != %d), committing r%ld"),
                                 head_predecessor_count,
                                 rev - head_revnum, /* This is equal to 1. */
                                 root_noderev->predecessor_count,
                                 rev);
    }

  return SVN_NO_ERROR;
}

/* Given the potentially txn-local id PART, update that to a permanent ID
 * based on the REVISION.
 */
static void
get_final_id(svn_fs_x__id_t *part,
             svn_revnum_t revision)
{
  if (!svn_fs_x__is_revision(part->change_set))
    part->change_set = svn_fs_x__change_set_by_rev(revision);
}

/* Copy a node-revision specified by id ID in fileystem FS from a
   transaction into the proto-rev-file FILE.  Set *NEW_ID_P to a
   pointer to the new noderev-id.  If this is a directory, copy all
   children as well.

   START_NODE_ID and START_COPY_ID are
   the first available node and copy ids for this filesystem, for older
   FS formats.

   REV is the revision number that this proto-rev-file will represent.

   INITIAL_OFFSET is the offset of the proto-rev-file on entry to
   commit_body.

   Collect the pair_cache_key_t of all directories written to the
   committed cache in DIRECTORY_IDS.

   If REPS_TO_CACHE is not NULL, append to it a copy (allocated in
   REPS_POOL) of each data rep that is new in this revision.

   If REPS_HASH is not NULL, append copies (allocated in REPS_POOL)
   of the representations of each property rep that is new in this
   revision.

   AT_ROOT is true if the node revision being written is the root
   node-revision.  It is only controls additional sanity checking
   logic.

   CHANGED_PATHS is the changed paths hash for the new revision.
   The noderev-ids in it will be updated as soon as the respective
   nodesrevs got their final IDs assigned.

   Temporary allocations are also from SCRATCH_POOL. */
static svn_error_t *
write_final_rev(svn_fs_x__id_t *new_id_p,
                apr_file_t *file,
                svn_revnum_t rev,
                svn_fs_t *fs,
                const svn_fs_x__id_t *id,
                apr_off_t initial_offset,
                apr_array_header_t *directory_ids,
                apr_array_header_t *reps_to_cache,
                apr_hash_t *reps_hash,
                apr_pool_t *reps_pool,
                svn_boolean_t at_root,
                apr_hash_t *changed_paths,
                apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev;
  apr_off_t my_offset;
  svn_fs_x__id_t new_id;
  svn_fs_x__id_t noderev_id;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__get_txn_id(id->change_set);
  svn_fs_x__p2l_entry_t entry;
  svn_fs_x__change_set_t change_set = svn_fs_x__change_set_by_rev(rev);
  svn_stream_t *file_stream;
  apr_pool_t *subpool;

  /* Check to see if this is a transaction node. */
  if (txn_id == SVN_FS_X__INVALID_TXN_ID)
    {
      svn_fs_x__id_reset(new_id_p);
      return SVN_NO_ERROR;
    }

  subpool = svn_pool_create(scratch_pool);
  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, id, scratch_pool,
                                      subpool));

  if (noderev->kind == svn_node_dir)
    {
      apr_array_header_t *entries;
      int i;

      /* This is a directory.  Write out all the children first. */

      SVN_ERR(svn_fs_x__rep_contents_dir(&entries, fs, noderev, scratch_pool,
                                         subpool));
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_x__dirent_t *dirent = APR_ARRAY_IDX(entries, i,
                                                     svn_fs_x__dirent_t *);

          svn_pool_clear(subpool);
          SVN_ERR(write_final_rev(&new_id, file, rev, fs, &dirent->id,
                                  initial_offset, directory_ids,
                                  reps_to_cache, reps_hash,
                                  reps_pool, FALSE, changed_paths, subpool));
          if (new_id.change_set == change_set)
            dirent->id = new_id;
        }

      if (noderev->data_rep
          && ! svn_fs_x__is_revision(noderev->data_rep->id.change_set))
        {
          svn_fs_x__pair_cache_key_t *key;
          svn_fs_x__dir_data_t dir_data;

          /* Write out the contents of this directory as a text rep. */
          noderev->data_rep->id.change_set = change_set;
          SVN_ERR(write_container_delta_rep(noderev->data_rep, file,
                                            entries,
                                            write_directory_to_stream,
                                            fs, txn_id, noderev, NULL, FALSE,
                                            SVN_FS_X__ITEM_TYPE_DIR_REP,
                                            rev, scratch_pool));

          /* Cache the new directory contents.  Otherwise, subsequent reads
           * or commits will likely have to reconstruct, verify and parse
           * it again. */
          key = apr_array_push(directory_ids);
          key->revision = noderev->data_rep->id.change_set;
          key->second = noderev->data_rep->id.number;

          /* Store directory contents under the new revision number but mark
           * it as "stale" by setting the file length to 0.  Committed dirs
           * will report -1, in-txn dirs will report > 0, so that this can
           * never match.  We reset that to -1 after the commit is complete.
           */
          dir_data.entries = entries;
          dir_data.txn_filesize = 0;

          SVN_ERR(svn_cache__set(ffd->dir_cache, key, &dir_data, subpool));
        }
    }
  else
    {
      /* This is a file.  We should make sure the data rep, if it
         exists in a "this" state, gets rewritten to our new revision
         num. */

      if (noderev->data_rep
          && svn_fs_x__is_txn(noderev->data_rep->id.change_set))
        {
          noderev->data_rep->id.change_set = change_set;
        }
    }

  svn_pool_destroy(subpool);

  /* Fix up the property reps. */
  if (noderev->prop_rep
      && svn_fs_x__is_txn(noderev->prop_rep->id.change_set))
    {
      apr_hash_t *proplist;
      apr_uint32_t item_type = noderev->kind == svn_node_dir
                             ? SVN_FS_X__ITEM_TYPE_DIR_PROPS
                             : SVN_FS_X__ITEM_TYPE_FILE_PROPS;
      SVN_ERR(svn_fs_x__get_proplist(&proplist, fs, noderev, scratch_pool,
                                     scratch_pool));

      noderev->prop_rep->id.change_set = change_set;

      SVN_ERR(write_container_delta_rep(noderev->prop_rep, file, proplist,
                                        write_hash_to_stream, fs, txn_id,
                                        noderev, reps_hash, TRUE, item_type,
                                        rev, scratch_pool));
    }

  /* Convert our temporary ID into a permanent revision one. */
  get_final_id(&noderev->node_id, rev);
  get_final_id(&noderev->copy_id, rev);
  get_final_id(&noderev->noderev_id, rev);

  if (noderev->copyroot_rev == SVN_INVALID_REVNUM)
    noderev->copyroot_rev = rev;

  SVN_ERR(svn_io_file_get_offset(&my_offset, file, scratch_pool));

  SVN_ERR(store_l2p_index_entry(fs, txn_id, my_offset,
                                noderev->noderev_id.number, scratch_pool));
  new_id = noderev->noderev_id;

  if (ffd->rep_sharing_allowed)
    {
      /* Save the data representation's hash in the rep cache. */
      if (   noderev->data_rep && noderev->kind == svn_node_file
          && svn_fs_x__get_revnum(noderev->data_rep->id.change_set) == rev)
        {
          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, svn_fs_x__representation_t *)
            = svn_fs_x__rep_copy(noderev->data_rep, reps_pool);
        }

      if (   noderev->prop_rep
          && svn_fs_x__get_revnum(noderev->prop_rep->id.change_set) == rev)
        {
          /* Add new property reps to hash and on-disk cache. */
          svn_fs_x__representation_t *copy
            = svn_fs_x__rep_copy(noderev->prop_rep, reps_pool);

          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, svn_fs_x__representation_t *) = copy;

          apr_hash_set(reps_hash,
                        copy->sha1_digest,
                        APR_SHA1_DIGESTSIZE,
                        copy);
        }
    }

  /* don't serialize SHA1 for dirs to disk (waste of space) */
  if (noderev->data_rep && noderev->kind == svn_node_dir)
    noderev->data_rep->has_sha1 = FALSE;

  /* don't serialize SHA1 for props to disk (waste of space) */
  if (noderev->prop_rep)
    noderev->prop_rep->has_sha1 = FALSE;

  /* Write out our new node-revision. */
  if (at_root)
    SVN_ERR(validate_root_noderev(fs, noderev, rev, scratch_pool));

  file_stream = svn_checksum__wrap_write_stream_fnv1a_32x4(
                                  &entry.fnv1_checksum,
                                  svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  scratch_pool);
  SVN_ERR(svn_fs_x__write_noderev(file_stream, noderev, scratch_pool));
  SVN_ERR(svn_stream_close(file_stream));

  /* reference the root noderev from the log-to-phys index */
  noderev_id = noderev->noderev_id;
  noderev_id.change_set = SVN_FS_X__INVALID_CHANGE_SET;

  entry.offset = my_offset;
  SVN_ERR(svn_io_file_get_offset(&my_offset, file, scratch_pool));
  entry.size = my_offset - entry.offset;
  entry.type = SVN_FS_X__ITEM_TYPE_NODEREV;
  entry.item_count = 1;
  entry.items = &noderev_id;

  SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, scratch_pool));

  /* Return our ID that references the revision file. */
  *new_id_p = new_id;

  return SVN_NO_ERROR;
}

/* Write the changed path info CHANGED_PATHS from transaction TXN_ID to the
   permanent rev-file FILE representing NEW_REV in filesystem FS.  *OFFSET_P
   is set the to offset in the file of the beginning of this information.
   NEW_REV is the revision currently being committed.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
write_final_changed_path_info(apr_off_t *offset_p,
                              apr_file_t *file,
                              svn_fs_t *fs,
                              svn_fs_x__txn_id_t txn_id,
                              apr_hash_t *changed_paths,
                              svn_revnum_t new_rev,
                              apr_pool_t *scratch_pool)
{
  apr_off_t offset;
  svn_stream_t *stream;
  svn_fs_x__p2l_entry_t entry;
  svn_fs_x__id_t rev_item
    = {SVN_INVALID_REVNUM, SVN_FS_X__ITEM_INDEX_CHANGES};

  SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));

  /* write to target file & calculate checksum */
  stream = svn_checksum__wrap_write_stream_fnv1a_32x4(&entry.fnv1_checksum,
                         svn_stream_from_aprfile2(file, TRUE, scratch_pool),
                         scratch_pool);
  SVN_ERR(svn_fs_x__write_changes(stream, fs, changed_paths, TRUE,
                                  scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  *offset_p = offset;

  /* reference changes from the indexes */
  entry.offset = offset;
  SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));
  entry.size = offset - entry.offset;
  entry.type = SVN_FS_X__ITEM_TYPE_CHANGES;
  entry.item_count = 1;
  entry.items = &rev_item;

  SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, scratch_pool));
  SVN_ERR(store_l2p_index_entry(fs, txn_id, entry.offset,
                                SVN_FS_X__ITEM_INDEX_CHANGES, scratch_pool));

  return SVN_NO_ERROR;
}

/* Open a new svn_fs_t handle to FS, set that handle's concept of "current
   youngest revision" to NEW_REV, and call svn_fs_x__verify_root() on
   NEW_REV's revision root.

   Intended to be called as the very last step in a commit before 'current'
   is bumped.  This implies that we are holding the write lock. */
static svn_error_t *
verify_as_revision_before_current_plus_plus(svn_fs_t *fs,
                                            svn_revnum_t new_rev,
                                            apr_pool_t *scratch_pool)
{
#ifdef SVN_DEBUG
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_t *ft; /* fs++ == ft */
  svn_fs_root_t *root;
  svn_fs_x__data_t *ft_ffd;
  apr_hash_t *fs_config;

  SVN_ERR_ASSERT(ffd->svn_fs_open_);

  /* make sure FT does not simply return data cached by other instances
   * but actually retrieves it from disk at least once.
   */
  fs_config = apr_hash_make(scratch_pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(scratch_pool));
  SVN_ERR(ffd->svn_fs_open_(&ft, fs->path,
                            fs_config,
                            scratch_pool,
                            scratch_pool));
  ft_ffd = ft->fsap_data;
  /* Don't let FT consult rep-cache.db, either. */
  ft_ffd->rep_sharing_allowed = FALSE;

  /* Time travel! */
  ft_ffd->youngest_rev_cache = new_rev;

  SVN_ERR(svn_fs_x__revision_root(&root, ft, new_rev, scratch_pool));
  SVN_ERR_ASSERT(root->is_txn_root == FALSE && root->rev == new_rev);
  SVN_ERR_ASSERT(ft_ffd->youngest_rev_cache == new_rev);
  SVN_ERR(svn_fs_x__verify_root(root, scratch_pool));
#endif /* SVN_DEBUG */

  return SVN_NO_ERROR;
}

/* Verify that the user registered with FS has all the locks necessary to
   permit all the changes associated with TXN_NAME.
   The FS write lock is assumed to be held by the caller. */
static svn_error_t *
verify_locks(svn_fs_t *fs,
             svn_fs_x__txn_id_t txn_id,
             apr_hash_t *changed_paths,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  apr_array_header_t *changed_paths_sorted;
  svn_stringbuf_t *last_recursed = NULL;
  int i;

  /* Make an array of the changed paths, and sort them depth-first-ily.  */
  changed_paths_sorted = svn_sort__hash(changed_paths,
                                        svn_sort_compare_items_as_paths,
                                        scratch_pool);

  /* Now, traverse the array of changed paths, verify locks.  Note
     that if we need to do a recursive verification a path, we'll skip
     over children of that path when we get to them. */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < changed_paths_sorted->nelts; i++)
    {
      const svn_sort__item_t *item;
      const char *path;
      svn_fs_x__change_t *change;
      svn_boolean_t recurse = TRUE;

      svn_pool_clear(iterpool);

      item = &APR_ARRAY_IDX(changed_paths_sorted, i, svn_sort__item_t);

      /* Fetch the change associated with our path.  */
      path = item->key;
      change = item->value;

      /* If this path has already been verified as part of a recursive
         check of one of its parents, no need to do it again.  */
      if (last_recursed
          && svn_fspath__skip_ancestor(last_recursed->data, path))
        continue;

      /* What does it mean to succeed at lock verification for a given
         path?  For an existing file or directory getting modified
         (text, props), it means we hold the lock on the file or
         directory.  For paths being added or removed, we need to hold
         the locks for that path and any children of that path.

         WHEW!  We have no reliable way to determine the node kind
         of deleted items, but fortunately we are going to do a
         recursive check on deleted paths regardless of their kind.  */
      if (change->change_kind == svn_fs_path_change_modify)
        recurse = FALSE;
      SVN_ERR(svn_fs_x__allow_locked_operation(path, fs, recurse, TRUE,
                                               iterpool));

      /* If we just did a recursive check, remember the path we
         checked (so children can be skipped).  */
      if (recurse)
        {
          if (! last_recursed)
            last_recursed = svn_stringbuf_create(path, scratch_pool);
          else
            svn_stringbuf_set(last_recursed, path);
        }
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Based on the transaction properties of TXN, write the final revision
   properties for REVISION into their final location. Return that location
   in *PATH and schedule the necessary fsync calls in BATCH.  This involves
   setting svn:date and removing any temporary properties associated with
   the commit flags. */
static svn_error_t *
write_final_revprop(const char **path,
                    svn_fs_txn_t *txn,
                    svn_revnum_t revision,
                    svn_fs_x__batch_fsync_t *batch,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  svn_string_t date;
  svn_string_t *client_date;
  apr_file_t *file;

  SVN_ERR(svn_fs_x__txn_proplist(&props, txn, scratch_pool));

  /* Remove any temporary txn props representing 'flags'. */
  if (svn_hash_gets(props, SVN_FS__PROP_TXN_CHECK_OOD))
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_OOD, NULL);

  if (svn_hash_gets(props, SVN_FS__PROP_TXN_CHECK_LOCKS))
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_LOCKS, NULL);

  client_date = svn_hash_gets(props, SVN_FS__PROP_TXN_CLIENT_DATE);
  if (client_date)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CLIENT_DATE, NULL);

  /* Update commit time to ensure that svn:date revprops remain ordered if
     requested. */
  if (!client_date || strcmp(client_date->data, "1"))
    {
      date.data = svn_time_to_cstring(apr_time_now(), scratch_pool);
      date.len = strlen(date.data);
      svn_hash_sets(props, SVN_PROP_REVISION_DATE, &date);
    }

  /* Create a file at the final revprops location. */
  *path = svn_fs_x__path_revprops(txn->fs, revision, result_pool);
  SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, *path, scratch_pool));

  /* Write the new contents to the final revprops file. */
  SVN_ERR(svn_fs_x__write_non_packed_revprops(file, props, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__add_index_data(svn_fs_t *fs,
                         apr_file_t *file,
                         const char *l2p_proto_index,
                         const char *p2l_proto_index,
                         svn_revnum_t revision,
                         apr_pool_t *scratch_pool)
{
  apr_off_t l2p_offset;
  apr_off_t p2l_offset;
  svn_stringbuf_t *footer;
  unsigned char footer_length;
  svn_checksum_t *l2p_checksum;
  svn_checksum_t *p2l_checksum;

  /* Append the actual index data to the pack file. */
  l2p_offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_END, &l2p_offset, scratch_pool));
  SVN_ERR(svn_fs_x__l2p_index_append(&l2p_checksum, fs, file,
                                     l2p_proto_index, revision,
                                     scratch_pool, scratch_pool));

  p2l_offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_END, &p2l_offset, scratch_pool));
  SVN_ERR(svn_fs_x__p2l_index_append(&p2l_checksum, fs, file,
                                     p2l_proto_index, revision,
                                     scratch_pool, scratch_pool));

  /* Append footer. */
  footer = svn_fs_x__unparse_footer(l2p_offset, l2p_checksum,
                                    p2l_offset, p2l_checksum, scratch_pool,
                                    scratch_pool);
  SVN_ERR(svn_io_file_write_full(file, footer->data, footer->len, NULL,
                                 scratch_pool));

  footer_length = footer->len;
  SVN_ERR_ASSERT(footer_length == footer->len);
  SVN_ERR(svn_io_file_write_full(file, &footer_length, 1, NULL,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* Make sure that the shard folder for REVSION exists in FS.  If we had to
   create them, schedule their fsync in BATCH.  Use SCRATCH_POOL for
   temporary allocations. */
static svn_error_t *
auto_create_shard(svn_fs_t *fs,
                  svn_revnum_t revision,
                  svn_fs_x__batch_fsync_t *batch,
                  apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  if (revision % ffd->max_files_per_dir == 0)
    {
      const char *new_dir = svn_fs_x__path_shard(fs, revision, scratch_pool);
      svn_error_t *err = svn_io_dir_make(new_dir, APR_OS_DEFAULT,
                                         scratch_pool);

      if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
        return svn_error_trace(err);
      svn_error_clear(err);

      SVN_ERR(svn_io_copy_perms(svn_dirent_join(fs->path, PATH_REVS_DIR,
                                                scratch_pool),
                                new_dir, scratch_pool));
      SVN_ERR(svn_fs_x__batch_fsync_new_path(batch, new_dir, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Move the protype revision file of transaction TXN_ID in FS to the final
   location for REVISION and return a handle to it in *FILE.  Schedule any
   fsyncs in BATCH and use SCRATCH_POOL for temporaries.

   Note that the lifetime of *FILE is determined by BATCH instead of
   SCRATCH_POOL.  It will be invalidated by either BATCH being cleaned up
   itself of by running svn_fs_x__batch_fsync_run on it.

   This function will "destroy" the transaction by removing its prototype
   revision file, so it can at most be called once per transaction.  Also,
   later attempts to modify this txn will fail due to get_writable_proto_rev
   not finding the protorev file.  Therefore, we will take out the lock for
   it only until we move the file to its final location.

   If the prototype revision file is already locked, return error
   SVN_ERR_FS_REP_BEING_WRITTEN. */
static svn_error_t *
get_writable_final_rev(apr_file_t **file,
                       svn_fs_t *fs,
                       svn_fs_x__txn_id_t txn_id,
                       svn_revnum_t revision,
                       svn_fs_x__batch_fsync_t *batch,
                       apr_pool_t *scratch_pool)
{
  get_writable_proto_rev_baton_t baton;
  apr_off_t end_offset = 0;
  void *lockcookie;

  const char *proto_rev_filename
    = svn_fs_x__path_txn_proto_rev(fs, txn_id, scratch_pool);
  const char *final_rev_filename
    = svn_fs_x__path_rev(fs, revision, scratch_pool);

  /* Acquire exclusive access to the proto-rev file. */
  baton.lockcookie = &lockcookie;
  baton.txn_id = txn_id;

  SVN_ERR(with_txnlist_lock(fs, get_writable_proto_rev_body, &baton,
                            scratch_pool));

  /* Move the proto-rev file to its final location as revision data file.
     After that, we don't need to protect it anymore and can unlock it. */
  SVN_ERR(svn_error_compose_create(svn_io_file_rename2(proto_rev_filename,
                                                       final_rev_filename,
                                                       FALSE,
                                                       scratch_pool),
                                   unlock_proto_rev(fs, txn_id, lockcookie,
                                                    scratch_pool)));
  SVN_ERR(svn_fs_x__batch_fsync_new_path(batch, final_rev_filename,
                                         scratch_pool));

  /* Now open the prototype revision file and seek to the end.
     Note that BATCH always seeks to position 0 before returning the file. */
  SVN_ERR(svn_fs_x__batch_fsync_open_file(file, batch, final_rev_filename,
                                          scratch_pool));
  SVN_ERR(svn_io_file_seek(*file, APR_END, &end_offset, scratch_pool));

  /* We don't want unused sections (such as leftovers from failed delta
     stream) in our file.  Detect and fix those cases by truncating the
     protorev file. */
  SVN_ERR(auto_truncate_proto_rev(fs, *file, end_offset, txn_id,
                                  scratch_pool));

  return SVN_NO_ERROR;
}

/* Write REVISION into FS' 'next' file and schedule necessary fsyncs in BATCH.
   Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
write_next_file(svn_fs_t *fs,
                svn_revnum_t revision,
                svn_fs_x__batch_fsync_t *batch,
                apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  const char *path = svn_fs_x__path_next(fs, scratch_pool);
  const char *perms_path = svn_fs_x__path_current(fs, scratch_pool);
  char *buf;

  /* Create / open the 'next' file. */
  SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, path, scratch_pool));

  /* Write its contents. */
  buf = apr_psprintf(scratch_pool, "%ld\n", revision);
  SVN_ERR(svn_io_file_write_full(file, buf, strlen(buf), NULL, scratch_pool));

  /* Adjust permissions. */
  SVN_ERR(svn_io_copy_perms(perms_path, path, scratch_pool));

  return SVN_NO_ERROR;
}

/* Bump the 'current' file in FS to NEW_REV.  Schedule fsyncs in BATCH.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
bump_current(svn_fs_t *fs,
             svn_revnum_t new_rev,
             svn_fs_x__batch_fsync_t *batch,
             apr_pool_t *scratch_pool)
{
  const char *current_filename;

  /* Write the 'next' file. */
  SVN_ERR(write_next_file(fs, new_rev, batch, scratch_pool));

  /* Commit all changes to disk. */
  SVN_ERR(svn_fs_x__batch_fsync_run(batch, scratch_pool));

  /* Make the revision visible to all processes and threads. */
  current_filename = svn_fs_x__path_current(fs, scratch_pool);
  SVN_ERR(svn_fs_x__move_into_place(svn_fs_x__path_next(fs, scratch_pool),
                                    current_filename, current_filename,
                                    batch, scratch_pool));

  /* Make the new revision permanently visible. */
  SVN_ERR(svn_fs_x__batch_fsync_run(batch, scratch_pool));

  return SVN_NO_ERROR;
}

/* Mark the directories cached in FS with the keys from DIRECTORY_IDS
 * as "valid" now.  Use SCRATCH_POOL for temporaries. */
static svn_error_t *
promote_cached_directories(svn_fs_t *fs,
                           apr_array_header_t *directory_ids,
                           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_pool_t *iterpool;
  int i;

  if (!ffd->dir_cache)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < directory_ids->nelts; ++i)
    {
      const svn_fs_x__pair_cache_key_t *key
        = &APR_ARRAY_IDX(directory_ids, i, svn_fs_x__pair_cache_key_t);

      svn_pool_clear(iterpool);

      /* Currently, the entry for KEY - if it still exists - is marked
       * as "stale" and would not be used.  Mark it as current for in-
       * revison data. */
      SVN_ERR(svn_cache__set_partial(ffd->dir_cache, key,
                                     svn_fs_x__reset_txn_filesize, NULL,
                                     iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Baton used for commit_body below. */
typedef struct commit_baton_t {
  svn_revnum_t *new_rev_p;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_array_header_t *reps_to_cache;
  apr_hash_t *reps_hash;
  apr_pool_t *reps_pool;
} commit_baton_t;

/* The work-horse for svn_fs_x__commit, called with the FS write lock.
   This implements the svn_fs_x__with_write_lock() 'body' callback
   type.  BATON is a 'commit_baton_t *'. */
static svn_error_t *
commit_body(void *baton,
            apr_pool_t *scratch_pool)
{
  commit_baton_t *cb = baton;
  svn_fs_x__data_t *ffd = cb->fs->fsap_data;
  const char *old_rev_filename, *rev_filename;
  const char *revprop_filename;
  svn_fs_x__id_t root_id, new_root_id;
  svn_revnum_t old_rev, new_rev;
  apr_file_t *proto_file;
  apr_off_t initial_offset, changed_path_offset;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__txn_get_id(cb->txn);
  apr_hash_t *changed_paths;
  svn_fs_x__batch_fsync_t *batch;
  apr_array_header_t *directory_ids
    = apr_array_make(scratch_pool, 4, sizeof(svn_fs_x__pair_cache_key_t));

  /* We perform a sequence of (potentially) large allocations.
     Keep the peak memory usage low by using a SUBPOOL and cleaning it
     up frequently. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  /* Re-Read the current repository format.  All our repo upgrade and
     config evaluation strategies are such that existing information in
     FS and FFD remains valid.

     Although we don't recommend upgrading hot repositories, people may
     still do it and we must make sure to either handle them gracefully
     or to error out.
   */
  SVN_ERR(svn_fs_x__read_format_file(cb->fs, subpool));

  /* Get the current youngest revision. */
  SVN_ERR(svn_fs_x__youngest_rev(&old_rev, cb->fs, subpool));
  svn_pool_clear(subpool);

  /* Check to make sure this transaction is based off the most recent
     revision. */
  if (cb->txn->base_rev != old_rev)
    return svn_error_create(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                            _("Transaction out of date"));

  /* We need the changes list for verification as well as for writing it
     to the final rev file. */
  SVN_ERR(svn_fs_x__txn_changes_fetch(&changed_paths, cb->fs, txn_id,
                                      scratch_pool));

  /* Locks may have been added (or stolen) between the calling of
     previous svn_fs.h functions and svn_fs_commit_txn(), so we need
     to re-examine every changed-path in the txn and re-verify all
     discovered locks. */
  SVN_ERR(verify_locks(cb->fs, txn_id, changed_paths, subpool));
  svn_pool_clear(subpool);

  /* We are going to be one better than this puny old revision. */
  new_rev = old_rev + 1;

  /* Use this to force all data to be flushed to physical storage
     (to the degree our environment will allow). */
  SVN_ERR(svn_fs_x__batch_fsync_create(&batch, ffd->flush_to_disk,
                                       scratch_pool));

  /* Set up the target directory. */
  SVN_ERR(auto_create_shard(cb->fs, new_rev, batch, subpool));

  /* Get a write handle on the proto revision file.

     ### This "breaks" the transaction by removing the protorev file
     ### but the revision is not yet complete.  If this commit does
     ### not complete for any reason the transaction will be lost. */
  SVN_ERR(get_writable_final_rev(&proto_file, cb->fs, txn_id, new_rev,
                                 batch, subpool));
  SVN_ERR(svn_io_file_get_offset(&initial_offset, proto_file, subpool));
  svn_pool_clear(subpool);

  /* Write out all the node-revisions and directory contents. */
  svn_fs_x__init_txn_root(&root_id, txn_id);
  SVN_ERR(write_final_rev(&new_root_id, proto_file, new_rev, cb->fs, &root_id,
                          initial_offset, directory_ids, cb->reps_to_cache,
                          cb->reps_hash, cb->reps_pool, TRUE, changed_paths,
                          subpool));
  svn_pool_clear(subpool);

  /* Write the changed-path information. */
  SVN_ERR(write_final_changed_path_info(&changed_path_offset, proto_file,
                                        cb->fs, txn_id, changed_paths,
                                        new_rev, subpool));
  svn_pool_clear(subpool);

  /* Append the index data to the rev file. */
  SVN_ERR(svn_fs_x__add_index_data(cb->fs, proto_file,
                      svn_fs_x__path_l2p_proto_index(cb->fs, txn_id, subpool),
                      svn_fs_x__path_p2l_proto_index(cb->fs, txn_id, subpool),
                      new_rev, subpool));
  svn_pool_clear(subpool);

  /* Set the correct permissions. */
  old_rev_filename = svn_fs_x__path_rev_absolute(cb->fs, old_rev, subpool);
  rev_filename = svn_fs_x__path_rev(cb->fs, new_rev, subpool);
  SVN_ERR(svn_io_copy_perms(rev_filename, old_rev_filename, subpool));

  /* Move the revprops file into place. */
  SVN_ERR_ASSERT(! svn_fs_x__is_packed_revprop(cb->fs, new_rev));
  SVN_ERR(write_final_revprop(&revprop_filename, cb->txn, new_rev, batch,
                              subpool, subpool));
  SVN_ERR(svn_io_copy_perms(revprop_filename, old_rev_filename, subpool));
  svn_pool_clear(subpool);

  /* Verify contents (no-op outside DEBUG mode). */
  SVN_ERR(svn_io_file_flush(proto_file, subpool));
  SVN_ERR(verify_as_revision_before_current_plus_plus(cb->fs, new_rev,
                                                      subpool));

  /* Bump 'current'. */
  SVN_ERR(bump_current(cb->fs, new_rev, batch, subpool));

  /* At this point the new revision is committed and globally visible
     so let the caller know it succeeded by giving it the new revision
     number, which fulfills svn_fs_commit_txn() contract.  Any errors
     after this point do not change the fact that a new revision was
     created. */
  *cb->new_rev_p = new_rev;

  ffd->youngest_rev_cache = new_rev;

  /* Make the directory contents already cached for the new revision
   * visible. */
  SVN_ERR(promote_cached_directories(cb->fs, directory_ids, subpool));

  /* Remove this transaction directory. */
  SVN_ERR(svn_fs_x__purge_txn(cb->fs, cb->txn->id, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Add the representations in REPS_TO_CACHE (an array of
 * svn_fs_x__representation_t *) to the rep-cache database of FS. */
static svn_error_t *
write_reps_to_cache(svn_fs_t *fs,
                    const apr_array_header_t *reps_to_cache,
                    apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < reps_to_cache->nelts; i++)
    {
      svn_fs_x__representation_t *rep
        = APR_ARRAY_IDX(reps_to_cache, i, svn_fs_x__representation_t *);

      /* FALSE because we don't care if another parallel commit happened to
       * collide with us.  (Non-parallel collisions will not be detected.) */
      SVN_ERR(svn_fs_x__set_rep_reference(fs, rep, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__commit(svn_revnum_t *new_rev_p,
                 svn_fs_t *fs,
                 svn_fs_txn_t *txn,
                 apr_pool_t *scratch_pool)
{
  commit_baton_t cb;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  cb.new_rev_p = new_rev_p;
  cb.fs = fs;
  cb.txn = txn;

  if (ffd->rep_sharing_allowed)
    {
      cb.reps_to_cache = apr_array_make(scratch_pool, 5,
                                        sizeof(svn_fs_x__representation_t *));
      cb.reps_hash = apr_hash_make(scratch_pool);
      cb.reps_pool = scratch_pool;
    }
  else
    {
      cb.reps_to_cache = NULL;
      cb.reps_hash = NULL;
      cb.reps_pool = NULL;
    }

  SVN_ERR(svn_fs_x__with_write_lock(fs, commit_body, &cb, scratch_pool));

  /* At this point, *NEW_REV_P has been set, so errors below won't affect
     the success of the commit.  (See svn_fs_commit_txn().)  */

  if (ffd->rep_sharing_allowed)
    {
      svn_error_t *err;

      SVN_ERR(svn_fs_x__open_rep_cache(fs, scratch_pool));

      /* Write new entries to the rep-sharing database.
       *
       * We use an sqlite transaction to speed things up;
       * see <http://www.sqlite.org/faq.html#q19>.
       */
      /* ### A commit that touches thousands of files will starve other
             (reader/writer) commits for the duration of the below call.
             Maybe write in batches? */
      SVN_ERR(svn_sqlite__begin_transaction(ffd->rep_cache_db));
      err = write_reps_to_cache(fs, cb.reps_to_cache, scratch_pool);
      err = svn_sqlite__finish_transaction(ffd->rep_cache_db, err);

      if (svn_error_find_cause(err, SVN_ERR_SQLITE_ROLLBACK_FAILED))
        {
          /* Failed rollback means that our db connection is unusable, and
             the only thing we can do is close it.  The connection will be
             reopened during the next operation with rep-cache.db. */
          return svn_error_trace(
              svn_error_compose_create(err,
                                       svn_fs_x__close_rep_cache(fs)));
        }
      else if (err)
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__list_transactions(apr_array_header_t **names_p,
                            svn_fs_t *fs,
                            apr_pool_t *pool)
{
  const char *txn_dir;
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_array_header_t *names;
  apr_size_t ext_len = strlen(PATH_EXT_TXN);

  names = apr_array_make(pool, 1, sizeof(const char *));

  /* Get the transactions directory. */
  txn_dir = svn_fs_x__path_txns_dir(fs, pool);

  /* Now find a listing of this directory. */
  SVN_ERR(svn_io_get_dirents3(&dirents, txn_dir, TRUE, pool, pool));

  /* Loop through all the entries and return anything that ends with '.txn'. */
  for (hi = apr_hash_first(pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      apr_ssize_t klen = apr_hash_this_key_len(hi);
      const char *id;

      /* The name must end with ".txn" to be considered a transaction. */
      if ((apr_size_t) klen <= ext_len
          || (strcmp(name + klen - ext_len, PATH_EXT_TXN)) != 0)
        continue;

      /* Truncate the ".txn" extension and store the ID. */
      id = apr_pstrndup(pool, name, strlen(name) - ext_len);
      APR_ARRAY_PUSH(names, const char *) = id;
    }

  *names_p = names;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__open_txn(svn_fs_txn_t **txn_p,
                   svn_fs_t *fs,
                   const char *name,
                   apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_node_kind_t kind;
  svn_fs_x__transaction_t *local_txn;
  svn_fs_x__txn_id_t txn_id;

  SVN_ERR(svn_fs_x__txn_by_name(&txn_id, name));

  /* First check to see if the directory exists. */
  SVN_ERR(svn_io_check_path(svn_fs_x__path_txn_dir(fs, txn_id, pool),
                            &kind, pool));

  /* Did we find it? */
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_TRANSACTION, NULL,
                             _("No such transaction '%s'"),
                             name);

  txn = apr_pcalloc(pool, sizeof(*txn));
  ftd = apr_pcalloc(pool, sizeof(*ftd));
  ftd->txn_id = txn_id;

  /* Read in the root node of this transaction. */
  txn->id = apr_pstrdup(pool, name);
  txn->fs = fs;

  SVN_ERR(svn_fs_x__get_txn(&local_txn, fs, txn_id, pool));

  txn->base_rev = local_txn->base_rev;

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__txn_proplist(apr_hash_t **table_p,
                       svn_fs_txn_t *txn,
                       apr_pool_t *pool)
{
  SVN_ERR(get_txn_proplist(table_p, txn->fs, svn_fs_x__txn_get_id(txn),
                           pool, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__delete_node_revision(svn_fs_t *fs,
                               const svn_fs_x__id_t *id,
                               apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev;
  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, id, scratch_pool,
                                      scratch_pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_rep
      && svn_fs_x__is_txn(noderev->prop_rep->id.change_set))
    SVN_ERR(svn_io_remove_file2(svn_fs_x__path_txn_node_props(fs, id,
                                                              scratch_pool,
                                                              scratch_pool),
                                FALSE, scratch_pool));

  /* Delete any mutable data representation. */
  if (noderev->data_rep
      && svn_fs_x__is_txn(noderev->data_rep->id.change_set)
      && noderev->kind == svn_node_dir)
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;
      const svn_fs_x__id_t *key = id;

      SVN_ERR(svn_io_remove_file2(
                  svn_fs_x__path_txn_node_children(fs, id, scratch_pool,
                                                   scratch_pool),
                  FALSE, scratch_pool));

      /* remove the corresponding entry from the cache, if such exists */
      SVN_ERR(svn_cache__set(ffd->dir_cache, key, NULL, scratch_pool));
    }

  return svn_io_remove_file2(svn_fs_x__path_txn_node_rev(fs, id,
                                                         scratch_pool,
                                                         scratch_pool),
                             FALSE, scratch_pool);
}



/*** Transactions ***/

svn_error_t *
svn_fs_x__get_base_rev(svn_revnum_t *revnum,
                       svn_fs_t *fs,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__transaction_t *txn;
  SVN_ERR(svn_fs_x__get_txn(&txn, fs, txn_id, scratch_pool));
  *revnum = txn->base_rev;

  return SVN_NO_ERROR;
}


/* Generic transaction operations.  */

svn_error_t *
svn_fs_x__txn_prop(svn_string_t **value_p,
                   svn_fs_txn_t *txn,
                   const char *propname,
                   apr_pool_t *pool)
{
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_x__txn_proplist(&table, txn, pool));

  *value_p = svn_hash_gets(table, propname);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__begin_txn(svn_fs_txn_t **txn_p,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_uint32_t flags,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_string_t date;
  fs_txn_data_t *ftd;
  apr_hash_t *props = apr_hash_make(scratch_pool);

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  SVN_ERR(create_txn(txn_p, fs, rev, result_pool, scratch_pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring(apr_time_now(), scratch_pool);
  date.len = strlen(date.data);

  svn_hash_sets(props, SVN_PROP_REVISION_DATE, &date);

  /* Set temporary txn props that represent the requested 'flags'
     behaviors. */
  if (flags & SVN_FS_TXN_CHECK_OOD)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_OOD,
                  svn_string_create("true", scratch_pool));

  if (flags & SVN_FS_TXN_CHECK_LOCKS)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_LOCKS,
                  svn_string_create("true", scratch_pool));

  if (flags & SVN_FS_TXN_CLIENT_DATE)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CLIENT_DATE,
                  svn_string_create("0", scratch_pool));

  ftd = (*txn_p)->fsap_data;
  SVN_ERR(set_txn_proplist(fs, ftd->txn_id, props, scratch_pool));

  return SVN_NO_ERROR;
}
