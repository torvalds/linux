/*
 * deprecated.c:  Wrappers to call deprecated functions.
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

#define SVN_DEPRECATED
#include "cl.h"
#include "svn_client.h"

svn_error_t *
svn_cl__deprecated_merge_reintegrate(const char *source_path_or_url,
                                     const svn_opt_revision_t *src_peg_revision,
                                     const char *target_wcpath,
                                     svn_boolean_t dry_run,
                                     const apr_array_header_t *merge_options,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool)
{
  SVN_ERR(svn_client_merge_reintegrate(source_path_or_url, src_peg_revision,
                                       target_wcpath, dry_run, merge_options,
                                       ctx, pool));
  return SVN_NO_ERROR;
}
