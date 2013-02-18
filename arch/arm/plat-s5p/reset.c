/* linux/arch/arm/plat-s5p/include/plat/system-reset.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Based on arch/arm/mach-s3c2410/include/mach/system-reset.h
 *
 * S5P - System define for arch_reset()
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <plat/system-reset.h>
#include <plat/watchdog-reset.h>


void (*s5p_reset_hook)(void);

void arch_reset(char mode, const char *cmd)
{
	/* SWRESET support in s5p_reset_hook() */

	if (s5p_reset_hook)
		s5p_reset_hook();

	/* Perform reset using Watchdog reset
	 * if there is no s5p_reset_hook()
	 */

	arch_wdt_reset();
}
