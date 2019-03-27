/*
 * merge.c :  MERGE response parsing functions for ra_serf
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
#include "svn_dirent_uri.h"
#include "svn_props.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_fspath.h"
#include "svn_private_config.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/*
 * This enum represents the current state of our XML parsing for a MERGE.
 */
typedef enum merge_state_e {
  INITIAL = XML_STATE_INITIAL,
  MERGE_RESPONSE,
  UPDATED_SET,
  RESPONSE,
  HREF,
  PROPSTAT,
  PROP,
  RESOURCE_TYPE,
  BASELINE,
  COLLECTION,
  SKIP_HREF,
  CHECKED_IN,
  VERSION_NAME,
  DATE,
  AUTHOR,
  POST_COMMIT_ERR,

  STATUS
} merge_state_e;


/* Structure associated with a MERGE request. */
typedef struct merge_context_t
{
  apr_pool_t *pool;

  svn_ra_serf__session_t *session;
  svn_ra_serf__handler_t *handler;

  apr_hash_t *lock_tokens;
  svn_boolean_t keep_locks;
  svn_boolean_t disable_merge_response;

  const char *merge_resource_url; /* URL of resource to be merged. */
  const char *merge_url; /* URL at which the MERGE request is aimed. */

  svn_commit_info_t *commit_info;

} merge_context_t;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t merge_ttable[] = {
  { INITIAL, D_, "merge-response", MERGE_RESPONSE,
    FALSE, { NULL }, FALSE },

  { MERGE_RESPONSE, D_, "updated-set", UPDATED_SET,
    FALSE, { NULL }, FALSE },

  { UPDATED_SET, D_, "response", RESPONSE,
    FALSE, { NULL }, TRUE },

  { RESPONSE, D_, "href", HREF,
    TRUE, { NULL }, TRUE },

  { RESPONSE, D_, "propstat", PROPSTAT,
    FALSE, { NULL }, FALSE },

#if 0
  /* Not needed.  */
  { PROPSTAT, D_, "status", STATUS,
    FALSE, { NULL }, FALSE },
#endif

  { PROPSTAT, D_, "prop", PROP,
    FALSE, { NULL }, FALSE },

  { PROP, D_, "resourcetype", RESOURCE_TYPE,
    FALSE, { NULL }, FALSE },

  { RESOURCE_TYPE, D_, "baseline", BASELINE,
    FALSE, { NULL }, TRUE },

  { RESOURCE_TYPE, D_, "collection", COLLECTION,
    FALSE, { NULL }, TRUE },

  { PROP, D_, "checked-in", SKIP_HREF,
    FALSE, { NULL }, FALSE },

  { SKIP_HREF, D_, "href", CHECKED_IN,
    TRUE, { NULL }, TRUE },

  { PROP, D_, SVN_DAV__VERSION_NAME, VERSION_NAME,
    TRUE, { NULL }, TRUE },

  { PROP, D_, SVN_DAV__CREATIONDATE, DATE,
    TRUE, { NULL }, TRUE },

  { PROP, D_, "creator-displayname", AUTHOR,
    TRUE, { NULL }, TRUE },

  { PROP, S_, "post-commit-err", POST_COMMIT_ERR,
    TRUE, { NULL }, TRUE },

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
merge_closed(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int leaving_state,
             const svn_string_t *cdata,
             apr_hash_t *attrs,
             apr_pool_t *scratch_pool)
{
  merge_context_t *merge_ctx = baton;

  if (leaving_state == RESPONSE)
    {
      const char *rtype;

      rtype = svn_hash_gets(attrs, "resourcetype");

      /* rtype can only be "baseline" or "collection" (or NULL). We can
         keep this check simple.  */
      if (rtype && *rtype == 'b')
        {
          const char *rev_str;

          rev_str = svn_hash_gets(attrs, "revision");
          if (rev_str)
            {
              apr_int64_t rev;

              SVN_ERR(svn_cstring_atoi64(&rev, rev_str));
              merge_ctx->commit_info->revision = (svn_revnum_t)rev;
            }
          else
            merge_ctx->commit_info->revision = SVN_INVALID_REVNUM;

          merge_ctx->commit_info->date =
              apr_pstrdup(merge_ctx->pool,
                          svn_hash_gets(attrs, "date"));

          merge_ctx->commit_info->author =
              apr_pstrdup(merge_ctx->pool,
                          svn_hash_gets(attrs, "author"));

          merge_ctx->commit_info->post_commit_err =
             apr_pstrdup(merge_ctx->pool,
                         svn_hash_gets(attrs, "post-commit-err"));
        }
      else
        {
          const char *href;

          href = svn_urlpath__skip_ancestor(
                   merge_ctx->merge_url,
                   svn_hash_gets(attrs, "href"));

          if (href == NULL)
            return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                     _("A MERGE response for '%s' is not "
                                       "a child of the destination ('%s')"),
                                     href, merge_ctx->merge_url);

          /* We now need to dive all the way into the WC to update the
             base VCC url.  */
          if (!SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(merge_ctx->session)
              && merge_ctx->session->wc_callbacks->push_wc_prop)
            {
              const char *checked_in;
              svn_string_t checked_in_str;

              checked_in = svn_hash_gets(attrs, "checked-in");
              checked_in_str.data = checked_in;
              checked_in_str.len = strlen(checked_in);

              SVN_ERR(merge_ctx->session->wc_callbacks->push_wc_prop(
                        merge_ctx->session->wc_callback_baton,
                        href,
                        SVN_RA_SERF__WC_CHECKED_IN_URL,
                        &checked_in_str,
                        scratch_pool));
            }
        }
    }
  else if (leaving_state == BASELINE)
    {
      svn_ra_serf__xml_note(xes, RESPONSE, "resourcetype", "baseline");
    }
  else if (leaving_state == COLLECTION)
    {
      svn_ra_serf__xml_note(xes, RESPONSE, "resourcetype", "collection");
    }
  else
    {
      const char *name;
      const char *value = cdata->data;

      if (leaving_state == HREF)
        {
          name = "href";
          value = svn_urlpath__canonicalize(value, scratch_pool);
        }
      else if (leaving_state == CHECKED_IN)
        {
          name = "checked-in";
          value = svn_urlpath__canonicalize(value, scratch_pool);
        }
      else if (leaving_state == VERSION_NAME)
        name = "revision";
      else if (leaving_state == DATE)
        name = "date";
      else if (leaving_state == AUTHOR)
        name = "author";
      else if (leaving_state == POST_COMMIT_ERR)
        name = "post-commit-err";
      else
        SVN_ERR_MALFUNCTION();

      svn_ra_serf__xml_note(xes, RESPONSE, name, value);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
setup_merge_headers(serf_bucket_t *headers,
                    void *baton,
                    apr_pool_t *pool /* request pool */,
                    apr_pool_t *scratch_pool)
{
  merge_context_t *ctx = baton;
  apr_array_header_t *vals = apr_array_make(scratch_pool, 2,
                                            sizeof(const char *));

  if (!ctx->keep_locks)
    APR_ARRAY_PUSH(vals, const char *) = SVN_DAV_OPTION_RELEASE_LOCKS;
  if (ctx->disable_merge_response)
    APR_ARRAY_PUSH(vals, const char *) = SVN_DAV_OPTION_NO_MERGE_RESPONSE;

  if (vals->nelts > 0)
    serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                            svn_cstring_join2(vals, " ", FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

void
svn_ra_serf__merge_lock_token_list(apr_hash_t *lock_tokens,
                                   const char *parent,
                                   serf_bucket_t *body,
                                   serf_bucket_alloc_t *alloc,
                                   apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  if (!lock_tokens || apr_hash_count(lock_tokens) == 0)
    return;

  svn_ra_serf__add_open_tag_buckets(body, alloc,
                                    "S:lock-token-list",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  for (hi = apr_hash_first(pool, lock_tokens);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_string_t path;

      apr_hash_this(hi, &key, &klen, &val);

      path.data = key;
      path.len = klen;

      if (parent && !svn_relpath_skip_ancestor(parent, key))
        continue;

      svn_ra_serf__add_open_tag_buckets(body, alloc, "S:lock", SVN_VA_NULL);

      svn_ra_serf__add_open_tag_buckets(body, alloc, "lock-path", SVN_VA_NULL);
      svn_ra_serf__add_cdata_len_buckets(body, alloc, path.data, path.len);
      svn_ra_serf__add_close_tag_buckets(body, alloc, "lock-path");

      svn_ra_serf__add_tag_buckets(body, "lock-token", val, alloc);

      svn_ra_serf__add_close_tag_buckets(body, alloc, "S:lock");
    }

  svn_ra_serf__add_close_tag_buckets(body, alloc, "S:lock-token-list");
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t*
create_merge_body(serf_bucket_t **bkt,
                  void *baton,
                  serf_bucket_alloc_t *alloc,
                  apr_pool_t *pool /* request pool */,
                  apr_pool_t *scratch_pool)
{
  merge_context_t *ctx = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body_bkt, alloc);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:merge",
                                    "xmlns:D", "DAV:",
                                    SVN_VA_NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:source", SVN_VA_NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:href", SVN_VA_NULL);

  svn_ra_serf__add_cdata_len_buckets(body_bkt, alloc,
                                     ctx->merge_resource_url,
                                     strlen(ctx->merge_resource_url));

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:href");
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:source");

  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:no-auto-merge", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:no-checkout", SVN_VA_NULL);

  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:prop", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:checked-in", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:" SVN_DAV__VERSION_NAME, SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:resourcetype", SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:" SVN_DAV__CREATIONDATE, SVN_VA_NULL);
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:creator-displayname", SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:prop");

  svn_ra_serf__merge_lock_token_list(ctx->lock_tokens, NULL, body_bkt,
                                     alloc, pool);

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:merge");

  *bkt = body_bkt;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__run_merge(const svn_commit_info_t **commit_info,
                       svn_ra_serf__session_t *session,
                       const char *merge_resource_url,
                       apr_hash_t *lock_tokens,
                       svn_boolean_t keep_locks,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  merge_context_t *merge_ctx;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;

  merge_ctx = apr_pcalloc(scratch_pool, sizeof(*merge_ctx));

  merge_ctx->pool = result_pool;
  merge_ctx->session = session;

  merge_ctx->merge_resource_url = merge_resource_url;

  merge_ctx->lock_tokens = lock_tokens;
  merge_ctx->keep_locks = keep_locks;

  /* We don't need the full merge response when working over HTTPv2.
   * Over HTTPv1, this response is only required with a non-null
   * svn_ra_push_wc_prop_func_t callback. */
  merge_ctx->disable_merge_response =
    SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session) ||
    session->wc_callbacks->push_wc_prop == NULL;

  merge_ctx->commit_info = svn_create_commit_info(result_pool);

  merge_ctx->merge_url = session->session_url.path;

  xmlctx = svn_ra_serf__xml_context_create(merge_ttable,
                                           NULL, merge_closed, NULL,
                                           merge_ctx,
                                           scratch_pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                              scratch_pool);

  handler->method = "MERGE";
  handler->path = merge_ctx->merge_url;
  handler->body_delegate = create_merge_body;
  handler->body_delegate_baton = merge_ctx;
  handler->body_type = "text/xml";

  handler->header_delegate = setup_merge_headers;
  handler->header_delegate_baton = merge_ctx;

  merge_ctx->handler = handler;

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  *commit_info = merge_ctx->commit_info;

  /* Sanity check (Reported to be triggered by CodePlex's svnbridge) */
  if (! SVN_IS_VALID_REVNUM(merge_ctx->commit_info->revision))
    {
      return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                              _("The MERGE response did not include "
                                "a new revision"));
    }

  merge_ctx->commit_info->repos_root = apr_pstrdup(result_pool,
                                                   session->repos_root_str);

  return SVN_NO_ERROR;
}
