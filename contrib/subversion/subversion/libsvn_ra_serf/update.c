/*
 * update.c :  entry point for update RA functions for ra_serf
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



#define APR_WANT_STRFUNC
#include <apr_version.h>
#include <apr_want.h>

#include <apr_uri.h>

#include <serf.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_dav.h"
#include "svn_xml.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_base64.h"
#include "svn_props.h"

#include "svn_private_config.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"
#include "private/svn_string_private.h"

#include "ra_serf.h"
#include "../libsvn_ra/ra_loader.h"



/*
 * This enum represents the current state of our XML parsing for a REPORT.
 *
 * A little explanation of how the parsing works.  Every time we see
 * an open-directory tag, we enter the OPEN_DIR state.  Likewise, for
 * add-directory, open-file, etc.  When we see the closing variant of the
 * open-directory tag, we'll 'pop' out of that state.
 *
 * Each state has a pool associated with it that can have temporary
 * allocations that will live as long as the tag is opened.  Once
 * the tag is 'closed', the pool will be reused.
 */
typedef enum report_state_e {
  INITIAL = XML_STATE_INITIAL /* = 0 */,
  UPDATE_REPORT,
  TARGET_REVISION,

  OPEN_DIR,
  ADD_DIR,

  OPEN_FILE,
  ADD_FILE,

  DELETE_ENTRY,
  ABSENT_DIR,
  ABSENT_FILE,

  SET_PROP,
  REMOVE_PROP,

  PROP,

  FETCH_FILE,
  FETCH_PROPS,
  TXDELTA,

  CHECKED_IN,
  CHECKED_IN_HREF,

  MD5_CHECKSUM,

  VERSION_NAME,
  CREATIONDATE,
  CREATOR_DISPLAYNAME
} report_state_e;


#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
#define V_ SVN_DAV_PROP_NS_DAV
static const svn_ra_serf__xml_transition_t update_ttable[] = {
  { INITIAL, S_, "update-report", UPDATE_REPORT,
    FALSE, { "?inline-props", "?send-all", NULL }, TRUE },

  { UPDATE_REPORT, S_, "target-revision", TARGET_REVISION,
    FALSE, { "rev", NULL }, TRUE },

  { UPDATE_REPORT, S_, "open-directory", OPEN_DIR,
    FALSE, { "rev", NULL }, TRUE },

  { OPEN_DIR, S_, "open-directory", OPEN_DIR,
    FALSE, { "rev", "name", NULL }, TRUE },

  { ADD_DIR, S_, "open-directory", OPEN_DIR,
    FALSE, { "rev", "name", NULL }, TRUE },

  { OPEN_DIR, S_, "add-directory", ADD_DIR,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev", /*"?bc-url",*/
              NULL }, TRUE },

  { ADD_DIR, S_, "add-directory", ADD_DIR,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev", /*"?bc-url",*/
              NULL }, TRUE },

  { OPEN_DIR, S_, "open-file", OPEN_FILE,
    FALSE, { "rev", "name", NULL }, TRUE },

  { ADD_DIR, S_, "open-file", OPEN_FILE,
    FALSE, { "rev", "name", NULL }, TRUE },

  { OPEN_DIR, S_, "add-file", ADD_FILE,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev",
             "?sha1-checksum", NULL }, TRUE },

  { ADD_DIR, S_, "add-file", ADD_FILE,
    FALSE, { "name", "?copyfrom-path", "?copyfrom-rev",
             "?sha1-checksum", NULL }, TRUE },

  { OPEN_DIR, S_, "delete-entry", DELETE_ENTRY,
    FALSE, { "?rev", "name", NULL }, TRUE },

  { ADD_DIR, S_, "delete-entry", DELETE_ENTRY,
    FALSE, { "?rev", "name", NULL }, TRUE },

  { OPEN_DIR, S_, "absent-directory", ABSENT_DIR,
    FALSE, { "name", NULL }, TRUE },

  { ADD_DIR, S_, "absent-directory", ABSENT_DIR,
    FALSE, { "name", NULL }, TRUE },

  { OPEN_DIR, S_, "absent-file", ABSENT_FILE,
    FALSE, { "name", NULL }, TRUE },

  { ADD_DIR, S_, "absent-file", ABSENT_FILE,
    FALSE, { "name", NULL }, TRUE },


  { OPEN_DIR, D_, "checked-in", CHECKED_IN,
    FALSE, { NULL }, FALSE },

  { ADD_DIR, D_, "checked-in", CHECKED_IN,
    FALSE, { NULL }, FALSE },

  { OPEN_FILE, D_, "checked-in", CHECKED_IN,
    FALSE, { NULL }, FALSE },

  { ADD_FILE, D_, "checked-in", CHECKED_IN,
    FALSE, { NULL }, FALSE },


  { OPEN_DIR, S_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { ADD_DIR, S_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { OPEN_FILE, S_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },

  { ADD_FILE, S_, "set-prop", SET_PROP,
    TRUE, { "name", "?encoding", NULL }, TRUE },


  { OPEN_DIR, S_, "remove-prop", REMOVE_PROP,
    TRUE, { "name", NULL }, TRUE },

  { ADD_DIR, S_, "remove-prop", REMOVE_PROP,
    TRUE, { "name", NULL }, TRUE },

  { OPEN_FILE, S_, "remove-prop", REMOVE_PROP,
    TRUE, { "name", NULL }, TRUE },

  { ADD_FILE, S_, "remove-prop", REMOVE_PROP,
    TRUE, { "name", NULL }, TRUE },

  { OPEN_FILE, S_, "prop", PROP,
    FALSE, { NULL }, FALSE },
  { OPEN_DIR, S_, "prop", PROP,
    FALSE, { NULL }, FALSE },
  { ADD_FILE, S_, "prop", PROP,
    FALSE, { NULL }, FALSE },
  { ADD_DIR, S_, "prop", PROP,
    FALSE, { NULL }, FALSE },

  { OPEN_FILE, S_, "txdelta", TXDELTA,
    FALSE, { "?base-checksum" }, TRUE },

  { ADD_FILE, S_, "txdelta", TXDELTA,
    FALSE, { "?base-checksum" }, TRUE },

  { OPEN_FILE, S_, "fetch-file", FETCH_FILE,
    FALSE, { "?base-checksum", "?sha1-checksum", NULL }, TRUE},

  { ADD_FILE, S_, "fetch-file", FETCH_FILE,
    FALSE, { "?base-checksum", "?sha1-checksum", NULL }, TRUE },

  { CHECKED_IN, D_, "href", CHECKED_IN_HREF,
    TRUE, { NULL }, TRUE },

  { PROP, V_, "md5-checksum", MD5_CHECKSUM,
    TRUE, { NULL }, TRUE },

  /* These are only reported for <= 1.6.x mod_dav_svn */
  { OPEN_DIR, S_, "fetch-props", FETCH_PROPS,
    FALSE, { NULL }, FALSE },
  { OPEN_FILE, S_, "fetch-props", FETCH_PROPS,
    FALSE, { NULL }, FALSE },

  { PROP, D_, "version-name", VERSION_NAME,
    TRUE, { NULL }, TRUE },
  { PROP, D_, "creationdate", CREATIONDATE,
    TRUE, { NULL }, TRUE },
  { PROP, D_, "creator-displayname", CREATOR_DISPLAYNAME,
    TRUE, { NULL }, TRUE },
  { 0 }
};

/* While we process the REPORT response, we will queue up GET and PROPFIND
   requests. For a very large checkout, it is very easy to queue requests
   faster than they are resolved. Thus, we need to pause the XML processing
   (which queues more requests) to avoid queueing too many, with their
   attendant memory costs. When the queue count drops low enough, we will
   resume XML processing.

   Note that we don't want the count to drop to zero. We have multiple
   connections that we want to keep busy. These are also heuristic numbers
   since network and parsing behavior (ie. it doesn't pause immediately)
   can make the measurements quite imprecise.

   We measure outstanding requests as the sum of NUM_ACTIVE_FETCHES and
   NUM_ACTIVE_PROPFINDS in the report_context_t structure.  */
#define REQUEST_COUNT_TO_PAUSE 50
#define REQUEST_COUNT_TO_RESUME 40

#define SPILLBUF_BLOCKSIZE 4096
#define SPILLBUF_MAXBUFFSIZE 131072

#define PARSE_CHUNK_SIZE 8000 /* Copied from xml.c ### Needs tuning */

/* Forward-declare our report context. */
typedef struct report_context_t report_context_t;
typedef struct body_create_baton_t body_create_baton_t;
/*
 * This structure represents the information for a directory.
 */
typedef struct dir_baton_t
{
  struct dir_baton_t *parent_dir;       /* NULL when root */

  apr_pool_t *pool;                     /* Subpool for this directory */

  /* Pointer back to our original report context. */
  report_context_t *ctx;

  const char *relpath;                  /* session relative path */
  const char *base_name;                /* Name of item "" for root */

  /* the canonical url for this directory after updating. (received) */
  const char *url;

  /* The original repos_relpath of this url (via the reporter)
  directly, or via an ancestor. */
  const char *repos_relpath;

  svn_revnum_t base_rev;                /* base revision or NULL for Add */

  const char *copyfrom_path;            /* NULL for open */
  svn_revnum_t copyfrom_rev;            /* SVN_INVALID_REVNUM for open */

  /* controlling dir baton - this is only created in ensure_dir_opened() */
  svn_boolean_t dir_opened;
  void *dir_baton;

  /* How many references to this directory do we still have open? */
  apr_size_t ref_count;

  svn_boolean_t fetch_props;                 /* Use PROPFIND request? */
  svn_ra_serf__handler_t *propfind_handler;
  apr_hash_t *remove_props;

} dir_baton_t;

/*
* This structure represents the information for a file.
*
* This structure is created as we parse the REPORT response and
* once the element is completed, we may create a fetch_ctx_t structure
* to give to serf to retrieve this file.
*/
typedef struct file_baton_t
{
  dir_baton_t *parent_dir;              /* The parent */
  apr_pool_t *pool;                     /* Subpool for this file*/

  const char *relpath;                  /* session relative path */
  const char *base_name;

  /* the canonical url for this directory after updating. (received) */
  const char *url;

  /* The original repos_relpath of this url as reported. */
  const char *repos_relpath;

  /* lock token, if we had one to start off with. */
  const char *lock_token;

  svn_revnum_t base_rev;                /* SVN_INVALID_REVNUM for Add */

  const char *copyfrom_path;            /* NULL for open */
  svn_revnum_t copyfrom_rev;            /* SVN_INVALID_REVNUM for open */

  /* controlling dir baton - this is only created in ensure_file_opened() */
  svn_boolean_t file_opened;
  void *file_baton;

  svn_boolean_t fetch_props;            /* Use PROPFIND request? */
  svn_ra_serf__handler_t *propfind_handler;
  svn_boolean_t found_lock_prop;
  apr_hash_t *remove_props;

  /* Has the server told us to go fetch - only valid if we had it already */
  svn_boolean_t fetch_file;

  /* controlling file_baton and textdelta handler */
  svn_txdelta_window_handler_t txdelta;
  void *txdelta_baton;

  svn_checksum_t *base_md5_checksum;
  svn_checksum_t *final_md5_checksum;
  svn_checksum_t *final_sha1_checksum;

  svn_stream_t *txdelta_stream;         /* Stream that feeds windows when
                                           written to within txdelta*/
} file_baton_t;

/*
 * This structure represents a single request to GET (fetch) a file with
 * its associated Serf session/connection.
 */
typedef struct fetch_ctx_t {

  /* The handler representing this particular fetch.  */
  svn_ra_serf__handler_t *handler;

  svn_ra_serf__session_t *session;

  /* Stores the information for the file we want to fetch. */
  file_baton_t *file;

  /* Have we read our response headers yet? */
  svn_boolean_t read_headers;

  /* This flag is set when our response is aborted before we reach the
   * end and we decide to requeue this request.
   */
  svn_boolean_t aborted_read;
  apr_off_t aborted_read_size;

  /* This is the amount of data that we have read so far. */
  apr_off_t read_size;

  /* If we're writing this file to a stream, this will be non-NULL. */
  svn_stream_t *result_stream;

  /* The base-rev header  */
  const char *delta_base;

} fetch_ctx_t;

/*
 * The master structure for a REPORT request and response.
 */
struct report_context_t {
  apr_pool_t *pool;

  svn_ra_serf__session_t *sess;

  /* Source path and destination path */
  const char *source;
  const char *destination;

  /* Our update target. */
  const char *update_target;

  /* What is the target revision that we want for this REPORT? */
  svn_revnum_t target_rev;

  /* Where are we (used while parsing) */
  dir_baton_t *cur_dir;
  file_baton_t *cur_file;

  /* Have we been asked to ignore ancestry or textdeltas? */
  svn_boolean_t ignore_ancestry;
  svn_boolean_t text_deltas;

  /* Do we want the server to send copyfrom args or not? */
  svn_boolean_t send_copyfrom_args;

  /* Is the server sending everything in one response? */
  svn_boolean_t send_all_mode;

  /* Is the server including properties inline for newly added
     files/dirs? */
  svn_boolean_t add_props_included;

  /* Path -> const char *repos_relpath mapping */
  apr_hash_t *switched_paths;

  /* Our master update editor and baton. */
  const svn_delta_editor_t *editor;
  void *editor_baton;

  /* Stream for collecting the request body. */
  svn_stream_t *body_template;

  /* Buffer holding request body for the REPORT (can spill to disk). */
  svn_ra_serf__request_body_t *body;

  /* number of pending GET requests */
  unsigned int num_active_fetches;

  /* number of pending PROPFIND requests */
  unsigned int num_active_propfinds;

  /* Are we done parsing the REPORT response? */
  svn_boolean_t done;

  /* Did we receive all data from the network? */
  svn_boolean_t report_received;

  /* Did we close the root directory? */
  svn_boolean_t closed_root;
};

static svn_error_t *
create_dir_baton(dir_baton_t **new_dir,
                 report_context_t *ctx,
                 const char *name,
                 apr_pool_t *scratch_pool)
{
  dir_baton_t *parent = ctx->cur_dir;
  apr_pool_t *dir_pool;
  dir_baton_t *dir;

  if (parent)
    dir_pool = svn_pool_create(parent->pool);
  else
    dir_pool = svn_pool_create(ctx->pool);

  dir = apr_pcalloc(dir_pool, sizeof(*dir));
  dir->pool = dir_pool;
  dir->ctx = ctx;

  if (parent)
    {
      dir->parent_dir = parent;
      parent->ref_count++;
    }

  dir->relpath = parent ? svn_relpath_join(parent->relpath, name, dir_pool)
                        : apr_pstrdup(dir_pool, name);
  dir->base_name = svn_relpath_basename(dir->relpath, NULL);

  dir->repos_relpath = svn_hash_gets(ctx->switched_paths, dir->relpath);
  if (!dir->repos_relpath)
    {
      if (parent)
        dir->repos_relpath = svn_relpath_join(parent->repos_relpath, name,
                                              dir_pool);
      else
        dir->repos_relpath = svn_uri_skip_ancestor(ctx->sess->repos_root_str,
                                                   ctx->sess->session_url_str,
                                                   dir_pool);
    }

  dir->base_rev = SVN_INVALID_REVNUM;
  dir->copyfrom_rev = SVN_INVALID_REVNUM;

  dir->ref_count = 1;

  ctx->cur_dir = dir;

  *new_dir = dir;
  return SVN_NO_ERROR;
}

static svn_error_t *
create_file_baton(file_baton_t **new_file,
                  report_context_t *ctx,
                  const char *name,
                  apr_pool_t *scratch_pool)
{
  dir_baton_t *parent = ctx->cur_dir;
  apr_pool_t *file_pool;
  file_baton_t *file;

  file_pool = svn_pool_create(parent->pool);

  file = apr_pcalloc(file_pool, sizeof(*file));
  file->pool = file_pool;

  file->parent_dir = parent;
  parent->ref_count++;

  file->relpath = svn_relpath_join(parent->relpath, name, file_pool);
  file->base_name = svn_relpath_basename(file->relpath, NULL);

  file->repos_relpath = svn_hash_gets(ctx->switched_paths, file->relpath);
  if (!file->repos_relpath)
    file->repos_relpath = svn_relpath_join(parent->repos_relpath, name,
                                           file_pool);

  /* Sane defaults */
  file->base_rev = SVN_INVALID_REVNUM;
  file->copyfrom_rev = SVN_INVALID_REVNUM;

  *new_file = file;

  ctx->cur_file = file;

  return SVN_NO_ERROR;
}

/** Minimum nr. of outstanding requests needed before a new connection is
 *  opened. */
#define REQS_PER_CONN 8

/** This function creates a new connection for this serf session, but only
 * if the number of NUM_ACTIVE_REQS > REQS_PER_CONN or if there currently is
 * only one main connection open.
 */
static svn_error_t *
open_connection_if_needed(svn_ra_serf__session_t *sess, int num_active_reqs)
{
  /* For each REQS_PER_CONN outstanding requests open a new connection, with
   * a minimum of 1 extra connection. */
  if (sess->num_conns == 1 ||
      ((num_active_reqs / REQS_PER_CONN) > sess->num_conns))
    {
      int cur = sess->num_conns;
      apr_status_t status;

      sess->conns[cur] = apr_pcalloc(sess->pool, sizeof(*sess->conns[cur]));
      sess->conns[cur]->bkt_alloc = serf_bucket_allocator_create(sess->pool,
                                                                 NULL, NULL);
      sess->conns[cur]->last_status_code = -1;
      sess->conns[cur]->session = sess;
      status = serf_connection_create2(&sess->conns[cur]->conn,
                                       sess->context,
                                       sess->session_url,
                                       svn_ra_serf__conn_setup,
                                       sess->conns[cur],
                                       svn_ra_serf__conn_closed,
                                       sess->conns[cur],
                                       sess->pool);
      if (status)
        return svn_ra_serf__wrap_err(status, NULL);

      sess->num_conns++;
    }

  return SVN_NO_ERROR;
}

/* Returns best connection for fetching files/properties. */
static svn_ra_serf__connection_t *
get_best_connection(report_context_t *ctx)
{
  svn_ra_serf__connection_t *conn;
  int first_conn = 1;

  /* Skip the first connection if the REPORT response hasn't been completely
     received yet or if we're being told to limit our connections to
     2 (because this could be an attempt to ensure that we do all our
     auxiliary GETs/PROPFINDs on a single connection).

     ### FIXME: This latter requirement (max_connections > 2) is
     ### really just a hack to work around the fact that some update
     ### editor implementations (such as svnrdump's dump editor)
     ### simply can't handle the way ra_serf violates the editor v1
     ### drive ordering requirements.
     ###
     ### See http://subversion.tigris.org/issues/show_bug.cgi?id=4116.
  */
  if (ctx->report_received && (ctx->sess->max_connections > 2))
    first_conn = 0;

  /* If there's only one available auxiliary connection to use, don't bother
     doing all the cur_conn math -- just return that one connection.  */
  if (ctx->sess->num_conns - first_conn == 1)
    {
      conn = ctx->sess->conns[first_conn];
    }
  else
    {
#if SERF_VERSION_AT_LEAST(1, 4, 0)
      /* Often one connection is slower than others, e.g. because the server
         process/thread has to do more work for the particular set of requests.
         In the worst case, when REQUEST_COUNT_TO_RESUME requests are queued
         on such a slow connection, ra_serf will completely stop sending
         requests.

         The method used here selects the connection with the least amount of
         pending requests, thereby giving more work to lightly loaded server
         processes.
       */
      int i, best_conn = first_conn;
      unsigned int min = INT_MAX;
      for (i = first_conn; i < ctx->sess->num_conns; i++)
        {
          serf_connection_t *sc = ctx->sess->conns[i]->conn;
          unsigned int pending = serf_connection_pending_requests(sc);
          if (pending < min)
            {
              min = pending;
              best_conn = i;
            }
        }
      conn = ctx->sess->conns[best_conn];
#else
    /* We don't know how many requests are pending per connection, so just
       cycle them. */
      conn = ctx->sess->conns[ctx->sess->cur_conn];
      ctx->sess->cur_conn++;
      if (ctx->sess->cur_conn >= ctx->sess->num_conns)
        ctx->sess->cur_conn = first_conn;
#endif
    }
  return conn;
}

/** Helpers to open and close directories */

static svn_error_t*
ensure_dir_opened(dir_baton_t *dir,
                  apr_pool_t *scratch_pool)
{
  report_context_t *ctx = dir->ctx;

  if (dir->dir_opened)
    return SVN_NO_ERROR;

  if (dir->base_name[0] == '\0')
    {
      if (ctx->destination
          && ctx->sess->wc_callbacks->invalidate_wc_props)
        {
          SVN_ERR(ctx->sess->wc_callbacks->invalidate_wc_props(
                      ctx->sess->wc_callback_baton,
                      ctx->update_target,
                      SVN_RA_SERF__WC_CHECKED_IN_URL, scratch_pool));
        }

      SVN_ERR(ctx->editor->open_root(ctx->editor_baton, dir->base_rev,
                                     dir->pool,
                                     &dir->dir_baton));
    }
  else
    {
      SVN_ERR(ensure_dir_opened(dir->parent_dir, scratch_pool));

      if (SVN_IS_VALID_REVNUM(dir->base_rev))
        {
          SVN_ERR(ctx->editor->open_directory(dir->relpath,
                                              dir->parent_dir->dir_baton,
                                              dir->base_rev,
                                              dir->pool,
                                              &dir->dir_baton));
        }
      else
        {
          SVN_ERR(ctx->editor->add_directory(dir->relpath,
                                             dir->parent_dir->dir_baton,
                                             dir->copyfrom_path,
                                             dir->copyfrom_rev,
                                             dir->pool,
                                             &dir->dir_baton));
        }
    }

  dir->dir_opened = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
maybe_close_dir(dir_baton_t *dir)
{
  apr_pool_t *scratch_pool = dir->pool;
  dir_baton_t *parent = dir->parent_dir;
  report_context_t *ctx = dir->ctx;

  if (--dir->ref_count)
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR(ensure_dir_opened(dir, dir->pool));

  if (dir->remove_props)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, dir->remove_props);
           hi;
           hi = apr_hash_next(hi))
        {
          SVN_ERR(ctx->editor->change_file_prop(dir->dir_baton,
                                                apr_hash_this_key(hi),
                                                NULL /* value */,
                                                scratch_pool));
        }
    }

  SVN_ERR(dir->ctx->editor->close_directory(dir->dir_baton, scratch_pool));

  svn_pool_destroy(dir->pool /* scratch_pool */);

  if (parent)
    return svn_error_trace(maybe_close_dir(parent));
  else
    return SVN_NO_ERROR;
}

static svn_error_t *
ensure_file_opened(file_baton_t *file,
                   apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor = file->parent_dir->ctx->editor;

  if (file->file_opened)
    return SVN_NO_ERROR;

  /* Ensure our parent is open. */
  SVN_ERR(ensure_dir_opened(file->parent_dir, scratch_pool));

  /* Open (or add) the file. */
  if (SVN_IS_VALID_REVNUM(file->base_rev))
    {
      SVN_ERR(editor->open_file(file->relpath,
                                file->parent_dir->dir_baton,
                                file->base_rev,
                                file->pool,
                                &file->file_baton));
    }
  else
    {
      SVN_ERR(editor->add_file(file->relpath,
                               file->parent_dir->dir_baton,
                               file->copyfrom_path,
                               file->copyfrom_rev,
                               file->pool,
                               &file->file_baton));
    }

  file->file_opened = TRUE;

  return SVN_NO_ERROR;
}


/** Routines called when we are fetching a file */

static svn_error_t *
headers_fetch(serf_bucket_t *headers,
              void *baton,
              apr_pool_t *pool /* request pool */,
              apr_pool_t *scratch_pool)
{
  fetch_ctx_t *fetch_ctx = baton;

  /* note that we have old VC URL */
  if (fetch_ctx->delta_base)
    {
      serf_bucket_headers_setn(headers, SVN_DAV_DELTA_BASE_HEADER,
                               fetch_ctx->delta_base);
      svn_ra_serf__setup_svndiff_accept_encoding(headers, fetch_ctx->session);
    }
  else if (fetch_ctx->session->using_compression != svn_tristate_false)
    {
      serf_bucket_headers_setn(headers, "Accept-Encoding", "gzip");
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
cancel_fetch(serf_request_t *request,
             serf_bucket_t *response,
             int status_code,
             void *baton)
{
  fetch_ctx_t *fetch_ctx = baton;

  /* Uh-oh.  Our connection died on us.
   *
   * The core ra_serf layer will requeue our request - we just need to note
   * that we got cut off in the middle of our song.
   */
  if (!response)
    {
      /* If we already started the fetch and opened the file handle, we need
       * to hold subsequent read() ops until we get back to where we were
       * before the close and we can then resume the textdelta() calls.
       */
      if (fetch_ctx->read_headers)
        {
          if (!fetch_ctx->aborted_read && fetch_ctx->read_size)
            {
              fetch_ctx->aborted_read = TRUE;
              fetch_ctx->aborted_read_size = fetch_ctx->read_size;
            }
          fetch_ctx->read_size = 0;
        }

      return SVN_NO_ERROR;
    }

  /* We have no idea what went wrong. */
  SVN_ERR_MALFUNCTION();
}

/* Wield the editor referenced by INFO to open (or add) the file
   file also associated with INFO, setting properties on the file and
   calling the editor's apply_textdelta() function on it if necessary
   (or if FORCE_APPLY_TEXTDELTA is set).

   Callers will probably want to also see the function that serves
   the opposite purpose of this one, close_updated_file().  */
static svn_error_t *
open_file_txdelta(file_baton_t *file,
                  apr_pool_t *scratch_pool)
{
  const svn_delta_editor_t *editor = file->parent_dir->ctx->editor;

  SVN_ERR_ASSERT(file->txdelta == NULL);

  SVN_ERR(ensure_file_opened(file, scratch_pool));

  /* Get (maybe) a textdelta window handler for transmitting file
     content changes. */
  SVN_ERR(editor->apply_textdelta(file->file_baton,
                                  svn_checksum_to_cstring(
                                                  file->base_md5_checksum,
                                                  scratch_pool),
                                  file->pool,
                                  &file->txdelta,
                                  &file->txdelta_baton));

  return SVN_NO_ERROR;
}

/* Close the file, handling loose ends and cleanup */
static svn_error_t *
close_file(file_baton_t *file,
           apr_pool_t *scratch_pool)
{
  dir_baton_t *parent_dir = file->parent_dir;
  report_context_t *ctx = parent_dir->ctx;

  SVN_ERR(ensure_file_opened(file, scratch_pool));

  /* Set all of the properties we received */
  if (file->remove_props)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, file->remove_props);
           hi;
           hi = apr_hash_next(hi))
        {
          SVN_ERR(ctx->editor->change_file_prop(file->file_baton,
                                                apr_hash_this_key(hi),
                                                NULL /* value */,
                                                scratch_pool));
        }
    }

  /* Check for lock information. */

  /* This works around a bug in some older versions of mod_dav_svn in that it
   * will not send remove-prop in the update report when a lock property
   * disappears when send-all is false.

   ### Given that we only fetch props on additions, is this really necessary?
       Or is it covering up old local copy bugs where we copied locks to other
       paths? */
  if (!ctx->add_props_included
      && file->lock_token && !file->found_lock_prop
      && SVN_IS_VALID_REVNUM(file->base_rev) /* file_is_added */)
    {
      SVN_ERR(ctx->editor->change_file_prop(file->file_baton,
                                            SVN_PROP_ENTRY_LOCK_TOKEN,
                                            NULL,
                                            scratch_pool));
    }

  if (file->url)
    {
      SVN_ERR(ctx->editor->change_file_prop(file->file_baton,
                                            SVN_RA_SERF__WC_CHECKED_IN_URL,
                                            svn_string_create(file->url,
                                                              scratch_pool),
                                            scratch_pool));
    }

  /* Close the file via the editor. */
  SVN_ERR(ctx->editor->close_file(file->file_baton,
                                  svn_checksum_to_cstring(
                                        file->final_md5_checksum,
                                        scratch_pool),
                                  scratch_pool));

  svn_pool_destroy(file->pool);

  SVN_ERR(maybe_close_dir(parent_dir)); /* Remove reference */

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
handle_fetch(serf_request_t *request,
             serf_bucket_t *response,
             void *handler_baton,
             apr_pool_t *pool)
{
  const char *data;
  apr_size_t len;
  apr_status_t status;
  fetch_ctx_t *fetch_ctx = handler_baton;
  file_baton_t *file = fetch_ctx->file;

  /* ### new field. make sure we didn't miss some initialization.  */
  SVN_ERR_ASSERT(fetch_ctx->handler != NULL);

  if (!fetch_ctx->read_headers)
    {
      serf_bucket_t *hdrs;
      const char *val;

      /* If the error code wasn't 200, something went wrong. Don't use the
       * returned data as its probably an error message. Just bail out instead.
       */
      if (fetch_ctx->handler->sline.code != 200)
        {
          fetch_ctx->handler->discard_body = TRUE;
          return SVN_NO_ERROR; /* Will return an error in the DONE handler */
        }

      hdrs = serf_bucket_response_get_headers(response);
      val = serf_bucket_headers_get(hdrs, "Content-Type");

      if (val && svn_cstring_casecmp(val, SVN_SVNDIFF_MIME_TYPE) == 0)
        {
          fetch_ctx->result_stream =
              svn_txdelta_parse_svndiff(file->txdelta,
                                        file->txdelta_baton,
                                        TRUE, file->pool);

          /* Validate the delta base claimed by the server matches
             what we asked for! */
          val = serf_bucket_headers_get(hdrs, SVN_DAV_DELTA_BASE_HEADER);
          if (val && fetch_ctx->delta_base == NULL)
            {
              /* We recieved response with delta base header while we didn't
                 requested it -- report it as error. */
              return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                       _("GET request returned unexpected "
                                         "delta base: %s"), val);
            }
          else if (val && (strcmp(val, fetch_ctx->delta_base) != 0))
            {
              return svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                       _("GET request returned unexpected "
                                         "delta base: %s"), val);
            }
        }
      else
        {
          fetch_ctx->result_stream = NULL;
        }

      fetch_ctx->read_headers = TRUE;
    }

  while (TRUE)
    {
      svn_txdelta_window_t delta_window = { 0 };
      svn_txdelta_op_t delta_op;
      svn_string_t window_data;

      status = serf_bucket_read(response, 8000, &data, &len);
      if (SERF_BUCKET_READ_ERROR(status))
        {
          return svn_ra_serf__wrap_err(status, NULL);
        }

      fetch_ctx->read_size += len;

      if (fetch_ctx->aborted_read)
        {
          apr_off_t skip;
          /* We haven't caught up to where we were before. */
          if (fetch_ctx->read_size < fetch_ctx->aborted_read_size)
            {
              /* Eek.  What did the file shrink or something? */
              if (APR_STATUS_IS_EOF(status))
                {
                  SVN_ERR_MALFUNCTION();
                }

              /* Skip on to the next iteration of this loop. */
              if (status /* includes EAGAIN */)
                return svn_ra_serf__wrap_err(status, NULL);

              continue;
            }

          /* Woo-hoo.  We're back. */
          fetch_ctx->aborted_read = FALSE;

          /* Update data and len to just provide the new data. */
          skip = len - (fetch_ctx->read_size - fetch_ctx->aborted_read_size);
          data += skip;
          len -= (apr_size_t)skip;
        }

      if (fetch_ctx->result_stream)
        SVN_ERR(svn_stream_write(fetch_ctx->result_stream, data, &len));

      /* otherwise, manually construct the text delta window. */
      else if (len)
        {
          window_data.data = data;
          window_data.len = len;

          delta_op.action_code = svn_txdelta_new;
          delta_op.offset = 0;
          delta_op.length = len;

          delta_window.tview_len = len;
          delta_window.num_ops = 1;
          delta_window.ops = &delta_op;
          delta_window.new_data = &window_data;

          /* write to the file located in the info. */
          SVN_ERR(file->txdelta(&delta_window, file->txdelta_baton));
        }

      if (APR_STATUS_IS_EOF(status))
        {
          if (fetch_ctx->result_stream)
            SVN_ERR(svn_stream_close(fetch_ctx->result_stream));
          else
            SVN_ERR(file->txdelta(NULL, file->txdelta_baton));
        }

      /* Report EOF, EEAGAIN and other special errors to serf */
      if (status)
        return svn_ra_serf__wrap_err(status, NULL);
    }
}

/* --------------------------------------------------------- */

/** Wrappers around our various property walkers **/

/* Implements svn_ra_serf__prop_func */
static svn_error_t *
set_file_props(void *baton,
               const char *path,
               const char *ns,
               const char *name,
               const svn_string_t *val,
               apr_pool_t *scratch_pool)
{
  file_baton_t *file = baton;
  report_context_t *ctx = file->parent_dir->ctx;
  const char *prop_name;

  prop_name = svn_ra_serf__svnname_from_wirename(ns, name, scratch_pool);

  if (!prop_name)
    {
      /* This works around a bug in some older versions of
       * mod_dav_svn in that it will not send remove-prop in the update
       * report when a lock property disappears when send-all is false.
       *
       * Therefore, we'll try to look at our properties and see if there's
       * an active lock.  If not, then we'll assume there isn't a lock
       * anymore.
       */
      /* assert(!ctx->add_props_included); // Or we wouldn't be here */
      if (file->lock_token
          && !file->found_lock_prop
          && val
          && strcmp(ns, "DAV:") == 0
          && strcmp(name, "lockdiscovery") == 0)
        {
          char *new_lock;
          new_lock = apr_pstrdup(scratch_pool, val->data);
          apr_collapse_spaces(new_lock, new_lock);

          if (new_lock[0] != '\0')
            file->found_lock_prop = TRUE;
        }

      return SVN_NO_ERROR;
    }

  SVN_ERR(ensure_file_opened(file, scratch_pool));

  SVN_ERR(ctx->editor->change_file_prop(file->file_baton,
                                        prop_name, val,
                                        scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_done_delegate_t */
static svn_error_t *
file_props_done(serf_request_t *request,
                void *baton,
                apr_pool_t *scratch_pool)
{
  file_baton_t *file = baton;
  svn_ra_serf__handler_t *handler = file->propfind_handler;

  if (handler->server_error)
      return svn_error_trace(svn_ra_serf__server_error_create(handler,
                                                              scratch_pool));

  if (handler->sline.code != 207)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  file->parent_dir->ctx->num_active_propfinds--;

  file->fetch_props = FALSE;

  if (file->fetch_file)
    return SVN_NO_ERROR; /* Still processing file request */

  /* Closing the file will automatically deliver the propfind props.
   *
   * Note that closing the directory may dispose the pool containing the
   * handler, which is only a valid operation in this callback, as only
   * after this callback our serf plumbing assumes the request is done. */

  return svn_error_trace(close_file(file, scratch_pool));
}

static svn_error_t *
file_fetch_done(serf_request_t *request,
                void *baton,
                apr_pool_t *scratch_pool)
{
  fetch_ctx_t *fetch_ctx = baton;
  file_baton_t *file = fetch_ctx->file;
  svn_ra_serf__handler_t *handler = fetch_ctx->handler;

  if (handler->server_error)
      return svn_error_trace(svn_ra_serf__server_error_create(handler,
                                                              scratch_pool));

  if (handler->sline.code != 200)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  file->parent_dir->ctx->num_active_fetches--;

  file->fetch_file = FALSE;

  if (file->fetch_props)
    return SVN_NO_ERROR; /* Still processing PROPFIND request */

  /* Closing the file will automatically deliver the propfind props.
   *
   * Note that closing the directory may dispose the pool containing the
   * handler, fetch_ctx, etc. which is only a valid operation in this
   * callback, as only after this callback our serf plumbing assumes the
   * request is done. */
  return svn_error_trace(close_file(file, scratch_pool));
}

/* Initiates additional requests needed for a file when not in "send-all" mode.
 */
static svn_error_t *
fetch_for_file(file_baton_t *file,
               apr_pool_t *scratch_pool)
{
  report_context_t *ctx = file->parent_dir->ctx;
  svn_ra_serf__connection_t *conn;
  svn_ra_serf__handler_t *handler;

  /* Open extra connections if we have enough requests to send. */
  if (ctx->sess->num_conns < ctx->sess->max_connections)
    SVN_ERR(open_connection_if_needed(ctx->sess, ctx->num_active_fetches +
                                                 ctx->num_active_propfinds));

  /* What connection should we go on? */
  conn = get_best_connection(ctx);

  /* Note that we (still) use conn for both requests.. Should we send
     them out on different connections? */

  if (file->fetch_file)
    {
      SVN_ERR(open_file_txdelta(file, scratch_pool));

      if (!ctx->text_deltas
          || file->txdelta == svn_delta_noop_window_handler)
        {
          SVN_ERR(file->txdelta(NULL, file->txdelta_baton));
          file->fetch_file = FALSE;
        }

      if (file->fetch_file
          && file->final_sha1_checksum
          && ctx->sess->wc_callbacks->get_wc_contents)
        {
          svn_error_t *err;
          svn_stream_t *cached_contents = NULL;

          err = ctx->sess->wc_callbacks->get_wc_contents(
                                                ctx->sess->wc_callback_baton,
                                                &cached_contents,
                                                file->final_sha1_checksum,
                                                scratch_pool);

          if (err || !cached_contents)
            svn_error_clear(err); /* ### Can we return some/most errors? */
          else
            {
              /* ### For debugging purposes we could validate the md5 here,
                     but our implementations in libsvn_client already do that
                     for us... */
              SVN_ERR(svn_txdelta_send_stream(cached_contents,
                                              file->txdelta,
                                              file->txdelta_baton,
                                              NULL, scratch_pool));
              SVN_ERR(svn_stream_close(cached_contents));
              file->fetch_file = FALSE;
            }
        }

      if (file->fetch_file)
        {
          fetch_ctx_t *fetch_ctx;

          /* Let's fetch the file with a GET request... */
          SVN_ERR_ASSERT(file->url && file->repos_relpath);

          /* Otherwise, we use a GET request for the file's contents. */

          fetch_ctx = apr_pcalloc(file->pool, sizeof(*fetch_ctx));
          fetch_ctx->file = file;
          fetch_ctx->session = ctx->sess;

          /* Can we somehow get away with just obtaining a DIFF? */
          if (SVN_RA_SERF__HAVE_HTTPV2_SUPPORT(ctx->sess))
            {
              /* If this file is switched vs the editor root we should provide
                 its real url instead of the one calculated from the session root.
              */
              if (SVN_IS_VALID_REVNUM(file->base_rev))
                {
                  fetch_ctx->delta_base = apr_psprintf(file->pool, "%s/%ld/%s",
                                                       ctx->sess->rev_root_stub,
                                                       file->base_rev,
                                                       svn_path_uri_encode(
                                                          file->repos_relpath,
                                                          scratch_pool));
                }
              else if (file->copyfrom_path)
                {
                  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(file->copyfrom_rev));

                  fetch_ctx->delta_base = apr_psprintf(file->pool, "%s/%ld/%s",
                                                       ctx->sess->rev_root_stub,
                                                       file->copyfrom_rev,
                                                       svn_path_uri_encode(
                                                          file->copyfrom_path+1,
                                                          scratch_pool));
                }
            }
          else if (ctx->sess->wc_callbacks->get_wc_prop)
            {
              /* If we have a WC, we might be able to dive all the way into the WC
              * to get the previous URL so we can do a differential GET with the
              * base URL.
              */
              const svn_string_t *value = NULL;
              SVN_ERR(ctx->sess->wc_callbacks->get_wc_prop(
                                                ctx->sess->wc_callback_baton,
                                                file->relpath,
                                                SVN_RA_SERF__WC_CHECKED_IN_URL,
                                                &value, scratch_pool));

              fetch_ctx->delta_base = value
                                        ? apr_pstrdup(file->pool, value->data)
                                        : NULL;
            }

          handler = svn_ra_serf__create_handler(ctx->sess, file->pool);

          handler->method = "GET";
          handler->path = file->url;

          handler->conn = conn; /* Explicit scheduling */

          handler->custom_accept_encoding = TRUE;
          handler->no_dav_headers = TRUE;
          handler->header_delegate = headers_fetch;
          handler->header_delegate_baton = fetch_ctx;

          handler->response_handler = handle_fetch;
          handler->response_baton = fetch_ctx;

          handler->response_error = cancel_fetch;
          handler->response_error_baton = fetch_ctx;

          handler->done_delegate = file_fetch_done;
          handler->done_delegate_baton = fetch_ctx;

          fetch_ctx->handler = handler;

          svn_ra_serf__request_create(handler);

          ctx->num_active_fetches++;
        }
    }

  /* If needed, create the PROPFIND to retrieve the file's properties. */
  if (file->fetch_props)
    {
      SVN_ERR(svn_ra_serf__create_propfind_handler(&file->propfind_handler,
                                                   ctx->sess, file->url,
                                                   ctx->target_rev, "0",
                                                   all_props,
                                                   set_file_props, file,
                                                   file->pool));
      file->propfind_handler->conn = conn; /* Explicit scheduling */

      file->propfind_handler->done_delegate = file_props_done;
      file->propfind_handler->done_delegate_baton = file;

      /* Create a serf request for the PROPFIND.  */
      svn_ra_serf__request_create(file->propfind_handler);

      ctx->num_active_propfinds++;
    }

  if (file->fetch_props || file->fetch_file)
      return SVN_NO_ERROR;


  /* Somehow we are done; probably via the local cache.
     Close the file and release memory, etc. */

  return svn_error_trace(close_file(file, scratch_pool));
}

/* Implements svn_ra_serf__prop_func */
static svn_error_t *
set_dir_prop(void *baton,
             const char *path,
             const char *ns,
             const char *name,
             const svn_string_t *val,
             apr_pool_t *scratch_pool)
{
  dir_baton_t *dir = baton;
  report_context_t *ctx = dir->ctx;
  const char *prop_name;

  prop_name = svn_ra_serf__svnname_from_wirename(ns, name, scratch_pool);
  if (prop_name == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(ensure_dir_opened(dir, scratch_pool));

  SVN_ERR(ctx->editor->change_dir_prop(dir->dir_baton,
                                       prop_name, val,
                                       scratch_pool));
  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__response_done_delegate_t */
static svn_error_t *
dir_props_done(serf_request_t *request,
               void *baton,
               apr_pool_t *scratch_pool)
{
  dir_baton_t *dir = baton;
  svn_ra_serf__handler_t *handler = dir->propfind_handler;

  if (handler->server_error)
    return svn_ra_serf__server_error_create(handler, scratch_pool);

  if (handler->sline.code != 207)
    return svn_error_trace(svn_ra_serf__unexpected_status(handler));

  dir->ctx->num_active_propfinds--;

  /* Closing the directory will automatically deliver the propfind props.
   *
   * Note that closing the directory may dispose the pool containing the
   * handler, which is only a valid operation in this callback, as after
   * this callback serf assumes the request is done. */

  return svn_error_trace(maybe_close_dir(dir));
}

/* Initiates additional requests needed for a directory when not in "send-all"
 * mode */
static svn_error_t *
fetch_for_dir(dir_baton_t *dir,
              apr_pool_t *scratch)
{
  report_context_t *ctx = dir->ctx;
  svn_ra_serf__connection_t *conn;

  /* Open extra connections if we have enough requests to send. */
  if (ctx->sess->num_conns < ctx->sess->max_connections)
    SVN_ERR(open_connection_if_needed(ctx->sess, ctx->num_active_fetches +
                                                 ctx->num_active_propfinds));

  /* What connection should we go on? */
  conn = get_best_connection(ctx);

  /* If needed, create the PROPFIND to retrieve the file's properties. */
  if (dir->fetch_props)
    {
      SVN_ERR(svn_ra_serf__create_propfind_handler(&dir->propfind_handler,
                                                   ctx->sess, dir->url,
                                                   ctx->target_rev, "0",
                                                   all_props,
                                                   set_dir_prop, dir,
                                                   dir->pool));

      dir->propfind_handler->conn = conn;
      dir->propfind_handler->done_delegate = dir_props_done;
      dir->propfind_handler->done_delegate_baton = dir;

      /* Create a serf request for the PROPFIND.  */
      svn_ra_serf__request_create(dir->propfind_handler);

      ctx->num_active_propfinds++;
    }
  else
    SVN_ERR_MALFUNCTION();

  return SVN_NO_ERROR;
}


/** XML callbacks for our update-report response parsing */

/* Conforms to svn_ra_serf__xml_opened_t  */
static svn_error_t *
update_opened(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int entered_state,
              const svn_ra_serf__dav_props_t *tag,
              apr_pool_t *scratch_pool)
{
  report_context_t *ctx = baton;
  apr_hash_t *attrs;

  switch (entered_state)
    {
      case UPDATE_REPORT:
        {
          const char *val;

          attrs = svn_ra_serf__xml_gather_since(xes, UPDATE_REPORT);
          val = svn_hash_gets(attrs, "inline-props");

          if (val && (strcmp(val, "true") == 0))
            ctx->add_props_included = TRUE;

          val = svn_hash_gets(attrs, "send-all");

          if (val && (strcmp(val, "true") == 0))
            {
              ctx->send_all_mode = TRUE;

              /* All properties are included in send-all mode. */
              ctx->add_props_included = TRUE;
            }
        }
        break;

      case OPEN_DIR:
      case ADD_DIR:
        {
          dir_baton_t *dir;
          const char *name;
          attrs = svn_ra_serf__xml_gather_since(xes, entered_state);

          name = svn_hash_gets(attrs, "name");
          if (!name)
            name = "";

          SVN_ERR(create_dir_baton(&dir, ctx, name, scratch_pool));

          if (entered_state == OPEN_DIR)
            {
              apr_int64_t base_rev;

              SVN_ERR(svn_cstring_atoi64(&base_rev,
                                         svn_hash_gets(attrs, "rev")));
              dir->base_rev = (svn_revnum_t)base_rev;
            }
          else
            {
              dir->copyfrom_path = svn_hash_gets(attrs, "copyfrom-path");

              if (dir->copyfrom_path)
                {
                  apr_int64_t copyfrom_rev;
                  const char *copyfrom_rev_str;
                  dir->copyfrom_path = svn_fspath__canonicalize(
                                                        dir->copyfrom_path,
                                                        dir->pool);

                  copyfrom_rev_str = svn_hash_gets(attrs, "copyfrom-rev");

                  if (!copyfrom_rev_str)
                    return svn_error_createf(SVN_ERR_XML_ATTRIB_NOT_FOUND,
                                             NULL,
                                            _("Missing '%s' attribute"),
                                            "copyfrom-rev");

                  SVN_ERR(svn_cstring_atoi64(&copyfrom_rev, copyfrom_rev_str));

                  dir->copyfrom_rev = (svn_revnum_t)copyfrom_rev;
                }

              if (! ctx->add_props_included)
                dir->fetch_props = TRUE;
            }
        }
        break;
      case OPEN_FILE:
      case ADD_FILE:
        {
          file_baton_t *file;

          attrs = svn_ra_serf__xml_gather_since(xes, entered_state);

          SVN_ERR(create_file_baton(&file, ctx, svn_hash_gets(attrs, "name"),
                                    scratch_pool));

          if (entered_state == OPEN_FILE)
            {
              apr_int64_t base_rev;

              SVN_ERR(svn_cstring_atoi64(&base_rev,
                                         svn_hash_gets(attrs, "rev")));
              file->base_rev = (svn_revnum_t)base_rev;
            }
          else
            {
              const char *sha1_checksum;
              file->copyfrom_path = svn_hash_gets(attrs, "copyfrom-path");

              if (file->copyfrom_path)
                {
                  apr_int64_t copyfrom_rev;
                  const char *copyfrom_rev_str;

                  file->copyfrom_path = svn_fspath__canonicalize(
                                                        file->copyfrom_path,
                                                        file->pool);

                  copyfrom_rev_str = svn_hash_gets(attrs, "copyfrom-rev");

                  if (!copyfrom_rev_str)
                    return svn_error_createf(SVN_ERR_XML_ATTRIB_NOT_FOUND,
                                             NULL,
                                            _("Missing '%s' attribute"),
                                            "copyfrom-rev");

                  SVN_ERR(svn_cstring_atoi64(&copyfrom_rev, copyfrom_rev_str));

                  file->copyfrom_rev = (svn_revnum_t)copyfrom_rev;
                }

              sha1_checksum = svn_hash_gets(attrs, "sha1-checksum");
              if (sha1_checksum)
                {
                  SVN_ERR(svn_checksum_parse_hex(&file->final_sha1_checksum,
                                                 svn_checksum_sha1,
                                                 sha1_checksum,
                                                 file->pool));
                }

              /* If the server isn't in "send-all" mode, we should expect to
                 fetch contents for added files. */
              if (! ctx->send_all_mode)
                file->fetch_file = TRUE;

              /* If the server isn't included properties for added items,
                 we'll need to fetch them ourselves. */
              if (! ctx->add_props_included)
                file->fetch_props = TRUE;
            }
        }
        break;

      case TXDELTA:
        {
          file_baton_t *file = ctx->cur_file;
          const char *base_checksum;

          /* Pre 1.2, mod_dav_svn was using <txdelta> tags (in
             addition to <fetch-file>s and such) when *not* in
             "send-all" mode.  As a client, we're smart enough to know
             that's wrong, so we'll just ignore these tags. */
          if (! ctx->send_all_mode)
            break;

          file->fetch_file = FALSE;

          attrs = svn_ra_serf__xml_gather_since(xes, entered_state);
          base_checksum = svn_hash_gets(attrs, "base-checksum");

          if (base_checksum)
            SVN_ERR(svn_checksum_parse_hex(&file->base_md5_checksum,
                                           svn_checksum_md5, base_checksum,
                                           file->pool));

          SVN_ERR(open_file_txdelta(ctx->cur_file, scratch_pool));

          if (ctx->cur_file->txdelta != svn_delta_noop_window_handler)
            {
              svn_stream_t *decoder;

              decoder = svn_txdelta_parse_svndiff(file->txdelta,
                                                  file->txdelta_baton,
                                                  TRUE /* error early close*/,
                                                  file->pool);

              file->txdelta_stream = svn_base64_decode(decoder, file->pool);
            }
        }
        break;

      case FETCH_PROPS:
        {
          /* Subversion <= 1.6 servers will return a fetch-props element on
             open-file and open-dir when non entry props were changed in
             !send-all mode. In turn we fetch the full set of properties
             and send all of those as *changes* to the editor. So these
             editors have to be aware that they receive-non property changes.
             (In case of incomplete directories they have to be aware anyway)

             In r1063337 this behavior was changed in mod_dav_svn to always
             send property changes inline in these cases. (See issue #3657)

             Note that before that change the property changes to the last_*
             entry props were already inlined via specific xml elements. */
          if (ctx->cur_file)
            ctx->cur_file->fetch_props = TRUE;
          else if (ctx->cur_dir)
            ctx->cur_dir->fetch_props = TRUE;
        }
        break;
    }

  return SVN_NO_ERROR;
}



/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
update_closed(svn_ra_serf__xml_estate_t *xes,
              void *baton,
              int leaving_state,
              const svn_string_t *cdata,
              apr_hash_t *attrs,
              apr_pool_t *scratch_pool)
{
  report_context_t *ctx = baton;

  switch (leaving_state)
    {
      case UPDATE_REPORT:
        ctx->done = TRUE;
        break;
      case TARGET_REVISION:
        {
          const char *revstr = svn_hash_gets(attrs, "rev");
          apr_int64_t rev;

          SVN_ERR(svn_cstring_atoi64(&rev, revstr));

          SVN_ERR(ctx->editor->set_target_revision(ctx->editor_baton,
                                                   (svn_revnum_t)rev,
                                                   scratch_pool));
        }
        break;

      case CHECKED_IN_HREF:
        if (ctx->cur_file)
          ctx->cur_file->url = apr_pstrdup(ctx->cur_file->pool, cdata->data);
        else
          ctx->cur_dir->url = apr_pstrdup(ctx->cur_dir->pool, cdata->data);
        break;

      case SET_PROP:
      case REMOVE_PROP:
        {
          const char *name = svn_hash_gets(attrs, "name");
          const char *encoding;
          const svn_string_t *value;

          if (leaving_state == REMOVE_PROP)
            value = NULL;
          else if ((encoding = svn_hash_gets(attrs, "encoding")))
            {
              if (strcmp(encoding, "base64") != 0)
                return svn_error_createf(SVN_ERR_XML_UNKNOWN_ENCODING, NULL,
                                         _("Got unrecognized encoding '%s'"),
                                         encoding);

              value = svn_base64_decode_string(cdata, scratch_pool);
            }
          else
            value = cdata;

          if (ctx->cur_file)
            {
              file_baton_t *file = ctx->cur_file;

              if (value
                  || ctx->add_props_included
                  || SVN_IS_VALID_REVNUM(file->base_rev))
                {
                  SVN_ERR(ensure_file_opened(file, scratch_pool));

                  SVN_ERR(ctx->editor->change_file_prop(file->file_baton,
                                                        name,
                                                        value,
                                                        scratch_pool));
                }
              else
                {
                  if (!file->remove_props)
                    file->remove_props = apr_hash_make(file->pool);

                  svn_hash_sets(file->remove_props,
                                apr_pstrdup(file->pool, name),
                                "");
                }
            }
          else
            {
              dir_baton_t *dir = ctx->cur_dir;

              if (value
                  || ctx->add_props_included
                  || SVN_IS_VALID_REVNUM(dir->base_rev))
                {
                  SVN_ERR(ensure_dir_opened(dir, scratch_pool));

                  SVN_ERR(ctx->editor->change_dir_prop(dir->dir_baton,
                                                       name,
                                                       value,
                                                       scratch_pool));
                }
              else
                {
                  if (!dir->remove_props)
                    dir->remove_props = apr_hash_make(dir->pool);

                  svn_hash_sets(dir->remove_props,
                                apr_pstrdup(dir->pool, name),
                                "");
                }
            }
        }
        break;

      case OPEN_DIR:
      case ADD_DIR:
        {
          dir_baton_t *dir = ctx->cur_dir;
          ctx->cur_dir = ctx->cur_dir->parent_dir;

          if (dir->fetch_props && ! dir->url)
            {
              return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                      _("The REPORT response did not "
                                        "include the requested checked-in "
                                        "value"));
            }

          if (!dir->fetch_props)
            {
              SVN_ERR(maybe_close_dir(dir));
              break; /* dir potentially no longer valid */
            }
          else
            {
              /* Otherwise, if the server is *not* in "send-all" mode, we
                 are at a point where we can queue up the PROPFIND request */
              SVN_ERR(fetch_for_dir(dir, scratch_pool));
            }
        }
        break;

      case OPEN_FILE:
      case ADD_FILE:
        {
          file_baton_t *file = ctx->cur_file;

          ctx->cur_file = NULL;
          /* go fetch info->name from DAV:checked-in */

          if ((file->fetch_file || file->fetch_props) && ! file->url)
            {
              return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                                      _("The REPORT response did not "
                                        "include the requested checked-in "
                                        "value"));
            }

          /* If the server is in "send-all" mode or didn't get further work,
             we can now close the file */
          if (! file->fetch_file && ! file->fetch_props)
            {
              SVN_ERR(close_file(file, scratch_pool));
              break; /* file is no longer valid */
            }
          else
            {
              /* Otherwise, if the server is *not* in "send-all" mode, we
                 should be at a point where we can queue up any auxiliary
                 content-fetching requests. */
              SVN_ERR(fetch_for_file(file, scratch_pool));
            }
        }
        break;

      case MD5_CHECKSUM:
        SVN_ERR(svn_checksum_parse_hex(&ctx->cur_file->final_md5_checksum,
                                       svn_checksum_md5,
                                       cdata->data,
                                       ctx->cur_file->pool));
        break;

      case FETCH_FILE:
        {
          file_baton_t *file = ctx->cur_file;
          const char *base_checksum = svn_hash_gets(attrs, "base-checksum");
          const char *sha1_checksum = svn_hash_gets(attrs, "sha1-checksum");

          if (base_checksum)
            SVN_ERR(svn_checksum_parse_hex(&file->base_md5_checksum,
                                           svn_checksum_md5, base_checksum,
                                           file->pool));

          /* Property is duplicated between add-file and fetch-file */
          if (sha1_checksum && !file->final_sha1_checksum)
            SVN_ERR(svn_checksum_parse_hex(&file->final_sha1_checksum,
                                           svn_checksum_sha1,
                                           sha1_checksum,
                                           file->pool));

          /* Some 0.3x mod_dav_svn wrote both txdelta and fetch-file
             elements in send-all mode. (See neon for history) */
          if (! ctx->send_all_mode)
            file->fetch_file = TRUE;
        }
        break;

      case DELETE_ENTRY:
        {
          const char *name = svn_hash_gets(attrs, "name");
          const char *revstr;
          apr_int64_t delete_rev;

          SVN_ERR(ensure_dir_opened(ctx->cur_dir, scratch_pool));

          revstr = svn_hash_gets(attrs, "rev");

          if (revstr)
            SVN_ERR(svn_cstring_atoi64(&delete_rev, revstr));
          else
            delete_rev = SVN_INVALID_REVNUM;

          SVN_ERR(ctx->editor->delete_entry(
                                    svn_relpath_join(ctx->cur_dir->relpath,
                                                     name,
                                                     scratch_pool),
                                    (svn_revnum_t)delete_rev,
                                    ctx->cur_dir->dir_baton,
                                    scratch_pool));
        }
        break;

      case ABSENT_DIR:
        {
          const char *name = svn_hash_gets(attrs, "name");

          SVN_ERR(ensure_dir_opened(ctx->cur_dir, scratch_pool));

          SVN_ERR(ctx->editor->absent_directory(
                                    svn_relpath_join(ctx->cur_dir->relpath,
                                                     name, scratch_pool),
                                    ctx->cur_dir->dir_baton,
                                    scratch_pool));
        }
        break;
     case ABSENT_FILE:
        {
          const char *name = svn_hash_gets(attrs, "name");

          SVN_ERR(ensure_dir_opened(ctx->cur_dir, scratch_pool));

          SVN_ERR(ctx->editor->absent_file(
                                    svn_relpath_join(ctx->cur_dir->relpath,
                                                     name, scratch_pool),
                                    ctx->cur_dir->dir_baton,
                                    scratch_pool));
        }
        break;

      case TXDELTA:
        {
          file_baton_t *file = ctx->cur_file;

          if (file->txdelta_stream)
            {
              SVN_ERR(svn_stream_close(file->txdelta_stream));
              file->txdelta_stream = NULL;
            }
        }
        break;

      case VERSION_NAME:
      case CREATIONDATE:
      case CREATOR_DISPLAYNAME:
        {
          /* Subversion <= 1.6 servers would return a fetch-props element on
             open-file and open-dir when non entry props were changed in
             !send-all mode. In turn we fetch the full set of properties and
             send those as *changes* to the editor. So these editors have to
             be aware that they receive non property changes.
             (In case of incomplete directories they have to be aware anyway)

             In that case the last_* entry props are posted as 3 specific xml
             elements, which we handle here.

             In r1063337 this behavior was changed in mod_dav_svn to always
             send property changes inline in these cases. (See issue #3657)
           */

          const char *propname;

          if (ctx->cur_file)
            SVN_ERR(ensure_file_opened(ctx->cur_file, scratch_pool));
          else if (ctx->cur_dir)
            SVN_ERR(ensure_dir_opened(ctx->cur_dir, scratch_pool));
          else
            break;

          switch (leaving_state)
            {
              case VERSION_NAME:
                propname = SVN_PROP_ENTRY_COMMITTED_REV;
                break;
              case CREATIONDATE:
                propname = SVN_PROP_ENTRY_COMMITTED_DATE;
                break;
              case CREATOR_DISPLAYNAME:
                propname = SVN_PROP_ENTRY_LAST_AUTHOR;
                break;
              default:
                SVN_ERR_MALFUNCTION(); /* Impossible to reach */
            }

          if (ctx->cur_file)
            SVN_ERR(ctx->editor->change_file_prop(ctx->cur_file->file_baton,
                                                  propname, cdata,
                                                  scratch_pool));
          else
            SVN_ERR(ctx->editor->change_dir_prop(ctx->cur_dir->dir_baton,
                                                  propname, cdata,
                                                  scratch_pool));
        }
        break;
    }

  return SVN_NO_ERROR;
}


/* Conforms to svn_ra_serf__xml_cdata_t  */
static svn_error_t *
update_cdata(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int current_state,
             const char *data,
             apr_size_t len,
             apr_pool_t *scratch_pool)
{
  report_context_t *ctx = baton;

  if (current_state == TXDELTA && ctx->cur_file
      && ctx->cur_file->txdelta_stream)
    {
      SVN_ERR(svn_stream_write(ctx->cur_file->txdelta_stream, data, &len));
    }

  return SVN_NO_ERROR;
}


/** Editor callbacks given to callers to create request body */

/* Helper to create simple xml tag without attributes. */
static void
make_simple_xml_tag(svn_stringbuf_t **buf_p,
                    const char *tagname,
                    const char *cdata,
                    apr_pool_t *pool)
{
  svn_xml_make_open_tag(buf_p, pool, svn_xml_protect_pcdata, tagname,
                        SVN_VA_NULL);
  svn_xml_escape_cdata_cstring(buf_p, cdata, pool);
  svn_xml_make_close_tag(buf_p, pool, tagname);
}

static svn_error_t *
set_path(void *report_baton,
         const char *path,
         svn_revnum_t revision,
         svn_depth_t depth,
         svn_boolean_t start_empty,
         const char *lock_token,
         apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  svn_stringbuf_t *buf = NULL;

  svn_xml_make_open_tag(&buf, pool, svn_xml_protect_pcdata, "S:entry",
                        "rev", apr_ltoa(pool, revision),
                        "lock-token", lock_token,
                        "depth", svn_depth_to_word(depth),
                        "start-empty", start_empty ? "true" : NULL,
                        SVN_VA_NULL);
  svn_xml_escape_cdata_cstring(&buf, path, pool);
  svn_xml_make_close_tag(&buf, pool, "S:entry");

  SVN_ERR(svn_stream_write(report->body_template, buf->data, &buf->len));

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_path(void *report_baton,
            const char *path,
            apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  svn_stringbuf_t *buf = NULL;

  make_simple_xml_tag(&buf, "S:missing", path, pool);

  SVN_ERR(svn_stream_write(report->body_template, buf->data, &buf->len));

  return SVN_NO_ERROR;
}

static svn_error_t *
link_path(void *report_baton,
          const char *path,
          const char *url,
          svn_revnum_t revision,
          svn_depth_t depth,
          svn_boolean_t start_empty,
          const char *lock_token,
          apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  const char *link, *report_target;
  apr_uri_t uri;
  apr_status_t status;
  svn_stringbuf_t *buf = NULL;

  /* We need to pass in the baseline relative path.
   *
   * TODO Confirm that it's on the same server?
   */
  status = apr_uri_parse(pool, url, &uri);
  if (status)
    {
      return svn_error_createf(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                               _("Unable to parse URL '%s'"), url);
    }

  SVN_ERR(svn_ra_serf__report_resource(&report_target, report->sess, pool));
  SVN_ERR(svn_ra_serf__get_relative_path(&link, uri.path, report->sess, pool));

  link = apr_pstrcat(pool, "/", link, SVN_VA_NULL);

  svn_xml_make_open_tag(&buf, pool, svn_xml_protect_pcdata, "S:entry",
                        "rev", apr_ltoa(pool, revision),
                        "lock-token", lock_token,
                        "depth", svn_depth_to_word(depth),
                        "linkpath", link,
                        "start-empty", start_empty ? "true" : NULL,
                        SVN_VA_NULL);
  svn_xml_escape_cdata_cstring(&buf, path, pool);
  svn_xml_make_close_tag(&buf, pool, "S:entry");

  SVN_ERR(svn_stream_write(report->body_template, buf->data, &buf->len));

  /* Store the switch roots to allow generating repos_relpaths from just
     the working copy paths. (Needed for HTTPv2) */
  path = apr_pstrdup(report->pool, path);
  link = apr_pstrdup(report->pool, link + 1);
  svn_hash_sets(report->switched_paths, path, link);

  if (!path[0] && report->update_target[0])
    {
      /* The update root is switched. Make sure we store it the way
         we expect it to find */
      svn_hash_sets(report->switched_paths, report->update_target, link);
    }

  return APR_SUCCESS;
}

/* Serf callback to setup update request headers. */
static svn_error_t *
setup_update_report_headers(serf_bucket_t *headers,
                            void *baton,
                            apr_pool_t *pool /* request pool */,
                            apr_pool_t *scratch_pool)
{
  report_context_t *report = baton;

  svn_ra_serf__setup_svndiff_accept_encoding(headers, report->sess);

  return SVN_NO_ERROR;
}

/* Baton for update_delay_handler */
typedef struct update_delay_baton_t
{
  report_context_t *report;
  svn_spillbuf_t *spillbuf;
  svn_ra_serf__response_handler_t inner_handler;
  void *inner_handler_baton;
} update_delay_baton_t;

/* Helper for update_delay_handler() and process_pending() to
   call UDB->INNER_HANDLER with buffer pointed by DATA. */
static svn_error_t *
process_buffer(update_delay_baton_t *udb,
               serf_request_t *request,
               const void *data,
               apr_size_t len,
               svn_boolean_t at_eof,
               serf_bucket_alloc_t *alloc,
               apr_pool_t *pool)
{
  serf_bucket_t *tmp_bucket;
  svn_error_t *err;

  /* ### This code (and the eagain bucket code) can probably be
      ### simplified by using a bit of aggregate bucket magic.
      ### See mail from Ivan to dev@s.a.o. */
  if (at_eof)
  {
      tmp_bucket = serf_bucket_simple_create(data, len, NULL, NULL,
                                             alloc);
  }
  else
  {
      tmp_bucket = svn_ra_serf__create_bucket_with_eagain(data, len,
                                                          alloc);
  }

  /* If not at EOF create a bucket that finishes with EAGAIN, otherwise
      use a standard bucket with default EOF handling */
  err = udb->inner_handler(request, tmp_bucket,
                           udb->inner_handler_baton, pool);

  /* And free the bucket explicitly to avoid growing request allocator
     storage (in a loop) */
  serf_bucket_destroy(tmp_bucket);

  return svn_error_trace(err);
}


/* Delaying wrapping reponse handler, to avoid creating too many
   requests to deliver efficiently */
static svn_error_t *
update_delay_handler(serf_request_t *request,
                     serf_bucket_t *response,
                     void *handler_baton,
                     apr_pool_t *scratch_pool)
{
  update_delay_baton_t *udb = handler_baton;
  apr_status_t status;
  apr_pool_t *iterpool = NULL;

  if (! udb->spillbuf)
    {
      if (udb->report->send_all_mode)
        {
          /* Easy out... We only have one request, so avoid everything and just
             call the inner handler.

             We will always get in the loop (below) on the first chunk, as only
             the server can get us in true send-all mode */

          return svn_error_trace(udb->inner_handler(request, response,
                                                    udb->inner_handler_baton,
                                                    scratch_pool));
        }

      while ((udb->report->num_active_fetches + udb->report->num_active_propfinds)
                 < REQUEST_COUNT_TO_RESUME)
        {
          const char *data;
          apr_size_t len;
          svn_boolean_t at_eof = FALSE;
          svn_error_t *err;

          status = serf_bucket_read(response, PARSE_CHUNK_SIZE, &data, &len);
          if (SERF_BUCKET_READ_ERROR(status))
            return svn_ra_serf__wrap_err(status, NULL);
          else if (APR_STATUS_IS_EOF(status))
            udb->report->report_received = at_eof = TRUE;

          if (!iterpool)
            iterpool = svn_pool_create(scratch_pool);
          else
            svn_pool_clear(iterpool);

          if (len == 0 && !at_eof)
            return svn_ra_serf__wrap_err(status, NULL);

          err = process_buffer(udb, request, data, len, at_eof,
                               serf_request_get_alloc(request),
                               iterpool);

          if (err && SERF_BUCKET_READ_ERROR(err->apr_err))
            return svn_error_trace(err);
          else if (err && APR_STATUS_IS_EAGAIN(err->apr_err))
            {
              svn_error_clear(err); /* Throttling is working ok */
            }
          else if (err && (APR_STATUS_IS_EOF(err->apr_err)))
            {
              svn_pool_destroy(iterpool);
              return svn_error_trace(err); /* No buffering was necessary */
            }
          else
            {
              /* SERF_ERROR_WAIT_CONN should be impossible? */
              return svn_error_trace(err);
            }
        }

      /* Let's start using the spill infrastructure */
      udb->spillbuf = svn_spillbuf__create(SPILLBUF_BLOCKSIZE,
                                           SPILLBUF_MAXBUFFSIZE,
                                           udb->report->pool);
    }

  /* Read everything we can to a spillbuffer */
  do
    {
      const char *data;
      apr_size_t len;

      /* ### What blocksize should we pass? */
      status = serf_bucket_read(response, 8*PARSE_CHUNK_SIZE, &data, &len);

      if (!SERF_BUCKET_READ_ERROR(status))
        SVN_ERR(svn_spillbuf__write(udb->spillbuf, data, len, scratch_pool));
    }
  while (status == APR_SUCCESS);

  if (APR_STATUS_IS_EOF(status))
    udb->report->report_received = TRUE;

  /* We handle feeding the data from the main context loop, which will be right
     after processing the pending data */

  if (status)
    return svn_ra_serf__wrap_err(status, NULL);
  else
    return SVN_NO_ERROR;
}

/* Process pending data from the update report, if any */
static svn_error_t *
process_pending(update_delay_baton_t *udb,
                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = NULL;
  serf_bucket_alloc_t *alloc = NULL;

  while ((udb->report->num_active_fetches + udb->report->num_active_propfinds)
            < REQUEST_COUNT_TO_RESUME)
    {
      const char *data;
      apr_size_t len;
      svn_boolean_t at_eof;
      svn_error_t *err;

      if (!iterpool)
        {
          iterpool = svn_pool_create(scratch_pool);
          alloc = serf_bucket_allocator_create(scratch_pool, NULL, NULL);
        }
      else
        svn_pool_clear(iterpool);

      SVN_ERR(svn_spillbuf__read(&data, &len, udb->spillbuf, iterpool));

      if (data == NULL && !udb->report->report_received)
        break;
      else if (data == NULL)
        at_eof = TRUE;
      else
        at_eof = FALSE;

      err = process_buffer(udb, NULL /* allowed? */, data, len,
                           at_eof, alloc, iterpool);

      if (err && APR_STATUS_IS_EAGAIN(err->apr_err))
        {
          svn_error_clear(err); /* Throttling is working */
        }
      else if (err && APR_STATUS_IS_EOF(err->apr_err))
        {
          svn_error_clear(err);

          svn_pool_destroy(iterpool);
          udb->spillbuf = NULL;
          return SVN_NO_ERROR;
        }
      else if (err)
        return svn_error_trace(err);
    }

  if (iterpool)
    svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Process the 'update' editor report */
static svn_error_t *
process_editor_report(report_context_t *ctx,
                      svn_ra_serf__handler_t *handler,
                      apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *sess = ctx->sess;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_interval_time_t waittime_left = sess->timeout;
  update_delay_baton_t *ud;

  /* Now wrap the response handler with delay support to avoid sending
     out too many requests at once */
  ud = apr_pcalloc(scratch_pool, sizeof(*ud));
  ud->report = ctx;

  ud->inner_handler = handler->response_handler;
  ud->inner_handler_baton = handler->response_baton;

  handler->response_handler = update_delay_handler;
  handler->response_baton = ud;

  /* Open the first extra connection. */
  SVN_ERR(open_connection_if_needed(sess, 0));

  sess->cur_conn = 1;

  /* Note that we may have no active GET or PROPFIND requests, yet the
     processing has not been completed. This could be from a delay on the
     network or because we've spooled the entire response into our "pending"
     content of the XML parser. The DONE flag will get set when all the
     XML content has been received *and* parsed.  */
  while (!handler->done
         || ctx->num_active_fetches
         || ctx->num_active_propfinds
         || !ctx->done)
    {
      svn_error_t *err;
      int i;

      svn_pool_clear(iterpool);

      err = svn_ra_serf__context_run(sess, &waittime_left, iterpool);

      if (handler->done && handler->server_error)
        {
          svn_error_clear(err);
          err = svn_ra_serf__server_error_create(handler, iterpool);

          SVN_ERR_ASSERT(err != NULL);
        }

      SVN_ERR(err);

      /* If there is pending REPORT data, process it now. */
      if (ud->spillbuf)
        SVN_ERR(process_pending(ud, iterpool));

      /* Debugging purposes only! */
      for (i = 0; i < sess->num_conns; i++)
        {
          serf_debug__closed_conn(sess->conns[i]->bkt_alloc);
        }
    }

  svn_pool_clear(iterpool);

  /* If we got a complete report, close the edit.  Otherwise, abort it. */
  if (ctx->done)
    SVN_ERR(ctx->editor->close_edit(ctx->editor_baton, iterpool));
  else
    return svn_error_create(SVN_ERR_RA_DAV_MALFORMED_DATA, NULL,
                            _("Missing update-report close tag"));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
finish_report(void *report_baton,
              apr_pool_t *pool)
{
  report_context_t *report = report_baton;
  svn_ra_serf__session_t *sess = report->sess;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  const char *report_target;
  svn_stringbuf_t *buf = NULL;
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  svn_error_t *err;

  svn_xml_make_close_tag(&buf, scratch_pool, "S:update-report");
  SVN_ERR(svn_stream_write(report->body_template, buf->data, &buf->len));
  SVN_ERR(svn_stream_close(report->body_template));

  SVN_ERR(svn_ra_serf__report_resource(&report_target, sess,  scratch_pool));

  xmlctx = svn_ra_serf__xml_context_create(update_ttable,
                                           update_opened, update_closed,
                                           update_cdata,
                                           report,
                                           scratch_pool);
  handler = svn_ra_serf__create_expat_handler(sess, xmlctx, NULL,
                                              scratch_pool);

  svn_ra_serf__request_body_get_delegate(&handler->body_delegate,
                                         &handler->body_delegate_baton,
                                         report->body);
  handler->method = "REPORT";
  handler->path = report_target;
  handler->body_type = "text/xml";
  handler->custom_accept_encoding = TRUE;
  handler->header_delegate = setup_update_report_headers;
  handler->header_delegate_baton = report;

  svn_ra_serf__request_create(handler);

  err = process_editor_report(report, handler, scratch_pool);

  if (err)
    {
      err = svn_error_trace(err);
      err = svn_error_compose_create(
                err,
                svn_error_trace(
                    report->editor->abort_edit(report->editor_baton,
                                               scratch_pool)));
    }

  svn_pool_destroy(scratch_pool);

  return svn_error_trace(err);
}


static svn_error_t *
abort_report(void *report_baton,
             apr_pool_t *pool)
{
#if 0
  report_context_t *report = report_baton;
#endif

  /* Should we perform some cleanup here? */

  return SVN_NO_ERROR;
}

static const svn_ra_reporter3_t ra_serf_reporter = {
  set_path,
  delete_path,
  link_path,
  finish_report,
  abort_report
};


/** RA function implementations and body */

static svn_error_t *
make_update_reporter(svn_ra_session_t *ra_session,
                     const svn_ra_reporter3_t **reporter,
                     void **report_baton,
                     svn_revnum_t revision,
                     const char *src_path,
                     const char *dest_path,
                     const char *update_target,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t text_deltas,
                     svn_boolean_t send_copyfrom_args,
                     const svn_delta_editor_t *update_editor,
                     void *update_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  report_context_t *report;
  const svn_delta_editor_t *filter_editor;
  void *filter_baton;
  svn_boolean_t has_target = *update_target != '\0';
  svn_boolean_t server_supports_depth;
  svn_ra_serf__session_t *sess = ra_session->priv;
  svn_stringbuf_t *buf = NULL;
  svn_boolean_t use_bulk_updates;

  SVN_ERR(svn_ra_serf__has_capability(ra_session, &server_supports_depth,
                                      SVN_RA_CAPABILITY_DEPTH, scratch_pool));
  /* We can skip the depth filtering when the user requested
     depth_files or depth_infinity because the server will
     transmit the right stuff anyway. */
  if ((depth != svn_depth_files)
      && (depth != svn_depth_infinity)
      && ! server_supports_depth)
    {
      SVN_ERR(svn_delta_depth_filter_editor(&filter_editor,
                                            &filter_baton,
                                            update_editor,
                                            update_baton,
                                            depth, has_target,
                                            result_pool));
      update_editor = filter_editor;
      update_baton = filter_baton;
    }

  report = apr_pcalloc(result_pool, sizeof(*report));
  report->pool = result_pool;
  report->sess = sess;
  report->target_rev = revision;
  report->ignore_ancestry = ignore_ancestry;
  report->send_copyfrom_args = send_copyfrom_args;
  report->text_deltas = text_deltas;
  report->switched_paths = apr_hash_make(report->pool);

  report->source = src_path;
  report->destination = dest_path;
  report->update_target = update_target;

  report->editor = update_editor;
  report->editor_baton = update_baton;
  report->done = FALSE;

  *reporter = &ra_serf_reporter;
  *report_baton = report;

  report->body =
    svn_ra_serf__request_body_create(SVN_RA_SERF__REQUEST_BODY_IN_MEM_SIZE,
                                     report->pool);
  report->body_template = svn_ra_serf__request_body_get_stream(report->body);

  if (sess->bulk_updates == svn_tristate_true)
    {
      /* User would like to use bulk updates. */
      use_bulk_updates = TRUE;
    }
  else if (sess->bulk_updates == svn_tristate_false)
    {
      /* User doesn't want bulk updates. */
      use_bulk_updates = FALSE;
    }
  else
    {
      /* User doesn't have any preferences on bulk updates. Decide on server
         preferences and capabilities. */
      if (sess->server_allows_bulk)
        {
          if (apr_strnatcasecmp(sess->server_allows_bulk, "off") == 0)
            {
              /* Server doesn't want bulk updates */
              use_bulk_updates = FALSE;
            }
          else if (apr_strnatcasecmp(sess->server_allows_bulk, "prefer") == 0)
            {
              /* Server prefers bulk updates, and we respect that */
              use_bulk_updates = TRUE;
            }
          else
            {
              /* Server allows bulk updates, but doesn't dictate its use. Do
                 whatever is the default. */
              use_bulk_updates = FALSE;
            }
        }
      else
        {
          /* Pre-1.8 server didn't send the bulk_updates header. Check if server
             supports inlining properties in update editor report. */
          if (sess->supports_inline_props)
            {
              /* NOTE: both inlined properties and server->allows_bulk_update
                 (flag SVN_DAV_ALLOW_BULK_UPDATES) were added in 1.8.0, so
                 this code is never reached with a released version of
                 mod_dav_svn.

                 Basically by default a 1.8.0 client connecting to a 1.7.x or
                 older server will always use bulk updates. */

              /* Inline props supported: do not use bulk updates. */
              use_bulk_updates = FALSE;
            }
          else
            {
              /* Inline props are not supported: use bulk updates to avoid
               * PROPFINDs for every added node. */
              use_bulk_updates = TRUE;
            }
        }
    }

  if (use_bulk_updates)
    {
      svn_xml_make_open_tag(&buf, scratch_pool, svn_xml_normal,
                            "S:update-report",
                            "xmlns:S", SVN_XML_NAMESPACE, "send-all", "true",
                            SVN_VA_NULL);
    }
  else
    {
      svn_xml_make_open_tag(&buf, scratch_pool, svn_xml_normal,
                            "S:update-report",
                            "xmlns:S", SVN_XML_NAMESPACE,
                            SVN_VA_NULL);
      /* Subversion 1.8+ servers can be told to send properties for newly
         added items inline even when doing a skelta response. */
      make_simple_xml_tag(&buf, "S:include-props", "yes", scratch_pool);
    }

  make_simple_xml_tag(&buf, "S:src-path", report->source, scratch_pool);

  if (SVN_IS_VALID_REVNUM(report->target_rev))
    {
      make_simple_xml_tag(&buf, "S:target-revision",
                          apr_ltoa(scratch_pool, report->target_rev),
                          scratch_pool);
    }

  if (report->destination && *report->destination)
    {
      make_simple_xml_tag(&buf, "S:dst-path", report->destination,
                          scratch_pool);
    }

  if (report->update_target && *report->update_target)
    {
      make_simple_xml_tag(&buf, "S:update-target", report->update_target,
                          scratch_pool);
    }

  if (report->ignore_ancestry)
    {
      make_simple_xml_tag(&buf, "S:ignore-ancestry", "yes", scratch_pool);
    }

  if (report->send_copyfrom_args)
    {
      make_simple_xml_tag(&buf, "S:send-copyfrom-args", "yes", scratch_pool);
    }

  /* Old servers know "recursive" but not "depth"; help them DTRT. */
  if (depth == svn_depth_files || depth == svn_depth_empty)
    {
      make_simple_xml_tag(&buf, "S:recursive", "no", scratch_pool);
    }

  /* When in 'send-all' mode, mod_dav_svn will assume that it should
     calculate and transmit real text-deltas (instead of empty windows
     that merely indicate "text is changed") unless it finds this
     element.

     NOTE: Do NOT count on servers actually obeying this, as some exist
     which obey send-all, but do not check for this directive at all!

     NOTE 2: When not in 'send-all' mode, mod_dav_svn can still be configured to
     override our request and send text-deltas. */
  if (! text_deltas)
    {
      make_simple_xml_tag(&buf, "S:text-deltas", "no", scratch_pool);
    }

  make_simple_xml_tag(&buf, "S:depth", svn_depth_to_word(depth), scratch_pool);

  SVN_ERR(svn_stream_write(report->body_template, buf->data, &buf->len));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__do_update(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       svn_revnum_t revision_to_update_to,
                       const char *update_target,
                       svn_depth_t depth,
                       svn_boolean_t send_copyfrom_args,
                       svn_boolean_t ignore_ancestry,
                       const svn_delta_editor_t *update_editor,
                       void *update_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  SVN_ERR(make_update_reporter(ra_session, reporter, report_baton,
                               revision_to_update_to,
                               session->session_url.path, NULL, update_target,
                               depth, ignore_ancestry, TRUE /* text_deltas */,
                               send_copyfrom_args,
                               update_editor, update_baton,
                               result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__do_diff(svn_ra_session_t *ra_session,
                     const svn_ra_reporter3_t **reporter,
                     void **report_baton,
                     svn_revnum_t revision,
                     const char *diff_target,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t text_deltas,
                     const char *versus_url,
                     const svn_delta_editor_t *diff_editor,
                     void *diff_baton,
                     apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(make_update_reporter(ra_session, reporter, report_baton,
                               revision,
                               session->session_url.path, versus_url, diff_target,
                               depth, ignore_ancestry, text_deltas,
                               FALSE /* send_copyfrom */,
                               diff_editor, diff_baton,
                               pool, scratch_pool));
  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__do_status(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       const char *status_target,
                       svn_revnum_t revision,
                       svn_depth_t depth,
                       const svn_delta_editor_t *status_editor,
                       void *status_baton,
                       apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  SVN_ERR(make_update_reporter(ra_session, reporter, report_baton,
                               revision,
                               session->session_url.path, NULL, status_target,
                               depth, FALSE, FALSE, FALSE,
                               status_editor, status_baton,
                               pool, scratch_pool));
  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__do_switch(svn_ra_session_t *ra_session,
                       const svn_ra_reporter3_t **reporter,
                       void **report_baton,
                       svn_revnum_t revision_to_switch_to,
                       const char *switch_target,
                       svn_depth_t depth,
                       const char *switch_url,
                       svn_boolean_t send_copyfrom_args,
                       svn_boolean_t ignore_ancestry,
                       const svn_delta_editor_t *switch_editor,
                       void *switch_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;

  return make_update_reporter(ra_session, reporter, report_baton,
                              revision_to_switch_to,
                              session->session_url.path,
                              switch_url, switch_target,
                              depth,
                              ignore_ancestry,
                              TRUE /* text_deltas */,
                              send_copyfrom_args,
                              switch_editor, switch_baton,
                              result_pool, scratch_pool);
}
