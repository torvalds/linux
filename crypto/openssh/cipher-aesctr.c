/* $OpenBSD: cipher-aesctr.c,v 1.2 2015/01/14 10:24:42 markus Exp $ */
/*
 * Copyright (c) 2003 Markus Friedl.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>
#include <string.h>

#ifndef WITH_OPENSSL

#include "cipher-aesctr.h"

/*
 * increment counter 'ctr',
 * the counter is of size 'len' bytes and stored in network-byte-order.
 * (LSB at ctr[len-1], MSB at ctr[0])
 */
static inline void
aesctr_inc(u8 *ctr, u32 len)
{
	ssize_t i;

#ifndef CONSTANT_TIME_INCREMENT
	for (i = len - 1; i >= 0; i--)
		if (++ctr[i])	/* continue on overflow */
			return;
#else
	u8 x, add = 1;

	for (i = len - 1; i >= 0; i--) {
		ctr[i] += add;
		/* constant time for: x = ctr[i] ? 1 : 0 */
		x = ctr[i];
		x = (x | (x >> 4)) & 0xf;
		x = (x | (x >> 2)) & 0x3;
		x = (x | (x >> 1)) & 0x1;
		add *= (x^1);
	}
#endif
}

void
aesctr_keysetup(aesctr_ctx *x,const u8 *k,u32 kbits,u32 ivbits)
{
	x->rounds = rijndaelKeySetupEnc(x->ek, k, kbits);
}

void
aesctr_ivsetup(aesctr_ctx *x,const u8 *iv)
{
	memcpy(x->ctr, iv, AES_BLOCK_SIZE);
}

void
aesctr_encrypt_bytes(aesctr_ctx *x,const u8 *m,u8 *c,u32 bytes)
{
	u32 n = 0;
	u8 buf[AES_BLOCK_SIZE];

	while ((bytes--) > 0) {
		if (n == 0) {
			rijndaelEncrypt(x->ek, x->rounds, x->ctr, buf);
			aesctr_inc(x->ctr, AES_BLOCK_SIZE);
		}
		*(c++) = *(m++) ^ buf[n];
		n = (n + 1) % AES_BLOCK_SIZE;
	}
}
#endif /* !WITH_OPENSSL */
