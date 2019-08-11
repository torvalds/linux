// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 */

#ifdef CONFIG_ARM64
#include <asm/neon-intrinsics.h>

#define AES_ROUND	"aese %0.16b, %1.16b \n\t aesmc %0.16b, %0.16b"
#else
#include <arm_neon.h>

#define AES_ROUND	"aese.8 %q0, %q1 \n\t aesmc.8 %q0, %q0"
#endif

#define AEGIS_BLOCK_SIZE	16

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

struct aegis128_state {
	uint8x16_t v[5];
};

static struct aegis128_state aegis128_load_state_neon(const void *state)
{
	return (struct aegis128_state){ {
		vld1q_u8(state),
		vld1q_u8(state + 16),
		vld1q_u8(state + 32),
		vld1q_u8(state + 48),
		vld1q_u8(state + 64)
	} };
}

static void aegis128_save_state_neon(struct aegis128_state st, void *state)
{
	vst1q_u8(state, st.v[0]);
	vst1q_u8(state + 16, st.v[1]);
	vst1q_u8(state + 32, st.v[2]);
	vst1q_u8(state + 48, st.v[3]);
	vst1q_u8(state + 64, st.v[4]);
}

static inline __attribute__((always_inline))
uint8x16_t aegis_aes_round(uint8x16_t w)
{
	uint8x16_t z = {};

	/*
	 * We use inline asm here instead of the vaeseq_u8/vaesmcq_u8 intrinsics
	 * to force the compiler to issue the aese/aesmc instructions in pairs.
	 * This is much faster on many cores, where the instruction pair can
	 * execute in a single cycle.
	 */
	asm(AES_ROUND : "+w"(w) : "w"(z));
	return w;
}

static inline __attribute__((always_inline))
struct aegis128_state aegis128_update_neon(struct aegis128_state st,
					   uint8x16_t m)
{
	m       ^= aegis_aes_round(st.v[4]);
	st.v[4] ^= aegis_aes_round(st.v[3]);
	st.v[3] ^= aegis_aes_round(st.v[2]);
	st.v[2] ^= aegis_aes_round(st.v[1]);
	st.v[1] ^= aegis_aes_round(st.v[0]);
	st.v[0] ^= m;

	return st;
}

void crypto_aegis128_update_neon(void *state, const void *msg)
{
	struct aegis128_state st = aegis128_load_state_neon(state);

	st = aegis128_update_neon(st, vld1q_u8(msg));

	aegis128_save_state_neon(st, state);
}

void crypto_aegis128_encrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size)
{
	struct aegis128_state st = aegis128_load_state_neon(state);
	uint8x16_t msg;

	while (size >= AEGIS_BLOCK_SIZE) {
		uint8x16_t s = st.v[1] ^ (st.v[2] & st.v[3]) ^ st.v[4];

		msg = vld1q_u8(src);
		st = aegis128_update_neon(st, msg);
		vst1q_u8(dst, msg ^ s);

		size -= AEGIS_BLOCK_SIZE;
		src += AEGIS_BLOCK_SIZE;
		dst += AEGIS_BLOCK_SIZE;
	}

	if (size > 0) {
		uint8x16_t s = st.v[1] ^ (st.v[2] & st.v[3]) ^ st.v[4];
		uint8_t buf[AEGIS_BLOCK_SIZE] = {};

		memcpy(buf, src, size);
		msg = vld1q_u8(buf);
		st = aegis128_update_neon(st, msg);
		vst1q_u8(buf, msg ^ s);
		memcpy(dst, buf, size);
	}

	aegis128_save_state_neon(st, state);
}

void crypto_aegis128_decrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size)
{
	struct aegis128_state st = aegis128_load_state_neon(state);
	uint8x16_t msg;

	while (size >= AEGIS_BLOCK_SIZE) {
		msg = vld1q_u8(src) ^ st.v[1] ^ (st.v[2] & st.v[3]) ^ st.v[4];
		st = aegis128_update_neon(st, msg);
		vst1q_u8(dst, msg);

		size -= AEGIS_BLOCK_SIZE;
		src += AEGIS_BLOCK_SIZE;
		dst += AEGIS_BLOCK_SIZE;
	}

	if (size > 0) {
		uint8x16_t s = st.v[1] ^ (st.v[2] & st.v[3]) ^ st.v[4];
		uint8_t buf[AEGIS_BLOCK_SIZE];

		vst1q_u8(buf, s);
		memcpy(buf, src, size);
		msg = vld1q_u8(buf) ^ s;
		vst1q_u8(buf, msg);
		memcpy(dst, buf, size);

		st = aegis128_update_neon(st, msg);
	}

	aegis128_save_state_neon(st, state);
}
