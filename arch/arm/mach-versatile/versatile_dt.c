/*
 * Versatile board support using the device tree
 *
 *  Copyright (C) 2010 Secret Lab Technologies Ltd.
 *  Copyright (C) 2009 Jeremy Kerr <jeremy.kerr@canonical.com>
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/hardware/vic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "core.h"

static void __init versatile_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     versatile_auxdata_lookup, NULL);
}

static const char *versatile_dt_match[] __initconst = {
	"arm,versatile-ab",
	"arm,versatile-pb",
	NULL,
};

DT_MACHINE_START(VERSATILE_PB, "ARM-Versatile (Device Tree Support)")
	.map_io		= versatile_map_io,
	.init_early	= versatile_init_early,
	.init_irq	= versatile_init_irq,
	.handle_irq	= vic_handle_irq,
	.timer		= &versatile_timer,
	.init_machine	= versatile_dt_init,
	.dt_compat	= versatile_dt_match,
	.restart	= versatile_restart,
MACHINE_END
