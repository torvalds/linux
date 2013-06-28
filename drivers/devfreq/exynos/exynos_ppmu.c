/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - PPMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>

#include "exynos_ppmu.h"

void exynos_ppmu_reset(void __iomem *ppmu_base)
{
	__raw_writel(PPMU_CYCLE_RESET | PPMU_COUNTER_RESET, ppmu_base);
	__raw_writel(PPMU_ENABLE_CYCLE  |
		     PPMU_ENABLE_COUNT0 |
		     PPMU_ENABLE_COUNT1 |
		     PPMU_ENABLE_COUNT2 |
		     PPMU_ENABLE_COUNT3,
		     ppmu_base + PPMU_CNTENS);
}

void exynos_ppmu_setevent(void __iomem *ppmu_base, unsigned int ch,
			unsigned int evt)
{
	__raw_writel(evt, ppmu_base + PPMU_BEVTSEL(ch));
}

void exynos_ppmu_start(void __iomem *ppmu_base)
{
	__raw_writel(PPMU_ENABLE, ppmu_base);
}

void exynos_ppmu_stop(void __iomem *ppmu_base)
{
	__raw_writel(PPMU_DISABLE, ppmu_base);
}

unsigned int exynos_ppmu_read(void __iomem *ppmu_base, unsigned int ch)
{
	unsigned int total;

	if (ch == PPMU_PMNCNT3)
		total = ((__raw_readl(ppmu_base + PMCNT_OFFSET(ch)) << 8) |
			  __raw_readl(ppmu_base + PMCNT_OFFSET(ch + 1)));
	else
		total = __raw_readl(ppmu_base + PMCNT_OFFSET(ch));

	return total;
}
