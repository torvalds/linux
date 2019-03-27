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
 * @file svn_base64.h
 * @brief Base64 encoding and decoding functions
 */

#ifndef SVN_BASE64_H
#define SVN_BASE64_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_io.h"     /* for svn_stream_t */
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 *
 *
 * @defgroup base64 Base64 encoding/decoding functions
 *
 * @{
 */

/** Return a writable generic stream which will encode binary data in
 * base64 format and write the encoded data to @a output.  If @a break_lines
 * is true, newlines will be inserted periodically; otherwise the output
 * stream will only consist of base64 encoding characters. Be sure to close
 * the stream when done writing in order to squeeze out the last bit of
 * encoded data.  The stream is allocated in @a pool.
 *
 * @since New in 1.10.
 */
svn_stream_t *
svn_base64_encode2(svn_stream_t *output,
                   svn_boolean_t break_lines,
                   apr_pool_t *pool);

/**
 * Same as svn_base64_encode2, but with @a break_lines always TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.9 API.
 */
SVN_DEPRECATED
svn_stream_t *
svn_base64_encode(svn_stream_t *output,
                  apr_pool_t *pool);

/** Return a writable generic stream which will decode base64-encoded
 * data and write the decoded data to @a output.  The stream is allocated
 * in @a pool.
 */
svn_stream_t *
svn_base64_decode(svn_stream_t *output,
                  apr_pool_t *pool);


/** Encode an @c svn_stringbuf_t into base64.
 *
 * A simple interface for encoding base64 data assuming we have all of
 * it present at once.  If @a break_lines is true, newlines will be
 * inserted periodically; otherwise the string will only consist of
 * base64 encoding characters.  The returned string will be allocated
 * from @a pool.
 *
 * @since New in 1.6.
 */
const svn_string_t *
svn_base64_encode_string2(const svn_string_t *str,
                          svn_boolean_t break_lines,
                          apr_pool_t *pool);

/**
 * Same as svn_base64_encode_string2, but with @a break_lines always
 * TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
const svn_string_t *
svn_base64_encode_string(const svn_string_t *str,
                         apr_pool_t *pool);

/** Decode an @c svn_stringbuf_t from base64.
 *
 * A simple interface for decoding base64 data assuming we have all of
 * it present at once.  The returned string will be allocated from @c
 * pool.
 *
 */
const svn_string_t *
svn_base64_decode_string(const svn_string_t *str,
                         apr_pool_t *pool);


/** Return a base64-encoded checksum for finalized @a digest.
 *
 * @a digest contains @c APR_MD5_DIGESTSIZE bytes of finalized data.
 * Allocate the returned checksum in @a pool.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_stringbuf_t *
svn_base64_from_md5(unsigned char digest[],
                    apr_pool_t *pool);


/** @} end group: Base64 encoding/decoding functions */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BASE64_H */
