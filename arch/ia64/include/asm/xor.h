/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Optimized RAID-5 checksumming functions for IA-64.
 */


extern void xor_ia64_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_ia64_3(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *);
extern void xor_ia64_4(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *, unsigned long *);
extern void xor_ia64_5(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *, unsigned long *, unsigned long *);

static struct xor_block_template xor_block_ia64 = {
	.name =	"ia64",
	.do_2 =	xor_ia64_2,
	.do_3 =	xor_ia64_3,
	.do_4 =	xor_ia64_4,
	.do_5 =	xor_ia64_5,
};

#define XOR_TRY_TEMPLATES	xor_speed(&xor_block_ia64)
