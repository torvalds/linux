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

#include <linux/init.h>
#include <linux/of_platform.h>
#include <mach/r8a7790.h>
#include <asm/mach/arch.h>

static void __init lager_add_standard_devices(void)
{
	/* clocks are setup late during boot in the case of DT */
	r8a7790_clock_init();

	r8a7790_add_dt_devices();
        of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager-reference",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.init_early	= r8a7790_init_delay,
	.init_machine	= lager_add_standard_devices,
	.init_time	= r8a7790_timer_init,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END
