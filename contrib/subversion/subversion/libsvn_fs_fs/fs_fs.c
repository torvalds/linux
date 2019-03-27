/* fs_fs.c --- filesystem operations specific to fs_fs
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

#include "fs_fs.h"

#include <apr_uuid.h>

#include "svn_private_config.h"

#include "svn_checksum.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "svn_version.h"

#include "cached_data.h"
#include "id.h"
#include "index.h"
#include "rep-cache.h"
#include "revprops.h"
#include "transaction.h"
#include "tree.h"
#include "util.h"

#include "private/svn_fs_util.h"
#include "private/svn_io_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "../libsvn_fs/fs-loader.h"

/* The default maximum number of files per directory to store in the
   rev and revprops directory.  The number below is somewhat arbitrary,
   and can be overridden by defining the macro while compiling; the
   figure of 1000 is reasonable for VFAT filesystems, which are by far
   the worst performers in this area. */
#ifndef SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR
#define SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR 1000
#endif

/* Begin deltification after a node history exceeded this this limit.
   Useful values are 4 to 64 with 16 being a good compromise between
   computational overhead and repository size savings.
   Should be a power of 2.
   Values < 2 will result in standard skip-delta behavior. */
#define SVN_FS_FS_MAX_LINEAR_DELTIFICATION 16

/* Finding a deltification base takes operations proportional to the
   number of changes being skipped. To prevent exploding runtime
   during commits, limit the deltification range to this value.
   Should be a power of 2 minus one.
   Values < 1 disable deltification. */
#define SVN_FS_FS_MAX_DELTIFICATION_WALK 1023

/* Notes:

To avoid opening and closing the rev-files all the time, it would
probably be advantageous to keep each rev-file open for the
lifetime of the transaction object.  I'll leave that as a later
optimization for now.

I didn't keep track of pool lifetimes at all in this code.  There
are likely some errors because of that.

*/

/* Declarations. */

static svn_error_t *
get_youngest(svn_revnum_t *youngest_p, svn_fs_t *fs, apr_pool_t *pool);

/* Pathname helper functions */

static const char *
path_format(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_FORMAT, pool);
}

static APR_INLINE const char *
path_uuid(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_UUID, pool);
}

const char *
svn_fs_fs__path_current(svn_fs_t *fs, apr_pool_t *pool)
{
  return svn_dirent_join(fs->path, PATH_CURRENT, pool);
}



/* Get a lock on empty file LOCK_FILENAME, creating it in POOL. */
static svn_error_t *
get_lock_on_filesystem(const char *lock_filename,
                       apr_pool_t *pool)
{
  return svn_error_trace(svn_io__file_lock_autocreate(lock_filename, pool));
}

/* Reset the HAS_WRITE_LOCK member in the FFD given as BATON_VOID.
   When registered with the pool holding the lock on the lock file,
   this makes sure the flag gets reset just before we release the lock. */
static apr_status_t
reset_lock_flag(void *baton_void)
{
  fs_fs_data_t *ffd = baton_void;
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
                       apr_pool_t *pool);

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
      fs_fs_data_t *ffd = fs->fsap_data;

      if (baton->is_global_lock)
        {
          /* set the "got the lock" flag and register reset function */
          apr_pool_cleanup_register(pool,
                                    ffd,
                                    reset_lock_flag,
                                    apr_pool_cleanup_null);
          ffd->has_write_lock = TRUE;
        }

      /* nobody else will modify the repo state
         => read HEAD & pack info once */
      if (baton->is_inner_most_lock)
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            err = svn_fs_fs__update_min_unpacked_rev(fs, pool);
          if (!err)
            err = get_youngest(&ffd->youngest_rev_cache, fs, pool);
        }

      if (!err)
        err = baton->body(baton->baton, pool);
    }

  if (baton->is_outer_most_lock)
    svn_pool_destroy(pool);

  return svn_error_trace(err);
}

/* Wraps with_some_lock_file, protecting it with BATON->MUTEX.

   POOL is unused here and only provided for signature compatibility with
   WITH_LOCK_BATON_T.BODY. */
static svn_error_t *
with_lock(void *baton,
          apr_pool_t *pool)
{
  with_lock_baton_t *lock_baton = baton;
  SVN_MUTEX__WITH_LOCK(lock_baton->mutex, with_some_lock_file(lock_baton));

  return SVN_NO_ERROR;
}

/* Enum identifying a filesystem lock. */
typedef enum lock_id_t
{
  write_lock,
  txn_lock,
  pack_lock
} lock_id_t;

/* Initialize BATON->MUTEX, BATON->LOCK_PATH and BATON->IS_GLOBAL_LOCK
   according to the LOCK_ID.  All other members of BATON must already be
   valid. */
static void
init_lock_baton(with_lock_baton_t *baton,
                lock_id_t lock_id)
{
  fs_fs_data_t *ffd = baton->fs->fsap_data;
  fs_fs_shared_data_t *ffsd = ffd->shared;

  switch (lock_id)
    {
    case write_lock:
      baton->mutex = ffsd->fs_write_lock;
      baton->lock_path = svn_fs_fs__path_lock(baton->fs, baton->lock_pool);
      baton->is_global_lock = TRUE;
      break;

    case txn_lock:
      baton->mutex = ffsd->txn_current_lock;
      baton->lock_path = svn_fs_fs__path_txn_current_lock(baton->fs,
                                                          baton->lock_pool);
      baton->is_global_lock = FALSE;
      break;

    case pack_lock:
      baton->mutex = ffsd->fs_pack_lock;
      baton->lock_path = svn_fs_fs__path_pack_lock(baton->fs,
                                                   baton->lock_pool);
      baton->is_global_lock = FALSE;
      break;
    }
}

/* Return the  baton for the innermost lock of a (potential) lock chain.
   The baton shall take out LOCK_ID from FS and execute BODY with BATON
   while the lock is being held.  Allocate the result in a sub-pool of POOL.
 */
static with_lock_baton_t *
create_lock_baton(svn_fs_t *fs,
                  lock_id_t lock_id,
                  svn_error_t *(*body)(void *baton,
                                       apr_pool_t *pool),
                  void *baton,
                  apr_pool_t *pool)
{
  /* Allocate everything along the lock chain into a single sub-pool.
     This minimizes memory usage and cleanup overhead. */
  apr_pool_t *lock_pool = svn_pool_create(pool);
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
svn_fs_fs__with_write_lock(svn_fs_t *fs,
                           svn_error_t *(*body)(void *baton,
                                                apr_pool_t *pool),
                           void *baton,
                           apr_pool_t *pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, write_lock, body, baton, pool),
                     pool));
}

svn_error_t *
svn_fs_fs__with_pack_lock(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *pool),
                          void *baton,
                          apr_pool_t *pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, pack_lock, body, baton, pool),
                     pool));
}

svn_error_t *
svn_fs_fs__with_txn_current_lock(svn_fs_t *fs,
                                 svn_error_t *(*body)(void *baton,
                                                      apr_pool_t *pool),
                                 void *baton,
                                 apr_pool_t *pool)
{
  return svn_error_trace(
           with_lock(create_lock_baton(fs, txn_lock, body, baton, pool),
                     pool));
}

svn_error_t *
svn_fs_fs__with_all_locks(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *pool),
                          void *baton,
                          apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Be sure to use the correct lock ordering as documented in
     fs_fs_shared_data_t.  The lock chain is being created in
     innermost (last to acquire) -> outermost (first to acquire) order. */
  with_lock_baton_t *lock_baton
    = create_lock_baton(fs, write_lock, body, baton, pool);

  if (ffd->format >= SVN_FS_FS__MIN_PACK_LOCK_FORMAT)
    lock_baton = chain_lock_baton(pack_lock, lock_baton);

  if (ffd->format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    lock_baton = chain_lock_baton(txn_lock, lock_baton);

  return svn_error_trace(with_lock(lock_baton, pool));
}





/* Check that BUF, a nul-terminated buffer of text from format file PATH,
   contains only digits at OFFSET and beyond, raising an error if not.

   Uses POOL for temporary allocation. */
static svn_error_t *
check_format_file_buffer_numeric(const char *buf, apr_off_t offset,
                                 const char *path, apr_pool_t *pool)
{
  return svn_fs_fs__check_file_buffer_numeric(buf, offset, path, "Format",
                                              pool);
}

/* Return the error SVN_ERR_FS_UNSUPPORTED_FORMAT if FS's format
   number is not the same as a format number supported by this
   Subversion. */
static svn_error_t *
check_format(int format)
{
  /* Blacklist.  These formats may be either younger or older than
     SVN_FS_FS__FORMAT_NUMBER, but we don't support them. */
  if (format == SVN_FS_FS__PACKED_REVPROP_SQLITE_DEV_FORMAT)
    return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                             _("Found format '%d', only created by "
                               "unreleased dev builds; see "
                               "http://subversion.apache.org"
                               "/docs/release-notes/1.7#revprop-packing"),
                             format);

  /* We support all formats from 1-current simultaneously */
  if (1 <= format && format <= SVN_FS_FS__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
     _("Expected FS format between '1' and '%d'; found format '%d'"),
     SVN_FS_FS__FORMAT_NUMBER, format);
}

/* Read the format number and maximum number of files per directory
   from PATH and return them in *PFORMAT, *MAX_FILES_PER_DIR and
   USE_LOG_ADDRESSIONG respectively.

   *MAX_FILES_PER_DIR is obtained from the 'layout' format option, and
   will be set to zero if a linear scheme should be used.
   *USE_LOG_ADDRESSIONG is obtained from the 'addressing' format option,
   and will be set to FALSE for physical addressing.

   Use POOL for temporary allocation. */
static svn_error_t *
read_format(int *pformat,
            int *max_files_per_dir,
            svn_boolean_t *use_log_addressing,
            const char *path,
            apr_pool_t *pool)
{
  svn_error_t *err;
  svn_stream_t *stream;
  svn_stringbuf_t *content;
  svn_stringbuf_t *buf;
  svn_boolean_t eos = FALSE;

  err = svn_stringbuf_from_file2(&content, path, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* Treat an absent format file as format 1.  Do not try to
         create the format file on the fly, because the repository
         might be read-only for us, or this might be a read-only
         operation, and the spirit of FSFS is to make no changes
         whatseover in read-only operations.  See thread starting at
         http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=97600
         for more. */
      svn_error_clear(err);
      *pformat = 1;
      *max_files_per_dir = 0;
      *use_log_addressing = FALSE;

      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  stream = svn_stream_from_stringbuf(content, pool);
  SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, pool));
  if (buf->len == 0 && eos)
    {
      /* Return a more useful error message. */
      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
                               _("Can't read first line of format file '%s'"),
                               svn_dirent_local_style(path, pool));
    }

  /* Check that the first line contains only digits. */
  SVN_ERR(check_format_file_buffer_numeric(buf->data, 0, path, pool));
  SVN_ERR(svn_cstring_atoi(pformat, buf->data));

  /* Check that we support this format at all */
  SVN_ERR(check_format(*pformat));

  /* Set the default values for anything that can be set via an option. */
  *max_files_per_dir = 0;
  *use_log_addressing = FALSE;

  /* Read any options. */
  while (!eos)
    {
      SVN_ERR(svn_stream_readline(stream, &buf, "\n", &eos, pool));
      if (buf->len == 0)
        break;

      if (*pformat >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT &&
          strncmp(buf->data, "layout ", 7) == 0)
        {
          if (strcmp(buf->data + 7, "linear") == 0)
            {
              *max_files_per_dir = 0;
              continue;
            }

          if (strncmp(buf->data + 7, "sharded ", 8) == 0)
            {
              /* Check that the argument is numeric. */
              SVN_ERR(check_format_file_buffer_numeric(buf->data, 15, path, pool));
              SVN_ERR(svn_cstring_atoi(max_files_per_dir, buf->data + 15));
              continue;
            }
        }

      if (*pformat >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT &&
          strncmp(buf->data, "addressing ", 11) == 0)
        {
          if (strcmp(buf->data + 11, "physical") == 0)
            {
              *use_log_addressing = FALSE;
              continue;
            }

          if (strcmp(buf->data + 11, "logical") == 0)
            {
              *use_log_addressing = TRUE;
              continue;
            }
        }

      return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
         _("'%s' contains invalid filesystem format option '%s'"),
         svn_dirent_local_style(path, pool), buf->data);
    }

  /* Non-sharded repositories never use logical addressing.
   * If the format file is inconsistent in that respect, something
   * probably went wrong.
   */
  if (*use_log_addressing && !*max_files_per_dir)
    return svn_error_createf(SVN_ERR_BAD_VERSION_FILE_FORMAT, NULL,
       _("'%s' specifies logical addressing for a non-sharded repository"),
       svn_dirent_local_style(path, pool));

  return SVN_NO_ERROR;
}

/* Write the format number, maximum number of files per directory and
   the addressing scheme to a new format file in PATH, possibly expecting
   to overwrite a previously existing file.

   Use POOL for temporary allocation. */
svn_error_t *
svn_fs_fs__write_format(svn_fs_t *fs,
                        svn_boolean_t overwrite,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *sb;
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *path = path_format(fs, pool);

  SVN_ERR_ASSERT(1 <= ffd->format
                 && ffd->format <= SVN_FS_FS__FORMAT_NUMBER);

  sb = svn_stringbuf_createf(pool, "%d\n", ffd->format);

  if (ffd->format >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT)
    {
      if (ffd->max_files_per_dir)
        svn_stringbuf_appendcstr(sb, apr_psprintf(pool, "layout sharded %d\n",
                                                  ffd->max_files_per_dir));
      else
        svn_stringbuf_appendcstr(sb, "layout linear\n");
    }

  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      if (ffd->use_log_addressing)
        svn_stringbuf_appendcstr(sb, "addressing logical\n");
      else
        svn_stringbuf_appendcstr(sb, "addressing physical\n");
    }

  /* svn_io_write_version_file() does a load of magic to allow it to
     replace version files that already exist.  We only need to do
     that when we're allowed to overwrite an existing file. */
  if (! overwrite)
    {
      /* Create the file */
      SVN_ERR(svn_io_file_create(path, sb->data, pool));
    }
  else
    {
      SVN_ERR(svn_io_write_atomic2(path, sb->data, sb->len,
                                   NULL /* copy_perms_path */,
                                   ffd->flush_to_disk, pool));
    }

  /* And set the perms to make it read only */
  return svn_io_set_file_read_only(path, FALSE, pool);
}

svn_boolean_t
svn_fs_fs__fs_supports_mergeinfo(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return ffd->format >= SVN_FS_FS__MIN_MERGEINFO_FORMAT;
}

/* Check that BLOCK_SIZE is a valid block / page size, i.e. it is within
 * the range of what the current system may address in RAM and it is a
 * power of 2.  Assume that the element size within the block is ITEM_SIZE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
verify_block_size(apr_int64_t block_size,
                  apr_size_t item_size,
                  const char *name,
                  apr_pool_t *scratch_pool
                 )
{
  /* Limit range. */
  if (block_size <= 0)
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is too small for fsfs.conf setting '%s'."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  if (block_size > SVN_MAX_OBJECT_SIZE / item_size)
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is too large for fsfs.conf setting '%s'."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  /* Ensure it is a power of two.
   * For positive X,  X & (X-1) will reset the lowest bit set.
   * If the result is 0, at most one bit has been set. */
  if (0 != (block_size & (block_size - 1)))
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                             _("%s is invalid for fsfs.conf setting '%s' "
                               "because it is not a power of 2."),
                             apr_psprintf(scratch_pool,
                                          "%" APR_INT64_T_FMT,
                                          block_size),
                             name);

  return SVN_NO_ERROR;
}

static svn_error_t *
parse_compression_option(compression_type_t *compression_type_p,
                         int *compression_level_p,
                         const char *value)
{
  compression_type_t type;
  int level;
  svn_boolean_t is_valid = TRUE;

  /* compression = none | lz4 | zlib | zlib-1 ... zlib-9 */
  if (strcmp(value, "none") == 0)
    {
      type = compression_type_none;
      level = SVN_DELTA_COMPRESSION_LEVEL_NONE;
    }
  else if (strcmp(value, "lz4") == 0)
    {
      type = compression_type_lz4;
      level = SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;
    }
  else if (strncmp(value, "zlib", 4) == 0)
    {
      const char *p = value + 4;

      type = compression_type_zlib;
      if (*p == 0)
        {
          level = SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;
        }
      else if (*p == '-')
        {
          p++;
          SVN_ERR(svn_cstring_atoi(&level, p));
          if (level < 1 || level > 9)
            is_valid = FALSE;
        }
      else
        is_valid = FALSE;
    }
  else
    {
      is_valid = FALSE;
    }

  if (!is_valid)
    return svn_error_createf(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                           _("Invalid 'compression' value '%s' in the config"),
                             value);

  *compression_type_p = type;
  *compression_level_p = level;
  return SVN_NO_ERROR;
}

/* Read the configuration information of the file system at FS_PATH
 * and set the respective values in FFD.  Use pools as usual.
 */
static svn_error_t *
read_config(fs_fs_data_t *ffd,
            const char *fs_path,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_config_t *config;

  SVN_ERR(svn_config_read3(&config,
                           svn_dirent_join(fs_path, PATH_CONFIG, scratch_pool),
                           FALSE, FALSE, FALSE, scratch_pool));

  /* Initialize ffd->rep_sharing_allowed. */
  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    SVN_ERR(svn_config_get_bool(config, &ffd->rep_sharing_allowed,
                                CONFIG_SECTION_REP_SHARING,
                                CONFIG_OPTION_ENABLE_REP_SHARING, TRUE));
  else
    ffd->rep_sharing_allowed = FALSE;

  /* Initialize deltification settings in ffd. */
  if (ffd->format >= SVN_FS_FS__MIN_DELTIFICATION_FORMAT)
    {
      SVN_ERR(svn_config_get_bool(config, &ffd->deltify_directories,
                                  CONFIG_SECTION_DELTIFICATION,
                                  CONFIG_OPTION_ENABLE_DIR_DELTIFICATION,
                                  TRUE));
      SVN_ERR(svn_config_get_bool(config, &ffd->deltify_properties,
                                  CONFIG_SECTION_DELTIFICATION,
                                  CONFIG_OPTION_ENABLE_PROPS_DELTIFICATION,
                                  TRUE));
      SVN_ERR(svn_config_get_int64(config, &ffd->max_deltification_walk,
                                   CONFIG_SECTION_DELTIFICATION,
                                   CONFIG_OPTION_MAX_DELTIFICATION_WALK,
                                   SVN_FS_FS_MAX_DELTIFICATION_WALK));
      SVN_ERR(svn_config_get_int64(config, &ffd->max_linear_deltification,
                                   CONFIG_SECTION_DELTIFICATION,
                                   CONFIG_OPTION_MAX_LINEAR_DELTIFICATION,
                                   SVN_FS_FS_MAX_LINEAR_DELTIFICATION));
    }
  else
    {
      ffd->deltify_directories = FALSE;
      ffd->deltify_properties = FALSE;
      ffd->max_deltification_walk = SVN_FS_FS_MAX_DELTIFICATION_WALK;
      ffd->max_linear_deltification = SVN_FS_FS_MAX_LINEAR_DELTIFICATION;
    }

  /* Initialize revprop packing settings in ffd. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
    {
      SVN_ERR(svn_config_get_bool(config, &ffd->compress_packed_revprops,
                                  CONFIG_SECTION_PACKED_REVPROPS,
                                  CONFIG_OPTION_COMPRESS_PACKED_REVPROPS,
                                  FALSE));
      SVN_ERR(svn_config_get_int64(config, &ffd->revprop_pack_size,
                                   CONFIG_SECTION_PACKED_REVPROPS,
                                   CONFIG_OPTION_REVPROP_PACK_SIZE,
                                   ffd->compress_packed_revprops
                                       ? 0x40
                                       : 0x10));

      ffd->revprop_pack_size *= 1024;
    }
  else
    {
      ffd->revprop_pack_size = 0x10000;
      ffd->compress_packed_revprops = FALSE;
    }

  if (ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    {
      SVN_ERR(svn_config_get_int64(config, &ffd->block_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_BLOCK_SIZE,
                                   64));
      SVN_ERR(svn_config_get_int64(config, &ffd->l2p_page_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_L2P_PAGE_SIZE,
                                   0x2000));
      SVN_ERR(svn_config_get_int64(config, &ffd->p2l_page_size,
                                   CONFIG_SECTION_IO,
                                   CONFIG_OPTION_P2L_PAGE_SIZE,
                                   0x400));

      /* Don't accept unreasonable or illegal values.
       * Block size and P2L page size are in kbytes;
       * L2P blocks are arrays of apr_off_t. */
      SVN_ERR(verify_block_size(ffd->block_size, 0x400,
                                CONFIG_OPTION_BLOCK_SIZE, scratch_pool));
      SVN_ERR(verify_block_size(ffd->p2l_page_size, 0x400,
                                CONFIG_OPTION_P2L_PAGE_SIZE, scratch_pool));
      SVN_ERR(verify_block_size(ffd->l2p_page_size, sizeof(apr_off_t),
                                CONFIG_OPTION_L2P_PAGE_SIZE, scratch_pool));

      /* convert kBytes to bytes */
      ffd->block_size *= 0x400;
      ffd->p2l_page_size *= 0x400;
      /* L2P pages are in entries - not in (k)Bytes */
    }
  else
    {
      /* should be irrelevant but we initialize them anyway */
      ffd->block_size = 0x1000; /* Matches default APR file buffer size. */
      ffd->l2p_page_size = 0x2000;    /* Matches above default. */
      ffd->p2l_page_size = 0x100000;  /* Matches above default in bytes. */
    }

  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    {
      SVN_ERR(svn_config_get_bool(config, &ffd->pack_after_commit,
                                  CONFIG_SECTION_DEBUG,
                                  CONFIG_OPTION_PACK_AFTER_COMMIT,
                                  FALSE));
    }
  else
    {
      ffd->pack_after_commit = FALSE;
    }

  /* Initialize compression settings in ffd. */
  if (ffd->format >= SVN_FS_FS__MIN_DELTIFICATION_FORMAT)
    {
      const char *compression_val;
      const char *compression_level_val;

      svn_config_get(config, &compression_val,
                     CONFIG_SECTION_DELTIFICATION,
                     CONFIG_OPTION_COMPRESSION, NULL);
      svn_config_get(config, &compression_level_val,
                     CONFIG_SECTION_DELTIFICATION,
                     CONFIG_OPTION_COMPRESSION_LEVEL, NULL);
      if (compression_val && compression_level_val)
        {
          return svn_error_create(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                                  _("The 'compression' and 'compression-level' "
                                    "config options are mutually exclusive"));
        }
      else if (compression_val)
        {
          SVN_ERR(parse_compression_option(&ffd->delta_compression_type,
                                           &ffd->delta_compression_level,
                                           compression_val));
          if (ffd->delta_compression_type == compression_type_lz4 &&
              ffd->format < SVN_FS_FS__MIN_SVNDIFF2_FORMAT)
            {
              return svn_error_create(SVN_ERR_BAD_CONFIG_VALUE, NULL,
                                      _("Compression type 'lz4' requires "
                                        "filesystem format 8 or higher"));
            }
        }
      else if (compression_level_val)
        {
          /* Handle the deprecated 'compression-level' option. */
          ffd->delta_compression_type = compression_type_zlib;
          SVN_ERR(svn_cstring_atoi(&ffd->delta_compression_level,
                                   compression_level_val));
          ffd->delta_compression_level =
            MIN(MAX(SVN_DELTA_COMPRESSION_LEVEL_NONE,
                    ffd->delta_compression_level),
                SVN_DELTA_COMPRESSION_LEVEL_MAX);
        }
      else
        {
          /* Nothing specified explicitly, use the default settings:
           * LZ4 compression for formats supporting it and zlib otherwise. */
          if (ffd->format >= SVN_FS_FS__MIN_SVNDIFF2_FORMAT)
            ffd->delta_compression_type = compression_type_lz4;
          else
            ffd->delta_compression_type = compression_type_zlib;

          ffd->delta_compression_level = SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;
        }
    }
  else if (ffd->format >= SVN_FS_FS__MIN_SVNDIFF1_FORMAT)
    {
      ffd->delta_compression_type = compression_type_zlib;
      ffd->delta_compression_level = SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;
    }
  else
    {
      ffd->delta_compression_type = compression_type_none;
      ffd->delta_compression_level = SVN_DELTA_COMPRESSION_LEVEL_NONE;
    }

#ifdef SVN_DEBUG
  SVN_ERR(svn_config_get_bool(config, &ffd->verify_before_commit,
                              CONFIG_SECTION_DEBUG,
                              CONFIG_OPTION_VERIFY_BEFORE_COMMIT,
                              TRUE));
#else
  SVN_ERR(svn_config_get_bool(config, &ffd->verify_before_commit,
                              CONFIG_SECTION_DEBUG,
                              CONFIG_OPTION_VERIFY_BEFORE_COMMIT,
                              FALSE));
#endif

  /* memcached configuration */
  SVN_ERR(svn_cache__make_memcache_from_config(&ffd->memcache, config,
                                               result_pool, scratch_pool));

  SVN_ERR(svn_config_get_bool(config, &ffd->fail_stop,
                              CONFIG_SECTION_CACHES, CONFIG_OPTION_FAIL_STOP,
                              FALSE));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_config(svn_fs_t *fs,
             apr_pool_t *pool)
{
#define NL APR_EOL_STR
  static const char * const fsfs_conf_contents =
"### This file controls the configuration of the FSFS filesystem."           NL
""                                                                           NL
"[" SVN_CACHE_CONFIG_CATEGORY_MEMCACHED_SERVERS "]"                          NL
"### These options name memcached servers used to cache internal FSFS"       NL
"### data.  See http://www.danga.com/memcached/ for more information on"     NL
"### memcached.  To use memcached with FSFS, run one or more memcached"      NL
"### servers, and specify each of them as an option like so:"                NL
"# first-server = 127.0.0.1:11211"                                           NL
"# remote-memcached = mymemcached.corp.example.com:11212"                    NL
"### The option name is ignored; the value is of the form HOST:PORT."        NL
"### memcached servers can be shared between multiple repositories;"         NL
"### however, if you do this, you *must* ensure that repositories have"      NL
"### distinct UUIDs and paths, or else cached data from one repository"      NL
"### might be used by another accidentally.  Note also that memcached has"   NL
"### no authentication for reads or writes, so you must ensure that your"    NL
"### memcached servers are only accessible by trusted users."                NL
""                                                                           NL
"[" CONFIG_SECTION_CACHES "]"                                                NL
"### When a cache-related error occurs, normally Subversion ignores it"      NL
"### and continues, logging an error if the server is appropriately"         NL
"### configured (and ignoring it with file:// access).  To make"             NL
"### Subversion never ignore cache errors, uncomment this line."             NL
"# " CONFIG_OPTION_FAIL_STOP " = true"                                       NL
""                                                                           NL
"[" CONFIG_SECTION_REP_SHARING "]"                                           NL
"### To conserve space, the filesystem can optionally avoid storing"         NL
"### duplicate representations.  This comes at a slight cost in"             NL
"### performance, as maintaining a database of shared representations can"   NL
"### increase commit times.  The space savings are dependent upon the size"  NL
"### of the repository, the number of objects it contains and the amount of" NL
"### duplication between them, usually a function of the branching and"      NL
"### merging process."                                                       NL
"###"                                                                        NL
"### The following parameter enables rep-sharing in the repository.  It can" NL
"### be switched on and off at will, but for best space-saving results"      NL
"### should be enabled consistently over the life of the repository."        NL
"### 'svnadmin verify' will check the rep-cache regardless of this setting." NL
"### rep-sharing is enabled by default."                                     NL
"# " CONFIG_OPTION_ENABLE_REP_SHARING " = true"                              NL
""                                                                           NL
"[" CONFIG_SECTION_DELTIFICATION "]"                                         NL
"### To conserve space, the filesystem stores data as differences against"   NL
"### existing representations.  This comes at a slight cost in performance," NL
"### as calculating differences can increase commit times.  Reading data"    NL
"### will also create higher CPU load and the data will be fragmented."      NL
"### Since deltification tends to save significant amounts of disk space,"   NL
"### the overall I/O load can actually be lower."                            NL
"###"                                                                        NL
"### The options in this section allow for tuning the deltification"         NL
"### strategy.  Their effects on data size and server performance may vary"  NL
"### from one repository to another.  Versions prior to 1.8 will ignore"     NL
"### this section."                                                          NL
"###"                                                                        NL
"### The following parameter enables deltification for directories. It can"  NL
"### be switched on and off at will, but for best space-saving results"      NL
"### should be enabled consistently over the lifetime of the repository."    NL
"### Repositories containing large directories will benefit greatly."        NL
"### In rarely accessed repositories, the I/O overhead may be significant"   NL
"### as caches will most likely be low."                                     NL
"### directory deltification is enabled by default."                         NL
"# " CONFIG_OPTION_ENABLE_DIR_DELTIFICATION " = true"                        NL
"###"                                                                        NL
"### The following parameter enables deltification for properties on files"  NL
"### and directories.  Overall, this is a minor tuning option but can save"  NL
"### some disk space if you merge frequently or frequently change node"      NL
"### properties.  You should not activate this if rep-sharing has been"      NL
"### disabled because this may result in a net increase in repository size." NL
"### property deltification is enabled by default."                          NL
"# " CONFIG_OPTION_ENABLE_PROPS_DELTIFICATION " = true"                      NL
"###"                                                                        NL
"### During commit, the server may need to walk the whole change history of" NL
"### of a given node to find a suitable deltification base.  This linear"    NL
"### process can impact commit times, svnadmin load and similar operations." NL
"### This setting limits the depth of the deltification history.  If the"    NL
"### threshold has been reached, the node will be stored as fulltext and a"  NL
"### new deltification history begins."                                      NL
"### Note, this is unrelated to svn log."                                    NL
"### Very large values rarely provide significant additional savings but"    NL
"### can impact performance greatly - in particular if directory"            NL
"### deltification has been activated.  Very small values may be useful in"  NL
"### repositories that are dominated by large, changing binaries."           NL
"### Should be a power of two minus 1.  A value of 0 will effectively"       NL
"### disable deltification."                                                 NL
"### For 1.8, the default value is 1023; earlier versions have no limit."    NL
"# " CONFIG_OPTION_MAX_DELTIFICATION_WALK " = 1023"                          NL
"###"                                                                        NL
"### The skip-delta scheme used by FSFS tends to repeatably store redundant" NL
"### delta information where a simple delta against the latest version is"   NL
"### often smaller.  By default, 1.8+ will therefore use skip deltas only"   NL
"### after the linear chain of deltas has grown beyond the threshold"        NL
"### specified by this setting."                                             NL
"### Values up to 64 can result in some reduction in repository size for"    NL
"### the cost of quickly increasing I/O and CPU costs. Similarly, smaller"   NL
"### numbers can reduce those costs at the cost of more disk space.  For"    NL
"### rarely read repositories or those containing larger binaries, this may" NL
"### present a better trade-off."                                            NL
"### Should be a power of two.  A value of 1 or smaller will cause the"      NL
"### exclusive use of skip-deltas (as in pre-1.8)."                          NL
"### For 1.8, the default value is 16; earlier versions use 1."              NL
"# " CONFIG_OPTION_MAX_LINEAR_DELTIFICATION " = 16"                          NL
"###"                                                                        NL
"### After deltification, we compress the data to minimize on-disk size."    NL
"### This setting controls the compression algorithm, which will be used in" NL
"### future revisions.  It can be used to either disable compression or to"  NL
"### select between available algorithms (zlib, lz4).  zlib is a general-"   NL
"### purpose compression algorithm.  lz4 is a fast compression algorithm"    NL
"### which should be preferred for repositories with large and, possibly,"   NL
"### incompressible files.  Note that the compression ratio of lz4 is"       NL
"### usually lower than the one provided by zlib, but using it can"          NL
"### significantly speed up commits as well as reading the data."            NL
"### lz4 compression algorithm is supported, starting from format 8"         NL
"### repositories, available in Subversion 1.10 and higher."                 NL
"### The syntax of this option is:"                                          NL
"###   " CONFIG_OPTION_COMPRESSION " = none | lz4 | zlib | zlib-1 ... zlib-9" NL
"### Versions prior to Subversion 1.10 will ignore this option."             NL
"### The default value is 'lz4' if supported by the repository format and"   NL
"### 'zlib' otherwise.  'zlib' is currently equivalent to 'zlib-5'."         NL
"# " CONFIG_OPTION_COMPRESSION " = lz4"                                      NL
"###"                                                                        NL
"### DEPRECATED: The new '" CONFIG_OPTION_COMPRESSION "' option deprecates previously used" NL
"### '" CONFIG_OPTION_COMPRESSION_LEVEL "' option, which was used to configure zlib compression." NL
"### For compatibility with previous versions of Subversion, this option can"NL
"### still be used (and it will result in zlib compression with the"         NL
"### corresponding compression level)."                                      NL
"###   " CONFIG_OPTION_COMPRESSION_LEVEL " = 0 ... 9 (default is 5)"         NL
""                                                                           NL
"[" CONFIG_SECTION_PACKED_REVPROPS "]"                                       NL
"### This parameter controls the size (in kBytes) of packed revprop files."  NL
"### Revprops of consecutive revisions will be concatenated into a single"   NL
"### file up to but not exceeding the threshold given here.  However, each"  NL
"### pack file may be much smaller and revprops of a single revision may be" NL
"### much larger than the limit set here.  The threshold will be applied"    NL
"### before optional compression takes place."                               NL
"### Large values will reduce disk space usage at the expense of increased"  NL
"### latency and CPU usage reading and changing individual revprops."        NL
"### Values smaller than 4 kByte will not improve latency any further and "  NL
"### quickly render revprop packing ineffective."                            NL
"### revprop-pack-size is 16 kBytes by default for non-compressed revprop"   NL
"### pack files and 64 kBytes when compression has been enabled."            NL
"# " CONFIG_OPTION_REVPROP_PACK_SIZE " = 16"                                 NL
"###"                                                                        NL
"### To save disk space, packed revprop files may be compressed.  Standard"  NL
"### revprops tend to allow for very effective compression.  Reading and"    NL
"### even more so writing, become significantly more CPU intensive."         NL
"### Compressing packed revprops is disabled by default."                    NL
"# " CONFIG_OPTION_COMPRESS_PACKED_REVPROPS " = false"                       NL
""                                                                           NL
"[" CONFIG_SECTION_IO "]"                                                    NL
"### Parameters in this section control the data access granularity in"      NL
"### format 7 repositories and later.  The defaults should translate into"   NL
"### decent performance over a wide range of setups."                        NL
"###"                                                                        NL
"### When a specific piece of information needs to be read from disk,  a"    NL
"### data block is being read at once and its contents are being cached."    NL
"### If the repository is being stored on a RAID, the block size should be"  NL
"### either 50% or 100% of RAID block size / granularity.  Also, your file"  NL
"### system blocks/clusters should be properly aligned and sized.  In that"  NL
"### setup, each access will hit only one disk (minimizes I/O load) but"     NL
"### uses all the data provided by the disk in a single access."             NL
"### For SSD-based storage systems, slightly lower values around 16 kB"      NL
"### may improve latency while still maximizing throughput.  If block-read"  NL
"### has not been enabled, this will be capped to 4 kBytes."                 NL
"### Can be changed at any time but must be a power of 2."                   NL
"### block-size is given in kBytes and with a default of 64 kBytes."         NL
"# " CONFIG_OPTION_BLOCK_SIZE " = 64"                                        NL
"###"                                                                        NL
"### The log-to-phys index maps data item numbers to offsets within the"     NL
"### rev or pack file.  This index is organized in pages of a fixed maximum" NL
"### capacity.  To access an item, the page table and the respective page"   NL
"### must be read."                                                          NL
"### This parameter only affects revisions with thousands of changed paths." NL
"### If you have several extremely large revisions (~1 mio changes), think"  NL
"### about increasing this setting.  Reducing the value will rarely result"  NL
"### in a net speedup."                                                      NL
"### This is an expert setting.  Must be a power of 2."                      NL
"### l2p-page-size is 8192 entries by default."                              NL
"# " CONFIG_OPTION_L2P_PAGE_SIZE " = 8192"                                   NL
"###"                                                                        NL
"### The phys-to-log index maps positions within the rev or pack file to"    NL
"### to data items,  i.e. describes what piece of information is being"      NL
"### stored at any particular offset.  The index describes the rev file"     NL
"### in chunks (pages) and keeps a global list of all those pages.  Large"   NL
"### pages mean a shorter page table but a larger per-page description of"   NL
"### data items in it.  The latency sweetspot depends on the change size"    NL
"### distribution but covers a relatively wide range."                       NL
"### If the repository contains very large files,  i.e. individual changes"  NL
"### of tens of MB each,  increasing the page size will shorten the index"   NL
"### file at the expense of a slightly increased latency in sections with"   NL
"### smaller changes."                                                       NL
"### For source code repositories, this should be about 16x the block-size." NL
"### Must be a power of 2."                                                  NL
"### p2l-page-size is given in kBytes and with a default of 1024 kBytes."    NL
"# " CONFIG_OPTION_P2L_PAGE_SIZE " = 1024"                                   NL
""                                                                           NL
"[" CONFIG_SECTION_DEBUG "]"                                                 NL
"###"                                                                        NL
"### Whether to verify each new revision immediately before finalizing"      NL
"### the commit.  This is disabled by default except in maintainer-mode"     NL
"### builds."                                                                NL
"# " CONFIG_OPTION_VERIFY_BEFORE_COMMIT " = false"                           NL
;
#undef NL
  return svn_io_file_create(svn_dirent_join(fs->path, PATH_CONFIG, pool),
                            fsfs_conf_contents, pool);
}

/* Read / Evaluate the global configuration in FS->CONFIG to set up
 * parameters in FS. */
static svn_error_t *
read_global_config(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  ffd->use_block_read = svn_hash__get_bool(fs->config,
                                           SVN_FS_CONFIG_FSFS_BLOCK_READ,
                                           FALSE);
  ffd->flush_to_disk = !svn_hash__get_bool(fs->config,
                                           SVN_FS_CONFIG_NO_FLUSH_TO_DISK,
                                           FALSE);

  /* Ignore the user-specified larger block size if we don't use block-read.
     Defaulting to 4k gives us the same access granularity in format 7 as in
     older formats. */
  if (!ffd->use_block_read)
    ffd->block_size = MIN(0x1000, ffd->block_size);

  return SVN_NO_ERROR;
}

/* Read FS's UUID file and store the data in the FS struct. */
static svn_error_t *
read_uuid(svn_fs_t *fs,
          apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *uuid_file;
  char buf[APR_UUID_FORMATTED_LENGTH + 2];
  apr_size_t limit;

  /* Read the repository uuid. */
  SVN_ERR(svn_io_file_open(&uuid_file, path_uuid(fs, scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           scratch_pool));

  limit = sizeof(buf);
  SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit, scratch_pool));
  fs->uuid = apr_pstrdup(fs->pool, buf);

  /* Read the instance ID. */
  if (ffd->format >= SVN_FS_FS__MIN_INSTANCE_ID_FORMAT)
    {
      limit = sizeof(buf);
      SVN_ERR(svn_io_read_length_line(uuid_file, buf, &limit,
                                      scratch_pool));
      ffd->instance_id = apr_pstrdup(fs->pool, buf);
    }
  else
    {
      ffd->instance_id = fs->uuid;
    }

  SVN_ERR(svn_io_file_close(uuid_file, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_format_file(svn_fs_t *fs, apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  int format, max_files_per_dir;
  svn_boolean_t use_log_addressing;

  /* Read info from format file. */
  SVN_ERR(read_format(&format, &max_files_per_dir, &use_log_addressing,
                      path_format(fs, scratch_pool), scratch_pool));

  /* Now that we've got *all* info, store / update values in FFD. */
  ffd->format = format;
  ffd->max_files_per_dir = max_files_per_dir;
  ffd->use_log_addressing = use_log_addressing;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open(svn_fs_t *fs, const char *path, apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  fs->path = apr_pstrdup(fs->pool, path);

  /* Read the FS format file. */
  SVN_ERR(svn_fs_fs__read_format_file(fs, pool));

  /* Read in and cache the repository uuid. */
  SVN_ERR(read_uuid(fs, pool));

  /* Read the min unpacked revision. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, pool));

  /* Read the configuration file. */
  SVN_ERR(read_config(ffd, fs->path, fs->pool, pool));

  /* Global configuration options. */
  SVN_ERR(read_global_config(fs));

  ffd->youngest_rev_cache = 0;

  return SVN_NO_ERROR;
}

/* Wrapper around svn_io_file_create which ignores EEXIST. */
static svn_error_t *
create_file_ignore_eexist(const char *file,
                          const char *contents,
                          apr_pool_t *pool)
{
  svn_error_t *err = svn_io_file_create(file, contents, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      err = SVN_NO_ERROR;
    }
  return svn_error_trace(err);
}

/* Baton type bridging svn_fs_fs__upgrade and upgrade_body carrying
 * parameters over between them. */
struct upgrade_baton_t
{
  svn_fs_t *fs;
  svn_fs_upgrade_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

static svn_error_t *
upgrade_body(void *baton, apr_pool_t *pool)
{
  struct upgrade_baton_t *upgrade_baton = baton;
  svn_fs_t *fs = upgrade_baton->fs;
  fs_fs_data_t *ffd = fs->fsap_data;
  int format, max_files_per_dir;
  svn_boolean_t use_log_addressing;
  const char *format_path = path_format(fs, pool);
  svn_node_kind_t kind;
  svn_boolean_t needs_revprop_shard_cleanup = FALSE;

  /* Read the FS format number and max-files-per-dir setting. */
  SVN_ERR(read_format(&format, &max_files_per_dir, &use_log_addressing,
                      format_path, pool));

  /* If the config file does not exist, create one. */
  SVN_ERR(svn_io_check_path(svn_dirent_join(fs->path, PATH_CONFIG, pool),
                            &kind, pool));
  switch (kind)
    {
    case svn_node_none:
      SVN_ERR(write_config(fs, pool));
      break;
    case svn_node_file:
      break;
    default:
      return svn_error_createf(SVN_ERR_FS_GENERAL, NULL,
                               _("'%s' is not a regular file."
                                 " Please move it out of "
                                 "the way and try again"),
                               svn_dirent_join(fs->path, PATH_CONFIG, pool));
    }

  /* If we're already up-to-date, there's nothing else to be done here. */
  if (format == SVN_FS_FS__FORMAT_NUMBER)
    return SVN_NO_ERROR;

  /* If our filesystem predates the existence of the 'txn-current
     file', make that file and its corresponding lock file. */
  if (format < SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    {
      SVN_ERR(create_file_ignore_eexist(
                           svn_fs_fs__path_txn_current(fs, pool), "0\n",
                           pool));
      SVN_ERR(create_file_ignore_eexist(
                           svn_fs_fs__path_txn_current_lock(fs, pool), "",
                           pool));
    }

  /* If our filesystem predates the existence of the 'txn-protorevs'
     dir, make that directory.  */
  if (format < SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    {
      SVN_ERR(svn_io_make_dir_recursively(
          svn_fs_fs__path_txn_proto_revs(fs, pool), pool));
    }

  /* If our filesystem is new enough, write the min unpacked rev file. */
  if (format < SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_io_file_create(svn_fs_fs__path_min_unpacked_rev(fs, pool),
                               "0\n", pool));

  /* If the file system supports revision packing but not revprop packing
     *and* the FS has been sharded, pack the revprops up to the point that
     revision data has been packed.  However, keep the non-packed revprop
     files around until after the format bump */
  if (   format >= SVN_FS_FS__MIN_PACKED_FORMAT
      && format < SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT
      && max_files_per_dir > 0)
    {
      needs_revprop_shard_cleanup = TRUE;
      SVN_ERR(svn_fs_fs__upgrade_pack_revprops(fs,
                                               upgrade_baton->notify_func,
                                               upgrade_baton->notify_baton,
                                               upgrade_baton->cancel_func,
                                               upgrade_baton->cancel_baton,
                                               pool));
    }

  /* We will need the UUID info shortly ...
     Read it before the format bump as the UUID file still uses the old
     format. */
  SVN_ERR(read_uuid(fs, pool));

  /* Update the format info in the FS struct.  Upgrade steps further
     down will use the format from FS to create missing info. */
  ffd->format = SVN_FS_FS__FORMAT_NUMBER;
  ffd->max_files_per_dir = max_files_per_dir;
  ffd->use_log_addressing = use_log_addressing;

  /* Always add / bump the instance ID such that no form of caching
     accidentally uses outdated information.  Keep the UUID. */
  SVN_ERR(svn_fs_fs__set_uuid(fs, fs->uuid, NULL, pool));

  /* Bump the format file. */
  SVN_ERR(svn_fs_fs__write_format(fs, TRUE, pool));

  if (upgrade_baton->notify_func)
    SVN_ERR(upgrade_baton->notify_func(upgrade_baton->notify_baton,
                                       SVN_FS_FS__FORMAT_NUMBER,
                                       svn_fs_upgrade_format_bumped,
                                       pool));

  /* Now, it is safe to remove the redundant revprop files. */
  if (needs_revprop_shard_cleanup)
    SVN_ERR(svn_fs_fs__upgrade_cleanup_pack_revprops(fs,
                                               upgrade_baton->notify_func,
                                               upgrade_baton->notify_baton,
                                               upgrade_baton->cancel_func,
                                               upgrade_baton->cancel_baton,
                                               pool));

  /* Done */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__upgrade(svn_fs_t *fs,
                   svn_fs_upgrade_notify_t notify_func,
                   void *notify_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  struct upgrade_baton_t baton;
  baton.fs = fs;
  baton.notify_func = notify_func;
  baton.notify_baton = notify_baton;
  baton.cancel_func = cancel_func;
  baton.cancel_baton = cancel_baton;

  return svn_fs_fs__with_all_locks(fs, upgrade_body, (void *)&baton, pool);
}

/* Find the youngest revision in a repository at path FS_PATH and
   return it in *YOUNGEST_P.  Perform temporary allocations in
   POOL. */
static svn_error_t *
get_youngest(svn_revnum_t *youngest_p,
             svn_fs_t *fs,
             apr_pool_t *pool)
{
  apr_uint64_t dummy;
  SVN_ERR(svn_fs_fs__read_current(youngest_p, &dummy, &dummy, fs, pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__youngest_rev(svn_revnum_t *youngest_p,
                        svn_fs_t *fs,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(get_youngest(youngest_p, fs, pool));
  ffd->youngest_rev_cache = *youngest_p;

  return SVN_NO_ERROR;
}

int
svn_fs_fs__shard_size(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  return ffd->max_files_per_dir;
}

svn_error_t *
svn_fs_fs__min_unpacked_rev(svn_revnum_t *min_unpacked,
                            svn_fs_t *fs,
                            apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Calling this for pre-v4 repos is illegal. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, pool));

  *min_unpacked = ffd->min_unpacked_rev;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__ensure_revision_exists(svn_revnum_t rev,
                                  svn_fs_t *fs,
                                  apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  if (! SVN_IS_VALID_REVNUM(rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Invalid revision number '%ld'"), rev);


  /* Did the revision exist the last time we checked the current
     file? */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  SVN_ERR(get_youngest(&(ffd->youngest_rev_cache), fs, pool));

  /* Check again. */
  if (rev <= ffd->youngest_rev_cache)
    return SVN_NO_ERROR;

  return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                           _("No such revision %ld"), rev);
}

svn_error_t *
svn_fs_fs__file_length(svn_filesize_t *length,
                       node_revision_t *noderev,
                       apr_pool_t *pool)
{
  representation_t *data_rep = noderev->data_rep;
  if (!data_rep)
    {
      /* Treat "no representation" as "empty file". */
      *length = 0;
    }
  else
    {
      *length = data_rep->expanded_size;
    }

  return SVN_NO_ERROR;
}

svn_boolean_t
svn_fs_fs__noderev_same_rep_key(representation_t *a,
                                representation_t *b)
{
  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  if (a->item_index != b->item_index)
    return FALSE;

  if (a->revision != b->revision)
    return FALSE;

  return memcmp(&a->uniquifier, &b->uniquifier, sizeof(a->uniquifier)) == 0;
}

svn_error_t *
svn_fs_fs__file_text_rep_equal(svn_boolean_t *equal,
                               svn_fs_t *fs,
                               node_revision_t *a,
                               node_revision_t *b,
                               apr_pool_t *scratch_pool)
{
  svn_stream_t *contents_a, *contents_b;
  representation_t *rep_a = a->data_rep;
  representation_t *rep_b = b->data_rep;
  svn_boolean_t a_empty = !rep_a || rep_a->expanded_size == 0;
  svn_boolean_t b_empty = !rep_b || rep_b->expanded_size == 0;

  /* This makes sure that neither rep will be NULL later on */
  if (a_empty && b_empty)
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  if (a_empty != b_empty)
    {
      *equal = FALSE;
      return SVN_NO_ERROR;
    }

  /* File text representations always know their checksums - even in a txn. */
  if (memcmp(rep_a->md5_digest, rep_b->md5_digest, sizeof(rep_a->md5_digest)))
    {
      *equal = FALSE;
      return SVN_NO_ERROR;
    }

  /* Paranoia. Compare SHA1 checksums because that's the level of
     confidence we require for e.g. the working copy. */
  if (rep_a->has_sha1 && rep_b->has_sha1)
    {
      *equal = memcmp(rep_a->sha1_digest, rep_b->sha1_digest,
                      sizeof(rep_a->sha1_digest)) == 0;
      return SVN_NO_ERROR;
    }

  /* Same path in same rev or txn? */
  if (svn_fs_fs__id_eq(a->id, b->id))
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_fs_fs__get_contents(&contents_a, fs, rep_a, TRUE,
                                  scratch_pool));
  SVN_ERR(svn_fs_fs__get_contents(&contents_b, fs, rep_b, TRUE,
                                  scratch_pool));
  SVN_ERR(svn_stream_contents_same2(equal, contents_a, contents_b,
                                   scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__prop_rep_equal(svn_boolean_t *equal,
                          svn_fs_t *fs,
                          node_revision_t *a,
                          node_revision_t *b,
                          apr_pool_t *scratch_pool)
{
  representation_t *rep_a = a->prop_rep;
  representation_t *rep_b = b->prop_rep;
  apr_hash_t *proplist_a;
  apr_hash_t *proplist_b;

  /* Mainly for a==b==NULL */
  if (rep_a == rep_b)
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  /* Committed property lists can be compared quickly */
  if (   rep_a && rep_b
      && !svn_fs_fs__id_txn_used(&rep_a->txn_id)
      && !svn_fs_fs__id_txn_used(&rep_b->txn_id))
    {
      /* Same representation? */
      if (   (rep_a->revision == rep_b->revision)
          && (rep_a->item_index == rep_b->item_index))
        {
          *equal = TRUE;
          return SVN_NO_ERROR;
        }

      /* Known different content? MD5 must be given. */
      if (memcmp(rep_a->md5_digest, rep_b->md5_digest,
                 sizeof(rep_a->md5_digest)))
        {
          *equal = FALSE;
          return SVN_NO_ERROR;
        }
    }

  /* Same path in same txn?
   *
   * For committed reps, IDs cannot be the same here b/c we already know
   * that they point to different representations. */
  if (svn_fs_fs__id_eq(a->id, b->id))
    {
      *equal = TRUE;
      return SVN_NO_ERROR;
    }

  /* At least one of the reps has been modified in a txn.
     Fetch and compare them. */
  SVN_ERR(svn_fs_fs__get_proplist(&proplist_a, fs, a, scratch_pool));
  SVN_ERR(svn_fs_fs__get_proplist(&proplist_b, fs, b, scratch_pool));

  *equal = svn_fs__prop_lists_equal(proplist_a, proplist_b, scratch_pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_fs__file_checksum(svn_checksum_t **checksum,
                         node_revision_t *noderev,
                         svn_checksum_kind_t kind,
                         apr_pool_t *pool)
{
  *checksum = NULL;

  if (noderev->data_rep)
    {
      svn_checksum_t temp;
      temp.kind = kind;

      switch(kind)
        {
          case svn_checksum_md5:
            temp.digest = noderev->data_rep->md5_digest;
            break;

          case svn_checksum_sha1:
            if (! noderev->data_rep->has_sha1)
              return SVN_NO_ERROR;

            temp.digest = noderev->data_rep->sha1_digest;
            break;

          default:
            return SVN_NO_ERROR;
        }

      *checksum = svn_checksum_dup(&temp, pool);
    }

  return SVN_NO_ERROR;
}

representation_t *
svn_fs_fs__rep_copy(representation_t *rep,
                    apr_pool_t *pool)
{
  if (rep == NULL)
    return NULL;

  return apr_pmemdup(pool, rep, sizeof(*rep));
}


/* Write out the zeroth revision for filesystem FS.
   Perform temporary allocations in SCRATCH_POOL. */
static svn_error_t *
write_revision_zero(svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  /* Use an explicit sub-pool to have full control over temp file lifetimes.
   * Since we have it, use it for everything else as well. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  const char *path_revision_zero = svn_fs_fs__path_rev(fs, 0, subpool);
  apr_hash_t *proplist;
  svn_string_t date;

  /* Write out a rev file for revision 0. */
  if (svn_fs_fs__use_log_addressing(fs))
    {
      apr_array_header_t *index_entries;
      svn_fs_fs__p2l_entry_t *entry;
      svn_fs_fs__revision_file_t *rev_file;
      const char *l2p_proto_index, *p2l_proto_index;

      /* Write a skeleton r0 with no indexes. */
      SVN_ERR(svn_io_file_create(path_revision_zero,
                    "PLAIN\nEND\nENDREP\n"
                    "id: 0.0.r0/2\n"
                    "type: dir\n"
                    "count: 0\n"
                    "text: 0 3 4 4 "
                    "2d2977d1c96f487abe4a1e202dd03b4e\n"
                    "cpath: /\n"
                    "\n\n", subpool));

      /* Construct the index P2L contents: describe the 3 items we have.
         Be sure to create them in on-disk order. */
      index_entries = apr_array_make(subpool, 3, sizeof(entry));

      entry = apr_pcalloc(subpool, sizeof(*entry));
      entry->offset = 0;
      entry->size = 17;
      entry->type = SVN_FS_FS__ITEM_TYPE_DIR_REP;
      entry->item.revision = 0;
      entry->item.number = SVN_FS_FS__ITEM_INDEX_FIRST_USER;
      APR_ARRAY_PUSH(index_entries, svn_fs_fs__p2l_entry_t *) = entry;

      entry = apr_pcalloc(subpool, sizeof(*entry));
      entry->offset = 17;
      entry->size = 89;
      entry->type = SVN_FS_FS__ITEM_TYPE_NODEREV;
      entry->item.revision = 0;
      entry->item.number = SVN_FS_FS__ITEM_INDEX_ROOT_NODE;
      APR_ARRAY_PUSH(index_entries, svn_fs_fs__p2l_entry_t *) = entry;

      entry = apr_pcalloc(subpool, sizeof(*entry));
      entry->offset = 106;
      entry->size = 1;
      entry->type = SVN_FS_FS__ITEM_TYPE_CHANGES;
      entry->item.revision = 0;
      entry->item.number = SVN_FS_FS__ITEM_INDEX_CHANGES;
      APR_ARRAY_PUSH(index_entries, svn_fs_fs__p2l_entry_t *) = entry;

      /* Now re-open r0, create proto-index files from our entries and
         rewrite the index section of r0. */
      SVN_ERR(svn_fs_fs__open_pack_or_rev_file_writable(&rev_file, fs, 0,
                                                        subpool, subpool));
      SVN_ERR(svn_fs_fs__p2l_index_from_p2l_entries(&p2l_proto_index, fs,
                                                    rev_file, index_entries,
                                                    subpool, subpool));
      SVN_ERR(svn_fs_fs__l2p_index_from_p2l_entries(&l2p_proto_index, fs,
                                                    index_entries,
                                                    subpool, subpool));
      SVN_ERR(svn_fs_fs__add_index_data(fs, rev_file->file, l2p_proto_index,
                                        p2l_proto_index, 0, subpool));
      SVN_ERR(svn_fs_fs__close_revision_file(rev_file));
    }
  else
    SVN_ERR(svn_io_file_create(path_revision_zero,
                               "PLAIN\nEND\nENDREP\n"
                               "id: 0.0.r0/17\n"
                               "type: dir\n"
                               "count: 0\n"
                               "text: 0 0 4 4 "
                               "2d2977d1c96f487abe4a1e202dd03b4e\n"
                               "cpath: /\n"
                               "\n\n17 107\n", subpool));

  SVN_ERR(svn_io_set_file_read_only(path_revision_zero, FALSE, subpool));

  /* Set a date on revision 0. */
  date.data = svn_time_to_cstring(apr_time_now(), subpool);
  date.len = strlen(date.data);
  proplist = apr_hash_make(subpool);
  svn_hash_sets(proplist, SVN_PROP_REVISION_DATE, &date);
  SVN_ERR(svn_fs_fs__set_revision_proplist(fs, 0, proplist, subpool));

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create_file_tree(svn_fs_t *fs,
                            const char *path,
                            int format,
                            int shard_size,
                            svn_boolean_t use_log_addressing,
                            apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  fs->path = apr_pstrdup(fs->pool, path);
  ffd->format = format;

  /* Use an appropriate sharding mode if supported by the format. */
  if (format >= SVN_FS_FS__MIN_LAYOUT_FORMAT_OPTION_FORMAT)
    ffd->max_files_per_dir = shard_size;
  else
    ffd->max_files_per_dir = 0;

  /* Select the addressing mode depending on the format. */
  if (format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT)
    ffd->use_log_addressing = use_log_addressing;
  else
    ffd->use_log_addressing = FALSE;

  /* Create the revision data directories. */
  if (ffd->max_files_per_dir)
    SVN_ERR(svn_io_make_dir_recursively(svn_fs_fs__path_rev_shard(fs, 0,
                                                                  pool),
                                        pool));
  else
    SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path, PATH_REVS_DIR,
                                                        pool),
                                        pool));

  /* Create the revprops directory. */
  if (ffd->max_files_per_dir)
    SVN_ERR(svn_io_make_dir_recursively(svn_fs_fs__path_revprops_shard(fs, 0,
                                                                       pool),
                                        pool));
  else
    SVN_ERR(svn_io_make_dir_recursively(svn_dirent_join(path,
                                                        PATH_REVPROPS_DIR,
                                                        pool),
                                        pool));

  /* Create the transaction directory. */
  SVN_ERR(svn_io_make_dir_recursively(svn_fs_fs__path_txns_dir(fs, pool),
                                      pool));

  /* Create the protorevs directory. */
  if (format >= SVN_FS_FS__MIN_PROTOREVS_DIR_FORMAT)
    SVN_ERR(svn_io_make_dir_recursively(svn_fs_fs__path_txn_proto_revs(fs,
                                                                       pool),
                                        pool));

  /* Create the 'current' file. */
  SVN_ERR(svn_io_file_create_empty(svn_fs_fs__path_current(fs, pool), pool));
  SVN_ERR(svn_fs_fs__write_current(fs, 0, 1, 1, pool));

  /* Create the 'uuid' file. */
  SVN_ERR(svn_io_file_create_empty(svn_fs_fs__path_lock(fs, pool), pool));
  SVN_ERR(svn_fs_fs__set_uuid(fs, NULL, NULL, pool));

  /* Create the fsfs.conf file if supported.  Older server versions would
     simply ignore the file but that might result in a different behavior
     than with the later releases.  Also, hotcopy would ignore, i.e. not
     copy, a fsfs.conf with old formats. */
  if (ffd->format >= SVN_FS_FS__MIN_CONFIG_FILE)
    SVN_ERR(write_config(fs, pool));

  SVN_ERR(read_config(ffd, fs->path, fs->pool, pool));

  /* Global configuration options. */
  SVN_ERR(read_global_config(fs));

  /* Add revision 0. */
  SVN_ERR(write_revision_zero(fs, pool));

  /* Create the min unpacked rev file. */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
    SVN_ERR(svn_io_file_create(svn_fs_fs__path_min_unpacked_rev(fs, pool),
                               "0\n", pool));

  /* Create the txn-current file if the repository supports
     the transaction sequence file. */
  if (format >= SVN_FS_FS__MIN_TXN_CURRENT_FORMAT)
    {
      SVN_ERR(svn_io_file_create(svn_fs_fs__path_txn_current(fs, pool),
                                 "0\n", pool));
      SVN_ERR(svn_io_file_create_empty(
                                 svn_fs_fs__path_txn_current_lock(fs, pool),
                                 pool));
    }

  ffd->youngest_rev_cache = 0;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create(svn_fs_t *fs,
                  const char *path,
                  apr_pool_t *pool)
{
  int format = SVN_FS_FS__FORMAT_NUMBER;
  int shard_size = SVN_FS_FS_DEFAULT_MAX_FILES_PER_DIR;
  svn_boolean_t log_addressing;

  /* Process the given filesystem config. */
  if (fs->config)
    {
      svn_version_t *compatible_version;
      const char *shard_size_str;
      SVN_ERR(svn_fs__compatible_version(&compatible_version, fs->config,
                                         pool));

      /* select format number */
      switch(compatible_version->minor)
        {
          case 0: return svn_error_create(SVN_ERR_FS_UNSUPPORTED_FORMAT, NULL,
                 _("FSFS is not compatible with Subversion prior to 1.1"));

          case 1:
          case 2:
          case 3: format = 1;
                  break;

          case 4: format = 2;
                  break;

          case 5: format = 3;
                  break;

          case 6:
          case 7: format = 4;
                  break;

          case 8: format = 6;
                  break;
          case 9: format = 7;
                  break;

          default:format = SVN_FS_FS__FORMAT_NUMBER;
        }

      shard_size_str = svn_hash_gets(fs->config, SVN_FS_CONFIG_FSFS_SHARD_SIZE);
      if (shard_size_str)
        {
          apr_int64_t val;
          SVN_ERR(svn_cstring_strtoi64(&val, shard_size_str, 0,
                                       APR_INT32_MAX, 10));

          shard_size = (int) val;
        }
    }

  log_addressing = svn_hash__get_bool(fs->config,
                                      SVN_FS_CONFIG_FSFS_LOG_ADDRESSING,
                                      TRUE);

  /* Actual FS creation. */
  SVN_ERR(svn_fs_fs__create_file_tree(fs, path, format, shard_size,
                                      log_addressing, pool));

  /* This filesystem is ready.  Stamp it with a format number. */
  SVN_ERR(svn_fs_fs__write_format(fs, FALSE, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__set_uuid(svn_fs_t *fs,
                    const char *uuid,
                    const char *instance_id,
                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *uuid_path = path_uuid(fs, pool);
  svn_stringbuf_t *contents = svn_stringbuf_create_empty(pool);

  if (! uuid)
    uuid = svn_uuid_generate(pool);

  if (! instance_id)
    instance_id = svn_uuid_generate(pool);

  svn_stringbuf_appendcstr(contents, uuid);
  svn_stringbuf_appendcstr(contents, "\n");

  if (ffd->format >= SVN_FS_FS__MIN_INSTANCE_ID_FORMAT)
    {
      svn_stringbuf_appendcstr(contents, instance_id);
      svn_stringbuf_appendcstr(contents, "\n");
    }

  /* We use the permissions of the 'current' file, because the 'uuid'
     file does not exist during repository creation. */
  SVN_ERR(svn_io_write_atomic2(uuid_path, contents->data, contents->len,
                               svn_fs_fs__path_current(fs, pool) /* perms */,
                               ffd->flush_to_disk, pool));

  fs->uuid = apr_pstrdup(fs->pool, uuid);

  if (ffd->format >= SVN_FS_FS__MIN_INSTANCE_ID_FORMAT)
    ffd->instance_id = apr_pstrdup(fs->pool, instance_id);
  else
    ffd->instance_id = fs->uuid;

  return SVN_NO_ERROR;
}

/** Node origin lazy cache. */

/* If directory PATH does not exist, create it and give it the same
   permissions as FS_path.*/
svn_error_t *
svn_fs_fs__ensure_dir_exists(const char *path,
                             const char *fs_path,
                             apr_pool_t *pool)
{
  svn_error_t *err = svn_io_dir_make(path, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_EEXIST(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  /* We successfully created a new directory.  Dup the permissions
     from FS->path. */
  return svn_io_copy_perms(fs_path, path, pool);
}

/* Set *NODE_ORIGINS to a hash mapping 'const char *' node IDs to
   'svn_string_t *' node revision IDs.  Use POOL for allocations. */
static svn_error_t *
get_node_origins_from_file(svn_fs_t *fs,
                           apr_hash_t **node_origins,
                           const char *node_origins_file,
                           apr_pool_t *pool)
{
  apr_file_t *fd;
  svn_error_t *err;
  svn_stream_t *stream;

  *node_origins = NULL;
  err = svn_io_file_open(&fd, node_origins_file,
                         APR_READ, APR_OS_DEFAULT, pool);
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  SVN_ERR(err);

  stream = svn_stream_from_aprfile2(fd, FALSE, pool);
  *node_origins = apr_hash_make(pool);
  err = svn_hash_read2(*node_origins, stream, SVN_HASH_TERMINATOR, pool);
  if (err)
    return svn_error_quick_wrapf(err, _("malformed node origin data in '%s'"),
                                 node_origins_file);
  return svn_stream_close(stream);
}

svn_error_t *
svn_fs_fs__get_node_origin(const svn_fs_id_t **origin_id,
                           svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           apr_pool_t *pool)
{
  apr_hash_t *node_origins;

  *origin_id = NULL;
  SVN_ERR(get_node_origins_from_file(fs, &node_origins,
                                     svn_fs_fs__path_node_origin(fs, node_id,
                                                                 pool),
                                     pool));
  if (node_origins)
    {
      char node_id_ptr[SVN_INT64_BUFFER_SIZE];
      apr_size_t len = svn__ui64tobase36(node_id_ptr, node_id->number);
      svn_string_t *origin_id_str
        = apr_hash_get(node_origins, node_id_ptr, len);

      if (origin_id_str)
        SVN_ERR(svn_fs_fs__id_parse(origin_id,
                                    apr_pstrdup(pool, origin_id_str->data),
                                    pool));
    }
  return SVN_NO_ERROR;
}


/* Helper for svn_fs_fs__set_node_origin.  Takes a NODE_ID/NODE_REV_ID
   pair and adds it to the NODE_ORIGINS_PATH file.  */
static svn_error_t *
set_node_origins_for_file(svn_fs_t *fs,
                          const char *node_origins_path,
                          const svn_fs_fs__id_part_t *node_id,
                          svn_string_t *node_rev_id,
                          apr_pool_t *pool)
{
  const char *path_tmp;
  svn_stream_t *stream;
  apr_hash_t *origins_hash;
  svn_string_t *old_node_rev_id;

  /* the hash serialization functions require strings as keys */
  char node_id_ptr[SVN_INT64_BUFFER_SIZE];
  apr_size_t len = svn__ui64tobase36(node_id_ptr, node_id->number);

  SVN_ERR(svn_fs_fs__ensure_dir_exists(svn_dirent_join(fs->path,
                                                       PATH_NODE_ORIGINS_DIR,
                                                       pool),
                                       fs->path, pool));

  /* Read the previously existing origins (if any), and merge our
     update with it. */
  SVN_ERR(get_node_origins_from_file(fs, &origins_hash,
                                     node_origins_path, pool));
  if (! origins_hash)
    origins_hash = apr_hash_make(pool);

  old_node_rev_id = apr_hash_get(origins_hash, node_id_ptr, len);

  if (old_node_rev_id && !svn_string_compare(node_rev_id, old_node_rev_id))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Node origin for '%s' exists with a different "
                               "value (%s) than what we were about to store "
                               "(%s)"),
                             node_id_ptr, old_node_rev_id->data,
                             node_rev_id->data);

  apr_hash_set(origins_hash, node_id_ptr, len, node_rev_id);

  /* Sure, there's a race condition here.  Two processes could be
     trying to add different cache elements to the same file at the
     same time, and the entries added by the first one to write will
     be lost.  But this is just a cache of reconstructible data, so
     we'll accept this problem in return for not having to deal with
     locking overhead. */

  /* Create a temporary file, write out our hash, and close the file. */
  SVN_ERR(svn_stream_open_unique(&stream, &path_tmp,
                                 svn_dirent_dirname(node_origins_path, pool),
                                 svn_io_file_del_none, pool, pool));
  SVN_ERR(svn_hash_write2(origins_hash, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* Rename the temp file as the real destination */
  return svn_io_file_rename2(path_tmp, node_origins_path, FALSE, pool);
}


svn_error_t *
svn_fs_fs__set_node_origin(svn_fs_t *fs,
                           const svn_fs_fs__id_part_t *node_id,
                           const svn_fs_id_t *node_rev_id,
                           apr_pool_t *pool)
{
  svn_error_t *err;
  const char *filename = svn_fs_fs__path_node_origin(fs, node_id, pool);

  err = set_node_origins_for_file(fs, filename,
                                  node_id,
                                  svn_fs_fs__id_unparse(node_rev_id, pool),
                                  pool);
  if (err && APR_STATUS_IS_EACCES(err->apr_err))
    {
      /* It's just a cache; stop trying if I can't write. */
      svn_error_clear(err);
      err = NULL;
    }
  return svn_error_trace(err);
}



/*** Revisions ***/

svn_error_t *
svn_fs_fs__revision_prop(svn_string_t **value_p,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         const char *propname,
                         svn_boolean_t refresh,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *table;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));
  SVN_ERR(svn_fs_fs__get_revision_proplist(&table, fs, rev, refresh,
                                           scratch_pool, scratch_pool));

  *value_p = svn_string_dup(svn_hash_gets(table, propname), result_pool);

  return SVN_NO_ERROR;
}


/* Baton used for change_rev_prop_body below. */
struct change_rev_prop_baton {
  svn_fs_t *fs;
  svn_revnum_t rev;
  const char *name;
  const svn_string_t *const *old_value_p;
  const svn_string_t *value;
};

/* The work-horse for svn_fs_fs__change_rev_prop, called with the FS
   write lock.  This implements the svn_fs_fs__with_write_lock()
   'body' callback type.  BATON is a 'struct change_rev_prop_baton *'. */
static svn_error_t *
change_rev_prop_body(void *baton, apr_pool_t *pool)
{
  struct change_rev_prop_baton *cb = baton;
  apr_hash_t *table;
  const svn_string_t *present_value;

  /* We always need to read the current revprops from disk.
   * Hence, always "refresh" here. */
  SVN_ERR(svn_fs_fs__get_revision_proplist(&table, cb->fs, cb->rev, TRUE,
                                           pool, pool));
  present_value = svn_hash_gets(table, cb->name);

  if (cb->old_value_p)
    {
      const svn_string_t *wanted_value = *cb->old_value_p;
      if ((!wanted_value != !present_value)
          || (wanted_value && present_value
              && !svn_string_compare(wanted_value, present_value)))
        {
          /* What we expected isn't what we found. */
          return svn_error_createf(SVN_ERR_FS_PROP_BASEVALUE_MISMATCH, NULL,
                                   _("revprop '%s' has unexpected value in "
                                     "filesystem"),
                                   cb->name);
        }
      /* Fall through. */
    }

  /* If the prop-set is a no-op, skip the actual write. */
  if ((!present_value && !cb->value)
      || (present_value && cb->value
          && svn_string_compare(present_value, cb->value)))
    return SVN_NO_ERROR;

  svn_hash_sets(table, cb->name, cb->value);

  return svn_fs_fs__set_revision_proplist(cb->fs, cb->rev, table, pool);
}

svn_error_t *
svn_fs_fs__change_rev_prop(svn_fs_t *fs,
                           svn_revnum_t rev,
                           const char *name,
                           const svn_string_t *const *old_value_p,
                           const svn_string_t *value,
                           apr_pool_t *pool)
{
  struct change_rev_prop_baton cb;

  SVN_ERR(svn_fs__check_fs(fs, TRUE));

  cb.fs = fs;
  cb.rev = rev;
  cb.name = name;
  cb.old_value_p = old_value_p;
  cb.value = value;

  return svn_fs_fs__with_write_lock(fs, change_rev_prop_body, &cb, pool);
}


svn_error_t *
svn_fs_fs__info_format(int *fs_format,
                       svn_version_t **supports_version,
                       svn_fs_t *fs,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  *fs_format = ffd->format;
  *supports_version = apr_palloc(result_pool, sizeof(svn_version_t));

  (*supports_version)->major = SVN_VER_MAJOR;
  (*supports_version)->minor = 1;
  (*supports_version)->patch = 0;
  (*supports_version)->tag = "";

  switch (ffd->format)
    {
    case 1:
      break;
    case 2:
      (*supports_version)->minor = 4;
      break;
    case 3:
      (*supports_version)->minor = 5;
      break;
    case 4:
      (*supports_version)->minor = 6;
      break;
    case 6:
      (*supports_version)->minor = 8;
      break;
    case 7:
      (*supports_version)->minor = 9;
      break;
    case 8:
      (*supports_version)->minor = 10;
      break;
#ifdef SVN_DEBUG
# if SVN_FS_FS__FORMAT_NUMBER != 8
#  error "Need to add a 'case' statement here"
# endif
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__info_config_files(apr_array_header_t **files,
                             svn_fs_t *fs,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  *files = apr_array_make(result_pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(*files, const char *) = svn_dirent_join(fs->path, PATH_CONFIG,
                                                         result_pool);
  return SVN_NO_ERROR;
}
