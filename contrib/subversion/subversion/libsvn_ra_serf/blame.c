/*
 * blame.c :  entry point for blame RA functions for ra_serf
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

#include <apr_uri.h>
#include <serf.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "svn_config.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_base64.h"
#include "svn_props.h"

#include "svn_private_config.h"

#include "private/svn_string_private.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
typedef enum blame_state_e {
  INITIAL = XML_STATE_INITIAL,
  FILE_REVS_REPORT,
  FILE_REV,
  REV_PROP,
  SET_PROP,
  REMOVE_PROP,
  MERGED_REVISION,
  TXDELTA
} blame_state_e;


typedef struct blame_context_t {
  /* pool passed to get_file_revs */
  apr_pool_t *pool;

  /* parameters set by our caller */
  const char *path;
  svn_revnum_t start;
  svn_revnum_t end;
  svn_boolean_t include_merged_revisions;

  /* blame handler and baton */
  svn_file_rev_handler_t file_rev;
  void *file_rev_baton;

  /* As we parse each FILE_REV, we collect data in these variables:
     property changes and new content.  STREAM is valid when we're
     in the TXDELTA state, processing the incoming cdata.  */
  apr_hash_t *rev_props;
  apr_array_header_t *prop_diffs;
  apr_pool_t *state_pool;  /* put property stuff in here  */

  svn_stream_t *stream;

  svn_ra_serf__session_t *session;

} blame_context_t;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t blame_ttable[] = {
  { INITIAL, S_, "file-revs-report", FILE_REVS_REPORT,
    FALSE, { NULL }, FALSE },

  { FILE_REVS_REPORT, S_, "file-rev", FILE_REV,
    FALSE, { "path", "rev", NULL }, TRUE },

  { FILE_REV, S_, "rev-prop", REV_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { FILE_REV, S_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { FILE_REV, S_, "remove-prop", REMOVE_PROP,
    FALSE, { "name", NULL }, TRUE },

  { FILE_REV, S_, "merged-revision", MERGED_REVISION,
    FALSE, { NULL }, TRUE },

  { FILE_REV, S_, "txdelta", TXDELTA,
    FALSE, { NULL }, TRUE },

  { 0 }
};

/* Conforms to svn_ra_serf__xml_opened_t  */
static svn_error_t *
blame_opened(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int entered_state,
             const svn_ra_serf__dav_props_t *tag,
             apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = baton;

  if (entered_state == FILE_REV)
    {
      apr_pool_t *state_pool = svn_ra_serf__xml_state_pool(xes);

      /* Child elements will store properties in these structures.  */
      blame_ctx->rev_props = apr_hash_make(state_pool);
      blame_ctx->prop_diffs = apr_array_make(state_pool,
                                             5, sizeof(svn_prop_t));
      blame_ctx->state_pool = state_pool;

      /* Clear this, so we can detect the absence of a TXDELTA.  */
      blame_ctx->stream = NULL;
    }
  else if (entered_state == TXDELTA)
    {
      apr_pool_t *state_pool = svn_ra_serf__xml_state_pool(xes);
      apr_hash_t *gathered = svn_ra_serf__xml_gather_since(xes, FILE_REV);
      const char *path;
      const char *rev_str;
      const char *merged_revision;
      svn_txdelta_window_handler_t txdelta;
      void *txdelta_baton;
      apr_int64_t rev;

      path = svn_hash_gets(gathered, "path");
      rev_str = svn_hash_gets(gathered, "rev");

      SVN_ERR(svn_cstring_atoi64(&rev, rev_str));
      merged_revision = svn_hash_gets(gathered, "merged-revision");

      SVN_ERR(blame_ctx->file_rev(blame_ctx->file_rev_baton,
                                  path, (svn_revnum_t)rev,
                                  blame_ctx->rev_props,
                                  merged_revision != NULL,
                                  &txdelta, &txdelta_baton,
                                  blame_ctx->prop_diffs,
                                  state_pool));

      blame_ctx->stream = svn_base64_decode(svn_txdelta_parse_svndiff(
                                              txdelta, txdelta_baton,
                                              TRUE /* error_on_early_close */,
                                              state_pool),
                                            state_pool);
    }

  return SVN_NO_ERROR;
}


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
blame_closed(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int leaving_state,
             const svn_string_t *cdata,
             apr_hash_t *attrs,
             apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = baton;

  if (leaving_state == FILE_REV)
    {
      /* Note that we test STREAM, but any pointer is currently invalid.
         It was closed when left the TXDELTA state.  */
      if (blame_ctx->stream == NULL)
        {
          const char *path;
          const char *rev;

          path = svn_hash_gets(attrs, "path");
          rev = svn_hash_gets(attrs, "rev");

          /* Send a "no content" notification.  */
          SVN_ERR(blame_ctx->file_rev(blame_ctx->file_rev_baton,
                                      path, SVN_STR_TO_REV(rev),
                                      blame_ctx->rev_props,
                                      FALSE /* result_of_merge */,
                                      NULL, NULL, /* txdelta / baton */
                                      blame_ctx->prop_diffs,
                                      scratch_pool));
        }
    }
  else if (leaving_state == MERGED_REVISION)
    {
      svn_ra_serf__xml_note(xes, FILE_REV, "merged-revision", "*");
    }
  else if (leaving_state == TXDELTA)
    {
      SVN_ERR(svn_stream_close(blame_ctx->stream));
    }
  else
    {
      const char *name;
      const svn_string_t *value;

      SVN_ERR_ASSERT(leaving_state == REV_PROP
                     || leaving_state == SET_PROP
                     || leaving_state == REMOVE_PROP);

      name = apr_pstrdup(blame_ctx->state_pool,
                         svn_hash_gets(attrs, "name"));

      if (leaving_state == REMOVE_PROP)
        {
          value = NULL;
        }
      else
        {
          const char *encoding = svn_hash_gets(attrs, "encoding");

          if (encoding && strcmp(encoding, "base64") == 0)
            value = svn_base64_decode_string(cdata, blame_ctx->state_pool);
          else
            value = svn_string_dup(cdata, blame_ctx->state_pool);
        }

      if (leaving_state == REV_PROP)
        {
          svn_hash_sets(blame_ctx->rev_props, name, value);
        }
      else
        {
          svn_prop_t *prop = apr_array_push(blame_ctx->prop_diffs);

          prop->name = name;
          prop->value = value;
        }
    }

  return SVN_NO_ERROR;
}


/* Conforms to svn_ra_serf__xml_cdata_t  */
static svn_error_t *
blame_cdata(svn_ra_serf__xml_estate_t *xes,
            void *baton,
            int current_state,
            const char *data,
            apr_size_t len,
            apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = baton;

  if (current_state == TXDELTA)
    {
      SVN_ERR(svn_stream_write(blame_ctx->stream, data, &len));
      /* Ignore the returned LEN value.  */
    }

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_file_revs_body(serf_bucket_t **body_bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool /* request pool */,
                      apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  blame_context_t *blame_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:file-revs-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision", apr_ltoa(pool, blame_ctx->start),
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision", apr_ltoa(pool, blame_ctx->end),
                               alloc);

  if (blame_ctx->include_merged_revisions)
    {
      svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                         "S:include-merged-revisions", SVN_VA_NULL);
    }

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", blame_ctx->path,
                               alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:file-revs-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_header_delegate_t */
static svn_error_t *
setup_headers(serf_bucket_t *headers,
              void *baton,
              apr_pool_t *request_pool,
              apr_pool_t *scratch_pool)
{
  blame_context_t *blame_ctx = baton;

  svn_ra_serf__setup_svndiff_accept_encoding(headers, blame_ctx->session);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_file_revs(svn_ra_session_t *ra_session,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t include_merged_revisions,
                           svn_file_rev_handler_t rev_handler,
                           void *rev_handler_baton,
                           apr_pool_t *pool)
{
  blame_context_t *blame_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;
  svn_revnum_t peg_rev;

  blame_ctx = apr_pcalloc(pool, sizeof(*blame_ctx));
  blame_ctx->pool = pool;
  blame_ctx->path = path;
  blame_ctx->file_rev = rev_handler;
  blame_ctx->file_rev_baton = rev_handler_baton;
  blame_ctx->start = start;
  blame_ctx->end = end;
  blame_ctx->include_merged_revisions = include_merged_revisions;
  blame_ctx->session = session;

  /* Since Subversion 1.8 we allow retrieving blames backwards. So we can't
     just unconditionally use end_rev as the peg revision as before */
  if (end > start)
    peg_rev = end;
  else
    peg_rev = start;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session,
                                      NULL /* url */, peg_rev,
                                      pool, pool));

  xmlctx = svn_ra_serf__xml_context_create(blame_ttable,
                                           blame_opened,
                                           blame_closed,
                                           blame_cdata,
                                           blame_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_type = "text/xml";
  handler->body_delegate = create_file_revs_body;
  handler->body_delegate_baton = blame_ctx;
  handler->custom_accept_encoding = TRUE;
  handler->header_delegate = setup_headers;
  handler->header_delegate_baton = blame_ctx;

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}
