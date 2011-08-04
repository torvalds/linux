/*
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright (C) 2002 Shane Nay (shane@minirl.com)
 * Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Denis 'GNUtoo' Carikli <GNUtoo@no-log.org>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <mach/iomux-mx3.h>
#include <mach/hardware.h>
#include <mach/common.h>

#include <asm/mach/time.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include "devices-imx31.h"

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

static struct sys_timer bug_timer = {
	.init = bug_timer_init,
};

MACHINE_START(BUG, "BugLabs BUGBase")
	.map_io = mx31_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31_init_irq,
	.timer = &bug_timer,
	.init_machine = bug_board_init,
MACHINE_END
