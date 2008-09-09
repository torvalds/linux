/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <linux/clk.h>
#include <linux/serial_8250.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/board-mx31ads.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>

/*!
 * @file mx31ads.c
 *
 * @brief This file contains the board-specific initialization routines.
 *
 * @ingroup System
 */

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_8250_MODULE)
/*!
 * The serial port definition structure.
 */
static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase  = (void *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTA),
		.mapbase  = (unsigned long)(CS4_BASE_ADDR + PBC_SC16C652_UARTA),
		.irq      = EXPIO_INT_XUART_INTA,
		.uartclk  = 14745600,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	}, {
		.membase  = (void *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTB),
		.mapbase  = (unsigned long)(CS4_BASE_ADDR + PBC_SC16C652_UARTB),
		.irq      = EXPIO_INT_XUART_INTB,
		.uartclk  = 14745600,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	},
	{},
};

static struct platform_device serial_device = {
	.name	= "serial8250",
	.id	= 0,
	.dev	= {
		.platform_data = serial_platform_data,
	},
};

static int __init mxc_init_extuart(void)
{
	return platform_device_register(&serial_device);
}
#else
static inline int mxc_init_extuart(void)
{
	return 0;
}
#endif

#if defined(CONFIG_SERIAL_IMX) || defined(CONFIG_SERIAL_IMX_MODULE)
static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static inline void mxc_init_imx_uart(void)
{
	mxc_iomux_mode(MX31_PIN_CTS1__CTS1);
	mxc_iomux_mode(MX31_PIN_RTS1__RTS1);
	mxc_iomux_mode(MX31_PIN_TXD1__TXD1);
	mxc_iomux_mode(MX31_PIN_RXD1__RXD1);

	mxc_register_device(&mxc_uart_device0, &uart_pdata);
}
#else /* !SERIAL_IMX */
static inline void mxc_init_imx_uart(void)
{
}
#endif /* !SERIAL_IMX */

/*!
 * This structure defines static mappings for the i.MX31ADS board.
 */
static struct map_desc mx31ads_io_desc[] __initdata = {
	{
		.virtual	= AIPS1_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(AIPS1_BASE_ADDR),
		.length		= AIPS1_SIZE,
		.type		= MT_NONSHARED_DEVICE
	}, {
		.virtual	= SPBA0_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(SPBA0_BASE_ADDR),
		.length		= SPBA0_SIZE,
		.type		= MT_NONSHARED_DEVICE
	}, {
		.virtual	= AIPS2_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(AIPS2_BASE_ADDR),
		.length		= AIPS2_SIZE,
		.type		= MT_NONSHARED_DEVICE
	}, {
		.virtual	= CS4_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(CS4_BASE_ADDR),
		.length		= CS4_SIZE / 2,
		.type		= MT_DEVICE
	},
};

/*!
 * Set up static virtual mappings.
 */
void __init mx31ads_map_io(void)
{
	mxc_map_io();
	iotable_init(mx31ads_io_desc, ARRAY_SIZE(mx31ads_io_desc));
}

/*!
 * Board specific initialization.
 */
static void __init mxc_board_init(void)
{
	mxc_init_extuart();
	mxc_init_imx_uart();
}

static void __init mx31ads_timer_init(void)
{
	mxc_clocks_init(26000000);
	mxc_timer_init("ipg_clk.0");
}

struct sys_timer mx31ads_timer = {
	.init	= mx31ads_timer_init,
};

/*
 * The following uses standard kernel macros defined in arch.h in order to
 * initialize __mach_desc_MX31ADS data structure.
 */
MACHINE_START(MX31ADS, "Freescale MX31ADS")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx31ads_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mxc_board_init,
	.timer          = &mx31ads_timer,
MACHINE_END
