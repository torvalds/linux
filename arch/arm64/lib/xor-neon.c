// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/lib/xor-neon.c
 *
 * Authors: Jackie Liu <liuyun01@kylinos.cn>
 * Copyright (C) 2018,Tianjin KYLIN Information Technology Co., Ltd.
 */

#include <linux/raid/xor.h>
#include <linux/module.h>
#include <asm/neon-intrinsics.h>

static void xor_arm64_neon_2(unsigned long bytes, unsigned long * __restrict p1,
	const unsigned long * __restrict p2)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 */
		v0 = veorq_u64(vld1q_u64(dp1 +  0), vld1q_u64(dp2 +  0));
		v1 = veorq_u64(vld1q_u64(dp1 +  2), vld1q_u64(dp2 +  2));
		v2 = veorq_u64(vld1q_u64(dp1 +  4), vld1q_u64(dp2 +  4));
		v3 = veorq_u64(vld1q_u64(dp1 +  6), vld1q_u64(dp2 +  6));

		/* store */
		vst1q_u64(dp1 +  0, v0);
		vst1q_u64(dp1 +  2, v1);
		vst1q_u64(dp1 +  4, v2);
		vst1q_u64(dp1 +  6, v3);

		dp1 += 8;
		dp2 += 8;
	} while (--lines > 0);
}

static void xor_arm64_neon_3(unsigned long bytes, unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 */
		v0 = veorq_u64(vld1q_u64(dp1 +  0), vld1q_u64(dp2 +  0));
		v1 = veorq_u64(vld1q_u64(dp1 +  2), vld1q_u64(dp2 +  2));
		v2 = veorq_u64(vld1q_u64(dp1 +  4), vld1q_u64(dp2 +  4));
		v3 = veorq_u64(vld1q_u64(dp1 +  6), vld1q_u64(dp2 +  6));

		/* p1 ^= p3 */
		v0 = veorq_u64(v0, vld1q_u64(dp3 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp3 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp3 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp3 +  6));

		/* store */
		vst1q_u64(dp1 +  0, v0);
		vst1q_u64(dp1 +  2, v1);
		vst1q_u64(dp1 +  4, v2);
		vst1q_u64(dp1 +  6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
	} while (--lines > 0);
}

static void xor_arm64_neon_4(unsigned long bytes, unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3,
	const unsigned long * __restrict p4)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;
	uint64_t *dp4 = (uint64_t *)p4;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 */
		v0 = veorq_u64(vld1q_u64(dp1 +  0), vld1q_u64(dp2 +  0));
		v1 = veorq_u64(vld1q_u64(dp1 +  2), vld1q_u64(dp2 +  2));
		v2 = veorq_u64(vld1q_u64(dp1 +  4), vld1q_u64(dp2 +  4));
		v3 = veorq_u64(vld1q_u64(dp1 +  6), vld1q_u64(dp2 +  6));

		/* p1 ^= p3 */
		v0 = veorq_u64(v0, vld1q_u64(dp3 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp3 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp3 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp3 +  6));

		/* p1 ^= p4 */
		v0 = veorq_u64(v0, vld1q_u64(dp4 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp4 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp4 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp4 +  6));

		/* store */
		vst1q_u64(dp1 +  0, v0);
		vst1q_u64(dp1 +  2, v1);
		vst1q_u64(dp1 +  4, v2);
		vst1q_u64(dp1 +  6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
		dp4 += 8;
	} while (--lines > 0);
}

static void xor_arm64_neon_5(unsigned long bytes, unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3,
	const unsigned long * __restrict p4,
	const unsigned long * __restrict p5)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;
	uint64_t *dp4 = (uint64_t *)p4;
	uint64_t *dp5 = (uint64_t *)p5;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 */
		v0 = veorq_u64(vld1q_u64(dp1 +  0), vld1q_u64(dp2 +  0));
		v1 = veorq_u64(vld1q_u64(dp1 +  2), vld1q_u64(dp2 +  2));
		v2 = veorq_u64(vld1q_u64(dp1 +  4), vld1q_u64(dp2 +  4));
		v3 = veorq_u64(vld1q_u64(dp1 +  6), vld1q_u64(dp2 +  6));

		/* p1 ^= p3 */
		v0 = veorq_u64(v0, vld1q_u64(dp3 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp3 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp3 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp3 +  6));

		/* p1 ^= p4 */
		v0 = veorq_u64(v0, vld1q_u64(dp4 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp4 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp4 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp4 +  6));

		/* p1 ^= p5 */
		v0 = veorq_u64(v0, vld1q_u64(dp5 +  0));
		v1 = veorq_u64(v1, vld1q_u64(dp5 +  2));
		v2 = veorq_u64(v2, vld1q_u64(dp5 +  4));
		v3 = veorq_u64(v3, vld1q_u64(dp5 +  6));

		/* store */
		vst1q_u64(dp1 +  0, v0);
		vst1q_u64(dp1 +  2, v1);
		vst1q_u64(dp1 +  4, v2);
		vst1q_u64(dp1 +  6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
		dp4 += 8;
		dp5 += 8;
	} while (--lines > 0);
}

struct xor_block_template xor_block_inner_neon __ro_after_init = {
	.name	= "__inner_neon__",
	.do_2	= xor_arm64_neon_2,
	.do_3	= xor_arm64_neon_3,
	.do_4	= xor_arm64_neon_4,
	.do_5	= xor_arm64_neon_5,
};
EXPORT_SYMBOL(xor_block_inner_neon);

static inline uint64x2_t eor3(uint64x2_t p, uint64x2_t q, uint64x2_t r)
{
	uint64x2_t res;

	asm(ARM64_ASM_PREAMBLE ".arch_extension sha3\n"
	    "eor3 %0.16b, %1.16b, %2.16b, %3.16b"
	    : "=w"(res) : "w"(p), "w"(q), "w"(r));
	return res;
}

static void xor_arm64_eor3_3(unsigned long bytes,
	unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 ^ p3 */
		v0 = eor3(vld1q_u64(dp1 + 0), vld1q_u64(dp2 + 0),
			  vld1q_u64(dp3 + 0));
		v1 = eor3(vld1q_u64(dp1 + 2), vld1q_u64(dp2 + 2),
			  vld1q_u64(dp3 + 2));
		v2 = eor3(vld1q_u64(dp1 + 4), vld1q_u64(dp2 + 4),
			  vld1q_u64(dp3 + 4));
		v3 = eor3(vld1q_u64(dp1 + 6), vld1q_u64(dp2 + 6),
			  vld1q_u64(dp3 + 6));

		/* store */
		vst1q_u64(dp1 + 0, v0);
		vst1q_u64(dp1 + 2, v1);
		vst1q_u64(dp1 + 4, v2);
		vst1q_u64(dp1 + 6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
	} while (--lines > 0);
}

static void xor_arm64_eor3_4(unsigned long bytes,
	unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3,
	const unsigned long * __restrict p4)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;
	uint64_t *dp4 = (uint64_t *)p4;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 ^ p3 */
		v0 = eor3(vld1q_u64(dp1 + 0), vld1q_u64(dp2 + 0),
			  vld1q_u64(dp3 + 0));
		v1 = eor3(vld1q_u64(dp1 + 2), vld1q_u64(dp2 + 2),
			  vld1q_u64(dp3 + 2));
		v2 = eor3(vld1q_u64(dp1 + 4), vld1q_u64(dp2 + 4),
			  vld1q_u64(dp3 + 4));
		v3 = eor3(vld1q_u64(dp1 + 6), vld1q_u64(dp2 + 6),
			  vld1q_u64(dp3 + 6));

		/* p1 ^= p4 */
		v0 = veorq_u64(v0, vld1q_u64(dp4 + 0));
		v1 = veorq_u64(v1, vld1q_u64(dp4 + 2));
		v2 = veorq_u64(v2, vld1q_u64(dp4 + 4));
		v3 = veorq_u64(v3, vld1q_u64(dp4 + 6));

		/* store */
		vst1q_u64(dp1 + 0, v0);
		vst1q_u64(dp1 + 2, v1);
		vst1q_u64(dp1 + 4, v2);
		vst1q_u64(dp1 + 6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
		dp4 += 8;
	} while (--lines > 0);
}

static void xor_arm64_eor3_5(unsigned long bytes,
	unsigned long * __restrict p1,
	const unsigned long * __restrict p2,
	const unsigned long * __restrict p3,
	const unsigned long * __restrict p4,
	const unsigned long * __restrict p5)
{
	uint64_t *dp1 = (uint64_t *)p1;
	uint64_t *dp2 = (uint64_t *)p2;
	uint64_t *dp3 = (uint64_t *)p3;
	uint64_t *dp4 = (uint64_t *)p4;
	uint64_t *dp5 = (uint64_t *)p5;

	register uint64x2_t v0, v1, v2, v3;
	long lines = bytes / (sizeof(uint64x2_t) * 4);

	do {
		/* p1 ^= p2 ^ p3 */
		v0 = eor3(vld1q_u64(dp1 + 0), vld1q_u64(dp2 + 0),
			  vld1q_u64(dp3 + 0));
		v1 = eor3(vld1q_u64(dp1 + 2), vld1q_u64(dp2 + 2),
			  vld1q_u64(dp3 + 2));
		v2 = eor3(vld1q_u64(dp1 + 4), vld1q_u64(dp2 + 4),
			  vld1q_u64(dp3 + 4));
		v3 = eor3(vld1q_u64(dp1 + 6), vld1q_u64(dp2 + 6),
			  vld1q_u64(dp3 + 6));

		/* p1 ^= p4 ^ p5 */
		v0 = eor3(v0, vld1q_u64(dp4 + 0), vld1q_u64(dp5 + 0));
		v1 = eor3(v1, vld1q_u64(dp4 + 2), vld1q_u64(dp5 + 2));
		v2 = eor3(v2, vld1q_u64(dp4 + 4), vld1q_u64(dp5 + 4));
		v3 = eor3(v3, vld1q_u64(dp4 + 6), vld1q_u64(dp5 + 6));

		/* store */
		vst1q_u64(dp1 + 0, v0);
		vst1q_u64(dp1 + 2, v1);
		vst1q_u64(dp1 + 4, v2);
		vst1q_u64(dp1 + 6, v3);

		dp1 += 8;
		dp2 += 8;
		dp3 += 8;
		dp4 += 8;
		dp5 += 8;
	} while (--lines > 0);
}

static int __init xor_neon_init(void)
{
	if (IS_ENABLED(CONFIG_AS_HAS_SHA3) && cpu_have_named_feature(SHA3)) {
		xor_block_inner_neon.do_3 = xor_arm64_eor3_3;
		xor_block_inner_neon.do_4 = xor_arm64_eor3_4;
		xor_block_inner_neon.do_5 = xor_arm64_eor3_5;
	}
	return 0;
}
module_init(xor_neon_init);

static void __exit xor_neon_exit(void)
{
}
module_exit(xor_neon_exit);

MODULE_AUTHOR("Jackie Liu <liuyun01@kylinos.cn>");
MODULE_DESCRIPTION("ARMv8 XOR Extensions");
MODULE_LICENSE("GPL");
