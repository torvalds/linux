// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright (C) 2002 Shane Nay (shane@minirl.com)
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Denis 'GNUtoo' Carikli <GNUtoo@no-log.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include "common.h"
#include "devices-imx31.h"
#include "hardware.h"
#include "iomux-mx3.h"

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static const unsigned int bug_pins[] __initconst = {
	MX31_PIN_PC_RST__CTS5,
	MX31_PIN_PC_VS2__RTS5,
	MX31_PIN_PC_BVD2__TXD5,
	MX31_PIN_PC_BVD1__RXD5,
};

static void __init bug_board_init(void)
{
	imx31_soc_init();

	mxc_iomux_setup_multiple_pins(bug_pins,
				      ARRAY_SIZE(bug_pins), "uart-4");
	imx31_add_imx_uart4(&uart_pdata);
}

static void __init bug_timer_init(void)
{
	mx31_clocks_init(26000000);
}

MACHINE_START(BUG, "BugLabs BUGBase")
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.init_time	= bug_timer_init,
	.init_machine = bug_board_init,
	.restart	= mxc_restart,
MACHINE_END
