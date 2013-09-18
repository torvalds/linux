/*
 * omap4-restart.c - Common to OMAP4 and OMAP5
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/reboot.h>
#include "prminst44xx.h"

/**
 * omap44xx_restart - trigger a software restart of the SoC
 * @mode: the "reboot mode", see arch/arm/kernel/{setup,process}.c
 * @cmd: passed from the userspace program rebooting the system (if provided)
 *
 * Resets the SoC.  For @cmd, see the 'reboot' syscall in
 * kernel/sys.c.  No return value.
 */
void omap44xx_restart(enum reboot_mode mode, const char *cmd)
{
	/* XXX Should save 'cmd' into scratchpad for use after reboot */
	omap4_prminst_global_warm_sw_reset(); /* never returns */
	while (1)
		;
}
