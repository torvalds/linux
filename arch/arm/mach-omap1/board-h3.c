/*
 * linux/arch/arm/mach-omap1/board-h3.c
 *
 * This file contains OMAP1710 H3 specific code.
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 * Copyright (C) 2002 MontaVista Software, Inc.
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc.
 *         Greg Lonnon (glonnon@ridgerun.com) or info@ridgerun.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/i2c/tps65010.h>
#include <linux/smc91x.h>
#include <linux/omapfb.h>
#include <linux/platform_data/gpio-omap.h>
#include <linux/leds.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mux.h>
#include <mach/tc.h>
#include <linux/platform_data/keypad-omap.h>
#include <linux/omap-dma.h>
#include "flash.h"

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/usb.h>

#include "common.h"
#include "board-h3.h"

/* In OMAP1710 H3 the Ethernet is directly connected to CS1 */
#define OMAP1710_ETHR_START		0x04000300

#define H3_TS_GPIO	48

static const unsigned int h3_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(1, 0, KEY_RIGHT),
	KEY(2, 0, KEY_3),
	KEY(3, 0, KEY_F10),
	KEY(4, 0, KEY_F5),
	KEY(5, 0, KEY_9),
	KEY(0, 1, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(2, 1, KEY_2),
	KEY(3, 1, KEY_F9),
	KEY(4, 1, KEY_F7),
	KEY(5, 1, KEY_0),
	KEY(0, 2, KEY_ENTER),
	KEY(1, 2, KEY_6),
	KEY(2, 2, KEY_1),
	KEY(3, 2, KEY_F2),
	KEY(4, 2, KEY_F6),
	KEY(5, 2, KEY_HOME),
	KEY(0, 3, KEY_8),
	KEY(1, 3, KEY_5),
	KEY(2, 3, KEY_F12),
	KEY(3, 3, KEY_F3),
	KEY(4, 3, KEY_F8),
	KEY(5, 3, KEY_END),
	KEY(0, 4, KEY_7),
	KEY(1, 4, KEY_4),
	KEY(2, 4, KEY_F11),
	KEY(3, 4, KEY_F1),
	KEY(4, 4, KEY_F4),
	KEY(5, 4, KEY_ESC),
	KEY(0, 5, KEY_F13),
	KEY(1, 5, KEY_F14),
	KEY(2, 5, KEY_F15),
	KEY(3, 5, KEY_F16),
	KEY(4, 5, KEY_SLEEP),
};


static struct mtd_partition nor_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
	      .name		= "bootloader",
	      .offset		= 0,
	      .size		= SZ_128K,
	      .mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
	      .name		= "params",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_128K,
	      .mask_flags	= 0,
	},
	/* kernel */
	{
	      .name		= "kernel",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= SZ_2M,
	      .mask_flags	= 0
	},
	/* file system */
	{
	      .name		= "filesystem",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	}
};

static struct physmap_flash_data nor_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= nor_partitions,
	.nr_parts	= ARRAY_SIZE(nor_partitions),
};

static struct resource nor_resource = {
	/* This is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device nor_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &nor_data,
	},
	.num_resources	= 1,
	.resource	= &nor_resource,
};

static struct mtd_partition nand_partitions[] = {
#if 0
	/* REVISIT: enable these partitions if you make NAND BOOT work */
	{
		.name		= "xloader",
		.offset		= 0,
		.size		= 64 * 1024,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "bootloader",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 256 * 1024,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "params",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 192 * 1024,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * SZ_1M,
	},
#endif
	{
		.name		= "filesystem",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	},
};

#define H3_NAND_RB_GPIO_PIN	10

static int nand_dev_ready(struct mtd_info *mtd)
{
	return gpio_get_value(H3_NAND_RB_GPIO_PIN);
}

static struct platform_nand_data nand_platdata = {
	.chip	= {
		.nr_chips		= 1,
		.chip_offset		= 0,
		.nr_partitions		= ARRAY_SIZE(nand_partitions),
		.partitions		= nand_partitions,
		.options		= NAND_SAMSUNG_LP_OPTIONS,
	},
	.ctrl	= {
		.cmd_ctrl	= omap1_nand_cmd_ctl,
		.dev_ready	= nand_dev_ready,

	},
};

static struct resource nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device nand_device = {
	.name		= "gen_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &nand_platdata,
	},
	.num_resources	= 1,
	.resource	= &nand_resource,
};

static struct smc91x_platdata smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= OMAP1710_ETHR_START,		/* Physical */
		.end	= OMAP1710_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.dev	= {
		.platform_data	= &smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static void __init h3_init_smc91x(void)
{
	omap_cfg_reg(W15_1710_GPIO40);
	if (gpio_request(40, "SMC91x irq") < 0) {
		printk("Error requesting gpio 40 for smc91x irq\n");
		return;
	}
}

#define GPTIMER_BASE		0xFFFB1400
#define GPTIMER_REGS(x)	(0xFFFB1400 + (x * 0x800))
#define GPTIMER_REGS_SIZE	0x46

static struct resource intlat_resources[] = {
	[0] = {
		.start  = GPTIMER_REGS(0),	      /* Physical */
		.end    = GPTIMER_REGS(0) + GPTIMER_REGS_SIZE,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = INT_1610_GPTIMER1,
		.end    = INT_1610_GPTIMER1,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device intlat_device = {
	.name	   = "omap_intlat",
	.id	     = 0,
	.num_resources  = ARRAY_SIZE(intlat_resources),
	.resource       = intlat_resources,
};

static struct resource h3_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data h3_keymap_data = {
	.keymap		= h3_keymap,
	.keymap_size	= ARRAY_SIZE(h3_keymap),
};

static struct omap_kp_platform_data h3_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &h3_keymap_data,
	.rep		= true,
	.delay		= 9,
	.dbounce	= true,
};

static struct platform_device h3_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &h3_kp_data,
	},
	.num_resources	= ARRAY_SIZE(h3_kp_resources),
	.resource	= h3_kp_resources,
};

static struct platform_device h3_lcd_device = {
	.name		= "lcd_h3",
	.id		= -1,
};

static struct spi_board_info h3_spi_board_info[] __initdata = {
	[0] = {
		.modalias	= "tsc2101",
		.bus_num	= 2,
		.chip_select	= 0,
		.max_speed_hz	= 16000000,
		/* .platform_data	= &tsc_platform_data, */
	},
};

static struct gpio_led h3_gpio_led_pins[] = {
	{
		.name		= "h3:red",
		.default_trigger = "heartbeat",
		.gpio		= 3,
	},
	{
		.name		= "h3:green",
		.default_trigger = "cpu0",
		.gpio		= OMAP_MPUIO(4),
	},
};

static struct gpio_led_platform_data h3_gpio_led_data = {
	.leds		= h3_gpio_led_pins,
	.num_leds	= ARRAY_SIZE(h3_gpio_led_pins),
};

static struct platform_device h3_gpio_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &h3_gpio_led_data,
	},
};

static struct platform_device *devices[] __initdata = {
	&nor_device,
	&nand_device,
        &smc91x_device,
	&intlat_device,
	&h3_kp_device,
	&h3_lcd_device,
	&h3_gpio_leds,
};

static struct omap_usb_config h3_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg	    = 2,

#if IS_ENABLED(CONFIG_USB_OMAP)
	.hmc_mode       = 19,   /* 0:host(off) 1:dev|otg 2:disabled */
#elif  defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* NONSTANDARD CABLE NEEDED (B-to-Mini-B) */
	.hmc_mode       = 20,   /* 1:dev|otg(off) 1:host 2:disabled */
#endif

	.pins[1]	= 3,
};

static struct omap_lcd_config h3_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct i2c_board_info __initdata h3_i2c_board_info[] = {
       {
		I2C_BOARD_INFO("tps65013", 0x48),
       },
	{
		I2C_BOARD_INFO("isp1301_omap", 0x2d),
	},
};

static void __init h3_init(void)
{
	h3_init_smc91x();

	/* Here we assume the NOR boot config:  NOR on CS3 (possibly swapped
	 * to address 0 by a dip switch), NAND on CS2B.  The NAND driver will
	 * notice whether a NAND chip is enabled at probe time.
	 *
	 * H3 support NAND-boot, with a dip switch to put NOR on CS2B and NAND
	 * (which on H2 may be 16bit) on CS3.  Try detecting that in code here,
	 * to avoid probing every possible flash configuration...
	 */
	nor_resource.end = nor_resource.start = omap_cs3_phys();
	nor_resource.end += SZ_32M - 1;

	nand_resource.end = nand_resource.start = OMAP_CS2B_PHYS;
	nand_resource.end += SZ_4K - 1;
	BUG_ON(gpio_request(H3_NAND_RB_GPIO_PIN, "NAND ready") < 0);
	gpio_direction_input(H3_NAND_RB_GPIO_PIN);

	/* GPIO10 Func_MUX_CTRL reg bit 29:27, Configure V2 to mode1 as GPIO */
	/* GPIO10 pullup/down register, Enable pullup on GPIO10 */
	omap_cfg_reg(V2_1710_GPIO10);

	/* Mux pins for keypad */
	omap_cfg_reg(F18_1610_KBC0);
	omap_cfg_reg(D20_1610_KBC1);
	omap_cfg_reg(D19_1610_KBC2);
	omap_cfg_reg(E18_1610_KBC3);
	omap_cfg_reg(C21_1610_KBC4);
	omap_cfg_reg(G18_1610_KBR0);
	omap_cfg_reg(F19_1610_KBR1);
	omap_cfg_reg(H14_1610_KBR2);
	omap_cfg_reg(E20_1610_KBR3);
	omap_cfg_reg(E19_1610_KBR4);
	omap_cfg_reg(N19_1610_KBR5);

	/* GPIO based LEDs */
	omap_cfg_reg(P18_1610_GPIO3);
	omap_cfg_reg(MPUIO4);

	smc91x_resources[1].start = gpio_to_irq(40);
	smc91x_resources[1].end = gpio_to_irq(40);
	platform_add_devices(devices, ARRAY_SIZE(devices));
	h3_spi_board_info[0].irq = gpio_to_irq(H3_TS_GPIO);
	spi_register_board_info(h3_spi_board_info,
				ARRAY_SIZE(h3_spi_board_info));
	omap_serial_init();
	h3_i2c_board_info[1].irq = gpio_to_irq(14);
	omap_register_i2c_bus(1, 100, h3_i2c_board_info,
			      ARRAY_SIZE(h3_i2c_board_info));
	omap1_usb_init(&h3_usb_config);
	h3_mmc_init();

	omapfb_set_lcd_config(&h3_lcd_config);
}

MACHINE_START(OMAP_H3, "TI OMAP1710 H3 board")
	/* Maintainer: Texas Instruments, Inc. */
	.atag_offset	= 0x100,
	.map_io		= omap16xx_map_io,
	.init_early     = omap1_init_early,
	.init_irq	= omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine	= h3_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
