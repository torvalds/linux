/*
 * r8a7790 processor support
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

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7790.h>
#include <asm/mach/arch.h>

void __init r8a7790_add_standard_devices(void)
{
}

#ifdef CONFIG_USE_OF
void __init r8a7790_add_standard_devices_dt(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *r8a7790_boards_compat_dt[] __initdata = {
	"renesas,r8a7790",
	NULL,
};

DT_MACHINE_START(R8A7790_DT, "Generic R8A7790 (Flattened Device Tree)")
	.init_irq	= irqchip_init,
	.init_machine	= r8a7790_add_standard_devices_dt,
	.init_time	= shmobile_timer_init,
	.dt_compat	= r8a7790_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
