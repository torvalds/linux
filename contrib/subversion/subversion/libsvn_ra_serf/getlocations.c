/*
 * getlocations.c :  entry point for get_locations RA functions for ra_serf
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
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_xml.h"
#include "svn_private_config.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum loc_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  LOCATION
};

typedef struct loc_context_t {
  /* pool to allocate memory from */
  apr_pool_t *pool;

  /* parameters set by our caller */
  const char *path;
  const apr_array_header_t *location_revisions;
  svn_revnum_t peg_revision;

  /* Returned location hash */
  apr_hash_t *paths;

} loc_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t getloc_ttable[] = {
  { INITIAL, S_, "get-locations-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, S_, "location", LOCATION,
    FALSE, { "?rev", "?path", NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
getloc_closed(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int leaving_state,
              const svn_string_t *cdata,
              apr_hash_t *attrs,
              apr_pool_t *scratch_pool)
{
  loc_context_t *loc_ctx = baton;
  const char *revstr;
  const char *path;

  SVN_ERR_ASSERT(leaving_state == LOCATION);

  revstr = svn_hash_gets(attrs, "rev");
  path = svn_hash_gets(attrs, "path");
  if (revstr != NULL && path != NULL)
    {
      apr_int64_t rev_val;
      svn_revnum_t rev;

      SVN_ERR(svn_cstring_atoi64(&rev_val, revstr));
      rev = (svn_revnum_t)rev_val;

      apr_hash_set(loc_ctx->paths,
                   apr_pmemdup(loc_ctx->pool, &rev, sizeof(rev)), sizeof(rev),
                   apr_pstrdup(loc_ctx->pool, path));
    }

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_get_locations_body(serf_bucket_t **body_bkt,
                          void *baton,
                          serf_bucket_alloc_t *alloc,
                          apr_pool_t *pool /* request pool */,
                          apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  loc_context_t *loc_ctx = baton;
  int i;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:get-locations",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    "xmlns:D", "DAV:",
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", loc_ctx->path,
                               alloc);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:peg-revision", apr_ltoa(pool, loc_ctx->peg_revision),
                               alloc);

  for (i = 0; i < loc_ctx->location_revisions->nelts; i++)
    {
      svn_revnum_t rev = APR_ARRAY_IDX(loc_ctx->location_revisions, i, svn_revnum_t);
      svn_ra_serf__add_tag_buckets(buckets,
                                   "S:location-revision", apr_ltoa(pool, rev),
                                   alloc);
    }

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:get-locations");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_locations(svn_ra_session_t *ra_session,
                           apr_hash_t **locations,
                           const char *path,
                           svn_revnum_t peg_revision,
                           const apr_array_header_t *location_revisions,
                           apr_pool_t *pool)
{
  loc_context_t *loc_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;

  loc_ctx = apr_pcalloc(pool, sizeof(*loc_ctx));
  loc_ctx->pool = pool;
  loc_ctx->path = path;
  loc_ctx->peg_revision = peg_revision;
  loc_ctx->location_revisions = location_revisions;
  loc_ctx->paths = apr_hash_make(loc_ctx->pool);

  *locations = loc_ctx->paths;

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session,  NULL /* url */, peg_revision,
                                      pool, pool));

  xmlctx = svn_ra_serf__xml_context_create(getloc_ttable,
                                           NULL, getloc_closed, NULL,
                                           loc_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_delegate = create_get_locations_body;
  handler->body_delegate_baton = loc_ctx;
  handler->body_type = "text/xml";

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code != 200)
    SVN_ERR(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}
