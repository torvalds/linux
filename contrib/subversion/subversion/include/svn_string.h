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
 * @file svn_string.h
 * @brief Counted-length strings for Subversion, plus some C string goodies.
 *
 * There are two string datatypes: @c svn_string_t and @c svn_stringbuf_t.
 * The former is a simple pointer/length pair useful for passing around
 * strings (or arbitrary bytes) with a counted length. @c svn_stringbuf_t is
 * buffered to enable efficient appending of strings without an allocation
 * and copy for each append operation.
 *
 * @c svn_string_t contains a <tt>const char *</tt> for its data, so it is
 * most appropriate for constant data and for functions which expect constant,
 * counted data. Functions should generally use <tt>const @c svn_string_t
 * *</tt> as their parameter to indicate they are expecting a constant,
 * counted string.
 *
 * @c svn_stringbuf_t uses a plain <tt>char *</tt> for its data, so it is
 * most appropriate for modifiable data.
 *
 * <h3>Invariants</h3>
 *
 *   1. Null termination:
 *
 *      Both structures maintain a significant invariant:
 *
 *         <tt>s->data[s->len] == '\\0'</tt>
 *
 *      The functions defined within this header file will maintain
 *      the invariant (which does imply that memory is
 *      allocated/defined as @c len+1 bytes).  If code outside of the
 *      @c svn_string.h functions manually builds these structures,
 *      then they must enforce this invariant.
 *
 *      Note that an @c svn_string(buf)_t may contain binary data,
 *      which means that strlen(s->data) does not have to equal @c
 *      s->len. The null terminator is provided to make it easier to
 *      pass @c s->data to C string interfaces.
 *
 *
 *   2. Non-NULL input:
 *
 *      All the functions assume their input data pointer is non-NULL,
 *      unless otherwise documented, and may seg fault if passed
 *      NULL.  The input data may *contain* null bytes, of course, just
 *      the data pointer itself must not be NULL.
 *
 * <h3>Memory allocation</h3>
 *
 *   All the functions make a deep copy of all input data, and never store
 *   a pointer to the original input data.
 */


#ifndef SVN_STRING_H
#define SVN_STRING_H

#include <apr.h>          /* for apr_size_t */
#include <apr_pools.h>    /* for apr_pool_t */
#include <apr_tables.h>   /* for apr_array_header_t */

#include "svn_types.h"    /* for svn_boolean_t, svn_error_t */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup svn_string String handling
 * @{
 */



/** A simple counted string. */
typedef struct svn_string_t
{
  const char *data; /**< pointer to the bytestring */
  apr_size_t len;   /**< length of bytestring */
} svn_string_t;

/** A buffered string, capable of appending without an allocation and copy
 * for each append. */
typedef struct svn_stringbuf_t
{
  /** a pool from which this string was originally allocated, and is not
   * necessarily specific to this string.  This is used only for allocating
   * more memory from when the string needs to grow.
   */
  apr_pool_t *pool;

  /** pointer to the bytestring */
  char *data;

  /** length of bytestring */
  apr_size_t len;

  /** total size of buffer allocated */
  apr_size_t blocksize;
} svn_stringbuf_t;


/**
 * @defgroup svn_string_svn_string_t svn_string_t functions
 * @{
 */

/** Create a new string copied from the null-terminated C string @a cstring.
 */
svn_string_t *
svn_string_create(const char *cstring, apr_pool_t *pool);

/** Create a new, empty string.
 *
 * @since New in 1.8.
 */
svn_string_t *
svn_string_create_empty(apr_pool_t *pool);

/** Create a new string copied from a generic string of bytes, @a bytes, of
 * length @a size bytes.  @a bytes is NOT assumed to be null-terminated, but
 * the new string will be.
 *
 * @since Since 1.9, @a bytes can be NULL if @a size is zero.
 */
svn_string_t *
svn_string_ncreate(const char *bytes, apr_size_t size, apr_pool_t *pool);

/** Create a new string copied from the stringbuf @a strbuf.
 */
svn_string_t *
svn_string_create_from_buf(const svn_stringbuf_t *strbuf, apr_pool_t *pool);

/** Create a new string by printf-style formatting using @a fmt and the
 * variable arguments, which are as appropriate for apr_psprintf().
 */
svn_string_t *
svn_string_createf(apr_pool_t *pool, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));

/** Create a new string by printf-style formatting using @c fmt and @a ap.
 * This is the same as svn_string_createf() except for the different
 * way of passing the variable arguments.
 */
svn_string_t *
svn_string_createv(apr_pool_t *pool, const char *fmt, va_list ap)
  __attribute__((format(printf, 2, 0)));

/** Return TRUE if @a str is empty (has length zero). */
svn_boolean_t
svn_string_isempty(const svn_string_t *str);

/** Return a duplicate of @a original_string.
 *
 * @since Since 1.9, @a original_string can be NULL in which case NULL will
 * be returned.
 */
svn_string_t *
svn_string_dup(const svn_string_t *original_string, apr_pool_t *pool);

/** Return @c TRUE iff @a str1 and @a str2 have identical length and data. */
svn_boolean_t
svn_string_compare(const svn_string_t *str1, const svn_string_t *str2);

/** Return offset of first non-whitespace character in @a str, or return
 * @a str->len if none.
 */
apr_size_t
svn_string_first_non_whitespace(const svn_string_t *str);

/** Return position of last occurrence of @a ch in @a str, or return
 * @a str->len if no occurrence.
 */
apr_size_t
svn_string_find_char_backward(const svn_string_t *str, char ch);

/** @} */


/**
 * @defgroup svn_string_svn_stringbuf_t svn_stringbuf_t functions
 * @{
 */

/** Create a new stringbuf copied from the null-terminated C string
 * @a cstring.
 */
svn_stringbuf_t *
svn_stringbuf_create(const char *cstring, apr_pool_t *pool);

/** Create a new stringbuf copied from the generic string of bytes, @a bytes,
 * of length @a size bytes.  @a bytes is NOT assumed to be null-terminated,
 * but the new stringbuf will be.
 *
 * @since Since 1.9, @a bytes can be NULL if @a size is zero.
 */
svn_stringbuf_t *
svn_stringbuf_ncreate(const char *bytes, apr_size_t size, apr_pool_t *pool);

/** Create a new, empty stringbuf.
 *
 * @since New in 1.8.
 */
svn_stringbuf_t *
svn_stringbuf_create_empty(apr_pool_t *pool);

/** Create a new, empty stringbuf with at least @a minimum_size bytes of
 * space available in the memory block.
 *
 * The allocated string buffer will be at least one byte larger than
 * @a minimum_size to account for a final '\\0'.
 *
 * @since New in 1.6.
 */
svn_stringbuf_t *
svn_stringbuf_create_ensure(apr_size_t minimum_size, apr_pool_t *pool);

/** Create a new stringbuf copied from the string @a str.
 */
svn_stringbuf_t *
svn_stringbuf_create_from_string(const svn_string_t *str, apr_pool_t *pool);

/** Create a new stringbuf using the given @a str as initial buffer.
 * Allocate the result in @a pool.  In contrast to #svn_stringbuf_create,
 * the contents of @a str may change when the stringbuf gets modified.
 *
 * @since New in 1.9
 */
svn_stringbuf_t *
svn_stringbuf_create_wrap(char *str, apr_pool_t *pool);

/** Create a new stringbuf by printf-style formatting using @a fmt and the
 * variable arguments, which are as appropriate for apr_psprintf().
 */
svn_stringbuf_t *
svn_stringbuf_createf(apr_pool_t *pool, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));

/** Create a new stringbuf by printf-style formatting using @c fmt and @a ap.
 * This is the same as svn_stringbuf_createf() except for the different
 * way of passing the variable arguments.
 */
svn_stringbuf_t *
svn_stringbuf_createv(apr_pool_t *pool, const char *fmt, va_list ap)
  __attribute__((format(printf, 2, 0)));

/** Make sure that @a str has at least @a minimum_size
 * bytes of space available in the memory block.
 *
 * The allocated string buffer will be at least one byte larger than
 * @a minimum_size to account for a final '\\0'.
 *
 * @note: Before Subversion 1.8 this function did not ensure space for
 * one byte more than @a minimum_size.  If compatibility with pre-1.8
 * behaviour is required callers must assume space for only
 * @a minimum_size-1 data bytes plus a final '\\0'.
 */
void
svn_stringbuf_ensure(svn_stringbuf_t *str, apr_size_t minimum_size);

/** Set @a str to a copy of the null-terminated C string @a value. */
void
svn_stringbuf_set(svn_stringbuf_t *str, const char *value);

/** Set @a str to empty (zero length). */
void
svn_stringbuf_setempty(svn_stringbuf_t *str);

/** Return @c TRUE if @a str is empty (has length zero). */
svn_boolean_t
svn_stringbuf_isempty(const svn_stringbuf_t *str);

/** Chop @a nbytes bytes off end of @a str, but not more than @a str->len. */
void
svn_stringbuf_chop(svn_stringbuf_t *str, apr_size_t nbytes);

/**
 * Chop @a nbytes bytes off the start of @a str, but not more than @a str->len.
 *
 * @since New in 1.10.
 */
void
svn_stringbuf_leftchop(svn_stringbuf_t *str, apr_size_t nbytes);

/** Fill @a str with character @a c. */
void
svn_stringbuf_fillchar(svn_stringbuf_t *str, unsigned char c);

/** Append the single character @a byte onto @a targetstr.
 *
 * This is an optimized version of svn_stringbuf_appendbytes()
 * that is much faster to call and execute. Gains vary with the ABI.
 * The advantages extend beyond the actual call because the reduced
 * register pressure allows for more optimization within the caller.
 *
 * Reallocs if necessary. @a targetstr is affected, nothing else is.
 * @since New in 1.7.
 */
void
svn_stringbuf_appendbyte(svn_stringbuf_t *targetstr,
                         char byte);

/** Append the array of bytes @a bytes of length @a count onto @a targetstr.
 *
 * Reallocs if necessary. @a targetstr is affected, nothing else is.
 *
 * @since 1.9 @a bytes can be NULL if @a count is zero.
 */
void
svn_stringbuf_appendbytes(svn_stringbuf_t *targetstr,
                          const char *bytes,
                          apr_size_t count);

/** Append @a byte @a count times onto @a targetstr.
 *
 * Reallocs if necessary. @a targetstr is affected, nothing else is.
 * @since New in 1.9.
 */
void
svn_stringbuf_appendfill(svn_stringbuf_t *targetstr,
                         char byte,
                         apr_size_t count);

/** Append the stringbuf @c appendstr onto @a targetstr.
 *
 * Reallocs if necessary. @a targetstr is affected, nothing else is.
 */
void
svn_stringbuf_appendstr(svn_stringbuf_t *targetstr,
                        const svn_stringbuf_t *appendstr);

/** Append the C string @a cstr onto @a targetstr.
 *
 * Reallocs if necessary. @a targetstr is affected, nothing else is.
 */
void
svn_stringbuf_appendcstr(svn_stringbuf_t *targetstr,
                         const char *cstr);

/** Insert into @a str at position @a pos an array of bytes @a bytes
 * which is @a count bytes long.
 *
 * The resulting string will be @c count+str->len bytes long.  If
 * @a pos is larger than or equal to @c str->len, simply append @a bytes.
 *
 * Reallocs if necessary. @a str is affected, nothing else is.
 *
 * @note The inserted string may be a sub-range of @a str.
 *
 * @since New in 1.8.
 *
 * @since Since 1.9, @a bytes can be NULL if @a count is zero.
 */
void
svn_stringbuf_insert(svn_stringbuf_t *str,
                     apr_size_t pos,
                     const char *bytes,
                     apr_size_t count);

/** Remove @a count bytes from @a str, starting at position @a pos.
 *
 * If that range exceeds the current string data, truncate @a str at
 * @a pos.  If @a pos is larger than or equal to @c str->len, this will
 * be a no-op.  Otherwise, the resulting string will be @c str->len-count
 * bytes long.
 *
 * @since New in 1.8.
 */
void
svn_stringbuf_remove(svn_stringbuf_t *str,
                     apr_size_t pos,
                     apr_size_t count);

/** Replace in @a str the substring which starts at @a pos and is @a
 * old_count bytes long with a new substring @a bytes which is @a
 * new_count bytes long.
 *
 * This is faster but functionally equivalent to the following sequence:
 * @code
     svn_stringbuf_remove(str, pos, old_count);
     svn_stringbuf_insert(str, pos, bytes, new_count);
 * @endcode
 *
 * @since New in 1.8.
 *
 * @since Since 1.9, @a bytes can be NULL if @a new_count is zero.
 */
void
svn_stringbuf_replace(svn_stringbuf_t *str,
                      apr_size_t pos,
                      apr_size_t old_count,
                      const char *bytes,
                      apr_size_t new_count);

/** Replace all occurrences of @a to_find in @a str with @a replacement.
 * Return the number of replacements made.
 *
 * @since New in 1.10.
 */
apr_size_t
svn_stringbuf_replace_all(svn_stringbuf_t *str,
                          const char *to_find,
                          const char *replacement);

/** Return a duplicate of @a original_string. */
svn_stringbuf_t *
svn_stringbuf_dup(const svn_stringbuf_t *original_string, apr_pool_t *pool);

/** Return @c TRUE iff @a str1 and @a str2 have identical length and data. */
svn_boolean_t
svn_stringbuf_compare(const svn_stringbuf_t *str1,
                      const svn_stringbuf_t *str2);

/** Return offset of first non-whitespace character in @a str, or return
 * @a str->len if none.
 */
apr_size_t
svn_stringbuf_first_non_whitespace(const svn_stringbuf_t *str);

/** Strip whitespace from both sides of @a str (modified in place). */
void
svn_stringbuf_strip_whitespace(svn_stringbuf_t *str);

/** Return position of last occurrence of @a ch in @a str, or return
 * @a str->len if no occurrence.
 */
apr_size_t
svn_stringbuf_find_char_backward(const svn_stringbuf_t *str, char ch);

/** Return @c TRUE iff @a str1 and @a str2 have identical length and data. */
svn_boolean_t
svn_string_compare_stringbuf(const svn_string_t *str1,
                             const svn_stringbuf_t *str2);

/** @} */


/**
 * @defgroup svn_string_cstrings C string functions
 * @{
 */

/** Divide @a input into substrings, interpreting any char from @a sep
 * as a token separator.
 *
 * Return an array of copies of those substrings (plain const char*),
 * allocating both the array and the copies in @a pool.
 *
 * None of the elements added to the array contain any of the
 * characters in @a sep_chars, and none of the new elements are empty
 * (thus, it is possible that the returned array will have length
 * zero).
 *
 * If @a chop_whitespace is TRUE, then remove leading and trailing
 * whitespace from the returned strings.
 */
apr_array_header_t *
svn_cstring_split(const char *input,
                  const char *sep_chars,
                  svn_boolean_t chop_whitespace,
                  apr_pool_t *pool);

/** Like svn_cstring_split(), but append to existing @a array instead of
 * creating a new one.  Allocate the copied substrings in @a pool
 * (i.e., caller decides whether or not to pass @a array->pool as @a pool).
 */
void
svn_cstring_split_append(apr_array_header_t *array,
                         const char *input,
                         const char *sep_chars,
                         svn_boolean_t chop_whitespace,
                         apr_pool_t *pool);


/** Return @c TRUE iff @a str matches any of the elements of @a list, a list
 * of zero or more glob patterns.
 */
svn_boolean_t
svn_cstring_match_glob_list(const char *str, const apr_array_header_t *list);

/** Return @c TRUE iff @a str exactly matches any of the elements of @a list.
 *
 * @since new in 1.7
 */
svn_boolean_t
svn_cstring_match_list(const char *str, const apr_array_header_t *list);

/**
 * Get the next token from @a *str interpreting any char from @a sep as a
 * token separator.  Separators at the beginning of @a str will be skipped.
 * Returns a pointer to the beginning of the first token in @a *str or NULL
 * if no token is left.  Modifies @a str such that the next call will return
 * the next token.
 *
 * @note The content of @a *str may be modified by this function.
 *
 * @since New in 1.8.
 */
char *
svn_cstring_tokenize(const char *sep, char **str);

/**
 * Return the number of line breaks in @a msg, allowing any kind of newline
 * termination (CR, LF, CRLF, or LFCR), even inconsistent.
 *
 * @since New in 1.2.
 */
int
svn_cstring_count_newlines(const char *msg);

/**
 * Return a cstring which is the concatenation of @a strings (an array
 * of char *) joined by @a separator.  Allocate the result in @a pool.
 * If @a strings is empty, then return the empty string.
 * If @a trailing_separator is non-zero, also append the separator
 * after the last joined element.
 *
 * @since New in 1.10.
 */
char *
svn_cstring_join2(const apr_array_header_t *strings,
                  const char *separator,
                  svn_boolean_t trailing_separator,
                  apr_pool_t *pool);

/**
 * Similar to svn_cstring_join2(), but always includes the trailing
 * separator.
 *
 * @since New in 1.2.
 * @deprecated Provided for backwards compatibility with the 1.9 API.
 */
SVN_DEPRECATED
char *
svn_cstring_join(const apr_array_header_t *strings,
                 const char *separator,
                 apr_pool_t *pool);

/**
 * Compare two strings @a atr1 and @a atr2, treating case-equivalent
 * unaccented Latin (ASCII subset) letters as equal.
 *
 * Returns in integer greater than, equal to, or less than 0,
 * according to whether @a str1 is considered greater than, equal to,
 * or less than @a str2.
 *
 * @since New in 1.5.
 */
int
svn_cstring_casecmp(const char *str1, const char *str2);

/**
 * Parse the C string @a str into a 64 bit number, and return it in @a *n.
 * Assume that the number is represented in base @a base.
 * Raise an error if conversion fails (e.g. due to overflow), or if the
 * converted number is smaller than @a minval or larger than @a maxval.
 *
 * Leading whitespace in @a str is skipped in a locale-dependent way.
 * After that, the string may contain an optional '+' (positive, default)
 * or '-' (negative) character, followed by an optional '0x' prefix if
 * @a base is 0 or 16, followed by numeric digits appropriate for the base.
 * If there are any more characters after the numeric digits, an error is
 * returned.
 *
 * If @a base is zero, then a leading '0x' or '0X' prefix means hexadecimal,
 * else a leading '0' means octal (implemented, though not documented, in
 * apr_strtoi64() in APR 0.9.0 through 1.5.0), else use base ten.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_strtoi64(apr_int64_t *n, const char *str,
                     apr_int64_t minval, apr_int64_t maxval,
                     int base);

/**
 * Parse the C string @a str into a 64 bit number, and return it in @a *n.
 * Assume that the number is represented in base 10.
 * Raise an error if conversion fails (e.g. due to overflow).
 *
 * The behaviour otherwise is as described for svn_cstring_strtoi64().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_atoi64(apr_int64_t *n, const char *str);

/**
 * Parse the C string @a str into a 32 bit number, and return it in @a *n.
 * Assume that the number is represented in base 10.
 * Raise an error if conversion fails (e.g. due to overflow).
 *
 * The behaviour otherwise is as described for svn_cstring_strtoi64().
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_atoi(int *n, const char *str);

/**
 * Parse the C string @a str into an unsigned 64 bit number, and return
 * it in @a *n. Assume that the number is represented in base @a base.
 * Raise an error if conversion fails (e.g. due to overflow), or if the
 * converted number is smaller than @a minval or larger than @a maxval.
 *
 * Leading whitespace in @a str is skipped in a locale-dependent way.
 * After that, the string may contain an optional '+' (positive, default)
 * or '-' (negative) character, followed by an optional '0x' prefix if
 * @a base is 0 or 16, followed by numeric digits appropriate for the base.
 * If there are any more characters after the numeric digits, an error is
 * returned.
 *
 * If @a base is zero, then a leading '0x' or '0X' prefix means hexadecimal,
 * else a leading '0' means octal (implemented, though not documented, in
 * apr_strtoi64() in APR 0.9.0 through 1.5.0), else use base ten.
 *
 * @warning The implementation used since version 1.7 returns an error
 * if the parsed number is greater than APR_INT64_MAX, even if it is not
 * greater than @a maxval.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_strtoui64(apr_uint64_t *n, const char *str,
                      apr_uint64_t minval, apr_uint64_t maxval,
                      int base);

/**
 * Parse the C string @a str into an unsigned 64 bit number, and return
 * it in @a *n. Assume that the number is represented in base 10.
 * Raise an error if conversion fails (e.g. due to overflow).
 *
 * The behaviour otherwise is as described for svn_cstring_strtoui64(),
 * including the upper limit of APR_INT64_MAX.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_atoui64(apr_uint64_t *n, const char *str);

/**
 * Parse the C string @a str into an unsigned 32 bit number, and return
 * it in @a *n. Assume that the number is represented in base 10.
 * Raise an error if conversion fails (e.g. due to overflow).
 *
 * The behaviour otherwise is as described for svn_cstring_strtoui64(),
 * including the upper limit of APR_INT64_MAX.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cstring_atoui(unsigned int *n, const char *str);

/**
 * Skip the common prefix @a prefix from the C string @a str, and return
 * a pointer to the next character after the prefix.
 * Return @c NULL if @a str does not start with @a prefix.
 *
 * @since New in 1.9.
 */
const char *
svn_cstring_skip_prefix(const char *str, const char *prefix);

/** @} */

/** @} */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_STRING_H */
