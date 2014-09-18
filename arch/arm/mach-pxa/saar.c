/*
 *  linux/arch/arm/mach-pxa/saar.c
 *
 *  Support for the Marvell PXA930 Handheld Platform (aka SAAR)
 *
 *  Copyright (C) 2007-2008 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/smc91x.h>
#include <linux/mfd/da903x.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/onenand.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>

#include <mach/pxa930.h>
#include <linux/platform_data/video-pxafb.h>

#include "devices.h"
#include "generic.h"

#define GPIO_LCD_RESET		(16)

/* SAAR MFP configurations */
static mfp_cfg_t saar_mfp_cfg[] __initdata = {
	/* LCD */
	GPIO23_LCD_DD0,
	GPIO24_LCD_DD1,
	GPIO25_LCD_DD2,
	GPIO26_LCD_DD3,
	GPIO27_LCD_DD4,
	GPIO28_LCD_DD5,
	GPIO29_LCD_DD6,
	GPIO44_LCD_DD7,
	GPIO21_LCD_CS,
	GPIO22_LCD_VSYNC,
	GPIO17_LCD_FCLK_RD,
	GPIO18_LCD_LCLK_A0,
	GPIO19_LCD_PCLK_WR,
	GPIO16_GPIO, /* LCD reset */

	/* Ethernet */
	DF_nCS1_nCS3,
	GPIO97_GPIO,

	/* DFI */
	DF_INT_RnB_ND_INT_RnB,
	DF_nRE_nOE_ND_nRE,
	DF_nWE_ND_nWE,
	DF_CLE_nOE_ND_CLE,
	DF_nADV1_ALE_ND_ALE,
	DF_nADV2_ALE_nCS3,
	DF_nCS0_ND_nCS0,
	DF_IO0_ND_IO0,
	DF_IO1_ND_IO1,
	DF_IO2_ND_IO2,
	DF_IO3_ND_IO3,
	DF_IO4_ND_IO4,
	DF_IO5_ND_IO5,
	DF_IO6_ND_IO6,
	DF_IO7_ND_IO7,
	DF_IO8_ND_IO8,
	DF_IO9_ND_IO9,
	DF_IO10_ND_IO10,
	DF_IO11_ND_IO11,
	DF_IO12_ND_IO12,
	DF_IO13_ND_IO13,
	DF_IO14_ND_IO14,
	DF_IO15_ND_IO15,
};

#define SAAR_ETH_PHYS	(0x14000000)

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= (SAAR_ETH_PHYS + 0x300),
		.end	= (SAAR_ETH_PHYS + 0xfffff),
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= PXA_GPIO_TO_IRQ(mfp_to_gpio(MFP_PIN_GPIO97)),
		.end	= PXA_GPIO_TO_IRQ(mfp_to_gpio(MFP_PIN_GPIO97)),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct smc91x_platdata saar_smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT | SMC91X_USE_DMA,
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &saar_smc91x_info,
	},
};

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static uint16_t lcd_power_on[] = {
	/* single frame */
	SMART_CMD_NOOP,
	SMART_CMD(0x00),
	SMART_DELAY(0),

	SMART_CMD_NOOP,
	SMART_CMD(0x00),
	SMART_DELAY(0),

	SMART_CMD_NOOP,
	SMART_CMD(0x00),
	SMART_DELAY(0),

	SMART_CMD_NOOP,
	SMART_CMD(0x00),
	SMART_DELAY(10),

	/* calibration control */
	SMART_CMD(0x00),
	SMART_CMD(0xA4),
	SMART_DAT(0x80),
	SMART_DAT(0x01),
	SMART_DELAY(150),

	/*Power-On Init sequence*/
	SMART_CMD(0x00),	/* output ctrl */
	SMART_CMD(0x01),
	SMART_DAT(0x01),
	SMART_DAT(0x00),
	SMART_CMD(0x00),	/* wave ctrl */
	SMART_CMD(0x02),
	SMART_DAT(0x07),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x03),	/* entry mode */
	SMART_DAT(0xD0),
	SMART_DAT(0x30),
	SMART_CMD(0x00),
	SMART_CMD(0x08),	/* display ctrl 2 */
	SMART_DAT(0x08),
	SMART_DAT(0x08),
	SMART_CMD(0x00),
	SMART_CMD(0x09),	/* display ctrl 3 */
	SMART_DAT(0x04),
	SMART_DAT(0x2F),
	SMART_CMD(0x00),
	SMART_CMD(0x0A),	/* display ctrl 4 */
	SMART_DAT(0x00),
	SMART_DAT(0x08),
	SMART_CMD(0x00),
	SMART_CMD(0x0D),	/* Frame Marker position */
	SMART_DAT(0x00),
	SMART_DAT(0x08),
	SMART_CMD(0x00),
	SMART_CMD(0x60),	/* Driver output control */
	SMART_DAT(0x27),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x61),	/* Base image display control */
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_CMD(0x00),
	SMART_CMD(0x30),	/* Y settings 30h-3Dh */
	SMART_DAT(0x07),
	SMART_DAT(0x07),
	SMART_CMD(0x00),
	SMART_CMD(0x31),
	SMART_DAT(0x00),
	SMART_DAT(0x07),
	SMART_CMD(0x00),
	SMART_CMD(0x32),	/* Timing(3), ASW HOLD=0.5CLK */
	SMART_DAT(0x04),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x33),	/* Timing(4), CKV ST=0CLK, CKV ED=1CLK */
	SMART_DAT(0x03),
	SMART_DAT(0x03),
	SMART_CMD(0x00),
	SMART_CMD(0x34),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x35),
	SMART_DAT(0x02),
	SMART_DAT(0x05),
	SMART_CMD(0x00),
	SMART_CMD(0x36),
	SMART_DAT(0x1F),
	SMART_DAT(0x1F),
	SMART_CMD(0x00),
	SMART_CMD(0x37),
	SMART_DAT(0x07),
	SMART_DAT(0x07),
	SMART_CMD(0x00),
	SMART_CMD(0x38),
	SMART_DAT(0x00),
	SMART_DAT(0x07),
	SMART_CMD(0x00),
	SMART_CMD(0x39),
	SMART_DAT(0x04),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x3A),
	SMART_DAT(0x03),
	SMART_DAT(0x03),
	SMART_CMD(0x00),
	SMART_CMD(0x3B),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),
	SMART_CMD(0x3C),
	SMART_DAT(0x02),
	SMART_DAT(0x05),
	SMART_CMD(0x00),
	SMART_CMD(0x3D),
	SMART_DAT(0x1F),
	SMART_DAT(0x1F),
	SMART_CMD(0x00),	/* Display control 1 */
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_CMD(0x00),	/* Power control 5 */
	SMART_CMD(0x17),
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_CMD(0x00),	/* Power control 1 */
	SMART_CMD(0x10),
	SMART_DAT(0x10),
	SMART_DAT(0xB0),
	SMART_CMD(0x00),	/* Power control 2 */
	SMART_CMD(0x11),
	SMART_DAT(0x01),
	SMART_DAT(0x30),
	SMART_CMD(0x00),	/* Power control 3 */
	SMART_CMD(0x12),
	SMART_DAT(0x01),
	SMART_DAT(0x9E),
	SMART_CMD(0x00),	/* Power control 4 */
	SMART_CMD(0x13),
	SMART_DAT(0x17),
	SMART_DAT(0x00),
	SMART_CMD(0x00),	/* Power control 3 */
	SMART_CMD(0x12),
	SMART_DAT(0x01),
	SMART_DAT(0xBE),
	SMART_DELAY(100),

	/* display mode : 240*320 */
	SMART_CMD(0x00),	/* RAM address set(H) 0*/
	SMART_CMD(0x20),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),	/* RAM address set(V)   4*/
	SMART_CMD(0x21),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),	/* Start of Window RAM address set(H) 8*/
	SMART_CMD(0x50),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00), 	/* End of Window RAM address set(H) 12*/
	SMART_CMD(0x51),
	SMART_DAT(0x00),
	SMART_DAT(0xEF),
	SMART_CMD(0x00),	/* Start of Window RAM address set(V) 16*/
	SMART_CMD(0x52),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00),	/* End of Window RAM address set(V) 20*/
	SMART_CMD(0x53),
	SMART_DAT(0x01),
	SMART_DAT(0x3F),
	SMART_CMD(0x00), 	/* Panel interface control 1 */
	SMART_CMD(0x90),
	SMART_DAT(0x00),
	SMART_DAT(0x1A),
	SMART_CMD(0x00), 	/* Panel interface control 2 */
	SMART_CMD(0x92),
	SMART_DAT(0x04),
	SMART_DAT(0x00),
	SMART_CMD(0x00), 	/* Panel interface control 3 */
	SMART_CMD(0x93),
	SMART_DAT(0x00),
	SMART_DAT(0x05),
	SMART_DELAY(20),
};

static uint16_t lcd_panel_on[] = {
	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x21),
	SMART_DELAY(1),

	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x61),
	SMART_DELAY(100),

	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x01),
	SMART_DAT(0x73),
	SMART_DELAY(1),
};

static uint16_t lcd_panel_off[] = {
	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x72),
	SMART_DELAY(40),

	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x01),
	SMART_DELAY(1),

	SMART_CMD(0x00),
	SMART_CMD(0x07),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_DELAY(1),
};

static uint16_t lcd_power_off[] = {
	SMART_CMD(0x00),
	SMART_CMD(0x10),
	SMART_DAT(0x00),
	SMART_DAT(0x80),

	SMART_CMD(0x00),
	SMART_CMD(0x11),
	SMART_DAT(0x01),
	SMART_DAT(0x60),

	SMART_CMD(0x00),
	SMART_CMD(0x12),
	SMART_DAT(0x01),
	SMART_DAT(0xAE),
	SMART_DELAY(40),

	SMART_CMD(0x00),
	SMART_CMD(0x10),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
};

static uint16_t update_framedata[] = {
	/* set display ram: 240*320 */
	SMART_CMD(0x00), /* RAM address set(H) 0*/
	SMART_CMD(0x20),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00), /* RAM address set(V) 4*/
	SMART_CMD(0x21),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00), /* Start of Window RAM address set(H) 8 */
	SMART_CMD(0x50),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00), /* End of Window RAM address set(H) 12 */
	SMART_CMD(0x51),
	SMART_DAT(0x00),
	SMART_DAT(0xEF),
	SMART_CMD(0x00), /* Start of Window RAM address set(V) 16 */
	SMART_CMD(0x52),
	SMART_DAT(0x00),
	SMART_DAT(0x00),
	SMART_CMD(0x00), /* End of Window RAM address set(V) 20 */
	SMART_CMD(0x53),
	SMART_DAT(0x01),
	SMART_DAT(0x3F),

	/* wait for vsync cmd before transferring frame data */
	SMART_CMD_WAIT_FOR_VSYNC,

	/* write ram */
	SMART_CMD(0x00),
	SMART_CMD(0x22),

	/* write frame data */
	SMART_CMD_WRITE_FRAME,
};

static void ltm022a97a_lcd_power(int on, struct fb_var_screeninfo *var)
{
	static int pin_requested = 0;
	struct fb_info *info = container_of(var, struct fb_info, var);
	int err;

	if (!pin_requested) {
		err = gpio_request(GPIO_LCD_RESET, "lcd reset");
		if (err) {
			pr_err("failed to request gpio for LCD reset\n");
			return;
		}

		gpio_direction_output(GPIO_LCD_RESET, 0);
		pin_requested = 1;
	}

	if (on) {
		gpio_set_value(GPIO_LCD_RESET, 0); msleep(100);
		gpio_set_value(GPIO_LCD_RESET, 1); msleep(10);

		pxafb_smart_queue(info, ARRAY_AND_SIZE(lcd_power_on));
		pxafb_smart_queue(info, ARRAY_AND_SIZE(lcd_panel_on));
	} else {
		pxafb_smart_queue(info, ARRAY_AND_SIZE(lcd_panel_off));
		pxafb_smart_queue(info, ARRAY_AND_SIZE(lcd_power_off));
	}

	err = pxafb_smart_flush(info);
	if (err)
		pr_err("%s: timed out\n", __func__);
}

static void ltm022a97a_update(struct fb_info *info)
{
	pxafb_smart_queue(info, ARRAY_AND_SIZE(update_framedata));
	pxafb_smart_flush(info);
}

static struct pxafb_mode_info toshiba_ltm022a97a_modes[] = {
	[0] = {
		.xres			= 240,
		.yres			= 320,
		.bpp			= 16,
		.a0csrd_set_hld		= 30,
		.a0cswr_set_hld		= 30,
		.wr_pulse_width		= 30,
		.rd_pulse_width 	= 30,
		.op_hold_time 		= 30,
		.cmd_inh_time		= 60,

		/* L_LCLK_A0 and L_LCLK_RD active low */
		.sync			= FB_SYNC_HOR_HIGH_ACT |
					  FB_SYNC_VERT_HIGH_ACT,
	},
};

static struct pxafb_mach_info saar_lcd_info = {
	.modes			= toshiba_ltm022a97a_modes,
	.num_modes		= 1,
	.lcd_conn		= LCD_SMART_PANEL_8BPP | LCD_PCLK_EDGE_FALL,
	.pxafb_lcd_power	= ltm022a97a_lcd_power,
	.smart_update		= ltm022a97a_update,
};

static void __init saar_init_lcd(void)
{
	pxa_set_fb_info(NULL, &saar_lcd_info);
}
#else
static inline void saar_init_lcd(void) {}
#endif

#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct da9034_backlight_pdata saar_da9034_backlight = {
	.output_current	= 4,	/* 4mA */
};

static struct da903x_subdev_info saar_da9034_subdevs[] = {
	[0] = {
		.name		= "da903x-backlight",
		.id		= DA9034_ID_WLED,
		.platform_data	= &saar_da9034_backlight,
	},
};

static struct da903x_platform_data saar_da9034_info = {
	.num_subdevs	= ARRAY_SIZE(saar_da9034_subdevs),
	.subdevs	= saar_da9034_subdevs,
};

static struct i2c_board_info saar_i2c_info[] = {
	[0] = {
		.type		= "da9034",
		.addr		= 0x34,
		.platform_data	= &saar_da9034_info,
		.irq		= PXA_GPIO_TO_IRQ(mfp_to_gpio(MFP_PIN_GPIO83)),
	},
};

static void __init saar_init_i2c(void)
{
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(saar_i2c_info));
}
#else
static inline void saar_init_i2c(void) {}
#endif

#if defined(CONFIG_MTD_ONENAND) || defined(CONFIG_MTD_ONENAND_MODULE)
static struct mtd_partition saar_onenand_partitions[] = {
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_8M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= (SZ_2M + SZ_1M),
		.mask_flags	= 0,
	}, {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_32M + SZ_16M,
		.mask_flags	= 0,
	}
};

static struct onenand_platform_data saar_onenand_info = {
	.parts		= saar_onenand_partitions,
	.nr_parts	= ARRAY_SIZE(saar_onenand_partitions),
};

#define SMC_CS0_PHYS_BASE	(0x10000000)

static struct resource saar_resource_onenand[] = {
	[0] = {
		.start	= SMC_CS0_PHYS_BASE,
		.end	= SMC_CS0_PHYS_BASE + SZ_1M,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device saar_device_onenand = {
	.name		= "onenand-flash",
	.id		= -1,
	.dev		= {
		.platform_data	= &saar_onenand_info,
	},
	.resource	= saar_resource_onenand,
	.num_resources	= ARRAY_SIZE(saar_resource_onenand),
};

static void __init saar_init_onenand(void)
{
	platform_device_register(&saar_device_onenand);
}
#else
static void __init saar_init_onenand(void) {}
#endif

static void __init saar_init(void)
{
	/* initialize MFP configurations */
	pxa3xx_mfp_config(ARRAY_AND_SIZE(saar_mfp_cfg));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	platform_device_register(&smc91x_device);
	saar_init_onenand();

	saar_init_i2c();
	saar_init_lcd();
}

MACHINE_START(SAAR, "PXA930 Handheld Platform (aka SAAR)")
	/* Maintainer: Eric Miao <eric.miao@marvell.com> */
	.atag_offset    = 0x100,
	.map_io         = pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq       = pxa3xx_init_irq,
	.handle_irq       = pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine   = saar_init,
	.restart	= pxa_restart,
MACHINE_END
