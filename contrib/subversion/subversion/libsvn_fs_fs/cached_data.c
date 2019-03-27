/* cached_data.c --- cached (read) access to FSFS data
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

#include "cached_data.h"

#include <assert.h>

#include "svn_hash.h"
#include "svn_ctype.h"
#include "svn_sorts.h"
#include "private/svn_delta_private.h"
#include "private/svn_io_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "fs_fs.h"
#include "id.h"
#include "index.h"
#include "low_level.h"
#include "pack.h"
#include "util.h"
#include "temp_serializer.h"

#include "../libsvn_fs/fs-loader.h"
#include "../libsvn_delta/delta.h"  /* for SVN_DELTA_WINDOW_SIZE */

#include "svn_private_config.h"

/* forward-declare. See implementation for the docstring */
static svn_error_t *
block_read(void **result,
           svn_fs_t *fs,
           svn_revnum_t revision,
           apr_uint64_t item_index,
           svn_fs_fs__revision_file_t *revision_file,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool);


/* Define this to enable access logging via dbg_log_access
#define SVN_FS_FS__LOG_ACCESS
 */

/* When SVN_FS_FS__LOG_ACCESS has been defined, write a line to console
 * showing where REVISION, ITEM_INDEX is located in FS and use ITEM to
 * show details on it's contents if not NULL.  To support format 6 and
 * earlier repos, ITEM_TYPE (SVN_FS_FS__ITEM_TYPE_*) must match ITEM.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * For pre-format7 repos, the display will be restricted.
 */
static svn_error_t *
dbg_log_access(svn_fs_t *fs,
               svn_revnum_t revision,
               apr_uint64_t item_index,
               void *item,
               apr_uint32_t item_type,
               apr_pool_t *scratch_pool)
{
  /* no-op if this macro is not defined */
#ifdef SVN_FS_FS__LOG_ACCESS
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_off_t end_offset = 0;
  svn_fs_fs__p2l_entry_t *entry = NULL;
  static const char *types[] = {"<n/a>", "frep ", "drep ", "fprop", "dprop",
                                "node ", "chgs ", "rep  "};
  const char *description = "";
  const char *type = types[item_type];
  const char *pack = "";
  apr_off_t offset;
  svn_fs_fs__revision_file_t *rev_file;

  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, revision,
                                           scratch_pool, scratch_pool));

  /* determine rev / pack file offset */
  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rev_file, revision, NULL,
                                 item_index, scratch_pool));

  /* constructing the pack file description */
  if (revision < ffd->min_unpacked_rev)
    pack = apr_psprintf(scratch_pool, "%4ld|",
                        revision / ffd->max_files_per_dir);

  /* construct description if possible */
  if (item_type == SVN_FS_FS__ITEM_TYPE_NODEREV && item != NULL)
    {
      node_revision_t *node = item;
      const char *data_rep
        = node->data_rep
        ? apr_psprintf(scratch_pool, " d=%ld/%" APR_UINT64_T_FMT,
                       node->data_rep->revision,
                       node->data_rep->item_index)
        : "";
      const char *prop_rep
        = node->prop_rep
        ? apr_psprintf(scratch_pool, " p=%ld/%" APR_UINT64_T_FMT,
                       node->prop_rep->revision,
                       node->prop_rep->item_index)
        : "";
      description = apr_psprintf(scratch_pool, "%s   (pc=%d%s%s)",
                                 node->created_path,
                                 node->predecessor_count,
                                 data_rep,
                                 prop_rep);
    }
  else if (item_type == SVN_FS_FS__ITEM_TYPE_ANY_REP)
    {
      svn_fs_fs__rep_header_t *header = item;
      if (header == NULL)
        description = "  (txdelta window)";
      else if (header->type == svn_fs_fs__rep_plain)
        description = "  PLAIN";
      else if (header->type == svn_fs_fs__rep_self_delta)
        description = "  DELTA";
      else
        description = apr_psprintf(scratch_pool,
                                   "  DELTA against %ld/%" APR_UINT64_T_FMT,
                                   header->base_revision,
                                   header->base_item_index);
    }
  else if (item_type == SVN_FS_FS__ITEM_TYPE_CHANGES && item != NULL)
    {
      apr_array_header_t *changes = item;
      switch (changes->nelts)
        {
          case 0:  description = "  no change";
                   break;
          case 1:  description = "  1 change";
                   break;
          default: description = apr_psprintf(scratch_pool, "  %d changes",
                                              changes->nelts);
        }
    }

  /* some info is only available in format7 repos */
  if (svn_fs_fs__use_log_addressing(fs))
    {
      /* reverse index lookup: get item description in ENTRY */
      SVN_ERR(svn_fs_fs__p2l_entry_lookup(&entry, fs, rev_file, revision,
                                          offset, scratch_pool,
                                          scratch_pool));
      if (entry)
        {
          /* more details */
          end_offset = offset + entry->size;
          type = types[entry->type];
        }

      /* line output */
      printf("%5s%4lx:%04lx -%4lx:%04lx %s %7ld %5"APR_UINT64_T_FMT"   %s\n",
             pack, (long)(offset / ffd->block_size),
             (long)(offset % ffd->block_size),
             (long)(end_offset / ffd->block_size),
             (long)(end_offset % ffd->block_size),
             type, revision, item_index, description);
    }
  else
    {
      /* reduced logging for format 6 and earlier */
      printf("%5s%10" APR_UINT64_T_HEX_FMT " %s %7ld %7" APR_UINT64_T_FMT \
             "   %s\n",
             pack, (apr_uint64_t)(offset), type, revision, item_index,
             description);
    }

  /* We don't know when SCRATCH_POOL will be cleared, so close the rev file
     explicitly. */
  SVN_ERR(svn_fs_fs__close_revision_file(rev_file));

#endif

  return SVN_NO_ERROR;
}

/* Convenience wrapper around svn_io_file_aligned_seek, taking filesystem
   FS instead of a block size. */
static svn_error_t *
aligned_seek(svn_fs_t *fs,
             apr_file_t *file,
             apr_off_t *buffer_start,
             apr_off_t offset,
             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return svn_error_trace(svn_io_file_aligned_seek(file, ffd->block_size,
                                                  buffer_start, offset,
                                                  pool));
}

/* Open the revision file for revision REV in filesystem FS and store
   the newly opened file in FILE.  Seek to location OFFSET before
   returning.  Perform temporary allocations in POOL. */
static svn_error_t *
open_and_seek_revision(svn_fs_fs__revision_file_t **file,
                       svn_fs_t *fs,
                       svn_revnum_t rev,
                       apr_uint64_t item,
                       apr_pool_t *pool)
{
  svn_fs_fs__revision_file_t *rev_file;
  apr_off_t offset = -1;

  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, pool));

  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, rev, pool, pool));
  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rev_file, rev, NULL, item,
                                 pool));

  SVN_ERR(aligned_seek(fs, rev_file->file, NULL, offset, pool));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Open the representation REP for a node-revision in filesystem FS, seek
   to its position and store the newly opened file in FILE.  Perform
   temporary allocations in POOL. */
static svn_error_t *
open_and_seek_transaction(svn_fs_fs__revision_file_t **file,
                          svn_fs_t *fs,
                          representation_t *rep,
                          apr_pool_t *pool)
{
  apr_off_t offset;

  SVN_ERR(svn_fs_fs__open_proto_rev_file(file, fs, &rep->txn_id, pool, pool));

  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, NULL, SVN_INVALID_REVNUM,
                                 &rep->txn_id, rep->item_index, pool));
  SVN_ERR(aligned_seek(fs, (*file)->file, NULL, offset, pool));

  return SVN_NO_ERROR;
}

/* Given a node-id ID, and a representation REP in filesystem FS, open
   the correct file and seek to the correction location.  Store this
   file in *FILE_P.  Perform any allocations in POOL. */
static svn_error_t *
open_and_seek_representation(svn_fs_fs__revision_file_t **file_p,
                             svn_fs_t *fs,
                             representation_t *rep,
                             apr_pool_t *pool)
{
  if (! svn_fs_fs__id_txn_used(&rep->txn_id))
    return open_and_seek_revision(file_p, fs, rep->revision, rep->item_index,
                                  pool);
  else
    return open_and_seek_transaction(file_p, fs, rep, pool);
}



static svn_error_t *
err_dangling_id(svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *id_str = svn_fs_fs__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}

/* Return TRUE, if FS is of a format that supports block-read and the
   feature has been enabled. */
static svn_boolean_t
use_block_read(svn_fs_t *fs)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  return svn_fs_fs__use_log_addressing(fs) && ffd->use_block_read;
}

svn_error_t *
svn_fs_fs__fixup_expanded_size(svn_fs_t *fs,
                               representation_t *rep,
                               apr_pool_t *scratch_pool)
{
  svn_checksum_t checksum;
  svn_checksum_t *empty_md5;
  svn_fs_fs__revision_file_t *revision_file;
  svn_fs_fs__rep_header_t *rep_header;

  /* Anything to do at all?
   *
   * Note that a 0 SIZE is only possible for PLAIN reps due to the SVN\1
   * magic prefix in any DELTA rep. */
  if (!rep || rep->expanded_size != 0 || rep->size == 0)
    return SVN_NO_ERROR;

  /* This function may only be called for committed data. */
  assert(!svn_fs_fs__id_txn_used(&rep->txn_id));

  /* EXPANDED_SIZE is 0. If the MD5 does not match the one for empty
   * contents, we know that EXPANDED_SIZE == 0 is wrong and needs to
   * be set to the actual value given by SIZE.
   *
   * Using svn_checksum_match() will also accept all-zero values for
   * the MD5 digest and only report a mismatch if the MD5 has actually
   * been given. */
  empty_md5 = svn_checksum_empty_checksum(svn_checksum_md5, scratch_pool);

  checksum.digest = rep->md5_digest;
  checksum.kind = svn_checksum_md5;
  if (!svn_checksum_match(empty_md5, &checksum))
    {
      rep->expanded_size = rep->size;
      return SVN_NO_ERROR;
    }

  /* Data in the rep-cache.db does not have MD5 checksums (all zero) on it.
   * Compare SHA1 instead. */
  if (rep->has_sha1)
    {
      svn_checksum_t *empty_sha1
        = svn_checksum_empty_checksum(svn_checksum_sha1, scratch_pool);

      checksum.digest = rep->sha1_digest;
      checksum.kind = svn_checksum_sha1;
      if (!svn_checksum_match(empty_sha1, &checksum))
        {
          rep->expanded_size = rep->size;
          return SVN_NO_ERROR;
        }
    }

  /* Only two cases are left here.
   * (1) A non-empty PLAIN rep with a MD5 collision on EMPTY_MD5.
   * (2) A DELTA rep with zero-length output. */

  /* SVN always stores a DELTA rep with zero-length output as an empty
   * sequence of txdelta windows, i.e. as "SVN\1".  In that case, SIZE is
   * 4 bytes.  There is no other possible DELTA rep of that size and any
   * PLAIN rep of 4 bytes would produce a different MD5.  Hence, if SIZE is
   * actually 4 here, we know that this is an empty DELTA rep.
   *
   * Note that it is technically legal to have DELTA reps with a 0 length
   * output window.  Their on-disk size would be longer.  We handle that
   * case later together with the equally unlikely MD5 collision. */
  if (rep->size == 4)
    {
      /* EXPANDED_SIZE is already 0. */
      return SVN_NO_ERROR;
    }

  /* We still have the two options, PLAIN or DELTA rep.  At this point, we
   * are in an extremely unlikely case and can spend some time to figure it
   * out.  So, let's just look at the representation header. */
  SVN_ERR(open_and_seek_revision(&revision_file, fs, rep->revision,
                                 rep->item_index, scratch_pool));
  SVN_ERR(svn_fs_fs__read_rep_header(&rep_header, revision_file->stream,
                                     scratch_pool, scratch_pool));
  SVN_ERR(svn_fs_fs__close_revision_file(revision_file));

  /* Only for PLAIN reps do we have to correct EXPANDED_SIZE. */
  if (rep_header->type == svn_fs_fs__rep_plain)
    rep->expanded_size = rep->size;

  return SVN_NO_ERROR;
}

/* Correct known issues with committed NODEREV in FS.
 * Uses SCRATCH_POOL for temporaries.
 */
static svn_error_t *
fixup_node_revision(svn_fs_t *fs,
                    node_revision_t *noderev,
                    apr_pool_t *scratch_pool)
{
  /* Workaround issue #4031: is-fresh-txn-root in revision files. */
  noderev->is_fresh_txn_root = FALSE;

  /* Make sure EXPANDED_SIZE has the correct value for every rep. */
  SVN_ERR(svn_fs_fs__fixup_expanded_size(fs, noderev->data_rep,
                                         scratch_pool));
  SVN_ERR(svn_fs_fs__fixup_expanded_size(fs, noderev->prop_rep,
                                         scratch_pool));

  return SVN_NO_ERROR;
}

/* Get the node-revision for the node ID in FS.
   Set *NODEREV_P to the new node-revision structure, allocated in POOL.
   See svn_fs_fs__get_node_revision, which wraps this and adds another
   error. */
static svn_error_t *
get_node_revision_body(node_revision_t **noderev_p,
                       svn_fs_t *fs,
                       const svn_fs_id_t *id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t is_cached = FALSE;
  fs_fs_data_t *ffd = fs->fsap_data;

  if (svn_fs_fs__id_is_txn(id))
    {
      apr_file_t *file;

      /* This is a transaction node-rev.  Its storage logic is very
         different from that of rev / pack files. */
      err = svn_io_file_open(&file,
                             svn_fs_fs__path_txn_node_rev(fs, id,
                             scratch_pool),
                             APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                             scratch_pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          svn_error_clear(err);
          return svn_error_trace(err_dangling_id(fs, id));
        }
      else if (err)
        {
          return svn_error_trace(err);
        }

      SVN_ERR(svn_fs_fs__read_noderev(noderev_p,
                                      svn_stream_from_aprfile2(file,
                                                               FALSE,
                                                               scratch_pool),
                                      result_pool, scratch_pool));
    }
  else
    {
      svn_fs_fs__revision_file_t *revision_file;

      /* noderevs in rev / pack files can be cached */
      const svn_fs_fs__id_part_t *rev_item = svn_fs_fs__id_rev_item(id);
      pair_cache_key_t key = { 0 };
      key.revision = rev_item->revision;
      key.second = rev_item->number;

      /* Not found or not applicable. Try a noderev cache lookup.
       * If that succeeds, we are done here. */
      if (ffd->node_revision_cache)
        {
          SVN_ERR(svn_cache__get((void **) noderev_p,
                                 &is_cached,
                                 ffd->node_revision_cache,
                                 &key,
                                 result_pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }

      /* read the data from disk */
      SVN_ERR(open_and_seek_revision(&revision_file, fs,
                                     rev_item->revision,
                                     rev_item->number,
                                     scratch_pool));

      if (use_block_read(fs))
        {
          /* block-read will parse the whole block and will also return
             the one noderev that we need right now. */
          SVN_ERR(block_read((void **)noderev_p, fs,
                             rev_item->revision,
                             rev_item->number,
                             revision_file,
                             result_pool,
                             scratch_pool));
        }
      else
        {
          /* physical addressing mode reading, parsing and caching */
          SVN_ERR(svn_fs_fs__read_noderev(noderev_p,
                                          revision_file->stream,
                                          result_pool,
                                          scratch_pool));
          SVN_ERR(fixup_node_revision(fs, *noderev_p, scratch_pool));

          /* The noderev is not in cache, yet. Add it, if caching has been enabled. */
          if (ffd->node_revision_cache)
            SVN_ERR(svn_cache__set(ffd->node_revision_cache,
                                   &key,
                                   *noderev_p,
                                   scratch_pool));
        }

      SVN_ERR(svn_fs_fs__close_revision_file(revision_file));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_node_revision(node_revision_t **noderev_p,
                             svn_fs_t *fs,
                             const svn_fs_id_t *id,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const svn_fs_fs__id_part_t *rev_item = svn_fs_fs__id_rev_item(id);

  svn_error_t *err = get_node_revision_body(noderev_p, fs, id,
                                            result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      svn_string_t *id_string = svn_fs_fs__id_unparse(id, scratch_pool);
      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt node-revision '%s'",
                               id_string->data);
    }

  SVN_ERR(dbg_log_access(fs,
                         rev_item->revision,
                         rev_item->number,
                         *noderev_p,
                         SVN_FS_FS__ITEM_TYPE_NODEREV,
                         scratch_pool));

  return svn_error_trace(err);
}


/* Given a revision file REV_FILE, opened to REV in FS, find the Node-ID
   of the header located at OFFSET and store it in *ID_P.  Allocate
   temporary variables from POOL. */
static svn_error_t *
get_fs_id_at_offset(svn_fs_id_t **id_p,
                    svn_fs_fs__revision_file_t *rev_file,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_off_t offset,
                    apr_pool_t *pool)
{
  node_revision_t *noderev;

  SVN_ERR(aligned_seek(fs, rev_file->file, NULL, offset, pool));
  SVN_ERR(svn_fs_fs__read_noderev(&noderev,
                                  rev_file->stream,
                                  pool, pool));

  /* noderev->id is const, get rid of that */
  *id_p = svn_fs_fs__id_copy(noderev->id, pool);

  /* assert that the txn_id is REV
   * (asserting on offset would be harder because we the rev_offset is not
   * known here) */
  assert(svn_fs_fs__id_rev(*id_p) == rev);

  return SVN_NO_ERROR;
}


/* Given an open revision file REV_FILE in FS for REV, locate the trailer that
   specifies the offset to the root node-id and to the changed path
   information.  Store the root node offset in *ROOT_OFFSET and the
   changed path offset in *CHANGES_OFFSET.  If either of these
   pointers is NULL, do nothing with it.

   Allocate temporary variables from POOL. */
static svn_error_t *
get_root_changes_offset(apr_off_t *root_offset,
                        apr_off_t *changes_offset,
                        svn_fs_fs__revision_file_t *rev_file,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_off_t rev_offset;
  apr_seek_where_t seek_relative;
  svn_stringbuf_t *trailer;
  char buffer[64];
  apr_off_t start;
  apr_off_t end;
  apr_size_t len;

  /* Determine where to seek to in the file.

     If we've got a pack file, we want to seek to the end of the desired
     revision.  But we don't track that, so we seek to the beginning of the
     next revision.

     Unless the next revision is in a different file, in which case, we can
     just seek to the end of the pack file -- just like we do in the
     non-packed case. */
  if (rev_file->is_packed && ((rev + 1) % ffd->max_files_per_dir != 0))
    {
      SVN_ERR(svn_fs_fs__get_packed_offset(&end, fs, rev + 1, pool));
      seek_relative = APR_SET;
    }
  else
    {
      seek_relative = APR_END;
      end = 0;
    }

  /* Offset of the revision from the start of the pack file, if applicable. */
  if (rev_file->is_packed)
    SVN_ERR(svn_fs_fs__get_packed_offset(&rev_offset, fs, rev, pool));
  else
    rev_offset = 0;

  /* We will assume that the last line containing the two offsets
     will never be longer than 64 characters. */
  SVN_ERR(svn_io_file_seek(rev_file->file, seek_relative, &end, pool));

  if (end < sizeof(buffer))
    {
      len = (apr_size_t)end;
      start = 0;
    }
  else
    {
      len = sizeof(buffer);
      start = end - sizeof(buffer);
    }

  /* Read in this last block, from which we will identify the last line. */
  SVN_ERR(aligned_seek(fs, rev_file->file, NULL, start, pool));
  SVN_ERR(svn_io_file_read_full2(rev_file->file, buffer, len, NULL, NULL,
                                 pool));

  /* Parse the last line. */
  trailer = svn_stringbuf_ncreate(buffer, len, pool);
  SVN_ERR(svn_fs_fs__parse_revision_trailer(root_offset,
                                            changes_offset,
                                            trailer,
                                            rev));

  /* return absolute offsets */
  if (root_offset)
    *root_offset += rev_offset;
  if (changes_offset)
    *changes_offset += rev_offset;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rev_get_root(svn_fs_id_t **root_id_p,
                        svn_fs_t *fs,
                        svn_revnum_t rev,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  SVN_ERR(svn_fs_fs__ensure_revision_exists(rev, fs, scratch_pool));

  if (svn_fs_fs__use_log_addressing(fs))
    {
      *root_id_p = svn_fs_fs__id_create_root(rev, result_pool);
    }
  else
    {
      svn_fs_fs__revision_file_t *revision_file;
      apr_off_t root_offset;
      svn_fs_id_t *root_id = NULL;
      svn_boolean_t is_cached;

      SVN_ERR(svn_cache__get((void **) root_id_p, &is_cached,
                            ffd->rev_root_id_cache, &rev, result_pool));
      if (is_cached)
        return SVN_NO_ERROR;

      SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&revision_file, fs, rev,
                                               scratch_pool, scratch_pool));
      SVN_ERR(get_root_changes_offset(&root_offset, NULL,
                                      revision_file, fs, rev,
                                      scratch_pool));

      SVN_ERR(get_fs_id_at_offset(&root_id, revision_file, fs, rev,
                                  root_offset, result_pool));

      SVN_ERR(svn_fs_fs__close_revision_file(revision_file));

      SVN_ERR(svn_cache__set(ffd->rev_root_id_cache, &rev, root_id,
                             scratch_pool));

      *root_id_p = root_id;
    }

  return SVN_NO_ERROR;
}

/* Describes a lazily opened rev / pack file.  Instances will be shared
   between multiple instances of rep_state_t. */
typedef struct shared_file_t
{
  /* The opened file. NULL while file is not open, yet. */
  svn_fs_fs__revision_file_t *rfile;

  /* file system to open the file in */
  svn_fs_t *fs;

  /* a revision contained in the FILE.  Since this file may be shared,
     that value may be different from REP_STATE_T->REVISION. */
  svn_revnum_t revision;

  /* pool to use when creating the FILE.  This guarantees that the file
     remains open / valid beyond the respective local context that required
     the file to be opened eventually. */
  apr_pool_t *pool;
} shared_file_t;

/* Represents where in the current svndiff data block each
   representation is. */
typedef struct rep_state_t
{
                    /* shared lazy-open rev/pack file structure */
  shared_file_t *sfile;
                    /* The txdelta window cache to use or NULL. */
  svn_cache__t *raw_window_cache;
                    /* Caches raw (unparsed) windows. May be NULL. */
  svn_cache__t *window_cache;
                    /* Caches un-deltified windows. May be NULL. */
  svn_cache__t *combined_cache;
                    /* revision containing the representation */
  svn_revnum_t revision;
                    /* representation's item index in REVISION */
  apr_uint64_t item_index;
                    /* length of the header at the start of the rep.
                       0 iff this is rep is stored in a container
                       (i.e. does not have a header) */
  apr_size_t header_size;
  apr_off_t start;  /* The starting offset for the raw
                       svndiff/plaintext data minus header.
                       -1 if the offset is yet unknown. */
  apr_off_t current;/* The current offset relative to START. */
  apr_off_t size;   /* The on-disk size of the representation. */
  int ver;          /* If a delta, what svndiff version?
                       -1 for unknown delta version. */
  int chunk_index;  /* number of the window to read */
} rep_state_t;

/* Simple wrapper around svn_io_file_get_offset to simplify callers. */
static svn_error_t *
get_file_offset(apr_off_t *offset,
                rep_state_t *rs,
                apr_pool_t *pool)
{
  return svn_error_trace(svn_io_file_get_offset(offset,
                                                rs->sfile->rfile->file,
                                                pool));
}

/* Simple wrapper around svn_io_file_aligned_seek to simplify callers. */
static svn_error_t *
rs_aligned_seek(rep_state_t *rs,
                apr_off_t *buffer_start,
                apr_off_t offset,
                apr_pool_t *pool)
{
  fs_fs_data_t *ffd = rs->sfile->fs->fsap_data;
  return svn_error_trace(svn_io_file_aligned_seek(rs->sfile->rfile->file,
                                                  ffd->block_size,
                                                  buffer_start, offset,
                                                  pool));
}

/* Open FILE->FILE and FILE->STREAM if they haven't been opened, yet. */
static svn_error_t*
auto_open_shared_file(shared_file_t *file)
{
  if (file->rfile == NULL)
    SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&file->rfile, file->fs,
                                             file->revision, file->pool,
                                             file->pool));

  return SVN_NO_ERROR;
}

/* Set RS->START to the begin of the representation raw in RS->FILE->FILE,
   if that hasn't been done yet.  Use POOL for temporary allocations. */
static svn_error_t*
auto_set_start_offset(rep_state_t *rs, apr_pool_t *pool)
{
  if (rs->start == -1)
    {
      SVN_ERR(svn_fs_fs__item_offset(&rs->start, rs->sfile->fs,
                                     rs->sfile->rfile, rs->revision, NULL,
                                     rs->item_index, pool));
      rs->start += rs->header_size;
    }

  return SVN_NO_ERROR;
}

/* Set RS->VER depending on what is found in the already open RS->FILE->FILE
   if the diff version is still unknown.  Use POOL for temporary allocations.
 */
static svn_error_t*
auto_read_diff_version(rep_state_t *rs, apr_pool_t *pool)
{
  if (rs->ver == -1)
    {
      char buf[4];
      SVN_ERR(rs_aligned_seek(rs, NULL, rs->start, pool));
      SVN_ERR(svn_io_file_read_full2(rs->sfile->rfile->file, buf,
                                     sizeof(buf), NULL, NULL, pool));

      /* ### Layering violation */
      if (! ((buf[0] == 'S') && (buf[1] == 'V') && (buf[2] == 'N')))
        return svn_error_create
          (SVN_ERR_FS_CORRUPT, NULL,
           _("Malformed svndiff data in representation"));
      rs->ver = buf[3];

      rs->chunk_index = 0;
      rs->current = 4;
    }

  return SVN_NO_ERROR;
}

/* See create_rep_state, which wraps this and adds another error. */
static svn_error_t *
create_rep_state_body(rep_state_t **rep_state,
                      svn_fs_fs__rep_header_t **rep_header,
                      shared_file_t **shared_file,
                      representation_t *rep,
                      svn_fs_t *fs,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  rep_state_t *rs = apr_pcalloc(result_pool, sizeof(*rs));
  svn_fs_fs__rep_header_t *rh;
  svn_boolean_t is_cached = FALSE;
  apr_uint64_t estimated_window_storage;

  /* If the hint is
   * - given,
   * - refers to a valid revision,
   * - refers to a packed revision,
   * - as does the rep we want to read, and
   * - refers to the same pack file as the rep
   * we can re-use the same, already open file object
   */
  svn_boolean_t reuse_shared_file
    =    shared_file && *shared_file && (*shared_file)->rfile
      && SVN_IS_VALID_REVNUM((*shared_file)->revision)
      && (*shared_file)->revision < ffd->min_unpacked_rev
      && rep->revision < ffd->min_unpacked_rev
      && (   ((*shared_file)->revision / ffd->max_files_per_dir)
          == (rep->revision / ffd->max_files_per_dir));

  pair_cache_key_t key;
  key.revision = rep->revision;
  key.second = rep->item_index;

  /* continue constructing RS and RA */
  rs->size = rep->size;
  rs->revision = rep->revision;
  rs->item_index = rep->item_index;
  rs->raw_window_cache = use_block_read(fs) ? ffd->raw_window_cache : NULL;
  rs->ver = -1;
  rs->start = -1;

  /* Very long files stored as self-delta will produce a huge number of
     delta windows.  Don't cache them lest we don't thrash the cache.
     Since we don't know the depth of the delta chain, let's assume, the
     whole contents get rewritten 3 times.
   */
  estimated_window_storage = 4 * (rep->expanded_size + SVN_DELTA_WINDOW_SIZE);
  estimated_window_storage = MIN(estimated_window_storage, APR_SIZE_MAX);

  rs->window_cache =    ffd->txdelta_window_cache
                     && svn_cache__is_cachable(ffd->txdelta_window_cache,
                                       (apr_size_t)estimated_window_storage)
                   ? ffd->txdelta_window_cache
                   : NULL;
  rs->combined_cache =    ffd->combined_window_cache
                       && svn_cache__is_cachable(ffd->combined_window_cache,
                                       (apr_size_t)estimated_window_storage)
                     ? ffd->combined_window_cache
                     : NULL;

  /* cache lookup, i.e. skip reading the rep header if possible */
  if (ffd->rep_header_cache && !svn_fs_fs__id_txn_used(&rep->txn_id))
    SVN_ERR(svn_cache__get((void **) &rh, &is_cached,
                           ffd->rep_header_cache, &key, result_pool));

  /* initialize the (shared) FILE member in RS */
  if (reuse_shared_file)
    {
      rs->sfile = *shared_file;
    }
  else
    {
      shared_file_t *file = apr_pcalloc(result_pool, sizeof(*file));
      file->revision = rep->revision;
      file->pool = result_pool;
      file->fs = fs;
      rs->sfile = file;

      /* remember the current file, if suggested by the caller */
      if (shared_file)
        *shared_file = file;
    }

  /* read rep header, if necessary */
  if (!is_cached)
    {
      /* ensure file is open and navigate to the start of rep header */
      if (reuse_shared_file)
        {
          apr_off_t offset;

          /* ... we can re-use the same, already open file object.
           * This implies that we don't read from a txn.
           */
          rs->sfile = *shared_file;
          SVN_ERR(auto_open_shared_file(rs->sfile));
          SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rs->sfile->rfile,
                                         rep->revision, NULL, rep->item_index,
                                         scratch_pool));
          SVN_ERR(rs_aligned_seek(rs, NULL, offset, scratch_pool));
        }
      else
        {
          /* otherwise, create a new file object.  May or may not be
           * an in-txn file.
           */
          SVN_ERR(open_and_seek_representation(&rs->sfile->rfile, fs, rep,
                                               result_pool));
        }

      SVN_ERR(svn_fs_fs__read_rep_header(&rh, rs->sfile->rfile->stream,
                                         result_pool, scratch_pool));
      SVN_ERR(get_file_offset(&rs->start, rs, result_pool));

      /* populate the cache if appropriate */
      if (! svn_fs_fs__id_txn_used(&rep->txn_id))
        {
          if (use_block_read(fs))
            SVN_ERR(block_read(NULL, fs, rep->revision, rep->item_index,
                               rs->sfile->rfile, result_pool, scratch_pool));
          else
            if (ffd->rep_header_cache)
              SVN_ERR(svn_cache__set(ffd->rep_header_cache, &key, rh,
                                     scratch_pool));
        }
    }

  /* finalize */
  SVN_ERR(dbg_log_access(fs, rep->revision, rep->item_index, rh,
                         SVN_FS_FS__ITEM_TYPE_ANY_REP, scratch_pool));

  rs->header_size = rh->header_size;
  *rep_state = rs;
  *rep_header = rh;

  if (rh->type == svn_fs_fs__rep_plain)
    /* This is a plaintext, so just return the current rep_state. */
    return SVN_NO_ERROR;

  /* skip "SVNx" diff marker */
  rs->current = 4;

  return SVN_NO_ERROR;
}

/* Read the rep args for REP in filesystem FS and create a rep_state
   for reading the representation.  Return the rep_state in *REP_STATE
   and the rep header in *REP_HEADER, both allocated in POOL.

   When reading multiple reps, i.e. a skip delta chain, you may provide
   non-NULL SHARED_FILE.  (If SHARED_FILE is not NULL, in the first
   call it should be a pointer to NULL.)  The function will use this
   variable to store the previous call results and tries to re-use it.
   This may result in significant savings in I/O for packed files and
   number of open file handles.
 */
static svn_error_t *
create_rep_state(rep_state_t **rep_state,
                 svn_fs_fs__rep_header_t **rep_header,
                 shared_file_t **shared_file,
                 representation_t *rep,
                 svn_fs_t *fs,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err = create_rep_state_body(rep_state, rep_header,
                                           shared_file, rep, fs,
                                           result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      const char *rep_str;

      /* ### This always returns "-1" for transaction reps, because
         ### this particular bit of code doesn't know if the rep is
         ### stored in the protorev or in the mutable area (for props
         ### or dir contents).  It is pretty rare for FSFS to *read*
         ### from the protorev file, though, so this is probably OK.
         ### And anyone going to debug corruption errors is probably
         ### going to jump straight to this comment anyway! */
      rep_str = rep
              ? svn_fs_fs__unparse_representation
                  (rep, ffd->format, TRUE, scratch_pool, scratch_pool)->data
              : "(null)";

      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt representation '%s'",
                               rep_str);
    }
  /* ### Call representation_string() ? */
  return svn_error_trace(err);
}

svn_error_t *
svn_fs_fs__check_rep(representation_t *rep,
                     svn_fs_t *fs,
                     void **hint,
                     apr_pool_t *scratch_pool)
{
  if (svn_fs_fs__use_log_addressing(fs))
    {
      apr_off_t offset;
      svn_fs_fs__p2l_entry_t *entry;
      svn_fs_fs__revision_file_t *rev_file = NULL;

      /* Reuse the revision file provided by *HINT, if it is given and
       * actually the rev / pack file that we want. */
      svn_revnum_t start_rev = svn_fs_fs__packed_base_rev(fs, rep->revision);
      if (hint)
        rev_file = *(svn_fs_fs__revision_file_t **)hint;

      if (rev_file == NULL || rev_file->start_revision != start_rev)
        SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, rep->revision,
                                                 scratch_pool, scratch_pool));

      if (hint)
        *hint = rev_file;

      /* This will auto-retry if there was a background pack. */
      SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rev_file, rep->revision,
                                     NULL, rep->item_index, scratch_pool));

      /* This may fail if there is a background pack operation (can't auto-
         retry because the item offset lookup has to be redone as well). */
      SVN_ERR(svn_fs_fs__p2l_entry_lookup(&entry, fs, rev_file,
                                          rep->revision, offset,
                                          scratch_pool, scratch_pool));

      if (   entry == NULL
          || entry->type < SVN_FS_FS__ITEM_TYPE_FILE_REP
          || entry->type > SVN_FS_FS__ITEM_TYPE_DIR_PROPS)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("No representation found at offset %s "
                                   "for item %s in revision %ld"),
                                 apr_off_t_toa(scratch_pool, offset),
                                 apr_psprintf(scratch_pool,
                                              "%" APR_UINT64_T_FMT,
                                              rep->item_index),
                                 rep->revision);
    }
  else
    {
      rep_state_t *rs;
      svn_fs_fs__rep_header_t *rep_header;

      /* ### Should this be using read_rep_line() directly? */
      SVN_ERR(create_rep_state(&rs, &rep_header, (shared_file_t**)hint,
                               rep, fs, scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__rep_chain_length(int *chain_length,
                            int *shard_count,
                            representation_t *rep,
                            svn_fs_t *fs,
                            apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t shard_size = ffd->max_files_per_dir
                          ? ffd->max_files_per_dir
                          : 1;
  apr_pool_t *subpool = svn_pool_create(scratch_pool);
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t is_delta = FALSE;
  int count = 0;
  int shards = 1;
  svn_revnum_t last_shard = rep->revision / shard_size;

  /* Check whether the length of the deltification chain is acceptable.
   * Otherwise, shared reps may form a non-skipping delta chain in
   * extreme cases. */
  representation_t base_rep = *rep;

  /* re-use open files between iterations */
  shared_file_t *file_hint = NULL;

  svn_fs_fs__rep_header_t *header;

  /* follow the delta chain towards the end but for at most
   * MAX_CHAIN_LENGTH steps. */
  do
    {
      rep_state_t *rep_state;

      svn_pool_clear(iterpool);

      if (base_rep.revision / shard_size != last_shard)
        {
          last_shard = base_rep.revision / shard_size;
          ++shards;
        }

      SVN_ERR(create_rep_state_body(&rep_state,
                                    &header,
                                    &file_hint,
                                    &base_rep,
                                    fs,
                                    subpool,
                                    iterpool));

      base_rep.revision = header->base_revision;
      base_rep.item_index = header->base_item_index;
      base_rep.size = header->base_length;
      svn_fs_fs__id_txn_reset(&base_rep.txn_id);
      is_delta = header->type == svn_fs_fs__rep_delta;

      /* Clear it the SUBPOOL once in a while.  Doing it too frequently
       * renders the FILE_HINT ineffective.  Doing too infrequently, may
       * leave us with too many open file handles.
       *
       * Note that this is mostly about efficiency, with larger values
       * being more efficient, and any non-zero value is legal here.  When
       * reading deltified contents, we may keep 10s of rev files open at
       * the same time and the system has to cope with that.  Thus, the
       * limit of 16 chosen below is in the same ballpark.
       */
      ++count;
      if (count % 16 == 0)
        {
          file_hint = NULL;
          svn_pool_clear(subpool);
        }
    }
  while (is_delta && base_rep.revision);

  *chain_length = count;
  *shard_count = shards;
  svn_pool_destroy(subpool);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

struct rep_read_baton
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* Representation to read. */
  representation_t rep;

  /* If not NULL, this is the base for the first delta window in rs_list */
  svn_stringbuf_t *base_window;

  /* The state of all prior delta representations. */
  apr_array_header_t *rs_list;

  /* The plaintext state, if there is a plaintext. */
  rep_state_t *src_state;

  /* The index of the current delta chunk, if we are reading a delta. */
  int chunk_index;

  /* The buffer where we store undeltified data. */
  char *buf;
  apr_size_t buf_pos;
  apr_size_t buf_len;

  /* A checksum context for summing the data read in order to verify it.
     Note: we don't need to use the sha1 checksum because we're only doing
     data verification, for which md5 is perfectly safe.  */
  svn_checksum_ctx_t *md5_checksum_ctx;

  svn_boolean_t checksum_finalized;

  /* The stored checksum of the representation we are reading, its
     length, and the amount we've read so far.  Some of this
     information is redundant with rs_list and src_state, but it's
     convenient for the checksumming code to have it here. */
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];

  svn_filesize_t len;
  svn_filesize_t off;

  /* The key for the fulltext cache for this rep, if there is a
     fulltext cache. */
  pair_cache_key_t fulltext_cache_key;
  /* The text we've been reading, if we're going to cache it. */
  svn_stringbuf_t *current_fulltext;

  /* If not NULL, attempt to read the data from this cache.
     Once that lookup fails, reset it to NULL. */
  svn_cache__t *fulltext_cache;

  /* Bytes delivered from the FULLTEXT_CACHE so far.  If the next
     lookup fails, we need to skip that much data from the reconstructed
     window stream before we continue normal operation. */
  svn_filesize_t fulltext_delivered;

  /* Used for temporary allocations during the read. */
  apr_pool_t *pool;

  /* Pool used to store file handles and other data that is persistant
     for the entire stream read. */
  apr_pool_t *filehandle_pool;
};

/* Set window key in *KEY to address the window described by RS.
   For convenience, return the KEY. */
static window_cache_key_t *
get_window_key(window_cache_key_t *key, rep_state_t *rs)
{
  assert(rs->revision <= APR_UINT32_MAX);
  key->revision = (apr_uint32_t)rs->revision;
  key->item_index = rs->item_index;
  key->chunk_index = rs->chunk_index;

  return key;
}

/* Implement svn_cache__partial_getter_func_t for raw txdelta windows.
 * Parse the raw data and return a svn_fs_fs__txdelta_cached_window_t.
 */
static svn_error_t *
parse_raw_window(void **out,
                 const void *data,
                 apr_size_t data_len,
                 void *baton,
                 apr_pool_t *result_pool)
{
  svn_string_t raw_window;
  svn_stream_t *stream;

  /* unparsed and parsed window */
  const svn_fs_fs__raw_cached_window_t *window
    = (const svn_fs_fs__raw_cached_window_t *)data;
  svn_fs_fs__txdelta_cached_window_t *result
    = apr_pcalloc(result_pool, sizeof(*result));

  /* create a read stream taking the raw window as input */
  raw_window.data = svn_temp_deserializer__ptr(window,
                                (const void * const *)&window->window.data);
  raw_window.len = window->window.len;
  stream = svn_stream_from_string(&raw_window, result_pool);

  /* parse it */
  SVN_ERR(svn_txdelta_read_svndiff_window(&result->window, stream, window->ver,
                                          result_pool));

  /* complete the window and return it */
  result->end_offset = window->end_offset;
  *out = result;

  return SVN_NO_ERROR;
}


/* Read the WINDOW_P number CHUNK_INDEX for the representation given in
 * rep state RS from the current FSFS session's cache.  This will be a
 * no-op and IS_CACHED will be set to FALSE if no cache has been given.
 * If a cache is available IS_CACHED will inform the caller about the
 * success of the lookup. Allocations of the window in will be made
 * from RESULT_POOL. Use SCRATCH_POOL for temporary allocations.
 *
 * If the information could be found, put RS to CHUNK_INDEX.
 */
static svn_error_t *
get_cached_window(svn_txdelta_window_t **window_p,
                  rep_state_t *rs,
                  int chunk_index,
                  svn_boolean_t *is_cached,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  if (! rs->window_cache)
    {
      /* txdelta window has not been enabled */
      *is_cached = FALSE;
    }
  else
    {
      /* ask the cache for the desired txdelta window */
      svn_fs_fs__txdelta_cached_window_t *cached_window;
      window_cache_key_t key = { 0 };
      get_window_key(&key, rs);
      key.chunk_index = chunk_index;
      SVN_ERR(svn_cache__get((void **) &cached_window,
                             is_cached,
                             rs->window_cache,
                             &key,
                             result_pool));

      /* If we did not find a parsed txdelta window, we might have a raw
         version of it in our cache.  If so, read, parse and re-cache it. */
      if (!*is_cached && rs->raw_window_cache)
        {
          SVN_ERR(svn_cache__get_partial((void **) &cached_window, is_cached,
                                         rs->raw_window_cache, &key,
                                         parse_raw_window, NULL, result_pool));
          if (*is_cached)
            SVN_ERR(svn_cache__set(rs->window_cache, &key, cached_window,
                                   scratch_pool));
        }

      /* Return cached information. */
      if (*is_cached)
        {
          /* found it. Pass it back to the caller. */
          *window_p = cached_window->window;

          /* manipulate the RS as if we just read the data */
          rs->current = cached_window->end_offset;
          rs->chunk_index = chunk_index;
        }
    }

  return SVN_NO_ERROR;
}

/* Store the WINDOW read for the rep state RS in the current FSFS
 * session's cache.  This will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_window(svn_txdelta_window_t *window,
                  rep_state_t *rs,
                  apr_pool_t *scratch_pool)
{
  if (rs->window_cache)
    {
      /* store the window and the first offset _past_ it */
      svn_fs_fs__txdelta_cached_window_t cached_window;
      window_cache_key_t key = {0};

      cached_window.window = window;
      cached_window.end_offset = rs->current;

      /* but key it with the start offset because that is the known state
       * when we will look it up */
      SVN_ERR(svn_cache__set(rs->window_cache,
                             get_window_key(&key, rs),
                             &cached_window,
                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Read the WINDOW_P for the rep state RS from the current FSFS session's
 * cache. This will be a no-op and IS_CACHED will be set to FALSE if no
 * cache has been given. If a cache is available IS_CACHED will inform
 * the caller about the success of the lookup. Allocations (of the window
 * in particular) will be made from POOL.
 */
static svn_error_t *
get_cached_combined_window(svn_stringbuf_t **window_p,
                           rep_state_t *rs,
                           svn_boolean_t *is_cached,
                           apr_pool_t *pool)
{
  if (! rs->combined_cache)
    {
      /* txdelta window has not been enabled */
      *is_cached = FALSE;
    }
  else
    {
      /* ask the cache for the desired txdelta window */
      window_cache_key_t key = { 0 };
      return svn_cache__get((void **)window_p,
                            is_cached,
                            rs->combined_cache,
                            get_window_key(&key, rs),
                            pool);
    }

  return SVN_NO_ERROR;
}

/* Store the WINDOW read for the rep state RS in the current FSFS session's
 * cache. This will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_combined_window(svn_stringbuf_t *window,
                           rep_state_t *rs,
                           apr_pool_t *scratch_pool)
{
  if (rs->combined_cache)
    {
      /* but key it with the start offset because that is the known state
       * when we will look it up */
      window_cache_key_t key = { 0 };
      return svn_cache__set(rs->combined_cache,
                            get_window_key(&key, rs),
                            window,
                            scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Build an array of rep_state structures in *LIST giving the delta
   reps from first_rep to a plain-text or self-compressed rep.  Set
   *SRC_STATE to the plain-text rep we find at the end of the chain,
   or to NULL if the final delta representation is self-compressed.
   The representation to start from is designated by filesystem FS, id
   ID, and representation REP.
   Also, set *WINDOW_P to the base window content for *LIST, if it
   could be found in cache. Otherwise, *LIST will contain the base
   representation for the whole delta chain. */
static svn_error_t *
build_rep_list(apr_array_header_t **list,
               svn_stringbuf_t **window_p,
               rep_state_t **src_state,
               svn_fs_t *fs,
               representation_t *first_rep,
               apr_pool_t *pool)
{
  representation_t rep;
  rep_state_t *rs = NULL;
  svn_fs_fs__rep_header_t *rep_header;
  svn_boolean_t is_cached = FALSE;
  shared_file_t *shared_file = NULL;
  apr_pool_t *iterpool = svn_pool_create(pool);

  *list = apr_array_make(pool, 1, sizeof(rep_state_t *));
  rep = *first_rep;

  /* for the top-level rep, we need the rep_args */
  SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file, &rep, fs, pool,
                           iterpool));
  while (1)
    {
      svn_pool_clear(iterpool);

      /* fetch state, if that has not been done already */
      if (!rs)
        SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file,
                                 &rep, fs, pool, iterpool));

      /* for txn reps, there won't be a cached combined window */
      if (   !svn_fs_fs__id_txn_used(&rep.txn_id)
          && rep.expanded_size < SVN_DELTA_WINDOW_SIZE)
        SVN_ERR(get_cached_combined_window(window_p, rs, &is_cached, pool));

      if (is_cached)
        {
          /* We already have a reconstructed window in our cache.
             Write a pseudo rep_state with the full length. */
          rs->start = 0;
          rs->current = 0;
          rs->size = (*window_p)->len;
          *src_state = rs;
          break;
        }

      if (rep_header->type == svn_fs_fs__rep_plain)
        {
          /* This is a plaintext, so just return the current rep_state. */
          *src_state = rs;
          break;
        }

      /* Push this rep onto the list.  If it's self-compressed, we're done. */
      APR_ARRAY_PUSH(*list, rep_state_t *) = rs;
      if (rep_header->type == svn_fs_fs__rep_self_delta)
        {
          *src_state = NULL;
          break;
        }

      rep.revision = rep_header->base_revision;
      rep.item_index = rep_header->base_item_index;
      rep.size = rep_header->base_length;
      svn_fs_fs__id_txn_reset(&rep.txn_id);

      rs = NULL;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Create a rep_read_baton structure for node revision NODEREV in
   filesystem FS and store it in *RB_P.  Perform all allocations in
   POOL.  If rep is mutable, it must be for file contents. */
static svn_error_t *
rep_read_get_baton(struct rep_read_baton **rb_p,
                   svn_fs_t *fs,
                   representation_t *rep,
                   pair_cache_key_t fulltext_cache_key,
                   apr_pool_t *pool)
{
  struct rep_read_baton *b;

  b = apr_pcalloc(pool, sizeof(*b));
  b->fs = fs;
  b->rep = *rep;
  b->base_window = NULL;
  b->chunk_index = 0;
  b->buf = NULL;
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
  b->checksum_finalized = FALSE;
  memcpy(b->md5_digest, rep->md5_digest, sizeof(rep->md5_digest));
  b->len = rep->expanded_size;
  b->off = 0;
  b->fulltext_cache_key = fulltext_cache_key;
  b->pool = svn_pool_create(pool);
  b->filehandle_pool = svn_pool_create(pool);
  b->fulltext_cache = NULL;
  b->fulltext_delivered = 0;
  b->current_fulltext = NULL;

  /* Save our output baton. */
  *rb_p = b;

  return SVN_NO_ERROR;
}

/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN.  Note that RS->CHUNK_INDEX will be THIS_CHUNK rather
   than THIS_CHUNK + 1 when this function returns. */
static svn_error_t *
read_delta_window(svn_txdelta_window_t **nwin, int this_chunk,
                  rep_state_t *rs, apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_boolean_t is_cached;
  apr_off_t start_offset;
  apr_off_t end_offset;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(rs->chunk_index <= this_chunk);

  SVN_ERR(dbg_log_access(rs->sfile->fs, rs->revision, rs->item_index,
                         NULL, SVN_FS_FS__ITEM_TYPE_ANY_REP, scratch_pool));

  /* Read the next window.  But first, try to find it in the cache. */
  SVN_ERR(get_cached_window(nwin, rs, this_chunk, &is_cached,
                            result_pool, scratch_pool));
  if (is_cached)
    return SVN_NO_ERROR;

  /* someone has to actually read the data from file.  Open it */
  SVN_ERR(auto_open_shared_file(rs->sfile));

  /* invoke the 'block-read' feature for non-txn data.
     However, don't do that if we are in the middle of some representation,
     because the block is unlikely to contain other data. */
  if (   rs->chunk_index == 0
      && SVN_IS_VALID_REVNUM(rs->revision)
      && use_block_read(rs->sfile->fs)
      && rs->raw_window_cache)
    {
      SVN_ERR(block_read(NULL, rs->sfile->fs, rs->revision, rs->item_index,
                         rs->sfile->rfile, result_pool, scratch_pool));

      /* reading the whole block probably also provided us with the
         desired txdelta window */
      SVN_ERR(get_cached_window(nwin, rs, this_chunk, &is_cached,
                                result_pool, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* data is still not cached -> we need to read it.
     Make sure we have all the necessary info. */
  SVN_ERR(auto_set_start_offset(rs, scratch_pool));
  SVN_ERR(auto_read_diff_version(rs, scratch_pool));

  /* RS->FILE may be shared between RS instances -> make sure we point
   * to the right data. */
  start_offset = rs->start + rs->current;
  SVN_ERR(rs_aligned_seek(rs, NULL, start_offset, scratch_pool));

  /* Skip windows to reach the current chunk if we aren't there yet. */
  iterpool = svn_pool_create(scratch_pool);
  while (rs->chunk_index < this_chunk)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(svn_txdelta_skip_svndiff_window(rs->sfile->rfile->file,
                                              rs->ver, iterpool));
      rs->chunk_index++;
      SVN_ERR(get_file_offset(&start_offset, rs, iterpool));
      rs->current = start_offset - rs->start;
      if (rs->current >= rs->size)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Reading one svndiff window read "
                                  "beyond the end of the "
                                  "representation"));
    }
  svn_pool_destroy(iterpool);

  /* Actually read the next window. */
  SVN_ERR(svn_txdelta_read_svndiff_window(nwin, rs->sfile->rfile->stream,
                                          rs->ver, result_pool));
  SVN_ERR(get_file_offset(&end_offset, rs, scratch_pool));
  rs->current = end_offset - rs->start;
  if (rs->current > rs->size)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Reading one svndiff window read beyond "
                              "the end of the representation"));

  /* the window has not been cached before, thus cache it now
   * (if caching is used for them at all) */
  if (SVN_IS_VALID_REVNUM(rs->revision))
    SVN_ERR(set_cached_window(*nwin, rs, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read SIZE bytes from the representation RS and return it in *NWIN. */
static svn_error_t *
read_plain_window(svn_stringbuf_t **nwin, rep_state_t *rs,
                  apr_size_t size, apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  apr_off_t offset;

  /* RS->FILE may be shared between RS instances -> make sure we point
   * to the right data. */
  SVN_ERR(auto_open_shared_file(rs->sfile));
  SVN_ERR(auto_set_start_offset(rs, scratch_pool));

  offset = rs->start + rs->current;
  SVN_ERR(rs_aligned_seek(rs, NULL, offset, scratch_pool));

  /* Read the plain data. */
  *nwin = svn_stringbuf_create_ensure(size, result_pool);
  SVN_ERR(svn_io_file_read_full2(rs->sfile->rfile->file, (*nwin)->data, size,
                                 NULL, NULL, result_pool));
  (*nwin)->data[size] = 0;

  /* Update RS. */
  rs->current += (apr_off_t)size;

  return SVN_NO_ERROR;
}

/* Skip SIZE bytes from the PLAIN representation RS. */
static svn_error_t *
skip_plain_window(rep_state_t *rs,
                  apr_size_t size)
{
  /* Update RS. */
  rs->current += (apr_off_t)size;

  return SVN_NO_ERROR;
}

/* Get the undeltified window that is a result of combining all deltas
   from the current desired representation identified in *RB with its
   base representation.  Store the window in *RESULT. */
static svn_error_t *
get_combined_window(svn_stringbuf_t **result,
                    struct rep_read_baton *rb)
{
  apr_pool_t *pool, *new_pool, *window_pool;
  int i;
  apr_array_header_t *windows;
  svn_stringbuf_t *source, *buf = rb->base_window;
  rep_state_t *rs;
  apr_pool_t *iterpool;

  /* Read all windows that we need to combine. This is fine because
     the size of each window is relatively small (100kB) and skip-
     delta limits the number of deltas in a chain to well under 100.
     Stop early if one of them does not depend on its predecessors. */
  window_pool = svn_pool_create(rb->pool);
  windows = apr_array_make(window_pool, 0, sizeof(svn_txdelta_window_t *));
  iterpool = svn_pool_create(rb->pool);
  for (i = 0; i < rb->rs_list->nelts; ++i)
    {
      svn_txdelta_window_t *window;

      svn_pool_clear(iterpool);

      rs = APR_ARRAY_IDX(rb->rs_list, i, rep_state_t *);
      SVN_ERR(read_delta_window(&window, rb->chunk_index, rs, window_pool,
                                iterpool));

      APR_ARRAY_PUSH(windows, svn_txdelta_window_t *) = window;
      if (window->src_ops == 0)
        {
          ++i;
          break;
        }
    }

  /* Combine in the windows from the other delta reps. */
  pool = svn_pool_create(rb->pool);
  for (--i; i >= 0; --i)
    {
      svn_txdelta_window_t *window;

      svn_pool_clear(iterpool);

      rs = APR_ARRAY_IDX(rb->rs_list, i, rep_state_t *);
      window = APR_ARRAY_IDX(windows, i, svn_txdelta_window_t *);

      /* Maybe, we've got a PLAIN start representation.  If we do, read
         as much data from it as the needed for the txdelta window's source
         view.
         Note that BUF / SOURCE may only be NULL in the first iteration.
         Also note that we may have short-cut reading the delta chain --
         in which case SRC_OPS is 0 and it might not be a PLAIN rep. */
      source = buf;
      if (source == NULL && rb->src_state != NULL)
        {
          /* Even if we don't need the source rep now, we still must keep
           * its read offset in sync with what we might need for the next
           * window. */
          if (window->src_ops)
            SVN_ERR(read_plain_window(&source, rb->src_state,
                                      window->sview_len,
                                      pool, iterpool));
          else
            SVN_ERR(skip_plain_window(rb->src_state, window->sview_len));
        }

      /* Combine this window with the current one. */
      new_pool = svn_pool_create(rb->pool);
      buf = svn_stringbuf_create_ensure(window->tview_len, new_pool);
      buf->len = window->tview_len;

      svn_txdelta_apply_instructions(window, source ? source->data : NULL,
                                     buf->data, &buf->len);
      if (buf->len != window->tview_len)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("svndiff window length is "
                                  "corrupt"));

      /* Cache windows only if the whole rep content could be read as a
         single chunk.  Only then will no other chunk need a deeper RS
         list than the cached chunk. */
      if (   (rb->chunk_index == 0) && (rs->current == rs->size)
          && SVN_IS_VALID_REVNUM(rs->revision))
        SVN_ERR(set_cached_combined_window(buf, rs, new_pool));

      rs->chunk_index++;

      /* Cycle pools so that we only need to hold three windows at a time. */
      svn_pool_destroy(pool);
      pool = new_pool;
    }
  svn_pool_destroy(iterpool);

  svn_pool_destroy(window_pool);

  *result = buf;
  return SVN_NO_ERROR;
}

/* Returns whether or not the expanded fulltext of the file is cachable
 * based on its size SIZE.  The decision depends on the cache used by FFD.
 */
static svn_boolean_t
fulltext_size_is_cachable(fs_fs_data_t *ffd, svn_filesize_t size)
{
  return (size < APR_SIZE_MAX)
      && svn_cache__is_cachable(ffd->fulltext_cache, (apr_size_t)size);
}

/* Close method used on streams returned by read_representation().
 */
static svn_error_t *
rep_read_contents_close(void *baton)
{
  struct rep_read_baton *rb = baton;

  svn_pool_destroy(rb->pool);
  svn_pool_destroy(rb->filehandle_pool);

  return SVN_NO_ERROR;
}

/* Return the next *LEN bytes of the rep from our plain / delta windows
   and store them in *BUF. */
static svn_error_t *
get_contents_from_windows(struct rep_read_baton *rb,
                          char *buf,
                          apr_size_t *len)
{
  apr_size_t copy_len, remaining = *len;
  char *cur = buf;
  rep_state_t *rs;

  /* Special case for when there are no delta reps, only a plain
     text. */
  if (rb->rs_list->nelts == 0)
    {
      copy_len = remaining;
      rs = rb->src_state;

      if (rb->base_window != NULL)
        {
          /* We got the desired rep directly from the cache.
             This is where we need the pseudo rep_state created
             by build_rep_list(). */
          apr_size_t offset = (apr_size_t)rs->current;
          if (offset >= rb->base_window->len)
            copy_len = 0ul;
          else if (copy_len > rb->base_window->len - offset)
            copy_len = rb->base_window->len - offset;

          memcpy (cur, rb->base_window->data + offset, copy_len);
        }
      else
        {
          apr_off_t offset;
          if (((apr_off_t) copy_len) > rs->size - rs->current)
            copy_len = (apr_size_t) (rs->size - rs->current);

          SVN_ERR(auto_open_shared_file(rs->sfile));
          SVN_ERR(auto_set_start_offset(rs, rb->pool));

          offset = rs->start + rs->current;
          SVN_ERR(rs_aligned_seek(rs, NULL, offset, rb->pool));
          SVN_ERR(svn_io_file_read_full2(rs->sfile->rfile->file, cur,
                                         copy_len, NULL, NULL, rb->pool));
        }

      rs->current += copy_len;
      *len = copy_len;
      return SVN_NO_ERROR;
    }

  while (remaining > 0)
    {
      /* If we have buffered data from a previous chunk, use that. */
      if (rb->buf)
        {
          /* Determine how much to copy from the buffer. */
          copy_len = rb->buf_len - rb->buf_pos;
          if (copy_len > remaining)
            copy_len = remaining;

          /* Actually copy the data. */
          memcpy(cur, rb->buf + rb->buf_pos, copy_len);
          rb->buf_pos += copy_len;
          cur += copy_len;
          remaining -= copy_len;

          /* If the buffer is all used up, clear it and empty the
             local pool. */
          if (rb->buf_pos == rb->buf_len)
            {
              svn_pool_clear(rb->pool);
              rb->buf = NULL;
            }
        }
      else
        {
          svn_stringbuf_t *sbuf = NULL;

          rs = APR_ARRAY_IDX(rb->rs_list, 0, rep_state_t *);
          if (rs->current == rs->size)
            break;

          /* Get more buffered data by evaluating a chunk. */
          SVN_ERR(get_combined_window(&sbuf, rb));

          rb->chunk_index++;
          rb->buf_len = sbuf->len;
          rb->buf = sbuf->data;
          rb->buf_pos = 0;
        }
    }

  *len = cur - buf;

  return SVN_NO_ERROR;
}

/* Baton type for get_fulltext_partial. */
typedef struct fulltext_baton_t
{
  /* Target buffer to write to; of at least LEN bytes. */
  char *buffer;

  /* Offset within the respective fulltext at which we shall start to
     copy data into BUFFER. */
  apr_size_t start;

  /* Number of bytes to copy.  The actual amount may be less in case
     the fulltext is short(er). */
  apr_size_t len;

  /* Number of bytes actually copied into BUFFER. */
  apr_size_t read;
} fulltext_baton_t;

/* Implement svn_cache__partial_getter_func_t for fulltext caches.
 * From the fulltext in DATA, we copy the range specified by the
 * fulltext_baton_t* BATON into the buffer provided by that baton.
 * OUT and RESULT_POOL are not used.
 */
static svn_error_t *
get_fulltext_partial(void **out,
                     const void *data,
                     apr_size_t data_len,
                     void *baton,
                     apr_pool_t *result_pool)
{
  fulltext_baton_t *fulltext_baton = baton;

  /* We cached the fulltext with an NUL appended to it. */
  apr_size_t fulltext_len = data_len - 1;

  /* Clip the copy range to what the fulltext size allows. */
  apr_size_t start = MIN(fulltext_baton->start, fulltext_len);
  fulltext_baton->read = MIN(fulltext_len - start, fulltext_baton->len);

  /* Copy the data to the output buffer and be done. */
  memcpy(fulltext_baton->buffer, (const char *)data + start,
         fulltext_baton->read);

  return SVN_NO_ERROR;
}

/* Find the fulltext specified in BATON in the fulltext cache given
 * as well by BATON.  If that succeeds, set *CACHED to TRUE and copy
 * up to the next *LEN bytes into BUFFER.  Set *LEN to the actual
 * number of bytes copied.
 */
static svn_error_t *
get_contents_from_fulltext(svn_boolean_t *cached,
                           struct rep_read_baton *baton,
                           char *buffer,
                           apr_size_t *len)
{
  void *dummy;
  fulltext_baton_t fulltext_baton;

  SVN_ERR_ASSERT((apr_size_t)baton->fulltext_delivered
                 == baton->fulltext_delivered);
  fulltext_baton.buffer = buffer;
  fulltext_baton.start = (apr_size_t)baton->fulltext_delivered;
  fulltext_baton.len = *len;
  fulltext_baton.read = 0;

  SVN_ERR(svn_cache__get_partial(&dummy, cached, baton->fulltext_cache,
                                 &baton->fulltext_cache_key,
                                 get_fulltext_partial, &fulltext_baton,
                                 baton->pool));

  if (*cached)
    {
      baton->fulltext_delivered += fulltext_baton.read;
      *len = fulltext_baton.read;
    }

  return SVN_NO_ERROR;
}

/* Determine the optimal size of a string buf that shall receive a
 * (full-) text of NEEDED bytes.
 *
 * The critical point is that those buffers may be very large and
 * can cause memory fragmentation.  We apply simple heuristics to
 * make fragmentation less likely.
 */
static apr_size_t
optimimal_allocation_size(apr_size_t needed)
{
  /* For all allocations, assume some overhead that is shared between
   * OS memory managemnt, APR memory management and svn_stringbuf_t. */
  const apr_size_t overhead = 0x400;
  apr_size_t optimal;

  /* If an allocation size if safe for other ephemeral buffers, it should
   * be safe for ours. */
  if (needed <= SVN__STREAM_CHUNK_SIZE)
    return needed;

  /* Paranoia edge case:
   * Skip our heuristics if they created arithmetical overflow.
   * Beware to make this test work for NEEDED = APR_SIZE_MAX as well! */
  if (needed >= APR_SIZE_MAX / 2 - overhead)
    return needed;

  /* As per definition SVN__STREAM_CHUNK_SIZE is a power of two.
   * Since we know NEEDED to be larger than that, use it as the
   * starting point.
   *
   * Heuristics: Allocate a power-of-two number of bytes that fit
   *             NEEDED plus some OVERHEAD.  The APR allocator
   *             will round it up to the next full page size.
   */
  optimal = SVN__STREAM_CHUNK_SIZE;
  while (optimal - overhead < needed)
    optimal *= 2;

  /* This is above or equal to NEEDED. */
  return optimal - overhead;
}

/* After a fulltext cache lookup failure, we will continue to read from
 * combined delta or plain windows.  However, we must first make that data
 * stream in BATON catch up tho the position LEN already delivered from the
 * fulltext cache.  Also, we need to store the reconstructed fulltext if we
 * want to cache it at the end.
 */
static svn_error_t *
skip_contents(struct rep_read_baton *baton,
              svn_filesize_t len)
{
  svn_error_t *err = SVN_NO_ERROR;

  /* Do we want to cache the reconstructed fulltext? */
  if (SVN_IS_VALID_REVNUM(baton->fulltext_cache_key.revision))
    {
      char *buffer;
      svn_filesize_t to_alloc = MAX(len, baton->len);

      /* This should only be happening if BATON->LEN and LEN are
       * cacheable, implying they fit into memory. */
      SVN_ERR_ASSERT((apr_size_t)to_alloc == to_alloc);

      /* Allocate the fulltext buffer. */
      baton->current_fulltext = svn_stringbuf_create_ensure(
                        optimimal_allocation_size((apr_size_t)to_alloc),
                        baton->filehandle_pool);

      /* Read LEN bytes from the window stream and store the data
       * in the fulltext buffer (will be filled by further reads later). */
      baton->current_fulltext->len = (apr_size_t)len;
      baton->current_fulltext->data[(apr_size_t)len] = 0;

      buffer = baton->current_fulltext->data;
      while (len > 0 && !err)
        {
          apr_size_t to_read = (apr_size_t)len;
          err = get_contents_from_windows(baton, buffer, &to_read);
          len -= to_read;
          buffer += to_read;
        }

      /* Make the MD5 calculation catch up with the data delivered
       * (we did not run MD5 on the data that we took from the cache). */
      if (!err)
        {
          SVN_ERR(svn_checksum_update(baton->md5_checksum_ctx,
                                      baton->current_fulltext->data,
                                      baton->current_fulltext->len));
          baton->off += baton->current_fulltext->len;
        }
    }
  else if (len > 0)
    {
      /* Simply drain LEN bytes from the window stream. */
      apr_pool_t *subpool = svn_pool_create(baton->pool);
      char *buffer = apr_palloc(subpool, SVN__STREAM_CHUNK_SIZE);

      while (len > 0 && !err)
        {
          apr_size_t to_read = len > SVN__STREAM_CHUNK_SIZE
                            ? SVN__STREAM_CHUNK_SIZE
                            : (apr_size_t)len;

          err = get_contents_from_windows(baton, buffer, &to_read);
          len -= to_read;

          /* Make the MD5 calculation catch up with the data delivered
           * (we did not run MD5 on the data that we took from the cache). */
          if (!err)
            {
              SVN_ERR(svn_checksum_update(baton->md5_checksum_ctx,
                                          buffer, to_read));
              baton->off += to_read;
            }
        }

      svn_pool_destroy(subpool);
    }

  return svn_error_trace(err);
}

/* BATON is of type `rep_read_baton'; read the next *LEN bytes of the
   representation and store them in *BUF.  Sum as we read and verify
   the MD5 sum at the end. */
static svn_error_t *
rep_read_contents(void *baton,
                  char *buf,
                  apr_size_t *len)
{
  struct rep_read_baton *rb = baton;

  /* Get data from the fulltext cache for as long as we can. */
  if (rb->fulltext_cache)
    {
      svn_boolean_t cached;
      SVN_ERR(get_contents_from_fulltext(&cached, rb, buf, len));
      if (cached)
        return SVN_NO_ERROR;

      /* Cache miss.  From now on, we will never read from the fulltext
       * cache for this representation anymore. */
      rb->fulltext_cache = NULL;
    }

  /* No fulltext cache to help us.  We must read from the window stream. */
  if (!rb->rs_list)
    {
      /* Window stream not initialized, yet.  Do it now. */
      rb->len = rb->rep.expanded_size;
      SVN_ERR(build_rep_list(&rb->rs_list, &rb->base_window,
                             &rb->src_state, rb->fs, &rb->rep,
                             rb->filehandle_pool));

      /* In case we did read from the fulltext cache before, make the
       * window stream catch up.  Also, initialize the fulltext buffer
       * if we want to cache the fulltext at the end. */
      SVN_ERR(skip_contents(rb, rb->fulltext_delivered));
    }

  /* Get the next block of data.
   * Keep in mind that the representation might be empty and leave us
   * already positioned at the end of the rep. */
  if (rb->off == rb->len)
    *len = 0;
  else
    SVN_ERR(get_contents_from_windows(rb, buf, len));

  if (rb->current_fulltext)
    svn_stringbuf_appendbytes(rb->current_fulltext, buf, *len);

  /* Perform checksumming.  We want to check the checksum as soon as
     the last byte of data is read, in case the caller never performs
     a short read, but we don't want to finalize the MD5 context
     twice. */
  if (!rb->checksum_finalized)
    {
      SVN_ERR(svn_checksum_update(rb->md5_checksum_ctx, buf, *len));
      rb->off += *len;
      if (rb->off == rb->len)
        {
          svn_checksum_t *md5_checksum;
          svn_checksum_t expected;
          expected.kind = svn_checksum_md5;
          expected.digest = rb->md5_digest;

          rb->checksum_finalized = TRUE;
          SVN_ERR(svn_checksum_final(&md5_checksum, rb->md5_checksum_ctx,
                                     rb->pool));
          if (!svn_checksum_match(md5_checksum, &expected))
            return svn_error_create(SVN_ERR_FS_CORRUPT,
                    svn_checksum_mismatch_err(&expected, md5_checksum,
                        rb->pool,
                        _("Checksum mismatch while reading representation")),
                    NULL);
        }
    }

  if (rb->off == rb->len && rb->current_fulltext)
    {
      fs_fs_data_t *ffd = rb->fs->fsap_data;
      SVN_ERR(svn_cache__set(ffd->fulltext_cache, &rb->fulltext_cache_key,
                             rb->current_fulltext, rb->pool));
      rb->current_fulltext = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_contents(svn_stream_t **contents_p,
                        svn_fs_t *fs,
                        representation_t *rep,
                        svn_boolean_t cache_fulltext,
                        apr_pool_t *pool)
{
  if (! rep)
    {
      *contents_p = svn_stream_empty(pool);
    }
  else
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      struct rep_read_baton *rb;

      pair_cache_key_t fulltext_cache_key = { 0 };
      fulltext_cache_key.revision = rep->revision;
      fulltext_cache_key.second = rep->item_index;

      /* Initialize the reader baton.  Some members may added lazily
       * while reading from the stream */
      SVN_ERR(rep_read_get_baton(&rb, fs, rep, fulltext_cache_key, pool));

      /* Make the stream attempt fulltext cache lookups if the fulltext
       * is cacheable.  If it is not, then also don't try to buffer and
       * cache it. */
      if (ffd->fulltext_cache && cache_fulltext
          && SVN_IS_VALID_REVNUM(rep->revision)
          && fulltext_size_is_cachable(ffd, rep->expanded_size))
        {
          rb->fulltext_cache = ffd->fulltext_cache;
        }
      else
        {
          /* This will also prevent the reconstructed fulltext from being
             put into the cache. */
          rb->fulltext_cache_key.revision = SVN_INVALID_REVNUM;
        }

      *contents_p = svn_stream_create(rb, pool);
      svn_stream_set_read2(*contents_p, NULL /* only full read support */,
                           rep_read_contents);
      svn_stream_set_close(*contents_p, rep_read_contents_close);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_contents_from_file(svn_stream_t **contents_p,
                                  svn_fs_t *fs,
                                  representation_t *rep,
                                  apr_file_t *file,
                                  apr_off_t offset,
                                  apr_pool_t *pool)
{
  struct rep_read_baton *rb;
  pair_cache_key_t fulltext_cache_key = { SVN_INVALID_REVNUM, 0 };
  rep_state_t *rs = apr_pcalloc(pool, sizeof(*rs));
  svn_fs_fs__rep_header_t *rh;

  /* Initialize the reader baton.  Some members may added lazily
   * while reading from the stream. */
  SVN_ERR(rep_read_get_baton(&rb, fs, rep, fulltext_cache_key, pool));

  /* Continue constructing RS. Leave caches as NULL. */
  rs->size = rep->size;
  rs->revision = SVN_INVALID_REVNUM;
  rs->item_index = 0;
  rs->ver = -1;
  rs->start = -1;

  /* Provide just enough file access info to allow for a basic read from
   * FILE but leave all index / footer info with empty values b/c FILE
   * probably is not a complete revision file. */
  rs->sfile = apr_pcalloc(pool, sizeof(*rs->sfile));
  rs->sfile->revision = rep->revision;
  rs->sfile->pool = pool;
  rs->sfile->fs = fs;
  rs->sfile->rfile = apr_pcalloc(pool, sizeof(*rs->sfile->rfile));
  rs->sfile->rfile->start_revision = SVN_INVALID_REVNUM;
  rs->sfile->rfile->file = file;
  rs->sfile->rfile->stream = svn_stream_from_aprfile2(file, TRUE, pool);

  /* Read the rep header. */
  SVN_ERR(aligned_seek(fs, file, NULL, offset, pool));
  SVN_ERR(svn_fs_fs__read_rep_header(&rh, rs->sfile->rfile->stream,
                                     pool, pool));
  SVN_ERR(get_file_offset(&rs->start, rs, pool));
  rs->header_size = rh->header_size;

  /* Log the access. */
  SVN_ERR(dbg_log_access(fs, SVN_INVALID_REVNUM, 0, rh,
                         SVN_FS_FS__ITEM_TYPE_ANY_REP, pool));

  /* Build the representation list (delta chain). */
  if (rh->type == svn_fs_fs__rep_plain)
    {
      rb->rs_list = apr_array_make(pool, 0, sizeof(rep_state_t *));
      rb->src_state = rs;
    }
  else if (rh->type == svn_fs_fs__rep_self_delta)
    {
      rb->rs_list = apr_array_make(pool, 1, sizeof(rep_state_t *));
      APR_ARRAY_PUSH(rb->rs_list, rep_state_t *) = rs;
      rb->src_state = NULL;
    }
  else
    {
      representation_t next_rep = { 0 };

      /* skip "SVNx" diff marker */
      rs->current = 4;

      /* REP's base rep is inside a proper revision.
       * It can be reconstructed in the usual way.  */
      next_rep.revision = rh->base_revision;
      next_rep.item_index = rh->base_item_index;
      next_rep.size = rh->base_length;
      svn_fs_fs__id_txn_reset(&next_rep.txn_id);

      SVN_ERR(build_rep_list(&rb->rs_list, &rb->base_window,
                             &rb->src_state, rb->fs, &next_rep,
                             rb->filehandle_pool));

      /* Insert the access to REP as the first element of the delta chain. */
      svn_sort__array_insert(rb->rs_list, &rs, 0);
    }

  /* Now, the baton is complete and we can assemble the stream around it. */
  *contents_p = svn_stream_create(rb, pool);
  svn_stream_set_read2(*contents_p, NULL /* only full read support */,
                       rep_read_contents);
  svn_stream_set_close(*contents_p, rep_read_contents_close);

  return SVN_NO_ERROR;
}

/* Baton for cache_access_wrapper. Wraps the original parameters of
 * svn_fs_fs__try_process_file_content().
 */
typedef struct cache_access_wrapper_baton_t
{
  svn_fs_process_contents_func_t func;
  void* baton;
} cache_access_wrapper_baton_t;

/* Wrapper to translate between svn_fs_process_contents_func_t and
 * svn_cache__partial_getter_func_t.
 */
static svn_error_t *
cache_access_wrapper(void **out,
                     const void *data,
                     apr_size_t data_len,
                     void *baton,
                     apr_pool_t *pool)
{
  cache_access_wrapper_baton_t *wrapper_baton = baton;

  SVN_ERR(wrapper_baton->func((const unsigned char *)data,
                              data_len - 1, /* cache adds terminating 0 */
                              wrapper_baton->baton,
                              pool));

  /* non-NULL value to signal the calling cache that all went well */
  *out = baton;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__try_process_file_contents(svn_boolean_t *success,
                                     svn_fs_t *fs,
                                     node_revision_t *noderev,
                                     svn_fs_process_contents_func_t processor,
                                     void* baton,
                                     apr_pool_t *pool)
{
  representation_t *rep = noderev->data_rep;
  if (rep)
    {
      fs_fs_data_t *ffd = fs->fsap_data;
      pair_cache_key_t fulltext_cache_key = { 0 };

      fulltext_cache_key.revision = rep->revision;
      fulltext_cache_key.second = rep->item_index;
      if (ffd->fulltext_cache && SVN_IS_VALID_REVNUM(rep->revision)
          && fulltext_size_is_cachable(ffd, rep->expanded_size))
        {
          cache_access_wrapper_baton_t wrapper_baton;
          void *dummy = NULL;

          wrapper_baton.func = processor;
          wrapper_baton.baton = baton;
          return svn_cache__get_partial(&dummy, success,
                                        ffd->fulltext_cache,
                                        &fulltext_cache_key,
                                        cache_access_wrapper,
                                        &wrapper_baton,
                                        pool);
        }
    }

  *success = FALSE;
  return SVN_NO_ERROR;
}


/* Baton used when reading delta windows. */
struct delta_read_baton
{
  rep_state_t *rs;
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];
};

/* This implements the svn_txdelta_next_window_fn_t interface. */
static svn_error_t *
delta_read_next_window(svn_txdelta_window_t **window, void *baton,
                       apr_pool_t *pool)
{
  struct delta_read_baton *drb = baton;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  *window = NULL;
  if (drb->rs->current < drb->rs->size)
    {
      SVN_ERR(read_delta_window(window, drb->rs->chunk_index, drb->rs, pool,
                                scratch_pool));
      drb->rs->chunk_index++;
    }

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

/* This implements the svn_txdelta_md5_digest_fn_t interface. */
static const unsigned char *
delta_read_md5_digest(void *baton)
{
  struct delta_read_baton *drb = baton;
  return drb->md5_digest;
}

/* Return a txdelta stream for on-disk representation REP_STATE
 * of TARGET.  Allocate the result in POOL.
 */
static svn_txdelta_stream_t *
get_storaged_delta_stream(rep_state_t *rep_state,
                          node_revision_t *target,
                          apr_pool_t *pool)
{
  /* Create the delta read baton. */
  struct delta_read_baton *drb = apr_pcalloc(pool, sizeof(*drb));
  drb->rs = rep_state;
  memcpy(drb->md5_digest, target->data_rep->md5_digest,
         sizeof(drb->md5_digest));
  return svn_txdelta_stream_create(drb, delta_read_next_window,
                                   delta_read_md5_digest, pool);
}

svn_error_t *
svn_fs_fs__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                 svn_fs_t *fs,
                                 node_revision_t *source,
                                 node_revision_t *target,
                                 apr_pool_t *pool)
{
  svn_stream_t *source_stream, *target_stream;
  rep_state_t *rep_state;
  svn_fs_fs__rep_header_t *rep_header;
  fs_fs_data_t *ffd = fs->fsap_data;

  /* Try a shortcut: if the target is stored as a delta against the source,
     then just use that delta.  However, prefer using the fulltext cache
     whenever that is available. */
  if (target->data_rep && (source || ! ffd->fulltext_cache))
    {
      /* Read target's base rep if any. */
      SVN_ERR(create_rep_state(&rep_state, &rep_header, NULL,
                                target->data_rep, fs, pool, pool));

      if (source && source->data_rep && target->data_rep)
        {
          /* If that matches source, then use this delta as is.
             Note that we want an actual delta here.  E.g. a self-delta would
             not be good enough. */
          if (rep_header->type == svn_fs_fs__rep_delta
              && rep_header->base_revision == source->data_rep->revision
              && rep_header->base_item_index == source->data_rep->item_index)
            {
              *stream_p = get_storaged_delta_stream(rep_state, target, pool);
              return SVN_NO_ERROR;
            }
        }
      else if (!source)
        {
          /* We want a self-delta. There is a fair chance that TARGET got
             added in this revision and is already stored in the requested
             format. */
          if (rep_header->type == svn_fs_fs__rep_self_delta)
            {
              *stream_p = get_storaged_delta_stream(rep_state, target, pool);
              return SVN_NO_ERROR;
            }
        }

      /* Don't keep file handles open for longer than necessary. */
      if (rep_state->sfile->rfile)
        {
          SVN_ERR(svn_fs_fs__close_revision_file(rep_state->sfile->rfile));
          rep_state->sfile->rfile = NULL;
        }
    }

  /* Read both fulltexts and construct a delta. */
  if (source)
    SVN_ERR(svn_fs_fs__get_contents(&source_stream, fs, source->data_rep,
                                    TRUE, pool));
  else
    source_stream = svn_stream_empty(pool);
  SVN_ERR(svn_fs_fs__get_contents(&target_stream, fs, target->data_rep,
                                  TRUE, pool));

  /* Because source and target stream will already verify their content,
   * there is no need to do this once more.  In particular if the stream
   * content is being fetched from cache. */
  svn_txdelta2(stream_p, source_stream, target_stream, FALSE, pool);

  return SVN_NO_ERROR;
}

/* Return TRUE when all svn_fs_dirent_t* in ENTRIES are already sorted
   by their respective name. */
static svn_boolean_t
sorted(apr_array_header_t *entries)
{
  int i;

  const svn_fs_dirent_t * const *dirents = (const void *)entries->elts;
  for (i = 0; i < entries->nelts-1; ++i)
    if (strcmp(dirents[i]->name, dirents[i+1]->name) > 0)
      return FALSE;

  return TRUE;
}

/* Compare the names of the two dirents given in **A and **B. */
static int
compare_dirents(const void *a, const void *b)
{
  const svn_fs_dirent_t *lhs = *((const svn_fs_dirent_t * const *) a);
  const svn_fs_dirent_t *rhs = *((const svn_fs_dirent_t * const *) b);

  return strcmp(lhs->name, rhs->name);
}

/* Compare the name of the dirents given in **A with the C string in *B. */
static int
compare_dirent_name(const void *a, const void *b)
{
  const svn_fs_dirent_t *lhs = *((const svn_fs_dirent_t * const *) a);
  const char *rhs = b;

  return strcmp(lhs->name, rhs);
}

/* Into *ENTRIES_P, read all directories entries from the key-value text in
 * STREAM.  If INCREMENTAL is TRUE, read until the end of the STREAM and
 * update the data.  ID is provided for nicer error messages.
 */
static svn_error_t *
read_dir_entries(apr_array_header_t **entries_p,
                 svn_stream_t *stream,
                 svn_boolean_t incremental,
                 const svn_fs_id_t *id,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *hash = NULL;
  const char *terminator = SVN_HASH_TERMINATOR;
  apr_array_header_t *entries = NULL;

  if (incremental)
    hash = svn_hash__make(scratch_pool);
  else
    entries = apr_array_make(result_pool, 16, sizeof(svn_fs_dirent_t *));

  /* Read until the terminator (non-incremental) or the end of STREAM
     (incremental mode).  In the latter mode, we use a temporary HASH
     to make updating and removing entries cheaper. */
  while (1)
    {
      svn_hash__entry_t entry;
      svn_fs_dirent_t *dirent;
      char *str;

      svn_pool_clear(iterpool);
      SVN_ERR_W(svn_hash__read_entry(&entry, stream, terminator,
                                     incremental, iterpool),
                apr_psprintf(iterpool,
                             _("Directory representation corrupt in '%s'"),
                             svn_fs_fs__id_unparse(id, scratch_pool)->data));

      /* End of directory? */
      if (entry.key == NULL)
        {
          /* In incremental mode, we skip the terminator and read the
             increments following it until the end of the stream. */
          if (incremental && terminator)
            terminator = NULL;
          else
            break;
        }

      /* Deleted entry? */
      if (entry.val == NULL)
        {
          /* We must be in incremental mode */
          assert(hash);
          apr_hash_set(hash, entry.key, entry.keylen, NULL);
          continue;
        }

      /* Add a new directory entry. */
      dirent = apr_pcalloc(result_pool, sizeof(*dirent));
      dirent->name = apr_pstrmemdup(result_pool, entry.key, entry.keylen);

      str = svn_cstring_tokenize(" ", &entry.val);
      if (str == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Directory entry corrupt in '%s'"),
                           svn_fs_fs__id_unparse(id, scratch_pool)->data);

      if (strcmp(str, SVN_FS_FS__KIND_FILE) == 0)
        {
          dirent->kind = svn_node_file;
        }
      else if (strcmp(str, SVN_FS_FS__KIND_DIR) == 0)
        {
          dirent->kind = svn_node_dir;
        }
      else
        {
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Directory entry corrupt in '%s'"),
                           svn_fs_fs__id_unparse(id, scratch_pool)->data);
        }

      str = svn_cstring_tokenize(" ", &entry.val);
      if (str == NULL)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Directory entry corrupt in '%s'"),
                           svn_fs_fs__id_unparse(id, scratch_pool)->data);

      SVN_ERR(svn_fs_fs__id_parse(&dirent->id, str, result_pool));

      /* In incremental mode, update the hash; otherwise, write to the
       * final array.  Be sure to use hash keys that survive this iteration.
       */
      if (incremental)
        apr_hash_set(hash, dirent->name, entry.keylen, dirent);
      else
        APR_ARRAY_PUSH(entries, svn_fs_dirent_t *) = dirent;
    }

  /* Convert container to a sorted array. */
  if (incremental)
    {
      apr_hash_index_t *hi;

      entries = apr_array_make(result_pool, apr_hash_count(hash),
                               sizeof(svn_fs_dirent_t *));
      for (hi = apr_hash_first(iterpool, hash); hi; hi = apr_hash_next(hi))
        APR_ARRAY_PUSH(entries, svn_fs_dirent_t *) = apr_hash_this_val(hi);
    }

  if (!sorted(entries))
    svn_sort__array(entries, compare_dirents);

  svn_pool_destroy(iterpool);

  *entries_p = entries;
  return SVN_NO_ERROR;
}

/* For directory NODEREV in FS, return the *FILESIZE of its in-txn
 * representation.  If the directory representation is comitted data,
 * set *FILESIZE to SVN_INVALID_FILESIZE. Use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
get_txn_dir_info(svn_filesize_t *filesize,
                 svn_fs_t *fs,
                 node_revision_t *noderev,
                 apr_pool_t *scratch_pool)
{
  if (noderev->data_rep && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
    {
      const svn_io_dirent2_t *dirent;
      const char *filename;

      filename = svn_fs_fs__path_txn_node_children(fs, noderev->id,
                                                   scratch_pool);

      SVN_ERR(svn_io_stat_dirent2(&dirent, filename, FALSE, FALSE,
                                  scratch_pool, scratch_pool));
      *filesize = dirent->filesize;
    }
  else
    {
      *filesize = SVN_INVALID_FILESIZE;
    }

  return SVN_NO_ERROR;
}

/* Fetch the contents of a directory into DIR.  Values are stored
   as filename to string mappings; further conversion is necessary to
   convert them into svn_fs_dirent_t values. */
static svn_error_t *
get_dir_contents(svn_fs_fs__dir_data_t *dir,
                 svn_fs_t *fs,
                 node_revision_t *noderev,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_stream_t *contents;

  /* Initialize the result. */
  dir->txn_filesize = SVN_INVALID_FILESIZE;

  /* Read dir contents - unless there is none in which case we are done. */
  if (noderev->data_rep && svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
    {
      /* Get location & current size of the directory representation. */
      const char *filename;
      apr_file_t *file;

      filename = svn_fs_fs__path_txn_node_children(fs, noderev->id,
                                                   scratch_pool);

      /* The representation is mutable.  Read the old directory
         contents from the mutable children file, followed by the
         changes we've made in this transaction. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_READ | APR_BUFFERED,
                               APR_OS_DEFAULT, scratch_pool));

      /* Obtain txn children file size. */
      SVN_ERR(svn_io_file_size_get(&dir->txn_filesize, file, scratch_pool));

      contents = svn_stream_from_aprfile2(file, FALSE, scratch_pool);
      SVN_ERR(read_dir_entries(&dir->entries, contents, TRUE, noderev->id,
                               result_pool, scratch_pool));
      SVN_ERR(svn_stream_close(contents));
    }
  else if (noderev->data_rep)
    {
      /* Undeltify content before parsing it. Otherwise, we could only
       * parse it byte-by-byte.
       */
      apr_size_t len = noderev->data_rep->expanded_size;
      svn_stringbuf_t *text;

      /* The representation is immutable.  Read it normally. */
      SVN_ERR(svn_fs_fs__get_contents(&contents, fs, noderev->data_rep,
                                      FALSE, scratch_pool));
      SVN_ERR(svn_stringbuf_from_stream(&text, contents, len, scratch_pool));
      SVN_ERR(svn_stream_close(contents));

      /* de-serialize hash */
      contents = svn_stream_from_stringbuf(text, scratch_pool);
      SVN_ERR(read_dir_entries(&dir->entries, contents, FALSE, noderev->id,
                               result_pool, scratch_pool));
    }
  else
    {
       dir->entries = apr_array_make(result_pool, 0, sizeof(svn_fs_dirent_t *));
    }

  return SVN_NO_ERROR;
}


/* Return the cache object in FS responsible to storing the directory the
 * NODEREV plus the corresponding *KEY.  If no cache exists, return NULL.
 * PAIR_KEY must point to some key struct, which does not need to be
 * initialized.  We use it to avoid dynamic allocation.
 */
static svn_cache__t *
locate_dir_cache(svn_fs_t *fs,
                 const void **key,
                 pair_cache_key_t *pair_key,
                 node_revision_t *noderev,
                 apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  if (!noderev->data_rep)
    {
      /* no data rep -> empty directory.
         A NULL key causes a cache miss. */
      *key = NULL;
      return ffd->dir_cache;
    }

  if (svn_fs_fs__id_txn_used(&noderev->data_rep->txn_id))
    {
      /* data in txns requires the expensive fs_id-based addressing mode */
      *key = svn_fs_fs__id_unparse(noderev->id, pool)->data;

      return ffd->txn_dir_cache;
    }
  else
    {
      /* committed data can use simple rev,item pairs */
      pair_key->revision = noderev->data_rep->revision;
      pair_key->second = noderev->data_rep->item_index;
      *key = pair_key;

      return ffd->dir_cache;
    }
}

svn_error_t *
svn_fs_fs__rep_contents_dir(apr_array_header_t **entries_p,
                            svn_fs_t *fs,
                            node_revision_t *noderev,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  pair_cache_key_t pair_key = { 0 };
  const void *key;
  svn_fs_fs__dir_data_t *dir;

  /* find the cache we may use */
  svn_cache__t *cache = locate_dir_cache(fs, &key, &pair_key, noderev,
                                         scratch_pool);
  if (cache)
    {
      svn_boolean_t found;

      SVN_ERR(svn_cache__get((void **)&dir, &found, cache, key,
                             result_pool));
      if (found)
        {
          /* Verify that the cached dir info is not stale
           * (no-op for committed data). */
          svn_filesize_t filesize;
          SVN_ERR(get_txn_dir_info(&filesize, fs, noderev, scratch_pool));

          if (filesize == dir->txn_filesize)
            {
              /* Still valid. Done. */
              *entries_p = dir->entries;
              return SVN_NO_ERROR;
            }
        }
    }

  /* Read in the directory contents. */
  dir = apr_pcalloc(scratch_pool, sizeof(*dir));
  SVN_ERR(get_dir_contents(dir, fs, noderev, result_pool, scratch_pool));
  *entries_p = dir->entries;

  /* Update the cache, if we are to use one.
   *
   * Don't even attempt to serialize very large directories; it would cause
   * an unnecessary memory allocation peak.  150 bytes/entry is about right.
   */
  if (cache && svn_cache__is_cachable(cache, 150 * dir->entries->nelts))
    SVN_ERR(svn_cache__set(cache, key, dir, scratch_pool));

  return SVN_NO_ERROR;
}

svn_fs_dirent_t *
svn_fs_fs__find_dir_entry(apr_array_header_t *entries,
                          const char *name,
                          int *hint)
{
  svn_fs_dirent_t **result
    = svn_sort__array_lookup(entries, name, hint, compare_dirent_name);
  return result ? *result : NULL;
}

svn_error_t *
svn_fs_fs__rep_contents_dir_entry(svn_fs_dirent_t **dirent,
                                  svn_fs_t *fs,
                                  node_revision_t *noderev,
                                  const char *name,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  extract_dir_entry_baton_t baton;
  svn_boolean_t found = FALSE;

  /* find the cache we may use */
  pair_cache_key_t pair_key = { 0 };
  const void *key;
  svn_cache__t *cache = locate_dir_cache(fs, &key, &pair_key, noderev,
                                         scratch_pool);
  if (cache)
    {
      svn_filesize_t filesize;
      SVN_ERR(get_txn_dir_info(&filesize, fs, noderev, scratch_pool));

      /* Cache lookup. */
      baton.txn_filesize = filesize;
      baton.name = name;
      SVN_ERR(svn_cache__get_partial((void **)dirent,
                                     &found,
                                     cache,
                                     key,
                                     svn_fs_fs__extract_dir_entry,
                                     &baton,
                                     result_pool));
    }

  /* fetch data from disk if we did not find it in the cache */
  if (! found || baton.out_of_date)
    {
      svn_fs_dirent_t *entry;
      svn_fs_dirent_t *entry_copy = NULL;
      svn_fs_fs__dir_data_t dir;

      /* Read in the directory contents. */
      SVN_ERR(get_dir_contents(&dir, fs, noderev, scratch_pool,
                               scratch_pool));

      /* Update the cache, if we are to use one.
       *
       * Don't even attempt to serialize very large directories; it would
       * cause an unnecessary memory allocation peak.  150 bytes / entry is
       * about right. */
      if (cache && svn_cache__is_cachable(cache, 150 * dir.entries->nelts))
        SVN_ERR(svn_cache__set(cache, key, &dir, scratch_pool));

      /* find desired entry and return a copy in POOL, if found */
      entry = svn_fs_fs__find_dir_entry(dir.entries, name, NULL);
      if (entry)
        {
          entry_copy = apr_palloc(result_pool, sizeof(*entry_copy));
          entry_copy->name = apr_pstrdup(result_pool, entry->name);
          entry_copy->id = svn_fs_fs__id_copy(entry->id, result_pool);
          entry_copy->kind = entry->kind;
        }

      *dirent = entry_copy;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_proplist(apr_hash_t **proplist_p,
                        svn_fs_t *fs,
                        node_revision_t *noderev,
                        apr_pool_t *pool)
{
  apr_hash_t *proplist;
  svn_stream_t *stream;

  if (noderev->prop_rep && svn_fs_fs__id_txn_used(&noderev->prop_rep->txn_id))
    {
      svn_error_t *err;
      const char *filename
        = svn_fs_fs__path_txn_node_props(fs, noderev->id, pool);
      proplist = apr_hash_make(pool);

      SVN_ERR(svn_stream_open_readonly(&stream, filename, pool, pool));
      err = svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool);
      if (err)
        {
          svn_string_t *id_str = svn_fs_fs__id_unparse(noderev->id, pool);

          err = svn_error_compose_create(err, svn_stream_close(stream));
          return svn_error_quick_wrapf(err,
                   _("malformed property list for node-revision '%s' in '%s'"),
                   id_str->data, filename);
        }
      SVN_ERR(svn_stream_close(stream));
    }
  else if (noderev->prop_rep)
    {
      svn_error_t *err;
      fs_fs_data_t *ffd = fs->fsap_data;
      representation_t *rep = noderev->prop_rep;
      pair_cache_key_t key = { 0 };

      key.revision = rep->revision;
      key.second = rep->item_index;
      if (ffd->properties_cache && SVN_IS_VALID_REVNUM(rep->revision))
        {
          svn_boolean_t is_cached;
          SVN_ERR(svn_cache__get((void **) proplist_p, &is_cached,
                                 ffd->properties_cache, &key, pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }

      proplist = apr_hash_make(pool);
      SVN_ERR(svn_fs_fs__get_contents(&stream, fs, noderev->prop_rep, FALSE,
                                      pool));
      err = svn_hash_read2(proplist, stream, SVN_HASH_TERMINATOR, pool);
      if (err)
        {
          svn_string_t *id_str = svn_fs_fs__id_unparse(noderev->id, pool);

          err = svn_error_compose_create(err, svn_stream_close(stream));
          return svn_error_quick_wrapf(err,
                   _("malformed property list for node-revision '%s'"),
                   id_str->data);
        }
      SVN_ERR(svn_stream_close(stream));

      if (ffd->properties_cache && SVN_IS_VALID_REVNUM(rep->revision))
        SVN_ERR(svn_cache__set(ffd->properties_cache, &key, proplist, pool));
    }
  else
    {
      /* return an empty prop list if the node doesn't have any props */
      proplist = apr_hash_make(pool);
    }

  *proplist_p = proplist;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__create_changes_context(svn_fs_fs__changes_context_t **context,
                                  svn_fs_t *fs,
                                  svn_revnum_t rev,
                                  apr_pool_t *result_pool)
{
  svn_fs_fs__changes_context_t *result = apr_pcalloc(result_pool,
                                                     sizeof(*result));
  result->fs = fs;
  result->revision = rev;
  result->rev_file_pool = result_pool;

  *context = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_changes(apr_array_header_t **changes,
                       svn_fs_fs__changes_context_t *context,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_off_t item_index = SVN_FS_FS__ITEM_INDEX_CHANGES;
  svn_boolean_t found;
  fs_fs_data_t *ffd = context->fs->fsap_data;
  svn_fs_fs__changes_list_t *changes_list;

  pair_cache_key_t key;
  key.revision = context->revision;
  key.second = context->next;

  /* try cache lookup first */

  if (ffd->changes_cache)
    {
      SVN_ERR(svn_cache__get((void **)&changes_list, &found,
                             ffd->changes_cache, &key, result_pool));
    }
  else
    {
      found = FALSE;
    }

  if (!found)
    {
      /* read changes from revision file */

      if (!context->revision_file)
        {
          SVN_ERR(svn_fs_fs__ensure_revision_exists(context->revision,
                                                    context->fs,
                                                    scratch_pool));
          SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&context->revision_file,
                                                   context->fs,
                                                   context->revision,
                                                   context->rev_file_pool,
                                                   scratch_pool));
        }

      if (use_block_read(context->fs))
        {
          /* 'block-read' will probably populate the cache with the data
           * that we want.  However, we won't want to force it to process
           * very large change lists as part of this prefetching mechanism.
           * Those would be better handled by the iterative code below. */
          SVN_ERR(block_read(NULL, context->fs,
                             context->revision, SVN_FS_FS__ITEM_INDEX_CHANGES,
                             context->revision_file, scratch_pool,
                             scratch_pool));

          /* This may succeed now ... */
          SVN_ERR(svn_cache__get((void **)&changes_list, &found,
                                 ffd->changes_cache, &key, result_pool));
        }

      /* If we still have no data, read it here. */
      if (!found)
        {
          apr_off_t changes_offset;

          /* Addressing is very different for old formats
           * (needs to read the revision trailer). */
          if (svn_fs_fs__use_log_addressing(context->fs))
            {
              SVN_ERR(svn_fs_fs__item_offset(&changes_offset, context->fs,
                                             context->revision_file,
                                             context->revision, NULL,
                                             SVN_FS_FS__ITEM_INDEX_CHANGES,
                                             scratch_pool));
            }
          else
            {
              SVN_ERR(get_root_changes_offset(NULL, &changes_offset,
                                              context->revision_file,
                                              context->fs, context->revision,
                                              scratch_pool));

              /* This variable will be used for debug logging only. */
              item_index = changes_offset;
            }

          /* Actual reading and parsing are the same, though. */
          SVN_ERR(aligned_seek(context->fs, context->revision_file->file,
                               NULL, changes_offset + context->next_offset,
                               scratch_pool));

          SVN_ERR(svn_fs_fs__read_changes(changes,
                                          context->revision_file->stream,
                                          SVN_FS_FS__CHANGES_BLOCK_SIZE,
                                          result_pool, scratch_pool));

          /* Construct the info object for the entries block we just read. */
          changes_list = apr_pcalloc(scratch_pool, sizeof(*changes_list));
          SVN_ERR(svn_io_file_get_offset(&changes_list->end_offset,
                                         context->revision_file->file,
                                         scratch_pool));
          changes_list->end_offset -= changes_offset;
          changes_list->start_offset = context->next_offset;
          changes_list->count = (*changes)->nelts;
          changes_list->changes = (change_t **)(*changes)->elts;
          changes_list->eol = changes_list->count < SVN_FS_FS__CHANGES_BLOCK_SIZE;

          /* cache for future reference */

          if (ffd->changes_cache)
            SVN_ERR(svn_cache__set(ffd->changes_cache, &key, changes_list,
                                   scratch_pool));
        }
    }

  if (found)
    {
      /* Return the block as a "proper" APR array. */
      (*changes) = apr_array_make(result_pool, 0, sizeof(void *));
      (*changes)->elts = (char *)changes_list->changes;
      (*changes)->nelts = changes_list->count;
      (*changes)->nalloc = changes_list->count;
    }

  /* Where to look next - if there is more data. */
  context->next += (*changes)->nelts;
  context->next_offset = changes_list->end_offset;
  context->eol = changes_list->eol;

  /* Close the revision file after we read all data. */
  if (context->eol && context->revision_file)
    {
      SVN_ERR(svn_fs_fs__close_revision_file(context->revision_file));
      context->revision_file = NULL;
    }

  SVN_ERR(dbg_log_access(context->fs, context->revision, item_index, *changes,
                         SVN_FS_FS__ITEM_TYPE_CHANGES, scratch_pool));

  return SVN_NO_ERROR;
}

/* Inialize the representation read state RS for the given REP_HEADER and
 * p2l index ENTRY.  If not NULL, assign FILE and STREAM to RS.
 * Use RESULT_POOL for allocations.
 */
static svn_error_t *
init_rep_state(rep_state_t *rs,
               svn_fs_fs__rep_header_t *rep_header,
               svn_fs_t *fs,
               svn_fs_fs__revision_file_t *file,
               svn_fs_fs__p2l_entry_t* entry,
               apr_pool_t *result_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  shared_file_t *shared_file = apr_pcalloc(result_pool, sizeof(*shared_file));

  /* this function does not apply to representation containers */
  SVN_ERR_ASSERT(entry->type >= SVN_FS_FS__ITEM_TYPE_FILE_REP
                 && entry->type <= SVN_FS_FS__ITEM_TYPE_DIR_PROPS);

  shared_file->rfile = file;
  shared_file->fs = fs;
  shared_file->revision = entry->item.revision;
  shared_file->pool = result_pool;

  rs->sfile = shared_file;
  rs->revision = entry->item.revision;
  rs->item_index = entry->item.number;
  rs->header_size = rep_header->header_size;
  rs->start = entry->offset + rs->header_size;
  rs->current = rep_header->type == svn_fs_fs__rep_plain ? 0 : 4;
  rs->size = entry->size - rep_header->header_size - 7;
  rs->ver = -1;
  rs->chunk_index = 0;
  rs->raw_window_cache = ffd->raw_window_cache;
  rs->window_cache = ffd->txdelta_window_cache;
  rs->combined_cache = ffd->combined_window_cache;

  return SVN_NO_ERROR;
}

/* Implement svn_cache__partial_getter_func_t for txdelta windows.
 * Instead of the whole window data, return only END_OFFSET member.
 */
static svn_error_t *
get_txdelta_window_end(void **out,
                       const void *data,
                       apr_size_t data_len,
                       void *baton,
                       apr_pool_t *result_pool)
{
  const svn_fs_fs__txdelta_cached_window_t *window
    = (const svn_fs_fs__txdelta_cached_window_t *)data;
  *(apr_off_t*)out = window->end_offset;

  return SVN_NO_ERROR;
}

/* Implement svn_cache__partial_getter_func_t for raw windows.
 * Instead of the whole window data, return only END_OFFSET member.
 */
static svn_error_t *
get_raw_window_end(void **out,
                   const void *data,
                   apr_size_t data_len,
                   void *baton,
                   apr_pool_t *result_pool)
{
  const svn_fs_fs__raw_cached_window_t *window
    = (const svn_fs_fs__raw_cached_window_t *)data;
  *(apr_off_t*)out = window->end_offset;

  return SVN_NO_ERROR;
}

/* Walk through all windows in the representation addressed by RS in FS
 * (excluding the delta bases) and put those not already cached into the
 * window caches.  If MAX_OFFSET is not -1, don't read windows that start
 * at or beyond that offset.  Use POOL for temporary allocations.
 *
 * This function requires RS->RAW_WINDOW_CACHE and RS->WINDOW_CACHE to
 * be non-NULL.
 */
static svn_error_t *
cache_windows(svn_fs_t *fs,
              rep_state_t *rs,
              apr_off_t max_offset,
              apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(auto_read_diff_version(rs, iterpool));

  while (rs->current < rs->size)
    {
      apr_off_t end_offset;
      svn_boolean_t found = FALSE;
      window_cache_key_t key = { 0 };

      svn_pool_clear(iterpool);

      if (max_offset != -1 && rs->start + rs->current >= max_offset)
        {
          svn_pool_destroy(iterpool);
          return SVN_NO_ERROR;
        }

      /* We don't need to read the data again if it is already in cache.
       * It might be cached as either raw or parsed window.
       */
      SVN_ERR(svn_cache__get_partial((void **) &end_offset, &found,
                                     rs->raw_window_cache,
                                     get_window_key(&key, rs),
                                     get_raw_window_end, NULL,
                                     iterpool));
      if (! found)
        SVN_ERR(svn_cache__get_partial((void **) &end_offset, &found,
                                       rs->window_cache, &key,
                                       get_txdelta_window_end, NULL,
                                       iterpool));

      if (found)
        {
          rs->current = end_offset;
        }
      else
        {
          /* Read, decode and cache the window. */
          svn_fs_fs__raw_cached_window_t window;
          apr_off_t start_offset = rs->start + rs->current;
          apr_size_t window_len;
          char *buf;

          /* navigate to the current window */
          SVN_ERR(rs_aligned_seek(rs, NULL, start_offset, iterpool));
          SVN_ERR(svn_txdelta__read_raw_window_len(&window_len,
                                                   rs->sfile->rfile->stream,
                                                   iterpool));

          /* Read the raw window. */
          buf = apr_palloc(iterpool, window_len + 1);
          SVN_ERR(rs_aligned_seek(rs, NULL, start_offset, iterpool));
          SVN_ERR(svn_io_file_read_full2(rs->sfile->rfile->file, buf,
                                         window_len, NULL, NULL, iterpool));
          buf[window_len] = 0;

          /* update relative offset in representation */
          rs->current += window_len;

          /* Construct the cachable raw window object. */
          window.end_offset = rs->current;
          window.window.len = window_len;
          window.window.data = buf;
          window.ver = rs->ver;

          /* cache the window now */
          SVN_ERR(svn_cache__set(rs->raw_window_cache, &key, &window,
                                 iterpool));
        }

      if (rs->current > rs->size)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Reading one svndiff window read beyond "
                                            "the end of the representation"));

      rs->chunk_index++;
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Read all txdelta / plain windows following REP_HEADER in FS as described
 * by ENTRY.  Read the data from the already open FILE and the wrapping
 * STREAM object.  If MAX_OFFSET is not -1, don't read windows that start
 * at or beyond that offset.  Use SCRATCH_POOL for temporary allocations.
 * If caching is not enabled, this is a no-op.
 */
static svn_error_t *
block_read_windows(svn_fs_fs__rep_header_t *rep_header,
                   svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_fs_fs__p2l_entry_t* entry,
                   apr_off_t max_offset,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  rep_state_t rs = { 0 };
  apr_off_t offset;
  window_cache_key_t key = { 0 };

  if (   (rep_header->type != svn_fs_fs__rep_plain
          && (!ffd->txdelta_window_cache || !ffd->raw_window_cache))
      || (rep_header->type == svn_fs_fs__rep_plain
          && !ffd->combined_window_cache))
    return SVN_NO_ERROR;

  SVN_ERR(init_rep_state(&rs, rep_header, fs, rev_file, entry,
                         result_pool));

  /* RS->FILE may be shared between RS instances -> make sure we point
   * to the right data. */
  offset = rs.start + rs.current;
  if (rep_header->type == svn_fs_fs__rep_plain)
    {
      svn_stringbuf_t *plaintext;
      svn_boolean_t is_cached;

      /* already in cache? */
      SVN_ERR(svn_cache__has_key(&is_cached, rs.combined_cache,
                                 get_window_key(&key, &rs),
                                 scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;

      /* for larger reps, the header may have crossed a block boundary.
       * make sure we still read blocks properly aligned, i.e. don't use
       * plain seek here. */
      SVN_ERR(aligned_seek(fs, rev_file->file, NULL, offset, scratch_pool));

      plaintext = svn_stringbuf_create_ensure(rs.size, result_pool);
      SVN_ERR(svn_io_file_read_full2(rev_file->file, plaintext->data,
                                     rs.size, &plaintext->len, NULL,
                                     result_pool));
      plaintext->data[plaintext->len] = 0;
      rs.current += rs.size;

      SVN_ERR(set_cached_combined_window(plaintext, &rs, scratch_pool));
    }
  else
    {
      SVN_ERR(cache_windows(fs, &rs, max_offset, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Try to get the representation header identified by KEY from FS's cache.
 * If it has not been cached, read it from the current position in STREAM
 * and put it into the cache (if caching has been enabled for rep headers).
 * Return the result in *REP_HEADER.  Use POOL for allocations.
 */
static svn_error_t *
read_rep_header(svn_fs_fs__rep_header_t **rep_header,
                svn_fs_t *fs,
                svn_stream_t *stream,
                pair_cache_key_t *key,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t is_cached = FALSE;

  if (ffd->rep_header_cache)
    {
      SVN_ERR(svn_cache__get((void**)rep_header, &is_cached,
                             ffd->rep_header_cache, key,
                             result_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(svn_fs_fs__read_rep_header(rep_header, stream, result_pool,
                                     scratch_pool));

  if (ffd->rep_header_cache)
    SVN_ERR(svn_cache__set(ffd->rep_header_cache, key, *rep_header,
                           scratch_pool));

  return SVN_NO_ERROR;
}

/* Fetch the representation data (header, txdelta / plain windows)
 * addressed by ENTRY->ITEM in FS and cache it if caches are enabled.
 * Read the data from REV_FILE.  If MAX_OFFSET is not -1, don't read
 * windows that start at or beyond that offset.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
block_read_contents(svn_fs_t *fs,
                    svn_fs_fs__revision_file_t *rev_file,
                    svn_fs_fs__p2l_entry_t* entry,
                    apr_off_t max_offset,
                    apr_pool_t *scratch_pool)
{
  pair_cache_key_t header_key = { 0 };
  svn_fs_fs__rep_header_t *rep_header;

  header_key.revision = (apr_int32_t)entry->item.revision;
  header_key.second = entry->item.number;

  SVN_ERR(read_rep_header(&rep_header, fs, rev_file->stream, &header_key,
                          scratch_pool, scratch_pool));
  SVN_ERR(block_read_windows(rep_header, fs, rev_file, entry, max_offset,
                             scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* For the given REV_FILE in FS, in *STREAM return a stream covering the
 * item specified by ENTRY.  Also, verify the item's content by low-level
 * checksum.  Allocate the result in POOL.
 */
static svn_error_t *
read_item(svn_stream_t **stream,
          svn_fs_t *fs,
          svn_fs_fs__revision_file_t *rev_file,
          svn_fs_fs__p2l_entry_t* entry,
          apr_pool_t *pool)
{
  apr_uint32_t digest;
  svn_checksum_t *expected, *actual;
  apr_uint32_t plain_digest;

  /* Read item into string buffer. */
  svn_stringbuf_t *text = svn_stringbuf_create_ensure(entry->size, pool);
  text->len = entry->size;
  text->data[text->len] = 0;
  SVN_ERR(svn_io_file_read_full2(rev_file->file, text->data, text->len,
                                 NULL, NULL, pool));

  /* Return (construct, calculate) stream and checksum. */
  *stream = svn_stream_from_stringbuf(text, pool);
  digest = svn__fnv1a_32x4(text->data, text->len);

  /* Checksums will match most of the time. */
  if (entry->fnv1_checksum == digest)
    return SVN_NO_ERROR;

  /* Construct proper checksum objects from their digests to allow for
   * nice error messages. */
  plain_digest = htonl(entry->fnv1_checksum);
  expected = svn_checksum__from_digest_fnv1a_32x4(
                (const unsigned char *)&plain_digest, pool);
  plain_digest = htonl(digest);
  actual = svn_checksum__from_digest_fnv1a_32x4(
                (const unsigned char *)&plain_digest, pool);

  /* Construct the full error message with all the info we have. */
  return svn_checksum_mismatch_err(expected, actual, pool,
                 _("Low-level checksum mismatch while reading\n"
                   "%s bytes of meta data at offset %s "
                   "for item %s in revision %ld"),
                 apr_off_t_toa(pool, entry->size),
                 apr_off_t_toa(pool, entry->offset),
                 apr_psprintf(pool, "%" APR_UINT64_T_FMT, entry->item.number),
                 entry->item.revision);
}

/* If not already cached, read the changed paths list addressed by ENTRY in
 * FS and cache it if it has no more than SVN_FS_FS__CHANGES_BLOCK_SIZE
 * entries and caching is enabled.  Read the data from REV_FILE.
 * Allocate temporaries in SCRATCH_POOL.
 */
static svn_error_t *
block_read_changes(svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_fs_fs__p2l_entry_t *entry,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;
  apr_array_header_t *changes;

  pair_cache_key_t key;
  key.revision = entry->item.revision;
  key.second = 0;

  if (!ffd->changes_cache)
    return SVN_NO_ERROR;

  /* already in cache? */
  if (ffd->changes_cache)
    {
      svn_boolean_t is_cached;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->changes_cache, &key,
                                 scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* Read changes from revision file.  But read just past the first block to
     enable us to determine whether the first block already hit the EOL.

     Note: A 100 entries block is already > 10kB on disk.  With a 4kB default
           disk block size, this function won't even be called for larger
           changed paths lists. */
  SVN_ERR(svn_fs_fs__read_changes(&changes, stream,
                                  SVN_FS_FS__CHANGES_BLOCK_SIZE + 1,
                                  scratch_pool, scratch_pool));

  /* We can only cache small lists that don't need to be split up.
     For longer lists, we miss the file offset info for the respective */
  if (changes->nelts <= SVN_FS_FS__CHANGES_BLOCK_SIZE)
    {
      svn_fs_fs__changes_list_t changes_list;

      /* Construct the info object for the entries block we just read. */
      changes_list.end_offset = entry->size;
      changes_list.start_offset = 0;
      changes_list.count = changes->nelts;
      changes_list.changes = (change_t **)changes->elts;
      changes_list.eol = TRUE;

      SVN_ERR(svn_cache__set(ffd->changes_cache, &key, &changes_list,
                             scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* If not already cached or if MUST_READ is set, read the node revision
 * addressed by ENTRY in FS and retúrn it in *NODEREV_P.  Cache the
 * result if caching is enabled.  Read the data from REV_FILE.  Allocate
 * *NODEREV_P in RESUSLT_POOL and allocate temporaries in SCRATCH_POOL.
 */
static svn_error_t *
block_read_noderev(node_revision_t **noderev_p,
                   svn_fs_t *fs,
                   svn_fs_fs__revision_file_t *rev_file,
                   svn_fs_fs__p2l_entry_t *entry,
                   svn_boolean_t must_read,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;

  pair_cache_key_t key = { 0 };
  key.revision = entry->item.revision;
  key.second = entry->item.number;

  if (!must_read && !ffd->node_revision_cache)
    return SVN_NO_ERROR;

  /* already in cache? */
  if (!must_read && ffd->node_revision_cache)
    {
      svn_boolean_t is_cached;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->node_revision_cache,
                                 &key, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* read node rev from revision file */
  SVN_ERR(svn_fs_fs__read_noderev(noderev_p, stream,
                                  result_pool, scratch_pool));
  SVN_ERR(fixup_node_revision(fs, *noderev_p, scratch_pool));

  if (ffd->node_revision_cache)
    SVN_ERR(svn_cache__set(ffd->node_revision_cache, &key, *noderev_p,
                           scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the whole (e.g. 64kB) block containing ITEM_INDEX of REVISION in FS
 * and put all data into cache.  If necessary and depending on heuristics,
 * neighboring blocks may also get read.  The data is being read from
 * already open REVISION_FILE, which must be the correct rev / pack file
 * w.r.t. REVISION.
 *
 * For noderevs and changed path lists, the item fetched can be allocated
 * RESULT_POOL and returned in *RESULT.  Otherwise, RESULT must be NULL.
 */
static svn_error_t *
block_read(void **result,
           svn_fs_t *fs,
           svn_revnum_t revision,
           apr_uint64_t item_index,
           svn_fs_fs__revision_file_t *revision_file,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_off_t offset, wanted_offset = 0;
  apr_off_t block_start = 0;
  apr_array_header_t *entries;
  int run_count = 0;
  int i;
  apr_pool_t *iterpool;

  /* Block read is an optional feature. If the caller does not want anything
   * specific we may not have to read anything. */
  if (!result)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  /* don't try this on transaction protorev files */
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));

  /* index lookup: find the OFFSET of the item we *must* read plus (in the
   * "do-while" block) the list of items in the same block. */
  SVN_ERR(svn_fs_fs__item_offset(&wanted_offset, fs, revision_file,
                                 revision, NULL, item_index, iterpool));

  offset = wanted_offset;

  /* Heuristics:
   *
   * Read this block.  If the last item crosses the block boundary, read
   * the next block but stop there.  Because cross-boundary items cause
   * blocks to be read twice, this heuristics will limit this effect to
   * approx. 50% of blocks, probably less, while providing a sensible
   * amount of read-ahead.
   */
  do
    {
      /* fetch list of items in the block surrounding OFFSET */
      block_start = offset - (offset % ffd->block_size);
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, fs, revision_file,
                                          revision, block_start,
                                          ffd->block_size, scratch_pool,
                                          scratch_pool));

      SVN_ERR(aligned_seek(fs, revision_file->file, &block_start, offset,
                           iterpool));

      /* read all items from the block */
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_boolean_t is_result, is_wanted;
          apr_pool_t *pool;
          svn_fs_fs__p2l_entry_t* entry;

          svn_pool_clear(iterpool);

          /* skip empty sections */
          entry = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);
          if (entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
            continue;

          /* the item / container we were looking for? */
          is_wanted =    entry->offset == wanted_offset
                      && entry->item.revision == revision
                      && entry->item.number == item_index;
          is_result = result && is_wanted;

          /* select the pool that we want the item to be allocated in */
          pool = is_result ? result_pool : iterpool;

          /* handle all items that start within this block and are relatively
           * small (i.e. < block size).  Always read the item we need to return.
           */
          if (is_result || (   entry->offset >= block_start
                            && entry->size < ffd->block_size))
            {
              void *item = NULL;
              SVN_ERR(svn_io_file_seek(revision_file->file, APR_SET,
                                       &entry->offset, iterpool));
              switch (entry->type)
                {
                  case SVN_FS_FS__ITEM_TYPE_FILE_REP:
                  case SVN_FS_FS__ITEM_TYPE_DIR_REP:
                  case SVN_FS_FS__ITEM_TYPE_FILE_PROPS:
                  case SVN_FS_FS__ITEM_TYPE_DIR_PROPS:
                    SVN_ERR(block_read_contents(fs, revision_file, entry,
                                                is_wanted
                                                  ? -1
                                                  : block_start + ffd->block_size,
                                                iterpool));
                    break;

                  case SVN_FS_FS__ITEM_TYPE_NODEREV:
                    if (ffd->node_revision_cache || is_result)
                      SVN_ERR(block_read_noderev((node_revision_t **)&item,
                                                 fs, revision_file,
                                                 entry, is_result, pool,
                                                 iterpool));
                    break;

                  case SVN_FS_FS__ITEM_TYPE_CHANGES:
                    SVN_ERR(block_read_changes(fs, revision_file,
                                               entry, iterpool));
                    break;

                  default:
                    break;
                }

              if (is_result)
                *result = item;

              /* if we crossed a block boundary, read the remainder of
               * the last block as well */
              offset = entry->offset + entry->size;
              if (offset - block_start > ffd->block_size)
                ++run_count;
            }
        }

    }
  while(run_count++ == 1); /* can only be true once and only if a block
                            * boundary got crossed */

  /* if the caller requested a result, we must have provided one by now */
  assert(!result || *result);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
