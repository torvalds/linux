/*
 * kzm9d board support - Reference DT implementation
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

#include <linux/init.h>
#include <linux/of_platform.h>
#include <mach/emev2.h>
#include <mach/common.h>
#include <asm/mach/arch.h>

static void __init kzm9d_add_standard_devices(void)
{
	emev2_clock_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *kzm9d_boards_compat_dt[] __initdata = {
	"renesas,kzm9d-reference",
	NULL,
};

DT_MACHINE_START(KZM9D_DT, "kzm9d")
	.smp		= smp_ops(emev2_smp_ops),
	.map_io		= emev2_map_io,
	.init_early	= emev2_init_delay,
	.init_machine	= kzm9d_add_standard_devices,
	.init_late	= shmobile_init_late,
	.dt_compat	= kzm9d_boards_compat_dt,
MACHINE_END
