/*
 * request_body.c :  svn_ra_serf__request_body_t implementation
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

#include "ra_serf.h"

struct svn_ra_serf__request_body_t
{
  svn_stream_t *stream;
  apr_size_t in_memory_size;
  apr_size_t total_bytes;
  serf_bucket_alloc_t *alloc;
  serf_bucket_t *collect_bucket;
  const void *all_data;
  apr_file_t *file;
  apr_pool_t *result_pool;
  apr_pool_t *scratch_pool;
};

/* Fold all previously collected data in a single buffer allocated in
   RESULT_POOL and clear all intermediate state. */
static const char *
allocate_all(svn_ra_serf__request_body_t *body,
             apr_pool_t *result_pool)
{
  char *buffer = apr_pcalloc(result_pool, body->total_bytes);
  const char *data;
  apr_size_t sz;
  apr_status_t s;
  apr_size_t remaining = body->total_bytes;
  char *next = buffer;

  while (!(s = serf_bucket_read(body->collect_bucket, remaining, &data, &sz)))
    {
      memcpy(next, data, sz);
      remaining -= sz;
      next += sz;

      if (! remaining)
        break;
    }

  if (!SERF_BUCKET_READ_ERROR(s))
    {
      memcpy(next, data, sz);
    }

  serf_bucket_destroy(body->collect_bucket);
  body->collect_bucket = NULL;

  return (s != APR_EOF) ? NULL : buffer;
}

/* Noop function.  Make serf take care of freeing in error situations. */
static void serf_free_no_error(void *unfreed_baton, void *block) {}

/* Stream write function for body creation. */
static svn_error_t *
request_body_stream_write(void *baton,
                          const char *data,
                          apr_size_t *len)
{
  svn_ra_serf__request_body_t *b = baton;

  if (!b->scratch_pool)
    b->scratch_pool = svn_pool_create(b->result_pool);

  if (b->file)
    {
      SVN_ERR(svn_io_file_write_full(b->file, data, *len, NULL,
                                     b->scratch_pool));
      svn_pool_clear(b->scratch_pool);

      b->total_bytes += *len;
    }
  else if (*len + b->total_bytes > b->in_memory_size)
    {
      SVN_ERR(svn_io_open_unique_file3(&b->file, NULL, NULL,
                                       svn_io_file_del_on_pool_cleanup,
                                       b->result_pool, b->scratch_pool));

      if (b->total_bytes)
        {
          const char *all = allocate_all(b, b->scratch_pool);

          SVN_ERR(svn_io_file_write_full(b->file, all, b->total_bytes,
                                         NULL, b->scratch_pool));
        }

      SVN_ERR(svn_io_file_write_full(b->file, data, *len, NULL,
                                     b->scratch_pool));
      b->total_bytes += *len;
    }
  else
    {
      if (!b->alloc)
        b->alloc = serf_bucket_allocator_create(b->scratch_pool,
                                                serf_free_no_error, NULL);

      if (!b->collect_bucket)
        b->collect_bucket = serf_bucket_aggregate_create(b->alloc);

      serf_bucket_aggregate_append(b->collect_bucket,
                                   serf_bucket_simple_copy_create(data, *len,
                                                                  b->alloc));

      b->total_bytes += *len;
    }

  return SVN_NO_ERROR;
}

/* Stream close function for collecting body. */
static svn_error_t *
request_body_stream_close(void *baton)
{
  svn_ra_serf__request_body_t *b = baton;

  if (b->file)
    {
      /* We need to flush the file, make it unbuffered (so that it can be
       * zero-copied via mmap), and reset the position before attempting
       * to deliver the file.
       *
       * N.B. If we have APR 1.3+, we can unbuffer the file to let us use
       * mmap and zero-copy the PUT body.  However, on older APR versions,
       * we can't check the buffer status; but serf will fall through and
       * create a file bucket for us on the buffered handle.
       */

      SVN_ERR(svn_io_file_flush(b->file, b->scratch_pool));
      apr_file_buffer_set(b->file, NULL, 0);
    }
  else if (b->collect_bucket)
    b->all_data = allocate_all(b, b->result_pool);

  if (b->scratch_pool)
    svn_pool_destroy(b->scratch_pool);

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t. */
static svn_error_t *
request_body_delegate(serf_bucket_t **body_bkt,
                      void *baton,
                      serf_bucket_alloc_t *alloc,
                      apr_pool_t *request_pool,
                      apr_pool_t *scratch_pool)
{
  svn_ra_serf__request_body_t *b = baton;

  if (b->file)
    {
      apr_off_t offset;

      offset = 0;
      SVN_ERR(svn_io_file_seek(b->file, APR_SET, &offset, scratch_pool));

      *body_bkt = serf_bucket_file_create(b->file, alloc);
    }
  else
    {
      *body_bkt = serf_bucket_simple_create(b->all_data,
                                            b->total_bytes,
                                            NULL, NULL, alloc);
    }

  return SVN_NO_ERROR;
}

svn_ra_serf__request_body_t *
svn_ra_serf__request_body_create(apr_size_t in_memory_size,
                                 apr_pool_t *result_pool)
{
  svn_ra_serf__request_body_t *body = apr_pcalloc(result_pool, sizeof(*body));

  body->in_memory_size = in_memory_size;
  body->result_pool = result_pool;
  body->stream = svn_stream_create(body, result_pool);

  svn_stream_set_write(body->stream, request_body_stream_write);
  svn_stream_set_close(body->stream, request_body_stream_close);

  return body;
}

svn_stream_t *
svn_ra_serf__request_body_get_stream(svn_ra_serf__request_body_t *body)
{
  return body->stream;
}

void
svn_ra_serf__request_body_get_delegate(svn_ra_serf__request_body_delegate_t *del,
                                       void **baton,
                                       svn_ra_serf__request_body_t *body)
{
  *del = request_body_delegate;
  *baton = body;
}

svn_error_t *
svn_ra_serf__request_body_cleanup(svn_ra_serf__request_body_t *body,
                                  apr_pool_t *scratch_pool)
{
  if (body->file)
    SVN_ERR(svn_io_file_close(body->file, scratch_pool));

  return SVN_NO_ERROR;
}
