/*
 * Bock-W board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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

#include <linux/of_platform.h>
#include <linux/pinctrl/machine.h>
#include <mach/common.h>
#include <mach/r8a7778.h>
#include <asm/mach/arch.h>

/*
 *	see board-bock.c for checking detail of dip-switch
 */

static const struct pinctrl_map bockw_pinctrl_map[] = {
	/* SCIF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_data_a", "scif0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7778",
				  "scif0_ctrl", "scif0"),
};

static void __init bockw_init(void)
{
	r8a7778_clock_init();

	pinctrl_register_mappings(bockw_pinctrl_map,
				  ARRAY_SIZE(bockw_pinctrl_map));
	r8a7778_pinmux_init();
	r8a7778_add_dt_devices();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *bockw_boards_compat_dt[] __initdata = {
	"renesas,bockw-reference",
	NULL,
};

DT_MACHINE_START(BOCKW_DT, "bockw")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_machine	= bockw_init,
	.init_time	= shmobile_timer_init,
	.dt_compat	= bockw_boards_compat_dt,
MACHINE_END
