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

#include "rev_file.h"
#include "fs_fs.h"
#include "index.h"
#include "low_level.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "private/svn_io_private.h"
#include "svn_private_config.h"

/* Initialize the *FILE structure for REVISION in filesystem FS.  Set its
 * pool member to the provided POOL. */
static void
init_revision_file(svn_fs_fs__revision_file_t *file,
                   svn_fs_t *fs,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  file->is_packed = svn_fs_fs__is_packed_rev(fs, revision);
  file->start_revision = svn_fs_fs__packed_base_rev(fs, revision);

  file->file = NULL;
  file->stream = NULL;
  file->p2l_stream = NULL;
  file->l2p_stream = NULL;
  file->block_size = ffd->block_size;
  file->l2p_offset = -1;
  file->l2p_checksum = NULL;
  file->p2l_offset = -1;
  file->p2l_checksum = NULL;
  file->footer_offset = -1;
  file->pool = pool;
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

/* Core implementation of svn_fs_fs__open_pack_or_rev_file working on an
 * existing, initialized FILE structure.  If WRITABLE is TRUE, give write
 * access to the file - temporarily resetting the r/o state if necessary.
 */
static svn_error_t *
open_pack_or_rev_file(svn_fs_fs__revision_file_t *file,
                      svn_fs_t *fs,
                      svn_revnum_t rev,
                      svn_boolean_t writable,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t retry = FALSE;

  do
    {
      const char *path = svn_fs_fs__path_rev_absolute(fs, rev, scratch_pool);
      apr_file_t *apr_file;
      apr_int32_t flags = writable
                        ? APR_READ | APR_WRITE | APR_BUFFERED
                        : APR_READ | APR_BUFFERED;

      /* We may have to *temporarily* enable write access. */
      err = writable ? auto_make_writable(path, result_pool, scratch_pool)
                     : SVN_NO_ERROR;

      /* open the revision file in buffered r/o or r/w mode */
      if (!err)
        err = svn_io_file_open(&apr_file, path, flags, APR_OS_DEFAULT,
                               result_pool);

      if (!err)
        {
          file->file = apr_file;
          file->stream = svn_stream_from_aprfile2(apr_file, TRUE,
                                                  result_pool);
          file->is_packed = svn_fs_fs__is_packed_rev(fs, rev);

          return SVN_NO_ERROR;
        }

      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          if (ffd->format >= SVN_FS_FS__MIN_PACKED_FORMAT)
            {
              /* Could not open the file. This may happen if the
               * file once existed but got packed later. */
              svn_error_clear(err);

              /* if that was our 2nd attempt, leave it at that. */
              if (retry)
                return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                         _("No such revision %ld"), rev);

              /* We failed for the first time. Refresh cache & retry. */
              SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, scratch_pool));
              file->start_revision = svn_fs_fs__packed_base_rev(fs, rev);

              retry = TRUE;
            }
          else
            {
              svn_error_clear(err);
              return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                       _("No such revision %ld"), rev);
            }
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
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  *file = apr_palloc(result_pool, sizeof(**file));
  init_revision_file(*file, fs, rev, result_pool);

  return svn_error_trace(open_pack_or_rev_file(*file, fs, rev, FALSE,
                                               result_pool, scratch_pool));
}

svn_error_t *
svn_fs_fs__open_pack_or_rev_file_writable(svn_fs_fs__revision_file_t** file,
                                          svn_fs_t* fs,
                                          svn_revnum_t rev,
                                          apr_pool_t* result_pool,
                                          apr_pool_t *scratch_pool)
{
  *file = apr_palloc(result_pool, sizeof(**file));
  init_revision_file(*file, fs, rev, result_pool);

  return svn_error_trace(open_pack_or_rev_file(*file, fs, rev, TRUE,
                                               result_pool, scratch_pool));
}

svn_error_t *
svn_fs_fs__auto_read_footer(svn_fs_fs__revision_file_t *file)
{
  if (file->l2p_offset == -1)
    {
      apr_off_t filesize = 0;
      unsigned char footer_length;
      svn_stringbuf_t *footer;

      /* Determine file size. */
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
      SVN_ERR(svn_fs_fs__parse_footer(&file->l2p_offset, &file->l2p_checksum,
                                      &file->p2l_offset, &file->p2l_checksum,
                                      footer, file->start_revision,
                                      filesize - footer_length - 1,
                                      file->pool));
      file->footer_offset = filesize - footer_length - 1;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t* result_pool,
                               apr_pool_t *scratch_pool)
{
  apr_file_t *apr_file;
  SVN_ERR(svn_io_file_open(&apr_file,
                           svn_fs_fs__path_txn_proto_rev(fs, txn_id,
                                                         scratch_pool),
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result_pool));

  *file = apr_pcalloc(result_pool, sizeof(**file));
  (*file)->file = apr_file;
  (*file)->is_packed = FALSE;
  (*file)->start_revision = SVN_INVALID_REVNUM;
  (*file)->stream = svn_stream_from_aprfile2(apr_file, TRUE, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file)
{
  if (file->stream)
    SVN_ERR(svn_stream_close(file->stream));
  if (file->file)
    SVN_ERR(svn_io_file_close(file->file, file->pool));

  file->file = NULL;
  file->stream = NULL;
  file->l2p_stream = NULL;
  file->p2l_stream = NULL;

  return SVN_NO_ERROR;
}
