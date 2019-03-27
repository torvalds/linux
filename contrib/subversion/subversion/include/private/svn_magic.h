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
 *
 * @file svn_magic.h
 * @brief Subversion interface to libmagic.
 */

#ifndef SVN_MAGIC_H
#define SVN_MAGIC_H

/* An opaque struct that wraps a libmagic cookie. */
typedef struct svn_magic__cookie_t svn_magic__cookie_t;

/* This routine initialises libmagic.  CONFIG is a config hash and
 * may be NULL.
 * Upon success a new *MAGIC_COOKIE is allocated in RESULT_POOL.
 * On failure *MAGIC_COOKIE is set to NULL.
 * All resources used by libmagic are freed by a cleanup handler
 * installed on RESULT_POOL, i.e. *MAGIC_COOKIE becomes invalid when
 * the pool is cleared! */
svn_error_t *
svn_magic__init(svn_magic__cookie_t **magic_cookie,
                apr_hash_t *config,
                apr_pool_t *result_pool);

/* Detect the mime-type of the file at LOCAL_ABSPATH using MAGIC_COOKIE.
 * If the mime-type is binary return the result in *MIMETYPE.
 * If the file is not a binary file or if its mime-type cannot be determined
 * set *MIMETYPE to NULL. Allocate *MIMETYPE in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_magic__detect_binary_mimetype(const char **mimetype,
                                  const char *local_abspath,
                                  svn_magic__cookie_t *magic_cookie,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

#endif /* SVN_MAGIC_H */
