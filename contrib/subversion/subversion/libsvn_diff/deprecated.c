/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_diff.h"
#include "svn_utf.h"

#include "svn_private_config.h"




/*** Code. ***/
struct fns_wrapper_baton
{
  /* We put the old baton in front of this one, so that we can still use
     this baton in place of the old.  This prevents us from having to
     implement simple wrappers around each member of diff_fns_t. */
  void *old_baton;
  const svn_diff_fns_t *vtable;
};

static svn_error_t *
datasources_open(void *baton,
                 apr_off_t *prefix_lines,
                 apr_off_t *suffix_lines,
                 const svn_diff_datasource_e *datasources,
                 apr_size_t datasource_len)
{
  struct fns_wrapper_baton *fwb = baton;
  apr_size_t i;

  /* Just iterate over the datasources, using the old singular version. */
  for (i = 0; i < datasource_len; i++)
    {
      SVN_ERR(fwb->vtable->datasource_open(fwb->old_baton, datasources[i]));
    }

  /* Don't claim any prefix or suffix matches. */
  *prefix_lines = 0;
  *suffix_lines = 0;

  return SVN_NO_ERROR;
}

static svn_error_t *
datasource_close(void *baton,
                 svn_diff_datasource_e datasource)
{
  struct fns_wrapper_baton *fwb = baton;
  return fwb->vtable->datasource_close(fwb->old_baton, datasource);
}

static svn_error_t *
datasource_get_next_token(apr_uint32_t *hash,
                          void **token,
                          void *baton,
                          svn_diff_datasource_e datasource)
{
  struct fns_wrapper_baton *fwb = baton;
  return fwb->vtable->datasource_get_next_token(hash, token, fwb->old_baton,
                                                datasource);
}

static svn_error_t *
token_compare(void *baton,
              void *ltoken,
              void *rtoken,
              int *compare)
{
  struct fns_wrapper_baton *fwb = baton;
  return fwb->vtable->token_compare(fwb->old_baton, ltoken, rtoken, compare);
}

static void
token_discard(void *baton,
              void *token)
{
  struct fns_wrapper_baton *fwb = baton;
  fwb->vtable->token_discard(fwb->old_baton, token);
}

static void
token_discard_all(void *baton)
{
  struct fns_wrapper_baton *fwb = baton;
  fwb->vtable->token_discard_all(fwb->old_baton);
}


static void
wrap_diff_fns(svn_diff_fns2_t **diff_fns2,
              struct fns_wrapper_baton **baton2,
              const svn_diff_fns_t *diff_fns,
              void *baton,
              apr_pool_t *result_pool)
{
  /* Initialize the return vtable. */
  *diff_fns2 = apr_palloc(result_pool, sizeof(**diff_fns2));

  (*diff_fns2)->datasources_open = datasources_open;
  (*diff_fns2)->datasource_close = datasource_close;
  (*diff_fns2)->datasource_get_next_token = datasource_get_next_token;
  (*diff_fns2)->token_compare = token_compare;
  (*diff_fns2)->token_discard = token_discard;
  (*diff_fns2)->token_discard_all = token_discard_all;

  /* Initialize the wrapper baton. */
  *baton2 = apr_palloc(result_pool, sizeof (**baton2));
  (*baton2)->old_baton = baton;
  (*baton2)->vtable = diff_fns;
}


/*** From diff_file.c ***/

svn_error_t *
svn_diff_file_output_unified3(svn_stream_t *output_stream,
                              svn_diff_t *diff,
                              const char *original_path,
                              const char *modified_path,
                              const char *original_header,
                              const char *modified_header,
                              const char *header_encoding,
                              const char *relative_to_dir,
                              svn_boolean_t show_c_function,
                              apr_pool_t *pool)
{
  return svn_error_trace(
              svn_diff_file_output_unified4(output_stream,
                                            diff,
                                            original_path,
                                            modified_path,
                                            original_header,
                                            modified_header,
                                            header_encoding,
                                            relative_to_dir,
                                            show_c_function,
                                            -1 /* context_size */,
                                            NULL, NULL, /* cancel */
                                            pool));
}

svn_error_t *
svn_diff_file_output_unified2(svn_stream_t *output_stream,
                              svn_diff_t *diff,
                              const char *original_path,
                              const char *modified_path,
                              const char *original_header,
                              const char *modified_header,
                              const char *header_encoding,
                              apr_pool_t *pool)
{
  return svn_diff_file_output_unified3(output_stream, diff,
                                       original_path, modified_path,
                                       original_header, modified_header,
                                       header_encoding, NULL, FALSE, pool);
}

svn_error_t *
svn_diff_file_output_unified(svn_stream_t *output_stream,
                             svn_diff_t *diff,
                             const char *original_path,
                             const char *modified_path,
                             const char *original_header,
                             const char *modified_header,
                             apr_pool_t *pool)
{
  return svn_diff_file_output_unified2(output_stream, diff,
                                       original_path, modified_path,
                                       original_header, modified_header,
                                       SVN_APR_LOCALE_CHARSET, pool);
}

svn_error_t *
svn_diff_file_diff(svn_diff_t **diff,
                   const char *original,
                   const char *modified,
                   apr_pool_t *pool)
{
  return svn_diff_file_diff_2(diff, original, modified,
                              svn_diff_file_options_create(pool), pool);
}

svn_error_t *
svn_diff_file_diff3(svn_diff_t **diff,
                    const char *original,
                    const char *modified,
                    const char *latest,
                    apr_pool_t *pool)
{
  return svn_diff_file_diff3_2(diff, original, modified, latest,
                               svn_diff_file_options_create(pool), pool);
}

svn_error_t *
svn_diff_file_diff4(svn_diff_t **diff,
                    const char *original,
                    const char *modified,
                    const char *latest,
                    const char *ancestor,
                    apr_pool_t *pool)
{
  return svn_diff_file_diff4_2(diff, original, modified, latest, ancestor,
                               svn_diff_file_options_create(pool), pool);
}

svn_error_t *
svn_diff_file_output_merge(svn_stream_t *output_stream,
                           svn_diff_t *diff,
                           const char *original_path,
                           const char *modified_path,
                           const char *latest_path,
                           const char *conflict_original,
                           const char *conflict_modified,
                           const char *conflict_latest,
                           const char *conflict_separator,
                           svn_boolean_t display_original_in_conflict,
                           svn_boolean_t display_resolved_conflicts,
                           apr_pool_t *pool)
{
  svn_diff_conflict_display_style_t style =
    svn_diff_conflict_display_modified_latest;

  if (display_resolved_conflicts)
    style = svn_diff_conflict_display_resolved_modified_latest;

  if (display_original_in_conflict)
    style = svn_diff_conflict_display_modified_original_latest;

  return svn_diff_file_output_merge2(output_stream,
                                     diff,
                                     original_path,
                                     modified_path,
                                     latest_path,
                                     conflict_original,
                                     conflict_modified,
                                     conflict_latest,
                                     conflict_separator,
                                     style,
                                     pool);
}

svn_error_t *
svn_diff_file_output_merge2(svn_stream_t *output_stream,
                            svn_diff_t *diff,
                            const char *original_path,
                            const char *modified_path,
                            const char *latest_path,
                            const char *conflict_original,
                            const char *conflict_modified,
                            const char *conflict_latest,
                            const char *conflict_separator,
                            svn_diff_conflict_display_style_t conflict_style,
                            apr_pool_t *pool)
{
  return svn_error_trace(svn_diff_file_output_merge3(output_stream,
                                                     diff, original_path,
                                                     modified_path,
                                                     latest_path,
                                                     conflict_original,
                                                     conflict_modified,
                                                     conflict_latest,
                                                     conflict_separator,
                                                     conflict_style,
                                                     NULL, NULL, /* cancel */
                                                     pool));
}

/*** From diff.c ***/
svn_error_t *
svn_diff_diff(svn_diff_t **diff,
              void *diff_baton,
              const svn_diff_fns_t *vtable,
              apr_pool_t *pool)
{
  svn_diff_fns2_t *diff_fns2;
  struct fns_wrapper_baton *fwb;

  wrap_diff_fns(&diff_fns2, &fwb, vtable, diff_baton, pool);
  return svn_diff_diff_2(diff, fwb, diff_fns2, pool);
}


/*** From diff3.c ***/
svn_error_t *
svn_diff_diff3(svn_diff_t **diff,
               void *diff_baton,
               const svn_diff_fns_t *vtable,
               apr_pool_t *pool)
{
  svn_diff_fns2_t *diff_fns2;
  struct fns_wrapper_baton *fwb;

  wrap_diff_fns(&diff_fns2, &fwb, vtable, diff_baton, pool);
  return svn_diff_diff3_2(diff, fwb, diff_fns2, pool);
}


/*** From diff4.c ***/
svn_error_t *
svn_diff_diff4(svn_diff_t **diff,
               void *diff_baton,
               const svn_diff_fns_t *vtable,
               apr_pool_t *pool)
{
  svn_diff_fns2_t *diff_fns2;
  struct fns_wrapper_baton *fwb;

  wrap_diff_fns(&diff_fns2, &fwb, vtable, diff_baton, pool);
  return svn_diff_diff4_2(diff, fwb, diff_fns2, pool);
}

/*** From util.c ***/
svn_error_t *
svn_diff_output(svn_diff_t *diff,
                void *output_baton,
                const svn_diff_output_fns_t *output_fns)
{
  return svn_error_trace(svn_diff_output2(diff, output_baton, output_fns,
                                          NULL, NULL /* cancel */));
}

/*** From diff_memory.c ***/
svn_error_t *
svn_diff_mem_string_output_merge(svn_stream_t *output_stream,
                                 svn_diff_t *diff,
                                 const svn_string_t *original,
                                 const svn_string_t *modified,
                                 const svn_string_t *latest,
                                 const char *conflict_original,
                                 const char *conflict_modified,
                                 const char *conflict_latest,
                                 const char *conflict_separator,
                                 svn_boolean_t display_original_in_conflict,
                                 svn_boolean_t display_resolved_conflicts,
                                 apr_pool_t *pool)
{
  svn_diff_conflict_display_style_t style =
    svn_diff_conflict_display_modified_latest;

  if (display_resolved_conflicts)
    style = svn_diff_conflict_display_resolved_modified_latest;

  if (display_original_in_conflict)
    style = svn_diff_conflict_display_modified_original_latest;

  return svn_diff_mem_string_output_merge2(output_stream,
                                           diff,
                                           original,
                                           modified,
                                           latest,
                                           conflict_original,
                                           conflict_modified,
                                           conflict_latest,
                                           conflict_separator,
                                           style,
                                           pool);
}

svn_error_t *
svn_diff_mem_string_output_merge2(svn_stream_t *output_stream,
                                  svn_diff_t *diff,
                                  const svn_string_t *original,
                                  const svn_string_t *modified,
                                  const svn_string_t *latest,
                                  const char *conflict_original,
                                  const char *conflict_modified,
                                  const char *conflict_latest,
                                  const char *conflict_separator,
                                  svn_diff_conflict_display_style_t style,
                                  apr_pool_t *pool)
{
  return svn_error_trace(svn_diff_mem_string_output_merge3(output_stream, diff,
                                                           original,
                                                           modified, latest,
                                                           conflict_original,
                                                           conflict_modified,
                                                           conflict_latest,
                                                           conflict_separator,
                                                           style,
                                                           /* no cancelation */
                                                           NULL, NULL,
                                                           pool));
}

svn_error_t *
svn_diff_mem_string_output_unified(svn_stream_t *output_stream,
                                   svn_diff_t *diff,
                                   const char *original_header,
                                   const char *modified_header,
                                   const char *header_encoding,
                                   const svn_string_t *original,
                                   const svn_string_t *modified,
                                   apr_pool_t *pool)
{
  return svn_error_trace(svn_diff_mem_string_output_unified2(output_stream,
                                                             diff,
                                                             TRUE,
                                                             NULL,
                                                             original_header,
                                                             modified_header,
                                                             header_encoding,
                                                             original,
                                                             modified,
                                                             pool));
}

svn_error_t *
svn_diff_mem_string_output_unified2(svn_stream_t *output_stream,
                                    svn_diff_t *diff,
                                    svn_boolean_t with_diff_header,
                                    const char *hunk_delimiter,
                                    const char *original_header,
                                    const char *modified_header,
                                    const char *header_encoding,
                                    const svn_string_t *original,
                                    const svn_string_t *modified,
                                    apr_pool_t *pool)
{
  return svn_error_trace(svn_diff_mem_string_output_unified3(output_stream,
                                                             diff,
                                                             with_diff_header,
                                                             hunk_delimiter,
                                                             original_header,
                                                             modified_header,
                                                             header_encoding,
                                                             original,
                                                             modified,
                                                             -1 /* context */,
                                                             /* cancel */
                                                             NULL, NULL,
                                                             pool));
}
