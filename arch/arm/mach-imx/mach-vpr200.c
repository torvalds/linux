/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Marc Kleine-Budde, Pengutronix
 * Copyright 2010 Creative Product Design
 *
 * Derived from mx35 3stack.
 * Original author: Fabio Estevam <fabio.estevam@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/memory.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>
#include <mach/irqs.h>

#include <linux/i2c.h>
#include <linux/i2c/at24.h>
#include <linux/mfd/mc13xxx.h>

#include "devices-imx35.h"

#define GPIO_LCDPWR	IMX_GPIO_NR(1, 2)
#define GPIO_PMIC_INT	IMX_GPIO_NR(2, 0)

#define GPIO_BUTTON1	IMX_GPIO_NR(1, 4)
#define GPIO_BUTTON2	IMX_GPIO_NR(1, 5)
#define GPIO_BUTTON3	IMX_GPIO_NR(1, 7)
#define GPIO_BUTTON4	IMX_GPIO_NR(1, 8)
#define GPIO_BUTTON5	IMX_GPIO_NR(1, 9)
#define GPIO_BUTTON6	IMX_GPIO_NR(1, 10)
#define GPIO_BUTTON7	IMX_GPIO_NR(1, 11)
#define GPIO_BUTTON8	IMX_GPIO_NR(1, 12)

static const struct fb_videomode fb_modedb[] = {
	{
		/* 800x480 @ 60 Hz */
		.name		= "PT0708048",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= KHZ2PICOS(33260),
		.left_margin	= 50,
		.right_margin	= 156,
		.upper_margin	= 10,
		.lower_margin	= 10,
		.hsync_len	= 1,	/* note: DE only display */
		.vsync_len	= 1,	/* note: DE only display */
		.sync		= FB_SYNC_CLK_IDLE_EN | FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {
		/* 800x480 @ 60 Hz */
		.name		= "CTP-CLAA070LC0ACW",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= KHZ2PICOS(27000),
		.left_margin	= 50,
		.right_margin	= 50,	/* whole line should have 900 clocks */
		.upper_margin	= 10,
		.lower_margin	= 10,	/* whole frame should have 500 lines */
		.hsync_len	= 1,	/* note: DE only display */
		.vsync_len	= 1,	/* note: DE only display */
		.sync		= FB_SYNC_CLK_IDLE_EN | FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}
};

static const struct ipu_platform_data mx3_ipu_data __initconst = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name		= "PT0708048",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

static struct physmap_flash_data vpr200_flash_data = {
	.width  = 2,
};

static struct resource vpr200_flash_resource = {
	.start	= MX35_CS0_BASE_ADDR,
	.end	= MX35_CS0_BASE_ADDR + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device vpr200_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &vpr200_flash_data,
	},
	.resource = &vpr200_flash_resource,
	.num_resources = 1,
};

static const struct mxc_nand_platform_data
		vpr200_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
	.flash_bbt = 1,
};

#define VPR_KEY_DEBOUNCE	500
static struct gpio_keys_button vpr200_gpio_keys_table[] = {
	{KEY_F2, GPIO_BUTTON1, 1, "vpr-keys: F2", 0, VPR_KEY_DEBOUNCE},
	{KEY_F3, GPIO_BUTTON2, 1, "vpr-keys: F3", 0, VPR_KEY_DEBOUNCE},
	{KEY_F4, GPIO_BUTTON3, 1, "vpr-keys: F4", 0, VPR_KEY_DEBOUNCE},
	{KEY_F5, GPIO_BUTTON4, 1, "vpr-keys: F5", 0, VPR_KEY_DEBOUNCE},
	{KEY_F6, GPIO_BUTTON5, 1, "vpr-keys: F6", 0, VPR_KEY_DEBOUNCE},
	{KEY_F7, GPIO_BUTTON6, 1, "vpr-keys: F7", 0, VPR_KEY_DEBOUNCE},
	{KEY_F8, GPIO_BUTTON7, 1, "vpr-keys: F8", 1, VPR_KEY_DEBOUNCE},
	{KEY_F9, GPIO_BUTTON8, 1, "vpr-keys: F9", 1, VPR_KEY_DEBOUNCE},
};

static const struct gpio_keys_platform_data
		vpr200_gpio_keys_data __initconst = {
	.buttons = vpr200_gpio_keys_table,
	.nbuttons = ARRAY_SIZE(vpr200_gpio_keys_table),
};

static struct mc13xxx_platform_data vpr200_pmic = {
	.flags = MC13XXX_USE_ADC | MC13XXX_USE_TOUCHSCREEN,
};

static const struct imxi2c_platform_data vpr200_i2c0_data __initconst = {
	.bitrate = 50000,
};

static struct at24_platform_data vpr200_eeprom = {
	.byte_len = 2048 / 8,
	.page_size = 1,
};

static struct i2c_board_info vpr200_i2c_devices[] = {
	{
		I2C_BOARD_INFO("at24", 0x50), /* E0=0, E1=0, E2=0 */
		.platform_data = &vpr200_eeprom,
	}, {
		I2C_BOARD_INFO("mc13892", 0x08),
		.platform_data = &vpr200_pmic,
		.irq = gpio_to_irq(GPIO_PMIC_INT),
	}
};

static iomux_v3_cfg_t vpr200_pads[] = {
	/* UART1 */
	MX35_PAD_TXD1__UART1_TXD_MUX,
	MX35_PAD_RXD1__UART1_RXD_MUX,
	/* UART3 */
	MX35_PAD_ATA_DATA10__UART3_RXD_MUX,
	MX35_PAD_ATA_DATA11__UART3_TXD_MUX,
	/* FEC */
	MX35_PAD_FEC_TX_CLK__FEC_TX_CLK,
	MX35_PAD_FEC_RX_CLK__FEC_RX_CLK,
	MX35_PAD_FEC_RX_DV__FEC_RX_DV,
	MX35_PAD_FEC_COL__FEC_COL,
	MX35_PAD_FEC_RDATA0__FEC_RDATA_0,
	MX35_PAD_FEC_TDATA0__FEC_TDATA_0,
	MX35_PAD_FEC_TX_EN__FEC_TX_EN,
	MX35_PAD_FEC_MDC__FEC_MDC,
	MX35_PAD_FEC_MDIO__FEC_MDIO,
	MX35_PAD_FEC_TX_ERR__FEC_TX_ERR,
	MX35_PAD_FEC_RX_ERR__FEC_RX_ERR,
	MX35_PAD_FEC_CRS__FEC_CRS,
	MX35_PAD_FEC_RDATA1__FEC_RDATA_1,
	MX35_PAD_FEC_TDATA1__FEC_TDATA_1,
	MX35_PAD_FEC_RDATA2__FEC_RDATA_2,
	MX35_PAD_FEC_TDATA2__FEC_TDATA_2,
	MX35_PAD_FEC_RDATA3__FEC_RDATA_3,
	MX35_PAD_FEC_TDATA3__FEC_TDATA_3,
	/* Display */
	MX35_PAD_LD0__IPU_DISPB_DAT_0,
	MX35_PAD_LD1__IPU_DISPB_DAT_1,
	MX35_PAD_LD2__IPU_DISPB_DAT_2,
	MX35_PAD_LD3__IPU_DISPB_DAT_3,
	MX35_PAD_LD4__IPU_DISPB_DAT_4,
	MX35_PAD_LD5__IPU_DISPB_DAT_5,
	MX35_PAD_LD6__IPU_DISPB_DAT_6,
	MX35_PAD_LD7__IPU_DISPB_DAT_7,
	MX35_PAD_LD8__IPU_DISPB_DAT_8,
	MX35_PAD_LD9__IPU_DISPB_DAT_9,
	MX35_PAD_LD10__IPU_DISPB_DAT_10,
	MX35_PAD_LD11__IPU_DISPB_DAT_11,
	MX35_PAD_LD12__IPU_DISPB_DAT_12,
	MX35_PAD_LD13__IPU_DISPB_DAT_13,
	MX35_PAD_LD14__IPU_DISPB_DAT_14,
	MX35_PAD_LD15__IPU_DISPB_DAT_15,
	MX35_PAD_LD16__IPU_DISPB_DAT_16,
	MX35_PAD_LD17__IPU_DISPB_DAT_17,
	MX35_PAD_D3_FPSHIFT__IPU_DISPB_D3_CLK,
	MX35_PAD_D3_DRDY__IPU_DISPB_D3_DRDY,
	MX35_PAD_CONTRAST__IPU_DISPB_CONTR,
	/* LCD Enable */
	MX35_PAD_D3_VSYNC__GPIO1_2,
	/* USBOTG */
	MX35_PAD_USBOTG_PWR__USB_TOP_USBOTG_PWR,
	MX35_PAD_USBOTG_OC__USB_TOP_USBOTG_OC,
	/* SDCARD */
	MX35_PAD_SD1_CMD__ESDHC1_CMD,
	MX35_PAD_SD1_CLK__ESDHC1_CLK,
	MX35_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX35_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX35_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX35_PAD_SD1_DATA3__ESDHC1_DAT3,
	/* PMIC */
	MX35_PAD_GPIO2_0__GPIO2_0,
	/* GPIO keys */
	MX35_PAD_SCKR__GPIO1_4,
	MX35_PAD_COMPARE__GPIO1_5,
	MX35_PAD_SCKT__GPIO1_7,
	MX35_PAD_FST__GPIO1_8,
	MX35_PAD_HCKT__GPIO1_9,
	MX35_PAD_TX5_RX0__GPIO1_10,
	MX35_PAD_TX4_RX1__GPIO1_11,
	MX35_PAD_TX3_RX2__GPIO1_12,
};

/* USB Device config */
static const struct fsl_usb2_platform_data otg_device_pdata __initconst = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI,
	.workaround	= FLS_USB2_WORKAROUND_ENGCM09152,
};

static int vpr200_usbh_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id,
			MXC_EHCI_INTERFACE_SINGLE_UNI | MXC_EHCI_INTERNAL_PHY);
}

/* USB HOST config */
static const struct mxc_usbh_platform_data usb_host_pdata __initconst = {
	.init = vpr200_usbh_init,
	.portsc = MXC_EHCI_MODE_SERIAL,
};

static struct platform_device *devices[] __initdata = {
	&vpr200_flash,
};

/*
 * Board specific initialization.
 */
static void __init vpr200_board_init(void)
{
	imx35_soc_init();

	mxc_iomux_v3_setup_multiple_pads(vpr200_pads, ARRAY_SIZE(vpr200_pads));

	imx35_add_fec(NULL);
	imx35_add_imx2_wdt(NULL);
	imx_add_gpio_keys(&vpr200_gpio_keys_data);

	platform_add_devices(devices, ARRAY_SIZE(devices));

	if (0 != gpio_request(GPIO_LCDPWR, "LCDPWR"))
		printk(KERN_WARNING "vpr200: Couldn't get LCDPWR gpio\n");
	else
		gpio_direction_output(GPIO_LCDPWR, 0);

	if (0 != gpio_request(GPIO_PMIC_INT, "PMIC_INT"))
		printk(KERN_WARNING "vpr200: Couldn't get PMIC_INT gpio\n");
	else
		gpio_direction_input(GPIO_PMIC_INT);

	imx35_add_imx_uart0(NULL);
	imx35_add_imx_uart2(NULL);

	imx35_add_ipu_core(&mx3_ipu_data);
	imx35_add_mx3_sdc_fb(&mx3fb_pdata);

	imx35_add_fsl_usb2_udc(&otg_device_pdata);
	imx35_add_mxc_ehci_hs(&usb_host_pdata);

	imx35_add_mxc_nand(&vpr200_nand_board_info);
	imx35_add_sdhci_esdhc_imx(0, NULL);

	i2c_register_board_info(0, vpr200_i2c_devices,
			ARRAY_SIZE(vpr200_i2c_devices));

	imx35_add_imx_i2c0(&vpr200_i2c0_data);
}

static void __init vpr200_timer_init(void)
{
	mx35_clocks_init();
}

struct sys_timer vpr200_timer = {
	.init	= vpr200_timer_init,
};

MACHINE_START(VPR200, "VPR200")
	/* Maintainer: Creative Product Design */
	.map_io = mx35_map_io,
	.init_early = imx35_init_early,
	.init_irq = mx35_init_irq,
	.timer = &vpr200_timer,
	.init_machine = vpr200_board_init,
MACHINE_END
