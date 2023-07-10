// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include "iomap.h"
#include "common.h"
#include "control.h"
#include "prm3xxx.h"

#define TI81XX_PRM_DEVICE_RSTCTRL	0x00a0
#define TI81XX_GLOBAL_RST_COLD		BIT(1)

/**
 * ti81xx_restart - trigger a software restart of the SoC
 * @mode: the "reboot mode", see arch/arm/kernel/{setup,process}.c
 * @cmd: passed from the userspace program rebooting the system (if provided)
 *
 * Resets the SoC.  For @cmd, see the 'reboot' syscall in
 * kernel/sys.c.  No return value.
 *
 * NOTE: Warm reset does not seem to work, may require resetting
 * clocks to bypass mode.
 */
void ti81xx_restart(enum reboot_mode mode, const char *cmd)
{
	omap2_prm_set_mod_reg_bits(TI81XX_GLOBAL_RST_COLD, 0,
				   TI81XX_PRM_DEVICE_RSTCTRL);
	while (1)
		;
}
