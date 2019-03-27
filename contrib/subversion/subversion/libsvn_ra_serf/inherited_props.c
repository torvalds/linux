/*
 * inherited_props.c : ra_serf implementation of svn_ra_get_inherited_props
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
#include "svn_path.h"
#include "svn_ra.h"
#include "svn_sorts.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_props.h"
#include "svn_base64.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_sorts_private.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_private_config.h"
#include "ra_serf.h"


/* The current state of our XML parsing. */
typedef enum iprops_state_e {
  INITIAL = XML_STATE_INITIAL,
  IPROPS_REPORT,
  IPROPS_ITEM,
  IPROPS_PATH,
  IPROPS_PROPNAME,
  IPROPS_PROPVAL
} iprops_state_e;

/* Struct for accumulating inherited props. */
typedef struct iprops_context_t {
  /* The depth-first ordered array of svn_prop_inherited_item_t *
     structures we are building. */
  apr_array_header_t *iprops;

  /* Pool in which to allocate elements of IPROPS. */
  apr_pool_t *pool;

  /* The repository's root URL. */
  const char *repos_root_url;

  /* Current property name */
  svn_stringbuf_t *curr_propname;

  /* Current element in IPROPS. */
  svn_prop_inherited_item_t *curr_iprop;

  /* Path we are finding inherited properties for.  This is relative to
     the RA session passed to svn_ra_serf__get_inherited_props. */
  const char *path;
  /* The revision of PATH*/
  svn_revnum_t revision;
} iprops_context_t;

#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t iprops_table[] = {
  { INITIAL, S_, SVN_DAV__INHERITED_PROPS_REPORT, IPROPS_REPORT,
    FALSE, { NULL }, FALSE },

  { IPROPS_REPORT, S_, SVN_DAV__IPROP_ITEM, IPROPS_ITEM,
    FALSE, { NULL }, TRUE },

  { IPROPS_ITEM, S_, SVN_DAV__IPROP_PATH, IPROPS_PATH,
    TRUE, { NULL }, TRUE },

  { IPROPS_ITEM, S_, SVN_DAV__IPROP_PROPNAME, IPROPS_PROPNAME,
    TRUE, { NULL }, TRUE },

  { IPROPS_ITEM, S_, SVN_DAV__IPROP_PROPVAL, IPROPS_PROPVAL,
    TRUE, { "?V:encoding", NULL }, TRUE },

  { 0 }
};

/* Conforms to svn_ra_serf__xml_opened_t */
static svn_error_t *
iprops_opened(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int entered_state,
              const svn_ra_serf__dav_props_t *tag,
              apr_pool_t *scratch_pool)
{
  iprops_context_t *iprops_ctx = baton;

  if (entered_state == IPROPS_ITEM)
    {
      svn_stringbuf_setempty(iprops_ctx->curr_propname);

      iprops_ctx->curr_iprop = apr_pcalloc(iprops_ctx->pool,
                                           sizeof(*iprops_ctx->curr_iprop));

      iprops_ctx->curr_iprop->prop_hash = apr_hash_make(iprops_ctx->pool);
    }
  return SVN_NO_ERROR;
}

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
iprops_closed(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int leaving_state,
              const svn_string_t *cdata,
              apr_hash_t *attrs,
              apr_pool_t *scratch_pool)
{
  iprops_context_t *iprops_ctx = baton;

  if (leaving_state == IPROPS_ITEM)
    {
      APR_ARRAY_PUSH(iprops_ctx->iprops, svn_prop_inherited_item_t *) =
        iprops_ctx->curr_iprop;

      iprops_ctx->curr_iprop = NULL;
    }
  else if (leaving_state == IPROPS_PATH)
    {
      /* Every <iprop-item> has a single <iprop-path> */
      if (iprops_ctx->curr_iprop->path_or_url)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      iprops_ctx->curr_iprop->path_or_url =
                                apr_pstrdup(iprops_ctx->pool, cdata->data);
    }
  else if (leaving_state == IPROPS_PROPNAME)
    {
      if (iprops_ctx->curr_propname->len)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      /* Store propname for value */
      svn_stringbuf_set(iprops_ctx->curr_propname, cdata->data);
    }
  else if (leaving_state == IPROPS_PROPVAL)
    {
      const char *encoding;
      const svn_string_t *val_str;

      if (! iprops_ctx->curr_propname->len)
        return svn_error_create(SVN_ERR_XML_MALFORMED, NULL, NULL);

      encoding = svn_hash_gets(attrs, "V:encoding");

      if (encoding)
        {
          if (strcmp(encoding, "base64") != 0)
            return svn_error_createf(SVN_ERR_XML_MALFORMED,
                                     NULL,
                                     _("Got unrecognized encoding '%s'"),
                                     encoding);

          /* Decode into the right pool.  */
          val_str = svn_base64_decode_string(cdata, iprops_ctx->pool);
        }
      else
        {
          /* Copy into the right pool.  */
          val_str = svn_string_dup(cdata, iprops_ctx->pool);
        }

      svn_hash_sets(iprops_ctx->curr_iprop->prop_hash,
                    apr_pstrdup(iprops_ctx->pool,
                                iprops_ctx->curr_propname->data),
                    val_str);
      /* Clear current propname. */
      svn_stringbuf_setempty(iprops_ctx->curr_propname);
    }
  else
    SVN_ERR_MALFUNCTION(); /* Invalid transition table */

  return SVN_NO_ERROR;
}

static svn_error_t *
create_iprops_body(serf_bucket_t **bkt,
                   void *baton,
                   serf_bucket_alloc_t *alloc,
                   apr_pool_t *pool /* request pool */,
                   apr_pool_t *scratch_pool)
{
  iprops_context_t *iprops_ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc,
                                    "S:" SVN_DAV__INHERITED_PROPS_REPORT,
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);
  svn_ra_serf__add_tag_buckets(body_bkt,
                               "S:" SVN_DAV__REVISION,
                               apr_ltoa(pool, iprops_ctx->revision),
                               alloc);
  svn_ra_serf__add_tag_buckets(body_bkt, "S:" SVN_DAV__PATH,
                               iprops_ctx->path, alloc);
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc,
                                     "S:" SVN_DAV__INHERITED_PROPS_REPORT);
  *bkt = body_bkt;
  return SVN_NO_ERROR;
}

/* Per request information for get_iprops_via_more_requests */
typedef struct iprop_rq_info_t
{
  const char *relpath;
  const char *urlpath;
  apr_hash_t *props;
  svn_ra_serf__handler_t *handler;
} iprop_rq_info_t;


/* Assumes session reparented to the repository root. The old session
   root is passed as session_url */
static svn_error_t *
get_iprops_via_more_requests(svn_ra_session_t *ra_session,
                             apr_array_header_t **iprops,
                             const char *session_url,
                             const char *path,
                             svn_revnum_t revision,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  const char *url;
  const char *relpath;
  apr_array_header_t *rq_info;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_interval_time_t waittime_left = session->timeout;
  const svn_revnum_t rev_marker = SVN_INVALID_REVNUM;
  int i;

  rq_info = apr_array_make(scratch_pool, 16, sizeof(iprop_rq_info_t *));

  if (!svn_path_is_empty(path))
    url = svn_path_url_add_component2(session_url, path, scratch_pool);
  else
    url = session_url;

  relpath = svn_uri_skip_ancestor(session->repos_root_str, url, scratch_pool);

  /* Create all requests */
  while (relpath[0] != '\0')
    {
      iprop_rq_info_t *rq = apr_pcalloc(scratch_pool, sizeof(*rq));

      relpath = svn_relpath_dirname(relpath, scratch_pool);

      rq->relpath = relpath;
      rq->props = apr_hash_make(scratch_pool);

      SVN_ERR(svn_ra_serf__get_stable_url(&rq->urlpath, NULL, session,
                                          svn_path_url_add_component2(
                                                session->repos_root.path,
                                                relpath, scratch_pool),
                                          revision,
                                          scratch_pool, scratch_pool));

      SVN_ERR(svn_ra_serf__create_propfind_handler(
                                          &rq->handler, session,
                                          rq->urlpath,
                                          rev_marker, "0", all_props,
                                          svn_ra_serf__deliver_svn_props,
                                          rq->props,
                                          scratch_pool));

      /* Allow ignoring authz problems */
      rq->handler->no_fail_on_http_failure_status = TRUE;

      svn_ra_serf__request_create(rq->handler);

      APR_ARRAY_PUSH(rq_info, iprop_rq_info_t *) = rq;
    }

  while (TRUE)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_serf__context_run(session, &waittime_left, iterpool));

      for (i = 0; i < rq_info->nelts; i++)
        {
          iprop_rq_info_t *rq = APR_ARRAY_IDX(rq_info, i, iprop_rq_info_t *);

          if (!rq->handler->done)
            break;
        }

      if (i >= rq_info->nelts)
        break; /* All requests done */
    }

  *iprops = apr_array_make(result_pool, rq_info->nelts,
                           sizeof(svn_prop_inherited_item_t *));

  /* And now create the result set */
  for (i = 0; i < rq_info->nelts; i++)
    {
      iprop_rq_info_t *rq = APR_ARRAY_IDX(rq_info, i, iprop_rq_info_t *);
      apr_hash_t *node_props;
      svn_prop_inherited_item_t *new_iprop;

      if (rq->handler->sline.code != 207 && rq->handler->sline.code != 403)
        {
          if (rq->handler->server_error)
            SVN_ERR(svn_ra_serf__server_error_create(rq->handler,
                                                     scratch_pool));

          return svn_error_trace(svn_ra_serf__unexpected_status(rq->handler));
        }

      node_props = rq->props;

      svn_ra_serf__keep_only_regular_props(node_props, scratch_pool);

      if (!apr_hash_count(node_props))
        continue;

      new_iprop = apr_palloc(result_pool, sizeof(*new_iprop));
      new_iprop->path_or_url = apr_pstrdup(result_pool, rq->relpath);
      new_iprop->prop_hash = svn_prop_hash_dup(node_props, result_pool);
      svn_sort__array_insert(*iprops, &new_iprop, 0);
    }

  return SVN_NO_ERROR;
}

/* Request a inherited-props-report from the URL attached to RA_SESSION,
   and fill the IPROPS array hash with the results.  */
svn_error_t *
svn_ra_serf__get_inherited_props(svn_ra_session_t *ra_session,
                                 apr_array_header_t **iprops,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  iprops_context_t *iprops_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;
  svn_boolean_t iprop_capable;

  SVN_ERR(svn_ra_serf__has_capability(ra_session, &iprop_capable,
                                      SVN_RA_CAPABILITY_INHERITED_PROPS,
                                      scratch_pool));

  if (!iprop_capable)
    {
      svn_error_t *err;
      const char *reparent_uri = NULL;
      const char *session_uri;
      const char *repos_root_url;

      SVN_ERR(svn_ra_serf__get_repos_root(ra_session, &repos_root_url,
                                          scratch_pool));

      session_uri = apr_pstrdup(scratch_pool, session->session_url_str);
      if (strcmp(repos_root_url, session->session_url_str) != 0)
        {
          reparent_uri  = session_uri;
          SVN_ERR(svn_ra_serf__reparent(ra_session, repos_root_url,
                                        scratch_pool));
        }

      err = get_iprops_via_more_requests(ra_session, iprops, session_uri, path,
                                         revision, result_pool, scratch_pool);

      if (reparent_uri)
        err = svn_error_compose_create(err,
                                       svn_ra_serf__reparent(ra_session,
                                                             reparent_uri ,
                                                             scratch_pool));

      return svn_error_trace(err);
    }

  SVN_ERR(svn_ra_serf__get_stable_url(&req_url,
                                      NULL /* latest_revnum */,
                                      session,
                                      NULL /* url */,
                                      revision,
                                      scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(session->repos_root_str);

  iprops_ctx = apr_pcalloc(scratch_pool, sizeof(*iprops_ctx));
  iprops_ctx->repos_root_url = session->repos_root_str;
  iprops_ctx->pool = result_pool;
  iprops_ctx->curr_propname = svn_stringbuf_create_empty(scratch_pool);
  iprops_ctx->curr_iprop = NULL;
  iprops_ctx->iprops = apr_array_make(result_pool, 1,
                                       sizeof(svn_prop_inherited_item_t *));
  iprops_ctx->path = path;
  iprops_ctx->revision = revision;

  xmlctx = svn_ra_serf__xml_context_create(iprops_table,
                                           iprops_opened, iprops_closed,
                                           NULL,
                                           iprops_ctx,
                                           scratch_pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                              scratch_pool);

  handler->method = "REPORT";
  handler->path = req_url;

  handler->body_delegate = create_iprops_body;
  handler->body_delegate_baton = iprops_ctx;
  handler->body_type = "text/xml";

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  *iprops = iprops_ctx->iprops;

  return SVN_NO_ERROR;
}
