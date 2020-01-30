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
#include <linux/module.h>
#include <asm/cpacf.h>
#include "sha.h"

int s390_sha_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);
	unsigned int bsize = crypto_shash_blocksize(desc->tfm);
	unsigned int index, n;

	/* how much is already in the buffer? */
	index = ctx->count % bsize;
	ctx->count += len;

	if ((index + len) < bsize)
		goto store;

	/* process one stored block */
	if (index) {
		memcpy(ctx->buf + index, data, bsize - index);
		cpacf_kimd(ctx->func, ctx->state, ctx->buf, bsize);
		data += bsize - index;
		len -= bsize - index;
		index = 0;
	}

	/* process as many blocks as possible */
	if (len >= bsize) {
		n = (len / bsize) * bsize;
		cpacf_kimd(ctx->func, ctx->state, data, n);
		data += n;
		len -= n;
	}
store:
	if (len)
		memcpy(ctx->buf + index , data, len);

	return 0;
}
EXPORT_SYMBOL_GPL(s390_sha_update);

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

int s390_sha_final(struct shash_desc *desc, u8 *out)
{
	struct s390_sha_ctx *ctx = shash_desc_ctx(desc);
	unsigned int bsize = crypto_shash_blocksize(desc->tfm);
	u64 bits;
	unsigned int n, mbl_offset;

	n = ctx->count % bsize;
	bits = ctx->count * 8;
	mbl_offset = s390_crypto_shash_parmsize(ctx->func) / sizeof(u32);
	if (mbl_offset < 0)
		return -EINVAL;

	/* set total msg bit length (mbl) in CPACF parmblock */
	switch (ctx->func) {
	case CPACF_KLMD_SHA_1:
	case CPACF_KLMD_SHA_256:
		memcpy(ctx->state + mbl_offset, &bits, sizeof(bits));
		break;
	case CPACF_KLMD_SHA_512:
		/*
		 * the SHA512 parmblock has a 128-bit mbl field, clear
		 * high-order u64 field, copy bits to low-order u64 field
		 */
		memset(ctx->state + mbl_offset, 0x00, sizeof(bits));
		mbl_offset += sizeof(u64) / sizeof(u32);
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

	cpacf_klmd(ctx->func, ctx->state, ctx->buf, n);

	/* copy digest to out */
	memcpy(out, ctx->state, crypto_shash_digestsize(desc->tfm));
	/* wipe context */
	memset(ctx, 0, sizeof *ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(s390_sha_final);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("s390 SHA cipher common functions");
