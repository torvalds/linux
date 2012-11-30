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

#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>

#include "gpmc.h"
#include "gpmc-smsc911x.h"

#include "board-zoom.h"

#include "soc.h"
#include "common.h"

#define ZOOM_SMSC911X_CS	7
#define ZOOM_SMSC911X_GPIO	158
#define ZOOM_QUADUART_CS	3
#define ZOOM_QUADUART_GPIO	102
#define ZOOM_QUADUART_RST_GPIO	152
#define QUART_CLK		1843200
#define DEBUG_BASE		0x08000000
#define ZOOM_ETHR_START	DEBUG_BASE

static struct omap_smsc911x_platform_data zoom_smsc911x_cfg = {
	.cs             = ZOOM_SMSC911X_CS,
	.gpio_irq       = ZOOM_SMSC911X_GPIO,
	.gpio_reset     = -EINVAL,
	.flags		= SMSC911X_USE_32BIT,
};

static inline void __init zoom_init_smsc911x(void)
{
	gpmc_smsc911x_init(&zoom_smsc911x_cfg);
}

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.mapbase	= ZOOM_UART_BASE,
		.flags		= UPF_BOOT_AUTOCONF|UPF_IOREMAP|UPF_SHARE_IRQ,
		.irqflags	= IRQF_SHARED | IRQF_TRIGGER_RISING,
		.iotype		= UPIO_MEM,
		.regshift	= 1,
		.uartclk	= QUART_CLK,
	}, {
		.flags		= 0
	}
};

static struct platform_device zoom_debugboard_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

static inline void __init zoom_init_quaduart(void)
{
	int quart_cs;
	unsigned long cs_mem_base;
	int quart_gpio = 0;

	if (gpio_request_one(ZOOM_QUADUART_RST_GPIO,
				GPIOF_OUT_INIT_LOW,
				"TL16CP754C GPIO") < 0) {
		pr_err("Failed to request GPIO%d for TL16CP754C\n",
			ZOOM_QUADUART_RST_GPIO);
		return;
	}

	quart_cs = ZOOM_QUADUART_CS;

	if (gpmc_cs_request(quart_cs, SZ_1M, &cs_mem_base) < 0) {
		pr_err("Failed to request GPMC mem for Quad UART(TL16CP754C)\n");
		return;
	}

	quart_gpio = ZOOM_QUADUART_GPIO;

	if (gpio_request_one(quart_gpio, GPIOF_IN, "TL16CP754C GPIO") < 0)
		printk(KERN_ERR "Failed to request GPIO%d for TL16CP754C\n",
								quart_gpio);

	serial_platform_data[0].irq = gpio_to_irq(102);
}

static inline int omap_zoom_debugboard_detect(void)
{
	int debug_board_detect = 0;
	int ret = 1;

	debug_board_detect = ZOOM_SMSC911X_GPIO;

	if (gpio_request_one(debug_board_detect, GPIOF_IN,
			     "Zoom debug board detect") < 0) {
		pr_err("Failed to request GPIO%d for Zoom debug board detect\n",
		       debug_board_detect);
		return 0;
	}

	if (!gpio_get_value(debug_board_detect)) {
		ret = 0;
	}
	gpio_free(debug_board_detect);
	return ret;
}

static struct platform_device *zoom_devices[] __initdata = {
	&zoom_debugboard_serial_device,
};

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

int __init zoom_debugboard_init(void)
{
	if (!omap_zoom_debugboard_detect())
		return 0;

	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));
	zoom_init_smsc911x();
	zoom_init_quaduart();
	return platform_add_devices(zoom_devices, ARRAY_SIZE(zoom_devices));
}
