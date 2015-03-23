/*
 * exynos_ppmu.h - EXYNOS PPMU header file
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_PPMU_H__
#define __EXYNOS_PPMU_H__

enum ppmu_state {
	PPMU_DISABLE = 0,
	PPMU_ENABLE,
};

enum ppmu_counter {
	PPMU_PMNCNT0 = 0,
	PPMU_PMNCNT1,
	PPMU_PMNCNT2,
	PPMU_PMNCNT3,

	PPMU_PMNCNT_MAX,
};

enum ppmu_event_type {
	PPMU_RO_BUSY_CYCLE_CNT	= 0x0,
	PPMU_WO_BUSY_CYCLE_CNT	= 0x1,
	PPMU_RW_BUSY_CYCLE_CNT	= 0x2,
	PPMU_RO_REQUEST_CNT	= 0x3,
	PPMU_WO_REQUEST_CNT	= 0x4,
	PPMU_RO_DATA_CNT	= 0x5,
	PPMU_WO_DATA_CNT	= 0x6,
	PPMU_RO_LATENCY		= 0x12,
	PPMU_WO_LATENCY		= 0x16,
};

enum ppmu_reg {
	/* PPC control register */
	PPMU_PMNC		= 0x00,
	PPMU_CNTENS		= 0x10,
	PPMU_CNTENC		= 0x20,
	PPMU_INTENS		= 0x30,
	PPMU_INTENC		= 0x40,
	PPMU_FLAG		= 0x50,

	/* Cycle Counter and Performance Event Counter Register */
	PPMU_CCNT		= 0x100,
	PPMU_PMCNT0		= 0x110,
	PPMU_PMCNT1		= 0x120,
	PPMU_PMCNT2		= 0x130,
	PPMU_PMCNT3_HIGH	= 0x140,
	PPMU_PMCNT3_LOW		= 0x150,

	/* Bus Event Generator */
	PPMU_BEVT0SEL		= 0x1000,
	PPMU_BEVT1SEL		= 0x1100,
	PPMU_BEVT2SEL		= 0x1200,
	PPMU_BEVT3SEL		= 0x1300,
	PPMU_COUNTER_RESET	= 0x1810,
	PPMU_READ_OVERFLOW_CNT	= 0x1810,
	PPMU_READ_UNDERFLOW_CNT	= 0x1814,
	PPMU_WRITE_OVERFLOW_CNT	= 0x1850,
	PPMU_WRITE_UNDERFLOW_CNT = 0x1854,
	PPMU_READ_PENDING_CNT	= 0x1880,
	PPMU_WRITE_PENDING_CNT	= 0x1884
};

/* PMNC register */
#define PPMU_PMNC_CC_RESET_SHIFT	2
#define PPMU_PMNC_COUNTER_RESET_SHIFT	1
#define PPMU_PMNC_ENABLE_SHIFT		0
#define PPMU_PMNC_START_MODE_MASK	BIT(16)
#define PPMU_PMNC_CC_DIVIDER_MASK	BIT(3)
#define PPMU_PMNC_CC_RESET_MASK		BIT(2)
#define PPMU_PMNC_COUNTER_RESET_MASK	BIT(1)
#define PPMU_PMNC_ENABLE_MASK		BIT(0)

/* CNTENS/CNTENC/INTENS/INTENC/FLAG register */
#define PPMU_CCNT_MASK			BIT(31)
#define PPMU_PMCNT3_MASK		BIT(3)
#define PPMU_PMCNT2_MASK		BIT(2)
#define PPMU_PMCNT1_MASK		BIT(1)
#define PPMU_PMCNT0_MASK		BIT(0)

/* PPMU_PMNCTx/PPMU_BETxSEL registers */
#define PPMU_PMNCT(x)			(PPMU_PMCNT0 + (0x10 * x))
#define PPMU_BEVTxSEL(x)		(PPMU_BEVT0SEL + (0x100 * x))

#endif /* __EXYNOS_PPMU_H__ */
