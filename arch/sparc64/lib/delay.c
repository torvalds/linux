/* delay.c: Delay loops for sparc64
 *
 * Copyright (C) 2004, 2006 David S. Miller <davem@davemloft.net>
 *
 * Based heavily upon x86 variant which is:
 *	Copyright (C) 1993 Linus Torvalds
 *	Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/delay.h>
#include <asm/timer.h>

void __delay(unsigned long loops)
{
	unsigned long bclock, now;
	
	bclock = tick_ops->get_tick();
	do {
		now = tick_ops->get_tick();
	} while ((now-bclock) < loops);
}

/* We used to multiply by HZ after shifting down by 32 bits
 * but that runs into problems for higher values of HZ and
 * slow cpus.
 */
void __const_udelay(unsigned long n)
{
	n *= 4;

	n *= (cpu_data(raw_smp_processor_id()).udelay_val * (HZ/4));
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
