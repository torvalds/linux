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

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "mx53.h"

static void __init imx53_qsb_init(void)
{
	struct clk *clk;

	clk = clk_get_sys(NULL, "ssi_ext1");
	if (IS_ERR(clk)) {
		pr_err("failed to get clk ssi_ext1\n");
		return;
	}

	clk_register_clkdev(clk, NULL, "0-000a");
}

static void __init imx53_dt_init(void)
{
	if (of_machine_is_compatible("fsl,imx53-qsb"))
		imx53_qsb_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *imx53_dt_board_compat[] __initdata = {
	"fsl,imx53",
	NULL
};

static void __init imx53_timer_init(void)
{
	mx53_clocks_init_dt();
}

DT_MACHINE_START(IMX53_DT, "Freescale i.MX53 (Device Tree Support)")
	.map_io		= mx53_map_io,
	.init_early	= imx53_init_early,
	.init_irq	= mx53_init_irq,
	.handle_irq	= imx53_handle_irq,
	.init_time	= imx53_timer_init,
	.init_machine	= imx53_dt_init,
	.init_late	= imx53_init_late,
	.dt_compat	= imx53_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
