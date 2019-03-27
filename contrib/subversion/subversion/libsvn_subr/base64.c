/*
 * base64.c:  base64 encoding and decoding functions
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



#include <string.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>        /* for APR_INLINE */

#include "svn_pools.h"
#include "svn_io.h"
#include "svn_error.h"
#include "svn_base64.h"
#include "private/svn_string_private.h"
#include "private/svn_subr_private.h"

/* When asked to format the base64-encoded output as multiple lines,
   we put this many chars in each line (plus one new line char) unless
   we run out of data.
   It is vital for some of the optimizations below that this value is
   a multiple of 4. */
#define BASE64_LINELEN 76

/* This number of bytes is encoded in a line of base64 chars. */
#define BYTES_PER_LINE (BASE64_LINELEN / 4 * 3)

/* Value -> base64 char mapping table (2^6 entries) */
static const char base64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
                                "abcdefghijklmnopqrstuvwxyz0123456789+/";


/* Binary input --> base64-encoded output */

struct encode_baton {
  svn_stream_t *output;
  unsigned char buf[3];         /* Bytes waiting to be encoded */
  size_t buflen;                /* Number of bytes waiting */
  size_t linelen;               /* Bytes output so far on this line */
  svn_boolean_t break_lines;
  apr_pool_t *scratch_pool;
};


/* Base64-encode a group.  IN needs to have three bytes and OUT needs
   to have room for four bytes.  The input group is treated as four
   six-bit units which are treated as lookups into base64tab for the
   bytes of the output group.  */
static APR_INLINE void
encode_group(const unsigned char *in, char *out)
{
  /* Expand input bytes to machine word length (with zero extra cost
     on x86/x64) ... */
  apr_size_t part0 = in[0];
  apr_size_t part1 = in[1];
  apr_size_t part2 = in[2];

  /* ... to prevent these arithmetic operations from being limited to
     byte size.  This saves non-zero cost conversions of the result when
     calculating the addresses within base64tab. */
  out[0] = base64tab[part0 >> 2];
  out[1] = base64tab[((part0 & 3) << 4) | (part1 >> 4)];
  out[2] = base64tab[((part1 & 0xf) << 2) | (part2 >> 6)];
  out[3] = base64tab[part2 & 0x3f];
}

/* Base64-encode a line, i.e. BYTES_PER_LINE bytes from DATA into
   BASE64_LINELEN chars and append it to STR.  It does not assume that
   a new line char will be appended, though.
   The code in this function will simply transform the data without
   performing any boundary checks.  Therefore, DATA must have at least
   BYTES_PER_LINE left and space for at least another BASE64_LINELEN
   chars must have been pre-allocated in STR before calling this
   function. */
static void
encode_line(svn_stringbuf_t *str, const char *data)
{
  /* Translate directly from DATA to STR->DATA. */
  const unsigned char *in = (const unsigned char *)data;
  char *out = str->data + str->len;
  char *end = out + BASE64_LINELEN;

  /* We assume that BYTES_PER_LINE is a multiple of 3 and BASE64_LINELEN
     a multiple of 4. */
  for ( ; out != end; in += 3, out += 4)
    encode_group(in, out);

  /* Expand and terminate the string. */
  *out = '\0';
  str->len += BASE64_LINELEN;
}

/* (Continue to) Base64-encode the byte string DATA (of length LEN)
   into STR. Include newlines every so often if BREAK_LINES is true.
   INBUF, INBUFLEN, and LINELEN are used internally; the caller shall
   make INBUF have room for three characters and initialize *INBUFLEN
   and *LINELEN to 0.

   INBUF and *INBUFLEN carry the leftover data from call to call, and
   *LINELEN carries the length of the current output line. */
static void
encode_bytes(svn_stringbuf_t *str, const void *data, apr_size_t len,
             unsigned char *inbuf, size_t *inbuflen, size_t *linelen,
             svn_boolean_t break_lines)
{
  char group[4];
  const char *p = data, *end = p + len;
  apr_size_t buflen;

  /* Resize the stringbuf to make room for the (approximate) size of
     output, to avoid repeated resizes later.
     Please note that our optimized code relies on the fact that STR
     never needs to be resized until we leave this function. */
  buflen = len * 4 / 3 + 4;
  if (break_lines)
    {
      /* Add an extra space for line breaks. */
      buflen += buflen / BASE64_LINELEN;
    }
  svn_stringbuf_ensure(str, str->len + buflen);

  /* Keep encoding three-byte groups until we run out.  */
  while ((end - p) >= (3 - *inbuflen))
    {
      /* May we encode BYTES_PER_LINE bytes without caring about
         line breaks, data in the temporary INBUF or running out
         of data? */
      if (   *inbuflen == 0
          && (*linelen == 0 || !break_lines)
          && (end - p >= BYTES_PER_LINE))
        {
          /* Yes, we can encode a whole chunk of data at once. */
          encode_line(str, p);
          p += BYTES_PER_LINE;
          *linelen += BASE64_LINELEN;
        }
      else
        {
          /* No, this is one of a number of special cases.
             Encode the data byte by byte. */
          memcpy(inbuf + *inbuflen, p, 3 - *inbuflen);
          p += (3 - *inbuflen);
          encode_group(inbuf, group);
          svn_stringbuf_appendbytes(str, group, 4);
          *inbuflen = 0;
          *linelen += 4;
        }

      /* Add line breaks as necessary. */
      if (break_lines && *linelen == BASE64_LINELEN)
        {
          svn_stringbuf_appendbyte(str, '\n');
          *linelen = 0;
        }
    }

  /* Tack any extra input onto *INBUF.  */
  memcpy(inbuf + *inbuflen, p, end - p);
  *inbuflen += (end - p);
}


/* Encode leftover data, if any, and possibly a final newline (if
   there has been any data and BREAK_LINES is set), appending to STR.
   LEN must be in the range 0..2.  */
static void
encode_partial_group(svn_stringbuf_t *str, const unsigned char *extra,
                     size_t len, size_t linelen, svn_boolean_t break_lines)
{
  unsigned char ingroup[3];
  char outgroup[4];

  if (len > 0)
    {
      memcpy(ingroup, extra, len);
      memset(ingroup + len, 0, 3 - len);
      encode_group(ingroup, outgroup);
      memset(outgroup + (len + 1), '=', 4 - (len + 1));
      svn_stringbuf_appendbytes(str, outgroup, 4);
      linelen += 4;
    }
  if (break_lines && linelen > 0)
    svn_stringbuf_appendbyte(str, '\n');
}


/* Write handler for svn_base64_encode.  */
static svn_error_t *
encode_data(void *baton, const char *data, apr_size_t *len)
{
  struct encode_baton *eb = baton;
  svn_stringbuf_t *encoded = svn_stringbuf_create_empty(eb->scratch_pool);
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Encode this block of data and write it out.  */
  encode_bytes(encoded, data, *len, eb->buf, &eb->buflen, &eb->linelen,
               eb->break_lines);
  enclen = encoded->len;
  if (enclen != 0)
    err = svn_stream_write(eb->output, encoded->data, &enclen);
  svn_pool_clear(eb->scratch_pool);
  return err;
}


/* Close handler for svn_base64_encode().  */
static svn_error_t *
finish_encoding_data(void *baton)
{
  struct encode_baton *eb = baton;
  svn_stringbuf_t *encoded = svn_stringbuf_create_empty(eb->scratch_pool);
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Encode a partial group at the end if necessary, and write it out.  */
  encode_partial_group(encoded, eb->buf, eb->buflen, eb->linelen,
                       eb->break_lines);
  enclen = encoded->len;
  if (enclen != 0)
    err = svn_stream_write(eb->output, encoded->data, &enclen);

  /* Pass on the close request and clean up the baton.  */
  if (err == SVN_NO_ERROR)
    err = svn_stream_close(eb->output);
  svn_pool_destroy(eb->scratch_pool);
  return err;
}


svn_stream_t *
svn_base64_encode2(svn_stream_t *output,
                   svn_boolean_t break_lines,
                   apr_pool_t *pool)
{
  struct encode_baton *eb = apr_palloc(pool, sizeof(*eb));
  svn_stream_t *stream;

  eb->output = output;
  eb->buflen = 0;
  eb->linelen = 0;
  eb->break_lines = break_lines;
  eb->scratch_pool = svn_pool_create(pool);
  stream = svn_stream_create(eb, pool);
  svn_stream_set_write(stream, encode_data);
  svn_stream_set_close(stream, finish_encoding_data);
  return stream;
}


const svn_string_t *
svn_base64_encode_string2(const svn_string_t *str,
                          svn_boolean_t break_lines,
                          apr_pool_t *pool)
{
  svn_stringbuf_t *encoded = svn_stringbuf_create_empty(pool);
  unsigned char ingroup[3];
  size_t ingrouplen = 0;
  size_t linelen = 0;

  encode_bytes(encoded, str->data, str->len, ingroup, &ingrouplen, &linelen,
               break_lines);
  encode_partial_group(encoded, ingroup, ingrouplen, linelen,
                       break_lines);
  return svn_stringbuf__morph_into_string(encoded);
}

const svn_string_t *
svn_base64_encode_string(const svn_string_t *str, apr_pool_t *pool)
{
  return svn_base64_encode_string2(str, TRUE, pool);
}



/* Base64-encoded input --> binary output */

struct decode_baton {
  svn_stream_t *output;
  unsigned char buf[4];         /* Bytes waiting to be decoded */
  int buflen;                   /* Number of bytes waiting */
  svn_boolean_t done;           /* True if we already saw an '=' */
  apr_pool_t *scratch_pool;
};


/* Base64-decode a group.  IN needs to have four bytes and OUT needs
   to have room for three bytes.  The input bytes must already have
   been decoded from base64tab into the range 0..63.  The four
   six-bit values are pasted together to form three eight-bit bytes.  */
static APR_INLINE void
decode_group(const unsigned char *in, char *out)
{
  out[0] = (char)((in[0] << 2) | (in[1] >> 4));
  out[1] = (char)(((in[1] & 0xf) << 4) | (in[2] >> 2));
  out[2] = (char)(((in[2] & 0x3) << 6) | in[3]);
}

/* Lookup table for base64 characters; reverse_base64[ch] gives a
   negative value if ch is not a valid base64 character, or otherwise
   the value of the byte represented; 'A' => 0 etc. */
static const signed char reverse_base64[256] = {
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
-1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/* Similar to decode_group but this function also translates the
   6-bit values from the IN buffer before translating them.
   Return FALSE if a non-base64 char (e.g. '=' or new line)
   has been encountered. */
static APR_INLINE svn_boolean_t
decode_group_directly(const unsigned char *in, char *out)
{
  /* Translate the base64 chars in values [0..63, 0xff] */
  apr_size_t part0 = (unsigned char)reverse_base64[(unsigned char)in[0]];
  apr_size_t part1 = (unsigned char)reverse_base64[(unsigned char)in[1]];
  apr_size_t part2 = (unsigned char)reverse_base64[(unsigned char)in[2]];
  apr_size_t part3 = (unsigned char)reverse_base64[(unsigned char)in[3]];

  /* Pack 4x6 bits into 3x8.*/
  out[0] = (char)((part0 << 2) | (part1 >> 4));
  out[1] = (char)(((part1 & 0xf) << 4) | (part2 >> 2));
  out[2] = (char)(((part2 & 0x3) << 6) | part3);

  /* FALSE, iff any part is 0xff. */
  return (part0 | part1 | part2 | part3) != (unsigned char)(-1);
}

/* Base64-encode up to BASE64_LINELEN chars from *DATA and append it to
   STR.  After the function returns, *DATA will point to the first char
   that has not been translated, yet.  Returns TRUE if all BASE64_LINELEN
   chars could be translated, i.e. no special char has been encountered
   in between.
   The code in this function will simply transform the data without
   performing any boundary checks.  Therefore, DATA must have at least
   BASE64_LINELEN left and space for at least another BYTES_PER_LINE
   chars must have been pre-allocated in STR before calling this
   function. */
static svn_boolean_t
decode_line(svn_stringbuf_t *str, const char **data)
{
  /* Decode up to BYTES_PER_LINE bytes directly from *DATA into STR->DATA. */
  const unsigned char *p = *(const unsigned char **)data;
  char *out = str->data + str->len;
  char *end = out + BYTES_PER_LINE;

  /* We assume that BYTES_PER_LINE is a multiple of 3 and BASE64_LINELEN
     a multiple of 4.  Stop translation as soon as we encounter a special
     char.  Leave the entire group untouched in that case. */
  for (; out < end; p += 4, out += 3)
    if (!decode_group_directly(p, out))
      break;

  /* Update string sizes and positions. */
  str->len = out - str->data;
  *out = '\0';
  *data = (const char *)p;

  /* Return FALSE, if the caller should continue the decoding process
     using the slow standard method. */
  return out == end;
}


/* (Continue to) Base64-decode the byte string DATA (of length LEN)
   into STR. INBUF, INBUFLEN, and DONE are used internally; the
   caller shall have room for four bytes in INBUF and initialize
   *INBUFLEN to 0 and *DONE to FALSE.

   INBUF and *INBUFLEN carry the leftover bytes from call to call, and
   *DONE keeps track of whether we've seen an '=' which terminates the
   encoded data. */
static void
decode_bytes(svn_stringbuf_t *str, const char *data, apr_size_t len,
             unsigned char *inbuf, int *inbuflen, svn_boolean_t *done)
{
  const char *p = data;
  char group[3];
  signed char find;
  const char *end = data + len;

  /* Resize the stringbuf to make room for the maximum size of output,
     to avoid repeated resizes later.  The optimizations in
     decode_line rely on no resizes being necessary!

     (*inbuflen+len) is encoded data length
     (*inbuflen+len)/4 is the number of complete 4-bytes sets
     (*inbuflen+len)/4*3 is the number of decoded bytes
     svn_stringbuf_ensure will add an additional byte for the terminating 0.
  */
  svn_stringbuf_ensure(str, str->len + ((*inbuflen + len) / 4) * 3);

  while ( !*done && p < end )
    {
      /* If no data is left in temporary INBUF and there is at least
         one line-sized chunk left to decode, we may use the optimized
         code path. */
      if ((*inbuflen == 0) && (end - p >= BASE64_LINELEN))
        if (decode_line(str, &p))
          continue;

      /* A special case or decode_line encountered a special char. */
      if (*p == '=')
        {
          /* We are at the end and have to decode a partial group.  */
          if (*inbuflen >= 2)
            {
              memset(inbuf + *inbuflen, 0, 4 - *inbuflen);
              decode_group(inbuf, group);
              svn_stringbuf_appendbytes(str, group, *inbuflen - 1);
            }
          *done = TRUE;
        }
      else
        {
          find = reverse_base64[(unsigned char)*p];
          ++p;

          if (find >= 0)
            inbuf[(*inbuflen)++] = find;
          if (*inbuflen == 4)
            {
              decode_group(inbuf, group);
              svn_stringbuf_appendbytes(str, group, 3);
              *inbuflen = 0;
            }
        }
    }
}


/* Write handler for svn_base64_decode.  */
static svn_error_t *
decode_data(void *baton, const char *data, apr_size_t *len)
{
  struct decode_baton *db = baton;
  svn_stringbuf_t *decoded;
  apr_size_t declen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Decode this block of data.  */
  decoded = svn_stringbuf_create_empty(db->scratch_pool);
  decode_bytes(decoded, data, *len, db->buf, &db->buflen, &db->done);

  /* Write the output, clean up, go home.  */
  declen = decoded->len;
  if (declen != 0)
    err = svn_stream_write(db->output, decoded->data, &declen);
  svn_pool_clear(db->scratch_pool);
  return err;
}


/* Close handler for svn_base64_decode().  */
static svn_error_t *
finish_decoding_data(void *baton)
{
  struct decode_baton *db = baton;
  svn_error_t *err;

  /* Pass on the close request and clean up the baton.  */
  err = svn_stream_close(db->output);
  svn_pool_destroy(db->scratch_pool);
  return err;
}


svn_stream_t *
svn_base64_decode(svn_stream_t *output, apr_pool_t *pool)
{
  struct decode_baton *db = apr_palloc(pool, sizeof(*db));
  svn_stream_t *stream;

  db->output = output;
  db->buflen = 0;
  db->done = FALSE;
  db->scratch_pool = svn_pool_create(pool);
  stream = svn_stream_create(db, pool);
  svn_stream_set_write(stream, decode_data);
  svn_stream_set_close(stream, finish_decoding_data);
  return stream;
}


const svn_string_t *
svn_base64_decode_string(const svn_string_t *str, apr_pool_t *pool)
{
  svn_stringbuf_t *decoded = svn_stringbuf_create_empty(pool);
  unsigned char ingroup[4];
  int ingrouplen = 0;
  svn_boolean_t done = FALSE;

  decode_bytes(decoded, str->data, str->len, ingroup, &ingrouplen, &done);
  return svn_stringbuf__morph_into_string(decoded);
}


/* Return a base64-encoded representation of CHECKSUM, allocated in POOL.
   If CHECKSUM->kind is not recognized, return NULL.
   ### That 'NULL' claim was in the header file when this was public, but
   doesn't look true in the implementation.

   ### This is now only used as a new implementation of svn_base64_from_md5();
   it would probably be safer to revert that to its old implementation. */
static svn_stringbuf_t *
base64_from_checksum(const svn_checksum_t *checksum, apr_pool_t *pool)
{
  svn_stringbuf_t *checksum_str;
  unsigned char ingroup[3];
  size_t ingrouplen = 0;
  size_t linelen = 0;
  checksum_str = svn_stringbuf_create_empty(pool);

  encode_bytes(checksum_str, checksum->digest,
               svn_checksum_size(checksum), ingroup, &ingrouplen,
               &linelen, TRUE);
  encode_partial_group(checksum_str, ingroup, ingrouplen, linelen, TRUE);

  /* Our base64-encoding routines append a final newline if any data
     was created at all, so let's hack that off. */
  if (checksum_str->len)
    {
      checksum_str->len--;
      checksum_str->data[checksum_str->len] = 0;
    }

  return checksum_str;
}


svn_stringbuf_t *
svn_base64_from_md5(unsigned char digest[], apr_pool_t *pool)
{
  svn_checksum_t *checksum
    = svn_checksum__from_digest_md5(digest, pool);

  return base64_from_checksum(checksum, pool);
}
