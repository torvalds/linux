/*
 * Copyright 2013 Greg Ungerer <gerg@uclinux.org>
 * Copyright 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/of_platform.h>
#include <asm/mach/arch.h>

#include "common.h"

static void __init imx50_dt_init(void)
{
	mxc_arch_reset_init_dt();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *imx50_dt_board_compat[] __initconst = {
	"fsl,imx50",
	NULL
};

DT_MACHINE_START(IMX50_DT, "Freescale i.MX50 (Device Tree Support)")
	.map_io		= mx53_map_io,
	.init_irq	= tzic_init_irq,
	.init_machine	= imx50_dt_init,
	.dt_compat	= imx50_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
