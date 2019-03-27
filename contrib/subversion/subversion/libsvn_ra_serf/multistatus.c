/*
 * multistatus.c : parse multistatus (error) responses.
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



#include <assert.h>

#include <apr.h>

#include <serf.h>
#include <serf_bucket_types.h>

#include "svn_private_config.h"
#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"

#include "ra_serf.h"

/* The current state of our XML parsing. */
typedef enum iprops_state_e {
  INITIAL = XML_STATE_INITIAL,
  MS_MULTISTATUS,

  MS_RESPONSE,
  MS_RESPONSE_HREF,

  MS_PROPSTAT,
  MS_PROPSTAT_PROP,
  MS_PROPSTAT_PROP_NAME,
  MS_PROPSTAT_STATUS,
  MS_PROPSTAT_RESPONSEDESCRIPTION,
  MS_PROPSTAT_ERROR,
  MS_PROPSTAT_ERROR_HUMANREADABLE,

  MS_RESPONSE_STATUS,
  MS_RESPONSE_RESPONSEDESCRIPTION,
  MS_RESPONSE_ERROR,
  MS_RESPONSE_ERROR_HUMANREADABLE,

  MS_MULTISTATUS_RESPONSEDESCRIPTION,

  D_ERROR,
  S_ERROR,
  M_ERROR_HUMANREADABLE
} iprops_state_e;

/*
  <D:multistatus xmlns:D="DAV:">
    <D:response>
      <D:href>http://something</D:href>
      <!-- Possibly multiple D:href elements -->
      <D:status>HTTP/1.1 500 failed</D:status>
      <D:error>
        <S:human-readable code="12345">
          Some Subversion error
        </S:human-readable>
      </D:error>
      <D:responsedescription>
        Human readable description
      </D:responsedescription>
      <D:location>http://redirected</D:location>
    </D:response>
    ...
  </D:multistatus>

  Or for property operations:

  <D:multistatus xmlns:D="DAV:">
    <D:response>
      <D:href>http://somewhere-else</D:href>
      <D:propstat>
        <D:propname><C:myprop /></D:propname>
        <D:status>HTTP/1.1 499 failed</D:status>
        <D:error>
          <S:human-readable code="12345">
            Some Subversion error
          </S:human-readable>
        </D:error>
        <D:responsedescription>
          Human readable description
        </D:responsedescription>
      </D:propstat>
      <D:status>HTTP/1.1 499 failed</D:status>
      <D:error>
        <S:human-readable code="12345">
          Some Subversion error
        </S:human-readable>
      </D:error>
      <D:responsedescription>
        Human readable description
      </D:responsedescription>
      <D:location>http://redirected</D:location>
    <D:responsedescription>
      Global description
    <D:responsedescription>
  </D:multistatus>

  Or on request failures
  <D:error>
    <X:some-error xmlns="QQ" />
    <D:human-readable code="12345">
          Some Subversion error
    </D:human-readable>
  </D:error>
 */

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
#define M_ "http://apache.org/dav/xmlns"
static const svn_ra_serf__xml_transition_t multistatus_ttable[] = {
  { INITIAL, D_, "multistatus", MS_MULTISTATUS,
    FALSE, { NULL }, FALSE },

  { MS_MULTISTATUS, D_, "responsedescription", MS_MULTISTATUS_RESPONSEDESCRIPTION,
    TRUE, { NULL }, TRUE },

  /* <response> */
  { MS_MULTISTATUS, D_, "response", MS_RESPONSE,
    FALSE, { NULL }, TRUE },

  { MS_RESPONSE, D_, "href", MS_RESPONSE_HREF,
    TRUE, { NULL }, TRUE },

  /* <propstat> */
  { MS_RESPONSE, D_, "propstat", MS_PROPSTAT,
    FALSE, { NULL }, TRUE },

  { MS_PROPSTAT, D_, "prop", MS_PROPSTAT_PROP,
    FALSE, { NULL }, FALSE },

  { MS_PROPSTAT_PROP, "", "*", MS_PROPSTAT_PROP_NAME,
    FALSE, { NULL }, FALSE },

  { MS_PROPSTAT, D_, "status", MS_PROPSTAT_STATUS,
    TRUE, { NULL }, TRUE },

  { MS_PROPSTAT, D_, "responsedescription", MS_PROPSTAT_RESPONSEDESCRIPTION,
    TRUE, { NULL }, TRUE },

  { MS_PROPSTAT, D_, "error", MS_PROPSTAT_ERROR,
    FALSE, { NULL }, FALSE },

  { MS_PROPSTAT_ERROR, M_, "human-readable", MS_PROPSTAT_ERROR_HUMANREADABLE,
    TRUE, { "?errcode", NULL }, TRUE },
  /* </propstat> */


  { MS_RESPONSE, D_, "status", MS_RESPONSE_STATUS,
    TRUE, { NULL }, TRUE },

  { MS_RESPONSE, D_, "responsedescription", MS_RESPONSE_RESPONSEDESCRIPTION,
    TRUE, { NULL }, TRUE },

  { MS_RESPONSE, D_, "error", MS_RESPONSE_ERROR,
    FALSE, { NULL }, TRUE },

  { MS_RESPONSE_ERROR, M_, "human-readable", MS_RESPONSE_ERROR_HUMANREADABLE,
    TRUE, { "?errcode", NULL }, TRUE },

  /* </response> */

  { MS_MULTISTATUS, D_, "responsedescription", MS_MULTISTATUS_RESPONSEDESCRIPTION,
    TRUE, { NULL }, TRUE },


  { INITIAL, D_, "error", D_ERROR,
    FALSE, { NULL }, TRUE },

  { D_ERROR, S_, "error", S_ERROR,
    FALSE, { NULL }, FALSE },

  { D_ERROR, M_, "human-readable", M_ERROR_HUMANREADABLE,
    TRUE, { "?errcode", NULL }, TRUE },

  { 0 }
};

/* Given a string like "HTTP/1.1 500 (status)" in BUF, parse out the numeric
   status code into *STATUS_CODE_OUT.  Ignores leading whitespace. */
static svn_error_t *
parse_status_line(int *status_code_out,
                  const char **reason,
                  const char *status_line,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *token;
  char *tok_status;
  svn_stringbuf_t *temp_buf = svn_stringbuf_create(status_line, scratch_pool);

  svn_stringbuf_strip_whitespace(temp_buf);
  token = apr_strtok(temp_buf->data, " \t\r\n", &tok_status);
  if (token)
    token = apr_strtok(NULL, " \t\r\n", &tok_status);
  if (!token)
    return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                             _("Malformed DAV:status '%s'"),
                             status_line);
  err = svn_cstring_atoi(status_code_out, token);
  if (err)
    return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, err,
                             _("Malformed DAV:status '%s'"),
                             status_line);

  token = apr_strtok(NULL, " \t\r\n", &tok_status);

  *reason = apr_pstrdup(result_pool, token);

  return SVN_NO_ERROR;
}


typedef struct error_item_t
{
  const char *path;
  const char *propname;

  int http_status;
  const char *http_reason;
  apr_status_t apr_err;

  const char *message;
} error_item_t;

static svn_error_t *
multistatus_opened(svn_ra_serf__xml_estate_t *xes,
                   void *baton,
                   int entered_state,
                   const svn_ra_serf__dav_props_t *tag,
                   apr_pool_t *scratch_pool)
{
  /*struct svn_ra_serf__server_error_t *server_error = baton;*/
  const char *propname;

  switch (entered_state)
    {
      case MS_PROPSTAT_PROP_NAME:
        if (strcmp(tag->xmlns, SVN_DAV_PROP_NS_SVN) == 0)
          propname = apr_pstrcat(scratch_pool, SVN_PROP_PREFIX, tag->name,
                                 SVN_VA_NULL);
        else
          propname = tag->name;
        svn_ra_serf__xml_note(xes, MS_PROPSTAT, "propname", propname);
        break;
      case S_ERROR:
        /* This toggles an has error boolean in libsvn_ra_neon in 1.7 */
        break;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
multistatus_closed(svn_ra_serf__xml_estate_t *xes,
                   void *baton,
                   int leaving_state,
                   const svn_string_t *cdata,
                   apr_hash_t *attrs,
                   apr_pool_t *scratch_pool)
{
  struct svn_ra_serf__server_error_t *server_error = baton;
  const char *errcode;
  const char *status;

  switch (leaving_state)
    {
      case MS_RESPONSE_HREF:
        {
          apr_status_t result;
          apr_uri_t uri;

          result = apr_uri_parse(scratch_pool, cdata->data, &uri);
          if (result)
            return svn_ra_serf__wrap_err(result, NULL);
          svn_ra_serf__xml_note(xes, MS_RESPONSE, "path",
                                svn_urlpath__canonicalize(uri.path, scratch_pool));
        }
        break;
      case MS_RESPONSE_STATUS:
        svn_ra_serf__xml_note(xes, MS_RESPONSE, "status", cdata->data);
        break;
      case MS_RESPONSE_ERROR_HUMANREADABLE:
        svn_ra_serf__xml_note(xes, MS_RESPONSE, "human-readable", cdata->data);
        errcode = svn_hash_gets(attrs, "errcode");
        if (errcode)
          svn_ra_serf__xml_note(xes, MS_RESPONSE, "errcode", errcode);
        break;
      case MS_RESPONSE:
        if ((status = svn_hash__get_cstring(attrs, "status", NULL)) != NULL)
          {
            error_item_t *item;

            item = apr_pcalloc(server_error->pool, sizeof(*item));

            item->path = apr_pstrdup(server_error->pool,
                                     svn_hash_gets(attrs, "path"));

            SVN_ERR(parse_status_line(&item->http_status,
                                      &item->http_reason,
                                      status,
                                      server_error->pool,
                                      scratch_pool));

            /* Do we have a mod_dav specific message? */
            item->message = svn_hash_gets(attrs, "human-readable");

            if (item->message)
              {
                if ((errcode = svn_hash_gets(attrs, "errcode")) != NULL)
                  {
                    apr_int64_t val;

                    SVN_ERR(svn_cstring_atoi64(&val, errcode));
                    item->apr_err = (apr_status_t)val;
                  }

                item->message = apr_pstrdup(server_error->pool, item->message);
              }
            else
              item->message = apr_pstrdup(server_error->pool,
                                          svn_hash_gets(attrs, "description"));

            APR_ARRAY_PUSH(server_error->items, error_item_t *) = item;
          }
        break;


      case MS_PROPSTAT_STATUS:
        svn_ra_serf__xml_note(xes, MS_PROPSTAT, "status", cdata->data);
        break;
      case MS_PROPSTAT_ERROR_HUMANREADABLE:
        svn_ra_serf__xml_note(xes, MS_PROPSTAT, "human-readable", cdata->data);
        errcode = svn_hash_gets(attrs, "errcode");
        if (errcode)
          svn_ra_serf__xml_note(xes, MS_PROPSTAT, "errcode", errcode);
        break;
      case MS_PROPSTAT_RESPONSEDESCRIPTION:
        svn_ra_serf__xml_note(xes, MS_PROPSTAT, "description",
                              cdata->data);
        break;

      case MS_PROPSTAT:
        if ((status = svn_hash__get_cstring(attrs, "status", NULL)) != NULL)
          {
            apr_hash_t *response_attrs;
            error_item_t *item;

            response_attrs = svn_ra_serf__xml_gather_since(xes, MS_RESPONSE);
            item = apr_pcalloc(server_error->pool, sizeof(*item));

            item->path = apr_pstrdup(server_error->pool,
                                     svn_hash_gets(response_attrs, "path"));
            item->propname = apr_pstrdup(server_error->pool,
                                         svn_hash_gets(attrs, "propname"));

            SVN_ERR(parse_status_line(&item->http_status,
                                      &item->http_reason,
                                      status,
                                      server_error->pool,
                                      scratch_pool));

            /* Do we have a mod_dav specific message? */
            item->message = svn_hash_gets(attrs, "human-readable");

            if (item->message)
              {
                if ((errcode = svn_hash_gets(attrs, "errcode")) != NULL)
                  {
                    apr_int64_t val;

                    SVN_ERR(svn_cstring_atoi64(&val, errcode));
                    item->apr_err = (apr_status_t)val;
                  }

                item->message = apr_pstrdup(server_error->pool, item->message);
              }
            else
              item->message = apr_pstrdup(server_error->pool,
                                          svn_hash_gets(attrs, "description"));


            APR_ARRAY_PUSH(server_error->items, error_item_t *) = item;
          }
        break;

      case M_ERROR_HUMANREADABLE:
        svn_ra_serf__xml_note(xes, D_ERROR, "human-readable", cdata->data);
        errcode = svn_hash_gets(attrs, "errcode");
        if (errcode)
          svn_ra_serf__xml_note(xes, D_ERROR, "errcode", errcode);
        break;

      case D_ERROR:
        {
          error_item_t *item;

          item = apr_pcalloc(server_error->pool, sizeof(*item));

          item->http_status = server_error->handler->sline.code;

          /* Do we have a mod_dav specific message? */
          item->message = svn_hash__get_cstring(attrs, "human-readable",
                                                NULL);

          if (item->message)
            {
              if ((errcode = svn_hash_gets(attrs, "errcode")) != NULL)
                {
                  apr_int64_t val;

                  SVN_ERR(svn_cstring_atoi64(&val, errcode));
                  item->apr_err = (apr_status_t)val;
                }

              item->message = apr_pstrdup(server_error->pool, item->message);
            }


          APR_ARRAY_PUSH(server_error->items, error_item_t *) = item;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__server_error_create(svn_ra_serf__handler_t *handler,
                                 apr_pool_t *scratch_pool)
{
  svn_ra_serf__server_error_t *server_error = handler->server_error;
  svn_error_t *err = NULL;
  int i;

  for (i = 0; i < server_error->items->nelts; i++)
    {
      const error_item_t *item;
      apr_status_t status;
      const char *message;
      svn_error_t *new_err;

      item = APR_ARRAY_IDX(server_error->items, i, error_item_t *);

      if (!item->apr_err && item->http_status == 200)
        {
          continue; /* Success code */
        }
      else if (!item->apr_err && item->http_status == 424 && item->propname)
        {
          continue; /* Failed because other PROPPATCH operations failed */
        }

      if (item->apr_err)
        status = item->apr_err;
      else
        switch (item->http_status)
          {
            case 0:
              continue; /* Not an error */
            case 301:
            case 302:
            case 303:
            case 307:
            case 308:
              status = SVN_ERR_RA_DAV_RELOCATED;
              break;
            case 403:
              status = SVN_ERR_RA_DAV_FORBIDDEN;
              break;
            case 404:
              status = SVN_ERR_FS_NOT_FOUND;
              break;
            case 409:
              status = SVN_ERR_FS_CONFLICT;
              break;
            case 412:
              status = SVN_ERR_RA_DAV_PRECONDITION_FAILED;
              break;
            case 423:
              status = SVN_ERR_FS_NO_LOCK_TOKEN;
              break;
            case 500:
              status = SVN_ERR_RA_DAV_REQUEST_FAILED;
              break;
            case 501:
              status = SVN_ERR_UNSUPPORTED_FEATURE;
              break;
            default:
              if (err)
                status = err->apr_err; /* Just use previous */
              else
                status = SVN_ERR_RA_DAV_REQUEST_FAILED;
              break;
        }

      if (item->message && *item->message)
        {
          svn_stringbuf_t *sb = svn_stringbuf_create(item->message,
                                                     scratch_pool);

          svn_stringbuf_strip_whitespace(sb);
          message = sb->data;
        }
      else if (item->propname)
        {
          message = apr_psprintf(scratch_pool,
                                 _("Property operation on '%s' failed"),
                                 item->propname);
        }
      else
        {
          /* Yuck: Older servers sometimes assume that we get convertable
                   apr error codes, while mod_dav_svn just produces a blank
                   text error, because err->message is NULL. */
          serf_status_line sline;
          svn_error_t *tmp_err;

          memset(&sline, 0, sizeof(sline));
          sline.code = item->http_status;
          sline.reason = item->http_reason;

          tmp_err = svn_ra_serf__error_on_status(sline, item->path, NULL);

          message = (tmp_err && tmp_err->message)
                       ? apr_pstrdup(scratch_pool, tmp_err->message)
                       : _("<blank error>");
          svn_error_clear(tmp_err);
        }

      SVN_ERR_ASSERT(status > 0);
      new_err = svn_error_create(status, NULL, message);

      if (item->propname)
        new_err = svn_error_createf(new_err->apr_err, new_err,
                                    _("While handling the '%s' property on '%s':"),
                                    item->propname, item->path);
      else if (item->path)
        new_err = svn_error_createf(new_err->apr_err, new_err,
                                    _("While handling the '%s' path:"),
                                    item->path);

      err = svn_error_compose_create(
                    err,
                    new_err);
    }

  /* Theoretically a 207 status can have a 'global' description without a
     global STATUS that summarizes the final result of property/href
     operations.

     We should wrap that around the existing errors if there is one.

     But currently I don't see how mod_dav ever sets that value */

  if (!err)
    {
      /* We should fail.... but why... Who installed us? */
      err = svn_error_trace(svn_ra_serf__unexpected_status(handler));
    }

  return err;
}


svn_error_t *
svn_ra_serf__setup_error_parsing(svn_ra_serf__server_error_t **server_err,
                                 svn_ra_serf__handler_t *handler,
                                 svn_boolean_t expect_207_only,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_ra_serf__server_error_t *ms_baton;
  svn_ra_serf__handler_t *tmp_handler;

  int *expected_status = apr_pcalloc(result_pool,
                                     2 * sizeof(expected_status[0]));

  expected_status[0] = handler->sline.code;

  ms_baton = apr_pcalloc(result_pool, sizeof(*ms_baton));
  ms_baton->pool = result_pool;

  ms_baton->items = apr_array_make(result_pool, 4, sizeof(error_item_t *));
  ms_baton->handler = handler;

  ms_baton->xmlctx = svn_ra_serf__xml_context_create(multistatus_ttable,
                                                     multistatus_opened,
                                                     multistatus_closed,
                                                     NULL,
                                                     ms_baton,
                                                     ms_baton->pool);

  tmp_handler = svn_ra_serf__create_expat_handler(handler->session,
                                                  ms_baton->xmlctx,
                                                  expected_status,
                                                  result_pool);

  /* Ugly way to obtain expat_handler() */
  tmp_handler->sline = handler->sline;
  ms_baton->response_handler = tmp_handler->response_handler;
  ms_baton->response_baton = tmp_handler->response_baton;

  *server_err = ms_baton;
  return SVN_NO_ERROR;
}



/* Implements svn_ra_serf__response_handler_t */
svn_error_t *
svn_ra_serf__handle_multistatus_only(serf_request_t *request,
                                     serf_bucket_t *response,
                                     void *baton,
                                     apr_pool_t *scratch_pool)
{
  svn_ra_serf__handler_t *handler = baton;

  /* This function is just like expect_empty_body() except for the
     XML parsing callbacks. We are looking for very limited pieces of
     the multistatus response.  */

  /* We should see this just once, in order to initialize SERVER_ERROR.
     At that point, the core error processing will take over. If we choose
     not to parse an error, then we'll never return here (because we
     change the response handler).  */
  SVN_ERR_ASSERT(handler->server_error == NULL);

    {
      serf_bucket_t *hdrs;
      const char *val;

      hdrs = serf_bucket_response_get_headers(response);
      val = serf_bucket_headers_get(hdrs, "Content-Type");
      if (val && strncasecmp(val, "text/xml", sizeof("text/xml") - 1) == 0)
        {
          svn_ra_serf__server_error_t *server_err;

          SVN_ERR(svn_ra_serf__setup_error_parsing(&server_err,
                                                   handler,
                                                   TRUE,
                                                   handler->handler_pool,
                                                   handler->handler_pool));

          handler->server_error = server_err;
        }
      else
        {
          /* The body was not text/xml, so we don't know what to do with it.
             Toss anything that arrives.  */
          handler->discard_body = TRUE;
        }
    }

  /* Returning SVN_NO_ERROR will return APR_SUCCESS to serf, which tells it
     to call the response handler again. That will start up the XML parsing,
     or it will be dropped on the floor (per the decision above).  */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__handle_server_error(svn_ra_serf__server_error_t *server_error,
                                 svn_ra_serf__handler_t *handler,
                                 serf_request_t *request,
                                 serf_bucket_t *response,
                                 apr_status_t *serf_status,
                                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = server_error->response_handler(request, response,
                                       server_error->response_baton,
                                       scratch_pool);
 /* If we do not receive an error or it is a non-transient error, return
     immediately.

     APR_EOF will be returned when parsing is complete.

     APR_EAGAIN & WAIT_CONN may be intermittently returned as we proceed through
     parsing and the network has no more data right now.  If we receive that,
     clear the error and return - allowing serf to wait for more data.
     */
  if (!err || SERF_BUCKET_READ_ERROR(err->apr_err))
    {
      /* Perhaps we already parsed some server generated message. Let's pass
         all information we can get.*/
      if (err)
        err = svn_error_compose_create(
          svn_ra_serf__server_error_create(handler, scratch_pool),
          err);

      return svn_error_trace(err);
    }

  if (!APR_STATUS_IS_EOF(err->apr_err))
    {
      *serf_status = err->apr_err;
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  /* Clear the EOF. We don't need it as subversion error.  */
  svn_error_clear(err);
  *serf_status = APR_EOF;

  /* On PROPPATCH we always get status 207, which may or may not imply an
     error status, but let's keep it generic and just do the check for
     any multistatus */
  if (handler->sline.code == 207 /* MULTISTATUS */)
    {
      svn_boolean_t have_error = FALSE;
      int i;

      for (i = 0; i < server_error->items->nelts; i++)
        {
          const error_item_t *item;
          item = APR_ARRAY_IDX(server_error->items, i, error_item_t *);

          if (!item->apr_err && item->http_status == 200)
            {
              continue; /* Success code */
            }

          have_error = TRUE;
          break;
        }

      if (! have_error)
        handler->server_error = NULL; /* We didn't have a server error */
    }

  return SVN_NO_ERROR;
}
