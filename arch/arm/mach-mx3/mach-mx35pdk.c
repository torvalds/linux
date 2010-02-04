/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/memory.h>
#include <linux/gpio.h>
#include <linux/fsl_devices.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx35.h>

#include "devices.h"

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *devices[] __initdata = {
	&mxc_fec_device,
};

static struct pad_desc mx35pdk_pads[] = {
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
};

/* OTG config */
static struct fsl_usb2_platform_data usb_pdata = {
	.operating_mode	= FSL_USB2_DR_DEVICE,
	.phy_mode	= FSL_USB2_PHY_UTMI_WIDE,
};

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx35pdk_pads, ARRAY_SIZE(mx35pdk_pads));

	platform_add_devices(devices, ARRAY_SIZE(devices));

	mxc_register_device(&mxc_uart_device0, &uart_pdata);

	mxc_register_device(&mxc_otg_udc_device, &usb_pdata);
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
	.phys_io	= MX35_AIPS1_BASE_ADDR,
	.io_pg_offst	= ((MX35_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX3x_PHYS_OFFSET + 0x100,
	.map_io         = mx35_map_io,
	.init_irq       = mx35_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx35pdk_timer,
MACHINE_END
