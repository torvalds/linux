// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/cpufeature.h>
#include <asm/neon.h>

#include "aegis.h"
#include "aegis-neon.h"

int aegis128_have_aes_insn __ro_after_init;

bool crypto_aegis128_have_simd(void)
{
	if (cpu_have_feature(cpu_feature(AES))) {
		aegis128_have_aes_insn = 1;
		return true;
	}
	return IS_ENABLED(CONFIG_ARM64);
}

void crypto_aegis128_init_simd(struct aegis_state *state,
			       const union aegis_block *key,
			       const u8 *iv)
{
	kernel_neon_begin();
	crypto_aegis128_init_neon(state, key, iv);
	kernel_neon_end();
}

void crypto_aegis128_update_simd(struct aegis_state *state, const void *msg)
{
	kernel_neon_begin();
	crypto_aegis128_update_neon(state, msg);
	kernel_neon_end();
}

void crypto_aegis128_encrypt_chunk_simd(struct aegis_state *state, u8 *dst,
					const u8 *src, unsigned int size)
{
	kernel_neon_begin();
	crypto_aegis128_encrypt_chunk_neon(state, dst, src, size);
	kernel_neon_end();
}

void crypto_aegis128_decrypt_chunk_simd(struct aegis_state *state, u8 *dst,
					const u8 *src, unsigned int size)
{
	kernel_neon_begin();
	crypto_aegis128_decrypt_chunk_neon(state, dst, src, size);
	kernel_neon_end();
}

int crypto_aegis128_final_simd(struct aegis_state *state,
			       union aegis_block *tag_xor,
			       unsigned int assoclen,
			       unsigned int cryptlen,
			       unsigned int authsize)
{
	int ret;

	kernel_neon_begin();
	ret = crypto_aegis128_final_neon(state, tag_xor, assoclen, cryptlen,
					 authsize);
	kernel_neon_end();

	return ret;
}
