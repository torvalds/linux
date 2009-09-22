/*
 * Copyright (C) 2009 Texas Instruments Inc.
 * Mikkel Christensen <mlc@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/serial_8250.h>
#include <linux/smsc911x.h>
#include <linux/interrupt.h>

#include <mach/gpmc.h>

#define ZOOM2_SMSC911X_CS	7
#define ZOOM2_SMSC911X_GPIO	158
#define ZOOM2_QUADUART_CS	3
#define ZOOM2_QUADUART_GPIO	102
#define QUART_CLK		1843200
#define DEBUG_BASE		0x08000000
#define ZOOM2_ETHR_START	DEBUG_BASE

static struct resource zoom2_smsc911x_resources[] = {
	[0] = {
		.start	= ZOOM2_ETHR_START,
		.end	= ZOOM2_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config zoom2_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device zoom2_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(zoom2_smsc911x_resources),
	.resource	= zoom2_smsc911x_resources,
	.dev		= {
		.platform_data = &zoom2_smsc911x_config,
	},
};

static inline void __init zoom2_init_smsc911x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	int eth_gpio = 0;

	eth_cs = ZOOM2_SMSC911X_CS;

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smsc911x\n");
		return;
	}

	zoom2_smsc911x_resources[0].start = cs_mem_base + 0x0;
	zoom2_smsc911x_resources[0].end   = cs_mem_base + 0xff;

	eth_gpio = ZOOM2_SMSC911X_GPIO;

	zoom2_smsc911x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (gpio_request(eth_gpio, "smsc911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smsc911x IRQ\n",
				eth_gpio);
		return;
	}
	gpio_direction_input(eth_gpio);
}

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.mapbase	= 0x10000000,
		.irq		= OMAP_GPIO_IRQ(102),
		.flags		= UPF_BOOT_AUTOCONF|UPF_IOREMAP|UPF_SHARE_IRQ,
		.irqflags	= IRQF_SHARED | IRQF_TRIGGER_RISING,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= QUART_CLK,
	}, {
		.flags		= 0
	}
};

static struct platform_device zoom2_debugboard_serial_device = {
	.name			= "serial8250",
	.id			= 3,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

static inline void __init zoom2_init_quaduart(void)
{
	int quart_cs;
	unsigned long cs_mem_base;
	int quart_gpio = 0;

	quart_cs = ZOOM2_QUADUART_CS;

	if (gpmc_cs_request(quart_cs, SZ_1M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem"
				"for Quad UART(TL16CP754C)\n");
		return;
	}

	quart_gpio = ZOOM2_QUADUART_GPIO;

	if (gpio_request(quart_gpio, "TL16CP754C GPIO") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for TL16CP754C\n",
								quart_gpio);
		return;
	}
	gpio_direction_input(quart_gpio);
}

static inline int omap_zoom2_debugboard_detect(void)
{
	int debug_board_detect = 0;
	int ret = 1;

	debug_board_detect = ZOOM2_SMSC911X_GPIO;

	if (gpio_request(debug_board_detect, "Zoom2 debug board detect") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for Zoom2 debug"
		"board detect\n", debug_board_detect);
		return 0;
	}
	gpio_direction_input(debug_board_detect);

	if (!gpio_get_value(debug_board_detect)) {
		ret = 0;
	}
	gpio_free(debug_board_detect);
	return ret;
}

static struct platform_device *zoom2_devices[] __initdata = {
	&zoom2_smsc911x_device,
	&zoom2_debugboard_serial_device,
};

int __init omap_zoom2_debugboard_init(void)
{
	if (!omap_zoom2_debugboard_detect())
		return 0;

	zoom2_init_smsc911x();
	zoom2_init_quaduart();
	return platform_add_devices(zoom2_devices, ARRAY_SIZE(zoom2_devices));
}
