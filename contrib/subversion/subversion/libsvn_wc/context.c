/*
 * context.c:  routines for managing a working copy context
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "wc_db.h"

#include "svn_private_config.h"



/* APR cleanup function used to explicitly close any of our dependent
   data structures before we disappear ourselves. */
static apr_status_t
close_ctx_apr(void *data)
{
  svn_wc_context_t *ctx = data;

  if (ctx->close_db_on_destroy)
    {
      svn_error_t *err = svn_wc__db_close(ctx->db);
      if (err)
        {
          int result = err->apr_err;
          svn_error_clear(err);
          return result;
        }
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_wc_context_create(svn_wc_context_t **wc_ctx,
                      const svn_config_t *config,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc_context_t *ctx = apr_pcalloc(result_pool, sizeof(*ctx));

  /* Create the state_pool, and open up a wc_db in it.
   * Since config contains a private mutable member but C doesn't support
   * we need to make it writable */
  ctx->state_pool = result_pool;
  SVN_ERR(svn_wc__db_open(&ctx->db, (svn_config_t *)config,
                          FALSE, TRUE, ctx->state_pool, scratch_pool));
  ctx->close_db_on_destroy = TRUE;

  apr_pool_cleanup_register(result_pool, ctx, close_ctx_apr,
                            apr_pool_cleanup_null);

  *wc_ctx = ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__context_create_with_db(svn_wc_context_t **wc_ctx,
                               svn_config_t *config,
                               svn_wc__db_t *db,
                               apr_pool_t *result_pool)
{
  svn_wc_context_t *ctx = apr_pcalloc(result_pool, sizeof(*ctx));

  /* Create the state pool.  We don't put the wc_db in it, because it's
     already open in a separate pool somewhere.  We also won't close the
     wc_db when we destroy the context, since it's not ours to close. */
  ctx->state_pool = result_pool;
  ctx->db = db;
  ctx->close_db_on_destroy = FALSE;

  apr_pool_cleanup_register(result_pool, ctx, close_ctx_apr,
                            apr_pool_cleanup_null);

  *wc_ctx = ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_context_destroy(svn_wc_context_t *wc_ctx)
{
  /* We added a cleanup when creating; just run it now to close the context. */
  apr_pool_cleanup_run(wc_ctx->state_pool, wc_ctx, close_ctx_apr);

  return SVN_NO_ERROR;
}
