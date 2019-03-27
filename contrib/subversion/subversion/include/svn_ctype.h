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
 * @file svn_ctype.h
 * @brief Character classification routines
 * @since New in 1.2.
 */


#ifndef SVN_CTYPE_H
#define SVN_CTYPE_H

#include <apr.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Table of flags for character classification. */
extern const apr_uint32_t *const svn_ctype_table;


/** Check if @a c is in the character class described by @a flags.
 * The @a flags is a bitwise-or combination of @c SVN_CTYPE_*
 * constants. Uses #svn_ctype_table.
 */
#define svn_ctype_test(c, flags) \
  (0 != (svn_ctype_table[(unsigned char)(c)] & (flags)))


/**
 * @defgroup ctype_basic Basic character classification - 7-bit ASCII only
 * @{
 */

/* Basic character classes */
#define SVN_CTYPE_CNTRL    0x0001 /**< Control character */
#define SVN_CTYPE_SPACE    0x0002 /**< Whitespace */
#define SVN_CTYPE_DIGIT    0x0004 /**< Decimal digit */
#define SVN_CTYPE_UPPER    0x0008 /**< Uppercase letter */
#define SVN_CTYPE_LOWER    0x0010 /**< Lowercase letter */
#define SVN_CTYPE_PUNCT    0x0020 /**< Punctuation mark */
#define SVN_CTYPE_XALPHA   0x0040 /**< Hexadecimal digits A to F */
#define SVN_CTYPE_ASCII    0x0080 /**< ASCII subset*/

/* Derived character classes */
/** ASCII letter */
#define SVN_CTYPE_ALPHA    (SVN_CTYPE_LOWER | SVN_CTYPE_UPPER)
/** ASCII letter or decimal digit */
#define SVN_CTYPE_ALNUM    (SVN_CTYPE_ALPHA | SVN_CTYPE_DIGIT)
/** ASCII hexadecimal digit */
#define SVN_CTYPE_XDIGIT   (SVN_CTYPE_DIGIT | SVN_CTYPE_XALPHA)
/** Printable ASCII except space */
#define SVN_CTYPE_GRAPH    (SVN_CTYPE_PUNCT | SVN_CTYPE_ALNUM)
/** All printable ASCII */
#define SVN_CTYPE_PRINT    (SVN_CTYPE_GRAPH | SVN_CTYPE_SPACE)


/** Check if @a c is an ASCII control character. */
#define svn_ctype_iscntrl(c)  svn_ctype_test((c), SVN_CTYPE_CNTRL)

/** Check if @a c is an ASCII whitespace character. */
#define svn_ctype_isspace(c)  svn_ctype_test((c), SVN_CTYPE_SPACE)

/** Check if @a c is an ASCII digit. */
#define svn_ctype_isdigit(c)  svn_ctype_test((c), SVN_CTYPE_DIGIT)

/** Check if @a c is an ASCII uppercase letter. */
#define svn_ctype_isupper(c)  svn_ctype_test((c), SVN_CTYPE_UPPER)

/** Check if @a c is an ASCII lowercase letter. */
#define svn_ctype_islower(c)  svn_ctype_test((c), SVN_CTYPE_LOWER)

/** Check if @a c is an ASCII punctuation mark. */
#define svn_ctype_ispunct(c)  svn_ctype_test((c), SVN_CTYPE_PUNCT)

/** Check if @a c is an ASCII character. */
#define svn_ctype_isascii(c)  svn_ctype_test((c), SVN_CTYPE_ASCII)

/** Check if @a c is an ASCII letter. */
#define svn_ctype_isalpha(c)  svn_ctype_test((c), SVN_CTYPE_ALPHA)

/** Check if @a c is an ASCII letter or decimal digit. */
#define svn_ctype_isalnum(c)  svn_ctype_test((c), SVN_CTYPE_ALNUM)

/** Check if @a c is an ASCII hexadecimal digit. */
#define svn_ctype_isxdigit(c) svn_ctype_test((c), SVN_CTYPE_XDIGIT)

/** Check if @a c is an ASCII graphical (visible printable) character. */
#define svn_ctype_isgraph(c)  svn_ctype_test((c), SVN_CTYPE_GRAPH)

/** Check if @a c is an ASCII printable character. */
#define svn_ctype_isprint(c)  svn_ctype_test((c), SVN_CTYPE_PRINT)

/** @} */

/**
 * @defgroup ctype_extra Extended character classification
 * @{
 */

/* Basic extended character classes */
#define SVN_CTYPE_UTF8LEAD 0x0100 /**< UTF-8 multibyte lead byte */
#define SVN_CTYPE_UTF8CONT 0x0200 /**< UTF-8 multibyte non-lead byte */
/* ### TBD
#define SVN_CTYPE_XMLNAME  0x0400
#define SVN_CTYPE_URISAFE  0x0800
*/

/* Derived extended character classes */
/** Part of a UTF-8 multibyte character. */
#define SVN_CTYPE_UTF8MBC  (SVN_CTYPE_UTF8LEAD | SVN_CTYPE_UTF8CONT)
/** All valid UTF-8 bytes. */
#define SVN_CTYPE_UTF8     (SVN_CTYPE_ASCII | SVN_CTYPE_UTF8MBC)

/** Check if @a c is a UTF-8 multibyte lead byte. */
#define svn_ctype_isutf8lead(c) svn_ctype_test((c), SVN_CTYPE_UTF8LEAD)

/** Check if @a c is a UTF-8 multibyte continuation (non-lead) byte. */
#define svn_ctype_isutf8cont(c) svn_ctype_test((c), SVN_CTYLE_UTF8CONT)

/** Check if @a c is part of a UTF-8 multibyte character. */
#define svn_ctype_isutf8mbc(c)  svn_ctype_test((c), SVN_CTYPE_UTF8MBC)

/** Check if @a c is valid in UTF-8. */
#define svn_ctype_isutf8(c)     svn_ctype_test((c), SVN_CTYPE_UTF8)

/** @} */

/**
 * @defgroup ctype_ascii ASCII character value constants
 * @{
 */

#define SVN_CTYPE_ASCII_MINUS            45 /**< ASCII value of '-' */
#define SVN_CTYPE_ASCII_DOT              46 /**< ASCII value of '.' */
#define SVN_CTYPE_ASCII_COLON            58 /**< ASCII value of ':' */
#define SVN_CTYPE_ASCII_UNDERSCORE       95 /**< ASCII value of '_' */
#define SVN_CTYPE_ASCII_TAB               9 /**< ASCII value of a tab */
#define SVN_CTYPE_ASCII_LINEFEED         10 /**< ASCII value of a line feed */
#define SVN_CTYPE_ASCII_CARRIAGERETURN   13
  /**< ASCII value of a carriage return */
#define SVN_CTYPE_ASCII_DELETE          127
  /**< ASCII value of a delete character */


/** @} */

/**
 * @defgroup ctype_case ASCII-subset case folding
 * @{
 */

/**
 * Compare two characters @a a and @a b, treating case-equivalent
 * unaccented Latin (ASCII subset) letters as equal.
 *
 * Returns in integer greater than, equal to, or less than 0,
 * according to whether @a a is considered greater than, equal to,
 * or less than @a b.
 *
 * @since New in 1.5.
 */
int
svn_ctype_casecmp(int a,
                  int b);


/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CTYPE_H */
