/*
 * arch/arm/mach-dove/board-dt.c
 *
 * Marvell Dove 88AP510 System On Chip FDT Board
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-tauros2.h>
#include <asm/mach/arch.h>
#include <mach/dove.h>
#include <mach/pm.h>
#include <plat/common.h>
#include "common.h"

static void __init dove_dt_init(void)
{
	pr_info("Dove 88AP510 SoC\n");

#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	BUG_ON(mvebu_mbus_dt_init());
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const dove_dt_board_compat[] = {
	"marvell,dove",
	NULL
};

DT_MACHINE_START(DOVE_DT, "Marvell Dove (Flattened Device Tree)")
	.map_io		= dove_map_io,
	.init_machine	= dove_dt_init,
	.restart	= dove_restart,
	.dt_compat	= dove_dt_board_compat,
MACHINE_END
