/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/iomux-mx27.h>

#include "devices-imx27.h"

static const int mx27ipcam_pins[] __initconst = {
	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
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

static void __init mx27ipcam_init(void)
{
	imx27_soc_init();

	mxc_gpio_setup_multiple_pins(mx27ipcam_pins, ARRAY_SIZE(mx27ipcam_pins),
		"mx27ipcam");

	imx27_add_imx_uart0(NULL);
	imx27_add_fec(NULL);
	imx27_add_imx2_wdt(NULL);
}

static void __init mx27ipcam_timer_init(void)
{
	mx27_clocks_init(25000000);
}

static struct sys_timer mx27ipcam_timer = {
	.init	= mx27ipcam_timer_init,
};

MACHINE_START(IMX27IPCAM, "Freescale IMX27IPCAM")
	/* maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx27_map_io,
	.init_early = imx27_init_early,
	.init_irq = mx27_init_irq,
	.handle_irq = imx27_handle_irq,
	.timer = &mx27ipcam_timer,
	.init_machine = mx27ipcam_init,
	.restart	= mxc_restart,
MACHINE_END
