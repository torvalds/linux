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

static const struct of_dev_auxdata imx31_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("fsl,imx31-uart", MX31_UART1_BASE_ADDR,
			"imx21-uart.0", NULL),
	OF_DEV_AUXDATA("fsl,imx31-uart", MX31_UART2_BASE_ADDR,
			"imx21-uart.1", NULL),
	OF_DEV_AUXDATA("fsl,imx31-uart", MX31_UART3_BASE_ADDR,
			"imx21-uart.2", NULL),
	OF_DEV_AUXDATA("fsl,imx31-uart", MX31_UART4_BASE_ADDR,
			"imx21-uart.3", NULL),
	OF_DEV_AUXDATA("fsl,imx31-uart", MX31_UART5_BASE_ADDR,
			"imx21-uart.4", NULL),
	{ /* sentinel */ }
};

static void __init imx31_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     imx31_auxdata_lookup, NULL);
}

static void __init imx31_timer_init(void)
{
	mx31_clocks_init_dt();
}

static struct sys_timer imx31_timer = {
	.init = imx31_timer_init,
};

static const char *imx31_dt_board_compat[] __initdata = {
	"fsl,imx31",
	NULL
};

DT_MACHINE_START(IMX31_DT, "Freescale i.MX31 (Device Tree Support)")
	.map_io		= mx31_map_io,
	.init_early	= imx31_init_early,
	.init_irq	= mx31_init_irq,
	.handle_irq	= imx31_handle_irq,
	.timer		= &imx31_timer,
	.init_machine	= imx31_dt_init,
	.dt_compat	= imx31_dt_board_compat,
	.restart	= mxc_restart,
MACHINE_END
