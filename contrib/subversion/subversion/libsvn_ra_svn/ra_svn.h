/*
 * ra_svn.h :  private declarations for the ra_svn module
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



#ifndef RA_SVN_H
#define RA_SVN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <apr_network_io.h>
#include <apr_file_io.h>
#include <apr_thread_proc.h>
#include "svn_ra.h"
#include "svn_ra_svn.h"

#include "private/svn_ra_svn_private.h"

/* Callback function that indicates if a svn_ra_svn__stream_t has pending
 * data.
 */
typedef svn_boolean_t (*ra_svn_pending_fn_t)(void *baton);

/* Callback function that sets the timeout value for a svn_ra_svn__stream_t. */
typedef void (*ra_svn_timeout_fn_t)(void *baton, apr_interval_time_t timeout);

/* A stream abstraction for ra_svn.
 *
 * This is different from svn_stream_t in that it provides timeouts and
 * the ability to check for pending data.
 */
typedef struct svn_ra_svn__stream_st svn_ra_svn__stream_t;

/* Handler for blocked writes. */
typedef svn_error_t *(*ra_svn_block_handler_t)(svn_ra_svn_conn_t *conn,
                                               apr_pool_t *pool,
                                               void *baton);

/* The default "user agent". */
#define SVN_RA_SVN__DEFAULT_USERAGENT  "SVN/" SVN_VER_NUMBER\
                                       " (" SVN_BUILD_TARGET ")"

/* The size of our per-connection read and write buffers. */
#define SVN_RA_SVN__PAGE_SIZE 4096
#define SVN_RA_SVN__READBUF_SIZE (4 * SVN_RA_SVN__PAGE_SIZE)
#define SVN_RA_SVN__WRITEBUF_SIZE (4 * SVN_RA_SVN__PAGE_SIZE)

/* Create forward reference */
typedef struct svn_ra_svn__session_baton_t svn_ra_svn__session_baton_t;

/* This structure is opaque to the server.  The client pokes at the
 * first few fields during setup and cleanup. */
struct svn_ra_svn_conn_st {

  /* I/O buffers */
  char write_buf[SVN_RA_SVN__WRITEBUF_SIZE];
  char read_buf[SVN_RA_SVN__READBUF_SIZE];
  char *read_ptr;
  char *read_end;
  apr_size_t write_pos;

  svn_ra_svn__stream_t *stream;
  svn_ra_svn__session_baton_t *session;
#ifdef SVN_HAVE_SASL
  /* Although all reads and writes go through the svn_ra_svn__stream_t
     interface, SASL still needs direct access to the underlying socket
     for stuff like IP addresses and port numbers. */
  apr_socket_t *sock;
  svn_boolean_t encrypted;
#endif

  /* abortion check control */
  apr_size_t written_since_error_check;
  apr_size_t error_check_interval;
  svn_boolean_t may_check_for_error;

  /* I/O limits and tracking */
  apr_uint64_t max_in;
  apr_uint64_t current_in;
  apr_uint64_t max_out;
  apr_uint64_t current_out;

  /* repository info */
  const char *uuid;
  const char *repos_root;

  /* TX block notification target */
  ra_svn_block_handler_t block_handler;
  void *block_baton;

  /* server settings */
  apr_hash_t *capabilities;
  int compression_level;
  apr_size_t zero_copy_limit;

  /* who's on the other side of the connection? */
  char *remote_ip;

  /* EV2 support*/
  svn_delta_shim_callbacks_t *shim_callbacks;

  /* our pool */
  apr_pool_t *pool;
};

/* The session's URL state for client and server side.
 *
 * This keeps track of the respective client-side and server-side "parent"
 * URLs.  It tells us whether we may have to send reparent commands to the
 * server and how to tweak path parameters when we decided to handle
 * reparent requests on the client side only. */
typedef struct svn_ra_svn__parent_t {
  /* Client-side session base URL, i.e. client's parent path. */
  svn_stringbuf_t *client_url;

  /* Server-side base URL, i.e. server's parent path. */
  svn_stringbuf_t *server_url;

  /* Relative path to add to a client-side parameter to translate it for the
   * server-side.  I.e. the relative path from SERVER_URL to CLIENT_URL. */
  svn_stringbuf_t *path;
} svn_ra_svn__parent_t;

struct svn_ra_svn__session_baton_t {
  apr_pool_t *pool;
  svn_ra_svn_conn_t *conn;
  svn_boolean_t is_tunneled;
  svn_auth_baton_t *auth_baton;
  svn_ra_svn__parent_t *parent;
  const char *user;
  const char *hostname; /* The remote hostname. */
  const char *realm_prefix;
  const char *tunnel_name;
  const char **tunnel_argv;
  const svn_ra_callbacks2_t *callbacks;
  void *callbacks_baton;
  apr_hash_t *config;
  apr_off_t bytes_read, bytes_written; /* apr_off_t's because that's what
                                          the callback interface uses */
  const char *useragent;
};

/* Set a callback for blocked writes on conn.  This handler may
 * perform reads on the connection in order to prevent deadlock due to
 * pipelining.  If callback is NULL, the connection goes back to
 * normal blocking I/O for writes.
 */
void svn_ra_svn__set_block_handler(svn_ra_svn_conn_t *conn,
                                   ra_svn_block_handler_t callback,
                                   void *baton);

/* Return true if there is input waiting on conn. */
svn_error_t *svn_ra_svn__data_available(svn_ra_svn_conn_t *conn,
                                       svn_boolean_t *data_available);

/* Signal a new request / response pair on CONN.  That resets the I/O
 * counters we use to limit the size of individual requests / response pairs.
 */
void
svn_ra_svn__reset_command_io_counters(svn_ra_svn_conn_t *conn);

/* CRAM-MD5 client implementation. */
svn_error_t *svn_ra_svn__cram_client(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const char *user, const char *password,
                                     const char **message);

/* Return a pointer to the error chain child of ERR which contains the
 * first "real" error message, not merely one of the
 * SVN_ERR_RA_SVN_CMD_ERR wrapper errors. */
svn_error_t *svn_ra_svn__locate_real_error_child(svn_error_t *err);

/* Return an error chain based on @a params (which contains a
 * command response indicating failure).  The error chain will be
 * in the same order as the errors indicated in @a params. */
svn_error_t *
svn_ra_svn__handle_failure_status(const svn_ra_svn__list_t *params);

/* Returns a stream that reads/writes from/to SOCK. */
svn_ra_svn__stream_t *svn_ra_svn__stream_from_sock(apr_socket_t *sock,
                                                   apr_pool_t *pool);

/* Returns a stream that reads from IN_STREAM and writes to OUT_STREAM,
   creating a timeout callback for OUT_STREAM if possible  */
svn_ra_svn__stream_t *svn_ra_svn__stream_from_streams(svn_stream_t *in_stream,
                                                      svn_stream_t *out_stream,
                                                      apr_pool_t *pool);

/* Create an svn_ra_svn__stream_t using READ_CB, WRITE_CB, TIMEOUT_CB,
 * PENDING_CB, and BATON.
 */
svn_ra_svn__stream_t *svn_ra_svn__stream_create(svn_stream_t *in_stream,
                                                svn_stream_t *out_stream,
                                                void *timeout_baton,
                                                ra_svn_timeout_fn_t timeout_cb,
                                                apr_pool_t *result_pool);

/* Write *LEN bytes from DATA to STREAM, returning the number of bytes
 * written in *LEN.
 */
svn_error_t *svn_ra_svn__stream_write(svn_ra_svn__stream_t *stream,
                                      const char *data, apr_size_t *len);

/* Read *LEN bytes from STREAM into DATA, returning the number of bytes
 * read in *LEN.
 */
svn_error_t *svn_ra_svn__stream_read(svn_ra_svn__stream_t *stream,
                                     char *data, apr_size_t *len);

/* Read the command word from CONN, return it in *COMMAND and skip to the
 * end of the command.  Allocate data in POOL.
 */
svn_error_t *svn_ra_svn__read_command_only(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           const char **command);

/* Set the timeout for operations on STREAM to INTERVAL. */
void svn_ra_svn__stream_timeout(svn_ra_svn__stream_t *stream,
                                apr_interval_time_t interval);

/* Return whether or not there is data pending on STREAM. */
svn_error_t *
svn_ra_svn__stream_data_available(svn_ra_svn__stream_t *stream,
                                  svn_boolean_t *data_available);

/* Respond to an auth request and perform authentication.  Use the Cyrus
 * SASL library for mechanism negotiation and for creating authentication
 * tokens. */
svn_error_t *
svn_ra_svn__do_cyrus_auth(svn_ra_svn__session_baton_t *sess,
                          const svn_ra_svn__list_t *mechlist,
                          const char *realm, apr_pool_t *pool);

/* Same as svn_ra_svn__do_cyrus_auth, but uses the built-in implementation of
 * the CRAM-MD5, ANONYMOUS and EXTERNAL mechanisms.  Return the error
 * SVN_ERR_RA_SVN_NO_MECHANSIMS if we cannot negotiate an authentication
 * mechanism with the server. */
svn_error_t *
svn_ra_svn__do_internal_auth(svn_ra_svn__session_baton_t *sess,
                             const svn_ra_svn__list_t *mechlist,
                             const char *realm, apr_pool_t *pool);

/* Having picked a mechanism, start authentication by writing out an
 * auth response.  MECH_ARG may be NULL for mechanisms with no
 * initial client response. */
svn_error_t *svn_ra_svn__auth_response(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       const char *mech, const char *mech_arg);

/* Looks for MECH as a word in MECHLIST. */
svn_boolean_t svn_ra_svn__find_mech(const svn_ra_svn__list_t *mechlist,
                                    const char *mech);

/* Initialize the SASL library. */
svn_error_t *svn_ra_svn__sasl_init(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_H */
