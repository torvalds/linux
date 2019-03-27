/*
 * streams.c :  stream encapsulation routines for the ra_svn protocol
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



#include <apr_general.h>
#include <apr_network_io.h>
#include <apr_poll.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_private_config.h"

#include "private/svn_io_private.h"

#include "ra_svn.h"

struct svn_ra_svn__stream_st {
  svn_stream_t *in_stream;
  svn_stream_t *out_stream;
  void *timeout_baton;
  ra_svn_timeout_fn_t timeout_fn;
};

typedef struct sock_baton_t {
  apr_socket_t *sock;
  apr_pool_t *pool;
} sock_baton_t;


/* Returns TRUE if PFD has pending data, FALSE otherwise. */
static svn_boolean_t pending(apr_pollfd_t *pfd, apr_pool_t *pool)
{
  apr_status_t status;
  int n;

  pfd->p = pool;
  pfd->reqevents = APR_POLLIN;
  status = apr_poll(pfd, 1, &n, 0);
  return (status == APR_SUCCESS && n);
}

/* Functions to implement a file backed svn_ra_svn__stream_t. */

/* Implements ra_svn_timeout_fn_t */
static void
file_timeout_cb(void *baton, apr_interval_time_t interval)
{
  apr_file_t *f = baton;

  if (f)
    apr_file_pipe_timeout_set(f, interval);
}

svn_ra_svn__stream_t *
svn_ra_svn__stream_from_streams(svn_stream_t *in_stream,
                                svn_stream_t *out_stream,
                                apr_pool_t *pool)
{
  apr_file_t *file;

  /* If out_stream is backed by an apr_file (e.g. an PIPE) we
     provide a working callback, otherwise the callback ignores
     the timeout.

     The callback is used to make the write non-blocking on
     some error scenarios. ### This (legacy) usage
     breaks the stream promise */
  file = svn_stream__aprfile(out_stream);

  return svn_ra_svn__stream_create(in_stream, out_stream,
                                   file, file_timeout_cb,
                                   pool);
}

/* Functions to implement a socket backed svn_ra_svn__stream_t. */

/* Implements svn_read_fn_t */
static svn_error_t *
sock_read_cb(void *baton, char *buffer, apr_size_t *len)
{
  sock_baton_t *b = baton;
  apr_status_t status;
  apr_interval_time_t interval;

  status = apr_socket_timeout_get(b->sock, &interval);
  if (status)
    return svn_error_wrap_apr(status, _("Can't get socket timeout"));

  /* Always block on read.
   * During pipelining, we set the timeout to 0 for some write
   * operations so that we can try them without blocking. If APR had
   * separate timeouts for read and write, we would only set the
   * write timeout, but it doesn't. So here, we revert back to blocking.
   */
  apr_socket_timeout_set(b->sock, -1);
  status = apr_socket_recv(b->sock, buffer, len);
  apr_socket_timeout_set(b->sock, interval);

  if (status && !APR_STATUS_IS_EOF(status))
    return svn_error_wrap_apr(status, _("Can't read from connection"));
  return SVN_NO_ERROR;
}

/* Implements svn_write_fn_t */
static svn_error_t *
sock_write_cb(void *baton, const char *buffer, apr_size_t *len)
{
  sock_baton_t *b = baton;
  apr_status_t status = apr_socket_send(b->sock, buffer, len);
  if (status)
    return svn_error_wrap_apr(status, _("Can't write to connection"));
  return SVN_NO_ERROR;
}

/* Implements ra_svn_timeout_fn_t */
static void
sock_timeout_cb(void *baton, apr_interval_time_t interval)
{
  sock_baton_t *b = baton;
  apr_socket_timeout_set(b->sock, interval);
}

/* Implements svn_stream_data_available_fn_t */
static svn_error_t *
sock_pending_cb(void *baton,
                svn_boolean_t *data_available)
{
  sock_baton_t *b = baton;
  apr_pollfd_t pfd;

  pfd.desc_type = APR_POLL_SOCKET;
  pfd.desc.s = b->sock;

  *data_available = pending(&pfd, b->pool);

  svn_pool_clear(b->pool);

  return SVN_NO_ERROR;
}

svn_ra_svn__stream_t *
svn_ra_svn__stream_from_sock(apr_socket_t *sock,
                             apr_pool_t *result_pool)
{
  sock_baton_t *b = apr_palloc(result_pool, sizeof(*b));
  svn_stream_t *sock_stream;

  b->sock = sock;
  b->pool = svn_pool_create(result_pool);

  sock_stream = svn_stream_create(b, result_pool);

  svn_stream_set_read2(sock_stream, sock_read_cb, NULL /* use default */);
  svn_stream_set_write(sock_stream, sock_write_cb);
  svn_stream_set_data_available(sock_stream, sock_pending_cb);

  return svn_ra_svn__stream_create(sock_stream, sock_stream,
                                   b, sock_timeout_cb, result_pool);
}

svn_ra_svn__stream_t *
svn_ra_svn__stream_create(svn_stream_t *in_stream,
                          svn_stream_t *out_stream,
                          void *timeout_baton,
                          ra_svn_timeout_fn_t timeout_cb,
                          apr_pool_t *pool)
{
  svn_ra_svn__stream_t *s = apr_palloc(pool, sizeof(*s));
  s->in_stream = in_stream;
  s->out_stream = out_stream;
  s->timeout_baton = timeout_baton;
  s->timeout_fn = timeout_cb;
  return s;
}

svn_error_t *
svn_ra_svn__stream_write(svn_ra_svn__stream_t *stream,
                         const char *data, apr_size_t *len)
{
  return svn_error_trace(svn_stream_write(stream->out_stream, data, len));
}

svn_error_t *
svn_ra_svn__stream_read(svn_ra_svn__stream_t *stream, char *data,
                        apr_size_t *len)
{
  SVN_ERR(svn_stream_read2(stream->in_stream, data, len));

  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL, NULL);

  return SVN_NO_ERROR;
}

void
svn_ra_svn__stream_timeout(svn_ra_svn__stream_t *stream,
                           apr_interval_time_t interval)
{
  stream->timeout_fn(stream->timeout_baton, interval);
}

svn_error_t *
svn_ra_svn__stream_data_available(svn_ra_svn__stream_t *stream,
                                  svn_boolean_t *data_available)
{
  return svn_error_trace(
          svn_stream_data_available(stream->in_stream,
                                    data_available));
}
