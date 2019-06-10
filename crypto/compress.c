// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Compression operations.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 */
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "internal.h"

static int crypto_compress(struct crypto_tfm *tfm,
                            const u8 *src, unsigned int slen,
                            u8 *dst, unsigned int *dlen)
{
	return tfm->__crt_alg->cra_compress.coa_compress(tfm, src, slen, dst,
	                                                 dlen);
}

static int crypto_decompress(struct crypto_tfm *tfm,
                             const u8 *src, unsigned int slen,
                             u8 *dst, unsigned int *dlen)
{
	return tfm->__crt_alg->cra_compress.coa_decompress(tfm, src, slen, dst,
	                                                   dlen);
}

int crypto_init_compress_ops(struct crypto_tfm *tfm)
{
	struct compress_tfm *ops = &tfm->crt_compress;

	ops->cot_compress = crypto_compress;
	ops->cot_decompress = crypto_decompress;

	return 0;
}
