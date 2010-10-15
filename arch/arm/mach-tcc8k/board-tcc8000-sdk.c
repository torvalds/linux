/*
 * Copyright (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/clock.h>

#include "common.h"

#define XI_FREQUENCY	12000000
#define XTI_FREQUENCY	32768

#ifdef CONFIG_MTD_NAND_TCC
/* NAND */
static struct tcc_nand_platform_data tcc8k_sdk_nand_data = {
	.width = 1,
	.hw_ecc = 0,
};
#endif

static void __init tcc8k_init(void)
{
#ifdef CONFIG_MTD_NAND_TCC
	tcc_nand_device.dev.platform_data = &tcc8k_sdk_nand_data;
	platform_device_register(&tcc_nand_device);
#endif
}

static void __init tcc8k_init_timer(void)
{
	tcc_clocks_init(XI_FREQUENCY, XTI_FREQUENCY);
}

static struct sys_timer tcc8k_timer = {
	.init	= tcc8k_init_timer,
};

static void __init tcc8k_map_io(void)
{
	tcc8k_map_common_io();
}

MACHINE_START(TCC8000_SDK, "Telechips TCC8000-SDK Demo Board")
	.boot_params	= PHYS_OFFSET + 0x00000100,
	.map_io		= tcc8k_map_io,
	.init_irq	= tcc8k_init_irq,
	.init_machine	= tcc8k_init,
	.timer		= &tcc8k_timer,
MACHINE_END
