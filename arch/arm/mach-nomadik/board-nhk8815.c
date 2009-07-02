/*
 *  linux/arch/arm/mach-nomadik/board-8815nhk.c
 *
 *  Copyright (C) STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 *  NHK15 board specifc driver definition
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <mach/setup.h>
#include "clock.h"

#define __MEM_4K_RESOURCE(x) \
	.res = {.start = (x), .end = (x) + SZ_4K - 1, .flags = IORESOURCE_MEM}

static struct amba_device uart0_device = {
	.dev = { .init_name = "uart0" },
	__MEM_4K_RESOURCE(NOMADIK_UART0_BASE),
	.irq = {IRQ_UART0, NO_IRQ},
};

static struct amba_device uart1_device = {
	.dev = { .init_name = "uart1" },
	__MEM_4K_RESOURCE(NOMADIK_UART1_BASE),
	.irq = {IRQ_UART1, NO_IRQ},
};

static struct amba_device *amba_devs[] __initdata = {
	&uart0_device,
	&uart1_device,
};

/* We have a fixed clock alone, by now */
static struct clk nhk8815_clk_48 = {
	.rate = 48*1000*1000,
};

static struct platform_device *nhk8815_platform_devices[] __initdata = {
	/* currently empty, will add keypad, touchscreen etc */
};

static void __init nhk8815_platform_init(void)
{
	int i;

	cpu8815_platform_init();
	platform_add_devices(nhk8815_platform_devices,
			     ARRAY_SIZE(nhk8815_platform_devices));

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		nmdk_clk_create(&nhk8815_clk_48, amba_devs[i]->dev.init_name);
		amba_device_register(amba_devs[i], &iomem_resource);
	}
}

MACHINE_START(NOMADIK, "NHK8815")
	/* Maintainer: ST MicroElectronics */
	.phys_io	= NOMADIK_UART0_BASE,
	.io_pg_offst	= (IO_ADDRESS(NOMADIK_UART0_BASE) >> 18) & 0xfffc,
	.boot_params	= 0x100,
	.map_io		= cpu8815_map_io,
	.init_irq	= cpu8815_init_irq,
	.timer		= &nomadik_timer,
	.init_machine	= nhk8815_platform_init,
MACHINE_END
