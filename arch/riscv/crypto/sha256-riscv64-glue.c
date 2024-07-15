// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256 and SHA-224 using the RISC-V vector crypto extensions
 *
 * Copyright (C) 2022 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha256_base.h>
#include <linux/linkage.h>
#include <linux/module.h>

/*
 * Note: the asm function only uses the 'state' field of struct sha256_state.
 * It is assumed to be the first field.
 */
asmlinkage void sha256_transform_zvknha_or_zvknhb_zvkb(
	struct sha256_state *state, const u8 *data, int num_blocks);

static int riscv64_sha256_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	/*
	 * Ensure struct sha256_state begins directly with the SHA-256
	 * 256-bit internal state, as this is what the asm function expects.
	 */
	BUILD_BUG_ON(offsetof(struct sha256_state, state) != 0);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		sha256_base_do_update(desc, data, len,
				      sha256_transform_zvknha_or_zvknhb_zvkb);
		kernel_vector_end();
	} else {
		crypto_sha256_update(desc, data, len);
	}
	return 0;
}

static int riscv64_sha256_finup(struct shash_desc *desc, const u8 *data,
				unsigned int len, u8 *out)
{
	if (crypto_simd_usable()) {
		kernel_vector_begin();
		if (len)
			sha256_base_do_update(
				desc, data, len,
				sha256_transform_zvknha_or_zvknhb_zvkb);
		sha256_base_do_finalize(
			desc, sha256_transform_zvknha_or_zvknhb_zvkb);
		kernel_vector_end();

		return sha256_base_finish(desc, out);
	}

	return crypto_sha256_finup(desc, data, len, out);
}

static int riscv64_sha256_final(struct shash_desc *desc, u8 *out)
{
	return riscv64_sha256_finup(desc, NULL, 0, out);
}

static int riscv64_sha256_digest(struct shash_desc *desc, const u8 *data,
				 unsigned int len, u8 *out)
{
	return sha256_base_init(desc) ?:
	       riscv64_sha256_finup(desc, data, len, out);
}

static struct shash_alg riscv64_sha256_algs[] = {
	{
		.init = sha256_base_init,
		.update = riscv64_sha256_update,
		.final = riscv64_sha256_final,
		.finup = riscv64_sha256_finup,
		.digest = riscv64_sha256_digest,
		.descsize = sizeof(struct sha256_state),
		.digestsize = SHA256_DIGEST_SIZE,
		.base = {
			.cra_blocksize = SHA256_BLOCK_SIZE,
			.cra_priority = 300,
			.cra_name = "sha256",
			.cra_driver_name = "sha256-riscv64-zvknha_or_zvknhb-zvkb",
			.cra_module = THIS_MODULE,
		},
	}, {
		.init = sha224_base_init,
		.update = riscv64_sha256_update,
		.final = riscv64_sha256_final,
		.finup = riscv64_sha256_finup,
		.descsize = sizeof(struct sha256_state),
		.digestsize = SHA224_DIGEST_SIZE,
		.base = {
			.cra_blocksize = SHA224_BLOCK_SIZE,
			.cra_priority = 300,
			.cra_name = "sha224",
			.cra_driver_name = "sha224-riscv64-zvknha_or_zvknhb-zvkb",
			.cra_module = THIS_MODULE,
		},
	},
};

static int __init riscv64_sha256_mod_init(void)
{
	/* Both zvknha and zvknhb provide the SHA-256 instructions. */
	if ((riscv_isa_extension_available(NULL, ZVKNHA) ||
	     riscv_isa_extension_available(NULL, ZVKNHB)) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		return crypto_register_shashes(riscv64_sha256_algs,
					       ARRAY_SIZE(riscv64_sha256_algs));

	return -ENODEV;
}

static void __exit riscv64_sha256_mod_exit(void)
{
	crypto_unregister_shashes(riscv64_sha256_algs,
				  ARRAY_SIZE(riscv64_sha256_algs));
}

module_init(riscv64_sha256_mod_init);
module_exit(riscv64_sha256_mod_exit);

MODULE_DESCRIPTION("SHA-256 (RISC-V accelerated)");
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@vrull.eu>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("sha256");
MODULE_ALIAS_CRYPTO("sha224");
