/* revprops.c --- everything needed to handle revprops in FSFS
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

#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"

#include "fs_fs.h"
#include "revprops.h"
#include "temp_serializer.h"
#include "util.h"

#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

svn_error_t *
svn_fs_fs__upgrade_pack_revprops(svn_fs_t *fs,
                                 svn_fs_upgrade_notify_t notify_func,
                                 void *notify_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *revprops_shard_path;
  const char *revprops_pack_file_dir;
  apr_int64_t shard;
  apr_int64_t first_unpacked_shard
    =  ffd->min_unpacked_rev / ffd->max_files_per_dir;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *revsprops_dir = svn_dirent_join(fs->path, PATH_REVPROPS_DIR,
                                              scratch_pool);
  int compression_level = ffd->compress_packed_revprops
                           ? SVN_DELTA_COMPRESSION_LEVEL_DEFAULT
                           : SVN_DELTA_COMPRESSION_LEVEL_NONE;

  /* first, pack all revprops shards to match the packed revision shards */
  for (shard = 0; shard < first_unpacked_shard; ++shard)
    {
      svn_pool_clear(iterpool);

      revprops_pack_file_dir = svn_dirent_join(revsprops_dir,
                   apr_psprintf(iterpool,
                                "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                                shard),
                   iterpool);
      revprops_shard_path = svn_dirent_join(revsprops_dir,
                       apr_psprintf(iterpool, "%" APR_INT64_T_FMT, shard),
                       iterpool);

      SVN_ERR(svn_fs_fs__pack_revprops_shard(revprops_pack_file_dir,
                                             revprops_shard_path,
                                             shard, ffd->max_files_per_dir,
                                             (int)(0.9 * ffd->revprop_pack_size),
                                             compression_level,
                                             ffd->flush_to_disk,
                                             cancel_func, cancel_baton,
                                             iterpool));
      if (notify_func)
        SVN_ERR(notify_func(notify_baton, shard,
                            svn_fs_upgrade_pack_revprops, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__upgrade_cleanup_pack_revprops(svn_fs_t *fs,
                                         svn_fs_upgrade_notify_t notify_func,
                                         void *notify_baton,
                                         svn_cancel_func_t cancel_func,
                                         void *cancel_baton,
                                         apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *revprops_shard_path;
  apr_int64_t shard;
  apr_int64_t first_unpacked_shard
    =  ffd->min_unpacked_rev / ffd->max_files_per_dir;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *revsprops_dir = svn_dirent_join(fs->path, PATH_REVPROPS_DIR,
                                              scratch_pool);

  /* delete the non-packed revprops shards afterwards */
  for (shard = 0; shard < first_unpacked_shard; ++shard)
    {
      svn_pool_clear(iterpool);

      revprops_shard_path = svn_dirent_join(revsprops_dir,
                       apr_psprintf(iterpool, "%" APR_INT64_T_FMT, shard),
                       iterpool);
      SVN_ERR(svn_fs_fs__delete_revprops_shard(revprops_shard_path,
                                               shard,
                                               ffd->max_files_per_dir,
                                               cancel_func, cancel_baton,
                                               iterpool));
      if (notify_func)
        SVN_ERR(notify_func(notify_baton, shard,
                            svn_fs_upgrade_cleanup_revprops, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

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


  /* name of the pack file (without folder path) */
  const char *filename;

  /* packed shard folder path */
  const char *folder;

  /* sum of values in SIZES */
  apr_size_t total_size;

  /* first revision in the pack (>= MANIFEST_START) */
  svn_revnum_t start_revision;

  /* size of the revprops in PACKED_REVPROPS */
  apr_array_header_t *sizes;

  /* offset of the revprops in PACKED_REVPROPS */
  apr_array_header_t *offsets;


  /* concatenation of the serialized representation of all revprops
   * in the pack, i.e. the pack content without header and compression */
  svn_stringbuf_t *packed_revprops;

  /* First revision covered by MANIFEST.
   * Will equal the shard start revision or 1, for the 1st shard. */
  svn_revnum_t manifest_start;

  /* content of the manifest.
   * Maps long(rev - MANIFEST_START) to const char* pack file name */
  apr_array_header_t *manifest;
} packed_revprops_t;

/* Parse the serialized revprops in CONTENT and return them in *PROPERTIES.
 * Also, put them into the revprop cache, if activated, for future use.
 *
 * The returned hash will be allocated in RESULT_POOL, SCRATCH_POOL is being
 * used for temporary allocations.
 */
static svn_error_t *
parse_revprop(apr_hash_t **properties,
              svn_fs_t *fs,
              svn_revnum_t revision,
              svn_string_t *content,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_stream_t *stream = svn_stream_from_string(content, scratch_pool);
  *properties = apr_hash_make(result_pool);

  SVN_ERR_W(svn_hash_read2(*properties, stream, SVN_HASH_TERMINATOR,
                           result_pool),
            apr_psprintf(scratch_pool, "Failed to parse revprops for r%ld.",
                         revision));

  return SVN_NO_ERROR;
}

void
svn_fs_fs__reset_revprop_cache(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  ffd->revprop_prefix = 0;
}

/* If FS has not a revprop cache prefix set, generate one.
 * Always call this before accessing the revprop cache.
 */
static svn_error_t *
prepare_revprop_cache(svn_fs_t *fs,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (!ffd->revprop_prefix)
    SVN_ERR(svn_atomic__unique_counter(&ffd->revprop_prefix));

  return SVN_NO_ERROR;
}

/* Store the unparsed revprop hash CONTENT for REVISION in FS's revprop
 * cache.  If CACHED is not NULL, set *CACHED if there already is such
 * an entry and skip the cache write in that case.  Use SCRATCH_POOL for
 * temporary allocations. */
static svn_error_t *
cache_revprops(svn_boolean_t *is_cached,
               svn_fs_t *fs,
               svn_revnum_t revision,
               svn_string_t *content,
               apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  pair_cache_key_t key;

  /* Make sure prepare_revprop_cache() has been called. */
  SVN_ERR_ASSERT(ffd->revprop_prefix);
  key.revision = revision;
  key.second = ffd->revprop_prefix;

  if (is_cached)
    {
      SVN_ERR(svn_cache__has_key(is_cached, ffd->revprop_cache, &key,
                                 scratch_pool));
      if (*is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(svn_cache__set(ffd->revprop_cache, &key, content, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the non-packed revprops for revision REV in FS, put them into the
 * revprop cache if PROPULATE_CACHE is set and return them in *PROPERTIES. 
 *
 * If the data could not be read due to an otherwise recoverable error,
 * leave *PROPERTIES unchanged. No error will be returned in that case.
 *
 * Allocations will be done in POOL.
 */
static svn_error_t *
read_non_packed_revprop(apr_hash_t **properties,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        svn_boolean_t populate_cache,
                        apr_pool_t *pool)
{
  svn_stringbuf_t *content = NULL;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t missing = FALSE;
  int i;

  for (i = 0;
       i < SVN_FS_FS__RECOVERABLE_RETRY_COUNT && !missing && !content;
       ++i)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__try_stringbuf_from_file(&content,
                              &missing,
                              svn_fs_fs__path_revprops(fs, rev, iterpool),
                              i + 1 < SVN_FS_FS__RECOVERABLE_RETRY_COUNT ,
                              iterpool));
    }

  if (content)
    {
      svn_string_t *as_string = svn_stringbuf__morph_into_string(content);
      SVN_ERR(parse_revprop(properties, fs, rev, as_string, pool, iterpool));

      if (populate_cache)
        SVN_ERR(cache_revprops(NULL, fs, rev, as_string, iterpool));
    }

  svn_pool_clear(iterpool);

  return SVN_NO_ERROR;
}

/* Return the minimum length of any packed revprop file name in REVPROPS. */
static apr_size_t
get_min_filename_len(packed_revprops_t *revprops)
{
  char number_buffer[SVN_INT64_BUFFER_SIZE];

  /* The revprop filenames have the format <REV>.<COUNT> - with <REV> being
   * at least the first rev in the shard and <COUNT> having at least one
   * digit.  Thus, the minimum is 2 + #decimal places in the start rev.
   */
  return svn__i64toa(number_buffer, revprops->manifest_start) + 2;
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
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content = NULL;
  const char *manifest_file_path;
  int idx, rev_count;
  char *buffer, *buffer_end;
  const char **filenames, **filenames_end;
  apr_size_t min_filename_len;

  /* Determine the dimensions. Rev 0 is excluded from the first shard. */
  rev_count = ffd->max_files_per_dir;
  revprops->manifest_start
    = revprops->revision - (revprops->revision % rev_count);
  if (revprops->manifest_start == 0)
    {
      ++revprops->manifest_start;
      --rev_count;
    }

  revprops->manifest = apr_array_make(result_pool, rev_count,
                                      sizeof(const char*));

  /* No line in the file can be less than this number of chars long. */
  min_filename_len = get_min_filename_len(revprops);

  /* Read the content of the manifest file */
  revprops->folder
    = svn_fs_fs__path_revprops_pack_shard(fs, revprops->revision,
                                          result_pool);
  manifest_file_path
    = svn_dirent_join(revprops->folder, PATH_MANIFEST, result_pool);

  SVN_ERR(svn_fs_fs__read_content(&content, manifest_file_path, result_pool));

  /* There CONTENT must have a certain minimal size and there no
   * unterminated lines at the end of the file.  Both guarantees also
   * simplify the parser loop below.
   */
  if (   content->len < rev_count * (min_filename_len + 1)
      || content->data[content->len - 1] != '\n')
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed revprop manifest for r%ld not "
                               "properly terminated"), revprops->revision);

  /* Chop (parse) the manifest CONTENT into filenames, one per line.
   * We only have to replace all newlines with NUL and add all line
   * starts to REVPROPS->MANIFEST.
   *
   * There must be exactly REV_COUNT lines and that is the number of
   * lines we parse from BUFFER to FILENAMES.  Set the end pointer for
   * the source BUFFER such that BUFFER+MIN_FILENAME_LEN is still valid
   * BUFFER_END is always valid due to CONTENT->LEN > MIN_FILENAME_LEN.
   *
   * Please note that this loop is performance critical for e.g. 'svn log'.
   * It is run 1000x per revprop access, i.e. per revision and about
   * 50 million times per sec (and CPU core).
   */
  for (filenames = (const char **)revprops->manifest->elts,
       filenames_end = filenames + rev_count,
       buffer = content->data,
       buffer_end = buffer + content->len - min_filename_len;
       (filenames < filenames_end) && (buffer < buffer_end);
       ++filenames)
    {
      /* BUFFER always points to the start of the next line / filename. */
      *filenames = buffer;

      /* Find the next EOL.  This is guaranteed to stay within the CONTENT
       * buffer because we left enough room after BUFFER_END and we know
       * we will always see a newline as the last non-NUL char. */
      buffer += min_filename_len;
      while (*buffer != '\n')
        ++buffer;

      /* Found EOL.  Turn it into the filename terminator and move BUFFER
       * to the start of the next line or CONTENT buffer end. */
      *buffer = '\0';
      ++buffer;
    }

  /* We must have reached the end of both buffers. */
  if (buffer < content->data + content->len)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed revprop manifest for r%ld "
                               "has too many entries"), revprops->revision);

  if (filenames < filenames_end)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed revprop manifest for r%ld "
                               "has too few entries"), revprops->revision);

  /* The target array has now exactly one entry per revision. */
  revprops->manifest->nelts = rev_count;

  /* Now get the file name */
  idx = (int)(revprops->revision - revprops->manifest_start);
  revprops->filename = APR_ARRAY_IDX(revprops->manifest, idx, const char*);

  return SVN_NO_ERROR;
}

/* Return TRUE, if revision R1 and R2 refer to the same shard in FS.
 */
static svn_boolean_t
same_shard(svn_fs_t *fs,
           svn_revnum_t r1,
           svn_revnum_t r2)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return (r1 / ffd->max_files_per_dir) == (r2 / ffd->max_files_per_dir);
}

/* Given FS and the full packed file content in REVPROPS->PACKED_REVPROPS,
 * fill the START_REVISION member, and make PACKED_REVPROPS point to the
 * first serialized revprop.  If READ_ALL is set, initialize the SIZES
 * and OFFSETS members as well.  If POPULATE_CACHE is set, cache all
 * revprops found in this pack.
 *
 * Parse the revprops for REVPROPS->REVISION and set the PROPERTIES as
 * well as the SERIALIZED_SIZE member.  If revprop caching has been
 * enabled, parse all revprops in the pack and cache them.
 */
static svn_error_t *
parse_packed_revprops(svn_fs_t *fs,
                      packed_revprops_t *revprops,
                      svn_boolean_t read_all,
                      svn_boolean_t populate_cache,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  apr_int64_t first_rev, count, i;
  apr_size_t offset;
  const char *header_end;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Initial value for the "Leaking bucket" pattern. */
  int bucket = 4;

  /* decompress (even if the data is only "stored", there is still a
   * length header to remove) */
  svn_stringbuf_t *compressed = revprops->packed_revprops;
  svn_stringbuf_t *uncompressed = svn_stringbuf_create_empty(result_pool);
  SVN_ERR(svn__decompress_zlib(compressed->data, compressed->len,
                               uncompressed, APR_SIZE_MAX));

  /* read first revision number and number of revisions in the pack */
  stream = svn_stream_from_stringbuf(uncompressed, scratch_pool);
  SVN_ERR(svn_fs_fs__read_number_from_stream(&first_rev, NULL, stream,
                                             iterpool));
  SVN_ERR(svn_fs_fs__read_number_from_stream(&count, NULL, stream,
                                             iterpool));

  /* Check revision range for validity. */
  if (   !same_shard(fs, revprops->revision, first_rev)
      || !same_shard(fs, revprops->revision, first_rev + count - 1)
      || count < 1)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Revprop pack for revision r%ld"
                               " contains revprops for r%ld .. r%ld"),
                             revprops->revision,
                             (svn_revnum_t)first_rev,
                             (svn_revnum_t)(first_rev + count -1));

  /* Since start & end are in the same shard, it is enough to just test
   * the FIRST_REV for being actually packed.  That will also cover the
   * special case of rev 0 never being packed. */
  if (!svn_fs_fs__is_packed_revprop(fs, first_rev))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Revprop pack for revision r%ld"
                               " starts at non-packed revisions r%ld"),
                             revprops->revision, (svn_revnum_t)first_rev);

  /* make PACKED_REVPROPS point to the first char after the header.
   * This is where the serialized revprops are. */
  header_end = strstr(uncompressed->data, "\n\n");
  if (header_end == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Header end not found"));

  offset = header_end - uncompressed->data + 2;

  revprops->packed_revprops = svn_stringbuf_create_empty(result_pool);
  revprops->packed_revprops->data = uncompressed->data + offset;
  revprops->packed_revprops->len = (apr_size_t)(uncompressed->len - offset);
  revprops->packed_revprops->blocksize = (apr_size_t)(uncompressed->blocksize
                                                      - offset);

  /* STREAM still points to the first entry in the sizes list. */
  revprops->start_revision = (svn_revnum_t)first_rev;
  if (read_all)
    {
      /* Init / construct REVPROPS members. */
      revprops->sizes = apr_array_make(result_pool, (int)count,
                                       sizeof(offset));
      revprops->offsets = apr_array_make(result_pool, (int)count,
                                         sizeof(offset));
    }

  /* Now parse, revision by revision, the size and content of each
   * revisions' revprops. */
  for (i = 0, offset = 0, revprops->total_size = 0; i < count; ++i)
    {
      apr_int64_t size;
      svn_string_t serialized;
      svn_revnum_t revision = (svn_revnum_t)(first_rev + i);
      svn_pool_clear(iterpool);

      /* read & check the serialized size */
      SVN_ERR(svn_fs_fs__read_number_from_stream(&size, NULL, stream,
                                                 iterpool));
      if (size > (apr_int64_t)revprops->packed_revprops->len - offset)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                        _("Packed revprop size exceeds pack file size"));

      /* Parse this revprops list, if necessary */
      serialized.data = revprops->packed_revprops->data + offset;
      serialized.len = (apr_size_t)size;

      if (revision == revprops->revision)
        {
          /* Parse (and possibly cache) the one revprop list we care about. */
          SVN_ERR(parse_revprop(&revprops->properties, fs, revision,
                                &serialized, result_pool, iterpool));
          revprops->serialized_size = serialized.len;

          /* If we only wanted the revprops for REVISION then we are done. */
          if (!read_all && !populate_cache)
            break;
        }

      if (populate_cache)
        {
          /* Adding all those revprops is expensive, in particular in a
           * multi-threaded environment.  There are situations where hit
           * rates are low and revprops get evicted before re-using them.
           *
           * We try to detect thosse cases here.
           * Only keep going while most (at least 2/3) aren't cached, yet. */
          svn_boolean_t already_cached;
          SVN_ERR(cache_revprops(&already_cached, fs, revision, &serialized,
                                 iterpool));

          /* Stop populating the cache once we encountered too many entries
           * already present relative to the numbers being added. */
          if (!already_cached)
            {
              ++bucket;
            }
          else
            {
              bucket -= 2;
              if (bucket < 0)
                populate_cache = FALSE;
            }
        }

      if (read_all)
        {
          /* fill REVPROPS data structures */
          APR_ARRAY_PUSH(revprops->sizes, apr_size_t) = serialized.len;
          APR_ARRAY_PUSH(revprops->offsets, apr_size_t) = offset;
        }
      revprops->total_size += serialized.len;

      offset += serialized.len;
    }

  return SVN_NO_ERROR;
}

/* In filesystem FS, read the packed revprops for revision REV into
 * *REVPROPS. Populate the revprop cache, if POPULATE_CACHE is set.
 * If you want to modify revprop contents / update REVPROPS, READ_ALL
 * must be set.  Otherwise, only the properties of REV are being provided.
 * Allocate data in POOL.
 */
static svn_error_t *
read_pack_revprop(packed_revprops_t **revprops,
                  svn_fs_t *fs,
                  svn_revnum_t rev,
                  svn_boolean_t read_all,
                  svn_boolean_t populate_cache,
                  apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t missing = FALSE;
  svn_error_t *err;
  packed_revprops_t *result;
  int i;

  /* someone insisted that REV is packed. Double-check if necessary */
  if (!svn_fs_fs__is_packed_revprop(fs, rev))
     SVN_ERR(svn_fs_fs__update_min_unpacked_rev(fs, iterpool));

  if (!svn_fs_fs__is_packed_revprop(fs, rev))
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                              _("No such packed revision %ld"), rev);

  /* initialize the result data structure */
  result = apr_pcalloc(pool, sizeof(*result));
  result->revision = rev;

  /* try to read the packed revprops. This may require retries if we have
   * concurrent writers. */
  for (i = 0;
       i < SVN_FS_FS__RECOVERABLE_RETRY_COUNT && !result->packed_revprops;
       ++i)
    {
      const char *file_path;
      svn_pool_clear(iterpool);

      /* there might have been concurrent writes.
       * Re-read the manifest and the pack file.
       */
      SVN_ERR(get_revprop_packname(fs, result, pool, iterpool));
      file_path  = svn_dirent_join(result->folder,
                                   result->filename,
                                   iterpool);
      SVN_ERR(svn_fs_fs__try_stringbuf_from_file(&result->packed_revprops,
                                &missing,
                                file_path,
                                i + 1 < SVN_FS_FS__RECOVERABLE_RETRY_COUNT,
                                pool));
    }

  /* the file content should be available now */
  if (!result->packed_revprops)
    return svn_error_createf(SVN_ERR_FS_PACKED_REVPROP_READ_FAILURE, NULL,
                  _("Failed to read revprop pack file for r%ld"), rev);

  /* parse it. RESULT will be complete afterwards. */
  err = parse_packed_revprops(fs, result, read_all, populate_cache, pool,
                              iterpool);
  svn_pool_destroy(iterpool);
  if (err)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                  _("Revprop pack file for r%ld is corrupt"), rev);

  *revprops = result;

  return SVN_NO_ERROR;
}

/* Read the revprops for revision REV in FS and return them in *PROPERTIES_P.
 *
 * Allocations will be done in POOL.
 */
svn_error_t *
svn_fs_fs__get_revision_proplist(apr_hash_t **proplist_p,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 svn_boolean_t refresh,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Only populate the cache if we did not just cross a sync barrier.
   * This is to eliminate overhead from code that always sets REFRESH.
   * For callers that want caching, the caching kicks in on read "later". */
  svn_boolean_t populate_cache = !refresh;

  /* not found, yet */
  *proplist_p = NULL;

  /* should they be available at all? */
  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, scratch_pool));

  if (refresh)
    {
      /* Previous cache contents is invalid now. */
      svn_fs_fs__reset_revprop_cache(fs);
    }
  else
    {
      /* Try cache lookup first. */
      svn_boolean_t is_cached;
      pair_cache_key_t key;

      /* Auto-alloc prefix and construct the key. */
      SVN_ERR(prepare_revprop_cache(fs, scratch_pool));
      key.revision = rev;
      key.second = ffd->revprop_prefix;

      /* The only way that this might error out is due to parser error. */
      SVN_ERR_W(svn_cache__get((void **) proplist_p, &is_cached,
                               ffd->revprop_cache, &key, result_pool),
                apr_psprintf(scratch_pool,
                             "Failed to parse revprops for r%ld.",
                             rev));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* if REV had not been packed when we began, try reading it from the
   * non-packed shard.  If that fails, we will fall through to packed
   * shard reads. */
  if (!svn_fs_fs__is_packed_revprop(fs, rev))
    {
      svn_error_t *err = read_non_packed_revprop(proplist_p, fs, rev,
                                                 populate_cache, result_pool);
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err)
              || ffd->format < SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
            return svn_error_trace(err);

          svn_error_clear(err);
          *proplist_p = NULL; /* in case read_non_packed_revprop changed it */
        }
    }

  /* if revprop packing is available and we have not read the revprops, yet,
   * try reading them from a packed shard.  If that fails, REV is most
   * likely invalid (or its revprops highly contested). */
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT && !*proplist_p)
    {
      packed_revprops_t *revprops;
      SVN_ERR(read_pack_revprop(&revprops, fs, rev, FALSE, populate_cache,
                                result_pool));
      *proplist_p = revprops->properties;
    }

  /* The revprops should have been there. Did we get them? */
  if (!*proplist_p)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("Could not read revprops for revision %ld"),
                             rev);

  return SVN_NO_ERROR;
}

/* Serialize the revision property list PROPLIST of revision REV in
 * filesystem FS to a non-packed file.  Return the name of that temporary
 * file in *TMP_PATH and the file path that it must be moved to in
 * *FINAL_PATH.
 *
 * Use POOL for allocations.
 */
static svn_error_t *
write_non_packed_revprop(const char **final_path,
                         const char **tmp_path,
                         svn_fs_t *fs,
                         svn_revnum_t rev,
                         apr_hash_t *proplist,
                         apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_file_t *file;
  svn_stream_t *stream;
  *final_path = svn_fs_fs__path_revprops(fs, rev, pool);

  /* ### do we have a directory sitting around already? we really shouldn't
     ### have to get the dirname here. */
  SVN_ERR(svn_io_open_unique_file3(&file, tmp_path,
                                   svn_dirent_dirname(*final_path, pool),
                                   svn_io_file_del_none, pool, pool));
  stream = svn_stream_from_aprfile2(file, TRUE, pool);
  SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* Flush temporary file to disk and close it. */
  if (ffd->flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(file, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* After writing the new revprop file(s), call this function to move the
 * file at TMP_PATH to FINAL_PATH and give it the permissions from
 * PERMS_REFERENCE.
 *
 * Finally, delete all the temporary files given in FILES_TO_DELETE.
 * The latter may be NULL.
 *
 * Use POOL for temporary allocations.
 */
static svn_error_t *
switch_to_new_revprop(svn_fs_t *fs,
                      const char *final_path,
                      const char *tmp_path,
                      const char *perms_reference,
                      apr_array_header_t *files_to_delete,
                      apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;

  SVN_ERR(svn_fs_fs__move_into_place(tmp_path, final_path, perms_reference,
                                     ffd->flush_to_disk, pool));

  /* Clean up temporary files, if necessary. */
  if (files_to_delete)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);
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

/* Write a pack file header to STREAM that starts at revision START_REVISION
 * and contains the indexes [START,END) of SIZES.
 */
static svn_error_t *
serialize_revprops_header(svn_stream_t *stream,
                          svn_revnum_t start_revision,
                          apr_array_header_t *sizes,
                          int start,
                          int end,
                          apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  int i;

  SVN_ERR_ASSERT(start < end);

  /* start revision and entry count */
  SVN_ERR(svn_stream_printf(stream, pool, "%ld\n", start_revision));
  SVN_ERR(svn_stream_printf(stream, pool, "%d\n", end - start));

  /* the sizes array */
  for (i = start; i < end; ++i)
    {
      /* Non-standard pool usage.
       *
       * We only allocate a few bytes each iteration -- even with a
       * million iterations we would still be in good shape memory-wise.
       */
      apr_size_t size = APR_ARRAY_IDX(sizes, i, apr_size_t);
      SVN_ERR(svn_stream_printf(stream, iterpool, "%" APR_SIZE_T_FMT "\n",
                                size));
    }

  /* the double newline char indicates the end of the header */
  SVN_ERR(svn_stream_puts(stream, "\n"));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Writes the a pack file to FILE.  It copies the serialized data
 * from REVPROPS for the indexes [START,END) except for index CHANGED_INDEX.
 *
 * The data for the latter is taken from NEW_SERIALIZED.  Note, that
 * CHANGED_INDEX may be outside the [START,END) range, i.e. no new data is
 * taken in that case but only a subset of the old data will be copied.
 *
 * NEW_TOTAL_SIZE is a hint for pre-allocating buffers of appropriate size.
 * POOL is used for temporary allocations.
 */
static svn_error_t *
repack_revprops(svn_fs_t *fs,
                packed_revprops_t *revprops,
                int start,
                int end,
                int changed_index,
                svn_stringbuf_t *new_serialized,
                apr_size_t new_total_size,
                apr_file_t *file,
                apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;
  int i;

  /* create data empty buffers and the stream object */
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure((apr_size_t)new_total_size, pool);
  svn_stringbuf_t *compressed
    = svn_stringbuf_create_empty(pool);
  stream = svn_stream_from_stringbuf(uncompressed, pool);

  /* write the header*/
  SVN_ERR(serialize_revprops_header(stream, revprops->start_revision + start,
                                    revprops->sizes, start, end, pool));

  /* append the serialized revprops */
  for (i = start; i < end; ++i)
    if (i == changed_index)
      {
        SVN_ERR(svn_stream_write(stream,
                                 new_serialized->data,
                                 &new_serialized->len));
      }
    else
      {
        apr_size_t size = APR_ARRAY_IDX(revprops->sizes, i, apr_size_t);
        apr_size_t offset = APR_ARRAY_IDX(revprops->offsets, i, apr_size_t);

        SVN_ERR(svn_stream_write(stream,
                                 revprops->packed_revprops->data + offset,
                                 &size));
      }

  /* flush the stream buffer (if any) to our underlying data buffer */
  SVN_ERR(svn_stream_close(stream));

  /* compress / store the data */
  SVN_ERR(svn__compress_zlib(uncompressed->data, uncompressed->len,
                             compressed,
                             ffd->compress_packed_revprops
                               ? SVN_DELTA_COMPRESSION_LEVEL_DEFAULT
                               : SVN_DELTA_COMPRESSION_LEVEL_NONE));

  /* finally, write the content to the target file, flush and close it */
  SVN_ERR(svn_io_file_write_full(file, compressed->data, compressed->len,
                                 NULL, pool));
  if (ffd->flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(file, pool));
  SVN_ERR(svn_io_file_close(file, pool));

  return SVN_NO_ERROR;
}

/* Allocate a new pack file name for revisions
 *     [REVPROPS->START_REVISION + START, REVPROPS->START_REVISION + END - 1]
 * of REVPROPS->MANIFEST.  Add the name of old file to FILES_TO_DELETE,
 * auto-create that array if necessary.  Return an open file *FILE that is
 * allocated in POOL.
 */
static svn_error_t *
repack_file_open(apr_file_t **file,
                 svn_fs_t *fs,
                 packed_revprops_t *revprops,
                 int start,
                 int end,
                 apr_array_header_t **files_to_delete,
                 apr_pool_t *pool)
{
  apr_int64_t tag;
  const char *tag_string;
  const char *new_filename;
  int i;
  int manifest_offset
    = (int)(revprops->start_revision - revprops->manifest_start);

  /* get the old (= current) file name and enlist it for later deletion */
  const char *old_filename = APR_ARRAY_IDX(revprops->manifest,
                                           start + manifest_offset,
                                           const char*);

  if (*files_to_delete == NULL)
    *files_to_delete = apr_array_make(pool, 3, sizeof(const char*));

  APR_ARRAY_PUSH(*files_to_delete, const char*)
    = svn_dirent_join(revprops->folder, old_filename, pool);

  /* increase the tag part, i.e. the counter after the dot */
  tag_string = strchr(old_filename, '.');
  if (tag_string == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Packed file '%s' misses a tag"),
                             old_filename);

  SVN_ERR(svn_cstring_atoi64(&tag, tag_string + 1));
  new_filename = apr_psprintf(pool, "%ld.%" APR_INT64_T_FMT,
                              revprops->start_revision + start,
                              ++tag);

  /* update the manifest to point to the new file */
  for (i = start; i < end; ++i)
    APR_ARRAY_IDX(revprops->manifest, i + manifest_offset, const char*)
      = new_filename;

  /* open the file */
  SVN_ERR(svn_io_file_open(file, svn_dirent_join(revprops->folder,
                                                 new_filename,
                                                 pool),
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT, pool));

  return SVN_NO_ERROR;
}

/* For revision REV in filesystem FS, set the revision properties to
 * PROPLIST.  Return a new file in *TMP_PATH that the caller shall move
 * to *FINAL_PATH to make the change visible.  Files to be deleted will
 * be listed in *FILES_TO_DELETE which may remain unchanged / unallocated.
 * Use POOL for allocations.
 */
static svn_error_t *
write_packed_revprop(const char **final_path,
                     const char **tmp_path,
                     apr_array_header_t **files_to_delete,
                     svn_fs_t *fs,
                     svn_revnum_t rev,
                     apr_hash_t *proplist,
                     apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  packed_revprops_t *revprops;
  svn_stream_t *stream;
  apr_file_t *file;
  svn_stringbuf_t *serialized;
  apr_size_t new_total_size;
  int changed_index;

  /* read contents of the current pack file */
  SVN_ERR(read_pack_revprop(&revprops, fs, rev, TRUE, FALSE, pool));

  /* serialize the new revprops */
  serialized = svn_stringbuf_create_empty(pool);
  stream = svn_stream_from_stringbuf(serialized, pool);
  SVN_ERR(svn_hash_write2(proplist, stream, SVN_HASH_TERMINATOR, pool));
  SVN_ERR(svn_stream_close(stream));

  /* calculate the size of the new data */
  changed_index = (int)(rev - revprops->start_revision);
  new_total_size = revprops->total_size - revprops->serialized_size
                 + serialized->len
                 + (revprops->offsets->nelts + 2) * SVN_INT64_BUFFER_SIZE;

  APR_ARRAY_IDX(revprops->sizes, changed_index, apr_size_t) = serialized->len;

  /* can we put the new data into the same pack as the before? */
  if (   new_total_size < ffd->revprop_pack_size
      || revprops->sizes->nelts == 1)
    {
      /* simply replace the old pack file with new content as we do it
       * in the non-packed case */

      *final_path = svn_dirent_join(revprops->folder, revprops->filename,
                                    pool);
      SVN_ERR(svn_io_open_unique_file3(&file, tmp_path, revprops->folder,
                                       svn_io_file_del_none, pool, pool));
      SVN_ERR(repack_revprops(fs, revprops, 0, revprops->sizes->nelts,
                              changed_index, serialized, new_total_size,
                              file, pool));
    }
  else
    {
      /* split the pack file into two of roughly equal size */
      int right_count, left_count, i;

      int left = 0;
      int right = revprops->sizes->nelts - 1;
      apr_size_t left_size = 2 * SVN_INT64_BUFFER_SIZE;
      apr_size_t right_size = 2 * SVN_INT64_BUFFER_SIZE;

      /* let left and right side grow such that their size difference
       * is minimal after each step. */
      while (left <= right)
        if (  left_size + APR_ARRAY_IDX(revprops->sizes, left, apr_size_t)
            < right_size + APR_ARRAY_IDX(revprops->sizes, right, apr_size_t))
          {
            left_size += APR_ARRAY_IDX(revprops->sizes, left, apr_size_t)
                      + SVN_INT64_BUFFER_SIZE;
            ++left;
          }
        else
          {
            right_size += APR_ARRAY_IDX(revprops->sizes, right, apr_size_t)
                       + SVN_INT64_BUFFER_SIZE;
            --right;
          }

       /* since the items need much less than SVN_INT64_BUFFER_SIZE
        * bytes to represent their length, the split may not be optimal */
      left_count = left;
      right_count = revprops->sizes->nelts - left;

      /* if new_size is large, one side may exceed the pack size limit.
       * In that case, split before and after the modified revprop.*/
      if (   left_size > ffd->revprop_pack_size
          || right_size > ffd->revprop_pack_size)
        {
          left_count = changed_index;
          right_count = revprops->sizes->nelts - left_count - 1;
        }

      /* write the new, split files */
      if (left_count)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops, 0,
                                   left_count, files_to_delete, pool));
          SVN_ERR(repack_revprops(fs, revprops, 0, left_count,
                                  changed_index, serialized, new_total_size,
                                  file, pool));
        }

      if (left_count + right_count < revprops->sizes->nelts)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops, changed_index,
                                   changed_index + 1, files_to_delete,
                                   pool));
          SVN_ERR(repack_revprops(fs, revprops, changed_index,
                                  changed_index + 1,
                                  changed_index, serialized, new_total_size,
                                  file, pool));
        }

      if (right_count)
        {
          SVN_ERR(repack_file_open(&file, fs, revprops,
                                   revprops->sizes->nelts - right_count,
                                   revprops->sizes->nelts,
                                   files_to_delete, pool));
          SVN_ERR(repack_revprops(fs, revprops,
                                  revprops->sizes->nelts - right_count,
                                  revprops->sizes->nelts, changed_index,
                                  serialized, new_total_size, file,
                                  pool));
        }

      /* write the new manifest */
      *final_path = svn_dirent_join(revprops->folder, PATH_MANIFEST, pool);
      SVN_ERR(svn_io_open_unique_file3(&file, tmp_path, revprops->folder,
                                       svn_io_file_del_none, pool, pool));
      stream = svn_stream_from_aprfile2(file, TRUE, pool);
      for (i = 0; i < revprops->manifest->nelts; ++i)
        {
          const char *filename = APR_ARRAY_IDX(revprops->manifest, i,
                                               const char*);
          SVN_ERR(svn_stream_printf(stream, pool, "%s\n", filename));
        }
      SVN_ERR(svn_stream_close(stream));
      if (ffd->flush_to_disk)
        SVN_ERR(svn_io_file_flush_to_disk(file, pool));
      SVN_ERR(svn_io_file_close(file, pool));
    }

  return SVN_NO_ERROR;
}

/* Set the revision property list of revision REV in filesystem FS to
   PROPLIST.  Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__set_revision_proplist(svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_hash_t *proplist,
                                 apr_pool_t *pool)
{
  svn_boolean_t is_packed;
  const char *final_path;
  const char *tmp_path;
  const char *perms_reference;
  apr_array_header_t *files_to_delete = NULL;

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));

  /* this info will not change while we hold the global FS write lock */
  is_packed = svn_fs_fs__is_packed_revprop(fs, rev);

  /* Serialize the new revprop data */
  if (is_packed)
    SVN_ERR(write_packed_revprop(&final_path, &tmp_path, &files_to_delete,
                                 fs, rev, proplist, pool));
  else
    SVN_ERR(write_non_packed_revprop(&final_path, &tmp_path,
                                     fs, rev, proplist, pool));

  /* Previous cache contents is invalid now. */
  svn_fs_fs__reset_revprop_cache(fs);

  /* We use the rev file of this revision as the perms reference,
   * because when setting revprops for the first time, the revprop
   * file won't exist and therefore can't serve as its own reference.
   * (Whereas the rev file should already exist at this point.)
   */
  perms_reference = svn_fs_fs__path_rev_absolute(fs, rev, pool);

  /* Now, switch to the new revprop data. */
  SVN_ERR(switch_to_new_revprop(fs, final_path, tmp_path, perms_reference,
                                files_to_delete, pool));

  return SVN_NO_ERROR;
}

/* Return TRUE, if for REVISION in FS, we can find the revprop pack file.
 * Use POOL for temporary allocations.
 * Set *MISSING, if the reason is a missing manifest or pack file.
 */
svn_boolean_t
svn_fs_fs__packed_revprop_available(svn_boolean_t *missing,
                                    svn_fs_t *fs,
                                    svn_revnum_t revision,
                                    apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stringbuf_t *content = NULL;

  /* try to read the manifest file */
  const char *folder
    = svn_fs_fs__path_revprops_pack_shard(fs, revision, pool);
  const char *manifest_path = svn_dirent_join(folder, PATH_MANIFEST, pool);

  svn_error_t *err = svn_fs_fs__try_stringbuf_from_file(&content,
                                                        missing,
                                                        manifest_path,
                                                        FALSE,
                                                        pool);

  /* if the manifest cannot be read, consider the pack files inaccessible
   * even if the file itself exists. */
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  if (*missing)
    return FALSE;

  /* parse manifest content until we find the entry for REVISION.
   * Revision 0 is never packed. */
  revision = revision < ffd->max_files_per_dir
           ? revision - 1
           : revision % ffd->max_files_per_dir;
  while (content->data)
    {
      char *next = strchr(content->data, '\n');
      if (next)
        {
          *next = 0;
          ++next;
        }

      if (revision-- == 0)
        {
          /* the respective pack file must exist (and be a file) */
          svn_node_kind_t kind;
          err = svn_io_check_path(svn_dirent_join(folder, content->data,
                                                  pool),
                                  &kind, pool);
          if (err)
            {
              svn_error_clear(err);
              return FALSE;
            }

          *missing = kind == svn_node_none;
          return kind == svn_node_file;
        }

      content->data = next;
    }

  return FALSE;
}


/****** Packing FSFS shards *********/

svn_error_t *
svn_fs_fs__copy_revprops(const char *pack_file_dir,
                         const char *pack_filename,
                         const char *shard_path,
                         svn_revnum_t start_rev,
                         svn_revnum_t end_rev,
                         apr_array_header_t *sizes,
                         apr_size_t total_size,
                         int compression_level,
                         svn_boolean_t flush_to_disk,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  svn_stream_t *pack_stream;
  apr_file_t *pack_file;
  svn_revnum_t rev;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* create empty data buffer and a write stream on top of it */
  svn_stringbuf_t *uncompressed
    = svn_stringbuf_create_ensure(total_size, scratch_pool);
  svn_stringbuf_t *compressed
    = svn_stringbuf_create_empty(scratch_pool);
  pack_stream = svn_stream_from_stringbuf(uncompressed, scratch_pool);

  /* write the pack file header */
  SVN_ERR(serialize_revprops_header(pack_stream, start_rev, sizes, 0,
                                    sizes->nelts, iterpool));

  /* Some useful paths. */
  SVN_ERR(svn_io_file_open(&pack_file, svn_dirent_join(pack_file_dir,
                                                       pack_filename,
                                                       scratch_pool),
                           APR_WRITE | APR_CREATE, APR_OS_DEFAULT,
                           scratch_pool));

  /* Iterate over the revisions in this shard, squashing them together. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      const char *path;
      svn_stream_t *stream;
      apr_file_t *file;

      svn_pool_clear(iterpool);

      /* Construct the file name. */
      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);

      /* Copy all the bits from the non-packed revprop file to the end of
       * the pack file.  Use unbuffered apr_file_t since we're going to
       * write using 16kb chunks. */
      SVN_ERR(svn_io_file_open(&file, path, APR_READ, APR_OS_DEFAULT,
                               iterpool));
      stream = svn_stream_from_aprfile2(file, FALSE, iterpool);
      SVN_ERR(svn_stream_copy3(stream, pack_stream,
                               cancel_func, cancel_baton, iterpool));
    }

  /* flush stream buffers to content buffer */
  SVN_ERR(svn_stream_close(pack_stream));

  /* compress the content (or just store it for COMPRESSION_LEVEL 0) */
  SVN_ERR(svn__compress_zlib(uncompressed->data, uncompressed->len,
                             compressed, compression_level));

  /* write the pack file content to disk */
  SVN_ERR(svn_io_file_write_full(pack_file, compressed->data, compressed->len,
                                 NULL, scratch_pool));
  if (flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(pack_file, scratch_pool));
  SVN_ERR(svn_io_file_close(pack_file, scratch_pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__pack_revprops_shard(const char *pack_file_dir,
                               const char *shard_path,
                               apr_int64_t shard,
                               int max_files_per_dir,
                               apr_int64_t max_pack_size,
                               int compression_level,
                               svn_boolean_t flush_to_disk,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool)
{
  const char *manifest_file_path, *pack_filename = NULL;
  apr_file_t *manifest_file;
  svn_stream_t *manifest_stream;
  svn_revnum_t start_rev, end_rev, rev;
  apr_size_t total_size;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *sizes;

  /* Sanitize config file values. */
  apr_size_t max_size = (apr_size_t)MIN(MAX(max_pack_size, 1),
                                        SVN_MAX_OBJECT_SIZE);

  /* Some useful paths. */
  manifest_file_path = svn_dirent_join(pack_file_dir, PATH_MANIFEST,
                                       scratch_pool);

  /* Remove any existing pack file for this shard, since it is incomplete. */
  SVN_ERR(svn_io_remove_dir2(pack_file_dir, TRUE, cancel_func, cancel_baton,
                             scratch_pool));

  /* Create the new directory and manifest file stream. */
  SVN_ERR(svn_io_dir_make(pack_file_dir, APR_OS_DEFAULT, scratch_pool));

  SVN_ERR(svn_io_file_open(&manifest_file, manifest_file_path,
                           APR_WRITE | APR_BUFFERED | APR_CREATE | APR_EXCL,
                           APR_OS_DEFAULT, scratch_pool));
  manifest_stream = svn_stream_from_aprfile2(manifest_file, TRUE,
                                             scratch_pool);

  /* revisions to handle. Special case: revision 0 */
  start_rev = (svn_revnum_t) (shard * max_files_per_dir);
  end_rev = (svn_revnum_t) ((shard + 1) * (max_files_per_dir) - 1);
  if (start_rev == 0)
    ++start_rev;
    /* Special special case: if max_files_per_dir is 1, then at this point
       start_rev == 1 and end_rev == 0 (!).  Fortunately, everything just
       works. */

  /* initialize the revprop size info */
  sizes = apr_array_make(scratch_pool, max_files_per_dir, sizeof(apr_size_t));
  total_size = 2 * SVN_INT64_BUFFER_SIZE;

  /* Iterate over the revisions in this shard, determine their size and
   * squashing them together into pack files. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      apr_finfo_t finfo;
      const char *path;

      svn_pool_clear(iterpool);

      /* Get the size of the file. */
      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);
      SVN_ERR(svn_io_stat(&finfo, path, APR_FINFO_SIZE, iterpool));

      /* If we already have started a pack file and this revprop cannot be
       * appended to it, write the previous pack file.  Note this overflow
       * check works because we enforced MAX_SIZE <= SVN_MAX_OBJECT_SIZE. */
      if (sizes->nelts != 0
          && (   finfo.size > max_size
              || total_size > max_size
              || SVN_INT64_BUFFER_SIZE + finfo.size > max_size - total_size))
        {
          SVN_ERR(svn_fs_fs__copy_revprops(pack_file_dir, pack_filename,
                                           shard_path, start_rev, rev-1,
                                           sizes, total_size,
                                           compression_level, flush_to_disk,
                                           cancel_func, cancel_baton,
                                           iterpool));

          /* next pack file starts empty again */
          apr_array_clear(sizes);
          total_size = 2 * SVN_INT64_BUFFER_SIZE;
          start_rev = rev;
        }

      /* Update the manifest. Allocate a file name for the current pack
       * file if it is a new one */
      if (sizes->nelts == 0)
        pack_filename = apr_psprintf(scratch_pool, "%ld.0", rev);

      SVN_ERR(svn_stream_printf(manifest_stream, iterpool, "%s\n",
                                pack_filename));

      /* add to list of files to put into the current pack file */
      APR_ARRAY_PUSH(sizes, apr_size_t) = finfo.size;
      total_size += SVN_INT64_BUFFER_SIZE + finfo.size;
    }

  /* write the last pack file */
  if (sizes->nelts != 0)
    SVN_ERR(svn_fs_fs__copy_revprops(pack_file_dir, pack_filename,
                                     shard_path, start_rev, rev-1,
                                     sizes, (apr_size_t)total_size,
                                     compression_level, flush_to_disk,
                                     cancel_func, cancel_baton, iterpool));

  /* flush the manifest file to disk and update permissions */
  SVN_ERR(svn_stream_close(manifest_stream));
  if (flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(manifest_file, iterpool));
  SVN_ERR(svn_io_file_close(manifest_file, iterpool));
  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__delete_revprops_shard(const char *shard_path,
                                 apr_int64_t shard,
                                 int max_files_per_dir,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool)
{
  if (shard == 0)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i;

      /* delete all files except the one for revision 0 */
      for (i = 1; i < max_files_per_dir; ++i)
        {
          const char *path;
          svn_pool_clear(iterpool);

          path = svn_dirent_join(shard_path,
                                 apr_psprintf(iterpool, "%d", i),
                                 iterpool);
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          SVN_ERR(svn_io_remove_file2(path, TRUE, iterpool));
        }

      svn_pool_destroy(iterpool);
    }
  else
    SVN_ERR(svn_io_remove_dir2(shard_path, TRUE,
                               cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

