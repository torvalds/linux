/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Marc Kleine-Budde, Pengutronix
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
 *
 * Copyright (C) 2011 Meprolight, Ltd.
 * Alex Gershgorin <alexg@meprolight.com>
 *
 * Modified from i.MX31 3-Stack Development System
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

/*
 * This machine is known as:
 *  - i.MX35 3-Stack Development System
 *  - i.MX35 Platform Development Kit (i.MX35 PDK)
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>

#include <linux/mtd/physmap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <asm/memblock.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>
#include <mach/irqs.h>
#include <mach/3ds_debugboard.h>
#include <video/platform_lcd.h>

#include <media/soc_camera.h>

#include "devices-imx35.h"

#define GPIO_MC9S08DZ60_GPS_ENABLE 0
#define GPIO_MC9S08DZ60_HDD_ENABLE 4
#define GPIO_MC9S08DZ60_WIFI_ENABLE 5
#define GPIO_MC9S08DZ60_LCD_ENABLE 6
#define GPIO_MC9S08DZ60_SPEAKER_ENABLE 8

static const struct fb_videomode fb_modedb[] = {
	{
		 /* 800x480 @ 55 Hz */
		.name = "Ceramate-CLAA070VC01",
		.refresh = 55,
		.xres = 800,
		.yres = 480,
		.pixclock = 40000,
		.left_margin = 40,
		.right_margin = 40,
		.upper_margin = 5,
		.lower_margin = 5,
		.hsync_len = 20,
		.vsync_len = 10,
		.sync = FB_SYNC_OE_ACT_HIGH,
		.vmode = FB_VMODE_NONINTERLACED,
		.flag = 0,
	 },
};

static const struct ipu_platform_data mx3_ipu_data __initconst = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct mx3fb_platform_data mx3fb_pdata __initdata = {
	.name = "Ceramate-CLAA070VC01",
	.mode = fb_modedb,
	.num_modes = ARRAY_SIZE(fb_modedb),
};

static struct i2c_board_info __initdata i2c_devices_3ds[] = {
	{
		I2C_BOARD_INFO("mc9s08dz60", 0x69),
	},
};

static int lcd_power_gpio = -ENXIO;

static int mc9s08dz60_gpiochip_match(struct gpio_chip *chip,
						     const void *data)
{
	return !strcmp(chip->label, data);
}

static void mx35_3ds_lcd_set_power(
				struct plat_lcd_data *pd, unsigned int power)
{
	struct gpio_chip *chip;

	if (!gpio_is_valid(lcd_power_gpio)) {
		chip = gpiochip_find(
				"mc9s08dz60", mc9s08dz60_gpiochip_match);
		if (chip) {
			lcd_power_gpio =
				chip->base + GPIO_MC9S08DZ60_LCD_ENABLE;
			if (gpio_request(lcd_power_gpio, "lcd_power") < 0) {
				pr_err("error: gpio already requested!\n");
				lcd_power_gpio = -ENXIO;
			}
		} else {
			pr_err("error: didn't find mc9s08dz60 gpio chip\n");
		}
	}

	if (gpio_is_valid(lcd_power_gpio))
		gpio_set_value_cansleep(lcd_power_gpio, power);
}

static struct plat_lcd_data mx35_3ds_lcd_data = {
	.set_power = mx35_3ds_lcd_set_power,
};

static struct platform_device mx35_3ds_lcd = {
	.name = "platform-lcd",
	.dev.platform_data = &mx35_3ds_lcd_data,
};

#define EXPIO_PARENT_INT	gpio_to_irq(IMX_GPIO_NR(1, 1))

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct physmap_flash_data mx35pdk_flash_data = {
	.width  = 2,
};

static struct resource mx35pdk_flash_resource = {
	.start	= MX35_CS0_BASE_ADDR,
	.end	= MX35_CS0_BASE_ADDR + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device mx35pdk_flash = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev	= {
		.platform_data  = &mx35pdk_flash_data,
	},
	.resource = &mx35pdk_flash_resource,
	.num_resources = 1,
};

static const struct mxc_nand_platform_data mx35pdk_nand_board_info __initconst = {
	.width = 1,
	.hw_ecc = 1,
	.flash_bbt = 1,
};

static struct platform_device *devices[] __initdata = {
	&mx35pdk_flash,
};

static iomux_v3_cfg_t mx35pdk_pads[] = {
	/* UART1 */
	MX35_PAD_CTS1__UART1_CTS,
	MX35_PAD_RTS1__UART1_RTS,
	MX35_PAD_TXD1__UART1_TXD_MUX,
	MX35_PAD_RXD1__UART1_RXD_MUX,
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
	/* USBOTG */
	MX35_PAD_USBOTG_PWR__USB_TOP_USBOTG_PWR,
	MX35_PAD_USBOTG_OC__USB_TOP_USBOTG_OC,
	/* USBH1 */
	MX35_PAD_I2C2_CLK__USB_TOP_USBH2_PWR,
	MX35_PAD_I2C2_DAT__USB_TOP_USBH2_OC,
	/* SDCARD */
	MX35_PAD_SD1_CMD__ESDHC1_CMD,
	MX35_PAD_SD1_CLK__ESDHC1_CLK,
	MX35_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX35_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX35_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX35_PAD_SD1_DATA3__ESDHC1_DAT3,
	/* I2C1 */
	MX35_PAD_I2C1_CLK__I2C1_SCL,
	MX35_PAD_I2C1_DAT__I2C1_SDA,
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
	MX35_PAD_D3_HSYNC__IPU_DISPB_D3_HSYNC,
	MX35_PAD_D3_FPSHIFT__IPU_DISPB_D3_CLK,
	MX35_PAD_D3_DRDY__IPU_DISPB_D3_DRDY,
	MX35_PAD_CONTRAST__IPU_DISPB_CONTR,
	MX35_PAD_D3_VSYNC__IPU_DISPB_D3_VSYNC,
	MX35_PAD_D3_REV__IPU_DISPB_D3_REV,
	MX35_PAD_D3_CLS__IPU_DISPB_D3_CLS,
	/* CSI */
	MX35_PAD_TX1__IPU_CSI_D_6,
	MX35_PAD_TX0__IPU_CSI_D_7,
	MX35_PAD_CSI_D8__IPU_CSI_D_8,
	MX35_PAD_CSI_D9__IPU_CSI_D_9,
	MX35_PAD_CSI_D10__IPU_CSI_D_10,
	MX35_PAD_CSI_D11__IPU_CSI_D_11,
	MX35_PAD_CSI_D12__IPU_CSI_D_12,
	MX35_PAD_CSI_D13__IPU_CSI_D_13,
	MX35_PAD_CSI_D14__IPU_CSI_D_14,
	MX35_PAD_CSI_D15__IPU_CSI_D_15,
	MX35_PAD_CSI_HSYNC__IPU_CSI_HSYNC,
	MX35_PAD_CSI_MCLK__IPU_CSI_MCLK,
	MX35_PAD_CSI_PIXCLK__IPU_CSI_PIXCLK,
	MX35_PAD_CSI_VSYNC__IPU_CSI_VSYNC,
};

/*
 * Camera support
*/
static phys_addr_t mx3_camera_base __initdata;
#define MX35_3DS_CAMERA_BUF_SIZE SZ_8M

static const struct mx3_camera_pdata mx35_3ds_camera_pdata __initconst = {
	.flags = MX3_CAMERA_DATAWIDTH_8,
	.mclk_10khz = 2000,
};

static int __init imx35_3ds_init_camera(void)
{
	int dma, ret = -ENOMEM;
	struct platform_device *pdev =
		imx35_alloc_mx3_camera(&mx35_3ds_camera_pdata);

	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!mx3_camera_base)
		goto err;

	dma = dma_declare_coherent_memory(&pdev->dev,
					mx3_camera_base, mx3_camera_base,
					MX35_3DS_CAMERA_BUF_SIZE,
					DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE);

	if (!(dma & DMA_MEMORY_MAP))
		goto err;

	ret = platform_device_add(pdev);
	if (ret)
err:
		platform_device_put(pdev);

	return ret;
}

static const struct ipu_platform_data mx35_3ds_ipu_data __initconst = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct i2c_board_info mx35_3ds_i2c_camera = {
	I2C_BOARD_INFO("ov2640", 0x30),
};

static struct soc_camera_link iclink_ov2640 = {
	.bus_id		= 0,
	.board_info	= &mx35_3ds_i2c_camera,
	.i2c_adapter_id	= 0,
	.power		= NULL,
};

static struct platform_device mx35_3ds_ov2640 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &iclink_ov2640,
	},
};

static int mx35_3ds_otg_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id, MXC_EHCI_INTERNAL_PHY);
}

/* OTG config */
static const struct fsl_usb2_platform_data usb_otg_pdata __initconst = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI_WIDE,
	.workaround	= FLS_USB2_WORKAROUND_ENGCM09152,
/*
 * ENGCM09152 also requires a hardware change.
 * Please check the MX35 Chip Errata document for details.
 */
};

static struct mxc_usbh_platform_data otg_pdata __initdata = {
	.init	= mx35_3ds_otg_init,
	.portsc	= MXC_EHCI_MODE_UTMI,
};

static int mx35_3ds_usbh_init(struct platform_device *pdev)
{
	return mx35_initialize_usb_hw(pdev->id, MXC_EHCI_INTERFACE_SINGLE_UNI |
			  MXC_EHCI_INTERNAL_PHY);
}

/* USB HOST config */
static const struct mxc_usbh_platform_data usb_host_pdata __initconst = {
	.init		= mx35_3ds_usbh_init,
	.portsc		= MXC_EHCI_MODE_SERIAL,
};

static int otg_mode_host;

static int __init mx35_3ds_otg_mode(char *options)
{
	if (!strcmp(options, "host"))
		otg_mode_host = 1;
	else if (!strcmp(options, "device"))
		otg_mode_host = 0;
	else
		pr_info("otg_mode neither \"host\" nor \"device\". "
			"Defaulting to device\n");
	return 0;
}
__setup("otg_mode=", mx35_3ds_otg_mode);

static const struct imxi2c_platform_data mx35_3ds_i2c0_data __initconst = {
	.bitrate = 100000,
};

/*
 * Board specific initialization.
 */
static void __init mx35_3ds_init(void)
{
	struct platform_device *imx35_fb_pdev;

	imx35_soc_init();

	mxc_iomux_v3_setup_multiple_pads(mx35pdk_pads, ARRAY_SIZE(mx35pdk_pads));

	imx35_add_fec(NULL);
	imx35_add_imx2_wdt(NULL);
	platform_add_devices(devices, ARRAY_SIZE(devices));

	imx35_add_imx_uart0(&uart_pdata);

	if (otg_mode_host)
		imx35_add_mxc_ehci_otg(&otg_pdata);

	imx35_add_mxc_ehci_hs(&usb_host_pdata);

	if (!otg_mode_host)
		imx35_add_fsl_usb2_udc(&usb_otg_pdata);

	imx35_add_mxc_nand(&mx35pdk_nand_board_info);
	imx35_add_sdhci_esdhc_imx(0, NULL);

	if (mxc_expio_init(MX35_CS5_BASE_ADDR, EXPIO_PARENT_INT))
		pr_warn("Init of the debugboard failed, all "
				"devices on the debugboard are unusable.\n");
	imx35_add_imx_i2c0(&mx35_3ds_i2c0_data);

	i2c_register_board_info(
		0, i2c_devices_3ds, ARRAY_SIZE(i2c_devices_3ds));

	imx35_add_ipu_core(&mx35_3ds_ipu_data);
	platform_device_register(&mx35_3ds_ov2640);
	imx35_3ds_init_camera();

	imx35_fb_pdev = imx35_add_mx3_sdc_fb(&mx3fb_pdata);
	mx35_3ds_lcd.dev.parent = &imx35_fb_pdev->dev;
	platform_device_register(&mx35_3ds_lcd);
}

static void __init mx35pdk_timer_init(void)
{
	mx35_clocks_init();
}

struct sys_timer mx35pdk_timer = {
	.init	= mx35pdk_timer_init,
};

static void __init mx35_3ds_reserve(void)
{
	/* reserve MX35_3DS_CAMERA_BUF_SIZE bytes for mx3-camera */
	mx3_camera_base = arm_memblock_steal(MX35_3DS_CAMERA_BUF_SIZE,
					 MX35_3DS_CAMERA_BUF_SIZE);
}

MACHINE_START(MX35_3DS, "Freescale MX35PDK")
	/* Maintainer: Freescale Semiconductor, Inc */
	.atag_offset = 0x100,
	.map_io = mx35_map_io,
	.init_early = imx35_init_early,
	.init_irq = mx35_init_irq,
	.handle_irq = imx35_handle_irq,
	.timer = &mx35pdk_timer,
	.init_machine = mx35_3ds_init,
	.reserve = mx35_3ds_reserve,
	.restart	= mxc_restart,
MACHINE_END
