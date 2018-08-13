/*
 * AM43XX Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/am4.h>

#include "clock.h"

int __init am43xx_dt_clk_init(void)
{
	struct clk *clk1, *clk2;

	ti_dt_clocks_register(am43xx_compat_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

	/*
	 * cpsw_cpts_rft_clk  has got the choice of 3 clocksources
	 * dpll_core_m4_ck, dpll_core_m5_ck and dpll_disp_m2_ck.
	 * By default dpll_core_m4_ck is selected, witn this as clock
	 * source the CPTS doesnot work properly. It gives clockcheck errors
	 * while running PTP.
	 * clockcheck: clock jumped backward or running slower than expected!
	 * By selecting dpll_core_m5_ck as the clocksource fixes this issue.
	 * In AM335x dpll_core_m5_ck is the default clocksource.
	 */
	clk1 = clk_get_sys(NULL, "cpsw_cpts_rft_clk");
	clk2 = clk_get_sys(NULL, "dpll_core_m5_ck");
	clk_set_parent(clk1, clk2);

	return 0;
}
