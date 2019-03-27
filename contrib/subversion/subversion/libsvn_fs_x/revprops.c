/* revprops.c --- everything needed to handle revprops in FSX
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

#include <assert.h>
#include <apr_md5.h>

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"

#include "fs_x.h"
#include "low_level.h"
#include "revprops.h"
#include "util.h"
#include "transaction.h"

#include "private/svn_packed_data.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Give writing processes 10 seconds to replace an existing revprop
   file with a new one. After that time, we assume that the writing
   process got aborted and that we have re-read revprops. */
#define REVPROP_CHANGE_TIMEOUT (10 * 1000000)

/* In case of an inconsistent read, close the generation file, yield,
   re-open and re-read.  This is the number of times we try this before
   giving up. */
#define GENERATION_READ_RETRY_COUNT 100


/* Revprop caching management.
 *
 * Mechanism:
 * ----------
 *
 * Revprop caching needs to be activated and will be deactivated for the
 * respective FS instance if the necessary infrastructure could not be
 * initialized.  As long as no revprops are being read or changed, revprop
 * caching imposes no overhead.
 *
 * When activated, we cache revprops using (revision, generation) pairs
 * as keys with the generation being incremented upon every revprop change.
 * Since the cache is process-local, the generation needs to be tracked
 * for at least as long as the process lives but may be reset afterwards.
 * We track the revprop generation in a file that.
 *
 * A race condition exists between switching to the modified revprop data
 * and bumping the generation number.  In particular, the process may crash
 * just after switching to the new revprop data and before bumping the
 * generation.  To be able to detect this scenario, we bump the generation
 * twice per revprop change: once immediately before (creating an odd number)
 * and once after the atomic switch (even generation).
 *
 * A writer holding the write lock can immediately assume a crashed writer
 * in case of an odd generation or they would not have been able to acquire
 * the lock.  A reader detecting an odd generation will use that number and
 * be forced to re-read any revprop data - usually getting the new revprops
 * already.  If the generation file modification timestamp is too old, the
 * reader will assume a crashed writer, acquire the write lock and bump
 * the generation if it is still odd.  So, for about REVPROP_CHANGE_TIMEOUT
 * after the crash, reader caches may be stale.
 */

/* Read revprop generation as stored on disk for repository FS. The result is
 * returned in *CURRENT.  Call only for repos that support revprop caching.
 */
static svn_error_t *
read_revprop_generation_file(apr_int64_t *current,
                             svn_fs_t *fs,
                             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_error_t *err = SVN_NO_ERROR;
  const char *path = svn_fs_x__path_revprop_generation(fs, scratch_pool);

  /* Retry in case of incomplete file buffer updates. */
  for (i = 0; i < GENERATION_READ_RETRY_COUNT; ++i)
    {
      svn_stringbuf_t *buf;

      svn_error_clear(err);
      svn_pool_clear(iterpool);

      /* Read the generation file. */
      err = svn_stringbuf_from_file2(&buf, path, iterpool);

      /* If we could read the file, it should be complete due to our atomic
       * file replacement scheme. */
      if (!err)
        {
          svn_stringbuf_strip_whitespace(buf);
          SVN_ERR(svn_cstring_atoi64(current, buf->data));
          break;
        }

      /* Got unlucky the file was not available.  Retry. */
#if APR_HAS_THREADS
      apr_thread_yield();
#else
      apr_sleep(0);
#endif
    }

  svn_pool_destroy(iterpool);

  /* If we had to give up, propagate the error. */
  return svn_error_trace(err);
}

/* Write the CURRENT revprop generation to disk for repository FS.
 * Call only for repos that support revprop caching.
 */
static svn_error_t *
write_revprop_generation_file(svn_fs_t *fs,
                              apr_int64_t current,
                              apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *buffer;
  const char *path = svn_fs_x__path_revprop_generation(fs, scratch_pool);

  /* Invalidate our cached revprop generation in case the file operations
   * below fail. */
  ffd->revprop_generation = -1;

  /* Write the new number. */
  buffer = svn_stringbuf_createf(scratch_pool, "%" APR_INT64_T_FMT "\n",
                                 current);
  SVN_ERR(svn_io_write_atomic2(path, buffer->data, buffer->len,
                               path /* copy_perms */, FALSE,
                               scratch_pool));

  /* Remember it to spare us the re-read. */
  ffd->revprop_generation = current;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__reset_revprop_generation_file(svn_fs_t *fs,
                                        apr_pool_t *scratch_pool)
{
  /* Write the initial revprop generation file contents. */
  SVN_ERR(write_revprop_generation_file(fs, 0, scratch_pool));

  return SVN_NO_ERROR;
}

/* Test whether revprop cache and necessary infrastructure are
   available in FS. */
static svn_boolean_t
has_revprop_cache(svn_fs_t *fs,
                  apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  /* is the cache enabled? */
  return ffd->revprop_cache != NULL;
}

/* Baton structure for revprop_generation_fixup. */
typedef struct revprop_generation_fixup_t
{
  /* revprop generation to read */
  apr_int64_t *generation;

  /* file system context */
  svn_fs_t *fs;
} revprop_generation_upgrade_t;

/* If the revprop generation has an odd value, it means the original writer
   of the revprop got killed. We don't know whether that process as able
   to change the revprop data but we assume that it was. Therefore, we
   increase the generation in that case to basically invalidate everyone's
   cache content.
   Execute this only while holding the write lock to the repo in baton->FFD.
 */
static svn_error_t *
revprop_generation_fixup(void *void_baton,
                         apr_pool_t *scratch_pool)
{
  revprop_generation_upgrade_t *baton = void_baton;
  svn_fs_x__data_t *ffd = baton->fs->fsap_data;
  assert(ffd->has_write_lock);

  /* Maybe, either the original revprop writer or some other reader has
     already corrected / bumped the revprop generation.  Thus, we need
     to read it again.  However, we will now be the only ones changing
     the file contents due to us holding the write lock. */
  SVN_ERR(read_revprop_generation_file(baton->generation, baton->fs,
                                       scratch_pool));

  /* Cause everyone to re-read revprops upon their next access, if the
     last revprop write did not complete properly. */
  if (*baton->generation % 2)
    {
      ++*baton->generation;
      SVN_ERR(write_revprop_generation_file(baton->fs,
                                            *baton->generation,
                                            scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read the current revprop generation of FS and its value in FS->FSAP_DATA.
   Also, detect aborted / crashed writers and recover from that. */
static svn_error_t *
read_revprop_generation(svn_fs_t *fs,
                        apr_pool_t *scratch_pool)
{
  apr_int64_t current = 0;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  /* read the current revprop generation number */
  SVN_ERR(read_revprop_generation_file(&current, fs, scratch_pool));

  /* is an unfinished revprop write under the way? */
  if (current % 2)
    {
      svn_boolean_t timeout = FALSE;

      /* Has the writer process been aborted?
       * Either by timeout or by us being the writer now.
       */
      if (!ffd->has_write_lock)
        {
          apr_time_t mtime;
          SVN_ERR(svn_io_file_affected_time(&mtime,
                        svn_fs_x__path_revprop_generation(fs, scratch_pool),
                        scratch_pool));
          timeout = apr_time_now() > mtime + REVPROP_CHANGE_TIMEOUT;
        }

      if (ffd->has_write_lock || timeout)
        {
          revprop_generation_upgrade_t baton;
          baton.generation = &current;
          baton.fs = fs;

          /* Ensure that the original writer process no longer exists by
           * acquiring the write lock to this repository.  Then, fix up
           * the revprop generation.
           */
          if (ffd->has_write_lock)
            SVN_ERR(revprop_generation_fixup(&baton, scratch_pool));
          else
            SVN_ERR(svn_fs_x__with_write_lock(fs, revprop_generation_fixup,
                                              &baton, scratch_pool));
        }
    }

  /* return the value we just got */
  ffd->revprop_generation = current;
  return SVN_NO_ERROR;
}

void
svn_fs_x__invalidate_revprop_generation(svn_fs_t *fs)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  ffd->revprop_generation = -1;
}

/* Return TRUE if the revprop generation value in FS->FSAP_DATA is valid. */
static svn_boolean_t
is_generation_valid(svn_fs_t *fs)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  return ffd->revprop_generation >= 0;
}

/* Set the revprop generation in FS to the next odd number to indicate
   that there is a revprop write process under way.  Update the value
   in FS->FSAP_DATA accordingly.  If the change times out, readers shall
   recover from that state & re-read revprops.
   This is a no-op for repo formats that don't support revprop caching. */
static svn_error_t *
begin_revprop_change(svn_fs_t *fs,
                     apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  SVN_ERR_ASSERT(ffd->has_write_lock);

  /* Set the revprop generation to an odd value to indicate
   * that a write is in progress.
   */
  SVN_ERR(read_revprop_generation(fs, scratch_pool));
  ++ffd->revprop_generation;
  SVN_ERR_ASSERT(ffd->revprop_generation % 2);
  SVN_ERR(write_revprop_generation_file(fs, ffd->revprop_generation,
                                        scratch_pool));

  return SVN_NO_ERROR;
}

/* Set the revprop generation in FS to the next even generation after
   the odd value in FS->FSAP_DATA to indicate that
   a) readers shall re-read revprops, and
   b) the write process has been completed (no recovery required).
   This is a no-op for repo formats that don't support revprop caching. */
static svn_error_t *
end_revprop_change(svn_fs_t *fs,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  SVN_ERR_ASSERT(ffd->has_write_lock);
  SVN_ERR_ASSERT(ffd->revprop_generation % 2);

  /* Set the revprop generation to an even value to indicate
   * that a write has been completed.  Since we held the write
   * lock, nobody else could have updated the file contents.
   */
  SVN_ERR(write_revprop_generation_file(fs, ffd->revprop_generation + 1,
                                        scratch_pool));

  return SVN_NO_ERROR;
}

/* Represents an entry in the packed revprop manifest.
 * There is one such entry per pack file. */
typedef struct manifest_entry_t
{
  /* First revision in the pack file. */
  svn_revnum_t start_rev;

  /* Tag (a counter) appended to the file name to distinguish it from
     outdated ones. */
  apr_uint64_t tag;
} manifest_entry_t;

/* Container for all data required to access the packed revprop file
 * for a given REVISION.  This structure will be filled incrementally
 * by read_pack_revprops() its sub-routines.
 */
typedef struct packed_revprops_t
{
  /* revision number to read (not necessarily the first in the pack) */
  svn_revnum_t revision;

  /* the actual revision properties */
  apr_hash_t *properties;

  /* their size when serialized to a single string
   * (as found in PACKED_REVPROPS) */
  apr_size_t serialized_size;


  /* manifest entry describing the pack file */
  manifest_entry_t entry;

  /* packed shard folder path */
  const char *folder;

  /* sum of values in SIZES */
  apr_size_t total_size;

  /* Array of svn_string_t, containing the serialized revprops for
   * REVISION * I. */
  apr_array_header_t *revprops;

  /* content of the manifest.
   * Sorted list of manifest_entry_t. */
  apr_array_header_t *manifest;
} packed_revprops_t;

/* Parse the serialized revprops in CONTENT and return them in *PROPERTIES.
 * Also, put them into the revprop cache, if activated, for future use.
 * Three more parameters are being used to update the revprop cache: FS is
 * our file system, the revprops belong to REVISION.
 *
 * The returned hash will be allocated in RESULT_POOL, SCRATCH_POOL is
 * being used for temporary allocations.
 */
static svn_error_t *
parse_revprop(apr_hash_t **properties,
              svn_fs_t *fs,
              svn_revnum_t revision,
              const svn_string_t *content,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  SVN_ERR_W(svn_fs_x__parse_properties(properties, content, result_pool),
            apr_psprintf(scratch_pool, "Failed to parse revprops for r%ld.",
                         revision));

  if (has_revprop_cache(fs, scratch_pool))
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;
      svn_fs_x__pair_cache_key_t key = { 0 };

      SVN_ERR_ASSERT(is_generation_valid(fs));

      key.revision = revision;
      key.second = ffd->revprop_generation;
      SVN_ERR(svn_cache__set(ffd->revprop_cache, &key, *properties,
                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Verify the checksum attached to CONTENT and remove it.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
verify_checksum(svn_stringbuf_t *content,
                apr_pool_t *scratch_pool)
{
  const apr_byte_t *digest;
  svn_checksum_t *actual, *expected;

  /* Verify the checksum. */
  if (content->len < sizeof(apr_uint32_t))
    return svn_error_create(SVN_ERR_CORRUPT_PACKED_DATA, NULL,
                            "File too short");

  content->len -= sizeof(apr_uint32_t);
  digest = (apr_byte_t *)content->data + content->len;

  expected = svn_checksum__from_digest_fnv1a_32x4(digest, scratch_pool);
  SVN_ERR(svn_checksum(&actual, svn_checksum_fnv1a_32x4, content->data,
                       content->len, scratch_pool));

  if (!svn_checksum_match(actual, expected))
    SVN_ERR(svn_checksum_mismatch_err(expected, actual, scratch_pool, 
                                      "checksum mismatch"));

  return SVN_NO_ERROR;
}

/* Read the non-packed revprops for revision REV in FS, put them into the
 * revprop cache if activated and return them in *PROPERTIES.
 *
 * If the data could not be read due to an otherwise recoverable error,
 * leave *PROPERTIES unchanged. No error will be returned in that case.
 *
 * Allocate *PROPERTIES in RESULT_POOL and temporaries in SCRATCH_POOL.
 */
static svn_error_t *
read_non_packed_revprop(apr_hash_t **properties,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *content = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t missing = FALSE;
  int i;

  for (i = 0;
       i < SVN_FS_X__RECOVERABLE_RETRY_COUNT && !missing && !content;
       ++i)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_x__try_stringbuf_from_file(&content,
                                  &missing,
                                  svn_fs_x__path_revprops(fs, rev, iterpool),
                                  i + 1 < SVN_FS_X__RECOVERABLE_RETRY_COUNT,
                                  iterpool));
    }

  if (content)
    {
      svn_string_t *as_string;

      /* Consistency check. */
      SVN_ERR_W(verify_checksum(content, scratch_pool),
                apr_psprintf(scratch_pool,
                             "Revprop file for r%ld is corrupt",
                             rev));

      /* The contents string becomes part of the *PROPERTIES structure, i.e.
       * we must make sure it lives at least as long as the latter. */
      as_string = svn_string_create_from_buf(content, result_pool);
      SVN_ERR(parse_revprop(properties, fs, rev, as_string,
                            result_pool, iterpool));
    }

  svn_pool_clear(iterpool);

  return SVN_NO_ERROR;
}

/* Serialize ROOT into FILE and append a checksum to it.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_packed_data_checksummed(svn_packed__data_root_t *root,
                              apr_file_t *file,
                              apr_pool_t *scratch_pool)
{
  svn_checksum_t *checksum;
  svn_stream_t *stream;

  stream = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  stream = svn_checksum__wrap_write_stream(&checksum, stream,
                                           svn_checksum_fnv1a_32x4,
                                           scratch_pool);
  SVN_ERR(svn_packed__data_write(stream, root, scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  /* Append the checksum */
  SVN_ERR(svn_io_file_write_full(file, checksum->digest,
                                 svn_checksum_size(checksum), NULL,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* Serialize the packed revprops MANIFEST into FILE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_manifest(apr_file_t *file,
               const apr_array_header_t *manifest,
               apr_pool_t *scratch_pool)
{
  int i;
  svn_packed__data_root_t *root = svn_packed__data_create_root(scratch_pool);

  /* one top-level stream per struct element */
  svn_packed__int_stream_t *start_rev_stream
    = svn_packed__create_int_stream(root, TRUE, FALSE);
  svn_packed__int_stream_t *tag_stream
    = svn_packed__create_int_stream(root, FALSE, FALSE);

  /* serialize ENTRIES */
  for (i = 0; i < manifest->nelts; ++i)
    {
      manifest_entry_t *entry = &APR_ARRAY_IDX(manifest, i, manifest_entry_t);
      svn_packed__add_uint(start_rev_stream, entry->start_rev);
      svn_packed__add_uint(tag_stream, entry->tag);
    }

  /* Write to file and calculate the checksum. */
  SVN_ERR(write_packed_data_checksummed(root, file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read *ROOT from CONTENT and verify its checksum.  Allocate *ROOT in
 * RESULT_POOL and use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_packed_data_checksummed(svn_packed__data_root_t **root,
                             svn_stringbuf_t *content,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;

  SVN_ERR(verify_checksum(content, scratch_pool));

  stream = svn_stream_from_stringbuf(content, scratch_pool);
  SVN_ERR(svn_packed__data_read(root, stream, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the packed revprops manifest from the CONTENT buffer and return it
 * in *MANIFEST, allocated in RESULT_POOL.  REVISION is the revision number
 * to put into error messages.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_manifest(apr_array_header_t **manifest,
              svn_stringbuf_t *content,
              svn_revnum_t revision,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_size_t i;
  apr_size_t count;

  svn_packed__data_root_t *root;
  svn_packed__int_stream_t *start_rev_stream;
  svn_packed__int_stream_t *tag_stream;

  /* Verify the checksum and decode packed data. */
  SVN_ERR_W(read_packed_data_checksummed(&root, content, result_pool,
                                         scratch_pool),
            apr_psprintf(scratch_pool,
                         "Revprop manifest file for r%ld is corrupt",
                         revision));

  /* get streams */
  start_rev_stream = svn_packed__first_int_stream(root);
  tag_stream = svn_packed__next_int_stream(start_rev_stream);

  /* read ids array */
  count = svn_packed__int_count(start_rev_stream);
  *manifest = apr_array_make(result_pool, (int)count,
                            sizeof(manifest_entry_t));

  for (i = 0; i < count; ++i)
    {
      manifest_entry_t *entry = apr_array_push(*manifest);
      entry->start_rev = (svn_revnum_t)svn_packed__get_int(start_rev_stream);
      entry->tag = svn_packed__get_uint(tag_stream);
    }

  return SVN_NO_ERROR;
}

/* Implements the standard comparison function signature comparing the
 * manifest_entry_t(lhs).start_rev to svn_revnum_t(rhs). */
static int
compare_entry_revision(const void *lhs,
                       const void *rhs)
{
  const manifest_entry_t *entry = lhs;
  const svn_revnum_t *revision = rhs;

  if (entry->start_rev < *revision)
    return -1;

  return entry->start_rev == *revision ? 0 : 1;
}

/* Return the index in MANIFEST that has the info for the pack file
 * containing REVISION. */
static int
get_entry(apr_array_header_t *manifest,
          svn_revnum_t revision)
{
  manifest_entry_t *entry;
  int idx = svn_sort__bsearch_lower_bound(manifest, &revision,
                                          compare_entry_revision);

  assert(manifest->nelts > 0);
  if (idx >= manifest->nelts)
    return idx - 1;

  entry = &APR_ARRAY_IDX(manifest, idx, manifest_entry_t);
  if (entry->start_rev > revision && idx > 0)
    return idx - 1;

  return idx;
}

/* Return the full path of the revprop pack file given by ENTRY within
 * REVPROPS.  Allocate the result in RESULT_POOL. */
static const char *
get_revprop_pack_filepath(packed_revprops_t *revprops,
                          manifest_entry_t *entry,
                          apr_pool_t *result_pool)
{
  const char *filename = apr_psprintf(result_pool, "%ld.%" APR_UINT64_T_FMT,
                                      entry->start_rev, entry->tag);
  return svn_dirent_join(revprops->folder, filename, result_pool);
}

/* Given FS and REVPROPS->REVISION, fill the FILENAME, FOLDER and MANIFEST
 * members. Use RESULT_POOL for allocating results and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
get_revprop_packname(svn_fs_t *fs,
                     packed_revprops_t *revprops,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content = NULL;
  const char *manifest_file_path;
  int idx;
  svn_revnum_t previous_start_rev;
  int i;

  /* Determine the dimensions. Rev 0 is excluded from the first shard. */
  int rev_count = ffd->max_files_per_dir;
  svn_revnum_t manifest_start
    = revprops->revision - (revprops->revision % rev_count);
  if (manifest_start == 0)
    {
      ++manifest_start;
      --rev_count;
    }

  /* Read the content of the manifest file */
  revprops->folder = svn_fs_x__path_pack_shard(fs, revprops->revision,
                                               result_pool);
  manifest_file_path = svn_dirent_join(revprops->folder, PATH_MANIFEST,
                                       result_pool);
  SVN_ERR(svn_fs_x__read_content(&content, manifest_file_path, result_pool));
  SVN_ERR(read_manifest(&revprops->manifest, content, revprops->revision,
                        result_pool, scratch_pool));

  /* Verify the manifest data. */
  if (revprops->manifest->nelts == 0)
    return svn_error_createf(SVN_ERR_FS_CORRUPT_REVPROP_MANIFEST, NULL,
                             "Revprop manifest for r%ld is empty",
                             revprops->revision);

  previous_start_rev = 0;
  for (i = 0; i < revprops->manifest->nelts; ++i)
    {
      svn_revnum_t start_rev = APR_ARRAY_IDX(revprops->manifest, i,
                                             manifest_entry_t).start_rev;
      if (   start_rev < manifest_start
          || start_rev >= manifest_start + rev_count)
        return svn_error_createf(SVN_ERR_FS_CORRUPT_REVPROP_MANIFEST, NULL,
                                 "Revprop manifest for r%ld contains "
                                 "out-of-range revision r%ld",
                                 revprops->revision, start_rev);

      if (start_rev < previous_start_rev)
        return svn_error_createf(SVN_ERR_FS_CORRUPT_REVPROP_MANIFEST, NULL,
                                 "Entries in revprop manifest for r%ld "
                                 "are not ordered", revprops->revision);

      previous_start_rev = start_rev;
    }

  /* Now get the pack file description */
  idx = get_entry(revprops->manifest, revprops->revision);
  revprops->entry = APR_ARRAY_IDX(revprops->manifest, idx,
                                  manifest_entry_t);

  return SVN_NO_ERROR;
}

/* Return TRUE, if revision R1 and R2 refer to the same shard in FS.
 */
static svn_boolean_t
same_shard(svn_fs_t *fs,
           svn_revnum_t r1,
           svn_revnum_t r2)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  return (r1 / ffd->max_files_per_dir) == (r2 / ffd->max_files_per_dir);
}

/* Given FS and the full packed file content in CONTENT and make
 * PACKED_REVPROPS point to the first serialized revprop.  If READ_ALL
 * is set, initialize the SIZES and OFFSETS members as well.
 *
 * Parse the revprops for REVPROPS->REVISION and set the PROPERTIES as
 * well as the SERIALIZED_SIZE member.  If revprop caching has been
 * enabled, parse all revprops in the pack and cache them.
 */
static svn_error_t *
parse_packed_revprops(svn_fs_t *fs,
                      packed_revprops_t *revprops,
                      svn_stringbuf_t *content,
                      svn_boolean_t read_all,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_size_t count, i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t cache_all = has_revprop_cache(fs, scratch_pool);
  svn_packed__data_root_t *root;
  svn_packed__byte_stream_t *revprops_stream;
  svn_revnum_t first_rev = revprops->entry.start_rev;

  /* Verify the checksum and decode packed data. */
  SVN_ERR_W(read_packed_data_checksummed(&root, content, result_pool,
                                         scratch_pool),
            apr_psprintf(scratch_pool,
                         "Revprop pack file for r%ld is corrupt",
                         first_rev));

  /* get streams */
  revprops_stream = svn_packed__first_byte_stream(root);
  count = svn_packed__byte_block_count(revprops_stream);

  /* Check revision range for validity. */
  if (!same_shard(fs, first_rev, first_rev + count - 1) || count < 1)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Revprop pack for revision r%ld"
                               " contains revprops for r%ld .. r%ld"),
                             revprops->revision,
                             (svn_revnum_t)first_rev,
                             (svn_revnum_t)(first_rev + count -1));

  /* Since start & end are in the same shard, it is enough to just test
   * the FIRST_REV for being actually packed.  That will also cover the
   * special case of rev 0 never being packed. */
  if (!svn_fs_x__is_packed_revprop(fs, first_rev))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Revprop pack for revision r%ld"
                               " starts at non-packed revisions r%ld"),
                             revprops->revision, (svn_revnum_t)first_rev);

  /* Request all data (just references to data already expanded in ROOT) */
  revprops->revprops = apr_array_make(result_pool, (int)count,
                                      sizeof(svn_string_t));
  for (i = 0, revprops->total_size = 0; i < count; ++i)
    {
      svn_string_t *props = apr_array_push(revprops->revprops);
      props->data = svn_packed__get_bytes(revprops_stream, &props->len);

      revprops->total_size += props->len;
    }

  /* Now parse the serialized revprops. */
  for (i = 0; i < count; ++i)
    {
      const svn_string_t *serialized;
      svn_revnum_t revision;

      svn_pool_clear(iterpool);

      serialized = &APR_ARRAY_IDX(revprops->revprops, (int)i, svn_string_t);
      revision = first_rev + (long)i;

      /* Parse this revprops list, if necessary */
      if (revision == revprops->revision)
        {
          /* Parse (and possibly cache) the one revprop list we care about. */
          SVN_ERR(parse_revprop(&revprops->properties, fs, revision,
                                serialized, result_pool, iterpool));
          revprops->serialized_size = serialized->len;

          /* If we only wanted the revprops for REVISION then we are done. */
          if (!read_all && !cache_all)
            break;
        }
      else if (cache_all)
        {
          /* Parse and cache all other revprop lists. */
          apr_hash_t *properties;
          SVN_ERR(parse_revprop(&properties, fs, revision, serialized,
                                iterpool, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* In filesystem FS, read the packed revprops for revision REV into
 * *REVPROPS.  Populate the revprop cache, if enabled.  If you want to
 * modify revprop contents / update REVPROPS, READ_ALL must be set.
 * Otherwise, only the properties of REV are being provided.
 *
 * Allocate *PROPERTIES in RESULT_POOL and temporaries in SCRATCH_POOL.
 */
static svn_error_t *
read_pack_revprop(packed_revprops_t **revprops,
                  svn_fs_t *fs,
                  svn_revnum_t rev,
                  svn_boolean_t read_all,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t missing = FALSE;
  packed_revprops_t *result;
  int i;

  /* someone insisted that REV is packed. Double-check if necessary */
  if (!svn_fs_x__is_packed_revprop(fs, rev))
     SVN_ERR(svn_fs_x__update_min_unpacked_rev(fs, iterpool));

  if (!svn_fs_x__is_packed_revprop(fs, rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                              _("No such packed revision %ld"), rev);

  /* initialize the result data structure */
  result = apr_pcalloc(result_pool, sizeof(*result));
  result->revision = rev;

  /* try to read the packed revprops. This may require retries if we have
   * concurrent writers. */
  for (i = 0; i < SVN_FS_X__RECOVERABLE_RETRY_COUNT; ++i)
    {
      const char *file_path;
      svn_stringbuf_t *contents = NULL;

      svn_pool_clear(iterpool);

      /* there might have been concurrent writes.
       * Re-read the manifest and the pack file.
       */
      SVN_ERR(get_revprop_packname(fs, result, result_pool, iterpool));
      file_path = get_revprop_pack_filepath(result, &result->entry,
                                            iterpool);
      SVN_ERR(svn_fs_x__try_stringbuf_from_file(&contents,
                                &missing,
                                file_path,
                                i + 1 < SVN_FS_X__RECOVERABLE_RETRY_COUNT,
                                iterpool));

      if (contents)
        {
          SVN_ERR_W(parse_packed_revprops(fs, result, contents, read_all,
                                          result_pool, iterpool),
                    apr_psprintf(iterpool,
                                 "Revprop pack file for r%ld is corrupt",
                                 rev));
          break;
        }

      /* If we could not find the file, there was a write.
       * So, we should refresh our revprop generation info as well such
       * that others may find data we will put into the cache.  They would
       * consider it outdated, otherwise.
       */
      if (missing && has_revprop_cache(fs, iterpool))
        SVN_ERR(read_revprop_generation(fs, iterpool));
    }

  /* the file content should be available now */
  if (!result->revprops)
    return svn_error_createf(SVN_ERR_FS_PACKED_REVPROP_READ_FAILURE, NULL,
                  _("Failed to read revprop pack file for r%ld"), rev);

  *revprops = result;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_revision_proplist(apr_hash_t **proplist_p,
                                svn_fs_t *fs,
                                svn_revnum_t rev,
                                svn_boolean_t bypass_cache,
                                svn_boolean_t refresh,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  /* not found, yet */
  *proplist_p = NULL;

  /* should they be available at all? */
  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, scratch_pool));

  /* Ensure that the revprop generation info is valid. */
  if (refresh || !is_generation_valid(fs))
    SVN_ERR(read_revprop_generation(fs, scratch_pool));

  /* Try cache lookup first. */
  if (!bypass_cache && has_revprop_cache(fs, scratch_pool))
    {
      svn_boolean_t is_cached;
      svn_fs_x__pair_cache_key_t key = { 0 };

      key.revision = rev;
      key.second = ffd->revprop_generation;
      SVN_ERR(svn_cache__get((void **) proplist_p, &is_cached,
                             ffd->revprop_cache, &key, result_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* if REV had not been packed when we began, try reading it from the
   * non-packed shard.  If that fails, we will fall through to packed
   * shard reads. */
  if (!svn_fs_x__is_packed_revprop(fs, rev))
    {
      svn_error_t *err = read_non_packed_revprop(proplist_p, fs, rev,
                                                 result_pool, scratch_pool);
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err))
            return svn_error_trace(err);

          svn_error_clear(err);
          *proplist_p = NULL; /* in case read_non_packed_revprop changed it */
        }
    }

  /* if revprop packing is available and we have not read the revprops, yet,
   * try reading them from a packed shard.  If that fails, REV is most
   * likely invalid (or its revprops highly contested). */
  if (!*proplist_p)
    {
      packed_revprops_t *revprops;
      SVN_ERR(read_pack_revprop(&revprops, fs, rev, FALSE,
                                result_pool, scratch_pool));
      *proplist_p = revprops->properties;
    }

  /* The revprops should have been there. Did we get them? */
  if (!*proplist_p)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Could not read revprops for revision %ld"),
                             rev);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__write_non_packed_revprops(apr_file_t *file,
                                    apr_hash_t *proplist,
                                    apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  svn_checksum_t *checksum;

  stream = svn_stream_from_aprfile2(file, TRUE, scratch_pool);
  stream = svn_checksum__wrap_write_stream(&checksum, stream,
                                           svn_checksum_fnv1a_32x4,
                                           scratch_pool);
  SVN_ERR(svn_fs_x__write_properties(stream, proplist, scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  /* Append the checksum */
  SVN_ERR(svn_io_file_write_full(file, checksum->digest,
                                 svn_checksum_size(checksum), NULL,
                                 scratch_pool));

  return SVN_NO_ERROR;
}

/* Serialize the revision property list PROPLIST of revision REV in
 * filesystem FS to a non-packed file.  Return the name of that temporary
 * file in *TMP_PATH and the file path that it must be moved to in
 * *FINAL_PATH.  Schedule necessary fsync calls in BATCH.
 *
 * Allocate *FINAL_PATH and *TMP_PATH in RESULT_POOL.  Use SCRATCH_POOL
 * for temporary allocations.
 */
static svn_error_t *
write_non_packed_revprop(const char **final_path,
                         const char **tmp_path,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_hash_t *proplist,
                         svn_fs_x__batch_fsync_t *batch,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  *final_path = svn_fs_x__path_revprops(fs, rev, result_pool);

  *tmp_path = apr_pstrcat(result_pool, *final_path, ".tmp", SVN_VA_NULL);
  SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, *tmp_path,
                                          scratch_pool));

  SVN_ERR(svn_fs_x__write_non_packed_revprops(file, proplist, scratch_pool));

  return SVN_NO_ERROR;
}

/* After writing the new revprop file(s), call this function to move the
 * file at TMP_PATH to FINAL_PATH and give it the permissions from
 * PERMS_REFERENCE.  Schedule necessary fsync calls in BATCH.
 *
 * If indicated in BUMP_GENERATION, increase FS' revprop generation.
 * Finally, delete all the temporary files given in FILES_TO_DELETE.
 * The latter may be NULL.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
switch_to_new_revprop(svn_fs_t *fs,
                      const char *final_path,
                      const char *tmp_path,
                      const char *perms_reference,
                      apr_array_header_t *files_to_delete,
                      svn_boolean_t bump_generation,
                      svn_fs_x__batch_fsync_t *batch,
                      apr_pool_t *scratch_pool)
{
  /* Now, we may actually be replacing revprops. Make sure that all other
     threads and processes will know about this. */
  if (bump_generation)
    SVN_ERR(begin_revprop_change(fs, scratch_pool));

  /* Ensure the new file contents makes it to disk before switching over to
   * it. */
  SVN_ERR(svn_fs_x__batch_fsync_run(batch, scratch_pool));

  /* Make the revision visible to all processes and threads. */
  SVN_ERR(svn_fs_x__move_into_place(tmp_path, final_path, perms_reference,
                                    batch, scratch_pool));
  SVN_ERR(svn_fs_x__batch_fsync_run(batch, scratch_pool));

  /* Indicate that the update (if relevant) has been completed. */
  if (bump_generation)
    SVN_ERR(end_revprop_change(fs, scratch_pool));

  /* Clean up temporary files, if necessary. */
  if (files_to_delete)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      for (i = 0; i < files_to_delete->nelts; ++i)
        {
          const char *path = APR_ARRAY_IDX(files_to_delete, i, const char*);

          svn_pool_clear(iterpool);
          SVN_ERR(svn_io_remove_file2(path, TRUE, iterpool));
        }

      svn_pool_destroy(iterpool);
    }
  return SVN_NO_ERROR;
}

/* Writes the a pack file to FILE.  It copies the serialized data
 * from REVPROPS for the indexes [START,END).
 *
 * NEW_TOTAL_SIZE is a hint for pre-allocating buffers of appropriate size.
 * SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
repack_revprops(svn_fs_t *fs,
                packed_revprops_t *revprops,
                int start,
                int end,
                apr_size_t new_total_size,
                apr_file_t *file,
                apr_pool_t *scratch_pool)
{
  int i;

  svn_packed__data_root_t *root = svn_packed__data_create_root(scratch_pool);
  svn_packed__byte_stream_t *revprops_stream
    = svn_packed__create_bytes_stream(root);

  /* append the serialized revprops */
  for (i = start; i < end; ++i)
    {
      const svn_string_t *props
        = &APR_ARRAY_IDX(revprops->revprops, i, svn_string_t);

      svn_packed__add_bytes(revprops_stream, props->data, props->len);
    }

  /* Write to file. */
  SVN_ERR(write_packed_data_checksummed(root, file, scratch_pool));

  return SVN_NO_ERROR;
}

/* Allocate a new pack file name for revisions starting at START_REV in
 * REVPROPS->MANIFEST.  Add the name of old file to FILES_TO_DELETE,
 * auto-create that array if necessary.  Return an open file *FILE that is
 * allocated in RESULT_POOL.  Allocate the paths in *FILES_TO_DELETE from
 * the same pool that contains the array itself.  Schedule necessary fsync
 * calls in BATCH.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
repack_file_open(apr_file_t **file,
                 svn_fs_t *fs,
                 packed_revprops_t *revprops,
                 svn_revnum_t start_rev,
                 apr_array_header_t **files_to_delete,
                 svn_fs_x__batch_fsync_t *batch,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  manifest_entry_t new_entry;
  const char *new_path;
  int idx;

  /* We always replace whole pack files - possibly by more than one new file.
   * When we create the file for the first part of the pack, enlist the old
   * one for later deletion */
  SVN_ERR_ASSERT(start_rev >= revprops->entry.start_rev);

  if (*files_to_delete == NULL)
    *files_to_delete = apr_array_make(result_pool, 3, sizeof(const char*));

  if (revprops->entry.start_rev == start_rev)
    APR_ARRAY_PUSH(*files_to_delete, const char*)
      = get_revprop_pack_filepath(revprops, &revprops->entry,
                                  (*files_to_delete)->pool);

  /* Initialize the new manifest entry. Bump the tag part. */
  new_entry.start_rev = start_rev;
  new_entry.tag = revprops->entry.tag + 1;

  /* update the manifest to point to the new file */
  idx = get_entry(revprops->manifest, start_rev);
  if (revprops->entry.start_rev == start_rev)
    APR_ARRAY_IDX(revprops->manifest, idx, manifest_entry_t) = new_entry;
  else
    svn_sort__array_insert(revprops->manifest, &new_path, idx + 1);

  /* open the file */
  new_path = get_revprop_pack_filepath(revprops, &new_entry, scratch_pool);
  SVN_ERR(svn_fs_x__batch_fsync_open_file(file, batch, new_path,
                                          scratch_pool));

  return SVN_NO_ERROR;
}

/* Return the length of the serialized reprop list of index I in REVPROPS. */
static apr_size_t
props_len(packed_revprops_t *revprops,
          int i)
{
  return APR_ARRAY_IDX(revprops->revprops, i, svn_string_t).len;
}

/* For revision REV in filesystem FS, set the revision properties to
 * PROPLIST.  Return a new file in *TMP_PATH that the caller shall move
 * to *FINAL_PATH to make the change visible.  Files to be deleted will
 * be listed in *FILES_TO_DELETE which may remain unchanged / unallocated.
 * Schedule necessary fsync calls in BATCH.
 *
 * Allocate output values in RESULT_POOL and temporaries from SCRATCH_POOL.
 */
static svn_error_t *
write_packed_revprop(const char **final_path,
                     const char **tmp_path,
                     apr_array_header_t **files_to_delete,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_hash_t *proplist,
                     svn_fs_x__batch_fsync_t *batch,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  packed_revprops_t *revprops;
  svn_stream_t *stream;
  apr_file_t *file;
  svn_stringbuf_t *serialized;
  apr_size_t new_total_size;
  int changed_index;
  int count;

  /* read the current revprop generation. This value will not change
   * while we hold the global write lock to this FS. */
  if (has_revprop_cache(fs, scratch_pool))
    SVN_ERR(read_revprop_generation(fs, scratch_pool));

  /* read contents of the current pack file */
  SVN_ERR(read_pack_revprop(&revprops, fs, rev, TRUE,
                            scratch_pool, scratch_pool));

  /* serialize the new revprops */
  serialized = svn_stringbuf_create_empty(scratch_pool);
  stream = svn_stream_from_stringbuf(serialized, scratch_pool);
  SVN_ERR(svn_fs_x__write_properties(stream, proplist, scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  /* estimate the size of the new data */
  count = revprops->revprops->nelts;
  changed_index = (int)(rev - revprops->entry.start_rev);
  new_total_size = revprops->total_size - revprops->serialized_size
                 + serialized->len
                 + (count + 2) * SVN_INT64_BUFFER_SIZE;

  APR_ARRAY_IDX(revprops->revprops, changed_index, svn_string_t)
    = *svn_stringbuf__morph_into_string(serialized);

  /* can we put the new data into the same pack as the before? */
  if (new_total_size < ffd->revprop_pack_size || count == 1)
    {
      /* simply replace the old pack file with new content as we do it
       * in the non-packed case */

      *final_path = get_revprop_pack_filepath(revprops, &revprops->entry,
                                              result_pool);
      *tmp_path = apr_pstrcat(result_pool, *final_path, ".tmp", SVN_VA_NULL);
      SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, *tmp_path,
                                              scratch_pool));
      SVN_ERR(repack_revprops(fs, revprops, 0, count,
                              new_total_size, file, scratch_pool));
    }
  else
    {
      /* split the pack file into two of roughly equal size */
      int right_count, left_count;

      int left = 0;
      int right = count - 1;
      apr_size_t left_size = 2 * SVN_INT64_BUFFER_SIZE;
      apr_size_t right_size = 2 * SVN_INT64_BUFFER_SIZE;

      /* let left and right side grow such that their size difference
       * is minimal after each step. */
      while (left <= right)
        if (  left_size + props_len(revprops, left)
            < right_size + props_len(revprops, right))
          {
            left_size += props_len(revprops, left) + SVN_INT64_BUFFER_SIZE;
            ++left;
          }
        else
          {
            right_size += props_len(revprops, right) + SVN_INT64_BUFFER_SIZE;
            --right;
          }

       /* since the items need much less than SVN_INT64_BUFFER_SIZE
        * bytes to represent their length, the split may not be optimal */
      left_count = left;
      right_count = count - left;

      /* if new_size is large, one side may exceed the pack size limit.
       * In that case, split before and after the modified revprop.*/
      if (   left_size > ffd->revprop_pack_size
          || right_size > ffd->revprop_pack_size)
        {
          left_count = changed_index;
          right_count = count - left_count - 1;
        }

      /* Allocate this here such that we can call the repack functions with
       * the scratch pool alone. */
      if (*files_to_delete == NULL)
        *files_to_delete = apr_array_make(result_pool, 3,
                                          sizeof(const char*));

      /* write the new, split files */
      if (left_count)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops,
                                   revprops->entry.start_rev,
                                   files_to_delete, batch,
                                   scratch_pool, scratch_pool));
          SVN_ERR(repack_revprops(fs, revprops, 0, left_count,
                                  new_total_size, file, scratch_pool));
        }

      if (left_count + right_count < count)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops, rev,
                                   files_to_delete, batch,
                                   scratch_pool, scratch_pool));
          SVN_ERR(repack_revprops(fs, revprops, changed_index,
                                  changed_index + 1,
                                  new_total_size, file, scratch_pool));
        }

      if (right_count)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops, rev + 1,
                                   files_to_delete,  batch,
                                   scratch_pool, scratch_pool));
          SVN_ERR(repack_revprops(fs, revprops, count - right_count, count,
                                  new_total_size, file, scratch_pool));
        }

      /* write the new manifest */
      *final_path = svn_dirent_join(revprops->folder, PATH_MANIFEST,
                                    result_pool);
      *tmp_path = apr_pstrcat(result_pool, *final_path, ".tmp", SVN_VA_NULL);
      SVN_ERR(svn_fs_x__batch_fsync_open_file(&file, batch, *tmp_path,
                                              scratch_pool));
      SVN_ERR(write_manifest(file, revprops->manifest, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__set_revision_proplist(svn_fs_t *fs,
                                svn_revnum_t rev,
                                apr_hash_t *proplist,
                                apr_pool_t *scratch_pool)
{
  svn_boolean_t is_packed;
  svn_boolean_t bump_generation = FALSE;
  const char *final_path;
  const char *tmp_path;
  const char *perms_reference;
  apr_array_header_t *files_to_delete = NULL;
  svn_fs_x__batch_fsync_t *batch;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, scratch_pool));

  /* Perform all fsyncs through this instance. */
  SVN_ERR(svn_fs_x__batch_fsync_create(&batch, ffd->flush_to_disk,
                                       scratch_pool));

  /* this info will not change while we hold the global FS write lock */
  is_packed = svn_fs_x__is_packed_revprop(fs, rev);

  /* Test whether revprops already exist for this revision.
   * Only then will we need to bump the revprop generation.
   * The fact that they did not yet exist is never cached. */
  if (is_packed)
    {
      bump_generation = TRUE;
    }
  else
    {
      svn_node_kind_t kind;
      SVN_ERR(svn_io_check_path(svn_fs_x__path_revprops(fs, rev,
                                                        scratch_pool),
                                &kind, scratch_pool));
      bump_generation = kind != svn_node_none;
    }

  /* Serialize the new revprop data */
  if (is_packed)
    SVN_ERR(write_packed_revprop(&final_path, &tmp_path, &files_to_delete,
                                 fs, rev, proplist, batch, scratch_pool,
                                 scratch_pool));
  else
    SVN_ERR(write_non_packed_revprop(&final_path, &tmp_path,
                                     fs, rev, proplist, batch, 
                                     scratch_pool, scratch_pool));

  /* We use the rev file of this revision as the perms reference,
   * because when setting revprops for the first time, the revprop
   * file won't exist and therefore can't serve as its own reference.
   * (Whereas the rev file should already exist at this point.)
   */
  perms_reference = svn_fs_x__path_rev_absolute(fs, rev, scratch_pool);

  /* Now, switch to the new revprop data. */
  SVN_ERR(switch_to_new_revprop(fs, final_path, tmp_path, perms_reference,
                                files_to_delete, bump_generation, batch,
                                scratch_pool));

  return SVN_NO_ERROR;
}

/* Return TRUE, if for REVISION in FS, we can find the revprop pack file.
 * Use SCRATCH_POOL for temporary allocations.
 * Set *MISSING, if the reason is a missing manifest or pack file.
 */
svn_boolean_t
svn_fs_x__packed_revprop_available(svn_boolean_t *missing,
                                   svn_fs_t *fs,
                                   svn_revnum_t revision,
                                   apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  packed_revprops_t *revprops;
  svn_error_t *err;

  /* try to read the manifest file */
  revprops = apr_pcalloc(scratch_pool, sizeof(*revprops));
  revprops->revision = revision;
  err = get_revprop_packname(fs, revprops, scratch_pool, scratch_pool);

  /* if the manifest cannot be read, consider the pack files inaccessible
   * even if the file itself exists. */
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  /* the respective pack file must exist (and be a file) */
  err = svn_io_check_path(get_revprop_pack_filepath(revprops,
                                                    &revprops->entry,
                                                    scratch_pool),
                          &kind, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  *missing = kind == svn_node_none;
  return kind == svn_node_file;
}


/****** Packing FSX shards *********/

/* Copy revprop files for revisions [START_REV, END_REV) from SHARD_PATH
 * in filesystem FS to the pack file at PACK_FILE_NAME in PACK_FILE_DIR.
 *
 * The file sizes have already been determined and written to SIZES.
 * Please note that this function will be executed while the filesystem
 * has been locked and that revprops files will therefore not be modified
 * while the pack is in progress.
 *
 * COMPRESSION_LEVEL defines how well the resulting pack file shall be
 * compressed or whether is shall be compressed at all.  TOTAL_SIZE is
 * a hint on which initial buffer size we should use to hold the pack file
 * content.  Schedule necessary fsync calls in BATCH.
 *
 * CANCEL_FUNC and CANCEL_BATON are used as usual. Temporary allocations
 * are done in SCRATCH_POOL.
 */
static svn_error_t *
copy_revprops(svn_fs_t *fs,
              const char *pack_file_dir,
              const char *pack_filename,
              const char *shard_path,
              svn_revnum_t start_rev,
              svn_revnum_t end_rev,
              apr_array_header_t *sizes,
              apr_size_t total_size,
              int compression_level,
              svn_fs_x__batch_fsync_t *batch,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  apr_file_t *pack_file;
  svn_revnum_t rev;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  svn_packed__data_root_t *root = svn_packed__data_create_root(scratch_pool);
  svn_packed__byte_stream_t *stream
    = svn_packed__create_bytes_stream(root);

  /* Iterate over the revisions in this shard, squashing them together. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      const char *path;
      svn_stringbuf_t *props;

      svn_pool_clear(iterpool);

      /* Construct the file name. */
      path = svn_fs_x__path_revprops(fs, rev, iterpool);

      /* Copy all the bits from the non-packed revprop file to the end of
       * the pack file. */
      SVN_ERR(svn_stringbuf_from_file2(&props, path, iterpool));
      SVN_ERR_W(verify_checksum(props, iterpool),
                apr_psprintf(iterpool, "Failed to read revprops for r%ld.",
                             rev));

      svn_packed__add_bytes(stream, props->data, props->len);
    }

  /* Create the auto-fsync'ing pack file. */
  SVN_ERR(svn_fs_x__batch_fsync_open_file(&pack_file, batch,
                                          svn_dirent_join(pack_file_dir,
                                                          pack_filename,
                                                          scratch_pool),
                                          scratch_pool));

  /* write all to disk */
  SVN_ERR(write_packed_data_checksummed(root, pack_file, scratch_pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__pack_revprops_shard(svn_fs_t *fs,
                              const char *pack_file_dir,
                              const char *shard_path,
                              apr_int64_t shard,
                              int max_files_per_dir,
                              apr_int64_t max_pack_size,
                              int compression_level,
                              svn_fs_x__batch_fsync_t *batch,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool)
{
  const char *manifest_file_path, *pack_filename = NULL;
  apr_file_t *manifest_file;
  svn_revnum_t start_rev, end_rev, rev;
  apr_size_t total_size;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *sizes;
  apr_array_header_t *manifest;

  /* Sanitize config file values. */
  apr_size_t max_size = (apr_size_t)MIN(MAX(max_pack_size, 1),
                                        SVN_MAX_OBJECT_SIZE);

  /* Some useful paths. */
  manifest_file_path = svn_dirent_join(pack_file_dir, PATH_MANIFEST,
                                       scratch_pool);

  /* Create the manifest file. */
  SVN_ERR(svn_fs_x__batch_fsync_open_file(&manifest_file, batch,
                                          manifest_file_path, scratch_pool));

  /* revisions to handle. Special case: revision 0 */
  start_rev = (svn_revnum_t) (shard * max_files_per_dir);
  end_rev = (svn_revnum_t) ((shard + 1) * (max_files_per_dir) - 1);
  if (start_rev == 0)
    {
      /* Never pack revprops for r0, just copy it. */
      SVN_ERR(svn_io_copy_file(svn_fs_x__path_revprops(fs, 0, iterpool),
                               svn_dirent_join(pack_file_dir, "p0",
                                               scratch_pool),
                               TRUE,
                               iterpool));

      ++start_rev;
      /* Special special case: if max_files_per_dir is 1, then at this point
         start_rev == 1 and end_rev == 0 (!).  Fortunately, everything just
         works. */
    }

  /* initialize the revprop size info */
  sizes = apr_array_make(scratch_pool, max_files_per_dir, sizeof(apr_size_t));
  total_size = 2 * SVN_INT64_BUFFER_SIZE;

  manifest = apr_array_make(scratch_pool, 4, sizeof(manifest_entry_t));

  /* Iterate over the revisions in this shard, determine their size and
   * squashing them together into pack files. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      apr_finfo_t finfo;
      const char *path;

      svn_pool_clear(iterpool);

      /* Get the size of the file. */
      path = svn_fs_x__path_revprops(fs, rev, iterpool);
      SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, iterpool));

      /* If we already have started a pack file and this revprop cannot be
       * appended to it, write the previous pack file.  Note this overflow
       * check works because we enforced MAX_SIZE <= SVN_MAX_OBJECT_SIZE. */
      if (sizes->nelts != 0
          && (   finfo.size > max_size
              || total_size > max_size
              || SVN_INT64_BUFFER_SIZE + finfo.size > max_size - total_size))
        {
          SVN_ERR(copy_revprops(fs, pack_file_dir, pack_filename,
                                shard_path, start_rev, rev-1,
                                sizes, (apr_size_t)total_size,
                                compression_level, batch, cancel_func,
                                cancel_baton, iterpool));

          /* next pack file starts empty again */
          apr_array_clear(sizes);
          total_size = 2 * SVN_INT64_BUFFER_SIZE;
          start_rev = rev;
        }

      /* Update the manifest. Allocate a file name for the current pack
       * file if it is a new one */
      if (sizes->nelts == 0)
        {
          manifest_entry_t *entry = apr_array_push(manifest);
          entry->start_rev = rev;
          entry->tag = 0;

          pack_filename = apr_psprintf(scratch_pool, "%ld.0", rev);
        }

      /* add to list of files to put into the current pack file */
      APR_ARRAY_PUSH(sizes, apr_size_t) = finfo.size;
      total_size += SVN_INT64_BUFFER_SIZE + finfo.size;
    }

  /* write the last pack file */
  if (sizes->nelts != 0)
    SVN_ERR(copy_revprops(fs, pack_file_dir, pack_filename, shard_path,
                          start_rev, rev-1, sizes,
                          (apr_size_t)total_size, compression_level,
                          batch, cancel_func, cancel_baton, iterpool));

  SVN_ERR(write_manifest(manifest_file, manifest, iterpool));

  /* flush all data to disk and update permissions */
  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
