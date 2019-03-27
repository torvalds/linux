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
 * @file svn_eol_private.h
 * @brief Subversion's EOL functions - Internal routines
 */

#ifndef SVN_EOL_PRIVATE_H
#define SVN_EOL_PRIVATE_H

#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Constants used by various chunky string processing functions.
 */
#if APR_SIZEOF_VOIDP == 8
#  define SVN__LOWER_7BITS_SET 0x7f7f7f7f7f7f7f7f
#  define SVN__BIT_7_SET       0x8080808080808080
#  define SVN__R_MASK          0x0a0a0a0a0a0a0a0a
#  define SVN__N_MASK          0x0d0d0d0d0d0d0d0d
#else
#  define SVN__LOWER_7BITS_SET 0x7f7f7f7f
#  define SVN__BIT_7_SET       0x80808080
#  define SVN__R_MASK          0x0a0a0a0a
#  define SVN__N_MASK          0x0d0d0d0d
#endif

/* Generic EOL character helper routines */

/* Look for the start of an end-of-line sequence (i.e. CR or LF)
 * in the array pointed to by @a buf , of length @a len.
 * If such a byte is found, return the pointer to it, else return NULL.
 *
 * @since New in 1.7
 */
char *
svn_eol__find_eol_start(char *buf, apr_size_t len);

/* Return the first eol marker found in buffer @a buf as a NUL-terminated
 * string, or NULL if no eol marker is found. Do not examine more than
 * @a len bytes in @a buf.
 *
 * If the last valid character of @a buf is the first byte of a
 * potentially two-byte eol sequence, just return that single-character
 * sequence, that is, assume @a buf represents a CR-only or LF-only file.
 * This is correct for callers that pass an entire file at once, and is
 * no more likely to be incorrect than correct for any caller that doesn't.
 *
 * The returned string is statically allocated, i.e. it is NOT a pointer
 * to an address within @a buf.
 *
 * If an eol marker is found and @a eolp is not NULL, store in @a *eolp
 * the address within @a buf of the first byte of the eol marker.
 * This allows callers to tell whether there might be more than one eol
 * sequence in @a buf, as well as detect two-byte eol sequences that
 * span buffer boundaries.
 *
 * @since New in 1.7
 */
const char *
svn_eol__detect_eol(char *buf, apr_size_t len, char **eolp);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EOL_PRIVATE_H */
