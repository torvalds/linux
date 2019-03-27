/* reps-strings.h : interpreting representations with respect to strings
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

#ifndef SVN_LIBSVN_FS_REPS_STRINGS_H
#define SVN_LIBSVN_FS_REPS_STRINGS_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_io.h"
#include "svn_fs.h"

#include "trail.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Get or create a mutable representation in FS, and set *NEW_REP_KEY to its
   key.

   TXN_ID is the id of the Subversion transaction under which this occurs.

   If REP_KEY is not null and is already a mutable representation, set
   *NEW_REP_KEY to REP_KEY, else create a brand new rep and set *NEW_REP_KEY
   to its key, allocated in POOL. */
svn_error_t *svn_fs_base__get_mutable_rep(const char **new_rep_key,
                                          const char *rep_key,
                                          svn_fs_t *fs,
                                          const char *txn_id,
                                          trail_t *trail,
                                          apr_pool_t *pool);


/* Delete REP_KEY from FS if REP_KEY is mutable, as part of trail, or
   do nothing if REP_KEY is immutable.  If a mutable rep is deleted,
   the string it refers to is deleted as well.  TXN_ID is the id of
   the Subversion transaction under which this occurs.

   If no such rep, return SVN_ERR_FS_NO_SUCH_REPRESENTATION.  */
svn_error_t *svn_fs_base__delete_rep_if_mutable(svn_fs_t *fs,
                                                const char *rep_key,
                                                const char *txn_id,
                                                trail_t *trail,
                                                apr_pool_t *pool);




/*** Reading and writing rep contents. ***/

/* Set *SIZE_P to the size of REP_KEY's contents in FS, as part of TRAIL.
   Note: this is the fulltext size, no matter how the contents are
   represented in storage.  */
svn_error_t *svn_fs_base__rep_contents_size(svn_filesize_t *size_p,
                                            svn_fs_t *fs,
                                            const char *rep_key,
                                            trail_t *trail,
                                            apr_pool_t *pool);


/* If MD5_CHECKSUM is non-NULL, set *MD5_CHECKSUM to the MD5 checksum
   for REP_KEY in FS, as part of TRAIL.

   If SHA1_CHECKSUM is non-NULL, set *SHA1_CHECKSUM to the SHA1
   checksum for REP_KEY in FS, as part of TRAIL.

   These are the prerecorded checksums for the rep's contents'
   fulltext.  If one or both of the checksums is not stored, do not
   calculate one dynamically, just put NULL into the respective return
   value.  (By convention, the NULL checksum is considered to match
   any checksum.) */
svn_error_t *
svn_fs_base__rep_contents_checksums(svn_checksum_t **md5_checksum,
                                    svn_checksum_t **sha1_checksum,
                                    svn_fs_t *fs,
                                    const char *rep_key,
                                    trail_t *trail,
                                    apr_pool_t *pool);


/* Set STR->data to the contents of REP_KEY in FS, and STR->len to the
   contents' length, as part of TRAIL.  The data is allocated in
   POOL.  If an error occurs, the effect on STR->data and
   STR->len is undefined.

   Note: this is the fulltext contents, no matter how the contents are
   represented in storage.  */
svn_error_t *svn_fs_base__rep_contents(svn_string_t *str,
                                       svn_fs_t *fs,
                                       const char *rep_key,
                                       trail_t *trail,
                                       apr_pool_t *pool);


/* Set *RS_P to a stream to read the contents of REP_KEY in FS.
   Allocate the stream in POOL.

   REP_KEY may be null, in which case reads just return 0 bytes.

   If USE_TRAIL_FOR_READS is TRUE, the stream's reads are part
   of TRAIL; otherwise, each read happens in an internal, one-off
   trail (though TRAIL is still required).  POOL may be TRAIL->pool. */
svn_error_t *
svn_fs_base__rep_contents_read_stream(svn_stream_t **rs_p,
                                      svn_fs_t *fs,
                                      const char *rep_key,
                                      svn_boolean_t use_trail_for_reads,
                                      trail_t *trail,
                                      apr_pool_t *pool);


/* Set *WS_P to a stream to write the contents of REP_KEY.  Allocate
   the stream in POOL.  TXN_ID is the id of the Subversion transaction
   under which this occurs.

   If USE_TRAIL_FOR_WRITES is TRUE, the stream's writes are part
   of TRAIL; otherwise, each write happens in an internal, one-off
   trail (though TRAIL is still required).  POOL may be TRAIL->pool.

   If REP_KEY is not mutable, writes to *WS_P will return the
   error SVN_ERR_FS_REP_NOT_MUTABLE.  */
svn_error_t *
svn_fs_base__rep_contents_write_stream(svn_stream_t **ws_p,
                                       svn_fs_t *fs,
                                       const char *rep_key,
                                       const char *txn_id,
                                       svn_boolean_t use_trail_for_writes,
                                       trail_t *trail,
                                       apr_pool_t *pool);



/*** Deltified storage. ***/

/* Offer TARGET the chance to store its contents as a delta against
   SOURCE, in FS, as part of TRAIL.  TARGET and SOURCE are both
   representation keys.

   This usually results in TARGET's data being stored as a diff
   against SOURCE; but it might not, if it turns out to be more
   efficient to store the contents some other way.  */
svn_error_t *svn_fs_base__rep_deltify(svn_fs_t *fs,
                                      const char *target,
                                      const char *source,
                                      trail_t *trail,
                                      apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_REPS_STRINGS_H */
