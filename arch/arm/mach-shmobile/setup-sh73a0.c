// SPDX-License-Identifier: GPL-2.0
/*
 * sh73a0 processor support
 *
 * Copyright (C) 2010  Takashi Yoshii
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2008  Yoshihiro Shimoda
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

static void __init sh73a0_generic_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* Shared attribute override enable, 64K*8way */
	l2x0_init(ioremap(0xf0100000, PAGE_SIZE), 0x00400000, 0xc20f0fff);
#endif
}

static const char *const sh73a0_boards_compat_dt[] __initconst = {
	"renesas,sh73a0",
	NULL,
};

DT_MACHINE_START(SH73A0_DT, "Generic SH73A0 (Flattened Device Tree)")
	.smp		= smp_ops(sh73a0_smp_ops),
	.init_machine	= sh73a0_generic_init,
	.init_late	= shmobile_init_late,
	.dt_compat	= sh73a0_boards_compat_dt,
MACHINE_END
