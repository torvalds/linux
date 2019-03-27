/* dump.c --- writing filesystem contents into a portable 'dumpfile' format.
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


#include <stdarg.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_hash.h"
#include "svn_iter.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_checksum.h"
#include "svn_props.h"
#include "svn_sorts.h"

#include "private/svn_repos_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_fs_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_utf_private.h"
#include "private/svn_cache.h"

#define ARE_VALID_COPY_ARGS(p,r) ((p) && SVN_IS_VALID_REVNUM(r))

/*----------------------------------------------------------------------*/


/* To be able to check whether a path exists in the current revision
   (as changes come in), we need to track the relevant tree changes.

   In particular, we remember deletions, additions and copies including
   their copy-from info.  Since the dump performs a pre-order tree walk,
   we only need to store the data for the stack of parent folders.

   The problem that we are trying to solve is that the dump receives
   transforming operations whose validity depends on previous operations
   in the same revision but cannot be checked against the final state
   as stored in the repository as that is the state *after* we applied
   the respective tree changes.

   Note that the tracker functions don't perform any sanity or validity
   checks.  Those higher-level tests have to be done in the calling code.
   However, there is no way to corrupt the data structure using the
   provided functions.
 */

/* Single entry in the path tracker.  Not all levels along the path
   hierarchy do need to have an instance of this struct but only those
   that got changed by a tree modification.

   Please note that the path info in this struct is stored in re-usable
   stringbuf objects such that we don't need to allocate more memory than
   the longest path we encounter.
 */
typedef struct path_tracker_entry_t
{
  /* path in the current tree */
  svn_stringbuf_t *path;

  /* copy-from path (must be empty if COPYFROM_REV is SVN_INVALID_REVNUM) */
  svn_stringbuf_t *copyfrom_path;

  /* copy-from revision (SVN_INVALID_REVNUM for additions / replacements
     that don't copy history, i.e. with no sub-tree) */
  svn_revnum_t copyfrom_rev;

  /* if FALSE, PATH has been deleted */
  svn_boolean_t exists;
} path_tracker_entry_t;

/* Tracks all tree modifications above the current path.
 */
typedef struct path_tracker_t
{
  /* Container for all relevant tree changes in depth order.
     May contain more entries than DEPTH to allow for reusing memory.
     Only entries 0 .. DEPTH-1 are valid.
   */
  apr_array_header_t *stack;

  /* Number of relevant entries in STACK.  May be 0 */
  int depth;

  /* Revision that we current track.  If DEPTH is 0, paths are exist in
     REVISION exactly when they exist in REVISION-1.  This applies only
     to the current state of our tree walk.
   */
  svn_revnum_t revision;

  /* Allocate container entries here. */
  apr_pool_t *pool;
} path_tracker_t;

/* Return a new path tracker object for REVISION, allocated in POOL.
 */
static path_tracker_t *
tracker_create(svn_revnum_t revision,
               apr_pool_t *pool)
{
  path_tracker_t *result = apr_pcalloc(pool, sizeof(*result));
  result->stack = apr_array_make(pool, 16, sizeof(path_tracker_entry_t));
  result->revision = revision;
  result->pool = pool;

  return result;
}

/* Remove all entries from TRACKER that are not relevant to PATH anymore.
 * If ALLOW_EXACT_MATCH is FALSE, keep only entries that pertain to
 * parent folders but not to PATH itself.
 *
 * This internal function implicitly updates the tracker state during the
 * tree by removing "past" entries.  Other functions will add entries when
 * we encounter a new tree change.
 */
static void
tracker_trim(path_tracker_t *tracker,
             const char *path,
             svn_boolean_t allow_exact_match)
{
  /* remove everything that is unrelated to PATH.
     Note that TRACKER->STACK is depth-ordered,
     i.e. stack[N] is a (maybe indirect) parent of stack[N+1]
     for N+1 < DEPTH.
   */
  for (; tracker->depth; --tracker->depth)
    {
      path_tracker_entry_t *parent = &APR_ARRAY_IDX(tracker->stack,
                                                    tracker->depth - 1,
                                                    path_tracker_entry_t);
      const char *rel_path
        = svn_dirent_skip_ancestor(parent->path->data, path);

      /* always keep parents.  Keep exact matches when allowed. */
      if (rel_path && (allow_exact_match || *rel_path != '\0'))
        break;
    }
}

/* Using TRACKER, check what path at what revision in the repository must
   be checked to decide that whether PATH exists.  Return the info in
   *ORIG_PATH and *ORIG_REV, respectively.

   If the path is known to not exist, *ORIG_PATH will be NULL and *ORIG_REV
   will be SVN_INVALID_REVNUM.  If *ORIG_REV is SVN_INVALID_REVNUM, PATH
   has just been added in the revision currently being tracked.

   Use POOL for allocations.  Note that *ORIG_PATH may be allocated in POOL,
   a reference to internal data with the same lifetime as TRACKER or just
   PATH.
 */
static void
tracker_lookup(const char **orig_path,
               svn_revnum_t *orig_rev,
               path_tracker_t *tracker,
               const char *path,
               apr_pool_t *pool)
{
  tracker_trim(tracker, path, TRUE);
  if (tracker->depth == 0)
    {
      /* no tree changes -> paths are the same as in the previous rev. */
      *orig_path = path;
      *orig_rev = tracker->revision - 1;
    }
  else
    {
      path_tracker_entry_t *parent = &APR_ARRAY_IDX(tracker->stack,
                                                    tracker->depth - 1,
                                                    path_tracker_entry_t);
      if (parent->exists)
        {
          const char *rel_path
            = svn_dirent_skip_ancestor(parent->path->data, path);

          if (parent->copyfrom_rev != SVN_INVALID_REVNUM)
            {
              /* parent is a copy with history. Translate path. */
              *orig_path = svn_dirent_join(parent->copyfrom_path->data,
                                           rel_path, pool);
              *orig_rev = parent->copyfrom_rev;
            }
          else if (*rel_path == '\0')
            {
              /* added in this revision with no history */
              *orig_path = path;
              *orig_rev = tracker->revision;
            }
          else
            {
              /* parent got added but not this path */
              *orig_path = NULL;
              *orig_rev = SVN_INVALID_REVNUM;
            }
        }
      else
        {
          /* (maybe parent) path has been deleted */
          *orig_path = NULL;
          *orig_rev = SVN_INVALID_REVNUM;
        }
    }
}

/* Return a reference to the stack entry in TRACKER for PATH.  If no
   suitable entry exists, add one.  Implicitly updates the tracked tree
   location.

   Only the PATH member of the result is being updated.  All other members
   will have undefined values.
 */
static path_tracker_entry_t *
tracker_add_entry(path_tracker_t *tracker,
                  const char *path)
{
  path_tracker_entry_t *entry;
  tracker_trim(tracker, path, FALSE);

  if (tracker->depth == tracker->stack->nelts)
    {
      entry = apr_array_push(tracker->stack);
      entry->path = svn_stringbuf_create_empty(tracker->pool);
      entry->copyfrom_path = svn_stringbuf_create_empty(tracker->pool);
    }
  else
    {
      entry = &APR_ARRAY_IDX(tracker->stack, tracker->depth,
                             path_tracker_entry_t);
    }

  svn_stringbuf_set(entry->path, path);
  ++tracker->depth;

  return entry;
}

/* Update the TRACKER with a copy from COPYFROM_PATH@COPYFROM_REV to
   PATH in the tracked revision.
 */
static void
tracker_path_copy(path_tracker_t *tracker,
                  const char *path,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_rev)
{
  path_tracker_entry_t *entry = tracker_add_entry(tracker, path);

  svn_stringbuf_set(entry->copyfrom_path, copyfrom_path);
  entry->copyfrom_rev = copyfrom_rev;
  entry->exists = TRUE;
}

/* Update the TRACKER with a plain addition of PATH (without history).
 */
static void
tracker_path_add(path_tracker_t *tracker,
                 const char *path)
{
  path_tracker_entry_t *entry = tracker_add_entry(tracker, path);

  svn_stringbuf_setempty(entry->copyfrom_path);
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->exists = TRUE;
}

/* Update the TRACKER with a replacement of PATH with a plain addition
   (without history).
 */
static void
tracker_path_replace(path_tracker_t *tracker,
                     const char *path)
{
  /* this will implicitly purge all previous sub-tree info from STACK.
     Thus, no need to tack the deletion explicitly. */
  tracker_path_add(tracker, path);
}

/* Update the TRACKER with a deletion of PATH.
 */
static void
tracker_path_delete(path_tracker_t *tracker,
                    const char *path)
{
  path_tracker_entry_t *entry = tracker_add_entry(tracker, path);

  svn_stringbuf_setempty(entry->copyfrom_path);
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->exists = FALSE;
}


/* Compute the delta between OLDROOT/OLDPATH and NEWROOT/NEWPATH and
   store it into a new temporary file *TEMPFILE.  OLDROOT may be NULL,
   in which case the delta will be computed against an empty file, as
   per the svn_fs_get_file_delta_stream docstring.  Record the length
   of the temporary file in *LEN, and rewind the file before
   returning. */
static svn_error_t *
store_delta(apr_file_t **tempfile, svn_filesize_t *len,
            svn_fs_root_t *oldroot, const char *oldpath,
            svn_fs_root_t *newroot, const char *newpath, apr_pool_t *pool)
{
  svn_stream_t *temp_stream;
  apr_off_t offset;
  svn_txdelta_stream_t *delta_stream;
  svn_txdelta_window_handler_t wh;
  void *whb;

  /* Create a temporary file and open a stream to it. Note that we need
     the file handle in order to rewind it. */
  SVN_ERR(svn_io_open_unique_file3(tempfile, NULL, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   pool, pool));
  temp_stream = svn_stream_from_aprfile2(*tempfile, TRUE, pool);

  /* Compute the delta and send it to the temporary file. */
  SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream, oldroot, oldpath,
                                       newroot, newpath, pool));
  svn_txdelta_to_svndiff3(&wh, &whb, temp_stream, 0,
                          SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, pool);
  SVN_ERR(svn_txdelta_send_txstream(delta_stream, wh, whb, pool));

  /* Get the length of the temporary file and rewind it. */
  SVN_ERR(svn_io_file_get_offset(&offset, *tempfile, pool));
  *len = offset;
  offset = 0;
  return svn_io_file_seek(*tempfile, APR_SET, &offset, pool);
}


/* Send a notification of type #svn_repos_notify_warning, subtype WARNING,
   with message WARNING_FMT formatted with the remaining variable arguments.
   Send it by calling NOTIFY_FUNC (if not null) with NOTIFY_BATON.
 */
__attribute__((format(printf, 5, 6)))
static void
notify_warning(apr_pool_t *scratch_pool,
               svn_repos_notify_func_t notify_func,
               void *notify_baton,
               svn_repos_notify_warning_t warning,
               const char *warning_fmt,
               ...)
{
  va_list va;
  svn_repos_notify_t *notify;

  if (notify_func == NULL)
    return;

  notify = svn_repos_notify_create(svn_repos_notify_warning, scratch_pool);
  notify->warning = warning;
  va_start(va, warning_fmt);
  notify->warning_str = apr_pvsprintf(scratch_pool, warning_fmt, va);
  va_end(va);

  notify_func(notify_baton, notify, scratch_pool);
}


/*----------------------------------------------------------------------*/

/* Write to STREAM the header in HEADERS named KEY, if present.
 */
static svn_error_t *
write_header(svn_stream_t *stream,
             apr_hash_t *headers,
             const char *key,
             apr_pool_t *scratch_pool)
{
  const char *val = svn_hash_gets(headers, key);

  if (val)
    {
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "%s: %s\n", key, val));
    }
  return SVN_NO_ERROR;
}

/* Write headers, in arbitrary order.
 * ### TODO: use a stable order
 * ### Modifies HEADERS.
 */
static svn_error_t *
write_revision_headers(svn_stream_t *stream,
                       apr_hash_t *headers,
                       apr_pool_t *scratch_pool)
{
  const char **h;
  apr_hash_index_t *hi;

  static const char *revision_headers_order[] =
  {
    SVN_REPOS_DUMPFILE_REVISION_NUMBER,  /* must be first */
    NULL
  };

  /* Write some headers in a given order */
  for (h = revision_headers_order; *h; h++)
    {
      SVN_ERR(write_header(stream, headers, *h, scratch_pool));
      svn_hash_sets(headers, *h, NULL);
    }

  /* Write any and all remaining headers except Content-length.
   * ### TODO: use a stable order
   */
  for (hi = apr_hash_first(scratch_pool, headers); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);

      if (strcmp(key, SVN_REPOS_DUMPFILE_CONTENT_LENGTH) != 0)
        SVN_ERR(write_header(stream, headers, key, scratch_pool));
    }

  /* Content-length must be last */
  SVN_ERR(write_header(stream, headers, SVN_REPOS_DUMPFILE_CONTENT_LENGTH,
                       scratch_pool));

  return SVN_NO_ERROR;
}

/* A header entry: the element type of the apr_array_header_t which is
 * the real type of svn_repos__dumpfile_headers_t.
 */
typedef struct svn_repos__dumpfile_header_entry_t {
  const char *key, *val;
} svn_repos__dumpfile_header_entry_t;

svn_repos__dumpfile_headers_t *
svn_repos__dumpfile_headers_create(apr_pool_t *pool)
{
  svn_repos__dumpfile_headers_t *headers
    = apr_array_make(pool, 5, sizeof(svn_repos__dumpfile_header_entry_t));

  return headers;
}

void
svn_repos__dumpfile_header_push(svn_repos__dumpfile_headers_t *headers,
                                const char *key,
                                const char *val)
{
  svn_repos__dumpfile_header_entry_t *h
    = &APR_ARRAY_PUSH(headers, svn_repos__dumpfile_header_entry_t);

  h->key = apr_pstrdup(headers->pool, key);
  h->val = apr_pstrdup(headers->pool, val);
}

void
svn_repos__dumpfile_header_pushf(svn_repos__dumpfile_headers_t *headers,
                                 const char *key,
                                 const char *val_fmt,
                                 ...)
{
  va_list ap;
  svn_repos__dumpfile_header_entry_t *h
    = &APR_ARRAY_PUSH(headers, svn_repos__dumpfile_header_entry_t);

  h->key = apr_pstrdup(headers->pool, key);
  va_start(ap, val_fmt);
  h->val = apr_pvsprintf(headers->pool, val_fmt, ap);
  va_end(ap);
}

svn_error_t *
svn_repos__dump_headers(svn_stream_t *stream,
                        svn_repos__dumpfile_headers_t *headers,
                        apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < headers->nelts; i++)
    {
      svn_repos__dumpfile_header_entry_t *h
        = &APR_ARRAY_IDX(headers, i, svn_repos__dumpfile_header_entry_t);

      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "%s: %s\n", h->key, h->val));
    }

  /* End of headers */
  SVN_ERR(svn_stream_puts(stream, "\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__dump_revision_record(svn_stream_t *dump_stream,
                                svn_revnum_t revision,
                                apr_hash_t *extra_headers,
                                apr_hash_t *revprops,
                                svn_boolean_t props_section_always,
                                apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *propstring = NULL;
  apr_hash_t *headers;

  if (extra_headers)
    headers = apr_hash_copy(scratch_pool, extra_headers);
  else
    headers = apr_hash_make(scratch_pool);

  /* ### someday write a revision-content-checksum */

  svn_hash_sets(headers, SVN_REPOS_DUMPFILE_REVISION_NUMBER,
                apr_psprintf(scratch_pool, "%ld", revision));

  if (apr_hash_count(revprops) || props_section_always)
    {
      svn_stream_t *propstream;

      propstring = svn_stringbuf_create_empty(scratch_pool);
      propstream = svn_stream_from_stringbuf(propstring, scratch_pool);
      SVN_ERR(svn_hash_write2(revprops, propstream, "PROPS-END", scratch_pool));
      SVN_ERR(svn_stream_close(propstream));

      svn_hash_sets(headers, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH,
                    apr_psprintf(scratch_pool,
                                 "%" APR_SIZE_T_FMT, propstring->len));
    }

  if (propstring)
    {
      /* Write out a regular Content-length header for the benefit of
         non-Subversion RFC-822 parsers. */
      svn_hash_sets(headers, SVN_REPOS_DUMPFILE_CONTENT_LENGTH,
                    apr_psprintf(scratch_pool,
                                 "%" APR_SIZE_T_FMT, propstring->len));
    }

  SVN_ERR(write_revision_headers(dump_stream, headers, scratch_pool));

  /* End of headers */
  SVN_ERR(svn_stream_puts(dump_stream, "\n"));

  /* Property data. */
  if (propstring)
    {
      SVN_ERR(svn_stream_write(dump_stream, propstring->data, &propstring->len));
    }

  /* put an end to revision */
  SVN_ERR(svn_stream_puts(dump_stream, "\n"));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__dump_node_record(svn_stream_t *dump_stream,
                            svn_repos__dumpfile_headers_t *headers,
                            svn_stringbuf_t *props_str,
                            svn_boolean_t has_text,
                            svn_filesize_t text_content_length,
                            svn_boolean_t content_length_always,
                            apr_pool_t *scratch_pool)
{
  svn_filesize_t content_length = 0;

  /* add content-length headers */
  if (props_str)
    {
      svn_repos__dumpfile_header_pushf(
        headers, SVN_REPOS_DUMPFILE_PROP_CONTENT_LENGTH,
        "%" APR_SIZE_T_FMT, props_str->len);
      content_length += props_str->len;
    }
  if (has_text)
    {
      svn_repos__dumpfile_header_pushf(
        headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH,
        "%" SVN_FILESIZE_T_FMT, text_content_length);
      content_length += text_content_length;
    }
  if (content_length_always || props_str || has_text)
    {
      svn_repos__dumpfile_header_pushf(
        headers, SVN_REPOS_DUMPFILE_CONTENT_LENGTH,
        "%" SVN_FILESIZE_T_FMT, content_length);
    }

  /* write the headers */
  SVN_ERR(svn_repos__dump_headers(dump_stream, headers, scratch_pool));

  /* write the props */
  if (props_str)
    {
      SVN_ERR(svn_stream_write(dump_stream, props_str->data, &props_str->len));
    }
  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------*/

/** An editor which dumps node-data in 'dumpfile format' to a file. **/

/* Look, mom!  No file batons! */

struct edit_baton
{
  /* The relpath which implicitly prepends all full paths coming into
     this editor.  This will almost always be "".  */
  const char *path;

  /* The stream to dump to. */
  svn_stream_t *stream;

  /* Send feedback here, if non-NULL */
  svn_repos_notify_func_t notify_func;
  void *notify_baton;

  /* The fs revision root, so we can read the contents of paths. */
  svn_fs_root_t *fs_root;
  svn_revnum_t current_rev;

  /* The fs, so we can grab historic information if needed. */
  svn_fs_t *fs;

  /* True if dumped nodes should output deltas instead of full text. */
  svn_boolean_t use_deltas;

  /* True if this "dump" is in fact a verify. */
  svn_boolean_t verify;

  /* True if checking UCS normalization during a verify. */
  svn_boolean_t check_normalization;

  /* The first revision dumped in this dumpstream. */
  svn_revnum_t oldest_dumped_rev;

  /* If not NULL, set to true if any references to revisions older than
     OLDEST_DUMPED_REV were found in the dumpstream. */
  svn_boolean_t *found_old_reference;

  /* If not NULL, set to true if any mergeinfo was dumped which contains
     revisions older than OLDEST_DUMPED_REV. */
  svn_boolean_t *found_old_mergeinfo;

  /* Structure allows us to verify the paths currently being dumped.
     If NULL, validity checks are being skipped. */
  path_tracker_t *path_tracker;
};

struct dir_baton
{
  struct edit_baton *edit_baton;

  /* has this directory been written to the output stream? */
  svn_boolean_t written_out;

  /* the repository relpath associated with this directory */
  const char *path;

  /* The comparison repository relpath and revision of this directory.
     If both of these are valid, use them as a source against which to
     compare the directory instead of the default comparison source of
     PATH in the previous revision. */
  const char *cmp_path;
  svn_revnum_t cmp_rev;

  /* hash of paths that need to be deleted, though some -might- be
     replaced.  maps const char * paths to this dir_baton.  (they're
     full paths, because that's what the editor driver gives us.  but
     really, they're all within this directory.) */
  apr_hash_t *deleted_entries;

  /* A flag indicating that new entries have been added to this
     directory in this revision. Used to optimize detection of UCS
     representation collisions; we will only check for that in
     revisions where new names appear in the directory. */
  svn_boolean_t check_name_collision;

  /* pool to be used for deleting the hash items */
  apr_pool_t *pool;
};


/* Make a directory baton to represent the directory was path
   (relative to EDIT_BATON's path) is PATH.

   CMP_PATH/CMP_REV are the path/revision against which this directory
   should be compared for changes.  If either is omitted (NULL for the
   path, SVN_INVALID_REVNUM for the rev), just compare this directory
   PATH against itself in the previous revision.

   PB is the directory baton of this directory's parent,
   or NULL if this is the top-level directory of the edit.

   Perform all allocations in POOL.  */
static struct dir_baton *
make_dir_baton(const char *path,
               const char *cmp_path,
               svn_revnum_t cmp_rev,
               void *edit_baton,
               struct dir_baton *pb,
               apr_pool_t *pool)
{
  struct edit_baton *eb = edit_baton;
  struct dir_baton *new_db = apr_pcalloc(pool, sizeof(*new_db));
  const char *full_path;

  /* A path relative to nothing?  I don't think so. */
  SVN_ERR_ASSERT_NO_RETURN(!path || pb);

  /* Construct the full path of this node. */
  if (pb)
    full_path = svn_relpath_join(eb->path, path, pool);
  else
    full_path = apr_pstrdup(pool, eb->path);

  /* Remove leading slashes from copyfrom paths. */
  if (cmp_path)
    cmp_path = svn_relpath_canonicalize(cmp_path, pool);

  new_db->edit_baton = eb;
  new_db->path = full_path;
  new_db->cmp_path = cmp_path;
  new_db->cmp_rev = cmp_rev;
  new_db->written_out = FALSE;
  new_db->deleted_entries = apr_hash_make(pool);
  new_db->check_name_collision = FALSE;
  new_db->pool = pool;

  return new_db;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool);

/* Return an error when PATH in REVISION does not exist or is of a
   different kind than EXPECTED_KIND.  If the latter is svn_node_unknown,
   skip that check.  Use EB for context information.  If REVISION is the
   current revision, use EB's path tracker to follow renames, deletions,
   etc.

   Use SCRATCH_POOL for temporary allocations.
   No-op if EB's path tracker has not been initialized.
 */
static svn_error_t *
node_must_exist(struct edit_baton *eb,
                const char *path,
                svn_revnum_t revision,
                svn_node_kind_t expected_kind,
                apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind = svn_node_none;

  /* in case the caller is trying something stupid ... */
  if (eb->path_tracker == NULL)
    return SVN_NO_ERROR;

  /* paths pertaining to the revision currently being processed must
     be translated / checked using our path tracker. */
  if (revision == eb->path_tracker->revision)
    tracker_lookup(&path, &revision, eb->path_tracker, path, scratch_pool);

  /* determine the node type (default: no such node) */
  if (path)
    SVN_ERR(fetch_kind_func(&kind, eb, path, revision, scratch_pool));

  /* check results */
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("Path '%s' not found in r%ld."),
                             path, revision);

  if (expected_kind != kind && expected_kind != svn_node_unknown)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Unexpected node kind %d for '%s' at r%ld. "
                               "Expected kind was %d."),
                             kind, path, revision, expected_kind);

  return SVN_NO_ERROR;
}

/* Return an error when PATH exists in REVISION.  Use EB for context
   information.  If REVISION is the current revision, use EB's path
   tracker to follow renames, deletions, etc.

   Use SCRATCH_POOL for temporary allocations.
   No-op if EB's path tracker has not been initialized.
 */
static svn_error_t *
node_must_not_exist(struct edit_baton *eb,
                    const char *path,
                    svn_revnum_t revision,
                    apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind = svn_node_none;

  /* in case the caller is trying something stupid ... */
  if (eb->path_tracker == NULL)
    return SVN_NO_ERROR;

  /* paths pertaining to the revision currently being processed must
     be translated / checked using our path tracker. */
  if (revision == eb->path_tracker->revision)
    tracker_lookup(&path, &revision, eb->path_tracker, path, scratch_pool);

  /* determine the node type (default: no such node) */
  if (path)
    SVN_ERR(fetch_kind_func(&kind, eb, path, revision, scratch_pool));

  /* check results */
  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                             _("Path '%s' exists in r%ld."),
                             path, revision);

  return SVN_NO_ERROR;
}

/* If the mergeinfo in MERGEINFO_STR refers to any revisions older than
 * OLDEST_DUMPED_REV, issue a warning and set *FOUND_OLD_MERGEINFO to TRUE,
 * otherwise leave *FOUND_OLD_MERGEINFO unchanged.
 */
static svn_error_t *
verify_mergeinfo_revisions(svn_boolean_t *found_old_mergeinfo,
                           const char *mergeinfo_str,
                           svn_revnum_t oldest_dumped_rev,
                           svn_repos_notify_func_t notify_func,
                           void *notify_baton,
                           apr_pool_t *pool)
{
  svn_mergeinfo_t mergeinfo, old_mergeinfo;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, mergeinfo_str, pool));
  SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
            &old_mergeinfo, mergeinfo,
            oldest_dumped_rev - 1, 0,
            TRUE, pool, pool));

  if (apr_hash_count(old_mergeinfo))
    {
      notify_warning(pool, notify_func, notify_baton,
                     svn_repos_notify_warning_found_old_mergeinfo,
                     _("Mergeinfo referencing revision(s) prior "
                       "to the oldest dumped revision (r%ld). "
                       "Loading this dump may result in invalid "
                       "mergeinfo."),
                     oldest_dumped_rev);

      if (found_old_mergeinfo)
        *found_old_mergeinfo = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Unique string pointers used by verify_mergeinfo_normalization()
   and check_name_collision() */
static const char normalized_unique[] = "normalized_unique";
static const char normalized_collision[] = "normalized_collision";


/* Baton for extract_mergeinfo_paths */
struct extract_mergeinfo_paths_baton
{
  apr_hash_t *result;
  svn_boolean_t normalize;
  svn_membuf_t buffer;
};

/* Hash iterator that uniquifies all keys into a single hash table,
   optionally normalizing them first. */
static svn_error_t *
extract_mergeinfo_paths(void *baton, const void *key, apr_ssize_t klen,
                         void *val, apr_pool_t *iterpool)
{
  struct extract_mergeinfo_paths_baton *const xb = baton;
  if (xb->normalize)
    {
      const char *normkey;
      SVN_ERR(svn_utf__normalize(&normkey, key, klen, &xb->buffer));
      svn_hash_sets(xb->result,
                    apr_pstrdup(xb->buffer.pool, normkey),
                    normalized_unique);
    }
  else
    apr_hash_set(xb->result,
                 apr_pmemdup(xb->buffer.pool, key, klen + 1), klen,
                 normalized_unique);
  return SVN_NO_ERROR;
}

/* Baton for filter_mergeinfo_paths */
struct filter_mergeinfo_paths_baton
{
  apr_hash_t *paths;
};

/* Compare two sets of denormalized paths from mergeinfo entries,
   removing duplicates. */
static svn_error_t *
filter_mergeinfo_paths(void *baton, const void *key, apr_ssize_t klen,
                       void *val, apr_pool_t *iterpool)
{
  struct filter_mergeinfo_paths_baton *const fb = baton;

  if (apr_hash_get(fb->paths, key, klen))
    apr_hash_set(fb->paths, key, klen, NULL);

  return SVN_NO_ERROR;
}

/* Baton used by the check_mergeinfo_normalization hash iterator. */
struct verify_mergeinfo_normalization_baton
{
  const char* path;
  apr_hash_t *normalized_paths;
  svn_membuf_t buffer;
  svn_repos_notify_func_t notify_func;
  void *notify_baton;
};

/* Hash iterator that verifies normalization and collision of paths in
   an svn:mergeinfo property. */
static svn_error_t *
verify_mergeinfo_normalization(void *baton, const void *key, apr_ssize_t klen,
                               void *val, apr_pool_t *iterpool)
{
  struct verify_mergeinfo_normalization_baton *const vb = baton;

  const char *const path = key;
  const char *normpath;
  const char *found;

  SVN_ERR(svn_utf__normalize(&normpath, path, klen, &vb->buffer));
  found = svn_hash_gets(vb->normalized_paths, normpath);
  if (!found)
      svn_hash_sets(vb->normalized_paths,
                    apr_pstrdup(vb->buffer.pool, normpath),
                    normalized_unique);
  else if (found == normalized_collision)
    /* Skip already reported collision */;
  else
    {
      /* Report path collision in mergeinfo */
      svn_hash_sets(vb->normalized_paths,
                    apr_pstrdup(vb->buffer.pool, normpath),
                    normalized_collision);

      notify_warning(iterpool, vb->notify_func, vb->notify_baton,
                     svn_repos_notify_warning_mergeinfo_collision,
                     _("Duplicate representation of path '%s'"
                       " in %s property of '%s'"),
                     normpath, SVN_PROP_MERGEINFO, vb->path);
    }
  return SVN_NO_ERROR;
}

/* Check UCS normalization of mergeinfo for PATH. NEW_MERGEINFO is the
   svn:mergeinfo property value being set; OLD_MERGEINFO is the
   previous property value, which may be NULL. Only the paths that
   were added in are checked, including collision checks. This
   minimizes the number of notifications we generate for a given
   mergeinfo property. */
static svn_error_t *
check_mergeinfo_normalization(const char *path,
                              const char *new_mergeinfo,
                              const char *old_mergeinfo,
                              svn_repos_notify_func_t notify_func,
                              void *notify_baton,
                              apr_pool_t *pool)
{
  svn_mergeinfo_t mergeinfo;
  apr_hash_t *normalized_paths;
  apr_hash_t *added_paths;
  struct extract_mergeinfo_paths_baton extract_baton;
  struct verify_mergeinfo_normalization_baton verify_baton;

  SVN_ERR(svn_mergeinfo_parse(&mergeinfo, new_mergeinfo, pool));

  extract_baton.result = apr_hash_make(pool);
  extract_baton.normalize = FALSE;
  svn_membuf__create(&extract_baton.buffer, 0, pool);
  SVN_ERR(svn_iter_apr_hash(NULL, mergeinfo,
                            extract_mergeinfo_paths,
                            &extract_baton, pool));
  added_paths = extract_baton.result;

  if (old_mergeinfo)
    {
      struct filter_mergeinfo_paths_baton filter_baton;
      svn_mergeinfo_t oldinfo;

      extract_baton.result = apr_hash_make(pool);
      extract_baton.normalize = TRUE;
      SVN_ERR(svn_mergeinfo_parse(&oldinfo, old_mergeinfo, pool));
      SVN_ERR(svn_iter_apr_hash(NULL, oldinfo,
                                extract_mergeinfo_paths,
                                &extract_baton, pool));
      normalized_paths = extract_baton.result;

      filter_baton.paths = added_paths;
      SVN_ERR(svn_iter_apr_hash(NULL, oldinfo,
                                filter_mergeinfo_paths,
                                &filter_baton, pool));
    }
  else
      normalized_paths = apr_hash_make(pool);

  verify_baton.path = path;
  verify_baton.normalized_paths = normalized_paths;
  verify_baton.buffer = extract_baton.buffer;
  verify_baton.notify_func = notify_func;
  verify_baton.notify_baton = notify_baton;
  SVN_ERR(svn_iter_apr_hash(NULL, added_paths,
                            verify_mergeinfo_normalization,
                            &verify_baton, pool));

  return SVN_NO_ERROR;
}


/* A special case of dump_node(), for a delete record.
 *
 * The only thing special about this version is it only writes one blank
 * line, not two, after the headers. Why? Historical precedent for the
 * case where a delete record is used as part of a (delete + add-with-history)
 * in implementing a replacement.
 *
 * Also it doesn't do a path-tracker check.
 */
static svn_error_t *
dump_node_delete(svn_stream_t *stream,
                 const char *node_relpath,
                 apr_pool_t *pool)
{
  svn_repos__dumpfile_headers_t *headers
    = svn_repos__dumpfile_headers_create(pool);

  /* Node-path: ... */
  svn_repos__dumpfile_header_push(
    headers, SVN_REPOS_DUMPFILE_NODE_PATH, node_relpath);

  /* Node-action: delete */
  svn_repos__dumpfile_header_push(
    headers, SVN_REPOS_DUMPFILE_NODE_ACTION, "delete");

  SVN_ERR(svn_repos__dump_headers(stream, headers, pool));
  return SVN_NO_ERROR;
}

/* This helper is the main "meat" of the editor -- it does all the
   work of writing a node record.

   Write out a node record for PATH of type KIND under EB->FS_ROOT.
   ACTION describes what is happening to the node (see enum svn_node_action).
   Write record to writable EB->STREAM.

   If the node was itself copied, IS_COPY is TRUE and the
   path/revision of the copy source are in CMP_PATH/CMP_REV.  If
   IS_COPY is FALSE, yet CMP_PATH/CMP_REV are valid, this node is part
   of a copied subtree.
  */
static svn_error_t *
dump_node(struct edit_baton *eb,
          const char *path,
          svn_node_kind_t kind,
          enum svn_node_action action,
          svn_boolean_t is_copy,
          const char *cmp_path,
          svn_revnum_t cmp_rev,
          apr_pool_t *pool)
{
  svn_stringbuf_t *propstring;
  apr_size_t len;
  svn_boolean_t must_dump_text = FALSE, must_dump_props = FALSE;
  const char *compare_path = path;
  svn_revnum_t compare_rev = eb->current_rev - 1;
  svn_fs_root_t *compare_root = NULL;
  apr_file_t *delta_file = NULL;
  svn_repos__dumpfile_headers_t *headers
    = svn_repos__dumpfile_headers_create(pool);
  svn_filesize_t textlen;

  /* Maybe validate the path. */
  if (eb->verify || eb->notify_func)
    {
      svn_error_t *err = svn_fs__path_valid(path, pool);

      if (err)
        {
          if (eb->notify_func)
            {
              char errbuf[512]; /* ### svn_strerror() magic number  */

              notify_warning(pool, eb->notify_func, eb->notify_baton,
                             svn_repos_notify_warning_invalid_fspath,
                             _("E%06d: While validating fspath '%s': %s"),
                             err->apr_err, path,
                             svn_err_best_message(err, errbuf, sizeof(errbuf)));
            }

          /* Return the error in addition to notifying about it. */
          if (eb->verify)
            return svn_error_trace(err);
          else
            svn_error_clear(err);
        }
    }

  /* Write out metadata headers for this file node. */
  svn_repos__dumpfile_header_push(
    headers, SVN_REPOS_DUMPFILE_NODE_PATH, path);
  if (kind == svn_node_file)
    svn_repos__dumpfile_header_push(
      headers, SVN_REPOS_DUMPFILE_NODE_KIND, "file");
  else if (kind == svn_node_dir)
    svn_repos__dumpfile_header_push(
      headers, SVN_REPOS_DUMPFILE_NODE_KIND, "dir");

  /* Remove leading slashes from copyfrom paths. */
  if (cmp_path)
    cmp_path = svn_relpath_canonicalize(cmp_path, pool);

  /* Validate the comparison path/rev. */
  if (ARE_VALID_COPY_ARGS(cmp_path, cmp_rev))
    {
      compare_path = cmp_path;
      compare_rev = cmp_rev;
    }

  switch (action)
    {
    case svn_node_action_change:
      if (eb->path_tracker)
        SVN_ERR_W(node_must_exist(eb, path, eb->current_rev, kind, pool),
                  apr_psprintf(pool, _("Change invalid path '%s' in r%ld"),
                               path, eb->current_rev));

      svn_repos__dumpfile_header_push(
        headers, SVN_REPOS_DUMPFILE_NODE_ACTION, "change");

      /* either the text or props changed, or possibly both. */
      SVN_ERR(svn_fs_revision_root(&compare_root,
                                   svn_fs_root_fs(eb->fs_root),
                                   compare_rev, pool));

      SVN_ERR(svn_fs_props_changed(&must_dump_props,
                                   compare_root, compare_path,
                                   eb->fs_root, path, pool));
      if (kind == svn_node_file)
        SVN_ERR(svn_fs_contents_changed(&must_dump_text,
                                        compare_root, compare_path,
                                        eb->fs_root, path, pool));
      break;

    case svn_node_action_delete:
      if (eb->path_tracker)
        {
          SVN_ERR_W(node_must_exist(eb, path, eb->current_rev, kind, pool),
                    apr_psprintf(pool, _("Deleting invalid path '%s' in r%ld"),
                                 path, eb->current_rev));
          tracker_path_delete(eb->path_tracker, path);
        }

      svn_repos__dumpfile_header_push(
        headers, SVN_REPOS_DUMPFILE_NODE_ACTION, "delete");

      /* we can leave this routine quietly now, don't need to dump
         any content. */
      must_dump_text = FALSE;
      must_dump_props = FALSE;
      break;

    case svn_node_action_replace:
      if (eb->path_tracker)
        SVN_ERR_W(node_must_exist(eb, path, eb->current_rev,
                                  svn_node_unknown, pool),
                  apr_psprintf(pool,
                               _("Replacing non-existent path '%s' in r%ld"),
                               path, eb->current_rev));

      if (! is_copy)
        {
          if (eb->path_tracker)
            tracker_path_replace(eb->path_tracker, path);

          /* a simple delete+add, implied by a single 'replace' action. */
          svn_repos__dumpfile_header_push(
            headers, SVN_REPOS_DUMPFILE_NODE_ACTION, "replace");

          /* definitely need to dump all content for a replace. */
          if (kind == svn_node_file)
            must_dump_text = TRUE;
          must_dump_props = TRUE;
          break;
        }
      else
        {
          /* more complex:  delete original, then add-with-history.  */
          /* ### Why not write a 'replace' record? Don't know. */

          if (eb->path_tracker)
            {
              tracker_path_delete(eb->path_tracker, path);
            }

          /* ### Unusually, we end this 'delete' node record with only a single
                 blank line after the header block -- no extra blank line. */
          SVN_ERR(dump_node_delete(eb->stream, path, pool));

          /* The remaining action is a non-replacing add-with-history */
          /* action = svn_node_action_add; */
        }
      /* FALL THROUGH to 'add' */

    case svn_node_action_add:
      if (eb->path_tracker)
        SVN_ERR_W(node_must_not_exist(eb, path, eb->current_rev, pool),
                  apr_psprintf(pool,
                               _("Adding already existing path '%s' in r%ld"),
                               path, eb->current_rev));

      svn_repos__dumpfile_header_push(
        headers, SVN_REPOS_DUMPFILE_NODE_ACTION, "add");

      if (! is_copy)
        {
          if (eb->path_tracker)
            tracker_path_add(eb->path_tracker, path);

          /* Dump all contents for a simple 'add'. */
          if (kind == svn_node_file)
            must_dump_text = TRUE;
          must_dump_props = TRUE;
        }
      else
        {
          if (eb->path_tracker)
            {
              SVN_ERR_W(node_must_exist(eb, compare_path, compare_rev,
                                        kind, pool),
                        apr_psprintf(pool,
                                     _("Copying from invalid path to "
                                       "'%s' in r%ld"),
                                     path, eb->current_rev));
              tracker_path_copy(eb->path_tracker, path, compare_path,
                                compare_rev);
            }

          if (!eb->verify && cmp_rev < eb->oldest_dumped_rev
              && eb->notify_func)
            {
              notify_warning(pool, eb->notify_func, eb->notify_baton,
                             svn_repos_notify_warning_found_old_reference,
                             _("Referencing data in revision %ld,"
                               " which is older than the oldest"
                               " dumped revision (r%ld).  Loading this dump"
                               " into an empty repository"
                               " will fail."),
                             cmp_rev, eb->oldest_dumped_rev);
              if (eb->found_old_reference)
                *eb->found_old_reference = TRUE;
            }

          svn_repos__dumpfile_header_pushf(
            headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV, "%ld", cmp_rev);
          svn_repos__dumpfile_header_push(
            headers, SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH, cmp_path);

          SVN_ERR(svn_fs_revision_root(&compare_root,
                                       svn_fs_root_fs(eb->fs_root),
                                       compare_rev, pool));

          /* Need to decide if the copied node had any extra textual or
             property mods as well.  */
          SVN_ERR(svn_fs_props_changed(&must_dump_props,
                                       compare_root, compare_path,
                                       eb->fs_root, path, pool));
          if (kind == svn_node_file)
            {
              svn_checksum_t *checksum;
              const char *hex_digest;
              SVN_ERR(svn_fs_contents_changed(&must_dump_text,
                                              compare_root, compare_path,
                                              eb->fs_root, path, pool));

              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                           compare_root, compare_path,
                                           FALSE, pool));
              hex_digest = svn_checksum_to_cstring(checksum, pool);
              if (hex_digest)
                svn_repos__dumpfile_header_push(
                  headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_MD5, hex_digest);

              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1,
                                           compare_root, compare_path,
                                           FALSE, pool));
              hex_digest = svn_checksum_to_cstring(checksum, pool);
              if (hex_digest)
                svn_repos__dumpfile_header_push(
                  headers, SVN_REPOS_DUMPFILE_TEXT_COPY_SOURCE_SHA1, hex_digest);
            }
        }
      break;
    }

  if ((! must_dump_text) && (! must_dump_props))
    {
      /* If we're not supposed to dump text or props, so be it, we can
         just go home.  However, if either one needs to be dumped,
         then our dumpstream format demands that at a *minimum*, we
         see a lone "PROPS-END" as a divider between text and props
         content within the content-block. */
      SVN_ERR(svn_repos__dump_headers(eb->stream, headers, pool));
      len = 1;
      return svn_stream_write(eb->stream, "\n", &len); /* ### needed? */
    }

  /*** Start prepping content to dump... ***/

  /* If we are supposed to dump properties, write out a property
     length header and generate a stringbuf that contains those
     property values here. */
  if (must_dump_props)
    {
      apr_hash_t *prophash, *oldhash = NULL;
      svn_stream_t *propstream;

      SVN_ERR(svn_fs_node_proplist(&prophash, eb->fs_root, path, pool));

      /* If this is a partial dump, then issue a warning if we dump mergeinfo
         properties that refer to revisions older than the first revision
         dumped. */
      if (!eb->verify && eb->notify_func && eb->oldest_dumped_rev > 1)
        {
          svn_string_t *mergeinfo_str = svn_hash_gets(prophash,
                                                      SVN_PROP_MERGEINFO);
          if (mergeinfo_str)
            {
              /* An error in verifying the mergeinfo must not prevent dumping
                 the data. Ignore any such error. */
              svn_error_clear(verify_mergeinfo_revisions(
                                eb->found_old_mergeinfo,
                                mergeinfo_str->data, eb->oldest_dumped_rev,
                                eb->notify_func, eb->notify_baton,
                                pool));
            }
        }

      /* If we're checking UCS normalization, also parse any changed
         mergeinfo and warn about denormalized paths and name
         collisions there. */
      if (eb->verify && eb->check_normalization && eb->notify_func)
        {
          /* N.B.: This hash lookup happens only once; the conditions
             for verifying historic mergeinfo references and checking
             UCS normalization are mutually exclusive. */
          svn_string_t *mergeinfo_str = svn_hash_gets(prophash,
                                                      SVN_PROP_MERGEINFO);
          if (mergeinfo_str)
            {
              svn_string_t *oldinfo_str = NULL;
              if (compare_root)
                {
                  SVN_ERR(svn_fs_node_proplist(&oldhash,
                                               compare_root, compare_path,
                                               pool));
                  oldinfo_str = svn_hash_gets(oldhash, SVN_PROP_MERGEINFO);
                }
              SVN_ERR(check_mergeinfo_normalization(
                          path, mergeinfo_str->data,
                          (oldinfo_str ? oldinfo_str->data : NULL),
                          eb->notify_func, eb->notify_baton, pool));
            }
        }

      if (eb->use_deltas && compare_root)
        {
          /* Fetch the old property hash to diff against and output a header
             saying that our property contents are a delta. */
          if (!oldhash)         /* May have been set for normalization check */
            SVN_ERR(svn_fs_node_proplist(&oldhash, compare_root, compare_path,
                                         pool));
          svn_repos__dumpfile_header_push(
            headers, SVN_REPOS_DUMPFILE_PROP_DELTA, "true");
        }
      else
        oldhash = apr_hash_make(pool);
      propstring = svn_stringbuf_create_ensure(0, pool);
      propstream = svn_stream_from_stringbuf(propstring, pool);
      SVN_ERR(svn_hash_write_incremental(prophash, oldhash, propstream,
                                         "PROPS-END", pool));
      SVN_ERR(svn_stream_close(propstream));
    }

  /* If we are supposed to dump text, write out a text length header
     here, and an MD5 checksum (if available). */
  if (must_dump_text && (kind == svn_node_file))
    {
      svn_checksum_t *checksum;
      const char *hex_digest;

      if (eb->use_deltas)
        {
          /* Compute the text delta now and write it into a temporary
             file, so that we can find its length.  Output a header
             saying our text contents are a delta. */
          SVN_ERR(store_delta(&delta_file, &textlen, compare_root,
                              compare_path, eb->fs_root, path, pool));
          svn_repos__dumpfile_header_push(
            headers, SVN_REPOS_DUMPFILE_TEXT_DELTA, "true");

          if (compare_root)
            {
              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                           compare_root, compare_path,
                                           FALSE, pool));
              hex_digest = svn_checksum_to_cstring(checksum, pool);
              if (hex_digest)
                svn_repos__dumpfile_header_push(
                  headers, SVN_REPOS_DUMPFILE_TEXT_DELTA_BASE_MD5, hex_digest);

              SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1,
                                           compare_root, compare_path,
                                           FALSE, pool));
              hex_digest = svn_checksum_to_cstring(checksum, pool);
              if (hex_digest)
                svn_repos__dumpfile_header_push(
                  headers, SVN_REPOS_DUMPFILE_TEXT_DELTA_BASE_SHA1, hex_digest);
            }
        }
      else
        {
          /* Just fetch the length of the file. */
          SVN_ERR(svn_fs_file_length(&textlen, eb->fs_root, path, pool));
        }

      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_md5,
                                   eb->fs_root, path, FALSE, pool));
      hex_digest = svn_checksum_to_cstring(checksum, pool);
      if (hex_digest)
        svn_repos__dumpfile_header_push(
          headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_MD5, hex_digest);

      SVN_ERR(svn_fs_file_checksum(&checksum, svn_checksum_sha1,
                                   eb->fs_root, path, FALSE, pool));
      hex_digest = svn_checksum_to_cstring(checksum, pool);
      if (hex_digest)
        svn_repos__dumpfile_header_push(
          headers, SVN_REPOS_DUMPFILE_TEXT_CONTENT_SHA1, hex_digest);
    }

  /* 'Content-length:' is the last header before we dump the content,
     and is the sum of the text and prop contents lengths.  We write
     this only for the benefit of non-Subversion RFC-822 parsers. */
  SVN_ERR(svn_repos__dump_node_record(eb->stream, headers,
                                      must_dump_props ? propstring : NULL,
                                      must_dump_text,
                                      must_dump_text ? textlen : 0,
                                      TRUE /*content_length_always*/,
                                      pool));

  /* Dump text content */
  if (must_dump_text && (kind == svn_node_file))
    {
      svn_stream_t *contents;

      if (delta_file)
        {
          /* Make sure to close the underlying file when the stream is
             closed. */
          contents = svn_stream_from_aprfile2(delta_file, FALSE, pool);
        }
      else
        SVN_ERR(svn_fs_file_contents(&contents, eb->fs_root, path, pool));

      SVN_ERR(svn_stream_copy3(contents, svn_stream_disown(eb->stream, pool),
                               NULL, NULL, pool));
    }

  len = 2;
  return svn_stream_write(eb->stream, "\n\n", &len); /* ### needed? */
}


static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  *root_baton = make_dir_baton(NULL, NULL, SVN_INVALID_REVNUM,
                               edit_baton, NULL, pool);
  return SVN_NO_ERROR;
}


static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  const char *mypath = apr_pstrdup(pb->pool, path);

  /* remember this path needs to be deleted. */
  svn_hash_sets(pb->deleted_entries, mypath, pb);

  return SVN_NO_ERROR;
}


static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  void *was_deleted;
  svn_boolean_t is_copy = FALSE;
  struct dir_baton *new_db
    = make_dir_baton(path, copyfrom_path, copyfrom_rev, eb, pb, pool);

  /* This might be a replacement -- is the path already deleted? */
  was_deleted = svn_hash_gets(pb->deleted_entries, path);

  /* Detect an add-with-history. */
  is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

  /* Dump the node. */
  SVN_ERR(dump_node(eb, path,
                    svn_node_dir,
                    was_deleted ? svn_node_action_replace : svn_node_action_add,
                    is_copy,
                    is_copy ? copyfrom_path : NULL,
                    is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
                    pool));

  if (was_deleted)
    /* Delete the path, it's now been dumped. */
    svn_hash_sets(pb->deleted_entries, path, NULL);

  /* Check for normalized name clashes, but only if this is actually a
     new name in the parent, not a replacement. */
  if (!was_deleted && eb->verify && eb->check_normalization && eb->notify_func)
    {
      pb->check_name_collision = TRUE;
    }

  new_db->written_out = TRUE;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  struct dir_baton *new_db;
  const char *cmp_path = NULL;
  svn_revnum_t cmp_rev = SVN_INVALID_REVNUM;

  /* If the parent directory has explicit comparison path and rev,
     record the same for this one. */
  if (ARE_VALID_COPY_ARGS(pb->cmp_path, pb->cmp_rev))
    {
      cmp_path = svn_relpath_join(pb->cmp_path,
                                  svn_relpath_basename(path, pool), pool);
      cmp_rev = pb->cmp_rev;
    }

  new_db = make_dir_baton(path, cmp_path, cmp_rev, eb, pb, pool);
  *child_baton = new_db;
  return SVN_NO_ERROR;
}


static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct edit_baton *eb = db->edit_baton;
  apr_pool_t *subpool = svn_pool_create(pool);
  int i;
  apr_array_header_t *sorted_entries;

  /* Sort entries lexically instead of as paths. Even though the entries
   * are full paths they're all in the same directory (see comment in struct
   * dir_baton definition). So we really want to sort by basename, in which
   * case the lexical sort function is more efficient. */
  sorted_entries = svn_sort__hash(db->deleted_entries,
                                  svn_sort_compare_items_lexically, pool);
  for (i = 0; i < sorted_entries->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(sorted_entries, i,
                                       svn_sort__item_t).key;

      svn_pool_clear(subpool);

      /* By sending 'svn_node_unknown', the Node-kind: header simply won't
         be written out.  No big deal at all, really.  The loader
         shouldn't care.  */
      SVN_ERR(dump_node(eb, path,
                        svn_node_unknown, svn_node_action_delete,
                        FALSE, NULL, SVN_INVALID_REVNUM, subpool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  void *was_deleted;
  svn_boolean_t is_copy = FALSE;

  /* This might be a replacement -- is the path already deleted? */
  was_deleted = svn_hash_gets(pb->deleted_entries, path);

  /* Detect add-with-history. */
  is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

  /* Dump the node. */
  SVN_ERR(dump_node(eb, path,
                    svn_node_file,
                    was_deleted ? svn_node_action_replace : svn_node_action_add,
                    is_copy,
                    is_copy ? copyfrom_path : NULL,
                    is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
                    pool));

  if (was_deleted)
    /* delete the path, it's now been dumped. */
    svn_hash_sets(pb->deleted_entries, path, NULL);

  /* Check for normalized name clashes, but only if this is actually a
     new name in the parent, not a replacement. */
  if (!was_deleted && eb->verify && eb->check_normalization && eb->notify_func)
    {
      pb->check_name_collision = TRUE;
    }

  *file_baton = NULL;  /* muhahahaha */
  return SVN_NO_ERROR;
}


static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t ancestor_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  struct edit_baton *eb = pb->edit_baton;
  const char *cmp_path = NULL;
  svn_revnum_t cmp_rev = SVN_INVALID_REVNUM;

  /* If the parent directory has explicit comparison path and rev,
     record the same for this one. */
  if (ARE_VALID_COPY_ARGS(pb->cmp_path, pb->cmp_rev))
    {
      cmp_path = svn_relpath_join(pb->cmp_path,
                                  svn_relpath_basename(path, pool), pool);
      cmp_rev = pb->cmp_rev;
    }

  SVN_ERR(dump_node(eb, path,
                    svn_node_file, svn_node_action_change,
                    FALSE, cmp_path, cmp_rev, pool));

  *file_baton = NULL;  /* muhahahaha again */
  return SVN_NO_ERROR;
}


static svn_error_t *
change_dir_prop(void *parent_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;
  struct edit_baton *eb = db->edit_baton;

  /* This function is what distinguishes between a directory that is
     opened to merely get somewhere, vs. one that is opened because it
     *actually* changed by itself.

     Instead of recording the prop changes here, we just use this method
     to trigger writing the node; dump_node() finds all the changes. */
  if (! db->written_out)
    {
      SVN_ERR(dump_node(eb, db->path,
                        svn_node_dir, svn_node_action_change,
                        /* ### We pass is_copy=FALSE; this might be wrong
                           but the parameter isn't used when action=change. */
                        FALSE, db->cmp_path, db->cmp_rev, pool));
      db->written_out = TRUE;
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_error_t *err;
  svn_fs_root_t *fs_root;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_rev - 1;

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs, base_revision, scratch_pool));

  err = svn_fs_node_proplist(props, fs_root, path, result_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *props = apr_hash_make(result_pool);
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_fs_root_t *fs_root;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_rev - 1;

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs, base_revision, scratch_pool));

  SVN_ERR(svn_fs_check_path(kind, fs_root, path, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;
  svn_stream_t *contents;
  svn_stream_t *file_stream;
  const char *tmp_filename;
  svn_error_t *err;
  svn_fs_root_t *fs_root;

  if (!SVN_IS_VALID_REVNUM(base_revision))
    base_revision = eb->current_rev - 1;

  SVN_ERR(svn_fs_revision_root(&fs_root, eb->fs, base_revision, scratch_pool));

  err = svn_fs_file_contents(&contents, fs_root, path, scratch_pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);
  SVN_ERR(svn_stream_open_unique(&file_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 scratch_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, file_stream, NULL, NULL, scratch_pool));

  *filename = apr_pstrdup(result_pool, tmp_filename);

  return SVN_NO_ERROR;
}


static svn_error_t *
get_dump_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                svn_fs_t *fs,
                svn_revnum_t to_rev,
                const char *root_path,
                svn_stream_t *stream,
                svn_boolean_t *found_old_reference,
                svn_boolean_t *found_old_mergeinfo,
                svn_error_t *(*custom_close_directory)(void *dir_baton,
                                  apr_pool_t *scratch_pool),
                svn_repos_notify_func_t notify_func,
                void *notify_baton,
                svn_revnum_t oldest_dumped_rev,
                svn_boolean_t use_deltas,
                svn_boolean_t verify,
                svn_boolean_t check_normalization,
                apr_pool_t *pool)
{
  /* Allocate an edit baton to be stored in every directory baton.
     Set it up for the directory baton we create here, which is the
     root baton. */
  struct edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));
  svn_delta_editor_t *dump_editor = svn_delta_default_editor(pool);
  svn_delta_shim_callbacks_t *shim_callbacks =
                                svn_delta_shim_callbacks_default(pool);

  /* Set up the edit baton. */
  eb->stream = stream;
  eb->notify_func = notify_func;
  eb->notify_baton = notify_baton;
  eb->oldest_dumped_rev = oldest_dumped_rev;
  eb->path = apr_pstrdup(pool, root_path);
  SVN_ERR(svn_fs_revision_root(&(eb->fs_root), fs, to_rev, pool));
  eb->fs = fs;
  eb->current_rev = to_rev;
  eb->use_deltas = use_deltas;
  eb->verify = verify;
  eb->check_normalization = check_normalization;
  eb->found_old_reference = found_old_reference;
  eb->found_old_mergeinfo = found_old_mergeinfo;

  /* In non-verification mode, we will allow anything to be dumped because
     it might be an incremental dump with possible manual intervention.
     Also, this might be the last resort when it comes to data recovery.

     Else, make sure that all paths exists at their respective revisions.
  */
  eb->path_tracker = verify ? tracker_create(to_rev, pool) : NULL;

  /* Set up the editor. */
  dump_editor->open_root = open_root;
  dump_editor->delete_entry = delete_entry;
  dump_editor->add_directory = add_directory;
  dump_editor->open_directory = open_directory;
  if (custom_close_directory)
    dump_editor->close_directory = custom_close_directory;
  else
    dump_editor->close_directory = close_directory;
  dump_editor->change_dir_prop = change_dir_prop;
  dump_editor->add_file = add_file;
  dump_editor->open_file = open_file;

  *edit_baton = eb;
  *editor = dump_editor;

  shim_callbacks->fetch_kind_func = fetch_kind_func;
  shim_callbacks->fetch_props_func = fetch_props_func;
  shim_callbacks->fetch_base_func = fetch_base_func;
  shim_callbacks->fetch_baton = eb;

  SVN_ERR(svn_editor__insert_shims(editor, edit_baton, *editor, *edit_baton,
                                   NULL, NULL, shim_callbacks, pool, pool));

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------*/

/** The main dumping routine, svn_repos_dump_fs. **/


/* Helper for svn_repos_dump_fs.

   Write a revision record of REV in REPOS to writable STREAM, using POOL.
   Dump revision properties as well if INCLUDE_REVPROPS has been set.
   AUTHZ_FUNC and AUTHZ_BATON are passed directly to the repos layer.
 */
static svn_error_t *
write_revision_record(svn_stream_t *stream,
                      svn_repos_t *repos,
                      svn_revnum_t rev,
                      svn_boolean_t include_revprops,
                      svn_repos_authz_func_t authz_func,
                      void *authz_baton,
                      apr_pool_t *pool)
{
  apr_hash_t *props;
  apr_time_t timetemp;
  svn_string_t *datevalue;

  if (include_revprops)
    {
      SVN_ERR(svn_repos_fs_revision_proplist(&props, repos, rev,
                                             authz_func, authz_baton, pool));

      /* Run revision date properties through the time conversion to
        canonicalize them. */
      /* ### Remove this when it is no longer needed for sure. */
      datevalue = svn_hash_gets(props, SVN_PROP_REVISION_DATE);
      if (datevalue)
        {
          SVN_ERR(svn_time_from_cstring(&timetemp, datevalue->data, pool));
          datevalue = svn_string_create(svn_time_to_cstring(timetemp, pool),
                                        pool);
          svn_hash_sets(props, SVN_PROP_REVISION_DATE, datevalue);
        }
    }
   else
    {
      /* Although we won't use it, we still need this container for the
         call below. */
      props = apr_hash_make(pool);
    }

  SVN_ERR(svn_repos__dump_revision_record(stream, rev, NULL, props,
                                          include_revprops,
                                          pool));
  return SVN_NO_ERROR;
}

/* Baton for dump_filter_authz_func(). */
typedef struct dump_filter_baton_t
{
  svn_repos_dump_filter_func_t filter_func;
  void *filter_baton;
} dump_filter_baton_t;

/* Implements svn_repos_authz_func_t. */
static svn_error_t *
dump_filter_authz_func(svn_boolean_t *allowed,
                       svn_fs_root_t *root,
                       const char *path,
                       void *baton,
                       apr_pool_t *pool)
{
  dump_filter_baton_t *b = baton;

  return svn_error_trace(b->filter_func(allowed, root, path, b->filter_baton,
                                        pool));
}



/* The main dumper. */
svn_error_t *
svn_repos_dump_fs4(svn_repos_t *repos,
                   svn_stream_t *stream,
                   svn_revnum_t start_rev,
                   svn_revnum_t end_rev,
                   svn_boolean_t incremental,
                   svn_boolean_t use_deltas,
                   svn_boolean_t include_revprops,
                   svn_boolean_t include_changes,
                   svn_repos_notify_func_t notify_func,
                   void *notify_baton,
                   svn_repos_dump_filter_func_t filter_func,
                   void *filter_baton,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *dump_editor;
  void *dump_edit_baton = NULL;
  svn_revnum_t rev;
  svn_fs_t *fs = svn_repos_fs(repos);
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_revnum_t youngest;
  const char *uuid;
  int version;
  svn_boolean_t found_old_reference = FALSE;
  svn_boolean_t found_old_mergeinfo = FALSE;
  svn_repos_notify_t *notify;
  svn_repos_authz_func_t authz_func;
  dump_filter_baton_t authz_baton = {0};

  /* Make sure we catch up on the latest revprop changes.  This is the only
   * time we will refresh the revprop data in this query. */
  SVN_ERR(svn_fs_refresh_revision_props(fs, pool));

  /* Determine the current youngest revision of the filesystem. */
  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));

  /* Use default vals if necessary. */
  if (! SVN_IS_VALID_REVNUM(start_rev))
    start_rev = 0;
  if (! SVN_IS_VALID_REVNUM(end_rev))
    end_rev = youngest;
  if (! stream)
    stream = svn_stream_empty(pool);

  /* Validate the revisions. */
  if (start_rev > end_rev)
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             _("Start revision %ld"
                               " is greater than end revision %ld"),
                             start_rev, end_rev);
  if (end_rev > youngest)
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             _("End revision %ld is invalid "
                               "(youngest revision is %ld)"),
                             end_rev, youngest);

  /* We use read authz callback to implement dump filtering. If there is no
   * read access for some node, it will be excluded from dump as well as
   * references to it (e.g. copy source). */
  if (filter_func)
    {
      authz_func = dump_filter_authz_func;
      authz_baton.filter_func = filter_func;
      authz_baton.filter_baton = filter_baton;
    }
  else
    {
      authz_func = NULL;
    }

  /* Write out the UUID. */
  SVN_ERR(svn_fs_get_uuid(fs, &uuid, pool));

  /* If we're not using deltas, use the previous version, for
     compatibility with svn 1.0.x. */
  version = SVN_REPOS_DUMPFILE_FORMAT_VERSION;
  if (!use_deltas)
    version--;

  /* Write out "general" metadata for the dumpfile.  In this case, a
     magic header followed by a dumpfile format version. */
  SVN_ERR(svn_stream_printf(stream, pool,
                            SVN_REPOS_DUMPFILE_MAGIC_HEADER ": %d\n\n",
                            version));
  SVN_ERR(svn_stream_printf(stream, pool, SVN_REPOS_DUMPFILE_UUID
                            ": %s\n\n", uuid));

  /* Create a notify object that we can reuse in the loop. */
  if (notify_func)
    notify = svn_repos_notify_create(svn_repos_notify_dump_rev_end,
                                     pool);

  /* Main loop:  we're going to dump revision REV.  */
  for (rev = start_rev; rev <= end_rev; rev++)
    {
      svn_fs_root_t *to_root;
      svn_boolean_t use_deltas_for_rev;

      svn_pool_clear(iterpool);

      /* Check for cancellation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Write the revision record. */
      SVN_ERR(write_revision_record(stream, repos, rev, include_revprops,
                                    authz_func, &authz_baton, iterpool));

      /* When dumping revision 0, we just write out the revision record.
         The parser might want to use its properties.
         If we don't want revision changes at all, skip in any case. */
      if (rev == 0 || !include_changes)
        goto loop_end;

      /* Fetch the editor which dumps nodes to a file.  Regardless of
         what we've been told, don't use deltas for the first rev of a
         non-incremental dump. */
      use_deltas_for_rev = use_deltas && (incremental || rev != start_rev);
      SVN_ERR(get_dump_editor(&dump_editor, &dump_edit_baton, fs, rev,
                              "", stream, &found_old_reference,
                              &found_old_mergeinfo, NULL,
                              notify_func, notify_baton,
                              start_rev, use_deltas_for_rev, FALSE, FALSE,
                              iterpool));

      /* Drive the editor in one way or another. */
      SVN_ERR(svn_fs_revision_root(&to_root, fs, rev, iterpool));

      /* If this is the first revision of a non-incremental dump,
         we're in for a full tree dump.  Otherwise, we want to simply
         replay the revision.  */
      if ((rev == start_rev) && (! incremental))
        {
          /* Compare against revision 0, so everything appears to be added. */
          svn_fs_root_t *from_root;
          SVN_ERR(svn_fs_revision_root(&from_root, fs, 0, iterpool));
          SVN_ERR(svn_repos_dir_delta2(from_root, "", "",
                                       to_root, "",
                                       dump_editor, dump_edit_baton,
                                       authz_func, &authz_baton,
                                       FALSE, /* don't send text-deltas */
                                       svn_depth_infinity,
                                       FALSE, /* don't send entry props */
                                       FALSE, /* don't ignore ancestry */
                                       iterpool));
        }
      else
        {
          /* The normal case: compare consecutive revs. */
          SVN_ERR(svn_repos_replay2(to_root, "", SVN_INVALID_REVNUM, FALSE,
                                    dump_editor, dump_edit_baton,
                                    authz_func, &authz_baton, iterpool));

          /* While our editor close_edit implementation is a no-op, we still
             do this for completeness. */
          SVN_ERR(dump_editor->close_edit(dump_edit_baton, iterpool));
        }

    loop_end:
      if (notify_func)
        {
          notify->revision = rev;
          notify_func(notify_baton, notify, iterpool);
        }
    }

  if (notify_func)
    {
      /* Did we issue any warnings about references to revisions older than
         the oldest dumped revision?  If so, then issue a final generic
         warning, since the inline warnings already issued might easily be
         missed. */

      notify = svn_repos_notify_create(svn_repos_notify_dump_end, iterpool);
      notify_func(notify_baton, notify, iterpool);

      if (found_old_reference)
        {
          notify_warning(iterpool, notify_func, notify_baton,
                         svn_repos_notify_warning_found_old_reference,
                         _("The range of revisions dumped "
                           "contained references to "
                           "copy sources outside that "
                           "range."));
        }

      /* Ditto if we issued any warnings about old revisions referenced
         in dumped mergeinfo. */
      if (found_old_mergeinfo)
        {
          notify_warning(iterpool, notify_func, notify_baton,
                         svn_repos_notify_warning_found_old_mergeinfo,
                         _("The range of revisions dumped "
                           "contained mergeinfo "
                           "which reference revisions outside "
                           "that range."));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/*----------------------------------------------------------------------*/

/* verify, based on dump */


/* Creating a new revision that changes /A/B/E/bravo means creating new
   directory listings for /, /A, /A/B, and /A/B/E in the new revision, with
   each entry not changed in the new revision a link back to the entry in a
   previous revision.  svn_repos_replay()ing a revision does not verify that
   those links are correct.

   For paths actually changed in the revision we verify, we get directory
   contents or file length twice: once in the dump editor, and once here.
   We could create a new verify baton, store in it the changed paths, and
   skip those here, but that means building an entire wrapper editor and
   managing two levels of batons.  The impact from checking these entries
   twice should be minimal, while the code to avoid it is not.
*/

static svn_error_t *
verify_directory_entry(void *baton, const void *key, apr_ssize_t klen,
                       void *val, apr_pool_t *pool)
{
  struct dir_baton *db = baton;
  svn_fs_dirent_t *dirent = (svn_fs_dirent_t *)val;
  char *path;
  svn_boolean_t right_kind;

  path = svn_relpath_join(db->path, (const char *)key, pool);

  /* since we can't access the directory entries directly by their ID,
     we need to navigate from the FS_ROOT to them (relatively expensive
     because we may start at a never rev than the last change to node).
     We check that the node kind stored in the noderev matches the dir
     entry.  This also ensures that all entries point to valid noderevs.
   */
  switch (dirent->kind) {
  case svn_node_dir:
    SVN_ERR(svn_fs_is_dir(&right_kind, db->edit_baton->fs_root, path, pool));
    if (!right_kind)
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("Node '%s' is not a directory."),
                               path);

    break;
  case svn_node_file:
    SVN_ERR(svn_fs_is_file(&right_kind, db->edit_baton->fs_root, path, pool));
    if (!right_kind)
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("Node '%s' is not a file."),
                               path);
    break;
  default:
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("Unexpected node kind %d for '%s'"),
                             dirent->kind, path);
  }

  return SVN_NO_ERROR;
}

/* Baton used by the check_name_collision hash iterator. */
struct check_name_collision_baton
{
  struct dir_baton *dir_baton;
  apr_hash_t *normalized;
  svn_membuf_t buffer;
};

/* Scan the directory and report all entry names that differ only in
   Unicode character representation. */
static svn_error_t *
check_name_collision(void *baton, const void *key, apr_ssize_t klen,
                     void *val, apr_pool_t *iterpool)
{
  struct check_name_collision_baton *const cb = baton;
  const char *name;
  const char *found;

  SVN_ERR(svn_utf__normalize(&name, key, klen, &cb->buffer));

  found = svn_hash_gets(cb->normalized, name);
  if (!found)
    svn_hash_sets(cb->normalized, apr_pstrdup(cb->buffer.pool, name),
                  normalized_unique);
  else if (found == normalized_collision)
    /* Skip already reported collision */;
  else
    {
      struct dir_baton *const db = cb->dir_baton;
      struct edit_baton *const eb = db->edit_baton;
      const char* normpath;

      svn_hash_sets(cb->normalized, apr_pstrdup(cb->buffer.pool, name),
                    normalized_collision);

      SVN_ERR(svn_utf__normalize(
                  &normpath, svn_relpath_join(db->path, name, iterpool),
                  SVN_UTF__UNKNOWN_LENGTH, &cb->buffer));
      notify_warning(iterpool, eb->notify_func, eb->notify_baton,
                     svn_repos_notify_warning_name_collision,
                     _("Duplicate representation of path '%s'"), normpath);
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
verify_close_directory(void *dir_baton, apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  apr_hash_t *dirents;
  SVN_ERR(svn_fs_dir_entries(&dirents, db->edit_baton->fs_root,
                             db->path, pool));
  SVN_ERR(svn_iter_apr_hash(NULL, dirents, verify_directory_entry,
                            dir_baton, pool));

  if (db->check_name_collision)
    {
      struct check_name_collision_baton check_baton;
      check_baton.dir_baton = db;
      check_baton.normalized = apr_hash_make(pool);
      svn_membuf__create(&check_baton.buffer, 0, pool);
      SVN_ERR(svn_iter_apr_hash(NULL, dirents, check_name_collision,
                                &check_baton, pool));
    }

  return close_directory(dir_baton, pool);
}

/* Verify revision REV in file system FS. */
static svn_error_t *
verify_one_revision(svn_fs_t *fs,
                    svn_revnum_t rev,
                    svn_repos_notify_func_t notify_func,
                    void *notify_baton,
                    svn_revnum_t start_rev,
                    svn_boolean_t check_normalization,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *dump_editor;
  void *dump_edit_baton;
  svn_fs_root_t *to_root;
  apr_hash_t *props;
  const svn_delta_editor_t *cancel_editor;
  void *cancel_edit_baton;

  /* Get cancellable dump editor, but with our close_directory handler.*/
  SVN_ERR(get_dump_editor(&dump_editor, &dump_edit_baton,
                          fs, rev, "",
                          svn_stream_empty(scratch_pool),
                          NULL, NULL,
                          verify_close_directory,
                          notify_func, notify_baton,
                          start_rev,
                          FALSE, TRUE, /* use_deltas, verify */
                          check_normalization,
                          scratch_pool));
  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            dump_editor, dump_edit_baton,
                                            &cancel_editor,
                                            &cancel_edit_baton,
                                            scratch_pool));
  SVN_ERR(svn_fs_revision_root(&to_root, fs, rev, scratch_pool));
  SVN_ERR(svn_fs_verify_root(to_root, scratch_pool));
  SVN_ERR(svn_repos_replay2(to_root, "", SVN_INVALID_REVNUM, FALSE,
                            cancel_editor, cancel_edit_baton,
                            NULL, NULL, scratch_pool));

  /* While our editor close_edit implementation is a no-op, we still
     do this for completeness. */
  SVN_ERR(cancel_editor->close_edit(cancel_edit_baton, scratch_pool));

  SVN_ERR(svn_fs_revision_proplist2(&props, fs, rev, FALSE, scratch_pool,
                                    scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton type used for forwarding notifications from FS API to REPOS API. */
struct verify_fs_notify_func_baton_t
{
   /* notification function to call (must not be NULL) */
   svn_repos_notify_func_t notify_func;

   /* baton to use for it */
   void *notify_baton;

   /* type of notification to send (we will simply plug in the revision) */
   svn_repos_notify_t *notify;
};

/* Forward the notification to BATON. */
static void
verify_fs_notify_func(svn_revnum_t revision,
                       void *baton,
                       apr_pool_t *pool)
{
  struct verify_fs_notify_func_baton_t *notify_baton = baton;

  notify_baton->notify->revision = revision;
  notify_baton->notify_func(notify_baton->notify_baton,
                            notify_baton->notify, pool);
}

static svn_error_t *
report_error(svn_revnum_t revision,
             svn_error_t *verify_err,
             svn_repos_verify_callback_t verify_callback,
             void *verify_baton,
             apr_pool_t *pool)
{
  if (verify_callback)
    {
      svn_error_t *cb_err;

      /* The caller provided us with a callback, so make him responsible
         for what's going to happen with the error. */
      cb_err = verify_callback(verify_baton, revision, verify_err, pool);
      svn_error_clear(verify_err);
      SVN_ERR(cb_err);

      return SVN_NO_ERROR;
    }
  else
    {
      /* No callback -- no second guessing.  Just return the error. */
      return svn_error_trace(verify_err);
    }
}

svn_error_t *
svn_repos_verify_fs3(svn_repos_t *repos,
                     svn_revnum_t start_rev,
                     svn_revnum_t end_rev,
                     svn_boolean_t check_normalization,
                     svn_boolean_t metadata_only,
                     svn_repos_notify_func_t notify_func,
                     void *notify_baton,
                     svn_repos_verify_callback_t verify_callback,
                     void *verify_baton,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  svn_fs_t *fs = svn_repos_fs(repos);
  svn_revnum_t youngest;
  svn_revnum_t rev;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_repos_notify_t *notify;
  svn_fs_progress_notify_func_t verify_notify = NULL;
  struct verify_fs_notify_func_baton_t *verify_notify_baton = NULL;
  svn_error_t *err;

  /* Make sure we catch up on the latest revprop changes.  This is the only
   * time we will refresh the revprop data in this query. */
  SVN_ERR(svn_fs_refresh_revision_props(fs, pool));

  /* Determine the current youngest revision of the filesystem. */
  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, pool));

  /* Use default vals if necessary. */
  if (! SVN_IS_VALID_REVNUM(start_rev))
    start_rev = 0;
  if (! SVN_IS_VALID_REVNUM(end_rev))
    end_rev = youngest;

  /* Validate the revisions. */
  if (start_rev > end_rev)
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             _("Start revision %ld"
                               " is greater than end revision %ld"),
                             start_rev, end_rev);
  if (end_rev > youngest)
    return svn_error_createf(SVN_ERR_REPOS_BAD_ARGS, NULL,
                             _("End revision %ld is invalid "
                               "(youngest revision is %ld)"),
                             end_rev, youngest);

  /* Create a notify object that we can reuse within the loop and a
     forwarding structure for notifications from inside svn_fs_verify(). */
  if (notify_func)
    {
      notify = svn_repos_notify_create(svn_repos_notify_verify_rev_end, pool);

      verify_notify = verify_fs_notify_func;
      verify_notify_baton = apr_palloc(pool, sizeof(*verify_notify_baton));
      verify_notify_baton->notify_func = notify_func;
      verify_notify_baton->notify_baton = notify_baton;
      verify_notify_baton->notify
        = svn_repos_notify_create(svn_repos_notify_verify_rev_structure, pool);
    }

  /* Verify global metadata and backend-specific data first. */
  err = svn_fs_verify(svn_fs_path(fs, pool), svn_fs_config(fs, pool),
                      start_rev, end_rev,
                      verify_notify, verify_notify_baton,
                      cancel_func, cancel_baton, pool);

  if (err && err->apr_err == SVN_ERR_CANCELLED)
    {
      return svn_error_trace(err);
    }
  else if (err)
    {
      SVN_ERR(report_error(SVN_INVALID_REVNUM, err, verify_callback,
                           verify_baton, iterpool));
    }

  if (!metadata_only)
    for (rev = start_rev; rev <= end_rev; rev++)
      {
        svn_pool_clear(iterpool);

        /* Wrapper function to catch the possible errors. */
        err = verify_one_revision(fs, rev, notify_func, notify_baton,
                                  start_rev, check_normalization,
                                  cancel_func, cancel_baton,
                                  iterpool);

        if (err && err->apr_err == SVN_ERR_CANCELLED)
          {
            return svn_error_trace(err);
          }
        else if (err)
          {
            SVN_ERR(report_error(rev, err, verify_callback, verify_baton,
                                 iterpool));
          }
        else if (notify_func)
          {
            /* Tell the caller that we're done with this revision. */
            notify->revision = rev;
            notify_func(notify_baton, notify, iterpool);
          }
      }

  /* We're done. */
  if (notify_func)
    {
      notify = svn_repos_notify_create(svn_repos_notify_verify_end, iterpool);
      notify_func(notify_baton, notify, iterpool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
