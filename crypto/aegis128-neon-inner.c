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

extern int aegis128_have_aes_insn;

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

struct aegis128_state {
	uint8x16_t v[5];
};

extern const uint8_t crypto_aes_sbox[];

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

#ifdef CONFIG_ARM64
	if (!__builtin_expect(aegis128_have_aes_insn, 1)) {
		static const uint8_t shift_rows[] = {
			0x0, 0x5, 0xa, 0xf, 0x4, 0x9, 0xe, 0x3,
			0x8, 0xd, 0x2, 0x7, 0xc, 0x1, 0x6, 0xb,
		};
		static const uint8_t ror32by8[] = {
			0x1, 0x2, 0x3, 0x0, 0x5, 0x6, 0x7, 0x4,
			0x9, 0xa, 0xb, 0x8, 0xd, 0xe, 0xf, 0xc,
		};
		uint8x16_t v;

		// shift rows
		w = vqtbl1q_u8(w, vld1q_u8(shift_rows));

		// sub bytes
#ifndef CONFIG_CC_IS_GCC
		v = vqtbl4q_u8(vld1q_u8_x4(crypto_aes_sbox), w);
		v = vqtbx4q_u8(v, vld1q_u8_x4(crypto_aes_sbox + 0x40), w - 0x40);
		v = vqtbx4q_u8(v, vld1q_u8_x4(crypto_aes_sbox + 0x80), w - 0x80);
		v = vqtbx4q_u8(v, vld1q_u8_x4(crypto_aes_sbox + 0xc0), w - 0xc0);
#else
		asm("tbl %0.16b, {v16.16b-v19.16b}, %1.16b" : "=w"(v) : "w"(w));
		w -= 0x40;
		asm("tbx %0.16b, {v20.16b-v23.16b}, %1.16b" : "+w"(v) : "w"(w));
		w -= 0x40;
		asm("tbx %0.16b, {v24.16b-v27.16b}, %1.16b" : "+w"(v) : "w"(w));
		w -= 0x40;
		asm("tbx %0.16b, {v28.16b-v31.16b}, %1.16b" : "+w"(v) : "w"(w));
#endif

		// mix columns
		w = (v << 1) ^ (uint8x16_t)(((int8x16_t)v >> 7) & 0x1b);
		w ^= (uint8x16_t)vrev32q_u16((uint16x8_t)v);
		w ^= vqtbl1q_u8(v ^ w, vld1q_u8(ror32by8));

		return w;
	}
#endif

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

static inline __attribute__((always_inline))
void preload_sbox(void)
{
	if (!IS_ENABLED(CONFIG_ARM64) ||
	    !IS_ENABLED(CONFIG_CC_IS_GCC) ||
	    __builtin_expect(aegis128_have_aes_insn, 1))
		return;

	asm("ld1	{v16.16b-v19.16b}, [%0], #64	\n\t"
	    "ld1	{v20.16b-v23.16b}, [%0], #64	\n\t"
	    "ld1	{v24.16b-v27.16b}, [%0], #64	\n\t"
	    "ld1	{v28.16b-v31.16b}, [%0]		\n\t"
	    :: "r"(crypto_aes_sbox));
}

void crypto_aegis128_init_neon(void *state, const void *key, const void *iv)
{
	static const uint8_t const0[] = {
		0x00, 0x01, 0x01, 0x02, 0x03, 0x05, 0x08, 0x0d,
		0x15, 0x22, 0x37, 0x59, 0x90, 0xe9, 0x79, 0x62,
	};
	static const uint8_t const1[] = {
		0xdb, 0x3d, 0x18, 0x55, 0x6d, 0xc2, 0x2f, 0xf1,
		0x20, 0x11, 0x31, 0x42, 0x73, 0xb5, 0x28, 0xdd,
	};
	uint8x16_t k = vld1q_u8(key);
	uint8x16_t kiv = k ^ vld1q_u8(iv);
	struct aegis128_state st = {{
		kiv,
		vld1q_u8(const1),
		vld1q_u8(const0),
		k ^ vld1q_u8(const0),
		k ^ vld1q_u8(const1),
	}};
	int i;

	preload_sbox();

	for (i = 0; i < 5; i++) {
		st = aegis128_update_neon(st, k);
		st = aegis128_update_neon(st, kiv);
	}
	aegis128_save_state_neon(st, state);
}

void crypto_aegis128_update_neon(void *state, const void *msg)
{
	struct aegis128_state st = aegis128_load_state_neon(state);

	preload_sbox();

	st = aegis128_update_neon(st, vld1q_u8(msg));

	aegis128_save_state_neon(st, state);
}

void crypto_aegis128_encrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size)
{
	struct aegis128_state st = aegis128_load_state_neon(state);
	uint8x16_t msg;

	preload_sbox();

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

	preload_sbox();

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

void crypto_aegis128_final_neon(void *state, void *tag_xor, uint64_t assoclen,
				uint64_t cryptlen)
{
	struct aegis128_state st = aegis128_load_state_neon(state);
	uint8x16_t v;
	int i;

	preload_sbox();

	v = st.v[3] ^ (uint8x16_t)vcombine_u64(vmov_n_u64(8 * assoclen),
					       vmov_n_u64(8 * cryptlen));

	for (i = 0; i < 7; i++)
		st = aegis128_update_neon(st, v);

	v = vld1q_u8(tag_xor);
	v ^= st.v[0] ^ st.v[1] ^ st.v[2] ^ st.v[3] ^ st.v[4];
	vst1q_u8(tag_xor, v);
}
