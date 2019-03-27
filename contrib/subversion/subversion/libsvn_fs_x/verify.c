/* verify.c --- verification of FSX filesystems
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

#include "verify.h"
#include "fs_x.h"
#include "svn_time.h"
#include "private/svn_subr_private.h"

#include "cached_data.h"
#include "rep-cache.h"
#include "revprops.h"
#include "util.h"
#include "index.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/** Verifying. **/

/* Baton type expected by verify_walker().  The purpose is to limit the
 * number of notifications sent.
 */
typedef struct verify_walker_baton_t
{
  /* number of calls to verify_walker() since the last clean */
  int iteration_count;

  /* progress notification callback to invoke periodically (may be NULL) */
  svn_fs_progress_notify_func_t notify_func;

  /* baton to use with NOTIFY_FUNC */
  void *notify_baton;

  /* remember the last revision for which we called notify_func */
  svn_revnum_t last_notified_revision;
} verify_walker_baton_t;

/* Used by svn_fs_x__verify().
   Implements svn_fs_x__walk_rep_reference().walker.  */
static svn_error_t *
verify_walker(svn_fs_x__representation_t *rep,
              void *baton,
              svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  verify_walker_baton_t *walker_baton = baton;

  /* notify and free resources periodically */
  if (walker_baton->iteration_count > 1000)
    {
      svn_revnum_t revision = svn_fs_x__get_revnum(rep->id.change_set);
      if (   walker_baton->notify_func
          && revision != walker_baton->last_notified_revision)
        {
          walker_baton->notify_func(revision,
                                    walker_baton->notify_baton,
                                    scratch_pool);
          walker_baton->last_notified_revision = revision;
        }

      walker_baton->iteration_count = 0;
    }

  /* access the repo data */
  SVN_ERR(svn_fs_x__check_rep(rep, fs, scratch_pool));

  /* update resource usage counters */
  walker_baton->iteration_count++;

  return SVN_NO_ERROR;
}

/* Verify the rep cache DB's consistency with our rev / pack data.
 * The function signature is similar to svn_fs_x__verify.
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_rep_cache(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  svn_boolean_t exists;

  /* rep-cache verification. */
  SVN_ERR(svn_fs_x__exists_rep_cache(&exists, fs, scratch_pool));
  if (exists)
    {
      /* provide a baton to allow the reuse of open file handles between
         iterations (saves 2/3 of OS level file operations). */
      verify_walker_baton_t *baton
        = apr_pcalloc(scratch_pool, sizeof(*baton));

      baton->last_notified_revision = SVN_INVALID_REVNUM;
      baton->notify_func = notify_func;
      baton->notify_baton = notify_baton;

      /* tell the user that we are now ready to do *something* */
      if (notify_func)
        notify_func(SVN_INVALID_REVNUM, notify_baton, scratch_pool);

      /* Do not attempt to walk the rep-cache database if its file does
         not exist,  since doing so would create it --- which may confuse
         the administrator.   Don't take any lock. */
      SVN_ERR(svn_fs_x__walk_rep_reference(fs, start, end,
                                           verify_walker, baton,
                                           cancel_func, cancel_baton,
                                           scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Verify that the MD5 checksum of the data between offsets START and END
 * in FILE matches the EXPECTED checksum.  If there is a mismatch use the
 * indedx NAME in the error message.  Supports cancellation with CANCEL_FUNC
 * and CANCEL_BATON.  SCRATCH_POOL is for temporary allocations. */
static svn_error_t *
verify_index_checksum(svn_fs_x__revision_file_t *file,
                      const char *name,
                      svn_fs_x__index_info_t *index_info,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *scratch_pool)
{
  unsigned char buffer[SVN__STREAM_CHUNK_SIZE];
  apr_off_t size = index_info->end - index_info->start;
  svn_checksum_t *actual;
  svn_checksum_ctx_t *context
    = svn_checksum_ctx_create(svn_checksum_md5, scratch_pool);

  /* Calculate the index checksum. */
  SVN_ERR(svn_fs_x__rev_file_seek(file, NULL, index_info->start));
  while (size > 0)
    {
      apr_size_t to_read = size > sizeof(buffer)
                         ? sizeof(buffer)
                         : (apr_size_t)size;
      SVN_ERR(svn_fs_x__rev_file_read(file, buffer, to_read));
      SVN_ERR(svn_checksum_update(context, buffer, to_read));
      size -= to_read;

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  SVN_ERR(svn_checksum_final(&actual, context, scratch_pool));

  /* Verify that it matches the expected checksum. */
  if (!svn_checksum_match(index_info->checksum, actual))
    {
      const char *file_name;

      SVN_ERR(svn_fs_x__rev_file_name(&file_name, file, scratch_pool));
      SVN_ERR(svn_checksum_mismatch_err(index_info->checksum, actual,
                                        scratch_pool, 
                                        _("%s checksum mismatch in file %s"),
                                        name, file_name));
    }

  return SVN_NO_ERROR;
}

/* Verify the MD5 checksums of the index data in the rev / pack file
 * containing revision START in FS.  If given, invoke CANCEL_FUNC with
 * CANCEL_BATON at regular intervals.  Use SCRATCH_POOL for temporary
 * allocations.
 */
static svn_error_t *
verify_index_checksums(svn_fs_t *fs,
                       svn_revnum_t start,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__revision_file_t *rev_file;
  svn_fs_x__index_info_t l2p_index_info;
  svn_fs_x__index_info_t p2l_index_info;

  /* Open the rev / pack file and read the footer */
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, start, scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_l2p_info(&l2p_index_info, rev_file));
  SVN_ERR(svn_fs_x__rev_file_p2l_info(&p2l_index_info, rev_file));

  /* Verify the index contents against the checksum from the footer. */
  SVN_ERR(verify_index_checksum(rev_file, "L2P index", &l2p_index_info,
                                cancel_func, cancel_baton, scratch_pool));
  SVN_ERR(verify_index_checksum(rev_file, "P2L index", &p2l_index_info,
                                cancel_func, cancel_baton, scratch_pool));

  /* Done. */
  SVN_ERR(svn_fs_x__close_revision_file(rev_file));

  return SVN_NO_ERROR;
}

/* Verify that for all log-to-phys index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the phys-to-log
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
compare_l2p_to_p2l_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  svn_revnum_t i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *max_ids;

  /* common file access structure */
  svn_fs_x__revision_file_t *rev_file;
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, start, scratch_pool));

  /* determine the range of items to check for each revision */
  SVN_ERR(svn_fs_x__l2p_get_max_ids(&max_ids, fs, start, count, scratch_pool,
                                    iterpool));

  /* check all items in all revisions if the given range */
  for (i = 0; i < max_ids->nelts; ++i)
    {
      apr_uint64_t k;
      apr_uint64_t max_id = APR_ARRAY_IDX(max_ids, i, apr_uint64_t);
      svn_revnum_t revision = start + i;

      for (k = 0; k < max_id; ++k)
        {
          apr_off_t offset;
          apr_uint32_t sub_item;
          svn_fs_x__id_t l2p_item;
          svn_fs_x__id_t *p2l_item;

          l2p_item.change_set = svn_fs_x__change_set_by_rev(revision);
          l2p_item.number = k;

          /* get L2P entry.  Ignore unused entries. */
          SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, rev_file,
                                        &l2p_item, iterpool));
          if (offset == -1)
            continue;

          /* find the corresponding P2L entry */
          SVN_ERR(svn_fs_x__p2l_item_lookup(&p2l_item, fs, rev_file,
                                            revision, offset, sub_item,
                                            iterpool, iterpool));

          if (p2l_item == NULL)
            return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                                     NULL,
                                     _("p2l index entry not found for "
                                       "PHYS o%s:s%ld returned by "
                                       "l2p index for LOG r%ld:i%ld"),
                                     apr_off_t_toa(scratch_pool, offset),
                                     (long)sub_item, revision, (long)k);

          if (!svn_fs_x__id_eq(&l2p_item, p2l_item))
            return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                                     NULL,
                                     _("p2l index info LOG r%ld:i%ld"
                                       " does not match "
                                       "l2p index for LOG r%ld:i%ld"),
                                     svn_fs_x__get_revnum(p2l_item->change_set),
                                     (long)p2l_item->number, revision,
                                     (long)k);

          svn_pool_clear(iterpool);
        }

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  SVN_ERR(svn_fs_x__close_revision_file(rev_file));

  return SVN_NO_ERROR;
}

/* Verify that for all phys-to-log index entries for revisions START to
 * START + COUNT-1 in FS there is a consistent entry in the log-to-phys
 * index.  If given, invoke CANCEL_FUNC with CANCEL_BATON at regular
 * intervals. Use SCRATCH_POOL for temporary allocations.
 *
 * Please note that we can only check on pack / rev file granularity and
 * must only be called for a single rev / pack file.
 */
static svn_error_t *
compare_p2l_to_l2p_index(svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t count,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *iterpool2 = svn_pool_create(scratch_pool);
  apr_off_t max_offset;
  apr_off_t offset = 0;

  /* common file access structure */
  svn_fs_x__revision_file_t *rev_file;
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, start, scratch_pool));

  /* get the size of the rev / pack file as covered by the P2L index */
  SVN_ERR(svn_fs_x__p2l_get_max_offset(&max_offset, fs, rev_file, start,
                                       scratch_pool));

  /* for all offsets in the file, get the P2L index entries and check
     them against the L2P index */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;
      svn_fs_x__p2l_entry_t *last_entry;
      int i;

      svn_pool_clear(iterpool);

      /* get all entries for the current block */
      SVN_ERR(svn_fs_x__p2l_index_lookup(&entries, fs, rev_file, start,
                                         offset, ffd->p2l_page_size,
                                         iterpool, iterpool));
      if (entries->nelts == 0)
        return svn_error_createf(SVN_ERR_FS_INDEX_CORRUPTION,
                                 NULL,
                                 _("p2l does not cover offset %s"
                                   " for revision %ld"),
                                  apr_off_t_toa(scratch_pool, offset), start);

      /* process all entries (and later continue with the next block) */
      last_entry
        = &APR_ARRAY_IDX(entries, entries->nelts-1, svn_fs_x__p2l_entry_t);
      offset = last_entry->offset + last_entry->size;

      for (i = 0; i < entries->nelts; ++i)
        {
          apr_uint32_t k;
          svn_fs_x__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t);

          /* check all sub-items for consist entries in the L2P index */
          for (k = 0; k < entry->item_count; ++k)
            {
              apr_off_t l2p_offset;
              apr_uint32_t sub_item;
              svn_fs_x__id_t *p2l_item = &entry->items[k];
              svn_revnum_t revision
                = svn_fs_x__get_revnum(p2l_item->change_set);

              svn_pool_clear(iterpool2);
              SVN_ERR(svn_fs_x__item_offset(&l2p_offset, &sub_item, fs,
                                            rev_file, p2l_item, iterpool2));

              if (sub_item != k || l2p_offset != entry->offset)
                return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                                         NULL,
                                         _("l2p index entry PHYS o%s:s%ld "
                                           "does not match p2l index value "
                                           "LOG r%ld:i%ld for PHYS o%s:s%ld"),
                                         apr_off_t_toa(scratch_pool,
                                                       l2p_offset),
                                         (long)sub_item,
                                         revision,
                                         (long)p2l_item->number,
                                         apr_off_t_toa(scratch_pool,
                                                       entry->offset),
                                         (long)k);
            }
        }

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool2);
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_fs_x__close_revision_file(rev_file));

  return SVN_NO_ERROR;
}

/* Items smaller than this can be read at once into a buffer and directly
 * be checksummed.  Larger items require stream processing.
 * Must be a multiple of 8. */
#define STREAM_THRESHOLD 4096

/* Verify that the next SIZE bytes read from FILE are NUL.  SIZE must not
 * exceed STREAM_THRESHOLD.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
expect_buffer_nul(svn_fs_x__revision_file_t *file,
                  apr_off_t size,
                  apr_pool_t *scratch_pool)
{
  union
  {
    unsigned char buffer[STREAM_THRESHOLD];
    apr_uint64_t chunks[STREAM_THRESHOLD / sizeof(apr_uint64_t)];
  } data;

  apr_size_t i;
  SVN_ERR_ASSERT(size <= STREAM_THRESHOLD);

  /* read the whole data block; error out on failure */
  data.chunks[(size - 1)/ sizeof(apr_uint64_t)] = 0;
  SVN_ERR(svn_fs_x__rev_file_read(file, data.buffer, size));

  /* chunky check */
  for (i = 0; i < size / sizeof(apr_uint64_t); ++i)
    if (data.chunks[i] != 0)
      break;

  /* byte-wise check upon mismatch or at the end of the block */
  for (i *= sizeof(apr_uint64_t); i < size; ++i)
    if (data.buffer[i] != 0)
      {
        const char *file_name;
        apr_off_t offset;

        SVN_ERR(svn_fs_x__rev_file_name(&file_name, file, scratch_pool));
        SVN_ERR(svn_fs_x__rev_file_offset(&offset, file));
        offset -= size - i;

        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Empty section in file %s contains "
                                   "non-NUL data at offset %s"),
                                 file_name,
                                 apr_off_t_toa(scratch_pool, offset));
      }

  return SVN_NO_ERROR;
}

/* Verify that the next SIZE bytes read from FILE are NUL.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
read_all_nul(svn_fs_x__revision_file_t *file,
             apr_off_t size,
             apr_pool_t *scratch_pool)
{
  for (; size >= STREAM_THRESHOLD; size -= STREAM_THRESHOLD)
    SVN_ERR(expect_buffer_nul(file, STREAM_THRESHOLD, scratch_pool));

  if (size)
    SVN_ERR(expect_buffer_nul(file, size, scratch_pool));

  return SVN_NO_ERROR;
}

/* Compare the ACTUAL checksum with the one expected by ENTRY.
 * Return an error in case of mismatch.  Use the name of FILE
 * in error message.  Allocate temporary data in SCRATCH_POOL.
 */
static svn_error_t *
expected_checksum(svn_fs_x__revision_file_t *file,
                  svn_fs_x__p2l_entry_t *entry,
                  apr_uint32_t actual,
                  apr_pool_t *scratch_pool)
{
  if (actual != entry->fnv1_checksum)
    {
      const char *file_name;

      SVN_ERR(svn_fs_x__rev_file_name(&file_name, file, scratch_pool));
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Checksum mismatch in item at offset %s of "
                                 "length %s bytes in file %s"),
                               apr_off_t_toa(scratch_pool, entry->offset),
                               apr_off_t_toa(scratch_pool, entry->size),
                               file_name);
    }

  return SVN_NO_ERROR;
}

/* Verify that the FNV checksum over the next ENTRY->SIZE bytes read
 * from FILE will match ENTRY's expected checksum.  SIZE must not
 * exceed STREAM_THRESHOLD.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
expected_buffered_checksum(svn_fs_x__revision_file_t *file,
                           svn_fs_x__p2l_entry_t *entry,
                           apr_pool_t *scratch_pool)
{
  unsigned char buffer[STREAM_THRESHOLD];
  SVN_ERR_ASSERT(entry->size <= STREAM_THRESHOLD);

  SVN_ERR(svn_fs_x__rev_file_read(file, buffer, (apr_size_t)entry->size));
  SVN_ERR(expected_checksum(file, entry,
                            svn__fnv1a_32x4(buffer, (apr_size_t)entry->size),
                            scratch_pool));

  return SVN_NO_ERROR;
}

/* Verify that the FNV checksum over the next ENTRY->SIZE bytes read from
 * FILE will match ENTRY's expected checksum.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
expected_streamed_checksum(svn_fs_x__revision_file_t *file,
                           svn_fs_x__p2l_entry_t *entry,
                           apr_pool_t *scratch_pool)
{
  unsigned char buffer[STREAM_THRESHOLD];
  svn_checksum_t *checksum;
  svn_checksum_ctx_t *context
    = svn_checksum_ctx_create(svn_checksum_fnv1a_32x4, scratch_pool);
  apr_off_t size = entry->size;

  while (size > 0)
    {
      apr_size_t to_read = size > sizeof(buffer)
                         ? sizeof(buffer)
                         : (apr_size_t)size;
      SVN_ERR(svn_fs_x__rev_file_read(file, buffer, to_read));
      SVN_ERR(svn_checksum_update(context, buffer, to_read));
      size -= to_read;
    }

  SVN_ERR(svn_checksum_final(&checksum, context, scratch_pool));
  SVN_ERR(expected_checksum(file, entry,
                            ntohl(*(const apr_uint32_t *)checksum->digest),
                            scratch_pool));

  return SVN_NO_ERROR;
}

/* Verify that for all phys-to-log index entries for revisions START to
 * START + COUNT-1 in FS match the actual pack / rev file contents.
 * If given, invoke CANCEL_FUNC with CANCEL_BATON at regular intervals.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * Please note that we can only check on pack / rev file granularity and
 * must only be called for a single rev / pack file.
 */
static svn_error_t *
compare_p2l_to_rev(svn_fs_t *fs,
                   svn_revnum_t start,
                   svn_revnum_t count,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_off_t max_offset;
  apr_off_t offset = 0;
  svn_fs_x__revision_file_t *rev_file;
  svn_fs_x__index_info_t l2p_index_info;

  /* open the pack / rev file that is covered by the p2l index */
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, start, scratch_pool));

  /* check file size vs. range covered by index */
  SVN_ERR(svn_fs_x__rev_file_l2p_info(&l2p_index_info, rev_file));
  SVN_ERR(svn_fs_x__p2l_get_max_offset(&max_offset, fs, rev_file, start,
                                       scratch_pool));

  if (l2p_index_info.start != max_offset)
    return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT, NULL,
                             _("File size of %s for revision r%ld does "
                               "not match p2l index size of %s"),
                             apr_off_t_toa(scratch_pool,
                                           l2p_index_info.start),
                             start,
                             apr_off_t_toa(scratch_pool,
                                           max_offset));

  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, 0));

  /* for all offsets in the file, get the P2L index entries and check
     them against the L2P index */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;
      int i;

      svn_pool_clear(iterpool);

      /* get all entries for the current block */
      SVN_ERR(svn_fs_x__p2l_index_lookup(&entries, fs, rev_file, start,
                                         offset, ffd->p2l_page_size,
                                         iterpool, iterpool));

      /* The above might have moved the file pointer.
       * Ensure we actually start reading at OFFSET.  */
      SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, offset));

      /* process all entries (and later continue with the next block) */
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_x__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t);

          /* skip bits we previously checked */
          if (i == 0 && entry->offset < offset)
            continue;

          /* skip zero-sized entries */
          if (entry->size == 0)
            continue;

          /* p2l index must cover all rev / pack file offsets exactly once */
          if (entry->offset != offset)
            return svn_error_createf(SVN_ERR_FS_INDEX_INCONSISTENT,
                                     NULL,
                                     _("p2l index entry for revision r%ld"
                                       " is non-contiguous between offsets "
                                       " %s and %s"),
                                     start,
                                     apr_off_t_toa(scratch_pool, offset),
                                     apr_off_t_toa(scratch_pool,
                                                   entry->offset));

          /* empty sections must contain NUL bytes only */
          if (entry->type == SVN_FS_X__ITEM_TYPE_UNUSED)
            {
              /* skip filler entry at the end of the p2l index */
              if (entry->offset != max_offset)
                SVN_ERR(read_all_nul(rev_file, entry->size, iterpool));
            }
          else
            {
              if (entry->size < STREAM_THRESHOLD)
                SVN_ERR(expected_buffered_checksum(rev_file, entry,
                                                   iterpool));
              else
                SVN_ERR(expected_streamed_checksum(rev_file, entry,
                                                   iterpool));
            }

          /* advance offset */
          offset += entry->size;
        }

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that the revprops of the revisions START to END in FS can be
 * accessed.  Invoke CANCEL_FUNC with CANCEL_BATON at regular intervals.
 *
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_revprops(svn_fs_t *fs,
                svn_revnum_t start,
                svn_revnum_t end,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  svn_revnum_t revision;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Invalidate the revprop generation once.
   * Use the cache inside the loop to speed up packed revprop access. */
  svn_fs_x__invalidate_revprop_generation(fs);

  for (revision = start; revision < end; ++revision)
    {
      svn_string_t *date;
      apr_time_t timetemp;

      svn_pool_clear(iterpool);

      /* Access the svn:date revprop.
       * This implies parsing all revprops for that revision. */
      SVN_ERR(svn_fs_x__revision_prop(&date, fs, revision,
                                      SVN_PROP_REVISION_DATE, FALSE,
                                      iterpool, iterpool));

      /* The time stamp is the only revprop that, if given, needs to
       * have a valid content. */
      if (date)
        SVN_ERR(svn_time_from_cstring(&timetemp, date->data, iterpool));

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Verify that on-disk representation has not been tempered with (in a way
 * that leaves the repository in a corrupted state).  This compares log-to-
 * phys with phys-to-log indexes, verifies the low-level checksums and
 * checks that all revprops are available.  The function signature is
 * similar to svn_fs_x__verify.
 *
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_metadata_consistency(svn_fs_t *fs,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_fs_progress_notify_func_t notify_func,
                            void *notify_baton,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_revnum_t revision, next_revision;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  for (revision = start; revision <= end; revision = next_revision)
    {
      svn_revnum_t count = svn_fs_x__packed_base_rev(fs, revision);
      svn_revnum_t pack_start = count;
      svn_revnum_t pack_end = pack_start + svn_fs_x__pack_size(fs, revision);

      svn_pool_clear(iterpool);

      if (notify_func && (pack_start % ffd->max_files_per_dir == 0))
        notify_func(pack_start, notify_baton, iterpool);

      /* Check for external corruption to the indexes. */
      err = verify_index_checksums(fs, pack_start, cancel_func,
                                   cancel_baton, iterpool);

      /* two-way index check */
      if (!err)
        err = compare_l2p_to_p2l_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool);
      if (!err)
        err = compare_p2l_to_l2p_index(fs, pack_start, pack_end - pack_start,
                                       cancel_func, cancel_baton, iterpool);

      /* verify in-index checksums and types vs. actual rev / pack files */
      if (!err)
        err = compare_p2l_to_rev(fs, pack_start, pack_end - pack_start,
                                 cancel_func, cancel_baton, iterpool);

      /* ensure that revprops are available and accessible */
      if (!err)
        err = verify_revprops(fs, pack_start, pack_end,
                              cancel_func, cancel_baton, iterpool);

      /* concurrent packing is one of the reasons why verification may fail.
         Make sure, we operate on up-to-date information. */
      if (err)
        {
          svn_error_t *err2
            = svn_fs_x__read_min_unpacked_rev(&ffd->min_unpacked_rev,
                                              fs, scratch_pool);

          /* Be careful to not leak ERR. */
          if (err2)
            return svn_error_trace(svn_error_compose_create(err, err2));
        }

      /* retry the whole shard if it got packed in the meantime */
      if (err && count != svn_fs_x__pack_size(fs, revision))
        {
          svn_error_clear(err);

          /* We could simply assign revision here but the code below is
             more intuitive to maintainers. */
          next_revision = svn_fs_x__packed_base_rev(fs, revision);
        }
      else
        {
          SVN_ERR(err);
          next_revision = pack_end;
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__verify(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  /* Input validation. */
  if (! SVN_IS_VALID_REVNUM(start))
    start = 0;
  if (! SVN_IS_VALID_REVNUM(end))
    {
      SVN_ERR(svn_fs_x__youngest_rev(&end, fs, scratch_pool));
    }

  SVN_ERR(svn_fs_x__ensure_revision_exists(start, fs, scratch_pool));
  SVN_ERR(svn_fs_x__ensure_revision_exists(end, fs, scratch_pool));

  /* log/phys index consistency.  We need to check them first to make
     sure we can access the rev / pack files in format7. */
  SVN_ERR(verify_metadata_consistency(fs, start, end,
                                      notify_func, notify_baton,
                                      cancel_func, cancel_baton,
                                      scratch_pool));

  /* rep cache consistency */
  SVN_ERR(verify_rep_cache(fs, start, end, notify_func, notify_baton,
                            cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}
