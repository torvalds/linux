/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file apr_escape.h
 * @brief APR-UTIL Escaping
 */
#ifndef APR_ESCAPE_H
#define APR_ESCAPE_H
#include "apr.h"
#include "apr_general.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup APR_Util_Escaping Escape functions
 * @ingroup APR
 * @{
 */

/* Simple escape/unescape functions.
 *
 */

/**
 * When passing a string to one of the escape functions, this value can be
 * passed to indicate a string-valued key, and have the length computed
 * automatically.
 */
#define APR_ESCAPE_STRING     (-1)

/**
 * Perform shell escaping on the provided string.
 * 
 * Shell escaping causes characters to be prefixed with a '\' character.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_shell(char *escaped, const char *str,
        apr_ssize_t slen, apr_size_t *len);

/**
 * Perform shell escaping on the provided string, returning the result
 * from the pool.
 *
 * Shell escaping causes characters to be prefixed with a '\' character.
 *
 * If no characters were escaped, the original string is returned.
 * @param p Pool to allocate from
 * @param str The original string
 * @return the encoded string, allocated from the pool, or the original
 * string if no escaping took place or the string was NULL.
 */
APR_DECLARE(const char *) apr_pescape_shell(apr_pool_t *p, const char *str)
        __attribute__((nonnull(1)));

/**
 * Unescapes a URL, leaving reserved characters intact.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param url String to be unescaped
 * @param slen The length of the original url, or APR_ESCAPE_STRING
 * @param forbid Optional list of forbidden characters, in addition to
 * 0x00
 * @param reserved Optional list of reserved characters that will be
 * left unescaped
 * @param plus If non zero, '+' is converted to ' ' as per
 * application/x-www-form-urlencoded encoding
 * @param len If set, the length of the escaped string will be returned
 * @return APR_SUCCESS on success, APR_NOTFOUND if no characters are
 * decoded or the string is NULL, APR_EINVAL if a bad escape sequence is
 * found, APR_BADCH if a character on the forbid list is found.
 */
APR_DECLARE(apr_status_t) apr_unescape_url(char *escaped, const char *url,
        apr_ssize_t slen, const char *forbid, const char *reserved, int plus,
        apr_size_t *len);

/**
 * Unescapes a URL, leaving reserved characters intact, returning the
 * result from a pool.
 * @param p Pool to allocate from
 * @param url String to be unescaped in place
 * @param forbid Optional list of forbidden characters, in addition to
 * 0x00
 * @param reserved Optional list of reserved characters that will be
 * left unescaped
 * @param plus If non zero, '+' is converted to ' ' as per
 * application/x-www-form-urlencoded encoding
 * @return A string allocated from the pool on success, the original string
 * if no characters are decoded, or NULL if a bad escape sequence is found
 * or if a character on the forbid list is found, or if the original string
 * was NULL.
 */
APR_DECLARE(const char *) apr_punescape_url(apr_pool_t *p, const char *url,
        const char *forbid, const char *reserved, int plus)
        __attribute__((nonnull(1)));

/**
 * Escape a path segment, as defined in RFC1808.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_path_segment(char *escaped,
        const char *str, apr_ssize_t slen, apr_size_t *len);

/**
 * Escape a path segment, as defined in RFC1808, returning the result from a
 * pool.
 * @param p Pool to allocate from
 * @param str String to be escaped
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or the string is NULL.
 */
APR_DECLARE(const char *) apr_pescape_path_segment(apr_pool_t *p,
        const char *str) __attribute__((nonnull(1)));

/**
 * Converts an OS path to a URL, in an OS dependent way, as defined in RFC1808.
 * In all cases if a ':' occurs before the first '/' in the URL, the URL should
 * be prefixed with "./" (or the ':' escaped). In the case of Unix, this means
 * leaving '/' alone, but otherwise doing what escape_path_segment() does. For
 * efficiency reasons, we don't use escape_path_segment(), which is provided for
 * reference. Again, RFC 1808 is where this stuff is defined.
 *
 * If partial is set, os_escape_path() assumes that the path will be appended to
 * something with a '/' in it (and thus does not prefix "./").
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param path The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param partial If non zero, suppresses the prepending of "./"
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or if the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_path(char *escaped, const char *path,
        apr_ssize_t slen, int partial, apr_size_t *len);

/**
 * Converts an OS path to a URL, in an OS dependent way, as defined in RFC1808,
 * returning the result from a pool.
 *
 * In all cases if a ':' occurs before the first '/' in the URL, the URL should
 * be prefixed with "./" (or the ':' escaped). In the case of Unix, this means
 * leaving '/' alone, but otherwise doing what escape_path_segment() does. For
 * efficiency reasons, we don't use escape_path_segment(), which is provided for
 * reference. Again, RFC 1808 is where this stuff is defined.
 *
 * If partial is set, os_escape_path() assumes that the path will be appended to
 * something with a '/' in it (and thus does not prefix "./").
 * @param p Pool to allocate from
 * @param str The original string
 * @param partial If non zero, suppresses the prepending of "./"
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or if the string was NULL.
 */
APR_DECLARE(const char *) apr_pescape_path(apr_pool_t *p, const char *str,
        int partial) __attribute__((nonnull(1)));

/**
 * Urlencode a string, as defined in
 * http://www.w3.org/TR/html401/interact/forms.html#h-17.13.4.1.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or if the stirng was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_urlencoded(char *escaped, const char *str,
        apr_ssize_t slen, apr_size_t *len);

/**
 * Urlencode a string, as defined in
 * http://www.w3.org/TR/html401/interact/forms.html#h-17.13.4.1, returning
 * the result from a pool.
 * @param p Pool to allocate from
 * @param str String to be escaped
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or if the string was NULL.
 */
APR_DECLARE(const char *) apr_pescape_urlencoded(apr_pool_t *p,
        const char *str) __attribute__((nonnull(1)));

/**
 * Apply entity encoding to a string. Characters are replaced as follows:
 * '<' becomes '&lt;', '>' becomes '&gt;', '&' becomes '&amp;', the
 * double quote becomes '&quot;" and the single quote becomes '&apos;'.
 *
 * If toasc is not zero, any non ascii character will be encoded as
 * '%\#ddd;', where ddd is the decimal code of the character.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param toasc If non zero, encode non ascii characters
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_entity(char *escaped, const char *str,
        apr_ssize_t slen, int toasc, apr_size_t *len);

/**
 * Apply entity encoding to a string, returning the result from a pool.
 * Characters are replaced as follows: '<' becomes '&lt;', '>' becomes
 * '&gt;', '&' becomes '&amp;', the double quote becomes '&quot;" and the
 * single quote becomes '&apos;'.
 * @param p Pool to allocate from
 * @param str The original string
 * @param toasc If non zero, encode non ascii characters
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or the string is NULL.
 */
APR_DECLARE(const char *) apr_pescape_entity(apr_pool_t *p, const char *str,
        int toasc) __attribute__((nonnull(1)));

/**
 * Decodes html entities or numeric character references in a string. If
 * the string to be unescaped is syntactically incorrect, then the
 * following fixups will be made:
 * unknown entities will be left undecoded;
 * references to unused numeric characters will be deleted.
 * In particular, &#00; will not be decoded, but will be deleted.
 * @param unescaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or the string was NULL
 */
APR_DECLARE(apr_status_t) apr_unescape_entity(char *unescaped, const char *str,
        apr_ssize_t slen, apr_size_t *len);

/**
 * Decodes html entities or numeric character references in a string. If
 * the string to be unescaped is syntactically incorrect, then the
 * following fixups will be made:
 * unknown entities will be left undecoded;
 * references to unused numeric characters will be deleted.
 * In particular, &#00; will not be decoded, but will be deleted.
 * @param p Pool to allocate from
 * @param str The original string
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or the string is NULL.
 */
APR_DECLARE(const char *) apr_punescape_entity(apr_pool_t *p, const char *str)
        __attribute__((nonnull(1)));

/**
 * Escape control characters in a string, as performed by the shell's
 * 'echo' command. Characters are replaced as follows:
 * \\a alert (bell), \\b backspace, \\f form feed, \\n new line, \\r carriage
 * return, \\t horizontal tab, \\v vertical tab, \\ backslash.
 *
 * Any non ascii character will be encoded as '\\xHH', where HH is the hex
 * code of the character.
 *
 * If quote is not zero, the double quote character will also be escaped.
 * @param escaped Optional buffer to write the encoded string, can be
 * NULL
 * @param str The original string
 * @param slen The length of the original string, or APR_ESCAPE_STRING
 * @param quote If non zero, encode double quotes
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if no changes to the string were
 * detected or the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_echo(char *escaped, const char *str,
        apr_ssize_t slen, int quote, apr_size_t *len);

/**
 * Escape control characters in a string, as performed by the shell's
 * 'echo' command, and return the results from a pool. Characters are
 * replaced as follows: \\a alert (bell), \\b backspace, \\f form feed,
 * \\n new line, \\r carriage return, \\t horizontal tab, \\v vertical tab,
 * \\ backslash.
 *
 * Any non ascii character will be encoded as '\\xHH', where HH is the hex
 * code of the character.
 *
 * If quote is not zero, the double quote character will also be escaped.
 * @param p Pool to allocate from
 * @param str The original string
 * @param quote If non zero, encode double quotes
 * @return A string allocated from the pool on success, the original string
 * if no characters are encoded or the string is NULL.
 */
APR_DECLARE(const char *) apr_pescape_echo(apr_pool_t *p, const char *str,
        int quote);

/**
 * Convert binary data to a hex encoding.
 * @param dest The destination buffer, can be NULL
 * @param src The original buffer
 * @param srclen The length of the original buffer
 * @param colon If not zero, insert colon characters between hex digits.
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if the string was NULL
 */
APR_DECLARE(apr_status_t) apr_escape_hex(char *dest, const void *src,
        apr_size_t srclen, int colon, apr_size_t *len);

/**
 * Convert binary data to a hex encoding, and return the results from a
 * pool.
 * @param p Pool to allocate from
 * @param src The original buffer
 * @param slen The length of the original buffer
 * @param colon If not zero, insert colon characters between hex digits.
 * @return A zero padded buffer allocated from the pool on success, or
 * NULL if src was NULL.
 */
APR_DECLARE(const char *) apr_pescape_hex(apr_pool_t *p, const void *src,
        apr_size_t slen, int colon) __attribute__((nonnull(1)));

/**
 * Convert hex encoded string to binary data.
 * @param dest The destination buffer, can be NULL
 * @param str The original buffer
 * @param slen The length of the original buffer
 * @param colon If not zero, ignore colon characters between hex digits.
 * @param len If present, returns the length of the string
 * @return APR_SUCCESS, or APR_NOTFOUND if the string was NULL, or APR_BADCH
 * if a non hex character is present.
 */
APR_DECLARE(apr_status_t) apr_unescape_hex(void *dest, const char *str,
        apr_ssize_t slen, int colon, apr_size_t *len);

/**
 * Convert hex encoding to binary data, and return the results from a pool.
 * If the colon character appears between pairs of hex digits, it will be
 * ignored.
 * @param p Pool to allocate from
 * @param str The original string
 * @param colon If not zero, ignore colon characters between hex digits.
 * @param len If present, returns the length of the final buffer
 * @return A buffer allocated from the pool on success, or NULL if src was
 * NULL, or a bad character was present.
 */
APR_DECLARE(const void *) apr_punescape_hex(apr_pool_t *p, const char *str,
        int colon, apr_size_t *len);

/** @} */
#ifdef __cplusplus
}
#endif

#endif	/* !APR_ESCAPE_H */
