/*
 * diff_memory.c :  routines for doing diffs on in-memory data
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

#define WANT_MEMFUNC
#define WANT_STRFUNC
#include <apr.h>
#include <apr_want.h>
#include <apr_tables.h>

#include <assert.h>

#include "svn_diff.h"
#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "diff.h"
#include "svn_private_config.h"
#include "private/svn_adler32.h"
#include "private/svn_diff_private.h"

typedef struct source_tokens_t
{
  /* A token simply is an svn_string_t pointing to
     the data of the in-memory data source, containing
     the raw token text, with length stored in the string */
  apr_array_header_t *tokens;

  /* Next token to be consumed */
  apr_size_t next_token;

  /* The source, containing the in-memory data to be diffed */
  const svn_string_t *source;

  /* The last token ends with a newline character (sequence) */
  svn_boolean_t ends_without_eol;
} source_tokens_t;

typedef struct diff_mem_baton_t
{
  /* The tokens for each of the sources */
  source_tokens_t sources[4];

  /* Normalization buffer; we only ever compare 2 tokens at the same time */
  char *normalization_buf[2];

  /* Options for normalized comparison of the data sources */
  const svn_diff_file_options_t *normalization_options;
} diff_mem_baton_t;


static int
datasource_to_index(svn_diff_datasource_e datasource)
{
  switch (datasource)
    {
    case svn_diff_datasource_original:
      return 0;

    case svn_diff_datasource_modified:
      return 1;

    case svn_diff_datasource_latest:
      return 2;

    case svn_diff_datasource_ancestor:
      return 3;
    }

  return -1;
}


/* Implements svn_diff_fns2_t::datasources_open */
static svn_error_t *
datasources_open(void *baton,
                 apr_off_t *prefix_lines,
                 apr_off_t *suffix_lines,
                 const svn_diff_datasource_e *datasources,
                 apr_size_t datasources_len)
{
  /* Do nothing: everything is already there and initialized to 0 */
  *prefix_lines = 0;
  *suffix_lines = 0;
  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns2_t::datasource_close */
static svn_error_t *
datasource_close(void *baton, svn_diff_datasource_e datasource)
{
  /* Do nothing.  The compare_token function needs previous datasources
   * to stay available until all datasources are processed.
   */

  return SVN_NO_ERROR;
}


/* Implements svn_diff_fns2_t::datasource_get_next_token */
static svn_error_t *
datasource_get_next_token(apr_uint32_t *hash, void **token, void *baton,
                          svn_diff_datasource_e datasource)
{
  diff_mem_baton_t *mem_baton = baton;
  source_tokens_t *src = &(mem_baton->sources[datasource_to_index(datasource)]);

  if ((apr_size_t)src->tokens->nelts > src->next_token)
    {
      /* There are actually tokens to be returned */
      char *buf = mem_baton->normalization_buf[0];
      svn_string_t *tok = (*token)
        = APR_ARRAY_IDX(src->tokens, src->next_token, svn_string_t *);
      apr_off_t len = tok->len;
      svn_diff__normalize_state_t state
        = svn_diff__normalize_state_normal;

      svn_diff__normalize_buffer(&buf, &len, &state, tok->data,
                                 mem_baton->normalization_options);
      *hash = svn__adler32(0, buf, len);
      src->next_token++;
    }
  else
    *token = NULL;

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns2_t::token_compare */
static svn_error_t *
token_compare(void *baton, void *token1, void *token2, int *result)
{
  /* Implement the same behaviour as diff_file.c:token_compare(),
     but be simpler, because we know we'll have all data in memory */
  diff_mem_baton_t *btn = baton;
  svn_string_t *t1 = token1;
  svn_string_t *t2 = token2;
  char *buf1 = btn->normalization_buf[0];
  char *buf2 = btn->normalization_buf[1];
  apr_off_t len1 = t1->len;
  apr_off_t len2 = t2->len;
  svn_diff__normalize_state_t state = svn_diff__normalize_state_normal;

  svn_diff__normalize_buffer(&buf1, &len1, &state, t1->data,
                             btn->normalization_options);
  state = svn_diff__normalize_state_normal;
  svn_diff__normalize_buffer(&buf2, &len2, &state, t2->data,
                             btn->normalization_options);

  if (len1 != len2)
    *result = (len1 < len2) ? -1 : 1;
  else
    *result = (len1 == 0) ? 0 : memcmp(buf1, buf2, (size_t) len1);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_fns2_t::token_discard */
static void
token_discard(void *baton, void *token)
{
  /* No-op, we have no use for discarded tokens... */
}


/* Implements svn_diff_fns2_t::token_discard_all */
static void
token_discard_all(void *baton)
{
  /* Do nothing.
     Note that in the file case, this function discards all
     tokens allocated, but we're geared toward small in-memory diffs.
     Meaning that there's no special pool to clear.
  */
}


static const svn_diff_fns2_t svn_diff__mem_vtable =
{
  datasources_open,
  datasource_close,
  datasource_get_next_token,
  token_compare,
  token_discard,
  token_discard_all
};

/* Fill SRC with the diff tokens (e.g. lines).

   TEXT is assumed to live long enough for the tokens to
   stay valid during their lifetime: no data is copied,
   instead, svn_string_t's are allocated pointing straight
   into TEXT.
*/
static void
fill_source_tokens(source_tokens_t *src,
                   const svn_string_t *text,
                   apr_pool_t *pool)
{
  const char *curp;
  const char *endp;
  const char *startp;

  src->tokens = apr_array_make(pool, 0, sizeof(svn_string_t *));
  src->next_token = 0;
  src->source = text;

  for (startp = curp = text->data, endp = curp + text->len;
       curp != endp; curp++)
    {
      if (curp != endp && *curp == '\r' && *(curp + 1) == '\n')
        curp++;

      if (*curp == '\r' || *curp == '\n')
        {
          APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
            svn_string_ncreate(startp, curp - startp + 1, pool);

          startp = curp + 1;
        }
    }

  /* If there's anything remaining (ie last line doesn't have a newline) */
  if (startp != endp)
    {
      APR_ARRAY_PUSH(src->tokens, svn_string_t *) =
        svn_string_ncreate(startp, endp - startp, pool);
      src->ends_without_eol = TRUE;
    }
  else
    src->ends_without_eol = FALSE;
}


static void
alloc_normalization_bufs(diff_mem_baton_t *btn,
                         int sources,
                         apr_pool_t *pool)
{
  apr_size_t max_len = 0;
  apr_off_t idx;
  int i;

  for (i = 0; i < sources; i++)
    {
      apr_array_header_t *tokens = btn->sources[i].tokens;
      if (tokens->nelts > 0)
        for (idx = 0; idx < tokens->nelts; idx++)
          {
            apr_size_t token_len
              = APR_ARRAY_IDX(tokens, idx, svn_string_t *)->len;
            max_len = (max_len < token_len) ? token_len : max_len;
          }
    }

  btn->normalization_buf[0] = apr_palloc(pool, max_len);
  btn->normalization_buf[1] = apr_palloc(pool, max_len);
}

svn_error_t *
svn_diff_mem_string_diff(svn_diff_t **diff,
                         const svn_string_t *original,
                         const svn_string_t *modified,
                         const svn_diff_file_options_t *options,
                         apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  alloc_normalization_bufs(&baton, 2, pool);

  baton.normalization_options = options;

  return svn_diff_diff_2(diff, &baton, &svn_diff__mem_vtable, pool);
}

svn_error_t *
svn_diff_mem_string_diff3(svn_diff_t **diff,
                          const svn_string_t *original,
                          const svn_string_t *modified,
                          const svn_string_t *latest,
                          const svn_diff_file_options_t *options,
                          apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  fill_source_tokens(&(baton.sources[2]), latest, pool);
  alloc_normalization_bufs(&baton, 3, pool);

  baton.normalization_options = options;

  return svn_diff_diff3_2(diff, &baton, &svn_diff__mem_vtable, pool);
}


svn_error_t *
svn_diff_mem_string_diff4(svn_diff_t **diff,
                          const svn_string_t *original,
                          const svn_string_t *modified,
                          const svn_string_t *latest,
                          const svn_string_t *ancestor,
                          const svn_diff_file_options_t *options,
                          apr_pool_t *pool)
{
  diff_mem_baton_t baton;

  fill_source_tokens(&(baton.sources[0]), original, pool);
  fill_source_tokens(&(baton.sources[1]), modified, pool);
  fill_source_tokens(&(baton.sources[2]), latest, pool);
  fill_source_tokens(&(baton.sources[3]), ancestor, pool);
  alloc_normalization_bufs(&baton, 4, pool);

  baton.normalization_options = options;

  return svn_diff_diff4_2(diff, &baton, &svn_diff__mem_vtable, pool);
}


typedef enum unified_output_e
{
  unified_output_context = 0,
  unified_output_delete,
  unified_output_insert,
  unified_output_skip
} unified_output_e;

/* Baton for generating unified diffs */
typedef struct unified_output_baton_t
{
  svn_stream_t *output_stream;
  const char *header_encoding;
  source_tokens_t sources[2]; /* 0 == original; 1 == modified */
  apr_off_t current_token[2]; /* current token per source */

  int context_size;

  /* Cached markers, in header_encoding,
     indexed using unified_output_e */
  const char *prefix_str[3];

  svn_stringbuf_t *hunk;    /* in-progress hunk data */
  apr_off_t hunk_length[2]; /* 0 == original; 1 == modified */
  apr_off_t hunk_start[2];  /* 0 == original; 1 == modified */

  /* The delimiters of the hunk header, '@@' for text hunks and '##' for
   * property hunks. */
  const char *hunk_delimiter;
  /* The string to print after a line that does not end with a newline.
   * It must start with a '\'.  Typically "\ No newline at end of file". */
  const char *no_newline_string;

  /* Pool for allocation of temporary memory in the callbacks
     Should be cleared on entry of each iteration of a callback */
  apr_pool_t *pool;
} output_baton_t;


/* Append tokens (lines) FIRST up to PAST_LAST
   from token-source index TOKENS with change-type TYPE
   to the current hunk.
*/
static svn_error_t *
output_unified_token_range(output_baton_t *btn,
                           int tokens,
                           unified_output_e type,
                           apr_off_t until)
{
  source_tokens_t *source = &btn->sources[tokens];

  if (until > source->tokens->nelts)
    until = source->tokens->nelts;

  if (until <= btn->current_token[tokens])
    return SVN_NO_ERROR;

  /* Do the loop with prefix and token */
  while (TRUE)
    {
      svn_string_t *token =
        APR_ARRAY_IDX(source->tokens, btn->current_token[tokens],
                      svn_string_t *);

      if (type != unified_output_skip)
        {
          svn_stringbuf_appendcstr(btn->hunk, btn->prefix_str[type]);
          svn_stringbuf_appendbytes(btn->hunk, token->data, token->len);
        }

      if (type == unified_output_context)
        {
          btn->hunk_length[0]++;
          btn->hunk_length[1]++;
        }
      else if (type == unified_output_delete)
        btn->hunk_length[0]++;
      else if (type == unified_output_insert)
        btn->hunk_length[1]++;

      /* ### TODO: Add skip processing for -p handling? */

      btn->current_token[tokens]++;
      if (btn->current_token[tokens] == until)
        break;
    }

  if (btn->current_token[tokens] == source->tokens->nelts
      && source->ends_without_eol)
    {
      const char *out_str;

      SVN_ERR(svn_utf_cstring_from_utf8_ex2(
                &out_str, btn->no_newline_string,
                btn->header_encoding, btn->pool));
      svn_stringbuf_appendcstr(btn->hunk, out_str);
    }



  return SVN_NO_ERROR;
}

/* Flush the hunk currently built up in BATON
   into the BATON's output_stream.
   Use the specified HUNK_DELIMITER.
   If HUNK_DELIMITER is NULL, fall back to the default delimiter. */
static svn_error_t *
output_unified_flush_hunk(output_baton_t *baton,
                          const char *hunk_delimiter)
{
  apr_off_t target_token;
  apr_size_t hunk_len;
  apr_off_t old_start;
  apr_off_t new_start;

  if (svn_stringbuf_isempty(baton->hunk))
    return SVN_NO_ERROR;

  svn_pool_clear(baton->pool);

  /* Write the trailing context */
  target_token = baton->hunk_start[0] + baton->hunk_length[0]
                 + baton->context_size;
  SVN_ERR(output_unified_token_range(baton, 0 /*original*/,
                                     unified_output_context,
                                     target_token));
  if (hunk_delimiter == NULL)
    hunk_delimiter = "@@";

  old_start = baton->hunk_start[0];
  new_start = baton->hunk_start[1];

  /* If the file is non-empty, convert the line indexes from
     zero based to one based */
  if (baton->hunk_length[0])
    old_start++;
  if (baton->hunk_length[1])
    new_start++;

  /* Write the hunk header */
  SVN_ERR(svn_diff__unified_write_hunk_header(
            baton->output_stream, baton->header_encoding, hunk_delimiter,
            old_start, baton->hunk_length[0],
            new_start, baton->hunk_length[1],
            NULL /* hunk_extra_context */,
            baton->pool));

  hunk_len = baton->hunk->len;
  SVN_ERR(svn_stream_write(baton->output_stream,
                           baton->hunk->data, &hunk_len));

  /* Prepare for the next hunk */
  baton->hunk_length[0] = 0;
  baton->hunk_length[1] = 0;
  baton->hunk_start[0] = 0;
  baton->hunk_start[1] = 0;
  svn_stringbuf_setempty(baton->hunk);

  return SVN_NO_ERROR;
}

/* Implements svn_diff_output_fns_t::output_diff_modified */
static svn_error_t *
output_unified_diff_modified(void *baton,
                             apr_off_t original_start,
                             apr_off_t original_length,
                             apr_off_t modified_start,
                             apr_off_t modified_length,
                             apr_off_t latest_start,
                             apr_off_t latest_length)
{
  output_baton_t *output_baton = baton;
  apr_off_t context_prefix_length;
  apr_off_t prev_context_end;
  svn_boolean_t init_hunk = FALSE;

  if (original_start > output_baton->context_size)
    context_prefix_length = output_baton->context_size;
  else
    context_prefix_length = original_start;

  /* Calculate where the previous hunk will end if we would write it now
     (including the necessary context at the end) */
  if (output_baton->hunk_length[0] > 0 || output_baton->hunk_length[1] > 0)
    {
      prev_context_end = output_baton->hunk_start[0]
                         + output_baton->hunk_length[0]
                         + output_baton->context_size;
    }
  else
    {
      prev_context_end = -1;

      if (output_baton->hunk_start[0] == 0
          && (original_length > 0 || modified_length > 0))
        init_hunk = TRUE;
    }

  /* If the changed range is far enough from the previous range, flush the current
     hunk. */
  {
    apr_off_t new_hunk_start = (original_start - context_prefix_length);

    if (output_baton->current_token[0] < new_hunk_start
          && prev_context_end <= new_hunk_start)
      {
        SVN_ERR(output_unified_flush_hunk(output_baton,
                                          output_baton->hunk_delimiter));
        init_hunk = TRUE;
      }
    else if (output_baton->hunk_length[0] > 0
             || output_baton->hunk_length[1] > 0)
      {
        /* We extend the current hunk */

        /* Original: Output the context preceding the changed range */
        SVN_ERR(output_unified_token_range(output_baton, 0 /* original */,
                                           unified_output_context,
                                           original_start));
      }
  }

  /* Original: Skip lines until we are at the beginning of the context we want
     to display */
  SVN_ERR(output_unified_token_range(output_baton, 0 /* original */,
                                     unified_output_skip,
                                     original_start - context_prefix_length));

  if (init_hunk)
    {
      SVN_ERR_ASSERT(output_baton->hunk_length[0] == 0
                     && output_baton->hunk_length[1] == 0);

      output_baton->hunk_start[0] = original_start - context_prefix_length;
      output_baton->hunk_start[1] = modified_start - context_prefix_length;
    }

  /* Modified: Skip lines until we are at the start of the changed range */
  SVN_ERR(output_unified_token_range(output_baton, 1 /* modified */,
                                     unified_output_skip,
                                     modified_start));

  /* Original: Output the context preceding the changed range */
  SVN_ERR(output_unified_token_range(output_baton, 0 /* original */,
                                    unified_output_context,
                                    original_start));

  /* Both: Output the changed range */
  SVN_ERR(output_unified_token_range(output_baton, 0 /* original */,
                                     unified_output_delete,
                                     original_start + original_length));
  SVN_ERR(output_unified_token_range(output_baton, 1 /* modified */,
                                     unified_output_insert,
                                     modified_start + modified_length));

  return SVN_NO_ERROR;
}

static const svn_diff_output_fns_t mem_output_unified_vtable =
{
  NULL, /* output_common */
  output_unified_diff_modified,
  NULL, /* output_diff_latest */
  NULL, /* output_diff_common */
  NULL  /* output_conflict */
};


svn_error_t *
svn_diff_mem_string_output_unified3(svn_stream_t *output_stream,
                                    svn_diff_t *diff,
                                    svn_boolean_t with_diff_header,
                                    const char *hunk_delimiter,
                                    const char *original_header,
                                    const char *modified_header,
                                    const char *header_encoding,
                                    const svn_string_t *original,
                                    const svn_string_t *modified,
                                    int context_size,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *scratch_pool)
{

  if (svn_diff_contains_diffs(diff))
    {
      output_baton_t baton;

      memset(&baton, 0, sizeof(baton));
      baton.output_stream = output_stream;
      baton.pool = svn_pool_create(scratch_pool);
      baton.header_encoding = header_encoding;
      baton.hunk = svn_stringbuf_create_empty(scratch_pool);
      baton.hunk_delimiter = hunk_delimiter;
      baton.no_newline_string
        = (hunk_delimiter == NULL || strcmp(hunk_delimiter, "##") != 0)
          ? APR_EOL_STR SVN_DIFF__NO_NEWLINE_AT_END_OF_FILE APR_EOL_STR
          : APR_EOL_STR SVN_DIFF__NO_NEWLINE_AT_END_OF_PROPERTY APR_EOL_STR;
      baton.context_size = context_size >= 0 ? context_size
                                             : SVN_DIFF__UNIFIED_CONTEXT_SIZE;

      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_context]), " ",
               header_encoding, scratch_pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_delete]), "-",
               header_encoding, scratch_pool));
      SVN_ERR(svn_utf_cstring_from_utf8_ex2
              (&(baton.prefix_str[unified_output_insert]), "+",
               header_encoding, scratch_pool));

      fill_source_tokens(&baton.sources[0], original, scratch_pool);
      fill_source_tokens(&baton.sources[1], modified, scratch_pool);

      if (with_diff_header)
        {
          SVN_ERR(svn_diff__unidiff_write_header(
                    output_stream, header_encoding,
                    original_header, modified_header, scratch_pool));
        }

      SVN_ERR(svn_diff_output2(diff, &baton,
                              &mem_output_unified_vtable,
                              cancel_func, cancel_baton));

      SVN_ERR(output_unified_flush_hunk(&baton, hunk_delimiter));

      svn_pool_destroy(baton.pool);
    }

  return SVN_NO_ERROR;
}




/* diff3 merge output */

/* A stream to remember *leading* context.  Note that this stream does
   *not* copy the data that it is remembering; it just saves
   *pointers! */
typedef struct context_saver_t {
  svn_stream_t *stream;
  int context_size;
  const char **data; /* const char *data[context_size] */
  apr_size_t *len;   /* apr_size_t len[context_size] */
  apr_size_t next_slot;
  apr_ssize_t total_writes;
} context_saver_t;


static svn_error_t *
context_saver_stream_write(void *baton,
                           const char *data,
                           apr_size_t *len)
{
  context_saver_t *cs = baton;
  cs->data[cs->next_slot] = data;
  cs->len[cs->next_slot] = *len;
  cs->next_slot = (cs->next_slot + 1) % cs->context_size;
  cs->total_writes++;
  return SVN_NO_ERROR;
}


typedef struct merge_output_baton_t
{
  svn_stream_t *output_stream;

  /* Tokenized source text */
  source_tokens_t sources[3];
  apr_off_t next_token[3];

  /* Markers for marking conflicted sections */
  const char *markers[4]; /* 0 = original, 1 = modified,
                             2 = separator, 3 = latest (end) */
  const char *marker_eol;

  svn_diff_conflict_display_style_t conflict_style;
  int context_size;

  /* cancel support */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The rest of the fields are for
     svn_diff_conflict_display_only_conflicts only.  Note that for
     these batons, OUTPUT_STREAM is either CONTEXT_SAVER->STREAM or
     (soon after a conflict) a "trailing context stream", never the
     actual output stream.*/
  /* The actual output stream. */
  svn_stream_t *real_output_stream;
  context_saver_t *context_saver;
  /* Used to allocate context_saver and trailing context streams, and
     for some printfs. */
  apr_pool_t *pool;
} merge_output_baton_t;


static svn_error_t *
flush_context_saver(context_saver_t *cs,
                    svn_stream_t *output_stream)
{
  int i;
  for (i = 0; i < cs->context_size; i++)
    {
      apr_size_t slot = (i + cs->next_slot) % cs->context_size;
      if (cs->data[slot])
        {
          apr_size_t len = cs->len[slot];
          SVN_ERR(svn_stream_write(output_stream, cs->data[slot], &len));
        }
    }
  return SVN_NO_ERROR;
}


static void
make_context_saver(merge_output_baton_t *mob)
{
  context_saver_t *cs;

  assert(mob->context_size > 0); /* Or nothing to save */

  svn_pool_clear(mob->pool);
  cs = apr_pcalloc(mob->pool, sizeof(*cs));
  cs->stream = svn_stream_empty(mob->pool);
  svn_stream_set_baton(cs->stream, cs);
  svn_stream_set_write(cs->stream, context_saver_stream_write);
  mob->context_saver = cs;
  mob->output_stream = cs->stream;
  cs->context_size = mob->context_size;
  cs->data = apr_pcalloc(mob->pool, sizeof(*cs->data) * cs->context_size);
  cs->len = apr_pcalloc(mob->pool, sizeof(*cs->len) * cs->context_size);
}


/* A stream which prints LINES_TO_PRINT (based on context_size) lines to
   BATON->REAL_OUTPUT_STREAM, and then changes BATON->OUTPUT_STREAM to
   a context_saver; used for *trailing* context. */

struct trailing_context_printer {
  apr_size_t lines_to_print;
  merge_output_baton_t *mob;
};


static svn_error_t *
trailing_context_printer_write(void *baton,
                               const char *data,
                               apr_size_t *len)
{
  struct trailing_context_printer *tcp = baton;
  SVN_ERR_ASSERT(tcp->lines_to_print > 0);
  SVN_ERR(svn_stream_write(tcp->mob->real_output_stream, data, len));
  tcp->lines_to_print--;
  if (tcp->lines_to_print == 0)
    make_context_saver(tcp->mob);
  return SVN_NO_ERROR;
}


static void
make_trailing_context_printer(merge_output_baton_t *btn)
{
  struct trailing_context_printer *tcp;
  svn_stream_t *s;

  svn_pool_clear(btn->pool);

  tcp = apr_pcalloc(btn->pool, sizeof(*tcp));
  tcp->lines_to_print = btn->context_size;
  tcp->mob = btn;
  s = svn_stream_empty(btn->pool);
  svn_stream_set_baton(s, tcp);
  svn_stream_set_write(s, trailing_context_printer_write);
  btn->output_stream = s;
}


static svn_error_t *
output_merge_token_range(merge_output_baton_t *btn,
                         int idx, apr_off_t first,
                         apr_off_t length)
{
  apr_array_header_t *tokens = btn->sources[idx].tokens;

  for (; length > 0 && first < tokens->nelts; length--, first++)
    {
      svn_string_t *token = APR_ARRAY_IDX(tokens, first, svn_string_t *);
      apr_size_t len = token->len;

      /* Note that the trailing context printer assumes that
         svn_stream_write is called exactly once per line. */
      SVN_ERR(svn_stream_write(btn->output_stream, token->data, &len));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
output_marker_eol(merge_output_baton_t *btn)
{
  return svn_stream_puts(btn->output_stream, btn->marker_eol);
}

static svn_error_t *
output_merge_marker(merge_output_baton_t *btn, int idx)
{
  SVN_ERR(svn_stream_puts(btn->output_stream, btn->markers[idx]));
  return output_marker_eol(btn);
}

static svn_error_t *
output_common_modified(void *baton,
                       apr_off_t original_start, apr_off_t original_length,
                       apr_off_t modified_start, apr_off_t modified_length,
                       apr_off_t latest_start, apr_off_t latest_length)
{
  return output_merge_token_range(baton, 1/*modified*/,
                                  modified_start, modified_length);
}

static svn_error_t *
output_latest(void *baton,
              apr_off_t original_start, apr_off_t original_length,
              apr_off_t modified_start, apr_off_t modified_length,
              apr_off_t latest_start, apr_off_t latest_length)
{
  return output_merge_token_range(baton, 2/*latest*/,
                                  latest_start, latest_length);
}

static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff);

static const svn_diff_output_fns_t merge_output_vtable =
{
  output_common_modified, /* common */
  output_common_modified, /* modified */
  output_latest,
  output_common_modified, /* output_diff_common */
  output_conflict
};

static svn_error_t *
output_conflict(void *baton,
                apr_off_t original_start, apr_off_t original_length,
                apr_off_t modified_start, apr_off_t modified_length,
                apr_off_t latest_start, apr_off_t latest_length,
                svn_diff_t *diff)
{
  merge_output_baton_t *btn = baton;

  svn_diff_conflict_display_style_t style = btn->conflict_style;

  if (style == svn_diff_conflict_display_resolved_modified_latest)
    {
      if (diff)
        return svn_diff_output2(diff, baton, &merge_output_vtable,
                                btn->cancel_func, btn->cancel_baton);
      else
        style = svn_diff_conflict_display_modified_latest;
    }

  if (style == svn_diff_conflict_display_modified_latest ||
      style == svn_diff_conflict_display_modified_original_latest)
    {
      SVN_ERR(output_merge_marker(btn, 1/*modified*/));
      SVN_ERR(output_merge_token_range(btn, 1/*modified*/,
                                       modified_start, modified_length));

      if (style == svn_diff_conflict_display_modified_original_latest)
        {
          SVN_ERR(output_merge_marker(btn, 0/*original*/));
          SVN_ERR(output_merge_token_range(btn, 0/*original*/,
                                           original_start, original_length));
        }

      SVN_ERR(output_merge_marker(btn, 2/*separator*/));
      SVN_ERR(output_merge_token_range(btn, 2/*latest*/,
                                       latest_start, latest_length));
      SVN_ERR(output_merge_marker(btn, 3/*latest (end)*/));
    }
  else if (style == svn_diff_conflict_display_modified)
      SVN_ERR(output_merge_token_range(btn, 1/*modified*/,
                                       modified_start, modified_length));
  else if (style == svn_diff_conflict_display_latest)
      SVN_ERR(output_merge_token_range(btn, 2/*latest*/,
                                       latest_start, latest_length));
  else /* unknown style */
    SVN_ERR_MALFUNCTION();

  return SVN_NO_ERROR;
}

static svn_error_t *
output_conflict_with_context_marker(merge_output_baton_t *btn,
                                    const char *label,
                                    apr_off_t start,
                                    apr_off_t length)
{
  if (length == 1)
    SVN_ERR(svn_stream_printf(btn->output_stream, btn->pool,
                              "%s (%" APR_OFF_T_FMT ")",
                              label, start + 1));
  else
    SVN_ERR(svn_stream_printf(btn->output_stream, btn->pool,
                              "%s (%" APR_OFF_T_FMT ",%" APR_OFF_T_FMT ")",
                              label, start + 1, length));

  SVN_ERR(output_marker_eol(btn));

  return SVN_NO_ERROR;
}

static svn_error_t *
output_conflict_with_context(void *baton,
                             apr_off_t original_start,
                             apr_off_t original_length,
                             apr_off_t modified_start,
                             apr_off_t modified_length,
                             apr_off_t latest_start,
                             apr_off_t latest_length,
                             svn_diff_t *diff)
{
  merge_output_baton_t *btn = baton;

  /* Are we currently saving starting context (as opposed to printing
     trailing context)?  If so, flush it. */
  if (btn->output_stream == btn->context_saver->stream)
    {
      if (btn->context_saver->total_writes > btn->context_size)
        SVN_ERR(svn_stream_puts(btn->real_output_stream, "@@\n"));
      SVN_ERR(flush_context_saver(btn->context_saver, btn->real_output_stream));
    }

  /* Print to the real output stream. */
  btn->output_stream = btn->real_output_stream;

  /* Output the conflict itself. */
  SVN_ERR(output_conflict_with_context_marker(btn, btn->markers[1],
                                              modified_start,
                                              modified_length));
  SVN_ERR(output_merge_token_range(btn, 1/*modified*/,
                                   modified_start, modified_length));

  SVN_ERR(output_conflict_with_context_marker(btn, btn->markers[0],
                                              original_start,
                                              original_length));
  SVN_ERR(output_merge_token_range(btn, 0/*original*/,
                                   original_start, original_length));

  SVN_ERR(output_merge_marker(btn, 2/*separator*/));
  SVN_ERR(output_merge_token_range(btn, 2/*latest*/,
                                   latest_start, latest_length));
  SVN_ERR(output_conflict_with_context_marker(btn, btn->markers[3],
                                              latest_start,
                                              latest_length));

  /* Go into print-trailing-context mode instead. */
  make_trailing_context_printer(btn);

  return SVN_NO_ERROR;
}


static const svn_diff_output_fns_t merge_only_conflicts_output_vtable =
{
  output_common_modified,
  output_common_modified,
  output_latest,
  output_common_modified,
  output_conflict_with_context
};


/* TOKEN is the first token in the modified file.
   Return its line-ending, if any. */
static const char *
detect_eol(svn_string_t *token)
{
  const char *curp;

  if (token->len == 0)
    return NULL;

  curp = token->data + token->len - 1;
  if (*curp == '\r')
    return "\r";
  else if (*curp != '\n')
    return NULL;
  else
    {
      if (token->len == 1
          || *(--curp) != '\r')
        return "\n";
      else
        return "\r\n";
    }
}

svn_error_t *
svn_diff_mem_string_output_merge3(svn_stream_t *output_stream,
                                  svn_diff_t *diff,
                                  const svn_string_t *original,
                                  const svn_string_t *modified,
                                  const svn_string_t *latest,
                                  const char *conflict_original,
                                  const char *conflict_modified,
                                  const char *conflict_latest,
                                  const char *conflict_separator,
                                  svn_diff_conflict_display_style_t style,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *scratch_pool)
{
  merge_output_baton_t btn;
  const char *eol;
  svn_boolean_t conflicts_only =
    (style == svn_diff_conflict_display_only_conflicts);
  const svn_diff_output_fns_t *vtable = conflicts_only
     ? &merge_only_conflicts_output_vtable : &merge_output_vtable;

  memset(&btn, 0, sizeof(btn));
  btn.context_size = SVN_DIFF__UNIFIED_CONTEXT_SIZE;

  if (conflicts_only)
    {
      btn.pool = svn_pool_create(scratch_pool);
      make_context_saver(&btn);
      btn.real_output_stream = output_stream;
    }
  else
    btn.output_stream = output_stream;

  fill_source_tokens(&(btn.sources[0]), original, scratch_pool);
  fill_source_tokens(&(btn.sources[1]), modified, scratch_pool);
  fill_source_tokens(&(btn.sources[2]), latest, scratch_pool);

  btn.conflict_style = style;

  if (btn.sources[1].tokens->nelts > 0)
    {
      eol = detect_eol(APR_ARRAY_IDX(btn.sources[1].tokens, 0, svn_string_t *));
      if (!eol)
        eol = APR_EOL_STR;  /* use the platform default */
    }
  else
    eol = APR_EOL_STR;  /* use the platform default */

  btn.marker_eol = eol;
  btn.cancel_func = cancel_func;
  btn.cancel_baton = cancel_baton;

  SVN_ERR(svn_utf_cstring_from_utf8(&btn.markers[1],
                                    conflict_modified
                                    ? conflict_modified
                                    : "<<<<<<< (modified)",
                                    scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&btn.markers[0],
                                    conflict_original
                                    ? conflict_original
                                    : "||||||| (original)",
                                    scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&btn.markers[2],
                                    conflict_separator
                                    ? conflict_separator
                                    : "=======",
                                    scratch_pool));
  SVN_ERR(svn_utf_cstring_from_utf8(&btn.markers[3],
                                    conflict_latest
                                    ? conflict_latest
                                    : ">>>>>>> (latest)",
                                    scratch_pool));

  SVN_ERR(svn_diff_output2(diff, &btn, vtable, cancel_func, cancel_baton));
  if (conflicts_only)
    svn_pool_destroy(btn.pool);

  return SVN_NO_ERROR;
}
