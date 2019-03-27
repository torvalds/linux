/*
 * revision_status.c: report the revision range and status of a working copy
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

#include "svn_wc.h"
#include "svn_dirent_uri.h"
#include "wc_db.h"
#include "wc.h"
#include "props.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

svn_error_t *
svn_wc_revision_status2(svn_wc_revision_status_t **result_p,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const char *trail_url,
                        svn_boolean_t committed,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc_revision_status_t *result = apr_pcalloc(result_pool, sizeof(*result));

  *result_p = result;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* set result as nil */
  result->min_rev  = SVN_INVALID_REVNUM;
  result->max_rev  = SVN_INVALID_REVNUM;
  result->switched = FALSE;
  result->modified = FALSE;
  result->sparse_checkout = FALSE;

  SVN_ERR(svn_wc__db_revision_status(&result->min_rev, &result->max_rev,
                                     &result->sparse_checkout,
                                     &result->modified,
                                     &result->switched,
                                     wc_ctx->db, local_abspath, trail_url,
                                     committed,
                                     scratch_pool));

  if (!result->modified)
    SVN_ERR(svn_wc__node_has_local_mods(&result->modified, NULL,
                                        wc_ctx->db, local_abspath, TRUE,
                                        cancel_func, cancel_baton,
                                        scratch_pool));

  return SVN_NO_ERROR;
}
