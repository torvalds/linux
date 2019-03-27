/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This is derived from material copyright RSA Data Security, Inc.
 * Their notice is reproduced below in its entirety.
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD4 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD4 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#include "apr_strings.h"
#include "apr_md4.h"
#include "apr_lib.h"

#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Constants for MD4Transform routine.
 */
 
#define S11 3
#define S12 7
#define S13 11
#define S14 19
#define S21 3
#define S22 5
#define S23 9
#define S24 13
#define S31 3
#define S32 9
#define S33 11
#define S34 15
 
static void MD4Transform(apr_uint32_t state[4], const unsigned char block[64]);
static void Encode(unsigned char *output, const apr_uint32_t *input,
                   unsigned int len);
static void Decode(apr_uint32_t *output, const unsigned char *input,
                   unsigned int len);

static unsigned char PADDING[64] =
{
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#if APR_CHARSET_EBCDIC
static apr_xlate_t *xlate_ebcdic_to_ascii; /* used in apr_md4_encode() */
#endif

/* F, G and I are basic MD4 functions.
 */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))

/* ROTATE_LEFT rotates x left n bits.
 */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG and HH are transformations for rounds 1, 2 and 3 */
/* Rotation is separate from addition to prevent recomputation */

#define FF(a, b, c, d, x, s) { \
  (a) += F ((b), (c), (d)) + (x); \
  (a) = ROTATE_LEFT ((a), (s)); \
  }
#define GG(a, b, c, d, x, s) { \
  (a) += G ((b), (c), (d)) + (x) + (apr_uint32_t)0x5a827999; \
  (a) = ROTATE_LEFT ((a), (s)); \
  }
#define HH(a, b, c, d, x, s) { \
  (a) += H ((b), (c), (d)) + (x) + (apr_uint32_t)0x6ed9eba1; \
  (a) = ROTATE_LEFT ((a), (s)); \
  }

/* MD4 initialization. Begins an MD4 operation, writing a new context.
 */
APU_DECLARE(apr_status_t) apr_md4_init(apr_md4_ctx_t *context)
{
    context->count[0] = context->count[1] = 0;

    /* Load magic initialization constants. */
    context->state[0] = 0x67452301;
    context->state[1] = 0xefcdab89;
    context->state[2] = 0x98badcfe;
    context->state[3] = 0x10325476;
    
#if APR_HAS_XLATE
    context->xlate = NULL;
#endif

    return APR_SUCCESS;
}

#if APR_HAS_XLATE
/* MD4 translation setup.  Provides the APR translation handle
 * to be used for translating the content before calculating the
 * digest.
 */
APU_DECLARE(apr_status_t) apr_md4_set_xlate(apr_md4_ctx_t *context, 
                                            apr_xlate_t *xlate)
{
    apr_status_t rv;
    int is_sb;

    /* TODO: remove the single-byte-only restriction from this code
     */
    rv = apr_xlate_sb_get(xlate, &is_sb);
    if (rv != APR_SUCCESS) {
        return rv;
    }
    if (!is_sb) {
        return APR_EINVAL;
    }
    context->xlate = xlate;
    return APR_SUCCESS;
}
#endif /* APR_HAS_XLATE */

/* MD4 block update operation. Continues an MD4 message-digest
 * operation, processing another message block, and updating the
 * context.
 */
APU_DECLARE(apr_status_t) apr_md4_update(apr_md4_ctx_t *context,
                                         const unsigned char *input,
                                         apr_size_t inputLen)
{
    unsigned int i, idx, partLen;
#if APR_HAS_XLATE
    apr_size_t inbytes_left, outbytes_left;
#endif

    /* Compute number of bytes mod 64 */
    idx = (unsigned int)((context->count[0] >> 3) & 0x3F);

    /* Update number of bits */
    if ((context->count[0] += ((apr_uint32_t)inputLen << 3)) 
            < ((apr_uint32_t)inputLen << 3))
        context->count[1]++;
    context->count[1] += (apr_uint32_t)inputLen >> 29;

    partLen = 64 - idx;

    /* Transform as many times as possible. */
#if !APR_HAS_XLATE
    if (inputLen >= partLen) {
        memcpy(&context->buffer[idx], input, partLen);
        MD4Transform(context->state, context->buffer);

        for (i = partLen; i + 63 < inputLen; i += 64)
            MD4Transform(context->state, &input[i]);

        idx = 0;
    }
    else
        i = 0;

    /* Buffer remaining input */
    memcpy(&context->buffer[idx], &input[i], inputLen - i);
#else /*APR_HAS_XLATE*/
    if (inputLen >= partLen) {
        if (context->xlate) {
            inbytes_left = outbytes_left = partLen;
            apr_xlate_conv_buffer(context->xlate, (const char *)input, 
                                  &inbytes_left, 
                                  (char *)&context->buffer[idx], 
                                  &outbytes_left);
        }
        else {
            memcpy(&context->buffer[idx], input, partLen);
        }
        MD4Transform(context->state, context->buffer);

        for (i = partLen; i + 63 < inputLen; i += 64) {
            if (context->xlate) {
                unsigned char inp_tmp[64];
                inbytes_left = outbytes_left = 64;
                apr_xlate_conv_buffer(context->xlate, (const char *)&input[i], 
                                      &inbytes_left,
                                      (char *)inp_tmp, &outbytes_left);
                MD4Transform(context->state, inp_tmp);
            }
            else {
                MD4Transform(context->state, &input[i]);
            }
        }

        idx = 0;
    }
    else
        i = 0;

    /* Buffer remaining input */
    if (context->xlate) {
        inbytes_left = outbytes_left = inputLen - i;
        apr_xlate_conv_buffer(context->xlate, (const char *)&input[i], 
                              &inbytes_left, (char *)&context->buffer[idx], 
                              &outbytes_left);
    }
    else {
        memcpy(&context->buffer[idx], &input[i], inputLen - i);
    }
#endif /*APR_HAS_XLATE*/
    return APR_SUCCESS;
}

/* MD4 finalization. Ends an MD4 message-digest operation, writing the
 * the message digest and zeroizing the context.
 */
APU_DECLARE(apr_status_t) apr_md4_final(
                                    unsigned char digest[APR_MD4_DIGESTSIZE],
                                    apr_md4_ctx_t *context)
{
    unsigned char bits[8];
    unsigned int idx, padLen;

    /* Save number of bits */
    Encode(bits, context->count, 8);

#if APR_HAS_XLATE
    /* apr_md4_update() should not translate for this final round. */
    context->xlate = NULL;
#endif /*APR_HAS_XLATE*/

    /* Pad out to 56 mod 64. */
    idx = (unsigned int) ((context->count[0] >> 3) & 0x3f);
    padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    apr_md4_update(context, PADDING, padLen);

    /* Append length (before padding) */
    apr_md4_update(context, bits, 8);

    /* Store state in digest */
    Encode(digest, context->state, APR_MD4_DIGESTSIZE);

    /* Zeroize sensitive information. */
    memset(context, 0, sizeof(*context));
    
    return APR_SUCCESS;
}

/* MD4 computation in one step (init, update, final)
 */
APU_DECLARE(apr_status_t) apr_md4(unsigned char digest[APR_MD4_DIGESTSIZE],
                                  const unsigned char *input,
                                  apr_size_t inputLen)
{
    apr_md4_ctx_t ctx;
    apr_status_t rv;

    apr_md4_init(&ctx);

    if ((rv = apr_md4_update(&ctx, input, inputLen)) != APR_SUCCESS)
        return rv;

    return apr_md4_final(digest, &ctx);
}

/* MD4 basic transformation. Transforms state based on block. */
static void MD4Transform(apr_uint32_t state[4], const unsigned char block[64])
{
    apr_uint32_t a = state[0], b = state[1], c = state[2], d = state[3],
                 x[APR_MD4_DIGESTSIZE];

    Decode(x, block, 64);

    /* Round 1 */
    FF (a, b, c, d, x[ 0], S11); /* 1 */
    FF (d, a, b, c, x[ 1], S12); /* 2 */
    FF (c, d, a, b, x[ 2], S13); /* 3 */
    FF (b, c, d, a, x[ 3], S14); /* 4 */
    FF (a, b, c, d, x[ 4], S11); /* 5 */
    FF (d, a, b, c, x[ 5], S12); /* 6 */
    FF (c, d, a, b, x[ 6], S13); /* 7 */
    FF (b, c, d, a, x[ 7], S14); /* 8 */
    FF (a, b, c, d, x[ 8], S11); /* 9 */
    FF (d, a, b, c, x[ 9], S12); /* 10 */
    FF (c, d, a, b, x[10], S13); /* 11 */
    FF (b, c, d, a, x[11], S14); /* 12 */
    FF (a, b, c, d, x[12], S11); /* 13 */
    FF (d, a, b, c, x[13], S12); /* 14 */
    FF (c, d, a, b, x[14], S13); /* 15 */
    FF (b, c, d, a, x[15], S14); /* 16 */

    /* Round 2 */
    GG (a, b, c, d, x[ 0], S21); /* 17 */
    GG (d, a, b, c, x[ 4], S22); /* 18 */
    GG (c, d, a, b, x[ 8], S23); /* 19 */
    GG (b, c, d, a, x[12], S24); /* 20 */
    GG (a, b, c, d, x[ 1], S21); /* 21 */
    GG (d, a, b, c, x[ 5], S22); /* 22 */
    GG (c, d, a, b, x[ 9], S23); /* 23 */
    GG (b, c, d, a, x[13], S24); /* 24 */
    GG (a, b, c, d, x[ 2], S21); /* 25 */
    GG (d, a, b, c, x[ 6], S22); /* 26 */
    GG (c, d, a, b, x[10], S23); /* 27 */
    GG (b, c, d, a, x[14], S24); /* 28 */
    GG (a, b, c, d, x[ 3], S21); /* 29 */
    GG (d, a, b, c, x[ 7], S22); /* 30 */
    GG (c, d, a, b, x[11], S23); /* 31 */
    GG (b, c, d, a, x[15], S24); /* 32 */

    /* Round 3 */
    HH (a, b, c, d, x[ 0], S31); /* 33 */
    HH (d, a, b, c, x[ 8], S32); /* 34 */
    HH (c, d, a, b, x[ 4], S33); /* 35 */
    HH (b, c, d, a, x[12], S34); /* 36 */
    HH (a, b, c, d, x[ 2], S31); /* 37 */
    HH (d, a, b, c, x[10], S32); /* 38 */
    HH (c, d, a, b, x[ 6], S33); /* 39 */
    HH (b, c, d, a, x[14], S34); /* 40 */
    HH (a, b, c, d, x[ 1], S31); /* 41 */
    HH (d, a, b, c, x[ 9], S32); /* 42 */
    HH (c, d, a, b, x[ 5], S33); /* 43 */
    HH (b, c, d, a, x[13], S34); /* 44 */
    HH (a, b, c, d, x[ 3], S31); /* 45 */
    HH (d, a, b, c, x[11], S32); /* 46 */
    HH (c, d, a, b, x[ 7], S33); /* 47 */
    HH (b, c, d, a, x[15], S34); /* 48 */

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    
    /* Zeroize sensitive information. */
    memset(x, 0, sizeof(x));
}

/* Encodes input (apr_uint32_t) into output (unsigned char). Assumes len is
 * a multiple of 4.
 */
static void Encode(unsigned char *output, const apr_uint32_t *input,
                   unsigned int len)
{
    unsigned int i, j;
    apr_uint32_t k;

    for (i = 0, j = 0; j < len; i++, j += 4) {
        k = input[i];
        output[j]     = (unsigned char)(k & 0xff);
        output[j + 1] = (unsigned char)((k >> 8)  & 0xff);
        output[j + 2] = (unsigned char)((k >> 16) & 0xff);
        output[j + 3] = (unsigned char)((k >> 24) & 0xff);
    }
}

/* Decodes input (unsigned char) into output (apr_uint32_t). Assumes len is
 * a multiple of 4.
 */
static void Decode(apr_uint32_t *output, const unsigned char *input,
                   unsigned int len)
{
    unsigned int i, j;

    for (i = 0, j = 0; j < len; i++, j += 4)
        output[i] = ((apr_uint32_t)input[j])             |
                    (((apr_uint32_t)input[j + 1]) << 8)  |
                    (((apr_uint32_t)input[j + 2]) << 16) |
                    (((apr_uint32_t)input[j + 3]) << 24);
}

#if APR_CHARSET_EBCDIC
APU_DECLARE(apr_status_t) apr_MD4InitEBCDIC(apr_xlate_t *xlate)
{
    xlate_ebcdic_to_ascii = xlate;
    return APR_SUCCESS;
}
#endif
