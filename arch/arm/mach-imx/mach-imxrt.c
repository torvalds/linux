// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019
 * Author(s): Giulio Benetti <giulio.benetti@benettiengineering.com>
 */

#include <linux/kernel.h>
#include <asm/mach/arch.h>
#include <asm/v7m.h>

static const char *const imxrt_compat[] __initconst = {
	"fsl,imxrt1050",
	NULL
};

DT_MACHINE_START(IMXRTDT, "IMXRT (Device Tree Support)")
	.dt_compat = imxrt_compat,
	.restart = armv7m_restart,
MACHINE_END
