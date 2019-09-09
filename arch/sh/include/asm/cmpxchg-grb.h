/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_CMPXCHG_GRB_H
#define __ASM_SH_CMPXCHG_GRB_H

static inline unsigned long xchg_u32(volatile u32 *m, unsigned long val)
{
	unsigned long retval;

	__asm__ __volatile__ (
		"   .align 2              \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
		"   nop                   \n\t"
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov    #-4,   r15     \n\t" /* LOGIN */
		"   mov.l  @%1,   %0      \n\t" /* load  old value */
		"   mov.l   %2,   @%1     \n\t" /* store new value */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (retval),
		  "+r"  (m),
		  "+r"  (val)		/* inhibit r15 overloading */
		:
		: "memory", "r0", "r1");

	return retval;
}

static inline unsigned long xchg_u16(volatile u16 *m, unsigned long val)
{
	unsigned long retval;

	__asm__ __volatile__ (
		"   .align  2             \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov    #-6,   r15     \n\t" /* LOGIN */
		"   mov.w  @%1,   %0      \n\t" /* load  old value */
		"   extu.w  %0,   %0      \n\t" /* extend as unsigned */
		"   mov.w   %2,   @%1     \n\t" /* store new value */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (retval),
		  "+r"  (m),
		  "+r"  (val)		/* inhibit r15 overloading */
		:
		: "memory" , "r0", "r1");

	return retval;
}

static inline unsigned long xchg_u8(volatile u8 *m, unsigned long val)
{
	unsigned long retval;

	__asm__ __volatile__ (
		"   .align  2             \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov    #-6,   r15     \n\t" /* LOGIN */
		"   mov.b  @%1,   %0      \n\t" /* load  old value */
		"   extu.b  %0,   %0      \n\t" /* extend as unsigned */
		"   mov.b   %2,   @%1     \n\t" /* store new value */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (retval),
		  "+r"  (m),
		  "+r"  (val)		/* inhibit r15 overloading */
		:
		: "memory" , "r0", "r1");

	return retval;
}

static inline unsigned long __cmpxchg_u32(volatile int *m, unsigned long old,
					  unsigned long new)
{
	unsigned long retval;

	__asm__ __volatile__ (
		"   .align  2             \n\t"
		"   mova    1f,   r0      \n\t" /* r0 = end point */
		"   nop                   \n\t"
		"   mov    r15,   r1      \n\t" /* r1 = saved sp */
		"   mov    #-8,   r15     \n\t" /* LOGIN */
		"   mov.l  @%3,   %0      \n\t" /* load  old value */
		"   cmp/eq  %0,   %1      \n\t"
		"   bf            1f      \n\t" /* if not equal */
		"   mov.l   %2,   @%3     \n\t" /* store new value */
		"1: mov     r1,   r15     \n\t" /* LOGOUT */
		: "=&r" (retval),
		  "+r"  (old), "+r"  (new) /* old or new can be r15 */
		:  "r"  (m)
		: "memory" , "r0", "r1", "t");

	return retval;
}

#endif /* __ASM_SH_CMPXCHG_GRB_H */
