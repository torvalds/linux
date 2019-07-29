// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Technology Europe RSK+ 7203 Support.
 *
 * Copyright (C) 2008 - 2010  Paul Mundt
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/smsc911x.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <cpu/sh7203.h>

static struct smsc911x_platform_config smsc911x_config = {
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SWAP_FIFO,
};

static struct resource smsc911x_resources[] = {
	[0] = {
		.start		= 0x24000000,
		.end		= 0x240000ff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= 64,
		.end		= 64,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &smsc911x_config,
	},
};

static struct gpio_led rsk7203_gpio_leds[] = {
	{
		.name			= "green",
		.gpio			= GPIO_PE10,
		.active_low		= 1,
	}, {
		.name			= "orange",
		.default_trigger	= "nand-disk",
		.gpio			= GPIO_PE12,
		.active_low		= 1,
	}, {
		.name			= "red:timer",
		.default_trigger	= "timer",
		.gpio			= GPIO_PC14,
		.active_low		= 1,
	}, {
		.name			= "red:heartbeat",
		.default_trigger	= "heartbeat",
		.gpio			= GPIO_PE11,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data rsk7203_gpio_leds_info = {
	.leds		= rsk7203_gpio_leds,
	.num_leds	= ARRAY_SIZE(rsk7203_gpio_leds),
};

static struct platform_device led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &rsk7203_gpio_leds_info,
	},
};

static struct gpio_keys_button rsk7203_gpio_keys_table[] = {
	{
		.code		= BTN_0,
		.gpio		= GPIO_PB0,
		.active_low	= 1,
		.desc		= "SW1",
	}, {
		.code		= BTN_1,
		.gpio		= GPIO_PB1,
		.active_low	= 1,
		.desc		= "SW2",
	}, {
		.code		= BTN_2,
		.gpio		= GPIO_PB2,
		.active_low	= 1,
		.desc		= "SW3",
	},
};

static struct gpio_keys_platform_data rsk7203_gpio_keys_info = {
	.buttons	= rsk7203_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(rsk7203_gpio_keys_table),
	.poll_interval	= 50, /* default to 50ms */
};

static struct platform_device keys_device = {
	.name		= "gpio-keys-polled",
	.dev		= {
		.platform_data	= &rsk7203_gpio_keys_info,
	},
};

static struct platform_device *rsk7203_devices[] __initdata = {
	&smsc911x_device,
	&led_device,
	&keys_device,
};

static int __init rsk7203_devices_setup(void)
{
	/* Select pins for SCIF0 */
	gpio_request(GPIO_FN_TXD0, NULL);
	gpio_request(GPIO_FN_RXD0, NULL);

	/* Setup LAN9118: CS1 in 16-bit Big Endian Mode, IRQ0 at Port B */
	__raw_writel(0x36db0400, 0xfffc0008); /* CS1BCR */
	gpio_request(GPIO_FN_IRQ0_PB, NULL);

	return platform_add_devices(rsk7203_devices,
				    ARRAY_SIZE(rsk7203_devices));
}
device_initcall(rsk7203_devices_setup);
