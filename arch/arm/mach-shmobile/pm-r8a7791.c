/*
 * r8a7791 Power management support
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>

#include <asm/io.h>

#include "pm-rcar.h"
#include "r8a7791.h"

/* SYSC */
#define SYSCIER 0x0c
#define SYSCIMR 0x10

#if defined(CONFIG_SMP)

static void __init r8a7791_sysc_init(void)
{
	void __iomem *base = rcar_sysc_init(0xe6180000);

	/* enable all interrupt sources, but do not use interrupt handler */
	iowrite32(0x0131000e, base + SYSCIER);
	iowrite32(0, base + SYSCIMR);
}

#else /* CONFIG_SMP */

static inline void r8a7791_sysc_init(void) {}

#endif /* CONFIG_SMP */

void __init r8a7791_pm_init(void)
{
	static int once;

	if (once++)
		return;

	r8a7791_sysc_init();
}
