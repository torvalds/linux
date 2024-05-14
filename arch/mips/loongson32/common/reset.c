// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/io.h>
#include <linux/pm.h>
#include <linux/sizes.h>
#include <asm/idle.h>
#include <asm/reboot.h>

#include <loongson1.h>

static void __iomem *wdt_reg_base;

static void ls1x_halt(void)
{
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void ls1x_restart(char *command)
{
	__raw_writel(0x1, wdt_reg_base + WDT_EN);
	__raw_writel(0x1, wdt_reg_base + WDT_TIMER);
	__raw_writel(0x1, wdt_reg_base + WDT_SET);

	ls1x_halt();
}

static void ls1x_power_off(void)
{
	ls1x_halt();
}

static int __init ls1x_reboot_setup(void)
{
	wdt_reg_base = ioremap(LS1X_WDT_BASE, (SZ_4 + SZ_8));
	if (!wdt_reg_base)
		panic("Failed to remap watchdog registers");

	_machine_restart = ls1x_restart;
	_machine_halt = ls1x_halt;
	pm_power_off = ls1x_power_off;

	return 0;
}

arch_initcall(ls1x_reboot_setup);
