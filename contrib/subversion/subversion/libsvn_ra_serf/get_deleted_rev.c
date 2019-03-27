/*
 * get_deleted_rev.c :  ra_serf get_deleted_rev API implementation.
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


#include "svn_ra.h"
#include "svn_xml.h"
#include "svn_path.h"
#include "svn_private_config.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum drev_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  VERSION_NAME
};

typedef struct drev_context_t {
  const char *path;
  svn_revnum_t peg_revision;
  svn_revnum_t end_revision;

  /* What revision was PATH@PEG_REVISION first deleted within
     the range PEG_REVISION-END-END_REVISION? */
  svn_revnum_t *revision_deleted;

} drev_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t getdrev_ttable[] = {
  { INITIAL, S_, "get-deleted-rev-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, D_, SVN_DAV__VERSION_NAME, VERSION_NAME,
    TRUE, { NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
getdrev_closed(svn_ra_serf__xml_estate_t *xes,
               void *baton,
               int leaving_state,
               const svn_string_t *cdata,
               apr_hash_t *attrs,
               apr_pool_t *scratch_pool)
{
  drev_context_t *drev_ctx = baton;
  apr_int64_t rev;

  SVN_ERR_ASSERT(leaving_state == VERSION_NAME);
  SVN_ERR_ASSERT(cdata != NULL);

  SVN_ERR(svn_cstring_atoi64(&rev, cdata->data));
  *drev_ctx->revision_deleted = (svn_revnum_t)rev;

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_getdrev_body(serf_bucket_t **body_bkt,
                    void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool /* request pool */,
                    apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  drev_context_t *drev_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:get-deleted-rev-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    "xmlns:D", "DAV:",
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", drev_ctx->path,
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:peg-revision",
                               apr_ltoa(pool, drev_ctx->peg_revision),
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:end-revision",
                               apr_ltoa(pool, drev_ctx->end_revision),
                               alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:get-deleted-rev-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_deleted_rev(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t end_revision,
                             svn_revnum_t *revision_deleted,
                             apr_pool_t *pool)
{
  drev_context_t *drev_ctx;
  svn_ra_serf__session_t *ras = session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;
  svn_error_t *err;

  drev_ctx = apr_pcalloc(pool, sizeof(*drev_ctx));
  drev_ctx->path = path;
  drev_ctx->peg_revision = peg_revision;
  drev_ctx->end_revision = end_revision;
  drev_ctx->revision_deleted = revision_deleted;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      ras, NULL /* url */, peg_revision,
                                      pool, pool));

  xmlctx = svn_ra_serf__xml_context_create(getdrev_ttable,
                                           NULL, getdrev_closed, NULL,
                                           drev_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(ras, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_type = "text/xml";
  handler->body_delegate = create_getdrev_body;
  handler->body_delegate_baton = drev_ctx;

  err = svn_ra_serf__context_run_one(handler, pool);

  /* Map status 501: Method Not Implemented to our not implemented error.
     1.5.x servers and older don't support this report. */
  if (handler->sline.code == 501)
    return svn_error_createf(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                             _("'%s' REPORT not implemented"),
                             "get-deleted-rev");
  SVN_ERR(err);

  return SVN_NO_ERROR;
}
