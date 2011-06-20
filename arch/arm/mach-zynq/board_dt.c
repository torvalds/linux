/*
 * This file contains code for boards with device tree support.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * based on arch/arm/mach-realview/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include "common.h"

static const char *xilinx_dt_match[] = {
	"xlnx,zynq-ep107",
	NULL
};

MACHINE_START(XILINX_EP107, "Xilinx Zynq Platform")
	.map_io		= xilinx_map_io,
	.init_irq	= xilinx_irq_init,
	.init_machine	= xilinx_init_machine,
	.timer		= &xttcpss_sys_timer,
	.dt_compat	= xilinx_dt_match,
MACHINE_END
