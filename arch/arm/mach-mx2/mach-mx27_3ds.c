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

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx27.h>
#include <mach/board-mx27pdk.h>

#include "devices.h"

static unsigned int mx27pdk_pins[] = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,
	/* FEC */
	PD0_AIN_FEC_TXD0,
	PD1_AIN_FEC_TXD1,
	PD2_AIN_FEC_TXD2,
	PD3_AIN_FEC_TXD3,
	PD4_AOUT_FEC_RX_ER,
	PD5_AOUT_FEC_RXD1,
	PD6_AOUT_FEC_RXD2,
	PD7_AOUT_FEC_RXD3,
	PD8_AF_FEC_MDIO,
	PD9_AIN_FEC_MDC,
	PD10_AOUT_FEC_CRS,
	PD11_AOUT_FEC_TX_CLK,
	PD12_AOUT_FEC_RXD0,
	PD13_AOUT_FEC_RX_DV,
	PD14_AOUT_FEC_RX_CLK,
	PD15_AOUT_FEC_COL,
	PD16_AIN_FEC_TX_ER,
	PF23_AIN_FEC_TX_EN,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *platform_devices[] __initdata = {
	&mxc_fec_device,
};

static void __init mx27pdk_init(void)
{
	mxc_gpio_setup_multiple_pins(mx27pdk_pins, ARRAY_SIZE(mx27pdk_pins),
		"mx27pdk");
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}

static void __init mx27pdk_timer_init(void)
{
	mx27_clocks_init(26000000);
}

static struct sys_timer mx27pdk_timer = {
	.init	= mx27pdk_timer_init,
};

MACHINE_START(MX27_3DS, "Freescale MX27PDK")
	/* maintainer: Freescale Semiconductor, Inc. */
	.phys_io        = MX27_AIPI_BASE_ADDR,
	.io_pg_offst    = ((MX27_AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = MX27_PHYS_OFFSET + 0x100,
	.map_io         = mx27_map_io,
	.init_irq       = mx27_init_irq,
	.init_machine   = mx27pdk_init,
	.timer          = &mx27pdk_timer,
MACHINE_END
