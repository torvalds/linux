/*
 * getdate.c :  entry point for get_dated_revision for ra_serf
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

#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_time.h"
#include "svn_xml.h"

#include "private/svn_dav_protocol.h"

#include "svn_private_config.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum date_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  VERSION_NAME
};


typedef struct date_context_t {
  /* The time asked about. */
  apr_time_t time;

  /* What was the youngest revision at that time? */
  svn_revnum_t *revision;

} date_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t date_ttable[] = {
  { INITIAL, S_, "dated-rev-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, D_, SVN_DAV__VERSION_NAME, VERSION_NAME,
    TRUE, { NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
date_closed(svn_ra_serf__xml_estate_t *xes,
            void *baton,
            int leaving_state,
            const svn_string_t *cdata,
            apr_hash_t *attrs,
            apr_pool_t *scratch_pool)
{
  date_context_t *date_ctx = baton;
  apr_int64_t rev;

  SVN_ERR_ASSERT(leaving_state == VERSION_NAME);
  SVN_ERR_ASSERT(cdata != NULL);

  SVN_ERR(svn_cstring_atoi64(&rev, cdata->data));

  *date_ctx->revision = (svn_revnum_t)rev;

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_getdate_body(serf_bucket_t **body_bkt,
                    void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool /* request pool */,
                    apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  date_context_t *date_ctx = baton;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "S:dated-rev-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    "xmlns:D", "DAV:",
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "D:" SVN_DAV__CREATIONDATE,
                               svn_time_to_cstring(date_ctx->time, pool),
                               alloc);

  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "S:dated-rev-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_dated_revision(svn_ra_session_t *ra_session,
                                svn_revnum_t *revision,
                                apr_time_t tm,
                                apr_pool_t *pool)
{
  date_context_t *date_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *report_target;

  date_ctx = apr_palloc(pool, sizeof(*date_ctx));
  date_ctx->time = tm;
  date_ctx->revision = revision;

  SVN_ERR(svn_ra_serf__report_resource(&report_target, session, pool));

  xmlctx = svn_ra_serf__xml_context_create(date_ttable,
                                           NULL, date_closed, NULL,
                                           date_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = report_target;
  handler->body_type = "text/xml";

  handler->body_delegate = create_getdate_body;
  handler->body_delegate_baton = date_ctx;

  *date_ctx->revision = SVN_INVALID_REVNUM;

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  if (!SVN_IS_VALID_REVNUM(*revision))
    return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                            _("The REPORT response did not include "
                              "the requested properties"));

  return SVN_NO_ERROR;
}
