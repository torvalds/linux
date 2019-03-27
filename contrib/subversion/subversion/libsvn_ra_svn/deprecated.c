/*
 * deprecated.c :  Public, deprecated wrappers to our private ra_svn API
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include "svn_ra_svn.h"

#include "private/svn_ra_svn_private.h"

svn_error_t *
svn_ra_svn_write_number(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        apr_uint64_t number)
{
  return svn_error_trace(svn_ra_svn__write_number(conn, pool, number));
}

svn_error_t *
svn_ra_svn_write_string(svn_ra_svn_conn_t *conn,
                        apr_pool_t *pool,
                        const svn_string_t *str)
{
  return svn_error_trace(svn_ra_svn__write_string(conn, pool, str));
}

svn_error_t *
svn_ra_svn_write_cstring(svn_ra_svn_conn_t *conn,
                         apr_pool_t *pool,
                         const char *s)
{
  return svn_error_trace(svn_ra_svn__write_cstring(conn, pool, s));
}

svn_error_t *
svn_ra_svn_write_word(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const char *word)
{
  return svn_error_trace(svn_ra_svn__write_word(conn, pool, word));
}

svn_error_t *
svn_ra_svn_write_proplist(svn_ra_svn_conn_t *conn,
                          apr_pool_t *pool,
                          apr_hash_t *props)
{
  return svn_error_trace(svn_ra_svn__write_proplist(conn, pool, props));
}

svn_error_t *
svn_ra_svn_start_list(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool)
{
  return svn_error_trace(svn_ra_svn__start_list(conn, pool));
}

svn_error_t *
svn_ra_svn_end_list(svn_ra_svn_conn_t *conn,
                    apr_pool_t *pool)
{
  return svn_error_trace(svn_ra_svn__end_list(conn, pool));
}

svn_error_t *
svn_ra_svn_flush(svn_ra_svn_conn_t *conn,
                 apr_pool_t *pool)
{
  return svn_error_trace(svn_ra_svn__flush(conn, pool));
}

svn_error_t *
svn_ra_svn_write_tuple(svn_ra_svn_conn_t *conn,
                       apr_pool_t *pool,
                       const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;

  va_start(va, fmt);
  err = svn_ra_svn__write_tuple(conn, pool, fmt, va);
  va_end(va);

  return svn_error_trace(err);
}

svn_error_t *
svn_ra_svn_read_item(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool,
                     svn_ra_svn_item_t **item)
{
  svn_ra_svn__item_t *temp;
  SVN_ERR(svn_ra_svn__read_item(conn, pool, &temp));
  *item  = apr_pcalloc(pool, sizeof(**item));
  svn_ra_svn__to_public_item(*item, temp, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn_skip_leading_garbage(svn_ra_svn_conn_t *conn,
                                apr_pool_t *pool)
{
  return svn_error_trace(svn_ra_svn__skip_leading_garbage(conn, pool));
}

svn_error_t *
svn_ra_svn_parse_tuple(const apr_array_header_t *list,
                       apr_pool_t *pool,
                       const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;
  svn_ra_svn__list_t *internal = svn_ra_svn__to_private_array(list, pool);

  va_start(va, fmt);
  err = svn_ra_svn__parse_tuple(internal, fmt, va);
  va_end(va);

  return svn_error_trace(err);
}

svn_error_t *
svn_ra_svn_read_tuple(svn_ra_svn_conn_t *conn,
                      apr_pool_t *pool,
                      const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;

  va_start(va, fmt);
  err = svn_ra_svn__read_tuple(conn, pool, fmt, va);
  va_end(va);

  return svn_error_trace(err);
}

svn_error_t *
svn_ra_svn_parse_proplist(const apr_array_header_t *list,
                          apr_pool_t *pool,
                          apr_hash_t **props)
{
  svn_ra_svn__list_t *internal
    = svn_ra_svn__to_private_array(list, pool);
  return svn_error_trace(svn_ra_svn__parse_proplist(internal, pool, props));
}

svn_error_t *
svn_ra_svn_read_cmd_response(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;

  va_start(va, fmt);
  err = svn_ra_svn__read_cmd_response(conn, pool, fmt, va);
  va_end(va);

  return svn_error_trace(err);
}

svn_error_t *
svn_ra_svn_handle_commands2(svn_ra_svn_conn_t *conn,
                            apr_pool_t *pool,
                            const svn_ra_svn_cmd_entry_t *commands,
                            void *baton,
                            svn_boolean_t error_on_disconnect)
{
  apr_size_t i, count = 0;
  svn_ra_svn__cmd_entry_t *internal;

  while (commands[count].cmdname)
    count++;

  internal = apr_pcalloc(pool, count * sizeof(*internal));
  for (i = 0; i < count; ++i)
    {
      internal[i].cmdname = commands[i].cmdname;
      internal[i].handler = NULL;
      internal[i].deprecated_handler = commands[i].handler;
      internal[i].terminate = commands[i].terminate;
    }

  return svn_error_trace(svn_ra_svn__handle_commands2(conn, pool,
                                                      internal, baton,
                                                      error_on_disconnect));
}

svn_error_t *
svn_ra_svn_handle_commands(svn_ra_svn_conn_t *conn,
                           apr_pool_t *pool,
                           const svn_ra_svn_cmd_entry_t *commands,
                           void *baton)
{
  return svn_error_trace(svn_ra_svn_handle_commands2(conn, pool,
                                                     commands, baton,
                                                     FALSE));
}

svn_error_t *
svn_ra_svn_write_cmd(svn_ra_svn_conn_t *conn,
                     apr_pool_t *pool,
                     const char *cmdname,
                     const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;

  SVN_ERR(svn_ra_svn__start_list(conn, pool));
  SVN_ERR(svn_ra_svn__write_word(conn, pool, cmdname));
  va_start(va, fmt);
  err = svn_ra_svn__write_tuple(conn, pool, fmt, va);
  va_end(va);
  return err ? svn_error_trace(err) : svn_ra_svn__end_list(conn, pool);
}

svn_error_t *
svn_ra_svn_write_cmd_response(svn_ra_svn_conn_t *conn,
                              apr_pool_t *pool,
                              const char *fmt, ...)
{
  va_list va;
  svn_error_t *err;

  va_start(va, fmt);
  err = svn_ra_svn__write_cmd_response(conn, pool, fmt, va);
  va_end(va);

  return svn_error_trace(err);
}


svn_error_t *
svn_ra_svn_write_cmd_failure(svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_error_t *err)
{
  return svn_error_trace(svn_ra_svn__write_cmd_failure(conn, pool, err));
}

/* From marshal.c */
svn_ra_svn_conn_t *
svn_ra_svn_create_conn4(apr_socket_t *sock,
                        svn_stream_t *in_stream,
                        svn_stream_t *out_stream,
                        int compression_level,
                        apr_size_t zero_copy_limit,
                        apr_size_t error_check_interval,
                        apr_pool_t *pool)
{
  return svn_ra_svn_create_conn5(sock, in_stream, out_stream,
                                 compression_level, zero_copy_limit,
                                 error_check_interval, 0, 0, pool);
}

svn_ra_svn_conn_t *
svn_ra_svn_create_conn3(apr_socket_t *sock,
                        apr_file_t *in_file,
                        apr_file_t *out_file,
                        int compression_level,
                        apr_size_t zero_copy_limit,
                        apr_size_t error_check_interval,
                        apr_pool_t *pool)
{
  svn_stream_t *in_stream = NULL;
  svn_stream_t *out_stream = NULL;

  if (in_file)
    in_stream = svn_stream_from_aprfile2(in_file, FALSE, pool);
  if (out_file)
    out_stream = svn_stream_from_aprfile2(out_file, FALSE, pool);

  return svn_ra_svn_create_conn4(sock, in_stream, out_stream,
                                 compression_level, zero_copy_limit,
                                 error_check_interval, pool);
}

svn_ra_svn_conn_t *
svn_ra_svn_create_conn2(apr_socket_t *sock,
                        apr_file_t *in_file,
                        apr_file_t *out_file,
                        int compression_level,
                        apr_pool_t *pool)
{
  return svn_ra_svn_create_conn3(sock, in_file, out_file,
                                 compression_level, 0, 0, pool);
}

/* backward-compatible implementation using the default compression level */
svn_ra_svn_conn_t *
svn_ra_svn_create_conn(apr_socket_t *sock,
                       apr_file_t *in_file,
                       apr_file_t *out_file,
                       apr_pool_t *pool)
{
  return svn_ra_svn_create_conn3(sock, in_file, out_file,
                                 SVN_DELTA_COMPRESSION_LEVEL_DEFAULT, 0, 0,
                                 pool);
}
