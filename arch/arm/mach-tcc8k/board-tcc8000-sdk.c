/*
 * Copyright (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

#include <mach/clock.h>
#include <mach/tcc-nand.h>
#include <mach/tcc8k-regs.h>

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

	/* set PLL0 clock to 96MHz, adapt UART0 divisor */
	__raw_writel(0x00026003, CKC_BASE + PLL0CFG_OFFS);
	__raw_writel(0x10000001, CKC_BASE + ACLKUART0_OFFS);

	/* set PLL1 clock to 192MHz */
	__raw_writel(0x00016003, CKC_BASE + PLL1CFG_OFFS);

	/* set PLL2 clock to 48MHz */
	__raw_writel(0x00036003, CKC_BASE + PLL2CFG_OFFS);

	/* with CPU freq higher than 150 MHz, need extra DTCM wait */
	__raw_writel(0x00000001, SCFG_BASE + DTCMWAIT_OFFS);

	/* PLL locking time as specified */
	udelay(300);
}

MACHINE_START(TCC8000_SDK, "Telechips TCC8000-SDK Demo Board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x00000100,
	.map_io		= tcc8k_map_io,
	.init_irq	= tcc8k_init_irq,
	.init_machine	= tcc8k_init,
	.timer		= &tcc8k_timer,
MACHINE_END
