/*
 *  util.c: A few utility functions.
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

#include "private/svn_repos_private.h"

#include "svnrdump.h"


svn_error_t *
svn_rdump__normalize_props(apr_hash_t **normal_props,
                           apr_hash_t *props,
                           apr_pool_t *result_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  *normal_props = apr_hash_make(result_pool);

  iterpool = svn_pool_create(result_pool);
  for (hi = apr_hash_first(result_pool, props); hi;
        hi = apr_hash_next(hi))
    {
      const char *key = apr_hash_this_key(hi);
      const svn_string_t *value = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_repos__normalize_prop(&value, NULL, key, value,
                                        result_pool, iterpool));
      svn_hash_sets(*normal_props, key, value);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
