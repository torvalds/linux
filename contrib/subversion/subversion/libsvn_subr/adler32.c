/*
 * adler32.c :  routines for handling Adler-32 checksums
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
#include <zlib.h>

#include "private/svn_adler32.h"

/**
 * An Adler-32 implementation per RFC1950.
 *
 * "The Adler-32 algorithm is much faster than the CRC32 algorithm yet
 * still provides an extremely low probability of undetected errors"
 */

/*
 * 65521 is the largest prime less than 65536.
 * "That 65521 is prime is important to avoid a possible large class of
 *  two-byte errors that leave the check unchanged."
 */
#define ADLER_MOD_BASE 65521

/*
 * Start with CHECKSUM and update the checksum by processing a chunk
 * of DATA sized LEN.
 */
apr_uint32_t
svn__adler32(apr_uint32_t checksum, const char *data, apr_off_t len)
{
  /* The actual limit can be set somewhat higher but should
   * not be lower because the SIMD code would not be used
   * in that case.
   *
   * However, it must be lower than 5552 to make sure our local
   * implementation does not suffer from overflows.
   */
  if (len >= 80)
    {
      /* Larger buffers can be efficiently handled by Marc Adler's
       * optimized code. Also, new zlib versions will come with
       * SIMD code for x86 and x64.
       */
      return (apr_uint32_t)adler32(checksum,
                                   (const Bytef *)data,
                                   (uInt)len);
    }
  else
    {
      const unsigned char *input = (const unsigned char *)data;
      apr_uint32_t s1 = checksum & 0xFFFF;
      apr_uint32_t s2 = checksum >> 16;
      apr_uint32_t b;

      /* Some loop unrolling
       * (approx. one clock tick per byte + 2 ticks loop overhead)
       */
      for (; len >= 8; len -= 8, input += 8)
        {
          s1 += input[0]; s2 += s1;
          s1 += input[1]; s2 += s1;
          s1 += input[2]; s2 += s1;
          s1 += input[3]; s2 += s1;
          s1 += input[4]; s2 += s1;
          s1 += input[5]; s2 += s1;
          s1 += input[6]; s2 += s1;
          s1 += input[7]; s2 += s1;
        }

      /* Adler-32 calculation as a simple two ticks per iteration loop.
       */
      while (len--)
        {
          b = *input++;
          s1 += b;
          s2 += s1;
        }

      return ((s2 % ADLER_MOD_BASE) << 16) | (s1 % ADLER_MOD_BASE);
    }
}
