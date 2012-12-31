/* linux/arch/arm/mach-exynos/ppc.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - PPMU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/ppmu.h>

void exynos4_ppc_reset(struct exynos4_ppmu_hw *ppmu)
{
	void __iomem *ppmu_base = ppmu->hw_base;
	int i;

	__raw_writel(0x8000000f, ppmu_base + 0xf010);
	__raw_writel(0x8000000f, ppmu_base + 0xf050);
	__raw_writel(0x6, ppmu_base + 0xf000);
	__raw_writel(0x0, ppmu_base + 0xf100);

	ppmu->ccnt = 0;
	for (i = 0; i < NUMBER_OF_COUNTER; i++)
		ppmu->count[i] = 0;
}

void exynos4_ppc_setevent(struct exynos4_ppmu_hw *ppmu,
				  unsigned int evt)
{
	void __iomem *ppmu_base = ppmu->hw_base;

	ppmu->event[0] = evt;

	__raw_writel(((evt << 12) | 0x1), ppmu_base + 0xfc);
}

void exynos4_ppc_start(struct exynos4_ppmu_hw *ppmu)
{
	void __iomem *ppmu_base = ppmu->hw_base;

	__raw_writel(0x1, ppmu_base + 0xf000);
}

void exynos4_ppc_stop(struct exynos4_ppmu_hw *ppmu)
{
	void __iomem *ppmu_base = ppmu->hw_base;

	__raw_writel(0x0, ppmu_base + 0xf000);
}

unsigned long long exynos4_ppc_update(struct exynos4_ppmu_hw *ppmu)
{
	void __iomem *ppmu_base = ppmu->hw_base;
	unsigned int i;

	ppmu->ccnt = __raw_readl(ppmu_base + 0xf100);

	for (i = 0; i < NUMBER_OF_COUNTER; i++)
		ppmu->count[i] =
			__raw_readl(ppmu_base + (0xf110 + (0x10 * i)));

	return 0;
}

