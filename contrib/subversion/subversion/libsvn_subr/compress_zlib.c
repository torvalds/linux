/*
 * compress_zlib.c:  zlib data compression routines
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


#include <zlib.h>

#include "private/svn_subr_private.h"
#include "private/svn_error_private.h"

#include "svn_private_config.h"

const char *
svn_zlib__compiled_version(void)
{
  static const char zlib_version_str[] = ZLIB_VERSION;

  return zlib_version_str;
}

const char *
svn_zlib__runtime_version(void)
{
  return zlibVersion();
}


/* The zlib compressBound function was not exported until 1.2.0. */
#if ZLIB_VERNUM >= 0x1200
#define svnCompressBound(LEN) compressBound(LEN)
#else
#define svnCompressBound(LEN) ((LEN) + ((LEN) >> 12) + ((LEN) >> 14) + 11)
#endif

/* For svndiff1, address/instruction/new data under this size will not
   be compressed using zlib as a secondary compressor.  */
#define MIN_COMPRESS_SIZE 512

/* If IN is a string that is >= MIN_COMPRESS_SIZE and the COMPRESSION_LEVEL
   is not SVN_DELTA_COMPRESSION_LEVEL_NONE, zlib compress it and places the
   result in OUT, with an integer prepended specifying the original size.
   If IN is < MIN_COMPRESS_SIZE, or if the compressed version of IN was no
   smaller than the original IN, OUT will be a copy of IN with the size
   prepended as an integer. */
static svn_error_t *
zlib_encode(const char *data,
            apr_size_t len,
            svn_stringbuf_t *out,
            int compression_level)
{
  unsigned long endlen;
  apr_size_t intlen;
  unsigned char buf[SVN__MAX_ENCODED_UINT_LEN], *p;

  svn_stringbuf_setempty(out);
  p = svn__encode_uint(buf, (apr_uint64_t)len);
  svn_stringbuf_appendbytes(out, (const char *)buf, p - buf);

  intlen = out->len;

  /* Compression initialization overhead is considered to large for
     short buffers.  Also, if we don't actually want to compress data,
     ZLIB will produce an output no shorter than the input.  Hence,
     the DATA would directly appended to OUT, so we can do that directly
     without calling ZLIB before. */
  if (len < MIN_COMPRESS_SIZE || compression_level == SVN__COMPRESSION_NONE)
    {
      svn_stringbuf_appendbytes(out, data, len);
    }
  else
    {
      int zerr;

      svn_stringbuf_ensure(out, svnCompressBound(len) + intlen);
      endlen = out->blocksize;

      zerr = compress2((unsigned char *)out->data + intlen, &endlen,
                       (const unsigned char *)data, len,
                       compression_level);
      if (zerr != Z_OK)
        return svn_error_trace(svn_error__wrap_zlib(
                                 zerr, "compress2",
                                 _("Compression of svndiff data failed")));

      /* Compression didn't help :(, just append the original text */
      if (endlen >= len)
        {
          svn_stringbuf_appendbytes(out, data, len);
          return SVN_NO_ERROR;
        }
      out->len = endlen + intlen;
      out->data[out->len] = 0;
    }
  return SVN_NO_ERROR;
}

/* Decode the possibly-zlib compressed string of length INLEN that is in
   IN, into OUT.  We expect an integer is prepended to IN that specifies
   the original size, and that if encoded size == original size, that the
   remaining data is not compressed.
   In that case, we will simply return pointer into IN as data pointer for
   OUT, COPYLESS_ALLOWED has been set.  The, the caller is expected not to
   modify the contents of OUT.
   An error is returned if the decoded length exceeds the given LIMIT.
 */
static svn_error_t *
zlib_decode(const unsigned char *in, apr_size_t inLen, svn_stringbuf_t *out,
            apr_size_t limit)
{
  apr_size_t len;
  apr_uint64_t size;
  const unsigned char *oldplace = in;

  /* First thing in the string is the original length.  */
  in = svn__decode_uint(&size, in, in + inLen);
  len = (apr_size_t)size;
  if (in == NULL || len != size)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of zlib compressed data failed: no size"));
  if (len > limit)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of zlib compressed data failed: "
                              "size too large"));

  /* We need to subtract the size of the encoded original length off the
   *      still remaining input length.  */
  inLen -= (in - oldplace);
  if (inLen == len)
    {
      svn_stringbuf_ensure(out, len);
      memcpy(out->data, in, len);
      out->data[len] = 0;
      out->len = len;

      return SVN_NO_ERROR;
    }
  else
    {
      unsigned long zlen = len;
      int zerr;

      svn_stringbuf_ensure(out, len);
      zerr = uncompress((unsigned char *)out->data, &zlen, in, inLen);
      if (zerr != Z_OK)
        return svn_error_trace(svn_error__wrap_zlib(
                                 zerr, "uncompress",
                                 _("Decompression of svndiff data failed")));

      /* Zlib should not produce something that has a different size than the
         original length we stored. */
      if (zlen != len)
        return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA,
                                NULL,
                                _("Size of uncompressed data "
                                  "does not match stored original length"));
      out->data[zlen] = 0;
      out->len = zlen;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn__compress_zlib(const void *data, apr_size_t len,
                   svn_stringbuf_t *out,
                   int compression_method)
{
  if (   compression_method < SVN__COMPRESSION_NONE
      || compression_method > SVN__COMPRESSION_ZLIB_MAX)
    return svn_error_createf(SVN_ERR_BAD_COMPRESSION_METHOD, NULL,
                             _("Unsupported compression method %d"),
                             compression_method);

  return zlib_encode(data, len, out, compression_method);
}

svn_error_t *
svn__decompress_zlib(const void *data, apr_size_t len,
                     svn_stringbuf_t *out,
                     apr_size_t limit)
{
  return zlib_decode(data, len, out, limit);
}
