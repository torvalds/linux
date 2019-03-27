/* dag_cache.c : DAG walker and node cache.
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


/* The job of this layer is to take a filesystem with lots of node
   sharing going on --- the real DAG filesystem as it appears in the
   database --- and make it look and act like an ordinary tree
   filesystem, with no sharing.

   We do just-in-time cloning: you can walk from some unfinished
   transaction's root down into directories and files shared with
   committed revisions; as soon as you try to change something, the
   appropriate nodes get cloned (and parent directory entries updated)
   invisibly, behind your back.  Any other references you have to
   nodes that have been cloned by other changes, even made by other
   processes, are automatically updated to point to the right clones.  */


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_mergeinfo.h"
#include "svn_fs.h"
#include "svn_props.h"
#include "svn_sorts.h"

#include "fs.h"
#include "dag.h"
#include "dag_cache.h"
#include "lock.h"
#include "tree.h"
#include "fs_x.h"
#include "fs_id.h"
#include "temp_serializer.h"
#include "cached_data.h"
#include "transaction.h"
#include "pack.h"
#include "util.h"

#include "private/svn_mergeinfo_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_fs_util.h"
#include "private/svn_fspath.h"
#include "../libsvn_fs/fs-loader.h"


/*** Path handling ***/

/* DAG caching uses "normalized" paths - which are a relaxed form of
   canonical relpaths.  They omit the leading '/' of the abspath and trim
   any trailing '/'.  Any sequences of '//' will be kept as the path walker
   simply skips over them.

   Non-canonical sections of the path will therefore only impact efficiency
   (extra walker iterations and possibly duplicated entries in the cache)
   but not correctness.

   Another optimization is that we don't NUL-terminate the path but strictly
   use its length info.  That way, it can be traversed easily without
   chopping it up and patching it together again.  ultimately, however,
   the path string is NUL-terminated because we wrapped a NUL-terminated
   C string.
 */

/* Set *RESULT to a normalized version of PATH without actually copying any
   string contents.

   For convenience, return the RESULT pointer as the function value too. */
static svn_string_t *
normalize_path(svn_string_t *result,
               const char *path)
{
  apr_size_t len;

  if (path[0] == '/')
    ++path;

  len = strlen(path);
  while (len && path[len-1] == '/')
    --len;

  result->data = path;
  result->len = len;

  return result;
}

/* Extend PATH, i.e. increase its LEN, to cover the next segment.  Skip
   sequences of '/'.  Store the segment in ENTRY and return a pointer to
   it C string representation.  If no segment has been found (end of path),
   return NULL. */
static const char *
next_entry_name(svn_string_t *path,
                svn_stringbuf_t *entry)
{
  const char *segment_start;
  const char *segment_end;

  /* Moving to the next segment, skip separators
     (normalized does not imply canonical). */
  segment_start = path->data + path->len;
  while (*segment_start == '/')
    ++segment_start;

  /* End of path? */
  if (*segment_start == '\0')
    return NULL;

  /* Find the end of this segment.  Note that strchr would not give us the
     length of the last segment. */
  segment_end = segment_start;
  while (*segment_end != '/' && *segment_end != '\0')
    ++segment_end;

  /* Copy the segment into the result buffer. */
  svn_stringbuf_setempty(entry);
  svn_stringbuf_appendbytes(entry, segment_start,
                            segment_end - segment_start);

  /* Extend the "visible" part of the path to the end of that segment. */
  path->len = segment_end - path->data;

  /* Indicate that we found something. */
  return entry->data;
}

/* Split the normalized PATH into its last segment the corresponding parent
   directory.  Store them in ENTRY and DIRECTORY, respectively.

   If PATH is empty, return FALSE and produce no output.
   Otherwise, return TRUE.
 */
static svn_boolean_t
extract_last_segment(const svn_string_t *path,
                     svn_string_t *directory,
                     svn_stringbuf_t *entry)
{
  const char *segment_start;
  const char *parent_end;

  /* Edge case.  We can't navigate in empty paths. */
  if (path->len == 0)
    return FALSE;

  /* Find the start of the last segment.  Note that normalized paths never
     start nor end with a '/'. */
  segment_start = path->data + path->len - 1;
  while (*segment_start != '/' && segment_start != path->data)
    --segment_start;

  /* At root level already, i.e. no parent? */
  if (segment_start == path->data)
    {
      /* Construct result. */
      directory->data = "";
      directory->len = 0;

      svn_stringbuf_setempty(entry);
      svn_stringbuf_appendbytes(entry, path->data, path->len);

      return TRUE;
    }

  /* Find the end of the parent directory. */
  parent_end = segment_start;
  while (parent_end[-1] == '/')
    --parent_end;

  /* Construct result. */
  directory->data = path->data;
  directory->len = parent_end - path->data;

  ++segment_start; /* previously pointed to the last '/'. */
  svn_stringbuf_setempty(entry);
  svn_stringbuf_appendbytes(entry, segment_start,
                            path->len - (segment_start - path->data));

  return TRUE;
}


/*** Node Caching ***/

/* 1st level cache */

/* An entry in the first-level cache.  REVISION and PATH form the key that
   will ultimately be matched.
 */
typedef struct cache_entry_t
{
  /* hash value derived from PATH, REVISION.
     Used to short-circuit failed lookups. */
  apr_uint32_t hash_value;

  /* change set to which the NODE belongs */
  svn_fs_x__change_set_t change_set;

  /* path of the NODE */
  char *path;

  /* cached value of strlen(PATH). */
  apr_size_t path_len;

  /* the node allocated in the cache's pool. NULL for empty entries. */
  dag_node_t *node;
} cache_entry_t;

/* Number of entries in the cache.  Keep this low to keep pressure on the
   CPU caches low as well.  A binary value is most efficient.  If we walk
   a directory tree, we want enough entries to store nodes for all files
   without overwriting the nodes for the parent folder.  That way, there
   will be no unnecessary misses (except for a few random ones caused by
   hash collision).

   The actual number of instances may be higher but entries that got
   overwritten are no longer visible.
 */
enum { BUCKET_COUNT = 256 };

/* The actual cache structure.  All nodes will be allocated in POOL.
   When the number of INSERTIONS (i.e. objects created form that pool)
   exceeds a certain threshold, the pool will be cleared and the cache
   with it.
 */
struct svn_fs_x__dag_cache_t
{
  /* fixed number of (possibly empty) cache entries */
  cache_entry_t buckets[BUCKET_COUNT];

  /* pool used for all node allocation */
  apr_pool_t *pool;

  /* number of entries created from POOL since the last cleanup */
  apr_size_t insertions;

  /* Property lookups etc. have a very high locality (75% re-hit).
     Thus, remember the last hit location for optimistic lookup. */
  apr_size_t last_hit;

  /* Position of the last bucket hit that actually had a DAG node in it.
     LAST_HIT may refer to a bucket that matches path@rev but has not
     its NODE element set, yet.
     This value is a mere hint for optimistic lookup and any value is
     valid (as long as it is < BUCKET_COUNT). */
  apr_size_t last_non_empty;
};

svn_fs_x__dag_cache_t*
svn_fs_x__create_dag_cache(apr_pool_t *result_pool)
{
  svn_fs_x__dag_cache_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->pool = svn_pool_create(result_pool);

  return result;
}

/* Clears the CACHE at regular intervals (destroying all cached nodes).
 * Return TRUE if the cache got cleared and previously obtained references
 * to cache contents have become invalid.
 */
static svn_boolean_t
auto_clear_dag_cache(svn_fs_x__dag_cache_t* cache)
{
  if (cache->insertions <= BUCKET_COUNT)
    return FALSE;

  svn_pool_clear(cache->pool);

  memset(cache->buckets, 0, sizeof(cache->buckets));
  cache->insertions = 0;

  return TRUE;
}

/* For the given CHANGE_SET and PATH, return the respective entry in CACHE.
   If the entry is empty, its NODE member will be NULL and the caller
   may then set it to the corresponding DAG node allocated in CACHE->POOL.
 */
static cache_entry_t *
cache_lookup(svn_fs_x__dag_cache_t *cache,
             svn_fs_x__change_set_t change_set,
             const svn_string_t *path)
{
  apr_size_t i, bucket_index;
  apr_size_t path_len = path->len;
  apr_uint32_t hash_value = (apr_uint32_t)(apr_uint64_t)change_set;

#if SVN_UNALIGNED_ACCESS_IS_OK
  /* "randomizing" / distributing factor used in our hash function */
  const apr_uint32_t factor = 0xd1f3da69;
#endif

  /* optimistic lookup: hit the same bucket again? */
  cache_entry_t *result = &cache->buckets[cache->last_hit];
  if (   (result->change_set == change_set)
      && (result->path_len == path_len)
      && !memcmp(result->path, path->data, path_len))
    {
      /* Remember the position of the last node we found in this cache. */
      if (result->node)
        cache->last_non_empty = cache->last_hit;

      return result;
    }

  /* need to do a full lookup.  Calculate the hash value
     (HASH_VALUE has been initialized to REVISION). */
  i = 0;
#if SVN_UNALIGNED_ACCESS_IS_OK
  /* We relax the dependency chain between iterations by processing
     two chunks from the input per hash_value self-multiplication.
     The HASH_VALUE update latency is now 1 MUL latency + 1 ADD latency
     per 2 chunks instead of 1 chunk.
   */
  for (; i + 8 <= path_len; i += 8)
    hash_value = hash_value * factor * factor
               + (  *(const apr_uint32_t*)(path->data + i) * factor
                  + *(const apr_uint32_t*)(path->data + i + 4));
#endif

  for (; i < path_len; ++i)
    /* Help GCC to minimize the HASH_VALUE update latency by splitting the
       MUL 33 of the naive implementation: h = h * 33 + path[i].  This
       shortens the dependency chain from 1 shift + 2 ADDs to 1 shift + 1 ADD.
     */
    hash_value = hash_value * 32 + (hash_value + (apr_byte_t)path->data[i]);

  bucket_index = hash_value + (hash_value >> 16);
  bucket_index = (bucket_index + (bucket_index >> 8)) % BUCKET_COUNT;

  /* access the corresponding bucket and remember its location */
  result = &cache->buckets[bucket_index];
  cache->last_hit = bucket_index;

  /* if it is *NOT* a match,  clear the bucket, expect the caller to fill
     in the node and count it as an insertion */
  if (   (result->hash_value != hash_value)
      || (result->change_set != change_set)
      || (result->path_len != path_len)
      || memcmp(result->path, path->data, path_len))
    {
      result->hash_value = hash_value;
      result->change_set = change_set;

      if (result->path_len < path_len || result->path_len == 0)
        result->path = apr_palloc(cache->pool, path_len + 1);
      result->path_len = path_len;

      memcpy(result->path, path->data, path_len);
      result->path[path_len] = 0;

      result->node = NULL;

      cache->insertions++;
    }
  else if (result->node)
    {
      /* This bucket is valid & has a suitable DAG node in it.
         Remember its location. */
      cache->last_non_empty = bucket_index;
    }

  return result;
}

/* Optimistic lookup using the last seen non-empty location in CACHE.
   Return the node of that entry, if it is still in use and matches PATH.
   Return NULL otherwise. */
static dag_node_t *
cache_lookup_last_path(svn_fs_x__dag_cache_t *cache,
                       const svn_string_t *path)
{
  cache_entry_t *result = &cache->buckets[cache->last_non_empty];

  if (   result->node
      && (result->path_len == path->len)
      && !memcmp(result->path, path->data, path->len))
    {
      return result->node;
    }

  return NULL;
}

/* Return the cached DAG node for PATH from ROOT's node cache, or NULL if
   the node isn't cached.
 */
static dag_node_t *
dag_node_cache_get(svn_fs_root_t *root,
                   const svn_string_t *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;
  svn_fs_x__change_set_t change_set = svn_fs_x__root_change_set(root);

  auto_clear_dag_cache(ffd->dag_node_cache);
  return cache_lookup(ffd->dag_node_cache, change_set, path)->node;
}


void
svn_fs_x__update_dag_cache(dag_node_t *node)
{
  svn_fs_x__data_t *ffd = svn_fs_x__dag_get_fs(node)->fsap_data;
  const char *path = svn_fs_x__dag_get_created_path(node);
  svn_fs_x__dag_cache_t *cache = ffd->dag_node_cache;

  cache_entry_t *bucket;
  svn_string_t normalized;

  auto_clear_dag_cache(cache);
  bucket = cache_lookup(cache, svn_fs_x__dag_get_id(node)->change_set,
                        normalize_path(&normalized, path));
  bucket->node = svn_fs_x__dag_dup(node, cache->pool);
}

void
svn_fs_x__invalidate_dag_cache(svn_fs_root_t *root,
                               const char *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;
  svn_fs_x__dag_cache_t *cache = ffd->dag_node_cache;
  svn_fs_x__change_set_t change_set = svn_fs_x__root_change_set(root);

  apr_size_t i;
  for (i = 0; i < BUCKET_COUNT; ++i)
    {
      cache_entry_t *bucket = &cache->buckets[i];
      if (bucket->change_set == change_set && bucket->node)
        {
          /* The call to svn_relpath_skip_ancestor() will require both
             parameters to be canonical.  Since we allow for non-canonical
             paths in our cache (unlikely to actually happen), we drop all
             such entries.
           */
          if (!svn_relpath_is_canonical(bucket->path)
              || svn_relpath_skip_ancestor(path + 1, bucket->path))
            bucket->node = NULL;
        }
    }
}


/* Traversing directory paths.  */

/* Try a short-cut for the open_path() function using the last node accessed.
 * If that ROOT is that nodes's "created rev" and PATH matches its "created-
 * path", return the node in *NODE_P.  Set it to NULL otherwise.
 *
 * This function is used to support ra_serf-style access patterns where we
 * are first asked for path@rev and then for path@c_rev of the same node.
 * The shortcut works by ignoring the "rev" part of the cache key and then
 * checking whether we got lucky.  Lookup and verification are both quick
 * plus there are many early outs for common types of mismatch.
 */
static svn_error_t *
try_match_last_node(dag_node_t **node_p,
                    svn_fs_root_t *root,
                    const svn_string_t *path)
{
  svn_fs_x__data_t *ffd = root->fs->fsap_data;

  /* Optimistic lookup: if the last node returned from the cache applied to
     the same PATH, return it in NODE. */
  dag_node_t *node = cache_lookup_last_path(ffd->dag_node_cache, path);

  /* Did we get a bucket with a committed node? */
  if (node && !svn_fs_x__dag_check_mutable(node))
    {
      /* Get the path&rev pair at which this node was created.
         This is repository location for which this node is _known_ to be
         the right lookup result irrespective of how we found it. */
      const char *created_path
        = svn_fs_x__dag_get_created_path(node) + 1;
      svn_revnum_t revision = svn_fs_x__dag_get_revision(node);

      /* Is it an exact match? */
      if (   revision == root->rev
          && strlen(created_path) == path->len
          && memcmp(created_path, path->data, path->len) == 0)
        {
          svn_fs_x__dag_cache_t *cache = ffd->dag_node_cache;

          /* Insert NODE into the cache at a second location.
             In a fraction of all calls, the auto-cleanup may
             cause NODE to become invalid. */
          if (!auto_clear_dag_cache(cache))
            {
              /* Cache it under its full path@rev access path. */
              svn_fs_x__change_set_t change_set
                = svn_fs_x__change_set_by_rev(revision);
              cache_entry_t *bucket = cache_lookup(cache, change_set, path);
              bucket->node = node;

              *node_p = node;
              return SVN_NO_ERROR;
            }
        }
    }

  *node_p = NULL;
  return SVN_NO_ERROR;
}


/* From directory node PARENT, under ROOT, go one step down to the entry
   NAME and return a reference to it in *CHILD_P.

   PATH is the combination of PARENT's path and NAME and is provided by
   the caller such that we don't have to construct it here ourselves.
   Similarly, CHANGE_SET is redundant with ROOT.

   If the directory entry cannot be found, instead of returning an error,
   *CHILD_P will be set to NULL if ALLOW_EMPTY is TRUE.

   NOTE: *NODE_P will live within the DAG cache and we merely return a
   reference to it.  Hence, it will invalid upon the next cache insertion.
   Callers must create a copy if they want a non-temporary object.
*/
static svn_error_t *
dag_step(dag_node_t **child_p,
         svn_fs_root_t *root,
         dag_node_t *parent,
         const char *name,
         const svn_string_t *path,
         svn_fs_x__change_set_t change_set,
         svn_boolean_t allow_empty,
         apr_pool_t *scratch_pool)
{
  svn_fs_t *fs = svn_fs_x__dag_get_fs(parent);
  svn_fs_x__data_t *ffd = fs->fsap_data;
  cache_entry_t *bucket;
  svn_fs_x__id_t node_id;

  /* Locate the corresponding cache entry.  We may need PARENT to remain
     valid for later use, so don't call auto_clear_dag_cache() here. */
  bucket = cache_lookup(ffd->dag_node_cache, change_set, path);
  if (bucket->node)
    {
      /* Already cached. Return a reference to the cached object. */
      *child_p = bucket->node;
      return SVN_NO_ERROR;
    }

  /* Get the ID of the node we are looking for.  The function call checks
     for various error conditions such like PARENT not being a directory. */
  SVN_ERR(svn_fs_x__dir_entry_id(&node_id, parent, name, scratch_pool));
  if (! svn_fs_x__id_used(&node_id))
    {
      const char *dir;

      /* No such directory entry.  Is a simple NULL result o.k.? */
      if (allow_empty)
        {
          *child_p = NULL;
          return SVN_NO_ERROR;
        }

      /* Produce an appropriate error message. */
      dir = apr_pstrmemdup(scratch_pool, path->data, path->len);
      dir = svn_fs__canonicalize_abspath(dir, scratch_pool);

      return SVN_FS__NOT_FOUND(root, dir);
    }

  /* We are about to add a new entry to the cache.  Periodically clear it.
     If we had to clear it just now (< 1% chance), re-add the entry for our
     item. */
  if (auto_clear_dag_cache(ffd->dag_node_cache))
    bucket = cache_lookup(ffd->dag_node_cache, change_set, path);

  /* Construct the DAG node object for NODE_ID. Let it live in the cache. */
  SVN_ERR(svn_fs_x__dag_get_node(&bucket->node, fs, &node_id,
                                 ffd->dag_node_cache->pool,
                                 scratch_pool));

  /* Return a reference to the cached object. */
  *child_p = bucket->node;
  return SVN_NO_ERROR;
}

/* Return the CHANGE_SET's root node in *NODE_P.  ROOT is the FS API root
   object for CHANGE_SET.  Use SCRATCH_POOL for temporary allocations.

   NOTE: *NODE_P will live within the DAG cache and we merely return a
   reference to it.  Hence, it will invalid upon the next cache insertion.
   Callers must create a copy if they want a non-temporary object.
 */
static svn_error_t *
get_root_node(dag_node_t **node_p,
              svn_fs_root_t *root,
              svn_fs_x__change_set_t change_set,
              apr_pool_t *scratch_pool)
{
  svn_fs_t *fs = root->fs;
  svn_fs_x__data_t *ffd = fs->fsap_data;
  cache_entry_t *bucket;
  const svn_string_t path = { "", 0 };

  /* Auto-insert the node in the cache. */
  auto_clear_dag_cache(ffd->dag_node_cache);
  bucket = cache_lookup(ffd->dag_node_cache, change_set, &path);

  /* If it is not already cached, construct the DAG node object for NODE_ID.
     Let it live in the cache.  Sadly, we often can't reuse txn DAG nodes. */
  if (bucket->node == NULL)
    SVN_ERR(svn_fs_x__dag_root(&bucket->node, fs, change_set,
                               ffd->dag_node_cache->pool, scratch_pool));

  /* Return a reference to the cached object. */
  *node_p = bucket->node;
  return SVN_NO_ERROR;
}

/* Walk the DAG starting at ROOT, following PATH and return a reference to
   the target node in *NODE_P.   Use SCRATCH_POOL for temporary allocations.

   NOTE: *NODE_P will live within the DAG cache and we merely return a
   reference to it.  Hence, it will invalid upon the next cache insertion.
   Callers must create a copy if they want a non-temporary object.
*/
static svn_error_t *
walk_dag_path(dag_node_t **node_p,
              svn_fs_root_t *root,
              svn_string_t *path,
              apr_pool_t *scratch_pool)
{
  dag_node_t *here = NULL; /* The directory we're currently looking at.  */
  apr_pool_t *iterpool;
  svn_fs_x__change_set_t change_set = svn_fs_x__root_change_set(root);
  const char *entry;
  svn_string_t directory;
  svn_stringbuf_t *entry_buffer;

  /* Special case: root directory.
     We will later assume that all paths have at least one parent level,
     so we must check here for those that don't. */
  if (path->len == 0)
    return svn_error_trace(get_root_node(node_p, root, change_set,
                                         scratch_pool));

  /* Callers often traverse the DAG in some path-based order or along the
     history segments.  That allows us to try a few guesses about where to
     find the next item.  This is only useful if the caller didn't request
     the full parent chain. */

  /* First attempt: Assume that we access the DAG for the same path as
     in the last lookup but for a different revision that happens to be
     the last revision that touched the respective node.  This is a
     common pattern when e.g. checking out over ra_serf.  Note that this
     will only work for committed data as the revision info for nodes in
     txns is bogus.

     This shortcut is quick and will exit this function upon success.
     So, try it first. */
  if (!root->is_txn_root)
    {
      SVN_ERR(try_match_last_node(node_p, root, path));

      /* Did the shortcut work? */
      if (*node_p)
          return SVN_NO_ERROR;
    }

  /* Second attempt: Try starting the lookup immediately at the parent
     node.  We will often have recently accessed either a sibling or
     said parent directory itself for the same revision.  ENTRY will
     point to the last '/' in PATH. */
  entry_buffer = svn_stringbuf_create_ensure(64, scratch_pool);
  if (extract_last_segment(path, &directory, entry_buffer))
    {
      here = dag_node_cache_get(root, &directory);

      /* Did the shortcut work? */
      if (here)
        return svn_error_trace(dag_step(node_p, root, here,
                                        entry_buffer->data, path,
                                        change_set, FALSE, scratch_pool));
    }

  /* Now there is something to iterate over. Thus, create the ITERPOOL. */
  iterpool = svn_pool_create(scratch_pool);

  /* Make a parent_path item for the root node, using its own current
     copy id.  */
  SVN_ERR(get_root_node(&here, root, change_set, iterpool));
  path->len = 0;

  /* Walk the path segment by segment. */
  for (entry = next_entry_name(path, entry_buffer);
       entry;
       entry = next_entry_name(path, entry_buffer))
    {
      svn_pool_clear(iterpool);

      /* Note that HERE is allocated from the DAG node cache and will
         therefore survive the ITERPOOL cleanup. */
      SVN_ERR(dag_step(&here, root, here, entry, path, change_set, FALSE,
                       iterpool));
    }

  svn_pool_destroy(iterpool);
  *node_p = here;

  return SVN_NO_ERROR;
}


/* Return a text string describing the absolute path of parent path
   DAG_PATH.  It will be allocated in POOL. */
static const char *
parent_path_path(svn_fs_x__dag_path_t *dag_path,
                 apr_pool_t *pool)
{
  const char *path_so_far = "/";
  if (dag_path->parent)
    path_so_far = parent_path_path(dag_path->parent, pool);
  return dag_path->entry
    ? svn_fspath__join(path_so_far, dag_path->entry, pool)
    : path_so_far;
}


/* Choose a copy ID inheritance method *INHERIT_P to be used in the
   event that immutable node CHILD in FS needs to be made mutable.  If
   the inheritance method is copy_id_inherit_new, also return a
   *COPY_SRC_PATH on which to base the new copy ID (else return NULL
   for that path).  CHILD must have a parent (it cannot be the root
   node).  Temporary allocations are taken from SCRATCH_POOL. */
static svn_error_t *
get_copy_inheritance(svn_fs_x__copy_id_inherit_t *inherit_p,
                     const char **copy_src_path,
                     svn_fs_t *fs,
                     svn_fs_x__dag_path_t *child,
                     apr_pool_t *scratch_pool)
{
  svn_fs_x__id_t child_copy_id, parent_copy_id;
  const char *id_path = NULL;
  svn_fs_root_t *copyroot_root;
  dag_node_t *copyroot_node;
  svn_revnum_t copyroot_rev;
  const char *copyroot_path;

  SVN_ERR_ASSERT(child && child->parent);

  /* Initialize some convenience variables. */
  child_copy_id = *svn_fs_x__dag_get_copy_id(child->node);
  parent_copy_id = *svn_fs_x__dag_get_copy_id(child->parent->node);

  /* By default, there is no copy source. */
  *copy_src_path = NULL;

  /* If this child is already mutable, we have nothing to do. */
  if (svn_fs_x__dag_check_mutable(child->node))
    {
      *inherit_p = svn_fs_x__copy_id_inherit_self;
      return SVN_NO_ERROR;
    }

  /* From this point on, we'll assume that the child will just take
     its copy ID from its parent. */
  *inherit_p = svn_fs_x__copy_id_inherit_parent;

  /* Special case: if the child's copy ID is '0', use the parent's
     copy ID. */
  if (svn_fs_x__id_is_root(&child_copy_id))
    return SVN_NO_ERROR;

  /* Compare the copy IDs of the child and its parent.  If they are
     the same, then the child is already on the same branch as the
     parent, and should use the same mutability copy ID that the
     parent will use. */
  if (svn_fs_x__id_eq(&child_copy_id, &parent_copy_id))
    return SVN_NO_ERROR;

  /* If the child is on the same branch that the parent is on, the
     child should just use the same copy ID that the parent would use.
     Else, the child needs to generate a new copy ID to use should it
     need to be made mutable.  We will claim that child is on the same
     branch as its parent if the child itself is not a branch point,
     or if it is a branch point that we are accessing via its original
     copy destination path. */
  svn_fs_x__dag_get_copyroot(&copyroot_rev, &copyroot_path, child->node);
  SVN_ERR(svn_fs_x__revision_root(&copyroot_root, fs, copyroot_rev, 
                                  scratch_pool));
  SVN_ERR(svn_fs_x__get_temp_dag_node(&copyroot_node, copyroot_root,
                                      copyroot_path, scratch_pool));

  if (!svn_fs_x__dag_related_node(copyroot_node, child->node))
    return SVN_NO_ERROR;

  /* Determine if we are looking at the child via its original path or
     as a subtree item of a copied tree. */
  id_path = svn_fs_x__dag_get_created_path(child->node);
  if (strcmp(id_path, parent_path_path(child, scratch_pool)) == 0)
    {
      *inherit_p = svn_fs_x__copy_id_inherit_self;
      return SVN_NO_ERROR;
    }

  /* We are pretty sure that the child node is an unedited nested
     branched node.  When it needs to be made mutable, it should claim
     a new copy ID. */
  *inherit_p = svn_fs_x__copy_id_inherit_new;
  *copy_src_path = id_path;
  return SVN_NO_ERROR;
}

/* Allocate a new svn_fs_x__dag_path_t node from RESULT_POOL, containing
   NODE, ENTRY and PARENT, all copied into RESULT_POOL as well.  */
static svn_fs_x__dag_path_t *
make_parent_path(dag_node_t *node,
                 const svn_stringbuf_t *entry,
                 svn_fs_x__dag_path_t *parent,
                 apr_pool_t *result_pool)
{
  svn_fs_x__dag_path_t *dag_path
    = apr_pcalloc(result_pool, sizeof(*dag_path));
  if (node)
    dag_path->node = svn_fs_x__dag_dup(node, result_pool);
  dag_path->entry = apr_pstrmemdup(result_pool, entry->data, entry->len);
  dag_path->parent = parent;
  dag_path->copy_inherit = svn_fs_x__copy_id_inherit_unknown;
  dag_path->copy_src_path = NULL;
  return dag_path;
}

svn_error_t *
svn_fs_x__get_dag_path(svn_fs_x__dag_path_t **dag_path_p,
                       svn_fs_root_t *root,
                       const char *fs_path,
                       int flags,
                       svn_boolean_t is_txn_path,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_fs_t *fs = root->fs;
  dag_node_t *here = NULL; /* The directory we're currently looking at.  */
  svn_fs_x__dag_path_t *dag_path; /* The path from HERE up to the root. */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  svn_fs_x__change_set_t change_set = svn_fs_x__root_change_set(root);
  const char *entry;
  svn_string_t path;
  svn_stringbuf_t *entry_buffer = svn_stringbuf_create_ensure(64,
                                                              scratch_pool);
  apr_size_t path_len;

  /* Normalize the FS_PATH to be compatible with our DAG walk utils. */
  normalize_path(&path, fs_path); /* "" */

  /* Make a DAG_PATH for the root node, using its own current copy id.  */
  SVN_ERR(get_root_node(&here, root, change_set, iterpool));
  dag_path = make_parent_path(here, entry_buffer, NULL, result_pool);
  dag_path->copy_inherit = svn_fs_x__copy_id_inherit_self;

  path_len = path.len;
  path.len = 0;

  /* Walk the path segment by segment.  Add to the DAG_PATH as we go. */
  for (entry = next_entry_name(&path, entry_buffer);
       entry;
       entry = next_entry_name(&path, entry_buffer))
    {
      svn_pool_clear(iterpool);

      /* If the current node is not a directory and we are just here to
       * check for the path's existence, then that's o.k.
       * Otherwise, non-dir nodes will cause an error in dag_step. */
      if (   (flags & svn_fs_x__dag_path_allow_null)
          && (svn_fs_x__dag_node_kind(dag_path->node) != svn_node_dir))
        {
          dag_path = NULL;
          break;
        }

      /* Find the sub-node. */
      SVN_ERR(dag_step(&here, root, dag_path->node, entry, &path, change_set,
                       TRUE, iterpool));

      /* "node not found" requires special handling.  */
      if (here == NULL)
        {
          /* If this was the last path component, and the caller
             said it was optional, then don't return an error;
             just put a NULL node pointer in the path. 
           */
          if ((flags & svn_fs_x__dag_path_last_optional)
              && (path_len == path.len))
            {
              dag_path = make_parent_path(NULL, entry_buffer, dag_path,
                                          result_pool);
              break;
            }
          else if (flags & svn_fs_x__dag_path_allow_null)
            {
              dag_path = NULL;
              break;
            }
          else
            {
              /* Build a better error message than svn_fs_x__dag_open
                 can provide, giving the root and full path name.  */
              return SVN_FS__NOT_FOUND(root, fs_path);
            }
        }

      /* Now, make a parent_path item for CHILD. */
      dag_path = make_parent_path(here, entry_buffer, dag_path, result_pool);
      if (is_txn_path)
        {
          SVN_ERR(get_copy_inheritance(&dag_path->copy_inherit,
                                       &dag_path->copy_src_path,
                                       fs, dag_path, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  *dag_path_p = dag_path;
  return SVN_NO_ERROR;
}

/* Set *NODE_P to a mutable root directory for ROOT, cloning if
   necessary, allocating in RESULT_POOL.  ROOT must be a transaction root.
   Use ERROR_PATH in error messages.  Use SCRATCH_POOL for temporaries.*/
static svn_error_t *
mutable_root_node(dag_node_t **node_p,
                  svn_fs_root_t *root,
                  const char *error_path,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  /* If it's not a transaction root, we can't change its contents.  */
  if (!root->is_txn_root)
    return SVN_FS__ERR_NOT_MUTABLE(root->fs, root->rev, error_path);

  /* It's a transaction root.
     Get the appropriate DAG root node and copy it into RESULT_POOL. */
  SVN_ERR(get_root_node(node_p, root, svn_fs_x__root_change_set(root),
                        scratch_pool));
  *node_p = svn_fs_x__dag_dup(*node_p, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__make_path_mutable(svn_fs_root_t *root,
                            svn_fs_x__dag_path_t *parent_path,
                            const char *error_path,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  dag_node_t *clone;
  svn_fs_x__txn_id_t txn_id = svn_fs_x__root_txn_id(root);

  /* Is the node mutable already?  */
  if (svn_fs_x__dag_check_mutable(parent_path->node))
    return SVN_NO_ERROR;

  /* Are we trying to clone the root, or somebody's child node?  */
  if (parent_path->parent)
    {
      svn_fs_x__id_t copy_id = { SVN_INVALID_REVNUM, 0 };
      svn_fs_x__id_t *copy_id_ptr = &copy_id;
      svn_fs_x__copy_id_inherit_t inherit = parent_path->copy_inherit;
      const char *clone_path, *copyroot_path;
      svn_revnum_t copyroot_rev;
      svn_boolean_t is_parent_copyroot = FALSE;
      svn_fs_root_t *copyroot_root;
      dag_node_t *copyroot_node;
      apr_pool_t *subpool;

      /* We're trying to clone somebody's child.  Make sure our parent
         is mutable.  */
      SVN_ERR(svn_fs_x__make_path_mutable(root, parent_path->parent,
                                          error_path, result_pool,
                                          scratch_pool));

      /* Allocate all temporaries in a sub-pool that we control locally.
         That way, we keep only the data of one level of recursion around
         at any time. */
      subpool = svn_pool_create(scratch_pool);
      switch (inherit)
        {
        case svn_fs_x__copy_id_inherit_parent:
          copy_id = *svn_fs_x__dag_get_copy_id(parent_path->parent->node);
          break;

        case svn_fs_x__copy_id_inherit_new:
          SVN_ERR(svn_fs_x__reserve_copy_id(&copy_id, root->fs, txn_id,
                                            subpool));
          break;

        case svn_fs_x__copy_id_inherit_self:
          copy_id_ptr = NULL;
          break;

        case svn_fs_x__copy_id_inherit_unknown:
        default:
          SVN_ERR_MALFUNCTION(); /* uh-oh -- somebody didn't calculate copy-ID
                      inheritance data. */
        }

      /* Determine what copyroot our new child node should use. */
      svn_fs_x__dag_get_copyroot(&copyroot_rev, &copyroot_path,
                                 parent_path->node);
      SVN_ERR(svn_fs_x__revision_root(&copyroot_root, root->fs,
                                      copyroot_rev, subpool));
      SVN_ERR(svn_fs_x__get_temp_dag_node(&copyroot_node, copyroot_root,
                                          copyroot_path, subpool));

      if (!svn_fs_x__dag_related_node(copyroot_node, parent_path->node))
        is_parent_copyroot = TRUE;

      /* Now make this node mutable.  */
      clone_path = parent_path_path(parent_path->parent, subpool);
      SVN_ERR(svn_fs_x__dag_clone_child(&clone,
                                        parent_path->parent->node,
                                        clone_path,
                                        parent_path->entry,
                                        copy_id_ptr, txn_id,
                                        is_parent_copyroot,
                                        result_pool,
                                        subpool));

      /* Update the path cache. */
      svn_fs_x__update_dag_cache(clone);
      svn_pool_destroy(subpool);
    }
  else
    {
      /* We're trying to clone the root directory.  */
      SVN_ERR(mutable_root_node(&clone, root, error_path, result_pool,
                                scratch_pool));
    }

  /* Update the PARENT_PATH link to refer to the clone.  */
  parent_path->node = clone;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__get_temp_dag_node(dag_node_t **node_p,
                            svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *scratch_pool)
{
  svn_string_t normalized;

  /* First we look for the DAG in our cache. */
  *node_p = dag_node_cache_get(root, normalize_path(&normalized, path));

  /* If it is not there, walk the DAG and fill the cache. */
  if (! *node_p)
    SVN_ERR(walk_dag_path(node_p, root, &normalized, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_x__get_dag_node(dag_node_t **dag_node_p,
                       svn_fs_root_t *root,
                       const char *path,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  dag_node_t *node = NULL;
  SVN_ERR(svn_fs_x__get_temp_dag_node(&node, root, path, scratch_pool));

  /* We want the returned node to live in POOL. */
  *dag_node_p = svn_fs_x__dag_dup(node, result_pool);

  return SVN_NO_ERROR;
}
