/**
 * @copyright
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
 * @endcopyright
 */



#ifndef DEPRECATED_H
#define DEPRECATED_H

#include <apr_hash.h>

#include "private/svn_editor.h"


#ifdef __cplusplus
extern "C" {
#endif

/* Non-deprecated wrapper around svn_ra_local_init. */
svn_error_t *
svn_ra_local__deprecated_init(int abi_version,
                              apr_pool_t *pool,
                              apr_hash_t *hash);

/* Non-deprecated wrapper around svn_ra_svn_init. */
svn_error_t *
svn_ra_svn__deprecated_init(int abi_version,
                            apr_pool_t *pool,
                            apr_hash_t *hash);

/* Non-deprecated wrapper around svn_ra_serf_init. */
svn_error_t *
svn_ra_serf__deprecated_init(int abi_version,
                             apr_pool_t *pool,
                             apr_hash_t *hash);

#ifdef __cplusplus
}
#endif

#endif /* DEPRECATED_H */
