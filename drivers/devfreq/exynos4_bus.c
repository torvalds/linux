/* drivers/devfreq/exynos4_bus.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *	MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * EXYNOS4 - Memory/Bus clock frequency scaling support in DEVFREQ framework
 *	This version supports EXYNOS4210 only. This changes bus frequencies
 *	and vddint voltages. Exynos4412/4212 should be able to be supported
 *	with minor modifications.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/opp.h>
#include <linux/devfreq.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/devfreq/exynos4_bus.h>
#include <linux/pm_qos_params.h>

/* Exynos4 ASV has been in the mailing list, but not upstreamed, yet. */

#include <mach/regs-clock.h>
#include <mach/asv.h>
#include <mach/ppmu.h>

#include <plat/map-s5p.h>
#include <plat/cpu.h>

#define MAX_SAFEVOLT	1200000 /* 1.2V */

enum exynos4_busf_type {
	TYPE_BUSF_EXYNOS4210,
	TYPE_BUSF_EXYNOS4212,
	TYPE_BUSF_EXYNOS4412,
};

/* Assume that the bus is saturated if the utilization is 40% or 30(4x12)% */
#define BUS_SATURATION_RATIO	40
#define BUS_SATURATION_RATIO_4x12 30

enum ppmu_counter_ {
	_PPMU_PMNCNT0 = 0,
	_PPMU_PMCCNT1,
	_PPMU_PMNCNT2,
	_PPMU_PMNCNT3,
	_PPMU_PMNCNT_MAX,
};
struct exynos4_ppmu_ {
	void __iomem *hw_base;
	unsigned int ccnt;
	unsigned int event;
	unsigned int count[_PPMU_PMNCNT_MAX];
	bool ccnt_overflow;
	bool count_overflow[_PPMU_PMNCNT_MAX];
};

enum busclk_level_idx {
	LV_0 = 0,
	LV_1,
	LV_2,
	LV_3,
	LV_4,
	LV_5,
	LV_6,
	_LV_END
};
#define EX4210_LV_MAX	LV_2
#define EX4x12_LV_MAX	LV_6
#define EX4210_LV_NUM	(LV_2 + 1)
#define EX4x12_LV_NUM	(LV_6 + 1)

struct busfreq_data {
	enum exynos4_busf_type type;
	struct device *dev;
	struct devfreq *devfreq;
	bool disabled;
	struct regulator *vdd_int;
	struct regulator *vdd_mif; /* Exynos4412/4212 only */
	struct opp *curr_opp;
	struct exynos4_ppmu_ dmc[2];

	/* Fix bus freq during suspend/wakeup */
	struct notifier_block pm_notifier;

	/* Gurantee high freq with high cpu freq */
	struct notifier_block cpuf_notifier;
	struct pm_qos_request_list cpuf_enforce;

	struct mutex lock;

	/* Dividers calculated at boot/probe-time */
	unsigned int dmc_divtable[_LV_END]; /* DMC0 */
	unsigned int top_divtable[_LV_END];

	/* Exynos4x12 uses DMC_PAUSE */
	unsigned int dmc_pause_ctrl;
};

struct bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
};

/* 4210 controls clock of mif and voltage of int */
static struct bus_opp_table exynos4210_busclk_table[] = {
	{LV_0, 400000, 1150000},
	{LV_1, 267000, 1050000},
	{LV_2, 133000, 1025000},
	{0, 0, 0},
};

/*
 * MIF is the main control knob clock for exynox4x12 MIF/INT
 * clock and voltage of both mif/int are controlled.
 */
static struct bus_opp_table exynos4x12_mifclk_table[] = {
	{LV_0, 400266, 1100000}, /* MIF : 400MHz INT : 266MHz */
	{LV_1, 400200, 1100000}, /* MIF : 400MHz INT : 200MHz */
	{LV_2, 267200, 1000000}, /* MIF : 267MHz INT : 200MHz */
	{LV_3, 267160, 1000000}, /* MIF : 267MHz INT : 160MHz */
	{LV_4, 160160, 950000},  /* MIF : 160MHz INT : 160MHz */
	{LV_5, 133133, 950000},  /* MIF : 133MHz INT : 133MHz */
	{LV_6, 100100, 950000},  /* MIF : 100MHz INT : 100MHz */
	{0, 0, 0},
};

/*
 * INT is not the control knob of 4x12. LV_x is not meant to represent
 * the current performance. (MIF does)
 */
static struct bus_opp_table exynos4x12_intclk_table[] = {
	{LV_0, 266000, 1100000},
	{LV_1, 200000, 1000000},
	{LV_2, 200000, 1000000},
	{LV_3, 160000, 950000},
	{LV_4, 160000, 950000},
	{LV_5, 133000, 925000},
	{LV_6, 100000, 900000},
	{0, 0, 0},
};

/* TODO: asv volt definitions are "__initdata"? */
/* Some chips have different operating voltages */
static unsigned int exynos4210_asv_volt[][EX4210_LV_NUM] = {
	{1150000, 1050000, 1050000},
	{1125000, 1025000, 1025000},
	{1100000, 1000000, 1000000},
	{1075000, 975000, 975000},
	{1050000, 950000, 950000},
};

/* DVFS Tables of Version 20120210 */
static unsigned int exynos4212_mif_volt[][_LV_END] = {
	/* 400      267      160     133     100 */
	{1012500, 962500,  912500, 912500, 912500}, /* RESERVED */
	{1000000, 950000,  900000, 900000, 900000}, /* ASV1 */
	{1000000, 950000,  900000, 900000, 900000}, /* ASV2 */
	{1000000, 950000,  900000, 900000, 900000}, /* ASV3 */
	{1050000, 1000000, 900000, 900000, 900000}, /* ASV4 */
	{1000000, 950000,  900000, 900000, 900000}, /* ASV5 */
	{1000000, 950000,  900000, 900000, 900000}, /* ASV6 */
	{950000,  900000,  900000, 900000, 900000}, /* ASV7 */
	{950000,  900000,  900000, 900000, 850000}, /* ASV8 */
	{950000,  900000,  900000, 900000, 850000}, /* ASV9 */
	{950000,  900000,  900000, 850000, 850000}, /* ASV10 */
	{937500,  887500,  887500, 850000, 850000}, /* RESERVED */
};

static unsigned int exynos4212_int_volt[][_LV_END] = {
	/* 266       200    160      133     100 */
	{1300000, 1250000, 950000, 912500, 887500}, /* RESERVED */
	{1062500, 1012500, 937500, 900000, 875000}, /* ASV1 */
	{1050000, 1000000, 925000, 887500, 875000}, /* ASV2 */
	{1050000, 1000000, 912500, 887500, 875000}, /* ASV3 */
	{1062500, 1012500, 925000, 900000, 875000}, /* ASV4 */
	{1050000, 1000000, 925000, 887500, 875000}, /* ASV5 */
	{1050000, 1000000, 912500, 887500, 875000}, /* ASV6 */
	{1037500, 987500,  912500, 875000, 875000}, /* ASV7 */
	{1037500, 987500,  900000, 875000, 875000}, /* ASV8 */
	{1037500, 987500,  900000, 875000, 875000}, /* ASV9 */
	{1037500, 987500,  900000, 862500, 850000}, /* ASV10 */
	{1035000, 975000,  887500, 850000, 850000}, /* RESERVED */
};

static unsigned int exynos4412_mif_volt[][_LV_END] = {
	/* 400      400      267      267      160     133     100 */
	{1100000, 1100000, 1000000, 1000000, 950000, 950000, 950000}, /* RESERVED */
	{1050000, 1050000, 950000,  950000,  900000, 900000, 900000}, /* RESERVED */
	{1050000, 1050000, 950000,  950000,  900000, 900000, 900000}, /* ASV2 */
	{1050000, 1050000, 950000,  950000,  900000, 900000, 900000}, /* ASV3 */
	{1050000, 1050000, 950000,  950000,  900000, 900000, 900000}, /* ASV4 */
	{1000000, 1000000, 950000,  950000,  900000, 900000, 900000}, /* ASV5 */
	{1000000, 1000000, 950000,  950000,  900000, 900000, 900000}, /* ASV6 */
	{1000000, 1000000, 950000,  950000,  900000, 900000, 900000}, /* ASV7 */
	{1000000, 1000000, 950000,  950000,  900000, 900000, 900000}, /* ASV8 */
	{1000000, 1000000, 950000,  950000,  900000, 900000, 850000}, /* ASV9 */
	{1000000, 1000000, 900000,  900000,  900000, 900000, 850000}, /* ASV10 */
	{1000000, 1000000, 900000,  900000,  900000, 900000, 850000}, /* RESERVED */
};

static unsigned int exynos4412_int_volt[][_LV_END] = {
  /* GDR : 266       200      200     160    160      133     100 */
	{1112500, 1062500, 1062500, 975000, 975000, 937500, 900000}, /* RESERVED */
	{1100000, 1050000, 1050000, 962500, 962500, 925000, 887500}, /* RESERVED */
	{1075000, 1025000, 1025000, 937500, 937500, 912500, 875000}, /* ASV2 */
	{1062500, 1012500, 1012500, 937500, 937500, 900000, 862500}, /* ASV3 */
	{1062500, 1012500, 1012500, 925000, 925000, 900000, 862500}, /* ASV4 */
	{1050000, 1000000, 1000000, 925000, 925000, 887500, 850000}, /* ASV5 */
	{1050000, 1000000, 1000000, 912500, 912500, 875000, 850000}, /* ASV6 */
	{1037500,  987500,  987500, 912500, 912500, 862500, 850000}, /* ASV7 */
	{1037500,  987500,  987500, 900000, 900000, 862500, 850000}, /* ASV8 */
	{1037500,  987500,  987500, 900000, 900000, 862500, 850000}, /* ASV9 */
	{1037500,  987500,  987500, 900000, 900000, 862500, 850000}, /* ASV10 */
	{1025000,  975000,  975000, 887500, 887500, 850000, 850000}, /* RESERVED */
};

static unsigned int exynos4x12_qos_value[][4] = {
	{0x06, 0x0b, 0x06, 0x0f},
	{0x06, 0x0b, 0x06, 0x0f},
	{0x06, 0x0b, 0x06, 0x0f},
	{0x06, 0x0b, 0x06, 0x0f},
	{0x06, 0x03, 0x06, 0x0e},
	{0x04, 0x03, 0x04, 0x0e},
	{0x03, 0x0b, 0x00, 0x00},
};

static unsigned int exynos4x12_timingrow[] = {
	0x34498691, 0x24488490, 0x154882D0, 0x154882D0, 0x0D488210,
};

/*** Clock Divider Data for Exynos4210 ***/
static unsigned int exynos4210_clkdiv_dmc0[][8] = {
	/*
	 * Clock divider value for following
	 * { DIVACP, DIVACP_PCLK, DIVDPHY, DIVDMC, DIVDMCD
	 *		DIVDMCP, DIVCOPY2, DIVCORE_TIMERS }
	 */

	/* DMC L0: 400MHz */
	{ 3, 1, 1, 1, 1, 1, 3, 1 },
	/* DMC L1: 266.7MHz */
	{ 4, 1, 1, 2, 1, 1, 3, 1 },
	/* DMC L2: 133MHz */
	{ 5, 1, 1, 5, 1, 1, 3, 1 },
};
static unsigned int exynos4210_clkdiv_top[][5] = {
	/*
	 * Clock divider value for following
	 * { DIVACLK200, DIVACLK100, DIVACLK160, DIVACLK133, DIVONENAND }
	 */
	/* ACLK200 L0: 200MHz */
	{ 3, 7, 4, 5, 1 },
	/* ACLK200 L1: 160MHz */
	{ 4, 7, 5, 6, 1 },
	/* ACLK200 L2: 133MHz */
	{ 5, 7, 7, 7, 1 },
};
static unsigned int exynos4210_clkdiv_lr_bus[][2] = {
	/*
	 * Clock divider value for following
	 * { DIVGDL/R, DIVGPL/R }
	 */
	/* ACLK_GDL/R L1: 200MHz */
	{ 3, 1 },
	/* ACLK_GDL/R L2: 160MHz */
	{ 4, 1 },
	/* ACLK_GDL/R L3: 133MHz */
	{ 5, 1 },
};

/*** Clock Divider Data for Exynos4212/4412 ***/
static unsigned int exynos4x12_clkdiv_dmc0[][6] = {
	/*
	 * Clock divider value for following
	 * { DIVACP, DIVACP_PCLK, DIVDPHY, DIVDMC, DIVDMCD
	 *              DIVDMCP}
	 */

	/* DMC L0: 400MHz */
	{3, 1, 1, 1, 1, 1},
	/* DMC L1: 400MHz */
	{3, 1, 1, 1, 1, 1},
	/* DMC L2: 266.7MHz */
	{4, 1, 1, 2, 1, 1},
	/* DMC L3: 266.7MHz */
	{4, 1, 1, 2, 1, 1},
	/* DMC L4: 160MHz */
	{5, 1, 1, 4, 1, 1},
	/* DMC L5: 133MHz */
	{5, 1, 1, 5, 1, 1},
	/* DMC L6: 100MHz */
	{7, 1, 1, 7, 1, 1},
};
static unsigned int exynos4x12_clkdiv_dmc1[][3] = {
	/*
	 * Clock divider value for following
	 * { G2DACP, DIVC2C, DIVC2C_ACLK }
	 */

	/* DMC L0: 400MHz */
	{3, 1, 1},
	/* DMC L1: 400MHz */
	{3, 1, 1},
	/* DMC L2: 266.7MHz */
	{4, 2, 1},
	/* DMC L3: 266.7MHz */
	{4, 2, 1},
	/* DMC L4: 160MHz */
	{5, 4, 1},
	/* DMC L5: 133MHz */
	{5, 5, 1},
	/* DMC L6: 100MHz */
	{7, 7, 1},
};
static unsigned int exynos4x12_clkdiv_top[][5] = {
	/*
	 * Clock divider value for following
	 * { DIVACLK266_GPS, DIVACLK100, DIVACLK160,
		DIVACLK133, DIVONENAND }
	 */

	/* ACLK_GDL/R L0: 266MHz */
	{2, 7, 4, 5, 1},
	/* ACLK_GDL/R L1: 200MHz */
	{2, 7, 4, 5, 1},
	/* ACLK_GDL/R L2: 200MHz */
	{2, 7, 4, 5, 1},
	/* ACLK_GDL/R L3: 160MHz */
	{4, 7, 5, 7, 1},
	/* ACLK_GDL/R L4: 160MHz */
	{4, 7, 5, 7, 1},
	/* ACLK_GDL/R L5: 133MHz */
	{5, 7, 5, 7, 1},
	/* ACLK_GDL/R L6: 100MHz */
	{7, 7, 7, 7, 1},
};
static unsigned int exynos4x12_clkdiv_l_bus[][2] = {
	/*
	 * Clock divider value for following
	 * { DIVGDL, DIVGPL }
	 */

	/* ACLK_GDL L0: 200MHz */
	{3, 1},
	/* ACLK_GDL L1: 200MHz */
	{3, 1},
	/* ACLK_GDL L2: 200MHz */
	{3, 1},
	/* ACLK_GDL L3: 160MHz */
	{4, 1},
	/* ACLK_GDL L4: 160MHz */
	{4, 1},
	/* ACLK_GDL L5: 133MHz */
	{5, 1},
	/* ACLK_GDL L6: 100MHz */
	{7, 1},
};
static unsigned int exynos4x12_clkdiv_r_bus[][2] = {
	/*
	 * Clock divider value for following
	 * { DIVGDL, DIVGPL }
	 */

	/* ACLK_GDR L0: 266MHz */
	{2, 1},
	/* ACLK_GDR L1: 200MHz */
	{3, 1},
	/* ACLK_GDR L2: 200MHz */
	{3, 1},
	/* ACLK_GDR L3: 160MHz */
	{4, 1},
	/* ACLK_GDR L4: 160MHz */
	{4, 1},
	/* ACLK_GDR L5: 133MHz */
	{5, 1},
	/* ACLK_GDR L6: 100MHz */
	{7, 1},
};
static unsigned int exynos4x12_clkdiv_sclkip[][3] = {
	/*
	 * Clock divider value for following
	 * { DIVMFC, DIVJPEG, DIVFIMC0~3}
	 */

	/* SCLK_MFC: 200MHz */
	{3, 3, 4},
	/* SCLK_MFC: 200MHz */
	{3, 3, 4},
	/* SCLK_MFC: 200MHz */
	{3, 3, 4},
	/* SCLK_MFC: 160MHz */
	{4, 4, 5},
	/* SCLK_MFC: 160MHz */
	{4, 4, 5},
	/* SCLK_MFC: 133MHz */
	{5, 5, 5},
	/* SCLK_MFC: 100MHz */
	{7, 7, 7},
};


static int exynos4210_set_busclk(struct busfreq_data *data, struct opp *opp)
{
	unsigned int index;
	unsigned int tmp;

	for (index = LV_0; index < EX4210_LV_NUM; index++)
		if (opp_get_freq(opp) == exynos4210_busclk_table[index].clk)
			break;

	if (index == EX4210_LV_NUM)
		return -EINVAL;

	/* Change Divider - DMC0 */
	tmp = data->dmc_divtable[index];

	__raw_writel(tmp, EXYNOS4_CLKDIV_DMC0);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_DMC0);
	} while (tmp & 0x11111111);

	/* Change Divider - TOP */
	tmp = data->top_divtable[index];

	__raw_writel(tmp, EXYNOS4_CLKDIV_TOP);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_TOP);
	} while (tmp & 0x11111);

	/* Change Divider - LEFTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_LEFTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((exynos4210_clkdiv_lr_bus[index][0] <<
				EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(exynos4210_clkdiv_lr_bus[index][1] <<
				EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_LEFTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_LEFTBUS);
	} while (tmp & 0x11);

	/* Change Divider - RIGHTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_RIGHTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((exynos4210_clkdiv_lr_bus[index][0] <<
				EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(exynos4210_clkdiv_lr_bus[index][1] <<
				EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_RIGHTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_RIGHTBUS);
	} while (tmp & 0x11);

	return 0;
}

static int exynos4x12_set_busclk(struct busfreq_data *data, struct opp *opp)
{
	unsigned int index;
	unsigned int tmp;

	for (index = LV_0; index < EX4x12_LV_NUM; index++)
		if (opp_get_freq(opp) == exynos4x12_mifclk_table[index].clk)
			break;

	if (index == EX4x12_LV_NUM)
		return -EINVAL;

	/* Change Divider - DMC0 */
	tmp = data->dmc_divtable[index];

	__raw_writel(tmp, EXYNOS4_CLKDIV_DMC0);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_DMC0);
	} while (tmp & 0x111111);

	/* Change Divider - DMC1 */
	tmp = __raw_readl(EXYNOS4_CLKDIV_DMC1);

	tmp &= ~(EXYNOS4_CLKDIV_DMC1_G2D_ACP_MASK |
		EXYNOS4_CLKDIV_DMC1_C2C_MASK |
		EXYNOS4_CLKDIV_DMC1_C2CACLK_MASK);

	tmp |= ((exynos4x12_clkdiv_dmc1[index][0] <<
				EXYNOS4_CLKDIV_DMC1_G2D_ACP_SHIFT) |
		(exynos4x12_clkdiv_dmc1[index][1] <<
				EXYNOS4_CLKDIV_DMC1_C2C_SHIFT) |
		(exynos4x12_clkdiv_dmc1[index][2] <<
				EXYNOS4_CLKDIV_DMC1_C2CACLK_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_DMC1);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_DMC1);
	} while (tmp & 0x1011);

	/* Change Divider - TOP */
	tmp = __raw_readl(EXYNOS4_CLKDIV_TOP);

	tmp &= ~(EXYNOS4_CLKDIV_TOP_ACLK266_GPS_MASK |
		EXYNOS4_CLKDIV_TOP_ACLK100_MASK |
		EXYNOS4_CLKDIV_TOP_ACLK160_MASK |
		EXYNOS4_CLKDIV_TOP_ACLK133_MASK |
		EXYNOS4_CLKDIV_TOP_ONENAND_MASK);

	tmp |= ((exynos4x12_clkdiv_top[index][0] <<
				EXYNOS4_CLKDIV_TOP_ACLK266_GPS_SHIFT) |
		(exynos4x12_clkdiv_top[index][1] <<
				EXYNOS4_CLKDIV_TOP_ACLK100_SHIFT) |
		(exynos4x12_clkdiv_top[index][2] <<
				EXYNOS4_CLKDIV_TOP_ACLK160_SHIFT) |
		(exynos4x12_clkdiv_top[index][3] <<
				EXYNOS4_CLKDIV_TOP_ACLK133_SHIFT) |
		(exynos4x12_clkdiv_top[index][4] <<
				EXYNOS4_CLKDIV_TOP_ONENAND_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_TOP);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_TOP);
	} while (tmp & 0x11111);

	/* Change Divider - LEFTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_LEFTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((exynos4x12_clkdiv_l_bus[index][0] <<
				EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(exynos4x12_clkdiv_l_bus[index][1] <<
				EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_LEFTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_LEFTBUS);
	} while (tmp & 0x11);

	/* Change Divider - RIGHTBUS */
	tmp = __raw_readl(EXYNOS4_CLKDIV_RIGHTBUS);

	tmp &= ~(EXYNOS4_CLKDIV_BUS_GDLR_MASK | EXYNOS4_CLKDIV_BUS_GPLR_MASK);

	tmp |= ((exynos4x12_clkdiv_r_bus[index][0] <<
				EXYNOS4_CLKDIV_BUS_GDLR_SHIFT) |
		(exynos4x12_clkdiv_r_bus[index][1] <<
				EXYNOS4_CLKDIV_BUS_GPLR_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_RIGHTBUS);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_RIGHTBUS);
	} while (tmp & 0x11);

	/* Change Divider - MFC */
	tmp = __raw_readl(EXYNOS4_CLKDIV_MFC);

	tmp &= ~(EXYNOS4_CLKDIV_MFC_MASK);

	tmp |= ((exynos4x12_clkdiv_sclkip[index][0] <<
				EXYNOS4_CLKDIV_MFC_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_MFC);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_MFC);
	} while (tmp & 0x1);

	/* Change Divider - JPEG */
	tmp = __raw_readl(EXYNOS4_CLKDIV_CAM1);

	tmp &= ~(EXYNOS4_CLKDIV_CAM1_JPEG_MASK);

	tmp |= ((exynos4x12_clkdiv_sclkip[index][1] <<
				EXYNOS4_CLKDIV_CAM1_JPEG_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_CAM1);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_CAM1);
	} while (tmp & 0x1);

	/* Change Divider - FIMC0~3 */
	tmp = __raw_readl(EXYNOS4_CLKDIV_CAM);

	tmp &= ~(EXYNOS4_CLKDIV_CAM_FIMC0_MASK | EXYNOS4_CLKDIV_CAM_FIMC1_MASK |
		EXYNOS4_CLKDIV_CAM_FIMC2_MASK | EXYNOS4_CLKDIV_CAM_FIMC3_MASK);

	tmp |= ((exynos4x12_clkdiv_sclkip[index][2] <<
				EXYNOS4_CLKDIV_CAM_FIMC0_SHIFT) |
		(exynos4x12_clkdiv_sclkip[index][2] <<
				EXYNOS4_CLKDIV_CAM_FIMC1_SHIFT) |
		(exynos4x12_clkdiv_sclkip[index][2] <<
				EXYNOS4_CLKDIV_CAM_FIMC2_SHIFT) |
		(exynos4x12_clkdiv_sclkip[index][2] <<
				EXYNOS4_CLKDIV_CAM_FIMC3_SHIFT));

	__raw_writel(tmp, EXYNOS4_CLKDIV_CAM);

	do {
		tmp = __raw_readl(EXYNOS4_CLKDIV_STAT_CAM1);
	} while (tmp & 0x1111);

	if (soc_is_exynos4412() && (exynos_result_of_asv > 3)) {
		if (index == LV_4) { /* MIF: 100, INT: 100 */
			exynos4x12_set_abb_member(ABB_INT, ABB_MODE_100V);
			exynos4x12_set_abb_member(ABB_MIF, ABB_MODE_100V);
		} else {
			exynos4x12_set_abb_member(ABB_INT, ABB_MODE_130V);
			exynos4x12_set_abb_member(ABB_MIF, ABB_MODE_130V);
		}
	}

	return 0;
}


static void busfreq_mon_reset(struct busfreq_data *data)
{
	unsigned int i;

	for (i = 0; i < 2; i++) {
		void __iomem *ppmu_base = data->dmc[i].hw_base;

		/* Reset PPMU */
		__raw_writel(0x8000000f, ppmu_base + 0xf010);
		__raw_writel(0x8000000f, ppmu_base + 0xf050);
		__raw_writel(0x6, ppmu_base + 0xf000);
		__raw_writel(0x0, ppmu_base + 0xf100);

		/* Set PPMU Event */
		data->dmc[i].event = 0x6;
		__raw_writel(((data->dmc[i].event << 12) | 0x1),
			     ppmu_base + 0xfc);

		/* Start PPMU */
		__raw_writel(0x1, ppmu_base + 0xf000);
	}
}

static void exynos4210_read_ppmu(struct busfreq_data *data)
{
	int i, j;

	for (i = 0; i < 2; i++) {
		void __iomem *ppmu_base = data->dmc[i].hw_base;
		u32 overflow;

		/* Stop PPMU */
		__raw_writel(0x0, ppmu_base + 0xf000);

		/* Update local data from PPMU */
		overflow = __raw_readl(ppmu_base + 0xf050);

		data->dmc[i].ccnt = __raw_readl(ppmu_base + 0xf100);
		data->dmc[i].ccnt_overflow = overflow & (1 << 31);

		for (j = 0; j < PPMU_PMNCNT_MAX; j++) {
			data->dmc[i].count[j] = __raw_readl(
					ppmu_base + (0xf110 + (0x10 * j)));
			data->dmc[i].count_overflow[j] = overflow & (1 << j);
		}
	}

	busfreq_mon_reset(data);
}

static int exynos4x12_get_dev_status(struct busfreq_data *data,
				     struct devfreq_dev_status *stat)
{
	int id;
	unsigned long long busy, total;

	ppmu_update(data->dev, 3);

	if (ppmu_load[PPMU_DMC0] > ppmu_load[PPMU_DMC1])
		id = PPMU_DMC0;
	else
		id = PPMU_DMC1;

	busy = ppmu_load_detail[0][id];
	total = ppmu_load_detail[1][id];

	while (total > (1ULL << 16)) {
		busy >>= 8;
		total >>= 8;
	}
	busy *= 100;
	total *= BUS_SATURATION_RATIO_4x12;

	stat->busy_time = busy;
	stat->total_time = total;

	ppmu_start(data->dev);
	return 0;
}

static int exynos4x12_get_intspec(unsigned long mifclk)
{
	switch (mifclk) {
	case 400266:
		return LV_0; /* 266000 */
	case 400200:
		return LV_1; /* 200000 */
	case 267200:
		return LV_2; /* 200000 */
	case 267160:
		return LV_3; /* 160000 */
	case 160160:
		return LV_4; /* 160000 */
	case 133133:
		return LV_5; /* 133000 */
	case 100100:
		return LV_6; /* 100000 */
	}

	return -EINVAL;
}

static int exynos4_bus_setvolt(struct busfreq_data *data, struct opp *opp,
			       struct opp *oldopp)
{
	int err = 0, tmp;
	unsigned long volt = opp_get_voltage(opp);

	switch (data->type) {
	case TYPE_BUSF_EXYNOS4210:
		/* OPP represents DMC clock + INT voltage */
		err = regulator_set_voltage(data->vdd_int, volt,
					    MAX_SAFEVOLT);
		break;
	case TYPE_BUSF_EXYNOS4212:
	case TYPE_BUSF_EXYNOS4412:
		/* OPP represents MIF clock + MIF voltage */
		err = regulator_set_voltage(data->vdd_mif, volt,
					    MAX_SAFEVOLT);
		if (err)
			break;

		tmp = exynos4x12_get_intspec(opp_get_freq(opp));
		if (tmp < 0) {
			err = tmp;
			regulator_set_voltage(data->vdd_mif,
					      opp_get_voltage(oldopp),
					      MAX_SAFEVOLT);
			break;
		}
		err = regulator_set_voltage(data->vdd_int,
					    exynos4x12_intclk_table[tmp].volt,
					    MAX_SAFEVOLT);
		/*  Try to recover */
		if (err)
			regulator_set_voltage(data->vdd_mif,
					      opp_get_voltage(oldopp),
					      MAX_SAFEVOLT);
		break;
	default:
		err = -EINVAL;
	}

	return err;
}


/**
 * exynos4x12_set_qos() - Apply QoS registers (GDL/GDR)
 * @data:
 * @opp:
 */
static void exynos4x12_set_qos(struct busfreq_data *data, struct opp *opp)
{
	int index;

	switch (opp_get_freq(opp)) {
	case 400266:
		index = 0;
		break;
	case 400200:
		index = 1;
		break;
	case 267200:
		index = 2;
		break;
	case 267160:
		index = 3;
		break;
	case 160160:
		index = 4;
		break;
	case 133133:
		index = 5;
		break;
	case 100100:
		index = 6;
		break;
	default:
		dev_err(data->dev, "Incorrect OPP configuration.\n");
		return;
	}

	__raw_writel(exynos4x12_qos_value[index][0], S5P_VA_GDL + 0x400);
	__raw_writel(exynos4x12_qos_value[index][1], S5P_VA_GDL + 0x404);
	__raw_writel(exynos4x12_qos_value[index][2], S5P_VA_GDR + 0x400);
	__raw_writel(exynos4x12_qos_value[index][3], S5P_VA_GDR + 0x404);
}

static int exynos4_bus_target(struct device *dev, unsigned long *_freq,
			      u32 options)
{
	int err = 0;
	unsigned long def = *_freq;
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data *data = platform_get_drvdata(pdev);
	struct opp *opp = devfreq_recommended_opp(dev, _freq, options &
						  DEVFREQ_OPTION_FREQ_GLB);
	unsigned long freq = opp_get_freq(opp);
	unsigned long old_freq = opp_get_freq(data->curr_opp);

	if (IS_ERR(opp))
		return PTR_ERR(opp);

	if (old_freq == freq)
		return 0;

	dev_dbg(dev, "targetting %lukHz %luuV (%luuV)\n", freq, opp_get_voltage(opp), def);

	mutex_lock(&data->lock);

	if (data->disabled)
		goto out;

	if (old_freq < freq)
		err = exynos4_bus_setvolt(data, opp, data->curr_opp);
	if (err)
		goto out;

	if (old_freq != freq) {
		switch (data->type) {
		case TYPE_BUSF_EXYNOS4210:
			err = exynos4210_set_busclk(data, opp);
			break;
		case TYPE_BUSF_EXYNOS4212:
		case TYPE_BUSF_EXYNOS4412:
			exynos4x12_set_qos(data, opp);
			err = exynos4x12_set_busclk(data, opp);
			break;
		default:
			err = -EINVAL;
		}
	}
	if (err)
		goto out;

	if (old_freq > freq)
		err = exynos4_bus_setvolt(data, opp, data->curr_opp);
	if (err)
		goto out;

	data->curr_opp = opp;
out:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos4_get_busier_dmc(struct busfreq_data *data)
{
	u64 p0 = data->dmc[0].count[0];
	u64 p1 = data->dmc[1].count[0];

	p0 *= data->dmc[1].ccnt;
	p1 *= data->dmc[0].ccnt;

	if (data->dmc[1].ccnt == 0)
		return 0;

	if (p0 > p1)
		return 0;
	return 1;
}

static int exynos4_bus_get_dev_status(struct device *dev,
				      struct devfreq_dev_status *stat)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data *data = platform_get_drvdata(pdev);
	int busier_dmc;
	int cycles_x2 = 2; /* 2 x cycles */
	void __iomem *addr;
	u32 timing;
	u32 memctrl;

	stat->current_frequency = opp_get_freq(data->curr_opp);

	switch (data->type) {
	case TYPE_BUSF_EXYNOS4210:
		exynos4210_read_ppmu(data);
		break;
	case TYPE_BUSF_EXYNOS4212:
	case TYPE_BUSF_EXYNOS4412:
		return exynos4x12_get_dev_status(data, stat);
		break;
	default:
		return -EINVAL;
	}
	busier_dmc = exynos4_get_busier_dmc(data);

	if (busier_dmc)
		addr = S5P_VA_DMC1;
	else
		addr = S5P_VA_DMC0;

	memctrl = __raw_readl(addr + 0x04); /* one of DDR2/3/LPDDR2 */
	timing = __raw_readl(addr + 0x38); /* CL or WL/RL values */

	switch ((memctrl >> 8) & 0xf) {
	case 0x4: /* DDR2 */
		cycles_x2 = ((timing >> 16) & 0xf) * 2;
		break;
	case 0x5: /* LPDDR2 */
	case 0x6: /* DDR3 */
		cycles_x2 = ((timing >> 8) & 0xf) + ((timing >> 0) & 0xf);
		break;
	default:
		pr_err("%s: Unknown Memory Type(%d).\n", __func__,
		       (memctrl >> 8) & 0xf);
		return -EINVAL;
	}

	/* Number of cycles spent on memory access */
	stat->busy_time = data->dmc[busier_dmc].count[0] / 2 * (cycles_x2 + 2);
	stat->busy_time *= 100 / BUS_SATURATION_RATIO;
	stat->total_time = data->dmc[busier_dmc].ccnt;

	pr_debug("%lu/%lu\n", stat->busy_time, stat->total_time);

	/* If the counters have overflown, retry */
	if (data->dmc[busier_dmc].ccnt_overflow ||
	    data->dmc[busier_dmc].count_overflow[0])
		return -EAGAIN;

	return 0;
}

static void exynos4_bus_exit(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data *data = platform_get_drvdata(pdev);

	devfreq_unregister_opp_notifier(dev, data->devfreq);
}

static struct devfreq_dev_profile exynos4_devfreq_profile = {
	.initial_freq	= 400000,
	.polling_ms	= 50,
	.target		= exynos4_bus_target,
	.get_dev_status	= exynos4_bus_get_dev_status,
	.exit		= exynos4_bus_exit,
};

static int exynos4210_init_tables(struct busfreq_data *data)
{
	u32 tmp;
	int mgrp;
	int i, err = 0;

	tmp = __raw_readl(EXYNOS4_CLKDIV_DMC0);
	for (i = LV_0; i < EX4210_LV_NUM; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_DMC0_ACP_MASK |
			EXYNOS4_CLKDIV_DMC0_ACPPCLK_MASK |
			EXYNOS4_CLKDIV_DMC0_DPHY_MASK |
			EXYNOS4_CLKDIV_DMC0_DMC_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCD_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCP_MASK |
			EXYNOS4_CLKDIV_DMC0_COPY2_MASK |
			EXYNOS4_CLKDIV_DMC0_CORETI_MASK);

		tmp |= ((exynos4210_clkdiv_dmc0[i][0] <<
					EXYNOS4_CLKDIV_DMC0_ACP_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][1] <<
					EXYNOS4_CLKDIV_DMC0_ACPPCLK_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][2] <<
					EXYNOS4_CLKDIV_DMC0_DPHY_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][3] <<
					EXYNOS4_CLKDIV_DMC0_DMC_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][4] <<
					EXYNOS4_CLKDIV_DMC0_DMCD_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][5] <<
					EXYNOS4_CLKDIV_DMC0_DMCP_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][6] <<
					EXYNOS4_CLKDIV_DMC0_COPY2_SHIFT) |
			(exynos4210_clkdiv_dmc0[i][7] <<
					EXYNOS4_CLKDIV_DMC0_CORETI_SHIFT));

		data->dmc_divtable[i] = tmp;
	}

	tmp = __raw_readl(EXYNOS4_CLKDIV_TOP);
	for (i = LV_0; i <  EX4210_LV_NUM; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_TOP_ACLK200_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK100_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK160_MASK |
			EXYNOS4_CLKDIV_TOP_ACLK133_MASK |
			EXYNOS4_CLKDIV_TOP_ONENAND_MASK);

		tmp |= ((exynos4210_clkdiv_top[i][0] <<
					EXYNOS4_CLKDIV_TOP_ACLK200_SHIFT) |
			(exynos4210_clkdiv_top[i][1] <<
					EXYNOS4_CLKDIV_TOP_ACLK100_SHIFT) |
			(exynos4210_clkdiv_top[i][2] <<
					EXYNOS4_CLKDIV_TOP_ACLK160_SHIFT) |
			(exynos4210_clkdiv_top[i][3] <<
					EXYNOS4_CLKDIV_TOP_ACLK133_SHIFT) |
			(exynos4210_clkdiv_top[i][4] <<
					EXYNOS4_CLKDIV_TOP_ONENAND_SHIFT));

		data->top_divtable[i] = tmp;
	}

	tmp = exynos_result_of_asv;

	pr_debug("ASV Group of Exynos4 is %d\n", tmp);
	/* Use merged grouping for voltage */
	switch (tmp) {
	case 0:
		mgrp = 0;
		break;
	case 1:
	case 2:
		mgrp = 1;
		break;
	case 3:
	case 4:
		mgrp = 2;
		break;
	case 5:
	case 6:
		mgrp = 3;
		break;
	case 7:
		mgrp = 4;
		break;
	default:
		pr_warn("Unknown ASV Group. Use max voltage.\n");
		mgrp = 0;
	}

	for (i = LV_0; i < EX4210_LV_NUM; i++)
		exynos4210_busclk_table[i].volt = exynos4210_asv_volt[mgrp][i];

	for (i = LV_0; i < EX4210_LV_NUM; i++) {
		err = opp_add(data->dev, exynos4210_busclk_table[i].clk,
			      exynos4210_busclk_table[i].volt);
		if (err) {
			dev_err(data->dev, "Cannot add opp entries.\n");
			return err;
		}
	}


	return 0;
}

static int exynos4x12_init_tables(struct busfreq_data *data)
{
	unsigned int i;
	unsigned int tmp;
	int ret;

	/* Enable pause function for DREX2 DVFS */
	tmp = __raw_readl(EXYNOS4_DMC_PAUSE_CTRL);
	tmp |= DMC_PAUSE_ENABLE;
	__raw_writel(tmp, EXYNOS4_DMC_PAUSE_CTRL);

	tmp = __raw_readl(EXYNOS4_CLKDIV_DMC0);

	for (i = 0; i <  EX4x12_LV_NUM; i++) {
		tmp &= ~(EXYNOS4_CLKDIV_DMC0_ACP_MASK |
			EXYNOS4_CLKDIV_DMC0_ACPPCLK_MASK |
			EXYNOS4_CLKDIV_DMC0_DPHY_MASK |
			EXYNOS4_CLKDIV_DMC0_DMC_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCD_MASK |
			EXYNOS4_CLKDIV_DMC0_DMCP_MASK);

		tmp |= ((exynos4x12_clkdiv_dmc0[i][0] <<
					EXYNOS4_CLKDIV_DMC0_ACP_SHIFT) |
			(exynos4x12_clkdiv_dmc0[i][1] <<
					EXYNOS4_CLKDIV_DMC0_ACPPCLK_SHIFT) |
			(exynos4x12_clkdiv_dmc0[i][2] <<
					EXYNOS4_CLKDIV_DMC0_DPHY_SHIFT) |
			(exynos4x12_clkdiv_dmc0[i][3] <<
					EXYNOS4_CLKDIV_DMC0_DMC_SHIFT) |
			(exynos4x12_clkdiv_dmc0[i][4] <<
					EXYNOS4_CLKDIV_DMC0_DMCD_SHIFT) |
			(exynos4x12_clkdiv_dmc0[i][5] <<
					EXYNOS4_CLKDIV_DMC0_DMCP_SHIFT));

		data->dmc_divtable[i] = tmp;
	}

	tmp = exynos_result_of_asv;

	if (data->type == TYPE_BUSF_EXYNOS4212) {
		/* EXYNOS 4212 */
		if (tmp >= ARRAY_SIZE(exynos4212_mif_volt))
			tmp = 0;
		pr_info("ASV Group of Exynos4212 is %d\n", tmp);

		for (i = 0; i < EX4x12_LV_NUM; i++) {
			exynos4x12_mifclk_table[i].volt =
				exynos4212_mif_volt[tmp][i];
			exynos4x12_intclk_table[i].volt =
				exynos4212_int_volt[tmp][i];
		}
	} else {
		/* EXYNOS 4412 */
		if (tmp >= ARRAY_SIZE(exynos4412_mif_volt))
			tmp = 0;
		pr_info("ASV Group of Exynos4412 is %d\n", tmp);

		for (i = 0; i < EX4x12_LV_NUM; i++) {
			exynos4x12_mifclk_table[i].volt =
				exynos4412_mif_volt[tmp][i];
			exynos4x12_intclk_table[i].volt =
				exynos4412_int_volt[tmp][i];
		}
	}

	for (i = 0; i < EX4x12_LV_NUM; i++) {
		ret = opp_add(data->dev, exynos4x12_mifclk_table[i].clk,
			      exynos4x12_mifclk_table[i].volt);
		if (ret) {
			dev_err(data->dev, "Fail to add opp entries.\n");
			return ret;
		}
	}

	return 0;
}

#define TIMINGROW_OFFSET	0x34
static int exynos4_busfreq_pm_notifier_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	struct busfreq_data *data = container_of(this, struct busfreq_data,
						 pm_notifier);
	struct opp *opp;
	unsigned long maxfreq = ULONG_MAX;
	int err = 0;
	unsigned int timing0;
	unsigned int index;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		/* Set Fastest and Deactivate DVFS */
		mutex_lock(&data->lock);

		data->disabled = true;

		opp = opp_find_freq_floor(data->dev, &maxfreq);

		err = exynos4_bus_setvolt(data, opp, data->curr_opp);
		if (err)
			goto unlock;

		for (index = LV_0; index < EX4x12_LV_NUM; index++)
			if (opp_get_freq(opp) ==
			    exynos4x12_mifclk_table[index].clk)
				break;

#if 0
		if (data->type == TYPE_BUSF_EXYNOS4212 ||
		    data->type == TYPE_BUSF_EXYNOS4412) {
			timing0 = __raw_readl(S5P_VA_DMC0 + TIMINGROW_OFFSET);
			timing0 |= exynos4x12_timingrow[index];
			__raw_writel(timing0, S5P_VA_DMC0 + TIMINGROW_OFFSET);
			__raw_writel(exynos4x12_timingrow[index],
				     S5P_VA_DMC0 + TIMINGROW_OFFSET);
			__raw_writel(timing0, S5P_VA_DMC1 + TIMINGROW_OFFSET);
			__raw_writel(exynos4x12_timingrow[index],
				     S5P_VA_DMC1 + TIMINGROW_OFFSET);
		}
#endif

		switch (data->type) {
		case TYPE_BUSF_EXYNOS4210:
			err = exynos4210_set_busclk(data, opp);
			break;
		case TYPE_BUSF_EXYNOS4212:
		case TYPE_BUSF_EXYNOS4412:
			err = exynos4x12_set_busclk(data, opp);
			break;
		default:
			err = -EINVAL;
		}
		if (err)
			goto unlock;

		data->curr_opp = opp;
unlock:
		mutex_unlock(&data->lock);
		if (err)
			return err;
		return NOTIFY_OK;
	case PM_POST_RESTORE:
	case PM_POST_SUSPEND:
		/* Reactivate */
		mutex_lock(&data->lock);
		data->disabled = false;
		mutex_unlock(&data->lock);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

/* For Exynos4x12, if CPU >= 1GHz, mem/bus should be >= 160Mhz */
static int exynos4x12_cpuf_notify(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct busfreq_data *data = container_of(this, struct busfreq_data,
						 cpuf_notifier);
	struct cpufreq_freqs *freqs = ptr;

	if (!pm_qos_request_active(&data->cpuf_enforce))
		pm_qos_add_request(&data->cpuf_enforce,
				   PM_QOS_BUS_DMA_THROUGHPUT, 0);

	switch (event) {
	case CPUFREQ_PRECHANGE:
		if (freqs->new >= 1000000)
			pm_qos_update_request(&data->cpuf_enforce, 160000);
		return NOTIFY_OK;
	case CPUFREQ_POSTCHANGE:
		if (freqs->new < 1000000)
			pm_qos_update_request(&data->cpuf_enforce, 0);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static __devinit int exynos4_busfreq_probe(struct platform_device *pdev)
{
	struct busfreq_data *data;
	struct opp *opp;
	struct device *dev = &pdev->dev;
	int err = 0;
	struct clk *ppmu_clk;
	struct exynos4_bus_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct devfreq_pm_qos_table *qos_list;
	int i;

	data = kzalloc(sizeof(struct busfreq_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	data->type = pdev->id_entry->driver_data;
	data->pm_notifier.notifier_call = exynos4_busfreq_pm_notifier_event;
	data->dev = dev;
	data->dmc[0].hw_base = S5P_VA_DMC0;
	data->dmc[1].hw_base = S5P_VA_DMC1;
	mutex_init(&data->lock);

	switch (data->type) {
	case TYPE_BUSF_EXYNOS4210:
		err = exynos4210_init_tables(data);
		break;
	case TYPE_BUSF_EXYNOS4212:
	case TYPE_BUSF_EXYNOS4412:
		err = exynos4x12_init_tables(data);

		/* Enable pause function for DREX2 DVFS */
		data->dmc_pause_ctrl = __raw_readl(EXYNOS4_DMC_PAUSE_CTRL);
		data->dmc_pause_ctrl |= DMC_PAUSE_ENABLE;
		__raw_writel(data->dmc_pause_ctrl, EXYNOS4_DMC_PAUSE_CTRL);

		ppmu_clk = clk_get(NULL, "ppmudmc0");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_dmc0\n");
		clk_enable(ppmu_clk);
		clk_put(ppmu_clk);

		ppmu_clk = clk_get(NULL, "ppmudmc1");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_dmc1\n");
		clk_enable(ppmu_clk);
		clk_put(ppmu_clk);

		ppmu_clk = clk_get(NULL, "ppmucpu");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_cpu\n");
		clk_enable(ppmu_clk);
		clk_put(ppmu_clk);

		ppmu_init(&exynos_ppmu[PPMU_DMC0], dev);
		ppmu_init(&exynos_ppmu[PPMU_DMC1], dev);
		ppmu_init(&exynos_ppmu[PPMU_CPU], dev);
		ppmu_start(data->dev);
		break;
	default:
		dev_err(dev, "Cannot determine the device id %d\n", data->type);
		err = -EINVAL;
	}
	if (err)
		goto err_regulator;

	data->vdd_int = regulator_get(dev, "vdd_int");
	if (IS_ERR(data->vdd_int)) {
		dev_err(dev, "Cannot get the regulator \"vdd_int\"\n");
		err = PTR_ERR(data->vdd_int);
		goto err_regulator;
	}
	if (data->type == TYPE_BUSF_EXYNOS4212 ||
	    data->type == TYPE_BUSF_EXYNOS4412) {
		data->vdd_mif = regulator_get(dev, "vdd_mif");
		if (IS_ERR(data->vdd_mif)) {
			dev_err(dev, "Cannot get the regulator \"vdd_mif\"\n");
			err = PTR_ERR(data->vdd_mif);
			regulator_put(data->vdd_int);
			goto err_regulator;

		}
	}

	opp = opp_find_freq_floor(dev, &exynos4_devfreq_profile.initial_freq);
	if (IS_ERR(opp)) {
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
		       exynos4_devfreq_profile.initial_freq);
		err = PTR_ERR(opp);
		goto err_opp_add;
	}
	data->curr_opp = opp;

	platform_set_drvdata(pdev, data);

	busfreq_mon_reset(data);

	if (pdata->polling_ms)
		exynos4_devfreq_profile.polling_ms = pdata->polling_ms;
	else
		exynos4_devfreq_profile.polling_ms = 50;

	switch (data->type) {
	case TYPE_BUSF_EXYNOS4210:
		qos_list = kzalloc(sizeof(struct devfreq_pm_qos_table) *
				   (EX4210_LV_NUM + 1), GFP_KERNEL);
		for (i = 0; i < EX4210_LV_NUM; i++) {
			qos_list[EX4210_LV_MAX - i].freq =
				exynos4210_busclk_table[i].clk;
			qos_list[EX4210_LV_MAX - i].qos_value =
				exynos4210_busclk_table[i].clk;
		}
		break;
	case TYPE_BUSF_EXYNOS4212:
	case TYPE_BUSF_EXYNOS4412:
		qos_list = kzalloc(sizeof(struct devfreq_pm_qos_table) *
				   (EX4x12_LV_NUM + 1), GFP_KERNEL);
		for (i = 0; i < EX4x12_LV_NUM; i++) {
			qos_list[EX4x12_LV_MAX - i].freq =
				exynos4x12_mifclk_table[i].clk;
			/*
			 * clk / 1000 is added in order to keep compatible
			 * with S.LSI hack (busfreq_opp)'s qos values.
			 */
			qos_list[EX4x12_LV_MAX - i].qos_value =
				exynos4x12_mifclk_table[i].clk +
				(exynos4x12_mifclk_table[i].clk / 1000);
		}
		break;
	default:
		dev_err(dev, "Cannot determine the device id %d\n", data->type);
		err = -EINVAL;
		goto err_opp_add;
	}

	exynos4_devfreq_profile.qos_type = PM_QOS_BUS_DMA_THROUGHPUT;
	exynos4_devfreq_profile.qos_use_max = true;
	exynos4_devfreq_profile.qos_list = qos_list;

	data->devfreq = devfreq_add_device(dev, &exynos4_devfreq_profile,
					   &devfreq_simple_ondemand,
					   (pdata) ? (&pdata->threshold) :
					   NULL);

	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_profile_qos_added;
	}

	devfreq_register_opp_notifier(dev, data->devfreq);

	err = register_pm_notifier(&data->pm_notifier);
	if (err) {
		dev_err(dev, "Failed to setup pm notifier\n");
		goto err_devfreq_add;
	}

	if (data->type == TYPE_BUSF_EXYNOS4212 ||
	    data->type == TYPE_BUSF_EXYNOS4412) {
		data->cpuf_notifier.notifier_call = exynos4x12_cpuf_notify;
		err = cpufreq_register_notifier(&data->cpuf_notifier,
						CPUFREQ_TRANSITION_NOTIFIER);
		if (err) {
			dev_err(dev, "Failed to setup cpufreq notifier\n");
			goto err_cpufreq_add;
		}
	}

	return 0;
err_cpufreq_add:
	unregister_pm_notifier(&data->pm_notifier);
err_devfreq_add:
	devfreq_remove_device(data->devfreq);
err_profile_qos_added:
	kfree(qos_list);
err_opp_add:
	if (data->vdd_mif)
		regulator_put(data->vdd_mif);
	regulator_put(data->vdd_int);
err_regulator:
	kfree(data);
	return err;
}

static __devexit int exynos4_busfreq_remove(struct platform_device *pdev)
{
	struct busfreq_data *data = platform_get_drvdata(pdev);
	struct clk *ppmu_clk;

	if (data->type == TYPE_BUSF_EXYNOS4212 ||
	    data->type == TYPE_BUSF_EXYNOS4412) {
		ppmu_clk = clk_get(NULL, "ppmudmc0");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_dmc0\n");
		clk_disable(ppmu_clk);
		clk_put(ppmu_clk);

		ppmu_clk = clk_get(NULL, "ppmudmc1");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_dmc1\n");
		clk_disable(ppmu_clk);
		clk_put(ppmu_clk);

		ppmu_clk = clk_get(NULL, "ppmucpu");
		if (IS_ERR(ppmu_clk))
			printk(KERN_ERR "failed to get ppmu_cpu\n");
		clk_disable(ppmu_clk);
		clk_put(ppmu_clk);

		cpufreq_unregister_notifier(&data->cpuf_notifier,
					    CPUFREQ_TRANSITION_NOTIFIER);
	}

	unregister_pm_notifier(&data->pm_notifier);
	devfreq_remove_device(data->devfreq);
	kfree(data->devfreq->profile->qos_list);
	regulator_put(data->vdd_int);
	if (data->vdd_mif)
		regulator_put(data->vdd_mif);
	kfree(data);

	return 0;
}

static int exynos4_busfreq_suspend(struct device *dev)
{
	return 0;
}

static int exynos4_busfreq_resume(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data *data = platform_get_drvdata(pdev);

	ppmu_reset(data->dev);

	if (data->type == TYPE_BUSF_EXYNOS4212 ||
	    data->type == TYPE_BUSF_EXYNOS4412)
		__raw_writel(data->dmc_pause_ctrl, EXYNOS4_DMC_PAUSE_CTRL);

	return 0;
}

static const struct dev_pm_ops exynos4_busfreq_pm = {
	.suspend = exynos4_busfreq_suspend,
	.resume	= exynos4_busfreq_resume,
};

static const struct platform_device_id exynos4_busfreq_id[] = {
	{ "exynos4210-busfreq", TYPE_BUSF_EXYNOS4210 },
	{ "exynos4412-busfreq", TYPE_BUSF_EXYNOS4412 },
	{ "exynos4212-busfreq", TYPE_BUSF_EXYNOS4212 },
	{ },
};

static struct platform_driver exynos4_busfreq_driver = {
	.probe	= exynos4_busfreq_probe,
	.remove	= __devexit_p(exynos4_busfreq_remove),
	.id_table = exynos4_busfreq_id,
	.driver = {
		.name	= "exynos-busfreq",
		.owner	= THIS_MODULE,
		.pm	= &exynos4_busfreq_pm,
	},
};

static int __init exynos4_busfreq_init(void)
{
	return platform_driver_register(&exynos4_busfreq_driver);
}
late_initcall(exynos4_busfreq_init);

static void __exit exynos4_busfreq_exit(void)
{
	platform_driver_unregister(&exynos4_busfreq_driver);
}
module_exit(exynos4_busfreq_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EXYNOS4 busfreq driver with devfreq framework");
MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_ALIAS("exynos-busfreq");
