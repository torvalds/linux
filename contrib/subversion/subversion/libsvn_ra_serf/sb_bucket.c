/*
 * sb_bucket.c :  a serf bucket that wraps a spillbuf
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

#include "svn_private_config.h"
#include "private/svn_subr_private.h"

#include "ra_serf.h"

#define SB_BLOCKSIZE 1024
#define SB_MAXSIZE 32768


struct sbb_baton
{
  svn_spillbuf_t *spillbuf;

  const char *holding;
  apr_size_t hold_len;

  apr_pool_t *scratch_pool;
};


svn_error_t *
svn_ra_serf__copy_into_spillbuf(svn_spillbuf_t **spillbuf,
                                serf_bucket_t *bkt,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  *spillbuf = svn_spillbuf__create(SB_BLOCKSIZE, SB_MAXSIZE, result_pool);

  /* Copy all data from the bucket into the spillbuf.  */
  while (TRUE)
    {
      apr_status_t status;
      const char *data;
      apr_size_t len;

      status = serf_bucket_read(bkt, SERF_READ_ALL_AVAIL, &data, &len);

      if (status != APR_SUCCESS && status != APR_EOF)
        return svn_ra_serf__wrap_err(status, _("Failed to read the request"));

      SVN_ERR(svn_spillbuf__write(*spillbuf, data, len, scratch_pool));

      if (status == APR_EOF)
        break;
    }

  return SVN_NO_ERROR;
}


static apr_status_t
sb_bucket_read(serf_bucket_t *bucket, apr_size_t requested,
               const char **data, apr_size_t *len)
{
  struct sbb_baton *sbb = bucket->data;
  svn_error_t *err;

  if (sbb->holding)
    {
      *data = sbb->holding;

      if (requested < sbb->hold_len)
        {
          *len = requested;
          sbb->holding += requested;
          sbb->hold_len -= requested;
          return APR_SUCCESS;
        }

      /* Return whatever we're holding, and then forget (consume) it.  */
      *len = sbb->hold_len;
      sbb->holding = NULL;
      return APR_SUCCESS;
    }

  err = svn_spillbuf__read(data, len, sbb->spillbuf, sbb->scratch_pool);
  svn_pool_clear(sbb->scratch_pool);

  /* ### do something with this  */
  svn_error_clear(err);

  /* The spillbuf may have returned more than requested. Stash any extra
     into our holding area.  */
  if (requested < *len)
    {
      sbb->holding = *data + requested;
      sbb->hold_len = *len - requested;
      *len = requested;
    }

  return *data == NULL ? APR_EOF : APR_SUCCESS;
}

#if !SERF_VERSION_AT_LEAST(1, 4, 0)
static apr_status_t
sb_bucket_readline(serf_bucket_t *bucket, int acceptable,
                   int *found,
                   const char **data, apr_size_t *len)
{
  /* ### for now, we know callers won't use this function.  */
  svn_error_clear(svn_error__malfunction(TRUE, __FILE__, __LINE__,
                                         "Not implemented."));
  return APR_ENOTIMPL;
}
#endif


static apr_status_t
sb_bucket_peek(serf_bucket_t *bucket,
               const char **data, apr_size_t *len)
{
  struct sbb_baton *sbb = bucket->data;
  svn_error_t *err;

  /* If we're not holding any data, then fill it.  */
  if (sbb->holding == NULL)
    {
      err = svn_spillbuf__read(&sbb->holding, &sbb->hold_len, sbb->spillbuf,
                               sbb->scratch_pool);
      svn_pool_clear(sbb->scratch_pool);

      /* ### do something with this  */
      svn_error_clear(err);
    }

  /* Return the data we are (now) holding.  */
  *data = sbb->holding;
  *len = sbb->hold_len;

  return *data == NULL ? APR_EOF : APR_SUCCESS;
}


static const serf_bucket_type_t sb_bucket_vtable = {
    "SPILLBUF",
    sb_bucket_read,
#if SERF_VERSION_AT_LEAST(1, 4, 0)
    serf_default_readline,
#else
    sb_bucket_readline,
#endif
    serf_default_read_iovec,
    serf_default_read_for_sendfile,
    serf_default_read_bucket,
    sb_bucket_peek,
    serf_default_destroy_and_data,
};


serf_bucket_t *
svn_ra_serf__create_sb_bucket(svn_spillbuf_t *spillbuf,
                              serf_bucket_alloc_t *allocator,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  struct sbb_baton *sbb;

  sbb = serf_bucket_mem_alloc(allocator, sizeof(*sbb));
  sbb->spillbuf = spillbuf;
  sbb->holding = NULL;
  sbb->scratch_pool = svn_pool_create(result_pool);

  return serf_bucket_create(&sb_bucket_vtable, allocator, sbb);
}
