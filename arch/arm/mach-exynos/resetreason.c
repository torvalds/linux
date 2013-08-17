/*
 * arch/arm/mach-exynos/resetreason.c
 *
 * Copyright (C) 2012 Google, Inc.
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
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/io.h>
#include <mach/regs-pmu.h>
#include "resetreason.h"

static char resetreason[1024];

#define EXYNOS_RST_STAT_SWRESET		(1<<29)
#define EXYNOS_RST_STAT_WRSET		(1<<28)
#define EXYNOS_RST_STAT_ISP_ARM_WDTRESET  (1<<26)
#define EXYNOS_RST_STAT_FSYS_ARM_WDTRESET (1<<25)
#define EXYNOS_RST_STAT_SYS_WDTRESET	(1<<20)
#define EXYNOS_RST_STAT_PINRESET	(1<<16)
#define EXYNOS_RST_STAT_EAGLE_WRESET1	(1<<1)
#define EXYNOS_RST_STAT_EAGLE_WRESET0	(1<<0)

static struct {
	const char *str;
	u32 mask;
} resetreason_flags[] = {
	{ "software ",			EXYNOS_RST_STAT_SWRESET },
	{ "warm ",			EXYNOS_RST_STAT_WRSET },
	{ "ISP_ARM watchdog timer ",	EXYNOS_RST_STAT_ISP_ARM_WDTRESET },
	{ "FSYS_ARM watchdog timer ",	EXYNOS_RST_STAT_FSYS_ARM_WDTRESET },
	{ "system watchdog timer ",	EXYNOS_RST_STAT_SYS_WDTRESET },
	{ "XnRESET pin ",		EXYNOS_RST_STAT_PINRESET },
	{ "Eagle warm ",		EXYNOS_RST_STAT_EAGLE_WRESET1 |
					EXYNOS_RST_STAT_EAGLE_WRESET0 },
};

const char *exynos_get_resetreason(void)
{
	return resetreason;
}

static int __init resetreason_init(void)
{
	int i;
	u32 reasons = __raw_readl(EXYNOS_RST_STAT);
	char buf[128];

	strlcpy(resetreason, "Last reset was ", sizeof(resetreason));

	for (i = 0; i < ARRAY_SIZE(resetreason_flags); i++)
		if (reasons & resetreason_flags[i].mask)
			strlcat(resetreason, resetreason_flags[i].str,
				sizeof(resetreason));

	snprintf(buf, sizeof(buf), "reset (RST_STAT=0x%x)\n", reasons);
	strlcat(resetreason, buf, sizeof(resetreason));
	pr_info("%s", resetreason);
	return 0;
}

postcore_initcall(resetreason_init);
