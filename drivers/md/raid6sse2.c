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
 * raid6sse2.c
 *
 * SSE-2 implementation of RAID-6 syndrome functions
 *
 */

#if defined(__i386__) || defined(__x86_64__)

#include "raid6.h"
#include "raid6x86.h"

static const struct raid6_sse_constants {
	u64 x1d[2];
} raid6_sse_constants  __attribute__((aligned(16))) = {
	{ 0x1d1d1d1d1d1d1d1dULL, 0x1d1d1d1d1d1d1d1dULL },
};

static int raid6_have_sse2(void)
{
#ifdef __KERNEL__
	/* Not really boot_cpu but "all_cpus" */
	return boot_cpu_has(X86_FEATURE_MMX) &&
		boot_cpu_has(X86_FEATURE_FXSR) &&
		boot_cpu_has(X86_FEATURE_XMM) &&
		boot_cpu_has(X86_FEATURE_XMM2);
#else
	/* User space test code */
	u32 features = cpuid_features();
	return ( (features & (15<<23)) == (15<<23) );
#endif
}

/*
 * Plain SSE2 implementation
 */
static void raid6_sse21_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	raid6_sse_save_t sa;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	raid6_before_sse2(&sa);

	asm volatile("movdqa %0,%%xmm0" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm5,%xmm5");	/* Zero temp */

	for ( d = 0 ; d < bytes ; d += 16 ) {
		asm volatile("prefetchnta %0" : : "m" (dptr[z0][d]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (dptr[z0][d])); /* P[0] */
		asm volatile("prefetchnta %0" : : "m" (dptr[z0-1][d]));
		asm volatile("movdqa %xmm2,%xmm4"); /* Q[0] */
		asm volatile("movdqa %0,%%xmm6" : : "m" (dptr[z0-1][d]));
		for ( z = z0-2 ; z >= 0 ; z-- ) {
			asm volatile("prefetchnta %0" : : "m" (dptr[z][d]));
			asm volatile("pcmpgtb %xmm4,%xmm5");
			asm volatile("paddb %xmm4,%xmm4");
			asm volatile("pand %xmm0,%xmm5");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm6,%xmm2");
			asm volatile("pxor %xmm6,%xmm4");
			asm volatile("movdqa %0,%%xmm6" : : "m" (dptr[z][d]));
		}
		asm volatile("pcmpgtb %xmm4,%xmm5");
		asm volatile("paddb %xmm4,%xmm4");
		asm volatile("pand %xmm0,%xmm5");
		asm volatile("pxor %xmm5,%xmm4");
		asm volatile("pxor %xmm5,%xmm5");
		asm volatile("pxor %xmm6,%xmm2");
		asm volatile("pxor %xmm6,%xmm4");

		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d]));
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[d]));
		asm volatile("pxor %xmm4,%xmm4");
	}

	raid6_after_sse2(&sa);
	asm volatile("sfence" : : : "memory");
}

const struct raid6_calls raid6_sse2x1 = {
	raid6_sse21_gen_syndrome,
	raid6_have_sse2,
	"sse2x1",
	1			/* Has cache hints */
};

/*
 * Unrolled-by-2 SSE2 implementation
 */
static void raid6_sse22_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	raid6_sse_save_t sa;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	raid6_before_sse2(&sa);

	asm volatile("movdqa %0,%%xmm0" : : "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm5,%xmm5"); /* Zero temp */
	asm volatile("pxor %xmm7,%xmm7"); /* Zero temp */

	/* We uniformly assume a single prefetch covers at least 32 bytes */
	for ( d = 0 ; d < bytes ; d += 32 ) {
		asm volatile("prefetchnta %0" : : "m" (dptr[z0][d]));
		asm volatile("movdqa %0,%%xmm2" : : "m" (dptr[z0][d]));    /* P[0] */
		asm volatile("movdqa %0,%%xmm3" : : "m" (dptr[z0][d+16])); /* P[1] */
		asm volatile("movdqa %xmm2,%xmm4"); /* Q[0] */
		asm volatile("movdqa %xmm3,%xmm6"); /* Q[1] */
		for ( z = z0-1 ; z >= 0 ; z-- ) {
			asm volatile("prefetchnta %0" : : "m" (dptr[z][d]));
			asm volatile("pcmpgtb %xmm4,%xmm5");
			asm volatile("pcmpgtb %xmm6,%xmm7");
			asm volatile("paddb %xmm4,%xmm4");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("pand %xmm0,%xmm5");
			asm volatile("pand %xmm0,%xmm7");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("movdqa %0,%%xmm5" : : "m" (dptr[z][d]));
			asm volatile("movdqa %0,%%xmm7" : : "m" (dptr[z][d+16]));
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm7,%xmm7");
		}
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d]));
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+16]));
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[d]));
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[d+16]));
	}

	raid6_after_sse2(&sa);
	asm volatile("sfence" : : : "memory");
}

const struct raid6_calls raid6_sse2x2 = {
	raid6_sse22_gen_syndrome,
	raid6_have_sse2,
	"sse2x2",
	1			/* Has cache hints */
};

#endif

#ifdef __x86_64__

/*
 * Unrolled-by-4 SSE2 implementation
 */
static void raid6_sse24_gen_syndrome(int disks, size_t bytes, void **ptrs)
{
	u8 **dptr = (u8 **)ptrs;
	u8 *p, *q;
	int d, z, z0;
	raid6_sse16_save_t sa;

	z0 = disks - 3;		/* Highest data disk */
	p = dptr[z0+1];		/* XOR parity */
	q = dptr[z0+2];		/* RS syndrome */

	raid6_before_sse16(&sa);

	asm volatile("movdqa %0,%%xmm0" :: "m" (raid6_sse_constants.x1d[0]));
	asm volatile("pxor %xmm2,%xmm2");	/* P[0] */
	asm volatile("pxor %xmm3,%xmm3");	/* P[1] */
	asm volatile("pxor %xmm4,%xmm4"); 	/* Q[0] */
	asm volatile("pxor %xmm5,%xmm5");	/* Zero temp */
	asm volatile("pxor %xmm6,%xmm6"); 	/* Q[1] */
	asm volatile("pxor %xmm7,%xmm7"); 	/* Zero temp */
	asm volatile("pxor %xmm10,%xmm10");	/* P[2] */
	asm volatile("pxor %xmm11,%xmm11");	/* P[3] */
	asm volatile("pxor %xmm12,%xmm12"); 	/* Q[2] */
	asm volatile("pxor %xmm13,%xmm13");	/* Zero temp */
	asm volatile("pxor %xmm14,%xmm14"); 	/* Q[3] */
	asm volatile("pxor %xmm15,%xmm15"); 	/* Zero temp */

	for ( d = 0 ; d < bytes ; d += 64 ) {
		for ( z = z0 ; z >= 0 ; z-- ) {
			/* The second prefetch seems to improve performance... */
			asm volatile("prefetchnta %0" :: "m" (dptr[z][d]));
			asm volatile("prefetchnta %0" :: "m" (dptr[z][d+32]));
			asm volatile("pcmpgtb %xmm4,%xmm5");
			asm volatile("pcmpgtb %xmm6,%xmm7");
			asm volatile("pcmpgtb %xmm12,%xmm13");
			asm volatile("pcmpgtb %xmm14,%xmm15");
			asm volatile("paddb %xmm4,%xmm4");
			asm volatile("paddb %xmm6,%xmm6");
			asm volatile("paddb %xmm12,%xmm12");
			asm volatile("paddb %xmm14,%xmm14");
			asm volatile("pand %xmm0,%xmm5");
			asm volatile("pand %xmm0,%xmm7");
			asm volatile("pand %xmm0,%xmm13");
			asm volatile("pand %xmm0,%xmm15");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("pxor %xmm13,%xmm12");
			asm volatile("pxor %xmm15,%xmm14");
			asm volatile("movdqa %0,%%xmm5" :: "m" (dptr[z][d]));
			asm volatile("movdqa %0,%%xmm7" :: "m" (dptr[z][d+16]));
			asm volatile("movdqa %0,%%xmm13" :: "m" (dptr[z][d+32]));
			asm volatile("movdqa %0,%%xmm15" :: "m" (dptr[z][d+48]));
			asm volatile("pxor %xmm5,%xmm2");
			asm volatile("pxor %xmm7,%xmm3");
			asm volatile("pxor %xmm13,%xmm10");
			asm volatile("pxor %xmm15,%xmm11");
			asm volatile("pxor %xmm5,%xmm4");
			asm volatile("pxor %xmm7,%xmm6");
			asm volatile("pxor %xmm13,%xmm12");
			asm volatile("pxor %xmm15,%xmm14");
			asm volatile("pxor %xmm5,%xmm5");
			asm volatile("pxor %xmm7,%xmm7");
			asm volatile("pxor %xmm13,%xmm13");
			asm volatile("pxor %xmm15,%xmm15");
		}
		asm volatile("movntdq %%xmm2,%0" : "=m" (p[d]));
		asm volatile("pxor %xmm2,%xmm2");
		asm volatile("movntdq %%xmm3,%0" : "=m" (p[d+16]));
		asm volatile("pxor %xmm3,%xmm3");
		asm volatile("movntdq %%xmm10,%0" : "=m" (p[d+32]));
		asm volatile("pxor %xmm10,%xmm10");
		asm volatile("movntdq %%xmm11,%0" : "=m" (p[d+48]));
		asm volatile("pxor %xmm11,%xmm11");
		asm volatile("movntdq %%xmm4,%0" : "=m" (q[d]));
		asm volatile("pxor %xmm4,%xmm4");
		asm volatile("movntdq %%xmm6,%0" : "=m" (q[d+16]));
		asm volatile("pxor %xmm6,%xmm6");
		asm volatile("movntdq %%xmm12,%0" : "=m" (q[d+32]));
		asm volatile("pxor %xmm12,%xmm12");
		asm volatile("movntdq %%xmm14,%0" : "=m" (q[d+48]));
		asm volatile("pxor %xmm14,%xmm14");
	}
	asm volatile("sfence" : : : "memory");
	raid6_after_sse16(&sa);
}

const struct raid6_calls raid6_sse2x4 = {
	raid6_sse24_gen_syndrome,
	raid6_have_sse2,
	"sse2x4",
	1			/* Has cache hints */
};

#endif
