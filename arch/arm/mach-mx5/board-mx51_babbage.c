/*
 * Copyright 2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2009-2010 Amit Kucheria <amit.kucheria@canonical.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/platform_device.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx51.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "devices.h"

static struct platform_device *devices[] __initdata = {
	&mxc_fec_device,
};

static struct pad_desc mx51babbage_pads[] = {
	/* UART1 */
	MX51_PAD_UART1_RXD__UART1_RXD,
	MX51_PAD_UART1_TXD__UART1_TXD,
	MX51_PAD_UART1_RTS__UART1_RTS,
	MX51_PAD_UART1_CTS__UART1_CTS,

	/* UART2 */
	MX51_PAD_UART2_RXD__UART2_RXD,
	MX51_PAD_UART2_TXD__UART2_TXD,

	/* UART3 */
	MX51_PAD_EIM_D25__UART3_RXD,
	MX51_PAD_EIM_D26__UART3_TXD,
	MX51_PAD_EIM_D27__UART3_RTS,
	MX51_PAD_EIM_D24__UART3_CTS,
};

/* Serial ports */
#if defined(CONFIG_SERIAL_IMX) || defined(CONFIG_SERIAL_IMX_MODULE)
static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static inline void mxc_init_imx_uart(void)
{
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_uart_device1, &uart_pdata);
	mxc_register_device(&mxc_uart_device2, &uart_pdata);
}
#else /* !SERIAL_IMX */
static inline void mxc_init_imx_uart(void)
{
}
#endif /* SERIAL_IMX */

/*
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx51babbage_pads,
					ARRAY_SIZE(mx51babbage_pads));
	mxc_init_imx_uart();
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init mx51_babbage_timer_init(void)
{
	mx51_clocks_init(32768, 24000000, 22579200, 0);
}

static struct sys_timer mxc_timer = {
	.init	= mx51_babbage_timer_init,
};

MACHINE_START(MX51_BABBAGE, "Freescale MX51 Babbage Board")
	/* Maintainer: Amit Kucheria <amit.kucheria@canonical.com> */
	.phys_io = MX51_AIPS1_BASE_ADDR,
	.io_pg_offst = ((MX51_AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = mx51_map_io,
	.init_irq = mx51_init_irq,
	.init_machine = mxc_board_init,
	.timer = &mxc_timer,
MACHINE_END
