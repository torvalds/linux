/*
 * psr.h: This file holds the macros for masking off various parts of
 *        the processor status register on the Sparc. This is valid
 *        for Version 8. On the V9 this is renamed to the PSTATE
 *        register and its members are accessed as fields like
 *        PSTATE.PRIV for the current CPU privilege level.
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __LINUX_SPARC_PSR_H
#define __LINUX_SPARC_PSR_H

/* The Sparc PSR fields are laid out as the following:
 *
 *  ------------------------------------------------------------------------
 *  | impl  | vers  | icc   | resv  | EC | EF | PIL  | S | PS | ET |  CWP  |
 *  | 31-28 | 27-24 | 23-20 | 19-14 | 13 | 12 | 11-8 | 7 | 6  | 5  |  4-0  |
 *  ------------------------------------------------------------------------
 */
#define PSR_CWP     0x0000001f         /* current window pointer     */
#define PSR_ET      0x00000020         /* enable traps field         */
#define PSR_PS      0x00000040         /* previous privilege level   */
#define PSR_S       0x00000080         /* current privilege level    */
#define PSR_PIL     0x00000f00         /* processor interrupt level  */
#define PSR_EF      0x00001000         /* enable floating point      */
#define PSR_EC      0x00002000         /* enable co-processor        */
#define PSR_SYSCALL 0x00004000         /* inside of a syscall        */
#define PSR_LE      0x00008000         /* SuperSparcII little-endian */
#define PSR_ICC     0x00f00000         /* integer condition codes    */
#define PSR_C       0x00100000         /* carry bit                  */
#define PSR_V       0x00200000         /* overflow bit               */
#define PSR_Z       0x00400000         /* zero bit                   */
#define PSR_N       0x00800000         /* negative bit               */
#define PSR_VERS    0x0f000000         /* cpu-version field          */
#define PSR_IMPL    0xf0000000         /* cpu-implementation field   */

#define PSR_IMPL_SHIFT		28
#define PSR_IMPL_SHIFTED_MASK	0xf

#define PSR_IMPL_TI		0x4
#define PSR_IMPL_LEON		0xf

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
/* Get the %psr register. */
static inline unsigned int get_psr(void)
{
	unsigned int psr;
	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
	: "=r" (psr)
	: /* no inputs */
	: "memory");

	return psr;
}

static inline void put_psr(unsigned int new_psr)
{
	__asm__ __volatile__(
		"wr	%0, 0x0, %%psr\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
	: /* no outputs */
	: "r" (new_psr)
	: "memory", "cc");
}

/* Get the %fsr register.  Be careful, make sure the floating point
 * enable bit is set in the %psr when you execute this or you will
 * incur a trap.
 */

extern unsigned int fsr_storage;

static inline unsigned int get_fsr(void)
{
	unsigned int fsr = 0;

	__asm__ __volatile__(
		"st	%%fsr, %1\n\t"
		"ld	%1, %0\n\t"
	: "=r" (fsr)
	: "m" (fsr_storage));

	return fsr;
}

#endif /* !(__ASSEMBLY__) */

#endif /* (__KERNEL__) */

#endif /* !(__LINUX_SPARC_PSR_H) */
