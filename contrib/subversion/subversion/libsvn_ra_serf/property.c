/*
 * property.c : property routines for ra_serf
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



#include <serf.h>

#include "svn_hash.h"
#include "svn_path.h"
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_fspath.h"
#include "private/svn_string_private.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/* Our current parsing state we're in for the PROPFIND response. */
typedef enum prop_state_e {
  INITIAL = XML_STATE_INITIAL,
  MULTISTATUS,
  RESPONSE,
  HREF,
  PROPSTAT,
  STATUS,
  PROP,
  PROPVAL,
  COLLECTION,
  HREF_VALUE
} prop_state_e;


/*
 * This structure represents a pending PROPFIND response.
 */
typedef struct propfind_context_t {
  svn_ra_serf__handler_t *handler;

  /* the requested path */
  const char *path;

  /* the requested version (in string form) */
  const char *label;

  /* the request depth */
  const char *depth;

  /* the list of requested properties */
  const svn_ra_serf__dav_props_t *find_props;

  svn_ra_serf__prop_func_t prop_func;
  void *prop_func_baton;

  /* hash table containing all the properties associated with the
   * "current" <propstat> tag.  These will get copied into RET_PROPS
   * if the status code similarly associated indicates that they are
   * "good"; otherwise, they'll get discarded.
   */
  apr_hash_t *ps_props;
} propfind_context_t;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t propfind_ttable[] = {
  { INITIAL, D_, "multistatus", MULTISTATUS,
    FALSE, { NULL }, TRUE },

  { MULTISTATUS, D_, "response", RESPONSE,
    FALSE, { NULL }, FALSE },

  { RESPONSE, D_, "href", HREF,
    TRUE, { NULL }, TRUE },

  { RESPONSE, D_, "propstat", PROPSTAT,
    FALSE, { NULL }, TRUE },

  { PROPSTAT, D_, "status", STATUS,
    TRUE, { NULL }, TRUE },

  { PROPSTAT, D_, "prop", PROP,
    FALSE, { NULL }, FALSE },

  { PROP, "*", "*", PROPVAL,
    TRUE, { "?V:encoding", NULL }, TRUE },

  { PROPVAL, D_, "collection", COLLECTION,
    FALSE, { NULL }, TRUE },

  { PROPVAL, D_, "href", HREF_VALUE,
    TRUE, { NULL }, TRUE },

  { 0 }
};

static const int propfind_expected_status[] = {
  207,
  0
};

/* Return the HTTP status code contained in STATUS_LINE, or 0 if
   there's a problem parsing it. */
static apr_int64_t parse_status_code(const char *status_line)
{
  /* STATUS_LINE should be of form: "HTTP/1.1 200 OK" */
  if (status_line[0] == 'H' &&
      status_line[1] == 'T' &&
      status_line[2] == 'T' &&
      status_line[3] == 'P' &&
      status_line[4] == '/' &&
      (status_line[5] >= '0' && status_line[5] <= '9') &&
      status_line[6] == '.' &&
      (status_line[7] >= '0' && status_line[7] <= '9') &&
      status_line[8] == ' ')
    {
      char *reason;

      return apr_strtoi64(status_line + 8, &reason, 10);
    }
  return 0;
}

/* Conforms to svn_ra_serf__xml_opened_t  */
static svn_error_t *
propfind_opened(svn_ra_serf__xml_estate_t *xes,
                void *baton,
                int entered_state,
                const svn_ra_serf__dav_props_t *tag,
                apr_pool_t *scratch_pool)
{
  propfind_context_t *ctx = baton;

  if (entered_state == PROPVAL)
    {
        svn_ra_serf__xml_note(xes, PROPVAL, "ns", tag->xmlns);
      svn_ra_serf__xml_note(xes, PROPVAL, "name", tag->name);
    }
  else if (entered_state == PROPSTAT)
    {
      ctx->ps_props = apr_hash_make(svn_ra_serf__xml_state_pool(xes));
    }

  return SVN_NO_ERROR;
}

/* Set PROPS for NS:NAME VAL. Helper for propfind_closed */
static void
set_ns_prop(apr_hash_t *ns_props,
            const char *ns, const char *name,
            const svn_string_t *val, apr_pool_t *result_pool)
{
  apr_hash_t *props = svn_hash_gets(ns_props, ns);

  if (!props)
    {
      props = apr_hash_make(result_pool);
      ns = apr_pstrdup(result_pool, ns);
      svn_hash_sets(ns_props, ns, props);
    }

  if (val)
    {
      name = apr_pstrdup(result_pool, name);
      val = svn_string_dup(val, result_pool);
    }

  svn_hash_sets(props, name, val);
}

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
propfind_closed(svn_ra_serf__xml_estate_t *xes,
                void *baton,
                int leaving_state,
                const svn_string_t *cdata,
                apr_hash_t *attrs,
                apr_pool_t *scratch_pool)
{
  propfind_context_t *ctx = baton;

  if (leaving_state == MULTISTATUS)
    {
      /* We've gathered all the data from the reponse. Add this item
         onto the "done list". External callers will then know this
         request has been completed (tho stray response bytes may still
         arrive).  */
    }
  else if (leaving_state == HREF)
    {
      const char *path;

      if (strcmp(ctx->depth, "1") == 0)
        path = svn_urlpath__canonicalize(cdata->data, scratch_pool);
      else
        path = ctx->path;

      svn_ra_serf__xml_note(xes, RESPONSE, "path", path);

      SVN_ERR(ctx->prop_func(ctx->prop_func_baton,
                             path,
                             D_, "href",
                             cdata, scratch_pool));
    }
  else if (leaving_state == COLLECTION)
    {
      svn_ra_serf__xml_note(xes, PROPVAL, "altvalue", "collection");
    }
  else if (leaving_state == HREF_VALUE)
    {
      svn_ra_serf__xml_note(xes, PROPVAL, "altvalue", cdata->data);
    }
  else if (leaving_state == STATUS)
    {
      /* Parse the status field, and remember if this is a property
         that we wish to ignore.  (Typically, if it's not a 200, the
         status will be 404 to indicate that a property we
         specifically requested from the server doesn't exist.)  */
      apr_int64_t status = parse_status_code(cdata->data);
      if (status != 200)
        svn_ra_serf__xml_note(xes, PROPSTAT, "ignore-prop", "*");
    }
  else if (leaving_state == PROPVAL)
    {
      const char *encoding;
      const svn_string_t *val_str;
      const char *ns;
      const char *name;
      const char *altvalue;

      if ((altvalue = svn_hash_gets(attrs, "altvalue")) != NULL)
        {
          val_str = svn_string_create(altvalue, scratch_pool);
        }
      else if ((encoding = svn_hash_gets(attrs, "V:encoding")) != NULL)
        {
          if (strcmp(encoding, "base64") != 0)
            return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA,
                                     NULL,
                                     _("Got unrecognized encoding '%s'"),
                                     encoding);

          /* Decode into the right pool.  */
          val_str = svn_base64_decode_string(cdata, scratch_pool);
        }
      else
        {
          /* Copy into the right pool.  */
          val_str = cdata;
        }

      /* The current path sits on the RESPONSE state.

         Now, it would be nice if we could, at this point, know that
         the status code for this property indicated a problem -- then
         we could simply bail out here and ignore the property.
         Sadly, though, we might get the status code *after* we get
         the property value.  So we'll carry on with our processing
         here, setting the property and value as expected.  Once we
         know for sure the status code associate with the property,
         we'll decide its fate.  */

      ns = svn_hash_gets(attrs, "ns");
      name = svn_hash_gets(attrs, "name");

      set_ns_prop(ctx->ps_props, ns, name, val_str,
                  apr_hash_pool_get(ctx->ps_props));
    }
  else
    {
      apr_hash_t *gathered;

      SVN_ERR_ASSERT(leaving_state == PROPSTAT);

      gathered = svn_ra_serf__xml_gather_since(xes, RESPONSE);

      /* If we've squirreled away a note that says we want to ignore
         these properties, we'll do so.  Otherwise, we need to copy
         them from the temporary hash into the ctx->ret_props hash. */
      if (! svn_hash_gets(gathered, "ignore-prop"))
        {
          apr_hash_index_t *hi_ns;
          const char *path;
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);


          path = svn_hash_gets(gathered, "path");
          if (!path)
            path = ctx->path;

          for (hi_ns = apr_hash_first(scratch_pool, ctx->ps_props);
               hi_ns;
               hi_ns = apr_hash_next(hi_ns))
            {
              const char *ns = apr_hash_this_key(hi_ns);
              apr_hash_t *props = apr_hash_this_val(hi_ns);
              apr_hash_index_t *hi_prop;

              svn_pool_clear(iterpool);

              for (hi_prop = apr_hash_first(iterpool, props);
                   hi_prop;
                   hi_prop = apr_hash_next(hi_prop))
                {
                  const char *name = apr_hash_this_key(hi_prop);
                  const svn_string_t *value = apr_hash_this_val(hi_prop);

                  SVN_ERR(ctx->prop_func(ctx->prop_func_baton, path,
                                         ns, name, value, iterpool));
                }
            }

          svn_pool_destroy(iterpool);
        }

      ctx->ps_props = NULL; /* Allocated in PROPSTAT state pool */
    }

  return SVN_NO_ERROR;
}



static svn_error_t *
setup_propfind_headers(serf_bucket_t *headers,
                       void *setup_baton,
                       apr_pool_t *pool /* request pool */,
                       apr_pool_t *scratch_pool)
{
  propfind_context_t *ctx = setup_baton;

  serf_bucket_headers_setn(headers, "Depth", ctx->depth);
  if (ctx->label)
    {
      serf_bucket_headers_setn(headers, "Label", ctx->label);
    }

  return SVN_NO_ERROR;
}

#define PROPFIND_HEADER "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\">"
#define PROPFIND_TRAILER "</propfind>"

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_propfind_body(serf_bucket_t **bkt,
                     void *setup_baton,
                     serf_bucket_alloc_t *alloc,
                     apr_pool_t *pool /* request pool */,
                     apr_pool_t *scratch_pool)
{
  propfind_context_t *ctx = setup_baton;

  serf_bucket_t *body_bkt, *tmp;
  const svn_ra_serf__dav_props_t *prop;
  svn_boolean_t requested_allprop = FALSE;

  body_bkt = serf_bucket_aggregate_create(alloc);

  prop = ctx->find_props;
  while (prop && prop->xmlns)
    {
      /* special case the allprop case. */
      if (strcmp(prop->name, "allprop") == 0)
        {
          requested_allprop = TRUE;
        }

      prop++;
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_HEADER,
                                      sizeof(PROPFIND_HEADER)-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  /* If we're not doing an allprop, add <prop> tags. */
  if (!requested_allprop)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<prop>",
                                          sizeof("<prop>")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);
    }

  prop = ctx->find_props;
  while (prop && prop->xmlns)
    {
      /* <*propname* xmlns="*propns*" /> */
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("<", 1, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(prop->name, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN(" xmlns=\"",
                                          sizeof(" xmlns=\"")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING(prop->xmlns, alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("\"/>", sizeof("\"/>")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);

      prop++;
    }

  if (!requested_allprop)
    {
      tmp = SERF_BUCKET_SIMPLE_STRING_LEN("</prop>",
                                          sizeof("</prop>")-1,
                                          alloc);
      serf_bucket_aggregate_append(body_bkt, tmp);
    }

  tmp = SERF_BUCKET_SIMPLE_STRING_LEN(PROPFIND_TRAILER,
                                      sizeof(PROPFIND_TRAILER)-1,
                                      alloc);
  serf_bucket_aggregate_append(body_bkt, tmp);

  *bkt = body_bkt;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__create_propfind_handler(svn_ra_serf__handler_t **propfind_handler,
                                     svn_ra_serf__session_t *sess,
                                     const char *path,
                                     svn_revnum_t rev,
                                     const char *depth,
                                     const svn_ra_serf__dav_props_t *find_props,
                                     svn_ra_serf__prop_func_t prop_func,
                                     void *prop_func_baton,
                                     apr_pool_t *pool)
{
  propfind_context_t *new_prop_ctx;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;

  new_prop_ctx = apr_pcalloc(pool, sizeof(*new_prop_ctx));

  new_prop_ctx->path = path;
  new_prop_ctx->find_props = find_props;
  new_prop_ctx->prop_func = prop_func;
  new_prop_ctx->prop_func_baton = prop_func_baton;
  new_prop_ctx->depth = depth;

  if (SVN_IS_VALID_REVNUM(rev))
    {
      new_prop_ctx->label = apr_ltoa(pool, rev);
    }
  else
    {
      new_prop_ctx->label = NULL;
    }

  xmlctx = svn_ra_serf__xml_context_create(propfind_ttable,
                                           propfind_opened,
                                           propfind_closed,
                                           NULL,
                                           new_prop_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(sess, xmlctx,
                                              propfind_expected_status,
                                              pool);

  handler->method = "PROPFIND";
  handler->path = path;
  handler->body_delegate = create_propfind_body;
  handler->body_type = "text/xml";
  handler->body_delegate_baton = new_prop_ctx;
  handler->header_delegate = setup_propfind_headers;
  handler->header_delegate_baton = new_prop_ctx;

  handler->no_dav_headers = TRUE;

  new_prop_ctx->handler = handler;

  *propfind_handler = handler;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__deliver_svn_props(void *baton,
                               const char *path,
                               const char *ns,
                               const char *name,
                               const svn_string_t *value,
                               apr_pool_t *scratch_pool)
{
  apr_hash_t *props = baton;
  apr_pool_t *result_pool = apr_hash_pool_get(props);
  const char *prop_name;

  prop_name = svn_ra_serf__svnname_from_wirename(ns, name, result_pool);
  if (prop_name == NULL)
    return SVN_NO_ERROR;

  svn_hash_sets(props, prop_name, svn_string_dup(value, result_pool));

  return SVN_NO_ERROR;
}

/*
 * Implementation of svn_ra_serf__prop_func_t that delivers all DAV properties
 * in (const char * -> apr_hash_t *) on Namespace pointing to a second hash
 *    (const char * -> svn_string_t *) to the values.
 */
static svn_error_t *
deliver_node_props(void *baton,
                  const char *path,
                  const char *ns,
                  const char *name,
                  const svn_string_t *value,
                  apr_pool_t *scratch_pool)
{
  apr_hash_t *nss = baton;
  apr_hash_t *props;
  apr_pool_t *result_pool = apr_hash_pool_get(nss);

  props = svn_hash_gets(nss, ns);

  if (!props)
    {
      props = apr_hash_make(result_pool);

      ns = apr_pstrdup(result_pool, ns);
      svn_hash_sets(nss, ns, props);
    }

  name = apr_pstrdup(result_pool, name);
  svn_hash_sets(props, name, svn_string_dup(value, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__fetch_node_props(apr_hash_t **results,
                              svn_ra_serf__session_t *session,
                              const char *url,
                              svn_revnum_t revision,
                              const svn_ra_serf__dav_props_t *which_props,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  svn_ra_serf__handler_t *handler;

  props = apr_hash_make(result_pool);

  SVN_ERR(svn_ra_serf__create_propfind_handler(&handler, session,
                                               url, revision, "0", which_props,
                                               deliver_node_props,
                                               props, scratch_pool));

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  *results = props;
  return SVN_NO_ERROR;
}

const char *
svn_ra_serf__svnname_from_wirename(const char *ns,
                                   const char *name,
                                   apr_pool_t *result_pool)
{
  if (*ns == '\0' || strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    return apr_pstrdup(result_pool, name);

  if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    return apr_pstrcat(result_pool, SVN_PROP_PREFIX, name, SVN_VA_NULL);

  if (strcmp(ns, SVN_PROP_PREFIX) == 0)
    return apr_pstrcat(result_pool, SVN_PROP_PREFIX, name, SVN_VA_NULL);

  if (strcmp(name, SVN_DAV__VERSION_NAME) == 0)
    return SVN_PROP_ENTRY_COMMITTED_REV;

  if (strcmp(name, SVN_DAV__CREATIONDATE) == 0)
    return SVN_PROP_ENTRY_COMMITTED_DATE;

  if (strcmp(name, "creator-displayname") == 0)
    return SVN_PROP_ENTRY_LAST_AUTHOR;

  if (strcmp(name, "repository-uuid") == 0)
    return SVN_PROP_ENTRY_UUID;

  if (strcmp(name, "lock-token") == 0)
    return SVN_PROP_ENTRY_LOCK_TOKEN;

  if (strcmp(name, "checked-in") == 0)
    return SVN_RA_SERF__WC_CHECKED_IN_URL;

  if (strcmp(ns, "DAV:") == 0 || strcmp(ns, SVN_DAV_PROP_NS_DAV) == 0)
    {
      /* Here DAV: properties not yet converted to svn: properties should be
         ignored. */
      return NULL;
    }

  /* An unknown namespace, must be a custom property. */
  return apr_pstrcat(result_pool, ns, name, SVN_VA_NULL);
}

/*
 * Contact the server (using CONN) to calculate baseline
 * information for BASELINE_URL at REVISION (which may be
 * SVN_INVALID_REVNUM to query the HEAD revision).
 *
 * If ACTUAL_REVISION is non-NULL, set *ACTUAL_REVISION to revision
 * retrieved from the server as part of this process (which should
 * match REVISION when REVISION is valid).  Set *BASECOLL_URL_P to the
 * baseline collection URL.
 */
static svn_error_t *
retrieve_baseline_info(svn_revnum_t *actual_revision,
                       const char **basecoll_url_p,
                       svn_ra_serf__session_t *session,
                       const char *baseline_url,
                       svn_revnum_t revision,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  apr_hash_t *dav_props;
  const char *basecoll_url;

  SVN_ERR(svn_ra_serf__fetch_node_props(&props, session,
                                        baseline_url, revision,
                                        baseline_props,
                                        scratch_pool, scratch_pool));
  dav_props = apr_hash_get(props, "DAV:", 4);
  /* If DAV_PROPS is NULL, then svn_prop_get_value() will return NULL.  */

  basecoll_url = svn_prop_get_value(dav_props, "baseline-collection");
  if (!basecoll_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                              _("The PROPFIND response did not include "
                                "the requested baseline-collection value"));
    }
  *basecoll_url_p = svn_urlpath__canonicalize(basecoll_url, result_pool);

  if (actual_revision)
    {
      const char *version_name;

      version_name = svn_prop_get_value(dav_props, SVN_DAV__VERSION_NAME);
      if (version_name)
        {
          apr_int64_t rev;

          SVN_ERR(svn_cstring_atoi64(&rev, version_name));
          *actual_revision = (svn_revnum_t)rev;
        }

      if (!version_name || !SVN_IS_VALID_REVNUM(*actual_revision))
        return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                                _("The PROPFIND response did not include "
                                  "the requested version-name value"));
    }

  return SVN_NO_ERROR;
}


/* For HTTPv1 servers, do a PROPFIND dance on the VCC to fetch the youngest
   revnum. If BASECOLL_URL is non-NULL, then the corresponding baseline
   collection URL is also returned.

   Do the work over CONN.

   *BASECOLL_URL (if requested) will be allocated in RESULT_POOL. All
   temporary allocations will be made in SCRATCH_POOL.  */
static svn_error_t *
v1_get_youngest_revnum(svn_revnum_t *youngest,
                       const char **basecoll_url,
                       svn_ra_serf__session_t *session,
                       const char *vcc_url,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *baseline_url;
  const char *bc_url;

  /* Fetching DAV:checked-in from the VCC (with no Label: to specify a
     revision) will return the latest Baseline resource's URL.  */
  SVN_ERR(svn_ra_serf__fetch_dav_prop(&baseline_url, session, vcc_url,
                                      SVN_INVALID_REVNUM,
                                      "checked-in",
                                      scratch_pool, scratch_pool));
  if (!baseline_url)
    {
      return svn_error_create(SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED, NULL,
                              _("The OPTIONS response did not include "
                                "the requested checked-in value"));
    }
  baseline_url = svn_urlpath__canonicalize(baseline_url, scratch_pool);

  /* From the Baseline resource, we can fetch the DAV:baseline-collection
     and DAV:version-name properties. The latter is the revision number,
     which is formally the name used in Label: headers.  */

  /* First check baseline information cache. */
  SVN_ERR(svn_ra_serf__blncache_get_baseline_info(&bc_url,
                                                  youngest,
                                                  session->blncache,
                                                  baseline_url,
                                                  scratch_pool));
  if (!bc_url)
    {
      SVN_ERR(retrieve_baseline_info(youngest, &bc_url, session,
                                     baseline_url, SVN_INVALID_REVNUM,
                                     scratch_pool, scratch_pool));
      SVN_ERR(svn_ra_serf__blncache_set(session->blncache,
                                        baseline_url, *youngest,
                                        bc_url, scratch_pool));
    }

  if (basecoll_url != NULL)
    *basecoll_url = apr_pstrdup(result_pool, bc_url);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__get_youngest_revnum(svn_revnum_t *youngest,
                                 svn_ra_serf__session_t *session,
                                 apr_pool_t *scratch_pool)
{
  const char *vcc_url;

  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    return svn_error_trace(svn_ra_serf__v2_get_youngest_revnum(
                             youngest, session, scratch_pool));

  SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, scratch_pool));

  return svn_error_trace(v1_get_youngest_revnum(youngest, NULL,
                                                session, vcc_url,
                                                scratch_pool, scratch_pool));
}


/* Set *BC_URL to the baseline collection url for REVISION. If REVISION
   is SVN_INVALID_REVNUM, then the youngest revnum ("HEAD") is used.

   *REVNUM_USED will be set to the revision used.

   Uses the specified CONN, which is part of SESSION.

   All allocations (results and temporary) are performed in POOL.  */
static svn_error_t *
get_baseline_info(const char **bc_url,
                  svn_revnum_t *revnum_used,
                  svn_ra_serf__session_t *session,
                  svn_revnum_t revision,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  /* If we detected HTTP v2 support on the server, we can construct
     the baseline collection URL ourselves, and fetch the latest
     revision (if needed) with an OPTIONS request.  */
  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    {
      if (SVN_IS_VALID_REVNUM(revision))
        {
          *revnum_used = revision;
        }
      else
        {
          SVN_ERR(svn_ra_serf__v2_get_youngest_revnum(
                    revnum_used, session, scratch_pool));
        }

      *bc_url = apr_psprintf(result_pool, "%s/%ld",
                             session->rev_root_stub, *revnum_used);
    }

  /* Otherwise, we fall back to the old VCC_URL PROPFIND hunt.  */
  else
    {
      const char *vcc_url;

      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, scratch_pool));

      if (SVN_IS_VALID_REVNUM(revision))
        {
          /* First check baseline information cache. */
          SVN_ERR(svn_ra_serf__blncache_get_bc_url(bc_url,
                                                   session->blncache,
                                                   revision, result_pool));
          if (!*bc_url)
            {
              SVN_ERR(retrieve_baseline_info(NULL, bc_url, session,
                                             vcc_url, revision,
                                             result_pool, scratch_pool));
              SVN_ERR(svn_ra_serf__blncache_set(session->blncache, NULL,
                                                revision, *bc_url,
                                                scratch_pool));
            }

          *revnum_used = revision;
        }
      else
        {
          SVN_ERR(v1_get_youngest_revnum(revnum_used, bc_url,
                                         session, vcc_url,
                                         result_pool, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__get_stable_url(const char **stable_url,
                            svn_revnum_t *latest_revnum,
                            svn_ra_serf__session_t *session,
                            const char *url,
                            svn_revnum_t revision,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *basecoll_url;
  const char *repos_relpath;
  svn_revnum_t revnum_used;

  /* No URL? No sweat. We'll use the session URL.  */
  if (! url)
    url = session->session_url.path;

  SVN_ERR(get_baseline_info(&basecoll_url, &revnum_used,
                            session, revision, scratch_pool, scratch_pool));
  SVN_ERR(svn_ra_serf__get_relative_path(&repos_relpath, url,
                                         session, scratch_pool));

  *stable_url = svn_path_url_add_component2(basecoll_url, repos_relpath,
                                            result_pool);
  if (latest_revnum)
    *latest_revnum = revnum_used;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__fetch_dav_prop(const char **value,
                            svn_ra_serf__session_t *session,
                            const char *url,
                            svn_revnum_t revision,
                            const char *propname,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  apr_hash_t *dav_props;

  SVN_ERR(svn_ra_serf__fetch_node_props(&props, session, url, revision,
                                        checked_in_props,
                                        scratch_pool, scratch_pool));
  dav_props = apr_hash_get(props, "DAV:", 4);
  if (dav_props == NULL)
    return svn_error_create(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                            _("The PROPFIND response did not include "
                              "the requested 'DAV:' properties"));

  /* We wouldn't get here if the resource was not found (404), so the
     property should be present.

     Note: it is okay to call apr_pstrdup() with NULL.  */
  *value = apr_pstrdup(result_pool, svn_prop_get_value(dav_props, propname));

  return SVN_NO_ERROR;
}

/* Removes all non regular properties from PROPS */
void
svn_ra_serf__keep_only_regular_props(apr_hash_t *props,
                                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *propname = apr_hash_this_key(hi);

      if (svn_property_kind2(propname) != svn_prop_regular_kind)
        svn_hash_sets(props, propname, NULL);
    }
}
