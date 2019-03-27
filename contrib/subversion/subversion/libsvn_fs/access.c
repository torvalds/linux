/*
 * access.c:  shared code to manipulate svn_fs_access_t objects
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


#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_fs.h"
#include "private/svn_fs_private.h"

#include "fs-loader.h"



svn_error_t *
svn_fs_create_access(svn_fs_access_t **access_ctx,
                     const char *username,
                     apr_pool_t *pool)
{
  svn_fs_access_t *ac;

  SVN_ERR_ASSERT(username != NULL);

  ac = apr_pcalloc(pool, sizeof(*ac));
  ac->username = apr_pstrdup(pool, username);
  ac->lock_tokens = apr_hash_make(pool);
  *access_ctx = ac;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_set_access(svn_fs_t *fs,
                  svn_fs_access_t *access_ctx)
{
  fs->access_ctx = access_ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_get_access(svn_fs_access_t **access_ctx,
                  svn_fs_t *fs)
{
  *access_ctx = fs->access_ctx;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_access_get_username(const char **username,
                           svn_fs_access_t *access_ctx)
{
  *username = access_ctx->username;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_access_add_lock_token2(svn_fs_access_t *access_ctx,
                              const char *path,
                              const char *token)
{
  svn_hash_sets(access_ctx->lock_tokens, token, path);
  return SVN_NO_ERROR;
}

apr_hash_t *
svn_fs__access_get_lock_tokens(svn_fs_access_t *access_ctx)
{
  return access_ctx->lock_tokens;
}
