/*
 * linux/arch/arm/mach-omap2/board-apollon.c
 *
 * Copyright (C) 2005,2006 Samsung Electronics
 * Author: Kyungmin Park <kyungmin.park@samsung.com>
 *
 * Modified from mach-omap/omap2/board-h4.c
 *
 * Code for apollon OMAP2 board. Should work on many OMAP2 systems where
 * the bootloader passes the board-specific data to the kernel.
 * Do not put any board specific code to this file; create a new machine
 * type if you need custom low-level initializations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/onenand.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/smc91x.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

#include <plat/led.h>
#include <plat/usb.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>

#include "mux.h"
#include "control.h"

/* LED & Switch macros */
#define LED0_GPIO13		13
#define LED1_GPIO14		14
#define LED2_GPIO15		15
#define SW_ENTER_GPIO16		16
#define SW_UP_GPIO17		17
#define SW_DOWN_GPIO58		58

#define APOLLON_FLASH_CS	0
#define APOLLON_ETH_CS		1
#define APOLLON_ETHR_GPIO_IRQ	74

static struct mtd_partition apollon_partitions[] = {
	{
		.name		= "X-Loader + U-Boot",
		.offset		= 0,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	},
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
	},
	{
		.name		= "rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_16M,
	},
	{
		.name		= "filesystem00",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_32M,
	},
	{
		.name		= "filesystem01",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct onenand_platform_data apollon_flash_data = {
	.parts		= apollon_partitions,
	.nr_parts	= ARRAY_SIZE(apollon_partitions),
};

static struct resource apollon_flash_resource[] = {
	[0] = {
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device apollon_onenand_device = {
	.name		= "onenand-flash",
	.id		= -1,
	.dev		= {
		.platform_data	= &apollon_flash_data,
	},
	.num_resources	= ARRAY_SIZE(apollon_flash_resource),
	.resource	= apollon_flash_resource,
};

static void __init apollon_flash_init(void)
{
	unsigned long base;

	if (gpmc_cs_request(APOLLON_FLASH_CS, SZ_128K, &base) < 0) {
		printk(KERN_ERR "Cannot request OneNAND GPMC CS\n");
		return;
	}
	apollon_flash_resource[0].start = base;
	apollon_flash_resource[0].end   = base + SZ_128K - 1;
}

static struct smc91x_platdata appolon_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource apollon_smc91x_resources[] = {
	[0] = {
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(APOLLON_ETHR_GPIO_IRQ),
		.end	= OMAP_GPIO_IRQ(APOLLON_ETHR_GPIO_IRQ),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
};

static struct platform_device apollon_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.dev	= {
		.platform_data	= &appolon_smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(apollon_smc91x_resources),
	.resource	= apollon_smc91x_resources,
};

static struct platform_device apollon_lcd_device = {
	.name		= "apollon_lcd",
	.id		= -1,
};

static struct omap_led_config apollon_led_config[] = {
	{
		.cdev	= {
			.name	= "apollon:led0",
		},
		.gpio	= LED0_GPIO13,
	},
	{
		.cdev	= {
			.name	= "apollon:led1",
		},
		.gpio	= LED1_GPIO14,
	},
	{
		.cdev	= {
			.name	= "apollon:led2",
		},
		.gpio	= LED2_GPIO15,
	},
};

static struct omap_led_platform_data apollon_led_data = {
	.nr_leds	= ARRAY_SIZE(apollon_led_config),
	.leds		= apollon_led_config,
};

static struct platform_device apollon_led_device = {
	.name		= "omap-led",
	.id		= -1,
	.dev		= {
		.platform_data	= &apollon_led_data,
	},
};

static struct platform_device *apollon_devices[] __initdata = {
	&apollon_onenand_device,
	&apollon_smc91x_device,
	&apollon_lcd_device,
	&apollon_led_device,
};

static inline void __init apollon_init_smc91x(void)
{
	unsigned long base;

	unsigned int rate;
	struct clk *gpmc_fck;
	int eth_cs;
	int err;

	gpmc_fck = clk_get(NULL, "gpmc_fck");	/* Always on ENABLE_ON_INIT */
	if (IS_ERR(gpmc_fck)) {
		WARN_ON(1);
		return;
	}

	clk_enable(gpmc_fck);
	rate = clk_get_rate(gpmc_fck);

	eth_cs = APOLLON_ETH_CS;

	/* Make sure CS1 timings are correct */
	gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG1, 0x00011200);

	if (rate >= 160000000) {
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f01);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080803);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1c0b1c0a);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x041f1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000004C4);
	} else if (rate >= 130000000) {
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f00);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080802);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1C091C09);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x041f1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000004C4);
	} else {/* rate = 100000000 */
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG2, 0x001f1f00);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG3, 0x00080802);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG4, 0x1C091C09);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG5, 0x031A1F1F);
		gpmc_cs_write_reg(eth_cs, GPMC_CS_CONFIG6, 0x000003C2);
	}

	if (gpmc_cs_request(APOLLON_ETH_CS, SZ_16M, &base) < 0) {
		printk(KERN_ERR "Failed to request GPMC CS for smc91x\n");
		goto out;
	}
	apollon_smc91x_resources[0].start = base + 0x300;
	apollon_smc91x_resources[0].end   = base + 0x30f;
	udelay(100);

	omap_mux_init_gpio(APOLLON_ETHR_GPIO_IRQ, 0);
	err = gpio_request_one(APOLLON_ETHR_GPIO_IRQ, GPIOF_IN, "SMC91x irq");
	if (err) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			APOLLON_ETHR_GPIO_IRQ);
		gpmc_cs_free(APOLLON_ETH_CS);
	}
out:
	clk_disable(gpmc_fck);
	clk_put(gpmc_fck);
}

static struct omap_usb_config apollon_usb_config __initdata = {
	.register_dev	= 1,
	.hmc_mode	= 0x14,	/* 0:dev 1:host1 2:disable */

	.pins[0]	= 6,
};

static struct omap_lcd_config apollon_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel apollon_config[] __initdata = {
	{ OMAP_TAG_LCD,		&apollon_lcd_config },
};

static void __init omap_apollon_init_early(void)
{
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL, NULL);
}

static struct gpio apollon_gpio_leds[] __initdata = {
	{ LED0_GPIO13, GPIOF_OUT_INIT_LOW, "LED0" }, /* LED0 - AA10 */
	{ LED1_GPIO14, GPIOF_OUT_INIT_LOW, "LED1" }, /* LED1 - AA6  */
	{ LED2_GPIO15, GPIOF_OUT_INIT_LOW, "LED2" }, /* LED2 - AA4  */
};

static void __init apollon_led_init(void)
{
	omap_mux_init_signal("vlynq_clk.gpio_13", 0);
	omap_mux_init_signal("vlynq_rx1.gpio_14", 0);
	omap_mux_init_signal("vlynq_rx0.gpio_15", 0);

	gpio_request_array(apollon_gpio_leds, ARRAY_SIZE(apollon_gpio_leds));
}

static void __init apollon_usb_init(void)
{
	/* USB device */
	/* DEVICE_SUSPEND */
	omap_mux_init_signal("mcbsp2_clkx.gpio_12", 0);
	gpio_request_one(12, GPIOF_OUT_INIT_LOW, "USB suspend");
	omap2_usbfs_init(&apollon_usb_config);
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init omap_apollon_init(void)
{
	u32 v;

	omap2420_mux_init(board_mux, OMAP_PACKAGE_ZAC);
	omap_board_config = apollon_config;
	omap_board_config_size = ARRAY_SIZE(apollon_config);

	apollon_init_smc91x();
	apollon_led_init();
	apollon_flash_init();
	apollon_usb_init();

	/* REVISIT: where's the correct place */
	omap_mux_init_signal("sys_nirq", OMAP_PULL_ENA | OMAP_PULL_UP);

	/* LCD PWR_EN */
	omap_mux_init_signal("mcbsp2_dr.gpio_11", OMAP_PULL_ENA | OMAP_PULL_UP);

	/* Use Interal loop-back in MMC/SDIO Module Input Clock selection */
	v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	v |= (1 << 24);
	omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);

	/*
 	 * Make sure the serial ports are muxed on at this point.
	 * You have to mux them off in device drivers later on
	 * if not needed.
	 */
	platform_add_devices(apollon_devices, ARRAY_SIZE(apollon_devices));
	omap_serial_init();
}

static void __init omap_apollon_map_io(void)
{
	omap2_set_globals_242x();
	omap242x_map_common_io();
}

MACHINE_START(OMAP_APOLLON, "OMAP24xx Apollon")
	/* Maintainer: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= 0x80000100,
	.reserve	= omap_reserve,
	.map_io		= omap_apollon_map_io,
	.init_early	= omap_apollon_init_early,
	.init_irq	= omap2_init_irq,
	.init_machine	= omap_apollon_init,
	.timer		= &omap2_timer,
MACHINE_END
