// SPDX-License-Identifier: GPL-2.0
/*
 * ChaCha stream cipher (s390 optimized)
 *
 * Copyright IBM Corp. 2021
 */

#define KMSG_COMPONENT "chacha_s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <crypto/chacha.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <asm/fpu.h>
#include "chacha-s390.h"

void hchacha_block_arch(const struct chacha_state *state,
			u32 out[HCHACHA_OUT_WORDS], int nrounds)
{
	/* TODO: implement hchacha_block_arch() in assembly */
	hchacha_block_generic(state, out, nrounds);
}
EXPORT_SYMBOL(hchacha_block_arch);

void chacha_crypt_arch(struct chacha_state *state, u8 *dst, const u8 *src,
		       unsigned int bytes, int nrounds)
{
	/* s390 chacha20 implementation has 20 rounds hard-coded,
	 * it cannot handle a block of data or less, but otherwise
	 * it can handle data of arbitrary size
	 */
	if (bytes <= CHACHA_BLOCK_SIZE || nrounds != 20 || !cpu_has_vx()) {
		chacha_crypt_generic(state, dst, src, bytes, nrounds);
	} else {
		DECLARE_KERNEL_FPU_ONSTACK32(vxstate);

		kernel_fpu_begin(&vxstate, KERNEL_VXR);
		chacha20_vx(dst, src, bytes, &state->x[4], &state->x[12]);
		kernel_fpu_end(&vxstate, KERNEL_VXR);

		state->x[12] += round_up(bytes, CHACHA_BLOCK_SIZE) /
				CHACHA_BLOCK_SIZE;
	}
}
EXPORT_SYMBOL(chacha_crypt_arch);

bool chacha_is_arch_optimized(void)
{
	return cpu_has_vx();
}
EXPORT_SYMBOL(chacha_is_arch_optimized);

MODULE_DESCRIPTION("ChaCha stream cipher (s390 optimized)");
MODULE_LICENSE("GPL v2");
