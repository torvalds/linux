/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_DELAY_H
#define _ASM_PARISC_DELAY_H

static __inline__ void __delay(unsigned long loops) {
	asm volatile(
	"	.balignl	64,0x34000034\n"
	"	addib,UV -1,%0,.\n"
	"	nop\n"
		: "=r" (loops) : "0" (loops));
}

extern void __udelay(unsigned long usecs);
extern void __udelay_bad(unsigned long usecs);

static inline void udelay(unsigned long usecs)
{
	if (__builtin_constant_p(usecs) && (usecs) > 20000)
		__udelay_bad(usecs);
	__udelay(usecs);
}

#endif /* _ASM_PARISC_DELAY_H */
