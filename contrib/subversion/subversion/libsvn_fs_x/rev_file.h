/* rev_file.h --- revision file and index access data structure
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

#ifndef SVN_LIBSVN_FS_X__REV_FILE_H
#define SVN_LIBSVN_FS_X__REV_FILE_H

#include "svn_fs.h"
#include "id.h"

/* In FSX, index data must be read in sync with the respective revision /
 * pack file.  I.e. we must use packed index files for packed rev files and
 * unpacked ones for non-packed rev files.  So, the whole point is to open
 * them with matching "is packed" setting in case some background pack
 * process was run.
 *
 * Another thing that this allows us is to lazily open the file, i.e. open
 * it upon first access.
 */

/* Opaque index stream type.
 */
typedef struct svn_fs_x__packed_number_stream_t
  svn_fs_x__packed_number_stream_t;

/* Location and content meta data for an index. */
typedef struct svn_fs_x__index_info_t
{
  /* Offset within the pack / rev file at which the index data starts. */
  apr_off_t start;

  /* First offset behind the index data. */
  apr_off_t end;

  /* MD5 checksum on the whole on-disk representation of the index. */
  svn_checksum_t *checksum;

} svn_fs_x__index_info_t;

/* Location and content meta data for a revision / pack file. */
typedef struct svn_fs_x__rev_file_info_t
{
  /* first (potentially only) revision in the rev / pack file.
   * SVN_INVALID_REVNUM for txn proto-rev files. */
  svn_revnum_t start_revision;

  /* the revision was packed when the first file / stream got opened */
  svn_boolean_t is_packed;

} svn_fs_x__rev_file_info_t;

/* Data file, including indexes data, and associated properties for
 * START_REVISION.  As the FILE is kept open, background pack operations
 * will not cause access to this file to fail.
 */
typedef struct svn_fs_x__revision_file_t svn_fs_x__revision_file_t;

/* Initialize the revision / pack file access structure in *FILE for reading
 * revision REV from filesystem FS.  The file will not be opened until the
 * first call to any of the access functions.
 *
 * Allocate *FILE in RESULT_POOL. */
svn_error_t *
svn_fs_x__rev_file_init(svn_fs_x__revision_file_t **file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool);

/* Open the correct revision file for REV with read and write access.
 * If necessary, temporarily reset the file's read-only state.  If the
 * filesystem FS has been packed, *FILE will be set to the packed file;
 * otherwise, set *FILE to the revision file for REV.
 *
 * Return SVN_ERR_FS_NO_SUCH_REVISION if the file doesn't exist.
 * Allocate *FILE in RESULT_POOL and use SCRATCH_POOLfor temporaries. */
svn_error_t *
svn_fs_x__rev_file_open_writable(svn_fs_x__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Open the proto-rev file of transaction TXN_ID in FS and return it in *FILE.
 * Allocate *FILE in RESULT_POOL use and SCRATCH_POOL for temporaries.. */
svn_error_t *
svn_fs_x__rev_file_open_proto_rev(svn_fs_x__revision_file_t **file,
                                  svn_fs_t *fs,
                                  svn_fs_x__txn_id_t txn_id,
                                  apr_pool_t* result_pool,
                                  apr_pool_t *scratch_pool);

/* Wrap the TEMP_FILE, used in the context of FS, into a revision file
 * struct, allocated in RESULT_POOL, and return it in *FILE.
 */
svn_error_t *
svn_fs_x__rev_file_wrap_temp(svn_fs_x__revision_file_t **file,
                             svn_fs_t *fs,
                             apr_file_t *temp_file,
                             apr_pool_t *result_pool);

/* Access functions */

/* Copy the L2P index info for FILE into *INFO.
 */
svn_error_t *
svn_fs_x__rev_file_info(svn_fs_x__rev_file_info_t *info,
                        svn_fs_x__revision_file_t *file);

/* Convenience wrapper around svn_io_file_name_get. */
svn_error_t *
svn_fs_x__rev_file_name(const char **filename,
                        svn_fs_x__revision_file_t *file,
                        apr_pool_t *result_pool);

/* Set *STREAM to the shared stream object of FILE.
 */
svn_error_t *
svn_fs_x__rev_file_stream(svn_stream_t **stream,
                          svn_fs_x__revision_file_t *file);

/* Set *APR_FILE to the shared file object of FILE.
 */
svn_error_t *
svn_fs_x__rev_file_get(apr_file_t **apr_file,
                       svn_fs_x__revision_file_t *file);

/* Set *STREAM to the shared L2P data stream of FILE.
 */
svn_error_t *
svn_fs_x__rev_file_l2p_index(svn_fs_x__packed_number_stream_t **stream,
                             svn_fs_x__revision_file_t *file);

/* Set *STREAM to the shared P2L data stream of FILE.
 */
svn_error_t *
svn_fs_x__rev_file_p2l_index(svn_fs_x__packed_number_stream_t **stream,
                             svn_fs_x__revision_file_t *file);

/* Copy the L2P index info for FILE into *INFO.
 */
svn_error_t *
svn_fs_x__rev_file_l2p_info(svn_fs_x__index_info_t *info,
                            svn_fs_x__revision_file_t *file);

/* Copy the P2L index info for FILE into *INFO.
 */
svn_error_t *
svn_fs_x__rev_file_p2l_info(svn_fs_x__index_info_t *info,
                            svn_fs_x__revision_file_t *file);

/* Set *SIZE to the length of the revision data in FILE.
 */
svn_error_t *
svn_fs_x__rev_file_data_size(svn_filesize_t *size,
                             svn_fs_x__revision_file_t *file);

/* File manipulation. */

/* Convenience wrapper around svn_io_file_aligned_seek. */
svn_error_t *
svn_fs_x__rev_file_seek(svn_fs_x__revision_file_t *file,
                        apr_off_t *buffer_start,
                        apr_off_t offset);

/* Convenience wrapper around svn_fs_x__get_file_offset. */
svn_error_t *
svn_fs_x__rev_file_offset(apr_off_t *offset,
                          svn_fs_x__revision_file_t *file);

/* Convenience wrapper around svn_io_file_read_full2. */
svn_error_t *
svn_fs_x__rev_file_read(svn_fs_x__revision_file_t *file,
                        void *buf,
                        apr_size_t nbytes);

/* Close all files and streams in FILE.  They will be reopened automatically
 * by any of the above access functions.
 */
svn_error_t *
svn_fs_x__close_revision_file(svn_fs_x__revision_file_t *file);

#endif
