#ifndef _ASM_CRIS_ARCH_SYSTEM_H
#define _ASM_CRIS_ARCH_SYSTEM_H


/* Read the CPU version register. */
static inline unsigned long rdvr(void)
{
	unsigned char vr;

	__asm__ __volatile__ ("move $vr, %0" : "=rm" (vr));
	return vr;
}

#define cris_machine_name "crisv32"

/* Read the user-mode stack pointer. */
static inline unsigned long rdusp(void)
{
	unsigned long usp;

	__asm__ __volatile__ ("move $usp, %0" : "=rm" (usp));
	return usp;
}

/* Read the current stack pointer. */
static inline unsigned long rdsp(void)
{
	unsigned long sp;

	__asm__ __volatile__ ("move.d $sp, %0" : "=rm" (sp));
	return sp;
}

/* Write the user-mode stack pointer. */
#define wrusp(usp) __asm__ __volatile__ ("move %0, $usp" : : "rm" (usp))

#endif /* _ASM_CRIS_ARCH_SYSTEM_H */
