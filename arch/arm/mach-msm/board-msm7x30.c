/* Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
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
 */

#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/smsc911x.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/setup.h>

#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/memory.h>
#include <mach/msm_iomap.h>
#include <mach/dma.h>

#include <mach/vreg.h>
#include "devices.h"
#include "proc_comm.h"

extern struct sys_timer msm_timer;

static struct platform_device *devices[] __initdata = {
#if defined(CONFIG_SERIAL_MSM) || defined(CONFIG_MSM_SERIAL_DEBUGGER)
        &msm_device_uart2,
#endif
	&msm_device_smd,
};

static void __init msm7x30_init_irq(void)
{
	msm_init_irq();
}

static void __init msm7x30_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init msm7x30_map_io(void)
{
	msm_map_msm7x30_io();
	msm_clock_init(msm_clocks_7x30, msm_num_clocks_7x30);
}

MACHINE_START(MSM7X30_SURF, "QCT MSM7X30 SURF")
#ifdef CONFIG_MSM_DEBUG_UART
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = msm7x30_map_io,
	.init_irq = msm7x30_init_irq,
	.init_machine = msm7x30_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(MSM7X30_FFA, "QCT MSM7X30 FFA")
#ifdef CONFIG_MSM_DEBUG_UART
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = msm7x30_map_io,
	.init_irq = msm7x30_init_irq,
	.init_machine = msm7x30_init,
	.timer = &msm_timer,
MACHINE_END

MACHINE_START(MSM7X30_FLUID, "QCT MSM7X30 FLUID")
#ifdef CONFIG_MSM_DEBUG_UART
#endif
	.boot_params = PHYS_OFFSET + 0x100,
	.map_io = msm7x30_map_io,
	.init_irq = msm7x30_init_irq,
	.init_machine = msm7x30_init,
	.timer = &msm_timer,
MACHINE_END
