/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright 2002 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6mmx.c
 *
 * MMX implementation of RAID-6 syndrome functions
 */

#if defined(__i386__)

#include "raid6.h"
#include "raid6x86.h"

/* Shared with raid6sse1.c */
const struct raid6_mmx_constants {
	u64 x1d;
} raid6_mmx_constants = {
	0x1d1d1d1d1d1d1d1dULL,
};

static int raid6_have_mmx(void)
{
#ifdef __KERNEL__
	/* Not really "boot_cpu" but "all_cpus" */
	return boot_cpu_has(X86_FEATURE_MMX);
#else
	/* User space test code */
	u32 features = cpuid_features();
	return ( (features & (1<<23)) == (1<<23) );
#endif
}

/*
 * Plain MMX implementation
 */
static void raid6_mmx1_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	raid6_mmx_save_t sa;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	raid6_before_mmx(&sa);

	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm5,%mm5");	/* Zero temp */

	for ( d = 0 ; d < bytes ; d += 8 ) {
		asm volatile("movq %0,%%mm2" : : "m" (dptr[z0][d])); /* P[0] */
		asm volatile("movq %mm2,%mm4");	/* Q[0] */
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			asm volatile("movq %0,%%mm6" : : "m" (dptr[z][d]));
			asm volatile("pcmpgtb %mm4,%mm5");
			asm volatile("paddb %mm4,%mm4");
			asm volatile("pand %mm0,%mm5");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm6,%mm2");
			asm volatile("pxor %mm6,%mm4");
		}
		asm volatile("movq %%mm2,%0" : "=m" (p[d]));
		asm volatile("pxor %mm2,%mm2");
		asm volatile("movq %%mm4,%0" : "=m" (q[d]));
		asm volatile("pxor %mm4,%mm4");
	}

	raid6_after_mmx(&sa);
}

const struct raid6_calls raid6_mmxx1 = {
	raid6_mmx1_gen_syndrome,
	raid6_have_mmx,
	"mmxx1",
	0
};

/*
 * Unrolled-by-2 MMX implementation
 */
static void raid6_mmx2_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	raid6_mmx_save_t sa;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	raid6_before_mmx(&sa);

	asm volatile("movq %0,%%mm0" : : "m" (raid6_mmx_constants.x1d));
	asm volatile("pxor %mm5,%mm5");	/* Zero temp */
	asm volatile("pxor %mm7,%mm7"); /* Zero temp */

	for ( d = 0 ; d < bytes ; d += 16 ) {
		asm volatile("movq %0,%%mm2" : : "m" (dptr[z0][d])); /* P[0] */
		asm volatile("movq %0,%%mm3" : : "m" (dptr[z0][d+8]));
		asm volatile("movq %mm2,%mm4"); /* Q[0] */
		asm volatile("movq %mm3,%mm6"); /* Q[1] */
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			asm volatile("pcmpgtb %mm4,%mm5");
			asm volatile("pcmpgtb %mm6,%mm7");
			asm volatile("paddb %mm4,%mm4");
			asm volatile("paddb %mm6,%mm6");
			asm volatile("pand %mm0,%mm5");
			asm volatile("pand %mm0,%mm7");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("movq %0,%%mm5" : : "m" (dptr[z][d]));
			asm volatile("movq %0,%%mm7" : : "m" (dptr[z][d+8]));
			asm volatile("pxor %mm5,%mm2");
			asm volatile("pxor %mm7,%mm3");
			asm volatile("pxor %mm5,%mm4");
			asm volatile("pxor %mm7,%mm6");
			asm volatile("pxor %mm5,%mm5");
			asm volatile("pxor %mm7,%mm7");
		}
		asm volatile("movq %%mm2,%0" : "=m" (p[d]));
		asm volatile("movq %%mm3,%0" : "=m" (p[d+8]));
		asm volatile("movq %%mm4,%0" : "=m" (q[d]));
		asm volatile("movq %%mm6,%0" : "=m" (q[d+8]));
	}

	raid6_after_mmx(&sa);
}

const struct raid6_calls raid6_mmxx2 = {
	raid6_mmx2_gen_syndrome,
	raid6_have_mmx,
	"mmxx2",
	0
};

#endif
