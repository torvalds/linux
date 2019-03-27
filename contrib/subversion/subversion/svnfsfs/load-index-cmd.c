/* load-index-cmd.c -- implements the dump-index sub-command.
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

#include "svn_ctype.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_pools.h"

#include "private/svn_fs_fs_private.h"
#include "private/svn_sorts_private.h"

#include "svn_private_config.h"

#include "svnfsfs.h"

/* Map svn_fs_fs__p2l_entry_t.type to C string. */
static const char *item_type_str[]
  = {"none", "frep", "drep", "fprop", "dprop", "node", "chgs", "rep"};

/* Reverse lookup in ITEM_TYPE_STR: Set *TYPE to the index that contains STR.
 * Return an error for invalid strings. */
static svn_error_t *
str_to_item_type(unsigned *type,
                 const char *str)
{
  unsigned i;
  for (i = 0; i < sizeof(item_type_str) / sizeof(item_type_str[0]); ++i)
    if (strcmp(item_type_str[i], str) == 0)
      {
        *type = i;
        return SVN_NO_ERROR;
      }

  return svn_error_createf(SVN_ERR_BAD_TOKEN, NULL,
                           _("Unknown item type '%s'"), str);
}

/* Parse the string given as const char * at IDX in TOKENS and return its
 * value in *VALUE_P.  Assume that the string an integer with base RADIX.
 * Check for index overflows and non-hex chars.
 */
static svn_error_t *
token_to_i64(apr_int64_t *value_p,
             apr_array_header_t *tokens,
             int idx,
             int radix)
{
  const char *hex;
  char *end;
  apr_int64_t value;

  /* Tell the user when there is not enough information. */
  SVN_ERR_ASSERT(idx >= 0);
  if (tokens->nelts <= idx)
    return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                             _("%i columns needed, %i provided"),
                             idx + 1, tokens->nelts);

  /* hex -> int conversion */
  hex = APR_ARRAY_IDX(tokens, idx, const char *);
  value = apr_strtoi64(hex, &end, radix);

  /* Has the whole token be parsed without error? */
  if (errno || *end != '\0')
    return svn_error_createf(SVN_ERR_INVALID_INPUT, NULL,
                             _("%s is not a value HEX string"), hex);

  *value_p = value;
  return SVN_NO_ERROR;
}

/* Parse the P2L entry given as space separated values in LINE and return it
 * in *ENTRY.  Ignore extra columns.  Allocate the result in RESULT_POOL and
 * use SCRATCH_POOL for temporaries.
 */
static svn_error_t *
parse_index_line(svn_fs_fs__p2l_entry_t **entry,
                 svn_stringbuf_t *line,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *tokens = svn_cstring_split(line->data, " ", TRUE,
                                                 scratch_pool);
  svn_fs_fs__p2l_entry_t *result = apr_pcalloc(result_pool, sizeof(*result));
  apr_int64_t value;

  /* Parse the hex columns. */
  SVN_ERR(token_to_i64(&value, tokens, 0, 16));
  result->offset = (apr_off_t)value;
  SVN_ERR(token_to_i64(&value, tokens, 1, 16));
  result->size = (apr_off_t)value;

  /* Parse the rightmost colum that we care of. */
  SVN_ERR(token_to_i64(&value, tokens, 4, 10));
  result->item.number = (apr_uint64_t)value;

  /* We now know that there were at least 5 columns.
   * Parse the non-hex columns without index check. */
  SVN_ERR(str_to_item_type(&result->type,
                           APR_ARRAY_IDX(tokens, 2, const char *)));
  SVN_ERR(svn_revnum_parse(&result->item.revision,
                           APR_ARRAY_IDX(tokens, 3, const char *), NULL));

  *entry = result;
  return SVN_NO_ERROR;
}

/* Parse the space separated P2L index table from INPUT, one entry per line.
 * Rewrite the respective index files in PATH.  Allocate from POOL. */
static svn_error_t *
load_index(const char *path,
           svn_stream_t *input,
           apr_pool_t *pool)
{
  svn_fs_t *fs;
  svn_revnum_t revision = SVN_INVALID_REVNUM;
  apr_array_header_t *entries = apr_array_make(pool, 16, sizeof(void*));
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Check repository type and open it. */
  SVN_ERR(open_fs(&fs, path, pool));

  while (TRUE)
    {
      svn_stringbuf_t *line;
      svn_fs_fs__p2l_entry_t *entry;
      svn_boolean_t eol;

      /* Get the next line from the input and stop if there is none. */
      svn_pool_clear(iterpool);
      svn_stream_readline(input, &line, "\n", &eol, iterpool);
      if (eol)
        break;

      /* Skip header line(s).  They contain the sub-string [Ss]tart. */
      if (strstr(line->data, "tart"))
        continue;

      /* Ignore empty lines (mostly trailing ones but we don't really care).
       */
      svn_stringbuf_strip_whitespace(line);
      if (line->len == 0)
        continue;

      /* Parse the entry and append it to ENTRIES. */
      SVN_ERR(parse_index_line(&entry, line, pool, iterpool));
      APR_ARRAY_PUSH(entries, svn_fs_fs__p2l_entry_t *) = entry;

      /* There should be at least one item that is not empty.
       * Get a revision from (probably inside) the respective shard. */
      if (   revision == SVN_INVALID_REVNUM
          && entry->item.revision != SVN_INVALID_REVNUM)
        revision = entry->item.revision;
    }

  /* Rewrite the indexes. */
  SVN_ERR(svn_fs_fs__load_index(fs, revision, entries, iterpool));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* This implements `svn_opt_subcommand_t'. */
svn_error_t *
subcommand__load_index(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  svnfsfs__opt_state *opt_state = baton;
  svn_stream_t *input;

  SVN_ERR(svn_stream_for_stdin2(&input, TRUE, pool));
  SVN_ERR(load_index(opt_state->repository_path, input, pool));

  return SVN_NO_ERROR;
}
