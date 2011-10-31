/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/msm_iomap.h>

static void __init msm8x60_fixup(struct machine_desc *desc, struct tag *tag,
			 char **cmdline, struct meminfo *mi)
{
	for (; tag->hdr.size; tag = tag_next(tag))
		if (tag->hdr.tag == ATAG_MEM &&
				tag->u.mem.start == 0x40200000) {
			tag->u.mem.start = 0x40000000;
			tag->u.mem.size += SZ_2M;
		}
}

static void __init msm8x60_reserve(void)
{
	memblock_remove(0x40000000, SZ_2M);
}

static void __init msm8x60_map_io(void)
{
	msm_map_msm8x60_io();
}

static void __init msm8x60_init_irq(void)
{
	gic_init(0, GIC_PPI_START, MSM_QGIC_DIST_BASE,
		 (void *)MSM_QGIC_CPU_BASE);

	/* Edge trigger PPIs except AVS_SVICINT and AVS_SVICINTSWDONE */
	writel(0xFFFFD7FF, MSM_QGIC_DIST_BASE + GIC_DIST_CONFIG + 4);

	/* RUMI does not adhere to GIC spec by enabling STIs by default.
	 * Enable/clear is supposed to be RO for STIs, but is RW on RUMI.
	 */
	if (!machine_is_msm8x60_sim())
		writel(0x0000FFFF, MSM_QGIC_DIST_BASE + GIC_DIST_ENABLE_SET);
}

static void __init msm8x60_init(void)
{
}

MACHINE_START(MSM8X60_RUMI3, "QCT MSM8X60 RUMI3")
	.fixup = msm8x60_fixup,
	.reserve = msm8x60_reserve,
	.map_io = msm8x60_map_io,
	.init_irq = msm8x60_init_irq,
	.init_machine = msm8x60_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(MSM8X60_SURF, "QCT MSM8X60 SURF")
	.fixup = msm8x60_fixup,
	.reserve = msm8x60_reserve,
	.map_io = msm8x60_map_io,
	.init_irq = msm8x60_init_irq,
	.init_machine = msm8x60_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(MSM8X60_SIM, "QCT MSM8X60 SIMULATOR")
	.fixup = msm8x60_fixup,
	.reserve = msm8x60_reserve,
	.map_io = msm8x60_map_io,
	.init_irq = msm8x60_init_irq,
	.init_machine = msm8x60_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(MSM8X60_FFA, "QCT MSM8X60 FFA")
	.fixup = msm8x60_fixup,
	.reserve = msm8x60_reserve,
	.map_io = msm8x60_map_io,
	.init_irq = msm8x60_init_irq,
	.init_machine = msm8x60_init,
	.timer = &msm_timer,
MACHINE_END
