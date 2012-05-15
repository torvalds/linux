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
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/i2c/tps65010.h>
#include <linux/smc91x.h>
#include <linux/omapfb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mux.h>
#include <plat/dma.h>
#include <plat/tc.h>
#include <plat/irda.h>
#include <plat/usb.h>
#include <plat/keypad.h>
#include <plat/flash.h>

#include <mach/hardware.h>

#include "common.h"
#include "board-h2.h"

/* At OMAP1610 Innovator the Ethernet is directly connected to CS1 */
#define OMAP1610_ETHR_START		0x04000300

static const unsigned int h2_keymap[] = {
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

static struct physmap_flash_data h2_nor_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= h2_nor_partitions,
	.nr_parts	= ARRAY_SIZE(h2_nor_partitions),
};

static struct resource h2_nor_resource = {
	/* This is on CS3, wherever it's mapped */
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h2_nor_device = {
	.name		= "physmap-flash",
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

static void h2_nand_cmd_ctl(struct mtd_info *mtd, int cmd, unsigned int ctrl)
{
	struct nand_chip *this = mtd->priv;
	unsigned long mask;

	if (cmd == NAND_CMD_NONE)
		return;

	mask = (ctrl & NAND_CLE) ? 0x02 : 0;
	if (ctrl & NAND_ALE)
		mask |= 0x04;
	writeb(cmd, (unsigned long)this->IO_ADDR_W | mask);
}

#define H2_NAND_RB_GPIO_PIN	62

static int h2_nand_dev_ready(struct mtd_info *mtd)
{
	return gpio_get_value(H2_NAND_RB_GPIO_PIN);
}

static const char *h2_part_probes[] = { "cmdlinepart", NULL };

static struct platform_nand_data h2_nand_platdata = {
	.chip	= {
		.nr_chips		= 1,
		.chip_offset		= 0,
		.nr_partitions		= ARRAY_SIZE(h2_nand_partitions),
		.partitions		= h2_nand_partitions,
		.options		= NAND_SAMSUNG_LP_OPTIONS,
		.part_probe_types	= h2_part_probes,
	},
	.ctrl	= {
		.cmd_ctrl	= h2_nand_cmd_ctl,
		.dev_ready	= h2_nand_dev_ready,

	},
};

static struct resource h2_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device h2_nand_device = {
	.name		= "gen_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &h2_nand_platdata,
	},
	.num_resources	= 1,
	.resource	= &h2_nand_resource,
};

static struct smc91x_platdata h2_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
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
	.dev	= {
		.platform_data	= &h2_smc91x_info,
	},
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

static const struct matrix_keymap_data h2_keymap_data = {
	.keymap		= h2_keymap,
	.keymap_size	= ARRAY_SIZE(h2_keymap),
};

static struct omap_kp_platform_data h2_kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &h2_keymap_data,
	.rep		= true,
	.delay		= 9,
	.dbounce	= true,
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

static struct platform_device *h2_devices[] __initdata = {
	&h2_nor_device,
	&h2_nand_device,
	&h2_smc91x_device,
	&h2_irda_device,
	&h2_kp_device,
};

static void __init h2_init_smc91x(void)
{
	if (gpio_request(0, "SMC91x irq") < 0) {
		printk("Error requesting gpio 0 for smc91x irq\n");
		return;
	}
}

static int tps_setup(struct i2c_client *client, void *context)
{
	tps65010_config_vregs1(TPS_LDO2_ENABLE | TPS_VLDO2_3_0V |
				TPS_LDO1_ENABLE | TPS_VLDO1_3_0V);

	return 0;
}

static struct tps65010_board tps_board = {
	.base		= H2_TPS_GPIO_BASE,
	.outmask	= 0x0f,
	.setup		= tps_setup,
};

static struct i2c_board_info __initdata h2_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("tps65010", 0x48),
		.irq            = OMAP_GPIO_IRQ(58),
		.platform_data	= &tps_board,
	}, {
		I2C_BOARD_INFO("isp1301_omap", 0x2d),
		.irq		= OMAP_GPIO_IRQ(2),
	},
};

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

static struct omap_lcd_config h2_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static void __init h2_init(void)
{
	h2_init_smc91x();

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
	if (gpio_request(H2_NAND_RB_GPIO_PIN, "NAND ready") < 0)
		BUG();
	gpio_direction_input(H2_NAND_RB_GPIO_PIN);

	omap_cfg_reg(L3_1610_FLASH_CS2B_OE);
	omap_cfg_reg(M8_1610_FLASH_CS2B_WE);

	/* MMC:  card detect and WP */
	/* omap_cfg_reg(U19_ARMIO1); */		/* CD */
	omap_cfg_reg(BALLOUT_V8_ARMIO3);	/* WP */

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

	platform_add_devices(h2_devices, ARRAY_SIZE(h2_devices));
	omap_serial_init();
	omap_register_i2c_bus(1, 100, h2_i2c_board_info,
			      ARRAY_SIZE(h2_i2c_board_info));
	omap1_usb_init(&h2_usb_config);
	h2_mmc_init();

	omapfb_set_lcd_config(&h2_lcd_config);
}

MACHINE_START(OMAP_H2, "TI-H2")
	/* Maintainer: Imre Deak <imre.deak@nokia.com> */
	.atag_offset	= 0x100,
	.map_io		= omap16xx_map_io,
	.init_early     = omap1_init_early,
	.reserve	= omap_reserve,
	.init_irq	= omap1_init_irq,
	.init_machine	= h2_init,
	.timer		= &omap1_timer,
	.restart	= omap1_restart,
MACHINE_END
