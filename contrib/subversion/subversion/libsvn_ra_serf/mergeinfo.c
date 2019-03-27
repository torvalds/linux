/*
 * mergeinfo.c : entry point for mergeinfo RA functions for ra_serf
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

#include <apr_tables.h>
#include <apr_xml.h>

#include "svn_hash.h"
#include "svn_mergeinfo.h"
#include "svn_path.h"
#include "svn_ra.h"
#include "svn_string.h"
#include "svn_xml.h"

#include "private/svn_dav_protocol.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_private_config.h"
#include "ra_serf.h"




/* The current state of our XML parsing. */
typedef enum mergeinfo_state_e {
  INITIAL = XML_STATE_INITIAL,
  MERGEINFO_REPORT,
  MERGEINFO_ITEM,
  MERGEINFO_PATH,
  MERGEINFO_INFO
} mergeinfo_state_e;

/* Baton for accumulating mergeinfo.  RESULT_CATALOG stores the final
   mergeinfo catalog result we are going to hand back to the caller of
   get_mergeinfo.  */
typedef struct mergeinfo_context_t {
  apr_pool_t *pool;
  svn_mergeinfo_t result_catalog;
  const apr_array_header_t *paths;
  svn_revnum_t revision;
  svn_mergeinfo_inheritance_t inherit;
  svn_boolean_t include_descendants;
} mergeinfo_context_t;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t mergeinfo_ttable[] = {
  { INITIAL, S_, SVN_DAV__MERGEINFO_REPORT, MERGEINFO_REPORT,
    FALSE, { NULL }, FALSE },

  { MERGEINFO_REPORT, S_, SVN_DAV__MERGEINFO_ITEM, MERGEINFO_ITEM,
    FALSE, { NULL }, TRUE },

  { MERGEINFO_ITEM, S_, SVN_DAV__MERGEINFO_PATH, MERGEINFO_PATH,
    TRUE, { NULL }, TRUE },

  { MERGEINFO_ITEM, S_, SVN_DAV__MERGEINFO_INFO, MERGEINFO_INFO,
    TRUE, { NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
mergeinfo_closed(svn_ra_serf__xml_estate_t *xes,
                 void *baton,
                 int leaving_state,
                 const svn_string_t *cdata,
                 apr_hash_t *attrs,
                 apr_pool_t *scratch_pool)
{
  mergeinfo_context_t *mergeinfo_ctx = baton;

  if (leaving_state == MERGEINFO_ITEM)
    {
      /* Placed here from the child elements.  */
      const char *path = svn_hash_gets(attrs, "path");
      const char *info = svn_hash_gets(attrs, "info");

      if (path != NULL && info != NULL)
        {
          svn_mergeinfo_t path_mergeinfo;

          /* Correct for naughty servers that send "relative" paths
             with leading slashes! */
          if (path[0] == '/')
            ++path;

          SVN_ERR(svn_mergeinfo_parse(&path_mergeinfo, info,
                                      mergeinfo_ctx->pool));

          svn_hash_sets(mergeinfo_ctx->result_catalog,
                        apr_pstrdup(mergeinfo_ctx->pool, path),
                        path_mergeinfo);
        }
    }
  else
    {
      SVN_ERR_ASSERT(leaving_state == MERGEINFO_PATH
                     || leaving_state == MERGEINFO_INFO);

      /* Stash the value onto the parent MERGEINFO_ITEM.  */
      svn_ra_serf__xml_note(xes, MERGEINFO_ITEM,
                            leaving_state == MERGEINFO_PATH
                              ? "path"
                              : "info",
                            cdata->data);
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_mergeinfo_body(serf_bucket_t **bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool /* request pool */,
                      apr_pool_t *scratch_pool)
{
  mergeinfo_context_t *mergeinfo_ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc,
                                    "S:" SVN_DAV__MERGEINFO_REPORT,
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:" SVN_DAV__REVISION,
                               apr_ltoa(pool, mergeinfo_ctx->revision),
                               alloc);
  svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__INHERIT,
                               svn_inheritance_to_word(mergeinfo_ctx->inherit),
                               alloc);
  if (mergeinfo_ctx->include_descendants)
    {
      svn_ra_serf__add_tag_buckets(body_bkt, "S:"
                                   SVN_DAV__INCLUDE_DESCENDANTS,
                                   "yes", alloc);
    }

  if (mergeinfo_ctx->paths)
    {
      int i;

      for (i = 0; i < mergeinfo_ctx->paths->nelts; i++)
        {
          const char *this_path = APR_ARRAY_IDX(mergeinfo_ctx->paths,
                                                i, const char *);

          svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__PATH,
                                       this_path, alloc);
        }
    }

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc,
                                     "S:" SVN_DAV__MERGEINFO_REPORT);

  *bkt = body_bkt;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_mergeinfo(svn_ra_session_t *ra_session,
                           svn_mergeinfo_catalog_t *catalog,
                           const apr_array_header_t *paths,
                           svn_revnum_t revision,
                           svn_mergeinfo_inheritance_t inherit,
                           svn_boolean_t include_descendants,
                           apr_pool_t *pool)
{
  mergeinfo_context_t *mergeinfo_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *path;

  *catalog = NULL;

  SVN_ERR(svn_ra_serf__get_stable_url(&path, NULL /* latest_revnum */,
                                      session,
                                      NULL /* url */, revision,
                                      pool, pool));

  mergeinfo_ctx = apr_pcalloc(pool, sizeof(*mergeinfo_ctx));
  mergeinfo_ctx->pool = pool;
  mergeinfo_ctx->result_catalog = apr_hash_make(pool);
  mergeinfo_ctx->paths = paths;
  mergeinfo_ctx->revision = revision;
  mergeinfo_ctx->inherit = inherit;
  mergeinfo_ctx->include_descendants = include_descendants;

  xmlctx = svn_ra_serf__xml_context_create(mergeinfo_ttable,
                                           NULL, mergeinfo_closed, NULL,
                                           mergeinfo_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL, pool);

  handler->method = "REPORT";
  handler->path = path;

  handler->body_delegate = create_mergeinfo_body;
  handler->body_delegate_baton = mergeinfo_ctx;
  handler->body_type = "text/xml";

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code != 200)
    SVN_ERR(svn_ra_serf__unexpected_status(handler));

  if (apr_hash_count(mergeinfo_ctx->result_catalog))
    *catalog = mergeinfo_ctx->result_catalog;

  return SVN_NO_ERROR;
}
