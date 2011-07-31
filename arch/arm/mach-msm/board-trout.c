/* linux/arch/arm/mach-msm/board-trout.c
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/msm_iomap.h>

#include "devices.h"
#include "board-trout.h"

extern int trout_init_mmc(unsigned int);

static struct platform_device *devices[] __initdata = {
	&msm_device_uart3,
	&msm_device_smd,
	&msm_device_nand,
	&msm_device_hsusb,
	&msm_device_i2c,
};

extern struct sys_timer msm_timer;

static void __init trout_init_irq(void)
{
	msm_init_irq();
}

static void __init trout_fixup(struct machine_desc *desc, struct tag *tags,
				char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size = (101*1024*1024);
}

static void __init trout_init(void)
{
	int rc;

	platform_add_devices(devices, ARRAY_SIZE(devices));

#ifdef CONFIG_MMC
        rc = trout_init_mmc(system_rev);
        if (rc)
                printk(KERN_CRIT "%s: MMC init failure (%d)\n", __func__, rc);
#endif

}

static struct map_desc trout_io_desc[] __initdata = {
	{
		.virtual = TROUT_CPLD_BASE,
		.pfn     = __phys_to_pfn(TROUT_CPLD_START),
		.length  = TROUT_CPLD_SIZE,
		.type    = MT_DEVICE_NONSHARED
	}
};

static void __init trout_map_io(void)
{
	msm_map_common_io();
	iotable_init(trout_io_desc, ARRAY_SIZE(trout_io_desc));

#ifdef CONFIG_MSM_DEBUG_UART3
	/* route UART3 to the "H2W" extended usb connector */
	writeb(0x80, TROUT_CPLD_BASE + 0x00);
#endif

	msm_clock_init(msm_clocks_7x01a, msm_num_clocks_7x01a);
}

MACHINE_START(TROUT, "HTC Dream")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io	= MSM_DEBUG_UART_PHYS,
	.io_pg_offst	= ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params	= 0x10000100,
	.fixup		= trout_fixup,
	.map_io		= trout_map_io,
	.init_irq	= trout_init_irq,
	.init_machine	= trout_init,
	.timer		= &msm_timer,
MACHINE_END
