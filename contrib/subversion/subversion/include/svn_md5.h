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
 * @file svn_md5.h
 * @brief Converting and comparing MD5 checksums.
 */

#ifndef SVN_MD5_H
#define SVN_MD5_H

#include <apr_pools.h>  /* for apr_pool_t */

#include "svn_types.h"  /* for svn_boolean_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/**
 * The MD5 digest for the empty string.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 * */
SVN_DEPRECATED
const unsigned char *
svn_md5_empty_string_digest(void);


/**
 * Return the hex representation of @a digest, which must be
 * @c APR_MD5_DIGESTSIZE bytes long, allocating the string in @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
const char *
svn_md5_digest_to_cstring_display(const unsigned char digest[],
                                  apr_pool_t *pool);


/**
 * Return the hex representation of @a digest, which must be
 * @c APR_MD5_DIGESTSIZE bytes long, allocating the string in @a pool.
 * If @a digest is all zeros, then return NULL.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
const char *
svn_md5_digest_to_cstring(const unsigned char digest[],
                          apr_pool_t *pool);


/**
 * Compare digests @a d1 and @a d2, each @c APR_MD5_DIGESTSIZE bytes long.
 * If neither is all zeros, and they do not match, then return FALSE;
 * else return TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_boolean_t
svn_md5_digests_match(const unsigned char d1[],
                      const unsigned char d2[]);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_MD5_H */
