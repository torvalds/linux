// SPDX-License-Identifier: GPL-2.0-only
/*
 * SM4 using the RISC-V vector crypto extensions
 *
 * Copyright (C) 2023 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/internal/cipher.h>
#include <crypto/internal/simd.h>
#include <crypto/sm4.h>
#include <linux/linkage.h>
#include <linux/module.h>

asmlinkage void sm4_expandkey_zvksed_zvkb(const u8 user_key[SM4_KEY_SIZE],
					  u32 rkey_enc[SM4_RKEY_WORDS],
					  u32 rkey_dec[SM4_RKEY_WORDS]);
asmlinkage void sm4_crypt_zvksed_zvkb(const u32 rkey[SM4_RKEY_WORDS],
				      const u8 in[SM4_BLOCK_SIZE],
				      u8 out[SM4_BLOCK_SIZE]);

static int riscv64_sm4_setkey(struct crypto_tfm *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct sm4_ctx *ctx = crypto_tfm_ctx(tfm);

	if (crypto_simd_usable()) {
		if (keylen != SM4_KEY_SIZE)
			return -EINVAL;
		kernel_vector_begin();
		sm4_expandkey_zvksed_zvkb(key, ctx->rkey_enc, ctx->rkey_dec);
		kernel_vector_end();
		return 0;
	}
	return sm4_expandkey(ctx, key, keylen);
}

static void riscv64_sm4_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct sm4_ctx *ctx = crypto_tfm_ctx(tfm);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		sm4_crypt_zvksed_zvkb(ctx->rkey_enc, src, dst);
		kernel_vector_end();
	} else {
		sm4_crypt_block(ctx->rkey_enc, dst, src);
	}
}

static void riscv64_sm4_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	const struct sm4_ctx *ctx = crypto_tfm_ctx(tfm);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		sm4_crypt_zvksed_zvkb(ctx->rkey_dec, src, dst);
		kernel_vector_end();
	} else {
		sm4_crypt_block(ctx->rkey_dec, dst, src);
	}
}

static struct crypto_alg riscv64_sm4_alg = {
	.cra_flags = CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize = SM4_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct sm4_ctx),
	.cra_priority = 300,
	.cra_name = "sm4",
	.cra_driver_name = "sm4-riscv64-zvksed-zvkb",
	.cra_cipher = {
		.cia_min_keysize = SM4_KEY_SIZE,
		.cia_max_keysize = SM4_KEY_SIZE,
		.cia_setkey = riscv64_sm4_setkey,
		.cia_encrypt = riscv64_sm4_encrypt,
		.cia_decrypt = riscv64_sm4_decrypt,
	},
	.cra_module = THIS_MODULE,
};

static int __init riscv64_sm4_mod_init(void)
{
	if (riscv_isa_extension_available(NULL, ZVKSED) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		return crypto_register_alg(&riscv64_sm4_alg);

	return -ENODEV;
}

static void __exit riscv64_sm4_mod_exit(void)
{
	crypto_unregister_alg(&riscv64_sm4_alg);
}

module_init(riscv64_sm4_mod_init);
module_exit(riscv64_sm4_mod_exit);

MODULE_DESCRIPTION("SM4 (RISC-V accelerated)");
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@vrull.eu>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("sm4");
