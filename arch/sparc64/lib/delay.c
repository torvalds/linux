/* delay.c: Delay loops for sparc64
 *
 * Copyright (C) 2004 David S. Miller <davem@redhat.com>
 *
 * Based heavily upon x86 variant which is:
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/delay.h>

void __delay(unsigned long loops)
{
	__asm__ __volatile__(
"	b,pt	%%xcc, 1f\n"
"	 cmp	%0, 0\n"
"	.align	32\n"
"1:\n"
"	bne,pt	%%xcc, 1b\n"
"	 subcc	%0, 1, %0\n"
	: "=&r" (loops)
	: "0" (loops)
	: "cc");
}

/* We used to multiply by HZ after shifting down by 32 bits
 * but that runs into problems for higher values of HZ and
 * slow cpus.
 */
void __const_udelay(unsigned long n)
{
	n *= 4;

	n *= (cpu_data(_smp_processor_id()).udelay_val * (HZ/4));
	n >>= 32;

	__delay(n + 1);
}

void __udelay(unsigned long n)
{
	__const_udelay(n * 0x10c7UL);
}


void __ndelay(unsigned long n)
{
	__const_udelay(n * 0x5UL);
}
