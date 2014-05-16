/*
 * marzen board support - Reference DT implementation
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
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

#include <linux/clk/shmobile.h>
#include <linux/clocksource.h>
#include <linux/of_platform.h>
#include <mach/r8a7779.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include "clock.h"
#include "common.h"
#include "irqs.h"

static void __init marzen_init_timer(void)
{
	r8a7779_clocks_init(r8a7779_read_mode_pins());
	clocksource_of_init();
}

/*
 * This is a really crude hack to provide clkdev support to platform
 * devices until they get moved to DT.
 */
static const struct clk_name clk_names[] __initconst = {
	{ "scif0", NULL, "sh-sci.0" },
	{ "scif1", NULL, "sh-sci.1" },
	{ "scif2", NULL, "sh-sci.2" },
	{ "scif3", NULL, "sh-sci.3" },
	{ "scif4", NULL, "sh-sci.4" },
	{ "scif5", NULL, "sh-sci.5" },
	{ "tmu0", "fck", "sh-tmu.0" },
};

static void __init marzen_init(void)
{
	shmobile_clk_workaround(clk_names, ARRAY_SIZE(clk_names), false);
	r8a7779_add_standard_devices_dt();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	r8a7779_init_irq_extpin_dt(1); /* IRQ1 as individual interrupt */
}

static const char *marzen_boards_compat_dt[] __initdata = {
	"renesas,marzen",
	"renesas,marzen-reference",
	NULL,
};

DT_MACHINE_START(MARZEN, "marzen")
	.smp		= smp_ops(r8a7779_smp_ops),
	.map_io		= r8a7779_map_io,
	.init_early	= shmobile_init_delay,
	.init_time	= marzen_init_timer,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= r8a7779_init_irq_dt,
	.init_machine	= marzen_init,
	.dt_compat	= marzen_boards_compat_dt,
MACHINE_END
