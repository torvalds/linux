/*
 * sh73a0 processor support
 *
 * Copyright (C) 2010  Takashi Yoshii
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/io.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "common.h"
#include "sh73a0.h"

static struct map_desc sh73a0_io_desc[] __initdata = {
	/* create a 1:1 identity mapping for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init sh73a0_map_io(void)
{
	debug_ll_io_init();
	iotable_init(sh73a0_io_desc, ARRAY_SIZE(sh73a0_io_desc));
}

static void __init sh73a0_generic_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x00400000, 0xc20f0fff);
#endif
}

static const char *const sh73a0_boards_compat_dt[] __initconst = {
	"renesas,sh73a0",
	NULL,
};

DT_MACHINE_START(SH73A0_DT, "Generic SH73A0 (Flattened Device Tree)")
	.smp		= smp_ops(sh73a0_smp_ops),
	.map_io		= sh73a0_map_io,
	.init_early	= shmobile_init_delay,
	.init_machine	= sh73a0_generic_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= sh73a0_boards_compat_dt,
MACHINE_END
