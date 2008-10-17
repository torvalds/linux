/* arch/arm/mach-s3c2410/include/mach/system.h
 *
 * Copyright (c) 2003 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - System function defines and includes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/io.h>
#include <mach/hardware.h>

#include <mach/map.h>
#include <mach/idle.h>
#include <mach/reset.h>

#include <mach/regs-clock.h>

void (*s3c24xx_idle)(void);
void (*s3c24xx_reset_hook)(void);

void s3c24xx_default_idle(void)
{
	unsigned long tmp;
	int i;

	/* idle the system by using the idle mode which will wait for an
	 * interrupt to happen before restarting the system.
	 */

	/* Warning: going into idle state upsets jtag scanning */

	__raw_writel(__raw_readl(S3C2410_CLKCON) | S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);

	/* the samsung port seems to do a loop and then unset idle.. */
	for (i = 0; i < 50; i++) {
		tmp += __raw_readl(S3C2410_CLKCON); /* ensure loop not optimised out */
	}

	/* this bit is not cleared on re-start... */

	__raw_writel(__raw_readl(S3C2410_CLKCON) & ~S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);
}

static void arch_idle(void)
{
	if (s3c24xx_idle != NULL)
		(s3c24xx_idle)();
	else
		s3c24xx_default_idle();
}

#include <mach/system-reset.h>
