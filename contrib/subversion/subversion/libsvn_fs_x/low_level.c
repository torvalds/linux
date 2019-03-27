/* low_level.c --- low level r/w access to FSX file structures
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
#include "util.h"
#include "pack.h"
#include "cached_data.h"

/* Headers used to describe node-revision in the revision file. */
#define HEADER_ID          "id"
#define HEADER_NODE        "node"
#define HEADER_COPY        "copy"
#define HEADER_TYPE        "type"
#define HEADER_COUNT       "count"
#define HEADER_PROPS       "props"
#define HEADER_TEXT        "text"
#define HEADER_CPATH       "cpath"
#define HEADER_PRED        "pred"
#define HEADER_COPYFROM    "copyfrom"
#define HEADER_COPYROOT    "copyroot"
#define HEADER_MINFO_HERE  "minfo-here"
#define HEADER_MINFO_CNT   "minfo-cnt"

/* Kinds that a change can be. */
#define ACTION_MODIFY      "modify"
#define ACTION_ADD         "add"
#define ACTION_DELETE      "delete"
#define ACTION_REPLACE     "replace"

/* True and False flags. */
#define FLAG_TRUE          "true"
#define FLAG_FALSE         "false"

/* Kinds of representation. */
#define REP_DELTA          "DELTA"

/* An arbitrary maximum path length, so clients can't run us out of memory
 * by giving us arbitrarily large paths. */
#define FSX_MAX_PATH_LEN 4096

/* The 256 is an arbitrary size large enough to hold the node id and the
 * various flags. */
#define MAX_CHANGE_LINE_LEN FSX_MAX_PATH_LEN + 256

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
svn_fs_x__parse_footer(apr_off_t *l2p_offset,
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
svn_fs_x__unparse_footer(apr_off_t l2p_offset,
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

svn_error_t *
svn_fs_x__parse_representation(svn_fs_x__representation_t **rep_p,
                               svn_stringbuf_t *text,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_fs_x__representation_t *rep;
  char *str;
  apr_int64_t val;
  char *string = text->data;
  svn_checksum_t *checksum;

  rep = apr_pcalloc(result_pool, sizeof(*rep));
  *rep_p = rep;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_cstring_atoi64(&rep->id.change_set, str));

  /* while in transactions, it is legal to simply write "-1" */
  if (rep->id.change_set == -1)
    return SVN_NO_ERROR;

  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    {
      if (rep->id.change_set == SVN_FS_X__INVALID_CHANGE_SET)
        return SVN_NO_ERROR;

      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Malformed text representation offset line in node-rev"));
    }

  SVN_ERR(svn_cstring_atoi64(&val, str));
  rep->id.number = (apr_off_t)val;

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
  if (checksum)
    memcpy(rep->md5_digest, checksum->digest, sizeof(rep->md5_digest));

  /* The remaining fields are only used for formats >= 4, so check that. */
  str = svn_cstring_tokenize(" ", &string);
  if (str == NULL)
    return SVN_NO_ERROR;

  /* Read the SHA1 hash. */
  if (strlen(str) != (APR_SHA1_DIGESTSIZE * 2))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Malformed text representation offset line in node-rev"));

  SVN_ERR(svn_checksum_parse_hex(&checksum, svn_checksum_sha1, str,
                                 scratch_pool));
  rep->has_sha1 = checksum != NULL;
  if (checksum)
    memcpy(rep->sha1_digest, checksum->digest, sizeof(rep->sha1_digest));

  return SVN_NO_ERROR;
}

/* Wrap read_rep_offsets_body(), extracting its TXN_ID from our NODEREV_ID,
   and adding an error message. */
static svn_error_t *
read_rep_offsets(svn_fs_x__representation_t **rep_p,
                 char *string,
                 const svn_fs_x__id_t *noderev_id,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err
    = svn_fs_x__parse_representation(rep_p,
                                     svn_stringbuf_create_wrap(string,
                                                               scratch_pool),
                                     result_pool,
                                     scratch_pool);
  if (err)
    {
      const svn_string_t *id_unparsed;
      const char *where;

      id_unparsed = svn_fs_x__id_unparse(noderev_id, scratch_pool);
      where = apr_psprintf(scratch_pool,
                           _("While reading representation offsets "
                             "for node-revision '%s':"),
                           id_unparsed->data);

      return svn_error_quick_wrap(err, where);
    }

  return SVN_NO_ERROR;
}

/* If PATH needs to be escaped, return an escaped version of it, allocated
 * from RESULT_POOL. Otherwise, return PATH directly. */
static const char *
auto_escape_path(const char *path,
                 apr_pool_t *result_pool)
{
  apr_size_t len = strlen(path);
  apr_size_t i;
  const char esc = '\x1b';

  for (i = 0; i < len; ++i)
    if (path[i] < ' ')
      {
        svn_stringbuf_t *escaped = svn_stringbuf_create_ensure(2 * len,
                                                               result_pool);
        for (i = 0; i < len; ++i)
          if (path[i] < ' ')
            {
              svn_stringbuf_appendbyte(escaped, esc);
              svn_stringbuf_appendbyte(escaped, path[i] + 'A' - 1);
            }
          else
            {
              svn_stringbuf_appendbyte(escaped, path[i]);
            }

        return escaped->data;
      }

   return path;
}

/* If PATH has been escaped, return the un-escaped version of it, allocated
 * from RESULT_POOL. Otherwise, return PATH directly. */
static const char *
auto_unescape_path(const char *path,
                   apr_pool_t *result_pool)
{
  const char esc = '\x1b';
  if (strchr(path, esc))
    {
      apr_size_t len = strlen(path);
      apr_size_t i;

      svn_stringbuf_t *unescaped = svn_stringbuf_create_ensure(len,
                                                               result_pool);
      for (i = 0; i < len; ++i)
        if (path[i] == esc)
          svn_stringbuf_appendbyte(unescaped, path[++i] + 1 - 'A');
        else
          svn_stringbuf_appendbyte(unescaped, path[i]);

      return unescaped->data;
    }

   return path;
}

/* Find entry HEADER_NAME in HEADERS and parse its value into *ID. */
static svn_error_t *
read_id_part(svn_fs_x__id_t *id,
             apr_hash_t *headers,
             const char *header_name)
{
  const char *value = svn_hash_gets(headers, header_name);
  if (value == NULL)
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Missing %s field in node-rev"),
                             header_name);

  SVN_ERR(svn_fs_x__id_parse(id, value));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_noderev(svn_fs_x__noderev_t **noderev_p,
                       svn_stream_t *stream,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_hash_t *headers;
  svn_fs_x__noderev_t *noderev;
  char *value;
  const char *noderev_id;

  SVN_ERR(read_header_block(&headers, stream, scratch_pool));
  SVN_ERR(svn_stream_close(stream));

  noderev = apr_pcalloc(result_pool, sizeof(*noderev));

  /* for error messages later */
  noderev_id = svn_hash_gets(headers, HEADER_ID);

  /* Read the node-rev id. */
  SVN_ERR(read_id_part(&noderev->noderev_id, headers, HEADER_ID));
  SVN_ERR(read_id_part(&noderev->node_id, headers, HEADER_NODE));
  SVN_ERR(read_id_part(&noderev->copy_id, headers, HEADER_COPY));

  /* Read the type. */
  value = svn_hash_gets(headers, HEADER_TYPE);

  if ((value == NULL) ||
      (   strcmp(value, SVN_FS_X__KIND_FILE)
       && strcmp(value, SVN_FS_X__KIND_DIR)))
    /* ### s/kind/type/ */
    return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                             _("Missing kind field in node-rev '%s'"),
                             noderev_id);

  noderev->kind = (strcmp(value, SVN_FS_X__KIND_FILE) == 0)
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
                               &noderev->noderev_id, result_pool,
                               scratch_pool));
    }

  /* Get the data location. */
  value = svn_hash_gets(headers, HEADER_TEXT);
  if (value)
    {
      SVN_ERR(read_rep_offsets(&noderev->data_rep, value,
                               &noderev->noderev_id, result_pool,
                               scratch_pool));
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

      noderev->created_path = auto_unescape_path(apr_pstrdup(result_pool,
                                                              value),
                                                 result_pool);
    }

  /* Get the predecessor ID. */
  value = svn_hash_gets(headers, HEADER_PRED);
  if (value)
    SVN_ERR(svn_fs_x__id_parse(&noderev->predecessor_id, value));
  else
    svn_fs_x__id_reset(&noderev->predecessor_id);

  /* Get the copyroot. */
  value = svn_hash_gets(headers, HEADER_COPYROOT);
  if (value == NULL)
    {
      noderev->copyroot_path = noderev->created_path;
      noderev->copyroot_rev
        = svn_fs_x__get_revnum(noderev->noderev_id.change_set);
    }
  else
    {
      SVN_ERR(parse_revnum(&noderev->copyroot_rev, (const char **)&value));

      if (!svn_fspath__is_canonical(value))
        return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                                 _("Malformed copyroot line in node-rev '%s'"),
                                 noderev_id);
      noderev->copyroot_path = auto_unescape_path(apr_pstrdup(result_pool,
                                                              value),
                                                  result_pool);
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
      noderev->copyfrom_path = auto_unescape_path(apr_pstrdup(result_pool,
                                                              value),
                                                  result_pool);
    }

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
 * If IS_NULL is TRUE, no digest is available.
 * Allocate the result in RESULT_POOL.
 */
static const char *
format_digest(const unsigned char *digest,
              svn_checksum_kind_t kind,
              svn_boolean_t is_null,
              apr_pool_t *result_pool)
{
  svn_checksum_t checksum;
  checksum.digest = digest;
  checksum.kind = kind;

  if (is_null)
    return "(null)";

  return svn_checksum_to_cstring_display(&checksum, result_pool);
}

svn_stringbuf_t *
svn_fs_x__unparse_representation(svn_fs_x__representation_t *rep,
                                 svn_boolean_t mutable_rep_truncated,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  if (!rep->has_sha1)
    return svn_stringbuf_createf
            (result_pool,
             "%" APR_INT64_T_FMT " %" APR_UINT64_T_FMT " %" SVN_FILESIZE_T_FMT
             " %" SVN_FILESIZE_T_FMT " %s",
             rep->id.change_set, rep->id.number, rep->size,
             rep->expanded_size,
             format_digest(rep->md5_digest, svn_checksum_md5, FALSE,
                           scratch_pool));

  return svn_stringbuf_createf
          (result_pool,
           "%" APR_INT64_T_FMT " %" APR_UINT64_T_FMT " %" SVN_FILESIZE_T_FMT
           " %" SVN_FILESIZE_T_FMT " %s %s",
           rep->id.change_set, rep->id.number, rep->size,
           rep->expanded_size,
           format_digest(rep->md5_digest, svn_checksum_md5,
                         FALSE, scratch_pool),
           format_digest(rep->sha1_digest, svn_checksum_sha1,
                         !rep->has_sha1, scratch_pool));
}


svn_error_t *
svn_fs_x__write_noderev(svn_stream_t *outfile,
                        svn_fs_x__noderev_t *noderev,
                        apr_pool_t *scratch_pool)
{
  svn_string_t *str_id;

  str_id = svn_fs_x__id_unparse(&noderev->noderev_id, scratch_pool);
  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_ID ": %s\n",
                            str_id->data));
  str_id = svn_fs_x__id_unparse(&noderev->node_id, scratch_pool);
  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_NODE ": %s\n",
                            str_id->data));
  str_id = svn_fs_x__id_unparse(&noderev->copy_id, scratch_pool);
  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COPY ": %s\n",
                            str_id->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_TYPE ": %s\n",
                            (noderev->kind == svn_node_file) ?
                            SVN_FS_X__KIND_FILE : SVN_FS_X__KIND_DIR));

  if (svn_fs_x__id_used(&noderev->predecessor_id))
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_PRED ": %s\n",
                              svn_fs_x__id_unparse(&noderev->predecessor_id,
                                                   scratch_pool)->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COUNT ": %d\n",
                            noderev->predecessor_count));

  if (noderev->data_rep)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_TEXT ": %s\n",
                              svn_fs_x__unparse_representation
                                (noderev->data_rep,
                                 noderev->kind == svn_node_dir,
                                 scratch_pool, scratch_pool)->data));

  if (noderev->prop_rep)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_PROPS ": %s\n",
                              svn_fs_x__unparse_representation
                                (noderev->prop_rep,
                                 TRUE, scratch_pool, scratch_pool)->data));

  SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_CPATH ": %s\n",
                            auto_escape_path(noderev->created_path,
                                             scratch_pool)));

  if (noderev->copyfrom_path)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COPYFROM ": %ld"
                              " %s\n",
                              noderev->copyfrom_rev,
                              auto_escape_path(noderev->copyfrom_path,
                                               scratch_pool)));

  if (   (   noderev->copyroot_rev
           != svn_fs_x__get_revnum(noderev->noderev_id.change_set))
      || (strcmp(noderev->copyroot_path, noderev->created_path) != 0))
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_COPYROOT ": %ld"
                              " %s\n",
                              noderev->copyroot_rev,
                              auto_escape_path(noderev->copyroot_path,
                                               scratch_pool)));

  if (noderev->mergeinfo_count > 0)
    SVN_ERR(svn_stream_printf(outfile, scratch_pool, HEADER_MINFO_CNT ": %"
                              APR_INT64_T_FMT "\n",
                              noderev->mergeinfo_count));

  if (noderev->has_mergeinfo)
    SVN_ERR(svn_stream_puts(outfile, HEADER_MINFO_HERE ": y\n"));

  return svn_stream_puts(outfile, "\n");
}

svn_error_t *
svn_fs_x__read_rep_header(svn_fs_x__rep_header_t **header,
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
  if (strcmp(buffer->data, REP_DELTA) == 0)
    {
      /* This is a delta against the empty stream. */
      (*header)->type = svn_fs_x__rep_self_delta;
      return SVN_NO_ERROR;
    }

  (*header)->type = svn_fs_x__rep_delta;

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
svn_fs_x__write_rep_header(svn_fs_x__rep_header_t *header,
                           svn_stream_t *stream,
                           apr_pool_t *scratch_pool)
{
  const char *text;

  switch (header->type)
    {
      case svn_fs_x__rep_self_delta:
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

/* Read the next entry in the changes record from file FILE and store
   the resulting change in *CHANGE_P.  If there is no next record,
   store NULL there.  Perform all allocations from POOL. */
static svn_error_t *
read_change(svn_fs_x__change_t **change_p,
            svn_stream_t *stream,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof = TRUE;
  svn_fs_x__change_t *change;
  char *str, *last_str, *kind_str;

  /* Default return value. */
  *change_p = NULL;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));

  /* Check for a blank line. */
  if (eof || (line->len == 0))
    return SVN_NO_ERROR;

  change = apr_pcalloc(result_pool, sizeof(*change));
  last_str = line->data;

  /* Get the change type. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  /* Don't bother to check the format number before looking for
   * node-kinds: just read them if you find them. */
  change->node_kind = svn_node_unknown;
  kind_str = strchr(str, '-');
  if (kind_str)
    {
      /* Cap off the end of "str" (the action). */
      *kind_str = '\0';
      kind_str++;
      if (strcmp(kind_str, SVN_FS_X__KIND_FILE) == 0)
        change->node_kind = svn_node_file;
      else if (strcmp(kind_str, SVN_FS_X__KIND_DIR) == 0)
        change->node_kind = svn_node_dir;
      else
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid changes line in rev-file"));
    }

  if (strcmp(str, ACTION_MODIFY) == 0)
    {
      change->change_kind = svn_fs_path_change_modify;
    }
  else if (strcmp(str, ACTION_ADD) == 0)
    {
      change->change_kind = svn_fs_path_change_add;
    }
  else if (strcmp(str, ACTION_DELETE) == 0)
    {
      change->change_kind = svn_fs_path_change_delete;
    }
  else if (strcmp(str, ACTION_REPLACE) == 0)
    {
      change->change_kind = svn_fs_path_change_replace;
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
      change->text_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      change->text_mod = FALSE;
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
      change->prop_mod = TRUE;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      change->prop_mod = FALSE;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid prop-mod flag in rev-file"));
    }

  /* Get the mergeinfo-mod flag. */
  str = svn_cstring_tokenize(" ", &last_str);
  if (str == NULL)
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid changes line in rev-file"));

  if (strcmp(str, FLAG_TRUE) == 0)
    {
      change->mergeinfo_mod = svn_tristate_true;
    }
  else if (strcmp(str, FLAG_FALSE) == 0)
    {
      change->mergeinfo_mod = svn_tristate_false;
    }
  else
    {
      return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                              _("Invalid mergeinfo-mod flag in rev-file"));
    }

  /* Get the changed path. */
  if (!svn_fspath__is_canonical(last_str))
    return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                            _("Invalid path in changes line"));

  change->path.data = auto_unescape_path(apr_pstrmemdup(result_pool,
                                                        last_str,
                                                        strlen(last_str)),
                                         result_pool);
  change->path.len = strlen(change->path.data);

  /* Read the next line, the copyfrom line. */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, result_pool));
  change->copyfrom_known = TRUE;
  if (eof || line->len == 0)
    {
      change->copyfrom_rev = SVN_INVALID_REVNUM;
      change->copyfrom_path = NULL;
    }
  else
    {
      last_str = line->data;
      SVN_ERR(parse_revnum(&change->copyfrom_rev, (const char **)&last_str));

      if (!svn_fspath__is_canonical(last_str))
        return svn_error_create(SVN_ERR_FS_CORRUPT, NULL,
                                _("Invalid copy-from path in changes line"));

      change->copyfrom_path = auto_unescape_path(last_str, result_pool);
    }

  *change_p = change;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_changes(apr_array_header_t **changes,
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
  *changes = apr_array_make(result_pool, 63, sizeof(svn_fs_x__change_t *));

  iterpool = svn_pool_create(scratch_pool);
  for (; max_count > 0; --max_count)
    {
      svn_fs_x__change_t *change;
      svn_pool_clear(iterpool);
      SVN_ERR(read_change(&change, stream, result_pool, iterpool));
      if (!change)
        break;
 
      APR_ARRAY_PUSH(*changes, svn_fs_x__change_t*) = change;
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__read_changes_incrementally(svn_stream_t *stream,
                                     svn_fs_x__change_receiver_t
                                       change_receiver,
                                     void *change_receiver_baton,
                                     apr_pool_t *scratch_pool)
{
  svn_fs_x__change_t *change;
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

   All temporary allocations are in SCRATCH_POOL. */
static svn_error_t *
write_change_entry(svn_stream_t *stream,
                   svn_fs_x__change_t *change,
                   apr_pool_t *scratch_pool)
{
  const char *change_string = NULL;
  const char *kind_string = "";
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
    default:
      return svn_error_createf(SVN_ERR_FS_CORRUPT, NULL,
                               _("Invalid change type %d"),
                               change->change_kind);
    }

  SVN_ERR_ASSERT(change->node_kind == svn_node_dir
                 || change->node_kind == svn_node_file);
  kind_string = apr_psprintf(scratch_pool, "-%s",
                             change->node_kind == svn_node_dir
                             ? SVN_FS_X__KIND_DIR
                             : SVN_FS_X__KIND_FILE);

  buf = svn_stringbuf_createf(scratch_pool, "%s%s %s %s %s %s\n",
                              change_string, kind_string,
                              change->text_mod ? FLAG_TRUE : FLAG_FALSE,
                              change->prop_mod ? FLAG_TRUE : FLAG_FALSE,
                              change->mergeinfo_mod == svn_tristate_true
                                               ? FLAG_TRUE : FLAG_FALSE,
                              auto_escape_path(change->path.data, scratch_pool));

  if (SVN_IS_VALID_REVNUM(change->copyfrom_rev))
    {
      svn_stringbuf_appendcstr(buf, apr_psprintf(scratch_pool, "%ld %s",
                               change->copyfrom_rev,
                               auto_escape_path(change->copyfrom_path,
                                                scratch_pool)));
    }

  svn_stringbuf_appendbyte(buf, '\n');

  /* Write all change info in one write call. */
  len = buf->len;
  return svn_error_trace(svn_stream_write(stream, buf->data, &len));
}

svn_error_t *
svn_fs_x__write_changes(svn_stream_t *stream,
                        svn_fs_t *fs,
                        apr_hash_t *changes,
                        svn_boolean_t terminate_list,
                        apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_array_header_t *sorted_changed_paths;
  int i;

  /* For the sake of the repository administrator sort the changes so
     that the final file is deterministic and repeatable, however the
     rest of the FSX code doesn't require any particular order here.

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
      svn_fs_x__change_t *change;

      svn_pool_clear(iterpool);
      change = APR_ARRAY_IDX(sorted_changed_paths, i, svn_sort__item_t).value;

      /* Write out the new entry into the final rev-file. */
      SVN_ERR(write_change_entry(stream, change, iterpool));
    }

  if (terminate_list)
    svn_stream_puts(stream, "\n");

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__parse_properties(apr_hash_t **properties,
                           const svn_string_t *content,
                           apr_pool_t *result_pool)
{
  const apr_byte_t *p = (const apr_byte_t *)content->data;
  const apr_byte_t *end = p + content->len;
  apr_uint64_t count;

  *properties = apr_hash_make(result_pool);

  /* Extract the number of properties we are expected to read. */
  p = svn__decode_uint(&count, p, end);

  /* Read all the properties we find.
     Because prop-name and prop-value are nicely NUL-terminated
     sub-strings of CONTENT, we can simply reference them there.
     I.e. there is no need to copy them around.
   */
  while (p < end)
    {
      apr_uint64_t value_len;
      svn_string_t *value;

      const char *key = (const char *)p;

      /* Note that this may never overflow / segfault because
         CONTENT itself is NUL-terminated. */
      apr_size_t key_len = strlen(key);
      p += key_len + 1;
      if (key[key_len])
        return svn_error_createf(SVN_ERR_FS_CORRUPT_PROPLIST, NULL,
                                 "Property name not NUL terminated");

      if (p >= end)
        return svn_error_createf(SVN_ERR_FS_CORRUPT_PROPLIST, NULL,
                                 "Property value missing");
      p = svn__decode_uint(&value_len, p, end);
      if (value_len >= (end - p))
        return svn_error_createf(SVN_ERR_FS_CORRUPT_PROPLIST, NULL,
                                 "Property value too long");

      value = apr_pcalloc(result_pool, sizeof(*value));
      value->data = (const char *)p;
      value->len = (apr_size_t)value_len;
      if (p[value->len])
        return svn_error_createf(SVN_ERR_FS_CORRUPT_PROPLIST, NULL,
                                 "Property value not NUL terminated");

      p += value->len + 1;

      apr_hash_set(*properties, key, key_len, value);
    }

  /* Check that we read the expected number of properties. */
  if ((apr_uint64_t)apr_hash_count(*properties) != count)
    return svn_error_createf(SVN_ERR_FS_CORRUPT_PROPLIST, NULL,
                             "Property count mismatch");

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_x__write_properties(svn_stream_t *stream,
                           apr_hash_t *proplist,
                           apr_pool_t *scratch_pool)
{
  apr_byte_t buffer[SVN__MAX_ENCODED_UINT_LEN];
  apr_size_t len;
  apr_hash_index_t *hi;

  /* Write the number of properties in this list. */
  len = svn__encode_uint(buffer, apr_hash_count(proplist)) - buffer;
  SVN_ERR(svn_stream_write(stream, (const char *)buffer, &len));

  /* Serialize each property as follows:
     <Prop-name> <NUL>
     <Value-len> <Prop-value> <NUL>
   */
  for (hi = apr_hash_first(scratch_pool, proplist);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *key;
      apr_size_t key_len;
      svn_string_t *value;
      apr_hash_this(hi, (const void **)&key, (apr_ssize_t *)&key_len,
                    (void **)&value);

      /* Include the terminating NUL. */
      ++key_len;
      SVN_ERR(svn_stream_write(stream, key, &key_len));

      len = svn__encode_uint(buffer, value->len) - buffer;
      SVN_ERR(svn_stream_write(stream, (const char *)buffer, &len));
      SVN_ERR(svn_stream_write(stream, value->data, &value->len));

      /* Terminate with NUL. */
      len = 1;
      SVN_ERR(svn_stream_write(stream, "", &len));
    }

  return SVN_NO_ERROR;
}
