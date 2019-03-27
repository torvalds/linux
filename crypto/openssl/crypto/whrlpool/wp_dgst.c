/*
 * Copyright 2005-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/**
 * The Whirlpool hashing function.
 *
 * See
 *      P.S.L.M. Barreto, V. Rijmen,
 *      ``The Whirlpool hashing function,''
 *      NESSIE submission, 2000 (tweaked version, 2001),
 *      <https://www.cosic.esat.kuleuven.ac.be/nessie/workshop/submissions/whirlpool.zip>
 *
 * Based on "@version 3.0 (2003.03.12)" by Paulo S.L.M. Barreto and
 * Vincent Rijmen. Lookup "reference implementations" on
 * <http://planeta.terra.com.br/informatica/paulobarreto/>
 *
 * =============================================================================
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * OpenSSL-specific implementation notes.
 *
 * WHIRLPOOL_Update as well as one-stroke WHIRLPOOL both expect
 * number of *bytes* as input length argument. Bit-oriented routine
 * as specified by authors is called WHIRLPOOL_BitUpdate[!] and
 * does not have one-stroke counterpart.
 *
 * WHIRLPOOL_BitUpdate implements byte-oriented loop, essentially
 * to serve WHIRLPOOL_Update. This is done for performance.
 *
 * Unlike authors' reference implementation, block processing
 * routine whirlpool_block is designed to operate on multi-block
 * input. This is done for performance.
 */

#include <openssl/crypto.h>
#include "wp_locl.h"
#include <string.h>

int WHIRLPOOL_Init(WHIRLPOOL_CTX *c)
{
    memset(c, 0, sizeof(*c));
    return 1;
}

int WHIRLPOOL_Update(WHIRLPOOL_CTX *c, const void *_inp, size_t bytes)
{
    /*
     * Well, largest suitable chunk size actually is
     * (1<<(sizeof(size_t)*8-3))-64, but below number is large enough for not
     * to care about excessive calls to WHIRLPOOL_BitUpdate...
     */
    size_t chunk = ((size_t)1) << (sizeof(size_t) * 8 - 4);
    const unsigned char *inp = _inp;

    while (bytes >= chunk) {
        WHIRLPOOL_BitUpdate(c, inp, chunk * 8);
        bytes -= chunk;
        inp += chunk;
    }
    if (bytes)
        WHIRLPOOL_BitUpdate(c, inp, bytes * 8);

    return 1;
}

void WHIRLPOOL_BitUpdate(WHIRLPOOL_CTX *c, const void *_inp, size_t bits)
{
    size_t n;
    unsigned int bitoff = c->bitoff,
        bitrem = bitoff % 8, inpgap = (8 - (unsigned int)bits % 8) & 7;
    const unsigned char *inp = _inp;

    /*
     * This 256-bit increment procedure relies on the size_t being natural
     * size of CPU register, so that we don't have to mask the value in order
     * to detect overflows.
     */
    c->bitlen[0] += bits;
    if (c->bitlen[0] < bits) {  /* overflow */
        n = 1;
        do {
            c->bitlen[n]++;
        } while (c->bitlen[n] == 0
                 && ++n < (WHIRLPOOL_COUNTER / sizeof(size_t)));
    }
#ifndef OPENSSL_SMALL_FOOTPRINT
 reconsider:
    if (inpgap == 0 && bitrem == 0) { /* byte-oriented loop */
        while (bits) {
            if (bitoff == 0 && (n = bits / WHIRLPOOL_BBLOCK)) {
                whirlpool_block(c, inp, n);
                inp += n * WHIRLPOOL_BBLOCK / 8;
                bits %= WHIRLPOOL_BBLOCK;
            } else {
                unsigned int byteoff = bitoff / 8;

                bitrem = WHIRLPOOL_BBLOCK - bitoff; /* re-use bitrem */
                if (bits >= bitrem) {
                    bits -= bitrem;
                    bitrem /= 8;
                    memcpy(c->data + byteoff, inp, bitrem);
                    inp += bitrem;
                    whirlpool_block(c, c->data, 1);
                    bitoff = 0;
                } else {
                    memcpy(c->data + byteoff, inp, bits / 8);
                    bitoff += (unsigned int)bits;
                    bits = 0;
                }
                c->bitoff = bitoff;
            }
        }
    } else                      /* bit-oriented loop */
#endif
    {
        /*-
                   inp
                   |
                   +-------+-------+-------
                      |||||||||||||||||||||
                   +-------+-------+-------
        +-------+-------+-------+-------+-------
        ||||||||||||||                          c->data
        +-------+-------+-------+-------+-------
                |
                c->bitoff/8
        */
        while (bits) {
            unsigned int byteoff = bitoff / 8;
            unsigned char b;

#ifndef OPENSSL_SMALL_FOOTPRINT
            if (bitrem == inpgap) {
                c->data[byteoff++] |= inp[0] & (0xff >> inpgap);
                inpgap = 8 - inpgap;
                bitoff += inpgap;
                bitrem = 0;     /* bitoff%8 */
                bits -= inpgap;
                inpgap = 0;     /* bits%8 */
                inp++;
                if (bitoff == WHIRLPOOL_BBLOCK) {
                    whirlpool_block(c, c->data, 1);
                    bitoff = 0;
                }
                c->bitoff = bitoff;
                goto reconsider;
            } else
#endif
            if (bits > 8) {
                b = ((inp[0] << inpgap) | (inp[1] >> (8 - inpgap)));
                b &= 0xff;
                if (bitrem)
                    c->data[byteoff++] |= b >> bitrem;
                else
                    c->data[byteoff++] = b;
                bitoff += 8;
                bits -= 8;
                inp++;
                if (bitoff >= WHIRLPOOL_BBLOCK) {
                    whirlpool_block(c, c->data, 1);
                    byteoff = 0;
                    bitoff %= WHIRLPOOL_BBLOCK;
                }
                if (bitrem)
                    c->data[byteoff] = b << (8 - bitrem);
            } else {            /* remaining less than or equal to 8 bits */

                b = (inp[0] << inpgap) & 0xff;
                if (bitrem)
                    c->data[byteoff++] |= b >> bitrem;
                else
                    c->data[byteoff++] = b;
                bitoff += (unsigned int)bits;
                if (bitoff == WHIRLPOOL_BBLOCK) {
                    whirlpool_block(c, c->data, 1);
                    byteoff = 0;
                    bitoff %= WHIRLPOOL_BBLOCK;
                }
                if (bitrem)
                    c->data[byteoff] = b << (8 - bitrem);
                bits = 0;
            }
            c->bitoff = bitoff;
        }
    }
}

int WHIRLPOOL_Final(unsigned char *md, WHIRLPOOL_CTX *c)
{
    unsigned int bitoff = c->bitoff, byteoff = bitoff / 8;
    size_t i, j, v;
    unsigned char *p;

    bitoff %= 8;
    if (bitoff)
        c->data[byteoff] |= 0x80 >> bitoff;
    else
        c->data[byteoff] = 0x80;
    byteoff++;

    /* pad with zeros */
    if (byteoff > (WHIRLPOOL_BBLOCK / 8 - WHIRLPOOL_COUNTER)) {
        if (byteoff < WHIRLPOOL_BBLOCK / 8)
            memset(&c->data[byteoff], 0, WHIRLPOOL_BBLOCK / 8 - byteoff);
        whirlpool_block(c, c->data, 1);
        byteoff = 0;
    }
    if (byteoff < (WHIRLPOOL_BBLOCK / 8 - WHIRLPOOL_COUNTER))
        memset(&c->data[byteoff], 0,
               (WHIRLPOOL_BBLOCK / 8 - WHIRLPOOL_COUNTER) - byteoff);
    /* smash 256-bit c->bitlen in big-endian order */
    p = &c->data[WHIRLPOOL_BBLOCK / 8 - 1]; /* last byte in c->data */
    for (i = 0; i < WHIRLPOOL_COUNTER / sizeof(size_t); i++)
        for (v = c->bitlen[i], j = 0; j < sizeof(size_t); j++, v >>= 8)
            *p-- = (unsigned char)(v & 0xff);

    whirlpool_block(c, c->data, 1);

    if (md) {
        memcpy(md, c->H.c, WHIRLPOOL_DIGEST_LENGTH);
        OPENSSL_cleanse(c, sizeof(*c));
        return 1;
    }
    return 0;
}

unsigned char *WHIRLPOOL(const void *inp, size_t bytes, unsigned char *md)
{
    WHIRLPOOL_CTX ctx;
    static unsigned char m[WHIRLPOOL_DIGEST_LENGTH];

    if (md == NULL)
        md = m;
    WHIRLPOOL_Init(&ctx);
    WHIRLPOOL_Update(&ctx, inp, bytes);
    WHIRLPOOL_Final(md, &ctx);
    return md;
}
