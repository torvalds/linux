/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clkdev.h>
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/msm_iomap.h>

#include "devices.h"

static void __init msm8960_fixup(struct machine_desc *desc, struct tag *tag,
			 char **cmdline, struct meminfo *mi)
{
	for (; tag->hdr.size; tag = tag_next(tag))
		if (tag->hdr.tag == ATAG_MEM &&
				tag->u.mem.start == 0x40200000) {
			tag->u.mem.start = 0x40000000;
			tag->u.mem.size += SZ_2M;
		}
}

static void __init msm8960_reserve(void)
{
	memblock_remove(0x40000000, SZ_2M);
}

static void __init msm8960_map_io(void)
{
	msm_map_msm8960_io();
}

static void __init msm8960_init_irq(void)
{
	unsigned int i;
	gic_init(0, GIC_PPI_START, MSM_QGIC_DIST_BASE,
		 (void *)MSM_QGIC_CPU_BASE);

	/* Edge trigger PPIs except AVS_SVICINT and AVS_SVICINTSWDONE */
	writel(0xFFFFD7FF, MSM_QGIC_DIST_BASE + GIC_DIST_CONFIG + 4);

	if (machine_is_msm8960_rumi3())
		writel(0x0000FFFF, MSM_QGIC_DIST_BASE + GIC_DIST_ENABLE_SET);

	/* FIXME: Not installing AVS_SVICINT and AVS_SVICINTSWDONE yet
	 * as they are configured as level, which does not play nice with
	 * handle_percpu_irq.
	 */
	for (i = GIC_PPI_START; i < GIC_SPI_START; i++) {
		if (i != AVS_SVICINT && i != AVS_SVICINTSWDONE)
			irq_set_handler(i, handle_percpu_irq);
	}
}

static struct platform_device *sim_devices[] __initdata = {
	&msm8960_device_uart_gsbi2,
};

static struct platform_device *rumi3_devices[] __initdata = {
	&msm8960_device_uart_gsbi5,
};

static void __init msm8960_sim_init(void)
{
	platform_add_devices(sim_devices, ARRAY_SIZE(sim_devices));
}

static void __init msm8960_rumi3_init(void)
{
	platform_add_devices(rumi3_devices, ARRAY_SIZE(rumi3_devices));
}

MACHINE_START(MSM8960_SIM, "QCT MSM8960 SIMULATOR")
	.fixup = msm8960_fixup,
	.reserve = msm8960_reserve,
	.map_io = msm8960_map_io,
	.init_irq = msm8960_init_irq,
	.timer = &msm_timer,
	.init_machine = msm8960_sim_init,
MACHINE_END

MACHINE_START(MSM8960_RUMI3, "QCT MSM8960 RUMI3")
	.fixup = msm8960_fixup,
	.reserve = msm8960_reserve,
	.map_io = msm8960_map_io,
	.init_irq = msm8960_init_irq,
	.timer = &msm_timer,
	.init_machine = msm8960_rumi3_init,
MACHINE_END

