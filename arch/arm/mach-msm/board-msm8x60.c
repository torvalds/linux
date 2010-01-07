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

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include <mach/msm_iomap.h>

void __iomem *gic_cpu_base_addr;

unsigned long clk_get_max_axi_khz(void)
{
	return 0;
}

static void __init msm8x60_map_io(void)
{
	msm_map_msm8x60_io();
}

static void __init msm8x60_init_irq(void)
{
	gic_dist_init(0, MSM_QGIC_DIST_BASE, 1);
	gic_cpu_base_addr = (void *)MSM_QGIC_CPU_BASE;
	gic_cpu_init(0, MSM_QGIC_CPU_BASE);
}

static void __init msm8x60_init(void)
{
}

MACHINE_START(MSM8X60_RUMI3, "QCT MSM8X60 RUMI3")
	.map_io = msm8x60_map_io,
	.init_irq = msm8x60_init_irq,
	.init_machine = msm8x60_init,
	.timer = &msm_timer,
MACHINE_END
