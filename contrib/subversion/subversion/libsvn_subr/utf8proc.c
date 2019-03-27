/*
 * utf8proc.c:  Wrappers for the utf8proc library
 *
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
 */



#include <apr_fnmatch.h>

#include "private/svn_string_private.h"
#include "private/svn_utf_private.h"
#include "svn_private_config.h"

#if SVN_INTERNAL_UTF8PROC
#define UTF8PROC_INLINE
/* Somehow utf8proc thinks it is nice to use strlen as an argument name,
   while this function is already defined via apr.h */
#define strlen svn__strlen_var
#include "utf8proc/utf8proc.c"
#undef strlen
#else
#include <utf8proc.h>
#endif



const char *
svn_utf__utf8proc_compiled_version(void)
{
  static const char utf8proc_version[] =
                                  APR_STRINGIFY(UTF8PROC_VERSION_MAJOR) "."
                                  APR_STRINGIFY(UTF8PROC_VERSION_MINOR) "."
                                  APR_STRINGIFY(UTF8PROC_VERSION_PATCH);
  return utf8proc_version;
}

const char *
svn_utf__utf8proc_runtime_version(void)
{
  /* Unused static function warning removal hack. */
  SVN_UNUSED(utf8proc_grapheme_break);
  SVN_UNUSED(utf8proc_tolower);
  SVN_UNUSED(utf8proc_toupper);
#if UTF8PROC_VERSION_MAJOR >= 2
  SVN_UNUSED(utf8proc_totitle);
#endif
  SVN_UNUSED(utf8proc_charwidth);
  SVN_UNUSED(utf8proc_category_string);
  SVN_UNUSED(utf8proc_NFD);
  SVN_UNUSED(utf8proc_NFC);
  SVN_UNUSED(utf8proc_NFKD);
  SVN_UNUSED(utf8proc_NFKC);

  return utf8proc_version();
}



/* Fill the given BUFFER with decomposed UCS-4 representation of the
 * UTF-8 STRING. If LENGTH is SVN_UTF__UNKNOWN_LENGTH, assume STRING
 * is NUL-terminated; otherwise look only at the first LENGTH bytes in
 * STRING. Upon return, BUFFER->data points at an array of UCS-4
 * characters, and return the length of the array. TRANSFORM_FLAGS
 * define exactly how the decomposition is performed.
 *
 * A negative return value is an utf8proc error code and may indicate
 * that STRING contains invalid UTF-8 or was so long that an overflow
 * occurred.
 */
static apr_ssize_t
unicode_decomposition(int transform_flags,
                      const char *string, apr_size_t length,
                      svn_membuf_t *buffer)
{
  const int nullterm = (length == SVN_UTF__UNKNOWN_LENGTH
                        ? UTF8PROC_NULLTERM : 0);

  for (;;)
    {
      apr_int32_t *const ucs4buf = buffer->data;
      const apr_ssize_t ucs4len = buffer->size / sizeof(*ucs4buf);
      const apr_ssize_t result =
        utf8proc_decompose((const void*) string, length, ucs4buf, ucs4len,
                           UTF8PROC_DECOMPOSE | UTF8PROC_STABLE
                           | transform_flags | nullterm);

      if (result < 0 || result <= ucs4len)
        return result;

      /* Increase the decomposition buffer size and retry */
      svn_membuf__ensure(buffer, result * sizeof(*ucs4buf));
    }
}

/* Fill the given BUFFER with an NFD UCS-4 representation of the UTF-8
 * STRING. If LENGTH is SVN_UTF__UNKNOWN_LENGTH, assume STRING is
 * NUL-terminated; otherwise look only at the first LENGTH bytes in
 * STRING. Upon return, BUFFER->data points at an array of UCS-4
 * characters and *RESULT_LENGTH contains the length of the array.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints. Any error message comes from utf8proc.
 */
static svn_error_t *
decompose_normalized(apr_size_t *result_length,
                     const char *string, apr_size_t length,
                     svn_membuf_t *buffer)
{
  apr_ssize_t result = unicode_decomposition(0, string, length, buffer);
  if (result < 0)
    return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                            gettext(utf8proc_errmsg(result)));
  *result_length = result;
  return SVN_NO_ERROR;
}

/* Fill the given BUFFER with an NFC UTF-8 representation of the UTF-8
 * STRING. If LENGTH is SVN_UTF__UNKNOWN_LENGTH, assume STRING is
 * NUL-terminated; otherwise look only at the first LENGTH bytes in
 * STRING. Upon return, BUFFER->data points at a NUL-terminated string
 * of UTF-8 characters.
 *
 * If CASEFOLD is non-zero, perform Unicode case folding, e.g., for
 * case-insensitive string comparison. If STRIPMARK is non-zero, strip
 * all diacritical marks (e.g., accents) from the string.
 *
 * A returned error may indicate that STRING contains invalid UTF-8 or
 * invalid Unicode codepoints. Any error message comes from utf8proc.
 */
static svn_error_t *
normalize_cstring(apr_size_t *result_length,
                  const char *string, apr_size_t length,
                  svn_boolean_t casefold,
                  svn_boolean_t stripmark,
                  svn_membuf_t *buffer)
{
  int flags = 0;
  apr_ssize_t result;

  if (casefold)
    flags |= UTF8PROC_CASEFOLD;

  if (stripmark)
    flags |= UTF8PROC_STRIPMARK;

  result = unicode_decomposition(flags, string, length, buffer);
  if (result >= 0)
    {
      svn_membuf__resize(buffer, result * sizeof(apr_int32_t) + 1);
      result = utf8proc_reencode(buffer->data, result,
                                 UTF8PROC_COMPOSE | UTF8PROC_STABLE);
    }
  if (result < 0)
    return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                            gettext(utf8proc_errmsg(result)));
  *result_length = result;
  return SVN_NO_ERROR;
}

/* Compare two arrays of UCS-4 codes, BUFA of length LENA and BUFB of
 * length LENB. Return 0 if they're equal, a negative value if BUFA is
 * less than BUFB, otherwise a positive value.
 *
 * Yes, this is strcmp for known-length UCS-4 strings.
 */
static int
ucs4cmp(const apr_int32_t *bufa, apr_size_t lena,
        const apr_int32_t *bufb, apr_size_t lenb)
{
  const apr_size_t len = (lena < lenb ? lena : lenb);
  apr_size_t i;

  for (i = 0; i < len; ++i)
    {
      const int diff = bufa[i] - bufb[i];
      if (diff)
        return diff;
    }
  return (lena == lenb ? 0 : (lena < lenb ? -1 : 1));
}

svn_error_t *
svn_utf__normcmp(int *result,
                 const char *str1, apr_size_t len1,
                 const char *str2, apr_size_t len2,
                 svn_membuf_t *buf1, svn_membuf_t *buf2)
{
  apr_size_t buflen1;
  apr_size_t buflen2;

  /* Shortcut-circuit the decision if at least one of the strings is empty. */
  const svn_boolean_t empty1 =
    (0 == len1 || (len1 == SVN_UTF__UNKNOWN_LENGTH && !*str1));
  const svn_boolean_t empty2 =
    (0 == len2 || (len2 == SVN_UTF__UNKNOWN_LENGTH && !*str2));
  if (empty1 || empty2)
    {
      *result = (empty1 == empty2 ? 0 : (empty1 ? -1 : 1));
      return SVN_NO_ERROR;
    }

  SVN_ERR(decompose_normalized(&buflen1, str1, len1, buf1));
  SVN_ERR(decompose_normalized(&buflen2, str2, len2, buf2));
  *result = ucs4cmp(buf1->data, buflen1, buf2->data, buflen2);
  return SVN_NO_ERROR;
}

svn_error_t*
svn_utf__normalize(const char **result,
                   const char *str, apr_size_t len,
                   svn_membuf_t *buf)
{
  apr_size_t result_length;
  SVN_ERR(normalize_cstring(&result_length, str, len, FALSE, FALSE, buf));
  *result = (const char*)(buf->data);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_utf__xfrm(const char **result,
              const char *str, apr_size_t len,
              svn_boolean_t case_insensitive,
              svn_boolean_t accent_insensitive,
              svn_membuf_t *buf)
{
  apr_size_t result_length;
  SVN_ERR(normalize_cstring(&result_length, str, len,
                            case_insensitive, accent_insensitive, buf));
  *result = (const char*)(buf->data);
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_utf__fuzzy_glob_match(const char *str,
                          const apr_array_header_t *patterns,
                          svn_membuf_t *buf)
{
  const char *normalized;
  svn_error_t *err;
  int i;

  /* Try to normalize case and accents in STR.
   *
   * If that should fail for some reason, consider STR a mismatch. */
  err = svn_utf__xfrm(&normalized, str, strlen(str), TRUE, TRUE, buf);
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }

  /* Now see whether it matches any/all of the patterns. */
  for (i = 0; i < patterns->nelts; ++i)
    {
      const char *pattern = APR_ARRAY_IDX(patterns, i, const char *);
      if (apr_fnmatch(pattern, normalized, 0) == APR_SUCCESS)
        return TRUE;
    }

  return FALSE;
}

/* Decode a single UCS-4 code point to UTF-8, appending the result to BUFFER.
 * Assume BUFFER is already filled to *LENGTH and return the new size there.
 * This function does *not* nul-terminate the stringbuf!
 *
 * A returned error indicates that the codepoint is invalid.
 */
static svn_error_t *
encode_ucs4(svn_membuf_t *buffer, apr_int32_t ucs4chr, apr_size_t *length)
{
  apr_size_t utf8len;

  if (buffer->size - *length < 4)
    svn_membuf__resize(buffer, buffer->size + 4);

  utf8len = utf8proc_encode_char(ucs4chr, ((apr_byte_t*)buffer->data + *length));
  if (!utf8len)
    return svn_error_createf(SVN_ERR_UTF8PROC_ERROR, NULL,
                             _("Invalid Unicode character U+%04lX"),
                             (long)ucs4chr);
  *length += utf8len;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_utf__encode_ucs4_string(svn_membuf_t *buffer,
                            const apr_int32_t *ucs4str,
                            apr_size_t length,
                            apr_size_t *result_length)
{
  *result_length = 0;
  while (length-- > 0)
    SVN_ERR(encode_ucs4(buffer, *ucs4str++, result_length));
  svn_membuf__resize(buffer, *result_length + 1);
  ((char*)buffer->data)[*result_length] = '\0';
  return SVN_NO_ERROR;
}


svn_error_t *
svn_utf__glob(svn_boolean_t *match,
              const char *pattern, apr_size_t pattern_len,
              const char *string, apr_size_t string_len,
              const char *escape, apr_size_t escape_len,
              svn_boolean_t sql_like,
              svn_membuf_t *pattern_buf,
              svn_membuf_t *string_buf,
              svn_membuf_t *temp_buf)
{
  apr_size_t patternbuf_len;
  apr_size_t tempbuf_len;

  /* If we're in GLOB mode, we don't do custom escape chars. */
  if (escape && !sql_like)
    return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                            _("Cannot use a custom escape token"
                              " in glob matching mode"));

  /* Convert the patern to NFD UTF-8. We can't use the UCS-4 result
     because apr_fnmatch can't handle it.*/
  SVN_ERR(decompose_normalized(&tempbuf_len, pattern, pattern_len, temp_buf));
  if (!sql_like)
    SVN_ERR(svn_utf__encode_ucs4_string(pattern_buf, temp_buf->data,
                                        tempbuf_len, &patternbuf_len));
  else
    {
      /* Convert a LIKE pattern to a GLOB pattern that apr_fnmatch can use. */
      const apr_int32_t *like = temp_buf->data;
      apr_int32_t ucs4esc;
      svn_boolean_t escaped;
      apr_size_t i;

      if (!escape)
        ucs4esc = -1;           /* Definitely an invalid UCS-4 character. */
      else
        {
          const int nullterm = (escape_len == SVN_UTF__UNKNOWN_LENGTH
                                ? UTF8PROC_NULLTERM : 0);
          apr_ssize_t result =
            utf8proc_decompose((const void*) escape, escape_len, &ucs4esc, 1,
                               UTF8PROC_DECOMPOSE | UTF8PROC_STABLE | nullterm);
          if (result < 0)
            return svn_error_create(SVN_ERR_UTF8PROC_ERROR, NULL,
                                    gettext(utf8proc_errmsg(result)));
          if (result == 0 || result > 1)
            return svn_error_create(SVN_ERR_UTF8_GLOB, NULL,
                                    _("Escape token must be one character"));
          if ((ucs4esc & 0xFF) != ucs4esc)
            return svn_error_createf(SVN_ERR_UTF8_GLOB, NULL,
                                     _("Invalid escape character U+%04lX"),
                                     (long)ucs4esc);
        }

      patternbuf_len = 0;
      svn_membuf__ensure(pattern_buf, tempbuf_len + 1);
      for (i = 0, escaped = FALSE; i < tempbuf_len; ++i, ++like)
        {
          if (*like == ucs4esc && !escaped)
            {
              svn_membuf__resize(pattern_buf, patternbuf_len + 1);
              ((char*)pattern_buf->data)[patternbuf_len++] = '\\';
              escaped = TRUE;
            }
          else if (escaped)
            {
              SVN_ERR(encode_ucs4(pattern_buf, *like, &patternbuf_len));
              escaped = FALSE;
            }
          else
            {
              if ((*like == '[' || *like == '\\') && !escaped)
                {
                  /* Escape brackets and backslashes which are always
                     literals in LIKE patterns. */
                  svn_membuf__resize(pattern_buf, patternbuf_len + 1);
                  ((char*)pattern_buf->data)[patternbuf_len++] = '\\';
                  escaped = TRUE;
                  --i; --like;
                  continue;
                }

              /* Replace LIKE wildcards with their GLOB equivalents. */
              if (*like == '%' || *like == '_')
                {
                  const char wildcard = (*like == '%' ? '*' : '?');
                  svn_membuf__resize(pattern_buf, patternbuf_len + 1);
                  ((char*)pattern_buf->data)[patternbuf_len++] = wildcard;
                }
              else
                SVN_ERR(encode_ucs4(pattern_buf, *like, &patternbuf_len));
            }
        }
      svn_membuf__resize(pattern_buf, patternbuf_len + 1);
      ((char*)pattern_buf->data)[patternbuf_len] = '\0';
    }

  /* Now normalize the string */
  SVN_ERR(decompose_normalized(&tempbuf_len, string, string_len, temp_buf));
  SVN_ERR(svn_utf__encode_ucs4_string(string_buf, temp_buf->data,
                                      tempbuf_len, &tempbuf_len));

  *match = !apr_fnmatch(pattern_buf->data, string_buf->data, 0);
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_utf__is_normalized(const char *string, apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_membuf_t buffer;
  apr_size_t result_length;
  const apr_size_t length = strlen(string);
  svn_membuf__create(&buffer, length * sizeof(apr_int32_t), scratch_pool);
  err = normalize_cstring(&result_length, string, length,
                          FALSE, FALSE, &buffer);
  if (err)
    {
      svn_error_clear(err);
      return FALSE;
    }
  return (length == result_length && 0 == strcmp(string, buffer.data));
}

const char *
svn_utf__fuzzy_escape(const char *src, apr_size_t length, apr_pool_t *pool)
{
  /* Hexadecimal digits for code conversion. */
  static const char digits[] = "0123456789ABCDEF";

  /* Flags used for Unicode decomposition. */
  static const int decomp_flags = (
      UTF8PROC_COMPAT | UTF8PROC_STABLE | UTF8PROC_LUMP
      | UTF8PROC_NLF2LF | UTF8PROC_STRIPCC | UTF8PROC_STRIPMARK);

  svn_stringbuf_t *result;
  svn_membuf_t buffer;
  apr_ssize_t decomp_length;
  apr_ssize_t len;

  /* Decompose to a non-reversible compatibility format. */
  svn_membuf__create(&buffer, length * sizeof(apr_int32_t), pool);
  decomp_length = unicode_decomposition(decomp_flags, src, length, &buffer);
  if (decomp_length < 0)
    {
      svn_membuf_t part;
      apr_size_t done, prev;

      /* The only other error we can receive here indicates an integer
         overflow due to the length of the input string. Not very
         likely, but we certainly shouldn't continue in that case. */
      SVN_ERR_ASSERT_NO_RETURN(decomp_length == UTF8PROC_ERROR_INVALIDUTF8);

      /* Break the decomposition into parts that are valid UTF-8, and
         bytes that are not. Represent the invalid bytes in the target
         erray by their negative value. This works because utf8proc
         will not generate Unicode code points with values larger than
         U+10FFFF. */
      svn_membuf__create(&part, sizeof(apr_int32_t), pool);
      decomp_length = 0;
      done = prev = 0;
      while (done < length)
        {
          apr_int32_t uc;

          while (done < length)
            {
              len = utf8proc_iterate((apr_byte_t*)src + done, length - done, &uc);
              if (len < 0)
                break;
              done += len;
            }

          /* Decompose the valid part */
          if (done > prev)
            {
              len = unicode_decomposition(
                  decomp_flags, src + prev, done - prev, &part);
              SVN_ERR_ASSERT_NO_RETURN(len > 0);
              svn_membuf__resize(
                  &buffer, (decomp_length + len) * sizeof(apr_int32_t));
              memcpy((apr_int32_t*)buffer.data + decomp_length,
                     part.data, len * sizeof(apr_int32_t));
              decomp_length += len;
              prev = done;
            }

          /* What follows could be a valid UTF-8 sequence, but not
             a valid Unicode character. */
          if (done < length)
            {
              const char *last;

              /* Determine the length of the UTF-8 sequence */
              const char *const p = src + done;
              len = utf8proc_utf8class[(apr_byte_t)*p];

              /* Check if the multi-byte sequence is valid UTF-8. */
              if (len > 1 && len <= (apr_ssize_t)(length - done))
                last = svn_utf__last_valid(p, len);
              else
                last = NULL;

              /* Might not be a valid UTF-8 sequence at all */
              if (!last || (last && last - p < len))
                {
                  uc = -((apr_int32_t)(*p & 0xff));
                  len = 1;
                }
              else
                {
                  switch (len)
                    {
                      /* Decode the UTF-8 sequence without validation. */
                    case 2:
                      uc = ((p[0] & 0x1f) <<  6) + (p[1] & 0x3f);
                      break;
                    case 3:
                      uc = (((p[0] & 0x0f) << 12) + ((p[1] & 0x3f) <<  6)
                            + (p[2] & 0x3f));
                      break;
                    case 4:
                      uc = (((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12)
                            + ((p[2] & 0x3f) <<  6) + (p[3] & 0x3f));
                      break;
                    default:
                      SVN_ERR_ASSERT_NO_RETURN(
                          !"Unexpected invalid UTF-8 byte");
                    }

                }

              svn_membuf__resize(
                  &buffer, (decomp_length + 1) * sizeof(apr_int32_t));
              ((apr_int32_t*)buffer.data)[decomp_length++] = uc;
              done += len;
              prev = done;
            }
        }
    }

  /* Scan the result and deleting any combining diacriticals and
     inserting placeholders where any non-ascii characters remain.  */
  result = svn_stringbuf_create_ensure(decomp_length, pool);
  for (len = 0; len < decomp_length; ++len)
    {
      const apr_int32_t cp = ((apr_int32_t*)buffer.data)[len];
      if (cp > 0 && cp < 127)
        svn_stringbuf_appendbyte(result, (char)cp);
      else if (cp == 0)
        svn_stringbuf_appendcstr(result, "\\0");
      else if (cp < 0)
        {
          const apr_int32_t rcp = ((-cp) & 0xff);
          svn_stringbuf_appendcstr(result, "?\\");
          svn_stringbuf_appendbyte(result, digits[(rcp & 0x00f0) >> 4]);
          svn_stringbuf_appendbyte(result, digits[(rcp & 0x000f)]);
        }
      else
        {
          if (utf8proc_codepoint_valid(cp))
            {
              const utf8proc_property_t *prop = utf8proc_get_property(cp);
              if (prop->combining_class != 0)
                continue;           /* Combining mark; ignore */
              svn_stringbuf_appendcstr(result, "{U+");
            }
          else
            svn_stringbuf_appendcstr(result, "{U?");
          if (cp > 0xffff)
            {
              svn_stringbuf_appendbyte(result, digits[(cp & 0xf00000) >> 20]);
              svn_stringbuf_appendbyte(result, digits[(cp & 0x0f0000) >> 16]);
            }
          svn_stringbuf_appendbyte(result, digits[(cp & 0xf000) >> 12]);
          svn_stringbuf_appendbyte(result, digits[(cp & 0x0f00) >> 8]);
          svn_stringbuf_appendbyte(result, digits[(cp & 0x00f0) >> 4]);
          svn_stringbuf_appendbyte(result, digits[(cp & 0x000f)]);
          svn_stringbuf_appendbyte(result, '}');
        }
    }

  return result->data;
}
