/*
 * AM33XX Clock init
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
#include <dt-bindings/clock/am3.h>

#include "clock.h"

static const char *enable_init_clks[] = {
	"dpll_ddr_m2_ck",
	"dpll_mpu_m2_ck",
	"l3_gclk",
	"l4hs_gclk",
	"l4fw_gclk",
	"l4ls_gclk",
	/* Required for external peripherals like, Audio codecs */
	"clkout2_ck",
};

int __init am33xx_dt_clk_init(void)
{
	struct clk *clk1, *clk2;

	ti_dt_clocks_register(am33xx_compat_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	/* TRM ERRATA: Timer 3 & 6 default parent (TCLKIN) may not be always
	 *    physically present, in such a case HWMOD enabling of
	 *    clock would be failure with default parent. And timer
	 *    probe thinks clock is already enabled, this leads to
	 *    crash upon accessing timer 3 & 6 registers in probe.
	 *    Fix by setting parent of both these timers to master
	 *    oscillator clock.
	 */

	clk1 = clk_get_sys(NULL, "sys_clkin_ck");
	clk2 = clk_get_sys(NULL, "timer3_fck");
	clk_set_parent(clk2, clk1);

	clk2 = clk_get_sys(NULL, "timer6_fck");
	clk_set_parent(clk2, clk1);
	/*
	 * The On-Chip 32K RC Osc clock is not an accurate clock-source as per
	 * the design/spec, so as a result, for example, timer which supposed
	 * to get expired @60Sec, but will expire somewhere ~@40Sec, which is
	 * not expected by any use-case, so change WDT1 clock source to PRCM
	 * 32KHz clock.
	 */
	clk1 = clk_get_sys(NULL, "wdt1_fck");
	clk2 = clk_get_sys(NULL, "clkdiv32k_ick");
	clk_set_parent(clk1, clk2);

	return 0;
}
