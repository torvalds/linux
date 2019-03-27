/* pack.c --- FSX shard packing functionality
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
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "private/svn_temp_serializer.h"

#include "fs_x.h"
#include "pack.h"
#include "util.h"
#include "revprops.h"
#include "transaction.h"
#include "index.h"
#include "low_level.h"
#include "cached_data.h"
#include "changes.h"
#include "noderevs.h"
#include "reps.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

/* Packing logic:
 *
 * We pack files on a pack file basis (e.g. 1000 revs) without changing
 * existing pack files nor the revision files outside the range to pack.
 *
 * First, we will scan the revision file indexes to determine the number
 * of items to "place" (i.e. determine their optimal position within the
 * future pack file).  For each item, we will need a constant amount of
 * memory to track it.  A MAX_MEM parameter sets a limit to the number of
 * items we may place in one go.  That means, we may not be able to add
 * all revisions at once.  Instead, we will run the placement for a subset
 * of revisions at a time.  The very unlikely worst case will simply append
 * all revision data with just a little reshuffling inside each revision.
 *
 * In a second step, we read all revisions in the selected range, build
 * the item tracking information and copy the items themselves from the
 * revision files to temporary files.  The latter serve as buckets for a
 * very coarse bucket presort:  Separate change lists, file properties,
 * directory properties and noderevs + representations from one another.
 *
 * The third step will determine an optimized placement for the items in
 * each of the 4 buckets separately.  The first three will simply order
 * their items by revision, starting with the newest once.  Placing rep
 * and noderev items is a more elaborate process documented in the code.
 *
 * In short, we store items in the following order:
 * - changed paths lists
 * - node property
 * - directory properties
 * - directory representations corresponding noderevs, lexical path order
 *   with special treatment of "trunk" and "branches"
 * - same for file representations
 *
 * Step 4 copies the items from the temporary buckets into the final
 * pack file and writes the temporary index files.
 *
 * Finally, after the last range of revisions, create the final indexes.
 */

/* Maximum amount of memory we allocate for placement information during
 * the pack process.
 */
#define DEFAULT_MAX_MEM (64 * 1024 * 1024)

/* Data structure describing a node change at PATH, REVISION.
 * We will sort these instances by PATH and NODE_ID such that we can combine
 * similar nodes in the same reps container and store containers in path
 * major order.
 */
typedef struct path_order_t
{
  /* changed path */
  svn_prefix_string__t *path;

  /* node ID for this PATH in REVISION */
  svn_fs_x__id_t node_id;

  /* when this change happened */
  svn_revnum_t revision;

  /* length of the expanded representation content */
  apr_int64_t expanded_size;

  /* item ID of the noderev linked to the change. May be (0, 0). */
  svn_fs_x__id_t noderev_id;

  /* item ID of the representation containing the new data. May be (0, 0). */
  svn_fs_x__id_t rep_id;
} path_order_t;

/* Represents a reference from item FROM to item TO.  FROM may be a noderev
 * or rep_id while TO is (currently) always a representation.  We will sort
 * them by TO which allows us to collect all dependent items.
 */
typedef struct reference_t
{
  svn_fs_x__id_t to;
  svn_fs_x__id_t from;
} reference_t;

/* This structure keeps track of all the temporary data and status that
 * needs to be kept around during the creation of one pack file.  After
 * each revision range (in case we can't process all revs at once due to
 * memory restrictions), parts of the data will get re-initialized.
 */
typedef struct pack_context_t
{
  /* file system that we operate on */
  svn_fs_t *fs;

  /* cancel function to invoke at regular intervals. May be NULL */
  svn_cancel_func_t cancel_func;

  /* baton to pass to CANCEL_FUNC */
  void *cancel_baton;

  /* first revision in the shard (and future pack file) */
  svn_revnum_t shard_rev;

  /* first revision in the range to process (>= SHARD_REV) */
  svn_revnum_t start_rev;

  /* first revision after the range to process (<= SHARD_END_REV) */
  svn_revnum_t end_rev;

  /* first revision after the current shard */
  svn_revnum_t shard_end_rev;

  /* log-to-phys proto index for the whole pack file */
  apr_file_t *proto_l2p_index;

  /* phys-to-log proto index for the whole pack file */
  apr_file_t *proto_p2l_index;

  /* full shard directory path (containing the unpacked revisions) */
  const char *shard_dir;

  /* full packed shard directory path (containing the pack file + indexes) */
  const char *pack_file_dir;

  /* full pack file path (including PACK_FILE_DIR) */
  const char *pack_file_path;

  /* current write position (i.e. file length) in the pack file */
  apr_off_t pack_offset;

  /* the pack file to ultimately write all data to */
  apr_file_t *pack_file;

  /* array of svn_fs_x__p2l_entry_t *, all referring to change lists.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *changes;

  /* temp file receiving all change list items (referenced by CHANGES).
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_file_t *changes_file;

  /* array of svn_fs_x__p2l_entry_t *, all referring to file properties.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *file_props;

  /* temp file receiving all file prop items (referenced by FILE_PROPS).
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *file_props_file;

  /* array of svn_fs_x__p2l_entry_t *, all referring to directory properties.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *dir_props;

  /* temp file receiving all directory prop items (referenced by DIR_PROPS).
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *dir_props_file;

  /* container for all PATH members in PATH_ORDER. */
  svn_prefix_tree__t *paths;

  /* array of path_order_t *.  Will be filled in phase 2 and be cleared
   * after each revision range.  Sorted by PATH, NODE_ID. */
  apr_array_header_t *path_order;

  /* array of reference_t* linking representations to their delta bases.
   * Will be filled in phase 2 and be cleared after each revision range.
   * It will be sorted by the FROM members (for rep->base rep lookup). */
  apr_array_header_t *references;

  /* array of svn_fs_x__p2l_entry_t*.  Will be filled in phase 2 and be
   * cleared after each revision range.  During phase 3, we will set items
   * to NULL that we already processed. */
  apr_array_header_t *reps;

  /* array of int, marking for each revision, at which offset their items
   * begin in REPS.  Will be filled in phase 2 and be cleared after
   * each revision range. */
  apr_array_header_t *rev_offsets;

  /* temp file receiving all items referenced by REPS.
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *reps_file;

  /* pool used for temporary data structures that will be cleaned up when
   * the next range of revisions is being processed */
  apr_pool_t *info_pool;
} pack_context_t;

/* Create and initialize a new pack context for packing shard SHARD_REV in
 * SHARD_DIR into PACK_FILE_DIR within filesystem FS.  Allocate it in POOL
 * and return the structure in *CONTEXT.
 *
 * Limit the number of items being copied per iteration to MAX_ITEMS.
 * Set CANCEL_FUNC and CANCEL_BATON as well.
 */
static svn_error_t *
initialize_pack_context(pack_context_t *context,
                        svn_fs_t *fs,
                        const char *pack_file_dir,
                        const char *shard_dir,
                        svn_revnum_t shard_rev,
                        int max_items,
                        svn_fs_x__batch_fsync_t *batch,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  const char *temp_dir;
  int max_revs = MIN(ffd->max_files_per_dir, max_items);

  SVN_ERR_ASSERT(shard_rev % ffd->max_files_per_dir == 0);

  /* where we will place our various temp files */
  SVN_ERR(svn_io_temp_dir(&temp_dir, pool));

  /* store parameters */
  context->fs = fs;
  context->cancel_func = cancel_func;
  context->cancel_baton = cancel_baton;

  context->shard_rev = shard_rev;
  context->start_rev = shard_rev;
  context->end_rev = shard_rev;
  context->shard_end_rev = shard_rev + ffd->max_files_per_dir;

  /* Create the new directory and pack file. */
  context->shard_dir = shard_dir;
  context->pack_file_dir = pack_file_dir;
  context->pack_file_path
    = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);

  SVN_ERR(svn_fs_x__batch_fsync_open_file(&context->pack_file, batch,
                                          context->pack_file_path, pool));

  /* Proto index files */
  SVN_ERR(svn_fs_x__l2p_proto_index_open(
             &context->proto_l2p_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_L2P_INDEX,
                             pool),
             pool));
  SVN_ERR(svn_fs_x__p2l_proto_index_open(
             &context->proto_p2l_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_P2L_INDEX,
                             pool),
             pool));

  /* item buckets: one item info array and one temp file per bucket */
  context->changes = apr_array_make(pool, max_items,
                                    sizeof(svn_fs_x__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->changes_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));
  context->file_props = apr_array_make(pool, max_items,
                                       sizeof(svn_fs_x__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->file_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));
  context->dir_props = apr_array_make(pool, max_items,
                                      sizeof(svn_fs_x__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->dir_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

  /* noderev and representation item bucket */
  context->rev_offsets = apr_array_make(pool, max_revs, sizeof(int));
  context->path_order = apr_array_make(pool, max_items,
                                       sizeof(path_order_t *));
  context->references = apr_array_make(pool, max_items,
                                       sizeof(reference_t *));
  context->reps = apr_array_make(pool, max_items,
                                 sizeof(svn_fs_x__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->reps_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

  /* the pool used for temp structures */
  context->info_pool = svn_pool_create(pool);
  context->paths = svn_prefix_tree__create(context->info_pool);

  return SVN_NO_ERROR;
}

/* Clean up / free all revision range specific data and files in CONTEXT.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
reset_pack_context(pack_context_t *context,
                   apr_pool_t *scratch_pool)
{
  apr_array_clear(context->changes);
  SVN_ERR(svn_io_file_trunc(context->changes_file, 0, scratch_pool));
  apr_array_clear(context->file_props);
  SVN_ERR(svn_io_file_trunc(context->file_props_file, 0, scratch_pool));
  apr_array_clear(context->dir_props);
  SVN_ERR(svn_io_file_trunc(context->dir_props_file, 0, scratch_pool));

  apr_array_clear(context->rev_offsets);
  apr_array_clear(context->path_order);
  apr_array_clear(context->references);
  apr_array_clear(context->reps);
  SVN_ERR(svn_io_file_trunc(context->reps_file, 0, scratch_pool));

  svn_pool_clear(context->info_pool);
  context->paths = svn_prefix_tree__create(context->info_pool);

  return SVN_NO_ERROR;
}

/* Call this after the last revision range.  It will finalize all index files
 * for CONTEXT and close any open files.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
close_pack_context(pack_context_t *context,
                   apr_pool_t *scratch_pool)
{
  const char *proto_l2p_index_path;
  const char *proto_p2l_index_path;

  /* need the file names for the actual index creation call further down */
  SVN_ERR(svn_io_file_name_get(&proto_l2p_index_path,
                               context->proto_l2p_index, scratch_pool));
  SVN_ERR(svn_io_file_name_get(&proto_p2l_index_path,
                               context->proto_p2l_index, scratch_pool));

  /* finalize proto index files */
  SVN_ERR(svn_io_file_close(context->proto_l2p_index, scratch_pool));
  SVN_ERR(svn_io_file_close(context->proto_p2l_index, scratch_pool));

  /* Append the actual index data to the pack file. */
  SVN_ERR(svn_fs_x__add_index_data(context->fs, context->pack_file,
                                    proto_l2p_index_path,
                                    proto_p2l_index_path,
                                    context->shard_rev,
                                    scratch_pool));

  /* remove proto index files */
  SVN_ERR(svn_io_remove_file2(proto_l2p_index_path, FALSE, scratch_pool));
  SVN_ERR(svn_io_remove_file2(proto_p2l_index_path, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

/* Efficiently copy SIZE bytes from SOURCE to DEST.  Invoke the CANCEL_FUNC
 * from CONTEXT at regular intervals.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
copy_file_data(pack_context_t *context,
               apr_file_t *dest,
               apr_file_t *source,
               svn_filesize_t size,
               apr_pool_t *scratch_pool)
{
  /* most non-representation items will be small.  Minimize the buffer
   * and infrastructure overhead in that case. */
  enum { STACK_BUFFER_SIZE = 1024 };

  if (size < STACK_BUFFER_SIZE)
    {
      /* copy small data using a fixed-size buffer on stack */
      char buffer[STACK_BUFFER_SIZE];
      SVN_ERR(svn_io_file_read_full2(source, buffer, (apr_size_t)size,
                                     NULL, NULL, scratch_pool));
      SVN_ERR(svn_io_file_write_full(dest, buffer, (apr_size_t)size,
                                     NULL, scratch_pool));
    }
  else
    {
      /* use streaming copies for larger data blocks.  That may require
       * the allocation of larger buffers and we should make sure that
       * this extra memory is released asap. */
      svn_fs_x__data_t *ffd = context->fs->fsap_data;
      apr_pool_t *copypool = svn_pool_create(scratch_pool);
      char *buffer = apr_palloc(copypool, ffd->block_size);

      while (size)
        {
          apr_size_t to_copy = (apr_size_t)(MIN(size, ffd->block_size));
          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));

          SVN_ERR(svn_io_file_read_full2(source, buffer, to_copy,
                                         NULL, NULL, scratch_pool));
          SVN_ERR(svn_io_file_write_full(dest, buffer, to_copy,
                                         NULL, scratch_pool));

          size -= to_copy;
        }

      svn_pool_destroy(copypool);
    }

  return SVN_NO_ERROR;
}

/* Writes SIZE bytes, all 0, to DEST.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_null_bytes(apr_file_t *dest,
                 apr_off_t size,
                 apr_pool_t *scratch_pool)
{
  /* Have a collection of high-quality, easy to access NUL bytes handy. */
  enum { BUFFER_SIZE = 1024 };
  static const char buffer[BUFFER_SIZE] = { 0 };

  /* copy SIZE of them into the file's buffer */
  while (size)
    {
      apr_size_t to_write = MIN(size, BUFFER_SIZE);
      SVN_ERR(svn_io_file_write_full(dest, buffer, to_write, NULL,
                                     scratch_pool));
      size -= to_write;
    }

  return SVN_NO_ERROR;
}

/* Copy the "simple" item (changed paths list or property representation)
 * from the current position in REV_FILE to TEMP_FILE using CONTEXT.  Add
 * a copy of ENTRY to ENTRIES but with an updated offset value that points
 * to the copy destination in TEMP_FILE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
copy_item_to_temp(pack_context_t *context,
                  apr_array_header_t *entries,
                  apr_file_t *temp_file,
                  svn_fs_x__revision_file_t *rev_file,
                  svn_fs_x__p2l_entry_t *entry,
                  apr_pool_t *scratch_pool)
{
  apr_file_t *file;
  svn_fs_x__p2l_entry_t *new_entry
    = svn_fs_x__p2l_entry_dup(entry, context->info_pool);

  SVN_ERR(svn_io_file_get_offset(&new_entry->offset, temp_file,
                                 scratch_pool));
  APR_ARRAY_PUSH(entries, svn_fs_x__p2l_entry_t *) = new_entry;

  SVN_ERR(svn_fs_x__rev_file_get(&file, rev_file));
  SVN_ERR(copy_file_data(context, temp_file, file, entry->size,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* Return the offset within CONTEXT->REPS that corresponds to item
 * ITEM_INDEX in  REVISION.
 */
static int
get_item_array_index(pack_context_t *context,
                     svn_revnum_t revision,
                     apr_int64_t item_index)
{
  assert(revision >= context->start_rev);
  return (int)item_index + APR_ARRAY_IDX(context->rev_offsets,
                                         revision - context->start_rev,
                                         int);
}

/* Write INFO to the correct position in CONTEXT->REPS.  The latter may
 * need auto-expanding.  Overwriting an array element is not allowed.
 */
static void
add_item_rep_mapping(pack_context_t *context,
                     svn_fs_x__p2l_entry_t *entry)
{
  int idx;
  assert(entry->item_count == 1);

  /* index of INFO */
  idx = get_item_array_index(context,
                             entry->items[0].change_set,
                             entry->items[0].number);

  /* make sure the index exists in the array */
  while (context->reps->nelts <= idx)
    APR_ARRAY_PUSH(context->reps, void *) = NULL;

  /* set the element.  If there is already an entry, there are probably
   * two items claiming to be the same -> bail out */
  assert(!APR_ARRAY_IDX(context->reps, idx, void *));
  APR_ARRAY_IDX(context->reps, idx, void *) = entry;
}

/* Return the P2L entry from CONTEXT->REPS for the given ID.  If there is
 * none (or not anymore), return NULL.  If RESET has been specified, set
 * the array entry to NULL after returning the entry.
 */
static svn_fs_x__p2l_entry_t *
get_item(pack_context_t *context,
         const svn_fs_x__id_t *id,
         svn_boolean_t reset)
{
  svn_fs_x__p2l_entry_t *result = NULL;
  svn_revnum_t revision = svn_fs_x__get_revnum(id->change_set);
  if (id->number && revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, revision, id->number);
      if (context->reps->nelts > idx)
        {
          result = APR_ARRAY_IDX(context->reps, idx, void *);
          if (result && reset)
            APR_ARRAY_IDX(context->reps, idx, void *) = NULL;
        }
    }

  return result;
}

/* Copy representation item identified by ENTRY from the current position
 * in REV_FILE into CONTEXT->REPS_FILE.  Add all tracking into needed by
 * our placement algorithm to CONTEXT.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
copy_rep_to_temp(pack_context_t *context,
                 svn_fs_x__revision_file_t *rev_file,
                 svn_fs_x__p2l_entry_t *entry,
                 apr_pool_t *scratch_pool)
{
  svn_fs_x__rep_header_t *rep_header;
  svn_stream_t *stream;
  apr_file_t *file;
  apr_off_t source_offset = entry->offset;

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  entry = svn_fs_x__p2l_entry_dup(entry, context->info_pool);
  SVN_ERR(svn_io_file_get_offset(&entry->offset, context->reps_file,
                                 scratch_pool));
  add_item_rep_mapping(context, entry);

  /* read & parse the representation header */
  SVN_ERR(svn_fs_x__rev_file_stream(&stream, rev_file));
  SVN_ERR(svn_fs_x__read_rep_header(&rep_header, stream,
                                    scratch_pool, scratch_pool));

  /* if the representation is a delta against some other rep, link the two */
  if (   rep_header->type == svn_fs_x__rep_delta
      && rep_header->base_revision >= context->start_rev)
    {
      reference_t *reference = apr_pcalloc(context->info_pool,
                                           sizeof(*reference));
      reference->from = entry->items[0];
      reference->to.change_set
        = svn_fs_x__change_set_by_rev(rep_header->base_revision);
      reference->to.number = rep_header->base_item_index;
      APR_ARRAY_PUSH(context->references, reference_t *) = reference;
    }

  /* copy the whole rep (including header!) to our temp file */
  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, source_offset));
  SVN_ERR(svn_fs_x__rev_file_get(&file, rev_file));
  SVN_ERR(copy_file_data(context, context->reps_file, file, entry->size,
                         scratch_pool));

  return SVN_NO_ERROR;
}

/* Directories first, dirs / files sorted by name in reverse lexical order.
 * This maximizes the chance of two items being located close to one another
 * in *all* pack files independent of their change order.  It also groups
 * multi-project repos nicely according to their sub-projects.  The reverse
 * order aspect gives "trunk" preference over "tags" and "branches", so
 * trunk-related items are more likely to be contiguous.
 */
static int
compare_dir_entries(const svn_sort__item_t *a,
                    const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  return strcmp(lhs->name, rhs->name);
}

apr_array_header_t *
svn_fs_x__order_dir_entries(svn_fs_t *fs,
                            apr_hash_t *directory,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_array_header_t *ordered
    = svn_sort__hash(directory, compare_dir_entries, scratch_pool);

  apr_array_header_t *result
    = apr_array_make(result_pool, ordered->nelts, sizeof(svn_fs_dirent_t *));

  int i;
  for (i = 0; i < ordered->nelts; ++i)
    APR_ARRAY_PUSH(result, svn_fs_dirent_t *)
      = APR_ARRAY_IDX(ordered, i, svn_sort__item_t).value;

  return result;
}

/* Return a duplicate of the ORIGINAL path and with special sub-strings
 * (e.g. "trunk") modified in such a way that have a lower lexicographic
 * value than any other "normal" file name.
 */
static const char *
tweak_path_for_ordering(const char *original,
                        apr_pool_t *pool)
{
  /* We may add further special cases as needed. */
  enum {SPECIAL_COUNT = 2};
  static const char *special[SPECIAL_COUNT] = {"trunk", "branch"};
  char *pos;
  char *path = apr_pstrdup(pool, original);
  int i;

  /* Replace the first char of any "special" sub-string we find by
   * a control char, i.e. '\1' .. '\31'.  In the rare event that this
   * would clash with existing paths, no data will be lost but merely
   * the node ordering will be sub-optimal.
   */
  for (i = 0; i < SPECIAL_COUNT; ++i)
    for (pos = strstr(path, special[i]);
         pos;
         pos = strstr(pos + 1, special[i]))
      {
        *pos = (char)(i + '\1');
      }

   return path;
}

/* Copy node revision item identified by ENTRY from the current position
 * in REV_FILE into CONTEXT->REPS_FILE.  Add all tracking into needed by
 * our placement algorithm to CONTEXT.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
copy_node_to_temp(pack_context_t *context,
                  svn_fs_x__revision_file_t *rev_file,
                  svn_fs_x__p2l_entry_t *entry,
                  apr_pool_t *scratch_pool)
{
  path_order_t *path_order = apr_pcalloc(context->info_pool,
                                         sizeof(*path_order));
  svn_fs_x__noderev_t *noderev;
  svn_stream_t *stream;
  apr_file_t *file;
  const char *sort_path;
  apr_off_t source_offset = entry->offset;

  /* read & parse noderev */
  SVN_ERR(svn_fs_x__rev_file_stream(&stream, rev_file));
  SVN_ERR(svn_fs_x__read_noderev(&noderev, stream, scratch_pool,
                                 scratch_pool));

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  entry = svn_fs_x__p2l_entry_dup(entry, context->info_pool);
  SVN_ERR(svn_io_file_get_offset(&entry->offset, context->reps_file,
                                 scratch_pool));
  add_item_rep_mapping(context, entry);

  /* copy the noderev to our temp file */
  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, source_offset));
  SVN_ERR(svn_fs_x__rev_file_get(&file, rev_file));
  SVN_ERR(copy_file_data(context, context->reps_file, file, entry->size,
                         scratch_pool));

  /* if the node has a data representation, make that the node's "base".
   * This will (often) cause the noderev to be placed right in front of
   * its data representation. */

  if (noderev->data_rep
      &&    svn_fs_x__get_revnum(noderev->data_rep->id.change_set)
         >= context->start_rev)
    {
      reference_t *reference = apr_pcalloc(context->info_pool,
                                           sizeof(*reference));
      reference->from = entry->items[0];
      reference->to.change_set = noderev->data_rep->id.change_set;
      reference->to.number = noderev->data_rep->id.number;
      APR_ARRAY_PUSH(context->references, reference_t *) = reference;

      path_order->rep_id = reference->to;
      path_order->expanded_size = noderev->data_rep->expanded_size;
    }

  /* Sort path is the key used for ordering noderevs and associated reps.
   * It will not be stored in the final pack file. */
  sort_path = tweak_path_for_ordering(noderev->created_path, scratch_pool);
  path_order->path = svn_prefix_string__create(context->paths, sort_path);
  path_order->node_id = noderev->node_id;
  path_order->revision = svn_fs_x__get_revnum(noderev->noderev_id.change_set);
  path_order->noderev_id = noderev->noderev_id;
  APR_ARRAY_PUSH(context->path_order, path_order_t *) = path_order;

  return SVN_NO_ERROR;
}

/* implements compare_fn_t. Place LHS before RHS, if the latter is older.
 */
static int
compare_p2l_info(const svn_fs_x__p2l_entry_t * const * lhs,
                 const svn_fs_x__p2l_entry_t * const * rhs)
{
  assert(*lhs != *rhs);
  if ((*lhs)->item_count == 0)
    return (*lhs)->item_count == 0 ? 0 : -1;
  if ((*lhs)->item_count == 0)
    return 1;

  if ((*lhs)->items[0].change_set == (*rhs)->items[0].change_set)
    return (*lhs)->items[0].number > (*rhs)->items[0].number ? -1 : 1;

  return (*lhs)->items[0].change_set > (*rhs)->items[0].change_set ? -1 : 1;
}

/* Sort svn_fs_x__p2l_entry_t * array ENTRIES by age.  Place the latest
 * items first.
 */
static void
sort_items(apr_array_header_t *entries)
{
  svn_sort__array(entries,
                  (int (*)(const void *, const void *))compare_p2l_info);
}

/* implements compare_fn_t.  Sort descending by PATH, NODE_ID and REVISION.
 */
static int
compare_path_order(const path_order_t * const * lhs_p,
                   const path_order_t * const * rhs_p)
{
  const path_order_t * lhs = *lhs_p;
  const path_order_t * rhs = *rhs_p;

  /* lexicographic order on path and node (i.e. latest first) */
  int diff = svn_prefix_string__compare(lhs->path, rhs->path);
  if (diff)
    return diff;

  /* reverse order on node (i.e. latest first) */
  diff = svn_fs_x__id_compare(&rhs->node_id, &lhs->node_id);
  if (diff)
    return diff;

  /* reverse order on revision (i.e. latest first) */
  if (lhs->revision != rhs->revision)
    return lhs->revision < rhs->revision ? 1 : -1;

  return 0;
}

/* implements compare_fn_t.  Sort ascending by TO, FROM.
 */
static int
compare_references(const reference_t * const * lhs_p,
                   const reference_t * const * rhs_p)
{
  const reference_t * lhs = *lhs_p;
  const reference_t * rhs = *rhs_p;

  int diff = svn_fs_x__id_compare(&lhs->to, &rhs->to);
  return diff ? diff : svn_fs_x__id_compare(&lhs->from, &rhs->from);
}

/* Order the data collected in CONTEXT such that we can place them in the
 * desired order.
 */
static void
sort_reps(pack_context_t *context)
{
  svn_sort__array(context->path_order,
                  (int (*)(const void *, const void *))compare_path_order);
  svn_sort__array(context->references,
                  (int (*)(const void *, const void *))compare_references);
}

/* Return the remaining unused bytes in the current block in CONTEXT's
 * pack file.
 */
static apr_off_t
get_block_left(pack_context_t *context)
{
  svn_fs_x__data_t *ffd = context->fs->fsap_data;
  return ffd->block_size - (context->pack_offset % ffd->block_size);
}

/* To prevent items from overlapping a block boundary, we will usually
 * put them into the next block and top up the old one with NUL bytes.
 * Pad CONTEXT's pack file to the end of the current block, if that padding
 * is short enough.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
auto_pad_block(pack_context_t *context,
               apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = context->fs->fsap_data;

  /* This is the maximum number of bytes "wasted" that way per block.
   * Larger items will cross the block boundaries. */
  const apr_off_t max_padding = MAX(ffd->block_size / 50, 512);

  /* Is wasted space small enough to align the current item to the next
   * block? */
  apr_off_t padding = get_block_left(context);

  if (padding < max_padding)
    {
      /* Yes. To up with NUL bytes and don't forget to create
       * an P2L index entry marking this section as unused. */
      svn_fs_x__p2l_entry_t null_entry;

      null_entry.offset = context->pack_offset;
      null_entry.size = padding;
      null_entry.type = SVN_FS_X__ITEM_TYPE_UNUSED;
      null_entry.fnv1_checksum = 0;
      null_entry.item_count = 0;
      null_entry.items = NULL;

      SVN_ERR(write_null_bytes(context->pack_file, padding, scratch_pool));
      SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
                  (context->proto_p2l_index, &null_entry, scratch_pool));
      context->pack_offset += padding;
    }

  return SVN_NO_ERROR;
}

/* Return the index of the first entry in CONTEXT->REFERENCES that
 * references ITEM->ITEMS[0] if such entries exist.  All matching items
 * will be consecutive.
 */
static int
find_first_reference(pack_context_t *context,
                     svn_fs_x__p2l_entry_t *item)
{
  int lower = 0;
  int upper = context->references->nelts - 1;

  while (lower <= upper)
    {
      int current = lower + (upper - lower) / 2;
      reference_t *reference
        = APR_ARRAY_IDX(context->references, current, reference_t *);

      if (svn_fs_x__id_compare(&reference->to, item->items) < 0)
        lower = current + 1;
      else
        upper = current - 1;
    }

  return lower;
}

/* Check whether entry number IDX in CONTEXT->REFERENCES references ITEM.
 */
static svn_boolean_t
is_reference_match(pack_context_t *context,
                   int idx,
                   svn_fs_x__p2l_entry_t *item)
{
  reference_t *reference;
  if (context->references->nelts <= idx)
    return FALSE;

  reference = APR_ARRAY_IDX(context->references, idx, reference_t *);
  return svn_fs_x__id_eq(&reference->to, item->items);
}

/* Starting at IDX in CONTEXT->PATH_ORDER, select all representations and
 * noderevs that should be placed into the same container, respectively.
 * Append the path_order_t * elements encountered in SELECTED, the
 * svn_fs_x__p2l_entry_t * of the representations that should be placed
 * into the same reps container will be appended to REP_PARTS and the
 * svn_fs_x__p2l_entry_t * of the noderevs referencing those reps will
 * be appended to NODE_PARTS.
 *
 * Remove all returned items from the CONTEXT->REPS container and prevent
 * them from being placed a second time later on.  That also means that the
 * caller has to place all items returned.
 */
static svn_error_t *
select_reps(pack_context_t *context,
            int idx,
            apr_array_header_t *selected,
            apr_array_header_t *node_parts,
            apr_array_header_t *rep_parts)
{
  apr_array_header_t *path_order = context->path_order;
  path_order_t *start_path = APR_ARRAY_IDX(path_order, idx, path_order_t *);

  svn_fs_x__p2l_entry_t *node_part;
  svn_fs_x__p2l_entry_t *rep_part;
  svn_fs_x__p2l_entry_t *depending;
  int i, k;

  /* collect all path_order records as well as rep and noderev items
   * that occupy the same path with the same node. */
  for (; idx < path_order->nelts; ++idx)
    {
      path_order_t *current_path
        = APR_ARRAY_IDX(path_order, idx, path_order_t *);

      if (!svn_fs_x__id_eq(&start_path->node_id, &current_path->node_id))
        break;

      APR_ARRAY_IDX(path_order, idx, path_order_t *) = NULL;
      node_part = get_item(context, &current_path->noderev_id, TRUE);
      rep_part = get_item(context, &current_path->rep_id, TRUE);

      if (node_part && rep_part)
        APR_ARRAY_PUSH(selected, path_order_t *) = current_path;

      if (node_part)
        APR_ARRAY_PUSH(node_parts, svn_fs_x__p2l_entry_t *) = node_part;
      if (rep_part)
        APR_ARRAY_PUSH(rep_parts, svn_fs_x__p2l_entry_t *) = rep_part;
    }

  /* collect depending reps and noderevs that reference any of the collected
   * reps */
  for (i = 0; i < rep_parts->nelts; ++i)
    {
      rep_part = APR_ARRAY_IDX(rep_parts, i, svn_fs_x__p2l_entry_t*);
      for (k = find_first_reference(context, rep_part);
           is_reference_match(context, k, rep_part);
           ++k)
        {
          reference_t *reference
            = APR_ARRAY_IDX(context->references, k, reference_t *);

          depending = get_item(context, &reference->from, TRUE);
          if (!depending)
            continue;

          if (depending->type == SVN_FS_X__ITEM_TYPE_NODEREV)
            APR_ARRAY_PUSH(node_parts, svn_fs_x__p2l_entry_t *) = depending;
          else
            APR_ARRAY_PUSH(rep_parts, svn_fs_x__p2l_entry_t *) = depending;
        }
    }

  return SVN_NO_ERROR;
}

/* Return TRUE, if all path_order_t * in SELECTED reference contents that is
 * not longer than LIMIT.
 */
static svn_boolean_t
reps_fit_into_containers(apr_array_header_t *selected,
                         apr_uint64_t limit)
{
  int i;
  for (i = 0; i < selected->nelts; ++i)
    if (APR_ARRAY_IDX(selected, i, path_order_t *)->expanded_size > limit)
      return FALSE;

  return TRUE;
}

/* Write the *CONTAINER containing the noderevs described by the
 * svn_fs_x__p2l_entry_t * in ITEMS to the pack file on CONTEXT.
 * Append a P2L entry for the container to CONTAINER->REPS.
 * Afterwards, clear ITEMS and re-allocate *CONTAINER in CONTAINER_POOL
 * so the caller may fill them again.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_nodes_container(pack_context_t *context,
                      svn_fs_x__noderevs_t **container,
                      apr_array_header_t *items,
                      apr_pool_t *container_pool,
                      apr_pool_t *scratch_pool)
{
  int i;
  apr_off_t offset = 0;
  svn_fs_x__p2l_entry_t *container_entry;
  svn_stream_t *pack_stream;

  if (items->nelts == 0)
    return SVN_NO_ERROR;

  /* serialize container */
  container_entry = apr_palloc(context->info_pool, sizeof(*container_entry));
  pack_stream = svn_checksum__wrap_write_stream_fnv1a_32x4
                                (&container_entry->fnv1_checksum,
                                 svn_stream_from_aprfile2(context->pack_file,
                                                          TRUE, scratch_pool),
                                 scratch_pool);
  SVN_ERR(svn_fs_x__write_noderevs_container(pack_stream, *container,
                                             scratch_pool));
  SVN_ERR(svn_stream_close(pack_stream));
  SVN_ERR(svn_io_file_seek(context->pack_file, APR_CUR, &offset,
                           scratch_pool));

  /* replace first noderev item in ENTRIES with the container
     and set all others to NULL */
  container_entry->offset = context->pack_offset;
  container_entry->size = offset - container_entry->offset;
  container_entry->type = SVN_FS_X__ITEM_TYPE_NODEREVS_CONT;
  container_entry->item_count = items->nelts;
  container_entry->items = apr_palloc(context->info_pool,
      sizeof(svn_fs_x__id_t) * container_entry->item_count);

  for (i = 0; i < items->nelts; ++i)
    container_entry->items[i]
      = APR_ARRAY_IDX(items, i, svn_fs_x__p2l_entry_t *)->items[0];

  context->pack_offset = offset;
  APR_ARRAY_PUSH(context->reps, svn_fs_x__p2l_entry_t *)
    = container_entry;

  /* Write P2L index for copied items, i.e. the 1 container */
  SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
            (context->proto_p2l_index, container_entry, scratch_pool));

  svn_pool_clear(container_pool);
  *container = svn_fs_x__noderevs_create(16, container_pool);
  apr_array_clear(items);

  return SVN_NO_ERROR;
}

/* Read the noderevs given by the svn_fs_x__p2l_entry_t * in NODE_PARTS
 * from TEMP_FILE and add them to *CONTAINER and NODES_IN_CONTAINER.
 * Whenever the container grows bigger than the current block in CONTEXT,
 * write the data to disk and continue in the next block.
 *
 * Use CONTAINER_POOL to re-allocate the *CONTAINER as necessary and
 * SCRATCH_POOL to temporary allocations.
 */
static svn_error_t *
store_nodes(pack_context_t *context,
            apr_file_t *temp_file,
            apr_array_header_t *node_parts,
            svn_fs_x__noderevs_t **container,
            apr_array_header_t *nodes_in_container,
            apr_pool_t *container_pool,
            apr_pool_t *scratch_pool)
{
  int i;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_stream_t *stream
    = svn_stream_from_aprfile2(temp_file, TRUE, scratch_pool);

  /* number of bytes in the current block not being spent on fixed-size
     items (i.e. those not put into the container). */
  apr_size_t capacity_left = get_block_left(context);

  /* Estimated noderev container size */
  apr_size_t last_container_size = 0, container_size = 0;

  /* Estimate extra capacity we will gain from container compression. */
  apr_size_t pack_savings = 0;
  for (i = 0; i < node_parts->nelts; ++i)
    {
      svn_fs_x__noderev_t *noderev;
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(node_parts, i, svn_fs_x__p2l_entry_t *);

      /* if we reached the limit, check whether we saved some space
         through the container. */
      if (capacity_left + pack_savings < container_size + entry->size)
        container_size = svn_fs_x__noderevs_estimate_size(*container);

      /* If necessary and the container is large enough, try harder
         by actually serializing the container and determine current
         savings due to compression. */
      if (   capacity_left + pack_savings < container_size + entry->size
          && container_size > last_container_size + 2000)
        {
          svn_stringbuf_t *serialized
            = svn_stringbuf_create_ensure(container_size, iterpool);
          svn_stream_t *temp_stream
            = svn_stream_from_stringbuf(serialized, iterpool);

          SVN_ERR(svn_fs_x__write_noderevs_container(temp_stream, *container,
                                                     iterpool));
          SVN_ERR(svn_stream_close(temp_stream));

          last_container_size = container_size;
          pack_savings = container_size - serialized->len;
        }

      /* still doesn't fit? -> block is full. Flush */
      if (   capacity_left + pack_savings < container_size + entry->size
          && nodes_in_container->nelts < 2)
        {
          SVN_ERR(auto_pad_block(context, iterpool));
          capacity_left = get_block_left(context);
        }

      /* still doesn't fit? -> block is full. Flush */
      if (capacity_left + pack_savings < container_size + entry->size)
        {
          SVN_ERR(write_nodes_container(context, container,
                                        nodes_in_container, container_pool,
                                        iterpool));

          capacity_left = get_block_left(context);
          pack_savings = 0;
          container_size = 0;
        }

      /* item will fit into the block. */
      SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &entry->offset, iterpool));
      SVN_ERR(svn_fs_x__read_noderev(&noderev, stream, iterpool, iterpool));
      svn_fs_x__noderevs_add(*container, noderev);

      container_size += entry->size;
      APR_ARRAY_PUSH(nodes_in_container, svn_fs_x__p2l_entry_t *) = entry;

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Finalize CONTAINER and write it to CONTEXT's pack file.
 * Append an P2L entry containing the given SUB_ITEMS to NEW_ENTRIES.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_reps_container(pack_context_t *context,
                     svn_fs_x__reps_builder_t *container,
                     apr_array_header_t *sub_items,
                     apr_array_header_t *new_entries,
                     apr_pool_t *scratch_pool)
{
  apr_off_t offset = 0;
  svn_fs_x__p2l_entry_t container_entry;

  svn_stream_t *pack_stream
    = svn_checksum__wrap_write_stream_fnv1a_32x4
                                (&container_entry.fnv1_checksum,
                                 svn_stream_from_aprfile2(context->pack_file,
                                                          TRUE, scratch_pool),
                                 scratch_pool);

  SVN_ERR(svn_fs_x__write_reps_container(pack_stream, container,
                                         scratch_pool));
  SVN_ERR(svn_stream_close(pack_stream));
  SVN_ERR(svn_io_file_seek(context->pack_file, APR_CUR, &offset,
                           scratch_pool));

  container_entry.offset = context->pack_offset;
  container_entry.size = offset - container_entry.offset;
  container_entry.type = SVN_FS_X__ITEM_TYPE_REPS_CONT;
  container_entry.item_count = sub_items->nelts;
  container_entry.items = (svn_fs_x__id_t *)sub_items->elts;

  context->pack_offset = offset;
  APR_ARRAY_PUSH(new_entries, svn_fs_x__p2l_entry_t *)
    = svn_fs_x__p2l_entry_dup(&container_entry, context->info_pool);

  SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
            (context->proto_p2l_index, &container_entry, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the (property) representations identified by svn_fs_x__p2l_entry_t
 * elements in ENTRIES from TEMP_FILE, aggregate them and write them into
 * CONTEXT->PACK_FILE.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_reps_containers(pack_context_t *context,
                      apr_array_header_t *entries,
                      apr_file_t *temp_file,
                      apr_array_header_t *new_entries,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *container_pool = svn_pool_create(scratch_pool);
  int i;

  apr_ssize_t block_left = get_block_left(context);

  svn_fs_x__reps_builder_t *container
    = svn_fs_x__reps_builder_create(context->fs, container_pool);
  apr_array_header_t *sub_items
    = apr_array_make(scratch_pool, 64, sizeof(svn_fs_x__id_t));
  svn_fs_x__revision_file_t *file;

  SVN_ERR(svn_fs_x__rev_file_wrap_temp(&file, context->fs, temp_file,
                                       scratch_pool));

  /* copy all items in strict order */
  for (i = entries->nelts-1; i >= 0; --i)
    {
      svn_fs_x__representation_t representation = { 0 };
      svn_stringbuf_t *contents;
      svn_stream_t *stream;
      apr_size_t list_index;
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t *);

      if ((block_left < entry->size) && sub_items->nelts)
        {
          block_left = get_block_left(context)
                     - svn_fs_x__reps_estimate_size(container);
        }

      if ((block_left < entry->size) && sub_items->nelts)
        {
          SVN_ERR(write_reps_container(context, container, sub_items,
                                       new_entries, iterpool));

          apr_array_clear(sub_items);
          svn_pool_clear(container_pool);
          container = svn_fs_x__reps_builder_create(context->fs,
                                                    container_pool);
          block_left = get_block_left(context);
        }

      /* still enough space in current block? */
      if (block_left < entry->size)
        {
          SVN_ERR(auto_pad_block(context, iterpool));
          block_left = get_block_left(context);
        }

      assert(entry->item_count == 1);
      representation.id = entry->items[0];

      /* select the change list in the source file, parse it and add it to
       * the container */
      SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &entry->offset,
                               iterpool));
      SVN_ERR(svn_fs_x__get_representation_length(&representation.size,
                                             &representation.expanded_size,
                                             context->fs, file,
                                             entry, iterpool));
      SVN_ERR(svn_fs_x__get_contents(&stream, context->fs, &representation,
                                     FALSE, iterpool));
      contents = svn_stringbuf_create_ensure(representation.expanded_size,
                                             iterpool);
      contents->len = representation.expanded_size;

      /* The representation is immutable.  Read it normally. */
      SVN_ERR(svn_stream_read_full(stream, contents->data, &contents->len));
      SVN_ERR(svn_stream_close(stream));

      SVN_ERR(svn_fs_x__reps_add(&list_index, container,
                                 svn_stringbuf__morph_into_string(contents)));
      SVN_ERR_ASSERT(list_index == sub_items->nelts);
      block_left -= entry->size;

      APR_ARRAY_PUSH(sub_items, svn_fs_x__id_t) = entry->items[0];

      svn_pool_clear(iterpool);
    }

  if (sub_items->nelts)
    SVN_ERR(write_reps_container(context, container, sub_items,
                                 new_entries, iterpool));

  svn_pool_destroy(iterpool);
  svn_pool_destroy(container_pool);

  return SVN_NO_ERROR;
}

/* Return TRUE if the estimated size of the NODES_IN_CONTAINER plus the
 * representations given as svn_fs_x__p2l_entry_t * in ENTRIES may exceed
 * the space left in the current block.
 */
static svn_boolean_t
should_flush_nodes_container(pack_context_t *context,
                             svn_fs_x__noderevs_t *nodes_container,
                             apr_array_header_t *entries)
{
  apr_ssize_t block_left = get_block_left(context);
  apr_ssize_t rep_sum = 0;
  apr_ssize_t container_size
    = svn_fs_x__noderevs_estimate_size(nodes_container);

  int i;
  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t *);
      rep_sum += entry->size;
    }

  return block_left < rep_sum + container_size;
}

/* Read the contents of the first COUNT non-NULL, non-empty items in ITEMS
 * from TEMP_FILE and write them to CONTEXT->PACK_FILE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
store_items(pack_context_t *context,
            apr_file_t *temp_file,
            apr_array_header_t *items,
            int count,
            apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* copy all items in strict order */
  for (i = 0; i < count; ++i)
    {
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(items, i, svn_fs_x__p2l_entry_t *);
      if (!entry
          || entry->type == SVN_FS_X__ITEM_TYPE_UNUSED
          || entry->item_count == 0)
        continue;

      /* select the item in the source file and copy it into the target
       * pack file */
      SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &entry->offset,
                               iterpool));
      SVN_ERR(copy_file_data(context, context->pack_file, temp_file,
                             entry->size, iterpool));

      /* write index entry and update current position */
      entry->offset = context->pack_offset;
      context->pack_offset += entry->size;

      SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
                  (context->proto_p2l_index, entry, iterpool));

      APR_ARRAY_PUSH(context->reps, svn_fs_x__p2l_entry_t *) = entry;
      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Copy (append) the items identified by svn_fs_x__p2l_entry_t * elements
 * in ENTRIES strictly in order from TEMP_FILE into CONTEXT->PACK_FILE.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
copy_reps_from_temp(pack_context_t *context,
                    apr_file_t *temp_file,
                    apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = context->fs->fsap_data;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *container_pool = svn_pool_create(scratch_pool);
  apr_array_header_t *path_order = context->path_order;
  apr_array_header_t *reps = context->reps;
  apr_array_header_t *selected = apr_array_make(scratch_pool, 16,
                                                path_order->elt_size);
  apr_array_header_t *node_parts = apr_array_make(scratch_pool, 16,
                                                  reps->elt_size);
  apr_array_header_t *rep_parts = apr_array_make(scratch_pool, 16,
                                                 reps->elt_size);
  apr_array_header_t *nodes_in_container = apr_array_make(scratch_pool, 16,
                                                          reps->elt_size);
  int i, k;
  int initial_reps_count = reps->nelts;

  /* 1 container for all noderevs in the current block.  We will try to
   * not write it to disk until the current block fills up, i.e. aim for
   * a single noderevs container per block. */
  svn_fs_x__noderevs_t *nodes_container
    = svn_fs_x__noderevs_create(16, container_pool);

  /* copy items in path order. Create block-sized containers. */
  for (i = 0; i < path_order->nelts; ++i)
    {
      if (APR_ARRAY_IDX(path_order, i, path_order_t *) == NULL)
        continue;

      /* Collect reps to combine and all noderevs referencing them */
      SVN_ERR(select_reps(context, i, selected, node_parts, rep_parts));

      /* store the noderevs container in front of the reps */
      SVN_ERR(store_nodes(context, temp_file, node_parts, &nodes_container,
                          nodes_in_container, container_pool, iterpool));

      /* actually flush the noderevs to disk if the reps container is likely
       * to fill the block, i.e. no further noderevs will be added to the
       * nodes container. */
      if (should_flush_nodes_container(context, nodes_container, node_parts))
        SVN_ERR(write_nodes_container(context, &nodes_container,
                                      nodes_in_container, container_pool,
                                      iterpool));

      /* if all reps are short enough put them into one container.
       * Otherwise, just store all containers here. */
      if (reps_fit_into_containers(selected, 2 * ffd->block_size))
        SVN_ERR(write_reps_containers(context, rep_parts, temp_file,
                                      context->reps, iterpool));
      else
        SVN_ERR(store_items(context, temp_file, rep_parts, rep_parts->nelts,
                            iterpool));

      /* processed all items */
      apr_array_clear(selected);
      apr_array_clear(node_parts);
      apr_array_clear(rep_parts);

      svn_pool_clear(iterpool);
    }

  /* flush noderevs container to disk */
  if (nodes_in_container->nelts)
    SVN_ERR(write_nodes_container(context, &nodes_container,
                                  nodes_in_container, container_pool,
                                  iterpool));

  /* copy all items in strict order */
  SVN_ERR(store_items(context, temp_file, reps, initial_reps_count,
                      scratch_pool));

  /* vaccum ENTRIES array: eliminate NULL entries */
  for (i = 0, k = 0; i < reps->nelts; ++i)
    {
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(reps, i, svn_fs_x__p2l_entry_t *);
      if (entry)
        {
          APR_ARRAY_IDX(reps, k, svn_fs_x__p2l_entry_t *) = entry;
          ++k;
        }
    }
  reps->nelts = k;

  svn_pool_destroy(iterpool);
  svn_pool_destroy(container_pool);

  return SVN_NO_ERROR;
}

/* Finalize CONTAINER and write it to CONTEXT's pack file.
 * Append an P2L entry containing the given SUB_ITEMS to NEW_ENTRIES.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_changes_container(pack_context_t *context,
                        svn_fs_x__changes_t *container,
                        apr_array_header_t *sub_items,
                        apr_array_header_t *new_entries,
                        apr_pool_t *scratch_pool)
{
  apr_off_t offset = 0;
  svn_fs_x__p2l_entry_t container_entry;

  svn_stream_t *pack_stream
    = svn_checksum__wrap_write_stream_fnv1a_32x4
                                (&container_entry.fnv1_checksum,
                                 svn_stream_from_aprfile2(context->pack_file,
                                                          TRUE, scratch_pool),
                                 scratch_pool);

  SVN_ERR(svn_fs_x__write_changes_container(pack_stream,
                                             container,
                                             scratch_pool));
  SVN_ERR(svn_stream_close(pack_stream));
  SVN_ERR(svn_io_file_seek(context->pack_file, APR_CUR, &offset,
                           scratch_pool));

  container_entry.offset = context->pack_offset;
  container_entry.size = offset - container_entry.offset;
  container_entry.type = SVN_FS_X__ITEM_TYPE_CHANGES_CONT;
  container_entry.item_count = sub_items->nelts;
  container_entry.items = (svn_fs_x__id_t *)sub_items->elts;

  context->pack_offset = offset;
  APR_ARRAY_PUSH(new_entries, svn_fs_x__p2l_entry_t *)
    = svn_fs_x__p2l_entry_dup(&container_entry, context->info_pool);

  SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
            (context->proto_p2l_index, &container_entry, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the change lists identified by svn_fs_x__p2l_entry_t * elements
 * in ENTRIES strictly in from TEMP_FILE, aggregate them and write them
 * into CONTEXT->PACK_FILE.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_changes_containers(pack_context_t *context,
                         apr_array_header_t *entries,
                         apr_file_t *temp_file,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *container_pool = svn_pool_create(scratch_pool);
  int i;

  apr_ssize_t block_left = get_block_left(context);
  apr_ssize_t estimated_addition = 0;

  svn_fs_x__changes_t *container
    = svn_fs_x__changes_create(1000, container_pool);
  apr_array_header_t *sub_items
    = apr_array_make(scratch_pool, 64, sizeof(svn_fs_x__id_t));
  apr_array_header_t *new_entries
    = apr_array_make(context->info_pool, 16, entries->elt_size);
  svn_stream_t *temp_stream
    = svn_stream_from_aprfile2(temp_file, TRUE, scratch_pool);

  /* copy all items in strict order */
  for (i = entries->nelts-1; i >= 0; --i)
    {
      apr_array_header_t *changes;
      apr_size_t list_index;
      svn_fs_x__p2l_entry_t *entry
        = APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t *);

      /* zip compression alone will significantly reduce the size of large
       * change lists. So, we will probably need even less than this estimate.
       */
      apr_ssize_t estimated_size = (entry->size / 5) + 250;

      /* If necessary and enough data has been added to the container since
       * the last test, try harder by actually serializing the container and
       * determine current savings due to compression. */
      if (block_left < estimated_size && estimated_addition > 2000)
        {
          svn_stringbuf_t *serialized
            = svn_stringbuf_create_ensure(get_block_left(context), iterpool);
          svn_stream_t *memory_stream
            = svn_stream_from_stringbuf(serialized, iterpool);

          SVN_ERR(svn_fs_x__write_changes_container(memory_stream,
                                                     container, iterpool));
          SVN_ERR(svn_stream_close(temp_stream));

          block_left = get_block_left(context) - serialized->len;
          estimated_addition = 0;
        }

      if ((block_left < estimated_size) && sub_items->nelts)
        {
          SVN_ERR(write_changes_container(context, container, sub_items,
                                          new_entries, iterpool));

          apr_array_clear(sub_items);
          svn_pool_clear(container_pool);
          container = svn_fs_x__changes_create(1000, container_pool);
          block_left = get_block_left(context);
          estimated_addition = 0;
        }

      /* still enough space in current block? */
      if (block_left < estimated_size)
        {
          SVN_ERR(auto_pad_block(context, iterpool));
          block_left = get_block_left(context);
        }

      /* select the change list in the source file, parse it and add it to
       * the container */
      SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &entry->offset,
                               iterpool));
      SVN_ERR(svn_fs_x__read_changes(&changes, temp_stream, INT_MAX,
                                     scratch_pool, iterpool));
      SVN_ERR(svn_fs_x__changes_append_list(&list_index, container, changes));
      SVN_ERR_ASSERT(list_index == sub_items->nelts);
      block_left -= estimated_size;
      estimated_addition += estimated_size;

      APR_ARRAY_PUSH(sub_items, svn_fs_x__id_t) = entry->items[0];

      svn_pool_clear(iterpool);
    }

  if (sub_items->nelts)
    SVN_ERR(write_changes_container(context, container, sub_items,
                                    new_entries, iterpool));

  *entries = *new_entries;
  svn_pool_destroy(iterpool);
  svn_pool_destroy(container_pool);

  return SVN_NO_ERROR;
}

/* Read the (property) representations identified by svn_fs_x__p2l_entry_t
 * elements in ENTRIES from TEMP_FILE, aggregate them and write them into
 * CONTEXT->PACK_FILE.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
write_property_containers(pack_context_t *context,
                          apr_array_header_t *entries,
                          apr_file_t *temp_file,
                          apr_pool_t *scratch_pool)
{
  apr_array_header_t *new_entries
    = apr_array_make(context->info_pool, 16, entries->elt_size);

  SVN_ERR(write_reps_containers(context, entries, temp_file, new_entries,
                                scratch_pool));

  *entries = *new_entries;

  return SVN_NO_ERROR;
}

/* Append all entries of svn_fs_x__p2l_entry_t * array TO_APPEND to
 * svn_fs_x__p2l_entry_t * array DEST.
 */
static void
append_entries(apr_array_header_t *dest,
               apr_array_header_t *to_append)
{
  int i;
  for (i = 0; i < to_append->nelts; ++i)
    APR_ARRAY_PUSH(dest, svn_fs_x__p2l_entry_t *)
      = APR_ARRAY_IDX(to_append, i, svn_fs_x__p2l_entry_t *);
}

/* Write the log-to-phys proto index file for CONTEXT and use POOL for
 * temporary allocations.  All items in all buckets must have been placed
 * by now.
 */
static svn_error_t *
write_l2p_index(pack_context_t *context,
                apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  const char *temp_name;
  const char *proto_index;
  apr_off_t offset = 0;

  /* lump all items into one bucket.  As target, use the bucket that
   * probably has the most entries already. */
  append_entries(context->reps, context->changes);
  append_entries(context->reps, context->file_props);
  append_entries(context->reps, context->dir_props);

  /* Let the index code do the expensive L2P -> P2L transformation. */
  SVN_ERR(svn_fs_x__l2p_index_from_p2l_entries(&temp_name,
                                               context->fs,
                                               context->reps,
                                               pool, scratch_pool));

  /* Append newly written segment to exisiting proto index file. */
  SVN_ERR(svn_io_file_name_get(&proto_index, context->proto_l2p_index,
                               scratch_pool));

  SVN_ERR(svn_io_file_flush(context->proto_l2p_index, scratch_pool));
  SVN_ERR(svn_io_append_file(temp_name, proto_index, scratch_pool));
  SVN_ERR(svn_io_remove_file2(temp_name, FALSE, scratch_pool));
  SVN_ERR(svn_io_file_seek(context->proto_l2p_index, APR_END, &offset,
                           scratch_pool));

  /* Done. */
  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

/* Pack the current revision range of CONTEXT, i.e. this covers phases 2
 * to 4.  Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
pack_range(pack_context_t *context,
           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = context->fs->fsap_data;
  apr_pool_t *revpool = svn_pool_create(scratch_pool);
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Phase 2: Copy items into various buckets and build tracking info */
  svn_revnum_t revision;
  for (revision = context->start_rev; revision < context->end_rev; ++revision)
    {
      apr_off_t offset = 0;
      svn_fs_x__revision_file_t *rev_file;
      svn_fs_x__index_info_t l2p_index_info;

      /* Get the rev file dimensions (mainly index locations). */
      SVN_ERR(svn_fs_x__rev_file_init(&rev_file, context->fs, revision,
                                      revpool));
      SVN_ERR(svn_fs_x__rev_file_l2p_info(&l2p_index_info, rev_file));

      /* store the indirect array index */
      APR_ARRAY_PUSH(context->rev_offsets, int) = context->reps->nelts;

      /* read the phys-to-log index file until we covered the whole rev file.
       * That index contains enough info to build both target indexes from it. */
      while (offset < l2p_index_info.start)
        {
          /* read one cluster */
          int i;
          apr_array_header_t *entries;
          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_x__p2l_index_lookup(&entries, context->fs,
                                             rev_file, revision, offset,
                                             ffd->p2l_page_size, iterpool,
                                             iterpool));

          for (i = 0; i < entries->nelts; ++i)
            {
              svn_fs_x__p2l_entry_t *entry
                = &APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t);

              /* skip first entry if that was duplicated due crossing a
                 cluster boundary */
              if (offset > entry->offset)
                continue;

              /* process entry while inside the rev file */
              offset = entry->offset;
              if (offset < l2p_index_info.start)
                {
                  SVN_ERR(svn_fs_x__rev_file_seek(rev_file, NULL, offset));

                  if (entry->type == SVN_FS_X__ITEM_TYPE_CHANGES)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->changes,
                                              context->changes_file,
                                              rev_file, entry, iterpool));
                  else if (entry->type == SVN_FS_X__ITEM_TYPE_FILE_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->file_props,
                                              context->file_props_file,
                                              rev_file, entry, iterpool));
                  else if (entry->type == SVN_FS_X__ITEM_TYPE_DIR_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->dir_props,
                                              context->dir_props_file,
                                              rev_file, entry, iterpool));
                  else if (   entry->type == SVN_FS_X__ITEM_TYPE_FILE_REP
                           || entry->type == SVN_FS_X__ITEM_TYPE_DIR_REP)
                    SVN_ERR(copy_rep_to_temp(context, rev_file, entry,
                                             iterpool));
                  else if (entry->type == SVN_FS_X__ITEM_TYPE_NODEREV)
                    SVN_ERR(copy_node_to_temp(context, rev_file, entry,
                                              iterpool));
                  else
                    SVN_ERR_ASSERT(entry->type == SVN_FS_X__ITEM_TYPE_UNUSED);

                  offset += entry->size;
                }
            }

          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));
        }

      svn_pool_clear(revpool);
    }

  svn_pool_destroy(iterpool);

  /* phase 3: placement.
   * Use "newest first" placement for simple items. */
  sort_items(context->changes);
  sort_items(context->file_props);
  sort_items(context->dir_props);

  /* follow dependencies recursively for noderevs and data representations */
  sort_reps(context);

  /* phase 4: copy bucket data to pack file.  Write P2L index. */
  SVN_ERR(write_changes_containers(context, context->changes,
                                   context->changes_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(write_property_containers(context, context->file_props,
                                    context->file_props_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(write_property_containers(context, context->dir_props,
                                    context->dir_props_file, revpool));
  svn_pool_clear(revpool);
  SVN_ERR(copy_reps_from_temp(context, context->reps_file, revpool));
  svn_pool_clear(revpool);

  /* write L2P index as well (now that we know all target offsets) */
  SVN_ERR(write_l2p_index(context, revpool));

  svn_pool_destroy(revpool);

  return SVN_NO_ERROR;
}

/* Append CONTEXT->START_REV to the context's pack file with no re-ordering.
 * This function will only be used for very large revisions (>>100k changes).
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
append_revision(pack_context_t *context,
                apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = context->fs->fsap_data;
  apr_off_t offset = 0;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_fs_x__revision_file_t *rev_file;
  apr_file_t *file;
  svn_filesize_t revdata_size;

  /* Copy all non-index contents the rev file to the end of the pack file. */
  SVN_ERR(svn_fs_x__rev_file_init(&rev_file, context->fs, context->start_rev,
                                  scratch_pool));
  SVN_ERR(svn_fs_x__rev_file_data_size(&revdata_size, rev_file));

  SVN_ERR(svn_fs_x__rev_file_get(&file, rev_file));
  SVN_ERR(svn_io_file_aligned_seek(file, ffd->block_size, NULL, 0,
                                   iterpool));
  SVN_ERR(copy_file_data(context, context->pack_file, file, revdata_size,
                         iterpool));

  /* mark the start of a new revision */
  SVN_ERR(svn_fs_x__l2p_proto_index_add_revision(context->proto_l2p_index,
                                                 scratch_pool));

  /* read the phys-to-log index file until we covered the whole rev file.
   * That index contains enough info to build both target indexes from it. */
  while (offset < revdata_size)
    {
      /* read one cluster */
      int i;
      apr_array_header_t *entries;
      SVN_ERR(svn_fs_x__p2l_index_lookup(&entries, context->fs, rev_file,
                                         context->start_rev, offset,
                                         ffd->p2l_page_size, iterpool,
                                         iterpool));

      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_x__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_x__p2l_entry_t);

          /* skip first entry if that was duplicated due crossing a
             cluster boundary */
          if (offset > entry->offset)
            continue;

          /* process entry while inside the rev file */
          offset = entry->offset;
          if (offset < revdata_size)
            {
              /* there should be true containers */
              SVN_ERR_ASSERT(entry->item_count == 1);

              entry->offset += context->pack_offset;
              offset += entry->size;
              SVN_ERR(svn_fs_x__l2p_proto_index_add_entry
                        (context->proto_l2p_index, entry->offset, 0,
                         entry->items[0].number, iterpool));
              SVN_ERR(svn_fs_x__p2l_proto_index_add_entry
                        (context->proto_p2l_index, entry, iterpool));
            }
        }

      svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);
  context->pack_offset += revdata_size;

  return SVN_NO_ERROR;
}

/* Format 7 packing logic.
 *
 * Pack the revision shard starting at SHARD_REV in filesystem FS from
 * SHARD_DIR into the PACK_FILE_DIR, using SCRATCH_POOL for temporary
 * allocations.  Limit the extra memory consumption to MAX_MEM bytes.
 * CANCEL_FUNC and CANCEL_BATON are what you think they are.
 * Schedule necessary fsync calls in BATCH.
 */
static svn_error_t *
pack_log_addressed(svn_fs_t *fs,
                   const char *pack_file_dir,
                   const char *shard_dir,
                   svn_revnum_t shard_rev,
                   apr_size_t max_mem,
                   svn_fs_x__batch_fsync_t *batch,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  enum
    {
      /* estimated amount of memory used to represent one item in memory
       * during rev file packing */
      PER_ITEM_MEM = APR_ALIGN_DEFAULT(sizeof(path_order_t))
                   + APR_ALIGN_DEFAULT(2 *sizeof(void*))
                   + APR_ALIGN_DEFAULT(sizeof(reference_t))
                   + APR_ALIGN_DEFAULT(sizeof(svn_fs_x__p2l_entry_t))
                   + 6 * sizeof(void*)
    };

  int max_items = max_mem / PER_ITEM_MEM > INT_MAX
                ? INT_MAX
                : (int)(max_mem / PER_ITEM_MEM);
  apr_array_header_t *max_ids;
  pack_context_t context = { 0 };
  int i;
  apr_size_t item_count = 0;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* set up a pack context */
  SVN_ERR(initialize_pack_context(&context, fs, pack_file_dir, shard_dir,
                                  shard_rev, max_items, batch, cancel_func,
                                  cancel_baton, scratch_pool));

  /* phase 1: determine the size of the revisions to pack */
  SVN_ERR(svn_fs_x__l2p_get_max_ids(&max_ids, fs, shard_rev,
                                    context.shard_end_rev - shard_rev,
                                    scratch_pool, scratch_pool));

  /* pack revisions in ranges that don't exceed MAX_MEM */
  for (i = 0; i < max_ids->nelts; ++i)
    if (   APR_ARRAY_IDX(max_ids, i, apr_uint64_t)
        <= (apr_uint64_t)max_items - item_count)
      {
        item_count += APR_ARRAY_IDX(max_ids, i, apr_uint64_t);
        context.end_rev++;
      }
    else
      {
        /* some unpacked revisions before this one? */
        if (context.start_rev < context.end_rev)
          {
            /* pack them intelligently (might be just 1 rev but still ...) */
            SVN_ERR(pack_range(&context, iterpool));
            SVN_ERR(reset_pack_context(&context, iterpool));
            item_count = 0;
          }

        /* next revision range is to start with the current revision */
        context.start_rev = i + context.shard_rev;
        context.end_rev = context.start_rev + 1;

        /* if this is a very large revision, we must place it as is */
        if (APR_ARRAY_IDX(max_ids, i, apr_uint64_t) > max_items)
          {
            SVN_ERR(append_revision(&context, iterpool));
            context.start_rev++;
          }
        else
          item_count += (apr_size_t)APR_ARRAY_IDX(max_ids, i, apr_uint64_t);

        svn_pool_clear(iterpool);
      }

  /* non-empty revision range at the end? */
  if (context.start_rev < context.end_rev)
    SVN_ERR(pack_range(&context, iterpool));

  /* last phase: finalize indexes and clean up */
  SVN_ERR(reset_pack_context(&context, iterpool));
  SVN_ERR(close_pack_context(&context, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* In filesystem FS, pack the revision SHARD containing exactly
 * MAX_FILES_PER_DIR revisions from SHARD_PATH into the PACK_FILE_DIR,
 * using SCRATCH_POOL for temporary allocations.  Try to limit the amount of
 * temporary memory needed to MAX_MEM bytes.  CANCEL_FUNC and CANCEL_BATON
 * are what you think they are.  Schedule necessary fsync calls in BATCH.
 *
 * If for some reason we detect a partial packing already performed, we
 * remove the pack file and start again.
 *
 * The actual packing will be done in a format-specific sub-function.
 */
static svn_error_t *
pack_rev_shard(svn_fs_t *fs,
               const char *pack_file_dir,
               const char *shard_path,
               apr_int64_t shard,
               int max_files_per_dir,
               apr_size_t max_mem,
               svn_fs_x__batch_fsync_t *batch,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  const char *pack_file_path;
  svn_revnum_t shard_rev = (svn_revnum_t) (shard * max_files_per_dir);

  /* Some useful paths. */
  pack_file_path = svn_dirent_join(pack_file_dir, PATH_PACKED, scratch_pool);

  /* Remove any existing pack file for this shard, since it is incomplete. */
  SVN_ERR(svn_io_remove_dir2(pack_file_dir, TRUE, cancel_func, cancel_baton,
                             scratch_pool));

  /* Create the new directory and pack file. */
  SVN_ERR(svn_io_dir_make(pack_file_dir, APR_OS_DEFAULT, scratch_pool));
  SVN_ERR(svn_fs_x__batch_fsync_new_path(batch, pack_file_dir, scratch_pool));

  /* Index information files */
  SVN_ERR(pack_log_addressed(fs, pack_file_dir, shard_path, shard_rev,
                             max_mem, batch, cancel_func, cancel_baton,
                             scratch_pool));

  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, scratch_pool));
  SVN_ERR(svn_io_set_file_read_only(pack_file_path, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

/* In the file system at FS_PATH, pack the SHARD in DIR containing exactly
 * MAX_FILES_PER_DIR revisions, using SCRATCH_POOL temporary for allocations.
 * COMPRESSION_LEVEL and MAX_PACK_SIZE will be ignored in that case.
 * An attempt will be made to keep memory usage below MAX_MEM.
 *
 * CANCEL_FUNC and CANCEL_BATON are what you think they are; similarly
 * NOTIFY_FUNC and NOTIFY_BATON.
 *
 * If for some reason we detect a partial packing already performed, we
 * remove the pack file and start again.
 */
static svn_error_t *
pack_shard(const char *dir,
           svn_fs_t *fs,
           apr_int64_t shard,
           int max_files_per_dir,
           apr_off_t max_pack_size,
           int compression_level,
           apr_size_t max_mem,
           svn_fs_pack_notify_t notify_func,
           void *notify_baton,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  const char *shard_path, *pack_file_dir;
  svn_fs_x__batch_fsync_t *batch;

  /* Notify caller we're starting to pack this shard. */
  if (notify_func)
    SVN_ERR(notify_func(notify_baton, shard, svn_fs_pack_notify_start,
                        scratch_pool));

  /* Perform all fsyncs through this instance. */
  SVN_ERR(svn_fs_x__batch_fsync_create(&batch, ffd->flush_to_disk,
                                       scratch_pool));

  /* Some useful paths. */
  pack_file_dir = svn_dirent_join(dir,
                  apr_psprintf(scratch_pool,
                               "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                               shard),
                  scratch_pool);
  shard_path = svn_dirent_join(dir,
                      apr_psprintf(scratch_pool, "%" APR_INT64_T_FMT, shard),
                      scratch_pool);

  /* pack the revision content */
  SVN_ERR(pack_rev_shard(fs, pack_file_dir, shard_path,
                         shard, max_files_per_dir, max_mem, batch,
                         cancel_func, cancel_baton, scratch_pool));

  /* pack the revprops in an equivalent way */
  SVN_ERR(svn_fs_x__pack_revprops_shard(fs,
                                        pack_file_dir,
                                        shard_path,
                                        shard, max_files_per_dir,
                                        (int)(0.9 * max_pack_size),
                                        compression_level, batch,
                                        cancel_func, cancel_baton,
                                        scratch_pool));

  /* Update the min-unpacked-rev file to reflect our newly packed shard. */
  SVN_ERR(svn_fs_x__write_min_unpacked_rev(fs,
                          (svn_revnum_t)((shard + 1) * max_files_per_dir),
                          scratch_pool));
  ffd->min_unpacked_rev = (svn_revnum_t)((shard + 1) * max_files_per_dir);

  /* Ensure that packed file is written to disk.*/
  SVN_ERR(svn_fs_x__batch_fsync_run(batch, scratch_pool));

  /* Finally, remove the existing shard directories. */
  SVN_ERR(svn_io_remove_dir2(shard_path, TRUE,
                             cancel_func, cancel_baton, scratch_pool));

  /* Notify caller we're starting to pack this shard. */
  if (notify_func)
    SVN_ERR(notify_func(notify_baton, shard, svn_fs_pack_notify_end,
                        scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the youngest rev and the first non-packed rev info for FS from disk.
   Set *FULLY_PACKED when there is no completed unpacked shard.
   Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_pack_status(svn_boolean_t *fully_packed,
                svn_fs_t *fs,
                apr_pool_t *scratch_pool)
{
  svn_fs_x__data_t *ffd = fs->fsap_data;
  apr_int64_t completed_shards;
  svn_revnum_t youngest;

  SVN_ERR(svn_fs_x__read_min_unpacked_rev(&ffd->min_unpacked_rev, fs,
                                          scratch_pool));

  SVN_ERR(svn_fs_x__youngest_rev(&youngest, fs, scratch_pool));
  completed_shards = (youngest + 1) / ffd->max_files_per_dir;

  /* See if we've already completed all possible shards thus far. */
  if (ffd->min_unpacked_rev == (completed_shards * ffd->max_files_per_dir))
    *fully_packed = TRUE;
  else
    *fully_packed = FALSE;

  return SVN_NO_ERROR;
}

typedef struct pack_baton_t
{
  svn_fs_t *fs;
  apr_size_t max_mem;
  svn_fs_pack_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
} pack_baton_t;


/* The work-horse for svn_fs_x__pack, called with the FS write lock.
   This implements the svn_fs_x__with_write_lock() 'body' callback
   type.  BATON is a 'pack_baton_t *'.

   WARNING: if you add a call to this function, please note:
     The code currently assumes that any piece of code running with
     the write-lock set can rely on the ffd->min_unpacked_rev and
     ffd->min_unpacked_revprop caches to be up-to-date (and, by
     extension, on not having to use a retry when calling
     svn_fs_x__path_rev_absolute() and friends).  If you add a call
     to this function, consider whether you have to call
     update_min_unpacked_rev().
     See this thread: http://thread.gmane.org/1291206765.3782.3309.camel@edith
 */
static svn_error_t *
pack_body(void *baton,
          apr_pool_t *scratch_pool)
{
  pack_baton_t *pb = baton;
  svn_fs_x__data_t *ffd = pb->fs->fsap_data;
  apr_int64_t completed_shards;
  apr_int64_t i;
  apr_pool_t *iterpool;
  const char *data_path;
  svn_boolean_t fully_packed;

  /* Since another process might have already packed the repo,
     we need to re-read the pack status. */
  SVN_ERR(get_pack_status(&fully_packed, pb->fs, scratch_pool));
  if (fully_packed)
    {
      if (pb->notify_func)
        (*pb->notify_func)(pb->notify_baton,
                           ffd->min_unpacked_rev / ffd->max_files_per_dir,
                           svn_fs_pack_notify_noop, scratch_pool);

      return SVN_NO_ERROR;
    }

  completed_shards = (ffd->youngest_rev_cache + 1) / ffd->max_files_per_dir;
  data_path = svn_dirent_join(pb->fs->path, PATH_REVS_DIR, scratch_pool);

  iterpool = svn_pool_create(scratch_pool);
  for (i = ffd->min_unpacked_rev / ffd->max_files_per_dir;
       i < completed_shards;
       i++)
    {
      svn_pool_clear(iterpool);

      if (pb->cancel_func)
        SVN_ERR(pb->cancel_func(pb->cancel_baton));

      SVN_ERR(pack_shard(data_path,
                         pb->fs, i, ffd->max_files_per_dir,
                         ffd->revprop_pack_size,
                         ffd->compress_packed_revprops
                           ? SVN__COMPRESSION_ZLIB_DEFAULT
                           : SVN__COMPRESSION_NONE,
                         pb->max_mem,
                         pb->notify_func, pb->notify_baton,
                         pb->cancel_func, pb->cancel_baton, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__pack(svn_fs_t *fs,
               apr_size_t max_mem,
               svn_fs_pack_notify_t notify_func,
               void *notify_baton,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  pack_baton_t pb = { 0 };
  svn_boolean_t fully_packed;

  /* Is there we even anything to do?. */
  SVN_ERR(get_pack_status(&fully_packed, fs, scratch_pool));
  if (fully_packed)
    {
      svn_fs_x__data_t *ffd = fs->fsap_data;

      if (notify_func)
        (*notify_func)(notify_baton,
                       ffd->min_unpacked_rev / ffd->max_files_per_dir,
                       svn_fs_pack_notify_noop, scratch_pool);

      return SVN_NO_ERROR;
    }

  /* Lock the repo and start the pack process. */
  pb.fs = fs;
  pb.notify_func = notify_func;
  pb.notify_baton = notify_baton;
  pb.cancel_func = cancel_func;
  pb.cancel_baton = cancel_baton;
  pb.max_mem = max_mem ? max_mem : DEFAULT_MAX_MEM;

  return svn_fs_x__with_pack_lock(fs, pack_body, &pb, scratch_pool);
}
