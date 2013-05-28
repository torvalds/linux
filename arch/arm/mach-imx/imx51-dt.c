/*
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

#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "mx51.h"

static void __init imx51_dt_init(void)
{
	struct platform_device_info devinfo = { .name = "cpufreq-cpu0", };

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	platform_device_register_full(&devinfo);
}

static const char *imx51_dt_board_compat[] __initdata = {
	"fsl,imx51",
	NULL
};

static void __init imx51_timer_init(void)
{
	mx51_clocks_init_dt();
}

DT_MACHINE_START(IMX51_DT, "Freescale i.MX51 (Device Tree Support)")
	.map_io		= mx51_map_io,
	.init_early	= imx51_init_early,
	.init_irq	= mx51_init_irq,
	.handle_irq	= imx51_handle_irq,
	.init_time	= imx51_timer_init,
	.init_machine	= imx51_dt_init,
	.init_late	= imx51_init_late,
	.dt_compat	= imx51_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
