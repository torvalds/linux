/*
 * Copyright 2012 Sascha Hauer, Pengutronix
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "mx27.h"

static void __init imx27_dt_init(void)
{
	struct platform_device_info devinfo = { .name = "cpufreq-cpu0", };

	mxc_arch_reset_init_dt();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	platform_device_register_full(&devinfo);
}

static const char * const imx27_dt_board_compat[] __initconst = {
	"fsl,imx27",
	NULL
};

DT_MACHINE_START(IMX27_DT, "Freescale i.MX27 (Device Tree Support)")
	.map_io		= mx27_map_io,
	.init_early	= imx27_init_early,
	.init_irq	= mx27_init_irq,
	.init_machine	= imx27_dt_init,
	.dt_compat	= imx27_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
