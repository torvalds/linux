/* cached_data.c --- cached (read) access to FSX data
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

#include "private/svn_io_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_temp_serializer.h"

#include "fs_x.h"
#include "low_level.h"
#include "util.h"
#include "pack.h"
#include "temp_serializer.h"
#include "index.h"
#include "changes.h"
#include "noderevs.h"
#include "reps.h"

#include "../libsvn_fs/fs-loader.h"
#include "../libsvn_delta/delta.h"  /* for SVN_DELTA_WINDOW_SIZE */

#include "svn_private_config.h"

/* forward-declare. See implementation for the docstring */
static svn_error_t *
block_read(void **result,
           svn_fs_t *fs,
           const svn_fs_x__id_t *id,
           svn_fs_x__revision_file_t *revision_file,
           void *baton,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool);


/* Defined this to enable access logging via dgb__log_access
#define SVN_FS_X__LOG_ACCESS
*/

/* When SVN_FS_X__LOG_ACCESS has been defined, write a line to console
 * showing where ID is located in FS and use ITEM to show details on it's
 * contents if not NULL.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
dbg__log_access(svn_fs_t *fs,
                const svn_fs_x__id_t *id,
                void *item,
                apr_uint32_t item_type,
                apr_pool_t *scratch_pool)
{
  /* no-op if this macro is not defined */
#ifdef SVN_FS_X__LOG_ACCESS
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_off_t offset = -1;
  apr_off_t end_offset = 0;
  apr_uint32_t sub_item = 0;
  svn_fs_x__p2l_entry_t *entry = NULL;
  static const char *types[] = {"<n/a>", "frep ", "drep ", "fprop", "dprop",
                                "node ", "chgs ", "rep  ", "c:", "n:", "r:"};
  const char *description = "";
  const char *type = types[item_type];
  const char *pack = "";
  svn_revnum_t revision = svn_fs_x__get_revnum(id->change_set);

  /* determine rev / pack file offset */
  SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, id, scratch_pool));

  /* constructing the pack file description */
  if (revision < ffd->min_unpacked_rev)
    pack = apr_psprintf(scratch_pool, "%4ld|",
                        revision / ffd->max_files_per_dir);

  /* construct description if possible */
  if (item_type == SVN_FS_X__ITEM_TYPE_NODEREV && item != NULL)
    {
      svn_fs_x__noderev_t *node = item;
      const char *data_rep
        = node->data_rep
        ? apr_psprintf(scratch_pool, " d=%ld/%" APR_UINT64_T_FMT,
                       svn_fs_x__get_revnum(node->data_rep->id.change_set),
                       node->data_rep->id.number)
        : "";
      const char *prop_rep
        = node->prop_rep
        ? apr_psprintf(scratch_pool, " p=%ld/%" APR_UINT64_T_FMT,
                       svn_fs_x__get_revnum(node->prop_rep->id.change_set),
                       node->prop_rep->id.number)
        : "";
      description = apr_psprintf(scratch_pool, "%s   (pc=%d%s%s)",
                                 node->created_path,
                                 node->predecessor_count,
                                 data_rep,
                                 prop_rep);
    }
  else if (item_type == SVN_FS_X__ITEM_TYPE_ANY_REP)
    {
      svn_fs_x__rep_header_t *header = item;
      if (header == NULL)
        description = "  (txdelta window)";
      else if (header->type == svn_fs_x__rep_self_delta)
        description = "  DELTA";
      else
        description = apr_psprintf(scratch_pool,
                                   "  DELTA against %ld/%" APR_UINT64_T_FMT,
                                   header->base_revision,
                                   header->base_item_index);
    }
  else if (item_type == SVN_FS_X__ITEM_TYPE_CHANGES && item != NULL)
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

  /* reverse index lookup: get item description in ENTRY */
  SVN_ERR(svn_fs_x__p2l_entry_lookup(&entry, fs, revision, offset,
                                      scratch_pool));
  if (entry)
    {
      /* more details */
      end_offset = offset + entry->size;
      type = types[entry->type];

      /* merge the sub-item number with the container type */
      if (   entry->type == SVN_FS_X__ITEM_TYPE_CHANGES_CONT
          || entry->type == SVN_FS_X__ITEM_TYPE_NODEREVS_CONT
          || entry->type == SVN_FS_X__ITEM_TYPE_REPS_CONT)
        type = apr_psprintf(scratch_pool, "%s%-3d", type, sub_item);
    }

  /* line output */
  printf("%5s%4lx:%04lx -%4lx:%04lx %s %7ld %5"APR_UINT64_T_FMT"   %s\n",
          pack, (long)(offset / ffd->block_size),
          (long)(offset % ffd->block_size),
          (long)(end_offset / ffd->block_size),
          (long)(end_offset % ffd->block_size),
          type, revision, id->number, description);

#endif

  return SVN_NO_ERROR;
}

/* Open the revision file for the item given by ID in filesystem FS and
   store the newly opened file in FILE.  Seek to the item's location before
   returning.

   Allocate the result in RESULT_POOL and temporaries in SCRATCH_POOL. */
static svn_error_t *
open_and_seek_revision(svn_fs_x__revision_file_t **file,
                       svn_fs_t *fs,
                       const svn_fs_x__id_t *id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_fs_x__revision_file_t *rev_file;
  apr_off_t offset = -1;
  apr_uint32_t sub_item = 0;
  svn_revnum_t rev = svn_fs_x__get_revnum(id->change_set);

  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, scratch_pool));

  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, rev, result_pool));
  SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, rev_file, id,
                                scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, offset));

  *file = rev_file;

  return SVN_NO_ERROR;
}

/* Open the representation REP for a node-revision in filesystem FS, seek
   to its position and store the newly opened file in FILE.

   Allocate the result in RESULT_POOL and temporaries in SCRATCH_POOL. */
static svn_error_t *
open_and_seek_transaction(svn_fs_x__revision_file_t **file,
                          svn_fs_t *fs,
                          svn_fs_x__representation_t *rep,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_off_t offset;
  apr_uint32_t sub_item = 0;
  apr_int64_t txn_id = svn_fs_x__get_txn_id(rep->id.change_set);

  SVN_ERR(svn_fs_x__rev_file_open_proto_rev(file, fs, txn_id, result_pool,
                                            scratch_pool));

  SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, *file, &rep->id,
                                scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_seek(*file, NULL, offset));

  return SVN_NO_ERROR;
}

/* Given a node-id ID, and a representation REP in filesystem FS, open
   the correct file and seek to the correction location.  Store this
   file in *FILE_P.

   Allocate the result in RESULT_POOL and temporaries in SCRATCH_POOL. */
static svn_error_t *
open_and_seek_representation(svn_fs_x__revision_file_t **file_p,
                             svn_fs_t *fs,
                             svn_fs_x__representation_t *rep,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  if (svn_fs_x__is_revision(rep->id.change_set))
    return open_and_seek_revision(file_p, fs, &rep->id, result_pool,
                                  scratch_pool);
  else
    return open_and_seek_transaction(file_p, fs, rep, result_pool,
                                     scratch_pool);
}



static svn_error_t *
err_dangling_id(svn_fs_t *fs,
                const svn_fs_x__id_t *id)
{
  svn_string_t *id_str = svn_fs_x__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}

/* Get the node-revision for the node ID in FS.
   Set *NODEREV_P to the new node-revision structure, allocated in POOL.
   See svn_fs_x__get_node_revision, which wraps this and adds another
   error. */
static svn_error_t *
get_node_revision_body(svn_fs_x__noderev_t **noderev_p,
                       svn_fs_t *fs,
                       const svn_fs_x__id_t *id,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t is_cached = FALSE;
  svn_fs_x__data_t *ffd = fs->fsap_data;

  if (svn_fs_x__is_txn(id->change_set))
    {
      apr_file_t *file;
      svn_stream_t *stream;

      /* This is a transaction node-rev.  Its storage logic is very
         different from that of rev / pack files. */
      err = svn_io_file_open(&file,
                             svn_fs_x__path_txn_node_rev(fs, id,
                                                         scratch_pool,
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

      /* Be sure to close the file ASAP. */
      stream = svn_stream_from_aprfile2(file, FALSE, scratch_pool);
      SVN_ERR(svn_fs_x__read_noderev(noderev_p, stream,
                                     result_pool, scratch_pool));
    }
  else
    {
      svn_fs_x__revision_file_t *revision_file;

      /* noderevs in rev / pack files can be cached */
      svn_revnum_t revision = svn_fs_x__get_revnum(id->change_set);
      svn_fs_x__pair_cache_key_t key;

      SVN_ERR(svn_fs_x__rev_file_init(&revision_file, fs, revision,
                                      scratch_pool));

      /* First, try a noderevs container cache lookup. */
      if (   svn_fs_x__is_packed_rev(fs, revision)
          && ffd->noderevs_container_cache)
        {
          apr_off_t offset;
          apr_uint32_t sub_item;
          SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, revision_file,
                                        id, scratch_pool));
          key.revision = svn_fs_x__packed_base_rev(fs, revision);
          key.second = offset;

          SVN_ERR(svn_cache__get_partial((void **)noderev_p, &is_cached,
                                         ffd->noderevs_container_cache, &key,
                                         svn_fs_x__noderevs_get_func,
                                         &sub_item, result_pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }

      key.revision = revision;
      key.second = id->number;

      /* Not found or not applicable. Try a noderev cache lookup.
       * If that succeeds, we are done here. */
      SVN_ERR(svn_cache__get((void **) noderev_p,
                             &is_cached,
                             ffd->node_revision_cache,
                             &key,
                             result_pool));
      if (is_cached)
        return SVN_NO_ERROR;

      /* block-read will parse the whole block and will also return
         the one noderev that we need right now. */
      SVN_ERR(block_read((void **)noderev_p, fs,
                         id,
                         revision_file,
                         NULL,
                         result_pool,
                         scratch_pool));
      SVN_ERR(svn_fs_x__close_revision_file(revision_file));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_node_revision(svn_fs_x__noderev_t **noderev_p,
                            svn_fs_t *fs,
                            const svn_fs_x__id_t *id,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err = get_node_revision_body(noderev_p, fs, id,
                                            result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      svn_string_t *id_string = svn_fs_x__id_unparse(id, scratch_pool);
      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt node-revision '%s'",
                               id_string->data);
    }

  SVN_ERR(dbg__log_access(fs, id, *noderev_p,
                          SVN_FS_X__ITEM_TYPE_NODEREV, scratch_pool));

  return svn_error_trace(err);
}


svn_error_t *
svn_fs_x__get_mergeinfo_count(apr_int64_t *count,
                              svn_fs_t *fs,
                              const svn_fs_x__id_t *id,
                              apr_pool_t *scratch_pool)
{
  svn_fs_x__noderev_t *noderev;

  /* If we want a full acccess log, we need to provide full data and
     cannot take shortcuts here. */
#if !defined(SVN_FS_X__LOG_ACCESS)

  /* First, try a noderevs container cache lookup. */
  if (! svn_fs_x__is_txn(id->change_set))
    {
      /* noderevs in rev / pack files can be cached */
      svn_fs_x__data_t *ffd = fs->fsap_data;
      svn_revnum_t revision = svn_fs_x__get_revnum(id->change_set);

      svn_fs_x__revision_file_t *rev_file;
      SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, revision,
                                      scratch_pool));

      if (   svn_fs_x__is_packed_rev(fs, revision)
          && ffd->noderevs_container_cache)
        {
          svn_fs_x__pair_cache_key_t key;
          apr_off_t offset;
          apr_uint32_t sub_item;
          svn_boolean_t is_cached;

          SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, rev_file,
                                        id, scratch_pool));
          key.revision = svn_fs_x__packed_base_rev(fs, revision);
          key.second = offset;

          SVN_ERR(svn_cache__get_partial((void **)count, &is_cached,
                                         ffd->noderevs_container_cache, &key,
                                         svn_fs_x__mergeinfo_count_get_func,
                                         &sub_item, scratch_pool));
          if (is_cached)
            return SVN_NO_ERROR;
        }
    }
#endif

  /* fallback to the naive implementation handling all edge cases */
  SVN_ERR(svn_fs_x__get_node_revision(&noderev, fs, id, scratch_pool,
                                      scratch_pool));
  *count = noderev->mergeinfo_count;

  return SVN_NO_ERROR;
}

/* Describes a lazily opened rev / pack file.  Instances will be shared
   between multiple instances of rep_state_t. */
typedef struct shared_file_t
{
  /* The opened file. NULL while file is not open, yet. */
  svn_fs_x__revision_file_t *rfile;

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
  svn_cache__t *window_cache;
                    /* Caches un-deltified windows. May be NULL. */
  svn_cache__t *combined_cache;
                    /* ID addressing the representation */
  svn_fs_x__id_t rep_id;
                    /* length of the header at the start of the rep.
                       0 iff this is rep is stored in a container
                       (i.e. does not have a header) */
  apr_size_t header_size;
  apr_off_t start;  /* The starting offset for the raw
                       svndiff data minus header.
                       -1 if the offset is yet unknown. */
                    /* sub-item index in case the rep is containered */
  apr_uint32_t sub_item;
  apr_off_t current;/* The current offset relative to START. */
  apr_off_t size;   /* The on-disk size of the representation. */
  int ver;          /* If a delta, what svndiff version?
                       -1 for unknown delta version. */
  int chunk_index;  /* number of the window to read */
} rep_state_t;

/* Open FILE->FILE and FILE->STREAM if they haven't been opened, yet. */
static svn_error_t*
auto_open_shared_file(shared_file_t *file)
{
  if (file->rfile == NULL)
    SVN_ERR(svn_fs_x__rev_file_init(&file->rfile, file->fs,
                                    file->revision, file->pool));

  return SVN_NO_ERROR;
}

/* Set RS->START to the begin of the representation raw in RS->SFILE->RFILE,
   if that hasn't been done yet.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t*
auto_set_start_offset(rep_state_t *rs,
                      apr_pool_t *scratch_pool)
{
  if (rs->start == -1)
    {
      SVN_ERR(svn_fs_x__item_offset(&rs->start, &rs->sub_item,
                                    rs->sfile->fs, rs->sfile->rfile,
                                    &rs->rep_id, scratch_pool));
      rs->start += rs->header_size;
    }

  return SVN_NO_ERROR;
}

/* Set RS->VER depending on what is found in the already open RS->FILE->FILE
   if the diff version is still unknown.  Use SCRATCH_POOL for temporary
   allocations.
 */
static svn_error_t*
auto_read_diff_version(rep_state_t *rs,
                       apr_pool_t *scratch_pool)
{
  if (rs->ver == -1)
    {
      char buf[4];
      SVN_ERR(svn_fs_x__rev_file_seek(rs->sfile->rfile, NULL, rs->start));
      SVN_ERR(svn_fs_x__rev_file_read(rs->sfile->rfile, buf, sizeof(buf)));

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
                      svn_fs_x__rep_header_t **rep_header,
                      shared_file_t **shared_file,
                      svn_fs_x__representation_t *rep,
                      svn_fs_t *fs,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  rep_state_t *rs = apr_pcalloc(result_pool, sizeof(*rs));
  svn_fs_x__rep_header_t *rh;
  svn_boolean_t is_cached = FALSE;
  svn_revnum_t revision = svn_fs_x__get_revnum(rep->id.change_set);
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
      && revision < ffd->min_unpacked_rev
      && (   ((*shared_file)->revision / ffd->max_files_per_dir)
          == (revision / ffd->max_files_per_dir));

  svn_fs_x__representation_cache_key_t key = { 0 };
  key.revision = revision;
  key.is_packed = revision < ffd->min_unpacked_rev;
  key.item_index = rep->id.number;

  /* continue constructing RS and RA */
  rs->size = rep->size;
  rs->rep_id = rep->id;
  rs->ver = -1;
  rs->start = -1;

  /* Very long files stored as self-delta will produce a huge number of
     delta windows.  Don't cache them lest we don't thrash the cache.
     Since we don't know the depth of the delta chain, let's assume, the
     whole contents get rewritten 3 times.
   */
  estimated_window_storage
    = 4 * (  (rep->expanded_size ? rep->expanded_size : rep->size)
           + SVN_DELTA_WINDOW_SIZE);
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
  if (SVN_IS_VALID_REVNUM(revision))
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
      file->revision = revision;
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
      svn_stream_t *stream;

      /* we will need the on-disk location for non-txn reps */
      apr_off_t offset;
      svn_boolean_t in_container = TRUE;

      /* ensure file is open and navigate to the start of rep header */
      if (reuse_shared_file)
        {
          /* ... we can re-use the same, already open file object.
           * This implies that we don't read from a txn.
           */
          rs->sfile = *shared_file;
          SVN_ERR(auto_open_shared_file(rs->sfile));
        }
      else
        {
          /* otherwise, create a new file object.  May or may not be
           * an in-txn file.
           */
          SVN_ERR(open_and_seek_representation(&rs->sfile->rfile, fs, rep,
                                               result_pool, scratch_pool));
        }

      if (SVN_IS_VALID_REVNUM(revision))
        {
          apr_uint32_t sub_item;

          SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs,
                                        rs->sfile->rfile, &rep->id,
                                        scratch_pool));

          /* is rep stored in some star-deltified container? */
          if (sub_item == 0)
            {
              svn_fs_x__p2l_entry_t *entry;
              SVN_ERR(svn_fs_x__p2l_entry_lookup(&entry, fs, rs->sfile->rfile,
                                                 revision, offset,
                                                 scratch_pool, scratch_pool));
              in_container = entry->type == SVN_FS_X__ITEM_TYPE_REPS_CONT;
            }

          if (in_container)
            {
              /* construct a container rep header */
              *rep_header = apr_pcalloc(result_pool, sizeof(**rep_header));
              (*rep_header)->type = svn_fs_x__rep_container;

              /* exit to caller */
              *rep_state = rs;
              return SVN_NO_ERROR;
            }

          SVN_ERR(svn_fs_x__rev_file_seek(rs->sfile->rfile, NULL, offset));
        }

      SVN_ERR(svn_fs_x__rev_file_stream(&stream, rs->sfile->rfile));
      SVN_ERR(svn_fs_x__read_rep_header(&rh, stream,
                                        result_pool, scratch_pool));
      SVN_ERR(svn_fs_x__rev_file_offset(&rs->start, rs->sfile->rfile));

      /* populate the cache if appropriate */
      if (SVN_IS_VALID_REVNUM(revision))
        {
          SVN_ERR(block_read(NULL, fs, &rs->rep_id, rs->sfile->rfile, NULL,
                             result_pool, scratch_pool));
          SVN_ERR(svn_cache__set(ffd->rep_header_cache, &key, rh,
                                 scratch_pool));
        }
    }

  /* finalize */
  SVN_ERR(dbg__log_access(fs, &rs->rep_id, rh, SVN_FS_X__ITEM_TYPE_ANY_REP,
                          scratch_pool));

  rs->header_size = rh->header_size;
  *rep_state = rs;
  *rep_header = rh;

  rs->chunk_index = 0;

  /* skip "SVNx" diff marker */
  rs->current = 4;

  return SVN_NO_ERROR;
}

/* Read the rep args for REP in filesystem FS and create a rep_state
   for reading the representation.  Return the rep_state in *REP_STATE
   and the rep args in *REP_ARGS, both allocated in POOL.

   When reading multiple reps, i.e. a skip delta chain, you may provide
   non-NULL SHARED_FILE.  (If SHARED_FILE is not NULL, in the first
   call it should be a pointer to NULL.)  The function will use this
   variable to store the previous call results and tries to re-use it.
   This may result in significant savings in I/O for packed files and
   number of open file handles.
 */
static svn_error_t *
create_rep_state(rep_state_t **rep_state,
                 svn_fs_x__rep_header_t **rep_header,
                 shared_file_t **shared_file,
                 svn_fs_x__representation_t *rep,
                 svn_fs_t *fs,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err = create_rep_state_body(rep_state, rep_header,
                                           shared_file, rep, fs,
                                           result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_CORRUPT)
    {
      /* ### This always returns "-1" for transaction reps, because
         ### this particular bit of code doesn't know if the rep is
         ### stored in the protorev or in the mutable area (for props
         ### or dir contents).  It is pretty rare for FSX to *read*
         ### from the protorev file, though, so this is probably OK.
         ### And anyone going to debug corruption errors is probably
         ### going to jump straight to this comment anyway! */
      return svn_error_createf(SVN_ERR_FS_CORRUPT, err,
                               "Corrupt representation '%s'",
                               rep
                               ? svn_fs_x__unparse_representation
                                   (rep, TRUE, scratch_pool,
                                    scratch_pool)->data
                               : "(null)");
    }
  /* ### Call representation_string() ? */
  return svn_error_trace(err);
}

svn_error_t *
svn_fs_x__check_rep(svn_fs_x__representation_t *rep,
                    svn_fs_t *fs,
                    apr_pool_t *scratch_pool)
{
  apr_off_t offset;
  apr_uint32_t sub_item;
  svn_fs_x__p2l_entry_t *entry;
  svn_revnum_t revision = svn_fs_x__get_revnum(rep->id.change_set);

  svn_fs_x__revision_file_t *rev_file;
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, fs, revision, scratch_pool));

  /* Does REP->ID refer to an actual item? Which one is it? */
  SVN_ERR(svn_fs_x__item_offset(&offset, &sub_item, fs, rev_file, &rep->id,
                                scratch_pool));

  /* What is the type of that item? */
  SVN_ERR(svn_fs_x__p2l_entry_lookup(&entry, fs, rev_file, revision, offset,
                                     scratch_pool, scratch_pool));

  /* Verify that we've got an item that is actually a representation. */
  if (   entry == NULL
      || (   entry->type != SVN_FS_X__ITEM_TYPE_FILE_REP
          && entry->type != SVN_FS_X__ITEM_TYPE_DIR_REP
          && entry->type != SVN_FS_X__ITEM_TYPE_FILE_PROPS
          && entry->type != SVN_FS_X__ITEM_TYPE_DIR_PROPS
          && entry->type != SVN_FS_X__ITEM_TYPE_REPS_CONT))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("No representation found at offset %s "
                               "for item %s in revision %ld"),
                             apr_off_t_toa(scratch_pool, offset),
                             apr_psprintf(scratch_pool, "%" APR_UINT64_T_FMT,
                                          rep->id.number),
                             revision);

  return SVN_NO_ERROR;
}

/* .
   Do any allocations in POOL. */
svn_error_t *
svn_fs_x__rep_chain_length(int *chain_length,
                           int *shard_count,
                           svn_fs_x__representation_t *rep,
                           svn_fs_t *fs,
                           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_revnum_t shard_size = ffd->max_files_per_dir;
  svn_boolean_t is_delta = FALSE;
  int count = 0;
  int shards = 1;
  svn_revnum_t revision = svn_fs_x__get_revnum(rep->id.change_set);
  svn_revnum_t last_shard = revision / shard_size;

  /* Note that this iteration pool will be used in a non-standard way.
   * To reuse open file handles between iterations (e.g. while within the
   * same pack file), we only clear this pool once in a while instead of
   * at the start of each iteration. */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check whether the length of the deltification chain is acceptable.
   * Otherwise, shared reps may form a non-skipping delta chain in
   * extreme cases. */
  svn_fs_x__representation_t base_rep = *rep;

  /* re-use open files between iterations */
  shared_file_t *file_hint = NULL;

  svn_fs_x__rep_header_t *header;

  /* follow the delta chain towards the end but for at most
   * MAX_CHAIN_LENGTH steps. */
  do
    {
      rep_state_t *rep_state;
      revision = svn_fs_x__get_revnum(base_rep.id.change_set);
      if (revision / shard_size != last_shard)
        {
          last_shard = revision / shard_size;
          ++shards;
        }

      SVN_ERR(create_rep_state_body(&rep_state,
                                    &header,
                                    &file_hint,
                                    &base_rep,
                                    fs,
                                    iterpool,
                                    iterpool));

      base_rep.id.change_set
        = svn_fs_x__change_set_by_rev(header->base_revision);
      base_rep.id.number = header->base_item_index;
      base_rep.size = header->base_length;
      is_delta = header->type == svn_fs_x__rep_delta;

      /* Clear it the ITERPOOL once in a while.  Doing it too frequently
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
          svn_pool_clear(iterpool);
        }
    }
  while (is_delta && base_rep.id.change_set);

  *chain_length = count;
  *shard_count = shards;
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


typedef struct rep_read_baton_t
{
  /* The FS from which we're reading. */
  svn_fs_t *fs;

  /* Representation to read. */
  svn_fs_x__representation_t rep;

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
  svn_fs_x__pair_cache_key_t fulltext_cache_key;
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
  apr_pool_t *scratch_pool;

  /* Pool used to store file handles and other data that is persistant
     for the entire stream read. */
  apr_pool_t *filehandle_pool;
} rep_read_baton_t;

/* Set window key in *KEY to address the window described by RS.
   For convenience, return the KEY. */
static svn_fs_x__window_cache_key_t *
get_window_key(svn_fs_x__window_cache_key_t *key,
               rep_state_t *rs)
{
  svn_revnum_t revision = svn_fs_x__get_revnum(rs->rep_id.change_set);
  assert(revision <= APR_UINT32_MAX);

  key->revision = (apr_uint32_t)revision;
  key->item_index = rs->rep_id.number;
  key->chunk_index = rs->chunk_index;

  return key;
}

/* Read the WINDOW_P number CHUNK_INDEX for the representation given in
 * rep state RS from the current FSX session's cache.  This will be a
 * no-op and IS_CACHED will be set to FALSE if no cache has been given.
 * If a cache is available IS_CACHED will inform the caller about the
 * success of the lookup. Allocations (of the window in particualar) will
 * be made from POOL.
 *
 * If the information could be found, put RS to CHUNK_INDEX.
 */

/* Return data type for get_cached_window_sizes_func.
 */
typedef struct window_sizes_t
{
  /* length of the txdelta window in its on-disk format */
  svn_filesize_t packed_len;

  /* expanded (and combined) window length */
  svn_filesize_t target_len;
} window_sizes_t;

/* Implements svn_cache__partial_getter_func_t extracting the packed
 * and expanded window sizes from a cached window and return the size
 * info as a window_sizes_t* in *OUT.
 */
static svn_error_t *
get_cached_window_sizes_func(void **out,
                             const void *data,
                             apr_size_t data_len,
                             void *baton,
                             apr_pool_t *pool)
{
  const svn_fs_x__txdelta_cached_window_t *window = data;
  const svn_txdelta_window_t *txdelta_window
    = svn_temp_deserializer__ptr(window, (const void **)&window->window);

  window_sizes_t *result = apr_palloc(pool, sizeof(*result));
  result->packed_len = window->end_offset - window->start_offset;
  result->target_len = txdelta_window->tview_len;

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
get_cached_window_sizes(window_sizes_t **sizes,
                        rep_state_t *rs,
                        svn_boolean_t *is_cached,
                        apr_pool_t *pool)
{
  svn_fs_x__window_cache_key_t key = { 0 };
  SVN_ERR(svn_cache__get_partial((void **)sizes,
                                 is_cached,
                                 rs->window_cache,
                                 get_window_key(&key, rs),
                                 get_cached_window_sizes_func,
                                 NULL,
                                 pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
get_cached_window(svn_txdelta_window_t **window_p,
                  rep_state_t *rs,
                  int chunk_index,
                  svn_boolean_t *is_cached,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  /* ask the cache for the desired txdelta window */
  svn_fs_x__txdelta_cached_window_t *cached_window;
  svn_fs_x__window_cache_key_t key = { 0 };
  get_window_key(&key, rs);
  key.chunk_index = chunk_index;
  SVN_ERR(svn_cache__get((void **) &cached_window,
                         is_cached,
                         rs->window_cache,
                         &key,
                         result_pool));

  if (*is_cached)
    {
      /* found it. Pass it back to the caller. */
      *window_p = cached_window->window;

      /* manipulate the RS as if we just read the data */
      rs->current = cached_window->end_offset;
      rs->chunk_index = chunk_index;
    }

  return SVN_NO_ERROR;
}

/* Store the WINDOW read for the rep state RS with the given START_OFFSET
 * within the pack / rev file in the current FSX session's cache.  This
 * will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_window(svn_txdelta_window_t *window,
                  rep_state_t *rs,
                  apr_off_t start_offset,
                  apr_pool_t *scratch_pool)
{
  /* store the window and the first offset _past_ it */
  svn_fs_x__txdelta_cached_window_t cached_window;
  svn_fs_x__window_cache_key_t key = {0};

  cached_window.window = window;
  cached_window.start_offset = start_offset - rs->start;
  cached_window.end_offset = rs->current;

  /* but key it with the start offset because that is the known state
   * when we will look it up */
  SVN_ERR(svn_cache__set(rs->window_cache,
                         get_window_key(&key, rs),
                         &cached_window,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the WINDOW_P for the rep state RS from the current FSX session's
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
  /* ask the cache for the desired txdelta window */
  svn_fs_x__window_cache_key_t key = { 0 };
  return svn_cache__get((void **)window_p,
                        is_cached,
                        rs->combined_cache,
                        get_window_key(&key, rs),
                        pool);
}

/* Store the WINDOW read for the rep state RS in the current FSX session's
 * cache. This will be a no-op if no cache has been given.
 * Temporary allocations will be made from SCRATCH_POOL. */
static svn_error_t *
set_cached_combined_window(svn_stringbuf_t *window,
                           rep_state_t *rs,
                           apr_pool_t *scratch_pool)
{
  /* but key it with the start offset because that is the known state
   * when we will look it up */
  svn_fs_x__window_cache_key_t key = { 0 };
  return svn_cache__set(rs->combined_cache,
                        get_window_key(&key, rs),
                        window,
                        scratch_pool);
}

/* Build an array of rep_state structures in *LIST giving the delta
   reps from first_rep to a  self-compressed rep.  Set *SRC_STATE to
   the container rep we find at the end of the chain, or to NULL if
   the final delta representation is self-compressed.
   The representation to start from is designated by filesystem FS, id
   ID, and representation REP.
   Also, set *WINDOW_P to the base window content for *LIST, if it
   could be found in cache. Otherwise, *LIST will contain the base
   representation for the whole delta chain.
 */
static svn_error_t *
build_rep_list(apr_array_header_t **list,
               svn_stringbuf_t **window_p,
               rep_state_t **src_state,
               svn_fs_t *fs,
               svn_fs_x__representation_t *first_rep,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_t rep;
  rep_state_t *rs = NULL;
  svn_fs_x__rep_header_t *rep_header;
  svn_boolean_t is_cached = FALSE;
  shared_file_t *shared_file = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *list = apr_array_make(result_pool, 1, sizeof(rep_state_t *));
  rep = *first_rep;

  /* for the top-level rep, we need the rep_args */
  SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file, &rep, fs,
                           result_pool, iterpool));

  while (1)
    {
      svn_pool_clear(iterpool);

      /* fetch state, if that has not been done already */
      if (!rs)
        SVN_ERR(create_rep_state(&rs, &rep_header, &shared_file,
                                 &rep, fs, result_pool, iterpool));

      /* for txn reps and containered reps, there won't be a cached
       * combined window */
      if (svn_fs_x__is_revision(rep.id.change_set)
          && rep_header->type != svn_fs_x__rep_container
          && rs->combined_cache)
        SVN_ERR(get_cached_combined_window(window_p, rs, &is_cached,
                                           result_pool));

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

      if (rep_header->type == svn_fs_x__rep_container)
        {
          /* This is a container item, so just return the current rep_state. */
          *src_state = rs;
          break;
        }

      /* Push this rep onto the list.  If it's self-compressed, we're done. */
      APR_ARRAY_PUSH(*list, rep_state_t *) = rs;
      if (rep_header->type == svn_fs_x__rep_self_delta)
        {
          *src_state = NULL;
          break;
        }

      rep.id.change_set
        = svn_fs_x__change_set_by_rev(rep_header->base_revision);
      rep.id.number = rep_header->base_item_index;
      rep.size = rep_header->base_length;

      rs = NULL;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Create a rep_read_baton structure for node revision NODEREV in
   filesystem FS and store it in *RB_P.  If FULLTEXT_CACHE_KEY is not
   NULL, it is the rep's key in the fulltext cache, and a stringbuf
   must be allocated to store the text.  If rep is mutable, it must be
   refer to file contents.

   Allocate the result in RESULT_POOL.  This includes the pools within *RB_P.
 */
static svn_error_t *
rep_read_get_baton(rep_read_baton_t **rb_p,
                   svn_fs_t *fs,
                   svn_fs_x__representation_t *rep,
                   svn_fs_x__pair_cache_key_t fulltext_cache_key,
                   apr_pool_t *result_pool)
{
  rep_read_baton_t *b;

  b = apr_pcalloc(result_pool, sizeof(*b));
  b->fs = fs;
  b->rep = *rep;
  b->base_window = NULL;
  b->chunk_index = 0;
  b->buf = NULL;
  b->md5_checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5,
                                                result_pool);
  b->checksum_finalized = FALSE;
  memcpy(b->md5_digest, rep->md5_digest, sizeof(rep->md5_digest));
  b->len = rep->expanded_size;
  b->off = 0;
  b->fulltext_cache_key = fulltext_cache_key;

  /* Clearable sub-pools.  Since they have to remain valid for as long as B
     lives, we can't take them from some scratch pool.  The caller of this
     function will have no control over how those subpools will be used. */
  b->scratch_pool = svn_pool_create(result_pool);
  b->filehandle_pool = svn_pool_create(result_pool);
  b->fulltext_cache = NULL;
  b->fulltext_delivered = 0;
  b->current_fulltext = NULL;

  /* Save our output baton. */
  *rb_p = b;

  return SVN_NO_ERROR;
}

/* Skip forwards to THIS_CHUNK in REP_STATE and then read the next delta
   window into *NWIN. */
static svn_error_t *
read_delta_window(svn_txdelta_window_t **nwin, int this_chunk,
                  rep_state_t *rs, apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_boolean_t is_cached;
  apr_off_t start_offset;
  apr_off_t end_offset;
  apr_pool_t *iterpool;
  svn_stream_t *stream;
  svn_fs_x__revision_file_t *file;
  svn_boolean_t cacheable = rs->chunk_index == 0
                         && svn_fs_x__is_revision(rs->rep_id.change_set)
                         && rs->window_cache;

  SVN_ERR_ASSERT(rs->chunk_index <= this_chunk);

  SVN_ERR(dbg__log_access(rs->sfile->fs, &rs->rep_id, NULL,
                          SVN_FS_X__ITEM_TYPE_ANY_REP, scratch_pool));

  /* Read the next window.  But first, try to find it in the cache. */
  if (cacheable)
    {
      SVN_ERR(get_cached_window(nwin, rs, this_chunk, &is_cached,
                                result_pool, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* someone has to actually read the data from file.  Open it */
  SVN_ERR(auto_open_shared_file(rs->sfile));
  file = rs->sfile->rfile;

  /* invoke the 'block-read' feature for non-txn data.
     However, don't do that if we are in the middle of some representation,
     because the block is unlikely to contain other data. */
  if (cacheable)
    {
      SVN_ERR(block_read(NULL, rs->sfile->fs, &rs->rep_id, file, NULL,
                         result_pool, scratch_pool));

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
  SVN_ERR(svn_fs_x__rev_file_seek(file, NULL, start_offset));

  /* Skip windows to reach the current chunk if we aren't there yet. */
  iterpool = svn_pool_create(scratch_pool);
  while (rs->chunk_index < this_chunk)
    {
      apr_file_t *apr_file;
      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_x__rev_file_get(&apr_file, file));
      SVN_ERR(svn_txdelta_skip_svndiff_window(apr_file, rs->ver, iterpool));
      rs->chunk_index++;
      SVN_ERR(svn_io_file_get_offset(&start_offset, apr_file, iterpool));

      rs->current = start_offset - rs->start;
      if (rs->current >= rs->size)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Reading one svndiff window read "
                                  "beyond the end of the "
                                  "representation"));
    }
  svn_pool_destroy(iterpool);

  /* Actually read the next window. */
  SVN_ERR(svn_fs_x__rev_file_stream(&stream, file));
  SVN_ERR(svn_txdelta_read_svndiff_window(nwin, stream, rs->ver,
                                          result_pool));
  SVN_ERR(svn_fs_x__rev_file_offset(&end_offset, file));
  rs->current = end_offset - rs->start;
  if (rs->current > rs->size)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Reading one svndiff window read beyond "
                              "the end of the representation"));

  /* the window has not been cached before, thus cache it now
   * (if caching is used for them at all) */
  if (cacheable)
    SVN_ERR(set_cached_window(*nwin, rs, start_offset, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the whole representation RS and return it in *NWIN. */
static svn_error_t *
read_container_window(svn_stringbuf_t **nwin,
                      rep_state_t *rs,
                      apr_size_t size,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_fs_x__rep_extractor_t *extractor = NULL;
  svn_fs_t *fs = rs->sfile->fs;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__pair_cache_key_t key;
  svn_revnum_t revision = svn_fs_x__get_revnum(rs->rep_id.change_set);
  svn_boolean_t is_cached = FALSE;
  svn_fs_x__reps_baton_t baton;

  SVN_ERR(auto_set_start_offset(rs, scratch_pool));
  key.revision = svn_fs_x__packed_base_rev(fs, revision);
  key.second = rs->start;

  /* already in cache? */
  baton.fs = fs;
  baton.idx = rs->sub_item;

  SVN_ERR(svn_cache__get_partial((void**)&extractor, &is_cached,
                                 ffd->reps_container_cache, &key,
                                 svn_fs_x__reps_get_func, &baton,
                                 result_pool));

  /* read from disk, if necessary */
  if (extractor == NULL)
    {
      SVN_ERR(auto_open_shared_file(rs->sfile));
      SVN_ERR(block_read((void **)&extractor, fs, &rs->rep_id,
                         rs->sfile->rfile, NULL,
                         result_pool, scratch_pool));
    }

  SVN_ERR(svn_fs_x__extractor_drive(nwin, extractor, rs->current, size,
                                    result_pool, scratch_pool));

  /* Update RS. */
  rs->current += (apr_off_t)size;

  return SVN_NO_ERROR;
}

/* Get the undeltified window that is a result of combining all deltas
   from the current desired representation identified in *RB with its
   base representation.  Store the window in *RESULT. */
static svn_error_t *
get_combined_window(svn_stringbuf_t **result,
                    rep_read_baton_t *rb)
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
  window_pool = svn_pool_create(rb->scratch_pool);
  windows = apr_array_make(window_pool, 0, sizeof(svn_txdelta_window_t *));
  iterpool = svn_pool_create(rb->scratch_pool);
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
  pool = svn_pool_create(rb->scratch_pool);
  for (--i; i >= 0; --i)
    {
      svn_txdelta_window_t *window;

      svn_pool_clear(iterpool);

      rs = APR_ARRAY_IDX(rb->rs_list, i, rep_state_t *);
      window = APR_ARRAY_IDX(windows, i, svn_txdelta_window_t *);

      /* Maybe, we've got a start representation in a container.  If we do,
         read as much data from it as the needed for the txdelta window's
         source view.
         Note that BUF / SOURCE may only be NULL in the first iteration. */
      source = buf;
      if (source == NULL && rb->src_state != NULL)
        SVN_ERR(read_container_window(&source, rb->src_state,
                                      window->sview_len, pool, iterpool));

      /* Combine this window with the current one. */
      new_pool = svn_pool_create(rb->scratch_pool);
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
          && svn_fs_x__is_revision(rs->rep_id.change_set)
          && rs->combined_cache)
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
fulltext_size_is_cachable(svn_fs_x__data_t *ffd,
                          svn_filesize_t size)
{
  return (size < APR_SIZE_MAX)
      && svn_cache__is_cachable(ffd->fulltext_cache, (apr_size_t)size);
}

/* Close method used on streams returned by read_representation().
 */
static svn_error_t *
rep_read_contents_close(void *baton)
{
  rep_read_baton_t *rb = baton;

  svn_pool_destroy(rb->scratch_pool);
  svn_pool_destroy(rb->filehandle_pool);

  return SVN_NO_ERROR;
}

/* Inialize the representation read state RS for the given REP_HEADER and
 * p2l index ENTRY.  If not NULL, assign FILE and STREAM to RS.
 * Allocate all sub-structures of RS in RESULT_POOL.
 */
static svn_error_t *
init_rep_state(rep_state_t *rs,
               svn_fs_x__rep_header_t *rep_header,
               svn_fs_t *fs,
               svn_fs_x__revision_file_t *rev_file,
               svn_fs_x__p2l_entry_t* entry,
               apr_pool_t *result_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  shared_file_t *shared_file = apr_pcalloc(result_pool, sizeof(*shared_file));

  /* this function does not apply to representation containers */
  SVN_ERR_ASSERT(entry->type >= SVN_FS_X__ITEM_TYPE_FILE_REP
                 && entry->type <= SVN_FS_X__ITEM_TYPE_DIR_PROPS);
  SVN_ERR_ASSERT(entry->item_count == 1);

  shared_file->rfile = rev_file;
  shared_file->fs = fs;
  shared_file->revision = svn_fs_x__get_revnum(entry->items[0].change_set);
  shared_file->pool = result_pool;

  rs->sfile = shared_file;
  rs->rep_id = entry->items[0];
  rs->header_size = rep_header->header_size;
  rs->start = entry->offset + rs->header_size;
  rs->current = 4;
  rs->size = entry->size - rep_header->header_size - 7;
  rs->ver = 1;
  rs->chunk_index = 0;
  rs->window_cache = ffd->txdelta_window_cache;
  rs->combined_cache = ffd->combined_window_cache;

  return SVN_NO_ERROR;
}

/* Walk through all windows in the representation addressed by RS in FS
 * (excluding the delta bases) and put those not already cached into the
 * window caches.  If MAX_OFFSET is not -1, don't read windows that start
 * at or beyond that offset.  As a side effect, return the total sum of all
 * expanded window sizes in *FULLTEXT_LEN.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
cache_windows(svn_filesize_t *fulltext_len,
              svn_fs_t *fs,
              rep_state_t *rs,
              apr_off_t max_offset,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  *fulltext_len = 0;

  while (rs->current < rs->size)
    {
      svn_boolean_t is_cached = FALSE;
      window_sizes_t *window_sizes;

      svn_pool_clear(iterpool);
      if (max_offset != -1 && rs->start + rs->current >= max_offset)
        {
          svn_pool_destroy(iterpool);
          return SVN_NO_ERROR;
        }

      /* efficiently skip windows that are still being cached instead
       * of fully decoding them */
      SVN_ERR(get_cached_window_sizes(&window_sizes, rs, &is_cached,
                                      iterpool));
      if (is_cached)
        {
          *fulltext_len += window_sizes->target_len;
          rs->current += window_sizes->packed_len;
        }
      else
        {
          svn_txdelta_window_t *window;
          svn_fs_x__revision_file_t *file = rs->sfile->rfile;
          svn_stream_t *stream;
          apr_off_t start_offset = rs->start + rs->current;
          apr_off_t end_offset;
          apr_off_t block_start;

          /* navigate to & read the current window */
          SVN_ERR(svn_fs_x__rev_file_stream(&stream, file));
          SVN_ERR(svn_fs_x__rev_file_seek(file, &block_start, start_offset));
          SVN_ERR(svn_txdelta_read_svndiff_window(&window, stream, rs->ver,
                                                  iterpool));

          /* aggregate expanded window size */
          *fulltext_len += window->tview_len;

          /* determine on-disk window size */
          SVN_ERR(svn_fs_x__rev_file_offset(&end_offset, rs->sfile->rfile));
          rs->current = end_offset - rs->start;
          if (rs->current > rs->size)
            return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                          _("Reading one svndiff window read beyond "
                                      "the end of the representation"));

          /* if the window has not been cached before, cache it now
           * (if caching is used for them at all) */
          if (!is_cached)
            SVN_ERR(set_cached_window(window, rs, start_offset, iterpool));
        }

      rs->chunk_index++;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Try to get the representation header identified by KEY from FS's cache.
 * If it has not been cached, read it from the current position in STREAM
 * and put it into the cache (if caching has been enabled for rep headers).
 * Return the result in *REP_HEADER.  Use POOL for allocations.
 */
static svn_error_t *
read_rep_header(svn_fs_x__rep_header_t **rep_header,
                svn_fs_t *fs,
                svn_fs_x__revision_file_t *file,
                svn_fs_x__representation_cache_key_t *key,
                apr_pool_t *pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;
  svn_boolean_t is_cached = FALSE;

  SVN_ERR(svn_cache__get((void**)rep_header, &is_cached,
                         ffd->rep_header_cache, key, pool));
  if (is_cached)
    return SVN_NO_ERROR;

  SVN_ERR(svn_fs_x__rev_file_stream(&stream, file));
  SVN_ERR(svn_fs_x__read_rep_header(rep_header, stream, pool, pool));
  SVN_ERR(svn_cache__set(ffd->rep_header_cache, key, *rep_header, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_representation_length(svn_filesize_t *packed_len,
                                    svn_filesize_t *expanded_len,
                                    svn_fs_t *fs,
                                    svn_fs_x__revision_file_t *rev_file,
                                    svn_fs_x__p2l_entry_t* entry,
                                    apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_cache_key_t key = { 0 };
  rep_state_t rs = { 0 };
  svn_fs_x__rep_header_t *rep_header;

  /* this function does not apply to representation containers */
  SVN_ERR_ASSERT(entry->type >= SVN_FS_X__ITEM_TYPE_FILE_REP
                 && entry->type <= SVN_FS_X__ITEM_TYPE_DIR_PROPS);
  SVN_ERR_ASSERT(entry->item_count == 1);

  /* get / read the representation header */
  key.revision = svn_fs_x__get_revnum(entry->items[0].change_set);
  key.is_packed = svn_fs_x__is_packed_rev(fs, key.revision);
  key.item_index = entry->items[0].number;
  SVN_ERR(read_rep_header(&rep_header, fs, rev_file, &key, scratch_pool));

  /* prepare representation reader state (rs) structure */
  SVN_ERR(init_rep_state(&rs, rep_header, fs, rev_file, entry,
                         scratch_pool));

  /* RS->SFILE may be shared between RS instances -> make sure we point
   * to the right data. */
  *packed_len = rs.size;
  SVN_ERR(cache_windows(expanded_len, fs, &rs, -1, scratch_pool));

  return SVN_NO_ERROR;
}

/* Return the next *LEN bytes of the rep from our plain / delta windows
   and store them in *BUF. */
static svn_error_t *
get_contents_from_windows(rep_read_baton_t *rb,
                          char *buf,
                          apr_size_t *len)
{
  apr_size_t copy_len, remaining = *len;
  char *cur = buf;
  rep_state_t *rs;

  /* Special case for when there are no delta reps, only a
     containered text. */
  if (rb->rs_list->nelts == 0 && rb->buf == NULL)
    {
      copy_len = remaining;
      rs = rb->src_state;

      /* reps in containers don't have a header */
      if (rs->header_size == 0 && rb->base_window == NULL)
        {
          /* RS->SIZE is unreliable here because it is based upon
           * the delta rep size _before_ putting the data into a
           * a container. */
          SVN_ERR(read_container_window(&rb->base_window, rs, rb->len,
                                        rb->scratch_pool, rb->scratch_pool));
          rs->current -= rb->base_window->len;
        }

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
              svn_pool_clear(rb->scratch_pool);
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
                           rep_read_baton_t *baton,
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
                                 baton->scratch_pool));

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
skip_contents(rep_read_baton_t *baton,
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
      apr_pool_t *subpool = svn_pool_create(baton->scratch_pool);
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

/* BATON is of type `rep_read_baton_t'; read the next *LEN bytes of the
   representation and store them in *BUF.  Sum as we read and verify
   the MD5 sum at the end. */
static svn_error_t *
rep_read_contents(void *baton,
                  char *buf,
                  apr_size_t *len)
{
  rep_read_baton_t *rb = baton;

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
      SVN_ERR(build_rep_list(&rb->rs_list, &rb->base_window,
                             &rb->src_state, rb->fs, &rb->rep,
                             rb->filehandle_pool, rb->scratch_pool));

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
                                     rb->scratch_pool));
          if (!svn_checksum_match(md5_checksum, &expected))
            return svn_error_create(SVN_ERR_FS_CORRUPT,
                    svn_checksum_mismatch_err(&expected, md5_checksum,
                        rb->scratch_pool,
                        _("Checksum mismatch while reading representation")),
                    NULL);
        }
    }

  if (rb->off == rb->len && rb->current_fulltext)
    {
      svn_fs_x__data_t *ffd = rb->fs->fsap_data;
      SVN_ERR(svn_cache__set(ffd->fulltext_cache, &rb->fulltext_cache_key,
                             rb->current_fulltext, rb->scratch_pool));
      rb->current_fulltext = NULL;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_contents(svn_stream_t **contents_p,
                       svn_fs_t *fs,
                       svn_fs_x__representation_t *rep,
                       svn_boolean_t cache_fulltext,
                       apr_pool_t *result_pool)
{
  if (! rep)
    {
      *contents_p = svn_stream_empty(result_pool);
    }
  else
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;
      svn_filesize_t len = rep->expanded_size;
      rep_read_baton_t *rb;
      svn_revnum_t revision = svn_fs_x__get_revnum(rep->id.change_set);

      svn_fs_x__pair_cache_key_t fulltext_cache_key = { 0 };
      fulltext_cache_key.revision = revision;
      fulltext_cache_key.second = rep->id.number;

      /* Initialize the reader baton.  Some members may added lazily
       * while reading from the stream */
      SVN_ERR(rep_read_get_baton(&rb, fs, rep, fulltext_cache_key,
                                 result_pool));

      /* Make the stream attempt fulltext cache lookups if the fulltext
       * is cacheable.  If it is not, then also don't try to buffer and
       * cache it. */
      if (   cache_fulltext
          && SVN_IS_VALID_REVNUM(revision)
          && fulltext_size_is_cachable(ffd, len))
        {
          rb->fulltext_cache = ffd->fulltext_cache;
        }
      else
        {
          /* This will also prevent the reconstructed fulltext from being
             put into the cache. */
          rb->fulltext_cache_key.revision = SVN_INVALID_REVNUM;
        }

      *contents_p = svn_stream_create(rb, result_pool);
      svn_stream_set_read2(*contents_p, NULL /* only full read support */,
                           rep_read_contents);
      svn_stream_set_close(*contents_p, rep_read_contents_close);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_contents_from_file(svn_stream_t **contents_p,
                                 svn_fs_t *fs,
                                 svn_fs_x__representation_t *rep,
                                 apr_file_t *file,
                                 apr_off_t offset,
                                 apr_pool_t *pool)
{
  rep_read_baton_t *rb;
  svn_fs_x__pair_cache_key_t fulltext_cache_key = { SVN_INVALID_REVNUM, 0 };
  rep_state_t *rs = apr_pcalloc(pool, sizeof(*rs));
  svn_fs_x__rep_header_t *rh;
  svn_stream_t *stream;

  /* Initialize the reader baton.  Some members may added lazily
   * while reading from the stream. */
  SVN_ERR(rep_read_get_baton(&rb, fs, rep, fulltext_cache_key, pool));

  /* Continue constructing RS. Leave caches as NULL. */
  rs->size = rep->size;
  rs->rep_id = rep->id;
  rs->ver = -1;
  rs->start = -1;

  /* Provide just enough file access info to allow for a basic read from
   * FILE but leave all index / footer info with empty values b/c FILE
   * probably is not a complete revision file. */
  rs->sfile = apr_pcalloc(pool, sizeof(*rs->sfile));
  rs->sfile->revision = SVN_INVALID_REVNUM;
  rs->sfile->pool = pool;
  rs->sfile->fs = fs;
  SVN_ERR(svn_fs_x__rev_file_wrap_temp(&rs->sfile->rfile, fs, file, pool));

  /* Read the rep header. */
  SVN_ERR(svn_fs_x__rev_file_seek(rs->sfile->rfile, NULL, offset));
  SVN_ERR(svn_fs_x__rev_file_stream(&stream, rs->sfile->rfile));
  SVN_ERR(svn_fs_x__read_rep_header(&rh, stream, pool, pool));
  SVN_ERR(svn_fs_x__rev_file_offset(&rs->start, rs->sfile->rfile));
  rs->header_size = rh->header_size;

  /* Log the access. */
  SVN_ERR(dbg__log_access(fs, &rep->id, rh,
                          SVN_FS_X__ITEM_TYPE_ANY_REP, pool));

  /* Build the representation list (delta chain). */
  if (rh->type == svn_fs_x__rep_self_delta)
    {
      rb->rs_list = apr_array_make(pool, 1, sizeof(rep_state_t *));
      APR_ARRAY_PUSH(rb->rs_list, rep_state_t *) = rs;
      rb->src_state = NULL;
    }
  else
    {
      svn_fs_x__representation_t next_rep = { 0 };

      /* skip "SVNx" diff marker */
      rs->current = 4;

      /* REP's base rep is inside a proper revision.
       * It can be reconstructed in the usual way.  */
      next_rep.id.change_set = svn_fs_x__change_set_by_rev(rh->base_revision);
      next_rep.id.number = rh->base_item_index;
      next_rep.size = rh->base_length;

      SVN_ERR(build_rep_list(&rb->rs_list, &rb->base_window,
                             &rb->src_state, rb->fs, &next_rep,
                             rb->filehandle_pool, rb->scratch_pool));

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
 * svn_fs_x__try_process_file_content().
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
svn_fs_x__try_process_file_contents(svn_boolean_t *success,
                                    svn_fs_t *fs,
                                    svn_fs_x__noderev_t *noderev,
                                    svn_fs_process_contents_func_t processor,
                                    void* baton,
                                    apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_t *rep = noderev->data_rep;
  if (rep)
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;
      svn_fs_x__pair_cache_key_t fulltext_cache_key = { 0 };

      fulltext_cache_key.revision = svn_fs_x__get_revnum(rep->id.change_set);
      fulltext_cache_key.second = rep->id.number;
      if (   SVN_IS_VALID_REVNUM(fulltext_cache_key.revision)
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
                                        scratch_pool);
        }
    }

  *success = FALSE;
  return SVN_NO_ERROR;
}

/* Baton used when reading delta windows. */
typedef struct delta_read_baton_t
{
  struct rep_state_t *rs;
  unsigned char md5_digest[APR_MD5_DIGESTSIZE];
} delta_read_baton_t;

/* This implements the svn_txdelta_next_window_fn_t interface. */
static svn_error_t *
delta_read_next_window(svn_txdelta_window_t **window,
                       void *baton,
                       apr_pool_t *pool)
{
  delta_read_baton_t *drb = baton;
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
  delta_read_baton_t *drb = baton;
  return drb->md5_digest;
}

/* Return a txdelta stream for on-disk representation REP_STATE
 * of TARGET.  Allocate the result in RESULT_POOL.
 */
static svn_txdelta_stream_t *
get_storaged_delta_stream(rep_state_t *rep_state,
                          svn_fs_x__noderev_t *target,
                          apr_pool_t *result_pool)
{
  /* Create the delta read baton. */
  delta_read_baton_t *drb = apr_pcalloc(result_pool, sizeof(*drb));
  drb->rs = rep_state;
  memcpy(drb->md5_digest, target->data_rep->md5_digest,
         sizeof(drb->md5_digest));
  return svn_txdelta_stream_create(drb, delta_read_next_window,
                                   delta_read_md5_digest, result_pool);
}

svn_error_t *
svn_fs_x__get_file_delta_stream(svn_txdelta_stream_t **stream_p,
                                svn_fs_t *fs,
                                svn_fs_x__noderev_t *source,
                                svn_fs_x__noderev_t *target,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_stream_t *source_stream, *target_stream;
  rep_state_t *rep_state;
  svn_fs_x__rep_header_t *rep_header;

  /* Try a shortcut: if the target is stored as a delta against the source,
     then just use that delta.  However, prefer using the fulltext cache
     whenever that is available. */
  if (target->data_rep && source)
    {
      /* Read target's base rep if any. */
      SVN_ERR(create_rep_state(&rep_state, &rep_header, NULL,
                               target->data_rep, fs, result_pool,
                               scratch_pool));

      /* Try a shortcut: if the target is stored as a delta against the source,
         then just use that delta. */
      if (source && source->data_rep && target->data_rep)
        {
          /* If that matches source, then use this delta as is.
             Note that we want an actual delta here.  E.g. a self-delta would
             not be good enough. */
          if (rep_header->type == svn_fs_x__rep_delta
              && rep_header->base_revision
                 == svn_fs_x__get_revnum(source->data_rep->id.change_set)
              && rep_header->base_item_index == source->data_rep->id.number)
            {
              *stream_p = get_storaged_delta_stream(rep_state, target,
                                                    result_pool);
              return SVN_NO_ERROR;
            }
        }
      else if (!source)
        {
          /* We want a self-delta. There is a fair chance that TARGET got
             added in this revision and is already stored in the requested
             format. */
          if (rep_header->type == svn_fs_x__rep_self_delta)
            {
              *stream_p = get_storaged_delta_stream(rep_state, target,
                                                    result_pool);
              return SVN_NO_ERROR;
            }
        }

      /* Don't keep file handles open for longer than necessary. */
      if (rep_state->sfile->rfile)
        {
          SVN_ERR(svn_fs_x__close_revision_file(rep_state->sfile->rfile));
          rep_state->sfile->rfile = NULL;
        }
    }

  /* Read both fulltexts and construct a delta. */
  if (source)
    SVN_ERR(svn_fs_x__get_contents(&source_stream, fs, source->data_rep,
                                   TRUE, result_pool));
  else
    source_stream = svn_stream_empty(result_pool);

  SVN_ERR(svn_fs_x__get_contents(&target_stream, fs, target->data_rep,
                                 TRUE, result_pool));

  /* Because source and target stream will already verify their content,
   * there is no need to do this once more.  In particular if the stream
   * content is being fetched from cache. */
  svn_txdelta2(stream_p, source_stream, target_stream, FALSE, result_pool);

  return SVN_NO_ERROR;
}

/* Return TRUE when all svn_fs_x__dirent_t* in ENTRIES are already sorted
   by their respective name. */
static svn_boolean_t
sorted(apr_array_header_t *entries)
{
  int i;

  const svn_fs_x__dirent_t * const *dirents = (const void *)entries->elts;
  for (i = 0; i < entries->nelts-1; ++i)
    if (strcmp(dirents[i]->name, dirents[i+1]->name) > 0)
      return FALSE;

  return TRUE;
}

/* Compare the names of the two dirents given in **A and **B. */
static int
compare_dirents(const void *a,
                const void *b)
{
  const svn_fs_x__dirent_t *lhs = *((const svn_fs_x__dirent_t * const *) a);
  const svn_fs_x__dirent_t *rhs = *((const svn_fs_x__dirent_t * const *) b);

  return strcmp(lhs->name, rhs->name);
}

/* Compare the name of the dirents given in **A with the C string in *B. */
static int
compare_dirent_name(const void *a,
                    const void *b)
{
  const svn_fs_x__dirent_t *lhs = *((const svn_fs_x__dirent_t * const *) a);
  const char *rhs = b;

  return strcmp(lhs->name, rhs);
}

/* Into ENTRIES, parse all directories entries from the serialized form in
 * DATA.  If INCREMENTAL is TRUE, read until the end of the STREAM and
 * update the data.  ID is provided for nicer error messages.
 *
 * The contents of DATA will be shared with the items in ENTRIES, i.e. it
 * must not be modified afterwards and must remain valid as long as ENTRIES
 * is valid.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
parse_dir_entries(apr_array_header_t **entries_p,
                  const svn_stringbuf_t *data,
                  svn_boolean_t incremental,
                  const svn_fs_x__id_t *id,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const apr_byte_t *p = (const apr_byte_t *)data->data;
  const apr_byte_t *end = p + data->len;
  apr_uint64_t count;
  apr_hash_t *hash = incremental ? svn_hash__make(scratch_pool) : NULL;
  apr_array_header_t *entries;

  /* Construct the resulting container. */
  p = svn__decode_uint(&count, p, end);
  if (count > INT_MAX)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Directory for '%s' is too large"),
                             svn_fs_x__id_unparse(id, scratch_pool)->data);

  entries = apr_array_make(result_pool, (int)count,
                           sizeof(svn_fs_x__dirent_t *));

  while (p != end)
    {
      apr_size_t len;
      svn_fs_x__dirent_t *dirent;
      dirent = apr_pcalloc(result_pool, sizeof(*dirent));

      /* The part of the serialized entry that is not the name will be
       * about 6 bytes or less.  Since APR allocates with an 8 byte
       * alignment (4 bytes loss on average per string), simply using
       * the name string in DATA already gives us near-optimal memory
       * usage. */
      dirent->name = (const char *)p;
      len = strlen(dirent->name);
      p += len + 1;
      if (p == end)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                            _("Directory entry missing kind in '%s'"),
                            svn_fs_x__id_unparse(id, scratch_pool)->data);

      dirent->kind = (svn_node_kind_t)*(p++);
      if (p == end)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                            _("Directory entry missing change set in '%s'"),
                            svn_fs_x__id_unparse(id, scratch_pool)->data);

      p = svn__decode_int(&dirent->id.change_set, p, end);
      if (p == end)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                            _("Directory entry missing item number in '%s'"),
                            svn_fs_x__id_unparse(id, scratch_pool)->data);

      p = svn__decode_uint(&dirent->id.number, p, end);

      /* In incremental mode, update the hash; otherwise, write to the
       * final array. */
      if (incremental)
        {
          /* Insertion / update or a deletion? */
          if (svn_fs_x__id_used(&dirent->id))
            apr_hash_set(hash, dirent->name, len, dirent);
          else
            apr_hash_set(hash, dirent->name, len, NULL);
        }
      else
        {
          APR_ARRAY_PUSH(entries, svn_fs_x__dirent_t *) = dirent;
        }
    }

  if (incremental)
    {
      /* Convert container into a sorted array. */
      apr_hash_index_t *hi;
      for (hi = apr_hash_first(scratch_pool, hash); hi; hi = apr_hash_next(hi))
        APR_ARRAY_PUSH(entries, svn_fs_x__dirent_t *) = apr_hash_this_val(hi);

      if (!sorted(entries))
        svn_sort__array(entries, compare_dirents);
    }
  else
    {
      /* Check that we read the expected amount of entries. */
      if ((apr_uint64_t)entries->nelts != count)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                            _("Directory length mismatch in '%s'"),
                            svn_fs_x__id_unparse(id, scratch_pool)->data);
    }

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
                 svn_fs_x__noderev_t *noderev,
                 apr_pool_t *scratch_pool)
{
  if (noderev->data_rep
      && ! svn_fs_x__is_revision(noderev->data_rep->id.change_set))
    {
      const svn_io_dirent2_t *dirent;
      const char *filename;

      filename = svn_fs_x__path_txn_node_children(fs, &noderev->noderev_id,
                                                  scratch_pool, scratch_pool);

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
   convert them into svn_fs_x__dirent_t values. */
static svn_error_t *
get_dir_contents(svn_fs_x__dir_data_t *dir,
                 svn_fs_t *fs,
                 svn_fs_x__noderev_t *noderev,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_stream_t *contents;
  const svn_fs_x__id_t *id = &noderev->noderev_id;
  apr_size_t len;
  svn_stringbuf_t *text;
  svn_boolean_t incremental;

  /* Initialize the result. */
  dir->txn_filesize = SVN_INVALID_FILESIZE;

  /* Read dir contents - unless there is none in which case we are done. */
  if (noderev->data_rep
      && ! svn_fs_x__is_revision(noderev->data_rep->id.change_set))
    {
      /* Get location & current size of the directory representation. */
      const char *filename;
      apr_file_t *file;

      filename = svn_fs_x__path_txn_node_children(fs, id, scratch_pool,
                                                  scratch_pool);

      /* The representation is mutable.  Read the old directory
         contents from the mutable children file, followed by the
         changes we've made in this transaction. */
      SVN_ERR(svn_io_file_open(&file, filename, APR_READ | APR_BUFFERED,
                               APR_OS_DEFAULT, scratch_pool));

      /* Obtain txn children file size. */
      SVN_ERR(svn_io_file_size_get(&dir->txn_filesize, file, scratch_pool));
      len = (apr_size_t)dir->txn_filesize;

      /* Finally, provide stream access to FILE. */
      contents = svn_stream_from_aprfile2(file, FALSE, scratch_pool);
      incremental = TRUE;
    }
  else if (noderev->data_rep)
    {
      /* The representation is immutable.  Read it normally. */
      len = noderev->data_rep->expanded_size;
      SVN_ERR(svn_fs_x__get_contents(&contents, fs, noderev->data_rep,
                                     FALSE, scratch_pool));
      incremental = FALSE;
    }
  else
    {
      /* Empty representation == empty directory. */
      dir->entries = apr_array_make(result_pool, 0,
                                    sizeof(svn_fs_x__dirent_t *));
      return SVN_NO_ERROR;
    }

  /* Read the whole stream contents into a single buffer.
   * Due to our LEN hint, no allocation overhead occurs.
   *
   * Also, a large portion of TEXT will be file / dir names which we
   * directly reference from DIR->ENTRIES instead of copying them.
   * Hence, we need to use the RESULT_POOL here. */
  SVN_ERR(svn_stringbuf_from_stream(&text, contents, len, result_pool));
  SVN_ERR(svn_stream_close(contents));

  /* de-serialize hash */
  SVN_ERR(parse_dir_entries(&dir->entries, text, incremental, id,
                            result_pool, scratch_pool));

  return SVN_NO_ERROR;
}


/* Return the cache object in FS responsible to storing the directory the
 * NODEREV plus the corresponding pre-allocated *KEY.
 */
static svn_cache__t *
locate_dir_cache(svn_fs_t *fs,
                 svn_fs_x__id_t *key,
                 svn_fs_x__noderev_t *noderev)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;

  if (!noderev->data_rep)
    {
      /* no data rep -> empty directory.
         Use a key that does definitely not clash with non-NULL reps. */
      key->change_set = SVN_FS_X__INVALID_CHANGE_SET;
      key->number = SVN_FS_X__ITEM_INDEX_UNUSED;
    }
  else if (svn_fs_x__is_txn(noderev->noderev_id.change_set))
    {
      /* data in txns must be addressed by noderev ID since the
         representation has not been created, yet. */
      *key = noderev->noderev_id;
    }
  else
    {
      /* committed data can use simple rev,item pairs */
      *key = noderev->data_rep->id;
    }

  return ffd->dir_cache;
}

svn_error_t *
svn_fs_x__rep_contents_dir(apr_array_header_t **entries_p,
                           svn_fs_t *fs,
                           svn_fs_x__noderev_t *noderev,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_fs_x__id_t key;
  svn_fs_x__dir_data_t *dir;

  /* find the cache we may use */
  svn_cache__t *cache = locate_dir_cache(fs, &key, noderev);
  svn_boolean_t found;

  SVN_ERR(svn_cache__get((void **)&dir, &found, cache, &key, result_pool));
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

  /* Read in the directory contents. */
  dir = apr_pcalloc(scratch_pool, sizeof(*dir));
  SVN_ERR(get_dir_contents(dir, fs, noderev, result_pool, scratch_pool));
  *entries_p = dir->entries;

  /* Update the cache, if we are to use one.
   *
   * Don't even attempt to serialize very large directories; it would cause
   * an unnecessary memory allocation peak.  100 bytes/entry is about right.
   */
  if (svn_cache__is_cachable(cache, 100 * dir->entries->nelts))
    SVN_ERR(svn_cache__set(cache, &key, dir, scratch_pool));

  return SVN_NO_ERROR;
}

svn_fs_x__dirent_t *
svn_fs_x__find_dir_entry(apr_array_header_t *entries,
                         const char *name,
                         int *hint)
{
  svn_fs_x__dirent_t **result
    = svn_sort__array_lookup(entries, name, hint, compare_dirent_name);
  return result ? *result : NULL;
}

svn_error_t *
svn_fs_x__rep_contents_dir_entry(svn_fs_x__dirent_t **dirent,
                                 svn_fs_t *fs,
                                 svn_fs_x__noderev_t *noderev,
                                 const char *name,
                                 apr_size_t *hint,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_boolean_t found = FALSE;

  /* find the cache we may use */
  svn_fs_x__id_t key;
  svn_cache__t *cache = locate_dir_cache(fs, &key, noderev);
  svn_fs_x__ede_baton_t baton;

  svn_filesize_t filesize;
  SVN_ERR(get_txn_dir_info(&filesize, fs, noderev, scratch_pool));

   /* Cache lookup. */
  baton.hint = *hint;
  baton.name = name;
  baton.txn_filesize = filesize;

  SVN_ERR(svn_cache__get_partial((void **)dirent,
                                 &found,
                                 cache,
                                 &key,
                                 svn_fs_x__extract_dir_entry,
                                 &baton,
                                 result_pool));

  /* Remember the new clue only if we found something at that spot. */
  if (found)
    *hint = baton.hint;

  /* fetch data from disk if we did not find it in the cache */
  if (! found || baton.out_of_date)
    {
      svn_fs_x__dirent_t *entry;
      svn_fs_x__dirent_t *entry_copy = NULL;
      svn_fs_x__dir_data_t dir;

      /* Read in the directory contents. */
      SVN_ERR(get_dir_contents(&dir, fs, noderev, scratch_pool,
                               scratch_pool));

      /* Update the cache, if we are to use one.
       *
       * Don't even attempt to serialize very large directories; it would
       * cause an unnecessary memory allocation peak.  150 bytes / entry is
       * about right. */
      if (cache && svn_cache__is_cachable(cache, 150 * dir.entries->nelts))
        SVN_ERR(svn_cache__set(cache, &key, &dir, scratch_pool));

      /* find desired entry and return a copy in POOL, if found */
      entry = svn_fs_x__find_dir_entry(dir.entries, name, NULL);
      if (entry)
        {
          entry_copy = apr_pmemdup(result_pool, entry, sizeof(*entry_copy));
          entry_copy->name = apr_pstrdup(result_pool, entry->name);
        }

      *dirent = entry_copy;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_proplist(apr_hash_t **proplist,
                       svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  const svn_fs_x__id_t *noderev_id = &noderev->noderev_id;

  if (noderev->prop_rep
      && !svn_fs_x__is_revision(noderev->prop_rep->id.change_set))
    {
      svn_stringbuf_t *content;
      svn_string_t *as_string;
      const char *filename = svn_fs_x__path_txn_node_props(fs, noderev_id,
                                                           scratch_pool,
                                                           scratch_pool);
      SVN_ERR(svn_stringbuf_from_file2(&content, filename, result_pool));

      as_string = svn_stringbuf__morph_into_string(content);
      SVN_ERR_W(svn_fs_x__parse_properties(proplist, as_string, result_pool),
                apr_psprintf(scratch_pool,
                    "malformed property list for node-revision '%s' in '%s'",
                    svn_fs_x__id_unparse(&noderev->noderev_id,
                                         scratch_pool)->data,
                    filename));
    }
  else if (noderev->prop_rep)
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;
      svn_fs_x__representation_t *rep = noderev->prop_rep;
      svn_fs_x__pair_cache_key_t key = { 0 };
      svn_string_t *content;
      svn_boolean_t is_cached;

      key.revision = svn_fs_x__get_revnum(rep->id.change_set);
      key.second = rep->id.number;
      SVN_ERR(svn_cache__get((void **) proplist, &is_cached,
                             ffd->properties_cache, &key, result_pool));
      if (is_cached)
        return SVN_NO_ERROR;

      SVN_ERR(svn_fs_x__get_contents(&stream, fs, rep, FALSE, scratch_pool));
      SVN_ERR(svn_string_from_stream2(&content, stream, rep->expanded_size,
                                      result_pool));

      SVN_ERR_W(svn_fs_x__parse_properties(proplist, content, result_pool),
                apr_psprintf(scratch_pool,
                    "malformed property list for node-revision '%s'",
                    svn_fs_x__id_unparse(&noderev->noderev_id,
                                         scratch_pool)->data));

      SVN_ERR(svn_cache__set(ffd->properties_cache, &key, *proplist,
                             scratch_pool));
    }
  else
    {
      /* return an empty prop list if the node doesn't have any props */
      *proplist = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__create_changes_context(svn_fs_x__changes_context_t **context,
                                 svn_fs_t *fs,
                                 svn_revnum_t rev,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_fs_x__changes_context_t *result = apr_pcalloc(result_pool,
                                                    sizeof(*result));
  result->fs = fs;
  result->revision = rev;

  SVN_ERR(svn_fs_x__ensure_revision_exists(rev, fs, scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_init(&result->revision_file, fs, rev,
                                  result_pool));

  *context = result;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__get_changes(apr_array_header_t **changes,
                      svn_fs_x__changes_context_t *context,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t found;
  svn_fs_x__data_t *ffd = context->fs->fsap_data;

  svn_fs_x__id_t id;
  id.change_set = svn_fs_x__change_set_by_rev(context->revision);
  id.number = SVN_FS_X__ITEM_INDEX_CHANGES;

  /* try cache lookup first */

  if (svn_fs_x__is_packed_rev(context->fs, context->revision))
    {
      apr_off_t offset;
      svn_fs_x__pair_cache_key_t key;
      svn_fs_x__changes_get_list_baton_t baton;
      baton.start = (int)context->next;
      baton.eol = &context->eol;

      SVN_ERR(svn_fs_x__item_offset(&offset, &baton.sub_item, context->fs,
                                    context->revision_file,
                                    &id, scratch_pool));
      key.revision = svn_fs_x__packed_base_rev(context->fs,
                                               context->revision);
      key.second = offset;

      SVN_ERR(svn_cache__get_partial((void **)changes, &found,
                                     ffd->changes_container_cache, &key,
                                     svn_fs_x__changes_get_list_func,
                                     &baton, result_pool));
    }
  else
    {
      svn_fs_x__changes_list_t *changes_list;
      svn_fs_x__pair_cache_key_t key;
      key.revision = context->revision;
      key.second = context->next;

      SVN_ERR(svn_cache__get((void **)&changes_list, &found,
                             ffd->changes_cache, &key, result_pool));

      if (found)
        {
          /* Where to look next - if there is more data. */
          context->eol = changes_list->eol;
          context->next_offset = changes_list->end_offset;

          /* Return the block as a "proper" APR array. */
          (*changes) = apr_array_make(result_pool, 0, sizeof(void *));
          (*changes)->elts = (char *)changes_list->changes;
          (*changes)->nelts = changes_list->count;
          (*changes)->nalloc = changes_list->count;
        }
    }

  if (!found)
    {
      /* 'block-read' will also provide us with the desired data */
      SVN_ERR(block_read((void **)changes, context->fs, &id,
                         context->revision_file, context,
                         result_pool, scratch_pool));
    }

  context->next += (*changes)->nelts;

  SVN_ERR(dbg__log_access(context->fs, &id, *changes,
                          SVN_FS_X__ITEM_TYPE_CHANGES, scratch_pool));

  return SVN_NO_ERROR;
}

/* Fetch the representation data (header, txdelta / plain windows)
 * addressed by ENTRY->ITEM in FS and cache it under KEY.  Read the data
 * from REV_FILE.  If MAX_OFFSET is not -1, don't read windows that start
 * at or beyond that offset.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
block_read_contents(svn_fs_t *fs,
                    svn_fs_x__revision_file_t *rev_file,
                    svn_fs_x__p2l_entry_t* entry,
                    svn_fs_x__pair_cache_key_t *key,
                    apr_off_t max_offset,
                    apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_cache_key_t header_key = { 0 };
  rep_state_t rs = { 0 };
  svn_filesize_t fulltext_len;
  svn_fs_x__rep_header_t *rep_header;

  header_key.revision = (apr_int32_t)key->revision;
  header_key.is_packed = svn_fs_x__is_packed_rev(fs, header_key.revision);
  header_key.item_index = key->second;

  SVN_ERR(read_rep_header(&rep_header, fs, rev_file, &header_key,
                          scratch_pool));
  SVN_ERR(init_rep_state(&rs, rep_header, fs, rev_file, entry, scratch_pool));
  SVN_ERR(cache_windows(&fulltext_len, fs, &rs, max_offset, scratch_pool));

  return SVN_NO_ERROR;
}

/* For the given REV_FILE in FS, in *STREAM return a stream covering the
 * item specified by ENTRY.  Also, verify the item's content by low-level
 * checksum.  Allocate the result in RESULT_POOL.
 */
static svn_error_t *
read_item(svn_stream_t **stream,
          svn_fs_t *fs,
          svn_fs_x__revision_file_t *rev_file,
          svn_fs_x__p2l_entry_t* entry,
          apr_pool_t *result_pool)
{
  apr_uint32_t digest;
  svn_checksum_t *expected, *actual;
  apr_uint32_t plain_digest;
  svn_stringbuf_t *text;

  /* Read item into string buffer. */
  text = svn_stringbuf_create_ensure(entry->size, result_pool);
  text->len = entry->size;
  text->data[text->len] = 0;
  SVN_ERR(svn_fs_x__rev_file_read(rev_file, text->data, text->len));

  /* Return (construct, calculate) stream and checksum. */
  *stream = svn_stream_from_stringbuf(text, result_pool);
  digest = svn__fnv1a_32x4(text->data, text->len);

  /* Checksums will match most of the time. */
  if (entry->fnv1_checksum == digest)
    return SVN_NO_ERROR;

  /* Construct proper checksum objects from their digests to allow for
   * nice error messages. */
  plain_digest = htonl(entry->fnv1_checksum);
  expected = svn_checksum__from_digest_fnv1a_32x4(
                (const unsigned char *)&plain_digest, result_pool);
  plain_digest = htonl(digest);
  actual = svn_checksum__from_digest_fnv1a_32x4(
                (const unsigned char *)&plain_digest, result_pool);

  /* Construct the full error message with all the info we have. */
  return svn_checksum_mismatch_err(expected, actual, result_pool,
                 _("Low-level checksum mismatch while reading\n"
                   "%s bytes of meta data at offset %s "),
                 apr_off_t_toa(result_pool, entry->size),
                 apr_off_t_toa(result_pool, entry->offset));
}

/* If not already cached or if MUST_READ is set, read the changed paths
 * list addressed by ENTRY in FS and retúrn it in *CHANGES.  Cache the
 * result if caching is enabled.  Read the data from REV_FILE.  Trim the
 * data in *CHANGES to the range given by CONTEXT.  Allocate *CHANGES in
 * RESUSLT_POOL and allocate temporaries in SCRATCH_POOL.
 */
static svn_error_t *
block_read_changes(apr_array_header_t **changes,
                   svn_fs_t *fs,
                   svn_fs_x__revision_file_t *rev_file,
                   svn_fs_x__p2l_entry_t* entry,
                   svn_fs_x__changes_context_t *context,
                   svn_boolean_t must_read,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;
  svn_fs_x__pair_cache_key_t key;
  svn_fs_x__changes_list_t changes_list;

  /* If we don't have to return any data, just read and cache the first
     block.  This means we won't cache the remaining blocks from longer
     lists right away but only if they are actually needed. */
  apr_size_t next = must_read ? context->next : 0;
  apr_size_t next_offset = must_read ? context->next_offset : 0;

  /* we don't support containers, yet */
  SVN_ERR_ASSERT(entry->item_count == 1);

  /* The item to read / write. */
  key.revision = svn_fs_x__get_revnum(entry->items[0].change_set);
  key.second = next;

  /* already in cache? */
  if (!must_read)
    {
      svn_boolean_t is_cached = FALSE;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->changes_cache, &key,
                                 scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  /* Verify the whole list only once.  We don't use the STREAM any further. */
  if (!must_read || next == 0)
    SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* Seek to the block to read within the changes list. */
  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL,
                                  entry->offset + next_offset));
  SVN_ERR(svn_fs_x__rev_file_stream(&stream, rev_file));

  /* read changes from revision file */
  SVN_ERR(svn_fs_x__read_changes(changes, stream, SVN_FS_X__CHANGES_BLOCK_SIZE,
                                 result_pool, scratch_pool));

  SVN_ERR(svn_fs_x__rev_file_offset(&changes_list.end_offset, rev_file));
  changes_list.end_offset -= entry->offset;
  changes_list.start_offset = next_offset;
  changes_list.count = (*changes)->nelts;
  changes_list.changes = (svn_fs_x__change_t **)(*changes)->elts;
  changes_list.eol =    (changes_list.count < SVN_FS_X__CHANGES_BLOCK_SIZE)
                     || (changes_list.end_offset + 1 >= entry->size);

  /* cache for future reference */

  SVN_ERR(svn_cache__set(ffd->changes_cache, &key, &changes_list,
                         scratch_pool));

  /* Trim the result:
   * Remove the entries that already been reported. */
  if (must_read)
    {
      context->next_offset = changes_list.end_offset;
      context->eol = changes_list.eol;
    }

  return SVN_NO_ERROR;
}

/* If not already cached or if MUST_READ is set, read the changed paths
 * list container addressed by ENTRY in FS.  Return the changes list
 * identified by SUB_ITEM in *CHANGES, using CONTEXT to select a sub-range
 * within that list.  Read the data from REV_FILE and cache the result.
 *
 * Allocate *CHANGES in RESUSLT_POOL and everything else in SCRATCH_POOL.
 */
static svn_error_t *
block_read_changes_container(apr_array_header_t **changes,
                             svn_fs_t *fs,
                             svn_fs_x__revision_file_t *rev_file,
                             svn_fs_x__p2l_entry_t* entry,
                             apr_uint32_t sub_item,
                             svn_fs_x__changes_context_t *context,
                             svn_boolean_t must_read,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__changes_t *container;
  svn_fs_x__pair_cache_key_t key;
  svn_stream_t *stream;
  svn_revnum_t revision = svn_fs_x__get_revnum(entry->items[0].change_set);

  key.revision = svn_fs_x__packed_base_rev(fs, revision);
  key.second = entry->offset;

  /* already in cache? */
  if (!must_read)
    {
      svn_boolean_t is_cached = FALSE;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->changes_container_cache,
                                 &key, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* read changes from revision file */

  SVN_ERR(svn_fs_x__read_changes_container(&container, stream, scratch_pool,
                                           scratch_pool));

  /* extract requested data */

  if (must_read)
    SVN_ERR(svn_fs_x__changes_get_list(changes, container, sub_item,
                                       context, result_pool));
  SVN_ERR(svn_cache__set(ffd->changes_container_cache, &key, container,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* If not already cached or if MUST_READ is set, read the node revision
 * addressed by ENTRY in FS and return it in *NODEREV_P.  Cache the
 * result under KEY if caching is enabled.  Read the data from REV_FILE.
 * Allocate *NODEREV_P in RESUSLT_POOL and allocate temporaries in
 * SCRATCH_POOL.
 */
static svn_error_t *
block_read_noderev(svn_fs_x__noderev_t **noderev_p,
                   svn_fs_t *fs,
                   svn_fs_x__revision_file_t *rev_file,
                   svn_fs_x__p2l_entry_t* entry,
                   svn_fs_x__pair_cache_key_t *key,
                   svn_boolean_t must_read,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_stream_t *stream;

  /* we don't support containers, yet */
  SVN_ERR_ASSERT(entry->item_count == 1);

  /* already in cache? */
  if (!must_read)
    {
      svn_boolean_t is_cached = FALSE;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->node_revision_cache, key,
                                 scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* read node rev from revision file */

  SVN_ERR(svn_fs_x__read_noderev(noderev_p, stream, result_pool,
                                 scratch_pool));
  SVN_ERR(svn_cache__set(ffd->node_revision_cache, key, *noderev_p,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* If not already cached or if MUST_READ is set, read the node revision
 * container addressed by ENTRY in FS.  Return the item identified by
 * SUB_ITEM in *NODEREV_P.  Read the data from REV_FILE and cache it.
 * Allocate *NODEREV_P in RESUSLT_POOL and allocate temporaries in
 * SCRATCH_POOL.
 */
static svn_error_t *
block_read_noderevs_container(svn_fs_x__noderev_t **noderev_p,
                              svn_fs_t *fs,
                              svn_fs_x__revision_file_t *rev_file,
                              svn_fs_x__p2l_entry_t* entry,
                              apr_uint32_t sub_item,
                              svn_boolean_t must_read,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__noderevs_t *container;
  svn_stream_t *stream;
  svn_fs_x__pair_cache_key_t key;
  svn_revnum_t revision = svn_fs_x__get_revnum(entry->items[0].change_set);

  key.revision = svn_fs_x__packed_base_rev(fs, revision);
  key.second = entry->offset;

  /* already in cache? */
  if (!must_read)
    {
      svn_boolean_t is_cached = FALSE;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->noderevs_container_cache,
                                 &key, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* read noderevs from revision file */
  SVN_ERR(svn_fs_x__read_noderevs_container(&container, stream, scratch_pool,
                                            scratch_pool));

  /* extract requested data */
  if (must_read)
    SVN_ERR(svn_fs_x__noderevs_get(noderev_p, container, sub_item,
                                   result_pool));

  SVN_ERR(svn_cache__set(ffd->noderevs_container_cache, &key, container,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* If not already cached or if MUST_READ is set, read the representation
 * container addressed by ENTRY in FS.  Return an extractor object for the
 * item identified by SUB_ITEM in *EXTRACTOR.  Read the data from REV_FILE
 * and cache it.  Allocate *EXTRACTOR in RESUSLT_POOL and all temporaries
 * in SCRATCH_POOL.
 */
static svn_error_t *
block_read_reps_container(svn_fs_x__rep_extractor_t **extractor,
                          svn_fs_t *fs,
                          svn_fs_x__revision_file_t *rev_file,
                          svn_fs_x__p2l_entry_t* entry,
                          apr_uint32_t sub_item,
                          svn_boolean_t must_read,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  svn_fs_x__reps_t *container;
  svn_stream_t *stream;
  svn_fs_x__pair_cache_key_t key;
  svn_revnum_t revision = svn_fs_x__get_revnum(entry->items[0].change_set);

  key.revision = svn_fs_x__packed_base_rev(fs, revision);
  key.second = entry->offset;

  /* already in cache? */
  if (!must_read)
    {
      svn_boolean_t is_cached = FALSE;
      SVN_ERR(svn_cache__has_key(&is_cached, ffd->reps_container_cache,
                                 &key, scratch_pool));
      if (is_cached)
        return SVN_NO_ERROR;
    }

  SVN_ERR(read_item(&stream, fs, rev_file, entry, scratch_pool));

  /* read noderevs from revision file */
  SVN_ERR(svn_fs_x__read_reps_container(&container, stream, result_pool,
                                        scratch_pool));

  /* extract requested data */

  if (must_read)
    SVN_ERR(svn_fs_x__reps_get(extractor, fs, container, sub_item,
                               result_pool));

  SVN_ERR(svn_cache__set(ffd->reps_container_cache, &key, container,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the whole (e.g. 64kB) block containing the item identified by ID in
 * FS and put all data into cache.  If necessary and depending on heuristics,
 * neighboring blocks may also get read.  The data is being read from
 * already open REVISION_FILE, which must be the correct rev / pack file
 * w.r.t. ID->CHANGE_SET.
 *
 * For noderevs and changed path lists, the item fetched can be allocated
 * RESULT_POOL and returned in *RESULT.  Otherwise, RESULT must be NULL.
 * The BATON is passed along to the extractor sub-functions and will be
 * used only when constructing the *RESULT.  SCRATCH_POOL will be used for
 * all temporary allocations.
 */
static svn_error_t *
block_read(void **result,
           svn_fs_t *fs,
           const svn_fs_x__id_t *id,
           svn_fs_x__revision_file_t *revision_file,
           void *baton,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_off_t offset, wanted_offset = 0;
  apr_off_t block_start = 0;
  apr_uint32_t wanted_sub_item = 0;
  svn_revnum_t revision = svn_fs_x__get_revnum(id->change_set);
  apr_array_header_t *entries;
  int run_count = 0;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* don't try this on transaction protorev files */
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));

  /* index lookup: find the OFFSET of the item we *must* read plus (in the
   * "do-while" block) the list of items in the same block. */
  SVN_ERR(svn_fs_x__item_offset(&wanted_offset, &wanted_sub_item, fs,
                                revision_file, id, iterpool));

  offset = wanted_offset;
  do
    {
      /* fetch list of items in the block surrounding OFFSET */
      SVN_ERR(svn_fs_x__rev_file_seek(revision_file, &block_start, offset));
      SVN_ERR(svn_fs_x__p2l_index_lookup(&entries, fs, revision_file,
                                         revision, block_start,
                                         ffd->block_size, scratch_pool,
                                         scratch_pool));

      /* read all items from the block */
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_boolean_t is_result, is_wanted;
          apr_pool_t *pool;

          svn_fs_x__p2l_entry_t* entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t);

          /* skip empty sections */
          if (entry->type == SVN_FS_X__ITEM_TYPE_UNUSED)
            continue;

          /* the item / container we were looking for? */
          is_wanted =    entry->offset == wanted_offset
                      && entry->item_count >= wanted_sub_item
                      && svn_fs_x__id_eq(entry->items + wanted_sub_item, id);
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
              svn_fs_x__pair_cache_key_t key = { 0 };
              key.revision = svn_fs_x__get_revnum(entry->items[0].change_set);
              key.second = entry->items[0].number;

              SVN_ERR(svn_fs_x__rev_file_seek(revision_file, NULL,
                                              entry->offset));
              switch (entry->type)
                {
                  case SVN_FS_X__ITEM_TYPE_FILE_REP:
                  case SVN_FS_X__ITEM_TYPE_DIR_REP:
                  case SVN_FS_X__ITEM_TYPE_FILE_PROPS:
                  case SVN_FS_X__ITEM_TYPE_DIR_PROPS:
                    SVN_ERR(block_read_contents(fs, revision_file,
                                                entry, &key,
                                                is_wanted
                                                  ? -1
                                                  : block_start + ffd->block_size,
                                                iterpool));
                    break;

                  case SVN_FS_X__ITEM_TYPE_NODEREV:
                    SVN_ERR(block_read_noderev((svn_fs_x__noderev_t **)&item,
                                               fs, revision_file,
                                               entry, &key, is_result,
                                               pool, iterpool));
                    break;

                  case SVN_FS_X__ITEM_TYPE_CHANGES:
                    SVN_ERR(block_read_changes((apr_array_header_t **)&item,
                                               fs, revision_file,
                                               entry, baton, is_result,
                                               pool, iterpool));
                    break;

                  case SVN_FS_X__ITEM_TYPE_CHANGES_CONT:
                    SVN_ERR(block_read_changes_container
                                            ((apr_array_header_t **)&item,
                                             fs, revision_file,
                                             entry, wanted_sub_item,
                                             baton, is_result,
                                             pool, iterpool));
                    break;

                  case SVN_FS_X__ITEM_TYPE_NODEREVS_CONT:
                    SVN_ERR(block_read_noderevs_container
                                            ((svn_fs_x__noderev_t **)&item,
                                             fs, revision_file,
                                             entry, wanted_sub_item,
                                             is_result, pool, iterpool));
                    break;

                  case SVN_FS_X__ITEM_TYPE_REPS_CONT:
                    SVN_ERR(block_read_reps_container
                                      ((svn_fs_x__rep_extractor_t **)&item,
                                       fs, revision_file,
                                       entry, wanted_sub_item,
                                       is_result, pool, iterpool));
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

              svn_pool_clear(iterpool);
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
