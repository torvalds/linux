/* low_level.c --- low level r/w access to fs_fs file structures
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

#include "svn_fs.h"

#include "fs_fs.h"
#include "id.h"

/* Kinds that a node-rev can be. */
#define SVN_FS_FS__KIND_FILE          "file"
#define SVN_FS_FS__KIND_DIR           "dir"

/* The functions are grouped as follows:
 *
 * - revision trailer (up to format 6)
 * - revision footer (since format 7)
 * - changed path list
 * - node revision
 * - representation (as in "text:" and "props:" lines)
 * - representation header ("PLAIN" and "DELTA" lines)
 */

/* Given the last "few" bytes (should be at least 40) of revision REV in
 * TRAILER,  parse the last line and return the offset of the root noderev
 * in *ROOT_OFFSET and the offset of the changed paths list in
 * *CHANGES_OFFSET.  Offsets are relative to the revision's start offset.
 * ROOT_OFFSET and / or CHANGES_OFFSET may be NULL.
 *
 * Note that REV is only used to construct nicer error objects.
 */
svn_error_t *
svn_fs_fs__parse_revision_trailer(apr_off_t *root_offset,
                                  apr_off_t *changes_offset,
                                  svn_stringbuf_t *trailer,
                                  svn_revnum_t rev);

/* Given the offset of the root noderev in ROOT_OFFSET and the offset of
 * the changed paths list in CHANGES_OFFSET,  return the corresponding
 * revision's trailer.  Allocate it in RESULT_POOL.
 */
svn_stringbuf_t *
svn_fs_fs__unparse_revision_trailer(apr_off_t root_offset,
                                    apr_off_t changes_offset,
                                    apr_pool_t *result_pool);

/* Given the format 7+ revision / pack FOOTER, parse it destructively
 * and return the start offsets of the index data in *L2P_OFFSET and
 * *P2L_OFFSET, respectively.  Also, return the expected checksums in
 * in *L2P_CHECKSUM and *P2L_CHECKSUM.
 *
 * FOOTER_OFFSET is used for validation.
 *
 * Note that REV is only used to construct nicer error objects that
 * mention this revision.  Allocate the checksums in RESULT_POOL.
 */
svn_error_t *
svn_fs_fs__parse_footer(apr_off_t *l2p_offset,
                        svn_checksum_t **l2p_checksum,
                        apr_off_t *p2l_offset,
                        svn_checksum_t **p2l_checksum,
                        svn_stringbuf_t *footer,
                        svn_revnum_t rev,
                        apr_off_t footer_offset,
                        apr_pool_t *result_pool);

/* Given the offset of the L2P index data in L2P_OFFSET, the content
 * checksum in L2P_CHECKSUM and the offset plus checksum of the P2L
 * index data in P2L_OFFSET and P2L_CHECKSUM.
 *
 * Return the corresponding format 7+ revision / pack file footer.
 * Allocate it in RESULT_POOL and use SCRATCH_POOL for temporary.
 */
svn_stringbuf_t *
svn_fs_fs__unparse_footer(apr_off_t l2p_offset,
                          svn_checksum_t *l2p_checksum,
                          apr_off_t p2l_offset,
                          svn_checksum_t *p2l_checksum,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Read up to MAX_COUNT of the changes from STREAM and store them in
   *CHANGES, allocated in RESULT_POOL.  Do temporary allocations in
   SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__read_changes(apr_array_header_t **changes,
                        svn_stream_t *stream,
                        int max_count,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Callback function used by svn_fs_fs__read_changes_incrementally(),
 * asking the receiver to process to process CHANGE using BATON.  CHANGE
 * and SCRATCH_POOL will not be valid beyond the current callback invocation.
 */
typedef svn_error_t *(*svn_fs_fs__change_receiver_t)(
  void *baton,
  change_t *change,
  apr_pool_t *scratch_pool);

/* Read all the changes from STREAM and invoke CHANGE_RECEIVER on each change.
   Do all allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__read_changes_incrementally(svn_stream_t *stream,
                                      svn_fs_fs__change_receiver_t
                                        change_receiver,
                                      void *change_receiver_baton,
                                      apr_pool_t *scratch_pool);

/* Write the changed path info from CHANGES in filesystem FS to the
   output stream STREAM.  You may call this function multiple time on
   the same stream.  If you are writing to a (proto-)revision file,
   the last call must set TERMINATE_LIST to write an extra empty line
   that marks the end of the changed paths list.
   Perform temporary allocations in SCRATCH_POOL.
 */
svn_error_t *
svn_fs_fs__write_changes(svn_stream_t *stream,
                         svn_fs_t *fs,
                         apr_hash_t *changes,
                         svn_boolean_t terminate_list,
                         apr_pool_t *scratch_pool);

/* Read a node-revision from STREAM. Set *NODEREV to the new structure,
   allocated in RESULT_POOL. */
svn_error_t *
svn_fs_fs__read_noderev(node_revision_t **noderev,
                        svn_stream_t *stream,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Write the node-revision NODEREV into the stream OUTFILE, compatible with
   filesystem format FORMAT.  Only write mergeinfo-related metadata if
   INCLUDE_MERGEINFO is true.  Temporary allocations are from SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__write_noderev(svn_stream_t *outfile,
                         node_revision_t *noderev,
                         int format,
                         svn_boolean_t include_mergeinfo,
                         apr_pool_t *scratch_pool);

/* Parse the description of a representation from TEXT and store it
   into *REP_P.  TEXT will be invalidated by this call.  Allocate *REP_P in
   RESULT_POOL and use SCRATCH_POOL for temporaries. */
svn_error_t *
svn_fs_fs__parse_representation(representation_t **rep_p,
                                svn_stringbuf_t *text,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Return a formatted string, compatible with filesystem format FORMAT,
   that represents the location of representation REP.  If
   MUTABLE_REP_TRUNCATED is given, the rep is for props or dir contents,
   and only a "-1" revision number will be given for a mutable rep.
   If MAY_BE_CORRUPT is true, guard for NULL when constructing the string.
   Allocate the result in RESULT_POOL and temporaries in SCRATCH_POOL. */
svn_stringbuf_t *
svn_fs_fs__unparse_representation(representation_t *rep,
                                  int format,
                                  svn_boolean_t mutable_rep_truncated,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* This type enumerates all forms of representations that we support. */
typedef enum svn_fs_fs__rep_type_t
{
  /* this is a PLAIN representation */
  svn_fs_fs__rep_plain,

  /* this is a DELTA representation with no base representation */
  svn_fs_fs__rep_self_delta,

  /* this is a DELTA representation against some base representation */
  svn_fs_fs__rep_delta
} svn_fs_fs__rep_type_t;

/* This structure is used to hold the information stored in a representation
 * header. */
typedef struct svn_fs_fs__rep_header_t
{
  /* type of the representation, i.e. whether it is PLAIN, self-DELTA etc. */
  svn_fs_fs__rep_type_t type;

  /* if this rep is a delta against some other rep, that base rep can
   * be found in this revision.  Should be 0 if there is no base rep. */
  svn_revnum_t base_revision;

  /* if this rep is a delta against some other rep, that base rep can
   * be found at this item index within the base rep's revision.  Should
   * be 0 if there is no base rep. */
  apr_off_t base_item_index;

  /* if this rep is a delta against some other rep, this is the (deltified)
   * size of that base rep.  Should be 0 if there is no base rep. */
  svn_filesize_t base_length;

  /* length of the textual representation of the header in the rep or pack
   * file, including EOL.  Only valid after reading it from disk.
   * Should be 0 otherwise. */
  apr_size_t header_size;
} svn_fs_fs__rep_header_t;

/* Read the next line from STREAM and parse it as a text
   representation header.  Return the parsed entry in *HEADER, allocated
   in RESULT_POOL. Perform temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_fs__read_rep_header(svn_fs_fs__rep_header_t **header,
                           svn_stream_t *stream,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Write the representation HEADER to STREAM.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__write_rep_header(svn_fs_fs__rep_header_t *header,
                            svn_stream_t *stream,
                            apr_pool_t *scratch_pool);
