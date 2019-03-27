/*
 * list.c :  entry point for the list RA function in ra_serf
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
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_config.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_time.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"
#include "svn_private_config.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"



/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum list_state_e {
  INITIAL = XML_STATE_INITIAL,
  REPORT,
  ITEM,
  AUTHOR
};

typedef struct list_context_t {
  apr_pool_t *pool;

  /* parameters set by our caller */
  const char *path;
  svn_revnum_t revision;
  const apr_array_header_t *patterns;
  svn_depth_t depth;
  apr_uint32_t dirent_fields;
  apr_array_header_t *props;

  /* Buffer the author info for the current item.
   * We use the AUTHOR pointer to differentiate between 0-length author
   * strings and missing / NULL authors. */
  const char *author;
  svn_stringbuf_t *author_buf;

  /* log receiver function and baton */
  svn_ra_dirent_receiver_t receiver;
  void *receiver_baton;
} list_context_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t log_ttable[] = {
  { INITIAL, S_, "list-report", REPORT,
    FALSE, { NULL }, FALSE },

  { REPORT, S_, "item", ITEM,
    TRUE, { "node-kind", "?size", "?has-props", "?created-rev",
             "?date", NULL }, TRUE },

  { ITEM, D_, "creator-displayname", AUTHOR,
    TRUE, { "?encoding", NULL }, TRUE },

  { 0 }
};

/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
item_closed(svn_ra_serf__xml_estate_t *xes,
            void *baton,
            int leaving_state,
            const svn_string_t *cdata,
            apr_hash_t *attrs,
            apr_pool_t *scratch_pool)
{
  list_context_t *list_ctx = baton;

  if (leaving_state == AUTHOR)
    {
      /* For compatibility with liveprops, current servers will not use
       * base64-encoding for "binary" user names bu simply drop the
       * offending control chars.
       *
       * We might want to switch to revprop-style encoding, though,
       * and this is the code to do that. */
      const char *encoding = svn_hash_gets(attrs, "encoding");
      if (encoding)
        {
          /* Check for a known encoding type.  This is easy -- there's
             only one.  */
          if (strcmp(encoding, "base64") != 0)
            {
              return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                       _("Unsupported encoding '%s'"),
                                       encoding);
            }

          cdata = svn_base64_decode_string(cdata, scratch_pool);
        }

      /* Remember until the next ITEM closing tag. */
      svn_stringbuf_set(list_ctx->author_buf, cdata->data);
      list_ctx->author = list_ctx->author_buf->data;
    }
  else if (leaving_state == ITEM)
    {
      const char *dirent_path = cdata->data;
      const char *kind_word, *date, *crev, *size;
      svn_dirent_t dirent = { 0 };

      kind_word = svn_hash_gets(attrs, "node-kind");
      size = svn_hash_gets(attrs, "size");

      dirent.has_props = svn_hash__get_bool(attrs, "has-props", FALSE);
      crev = svn_hash_gets(attrs, "created-rev");
      date = svn_hash_gets(attrs, "date");

      /* Convert data. */
      dirent.kind = svn_node_kind_from_word(kind_word);

      if (size)
        SVN_ERR(svn_cstring_atoi64(&dirent.size, size));
      else
        dirent.size = SVN_INVALID_FILESIZE;

      if (crev)
        SVN_ERR(svn_revnum_parse(&dirent.created_rev, crev, NULL));
      else
        dirent.created_rev = SVN_INVALID_REVNUM;

      if (date)
        SVN_ERR(svn_time_from_cstring(&dirent.time, date, scratch_pool));

      if (list_ctx->author)
        dirent.last_author = list_ctx->author;

      /* Invoke RECEIVER */
      SVN_ERR(list_ctx->receiver(dirent_path, &dirent,
                                 list_ctx->receiver_baton, scratch_pool));

      /* Reset buffered info. */
      list_ctx->author = NULL;
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_list_body(serf_bucket_t **body_bkt,
                 void *baton,
                 serf_bucket_alloc_t *alloc,
                 apr_pool_t *pool /* request pool */,
                 apr_pool_t *scratch_pool)
{
  serf_bucket_t *buckets;
  list_context_t *list_ctx = baton;
  int i;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc,
                                    "S:list-report",
                                    "xmlns:S", SVN_XML_NAMESPACE,
                                    SVN_VA_NULL);

  svn_ra_serf__add_tag_buckets(buckets,
                               "S:path", list_ctx->path,
                               alloc);
  svn_ra_serf__add_tag_buckets(buckets,
                               "S:revision",
                               apr_ltoa(pool, list_ctx->revision),
                               alloc);
  svn_ra_serf__add_tag_buckets(buckets,
                               "S:depth", svn_depth_to_word(list_ctx->depth),
                               alloc);

  if (list_ctx->patterns)
    {
      for (i = 0; i < list_ctx->patterns->nelts; i++)
        {
          char *name = APR_ARRAY_IDX(list_ctx->patterns, i, char *);
          svn_ra_serf__add_tag_buckets(buckets,
                                       "S:pattern", name,
                                       alloc);
        }
      if (list_ctx->patterns->nelts == 0)
        {
          svn_ra_serf__add_empty_tag_buckets(buckets, alloc,
                                             "S:no-patterns", SVN_VA_NULL);
        }
    }

  for (i = 0; i < list_ctx->props->nelts; i++)
    {
      const svn_ra_serf__dav_props_t *prop
        = &APR_ARRAY_IDX(list_ctx->props, i, const svn_ra_serf__dav_props_t);
      const char *name
        = apr_pstrcat(pool, prop->xmlns, prop->name, SVN_VA_NULL);

      svn_ra_serf__add_tag_buckets(buckets, "S:prop", name, alloc);
    }

  svn_ra_serf__add_close_tag_buckets(buckets, alloc,
                                     "S:list-report");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra_serf__list(svn_ra_session_t *ra_session,
                  const char *path,
                  svn_revnum_t revision,
                  const apr_array_header_t *patterns,
                  svn_depth_t depth,
                  apr_uint32_t dirent_fields,
                  svn_ra_dirent_receiver_t receiver,
                  void *receiver_baton,
                  apr_pool_t *scratch_pool)
{
  list_context_t *list_ctx;
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *req_url;

  list_ctx = apr_pcalloc(scratch_pool, sizeof(*list_ctx));
  list_ctx->pool = scratch_pool;
  list_ctx->receiver = receiver;
  list_ctx->receiver_baton = receiver_baton;
  list_ctx->path = path;
  list_ctx->revision = revision;
  list_ctx->patterns = patterns;
  list_ctx->depth = depth;
  list_ctx->dirent_fields = dirent_fields;
  list_ctx->props = svn_ra_serf__get_dirent_props(dirent_fields, session,
                                                  scratch_pool);
  list_ctx->author_buf = svn_stringbuf_create_empty(scratch_pool);

  /* At this point, we may have a deleted file.  So, we'll match ra_neon's
   * behavior and use the larger of start or end as our 'peg' rev.
   */
  SVN_ERR(svn_ra_serf__get_stable_url(&req_url, NULL /* latest_revnum */,
                                      session,
                                      NULL /* url */, revision,
                                      scratch_pool, scratch_pool));

  xmlctx = svn_ra_serf__xml_context_create(log_ttable,
                                           NULL, item_closed, NULL,
                                           list_ctx,
                                           scratch_pool);
  handler = svn_ra_serf__create_expat_handler(session, xmlctx, NULL,
                                              scratch_pool);

  handler->method = "REPORT";
  handler->path = req_url;
  handler->body_delegate = create_list_body;
  handler->body_delegate_baton = list_ctx;
  handler->body_type = "text/xml";

  SVN_ERR(svn_ra_serf__context_run_one(handler, scratch_pool));

  if (handler->sline.code != 200)
    SVN_ERR(svn_ra_serf__unexpected_status(handler));

  return SVN_NO_ERROR;
}
