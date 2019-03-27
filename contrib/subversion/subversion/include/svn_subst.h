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
 * @file svn_subst.h
 * @brief Data substitution (keywords and EOL style)
 */



#ifndef SVN_SUBST_H
#define SVN_SUBST_H

#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_time.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* EOL conversion and keyword expansion. */

/** The EOL used in the Repository for "native" files */
#define SVN_SUBST_NATIVE_EOL_STR "\n"

/** Valid states for 'svn:eol-style' property.
 *
 * Property nonexistence is equivalent to 'none'.
 */
typedef enum svn_subst_eol_style
{
  /** An unrecognized style */
  svn_subst_eol_style_unknown,

  /** EOL translation is "off" or ignored value */
  svn_subst_eol_style_none,

  /** Translation is set to client's native eol */
  svn_subst_eol_style_native,

  /** Translation is set to one of LF, CR, CRLF */
  svn_subst_eol_style_fixed

} svn_subst_eol_style_t;

/** Set @a *style to the appropriate @c svn_subst_eol_style_t and @a *eol to
 * the appropriate cstring for a given svn:eol-style property value.
 *
 * Set @a *eol to
 *
 *    - @c NULL for @c svn_subst_eol_style_none, or
 *
 *    - a NULL-terminated C string containing the native eol marker
 *      for this platform, for @c svn_subst_eol_style_native, or
 *
 *    - a NULL-terminated C string containing the eol marker indicated
 *      by the property value, for @c svn_subst_eol_style_fixed.
 *
 * If @a *style is NULL, it is ignored.
 */
void
svn_subst_eol_style_from_value(svn_subst_eol_style_t *style,
                               const char **eol,
                               const char *value);

/** Indicates whether the working copy and normalized versions of a file
 * with the given the parameters differ.  If @a force_eol_check is TRUE,
 * the routine also accounts for all translations required due to repairing
 * fixed eol styles.
 *
 * @since New in 1.4
 *
 */
svn_boolean_t
svn_subst_translation_required(svn_subst_eol_style_t style,
                               const char *eol,
                               apr_hash_t *keywords,
                               svn_boolean_t special,
                               svn_boolean_t force_eol_check);


/** Values used in keyword expansion.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef struct svn_subst_keywords_t
{
  /**
   * @name svn_subst_keywords_t fields
   * String expansion of the like-named keyword, or NULL if the keyword
   * was not selected in the svn:keywords property.
   * @{
   */
  const svn_string_t *revision;
  const svn_string_t *date;
  const svn_string_t *author;
  const svn_string_t *url;
  const svn_string_t *id;
  /** @} */
} svn_subst_keywords_t;


/**
 * Set @a *kw to a new keywords hash filled with the appropriate contents
 * given a @a keywords_string (the contents of the svn:keywords
 * property for the file in question), the revision @a rev, the @a url,
 * the @a date the file was committed on, the @a author of the last
 * commit, and the URL of the repository root @a repos_root_url.
 *
 * Custom keywords defined in svn:keywords properties are expanded
 * using the provided parameters and in accordance with the following
 * format substitutions in the @a keywords_string:
 *   %a   - The author.
 *   %b   - The basename of the URL.
 *   %d   - Short format of the date.
 *   %D   - Long format of the date.
 *   %P   - The file's path, relative to the repository root URL.
 *   %r   - The revision.
 *   %R   - The URL to the root of the repository.
 *   %u   - The URL of the file.
 *   %_   - A space (keyword definitions cannot contain a literal space).
 *   %%   - A literal '%'.
 *   %H   - Equivalent to %P%_%r%_%d%_%a.
 *   %I   - Equivalent to %b%_%r%_%d%_%a.
 *
 * Custom keywords are defined by appending '=' to the keyword name, followed
 * by a string containing any combination of the format substitutions.
 *
 * Any of the inputs @a rev, @a url, @a date, @a author, and @a repos_root_url
 * can be @c NULL, or @c 0 for @a date, to indicate that the information is
 * not present. Each piece of information that is not present expands to the
 * empty string wherever it appears in an expanded keyword value.  (This can
 * result in multiple adjacent spaces in the expansion of a multi-valued
 * keyword such as "Id".)
 *
 * Hash keys are of type <tt>const char *</tt>.
 * Hash values are of type <tt>svn_string_t *</tt>.
 *
 * All memory is allocated out of @a pool.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_subst_build_keywords3(apr_hash_t **kw,
                          const char *keywords_string,
                          const char *rev,
                          const char *url,
                          const char *repos_root_url,
                          apr_time_t date,
                          const char *author,
                          apr_pool_t *pool);

/** Similar to svn_subst_build_keywords3() except that it does not accept
 * the @a repos_root_url parameter and hence supports less substitutions,
 * and also does not support custom keyword definitions.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.7 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_build_keywords2(apr_hash_t **kw,
                          const char *keywords_string,
                          const char *rev,
                          const char *url,
                          apr_time_t date,
                          const char *author,
                          apr_pool_t *pool);

/** Similar to svn_subst_build_keywords2() except that it populates
 * an existing structure @a *kw instead of creating a keywords hash.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_build_keywords(svn_subst_keywords_t *kw,
                         const char *keywords_string,
                         const char *rev,
                         const char *url,
                         apr_time_t date,
                         const char *author,
                         apr_pool_t *pool);


/** Return @c TRUE if @a a and @a b do not hold the same keywords.
 *
 * @a a and @a b are hashes of the form produced by
 * svn_subst_build_keywords2().
 *
 * @since New in 1.3.
 *
 * If @a compare_values is @c TRUE, "same" means that the @a a and @a b
 * contain exactly the same set of keywords, and the values of corresponding
 * keywords match as well.  Else if @a compare_values is @c FALSE, then
 * "same" merely means that @a a and @a b hold the same set of keywords,
 * although those keywords' values might differ.
 *
 * @a a and/or @a b may be @c NULL; for purposes of comparison, @c NULL is
 * equivalent to holding no keywords.
 */
svn_boolean_t
svn_subst_keywords_differ2(apr_hash_t *a,
                           apr_hash_t *b,
                           svn_boolean_t compare_values,
                           apr_pool_t *pool);

/** Similar to svn_subst_keywords_differ2() except that it compares
 * two @c svn_subst_keywords_t structs instead of keyword hashes.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_boolean_t
svn_subst_keywords_differ(const svn_subst_keywords_t *a,
                          const svn_subst_keywords_t *b,
                          svn_boolean_t compare_values);


/**
 * Copy and translate the data in @a src_stream into @a dst_stream.  It is
 * assumed that @a src_stream is a readable stream and @a dst_stream is a
 * writable stream.
 *
 * If @a eol_str is non-@c NULL, replace whatever bytestring @a src_stream
 * uses to denote line endings with @a eol_str in the output.  If
 * @a src_stream has an inconsistent line ending style, then: if @a repair
 * is @c FALSE, return @c SVN_ERR_IO_INCONSISTENT_EOL, else if @a repair is
 * @c TRUE, convert any line ending in @a src_stream to @a eol_str in
 * @a dst_stream.  Recognized line endings are: "\n", "\r", and "\r\n".
 *
 * See svn_subst_stream_translated() for details of the keyword substitution
 * which is controlled by the @a expand and @a keywords parameters.
 *
 * Note that a translation request is *required*:  one of @a eol_str or
 * @a keywords must be non-@c NULL.
 *
 * Notes:
 *
 * See svn_wc__get_keywords() and svn_wc__get_eol_style() for a
 * convenient way to get @a eol_str and @a keywords if in libsvn_wc.
 *
 * @since New in 1.3.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 *   Callers should use svn_subst_stream_translated() instead.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_translate_stream3(svn_stream_t *src_stream,
                            svn_stream_t *dst_stream,
                            const char *eol_str,
                            svn_boolean_t repair,
                            apr_hash_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *scratch_pool);


/** Similar to svn_subst_translate_stream3() except relies upon a
 * @c svn_subst_keywords_t struct instead of a hash for the keywords.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_translate_stream2(svn_stream_t *src_stream,
                            svn_stream_t *dst_stream,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *scratch_pool);


/**
 * Same as svn_subst_translate_stream2(), but does not take a @a pool
 * argument, instead creates a temporary subpool of the global pool, and
 * destroys it before returning.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_translate_stream(svn_stream_t *src_stream,
                           svn_stream_t *dst_stream,
                           const char *eol_str,
                           svn_boolean_t repair,
                           const svn_subst_keywords_t *keywords,
                           svn_boolean_t expand);


/** Return a stream which performs eol translation and keyword
 * expansion when read from or written to.  The stream @a stream
 * is used to read and write all data.
 *
 * Make sure you call svn_stream_close() on the returned stream to
 * ensure all data is flushed and cleaned up (this will also close
 * the provided @a stream).
 *
 * Read operations from and write operations to the stream
 * perform the same operation: if @a expand is @c FALSE, both
 * contract keywords.  One stream supports both read and write
 * operations.  Reads and writes may be mixed.
 *
 * If @a eol_str is non-@c NULL, replace whatever bytestring the input uses
 * to denote line endings with @a eol_str in the output.  If the input has
 * an inconsistent line ending style, then: if @a repair is @c FALSE, then a
 * subsequent read, write or other operation on the stream will return
 * @c SVN_ERR_IO_INCONSISTENT_EOL when the inconsistency is detected, else
 * if @a repair is @c TRUE, convert any line ending to @a eol_str.
 * Recognized line endings are: "\n", "\r", and "\r\n".
 *
 * Expand and contract keywords using the contents of @a keywords as the
 * new values.  If @a expand is @c TRUE, expand contracted keywords and
 * re-expand expanded keywords.  If @a expand is @c FALSE, contract expanded
 * keywords and ignore contracted ones.  Keywords not found in the hash are
 * ignored (not contracted or expanded).  If the @a keywords hash
 * itself is @c NULL, keyword substitution will be altogether ignored.
 *
 * Detect only keywords that are no longer than @c SVN_KEYWORD_MAX_LEN
 * bytes, including the delimiters and the keyword itself.
 *
 * Recommendation: if @a expand is FALSE, then you don't care about the
 * keyword values, so use empty strings as non-NULL signifiers when you
 * build the keywords hash.
 *
 * The stream returned is allocated in @a result_pool.
 *
 * If the inner stream implements resetting via svn_stream_reset(),
 * or marking and seeking via svn_stream_mark() and svn_stream_seek(),
 * the translated stream will too.
 *
 * @since New in 1.4.
 */
svn_stream_t *
svn_subst_stream_translated(svn_stream_t *stream,
                            const char *eol_str,
                            svn_boolean_t repair,
                            apr_hash_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *result_pool);


/** Set @a *stream to a stream which performs eol translation and keyword
 * expansion when read from or written to.  The stream @a source
 * is used to read and write all data.  Make sure you call
 * svn_stream_close() on @a stream to make sure all data are flushed
 * and cleaned up.
 *
 * When @a stream is closed, then @a source will be closed.
 *
 * Read and write operations perform the same transformation:
 * all data is translated to normal form.
 *
 * @see svn_subst_translate_to_normal_form()
 *
 * @since New in 1.5.
 * @deprecated Provided for backward compatibility with the 1.5 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_stream_translated_to_normal_form(svn_stream_t **stream,
                                           svn_stream_t *source,
                                           svn_subst_eol_style_t eol_style,
                                           const char *eol_str,
                                           svn_boolean_t always_repair_eols,
                                           apr_hash_t *keywords,
                                           apr_pool_t *pool);


/** Set @a *stream to a readable stream containing the "normal form"
 * of the special file located at @a path. The stream will be allocated
 * in @a result_pool, and any temporary allocations will be made in
 * @a scratch_pool.
 *
 * If the file at @a path is in fact a regular file, just read its content,
 * which should be in the "normal form" for a special file.  This enables
 * special files to be written and read on platforms that do not treat them
 * as special.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_subst_read_specialfile(svn_stream_t **stream,
                           const char *path,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/** Set @a *stream to a writable stream that accepts content in
 * the "normal form" for a special file, to be located at @a path, and
 * will create that file when the stream is closed. The stream will be
 * allocated in @a result_pool, and any temporary allocations will be
 * made in @a scratch_pool.
 *
 * If the platform does not support the semantics of the special file, write
 * a regular file containing the "normal form" text.  This enables special
 * files to be written and read on platforms that do not treat them as
 * special.
 *
 * Note: the target file is created in a temporary location, then renamed
 *   into position, so the creation can be considered "atomic".
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_subst_create_specialfile(svn_stream_t **stream,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


/** Set @a *stream to a stream which translates the special file at @a path
 * to the internal representation for special files when read from.  When
 * written to, it does the reverse: creating a special file when the
 * stream is closed.
 *
 * @since New in 1.5.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 *   Callers should use svn_subst_read_specialfile or
 *   svn_subst_create_specialfile as appropriate.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_stream_from_specialfile(svn_stream_t **stream,
                                  const char *path,
                                  apr_pool_t *pool);


/**
 * Copy the contents of file-path @a src to file-path @a dst atomically,
 * either creating @a dst or overwriting @a dst if it exists, possibly
 * performing line ending and keyword translations.
 *
 * The parameters @a *eol_str, @a repair, @a *keywords and @a expand are
 * defined the same as in svn_subst_translate_stream3().
 *
 * In addition, it will create a special file from normal form or
 * translate one to normal form if @a special is @c TRUE.
 *
 * If anything goes wrong during the copy, attempt to delete @a dst (if
 * it exists).
 *
 * If @a eol_str and @a keywords are @c NULL, behavior is just a byte-for-byte
 * copy.
 *
 * @a cancel_func and @a cancel_baton will be called (if not NULL)
 * periodically to check for cancellation.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_subst_copy_and_translate4(const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              apr_hash_t *keywords,
                              svn_boolean_t expand,
                              svn_boolean_t special,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *pool);


/**
 * Similar to svn_subst_copy_and_translate4() but without a cancellation
 * function and baton.
 *
 * @since New in 1.3.
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_copy_and_translate3(const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              apr_hash_t *keywords,
                              svn_boolean_t expand,
                              svn_boolean_t special,
                              apr_pool_t *pool);


/**
 * Similar to svn_subst_copy_and_translate3() except that @a keywords is a
 * @c svn_subst_keywords_t struct instead of a keywords hash.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 * @since New in 1.1.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_copy_and_translate2(const char *src,
                              const char *dst,
                              const char *eol_str,
                              svn_boolean_t repair,
                              const svn_subst_keywords_t *keywords,
                              svn_boolean_t expand,
                              svn_boolean_t special,
                              apr_pool_t *pool);

/**
 * Similar to svn_subst_copy_and_translate2() except that @a special is
 * always set to @c FALSE.
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_copy_and_translate(const char *src,
                             const char *dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             const svn_subst_keywords_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool);


/**
 * Set @a *dst to a copy of the string @a src, possibly performing line
 * ending and keyword translations.
 *
 * This is a variant of svn_subst_translate_stream3() that operates on
 * cstrings.  @see svn_subst_stream_translated() for details of the
 * translation and of @a eol_str, @a repair, @a keywords and @a expand.
 *
 * If @a eol_str and @a keywords are @c NULL, behavior is just a byte-for-byte
 * copy.
 *
 * Allocate @a *dst in @a pool.
 *
 * @since New in 1.3.
 */
svn_error_t *
svn_subst_translate_cstring2(const char *src,
                             const char **dst,
                             const char *eol_str,
                             svn_boolean_t repair,
                             apr_hash_t *keywords,
                             svn_boolean_t expand,
                             apr_pool_t *pool);

/**
 * Similar to svn_subst_translate_cstring2() except that @a keywords is a
 * @c svn_subst_keywords_t struct instead of a keywords hash.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_translate_cstring(const char *src,
                            const char **dst,
                            const char *eol_str,
                            svn_boolean_t repair,
                            const svn_subst_keywords_t *keywords,
                            svn_boolean_t expand,
                            apr_pool_t *pool);

/**
 * Translate the file @a src in working copy form to a file @a dst in
 * normal form.
 *
 * The values specified for @a eol_style, @a *eol_str, @a keywords and
 * @a special, should be the ones used to translate the file to its
 * working copy form.  Usually, these are the values specified by the
 * user in the files' properties.
 *
 * Inconsistent line endings in the file will be automatically repaired
 * (made consistent) for some eol styles.  For all others, an error is
 * returned.  By setting @a always_repair_eols to @c TRUE, eols will be
 * made consistent even for those styles which don't have it by default.
 *
 * @note To translate a file FROM normal form, use
 *       svn_subst_copy_and_translate3().
 *
 * @since New in 1.4
 * @deprecated Provided for backward compatibility with the 1.5 API
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_translate_to_normal_form(const char *src,
                                   const char *dst,
                                   svn_subst_eol_style_t eol_style,
                                   const char *eol_str,
                                   svn_boolean_t always_repair_eols,
                                   apr_hash_t *keywords,
                                   svn_boolean_t special,
                                   apr_pool_t *pool);

/**
 * Set @a *stream_p to a stream that detranslates the file @a src from
 * working copy form to normal form, allocated in @a pool.
 *
 * The values specified for @a eol_style, @a *eol_str, @a keywords and
 * @a special, should be the ones used to translate the file to its
 * working copy form.  Usually, these are the values specified by the
 * user in the files' properties.
 *
 * Inconsistent line endings in the file will be automatically repaired
 * (made consistent) for some eol styles.  For all others, an error is
 * returned.  By setting @a always_repair_eols to @c TRUE, eols will be
 * made consistent even for those styles which don't have it by default.
 *
 * @since New in 1.4.
 *
 * @deprecated Provided for backward compatibility with the 1.5 API.
 *   Use svn_subst_stream_from_specialfile if the source is special;
 *   otherwise, use svn_subst_stream_translated_to_normal_form.
 */
SVN_DEPRECATED
svn_error_t *
svn_subst_stream_detranslated(svn_stream_t **stream_p,
                              const char *src,
                              svn_subst_eol_style_t eol_style,
                              const char *eol_str,
                              svn_boolean_t always_repair_eols,
                              apr_hash_t *keywords,
                              svn_boolean_t special,
                              apr_pool_t *pool);


/* EOL conversion and character encodings */

/** Translate the string @a value from character encoding @a encoding to
 * UTF8, and also from its current line-ending style to LF line-endings.  If
 * @a encoding is @c NULL, translate from the system-default encoding.
 *
 * If @a translated_to_utf8 is not @c NULL, then set @a *translated_to_utf8
 * to @c TRUE if at least one character of @a value in the source character
 * encoding was translated to UTF-8, or to @c FALSE otherwise.
 *
 * If @a translated_line_endings is not @c NULL, then set @a
 * *translated_line_endings to @c TRUE if at least one line ending was
 * changed to LF, or to @c FALSE otherwise.
 *
 * If @a value has an inconsistent line ending style, then: if @a repair
 * is @c FALSE, return @c SVN_ERR_IO_INCONSISTENT_EOL, else if @a repair is
 * @c TRUE, convert any line ending in @a value to "\n" in
 * @a *new_value.  Recognized line endings are: "\n", "\r", and "\r\n".
 *
 * Set @a *new_value to the translated string, allocated in @a result_pool.
 *
 * @a scratch_pool is used for temporary allocations.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_subst_translate_string2(svn_string_t **new_value,
                            svn_boolean_t *translated_to_utf8,
                            svn_boolean_t *translated_line_endings,
                            const svn_string_t *value,
                            const char *encoding,
                            svn_boolean_t repair,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/** Similar to svn_subst_translate_string2(), except that the information about
 * whether re-encoding or line ending translation were performed is discarded.
 *
 * @deprecated Provided for backward compatibility with the 1.6 API.
 */
SVN_DEPRECATED
svn_error_t *svn_subst_translate_string(svn_string_t **new_value,
                                        const svn_string_t *value,
                                        const char *encoding,
                                        apr_pool_t *pool);

/** Translate the string @a value from UTF8 and LF line-endings into native
 * character encoding and native line-endings.  If @a for_output is TRUE,
 * translate to the character encoding of the output locale, else to that of
 * the default locale.
 *
 * Set @a *new_value to the translated string, allocated in @a pool.
 */
svn_error_t *svn_subst_detranslate_string(svn_string_t **new_value,
                                          const svn_string_t *value,
                                          svn_boolean_t for_output,
                                          apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBST_H */
