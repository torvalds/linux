// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-omap1/board-perseus2.c
 *
 * Modified from board-generic.c
 *
 * Original OMAP730 support by Jean Pihet <j-pihet@ti.com>
 * Updated for 2.6 by Kevin Hilman <kjh@hilman.org>
 */
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/platnand.h>
#include <linux/mtd/physmap.h>
#include <linux/input.h>
#include <linux/smc91x.h>
#include <linux/omapfb.h>
#include <linux/platform_data/keypad-omap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/tc.h>
#include <mach/mux.h>
#include "flash.h"

#include <mach/hardware.h>

#include "iomap.h"
#include "common.h"
#include "fpga.h"

static const unsigned int p2_keymap[] = {
	KEY(0, 0, KEY_UP),
	KEY(1, 0, KEY_RIGHT),
	KEY(2, 0, KEY_LEFT),
	KEY(3, 0, KEY_DOWN),
	KEY(4, 0, KEY_ENTER),
	KEY(0, 1, KEY_F10),
	KEY(1, 1, KEY_SEND),
	KEY(2, 1, KEY_END),
	KEY(3, 1, KEY_VOLUMEDOWN),
	KEY(4, 1, KEY_VOLUMEUP),
	KEY(5, 1, KEY_RECORD),
	KEY(0, 2, KEY_F9),
	KEY(1, 2, KEY_3),
	KEY(2, 2, KEY_6),
	KEY(3, 2, KEY_9),
	KEY(4, 2, KEY_KPDOT),
	KEY(0, 3, KEY_BACK),
	KEY(1, 3, KEY_2),
	KEY(2, 3, KEY_5),
	KEY(3, 3, KEY_8),
	KEY(4, 3, KEY_0),
	KEY(5, 3, KEY_KPSLASH),
	KEY(0, 4, KEY_HOME),
	KEY(1, 4, KEY_1),
	KEY(2, 4, KEY_4),
	KEY(3, 4, KEY_7),
	KEY(4, 4, KEY_KPASTERISK),
	KEY(5, 4, KEY_POWER),
};

static struct smc91x_platdata smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
	.leda	= RPC_LED_100_10,
	.ledb	= RPC_LED_TX_RX,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= H2P2_DBG_FPGA_ETHR_START,	/* Physical */
		.end	= H2P2_DBG_FPGA_ETHR_START + 0xf,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_7XX_MPU_EXT_NIRQ,
		.end	= 0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	},
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
	/* rest of flash is a file system */
	{
	      .name		= "rootfs",
	      .offset		= MTDPART_OFS_APPEND,
	      .size		= MTDPART_SIZ_FULL,
	      .mask_flags	= 0
	},
};

static struct physmap_flash_data nor_data = {
	.width		= 2,
	.set_vpp	= omap1_set_vpp,
	.parts		= nor_partitions,
	.nr_parts	= ARRAY_SIZE(nor_partitions),
};

static struct resource nor_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_32M - 1,
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

#define P2_NAND_RB_GPIO_PIN	62

static int nand_dev_ready(struct nand_chip *chip)
{
	return gpio_get_value(P2_NAND_RB_GPIO_PIN);
}

static struct platform_nand_data nand_data = {
	.chip	= {
		.nr_chips		= 1,
		.chip_offset		= 0,
		.options		= NAND_SAMSUNG_LP_OPTIONS,
	},
	.ctrl	= {
		.cmd_ctrl	= omap1_nand_cmd_ctl,
		.dev_ready	= nand_dev_ready,
	},
};

static struct resource nand_resource = {
	.start		= OMAP_CS3_PHYS,
	.end		= OMAP_CS3_PHYS + SZ_4K - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device nand_device = {
	.name		= "gen_nand",
	.id		= 0,
	.dev		= {
		.platform_data	= &nand_data,
	},
	.num_resources	= 1,
	.resource	= &nand_resource,
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

static struct resource kp_resources[] = {
	[0] = {
		.start	= INT_7XX_MPUIO_KEYPAD,
		.end	= INT_7XX_MPUIO_KEYPAD,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct matrix_keymap_data p2_keymap_data = {
	.keymap		= p2_keymap,
	.keymap_size	= ARRAY_SIZE(p2_keymap),
};

static struct omap_kp_platform_data kp_data = {
	.rows		= 8,
	.cols		= 8,
	.keymap_data	= &p2_keymap_data,
	.delay		= 4,
	.dbounce	= true,
};

static struct platform_device kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &kp_data,
	},
	.num_resources	= ARRAY_SIZE(kp_resources),
	.resource	= kp_resources,
};

static struct platform_device *devices[] __initdata = {
	&nor_device,
	&nand_device,
	&smc91x_device,
	&kp_device,
};

static const struct omap_lcd_config perseus2_lcd_config __initconst = {
	.ctrl_name	= "internal",
};

static void __init perseus2_init_smc91x(void)
{
	__raw_writeb(1, H2P2_DBG_FPGA_LAN_RESET);
	mdelay(50);
	__raw_writeb(__raw_readb(H2P2_DBG_FPGA_LAN_RESET) & ~1,
		   H2P2_DBG_FPGA_LAN_RESET);
	mdelay(50);
}

static void __init omap_perseus2_init(void)
{
	/* Early, board-dependent init */

	/*
	 * Hold GSM Reset until needed
	 */
	omap_writew(omap_readw(OMAP7XX_DSP_M_CTL) & ~1, OMAP7XX_DSP_M_CTL);

	/*
	 * UARTs -> done automagically by 8250 driver
	 */

	/*
	 * CSx timings, GPIO Mux ... setup
	 */

	/* Flash: CS0 timings setup */
	omap_writel(0x0000fff3, OMAP7XX_FLASH_CFG_0);
	omap_writel(0x00000088, OMAP7XX_FLASH_ACFG_0);

	/*
	 * Ethernet support through the debug board
	 * CS1 timings setup
	 */
	omap_writel(0x0000fff3, OMAP7XX_FLASH_CFG_1);
	omap_writel(0x00000000, OMAP7XX_FLASH_ACFG_1);

	/*
	 * Configure MPU_EXT_NIRQ IO in IO_CONF9 register,
	 * It is used as the Ethernet controller interrupt
	 */
	omap_writel(omap_readl(OMAP7XX_IO_CONF_9) & 0x1FFFFFFF,
				OMAP7XX_IO_CONF_9);

	perseus2_init_smc91x();

	BUG_ON(gpio_request(P2_NAND_RB_GPIO_PIN, "NAND ready") < 0);
	gpio_direction_input(P2_NAND_RB_GPIO_PIN);

	omap_cfg_reg(L3_1610_FLASH_CS2B_OE);
	omap_cfg_reg(M8_1610_FLASH_CS2B_WE);

	/* Mux pins for keypad */
	omap_cfg_reg(E2_7XX_KBR0);
	omap_cfg_reg(J7_7XX_KBR1);
	omap_cfg_reg(E1_7XX_KBR2);
	omap_cfg_reg(F3_7XX_KBR3);
	omap_cfg_reg(D2_7XX_KBR4);
	omap_cfg_reg(C2_7XX_KBC0);
	omap_cfg_reg(D3_7XX_KBC1);
	omap_cfg_reg(E4_7XX_KBC2);
	omap_cfg_reg(F4_7XX_KBC3);
	omap_cfg_reg(E3_7XX_KBC4);

	platform_add_devices(devices, ARRAY_SIZE(devices));

	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);

	omapfb_set_lcd_config(&perseus2_lcd_config);
}

/* Only FPGA needs to be mapped here. All others are done with ioremap */
static struct map_desc omap_perseus2_io_desc[] __initdata = {
	{
		.virtual	= H2P2_DBG_FPGA_BASE,
		.pfn		= __phys_to_pfn(H2P2_DBG_FPGA_START),
		.length		= H2P2_DBG_FPGA_SIZE,
		.type		= MT_DEVICE
	}
};

static void __init omap_perseus2_map_io(void)
{
	omap7xx_map_io();
	iotable_init(omap_perseus2_io_desc,
		     ARRAY_SIZE(omap_perseus2_io_desc));
}

MACHINE_START(OMAP_PERSEUS2, "OMAP730 Perseus2")
	/* Maintainer: Kevin Hilman <kjh@hilman.org> */
	.atag_offset	= 0x100,
	.map_io		= omap_perseus2_map_io,
	.init_early     = omap1_init_early,
	.init_irq	= omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine	= omap_perseus2_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
