#ifndef _ASM_SCORE_DELAY_H
#define _ASM_SCORE_DELAY_H

static inline void __delay(unsigned long loops)
{
	__asm__ __volatile__ (
		"1:\tsubi\t%0,1\n\t"
		"cmpz.c\t%0\n\t"
		"bne\t1b\n\t"
		: "=r" (loops)
		: "0" (loops));
}

static inline void __udelay(unsigned long usecs)
{
	__delay(usecs);
}

#define udelay(usecs) __udelay(usecs)

#endif /* _ASM_SCORE_DELAY_H */
