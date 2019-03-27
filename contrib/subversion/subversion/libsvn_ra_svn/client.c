/*
 * client.c :  Functions for repository access via the Subversion protocol
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



#include "svn_private_config.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_network_io.h>
#include <apr_uri.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_time.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_version.h"
#include "svn_ctype.h"

#include "svn_private_config.h"

#include "private/svn_fspath.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"

#include "../libsvn_ra/ra_loader.h"

#include "ra_svn.h"

#ifdef SVN_HAVE_SASL
#define DO_AUTH svn_ra_svn__do_cyrus_auth
#else
#define DO_AUTH svn_ra_svn__do_internal_auth
#endif

/* We aren't using SVN_DEPTH_IS_RECURSIVE here because that macro (for
   whatever reason) deems svn_depth_immediates as non-recursive, which
   is ... kinda true, but not true enough for our purposes.  We need
   our requested recursion level to be *at least* as recursive as the
   real depth we're looking for.
 */
#define DEPTH_TO_RECURSE(d)    \
        ((d) == svn_depth_unknown || (d) > svn_depth_files)

typedef struct ra_svn_commit_callback_baton_t {
  svn_ra_svn__session_baton_t *sess_baton;
  apr_pool_t *pool;
  svn_revnum_t *new_rev;
  svn_commit_callback2_t callback;
  void *callback_baton;
} ra_svn_commit_callback_baton_t;

typedef struct ra_svn_reporter_baton_t {
  svn_ra_svn__session_baton_t *sess_baton;
  svn_ra_svn_conn_t *conn;
  apr_pool_t *pool;
  const svn_delta_editor_t *editor;
  void *edit_baton;
} ra_svn_reporter_baton_t;

/* Parse an svn URL's tunnel portion into tunnel, if there is a tunnel
   portion. */
static void parse_tunnel(const char *url, const char **tunnel,
                         apr_pool_t *pool)
{
  *tunnel = NULL;

  if (strncasecmp(url, "svn", 3) != 0)
    return;
  url += 3;

  /* Get the tunnel specification, if any. */
  if (*url == '+')
    {
      const char *p;

      url++;
      p = strchr(url, ':');
      if (!p)
        return;
      *tunnel = apr_pstrmemdup(pool, url, p - url);
    }
}

static svn_error_t *make_connection(const char *hostname, unsigned short port,
                                    apr_socket_t **sock, apr_pool_t *pool)
{
  apr_sockaddr_t *sa;
  apr_status_t status;
  int family = APR_INET;

  /* Make sure we have IPV6 support first before giving apr_sockaddr_info_get
     APR_UNSPEC, because it may give us back an IPV6 address even if we can't
     create IPV6 sockets.  */

#if APR_HAVE_IPV6
#ifdef MAX_SECS_TO_LINGER
  status = apr_socket_create(sock, APR_INET6, SOCK_STREAM, pool);
#else
  status = apr_socket_create(sock, APR_INET6, SOCK_STREAM,
                             APR_PROTO_TCP, pool);
#endif
  if (status == 0)
    {
      apr_socket_close(*sock);
      family = APR_UNSPEC;
    }
#endif

  /* Resolve the hostname. */
  status = apr_sockaddr_info_get(&sa, hostname, family, port, 0, pool);
  if (status)
    return svn_error_createf(status, NULL, _("Unknown hostname '%s'"),
                             hostname);
  /* Iterate through the returned list of addresses attempting to
   * connect to each in turn. */
  do
    {
      /* Create the socket. */
#ifdef MAX_SECS_TO_LINGER
      /* ### old APR interface */
      status = apr_socket_create(sock, sa->family, SOCK_STREAM, pool);
#else
      status = apr_socket_create(sock, sa->family, SOCK_STREAM, APR_PROTO_TCP,
                                 pool);
#endif
      if (status == APR_SUCCESS)
        {
          status = apr_socket_connect(*sock, sa);
          if (status != APR_SUCCESS)
            apr_socket_close(*sock);
        }
      sa = sa->next;
    }
  while (status != APR_SUCCESS && sa);

  if (status)
    return svn_error_wrap_apr(status, _("Can't connect to host '%s'"),
                              hostname);

  /* Enable TCP keep-alives on the socket so we time out when
   * the connection breaks due to network-layer problems.
   * If the peer has dropped the connection due to a network partition
   * or a crash, or if the peer no longer considers the connection
   * valid because we are behind a NAT and our public IP has changed,
   * it will respond to the keep-alive probe with a RST instead of an
   * acknowledgment segment, which will cause svn to abort the session
   * even while it is currently blocked waiting for data from the peer.
   * See issue #3347. */
  status = apr_socket_opt_set(*sock, APR_SO_KEEPALIVE, 1);
  if (status)
    {
      /* It's not a fatal error if we cannot enable keep-alives. */
    }

  return SVN_NO_ERROR;
}

/* Set *DIFFS to an array of svn_prop_t, allocated in POOL, based on the
   property diffs in LIST, received from the server. */
static svn_error_t *parse_prop_diffs(const svn_ra_svn__list_t *list,
                                     apr_pool_t *pool,
                                     apr_array_header_t **diffs)
{
  int i;

  *diffs = apr_array_make(pool, list->nelts, sizeof(svn_prop_t));

  for (i = 0; i < list->nelts; i++)
    {
      svn_prop_t *prop;
      svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(list, i);

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Prop diffs element not a list"));
      prop = apr_array_push(*diffs);
      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "c(?s)",
                                      &prop->name, &prop->value));
    }
  return SVN_NO_ERROR;
}

/* Parse a lockdesc, provided in LIST as specified by the protocol into
   LOCK, allocated in POOL. */
static svn_error_t *parse_lock(const svn_ra_svn__list_t *list,
                               apr_pool_t *pool,
                               svn_lock_t **lock)
{
  const char *cdate, *edate;
  *lock = svn_lock_create(pool);
  SVN_ERR(svn_ra_svn__parse_tuple(list, "ccc(?c)c(?c)", &(*lock)->path,
                                  &(*lock)->token, &(*lock)->owner,
                                  &(*lock)->comment, &cdate, &edate));
  (*lock)->path = svn_fspath__canonicalize((*lock)->path, pool);
  SVN_ERR(svn_time_from_cstring(&(*lock)->creation_date, cdate, pool));
  if (edate)
    SVN_ERR(svn_time_from_cstring(&(*lock)->expiration_date, edate, pool));
  return SVN_NO_ERROR;
}

/* --- AUTHENTICATION ROUTINES --- */

svn_error_t *svn_ra_svn__auth_response(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       const char *mech, const char *mech_arg)
{
  return svn_error_trace(svn_ra_svn__write_tuple(conn, pool, "w(?c)", mech, mech_arg));
}

static svn_error_t *handle_auth_request(svn_ra_svn__session_baton_t *sess,
                                        apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess->conn;
  svn_ra_svn__list_t *mechlist;
  const char *realm;

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "lc", &mechlist, &realm));
  if (mechlist->nelts == 0)
    return SVN_NO_ERROR;
  return DO_AUTH(sess, mechlist, realm, pool);
}

/* --- REPORTER IMPLEMENTATION --- */

static svn_error_t *ra_svn_set_path(void *baton, const char *path,
                                    svn_revnum_t rev,
                                    svn_depth_t depth,
                                    svn_boolean_t start_empty,
                                    const char *lock_token,
                                    apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn__write_cmd_set_path(b->conn, pool, path, rev,
                                         start_empty, lock_token, depth));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_delete_path(void *baton, const char *path,
                                       apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn__write_cmd_delete_path(b->conn, pool, path));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_link_path(void *baton, const char *path,
                                     const char *url,
                                     svn_revnum_t rev,
                                     svn_depth_t depth,
                                     svn_boolean_t start_empty,
                                     const char *lock_token,
                                     apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn__write_cmd_link_path(b->conn, pool, path, url, rev,
                                          start_empty, lock_token, depth));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_finish_report(void *baton,
                                         apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn__write_cmd_finish_report(b->conn, b->pool));
  SVN_ERR(handle_auth_request(b->sess_baton, b->pool));
  SVN_ERR(svn_ra_svn_drive_editor2(b->conn, b->pool, b->editor, b->edit_baton,
                                   NULL, FALSE));
  SVN_ERR(svn_ra_svn__read_cmd_response(b->conn, b->pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_abort_report(void *baton,
                                        apr_pool_t *pool)
{
  ra_svn_reporter_baton_t *b = baton;

  SVN_ERR(svn_ra_svn__write_cmd_abort_report(b->conn, b->pool));
  return SVN_NO_ERROR;
}

static svn_ra_reporter3_t ra_svn_reporter = {
  ra_svn_set_path,
  ra_svn_delete_path,
  ra_svn_link_path,
  ra_svn_finish_report,
  ra_svn_abort_report
};

/* Set *REPORTER and *REPORT_BATON to a new reporter which will drive
 * EDITOR/EDIT_BATON when it gets the finish_report() call.
 *
 * Allocate the new reporter in POOL.
 */
static svn_error_t *
ra_svn_get_reporter(svn_ra_svn__session_baton_t *sess_baton,
                    apr_pool_t *pool,
                    const svn_delta_editor_t *editor,
                    void *edit_baton,
                    const char *target,
                    svn_depth_t depth,
                    const svn_ra_reporter3_t **reporter,
                    void **report_baton)
{
  ra_svn_reporter_baton_t *b;
  const svn_delta_editor_t *filter_editor;
  void *filter_baton;

  /* We can skip the depth filtering when the user requested
     depth_files or depth_infinity because the server will
     transmit the right stuff anyway. */
  if ((depth != svn_depth_files) && (depth != svn_depth_infinity)
      && ! svn_ra_svn_has_capability(sess_baton->conn, SVN_RA_SVN_CAP_DEPTH))
    {
      SVN_ERR(svn_delta_depth_filter_editor(&filter_editor,
                                            &filter_baton,
                                            editor, edit_baton, depth,
                                            *target != '\0',
                                            pool));
      editor = filter_editor;
      edit_baton = filter_baton;
    }

  b = apr_palloc(pool, sizeof(*b));
  b->sess_baton = sess_baton;
  b->conn = sess_baton->conn;
  b->pool = pool;
  b->editor = editor;
  b->edit_baton = edit_baton;

  *reporter = &ra_svn_reporter;
  *report_baton = b;

  return SVN_NO_ERROR;
}

/* --- RA LAYER IMPLEMENTATION --- */

/* (Note: *ARGV_P is an output parameter.) */
static svn_error_t *find_tunnel_agent(const char *tunnel,
                                      const char *hostinfo,
                                      const char ***argv_p,
                                      apr_hash_t *config, apr_pool_t *pool)
{
  svn_config_t *cfg;
  const char *val, *var, *cmd;
  char **cmd_argv;
  const char **argv;
  apr_size_t len;
  apr_status_t status;
  int n;

  /* Look up the tunnel specification in config. */
  cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG) : NULL;
  svn_config_get(cfg, &val, SVN_CONFIG_SECTION_TUNNELS, tunnel, NULL);

  /* We have one predefined tunnel scheme, if it isn't overridden by config. */
  if (!val && strcmp(tunnel, "ssh") == 0)
    {
      /* Killing the tunnel agent with SIGTERM leads to unsightly
       * stderr output from ssh, unless we pass -q.
       * The "-q" option to ssh is widely supported: all versions of
       * OpenSSH have it, the old ssh-1.x and the 2.x, 3.x ssh.com
       * versions have it too. If the user is using some other ssh
       * implementation that doesn't accept it, they can override it
       * in the [tunnels] section of the config. */
      val = "$SVN_SSH ssh -q --";
    }

  if (!val || !*val)
    return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                             _("Undefined tunnel scheme '%s'"), tunnel);

  /* If the scheme definition begins with "$varname", it means there
   * is an environment variable which can override the command. */
  if (*val == '$')
    {
      val++;
      len = strcspn(val, " ");
      var = apr_pstrmemdup(pool, val, len);
      cmd = getenv(var);
      if (!cmd)
        {
          cmd = val + len;
          while (*cmd == ' ')
            cmd++;
          if (!*cmd)
            return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                     _("Tunnel scheme %s requires environment "
                                       "variable %s to be defined"), tunnel,
                                     var);
        }
    }
  else
    cmd = val;

  /* Tokenize the command into a list of arguments. */
  status = apr_tokenize_to_argv(cmd, &cmd_argv, pool);
  if (status != APR_SUCCESS)
    return svn_error_wrap_apr(status, _("Can't tokenize command '%s'"), cmd);

  /* Calc number of the fixed arguments. */
  for (n = 0; cmd_argv[n] != NULL; n++)
    ;

  argv = apr_palloc(pool, (n + 4) * sizeof(char *));

  /* Append the fixed arguments to the result. */
  for (n = 0; cmd_argv[n] != NULL; n++)
    argv[n] = cmd_argv[n];

  argv[n++] = hostinfo;
  argv[n++] = "svnserve";
  argv[n++] = "-t";
  argv[n] = NULL;

  *argv_p = argv;
  return SVN_NO_ERROR;
}

/* This function handles any errors which occur in the child process
 * created for a tunnel agent.  We write the error out as a command
 * failure; the code in ra_svn_open() to read the server's greeting
 * will see the error and return it to the caller. */
static void handle_child_process_error(apr_pool_t *pool, apr_status_t status,
                                       const char *desc)
{
  svn_ra_svn_conn_t *conn;
  apr_file_t *in_file, *out_file;
  svn_stream_t *in_stream, *out_stream;
  svn_error_t *err;

  if (apr_file_open_stdin(&in_file, pool)
      || apr_file_open_stdout(&out_file, pool))
    return;

  in_stream = svn_stream_from_aprfile2(in_file, FALSE, pool);
  out_stream = svn_stream_from_aprfile2(out_file, FALSE, pool);

  conn = svn_ra_svn_create_conn5(NULL, in_stream, out_stream,
                                 SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, 0,
                                 0, 0, 0, pool);
  err = svn_error_wrap_apr(status, _("Error in child process: %s"), desc);
  svn_error_clear(svn_ra_svn__write_cmd_failure(conn, pool, err));
  svn_error_clear(err);
  svn_error_clear(svn_ra_svn__flush(conn, pool));
}

/* (Note: *CONN is an output parameter.) */
static svn_error_t *make_tunnel(const char **args, svn_ra_svn_conn_t **conn,
                                apr_pool_t *pool)
{
  apr_status_t status;
  apr_proc_t *proc;
  apr_procattr_t *attr;
  svn_error_t *err;

  status = apr_procattr_create(&attr, pool);
  if (status == APR_SUCCESS)
    status = apr_procattr_io_set(attr, 1, 1, 0);
  if (status == APR_SUCCESS)
    status = apr_procattr_cmdtype_set(attr, APR_PROGRAM_PATH);
  if (status == APR_SUCCESS)
    status = apr_procattr_child_errfn_set(attr, handle_child_process_error);
  proc = apr_palloc(pool, sizeof(*proc));
  if (status == APR_SUCCESS)
    status = apr_proc_create(proc, *args, args, NULL, attr, pool);
  if (status != APR_SUCCESS)
    return svn_error_create(SVN_ERR_RA_CANNOT_CREATE_TUNNEL,
                            svn_error_wrap_apr(status,
                                               _("Can't create tunnel")), NULL);

  /* Arrange for the tunnel agent to get a SIGTERM on pool
   * cleanup.  This is a little extreme, but the alternatives
   * weren't working out.
   *
   * Closing the pipes and waiting for the process to die
   * was prone to mysterious hangs which are difficult to
   * diagnose (e.g. svnserve dumps core due to unrelated bug;
   * sshd goes into zombie state; ssh connection is never
   * closed; ssh never terminates).
   * See also the long dicussion in issue #2580 if you really
   * want to know various reasons for these problems and
   * the different opinions on this issue.
   *
   * On Win32, APR does not support KILL_ONLY_ONCE. It only has
   * KILL_ALWAYS and KILL_NEVER. Other modes are converted to
   * KILL_ALWAYS, which immediately calls TerminateProcess().
   * This instantly kills the tunnel, leaving sshd and svnserve
   * on a remote machine running indefinitely. These processes
   * accumulate. The problem is most often seen with a fast client
   * machine and a modest internet connection, as the tunnel
   * is killed before being able to gracefully complete the
   * session. In that case, svn is unusable 100% of the time on
   * the windows machine. Thus, on Win32, we use KILL_NEVER and
   * take the lesser of two evils.
   */
#ifdef WIN32
  apr_pool_note_subprocess(pool, proc, APR_KILL_NEVER);
#else
  apr_pool_note_subprocess(pool, proc, APR_KILL_ONLY_ONCE);
#endif

  /* APR pipe objects inherit by default.  But we don't want the
   * tunnel agent's pipes held open by future child processes
   * (such as other ra_svn sessions), so turn that off. */
  apr_file_inherit_unset(proc->in);
  apr_file_inherit_unset(proc->out);

  /* Guard against dotfile output to stdout on the server. */
  *conn = svn_ra_svn_create_conn5(NULL,
                                  svn_stream_from_aprfile2(proc->out, FALSE,
                                                           pool),
                                  svn_stream_from_aprfile2(proc->in, FALSE,
                                                           pool),
                                  SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                                  0, 0, 0, 0, pool);
  err = svn_ra_svn__skip_leading_garbage(*conn, pool);
  if (err)
    return svn_error_quick_wrap(
             err,
             _("To better debug SSH connection problems, remove the -q "
               "option from 'ssh' in the [tunnels] section of your "
               "Subversion configuration file."));

  return SVN_NO_ERROR;
}

/* Parse URL inot URI, validating it and setting the default port if none
   was given.  Allocate the URI fileds out of POOL. */
static svn_error_t *parse_url(const char *url, apr_uri_t *uri,
                              apr_pool_t *pool)
{
  apr_status_t apr_err;

  apr_err = apr_uri_parse(pool, url, uri);

  if (apr_err != 0)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("Illegal svn repository URL '%s'"), url);

  return SVN_NO_ERROR;
}

/* This structure is used as a baton for the pool cleanup function to
   store tunnel parameters used by the close-tunnel callback. */
struct tunnel_data_t {
  void *tunnel_context;
  void *tunnel_baton;
  svn_ra_close_tunnel_func_t close_tunnel;
  svn_stream_t *request;
  svn_stream_t *response;
};

/* Pool cleanup function that invokes the close-tunnel callback. */
static apr_status_t close_tunnel_cleanup(void *baton)
{
  const struct tunnel_data_t *const td = baton;

  if (td->close_tunnel)
    td->close_tunnel(td->tunnel_context, td->tunnel_baton);

  svn_error_clear(svn_stream_close(td->request));

  /* We might have one stream to use for both request and response! */
  if (td->request != td->response)
    svn_error_clear(svn_stream_close(td->response));

  return APR_SUCCESS; /* ignored */
}

/* Open a session to URL, returning it in *SESS_P, allocating it in POOL.
   URI is a parsed version of URL.  CALLBACKS and CALLBACKS_BATON
   are provided by the caller of ra_svn_open. If TUNNEL_NAME is not NULL,
   it is the name of the tunnel type parsed from the URL scheme.
   If TUNNEL_ARGV is not NULL, it points to a program argument list to use
   when invoking the tunnel agent.
*/
static svn_error_t *open_session(svn_ra_svn__session_baton_t **sess_p,
                                 const char *url,
                                 const apr_uri_t *uri,
                                 const char *tunnel_name,
                                 const char **tunnel_argv,
                                 apr_hash_t *config,
                                 const svn_ra_callbacks2_t *callbacks,
                                 void *callbacks_baton,
                                 svn_auth_baton_t *auth_baton,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess;
  svn_ra_svn_conn_t *conn;
  apr_socket_t *sock;
  apr_uint64_t minver, maxver;
  svn_ra_svn__list_t *mechlist, *server_caplist, *repos_caplist;
  const char *client_string = NULL;
  apr_pool_t *pool = result_pool;
  svn_ra_svn__parent_t *parent;

  parent = apr_pcalloc(pool, sizeof(*parent));
  parent->client_url = svn_stringbuf_create(url, pool);
  parent->server_url = svn_stringbuf_create(url, pool);
  parent->path = svn_stringbuf_create_empty(pool);

  sess = apr_palloc(pool, sizeof(*sess));
  sess->pool = pool;
  sess->is_tunneled = (tunnel_name != NULL);
  sess->parent = parent;
  sess->user = uri->user;
  sess->hostname = uri->hostname;
  sess->tunnel_name = tunnel_name;
  sess->tunnel_argv = tunnel_argv;
  sess->callbacks = callbacks;
  sess->callbacks_baton = callbacks_baton;
  sess->bytes_read = sess->bytes_written = 0;
  sess->auth_baton = auth_baton;

  if (config)
    SVN_ERR(svn_config_copy_config(&sess->config, config, pool));
  else
    sess->config = NULL;

  if (tunnel_name)
    {
      sess->realm_prefix = apr_psprintf(pool, "<svn+%s://%s:%d>",
                                        tunnel_name,
                                        uri->hostname, uri->port);

      if (tunnel_argv)
        SVN_ERR(make_tunnel(tunnel_argv, &conn, pool));
      else
        {
          struct tunnel_data_t *const td = apr_palloc(pool, sizeof(*td));

          td->tunnel_baton = callbacks->tunnel_baton;
          td->close_tunnel = NULL;

          SVN_ERR(callbacks->open_tunnel_func(
                      &td->request, &td->response,
                      &td->close_tunnel, &td->tunnel_context,
                      callbacks->tunnel_baton, tunnel_name,
                      uri->user, uri->hostname, uri->port,
                      callbacks->cancel_func, callbacks_baton,
                      pool));

          apr_pool_cleanup_register(pool, td, close_tunnel_cleanup,
                                    apr_pool_cleanup_null);

          conn = svn_ra_svn_create_conn5(NULL, td->response, td->request,
                                         SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                                         0, 0, 0, 0, pool);
          SVN_ERR(svn_ra_svn__skip_leading_garbage(conn, pool));
        }
    }
  else
    {
      sess->realm_prefix = apr_psprintf(pool, "<svn://%s:%d>", uri->hostname,
                                        uri->port ? uri->port : SVN_RA_SVN_PORT);

      SVN_ERR(make_connection(uri->hostname,
                              uri->port ? uri->port : SVN_RA_SVN_PORT,
                              &sock, pool));
      conn = svn_ra_svn_create_conn5(sock, NULL, NULL,
                                     SVN_DELTA_COMPRESSION_LEVEL_DEFAULT,
                                     0, 0, 0, 0, pool);
    }

  /* Build the useragent string, querying the client for any
     customizations it wishes to note.  For historical reasons, we
     still deliver the hard-coded client version info
     (SVN_RA_SVN__DEFAULT_USERAGENT) and the customized client string
     separately in the protocol/capabilities handshake below.  But the
     commit logic wants the combined form for use with the
     SVN_PROP_TXN_USER_AGENT ephemeral property because that's
     consistent with our DAV approach.  */
  if (sess->callbacks->get_client_string != NULL)
    SVN_ERR(sess->callbacks->get_client_string(sess->callbacks_baton,
                                               &client_string, pool));
  if (client_string)
    sess->useragent = apr_pstrcat(pool, SVN_RA_SVN__DEFAULT_USERAGENT " ",
                                  client_string, SVN_VA_NULL);
  else
    sess->useragent = SVN_RA_SVN__DEFAULT_USERAGENT;

  /* Make sure we set conn->session before reading from it,
   * because the reader and writer functions expect a non-NULL value. */
  sess->conn = conn;
  conn->session = sess;

  /* Read server's greeting. */
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "nnll", &minver, &maxver,
                                        &mechlist, &server_caplist));

  /* We support protocol version 2. */
  if (minver > 2)
    return svn_error_createf(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                             _("Server requires minimum version %d"),
                             (int) minver);
  if (maxver < 2)
    return svn_error_createf(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                             _("Server only supports versions up to %d"),
                             (int) maxver);
  SVN_ERR(svn_ra_svn__set_capabilities(conn, server_caplist));

  /* All released versions of Subversion support edit-pipeline,
   * so we do not support servers that do not. */
  if (! svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_EDIT_PIPELINE))
    return svn_error_create(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                            _("Server does not support edit pipelining"));

  /* In protocol version 2, we send back our protocol version, our
   * capability list, and the URL, and subsequently there is an auth
   * request. */
  /* Client-side capabilities list: */
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "n(wwwwwww)cc(?c)",
                                  (apr_uint64_t) 2,
                                  SVN_RA_SVN_CAP_EDIT_PIPELINE,
                                  SVN_RA_SVN_CAP_SVNDIFF1,
                                  SVN_RA_SVN_CAP_SVNDIFF2_ACCEPTED,
                                  SVN_RA_SVN_CAP_ABSENT_ENTRIES,
                                  SVN_RA_SVN_CAP_DEPTH,
                                  SVN_RA_SVN_CAP_MERGEINFO,
                                  SVN_RA_SVN_CAP_LOG_REVPROPS,
                                  url,
                                  SVN_RA_SVN__DEFAULT_USERAGENT,
                                  client_string));
  SVN_ERR(handle_auth_request(sess, pool));

  /* This is where the security layer would go into effect if we
   * supported security layers, which is a ways off. */

  /* Read the repository's uuid and root URL, and perhaps learn more
     capabilities that weren't available before now. */
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "c?c?l", &conn->uuid,
                                        &conn->repos_root, &repos_caplist));
  if (repos_caplist)
    SVN_ERR(svn_ra_svn__set_capabilities(conn, repos_caplist));

  if (conn->repos_root)
    {
      conn->repos_root = svn_uri_canonicalize(conn->repos_root, pool);
      /* We should check that the returned string is a prefix of url, since
         that's the API guarantee, but this isn't true for 1.0 servers.
         Checking the length prevents client crashes. */
      if (strlen(conn->repos_root) > strlen(url))
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Impossibly long repository root from "
                                  "server"));
    }

  *sess_p = sess;

  return SVN_NO_ERROR;
}


#ifdef SVN_HAVE_SASL
#define RA_SVN_DESCRIPTION \
  N_("Module for accessing a repository using the svn network protocol.\n" \
     "  - with Cyrus SASL authentication")
#else
#define RA_SVN_DESCRIPTION \
  N_("Module for accessing a repository using the svn network protocol.")
#endif

static const char *ra_svn_get_description(apr_pool_t *pool)
{
  return _(RA_SVN_DESCRIPTION);
}

static const char * const *
ra_svn_get_schemes(apr_pool_t *pool)
{
  static const char *schemes[] = { "svn", NULL };

  return schemes;
}


/* A simple whitelist to ensure the following are valid:
 *   user@server
 *   [::1]:22
 *   server-name
 *   server_name
 *   127.0.0.1
 * with an extra restriction that a leading '-' is invalid.
 */
static svn_boolean_t
is_valid_hostinfo(const char *hostinfo)
{
  const char *p = hostinfo;

  if (p[0] == '-')
    return FALSE;

  while (*p)
    {
      if (!svn_ctype_isalnum(*p) && !strchr(":.-_[]@", *p))
        return FALSE;

      ++p;
    }

  return TRUE;
}

static svn_error_t *ra_svn_open(svn_ra_session_t *session,
                                const char **corrected_url,
                                const char *url,
                                const svn_ra_callbacks2_t *callbacks,
                                void *callback_baton,
                                svn_auth_baton_t *auth_baton,
                                apr_hash_t *config,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  apr_pool_t *sess_pool = svn_pool_create(result_pool);
  svn_ra_svn__session_baton_t *sess;
  const char *tunnel, **tunnel_argv;
  apr_uri_t uri;
  svn_config_t *cfg, *cfg_client;

  /* We don't support server-prescribed redirections in ra-svn. */
  if (corrected_url)
    *corrected_url = NULL;

  SVN_ERR(parse_url(url, &uri, sess_pool));

  parse_tunnel(url, &tunnel, result_pool);

  /* Use the default tunnel implementation if we got a tunnel name,
     but either do not have tunnel handler callbacks installed, or
     the handlers don't like the tunnel name. */
  if (tunnel
      && (!callbacks->open_tunnel_func
          || (callbacks->check_tunnel_func && callbacks->open_tunnel_func
              && !callbacks->check_tunnel_func(callbacks->tunnel_baton,
                                               tunnel))))
    {
      const char *decoded_hostinfo;

      decoded_hostinfo = svn_path_uri_decode(uri.hostinfo, result_pool);

      if (!is_valid_hostinfo(decoded_hostinfo))
        return svn_error_createf(SVN_ERR_BAD_URL, NULL, _("Invalid host '%s'"),
                                 uri.hostinfo);

      SVN_ERR(find_tunnel_agent(tunnel, decoded_hostinfo, &tunnel_argv,
                                config, result_pool));
    }
  else
    tunnel_argv = NULL;

  cfg_client = config
               ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG)
               : NULL;
  cfg = config ? svn_hash_gets(config, SVN_CONFIG_CATEGORY_SERVERS) : NULL;
  svn_auth_set_parameter(auth_baton,
                         SVN_AUTH_PARAM_CONFIG_CATEGORY_CONFIG, cfg_client);
  svn_auth_set_parameter(auth_baton,
                         SVN_AUTH_PARAM_CONFIG_CATEGORY_SERVERS, cfg);

  /* We open the session in a subpool so we can get rid of it if we
     reparent with a server that doesn't support reparenting. */
  SVN_ERR(open_session(&sess, url, &uri, tunnel, tunnel_argv, config,
                       callbacks, callback_baton,
                       auth_baton, sess_pool, scratch_pool));
  session->priv = sess;

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_dup_session(svn_ra_session_t *new_session,
                                       svn_ra_session_t *old_session,
                                       const char *new_session_url,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *old_sess = old_session->priv;

  SVN_ERR(ra_svn_open(new_session, NULL, new_session_url,
                      old_sess->callbacks, old_sess->callbacks_baton,
                      old_sess->auth_baton, old_sess->config,
                      result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Send the "reparent to URL" command to the server for RA_SESSION and
   update the session state.  Use SCRATCH_POOL for tempoaries.
 */
static svn_error_t *
reparent_server(svn_ra_session_t *ra_session,
                const char *url,
                apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess = ra_session->priv;
  svn_ra_svn__parent_t *parent = sess->parent;
  svn_ra_svn_conn_t *conn = sess->conn;
  svn_error_t *err;
  apr_pool_t *sess_pool;
  svn_ra_svn__session_baton_t *new_sess;
  apr_uri_t uri;

  /* Send the request to the server. */
  SVN_ERR(svn_ra_svn__write_cmd_reparent(conn, scratch_pool, url));
  err = handle_auth_request(sess, scratch_pool);
  if (! err)
    {
      SVN_ERR(svn_ra_svn__read_cmd_response(conn, scratch_pool, ""));
      svn_stringbuf_set(parent->server_url, url);
      return SVN_NO_ERROR;
    }
  else if (err->apr_err != SVN_ERR_RA_SVN_UNKNOWN_CMD)
    return err;

  /* Servers before 1.4 doesn't support this command; try to reconnect
     instead. */
  svn_error_clear(err);
  /* Create a new subpool of the RA session pool. */
  sess_pool = svn_pool_create(ra_session->pool);
  err = parse_url(url, &uri, sess_pool);
  if (! err)
    err = open_session(&new_sess, url, &uri, sess->tunnel_name, sess->tunnel_argv,
                       sess->config, sess->callbacks, sess->callbacks_baton,
                       sess->auth_baton, sess_pool, sess_pool);
  /* We destroy the new session pool on error, since it is allocated in
     the main session pool. */
  if (err)
    {
      svn_pool_destroy(sess_pool);
      return err;
    }

  /* We have a new connection, assign it and destroy the old. */
  ra_session->priv = new_sess;
  svn_pool_destroy(sess->pool);

  return SVN_NO_ERROR;
}

/* Make sure that RA_SESSION's client and server-side parent infp are in
   sync.  Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
ensure_exact_server_parent(svn_ra_session_t *ra_session,
                           apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess = ra_session->priv;
  svn_ra_svn__parent_t *parent = sess->parent;

  /* During e.g. a checkout operation, many requests will be sent for the
     same URL that was used to create the session.  So, both sides are
     often already in sync. */
  if (svn_stringbuf_compare(parent->client_url, parent->server_url))
    return SVN_NO_ERROR;

  /* Actually reparent the server to the session URL. */
  SVN_ERR(reparent_server(ra_session, parent->client_url->data,
                          scratch_pool));
  svn_stringbuf_setempty(parent->path);

  return SVN_NO_ERROR;
}

/* Return a copy of PATH, adjusted to the RA_SESSION's server parent URL.
   Allocate the result in RESULT_POOL. */
static const char *
reparent_path(svn_ra_session_t *ra_session,
              const char *path,
              apr_pool_t *result_pool)
{
  svn_ra_svn__session_baton_t *sess = ra_session->priv;
  svn_ra_svn__parent_t *parent = sess->parent;

  return svn_relpath_join(parent->path->data, path, result_pool);
}

/* Return a copy of PATHS, containing the same const char * paths but
   adjusted to the RA_SESSION's server parent URL.  Returns NULL if
   PATHS is NULL.  Allocate the result in RESULT_POOL. */
static apr_array_header_t *
reparent_path_array(svn_ra_session_t *ra_session,
                    const apr_array_header_t *paths,
                    apr_pool_t *result_pool)
{
  int i;
  apr_array_header_t *result;

  if (!paths)
    return NULL;

  result = apr_array_copy(result_pool, paths);
  for (i = 0; i < result->nelts; ++i)
    {
      const char **path = &APR_ARRAY_IDX(result, i, const char *);
      *path = reparent_path(ra_session, *path, result_pool);
    }

  return result;
}

/* Return a copy of PATHS, containing the same paths for keys but adjusted
   to the RA_SESSION's server parent URL.  Keeps the values as-are and
   returns NULL if PATHS is NULL.  Allocate the result in RESULT_POOL. */
static apr_hash_t *
reparent_path_hash(svn_ra_session_t *ra_session,
                   apr_hash_t *paths,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_hash_t *result;
  apr_hash_index_t *hi;

  if (!paths)
    return NULL;

  result = svn_hash__make(result_pool);
  for (hi = apr_hash_first(scratch_pool, paths); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_hash_sets(result,
                    reparent_path(ra_session, path, result_pool),
                    apr_hash_this_val(hi));
    }

  return result;
}

static svn_error_t *ra_svn_reparent(svn_ra_session_t *ra_session,
                                    const char *url,
                                    apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = ra_session->priv;
  svn_ra_svn__parent_t *parent = sess->parent;
  svn_ra_svn_conn_t *conn = sess->conn;
  const char *path;

  /* Eliminate reparent requests if they are to a sub-path of the
     server's current parent path. */
  path = svn_uri_skip_ancestor(parent->server_url->data, url, pool);
  if (!path)
    {
      /* Send the request to the server.

         If within the same repository, reparent to the repo root
         because this will maximize the chance to turn future reparent
         requests into a client-side update of the rel path. */
      path = conn->repos_root
           ? svn_uri_skip_ancestor(conn->repos_root, url, pool)
           : NULL;

      if (path)
        SVN_ERR(reparent_server(ra_session, conn->repos_root, pool));
      else
        SVN_ERR(reparent_server(ra_session, url, pool));
    }

  /* Update the local PARENT information.
     PARENT.SERVER_BASE_URL is already up-to-date. */
  svn_stringbuf_set(parent->client_url, url);
  if (path)
    svn_stringbuf_set(parent->path, path);
  else
    svn_stringbuf_setempty(parent->path);

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_session_url(svn_ra_session_t *session,
                                           const char **url,
                                           apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn__parent_t *parent = sess->parent;

  *url = apr_pstrmemdup(pool, parent->client_url->data,
                        parent->client_url->len);

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_latest_rev(svn_ra_session_t *session,
                                          svn_revnum_t *rev, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn__write_cmd_get_latest_rev(conn, pool));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dated_rev(svn_ra_session_t *session,
                                         svn_revnum_t *rev, apr_time_t tm,
                                         apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn__write_cmd_get_dated_rev(conn, pool, tm));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "r", rev));
  return SVN_NO_ERROR;
}

/* Forward declaration. */
static svn_error_t *ra_svn_has_capability(svn_ra_session_t *session,
                                          svn_boolean_t *has,
                                          const char *capability,
                                          apr_pool_t *pool);

static svn_error_t *ra_svn_change_rev_prop(svn_ra_session_t *session, svn_revnum_t rev,
                                           const char *name,
                                           const svn_string_t *const *old_value_p,
                                           const svn_string_t *value,
                                           apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t dont_care;
  const svn_string_t *old_value;
  svn_boolean_t has_atomic_revprops;

  SVN_ERR(ra_svn_has_capability(session, &has_atomic_revprops,
                                SVN_RA_SVN_CAP_ATOMIC_REVPROPS,
                                pool));

  if (old_value_p)
    {
      /* How did you get past the same check in svn_ra_change_rev_prop2()? */
      SVN_ERR_ASSERT(has_atomic_revprops);

      dont_care = FALSE;
      old_value = *old_value_p;
    }
  else
    {
      dont_care = TRUE;
      old_value = NULL;
    }

  if (has_atomic_revprops)
    SVN_ERR(svn_ra_svn__write_cmd_change_rev_prop2(conn, pool, rev, name,
                                                   value, dont_care,
                                                   old_value));
  else
    SVN_ERR(svn_ra_svn__write_cmd_change_rev_prop(conn, pool, rev, name,
                                                  value));

  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_uuid(svn_ra_session_t *session, const char **uuid,
                                    apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  *uuid = conn->uuid;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_repos_root(svn_ra_session_t *session, const char **url,
                                          apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  if (!conn->repos_root)
    return svn_error_create(SVN_ERR_RA_SVN_BAD_VERSION, NULL,
                            _("Server did not send repository root"));
  *url = conn->repos_root;
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_proplist(svn_ra_session_t *session, svn_revnum_t rev,
                                        apr_hash_t **props, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn__list_t *proplist;

  SVN_ERR(svn_ra_svn__write_cmd_rev_proplist(conn, pool, rev));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "l", &proplist));
  SVN_ERR(svn_ra_svn__parse_proplist(proplist, pool, props));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_rev_prop(svn_ra_session_t *session, svn_revnum_t rev,
                                    const char *name,
                                    svn_string_t **value, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  SVN_ERR(svn_ra_svn__write_cmd_rev_prop(conn, pool, rev, name));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "(?s)", value));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_end_commit(void *baton)
{
  ra_svn_commit_callback_baton_t *ccb = baton;
  svn_commit_info_t *commit_info = svn_create_commit_info(ccb->pool);

  SVN_ERR(handle_auth_request(ccb->sess_baton, ccb->pool));
  SVN_ERR(svn_ra_svn__read_tuple(ccb->sess_baton->conn, ccb->pool,
                                 "r(?c)(?c)?(?c)",
                                 &(commit_info->revision),
                                 &(commit_info->date),
                                 &(commit_info->author),
                                 &(commit_info->post_commit_err)));

  commit_info->repos_root = apr_pstrdup(ccb->pool,
                                        ccb->sess_baton->conn->repos_root);

  if (ccb->callback)
    SVN_ERR(ccb->callback(commit_info, ccb->callback_baton, ccb->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_commit(svn_ra_session_t *session,
                                  const svn_delta_editor_t **editor,
                                  void **edit_baton,
                                  apr_hash_t *revprop_table,
                                  svn_commit_callback2_t callback,
                                  void *callback_baton,
                                  apr_hash_t *lock_tokens,
                                  svn_boolean_t keep_locks,
                                  apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  ra_svn_commit_callback_baton_t *ccb;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  const svn_string_t *log_msg = svn_hash_gets(revprop_table,
                                              SVN_PROP_REVISION_LOG);

  if (log_msg == NULL &&
      ! svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_COMMIT_REVPROPS))
    {
      return svn_error_createf(SVN_ERR_BAD_PROPERTY_VALUE, NULL,
                               _("ra_svn does not support not specifying "
                                 "a log message with pre-1.5 servers; "
                                 "consider passing an empty one, or upgrading "
                                 "the server"));
    }
  else if (log_msg == NULL)
    /* 1.5+ server.  Set LOG_MSG to something, since the 'logmsg' argument
       to the 'commit' protocol command is non-optional; on the server side,
       only REVPROP_TABLE will be used, and LOG_MSG will be ignored.  The
       "svn:log" member of REVPROP_TABLE table is NULL, therefore the commit
       will have a NULL log message (not just "", really NULL).

       svnserve 1.5.x+ has always ignored LOG_MSG when REVPROP_TABLE was
       present; this was elevated to a protocol promise in r1498550 (and
       later documented in this comment) in order to fix the segmentation
       fault bug described in the log message of r1498550.*/
    log_msg = svn_string_create("", pool);

  /* If we're sending revprops other than svn:log, make sure the server won't
     silently ignore them. */
  if (apr_hash_count(revprop_table) > 1 &&
      ! svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_COMMIT_REVPROPS))
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                            _("Server doesn't support setting arbitrary "
                              "revision properties during commit"));

  /* If the server supports ephemeral txnprops, add the one that
     reports the client's version level string. */
  if (svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_COMMIT_REVPROPS) &&
      svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_EPHEMERAL_TXNPROPS))
    {
      svn_hash_sets(revprop_table, SVN_PROP_TXN_CLIENT_COMPAT_VERSION,
                    svn_string_create(SVN_VER_NUMBER, pool));
      svn_hash_sets(revprop_table, SVN_PROP_TXN_USER_AGENT,
                    svn_string_create(sess_baton->useragent, pool));
    }

  /* Callbacks may assume that all data is relative the sessions's URL. */
  SVN_ERR(ensure_exact_server_parent(session, pool));

  /* Tell the server we're starting the commit.
     Send log message here for backwards compatibility with servers
     before 1.5. */
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c(!", "commit",
                                  log_msg->data));
  if (lock_tokens)
    {
      iterpool = svn_pool_create(pool);
      for (hi = apr_hash_first(pool, lock_tokens); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          const char *path, *token;

          svn_pool_clear(iterpool);
          apr_hash_this(hi, &key, NULL, &val);
          path = key;
          token = val;
          SVN_ERR(svn_ra_svn__write_tuple(conn, iterpool, "cc", path, token));
        }
      svn_pool_destroy(iterpool);
    }
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!)b(!", keep_locks));
  SVN_ERR(svn_ra_svn__write_proplist(conn, pool, revprop_table));
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!))"));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));

  /* Remember a few arguments for when the commit is over. */
  ccb = apr_palloc(pool, sizeof(*ccb));
  ccb->sess_baton = sess_baton;
  ccb->pool = pool;
  ccb->new_rev = NULL;
  ccb->callback = callback;
  ccb->callback_baton = callback_baton;

  /* Fetch an editor for the caller to drive.  The editor will call
   * ra_svn_end_commit() upon close_edit(), at which point we'll fill
   * in the new_rev, committed_date, and committed_author values. */
  svn_ra_svn_get_editor(editor, edit_baton, conn, pool,
                        ra_svn_end_commit, ccb);
  return SVN_NO_ERROR;
}

/* Parse IPROPLIST, an array of svn_ra_svn__item_t structures, as a list of
   const char * repos relative paths and properties for those paths, storing
   the result as an array of svn_prop_inherited_item_t *items. */
static svn_error_t *
parse_iproplist(apr_array_header_t **inherited_props,
                const svn_ra_svn__list_t *iproplist,
                svn_ra_session_t *session,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)

{
  int i;
  apr_pool_t *iterpool;

  if (iproplist == NULL)
    {
      /* If the server doesn't have the SVN_RA_CAPABILITY_INHERITED_PROPS
         capability we shouldn't be asking for inherited props, but if we
         did and the server sent back nothing then we'll want to handle
         that. */
      *inherited_props = NULL;
      return SVN_NO_ERROR;
    }

  *inherited_props = apr_array_make(
    result_pool, iproplist->nelts, sizeof(svn_prop_inherited_item_t *));

  iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < iproplist->nelts; i++)
    {
      svn_ra_svn__list_t *iprop_list;
      char *parent_rel_path;
      apr_hash_t *iprops;
      apr_hash_index_t *hi;
      svn_prop_inherited_item_t *new_iprop =
        apr_palloc(result_pool, sizeof(*new_iprop));
      svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(iproplist, i);
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(
          SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
          _("Inherited proplist element not a list"));

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "cl",
                                      &parent_rel_path, &iprop_list));
      SVN_ERR(svn_ra_svn__parse_proplist(iprop_list, iterpool, &iprops));
      new_iprop->path_or_url = apr_pstrdup(result_pool, parent_rel_path);
      new_iprop->prop_hash = svn_hash__make(result_pool);
      for (hi = apr_hash_first(iterpool, iprops);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *name = apr_hash_this_key(hi);
          svn_string_t *value = apr_hash_this_val(hi);
          svn_hash_sets(new_iprop->prop_hash,
                        apr_pstrdup(result_pool, name),
                        svn_string_dup(value, result_pool));
        }
      APR_ARRAY_PUSH(*inherited_props, svn_prop_inherited_item_t *) =
        new_iprop;
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_file(svn_ra_session_t *session, const char *path,
                                    svn_revnum_t rev, svn_stream_t *stream,
                                    svn_revnum_t *fetched_rev,
                                    apr_hash_t **props,
                                    apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn__list_t *proplist;
  const char *expected_digest;
  svn_checksum_t *expected_checksum = NULL;
  svn_checksum_ctx_t *checksum_ctx;
  apr_pool_t *iterpool;

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_cmd_get_file(conn, pool, path, rev,
                                         (props != NULL), (stream != NULL)));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "(?c)rl",
                                        &expected_digest,
                                        &rev, &proplist));

  if (fetched_rev)
    *fetched_rev = rev;
  if (props)
    SVN_ERR(svn_ra_svn__parse_proplist(proplist, pool, props));

  /* We're done if the contents weren't wanted. */
  if (!stream)
    return SVN_NO_ERROR;

  if (expected_digest)
    {
      SVN_ERR(svn_checksum_parse_hex(&expected_checksum, svn_checksum_md5,
                                     expected_digest, pool));
      checksum_ctx = svn_checksum_ctx_create(svn_checksum_md5, pool);
    }

  /* Read the file's contents. */
  iterpool = svn_pool_create(pool);
  while (1)
    {
      svn_ra_svn__item_t *item;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Non-string as part of file contents"));
      if (item->u.string.len == 0)
        break;

      if (expected_checksum)
        SVN_ERR(svn_checksum_update(checksum_ctx, item->u.string.data,
                                    item->u.string.len));

      SVN_ERR(svn_stream_write(stream, item->u.string.data,
                               &item->u.string.len));
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));

  if (expected_checksum)
    {
      svn_checksum_t *checksum;

      SVN_ERR(svn_checksum_final(&checksum, checksum_ctx, pool));
      if (!svn_checksum_match(checksum, expected_checksum))
        return svn_checksum_mismatch_err(expected_checksum, checksum, pool,
                                         _("Checksum mismatch for '%s'"),
                                         path);
    }

  return SVN_NO_ERROR;
}

/* Write the protocol words that correspond to DIRENT_FIELDS to CONN
 * and use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
send_dirent_fields(svn_ra_svn_conn_t *conn,
                   apr_uint32_t dirent_fields,
                   apr_pool_t *scratch_pool)
{
  if (dirent_fields & SVN_DIRENT_KIND)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_KIND));
  if (dirent_fields & SVN_DIRENT_SIZE)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_SIZE));
  if (dirent_fields & SVN_DIRENT_HAS_PROPS)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_HAS_PROPS));
  if (dirent_fields & SVN_DIRENT_CREATED_REV)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_CREATED_REV));
  if (dirent_fields & SVN_DIRENT_TIME)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_TIME));
  if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
    SVN_ERR(svn_ra_svn__write_word(conn, scratch_pool,
                                   SVN_RA_SVN_DIRENT_LAST_AUTHOR));

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_dir(svn_ra_session_t *session,
                                   apr_hash_t **dirents,
                                   svn_revnum_t *fetched_rev,
                                   apr_hash_t **props,
                                   const char *path,
                                   svn_revnum_t rev,
                                   apr_uint32_t dirent_fields,
                                   apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn__list_t *proplist, *dirlist;
  int i;

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c(?r)bb(!", "get-dir", path,
                                  rev, (props != NULL), (dirents != NULL)));
  SVN_ERR(send_dirent_fields(conn, dirent_fields, pool));

  /* Always send the, nominally optional, want-iprops as "false" to
     workaround a bug in svnserve 1.8.0-1.8.8 that causes the server
     to see "true" if it is omitted. */
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!)b)", FALSE));

  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "rll", &rev, &proplist,
                                        &dirlist));

  if (fetched_rev)
    *fetched_rev = rev;
  if (props)
    SVN_ERR(svn_ra_svn__parse_proplist(proplist, pool, props));

  /* We're done if dirents aren't wanted. */
  if (!dirents)
    return SVN_NO_ERROR;

  /* Interpret the directory list. */
  *dirents = svn_hash__make(pool);
  for (i = 0; i < dirlist->nelts; i++)
    {
      const char *name, *kind, *cdate, *cauthor;
      svn_boolean_t has_props;
      svn_dirent_t *dirent;
      apr_uint64_t size;
      svn_revnum_t crev;
      svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(dirlist, i);

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Dirlist element not a list"));
      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "cwnbr(?c)(?c)",
                                      &name, &kind, &size, &has_props,
                                      &crev, &cdate, &cauthor));

      /* Nothing to sanitize here.  Any multi-segment path is simply
         illegal in the hash returned by svn_ra_get_dir2. */
      if (strchr(name, '/'))
        return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                 _("Invalid directory entry name '%s'"),
                                 name);

      dirent = svn_dirent_create(pool);
      dirent->kind = svn_node_kind_from_word(kind);
      dirent->size = size;/* FIXME: svn_filesize_t */
      dirent->has_props = has_props;
      dirent->created_rev = crev;
      /* NOTE: the tuple's format string says CDATE may be NULL. But this
         function does not allow that. The server has always sent us some
         random date, however, so this just happens to work. But let's
         be wary of servers that are (improperly) fixed to send NULL.

         Note: they should NOT be "fixed" to send NULL, as that would break
         any older clients which received that NULL. But we may as well
         be defensive against a malicous server.  */
      if (cdate == NULL)
        dirent->time = 0;
      else
        SVN_ERR(svn_time_from_cstring(&dirent->time, cdate, pool));
      dirent->last_author = cauthor;
      svn_hash_sets(*dirents, name, dirent);
    }

  return SVN_NO_ERROR;
}

/* Converts a apr_uint64_t with values TRUE, FALSE or
   SVN_RA_SVN_UNSPECIFIED_NUMBER as provided by svn_ra_svn__parse_tuple
   to a svn_tristate_t */
static svn_tristate_t
optbool_to_tristate(apr_uint64_t v)
{
  if (v == TRUE)  /* not just non-zero but exactly equal to 'TRUE' */
    return svn_tristate_true;
  if (v == FALSE)
    return svn_tristate_false;

  return svn_tristate_unknown; /* Contains SVN_RA_SVN_UNSPECIFIED_NUMBER */
}

/* If REVISION is SVN_INVALID_REVNUM, no value is sent to the
   server, which defaults to youngest. */
static svn_error_t *ra_svn_get_mergeinfo(svn_ra_session_t *session,
                                         svn_mergeinfo_catalog_t *catalog,
                                         const apr_array_header_t *paths,
                                         svn_revnum_t revision,
                                         svn_mergeinfo_inheritance_t inherit,
                                         svn_boolean_t include_descendants,
                                         apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn__parent_t *parent = sess_baton->parent;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  int i;
  svn_ra_svn__list_t *mergeinfo_tuple;
  svn_ra_svn__item_t *elt;
  const char *path;

  paths = reparent_path_array(session, paths, pool);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w((!", "get-mergeinfo"));
  for (i = 0; i < paths->nelts; i++)
    {
      path = APR_ARRAY_IDX(paths, i, const char *);
      SVN_ERR(svn_ra_svn__write_cstring(conn, pool, path));
    }
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!)(?r)wb)", revision,
                                  svn_inheritance_to_word(inherit),
                                  include_descendants));

  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "l", &mergeinfo_tuple));

  *catalog = NULL;
  if (mergeinfo_tuple->nelts > 0)
    {
      *catalog = svn_hash__make(pool);
      for (i = 0; i < mergeinfo_tuple->nelts; i++)
        {
          svn_mergeinfo_t for_path;
          const char *to_parse;

          elt = &SVN_RA_SVN__LIST_ITEM(mergeinfo_tuple, i);
          if (elt->kind != SVN_RA_SVN_LIST)
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Mergeinfo element is not a list"));
          SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "cc",
                                          &path, &to_parse));
          SVN_ERR(svn_mergeinfo_parse(&for_path, to_parse, pool));

          /* Correct for naughty servers that send "relative" paths
             with leading slashes! */
          if (path[0] == '/')
            ++path;

          /* Correct for the (potential) difference between client and
             server-side session parent paths. */
          path = svn_relpath_skip_ancestor(parent->path->data, path);
          svn_hash_sets(*catalog, path, for_path);
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_update(svn_ra_session_t *session,
                                  const svn_ra_reporter3_t **reporter,
                                  void **report_baton, svn_revnum_t rev,
                                  const char *target, svn_depth_t depth,
                                  svn_boolean_t send_copyfrom_args,
                                  svn_boolean_t ignore_ancestry,
                                  const svn_delta_editor_t *update_editor,
                                  void *update_baton,
                                  apr_pool_t *pool,
                                  apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t recurse = DEPTH_TO_RECURSE(depth);

  /* Callbacks may assume that all data is relative the sessions's URL. */
  SVN_ERR(ensure_exact_server_parent(session, scratch_pool));

  /* Tell the server we want to start an update. */
  SVN_ERR(svn_ra_svn__write_cmd_update(conn, pool, rev, target, recurse,
                                       depth, send_copyfrom_args,
                                       ignore_ancestry));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  SVN_ERR(ra_svn_get_reporter(sess_baton, pool, update_editor, update_baton,
                              target, depth, reporter, report_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_switch(svn_ra_session_t *session,
              const svn_ra_reporter3_t **reporter,
              void **report_baton, svn_revnum_t rev,
              const char *target, svn_depth_t depth,
              const char *switch_url,
              svn_boolean_t send_copyfrom_args,
              svn_boolean_t ignore_ancestry,
              const svn_delta_editor_t *update_editor,
              void *update_baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *pool = result_pool;
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t recurse = DEPTH_TO_RECURSE(depth);

  /* Callbacks may assume that all data is relative the sessions's URL. */
  SVN_ERR(ensure_exact_server_parent(session, scratch_pool));

  /* Tell the server we want to start a switch. */
  SVN_ERR(svn_ra_svn__write_cmd_switch(conn, pool, rev, target, recurse,
                                       switch_url, depth,
                                       send_copyfrom_args, ignore_ancestry));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * update_editor upon finish_report(). */
  SVN_ERR(ra_svn_get_reporter(sess_baton, pool, update_editor, update_baton,
                              target, depth, reporter, report_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_status(svn_ra_session_t *session,
                                  const svn_ra_reporter3_t **reporter,
                                  void **report_baton,
                                  const char *target, svn_revnum_t rev,
                                  svn_depth_t depth,
                                  const svn_delta_editor_t *status_editor,
                                  void *status_baton, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t recurse = DEPTH_TO_RECURSE(depth);

  /* Callbacks may assume that all data is relative the sessions's URL. */
  SVN_ERR(ensure_exact_server_parent(session, pool));

  /* Tell the server we want to start a status operation. */
  SVN_ERR(svn_ra_svn__write_cmd_status(conn, pool, target, recurse, rev,
                                       depth));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * status_editor upon finish_report(). */
  SVN_ERR(ra_svn_get_reporter(sess_baton, pool, status_editor, status_baton,
                              target, depth, reporter, report_baton));
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_diff(svn_ra_session_t *session,
                                const svn_ra_reporter3_t **reporter,
                                void **report_baton,
                                svn_revnum_t rev, const char *target,
                                svn_depth_t depth,
                                svn_boolean_t ignore_ancestry,
                                svn_boolean_t text_deltas,
                                const char *versus_url,
                                const svn_delta_editor_t *diff_editor,
                                void *diff_baton, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t recurse = DEPTH_TO_RECURSE(depth);

  /* Callbacks may assume that all data is relative the sessions's URL. */
  SVN_ERR(ensure_exact_server_parent(session, pool));

  /* Tell the server we want to start a diff. */
  SVN_ERR(svn_ra_svn__write_cmd_diff(conn, pool, rev, target, recurse,
                                     ignore_ancestry, versus_url,
                                     text_deltas, depth));
  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Fetch a reporter for the caller to drive.  The reporter will drive
   * diff_editor upon finish_report(). */
  SVN_ERR(ra_svn_get_reporter(sess_baton, pool, diff_editor, diff_baton,
                              target, depth, reporter, report_baton));
  return SVN_NO_ERROR;
}

/* Return TRUE if ITEM matches the word "done". */
static svn_boolean_t
is_done_response(const svn_ra_svn__item_t *item)
{
  static const svn_string_t str_done = SVN__STATIC_STRING("done");

  return (   item->kind == SVN_RA_SVN_WORD
          && svn_string_compare(&item->u.word, &str_done));
}


static svn_error_t *
perform_ra_svn_log(svn_error_t **outer_error,
                   svn_ra_session_t *session,
                   const apr_array_header_t *paths,
                   svn_revnum_t start, svn_revnum_t end,
                   int limit,
                   svn_boolean_t discover_changed_paths,
                   svn_boolean_t strict_node_history,
                   svn_boolean_t include_merged_revisions,
                   const apr_array_header_t *revprops,
                   svn_log_entry_receiver_t receiver,
                   void *receiver_baton,
                   apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  apr_pool_t *iterpool;
  int i;
  int nest_level = 0;
  const char *path;
  char *name;
  svn_boolean_t want_custom_revprops;
  svn_boolean_t want_author = FALSE;
  svn_boolean_t want_message = FALSE;
  svn_boolean_t want_date = FALSE;
  int nreceived = 0;

  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w((!", "log"));
  if (paths)
    {
      for (i = 0; i < paths->nelts; i++)
        {
          path = APR_ARRAY_IDX(paths, i, const char *);
          SVN_ERR(svn_ra_svn__write_cstring(conn, pool, path));
        }
    }
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!)(?r)(?r)bbnb!", start, end,
                                  discover_changed_paths, strict_node_history,
                                  (apr_uint64_t) limit,
                                  include_merged_revisions));
  if (revprops)
    {
      want_custom_revprops = FALSE;
      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!w(!", "revprops"));
      for (i = 0; i < revprops->nelts; i++)
        {
          name = APR_ARRAY_IDX(revprops, i, char *);
          SVN_ERR(svn_ra_svn__write_cstring(conn, pool, name));

          if (strcmp(name, SVN_PROP_REVISION_AUTHOR) == 0)
            want_author = TRUE;
          else if (strcmp(name, SVN_PROP_REVISION_DATE) == 0)
            want_date = TRUE;
          else if (strcmp(name, SVN_PROP_REVISION_LOG) == 0)
            want_message = TRUE;
          else
            want_custom_revprops = TRUE;
        }
      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!))"));
    }
  else
    {
      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!w())", "all-revprops"));

      want_author = TRUE;
      want_date = TRUE;
      want_message = TRUE;
      want_custom_revprops = TRUE;
    }

  SVN_ERR(handle_auth_request(sess_baton, pool));

  /* Read the log messages. */
  iterpool = svn_pool_create(pool);
  while (1)
    {
      apr_uint64_t has_children_param, invalid_revnum_param;
      apr_uint64_t has_subtractive_merge_param;
      svn_string_t *author, *date, *message;
      svn_ra_svn__list_t *cplist, *rplist;
      svn_log_entry_t *log_entry;
      svn_boolean_t has_children;
      svn_boolean_t subtractive_merge = FALSE;
      apr_uint64_t revprop_count;
      svn_ra_svn__item_t *item;
      apr_hash_t *cphash;
      svn_revnum_t rev;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &item));
      if (is_done_response(item))
        break;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Log entry not a list"));
      SVN_ERR(svn_ra_svn__parse_tuple(&item->u.list,
                                      "lr(?s)(?s)(?s)?BBnl?B",
                                      &cplist, &rev, &author, &date,
                                      &message, &has_children_param,
                                      &invalid_revnum_param,
                                      &revprop_count, &rplist,
                                      &has_subtractive_merge_param));
      if (want_custom_revprops && rplist == NULL)
        {
          /* Caller asked for custom revprops, but server is too old. */
          return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL,
                                  _("Server does not support custom revprops"
                                    " via log"));
        }

      if (has_children_param == SVN_RA_SVN_UNSPECIFIED_NUMBER)
        has_children = FALSE;
      else
        has_children = (svn_boolean_t) has_children_param;

      if (has_subtractive_merge_param == SVN_RA_SVN_UNSPECIFIED_NUMBER)
        subtractive_merge = FALSE;
      else
        subtractive_merge = (svn_boolean_t) has_subtractive_merge_param;

      /* Because the svn protocol won't let us send an invalid revnum, we have
         to recover that fact using the extra parameter. */
      if (invalid_revnum_param != SVN_RA_SVN_UNSPECIFIED_NUMBER
            && invalid_revnum_param)
        rev = SVN_INVALID_REVNUM;

      if (cplist->nelts > 0)
        {
          /* Interpret the changed-paths list. */
          cphash = svn_hash__make(iterpool);
          for (i = 0; i < cplist->nelts; i++)
            {
              svn_log_changed_path2_t *change;
              svn_string_t *cpath;
              const char *copy_path, *action, *kind_str;
              apr_uint64_t text_mods, prop_mods;
              svn_revnum_t copy_rev;
              svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(cplist, i);

              if (elt->kind != SVN_RA_SVN_LIST)
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                        _("Changed-path entry not a list"));
              SVN_ERR(svn_ra_svn__read_data_log_changed_entry(&elt->u.list,
                                              &cpath, &action, &copy_path,
                                              &copy_rev, &kind_str,
                                              &text_mods, &prop_mods));

              if (!svn_fspath__is_canonical(cpath->data))
                {
                  cpath->data = svn_fspath__canonicalize(cpath->data, iterpool);
                  cpath->len = strlen(cpath->data);
                }
              if (copy_path && !svn_fspath__is_canonical(copy_path))
                copy_path = svn_fspath__canonicalize(copy_path, iterpool);

              change = svn_log_changed_path2_create(iterpool);
              change->action = *action;
              change->copyfrom_path = copy_path;
              change->copyfrom_rev = copy_rev;
              change->node_kind = svn_node_kind_from_word(kind_str);
              change->text_modified = optbool_to_tristate(text_mods);
              change->props_modified = optbool_to_tristate(prop_mods);
              apr_hash_set(cphash, cpath->data, cpath->len, change);
            }
        }
      else
        cphash = NULL;

      /* Invoke RECEIVER
          - Except if the server sends more than a >= 1 limit top level items
          - Or when the callback reported a SVN_ERR_CEASE_INVOCATION
            in an earlier invocation. */
      if (! ((limit > 0) && (nest_level == 0) && (++nreceived > limit))
          && ! *outer_error)
        {
          svn_error_t *err;
          log_entry = svn_log_entry_create(iterpool);

          log_entry->changed_paths = cphash;
          log_entry->changed_paths2 = cphash;
          log_entry->revision = rev;
          log_entry->has_children = has_children;
          log_entry->subtractive_merge = subtractive_merge;
          if (rplist)
            SVN_ERR(svn_ra_svn__parse_proplist(rplist, iterpool,
                                               &log_entry->revprops));
          if (log_entry->revprops == NULL)
            log_entry->revprops = svn_hash__make(iterpool);

          if (author && want_author)
            svn_hash_sets(log_entry->revprops,
                          SVN_PROP_REVISION_AUTHOR, author);
          if (date && want_date)
            svn_hash_sets(log_entry->revprops,
                          SVN_PROP_REVISION_DATE, date);
          if (message && want_message)
            svn_hash_sets(log_entry->revprops,
                          SVN_PROP_REVISION_LOG, message);

          err = receiver(receiver_baton, log_entry, iterpool);
          if (svn_error_find_cause(err, SVN_ERR_CEASE_INVOCATION))
            {
              *outer_error = svn_error_trace(
                                svn_error_compose_create(*outer_error, err));
            }
          else
            SVN_ERR(err);

          if (log_entry->has_children)
            {
              nest_level++;
            }
          if (! SVN_IS_VALID_REVNUM(log_entry->revision))
            {
              SVN_ERR_ASSERT(nest_level);
              nest_level--;
            }
        }
    }
  svn_pool_destroy(iterpool);

  /* Read the response. */
  return svn_error_trace(svn_ra_svn__read_cmd_response(conn, pool, ""));
}

static svn_error_t *
ra_svn_log(svn_ra_session_t *session,
           const apr_array_header_t *paths,
           svn_revnum_t start, svn_revnum_t end,
           int limit,
           svn_boolean_t discover_changed_paths,
           svn_boolean_t strict_node_history,
           svn_boolean_t include_merged_revisions,
           const apr_array_header_t *revprops,
           svn_log_entry_receiver_t receiver,
           void *receiver_baton, apr_pool_t *pool)
{
  svn_error_t *outer_error = NULL;
  svn_error_t *err;

  /* If we don't specify paths, the session's URL is implied.

     Because the paths passed to callbacks are always relative the repos
     root, there is no need *always* sync the parent URLs despite invoking
     user-provided callbacks. */
  if (paths)
    paths = reparent_path_array(session, paths, pool);
  else
    SVN_ERR(ensure_exact_server_parent(session, pool));

  err = svn_error_trace(perform_ra_svn_log(&outer_error,
                                           session, paths,
                                           start, end,
                                           limit,
                                           discover_changed_paths,
                                           strict_node_history,
                                           include_merged_revisions,
                                           revprops,
                                           receiver, receiver_baton,
                                           pool));
  return svn_error_trace(
            svn_error_compose_create(outer_error,
                                     err));
}



static svn_error_t *ra_svn_check_path(svn_ra_session_t *session,
                                      const char *path, svn_revnum_t rev,
                                      svn_node_kind_t *kind, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  const char *kind_word;

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_cmd_check_path(conn, pool, path, rev));
  SVN_ERR(handle_auth_request(sess_baton, pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "w", &kind_word));
  *kind = svn_node_kind_from_word(kind_word);
  return SVN_NO_ERROR;
}


/* If ERR is a command not supported error, wrap it in a
   SVN_ERR_RA_NOT_IMPLEMENTED with error message MSG.  Else, return err. */
static svn_error_t *handle_unsupported_cmd(svn_error_t *err,
                                           const char *msg)
{
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                            _(msg));
  return err;
}


static svn_error_t *ra_svn_stat(svn_ra_session_t *session,
                                const char *path, svn_revnum_t rev,
                                svn_dirent_t **dirent, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn__list_t *list = NULL;
  svn_dirent_t *the_dirent;

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_cmd_stat(conn, pool, path, rev));
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 N_("'stat' not implemented")));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "(?l)", &list));

  if (! list)
    {
      *dirent = NULL;
    }
  else
    {
      const char *kind, *cdate, *cauthor;
      svn_boolean_t has_props;
      svn_revnum_t crev;
      apr_uint64_t size;

      SVN_ERR(svn_ra_svn__parse_tuple(list, "wnbr(?c)(?c)",
                                      &kind, &size, &has_props,
                                      &crev, &cdate, &cauthor));

      the_dirent = svn_dirent_create(pool);
      the_dirent->kind = svn_node_kind_from_word(kind);
      the_dirent->size = size;/* FIXME: svn_filesize_t */
      the_dirent->has_props = has_props;
      the_dirent->created_rev = crev;
      SVN_ERR(svn_time_from_cstring(&the_dirent->time, cdate, pool));
      the_dirent->last_author = cauthor;

      *dirent = the_dirent;
    }

  return SVN_NO_ERROR;
}


static svn_error_t *ra_svn_get_locations(svn_ra_session_t *session,
                                         apr_hash_t **locations,
                                         const char *path,
                                         svn_revnum_t peg_revision,
                                         const apr_array_header_t *location_revisions,
                                         apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_revnum_t revision;
  svn_boolean_t is_done;
  apr_pool_t *iterpool;
  int i;

  path = reparent_path(session, path, pool);

  /* Transmit the parameters. */
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(cr(!",
                                  "get-locations", path, peg_revision));
  for (i = 0; i < location_revisions->nelts; i++)
    {
      revision = APR_ARRAY_IDX(location_revisions, i, svn_revnum_t);
      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!r!", revision));
    }

  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!))"));

  /* Servers before 1.1 don't support this command. Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 N_("'get-locations' not implemented")));

  /* Read the hash items. */
  is_done = FALSE;
  *locations = apr_hash_make(pool);
  iterpool = svn_pool_create(pool);
  while (!is_done)
    {
      svn_ra_svn__item_t *item;
      const char *ret_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &item));
      if (is_done_response(item))
        is_done = 1;
      else if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Location entry not a list"));
      else
        {
          SVN_ERR(svn_ra_svn__parse_tuple(&item->u.list, "rc",
                                          &revision, &ret_path));

          /* This also makes RET_PATH live in POOL rather than ITERPOOL. */
          ret_path = svn_fspath__canonicalize(ret_path, pool);
          apr_hash_set(*locations, apr_pmemdup(pool, &revision,
                                               sizeof(revision)),
                       sizeof(revision), ret_path);
        }
    }

  svn_pool_destroy(iterpool);

  /* Read the response. This is so the server would have a chance to
   * report an error. */
  return svn_error_trace(svn_ra_svn__read_cmd_response(conn, pool, ""));
}

static svn_error_t *
perform_get_location_segments(svn_error_t **outer_error,
                              svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t peg_revision,
                              svn_revnum_t start_rev,
                              svn_revnum_t end_rev,
                              svn_location_segment_receiver_t receiver,
                              void *receiver_baton,
                              apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_boolean_t is_done;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Transmit the parameters. */
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(c(?r)(?r)(?r))",
                                  "get-location-segments",
                                  path, peg_revision, start_rev, end_rev));

  /* Servers before 1.5 don't support this command. Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 N_("'get-location-segments'"
                                    " not implemented")));

  /* Parse the response. */
  is_done = FALSE;
  while (!is_done)
    {
      svn_revnum_t range_start, range_end;
      svn_ra_svn__item_t *item;
      const char *ret_path;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &item));
      if (is_done_response(item))
        is_done = 1;
      else if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Location segment entry not a list"));
      else
        {
          svn_location_segment_t *segment = apr_pcalloc(iterpool,
                                                        sizeof(*segment));
          SVN_ERR(svn_ra_svn__parse_tuple(&item->u.list, "rr(?c)",
                                          &range_start, &range_end, &ret_path));
          if (! (SVN_IS_VALID_REVNUM(range_start)
                 && SVN_IS_VALID_REVNUM(range_end)))
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Expected valid revision range"));
          if (ret_path)
            ret_path = svn_relpath_canonicalize(ret_path, iterpool);
          segment->path = ret_path;
          segment->range_start = range_start;
          segment->range_end = range_end;

          if (!*outer_error)
            {
              svn_error_t *err = svn_error_trace(receiver(segment, receiver_baton,
                                                          iterpool));

              if (svn_error_find_cause(err, SVN_ERR_CEASE_INVOCATION))
                *outer_error = err;
              else
                SVN_ERR(err);
            }
        }
    }
  svn_pool_destroy(iterpool);

  /* Read the response. This is so the server would have a chance to
   * report an error. */
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_get_location_segments(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t peg_revision,
                             svn_revnum_t start_rev,
                             svn_revnum_t end_rev,
                             svn_location_segment_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool)
{
  svn_error_t *outer_err = SVN_NO_ERROR;
  svn_error_t *err;

  path = reparent_path(session, path, pool);
  err = svn_error_trace(
            perform_get_location_segments(&outer_err, session, path,
                                          peg_revision, start_rev, end_rev,
                                          receiver, receiver_baton, pool));
  return svn_error_compose_create(outer_err, err);
}

static svn_error_t *ra_svn_get_file_revs(svn_ra_session_t *session,
                                         const char *path,
                                         svn_revnum_t start, svn_revnum_t end,
                                         svn_boolean_t include_merged_revisions,
                                         svn_file_rev_handler_t handler,
                                         void *handler_baton, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  apr_pool_t *rev_pool, *chunk_pool;
  svn_boolean_t has_txdelta;
  svn_boolean_t had_revision = FALSE;

  /* One sub-pool for each revision and one for each txdelta chunk.
     Note that the rev_pool must live during the following txdelta. */
  rev_pool = svn_pool_create(pool);
  chunk_pool = svn_pool_create(pool);

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_cmd_get_file_revs(sess_baton->conn, pool,
                                              path, start, end,
                                              include_merged_revisions));

  /* Servers before 1.1 don't support this command.  Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 N_("'get-file-revs' not implemented")));

  while (1)
    {
      svn_ra_svn__list_t *rev_proplist, *proplist;
      apr_uint64_t merged_rev_param;
      apr_array_header_t *props;
      svn_ra_svn__item_t *item;
      apr_hash_t *rev_props;
      svn_revnum_t rev;
      const char *p;
      svn_boolean_t merged_rev;
      svn_txdelta_window_handler_t d_handler;
      void *d_baton;

      svn_pool_clear(rev_pool);
      svn_pool_clear(chunk_pool);
      SVN_ERR(svn_ra_svn__read_item(sess_baton->conn, rev_pool, &item));
      if (is_done_response(item))
        break;
      /* Either we've got a correct revision or we will error out below. */
      had_revision = TRUE;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Revision entry not a list"));

      SVN_ERR(svn_ra_svn__parse_tuple(&item->u.list,
                                      "crll?B", &p, &rev, &rev_proplist,
                                      &proplist, &merged_rev_param));
      p = svn_fspath__canonicalize(p, rev_pool);
      SVN_ERR(svn_ra_svn__parse_proplist(rev_proplist, rev_pool, &rev_props));
      SVN_ERR(parse_prop_diffs(proplist, rev_pool, &props));
      if (merged_rev_param == SVN_RA_SVN_UNSPECIFIED_NUMBER)
        merged_rev = FALSE;
      else
        merged_rev = (svn_boolean_t) merged_rev_param;

      /* Get the first delta chunk so we know if there is a delta. */
      SVN_ERR(svn_ra_svn__read_item(sess_baton->conn, chunk_pool, &item));
      if (item->kind != SVN_RA_SVN_STRING)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Text delta chunk not a string"));
      has_txdelta = item->u.string.len > 0;

      SVN_ERR(handler(handler_baton, p, rev, rev_props, merged_rev,
                      has_txdelta ? &d_handler : NULL, &d_baton,
                      props, rev_pool));

      /* Process the text delta if any. */
      if (has_txdelta)
        {
          svn_stream_t *stream;

          if (d_handler && d_handler != svn_delta_noop_window_handler)
            stream = svn_txdelta_parse_svndiff(d_handler, d_baton, TRUE,
                                               rev_pool);
          else
            stream = NULL;
          while (item->u.string.len > 0)
            {
              apr_size_t size;

              size = item->u.string.len;
              if (stream)
                SVN_ERR(svn_stream_write(stream, item->u.string.data, &size));
              svn_pool_clear(chunk_pool);

              SVN_ERR(svn_ra_svn__read_item(sess_baton->conn, chunk_pool,
                                            &item));
              if (item->kind != SVN_RA_SVN_STRING)
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                        _("Text delta chunk not a string"));
            }
          if (stream)
            SVN_ERR(svn_stream_close(stream));
        }
    }

  SVN_ERR(svn_ra_svn__read_cmd_response(sess_baton->conn, pool, ""));

  /* Return error if we didn't get any revisions. */
  if (!had_revision)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("The get-file-revs command didn't return "
                              "any revisions"));

  svn_pool_destroy(chunk_pool);
  svn_pool_destroy(rev_pool);

  return SVN_NO_ERROR;
}

/* For each path in PATH_REVS, send a 'lock' command to the server.
   Used with 1.2.x series servers which support locking, but of only
   one path at a time.  ra_svn_lock(), which supports 'lock-many'
   is now the default.  See svn_ra_lock() docstring for interface details. */
static svn_error_t *ra_svn_lock_compat(svn_ra_session_t *session,
                                       apr_hash_t *path_revs,
                                       const char *comment,
                                       svn_boolean_t steal_lock,
                                       svn_ra_lock_callback_t lock_func,
                                       void *lock_baton,
                                       apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  svn_ra_svn__list_t *list;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_lock_t *lock;
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;
      svn_error_t *err, *callback_err = NULL;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      SVN_ERR(svn_ra_svn__write_cmd_lock(conn, iterpool, path, comment,
                                         steal_lock, *revnum));

      /* Servers before 1.2 doesn't support locking.  Check this here. */
      SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                     N_("Server doesn't support "
                                        "the lock command")));

      err = svn_ra_svn__read_cmd_response(conn, iterpool, "l", &list);

      if (!err)
        SVN_ERR(parse_lock(list, iterpool, &lock));

      if (err && !SVN_ERR_IS_LOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE, err ? NULL : lock,
                                 err, iterpool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* For each path in PATH_TOKENS, send an 'unlock' command to the server.
   Used with 1.2.x series servers which support unlocking, but of only
   one path at a time.  ra_svn_unlock(), which supports 'unlock-many' is
   now the default.  See svn_ra_unlock() docstring for interface details. */
static svn_error_t *ra_svn_unlock_compat(svn_ra_session_t *session,
                                         apr_hash_t *path_tokens,
                                         svn_boolean_t break_lock,
                                         svn_ra_lock_callback_t lock_func,
                                         void *lock_baton,
                                         apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      const svn_string_t *token;
      svn_error_t *err, *callback_err = NULL;

      svn_pool_clear(iterpool);

      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      if (strcmp(val, "") != 0)
        token = svn_string_create(val, iterpool);
      else
        token = NULL;

      SVN_ERR(svn_ra_svn__write_cmd_unlock(conn, iterpool, path, token,
                                           break_lock));

      /* Servers before 1.2 don't support locking.  Check this here. */
      SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, iterpool),
                                     N_("Server doesn't support the unlock "
                                        "command")));

      err = svn_ra_svn__read_cmd_response(conn, iterpool, "");

      if (err && !SVN_ERR_IS_UNLOCK_ERROR(err))
        return err;

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err, pool);

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Tell the server to lock all paths in PATH_REVS.
   See svn_ra_lock() for interface details. */
static svn_error_t *ra_svn_lock(svn_ra_session_t *session,
                                apr_hash_t *path_revs,
                                const char *comment,
                                svn_boolean_t steal_lock,
                                svn_ra_lock_callback_t lock_func,
                                void *lock_baton,
                                apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t *conn = sess->conn;
  apr_hash_index_t *hi;
  svn_error_t *err;
  apr_pool_t *iterpool = svn_pool_create(pool);

  path_revs = reparent_path_hash(session, path_revs, pool, pool);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w((?c)b(!", "lock-many",
                                  comment, steal_lock));

  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      const char *path;
      void *val;
      svn_revnum_t *revnum;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;
      revnum = val;

      SVN_ERR(svn_ra_svn__write_tuple(conn, iterpool, "c(?r)", path, *revnum));
    }

  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!))"));

  err = handle_auth_request(sess, pool);

  /* Pre-1.3 servers don't support 'lock-many'. If that fails, fall back
   * to 'lock'. */
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    {
      svn_error_clear(err);
      return ra_svn_lock_compat(session, path_revs, comment, steal_lock,
                                lock_func, lock_baton, pool);
    }

  if (err)
    return err;

  /* Loop over responses to get lock information. */
  for (hi = apr_hash_first(pool, path_revs); hi; hi = apr_hash_next(hi))
    {
      svn_ra_svn__item_t *elt;
      const void *key;
      const char *path;
      svn_error_t *callback_err;
      const char *status;
      svn_lock_t *lock;
      svn_ra_svn__list_t *list;

      apr_hash_this(hi, &key, NULL, NULL);
      path = key;

      svn_pool_clear(iterpool);
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &elt));

      /* The server might have encountered some sort of fatal error in
         the middle of the request list.  If this happens, it will
         transmit "done" to end the lock-info early, and then the
         overall command response will talk about the fatal error. */
      if (is_done_response(elt))
        break;

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Lock response not a list"));

      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "wl", &status, &list));

      if (strcmp(status, "failure") == 0)
        err = svn_ra_svn__handle_failure_status(list);
      else if (strcmp(status, "success") == 0)
        {
          SVN_ERR(parse_lock(list, iterpool, &lock));
          err = NULL;
        }
      else
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unknown status for lock command"));

      if (lock_func)
        callback_err = lock_func(lock_baton, path, TRUE,
                                 err ? NULL : lock,
                                 err, iterpool);
      else
        callback_err = SVN_NO_ERROR;

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  /* If we didn't break early above, and the whole hash was traversed,
     read the final "done" from the server. */
  if (!hi)
    {
      svn_ra_svn__item_t *elt;

      SVN_ERR(svn_ra_svn__read_item(conn, pool, &elt));
      if (!is_done_response(elt))
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Didn't receive end marker for lock "
                                  "responses"));
    }

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Tell the server to unlock all paths in PATH_TOKENS.
   See svn_ra_unlock() for interface details. */
static svn_error_t *ra_svn_unlock(svn_ra_session_t *session,
                                  apr_hash_t *path_tokens,
                                  svn_boolean_t break_lock,
                                  svn_ra_lock_callback_t lock_func,
                                  void *lock_baton,
                                  apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t *conn = sess->conn;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_error_t *err;
  const char *path;

  path_tokens = reparent_path_hash(session, path_tokens, pool, pool);
  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "w(b(!", "unlock-many",
                                  break_lock));

  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      void *val;
      const void *key;
      const char *token;

      svn_pool_clear(iterpool);
      apr_hash_this(hi, &key, NULL, &val);
      path = key;

      if (strcmp(val, "") != 0)
        token = val;
      else
        token = NULL;

      SVN_ERR(svn_ra_svn__write_tuple(conn, iterpool, "c(?c)", path, token));
    }

  SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "!))"));

  err = handle_auth_request(sess, pool);

  /* Pre-1.3 servers don't support 'unlock-many'. If unknown, fall back
   * to 'unlock'.
   */
  if (err && err->apr_err == SVN_ERR_RA_SVN_UNKNOWN_CMD)
    {
      svn_error_clear(err);
      return ra_svn_unlock_compat(session, path_tokens, break_lock, lock_func,
                                  lock_baton, pool);
    }

  if (err)
    return err;

  /* Loop over responses to unlock files. */
  for (hi = apr_hash_first(pool, path_tokens); hi; hi = apr_hash_next(hi))
    {
      svn_ra_svn__item_t *elt;
      const void *key;
      svn_error_t *callback_err;
      const char *status;
      svn_ra_svn__list_t *list;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &elt));

      /* The server might have encountered some sort of fatal error in
         the middle of the request list.  If this happens, it will
         transmit "done" to end the lock-info early, and then the
         overall command response will talk about the fatal error. */
      if (is_done_response(elt))
        break;

      apr_hash_this(hi, &key, NULL, NULL);
      path = key;

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unlock response not a list"));

      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "wl", &status, &list));

      if (strcmp(status, "failure") == 0)
        err = svn_ra_svn__handle_failure_status(list);
      else if (strcmp(status, "success") == 0)
        {
          SVN_ERR(svn_ra_svn__parse_tuple(list, "c", &path));
          err = SVN_NO_ERROR;
        }
      else
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Unknown status for unlock command"));

      if (lock_func)
        callback_err = lock_func(lock_baton, path, FALSE, NULL, err,
                                 iterpool);
      else
        callback_err = SVN_NO_ERROR;

      svn_error_clear(err);

      if (callback_err)
        return callback_err;
    }

  /* If we didn't break early above, and the whole hash was traversed,
     read the final "done" from the server. */
  if (!hi)
    {
      svn_ra_svn__item_t *elt;

      SVN_ERR(svn_ra_svn__read_item(conn, pool, &elt));
      if (! is_done_response(elt))
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Didn't receive end marker for unlock "
                                  "responses"));
    }

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, ""));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_lock(svn_ra_session_t *session,
                                    svn_lock_t **lock,
                                    const char *path,
                                    apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  svn_ra_svn__list_t *list;

  path = reparent_path(session, path, pool);
  SVN_ERR(svn_ra_svn__write_cmd_get_lock(conn, pool, path));

  /* Servers before 1.2 doesn't support locking.  Check this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 N_("Server doesn't support the get-lock "
                                    "command")));

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "(?l)", &list));
  if (list)
    SVN_ERR(parse_lock(list, pool, lock));
  else
    *lock = NULL;

  return SVN_NO_ERROR;
}

/* Copied from svn_ra_get_path_relative_to_root() and de-vtable-ized
   to prevent a dependency cycle. */
static svn_error_t *path_relative_to_root(svn_ra_session_t *session,
                                          const char **rel_path,
                                          const char *url,
                                          apr_pool_t *pool)
{
  const char *root_url;

  SVN_ERR(ra_svn_get_repos_root(session, &root_url, pool));
  *rel_path = svn_uri_skip_ancestor(root_url, url, pool);
  if (! *rel_path)
    return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                             _("'%s' isn't a child of repository root "
                               "URL '%s'"),
                             url, root_url);
  return SVN_NO_ERROR;
}

static svn_error_t *ra_svn_get_locks(svn_ra_session_t *session,
                                     apr_hash_t **locks,
                                     const char *path,
                                     svn_depth_t depth,
                                     apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  svn_ra_svn_conn_t* conn = sess->conn;
  svn_ra_svn__list_t *list;
  const char *full_url, *abs_path;
  int i;

  /* Figure out the repository abspath from PATH. */
  full_url = svn_path_url_add_component2(sess->parent->client_url->data,
                                         path, pool);
  SVN_ERR(path_relative_to_root(session, &abs_path, full_url, pool));
  abs_path = svn_fspath__canonicalize(abs_path, pool);

  SVN_ERR(svn_ra_svn__write_cmd_get_locks(conn, pool, path, depth));

  /* Servers before 1.2 doesn't support locking.  Check this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 N_("Server doesn't support the get-lock "
                                    "command")));

  SVN_ERR(svn_ra_svn__read_cmd_response(conn, pool, "l", &list));

  *locks = apr_hash_make(pool);

  for (i = 0; i < list->nelts; ++i)
    {
      svn_lock_t *lock;
      svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(list, i);

      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Lock element not a list"));
      SVN_ERR(parse_lock(&elt->u.list, pool, &lock));

      /* Filter out unwanted paths.  Since Subversion only allows
         locks on files, we can treat depth=immediates the same as
         depth=files for filtering purposes.  Meaning, we'll keep
         this lock if:

         a) its path is the very path we queried, or
         b) we've asked for a fully recursive answer, or
         c) we've asked for depth=files or depth=immediates, and this
            lock is on an immediate child of our query path.
      */
      if ((strcmp(abs_path, lock->path) == 0) || (depth == svn_depth_infinity))
        {
          svn_hash_sets(*locks, lock->path, lock);
        }
      else if ((depth == svn_depth_files) || (depth == svn_depth_immediates))
        {
          const char *relpath = svn_fspath__skip_ancestor(abs_path, lock->path);
          if (relpath && (svn_path_component_count(relpath) == 1))
            svn_hash_sets(*locks, lock->path, lock);
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *ra_svn_replay(svn_ra_session_t *session,
                                  svn_revnum_t revision,
                                  svn_revnum_t low_water_mark,
                                  svn_boolean_t send_deltas,
                                  const svn_delta_editor_t *editor,
                                  void *edit_baton,
                                  apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;

  /* Complex EDITOR callbacks may rely on client and server parent path
     being in sync. */
  SVN_ERR(ensure_exact_server_parent(session, pool));
  SVN_ERR(svn_ra_svn__write_cmd_replay(sess->conn, pool, revision,
                                       low_water_mark, send_deltas));

  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 N_("Server doesn't support the replay "
                                    "command")));

  SVN_ERR(svn_ra_svn_drive_editor2(sess->conn, pool, editor, edit_baton,
                                   NULL, TRUE));

  return svn_error_trace(svn_ra_svn__read_cmd_response(sess->conn, pool, ""));
}


static svn_error_t *
ra_svn_replay_range(svn_ra_session_t *session,
                    svn_revnum_t start_revision,
                    svn_revnum_t end_revision,
                    svn_revnum_t low_water_mark,
                    svn_boolean_t send_deltas,
                    svn_ra_replay_revstart_callback_t revstart_func,
                    svn_ra_replay_revfinish_callback_t revfinish_func,
                    void *replay_baton,
                    apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  apr_pool_t *iterpool;
  svn_revnum_t rev;
  svn_boolean_t drive_aborted = FALSE;

  /* Complex EDITOR callbacks may rely on client and server parent path
     being in sync. */
  SVN_ERR(ensure_exact_server_parent(session, pool));
  SVN_ERR(svn_ra_svn__write_cmd_replay_range(sess->conn, pool,
                                             start_revision, end_revision,
                                             low_water_mark, send_deltas));

  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess, pool),
                                 N_("Server doesn't support the "
                                    "replay-range command")));

  iterpool = svn_pool_create(pool);
  for (rev = start_revision; rev <= end_revision; rev++)
    {
      const svn_delta_editor_t *editor;
      void *edit_baton;
      apr_hash_t *rev_props;
      const char *word;
      svn_ra_svn__list_t *list;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_ra_svn__read_tuple(sess->conn, iterpool,
                                     "wl", &word, &list));

      if (strcmp(word, "revprops") != 0)
        {
          if (strcmp(word, "failure") == 0)
            SVN_ERR(svn_ra_svn__handle_failure_status(list));

          return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                   _("Expected 'revprops', found '%s'"),
                                   word);
        }

      SVN_ERR(svn_ra_svn__parse_proplist(list, iterpool, &rev_props));

      SVN_ERR(revstart_func(rev, replay_baton,
                            &editor, &edit_baton,
                            rev_props,
                            iterpool));
      SVN_ERR(svn_ra_svn_drive_editor2(sess->conn, iterpool,
                                       editor, edit_baton,
                                       &drive_aborted, TRUE));
      /* If drive_editor2() aborted the commit, do NOT try to call
         revfinish_func and commit the transaction! */
      if (drive_aborted) {
        svn_pool_destroy(iterpool);
        return svn_error_create(SVN_ERR_RA_SVN_EDIT_ABORTED, NULL,
                                _("Error while replaying commit"));
      }
      SVN_ERR(revfinish_func(rev, replay_baton,
                             editor, edit_baton,
                             rev_props,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  return svn_error_trace(svn_ra_svn__read_cmd_response(sess->conn, pool, ""));
}


static svn_error_t *
ra_svn_has_capability(svn_ra_session_t *session,
                      svn_boolean_t *has,
                      const char *capability,
                      apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *sess = session->priv;
  static const char* capabilities[][2] =
  {
      /* { ra capability string, svn:// wire capability string} */
      {SVN_RA_CAPABILITY_DEPTH, SVN_RA_SVN_CAP_DEPTH},
      {SVN_RA_CAPABILITY_MERGEINFO, SVN_RA_SVN_CAP_MERGEINFO},
      {SVN_RA_CAPABILITY_LOG_REVPROPS, SVN_RA_SVN_CAP_LOG_REVPROPS},
      {SVN_RA_CAPABILITY_PARTIAL_REPLAY, SVN_RA_SVN_CAP_PARTIAL_REPLAY},
      {SVN_RA_CAPABILITY_COMMIT_REVPROPS, SVN_RA_SVN_CAP_COMMIT_REVPROPS},
      {SVN_RA_CAPABILITY_ATOMIC_REVPROPS, SVN_RA_SVN_CAP_ATOMIC_REVPROPS},
      {SVN_RA_CAPABILITY_INHERITED_PROPS, SVN_RA_SVN_CAP_INHERITED_PROPS},
      {SVN_RA_CAPABILITY_EPHEMERAL_TXNPROPS,
                                          SVN_RA_SVN_CAP_EPHEMERAL_TXNPROPS},
      {SVN_RA_CAPABILITY_GET_FILE_REVS_REVERSE,
                                       SVN_RA_SVN_CAP_GET_FILE_REVS_REVERSE},
      {SVN_RA_CAPABILITY_LIST, SVN_RA_SVN_CAP_LIST},

      {NULL, NULL} /* End of list marker */
  };
  int i;

  *has = FALSE;

  for (i = 0; capabilities[i][0]; i++)
    {
      if (strcmp(capability, capabilities[i][0]) == 0)
        {
          *has = svn_ra_svn_has_capability(sess->conn, capabilities[i][1]);
          return SVN_NO_ERROR;
        }
    }

  return svn_error_createf(SVN_ERR_UNKNOWN_CAPABILITY, NULL,
                           _("Don't know anything about capability '%s'"),
                           capability);
}

static svn_error_t *
ra_svn_get_deleted_rev(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t peg_revision,
                       svn_revnum_t end_revision,
                       svn_revnum_t *revision_deleted,
                       apr_pool_t *pool)

{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  path = reparent_path(session, path, pool);

  /* Transmit the parameters. */
  SVN_ERR(svn_ra_svn__write_cmd_get_deleted_rev(conn, pool, path,
                                               peg_revision, end_revision));

  /* Servers before 1.6 don't support this command.  Check for this here. */
  SVN_ERR(handle_unsupported_cmd(handle_auth_request(sess_baton, pool),
                                 N_("'get-deleted-rev' not implemented")));

  return svn_error_trace(svn_ra_svn__read_cmd_response(conn, pool, "r",
                                                       revision_deleted));
}

static svn_error_t *
ra_svn_register_editor_shim_callbacks(svn_ra_session_t *session,
                                      svn_delta_shim_callbacks_t *callbacks)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;

  conn->shim_callbacks = callbacks;

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_get_inherited_props(svn_ra_session_t *session,
                           apr_array_header_t **iprops,
                           const char *path,
                           svn_revnum_t revision,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  svn_ra_svn__list_t *iproplist;
  svn_boolean_t iprop_capable;

  path = reparent_path(session, path, scratch_pool);
  SVN_ERR(ra_svn_has_capability(session, &iprop_capable,
                                SVN_RA_CAPABILITY_INHERITED_PROPS,
                                scratch_pool));

  /* If we don't support native iprop handling, use the implementation
     in libsvn_ra */
  if (!iprop_capable)
    return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, NULL, NULL);

  SVN_ERR(svn_ra_svn__write_cmd_get_iprops(conn, scratch_pool,
                                           path, revision));
  SVN_ERR(handle_auth_request(sess_baton, scratch_pool));
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, scratch_pool, "l", &iproplist));
  SVN_ERR(parse_iproplist(iprops, iproplist, session, result_pool,
                          scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
ra_svn_list(svn_ra_session_t *session,
            const char *path,
            svn_revnum_t revision,
            const apr_array_header_t *patterns,
            svn_depth_t depth,
            apr_uint32_t dirent_fields,
            svn_ra_dirent_receiver_t receiver,
            void *receiver_baton,
            apr_pool_t *scratch_pool)
{
  svn_ra_svn__session_baton_t *sess_baton = session->priv;
  svn_ra_svn_conn_t *conn = sess_baton->conn;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  path = reparent_path(session, path, scratch_pool);

  /* Send the list request. */
  SVN_ERR(svn_ra_svn__write_tuple(conn, scratch_pool, "w(c(?r)w(!", "list",
                                  path, revision, svn_depth_to_word(depth)));
  SVN_ERR(send_dirent_fields(conn, dirent_fields, scratch_pool));

  if (patterns)
    {
      SVN_ERR(svn_ra_svn__write_tuple(conn, scratch_pool, "!)(!"));

      for (i = 0; i < patterns->nelts; ++i)
        {
          const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
          SVN_ERR(svn_ra_svn__write_cstring(conn, scratch_pool, pattern));
        }
    }

  SVN_ERR(svn_ra_svn__write_tuple(conn, scratch_pool, "!))"));

  /* Handle auth request by server */
  SVN_ERR(handle_auth_request(sess_baton, scratch_pool));

  /* Read and process list response. */
  while (1)
    {
      svn_ra_svn__item_t *item;
      const char *dirent_path;
      const char *kind_word, *date;
      svn_dirent_t dirent = { 0 };

      svn_pool_clear(iterpool);

      /* Read the next dirent or bail out on "done", respectively */
      SVN_ERR(svn_ra_svn__read_item(conn, iterpool, &item));
      if (is_done_response(item))
        break;
      if (item->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("List entry not a list"));
      SVN_ERR(svn_ra_svn__parse_tuple(&item->u.list,
                                      "cw?(?n)(?b)(?r)(?c)(?c)",
                                      &dirent_path, &kind_word, &dirent.size,
                                      &dirent.has_props, &dirent.created_rev,
                                      &date, &dirent.last_author));

      /* Convert data. */
      dirent.kind = svn_node_kind_from_word(kind_word);
      if (date)
        SVN_ERR(svn_time_from_cstring(&dirent.time, date, iterpool));

      /* Invoke RECEIVER */
      SVN_ERR(receiver(dirent_path, &dirent, receiver_baton, iterpool));
    }
  svn_pool_destroy(iterpool);

  /* Read the actual command response. */
  SVN_ERR(svn_ra_svn__read_cmd_response(conn, scratch_pool, ""));
  return SVN_NO_ERROR;
}

static const svn_ra__vtable_t ra_svn_vtable = {
  svn_ra_svn_version,
  ra_svn_get_description,
  ra_svn_get_schemes,
  ra_svn_open,
  ra_svn_dup_session,
  ra_svn_reparent,
  ra_svn_get_session_url,
  ra_svn_get_latest_rev,
  ra_svn_get_dated_rev,
  ra_svn_change_rev_prop,
  ra_svn_rev_proplist,
  ra_svn_rev_prop,
  ra_svn_commit,
  ra_svn_get_file,
  ra_svn_get_dir,
  ra_svn_get_mergeinfo,
  ra_svn_update,
  ra_svn_switch,
  ra_svn_status,
  ra_svn_diff,
  ra_svn_log,
  ra_svn_check_path,
  ra_svn_stat,
  ra_svn_get_uuid,
  ra_svn_get_repos_root,
  ra_svn_get_locations,
  ra_svn_get_location_segments,
  ra_svn_get_file_revs,
  ra_svn_lock,
  ra_svn_unlock,
  ra_svn_get_lock,
  ra_svn_get_locks,
  ra_svn_replay,
  ra_svn_has_capability,
  ra_svn_replay_range,
  ra_svn_get_deleted_rev,
  ra_svn_get_inherited_props,
  NULL /* ra_set_svn_ra_open */,
  ra_svn_list,
  ra_svn_register_editor_shim_callbacks,
  NULL /* commit_ev2 */,
  NULL /* replay_range_ev2 */
};

svn_error_t *
svn_ra_svn__init(const svn_version_t *loader_version,
                 const svn_ra__vtable_t **vtable,
                 apr_pool_t *pool)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { NULL, NULL }
    };

  SVN_ERR(svn_ver_check_list2(svn_ra_svn_version(), checklist, svn_ver_equal));

  /* Simplified version check to make sure we can safely use the
     VTABLE parameter. The RA loader does a more exhaustive check. */
  if (loader_version->major != SVN_VER_MAJOR)
    {
      return svn_error_createf
        (SVN_ERR_VERSION_MISMATCH, NULL,
         _("Unsupported RA loader version (%d) for ra_svn"),
         loader_version->major);
    }

  *vtable = &ra_svn_vtable;

#ifdef SVN_HAVE_SASL
  SVN_ERR(svn_ra_svn__sasl_init());
#endif

  return SVN_NO_ERROR;
}

/* Compatibility wrapper for the 1.1 and before API. */
#define NAME "ra_svn"
#define DESCRIPTION RA_SVN_DESCRIPTION
#define VTBL ra_svn_vtable
#define INITFUNC svn_ra_svn__init
#define COMPAT_INITFUNC svn_ra_svn_init
#include "../libsvn_ra/wrapper_template.h"
