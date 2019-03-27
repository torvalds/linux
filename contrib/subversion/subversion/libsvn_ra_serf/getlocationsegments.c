/*
 * getlocationsegments.c :  entry point for get_location_segments
 *                          RA functions for ra_serf
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
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_private_config.h"
#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"



typedef struct gls_context_t {
  /* parameters set by our caller */
  svn_revnum_t peg_revision;
  svn_revnum_t start_rev;
  svn_revnum_t end_rev;
  const char *path;

  /* location segment callback function/baton */
  svn_location_segment_receiver_t receiver;
  void *receiver_baton;

} gls_context_t;

enum locseg_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  SEGMENT
};

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t gls_ttable[] = {
  { INITIAL, S_, "get-location-segments-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, S_, "location-segment", SEGMENT,
    FALSE, { "?path", "range-start", "range-end", NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
gls_closed(svn_ra_serf__xml_estate_t *xes,
           void *baton,
           int leaving_state,
           const svn_string_t *cdata,
           apr_hash_t *attrs,
           apr_pool_t *scratch_pool)
{
  gls_context_t *gls_ctx = baton;
  const char *path;
  const char *start_str;
  const char *end_str;
  apr_int64_t start_val;
  apr_int64_t end_val;
  svn_location_segment_t segment;

  SVN_ERR_ASSERT(leaving_state == SEGMENT);

  path = svn_hash_gets(attrs, "path");
  start_str = svn_hash_gets(attrs, "range-start");
  end_str = svn_hash_gets(attrs, "range-end");

  /* The transition table said these must exist.  */
  SVN_ERR_ASSERT(start_str && end_str);

  SVN_ERR(svn_cstring_atoi64(&start_val, start_str));
  SVN_ERR(svn_cstring_atoi64(&end_val, end_str));

  segment.path = path;  /* may be NULL  */
  segment.range_start = (svn_revnum_t)start_val;
  segment.range_end = (svn_revnum_t)end_val;
  SVN_ERR(gls_ctx->receiver(&segment, gls_ctx->receiver_baton, scratch_pool));

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_gls_body(serf_bucket_t **body_bkt,
                void *baton,
                serf_bucket_alloc_t *alloc,
                apr_pool_t *pool /* request pool */,
                apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  gls_context_t *gls_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:get-location-segments",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", gls_ctx->path,
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:peg-revision",
                               apr_ltoa(pool, gls_ctx->peg_revision),
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:start-revision",
                               apr_ltoa(pool, gls_ctx->start_rev),
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision",
                               apr_ltoa(pool, gls_ctx->end_rev),
                               alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:get-location-segments");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_location_segments(svn_ra_session_t *ra_session,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   svn_revnum_t start_rev,
                                   svn_revnum_t end_rev,
                                   svn_location_segment_receiver_t receiver,
                                   void *receiver_baton,
                                   apr_pool_t *pool)
{
  gls_context_t *gls_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;
  svn_error_t *err;

  gls_ctx = apr_pcalloc(pool, sizeof(*gls_ctx));
  gls_ctx->path = path;
  gls_ctx->peg_revision = peg_revision;
  gls_ctx->start_rev = start_rev;
  gls_ctx->end_rev = end_rev;
  gls_ctx->receiver = receiver;
  gls_ctx->receiver_baton = receiver_baton;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session, NULL /* url */, peg_revision,
                                      pool, pool));

  xmlctx = svn_ra_serf__xml_context_create(gls_ttable,
                                           NULL, gls_closed, NULL,
                                           gls_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_delegate = create_gls_body;
  handler->body_delegate_baton = gls_ctx;
  handler->body_type = "text/xml";

  err = svn_ra_serf__context_run_one(handler, pool);

  if (!err && handler->sline.code != 200)
    err = svn_ra_serf__unexpected_status(handler);

  if (err && (err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE))
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err, NULL);

  return svn_error_trace(err);
}
