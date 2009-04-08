/*
 * linux/arch/arm/mach-omap2/board-ldp.c
 *
 * Copyright (C) 2008 Texas Instruments Inc.
 * Nishant Kamat <nskamat@ti.com>
 *
 * Modified from mach-omap2/board-3430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl4030.h>
#include <linux/io.h>
#include <linux/smsc911x.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpmc.h>

#include <asm/delay.h>
#include <mach/control.h>
#include <mach/usb.h>

#include "mmc-twl4030.h"

#define LDP_SMSC911X_CS		1
#define LDP_SMSC911X_GPIO	152
#define DEBUG_BASE		0x08000000
#define LDP_ETHR_START		DEBUG_BASE

static struct resource ldp_smsc911x_resources[] = {
	[0] = {
		.start	= LDP_ETHR_START,
		.end	= LDP_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config ldp_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct platform_device ldp_smsc911x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(ldp_smsc911x_resources),
	.resource	= ldp_smsc911x_resources,
	.dev		= {
		.platform_data = &ldp_smsc911x_config,
	},
};

static struct platform_device *ldp_devices[] __initdata = {
	&ldp_smsc911x_device,
};

static inline void __init ldp_init_smsc911x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	int eth_gpio = 0;

	eth_cs = LDP_SMSC911X_CS;

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smsc911x\n");
		return;
	}

	ldp_smsc911x_resources[0].start = cs_mem_base + 0x0;
	ldp_smsc911x_resources[0].end   = cs_mem_base + 0xff;
	udelay(100);

	eth_gpio = LDP_SMSC911X_GPIO;

	ldp_smsc911x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (gpio_request(eth_gpio, "smsc911x irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smsc911x IRQ\n",
				eth_gpio);
		return;
	}
	gpio_direction_input(eth_gpio);
}

static void __init omap_ldp_init_irq(void)
{
	omap2_init_common_hw(NULL);
	omap_init_irq();
	omap_gpio_init();
	ldp_init_smsc911x();
}

static struct omap_uart_config ldp_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel ldp_config[] __initdata = {
	{ OMAP_TAG_UART,	&ldp_uart_config },
};

static struct twl4030_gpio_platform_data ldp_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_platform_data ldp_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.gpio		= &ldp_gpio_data,
};

static struct i2c_board_info __initdata ldp_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &ldp_twldata,
	},
};

static int __init omap_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, ldp_i2c_boardinfo,
			ARRAY_SIZE(ldp_i2c_boardinfo));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct twl4030_hsmmc_info mmc[] __initdata = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}	/* Terminator */
};

static void __init omap_ldp_init(void)
{
	omap_i2c_init();
	platform_add_devices(ldp_devices, ARRAY_SIZE(ldp_devices));
	omap_board_config = ldp_config;
	omap_board_config_size = ARRAY_SIZE(ldp_config);
	omap_serial_init();
	twl4030_mmc_init(mmc);
	usb_musb_init();
}

static void __init omap_ldp_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_LDP, "OMAP LDP board")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_ldp_map_io,
	.init_irq	= omap_ldp_init_irq,
	.init_machine	= omap_ldp_init,
	.timer		= &omap_timer,
MACHINE_END
