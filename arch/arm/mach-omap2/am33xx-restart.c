// SPDX-License-Identifier: GPL-2.0-only
/*
 * am33xx-restart.c - Code common to all AM33xx machines.
 */
#include <dt-bindings/pinctrl/am33xx.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/reboot.h>

#include "common.h"
#include "control.h"
#include "prm.h"

/*
 * Advisory 1.0.36 EMU0 and EMU1: Terminals Must be Pulled High Before
 * ICEPick Samples
 *
 * If EMU0/EMU1 pins have been used as GPIO outputs and actively driving low
 * level, the device might not reboot in normal mode. We are in a bad position
 * to override GPIO state here, so just switch the pins into EMU input mode
 * (that's what reset will do anyway) and wait a bit, because the state will be
 * latched 190 ns after reset.
 */
static void am33xx_advisory_1_0_36(void)
{
	u32 emu0 = omap_ctrl_readl(AM335X_PIN_EMU0);
	u32 emu1 = omap_ctrl_readl(AM335X_PIN_EMU1);

	/* If both pins are in EMU mode, nothing to do */
	if (!(emu0 & 7) && !(emu1 & 7))
		return;

	/* Switch GPIO3_7/GPIO3_8 into EMU0/EMU1 modes respectively */
	omap_ctrl_writel(emu0 & ~7, AM335X_PIN_EMU0);
	omap_ctrl_writel(emu1 & ~7, AM335X_PIN_EMU1);

	/*
	 * Give pull-ups time to load the pin/PCB trace capacity.
	 * 5 ms shall be enough to load 1 uF (would be huge capacity for these
	 * pins) with TI-recommended 4k7 external pull-ups.
	 */
	mdelay(5);
}

/**
 * am33xx_restart - trigger a software restart of the SoC
 * @mode: the "reboot mode", see arch/arm/kernel/{setup,process}.c
 * @cmd: passed from the userspace program rebooting the system (if provided)
 *
 * Resets the SoC.  For @cmd, see the 'reboot' syscall in
 * kernel/sys.c.  No return value.
 */
void am33xx_restart(enum reboot_mode mode, const char *cmd)
{
	am33xx_advisory_1_0_36();

	/* TODO: Handle cmd if necessary */
	prm_reboot_mode = mode;

	omap_prm_reset_system();
}
