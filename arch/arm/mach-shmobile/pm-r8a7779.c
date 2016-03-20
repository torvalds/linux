/*
 * r8a7779 Power management support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <asm/io.h>

#include "pm-rcar.h"
#include "r8a7779.h"

/* SYSC */
#define SYSCIER 0x0c
#define SYSCIMR 0x10

#if defined(CONFIG_PM) || defined(CONFIG_SMP)

static void __init r8a7779_sysc_init(void)
{
	void __iomem *base = rcar_sysc_init(0xffd85000);

	/* enable all interrupt sources, but do not use interrupt handler */
	iowrite32(0x0131000e, base + SYSCIER);
	iowrite32(0, base + SYSCIMR);
}

#else /* CONFIG_PM || CONFIG_SMP */

static inline void r8a7779_sysc_init(void) {}

#endif /* CONFIG_PM || CONFIG_SMP */

void __init r8a7779_pm_init(void)
{
	static int once;

	if (!once++)
		r8a7779_sysc_init();
}
