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
 * @file svn_xml.h
 * @brief XML code shared by various Subversion libraries.
 */

#ifndef SVN_XML_H
#define SVN_XML_H

#include <apr.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** The namespace all Subversion XML uses. */
#define SVN_XML_NAMESPACE "svn:"

/** Used as style argument to svn_xml_make_open_tag() and friends. */
enum svn_xml_open_tag_style {
  /** <tag ...> */
  svn_xml_normal = 1,

  /** <tag ...>, no cosmetic newline */
  svn_xml_protect_pcdata,

  /** <tag .../>  */
  svn_xml_self_closing
};



/** Determine if a string of character @a data of length @a len is a
 * safe bet for use with the svn_xml_escape_* functions found in this
 * header.
 *
 * Return @c TRUE if it is, @c FALSE otherwise.
 *
 * Essentially, this function exists to determine whether or not
 * simply running a string of bytes through the Subversion XML escape
 * routines will produce legitimate XML.  It should only be necessary
 * for data which might contain bytes that cannot be safely encoded
 * into XML (certain control characters, for example).
 */
svn_boolean_t
svn_xml_is_xml_safe(const char *data,
                    apr_size_t len);

/** Create or append in @a *outstr an xml-escaped version of @a string,
 * suitable for output as character data.
 *
 * If @a *outstr is @c NULL, set @a *outstr to a new stringbuf allocated
 * in @a pool, else append to the existing stringbuf there.
 */
void
svn_xml_escape_cdata_stringbuf(svn_stringbuf_t **outstr,
                               const svn_stringbuf_t *string,
                               apr_pool_t *pool);

/** Same as svn_xml_escape_cdata_stringbuf(), but @a string is an
 * @c svn_string_t.
 */
void
svn_xml_escape_cdata_string(svn_stringbuf_t **outstr,
                            const svn_string_t *string,
                            apr_pool_t *pool);

/** Same as svn_xml_escape_cdata_stringbuf(), but @a string is a
 * NULL-terminated C string.
 */
void
svn_xml_escape_cdata_cstring(svn_stringbuf_t **outstr,
                             const char *string,
                             apr_pool_t *pool);


/** Create or append in @a *outstr an xml-escaped version of @a string,
 * suitable for output as an attribute value.
 *
 * If @a *outstr is @c NULL, set @a *outstr to a new stringbuf allocated
 * in @a pool, else append to the existing stringbuf there.
 */
void
svn_xml_escape_attr_stringbuf(svn_stringbuf_t **outstr,
                              const svn_stringbuf_t *string,
                              apr_pool_t *pool);

/** Same as svn_xml_escape_attr_stringbuf(), but @a string is an
 * @c svn_string_t.
 */
void
svn_xml_escape_attr_string(svn_stringbuf_t **outstr,
                           const svn_string_t *string,
                           apr_pool_t *pool);

/** Same as svn_xml_escape_attr_stringbuf(), but @a string is a
 * NULL-terminated C string.
 */
void
svn_xml_escape_attr_cstring(svn_stringbuf_t **outstr,
                            const char *string,
                            apr_pool_t *pool);

/**
 * Return UTF-8 string @a string if it contains no characters that are
 * unrepresentable in XML.  Else, return a copy of @a string,
 * allocated in @a pool, with each unrepresentable character replaced
 * by "?\uuu", where "uuu" is the three-digit unsigned decimal value
 * of that character.
 *
 * Neither the input nor the output need be valid XML; however, the
 * output can always be safely XML-escaped.
 *
 * @note The current implementation treats all Unicode characters as
 * representable, except for most ASCII control characters (the
 * exceptions being CR, LF, and TAB, which are valid in XML).  There
 * may be other UTF-8 characters that are invalid in XML; see
 * http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=90591
 * and its thread for details.
 *
 * @since New in 1.2.
 */
const char *
svn_xml_fuzzy_escape(const char *string,
                     apr_pool_t *pool);


/*---------------------------------------------------------------*/

/* Generalized Subversion XML Parsing */

/** A generalized Subversion XML parser object */
typedef struct svn_xml_parser_t svn_xml_parser_t;

typedef void (*svn_xml_start_elem)(void *baton,
                                   const char *name,
                                   const char **atts);

typedef void (*svn_xml_end_elem)(void *baton, const char *name);

/* data is not NULL-terminated. */
typedef void (*svn_xml_char_data)(void *baton,
                                  const char *data,
                                  apr_size_t len);


/** Create a general Subversion XML parser.
 *
 * The @c svn_xml_parser_t object itself will be allocated from @a pool,
 * but some internal structures may be allocated out of pool.  Use
 * svn_xml_free_parser() to free all memory used by the parser.
 *
 * @since Since Subversion 1.10 parser will be freed automatically on pool
 * cleanup or by svn_xml_free_parser() call.
 */
svn_xml_parser_t *
svn_xml_make_parser(void *baton,
                    svn_xml_start_elem start_handler,
                    svn_xml_end_elem end_handler,
                    svn_xml_char_data data_handler,
                    apr_pool_t *pool);


/** Free a general Subversion XML parser */
void
svn_xml_free_parser(svn_xml_parser_t *svn_parser);


/** Push @a len bytes of xml data in @a buf at @a svn_parser.
 *
 * If this is the final push, @a is_final must be set.
 *
 * An error will be returned if there was a syntax problem in the XML,
 * or if any of the callbacks set an error using
 * svn_xml_signal_bailout().
 *
 * If an error is returned, the @c svn_xml_parser_t will have been freed
 * automatically, so the caller should not call svn_xml_free_parser().
 */
svn_error_t *
svn_xml_parse(svn_xml_parser_t *svn_parser,
              const char *buf,
              apr_size_t len,
              svn_boolean_t is_final);



/** The way to officially bail out of xml parsing.
 *
 * Store @a error in @a svn_parser and set all expat callbacks to @c NULL.
 */
void
svn_xml_signal_bailout(svn_error_t *error,
                       svn_xml_parser_t *svn_parser);





/*** Helpers for dealing with the data Expat gives us. ***/

/** Return the value associated with @a name in expat attribute array @a atts,
 * else return @c NULL.
 *
 * (There could never be a @c NULL attribute value in the XML,
 * although the empty string is possible.)
 *
 * @a atts is an array of c-strings: even-numbered indexes are names,
 * odd-numbers hold values.  If all is right, it should end on an
 * even-numbered index pointing to @c NULL.
 */
const char *
svn_xml_get_attr_value(const char *name,
                       const char *const *atts);



/* Converting between Expat attribute lists and APR hash tables. */


/** Create an attribute hash from @c va_list @a ap.
 *
 * The contents of @a ap are alternating <tt>char *</tt> keys and
 * <tt>char *</tt> vals, terminated by a final @c NULL falling on an
 * even index (zero-based).
 */
apr_hash_t *
svn_xml_ap_to_hash(va_list ap,
                   apr_pool_t *pool);

/** Create a hash that corresponds to Expat xml attribute list @a atts.
 *
 * The hash's keys and values are <tt>char *</tt>'s.
 *
 * @a atts may be NULL, in which case you just get an empty hash back
 * (this makes life more convenient for some callers).
 */
apr_hash_t *
svn_xml_make_att_hash(const char **atts,
                      apr_pool_t *pool);


/** Like svn_xml_make_att_hash(), but takes a hash and preserves any
 * key/value pairs already in it.
 */
void
svn_xml_hash_atts_preserving(const char **atts,
                             apr_hash_t *ht,
                             apr_pool_t *pool);

/** Like svn_xml_make_att_hash(), but takes a hash and overwrites
 * key/value pairs already in it that also appear in @a atts.
 */
void
svn_xml_hash_atts_overlaying(const char **atts,
                             apr_hash_t *ht,
                             apr_pool_t *pool);



/* Printing XML */

/** Create an XML header and return it in @a *str.
 *
 * Fully-formed XML documents should start out with a header,
 * something like <pre>
 *         \<?xml version="1.0" encoding="UTF-8"?\>
 * </pre>
 *
 * This function returns such a header.  @a *str must either be @c NULL, in
 * which case a new string is created, or it must point to an existing
 * string to be appended to. @a encoding must either be NULL, in which case
 * encoding information is omitted from the header, or must be the name of
 * the encoding of the XML document, such as "UTF-8".
 *
 * @since New in 1.7.
 */
void
svn_xml_make_header2(svn_stringbuf_t **str,
                     const char *encoding,
                     apr_pool_t *pool);

/** Like svn_xml_make_header2(), but does not emit encoding information.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
void
svn_xml_make_header(svn_stringbuf_t **str,
                    apr_pool_t *pool);


/** Store a new xml tag @a tagname in @a *str.
 *
 * If @a *str is @c NULL, set @a *str to a new stringbuf allocated
 * in @a pool, else append to the existing stringbuf there.
 *
 * Take the tag's attributes from varargs, a SVN_VA_NULL-terminated list of
 * alternating <tt>char *</tt> key and <tt>char *</tt> val.  Do xml-escaping
 * on each val.
 *
 * @a style is one of the enumerated styles in @c svn_xml_open_tag_style.
 */
void
svn_xml_make_open_tag(svn_stringbuf_t **str,
                      apr_pool_t *pool,
                      enum svn_xml_open_tag_style style,
                      const char *tagname,
                      ...) SVN_NEEDS_SENTINEL_NULL;


/** Like svn_xml_make_open_tag(), but takes a @c va_list instead of being
 * variadic.
 */
void
svn_xml_make_open_tag_v(svn_stringbuf_t **str,
                        apr_pool_t *pool,
                        enum svn_xml_open_tag_style style,
                        const char *tagname,
                        va_list ap);


/** Like svn_xml_make_open_tag(), but takes a hash table of attributes
 * (<tt>char *</tt> keys mapping to <tt>char *</tt> values).
 *
 * You might ask, why not just provide svn_xml_make_tag_atts()?
 *
 * The reason is that a hash table is the most natural interface to an
 * attribute list; the fact that Expat uses <tt>char **</tt> atts instead is
 * certainly a defensible implementation decision, but since we'd have
 * to have special code to support such lists throughout Subversion
 * anyway, we might as well write that code for the natural interface
 * (hashes) and then convert in the few cases where conversion is
 * needed.  Someday it might even be nice to change expat-lite to work
 * with apr hashes.
 *
 * See conversion functions svn_xml_make_att_hash() and
 * svn_xml_make_att_hash_overlaying().  Callers should use those to
 * convert Expat attr lists into hashes when necessary.
 */
void
svn_xml_make_open_tag_hash(svn_stringbuf_t **str,
                           apr_pool_t *pool,
                           enum svn_xml_open_tag_style style,
                           const char *tagname,
                           apr_hash_t *attributes);


/** Store an xml close tag @a tagname in @a str.
 *
 * If @a *str is @c NULL, set @a *str to a new stringbuf allocated
 * in @a pool, else append to the existing stringbuf there.
 */
void
svn_xml_make_close_tag(svn_stringbuf_t **str,
                       apr_pool_t *pool,
                       const char *tagname);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_XML_H */
