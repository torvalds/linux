/* linux/arch/arm/mach-exynos/include/mach/ppmu.h
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

#ifndef __ASM_ARCH_PPMU_H
#define __ASM_ARCH_PPMU_H __FILE__

#define NUMBER_OF_COUNTER	4

#define PPMU_CNTENS	0x10
#define PPMU_CNTENC	0x20
#define PPMU_INTENS	0x30
#define PPMU_INTENC	0x40
#define PPMU_FLAG	0x50

#define PPMU_CCNT		0x100
#define PPMU_PMCNT0		0x110
#define PPMU_PMCNT_OFFSET	0x10

#define PPMU_BEVT0SEL		0x1000
#define PPMU_BEVTSEL_OFFSET	0x100
#define PPMU_CNT_RESET		0x1800

#define DEVT0_SEL	0x1000
#define DEVT0_ID	0x1010
#define DEVT0_IDMSK	0x1014
#define DEVT_ID_OFFSET	0x100

#define DEFAULT_WEIGHT	1

#define MAX_CCNT	100

/* For flags */
#define VIDEO_DOMAIN	0x00000001
#define AUDIO_DOMAIN	0x00000002
#define ALL_DOMAIN	0xffffffff

/* For event */
#define RD_DATA_COUNT		0x00000005
#define WR_DATA_COUNT		0x00000006
#define RDWR_DATA_COUNT		0x00000007

#define PMCNT_OFFSET(i)		(PPMU_PMCNT0 + (PPMU_PMCNT_OFFSET * i))

enum ppmu_counter {
	PPMU_PMNCNT0,
	PPMU_PMCCNT1,
	PPMU_PMNCNT2,
	PPMU_PMNCNT3,
	PPMU_PMNCNT_MAX,
};

enum ppmu_ch {
	DMC0,
	DMC1,
};

enum ppmu_type {
	PPMU_MIF,
	PPMU_INT,
	PPMU_TYPE_END,
};

enum exynos4_ppmu {
	PPMU_DMC0,
	PPMU_DMC1,
	PPMU_CPU,
#ifdef CONFIG_ARCH_EXYNOS5
	PPMU_DDR_C,
	PPMU_DDR_R1,
	PPMU_DDR_L,
	PPMU_RIGHT0_BUS,
#endif
	PPMU_END,
};

extern unsigned long long ppmu_load[PPMU_END];

struct exynos4_ppmu_hw {
	struct list_head node;
	void __iomem *hw_base;
	unsigned int ccnt;
	unsigned int event[NUMBER_OF_COUNTER];
	unsigned int weight;
	int usage;
	int id;
	struct device *dev;
	unsigned int count[NUMBER_OF_COUNTER];
};

void exynos4_ppc_reset(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppc_start(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppc_stop(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppc_setevent(struct exynos4_ppmu_hw *ppmu,
				  unsigned int evt_num);
unsigned long long exynos4_ppc_update(struct exynos4_ppmu_hw *ppmu);

void exynos4_ppmu_reset(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppmu_start(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppmu_stop(struct exynos4_ppmu_hw *ppmu);
void exynos4_ppmu_setevent(struct exynos4_ppmu_hw *ppmu,
				   unsigned int evt_num);
unsigned long long exynos4_ppmu_update(struct exynos4_ppmu_hw *ppmu, int ch);

void ppmu_init(struct exynos4_ppmu_hw *ppmu, struct device *dev);
void ppmu_start(struct device *dev);
void ppmu_update(struct device *dev, int ch);
void ppmu_reset(struct device *dev);

extern struct exynos4_ppmu_hw exynos_ppmu[];
#endif /* __ASM_ARCH_PPMU_H */

