/* transaction.c --- transaction-related functions of FSFS
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

#include "fs_fs.h"
#include "index.h"
#include "tree.h"
#include "util.h"
#include "id.h"
#include "low_level.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "lock.h"
#include "rep-cache.h"

#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Return the name of the sha1->rep mapping file in transaction TXN_ID
 * within FS for the given SHA1 checksum.  Use POOL for allocations.
 */
static APR_INLINE const char *
path_txn_sha1(svn_fs_t *fs,
              const svn_fs_fs__id_part_t *txn_id,
              const unsigned char *sha1,
              apr_pool_t *pool)
{
  svn_checksum_t checksum;
  checksum.digest = sha1;
  checksum.kind = svn_checksum_sha1;

  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         svn_checksum_to_cstring(&checksum, pool),
                         pool);
}

static APR_INLINE const char *
path_txn_changes(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_CHANGES, pool);
}

static APR_INLINE const char *
path_txn_props(svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_TXN_PROPS, pool);
}

static APR_INLINE const char *
path_txn_next_ids(svn_fs_t *fs,
                  const svn_fs_fs__id_part_t *txn_id,
                  apr_pool_t *pool)
{
  return svn_dirent_join(svn_fs_fs__path_txn_dir(fs, txn_id, pool),
                         PATH_NEXT_IDS, pool);
}


/* The vtable associated with an open transaction object. */
static txn_vtable_t txn_vtable = {
  svn_fs_fs__commit_txn,
  svn_fs_fs__abort_txn,
  svn_fs_fs__txn_prop,
  svn_fs_fs__txn_proplist,
  svn_fs_fs__change_txn_prop,
  svn_fs_fs__txn_root,
  svn_fs_fs__change_txn_props
};

/* FSFS-specific data being attached to svn_fs_txn_t.
 */
typedef struct fs_txn_data_t
{
  /* Strongly typed representation of the TXN's ID member. */
  svn_fs_fs__id_part_t txn_id;
} fs_txn_data_t;

const svn_fs_fs__id_part_t *
svn_fs_fs__txn_get_id(svn_fs_txn_t *txn)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  return &ftd->txn_id;
}

/* Functions for working with shared transaction data. */

/* Return the transaction object for transaction TXN_ID from the
   transaction list of filesystem FS (which must already be locked via the
   txn_list_lock mutex).  If the transaction does not exist in the list,
   then create a new transaction object and return it (if CREATE_NEW is
   true) or return NULL (otherwise). */
static fs_fs_shared_txn_data_t *
get_shared_txn(svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               svn_boolean_t create_new)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn;

  for (txn = ffsd->txns; txn; txn = txn->next)
    if (svn_fs_fs__id_part_eq(&txn->txn_id, txn_id))
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

  txn->txn_id = *txn_id;
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
free_shared_txn(svn_fs_t *fs, const svn_fs_fs__id_part_t *txn_id)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;
  fs_fs_shared_txn_data_t *txn, *prev = NULL;

  for (txn = ffsd->txns; txn; prev = txn, txn = txn->next)
    if (svn_fs_fs__id_part_eq(&txn->txn_id, txn_id))
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
  fs_fs_data_t *ffd = fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;

  SVN_MUTEX__WITH_LOCK(ffsd->txn_list_lock,
                       body(fs, baton, pool));

  return SVN_NO_ERROR;
}


/* A structure used by unlock_proto_rev() and unlock_proto_rev_body(),
   which see. */
struct unlock_proto_rev_baton
{
  svn_fs_fs__id_part_t txn_id;
  void *lockcookie;
};

/* Callback used in the implementation of unlock_proto_rev(). */
static svn_error_t *
unlock_proto_rev_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const struct unlock_proto_rev_baton *b = baton;
  apr_file_t *lockfile = b->lockcookie;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, &b->txn_id, FALSE);
  apr_status_t apr_err;

  if (!txn)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock unknown transaction '%s'"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));
  if (!txn->being_written)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Can't unlock nonlocked transaction '%s'"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));

  apr_err = apr_file_unlock(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't unlock prototype revision lockfile for transaction '%s'"),
       svn_fs_fs__id_txn_unparse(&b->txn_id, pool));
  apr_err = apr_file_close(lockfile);
  if (apr_err)
    return svn_error_wrap_apr
      (apr_err,
       _("Can't close prototype revision lockfile for transaction '%s'"),
       svn_fs_fs__id_txn_unparse(&b->txn_id, pool));

  txn->being_written = FALSE;

  return SVN_NO_ERROR;
}

/* Unlock the prototype revision file for transaction TXN_ID in filesystem
   FS using cookie LOCKCOOKIE.  The original prototype revision file must
   have been closed _before_ calling this function.

   Perform temporary allocations in POOL. */
static svn_error_t *
unlock_proto_rev(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 void *lockcookie,
                 apr_pool_t *pool)
{
  struct unlock_proto_rev_baton b;

  b.txn_id = *txn_id;
  b.lockcookie = lockcookie;
  return with_txnlist_lock(fs, unlock_proto_rev_body, &b, pool);
}

/* A structure used by get_writable_proto_rev() and
   get_writable_proto_rev_body(), which see. */
struct get_writable_proto_rev_baton
{
  void **lockcookie;
  svn_fs_fs__id_part_t txn_id;
};

/* Callback used in the implementation of get_writable_proto_rev(). */
static svn_error_t *
get_writable_proto_rev_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const struct get_writable_proto_rev_baton *b = baton;
  void **lockcookie = b->lockcookie;
  fs_fs_shared_txn_data_t *txn = get_shared_txn(fs, &b->txn_id, TRUE);

  /* First, ensure that no thread in this process (including this one)
     is currently writing to this transaction's proto-rev file. */
  if (txn->being_written)
    return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                             _("Cannot write to the prototype revision file "
                               "of transaction '%s' because a previous "
                               "representation is currently being written by "
                               "this process"),
                             svn_fs_fs__id_txn_unparse(&b->txn_id, pool));


  /* We know that no thread in this process is writing to the proto-rev
     file, and by extension, that no thread in this process is holding a
     lock on the prototype revision lock file.  It is therefore safe
     for us to attempt to lock this file, to see if any other process
     is holding a lock. */

  {
    apr_file_t *lockfile;
    apr_status_t apr_err;
    const char *lockfile_path
      = svn_fs_fs__path_txn_proto_rev_lock(fs, &b->txn_id, pool);

    /* Open the proto-rev lockfile, creating it if necessary, as it may
       not exist if the transaction dates from before the lockfiles were
       introduced.

       ### We'd also like to use something like svn_io_file_lock2(), but
           that forces us to create a subpool just to be able to unlock
           the file, which seems a waste. */
    SVN_ERR(svn_io_file_open(&lockfile, lockfile_path,
                             APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));

    apr_err = apr_file_lock(lockfile,
                            APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK);
    if (apr_err)
      {
        svn_error_clear(svn_io_file_close(lockfile, pool));

        if (APR_STATUS_IS_EAGAIN(apr_err))
          return svn_error_createf(SVN_ERR_FS_REP_BEING_WRITTEN, NULL,
                                   _("Cannot write to the prototype revision "
                                     "file of transaction '%s' because a "
                                     "previous representation is currently "
                                     "being written by another process"),
                                   svn_fs_fs__id_txn_unparse(&b->txn_id,
                                                             pool));

        return svn_error_wrap_apr(apr_err,
                                  _("Can't get exclusive lock on file '%s'"),
                                  svn_dirent_local_style(lockfile_path, pool));
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

   Perform all allocations in POOL. */
static svn_error_t *
auto_truncate_proto_rev(svn_fs_t *fs,
                        apr_file_t *proto_rev,
                        apr_off_t actual_length,
                        const svn_fs_fs__id_part_t *txn_id,
                        apr_pool_t *pool)
{
  /* Only relevant for newer FSFS formats. */
  if (svn_fs_fs__use_log_addressing(fs))
    {
      /* Determine file range covered by the proto-index so far.  Note that
         we always append to both file, i.e. the last index entry also
         corresponds to the last addition in the rev file. */
      const char *path = svn_fs_fs__path_p2l_proto_index(fs, txn_id, pool);
      apr_file_t *file;
      apr_off_t indexed_length;

      SVN_ERR(svn_fs_fs__p2l_proto_index_open(&file, path, pool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_next_offset(&indexed_length, file,
                                                     pool));
      SVN_ERR(svn_io_file_close(file, pool));

      /* Handle mismatches. */
      if (indexed_length < actual_length)
        SVN_ERR(svn_io_file_trunc(proto_rev, indexed_length, pool));
      else if (indexed_length > actual_length)
        return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                                 NULL,
                                 _("p2l proto index offset %s beyond proto"
                                   "rev file size %s for TXN %s"),
                                   apr_off_t_toa(pool, indexed_length),
                                   apr_off_t_toa(pool, actual_length),
                                   svn_fs_fs__id_txn_unparse(txn_id, pool));
    }

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
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  struct get_writable_proto_rev_baton b;
  svn_error_t *err;
  apr_off_t end_offset = 0;

  b.lockcookie = lockcookie;
  b.txn_id = *txn_id;

  SVN_ERR(with_txnlist_lock(fs, get_writable_proto_rev_body, &b, pool));

  /* Now open the prototype revision file and seek to the end. */
  err = svn_io_file_open(file,
                         svn_fs_fs__path_txn_proto_rev(fs, txn_id, pool),
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
purge_shared_txn_body(svn_fs_t *fs, const void *baton, apr_pool_t *pool)
{
  const svn_fs_fs__id_part_t *txn_id = baton;

  free_shared_txn(fs, txn_id);
  svn_fs_fs__reset_txn_caches(fs);

  return SVN_NO_ERROR;
}

/* Purge the shared data for transaction TXN_ID in filesystem FS.
   Perform all allocations in POOL. */
static svn_error_t *
purge_shared_txn(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_pool_t *pool)
{
  return with_txnlist_lock(fs, purge_shared_txn_body, txn_id, pool);
}


svn_error_t *
svn_fs_fs__put_node_revision(svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             node_revision_t *noderev,
                             svn_boolean_t fresh_txn_root,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *noderev_file;

  noderev->is_fresh_txn_root = fresh_txn_root;

  if (! svn_fs_fs__id_is_txn(id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_fs__id_unparse(id, pool)->data);

  SVN_ERR(svn_io_file_open(&noderev_file,
                           svn_fs_fs__path_txn_node_rev(fs, id, pool),
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  SVN_ERR(svn_fs_fs__write_noderev(svn_stream_from_aprfile2(noderev_file, TRUE,
                                                            pool),
                                   noderev, ffd->format,
                                   svn_fs_fs__fs_supports_mergeinfo(fs),
                                   pool));

  SVN_ERR(svn_io_file_close(noderev_file, pool));

  return SVN_NO_ERROR;
}

/* For the in-transaction NODEREV within FS, write the sha1->rep mapping
 * file in the respective transaction, if rep sharing has been enabled etc.
 * Use SCATCH_POOL for temporary allocations.
 */
static svn_error_t *
store_sha1_rep_mapping(svn_fs_t *fs,
                       node_revision_t *noderev,
                       apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* if rep sharing has been enabled and the noderev has a data rep and
   * its SHA-1 is known, store the rep struct under its SHA1. */
  if (   ffd->rep_sharing_allowed
      && noderev->data_rep
      && noderev->data_rep->has_sha1)
    {
      apr_file_t *rep_file;
      const char *file_name = path_txn_sha1(fs,
                                            &noderev->data_rep->txn_id,
                                            noderev->data_rep->sha1_digest,
                                            scratch_pool);
      svn_stringbuf_t *rep_string
        = svn_fs_fs__unparse_representation(noderev->data_rep,
                                            ffd->format,
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
unparse_dir_entry(svn_fs_dirent_t *dirent,
                  svn_stream_t *stream,
                  apr_pool_t *pool)
{
  apr_size_t to_write;
  svn_string_t *id_str = svn_fs_fs__id_unparse(dirent->id, pool);
  apr_size_t name_len = strlen(dirent->name);

  /* Note that sizeof == len + 1, i.e. accounts for the space between
   * type and ID. */
  apr_size_t type_len = (dirent->kind == svn_node_file)
                      ? sizeof(SVN_FS_FS__KIND_FILE)
                      : sizeof(SVN_FS_FS__KIND_DIR);
  apr_size_t value_len = type_len + id_str->len;

  /* A buffer with sufficient space for 
   * - both string lines
   * - 4 newlines
   * - 2 lines K/V lines containing a number each
   */
  char *buffer = apr_palloc(pool,   name_len + value_len
                                  + 4
                                  + 2 * (2 + SVN_INT64_BUFFER_SIZE));

  /* Now construct the value. */
  char *p = buffer;

  /* The "K length(name)\n" line. */
  p[0] = 'K';
  p[1] = ' ';
  p += 2;
  p += svn__i64toa(p, name_len);
  *(p++) = '\n';

  /* The line with the key, i.e. dir entry name. */
  memcpy(p, dirent->name, name_len);
  p += name_len;
  *(p++) = '\n';

  /* The "V length(type+id)\n" line. */
  p[0] = 'V';
  p[1] = ' ';
  p += 2;
  p += svn__i64toa(p, value_len);
  *(p++) = '\n';

  /* The line with the type and ID. */
  memcpy(p,
         (dirent->kind == svn_node_file) ? SVN_FS_FS__KIND_FILE
                                         : SVN_FS_FS__KIND_DIR,
         type_len - 1);
  p += type_len - 1;
  *(p++) = ' ';
  memcpy(p, id_str->data, id_str->len);
  p+=id_str->len;
  *(p++) = '\n';

  /* Add the entry to the output stream. */
  to_write = p - buffer;
  SVN_ERR(svn_stream_write(stream, buffer, &to_write));
  return SVN_NO_ERROR;
}

/* Write the directory given as array of dirent structs in ENTRIES to STREAM.
   Perform temporary allocations in POOL. */
static svn_error_t *
unparse_dir_entries(apr_array_header_t *entries,
                    svn_stream_t *stream,
                    apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_dirent_t *dirent;

      svn_pool_clear(iterpool);
      dirent = APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *);
      SVN_ERR(unparse_dir_entry(dirent, stream, iterpool));
    }

  SVN_ERR(svn_stream_printf(stream, pool, "%s\n", SVN_HASH_TERMINATOR));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Return a deep copy of SOURCE and allocate it in RESULT_POOL.
 */
static svn_fs_path_change2_t *
path_change_dup(const svn_fs_path_change2_t *source,
                apr_pool_t *result_pool)
{
  svn_fs_path_change2_t *result = apr_pmemdup(result_pool, source,
                                              sizeof(*source));
  result->node_rev_id = svn_fs_fs__id_copy(source->node_rev_id, result_pool);
  if (source->copyfrom_path)
    result->copyfrom_path = apr_pstrdup(result_pool, source->copyfrom_path);

  return result;
}

/* Merge the internal-use-only CHANGE into a hash of public-FS
   svn_fs_path_change2_t CHANGED_PATHS, collapsing multiple changes into a
   single summarical (is that real word?) change per path.  DELETIONS is
   also a path->svn_fs_path_change2_t hash and contains all the deletions
   that got turned into a replacement. */
static svn_error_t *
fold_change(apr_hash_t *changed_paths,
            apr_hash_t *deletions,
            const change_t *change)
{
  apr_pool_t *pool = apr_hash_pool_get(changed_paths);
  svn_fs_path_change2_t *old_change, *new_change;
  const svn_string_t *path = &change->path;
  const svn_fs_path_change2_t *info = &change->info;

  if ((old_change = apr_hash_get(changed_paths, path->data, path->len)))
    {
      /* This path already exists in the hash, so we have to merge
         this change into the already existing one. */

      /* Sanity check:  only allow NULL node revision ID in the
         `reset' case. */
      if ((! info->node_rev_id)
           && (info->change_kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Missing required node revision ID"));

      /* Sanity check: we should be talking about the same node
         revision ID as our last change except where the last change
         was a deletion. */
      if (info->node_rev_id
          && (! svn_fs_fs__id_eq(old_change->node_rev_id, info->node_rev_id))
          && (old_change->change_kind != svn_fs_path_change_delete))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: new node revision ID "
             "without delete"));

      /* Sanity check: an add, replacement, or reset must be the first
         thing to follow a deletion. */
      if ((old_change->change_kind == svn_fs_path_change_delete)
          && (! ((info->change_kind == svn_fs_path_change_replace)
                 || (info->change_kind == svn_fs_path_change_reset)
                 || (info->change_kind == svn_fs_path_change_add))))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: non-add change on deleted path"));

      /* Sanity check: an add can't follow anything except
         a delete or reset.  */
      if ((info->change_kind == svn_fs_path_change_add)
          && (old_change->change_kind != svn_fs_path_change_delete)
          && (old_change->change_kind != svn_fs_path_change_reset))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Invalid change ordering: add change on preexisting path"));

      /* Now, merge that change in. */
      switch (info->change_kind)
        {
        case svn_fs_path_change_reset:
          /* A reset here will simply remove the path change from the
             hash. */
          apr_hash_set(changed_paths, path->data, path->len, NULL);
          break;

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
              new_change = path_change_dup(info, pool);
              apr_hash_set(changed_paths, path->data, path->len, new_change);
            }
          break;

        case svn_fs_path_change_add:
        case svn_fs_path_change_replace:
          /* An add at this point must be following a previous delete,
             so treat it just like a replace.  Remember the original
             deletion such that we are able to delete this path again
             (the replacement may have changed node kind and id). */
          new_change = path_change_dup(info, pool);
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
          if (info->text_mod)
            old_change->text_mod = TRUE;
          if (info->prop_mod)
            old_change->prop_mod = TRUE;
          if (info->mergeinfo_mod == svn_tristate_true)
            old_change->mergeinfo_mod = svn_tristate_true;
          break;
        }
    }
  else
    {
      /* Add this path.  The API makes no guarantees that this (new) key
         will not be retained.  Thus, we copy the key into the target pool
         to ensure a proper lifetime.  */
      apr_hash_set(changed_paths,
                   apr_pstrmemdup(pool, path->data, path->len), path->len,
                   path_change_dup(info, pool));
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

/* An implementation of svn_fs_fs__change_receiver_t.
   Examine all the changed path entries in CHANGES and store them in
   *CHANGED_PATHS.  Folding is done to remove redundant or unnecessary
   data. Do all allocations in POOL. */
static svn_error_t *
process_changes(void *baton_p,
                change_t *change,
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

  if ((change->info.change_kind == svn_fs_path_change_delete)
       || (change->info.change_kind == svn_fs_path_change_replace))
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
          svn_fs_path_change2_t *old_change;
          apr_hash_this(hi, &path, &klen, (void**)&old_change);

          /* If we come across a child of our path, remove it.
             Call svn_fspath__skip_ancestor only if there is a chance that
             this is actually a sub-path.
           */
          if (klen >= min_child_len)
            {
              const char *child;

              child = svn_fspath__skip_ancestor(change->path.data, path);
              if (child && child[0] != '\0')
                {
                  apr_hash_set(baton->changed_paths, path, klen, NULL);
                }
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__txn_changes_fetch(apr_hash_t **changed_paths_p,
                             svn_fs_t *fs,
                             const svn_fs_fs__id_part_t *txn_id,
                             apr_pool_t *pool)
{
  apr_file_t *file;
  apr_hash_t *changed_paths = apr_hash_make(pool);
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  process_changes_baton_t baton;

  baton.changed_paths = changed_paths;
  baton.deletions = apr_hash_make(scratch_pool);

  SVN_ERR(svn_io_file_open(&file,
                           path_txn_changes(fs, txn_id, scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  SVN_ERR(svn_fs_fs__read_changes_incrementally(
                                  svn_stream_from_aprfile2(file, TRUE,
                                                           scratch_pool),
                                  process_changes, &baton,
                                  scratch_pool));
  svn_pool_destroy(scratch_pool);

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__paths_changed(apr_hash_t **changed_paths_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  apr_hash_t *changed_paths = svn_hash__make(pool);
  svn_fs_fs__changes_context_t *context;

  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Fetch all data block-by-block. */
  SVN_ERR(svn_fs_fs__create_changes_context(&context, fs, rev, pool));
  while (!context->eol)
    {
      apr_array_header_t *changes;
      int i;

      svn_pool_clear(iterpool);

      /* Be sure to allocate the changes in the result POOL, even though
         we don't need the array itself afterwards.  Copying the entries
         from a temp pool to the result POOL would be expensive and saves
         use less then 10% memory. */
      SVN_ERR(svn_fs_fs__get_changes(&changes, context, pool, iterpool));

      for (i = 0; i < changes->nelts; ++i)
        {
          change_t *change = APR_ARRAY_IDX(changes, i, change_t *);
          apr_hash_set(changed_paths, change->path.data, change->path.len,
                       &change->info);
        }
    }

  svn_pool_destroy(iterpool);

  *changed_paths_p = changed_paths;

  return SVN_NO_ERROR;
}

/* Copy a revision node-rev SRC into the current transaction TXN_ID in
   the filesystem FS.  This is only used to create the root of a transaction.
   Allocations are from POOL.  */
static svn_error_t *
create_new_txn_noderev_from_rev(svn_fs_t *fs,
                                const svn_fs_fs__id_part_t *txn_id,
                                svn_fs_id_t *src,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;
  const svn_fs_fs__id_part_t *node_id, *copy_id;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, src, pool, pool));

  if (svn_fs_fs__id_is_txn(noderev->id))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Copying from transactions not allowed"));

  noderev->predecessor_id = noderev->id;
  noderev->predecessor_count++;
  noderev->copyfrom_path = NULL;
  noderev->copyfrom_rev = SVN_INVALID_REVNUM;

  /* For the transaction root, the copyroot never changes. */

  node_id = svn_fs_fs__id_node_id(noderev->id);
  copy_id = svn_fs_fs__id_copy_id(noderev->id);
  noderev->id = svn_fs_fs__id_txn_create(node_id, copy_id, txn_id, pool);

  return svn_fs_fs__put_node_revision(fs, noderev->id, noderev, TRUE, pool);
}

/* A structure used by get_and_increment_txn_key_body(). */
struct get_and_increment_txn_key_baton {
  svn_fs_t *fs;
  apr_uint64_t txn_number;
  apr_pool_t *pool;
};

/* Callback used in the implementation of create_txn_dir().  This gets
   the current base 36 value in PATH_TXN_CURRENT and increments it.
   It returns the original value by the baton. */
static svn_error_t *
get_and_increment_txn_key_body(void *baton, apr_pool_t *pool)
{
  struct get_and_increment_txn_key_baton *cb = baton;
  fs_fs_data_t *ffd = cb->fs->fsap_data;
  const char *txn_current_filename
    = svn_fs_fs__path_txn_current(cb->fs, pool);
  char new_id_str[SVN_INT64_BUFFER_SIZE + 1]; /* add space for a newline */
  apr_size_t line_length;

  svn_stringbuf_t *buf;
  SVN_ERR(svn_fs_fs__read_content(&buf, txn_current_filename, cb->pool));

  /* assign the current txn counter value to our result */
  cb->txn_number = svn__base36toui64(NULL, buf->data);

  /* remove trailing newlines */
  line_length = svn__ui64tobase36(new_id_str, cb->txn_number+1);
  new_id_str[line_length] = '\n';

  /* Increment the key and add a trailing \n to the string so the
     txn-current file has a newline in it. */
  SVN_ERR(svn_io_write_atomic2(txn_current_filename, new_id_str,
                               line_length + 1,
                               txn_current_filename /* copy_perms path */,
                               ffd->flush_to_disk, pool));

  return SVN_NO_ERROR;
}

/* Create a unique directory for a transaction in FS based on revision REV.
   Return the ID for this transaction in *ID_P and *TXN_ID.  Use a sequence
   value in the transaction ID to prevent reuse of transaction IDs. */
static svn_error_t *
create_txn_dir(const char **id_p,
               svn_fs_fs__id_part_t *txn_id,
               svn_fs_t *fs,
               svn_revnum_t rev,
               apr_pool_t *pool)
{
  struct get_and_increment_txn_key_baton cb;
  const char *txn_dir;

  /* Get the current transaction sequence value, which is a base-36
     number, from the txn-current file, and write an
     incremented value back out to the file.  Place the revision
     number the transaction is based off into the transaction id. */
  cb.pool = pool;
  cb.fs = fs;
  SVN_ERR(svn_fs_fs__with_txn_current_lock(fs,
                                           get_and_increment_txn_key_body,
                                           &cb,
                                           pool));
  txn_id->revision = rev;
  txn_id->number = cb.txn_number;

  *id_p = svn_fs_fs__id_txn_unparse(txn_id, pool);
  txn_dir = svn_fs_fs__path_txn_dir(fs, txn_id, pool);

  return svn_io_dir_make(txn_dir, APR_OS_DEFAULT, pool);
}

/* Create a unique directory for a transaction in FS based on revision
   REV.  Return the ID for this transaction in *ID_P and *TXN_ID.  This
   implementation is used in svn 1.4 and earlier repositories and is
   kept in 1.5 and greater to support the --pre-1.4-compatible and
   --pre-1.5-compatible repository creation options.  Reused
   transaction IDs are possible with this implementation. */
static svn_error_t *
create_txn_dir_pre_1_5(const char **id_p,
                       svn_fs_fs__id_part_t *txn_id,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_pool_t *pool)
{
  unsigned int i;
  apr_pool_t *subpool;
  const char *unique_path, *prefix;

  /* Try to create directories named "<txndir>/<rev>-<uniqueifier>.txn". */
  prefix = svn_dirent_join(svn_fs_fs__path_txns_dir(fs, pool),
                           apr_psprintf(pool, "%ld", rev), pool);

  subpool = svn_pool_create(pool);
  for (i = 1; i <= 99999; i++)
    {
      svn_error_t *err;

      svn_pool_clear(subpool);
      unique_path = apr_psprintf(subpool, "%s-%u" PATH_EXT_TXN, prefix, i);
      err = svn_io_dir_make(unique_path, APR_OS_DEFAULT, subpool);
      if (! err)
        {
          /* We succeeded.  Return the basename minus the ".txn" extension. */
          const char *name = svn_dirent_basename(unique_path, subpool);
          *id_p = apr_pstrndup(pool, name,
                               strlen(name) - strlen(PATH_EXT_TXN));
          SVN_ERR(svn_fs_fs__id_txn_parse(txn_id, *id_p));
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      if (! APR_STATUS_IS_EEXIST(err->apr_err))
        return svn_error_trace(err);
      svn_error_clear(err);
    }

  return svn_error_createf(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
                           NULL,
                           _("Unable to create transaction directory "
                             "in '%s' for revision %ld"),
                           svn_dirent_local_style(fs->path, pool),
                           rev);
}

svn_error_t *
svn_fs_fs__create_txn(svn_fs_txn_t **txn_p,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  ftd = apr_pcalloc(pool, sizeof(*ftd));

  /* Get the txn_id. */
  if (ffd->format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    SVN_ERR(create_txn_dir(&txn->id, &ftd->txn_id, fs, rev, pool));
  else
    SVN_ERR(create_txn_dir_pre_1_5(&txn->id, &ftd->txn_id, fs, rev, pool));

  txn->fs = fs;
  txn->base_rev = rev;

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  /* Create a new root node for this transaction. */
  SVN_ERR(svn_fs_fs__rev_get_root(&root_id, fs, rev, pool, pool));
  SVN_ERR(create_new_txn_noderev_from_rev(fs, &ftd->txn_id, root_id, pool));

  /* Create an empty rev file. */
  SVN_ERR(svn_io_file_create_empty(
                    svn_fs_fs__path_txn_proto_rev(fs, &ftd->txn_id, pool),
                    pool));

  /* Create an empty rev-lock file. */
  SVN_ERR(svn_io_file_create_empty(
               svn_fs_fs__path_txn_proto_rev_lock(fs, &ftd->txn_id, pool),
               pool));

  /* Create an empty changes file. */
  SVN_ERR(svn_io_file_create_empty(path_txn_changes(fs, &ftd->txn_id, pool),
                                   pool));

  /* Create the next-ids file. */
  return svn_io_file_create(path_txn_next_ids(fs, &ftd->txn_id, pool),
                            "0 0\n", pool);
}

/* Store the property list for transaction TXN_ID in PROPLIST.
   Perform temporary allocations in POOL. */
static svn_error_t *
get_txn_proplist(apr_hash_t *proplist,
                 svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_pool_t *pool)
{
  svn_stream_t *stream;
  svn_error_t *err;

  /* Check for issue #3696. (When we find and fix the cause, we can change
   * this to an assertion.) */
  if (!txn_id || !svn_fs_fs__id_txn_used(txn_id))
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Internal error: a null transaction id was "
                              "passed to get_txn_proplist()"));

  /* Open the transaction properties file. */
  SVN_ERR(svn_stream_open_readonly(&stream, path_txn_props(fs, txn_id, pool),
                                   pool, pool));

  /* Read in the property list. */
  err = svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool);
  if (err)
    {
      err = svn_error_compose_create(err, svn_stream_close(stream));
      return svn_error_quick_wrapf(err,
               _("malformed property list in transaction '%s'"),
               path_txn_props(fs, txn_id, pool));
    }

  return svn_stream_close(stream);
}

/* Save the property list PROPS as the revprops for transaction TXN_ID
   in FS.  Perform temporary allocations in POOL. */
static svn_error_t *
set_txn_proplist(svn_fs_t *fs,
                 const svn_fs_fs__id_part_t *txn_id,
                 apr_hash_t *props,
                 apr_pool_t *pool)
{
  svn_stream_t *tmp_stream;
  const char *tmp_path;
  const char *final_path = path_txn_props(fs, txn_id, pool);

  /* Write the new contents into a temporary file. */
  SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_path,
                                 svn_dirent_dirname(final_path, pool),
                                 svn_io_file_del_none,
                                 pool, pool));

  /* Replace the old file with the new one. */
  SVN_ERR(svn_hash_write2(props, tmp_stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(tmp_stream));

  SVN_ERR(svn_io_file_rename2(tmp_path, final_path, FALSE, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__change_txn_prop(svn_fs_txn_t *txn,
                           const char *name,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  apr_array_header_t *props = apr_array_make(pool, 1, sizeof(svn_prop_t));
  svn_prop_t prop;

  prop.name = name;
  prop.value = value;
  APR_ARRAY_PUSH(props, svn_prop_t) = prop;

  return svn_fs_fs__change_txn_props(txn, props, pool);
}

svn_error_t *
svn_fs_fs__change_txn_props(svn_fs_txn_t *txn,
                            const apr_array_header_t *props,
                            apr_pool_t *pool)
{
  fs_txn_data_t *ftd = txn->fsap_data;
  apr_hash_t *txn_prop = apr_hash_make(pool);
  int i;
  svn_error_t *err;

  err = get_txn_proplist(txn_prop, txn->fs, &ftd->txn_id, pool);
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
                      svn_string_create("1", pool));

      svn_hash_sets(txn_prop, prop->name, prop->value);
    }

  /* Create a new version of the file and write out the new props. */
  /* Open the transaction properties file. */
  SVN_ERR(set_txn_proplist(txn->fs, &ftd->txn_id, txn_prop, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_txn(transaction_t **txn_p,
                   svn_fs_t *fs,
                   const svn_fs_fs__id_part_t *txn_id,
                   apr_pool_t *pool)
{
  transaction_t *txn;
  node_revision_t *noderev;
  svn_fs_id_t *root_id;

  txn = apr_pcalloc(pool, sizeof(*txn));
  root_id = svn_fs_fs__id_txn_create_root(txn_id, pool);

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, root_id, pool, pool));

  txn->root_id = svn_fs_fs__id_copy(noderev->id, pool);
  txn->base_id = svn_fs_fs__id_copy(noderev->predecessor_id, pool);
  txn->copies = NULL;

  *txn_p = txn;

  return SVN_NO_ERROR;
}

/* Write out the currently available next node_id NODE_ID and copy_id
   COPY_ID for transaction TXN_ID in filesystem FS.  The next node-id is
   used both for creating new unique nodes for the given transaction, as
   well as uniquifying representations.  Perform temporary allocations in
   POOL. */
static svn_error_t *
write_next_ids(svn_fs_t *fs,
               const svn_fs_fs__id_part_t *txn_id,
               apr_uint64_t node_id,
               apr_uint64_t copy_id,
               apr_pool_t *pool)
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
                           path_txn_next_ids(fs, txn_id, pool),
                           APR_WRITE | APR_TRUNCATE,
                           APR_OS_DEFAULT, pool));
  SVN_ERR(svn_io_file_write_full(file, buffer, p - buffer, NULL, pool));
  return svn_io_file_close(file, pool);
}

/* Find out what the next unique node-id and copy-id are for
   transaction TXN_ID in filesystem FS.  Store the results in *NODE_ID
   and *COPY_ID.  The next node-id is used both for creating new unique
   nodes for the given transaction, as well as uniquifying representations.
   Perform all allocations in POOL. */
static svn_error_t *
read_next_ids(apr_uint64_t *node_id,
              apr_uint64_t *copy_id,
              svn_fs_t *fs,
              const svn_fs_fs__id_part_t *txn_id,
              apr_pool_t *pool)
{
  svn_stringbuf_t *buf;
  const char *str;
  SVN_ERR(svn_fs_fs__read_content(&buf,
                                  path_txn_next_ids(fs, txn_id, pool),
                                  pool));

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
   not necessarily be sequential.  Perform all allocations in POOL. */
static svn_error_t *
get_new_txn_node_id(svn_fs_fs__id_part_t *node_id_p,
                    svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    apr_pool_t *pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, pool));

  node_id_p->revision = SVN_INVALID_REVNUM;
  node_id_p->number = node_id;

  SVN_ERR(write_next_ids(fs, txn_id, ++node_id, copy_id, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__reserve_copy_id(svn_fs_fs__id_part_t *copy_id_p,
                           svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *txn_id,
                           apr_pool_t *pool)
{
  apr_uint64_t node_id, copy_id;

  /* First read in the current next-ids file. */
  SVN_ERR(read_next_ids(&node_id, &copy_id, fs, txn_id, pool));

  /* this is an in-txn ID now */
  copy_id_p->revision = SVN_INVALID_REVNUM;
  copy_id_p->number = copy_id;

  /* Update the ID counter file */
  SVN_ERR(write_next_ids(fs, txn_id, node_id, ++copy_id, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create_node(const svn_fs_id_t **id_p,
                       svn_fs_t *fs,
                       node_revision_t *noderev,
                       const svn_fs_fs__id_part_t *copy_id,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  svn_fs_fs__id_part_t node_id;
  const svn_fs_id_t *id;

  /* Get a new node-id for this node. */
  SVN_ERR(get_new_txn_node_id(&node_id, fs, txn_id, pool));

  id = svn_fs_fs__id_txn_create(&node_id, copy_id, txn_id, pool);

  noderev->id = id;

  SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, FALSE, pool));

  *id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__purge_txn(svn_fs_t *fs,
                     const char *txn_id_str,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_fs__id_part_t txn_id;
  SVN_ERR(svn_fs_fs__id_txn_parse(&txn_id, txn_id_str));

  /* Remove the shared transaction object associated with this transaction. */
  SVN_ERR(purge_shared_txn(fs, &txn_id, pool));
  /* Remove the directory associated with this transaction. */
  SVN_ERR(svn_io_remove_dir2(svn_fs_fs__path_txn_dir(fs, &txn_id, pool),
                             FALSE, NULL, NULL, pool));
  if (ffd->format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    {
      /* Delete protorev and its lock, which aren't in the txn
         directory.  It's OK if they don't exist (for example, if this
         is post-commit and the proto-rev has been moved into
         place). */
      SVN_ERR(svn_io_remove_file2(
                  svn_fs_fs__path_txn_proto_rev(fs, &txn_id, pool),
                  TRUE, pool));
      SVN_ERR(svn_io_remove_file2(
                  svn_fs_fs__path_txn_proto_rev_lock(fs, &txn_id, pool),
                  TRUE, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__abort_txn(svn_fs_txn_t *txn,
                     apr_pool_t *pool)
{
  SVN_ERR(svn_fs__check_fs(txn->fs, TRUE));

  /* Now, purge the transaction. */
  SVN_ERR_W(svn_fs_fs__purge_txn(txn->fs, txn->id, pool),
            apr_psprintf(pool, _("Transaction '%s' cleanup failed"),
                         txn->id));

  return SVN_NO_ERROR;
}

/* Assign the UNIQUIFIER member of REP based on the current state of TXN_ID
 * in FS.  Allocate the uniquifier in POOL.
 */
static svn_error_t *
set_uniquifier(svn_fs_t *fs,
               representation_t *rep,
               apr_pool_t *pool)
{
  svn_fs_fs__id_part_t temp;
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    {
      SVN_ERR(get_new_txn_node_id(&temp, fs, &rep->txn_id, pool));
      rep->uniquifier.noderev_txn_id = rep->txn_id;
      rep->uniquifier.number = temp.number;
    }

  return SVN_NO_ERROR;
}

/* Return TRUE if the TXN_ID member of REP is in use.
 */
static svn_boolean_t
is_txn_rep(const representation_t *rep)
{
  return svn_fs_fs__id_txn_used(&rep->txn_id);
}

/* Mark the TXN_ID member of REP as "unused".
 */
static void
reset_txn_in_rep(representation_t *rep)
{
  svn_fs_fs__id_txn_reset(&rep->txn_id);
}

svn_error_t *
svn_fs_fs__set_entry(svn_fs_t *fs,
                     const svn_fs_fs__id_part_t *txn_id,
                     node_revision_t *parent_noderev,
                     const char *name,
                     const svn_fs_id_t *id,
                     svn_node_kind_t kind,
                     apr_pool_t *pool)
{
  representation_t *rep = parent_noderev->data_rep;
  const char *filename
    = svn_fs_fs__path_txn_node_children(fs, parent_noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;
  svn_filesize_t filesize;
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_pool_t *subpool = svn_pool_create(pool);

  if (!rep || !is_txn_rep(rep))
    {
      apr_array_header_t *entries;

      /* Before we can modify the directory, we need to dump its old
         contents into a mutable representation file. */
      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, parent_noderev,
                                          subpool, subpool));
      SVN_ERR(svn_io_file_open(&file, filename,
                               APR_WRITE | APR_CREATE | APR_BUFFERED,
                               APR_OS_DEFAULT, pool));
      out = svn_stream_from_aprfile2(file, TRUE, pool);
      SVN_ERR(unparse_dir_entries(entries, out, subpool));

      /* Mark the node-rev's data rep as mutable. */
      rep = apr_pcalloc(pool, sizeof(*rep));
      rep->revision = SVN_INVALID_REVNUM;
      rep->txn_id = *txn_id;
      SVN_ERR(set_uniquifier(fs, rep, pool));
      parent_noderev->data_rep = rep;
      SVN_ERR(svn_fs_fs__put_node_revision(fs, parent_noderev->id,
                                           parent_noderev, FALSE, pool));

      /* Immediately populate the txn dir cache to avoid re-reading
       * the file we just wrote. */
      if (ffd->txn_dir_cache)
        {
          const char *key
            = svn_fs_fs__id_unparse(parent_noderev->id, subpool)->data;
          svn_fs_fs__dir_data_t dir_data;

          /* Flush APR buffers. */
          SVN_ERR(svn_io_file_flush(file, subpool));

          /* Obtain final file size to update txn_dir_cache. */
          SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

          /* Store in the cache. */
          dir_data.entries = entries;
          dir_data.txn_filesize = filesize;
          SVN_ERR(svn_cache__set(ffd->txn_dir_cache, key, &dir_data,
                                 subpool));
        }

      svn_pool_clear(subpool);
    }
  else
    {
      /* The directory rep is already mutable, so just open it for append. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_WRITE | APR_APPEND,
                               APR_OS_DEFAULT, subpool));
      out = svn_stream_from_aprfile2(file, TRUE, subpool);

      /* If the cache contents is stale, drop it.
       *
       * Note that the directory file is append-only, i.e. if the size
       * did not change, the contents didn't either. */
      if (ffd->txn_dir_cache)
        {
          const char *key
            = svn_fs_fs__id_unparse(parent_noderev->id, subpool)->data;
          svn_boolean_t found;
          svn_filesize_t cached_filesize;

          /* Get the file size that corresponds to the cached contents
           * (if any). */
          SVN_ERR(svn_cache__get_partial((void **)&cached_filesize, &found,
                                         ffd->txn_dir_cache, key,
                                         svn_fs_fs__extract_dir_filesize,
                                         NULL, subpool));

          /* File size info still matches?
           * If not, we need to drop the cache entry. */
          if (found)
            {
              SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

              if (cached_filesize != filesize)
                SVN_ERR(svn_cache__set(ffd->txn_dir_cache, key, NULL,
                                       subpool));
            }
        }
    }

  /* Append an incremental hash entry for the entry change. */
  if (id)
    {
      svn_fs_dirent_t entry;
      entry.name = name;
      entry.id = id;
      entry.kind = kind;

      SVN_ERR(unparse_dir_entry(&entry, out, subpool));
    }
  else
    {
      SVN_ERR(svn_stream_printf(out, subpool, "D %" APR_SIZE_T_FMT "\n%s\n",
                                strlen(name), name));
    }

  /* Flush APR buffers. */
  SVN_ERR(svn_io_file_flush(file, subpool));

  /* Obtain final file size to update txn_dir_cache. */
  SVN_ERR(svn_io_file_size_get(&filesize, file, subpool));

  /* Close file. */
  SVN_ERR(svn_io_file_close(file, subpool));
  svn_pool_clear(subpool);

  /* if we have a directory cache for this transaction, update it */
  if (ffd->txn_dir_cache)
    {
      /* build parameters: name, new entry, new file size  */
      const char *key =
          svn_fs_fs__id_unparse(parent_noderev->id, subpool)->data;
      replace_baton_t baton;

      baton.name = name;
      baton.new_entry = NULL;
      baton.txn_filesize = filesize;

      if (id)
        {
          baton.new_entry = apr_pcalloc(subpool, sizeof(*baton.new_entry));
          baton.new_entry->name = name;
          baton.new_entry->kind = kind;
          baton.new_entry->id = id;
        }

      /* actually update the cached directory (if cached) */
      SVN_ERR(svn_cache__set_partial(ffd->txn_dir_cache, key,
                                     svn_fs_fs__replace_dir_entry, &baton,
                                     subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__add_change(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      const char *path,
                      const svn_fs_id_t *id,
                      svn_fs_path_change_kind_t change_kind,
                      svn_boolean_t text_mod,
                      svn_boolean_t prop_mod,
                      svn_boolean_t mergeinfo_mod,
                      svn_node_kind_t node_kind,
                      svn_revnum_t copyfrom_rev,
                      const char *copyfrom_path,
                      apr_pool_t *pool)
{
  apr_file_t *file;
  svn_fs_path_change2_t *change;
  apr_hash_t *changes = apr_hash_make(pool);

  /* Not using APR_BUFFERED to append change in one atomic write operation. */
  SVN_ERR(svn_io_file_open(&file, path_txn_changes(fs, txn_id, pool),
                           APR_APPEND | APR_WRITE | APR_CREATE,
                           APR_OS_DEFAULT, pool));

  change = svn_fs__path_change_create_internal(id, change_kind, pool);
  change->text_mod = text_mod;
  change->prop_mod = prop_mod;
  change->mergeinfo_mod = mergeinfo_mod
                        ? svn_tristate_true
                        : svn_tristate_false;
  change->node_kind = node_kind;
  change->copyfrom_known = TRUE;
  change->copyfrom_rev = copyfrom_rev;
  if (copyfrom_path)
    change->copyfrom_path = apr_pstrdup(pool, copyfrom_path);

  svn_hash_sets(changes, path, change);
  SVN_ERR(svn_fs_fs__write_changes(svn_stream_from_aprfile2(file, TRUE, pool),
                                   fs, changes, FALSE, pool));

  return svn_io_file_close(file, pool);
}

/* Store the (ITEM_INDEX, OFFSET) pair in the txn's log-to-phys proto
 * index file.
 * Use POOL for allocations.
 * This function assumes that transaction TXN_ID in FS uses logical
 * addressing.
 */
static svn_error_t *
store_l2p_index_entry(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      apr_off_t offset,
                      apr_uint64_t item_index,
                      apr_pool_t *pool)
{
  const char *path;
  apr_file_t *file;

  SVN_ERR_ASSERT(svn_fs_fs__use_log_addressing(fs));

  path = svn_fs_fs__path_l2p_proto_index(fs, txn_id, pool);
  SVN_ERR(svn_fs_fs__l2p_proto_index_open(&file, path, pool));
  SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(file, offset,
                                               item_index, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* Store ENTRY in the phys-to-log proto index file of transaction TXN_ID.
 * Use POOL for allocations.
 * This function assumes that transaction TXN_ID in FS uses logical
 * addressing.
 */
static svn_error_t *
store_p2l_index_entry(svn_fs_t *fs,
                      const svn_fs_fs__id_part_t *txn_id,
                      const svn_fs_fs__p2l_entry_t *entry,
                      apr_pool_t *pool)
{
  const char *path;
  apr_file_t *file;

  SVN_ERR_ASSERT(svn_fs_fs__use_log_addressing(fs));

  path = svn_fs_fs__path_p2l_proto_index(fs, txn_id, pool);
  SVN_ERR(svn_fs_fs__p2l_proto_index_open(&file, path, pool));
  SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(file, entry, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* Allocate an item index for the given MY_OFFSET in the transaction TXN_ID
 * of file system FS and return it in *ITEM_INDEX.  For old formats, it
 * will simply return the offset as item index; in new formats, it will
 * increment the txn's item index counter file and store the mapping in
 * the proto index file.  Use POOL for allocations.
 */
static svn_error_t *
allocate_item_index(apr_uint64_t *item_index,
                    svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    apr_off_t my_offset,
                    apr_pool_t *pool)
{
  if (svn_fs_fs__use_log_addressing(fs))
    {
      apr_file_t *file;
      char buffer[SVN_INT64_BUFFER_SIZE] = { 0 };
      svn_boolean_t eof = FALSE;
      apr_size_t to_write;
      apr_size_t bytes_read;
      apr_off_t offset = 0;

      /* read number, increment it and write it back to disk */
      SVN_ERR(svn_io_file_open(&file,
                         svn_fs_fs__path_txn_item_index(fs, txn_id, pool),
                         APR_READ | APR_WRITE | APR_CREATE,
                         APR_OS_DEFAULT, pool));
      SVN_ERR(svn_io_file_read_full2(file, buffer, sizeof(buffer)-1,
                                     &bytes_read, &eof, pool));

      /* Item index file should be shorter than SVN_INT64_BUFFER_SIZE,
         otherwise we truncate data. */
      if (!eof)
          return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                  _("Unexpected itemidx file length"));
      else if (bytes_read)
        SVN_ERR(svn_cstring_atoui64(item_index, buffer));
      else
        *item_index = SVN_FS_FS__ITEM_INDEX_FIRST_USER;

      to_write = svn__ui64toa(buffer, *item_index + 1);
      SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, pool));
      SVN_ERR(svn_io_file_write_full(file, buffer, to_write, NULL, pool));
      SVN_ERR(svn_io_file_close(file, pool));

      /* write log-to-phys index */
      SVN_ERR(store_l2p_index_entry(fs, txn_id, my_offset, *item_index, pool));
    }
  else
    {
      *item_index = (apr_uint64_t)my_offset;
    }

  return SVN_NO_ERROR;
}

/* Baton used by fnv1a_write_handler to calculate the FNV checksum
 * before passing the data on to the INNER_STREAM.
 */
typedef struct fnv1a_stream_baton_t
{
  svn_stream_t *inner_stream;
  svn_checksum_ctx_t *context;
} fnv1a_stream_baton_t;

/* Implement svn_write_fn_t.
 * Update checksum and pass data on to inner stream.
 */
static svn_error_t *
fnv1a_write_handler(void *baton,
                    const char *data,
                    apr_size_t *len)
{
  fnv1a_stream_baton_t *b = baton;

  SVN_ERR(svn_checksum_update(b->context, data, *len));
  SVN_ERR(svn_stream_write(b->inner_stream, data, len));

  return SVN_NO_ERROR;
}

/* Return a stream that calculates a FNV checksum in *CONTEXT
 * over all data written to the stream and passes that data on
 * to INNER_STREAM.  Allocate objects in POOL.
 */
static svn_stream_t *
fnv1a_wrap_stream(svn_checksum_ctx_t **context,
                  svn_stream_t *inner_stream,
                  apr_pool_t *pool)
{
  svn_stream_t *outer_stream;

  fnv1a_stream_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
  baton->inner_stream = inner_stream;
  baton->context = svn_checksum_ctx_create(svn_checksum_fnv1a_32x4, pool);
  *context = baton->context;

  outer_stream = svn_stream_create(baton, pool);
  svn_stream_set_write(outer_stream, fnv1a_write_handler);

  return outer_stream;
}

/* Set *DIGEST to the FNV checksum calculated in CONTEXT.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
fnv1a_checksum_finalize(apr_uint32_t *digest,
                        svn_checksum_ctx_t *context,
                        apr_pool_t *scratch_pool)
{
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum_final(&checksum, context, scratch_pool));
  SVN_ERR_ASSERT(checksum->kind == svn_checksum_fnv1a_32x4);
  *digest = ntohl(*(const apr_uint32_t *)(checksum->digest));

  return SVN_NO_ERROR;
}

/* This baton is used by the representation writing streams.  It keeps
   track of the checksum information as well as the total size of the
   representation so far. */
struct rep_write_baton
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
  node_revision_t *noderev;

  /* Actual output file. */
  apr_file_t *file;
  /* Lock 'cookie' used to unlock the output file once we've finished
     writing to it. */
  void *lockcookie;

  svn_checksum_ctx_t *md5_checksum_ctx;
  svn_checksum_ctx_t *sha1_checksum_ctx;

  /* calculate a modified FNV-1a checksum of the on-disk representation */
  svn_checksum_ctx_t *fnv1a_checksum_ctx;

  /* Local / scratch pool, available for temporary allocations. */
  apr_pool_t *scratch_pool;

  /* Outer / result pool. */
  apr_pool_t *result_pool;
};

/* Handler for the write method of the representation writable stream.
   BATON is a rep_write_baton, DATA is the data to write, and *LEN is
   the length of this data. */
static svn_error_t *
rep_write_contents(void *baton,
                   const char *data,
                   apr_size_t *len)
{
  struct rep_write_baton *b = baton;

  SVN_ERR(svn_checksum_update(b->md5_checksum_ctx, data, *len));
  SVN_ERR(svn_checksum_update(b->sha1_checksum_ctx, data, *len));
  b->rep_size += *len;

  /* If we are writing a delta, use that stream. */
  if (b->delta_stream)
    return svn_stream_write(b->delta_stream, data, len);
  else
    return svn_stream_write(b->rep_stream, data, len);
}

/* Set *SPANNED to the number of shards touched when walking WALK steps on
 * NODEREV's predecessor chain in FS.  Use POOL for temporary allocations.
 */
static svn_error_t *
shards_spanned(int *spanned,
               svn_fs_t *fs,
               node_revision_t *noderev,
               int walk,
               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  int shard_size = ffd->max_files_per_dir ? ffd->max_files_per_dir : 1;
  apr_pool_t *iterpool;

  int count = walk ? 1 : 0; /* The start of a walk already touches a shard. */
  svn_revnum_t shard, last_shard = ffd->youngest_rev_cache / shard_size;
  iterpool = svn_pool_create(pool);
  while (walk-- && noderev->predecessor_count)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs,
                                           noderev->predecessor_id, pool,
                                           iterpool));
      shard = svn_fs_fs__id_rev(noderev->id) / shard_size;
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
   base representation will be returned.  Perform temporary allocations
   in *POOL. */
static svn_error_t *
choose_delta_base(representation_t **rep,
                  svn_fs_t *fs,
                  node_revision_t *noderev,
                  svn_boolean_t props,
                  apr_pool_t *pool)
{
  /* The zero-based index (counting from the "oldest" end), along NODEREVs line
   * predecessors, of the node-rev we will use as delta base. */
  int count;
  /* The length of the linear part of a delta chain.  (Delta chains use
   * skip-delta bits for the high-order bits and are linear in the low-order
   * bits.) */
  int walk;
  node_revision_t *base;
  fs_fs_data_t *ffd = fs->fsap_data;
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
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__get_node_revision(&base, fs,
                                           base->predecessor_id, pool,
                                           iterpool));
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
      svn_filesize_t rep_size = (*rep)->expanded_size;
      if (rep_size < 64)
        {
          *rep = NULL;
          return SVN_NO_ERROR;
        }

      /* Check whether the length of the deltification chain is acceptable.
       * Otherwise, shared reps may form a non-skipping delta chain in
       * extreme cases. */
      SVN_ERR(svn_fs_fs__rep_chain_length(&chain_length, &shard_count,
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
  struct rep_write_baton *b = data;
  svn_error_t *err;

  /* Truncate and close the protorevfile. */
  err = svn_io_file_trunc(b->file, b->rep_offset, b->scratch_pool);
  err = svn_error_compose_create(err, svn_io_file_close(b->file,
                                                        b->scratch_pool));

  /* Remove our lock regardless of any preceding errors so that the
     being_written flag is always removed and stays consistent with the
     file lock which will be removed no matter what since the pool is
     going away. */
  err = svn_error_compose_create(err,
                                 unlock_proto_rev(b->fs,
                                     svn_fs_fs__id_txn_id(b->noderev->id),
                                     b->lockcookie, b->scratch_pool));
  if (err)
    {
      apr_status_t rc = err->apr_err;
      svn_error_clear(err);
      return rc;
    }

  return APR_SUCCESS;
}

static void
txdelta_to_svndiff(svn_txdelta_window_handler_t *handler,
                   void **handler_baton,
                   svn_stream_t *output,
                   svn_fs_t *fs,
                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  int svndiff_version;

  if (ffd->delta_compression_type == compression_type_lz4)
    {
      SVN_ERR_ASSERT_NO_RETURN(ffd->format >= SVN_FS_FS__MIN_SVNDIFF2_FORMAT);
      svndiff_version = 2;
    }
  else if (ffd->delta_compression_type == compression_type_zlib)
    {
      SVN_ERR_ASSERT_NO_RETURN(ffd->format >= SVN_FS_FS__MIN_SVNDIFF1_FORMAT);
      svndiff_version = 1;
    }
  else
    {
      svndiff_version = 0;
    }

  svn_txdelta_to_svndiff3(handler, handler_baton, output, svndiff_version,
                          ffd->delta_compression_level, pool);
}

/* Get a rep_write_baton and store it in *WB_P for the representation
   indicated by NODEREV in filesystem FS.  Perform allocations in
   POOL.  Only appropriate for file contents, not for props or
   directory contents. */
static svn_error_t *
rep_write_get_baton(struct rep_write_baton **wb_p,
                    svn_fs_t *fs,
                    node_revision_t *noderev,
                    apr_pool_t *pool)
{
  struct rep_write_baton *b;
  apr_file_t *file;
  representation_t *base_rep;
  svn_stream_t *source;
  svn_txdelta_window_handler_t wh;
  void *whb;
  svn_fs_fs__rep_header_t header = { 0 };

  b = apr_pcalloc(pool, sizeof(*b));

  b->sha1_checksum_ctx = svn_checksum_ctx_create(svn_checksum_sha1, pool);
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);

  b->fs = fs;
  b->result_pool = pool;
  b->scratch_pool = svn_pool_create(pool);
  b->rep_size = 0;
  b->noderev = noderev;

  /* Open the prototype rev file and seek to its end. */
  SVN_ERR(get_writable_proto_rev(&file, &b->lockcookie,
                                 fs, svn_fs_fs__id_txn_id(noderev->id),
                                 b->scratch_pool));

  b->file = file;
  b->rep_stream = svn_stream_from_aprfile2(file, TRUE, b->scratch_pool);
  if (svn_fs_fs__use_log_addressing(fs))
    b->rep_stream = fnv1a_wrap_stream(&b->fnv1a_checksum_ctx, b->rep_stream,
                                      b->scratch_pool);

  SVN_ERR(svn_io_file_get_offset(&b->rep_offset, file, b->scratch_pool));

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, FALSE, b->scratch_pool));
  SVN_ERR(svn_fs_fs__get_contents(&source, fs, base_rep, TRUE,
                                  b->scratch_pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = base_rep->revision;
      header.base_item_index = base_rep->item_index;
      header.base_length = base_rep->size;
      header.type = svn_fs_fs__rep_delta;
    }
  else
    {
      header.type = svn_fs_fs__rep_self_delta;
    }
  SVN_ERR(svn_fs_fs__write_rep_header(&header, b->rep_stream,
                                      b->scratch_pool));

  /* Now determine the offset of the actual svndiff data. */
  SVN_ERR(svn_io_file_get_offset(&b->delta_start, file,
                                 b->scratch_pool));

  /* Cleanup in case something goes wrong. */
  apr_pool_cleanup_register(b->scratch_pool, b, rep_write_cleanup,
                            apr_pool_cleanup_null);

  /* Prepare to write the svndiff data. */
  txdelta_to_svndiff(&wh, &whb, b->rep_stream, fs, pool);

  b->delta_stream = svn_txdelta_target_push(wh, whb, source,
                                            b->scratch_pool);

  *wb_p = b;

  return SVN_NO_ERROR;
}

/* For REP->SHA1_CHECKSUM, try to find an already existing representation
   in FS and return it in *OLD_REP.  If no such representation exists or
   if rep sharing has been disabled for FS, NULL will be returned.  Since
   there may be new duplicate representations within the same uncommitted
   revision, those can be passed in REPS_HASH (maps a sha1 digest onto
   representation_t*), otherwise pass in NULL for REPS_HASH.

   The content of both representations will be compared, taking REP's content
   from FILE at OFFSET.  Only if they actually match, will *OLD_REP not be
   NULL.

   Use RESULT_POOL for *OLD_REP  allocations and SCRATCH_POOL for temporaries.
   The lifetime of *OLD_REP is limited by both, RESULT_POOL and REP lifetime.
 */
static svn_error_t *
get_shared_rep(representation_t **old_rep,
               svn_fs_t *fs,
               representation_t *rep,
               apr_file_t *file,
               apr_off_t offset,
               apr_hash_t *reps_hash,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  fs_fs_data_t *ffd = fs->fsap_data;

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
      err = svn_fs_fs__get_rep_reference(old_rep, fs, &checksum, result_pool);
      /* ### Other error codes that we shouldn't mask out? */
      if (err == SVN_NO_ERROR)
        {
          if (*old_rep)
            SVN_ERR(svn_fs_fs__check_rep(*old_rep, fs, NULL, scratch_pool));
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
  if (*old_rep == NULL && is_txn_rep(rep))
    {
      svn_node_kind_t kind;
      const char *file_name
        = path_txn_sha1(fs, &rep->txn_id, rep->sha1_digest, scratch_pool);

      /* in our txn, is there a rep file named with the wanted SHA1?
         If so, read it and use that rep.
       */
      SVN_ERR(svn_io_check_path(file_name, &kind, scratch_pool));
      if (kind == svn_node_file)
        {
          svn_stringbuf_t *rep_string;
          SVN_ERR(svn_stringbuf_from_file2(&rep_string, file_name,
                                           scratch_pool));
          SVN_ERR(svn_fs_fs__parse_representation(old_rep, rep_string,
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
      (*old_rep)->uniquifier = rep->uniquifier;
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

      /* The existing representation may itself be part of the current
       * transaction.  In that case, it may be in different stages of
       * the commit finalization process.
       *
       * OLD_REP_NORM is the same as that OLD_REP but it is assigned
       * explicitly to REP's transaction if OLD_REP does not point
       * to an already committed revision.  This then prevents the
       * revision lookup and the txn data will be accessed.
       */
      representation_t old_rep_norm = **old_rep;
      if (   !SVN_IS_VALID_REVNUM(old_rep_norm.revision)
          || old_rep_norm.revision > ffd->youngest_rev_cache)
        old_rep_norm.txn_id = rep->txn_id;

      /* Make sure we can later restore FILE's current position. */
      SVN_ERR(svn_io_file_get_offset(&old_position, file, scratch_pool));

      /* Compare the two representations.
       * Note that the stream comparison might also produce MD5 checksum
       * errors or other failures in case of SHA1 collisions. */
      SVN_ERR(svn_fs_fs__get_contents_from_file(&contents, fs, rep, file,
                                                offset, scratch_pool));
      SVN_ERR(svn_fs_fs__get_contents(&old_contents, fs, &old_rep_norm,
                                      FALSE, scratch_pool));
      err = svn_stream_contents_same2(&same, contents, old_contents,
                                      scratch_pool);

      /* A mismatch should be extremely rare.
       * If it does happen, reject the commit. */
      if (!same || err)
        {
          /* SHA1 collision or worse. */
          svn_stringbuf_t *old_rep_str
            = svn_fs_fs__unparse_representation(*old_rep,
                                                ffd->format, FALSE,
                                                scratch_pool,
                                                scratch_pool);
          svn_stringbuf_t *rep_str
            = svn_fs_fs__unparse_representation(rep,
                                                ffd->format, FALSE,
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
 * Use POOL for allocations.
 */
static svn_error_t *
digests_final(representation_t *rep,
              const svn_checksum_ctx_t *md5_ctx,
              const svn_checksum_ctx_t *sha1_ctx,
              apr_pool_t *pool)
{
  svn_checksum_t *checksum;

  SVN_ERR(svn_checksum_final(&checksum, md5_ctx, pool));
  memcpy(rep->md5_digest, checksum->digest, svn_checksum_size(checksum));
  rep->has_sha1 = sha1_ctx != NULL;
  if (rep->has_sha1)
    {
      SVN_ERR(svn_checksum_final(&checksum, sha1_ctx, pool));
      memcpy(rep->sha1_digest, checksum->digest, svn_checksum_size(checksum));
    }

  return SVN_NO_ERROR;
}

/* Close handler for the representation write stream.  BATON is a
   rep_write_baton.  Writes out a new node-rev that correctly
   references the representation we just finished writing. */
static svn_error_t *
rep_write_contents_close(void *baton)
{
  struct rep_write_baton *b = baton;
  representation_t *rep;
  representation_t *old_rep;
  apr_off_t offset;

  rep = apr_pcalloc(b->result_pool, sizeof(*rep));

  /* Close our delta stream so the last bits of svndiff are written
     out. */
  if (b->delta_stream)
    SVN_ERR(svn_stream_close(b->delta_stream));

  /* Determine the length of the svndiff data. */
  SVN_ERR(svn_io_file_get_offset(&offset, b->file, b->scratch_pool));
  rep->size = offset - b->delta_start;

  /* Fill in the rest of the representation field. */
  rep->expanded_size = b->rep_size;
  rep->txn_id = *svn_fs_fs__id_txn_id(b->noderev->id);
  SVN_ERR(set_uniquifier(b->fs, rep, b->scratch_pool));
  rep->revision = SVN_INVALID_REVNUM;

  /* Finalize the checksum. */
  SVN_ERR(digests_final(rep, b->md5_checksum_ctx, b->sha1_checksum_ctx,
                        b->result_pool));

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  SVN_ERR(get_shared_rep(&old_rep, b->fs, rep, b->file, b->rep_offset, NULL,
                         b->result_pool, b->scratch_pool));

  if (old_rep)
    {
      /* We need to erase from the protorev the data we just wrote. */
      SVN_ERR(svn_io_file_trunc(b->file, b->rep_offset, b->scratch_pool));

      /* Use the old rep for this content. */
      b->noderev->data_rep = old_rep;
    }
  else
    {
      /* Write out our cosmetic end marker. */
      SVN_ERR(svn_stream_puts(b->rep_stream, "ENDREP\n"));
      SVN_ERR(allocate_item_index(&rep->item_index, b->fs, &rep->txn_id,
                                  b->rep_offset, b->scratch_pool));

      b->noderev->data_rep = rep;
    }

  /* Remove cleanup callback. */
  apr_pool_cleanup_kill(b->scratch_pool, b, rep_write_cleanup);

  /* Write out the new node-rev information. */
  SVN_ERR(svn_fs_fs__put_node_revision(b->fs, b->noderev->id, b->noderev,
                                       FALSE, b->scratch_pool));
  if (!old_rep && svn_fs_fs__use_log_addressing(b->fs))
    {
      svn_fs_fs__p2l_entry_t entry;

      entry.offset = b->rep_offset;
      SVN_ERR(svn_io_file_get_offset(&offset, b->file, b->scratch_pool));
      entry.size = offset - b->rep_offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_FILE_REP;
      entry.item.revision = SVN_INVALID_REVNUM;
      entry.item.number = rep->item_index;
      SVN_ERR(fnv1a_checksum_finalize(&entry.fnv1_checksum,
                                      b->fnv1a_checksum_ctx,
                                      b->scratch_pool));

      SVN_ERR(store_p2l_index_entry(b->fs, &rep->txn_id, &entry,
                                    b->scratch_pool));
    }

  SVN_ERR(svn_io_file_close(b->file, b->scratch_pool));

  /* Write the sha1->rep mapping *after* we successfully written node
   * revision to disk. */
  if (!old_rep)
    SVN_ERR(store_sha1_rep_mapping(b->fs, b->noderev, b->scratch_pool));

  SVN_ERR(unlock_proto_rev(b->fs, &rep->txn_id, b->lockcookie,
                           b->scratch_pool));
  svn_pool_destroy(b->scratch_pool);

  return SVN_NO_ERROR;
}

/* Store a writable stream in *CONTENTS_P that will receive all data
   written and store it as the file data representation referenced by
   NODEREV in filesystem FS.  Perform temporary allocations in
   POOL.  Only appropriate for file data, not props or directory
   contents. */
static svn_error_t *
set_representation(svn_stream_t **contents_p,
                   svn_fs_t *fs,
                   node_revision_t *noderev,
                   apr_pool_t *pool)
{
  struct rep_write_baton *wb;

  if (! svn_fs_fs__id_is_txn(noderev->id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Attempted to write to non-transaction '%s'"),
                             svn_fs_fs__id_unparse(noderev->id, pool)->data);

  SVN_ERR(rep_write_get_baton(&wb, fs, noderev, pool));

  *contents_p = svn_stream_create(wb, pool);
  svn_stream_set_write(*contents_p, rep_write_contents);
  svn_stream_set_close(*contents_p, rep_write_contents_close);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_contents(svn_stream_t **stream,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  if (noderev->kind != svn_node_file)
    return svn_error_create(SVN_ERR_FS_NOT_FILE, NULL,
                            _("Can't set text contents of a directory"));

  return set_representation(stream, fs, noderev, pool);
}

svn_error_t *
svn_fs_fs__create_successor(const svn_fs_id_t **new_id_p,
                            svn_fs_t *fs,
                            const svn_fs_id_t *old_idp,
                            node_revision_t *new_noderev,
                            const svn_fs_fs__id_part_t *copy_id,
                            const svn_fs_fs__id_part_t *txn_id,
                            apr_pool_t *pool)
{
  const svn_fs_id_t *id;

  if (! copy_id)
    copy_id = svn_fs_fs__id_copy_id(old_idp);
  id = svn_fs_fs__id_txn_create(svn_fs_fs__id_node_id(old_idp), copy_id,
                                txn_id, pool);

  new_noderev->id = id;

  if (! new_noderev->copyroot_path)
    {
      new_noderev->copyroot_path = apr_pstrdup(pool,
                                               new_noderev->created_path);
      new_noderev->copyroot_rev = svn_fs_fs__id_rev(new_noderev->id);
    }

  SVN_ERR(svn_fs_fs__put_node_revision(fs, new_noderev->id, new_noderev, FALSE,
                                       pool));

  *new_id_p = id;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_proplist(svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_hash_t *proplist,
                        apr_pool_t *pool)
{
  const char *filename
    = svn_fs_fs__path_txn_node_props(fs, noderev->id, pool);
  apr_file_t *file;
  svn_stream_t *out;

  /* Dump the property list to the mutable property file. */
  SVN_ERR(svn_io_file_open(&file, filename,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));
  out = svn_stream_from_aprfile2(file, TRUE, pool);
  SVN_ERR(svn_hash_write2(proplist, out, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  /* Mark the node-rev's prop rep as mutable, if not already done. */
  if (!noderev->prop_rep || !is_txn_rep(noderev->prop_rep))
    {
      noderev->prop_rep = apr_pcalloc(pool, sizeof(*noderev->prop_rep));
      noderev->prop_rep->txn_id = *svn_fs_fs__id_txn_id(noderev->id);
      SVN_ERR(set_uniquifier(fs, noderev->prop_rep, pool));
      noderev->prop_rep->revision = SVN_INVALID_REVNUM;
      SVN_ERR(svn_fs_fs__put_node_revision(fs, noderev->id, noderev, FALSE,
                                           pool));
    }

  return SVN_NO_ERROR;
}

/* This baton is used by the stream created for write_container_rep. */
struct write_container_baton
{
  svn_stream_t *stream;

  apr_size_t size;

  svn_checksum_ctx_t *md5_ctx;

  /* SHA1 calculation is optional. If not needed, this will be NULL. */
  svn_checksum_ctx_t *sha1_ctx;
};

/* The handler for the write_container_rep stream.  BATON is a
   write_container_baton, DATA has the data to write and *LEN is the number
   of bytes to write. */
static svn_error_t *
write_container_handler(void *baton,
                        const char *data,
                        apr_size_t *len)
{
  struct write_container_baton *whb = baton;

  SVN_ERR(svn_checksum_update(whb->md5_ctx, data, *len));
  if (whb->sha1_ctx)
    SVN_ERR(svn_checksum_update(whb->sha1_ctx, data, *len));

  SVN_ERR(svn_stream_write(whb->stream, data, len));
  whb->size += *len;

  return SVN_NO_ERROR;
}

/* Callback function type.  Write the data provided by BATON into STREAM. */
typedef svn_error_t *
(* collection_writer_t)(svn_stream_t *stream, void *baton, apr_pool_t *pool);

/* Implement collection_writer_t writing the C string->svn_string_t hash
   given as BATON. */
static svn_error_t *
write_hash_to_stream(svn_stream_t *stream,
                     void *baton,
                     apr_pool_t *pool)
{
  apr_hash_t *hash = baton;
  SVN_ERR(svn_hash_write2(hash, stream, SVN_HASH_TERMINATOR, pool));

  return SVN_NO_ERROR;
}

/* Implement collection_writer_t writing the svn_fs_dirent_t* array given
   as BATON. */
static svn_error_t *
write_directory_to_stream(svn_stream_t *stream,
                          void *baton,
                          apr_pool_t *pool)
{
  apr_array_header_t *dir = baton;
  SVN_ERR(unparse_dir_entries(dir, stream, pool));

  return SVN_NO_ERROR;
}

/* Write out the COLLECTION as a text representation to file FILE using
   WRITER.  In the process, record position, the total size of the dump and
   MD5 as well as SHA1 in REP.   Add the representation of type ITEM_TYPE to
   the indexes if necessary.

   If ALLOW_REP_SHARING is FALSE, rep-sharing will not be used, regardless
   of any other option and rep-sharing settings.  If rep sharing has been
   enabled and REPS_HASH is not NULL, it will be used in addition to the
   on-disk cache to find earlier reps with the same content.  If such
   existing reps can be found, we will truncate the one just written from
   the file and return the existing rep.

   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
write_container_rep(representation_t *rep,
                    apr_file_t *file,
                    void *collection,
                    collection_writer_t writer,
                    svn_fs_t *fs,
                    apr_hash_t *reps_hash,
                    svn_boolean_t allow_rep_sharing,
                    apr_uint32_t item_type,
                    apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  struct write_container_baton *whb;
  svn_checksum_ctx_t *fnv1a_checksum_ctx;
  apr_off_t offset = 0;

  SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));

  whb = apr_pcalloc(scratch_pool, sizeof(*whb));

  whb->stream = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  if (svn_fs_fs__use_log_addressing(fs))
    whb->stream = fnv1a_wrap_stream(&fnv1a_checksum_ctx, whb->stream,
                                    scratch_pool);
  else
    fnv1a_checksum_ctx = NULL;
  whb->size = 0;
  whb->md5_ctx = svn_checksum_ctx_create(svn_checksum_md5, scratch_pool);
  if (item_type != SVN_FS_FS__ITEM_TYPE_DIR_REP)
    whb->sha1_ctx = svn_checksum_ctx_create(svn_checksum_sha1, scratch_pool);

  stream = svn_stream_create(whb, scratch_pool);
  svn_stream_set_write(stream, write_container_handler);

  SVN_ERR(svn_stream_puts(whb->stream, "PLAIN\n"));

  SVN_ERR(writer(stream, collection, scratch_pool));

  /* Store the results. */
  SVN_ERR(digests_final(rep, whb->md5_ctx, whb->sha1_ctx, scratch_pool));

  /* Update size info. */
  rep->expanded_size = whb->size;
  rep->size = whb->size;

  /* Check and see if we already have a representation somewhere that's
     identical to the one we just wrote out. */
  if (allow_rep_sharing)
    {
      representation_t *old_rep;
      SVN_ERR(get_shared_rep(&old_rep, fs, rep, file, offset, reps_hash,
                             scratch_pool, scratch_pool));

      if (old_rep)
        {
          /* We need to erase from the protorev the data we just wrote. */
          SVN_ERR(svn_io_file_trunc(file, offset, scratch_pool));

          /* Use the old rep for this content. */
          memcpy(rep, old_rep, sizeof (*rep));
          return SVN_NO_ERROR;
        }
    }

  /* Write out our cosmetic end marker. */
  SVN_ERR(svn_stream_puts(whb->stream, "ENDREP\n"));

  SVN_ERR(allocate_item_index(&rep->item_index, fs, &rep->txn_id,
                              offset, scratch_pool));

  if (svn_fs_fs__use_log_addressing(fs))
    {
      svn_fs_fs__p2l_entry_t entry;

      entry.offset = offset;
      SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));
      entry.size = offset - entry.offset;
      entry.type = item_type;
      entry.item.revision = SVN_INVALID_REVNUM;
      entry.item.number = rep->item_index;
      SVN_ERR(fnv1a_checksum_finalize(&entry.fnv1_checksum,
                                      fnv1a_checksum_ctx,
                                      scratch_pool));

      SVN_ERR(store_p2l_index_entry(fs, &rep->txn_id, &entry, scratch_pool));
    }

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
   Perform temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
write_container_delta_rep(representation_t *rep,
                          apr_file_t *file,
                          void *collection,
                          collection_writer_t writer,
                          svn_fs_t *fs,
                          node_revision_t *noderev,
                          apr_hash_t *reps_hash,
                          svn_boolean_t allow_rep_sharing,
                          apr_uint32_t item_type,
                          apr_pool_t *scratch_pool)
{
  svn_txdelta_window_handler_t diff_wh;
  void *diff_whb;

  svn_stream_t *file_stream;
  svn_stream_t *stream;
  representation_t *base_rep;
  svn_checksum_ctx_t *fnv1a_checksum_ctx;
  svn_stream_t *source;
  svn_fs_fs__rep_header_t header = { 0 };

  apr_off_t rep_end = 0;
  apr_off_t delta_start = 0;
  apr_off_t offset = 0;

  struct write_container_baton *whb;
  svn_boolean_t is_props = (item_type == SVN_FS_FS__ITEM_TYPE_FILE_PROPS)
                        || (item_type == SVN_FS_FS__ITEM_TYPE_DIR_PROPS);

  /* Get the base for this delta. */
  SVN_ERR(choose_delta_base(&base_rep, fs, noderev, is_props, scratch_pool));
  SVN_ERR(svn_fs_fs__get_contents(&source, fs, base_rep, FALSE, scratch_pool));

  SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));

  /* Write out the rep header. */
  if (base_rep)
    {
      header.base_revision = base_rep->revision;
      header.base_item_index = base_rep->item_index;
      header.base_length = base_rep->size;
      header.type = svn_fs_fs__rep_delta;
    }
  else
    {
      header.type = svn_fs_fs__rep_self_delta;
    }

  file_stream = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  if (svn_fs_fs__use_log_addressing(fs))
    file_stream = fnv1a_wrap_stream(&fnv1a_checksum_ctx, file_stream,
                                    scratch_pool);
  else
    fnv1a_checksum_ctx = NULL;
  SVN_ERR(svn_fs_fs__write_rep_header(&header, file_stream, scratch_pool));
  SVN_ERR(svn_io_file_get_offset(&delta_start, file, scratch_pool));

  /* Prepare to write the svndiff data. */
  txdelta_to_svndiff(&diff_wh, &diff_whb, file_stream, fs, scratch_pool);

  whb = apr_pcalloc(scratch_pool, sizeof(*whb));
  whb->stream = svn_txdelta_target_push(diff_wh, diff_whb, source,
                                        scratch_pool);
  whb->size = 0;
  whb->md5_ctx = svn_checksum_ctx_create(svn_checksum_md5, scratch_pool);
  if (item_type != SVN_FS_FS__ITEM_TYPE_DIR_REP)
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
    {
      representation_t *old_rep;
      SVN_ERR(get_shared_rep(&old_rep, fs, rep, file, offset, reps_hash,
                             scratch_pool, scratch_pool));

      if (old_rep)
        {
          /* We need to erase from the protorev the data we just wrote. */
          SVN_ERR(svn_io_file_trunc(file, offset, scratch_pool));

          /* Use the old rep for this content. */
          memcpy(rep, old_rep, sizeof (*rep));
          return SVN_NO_ERROR;
        }
    }

  /* Write out our cosmetic end marker. */
  SVN_ERR(svn_stream_puts(file_stream, "ENDREP\n"));

  SVN_ERR(allocate_item_index(&rep->item_index, fs, &rep->txn_id,
                              offset, scratch_pool));

  if (svn_fs_fs__use_log_addressing(fs))
    {
      svn_fs_fs__p2l_entry_t entry;

      entry.offset = offset;
      SVN_ERR(svn_io_file_get_offset(&offset, file, scratch_pool));
      entry.size = offset - entry.offset;
      entry.type = item_type;
      entry.item.revision = SVN_INVALID_REVNUM;
      entry.item.number = rep->item_index;
      SVN_ERR(fnv1a_checksum_finalize(&entry.fnv1_checksum,
                                      fnv1a_checksum_ctx,
                                      scratch_pool));

      SVN_ERR(store_p2l_index_entry(fs, &rep->txn_id, &entry, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Sanity check ROOT_NODEREV, a candidate for being the root node-revision
   of (not yet committed) revision REV in FS.  Use POOL for temporary
   allocations.

   If you change this function, consider updating svn_fs_fs__verify() too.
 */
static svn_error_t *
validate_root_noderev(svn_fs_t *fs,
                      node_revision_t *root_noderev,
                      svn_revnum_t rev,
                      apr_pool_t *pool)
{
  svn_revnum_t head_revnum = rev-1;
  int head_predecessor_count;

  SVN_ERR_ASSERT(rev > 0);

  /* Compute HEAD_PREDECESSOR_COUNT. */
  {
    svn_fs_root_t *head_revision;
    const svn_fs_id_t *head_root_id;
    node_revision_t *head_root_noderev;

    /* Get /@HEAD's noderev. */
    SVN_ERR(svn_fs_fs__revision_root(&head_revision, fs, head_revnum, pool));
    SVN_ERR(svn_fs_fs__node_id(&head_root_id, head_revision, "/", pool));
    SVN_ERR(svn_fs_fs__get_node_revision(&head_root_noderev, fs, head_root_id,
                                         pool, pool));
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
  if (   (root_noderev->predecessor_count - head_predecessor_count)
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
 * based on the REVISION currently being written and the START_ID for that
 * revision.  Use the repo FORMAT to decide which implementation to use.
 */
static void
get_final_id(svn_fs_fs__id_part_t *part,
             svn_revnum_t revision,
             apr_uint64_t start_id,
             int format)
{
  if (part->revision == SVN_INVALID_REVNUM)
    {
      if (format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
        {
          part->revision = revision;
        }
      else
        {
          part->revision = 0;
          part->number += start_id;
        }
    }
}

/* Copy a node-revision specified by id ID in fileystem FS from a
   transaction into the proto-rev-file FILE.  Set *NEW_ID_P to a
   pointer to the new node-id which will be allocated in POOL.
   If this is a directory, copy all children as well.

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

   Temporary allocations are also from POOL. */
static svn_error_t *
write_final_rev(const svn_fs_id_t **new_id_p,
                apr_file_t *file,
                svn_revnum_t rev,
                svn_fs_t *fs,
                const svn_fs_id_t *id,
                apr_uint64_t start_node_id,
                apr_uint64_t start_copy_id,
                apr_off_t initial_offset,
                apr_array_header_t *directory_ids,
                apr_array_header_t *reps_to_cache,
                apr_hash_t *reps_hash,
                apr_pool_t *reps_pool,
                svn_boolean_t at_root,
                apr_pool_t *pool)
{
  node_revision_t *noderev;
  apr_off_t my_offset;
  const svn_fs_id_t *new_id;
  svn_fs_fs__id_part_t node_id, copy_id, rev_item;
  fs_fs_data_t *ffd = fs->fsap_data;
  const svn_fs_fs__id_part_t *txn_id = svn_fs_fs__id_txn_id(id);
  svn_stream_t *file_stream;
  svn_checksum_ctx_t *fnv1a_checksum_ctx;
  apr_pool_t *subpool;

  *new_id_p = NULL;

  /* Check to see if this is a transaction node. */
  if (! svn_fs_fs__id_is_txn(id))
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);
  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool, subpool));

  if (noderev->kind == svn_node_dir)
    {
      apr_array_header_t *entries;
      int i;

      /* This is a directory.  Write out all the children first. */

      SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, fs, noderev, pool,
                                          subpool));
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_dirent_t *dirent
            = APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *);

          svn_pool_clear(subpool);
          SVN_ERR(write_final_rev(&new_id, file, rev, fs, dirent->id,
                                  start_node_id, start_copy_id, initial_offset,
                                  directory_ids, reps_to_cache, reps_hash,
                                  reps_pool, FALSE, subpool));
          if (new_id && (svn_fs_fs__id_rev(new_id) == rev))
            dirent->id = svn_fs_fs__id_copy(new_id, pool);
        }

      if (noderev->data_rep && is_txn_rep(noderev->data_rep))
        {
          pair_cache_key_t *key;
          svn_fs_fs__dir_data_t dir_data;

          /* Write out the contents of this directory as a text rep. */
          noderev->data_rep->revision = rev;
          if (ffd->deltify_directories)
            SVN_ERR(write_container_delta_rep(noderev->data_rep, file,
                                              entries,
                                              write_directory_to_stream,
                                              fs, noderev, NULL, FALSE,
                                              SVN_FS_FS__ITEM_TYPE_DIR_REP,
                                              pool));
          else
            SVN_ERR(write_container_rep(noderev->data_rep, file, entries,
                                        write_directory_to_stream, fs, NULL,
                                        FALSE, SVN_FS_FS__ITEM_TYPE_DIR_REP,
                                        pool));

          reset_txn_in_rep(noderev->data_rep);

          /* Cache the new directory contents.  Otherwise, subsequent reads
           * or commits will likely have to reconstruct, verify and parse
           * it again. */
          key = apr_array_push(directory_ids);
          key->revision = noderev->data_rep->revision;
          key->second = noderev->data_rep->item_index;

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

      if (noderev->data_rep && is_txn_rep(noderev->data_rep))
        {
          reset_txn_in_rep(noderev->data_rep);
          noderev->data_rep->revision = rev;

          if (!svn_fs_fs__use_log_addressing(fs))
            {
              /* See issue 3845.  Some unknown mechanism caused the
                 protorev file to get truncated, so check for that
                 here.  */
              if (noderev->data_rep->item_index + noderev->data_rep->size
                  > initial_offset)
                return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                        _("Truncated protorev file detected"));
            }
        }
    }

  svn_pool_destroy(subpool);

  /* Fix up the property reps. */
  if (noderev->prop_rep && is_txn_rep(noderev->prop_rep))
    {
      apr_hash_t *proplist;
      apr_uint32_t item_type = noderev->kind == svn_node_dir
                             ? SVN_FS_FS__ITEM_TYPE_DIR_PROPS
                             : SVN_FS_FS__ITEM_TYPE_FILE_PROPS;
      SVN_ERR(svn_fs_fs__get_proplist(&proplist, fs, noderev, pool));
      noderev->prop_rep->txn_id = *txn_id;
      SVN_ERR(set_uniquifier(fs, noderev->prop_rep, pool));
      noderev->prop_rep->revision = rev;

      if (ffd->deltify_properties)
        SVN_ERR(write_container_delta_rep(noderev->prop_rep, file, proplist,
                                          write_hash_to_stream, fs, noderev,
                                          reps_hash, TRUE, item_type, pool));
      else
        SVN_ERR(write_container_rep(noderev->prop_rep, file, proplist,
                                    write_hash_to_stream, fs, reps_hash,
                                    TRUE, item_type, pool));

      reset_txn_in_rep(noderev->prop_rep);
    }

  /* Convert our temporary ID into a permanent revision one. */
  node_id = *svn_fs_fs__id_node_id(noderev->id);
  get_final_id(&node_id, rev, start_node_id, ffd->format);
  copy_id = *svn_fs_fs__id_copy_id(noderev->id);
  get_final_id(&copy_id, rev, start_copy_id, ffd->format);

  if (noderev->copyroot_rev == SVN_INVALID_REVNUM)
    noderev->copyroot_rev = rev;

  /* root nodes have a fixed ID in log addressing mode */
  SVN_ERR(svn_io_file_get_offset(&my_offset, file, pool));
  if (svn_fs_fs__use_log_addressing(fs) && at_root)
    {
      /* reference the root noderev from the log-to-phys index */
      rev_item.number = SVN_FS_FS__ITEM_INDEX_ROOT_NODE;
      SVN_ERR(store_l2p_index_entry(fs, txn_id, my_offset,
                                    rev_item.number, pool));
    }
  else
    SVN_ERR(allocate_item_index(&rev_item.number, fs, txn_id,
                                my_offset, pool));

  rev_item.revision = rev;
  new_id = svn_fs_fs__id_rev_create(&node_id, &copy_id, &rev_item, pool);

  noderev->id = new_id;

  if (ffd->rep_sharing_allowed)
    {
      /* Save the data representation's hash in the rep cache. */
      if (   noderev->data_rep && noderev->kind == svn_node_file
          && noderev->data_rep->revision == rev)
        {
          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, representation_t *)
            = svn_fs_fs__rep_copy(noderev->data_rep, reps_pool);
        }

      if (noderev->prop_rep && noderev->prop_rep->revision == rev)
        {
          /* Add new property reps to hash and on-disk cache. */
          representation_t *copy
            = svn_fs_fs__rep_copy(noderev->prop_rep, reps_pool);

          SVN_ERR_ASSERT(reps_to_cache && reps_pool);
          APR_ARRAY_PUSH(reps_to_cache, representation_t *) = copy;

          apr_hash_set(reps_hash,
                        copy->sha1_digest,
                        APR_SHA1_DIGESTSIZE,
                        copy);
        }
    }

  /* don't serialize SHA1 for dir content to disk (waste of space) */
  /* ### Could clients record bogus last-changed-revisions (issue #4700)? */
  if (noderev->data_rep && noderev->kind == svn_node_dir)
    noderev->data_rep->has_sha1 = FALSE;

  /* Compatibility: while we don't need to serialize SHA1 for props (it is
     not used), older formats can only have representation strings that either
     have both the SHA1 value *and* the uniquifier, or don't have them at all.
     For such formats, both values get written to the disk only if the SHA1
     is present.

     We cannot omit the uniquifier, as doing so breaks svn_fs_props_changed()
     for properties with shared representations, see issues #4623 and #4700.
     Therefore, we skip writing SHA1, but only for the newer formats where
     this dependency is untied and we can write the uniquifier to the disk
     without the SHA1.
   */
  if (ffd->format >= SVN_FS_FS__MIN_REP_STRING_OPTIONAL_VALUES_FORMAT &&
      noderev->prop_rep)
    {
      noderev->prop_rep->has_sha1 = FALSE;
    }

  /* Workaround issue #4031: is-fresh-txn-root in revision files. */
  noderev->is_fresh_txn_root = FALSE;

  /* Write out our new node-revision. */
  if (at_root)
    SVN_ERR(validate_root_noderev(fs, noderev, rev, pool));

  file_stream = svn_stream_from_aprfile2(file, TRUE, pool);
  if (svn_fs_fs__use_log_addressing(fs))
    file_stream = fnv1a_wrap_stream(&fnv1a_checksum_ctx, file_stream, pool);
  else
    fnv1a_checksum_ctx = NULL;

  SVN_ERR(svn_fs_fs__write_noderev(file_stream, noderev, ffd->format,
                                   svn_fs_fs__fs_supports_mergeinfo(fs),
                                   pool));

  /* reference the root noderev from the log-to-phys index */
  if (svn_fs_fs__use_log_addressing(fs))
    {
      svn_fs_fs__p2l_entry_t entry;
      rev_item.revision = SVN_INVALID_REVNUM;

      entry.offset = my_offset;
      SVN_ERR(svn_io_file_get_offset(&my_offset, file, pool));
      entry.size = my_offset - entry.offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_NODEREV;
      entry.item = rev_item;
      SVN_ERR(fnv1a_checksum_finalize(&entry.fnv1_checksum,
                                      fnv1a_checksum_ctx,
                                      pool));

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));
    }

  /* Return our ID that references the revision file. */
  *new_id_p = noderev->id;

  return SVN_NO_ERROR;
}

/* Write the changed path info CHANGED_PATHS from transaction TXN_ID to the
   permanent rev-file FILE in filesystem FS.  *OFFSET_P is set the to offset
   in the file of the beginning of this information.  Perform temporary
   allocations in POOL. */
static svn_error_t *
write_final_changed_path_info(apr_off_t *offset_p,
                              apr_file_t *file,
                              svn_fs_t *fs,
                              const svn_fs_fs__id_part_t *txn_id,
                              apr_hash_t *changed_paths,
                              apr_pool_t *pool)
{
  apr_off_t offset;
  svn_stream_t *stream;
  svn_checksum_ctx_t *fnv1a_checksum_ctx;

  SVN_ERR(svn_io_file_get_offset(&offset, file, pool));

  /* write to target file & calculate checksum if needed */
  stream = svn_stream_from_aprfile2(file, TRUE, pool);
  if (svn_fs_fs__use_log_addressing(fs))
    stream = fnv1a_wrap_stream(&fnv1a_checksum_ctx, stream, pool);
  else
    fnv1a_checksum_ctx = NULL;

  SVN_ERR(svn_fs_fs__write_changes(stream, fs, changed_paths, TRUE, pool));

  *offset_p = offset;

  /* reference changes from the indexes */
  if (svn_fs_fs__use_log_addressing(fs))
    {
      svn_fs_fs__p2l_entry_t entry;

      entry.offset = offset;
      SVN_ERR(svn_io_file_get_offset(&offset, file, pool));
      entry.size = offset - entry.offset;
      entry.type = SVN_FS_FS__ITEM_TYPE_CHANGES;
      entry.item.revision = SVN_INVALID_REVNUM;
      entry.item.number = SVN_FS_FS__ITEM_INDEX_CHANGES;
      SVN_ERR(fnv1a_checksum_finalize(&entry.fnv1_checksum,
                                      fnv1a_checksum_ctx,
                                      pool));

      SVN_ERR(store_p2l_index_entry(fs, txn_id, &entry, pool));
      SVN_ERR(store_l2p_index_entry(fs, txn_id, entry.offset,
                                    SVN_FS_FS__ITEM_INDEX_CHANGES, pool));
    }

  return SVN_NO_ERROR;
}

/* Open a new svn_fs_t handle to FS, set that handle's concept of "current
   youngest revision" to NEW_REV, and call svn_fs_fs__verify_root() on
   NEW_REV's revision root.

   Intended to be called as the very last step in a commit before 'current'
   is bumped.  This implies that we are holding the write lock. */
static svn_error_t *
verify_before_commit(svn_fs_t *fs,
                     svn_revnum_t new_rev,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_fs_t *ft; /* fs++ == ft */
  svn_fs_root_t *root;
  fs_fs_data_t *ft_ffd;
  apr_hash_t *fs_config;

  SVN_ERR_ASSERT(ffd->svn_fs_open_);

  /* make sure FT does not simply return data cached by other instances
   * but actually retrieves it from disk at least once.
   */
  fs_config = apr_hash_make(pool);
  svn_hash_sets(fs_config, SVN_FS_CONFIG_FSFS_CACHE_NS,
                           svn_uuid_generate(pool));
  SVN_ERR(ffd->svn_fs_open_(&ft, fs->path,
                            fs_config,
                            pool,
                            pool));
  ft_ffd = ft->fsap_data;
  /* Don't let FT consult rep-cache.db, either. */
  ft_ffd->rep_sharing_allowed = FALSE;

  /* Time travel! */
  ft_ffd->youngest_rev_cache = new_rev;

  SVN_ERR(svn_fs_fs__revision_root(&root, ft, new_rev, pool));
  SVN_ERR_ASSERT(root->is_txn_root == FALSE && root->rev == new_rev);
  SVN_ERR_ASSERT(ft_ffd->youngest_rev_cache == new_rev);
  SVN_ERR(svn_fs_fs__verify_root(root, pool));

  return SVN_NO_ERROR;
}

/* Update the 'current' file to hold the correct next node and copy_ids
   from transaction TXN_ID in filesystem FS.  The current revision is
   set to REV.  Perform temporary allocations in POOL. */
static svn_error_t *
write_final_current(svn_fs_t *fs,
                    const svn_fs_fs__id_part_t *txn_id,
                    svn_revnum_t rev,
                    apr_uint64_t start_node_id,
                    apr_uint64_t start_copy_id,
                    apr_pool_t *pool)
{
  apr_uint64_t txn_node_id;
  apr_uint64_t txn_copy_id;
  fs_fs_data_t *ffd = fs->fsap_data;

  if (ffd->format >= SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    return svn_fs_fs__write_current(fs, rev, 0, 0, pool);

  /* To find the next available ids, we add the id that used to be in
     the 'current' file, to the next ids from the transaction file. */
  SVN_ERR(read_next_ids(&txn_node_id, &txn_copy_id, fs, txn_id, pool));

  start_node_id += txn_node_id;
  start_copy_id += txn_copy_id;

  return svn_fs_fs__write_current(fs, rev, start_node_id, start_copy_id,
                                  pool);
}

/* Verify that the user registered with FS has all the locks necessary to
   permit all the changes associated with TXN_NAME.
   The FS write lock is assumed to be held by the caller. */
static svn_error_t *
verify_locks(svn_fs_t *fs,
             const svn_fs_fs__id_part_t *txn_id,
             apr_hash_t *changed_paths,
             apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_array_header_t *changed_paths_sorted;
  svn_stringbuf_t *last_recursed = NULL;
  int i;

  /* Make an array of the changed paths, and sort them depth-first-ily.  */
  changed_paths_sorted = svn_sort__hash(changed_paths,
                                        svn_sort_compare_items_as_paths,
                                        pool);

  /* Now, traverse the array of changed paths, verify locks.  Note
     that if we need to do a recursive verification a path, we'll skip
     over children of that path when we get to them. */
  iterpool = svn_pool_create(pool);
  for (i = 0; i < changed_paths_sorted->nelts; i++)
    {
      const svn_sort__item_t *item;
      const char *path;
      svn_fs_path_change2_t *change;
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
      SVN_ERR(svn_fs_fs__allow_locked_operation(path, fs, recurse, TRUE,
                                                iterpool));

      /* If we just did a recursive check, remember the path we
         checked (so children can be skipped).  */
      if (recurse)
        {
          if (! last_recursed)
            last_recursed = svn_stringbuf_create(path, pool);
          else
            svn_stringbuf_set(last_recursed, path);
        }
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Writes final revision properties to file PATH applying permissions
   from file PERMS_REFERENCE. This involves setting svn:date and
   removing any temporary properties associated with the commit flags. */
static svn_error_t *
write_final_revprop(const char *path,
                    const char *perms_reference,
                    svn_fs_txn_t *txn,
                    svn_boolean_t flush_to_disk,
                    apr_pool_t *pool)
{
  apr_hash_t *txnprops;
  svn_string_t date;
  svn_string_t *client_date;
  apr_file_t *revprop_file;
  svn_stream_t *stream;

  SVN_ERR(svn_fs_fs__txn_proplist(&txnprops, txn, pool));

  /* Remove any temporary txn props representing 'flags'. */
  svn_hash_sets(txnprops, SVN_FS__PROP_TXN_CHECK_OOD, NULL);
  svn_hash_sets(txnprops, SVN_FS__PROP_TXN_CHECK_LOCKS, NULL);

  client_date = svn_hash_gets(txnprops, SVN_FS__PROP_TXN_CLIENT_DATE);
  if (client_date)
    {
      svn_hash_sets(txnprops, SVN_FS__PROP_TXN_CLIENT_DATE, NULL);
    }

  /* Update commit time to ensure that svn:date revprops remain ordered if
     requested. */
  if (!client_date || strcmp(client_date->data, "1"))
    {
      date.data = svn_time_to_cstring(apr_time_now(), pool);
      date.len = strlen(date.data);
      svn_hash_sets(txnprops, SVN_PROP_REVISION_DATE, &date);
    }

  /* Create new revprops file. Tell OS to truncate existing file,
     since  file may already exists from failed transaction. */
  SVN_ERR(svn_io_file_open(&revprop_file, path,
                           APR_WRITE | APR_CREATE | APR_TRUNCATE
                           | APR_BUFFERED, APR_OS_DEFAULT, pool));

  stream = svn_stream_from_aprfile2(revprop_file, TRUE, pool);
  SVN_ERR(svn_hash_write2(txnprops, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  if (flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(revprop_file, pool));
  SVN_ERR(svn_io_file_close(revprop_file, pool));

  SVN_ERR(svn_io_copy_perms(perms_reference, path, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__add_index_data(svn_fs_t *fs,
                          apr_file_t *file,
                          const char *l2p_proto_index,
                          const char *p2l_proto_index,
                          svn_revnum_t revision,
                          apr_pool_t *pool)
{
  apr_off_t l2p_offset;
  apr_off_t p2l_offset;
  svn_stringbuf_t *footer;
  unsigned char footer_length;
  svn_checksum_t *l2p_checksum;
  svn_checksum_t *p2l_checksum;

  /* Append the actual index data to the pack file. */
  l2p_offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_END, &l2p_offset, pool));
  SVN_ERR(svn_fs_fs__l2p_index_append(&l2p_checksum, fs, file,
                                      l2p_proto_index, revision,
                                      pool, pool));

  p2l_offset = 0;
  SVN_ERR(svn_io_file_seek(file, APR_END, &p2l_offset, pool));
  SVN_ERR(svn_fs_fs__p2l_index_append(&p2l_checksum, fs, file,
                                      p2l_proto_index, revision,
                                      pool, pool));

  /* Append footer. */
  footer = svn_fs_fs__unparse_footer(l2p_offset, l2p_checksum,
                                     p2l_offset, p2l_checksum, pool, pool);
  SVN_ERR(svn_io_file_write_full(file, footer->data, footer->len, NULL,
                                 pool));

  footer_length = footer->len;
  SVN_ERR_ASSERT(footer_length == footer->len);
  SVN_ERR(svn_io_file_write_full(file, &footer_length, 1, NULL, pool));

  return SVN_NO_ERROR;
}

/* Mark the directories cached in FS with the keys from DIRECTORY_IDS
 * as "valid" now.  Use SCRATCH_POOL for temporaries. */
static svn_error_t *
promote_cached_directories(svn_fs_t *fs,
                           apr_array_header_t *directory_ids,
                           apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_pool_t *iterpool;
  int i;

  if (!ffd->dir_cache)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < directory_ids->nelts; ++i)
    {
      const pair_cache_key_t *key
        = &APR_ARRAY_IDX(directory_ids, i, pair_cache_key_t);

      svn_pool_clear(iterpool);

      /* Currently, the entry for KEY - if it still exists - is marked
       * as "stale" and would not be used.  Mark it as current for in-
       * revison data. */
      SVN_ERR(svn_cache__set_partial(ffd->dir_cache, key,
                                     svn_fs_fs__reset_txn_filesize, NULL,
                                     iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Baton used for commit_body below. */
struct commit_baton {
  svn_revnum_t *new_rev_p;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  apr_array_header_t *reps_to_cache;
  apr_hash_t *reps_hash;
  apr_pool_t *reps_pool;
};

/* The work-horse for svn_fs_fs__commit, called with the FS write lock.
   This implements the svn_fs_fs__with_write_lock() 'body' callback
   type.  BATON is a 'struct commit_baton *'. */
static svn_error_t *
commit_body(void *baton, apr_pool_t *pool)
{
  struct commit_baton *cb = baton;
  fs_fs_data_t *ffd = cb->fs->fsap_data;
  const char *old_rev_filename, *rev_filename, *proto_filename;
  const char *revprop_filename;
  const svn_fs_id_t *root_id, *new_root_id;
  apr_uint64_t start_node_id;
  apr_uint64_t start_copy_id;
  svn_revnum_t old_rev, new_rev;
  apr_file_t *proto_file;
  void *proto_file_lockcookie;
  apr_off_t initial_offset, changed_path_offset;
  const svn_fs_fs__id_part_t *txn_id = svn_fs_fs__txn_get_id(cb->txn);
  apr_hash_t *changed_paths;
  apr_array_header_t *directory_ids = apr_array_make(pool, 4,
                                                     sizeof(pair_cache_key_t));

  /* Re-Read the current repository format.  All our repo upgrade and
     config evaluation strategies are such that existing information in
     FS and FFD remains valid.

     Although we don't recommend upgrading hot repositories, people may
     still do it and we must make sure to either handle them gracefully
     or to error out.

     Committing pre-format 3 txns will fail after upgrade to format 3+
     because the proto-rev cannot be found; no further action needed.
     Upgrades from pre-f7 to f7+ means a potential change in addressing
     mode for the final rev.  We must be sure to detect that cause because
     the failure would only manifest once the new revision got committed.
   */
  SVN_ERR(svn_fs_fs__read_format_file(cb->fs, pool));

  /* Read the current youngest revision and, possibly, the next available
     node id and copy id (for old format filesystems).  Update the cached
     value for the youngest revision, because we have just checked it. */
  SVN_ERR(svn_fs_fs__read_current(&old_rev, &start_node_id, &start_copy_id,
                                  cb->fs, pool));
  ffd->youngest_rev_cache = old_rev;

  /* Check to make sure this transaction is based off the most recent
     revision. */
  if (cb->txn->base_rev != old_rev)
    return svn_error_create(SVN_ERR_FS_TXN_OUT_OF_DATE, NULL,
                            _("Transaction out of date"));

  /* We need the changes list for verification as well as for writing it
     to the final rev file. */
  SVN_ERR(svn_fs_fs__txn_changes_fetch(&changed_paths, cb->fs, txn_id,
                                       pool));

  /* Locks may have been added (or stolen) between the calling of
     previous svn_fs.h functions and svn_fs_commit_txn(), so we need
     to re-examine every changed-path in the txn and re-verify all
     discovered locks. */
  SVN_ERR(verify_locks(cb->fs, txn_id, changed_paths, pool));

  /* We are going to be one better than this puny old revision. */
  new_rev = old_rev + 1;

  /* Get a write handle on the proto revision file. */
  SVN_ERR(get_writable_proto_rev(&proto_file, &proto_file_lockcookie,
                                 cb->fs, txn_id, pool));
  SVN_ERR(svn_io_file_get_offset(&initial_offset, proto_file, pool));

  /* Write out all the node-revisions and directory contents. */
  root_id = svn_fs_fs__id_txn_create_root(txn_id, pool);
  SVN_ERR(write_final_rev(&new_root_id, proto_file, new_rev, cb->fs, root_id,
                          start_node_id, start_copy_id, initial_offset,
                          directory_ids, cb->reps_to_cache, cb->reps_hash,
                          cb->reps_pool, TRUE, pool));

  /* Write the changed-path information. */
  SVN_ERR(write_final_changed_path_info(&changed_path_offset, proto_file,
                                        cb->fs, txn_id, changed_paths,
                                        pool));

  if (svn_fs_fs__use_log_addressing(cb->fs))
    {
      /* Append the index data to the rev file. */
      SVN_ERR(svn_fs_fs__add_index_data(cb->fs, proto_file,
                      svn_fs_fs__path_l2p_proto_index(cb->fs, txn_id, pool),
                      svn_fs_fs__path_p2l_proto_index(cb->fs, txn_id, pool),
                      new_rev, pool));
    }
  else
    {
      /* Write the final line. */

      svn_stringbuf_t *trailer
        = svn_fs_fs__unparse_revision_trailer
                  ((apr_off_t)svn_fs_fs__id_item(new_root_id),
                   changed_path_offset,
                   pool);
      SVN_ERR(svn_io_file_write_full(proto_file, trailer->data, trailer->len,
                                     NULL, pool));
    }

  if (ffd->flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(proto_file, pool));
  SVN_ERR(svn_io_file_close(proto_file, pool));

  /* We don't unlock the prototype revision file immediately to avoid a
     race with another caller writing to the prototype revision file
     before we commit it. */

  /* Create the shard for the rev and revprop file, if we're sharding and
     this is the first revision of a new shard.  We don't care if this
     fails because the shard already existed for some reason. */
  if (ffd->max_files_per_dir && new_rev % ffd->max_files_per_dir == 0)
    {
      /* Create the revs shard. */
        {
          const char *new_dir
            = svn_fs_fs__path_rev_shard(cb->fs, new_rev, pool);
          svn_error_t *err
            = svn_io_dir_make(new_dir, APR_OS_DEFAULT, pool);
          if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
            return svn_error_trace(err);
          svn_error_clear(err);
          SVN_ERR(svn_io_copy_perms(svn_dirent_join(cb->fs->path,
                                                    PATH_REVS_DIR,
                                                    pool),
                                    new_dir, pool));
        }

      /* Create the revprops shard. */
      SVN_ERR_ASSERT(! svn_fs_fs__is_packed_revprop(cb->fs, new_rev));
        {
          const char *new_dir
            = svn_fs_fs__path_revprops_shard(cb->fs, new_rev, pool);
          svn_error_t *err
            = svn_io_dir_make(new_dir, APR_OS_DEFAULT, pool);
          if (err && !APR_STATUS_IS_EEXIST(err->apr_err))
            return svn_error_trace(err);
          svn_error_clear(err);
          SVN_ERR(svn_io_copy_perms(svn_dirent_join(cb->fs->path,
                                                    PATH_REVPROPS_DIR,
                                                    pool),
                                    new_dir, pool));
        }
    }

  /* Move the finished rev file into place.

     ### This "breaks" the transaction by removing the protorev file
     ### but the revision is not yet complete.  If this commit does
     ### not complete for any reason the transaction will be lost. */
  old_rev_filename = svn_fs_fs__path_rev_absolute(cb->fs, old_rev, pool);
  rev_filename = svn_fs_fs__path_rev(cb->fs, new_rev, pool);
  proto_filename = svn_fs_fs__path_txn_proto_rev(cb->fs, txn_id, pool);
  SVN_ERR(svn_fs_fs__move_into_place(proto_filename, rev_filename,
                                     old_rev_filename, ffd->flush_to_disk,
                                     pool));

  /* Now that we've moved the prototype revision file out of the way,
     we can unlock it (since further attempts to write to the file
     will fail as it no longer exists).  We must do this so that we can
     remove the transaction directory later. */
  SVN_ERR(unlock_proto_rev(cb->fs, txn_id, proto_file_lockcookie, pool));

  /* Write final revprops file. */
  SVN_ERR_ASSERT(! svn_fs_fs__is_packed_revprop(cb->fs, new_rev));
  revprop_filename = svn_fs_fs__path_revprops(cb->fs, new_rev, pool);
  SVN_ERR(write_final_revprop(revprop_filename, old_rev_filename,
                              cb->txn, ffd->flush_to_disk, pool));

  /* Run paranoia checks. */
  if (ffd->verify_before_commit)
    {
      SVN_ERR(verify_before_commit(cb->fs, new_rev, pool));
    }

  /* Update the 'current' file. */
  SVN_ERR(write_final_current(cb->fs, txn_id, new_rev, start_node_id,
                              start_copy_id, pool));

  /* At this point the new revision is committed and globally visible
     so let the caller know it succeeded by giving it the new revision
     number, which fulfills svn_fs_commit_txn() contract.  Any errors
     after this point do not change the fact that a new revision was
     created. */
  *cb->new_rev_p = new_rev;

  ffd->youngest_rev_cache = new_rev;

  /* Make the directory contents alreday cached for the new revision
   * visible. */
  SVN_ERR(promote_cached_directories(cb->fs, directory_ids, pool));

  /* Remove this transaction directory. */
  SVN_ERR(svn_fs_fs__purge_txn(cb->fs, cb->txn->id, pool));

  return SVN_NO_ERROR;
}

/* Add the representations in REPS_TO_CACHE (an array of representation_t *)
 * to the rep-cache database of FS. */
static svn_error_t *
write_reps_to_cache(svn_fs_t *fs,
                    const apr_array_header_t *reps_to_cache,
                    apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < reps_to_cache->nelts; i++)
    {
      representation_t *rep = APR_ARRAY_IDX(reps_to_cache, i, representation_t *);

      SVN_ERR(svn_fs_fs__set_rep_reference(fs, rep, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__commit(svn_revnum_t *new_rev_p,
                  svn_fs_t *fs,
                  svn_fs_txn_t *txn,
                  apr_pool_t *pool)
{
  struct commit_baton cb;
  fs_fs_data_t *ffd = fs->fsap_data;

  cb.new_rev_p = new_rev_p;
  cb.fs = fs;
  cb.txn = txn;

  if (ffd->rep_sharing_allowed)
    {
      cb.reps_to_cache = apr_array_make(pool, 5, sizeof(representation_t *));
      cb.reps_hash = apr_hash_make(pool);
      cb.reps_pool = pool;
    }
  else
    {
      cb.reps_to_cache = NULL;
      cb.reps_hash = NULL;
      cb.reps_pool = NULL;
    }

  SVN_ERR(svn_fs_fs__with_write_lock(fs, commit_body, &cb, pool));

  /* At this point, *NEW_REV_P has been set, so errors below won't affect
     the success of the commit.  (See svn_fs_commit_txn().)  */

  if (ffd->rep_sharing_allowed)
    {
      svn_error_t *err;

      SVN_ERR(svn_fs_fs__open_rep_cache(fs, pool));

      /* Write new entries to the rep-sharing database.
       *
       * We use an sqlite transaction to speed things up;
       * see <http://www.sqlite.org/faq.html#q19>.
       */
      /* ### A commit that touches thousands of files will starve other
             (reader/writer) commits for the duration of the below call.
             Maybe write in batches? */
      SVN_ERR(svn_sqlite__begin_transaction(ffd->rep_cache_db));
      err = write_reps_to_cache(fs, cb.reps_to_cache, pool);
      err = svn_sqlite__finish_transaction(ffd->rep_cache_db, err);

      if (svn_error_find_cause(err, SVN_ERR_SQLITE_ROLLBACK_FAILED))
        {
          /* Failed rollback means that our db connection is unusable, and
             the only thing we can do is close it.  The connection will be
             reopened during the next operation with rep-cache.db. */
          return svn_error_trace(
              svn_error_compose_create(err,
                                       svn_fs_fs__close_rep_cache(fs)));
        }
      else if (err)
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__list_transactions(apr_array_header_t **names_p,
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
  txn_dir = svn_fs_fs__path_txns_dir(fs, pool);

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
svn_fs_fs__open_txn(svn_fs_txn_t **txn_p,
                    svn_fs_t *fs,
                    const char *name,
                    apr_pool_t *pool)
{
  svn_fs_txn_t *txn;
  fs_txn_data_t *ftd;
  svn_node_kind_t kind;
  transaction_t *local_txn;
  svn_fs_fs__id_part_t txn_id;

  SVN_ERR(svn_fs_fs__id_txn_parse(&txn_id, name));

  /* First check to see if the directory exists. */
  SVN_ERR(svn_io_check_path(svn_fs_fs__path_txn_dir(fs, &txn_id, pool),
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

  SVN_ERR(svn_fs_fs__get_txn(&local_txn, fs, &txn_id, pool));

  txn->base_rev = svn_fs_fs__id_rev(local_txn->base_id);

  txn->vtable = &txn_vtable;
  txn->fsap_data = ftd;
  *txn_p = txn;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__txn_proplist(apr_hash_t **table_p,
                        svn_fs_txn_t *txn,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist = apr_hash_make(pool);
  SVN_ERR(get_txn_proplist(proplist, txn->fs, svn_fs_fs__txn_get_id(txn),
                           pool));
  *table_p = proplist;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__delete_node_revision(svn_fs_t *fs,
                                const svn_fs_id_t *id,
                                apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(svn_fs_fs__get_node_revision(&noderev, fs, id, pool, pool));

  /* Delete any mutable property representation. */
  if (noderev->prop_rep && is_txn_rep(noderev->prop_rep))
    SVN_ERR(svn_io_remove_file2(svn_fs_fs__path_txn_node_props(fs, id, pool),
                                FALSE, pool));

  /* Delete any mutable data representation. */
  if (noderev->data_rep && is_txn_rep(noderev->data_rep)
      && noderev->kind == svn_node_dir)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      SVN_ERR(svn_io_remove_file2(svn_fs_fs__path_txn_node_children(fs, id,
                                                                    pool),
                                  FALSE, pool));

      /* remove the corresponding entry from the cache, if such exists */
      if (ffd->txn_dir_cache)
        {
          const char *key = svn_fs_fs__id_unparse(id, pool)->data;
          SVN_ERR(svn_cache__set(ffd->txn_dir_cache, key, NULL, pool));
        }
    }

  return svn_io_remove_file2(svn_fs_fs__path_txn_node_rev(fs, id, pool),
                             FALSE, pool);
}



/*** Transactions ***/

svn_error_t *
svn_fs_fs__get_txn_ids(const svn_fs_id_t **root_id_p,
                       const svn_fs_id_t **base_root_id_p,
                       svn_fs_t *fs,
                       const svn_fs_fs__id_part_t *txn_id,
                       apr_pool_t *pool)
{
  transaction_t *txn;
  SVN_ERR(svn_fs_fs__get_txn(&txn, fs, txn_id, pool));
  *root_id_p = txn->root_id;
  *base_root_id_p = txn->base_id;
  return SVN_NO_ERROR;
}


/* Generic transaction operations.  */

svn_error_t *
svn_fs_fs__txn_prop(svn_string_t **value_p,
                    svn_fs_txn_t *txn,
                    const char *propname,
                    apr_pool_t *pool)
{
  apr_hash_t *table;
  svn_fs_t *fs = txn->fs;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_fs__txn_proplist(&table, txn, pool));

  *value_p = svn_hash_gets(table, propname);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__begin_txn(svn_fs_txn_t **txn_p,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_uint32_t flags,
                     apr_pool_t *pool)
{
  svn_string_t date;
  fs_txn_data_t *ftd;
  apr_hash_t *props = apr_hash_make(pool);

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  SVN_ERR(svn_fs_fs__create_txn(txn_p, fs, rev, pool));

  /* Put a datestamp on the newly created txn, so we always know
     exactly how old it is.  (This will help sysadmins identify
     long-abandoned txns that may need to be manually removed.)  When
     a txn is promoted to a revision, this property will be
     automatically overwritten with a revision datestamp. */
  date.data = svn_time_to_cstring(apr_time_now(), pool);
  date.len = strlen(date.data);

  svn_hash_sets(props, SVN_PROP_REVISION_DATE, &date);

  /* Set temporary txn props that represent the requested 'flags'
     behaviors. */
  if (flags & SVN_FS_TXN_CHECK_OOD)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_OOD,
                  svn_string_create("true", pool));

  if (flags & SVN_FS_TXN_CHECK_LOCKS)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CHECK_LOCKS,
                  svn_string_create("true", pool));

  if (flags & SVN_FS_TXN_CLIENT_DATE)
    svn_hash_sets(props, SVN_FS__PROP_TXN_CLIENT_DATE,
                  svn_string_create("0", pool));

  ftd = (*txn_p)->fsap_data;
  return svn_error_trace(set_txn_proplist(fs, &ftd->txn_id, props, pool));
}
