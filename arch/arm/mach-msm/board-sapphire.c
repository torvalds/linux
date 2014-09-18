/* linux/arch/arm/mach-msm/board-sapphire.c
 * Copyright (C) 2007-2009 HTC Corporation.
 * Author: Thomas Tsai <thomas_tsai@htc.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
*/
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/device.h>

#include <linux/delay.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <mach/vreg.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/setup.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/memblock.h>

#include "gpio_chip.h"
#include "board-sapphire.h"
#include "proc_comm.h"
#include "devices.h"
#include "common.h"

void msm_init_irq(void);
void msm_init_gpio(void);

static struct platform_device *devices[] __initdata = {
	&msm_device_smd,
	&msm_device_dmov,
	&msm_device_nand,
	&msm_device_uart1,
	&msm_device_uart3,
};

void msm_timer_init(void);

static void __init sapphire_init_irq(void)
{
	msm_init_irq();
}

static void __init sapphire_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static struct map_desc sapphire_io_desc[] __initdata = {
	{
		.virtual = SAPPHIRE_CPLD_BASE,
		.pfn     = __phys_to_pfn(SAPPHIRE_CPLD_START),
		.length  = SAPPHIRE_CPLD_SIZE,
		.type    = MT_DEVICE_NONSHARED
	}
};

static void __init sapphire_fixup(struct tag *tags, char **cmdline)
{
	int smi_sz = parse_tag_smi((const struct tag *)tags);

	if (smi_sz == 32) {
		memblock_add(PHYS_OFFSET, 84*SZ_1M);
	} else if (smi_sz == 64) {
		memblock_add(PHYS_OFFSET, 101*SZ_1M);
	} else {
		memblock_add(PHYS_OFFSET, 101*SZ_1M);
		/* Give a default value when not get smi size */
		smi_sz = 64;
	}
}

static void __init sapphire_map_io(void)
{
	msm_map_common_io();
	iotable_init(sapphire_io_desc, ARRAY_SIZE(sapphire_io_desc));
	msm_clock_init();
}

static void __init sapphire_init_late(void)
{
	smd_debugfs_init();
}

MACHINE_START(SAPPHIRE, "sapphire")
/* Maintainer: Brian Swetland <swetland@google.com> */
	.atag_offset    = 0x100,
	.fixup          = sapphire_fixup,
	.map_io         = sapphire_map_io,
	.init_irq       = sapphire_init_irq,
	.init_machine   = sapphire_init,
	.init_late      = sapphire_init_late,
	.init_time	= msm_timer_init,
MACHINE_END
