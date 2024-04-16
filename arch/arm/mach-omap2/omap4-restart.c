// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap4-restart.c - Common to OMAP4 and OMAP5
 */

#include <linux/types.h>
#include <linux/reboot.h>
#include "common.h"
#include "prm.h"

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
	omap_prm_reset_system();
}
