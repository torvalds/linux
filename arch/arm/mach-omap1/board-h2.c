/*
 * linux/arch/arm/mach-omap1/board-h2.c
 *
 * Board specific inits for OMAP-1610 H2
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * Copyright (C) 2002 MontaVista Software, Inc.
 *
 * Separated FPGA interrupts from innovator1510.c and cleaned up for 2.6
 * Copyright (C) 2004 Nokia Corporation by Tony Lindrgen <tony@atomide.com>
 *
 * H2 specific changes and cleanup
 * Copyright (C) 2004 Nokia Corporation by Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/input.h>
#include <linux/i2c/tps65010.h>

#include <asm/hardware.h>
#include <asm/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <asm/arch/gpio-switch.h>
#include <asm/arch/mux.h>
#include <asm/arch/tc.h>
#include <asm/arch/nand.h>
#include <asm/arch/irda.h>
#include <asm/arch/usb.h>
#include <asm/arch/keypad.h>
#include <asm/arch/common.h>
#include <asm/arch/mcbsp.h>
#include <asm/arch/omap-alsa.h>

static int h2_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_3),
	KEY(0, 3, KEY_F10),
	KEY(0, 4, KEY_F5),
	KEY(0, 5, KEY_9),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_2),
	KEY(1, 3, KEY_F9),
	KEY(1, 4, KEY_F7),
	KEY(1, 5, KEY_0),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_6),
	KEY(2, 2, KEY_1),
	KEY(2, 3, KEY_F2),
	KEY(2, 4, KEY_F6),
	KEY(2, 5, KEY_HOME),
	KEY(3, 0, KEY_8),
	KEY(3, 1, KEY_5),
	KEY(3, 2, KEY_F12),
	KEY(3, 3, KEY_F3),
	KEY(3, 4, KEY_F8),
	KEY(3, 5, KEY_END),
	KEY(4, 0, KEY_7),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_F11),
	KEY(4, 3, KEY_F1),
	KEY(4, 4, KEY_F4),
	KEY(4, 5, KEY_ESC),
	KEY(5, 0, KEY_F13),
	KEY(5, 1, KEY_F14),
	KEY(5, 2, KEY_F15),
	KEY(5, 3, KEY_F16),
	KEY(5, 4, KEY_SLEEP),
	0
};

static struct mtd_partition h2_nor_partitions[] = {
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

static struct flash_platform_data h2_nor_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
	.parts		= h2_nor_partitions,
	.nr_parts	= ARRAY_SIZE(h2_nor_partitions),
};

static struct resource h2_nor_resource = {
	/* This is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h2_nor_device = {
	.name		= "omapflash",
	.id		= 0,
	.dev		= {
		.platform_data	= &h2_nor_data,
	},
	.num_resources	= 1,
	.resource	= &h2_nor_resource,
};

static struct mtd_partition h2_nand_partitions[] = {
#if 0
	/* REVISIT:  enable these partitions if you make NAND BOOT
	 * work on your H2 (rev C or newer); published versions of
	 * x-load only support P2 and H3.
	 */
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

/* dip switches control NAND chip access:  8 bit, 16 bit, or neither */
static struct omap_nand_platform_data h2_nand_data = {
	.options	= NAND_SAMSUNG_LP_OPTIONS,
	.parts		= h2_nand_partitions,
	.nr_parts	= ARRAY_SIZE(h2_nand_partitions),
};

static struct resource h2_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h2_nand_device = {
	.name		= "omapnand",
	.id		= 0,
	.dev		= {
		.platform_data	= &h2_nand_data,
	},
	.num_resources	= 1,
	.resource	= &h2_nand_resource,
};

static struct resource h2_smc91x_resources[] = {
	[0] = {
		.start	= OMAP1610_ETHR_START,		/* Physical */
		.end	= OMAP1610_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= OMAP_GPIO_IRQ(0),
		.end	= OMAP_GPIO_IRQ(0),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct platform_device h2_smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(h2_smc91x_resources),
	.resource	= h2_smc91x_resources,
};

static struct resource h2_kp_resources[] = {
	[0] = {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct omap_kp_platform_data h2_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap		= h2_keymap,
	.keymapsize	= ARRAY_SIZE(h2_keymap),
	.rep		= 1,
	.delay		= 9,
	.dbounce	= 1,
};

static struct platform_device h2_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &h2_kp_data,
	},
	.num_resources	= ARRAY_SIZE(h2_kp_resources),
	.resource	= h2_kp_resources,
};

#define H2_IRDA_FIRSEL_GPIO_PIN	17

#if defined(CONFIG_OMAP_IR) || defined(CONFIG_OMAP_IR_MODULE)
static int h2_transceiver_mode(struct device *dev, int state)
{
	if (state & IR_SIRMODE)
		omap_set_gpio_dataout(H2_IRDA_FIRSEL_GPIO_PIN, 0);
	else    /* MIR/FIR */
		omap_set_gpio_dataout(H2_IRDA_FIRSEL_GPIO_PIN, 1);

	return 0;
}
#endif

static struct omap_irda_config h2_irda_data = {
	.transceiver_cap	= IR_SIRMODE | IR_MIRMODE | IR_FIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource h2_irda_resources[] = {
	[0] = {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 irda_dmamask = 0xffffffff;

static struct platform_device h2_irda_device = {
	.name		= "omapirda",
	.id		= 0,
	.dev		= {
		.platform_data	= &h2_irda_data,
		.dma_mask	= &irda_dmamask,
	},
	.num_resources	= ARRAY_SIZE(h2_irda_resources),
	.resource	= h2_irda_resources,
};

static struct platform_device h2_lcd_device = {
	.name		= "lcd_h2",
	.id		= -1,
};

static struct omap_mcbsp_reg_cfg mcbsp_regs = {
	.spcr2 = FREE | FRST | GRST | XRST | XINTM(3),
	.spcr1 = RINTM(3) | RRST,
	.rcr2  = RPHASE | RFRLEN2(OMAP_MCBSP_WORD_8) |
                RWDLEN2(OMAP_MCBSP_WORD_16) | RDATDLY(1),
	.rcr1  = RFRLEN1(OMAP_MCBSP_WORD_8) | RWDLEN1(OMAP_MCBSP_WORD_16),
	.xcr2  = XPHASE | XFRLEN2(OMAP_MCBSP_WORD_8) |
                XWDLEN2(OMAP_MCBSP_WORD_16) | XDATDLY(1) | XFIG,
	.xcr1  = XFRLEN1(OMAP_MCBSP_WORD_8) | XWDLEN1(OMAP_MCBSP_WORD_16),
	.srgr1 = FWID(15),
	.srgr2 = GSYNC | CLKSP | FSGM | FPER(31),

	.pcr0  = CLKXM | CLKRM | FSXP | FSRP | CLKXP | CLKRP,
	/*.pcr0 = CLKXP | CLKRP,*/        /* mcbsp: slave */
};

static struct omap_alsa_codec_config alsa_config = {
	.name                   = "H2 TSC2101",
	.mcbsp_regs_alsa        = &mcbsp_regs,
	.codec_configure_dev    = NULL, /* tsc2101_configure, */
	.codec_set_samplerate   = NULL, /* tsc2101_set_samplerate, */
	.codec_clock_setup      = NULL, /* tsc2101_clock_setup, */
	.codec_clock_on         = NULL, /* tsc2101_clock_on, */
	.codec_clock_off        = NULL, /* tsc2101_clock_off, */
	.get_default_samplerate = NULL, /* tsc2101_get_default_samplerate, */
};

static struct platform_device h2_mcbsp1_device = {
	.name	= "omap_alsa_mcbsp",
	.id	= 1,
	.dev = {
		.platform_data	= &alsa_config,
	},
};

static struct platform_device *h2_devices[] __initdata = {
	&h2_nor_device,
	&h2_nand_device,
	&h2_smc91x_device,
	&h2_irda_device,
	&h2_kp_device,
	&h2_lcd_device,
	&h2_mcbsp1_device,
};

static void __init h2_init_smc91x(void)
{
	if ((omap_request_gpio(0)) < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}
}

static struct i2c_board_info __initdata h2_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("tps65010", 0x48),
		.irq            = OMAP_GPIO_IRQ(58),
	}, {
		I2C_BOARD_INFO("isp1301_omap", 0x2d),
		.irq		= OMAP_GPIO_IRQ(2),
	},
};

static void __init h2_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
	h2_init_smc91x();
}

static struct omap_usb_config h2_usb_config __initdata = {
	/* usb1 has a Mini-AB port and external isp1301 transceiver */
	.otg		= 2,

#ifdef	CONFIG_USB_GADGET_OMAP
	.hmc_mode	= 19,	/* 0:host(off) 1:dev|otg 2:disabled */
	/* .hmc_mode	= 21,*/	/* 0:host(off) 1:dev(loopback) 2:host(loopback) */
#elif	defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
	/* needs OTG cable, or NONSTANDARD (B-to-MiniB) */
	.hmc_mode	= 20,	/* 1:dev|otg(off) 1:host 2:disabled */
#endif

	.pins[1]	= 3,
};

static struct omap_mmc_config h2_mmc_config __initdata = {
	.mmc[0] = {
		.enabled	= 1,
		.wire4		= 1,
	},
};

extern struct omap_mmc_platform_data h2_mmc_data;

static struct omap_uart_config h2_uart_config __initdata = {
	.enabled_uarts = ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_lcd_config h2_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel h2_config[] __initdata = {
	{ OMAP_TAG_USB,		&h2_usb_config },
	{ OMAP_TAG_MMC,		&h2_mmc_config },
	{ OMAP_TAG_UART,	&h2_uart_config },
	{ OMAP_TAG_LCD,		&h2_lcd_config },
};

#define H2_NAND_RB_GPIO_PIN	62

static int h2_nand_dev_ready(struct omap_nand_platform_data *data)
{
	return omap_get_gpio_datain(H2_NAND_RB_GPIO_PIN);
}

static void __init h2_init(void)
{
	/* Here we assume the NOR boot config:  NOR on CS3 (possibly swapped
	 * to address 0 by a dip switch), NAND on CS2B.  The NAND driver will
	 * notice whether a NAND chip is enabled at probe time.
	 *
	 * FIXME revC boards (and H3) support NAND-boot, with a dip switch to
	 * put NOR on CS2B and NAND (which on H2 may be 16bit) on CS3.  Try
	 * detecting that in code here, to avoid probing every possible flash
	 * configuration...
	 */
	h2_nor_resource.end = h2_nor_resource.start = omap_cs3_phys();
	h2_nor_resource.end += SZ_32M - 1;

	h2_nand_resource.end = h2_nand_resource.start = OMAP_CS2B_PHYS;
	h2_nand_resource.end += SZ_4K - 1;
	if (!(omap_request_gpio(H2_NAND_RB_GPIO_PIN)))
		h2_nand_data.dev_ready = h2_nand_dev_ready;

	omap_cfg_reg(L3_1610_FLASH_CS2B_OE);
	omap_cfg_reg(M8_1610_FLASH_CS2B_WE);

	/* MMC:  card detect and WP */
	/* omap_cfg_reg(U19_ARMIO1); */		/* CD */
	omap_cfg_reg(BALLOUT_V8_ARMIO3);	/* WP */

	/* Irda */
#if defined(CONFIG_OMAP_IR) || defined(CONFIG_OMAP_IR_MODULE)
	omap_writel(omap_readl(FUNC_MUX_CTRL_A) | 7, FUNC_MUX_CTRL_A);
	if (!(omap_request_gpio(H2_IRDA_FIRSEL_GPIO_PIN))) {
		omap_set_gpio_direction(H2_IRDA_FIRSEL_GPIO_PIN, 0);
		h2_irda_data.transceiver_mode = h2_transceiver_mode;
	}
#endif

	platform_add_devices(h2_devices, ARRAY_SIZE(h2_devices));
	omap_board_config = h2_config;
	omap_board_config_size = ARRAY_SIZE(h2_config);
	omap_serial_init();
	omap_register_i2c_bus(1, 100, h2_i2c_board_info,
			      ARRAY_SIZE(h2_i2c_board_info));
	h2_mmc_init();
}

static void __init h2_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(OMAP_H2, "TI-H2")
	/* Maintainer: Imre Deak <imre.deak@nokia.com> */
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= h2_map_io,
	.init_irq	= h2_init_irq,
	.init_machine	= h2_init,
	.timer		= &omap_timer,
MACHINE_END
