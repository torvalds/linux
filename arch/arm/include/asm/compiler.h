#ifndef __ASM_ARM_COMPILER_H
#define __ASM_ARM_COMPILER_H

/*
 * This is used to ensure the compiler did actually allocate the register we
 * asked it for some inline assembly sequences.  Apparently we can't trust
 * the compiler from one version to another so a bit of paranoia won't hurt.
 * This string is meant to be concatenated with the inline asm string and
 * will cause compilation to stop on mismatch.
 * (for details, see gcc PR 15089)
 */
#define __asmeq(x, y)  ".ifnc " x "," y " ; .err ; .endif\n\t"

#if defined(CONFIG_SMP) && !defined(CONFIG_CPU_V6)
/*
 * Read TPIDRPRW.
 * GCC requires a workaround as it does not treat a "memory" clobber on a
 * non-volatile asm block as a side-effect.
 * We want to allow caching the value, so for GCC avoid using volatile and
 * instead use a fake stack read to hazard against barrier().
 */
#if defined(__clang__)
static inline unsigned long read_TPIDRPRW(void)
{
	unsigned long off;

	/*
	 * Read TPIDRPRW.
	 */
	asm("mrc p15, 0, %0, c13, c0, 4" : "=r" (off) : : "memory");

	return off;
}
#else
static inline unsigned long read_TPIDRPRW(void)
{
	unsigned long off;
	register unsigned long *sp asm ("sp");

	asm("mrc p15, 0, %0, c13, c0, 4" : "=r" (off) : "Q" (*sp));

	return off;
}
#endif
#endif

#endif /* __ASM_ARM_COMPILER_H */
