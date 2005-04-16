/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2002-2004 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Bostom MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * raid6x86.h
 *
 * Definitions common to x86 and x86-64 RAID-6 code only
 */

#ifndef LINUX_RAID_RAID6X86_H
#define LINUX_RAID_RAID6X86_H

#if defined(__i386__) || defined(__x86_64__)

#ifdef __x86_64__

typedef struct {
	unsigned int fsave[27];
	unsigned long cr0;
} raid6_mmx_save_t __attribute__((aligned(16)));

/* N.B.: For SSE we only save %xmm0-%xmm7 even for x86-64, since
   the code doesn't know about the additional x86-64 registers */
typedef struct {
	unsigned int sarea[8*4+2];
	unsigned long cr0;
} raid6_sse_save_t __attribute__((aligned(16)));

/* This is for x86-64-specific code which uses all 16 XMM registers */
typedef struct {
	unsigned int sarea[16*4+2];
	unsigned long cr0;
} raid6_sse16_save_t __attribute__((aligned(16)));

/* On x86-64 the stack *SHOULD* be 16-byte aligned, but currently this
   is buggy in the kernel and it's only 8-byte aligned in places, so
   we need to do this anyway.  Sigh. */
#define SAREA(x) ((unsigned int *)((((unsigned long)&(x)->sarea)+15) & ~15))

#else /* __i386__ */

typedef struct {
	unsigned int fsave[27];
	unsigned long cr0;
} raid6_mmx_save_t;

/* On i386, the stack is only 8-byte aligned, but SSE requires 16-byte
   alignment.  The +3 is so we have the slack space to manually align
   a properly-sized area correctly.  */
typedef struct {
	unsigned int sarea[8*4+3];
	unsigned long cr0;
} raid6_sse_save_t;

/* Find the 16-byte aligned save area */
#define SAREA(x) ((unsigned int *)((((unsigned long)&(x)->sarea)+15) & ~15))

#endif

#ifdef __KERNEL__ /* Real code */

/* Note: %cr0 is 32 bits on i386 and 64 bits on x86-64 */

static inline unsigned long raid6_get_fpu(void)
{
	unsigned long cr0;

	preempt_disable();
	asm volatile("mov %%cr0,%0 ; clts" : "=r" (cr0));
	return cr0;
}

static inline void raid6_put_fpu(unsigned long cr0)
{
	asm volatile("mov %0,%%cr0" : : "r" (cr0));
	preempt_enable();
}

#else /* Dummy code for user space testing */

static inline unsigned long raid6_get_fpu(void)
{
	return 0xf00ba6;
}

static inline void raid6_put_fpu(unsigned long cr0)
{
	(void)cr0;
}

#endif

static inline void raid6_before_mmx(raid6_mmx_save_t *s)
{
	s->cr0 = raid6_get_fpu();
	asm volatile("fsave %0 ; fwait" : "=m" (s->fsave[0]));
}

static inline void raid6_after_mmx(raid6_mmx_save_t *s)
{
	asm volatile("frstor %0" : : "m" (s->fsave[0]));
	raid6_put_fpu(s->cr0);
}

static inline void raid6_before_sse(raid6_sse_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	s->cr0 = raid6_get_fpu();

	asm volatile("movaps %%xmm0,%0" : "=m" (rsa[0]));
	asm volatile("movaps %%xmm1,%0" : "=m" (rsa[4]));
	asm volatile("movaps %%xmm2,%0" : "=m" (rsa[8]));
	asm volatile("movaps %%xmm3,%0" : "=m" (rsa[12]));
	asm volatile("movaps %%xmm4,%0" : "=m" (rsa[16]));
	asm volatile("movaps %%xmm5,%0" : "=m" (rsa[20]));
	asm volatile("movaps %%xmm6,%0" : "=m" (rsa[24]));
	asm volatile("movaps %%xmm7,%0" : "=m" (rsa[28]));
}

static inline void raid6_after_sse(raid6_sse_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	asm volatile("movaps %0,%%xmm0" : : "m" (rsa[0]));
	asm volatile("movaps %0,%%xmm1" : : "m" (rsa[4]));
	asm volatile("movaps %0,%%xmm2" : : "m" (rsa[8]));
	asm volatile("movaps %0,%%xmm3" : : "m" (rsa[12]));
	asm volatile("movaps %0,%%xmm4" : : "m" (rsa[16]));
	asm volatile("movaps %0,%%xmm5" : : "m" (rsa[20]));
	asm volatile("movaps %0,%%xmm6" : : "m" (rsa[24]));
	asm volatile("movaps %0,%%xmm7" : : "m" (rsa[28]));

	raid6_put_fpu(s->cr0);
}

static inline void raid6_before_sse2(raid6_sse_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	s->cr0 = raid6_get_fpu();

	asm volatile("movdqa %%xmm0,%0" : "=m" (rsa[0]));
	asm volatile("movdqa %%xmm1,%0" : "=m" (rsa[4]));
	asm volatile("movdqa %%xmm2,%0" : "=m" (rsa[8]));
	asm volatile("movdqa %%xmm3,%0" : "=m" (rsa[12]));
	asm volatile("movdqa %%xmm4,%0" : "=m" (rsa[16]));
	asm volatile("movdqa %%xmm5,%0" : "=m" (rsa[20]));
	asm volatile("movdqa %%xmm6,%0" : "=m" (rsa[24]));
	asm volatile("movdqa %%xmm7,%0" : "=m" (rsa[28]));
}

static inline void raid6_after_sse2(raid6_sse_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	asm volatile("movdqa %0,%%xmm0" : : "m" (rsa[0]));
	asm volatile("movdqa %0,%%xmm1" : : "m" (rsa[4]));
	asm volatile("movdqa %0,%%xmm2" : : "m" (rsa[8]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (rsa[12]));
	asm volatile("movdqa %0,%%xmm4" : : "m" (rsa[16]));
	asm volatile("movdqa %0,%%xmm5" : : "m" (rsa[20]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (rsa[24]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (rsa[28]));

	raid6_put_fpu(s->cr0);
}

#ifdef __x86_64__

static inline void raid6_before_sse16(raid6_sse16_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	s->cr0 = raid6_get_fpu();

	asm volatile("movdqa %%xmm0,%0" : "=m" (rsa[0]));
	asm volatile("movdqa %%xmm1,%0" : "=m" (rsa[4]));
	asm volatile("movdqa %%xmm2,%0" : "=m" (rsa[8]));
	asm volatile("movdqa %%xmm3,%0" : "=m" (rsa[12]));
	asm volatile("movdqa %%xmm4,%0" : "=m" (rsa[16]));
	asm volatile("movdqa %%xmm5,%0" : "=m" (rsa[20]));
	asm volatile("movdqa %%xmm6,%0" : "=m" (rsa[24]));
	asm volatile("movdqa %%xmm7,%0" : "=m" (rsa[28]));
	asm volatile("movdqa %%xmm8,%0" : "=m" (rsa[32]));
	asm volatile("movdqa %%xmm9,%0" : "=m" (rsa[36]));
	asm volatile("movdqa %%xmm10,%0" : "=m" (rsa[40]));
	asm volatile("movdqa %%xmm11,%0" : "=m" (rsa[44]));
	asm volatile("movdqa %%xmm12,%0" : "=m" (rsa[48]));
	asm volatile("movdqa %%xmm13,%0" : "=m" (rsa[52]));
	asm volatile("movdqa %%xmm14,%0" : "=m" (rsa[56]));
	asm volatile("movdqa %%xmm15,%0" : "=m" (rsa[60]));
}

static inline void raid6_after_sse16(raid6_sse16_save_t *s)
{
	unsigned int *rsa = SAREA(s);

	asm volatile("movdqa %0,%%xmm0" : : "m" (rsa[0]));
	asm volatile("movdqa %0,%%xmm1" : : "m" (rsa[4]));
	asm volatile("movdqa %0,%%xmm2" : : "m" (rsa[8]));
	asm volatile("movdqa %0,%%xmm3" : : "m" (rsa[12]));
	asm volatile("movdqa %0,%%xmm4" : : "m" (rsa[16]));
	asm volatile("movdqa %0,%%xmm5" : : "m" (rsa[20]));
	asm volatile("movdqa %0,%%xmm6" : : "m" (rsa[24]));
	asm volatile("movdqa %0,%%xmm7" : : "m" (rsa[28]));
	asm volatile("movdqa %0,%%xmm8" : : "m" (rsa[32]));
	asm volatile("movdqa %0,%%xmm9" : : "m" (rsa[36]));
	asm volatile("movdqa %0,%%xmm10" : : "m" (rsa[40]));
	asm volatile("movdqa %0,%%xmm11" : : "m" (rsa[44]));
	asm volatile("movdqa %0,%%xmm12" : : "m" (rsa[48]));
	asm volatile("movdqa %0,%%xmm13" : : "m" (rsa[52]));
	asm volatile("movdqa %0,%%xmm14" : : "m" (rsa[56]));
	asm volatile("movdqa %0,%%xmm15" : : "m" (rsa[60]));

	raid6_put_fpu(s->cr0);
}

#endif /* __x86_64__ */

/* User space test hack */
#ifndef __KERNEL__
static inline int cpuid_features(void)
{
	u32 eax = 1;
	u32 ebx, ecx, edx;

	asm volatile("cpuid" :
		     "+a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx));

	return edx;
}
#endif /* ndef __KERNEL__ */

#endif
#endif
