/*
 * eagain_bucket.c :  a serf bucket that handles slowing down data
 *                   for specific readers that would have unwanted
 *                   behavior if they read everything too fast
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

#include "svn_private_config.h"

#include "ra_serf.h"

typedef struct eagain_baton_t
{
  const char *data;
  apr_size_t remaining;
} eagain_baton_t;

static apr_status_t
eagain_bucket_read(serf_bucket_t *bucket,
                   apr_size_t requested,
                   const char **data,
                   apr_size_t *len)
{
  eagain_baton_t *eab = bucket->data;

  if (eab->remaining > 0)
    {
      *data = eab->data;
      if (requested > eab->remaining || requested == SERF_READ_ALL_AVAIL)
        {
          *len = eab->remaining;
          eab->data = NULL;
          eab->remaining = 0;
        }
      else
        {
          *len = requested;
          eab->data += requested;
          eab->remaining -= requested;
        }

      if (eab->remaining)
        return APR_SUCCESS;
    }

  return APR_EAGAIN;
}

#if !SERF_VERSION_AT_LEAST(1, 4, 0)
static apr_status_t
eagain_bucket_readline(serf_bucket_t *bucket,
                       int acceptable,
                       int *found,
                       const char **data,
                       apr_size_t *len)
{
  /* ### for now, we know callers won't use this function.  */
  svn_error_clear(svn_error__malfunction(TRUE, __FILE__, __LINE__,
                                         "Not implemented."));
  return APR_ENOTIMPL;
}
#endif


static apr_status_t
eagain_bucket_peek(serf_bucket_t *bucket,
                   const char **data,
                   apr_size_t *len)
{
  const eagain_baton_t *eab = bucket->data;

  *data = eab->data ? eab->data : "";
  *len = eab->remaining;

  return APR_SUCCESS;
}


static const serf_bucket_type_t delay_bucket_vtable = {
    "BUF-EAGAIN",
    eagain_bucket_read,
#if SERF_VERSION_AT_LEAST(1, 4, 0)
    serf_default_readline,
#else
    eagain_bucket_readline,
#endif
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    eagain_bucket_peek,
    serf_default_destroy_and_data,
};


serf_bucket_t *
svn_ra_serf__create_bucket_with_eagain(const char *data,
                                       apr_size_t len,
                                       serf_bucket_alloc_t *allocator)
{
  eagain_baton_t *eab;

  eab = serf_bucket_mem_alloc(allocator, sizeof(*eab));
  eab->data = data;
  eab->remaining = len;

  return serf_bucket_create(&delay_bucket_vtable, allocator, eab);
}
