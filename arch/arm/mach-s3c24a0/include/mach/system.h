/* linux/arch/arm/mach-s3c24a0/include/mach/system.h
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24A0 - System function defines and includes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/hardware.h>
#include <asm/io.h>

#include <mach/map.h>

static void arch_idle(void)
{
	/* currently no specific idle support. */
}

void (*s3c24xx_reset_hook)(void);

#include <asm/plat-s3c24xx/system-reset.h>
