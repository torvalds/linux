/*
 * log.c :  Functions for logging Subversion operations
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

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_mergeinfo.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_hash.h"

#include "private/svn_log.h"


static const char *
log_depth(svn_depth_t depth, apr_pool_t *pool)
{
  if (depth == svn_depth_unknown)
    return "";
  return apr_pstrcat(pool, " depth=", svn_depth_to_word(depth), SVN_VA_NULL);
}

static const char *
log_include_merged_revisions(svn_boolean_t include_merged_revisions)
{
  if (include_merged_revisions)
    return " include-merged-revisions";
  return "";
}


const char *
svn_log__reparent(const char *path, apr_pool_t *pool)
{
  return apr_psprintf(pool, "reparent %s", svn_path_uri_encode(path, pool));

}

const char *
svn_log__change_rev_prop(svn_revnum_t rev, const char *name, apr_pool_t *pool)
{
  return apr_psprintf(pool, "change-rev-prop r%ld %s", rev,
                      svn_path_uri_encode(name, pool));
}

const char *
svn_log__rev_proplist(svn_revnum_t rev, apr_pool_t *pool)
{
  return apr_psprintf(pool, "rev-proplist r%ld", rev);
}

const char *
svn_log__rev_prop(svn_revnum_t rev, const char *name, apr_pool_t *pool)
{
  return apr_psprintf(pool, "rev-prop r%ld %s", rev,
                      svn_path_uri_encode(name, pool));
}

const char *
svn_log__commit(svn_revnum_t rev, apr_pool_t *pool)
{
    return apr_psprintf(pool, "commit r%ld", rev);
}

const char *
svn_log__get_file(const char *path, svn_revnum_t rev,
                  svn_boolean_t want_contents, svn_boolean_t want_props,
                  apr_pool_t *pool)
{
  return apr_psprintf(pool, "get-file %s r%ld%s%s",
                      svn_path_uri_encode(path, pool), rev,
                      want_contents ? " text" : "",
                      want_props ? " props" : "");
}

const char *
svn_log__get_dir(const char *path, svn_revnum_t rev,
                 svn_boolean_t want_contents, svn_boolean_t want_props,
                 apr_uint32_t dirent_fields,
                 apr_pool_t *pool)
{
  return apr_psprintf(pool, "get-dir %s r%ld%s%s",
                      svn_path_uri_encode(path, pool), rev,
                      want_contents ? " text" : "",
                      want_props ? " props" : "");
}

const char *
svn_log__get_mergeinfo(const apr_array_header_t *paths,
                       svn_mergeinfo_inheritance_t inherit,
                       svn_boolean_t include_descendants,
                       apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *space_separated_paths = svn_stringbuf_create_empty(pool);

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      svn_pool_clear(iterpool);
      if (i != 0)
        svn_stringbuf_appendcstr(space_separated_paths, " ");
      svn_stringbuf_appendcstr(space_separated_paths,
                               svn_path_uri_encode(path, iterpool));
    }
  svn_pool_destroy(iterpool);

  return apr_psprintf(pool, "get-mergeinfo (%s) %s%s",
                      space_separated_paths->data,
                      svn_inheritance_to_word(inherit),
                      include_descendants ? " include-descendants" : "");
}

const char *
svn_log__checkout(const char *path, svn_revnum_t rev, svn_depth_t depth,
                  apr_pool_t *pool)
{
  return apr_psprintf(pool, "checkout-or-export %s r%ld%s",
                      svn_path_uri_encode(path, pool), rev,
                      log_depth(depth, pool));
}

const char *
svn_log__update(const char *path, svn_revnum_t rev, svn_depth_t depth,
                svn_boolean_t send_copyfrom_args,
                apr_pool_t *pool)
{
  return apr_psprintf(pool, "update %s r%ld%s%s",
                      svn_path_uri_encode(path, pool), rev,
                      log_depth(depth, pool),
                      (send_copyfrom_args
                       ? " send-copyfrom-args"
                       : ""));
}

const char *
svn_log__switch(const char *path, const char *dst_path, svn_revnum_t revnum,
                svn_depth_t depth, apr_pool_t *pool)
{
  return apr_psprintf(pool, "switch %s %s@%ld%s",
                      svn_path_uri_encode(path, pool),
                      svn_path_uri_encode(dst_path, pool), revnum,
                      log_depth(depth, pool));
}

const char *
svn_log__status(const char *path, svn_revnum_t rev, svn_depth_t depth,
                apr_pool_t *pool)
{
  return apr_psprintf(pool, "status %s r%ld%s",
                      svn_path_uri_encode(path, pool), rev,
                      log_depth(depth, pool));
}

const char *
svn_log__diff(const char *path, svn_revnum_t from_revnum,
              const char *dst_path, svn_revnum_t revnum,
              svn_depth_t depth, svn_boolean_t ignore_ancestry,
              apr_pool_t *pool)
{
  const char *log_ignore_ancestry = (ignore_ancestry
                                     ? " ignore-ancestry"
                                     : "");
  if (strcmp(path, dst_path) == 0)
    return apr_psprintf(pool, "diff %s r%ld:%ld%s%s",
                        svn_path_uri_encode(path, pool), from_revnum, revnum,
                        log_depth(depth, pool), log_ignore_ancestry);
  return apr_psprintf(pool, "diff %s@%ld %s@%ld%s%s",
                      svn_path_uri_encode(path, pool), from_revnum,
                      svn_path_uri_encode(dst_path, pool), revnum,
                      log_depth(depth, pool), log_ignore_ancestry);
}

const char *
svn_log__log(const apr_array_header_t *paths,
             svn_revnum_t start, svn_revnum_t end,
             int limit, svn_boolean_t discover_changed_paths,
             svn_boolean_t strict_node_history,
             svn_boolean_t include_merged_revisions,
             const apr_array_header_t *revprops, apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *space_separated_paths = svn_stringbuf_create_empty(pool);
  svn_stringbuf_t *options = svn_stringbuf_create_empty(pool);

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      svn_pool_clear(iterpool);
      if (i != 0)
        svn_stringbuf_appendcstr(space_separated_paths, " ");
      svn_stringbuf_appendcstr(space_separated_paths,
                               svn_path_uri_encode(path, iterpool));
    }

  if (limit)
    {
      const char *tmp = apr_psprintf(pool, " limit=%d", limit);
      svn_stringbuf_appendcstr(options, tmp);
    }
  if (discover_changed_paths)
    svn_stringbuf_appendcstr(options, " discover-changed-paths");
  if (strict_node_history)
    svn_stringbuf_appendcstr(options, " strict");
  if (include_merged_revisions)
    svn_stringbuf_appendcstr(options,
                        log_include_merged_revisions(include_merged_revisions));
  if (revprops == NULL)
    svn_stringbuf_appendcstr(options, " revprops=all");
  else if (revprops->nelts > 0)
    {
      svn_stringbuf_appendcstr(options, " revprops=(");
      for (i = 0; i < revprops->nelts; i++)
        {
          const char *name = APR_ARRAY_IDX(revprops, i, const char *);
          svn_pool_clear(iterpool);
          if (i != 0)
            svn_stringbuf_appendcstr(options, " ");
          svn_stringbuf_appendcstr(options, svn_path_uri_encode(name,
                                                                iterpool));
        }
      svn_stringbuf_appendcstr(options, ")");
    }
  svn_pool_destroy(iterpool);
  return apr_psprintf(pool, "log (%s) r%ld:%ld%s",
                      space_separated_paths->data, start, end,
                      options->data);
}

const char *
svn_log__get_locations(const char *path, svn_revnum_t peg_revision,
                       const apr_array_header_t *location_revisions,
                       apr_pool_t *pool)
{
  const svn_revnum_t *revision_ptr, *revision_ptr_start, *revision_ptr_end;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *space_separated_revnums = svn_stringbuf_create_empty(pool);

  revision_ptr_start = (const svn_revnum_t *)location_revisions->elts;
  revision_ptr = revision_ptr_start;
  revision_ptr_end = revision_ptr + location_revisions->nelts;
  while (revision_ptr < revision_ptr_end)
    {
      svn_pool_clear(iterpool);
      if (revision_ptr != revision_ptr_start)
        svn_stringbuf_appendcstr(space_separated_revnums, " ");
      svn_stringbuf_appendcstr(space_separated_revnums,
                               apr_psprintf(iterpool, "%ld", *revision_ptr));
      ++revision_ptr;
    }
  svn_pool_destroy(iterpool);

  return apr_psprintf(pool, "get-locations %s@%ld (%s)",
                      svn_path_uri_encode(path, pool),
                      peg_revision, space_separated_revnums->data);
}

const char *
svn_log__get_location_segments(const char *path, svn_revnum_t peg_revision,
                               svn_revnum_t start, svn_revnum_t end,
                               apr_pool_t *pool)
{
  return apr_psprintf(pool, "get-location-segments %s@%ld r%ld:%ld",
                      svn_path_uri_encode(path, pool),
                      peg_revision, start, end);
}

const char *
svn_log__get_file_revs(const char *path, svn_revnum_t start, svn_revnum_t end,
                       svn_boolean_t include_merged_revisions,
                       apr_pool_t *pool)
{
  return apr_psprintf(pool, "get-file-revs %s r%ld:%ld%s",
                      svn_path_uri_encode(path, pool), start, end,
                      log_include_merged_revisions(include_merged_revisions));
}

const char *
svn_log__lock(apr_hash_t *targets,
              svn_boolean_t steal, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *space_separated_paths = svn_stringbuf_create_empty(pool);

  for (hi = apr_hash_first(pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_pool_clear(iterpool);
      if (space_separated_paths->len)
        svn_stringbuf_appendcstr(space_separated_paths, " ");
      svn_stringbuf_appendcstr(space_separated_paths,
                               svn_path_uri_encode(path, iterpool));
    }
  svn_pool_destroy(iterpool);

  return apr_psprintf(pool, "lock (%s)%s", space_separated_paths->data,
                      steal ? " steal" : "");
}

const char *
svn_log__unlock(apr_hash_t *targets,
                svn_boolean_t break_lock, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_stringbuf_t *space_separated_paths = svn_stringbuf_create_empty(pool);

  for (hi = apr_hash_first(pool, targets); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_pool_clear(iterpool);
      if (space_separated_paths->len)
        svn_stringbuf_appendcstr(space_separated_paths, " ");
      svn_stringbuf_appendcstr(space_separated_paths,
                               svn_path_uri_encode(path, iterpool));
    }
  svn_pool_destroy(iterpool);

  return apr_psprintf(pool, "unlock (%s)%s", space_separated_paths->data,
                      break_lock ? " break" : "");
}

const char *
svn_log__lock_one_path(const char *path, svn_boolean_t steal,
                       apr_pool_t *pool)
{
  apr_hash_t *paths = apr_hash_make(pool);
  svn_hash_sets(paths, path, path);
  return svn_log__lock(paths, steal, pool);
}

const char *
svn_log__unlock_one_path(const char *path, svn_boolean_t break_lock,
                         apr_pool_t *pool)
{
  apr_hash_t *paths = apr_hash_make(pool);
  svn_hash_sets(paths, path, path);
  return svn_log__unlock(paths, break_lock, pool);
}

const char *
svn_log__replay(const char *path, svn_revnum_t rev, apr_pool_t *pool)
{
  const char *log_path;

  if (path && path[0] != '\0')
    log_path = svn_path_uri_encode(path, pool);
  else
    log_path = "/";
  return apr_psprintf(pool, "replay %s r%ld", log_path, rev);
}

const char *
svn_log__get_inherited_props(const char *path,
                             svn_revnum_t rev,
                             apr_pool_t *pool)
{
  const char *log_path;

  if (path && path[0] != '\0')
    log_path = svn_path_uri_encode(path, pool);
  else
    log_path = "/";
  return apr_psprintf(pool, "get-inherited-props %s r%ld", log_path, rev);
}

const char *
svn_log__list(const char *path, svn_revnum_t revision,
              apr_array_header_t *patterns, svn_depth_t depth,
              apr_uint32_t dirent_fields, apr_pool_t *pool)
{
  svn_stringbuf_t *pattern_text = svn_stringbuf_create_empty(pool);
  const char *log_path;
  int i;

  if (path && path[0] != '\0')
    log_path = svn_path_uri_encode(path, pool);
  else
    log_path = "/";

  if (patterns)
    {
      for (i = 0; i < patterns->nelts; ++i)
        {
          const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
          svn_stringbuf_appendbyte(pattern_text, ' ');
          svn_stringbuf_appendcstr(pattern_text, pattern);
        }
    }
  else
    {
      svn_stringbuf_appendcstr(pattern_text, " <ANY>");
    }

  return apr_psprintf(pool, "list %s r%ld%s%s", log_path, revision,
                      log_depth(depth, pool), pattern_text->data);
}
