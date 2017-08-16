/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#include <linux/init.h>
#include <linux/pm.h>
#include <asm/reboot.h>
#include <asm/mach-pic32/pic32.h>

#define PIC32_RSWRST		0x10

static void pic32_halt(void)
{
	while (1) {
		__asm__(".set push;\n"
			".set arch=r4000;\n"
			"wait;\n"
			".set pop;\n"
		);
	}
}

static void pic32_machine_restart(char *command)
{
	void __iomem *reg =
		ioremap(PIC32_BASE_RESET + PIC32_RSWRST, sizeof(u32));

	pic32_syskey_unlock();

	/* magic write/read */
	__raw_writel(1, reg);
	(void)__raw_readl(reg);

	pic32_halt();
}

static void pic32_machine_halt(void)
{
	local_irq_disable();

	pic32_halt();
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = pic32_machine_restart;
	_machine_halt = pic32_machine_halt;
	pm_power_off = pic32_machine_halt;

	return 0;
}

arch_initcall(mips_reboot_setup);
