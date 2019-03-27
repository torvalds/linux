/*
 * string.c:  routines to manipulate counted-length strings
 *            (svn_stringbuf_t and svn_string_t) and C strings.
 *
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



#include <apr.h>
#include <assert.h>

#include <string.h>      /* for memcpy(), memcmp(), strlen() */
#include <apr_fnmatch.h>
#include "svn_string.h"  /* loads "svn_types.h" and <apr_pools.h> */
#include "svn_ctype.h"
#include "private/svn_dep_compat.h"
#include "private/svn_string_private.h"

#include "svn_private_config.h"



/* Allocate the space for a memory buffer from POOL.
 * Return a pointer to the new buffer in *DATA and its size in *SIZE.
 * The buffer size will be at least MINIMUM_SIZE.
 *
 * N.B.: The stringbuf creation functions use this, but since stringbufs
 *       always consume at least 1 byte for the NUL terminator, the
 *       resulting data pointers will never be NULL.
 */
static APR_INLINE void
membuf_create(void **data, apr_size_t *size,
              apr_size_t minimum_size, apr_pool_t *pool)
{
  /* apr_palloc will allocate multiples of 8.
   * Thus, we would waste some of that memory if we stuck to the
   * smaller size. Note that this is safe even if apr_palloc would
   * use some other alignment or none at all. */
  minimum_size = APR_ALIGN_DEFAULT(minimum_size);
  *data = apr_palloc(pool, minimum_size);
  *size = minimum_size;
}

/* Ensure that the size of a given memory buffer is at least MINIMUM_SIZE
 * bytes. If *SIZE is already greater than or equal to MINIMUM_SIZE,
 * this function does nothing.
 *
 * If *SIZE is 0, the allocated buffer size will be MINIMUM_SIZE
 * rounded up to the nearest APR alignment boundary. Otherwse, *SIZE
 * will be multiplied by a power of two such that the result is
 * greater or equal to MINIMUM_SIZE. The pointer to the new buffer
 * will be returned in *DATA, and its size in *SIZE.
 */
static APR_INLINE void
membuf_ensure(void **data, apr_size_t *size,
              apr_size_t minimum_size, apr_pool_t *pool)
{
  if (minimum_size > *size)
    {
      apr_size_t new_size = *size;

      if (new_size == 0)
        new_size = minimum_size;
      else
        while (new_size < minimum_size)
          {
            const apr_size_t prev_size = new_size;
            new_size *= 2;

            /* check for apr_size_t overflow */
            if (prev_size > new_size)
              {
                new_size = minimum_size;
                break;
              }
          }

      membuf_create(data, size, new_size, pool);
    }
}

void
svn_membuf__create(svn_membuf_t *membuf, apr_size_t size, apr_pool_t *pool)
{
  membuf_create(&membuf->data, &membuf->size, size, pool);
  membuf->pool = pool;
}

void
svn_membuf__ensure(svn_membuf_t *membuf, apr_size_t size)
{
  membuf_ensure(&membuf->data, &membuf->size, size, membuf->pool);
}

void
svn_membuf__resize(svn_membuf_t *membuf, apr_size_t size)
{
  const void *const old_data = membuf->data;
  const apr_size_t old_size = membuf->size;

  membuf_ensure(&membuf->data, &membuf->size, size, membuf->pool);

  /* If we re-allocated MEMBUF->DATA, it cannot be NULL.
   * Statically initialized membuffers (OLD_DATA) may be NULL, though. */
  if (old_data && old_data != membuf->data)
    memcpy(membuf->data, old_data, old_size);
}

/* Always provide an out-of-line implementation of svn_membuf__zero */
#undef svn_membuf__zero
void
svn_membuf__zero(svn_membuf_t *membuf)
{
  SVN_MEMBUF__ZERO(membuf);
}

/* Always provide an out-of-line implementation of svn_membuf__nzero */
#undef svn_membuf__nzero
void
svn_membuf__nzero(svn_membuf_t *membuf, apr_size_t size)
{
  SVN_MEMBUF__NZERO(membuf, size);
}

static APR_INLINE svn_boolean_t
string_compare(const char *str1,
               const char *str2,
               apr_size_t len1,
               apr_size_t len2)
{
  /* easy way out :)  */
  if (len1 != len2)
    return FALSE;

  /* now the strings must have identical lengths */

  if ((memcmp(str1, str2, len1)) == 0)
    return TRUE;
  else
    return FALSE;
}

static APR_INLINE apr_size_t
string_first_non_whitespace(const char *str, apr_size_t len)
{
  apr_size_t i;

  for (i = 0; i < len; i++)
    {
      if (! svn_ctype_isspace(str[i]))
        return i;
    }

  /* if we get here, then the string must be entirely whitespace */
  return len;
}

static APR_INLINE apr_size_t
find_char_backward(const char *str, apr_size_t len, char ch)
{
  apr_size_t i = len;

  while (i != 0)
    {
      if (str[--i] == ch)
        return i;
    }

  /* char was not found, return len */
  return len;
}


/* svn_string functions */

/* Return a new svn_string_t object, allocated in POOL, initialized with
 * DATA and SIZE.  Do not copy the contents of DATA, just store the pointer.
 * SIZE is the length in bytes of DATA, excluding the required NUL
 * terminator. */
static svn_string_t *
create_string(const char *data, apr_size_t size,
              apr_pool_t *pool)
{
  svn_string_t *new_string;

  new_string = apr_palloc(pool, sizeof(*new_string));

  new_string->data = data;
  new_string->len = size;

  return new_string;
}

/* A data buffer for a zero-length string (just a null terminator).  Many
 * svn_string_t instances may share this same buffer. */
static const char empty_buffer[1] = {0};

svn_string_t *
svn_string_create_empty(apr_pool_t *pool)
{
  svn_string_t *new_string = apr_palloc(pool, sizeof(*new_string));
  new_string->data = empty_buffer;
  new_string->len = 0;

  return new_string;
}


svn_string_t *
svn_string_ncreate(const char *bytes, apr_size_t size, apr_pool_t *pool)
{
  void *mem;
  char *data;
  svn_string_t *new_string;

  /* Allocate memory for svn_string_t and data in one chunk. */
  mem = apr_palloc(pool, sizeof(*new_string) + size + 1);
  data = (char*)mem + sizeof(*new_string);

  new_string = mem;
  new_string->data = data;
  new_string->len = size;

  /* If SIZE is 0, NULL is valid for BYTES. */
  if (size)
    memcpy(data, bytes, size);

  /* Null termination is the convention -- even if we suspect the data
     to be binary, it's not up to us to decide, it's the caller's
     call.  Heck, that's why they call it the caller! */
  data[size] = '\0';

  return new_string;
}


svn_string_t *
svn_string_create(const char *cstring, apr_pool_t *pool)
{
  return svn_string_ncreate(cstring, strlen(cstring), pool);
}


svn_string_t *
svn_string_create_from_buf(const svn_stringbuf_t *strbuf, apr_pool_t *pool)
{
  return svn_string_ncreate(strbuf->data, strbuf->len, pool);
}


svn_string_t *
svn_string_createv(apr_pool_t *pool, const char *fmt, va_list ap)
{
  char *data = apr_pvsprintf(pool, fmt, ap);

  /* wrap an svn_string_t around the new data */
  return create_string(data, strlen(data), pool);
}


svn_string_t *
svn_string_createf(apr_pool_t *pool, const char *fmt, ...)
{
  svn_string_t *str;

  va_list ap;
  va_start(ap, fmt);
  str = svn_string_createv(pool, fmt, ap);
  va_end(ap);

  return str;
}


svn_boolean_t
svn_string_isempty(const svn_string_t *str)
{
  return (str->len == 0);
}


svn_string_t *
svn_string_dup(const svn_string_t *original_string, apr_pool_t *pool)
{
  return (original_string ? svn_string_ncreate(original_string->data,
                                               original_string->len, pool)
                          : NULL);
}



svn_boolean_t
svn_string_compare(const svn_string_t *str1, const svn_string_t *str2)
{
  return
    string_compare(str1->data, str2->data, str1->len, str2->len);
}



apr_size_t
svn_string_first_non_whitespace(const svn_string_t *str)
{
  return
    string_first_non_whitespace(str->data, str->len);
}


apr_size_t
svn_string_find_char_backward(const svn_string_t *str, char ch)
{
  return find_char_backward(str->data, str->len, ch);
}

svn_string_t *
svn_stringbuf__morph_into_string(svn_stringbuf_t *strbuf)
{
  /* In debug mode, detect attempts to modify the original STRBUF object.
   */
#ifdef SVN_DEBUG
  strbuf->pool = NULL;
  strbuf->blocksize = strbuf->len + 1;
#endif

  /* Both, svn_string_t and svn_stringbuf_t are public API structures
   * since the svn epoch. Thus, we can rely on their precise layout not
   * to change.
   *
   * It just so happens that svn_string_t is structurally equivalent
   * to the (data, len) sub-set of svn_stringbuf_t. There is also no
   * difference in alignment and padding. So, we can just re-interpret
   * that part of STRBUF as a svn_string_t.
   *
   * However, since svn_string_t does not know about the blocksize
   * member in svn_stringbuf_t, any attempt to re-size the returned
   * svn_string_t might invalidate the STRBUF struct. Hence, we consider
   * the source STRBUF "consumed".
   *
   * Modifying the string character content is fine, though.
   */
  return (svn_string_t *)&strbuf->data;
}



/* svn_stringbuf functions */

svn_stringbuf_t *
svn_stringbuf_create_empty(apr_pool_t *pool)
{
  return svn_stringbuf_create_ensure(0, pool);
}

svn_stringbuf_t *
svn_stringbuf_create_ensure(apr_size_t blocksize, apr_pool_t *pool)
{
  void *mem;
  svn_stringbuf_t *new_string;

  ++blocksize; /* + space for '\0' */

  /* Allocate memory for svn_string_t and data in one chunk. */
  membuf_create(&mem, &blocksize, blocksize + sizeof(*new_string), pool);

  /* Initialize header and string */
  new_string = mem;
  new_string->data = (char*)mem + sizeof(*new_string);
  new_string->data[0] = '\0';
  new_string->len = 0;
  new_string->blocksize = blocksize - sizeof(*new_string);
  new_string->pool = pool;

  return new_string;
}

svn_stringbuf_t *
svn_stringbuf_ncreate(const char *bytes, apr_size_t size, apr_pool_t *pool)
{
  svn_stringbuf_t *strbuf = svn_stringbuf_create_ensure(size, pool);

  /* If SIZE is 0, NULL is valid for BYTES. */
  if (size)
    memcpy(strbuf->data, bytes, size);

  /* Null termination is the convention -- even if we suspect the data
     to be binary, it's not up to us to decide, it's the caller's
     call.  Heck, that's why they call it the caller! */
  strbuf->data[size] = '\0';
  strbuf->len = size;

  return strbuf;
}


svn_stringbuf_t *
svn_stringbuf_create(const char *cstring, apr_pool_t *pool)
{
  return svn_stringbuf_ncreate(cstring, strlen(cstring), pool);
}


svn_stringbuf_t *
svn_stringbuf_create_from_string(const svn_string_t *str, apr_pool_t *pool)
{
  return svn_stringbuf_ncreate(str->data, str->len, pool);
}

svn_stringbuf_t *
svn_stringbuf_create_wrap(char *str, apr_pool_t *pool)
{
  svn_stringbuf_t *result = apr_palloc(pool, sizeof(*result));
  result->pool = pool;
  result->data = str;
  result->len = strlen(str);
  result->blocksize = result->len + 1;

  return result;
}

svn_stringbuf_t *
svn_stringbuf_createv(apr_pool_t *pool, const char *fmt, va_list ap)
{
  char *data = apr_pvsprintf(pool, fmt, ap);
  apr_size_t size = strlen(data);
  svn_stringbuf_t *new_string;

  new_string = apr_palloc(pool, sizeof(*new_string));
  new_string->data = data;
  new_string->len = size;
  new_string->blocksize = size + 1;
  new_string->pool = pool;

  return new_string;
}


svn_stringbuf_t *
svn_stringbuf_createf(apr_pool_t *pool, const char *fmt, ...)
{
  svn_stringbuf_t *str;

  va_list ap;
  va_start(ap, fmt);
  str = svn_stringbuf_createv(pool, fmt, ap);
  va_end(ap);

  return str;
}


void
svn_stringbuf_fillchar(svn_stringbuf_t *str, unsigned char c)
{
  memset(str->data, c, str->len);
}


void
svn_stringbuf_set(svn_stringbuf_t *str, const char *value)
{
  apr_size_t amt = strlen(value);

  membuf_ensure((void**) &str->data, &str->blocksize, amt + 1, str->pool);
  memcpy(str->data, value, amt + 1);
  str->len = amt;
}

void
svn_stringbuf_setempty(svn_stringbuf_t *str)
{
  str->data[0] = '\0';
  str->len = 0;
}


void
svn_stringbuf_chop(svn_stringbuf_t *str, apr_size_t nbytes)
{
  if (nbytes > str->len)
    str->len = 0;
  else
    str->len -= nbytes;

  str->data[str->len] = '\0';
}

void
svn_stringbuf_leftchop(svn_stringbuf_t *str, apr_size_t nbytes)
{
  if (str->len == 0)
    return;

  if (nbytes >= str->len)
    {
      str->len = 0;
      *str->data = '\0';
    }
  else
    {
      /* Note: This will irretrievably waste nbytes of space in the
         stringbuf's pool, but unlike the alternative of memmoving the
         data, it's a constant-time operation. */
      str->data += nbytes;
      str->len -= nbytes;
      str->blocksize -= nbytes;
    }
}

svn_boolean_t
svn_stringbuf_isempty(const svn_stringbuf_t *str)
{
  return (str->len == 0);
}


void
svn_stringbuf_ensure(svn_stringbuf_t *str, apr_size_t minimum_size)
{
  void *mem = NULL;
  ++minimum_size;  /* + space for '\0' */

  membuf_ensure(&mem, &str->blocksize, minimum_size, str->pool);
  if (mem && mem != str->data)
    {
      if (str->data)
        memcpy(mem, str->data, str->len + 1);
      str->data = mem;
    }
}


/* WARNING - Optimized code ahead!
 * This function has been hand-tuned for performance. Please read
 * the comments below before modifying the code.
 */
void
svn_stringbuf_appendbyte(svn_stringbuf_t *str, char byte)
{
  char *dest;
  apr_size_t old_len = str->len;

  /* In most cases, there will be pre-allocated memory left
   * to just write the new byte at the end of the used section
   * and terminate the string properly.
   */
  if (str->blocksize > old_len + 1)
    {
      /* The following read does not depend this write, so we
       * can issue the write first to minimize register pressure:
       * The value of old_len+1 is no longer needed; on most processors,
       * dest[old_len+1] will be calculated implicitly as part of
       * the addressing scheme.
       */
      str->len = old_len+1;

      /* Since the compiler cannot be sure that *src->data and *src
       * don't overlap, we read src->data *once* before writing
       * to *src->data. Replacing dest with str->data would force
       * the compiler to read it again after the first byte.
       */
      dest = str->data;

      /* If not already available in a register as per ABI, load
       * "byte" into the register (e.g. the one freed from old_len+1),
       * then write it to the string buffer and terminate it properly.
       *
       * Including the "byte" fetch, all operations so far could be
       * issued at once and be scheduled at the CPU's descression.
       * Most likely, no-one will soon depend on the data that will be
       * written in this function. So, no stalls there, either.
       */
      dest[old_len] = byte;
      dest[old_len+1] = '\0';
    }
  else
    {
      /* we need to re-allocate the string buffer
       * -> let the more generic implementation take care of that part
       */

      /* Depending on the ABI, "byte" is a register value. If we were
       * to take its address directly, the compiler might decide to
       * put in on the stack *unconditionally*, even if that would
       * only be necessary for this block.
       */
      char b = byte;
      svn_stringbuf_appendbytes(str, &b, 1);
    }
}


void
svn_stringbuf_appendbytes(svn_stringbuf_t *str, const char *bytes,
                          apr_size_t count)
{
  apr_size_t total_len;
  void *start_address;

  if (!count)
    /* Allow BYTES to be NULL by avoiding passing it to memcpy. */
    return;

  total_len = str->len + count;  /* total size needed */

  /* svn_stringbuf_ensure adds 1 for null terminator. */
  svn_stringbuf_ensure(str, total_len);

  /* get address 1 byte beyond end of original bytestring */
  start_address = (str->data + str->len);

  memcpy(start_address, bytes, count);
  str->len = total_len;

  str->data[str->len] = '\0';  /* We don't know if this is binary
                                  data or not, but convention is
                                  to null-terminate. */
}

void
svn_stringbuf_appendfill(svn_stringbuf_t *str,
                         char byte,
                         apr_size_t count)
{
  apr_size_t new_len = str->len + count;
  svn_stringbuf_ensure(str, new_len);

  memset(str->data + str->len, byte, count);

  /* update buffer length and always NUL-terminate it */
  str->len = new_len;
  str->data[new_len] = '\0';
}


void
svn_stringbuf_appendstr(svn_stringbuf_t *targetstr,
                        const svn_stringbuf_t *appendstr)
{
  svn_stringbuf_appendbytes(targetstr, appendstr->data, appendstr->len);
}


void
svn_stringbuf_appendcstr(svn_stringbuf_t *targetstr, const char *cstr)
{
  svn_stringbuf_appendbytes(targetstr, cstr, strlen(cstr));
}

void
svn_stringbuf_insert(svn_stringbuf_t *str,
                     apr_size_t pos,
                     const char *bytes,
                     apr_size_t count)
{
  /* For COUNT==0, we allow BYTES to be NULL. It's a no-op in that case. */
  if (count == 0)
    return;

  /* special case: BYTES overlaps with this string -> copy the source */
  if (bytes + count > str->data && bytes < str->data + str->blocksize)
    bytes = apr_pmemdup(str->pool, bytes, count);

  if (pos > str->len)
    pos = str->len;

  svn_stringbuf_ensure(str, str->len + count);
  memmove(str->data + pos + count, str->data + pos, str->len - pos + 1);
  memcpy(str->data + pos, bytes, count);

  str->len += count;
}

void
svn_stringbuf_remove(svn_stringbuf_t *str,
                     apr_size_t pos,
                     apr_size_t count)
{
  if (pos > str->len)
    pos = str->len;
  if (count > str->len - pos)
    count = str->len - pos;

  memmove(str->data + pos, str->data + pos + count, str->len - pos - count + 1);
  str->len -= count;
}

void
svn_stringbuf_replace(svn_stringbuf_t *str,
                      apr_size_t pos,
                      apr_size_t old_count,
                      const char *bytes,
                      apr_size_t new_count)
{
  /* For COUNT==0, we allow BYTES to be NULL.
   * In that case, this is just a substring removal. */
  if (new_count == 0)
    {
      svn_stringbuf_remove(str, pos, old_count);
      return;
    }

  /* special case: BYTES overlaps with this string -> copy the source */
  if (bytes + new_count > str->data && bytes < str->data + str->blocksize)
    bytes = apr_pmemdup(str->pool, bytes, new_count);

  if (pos > str->len)
    pos = str->len;
  if (old_count > str->len - pos)
    old_count = str->len - pos;

  if (old_count < new_count)
    {
      apr_size_t delta = new_count - old_count;
      svn_stringbuf_ensure(str, str->len + delta);
    }

  if (old_count != new_count)
    memmove(str->data + pos + new_count, str->data + pos + old_count,
            str->len - pos - old_count + 1);

  memcpy(str->data + pos, bytes, new_count);
  str->len += new_count - old_count;
}


apr_size_t
svn_stringbuf_replace_all(svn_stringbuf_t *str,
                          const char *to_find,
                          const char *replacement)
{
  apr_size_t replacements = 0;

  apr_size_t current = 0;
  apr_size_t original_length = str->len;

  apr_size_t to_copy;
  apr_size_t to_find_len;
  apr_size_t replacement_len;
  apr_size_t new_length;

  /* Early exit. */
  const char *pos = strstr(str->data, to_find);
  if (pos == NULL)
    return 0;

  to_find_len = strlen(to_find);
  replacement_len = strlen(replacement);

  /* We will store the new contents behind the NUL terminator of the current
   * data and track the total length in STR->LEN to make the reallocation
   * code preserve both bits.  However, we need to keep the NUL between them
   * to make strstr stop at that boundary. */
  ++str->len;

  /* Find all occurrences of TO_FIND, copy the bits in between to the target,
   * separated by REPLACEMENT. */
  for ( ; pos; pos = strstr(str->data + current, to_find), ++replacements)
    {
      to_copy = pos - str->data - current;
      svn_stringbuf_ensure(str, str->len + to_copy + replacement_len);

      if (to_copy)
        memcpy(str->data + str->len, str->data + current, to_copy);
      current += to_copy + to_find_len;

      str->len += to_copy;
      memcpy(str->data + str->len, replacement, replacement_len);
      str->len += replacement_len;
    }

  /* Copy remainder. */
  to_copy = original_length - current;
  if (to_copy)
    {
      svn_stringbuf_ensure(str, str->len + to_copy);
      memcpy(str->data + str->len, str->data + current, to_copy);
      str->len += to_copy;
    }

  /* Move new contents to the start of the buffer and terminate it. */
  new_length = str->len - original_length - 1;
  memmove(str->data, str->data + original_length + 1, new_length);
  str->len = new_length;
  str->data[new_length] = 0;

  /* Done. */
  return replacements;
}


svn_stringbuf_t *
svn_stringbuf_dup(const svn_stringbuf_t *original_string, apr_pool_t *pool)
{
  return (svn_stringbuf_ncreate(original_string->data,
                                original_string->len, pool));
}



svn_boolean_t
svn_stringbuf_compare(const svn_stringbuf_t *str1,
                      const svn_stringbuf_t *str2)
{
  return string_compare(str1->data, str2->data, str1->len, str2->len);
}



apr_size_t
svn_stringbuf_first_non_whitespace(const svn_stringbuf_t *str)
{
  return string_first_non_whitespace(str->data, str->len);
}


void
svn_stringbuf_strip_whitespace(svn_stringbuf_t *str)
{
  /* Skip (hide) whitespace at the beginning of the string. */
  if (svn_ctype_isspace(str->data[0]))
    {
      /* Find first non-whitespace character */
      apr_size_t offset = string_first_non_whitespace(str->data + 1,
                                                      str->len - 1) + 1;

      /* Go ahead!  Waste some RAM, we've got pools! :)  */
      str->data += offset;
      str->len -= offset;
      str->blocksize -= offset;
    }

  /* Now that we've trimmed the front, trim the end, wasting more RAM. */
  while ((str->len > 0) && svn_ctype_isspace(str->data[str->len - 1]))
    str->len--;
  str->data[str->len] = '\0';
}


apr_size_t
svn_stringbuf_find_char_backward(const svn_stringbuf_t *str, char ch)
{
  return find_char_backward(str->data, str->len, ch);
}


svn_boolean_t
svn_string_compare_stringbuf(const svn_string_t *str1,
                             const svn_stringbuf_t *str2)
{
  return string_compare(str1->data, str2->data, str1->len, str2->len);
}



/*** C string stuff. ***/

void
svn_cstring_split_append(apr_array_header_t *array,
                         const char *input,
                         const char *sep_chars,
                         svn_boolean_t chop_whitespace,
                         apr_pool_t *pool)
{
  char *pats;
  char *p;

  pats = apr_pstrdup(pool, input);  /* strtok wants non-const data */
  p = svn_cstring_tokenize(sep_chars, &pats);

  while (p)
    {
      if (chop_whitespace)
        {
          while (svn_ctype_isspace(*p))
            p++;

          {
            char *e = p + (strlen(p) - 1);
            while ((e >= p) && (svn_ctype_isspace(*e)))
              e--;
            *(++e) = '\0';
          }
        }

      if (p[0] != '\0')
        APR_ARRAY_PUSH(array, const char *) = p;

      p = svn_cstring_tokenize(sep_chars, &pats);
    }

  return;
}


apr_array_header_t *
svn_cstring_split(const char *input,
                  const char *sep_chars,
                  svn_boolean_t chop_whitespace,
                  apr_pool_t *pool)
{
  apr_array_header_t *a = apr_array_make(pool, 5, sizeof(input));
  svn_cstring_split_append(a, input, sep_chars, chop_whitespace, pool);
  return a;
}


svn_boolean_t svn_cstring_match_glob_list(const char *str,
                                          const apr_array_header_t *list)
{
  int i;

  for (i = 0; i < list->nelts; i++)
    {
      const char *this_pattern = APR_ARRAY_IDX(list, i, char *);

      if (apr_fnmatch(this_pattern, str, 0) == APR_SUCCESS)
        return TRUE;
    }

  return FALSE;
}

svn_boolean_t
svn_cstring_match_list(const char *str, const apr_array_header_t *list)
{
  int i;

  for (i = 0; i < list->nelts; i++)
    {
      const char *this_str = APR_ARRAY_IDX(list, i, char *);

      if (strcmp(this_str, str) == 0)
        return TRUE;
    }

  return FALSE;
}

char *
svn_cstring_tokenize(const char *sep, char **str)
{
    char *token;
    char *next;
    char csep;

    /* check parameters */
    if ((sep == NULL) || (str == NULL) || (*str == NULL))
        return NULL;

    /* let APR handle edge cases and multiple separators */
    csep = *sep;
    if (csep == '\0' || sep[1] != '\0')
      return apr_strtok(NULL, sep, str);

    /* skip characters in sep (will terminate at '\0') */
    token = *str;
    while (*token == csep)
        ++token;

    if (!*token)          /* no more tokens */
        return NULL;

    /* skip valid token characters to terminate token and
     * prepare for the next call (will terminate at '\0)
     */
    next = strchr(token, csep);
    if (next == NULL)
      {
        *str = token + strlen(token);
      }
    else
      {
        *next = '\0';
        *str = next + 1;
      }

    return token;
}

int svn_cstring_count_newlines(const char *msg)
{
  int count = 0;
  const char *p;

  for (p = msg; *p; p++)
    {
      if (*p == '\n')
        {
          count++;
          if (*(p + 1) == '\r')
            p++;
        }
      else if (*p == '\r')
        {
          count++;
          if (*(p + 1) == '\n')
            p++;
        }
    }

  return count;
}

char *
svn_cstring_join2(const apr_array_header_t *strings,
                  const char *separator,
                  svn_boolean_t trailing_separator,
                  apr_pool_t *pool)
{
  svn_stringbuf_t *new_str = svn_stringbuf_create_empty(pool);
  size_t sep_len = strlen(separator);
  int i;

  for (i = 0; i < strings->nelts; i++)
    {
      const char *string = APR_ARRAY_IDX(strings, i, const char *);
      if (i > 0)
        svn_stringbuf_appendbytes(new_str, separator, sep_len);
      svn_stringbuf_appendbytes(new_str, string, strlen(string));
    }

  if (strings->nelts > 0 && trailing_separator)
    svn_stringbuf_appendbytes(new_str, separator, sep_len);

  return new_str->data;
}

int
svn_cstring_casecmp(const char *str1, const char *str2)
{
  for (;;)
    {
      const int a = *str1++;
      const int b = *str2++;
      const int cmp = svn_ctype_casecmp(a, b);
      if (cmp || !a || !b)
        return cmp;
    }
}

svn_error_t *
svn_cstring_strtoui64(apr_uint64_t *n, const char *str,
                      apr_uint64_t minval, apr_uint64_t maxval,
                      int base)
{
  apr_int64_t val;
  char *endptr;

  /* We assume errno is thread-safe. */
  errno = 0; /* APR-0.9 doesn't always set errno */

  /* ### We're throwing away half the number range here.
   * ### APR needs a apr_strtoui64() function. */
  val = apr_strtoi64(str, &endptr, base);
  if (errno == EINVAL || endptr == str || str[0] == '\0' || *endptr != '\0')
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Could not convert '%s' into a number"),
                             str);
  if ((errno == ERANGE && (val == APR_INT64_MIN || val == APR_INT64_MAX)) ||
      val < 0 || (apr_uint64_t)val < minval || (apr_uint64_t)val > maxval)
    /* ### Mark this for translation when gettext doesn't choke on macros. */
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             "Number '%s' is out of range "
                             "'[%" APR_UINT64_T_FMT ", %" APR_UINT64_T_FMT "]'",
                             str, minval, maxval);
  *n = val;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cstring_atoui64(apr_uint64_t *n, const char *str)
{
  return svn_error_trace(svn_cstring_strtoui64(n, str, 0,
                                               APR_UINT64_MAX, 10));
}

svn_error_t *
svn_cstring_atoui(unsigned int *n, const char *str)
{
  apr_uint64_t val;

  SVN_ERR(svn_cstring_strtoui64(&val, str, 0, APR_UINT32_MAX, 10));
  *n = (unsigned int)val;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cstring_strtoi64(apr_int64_t *n, const char *str,
                     apr_int64_t minval, apr_int64_t maxval,
                     int base)
{
  apr_int64_t val;
  char *endptr;

  /* We assume errno is thread-safe. */
  errno = 0; /* APR-0.9 doesn't always set errno */

  val = apr_strtoi64(str, &endptr, base);
  if (errno == EINVAL || endptr == str || str[0] == '\0' || *endptr != '\0')
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             _("Could not convert '%s' into a number"),
                             str);
  if ((errno == ERANGE && (val == APR_INT64_MIN || val == APR_INT64_MAX)) ||
      val < minval || val > maxval)
    /* ### Mark this for translation when gettext doesn't choke on macros. */
    return svn_error_createf(SVN_ERR_INCORRECT_PARAMS, NULL,
                             "Number '%s' is out of range "
                             "'[%" APR_INT64_T_FMT ", %" APR_INT64_T_FMT "]'",
                             str, minval, maxval);
  *n = val;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_cstring_atoi64(apr_int64_t *n, const char *str)
{
  return svn_error_trace(svn_cstring_strtoi64(n, str, APR_INT64_MIN,
                                              APR_INT64_MAX, 10));
}

svn_error_t *
svn_cstring_atoi(int *n, const char *str)
{
  apr_int64_t val;

  SVN_ERR(svn_cstring_strtoi64(&val, str, APR_INT32_MIN, APR_INT32_MAX, 10));
  *n = (int)val;
  return SVN_NO_ERROR;
}

unsigned long
svn__strtoul(const char* buffer, const char** end)
{
  unsigned long result = 0;

  /* this loop will execute in just 2 CPU cycles, confirmed by measurement:
     7 macro-ops (max 4 / cycle => 2 cycles)
       1 load (max 1 / cycle)
       1 jumps (compare + conditional jump == 1 macro op; max 1 / cycle)
       2 arithmetic ops (subtract, increment; max 3 / cycle)
       2 scale-and-add AGU ops (max 3 / cycle)
       1 compiler-generated move operation
     dependency chain: temp = result * 4 + result; result = temp * 2 + c
                       (2 ops with latency 1 => 2 cycles)
   */
  while (1)
    {
      unsigned long c = (unsigned char)*buffer - (unsigned char)'0';
      if (c > 9)
        break;

      result = result * 10 + c;
      ++buffer;
    }

  *end = buffer;
  return result;
}

/* "Precalculated" itoa values for 2 places (including leading zeros).
 * For maximum performance, make sure all table entries are word-aligned.
 */
static const char decimal_table[100][4]
    = { "00", "01", "02", "03", "04", "05", "06", "07", "08", "09"
      , "10", "11", "12", "13", "14", "15", "16", "17", "18", "19"
      , "20", "21", "22", "23", "24", "25", "26", "27", "28", "29"
      , "30", "31", "32", "33", "34", "35", "36", "37", "38", "39"
      , "40", "41", "42", "43", "44", "45", "46", "47", "48", "49"
      , "50", "51", "52", "53", "54", "55", "56", "57", "58", "59"
      , "60", "61", "62", "63", "64", "65", "66", "67", "68", "69"
      , "70", "71", "72", "73", "74", "75", "76", "77", "78", "79"
      , "80", "81", "82", "83", "84", "85", "86", "87", "88", "89"
      , "90", "91", "92", "93", "94", "95", "96", "97", "98", "99"};

/* Copy the two bytes at SOURCE[0] and SOURCE[1] to DEST[0] and DEST[1] */
#define COPY_TWO_BYTES(dest,source)\
  memcpy((dest), (source), 2)

apr_size_t
svn__ui64toa(char * dest, apr_uint64_t number)
{
  char buffer[SVN_INT64_BUFFER_SIZE];
  apr_uint32_t reduced;   /* used for 32 bit DIV */
  char* target;

  /* Small numbers are by far the most common case.
   * Therefore, we use special code.
   */
  if (number < 100)
    {
      if (number < 10)
        {
          dest[0] = (char)('0' + number);
          dest[1] = 0;
          return 1;
        }
      else
        {
          COPY_TWO_BYTES(dest, decimal_table[(apr_size_t)number]);
          dest[2] = 0;
          return 2;
        }
    }

  /* Standard code. Write string in pairs of chars back-to-front */
  buffer[SVN_INT64_BUFFER_SIZE - 1] = 0;
  target = &buffer[SVN_INT64_BUFFER_SIZE - 3];

  /* Loop may be executed 0 .. 2 times. */
  while (number >= 100000000)
    {
      /* Number is larger than 100^4, i.e. we can write 4x2 chars.
       * Also, use 32 bit DIVs as these are about twice as fast.
       */
      reduced = (apr_uint32_t)(number % 100000000);
      number /= 100000000;

      COPY_TWO_BYTES(target - 0, decimal_table[reduced % 100]);
      reduced /= 100;
      COPY_TWO_BYTES(target - 2, decimal_table[reduced % 100]);
      reduced /= 100;
      COPY_TWO_BYTES(target - 4, decimal_table[reduced % 100]);
      reduced /= 100;
      COPY_TWO_BYTES(target - 6, decimal_table[reduced % 100]);
      target -= 8;
    }

  /* Now, the number fits into 32 bits, but may still be larger than 99 */
  reduced = (apr_uint32_t)(number);
  while (reduced >= 100)
    {
      COPY_TWO_BYTES(target, decimal_table[reduced % 100]);
      reduced /= 100;
      target -= 2;
    }

  /* The number is now smaller than 100 but larger than 1 */
  COPY_TWO_BYTES(target, decimal_table[reduced]);

  /* Correction for uneven count of places. */
  if (reduced < 10)
    ++target;

  /* Copy to target */
  memcpy(dest, target, &buffer[SVN_INT64_BUFFER_SIZE] - target);
  return &buffer[SVN_INT64_BUFFER_SIZE] - target - 1;
}

apr_size_t
svn__i64toa(char * dest, apr_int64_t number)
{
  if (number >= 0)
    return svn__ui64toa(dest, (apr_uint64_t)number);

  *dest = '-';
  return svn__ui64toa(dest + 1, 0 - (apr_uint64_t)number) + 1;
}

static void
ui64toa_sep(apr_uint64_t number, char separator, char *buffer)
{
  apr_size_t length = svn__ui64toa(buffer, number);
  apr_size_t i;

  for (i = length; i > 3; i -= 3)
    {
      memmove(&buffer[i - 2], &buffer[i - 3], length - i + 3);
      buffer[i-3] = separator;
      length++;
    }

  buffer[length] = 0;
}

char *
svn__ui64toa_sep(apr_uint64_t number, char separator, apr_pool_t *pool)
{
  char buffer[2 * SVN_INT64_BUFFER_SIZE];
  ui64toa_sep(number, separator, buffer);

  return apr_pstrdup(pool, buffer);
}

char *
svn__i64toa_sep(apr_int64_t number, char separator, apr_pool_t *pool)
{
  char buffer[2 * SVN_INT64_BUFFER_SIZE];
  if (number < 0)
    {
      buffer[0] = '-';
      ui64toa_sep((apr_uint64_t)(-number), separator, &buffer[1]);
    }
  else
    ui64toa_sep((apr_uint64_t)(number), separator, buffer);

  return apr_pstrdup(pool, buffer);
}

apr_size_t
svn__ui64tobase36(char *dest, apr_uint64_t value)
{
  char *dest_start = dest;
  if (value < 10)
    {
      /* pretty frequent and trivial case. Make it fast. */
      *(dest++) = (char)(value) + '0';
    }
  else
    {
      char buffer[SVN_INT64_BUFFER_SIZE];
      char *p = buffer;

      /* write result as little-endian to buffer */
      while (value > 0)
        {
          char c = (char)(value % 36);
          value /= 36;

          *p = (c <= 9) ? (c + '0') : (c - 10 + 'a');
          ++p;
        }

      /* copy as big-endian to DEST */
      while (p > buffer)
        *(dest++) = *(--p);
    }

  *dest = '\0';
  return dest - dest_start;
}

apr_uint64_t
svn__base36toui64(const char **next, const char *source)
{
  apr_uint64_t result = 0;
  apr_uint64_t factor = 1;
  int i  = 0;
  char digits[SVN_INT64_BUFFER_SIZE];

  /* convert digits to numerical values and count the number of places.
   * Also, prevent buffer overflow. */
  while (i < sizeof(digits))
    {
      char c = *source;
      if (c < 'a')
        {
          /* includes detection of NUL terminator */
          if (c < '0' || c > '9')
            break;

          c -= '0';
        }
      else
        {
          if (c < 'a' || c > 'z')
            break;

          c -= 'a' - 10;
        }

      digits[i++] = c;
      source++;
    }

  /* fold digits into the result */
  while (i > 0)
    {
      result += factor * (apr_uint64_t)digits[--i];
      factor *= 36;
    }

  if (next)
    *next = source;

  return result;
}


apr_size_t
svn_cstring__similarity(const char *stra, const char *strb,
                        svn_membuf_t *buffer, apr_size_t *rlcs)
{
  svn_string_t stringa, stringb;
  stringa.data = stra;
  stringa.len = strlen(stra);
  stringb.data = strb;
  stringb.len = strlen(strb);
  return svn_string__similarity(&stringa, &stringb, buffer, rlcs);
}

apr_size_t
svn_string__similarity(const svn_string_t *stringa,
                       const svn_string_t *stringb,
                       svn_membuf_t *buffer, apr_size_t *rlcs)
{
  const char *stra = stringa->data;
  const char *strb = stringb->data;
  const apr_size_t lena = stringa->len;
  const apr_size_t lenb = stringb->len;
  const apr_size_t total = lena + lenb;
  const char *enda = stra + lena;
  const char *endb = strb + lenb;
  apr_size_t lcs = 0;

  /* Skip the common prefix ... */
  while (stra < enda && strb < endb && *stra == *strb)
    {
      ++stra; ++strb;
      ++lcs;
    }

  /* ... and the common suffix */
  while (stra < enda && strb < endb)
    {
      --enda; --endb;
      if (*enda != *endb)
        {
          ++enda; ++endb;
          break;
        }

      ++lcs;
    }

  if (stra < enda && strb < endb)
    {
      const apr_size_t resta = enda - stra;
      const apr_size_t restb = endb - strb;
      const apr_size_t slots = (resta > restb ? restb : resta);
      apr_size_t *curr, *prev;
      const char *pstr;

      /* The outer loop must iterate on the longer string. */
      if (resta < restb)
        {
          pstr = stra;
          stra = strb;
          strb = pstr;

          pstr = enda;
          enda = endb;
          endb = pstr;
        }

      /* Allocate two columns in the LCS matrix
         ### Optimize this to (slots + 2) instesd of 2 * (slots + 1) */
      svn_membuf__ensure(buffer, 2 * (slots + 1) * sizeof(apr_size_t));
      svn_membuf__nzero(buffer, (slots + 2) * sizeof(apr_size_t));
      prev = buffer->data;
      curr = prev + slots + 1;

      /* Calculate LCS length of the remainder */
      for (pstr = stra; pstr < enda; ++pstr)
        {
          apr_size_t i;
          for (i = 1; i <= slots; ++i)
            {
              if (*pstr == strb[i-1])
                curr[i] = prev[i-1] + 1;
              else
                curr[i] = (curr[i-1] > prev[i] ? curr[i-1] : prev[i]);
            }

          /* Swap the buffers, making the previous one current */
          {
            apr_size_t *const temp = prev;
            prev = curr;
            curr = temp;
          }
        }

      lcs += prev[slots];
    }

  if (rlcs)
    *rlcs = lcs;

  /* Return similarity ratio rounded to 4 significant digits */
  if (total)
    return ((2 * SVN_STRING__SIM_RANGE_MAX * lcs + total/2) / total);
  else
    return SVN_STRING__SIM_RANGE_MAX;
}

apr_size_t
svn_cstring__match_length(const char *a,
                          const char *b,
                          apr_size_t max_len)
{
  apr_size_t pos = 0;

#if SVN_UNALIGNED_ACCESS_IS_OK

  /* Chunky processing is so much faster ...
   *
   * We can't make this work on architectures that require aligned access
   * because A and B will probably have different alignment. So, skipping
   * the first few chars until alignment is reached is not an option.
   */
  for (; max_len - pos >= sizeof(apr_size_t); pos += sizeof(apr_size_t))
    if (*(const apr_size_t*)(a + pos) != *(const apr_size_t*)(b + pos))
      break;

#endif

  for (; pos < max_len; ++pos)
    if (a[pos] != b[pos])
      break;

  return pos;
}

apr_size_t
svn_cstring__reverse_match_length(const char *a,
                                  const char *b,
                                  apr_size_t max_len)
{
  apr_size_t pos = 0;

#if SVN_UNALIGNED_ACCESS_IS_OK

  /* Chunky processing is so much faster ...
   *
   * We can't make this work on architectures that require aligned access
   * because A and B will probably have different alignment. So, skipping
   * the first few chars until alignment is reached is not an option.
   */
  for (pos = sizeof(apr_size_t); pos <= max_len; pos += sizeof(apr_size_t))
    if (*(const apr_size_t*)(a - pos) != *(const apr_size_t*)(b - pos))
      break;

  pos -= sizeof(apr_size_t);

#endif

  /* If we find a mismatch at -pos, pos-1 characters matched.
   */
  while (++pos <= max_len)
    if (a[0-pos] != b[0-pos])
      return pos - 1;

  /* No mismatch found -> at least MAX_LEN matching chars.
   */
  return max_len;
}

const char *
svn_cstring_skip_prefix(const char *str, const char *prefix)
{
  apr_size_t len = strlen(prefix);

  if (strncmp(str, prefix, len) == 0)
    {
      return str + len;
    }
  else
    {
      return NULL;
    }
}
