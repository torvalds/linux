/* low_level.c --- low level r/w access to fs_fs file structures
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

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "private/svn_sorts_private.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_fspath.h"

#include "../libsvn_fs/fs-loader.h"

#include "low_level.h"

/* Headers used to describe node-revision in the revision file. */
#define HEADER_ID          "id"
#define HEADER_TYPE        "type"
#define HEADER_COUNT       "count"
#define HEADER_PROPS       "props"
#define HEADER_TEXT        "text"
#define HEADER_CPATH       "cpath"
#define HEADER_PRED        "pred"
#define HEADER_COPYFROM    "copyfrom"
#define HEADER_COPYROOT    "copyroot"
#define HEADER_FRESHTXNRT  "is-fresh-txn-root"
#define HEADER_MINFO_HERE  "minfo-here"
#define HEADER_MINFO_CNT   "minfo-cnt"

/* Kinds that a change can be. */
#define ACTION_MODIFY      "modify"
#define ACTION_ADD         "add"
#define ACTION_DELETE      "delete"
#define ACTION_REPLACE     "replace"
#define ACTION_RESET       "reset"

/* True and False flags. */
#define FLAG_TRUE          "true"
#define FLAG_FALSE         "false"

/* Kinds of representation. */
#define REP_PLAIN          "PLAIN"
#define REP_DELTA          "DELTA"

/* An arbitrary maximum path length, so clients can't run us out of memory
 * by giving us arbitrarily large paths. */
#define FSFS_MAX_PATH_LEN 4096

/* The 256 is an arbitrary size large enough to hold the node id and the
 * various flags. */
#define MAX_CHANGE_LINE_LEN FSFS_MAX_PATH_LEN + 256

/* Convert the C string in *TEXT to a revision number and return it in *REV.
 * Overflows, negative values other than -1 and terminating characters other
 * than 0x20 or 0x0 will cause an error.  Set *TEXT to the first char after
 * the initial separator or to EOS.
 */
static svn_error_t *
parse_revnum(svn_revnum_t *rev,
             const char **text)
{
  const char *string = *text;
  if ((string[0] == '-') && (string[1] == '1'))
    {
      *rev = SVN_INVALID_REVNUM;
      string += 2;
    }
  else
    {
      SVN_ERR(svn_revnum_parse(rev, string, &string));
    }

  if (*string == ' ')
    ++string;
  else if (*string != '\0')
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid character in revision number"));

  *text = string;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__parse_revision_trailer(apr_off_t *root_offset,
                                  apr_off_t *changes_offset,
                                  svn_stringbuf_t *trailer,
                                  svn_revnum_t rev)
{
  int i, num_bytes;
  const char *str;

  /* This cast should be safe since the maximum amount read, 64, will
     never be bigger than the size of an int. */
  num_bytes = (int) trailer->len;

  /* The last byte should be a newline. */
  if (trailer->len == 0 || trailer->data[trailer->len - 1] != '\n')
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Revision file (r%ld) lacks trailing newline"),
                               rev);
    }

  /* Look for the next previous newline. */
  for (i = num_bytes - 2; i >= 0; i--)
    {
      if (trailer->data[i] == '\n')
        break;
    }

  if (i < 0)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Final line in revision file (r%ld) longer "
                                 "than 64 characters"),
                               rev);
    }

  i++;
  str = &trailer->data[i];

  /* find the next space */
  for ( ; i < (num_bytes - 2) ; i++)
    if (trailer->data[i] == ' ')
      break;

  if (i == (num_bytes - 2))
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Final line in revision file r%ld missing space"),
                             rev);

  if (root_offset)
    {
      apr_int64_t val;

      trailer->data[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *root_offset = (apr_off_t)val;
    }

  i++;
  str = &trailer->data[i];

  /* find the next newline */
  for ( ; i < num_bytes; i++)
    if (trailer->data[i] == '\n')
      break;

  if (changes_offset)
    {
      apr_int64_t val;

      trailer->data[i] = '\0';
      SVN_ERR(svn_cstring_atoi64(&val, str));
      *changes_offset = (apr_off_t)val;
    }

  return SVN_NO_ERROR;
}

svn_stringbuf_t *
svn_fs_fs__unparse_revision_trailer(apr_off_t root_offset,
                                    apr_off_t changes_offset,
                                    apr_pool_t *result_pool)
{
  return svn_stringbuf_createf(result_pool,
                               "%" APR_OFF_T_FMT " %" APR_OFF_T_FMT "\n",
                               root_offset,
                               changes_offset);
}

/* If ERR is not NULL, wrap it MESSAGE.  The latter must have an %ld
 * format parameter that will be filled with REV. */
static svn_error_t *
wrap_footer_error(svn_error_t *err,
                  const char *message,
                  svn_revnum_t rev)
{
  if (err)
    return svn_error_quick_wrapf(err, message, rev);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__parse_footer(apr_off_t *l2p_offset,
                        svn_checksum_t **l2p_checksum,
                        apr_off_t *p2l_offset,
                        svn_checksum_t **p2l_checksum,
                        svn_stringbuf_t *footer,
                        svn_revnum_t rev,
                        apr_off_t footer_offset,
                        apr_pool_t *result_pool)
{
  apr_int64_t val;
  char *last_str = footer->data;

  /* Get the L2P offset. */
  const char *str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid r%ld footer", rev);

  SVN_ERR(wrap_footer_error(svn_cstring_strtoi64(&val, str, 0,
                                                 footer_offset - 1, 10),
                            "Invalid L2P offset in r%ld footer",
                            rev));
  *l2p_offset = (apr_off_t)val;

  /* Get the L2P checksum. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid r%ld footer", rev);

  SVN_ERR(svn_checksum_parse_hex(l2p_checksum, svn_checksum_md5, str,
                                 result_pool));

  /* Get the P2L offset. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid r%ld footer", rev);

  SVN_ERR(wrap_footer_error(svn_cstring_strtoi64(&val, str, 0,
                                                 footer_offset - 1, 10),
                            "Invalid P2L offset in r%ld footer",
                            rev));
  *p2l_offset = (apr_off_t)val;

  /* The P2L indes follows the L2P index */
  if (*p2l_offset <= *l2p_offset)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "P2L offset %s must be larger than L2P offset %s"
                             " in r%ld footer",
                             apr_psprintf(result_pool,
                                          "%" APR_UINT64_T_HEX_FMT,
                                          (apr_uint64_t)*p2l_offset),
                             apr_psprintf(result_pool,
                                          "%" APR_UINT64_T_HEX_FMT,
                                          (apr_uint64_t)*l2p_offset),
                             rev);

  /* Get the P2L checksum. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             "Invalid r%ld footer", rev);

  SVN_ERR(svn_checksum_parse_hex(p2l_checksum, svn_checksum_md5, str,
                                 result_pool));

  return SVN_NO_ERROR;
}

svn_stringbuf_t *
svn_fs_fs__unparse_footer(apr_off_t l2p_offset,
                          svn_checksum_t *l2p_checksum,
                          apr_off_t p2l_offset,
                          svn_checksum_t *p2l_checksum,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_stringbuf_createf(result_pool,
                               "%" APR_OFF_T_FMT " %s %" APR_OFF_T_FMT " %s",
                               l2p_offset,
                               svn_checksum_to_cstring(l2p_checksum,
                                                       scratch_pool),
                               p2l_offset,
                               svn_checksum_to_cstring(p2l_checksum,
                                                       scratch_pool));
}

/* Read the next entry in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change(change_t **change_p,
            svn_stream_t *stream,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof = TRUE;
  change_t *change;
  char *str, *last_str, *kind_str;
  svn_fs_path_change2_t *info;

  /* Default return value. */
  *change_p = NULL;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));

  /* Check for a blank line. */
  if (eof || (line->len == 0))
    return SVN_NO_ERROR;

  change = apr_pcalloc(result_pool, sizeof(*change));
  info = &change->info;
  last_str = line->data;

  /* Get the node-id of the change. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  SVN_ERR(svn_fs_fs__id_parse(&info->node_rev_id, str, result_pool));
  if (info->node_rev_id == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Get the change type. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Don't bother to check the format number before looking for
   * node-kinds: just read them if you find them. */
  info->node_kind = svn_node_unknown;
  kind_str = strchr(str, '-');
  if (kind_str)
    {
      /* Cap off the end of "str" (the action). */
      *kind_str = '\0';
      kind_str++;
      if (strcmp(kind_str, SVN_FS_FS__KIND_FILE) == 0)
        info->node_kind = svn_node_file;
      else if (strcmp(kind_str, SVN_FS_FS__KIND_DIR) == 0)
        info->node_kind = svn_node_dir;
      else
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
    }

  if (strcmp(str, ACTION_MODIFY) == 0)
    {
      info->change_kind = svn_fs_path_change_modify;
    }
  else if (strcmp(str, ACTION_ADD) == 0)
    {
      info->change_kind = svn_fs_path_change_add;
    }
  else if (strcmp(str, ACTION_DELETE) == 0)
    {
      info->change_kind = svn_fs_path_change_delete;
    }
  else if (strcmp(str, ACTION_REPLACE) == 0)
    {
      info->change_kind = svn_fs_path_change_replace;
    }
  else if (strcmp(str, ACTION_RESET) == 0)
    {
      info->change_kind = svn_fs_path_change_reset;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid change kind in rev file"));
    }

  /* Get the text-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      info->text_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      info->text_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid text-mod flag in rev-file"));
    }

  /* Get the prop-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      info->prop_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      info->prop_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid prop-mod flag in rev-file"));
    }

  /* Get the mergeinfo-mod flag if given.  Otherwise, the next thing
     is the path starting with a slash.  Also, we must initialize the
     flag explicitly because 0 is not valid for a svn_tristate_t. */
  info->mergeinfo_mod = svn_tristate_unknown;
  if (*last_str != '/')
    {
      str = svn_cstring_tokenize(" ", &last_str);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));

      if (strcmp(str, FLAG_TRUE) == 0)
        {
          info->mergeinfo_mod = svn_tristate_true;
        }
      else if (strcmp(str, FLAG_FALSE) == 0)
        {
          info->mergeinfo_mod = svn_tristate_false;
        }
      else
        {
          return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid mergeinfo-mod flag in rev-file"));
        }
    }

  /* Get the changed path. */
  if (!svn_fspath__is_canonical(last_str))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid path in changes line"));

  change->path.len = strlen(last_str);
  change->path.data = apr_pstrdup(result_pool, last_str);

  /* Read the next line, the copyfrom line. */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  info->copyfrom_known = TRUE;
  if (eof || line->len == 0)
    {
      info->copyfrom_rev = SVN_INVALID_REVNUM;
      info->copyfrom_path = NULL;
    }
  else
    {
      last_str = line->data;
      SVN_ERR(parse_revnum(&info->copyfrom_rev, (const char **)&last_str));

      if (!svn_fspath__is_canonical(last_str))
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid copy-from path in changes line"));

      info->copyfrom_path = apr_pstrdup(result_pool, last_str);
    }

  *change_p = change;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_changes(apr_array_header_t **changes,
                        svn_stream_t *stream,
                        int max_count,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;

  /* Pre-allocate enough room for most change lists.
     (will be auto-expanded as necessary).

     Chose the default to just below 2^N such that the doubling reallocs
     will request roughly 2^M bytes from the OS without exceeding the
     respective two-power by just a few bytes (leaves room array and APR
     node overhead for large enough M).
   */
  *changes = apr_array_make(result_pool, 63, sizeof(change_t *));

  iterpool = svn_pool_create(scratch_pool);
  for (; max_count > 0; --max_count)
    {
      change_t *change;
      svn_pool_clear(iterpool);
      SVN_ERR(read_change(&change, stream, result_pool, iterpool));
      if (!change)
        break;
 
      APR_ARRAY_PUSH(*changes, change_t*) = change;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_changes_incrementally(svn_stream_t *stream,
                                      svn_fs_fs__change_receiver_t
                                        change_receiver,
                                      void *change_receiver_baton,
                                      apr_pool_t *scratch_pool)
{
  change_t *change;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);
  do
    {
      svn_pool_clear(iterpool);

      SVN_ERR(read_change(&change, stream, iterpool, iterpool));
      if (change)
        SVN_ERR(change_receiver(change_receiver_baton, change, iterpool));
    }
  while (change);
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Write a single change entry, path PATH, change CHANGE, to STREAM.

   Only include the node kind field if INCLUDE_NODE_KIND is true.  Only
   include the mergeinfo-mod field if INCLUDE_MERGEINFO_MODS is true.
   All temporary allocations are in SCRATCH_POOL. */
static svn_error_t *
write_change_entry(svn_stream_t *stream,
                   const char *path,
                   svn_fs_path_change2_t *change,
                   svn_boolean_t include_node_kind,
                   svn_boolean_t include_mergeinfo_mods,
                   apr_pool_t *scratch_pool)
{
  const char *idstr;
  const char *change_string = NULL;
  const char *kind_string = "";
  const char *mergeinfo_string = "";
  svn_stringbuf_t *buf;
  apr_size_t len;

  switch (change->change_kind)
    {
    case svn_fs_path_change_modify:
      change_string = ACTION_MODIFY;
      break;
    case svn_fs_path_change_add:
      change_string = ACTION_ADD;
      break;
    case svn_fs_path_change_delete:
      change_string = ACTION_DELETE;
      break;
    case svn_fs_path_change_replace:
      change_string = ACTION_REPLACE;
      break;
    case svn_fs_path_change_reset:
      change_string = ACTION_RESET;
      break;
    default:
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Invalid change type %d"),
                               change->change_kind);
    }

  if (change->node_rev_id)
    idstr = svn_fs_fs__id_unparse(change->node_rev_id, scratch_pool)->data;
  else
    idstr = ACTION_RESET;

  if (include_node_kind)
    {
      SVN_ERR_ASSERT(change->node_kind == svn_node_dir
                     || change->node_kind == svn_node_file);
      kind_string = apr_psprintf(scratch_pool, "-%s",
                                 change->node_kind == svn_node_dir
                                 ? SVN_FS_FS__KIND_DIR
                                  : SVN_FS_FS__KIND_FILE);
    }

  if (include_mergeinfo_mods && change->mergeinfo_mod != svn_tristate_unknown)
    mergeinfo_string = apr_psprintf(scratch_pool, " %s",
                                    change->mergeinfo_mod == svn_tristate_true
                                      ? FLAG_TRUE
                                      : FLAG_FALSE);

  buf = svn_stringbuf_createf(scratch_pool, "%s %s%s %s %s%s %s\n",
                              idstr, change_string, kind_string,
                              change->text_mod ? FLAG_TRUE : FLAG_FALSE,
                              change->prop_mod ? FLAG_TRUE : FLAG_FALSE,
                              mergeinfo_string,
                              path);

  if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
    {
      svn_stringbuf_appendcstr(buf, apr_psprintf(scratch_pool, "%ld %s",
                                                 change->copyfrom_rev,
                                                 change->copyfrom_path));
    }

   svn_stringbuf_appendbyte(buf, '\n');

   /* Write all change info in one write call. */
   len = buf->len;
   return svn_error_trace(svn_stream_write(stream, buf->data, &len));
}

svn_error_t *
svn_fs_fs__write_changes(svn_stream_t *stream,
                         svn_fs_t *fs,
                         apr_hash_t *changes,
                         svn_boolean_t terminate_list,
                         apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_boolean_t include_node_kinds =
      ffd->format >= SVN_FS_FS__MIN_KIND_IN_CHANGED_FORMAT;
  svn_boolean_t include_mergeinfo_mods =
      ffd->format >= SVN_FS_FS__MIN_MERGEINFO_IN_CHANGED_FORMAT;
  apr_array_header_t *sorted_changed_paths;
  int i;

  /* For the sake of the repository administrator sort the changes so
     that the final file is deterministic and repeatable, however the
     rest of the FSFS code doesn't require any particular order here.

     Also, this sorting is only effective in writing all entries with
     a single call as write_final_changed_path_info() does.  For the
     list being written incrementally during transaction, we actually
     *must not* change the order of entries from different calls.
   */
  sorted_changed_paths = svn_sort__hash(changes,
                                        svn_sort_compare_items_lexically,
                                        scratch_pool);

  /* Write all items to disk in the new order. */
  for (i = 0; i < sorted_changed_paths->nelts; ++i)
    {
      svn_fs_path_change2_t *change;
      const char *path;

      svn_pool_clear(iterpool);

      change = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).value;
      path = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).key;

      /* Write out the new entry into the final rev-file. */
      SVN_ERR(write_change_entry(stream, path, change, include_node_kinds,
                                 include_mergeinfo_mods, iterpool));
    }

  if (terminate_list)
    svn_stream_puts(stream, "\n");

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Given a revision file FILE that has been pre-positioned at the
   beginning of a Node-Rev header block, read in that header block and
   store it in the apr_hash_t HEADERS.  All allocations will be from
   RESULT_POOL. */
static svn_error_t *
read_header_block(apr_hash_t **headers,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool)
{
  *headers = svn_hash__make(result_pool);

  while (1)
    {
      svn_stringbuf_t *header_str;
      const char *name, *value;
      apr_size_t i = 0;
      apr_size_t name_len;
      svn_boolean_t eof;

      SVN_ERR(svn_stream_readline(stream, &header_str, "\n", &eof,
                                  result_pool));

      if (eof || header_str->len == 0)
        break; /* end of header block */

      while (header_str->data[i] != ':')
        {
          if (header_str->data[i] == '\0')
            return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                     _("Found malformed header '%s' in "
                                       "revision file"),
                                     header_str->data);
          i++;
        }

      /* Create a 'name' string and point to it. */
      header_str->data[i] = '\0';
      name = header_str->data;
      name_len = i;

      /* Check if we have enough data to parse. */
      if (i + 2 > header_str->len)
        {
          /* Restore the original line for the error. */
          header_str->data[i] = ':';
          return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                   _("Found malformed header '%s' in "
                                     "revision file"),
                                   header_str->data);
        }

      /* Skip over the NULL byte and the space following it. */
      i += 2;

      value = header_str->data + i;

      /* header_str is safely in our pool, so we can use bits of it as
         key and value. */
      apr_hash_set(*headers, name, name_len, value);
    }

  return SVN_NO_ERROR;
}

/* ### Ouch!  The implementation of this function currently modifies
   ### the input string when tokenizing it (so the input cannot be
   ### used after that). */
svn_error_t *
svn_fs_fs__parse_representation(representation_t **rep_p,
                                svn_stringbuf_t *text,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  representation_t *rep;
  char *str;
  apr_int64_t val;
  char *string = text->data;
  svn_checksum_t *checksum;
  const char *end;

  rep = apr_pcalloc(result_pool, sizeof(*rep));
  *rep_p = rep;

  SVN_ERR(parse_revnum(&rep->revision, (const char **)&string));

  /* initialize transaction info (never stored) */
  svn_fs_fs__id_txn_reset(&rep->txn_id);

  /* while in transactions, it is legal to simply write "-1" */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    {
      if (rep->revision == SVN_INVALID_REVNUM)
        return SVN_NO_ERROR;

      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Malformed text representation offset line in node-rev"));
    }

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->item_index = (apr_uint64_t)val;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->size = (svn_filesize_t)val;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->expanded_size = (svn_filesize_t)val;

  /* Read in the MD5 hash. */
  str = svn_cstring_tokenize(" ", &string);
  if ((str == NULL) || (strlen(str) != (APR_MD5_DIGESTSIZE * 2)))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_md5, str,
                                 scratch_pool));

  /* If STR is a all-zero checksum, CHECKSUM will be NULL and REP already
     contains the correct value. */
  if (checksum)
    memcpy(rep->md5_digest, checksum->digest, sizeof(rep->md5_digest));

  /* The remaining fields are only used for formats >= 4, so check that. */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return SVN_NO_ERROR;

  /* Is the SHA1 hash present? */
  if (str[0] == '-' && str[1] == 0)
    {
      checksum = NULL;
    }
  else
    {
      /* Read the SHA1 hash. */
      if (strlen(str) != (APR_SHA1_DIGESTSIZE * 2))
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed text representation offset line in node-rev"));

      SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, str,
                                     scratch_pool));
    }

  /* We do have a valid SHA1 but it might be all 0.
     We cannot be sure where that came from (Alas! legacy), so let's not
     claim we know the SHA1 in that case. */
  rep->has_sha1 = checksum != NULL;

  /* If STR is a all-zero checksum, CHECKSUM will be NULL and REP already
     contains the correct value. */
  if (checksum)
    memcpy(rep->sha1_digest, checksum->digest, sizeof(rep->sha1_digest));

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  /* Is the uniquifier present? */
  if (str[0] == '-' && str[1] == 0)
    {
      end = string;
    }
  else
    {
      char *substring = str;

      /* Read the uniquifier. */
      str = svn_cstring_tokenize("/", &substring);
      if (str == NULL)
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed text representation offset line in node-rev"));

      SVN_ERR(svn_fs_fs__id_txn_parse(&rep->uniquifier.noderev_txn_id, str));

      str = svn_cstring_tokenize(" ", &substring);
      if (str == NULL || *str != '_')
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Malformed text representation offset line in node-rev"));

      ++str;
      rep->uniquifier.number = svn__base36toui64(&end, str);
    }

  if (*end)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  return SVN_NO_ERROR;
}

/* Wrap svn_fs_fs__parse_representation(), extracting its TXN_ID from our
   NODEREV_ID, and adding an error message. */
static svn_error_t *
read_rep_offsets(representation_t **rep_p,
                 char *string,
                 const svn_fs_id_t *noderev_id,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err
    = svn_fs_fs__parse_representation(rep_p,
                                      svn_stringbuf_create_wrap(string,
                                                                scratch_pool),
                                      result_pool,
                                      scratch_pool);
  if (err)
    {
      const svn_string_t *id_unparsed;
      const char *where;

      id_unparsed = svn_fs_fs__id_unparse(noderev_id, scratch_pool);
      where = apr_psprintf(scratch_pool,
                           _("While reading representation offsets "
                             "for node-revision '%s':"),
                           noderev_id ? id_unparsed->data : "(null)");

      return svn_error_quick_wrap(err, where);
    }

  if ((*rep_p)->revision == SVN_INVALID_REVNUM)
    if (noderev_id)
      (*rep_p)->txn_id = *svn_fs_fs__id_txn_id(noderev_id);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__read_noderev(node_revision_t **noderev_p,
                        svn_stream_t *stream,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  apr_hash_t *headers;
  node_revision_t *noderev;
  char *value;
  const char *noderev_id;

  SVN_ERR(read_header_block(&headers, stream, scratch_pool));

  noderev = apr_pcalloc(result_pool, sizeof(*noderev));

  /* Read the node-rev id. */
  value = svn_hash_gets(headers, HEADER_ID);
  if (value == NULL)
      /* ### More information: filename/offset coordinates */
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Missing id field in node-rev"));

  SVN_ERR(svn_stream_close(stream));

  SVN_ERR(svn_fs_fs__id_parse(&noderev->id, value, result_pool));
  noderev_id = value; /* for error messages later */

  /* Read the type. */
  value = svn_hash_gets(headers, HEADER_TYPE);

  if ((value == NULL) ||
      (   strcmp(value, SVN_FS_FS__KIND_FILE)
       && strcmp(value, SVN_FS_FS__KIND_DIR)))
    /* ### s/kind/type/ */
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Missing kind field in node-rev '%s'"),
                             noderev_id);

  noderev->kind = (strcmp(value, SVN_FS_FS__KIND_FILE) == 0)
                ? svn_node_file
                : svn_node_dir;

  /* Read the 'count' field. */
  value = svn_hash_gets(headers, HEADER_COUNT);
  if (value)
    SVN_ERR(svn_cstring_atoi(&noderev->predecessor_count, value));
  else
    noderev->predecessor_count = 0;

  /* Get the properties location. */
  value = svn_hash_gets(headers, HEADER_PROPS);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->prop_rep, value,
                               noderev->id, result_pool, scratch_pool));
    }

  /* Get the data location. */
  value = svn_hash_gets(headers, HEADER_TEXT);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->data_rep, value,
                               noderev->id, result_pool, scratch_pool));
    }

  /* Get the created path. */
  value = svn_hash_gets(headers, HEADER_CPATH);
  if (value == NULL)
    {
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Missing cpath field in node-rev '%s'"),
                               noderev_id);
    }
  else
    {
      if (!svn_fspath__is_canonical(value))
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                            _("Non-canonical cpath field in node-rev '%s'"),
                            noderev_id);

      noderev->created_path = apr_pstrdup(result_pool, value);
    }

  /* Get the predecessor ID. */
  value = svn_hash_gets(headers, HEADER_PRED);
  if (value)
    SVN_ERR(svn_fs_fs__id_parse(&noderev->predecessor_id, value,
                                result_pool));

  /* Get the copyroot. */
  value = svn_hash_gets(headers, HEADER_COPYROOT);
  if (value == NULL)
    {
      noderev->copyroot_path = apr_pstrdup(result_pool, noderev->created_path);
      noderev->copyroot_rev = svn_fs_fs__id_rev(noderev->id);
    }
  else
    {
      SVN_ERR(parse_revnum(&noderev->copyroot_rev, (const char **)&value));

      if (!svn_fspath__is_canonical(value))
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyroot line in node-rev '%s'"),
                                 noderev_id);
      noderev->copyroot_path = apr_pstrdup(result_pool, value);
    }

  /* Get the copyfrom. */
  value = svn_hash_gets(headers, HEADER_COPYFROM);
  if (value == NULL)
    {
      noderev->copyfrom_path = NULL;
      noderev->copyfrom_rev = SVN_INVALID_REVNUM;
    }
  else
    {
      SVN_ERR(parse_revnum(&noderev->copyfrom_rev, (const char **)&value));

      if (*value == 0)
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyfrom line in node-rev '%s'"),
                                 noderev_id);
      noderev->copyfrom_path = apr_pstrdup(result_pool, value);
    }

  /* Get whether this is a fresh txn root. */
  value = svn_hash_gets(headers, HEADER_FRESHTXNRT);
  noderev->is_fresh_txn_root = (value != NULL);

  /* Get the mergeinfo count. */
  value = svn_hash_gets(headers, HEADER_MINFO_CNT);
  if (value)
    SVN_ERR(svn_cstring_atoi64(&noderev->mergeinfo_count, value));
  else
    noderev->mergeinfo_count = 0;

  /* Get whether *this* node has mergeinfo. */
  value = svn_hash_gets(headers, HEADER_MINFO_HERE);
  noderev->has_mergeinfo = (value != NULL);

  *noderev_p = noderev;

  return SVN_NO_ERROR;
}

/* Return a textual representation of the DIGEST of given KIND.
 * Allocate the result in RESULT_POOL.
 */
static const char *
format_digest(const unsigned char *digest,
              svn_checksum_kind_t kind,
              apr_pool_t *result_pool)
{
  svn_checksum_t checksum;
  checksum.digest = digest;
  checksum.kind = kind;

  return svn_checksum_to_cstring_display(&checksum, result_pool);
}

/* Return a textual representation of the uniquifier represented
 * by NODEREV_TXN_ID and NUMBER.  Use POOL for the allocations.
 */
static const char *
format_uniquifier(const svn_fs_fs__id_part_t *noderev_txn_id,
                  apr_uint64_t number,
                  apr_pool_t *pool)
{
  char buf[SVN_INT64_BUFFER_SIZE];
  const char *txn_id_str;

  txn_id_str = svn_fs_fs__id_txn_unparse(noderev_txn_id, pool);
  svn__ui64tobase36(buf, number);

  return apr_psprintf(pool, "%s/_%s", txn_id_str, buf);
}

svn_stringbuf_t *
svn_fs_fs__unparse_representation(representation_t *rep,
                                  int format,
                                  svn_boolean_t mutable_rep_truncated,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *str;
  const char *sha1_str;
  const char *uniquifier_str;

  if (svn_fs_fs__id_txn_used(&rep->txn_id) && mutable_rep_truncated)
    return svn_stringbuf_ncreate("-1", 2, result_pool);

  /* Format of the string:
     <rev> <item_index> <size> <expanded-size> <md5> [<sha1>] [<uniquifier>]
   */
  str = svn_stringbuf_createf(
          result_pool,
          "%ld"
          " %" APR_UINT64_T_FMT
          " %" SVN_FILESIZE_T_FMT
          " %" SVN_FILESIZE_T_FMT
          " %s",
          rep->revision,
          rep->item_index,
          rep->size,
          rep->expanded_size,
          format_digest(rep->md5_digest, svn_checksum_md5, scratch_pool));

  /* Compatibility: these formats don't understand <sha1> and <uniquifier>. */
  if (format < SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    return str;

  if (format < SVN_FS_FS__MIN_REP_STRING_OPTIONAL_VALUES_FORMAT)
    {
      /* Compatibility: these formats can only have <sha1> and <uniquifier>
         present simultaneously, or don't have them at all. */
      if (rep->has_sha1)
        {
          sha1_str = format_digest(rep->sha1_digest, svn_checksum_sha1,
                                   scratch_pool);
          uniquifier_str = format_uniquifier(&rep->uniquifier.noderev_txn_id,
                                             rep->uniquifier.number,
                                             scratch_pool);
          svn_stringbuf_appendbyte(str, ' ');
          svn_stringbuf_appendcstr(str, sha1_str);
          svn_stringbuf_appendbyte(str, ' ');
          svn_stringbuf_appendcstr(str, uniquifier_str);
        }
      return str;
    }

  /* The most recent formats support optional <sha1> and <uniquifier> values. */
  if (rep->has_sha1)
    {
      sha1_str = format_digest(rep->sha1_digest, svn_checksum_sha1,
                               scratch_pool);
    }
  else
    sha1_str = "-";

  if (rep->uniquifier.number == 0 &&
      rep->uniquifier.noderev_txn_id.number == 0 &&
      rep->uniquifier.noderev_txn_id.revision == 0)
    {
      uniquifier_str = "-";
    }
  else
    {
      uniquifier_str = format_uniquifier(&rep->uniquifier.noderev_txn_id,
                                         rep->uniquifier.number,
                                         scratch_pool);
    }

  svn_stringbuf_appendbyte(str, ' ');
  svn_stringbuf_appendcstr(str, sha1_str);
  svn_stringbuf_appendbyte(str, ' ');
  svn_stringbuf_appendcstr(str, uniquifier_str);

  return str;
}


svn_error_t *
svn_fs_fs__write_noderev(svn_stream_t *outfile,
                         node_revision_t *noderev,
                         int format,
                         svn_boolean_t include_mergeinfo,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_ID ": %s\n",
                            svn_fs_fs__id_unparse(noderev->id,
                                                  scratch_pool)->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_TYPE ": %s\n",
                            (noderev->kind == svn_node_file) ?
                            SVN_FS_FS__KIND_FILE : SVN_FS_FS__KIND_DIR));

  if (noderev->predecessor_id)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_PRED ": %s\n",
                              svn_fs_fs__id_unparse(noderev->predecessor_id,
                                                    scratch_pool)->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COUNT ": %d\n",
                            noderev->predecessor_count));

  if (noderev->data_rep)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_TEXT ": %s\n",
                              svn_fs_fs__unparse_representation
                                (noderev->data_rep,
                                 format,
                                 noderev->kind == svn_node_dir,
                                 scratch_pool, scratch_pool)->data));

  if (noderev->prop_rep)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_PROPS ": %s\n",
                              svn_fs_fs__unparse_representation
                                (noderev->prop_rep, format,
                                 TRUE, scratch_pool, scratch_pool)->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_CPATH ": %s\n",
                            noderev->created_path));

  if (noderev->copyfrom_path)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COPYFROM ": %ld"
                              " %s\n",
                              noderev->copyfrom_rev,
                              noderev->copyfrom_path));

  if ((noderev->copyroot_rev != svn_fs_fs__id_rev(noderev->id)) ||
      (strcmp(noderev->copyroot_path, noderev->created_path) != 0))
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COPYROOT ": %ld"
                              " %s\n",
                              noderev->copyroot_rev,
                              noderev->copyroot_path));

  if (noderev->is_fresh_txn_root)
    SVN_ERR(svn_stream_puts(outfile, HEADER_FRESHTXNRT ": y\n"));

  if (include_mergeinfo)
    {
      if (noderev->mergeinfo_count > 0)
        SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_MINFO_CNT
                                  ": %" APR_INT64_T_FMT "\n",
                                  noderev->mergeinfo_count));

      if (noderev->has_mergeinfo)
        SVN_ERR(svn_stream_puts(outfile, HEADER_MINFO_HERE ": y\n"));
    }

  return svn_stream_puts(outfile, "\n");
}

svn_error_t *
svn_fs_fs__read_rep_header(svn_fs_fs__rep_header_t **header,
                           svn_stream_t *stream,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buffer;
  char *str, *last_str;
  apr_int64_t val;
  svn_boolean_t eol = FALSE;

  SVN_ERR(svn_stream_readline(stream, &buffer, "\n", &eol, scratch_pool));

  *header = apr_pcalloc(result_pool, sizeof(**header));
  (*header)->header_size = buffer->len + 1;
  if (strcmp(buffer->data, REP_PLAIN) == 0)
    {
      (*header)->type = svn_fs_fs__rep_plain;
      return SVN_NO_ERROR;
    }

  if (strcmp(buffer->data, REP_DELTA) == 0)
    {
      /* This is a delta against the empty stream. */
      (*header)->type = svn_fs_fs__rep_self_delta;
      return SVN_NO_ERROR;
    }

  (*header)->type = svn_fs_fs__rep_delta;

  /* We have hopefully a DELTA vs. a non-empty base revision. */
  last_str = buffer->data;
  str = svn_cstring_tokenize(" ", &last_str);
  if (! str || (strcmp(str, REP_DELTA) != 0))
    goto error;

  SVN_ERR(parse_revnum(&(*header)->base_revision, (const char **)&last_str));

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  (*header)->base_item_index = (apr_off_t)val;

  str = svn_cstring_tokenize(" ", &last_str);
  if (! str)
    goto error;
  SVN_ERR(svn_cstring_atoi64(&val, str));
  (*header)->base_length = (svn_filesize_t)val;

  return SVN_NO_ERROR;

 error:
  return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                           _("Malformed representation header"));
}

svn_error_t *
svn_fs_fs__write_rep_header(svn_fs_fs__rep_header_t *header,
                            svn_stream_t *stream,
                            apr_pool_t *scratch_pool)
{
  const char *text;

  switch (header->type)
    {
      case svn_fs_fs__rep_plain:
        text = REP_PLAIN "\n";
        break;

      case svn_fs_fs__rep_self_delta:
        text = REP_DELTA "\n";
        break;

      default:
        text = apr_psprintf(scratch_pool, REP_DELTA " %ld %" APR_OFF_T_FMT
                                          " %" SVN_FILESIZE_T_FMT "\n",
                            header->base_revision, header->base_item_index,
                            header->base_length);
    }

  return svn_error_trace(svn_stream_puts(stream, text));
}
