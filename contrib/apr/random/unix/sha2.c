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
 */
/*
 * FILE:        sha2.c
 * AUTHOR:      Aaron D. Gifford <me@aarongifford.com>
 *
 * A licence was granted to the ASF by Aaron on 4 November 2003.
 */

#include <string.h>     /* memcpy()/memset() or bcopy()/bzero() */
#include <assert.h>     /* assert() */
#include "sha2.h"

/*
 * ASSERT NOTE:
 * Some sanity checking code is included using assert().  On my FreeBSD
 * system, this additional code can be removed by compiling with NDEBUG
 * defined.  Check your own systems manpage on assert() to see how to
 * compile WITHOUT the sanity checking code on your system.
 *
 * UNROLLED TRANSFORM LOOP NOTE:
 * You can define SHA2_UNROLL_TRANSFORM to use the unrolled transform
 * loop version for the hash transform rounds (defined using macros
 * later in this file).  Either define on the command line, for example:
 *
 *   cc -DSHA2_UNROLL_TRANSFORM -o sha2 sha2.c sha2prog.c
 *
 * or define below:
 *
 *   #define SHA2_UNROLL_TRANSFORM
 *
 */

/*** SHA-256/384/512 Machine Architecture Definitions *****************/
typedef apr_byte_t   sha2_byte;         /* Exactly 1 byte */
typedef apr_uint32_t sha2_word32;       /* Exactly 4 bytes */
typedef apr_uint64_t sha2_word64;       /* Exactly 8 bytes */

/*** SHA-256/384/512 Various Length Definitions ***********************/
/* NOTE: Most of these are in sha2.h */
#define SHA256_SHORT_BLOCK_LENGTH       (SHA256_BLOCK_LENGTH - 8)


/*** ENDIAN REVERSAL MACROS *******************************************/
#if !APR_IS_BIGENDIAN
#define REVERSE32(w,x)  { \
        sha2_word32 tmp = (w); \
        tmp = (tmp >> 16) | (tmp << 16); \
        (x) = ((tmp & 0xff00ff00UL) >> 8) | ((tmp & 0x00ff00ffUL) << 8); \
}
#define REVERSE64(w,x)  { \
        sha2_word64 tmp = (w); \
        tmp = (tmp >> 32) | (tmp << 32); \
        tmp = ((tmp & APR_UINT64_C(0xff00ff00ff00ff00)) >> 8) | \
              ((tmp & APR_UINT64_C(0x00ff00ff00ff00ff)) << 8); \
        (x) = ((tmp & APR_UINT64_C(0xffff0000ffff0000)) >> 16) | \
              ((tmp & APR_UINT64_C(0x0000ffff0000ffff)) << 16); \
}
#endif /* !APR_IS_BIGENDIAN */

/*
 * Macro for incrementally adding the unsigned 64-bit integer n to the
 * unsigned 128-bit integer (represented using a two-element array of
 * 64-bit words):
 */
#define ADDINC128(w,n)  { \
        (w)[0] += (sha2_word64)(n); \
        if ((w)[0] < (n)) { \
                (w)[1]++; \
        } \
}

/*
 * Macros for copying blocks of memory and for zeroing out ranges
 * of memory.  Using these macros makes it easy to switch from
 * using memset()/memcpy() and using bzero()/bcopy().
 *
 * Please define either SHA2_USE_MEMSET_MEMCPY or define
 * SHA2_USE_BZERO_BCOPY depending on which function set you
 * choose to use:
 */
#if !defined(SHA2_USE_MEMSET_MEMCPY) && !defined(SHA2_USE_BZERO_BCOPY)
/* Default to memset()/memcpy() if no option is specified */
#define SHA2_USE_MEMSET_MEMCPY  1
#endif
#if defined(SHA2_USE_MEMSET_MEMCPY) && defined(SHA2_USE_BZERO_BCOPY)
/* Abort with an error if BOTH options are defined */
#error Define either SHA2_USE_MEMSET_MEMCPY or SHA2_USE_BZERO_BCOPY, not both!
#endif

#ifdef SHA2_USE_MEMSET_MEMCPY
#define MEMSET_BZERO(p,l)       memset((p), 0, (l))
#define MEMCPY_BCOPY(d,s,l)     memcpy((d), (s), (l))
#endif
#ifdef SHA2_USE_BZERO_BCOPY
#define MEMSET_BZERO(p,l)       bzero((p), (l))
#define MEMCPY_BCOPY(d,s,l)     bcopy((s), (d), (l))
#endif


/*** THE SIX LOGICAL FUNCTIONS ****************************************/
/*
 * Bit shifting and rotation (used by the six SHA-XYZ logical functions:
 *
 *   NOTE:  The naming of R and S appears backwards here (R is a SHIFT and
 *   S is a ROTATION) because the SHA-256/384/512 description document
 *   (see http://csrc.nist.gov/cryptval/shs/sha256-384-512.pdf) uses this
 *   same "backwards" definition.
 */
/* Shift-right (used in SHA-256, SHA-384, and SHA-512): */
#define R(b,x)          ((x) >> (b))
/* 32-bit Rotate-right (used in SHA-256): */
#define S32(b,x)        (((x) >> (b)) | ((x) << (32 - (b))))
/* 64-bit Rotate-right (used in SHA-384 and SHA-512): */
#define S64(b,x)        (((x) >> (b)) | ((x) << (64 - (b))))

/* Two of six logical functions used in SHA-256, SHA-384, and SHA-512: */
#define Ch(x,y,z)       (((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x,y,z)      (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Four of six logical functions used in SHA-256: */
#define Sigma0_256(x)   (S32(2,  (x)) ^ S32(13, (x)) ^ S32(22, (x)))
#define Sigma1_256(x)   (S32(6,  (x)) ^ S32(11, (x)) ^ S32(25, (x)))
#define sigma0_256(x)   (S32(7,  (x)) ^ S32(18, (x)) ^ R(3 ,   (x)))
#define sigma1_256(x)   (S32(17, (x)) ^ S32(19, (x)) ^ R(10,   (x)))

/* Four of six logical functions used in SHA-384 and SHA-512: */
#define Sigma0_512(x)   (S64(28, (x)) ^ S64(34, (x)) ^ S64(39, (x)))
#define Sigma1_512(x)   (S64(14, (x)) ^ S64(18, (x)) ^ S64(41, (x)))
#define sigma0_512(x)   (S64( 1, (x)) ^ S64( 8, (x)) ^ R( 7,   (x)))
#define sigma1_512(x)   (S64(19, (x)) ^ S64(61, (x)) ^ R( 6,   (x)))

/*** INTERNAL FUNCTION PROTOTYPES *************************************/
/* NOTE: These should not be accessed directly from outside this
 * library -- they are intended for private internal visibility/use
 * only.
 */
void apr__SHA256_Transform(SHA256_CTX*, const sha2_word32*);


/*** SHA-XYZ INITIAL HASH VALUES AND CONSTANTS ************************/
/* Hash constant words K for SHA-256: */
static const sha2_word32 K256[64] = {
        0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
        0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
        0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
        0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
        0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
        0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
        0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
        0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
        0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
        0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
        0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
        0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
        0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
        0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
        0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
        0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* Initial hash value H for SHA-256: */
static const sha2_word32 sha256_initial_hash_value[8] = {
        0x6a09e667UL,
        0xbb67ae85UL,
        0x3c6ef372UL,
        0xa54ff53aUL,
        0x510e527fUL,
        0x9b05688cUL,
        0x1f83d9abUL,
        0x5be0cd19UL
};

/*
 * Constant used by SHA256/384/512_End() functions for converting the
 * digest to a readable hexadecimal character string:
 */
static const char *sha2_hex_digits = "0123456789abcdef";


/*** SHA-256: *********************************************************/
void apr__SHA256_Init(SHA256_CTX* context) {
        if (context == (SHA256_CTX*)0) {
                return;
        }
        MEMCPY_BCOPY(context->state, sha256_initial_hash_value, SHA256_DIGEST_LENGTH);
        MEMSET_BZERO(context->buffer, SHA256_BLOCK_LENGTH);
        context->bitcount = 0;
}

#ifdef SHA2_UNROLL_TRANSFORM

/* Unrolled SHA-256 round macros: */

#if !APR_IS_BIGENDIAN

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h)       \
        REVERSE32(*data++, W256[j]); \
        T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
             K256[j] + W256[j]; \
        (d) += T1; \
        (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
        j++


#else /* APR_IS_BIGENDIAN */

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h)       \
        T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
             K256[j] + (W256[j] = *data++); \
        (d) += T1; \
        (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
        j++

#endif /* APR_IS_BIGENDIAN */

#define ROUND256(a,b,c,d,e,f,g,h)       \
        s0 = W256[(j+1)&0x0f]; \
        s0 = sigma0_256(s0); \
        s1 = W256[(j+14)&0x0f]; \
        s1 = sigma1_256(s1); \
        T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + K256[j] + \
             (W256[j&0x0f] += s1 + W256[(j+9)&0x0f] + s0); \
        (d) += T1; \
        (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
        j++

void apr__SHA256_Transform(SHA256_CTX* context, const sha2_word32* data) {
        sha2_word32     a, b, c, d, e, f, g, h, s0, s1;
        sha2_word32     T1, *W256;
        int             j;

        W256 = (sha2_word32*)context->buffer;

        /* Initialize registers with the prev. intermediate value */
        a = context->state[0];
        b = context->state[1];
        c = context->state[2];
        d = context->state[3];
        e = context->state[4];
        f = context->state[5];
        g = context->state[6];
        h = context->state[7];

        j = 0;
        do {
                /* Rounds 0 to 15 (unrolled): */
                ROUND256_0_TO_15(a,b,c,d,e,f,g,h);
                ROUND256_0_TO_15(h,a,b,c,d,e,f,g);
                ROUND256_0_TO_15(g,h,a,b,c,d,e,f);
                ROUND256_0_TO_15(f,g,h,a,b,c,d,e);
                ROUND256_0_TO_15(e,f,g,h,a,b,c,d);
                ROUND256_0_TO_15(d,e,f,g,h,a,b,c);
                ROUND256_0_TO_15(c,d,e,f,g,h,a,b);
                ROUND256_0_TO_15(b,c,d,e,f,g,h,a);
        } while (j < 16);

        /* Now for the remaining rounds to 64: */
        do {
                ROUND256(a,b,c,d,e,f,g,h);
                ROUND256(h,a,b,c,d,e,f,g);
                ROUND256(g,h,a,b,c,d,e,f);
                ROUND256(f,g,h,a,b,c,d,e);
                ROUND256(e,f,g,h,a,b,c,d);
                ROUND256(d,e,f,g,h,a,b,c);
                ROUND256(c,d,e,f,g,h,a,b);
                ROUND256(b,c,d,e,f,g,h,a);
        } while (j < 64);

        /* Compute the current intermediate hash value */
        context->state[0] += a;
        context->state[1] += b;
        context->state[2] += c;
        context->state[3] += d;
        context->state[4] += e;
        context->state[5] += f;
        context->state[6] += g;
        context->state[7] += h;

        /* Clean up */
        a = b = c = d = e = f = g = h = T1 = 0;
}

#else /* SHA2_UNROLL_TRANSFORM */

void apr__SHA256_Transform(SHA256_CTX* context, const sha2_word32* data) {
        sha2_word32     a, b, c, d, e, f, g, h, s0, s1;
        sha2_word32     T1, T2, *W256;
        int             j;

        W256 = (sha2_word32*)context->buffer;

        /* Initialize registers with the prev. intermediate value */
        a = context->state[0];
        b = context->state[1];
        c = context->state[2];
        d = context->state[3];
        e = context->state[4];
        f = context->state[5];
        g = context->state[6];
        h = context->state[7];

        j = 0;
        do {
#if !APR_IS_BIGENDIAN
                /* Copy data while converting to host byte order */
                REVERSE32(*data++,W256[j]);
                /* Apply the SHA-256 compression function to update a..h */
                T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] + W256[j];
#else /* APR_IS_BIGENDIAN */
                /* Apply the SHA-256 compression function to update a..h with copy */
                T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] + (W256[j] = *data++);
#endif /* APR_IS_BIGENDIAN */
                T2 = Sigma0_256(a) + Maj(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + T1;
                d = c;
                c = b;
                b = a;
                a = T1 + T2;

                j++;
        } while (j < 16);

        do {
                /* Part of the message block expansion: */
                s0 = W256[(j+1)&0x0f];
                s0 = sigma0_256(s0);
                s1 = W256[(j+14)&0x0f]; 
                s1 = sigma1_256(s1);

                /* Apply the SHA-256 compression function to update a..h */
                T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] + 
                     (W256[j&0x0f] += s1 + W256[(j+9)&0x0f] + s0);
                T2 = Sigma0_256(a) + Maj(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + T1;
                d = c;
                c = b;
                b = a;
                a = T1 + T2;

                j++;
        } while (j < 64);

        /* Compute the current intermediate hash value */
        context->state[0] += a;
        context->state[1] += b;
        context->state[2] += c;
        context->state[3] += d;
        context->state[4] += e;
        context->state[5] += f;
        context->state[6] += g;
        context->state[7] += h;

        /* Clean up */
        a = b = c = d = e = f = g = h = T1 = T2 = 0;
}

#endif /* SHA2_UNROLL_TRANSFORM */

void apr__SHA256_Update(SHA256_CTX* context, const sha2_byte *data, size_t len) {
        unsigned int    freespace, usedspace;

        if (len == 0) {
                /* Calling with no data is valid - we do nothing */
                return;
        }

        /* Sanity check: */
        assert(context != (SHA256_CTX*)0 && data != (sha2_byte*)0);

        usedspace = (unsigned int)((context->bitcount >> 3) 
                                 % SHA256_BLOCK_LENGTH);
        if (usedspace > 0) {
                /* Calculate how much free space is available in the buffer */
                freespace = SHA256_BLOCK_LENGTH - usedspace;

                if (len >= freespace) {
                        /* Fill the buffer completely and process it */
                        MEMCPY_BCOPY(&context->buffer[usedspace], data, freespace);
                        context->bitcount += freespace << 3;
                        len -= freespace;
                        data += freespace;
                        apr__SHA256_Transform(context, (sha2_word32*)context->buffer);
                } else {
                        /* The buffer is not yet full */
                        MEMCPY_BCOPY(&context->buffer[usedspace], data, len);
                        context->bitcount += len << 3;
                        /* Clean up: */
                        usedspace = freespace = 0;
                        return;
                }
        }
        while (len >= SHA256_BLOCK_LENGTH) {
                /* Process as many complete blocks as we can */
                apr__SHA256_Transform(context, (sha2_word32*)data);
                context->bitcount += SHA256_BLOCK_LENGTH << 3;
                len -= SHA256_BLOCK_LENGTH;
                data += SHA256_BLOCK_LENGTH;
        }
        if (len > 0) {
                /* There's left-overs, so save 'em */
                MEMCPY_BCOPY(context->buffer, data, len);
                context->bitcount += len << 3;
        }
        /* Clean up: */
        usedspace = freespace = 0;
}

void apr__SHA256_Final(sha2_byte digest[], SHA256_CTX* context) {
        sha2_word32     *d = (sha2_word32*)digest;
        unsigned int    usedspace;

        /* Sanity check: */
        assert(context != (SHA256_CTX*)0);

        /* If no digest buffer is passed, we don't bother doing this: */
        if (digest != (sha2_byte*)0) {
                usedspace = (unsigned int)((context->bitcount >> 3) 
                                         % SHA256_BLOCK_LENGTH);
#if !APR_IS_BIGENDIAN
                /* Convert FROM host byte order */
                REVERSE64(context->bitcount,context->bitcount);
#endif
                if (usedspace > 0) {
                        /* Begin padding with a 1 bit: */
                        context->buffer[usedspace++] = 0x80;

                        if (usedspace <= SHA256_SHORT_BLOCK_LENGTH) {
                                /* Set-up for the last transform: */
                                MEMSET_BZERO(&context->buffer[usedspace], SHA256_SHORT_BLOCK_LENGTH - usedspace);
                        } else {
                                if (usedspace < SHA256_BLOCK_LENGTH) {
                                        MEMSET_BZERO(&context->buffer[usedspace], SHA256_BLOCK_LENGTH - usedspace);
                                }
                                /* Do second-to-last transform: */
                                apr__SHA256_Transform(context, (sha2_word32*)context->buffer);

                                /* And set-up for the last transform: */
                                MEMSET_BZERO(context->buffer, SHA256_SHORT_BLOCK_LENGTH);
                        }
                } else {
                        /* Set-up for the last transform: */
                        MEMSET_BZERO(context->buffer, SHA256_SHORT_BLOCK_LENGTH);

                        /* Begin padding with a 1 bit: */
                        *context->buffer = 0x80;
                }
                /* Set the bit count: */
                {
                        union dummy {
                                apr_uint64_t bitcount;
                                apr_byte_t bytes[8];
                        } bitcount;
                        bitcount.bitcount = context->bitcount;
                        MEMCPY_BCOPY(&context->buffer[SHA256_SHORT_BLOCK_LENGTH], bitcount.bytes, 8);
                }

                /* Final transform: */
                apr__SHA256_Transform(context, (sha2_word32*)context->buffer);

#if !APR_IS_BIGENDIAN
                {
                        /* Convert TO host byte order */
                        int     j;
                        for (j = 0; j < 8; j++) {
                                REVERSE32(context->state[j],context->state[j]);
                                *d++ = context->state[j];
                        }
                }
#else
                MEMCPY_BCOPY(d, context->state, SHA256_DIGEST_LENGTH);
#endif
        }

        /* Clean up state data: */
        MEMSET_BZERO(context, sizeof(*context));
        usedspace = 0;
}

char *apr__SHA256_End(SHA256_CTX* context, char buffer[]) {
        sha2_byte       digest[SHA256_DIGEST_LENGTH], *d = digest;
        int             i;

        /* Sanity check: */
        assert(context != (SHA256_CTX*)0);

        if (buffer != (char*)0) {
                apr__SHA256_Final(digest, context);

                for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                        *buffer++ = sha2_hex_digits[(*d & 0xf0) >> 4];
                        *buffer++ = sha2_hex_digits[*d & 0x0f];
                        d++;
                }
                *buffer = (char)0;
        } else {
                MEMSET_BZERO(context, sizeof(*context));
        }
        MEMSET_BZERO(digest, SHA256_DIGEST_LENGTH);
        return buffer;
}

char* apr__SHA256_Data(const sha2_byte* data, size_t len, char digest[SHA256_DIGEST_STRING_LENGTH]) {
        SHA256_CTX      context;

        apr__SHA256_Init(&context);
        apr__SHA256_Update(&context, data, len);
        return apr__SHA256_End(&context, digest);
}
