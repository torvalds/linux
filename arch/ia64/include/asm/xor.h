/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Optimized RAID-5 checksumming functions for IA-64.
 */


extern void xor_ia64_2(unsigned long bytes, unsigned long * __restrict p1,
		       const unsigned long * __restrict p2);
extern void xor_ia64_3(unsigned long bytes, unsigned long * __restrict p1,
		       const unsigned long * __restrict p2,
		       const unsigned long * __restrict p3);
extern void xor_ia64_4(unsigned long bytes, unsigned long * __restrict p1,
		       const unsigned long * __restrict p2,
		       const unsigned long * __restrict p3,
		       const unsigned long * __restrict p4);
extern void xor_ia64_5(unsigned long bytes, unsigned long * __restrict p1,
		       const unsigned long * __restrict p2,
		       const unsigned long * __restrict p3,
		       const unsigned long * __restrict p4,
		       const unsigned long * __restrict p5);

static struct xor_block_template xor_block_ia64 = {
	.name =	"ia64",
	.do_2 =	xor_ia64_2,
	.do_3 =	xor_ia64_3,
	.do_4 =	xor_ia64_4,
	.do_5 =	xor_ia64_5,
};

#define XOR_TRY_TEMPLATES	xor_speed(&xor_block_ia64)
