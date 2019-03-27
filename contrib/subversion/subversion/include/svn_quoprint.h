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
 * @file svn_quoprint.h
 * @brief quoted-printable encoding and decoding functions.
 */

#ifndef SVN_QUOPRINT_H
#define SVN_QUOPRINT_H

#include <apr_pools.h>

#include "svn_string.h"  /* for svn_strinbuf_t */
#include "svn_io.h"      /* for svn_stream_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Return a writable generic stream which will encode binary data in
 * quoted-printable format and write the encoded data to @a output.  Be
 * sure to close the stream when done writing in order to squeeze out
 * the last bit of encoded data.
 */
svn_stream_t *
svn_quoprint_encode(svn_stream_t *output,
                    apr_pool_t *pool);

/** Return a writable generic stream which will decode binary data in
 * quoted-printable format and write the decoded data to @a output.  Be
 * sure to close the stream when done writing in order to squeeze out
 * the last bit of encoded data.
 */
svn_stream_t *
svn_quoprint_decode(svn_stream_t *output,
                    apr_pool_t *pool);


/** Simpler interface for encoding quoted-printable data assuming we have all
 * of it present at once.  The returned string will be allocated from @a pool.
 */
svn_stringbuf_t *
svn_quoprint_encode_string(const svn_stringbuf_t *str,
                           apr_pool_t *pool);

/** Simpler interface for decoding quoted-printable data assuming we have all
 * of it present at once.  The returned string will be allocated from @a pool.
 */
svn_stringbuf_t *
svn_quoprint_decode_string(const svn_stringbuf_t *str,
                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_QUOPRINT_H */
