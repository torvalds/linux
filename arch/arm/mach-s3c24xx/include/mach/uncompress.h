/* arch/arm/mach-s3c2410/include/mach/uncompress.h
 *
 * Copyright (c) 2003-2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - uncompress code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <mach/regs-gpio.h>
#include <mach/map.h>

/* working in physical space... */
#undef S3C2410_GPIOREG
#define S3C2410_GPIOREG(x) ((S3C24XX_PA_GPIO + (x)))

#include <plat/uncompress.h>

static inline int is_arm926(void)
{
	unsigned int cpuid;

	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r" (cpuid));

	return ((cpuid & 0xff0) == 0x260);
}

static void arch_detect_cpu(void)
{
	unsigned int cpuid;

	cpuid = *((volatile unsigned int *)S3C2410_GSTATUS1);
	cpuid &= S3C2410_GSTATUS1_IDMASK;

	if (is_arm926() || cpuid == S3C2410_GSTATUS1_2440 ||
	    cpuid == S3C2410_GSTATUS1_2442 ||
	    cpuid == S3C2410_GSTATUS1_2416 ||
	    cpuid == S3C2410_GSTATUS1_2450) {
		fifo_mask = S3C2440_UFSTAT_TXMASK;
		fifo_max = 63 << S3C2440_UFSTAT_TXSHIFT;
	} else {
		fifo_mask = S3C2410_UFSTAT_TXMASK;
		fifo_max = 15 << S3C2410_UFSTAT_TXSHIFT;
	}
}

#endif /* __ASM_ARCH_UNCOMPRESS_H */
