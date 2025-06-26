// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 generic implementation of the SHA Secure Hash Algorithms.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 */

#include <crypto/internal/hash.h>
#include <linux/export.h>
#include <linux/module.h>
#include <asm/cpacf.h>
#include "sha.h"

int s390_sha_update_blocks(struct shash_desc *desc, const u8 *data,
			   unsigned int len)
{
	unsigned int bsize = crypto_shash_blocksize(desc->tfm);
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);
	unsigned int n;
	int fc;

	fc = ctx->func;
	if (ctx->first_message_part)
		fc |= CPACF_KIMD_NIP;

	/* process as many blocks as possible */
	n = (len / bsize) * bsize;
	ctx->count += n;
	switch (ctx->func) {
	case CPACF_KLMD_SHA_512:
	case CPACF_KLMD_SHA3_384:
		if (ctx->count < n)
			ctx->sha512.count_hi++;
		break;
	}
	cpacf_kimd(fc, ctx->state, data, n);
	ctx->first_message_part = 0;
	return len - n;
}
EXPORT_SYMBOL_GPL(s390_sha_update_blocks);

static int s390_crypto_shash_parmsize(int func)
{
	switch (func) {
	case CPACF_KLMD_SHA_1:
		return 20;
	case CPACF_KLMD_SHA_256:
		return 32;
	case CPACF_KLMD_SHA_512:
		return 64;
	case CPACF_KLMD_SHA3_224:
	case CPACF_KLMD_SHA3_256:
	case CPACF_KLMD_SHA3_384:
	case CPACF_KLMD_SHA3_512:
		return 200;
	default:
		return -EINVAL;
	}
}

int s390_sha_finup(struct shash_desc *desc, const u8 *src, unsigned int len,
		   u8 *out)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);
	int mbl_offset, fc;
	u64 bits;

	ctx->count += len;

	bits = ctx->count * 8;
	mbl_offset = s390_crypto_shash_parmsize(ctx->func);
	if (mbl_offset < 0)
		return -EINVAL;

	mbl_offset = mbl_offset / sizeof(u32);

	/* set total msg bit length (mbl) in CPACF parmblock */
	switch (ctx->func) {
	case CPACF_KLMD_SHA_512:
		/* The SHA512 parmblock has a 128-bit mbl field. */
		if (ctx->count < len)
			ctx->sha512.count_hi++;
		ctx->sha512.count_hi <<= 3;
		ctx->sha512.count_hi |= ctx->count >> 61;
		mbl_offset += sizeof(u64) / sizeof(u32);
		fallthrough;
	case CPACF_KLMD_SHA_1:
	case CPACF_KLMD_SHA_256:
		memcpy(ctx->state + mbl_offset, &bits, sizeof(bits));
		break;
	case CPACF_KLMD_SHA3_224:
	case CPACF_KLMD_SHA3_256:
	case CPACF_KLMD_SHA3_384:
	case CPACF_KLMD_SHA3_512:
		break;
	default:
		return -EINVAL;
	}

	fc = ctx->func;
	fc |= test_facility(86) ? CPACF_KLMD_DUFOP : 0;
	if (ctx->first_message_part)
		fc |= CPACF_KLMD_NIP;
	cpacf_klmd(fc, ctx->state, src, len);

	/* copy digest to out */
	memcpy(out, ctx->state, crypto_shash_digestsize(desc->tfm));

	return 0;
}
EXPORT_SYMBOL_GPL(s390_sha_finup);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("s390 SHA cipher common functions");
