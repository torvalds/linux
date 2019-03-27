/*
 * commit.c :  entry point for commit RA functions for ra_serf
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
#include "svn_base64.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"

#include "svn_private_config.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"
#include "private/svn_skel.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"


/* Baton passed back with the commit editor. */
typedef struct commit_context_t {
  /* Pool for our commit. */
  apr_pool_t *pool;

  svn_ra_serf__session_t *session;

  apr_hash_t *revprop_table;

  svn_commit_callback2_t callback;
  void *callback_baton;

  apr_hash_t *lock_tokens;
  svn_boolean_t keep_locks;
  apr_hash_t *deleted_entries;   /* deleted files (for delete+add detection) */

  /* HTTP v2 stuff */
  const char *txn_url;           /* txn URL (!svn/txn/TXN_NAME) */
  const char *txn_root_url;      /* commit anchor txn root URL */

  /* HTTP v1 stuff (only valid when 'txn_url' is NULL) */
  const char *activity_url;      /* activity base URL... */
  const char *baseline_url;      /* the working-baseline resource */
  const char *checked_in_url;    /* checked-in root to base CHECKOUTs from */
  const char *vcc_url;           /* vcc url */

  int open_batons;               /* Number of open batons */
} commit_context_t;

#define USING_HTTPV2_COMMIT_SUPPORT(commit_ctx) ((commit_ctx)->txn_url != NULL)

/* Structure associated with a PROPPATCH request. */
typedef struct proppatch_context_t {
  apr_pool_t *pool;

  const char *relpath;
  const char *path;

  commit_context_t *commit_ctx;

  /* Changed properties. const char * -> svn_prop_t * */
  apr_hash_t *prop_changes;

  /* Same, for the old value, or NULL. */
  apr_hash_t *old_props;

  /* In HTTP v2, this is the file/directory version we think we're changing. */
  svn_revnum_t base_revision;

} proppatch_context_t;

typedef struct delete_context_t {
  const char *relpath;

  svn_revnum_t revision;

  commit_context_t *commit_ctx;

  svn_boolean_t non_recursive_if; /* Only create a non-recursive If header */
} delete_context_t;

/* Represents a directory. */
typedef struct dir_context_t {
  /* Pool for our directory. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit_ctx;

  /* URL to operate against (used for CHECKOUT and PROPPATCH before
     HTTP v2, for PROPPATCH in HTTP v2).  */
  const char *url;

  /* Is this directory being added?  (Otherwise, just opened.) */
  svn_boolean_t added;

  /* Our parent */
  struct dir_context_t *parent_dir;

  /* The directory name; if "", we're the 'root' */
  const char *relpath;

  /* The basename of the directory. "" for the 'root' */
  const char *name;

  /* The base revision of the dir. */
  svn_revnum_t base_revision;

  const char *copy_path;
  svn_revnum_t copy_revision;

  /* Changed properties (const char * -> svn_prop_t *) */
  apr_hash_t *prop_changes;

  /* The checked-out working resource for this directory.  May be NULL; if so
     call checkout_dir() first.  */
  const char *working_url;
} dir_context_t;

/* Represents a file to be committed. */
typedef struct file_context_t {
  /* Pool for our file. */
  apr_pool_t *pool;

  /* The root commit we're in progress for. */
  commit_context_t *commit_ctx;

  /* Is this file being added?  (Otherwise, just opened.) */
  svn_boolean_t added;

  dir_context_t *parent_dir;

  const char *relpath;
  const char *name;

  /* The checked-out working resource for this file. */
  const char *working_url;

  /* The base revision of the file. */
  svn_revnum_t base_revision;

  /* Copy path and revision */
  const char *copy_path;
  svn_revnum_t copy_revision;

  /* Stream for collecting the svndiff. */
  svn_stream_t *stream;

  /* Buffer holding the svndiff (can spill to disk). */
  svn_ra_serf__request_body_t *svndiff;

  /* Did we send the svndiff in apply_textdelta_stream()? */
  svn_boolean_t svndiff_sent;

  /* Our base checksum as reported by the WC. */
  const char *base_checksum;

  /* Our resulting checksum as reported by the WC. */
  const char *result_checksum;

  /* Our resulting checksum as reported by the server. */
  svn_checksum_t *remote_result_checksum;

  /* Changed properties (const char * -> svn_prop_t *) */
  apr_hash_t *prop_changes;

  /* URL to PUT the file at. */
  const char *url;

} file_context_t;


/* Setup routines and handlers for various requests we'll invoke. */

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_checkout_body(serf_bucket_t **bkt,
                     void *baton,
                     serf_bucket_alloc_t *alloc,
                     apr_pool_t *pool /* request pool */,
                     apr_pool_t *scratch_pool)
{
  const char *activity_url = baton;
  serf_bucket_t *body_bkt;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body_bkt, alloc);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:checkout",
                                    "xmlns:D", "DAV:",
                                    SVN_VA_NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:activity-set",
                                    SVN_VA_NULL);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:href",
                                    SVN_VA_NULL);

  SVN_ERR_ASSERT(activity_url != NULL);
  svn_ra_serf__add_cdata_len_buckets(body_bkt, alloc,
                                     activity_url,
                                     strlen(activity_url));

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:href");
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:activity-set");
  svn_ra_serf__add_empty_tag_buckets(body_bkt, alloc,
                                     "D:apply-to-version", SVN_VA_NULL);
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:checkout");

  *bkt = body_bkt;
  return SVN_NO_ERROR;
}


/* Using the HTTPv1 protocol, perform a CHECKOUT of NODE_URL within the
   given COMMIT_CTX. The resulting working resource will be returned in
   *WORKING_URL, allocated from RESULT_POOL. All temporary allocations
   are performed in SCRATCH_POOL.

   ### are these URLs actually repos relpath values? or fspath? or maybe
   ### the abspath portion of the full URL.

   This function operates synchronously.

   Strictly speaking, we could perform "all" of the CHECKOUT requests
   when the commit starts, and only block when we need a specific
   answer. Or, at a minimum, send off these individual requests async
   and block when we need the answer (eg PUT or PROPPATCH).

   However: the investment to speed this up is not worthwhile, given
   that CHECKOUT (and the related round trip) is completely obviated
   in HTTPv2.
*/
static svn_error_t *
checkout_node(const char **working_url,
              const commit_context_t *commit_ctx,
              const char *node_url,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_ra_serf__handler_t *handler;
  apr_status_t status;
  apr_uri_t uri;

  /* HANDLER_POOL is the scratch pool since we don't need to remember
     anything from the handler. We just want the working resource.  */
  handler = svn_ra_serf__create_handler(commit_ctx->session, scratch_pool);

  handler->body_delegate = create_checkout_body;
  handler->body_delegate_baton = (/* const */ void *)commit_ctx->activity_url;
  handler->body_type = "text/xml";

  handler->response_handler = svn_ra_serf__expect_empty_body;
  handler->response_baton = handler;

  handler->method = "CHECKOUT";
  handler->path = node_url;

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  if (handler->sline.code != 201)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  if (handler->location == NULL)
    return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                            _("No Location header received"));

  /* We only want the path portion of the Location header.
     (code.google.com sometimes returns an 'http:' scheme for an
     'https:' transaction ... we'll work around that by stripping the
     scheme, host, and port here and re-adding the correct ones
     later.  */
  status = apr_uri_parse(scratch_pool, handler->location, &uri);
  if (status)
    return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                            _("Error parsing Location header value"));

  *working_url = svn_urlpath__canonicalize(uri.path, result_pool);

  return SVN_NO_ERROR;
}


/* This is a wrapper around checkout_node() (which see for
   documentation) which simply retries the CHECKOUT request when it
   fails due to an SVN_ERR_APMOD_BAD_BASELINE error return from the
   server.

   See http://subversion.tigris.org/issues/show_bug.cgi?id=4127 for
   details.
*/
static svn_error_t *
retry_checkout_node(const char **working_url,
                    const commit_context_t *commit_ctx,
                    const char *node_url,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  int retry_count = 5; /* Magic, arbitrary number. */

  do
    {
      svn_error_clear(err);

      err = checkout_node(working_url, commit_ctx, node_url,
                          result_pool, scratch_pool);

      /* There's a small chance of a race condition here if Apache is
         experiencing heavy commit concurrency or if the network has
         long latency.  It's possible that the value of HEAD changed
         between the time we fetched the latest baseline and the time
         we try to CHECKOUT that baseline.  If that happens, Apache
         will throw us a BAD_BASELINE error (deltaV says you can only
         checkout the latest baseline).  We just ignore that specific
         error and retry a few times, asking for the latest baseline
         again. */
      if (err && (err->apr_err != SVN_ERR_APMOD_BAD_BASELINE))
        return svn_error_trace(err);
    }
  while (err && retry_count--);

  return svn_error_trace(err);
}


static svn_error_t *
checkout_dir(dir_context_t *dir,
             apr_pool_t *scratch_pool)
{
  dir_context_t *c_dir = dir;
  const char *checkout_url;
  const char **working;

  if (dir->working_url)
    {
      return SVN_NO_ERROR;
    }

  /* Is this directory or one of our parent dirs newly added?
   * If so, we're already implicitly checked out. */
  while (c_dir)
    {
      if (c_dir->added)
        {
          /* Calculate the working_url by skipping the shared ancestor between
           * the c_dir_parent->relpath and dir->relpath. This is safe since an
           * add is guaranteed to have a parent that is checked out. */
          dir_context_t *c_dir_parent = c_dir->parent_dir;
          const char *relpath = svn_relpath_skip_ancestor(c_dir_parent->relpath,
                                                          dir->relpath);

          /* Implicitly checkout this dir now. */
          SVN_ERR_ASSERT(c_dir_parent->working_url);
          dir->working_url = svn_path_url_add_component2(
                                   c_dir_parent->working_url,
                                   relpath, dir->pool);
          return SVN_NO_ERROR;
        }
      c_dir = c_dir->parent_dir;
    }

  /* We could be called twice for the root: once to checkout the baseline;
   * once to checkout the directory itself if we need to do so.
   * Note: CHECKOUT_URL should live longer than HANDLER.
   */
  if (!dir->parent_dir && !dir->commit_ctx->baseline_url)
    {
      checkout_url = dir->commit_ctx->vcc_url;
      working = &dir->commit_ctx->baseline_url;
    }
  else
    {
      checkout_url = dir->url;
      working = &dir->working_url;
    }

  /* Checkout our directory into the activity URL now. */
  return svn_error_trace(retry_checkout_node(working, dir->commit_ctx,
                                             checkout_url,
                                             dir->pool, scratch_pool));
}


/* Set *CHECKED_IN_URL to the appropriate DAV version url for
 * RELPATH (relative to the root of SESSION).
 *
 * Try to find this version url in three ways:
 * First, if SESSION->callbacks->get_wc_prop() is defined, try to read the
 * version url from the working copy properties.
 * Second, if the version url of the parent directory PARENT_VSN_URL is
 * defined, set *CHECKED_IN_URL to the concatenation of PARENT_VSN_URL with
 * RELPATH.
 * Else, fetch the version url for the root of SESSION using CONN and
 * BASE_REVISION, and set *CHECKED_IN_URL to the concatenation of that
 * with RELPATH.
 *
 * Allocate the result in RESULT_POOL, and use SCRATCH_POOL for
 * temporary allocation.
 */
static svn_error_t *
get_version_url(const char **checked_in_url,
                svn_ra_serf__session_t *session,
                const char *relpath,
                svn_revnum_t base_revision,
                const char *parent_vsn_url,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const char *root_checkout;

  if (session->wc_callbacks->get_wc_prop)
    {
      const svn_string_t *current_version;

      SVN_ERR(session->wc_callbacks->get_wc_prop(
                session->wc_callback_baton,
                relpath,
                SVN_RA_SERF__WC_CHECKED_IN_URL,
                &current_version, scratch_pool));

      if (current_version)
        {
          *checked_in_url =
            svn_urlpath__canonicalize(current_version->data, result_pool);
          return SVN_NO_ERROR;
        }
    }

  if (parent_vsn_url)
    {
      root_checkout = parent_vsn_url;
    }
  else
    {
      const char *propfind_url;

      if (SVN_IS_VALID_REVNUM(base_revision))
        {
          /* mod_dav_svn can't handle the "Label:" header that
             svn_ra_serf__deliver_props() is going to try to use for
             this lookup, so we'll do things the hard(er) way, by
             looking up the version URL from a resource in the
             baseline collection. */
          SVN_ERR(svn_ra_serf__get_stable_url(&propfind_url,
                                              NULL /* latest_revnum */,
                                              session,
                                              NULL /* url */, base_revision,
                                              scratch_pool, scratch_pool));
        }
      else
        {
          propfind_url = session->session_url.path;
        }

      SVN_ERR(svn_ra_serf__fetch_dav_prop(&root_checkout, session,
                                          propfind_url, base_revision,
                                          "checked-in",
                                          scratch_pool, scratch_pool));
      if (!root_checkout)
        return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                 _("Path '%s' not present"),
                                 session->session_url.path);

      root_checkout = svn_urlpath__canonicalize(root_checkout, scratch_pool);
    }

  *checked_in_url = svn_path_url_add_component2(root_checkout, relpath,
                                                result_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
checkout_file(file_context_t *file,
              apr_pool_t *scratch_pool)
{
  dir_context_t *parent_dir = file->parent_dir;
  const char *checkout_url;

  /* Is one of our parent dirs newly added?  If so, we're already
   * implicitly checked out.
   */
  while (parent_dir)
    {
      if (parent_dir->added)
        {
          /* Implicitly checkout this file now. */
          SVN_ERR_ASSERT(parent_dir->working_url);
          file->working_url = svn_path_url_add_component2(
                                    parent_dir->working_url,
                                    svn_relpath_skip_ancestor(
                                      parent_dir->relpath, file->relpath),
                                    file->pool);
          return SVN_NO_ERROR;
        }
      parent_dir = parent_dir->parent_dir;
    }

  SVN_ERR(get_version_url(&checkout_url,
                          file->commit_ctx->session,
                          file->relpath, file->base_revision,
                          NULL, scratch_pool, scratch_pool));

  /* Checkout our file into the activity URL now. */
  return svn_error_trace(retry_checkout_node(&file->working_url,
                                             file->commit_ctx, checkout_url,
                                              file->pool, scratch_pool));
}

/* Helper function for proppatch_walker() below. */
static svn_error_t *
get_encoding_and_cdata(const char **encoding_p,
                       const svn_string_t **encoded_value_p,
                       serf_bucket_alloc_t *alloc,
                       const svn_string_t *value,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  if (value == NULL)
    {
      *encoding_p = NULL;
      *encoded_value_p = NULL;
      return SVN_NO_ERROR;
    }

  /* If a property is XML-safe, XML-encode it.  Else, base64-encode
     it. */
  if (svn_xml_is_xml_safe(value->data, value->len))
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_xml_escape_cdata_string(&xml_esc, value, scratch_pool);
      *encoding_p = NULL;
      *encoded_value_p = svn_string_create_from_buf(xml_esc, result_pool);
    }
  else
    {
      *encoding_p = "base64";
      *encoded_value_p = svn_base64_encode_string2(value, TRUE, result_pool);
    }

  return SVN_NO_ERROR;
}

/* Helper for create_proppatch_body. Writes per property xml to body */
static svn_error_t *
write_prop_xml(const proppatch_context_t *proppatch,
               serf_bucket_t *body_bkt,
               serf_bucket_alloc_t *alloc,
               const svn_prop_t *prop,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  serf_bucket_t *cdata_bkt;
  const char *encoding;
  const svn_string_t *encoded_value;
  const char *prop_name;
  const svn_prop_t *old_prop;

  SVN_ERR(get_encoding_and_cdata(&encoding, &encoded_value, alloc, prop->value,
                                 result_pool, scratch_pool));
  if (encoded_value)
    {
      cdata_bkt = SERF_BUCKET_SIMPLE_STRING_LEN(encoded_value->data,
                                                encoded_value->len,
                                                alloc);
    }
  else
    {
      cdata_bkt = NULL;
    }

  /* Use the namespace prefix instead of adding the xmlns attribute to support
     property names containing ':' */
  if (strncmp(prop->name, SVN_PROP_PREFIX, sizeof(SVN_PROP_PREFIX) - 1) == 0)
    {
      prop_name = apr_pstrcat(result_pool,
                              "S:", prop->name + sizeof(SVN_PROP_PREFIX) - 1,
                              SVN_VA_NULL);
    }
  else
    {
      prop_name = apr_pstrcat(result_pool,
                              "C:", prop->name,
                              SVN_VA_NULL);
    }

  if (cdata_bkt)
    svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, prop_name,
                                      "V:encoding", encoding,
                                      SVN_VA_NULL);
  else
    svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, prop_name,
                                      "V:" SVN_DAV__OLD_VALUE__ABSENT, "1",
                                      SVN_VA_NULL);

  old_prop = proppatch->old_props
                          ? svn_hash_gets(proppatch->old_props, prop->name)
                          : NULL;
  if (old_prop)
    {
      const char *encoding2;
      const svn_string_t *encoded_value2;
      serf_bucket_t *cdata_bkt2;

      SVN_ERR(get_encoding_and_cdata(&encoding2, &encoded_value2,
                                     alloc, old_prop->value,
                                     result_pool, scratch_pool));

      if (encoded_value2)
        {
          cdata_bkt2 = SERF_BUCKET_SIMPLE_STRING_LEN(encoded_value2->data,
                                                     encoded_value2->len,
                                                     alloc);
        }
      else
        {
          cdata_bkt2 = NULL;
        }

      if (cdata_bkt2)
        svn_ra_serf__add_open_tag_buckets(body_bkt, alloc,
                                          "V:" SVN_DAV__OLD_VALUE,
                                          "V:encoding", encoding2,
                                          SVN_VA_NULL);
      else
        svn_ra_serf__add_open_tag_buckets(body_bkt, alloc,
                                          "V:" SVN_DAV__OLD_VALUE,
                                          "V:" SVN_DAV__OLD_VALUE__ABSENT, "1",
                                          SVN_VA_NULL);

      if (cdata_bkt2)
        serf_bucket_aggregate_append(body_bkt, cdata_bkt2);

      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc,
                                         "V:" SVN_DAV__OLD_VALUE);
    }
  if (cdata_bkt)
    serf_bucket_aggregate_append(body_bkt, cdata_bkt);
  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, prop_name);

  return SVN_NO_ERROR;
}

/* Possible add the lock-token "If:" precondition header to HEADERS if
   an examination of COMMIT_CTX and RELPATH indicates that this is the
   right thing to do.

   Generally speaking, if the client provided a lock token for
   RELPATH, it's the right thing to do.  There is a notable instance
   where this is not the case, however.  If the file at RELPATH was
   explicitly deleted in this commit already, then mod_dav removed its
   lock token when it fielded the DELETE request, so we don't want to
   set the lock precondition again.  (See
   http://subversion.tigris.org/issues/show_bug.cgi?id=3674 for details.)
*/
static svn_error_t *
maybe_set_lock_token_header(serf_bucket_t *headers,
                            commit_context_t *commit_ctx,
                            const char *relpath,
                            apr_pool_t *pool)
{
  const char *token;

  if (! commit_ctx->lock_tokens)
    return SVN_NO_ERROR;

  if (! svn_hash_gets(commit_ctx->deleted_entries, relpath))
    {
      token = svn_hash_gets(commit_ctx->lock_tokens, relpath);
      if (token)
        {
          const char *token_header;
          const char *token_uri;
          apr_uri_t uri = commit_ctx->session->session_url;

          /* Supplying the optional URI affects apache response when
             the lock is broken, see issue 4369.  When present any URI
             must be absolute (RFC 2518 9.4). */
          uri.path = (char *)svn_path_url_add_component2(uri.path, relpath,
                                                         pool);
          token_uri = apr_uri_unparse(pool, &uri, 0);

          token_header = apr_pstrcat(pool, "<", token_uri, "> (<", token, ">)",
                                     SVN_VA_NULL);
          serf_bucket_headers_set(headers, "If", token_header);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
setup_proppatch_headers(serf_bucket_t *headers,
                        void *baton,
                        apr_pool_t *pool /* request pool */,
                        apr_pool_t *scratch_pool)
{
  proppatch_context_t *proppatch = baton;

  if (SVN_IS_VALID_REVNUM(proppatch->base_revision))
    {
      serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                              apr_psprintf(pool, "%ld",
                                           proppatch->base_revision));
    }

  if (proppatch->relpath && proppatch->commit_ctx)
    SVN_ERR(maybe_set_lock_token_header(headers, proppatch->commit_ctx,
                                        proppatch->relpath, pool));

  return SVN_NO_ERROR;
}


/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_proppatch_body(serf_bucket_t **bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool /* request pool */,
                      apr_pool_t *scratch_pool)
{
  proppatch_context_t *ctx = baton;
  serf_bucket_t *body_bkt;
  svn_boolean_t opened = FALSE;
  apr_hash_index_t *hi;

  body_bkt = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body_bkt, alloc);
  svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:propertyupdate",
                                    "xmlns:D", "DAV:",
                                    "xmlns:V", SVN_DAV_PROP_NS_DAV,
                                    "xmlns:C", SVN_DAV_PROP_NS_CUSTOM,
                                    "xmlns:S", SVN_DAV_PROP_NS_SVN,
                                    SVN_VA_NULL);

  /* First we write property SETs */
  for (hi = apr_hash_first(scratch_pool, ctx->prop_changes);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_prop_t *prop = apr_hash_this_val(hi);

      if (prop->value
          || (ctx->old_props && svn_hash_gets(ctx->old_props, prop->name)))
        {
          if (!opened)
            {
              opened = TRUE;
              svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:set",
                                                SVN_VA_NULL);
              svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:prop",
                                                SVN_VA_NULL);
            }

          SVN_ERR(write_prop_xml(ctx, body_bkt, alloc, prop,
                                 pool, scratch_pool));
        }
    }

  if (opened)
    {
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:prop");
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:set");
    }

  /* And then property REMOVEs */
  opened = FALSE;

  for (hi = apr_hash_first(scratch_pool, ctx->prop_changes);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_prop_t *prop = apr_hash_this_val(hi);

      if (!prop->value
          && !(ctx->old_props && svn_hash_gets(ctx->old_props, prop->name)))
        {
          if (!opened)
            {
              opened = TRUE;
              svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:remove",
                                                SVN_VA_NULL);
              svn_ra_serf__add_open_tag_buckets(body_bkt, alloc, "D:prop",
                                                SVN_VA_NULL);
            }

          SVN_ERR(write_prop_xml(ctx, body_bkt, alloc, prop,
                                 pool, scratch_pool));
        }
    }

  if (opened)
    {
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:prop");
      svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:remove");
    }

  svn_ra_serf__add_close_tag_buckets(body_bkt, alloc, "D:propertyupdate");

  *bkt = body_bkt;
  return SVN_NO_ERROR;
}

static svn_error_t*
proppatch_resource(svn_ra_serf__session_t *session,
                   proppatch_context_t *proppatch,
                   apr_pool_t *pool)
{
  svn_ra_serf__handler_t *handler;
  svn_error_t *err;

  handler = svn_ra_serf__create_handler(session, pool);

  handler->method = "PROPPATCH";
  handler->path = proppatch->path;

  handler->header_delegate = setup_proppatch_headers;
  handler->header_delegate_baton = proppatch;

  handler->body_delegate = create_proppatch_body;
  handler->body_delegate_baton = proppatch;
  handler->body_type = "text/xml";

  handler->response_handler = svn_ra_serf__handle_multistatus_only;
  handler->response_baton = handler;

  err = svn_ra_serf__context_run_one(handler, pool);

  if (!err && handler->sline.code != 207)
    err = svn_error_trace(svn_ra_serf__unexpected_status(handler));

  /* Use specific error code for property handling errors.
     Use loop to provide the right result with tracing */
  if (err && err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
    {
      svn_error_t *e = err;

      while (e && e->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
        {
          e->apr_err = SVN_ERR_RA_DAV_PROPPATCH_FAILED;
          e = e->child;
        }
    }

  return svn_error_trace(err);
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_empty_put_body(serf_bucket_t **body_bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *pool /* request pool */,
                      apr_pool_t *scratch_pool)
{
  *body_bkt = SERF_BUCKET_SIMPLE_STRING("", alloc);
  return SVN_NO_ERROR;
}

static svn_error_t *
setup_put_headers(serf_bucket_t *headers,
                  void *baton,
                  apr_pool_t *pool /* request pool */,
                  apr_pool_t *scratch_pool)
{
  file_context_t *ctx = baton;

  if (SVN_IS_VALID_REVNUM(ctx->base_revision))
    {
      serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                              apr_psprintf(pool, "%ld", ctx->base_revision));
    }

  if (ctx->base_checksum)
    {
      serf_bucket_headers_set(headers, SVN_DAV_BASE_FULLTEXT_MD5_HEADER,
                              ctx->base_checksum);
    }

  if (ctx->result_checksum)
    {
      serf_bucket_headers_set(headers, SVN_DAV_RESULT_FULLTEXT_MD5_HEADER,
                              ctx->result_checksum);
    }

  SVN_ERR(maybe_set_lock_token_header(headers, ctx->commit_ctx,
                                      ctx->relpath, pool));

  return APR_SUCCESS;
}

static svn_error_t *
setup_copy_file_headers(serf_bucket_t *headers,
                        void *baton,
                        apr_pool_t *pool /* request pool */,
                        apr_pool_t *scratch_pool)
{
  file_context_t *file = baton;
  apr_uri_t uri;
  const char *absolute_uri;

  /* The Dest URI must be absolute.  Bummer. */
  uri = file->commit_ctx->session->session_url;
  uri.path = (char*)file->url;
  absolute_uri = apr_uri_unparse(pool, &uri, 0);

  serf_bucket_headers_set(headers, "Destination", absolute_uri);

  serf_bucket_headers_setn(headers, "Overwrite", "F");

  return SVN_NO_ERROR;
}

static svn_error_t *
setup_if_header_recursive(svn_boolean_t *added,
                          serf_bucket_t *headers,
                          commit_context_t *commit_ctx,
                          const char *rq_relpath,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *sb = NULL;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = NULL;

  if (!commit_ctx->lock_tokens)
    {
      *added = FALSE;
      return SVN_NO_ERROR;
    }

  /* We try to create a directory, so within the Subversion world that
     would imply that there is nothing here, but mod_dav_svn still sees
     locks on the old nodes here as in DAV it is perfectly legal to lock
     something that is not there...

     Let's make mod_dav, mod_dav_svn and the DAV RFC happy by providing
     the locks we know of with the request */

  for (hi = apr_hash_first(pool, commit_ctx->lock_tokens);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *relpath = apr_hash_this_key(hi);
      apr_uri_t uri;

      if (!svn_relpath_skip_ancestor(rq_relpath, relpath))
        continue;
      else if (svn_hash_gets(commit_ctx->deleted_entries, relpath))
        {
          /* When a path is already explicit deleted then its lock
             will be removed by mod_dav. But mod_dav doesn't remove
             locks on descendants */
          continue;
        }

      if (!iterpool)
        iterpool = svn_pool_create(pool);
      else
        svn_pool_clear(iterpool);

      if (sb == NULL)
        sb = svn_stringbuf_create("", pool);
      else
        svn_stringbuf_appendbyte(sb, ' ');

      uri = commit_ctx->session->session_url;
      uri.path = (char *)svn_path_url_add_component2(uri.path, relpath,
                                                    iterpool);

      svn_stringbuf_appendbyte(sb, '<');
      svn_stringbuf_appendcstr(sb, apr_uri_unparse(iterpool, &uri, 0));
      svn_stringbuf_appendcstr(sb, "> (<");
      svn_stringbuf_appendcstr(sb, apr_hash_this_val(hi));
      svn_stringbuf_appendcstr(sb, ">)");
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  if (sb)
    {
      serf_bucket_headers_set(headers, "If", sb->data);
      *added = TRUE;
    }
  else
    *added = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
setup_add_dir_common_headers(serf_bucket_t *headers,
                             void *baton,
                             apr_pool_t *pool /* request pool */,
                             apr_pool_t *scratch_pool)
{
  dir_context_t *dir = baton;
  svn_boolean_t added;

  return svn_error_trace(
      setup_if_header_recursive(&added, headers, dir->commit_ctx, dir->relpath,
                                pool));
}

static svn_error_t *
setup_copy_dir_headers(serf_bucket_t *headers,
                       void *baton,
                       apr_pool_t *pool /* request pool */,
                       apr_pool_t *scratch_pool)
{
  dir_context_t *dir = baton;
  apr_uri_t uri;
  const char *absolute_uri;

  /* The Dest URI must be absolute.  Bummer. */
  uri = dir->commit_ctx->session->session_url;

  if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      uri.path = (char *)dir->url;
    }
  else
    {
      uri.path = (char *)svn_path_url_add_component2(
                                    dir->parent_dir->working_url,
                                    dir->name, pool);
    }
  absolute_uri = apr_uri_unparse(pool, &uri, 0);

  serf_bucket_headers_set(headers, "Destination", absolute_uri);

  serf_bucket_headers_setn(headers, "Depth", "infinity");
  serf_bucket_headers_setn(headers, "Overwrite", "F");

  /* Implicitly checkout this dir now. */
  dir->working_url = apr_pstrdup(dir->pool, uri.path);

  return svn_error_trace(setup_add_dir_common_headers(headers, baton, pool,
                                                      scratch_pool));
}

static svn_error_t *
setup_delete_headers(serf_bucket_t *headers,
                     void *baton,
                     apr_pool_t *pool /* request pool */,
                     apr_pool_t *scratch_pool)
{
  delete_context_t *del = baton;
  svn_boolean_t added;

  serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                          apr_ltoa(pool, del->revision));

  if (! del->non_recursive_if)
    SVN_ERR(setup_if_header_recursive(&added, headers, del->commit_ctx,
                                      del->relpath, pool));
  else
    {
      SVN_ERR(maybe_set_lock_token_header(headers, del->commit_ctx,
                                          del->relpath, pool));
      added = TRUE;
    }

  if (added && del->commit_ctx->keep_locks)
    serf_bucket_headers_setn(headers, SVN_DAV_OPTIONS_HEADER,
                                      SVN_DAV_OPTION_KEEP_LOCKS);

  return SVN_NO_ERROR;
}

/* POST against 'me' resource handlers. */

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_txn_post_body(serf_bucket_t **body_bkt,
                     void *baton,
                     serf_bucket_alloc_t *alloc,
                     apr_pool_t *pool /* request pool */,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *revprops = baton;
  svn_skel_t *request_skel;
  svn_stringbuf_t *skel_str;

  request_skel = svn_skel__make_empty_list(pool);
  if (revprops)
    {
      svn_skel_t *proplist_skel;

      SVN_ERR(svn_skel__unparse_proplist(&proplist_skel, revprops, pool));
      svn_skel__prepend(proplist_skel, request_skel);
      svn_skel__prepend_str("create-txn-with-props", request_skel, pool);
      skel_str = svn_skel__unparse(request_skel, pool);
      *body_bkt = SERF_BUCKET_SIMPLE_STRING(skel_str->data, alloc);
    }
  else
    {
      *body_bkt = SERF_BUCKET_SIMPLE_STRING("( create-txn )", alloc);
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_header_delegate_t */
static svn_error_t *
setup_post_headers(serf_bucket_t *headers,
                   void *baton,
                   apr_pool_t *pool /* request pool */,
                   apr_pool_t *scratch_pool)
{
#ifdef SVN_DAV_SEND_VTXN_NAME
  /* Enable this to exercise the VTXN-NAME code based on a client
     supplied transaction name. */
  serf_bucket_headers_set(headers, SVN_DAV_VTXN_NAME_HEADER,
                          svn_uuid_generate(pool));
#endif

  return SVN_NO_ERROR;
}


/* Handler baton for POST request. */
typedef struct post_response_ctx_t
{
  svn_ra_serf__handler_t *handler;
  commit_context_t *commit_ctx;
} post_response_ctx_t;


/* This implements serf_bucket_headers_do_callback_fn_t.   */
static int
post_headers_iterator_callback(void *baton,
                               const char *key,
                               const char *val)
{
  post_response_ctx_t *prc = baton;
  commit_context_t *prc_cc = prc->commit_ctx;
  svn_ra_serf__session_t *sess = prc_cc->session;

  /* If we provided a UUID to the POST request, we should get back
     from the server an SVN_DAV_VTXN_NAME_HEADER header; otherwise we
     expect the SVN_DAV_TXN_NAME_HEADER.  We certainly don't expect to
     see both. */

  if (svn_cstring_casecmp(key, SVN_DAV_TXN_NAME_HEADER) == 0)
    {
      /* Build out txn and txn-root URLs using the txn name we're
         given, and store the whole lot of it in the commit context.  */
      prc_cc->txn_url =
        svn_path_url_add_component2(sess->txn_stub, val, prc_cc->pool);
      prc_cc->txn_root_url =
        svn_path_url_add_component2(sess->txn_root_stub, val, prc_cc->pool);
    }

  if (svn_cstring_casecmp(key, SVN_DAV_VTXN_NAME_HEADER) == 0)
    {
      /* Build out vtxn and vtxn-root URLs using the vtxn name we're
         given, and store the whole lot of it in the commit context.  */
      prc_cc->txn_url =
        svn_path_url_add_component2(sess->vtxn_stub, val, prc_cc->pool);
      prc_cc->txn_root_url =
        svn_path_url_add_component2(sess->vtxn_root_stub, val, prc_cc->pool);
    }

  return 0;
}


/* A custom serf_response_handler_t which is mostly a wrapper around
   svn_ra_serf__expect_empty_body -- it just notices POST response
   headers, too.

   Implements svn_ra_serf__response_handler_t */
static svn_error_t *
post_response_handler(serf_request_t *request,
                      serf_bucket_t *response,
                      void *baton,
                      apr_pool_t *scratch_pool)
{
  post_response_ctx_t *prc = baton;
  serf_bucket_t *hdrs = serf_bucket_response_get_headers(response);

  /* Then see which ones we can discover. */
  serf_bucket_headers_do(hdrs, post_headers_iterator_callback, prc);

  /* Execute the 'real' response handler to XML-parse the repsonse body. */
  return svn_ra_serf__expect_empty_body(request, response,
                                        prc->handler, scratch_pool);
}



/* Commit baton callbacks */

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *dir_pool,
          void **root_baton)
{
  commit_context_t *commit_ctx = edit_baton;
  svn_ra_serf__handler_t *handler;
  proppatch_context_t *proppatch_ctx;
  dir_context_t *dir;
  apr_hash_index_t *hi;
  const char *proppatch_target = NULL;
  apr_pool_t *scratch_pool = svn_pool_create(dir_pool);

  commit_ctx->open_batons++;

  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(commit_ctx->session))
    {
      post_response_ctx_t *prc;
      const char *rel_path;
      svn_boolean_t post_with_revprops
        = (NULL != svn_hash_gets(commit_ctx->session->supported_posts,
                                 "create-txn-with-props"));

      /* Create our activity URL now on the server. */
      handler = svn_ra_serf__create_handler(commit_ctx->session, scratch_pool);

      handler->method = "POST";
      handler->body_type = SVN_SKEL_MIME_TYPE;
      handler->body_delegate = create_txn_post_body;
      handler->body_delegate_baton =
          post_with_revprops ? commit_ctx->revprop_table : NULL;
      handler->header_delegate = setup_post_headers;
      handler->header_delegate_baton = NULL;
      handler->path = commit_ctx->session->me_resource;

      prc = apr_pcalloc(scratch_pool, sizeof(*prc));
      prc->handler = handler;
      prc->commit_ctx = commit_ctx;

      handler->response_handler = post_response_handler;
      handler->response_baton = prc;

      SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

      if (handler->sline.code != 201)
        return svn_error_trace(svn_ra_serf__unexpected_status(handler));

      if (! (commit_ctx->txn_root_url && commit_ctx->txn_url))
        {
          return svn_error_createf(
            SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
            _("POST request did not return transaction information"));
        }

      /* Fixup the txn_root_url to point to the anchor of the commit. */
      SVN_ERR(svn_ra_serf__get_relative_path(
                                        &rel_path,
                                        commit_ctx->session->session_url.path,
                                        commit_ctx->session,
                                        scratch_pool));
      commit_ctx->txn_root_url = svn_path_url_add_component2(
                                        commit_ctx->txn_root_url,
                                        rel_path, commit_ctx->pool);

      /* Build our directory baton. */
      dir = apr_pcalloc(dir_pool, sizeof(*dir));
      dir->pool = dir_pool;
      dir->commit_ctx = commit_ctx;
      dir->base_revision = base_revision;
      dir->relpath = "";
      dir->name = "";
      dir->prop_changes = apr_hash_make(dir->pool);
      dir->url = apr_pstrdup(dir->pool, commit_ctx->txn_root_url);

      /* If we included our revprops in the POST, we need not
         PROPPATCH them. */
      proppatch_target = post_with_revprops ? NULL : commit_ctx->txn_url;
    }
  else
    {
      const char *activity_str = commit_ctx->session->activity_collection_url;

      if (!activity_str)
        SVN_ERR(svn_ra_serf__v1_get_activity_collection(
                                    &activity_str,
                                    commit_ctx->session,
                                    scratch_pool, scratch_pool));

      commit_ctx->activity_url = svn_path_url_add_component2(
                                    activity_str,
                                    svn_uuid_generate(scratch_pool),
                                    commit_ctx->pool);

      /* Create our activity URL now on the server. */
      handler = svn_ra_serf__create_handler(commit_ctx->session, scratch_pool);

      handler->method = "MKACTIVITY";
      handler->path = commit_ctx->activity_url;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

      if (handler->sline.code != 201)
        return svn_error_trace(svn_ra_serf__unexpected_status(handler));

      /* Now go fetch our VCC and baseline so we can do a CHECKOUT. */
      SVN_ERR(svn_ra_serf__discover_vcc(&(commit_ctx->vcc_url),
                                        commit_ctx->session, scratch_pool));


      /* Build our directory baton. */
      dir = apr_pcalloc(dir_pool, sizeof(*dir));
      dir->pool = dir_pool;
      dir->commit_ctx = commit_ctx;
      dir->base_revision = base_revision;
      dir->relpath = "";
      dir->name = "";
      dir->prop_changes = apr_hash_make(dir->pool);

      SVN_ERR(get_version_url(&dir->url, dir->commit_ctx->session,
                              dir->relpath,
                              dir->base_revision, commit_ctx->checked_in_url,
                              dir->pool, scratch_pool));
      commit_ctx->checked_in_url = apr_pstrdup(commit_ctx->pool, dir->url);

      /* Checkout our root dir */
      SVN_ERR(checkout_dir(dir, scratch_pool));

      proppatch_target = commit_ctx->baseline_url;
    }

  /* Unless this is NULL -- which means we don't need to PROPPATCH the
     transaction with our revprops -- then, you know, PROPPATCH the
     transaction with our revprops.  */
  if (proppatch_target)
    {
      proppatch_ctx = apr_pcalloc(scratch_pool, sizeof(*proppatch_ctx));
      proppatch_ctx->pool = scratch_pool;
      proppatch_ctx->commit_ctx = NULL; /* No lock info */
      proppatch_ctx->path = proppatch_target;
      proppatch_ctx->prop_changes = apr_hash_make(proppatch_ctx->pool);
      proppatch_ctx->base_revision = SVN_INVALID_REVNUM;

      for (hi = apr_hash_first(scratch_pool, commit_ctx->revprop_table);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_prop_t *prop = apr_palloc(scratch_pool, sizeof(*prop));

          prop->name = apr_hash_this_key(hi);
          prop->value = apr_hash_this_val(hi);

          svn_hash_sets(proppatch_ctx->prop_changes, prop->name, prop);
        }

      SVN_ERR(proppatch_resource(commit_ctx->session,
                                 proppatch_ctx, scratch_pool));
    }

  svn_pool_destroy(scratch_pool);

  *root_baton = dir;

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_delete_body(serf_bucket_t **body_bkt,
                   void *baton,
                   serf_bucket_alloc_t *alloc,
                   apr_pool_t *pool /* request pool */,
                   apr_pool_t *scratch_pool)
{
  delete_context_t *ctx = baton;
  serf_bucket_t *body;

  body = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(body, alloc);

  svn_ra_serf__merge_lock_token_list(ctx->commit_ctx->lock_tokens,
                                     ctx->relpath, body, alloc, pool);

  *body_bkt = body;
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  dir_context_t *dir = parent_baton;
  delete_context_t *delete_ctx;
  svn_ra_serf__handler_t *handler;
  const char *delete_target;

  if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      delete_target = svn_path_url_add_component2(
                                    dir->commit_ctx->txn_root_url,
                                    path, dir->pool);
    }
  else
    {
      /* Ensure our directory has been checked out */
      SVN_ERR(checkout_dir(dir, pool /* scratch_pool */));
      delete_target = svn_path_url_add_component2(dir->working_url,
                                                  svn_relpath_basename(path,
                                                                       NULL),
                                                  pool);
    }

  /* DELETE our entry */
  delete_ctx = apr_pcalloc(pool, sizeof(*delete_ctx));
  delete_ctx->relpath = apr_pstrdup(pool, path);
  delete_ctx->revision = revision;
  delete_ctx->commit_ctx = dir->commit_ctx;

  handler = svn_ra_serf__create_handler(dir->commit_ctx->session, pool);

  handler->response_handler = svn_ra_serf__expect_empty_body;
  handler->response_baton = handler;

  handler->header_delegate = setup_delete_headers;
  handler->header_delegate_baton = delete_ctx;

  handler->method = "DELETE";
  handler->path = delete_target;
  handler->no_fail_on_http_failure_status = TRUE;

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  if (handler->sline.code == 400)
    {
      /* Try again with non-standard body to overcome Apache Httpd
         header limit */
      delete_ctx->non_recursive_if = TRUE;

      handler = svn_ra_serf__create_handler(dir->commit_ctx->session, pool);

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      handler->header_delegate = setup_delete_headers;
      handler->header_delegate_baton = delete_ctx;

      handler->method = "DELETE";
      handler->path = delete_target;

      handler->body_type = "text/xml";
      handler->body_delegate = create_delete_body;
      handler->body_delegate_baton = delete_ctx;

      SVN_ERR(svn_ra_serf__context_run_one(handler, pool));
    }

  if (handler->server_error)
    return svn_ra_serf__server_error_create(handler, pool);

  /* 204 No Content: item successfully deleted */
  if (handler->sline.code != 204)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  svn_hash_sets(dir->commit_ctx->deleted_entries,
                apr_pstrdup(dir->commit_ctx->pool, path), (void *)1);

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_revision,
              apr_pool_t *dir_pool,
              void **child_baton)
{
  dir_context_t *parent = parent_baton;
  dir_context_t *dir;
  svn_ra_serf__handler_t *handler;
  apr_status_t status;
  const char *mkcol_target;

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;
  dir->parent_dir = parent;
  dir->commit_ctx = parent->commit_ctx;
  dir->added = TRUE;
  dir->base_revision = SVN_INVALID_REVNUM;
  dir->copy_revision = copyfrom_revision;
  dir->copy_path = apr_pstrdup(dir->pool, copyfrom_path);
  dir->relpath = apr_pstrdup(dir->pool, path);
  dir->name = svn_relpath_basename(dir->relpath, NULL);
  dir->prop_changes = apr_hash_make(dir->pool);

  dir->commit_ctx->open_batons++;

  if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      dir->url = svn_path_url_add_component2(parent->commit_ctx->txn_root_url,
                                             path, dir->pool);
      mkcol_target = dir->url;
    }
  else
    {
      /* Ensure our parent is checked out. */
      SVN_ERR(checkout_dir(parent, dir->pool /* scratch_pool */));

      dir->url = svn_path_url_add_component2(parent->commit_ctx->checked_in_url,
                                             dir->name, dir->pool);
      mkcol_target = svn_path_url_add_component2(
                               parent->working_url,
                               dir->name, dir->pool);
    }

  handler = svn_ra_serf__create_handler(dir->commit_ctx->session, dir->pool);

  handler->response_handler = svn_ra_serf__expect_empty_body;
  handler->response_baton = handler;
  if (!dir->copy_path)
    {
      handler->method = "MKCOL";
      handler->path = mkcol_target;

      handler->header_delegate = setup_add_dir_common_headers;
      handler->header_delegate_baton = dir;
    }
  else
    {
      apr_uri_t uri;
      const char *req_url;

      status = apr_uri_parse(dir->pool, dir->copy_path, &uri);
      if (status)
        {
          return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                   _("Unable to parse URL '%s'"),
                                   dir->copy_path);
        }

      SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                          dir->commit_ctx->session,
                                          uri.path, dir->copy_revision,
                                          dir_pool, dir_pool));

      handler->method = "COPY";
      handler->path = req_url;

      handler->header_delegate = setup_copy_dir_headers;
      handler->header_delegate_baton = dir;
    }
  /* We have the same problem as with DELETE here: if there are too many
     locks, the request fails. But in this case there is no way to retry
     with a non-standard request. #### How to fix? */
  SVN_ERR(svn_ra_serf__context_run_one(handler, dir->pool));

  if (handler->sline.code != 201)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  *child_baton = dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *dir_pool,
               void **child_baton)
{
  dir_context_t *parent = parent_baton;
  dir_context_t *dir;

  dir = apr_pcalloc(dir_pool, sizeof(*dir));

  dir->pool = dir_pool;

  dir->parent_dir = parent;
  dir->commit_ctx = parent->commit_ctx;

  dir->added = FALSE;
  dir->base_revision = base_revision;
  dir->relpath = apr_pstrdup(dir->pool, path);
  dir->name = svn_relpath_basename(dir->relpath, NULL);
  dir->prop_changes = apr_hash_make(dir->pool);

  dir->commit_ctx->open_batons++;

  if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      dir->url = svn_path_url_add_component2(parent->commit_ctx->txn_root_url,
                                             path, dir->pool);
    }
  else
    {
      SVN_ERR(get_version_url(&dir->url,
                              dir->commit_ctx->session,
                              dir->relpath, dir->base_revision,
                              dir->commit_ctx->checked_in_url,
                              dir->pool, dir->pool /* scratch_pool */));
    }
  *child_baton = dir;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *scratch_pool)
{
  dir_context_t *dir = dir_baton;
  svn_prop_t *prop;

  if (! USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      /* Ensure we have a checked out dir. */
      SVN_ERR(checkout_dir(dir, scratch_pool));
    }

  prop = apr_palloc(dir->pool, sizeof(*prop));

  prop->name = apr_pstrdup(dir->pool, name);
  prop->value = svn_string_dup(value, dir->pool);

  svn_hash_sets(dir->prop_changes, prop->name, prop);

  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  dir_context_t *dir = dir_baton;

  /* Huh?  We're going to be called before the texts are sent.  Ugh.
   * Therefore, just wave politely at our caller.
   */

  /* PROPPATCH our prop change and pass it along.  */
  if (apr_hash_count(dir->prop_changes))
    {
      proppatch_context_t *proppatch_ctx;

      proppatch_ctx = apr_pcalloc(pool, sizeof(*proppatch_ctx));
      proppatch_ctx->pool = pool;
      proppatch_ctx->commit_ctx = NULL /* No lock tokens necessary */;
      proppatch_ctx->relpath = dir->relpath;
      proppatch_ctx->prop_changes = dir->prop_changes;
      proppatch_ctx->base_revision = dir->base_revision;

      if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
        {
          proppatch_ctx->path = dir->url;
        }
      else
        {
          proppatch_ctx->path = dir->working_url;
        }

      SVN_ERR(proppatch_resource(dir->commit_ctx->session,
                                 proppatch_ctx, dir->pool));
    }

  dir->commit_ctx->open_batons--;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copy_path,
         svn_revnum_t copy_revision,
         apr_pool_t *file_pool,
         void **file_baton)
{
  dir_context_t *dir = parent_baton;
  file_context_t *new_file;
  const char *deleted_parent = path;
  apr_pool_t *scratch_pool = svn_pool_create(file_pool);

  new_file = apr_pcalloc(file_pool, sizeof(*new_file));
  new_file->pool = file_pool;

  new_file->parent_dir = dir;
  new_file->commit_ctx = dir->commit_ctx;
  new_file->relpath = apr_pstrdup(new_file->pool, path);
  new_file->name = svn_relpath_basename(new_file->relpath, NULL);
  new_file->added = TRUE;
  new_file->base_revision = SVN_INVALID_REVNUM;
  new_file->copy_path = apr_pstrdup(new_file->pool, copy_path);
  new_file->copy_revision = copy_revision;
  new_file->prop_changes = apr_hash_make(new_file->pool);

  dir->commit_ctx->open_batons++;

  /* Ensure that the file doesn't exist by doing a HEAD on the
     resource.  If we're using HTTP v2, we'll just look into the
     transaction root tree for this thing.  */
  if (USING_HTTPV2_COMMIT_SUPPORT(dir->commit_ctx))
    {
      new_file->url = svn_path_url_add_component2(dir->commit_ctx->txn_root_url,
                                                  path, new_file->pool);
    }
  else
    {
      /* Ensure our parent directory has been checked out */
      SVN_ERR(checkout_dir(dir, scratch_pool));

      new_file->url =
        svn_path_url_add_component2(dir->working_url,
                                    new_file->name, new_file->pool);
    }

  while (deleted_parent && deleted_parent[0] != '\0')
    {
      if (svn_hash_gets(dir->commit_ctx->deleted_entries, deleted_parent))
        {
          break;
        }
      deleted_parent = svn_relpath_dirname(deleted_parent, file_pool);
    }

  if (copy_path)
    {
      svn_ra_serf__handler_t *handler;
      apr_uri_t uri;
      const char *req_url;
      apr_status_t status;

      /* Create the copy directly as cheap 'does exist/out of date'
         check. We update the copy (if needed) from close_file() */

      status = apr_uri_parse(scratch_pool, copy_path, &uri);
      if (status)
        return svn_ra_serf__wrap_err(status, NULL);

      SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                          dir->commit_ctx->session,
                                          uri.path, copy_revision,
                                          scratch_pool, scratch_pool));

      handler = svn_ra_serf__create_handler(dir->commit_ctx->session,
                                            scratch_pool);
      handler->method = "COPY";
      handler->path = req_url;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      handler->header_delegate = setup_copy_file_headers;
      handler->header_delegate_baton = new_file;

      SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

      if (handler->sline.code != 201)
        return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }
  else if (! ((dir->added && !dir->copy_path) ||
           (deleted_parent && deleted_parent[0] != '\0')))
    {
      svn_ra_serf__handler_t *handler;
      svn_error_t *err;

      handler = svn_ra_serf__create_handler(dir->commit_ctx->session,
                                            scratch_pool);
      handler->method = "HEAD";
      handler->path = svn_path_url_add_component2(
                                        dir->commit_ctx->session->session_url.path,
                                        path, scratch_pool);
      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;
      handler->no_dav_headers = TRUE; /* Read only operation outside txn */

      err = svn_ra_serf__context_run_one(handler, scratch_pool);

      if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err); /* Great. We can create a new file! */
        }
      else if (err)
        return svn_error_trace(err);
      else
        return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                 _("File '%s' already exists"), path);
    }

  svn_pool_destroy(scratch_pool);
  *file_baton = new_file;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *file_pool,
          void **file_baton)
{
  dir_context_t *parent = parent_baton;
  file_context_t *new_file;

  new_file = apr_pcalloc(file_pool, sizeof(*new_file));
  new_file->pool = file_pool;

  new_file->parent_dir = parent;
  new_file->commit_ctx = parent->commit_ctx;
  new_file->relpath = apr_pstrdup(new_file->pool, path);
  new_file->name = svn_relpath_basename(new_file->relpath, NULL);
  new_file->added = FALSE;
  new_file->base_revision = base_revision;
  new_file->prop_changes = apr_hash_make(new_file->pool);

  parent->commit_ctx->open_batons++;

  if (USING_HTTPV2_COMMIT_SUPPORT(parent->commit_ctx))
    {
      new_file->url = svn_path_url_add_component2(parent->commit_ctx->txn_root_url,
                                                  path, new_file->pool);
    }
  else
    {
      /* CHECKOUT the file into our activity. */
      SVN_ERR(checkout_file(new_file, new_file->pool /* scratch_pool */));

      new_file->url = new_file->working_url;
    }

  *file_baton = new_file;

  return SVN_NO_ERROR;
}

static void
negotiate_put_encoding(int *svndiff_version_p,
                       int *svndiff_compression_level_p,
                       svn_ra_serf__session_t *session)
{
  int svndiff_version;
  int compression_level;

  if (session->using_compression == svn_tristate_unknown)
    {
      /* With http-compression=auto, prefer svndiff2 to svndiff1 with a
       * low latency connection (assuming the underlying network has high
       * bandwidth), as it is faster and in this case, we don't care about
       * worse compression ratio.
       *
       * Note: For future compatibility, we also handle a theoretically
       * possible case where the server has advertised only svndiff2 support.
       */
      if (session->supports_svndiff2 &&
          svn_ra_serf__is_low_latency_connection(session))
        svndiff_version = 2;
      else if (session->supports_svndiff1)
        svndiff_version = 1;
      else if (session->supports_svndiff2)
        svndiff_version = 2;
      else
        svndiff_version = 0;
    }
  else if (session->using_compression == svn_tristate_true)
    {
      /* Otherwise, prefer svndiff1, as svndiff2 is not a reasonable
       * substitute for svndiff1 with default compression level.  (It gives
       * better speed and compression ratio comparable to svndiff1 with
       * compression level 1, but not 5).
       *
       * Note: For future compatibility, we also handle a theoretically
       * possible case where the server has advertised only svndiff2 support.
       */
      if (session->supports_svndiff1)
        svndiff_version = 1;
      else if (session->supports_svndiff2)
        svndiff_version = 2;
      else
        svndiff_version = 0;
    }
  else
    {
      /* Difference between svndiff formats 0 and 1/2 that format 1/2 allows
       * compression.  Uncompressed svndiff0 should also be slightly more
       * effective if the compression is not required at all.
       *
       * If the server cannot handle svndiff1/2, or compression is disabled
       * with the 'http-compression = no' client configuration option, fall
       * back to uncompressed svndiff0 format.  As a bonus, users can force
       * the usage of the uncompressed format by setting the corresponding
       * client configuration option, if they want to.
       */
      svndiff_version = 0;
    }

  if (svndiff_version == 0)
    compression_level = SVN_DELTA_COMPRESSION_LEVEL_NONE;
  else
    compression_level = SVN_DELTA_COMPRESSION_LEVEL_DEFAULT;

  *svndiff_version_p = svndiff_version;
  *svndiff_compression_level_p = compression_level;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  file_context_t *ctx = file_baton;
  int svndiff_version;
  int compression_level;

  /* Construct a holder for the request body; we'll give it to serf when we
   * close this file.
   *
   * Please note that if this callback is used, large request bodies will
   * be spilled into temporary files (that requires disk space and prevents
   * simultaneous processing by the server and the client).  A better approach
   * that streams the request body is implemented in apply_textdelta_stream().
   * It will be used with most recent servers having the "send result checksum
   * in response to a PUT" capability, and only if the editor driver uses the
   * new callback.
   */
  ctx->svndiff =
    svn_ra_serf__request_body_create(SVN_RA_SERF__REQUEST_BODY_IN_MEM_SIZE,
                                     ctx->pool);
  ctx->stream = svn_ra_serf__request_body_get_stream(ctx->svndiff);

  negotiate_put_encoding(&svndiff_version, &compression_level,
                         ctx->commit_ctx->session);
  /* Disown the stream; we'll close it explicitly in close_file(). */
  svn_txdelta_to_svndiff3(handler, handler_baton,
                          svn_stream_disown(ctx->stream, pool),
                          svndiff_version, compression_level, pool);

  if (base_checksum)
    ctx->base_checksum = apr_pstrdup(ctx->pool, base_checksum);

  return SVN_NO_ERROR;
}

typedef struct open_txdelta_baton_t
{
  svn_ra_serf__session_t *session;
  svn_txdelta_stream_open_func_t open_func;
  void *open_baton;
  svn_error_t *err;
} open_txdelta_baton_t;

static void
txdelta_stream_errfunc(void *baton, svn_error_t *err)
{
  open_txdelta_baton_t *b = baton;

  /* Remember extended error info from the stream bucket.  Note that
   * theoretically this errfunc could be called multiple times -- say,
   * if the request gets restarted after an error.  Compose the errors
   * so we don't leak one of them if this happens. */
  b->err = svn_error_compose_create(b->err, svn_error_dup(err));
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_body_from_txdelta_stream(serf_bucket_t **body_bkt,
                                void *baton,
                                serf_bucket_alloc_t *alloc,
                                apr_pool_t *pool /* request pool */,
                                apr_pool_t *scratch_pool)
{
  open_txdelta_baton_t *b = baton;
  svn_txdelta_stream_t *txdelta_stream;
  svn_stream_t *stream;
  int svndiff_version;
  int compression_level;

  SVN_ERR(b->open_func(&txdelta_stream, b->open_baton, pool, scratch_pool));

  negotiate_put_encoding(&svndiff_version, &compression_level, b->session);
  stream = svn_txdelta_to_svndiff_stream(txdelta_stream, svndiff_version,
                                         compression_level, pool);
  *body_bkt = svn_ra_serf__create_stream_bucket(stream, alloc,
                                                txdelta_stream_errfunc, b);

  return SVN_NO_ERROR;
}

/* Handler baton for PUT request. */
typedef struct put_response_ctx_t
{
  svn_ra_serf__handler_t *handler;
  file_context_t *file_ctx;
} put_response_ctx_t;

/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
put_response_handler(serf_request_t *request,
                     serf_bucket_t *response,
                     void *baton,
                     apr_pool_t *scratch_pool)
{
  put_response_ctx_t *prc = baton;
  serf_bucket_t *hdrs;
  const char *val;

  hdrs = serf_bucket_response_get_headers(response);
  val = serf_bucket_headers_get(hdrs, SVN_DAV_RESULT_FULLTEXT_MD5_HEADER);
  SVN_ERR(svn_checksum_parse_hex(&prc->file_ctx->remote_result_checksum,
                                 svn_checksum_md5, val, prc->file_ctx->pool));

  return svn_error_trace(
           svn_ra_serf__expect_empty_body(request, response,
                                          prc->handler, scratch_pool));
}

static svn_error_t *
apply_textdelta_stream(const svn_delta_editor_t *editor,
                       void *file_baton,
                       const char *base_checksum,
                       svn_txdelta_stream_open_func_t open_func,
                       void *open_baton,
                       apr_pool_t *scratch_pool)
{
  file_context_t *ctx = file_baton;
  open_txdelta_baton_t open_txdelta_baton = {0};
  svn_ra_serf__handler_t *handler;
  put_response_ctx_t *prc;
  int expected_result;
  svn_error_t *err;

  /* Remember that we have sent the svndiff.  A case when we need to
   * perform a zero-byte file PUT (during add_file, close_file editor
   * sequences) is handled in close_file().
   */
  ctx->svndiff_sent = TRUE;
  ctx->base_checksum = base_checksum;

  handler = svn_ra_serf__create_handler(ctx->commit_ctx->session,
                                        scratch_pool);
  handler->method = "PUT";
  handler->path = ctx->url;

  prc = apr_pcalloc(scratch_pool, sizeof(*prc));
  prc->handler = handler;
  prc->file_ctx = ctx;

  handler->response_handler = put_response_handler;
  handler->response_baton = prc;

  open_txdelta_baton.session = ctx->commit_ctx->session;
  open_txdelta_baton.open_func = open_func;
  open_txdelta_baton.open_baton = open_baton;
  open_txdelta_baton.err = SVN_NO_ERROR;

  handler->body_delegate = create_body_from_txdelta_stream;
  handler->body_delegate_baton = &open_txdelta_baton;
  handler->body_type = SVN_SVNDIFF_MIME_TYPE;

  handler->header_delegate = setup_put_headers;
  handler->header_delegate_baton = ctx;

  err = svn_ra_serf__context_run_one(handler, scratch_pool);
  /* Do we have an error from the stream bucket?  If yes, use it. */
  if (open_txdelta_baton.err)
    {
      svn_error_clear(err);
      return svn_error_trace(open_txdelta_baton.err);
    }
  else if (err)
    return svn_error_trace(err);

  if (ctx->added && !ctx->copy_path)
    expected_result = 201; /* Created */
  else
    expected_result = 204; /* Updated */

  if (handler->sline.code != expected_result)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  file_context_t *file = file_baton;
  svn_prop_t *prop;

  prop = apr_palloc(file->pool, sizeof(*prop));

  prop->name = apr_pstrdup(file->pool, name);
  prop->value = svn_string_dup(value, file->pool);

  svn_hash_sets(file->prop_changes, prop->name, prop);

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *scratch_pool)
{
  file_context_t *ctx = file_baton;
  svn_boolean_t put_empty_file = FALSE;

  ctx->result_checksum = text_checksum;

  /* If we got no stream of changes, but this is an added-without-history
   * file, make a note that we'll be PUTting a zero-byte file to the server.
   */
  if ((!ctx->svndiff) && ctx->added && (!ctx->copy_path))
    put_empty_file = TRUE;

  /* If we have a stream of changes, push them to the server... */
  if ((ctx->svndiff || put_empty_file) && !ctx->svndiff_sent)
    {
      svn_ra_serf__handler_t *handler;
      int expected_result;

      handler = svn_ra_serf__create_handler(ctx->commit_ctx->session,
                                            scratch_pool);

      handler->method = "PUT";
      handler->path = ctx->url;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      if (put_empty_file)
        {
          handler->body_delegate = create_empty_put_body;
          handler->body_delegate_baton = ctx;
          handler->body_type = "text/plain";
        }
      else
        {
          SVN_ERR(svn_stream_close(ctx->stream));

          svn_ra_serf__request_body_get_delegate(&handler->body_delegate,
                                                 &handler->body_delegate_baton,
                                                 ctx->svndiff);
          handler->body_type = SVN_SVNDIFF_MIME_TYPE;
        }

      handler->header_delegate = setup_put_headers;
      handler->header_delegate_baton = ctx;

      SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

      if (ctx->added && ! ctx->copy_path)
        expected_result = 201; /* Created */
      else
        expected_result = 204; /* Updated */

      if (handler->sline.code != expected_result)
        return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  /* Don't keep open file handles longer than necessary. */
  if (ctx->svndiff)
    SVN_ERR(svn_ra_serf__request_body_cleanup(ctx->svndiff, scratch_pool));

  /* If we had any prop changes, push them via PROPPATCH. */
  if (apr_hash_count(ctx->prop_changes))
    {
      proppatch_context_t *proppatch;

      proppatch = apr_pcalloc(scratch_pool, sizeof(*proppatch));
      proppatch->pool = scratch_pool;
      proppatch->relpath = ctx->relpath;
      proppatch->path = ctx->url;
      proppatch->commit_ctx = ctx->commit_ctx;
      proppatch->prop_changes = ctx->prop_changes;
      proppatch->base_revision = ctx->base_revision;

      SVN_ERR(proppatch_resource(ctx->commit_ctx->session,
                                 proppatch, scratch_pool));
    }

  if (ctx->result_checksum && ctx->remote_result_checksum)
    {
      svn_checksum_t *result_checksum;

      SVN_ERR(svn_checksum_parse_hex(&result_checksum, svn_checksum_md5,
                                     ctx->result_checksum, scratch_pool));

      if (!svn_checksum_match(result_checksum, ctx->remote_result_checksum))
        return svn_checksum_mismatch_err(result_checksum,
                                         ctx->remote_result_checksum,
                                         scratch_pool,
                                         _("Checksum mismatch for '%s'"),
                                         svn_dirent_local_style(ctx->relpath,
                                                                scratch_pool));
    }

  ctx->commit_ctx->open_batons--;

  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;
  const char *merge_target =
    ctx->activity_url ? ctx->activity_url : ctx->txn_url;
  const svn_commit_info_t *commit_info;
  svn_error_t *err = NULL;

  if (ctx->open_batons > 0)
    return svn_error_create(
              SVN_ERR_FS_INCORRECT_EDITOR_COMPLETION, NULL,
              _("Closing editor with directories or files open"));

  /* MERGE our activity */
  SVN_ERR(svn_ra_serf__run_merge(&commit_info,
                                 ctx->session,
                                 merge_target,
                                 ctx->lock_tokens,
                                 ctx->keep_locks,
                                 pool, pool));

  ctx->txn_url = NULL; /* If HTTPv2, the txn is now done */

  /* Inform the WC that we did a commit.  */
  if (ctx->callback)
    err = ctx->callback(commit_info, ctx->callback_baton, pool);

  /* If we're using activities, DELETE our completed activity.  */
  if (ctx->activity_url)
    {
      svn_ra_serf__handler_t *handler;

      handler = svn_ra_serf__create_handler(ctx->session, pool);

      handler->method = "DELETE";
      handler->path = ctx->activity_url;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      ctx->activity_url = NULL; /* Don't try again in abort_edit() on fail */

      SVN_ERR(svn_error_compose_create(
                  err,
                  svn_ra_serf__context_run_one(handler, pool)));

      if (handler->sline.code != 204)
        return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  SVN_ERR(err);

  return SVN_NO_ERROR;
}

static svn_error_t *
abort_edit(void *edit_baton,
           apr_pool_t *pool)
{
  commit_context_t *ctx = edit_baton;
  svn_ra_serf__handler_t *handler;

  /* If an activity or transaction wasn't even created, don't bother
     trying to delete it. */
  if (! (ctx->activity_url || ctx->txn_url))
    return SVN_NO_ERROR;

  /* An error occurred on conns[0]. serf 0.4.0 remembers that the connection
     had a problem. We need to reset it, in order to use it again.  */
  serf_connection_reset(ctx->session->conns[0]->conn);

  /* DELETE our aborted activity */
  handler = svn_ra_serf__create_handler(ctx->session, pool);

  handler->method = "DELETE";

  handler->response_handler = svn_ra_serf__expect_empty_body;
  handler->response_baton = handler;
  handler->no_fail_on_http_failure_status = TRUE;

  if (USING_HTTPV2_COMMIT_SUPPORT(ctx)) /* HTTP v2 */
    handler->path = ctx->txn_url;
  else
    handler->path = ctx->activity_url;

  SVN_ERR(svn_ra_serf__context_run_one(handler, pool));

  /* 204 if deleted,
     403 if DELETE was forbidden (indicates MKACTIVITY was forbidden too),
     404 if the activity wasn't found. */
  if (handler->sline.code != 204
      && handler->sline.code != 403
      && handler->sline.code != 404)
    {
      return svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  /* Don't delete again if somebody aborts twice */
  ctx->activity_url = NULL;
  ctx->txn_url = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_commit_editor(svn_ra_session_t *ra_session,
                               const svn_delta_editor_t **ret_editor,
                               void **edit_baton,
                               apr_hash_t *revprop_table,
                               svn_commit_callback2_t callback,
                               void *callback_baton,
                               apr_hash_t *lock_tokens,
                               svn_boolean_t keep_locks,
                               apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_delta_editor_t *editor;
  commit_context_t *ctx;
  const char *repos_root;
  const char *base_relpath;
  svn_boolean_t supports_ephemeral_props;

  ctx = apr_pcalloc(pool, sizeof(*ctx));

  ctx->pool = pool;

  ctx->session = session;

  ctx->revprop_table = svn_prop_hash_dup(revprop_table, pool);

  /* If the server supports ephemeral properties, add some carrying
     interesting version information. */
  SVN_ERR(svn_ra_serf__has_capability(ra_session, &supports_ephemeral_props,
                                      SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS,
                                      pool));
  if (supports_ephemeral_props)
    {
      svn_hash_sets(ctx->revprop_table,
                    apr_pstrdup(pool, SVN_PROP_TXN_CLIENT_COMPAT_VERSION),
                    svn_string_create(SVN_VER_NUMBER, pool));
      svn_hash_sets(ctx->revprop_table,
                    apr_pstrdup(pool, SVN_PROP_TXN_USER_AGENT),
                    svn_string_create(session->useragent, pool));
    }

  ctx->callback = callback;
  ctx->callback_baton = callback_baton;

  ctx->lock_tokens = (lock_tokens && apr_hash_count(lock_tokens))
                       ? lock_tokens : NULL;
  ctx->keep_locks = keep_locks;

  ctx->deleted_entries = apr_hash_make(ctx->pool);

  editor = svn_delta_default_editor(pool);
  editor->open_root = open_root;
  editor->delete_entry = delete_entry;
  editor->add_directory = add_directory;
  editor->open_directory = open_directory;
  editor->change_dir_prop = change_dir_prop;
  editor->close_directory = close_directory;
  editor->add_file = add_file;
  editor->open_file = open_file;
  editor->apply_textdelta = apply_textdelta;
  editor->change_file_prop = change_file_prop;
  editor->close_file = close_file;
  editor->close_edit = close_edit;
  editor->abort_edit = abort_edit;
  /* Only install the callback that allows streaming PUT request bodies
   * if the server has the necessary capability.  Otherwise, this will
   * fallback to the default implementation using the temporary files.
   * See default_editor.c:apply_textdelta_stream(). */
  if (session->supports_put_result_checksum)
    editor->apply_textdelta_stream = apply_textdelta_stream;

  *ret_editor = editor;
  *edit_baton = ctx;

  SVN_ERR(svn_ra_serf__get_repos_root(ra_session, &repos_root, pool));
  base_relpath = svn_uri_skip_ancestor(repos_root, session->session_url_str,
                                       pool);

  SVN_ERR(svn_editor__insert_shims(ret_editor, edit_baton, *ret_editor,
                                   *edit_baton, repos_root, base_relpath,
                                   session->shim_callbacks, pool, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__change_rev_prop(svn_ra_session_t *ra_session,
                             svn_revnum_t rev,
                             const char *name,
                             const svn_string_t *const *old_value_p,
                             const svn_string_t *value,
                             apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  proppatch_context_t *proppatch_ctx;
  const char *proppatch_target;
  const svn_string_t *tmp_old_value;
  svn_boolean_t atomic_capable = FALSE;
  svn_prop_t *prop;
  svn_error_t *err;

  if (old_value_p || !value)
    SVN_ERR(svn_ra_serf__has_capability(ra_session, &atomic_capable,
                                        SVN_RA_CAPABILITY_ATOMIC_REVPROPS,
                                        pool));

  if (old_value_p)
    {
      /* How did you get past the same check in svn_ra_change_rev_prop2()? */
      SVN_ERR_ASSERT(atomic_capable);
    }
  else if (! value && atomic_capable)
    {
      svn_string_t *old_value;
      /* mod_dav_svn doesn't report a failure when a property delete fails. The
         atomic revprop change behavior is a nice workaround, to allow getting
         access to the error anyway.

         Somehow the mod_dav maintainers think that returning an error from
         mod_dav's property delete is an RFC violation.
         See https://issues.apache.org/bugzilla/show_bug.cgi?id=53525 */

      SVN_ERR(svn_ra_serf__rev_prop(ra_session, rev, name, &old_value,
                                    pool));

      if (!old_value)
        return SVN_NO_ERROR; /* Nothing to delete */

      /* The api expects a double const pointer. Let's make one */
      tmp_old_value = old_value;
      old_value_p = &tmp_old_value;
    }

  if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(session))
    {
      proppatch_target = apr_psprintf(pool, "%s/%ld", session->rev_stub, rev);
    }
  else
    {
      const char *vcc_url;

      SVN_ERR(svn_ra_serf__discover_vcc(&vcc_url, session, pool));

      SVN_ERR(svn_ra_serf__fetch_dav_prop(&proppatch_target,
                                          session, vcc_url, rev, "href",
                                          pool, pool));
    }

  /* PROPPATCH our log message and pass it along.  */
  proppatch_ctx = apr_pcalloc(pool, sizeof(*proppatch_ctx));
  proppatch_ctx->pool = pool;
  proppatch_ctx->commit_ctx = NULL; /* No lock headers */
  proppatch_ctx->path = proppatch_target;
  proppatch_ctx->prop_changes = apr_hash_make(pool);
  proppatch_ctx->base_revision = SVN_INVALID_REVNUM;

  if (old_value_p)
    {
      prop = apr_palloc(pool, sizeof (*prop));

      prop->name = name;
      prop->value = *old_value_p;

      proppatch_ctx->old_props = apr_hash_make(pool);
      svn_hash_sets(proppatch_ctx->old_props, prop->name, prop);
    }

  prop = apr_palloc(pool, sizeof (*prop));

  prop->name = name;
  prop->value = value;
  svn_hash_sets(proppatch_ctx->prop_changes, prop->name, prop);

  err = proppatch_resource(session, proppatch_ctx, pool);

  /* Use specific error code for old property value mismatch.
     Use loop to provide the right result with tracing */
  if (err && err->apr_err == SVN_ERR_RA_DAV_PRECONDITION_FAILED)
    {
      svn_error_t *e = err;

      while (e && e->apr_err == SVN_ERR_RA_DAV_PRECONDITION_FAILED)
        {
          e->apr_err = SVN_ERR_FS_PROP_BASEVALUE_MISMATCH;
          e = e->child;
        }
    }

  return svn_error_trace(err);
}
