/*
 * Modified from board-generic.c
 *
 * Copyright (C) 2004 2N Telekomunikace, Ladislav Michl <michl@2n.cz>
 *
 * Code for Netstar OMAP board.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/common.h>

extern void __init omap_init_time(void);
extern int omap_gpio_init(void);

static struct resource netstar_smc91x_resources[] = {
	[0] = {
		.start	= OMAP_CS1_PHYS + 0x300,
		.end	= OMAP_CS1_PHYS + 0x300 + 16,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(8),
		.end	= OMAP_GPIO_IRQ(8),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device netstar_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(netstar_smc91x_resources),
	.resource	= netstar_smc91x_resources,
};

static struct platform_device *netstar_devices[] __initdata = {
	&netstar_smc91x_device,
};

static struct omap_uart_config netstar_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel netstar_config[] = {
	{ OMAP_TAG_UART,	&netstar_uart_config },
};

static void __init netstar_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static void __init netstar_init(void)
{
	/* green LED */
	omap_request_gpio(4);
	omap_set_gpio_direction(4, 0);
	/* smc91x reset */
	omap_request_gpio(7);
	omap_set_gpio_direction(7, 0);
	omap_set_gpio_dataout(7, 1);
	udelay(2);	/* wait at least 100ns */
	omap_set_gpio_dataout(7, 0);
	mdelay(50);	/* 50ms until PHY ready */
	/* smc91x interrupt pin */
	omap_request_gpio(8);

	omap_request_gpio(12);
	omap_request_gpio(13);
	omap_request_gpio(14);
	omap_request_gpio(15);
	set_irq_type(OMAP_GPIO_IRQ(12), IRQT_FALLING);
	set_irq_type(OMAP_GPIO_IRQ(13), IRQT_FALLING);
	set_irq_type(OMAP_GPIO_IRQ(14), IRQT_FALLING);
	set_irq_type(OMAP_GPIO_IRQ(15), IRQT_FALLING);

	platform_add_devices(netstar_devices, ARRAY_SIZE(netstar_devices));

	/* Switch on green LED */
	omap_set_gpio_dataout(4, 0);
	/* Switch off red LED */
	omap_writeb(0x00, OMAP_LPG1_PMR);	/* Disable clock */
	omap_writeb(0x80, OMAP_LPG1_LCR);

	omap_board_config = netstar_config;
	omap_board_config_size = ARRAY_SIZE(netstar_config);
	omap_serial_init();
}

static void __init netstar_map_io(void)
{
	omap1_map_common_io();
}

#define MACHINE_PANICED		1
#define MACHINE_REBOOTING	2
#define MACHINE_REBOOT		4
static unsigned long machine_state;

static int panic_event(struct notifier_block *this, unsigned long event,
	 void *ptr)
{
	if (test_and_set_bit(MACHINE_PANICED, &machine_state))
		return NOTIFY_DONE;

	/* Switch off green LED */
	omap_set_gpio_dataout(4, 1);
	/* Flash red LED */
	omap_writeb(0x78, OMAP_LPG1_LCR);
	omap_writeb(0x01, OMAP_LPG1_PMR);	/* Enable clock */

	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
};

static int __init netstar_late_init(void)
{
	/* TODO: Setup front panel switch here */

	/* Setup panic notifier */
	notifier_chain_register(&panic_notifier_list, &panic_block);

	return 0;
}

postcore_initcall(netstar_late_init);

MACHINE_START(NETSTAR, "NetStar OMAP5910")
	/* Maintainer: Ladislav Michl <michl@2n.cz> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= netstar_map_io,
	.init_irq	= netstar_init_irq,
	.init_machine	= netstar_init,
	.timer		= &omap_timer,
MACHINE_END
