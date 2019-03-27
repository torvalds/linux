/* recovery.c --- FSFS recovery functionality
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

#include "recovery.h"

#include "svn_hash.h"
#include "svn_pools.h"
#include "private/svn_string_private.h"

#include "index.h"
#include "low_level.h"
#include "rep-cache.h"
#include "revprops.h"
#include "util.h"
#include "cached_data.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* Part of the recovery procedure.  Return the largest revision *REV in
   filesystem FS.  Use POOL for temporary allocation. */
static svn_error_t *
recover_get_largest_revision(svn_fs_t *fs, svn_revnum_t *rev, apr_pool_t *pool)
{
  /* Discovering the largest revision in the filesystem would be an
     expensive operation if we did a readdir() or searched linearly,
     so we'll do a form of binary search.  left is a revision that we
     know exists, right a revision that we know does not exist. */
  apr_pool_t *iterpool;
  svn_revnum_t left, right = 1;

  iterpool = svn_pool_create(pool);
  /* Keep doubling right, until we find a revision that doesn't exist. */
  while (1)
    {
      svn_error_t *err;
      svn_fs_fs__revision_file_t *file;
      svn_pool_clear(iterpool);

      err = svn_fs_fs__open_pack_or_rev_file(&file, fs, right, iterpool,
                                             iterpool);
      if (err && err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
        {
          svn_error_clear(err);
          break;
        }
      else
        SVN_ERR(err);

      right <<= 1;
    }

  left = right >> 1;

  /* We know that left exists and right doesn't.  Do a normal bsearch to find
     the last revision. */
  while (left + 1 < right)
    {
      svn_revnum_t probe = left + ((right - left) / 2);
      svn_error_t *err;
      svn_fs_fs__revision_file_t *file;
      svn_pool_clear(iterpool);

      err = svn_fs_fs__open_pack_or_rev_file(&file, fs, probe, iterpool,
                                             iterpool);
      if (err && err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
        {
          svn_error_clear(err);
          right = probe;
        }
      else
        {
          SVN_ERR(err);
          left = probe;
        }
    }

  svn_pool_destroy(iterpool);

  /* left is now the largest revision that exists. */
  *rev = left;
  return SVN_NO_ERROR;
}

/* A baton for reading a fixed amount from an open file.  For
   recover_find_max_ids() below. */
struct recover_read_from_file_baton
{
  svn_stream_t *stream;
  apr_pool_t *pool;
  apr_off_t remaining;
};

/* A stream read handler used by recover_find_max_ids() below.
   Read and return at most BATON->REMAINING bytes from the stream,
   returning nothing after that to indicate EOF. */
static svn_error_t *
read_handler_recover(void *baton, char *buffer, apr_size_t *len)
{
  struct recover_read_from_file_baton *b = baton;
  apr_size_t bytes_to_read = *len;

  if (b->remaining == 0)
    {
      /* Return a successful read of zero bytes to signal EOF. */
      *len = 0;
      return SVN_NO_ERROR;
    }

  if ((apr_int64_t)bytes_to_read > (apr_int64_t)b->remaining)
    bytes_to_read = (apr_size_t)b->remaining;
  b->remaining -= bytes_to_read;

  return svn_stream_read_full(b->stream, buffer, &bytes_to_read);
}

/* Part of the recovery procedure.  Read the directory noderev at offset
   OFFSET of file REV_FILE (the revision file of revision REV of
   filesystem FS), and set MAX_NODE_ID and MAX_COPY_ID to be the node-id
   and copy-id of that node, if greater than the current value stored
   in either.  Recurse into any child directories that were modified in
   this revision.

   MAX_NODE_ID and MAX_COPY_ID must be arrays of at least MAX_KEY_SIZE.

   Perform temporary allocation in POOL. */
static svn_error_t *
recover_find_max_ids(svn_fs_t *fs,
                     svn_revnum_t rev,
                     svn_fs_fs__revision_file_t *rev_file,
                     apr_off_t offset,
                     apr_uint64_t *max_node_id,
                     apr_uint64_t *max_copy_id,
                     apr_pool_t *pool)
{
  svn_fs_fs__rep_header_t *header;
  struct recover_read_from_file_baton baton;
  svn_stream_t *stream;
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  node_revision_t *noderev;
  svn_error_t *err;

  baton.stream = rev_file->stream;
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &offset, pool));
  SVN_ERR(svn_fs_fs__read_noderev(&noderev, baton.stream, pool, pool));

  /* Check that this is a directory.  It should be. */
  if (noderev->kind != svn_node_dir)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Recovery encountered a non-directory node"));

  /* Get the data location.  No data location indicates an empty directory. */
  if (!noderev->data_rep)
    return SVN_NO_ERROR;

  /* If the directory's data representation wasn't changed in this revision,
     we've already scanned the directory's contents for noderevs, so we don't
     need to again.  This will occur if a property is changed on a directory
     without changing the directory's contents. */
  if (noderev->data_rep->revision != rev)
    return SVN_NO_ERROR;

  /* We could use get_dir_contents(), but this is much cheaper.  It does
     rely on directory entries being stored as PLAIN reps, though. */
  SVN_ERR(svn_fs_fs__item_offset(&offset, fs, rev_file, rev, NULL,
                                 noderev->data_rep->item_index, pool));
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &offset, pool));
  SVN_ERR(svn_fs_fs__read_rep_header(&header, baton.stream, pool, pool));
  if (header->type != svn_fs_fs__rep_plain)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Recovery encountered a deltified directory "
                              "representation"));

  /* Now create a stream that's allowed to read only as much data as is
     stored in the representation.  Note that this is a directory, i.e.
     represented using the hash format on disk and can never have 0 length. */
  baton.pool = pool;
  baton.remaining = noderev->data_rep->expanded_size;
  stream = svn_stream_create(&baton, pool);
  svn_stream_set_read2(stream, NULL /* only full read support */,
                       read_handler_recover);

  /* Now read the entries from that stream. */
  entries = apr_hash_make(pool);
  err = svn_hash_read2(entries, stream, SVN_HASH_TERMINATOR, pool);
  if (err)
    {
      svn_string_t *id_str = svn_fs_fs__id_unparse(noderev->id, pool);

      err = svn_error_compose_create(err, svn_stream_close(stream));
      return svn_error_quick_wrapf(err,
                _("malformed representation for node-revision '%s'"),
                id_str->data);
    }
  SVN_ERR(svn_stream_close(stream));

  /* Now check each of the entries in our directory to find new node and
     copy ids, and recurse into new subdirectories. */
  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      char *str_val;
      char *str;
      svn_node_kind_t kind;
      const svn_fs_id_t *id;
      const svn_fs_fs__id_part_t *rev_item;
      apr_uint64_t node_id, copy_id;
      apr_off_t child_dir_offset;
      const svn_string_t *path = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      str_val = apr_pstrdup(iterpool, path->data);

      str = svn_cstring_tokenize(" ", &str_val);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Directory entry corrupt"));

      if (strcmp(str, SVN_FS_FS__KIND_FILE) == 0)
        kind = svn_node_file;
      else if (strcmp(str, SVN_FS_FS__KIND_DIR) == 0)
        kind = svn_node_dir;
      else
        {
          return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                  _("Directory entry corrupt"));
        }

      str = svn_cstring_tokenize(" ", &str_val);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Directory entry corrupt"));

      SVN_ERR(svn_fs_fs__id_parse(&id, str, iterpool));

      rev_item = svn_fs_fs__id_rev_item(id);
      if (rev_item->revision != rev)
        {
          /* If the node wasn't modified in this revision, we've already
             checked the node and copy id. */
          continue;
        }

      node_id = svn_fs_fs__id_node_id(id)->number;
      copy_id = svn_fs_fs__id_copy_id(id)->number;

      if (node_id > *max_node_id)
        *max_node_id = node_id;
      if (copy_id > *max_copy_id)
        *max_copy_id = copy_id;

      if (kind == svn_node_file)
        continue;

      SVN_ERR(svn_fs_fs__item_offset(&child_dir_offset, fs,
                                     rev_file, rev, NULL, rev_item->number,
                                     iterpool));
      SVN_ERR(recover_find_max_ids(fs, rev, rev_file, child_dir_offset,
                                   max_node_id, max_copy_id, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Part of the recovery procedure.  Given an open non-packed revision file
   REV_FILE for REV, locate the trailer that specifies the offset to the root
   node-id and store this offset in *ROOT_OFFSET.  Do temporary allocations in
   POOL. */
static svn_error_t *
recover_get_root_offset(apr_off_t *root_offset,
                        svn_revnum_t rev,
                        svn_fs_fs__revision_file_t *rev_file,
                        apr_pool_t *pool)
{
  char buffer[64];
  svn_stringbuf_t *trailer;
  apr_off_t start;
  apr_off_t end;
  apr_size_t len;

  SVN_ERR_ASSERT(!rev_file->is_packed);

  /* We will assume that the last line containing the two offsets (to the root
     node-id and to the changed path information) will never be longer than 64
     characters. */
  end = 0;
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_END, &end, pool));

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

  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &start, pool));
  SVN_ERR(svn_io_file_read_full2(rev_file->file, buffer, len,
                                 NULL, NULL, pool));

  trailer = svn_stringbuf_ncreate(buffer, len, pool);
  SVN_ERR(svn_fs_fs__parse_revision_trailer(root_offset, NULL, trailer, rev));

  return SVN_NO_ERROR;
}

/* Baton used for recover_body below. */
struct recover_baton {
  svn_fs_t *fs;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
};

/* The work-horse for svn_fs_fs__recover, called with the FS
   write lock.  This implements the svn_fs_fs__with_write_lock()
   'body' callback type.  BATON is a 'struct recover_baton *'. */
static svn_error_t *
recover_body(void *baton, apr_pool_t *pool)
{
  struct recover_baton *b = baton;
  svn_fs_t *fs = b->fs;
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t max_rev;
  apr_uint64_t next_node_id = 0;
  apr_uint64_t next_copy_id = 0;
  svn_revnum_t youngest_rev;
  svn_node_kind_t youngest_revprops_kind;

  /* The admin may have created a plain copy of this repo before attempting
     to recover it (hotcopy may or may not work with corrupted repos).
     Bump the instance ID. */
  SVN_ERR(svn_fs_fs__set_uuid(fs, fs->uuid, NULL, pool));

  /* We need to know the largest revision in the filesystem. */
  SVN_ERR(recover_get_largest_revision(fs, &max_rev, pool));

  /* Get the expected youngest revision */
  SVN_ERR(svn_fs_fs__youngest_rev(&youngest_rev, fs, pool));

  /* Policy note:

     Since the revprops file is written after the revs file, the true
     maximum available revision is the youngest one for which both are
     present.  That's probably the same as the max_rev we just found,
     but if it's not, we could, in theory, repeatedly decrement
     max_rev until we find a revision that has both a revs and
     revprops file, then write db/current with that.

     But we choose not to.  If a repository is so corrupt that it's
     missing at least one revprops file, we shouldn't assume that the
     youngest revision for which both the revs and revprops files are
     present is healthy.  In other words, we're willing to recover
     from a missing or out-of-date db/current file, because db/current
     is truly redundant -- it's basically a cache so we don't have to
     find max_rev each time, albeit a cache with unusual semantics,
     since it also officially defines when a revision goes live.  But
     if we're missing more than the cache, it's time to back out and
     let the admin reconstruct things by hand: correctness at that
     point may depend on external things like checking a commit email
     list, looking in particular working copies, etc.

     This policy matches well with a typical naive backup scenario.
     Say you're rsyncing your FSFS repository nightly to the same
     location.  Once revs and revprops are written, you've got the
     maximum rev; if the backup should bomb before db/current is
     written, then db/current could stay arbitrarily out-of-date, but
     we can still recover.  It's a small window, but we might as well
     do what we can. */

  /* Even if db/current were missing, it would be created with 0 by
     get_youngest(), so this conditional remains valid. */
  if (youngest_rev > max_rev)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Expected current rev to be <= %ld "
                               "but found %ld"), max_rev, youngest_rev);

  /* We only need to search for maximum IDs for old FS formats which
     se global ID counters. */
  if (ffd->format < SVN_FS_FS__MIN_NO_GLOBAL_IDS_FORMAT)
    {
      /* Next we need to find the maximum node id and copy id in use across the
         filesystem.  Unfortunately, the only way we can get this information
         is to scan all the noderevs of all the revisions and keep track as
         we go along. */
      svn_revnum_t rev;
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (rev = 0; rev <= max_rev; rev++)
        {
          svn_fs_fs__revision_file_t *rev_file;
          apr_off_t root_offset;

          svn_pool_clear(iterpool);

          if (b->cancel_func)
            SVN_ERR(b->cancel_func(b->cancel_baton));

          SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, fs, rev, pool,
                                                   iterpool));
          SVN_ERR(recover_get_root_offset(&root_offset, rev, rev_file, pool));
          SVN_ERR(recover_find_max_ids(fs, rev, rev_file, root_offset,
                                       &next_node_id, &next_copy_id, pool));
          SVN_ERR(svn_fs_fs__close_revision_file(rev_file));
        }
      svn_pool_destroy(iterpool);

      /* Now that we finally have the maximum revision, node-id and copy-id, we
         can bump the two ids to get the next of each. */
      next_node_id++;
      next_copy_id++;
    }

  /* Before setting current, verify that there is a revprops file
     for the youngest revision.  (Issue #2992) */
  SVN_ERR(svn_io_check_path(svn_fs_fs__path_revprops(fs, max_rev, pool),
                            &youngest_revprops_kind, pool));
  if (youngest_revprops_kind == svn_node_none)
    {
      svn_boolean_t missing = TRUE;
      if (!svn_fs_fs__packed_revprop_available(&missing, fs, max_rev, pool))
        {
          if (missing)
            {
              return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                      _("Revision %ld has a revs file but no "
                                        "revprops file"),
                                      max_rev);
            }
          else
            {
              return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                      _("Revision %ld has a revs file but the "
                                        "revprops file is inaccessible"),
                                      max_rev);
            }
          }
    }
  else if (youngest_revprops_kind != svn_node_file)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Revision %ld has a non-file where its "
                                 "revprops file should be"),
                               max_rev);
    }

  /* Prune younger-than-(newfound-youngest) revisions from the rep
     cache if sharing is enabled taking care not to create the cache
     if it does not exist. */
  if (ffd->rep_sharing_allowed)
    {
      svn_boolean_t rep_cache_exists;

      SVN_ERR(svn_fs_fs__exists_rep_cache(&rep_cache_exists, fs, pool));
      if (rep_cache_exists)
        SVN_ERR(svn_fs_fs__del_rep_reference(fs, max_rev, pool));
    }

  /* Now store the discovered youngest revision, and the next IDs if
     relevant, in a new 'current' file. */
  return svn_fs_fs__write_current(fs, max_rev, next_node_id, next_copy_id,
                                  pool);
}

/* This implements the fs_library_vtable_t.recover() API. */
svn_error_t *
svn_fs_fs__recover(svn_fs_t *fs,
                   svn_cancel_func_t cancel_func, void *cancel_baton,
                   apr_pool_t *pool)
{
  struct recover_baton b;

  /* We have no way to take out an exclusive lock in FSFS, so we're
     restricted as to the types of recovery we can do.  Luckily,
     we just want to recreate the 'current' file, and we can do that just
     by blocking other writers. */
  b.fs = fs;
  b.cancel_func = cancel_func;
  b.cancel_baton = cancel_baton;
  return svn_fs_fs__with_all_locks(fs, recover_body, &b, pool);
}
