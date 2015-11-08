/*
 * omap3-restart.c - Code common to all OMAP3xxx machines.
 *
 * Copyright (C) 2009, 2012 Texas Instruments
 * Copyright (C) 2010 Nokia Corporation
 * Tony Lindgren <tony@atomide.com>
 * Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include "common.h"
#include "control.h"
#include "prm.h"

/* Global address base setup code */

/**
 * omap3xxx_restart - trigger a software restart of the SoC
 * @mode: the "reboot mode", see arch/arm/kernel/{setup,process}.c
 * @cmd: passed from the userspace program rebooting the system (if provided)
 *
 * Resets the SoC.  For @cmd, see the 'reboot' syscall in
 * kernel/sys.c.  No return value.
 */
void omap3xxx_restart(enum reboot_mode mode, const char *cmd)
{
	omap3_ctrl_write_boot_mode((cmd ? (u8)*cmd : 0));
	omap_prm_reset_system();
}
