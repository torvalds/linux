/* pack.c --- FSFS shard packing functionality
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
#include <string.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_sorts.h"
#include "private/svn_temp_serializer.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_string_private.h"
#include "private/svn_io_private.h"

#include "fs_fs.h"
#include "pack.h"
#include "util.h"
#include "id.h"
#include "index.h"
#include "low_level.h"
#include "revprops.h"
#include "transaction.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"
#include "temp_serializer.h"

/* Logical addressing packing logic:
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
  svn_fs_fs__id_part_t node_id;

  /* when this change happened */
  svn_revnum_t revision;

  /* noderev predecessor count */
  int predecessor_count;

  /* this is a node is the latest for this PATH in this rev / pack file */
  svn_boolean_t is_head;

  /* length of the expanded representation content */
  apr_int64_t expanded_size;

  /* item ID of the noderev linked to the change. May be (0, 0). */
  svn_fs_fs__id_part_t noderev_id;

  /* item ID of the representation containing the new data. May be (0, 0). */
  svn_fs_fs__id_part_t rep_id;
} path_order_t;

/* Represents a reference from item FROM to item TO.  FROM may be a noderev
 * or rep_id while TO is (currently) always a representation.  We will sort
 * them by TO which allows us to collect all dependent items.
 */
typedef struct reference_t
{
  svn_fs_fs__id_part_t to;
  svn_fs_fs__id_part_t from;
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

  /* array of svn_fs_fs__p2l_entry_t *, all referring to change lists.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *changes;

  /* temp file receiving all change list items (referenced by CHANGES).
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_file_t *changes_file;

  /* array of svn_fs_fs__p2l_entry_t *, all referring to file properties.
   * Will be filled in phase 2 and be cleared after each revision range. */
  apr_array_header_t *file_props;

  /* temp file receiving all file prop items (referenced by FILE_PROPS).
   * Will be filled in phase 2 and be cleared after each revision range.*/
  apr_file_t *file_props_file;

  /* array of svn_fs_fs__p2l_entry_t *, all referring to directory properties.
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

  /* array of svn_fs_fs__p2l_entry_t*.  Will be filled in phase 2 and be
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

  /* ensure that all filesystem changes are written to disk. */
  svn_boolean_t flush_to_disk;
} pack_context_t;

/* Create and initialize a new pack context for packing shard SHARD_REV in
 * SHARD_DIR into PACK_FILE_DIR within filesystem FS.  Allocate it in POOL
 * and return the structure in *CONTEXT.
 *
 * Limit the number of items being copied per iteration to MAX_ITEMS.
 * Set FLUSH_TO_DISK, CANCEL_FUNC and CANCEL_BATON as well.
 */
static svn_error_t *
initialize_pack_context(pack_context_t *context,
                        svn_fs_t *fs,
                        const char *pack_file_dir,
                        const char *shard_dir,
                        svn_revnum_t shard_rev,
                        int max_items,
                        svn_boolean_t flush_to_disk,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  const char *temp_dir;
  int max_revs = MIN(ffd->max_files_per_dir, max_items);

  SVN_ERR_ASSERT(ffd->format >= SVN_FS_FS__MIN_LOG_ADDRESSING_FORMAT);
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

  /* the pool used for temp structures */
  context->info_pool = svn_pool_create(pool);
  context->paths = svn_prefix_tree__create(context->info_pool);

  context->flush_to_disk = flush_to_disk;

  /* Create the new directory and pack file. */
  context->shard_dir = shard_dir;
  context->pack_file_dir = pack_file_dir;
  context->pack_file_path
    = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);
  SVN_ERR(svn_io_file_open(&context->pack_file, context->pack_file_path,
                           APR_WRITE | APR_BUFFERED | APR_BINARY | APR_EXCL
                             | APR_CREATE, APR_OS_DEFAULT, pool));

  /* Proto index files */
  SVN_ERR(svn_fs_fs__l2p_proto_index_open(
             &context->proto_l2p_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_L2P_INDEX,
                             pool),
             pool));
  SVN_ERR(svn_fs_fs__p2l_proto_index_open(
             &context->proto_p2l_index,
             svn_dirent_join(pack_file_dir,
                             PATH_INDEX PATH_EXT_P2L_INDEX,
                             pool),
             pool));

  /* item buckets: one item info array and one temp file per bucket */
  context->changes = apr_array_make(pool, max_items,
                                    sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->changes_file, NULL, temp_dir,
                                   svn_io_file_del_on_close,
                                   context->info_pool, pool));
  context->file_props = apr_array_make(pool, max_items,
                                       sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->file_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, 
                                   context->info_pool, pool));
  context->dir_props = apr_array_make(pool, max_items,
                                      sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->dir_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, 
                                   context->info_pool, pool));

  /* noderev and representation item bucket */
  context->rev_offsets = apr_array_make(pool, max_revs, sizeof(int));
  context->path_order = apr_array_make(pool, max_items,
                                       sizeof(path_order_t *));
  context->references = apr_array_make(pool, max_items,
                                       sizeof(reference_t *));
  context->reps = apr_array_make(pool, max_items,
                                 sizeof(svn_fs_fs__p2l_entry_t *));
  SVN_ERR(svn_io_open_unique_file3(&context->reps_file, NULL, temp_dir,
                                   svn_io_file_del_on_close, pool, pool));

  return SVN_NO_ERROR;
}

/* Clean up / free all revision range specific data and files in CONTEXT.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
reset_pack_context(pack_context_t *context,
                   apr_pool_t *pool)
{
  const char *temp_dir;

  apr_array_clear(context->changes);
  SVN_ERR(svn_io_file_close(context->changes_file, pool));
  apr_array_clear(context->file_props);
  SVN_ERR(svn_io_file_close(context->file_props_file, pool));
  apr_array_clear(context->dir_props);
  SVN_ERR(svn_io_file_close(context->dir_props_file, pool));

  apr_array_clear(context->rev_offsets);
  apr_array_clear(context->path_order);
  apr_array_clear(context->references);
  apr_array_clear(context->reps);
  SVN_ERR(svn_io_file_close(context->reps_file, pool));

  svn_pool_clear(context->info_pool);

  /* The new temporary files must live at least as long as any other info
   * object in CONTEXT. */
  SVN_ERR(svn_io_temp_dir(&temp_dir, pool));
  SVN_ERR(svn_io_open_unique_file3(&context->changes_file, NULL, temp_dir,
                                   svn_io_file_del_on_close,
                                   context->info_pool, pool));
  SVN_ERR(svn_io_open_unique_file3(&context->file_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close,
                                   context->info_pool, pool));
  SVN_ERR(svn_io_open_unique_file3(&context->dir_props_file, NULL, temp_dir,
                                   svn_io_file_del_on_close,
                                   context->info_pool, pool));
  SVN_ERR(svn_io_open_unique_file3(&context->reps_file, NULL, temp_dir,
                                   svn_io_file_del_on_close,
                                   context->info_pool, pool));
  context->paths = svn_prefix_tree__create(context->info_pool);

  return SVN_NO_ERROR;
}

/* Call this after the last revision range.  It will finalize all index files
 * for CONTEXT and close any open files.  Use POOL for temporary allocations.
 */
static svn_error_t *
close_pack_context(pack_context_t *context,
                   apr_pool_t *pool)
{
  const char *proto_l2p_index_path;
  const char *proto_p2l_index_path;

  /* need the file names for the actual index creation call further down */
  SVN_ERR(svn_io_file_name_get(&proto_l2p_index_path,
                               context->proto_l2p_index, pool));
  SVN_ERR(svn_io_file_name_get(&proto_p2l_index_path,
                               context->proto_p2l_index, pool));

  /* finalize proto index files */
  SVN_ERR(svn_io_file_close(context->proto_l2p_index, pool));
  SVN_ERR(svn_io_file_close(context->proto_p2l_index, pool));

  /* Append the actual index data to the pack file. */
  SVN_ERR(svn_fs_fs__add_index_data(context->fs, context->pack_file,
                                    proto_l2p_index_path,
                                    proto_p2l_index_path,
                                    context->shard_rev,
                                    pool));

  /* remove proto index files */
  SVN_ERR(svn_io_remove_file2(proto_l2p_index_path, FALSE, pool));
  SVN_ERR(svn_io_remove_file2(proto_p2l_index_path, FALSE, pool));

  /* Ensure that packed file is written to disk.*/
  if (context->flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(context->pack_file, pool));
  SVN_ERR(svn_io_file_close(context->pack_file, pool));

  return SVN_NO_ERROR;
}

/* Efficiently copy SIZE bytes from SOURCE to DEST.  Invoke the CANCEL_FUNC
 * from CONTEXT at regular intervals.  Use POOL for allocations.
 */
static svn_error_t *
copy_file_data(pack_context_t *context,
               apr_file_t *dest,
               apr_file_t *source,
               apr_off_t size,
               apr_pool_t *pool)
{
  /* most non-representation items will be small.  Minimize the buffer
   * and infrastructure overhead in that case. */
  enum { STACK_BUFFER_SIZE = 1024 };

  if (size < STACK_BUFFER_SIZE)
    {
      /* copy small data using a fixed-size buffer on stack */
      char buffer[STACK_BUFFER_SIZE];
      SVN_ERR(svn_io_file_read_full2(source, buffer, (apr_size_t)size,
                                     NULL, NULL, pool));
      SVN_ERR(svn_io_file_write_full(dest, buffer, (apr_size_t)size,
                                     NULL, pool));
    }
  else
    {
      /* use streaming copies for larger data blocks.  That may require
       * the allocation of larger buffers and we should make sure that
       * this extra memory is released asap. */
      fs_fs_data_t *ffd = context->fs->fsap_data;
      apr_pool_t *copypool = svn_pool_create(pool);
      char *buffer = apr_palloc(copypool, ffd->block_size);

      while (size)
        {
          apr_size_t to_copy = (apr_size_t)(MIN(size, ffd->block_size));
          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));

          SVN_ERR(svn_io_file_read_full2(source, buffer, to_copy,
                                         NULL, NULL, pool));
          SVN_ERR(svn_io_file_write_full(dest, buffer, to_copy,
                                         NULL, pool));

          size -= to_copy;
        }

      svn_pool_destroy(copypool);
    }

  return SVN_NO_ERROR;
}

/* Writes SIZE bytes, all 0, to DEST.  Uses POOL for allocations.
 */
static svn_error_t *
write_null_bytes(apr_file_t *dest,
                 apr_off_t size,
                 apr_pool_t *pool)
{
  /* Have a collection of high-quality, easy to access NUL bytes handy. */
  enum { BUFFER_SIZE = 1024 };
  static const char buffer[BUFFER_SIZE] = { 0 };

  /* copy SIZE of them into the file's buffer */
  while (size)
    {
      apr_size_t to_write = MIN(size, BUFFER_SIZE);
      SVN_ERR(svn_io_file_write_full(dest, buffer, to_write, NULL, pool));
      size -= to_write;
    }

  return SVN_NO_ERROR;
}

/* Copy the "simple" item (changed paths list or property representation)
 * from the current position in REV_FILE to TEMP_FILE using CONTEXT.  Add
 * a copy of ENTRY to ENTRIES but with an updated offset value that points
 * to the copy destination in TEMP_FILE.  Use POOL for allocations.
 */
static svn_error_t *
copy_item_to_temp(pack_context_t *context,
                  apr_array_header_t *entries,
                  apr_file_t *temp_file,
                  apr_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  svn_fs_fs__p2l_entry_t *new_entry
    = apr_pmemdup(context->info_pool, entry, sizeof(*entry));

  SVN_ERR(svn_io_file_get_offset(&new_entry->offset, temp_file, pool));
  APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *) = new_entry;

  SVN_ERR(copy_file_data(context, temp_file, rev_file, entry->size, pool));

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

/* Write INFO to the correct position in CONTEXT->REP_INFOS.  The latter
 * may need auto-expanding.  Overwriting an array element is not allowed.
 */
static void
add_item_rep_mapping(pack_context_t *context,
                     svn_fs_fs__p2l_entry_t *entry)
{
  int idx;

  /* index of INFO */
  idx = get_item_array_index(context,
                             entry->item.revision,
                             entry->item.number);

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
static svn_fs_fs__p2l_entry_t *
get_item(pack_context_t *context,
         const svn_fs_fs__id_part_t *id,
         svn_boolean_t reset)
{
  svn_fs_fs__p2l_entry_t *result = NULL;
  if (id->number && id->revision >= context->start_rev)
    {
      int idx = get_item_array_index(context, id->revision, id->number);
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
 * our placement algorithm to CONTEXT.  Use POOL for temporary allocations.
 */
static svn_error_t *
copy_rep_to_temp(pack_context_t *context,
                 apr_file_t *rev_file,
                 svn_fs_fs__p2l_entry_t *entry,
                 apr_pool_t *pool)
{
  svn_fs_fs__rep_header_t *rep_header;
  svn_stream_t *stream;
  apr_off_t source_offset = entry->offset;

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  entry = apr_pmemdup(context->info_pool, entry, sizeof(*entry));
  SVN_ERR(svn_io_file_get_offset(&entry->offset, context->reps_file, pool));
  add_item_rep_mapping(context, entry);

  /* read & parse the representation header */
  stream = svn_stream_from_aprfile2(rev_file, TRUE, pool);
  SVN_ERR(svn_fs_fs__read_rep_header(&rep_header, stream, pool, pool));
  SVN_ERR(svn_stream_close(stream));

  /* if the representation is a delta against some other rep, link the two */
  if (   rep_header->type == svn_fs_fs__rep_delta
      && rep_header->base_revision >= context->start_rev)
    {
      reference_t *reference = apr_pcalloc(context->info_pool,
                                           sizeof(*reference));
      reference->from = entry->item;
      reference->to.revision = rep_header->base_revision;
      reference->to.number = rep_header->base_item_index;
      APR_ARRAY_PUSH(context->references, reference_t *) = reference;
    }

  /* copy the whole rep (including header!) to our temp file */
  SVN_ERR(svn_io_file_seek(rev_file, APR_SET, &source_offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file, entry->size,
                         pool));

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
compare_dir_entries_format7(const svn_sort__item_t *a,
                            const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  return strcmp(lhs->name, rhs->name);
}

/* Directories entries sorted by revision (decreasing - to max cache hits)
 * and offset (increasing - to max benefit from APR file buffering).
 */
static int
compare_dir_entries_format6(const svn_sort__item_t *a,
                            const svn_sort__item_t *b)
{
  const svn_fs_dirent_t *lhs = (const svn_fs_dirent_t *) a->value;
  const svn_fs_dirent_t *rhs = (const svn_fs_dirent_t *) b->value;

  const svn_fs_fs__id_part_t *lhs_rev_item
    = svn_fs_fs__id_rev_item(lhs->id);
  const svn_fs_fs__id_part_t *rhs_rev_item
    = svn_fs_fs__id_rev_item(rhs->id);

  /* decreasing ("reverse") order on revs */
  if (lhs_rev_item->revision != rhs_rev_item->revision)
    return lhs_rev_item->revision > rhs_rev_item->revision ? -1 : 1;

  /* increasing order on offsets */
  if (lhs_rev_item->number != rhs_rev_item->number)
    return lhs_rev_item->number > rhs_rev_item->number ? 1 : -1;

  return 0;
}

apr_array_header_t *
svn_fs_fs__order_dir_entries(svn_fs_t *fs,
                             apr_hash_t *directory,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  apr_array_header_t *ordered
    = svn_sort__hash(directory,
                     svn_fs_fs__use_log_addressing(fs)
                       ? compare_dir_entries_format7
                       : compare_dir_entries_format6,
                     scratch_pool);

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
 * our placement algorithm to CONTEXT.  Use POOL for temporary allocations.
 */
static svn_error_t *
copy_node_to_temp(pack_context_t *context,
                  svn_fs_fs__revision_file_t *rev_file,
                  svn_fs_fs__p2l_entry_t *entry,
                  apr_pool_t *pool)
{
  path_order_t *path_order = apr_pcalloc(context->info_pool,
                                         sizeof(*path_order));
  node_revision_t *noderev;
  const char *sort_path;
  apr_off_t source_offset = entry->offset;

  /* read & parse noderev */
  SVN_ERR(svn_fs_fs__read_noderev(&noderev, rev_file->stream, pool, pool));

  /* create a copy of ENTRY, make it point to the copy destination and
   * store it in CONTEXT */
  entry = apr_pmemdup(context->info_pool, entry, sizeof(*entry));
  SVN_ERR(svn_io_file_get_offset(&entry->offset, context->reps_file,
                                 pool));
  add_item_rep_mapping(context, entry);

  /* copy the noderev to our temp file */
  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &source_offset, pool));
  SVN_ERR(copy_file_data(context, context->reps_file, rev_file->file,
                         entry->size, pool));

  /* if the node has a data representation, make that the node's "base".
   * This will (often) cause the noderev to be placed right in front of
   * its data representation. */

  if (noderev->data_rep && noderev->data_rep->revision >= context->start_rev)
    {
      path_order->rep_id.revision = noderev->data_rep->revision;
      path_order->rep_id.number = noderev->data_rep->item_index;
      path_order->expanded_size = noderev->data_rep->expanded_size;
    }

  /* Sort path is the key used for ordering noderevs and associated reps.
   * It will not be stored in the final pack file. */
  sort_path = tweak_path_for_ordering(noderev->created_path, pool);
  path_order->path = svn_prefix_string__create(context->paths, sort_path);
  path_order->node_id = *svn_fs_fs__id_node_id(noderev->id);
  path_order->revision = svn_fs_fs__id_rev(noderev->id);
  path_order->predecessor_count = noderev->predecessor_count;
  path_order->noderev_id = *svn_fs_fs__id_rev_item(noderev->id);
  APR_ARRAY_PUSH(context->path_order, path_order_t *) = path_order;

  return SVN_NO_ERROR;
}

/* implements compare_fn_t.  Bring all directories in front of the files
   and sort descendingly by PATH, NODE_ID and REVISION.
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
  diff = svn_fs_fs__id_part_compare(&rhs->node_id, &lhs->node_id);
  if (diff)
    return diff;

  /* reverse order on revision (i.e. latest first) */
  if (lhs->revision != rhs->revision)
    return lhs->revision < rhs->revision ? 1 : -1;

  return 0;
}

/* implements compare_fn_t.  Sort ascendingly by FROM, TO.
 */
static int
compare_references(const reference_t * const * lhs_p,
                   const reference_t * const * rhs_p)
{
  const reference_t * lhs = *lhs_p;
  const reference_t * rhs = *rhs_p;

  int diff = svn_fs_fs__id_part_compare(&lhs->from, &rhs->from);
  return diff ? diff : svn_fs_fs__id_part_compare(&lhs->to, &rhs->to);
}

/* implements compare_fn_t.  Assume ascending order by FROM.
 */
static int
compare_ref_to_item(const reference_t * const * lhs_p,
                    const svn_fs_fs__id_part_t * rhs_p)
{
  return svn_fs_fs__id_part_compare(&(*lhs_p)->from, rhs_p);
}

/* Look for the least significant bit set in VALUE and return the smallest
 * number with the same property, i.e. the largest power of 2 that is a
 * factor in VALUE.  Edge case: roundness(0) := 0 . */
static int
roundness(int value)
{
  return value - (value & (value - 1));
}

/* For all paths in first COUNT entries in PATH_ORDER, mark their latest
 * node as "HEAD".  PATH_ORDER must be ordered by path, revision.
 */
static void
classify_nodes(path_order_t **path_order,
               int count)
{
  const svn_prefix_string__t *path;
  int i;

  /* The logic below would fail for empty ranges. */
  if (count == 0)
    return;

  /* All entries are sorted by path, followed by revision.
   * So, the first index is also HEAD for the first path.
   */
  path = path_order[0]->path;
  path_order[0]->is_head = TRUE;

  /* Since the sorting implicitly groups all entries by path and then sorts
   * by descending revision within the group, whenever we encounter a new
   * path, the first entry is "HEAD" for that path.
   */
  for (i = 1; i < count; ++i)
    {
      /* New path? */
      if (svn_prefix_string__compare(path, path_order[i]->path))
        {
          path = path_order[i]->path;
          path_order[i]->is_head = TRUE;
        }
    }
}

/* Order a range of data collected in CONTEXT such that we can place them
 * in the desired order.  The input is taken from *PATH_ORDER, offsets FIRST
 * to LAST and then written in the final order to the same range in *TEMP.
 */
static void
sort_reps_range(pack_context_t *context,
                path_order_t **path_order,
                path_order_t **temp,
                int first,
                int last)
{
  const svn_prefix_string__t *path;
  int i, dest;
  svn_fs_fs__id_part_t rep_id;
  fs_fs_data_t *ffd = context->fs->fsap_data;

  /* The logic below would fail for empty ranges. */
  if (first == last)
    return;

  /* Re-order noderevs like this:
   *
   * (1) Most likely to be referenced by future pack files, in path order.
   * (2) highest revision rep per path + dependency chain
   * (3) Remaining reps in path, rev order
   *
   * We simply pick & chose from the existing path, rev order.
   */
  dest = first;

  /* (1) There are two classes of representations that are likely to be
   * referenced from future shards.  These form a "hot zone" of mostly
   * relevant data, i.e. we try to include as many reps as possible that
   * are needed for future checkouts while trying to exclude as many as
   * possible that are likely not needed in future checkouts.
   *
   * First, "very round" representations from frequently changing nodes.
   * That excludes many in-between representations not accessed from HEAD.
   *
   * The second class are infrequently changing nodes.  Because they are
   * unlikely to change often in the future, they will remain relevant for
   * HEAD even over long spans of revisions.  They are most likely the only
   * thing we need from very old pack files.
   */
  for (i = first; i < last; ++i)
    {
      int round = roundness(path_order[i]->predecessor_count);

      /* Class 1:
       * Pretty round _and_ a significant stop in the node's delta chain.
       * This may pick up more than one representation from the same chain
       * but that's rare and not a problem.  Prefer simple checks here.
       *
       * The divider of 4 is arbitrary but seems to work well in practice.
       * Larger values increase the number of items in the "hot zone".
       * Smaller values make delta chains at HEAD more likely to contain
       * "cold zone" representations. */
      svn_boolean_t likely_target
        =    (round >= ffd->max_linear_deltification)
          && (round >= path_order[i]->predecessor_count / 4);

      /* Class 2:
       * Anything from short node chains.  The default of 16 is generous
       * but we'd rather include too many than too few nodes here to keep
       * seeks between different regions of this pack file at a minimum. */
      svn_boolean_t likely_head
        =   path_order[i]->predecessor_count
          < ffd->max_linear_deltification;

      /* Pick any node that from either class. */
      if (likely_target || likely_head)
        {
          temp[dest++] = path_order[i];
          path_order[i] = NULL;
        }
    }

  /* (2) For each (remaining) path, pick the nodes along the delta chain
   * for the highest revision.  Due to our ordering, this is the first
   * node we encounter for any path.
   *
   * Most references that don't hit a delta base picked in (1), will
   * access HEAD of the respective path.  Keeping all its dependency chain
   * in one place turns reconstruction into a linear scan of minimal length.
   */
  for (i = first; i < last; ++i)
    if (path_order[i])
      {
        /* This is the first path we still have to handle. */
        path = path_order[i]->path;
        rep_id = path_order[i]->rep_id;
        break;
      }

  for (i = first; i < last; ++i)
    if (path_order[i])
      {
        /* New path? */
        if (svn_prefix_string__compare(path, path_order[i]->path))
          {
            path = path_order[i]->path;
            rep_id = path_order[i]->rep_id;
          }

        /* Pick nodes along the deltification chain.  Skip side-branches. */
        if (svn_fs_fs__id_part_eq(&path_order[i]->rep_id, &rep_id))
          {
            reference_t **reference;

            temp[dest++] = path_order[i];
            path_order[i] = NULL;

            reference = svn_sort__array_lookup(context->references,
                                               &rep_id, NULL,
              (int (*)(const void *, const void *))compare_ref_to_item);
            if (reference)
              rep_id = (*reference)->to;
          }
      }

  /* (3) All remaining nodes in path, rev order.  Linear deltification
   * makes HEAD delta chains from (2) cover all or most of their deltas
   * in a given pack file.  So, this is just a few remnants that we put
   * at the end of the pack file.
   */
  for (i = first; i < last; ++i)
    if (path_order[i])
      temp[dest++] = path_order[i];

  /* We now know the final ordering. */
  assert(dest == last);
}

/* Order the data collected in CONTEXT such that we can place them in the
 * desired order.
 */
static void
sort_reps(pack_context_t *context)
{
  apr_pool_t *temp_pool;
  path_order_t **temp, **path_order;
  int i, count;

  /* We will later assume that there is at least one node / path.
   */
  if (context->path_order->nelts == 0)
    {
      assert(context->references->nelts == 0);
      return;
    }

  /* Sort containers by path and IDs, respectively.
   */
  svn_sort__array(context->path_order,
                  (int (*)(const void *, const void *))compare_path_order);
  svn_sort__array(context->references,
                  (int (*)(const void *, const void *))compare_references);

  /* Directories are already in front; sort directories section and files
   * section separately but use the same heuristics (see sub-function).
   */
  temp_pool = svn_pool_create(context->info_pool);
  count = context->path_order->nelts;
  temp = apr_pcalloc(temp_pool, count * sizeof(*temp));
  path_order = (void *)context->path_order->elts;

  /* Mark nodes depending on what other nodes exist for the same path etc. */
  classify_nodes(path_order, count);

  /* Rearrange those sub-sections separately. */
  sort_reps_range(context, path_order, temp, 0, count);

  /* We now know the final ordering. */
  for (i = 0; i < count; ++i)
    path_order[i] = temp[i];

  svn_pool_destroy(temp_pool);
}

/* implements compare_fn_t. Place LHS before RHS, if the latter is older.
 */
static int
compare_p2l_info(const svn_fs_fs__p2l_entry_t * const * lhs,
                 const svn_fs_fs__p2l_entry_t * const * rhs)
{
  assert(*lhs != *rhs);

  if ((*lhs)->item.revision == (*rhs)->item.revision)
    return (*lhs)->item.number > (*rhs)->item.number ? -1 : 1;

  return (*lhs)->item.revision > (*rhs)->item.revision ? -1 : 1;
}

/* Sort svn_fs_fs__p2l_entry_t * array ENTRIES by age.  Place the latest
 * items first.
 */
static void
sort_items(apr_array_header_t *entries)
{
  svn_sort__array(entries,
                  (int (*)(const void *, const void *))compare_p2l_info);
}

/* Return the remaining unused bytes in the current block in CONTEXT's
 * pack file.
 */
static apr_off_t
get_block_left(pack_context_t *context)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  return ffd->block_size - (context->pack_offset % ffd->block_size);
}

/* To prevent items from overlapping a block boundary, we will usually
 * put them into the next block and top up the old one with NUL bytes.
 * Pad CONTEXT's pack file to the end of the current block, if TO_ADD does
 * not fit into the current block and the padding is short enough.
 * Use POOL for allocations.
 */
static svn_error_t *
auto_pad_block(pack_context_t *context,
               apr_off_t to_add,
               apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;

  /* This is the maximum number of bytes "wasted" that way per block.
   * Larger items will cross the block boundaries. */
  const apr_off_t max_padding = MAX(ffd->block_size / 50, 512);

  /* Is wasted space small enough to align the current item to the next
   * block? */
  apr_off_t padding = get_block_left(context);

  if (padding < to_add && padding < max_padding)
    {
      /* Yes. To up with NUL bytes and don't forget to create
       * an P2L index entry marking this section as unused. */
      svn_fs_fs__p2l_entry_t null_entry;

      null_entry.offset = context->pack_offset;
      null_entry.size = padding;
      null_entry.type = SVN_FS_FS__ITEM_TYPE_UNUSED;
      null_entry.item.revision = SVN_INVALID_REVNUM;
      null_entry.item.number = SVN_FS_FS__ITEM_INDEX_UNUSED;
      null_entry.fnv1_checksum = 0;

      SVN_ERR(write_null_bytes(context->pack_file, padding, pool));
      SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(
                   context->proto_p2l_index, &null_entry, pool));
      context->pack_offset += padding;
    }

  return SVN_NO_ERROR;
}

/* Read the contents of ITEM, if not empty, from TEMP_FILE and write it
 * to CONTEXT->PACK_FILE.  Use POOL for allocations.
 */
static svn_error_t *
store_item(pack_context_t *context,
           apr_file_t *temp_file,
           svn_fs_fs__p2l_entry_t *item,
           apr_pool_t *pool)
{
  apr_off_t safety_margin;

  /* skip empty entries */
  if (item->type == SVN_FS_FS__ITEM_TYPE_UNUSED)
    return SVN_NO_ERROR;

  /* If the next item does not fit into the current block, auto-pad it.
      Take special care of textual noderevs since their parsers may
      prefetch up to 80 bytes and we don't want them to cross block
      boundaries. */
  safety_margin = item->type == SVN_FS_FS__ITEM_TYPE_NODEREV
                ? SVN__LINE_CHUNK_SIZE
                : 0;
  SVN_ERR(auto_pad_block(context, item->size + safety_margin, pool));

  /* select the item in the source file and copy it into the target
    * pack file */
  SVN_ERR(svn_io_file_seek(temp_file, APR_SET, &item->offset, pool));
  SVN_ERR(copy_file_data(context, context->pack_file, temp_file,
                         item->size, pool));

  /* write index entry and update current position */
  item->offset = context->pack_offset;
  context->pack_offset += item->size;

  SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(context->proto_p2l_index,
                                               item, pool));

  APR_ARRAY_PUSH(context->reps, svn_fs_fs__p2l_entry_t *) = item;

  return SVN_NO_ERROR;
}

/* Read the contents of the non-empty items in ITEMS from TEMP_FILE and
 * write them to CONTEXT->PACK_FILE.  Use POOL for allocations.
 */
static svn_error_t *
store_items(pack_context_t *context,
            apr_file_t *temp_file,
            apr_array_header_t *items,
            apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* copy all items in strict order */
  for (i = 0; i < items->nelts; ++i)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(store_item(context, temp_file,
                         APR_ARRAY_IDX(items, i, svn_fs_fs__p2l_entry_t *),
                         iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Copy (append) the items identified by svn_fs_fs__p2l_entry_t * elements
 * in ENTRIES strictly in order from TEMP_FILE into CONTEXT->PACK_FILE.
 * Use POOL for temporary allocations.
 */
static svn_error_t *
copy_reps_from_temp(pack_context_t *context,
                    apr_file_t *temp_file,
                    apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_array_header_t *path_order = context->path_order;
  int i;

  /* copy items in path order.  Exclude the non-HEAD noderevs. */
  for (i = 0; i < path_order->nelts; ++i)
    {
      path_order_t *current_path;
      svn_fs_fs__p2l_entry_t *node_part;
      svn_fs_fs__p2l_entry_t *rep_part;

      svn_pool_clear(iterpool);

      current_path = APR_ARRAY_IDX(path_order, i, path_order_t *);
      if (current_path->is_head)
        {
          node_part = get_item(context, &current_path->noderev_id, TRUE);
          if (node_part)
            SVN_ERR(store_item(context, temp_file, node_part, iterpool));
        }

      rep_part = get_item(context, &current_path->rep_id, TRUE);
      if (rep_part)
        SVN_ERR(store_item(context, temp_file, rep_part, iterpool));
    }

  /* copy the remaining non-head noderevs. */
  for (i = 0; i < path_order->nelts; ++i)
    {
      path_order_t *current_path;
      svn_fs_fs__p2l_entry_t *node_part;

      svn_pool_clear(iterpool);

      current_path = APR_ARRAY_IDX(path_order, i, path_order_t *);
      node_part = get_item(context, &current_path->noderev_id, TRUE);
      if (node_part)
        SVN_ERR(store_item(context, temp_file, node_part, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* implements compare_fn_t. Place LHS before RHS, if the latter belongs to
 * a newer revision.
 */
static int
compare_p2l_info_rev(const svn_fs_fs__p2l_entry_t * const * lhs_p,
                     const svn_fs_fs__p2l_entry_t * const * rhs_p)
{
  const svn_fs_fs__p2l_entry_t * lhs = *lhs_p;
  const svn_fs_fs__p2l_entry_t * rhs = *rhs_p;

  if (lhs->item.revision == rhs->item.revision)
    return 0;

  return lhs->item.revision < rhs->item.revision ? -1 : 1;
}

/* Write the log-to-phys proto index file for CONTEXT and use POOL for
 * temporary allocations.  All items in all buckets must have been placed
 * by now.
 */
static svn_error_t *
write_l2p_index(pack_context_t *context,
                apr_pool_t *pool)
{
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_revnum_t prev_rev = SVN_INVALID_REVNUM;
  int i, dest;

  /* eliminate empty entries from CONTEXT->REPS */
  for (i = 0, dest = 0; i < context->reps->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *entry
        = APR_ARRAY_IDX(context->reps, i, svn_fs_fs__p2l_entry_t *);
      if (entry)
        APR_ARRAY_IDX(context->reps, dest++, svn_fs_fs__p2l_entry_t *)
          = entry;
    }
  context->reps->nelts = dest;

  /* we need to write the l2p index revision by revision */
  svn_sort__array(context->reps,
                  (int (*)(const void *, const void *))compare_p2l_info_rev);

  /* write index entries */
  for (i = 0; i < context->reps->nelts; ++i)
    {
      svn_fs_fs__p2l_entry_t *p2l_entry
        = APR_ARRAY_IDX(context->reps, i, svn_fs_fs__p2l_entry_t *);
      if (p2l_entry == NULL)
        continue;

      /* next revision? */
      if (prev_rev != p2l_entry->item.revision)
        {
          prev_rev = p2l_entry->item.revision;
          SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision(
                       context->proto_l2p_index, iterpool));
        }

      /* add entry */
      SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(context->proto_l2p_index,
                                                   p2l_entry->offset,
                                                   p2l_entry->item.number,
                                                   iterpool));

      /* keep memory usage in check */
      if (i % 256 == 0)
        svn_pool_clear(iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Pack the current revision range of CONTEXT, i.e. this covers phases 2
 * to 4.  Use POOL for allocations.
 */
static svn_error_t *
pack_range(pack_context_t *context,
           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  apr_pool_t *revpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(pool);
  apr_pool_t *iterpool2 = svn_pool_create(pool);

  /* Phase 2: Copy items into various buckets and build tracking info */
  svn_revnum_t revision;
  for (revision = context->start_rev; revision < context->end_rev; ++revision)
    {
      apr_off_t offset = 0;
      svn_fs_fs__revision_file_t *rev_file;

      svn_pool_clear(revpool);

      /* Get the rev file dimensions (mainly index locations). */
      SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, context->fs,
                                               revision, revpool, iterpool));
      SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));

      /* store the indirect array index */
      APR_ARRAY_PUSH(context->rev_offsets, int) = context->reps->nelts;

      /* read the phys-to-log index file until we covered the whole rev file.
       * That index contains enough info to build both target indexes from it. */
      while (offset < rev_file->l2p_offset)
        {
          /* read one cluster */
          int i;
          apr_array_header_t *entries;

          svn_pool_clear(iterpool);

          SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, context->fs,
                                              rev_file, revision, offset,
                                              ffd->p2l_page_size, iterpool,
                                              iterpool));

          for (i = 0; i < entries->nelts; ++i)
            {
              svn_fs_fs__p2l_entry_t *entry
                = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

              /* skip first entry if that was duplicated due crossing a
                 cluster boundary */
              if (offset > entry->offset)
                continue;

              svn_pool_clear(iterpool2);

              /* process entry while inside the rev file */
              offset = entry->offset;
              if (offset < rev_file->l2p_offset)
                {
                  SVN_ERR(svn_io_file_seek(rev_file->file, APR_SET, &offset,
                                           iterpool2));

                  if (entry->type == SVN_FS_FS__ITEM_TYPE_CHANGES)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->changes,
                                              context->changes_file,
                                              rev_file->file, entry,
                                              iterpool2));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_FILE_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->file_props,
                                              context->file_props_file,
                                              rev_file->file, entry,
                                              iterpool2));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_DIR_PROPS)
                    SVN_ERR(copy_item_to_temp(context,
                                              context->dir_props,
                                              context->dir_props_file,
                                              rev_file->file, entry,
                                              iterpool2));
                  else if (   entry->type == SVN_FS_FS__ITEM_TYPE_FILE_REP
                           || entry->type == SVN_FS_FS__ITEM_TYPE_DIR_REP)
                    SVN_ERR(copy_rep_to_temp(context, rev_file->file, entry,
                                             iterpool2));
                  else if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV)
                    SVN_ERR(copy_node_to_temp(context, rev_file, entry,
                                              iterpool2));
                  else
                    SVN_ERR_ASSERT(entry->type == SVN_FS_FS__ITEM_TYPE_UNUSED);

                  offset += entry->size;
                }
            }

          if (context->cancel_func)
            SVN_ERR(context->cancel_func(context->cancel_baton));
        }
    }

  svn_pool_destroy(iterpool2);
  svn_pool_destroy(iterpool);

  /* phase 3: placement.
   * Use "newest first" placement for simple items. */
  sort_items(context->changes);
  sort_items(context->file_props);
  sort_items(context->dir_props);

  /* follow dependencies recursively for noderevs and data representations */
  sort_reps(context);

  /* phase 4: copy bucket data to pack file.  Write P2L index. */
  SVN_ERR(store_items(context, context->changes_file, context->changes,
                      revpool));
  svn_pool_clear(revpool);
  SVN_ERR(store_items(context, context->file_props_file, context->file_props,
                      revpool));
  svn_pool_clear(revpool);
  SVN_ERR(store_items(context, context->dir_props_file, context->dir_props,
                      revpool));
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
 * Use POOL for temporary allocations.
 */
static svn_error_t *
append_revision(pack_context_t *context,
                apr_pool_t *pool)
{
  fs_fs_data_t *ffd = context->fs->fsap_data;
  apr_off_t offset = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_fs_fs__revision_file_t *rev_file;
  svn_filesize_t revdata_size;

  /* Copy all non-index contents the rev file to the end of the pack file. */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, context->fs,
                                           context->start_rev, pool,
                                           iterpool));
  SVN_ERR(svn_fs_fs__auto_read_footer(rev_file));
  revdata_size = rev_file->l2p_offset;

  SVN_ERR(svn_io_file_aligned_seek(rev_file->file, ffd->block_size, NULL, 0,
                                   iterpool));
  SVN_ERR(copy_file_data(context, context->pack_file, rev_file->file,
                         revdata_size, iterpool));

  /* mark the start of a new revision */
  SVN_ERR(svn_fs_fs__l2p_proto_index_add_revision(context->proto_l2p_index,
                                                  pool));

  /* read the phys-to-log index file until we covered the whole rev file.
   * That index contains enough info to build both target indexes from it. */
  while (offset < revdata_size)
    {
      /* read one cluster */
      int i;
      apr_array_header_t *entries;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, context->fs, rev_file,
                                          context->start_rev, offset,
                                          ffd->p2l_page_size, iterpool,
                                          iterpool));

      for (i = 0; i < entries->nelts; ++i)
        {
          svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

          /* skip first entry if that was duplicated due crossing a
             cluster boundary */
          if (offset > entry->offset)
            continue;

          /* process entry while inside the rev file */
          offset = entry->offset;
          if (offset < revdata_size)
            {
              entry->offset += context->pack_offset;
              offset += entry->size;
              SVN_ERR(svn_fs_fs__l2p_proto_index_add_entry(
                         context->proto_l2p_index, entry->offset,
                         entry->item.number, iterpool));
              SVN_ERR(svn_fs_fs__p2l_proto_index_add_entry(
                         context->proto_p2l_index, entry, iterpool));
            }
        }
    }

  svn_pool_destroy(iterpool);
  context->pack_offset += revdata_size;

  SVN_ERR(svn_fs_fs__close_revision_file(rev_file));

  return SVN_NO_ERROR;
}

/* Logical addressing mode packing logic.
 *
 * Pack the revision shard starting at SHARD_REV in filesystem FS from
 * SHARD_DIR into the PACK_FILE_DIR, using POOL for allocations.  Limit
 * the extra memory consumption to MAX_MEM bytes.  If FLUSH_TO_DISK is
 * non-zero, do not return until the data has actually been written on
 * the disk.  CANCEL_FUNC and CANCEL_BATON are what you think they are.
 */
static svn_error_t *
pack_log_addressed(svn_fs_t *fs,
                   const char *pack_file_dir,
                   const char *shard_dir,
                   svn_revnum_t shard_rev,
                   apr_size_t max_mem,
                   svn_boolean_t flush_to_disk,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  enum
    {
      /* estimated amount of memory used to represent one item in memory
       * during rev file packing */
      PER_ITEM_MEM = APR_ALIGN_DEFAULT(sizeof(path_order_t))
                   + APR_ALIGN_DEFAULT(2 *sizeof(void*))
                   + APR_ALIGN_DEFAULT(sizeof(reference_t))
                   + APR_ALIGN_DEFAULT(sizeof(svn_fs_fs__p2l_entry_t))
                   + 6 * sizeof(void*)
    };

  int max_items;
  apr_array_header_t *max_ids;
  pack_context_t context = { 0 };
  int i;
  apr_size_t item_count = 0;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Prevent integer overflow.  We use apr arrays to process the items so
   * the maximum number of items is INT_MAX. */
    {
      apr_size_t temp = max_mem / PER_ITEM_MEM;
      SVN_ERR_ASSERT(temp <= INT_MAX);
      max_items = (int)temp;
    }

  /* set up a pack context */
  SVN_ERR(initialize_pack_context(&context, fs, pack_file_dir, shard_dir,
                                  shard_rev, max_items, flush_to_disk,
                                  cancel_func, cancel_baton, pool));

  /* phase 1: determine the size of the revisions to pack */
  SVN_ERR(svn_fs_fs__l2p_get_max_ids(&max_ids, fs, shard_rev,
                                     context.shard_end_rev - shard_rev,
                                     pool, pool));

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
        svn_pool_clear(iterpool);

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

/* Given REV in FS, set *REV_OFFSET to REV's offset in the packed file.
   Use POOL for temporary allocations. */
svn_error_t *
svn_fs_fs__get_packed_offset(apr_off_t *rev_offset,
                             svn_fs_t *fs,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_stream_t *manifest_stream;
  svn_boolean_t is_cached;
  svn_revnum_t shard;
  apr_int64_t shard_pos;
  apr_array_header_t *manifest;
  apr_pool_t *iterpool;

  shard = rev / ffd->max_files_per_dir;

  /* position of the shard within the manifest */
  shard_pos = rev % ffd->max_files_per_dir;

  /* fetch exactly that element into *rev_offset, if the manifest is found
     in the cache */
  SVN_ERR(svn_cache__get_partial((void **) rev_offset, &is_cached,
                                 ffd->packed_offset_cache, &shard,
                                 svn_fs_fs__get_sharded_offset, &shard_pos,
                                 pool));

  if (is_cached)
      return SVN_NO_ERROR;

  /* Open the manifest file. */
  SVN_ERR(svn_stream_open_readonly(&manifest_stream,
                                   svn_fs_fs__path_rev_packed(fs, rev,
                                                              PATH_MANIFEST,
                                                              pool),
                                   pool, pool));

  /* While we're here, let's just read the entire manifest file into an array,
     so we can cache the entire thing. */
  iterpool = svn_pool_create(pool);
  manifest = apr_array_make(pool, ffd->max_files_per_dir, sizeof(apr_off_t));
  while (1)
    {
      svn_boolean_t eof;
      apr_int64_t val;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__read_number_from_stream(&val, &eof, manifest_stream,
                                                 iterpool));
      if (eof)
        break;

      APR_ARRAY_PUSH(manifest, apr_off_t) = (apr_off_t)val;
    }
  svn_pool_destroy(iterpool);

  *rev_offset = APR_ARRAY_IDX(manifest, rev % ffd->max_files_per_dir,
                              apr_off_t);

  /* Close up shop and cache the array. */
  SVN_ERR(svn_stream_close(manifest_stream));
  return svn_cache__set(ffd->packed_offset_cache, &shard, manifest, pool);
}

/* Packing logic for physical addresssing mode:
 * Simply concatenate all revision contents.
 *
 * Pack the revision shard starting at SHARD_REV containing exactly
 * MAX_FILES_PER_DIR revisions from SHARD_PATH into the PACK_FILE_DIR,
 * using POOL for allocations.  If FLUSH_TO_DISK is non-zero, do not
 * return until the data has actually been written on the disk.
 * CANCEL_FUNC and CANCEL_BATON are what you think they are.
 */
static svn_error_t *
pack_phys_addressed(const char *pack_file_dir,
                    const char *shard_path,
                    svn_revnum_t start_rev,
                    int max_files_per_dir,
                    svn_boolean_t flush_to_disk,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *pool)
{
  const char *pack_file_path, *manifest_file_path;
  apr_file_t *pack_file;
  apr_file_t *manifest_file;
  svn_stream_t *manifest_stream;
  svn_revnum_t end_rev, rev;
  apr_pool_t *iterpool;

  /* Some useful paths. */
  pack_file_path = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);
  manifest_file_path = svn_dirent_join(pack_file_dir, PATH_MANIFEST, pool);

  /* Create the new directory and pack file.
   * Use unbuffered apr_file_t since we're going to write using 16kb
   * chunks. */
  SVN_ERR(svn_io_file_open(&pack_file, pack_file_path,
                           APR_WRITE | APR_CREATE | APR_EXCL,
                           APR_OS_DEFAULT, pool));

  /* Create the manifest file. */
  SVN_ERR(svn_io_file_open(&manifest_file, manifest_file_path,
                           APR_WRITE | APR_BUFFERED | APR_CREATE | APR_EXCL,
                           APR_OS_DEFAULT, pool));
  manifest_stream = svn_stream_from_aprfile2(manifest_file, TRUE, pool);

  end_rev = start_rev + max_files_per_dir - 1;
  iterpool = svn_pool_create(pool);

  /* Iterate over the revisions in this shard, squashing them together. */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      svn_stream_t *rev_stream;
      const char *path;
      apr_off_t offset;
      apr_file_t *rev_file;

      svn_pool_clear(iterpool);

      path = svn_dirent_join(shard_path, apr_psprintf(iterpool, "%ld", rev),
                             iterpool);

      /* Obtain current offset in pack file. */
      SVN_ERR(svn_io_file_get_offset(&offset, pack_file, iterpool));

      /* build manifest */
      SVN_ERR(svn_stream_printf(manifest_stream, iterpool,
                                "%" APR_OFF_T_FMT "\n", offset));

      /* Copy all the bits from the rev file to the end of the pack file.
       * Use unbuffered apr_file_t since we're going to write using 16kb
       * chunks. */
      SVN_ERR(svn_io_file_open(&rev_file, path, APR_READ, APR_OS_DEFAULT,
                               iterpool));
      rev_stream = svn_stream_from_aprfile2(rev_file, FALSE, iterpool);
      SVN_ERR(svn_stream_copy3(rev_stream,
                               svn_stream_from_aprfile2(pack_file, TRUE,
                                                        iterpool),
                               cancel_func, cancel_baton, iterpool));
    }

  /* Close stream over APR file. */
  SVN_ERR(svn_stream_close(manifest_stream));

  /* Ensure that pack file is written to disk. */
  if (flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(manifest_file, pool));
  SVN_ERR(svn_io_file_close(manifest_file, pool));

  /* disallow write access to the manifest file */
  SVN_ERR(svn_io_set_file_read_only(manifest_file_path, FALSE, iterpool));

  /* Ensure that pack file is written to disk. */
  if (flush_to_disk)
    SVN_ERR(svn_io_file_flush_to_disk(pack_file, pool));
  SVN_ERR(svn_io_file_close(pack_file, pool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* In filesystem FS, pack the revision SHARD containing exactly
 * MAX_FILES_PER_DIR revisions from SHARD_PATH into the PACK_FILE_DIR,
 * using POOL for allocations.  Try to limit the amount of temporary
 * memory needed to MAX_MEM bytes.  If FLUSH_TO_DISK is non-zero, do
 * not return until the data has actually been written on the disk.
 * CANCEL_FUNC and CANCEL_BATON are what you think they are.
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
               svn_boolean_t flush_to_disk,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  const char *pack_file_path;
  svn_revnum_t shard_rev = (svn_revnum_t) (shard * max_files_per_dir);

  /* Some useful paths. */
  pack_file_path = svn_dirent_join(pack_file_dir, PATH_PACKED, pool);

  /* Remove any existing pack file for this shard, since it is incomplete. */
  SVN_ERR(svn_io_remove_dir2(pack_file_dir, TRUE, cancel_func, cancel_baton,
                             pool));

  /* Create the new directory and pack file. */
  SVN_ERR(svn_io_dir_make(pack_file_dir, APR_OS_DEFAULT, pool));

  /* Index information files */
  if (svn_fs_fs__use_log_addressing(fs))
    SVN_ERR(pack_log_addressed(fs, pack_file_dir, shard_path,
                               shard_rev, max_mem, flush_to_disk,
                               cancel_func, cancel_baton, pool));
  else
    SVN_ERR(pack_phys_addressed(pack_file_dir, shard_path, shard_rev,
                                max_files_per_dir, flush_to_disk,
                                cancel_func, cancel_baton, pool));

  SVN_ERR(svn_io_copy_perms(shard_path, pack_file_dir, pool));
  SVN_ERR(svn_io_set_file_read_only(pack_file_path, FALSE, pool));

  return SVN_NO_ERROR;
}

/* Baton struct used by pack_body(), pack_shard() and synced_pack_shard().
   These calls are nested and for every level additional fields will be
   available. */
struct pack_baton
{
  /* Valid when entering pack_body(). */
  svn_fs_t *fs;
  svn_fs_pack_notify_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  size_t max_mem;

  /* Additional entries valid when entering pack_shard(). */
  const char *revs_dir;
  const char *revsprops_dir;
  apr_int64_t shard;

  /* Additional entries valid when entering synced_pack_shard(). */
  const char *rev_shard_path;
};


/* Part of the pack process that requires global (write) synchronization.
 * We pack the revision properties of the shard described by BATON and
 * In the file system at FS_PATH, pack the SHARD in REVS_DIR and replace
 * the non-packed revprop & rev shard folder(s) with the packed ones.
 * The packed rev folder has been created prior to calling this function.
 */
static svn_error_t *
synced_pack_shard(void *baton,
                  apr_pool_t *pool)
{
  struct pack_baton *pb = baton;
  fs_fs_data_t *ffd = pb->fs->fsap_data;
  const char *revprops_shard_path, *revprops_pack_file_dir;

  /* if enabled, pack the revprops in an equivalent way */
  if (pb->revsprops_dir)
    {
      apr_int64_t pack_size_limit = 0.9 * ffd->revprop_pack_size;

      revprops_pack_file_dir = svn_dirent_join(pb->revsprops_dir,
                   apr_psprintf(pool,
                                "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                                pb->shard),
                   pool);
      revprops_shard_path = svn_dirent_join(pb->revsprops_dir,
                    apr_psprintf(pool, "%" APR_INT64_T_FMT, pb->shard),
                    pool);

      SVN_ERR(svn_fs_fs__pack_revprops_shard(revprops_pack_file_dir,
                                             revprops_shard_path,
                                             pb->shard,
                                             ffd->max_files_per_dir,
                                             pack_size_limit,
                                             ffd->compress_packed_revprops
                                               ? SVN__COMPRESSION_ZLIB_DEFAULT
                                               : SVN__COMPRESSION_NONE,
                                             ffd->flush_to_disk,
                                             pb->cancel_func,
                                             pb->cancel_baton,
                                             pool));
    }

  /* Update the min-unpacked-rev file to reflect our newly packed shard. */
  SVN_ERR(svn_fs_fs__write_min_unpacked_rev(pb->fs,
                    (svn_revnum_t)((pb->shard + 1) * ffd->max_files_per_dir),
                    pool));
  ffd->min_unpacked_rev
    = (svn_revnum_t)((pb->shard + 1) * ffd->max_files_per_dir);

  /* Finally, remove the existing shard directories.
   * For revprops, clean up older obsolete shards as well as they might
   * have been left over from an interrupted FS upgrade. */
  SVN_ERR(svn_io_remove_dir2(pb->rev_shard_path, TRUE,
                             pb->cancel_func, pb->cancel_baton, pool));
  if (pb->revsprops_dir)
    {
      svn_node_kind_t kind = svn_node_dir;
      apr_int64_t to_cleanup = pb->shard;
      do
        {
          SVN_ERR(svn_fs_fs__delete_revprops_shard(revprops_shard_path,
                                                   to_cleanup,
                                                   ffd->max_files_per_dir,
                                                   pb->cancel_func,
                                                   pb->cancel_baton,
                                                   pool));

          /* If the previous shard exists, clean it up as well.
             Don't try to clean up shard 0 as it we can't tell quickly
             whether it actually needs cleaning up. */
          revprops_shard_path = svn_dirent_join(pb->revsprops_dir,
                      apr_psprintf(pool, "%" APR_INT64_T_FMT, --to_cleanup),
                      pool);
          SVN_ERR(svn_io_check_path(revprops_shard_path, &kind, pool));
        }
      while (kind == svn_node_dir && to_cleanup > 0);
    }

  return SVN_NO_ERROR;
}

/* Pack the shard described by BATON.
 *
 * If for some reason we detect a partial packing already performed,
 * we remove the pack file and start again.
 */
static svn_error_t *
pack_shard(struct pack_baton *baton,
           apr_pool_t *pool)
{
  fs_fs_data_t *ffd = baton->fs->fsap_data;
  const char *rev_pack_file_dir;

  /* Notify caller we're starting to pack this shard. */
  if (baton->notify_func)
    SVN_ERR(baton->notify_func(baton->notify_baton, baton->shard,
                               svn_fs_pack_notify_start, pool));

  /* Some useful paths. */
  rev_pack_file_dir = svn_dirent_join(baton->revs_dir,
                  apr_psprintf(pool,
                               "%" APR_INT64_T_FMT PATH_EXT_PACKED_SHARD,
                               baton->shard),
                  pool);
  baton->rev_shard_path = svn_dirent_join(baton->revs_dir,
                                          apr_psprintf(pool,
                                                       "%" APR_INT64_T_FMT,
                                                       baton->shard),
                                          pool);

  /* pack the revision content */
  SVN_ERR(pack_rev_shard(baton->fs, rev_pack_file_dir, baton->rev_shard_path,
                         baton->shard, ffd->max_files_per_dir,
                         baton->max_mem, ffd->flush_to_disk,
                         baton->cancel_func, baton->cancel_baton, pool));

  /* For newer repo formats, we only acquired the pack lock so far.
     Before modifying the repo state by switching over to the packed
     data, we need to acquire the global (write) lock. */
  if (ffd->format >= SVN_FS_FS__MIN_PACK_LOCK_FORMAT)
    SVN_ERR(svn_fs_fs__with_write_lock(baton->fs, synced_pack_shard, baton,
                                       pool));
  else
    SVN_ERR(synced_pack_shard(baton, pool));

  /* Notify caller we're starting to pack this shard. */
  if (baton->notify_func)
    SVN_ERR(baton->notify_func(baton->notify_baton, baton->shard,
                               svn_fs_pack_notify_end, pool));

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
  fs_fs_data_t *ffd = fs->fsap_data;
  apr_int64_t completed_shards;
  svn_revnum_t youngest;

  SVN_ERR(svn_fs_fs__read_min_unpacked_rev(&ffd->min_unpacked_rev, fs,
                                           scratch_pool));

  SVN_ERR(svn_fs_fs__youngest_rev(&youngest, fs, scratch_pool));
  completed_shards = (youngest + 1) / ffd->max_files_per_dir;

  /* See if we've already completed all possible shards thus far. */
  if (ffd->min_unpacked_rev == (completed_shards * ffd->max_files_per_dir))
    *fully_packed = TRUE;
  else
    *fully_packed = FALSE;

  return SVN_NO_ERROR;
}

/* The work-horse for svn_fs_fs__pack, called with the FS write lock.
   This implements the svn_fs_fs__with_write_lock() 'body' callback
   type.  BATON is a 'struct pack_baton *'.

   WARNING: if you add a call to this function, please note:
     The code currently assumes that any piece of code running with
     the write-lock set can rely on the ffd->min_unpacked_rev and
     ffd->min_unpacked_revprop caches to be up-to-date (and, by
     extension, on not having to use a retry when calling
     svn_fs_fs__path_rev_absolute() and friends).  If you add a call
     to this function, consider whether you have to call
     svn_fs_fs__update_min_unpacked_rev().
     See this thread: http://thread.gmane.org/1291206765.3782.3309.camel@edith
 */
static svn_error_t *
pack_body(void *baton,
          apr_pool_t *pool)
{
  struct pack_baton *pb = baton;
  fs_fs_data_t *ffd = pb->fs->fsap_data;
  apr_int64_t completed_shards;
  apr_pool_t *iterpool;
  svn_boolean_t fully_packed;

  /* Since another process might have already packed the repo,
     we need to re-read the pack status. */
  SVN_ERR(get_pack_status(&fully_packed, pb->fs, pool));
  if (fully_packed)
    {
      if (pb->notify_func)
        (*pb->notify_func)(pb->notify_baton,
                           ffd->min_unpacked_rev / ffd->max_files_per_dir,
                           svn_fs_pack_notify_noop, pool);

      return SVN_NO_ERROR;
    }

  completed_shards = (ffd->youngest_rev_cache + 1) / ffd->max_files_per_dir;
  pb->revs_dir = svn_dirent_join(pb->fs->path, PATH_REVS_DIR, pool);
  if (ffd->format >= SVN_FS_FS__MIN_PACKED_REVPROP_FORMAT)
    pb->revsprops_dir = svn_dirent_join(pb->fs->path, PATH_REVPROPS_DIR,
                                        pool);

  iterpool = svn_pool_create(pool);
  for (pb->shard = ffd->min_unpacked_rev / ffd->max_files_per_dir;
       pb->shard < completed_shards;
       pb->shard++)
    {
      svn_pool_clear(iterpool);

      if (pb->cancel_func)
        SVN_ERR(pb->cancel_func(pb->cancel_baton));

      SVN_ERR(pack_shard(pb, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__pack(svn_fs_t *fs,
                apr_size_t max_mem,
                svn_fs_pack_notify_t notify_func,
                void *notify_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  struct pack_baton pb = { 0 };
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_error_t *err;
  svn_boolean_t fully_packed;

  /* If the repository isn't a new enough format, we don't support packing.
     Return a friendly error to that effect. */
  if (ffd->format < SVN_FS_FS__MIN_PACKED_FORMAT)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
      _("FSFS format (%d) too old to pack; please upgrade the filesystem."),
      ffd->format);

  /* If we aren't using sharding, we can't do any packing, so quit. */
  if (!ffd->max_files_per_dir)
    {
      if (notify_func)
        (*notify_func)(notify_baton, -1, svn_fs_pack_notify_noop, pool);

      return SVN_NO_ERROR;
    }

  /* Is there we even anything to do?. */
  SVN_ERR(get_pack_status(&fully_packed, fs, pool));
  if (fully_packed)
    {
      if (notify_func)
        (*notify_func)(notify_baton,
                       ffd->min_unpacked_rev / ffd->max_files_per_dir,
                       svn_fs_pack_notify_noop, pool);

      return SVN_NO_ERROR;
    }

  /* Lock the repo and start the pack process. */
  pb.fs = fs;
  pb.notify_func = notify_func;
  pb.notify_baton = notify_baton;
  pb.cancel_func = cancel_func;
  pb.cancel_baton = cancel_baton;
  pb.max_mem = max_mem ? max_mem : DEFAULT_MAX_MEM;

  if (ffd->format >= SVN_FS_FS__MIN_PACK_LOCK_FORMAT)
    {
      /* Newer repositories provide a pack operation specific lock.
         Acquire it to prevent concurrent packs.

         Since the file lock's lifetime is bound to a pool, we create a
         separate subpool here to release the lock immediately after the
         operation finished.
       */
      err = svn_fs_fs__with_pack_lock(fs, pack_body, &pb, pool);
    }
  else
    {
      /* Use the global write lock for older repos. */
      err = svn_fs_fs__with_write_lock(fs, pack_body, &pb, pool);
    }

  return svn_error_trace(err);
}
