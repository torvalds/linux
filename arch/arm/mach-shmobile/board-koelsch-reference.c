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
	 * This is a really crude hack to provide clkdev support to the SCIF
	 * and CMT devices until they get moved to DT.
	 */
	static const char * const scif_names[] = {
		"scifa0", "scifa1", "scifb0", "scifb1", "scifb2", "scifa2",
		"scif0", "scif1", "scif2", "scif3", "scif4", "scif5", "scifa3",
		"scifa4", "scifa5",
	};
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scif_names); ++i) {
		clk = clk_get(NULL, scif_names[i]);
		if (clk) {
			clk_register_clkdev(clk, NULL, "sh-sci.%u", i);
			clk_put(clk);
		}
	}

	clk = clk_get(NULL, "cmt0");
	if (clk) {
		clk_register_clkdev(clk, NULL, "sh_cmt.0");
		clk_put(clk);
	}
#else
	r8a7791_clock_init();
#endif
	r8a7791_add_dt_devices();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const koelsch_boards_compat_dt[] __initconst = {
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
