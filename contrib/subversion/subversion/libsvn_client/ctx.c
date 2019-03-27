/*
 * ctx.c:  initialization function for client context
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

/* ==================================================================== */



/*** Includes. ***/

#include <stddef.h>
#include <apr_pools.h>
#include "svn_hash.h"
#include "svn_client.h"
#include "svn_error.h"

#include "private/svn_wc_private.h"

#include "client.h"


/*** Code. ***/

/* Call the notify_func of the context provided by BATON, if non-NULL. */
static void
call_notify_func(void *baton, const svn_wc_notify_t *n, apr_pool_t *pool)
{
  const svn_client_ctx_t *ctx = baton;

  if (ctx->notify_func)
    ctx->notify_func(ctx->notify_baton, n->path, n->action, n->kind,
                     n->mime_type, n->content_state, n->prop_state,
                     n->revision);
}

static svn_error_t *
call_conflict_func(svn_wc_conflict_result_t **result,
                   const svn_wc_conflict_description2_t *conflict,
                   void *baton,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_client_ctx_t *ctx = baton;

  if (ctx->conflict_func)
    {
      const svn_wc_conflict_description_t *cd;

      cd = svn_wc__cd2_to_cd(conflict, scratch_pool);
      SVN_ERR(ctx->conflict_func(result, cd, ctx->conflict_baton,
                                 result_pool));
    }
  else
    {
      /* We have to set a result; so we postpone */
      *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                              NULL, result_pool);
    }

  return SVN_NO_ERROR;
}

/* The magic number in client_ctx_t.magic_id. */
#define CLIENT_CTX_MAGIC APR_UINT64_C(0xDEADBEEF600DF00D)

svn_client__private_ctx_t *
svn_client__get_private_ctx(svn_client_ctx_t *ctx)
{
  svn_client__private_ctx_t *const private_ctx =
    (void*)((char *)ctx - offsetof(svn_client__private_ctx_t, public_ctx));
  SVN_ERR_ASSERT_NO_RETURN(&private_ctx->public_ctx == ctx);
  SVN_ERR_ASSERT_NO_RETURN(0 == private_ctx->magic_null);
  SVN_ERR_ASSERT_NO_RETURN(CLIENT_CTX_MAGIC == private_ctx->magic_id);
  return private_ctx;
}

svn_error_t *
svn_client_create_context2(svn_client_ctx_t **ctx,
                           apr_hash_t *cfg_hash,
                           apr_pool_t *pool)
{
  svn_config_t *cfg_config;

  svn_client__private_ctx_t *const private_ctx =
    apr_pcalloc(pool, sizeof(*private_ctx));
  svn_client_ctx_t *const public_ctx = &private_ctx->public_ctx;

  private_ctx->magic_null = 0;
  private_ctx->magic_id = CLIENT_CTX_MAGIC;

  public_ctx->notify_func2 = call_notify_func;
  public_ctx->notify_baton2 = public_ctx;

  public_ctx->conflict_func2 = call_conflict_func;
  public_ctx->conflict_baton2 = public_ctx;

  public_ctx->config = cfg_hash;

  if (cfg_hash)
    cfg_config = svn_hash_gets(cfg_hash, SVN_CONFIG_CATEGORY_CONFIG);
  else
    cfg_config = NULL;

  SVN_ERR(svn_wc_context_create(&public_ctx->wc_ctx, cfg_config,
                                pool, pool));
  *ctx = public_ctx;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_create_context(svn_client_ctx_t **ctx,
                          apr_pool_t *pool)
{
  return svn_client_create_context2(ctx, NULL, pool);
}
