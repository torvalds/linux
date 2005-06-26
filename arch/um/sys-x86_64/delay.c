/*
 * Copyright 2003 PathScale, Inc.
 * Copied from arch/x86_64
 *
 * Licensed under the GPL
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <asm/processor.h>
#include <asm/param.h>

void __delay(unsigned long loops)
{
	unsigned long i;

        for(i = 0; i < loops; i++)
                cpu_relax();
}

void __udelay(unsigned long usecs)
{
	unsigned long i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
        for(i=0;i<n;i++)
                cpu_relax();
}

EXPORT_SYMBOL(__udelay);

void __const_udelay(unsigned long usecs)
{
	unsigned long i, n;

	n = (loops_per_jiffy * HZ * usecs) / MILLION;
        for(i=0;i<n;i++)
                cpu_relax();
}

EXPORT_SYMBOL(__const_udelay);
