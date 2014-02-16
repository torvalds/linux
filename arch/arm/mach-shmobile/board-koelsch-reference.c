/*
 * Koelsch board support - Reference DT implementation
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
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
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/rcar-gen2.h>
#include <mach/r8a7791.h>
#include <asm/mach/arch.h>

static void __init koelsch_add_standard_devices(void)
{
#ifdef CONFIG_COMMON_CLK
	/*
	 * This is a really crude hack to provide clkdev support to the CMT and
	 * DU devices until they get moved to DT.
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
		{ "scif2", NULL, "sh-sci.8" },
		{ "scif3", NULL, "sh-sci.9" },
		{ "scif4", NULL, "sh-sci.10" },
		{ "scif5", NULL, "sh-sci.11" },
		{ "scifa3", NULL, "sh-sci.12" },
		{ "scifa4", NULL, "sh-sci.13" },
		{ "scifa5", NULL, "sh-sci.14" },
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
	r8a7791_clock_init();
#endif
	r8a7791_add_dt_devices();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const koelsch_boards_compat_dt[] __initconst = {
	"renesas,koelsch",
	"renesas,koelsch-reference",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_time	= rcar_gen2_timer_init,
	.init_machine	= koelsch_add_standard_devices,
	.init_late	= shmobile_init_late,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END
