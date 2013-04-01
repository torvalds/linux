/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Copyright (C) 2008-2009 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 */

#include <linux/pm.h>
#include <linux/io.h>

#include <asm/reboot.h>

#include <asm/mach-ralink/ralink_regs.h>

/* Reset Control */
#define SYSC_REG_RESET_CTRL     0x034
#define RSTCTL_RESET_SYSTEM     BIT(0)

static void ralink_restart(char *command)
{
	local_irq_disable();
	rt_sysc_w32(RSTCTL_RESET_SYSTEM, SYSC_REG_RESET_CTRL);
	unreachable();
}

static void ralink_halt(void)
{
	local_irq_disable();
	unreachable();
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = ralink_restart;
	_machine_halt = ralink_halt;
	pm_power_off = ralink_halt;

	return 0;
}

arch_initcall(mips_reboot_setup);
