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
#include <linux/interrupt.h>
#include <linux/gpio.h>
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

static struct resource nhk8815_eth_resources[] = {
	{
		.name = "smc91x-regs",
		.start = 0x34000000 + 0x300,
		.end = 0x34000000 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = NOMADIK_GPIO_TO_IRQ(115),
		.end = NOMADIK_GPIO_TO_IRQ(115),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_RISING,
	}
};

static struct platform_device nhk8815_eth_device = {
	.name = "smc91x",
	.resource = nhk8815_eth_resources,
	.num_resources = ARRAY_SIZE(nhk8815_eth_resources),
};

static int __init nhk8815_eth_init(void)
{
	int gpio_nr = 115; /* hardwired in the board */
	int err;

	err = gpio_request(gpio_nr, "eth_irq");
	if (!err) err = nmk_gpio_set_mode(gpio_nr, NMK_GPIO_ALT_GPIO);
	if (!err) err = gpio_direction_input(gpio_nr);
	if (err)
		pr_err("Error %i in %s\n", err, __func__);
	return err;
}
device_initcall(nhk8815_eth_init);

static struct platform_device *nhk8815_platform_devices[] __initdata = {
	&nhk8815_eth_device,
	/* will add more devices */
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
