// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SM3 using the RISC-V vector crypto extensions
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
#include <crypto/sm3.h>
#include <crypto/sm3_base.h>
#include <linux/kernel.h>
#include <linux/module.h>

/*
 * Note: the asm function only uses the 'state' field of struct sm3_state.
 * It is assumed to be the first field.
 */
asmlinkage void sm3_transform_zvksh_zvkb(
	struct sm3_state *state, const u8 *data, int num_blocks);

static void sm3_block(struct sm3_state *state, const u8 *data,
		      int num_blocks)
{
	/*
	 * Ensure struct sm3_state begins directly with the SM3
	 * 256-bit internal state, as this is what the asm function expects.
	 */
	BUILD_BUG_ON(offsetof(struct sm3_state, state) != 0);

	if (crypto_simd_usable()) {
		kernel_vector_begin();
		sm3_transform_zvksh_zvkb(state, data, num_blocks);
		kernel_vector_end();
	} else {
		sm3_block_generic(state, data, num_blocks);
	}
}

static int riscv64_sm3_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len)
{
	return sm3_base_do_update_blocks(desc, data, len, sm3_block);
}

static int riscv64_sm3_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *out)
{
	sm3_base_do_finup(desc, data, len, sm3_block);
	return sm3_base_finish(desc, out);
}

static struct shash_alg riscv64_sm3_alg = {
	.init = sm3_base_init,
	.update = riscv64_sm3_update,
	.finup = riscv64_sm3_finup,
	.descsize = SM3_STATE_SIZE,
	.digestsize = SM3_DIGEST_SIZE,
	.base = {
		.cra_blocksize = SM3_BLOCK_SIZE,
		.cra_flags = CRYPTO_AHASH_ALG_BLOCK_ONLY |
			     CRYPTO_AHASH_ALG_FINUP_MAX,
		.cra_priority = 300,
		.cra_name = "sm3",
		.cra_driver_name = "sm3-riscv64-zvksh-zvkb",
		.cra_module = THIS_MODULE,
	},
};

static int __init riscv64_sm3_mod_init(void)
{
	if (riscv_isa_extension_available(NULL, ZVKSH) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		return crypto_register_shash(&riscv64_sm3_alg);

	return -ENODEV;
}

static void __exit riscv64_sm3_mod_exit(void)
{
	crypto_unregister_shash(&riscv64_sm3_alg);
}

module_init(riscv64_sm3_mod_init);
module_exit(riscv64_sm3_mod_exit);

MODULE_DESCRIPTION("SM3 (RISC-V accelerated)");
MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@vrull.eu>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("sm3");
