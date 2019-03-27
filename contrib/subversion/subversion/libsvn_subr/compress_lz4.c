/*
 * compress_lz4.c:  LZ4 data compression routines
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

#include <assert.h>

#include "private/svn_subr_private.h"

#include "svn_private_config.h"

#if SVN_INTERNAL_LZ4
#include "lz4/lz4internal.h"
#else
#include <lz4.h>
#endif

svn_error_t *
svn__compress_lz4(const void *data, apr_size_t len,
                  svn_stringbuf_t *out)
{
  apr_size_t hdrlen;
  unsigned char buf[SVN__MAX_ENCODED_UINT_LEN];
  unsigned char *p;
  int compressed_data_len;
  int max_compressed_data_len;

  assert(len <= LZ4_MAX_INPUT_SIZE);

  p = svn__encode_uint(buf, (apr_uint64_t)len);
  hdrlen = p - buf;
  max_compressed_data_len = LZ4_compressBound((int)len);
  svn_stringbuf_setempty(out);
  svn_stringbuf_ensure(out, max_compressed_data_len + hdrlen);
  svn_stringbuf_appendbytes(out, (const char *)buf, hdrlen);
  compressed_data_len = LZ4_compress_default(data, out->data + out->len,
                                             (int)len, max_compressed_data_len);
  if (!compressed_data_len)
    return svn_error_create(SVN_ERR_LZ4_COMPRESSION_FAILED, NULL, NULL);

  if (compressed_data_len >= (int)len)
    {
      /* Compression didn't help :(, just append the original text */
      svn_stringbuf_appendbytes(out, data, len);
    }
  else
    {
      out->len += compressed_data_len;
      out->data[out->len] = 0;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn__decompress_lz4(const void *data, apr_size_t len,
                    svn_stringbuf_t *out,
                    apr_size_t limit)
{
  apr_size_t hdrlen;
  int compressed_data_len;
  int decompressed_data_len;
  apr_uint64_t u64;
  const unsigned char *p = data;
  int rv;

  assert(len <= INT_MAX);
  assert(limit <= INT_MAX);

  /* First thing in the string is the original length.  */
  p = svn__decode_uint(&u64, p, p + len);
  if (p == NULL)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of compressed data failed: "
                              "no size"));
  if (u64 > limit)
    return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA, NULL,
                            _("Decompression of compressed data failed: "
                              "size too large"));
  decompressed_data_len = (int)u64;
  hdrlen = p - (const unsigned char *)data;
  compressed_data_len = (int)(len - hdrlen);

  svn_stringbuf_setempty(out);
  svn_stringbuf_ensure(out, decompressed_data_len);

  if (compressed_data_len == decompressed_data_len)
    {
      /* Data is in the original, uncompressed form. */
      memcpy(out->data, p, decompressed_data_len);
    }
  else
    {
      rv = LZ4_decompress_safe((const char *)p, out->data, compressed_data_len,
                               decompressed_data_len);
      if (rv < 0)
        return svn_error_create(SVN_ERR_LZ4_DECOMPRESSION_FAILED, NULL, NULL);

      if (rv != decompressed_data_len)
        return svn_error_create(SVN_ERR_SVNDIFF_INVALID_COMPRESSED_DATA,
                                NULL,
                                _("Size of uncompressed data "
                                  "does not match stored original length"));
    }

  out->data[decompressed_data_len] = 0;
  out->len = decompressed_data_len;

  return SVN_NO_ERROR;
}

const char *
svn_lz4__compiled_version(void)
{
  static const char lz4_version_str[] = APR_STRINGIFY(LZ4_VERSION_MAJOR) "." \
                                        APR_STRINGIFY(LZ4_VERSION_MINOR) "." \
                                        APR_STRINGIFY(LZ4_VERSION_RELEASE);

  return lz4_version_str;
}

int
svn_lz4__runtime_version(void)
{
  return LZ4_versionNumber();
}
