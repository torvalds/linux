/*
 * quoprint.c:  quoted-printable encoding and decoding functions
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
#include "svn_quoprint.h"


/* Caveats:

        (1) This code is for the encoding and decoding of binary data
            only.  Thus, CRLF sequences are encoded as =0D=0A, and we
            don't have to worry about tabs and spaces coming before
            hard newlines, since there aren't any.

        (2) The decoder does no error reporting, and instead throws
            away invalid sequences.  It also discards CRLF sequences,
            since those can only appear in the encoding of text data.

        (3) The decoder does not strip whitespace at the end of a
            line, so it is not actually compliant with RFC 2045.
            (Such whitespace should never occur, even in the encoding
            of text data, but RFC 2045 requires a decoder to detect
            that a transport agent has added trailing whitespace).

        (4) The encoder is tailored to make output embeddable in XML,
            which means it quotes <>'"& as well as the characters
            required by RFC 2045.  */

#define QUOPRINT_LINELEN 76
#define VALID_LITERAL(c) ((c) == '\t' || ((c) >= ' ' && (c) <= '~' \
                                          && (c) != '='))
#define ENCODE_AS_LITERAL(c) (VALID_LITERAL(c) && (c) != '\t' && (c) != '<' \
                              && (c) != '>' && (c) != '\'' && (c) != '"' \
                              && (c) != '&')
static const char hextab[] = "0123456789ABCDEF";



/* Binary input --> quoted-printable-encoded output */

struct encode_baton {
  svn_stream_t *output;
  int linelen;                  /* Bytes output so far on this line */
  apr_pool_t *pool;
};


/* Quoted-printable-encode a byte string which may or may not be the
   totality of the data being encoded.  *LINELEN carries the length of
   the current output line; initialize it to 0.  Output will be
   appended to STR.  */
static void
encode_bytes(svn_stringbuf_t *str, const char *data, apr_size_t len,
             int *linelen)
{
  char buf[3];
  const char *p;

  /* Keep encoding three-byte groups until we run out.  */
  for (p = data; p < data + len; p++)
    {
      /* Encode this character.  */
      if (ENCODE_AS_LITERAL(*p))
        {
          svn_stringbuf_appendbyte(str, *p);
          (*linelen)++;
        }
      else
        {
          buf[0] = '=';
          buf[1] = hextab[(*p >> 4) & 0xf];
          buf[2] = hextab[*p & 0xf];
          svn_stringbuf_appendbytes(str, buf, 3);
          *linelen += 3;
        }

      /* Make sure our output lines don't exceed QUOPRINT_LINELEN.  */
      if (*linelen + 3 > QUOPRINT_LINELEN)
        {
          svn_stringbuf_appendcstr(str, "=\n");
          *linelen = 0;
        }
    }
}


/* Write handler for svn_quoprint_encode.  */
static svn_error_t *
encode_data(void *baton, const char *data, apr_size_t *len)
{
  struct encode_baton *eb = baton;
  apr_pool_t *subpool = svn_pool_create(eb->pool);
  svn_stringbuf_t *encoded = svn_stringbuf_create_empty(subpool);
  apr_size_t enclen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Encode this block of data and write it out.  */
  encode_bytes(encoded, data, *len, &eb->linelen);
  enclen = encoded->len;
  if (enclen != 0)
    err = svn_stream_write(eb->output, encoded->data, &enclen);
  svn_pool_destroy(subpool);
  return err;
}


/* Close handler for svn_quoprint_encode().  */
static svn_error_t *
finish_encoding_data(void *baton)
{
  struct encode_baton *eb = baton;
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t len;

  /* Terminate the current output line if it's not empty.  */
  if (eb->linelen > 0)
    {
      len = 2;
      err = svn_stream_write(eb->output, "=\n", &len);
    }

  /* Pass on the close request and clean up the baton.  */
  if (err == SVN_NO_ERROR)
    err = svn_stream_close(eb->output);
  svn_pool_destroy(eb->pool);
  return err;
}


svn_stream_t *
svn_quoprint_encode(svn_stream_t *output, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  struct encode_baton *eb = apr_palloc(subpool, sizeof(*eb));
  svn_stream_t *stream;

  eb->output = output;
  eb->linelen = 0;
  eb->pool = subpool;
  stream = svn_stream_create(eb, pool);
  svn_stream_set_write(stream, encode_data);
  svn_stream_set_close(stream, finish_encoding_data);
  return stream;
}


svn_stringbuf_t *
svn_quoprint_encode_string(const svn_stringbuf_t *str, apr_pool_t *pool)
{
  svn_stringbuf_t *encoded = svn_stringbuf_create_empty(pool);
  int linelen = 0;

  encode_bytes(encoded, str->data, str->len, &linelen);
  if (linelen > 0)
    svn_stringbuf_appendcstr(encoded, "=\n");
  return encoded;
}



/* Quoted-printable-encoded input --> binary output */

struct decode_baton {
  svn_stream_t *output;
  char buf[3];                  /* Bytes waiting to be decoded */
  int buflen;                   /* Number of bytes waiting */
  apr_pool_t *pool;
};


/* Decode a byte string which may or may not be the total amount of
   data being decoded.  INBUF and *INBUFLEN carry the leftover bytes
   from call to call.  Have room for four bytes in INBUF and
   initialize *INBUFLEN to 0 and *DONE to FALSE.  Output will be
   appended to STR.  */
static void
decode_bytes(svn_stringbuf_t *str, const char *data, apr_size_t len,
             char *inbuf, int *inbuflen)
{
  const char *p, *find1, *find2;
  char c;

  for (p = data; p <= data + len; p++)
    {
      /* Append this byte to the buffer and see what we have.  */
      inbuf[(*inbuflen)++] = *p;
      if (*inbuf != '=')
        {
          /* Literal character; append it if it's valid as such.  */
          if (VALID_LITERAL(*inbuf))
            svn_stringbuf_appendbyte(str, *inbuf);
          *inbuflen = 0;
        }
      else if (*inbuf == '=' && *inbuflen == 2 && inbuf[1] == '\n')
        {
          /* Soft newline; ignore.  */
          *inbuflen = 0;
        }
      else if (*inbuf == '=' && *inbuflen == 3)
        {
          /* Encoded character; decode it and append.  */
          find1 = strchr(hextab, inbuf[1]);
          find2 = strchr(hextab, inbuf[2]);
          if (find1 != NULL && find2 != NULL)
            {
              c = (char)(((find1 - hextab) << 4) | (find2 - hextab));
              svn_stringbuf_appendbyte(str, c);
            }
          *inbuflen = 0;
        }
    }
}


/* Write handler for svn_quoprint_decode.  */
static svn_error_t *
decode_data(void *baton, const char *data, apr_size_t *len)
{
  struct decode_baton *db = baton;
  apr_pool_t *subpool;
  svn_stringbuf_t *decoded;
  apr_size_t declen;
  svn_error_t *err = SVN_NO_ERROR;

  /* Decode this block of data.  */
  subpool = svn_pool_create(db->pool);
  decoded = svn_stringbuf_create_empty(subpool);
  decode_bytes(decoded, data, *len, db->buf, &db->buflen);

  /* Write the output, clean up, go home.  */
  declen = decoded->len;
  if (declen != 0)
    err = svn_stream_write(db->output, decoded->data, &declen);
  svn_pool_destroy(subpool);
  return err;
}


/* Close handler for svn_quoprint_decode().  */
static svn_error_t *
finish_decoding_data(void *baton)
{
  struct decode_baton *db = baton;
  svn_error_t *err;

  /* Pass on the close request and clean up the baton.  */
  err = svn_stream_close(db->output);
  svn_pool_destroy(db->pool);
  return err;
}


svn_stream_t *
svn_quoprint_decode(svn_stream_t *output, apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  struct decode_baton *db = apr_palloc(subpool, sizeof(*db));
  svn_stream_t *stream;

  db->output = output;
  db->buflen = 0;
  db->pool = subpool;
  stream = svn_stream_create(db, pool);
  svn_stream_set_write(stream, decode_data);
  svn_stream_set_close(stream, finish_decoding_data);
  return stream;
}


svn_stringbuf_t *
svn_quoprint_decode_string(const svn_stringbuf_t *str, apr_pool_t *pool)
{
  svn_stringbuf_t *decoded = svn_stringbuf_create_empty(pool);
  char ingroup[4];
  int ingrouplen = 0;

  decode_bytes(decoded, str->data, str->len, ingroup, &ingrouplen);
  return decoded;
}
