/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 * Copyright (C) 2009 Lemote, Inc. & Institute of Computing Technology
 * Author: Zhangjin Wu, wuzj@lemote.com
 */
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/io.h>

#include <asm/reboot.h>
#include <asm/mips-boards/bonito64.h>

static void loongson2e_restart(char *command)
{
	/* do preparation for reboot */
	BONITO_BONGENCFG &= ~(1 << 2);
	BONITO_BONGENCFG |= (1 << 2);

	/* reboot via jumping to boot base address */
	((void (*)(void))ioremap_nocache(BONITO_BOOT_BASE, 4)) ();
}

static void loongson2e_halt(void)
{
	while (1) ;
}

static int __init mips_reboot_setup(void)
{
	_machine_restart = loongson2e_restart;
	_machine_halt = loongson2e_halt;
	pm_power_off = loongson2e_halt;

	return 0;
}

arch_initcall(mips_reboot_setup);
