/*
 * stream_bucket.c : a serf bucket that wraps a readable svn_stream_t
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
#include <serf_bucket_util.h>

#include "ra_serf.h"

typedef struct stream_bucket_ctx_t
{
  svn_stream_t *stream;
  svn_ra_serf__stream_bucket_errfunc_t errfunc;
  void *errfunc_baton;
  serf_databuf_t databuf;
} stream_bucket_ctx_t;

static apr_status_t
stream_reader(void *baton, apr_size_t bufsize, char *buf, apr_size_t *len)
{
  stream_bucket_ctx_t *ctx = baton;
  svn_error_t *err;

  *len = bufsize;

  err = svn_stream_read_full(ctx->stream, buf, len);
  if (err)
    {
      if (ctx->errfunc)
        ctx->errfunc(ctx->errfunc_baton, err);
      svn_error_clear(err);

      return SVN_ERR_RA_SERF_STREAM_BUCKET_READ_ERROR;
    }

  if (*len == bufsize)
    {
      return APR_SUCCESS;
    }
  else
    {
      svn_error_clear(svn_stream_close(ctx->stream));
      return APR_EOF;
    }
}

static apr_status_t
stream_bucket_read(serf_bucket_t *bucket, apr_size_t requested,
                   const char **data, apr_size_t *len)
{
  stream_bucket_ctx_t *ctx = bucket->data;

  return serf_databuf_read(&ctx->databuf, requested, data, len);
}

static apr_status_t
stream_bucket_readline(serf_bucket_t *bucket, int acceptable,
                       int *found, const char **data, apr_size_t *len)
{
  stream_bucket_ctx_t *ctx = bucket->data;

  return serf_databuf_readline(&ctx->databuf, acceptable, found, data, len);
}

static apr_status_t
stream_bucket_peek(serf_bucket_t *bucket, const char **data, apr_size_t *len)
{
  stream_bucket_ctx_t *ctx = bucket->data;

  return serf_databuf_peek(&ctx->databuf, data, len);
}

static const serf_bucket_type_t stream_bucket_vtable = {
  "SVNSTREAM",
  stream_bucket_read,
  stream_bucket_readline,
  serf_default_read_iovec,
  serf_default_read_for_sendfile,
  serf_default_read_bucket,
  stream_bucket_peek,
  serf_default_destroy_and_data
};

serf_bucket_t *
svn_ra_serf__create_stream_bucket(svn_stream_t *stream,
                                  serf_bucket_alloc_t *allocator,
                                  svn_ra_serf__stream_bucket_errfunc_t errfunc,
                                  void *errfunc_baton)
{
  stream_bucket_ctx_t *ctx;

  ctx = serf_bucket_mem_calloc(allocator, sizeof(*ctx));
  ctx->stream = stream;
  ctx->errfunc = errfunc;
  ctx->errfunc_baton = errfunc_baton;
  serf_databuf_init(&ctx->databuf);
  ctx->databuf.read = stream_reader;
  ctx->databuf.read_baton = ctx;

  return serf_bucket_create(&stream_bucket_vtable, allocator, ctx);
}
