/*
 * encode.c:  various data encoding routines
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

#include "private/svn_subr_private.h"

#include "svn_private_config.h"

unsigned char *
svn__encode_uint(unsigned char *p, apr_uint64_t val)
{
  int n;
  apr_uint64_t v;

  /* Figure out how many bytes we'll need.  */
  v = val >> 7;
  n = 1;
  while (v > 0)
    {
      v = v >> 7;
      n++;
    }

  /* Encode the remaining bytes; n is always the number of bytes
     coming after the one we're encoding.  */
  while (--n >= 1)
    *p++ = (unsigned char)(((val >> (n * 7)) | 0x80) & 0xff);

  *p++ = (unsigned char)(val & 0x7f);

  return p;
}

unsigned char *
svn__encode_int(unsigned char *p, apr_int64_t val)
{
  apr_uint64_t value = val;
  value = value & APR_UINT64_C(0x8000000000000000)
        ? APR_UINT64_MAX - (2 * value)
        : 2 * value;

  return svn__encode_uint(p, value);
}

const unsigned char *
svn__decode_uint(apr_uint64_t *val,
                 const unsigned char *p,
                 const unsigned char *end)
{
  apr_uint64_t temp = 0;

  if (end - p > SVN__MAX_ENCODED_UINT_LEN)
    end = p + SVN__MAX_ENCODED_UINT_LEN;

  /* Decode bytes until we're done. */
  while (SVN__PREDICT_TRUE(p < end))
    {
      unsigned int c = *p++;

      if (c < 0x80)
        {
          *val = (temp << 7) | c;
          return p;
        }
      else
        {
          temp = (temp << 7) | (c & 0x7f);
        }
    }

  return NULL;
}

const unsigned char *
svn__decode_int(apr_int64_t *val,
                const unsigned char *p,
                const unsigned char *end)
{
  apr_uint64_t value;
  const unsigned char *result = svn__decode_uint(&value, p, end);

  value = value & 1
        ? (APR_UINT64_MAX - value / 2)
        : value / 2;
  *val = (apr_int64_t)value;

  return result;
}
