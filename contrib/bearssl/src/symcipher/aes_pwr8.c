/*
 * Copyright (c) 2017 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define BR_POWER_ASM_MACROS   1
#include "inner.h"

/*
 * This code contains the AES key schedule implementation using the
 * POWER8 opcodes.
 */

#if BR_POWER8

static void
key_schedule_128(unsigned char *sk, const unsigned char *key)
{
	long cc;

	static const uint32_t fmod[] = { 0x11B, 0x11B, 0x11B, 0x11B };
#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;

	/*
	 * We use the VSX instructions for loading and storing the
	 * key/subkeys, since they support unaligned accesses. The rest
	 * of the computation is VMX only. VMX register 0 is VSX
	 * register 32.
	 */
	asm volatile (

		/*
		 * v0 = all-zero word
		 * v1 = constant -8 / +8, copied into four words
		 * v2 = current subkey
		 * v3 = Rcon (x4 words)
		 * v6 = constant 8, copied into four words
		 * v7 = constant 0x11B, copied into four words
		 * v8 = constant for byteswapping words
		 */
		vspltisw(0, 0)
#if BR_POWER8_LE
		vspltisw(1, -8)
#else
		vspltisw(1, 8)
#endif
		lxvw4x(34, 0, %[key])
		vspltisw(3, 1)
		vspltisw(6, 8)
		lxvw4x(39, 0, %[fmod])
#if BR_POWER8_LE
		lxvw4x(40, 0, %[idx2be])
#endif

		/*
		 * First subkey is a copy of the key itself.
		 */
#if BR_POWER8_LE
		vperm(4, 2, 2, 8)
		stxvw4x(36, 0, %[sk])
#else
		stxvw4x(34, 0, %[sk])
#endif

		/*
		 * Loop must run 10 times.
		 */
		li(%[cc], 10)
		mtctr(%[cc])
	label(loop)
		/* Increment subkey address */
		addi(%[sk], %[sk], 16)

		/* Compute SubWord(RotWord(temp)) xor Rcon  (into v4, splat) */
		vrlw(4, 2, 1)
		vsbox(4, 4)
#if BR_POWER8_LE
		vxor(4, 4, 3)
#else
		vsldoi(5, 3, 0, 3)
		vxor(4, 4, 5)
#endif
		vspltw(4, 4, 3)

		/* XOR words for next subkey */
		vsldoi(5, 0, 2, 12)
		vxor(2, 2, 5)
		vsldoi(5, 0, 2, 12)
		vxor(2, 2, 5)
		vsldoi(5, 0, 2, 12)
		vxor(2, 2, 5)
		vxor(2, 2, 4)

		/* Store next subkey */
#if BR_POWER8_LE
		vperm(4, 2, 2, 8)
		stxvw4x(36, 0, %[sk])
#else
		stxvw4x(34, 0, %[sk])
#endif

		/* Update Rcon */
		vadduwm(3, 3, 3)
		vsrw(4, 3, 6)
		vsubuwm(4, 0, 4)
		vand(4, 4, 7)
		vxor(3, 3, 4)

		bdnz(loop)

: [sk] "+b" (sk), [cc] "+b" (cc)
: [key] "b" (key), [fmod] "b" (fmod)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "ctr", "memory"
	);
}

static void
key_schedule_192(unsigned char *sk, const unsigned char *key)
{
	long cc;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;

	/*
	 * We use the VSX instructions for loading and storing the
	 * key/subkeys, since they support unaligned accesses. The rest
	 * of the computation is VMX only. VMX register 0 is VSX
	 * register 32.
	 */
	asm volatile (

		/*
		 * v0 = all-zero word
		 * v1 = constant -8 / +8, copied into four words
		 * v2, v3 = current subkey
		 * v5 = Rcon (x4 words) (already shifted on big-endian)
		 * v6 = constant 8, copied into four words
		 * v8 = constant for byteswapping words
		 *
		 * The left two words of v3 are ignored.
		 */
		vspltisw(0, 0)
#if BR_POWER8_LE
		vspltisw(1, -8)
#else
		vspltisw(1, 8)
#endif
		li(%[cc], 8)
		lxvw4x(34, 0, %[key])
		lxvw4x(35, %[cc], %[key])
		vsldoi(3, 3, 0, 8)
		vspltisw(5, 1)
#if !BR_POWER8_LE
		vsldoi(5, 5, 0, 3)
#endif
		vspltisw(6, 8)
#if BR_POWER8_LE
		lxvw4x(40, 0, %[idx2be])
#endif

		/*
		 * Loop must run 8 times. Each iteration produces 256
		 * bits of subkeys, with a 64-bit overlap.
		 */
		li(%[cc], 8)
		mtctr(%[cc])
		li(%[cc], 16)
	label(loop)

		/*
		 * Last 6 words in v2:v3l. Compute next 6 words into
		 * v3r:v4.
		 */
		vrlw(10, 3, 1)
		vsbox(10, 10)
		vxor(10, 10, 5)
		vspltw(10, 10, 1)
		vsldoi(11, 0, 10, 8)

		vsldoi(12, 0, 2, 12)
		vxor(12, 2, 12)
		vsldoi(13, 0, 12, 12)
		vxor(12, 12, 13)
		vsldoi(13, 0, 12, 12)
		vxor(12, 12, 13)

		vspltw(13, 12, 3)
		vxor(13, 13, 3)
		vsldoi(14, 0, 3, 12)
		vxor(13, 13, 14)

		vsldoi(4, 12, 13, 8)
		vsldoi(14, 0, 3, 8)
		vsldoi(3, 14, 12, 8)

		vxor(3, 3, 11)
		vxor(4, 4, 10)

		/*
		 * Update Rcon. Since for a 192-bit key, we use only 8
		 * such constants, we will not hit the field modulus,
		 * so a simple shift (addition) works well.
		 */
		vadduwm(5, 5, 5)

		/*
		 * Write out the two left 128-bit words
		 */
#if BR_POWER8_LE
		vperm(10, 2, 2, 8)
		vperm(11, 3, 3, 8)
		stxvw4x(42, 0, %[sk])
		stxvw4x(43, %[cc], %[sk])
#else
		stxvw4x(34, 0, %[sk])
		stxvw4x(35, %[cc], %[sk])
#endif
		addi(%[sk], %[sk], 24)

		/*
		 * Shift words for next iteration.
		 */
		vsldoi(2, 3, 4, 8)
		vsldoi(3, 4, 0, 8)

		bdnz(loop)

		/*
		 * The loop wrote the first 50 subkey words, but we need
		 * to produce 52, so we must do one last write.
		 */
#if BR_POWER8_LE
		vperm(10, 2, 2, 8)
		stxvw4x(42, 0, %[sk])
#else
		stxvw4x(34, 0, %[sk])
#endif

: [sk] "+b" (sk), [cc] "+b" (cc)
: [key] "b" (key)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "ctr", "memory"
	);
}

static void
key_schedule_256(unsigned char *sk, const unsigned char *key)
{
	long cc;

#if BR_POWER8_LE
	static const uint32_t idx2be[] = {
		0x03020100, 0x07060504, 0x0B0A0908, 0x0F0E0D0C
	};
#endif

	cc = 0;

	/*
	 * We use the VSX instructions for loading and storing the
	 * key/subkeys, since they support unaligned accesses. The rest
	 * of the computation is VMX only. VMX register 0 is VSX
	 * register 32.
	 */
	asm volatile (

		/*
		 * v0 = all-zero word
		 * v1 = constant -8 / +8, copied into four words
		 * v2, v3 = current subkey
		 * v6 = Rcon (x4 words) (already shifted on big-endian)
		 * v7 = constant 8, copied into four words
		 * v8 = constant for byteswapping words
		 *
		 * The left two words of v3 are ignored.
		 */
		vspltisw(0, 0)
#if BR_POWER8_LE
		vspltisw(1, -8)
#else
		vspltisw(1, 8)
#endif
		li(%[cc], 16)
		lxvw4x(34, 0, %[key])
		lxvw4x(35, %[cc], %[key])
		vspltisw(6, 1)
#if !BR_POWER8_LE
		vsldoi(6, 6, 0, 3)
#endif
		vspltisw(7, 8)
#if BR_POWER8_LE
		lxvw4x(40, 0, %[idx2be])
#endif

		/*
		 * Loop must run 7 times. Each iteration produces two
		 * subkeys.
		 */
		li(%[cc], 7)
		mtctr(%[cc])
		li(%[cc], 16)
	label(loop)

		/*
		 * Current words are in v2:v3. Compute next word in v4.
		 */
		vrlw(10, 3, 1)
		vsbox(10, 10)
		vxor(10, 10, 6)
		vspltw(10, 10, 3)

		vsldoi(4, 0, 2, 12)
		vxor(4, 2, 4)
		vsldoi(5, 0, 4, 12)
		vxor(4, 4, 5)
		vsldoi(5, 0, 4, 12)
		vxor(4, 4, 5)
		vxor(4, 4, 10)

		/*
		 * Then other word in v5.
		 */
		vsbox(10, 4)
		vspltw(10, 10, 3)

		vsldoi(5, 0, 3, 12)
		vxor(5, 3, 5)
		vsldoi(11, 0, 5, 12)
		vxor(5, 5, 11)
		vsldoi(11, 0, 5, 12)
		vxor(5, 5, 11)
		vxor(5, 5, 10)

		/*
		 * Update Rcon. Since for a 256-bit key, we use only 7
		 * such constants, we will not hit the field modulus,
		 * so a simple shift (addition) works well.
		 */
		vadduwm(6, 6, 6)

		/*
		 * Write out the two left 128-bit words
		 */
#if BR_POWER8_LE
		vperm(10, 2, 2, 8)
		vperm(11, 3, 3, 8)
		stxvw4x(42, 0, %[sk])
		stxvw4x(43, %[cc], %[sk])
#else
		stxvw4x(34, 0, %[sk])
		stxvw4x(35, %[cc], %[sk])
#endif
		addi(%[sk], %[sk], 32)

		/*
		 * Replace v2:v3 with v4:v5.
		 */
		vxor(2, 0, 4)
		vxor(3, 0, 5)

		bdnz(loop)

		/*
		 * The loop wrote the first 14 subkeys, but we need 15,
		 * so we must do an extra write.
		 */
#if BR_POWER8_LE
		vperm(10, 2, 2, 8)
		stxvw4x(42, 0, %[sk])
#else
		stxvw4x(34, 0, %[sk])
#endif

: [sk] "+b" (sk), [cc] "+b" (cc)
: [key] "b" (key)
#if BR_POWER8_LE
	, [idx2be] "b" (idx2be)
#endif
: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "ctr", "memory"
	);
}

/* see inner.h */
int
br_aes_pwr8_supported(void)
{
	return 1;
}

/* see inner.h */
unsigned
br_aes_pwr8_keysched(unsigned char *sk, const void *key, size_t len)
{
	switch (len) {
	case 16:
		key_schedule_128(sk, key);
		return 10;
	case 24:
		key_schedule_192(sk, key);
		return 12;
	default:
		key_schedule_256(sk, key);
		return 14;
	}
}

#endif
