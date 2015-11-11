/*
 * Dummy Virtual Machine - does what it says on the tin.
 *
 * Copyright (C) 2012 ARM Ltd
 * Authors: Will Deacon <will.deacon@arm.com>,
 *          Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/smp.h>

#include <asm/mach/arch.h>

static void __init virt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *virt_dt_match[] = {
	"linux,dummy-virt",
	"xen,xenvm",
	NULL
};

extern struct smp_operations virt_smp_ops;

DT_MACHINE_START(VIRT, "Dummy Virtual Machine")
	.init_irq	= irqchip_init,
	.init_machine	= virt_init,
	.smp		= smp_ops(virt_smp_ops),
	.dt_compat	= virt_dt_match,
MACHINE_END
