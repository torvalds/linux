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
#include "mx31.h"

static void __init imx31_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *imx31_dt_board_compat[] __initdata = {
	"fsl,imx31",
	NULL
};

static void __init imx31_dt_timer_init(void)
{
	mx31_clocks_init_dt();
}

DT_MACHINE_START(IMX31_DT, "Freescale i.MX31 (Device Tree Support)")
	.map_io		= mx31_map_io,
	.init_early	= imx31_init_early,
	.init_irq	= mx31_init_irq,
	.handle_irq	= imx31_handle_irq,
	.init_time	= imx31_dt_timer_init,
	.init_machine	= imx31_dt_init,
	.dt_compat	= imx31_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
