/*
 *  arch/s390/kernel/delay.c
 *    Precise Delay Loops for S390
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 *  Derived from "arch/i386/lib/delay.c"
 *    Copyright (C) 1993 Linus Torvalds
 *    Copyright (C) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>

#ifdef CONFIG_SMP
#include <asm/smp.h>
#endif

void __delay(unsigned long loops)
{
        /*
         * To end the bloody studid and useless discussion about the
         * BogoMips number I took the liberty to define the __delay
         * function in a way that that resulting BogoMips number will
         * yield the megahertz number of the cpu. The important function
         * is udelay and that is done using the tod clock. -- martin.
         */
        __asm__ __volatile__(
                "0: brct %0,0b"
                : /* no outputs */ : "r" (loops/2) );
}

/*
 * Waits for 'usecs' microseconds using the tod clock, giving up the time slice
 * of the virtual PU inbetween to avoid congestion.
 */
void __udelay(unsigned long usecs)
{
        uint64_t start_cc, end_cc;

        if (usecs == 0)
                return;
        asm volatile ("STCK %0" : "=m" (start_cc));
        do {
		cpu_relax();
                asm volatile ("STCK %0" : "=m" (end_cc));
        } while (((end_cc - start_cc)/4096) < usecs);
}
