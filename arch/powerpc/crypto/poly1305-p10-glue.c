// SPDX-License-Identifier: GPL-2.0
/*
 * Poly1305 authenticator algorithm, RFC7539.
 *
 * Copyright 2023- IBM Corp. All rights reserved.
 */

#include <crypto/algapi.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jump_label.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <crypto/internal/simd.h>
#include <linux/cpufeature.h>
#include <asm/unaligned.h>
#include <asm/simd.h>
#include <asm/switch_to.h>

asmlinkage void poly1305_p10le_4blocks(void *h, const u8 *m, u32 mlen);
asmlinkage void poly1305_64s(void *h, const u8 *m, u32 mlen, int highbit);
asmlinkage void poly1305_emit_64(void *h, void *s, u8 *dst);

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	preempt_enable();
}

static int crypto_poly1305_p10_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	poly1305_core_init(&dctx->h);
	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}

static unsigned int crypto_poly1305_setdctxkey(struct poly1305_desc_ctx *dctx,
					       const u8 *inp, unsigned int len)
{
	unsigned int acc = 0;

	if (unlikely(!dctx->sset)) {
		if (!dctx->rset && len >= POLY1305_BLOCK_SIZE) {
			struct poly1305_core_key *key = &dctx->core_r;

			key->key.r64[0] = get_unaligned_le64(&inp[0]);
			key->key.r64[1] = get_unaligned_le64(&inp[8]);
			inp += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			acc += POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (len >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(&inp[0]);
			dctx->s[1] = get_unaligned_le32(&inp[4]);
			dctx->s[2] = get_unaligned_le32(&inp[8]);
			dctx->s[3] = get_unaligned_le32(&inp[12]);
			acc += POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
	}
	return acc;
}

static int crypto_poly1305_p10_update(struct shash_desc *desc,
				      const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	unsigned int bytes, used;

	if (unlikely(dctx->buflen)) {
		bytes = min(srclen, POLY1305_BLOCK_SIZE - dctx->buflen);
		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		srclen -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			if (likely(!crypto_poly1305_setdctxkey(dctx, dctx->buf,
							       POLY1305_BLOCK_SIZE))) {
				vsx_begin();
				poly1305_64s(&dctx->h, dctx->buf,
						  POLY1305_BLOCK_SIZE, 1);
				vsx_end();
			}
			dctx->buflen = 0;
		}
	}

	if (likely(srclen >= POLY1305_BLOCK_SIZE)) {
		bytes = round_down(srclen, POLY1305_BLOCK_SIZE);
		used = crypto_poly1305_setdctxkey(dctx, src, bytes);
		if (likely(used)) {
			srclen -= used;
			src += used;
		}
		if (crypto_simd_usable() && (srclen >= POLY1305_BLOCK_SIZE*4)) {
			vsx_begin();
			poly1305_p10le_4blocks(&dctx->h, src, srclen);
			vsx_end();
			src += srclen - (srclen % (POLY1305_BLOCK_SIZE * 4));
			srclen %= POLY1305_BLOCK_SIZE * 4;
		}
		while (srclen >= POLY1305_BLOCK_SIZE) {
			vsx_begin();
			poly1305_64s(&dctx->h, src, POLY1305_BLOCK_SIZE, 1);
			vsx_end();
			srclen -= POLY1305_BLOCK_SIZE;
			src += POLY1305_BLOCK_SIZE;
		}
	}

	if (unlikely(srclen)) {
		dctx->buflen = srclen;
		memcpy(dctx->buf, src, srclen);
	}

	return 0;
}

static int crypto_poly1305_p10_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	if ((dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		vsx_begin();
		poly1305_64s(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
		vsx_end();
		dctx->buflen = 0;
	}

	poly1305_emit_64(&dctx->h, &dctx->s, dst);
	return 0;
}

static struct shash_alg poly1305_alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_poly1305_p10_init,
	.update		= crypto_poly1305_p10_update,
	.final		= crypto_poly1305_p10_final,
	.descsize	= sizeof(struct poly1305_desc_ctx),
	.base		= {
		.cra_name		= "poly1305",
		.cra_driver_name	= "poly1305-p10",
		.cra_priority		= 300,
		.cra_blocksize		= POLY1305_BLOCK_SIZE,
		.cra_module		= THIS_MODULE,
	},
};

static int __init poly1305_p10_init(void)
{
	return crypto_register_shash(&poly1305_alg);
}

static void __exit poly1305_p10_exit(void)
{
	crypto_unregister_shash(&poly1305_alg);
}

module_cpu_feature_match(PPC_MODULE_FEATURE_P10, poly1305_p10_init);
module_exit(poly1305_p10_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com>");
MODULE_DESCRIPTION("Optimized Poly1305 for P10");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-p10");
