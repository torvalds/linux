#ifndef __ASM_METAG_CMPXCHG_LNKGET_H
#define __ASM_METAG_CMPXCHG_LNKGET_H

static inline unsigned long xchg_u32(volatile u32 *m, unsigned long val)
{
	int temp, old;

	smp_mb();

	asm volatile (
		      "1:	LNKGETD %1, [%2]\n"
		      "	LNKSETD	[%2], %3\n"
		      "	DEFR	%0, TXSTAT\n"
		      "	ANDT	%0, %0, #HI(0x3f000000)\n"
		      "	CMPT	%0, #HI(0x02000000)\n"
		      "	BNZ	1b\n"
#ifdef CONFIG_METAG_LNKGET_AROUND_CACHE
		      "	DCACHE	[%2], %0\n"
#endif
		      : "=&d" (temp), "=&d" (old)
		      : "da" (m), "da" (val)
		      : "cc"
		      );

	smp_mb();

	return old;
}

static inline unsigned long xchg_u8(volatile u8 *m, unsigned long val)
{
	int temp, old;

	smp_mb();

	asm volatile (
		      "1:	LNKGETD %1, [%2]\n"
		      "	LNKSETD	[%2], %3\n"
		      "	DEFR	%0, TXSTAT\n"
		      "	ANDT	%0, %0, #HI(0x3f000000)\n"
		      "	CMPT	%0, #HI(0x02000000)\n"
		      "	BNZ	1b\n"
#ifdef CONFIG_METAG_LNKGET_AROUND_CACHE
		      "	DCACHE	[%2], %0\n"
#endif
		      : "=&d" (temp), "=&d" (old)
		      : "da" (m), "da" (val & 0xff)
		      : "cc"
		      );

	smp_mb();

	return old;
}

static inline unsigned long __cmpxchg_u32(volatile int *m, unsigned long old,
					  unsigned long new)
{
	__u32 retval, temp;

	smp_mb();

	asm volatile (
		      "1:	LNKGETD	%1, [%2]\n"
		      "	CMP	%1, %3\n"
		      "	LNKSETDEQ [%2], %4\n"
		      "	BNE	2f\n"
		      "	DEFR	%0, TXSTAT\n"
		      "	ANDT	%0, %0, #HI(0x3f000000)\n"
		      "	CMPT	%0, #HI(0x02000000)\n"
		      "	BNZ	1b\n"
#ifdef CONFIG_METAG_LNKGET_AROUND_CACHE
		      "	DCACHE	[%2], %0\n"
#endif
		      "2:\n"
		      : "=&d" (temp), "=&d" (retval)
		      : "da" (m), "bd" (old), "da" (new)
		      : "cc"
		      );

	smp_mb();

	return retval;
}

#endif /* __ASM_METAG_CMPXCHG_LNKGET_H */
