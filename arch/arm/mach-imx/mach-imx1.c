// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2014 Alexander Shiyan <shc_work@mail.ru>
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "hardware.h"

#define MX1_AVIC_ADDR	0x00223000

static void __init imx1_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX1);
}

static const char * const imx1_dt_board_compat[] __initconst = {
	"fsl,imx1",
	NULL
};

DT_MACHINE_START(IMX1_DT, "Freescale i.MX1 (Device Tree Support)")
	.init_early	= imx1_init_early,
	.dt_compat	= imx1_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
