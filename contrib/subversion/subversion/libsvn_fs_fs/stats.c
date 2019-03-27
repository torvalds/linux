/* stats.c -- implements the svn_fs_fs__get_stats private API.
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

#include "svn_dirent_uri.h"
#include "svn_fs.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "private/svn_cache.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_fs_fs_private.h"

#include "index.h"
#include "pack.h"
#include "rev_file.h"
#include "util.h"
#include "fs_fs.h"
#include "cached_data.h"
#include "low_level.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"

/* We group representations into 2x2 different kinds plus one default:
 * [dir / file] x [text / prop]. The assignment is done by the first node
 * that references the respective representation.
 */
typedef enum rep_kind_t
{
  /* The representation is not used _directly_, i.e. not referenced by any
   * noderev. However, some other representation may use it as delta base.
   * Null value. Should not occur in real-word repositories. */
  unused_rep,

  /* a properties on directory rep  */
  dir_property_rep,

  /* a properties on file rep  */
  file_property_rep,

  /* a directory rep  */
  dir_rep,

  /* a file rep  */
  file_rep
} rep_kind_t;

/* A representation fragment.
 */
typedef struct rep_stats_t
{
  /* offset in the revision file (phys. addressing) /
   * item index within REVISION (log. addressing) */
  apr_uint64_t item_index;

  /* item length in bytes */
  apr_uint64_t size;

  /* item length after de-deltification */
  apr_uint64_t expanded_size;

  /* revision that contains this representation
   * (may be referenced by other revisions, though) */
  svn_revnum_t revision;

  /* number of nodes that reference this representation */
  apr_uint32_t ref_count;

  /* length of the PLAIN / DELTA line in the source file in bytes */
  apr_uint16_t header_size;

  /* classification of the representation. values of rep_kind_t */
  char kind;

  /* length of the delta chain, including this representation,
   * saturated to 255 - if need be */
  apr_byte_t chain_length;
} rep_stats_t;

/* Represents a link in the rep delta chain.  REVISION + ITEM_INDEX points
 * to BASE_REVISION + BASE_ITEM_INDEX.  We collect this info while scanning
 * a f7 repo in a single pass and resolve it afterwards. */
typedef struct rep_ref_t
{
  /* Revision that contains this representation. */
  svn_revnum_t revision;

  /* Item index of this rep within REVISION. */
  apr_uint64_t item_index;

  /* Revision of the representation we deltified against.
   * -1 if this representation is either PLAIN or a self-delta. */
  svn_revnum_t base_revision;

  /* Item index of that rep within BASE_REVISION. */
  apr_uint64_t base_item_index;

  /* Length of the PLAIN / DELTA line in the source file in bytes.
   * We use this to update the info in the rep stats after scanning the
   * whole file. */
  apr_uint16_t header_size;

} rep_ref_t;

/* Represents a single revision.
 * There will be only one instance per revision. */
typedef struct revision_info_t
{
  /* number of this revision */
  svn_revnum_t revision;

  /* pack file offset (manifest value), 0 for non-packed files */
  apr_off_t offset;

  /* length of the changes list on bytes */
  apr_uint64_t changes_len;

  /* offset of the changes list relative to OFFSET */
  apr_uint64_t change_count;

  /* first offset behind the revision data in the pack file (file length
   * for non-packed revs) */
  apr_off_t end;

  /* number of directory noderevs in this revision */
  apr_uint64_t dir_noderev_count;

  /* number of file noderevs in this revision */
  apr_uint64_t file_noderev_count;

  /* total size of directory noderevs (i.e. the structs - not the rep) */
  apr_uint64_t dir_noderev_size;

  /* total size of file noderevs (i.e. the structs - not the rep) */
  apr_uint64_t file_noderev_size;

  /* all rep_stats_t of this revision (in no particular order),
   * i.e. those that point back to this struct */
  apr_array_header_t *representations;

  /* Temporary rev / pack file access object, used in phys. addressing
   * mode only.  NULL when done reading this revision. */
  svn_fs_fs__revision_file_t *rev_file;
} revision_info_t;

/* Root data structure containing all information about a given repository.
 * We use it as a wrapper around svn_fs_t and pass it around where we would
 * otherwise just use a svn_fs_t.
 */
typedef struct query_t
{
  /* FS API object*/
  svn_fs_t *fs;

  /* The HEAD revision. */
  svn_revnum_t head;

  /* Number of revs per shard; 0 for non-sharded repos. */
  int shard_size;

  /* First non-packed revision. */
  svn_revnum_t min_unpacked_rev;

  /* all revisions */
  apr_array_header_t *revisions;

  /* empty representation.
   * Used as a dummy base for DELTA reps without base. */
  rep_stats_t *null_base;

  /* collected statistics */
  svn_fs_fs__stats_t *stats;

  /* Progress notification callback to call after each shard.  May be NULL. */
  svn_fs_progress_notify_func_t progress_func;

  /* Baton for PROGRESS_FUNC. */
  void *progress_baton;

  /* Cancellation support callback to call once in a while.  May be NULL. */
  svn_cancel_func_t cancel_func;

  /* Baton for CANCEL_FUNC. */
  void *cancel_baton;
} query_t;

/* Initialize the LARGEST_CHANGES member in STATS with a capacity of COUNT
 * entries.  Allocate the result in RESULT_POOL.
 */
static void
initialize_largest_changes(svn_fs_fs__stats_t *stats,
                           apr_size_t count,
                           apr_pool_t *result_pool)
{
  apr_size_t i;

  stats->largest_changes = apr_pcalloc(result_pool,
                                       sizeof(*stats->largest_changes));
  stats->largest_changes->count = count;
  stats->largest_changes->min_size = 1;
  stats->largest_changes->changes
    = apr_palloc(result_pool, count * sizeof(*stats->largest_changes->changes));

  /* allocate *all* entries before the path stringbufs.  This increases
   * cache locality and enhances performance significantly. */
  for (i = 0; i < count; ++i)
    stats->largest_changes->changes[i]
      = apr_palloc(result_pool, sizeof(**stats->largest_changes->changes));

  /* now initialize them and allocate the stringbufs */
  for (i = 0; i < count; ++i)
    {
      stats->largest_changes->changes[i]->size = 0;
      stats->largest_changes->changes[i]->revision = SVN_INVALID_REVNUM;
      stats->largest_changes->changes[i]->path
        = svn_stringbuf_create_ensure(1024, result_pool);
    }
}

/* Add entry for SIZE to HISTOGRAM.
 */
static void
add_to_histogram(svn_fs_fs__histogram_t *histogram,
                 apr_int64_t size)
{
  apr_int64_t shift = 0;

  while (((apr_int64_t)(1) << shift) <= size)
    shift++;

  histogram->total.count++;
  histogram->total.sum += size;
  histogram->lines[(apr_size_t)shift].count++;
  histogram->lines[(apr_size_t)shift].sum += size;
}

/* Update data aggregators in STATS with this representation of type KIND,
 * on-disk REP_SIZE and expanded node size EXPANDED_SIZE for PATH in REVSION.
 * PLAIN_ADDED indicates whether the node has a deltification predecessor.
 */
static void
add_change(svn_fs_fs__stats_t *stats,
           apr_uint64_t rep_size,
           apr_uint64_t expanded_size,
           svn_revnum_t revision,
           const char *path,
           rep_kind_t kind,
           svn_boolean_t plain_added)
{
  /* identify largest reps */
  if (rep_size >= stats->largest_changes->min_size)
    {
      apr_size_t i;
      svn_fs_fs__largest_changes_t *largest_changes = stats->largest_changes;
      svn_fs_fs__large_change_info_t *info
        = largest_changes->changes[largest_changes->count - 1];
      info->size = rep_size;
      info->revision = revision;
      svn_stringbuf_set(info->path, path);

      /* linear insertion but not too bad since count is low and insertions
       * near the end are more likely than close to front */
      for (i = largest_changes->count - 1; i > 0; --i)
        if (largest_changes->changes[i-1]->size >= rep_size)
          break;
        else
          largest_changes->changes[i] = largest_changes->changes[i-1];

      largest_changes->changes[i] = info;
      largest_changes->min_size
        = largest_changes->changes[largest_changes->count-1]->size;
    }

  /* global histograms */
  add_to_histogram(&stats->rep_size_histogram, rep_size);
  add_to_histogram(&stats->node_size_histogram, expanded_size);

  if (plain_added)
    {
      add_to_histogram(&stats->added_rep_size_histogram, rep_size);
      add_to_histogram(&stats->added_node_size_histogram, expanded_size);
    }

  /* specific histograms by type */
  switch (kind)
    {
      case unused_rep:
        add_to_histogram(&stats->unused_rep_histogram, rep_size);
        break;
      case dir_property_rep:
        add_to_histogram(&stats->dir_prop_rep_histogram, rep_size);
        add_to_histogram(&stats->dir_prop_histogram, expanded_size);
        break;
      case file_property_rep:
        add_to_histogram(&stats->file_prop_rep_histogram, rep_size);
        add_to_histogram(&stats->file_prop_histogram, expanded_size);
        break;
      case dir_rep:
        add_to_histogram(&stats->dir_rep_histogram, rep_size);
        add_to_histogram(&stats->dir_histogram, expanded_size);
        break;
      case file_rep:
        add_to_histogram(&stats->file_rep_histogram, rep_size);
        add_to_histogram(&stats->file_histogram, expanded_size);
        break;
    }

  /* by extension */
  if (kind == file_rep)
    {
      /* determine extension */
      svn_fs_fs__extension_info_t *info;
      const char * file_name = strrchr(path, '/');
      const char * extension = file_name ? strrchr(file_name, '.') : NULL;

      if (extension == NULL || extension == file_name + 1)
        extension = "(none)";

      /* get / auto-insert entry for this extension */
      info = apr_hash_get(stats->by_extension, extension, APR_HASH_KEY_STRING);
      if (info == NULL)
        {
          apr_pool_t *pool = apr_hash_pool_get(stats->by_extension);
          info = apr_pcalloc(pool, sizeof(*info));
          info->extension = apr_pstrdup(pool, extension);

          apr_hash_set(stats->by_extension, info->extension,
                       APR_HASH_KEY_STRING, info);
        }

      /* update per-extension histogram */
      add_to_histogram(&info->node_histogram, expanded_size);
      add_to_histogram(&info->rep_histogram, rep_size);
    }
}

/* Comparator used for binary search comparing the absolute file offset
 * of a representation to some other offset. DATA is a *rep_stats_t,
 * KEY is a pointer to an apr_uint64_t.
 */
static int
compare_representation_item_index(const void *data, const void *key)
{
  apr_uint64_t lhs = (*(const rep_stats_t *const *)data)->item_index;
  apr_uint64_t rhs = *(const apr_uint64_t *)key;

  if (lhs < rhs)
    return -1;
  return (lhs > rhs ? 1 : 0);
}

/* Find the revision_info_t object to the given REVISION in QUERY and
 * return it in *REVISION_INFO. For performance reasons, we skip the
 * lookup if the info is already provided.
 *
 * In that revision, look for the rep_stats_t object for item ITEM_INDEX.
 * If it already exists, set *IDX to its index in *REVISION_INFO's
 * representations list and return the representation object. Otherwise,
 * set the index to where it must be inserted and return NULL.
 */
static rep_stats_t *
find_representation(int *idx,
                    query_t *query,
                    revision_info_t **revision_info,
                    svn_revnum_t revision,
                    apr_uint64_t item_index)
{
  revision_info_t *info;
  *idx = -1;

  /* first let's find the revision */
  info = revision_info ? *revision_info : NULL;
  if (info == NULL || info->revision != revision)
    {
      info = APR_ARRAY_IDX(query->revisions, revision, revision_info_t*);
      if (revision_info)
        *revision_info = info;
    }

  /* not found -> no result */
  if (info == NULL)
    return NULL;

  /* look for the representation */
  *idx = svn_sort__bsearch_lower_bound(info->representations,
                                       &item_index,
                                       compare_representation_item_index);
  if (*idx < info->representations->nelts)
    {
      /* return the representation, if this is the one we were looking for */
      rep_stats_t *result
        = APR_ARRAY_IDX(info->representations, *idx, rep_stats_t *);
      if (result->item_index == item_index)
        return result;
    }

  /* not parsed, yet */
  return NULL;
}

/* Find / auto-construct the representation stats for REP in QUERY and
 * return it in *REPRESENTATION.
 *
 * If necessary, allocate the result in RESULT_POOL; use SCRATCH_POOL for
 * temporary allocations.
 */
static svn_error_t *
parse_representation(rep_stats_t **representation,
                     query_t *query,
                     representation_t *rep,
                     revision_info_t *revision_info,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  rep_stats_t *result;
  int idx;

  /* read location (revision, offset) and size */

  /* look it up */
  result = find_representation(&idx, query, &revision_info, rep->revision,
                               rep->item_index);
  if (!result)
    {
      /* not parsed, yet (probably a rep in the same revision).
       * Create a new rep object and determine its base rep as well.
       */
      result = apr_pcalloc(result_pool, sizeof(*result));
      result->revision = rep->revision;
      result->expanded_size = rep->expanded_size;
      result->item_index = rep->item_index;
      result->size = rep->size;

      /* In phys. addressing mode, follow link to the actual representation.
       * In log. addressing mode, we will find it already as part of our
       * linear walk through the whole file. */
      if (!svn_fs_fs__use_log_addressing(query->fs))
        {
          svn_fs_fs__rep_header_t *header;
          apr_off_t offset = revision_info->offset
                           + (apr_off_t)rep->item_index;

          SVN_ERR_ASSERT(revision_info->rev_file);
          SVN_ERR(svn_io_file_seek(revision_info->rev_file->file, APR_SET,
                                   &offset, scratch_pool));
          SVN_ERR(svn_fs_fs__read_rep_header(&header,
                                             revision_info->rev_file->stream,
                                             scratch_pool, scratch_pool));

          result->header_size = header->header_size;

          /* Determine length of the delta chain. */
          if (header->type == svn_fs_fs__rep_delta)
            {
              int base_idx;
              rep_stats_t *base_rep
                = find_representation(&base_idx, query, NULL,
                                      header->base_revision,
                                      header->base_item_index);

              result->chain_length = 1 + MIN(base_rep->chain_length,
                                             (apr_byte_t)0xfe);
            }
          else
            {
              result->chain_length = 1;
            }
        }

      svn_sort__array_insert(revision_info->representations, &result, idx);
    }

  *representation = result;

  return SVN_NO_ERROR;
}


/* forward declaration */
static svn_error_t *
read_noderev(query_t *query,
             svn_stringbuf_t *noderev_str,
             revision_info_t *revision_info,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool);

/* Read the noderev item at OFFSET in REVISION_INFO from the filesystem
 * provided by QUERY.  Return it in *NODEREV, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations.
 *
 * The textual representation of the noderev will be used to determine
 * the on-disk size of the noderev.  Only called in phys. addressing mode.
 */
static svn_error_t *
read_phsy_noderev(svn_stringbuf_t **noderev,
                  query_t *query,
                  apr_off_t offset,
                  revision_info_t *revision_info,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *noderev_str = svn_stringbuf_create_empty(result_pool);
  svn_stringbuf_t *line;
  svn_boolean_t eof;

  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Navigate the file stream to the start of noderev. */
  SVN_ERR_ASSERT(revision_info->rev_file);

  offset += revision_info->offset;
  SVN_ERR(svn_io_file_seek(revision_info->rev_file->file, APR_SET,
                           &offset, scratch_pool));

  /* Read it (terminated by an empty line) */
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_stream_readline(revision_info->rev_file->stream, &line,
                                  "\n", &eof, iterpool));
      svn_stringbuf_appendstr(noderev_str, line);
      svn_stringbuf_appendbyte(noderev_str, '\n');
    }
  while (line->len > 0 && !eof);

  /* Return the result. */
  *noderev = noderev_str;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Starting at the directory in NODEREV's text, read all DAG nodes,
 * directories and representations linked in that tree structure.
 * Store them in QUERY and REVISION_INFO.  Also, read them only once.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
parse_dir(query_t *query,
          node_revision_t *noderev,
          revision_info_t *revision_info,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  int i;
  apr_array_header_t *entries;
  SVN_ERR(svn_fs_fs__rep_contents_dir(&entries, query->fs, noderev,
                                      scratch_pool, scratch_pool));

  for (i = 0; i < entries->nelts; ++i)
    {
      svn_fs_dirent_t *dirent = APR_ARRAY_IDX(entries, i, svn_fs_dirent_t *);

      if (svn_fs_fs__id_rev(dirent->id) == revision_info->revision)
        {
          svn_stringbuf_t *noderev_str;
          svn_pool_clear(iterpool);

          SVN_ERR(read_phsy_noderev(&noderev_str, query,
                                    svn_fs_fs__id_item(dirent->id),
                                    revision_info, iterpool, iterpool));
          SVN_ERR(read_noderev(query, noderev_str, revision_info,
                               result_pool, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Parse the noderev given as NODEREV_STR and store the info in QUERY and
 * REVISION_INFO.  In phys. addressing mode, continue reading all DAG nodes,
 * directories and representations linked in that tree structure.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_noderev(query_t *query,
             svn_stringbuf_t *noderev_str,
             revision_info_t *revision_info,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  rep_stats_t *text = NULL;
  rep_stats_t *props = NULL;
  node_revision_t *noderev;

  svn_stream_t *stream = svn_stream_from_stringbuf(noderev_str, scratch_pool);
  SVN_ERR(svn_fs_fs__read_noderev(&noderev, stream, scratch_pool,
                                  scratch_pool));
  SVN_ERR(svn_fs_fs__fixup_expanded_size(query->fs, noderev->data_rep,
                                         scratch_pool));
  SVN_ERR(svn_fs_fs__fixup_expanded_size(query->fs, noderev->prop_rep,
                                         scratch_pool));

  if (noderev->data_rep)
    {
      SVN_ERR(parse_representation(&text, query,
                                   noderev->data_rep, revision_info,
                                   result_pool, scratch_pool));

      /* if we are the first to use this rep, mark it as "text rep" */
      if (++text->ref_count == 1)
        text->kind = noderev->kind == svn_node_dir ? dir_rep : file_rep;
    }

  if (noderev->prop_rep)
    {
      SVN_ERR(parse_representation(&props, query,
                                   noderev->prop_rep, revision_info,
                                   result_pool, scratch_pool));

      /* if we are the first to use this rep, mark it as "prop rep" */
      if (++props->ref_count == 1)
        props->kind = noderev->kind == svn_node_dir ? dir_property_rep
                                                    : file_property_rep;
    }

  /* record largest changes */
  if (text && text->ref_count == 1)
    add_change(query->stats, text->size, text->expanded_size, text->revision,
               noderev->created_path, text->kind, !noderev->predecessor_id);
  if (props && props->ref_count == 1)
    add_change(query->stats, props->size, props->expanded_size,
               props->revision, noderev->created_path, props->kind,
               !noderev->predecessor_id);

  /* if this is a directory and has not been processed, yet, read and
   * process it recursively */
  if (   noderev->kind == svn_node_dir && text && text->ref_count == 1
      && !svn_fs_fs__use_log_addressing(query->fs))
    SVN_ERR(parse_dir(query, noderev, revision_info, result_pool,
                      scratch_pool));

  /* update stats */
  if (noderev->kind == svn_node_dir)
    {
      revision_info->dir_noderev_size += noderev_str->len;
      revision_info->dir_noderev_count++;
    }
  else
    {
      revision_info->file_noderev_size += noderev_str->len;
      revision_info->file_noderev_count++;
    }

  return SVN_NO_ERROR;
}

/* For the revision given as REVISION_INFO within QUERY, determine the number
 * of entries in its changed paths list and store that info in REVISION_INFO.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
get_phys_change_count(query_t *query,
                      revision_info_t *revision_info,
                      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_fs_fs__changes_context_t *context;

  /* Fetch the first block of data. */
  SVN_ERR(svn_fs_fs__create_changes_context(&context, query->fs,
                                            revision_info->revision,
                                            scratch_pool));

  revision_info->change_count = 0;
  while (!context->eol)
    {
      apr_array_header_t *changes;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_fs_fs__get_changes(&changes, context, iterpool, iterpool));
      revision_info->change_count = changes->nelts;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Read header information for the revision stored in FILE_CONTENT (one
 * whole revision).  Return the offsets within FILE_CONTENT for the
 * *ROOT_NODEREV, the list of *CHANGES and its len in *CHANGES_LEN.
 * Use POOL for temporary allocations. */
static svn_error_t *
read_phys_revision(query_t *query,
                   revision_info_t *info,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  char buf[64];
  apr_off_t root_node_offset;
  apr_off_t changes_offset;
  svn_stringbuf_t *trailer;
  svn_stringbuf_t *noderev_str;

  /* Read the last 64 bytes of the revision (if long enough). */
  apr_off_t start = MAX(info->offset, info->end - sizeof(buf));
  apr_size_t len = (apr_size_t)(info->end - start);
  SVN_ERR(svn_io_file_seek(info->rev_file->file, APR_SET, &start,
                           scratch_pool));
  SVN_ERR(svn_io_file_read_full2(info->rev_file->file, buf, len, NULL, NULL,
                                 scratch_pool));
  trailer = svn_stringbuf_ncreate(buf, len, scratch_pool);

  /* Parse that trailer. */
  SVN_ERR(svn_fs_fs__parse_revision_trailer(&root_node_offset,
                                            &changes_offset, trailer,
                                            info->revision));
  SVN_ERR(get_phys_change_count(query, info, scratch_pool));

  /* Calculate the length of the changes list. */
  trailer = svn_fs_fs__unparse_revision_trailer(root_node_offset,
                                                changes_offset,
                                                scratch_pool);
  info->changes_len = info->end - info->offset - changes_offset
                    - trailer->len;

  /* Recursively read nodes added in this rev. */
  SVN_ERR(read_phsy_noderev(&noderev_str, query, root_node_offset, info,
                            scratch_pool, scratch_pool));
  SVN_ERR(read_noderev(query, noderev_str, info, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Read the content of the pack file staring at revision BASE physical
 * addressing mode and store it in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_phys_pack_file(query_t *query,
                    svn_revnum_t base,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_filesize_t file_size = 0;
  svn_fs_fs__revision_file_t *rev_file;

  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, query->fs, base,
                                           scratch_pool, scratch_pool));
  SVN_ERR(svn_io_file_size_get(&file_size, rev_file->file, scratch_pool));

  /* process each revision in the pack file */
  for (i = 0; i < query->shard_size; ++i)
    {
      revision_info_t *info;

      /* cancellation support */
      if (query->cancel_func)
        SVN_ERR(query->cancel_func(query->cancel_baton));

      /* create the revision info for the current rev */
      info = apr_pcalloc(result_pool, sizeof(*info));
      info->representations = apr_array_make(result_pool, 4,
                                             sizeof(rep_stats_t*));
      info->rev_file = rev_file;

      info->revision = base + i;
      SVN_ERR(svn_fs_fs__get_packed_offset(&info->offset, query->fs, base + i,
                                           iterpool));
      if (i + 1 == query->shard_size)
        info->end = file_size;
      else
        SVN_ERR(svn_fs_fs__get_packed_offset(&info->end, query->fs,
                                             base + i + 1, iterpool));

      SVN_ERR(read_phys_revision(query, info, result_pool, iterpool));

      info->representations = apr_array_copy(result_pool,
                                             info->representations);

      /* Done with this revision. */
      info->rev_file = NULL;

      /* put it into our container */
      APR_ARRAY_PUSH(query->revisions, revision_info_t*) = info;

      /* destroy temps */
      svn_pool_clear(iterpool);
    }

  /* Done with this pack file. */
  SVN_ERR(svn_fs_fs__close_revision_file(rev_file));

  /* one more pack file processed */
  if (query->progress_func)
    query->progress_func(base, query->progress_baton, scratch_pool);

  return SVN_NO_ERROR;
}

/* Read the content of the file for REVISION in physical addressing mode
 * and store its contents in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_phys_revision_file(query_t *query,
                        svn_revnum_t revision,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  revision_info_t *info = apr_pcalloc(result_pool, sizeof(*info));
  svn_filesize_t file_size = 0;
  svn_fs_fs__revision_file_t *rev_file;

  /* cancellation support */
  if (query->cancel_func)
    SVN_ERR(query->cancel_func(query->cancel_baton));

  /* read the whole pack file into memory */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, query->fs, revision,
                                           scratch_pool, scratch_pool));
  SVN_ERR(svn_io_file_size_get(&file_size, rev_file->file, scratch_pool));

  /* create the revision info for the current rev */
  info->representations = apr_array_make(result_pool, 4, sizeof(rep_stats_t*));

  info->rev_file = rev_file;
  info->revision = revision;
  info->offset = 0;
  info->end = file_size;

  SVN_ERR(read_phys_revision(query, info, result_pool, scratch_pool));

  /* Done with this revision. */
  SVN_ERR(svn_fs_fs__close_revision_file(rev_file));
  info->rev_file = NULL;

  /* put it into our container */
  APR_ARRAY_PUSH(query->revisions, revision_info_t*) = info;

  /* show progress every 1000 revs or so */
  if (query->progress_func)
    {
      if (query->shard_size && (revision % query->shard_size == 0))
        query->progress_func(revision, query->progress_baton, scratch_pool);
      if (!query->shard_size && (revision % 1000 == 0))
        query->progress_func(revision, query->progress_baton, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Given the unparsed changes list in CHANGES with LEN chars, return the
 * number of changed paths encoded in it.  Only used in log. addressing
 * mode.
 */
static apr_uint64_t
get_log_change_count(const char *changes,
                     apr_size_t len)
{
  apr_size_t lines = 0;
  const char *end = changes + len;

  /* line count */
  for (; changes < end; ++changes)
    if (*changes == '\n')
      ++lines;

  /* two lines per change */
  return lines / 2;
}

/* Read the item described by ENTRY from the REV_FILE and return the
 * respective byte sequence in *CONTENTS, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations
 */
static svn_error_t *
read_item(svn_stringbuf_t **contents,
          svn_fs_fs__revision_file_t *rev_file,
          svn_fs_fs__p2l_entry_t *entry,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *item = svn_stringbuf_create_ensure(entry->size,
                                                      result_pool);
  item->len = entry->size;
  item->data[item->len] = 0;

  SVN_ERR(svn_io_file_aligned_seek(rev_file->file, rev_file->block_size,
                                   NULL, entry->offset, scratch_pool));
  SVN_ERR(svn_io_file_read_full2(rev_file->file, item->data, item->len,
                                 NULL, NULL, scratch_pool));

  *contents = item;

  return SVN_NO_ERROR;
}

/* Predicate comparing the two rep_ref_t** LHS and RHS by the respective
 * representation's revision.
 */
static int
compare_representation_refs(const void *lhs, const void *rhs)
{
  svn_revnum_t lhs_rev = (*(const rep_ref_t *const *)lhs)->revision;
  svn_revnum_t rhs_rev = (*(const rep_ref_t *const *)rhs)->revision;

  if (lhs_rev < rhs_rev)
    return -1;
  return (lhs_rev > rhs_rev ? 1 : 0);
}

/* Given all the presentations found in a single rev / pack file as
 * rep_ref_t * in REP_REFS, update the delta chain lengths in QUERY.
 * REP_REFS and its contents can then be discarded.
 */
static svn_error_t *
resolve_representation_refs(query_t *query,
                            apr_array_header_t *rep_refs)
{
  int i;

  /* Because delta chains can only point to previous revs, after sorting
   * REP_REFS, all base refs have already been updated. */
  svn_sort__array(rep_refs, compare_representation_refs);

  /* Build up the CHAIN_LENGTH values. */
  for (i = 0; i < rep_refs->nelts; ++i)
    {
      int idx;
      rep_ref_t *ref = APR_ARRAY_IDX(rep_refs, i, rep_ref_t *);
      rep_stats_t *rep = find_representation(&idx, query, NULL,
                                             ref->revision, ref->item_index);

      /* No dangling pointers and all base reps have been processed. */
      SVN_ERR_ASSERT(rep);
      SVN_ERR_ASSERT(!rep->chain_length);

      /* Set the HEADER_SIZE as we found it during the scan. */
      rep->header_size = ref->header_size;

      /* The delta chain got 1 element longer. */
      if (ref->base_revision == SVN_INVALID_REVNUM)
        {
          rep->chain_length = 1;
        }
      else
        {
          rep_stats_t *base;

          base = find_representation(&idx, query, NULL, ref->base_revision,
                                     ref->base_item_index);
          SVN_ERR_ASSERT(base);
          SVN_ERR_ASSERT(base->chain_length);

          rep->chain_length = 1 + MIN(base->chain_length, (apr_byte_t)0xfe);
        }
    }

  return SVN_NO_ERROR;
}

/* Process the logically addressed revision contents of revisions BASE to
 * BASE + COUNT - 1 in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_log_rev_or_packfile(query_t *query,
                         svn_revnum_t base,
                         int count,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  fs_fs_data_t *ffd = query->fs->fsap_data;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_off_t max_offset;
  apr_off_t offset = 0;
  int i;
  svn_fs_fs__revision_file_t *rev_file;

  /* We collect the delta chain links as we scan the file.  Afterwards,
   * we determine the lengths of those delta chains and throw this
   * temporary container away. */
  apr_array_header_t *rep_refs = apr_array_make(scratch_pool, 64,
                                                sizeof(rep_ref_t *));

  /* we will process every revision in the rev / pack file */
  for (i = 0; i < count; ++i)
    {
      /* create the revision info for the current rev */
      revision_info_t *info = apr_pcalloc(result_pool, sizeof(*info));
      info->representations = apr_array_make(result_pool, 4,
                                             sizeof(rep_stats_t*));
      info->revision = base + i;

      APR_ARRAY_PUSH(query->revisions, revision_info_t*) = info;
    }

  /* open the pack / rev file that is covered by the p2l index */
  SVN_ERR(svn_fs_fs__open_pack_or_rev_file(&rev_file, query->fs, base,
                                           scratch_pool, iterpool));
  SVN_ERR(svn_fs_fs__p2l_get_max_offset(&max_offset, query->fs, rev_file,
                                        base, scratch_pool));

  /* record the whole pack size in the first rev so the total sum will
     still be correct */
  APR_ARRAY_IDX(query->revisions, base, revision_info_t*)->end = max_offset;

  /* for all offsets in the file, get the P2L index entries and process
     the interesting items (change lists, noderevs) */
  for (offset = 0; offset < max_offset; )
    {
      apr_array_header_t *entries;

      svn_pool_clear(iterpool);

      /* cancellation support */
      if (query->cancel_func)
        SVN_ERR(query->cancel_func(query->cancel_baton));

      /* get all entries for the current block */
      SVN_ERR(svn_fs_fs__p2l_index_lookup(&entries, query->fs, rev_file, base,
                                          offset, ffd->p2l_page_size,
                                          iterpool, iterpool));

      /* process all entries (and later continue with the next block) */
      for (i = 0; i < entries->nelts; ++i)
        {
          svn_stringbuf_t *item;
          revision_info_t *info;
          svn_fs_fs__p2l_entry_t *entry
            = &APR_ARRAY_IDX(entries, i, svn_fs_fs__p2l_entry_t);

          /* skip bits we previously processed */
          if (i == 0 && entry->offset < offset)
            continue;

          /* skip zero-sized entries */
          if (entry->size == 0)
            continue;

          /* read and process interesting items */
          info = APR_ARRAY_IDX(query->revisions, entry->item.revision,
                               revision_info_t*);

          if (entry->type == SVN_FS_FS__ITEM_TYPE_NODEREV)
            {
              SVN_ERR(read_item(&item, rev_file, entry, iterpool, iterpool));
              SVN_ERR(read_noderev(query, item, info, result_pool, iterpool));
            }
          else if (entry->type == SVN_FS_FS__ITEM_TYPE_CHANGES)
            {
              SVN_ERR(read_item(&item, rev_file, entry, iterpool, iterpool));
              info->change_count
                = get_log_change_count(item->data + 0, item->len);
              info->changes_len += entry->size;
            }
          else if (   (entry->type == SVN_FS_FS__ITEM_TYPE_FILE_REP)
                   || (entry->type == SVN_FS_FS__ITEM_TYPE_DIR_REP)
                   || (entry->type == SVN_FS_FS__ITEM_TYPE_FILE_PROPS)
                   || (entry->type == SVN_FS_FS__ITEM_TYPE_DIR_PROPS))
            {
              /* Collect the delta chain link. */
              svn_fs_fs__rep_header_t *header;
              rep_ref_t *ref = apr_pcalloc(scratch_pool, sizeof(*ref));

              SVN_ERR(svn_io_file_aligned_seek(rev_file->file,
                                               rev_file->block_size,
                                               NULL, entry->offset,
                                               iterpool));
              SVN_ERR(svn_fs_fs__read_rep_header(&header,
                                                 rev_file->stream,
                                                 iterpool, iterpool));

              ref->header_size = header->header_size;
              ref->revision = entry->item.revision;
              ref->item_index = entry->item.number;

              if (header->type == svn_fs_fs__rep_delta)
                {
                  ref->base_item_index = header->base_item_index;
                  ref->base_revision = header->base_revision;
                }
              else
                {
                  ref->base_item_index = SVN_FS_FS__ITEM_INDEX_UNUSED;
                  ref->base_revision = SVN_INVALID_REVNUM;
                }

              APR_ARRAY_PUSH(rep_refs, rep_ref_t *) = ref;
            }

          /* advance offset */
          offset += entry->size;
        }
    }

  /* Resolve the delta chain links. */
  SVN_ERR(resolve_representation_refs(query, rep_refs));

  /* clean up and close file handles */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Read the content of the pack file staring at revision BASE logical
 * addressing mode and store it in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_log_pack_file(query_t *query,
                   svn_revnum_t base,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR(read_log_rev_or_packfile(query, base, query->shard_size,
                                   result_pool, scratch_pool));

  /* one more pack file processed */
  if (query->progress_func)
    query->progress_func(base, query->progress_baton, scratch_pool);

  return SVN_NO_ERROR;
}

/* Read the content of the file for REVISION in logical addressing mode
 * and store its contents in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_log_revision_file(query_t *query,
                       svn_revnum_t revision,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR(read_log_rev_or_packfile(query, revision, 1,
                                   result_pool, scratch_pool));

  /* show progress every 1000 revs or so */
  if (query->progress_func)
    {
      if (query->shard_size && (revision % query->shard_size == 0))
        query->progress_func(revision, query->progress_baton, scratch_pool);
      if (!query->shard_size && (revision % 1000 == 0))
        query->progress_func(revision, query->progress_baton, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Read the repository and collect the stats info in QUERY.
 *
 * Use RESULT_POOL for persistent allocations and SCRATCH_POOL for
 * temporaries.
 */
static svn_error_t *
read_revisions(query_t *query,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_revnum_t revision;

  /* read all packed revs */
  for ( revision = 0
      ; revision < query->min_unpacked_rev
      ; revision += query->shard_size)
    {
      svn_pool_clear(iterpool);

      if (svn_fs_fs__use_log_addressing(query->fs))
        SVN_ERR(read_log_pack_file(query, revision, result_pool, iterpool));
      else
        SVN_ERR(read_phys_pack_file(query, revision, result_pool, iterpool));
    }

  /* read non-packed revs */
  for ( ; revision <= query->head; ++revision)
    {
      svn_pool_clear(iterpool);

      if (svn_fs_fs__use_log_addressing(query->fs))
        SVN_ERR(read_log_revision_file(query, revision, result_pool,
                                       iterpool));
      else
        SVN_ERR(read_phys_revision_file(query, revision, result_pool,
                                        iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Accumulate stats of REP in STATS.
 */
static void
add_rep_pack_stats(svn_fs_fs__rep_pack_stats_t *stats,
                   rep_stats_t *rep)
{
  stats->count++;

  stats->packed_size += rep->size;
  stats->expanded_size += rep->expanded_size;
  stats->overhead_size += rep->header_size + 7 /* ENDREP\n */;
}

/* Accumulate stats of REP in STATS.
 */
static void
add_rep_stats(svn_fs_fs__representation_stats_t *stats,
              rep_stats_t *rep)
{
  add_rep_pack_stats(&stats->total, rep);
  if (rep->ref_count == 1)
    add_rep_pack_stats(&stats->uniques, rep);
  else
    add_rep_pack_stats(&stats->shared, rep);

  stats->references += rep->ref_count;
  stats->expanded_size += rep->ref_count * rep->expanded_size;
  stats->chain_len += rep->chain_length;
}

/* Aggregate the info the in revision_info_t * array REVISIONS into the
 * respectve fields of STATS.
 */
static void
aggregate_stats(const apr_array_header_t *revisions,
                svn_fs_fs__stats_t *stats)
{
  int i, k;

  /* aggregate info from all revisions */
  stats->revision_count = revisions->nelts;
  for (i = 0; i < revisions->nelts; ++i)
    {
      revision_info_t *revision = APR_ARRAY_IDX(revisions, i,
                                                revision_info_t *);

      /* data gathered on a revision level */
      stats->change_count += revision->change_count;
      stats->change_len += revision->changes_len;
      stats->total_size += revision->end - revision->offset;

      stats->dir_node_stats.count += revision->dir_noderev_count;
      stats->dir_node_stats.size += revision->dir_noderev_size;
      stats->file_node_stats.count += revision->file_noderev_count;
      stats->file_node_stats.size += revision->file_noderev_size;
      stats->total_node_stats.count += revision->dir_noderev_count
                                    + revision->file_noderev_count;
      stats->total_node_stats.size += revision->dir_noderev_size
                                   + revision->file_noderev_size;

      /* process representations */
      for (k = 0; k < revision->representations->nelts; ++k)
        {
          rep_stats_t *rep = APR_ARRAY_IDX(revision->representations, k,
                                           rep_stats_t *);

          /* accumulate in the right bucket */
          switch(rep->kind)
            {
              case file_rep:
                add_rep_stats(&stats->file_rep_stats, rep);
                break;
              case dir_rep:
                add_rep_stats(&stats->dir_rep_stats, rep);
                break;
              case file_property_rep:
                add_rep_stats(&stats->file_prop_rep_stats, rep);
                break;
              case dir_property_rep:
                add_rep_stats(&stats->dir_prop_rep_stats, rep);
                break;
              default:
                break;
            }

          add_rep_stats(&stats->total_rep_stats, rep);
        }
    }
}

/* Return a new svn_fs_fs__stats_t instance, allocated in RESULT_POOL.
 */
static svn_fs_fs__stats_t *
create_stats(apr_pool_t *result_pool)
{
  svn_fs_fs__stats_t *stats = apr_pcalloc(result_pool, sizeof(*stats));

  initialize_largest_changes(stats, 64, result_pool);
  stats->by_extension = apr_hash_make(result_pool);

  return stats;
}

/* Create a *QUERY, allocated in RESULT_POOL, reading filesystem FS and
 * collecting results in STATS.  Store the optional PROCESS_FUNC and
 * PROGRESS_BATON as well as CANCEL_FUNC and CANCEL_BATON in *QUERY, too.
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
create_query(query_t **query,
             svn_fs_t *fs,
             svn_fs_fs__stats_t *stats,
             svn_fs_progress_notify_func_t progress_func,
             void *progress_baton,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *result_pool,
             apr_pool_t *scratch_pool)
{
  *query = apr_pcalloc(result_pool, sizeof(**query));

  /* Read repository dimensions. */
  (*query)->shard_size = svn_fs_fs__shard_size(fs);
  SVN_ERR(svn_fs_fs__youngest_rev(&(*query)->head, fs, scratch_pool));
  SVN_ERR(svn_fs_fs__min_unpacked_rev(&(*query)->min_unpacked_rev, fs,
                                      scratch_pool));

  /* create data containers and caches
   * Note: this assumes that int is at least 32-bits and that we only support
   * 32-bit wide revision numbers (actually 31-bits due to the signedness
   * of both the nelts field of the array and our revision numbers). This
   * means this code will fail on platforms where int is less than 32-bits
   * and the repository has more revisions than int can hold. */
  (*query)->revisions = apr_array_make(result_pool, (int) (*query)->head + 1,
                                       sizeof(revision_info_t *));
  (*query)->null_base = apr_pcalloc(result_pool,
                                    sizeof(*(*query)->null_base));

  /* Store other parameters */
  (*query)->fs = fs;
  (*query)->stats = stats;
  (*query)->progress_func = progress_func;
  (*query)->progress_baton = progress_baton;
  (*query)->cancel_func = cancel_func;
  (*query)->cancel_baton = cancel_baton;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__get_stats(svn_fs_fs__stats_t **stats,
                     svn_fs_t *fs,
                     svn_fs_progress_notify_func_t progress_func,
                     void *progress_baton,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  query_t *query;

  *stats = create_stats(result_pool);
  SVN_ERR(create_query(&query, fs, *stats, progress_func, progress_baton,
                       cancel_func, cancel_baton, scratch_pool,
                       scratch_pool));
  SVN_ERR(read_revisions(query, scratch_pool, scratch_pool));
  aggregate_stats(query->revisions, *stats);

  return SVN_NO_ERROR;
}
