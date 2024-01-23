// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-512 and SHA-384 using the RISC-V vector crypto extensions
 *
 * Copyright (C) 2023 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/sha512_base.h>
#include <linux/linkage.h>
#include <linux/module.h>

/*
 * Note: the asm function only uses the 'state' field of struct sha512_state.
 * It is assumed to be the first field.
 */
asmlinkage void sha512_transform_zvknhb_zvkb(
	struct sha512_state *state, const u8 *data, int num_blocks);

static int riscv64_sha512_update(struct shash_desc *desc, const u8 *data,
				 unsigned int len)
{
	/*
	 * Ensure struct sha512_state begins directly with the SHA-512
	 * 512-bit internal state, as this is what the asm function expects.
	 */
	BUILD_BUG_ON(offsetof(struct sha512_state, state) != 0);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		sha512_base_do_update(desc, data, len,
				      sha512_transform_zvknhb_zvkb);
		kernel_vector_end();
	} else {
		crypto_sha512_update(desc, data, len);
	}
	return 0;
}

static int riscv64_sha512_finup(struct shash_desc *desc, const u8 *data,
				unsigned int len, u8 *out)
{
	if (crypto_simd_usable()) {
		kernel_vector_begin();
		if (len)
			sha512_base_do_update(desc, data, len,
					      sha512_transform_zvknhb_zvkb);
		sha512_base_do_finalize(desc, sha512_transform_zvknhb_zvkb);
		kernel_vector_end();

		return sha512_base_finish(desc, out);
	}

	return crypto_sha512_finup(desc, data, len, out);
}

static int riscv64_sha512_final(struct shash_desc *desc, u8 *out)
{
	return riscv64_sha512_finup(desc, NULL, 0, out);
}

static int riscv64_sha512_digest(struct shash_desc *desc, const u8 *data,
				 unsigned int len, u8 *out)
{
	return sha512_base_init(desc) ?:
	       riscv64_sha512_finup(desc, data, len, out);
}

static struct shash_alg riscv64_sha512_algs[] = {
	{
		.init = sha512_base_init,
		.update = riscv64_sha512_update,
		.final = riscv64_sha512_final,
		.finup = riscv64_sha512_finup,
		.digest = riscv64_sha512_digest,
		.descsize = sizeof(struct sha512_state),
		.digestsize = SHA512_DIGEST_SIZE,
		.base = {
			.cra_blocksize = SHA512_BLOCK_SIZE,
			.cra_priority = 300,
			.cra_name = "sha512",
			.cra_driver_name = "sha512-riscv64-zvknhb-zvkb",
			.cra_module = THIS_MODULE,
		},
	}, {
		.init = sha384_base_init,
		.update = riscv64_sha512_update,
		.final = riscv64_sha512_final,
		.finup = riscv64_sha512_finup,
		.descsize = sizeof(struct sha512_state),
		.digestsize = SHA384_DIGEST_SIZE,
		.base = {
			.cra_blocksize = SHA384_BLOCK_SIZE,
			.cra_priority = 300,
			.cra_name = "sha384",
			.cra_driver_name = "sha384-riscv64-zvknhb-zvkb",
			.cra_module = THIS_MODULE,
		},
	},
};

static int __init riscv64_sha512_mod_init(void)
{
	if (riscv_isa_extension_available(NULL, ZVKNHB) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		return crypto_register_shashes(riscv64_sha512_algs,
					       ARRAY_SIZE(riscv64_sha512_algs));

	return -ENODEV;
}

static void __exit riscv64_sha512_mod_exit(void)
{
	crypto_unregister_shashes(riscv64_sha512_algs,
				  ARRAY_SIZE(riscv64_sha512_algs));
}

module_init(riscv64_sha512_mod_init);
module_exit(riscv64_sha512_mod_exit);

MODULE_DESCRIPTION("SHA-512 (RISC-V accelerated)");
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@vrull.eu>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("sha512");
MODULE_ALIAS_CRYPTO("sha384");
