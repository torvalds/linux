/*
 * marshal.c :  Marshalling routines for Subversion protocol
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
#include <stdlib.h>

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_lib.h>
#include <apr_strings.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra_svn.h"
#include "svn_private_config.h"
#include "svn_ctype.h"
#include "svn_sorts.h"
#include "svn_time.h"

#include "ra_svn.h"

#include "private/svn_string_private.h"
#include "private/svn_dep_compat.h"
#include "private/svn_error_private.h"
#include "private/svn_subr_private.h"

#define svn_iswhitespace(c) ((c) == ' ' || (c) == '\n')

/* If we receive data that *claims* to be followed by a very long string,
 * we should not trust that claim right away. But everything up to 1 MB
 * should be too small to be instrumental for a DOS attack. */

#define SUSPICIOUSLY_HUGE_STRING_SIZE_THRESHOLD (0x100000)

/* We don't use "words" longer than this in our protocol.  The longest word
 * we are currently using is only about 16 chars long but we leave room for
 * longer future capability and command names.  See read_item() to understand
 * why MAX_WORD_LENGTH - 1 should be a multiple of 8.
 */
#define MAX_WORD_LENGTH 25

/* The generic parsers will use the following value to limit the recursion
 * depth to some reasonable value.  The current protocol implementation
 * actually uses only maximum item nesting level of around 5.  So, there is
 * plenty of headroom here.
 */
#define ITEM_NESTING_LIMIT 64

/* The protocol words for booleans. */
static const svn_string_t str_true = SVN__STATIC_STRING("true");
static const svn_string_t str_false = SVN__STATIC_STRING("false");

/* Return the APR socket timeout to be used for the connection depending
 * on whether there is a blockage handler or zero copy has been activated. */
static apr_interval_time_t
get_timeout(svn_ra_svn_conn_t *conn)
{
  return conn->block_handler ? 0 : -1;
}

/* --- Public / private API data conversion --- */

void
svn_ra_svn__to_public_item(svn_ra_svn_item_t *target,
                           const svn_ra_svn__item_t *source,
                           apr_pool_t *result_pool)
{
  target->kind = source->kind;
  switch (source->kind)
    {
      case SVN_RA_SVN_STRING:
        target->u.string = svn_string_dup(&source->u.string, result_pool);
        break;
      case SVN_RA_SVN_NUMBER:
        target->u.number = source->u.number;
        break;
      case SVN_RA_SVN_WORD:
        target->u.word = source->u.word.data;
        break;
      case SVN_RA_SVN_LIST:
        target->u.list = svn_ra_svn__to_public_array(&source->u.list,
                                                     result_pool);
        break;
    }
}

apr_array_header_t *
svn_ra_svn__to_public_array(const svn_ra_svn__list_t *source,
                            apr_pool_t *result_pool)
{
  apr_array_header_t *result = apr_array_make(result_pool, source->nelts,
                                              sizeof(svn_ra_svn_item_t));

  int i;
  for (i = 0; i < source->nelts; ++i)
    {
      svn_ra_svn_item_t *sub_target = apr_array_push(result);
      svn_ra_svn__item_t *sub_source = &SVN_RA_SVN__LIST_ITEM(source, i);

      svn_ra_svn__to_public_item(sub_target, sub_source, result_pool);
    }

  return result;
}

void
svn_ra_svn__to_private_item(svn_ra_svn__item_t *target,
                            const svn_ra_svn_item_t *source,
                            apr_pool_t *result_pool)
{
  target->kind = source->kind;
  switch (source->kind)
    {
      case SVN_RA_SVN_STRING:
        target->u.string = *source->u.string;
        break;
      case SVN_RA_SVN_NUMBER:
        target->u.number = source->u.number;
        break;
      case SVN_RA_SVN_WORD:
        target->u.word.data = source->u.word;
        target->u.word.len = strlen(source->u.word);
        break;
      case SVN_RA_SVN_LIST:
        target->u.list = *svn_ra_svn__to_private_array(source->u.list,
                                                       result_pool);
        break;
    }
}

svn_ra_svn__list_t *
svn_ra_svn__to_private_array(const apr_array_header_t *source,
                             apr_pool_t *result_pool)
{
  int i;

  svn_ra_svn__list_t *result = apr_pcalloc(result_pool, sizeof(*result));
  result->nelts = source->nelts;
  result->items = apr_palloc(result_pool,
                             source->nelts * sizeof(*result->items));

  for (i = 0; i < source->nelts; ++i)
    {
      svn_ra_svn__item_t *sub_target = &result->items[i];
      svn_ra_svn_item_t *sub_source = &APR_ARRAY_IDX(source, i,
                                                     svn_ra_svn_item_t);

      svn_ra_svn__to_private_item(sub_target, sub_source, result_pool);
    }

  return result;
}

/* --- CONNECTION INITIALIZATION --- */

svn_ra_svn_conn_t *svn_ra_svn_create_conn5(apr_socket_t *sock,
                                           svn_stream_t *in_stream,
                                           svn_stream_t *out_stream,
                                           int compression_level,
                                           apr_size_t zero_copy_limit,
                                           apr_size_t error_check_interval,
                                           apr_uint64_t max_in,
                                           apr_uint64_t max_out,
                                           apr_pool_t *result_pool)
{
  svn_ra_svn_conn_t *conn;
  void *mem = apr_palloc(result_pool, sizeof(*conn) + SVN_RA_SVN__PAGE_SIZE);
  conn = (void*)APR_ALIGN((apr_uintptr_t)mem, SVN_RA_SVN__PAGE_SIZE);

  assert((sock && !in_stream && !out_stream)
         || (!sock && in_stream && out_stream));
#ifdef SVN_HAVE_SASL
  conn->sock = sock;
  conn->encrypted = FALSE;
#endif
  conn->session = NULL;
  conn->read_ptr = conn->read_buf;
  conn->read_end = conn->read_buf;
  conn->write_pos = 0;
  conn->written_since_error_check = 0;
  conn->error_check_interval = error_check_interval;
  conn->may_check_for_error = error_check_interval == 0;
  conn->max_in = max_in;
  conn->current_in = 0;
  conn->max_out = max_out;
  conn->current_out = 0;
  conn->block_handler = NULL;
  conn->block_baton = NULL;
  conn->capabilities = apr_hash_make(result_pool);
  conn->compression_level = compression_level;
  conn->zero_copy_limit = zero_copy_limit;
  conn->pool = result_pool;

  if (sock != NULL)
    {
      apr_sockaddr_t *sa;
      conn->stream = svn_ra_svn__stream_from_sock(sock, result_pool);
      if (!(apr_socket_addr_get(&sa, APR_REMOTE, sock) == APR_SUCCESS
            && apr_sockaddr_ip_get(&conn->remote_ip, sa) == APR_SUCCESS))
        conn->remote_ip = NULL;
      svn_ra_svn__stream_timeout(conn->stream, get_timeout(conn));
    }
  else
    {
      conn->stream = svn_ra_svn__stream_from_streams(in_stream, out_stream,
                                                     result_pool);
      conn->remote_ip = NULL;
    }

  return conn;
}

svn_error_t *
svn_ra_svn_set_capabilities(svn_ra_svn_conn_t *conn,
                            const apr_array_header_t *list)
{
  svn_ra_svn__list_t *internal
    = svn_ra_svn__to_private_array(list, list->pool);
  return svn_error_trace(svn_ra_svn__set_capabilities(conn, internal));
}

svn_error_t *
svn_ra_svn__set_capabilities(svn_ra_svn_conn_t *conn,
                             const svn_ra_svn__list_t *list)
{
  int i;
  svn_ra_svn__item_t *item;
  const char *word;

  for (i = 0; i < list->nelts; i++)
    {
      item = &SVN_RA_SVN__LIST_ITEM(list, i);
      if (item->kind != SVN_RA_SVN_WORD)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Capability entry is not a word"));
      word = apr_pstrmemdup(conn->pool, item->u.word.data, item->u.word.len);
      apr_hash_set(conn->capabilities, word, item->u.word.len, word);
    }
  return SVN_NO_ERROR;
}

int
svn_ra_svn__svndiff_version(svn_ra_svn_conn_t *conn)
{
  /* If we don't want to use compression, use the non-compressing
   * "version 0" implementation. */
  if (svn_ra_svn_compression_level(conn) <= 0)
    return 0;

  /* Prefer SVNDIFF2 over SVNDIFF1. */
  if (svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_SVNDIFF2_ACCEPTED))
    return 2;
  if (svn_ra_svn_has_capability(conn, SVN_RA_SVN_CAP_SVNDIFF1))
    return 1;

  /* The connection does not support SVNDIFF1/2; default to "version 0". */
  return 0;
}

apr_pool_t *
svn_ra_svn__get_pool(svn_ra_svn_conn_t *conn)
{
  return conn->pool;
}

svn_error_t *
svn_ra_svn__set_shim_callbacks(svn_ra_svn_conn_t *conn,
                               svn_delta_shim_callbacks_t *shim_callbacks)
{
  conn->shim_callbacks = shim_callbacks;
  return SVN_NO_ERROR;
}

svn_boolean_t svn_ra_svn_has_capability(svn_ra_svn_conn_t *conn,
                                        const char *capability)
{
  return (svn_hash_gets(conn->capabilities, capability) != NULL);
}

int
svn_ra_svn_compression_level(svn_ra_svn_conn_t *conn)
{
  return conn->compression_level;
}

apr_size_t
svn_ra_svn_zero_copy_limit(svn_ra_svn_conn_t *conn)
{
  return conn->zero_copy_limit;
}

const char *svn_ra_svn_conn_remote_host(svn_ra_svn_conn_t *conn)
{
  return conn->remote_ip;
}

void
svn_ra_svn__set_block_handler(svn_ra_svn_conn_t *conn,
                              ra_svn_block_handler_t handler,
                              void *baton)
{
  conn->block_handler = handler;
  conn->block_baton = baton;
  svn_ra_svn__stream_timeout(conn->stream, get_timeout(conn));
}

svn_error_t *svn_ra_svn__data_available(svn_ra_svn_conn_t *conn,
                                       svn_boolean_t *data_available)
{
  return svn_ra_svn__stream_data_available(conn->stream, data_available);
}

void
svn_ra_svn__reset_command_io_counters(svn_ra_svn_conn_t *conn)
{
  conn->current_in = 0;
  conn->current_out = 0;
}


/* --- WRITE BUFFER MANAGEMENT --- */

/* Return an error object if CONN exceeded its send or receive limits. */
static svn_error_t *
check_io_limits(svn_ra_svn_conn_t *conn)
{
  if (conn->max_in && (conn->current_in > conn->max_in))
    return svn_error_create(SVN_ERR_RA_SVN_REQUEST_SIZE, NULL,
                            "The client request size exceeds the "
                            "configured limit");

  if (conn->max_out && (conn->current_out > conn->max_out))
    return svn_error_create(SVN_ERR_RA_SVN_RESPONSE_SIZE, NULL,
                            "The server response size exceeds the "
                            "configured limit");

  return SVN_NO_ERROR;
}

/* Write data to socket or output file as appropriate. */
static svn_error_t *writebuf_output(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                    const char *data, apr_size_t len)
{
  const char *end = data + len;
  apr_size_t count;
  apr_pool_t *subpool = NULL;
  svn_ra_svn__session_baton_t *session = conn->session;

  /* Limit the size of the response, if a limit has been configured.
   * This is to limit the server load in case users e.g. accidentally ran
   * an export on the root folder. */
  conn->current_out += len;
  SVN_ERR(check_io_limits(conn));

  while (data < end)
    {
      count = end - data;

      if (session && session->callbacks && session->callbacks->cancel_func)
        SVN_ERR((session->callbacks->cancel_func)(session->callbacks_baton));

      SVN_ERR(svn_ra_svn__stream_write(conn->stream, data, &count));
      if (count == 0)
        {
          if (!subpool)
            subpool = svn_pool_create(pool);
          else
            svn_pool_clear(subpool);
          SVN_ERR(conn->block_handler(conn, subpool, conn->block_baton));
        }
      data += count;

      if (session)
        {
          const svn_ra_callbacks2_t *cb = session->callbacks;
          session->bytes_written += count;

          if (cb && cb->progress_func)
            (cb->progress_func)(session->bytes_written + session->bytes_read,
                                -1, cb->progress_baton, subpool);
        }
    }

  conn->written_since_error_check += len;
  conn->may_check_for_error
    = conn->written_since_error_check >= conn->error_check_interval;

  if (subpool)
    svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Write data from the write buffer out to the socket. */
static svn_error_t *writebuf_flush(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  apr_size_t write_pos = conn->write_pos;

  /* Clear conn->write_pos first in case the block handler does a read. */
  conn->write_pos = 0;
  SVN_ERR(writebuf_output(conn, pool, conn->write_buf, write_pos));
  return SVN_NO_ERROR;
}

static svn_error_t *writebuf_write(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                   const char *data, apr_size_t len)
{
  /* data >= 8k is sent immediately */
  if (len >= sizeof(conn->write_buf) / 2)
    {
      if (conn->write_pos > 0)
        SVN_ERR(writebuf_flush(conn, pool));

      return writebuf_output(conn, pool, data, len);
    }

  /* ensure room for the data to add */
  if (conn->write_pos + len > sizeof(conn->write_buf))
    SVN_ERR(writebuf_flush(conn, pool));

  /* buffer the new data block as well */
  memcpy(conn->write_buf + conn->write_pos, data, len);
  conn->write_pos += len;

  return SVN_NO_ERROR;
}

/* Write STRING_LITERAL, which is a string literal argument.

   Note: The purpose of the empty string "" in the macro definition is to
   assert that STRING_LITERAL is in fact a string literal. Otherwise, the
   string concatenation attempt should produce a compile-time error. */
#define writebuf_write_literal(conn, pool, string_literal) \
    writebuf_write(conn, pool, string_literal, sizeof(string_literal "") - 1)

static APR_INLINE svn_error_t *
writebuf_writechar(svn_ra_svn_conn_t *conn, apr_pool_t *pool, char data)
{
  if (conn->write_pos < sizeof(conn->write_buf))
  {
    conn->write_buf[conn->write_pos] = data;
    conn->write_pos++;

    return SVN_NO_ERROR;
  }
  else
  {
    char temp = data;
    return writebuf_write(conn, pool, &temp, 1);
  }
}

/* --- READ BUFFER MANAGEMENT --- */

/* Read bytes into DATA until either the read buffer is empty or
 * we reach END. */
static char *readbuf_drain(svn_ra_svn_conn_t *conn, char *data, char *end)
{
  apr_ssize_t buflen, copylen;

  buflen = conn->read_end - conn->read_ptr;
  copylen = (buflen < end - data) ? buflen : end - data;
  memcpy(data, conn->read_ptr, copylen);
  conn->read_ptr += copylen;
  return data + copylen;
}

/* Read data from socket or input file as appropriate. */
static svn_error_t *readbuf_input(svn_ra_svn_conn_t *conn, char *data,
                                  apr_size_t *len, apr_pool_t *pool)
{
  svn_ra_svn__session_baton_t *session = conn->session;

  /* First, give the user a chance to cancel the request before we do. */
  if (session && session->callbacks && session->callbacks->cancel_func)
    SVN_ERR((session->callbacks->cancel_func)(session->callbacks_baton));

  /* Limit our memory usage, if a limit has been configured.  Note that
   * we first read the whole request into memory before process it. */
  SVN_ERR(check_io_limits(conn));

  /* Actually fill the buffer. */
  SVN_ERR(svn_ra_svn__stream_read(conn->stream, data, len));
  if (*len == 0)
    return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL, NULL);
  conn->current_in += *len;

  if (session)
    {
      const svn_ra_callbacks2_t *cb = session->callbacks;
      session->bytes_read += *len;

      if (cb && cb->progress_func)
        (cb->progress_func)(session->bytes_read + session->bytes_written,
                            -1, cb->progress_baton, pool);
    }

  return SVN_NO_ERROR;
}

/* Treat the next LEN input bytes from CONN as "read" */
static svn_error_t *readbuf_skip(svn_ra_svn_conn_t *conn, apr_uint64_t len)
{
  do
  {
    apr_size_t buflen = conn->read_end - conn->read_ptr;
    apr_size_t copylen = (buflen < len) ? buflen : (apr_size_t)len;
    conn->read_ptr += copylen;
    len -= copylen;
    if (len == 0)
      break;

    buflen = sizeof(conn->read_buf);
    SVN_ERR(svn_ra_svn__stream_read(conn->stream, conn->read_buf, &buflen));
    if (buflen == 0)
      return svn_error_create(SVN_ERR_RA_SVN_CONNECTION_CLOSED, NULL, NULL);

    conn->read_end = conn->read_buf + buflen;
    conn->read_ptr = conn->read_buf;
  }
  while (len > 0);

  return SVN_NO_ERROR;
}

/* Read data from the socket into the read buffer, which must be empty. */
static svn_error_t *readbuf_fill(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  apr_size_t len;

  SVN_ERR_ASSERT(conn->read_ptr == conn->read_end);

  /* Make sure we tell the other side everything we have to say before
   * reading / waiting for an answer. */
  if (conn->write_pos)
    SVN_ERR(writebuf_flush(conn, pool));

  /* Fill (some of the) buffer. */
  len = sizeof(conn->read_buf);
  SVN_ERR(readbuf_input(conn, conn->read_buf, &len, pool));
  conn->read_ptr = conn->read_buf;
  conn->read_end = conn->read_buf + len;
  return SVN_NO_ERROR;
}

/* This is a hot function calling a cold function.  GCC and others tend to
 * inline the cold sub-function instead of this hot one.  Therefore, be
 * very insistent on lining this one.  It is not a correctness issue, though.
 */
static SVN__FORCE_INLINE svn_error_t *
readbuf_getchar(svn_ra_svn_conn_t *conn, apr_pool_t *pool, char *result)
{
  if (conn->read_ptr == conn->read_end)
    SVN_ERR(readbuf_fill(conn, pool));
  *result = *conn->read_ptr++;
  return SVN_NO_ERROR;
}

static svn_error_t *readbuf_getchar_skip_whitespace(svn_ra_svn_conn_t *conn,
                                                    apr_pool_t *pool,
                                                    char *result)
{
  do
    SVN_ERR(readbuf_getchar(conn, pool, result));
  while (svn_iswhitespace(*result));
  return SVN_NO_ERROR;
}

/* Read the next LEN bytes from CONN and copy them to *DATA. */
static svn_error_t *readbuf_read(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 char *data, apr_size_t len)
{
  char *end = data + len;
  apr_size_t count;

  /* Copy in an appropriate amount of data from the buffer. */
  data = readbuf_drain(conn, data, end);

  /* Read large chunks directly into buffer. */
  while (end - data > (apr_ssize_t)sizeof(conn->read_buf))
    {
      SVN_ERR(writebuf_flush(conn, pool));
      count = end - data;
      SVN_ERR(readbuf_input(conn, data, &count, pool));
      data += count;
    }

  while (end > data)
    {
      /* The remaining amount to read is small; fill the buffer and
       * copy from that. */
      SVN_ERR(readbuf_fill(conn, pool));
      data = readbuf_drain(conn, data, end);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *readbuf_skip_leading_garbage(svn_ra_svn_conn_t *conn,
                                                 apr_pool_t *pool)
{
  char buf[256];  /* Must be smaller than sizeof(conn->read_buf) - 1. */
  const char *p, *end;
  apr_size_t len;
  svn_boolean_t lparen = FALSE;

  SVN_ERR_ASSERT(conn->read_ptr == conn->read_end);
  while (1)
    {
      /* Read some data directly from the connection input source. */
      len = sizeof(buf);
      SVN_ERR(readbuf_input(conn, buf, &len, pool));
      end = buf + len;

      /* Scan the data for '(' WS with a very simple state machine. */
      for (p = buf; p < end; p++)
        {
          if (lparen && svn_iswhitespace(*p))
            break;
          else
            lparen = (*p == '(');
        }
      if (p < end)
        break;
    }

  /* p now points to the whitespace just after the left paren.  Fake
   * up the left paren and then copy what we have into the read
   * buffer. */
  conn->read_buf[0] = '(';
  memcpy(conn->read_buf + 1, p, end - p);
  conn->read_ptr = conn->read_buf;
  conn->read_end = conn->read_buf + 1 + (end - p);
  return SVN_NO_ERROR;
}

/* --- WRITING DATA ITEMS --- */

static svn_error_t *write_number(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 apr_uint64_t number, char follow)
{
  apr_size_t written;

  /* SVN_INT64_BUFFER_SIZE includes space for a terminating NUL that
   * svn__ui64toa will always append. */
  if (conn->write_pos + SVN_INT64_BUFFER_SIZE >= sizeof(conn->write_buf))
    SVN_ERR(writebuf_flush(conn, pool));

  written = svn__ui64toa(conn->write_buf + conn->write_pos, number);
  conn->write_buf[conn->write_pos + written] = follow;
  conn->write_pos += written + 1;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_number(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         apr_uint64_t number)
{
  return write_number(conn, pool, number, ' ');
}

/* Write string S of length LEN to TARGET and return the first position
   after the written data.

   NOTE: This function assumes that TARGET has enough room for S, the LEN
         prefix and the required separators.  The available buffer size
         should be SVN_INT64_BUFFER_SIZE + LEN + 1 to avoid any chance of
         overflow.
 */
static char *
write_ncstring_quick(char *target,
                     const char *s,
                     apr_size_t len)
{
  /* Write string length. */
  if (len < 10)
    {
      *target = (char)(len + '0');
      target++;
    }
  else
    {
      target += svn__ui64toa(target, len);
    }

  /* Separator & contents. */
  target[0] = ':';
  memcpy(target + 1, s, len);
  target[len + 1] = ' ';

  /* First location after the string. */
  return target + len + 2;
}


static svn_error_t *
svn_ra_svn__write_ncstring(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const char *s,
                           apr_size_t len)
{
  /* Apart from LEN bytes of string contents, we need room for a number,
     a colon and a space. */
  apr_size_t max_fill = sizeof(conn->write_buf) - SVN_INT64_BUFFER_SIZE - 2;

  /* In most cases, there is enough left room in the WRITE_BUF
     the we can serialize directly into it.  On platforms with
     segmented memory, LEN might actually be close to APR_SIZE_MAX.
     Blindly doing arithmetic on it might cause an overflow. */
  if ((len <= max_fill) && (conn->write_pos <= max_fill - len))
    {
      /* Quick path. */
      conn->write_pos = write_ncstring_quick(conn->write_buf
                                               + conn->write_pos, s, len)
                      - conn->write_buf;
    }
  else
    {
      /* Slower fallback code. */
      SVN_ERR(write_number(conn, pool, len, ':'));

      SVN_ERR(writebuf_write(conn, pool, s, len));
      SVN_ERR(writebuf_writechar(conn, pool, ' '));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_string(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const svn_string_t *str)
{
  SVN_ERR(svn_ra_svn__write_ncstring(conn, pool, str->data, str->len));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cstring(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          const char *s)
{
  SVN_ERR(svn_ra_svn__write_ncstring(conn, pool, s, strlen(s)));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_word(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *word)
{
  SVN_ERR(writebuf_write(conn, pool, word, strlen(word)));
  SVN_ERR(writebuf_writechar(conn, pool, ' '));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_boolean(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          svn_boolean_t value)
{
  if (value)
    SVN_ERR(writebuf_write_literal(conn, pool, "true "));
  else
    SVN_ERR(writebuf_write_literal(conn, pool, "false "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_proplist(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           apr_hash_t *props)
{
  apr_hash_index_t *hi;
  const char *propname;
  svn_string_t *propval;
  apr_size_t len;

  /* One might use an iterpool here but that would only be used when the
     send buffer gets flushed and only by the CONN's progress callback.
     That should happen at most once for typical prop lists and even then
     use only a few bytes at best.
   */
  if (props)
    for (hi = apr_hash_first(pool, props); hi; hi = apr_hash_next(hi))
      {
        apr_hash_this(hi, (const void **)&propname,
                          (apr_ssize_t *)&len,
                          (void **)&propval);

        SVN_ERR(svn_ra_svn__start_list(conn, pool));
        SVN_ERR(svn_ra_svn__write_ncstring(conn, pool, propname, len));
        SVN_ERR(svn_ra_svn__write_string(conn, pool, propval));
        SVN_ERR(svn_ra_svn__end_list(conn, pool));
      }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__start_list(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool)
{
  if (conn->write_pos + 2 <= sizeof(conn->write_buf))
    {
      conn->write_buf[conn->write_pos] = '(';
      conn->write_buf[conn->write_pos+1] = ' ';
      conn->write_pos += 2;
      return SVN_NO_ERROR;
    }

  return writebuf_write(conn, pool, "( ", 2);
}

svn_error_t *
svn_ra_svn__end_list(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool)
{
  if (conn->write_pos + 2 <= sizeof(conn->write_buf))
  {
    conn->write_buf[conn->write_pos] = ')';
    conn->write_buf[conn->write_pos+1] = ' ';
    conn->write_pos += 2;
    return SVN_NO_ERROR;
  }

  return writebuf_write(conn, pool, ") ", 2);
}

svn_error_t *
svn_ra_svn__flush(svn_ra_svn_conn_t *conn,
                  apr_pool_t *pool)
{
  SVN_ERR(writebuf_flush(conn, pool));
  conn->may_check_for_error = TRUE;

  return SVN_NO_ERROR;
}

/* --- WRITING TUPLES --- */

static svn_error_t *
vwrite_tuple_cstring(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const char *cstr = va_arg(*ap, const char *);
  SVN_ERR_ASSERT(cstr);
  return svn_ra_svn__write_cstring(conn, pool, cstr);
}

static svn_error_t *
vwrite_tuple_cstring_opt(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const char *cstr = va_arg(*ap, const char *);
  return cstr ? svn_ra_svn__write_cstring(conn, pool, cstr) : SVN_NO_ERROR;
}

static svn_error_t *
vwrite_tuple_string(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const svn_string_t *str = va_arg(*ap, const svn_string_t *);
  SVN_ERR_ASSERT(str);
  return svn_ra_svn__write_string(conn, pool, str);
}

static svn_error_t *
vwrite_tuple_string_opt(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const svn_string_t *str = va_arg(*ap, const svn_string_t *);
  return str ? svn_ra_svn__write_string(conn, pool, str) : SVN_NO_ERROR;
}

static svn_error_t *
vwrite_tuple_word(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const char *cstr = va_arg(*ap, const char *);
  SVN_ERR_ASSERT(cstr);
  return svn_ra_svn__write_word(conn, pool, cstr);
}

static svn_error_t *
vwrite_tuple_word_opt(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  const char *cstr = va_arg(*ap, const char *);
  return cstr ? svn_ra_svn__write_word(conn, pool, cstr) : SVN_NO_ERROR;
}

static svn_error_t *
vwrite_tuple_revision(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  svn_revnum_t rev = va_arg(*ap, svn_revnum_t);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev));
  return svn_ra_svn__write_number(conn, pool, rev);
}

static svn_error_t *
vwrite_tuple_revision_opt(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  svn_revnum_t rev = va_arg(*ap, svn_revnum_t);
  return SVN_IS_VALID_REVNUM(rev)
       ? svn_ra_svn__write_number(conn, pool, rev)
       : SVN_NO_ERROR;
}

static svn_error_t *
vwrite_tuple_number(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  return svn_ra_svn__write_number(conn, pool, va_arg(*ap, apr_uint64_t));
}

static svn_error_t *
vwrite_tuple_boolean(svn_ra_svn_conn_t *conn, apr_pool_t *pool, va_list *ap)
{
  return svn_ra_svn__write_boolean(conn, pool, va_arg(*ap, svn_boolean_t));
}

static svn_error_t *
write_tuple_cstring(svn_ra_svn_conn_t *conn,
                    apr_pool_t *pool,
                    const char *cstr)
{
  SVN_ERR_ASSERT(cstr);
  return svn_ra_svn__write_cstring(conn, pool, cstr);
}

static svn_error_t *
write_tuple_cstring_opt(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const char *cstr)
{
  return cstr ? svn_ra_svn__write_cstring(conn, pool, cstr) : SVN_NO_ERROR;
}

static svn_error_t *
write_tuple_string(svn_ra_svn_conn_t *conn,
                   apr_pool_t *pool,
                   const svn_string_t *str)
{
  SVN_ERR_ASSERT(str);
  return svn_ra_svn__write_string(conn, pool, str);
}

static svn_error_t *
write_tuple_string_opt(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const svn_string_t *str)
{
  return str ? svn_ra_svn__write_string(conn, pool, str) : SVN_NO_ERROR;
}

/* Optimized sending code for the "(s?)" pattern. */
static svn_error_t *
write_tuple_string_opt_list(svn_ra_svn_conn_t *conn,
                            apr_pool_t *pool,
                            const svn_string_t *str)
{
  apr_size_t max_fill;

  /* Special case. */
  if (!str)
    return writebuf_write(conn, pool, "( ) ", 4);

  /* If this how far we can fill the WRITE_BUF with string data and still
     guarantee that the length info will fit in as well. */
  max_fill = sizeof(conn->write_buf)
           - 2                       /* open list */
           - SVN_INT64_BUFFER_SIZE   /* string length + separator */
           - 2;                      /* close list */

   /* On platforms with segmented memory, STR->LEN might actually be
      close to APR_SIZE_MAX.  Blindly doing arithmetic on it might
      cause an overflow. */
  if ((str->len <= max_fill) && (conn->write_pos <= max_fill - str->len))
    {
      /* Quick path. */
      /* Open list. */
      char *p = conn->write_buf + conn->write_pos;
      p[0] = '(';
      p[1] = ' ';

      /* Write string. */
      p = write_ncstring_quick(p + 2, str->data, str->len);

      /* Close list. */
      p[0] = ')';
      p[1] = ' ';
      conn->write_pos = p + 2 - conn->write_buf;
    }
  else
    {
      /* Standard code path (fallback). */
      SVN_ERR(svn_ra_svn__start_list(conn, pool));
      SVN_ERR(svn_ra_svn__write_string(conn, pool, str));
      SVN_ERR(svn_ra_svn__end_list(conn, pool));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
write_tuple_start_list(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool)
{
  return svn_ra_svn__start_list(conn, pool);
}

static svn_error_t *
write_tuple_end_list(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool)
{
  return svn_ra_svn__end_list(conn, pool);
}

static svn_error_t *
write_tuple_revision(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool,
                     svn_revnum_t rev)
{
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev));
  return svn_ra_svn__write_number(conn, pool, rev);
}

static svn_error_t *
write_tuple_revision_opt(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         svn_revnum_t rev)
{
  return SVN_IS_VALID_REVNUM(rev)
       ? svn_ra_svn__write_number(conn, pool, rev)
       : SVN_NO_ERROR;
}

static svn_error_t *
write_tuple_boolean(svn_ra_svn_conn_t *conn,
                    apr_pool_t *pool,
                    svn_boolean_t value)
{
  return svn_ra_svn__write_boolean(conn, pool, value);
}

static svn_error_t *
write_tuple_depth(svn_ra_svn_conn_t *conn,
                  apr_pool_t *pool,
                  svn_depth_t depth)
{
  return svn_ra_svn__write_word(conn, pool, svn_depth_to_word(depth));
}


static svn_error_t *
write_cmd_add_node(svn_ra_svn_conn_t *conn,
                   apr_pool_t *pool,
                   const char *path,
                   const svn_string_t *parent_token,
                   const svn_string_t *token,
                   const char *copy_path,
                   svn_revnum_t copy_rev)
{
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_string(conn, pool, parent_token));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool, copy_path));
  SVN_ERR(write_tuple_revision_opt(conn, pool, copy_rev));
  SVN_ERR(write_tuple_end_list(conn, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_cmd_open_node(svn_ra_svn_conn_t *conn,
                    apr_pool_t *pool,
                    const char *path,
                    const svn_string_t *parent_token,
                    const svn_string_t *token,
                    svn_revnum_t rev)
{
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_string(conn, pool, parent_token));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_cmd_change_node_prop(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const svn_string_t *token,
                           const char *name,
                           const svn_string_t *value)
{
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_cstring(conn, pool, name));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, value));

  return SVN_NO_ERROR;
}

static svn_error_t *
write_cmd_absent_node(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const char *path,
                      const svn_string_t *token)
{
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_string(conn, pool, token));

  return SVN_NO_ERROR;
}




static svn_error_t *vwrite_tuple(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                 const char *fmt, va_list *ap)
{
  svn_boolean_t opt = FALSE;

  if (*fmt == '!')
    fmt++;
  else
    SVN_ERR(svn_ra_svn__start_list(conn, pool));
  for (; *fmt; fmt++)
    {
      if (*fmt == 'c')
        SVN_ERR(opt ? vwrite_tuple_cstring_opt(conn, pool, ap)
                    : vwrite_tuple_cstring(conn, pool, ap));
      else if (*fmt == 's')
        SVN_ERR(opt ? vwrite_tuple_string_opt(conn, pool, ap)
                    : vwrite_tuple_string(conn, pool, ap));
      else if (*fmt == '(' && !opt)
        {
          /* Optional sub-tuples are not supported.
           * If OPT was set, we would fall through to the malfunction call. */
          SVN_ERR(write_tuple_start_list(conn, pool));
        }
      else if (*fmt == ')')
        {
          SVN_ERR(write_tuple_end_list(conn, pool));

          /* OPT could not have been set when opening the list (see above),
           * hence this is correct and handles nested tuples just fine. */
          opt = FALSE;
        }
      else if (*fmt == '?')
        opt = TRUE;
      else if (*fmt == 'w')
        SVN_ERR(opt ? vwrite_tuple_word_opt(conn, pool, ap)
                    : vwrite_tuple_word(conn, pool, ap));
      else if (*fmt == 'r')
        SVN_ERR(opt ? vwrite_tuple_revision_opt(conn, pool, ap)
                    : vwrite_tuple_revision(conn, pool, ap));
      else if (*fmt == 'n' && !opt)
        SVN_ERR(vwrite_tuple_number(conn, pool, ap));
      else if (*fmt == 'b' && !opt)
        SVN_ERR(vwrite_tuple_boolean(conn, pool, ap));
      else if (*fmt == '!' && !*(fmt + 1))
        return SVN_NO_ERROR;
      else
        SVN_ERR_MALFUNCTION();
    }
  SVN_ERR(svn_ra_svn__end_list(conn, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_tuple(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vwrite_tuple(conn, pool, fmt, &ap);
  va_end(ap);
  return err;
}

/* --- READING DATA ITEMS --- */

/* Read LEN bytes from CONN into already-allocated structure ITEM.
 * Afterwards, *ITEM is of type 'SVN_RA_SVN_STRING', and its string
 * data is allocated in POOL. */
static svn_error_t *read_string(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                svn_ra_svn__item_t *item, apr_uint64_t len64)
{
  apr_size_t len = (apr_size_t)len64;
  apr_size_t readbuf_len;
  char *dest;
  apr_size_t buflen;

  /* We can't store strings longer than the maximum size of apr_size_t,
   * so check before using the truncated value. */
  if (len64 > APR_SIZE_MAX)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("String length larger than maximum"));

  buflen = conn->read_end - conn->read_ptr;
  /* Shorter strings can be copied directly from the read buffer. */
  if (len <= buflen)
    {
      item->kind = SVN_RA_SVN_STRING;
      item->u.string.data = apr_pstrmemdup(pool, conn->read_ptr, len);
      item->u.string.len = len;
      conn->read_ptr += len;
    }
  else
    {
      svn_stringbuf_t *stringbuf;

      /* Don't even attempt to read anything that exceeds the I/O limit.
       * So, we can terminate the transfer at an early point, saving
       * everybody's time and resources. */
      if (conn->max_in && (conn->max_in < len64))
        return svn_error_create(SVN_ERR_RA_SVN_REQUEST_SIZE, NULL,
                                "The client request size exceeds the "
                                "configured limit");

      /* Read the string in chunks.  The chunk size is large enough to avoid
       * re-allocation in typical cases, and small enough to ensure we do
       * not pre-allocate an unreasonable amount of memory if (perhaps due
       * to network data corruption or a DOS attack), we receive a bogus
       * claim that a very long string is going to follow.  In that case, we
       * start small and wait for all that data to actually show up.  This
       * does not fully prevent DOS attacks but makes them harder (you have
       * to actually send gigabytes of data). */
      stringbuf = svn_stringbuf_create_empty(pool);

      /* Read string data directly into the string structure.
       * Do it iteratively.  */
      do
        {
          /* Determine length of chunk to read and re-alloc the buffer. */
          readbuf_len
            = len < SUSPICIOUSLY_HUGE_STRING_SIZE_THRESHOLD
                  ? len
                  : SUSPICIOUSLY_HUGE_STRING_SIZE_THRESHOLD;

          svn_stringbuf_ensure(stringbuf, stringbuf->len + readbuf_len);
          dest = stringbuf->data + stringbuf->len;

          /* read data & update length info */
          SVN_ERR(readbuf_read(conn, pool, dest, readbuf_len));

          stringbuf->len += readbuf_len;
          len -= readbuf_len;
        }
      while (len);

      /* zero-terminate the string */
      stringbuf->data[stringbuf->len] = '\0';

      /* Return the string properly wrapped into an RA_SVN item. */
      item->kind = SVN_RA_SVN_STRING;
      item->u.string.data = stringbuf->data;
      item->u.string.len = stringbuf->len;
    }

  return SVN_NO_ERROR;
}

/* Given the first non-whitespace character FIRST_CHAR, read an item
 * into the already allocated structure ITEM.  LEVEL should be set
 * to 0 for the first call and is used to enforce a recursion limit
 * on the parser. */
static svn_error_t *read_item(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                              svn_ra_svn__item_t *item, char first_char,
                              int level)
{
  char c = first_char;
  apr_uint64_t val;
  svn_ra_svn__item_t *listitem;

  if (++level >= ITEM_NESTING_LIMIT)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Items are nested too deeply"));


  /* Determine the item type and read it in.  Make sure that c is the
   * first character at the end of the item so we can test to make
   * sure it's whitespace. */
  if (svn_ctype_isdigit(c))
    {
      /* It's a number or a string.  Read the number part, either way. */
      val = c - '0';
      while (1)
        {
          apr_uint64_t prev_val = val;
          SVN_ERR(readbuf_getchar(conn, pool, &c));
          if (!svn_ctype_isdigit(c))
            break;
          val = val * 10 + (c - '0');
          /* val wrapped past maximum value? */
          if ((prev_val >= (APR_UINT64_MAX / 10))
              && (val < APR_UINT64_MAX - 10))
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Number is larger than maximum"));
        }
      if (c == ':')
        {
          /* It's a string. */
          SVN_ERR(read_string(conn, pool, item, val));
          SVN_ERR(readbuf_getchar(conn, pool, &c));
        }
      else
        {
          /* It's a number. */
          item->kind = SVN_RA_SVN_NUMBER;
          item->u.number = val;
        }
    }
  else if (svn_ctype_isalpha(c))
    {
      /* It's a word.  Read it into a buffer of limited size. */
      char *buffer = apr_palloc(pool, MAX_WORD_LENGTH + 1);
      char *end = buffer + MAX_WORD_LENGTH;
      char *p = buffer + 1;

      buffer[0] = c;
      if (conn->read_ptr + MAX_WORD_LENGTH <= conn->read_end)
        {
          /* Fast path: we can simply take a chunk from the read
           * buffer and inspect it with no overflow checks etc.
           *
           * Copying these 24 bytes unconditionally is also faster
           * than a variable-sized memcpy.  Note that P is at BUFFER[1].
           */
          memcpy(p, conn->read_ptr, MAX_WORD_LENGTH - 1);
          *end = 0;

          /* This will terminate at P == END because of *END == NUL. */
          while (svn_ctype_isalnum(*p) || *p == '-')
            ++p;

          /* Only now do we mark data as actually read. */
          conn->read_ptr += p - buffer;
        }
      else
        {
          /* Slow path. Byte-by-byte copying and checking for
           * input and output buffer boundaries. */
          for (p = buffer + 1; p != end; ++p)
            {
              SVN_ERR(readbuf_getchar(conn, pool, p));
              if (!svn_ctype_isalnum(*p) && *p != '-')
                break;
            }
        }

      if (p == end)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Word is too long"));

      c = *p;
      *p = '\0';

      /* Store the word in ITEM. */
      item->kind = SVN_RA_SVN_WORD;
      item->u.word.data = buffer;
      item->u.word.len = p - buffer;
    }
  else if (c == '(')
    {
      /* The largest struct that the protocol currently defines has 10
       * elements (log-entry) and add some headroom for future extensions.
       * At a maximum nesting level of 64 this use <= 18kB of stack.
       *
       * All system-defined data structures will fit into this and will be
       * copied into ITEM after a single apr_palloc with no over-provision.
       * Unbounded lists with more than 12 but less than 25 entries will
       * also see only a single allocation from POOL.  However, there will
       * be some over-provision.  Longer lists will see log N resizes and
       * O(N) total cost.
       */
      svn_ra_svn__item_t stack_items[12];
      svn_ra_svn__item_t *items = stack_items;
      int capacity = sizeof(stack_items) / sizeof(stack_items[0]);
      int count = 0;

      /* Read in the list items. */
      item->kind = SVN_RA_SVN_LIST;
      while (1)
        {
          SVN_ERR(readbuf_getchar_skip_whitespace(conn, pool, &c));
          if (c == ')')
            break;

          /* Auto-expand the list. */
          if (count == capacity)
            {
              svn_ra_svn__item_t *new_items
                = apr_palloc(pool, 2 * capacity * sizeof(*new_items));
              memcpy(new_items, items, capacity * sizeof(*new_items));
              items = new_items;
              capacity = 2 * capacity;
            }

          listitem = &items[count];
          ++count;

          SVN_ERR(read_item(conn, pool, listitem, c, level));
        }

      /* Store the list in ITEM - if not empty (= default). */
      if (count)
        {
          item->u.list.nelts = count;

          /* If we haven't allocated from POOL, yet, do it now. */
          if (items == stack_items)
            item->u.list.items = apr_pmemdup(pool, items,
                                             count * sizeof(*items));
          else
            item->u.list.items = items;
        }
      else
        {
          item->u.list.items = NULL;
          item->u.list.nelts = 0;
        }

      SVN_ERR(readbuf_getchar(conn, pool, &c));
    }

  if (!svn_iswhitespace(c))
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed network data"));
  return SVN_NO_ERROR;
}

/* Given the first non-whitespace character FIRST_CHAR, read the first
 * command (word) encountered in CONN into *ITEM.  If ITEM is NULL, skip
 * to the end of the current list.  Use POOL for allocations. */
static svn_error_t *
read_command_only(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                  const char **item, char first_char)
{
  char c = first_char;

  /* Determine the item type and read it in.  Make sure that c is the
  * first character at the end of the item so we can test to make
  * sure it's whitespace. */
  if (svn_ctype_isdigit(c))
    {
      /* It's a number or a string.  Read the number part, either way. */
      apr_uint64_t val, prev_val=0;
      val = c - '0';
      while (1)
        {
          prev_val = val;
          SVN_ERR(readbuf_getchar(conn, pool, &c));
          if (!svn_ctype_isdigit(c))
            break;
          val = val * 10 + (c - '0');
          if (prev_val >= (APR_UINT64_MAX / 10)) /* > maximum value? */
            return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                    _("Number is larger than maximum"));
        }
      if (c == ':')
        {
          /* It's a string. */
          SVN_ERR(readbuf_skip(conn, val));
          SVN_ERR(readbuf_getchar(conn, pool, &c));
        }
    }
  else if (svn_ctype_isalpha(c))
    {
      /* It's a word. */
      if (item)
        {
          /* This is the word we want to read */

          char *buf = apr_palloc(pool, 32);
          apr_size_t len = 1;
          buf[0] = c;

          while (1)
            {
              SVN_ERR(readbuf_getchar(conn, pool, &c));
              if (!svn_ctype_isalnum(c) && c != '-')
                break;
              buf[len] = c;
              if (++len == 32)
                return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                        _("Word too long"));
            }
          buf[len] = 0;
          *item = buf;
        }
      else
        {
          /* we don't need the actual word, just skip it */
          do
          {
            SVN_ERR(readbuf_getchar(conn, pool, &c));
          }
          while (svn_ctype_isalnum(c) || c == '-');
        }
    }
  else if (c == '(')
    {
      /* Read in the list items. */
      while (1)
        {
          SVN_ERR(readbuf_getchar_skip_whitespace(conn, pool, &c));
          if (c == ')')
            break;

          if (item && *item == NULL)
            SVN_ERR(read_command_only(conn, pool, item, c));
          else
            SVN_ERR(read_command_only(conn, pool, NULL, c));
        }
      SVN_ERR(readbuf_getchar(conn, pool, &c));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__read_item(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      svn_ra_svn__item_t **item)
{
  char c;

  /* Allocate space, read the first character, and then do the rest of
   * the work.  This makes sense because of the way lists are read. */
  *item = apr_palloc(pool, sizeof(**item));
  SVN_ERR(readbuf_getchar_skip_whitespace(conn, pool, &c));
  return read_item(conn, pool, *item, c, 0);
}

/* Drain existing whitespace from the receive buffer of CONN until either
   there is no data in the underlying receive socket anymore or we found
   a non-whitespace char.  Set *HAS_ITEM to TRUE in the latter case.
 */
static svn_error_t *
svn_ra_svn__has_item(svn_boolean_t *has_item,
                     svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool)
{
  do
    {
      if (conn->read_ptr == conn->read_end)
        {
          svn_boolean_t available;
          if (conn->write_pos)
            SVN_ERR(writebuf_flush(conn, pool));

          SVN_ERR(svn_ra_svn__data_available(conn, &available));
          if (!available)
            break;

          SVN_ERR(readbuf_fill(conn, pool));
        }
    }
  while (svn_iswhitespace(*conn->read_ptr) && ++conn->read_ptr);

  *has_item = conn->read_ptr != conn->read_end;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__skip_leading_garbage(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool)
{
  return readbuf_skip_leading_garbage(conn, pool);
}

/* --- READING AND PARSING TUPLES --- */

/* Parse a tuple of svn_ra_svn__item_t *'s.  Advance *FMT to the end of the
 * tuple specification and advance AP by the corresponding arguments. */
static svn_error_t *
vparse_tuple(const svn_ra_svn__list_t *items,
             const char **fmt,
             va_list *ap)
{
  int count, nesting_level;
  svn_ra_svn__item_t *elt;

  for (count = 0; **fmt && count < items->nelts; (*fmt)++, count++)
    {
      /* '?' just means the tuple may stop; skip past it. */
      if (**fmt == '?')
        (*fmt)++;
      elt = &SVN_RA_SVN__LIST_ITEM(items, count);
      if (**fmt == '(' && elt->kind == SVN_RA_SVN_LIST)
        {
          (*fmt)++;
          SVN_ERR(vparse_tuple(&elt->u.list, fmt, ap));
        }
      else if (**fmt == 'c' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, const char **) = elt->u.string.data;
      else if (**fmt == 's' && elt->kind == SVN_RA_SVN_STRING)
        *va_arg(*ap, svn_string_t **) = &elt->u.string;
      else if (**fmt == 'w' && elt->kind == SVN_RA_SVN_WORD)
        *va_arg(*ap, const char **) = elt->u.word.data;
      else if (**fmt == 'b' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (svn_string_compare(&elt->u.word, &str_true))
            *va_arg(*ap, svn_boolean_t *) = TRUE;
          else if (svn_string_compare(&elt->u.word, &str_false))
            *va_arg(*ap, svn_boolean_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == 'n' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, apr_uint64_t *) = elt->u.number;
      else if (**fmt == 'r' && elt->kind == SVN_RA_SVN_NUMBER)
        *va_arg(*ap, svn_revnum_t *) = (svn_revnum_t) elt->u.number;
      else if (**fmt == 'B' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (svn_string_compare(&elt->u.word, &str_true))
            *va_arg(*ap, apr_uint64_t *) = TRUE;
          else if (svn_string_compare(&elt->u.word, &str_false))
            *va_arg(*ap, apr_uint64_t *) = FALSE;
          else
            break;
        }
      else if (**fmt == '3' && elt->kind == SVN_RA_SVN_WORD)
        {
          if (svn_string_compare(&elt->u.word, &str_true))
            *va_arg(*ap, svn_tristate_t *) = svn_tristate_true;
          else if (svn_string_compare(&elt->u.word, &str_false))
            *va_arg(*ap, svn_tristate_t *) = svn_tristate_false;
          else
            break;
        }
      else if (**fmt == 'l' && elt->kind == SVN_RA_SVN_LIST)
        *va_arg(*ap, svn_ra_svn__list_t **) = &elt->u.list;
      else if (**fmt == ')')
        return SVN_NO_ERROR;
      else
        break;
    }
  if (**fmt == '?')
    {
      nesting_level = 0;
      for (; **fmt; (*fmt)++)
        {
          switch (**fmt)
            {
            case '?':
              break;
            case 'r':
              *va_arg(*ap, svn_revnum_t *) = SVN_INVALID_REVNUM;
              break;
            case 's':
              *va_arg(*ap, svn_string_t **) = NULL;
              break;
            case 'c':
            case 'w':
              *va_arg(*ap, const char **) = NULL;
              break;
            case 'l':
              *va_arg(*ap, svn_ra_svn__list_t **) = NULL;
              break;
            case 'B':
            case 'n':
              *va_arg(*ap, apr_uint64_t *) = SVN_RA_SVN_UNSPECIFIED_NUMBER;
              break;
            case '3':
              *va_arg(*ap, svn_tristate_t *) = svn_tristate_unknown;
              break;
            case 'b':
              *va_arg(*ap, svn_boolean_t *) = FALSE;
              break;
            case '(':
              nesting_level++;
              break;
            case ')':
              if (--nesting_level < 0)
                return SVN_NO_ERROR;
              break;
            default:
              SVN_ERR_MALFUNCTION();
            }
        }
    }
  if (**fmt && **fmt != ')')
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed network data"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__parse_tuple(const svn_ra_svn__list_t *list,
                        const char *fmt, ...)
{
  svn_error_t *err;
  va_list ap;

  va_start(ap, fmt);
  err = vparse_tuple(list, &fmt, &ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_ra_svn__read_tuple(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *fmt, ...)
{
  va_list ap;
  svn_ra_svn__item_t *item;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn__read_item(conn, pool, &item));
  if (item->kind != SVN_RA_SVN_LIST)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Malformed network data"));
  va_start(ap, fmt);
  err = vparse_tuple(&item->u.list, &fmt, &ap);
  va_end(ap);
  return err;
}

svn_error_t *
svn_ra_svn__read_command_only(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char **command)
{
  char c;
  SVN_ERR(readbuf_getchar_skip_whitespace(conn, pool, &c));

  *command = NULL;
  return read_command_only(conn, pool, command, c);
}


svn_error_t *
svn_ra_svn__parse_proplist(const svn_ra_svn__list_t *list,
                           apr_pool_t *pool,
                           apr_hash_t **props)
{
  svn_string_t *name;
  svn_string_t *value;
  svn_ra_svn__item_t *elt;
  int i;

  *props = svn_hash__make(pool);
  for (i = 0; i < list->nelts; i++)
    {
      elt = &SVN_RA_SVN__LIST_ITEM(list, i);
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Proplist element not a list"));
      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "ss", &name, &value));
      apr_hash_set(*props, name->data, name->len, value);
    }

  return SVN_NO_ERROR;
}


/* --- READING AND WRITING COMMANDS AND RESPONSES --- */

svn_error_t *svn_ra_svn__locate_real_error_child(svn_error_t *err)
{
  svn_error_t *this_link;

  SVN_ERR_ASSERT(err);

  for (this_link = err;
       this_link && (this_link->apr_err == SVN_ERR_RA_SVN_CMD_ERR);
       this_link = this_link->child)
    ;

  SVN_ERR_ASSERT(this_link);
  return this_link;
}

svn_error_t *
svn_ra_svn__handle_failure_status(const svn_ra_svn__list_t *params)
{
  const char *message, *file;
  svn_error_t *err = NULL;
  svn_ra_svn__item_t *elt;
  int i;
  apr_uint64_t apr_err, line;

  if (params->nelts == 0)
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                            _("Empty error list"));

  /* Rebuild the error list from the end, to avoid reversing the order. */
  for (i = params->nelts - 1; i >= 0; i--)
    {
      elt = &SVN_RA_SVN__LIST_ITEM(params, i);
      if (elt->kind != SVN_RA_SVN_LIST)
        return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                                _("Malformed error list"));
      SVN_ERR(svn_ra_svn__parse_tuple(&elt->u.list, "nccn",
                                      &apr_err, &message, &file, &line));
      /* The message field should have been optional, but we can't
         easily change that, so "" means a nonexistent message. */
      if (!*message)
        message = NULL;

      /* Skip over links in the error chain that were intended only to
         exist on the server (to wrap real errors intended for the
         client) but accidentally got included in the server's actual
         response. */
      if ((apr_status_t)apr_err != SVN_ERR_RA_SVN_CMD_ERR)
        {
          err = svn_error_create((apr_status_t)apr_err, err, message);
          err->file = apr_pstrdup(err->pool, file);
          err->line = (long)line;
        }
    }

  /* If we get here, then we failed to find a real error in the error
     chain that the server proported to be sending us.  That's bad. */
  if (! err)
    err = svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                           _("Malformed error list"));

  return err;
}

svn_error_t *
svn_ra_svn__read_cmd_response(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *fmt, ...)
{
  va_list ap;
  const char *status;
  svn_ra_svn__list_t *params;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn__read_tuple(conn, pool, "wl", &status, &params));
  if (strcmp(status, "success") == 0)
    {
      va_start(ap, fmt);
      err = vparse_tuple(params, &fmt, &ap);
      va_end(ap);
      return err;
    }
  else if (strcmp(status, "failure") == 0)
    {
      return svn_error_trace(svn_ra_svn__handle_failure_status(params));
    }

  return svn_error_createf(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL,
                           _("Unknown status '%s' in command response"),
                           status);
}

svn_error_t *
svn_ra_svn__has_command(svn_boolean_t *has_command,
                        svn_boolean_t *terminated,
                        svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool)
{
  svn_error_t *err;

  /* Don't make whitespace between commands trigger I/O limitiations. */
  svn_ra_svn__reset_command_io_counters(conn);

  err = svn_ra_svn__has_item(has_command, conn, pool);
  if (err && err->apr_err == SVN_ERR_RA_SVN_CONNECTION_CLOSED)
    {
      *terminated = TRUE;
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  *terminated = FALSE;
  return svn_error_trace(err);
}

svn_error_t *
svn_ra_svn__handle_command(svn_boolean_t *terminate,
                           apr_hash_t *cmd_hash,
                           void *baton,
                           svn_ra_svn_conn_t *conn,
                           svn_boolean_t error_on_disconnect,
                           apr_pool_t *pool)
{
  const char *cmdname;
  svn_error_t *err, *write_err;
  svn_ra_svn__list_t *params;
  const svn_ra_svn__cmd_entry_t *command;

  *terminate = FALSE;

  /* Limit I/O for every command separately. */
  svn_ra_svn__reset_command_io_counters(conn);

  err = svn_ra_svn__read_tuple(conn, pool, "wl", &cmdname, &params);
  if (err)
    {
      if (!error_on_disconnect
          && err->apr_err == SVN_ERR_RA_SVN_CONNECTION_CLOSED)
        {
          svn_error_clear(err);
          *terminate = TRUE;
          return SVN_NO_ERROR;
        }
      return err;
    }

  command = svn_hash_gets(cmd_hash, cmdname);
  if (command)
    {
      /* Call the standard command handler.
       * If that is not set, then this is a lecagy API call and we invoke
       * the legacy command handler. */
      if (command->handler)
        {
          err = (*command->handler)(conn, pool, params, baton);
        }
      else
        {
          apr_array_header_t *deprecated_params
            = svn_ra_svn__to_public_array(params, pool);
          err = (*command->deprecated_handler)(conn, pool, deprecated_params,
                                               baton);
        }

      /* The command implementation may have swallowed or wrapped the I/O
       * error not knowing that we may no longer be able to send data.
       *
       * So, check again for the limit violations and exit the command
       * processing quickly if we may have truncated data. */
      err = svn_error_compose_create(check_io_limits(conn), err);

      *terminate = command->terminate;
    }
  else
    {
      err = svn_error_createf(SVN_ERR_RA_SVN_UNKNOWN_CMD, NULL,
                              _("Unknown editor command '%s'"), cmdname);
      err = svn_error_create(SVN_ERR_RA_SVN_CMD_ERR, err, NULL);
    }

  if (err && err->apr_err == SVN_ERR_RA_SVN_CMD_ERR)
    {
      write_err = svn_ra_svn__write_cmd_failure(
                      conn, pool,
                      svn_ra_svn__locate_real_error_child(err));
      svn_error_clear(err);
      return write_err ? write_err : SVN_NO_ERROR;
    }

  return err;
}

svn_error_t *
svn_ra_svn__handle_commands2(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const svn_ra_svn__cmd_entry_t *commands,
                             void *baton,
                             svn_boolean_t error_on_disconnect)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_pool_t *iterpool = svn_pool_create(subpool);
  const svn_ra_svn__cmd_entry_t *command;
  apr_hash_t *cmd_hash = apr_hash_make(subpool);

  for (command = commands; command->cmdname; command++)
    svn_hash_sets(cmd_hash, command->cmdname, command);

  while (1)
    {
      svn_boolean_t terminate;
      svn_error_t *err;
      svn_pool_clear(iterpool);

      err = svn_ra_svn__handle_command(&terminate, cmd_hash, baton, conn,
                                       error_on_disconnect, iterpool);
      if (err)
        {
          svn_pool_destroy(subpool);
          return svn_error_trace(err);
        }
      if (terminate)
        break;
    }
  svn_pool_destroy(iterpool);
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_target_rev(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( target-rev ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_open_root(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                svn_revnum_t rev,
                                const svn_string_t *token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( open-root ( "));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_delete_entry(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   const char *path,
                                   svn_revnum_t rev,
                                   const svn_string_t *token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( delete-entry ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_add_dir(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *path,
                              const svn_string_t *parent_token,
                              const svn_string_t *token,
                              const char *copy_path,
                              svn_revnum_t copy_rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( add-dir ( "));
  SVN_ERR(write_cmd_add_node(conn, pool, path, parent_token, token,
                              copy_path, copy_rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_open_dir(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               const svn_string_t *parent_token,
                               const svn_string_t *token,
                               svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( open-dir ( "));
  SVN_ERR(write_cmd_open_node(conn, pool, path, parent_token, token, rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_change_dir_prop(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const char *name,
                                      const svn_string_t *value)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( change-dir-prop ( "));
  SVN_ERR(write_cmd_change_node_prop(conn, pool, token, name, value));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_close_dir(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const svn_string_t *token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( close-dir ( "));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_absent_dir(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 const svn_string_t *parent_token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( absent-dir ( "));
  SVN_ERR(write_cmd_absent_node(conn, pool, path, parent_token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_add_file(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               const svn_string_t *parent_token,
                               const svn_string_t *token,
                               const char *copy_path,
                               svn_revnum_t copy_rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( add-file ( "));
  SVN_ERR(write_cmd_add_node(conn, pool, path, parent_token, token,
                              copy_path, copy_rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_open_file(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                const svn_string_t *parent_token,
                                const svn_string_t *token,
                                svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( open-file ( "));
  SVN_ERR(write_cmd_open_node(conn, pool, path, parent_token, token, rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_change_file_prop(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       const svn_string_t *token,
                                       const char *name,
                                       const svn_string_t *value)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( change-file-prop ( "));
  SVN_ERR(write_cmd_change_node_prop(conn, pool, token, name, value));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_close_file(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const svn_string_t *token,
                                 const char *text_checksum)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( close-file ( "));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool, text_checksum));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_absent_file(svn_ra_svn_conn_t *conn,
                                  apr_pool_t *pool,
                                  const char *path,
                                  const svn_string_t *parent_token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( absent-file ( "));
  SVN_ERR(write_cmd_absent_node(conn, pool, path, parent_token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_textdelta_chunk(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const svn_string_t *chunk)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( textdelta-chunk ( "));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_string(conn, pool, chunk));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_textdelta_end(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    const svn_string_t *token)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( textdelta-end ( "));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_apply_textdelta(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const svn_string_t *token,
                                      const char *base_checksum)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( apply-textdelta ( "));
  SVN_ERR(write_tuple_string(conn, pool, token));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool, base_checksum));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_close_edit(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( close-edit ( ) ) ");
}

svn_error_t *
svn_ra_svn__write_cmd_abort_edit(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( abort-edit ( ) ) ");
}

svn_error_t *
svn_ra_svn__write_cmd_set_path(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               svn_revnum_t rev,
                               svn_boolean_t start_empty,
                               const char *lock_token,
                               svn_depth_t depth)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( set-path ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_boolean(conn, pool, start_empty));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool, lock_token));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_delete_path(svn_ra_svn_conn_t *conn,
                                  apr_pool_t *pool,
                                  const char *path)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( delete-path ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_link_path(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                const char *url,
                                svn_revnum_t rev,
                                svn_boolean_t start_empty,
                                const char *lock_token,
                                svn_depth_t depth)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( link-path ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_cstring(conn, pool, url));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_boolean(conn, pool, start_empty));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool,lock_token));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_finish_report(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( finish-report ( ) ) ");
}

svn_error_t *
svn_ra_svn__write_cmd_abort_report(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( abort-report ( ) ) ");
}

svn_error_t *
svn_ra_svn__write_cmd_reparent(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *url)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( reparent ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, url));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_latest_rev(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( get-latest-rev ( ) ) ");
}

svn_error_t *
svn_ra_svn__write_cmd_get_dated_rev(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    apr_time_t tm)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-dated-rev ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, svn_time_to_cstring(tm, pool)));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_change_rev_prop2(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       svn_revnum_t rev,
                                       const char *name,
                                       const svn_string_t *value,
                                       svn_boolean_t dont_care,
                                       const svn_string_t *old_value)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( change-rev-prop2 ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_cstring(conn, pool, name));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, value));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_boolean(conn, pool, dont_care));
  SVN_ERR(write_tuple_string_opt(conn, pool, old_value));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_change_rev_prop(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      svn_revnum_t rev,
                                      const char *name,
                                      const svn_string_t *value)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( change-rev-prop ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_cstring(conn, pool, name));
  SVN_ERR(write_tuple_string_opt(conn, pool, value));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_rev_proplist(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( rev-proplist ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_rev_prop(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               svn_revnum_t rev,
                               const char *name)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( rev-prop ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_cstring(conn, pool, name));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_file(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path,
                               svn_revnum_t rev,
                               svn_boolean_t props,
                               svn_boolean_t stream)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-file ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_boolean(conn, pool, props));
  SVN_ERR(write_tuple_boolean(conn, pool, stream));

  /* Always send the, nominally optional, want-iprops as "false" to
     workaround a bug in svnserve 1.8.0-1.8.8 that causes the server
     to see "true" if it is omitted. */
  SVN_ERR(writebuf_write_literal(conn, pool, " false ) ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_update(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             const char *target,
                             svn_boolean_t recurse,
                             svn_depth_t depth,
                             svn_boolean_t send_copyfrom_args,
                             svn_boolean_t ignore_ancestry)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( update ( "));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_cstring(conn, pool, target));
  SVN_ERR(write_tuple_boolean(conn, pool, recurse));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(write_tuple_boolean(conn, pool, send_copyfrom_args));
  SVN_ERR(write_tuple_boolean(conn, pool, ignore_ancestry));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_switch(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             const char *target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             svn_depth_t depth,
                             svn_boolean_t send_copyfrom_args,
                             svn_boolean_t ignore_ancestry)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( switch ( "));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_cstring(conn, pool, target));
  SVN_ERR(write_tuple_boolean(conn, pool, recurse));
  SVN_ERR(write_tuple_cstring(conn, pool, switch_url));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(write_tuple_boolean(conn, pool, send_copyfrom_args));
  SVN_ERR(write_tuple_boolean(conn, pool, ignore_ancestry));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_status(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *target,
                             svn_boolean_t recurse,
                             svn_revnum_t rev,
                             svn_depth_t depth)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( status ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, target));
  SVN_ERR(write_tuple_boolean(conn, pool, recurse));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_diff(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           svn_revnum_t rev,
                           const char *target,
                           svn_boolean_t recurse,
                           svn_boolean_t ignore_ancestry,
                           const char *versus_url,
                           svn_boolean_t text_deltas,
                           svn_depth_t depth)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( diff ( "));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_cstring(conn, pool, target));
  SVN_ERR(write_tuple_boolean(conn, pool, recurse));
  SVN_ERR(write_tuple_boolean(conn, pool, ignore_ancestry));
  SVN_ERR(write_tuple_cstring(conn, pool, versus_url));
  SVN_ERR(write_tuple_boolean(conn, pool, text_deltas));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_check_path(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( check-path ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_stat(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const char *path,
                           svn_revnum_t rev)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( stat ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, rev));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_file_revs(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool,
                                    const char *path,
                                    svn_revnum_t start,
                                    svn_revnum_t end,
                                    svn_boolean_t include_merged_revisions)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-file-revs ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, start));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, end));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_boolean(conn, pool, include_merged_revisions));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_lock(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const char *path,
                           const char *comment,
                           svn_boolean_t steal_lock,
                           svn_revnum_t revnum)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( lock ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_cstring_opt(conn, pool, comment));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(write_tuple_boolean(conn, pool, steal_lock));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, revnum));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_unlock(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *path,
                             const svn_string_t *token,
                             svn_boolean_t break_lock)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( unlock ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, token));
  SVN_ERR(write_tuple_boolean(conn, pool, break_lock));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_lock(svn_ra_svn_conn_t *conn,
                               apr_pool_t *pool,
                               const char *path)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-lock ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_locks(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool,
                                const char *path,
                                svn_depth_t depth)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-locks ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_depth(conn, pool, depth));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_replay(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_revnum_t rev,
                             svn_revnum_t low_water_mark,
                             svn_boolean_t send_deltas)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( replay ( "));
  SVN_ERR(write_tuple_revision(conn, pool, rev));
  SVN_ERR(write_tuple_revision(conn, pool, low_water_mark));
  SVN_ERR(write_tuple_boolean(conn, pool, send_deltas));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_replay_range(svn_ra_svn_conn_t *conn,
                                   apr_pool_t *pool,
                                   svn_revnum_t start_revision,
                                   svn_revnum_t end_revision,
                                   svn_revnum_t low_water_mark,
                                   svn_boolean_t send_deltas)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( replay-range ( "));
  SVN_ERR(write_tuple_revision(conn, pool, start_revision));
  SVN_ERR(write_tuple_revision(conn, pool, end_revision));
  SVN_ERR(write_tuple_revision(conn, pool, low_water_mark));
  SVN_ERR(write_tuple_boolean(conn, pool, send_deltas));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_deleted_rev(svn_ra_svn_conn_t *conn,
                                      apr_pool_t *pool,
                                      const char *path,
                                      svn_revnum_t peg_revision,
                                      svn_revnum_t end_revision)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-deleted-rev ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_revision(conn, pool, peg_revision));
  SVN_ERR(write_tuple_revision(conn, pool, end_revision));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_get_iprops(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 const char *path,
                                 svn_revnum_t revision)
{
  SVN_ERR(writebuf_write_literal(conn, pool, "( get-iprops ( "));
  SVN_ERR(write_tuple_cstring(conn, pool, path));
  SVN_ERR(write_tuple_start_list(conn, pool));
  SVN_ERR(write_tuple_revision_opt(conn, pool, revision));
  SVN_ERR(write_tuple_end_list(conn, pool));
  SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_cmd_finish_replay(svn_ra_svn_conn_t *conn,
                                    apr_pool_t *pool)
{
  return writebuf_write_literal(conn, pool, "( finish-replay ( ) ) ");
}

svn_error_t *svn_ra_svn__write_cmd_response(svn_ra_svn_conn_t *conn,
                                            apr_pool_t *pool,
                                            const char *fmt, ...)
{
  va_list ap;
  svn_error_t *err;

  SVN_ERR(writebuf_write_literal(conn, pool, "( success "));
  va_start(ap, fmt);
  err = vwrite_tuple(conn, pool, fmt, &ap);
  va_end(ap);
  return err ? svn_error_trace(err) : svn_ra_svn__end_list(conn, pool);
}

svn_error_t *svn_ra_svn__write_cmd_failure(svn_ra_svn_conn_t *conn,
                                           apr_pool_t *pool,
                                           const svn_error_t *err)
{
  char buffer[128];
  SVN_ERR(writebuf_write_literal(conn, pool, "( failure ( "));
  for (; err; err = err->child)
    {
      const char *msg;

#ifdef SVN_ERR__TRACING
      if (svn_error__is_tracing_link(err))
        msg = err->message;
      else
#endif
        msg = svn_err_best_message(err, buffer, sizeof(buffer));

      /* The message string should have been optional, but we can't
         easily change that, so marshal nonexistent messages as "". */
      SVN_ERR(svn_ra_svn__write_tuple(conn, pool, "nccn",
                                      (apr_uint64_t) err->apr_err,
                                      msg ? msg : "",
                                      err->file ? err->file : "",
                                      (apr_uint64_t) err->line));
    }
  return writebuf_write_literal(conn, pool, ") ) ");
}

/* Initializer for static svn_string_t . */
#define STATIC_SVN_STRING(x) { x, sizeof(x) - 1 }

/* Return a pre-cooked serialized representation for the changed path
   flags NODE_KIND, TEXT_MODIFIED and PROPS_MODIFIED.  If we don't
   have a suitable pre-cooked string, return an empty string. */
static const svn_string_t *
changed_path_flags(svn_node_kind_t node_kind,
                   svn_boolean_t text_modified,
                   svn_boolean_t props_modified)
{
  static const svn_string_t file_flags[4]
    = { STATIC_SVN_STRING(" ) ( 4:file false false ) ) "),
        STATIC_SVN_STRING(" ) ( 4:file false true ) ) "),
        STATIC_SVN_STRING(" ) ( 4:file true false ) ) "),
        STATIC_SVN_STRING(" ) ( 4:file true true ) ) ") };

  static const svn_string_t dir_flags[4]
    = { STATIC_SVN_STRING(" ) ( 3:dir false false ) ) "),
        STATIC_SVN_STRING(" ) ( 3:dir false true ) ) "),
        STATIC_SVN_STRING(" ) ( 3:dir true false ) ) "),
        STATIC_SVN_STRING(" ) ( 3:dir true true ) ) ") };

  static const svn_string_t no_flags = STATIC_SVN_STRING("");

  /* Select the array based on the NODE_KIND. */
  const svn_string_t *flags;
  if (node_kind == svn_node_file)
    flags = file_flags;
  else if (node_kind == svn_node_dir)
    flags = dir_flags;
  else
    return &no_flags;

  /* Select the correct array entry. */
  if (text_modified)
    flags += 2;
  if (props_modified)
    flags++;

  return flags;
}

svn_error_t *
svn_ra_svn__write_data_log_changed_path(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool,
                                        const svn_string_t *path,
                                        char action,
                                        const char *copyfrom_path,
                                        svn_revnum_t copyfrom_rev,
                                        svn_node_kind_t node_kind,
                                        svn_boolean_t text_modified,
                                        svn_boolean_t props_modified)
{
  apr_size_t path_len = path->len;
  apr_size_t copyfrom_len = copyfrom_path ? strlen(copyfrom_path) : 0;
  const svn_string_t *flags_str = changed_path_flags(node_kind,
                                                     text_modified,
                                                     props_modified);
  apr_size_t flags_len = flags_str->len;

  /* How much buffer space can we use for non-string data (worst case)? */
  apr_size_t max_fill = sizeof(conn->write_buf)
                      - 2                          /* list start */
                      - 2 - SVN_INT64_BUFFER_SIZE  /* path */
                      - 2                          /* action */
                      - 2                          /* list start */
                      - 2 - SVN_INT64_BUFFER_SIZE  /* copy-from path */
                      - 1 - SVN_INT64_BUFFER_SIZE; /* copy-from rev */

  /* If the remaining buffer is big enough and we've got all parts,
     directly copy into the buffer.   On platforms with segmented memory,
     PATH_LEN + COPYFROM_LEN might actually be close to APR_SIZE_MAX.
     Blindly doing arithmetic on them might cause an overflow.
     The sum in here cannot overflow because WRITE_BUF is small, i.e.
     MAX_FILL and WRITE_POS are much smaller than APR_SIZE_MAX. */
  if (   (path_len <= max_fill) && (copyfrom_len <= max_fill)
      && (conn->write_pos + path_len + copyfrom_len + flags_len <= max_fill)
      && (flags_len > 0))
    {
      /* Quick path. */
      /* Open list. */
      char *p = conn->write_buf + conn->write_pos;
      p[0] = '(';
      p[1] = ' ';

      /* Write path. */
      p = write_ncstring_quick(p + 2, path->data, path_len);

      /* Action */
      p[0] = action;
      p[1] = ' ';
      p[2] = '(';

      /* Copy-from info (if given) */
      if (copyfrom_path)
        {
          p[3] = ' ';
          p = write_ncstring_quick(p + 4, copyfrom_path, copyfrom_len);
          p += svn__ui64toa(p, copyfrom_rev);
        }
      else
        {
          p += 3;
        }

      /* Close with flags. */
      memcpy(p, flags_str->data, flags_str->len);
      conn->write_pos = p + flags_str->len - conn->write_buf;
    }
  else
    {
      /* Standard code path (fallback). */
      SVN_ERR(write_tuple_start_list(conn, pool));

      SVN_ERR(svn_ra_svn__write_ncstring(conn, pool, path->data, path_len));
      SVN_ERR(writebuf_writechar(conn, pool, action));
      SVN_ERR(writebuf_writechar(conn, pool, ' '));
      SVN_ERR(write_tuple_start_list(conn, pool));
      SVN_ERR(write_tuple_cstring_opt(conn, pool, copyfrom_path));
      SVN_ERR(write_tuple_revision_opt(conn, pool, copyfrom_rev));
      SVN_ERR(write_tuple_end_list(conn, pool));
      SVN_ERR(write_tuple_start_list(conn, pool));
      SVN_ERR(write_tuple_cstring(conn, pool, svn_node_kind_to_word(node_kind)));
      SVN_ERR(write_tuple_boolean(conn, pool, text_modified));
      SVN_ERR(write_tuple_boolean(conn, pool, props_modified));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_data_log_entry(svn_ra_svn_conn_t *conn,
                                 apr_pool_t *pool,
                                 svn_revnum_t revision,
                                 const svn_string_t *author,
                                 const svn_string_t *date,
                                 const svn_string_t *message,
                                 svn_boolean_t has_children,
                                 svn_boolean_t invalid_revnum,
                                 unsigned revprop_count)
{
  SVN_ERR(write_tuple_revision(conn, pool, revision));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, author));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, date));
  SVN_ERR(write_tuple_string_opt_list(conn, pool, message));
  SVN_ERR(write_tuple_boolean(conn, pool, has_children));
  SVN_ERR(write_tuple_boolean(conn, pool, invalid_revnum));
  SVN_ERR(svn_ra_svn__write_number(conn, pool, revprop_count));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__write_dirent(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const char *path,
                         svn_dirent_t *dirent,
                         apr_uint32_t dirent_fields)
{
  const char *kind = (dirent_fields & SVN_DIRENT_KIND)
                   ? svn_node_kind_to_word(dirent->kind)
                   : "unknown";

  if (dirent_fields & ~SVN_DIRENT_KIND)
    {
      SVN_ERR(write_tuple_start_list(conn, pool));
      SVN_ERR(write_tuple_cstring(conn, pool, path));
      SVN_ERR(writebuf_write(conn, pool, kind, strlen(kind)));

      SVN_ERR(writebuf_write_literal(conn, pool, " ( "));
      if (dirent_fields & SVN_DIRENT_SIZE)
        SVN_ERR(svn_ra_svn__write_number(conn, pool, dirent->size));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ( "));
      if (dirent_fields & SVN_DIRENT_HAS_PROPS)
        SVN_ERR(write_tuple_boolean(conn, pool, dirent->has_props));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ( "));
      if (dirent_fields & SVN_DIRENT_CREATED_REV)
        SVN_ERR(write_tuple_revision(conn, pool, dirent->created_rev));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ( "));
      if (dirent_fields & SVN_DIRENT_TIME)
        SVN_ERR(write_tuple_cstring_opt(conn, pool,
                                  svn_time_to_cstring(dirent->time, pool)));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ( "));
      if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
        SVN_ERR(write_tuple_cstring_opt(conn, pool, dirent->last_author));

      SVN_ERR(writebuf_write_literal(conn, pool, ") ) "));
    }
  else
    {
      SVN_ERR(write_tuple_start_list(conn, pool));
      SVN_ERR(write_tuple_cstring(conn, pool, path));
      SVN_ERR(writebuf_write(conn, pool, kind, strlen(kind)));
      SVN_ERR(writebuf_write_literal(conn, pool, " ) "));
    }

  return SVN_NO_ERROR;
}

/* If condition COND is not met, return a "malformed network data" error.
 */
#define CHECK_PROTOCOL_COND(cond)\
  if (!(cond)) \
    return svn_error_create(SVN_ERR_RA_SVN_MALFORMED_DATA, NULL, \
                            _("Malformed network data"));

/* In *RESULT, return the SVN-style string at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_string(const svn_ra_svn__list_t *items,
                        int idx,
                        svn_string_t **result)
{
  svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_STRING);
  *result = &elt->u.string;

  return SVN_NO_ERROR;
}

/* In *RESULT, return the C-style string at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_cstring(const svn_ra_svn__list_t *items,
                         int idx,
                         const char **result)
{
  svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_STRING);
  *result = elt->u.string.data;

  return SVN_NO_ERROR;
}

/* In *RESULT, return the word at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_word(const svn_ra_svn__list_t *items,
                      int idx,
                      const char **result)
{
  svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_WORD);
  *result = elt->u.word.data;

  return SVN_NO_ERROR;
}

/* In *RESULT, return the revision at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_revision(const svn_ra_svn__list_t *items,
                          int idx,
                          svn_revnum_t *result)
{
  svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_NUMBER);
  *result = (svn_revnum_t)elt->u.number;

  return SVN_NO_ERROR;
}

/* In *RESULT, return the boolean at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_boolean(const svn_ra_svn__list_t *items,
                         int idx,
                         apr_uint64_t *result)
{
  svn_ra_svn__item_t *elt = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_WORD);
  if (svn_string_compare(&elt->u.word, &str_true))
    *result = TRUE;
  else if (svn_string_compare(&elt->u.word, &str_false))
    *result = FALSE;
  else
    CHECK_PROTOCOL_COND(FALSE);

  return SVN_NO_ERROR;
}

/* In *RESULT, return the tuple at index IDX in tuple ITEMS.
 */
static svn_error_t *
svn_ra_svn__read_list(const svn_ra_svn__list_t *items,
                      int idx,
                      const svn_ra_svn__list_t **result)
{
  svn_ra_svn__item_t *elt  = &SVN_RA_SVN__LIST_ITEM(items, idx);
  CHECK_PROTOCOL_COND(elt->kind == SVN_RA_SVN_LIST);

  *result = &elt->u.list;
  return SVN_NO_ERROR;
}

/* Verify the tuple ITEMS contains at least MIN and at most MAX elements.
 */
static svn_error_t *
svn_ra_svn__read_check_array_size(const svn_ra_svn__list_t *items,
                                  int min,
                                  int max)
{
  CHECK_PROTOCOL_COND(items->nelts >= min && items->nelts <= max);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__read_data_log_changed_entry(const svn_ra_svn__list_t *items,
                                        svn_string_t **cpath,
                                        const char **action,
                                        const char **copy_path,
                                        svn_revnum_t *copy_rev,
                                        const char **kind_str,
                                        apr_uint64_t *text_mods,
                                        apr_uint64_t *prop_mods)
{
  const svn_ra_svn__list_t *sub_items;

  /* initialize optional values */
  *copy_path = NULL;
  *copy_rev = SVN_INVALID_REVNUM;
  *kind_str = NULL;
  *text_mods = SVN_RA_SVN_UNSPECIFIED_NUMBER;
  *prop_mods = SVN_RA_SVN_UNSPECIFIED_NUMBER;

  /* top-level elements (mandatory) */
  SVN_ERR(svn_ra_svn__read_check_array_size(items, 3, INT_MAX));
  SVN_ERR(svn_ra_svn__read_string(items, 0, cpath));
  SVN_ERR(svn_ra_svn__read_word(items, 1, action));

  /* first sub-structure (mandatory) */
  SVN_ERR(svn_ra_svn__read_list(items, 2, &sub_items));
  if (sub_items->nelts)
    {
      SVN_ERR(svn_ra_svn__read_check_array_size(sub_items, 2, 2));
      SVN_ERR(svn_ra_svn__read_cstring(sub_items, 0, copy_path));
      SVN_ERR(svn_ra_svn__read_revision(sub_items, 1, copy_rev));
    }

  /* second sub-structure (optional) */
  if (items->nelts >= 4)
    {
      SVN_ERR(svn_ra_svn__read_list(items, 3, &sub_items));
      switch (MIN(3, sub_items->nelts))
        {
          case 3 : SVN_ERR(svn_ra_svn__read_boolean(sub_items, 2, prop_mods));
          case 2 : SVN_ERR(svn_ra_svn__read_boolean(sub_items, 1, text_mods));
          case 1 : SVN_ERR(svn_ra_svn__read_cstring(sub_items, 0, kind_str));
          default: break;
        }
    }

  return SVN_NO_ERROR;
}
