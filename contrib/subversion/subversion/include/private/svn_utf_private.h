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
 * @file svn_utf_private.h
 * @brief UTF validation and normalization routines
 */

#ifndef SVN_UTF_PRIVATE_H
#define SVN_UTF_PRIVATE_H

#include <apr.h>
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_string_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Return TRUE if the string SRC of length LEN is a valid UTF-8 encoding
 * according to the rules laid down by the Unicode 4.0 standard, FALSE
 * otherwise.  This function is faster than svn_utf__last_valid().
 */
svn_boolean_t
svn_utf__is_valid(const char *src, apr_size_t len);

/* As for svn_utf__is_valid but SRC is NULL terminated. */
svn_boolean_t
svn_utf__cstring_is_valid(const char *src);

/* Return a pointer to the first character after the last valid UTF-8
 * potentially multi-byte character in the string SRC of length LEN.
 * Validity of bytes from SRC to SRC+LEN-1, inclusively, is checked.
 * If SRC is a valid UTF-8, the return value will point to the byte SRC+LEN,
 * otherwise it will point to the start of the first invalid character.
 * In either case all the characters between SRC and the return pointer - 1,
 * inclusively, are valid UTF-8.
 *
 * See also svn_utf__is_valid().
 */
const char *
svn_utf__last_valid(const char *src, apr_size_t len);

/* As for svn_utf__last_valid but uses a different implementation without
   lookup tables.  It avoids the table memory use (about 400 bytes) but the
   function is longer (about 200 bytes extra) and likely to be slower when
   the string is valid.  If the string is invalid this function may be
   faster since it returns immediately rather than continuing to the end of
   the string.  The main reason this function exists is to test the table
   driven implementation.  */
const char *
svn_utf__last_valid2(const char *src, apr_size_t len);

/* Copy LENGTH bytes of SRC, converting characters as follows:
    - Pass characters from the ASCII subset to the result
    - Strip all combining marks from the string
    - Represent other valid Unicode chars as {U+XXXX}
    - Replace invalid Unicode chars with {U?XXXX}
    - Represent chars that are not valid UTF-8 as ?\XX
    - Replace codes outside the Unicode range with a sequence of ?\XX
    - Represent the null byte as \0
   Allocate the result in POOL. */
const char *
svn_utf__fuzzy_escape(const char *src, apr_size_t length, apr_pool_t *pool);

const char *
svn_utf__cstring_from_utf8_fuzzy(const char *src,
                                 apr_pool_t *pool,
                                 svn_error_t *(*convert_from_utf8)
                                              (const char **,
                                               const char *,
                                               apr_pool_t *));


#if defined(WIN32)
/* On Windows: Convert the UTF-8 string SRC to UTF-16.
   If PREFIX is not NULL, prepend it to the converted result.
   The result, if not empty, will be allocated in RESULT_POOL. */
svn_error_t *
svn_utf__win32_utf8_to_utf16(const WCHAR **result,
                             const char *src,
                             const WCHAR *prefix,
                             apr_pool_t *result_pool);

/* On Windows: Convert the UTF-16 string SRC to UTF-8.
   If PREFIX is not NULL, prepend it to the converted result.
   The result, if not empty, will be allocated in RESULT_POOL. */
svn_error_t *
svn_utf__win32_utf16_to_utf8(const char **result,
                             const WCHAR *src,
                             const char *prefix,
                             apr_pool_t *result_pool);
#endif /* WIN32*/


/* A constant used for many length parameters in the utf8proc wrappers
 * to indicate that the length of a string is unknonw. */
#define SVN_UTF__UNKNOWN_LENGTH ((apr_size_t) -1)


/* Compare two UTF-8 strings, ignoring normalization, using buffers
 * BUF1 and BUF2 for temporary storage. If either of LEN1 or LEN2 is
 * SVN_UTF__UNKNOWN_LENGTH, assume the associated string is
 * null-terminated; otherwise, consider the string only up to the
 * given length.
 *
 * Return compare value in *RESULT.
 */
svn_error_t *
svn_utf__normcmp(int *result,
                 const char *str1, apr_size_t len1,
                 const char *str2, apr_size_t len2,
                 svn_membuf_t *buf1, svn_membuf_t *buf2);

/* Normalize the UTF-8 string STR to form C, using BUF for temporary
 * storage. If LEN is SVN_UTF__UNKNOWN_LENGTH, assume STR is
 * null-terminated; otherwise, consider the string only up to the
 * given length.
 *
 * Return the normalized string in *RESULT, which shares storage with
 * BUF and is valid only until the next time BUF is modified.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints.
 */
svn_error_t*
svn_utf__normalize(const char **result,
                   const char *str, apr_size_t len,
                   svn_membuf_t *buf);

/* Transform the UTF-8 string to a shape suitable for comparison with
 * strcmp(). The tranformation is defined by CASE_INSENSITIVE and
 * ACCENT_INSENSITIVE arguments. If CASE_INSENSITIVE is non-zero,
 * remove case distinctions from the string. If ACCENT_INSENSITIVE
 * is non-zero, remove diacritical marks from the string.
 *
 * Use BUF as a temporary storage. If LEN is SVN_UTF__UNKNOWN_LENGTH,
 * assume STR is null-terminated; otherwise, consider the string only
 * up to the given length. Place the tranformed string in *RESULT, which
 * shares storage with BUF and is valid only until the next time BUF is
 * modified.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints.
 */
svn_error_t *
svn_utf__xfrm(const char **result,
              const char *str, apr_size_t len,
              svn_boolean_t case_insensitive,
              svn_boolean_t accent_insensitive,
              svn_membuf_t *buf);

/* Return TRUE if S matches any of the const char * glob patterns in
 * PATTERNS.
 *
 * S will internally be normalized to lower-case and accents removed
 * using svn_utf__xfrm.  To get a match, the PATTERNS must have been
 * normalized accordingly before calling this function.
 */
svn_boolean_t
svn_utf__fuzzy_glob_match(const char *str,
                          const apr_array_header_t *patterns,
                          svn_membuf_t *buf);

/* Check if STRING is a valid, NFC-normalized UTF-8 string.  Note that
 * a FALSE return value may indicate that STRING is not valid UTF-8 at
 * all.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_boolean_t
svn_utf__is_normalized(const char *string, apr_pool_t *scratch_pool);

/* Encode an UCS-4 string to UTF-8, placing the result into BUFFER.
 * While utf8proc does have a similar function, it does more checking
 * and processing than we want here; this function does not attempt
 * any normalizations but just encodes the individual code points.
 * The encoded string will always be NUL-terminated.
 *
 * Return the length of the result (excluding the NUL terminator) in
 * *result_length.
 *
 * A returned error indicates that a codepoint is invalid.
 */
svn_error_t *
svn_utf__encode_ucs4_string(svn_membuf_t *buffer,
                            const apr_int32_t *ucs4str,
                            apr_size_t length,
                            apr_size_t *result_length);

/* Pattern matching similar to the SQLite LIKE and GLOB
 * operators. PATTERN, KEY and ESCAPE must all point to UTF-8
 * strings. Furthermore, ESCAPE, if provided, must be a character from
 * the ASCII subset.
 *
 * If any of PATTERN_LEN, STRING_LEN or ESCAPE_LEN are
 * SVN_UTF__UNKNOWN_LENGTH, assume the associated string is
 * null-terminated; otherwise, consider the string only up to the
 * given length.
 *
 * Use buffers PATTERN_BUF, STRING_BUF and TEMP_BUF for temporary storage.
 *
 * If SQL_LIKE is true, interpret PATTERN as a pattern used by the SQL
 * LIKE operator and notice ESCAPE. Otherwise it's a Unix fileglob
 * pattern, and ESCAPE must be NULL.
 *
 * Set *MATCH to the result of the comparison.
*/
svn_error_t *
svn_utf__glob(svn_boolean_t *match,
              const char *pattern, apr_size_t pattern_len,
              const char *string, apr_size_t string_len,
              const char *escape, apr_size_t escape_len,
              svn_boolean_t sql_like,
              svn_membuf_t *pattern_buf,
              svn_membuf_t *string_buf,
              svn_membuf_t *temp_buf);

/* Return the compiled version of the wrapped utf8proc library. */
const char *
svn_utf__utf8proc_compiled_version(void);

/* Return the runtime version of the wrapped utf8proc library. */
const char *
svn_utf__utf8proc_runtime_version(void);

/* Convert an UTF-16 (or UCS-2) string to UTF-8, returning the pointer
 * in RESULT. If BIG_ENDIAN is set, then UTF16STR is big-endian;
 * otherwise, it's little-endian.
 *
 * If UTF16LEN is SVN_UTF__UNKNOWN_LENGTH, then UTF16STR must be
 * terminated with a zero; otherwise, it is the number of 16-bit codes
 * to convert, and the source string may contain NUL values.
 *
 * Allocate RESULT in RESULT_POOL and use SCRATCH_POOL for
 * intermediate allocation.
 *
 * This function combines UTF-16 surrogate pairs into single code
 * points, but will leave single lead or trail surrogates unchanged.
 */
svn_error_t *
svn_utf__utf16_to_utf8(const svn_string_t **result,
                       const apr_uint16_t *utf16str,
                       apr_size_t utf16len,
                       svn_boolean_t big_endian,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

/* Convert an UTF-32 string to UTF-8, returning the pointer in
 * RESULT. If BIG_ENDIAN is set, then UTF32STR is big-endian;
 * otherwise, it's little-endian.
 *
 * If UTF32LEN is SVN_UTF__UNKNOWN_LENGTH, then UTF32STR must be
 * terminated with a zero; otherwise, it is the number of 32-bit codes
 * to convert, and the source string may contain NUL values.
 *
 * Allocate RESULT in RESULT_POOL and use SCRATCH_POOL for
 * intermediate allocation.
 */
svn_error_t *
svn_utf__utf32_to_utf8(const svn_string_t **result,
                       const apr_int32_t *utf32str,
                       apr_size_t utf32len,
                       svn_boolean_t big_endian,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_UTF_PRIVATE_H */
