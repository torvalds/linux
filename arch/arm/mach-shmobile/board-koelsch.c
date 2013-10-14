/*
 * Koelsch board support
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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <mach/common.h>
#include <mach/r8a7791.h>
#include <mach/rcar-gen2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

static void __init koelsch_add_standard_devices(void)
{
	r8a7791_clock_init();
	r8a7791_add_standard_devices();
}

static const char * const koelsch_boards_compat_dt[] __initconst = {
	"renesas,koelsch",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_machine	= koelsch_add_standard_devices,
	.init_time	= rcar_gen2_timer_init,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END
