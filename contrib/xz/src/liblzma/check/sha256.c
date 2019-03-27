///////////////////////////////////////////////////////////////////////////////
//
/// \file       sha256.c
/// \brief      SHA-256
///
/// \todo       Crypto++ has x86 ASM optimizations. They use SSE so if they
///             are imported to liblzma, SSE instructions need to be used
///             conditionally to keep the code working on older boxes.
//
//  This code is based on the code found from 7-Zip, which has a modified
//  version of the SHA-256 found from Crypto++ <http://www.cryptopp.com/>.
//  The code was modified a little to fit into liblzma.
//
//  Authors:    Kevin Springle
//              Wei Dai
//              Igor Pavlov
//              Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "check.h"

// Rotate a uint32_t. GCC can optimize this to a rotate instruction
// at least on x86.
static inline uint32_t
rotr_32(uint32_t num, unsigned amount)
{
        return (num >> amount) | (num << (32 - amount));
}

#define blk0(i) (W[i] = conv32be(data[i]))
#define blk2(i) (W[i & 15] += s1(W[(i - 2) & 15]) + W[(i - 7) & 15] \
		+ s0(W[(i - 15) & 15]))

#define Ch(x, y, z) (z ^ (x & (y ^ z)))
#define Maj(x, y, z) ((x & (y ^ z)) + (y & z))

#define a(i) T[(0 - i) & 7]
#define b(i) T[(1 - i) & 7]
#define c(i) T[(2 - i) & 7]
#define d(i) T[(3 - i) & 7]
#define e(i) T[(4 - i) & 7]
#define f(i) T[(5 - i) & 7]
#define g(i) T[(6 - i) & 7]
#define h(i) T[(7 - i) & 7]

#define R(i, j, blk) \
	h(i) += S1(e(i)) + Ch(e(i), f(i), g(i)) + SHA256_K[i + j] + blk; \
	d(i) += h(i); \
	h(i) += S0(a(i)) + Maj(a(i), b(i), c(i))
#define R0(i) R(i, 0, blk0(i))
#define R2(i) R(i, j, blk2(i))

#define S0(x) rotr_32(x ^ rotr_32(x ^ rotr_32(x, 9), 11), 2)
#define S1(x) rotr_32(x ^ rotr_32(x ^ rotr_32(x, 14), 5), 6)
#define s0(x) (rotr_32(x ^ rotr_32(x, 11), 7) ^ (x >> 3))
#define s1(x) (rotr_32(x ^ rotr_32(x, 2), 17) ^ (x >> 10))


static const uint32_t SHA256_K[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
	0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
	0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
	0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
	0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
	0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
	0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
	0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
	0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
};


static void
transform(uint32_t state[8], const uint32_t data[16])
{
	uint32_t W[16];
	uint32_t T[8];

	// Copy state[] to working vars.
	memcpy(T, state, sizeof(T));

	// The first 16 operations unrolled
	R0( 0); R0( 1); R0( 2); R0( 3);
	R0( 4); R0( 5); R0( 6); R0( 7);
	R0( 8); R0( 9); R0(10); R0(11);
	R0(12); R0(13); R0(14); R0(15);

	// The remaining 48 operations partially unrolled
	for (unsigned int j = 16; j < 64; j += 16) {
		R2( 0); R2( 1); R2( 2); R2( 3);
		R2( 4); R2( 5); R2( 6); R2( 7);
		R2( 8); R2( 9); R2(10); R2(11);
		R2(12); R2(13); R2(14); R2(15);
	}

	// Add the working vars back into state[].
	state[0] += a(0);
	state[1] += b(0);
	state[2] += c(0);
	state[3] += d(0);
	state[4] += e(0);
	state[5] += f(0);
	state[6] += g(0);
	state[7] += h(0);
}


static void
process(lzma_check_state *check)
{
	transform(check->state.sha256.state, check->buffer.u32);
	return;
}


extern void
lzma_sha256_init(lzma_check_state *check)
{
	static const uint32_t s[8] = {
		0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
		0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
	};

	memcpy(check->state.sha256.state, s, sizeof(s));
	check->state.sha256.size = 0;

	return;
}


extern void
lzma_sha256_update(const uint8_t *buf, size_t size, lzma_check_state *check)
{
	// Copy the input data into a properly aligned temporary buffer.
	// This way we can be called with arbitrarily sized buffers
	// (no need to be multiple of 64 bytes), and the code works also
	// on architectures that don't allow unaligned memory access.
	while (size > 0) {
		const size_t copy_start = check->state.sha256.size & 0x3F;
		size_t copy_size = 64 - copy_start;
		if (copy_size > size)
			copy_size = size;

		memcpy(check->buffer.u8 + copy_start, buf, copy_size);

		buf += copy_size;
		size -= copy_size;
		check->state.sha256.size += copy_size;

		if ((check->state.sha256.size & 0x3F) == 0)
			process(check);
	}

	return;
}


extern void
lzma_sha256_finish(lzma_check_state *check)
{
	// Add padding as described in RFC 3174 (it describes SHA-1 but
	// the same padding style is used for SHA-256 too).
	size_t pos = check->state.sha256.size & 0x3F;
	check->buffer.u8[pos++] = 0x80;

	while (pos != 64 - 8) {
		if (pos == 64) {
			process(check);
			pos = 0;
		}

		check->buffer.u8[pos++] = 0x00;
	}

	// Convert the message size from bytes to bits.
	check->state.sha256.size *= 8;

	check->buffer.u64[(64 - 8) / 8] = conv64be(check->state.sha256.size);

	process(check);

	for (size_t i = 0; i < 8; ++i)
		check->buffer.u32[i] = conv32be(check->state.sha256.state[i]);

	return;
}
