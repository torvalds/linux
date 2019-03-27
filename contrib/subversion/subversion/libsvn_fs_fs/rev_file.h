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

#ifndef SVN_LIBSVN_FS__REV_FILE_H
#define SVN_LIBSVN_FS__REV_FILE_H

#include "svn_fs.h"
#include "id.h"

/* In format 7, index files must be read in sync with the respective
 * revision / pack file.  I.e. we must use packed index files for packed
 * rev files and unpacked ones for non-packed rev files.  So, the whole
 * point is to open them with matching "is packed" setting in case some
 * background pack process was run.
 */

/* Opaque index stream type.
 */
typedef struct svn_fs_fs__packed_number_stream_t
  svn_fs_fs__packed_number_stream_t;

/* Data file, including indexes data, and associated properties for
 * START_REVISION.  As the FILE is kept open, background pack operations
 * will not cause access to this file to fail.
 */
typedef struct svn_fs_fs__revision_file_t
{
  /* first (potentially only) revision in the rev / pack file.
   * SVN_INVALID_REVNUM for txn proto-rev files. */
  svn_revnum_t start_revision;

  /* the revision was packed when the first file / stream got opened */
  svn_boolean_t is_packed;

  /* rev / pack file */
  apr_file_t *file;

  /* stream based on FILE and not NULL exactly when FILE is not NULL */
  svn_stream_t *stream;

  /* the opened P2L index stream or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *p2l_stream;

  /* the opened L2P index stream or NULL.  Always NULL for txns. */
  svn_fs_fs__packed_number_stream_t *l2p_stream;

  /* Copied from FS->FFD->BLOCK_SIZE upon creation.  It allows us to
   * use aligned seek() without having the FS handy. */
  apr_off_t block_size;

  /* Offset within FILE at which the rev data ends and the L2P index
   * data starts. Less than P2L_OFFSET. -1 if svn_fs_fs__auto_read_footer
   * has not been called, yet. */
  apr_off_t l2p_offset;

  /* MD5 checksum on the whole on-disk representation of the L2P index.
   * NULL if svn_fs_fs__auto_read_footer has not been called, yet. */
  svn_checksum_t *l2p_checksum;

  /* Offset within FILE at which the L2P index ends and the P2L index
   * data starts. Greater than L2P_OFFSET. -1 if svn_fs_fs__auto_read_footer
   * has not been called, yet. */
  apr_off_t p2l_offset;

  /* MD5 checksum on the whole on-disk representation of the P2L index.
   * NULL if svn_fs_fs__auto_read_footer has not been called, yet. */
  svn_checksum_t *p2l_checksum;

  /* Offset within FILE at which the P2L index ends and the footer starts.
   * Greater than P2L_OFFSET. -1 if svn_fs_fs__auto_read_footer has not
   * been called, yet. */
  apr_off_t footer_offset;

  /* pool containing this object */
  apr_pool_t *pool;
} svn_fs_fs__revision_file_t;

/* Open the correct revision file for REV.  If the filesystem FS has
 * been packed, *FILE will be set to the packed file; otherwise, set *FILE
 * to the revision file for REV.  Return SVN_ERR_FS_NO_SUCH_REVISION if the
 * file doesn't exist.  Allocate *FILE in RESULT_POOL and use SCRATCH_POOL
 * for temporaries. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file(svn_fs_fs__revision_file_t **file,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Open the correct revision file for REV with read and write access.
 * If necessary, temporarily reset the file's read-only state.  If the
 * filesystem FS has been packed, *FILE will be set to the packed file;
 * otherwise, set *FILE to the revision file for REV.
 *
 * Return SVN_ERR_FS_NO_SUCH_REVISION if the file doesn't exist.
 * Allocate *FILE in RESULT_POOL and use SCRATCH_POOLfor temporaries. */
svn_error_t *
svn_fs_fs__open_pack_or_rev_file_writable(svn_fs_fs__revision_file_t **file,
                                          svn_fs_t *fs,
                                          svn_revnum_t rev,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/* If the footer data in FILE has not been read, yet, do so now.
 * Index locations will only be read upon request as we assume they get
 * cached and the FILE is usually used for REP data access only.
 * Hence, the separate step.
 */
svn_error_t *
svn_fs_fs__auto_read_footer(svn_fs_fs__revision_file_t *file);

/* Open the proto-rev file of transaction TXN_ID in FS and return it in *FILE.
 * Allocate *FILE in RESULT_POOL use and SCRATCH_POOL for temporaries.. */
svn_error_t *
svn_fs_fs__open_proto_rev_file(svn_fs_fs__revision_file_t **file,
                               svn_fs_t *fs,
                               const svn_fs_fs__id_part_t *txn_id,
                               apr_pool_t* result_pool,
                               apr_pool_t *scratch_pool);

/* Close all files and streams in FILE.
 */
svn_error_t *
svn_fs_fs__close_revision_file(svn_fs_fs__revision_file_t *file);

#endif
