/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/io.h>
#include <linux/pm.h>
#include <asm/reboot.h>

#include <loongson1.h>

static void ls1x_restart(char *command)
{
	__raw_writel(0x1, LS1X_WDT_EN);
	__raw_writel(0x5000000, LS1X_WDT_TIMER);
	__raw_writel(0x1, LS1X_WDT_SET);
}

static void ls1x_halt(void)
{
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void ls1x_power_off(void)
{
	ls1x_halt();
}

static int __init ls1x_reboot_setup(void)
{
	_machine_restart = ls1x_restart;
	_machine_halt = ls1x_halt;
	pm_power_off = ls1x_power_off;

	return 0;
}

arch_initcall(ls1x_reboot_setup);
