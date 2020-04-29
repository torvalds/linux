// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013 Greg Ungerer <gerg@uclinux.org>
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>

#include "common.h"

static const char * const imx50_dt_board_compat[] __initconst = {
	"fsl,imx50",
	NULL
};

DT_MACHINE_START(IMX50_DT, "Freescale i.MX50 (Device Tree Support)")
	.dt_compat	= imx50_dt_board_compat,
MACHINE_END
