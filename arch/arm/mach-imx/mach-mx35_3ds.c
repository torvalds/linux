/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009 Marc Kleine-Budde, Pengutronix
 *
 * Author: Fabio Estevam <fabio.estevam@freescale.com>
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

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx35.h>
#include <mach/irqs.h>
#include <mach/3ds_debugboard.h>

#include "devices-imx35.h"

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
}

static void __init mx35pdk_timer_init(void)
{
	mx35_clocks_init();
}

struct sys_timer mx35pdk_timer = {
	.init	= mx35pdk_timer_init,
};

MACHINE_START(MX35_3DS, "Freescale MX35PDK")
	/* Maintainer: Freescale Semiconductor, Inc */
	.atag_offset = 0x100,
	.map_io = mx35_map_io,
	.init_early = imx35_init_early,
	.init_irq = mx35_init_irq,
	.handle_irq = imx35_handle_irq,
	.timer = &mx35pdk_timer,
	.init_machine = mx35_3ds_init,
	.restart	= mxc_restart,
MACHINE_END
