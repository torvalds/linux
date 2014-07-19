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

void busfreq_mon_reset(struct busfreq_ppmu_data *ppmu_data)
{
	unsigned int i;

	for (i = 0; i < ppmu_data->ppmu_end; i++) {
		void __iomem *ppmu_base = ppmu_data->ppmu[i].hw_base;

		/* Reset the performance and cycle counters */
		exynos_ppmu_reset(ppmu_base);

		/* Setup count registers to monitor read/write transactions */
		ppmu_data->ppmu[i].event[PPMU_PMNCNT3] = RDWR_DATA_COUNT;
		exynos_ppmu_setevent(ppmu_base, PPMU_PMNCNT3,
					ppmu_data->ppmu[i].event[PPMU_PMNCNT3]);

		exynos_ppmu_start(ppmu_base);
	}
}

void exynos_read_ppmu(struct busfreq_ppmu_data *ppmu_data)
{
	int i, j;

	for (i = 0; i < ppmu_data->ppmu_end; i++) {
		void __iomem *ppmu_base = ppmu_data->ppmu[i].hw_base;

		exynos_ppmu_stop(ppmu_base);

		/* Update local data from PPMU */
		ppmu_data->ppmu[i].ccnt = __raw_readl(ppmu_base + PPMU_CCNT);

		for (j = PPMU_PMNCNT0; j < PPMU_PMNCNT_MAX; j++) {
			if (ppmu_data->ppmu[i].event[j] == 0)
				ppmu_data->ppmu[i].count[j] = 0;
			else
				ppmu_data->ppmu[i].count[j] =
					exynos_ppmu_read(ppmu_base, j);
		}
	}

	busfreq_mon_reset(ppmu_data);
}

int exynos_get_busier_ppmu(struct busfreq_ppmu_data *ppmu_data)
{
	unsigned int count = 0;
	int i, j, busy = 0;

	for (i = 0; i < ppmu_data->ppmu_end; i++) {
		for (j = PPMU_PMNCNT0; j < PPMU_PMNCNT_MAX; j++) {
			if (ppmu_data->ppmu[i].count[j] > count) {
				count = ppmu_data->ppmu[i].count[j];
				busy = i;
			}
		}
	}

	return busy;
}
