// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <linux/kernel.h>
#include <asm/v7m.h>
#include <asm/mach/arch.h>

static const char * const imx7d_cm4_dt_compat[] __initconst = {
	"fsl,imx7d-cm4",
	NULL,
};

DT_MACHINE_START(IMX7D, "Freescale i.MX7 Dual Cortex-M4 (Device Tree)")
	.dt_compat = imx7d_cm4_dt_compat,
	.restart = armv7m_restart,
MACHINE_END
