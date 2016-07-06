#ifndef _ASM_HASH_H
#define _ASM_HASH_H

/*
 * The later H8SX models have a 32x32-bit multiply, but the H8/300H
 * and H8S have only 16x16->32.  Since it's tolerably compact, this is
 * basically an inlined version of the __mulsi3 code.  Since the inputs
 * are not expected to be small, it's also simplfied by skipping the
 * early-out checks.
 *
 * (Since neither CPU has any multi-bit shift instructions, a
 * shift-and-add version is a non-starter.)
 *
 * TODO: come up with an arch-specific version of the hashing in fs/namei.c,
 * since that is heavily dependent on rotates.  Which, as mentioned, suck
 * horribly on H8.
 */

#if defined(CONFIG_CPU_H300H) || defined(CONFIG_CPU_H8S)

#define HAVE_ARCH__HASH_32 1

/*
 * Multiply by k = 0x61C88647.  Fitting this into three registers requires
 * one extra instruction, but reducing register pressure will probably
 * make that back and then some.
 *
 * GCC asm note: %e1 is the high half of operand %1, while %f1 is the
 * low half.  So if %1 is er4, then %e1 is e4 and %f1 is r4.
 *
 * This has been designed to modify x in place, since that's the most
 * common usage, but preserve k, since hash_64() makes two calls in
 * quick succession.
 */
static inline u32 __attribute_const__ __hash_32(u32 x)
{
	u32 temp;

	asm(   "mov.w	%e1,%f0"
	"\n	mulxu.w	%f2,%0"		/* klow * xhigh */
	"\n	mov.w	%f0,%e1"	/* The extra instruction */
	"\n	mov.w	%f1,%f0"
	"\n	mulxu.w	%e2,%0"		/* khigh * xlow */
	"\n	add.w	%e1,%f0"
	"\n	mulxu.w	%f2,%1"		/* klow * xlow */
	"\n	add.w	%f0,%e1"
	: "=&r" (temp), "=r" (x)
	: "%r" (GOLDEN_RATIO_32), "1" (x));
	return x;
}

#endif
#endif /* _ASM_HASH_H */
