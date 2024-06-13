// SPDX-License-Identifier: GPL-2.0-only
/*
 * am33xx-restart.c - Code common to all AM33xx machines.
 */
#include <linux/kernel.h>
#include <linux/reboot.h>

#include "common.h"
#include "prm.h"

/**
 * am3xx_restart - trigger a software restart of the SoC
 * @mode: the "reboot mode", see arch/arm/kernel/{setup,process}.c
 * @cmd: passed from the userspace program rebooting the system (if provided)
 *
 * Resets the SoC.  For @cmd, see the 'reboot' syscall in
 * kernel/sys.c.  No return value.
 */
void am33xx_restart(enum reboot_mode mode, const char *cmd)
{
	/* TODO: Handle mode and cmd if necessary */

	omap_prm_reset_system();
}
