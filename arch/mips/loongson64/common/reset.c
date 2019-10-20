// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Zhangjin Wu, wuzhangjin@gmail.com
 */
#include <linux/init.h>
#include <linux/pm.h>

#include <asm/idle.h>
#include <asm/reboot.h>

#include <loongson.h>
#include <boot_param.h>

static inline void loongson_reboot(void)
{
	((void (*)(void))ioremap_nocache(LOONGSON_BOOT_BASE, 4)) ();
}

static void loongson_restart(char *command)
{

	void (*fw_restart)(void) = (void *)loongson_sysconf.restart_addr;

	fw_restart();
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_poweroff(void)
{
	void (*fw_poweroff)(void) = (void *)loongson_sysconf.poweroff_addr;

	fw_poweroff();
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static void loongson_halt(void)
{
	pr_notice("\n\n** You can safely turn off the power now **\n\n");
	while (1) {
		if (cpu_wait)
			cpu_wait();
	}
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = loongson_restart;
	_machine_halt = loongson_halt;
	pm_power_off = loongson_poweroff;

	return 0;
}

arch_initcall(mips_reboot_setup);
