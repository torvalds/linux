/*
 * Genmai board support
 *
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

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/r7s72100.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static void __init genmai_add_standard_devices(void)
{
#ifdef CONFIG_COMMON_CLK
	of_clk_init(NULL);
#else
	r7s72100_clock_init();
#endif
	r7s72100_add_dt_devices();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const genmai_boards_compat_dt[] __initconst = {
	"renesas,genmai-reference",
	NULL,
};

DT_MACHINE_START(GENMAI_DT, "genmai")
	.init_early	= r7s72100_init_early,
	.init_machine	= genmai_add_standard_devices,
	.dt_compat	= genmai_boards_compat_dt,
MACHINE_END
