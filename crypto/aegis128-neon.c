// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <asm/cpufeature.h>
#include <asm/neon.h>

#include "aegis.h"

void crypto_aegis128_update_neon(void *state, const void *msg);
void crypto_aegis128_encrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size);
void crypto_aegis128_decrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size);

int aegis128_have_aes_insn __ro_after_init;

bool crypto_aegis128_have_simd(void)
{
	if (cpu_have_feature(cpu_feature(AES))) {
		aegis128_have_aes_insn = 1;
		return true;
	}
	return IS_ENABLED(CONFIG_ARM64);
}

void crypto_aegis128_update_simd(union aegis_block *state, const void *msg)
{
	kernel_neon_begin();
	crypto_aegis128_update_neon(state, msg);
	kernel_neon_end();
}

void crypto_aegis128_encrypt_chunk_simd(union aegis_block *state, u8 *dst,
					const u8 *src, unsigned int size)
{
	kernel_neon_begin();
	crypto_aegis128_encrypt_chunk_neon(state, dst, src, size);
	kernel_neon_end();
}

void crypto_aegis128_decrypt_chunk_simd(union aegis_block *state, u8 *dst,
					const u8 *src, unsigned int size)
{
	kernel_neon_begin();
	crypto_aegis128_decrypt_chunk_neon(state, dst, src, size);
	kernel_neon_end();
}
