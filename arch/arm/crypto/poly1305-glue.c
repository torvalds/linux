// SPDX-License-Identifier: GPL-2.0
/*
 * OpenSSL/Cryptogams accelerated Poly1305 transform for ARM
 *
 * Copyright (C) 2019 Linaro Ltd. <ard.biesheuvel@linaro.org>
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <asm/unaligned.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/poly1305.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/jump_label.h>
#include <linux/module.h>

void poly1305_init_arm(void *state, const u8 *key);
void poly1305_blocks_arm(void *state, const u8 *src, u32 len, u32 hibit);
void poly1305_emit_arm(void *state, __le32 *digest, const u32 *nonce);

void __weak poly1305_blocks_neon(void *state, const u8 *src, u32 len, u32 hibit)
{
}

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

void poly1305_init_arch(struct poly1305_desc_ctx *dctx, const u8 *key)
{
	poly1305_init_arm(&dctx->h, key);
	dctx->s[0] = get_unaligned_le32(key + 16);
	dctx->s[1] = get_unaligned_le32(key + 20);
	dctx->s[2] = get_unaligned_le32(key + 24);
	dctx->s[3] = get_unaligned_le32(key + 28);
	dctx->buflen = 0;
}
EXPORT_SYMBOL(poly1305_init_arch);

static int arm_poly1305_init(struct shash_desc *desc)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	dctx->buflen = 0;
	dctx->rset = 0;
	dctx->sset = false;

	return 0;
}

static void arm_poly1305_blocks(struct poly1305_desc_ctx *dctx, const u8 *src,
				 u32 len, u32 hibit, bool do_neon)
{
	if (unlikely(!dctx->sset)) {
		if (!dctx->rset) {
			poly1305_init_arm(&dctx->h, src);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->rset = 1;
		}
		if (len >= POLY1305_BLOCK_SIZE) {
			dctx->s[0] = get_unaligned_le32(src +  0);
			dctx->s[1] = get_unaligned_le32(src +  4);
			dctx->s[2] = get_unaligned_le32(src +  8);
			dctx->s[3] = get_unaligned_le32(src + 12);
			src += POLY1305_BLOCK_SIZE;
			len -= POLY1305_BLOCK_SIZE;
			dctx->sset = true;
		}
		if (len < POLY1305_BLOCK_SIZE)
			return;
	}

	len &= ~(POLY1305_BLOCK_SIZE - 1);

	if (static_branch_likely(&have_neon) && likely(do_neon))
		poly1305_blocks_neon(&dctx->h, src, len, hibit);
	else
		poly1305_blocks_arm(&dctx->h, src, len, hibit);
}

static void arm_poly1305_do_update(struct poly1305_desc_ctx *dctx,
				    const u8 *src, u32 len, bool do_neon)
{
	if (unlikely(dctx->buflen)) {
		u32 bytes = min(len, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		len -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			arm_poly1305_blocks(dctx, dctx->buf,
					    POLY1305_BLOCK_SIZE, 1, false);
			dctx->buflen = 0;
		}
	}

	if (likely(len >= POLY1305_BLOCK_SIZE)) {
		arm_poly1305_blocks(dctx, src, len, 1, do_neon);
		src += round_down(len, POLY1305_BLOCK_SIZE);
		len %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(len)) {
		dctx->buflen = len;
		memcpy(dctx->buf, src, len);
	}
}

static int arm_poly1305_update(struct shash_desc *desc,
			       const u8 *src, unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	arm_poly1305_do_update(dctx, src, srclen, false);
	return 0;
}

static int __maybe_unused arm_poly1305_update_neon(struct shash_desc *desc,
						   const u8 *src,
						   unsigned int srclen)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);
	bool do_neon = may_use_simd() && srclen > 128;

	if (static_branch_likely(&have_neon) && do_neon)
		kernel_neon_begin();
	arm_poly1305_do_update(dctx, src, srclen, do_neon);
	if (static_branch_likely(&have_neon) && do_neon)
		kernel_neon_end();
	return 0;
}

void poly1305_update_arch(struct poly1305_desc_ctx *dctx, const u8 *src,
			  unsigned int nbytes)
{
	bool do_neon = IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
		       may_use_simd();

	if (unlikely(dctx->buflen)) {
		u32 bytes = min(nbytes, POLY1305_BLOCK_SIZE - dctx->buflen);

		memcpy(dctx->buf + dctx->buflen, src, bytes);
		src += bytes;
		nbytes -= bytes;
		dctx->buflen += bytes;

		if (dctx->buflen == POLY1305_BLOCK_SIZE) {
			poly1305_blocks_arm(&dctx->h, dctx->buf,
					    POLY1305_BLOCK_SIZE, 1);
			dctx->buflen = 0;
		}
	}

	if (likely(nbytes >= POLY1305_BLOCK_SIZE)) {
		unsigned int len = round_down(nbytes, POLY1305_BLOCK_SIZE);

		if (static_branch_likely(&have_neon) && do_neon) {
			kernel_neon_begin();
			poly1305_blocks_neon(&dctx->h, src, len, 1);
			kernel_neon_end();
		} else {
			poly1305_blocks_arm(&dctx->h, src, len, 1);
		}
		src += len;
		nbytes %= POLY1305_BLOCK_SIZE;
	}

	if (unlikely(nbytes)) {
		dctx->buflen = nbytes;
		memcpy(dctx->buf, src, nbytes);
	}
}
EXPORT_SYMBOL(poly1305_update_arch);

void poly1305_final_arch(struct poly1305_desc_ctx *dctx, u8 *dst)
{
	__le32 digest[4];
	u64 f = 0;

	if (unlikely(dctx->buflen)) {
		dctx->buf[dctx->buflen++] = 1;
		memset(dctx->buf + dctx->buflen, 0,
		       POLY1305_BLOCK_SIZE - dctx->buflen);
		poly1305_blocks_arm(&dctx->h, dctx->buf, POLY1305_BLOCK_SIZE, 0);
	}

	poly1305_emit_arm(&dctx->h, digest, dctx->s);

	/* mac = (h + s) % (2^128) */
	f = (f >> 32) + le32_to_cpu(digest[0]);
	put_unaligned_le32(f, dst);
	f = (f >> 32) + le32_to_cpu(digest[1]);
	put_unaligned_le32(f, dst + 4);
	f = (f >> 32) + le32_to_cpu(digest[2]);
	put_unaligned_le32(f, dst + 8);
	f = (f >> 32) + le32_to_cpu(digest[3]);
	put_unaligned_le32(f, dst + 12);

	*dctx = (struct poly1305_desc_ctx){};
}
EXPORT_SYMBOL(poly1305_final_arch);

static int arm_poly1305_final(struct shash_desc *desc, u8 *dst)
{
	struct poly1305_desc_ctx *dctx = shash_desc_ctx(desc);

	if (unlikely(!dctx->sset))
		return -ENOKEY;

	poly1305_final_arch(dctx, dst);
	return 0;
}

static struct shash_alg arm_poly1305_algs[] = {{
	.init			= arm_poly1305_init,
	.update			= arm_poly1305_update,
	.final			= arm_poly1305_final,
	.digestsize		= POLY1305_DIGEST_SIZE,
	.descsize		= sizeof(struct poly1305_desc_ctx),

	.base.cra_name		= "poly1305",
	.base.cra_driver_name	= "poly1305-arm",
	.base.cra_priority	= 150,
	.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
#ifdef CONFIG_KERNEL_MODE_NEON
}, {
	.init			= arm_poly1305_init,
	.update			= arm_poly1305_update_neon,
	.final			= arm_poly1305_final,
	.digestsize		= POLY1305_DIGEST_SIZE,
	.descsize		= sizeof(struct poly1305_desc_ctx),

	.base.cra_name		= "poly1305",
	.base.cra_driver_name	= "poly1305-neon",
	.base.cra_priority	= 200,
	.base.cra_blocksize	= POLY1305_BLOCK_SIZE,
	.base.cra_module	= THIS_MODULE,
#endif
}};

static int __init arm_poly1305_mod_init(void)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    (elf_hwcap & HWCAP_NEON))
		static_branch_enable(&have_neon);
	else
		/* register only the first entry */
		return crypto_register_shash(&arm_poly1305_algs[0]);

	return crypto_register_shashes(arm_poly1305_algs,
				       ARRAY_SIZE(arm_poly1305_algs));
}

static void __exit arm_poly1305_mod_exit(void)
{
	if (!static_branch_likely(&have_neon)) {
		crypto_unregister_shash(&arm_poly1305_algs[0]);
		return;
	}
	crypto_unregister_shashes(arm_poly1305_algs,
				  ARRAY_SIZE(arm_poly1305_algs));
}

module_init(arm_poly1305_mod_init);
module_exit(arm_poly1305_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("poly1305");
MODULE_ALIAS_CRYPTO("poly1305-arm");
MODULE_ALIAS_CRYPTO("poly1305-neon");
