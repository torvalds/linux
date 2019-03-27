/*
 * svn_types.c :  Implementation for Subversion's data types.
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

#include <apr_pools.h>
#include <apr_uuid.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_props.h"
#include "svn_private_config.h"

#include "private/svn_dep_compat.h"
#include "private/svn_string_private.h"

svn_error_t *
svn_revnum_parse(svn_revnum_t *rev,
                 const char *str,
                 const char **endptr)
{
  const char *end;

  svn_revnum_t result = (svn_revnum_t)svn__strtoul(str, &end);

  if (endptr)
    *endptr = str;

  if (str == end)
    return svn_error_createf
              (SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
               *str == '-' ? _("Negative revision number found parsing '%s'")
                           : _("Invalid revision number found parsing '%s'"),
               str);

  /* a revision number with more than 9 digits is suspicious.
     Have a closer look at those. */
  if (str + 10 <= end)
    {
      /* we support 32 bit revision numbers only. check for overflows */
      if (str + 10 < end)
        return svn_error_createf
                  (SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
                  _("Revision number longer than 10 digits '%s'"), str);

      /* we support 32 bit revision numbers only. check for overflows */
      if (*str > '2' || (apr_uint32_t)result > APR_INT32_MAX)
        return svn_error_createf
                  (SVN_ERR_REVNUM_PARSE_FAILURE, NULL,
                  _("Revision number too large '%s'"), str);
    }

  if (endptr)
    *endptr = end;

  *rev = result;

  return SVN_NO_ERROR;
}

const char *
svn_uuid_generate(apr_pool_t *pool)
{
  apr_uuid_t uuid;
  char *uuid_str = apr_pcalloc(pool, APR_UUID_FORMATTED_LENGTH + 1);
  apr_uuid_get(&uuid);
  apr_uuid_format(uuid_str, &uuid);
  return uuid_str;
}

const char *
svn_depth_to_word(svn_depth_t depth)
{
  switch (depth)
    {
    case svn_depth_exclude:
      return "exclude";
    case svn_depth_unknown:
      return "unknown";
    case svn_depth_empty:
      return "empty";
    case svn_depth_files:
      return "files";
    case svn_depth_immediates:
      return "immediates";
    case svn_depth_infinity:
      return "infinity";
    default:
      return "INVALID-DEPTH";
    }
}


svn_depth_t
svn_depth_from_word(const char *word)
{
  if (strcmp(word, "exclude") == 0)
    return svn_depth_exclude;
  if (strcmp(word, "unknown") == 0)
    return svn_depth_unknown;
  if (strcmp(word, "empty") == 0)
    return svn_depth_empty;
  if (strcmp(word, "files") == 0)
    return svn_depth_files;
  if (strcmp(word, "immediates") == 0)
    return svn_depth_immediates;
  if (strcmp(word, "infinity") == 0)
    return svn_depth_infinity;
  /* There's no special value for invalid depth, and no convincing
     reason to make one yet, so just fall back to unknown depth.
     If you ever change that convention, check callers to make sure
     they're not depending on it (e.g., option parsing in main() ).
  */
  return svn_depth_unknown;
}

const char *
svn_node_kind_to_word(svn_node_kind_t kind)
{
  switch (kind)
    {
    case svn_node_none:
      return "none";
    case svn_node_file:
      return "file";
    case svn_node_dir:
      return "dir";
    case svn_node_symlink:
      return "symlink";
    case svn_node_unknown:
    default:
      return "unknown";
    }
}


svn_node_kind_t
svn_node_kind_from_word(const char *word)
{
  if (word == NULL)
    return svn_node_unknown;

  if (strcmp(word, "none") == 0)
    return svn_node_none;
  else if (strcmp(word, "file") == 0)
    return svn_node_file;
  else if (strcmp(word, "dir") == 0)
    return svn_node_dir;
  else if (strcmp(word, "symlink") == 0)
    return svn_node_symlink;
  else
    /* This also handles word == "unknown" */
    return svn_node_unknown;
}

const char *
svn_tristate__to_word(svn_tristate_t tristate)
{
  switch (tristate)
    {
      case svn_tristate_false:
        return "false";
      case svn_tristate_true:
        return "true";
      case svn_tristate_unknown:
      default:
        return NULL;
    }
}

svn_tristate_t
svn_tristate__from_word(const char *word)
{
  if (word == NULL)
    return svn_tristate_unknown;
  else if (0 == svn_cstring_casecmp(word, "true")
           || 0 == svn_cstring_casecmp(word, "yes")
           || 0 == svn_cstring_casecmp(word, "on")
           || 0 == strcmp(word, "1"))
    return svn_tristate_true;
  else if (0 == svn_cstring_casecmp(word, "false")
           || 0 == svn_cstring_casecmp(word, "no")
           || 0 == svn_cstring_casecmp(word, "off")
           || 0 == strcmp(word, "0"))
    return svn_tristate_false;

  return svn_tristate_unknown;
}

svn_commit_info_t *
svn_create_commit_info(apr_pool_t *pool)
{
  svn_commit_info_t *commit_info
    = apr_pcalloc(pool, sizeof(*commit_info));

  commit_info->revision = SVN_INVALID_REVNUM;
  /* All other fields were initialized to NULL above. */

  return commit_info;
}

svn_commit_info_t *
svn_commit_info_dup(const svn_commit_info_t *src_commit_info,
                    apr_pool_t *pool)
{
  svn_commit_info_t *dst_commit_info
    = apr_palloc(pool, sizeof(*dst_commit_info));

  dst_commit_info->date = src_commit_info->date
    ? apr_pstrdup(pool, src_commit_info->date) : NULL;
  dst_commit_info->author = src_commit_info->author
    ? apr_pstrdup(pool, src_commit_info->author) : NULL;
  dst_commit_info->revision = src_commit_info->revision;
  dst_commit_info->post_commit_err = src_commit_info->post_commit_err
    ? apr_pstrdup(pool, src_commit_info->post_commit_err) : NULL;
  dst_commit_info->repos_root = src_commit_info->repos_root
    ? apr_pstrdup(pool, src_commit_info->repos_root) : NULL;

  return dst_commit_info;
}

svn_log_changed_path2_t *
svn_log_changed_path2_create(apr_pool_t *pool)
{
  svn_log_changed_path2_t *new_changed_path
    = apr_pcalloc(pool, sizeof(*new_changed_path));

  new_changed_path->text_modified = svn_tristate_unknown;
  new_changed_path->props_modified = svn_tristate_unknown;

  return new_changed_path;
}

svn_log_changed_path2_t *
svn_log_changed_path2_dup(const svn_log_changed_path2_t *changed_path,
                          apr_pool_t *pool)
{
  svn_log_changed_path2_t *new_changed_path
    = apr_palloc(pool, sizeof(*new_changed_path));

  *new_changed_path = *changed_path;

  if (new_changed_path->copyfrom_path)
    new_changed_path->copyfrom_path =
      apr_pstrdup(pool, new_changed_path->copyfrom_path);

  return new_changed_path;
}

svn_dirent_t *
svn_dirent_create(apr_pool_t *result_pool)
{
  svn_dirent_t *new_dirent = apr_pcalloc(result_pool, sizeof(*new_dirent));

  new_dirent->kind = svn_node_unknown;
  new_dirent->size = SVN_INVALID_FILESIZE;
  new_dirent->created_rev = SVN_INVALID_REVNUM;
  new_dirent->time = 0;
  new_dirent->last_author = NULL;

  return new_dirent;
}

svn_dirent_t *
svn_dirent_dup(const svn_dirent_t *dirent,
               apr_pool_t *pool)
{
  svn_dirent_t *new_dirent = apr_palloc(pool, sizeof(*new_dirent));

  *new_dirent = *dirent;

  new_dirent->last_author = apr_pstrdup(pool, dirent->last_author);

  return new_dirent;
}

svn_log_entry_t *
svn_log_entry_create(apr_pool_t *pool)
{
  svn_log_entry_t *log_entry = apr_pcalloc(pool, sizeof(*log_entry));

  return log_entry;
}

svn_log_entry_t *
svn_log_entry_dup(const svn_log_entry_t *log_entry, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_log_entry_t *new_entry = apr_palloc(pool, sizeof(*new_entry));

  *new_entry = *log_entry;

  if (log_entry->revprops)
    new_entry->revprops = svn_prop_hash_dup(log_entry->revprops, pool);

  if (log_entry->changed_paths2)
    {
      new_entry->changed_paths2 = apr_hash_make(pool);

      for (hi = apr_hash_first(pool, log_entry->changed_paths2);
           hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *change;

          apr_hash_this(hi, &key, NULL, &change);

          svn_hash_sets(new_entry->changed_paths2, apr_pstrdup(pool, key),
                        svn_log_changed_path2_dup(change, pool));
        }
    }

  /* We can't copy changed_paths by itself without using deprecated code,
     but we don't have to, as this function was new after the introduction
     of the changed_paths2 field. */
  new_entry->changed_paths = new_entry->changed_paths2;

  return new_entry;
}

svn_location_segment_t *
svn_location_segment_dup(const svn_location_segment_t *segment,
                         apr_pool_t *pool)
{
  svn_location_segment_t *new_segment =
    apr_palloc(pool, sizeof(*new_segment));

  *new_segment = *segment;
  if (segment->path)
    new_segment->path = apr_pstrdup(pool, segment->path);
  return new_segment;
}
