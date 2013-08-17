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

#ifndef __DEVFREQ_EXYNOS_PPMU_H
#define __DEVFREQ_EXYNOS_PPMU_H __FILE__

#include <linux/ktime.h>

#define PPMU_CNTENS		0x10

#define PPMU_FLAG		0x50
#define  PPMU_CCNT_OVERFLOW	BIT(31)
#define PPMU_CCNT		0x100
#define PPMU_PMCNT0		0x110
#define PPMU_PMCNT_OFFSET	0x10
#define PMCNT_OFFSET(x)		(PPMU_PMCNT0 + (PPMU_PMCNT_OFFSET * x))

#define PPMU_BEVT0SEL		0x1000
#define PPMU_BEVTSEL_OFFSET	0x100
#define PPMU_BEVTSEL(x)		(PPMU_BEVT0SEL + (ch * PPMU_BEVTSEL_OFFSET))

#define PPMU_CNT_RESET		0x1800

/* For Event Selection */
#define RD_DATA_COUNT		0x5
#define WR_DATA_COUNT		0x6
#define RDWR_DATA_COUNT		0x7

enum ppmu_counter {
	PPMU_PMNCNT0,
	PPMU_PMCCNT1,
	PPMU_PMNCNT2,
	PPMU_PMNCNT3,
	PPMU_PMNCNT_MAX,
};

struct bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

struct exynos_ppmu {
	void __iomem *hw_base;
	unsigned int ccnt;
	unsigned int event[PPMU_PMNCNT_MAX];
	unsigned int count[PPMU_PMNCNT_MAX];
	unsigned long long ns;
	ktime_t reset_time;
	bool ccnt_overflow;
	bool count_overflow[PPMU_PMNCNT_MAX];
};

void exynos_ppmu_reset(void __iomem *ppmu_base);
void exynos_ppmu_setevent(void __iomem *ppmu_base,
			  unsigned int ch,
			  unsigned int evt);
void exynos_ppmu_start(void __iomem *ppmu_base);
void exynos_ppmu_stop(void __iomem *ppmu_base);
unsigned int exynos_ppmu_read(void __iomem *ppmu_base,
			      unsigned int ch);
#endif /* __DEVFREQ_EXYNOS_PPMU_H */

