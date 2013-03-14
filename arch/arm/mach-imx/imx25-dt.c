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
#include "mx25.h"

static void __init imx25_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const imx25_dt_board_compat[] __initconst = {
	"fsl,imx25",
	NULL
};

static void __init imx25_timer_init(void)
{
	mx25_clocks_init_dt();
}

DT_MACHINE_START(IMX25_DT, "Freescale i.MX25 (Device Tree Support)")
	.map_io		= mx25_map_io,
	.init_early	= imx25_init_early,
	.init_irq	= mx25_init_irq,
	.handle_irq	= imx25_handle_irq,
	.init_time	= imx25_timer_init,
	.init_machine	= imx25_dt_init,
	.dt_compat	= imx25_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
