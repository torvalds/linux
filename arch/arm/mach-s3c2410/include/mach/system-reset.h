/* arch/arm/mach-s3c2410/include/mach/system-reset.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - System define for arch_reset() function
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <mach/hardware.h>
#include <plat/watchdog-reset.h>

extern void (*s3c24xx_reset_hook)(void);

static void
arch_reset(char mode, const char *cmd)
{
	if (mode == 's') {
		soft_restart(0);
	}

	if (s3c24xx_reset_hook)
		s3c24xx_reset_hook();

	arch_wdt_reset();

	/* we'll take a jump through zero as a poor second */
	soft_restart(0);
}
