/*
 * Lager board support - Reference DT implementation
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Simon Horman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/rcar-gen2.h>
#include <mach/r8a7790.h>
#include <asm/mach/arch.h>

static void __init lager_add_standard_devices(void)
{
#ifdef CONFIG_COMMON_CLK
	/*
	 * This is a really crude hack to provide clkdev support to platform
	 * devices until they get moved to DT.
	 */
	static const struct clk_name {
		const char *clk;
		const char *con_id;
		const char *dev_id;
	} clk_names[] = {
		{ "cmt0", NULL, "sh_cmt.0" },
		{ "scifa0", NULL, "sh-sci.0" },
		{ "scifa1", NULL, "sh-sci.1" },
		{ "scifb0", NULL, "sh-sci.2" },
		{ "scifb1", NULL, "sh-sci.3" },
		{ "scifb2", NULL, "sh-sci.4" },
		{ "scifa2", NULL, "sh-sci.5" },
		{ "scif0", NULL, "sh-sci.6" },
		{ "scif1", NULL, "sh-sci.7" },
		{ "hscif0", NULL, "sh-sci.8" },
		{ "hscif1", NULL, "sh-sci.9" },
	};
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(clk_names); ++i) {
		clk = clk_get(NULL, clk_names[i].clk);
		if (!IS_ERR(clk)) {
			clk_register_clkdev(clk, clk_names[i].con_id,
					    clk_names[i].dev_id);
			clk_put(clk);
		}
	}
#else
	r8a7790_clock_init();
#endif

	r8a7790_add_dt_devices();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager",
	"renesas,lager-reference",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= r8a7790_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= lager_add_standard_devices,
	.init_late	= shmobile_init_late,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
