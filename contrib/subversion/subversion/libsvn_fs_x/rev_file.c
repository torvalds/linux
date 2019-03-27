/* rev_file.c --- revision file and index access functions
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

#include "svn_pools.h"

#include "rev_file.h"
#include "fs_x.h"
#include "index.h"
#include "low_level.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "private/svn_io_private.h"
#include "svn_private_config.h"

struct svn_fs_x__revision_file_t
{
  /* the filesystem that this revision file belongs to */
  svn_fs_t *fs;

  /* Meta-data to FILE. */
  svn_fs_x__rev_file_info_t file_info;

  /* rev / pack file */
  apr_file_t *file;

  /* stream based on FILE and not NULL exactly when FILE is not NULL */
  svn_stream_t *stream;

  /* the opened P2L index stream or NULL.  Always NULL for txns. */
  svn_fs_x__packed_number_stream_t *p2l_stream;

  /* the opened L2P index stream or NULL.  Always NULL for txns. */
  svn_fs_x__packed_number_stream_t *l2p_stream;

  /* Copied from FS->FFD->BLOCK_SIZE upon creation.  It allows us to
   * use aligned seek() without having the FS handy. */
  apr_off_t block_size;

  /* Info on the L2P index within FILE.
   * Elements are -1 / NULL until svn_fs_x__auto_read_footer gets called. */
  svn_fs_x__index_info_t l2p_info;

  /* Info on the P2L index within FILE.
   * Elements are -1 / NULL until svn_fs_x__auto_read_footer gets called. */
  svn_fs_x__index_info_t p2l_info;

  /* Pool used for all sub-structure allocations (file, streams etc.).
     A sub-pool of OWNER. NULL until the lazily initilized. */
  apr_pool_t *pool;

  /* Pool that this structure got allocated in. */
  apr_pool_t *owner;
};

/* Return a new revision file instance, allocated in RESULT_POOL, for
 * filesystem FS.  Set its pool member to the provided RESULT_POOL. */
static svn_fs_x__revision_file_t *
create_revision_file(svn_fs_t *fs,
                     apr_pool_t *result_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  svn_fs_x__revision_file_t *file = apr_palloc(result_pool, sizeof(*file));
  file->fs = fs;
  file->file_info.is_packed = FALSE;
  file->file_info.start_revision = SVN_INVALID_REVNUM;
  file->file = NULL;
  file->stream = NULL;
  file->p2l_stream = NULL;
  file->l2p_stream = NULL;
  file->block_size = ffd->block_size;
  file->l2p_info.start = -1;
  file->l2p_info.end = -1;
  file->l2p_info.checksum = NULL;
  file->p2l_info.start = -1;
  file->p2l_info.end = -1;
  file->p2l_info.checksum = NULL;
  file->pool = NULL;
  file->owner = result_pool;

  return file;
}

/* Return a new revision file instance, allocated in RESULT_POOL, for
 * REVISION in filesystem FS.  Set its pool member to the provided
 * RESULT_POOL. */
static svn_fs_x__revision_file_t *
init_revision_file(svn_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *result_pool)
{
  svn_fs_x__revision_file_t *file = create_revision_file(fs, result_pool);

  file->file_info.is_packed = svn_fs_x__is_packed_rev(fs, revision);
  file->file_info.start_revision = svn_fs_x__packed_base_rev(fs, revision);

  return file;
}

/* Baton type for set_read_only() */
typedef struct set_read_only_baton_t
{
  /* File to set to read-only. */
  const char *file_path;

  /* Scratch pool sufficient life time.
   * Ideally the pool that we registered the cleanup on. */
  apr_pool_t *pool;
} set_read_only_baton_t;

/* APR pool cleanup callback taking a set_read_only_baton_t baton and then
 * (trying to) set the specified file to r/o mode. */
static apr_status_t
set_read_only(void *baton)
{
  set_read_only_baton_t *ro_baton = baton;
  apr_status_t status = APR_SUCCESS;
  svn_error_t *err;

  err = svn_io_set_file_read_only(ro_baton->file_path, TRUE, ro_baton->pool);
  if (err)
    {
      status = err->apr_err;
      svn_error_clear(err);
    }

  return status;
}

/* If the file at PATH is read-only, attempt to make it writable.  The
 * original state will be restored with RESULT_POOL gets cleaned up.
 * SCRATCH_POOL is for temporary allocations. */
static svn_error_t *
auto_make_writable(const char *path,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_boolean_t is_read_only;
  apr_finfo_t finfo;

  SVN_ERR(svn_io_stat(&finfo, path, SVN__APR_FINFO_READONLY, scratch_pool));
  SVN_ERR(svn_io__is_finfo_read_only(&is_read_only, &finfo, scratch_pool));

  if (is_read_only)
    {
      /* Tell the pool to restore the r/o state upon cleanup
         (assuming the file will still exist, failing silently otherwise). */
      set_read_only_baton_t *baton = apr_pcalloc(result_pool,
                                                  sizeof(*baton));
      baton->pool = result_pool;
      baton->file_path = apr_pstrdup(result_pool, path);
      apr_pool_cleanup_register(result_pool, baton,
                                set_read_only, apr_pool_cleanup_null);

      /* Finally, allow write access (undoing it has already been scheduled
         and is idempotent). */
      SVN_ERR(svn_io_set_file_read_write(path, FALSE, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Return the pool to be used for allocations with FILE.
   Lazily created that pool upon the first call. */
static apr_pool_t *
get_file_pool(svn_fs_x__revision_file_t *file)
{
  if (file->pool == NULL)
    file->pool = svn_pool_create(file->owner);

  return file->pool;
}

/* Core implementation of svn_fs_x__open_pack_or_rev_file working on an
 * existing, initialized FILE structure.  If WRITABLE is TRUE, give write
 * access to the file - temporarily resetting the r/o state if necessary.
 */
static svn_error_t *
open_pack_or_rev_file(svn_fs_x__revision_file_t *file,
                      svn_boolean_t writable,
                      apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t retry = FALSE;
  svn_fs_t *fs = file->fs;
  svn_revnum_t rev = file->file_info.start_revision;
  apr_pool_t *file_pool = get_file_pool(file);

  do
    {
      const char *path = svn_fs_x__path_rev_absolute(fs, rev, scratch_pool);
      apr_file_t *apr_file;
      apr_int32_t flags = writable
                        ? APR_READ | APR_WRITE | APR_BUFFERED
                        : APR_READ | APR_BUFFERED;

      /* We may have to *temporarily* enable write access. */
      err = writable ? auto_make_writable(path, file_pool, scratch_pool)
                     : SVN_NO_ERROR;

      /* open the revision file in buffered r/o or r/w mode */
      if (!err)
        err = svn_io_file_open(&apr_file, path, flags, APR_OS_DEFAULT,
                               file_pool);

      if (!err)
        {
          file->file = apr_file;
          file->stream = svn_stream_from_aprfile2(apr_file, TRUE,
                                                  file_pool);

          return SVN_NO_ERROR;
        }

      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* Could not open the file. This may happen if the
            * file once existed but got packed later. */
          svn_error_clear(err);

          /* if that was our 2nd attempt, leave it at that. */
          if (retry)
            return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                     _("No such revision %ld"), rev);

          /* We failed for the first time. Refresh cache & retry. */
          SVN_ERR(svn_fs_x__update_min_unpacked_rev(fs, scratch_pool));
          file->file_info.start_revision = svn_fs_x__packed_base_rev(fs, rev);

          retry = TRUE;
        }
      else
        {
          retry = FALSE;
        }
    }
  while (retry);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_x__rev_file_init(svn_fs_x__revision_file_t **file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool)
{
  *file = init_revision_file(fs, rev, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_open_writable(svn_fs_x__revision_file_t** file,
                                 svn_fs_t* fs,
                                 svn_revnum_t rev,
                                 apr_pool_t* result_pool,
                                 apr_pool_t *scratch_pool)
{
  *file = init_revision_file(fs, rev, result_pool);
  return svn_error_trace(open_pack_or_rev_file(*file, TRUE, scratch_pool));
}

/* If the revision file in FILE has not been opened, yet, do it now. */
static svn_error_t *
auto_open(svn_fs_x__revision_file_t *file)
{
  if (file->file == NULL)
    SVN_ERR(open_pack_or_rev_file(file, FALSE, get_file_pool(file)));

  return SVN_NO_ERROR;
}

/* If the footer data in FILE has not been read, yet, do so now.
 * Index locations will only be read upon request as we assume they get
 * cached and the FILE is usually used for REP data access only.
 * Hence, the separate step.
 */
static svn_error_t *
auto_read_footer(svn_fs_x__revision_file_t *file)
{
  if (file->l2p_info.start == -1)
    {
      apr_off_t filesize = 0;
      unsigned char footer_length;
      svn_stringbuf_t *footer;

      /* Determine file size. */
      SVN_ERR(auto_open(file));
      SVN_ERR(svn_io_file_seek(file->file, APR_END, &filesize, file->pool));

      /* Read last byte (containing the length of the footer). */
      SVN_ERR(svn_io_file_aligned_seek(file->file, file->block_size, NULL,
                                       filesize - 1, file->pool));
      SVN_ERR(svn_io_file_read_full2(file->file, &footer_length,
                                     sizeof(footer_length), NULL, NULL,
                                     file->pool));

      /* Read footer. */
      footer = svn_stringbuf_create_ensure(footer_length, file->pool);
      SVN_ERR(svn_io_file_aligned_seek(file->file, file->block_size, NULL,
                                       filesize - 1 - footer_length,
                                       file->pool));
      SVN_ERR(svn_io_file_read_full2(file->file, footer->data, footer_length,
                                     &footer->len, NULL, file->pool));
      footer->data[footer->len] = '\0';

      /* Extract index locations. */
      SVN_ERR(svn_fs_x__parse_footer(&file->l2p_info.start,
                                     &file->l2p_info.checksum,
                                     &file->p2l_info.start,
                                     &file->p2l_info.checksum,
                                     footer, file->file_info.start_revision,
                                     filesize - footer_length - 1, file->pool));
      file->l2p_info.end = file->p2l_info.start;
      file->p2l_info.end = filesize - footer_length - 1;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_open_proto_rev(svn_fs_x__revision_file_t **file,
                                  svn_fs_t *fs,
                                  svn_fs_x__txn_id_t txn_id,
                                  apr_pool_t* result_pool,
                                  apr_pool_t *scratch_pool)
{
  apr_file_t *apr_file;
  SVN_ERR(svn_io_file_open(&apr_file,
                           svn_fs_x__path_txn_proto_rev(fs, txn_id,
                                                        scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result_pool));

  return svn_error_trace(svn_fs_x__rev_file_wrap_temp(file, fs, apr_file,
                                                      result_pool));
}

svn_error_t *
svn_fs_x__rev_file_wrap_temp(svn_fs_x__revision_file_t **file,
                             svn_fs_t *fs,
                             apr_file_t *temp_file,
                             apr_pool_t *result_pool)
{
  *file = create_revision_file(fs, result_pool);
  (*file)->file = temp_file;
  (*file)->stream = svn_stream_from_aprfile2(temp_file, TRUE, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_info(svn_fs_x__rev_file_info_t *info,
                        svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_open(file));

  *info = file->file_info;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_name(const char **filename,
                        svn_fs_x__revision_file_t *file,
                        apr_pool_t *result_pool)
{
  SVN_ERR(auto_open(file));

  return svn_error_trace(svn_io_file_name_get(filename, file->file,
                                              result_pool));
}

svn_error_t *
svn_fs_x__rev_file_stream(svn_stream_t **stream,
                          svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_open(file));

  *stream = file->stream;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_get(apr_file_t **apr_file,
                       svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_open(file));

  *apr_file = file->file;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_l2p_index(svn_fs_x__packed_number_stream_t **stream,
                             svn_fs_x__revision_file_t *file)
{
  if (file->l2p_stream == NULL)
    {
      SVN_ERR(auto_read_footer(file));
      SVN_ERR(svn_fs_x__packed_stream_open(&file->l2p_stream,
                                           file->file,
                                           file->l2p_info.start,
                                           file->l2p_info.end,
                                           SVN_FS_X__L2P_STREAM_PREFIX,
                                           (apr_size_t)file->block_size,
                                           file->pool,
                                           file->pool));
    }

  *stream = file->l2p_stream;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_p2l_index(svn_fs_x__packed_number_stream_t **stream,
                             svn_fs_x__revision_file_t *file)
{
  if (file->p2l_stream== NULL)
    {
      SVN_ERR(auto_read_footer(file));
      SVN_ERR(svn_fs_x__packed_stream_open(&file->p2l_stream,
                                           file->file,
                                           file->p2l_info.start,
                                           file->p2l_info.end,
                                           SVN_FS_X__P2L_STREAM_PREFIX,
                                           (apr_size_t)file->block_size,
                                           file->pool,
                                           file->pool));
    }

  *stream = file->p2l_stream;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_l2p_info(svn_fs_x__index_info_t *info,
                            svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_read_footer(file));
  *info = file->l2p_info;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_p2l_info(svn_fs_x__index_info_t *info,
                            svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_read_footer(file));
  *info = file->p2l_info;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_data_size(svn_filesize_t *size,
                             svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_read_footer(file));
  *size = file->l2p_info.start;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__rev_file_seek(svn_fs_x__revision_file_t *file,
                        apr_off_t *buffer_start,
                        apr_off_t offset)
{
  SVN_ERR(auto_open(file));
  return svn_error_trace(svn_io_file_aligned_seek(file->file,
                                                  file->block_size,
                                                  buffer_start, offset,
                                                  file->pool));
}

svn_error_t *
svn_fs_x__rev_file_offset(apr_off_t *offset,
                          svn_fs_x__revision_file_t *file)
{
  SVN_ERR(auto_open(file));
  return svn_error_trace(svn_io_file_get_offset(offset, file->file,
                                                file->pool));
}

svn_error_t *
svn_fs_x__rev_file_read(svn_fs_x__revision_file_t *file,
                        void *buf,
                        apr_size_t nbytes)
{
  SVN_ERR(auto_open(file));
  return svn_error_trace(svn_io_file_read_full2(file->file, buf, nbytes,
                                                NULL, NULL, file->pool));
}

svn_error_t *
svn_fs_x__close_revision_file(svn_fs_x__revision_file_t *file)
{
  /* Close sub-objects properly */
  if (file->stream)
    SVN_ERR(svn_stream_close(file->stream));
  if (file->file)
    SVN_ERR(svn_io_file_close(file->file, file->pool));

  /* Release the memory. */
  if (file->pool)
    svn_pool_clear(file->pool);

  /* Reset pointers to objects previously allocated from FILE->POOL. */
  file->file = NULL;
  file->stream = NULL;
  file->l2p_stream = NULL;
  file->p2l_stream = NULL;

  /* Cause any index data getters to re-read the footer. */
  file->l2p_info.start = -1;
  return SVN_NO_ERROR;
}
