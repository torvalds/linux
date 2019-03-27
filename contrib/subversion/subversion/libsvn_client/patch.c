/*
 * patch.c: patch application support
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_hash.h>
#include <apr_fnmatch.h>
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_diff.h"
#include "svn_hash.h"
#include "svn_io.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "svn_wc.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_eol_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_diff_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_sorts_private.h"

typedef struct hunk_info_t {
  /* The hunk. */
  svn_diff_hunk_t *hunk;

  /* The line where the hunk matched in the target file. */
  svn_linenum_t matched_line;

  /* Whether this hunk has been rejected. */
  svn_boolean_t rejected;

  /* Whether this hunk has already been applied (either manually
   * or by an earlier run of patch). */
  svn_boolean_t already_applied;

  /* The fuzz factor used when matching this hunk, i.e. how many
   * lines of leading and trailing context to ignore during matching. */
  svn_linenum_t match_fuzz;

  /* match_fuzz + the penalty caused by bad patch files */
  svn_linenum_t report_fuzz;
} hunk_info_t;

/* A struct carrying information related to the patched and unpatched
 * content of a target, be it a property or the text of a file. */
typedef struct target_content_t {
  /* Indicates whether unpatched content existed prior to patching. */
  svn_boolean_t existed;

  /* The line last read from the unpatched content. */
  svn_linenum_t current_line;

  /* The EOL-style of the unpatched content. Either 'none', 'fixed',
   * or 'native'. See the documentation of svn_subst_eol_style_t. */
  svn_subst_eol_style_t eol_style;

  /* If the EOL_STYLE above is not 'none', this is the EOL string
   * corresponding to the EOL-style. Else, it is the EOL string the
   * last line read from the target file was using. */
  const char *eol_str;

  /* An array containing apr_off_t offsets marking the beginning of
   * each line in the unpatched content. */
  apr_array_header_t *lines;

  /* An array containing hunk_info_t structures for hunks already matched. */
  apr_array_header_t *hunks;

  /* True if end-of-file was reached while reading from the unpatched
   * content. */
  svn_boolean_t eof;

  /* The keywords of the target. They will be contracted when reading
   * unpatched content and expanded when writing patched content.
   * When patching properties this hash is always empty. */
  apr_hash_t *keywords;

  /* A callback, with an associated baton, to read a line of unpatched
   * content. */
  svn_error_t *(*readline)(void *baton, svn_stringbuf_t **line,
                           const char **eol_str, svn_boolean_t *eof,
                           apr_pool_t *result_pool, apr_pool_t *scratch_pool);
  void *read_baton;

  /* A callback to get the current byte offset within the unpatched
   * content. Uses the read baton. */
  svn_error_t * (*tell)(void *baton, apr_off_t *offset,
                        apr_pool_t *scratch_pool);

  /* A callback to seek to an offset within the unpatched content.
   * Uses the read baton. */
  svn_error_t * (*seek)(void *baton, apr_off_t offset,
                        apr_pool_t *scratch_pool);

  /* A callback to write data to the patched content, with an
   * associated baton. */
  svn_error_t * (*write)(void *baton, const char *buf, apr_size_t len,
                         apr_pool_t *scratch_pool);
  void *write_baton;

} target_content_t;

typedef struct prop_patch_target_t {

  /* The name of the property */
  const char *name;

  /* The property value. This is NULL in case the property did not exist
   * prior to patch application (see also CONTENT->existed).
   * Note that the patch implementation does not support binary properties,
   * so this string is not expected to contain embedded NUL characters. */
  const svn_string_t *value;

  /* The patched property value.
   * This is equivalent to the target, except that in appropriate
   * places it contains the modified text as it appears in the patch file. */
  svn_stringbuf_t *patched_value;

  /* All information that is specific to the content of the property. */
  target_content_t *content;

  /* Represents the operation performed on the property. It can be added,
   * deleted or modified.
   * ### Should we use flags instead since we're not using all enum values? */
  svn_diff_operation_kind_t operation;

  /* When true the property change won't be applied */
  svn_boolean_t skipped;

  /* ### Here we'll add flags telling if the prop was added, deleted,
   * ### had_rejects, had_local_mods prior to patching and so on. */
} prop_patch_target_t;

typedef struct patch_target_t {
  /* The target path as it appeared in the patch file,
   * but in canonicalised form. */
  const char *canon_path_from_patchfile;

  /* The target path, relative to the working copy directory the
   * patch is being applied to. A patch strip count applies to this
   * and only this path. This is never NULL. */
  const char *local_relpath;

  /* The absolute path of the target on the filesystem.
   * Any symlinks the path from the patch file may contain are resolved.
   * Is not always known, so it may be NULL. */
  const char *local_abspath;

  /* The target file, read-only. This is NULL in case the target
   * file did not exist prior to patch application (see also
   * CONTENT->existed). */
  apr_file_t *file;

  /* The target file is a symlink */
  svn_boolean_t is_symlink;

  /* The patched file.
   * This is equivalent to the target, except that in appropriate
   * places it contains the modified text as it appears in the patch file.
   * The data in this file is written in repository-normal form.
   * EOL transformation and keyword contraction is performed when the
   * patched result is installed in the working copy. */
  apr_file_t *patched_file;

  /* Path to the patched file. */
  const char *patched_path;

  /* Hunks that are rejected will be written to this stream. */
  svn_stream_t *reject_stream;

  /* Path to the reject file. */
  const char *reject_path;

  /* The node kind of the target as found in WC-DB prior
   * to patch application. */
  svn_node_kind_t db_kind;

  /* The target's kind on disk prior to patch application. */
  svn_node_kind_t kind_on_disk;

  /* True if the target was locally deleted prior to patching. */
  svn_boolean_t locally_deleted;

  /* True if the target had to be skipped for some reason. */
  svn_boolean_t skipped;

  /* True if the reason for skipping is a local obstruction */
  svn_boolean_t obstructed;

  /* True if at least one hunk was rejected. */
  svn_boolean_t had_rejects;

  /* True if at least one property hunk was rejected. */
  svn_boolean_t had_prop_rejects;

  /* True if at least one hunk was handled as already applied */
  svn_boolean_t had_already_applied;

  /* True if at least one property hunk was handled as already applied */
  svn_boolean_t had_prop_already_applied;

  /* The operation on the target as set in the patch file */
  svn_diff_operation_kind_t operation;

  /* True if the target was added by the patch, which means that it did
   * not exist on disk before patching and has content after patching. */
  svn_boolean_t added;

  /* True if the target ended up being deleted by the patch. */
  svn_boolean_t deleted;

  /* Set if the target is supposed to be moved by the patch.
   * This applies to --git diffs which carry "rename from/to" headers. */
   const char *move_target_abspath;

  /* True if the target has the executable bit set. */
  svn_boolean_t executable;

  /* True if the patch changed the text of the target. */
  svn_boolean_t has_text_changes;

  /* True if the patch changed any of the properties of the target. */
  svn_boolean_t has_prop_changes;

  /* True if the patch contained a svn:special property. */
  svn_boolean_t is_special;

  /* All the information that is specific to the content of the target. */
  target_content_t *content;

  /* A hash table of prop_patch_target_t objects keyed by property names. */
  apr_hash_t *prop_targets;

  /* When TRUE, this patch uses the raw git symlink format instead of the
     Subversion internal style format where links start with 'link '. */
  svn_boolean_t git_symlink_format;

} patch_target_t;


/* A smaller struct containing a subset of patch_target_t.
 * Carries the minimal amount of information we still need for a
 * target after we're done patching it so we can free other resources. */
typedef struct patch_target_info_t {
  const char *local_abspath;
  svn_boolean_t deleted;
  svn_boolean_t added;
} patch_target_info_t;

/* Check if LOCAL_ABSPATH is recorded as added in TARGETS_INFO */
static svn_boolean_t
target_is_added(const apr_array_header_t *targets_info,
                const char *local_abspath,
                apr_pool_t *scratch_pool)
{
  int i;

  for (i = targets_info->nelts - 1; i >= 0; i--)
  {
    const patch_target_info_t *target_info =
      APR_ARRAY_IDX(targets_info, i, const patch_target_info_t *);

    const char *info = svn_dirent_skip_ancestor(target_info->local_abspath,
                                                local_abspath);

    if (info && !*info)
      return target_info->added;
    else if (info)
      return FALSE;
  }

  return FALSE;
}

/* Check if LOCAL_ABSPATH or an ancestor is recorded as deleted in
   TARGETS_INFO */
static svn_boolean_t
target_is_deleted(const apr_array_header_t *targets_info,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  int i;

  for (i = targets_info->nelts - 1; i >= 0; i--)
  {
    const patch_target_info_t *target_info =
      APR_ARRAY_IDX(targets_info, i, const patch_target_info_t *);

    const char *info = svn_dirent_skip_ancestor(target_info->local_abspath,
                                                local_abspath);

    if (info)
      return target_info->deleted;
  }

  return FALSE;
}


/* Strip STRIP_COUNT components from the front of PATH, returning
 * the result in *RESULT, allocated in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
strip_path(const char **result, const char *path, int strip_count,
           apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  int i;
  apr_array_header_t *components;
  apr_array_header_t *stripped;

  components = svn_path_decompose(path, scratch_pool);
  if (strip_count > components->nelts)
    return svn_error_createf(SVN_ERR_CLIENT_PATCH_BAD_STRIP_COUNT, NULL,
                             _("Cannot strip %u components from '%s'"),
                             strip_count,
                             svn_dirent_local_style(path, scratch_pool));

  stripped = apr_array_make(scratch_pool, components->nelts - strip_count,
                            sizeof(const char *));
  for (i = strip_count; i < components->nelts; i++)
    {
      const char *component;

      component = APR_ARRAY_IDX(components, i, const char *);
      APR_ARRAY_PUSH(stripped, const char *) = component;
    }

  *result = svn_path_compose(stripped, result_pool);

  return SVN_NO_ERROR;
}

/* Obtain KEYWORDS, EOL_STYLE and EOL_STR for LOCAL_ABSPATH.
 * WC_CTX is a context for the working copy the patch is applied to.
 * Use RESULT_POOL for allocations of fields in TARGET.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
obtain_eol_and_keywords_for_file(apr_hash_t **keywords,
                                 svn_subst_eol_style_t *eol_style,
                                 const char **eol_str,
                                 svn_wc_context_t *wc_ctx,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  svn_string_t *keywords_val, *eol_style_val;

  SVN_ERR(svn_wc_prop_list2(&props, wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));
  keywords_val = svn_hash_gets(props, SVN_PROP_KEYWORDS);
  if (keywords_val)
    {
      svn_revnum_t changed_rev;
      apr_time_t changed_date;
      const char *rev_str;
      const char *author;
      const char *url;
      const char *repos_root_url;
      const char *repos_relpath;

      SVN_ERR(svn_wc__node_get_changed_info(&changed_rev,
                                            &changed_date,
                                            &author, wc_ctx,
                                            local_abspath,
                                            scratch_pool,
                                            scratch_pool));
      rev_str = apr_psprintf(scratch_pool, "%ld", changed_rev);
      SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath, &repos_root_url,
                                          NULL,
                                          wc_ctx, local_abspath,
                                          scratch_pool, scratch_pool));
      url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                        scratch_pool);

      SVN_ERR(svn_subst_build_keywords3(keywords,
                                        keywords_val->data,
                                        rev_str, url, repos_root_url,
                                        changed_date,
                                        author, result_pool));
    }

  eol_style_val = svn_hash_gets(props, SVN_PROP_EOL_STYLE);
  if (eol_style_val)
    {
      svn_subst_eol_style_from_value(eol_style,
                                     eol_str,
                                     eol_style_val->data);
    }

  return SVN_NO_ERROR;
}

/* Resolve the exact path for a patch TARGET at path PATH_FROM_PATCHFILE,
 * which is the path of the target as it appeared in the patch file.
 * Put a canonicalized version of PATH_FROM_PATCHFILE into
 * TARGET->CANON_PATH_FROM_PATCHFILE.
 * WC_CTX is a context for the working copy the patch is applied to.
 * If possible, determine TARGET->WC_PATH, TARGET->ABS_PATH, TARGET->KIND,
 * TARGET->ADDED, and TARGET->PARENT_DIR_EXISTS.
 * Indicate in TARGET->SKIPPED whether the target should be skipped.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * HAS_TEXT_CHANGES specifies whether the target path will have some text
 * changes applied, implying that the target should be a file and not a
 * directory.
 * Use RESULT_POOL for allocations of fields in TARGET.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
resolve_target_path(patch_target_t *target,
                    const char *path_from_patchfile,
                    const char *root_abspath,
                    int strip_count,
                    svn_boolean_t has_text_changes,
                    svn_boolean_t follow_moves,
                    svn_wc_context_t *wc_ctx,
                    const apr_array_header_t *targets_info,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *stripped_path;
  svn_wc_status3_t *status;
  svn_error_t *err;
  svn_boolean_t under_root;

  target->canon_path_from_patchfile = svn_dirent_internal_style(
                                        path_from_patchfile, result_pool);

  /* We can't handle text changes on the patch root dir. */
  if (has_text_changes && target->canon_path_from_patchfile[0] == '\0')
    {
      /* An empty patch target path? What gives? Skip this. */
      target->skipped = TRUE;
      target->local_abspath = NULL;
      target->local_relpath = "";
      return SVN_NO_ERROR;
    }

  if (strip_count > 0)
    SVN_ERR(strip_path(&stripped_path, target->canon_path_from_patchfile,
                       strip_count, result_pool, scratch_pool));
  else
    stripped_path = target->canon_path_from_patchfile;

  if (svn_dirent_is_absolute(stripped_path))
    {
      target->local_relpath = svn_dirent_is_child(root_abspath,
                                                  stripped_path,
                                                  result_pool);

      if (! target->local_relpath)
        {
          /* The target path is either outside of the working copy
           * or it is the patch root itself. Skip it. */
          target->skipped = TRUE;
          target->local_abspath = NULL;
          target->local_relpath = stripped_path;
          return SVN_NO_ERROR;
        }
    }
  else
    {
      target->local_relpath = stripped_path;
    }

  /* Make sure the path is secure to use. We want the target to be inside
   * the locked tree and not be fooled by symlinks it might contain. */
  SVN_ERR(svn_dirent_is_under_root(&under_root,
                                   &target->local_abspath, root_abspath,
                                   target->local_relpath, result_pool));

  if (! under_root)
    {
      /* The target path is outside of the working copy. Skip it. */
      target->skipped = TRUE;
      target->local_abspath = NULL;
      return SVN_NO_ERROR;
    }

  if (target_is_deleted(targets_info, target->local_abspath, scratch_pool))
    {
      target->locally_deleted = TRUE;
      target->db_kind = svn_node_none;
      return SVN_NO_ERROR;
    }

  /* Skip things we should not be messing with. */
  err = svn_wc_status3(&status, wc_ctx, target->local_abspath,
                       result_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);

      target->locally_deleted = TRUE;
      target->db_kind = svn_node_none;
      status = NULL;
    }
  else if (status->node_status == svn_wc_status_ignored ||
           status->node_status == svn_wc_status_unversioned ||
           status->node_status == svn_wc_status_missing ||
           status->node_status == svn_wc_status_obstructed ||
           status->conflicted)
    {
      target->skipped = TRUE;
      target->obstructed = TRUE;
      return SVN_NO_ERROR;
    }
  else if (status->node_status == svn_wc_status_deleted)
    {
      target->locally_deleted = TRUE;
    }

  if (status && (status->kind != svn_node_unknown))
    target->db_kind = status->kind;
  else
    target->db_kind = svn_node_none;

  SVN_ERR(svn_io_check_special_path(target->local_abspath,
                                    &target->kind_on_disk, &target->is_symlink,
                                    scratch_pool));

  if (target->locally_deleted)
    {
      const char *moved_to_abspath = NULL;

      if (follow_moves
          && !target_is_added(targets_info, target->local_abspath,
                              scratch_pool))
        {
          SVN_ERR(svn_wc__node_was_moved_away(&moved_to_abspath, NULL,
                                              wc_ctx, target->local_abspath,
                                              result_pool, scratch_pool));
        }

      if (moved_to_abspath)
        {
          target->local_abspath = moved_to_abspath;
          target->local_relpath = svn_dirent_skip_ancestor(root_abspath,
                                                           moved_to_abspath);

          if (!target->local_relpath || target->local_relpath[0] == '\0')
            {
              /* The target path is outside of the patch area. Skip it. */
              target->skipped = TRUE;
              return SVN_NO_ERROR;
            }

          /* As far as we are concerned this target is not locally deleted. */
          target->locally_deleted = FALSE;

          SVN_ERR(svn_io_check_special_path(target->local_abspath,
                                            &target->kind_on_disk,
                                            &target->is_symlink,
                                            scratch_pool));
        }
      else if (target->kind_on_disk != svn_node_none)
        {
          target->skipped = TRUE;
          return SVN_NO_ERROR;
        }
    }

#ifndef HAVE_SYMLINK
  if (target->kind_on_disk == svn_node_file
      && !target->is_symlink
      && !target->locally_deleted
      && status->prop_status != svn_wc_status_none)
    {
      const svn_string_t *value;

      SVN_ERR(svn_wc_prop_get2(&value, wc_ctx, target->local_abspath,
                               SVN_PROP_SPECIAL, scratch_pool, scratch_pool));

      if (value)
        target->is_symlink = TRUE;
    }
#endif

  return SVN_NO_ERROR;
}

/* Baton for reading from properties. */
typedef struct prop_read_baton_t {
  const svn_string_t *value;
  apr_off_t offset;
} prop_read_baton_t;

/* Allocate *STRINGBUF in RESULT_POOL, and read into it one line from
 * the unpatched property value accessed via BATON.
 * Reading stops either after a line-terminator was found, or if
 * the property value runs out in which case *EOF is set to TRUE.
 * The line-terminator is not stored in *STRINGBUF.
 *
 * If the line is empty or could not be read, *line is set to NULL.
 *
 * The line-terminator is detected automatically and stored in *EOL
 * if EOL is not NULL. If the end of the property value is reached
 * and does not end with a newline character, and EOL is not NULL,
 * *EOL is set to NULL.
 *
 * SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
readline_prop(void *baton, svn_stringbuf_t **line, const char **eol_str,
              svn_boolean_t *eof, apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  prop_read_baton_t *b = baton;
  svn_stringbuf_t *str = NULL;
  const char *c;
  svn_boolean_t found_eof;

  if ((apr_uint64_t)b->offset >= (apr_uint64_t)b->value->len)
    {
      *eol_str = NULL;
      *eof = TRUE;
      *line = NULL;
      return SVN_NO_ERROR;
    }

  /* Read bytes into STR up to and including, but not storing,
   * the next EOL sequence. */
  *eol_str = NULL;
  found_eof = FALSE;
  do
    {
      c = b->value->data + b->offset;
      b->offset++;

      if (*c == '\0')
        {
          found_eof = TRUE;
          break;
        }
      else if (*c == '\n')
        {
          *eol_str = "\n";
        }
      else if (*c == '\r')
        {
          *eol_str = "\r";
          if (*(c + 1) == '\n')
            {
              *eol_str = "\r\n";
              b->offset++;
            }
        }
      else
        {
          if (str == NULL)
            str = svn_stringbuf_create_ensure(80, result_pool);
          svn_stringbuf_appendbyte(str, *c);
        }

      if (*eol_str)
        break;
    }
  while (c < b->value->data + b->value->len);

  if (eof)
    *eof = found_eof && !(str && str->len > 0);
  *line = str;

  return SVN_NO_ERROR;
}

/* Return in *OFFSET the current byte offset for reading from the
 * unpatched property value accessed via BATON.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
tell_prop(void *baton, apr_off_t *offset, apr_pool_t *scratch_pool)
{
  prop_read_baton_t *b = baton;

  *offset = b->offset;
  return SVN_NO_ERROR;
}

/* Seek to the specified by OFFSET in the unpatched property value accessed
 * via BATON. Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
seek_prop(void *baton, apr_off_t offset, apr_pool_t *scratch_pool)
{
  prop_read_baton_t *b = baton;

  b->offset = offset;
  return SVN_NO_ERROR;
}

/* Write LEN bytes from BUF into the patched property value accessed
 * via BATON. Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
write_prop(void *baton, const char *buf, apr_size_t len,
           apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *patched_value = baton;

  svn_stringbuf_appendbytes(patched_value, buf, len);
  return SVN_NO_ERROR;
}

/* Initialize a PROP_TARGET structure for PROP_NAME on the patch target
 * at LOCAL_ABSPATH. OPERATION indicates the operation performed on the
 * property. Use working copy context WC_CTX.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
init_prop_target(prop_patch_target_t **prop_target,
                 const patch_target_t *target,
                 const char *prop_name,
                 svn_diff_operation_kind_t operation,
                 svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  prop_patch_target_t *new_prop_target;
  target_content_t *content;
  const svn_string_t *value;
  prop_read_baton_t *prop_read_baton;

  content = apr_pcalloc(result_pool, sizeof(*content));

  /* All other fields are FALSE or NULL due to apr_pcalloc(). */
  content->current_line = 1;
  content->eol_style = svn_subst_eol_style_none;
  content->lines = apr_array_make(result_pool, 0, sizeof(apr_off_t));
  content->hunks = apr_array_make(result_pool, 0, sizeof(hunk_info_t *));
  content->keywords = apr_hash_make(result_pool);

  new_prop_target = apr_pcalloc(result_pool, sizeof(*new_prop_target));
  new_prop_target->name = apr_pstrdup(result_pool, prop_name);
  new_prop_target->operation = operation;
  new_prop_target->content = content;

  if (!(target->deleted || target->db_kind == svn_node_none))
    SVN_ERR(svn_wc_prop_get2(&value, wc_ctx, local_abspath, prop_name,
                             result_pool, scratch_pool));
  else
    value = NULL;

  content->existed = (value != NULL);
  new_prop_target->value = value;
  new_prop_target->patched_value = svn_stringbuf_create_empty(result_pool);


  /* Wire up the read and write callbacks. */
  prop_read_baton = apr_pcalloc(result_pool, sizeof(*prop_read_baton));
  prop_read_baton->value = value;
  prop_read_baton->offset = 0;
  content->readline = readline_prop;
  content->tell = tell_prop;
  content->seek = seek_prop;
  content->read_baton = prop_read_baton;
  content->write = write_prop;
  content->write_baton = new_prop_target->patched_value;

  *prop_target = new_prop_target;

  return SVN_NO_ERROR;
}

/* Allocate *STRINGBUF in RESULT_POOL, and read into it one line from
 * the unpatched file content accessed via BATON.
 * Reading stops either after a line-terminator was found,
 * or if EOF is reached in which case *EOF is set to TRUE.
 * The line-terminator is not stored in *STRINGBUF.
 *
 * If the line is empty or could not be read, *line is set to NULL.
 *
 * The line-terminator is detected automatically and stored in *EOL
 * if EOL is not NULL. If EOF is reached and FILE does not end
 * with a newline character, and EOL is not NULL, *EOL is set to NULL.
 *
 * SCRATCH_POOL is used for temporary allocations.
 */
static svn_error_t *
readline_file(void *baton, svn_stringbuf_t **line, const char **eol_str,
              svn_boolean_t *eof, apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_file_t *file = baton;

  SVN_ERR(svn_io_file_readline(file, line, eol_str, eof, APR_SIZE_MAX,
                               result_pool, scratch_pool));

  if (!(*line)->len)
    *line = NULL;
  else
    *eof = FALSE;

  return SVN_NO_ERROR;
}

/* Return in *OFFSET the current byte offset for reading from the
 * unpatched file content accessed via BATON.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
tell_file(void *baton, apr_off_t *offset, apr_pool_t *scratch_pool)
{
  apr_file_t *file = baton;

  SVN_ERR(svn_io_file_get_offset(offset, file, scratch_pool));
  return SVN_NO_ERROR;
}

/* Seek to the specified by OFFSET in the unpatched file content accessed
 * via BATON. Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
seek_file(void *baton, apr_off_t offset, apr_pool_t *scratch_pool)
{
  apr_file_t *file = baton;

  SVN_ERR(svn_io_file_seek(file, APR_SET, &offset, scratch_pool));
  return SVN_NO_ERROR;
}

/* Write LEN bytes from BUF into the patched file content accessed
 * via BATON. Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
write_file(void *baton, const char *buf, apr_size_t len,
           apr_pool_t *scratch_pool)
{
  apr_file_t *file = baton;

  SVN_ERR(svn_io_file_write_full(file, buf, len, &len, scratch_pool));
  return SVN_NO_ERROR;
}

/* Symlinks appear in patches in their repository normal form, abstracted by
 * the svn_subst_* module.  The functions below enable patches to change the
 * targets of symlinks.
 */

/* Baton for the (readline|tell|seek|write)_symlink functions. */
struct symlink_baton_t
{
  /* The path to the symlink on disk (not the path to the target of the link) */
  const char *local_abspath;

  /* Indicates whether the "normal form" of the symlink has been read. */
  svn_boolean_t at_eof;
};

/* Allocate *STRINGBUF in RESULT_POOL, and store into it the "normal form"
 * of the symlink accessed via BATON.
 *
 * Otherwise behaves like readline_file(), which see.
 */
static svn_error_t *
readline_symlink(void *baton, svn_stringbuf_t **line, const char **eol_str,
                 svn_boolean_t *eof, apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct symlink_baton_t *sb = baton;

  if (eof)
    *eof = TRUE;
  if (eol_str)
    *eol_str = NULL;

  if (sb->at_eof)
    {
      *line = NULL;
    }
  else
    {
      svn_stream_t *stream;
      const apr_size_t len_hint = 64; /* arbitrary */

      SVN_ERR(svn_subst_read_specialfile(&stream, sb->local_abspath,
                                         scratch_pool, scratch_pool));
      SVN_ERR(svn_stringbuf_from_stream(line, stream, len_hint, result_pool));
      *eof = FALSE;
      sb->at_eof = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Identical to readline_symlink(), but returns symlink in raw format to
 * allow patching links in git-style.
 */
static svn_error_t *
readline_symlink_git(void *baton, svn_stringbuf_t **line, const char **eol_str,
                     svn_boolean_t *eof, apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(readline_symlink(baton, line, eol_str, eof,
                           result_pool, scratch_pool));

  if (*line && (*line)->len > 5 && !strncmp((*line)->data, "link ", 5))
    svn_stringbuf_remove(*line, 0, 5); /* Skip "link " */

  return SVN_NO_ERROR;
}

/* Set *OFFSET to 1 or 0 depending on whether the "normal form" of
 * the symlink has already been read. */
static svn_error_t *
tell_symlink(void *baton, apr_off_t *offset, apr_pool_t *scratch_pool)
{
  struct symlink_baton_t *sb = baton;

  *offset = sb->at_eof ? 1 : 0;
  return SVN_NO_ERROR;
}

/* If offset is non-zero, mark the symlink as having been read in its
 * "normal form". Else, mark the symlink as not having been read yet. */
static svn_error_t *
seek_symlink(void *baton, apr_off_t offset, apr_pool_t *scratch_pool)
{
  struct symlink_baton_t *sb = baton;

  sb->at_eof = (offset != 0);
  return SVN_NO_ERROR;
}

/* Return a suitable filename for the target of PATCH.
 * Examine the ``old'' and ``new'' file names, and choose the file name
 * with the fewest path components, the shortest basename, and the shortest
 * total file name length (in that order). In case of a tie, return the new
 * filename. This heuristic is also used by Larry Wall's UNIX patch (except
 * that it prompts for a filename in case of a tie).
 * Additionally, for compatibility with git, if one of the filenames
 * is "/dev/null", use the other filename. */
static const char *
choose_target_filename(const svn_patch_t *patch)
{
  apr_size_t old;
  apr_size_t new;

  if (strcmp(patch->old_filename, "/dev/null") == 0)
    return patch->new_filename;
  if (strcmp(patch->new_filename, "/dev/null") == 0)
    return patch->old_filename;

  /* If the patch renames the target, use the old name while
   * applying hunks. The target will be renamed to the new name
   * after hunks have been applied. */
  if (patch->operation == svn_diff_op_moved)
    return patch->old_filename;

  old = svn_path_component_count(patch->old_filename);
  new = svn_path_component_count(patch->new_filename);

  if (old == new)
    {
      old = strlen(svn_dirent_basename(patch->old_filename, NULL));
      new = strlen(svn_dirent_basename(patch->new_filename, NULL));

      if (old == new)
        {
          old = strlen(patch->old_filename);
          new = strlen(patch->new_filename);
        }
    }

  return (old < new) ? patch->old_filename : patch->new_filename;
}

/* Attempt to initialize a *PATCH_TARGET structure for a target file
 * described by PATCH. Use working copy context WC_CTX.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * The patch target structure is allocated in RESULT_POOL, but if the target
 * should be skipped, PATCH_TARGET->SKIPPED is set and the target should be
 * treated as not fully initialized, e.g. the caller should not not do any
 * further operations on the target if it is marked to be skipped.
 * If REMOVE_TEMPFILES is TRUE, set up temporary files to be removed as
 * soon as they are no longer needed.
 * Use SCRATCH_POOL for all other allocations. */
static svn_error_t *
init_patch_target(patch_target_t **patch_target,
                  const svn_patch_t *patch,
                  const char *root_abspath,
                  svn_wc_context_t *wc_ctx, int strip_count,
                  svn_boolean_t remove_tempfiles,
                  const apr_array_header_t *targets_info,
                  apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;
  target_content_t *content;
  svn_boolean_t has_text_changes = FALSE;
  svn_boolean_t follow_moves;

  has_text_changes = ((patch->hunks && patch->hunks->nelts > 0)
                      || patch->binary_patch);

  content = apr_pcalloc(result_pool, sizeof(*content));

  /* All other fields in content are FALSE or NULL due to apr_pcalloc().*/
  content->current_line = 1;
  content->eol_style = svn_subst_eol_style_none;
  content->lines = apr_array_make(result_pool, 0, sizeof(apr_off_t));
  content->hunks = apr_array_make(result_pool, 0, sizeof(hunk_info_t *));
  content->keywords = apr_hash_make(result_pool);

  target = apr_pcalloc(result_pool, sizeof(*target));

  /* All other fields in target are FALSE or NULL due to apr_pcalloc(). */
  target->db_kind = svn_node_none;
  target->kind_on_disk = svn_node_none;
  target->content = content;
  target->prop_targets = apr_hash_make(result_pool);
  target->operation = patch->operation;

  if (patch->operation == svn_diff_op_added /* Allow replacing */
      || patch->operation == svn_diff_op_moved)
    {
      follow_moves = FALSE;
    }
  else if (patch->operation == svn_diff_op_unchanged
           && patch->hunks && patch->hunks->nelts == 1)
    {
      svn_diff_hunk_t *hunk = APR_ARRAY_IDX(patch->hunks, 0,
                                            svn_diff_hunk_t *);

      follow_moves = (svn_diff_hunk_get_original_start(hunk) != 0);
    }
  else
    follow_moves = TRUE;

  SVN_ERR(resolve_target_path(target, choose_target_filename(patch),
                              root_abspath, strip_count, has_text_changes,
                              follow_moves, wc_ctx, targets_info,
                              result_pool, scratch_pool));
  *patch_target = target;
  if (! target->skipped)
    {
      if (patch->old_symlink_bit == svn_tristate_true
          || patch->new_symlink_bit == svn_tristate_true)
        {
          target->git_symlink_format = TRUE;
        }

      /* ### Is it ok to set the operation of the target already here? Isn't
       * ### the target supposed to be marked with an operation after we have
       * ### determined that the changes will apply cleanly to the WC? Maybe
       * ### we should have kept the patch field in patch_target_t to be
       * ### able to distinguish between 'what the patch says we should do'
       * ### and 'what we can do with the given state of our WC'. */
      if (patch->operation == svn_diff_op_added)
        target->added = TRUE;
      else if (patch->operation == svn_diff_op_deleted)
        target->deleted = TRUE;
      else if (patch->operation == svn_diff_op_moved)
        {
          const char *move_target_path;
          const char *move_target_relpath;
          svn_boolean_t under_root;
          svn_boolean_t is_special;
          svn_node_kind_t kind_on_disk;
          svn_node_kind_t wc_kind;

          move_target_path = svn_dirent_internal_style(patch->new_filename,
                                                       scratch_pool);

          if (strip_count > 0)
            SVN_ERR(strip_path(&move_target_path, move_target_path,
                               strip_count, scratch_pool, scratch_pool));

          if (svn_dirent_is_absolute(move_target_path))
            {
              move_target_relpath = svn_dirent_is_child(root_abspath,
                                                        move_target_path,
                                                        scratch_pool);
              if (! move_target_relpath)
                {
                  /* The move target path is either outside of the working
                   * copy or it is the working copy itself. Skip it. */
                  target->skipped = TRUE;
                  return SVN_NO_ERROR;
                }
            }
          else
            move_target_relpath = move_target_path;

          /* Make sure the move target path is secure to use. */
          SVN_ERR(svn_dirent_is_under_root(&under_root,
                                           &target->move_target_abspath,
                                           root_abspath,
                                           move_target_relpath, result_pool));
          if (! under_root)
            {
              /* The target path is outside of the working copy. Skip it. */
              target->skipped = TRUE;
              target->move_target_abspath = NULL;
              return SVN_NO_ERROR;
            }

          SVN_ERR(svn_io_check_special_path(target->move_target_abspath,
                                            &kind_on_disk, &is_special,
                                            scratch_pool));
          SVN_ERR(svn_wc_read_kind2(&wc_kind, wc_ctx,
                                    target->move_target_abspath,
                                    FALSE, FALSE, scratch_pool));
          if (wc_kind == svn_node_file || wc_kind == svn_node_dir)
            {
              /* The move target path already exists on disk. */
              svn_error_t *err;
              const char *moved_from_abspath;

              err = svn_wc__node_was_moved_here(&moved_from_abspath, NULL,
                                                wc_ctx,
                                                target->move_target_abspath,
                                                scratch_pool, scratch_pool);

              if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
                {
                  svn_error_clear(err);
                  err = NULL;
                  moved_from_abspath = NULL;
                }
              else
                SVN_ERR(err);

              if (moved_from_abspath && (strcmp(moved_from_abspath,
                                                target->local_abspath) == 0))
                {
                  target->local_abspath = target->move_target_abspath;
                  target->move_target_abspath = NULL;
                  target->operation = svn_diff_op_modified;
                  target->locally_deleted = FALSE;
                  target->db_kind = wc_kind;
                  target->kind_on_disk = kind_on_disk;
                  target->is_special = is_special;

                  target->had_already_applied = TRUE; /* Make sure we notify */
                }
              else
                {
                  target->skipped = TRUE;
                  target->move_target_abspath = NULL;
                  return SVN_NO_ERROR;
                }

            }
          else if (kind_on_disk != svn_node_none
              || target_is_added(targets_info, target->move_target_abspath,
                                 scratch_pool))
            {
                  target->skipped = TRUE;
                  target->move_target_abspath = NULL;
                  return SVN_NO_ERROR;
            }
        }

      /* Create a temporary file to write the patched result to.
       * Also grab various bits of information about the file. */
      if (target->is_symlink)
        {
          struct symlink_baton_t *sb = apr_pcalloc(result_pool, sizeof(*sb));
          content->existed = TRUE;

          sb->local_abspath = target->local_abspath;

          /* Wire up the read callbacks. */
          content->read_baton = sb;

          content->readline = target->git_symlink_format ? readline_symlink_git
                                                         : readline_symlink;
          content->seek = seek_symlink;
          content->tell = tell_symlink;
        }
      else if (target->kind_on_disk == svn_node_file)
        {
          SVN_ERR(svn_io_file_open(&target->file, target->local_abspath,
                                   APR_READ | APR_BUFFERED,
                                   APR_OS_DEFAULT, result_pool));
          SVN_ERR(svn_io_is_file_executable(&target->executable,
                                            target->local_abspath,
                                            scratch_pool));
          SVN_ERR(obtain_eol_and_keywords_for_file(&content->keywords,
                                                   &content->eol_style,
                                                   &content->eol_str,
                                                   wc_ctx,
                                                   target->local_abspath,
                                                   result_pool,
                                                   scratch_pool));
          content->existed = TRUE;

          /* Wire up the read callbacks. */
          content->readline = readline_file;
          content->seek = seek_file;
          content->tell = tell_file;
          content->read_baton = target->file;
        }

      /* Open a temporary file to write the patched result to. */
      SVN_ERR(svn_io_open_unique_file3(&target->patched_file,
                                       &target->patched_path, NULL,
                                       remove_tempfiles ?
                                         svn_io_file_del_on_pool_cleanup :
                                         svn_io_file_del_none,
                                       result_pool, scratch_pool));

      /* Put the write callback in place. */
      content->write = write_file;
      content->write_baton = target->patched_file;

      /* Open a temporary stream to write rejected hunks to. */
      SVN_ERR(svn_stream_open_unique(&target->reject_stream,
                                     &target->reject_path, NULL,
                                     remove_tempfiles ?
                                         svn_io_file_del_on_pool_cleanup :
                                         svn_io_file_del_none,
                                     result_pool, scratch_pool));

      /* Handle properties. */
      if (! target->skipped)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(result_pool, patch->prop_patches);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *prop_name = apr_hash_this_key(hi);
              svn_prop_patch_t *prop_patch = apr_hash_this_val(hi);
              prop_patch_target_t *prop_target;

              SVN_ERR(init_prop_target(&prop_target,
                                       target, prop_name,
                                       prop_patch->operation,
                                       wc_ctx, target->local_abspath,
                                       result_pool, scratch_pool));
              svn_hash_sets(target->prop_targets, prop_name, prop_target);
            }

          /* Now, check for out-of-band mode changes and convert these in
             their Subversion equivalent properties. */
          if (patch->new_executable_bit != svn_tristate_unknown
              && patch->new_executable_bit != patch->old_executable_bit)
            {
              svn_diff_operation_kind_t operation;

              if (patch->new_executable_bit == svn_tristate_true)
                operation = svn_diff_op_added;
              else if (patch->new_executable_bit == svn_tristate_false)
                {
                  /* Made non-executable. */
                  if (patch->old_executable_bit == svn_tristate_true)
                    operation = svn_diff_op_deleted;
                  else
                    operation = svn_diff_op_unchanged;
                }
              else
                operation = svn_diff_op_unchanged;

              if (operation != svn_diff_op_unchanged)
                {
                  prop_patch_target_t *prop_target;

                  prop_target = svn_hash_gets(target->prop_targets,
                                              SVN_PROP_EXECUTABLE);

                  if (prop_target && operation != prop_target->operation)
                    {
                      return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                                               _("Invalid patch: specifies "
                                                 "contradicting mode changes and "
                                                 "%s changes (for '%s')"),
                                               SVN_PROP_EXECUTABLE,
                                               target->local_abspath);
                    }
                  else if (!prop_target)
                    {
                      SVN_ERR(init_prop_target(&prop_target,
                                               target, SVN_PROP_EXECUTABLE,
                                               operation,
                                               wc_ctx, target->local_abspath,
                                               result_pool, scratch_pool));
                      svn_hash_sets(target->prop_targets, SVN_PROP_EXECUTABLE,
                                    prop_target);
                    }
                }
            }

          if (patch->new_symlink_bit != svn_tristate_unknown
              && patch->new_symlink_bit != patch->old_symlink_bit)
            {
              svn_diff_operation_kind_t operation;

              if (patch->new_symlink_bit == svn_tristate_true)
                operation = svn_diff_op_added;
              else if (patch->new_symlink_bit == svn_tristate_false)
                {
                  /* Made non-symlink. */
                  if (patch->old_symlink_bit == svn_tristate_true)
                    operation = svn_diff_op_deleted;
                  else
                    operation = svn_diff_op_unchanged;
                }
              else
                operation = svn_diff_op_unchanged;

              if (operation != svn_diff_op_unchanged)
                {
                  prop_patch_target_t *prop_target;
                  prop_target = svn_hash_gets(target->prop_targets,
                                              SVN_PROP_SPECIAL);

                  if (prop_target && operation != prop_target->operation)
                    {
                      return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                                               _("Invalid patch: specifies "
                                                 "contradicting mode changes and "
                                                 "%s changes (for '%s')"),
                                               SVN_PROP_SPECIAL,
                                               target->local_abspath);
                    }
                  else if (!prop_target)
                    {
                      SVN_ERR(init_prop_target(&prop_target,
                                               target, SVN_PROP_SPECIAL,
                                               operation,
                                               wc_ctx, target->local_abspath,
                                               result_pool, scratch_pool));
                      svn_hash_sets(target->prop_targets, SVN_PROP_SPECIAL,
                                    prop_target);
                    }
                }
            }
        }
    }

  if ((target->locally_deleted || target->db_kind == svn_node_none)
      && !target->added
      && target->operation == svn_diff_op_unchanged)
    {
      svn_boolean_t maybe_add = FALSE;

      if (patch->hunks && patch->hunks->nelts == 1)
        {
          svn_diff_hunk_t *hunk = APR_ARRAY_IDX(patch->hunks, 0,
                                                svn_diff_hunk_t *);

          if (svn_diff_hunk_get_original_start(hunk) == 0)
            maybe_add = TRUE;
        }
      else if (patch->prop_patches && apr_hash_count(patch->prop_patches))
        {
          apr_hash_index_t *hi;
          svn_boolean_t all_add = TRUE;

          for (hi = apr_hash_first(result_pool, patch->prop_patches);
               hi;
               hi = apr_hash_next(hi))
            {
              svn_prop_patch_t *prop_patch = apr_hash_this_val(hi);

              if (prop_patch->operation != svn_diff_op_added)
                {
                  all_add = FALSE;
                  break;
                }
            }

          maybe_add = all_add;
        }
      /* Other implied types */

      if (maybe_add)
        target->added = TRUE;
    }
  else if (!target->deleted && !target->added
           && target->operation == svn_diff_op_unchanged)
    {
      svn_boolean_t maybe_delete = FALSE;

      if (patch->hunks && patch->hunks->nelts == 1)
        {
          svn_diff_hunk_t *hunk = APR_ARRAY_IDX(patch->hunks, 0,
                                                svn_diff_hunk_t *);

          if (svn_diff_hunk_get_modified_start(hunk) == 0)
            maybe_delete = TRUE;
        }

      /* Other implied types */

      if (maybe_delete)
        target->deleted = TRUE;
    }

  if (target->reject_stream != NULL)
    {
      /* The reject file needs a diff header. */
      const char *left_src = target->canon_path_from_patchfile;
      const char *right_src = target->canon_path_from_patchfile;

      /* Handle moves specifically? */
      if (target->added)
        left_src = "/dev/null";
      if (target->deleted)
        right_src = "/dev/null";

      SVN_ERR(svn_stream_printf(target->reject_stream, scratch_pool,
                                "--- %s" APR_EOL_STR
                                "+++ %s" APR_EOL_STR,
                                left_src, right_src));
    }

  return SVN_NO_ERROR;
}

/* Read a *LINE from CONTENT. If the line has not been read before
 * mark the line in CONTENT->LINES.
 * If a line could be read successfully, increase CONTENT->CURRENT_LINE,
 * and allocate *LINE in RESULT_POOL.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
readline(target_content_t *content,
         const char **line,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line_raw;
  const char *eol_str;
  svn_linenum_t max_line = (svn_linenum_t)content->lines->nelts + 1;

  if (content->eof || content->readline == NULL)
    {
      *line = "";
      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(content->current_line <= max_line);
  if (content->current_line == max_line)
    {
      apr_off_t offset;

      SVN_ERR(content->tell(content->read_baton, &offset,
                            scratch_pool));
      APR_ARRAY_PUSH(content->lines, apr_off_t) = offset;
    }

  SVN_ERR(content->readline(content->read_baton, &line_raw,
                            &eol_str, &content->eof,
                            result_pool, scratch_pool));
  if (content->eol_style == svn_subst_eol_style_none)
    content->eol_str = eol_str;

  if (line_raw)
    {
      /* Contract keywords. */
      SVN_ERR(svn_subst_translate_cstring2(line_raw->data, line,
                                           NULL, FALSE,
                                           content->keywords, FALSE,
                                           result_pool));
    }
  else
    *line = "";

  if ((line_raw && line_raw->len > 0) || eol_str)
    content->current_line++;

  SVN_ERR_ASSERT(content->current_line > 0);

  return SVN_NO_ERROR;
}

/* Seek to the specified LINE in CONTENT.
 * Mark any lines not read before in CONTENT->LINES.
 * Do temporary allocations in SCRATCH_POOL.
 */
static svn_error_t *
seek_to_line(target_content_t *content, svn_linenum_t line,
             apr_pool_t *scratch_pool)
{
  svn_linenum_t saved_line;
  svn_boolean_t saved_eof;

  SVN_ERR_ASSERT(line > 0);

  if (line == content->current_line)
    return SVN_NO_ERROR;

  saved_line = content->current_line;
  saved_eof = content->eof;

  if (line <= (svn_linenum_t)content->lines->nelts)
    {
      apr_off_t offset;

      offset = APR_ARRAY_IDX(content->lines, line - 1, apr_off_t);
      SVN_ERR(content->seek(content->read_baton, offset,
                            scratch_pool));
      content->current_line = line;
    }
  else
    {
      const char *dummy;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      while (! content->eof && content->current_line < line)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(readline(content, &dummy, iterpool, iterpool));
        }
      svn_pool_destroy(iterpool);
    }

  /* After seeking backwards from EOF position clear EOF indicator. */
  if (saved_eof && saved_line > content->current_line)
    content->eof = FALSE;

  return SVN_NO_ERROR;
}

/* Indicate in *MATCHED whether the original text of HUNK matches the patch
 * CONTENT at its current line. Lines within FUZZ lines of the start or
 * end of HUNK will always match. If IGNORE_WHITESPACE is set, we ignore
 * whitespace when doing the matching. When this function returns, neither
 * CONTENT->CURRENT_LINE nor the file offset in the target file will
 * have changed. If MATCH_MODIFIED is TRUE, match the modified hunk text,
 * rather than the original hunk text.
 * Do temporary allocations in POOL. */
static svn_error_t *
match_hunk(svn_boolean_t *matched, target_content_t *content,
           svn_diff_hunk_t *hunk, svn_linenum_t fuzz,
           svn_boolean_t ignore_whitespace,
           svn_boolean_t match_modified, apr_pool_t *pool)
{
  svn_stringbuf_t *hunk_line;
  const char *target_line;
  svn_linenum_t lines_read;
  svn_linenum_t saved_line;
  svn_boolean_t hunk_eof;
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;
  svn_linenum_t hunk_length;
  svn_linenum_t leading_context;
  svn_linenum_t trailing_context;
  svn_linenum_t fuzz_penalty;

  *matched = FALSE;

  if (content->eof)
    return SVN_NO_ERROR;

  fuzz_penalty = svn_diff_hunk__get_fuzz_penalty(hunk);

  if (fuzz_penalty > fuzz)
    return SVN_NO_ERROR;
  else
    fuzz -= fuzz_penalty;

  saved_line = content->current_line;
  lines_read = 0;
  lines_matched = FALSE;
  leading_context = svn_diff_hunk_get_leading_context(hunk);
  trailing_context = svn_diff_hunk_get_trailing_context(hunk);
  if (match_modified)
    {
      svn_diff_hunk_reset_modified_text(hunk);
      hunk_length = svn_diff_hunk_get_modified_length(hunk);
    }
  else
    {
      svn_diff_hunk_reset_original_text(hunk);
      hunk_length = svn_diff_hunk_get_original_length(hunk);
    }
  iterpool = svn_pool_create(pool);
  do
    {
      const char *hunk_line_translated;

      svn_pool_clear(iterpool);

      if (match_modified)
        SVN_ERR(svn_diff_hunk_readline_modified_text(hunk, &hunk_line,
                                                     NULL, &hunk_eof,
                                                     iterpool, iterpool));
      else
        SVN_ERR(svn_diff_hunk_readline_original_text(hunk, &hunk_line,
                                                     NULL, &hunk_eof,
                                                     iterpool, iterpool));

      /* Contract keywords, if any, before matching. */
      SVN_ERR(svn_subst_translate_cstring2(hunk_line->data,
                                           &hunk_line_translated,
                                           NULL, FALSE,
                                           content->keywords, FALSE,
                                           iterpool));
      SVN_ERR(readline(content, &target_line, iterpool, iterpool));

      lines_read++;

      /* If the last line doesn't have a newline, we get EOF but still
       * have a non-empty line to compare. */
      if ((hunk_eof && hunk_line->len == 0) ||
          (content->eof && *target_line == 0))
        break;

      /* Leading/trailing fuzzy lines always match. */
      if ((lines_read <= fuzz && leading_context > fuzz) ||
          (lines_read > hunk_length - fuzz && trailing_context > fuzz))
        lines_matched = TRUE;
      else
        {
          if (ignore_whitespace)
            {
              char *hunk_line_trimmed;
              char *target_line_trimmed;

              hunk_line_trimmed = apr_pstrdup(iterpool, hunk_line_translated);
              target_line_trimmed = apr_pstrdup(iterpool, target_line);
              apr_collapse_spaces(hunk_line_trimmed, hunk_line_trimmed);
              apr_collapse_spaces(target_line_trimmed, target_line_trimmed);
              lines_matched = ! strcmp(hunk_line_trimmed, target_line_trimmed);
            }
          else
            lines_matched = ! strcmp(hunk_line_translated, target_line);
        }
    }
  while (lines_matched);

  *matched = lines_matched && hunk_eof && hunk_line->len == 0;
  SVN_ERR(seek_to_line(content, saved_line, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Scan lines of CONTENT for a match of the original text of HUNK,
 * up to but not including the specified UPPER_LINE. Use fuzz factor FUZZ.
 * If UPPER_LINE is zero scan until EOF occurs when reading from TARGET.
 * Return the line at which HUNK was matched in *MATCHED_LINE.
 * If the hunk did not match at all, set *MATCHED_LINE to zero.
 * If the hunk matched multiple times, and MATCH_FIRST is TRUE,
 * return the line number at which the first match occurred in *MATCHED_LINE.
 * If the hunk matched multiple times, and MATCH_FIRST is FALSE,
 * return the line number at which the last match occurred in *MATCHED_LINE.
 * If IGNORE_WHITESPACE is set, ignore whitespace during the matching.
 * If MATCH_MODIFIED is TRUE, match the modified hunk text,
 * rather than the original hunk text.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do all allocations in POOL. */
static svn_error_t *
scan_for_match(svn_linenum_t *matched_line,
               target_content_t *content,
               svn_diff_hunk_t *hunk, svn_boolean_t match_first,
               svn_linenum_t upper_line, svn_linenum_t fuzz,
               svn_boolean_t ignore_whitespace,
               svn_boolean_t match_modified,
               svn_cancel_func_t cancel_func, void *cancel_baton,
               apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  *matched_line = 0;
  iterpool = svn_pool_create(pool);
  while ((content->current_line < upper_line || upper_line == 0) &&
         ! content->eof)
    {
      svn_boolean_t matched;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      SVN_ERR(match_hunk(&matched, content, hunk, fuzz, ignore_whitespace,
                         match_modified, iterpool));
      if (matched)
        {
          svn_boolean_t taken = FALSE;
          int i;

          /* Don't allow hunks to match at overlapping locations. */
          for (i = 0; i < content->hunks->nelts; i++)
            {
              const hunk_info_t *hi;
              svn_linenum_t length;

              hi = APR_ARRAY_IDX(content->hunks, i, const hunk_info_t *);

              if (match_modified)
                length = svn_diff_hunk_get_modified_length(hi->hunk);
              else
                length = svn_diff_hunk_get_original_length(hi->hunk);

              taken = (! hi->rejected &&
                       content->current_line >= hi->matched_line &&
                       content->current_line < (hi->matched_line + length));
              if (taken)
                break;
            }

          if (! taken)
            {
              *matched_line = content->current_line;
              if (match_first)
                break;
            }
        }

      if (! content->eof)
        SVN_ERR(seek_to_line(content, content->current_line + 1,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Indicate in *MATCH whether the content described by CONTENT
 * matches the modified text of HUNK.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
match_existing_target(svn_boolean_t *match,
                      target_content_t *content,
                      svn_diff_hunk_t *hunk,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t lines_matched;
  apr_pool_t *iterpool;
  svn_boolean_t hunk_eof;
  svn_linenum_t saved_line;

  svn_diff_hunk_reset_modified_text(hunk);

  saved_line = content->current_line;

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      const char *line;
      svn_stringbuf_t *hunk_line;
      const char *line_translated;
      const char *hunk_line_translated;

      svn_pool_clear(iterpool);

      SVN_ERR(readline(content, &line, iterpool, iterpool));
      SVN_ERR(svn_diff_hunk_readline_modified_text(hunk, &hunk_line,
                                                   NULL, &hunk_eof,
                                                   iterpool, iterpool));
      /* Contract keywords. */
      SVN_ERR(svn_subst_translate_cstring2(line, &line_translated,
                                           NULL, FALSE,
                                           content->keywords,
                                           FALSE, iterpool));
      SVN_ERR(svn_subst_translate_cstring2(hunk_line->data,
                                           &hunk_line_translated,
                                           NULL, FALSE,
                                           content->keywords,
                                           FALSE, iterpool));
      lines_matched = ! strcmp(line_translated, hunk_line_translated);
      if (content->eof != hunk_eof)
        {
          svn_pool_destroy(iterpool);
          *match = FALSE;
          return SVN_NO_ERROR;
        }
    }
  while (lines_matched && ! content->eof && ! hunk_eof);
  svn_pool_destroy(iterpool);

  *match = (lines_matched && content->eof == hunk_eof);
  SVN_ERR(seek_to_line(content, saved_line, scratch_pool));

  return SVN_NO_ERROR;
}

/* Determine the line at which a HUNK applies to CONTENT of the TARGET
 * file, and return an appropriate hunk_info object in *HI, allocated from
 * RESULT_POOL. Use fuzz factor FUZZ. Set HI->FUZZ to FUZZ. If no correct
 * line can be determined, set HI->REJECTED to TRUE.  PREVIOUS_OFFSET
 * is the offset at which the previous matching hunk was applied, or zero.
 * IGNORE_WHITESPACE tells whether whitespace should be considered when
 * matching. IS_PROP_HUNK indicates whether the hunk patches file content
 * or a property.
 * When this function returns, neither CONTENT->CURRENT_LINE nor
 * the file offset in the target file will have changed.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do temporary allocations in POOL. */
static svn_error_t *
get_hunk_info(hunk_info_t **hi, patch_target_t *target,
              target_content_t *content,
              svn_diff_hunk_t *hunk, svn_linenum_t fuzz,
              svn_linenum_t previous_offset,
              svn_boolean_t ignore_whitespace,
              svn_boolean_t is_prop_hunk,
              svn_cancel_func_t cancel_func, void *cancel_baton,
              apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  svn_linenum_t matched_line;
  svn_linenum_t original_start;
  svn_boolean_t already_applied;

  original_start = svn_diff_hunk_get_original_start(hunk) + previous_offset;
  already_applied = FALSE;

  /* An original offset of zero means that this hunk wants to create
   * a new file. Don't bother matching hunks in that case, since
   * the hunk applies at line 1. If the file already exists, the hunk
   * is rejected, unless the file is versioned and its content matches
   * the file the patch wants to create.  */
  if (original_start == 0 && fuzz > 0)
    {
      matched_line = 0; /* reject any fuzz for new files */
    }
  else if (original_start == 0 && ! is_prop_hunk)
    {
      if (target->kind_on_disk == svn_node_file)
        {
          const svn_io_dirent2_t *dirent;
          SVN_ERR(svn_io_stat_dirent2(&dirent, target->local_abspath, FALSE,
                                      TRUE, scratch_pool, scratch_pool));

          if (dirent->kind == svn_node_file
              && !dirent->special
              && dirent->filesize == 0)
            {
              matched_line = 1; /* Matched an on-disk empty file */
            }
          else
            {
              if (target->db_kind == svn_node_file)
                {
                  svn_boolean_t file_matches;

                  SVN_ERR(match_existing_target(&file_matches, content, hunk,
                                            scratch_pool));
                  if (file_matches)
                    {
                      matched_line = 1;
                      already_applied = TRUE;
                    }
                  else
                    matched_line = 0; /* reject */
                }
              else
                matched_line = 0; /* reject */
            }
        }
      else
        matched_line = 1;
    }
  /* Same conditions apply as for the file case above.
   *
   * ### Since the hunk says the prop should be added we just assume so for
   * ### now and don't bother with storing the previous lines and such. When
   * ### we have the diff operation available we can just check for adds. */
  else if (original_start == 0 && is_prop_hunk)
    {
      if (content->existed)
        {
          svn_boolean_t prop_matches;

          SVN_ERR(match_existing_target(&prop_matches, content, hunk,
                                        scratch_pool));

          if (prop_matches)
            {
              matched_line = 1;
              already_applied = TRUE;
            }
          else
            matched_line = 0; /* reject */
        }
      else
        matched_line = 1;
    }
  else if (original_start > 0 && content->existed)
    {
      svn_linenum_t modified_start;
      svn_linenum_t saved_line = content->current_line;

      modified_start = svn_diff_hunk_get_modified_start(hunk);

      /* Scan for a match at the line where the hunk thinks it
       * should be going. */
      SVN_ERR(seek_to_line(content, original_start, scratch_pool));
      if (content->current_line != original_start)
        {
          /* Seek failed. */
          matched_line = 0;
        }
      else
        SVN_ERR(scan_for_match(&matched_line, content, hunk, TRUE,
                               original_start + 1, fuzz,
                               ignore_whitespace, FALSE,
                               cancel_func, cancel_baton,
                               scratch_pool));

      if (matched_line != original_start)
        {
          /* Check if the hunk is already applied.
           * We only check for an exact match here, and don't bother checking
           * for already applied patches with offset/fuzz, because such a
           * check would be ambiguous. */
          if (fuzz == 0)
            {
              if (modified_start == 0
                  && (target->operation == svn_diff_op_unchanged
                      || target->operation == svn_diff_op_deleted))
                {
                  /* Patch wants to delete the file. */

                  already_applied = target->locally_deleted;
                }
              else
                {
                  svn_linenum_t seek_to;

                  if (modified_start == 0)
                    seek_to = 1; /* Empty file case */
                  else
                    seek_to = modified_start;

                  SVN_ERR(seek_to_line(content, seek_to, scratch_pool));
                  SVN_ERR(scan_for_match(&matched_line, content,
                                         hunk, TRUE,
                                         modified_start + 1,
                                         fuzz, ignore_whitespace, TRUE,
                                         cancel_func, cancel_baton,
                                         scratch_pool));
                  already_applied = (matched_line == modified_start);
                }
            }
          else
            already_applied = FALSE;

          if (! already_applied)
            {
              int i;
              svn_linenum_t search_start = 1, search_end = 0;
              svn_linenum_t matched_line2;

              /* Search for closest match before or after original
                 start.  We have no backward search so search forwards
                 from the previous match (or start of file) to the
                 original start looking for the last match.  Then
                 search forwards from the original start looking for a
                 better match.  Finally search forwards from the start
                 of file to the previous hunk if that could result in
                 a better match. */

              for (i = content->hunks->nelts; i > 0; --i)
                {
                  const hunk_info_t *prev
                    = APR_ARRAY_IDX(content->hunks, i - 1, const hunk_info_t *);
                  if (!prev->rejected)
                    {
                      svn_linenum_t length;

                      length = svn_diff_hunk_get_original_length(prev->hunk);
                      search_start = prev->matched_line + length;
                      break;
                    }
                }

              /* Search from the previous match, or start of file,
                 towards the original location. */
              SVN_ERR(seek_to_line(content, search_start, scratch_pool));
              SVN_ERR(scan_for_match(&matched_line, content, hunk, FALSE,
                                     original_start, fuzz,
                                     ignore_whitespace, FALSE,
                                     cancel_func, cancel_baton,
                                     scratch_pool));

              /* If a match we only need to search forwards for a
                 better match, otherwise to the end of the file. */
              if (matched_line)
                search_end = original_start + (original_start - matched_line);

              /* Search from original location, towards the end. */
              SVN_ERR(seek_to_line(content, original_start + 1, scratch_pool));
              SVN_ERR(scan_for_match(&matched_line2, content, hunk,
                                     TRUE, search_end, fuzz, ignore_whitespace,
                                     FALSE, cancel_func, cancel_baton,
                                     scratch_pool));

              /* Chose the forward match if it is closer than the
                 backward match or if there is no backward match. */
              if (matched_line2
                  && (!matched_line
                      || (matched_line2 - original_start
                          < original_start - matched_line)))
                  matched_line = matched_line2;

              /* Search from before previous hunk if there could be a
                 better match. */
              if (search_start > 1
                  && (!matched_line
                      || (matched_line > original_start
                          && (matched_line - original_start
                              > original_start - search_start))))
                {
                  svn_linenum_t search_start2 = 1;

                  if (matched_line
                      && matched_line - original_start < original_start)
                    search_start2
                      = original_start - (matched_line - original_start) + 1;

                  SVN_ERR(seek_to_line(content, search_start2, scratch_pool));
                  SVN_ERR(scan_for_match(&matched_line2, content, hunk, FALSE,
                                         search_start - 1, fuzz,
                                         ignore_whitespace, FALSE,
                                         cancel_func, cancel_baton,
                                         scratch_pool));
                  if (matched_line2)
                    matched_line = matched_line2;
                }
            }
        }
      else if (matched_line > 0
               && fuzz == 0
               && (svn_diff_hunk_get_leading_context(hunk) == 0
                   || svn_diff_hunk_get_trailing_context(hunk) == 0)
               && (svn_diff_hunk_get_modified_length(hunk) >
                                    svn_diff_hunk_get_original_length(hunk)))
        {
          /* Check that we are not applying the same change that just adds some
             lines again, when we don't have enough context to see the
             difference */
          svn_linenum_t reverse_matched_line;

          SVN_ERR(seek_to_line(content, modified_start, scratch_pool));
          SVN_ERR(scan_for_match(&reverse_matched_line, content,
                                  hunk, TRUE,
                                  modified_start + 1,
                                  fuzz, ignore_whitespace, TRUE,
                                  cancel_func, cancel_baton,
                                  scratch_pool));

          /* We might want to check that we are actually at the start or the
             end of the file. Having no context implies that we should be. */
          already_applied = (reverse_matched_line == modified_start);
        }

      SVN_ERR(seek_to_line(content, saved_line, scratch_pool));
    }
  else if (!content->existed && svn_diff_hunk_get_modified_start(hunk) == 0)
    {
      /* The hunk wants to delete a file or property which doesn't exist. */
      matched_line = 0;
      already_applied = TRUE;
    }
  else
    {
      /* The hunk wants to modify a file or property which doesn't exist. */
      matched_line = 0;
    }

  (*hi) = apr_pcalloc(result_pool, sizeof(hunk_info_t));
  (*hi)->hunk = hunk;
  (*hi)->matched_line = matched_line;
  (*hi)->rejected = (matched_line == 0);
  (*hi)->already_applied = already_applied;
  (*hi)->report_fuzz = fuzz;
  (*hi)->match_fuzz = fuzz - svn_diff_hunk__get_fuzz_penalty(hunk);

  return SVN_NO_ERROR;
}

/* Copy lines to the patched content until the specified LINE has been
 * reached. Indicate in *EOF whether end-of-file was encountered while
 * reading from the target.
 * If LINE is zero, copy lines until end-of-file has been reached.
 * Do all allocations in POOL. */
static svn_error_t *
copy_lines_to_target(target_content_t *content, svn_linenum_t line,
                     apr_pool_t *pool)
{
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(pool);
  while ((content->current_line < line || line == 0) && ! content->eof)
    {
      const char *target_line;
      apr_size_t len;

      svn_pool_clear(iterpool);

      SVN_ERR(readline(content, &target_line, iterpool, iterpool));
      if (! content->eof)
        target_line = apr_pstrcat(iterpool, target_line, content->eol_str,
                                  SVN_VA_NULL);
      len = strlen(target_line);
      SVN_ERR(content->write(content->write_baton, target_line,
                             len, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Write the diff text of HUNK to TARGET's reject file,
 * and mark TARGET as having had rejects.
 * We don't expand keywords, nor normalise line-endings, in reject files.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
reject_hunk(patch_target_t *target, target_content_t *content,
            svn_diff_hunk_t *hunk, const char *prop_name,
            apr_pool_t *pool)
{
  svn_boolean_t eof;
  static const char * const text_atat = "@@";
  static const char * const prop_atat = "##";
  const char *atat;
  apr_pool_t *iterpool;

  if (prop_name)
    {
      /* ### Print 'Added', 'Deleted' or 'Modified' instead of 'Property'. */
      svn_stream_printf(target->reject_stream,
                        pool, "Property: %s" APR_EOL_STR, prop_name);
      atat = prop_atat;
    }
  else
    {
      atat = text_atat;
    }

  SVN_ERR(svn_stream_printf(target->reject_stream, pool,
                            "%s -%lu,%lu +%lu,%lu %s" APR_EOL_STR,
                            atat,
                            svn_diff_hunk_get_original_start(hunk),
                            svn_diff_hunk_get_original_length(hunk),
                            svn_diff_hunk_get_modified_start(hunk),
                            svn_diff_hunk_get_modified_length(hunk),
                            atat));

  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_diff_hunk_readline_diff_text(hunk, &hunk_line, &eol_str,
                                               &eof, iterpool, iterpool));
      if (! eof)
        {
          if (hunk_line->len >= 1)
            {
              apr_size_t len = hunk_line->len;

              SVN_ERR(svn_stream_write(target->reject_stream,
                                       hunk_line->data, &len));
            }

          if (eol_str)
            {
              SVN_ERR(svn_stream_puts(target->reject_stream, eol_str));
            }
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  if (prop_name)
    target->had_prop_rejects = TRUE;
  else
    target->had_rejects = TRUE;

  return SVN_NO_ERROR;
}

/* Write the modified text of the hunk described by HI to the patched
 * CONTENT. TARGET is the patch target.
 * If PROP_NAME is not NULL, the hunk is assumed to be targeted for
 * a property with the given name.
 * Do temporary allocations in POOL. */
static svn_error_t *
apply_hunk(patch_target_t *target, target_content_t *content,
           hunk_info_t *hi, const char *prop_name, apr_pool_t *pool)
{
  svn_linenum_t lines_read;
  svn_boolean_t eof;
  apr_pool_t *iterpool;
  svn_linenum_t fuzz = hi->match_fuzz;

  /* ### Is there a cleaner way to describe if we have an existing target?
   */
  if (target->kind_on_disk == svn_node_file || prop_name)
    {
      svn_linenum_t line;

      /* Move forward to the hunk's line, copying data as we go.
       * Also copy leading lines of context which matched with fuzz.
       * The target has changed on the fuzzy-matched lines,
       * so we should retain the target's version of those lines. */
      SVN_ERR(copy_lines_to_target(content, hi->matched_line + fuzz,
                                   pool));

      /* Skip the target's version of the hunk.
       * Don't skip trailing lines which matched with fuzz. */
      line = content->current_line +
             svn_diff_hunk_get_original_length(hi->hunk) - (2 * fuzz);
      SVN_ERR(seek_to_line(content, line, pool));
      if (content->current_line != line && ! content->eof)
        {
          /* Seek failed, reject this hunk. */
          hi->rejected = TRUE;
          SVN_ERR(reject_hunk(target, content, hi->hunk, prop_name, pool));
          return SVN_NO_ERROR;
        }
    }

  /* Write the hunk's version to the patched result.
   * Don't write the lines which matched with fuzz. */
  lines_read = 0;
  svn_diff_hunk_reset_modified_text(hi->hunk);
  iterpool = svn_pool_create(pool);
  do
    {
      svn_stringbuf_t *hunk_line;
      const char *eol_str;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_diff_hunk_readline_modified_text(hi->hunk, &hunk_line,
                                                   &eol_str, &eof,
                                                   iterpool, iterpool));
      lines_read++;
      if (lines_read > fuzz &&
          lines_read <= svn_diff_hunk_get_modified_length(hi->hunk) - fuzz)
        {
          apr_size_t len;

          if (hunk_line->len >= 1)
            {
              len = hunk_line->len;
              SVN_ERR(content->write(content->write_baton,
                                     hunk_line->data, len, iterpool));
            }

          if (eol_str)
            {
              /* Use the EOL as it was read from the patch file,
               * unless the target's EOL style is set by svn:eol-style */
              if (content->eol_style != svn_subst_eol_style_none)
                eol_str = content->eol_str;

              len = strlen(eol_str);
              SVN_ERR(content->write(content->write_baton,
                                     eol_str, len, iterpool));
            }
        }
    }
  while (! eof);
  svn_pool_destroy(iterpool);

  if (prop_name)
    target->has_prop_changes = TRUE;
  else
    target->has_text_changes = TRUE;

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for hunk HI,
 * using TARGET to determine the path. If the hunk is a property hunk,
 * PROP_NAME must be the name of the property, else NULL.
 * Use POOL for temporary allocations. */
static svn_error_t *
send_hunk_notification(const hunk_info_t *hi,
                       const patch_target_t *target,
                       const char *prop_name,
                       const svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  svn_wc_notify_t *notify;
  svn_wc_notify_action_t action;

  if (hi->already_applied)
    action = svn_wc_notify_patch_hunk_already_applied;
  else if (hi->rejected)
    action = svn_wc_notify_patch_rejected_hunk;
  else
    action = svn_wc_notify_patch_applied_hunk;

  notify = svn_wc_create_notify(target->local_abspath
                                    ? target->local_abspath
                                    : target->local_relpath,
                                action, pool);
  notify->hunk_original_start =
    svn_diff_hunk_get_original_start(hi->hunk);
  notify->hunk_original_length =
    svn_diff_hunk_get_original_length(hi->hunk);
  notify->hunk_modified_start =
    svn_diff_hunk_get_modified_start(hi->hunk);
  notify->hunk_modified_length =
    svn_diff_hunk_get_modified_length(hi->hunk);
  notify->hunk_matched_line = hi->matched_line;
  notify->hunk_fuzz = hi->report_fuzz;
  notify->prop_name = prop_name;

  ctx->notify_func2(ctx->notify_baton2, notify, pool);

  return SVN_NO_ERROR;
}

/* Use client context CTX to send a suitable notification for a patch TARGET.
 * Use POOL for temporary allocations. */
static svn_error_t *
send_patch_notification(const patch_target_t *target,
                        const svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  svn_wc_notify_t *notify;
  svn_wc_notify_action_t action;
  const char *notify_path;

  if (! ctx->notify_func2)
    return SVN_NO_ERROR;

  if (target->skipped)
    action = svn_wc_notify_skip;
  else if (target->deleted)
    action = svn_wc_notify_delete;
  else if (target->added || target->move_target_abspath)
    action = svn_wc_notify_add;
  else
    action = svn_wc_notify_patch;

  if (target->move_target_abspath)
    notify_path = target->move_target_abspath;
  else
    notify_path = target->local_abspath ? target->local_abspath
                                        : target->local_relpath;

  notify = svn_wc_create_notify(notify_path, action, scratch_pool);
  notify->kind = (target->db_kind == svn_node_dir) ? svn_node_dir
                                                   : svn_node_file;

  if (action == svn_wc_notify_skip)
    {
      if (target->obstructed)
        notify->content_state = svn_wc_notify_state_obstructed;
      else if (target->db_kind == svn_node_none ||
               target->db_kind == svn_node_unknown)
        notify->content_state = svn_wc_notify_state_missing;
      else
        notify->content_state = svn_wc_notify_state_unknown;
    }
  else
    {
      if (target->had_rejects)
        notify->content_state = svn_wc_notify_state_conflicted;
      else if (target->has_text_changes)
        notify->content_state = svn_wc_notify_state_changed;
      else if (target->had_already_applied)
        notify->content_state = svn_wc_notify_state_merged;
      else
        notify->content_state = svn_wc_notify_state_unchanged;

      if (target->had_prop_rejects)
        notify->prop_state = svn_wc_notify_state_conflicted;
      else if (target->has_prop_changes)
        notify->prop_state = svn_wc_notify_state_changed;
      else if (target->had_prop_already_applied)
        notify->prop_state = svn_wc_notify_state_merged;
      else
        notify->prop_state = svn_wc_notify_state_unchanged;
    }

  ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);

  if (action == svn_wc_notify_patch)
    {
      int i;
      apr_pool_t *iterpool;
      apr_array_header_t *prop_targets;

      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < target->content->hunks->nelts; i++)
        {
          const hunk_info_t *hi;

          svn_pool_clear(iterpool);

          hi = APR_ARRAY_IDX(target->content->hunks, i, hunk_info_t *);

          SVN_ERR(send_hunk_notification(hi, target, NULL /* prop_name */,
                                         ctx, iterpool));
        }

      prop_targets = svn_sort__hash(target->prop_targets,
                                    svn_sort_compare_items_lexically,
                                    scratch_pool);
      for (i = 0; i < prop_targets->nelts; i++)
        {
          int j;
          svn_sort__item_t item = APR_ARRAY_IDX(prop_targets, i,
                                                svn_sort__item_t);

          prop_patch_target_t *prop_target = item.value;

          for (j = 0; j < prop_target->content->hunks->nelts; j++)
            {
              const hunk_info_t *hi;

              svn_pool_clear(iterpool);

              hi = APR_ARRAY_IDX(prop_target->content->hunks, j,
                                 hunk_info_t *);

              /* Don't notify on the hunk level for added or deleted props. */
              if ((prop_target->operation != svn_diff_op_added &&
                  prop_target->operation != svn_diff_op_deleted)
                  || hi->rejected || hi->already_applied)
                SVN_ERR(send_hunk_notification(hi, target, prop_target->name,
                                               ctx, iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }

  if (!target->skipped && target->move_target_abspath)
    {
      /* Notify about deletion of move source. */
      notify = svn_wc_create_notify(target->local_abspath,
                                    svn_wc_notify_delete, scratch_pool);
      notify->kind = svn_node_file;
      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Implements the callback for svn_sort__array.  Puts hunks that match
   before hunks that do not match, puts hunks that match in order
   based on postion matched, puts hunks that do not match in order
   based on original position. */
static int
sort_matched_hunks(const void *a, const void *b)
{
  const hunk_info_t *item1 = *((const hunk_info_t * const *)a);
  const hunk_info_t *item2 = *((const hunk_info_t * const *)b);
  svn_boolean_t matched1 = !item1->rejected && !item1->already_applied;
  svn_boolean_t matched2 = !item2->rejected && !item2->already_applied;
  svn_linenum_t original1, original2;

  if (matched1 && matched2)
    {
      /* Both match so use order matched in file. */
      if (item1->matched_line > item2->matched_line)
        return 1;
      else if (item1->matched_line == item2->matched_line)
        return 0;
      else
        return -1;
    }
  else if (matched2)
    /* Only second matches, put it before first. */
    return 1;
  else if (matched1)
    /* Only first matches, put it before second. */
    return -1;

  /* Neither matches, sort by original_start. */
  original1 = svn_diff_hunk_get_original_start(item1->hunk);
  original2 = svn_diff_hunk_get_original_start(item2->hunk);
  if (original1 > original2)
    return 1;
  else if (original1 == original2)
    return 0;
  else
    return -1;
}


/* Apply a PATCH to a working copy at ABS_WC_PATH and put the result
 * into temporary files, to be installed in the working copy later.
 * Return information about the patch target in *PATCH_TARGET, allocated
 * in RESULT_POOL. Use WC_CTX as the working copy context.
 * STRIP_COUNT specifies the number of leading path components
 * which should be stripped from target paths in the patch.
 * REMOVE_TEMPFILES is as in svn_client_patch().
 * TARGETS_INFO is for preserving info across calls.
 * IGNORE_WHITESPACE tells whether whitespace should be considered when
 * doing the matching.
 * Call cancel CANCEL_FUNC with baton CANCEL_BATON to trigger cancellation.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
apply_one_patch(patch_target_t **patch_target, svn_patch_t *patch,
                const char *abs_wc_path, svn_wc_context_t *wc_ctx,
                int strip_count,
                svn_boolean_t ignore_whitespace,
                svn_boolean_t remove_tempfiles,
                const apr_array_header_t *targets_info,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch_target_t *target;
  apr_pool_t *iterpool;
  int i;
  static const svn_linenum_t MAX_FUZZ = 2;
  apr_hash_index_t *hash_index;
  svn_linenum_t previous_offset = 0;
  apr_array_header_t *prop_targets;

  SVN_ERR(init_patch_target(&target, patch, abs_wc_path, wc_ctx, strip_count,
                            remove_tempfiles, targets_info,
                            result_pool, scratch_pool));
  if (target->skipped)
    {
      *patch_target = target;
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);

  if (patch->hunks && patch->hunks->nelts)
    {
      /* Match hunks. */
      for (i = 0; i < patch->hunks->nelts; i++)
        {
          svn_diff_hunk_t *hunk;
          hunk_info_t *hi;
          svn_linenum_t fuzz = 0;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          hunk = APR_ARRAY_IDX(patch->hunks, i, svn_diff_hunk_t *);

          /* Determine the line the hunk should be applied at.
           * If no match is found initially, try with fuzz. */
          do
            {
              SVN_ERR(get_hunk_info(&hi, target, target->content, hunk, fuzz,
                                    previous_offset,
                                    ignore_whitespace,
                                    FALSE /* is_prop_hunk */,
                                    cancel_func, cancel_baton,
                                    result_pool, iterpool));
              fuzz++;
            }
          while (hi->rejected && fuzz <= MAX_FUZZ && ! hi->already_applied);

          if (hi->matched_line)
            previous_offset
              = hi->matched_line - svn_diff_hunk_get_original_start(hunk);

          APR_ARRAY_PUSH(target->content->hunks, hunk_info_t *) = hi;
        }

      /* Hunks are applied in the order determined by the matched line and
         this may be different from the order of the original lines. */
      svn_sort__array(target->content->hunks, sort_matched_hunks);

      /* Apply or reject hunks. */
      for (i = 0; i < target->content->hunks->nelts; i++)
        {
          hunk_info_t *hi;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          hi = APR_ARRAY_IDX(target->content->hunks, i, hunk_info_t *);
          if (hi->already_applied)
            {
              target->had_already_applied = TRUE;
              continue;
            }
          else if (hi->rejected)
            SVN_ERR(reject_hunk(target, target->content, hi->hunk,
                                NULL /* prop_name */,
                                iterpool));
          else
            SVN_ERR(apply_hunk(target, target->content, hi,
                               NULL /* prop_name */,  iterpool));
        }

      if (target->kind_on_disk == svn_node_file)
        {
          /* Copy any remaining lines to target. */
          SVN_ERR(copy_lines_to_target(target->content, 0, scratch_pool));
          if (! target->content->eof)
            {
              /* We could not copy the entire target file to the temporary
               * file, and would truncate the target if we copied the
               * temporary file on top of it. Skip this target. */
              target->skipped = TRUE;
            }
        }
    }
  else if (patch->binary_patch)
    {
      svn_stream_t *orig_stream;
      svn_boolean_t same;

      if (target->file)
        orig_stream = svn_stream_from_aprfile2(target->file, TRUE, iterpool);
      else
        orig_stream = svn_stream_empty(iterpool);

      SVN_ERR(svn_stream_contents_same2(
                &same, orig_stream,
                svn_diff_get_binary_diff_original_stream(patch->binary_patch,
                                                         iterpool),
                iterpool));
      svn_pool_clear(iterpool);

      if (same)
        {
          /* The file in the working copy is identical to the one expected by
             the patch... So we can write the result stream; no fuzz,
             just a 100% match */

          target->has_text_changes = TRUE;
        }
      else
        {
          /* Perhaps the file is identical to the resulting version, implying
             that the patch has already been applied */
          if (target->file)
            {
              apr_off_t start = 0;

              SVN_ERR(svn_io_file_seek(target->file, APR_SET, &start, iterpool));

              orig_stream = svn_stream_from_aprfile2(target->file, TRUE, iterpool);
            }
          else
            orig_stream = svn_stream_empty(iterpool);

          SVN_ERR(svn_stream_contents_same2(
                    &same, orig_stream,
                    svn_diff_get_binary_diff_result_stream(patch->binary_patch,
                                                           iterpool),
                    iterpool));
          svn_pool_clear(iterpool);

          if (same)
            target->had_already_applied = TRUE;
        }

      if (same)
        {
          SVN_ERR(svn_stream_copy3(
                svn_diff_get_binary_diff_result_stream(patch->binary_patch,
                                                       iterpool),
                svn_stream_from_aprfile2(target->patched_file, TRUE,
                                         iterpool),
                cancel_func, cancel_baton,
                iterpool));
        }
      else
        {
          /* ### TODO: Implement a proper reject of a binary patch

             This should at least setup things for a proper notification,
             and perhaps install a normal text conflict. Unlike normal unified
             diff based patches we have all the versions we would need for
             that in a much easier format than can be obtained from the patch
             file. */
          target->skipped = TRUE;
        }
    }
  else if (target->move_target_abspath)
    {
      /* ### Why do we do this?
             BH: I don't know, but if we don't do this some tests
                 on git style patches break.

         ### It would be much better to really move the actual file instead
             of copying to a temporary file; move that to target and then
             delete the original file

         ### BH: I have absolutely no idea if moving directories would work.
       */
      if (target->kind_on_disk == svn_node_file)
        {
          /* Copy any remaining lines to target. (read: all lines) */
          SVN_ERR(copy_lines_to_target(target->content, 0, scratch_pool));
          if (!target->content->eof)
            {
              /* We could not copy the entire target file to the temporary
               * file, and would truncate the target if we copied the
               * temporary file on top of it. Skip this target. */
              target->skipped = TRUE;
            }
        }
    }

  if (target->had_rejects || target->locally_deleted)
    target->deleted = FALSE;

  if (target->added
      && !(target->locally_deleted || target->db_kind == svn_node_none))
    {
      target->added = FALSE;
    }

  /* Assume nothing changed. Will be updated via property hunks */
  target->is_special = target->is_symlink;

  /* Match property hunks. */
  for (hash_index = apr_hash_first(scratch_pool, patch->prop_patches);
       hash_index;
       hash_index = apr_hash_next(hash_index))
    {
      svn_prop_patch_t *prop_patch;
      const char *prop_name;
      prop_patch_target_t *prop_target;

      prop_name = apr_hash_this_key(hash_index);
      prop_patch = apr_hash_this_val(hash_index);

      if (!strcmp(prop_name, SVN_PROP_SPECIAL))
        target->is_special = (prop_patch->operation != svn_diff_op_deleted);

      /* We'll store matched hunks in prop_content. */
      prop_target = svn_hash_gets(target->prop_targets, prop_name);

      for (i = 0; i < prop_patch->hunks->nelts; i++)
        {
          svn_diff_hunk_t *hunk;
          hunk_info_t *hi;
          svn_linenum_t fuzz = 0;

          svn_pool_clear(iterpool);

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          hunk = APR_ARRAY_IDX(prop_patch->hunks, i, svn_diff_hunk_t *);

          /* Determine the line the hunk should be applied at.
           * If no match is found initially, try with fuzz. */
          do
            {
              SVN_ERR(get_hunk_info(&hi, target, prop_target->content,
                                    hunk, fuzz, 0,
                                    ignore_whitespace,
                                    TRUE /* is_prop_hunk */,
                                    cancel_func, cancel_baton,
                                    result_pool, iterpool));
              fuzz++;
            }
          while (hi->rejected && fuzz <= MAX_FUZZ && ! hi->already_applied);

          APR_ARRAY_PUSH(prop_target->content->hunks, hunk_info_t *) = hi;
        }
    }

  /* Match implied property hunks. */
  if (patch->new_executable_bit != svn_tristate_unknown
      && patch->new_executable_bit != patch->old_executable_bit
      && svn_hash_gets(target->prop_targets, SVN_PROP_EXECUTABLE)
      && !svn_hash_gets(patch->prop_patches, SVN_PROP_EXECUTABLE))
    {
      hunk_info_t *hi;
      svn_diff_hunk_t *hunk;
      prop_patch_target_t *prop_target = svn_hash_gets(target->prop_targets,
                                                       SVN_PROP_EXECUTABLE);

      if (patch->new_executable_bit == svn_tristate_true)
        SVN_ERR(svn_diff_hunk__create_adds_single_line(
                                            &hunk,
                                            SVN_PROP_EXECUTABLE_VALUE,
                                            patch,
                                            result_pool,
                                            iterpool));
      else
        SVN_ERR(svn_diff_hunk__create_deletes_single_line(
                                            &hunk,
                                            SVN_PROP_EXECUTABLE_VALUE,
                                            patch,
                                            result_pool,
                                            iterpool));

      /* Derive a hunk_info from hunk. */
      SVN_ERR(get_hunk_info(&hi, target, prop_target->content,
                            hunk, 0 /* fuzz */, 0 /* previous_offset */,
                            ignore_whitespace,
                            TRUE /* is_prop_hunk */,
                            cancel_func, cancel_baton,
                            result_pool, iterpool));
      APR_ARRAY_PUSH(prop_target->content->hunks, hunk_info_t *) = hi;
    }

  if (patch->new_symlink_bit != svn_tristate_unknown
      && patch->new_symlink_bit != patch->old_symlink_bit
      && svn_hash_gets(target->prop_targets, SVN_PROP_SPECIAL)
      && !svn_hash_gets(patch->prop_patches, SVN_PROP_SPECIAL))
    {
      hunk_info_t *hi;
      svn_diff_hunk_t *hunk;

      prop_patch_target_t *prop_target = svn_hash_gets(target->prop_targets,
                                                       SVN_PROP_SPECIAL);

      if (patch->new_symlink_bit == svn_tristate_true)
        {
          SVN_ERR(svn_diff_hunk__create_adds_single_line(
                                            &hunk,
                                            SVN_PROP_SPECIAL_VALUE,
                                            patch,
                                            result_pool,
                                            iterpool));
          target->is_special = TRUE;
        }
      else
        {
          SVN_ERR(svn_diff_hunk__create_deletes_single_line(
                                            &hunk,
                                            SVN_PROP_SPECIAL_VALUE,
                                            patch,
                                            result_pool,
                                            iterpool));
          target->is_special = FALSE;
        }

      /* Derive a hunk_info from hunk. */
      SVN_ERR(get_hunk_info(&hi, target, prop_target->content,
                            hunk, 0 /* fuzz */, 0 /* previous_offset */,
                            ignore_whitespace,
                            TRUE /* is_prop_hunk */,
                            cancel_func, cancel_baton,
                            result_pool, iterpool));
      APR_ARRAY_PUSH(prop_target->content->hunks, hunk_info_t *) = hi;
    }

  /* When the node is deleted or does not exist after the patch is applied
     we should reject a few more property hunks that can't be applied even
     though the source matched */
  if (target->deleted
      || (!target->added &&
          (target->locally_deleted || target->db_kind == svn_node_none)))
    {
      for (hash_index = apr_hash_first(scratch_pool, target->prop_targets);
           hash_index;
           hash_index = apr_hash_next(hash_index))
        {
          prop_patch_target_t *prop_target = apr_hash_this_val(hash_index);

          if (prop_target->operation == svn_diff_op_deleted)
            continue;

          for (i = 0; i < prop_target->content->hunks->nelts; i++)
            {
              hunk_info_t *hi;

              hi = APR_ARRAY_IDX(prop_target->content->hunks, i, hunk_info_t*);

              if (hi->already_applied || hi->rejected)
                continue;
              else
                {
                  hi->rejected = TRUE;
                  prop_target->skipped = TRUE;

                  if (!target->deleted && !target->added)
                    target->skipped = TRUE;
                }
            }
        }
    }

  /* Apply or reject property hunks. */

  prop_targets = svn_sort__hash(target->prop_targets,
                                svn_sort_compare_items_lexically,
                                scratch_pool);
  for (i = 0; i < prop_targets->nelts; i++)
    {
      svn_sort__item_t item = APR_ARRAY_IDX(prop_targets, i, svn_sort__item_t);
      prop_patch_target_t *prop_target = item.value;
      svn_boolean_t applied_one = FALSE;
      int j;

      for (j = 0; j < prop_target->content->hunks->nelts; j++)
        {
          hunk_info_t *hi;

          svn_pool_clear(iterpool);

          hi = APR_ARRAY_IDX(prop_target->content->hunks, j,
                             hunk_info_t *);
          if (hi->already_applied)
            {
              target->had_prop_already_applied = TRUE;
              continue;
            }
          else if (hi->rejected)
            SVN_ERR(reject_hunk(target, prop_target->content, hi->hunk,
                                prop_target->name,
                                iterpool));
          else
            {
              SVN_ERR(apply_hunk(target, prop_target->content, hi,
                                 prop_target->name,
                                 iterpool));
              applied_one = TRUE;
            }
        }

      if (!applied_one)
        prop_target->skipped = TRUE;

      if (applied_one && prop_target->content->existed)
        {
          /* Copy any remaining lines to target. */
          SVN_ERR(copy_lines_to_target(prop_target->content, 0,
                                       scratch_pool));
          if (! prop_target->content->eof)
            {
              /* We could not copy the entire target property to the
               * temporary stream, and would truncate the target if we
               * copied the temporary stream on top of it. Skip this target. */
              prop_target->skipped = TRUE;
            }
        }
      }

  svn_pool_destroy(iterpool);

  if (!target->is_symlink)
    {
      /* Now close files we don't need any longer to get their contents
       * flushed to disk.
       * But we're not closing the reject file -- it still needed and
       * will be closed later in write_out_rejected_hunks(). */
      if (target->kind_on_disk == svn_node_file)
        SVN_ERR(svn_io_file_close(target->file, scratch_pool));
    }

  SVN_ERR(svn_io_file_close(target->patched_file, scratch_pool));

  *patch_target = target;

  return SVN_NO_ERROR;
}

/* Try to create missing parent directories for TARGET in the working copy
 * rooted at ABS_WC_PATH, and add the parents to version control.
 * If the parents cannot be created, mark the target as skipped.
 *
 * In dry run mode record missing parents in ALREADY_ADDED
 *
 * Use client context CTX. If DRY_RUN is true, do not create missing
 * parents but issue notifications only.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
create_missing_parents(patch_target_t *target,
                       const char *abs_wc_path,
                       svn_client_ctx_t *ctx,
                       svn_boolean_t dry_run,
                       apr_array_header_t *targets_info,
                       apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  apr_array_header_t *components;
  int present_components;
  int i;
  apr_pool_t *iterpool;

  /* Check if we can safely create the target's parent. */
  local_abspath = abs_wc_path;
  components = svn_path_decompose(target->local_relpath, scratch_pool);
  present_components = 0;
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < components->nelts - 1; i++)
    {
      const char *component;
      svn_node_kind_t wc_kind, disk_kind;

      svn_pool_clear(iterpool);

      component = APR_ARRAY_IDX(components, i, const char *);
      local_abspath = svn_dirent_join(local_abspath, component, scratch_pool);

      SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, local_abspath,
                                FALSE, TRUE, iterpool));

      SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, iterpool));

      if (disk_kind == svn_node_file || wc_kind == svn_node_file)
        {
          /* on-disk files and missing files are obstructions */
          target->skipped = TRUE;
          break;
        }
      else if (disk_kind == svn_node_dir)
        {
          if (wc_kind == svn_node_dir)
            present_components++;
          else
            {
              target->skipped = TRUE;
              break;
            }
        }
      else if (wc_kind != svn_node_none)
        {
          /* Node is missing */
          target->skipped = TRUE;
          break;
        }
      else
        {
          /* It's not a file, it's not a dir...
             Let's add a dir */
          break;
        }
    }
  if (! target->skipped)
    {
      local_abspath = abs_wc_path;
      for (i = 0; i < present_components; i++)
        {
          const char *component;
          component = APR_ARRAY_IDX(components, i, const char *);
          local_abspath = svn_dirent_join(local_abspath,
                                          component, scratch_pool);
        }

      if (!dry_run && present_components < components->nelts - 1)
        SVN_ERR(svn_io_make_dir_recursively(
                        svn_dirent_join(
                                   abs_wc_path,
                                   svn_relpath_dirname(target->local_relpath,
                                                       scratch_pool),
                                   scratch_pool),
                        scratch_pool));

      for (i = present_components; i < components->nelts - 1; i++)
        {
          const char *component;
          patch_target_info_t *pti;

          svn_pool_clear(iterpool);

          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          component = APR_ARRAY_IDX(components, i, const char *);
          local_abspath = svn_dirent_join(local_abspath, component,
                                          scratch_pool);

          if (target_is_added(targets_info, local_abspath, iterpool))
            continue;

          pti = apr_pcalloc(targets_info->pool, sizeof(*pti));

          pti->local_abspath = apr_pstrdup(targets_info->pool,
                                           local_abspath);
          pti->added = TRUE;

          APR_ARRAY_PUSH(targets_info, patch_target_info_t *) = pti;

          if (dry_run)
            {
              if (ctx->notify_func2)
                {
                  /* Just do notification. */
                  svn_wc_notify_t *notify;
                  notify = svn_wc_create_notify(local_abspath,
                                                svn_wc_notify_add,
                                                iterpool);
                  notify->kind = svn_node_dir;
                  ctx->notify_func2(ctx->notify_baton2, notify,
                                    iterpool);
                }
            }
          else
            {
              /* Create the missing component and add it
               * to version control. Allow cancellation since we
               * have not modified the working copy yet for this
               * target. */
              SVN_ERR(svn_wc_add_from_disk3(ctx->wc_ctx, local_abspath,
                                            NULL /*props*/,
                                            FALSE /* skip checks */,
                                            ctx->notify_func2, ctx->notify_baton2,
                                            iterpool));
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Install a patched TARGET into the working copy at ABS_WC_PATH.
 * Use client context CTX to retrieve WC_CTX, and possibly doing
 * notifications.
 *
 * Pass on ALREADY_ADDED to allow recording already added ancestors
 * in dry-run mode.
 *
 * If DRY_RUN is TRUE, don't modify the working copy.
 * Do temporary allocations in POOL. */
static svn_error_t *
install_patched_target(patch_target_t *target, const char *abs_wc_path,
                       svn_client_ctx_t *ctx, svn_boolean_t dry_run,
                       apr_array_header_t *targets_info,
                       apr_pool_t *pool)
{
  if (target->deleted)
    {
      if (! dry_run)
        {
          /* Schedule the target for deletion.  Suppress
           * notification, we'll do it manually in a minute
           * because we also need to notify during dry-run.
           * Also suppress cancellation, because we'd rather
           * notify about what we did before aborting. */
          SVN_ERR(svn_wc_delete4(ctx->wc_ctx, target->local_abspath,
                                 FALSE /* keep_local */, FALSE,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 NULL, NULL /* notify */,
                                 pool));
        }
    }
  else
    {
      svn_node_kind_t parent_db_kind;
      if (target->added)
        {
          const char *parent_abspath;

          parent_abspath = svn_dirent_dirname(target->local_abspath,
                                              pool);
          /* If the target's parent directory does not yet exist
           * we need to create it before we can copy the patched
           * result in place. */
          SVN_ERR(svn_wc_read_kind2(&parent_db_kind, ctx->wc_ctx,
                                    parent_abspath, FALSE, FALSE, pool));

          /* We can't add targets under nodes scheduled for delete, so add
             a new directory if needed. */
          if (parent_db_kind == svn_node_dir
              || parent_db_kind == svn_node_file)
            {
              if (parent_db_kind != svn_node_dir)
                target->skipped = TRUE;
              else
                {
                  svn_node_kind_t disk_kind;

                  SVN_ERR(svn_io_check_path(parent_abspath, &disk_kind, pool));
                  if (disk_kind != svn_node_dir)
                    target->skipped = TRUE;
                }
            }
          else
            SVN_ERR(create_missing_parents(target, abs_wc_path, ctx,
                                           dry_run, targets_info, pool));

        }
      else
        {
          svn_node_kind_t wc_kind;

          /* The target should exist */
          SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx,
                                    target->local_abspath,
                                    FALSE, FALSE, pool));

          if (target->kind_on_disk == svn_node_none
              || wc_kind != target->kind_on_disk)
            {
              target->skipped = TRUE;
              if (wc_kind != target->kind_on_disk)
                target->obstructed = TRUE;
            }
        }

      if (! dry_run && ! target->skipped)
        {
          if (target->is_special)
            {
              svn_stream_t *stream;
              svn_stream_t *patched_stream;

              SVN_ERR(svn_stream_open_readonly(&patched_stream,
                                               target->patched_path,
                                               pool, pool));
              SVN_ERR(svn_subst_create_specialfile(&stream,
                                                   target->local_abspath,
                                                   pool, pool));
              if (target->git_symlink_format)
                  SVN_ERR(svn_stream_puts(stream, "link "));
              SVN_ERR(svn_stream_copy3(patched_stream, stream,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       pool));
            }
          else
            {
              svn_boolean_t repair_eol;

              /* Copy the patched file on top of the target file.
               * Always expand keywords in the patched file, but repair EOL
               * only if svn:eol-style dictates a particular style. */
              repair_eol = (target->content->eol_style ==
                              svn_subst_eol_style_fixed ||
                            target->content->eol_style ==
                              svn_subst_eol_style_native);

              SVN_ERR(svn_subst_copy_and_translate4(
                        target->patched_path,
                        target->move_target_abspath
                          ? target->move_target_abspath
                          : target->local_abspath,
                        target->content->eol_str, repair_eol,
                        target->content->keywords,
                        TRUE /* expand */, FALSE /* special */,
                        ctx->cancel_func, ctx->cancel_baton, pool));
            }

          if (target->added)
            {
              /* The target file didn't exist previously,
               * so add it to version control.
               * Suppress notification, we'll do that later (and also
               * during dry-run). Don't allow cancellation because
               * we'd rather notify about what we did before aborting. */
              SVN_ERR(svn_wc_add_from_disk3(ctx->wc_ctx, target->local_abspath,
                                            NULL /*props*/,
                                            FALSE /* skip checks */,
                                            NULL, NULL, pool));
            }

          /* Restore the target's executable bit if necessary. */
          SVN_ERR(svn_io_set_file_executable(target->move_target_abspath
                                               ? target->move_target_abspath
                                               : target->local_abspath,
                                             target->executable,
                                             FALSE, pool));

          if (target->move_target_abspath)
            {
              /* ### Copying the patched content to the move target location,
               * performing the move in meta-data, and removing the file at
               * the move source should be one atomic operation. */

              /* ### Create missing parents. */

              /* Perform the move in meta-data. */
              SVN_ERR(svn_wc__move2(ctx->wc_ctx,
                                    target->local_abspath,
                                    target->move_target_abspath,
                                    TRUE, /* metadata_only */
                                    FALSE, /* allow_mixed_revisions */
                                    ctx->cancel_func, ctx->cancel_baton,
                                    NULL, NULL,
                                    pool));

              /* Delete the patch target's old location from disk. */
              SVN_ERR(svn_io_remove_file2(target->local_abspath, FALSE, pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Write out rejected hunks, if any, to TARGET->REJECT_PATH. If DRY_RUN is
 * TRUE, don't modify the working copy.
 * Do temporary allocations in POOL.
 */
static svn_error_t *
write_out_rejected_hunks(patch_target_t *target,
                         const char *root_abspath,
                         svn_boolean_t dry_run,
                         apr_pool_t *scratch_pool)
{
  if (! dry_run && (target->had_rejects || target->had_prop_rejects))
    {
      /* Write out rejected hunks, if any. */
      apr_file_t *reject_file;
      svn_error_t *err;

      err = svn_io_open_uniquely_named(&reject_file, NULL,
                                       svn_dirent_dirname(target->local_abspath,
                                                          scratch_pool),
                                       svn_dirent_basename(
                                         target->local_abspath,
                                         NULL),
                                       ".svnpatch.rej",
                                       svn_io_file_del_none,
                                       scratch_pool, scratch_pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          /* The hunk applies to a file in a directory which does not exist.
           * Put the reject file into the working copy root instead. */
          svn_error_clear(err);
          SVN_ERR(svn_io_open_uniquely_named(&reject_file, NULL,
                                             root_abspath,
                                             svn_dirent_basename(
                                               target->local_abspath,
                                               NULL),
                                             ".svnpatch.rej",
                                             svn_io_file_del_none,
                                             scratch_pool, scratch_pool));
        }
      else
        SVN_ERR(err);

      SVN_ERR(svn_stream_reset(target->reject_stream));

      /* svn_stream_copy3() closes the files for us */
      SVN_ERR(svn_stream_copy3(target->reject_stream,
                                  svn_stream_from_aprfile2(reject_file, FALSE,
                                                           scratch_pool),
                                  NULL, NULL, scratch_pool));
      /* ### TODO mark file as conflicted. */
    }
  else
    SVN_ERR(svn_stream_close(target->reject_stream));

  return SVN_NO_ERROR;
}

/* Install the patched properties for TARGET. Use client context CTX to
 * retrieve WC_CTX. If DRY_RUN is TRUE, don't modify the working copy.
 * Do temporary allocations in SCRATCH_POOL. */
static svn_error_t *
install_patched_prop_targets(patch_target_t *target,
                             svn_client_ctx_t *ctx, svn_boolean_t dry_run,
                             apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  const char *local_abspath;

  /* Apply properties to a move target if there is one */
  if (target->move_target_abspath)
    local_abspath = target->move_target_abspath;
  else
    local_abspath = target->local_abspath;

  iterpool = svn_pool_create(scratch_pool);

  for (hi = apr_hash_first(scratch_pool, target->prop_targets);
       hi;
       hi = apr_hash_next(hi))
    {
      prop_patch_target_t *prop_target = apr_hash_this_val(hi);
      const svn_string_t *prop_val;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (prop_target->skipped)
        continue;

      /* For a deleted prop we only set the value to NULL. */
      if (prop_target->operation == svn_diff_op_deleted)
        {
          if (! dry_run)
            SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath,
                                     prop_target->name, NULL, svn_depth_empty,
                                     TRUE /* skip_checks */,
                                     NULL /* changelist_filter */,
                                     NULL, NULL /* cancellation */,
                                     NULL, NULL /* notification */,
                                     iterpool));
          continue;
        }

      /* Attempt to set the property, and reject all hunks if this
         fails.  If the property had a non-empty value, but now has
         an empty one, we'll just delete the property altogether.  */
      if (prop_target->value && prop_target->value->len
          && prop_target->patched_value && !prop_target->patched_value->len)
        prop_val = NULL;
      else
        prop_val = svn_stringbuf__morph_into_string(prop_target->patched_value);

      if (dry_run)
        {
          const svn_string_t *canon_propval;

          err = svn_wc_canonicalize_svn_prop(&canon_propval,
                                             prop_target->name,
                                             prop_val, local_abspath,
                                             target->db_kind,
                                             TRUE, /* ### Skipping checks */
                                             NULL, NULL,
                                             iterpool);
        }
      else
        {
          err = svn_wc_prop_set4(ctx->wc_ctx, local_abspath,
                                 prop_target->name, prop_val, svn_depth_empty,
                                 TRUE /* skip_checks */,
                                 NULL /* changelist_filter */,
                                 NULL, NULL /* cancellation */,
                                 NULL, NULL /* notification */,
                                 iterpool);
        }

      if (err)
        {
          /* ### The errors which svn_wc_canonicalize_svn_prop() will
           * ### return aren't documented. */
          if (err->apr_err == SVN_ERR_ILLEGAL_TARGET ||
              err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND ||
              err->apr_err == SVN_ERR_IO_UNKNOWN_EOL ||
              err->apr_err == SVN_ERR_BAD_MIME_TYPE ||
              err->apr_err == SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION)
            {
              int i;

              svn_error_clear(err);

              for (i = 0; i < prop_target->content->hunks->nelts; i++)
                {
                  hunk_info_t *hunk_info;

                  hunk_info = APR_ARRAY_IDX(prop_target->content->hunks,
                                            i, hunk_info_t *);
                  hunk_info->rejected = TRUE;
                  SVN_ERR(reject_hunk(target, prop_target->content,
                                      hunk_info->hunk, prop_target->name,
                                      iterpool));
                }
            }
          else
            return svn_error_trace(err);
        }

    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Baton for can_delete_callback */
struct can_delete_baton_t
{
  svn_boolean_t must_keep;
  const apr_array_header_t *targets_info;
  const char *local_abspath;
};

/* Implements svn_wc_status_func4_t. */
static svn_error_t *
can_delete_callback(void *baton,
                    const char *abspath,
                    const svn_wc_status3_t *status,
                    apr_pool_t *pool)
{
  struct can_delete_baton_t *cb = baton;
  int i;

  switch(status->node_status)
    {
      case svn_wc_status_none:
      case svn_wc_status_deleted:
        return SVN_NO_ERROR;

      default:
        if (! strcmp(cb->local_abspath, abspath))
          return SVN_NO_ERROR; /* Only interested in descendants */

        for (i = 0; i < cb->targets_info->nelts; i++)
          {
            const patch_target_info_t *target_info =
               APR_ARRAY_IDX(cb->targets_info, i, const patch_target_info_t *);

            if (! strcmp(target_info->local_abspath, abspath))
              {
                if (target_info->deleted)
                  return SVN_NO_ERROR;

                break; /* Cease invocation; must keep */
              }
          }

        cb->must_keep = TRUE;

        return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
    }
}

static svn_error_t *
check_ancestor_delete(const char *deleted_target,
                      apr_array_header_t *targets_info,
                      const char *apply_root,
                      svn_boolean_t dry_run,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  struct can_delete_baton_t cb;
  svn_error_t *err;
  apr_array_header_t *ignores;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  const char *dir_abspath = svn_dirent_dirname(deleted_target, scratch_pool);

  SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, scratch_pool));

  while (svn_dirent_is_child(apply_root, dir_abspath, iterpool))
    {
      svn_pool_clear(iterpool);

      cb.local_abspath = dir_abspath;
      cb.must_keep = FALSE;
      cb.targets_info = targets_info;

      err = svn_wc_walk_status(ctx->wc_ctx, dir_abspath, svn_depth_infinity,
                               TRUE, FALSE, FALSE, ignores,
                               can_delete_callback, &cb,
                               ctx->cancel_func, ctx->cancel_baton,
                               iterpool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_CEASE_INVOCATION)
            return svn_error_trace(err);

          svn_error_clear(err);
        }

      if (cb.must_keep)
      {
        break;
      }

      if (! dry_run)
        {
          SVN_ERR(svn_wc_delete4(ctx->wc_ctx, dir_abspath, FALSE, FALSE,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 NULL, NULL,
                                 scratch_pool));
        }

      {
        patch_target_info_t *pti = apr_pcalloc(result_pool, sizeof(*pti));

        pti->local_abspath = apr_pstrdup(result_pool, dir_abspath);
        pti->deleted = TRUE;

        APR_ARRAY_PUSH(targets_info, patch_target_info_t *) = pti;
      }


      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify(dir_abspath, svn_wc_notify_delete,
                                    iterpool);
          notify->kind = svn_node_dir;

          ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
        }

      /* And check if we must also delete the parent */
      dir_abspath = svn_dirent_dirname(dir_abspath, scratch_pool);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This function is the main entry point into the patch code. */
static svn_error_t *
apply_patches(/* The path to the patch file. */
              const char *patch_abspath,
              /* The abspath to the working copy the patch should be applied to. */
              const char *root_abspath,
              /* Indicates whether we're doing a dry run. */
              svn_boolean_t dry_run,
              /* Number of leading components to strip from patch target paths. */
              int strip_count,
              /* Whether to apply the patch in reverse. */
              svn_boolean_t reverse,
              /* Whether to ignore whitespace when matching context lines. */
              svn_boolean_t ignore_whitespace,
              /* As in svn_client_patch(). */
              svn_boolean_t remove_tempfiles,
              /* As in svn_client_patch(). */
              svn_client_patch_func_t patch_func,
              void *patch_baton,
              /* The client context. */
              svn_client_ctx_t *ctx,
              apr_pool_t *scratch_pool)
{
  svn_patch_t *patch;
  apr_pool_t *iterpool;
  svn_patch_file_t *patch_file;
  apr_array_header_t *targets_info;

  /* Try to open the patch file. */
  SVN_ERR(svn_diff_open_patch_file(&patch_file, patch_abspath, scratch_pool));

  /* Apply patches. */
  targets_info = apr_array_make(scratch_pool, 0,
                                sizeof(patch_target_info_t *));
  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      SVN_ERR(svn_diff_parse_next_patch(&patch, patch_file,
                                        reverse, ignore_whitespace,
                                        iterpool, iterpool));
      if (patch)
        {
          patch_target_t *target;
          svn_boolean_t filtered = FALSE;

          SVN_ERR(apply_one_patch(&target, patch, root_abspath,
                                  ctx->wc_ctx, strip_count,
                                  ignore_whitespace, remove_tempfiles,
                                  targets_info,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  iterpool, iterpool));

          if (!target->skipped && patch_func)
            {
              SVN_ERR(patch_func(patch_baton, &filtered,
                                 target->canon_path_from_patchfile,
                                 target->patched_path, target->reject_path,
                                 iterpool));
            }

          if (! filtered)
            {
              /* Save info we'll still need when we're done patching. */
              patch_target_info_t *target_info =
                apr_pcalloc(scratch_pool, sizeof(patch_target_info_t));
              target_info->local_abspath = apr_pstrdup(scratch_pool,
                                                       target->local_abspath);
              target_info->deleted = target->deleted;
              target_info->added = target->added;

              if (! target->skipped)
                {
                  if (target->has_text_changes
                      || target->added
                      || target->move_target_abspath
                      || target->deleted)
                    SVN_ERR(install_patched_target(target, root_abspath,
                                                   ctx, dry_run,
                                                   targets_info, iterpool));

                  if (target->has_prop_changes && (!target->deleted))
                    SVN_ERR(install_patched_prop_targets(target, ctx,
                                                         dry_run, iterpool));

                  SVN_ERR(write_out_rejected_hunks(target, root_abspath,
                                                   dry_run, iterpool));

                  APR_ARRAY_PUSH(targets_info,
                                 patch_target_info_t *) = target_info;
              }
              SVN_ERR(send_patch_notification(target, ctx, iterpool));

              if (target->deleted && !target->skipped)
                {
                  SVN_ERR(check_ancestor_delete(target_info->local_abspath,
                                                targets_info, root_abspath,
                                                dry_run, ctx,
                                                scratch_pool, iterpool));
                }
            }
        }
    }
  while (patch);

  SVN_ERR(svn_diff_close_patch_file(patch_file, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_patch(const char *patch_abspath,
                 const char *wc_dir_abspath,
                 svn_boolean_t dry_run,
                 int strip_count,
                 svn_boolean_t reverse,
                 svn_boolean_t ignore_whitespace,
                 svn_boolean_t remove_tempfiles,
                 svn_client_patch_func_t patch_func,
                 void *patch_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;

  if (strip_count < 0)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("strip count must be positive"));

  if (svn_path_is_url(wc_dir_abspath))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"),
                             svn_dirent_local_style(wc_dir_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_io_check_path(patch_abspath, &kind, scratch_pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' does not exist"),
                             svn_dirent_local_style(patch_abspath,
                                                    scratch_pool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a file"),
                             svn_dirent_local_style(patch_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_io_check_path(wc_dir_abspath, &kind, scratch_pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' does not exist"),
                             svn_dirent_local_style(wc_dir_abspath,
                                                    scratch_pool));
  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a directory"),
                             svn_dirent_local_style(wc_dir_abspath,
                                                    scratch_pool));

  SVN_WC__CALL_WITH_WRITE_LOCK(
    apply_patches(patch_abspath, wc_dir_abspath, dry_run, strip_count,
                  reverse, ignore_whitespace, remove_tempfiles,
                  patch_func, patch_baton, ctx, scratch_pool),
    ctx->wc_ctx, wc_dir_abspath, FALSE /* lock_anchor */, scratch_pool);
  return SVN_NO_ERROR;
}
