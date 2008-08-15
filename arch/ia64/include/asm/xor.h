/*
 * Optimized RAID-5 checksumming functions for IA-64.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
