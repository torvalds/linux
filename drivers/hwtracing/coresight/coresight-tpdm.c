// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/bitmap.h>
#include <linux/of.h>
#include <linux/coresight.h>
#include <linux/regulator/consumer.h>
#include <linux/qcom_scm.h>
#include <linux/suspend.h>

#include "coresight-priv.h"
#include "coresight-common.h"

#define tpdm_writel(drvdata, val, off)	__raw_writel((val), drvdata->base + off)
#define tpdm_readl(drvdata, off)		__raw_readl(drvdata->base + off)

#define TPDM_LOCK(drvdata)						\
do {									\
	mb(); /* ensure configuration take effect before we lock it */	\
	tpdm_writel(drvdata, 0x0, CORESIGHT_LAR);			\
} while (0)
#define TPDM_UNLOCK(drvdata)						\
do {									\
	tpdm_writel(drvdata, CORESIGHT_UNLOCK, CORESIGHT_LAR);		\
	mb(); /* ensure unlock take effect before we configure */	\
} while (0)

/* GPR Registers */
#define TPDM_GPR_CR(n)		(0x0 + (n * 4))

/* BC Subunit Registers */
#define TPDM_BC_CR		(0x280)
#define TPDM_BC_SATROLL		(0x284)
#define TPDM_BC_CNTENSET	(0x288)
#define TPDM_BC_CNTENCLR	(0x28C)
#define TPDM_BC_INTENSET	(0x290)
#define TPDM_BC_INTENCLR	(0x294)
#define TPDM_BC_TRIG_LO(n)	(0x298 + (n * 4))
#define TPDM_BC_TRIG_HI(n)	(0x318 + (n * 4))
#define TPDM_BC_GANG		(0x398)
#define TPDM_BC_OVERFLOW(n)	(0x39C + (n * 4))
#define TPDM_BC_OVSR		(0x3C0)
#define TPDM_BC_SELR		(0x3C4)
#define TPDM_BC_CNTR_LO		(0x3C8)
#define TPDM_BC_CNTR_HI		(0x3CC)
#define TPDM_BC_SHADOW_LO(n)	(0x3D0 + (n * 4))
#define TPDM_BC_SHADOW_HI(n)	(0x450 + (n * 4))
#define TPDM_BC_SWINC		(0x4D0)
#define TPDM_BC_MSR(n)		(0x4F0 + (n * 4))

/* TC Subunit Registers */
#define TPDM_TC_CR		(0x500)
#define TPDM_TC_CNTENSET	(0x504)
#define TPDM_TC_CNTENCLR	(0x508)
#define TPDM_TC_INTENSET	(0x50C)
#define TPDM_TC_INTENCLR	(0x510)
#define TPDM_TC_TRIG_SEL(n)	(0x514 + (n * 4))
#define TPDM_TC_TRIG_LO(n)	(0x534 + (n * 4))
#define TPDM_TC_TRIG_HI(n)	(0x554 + (n * 4))
#define TPDM_TC_OVSR_GP		(0x580)
#define TPDM_TC_OVSR_IMPL	(0x584)
#define TPDM_TC_SELR		(0x588)
#define TPDM_TC_CNTR_LO		(0x58C)
#define TPDM_TC_CNTR_HI		(0x590)
#define TPDM_TC_SHADOW_LO(n)	(0x594 + (n * 4))
#define TPDM_TC_SHADOW_HI(n)	(0x644 + (n * 4))
#define TPDM_TC_SWINC		(0x700)
#define TPDM_TC_MSR(n)		(0x768 + (n * 4))

/* DSB Subunit Registers */
#define TPDM_DSB_CR		(0x780)
#define TPDM_DSB_TIER		(0x784)
#define TPDM_DSB_TPR(n)		(0x788 + (n * 4))
#define TPDM_DSB_TPMR(n)	(0x7A8 + (n * 4))
#define TPDM_DSB_XPR(n)		(0x7C8 + (n * 4))
#define TPDM_DSB_XPMR(n)	(0x7E8 + (n * 4))
#define TPDM_DSB_EDCR(n)	(0x808 + (n * 4))
#define TPDM_DSB_EDCMR(n)	(0x848 + (n * 4))
#define TPDM_DSB_TESTMODE_DATA(n)	(0x868 + (n * 4))
#define TPDM_DSB_CA_SELECT(n)	(0x86c + (n * 4))
#define TPDM_DSB_TESTMODE_DATA_VALID	(0x888)
#define TPDM_DSB_MSR(n)		(0x980 + (n * 4))

/* CMB/MCMB Subunit Registers */
#define TPDM_CMB_CR		(0xA00)
#define TPDM_CMB_TIER		(0xA04)
#define TPDM_CMB_TPR(n)		(0xA08 + (n * 4))
#define TPDM_CMB_TPMR(n)	(0xA10 + (n * 4))
#define TPDM_CMB_XPR(n)		(0xA18 + (n * 4))
#define TPDM_CMB_XPMR(n)	(0xA20 + (n * 4))
#define TPDM_CMB_MARKR		(0xA28)
#define TPDM_CMB_READCTL	(0xA70)
#define TPDM_CMB_READVAL	(0xA74)
#define TPDM_CMB_MSR(n)		(0xA80 + (n * 4))

/* TPDM Specific Registers */
#define TPDM_ITATBCNTRL		(0xEF0)
#define TPDM_CLK_CTRL		(0x220)
#define TPDM_ITCNTRL		(0xF00)


#define TPDM_DATASETS		32
#define TPDM_BC_MAX_COUNTERS	32
#define TPDM_BC_MAX_OVERFLOW	6
#define TPDM_BC_MAX_MSR		4
#define TPDM_TC_MAX_COUNTERS	44
#define TPDM_TC_MAX_TRIG	8
#define TPDM_TC_MAX_MSR		6
#define TPDM_DSB_MAX_PATT	8
#define TPDM_DSB_MAX_SELECT	8
#define TPDM_DSB_MAX_MSR	32
#define TPDM_DSB_MAX_EDCR	16
#define TPDM_DSB_MAX_LINES	256
#define TPDM_CMB_PATT_CMP	2
#define TPDM_CMB_MAX_MSR	32
#define TPDM_MCMB_MAX_LANES	8

/* DSB programming modes */
#define TPDM_DSB_MODE_CYCACC(val)	BMVAL(val, 0, 2)
#define TPDM_DSB_MODE_PERF		BIT(3)
#define TPDM_DSB_MODE_HPBYTESEL(val)	BMVAL(val, 4, 8)
#define TPDM_MODE_ALL			(0xFFFFFFF)

#define NUM_OF_BITS		32
#define TPDM_GPR_REGS_MAX	160

#define TPDM_TRACE_ID_START	128

#define TPDM_REVISION_A		0
#define TPDM_REVISION_B		1

#define HW_ENABLE_CHECK_VALUE   0x10


#define ATBCNTRL_VAL_32		0xC00F1409
#define ATBCNTRL_VAL_64		0xC01F1409


enum tpdm_dataset {
	TPDM_DS_IMPLDEF,
	TPDM_DS_DSB,
	TPDM_DS_CMB,
	TPDM_DS_TC,
	TPDM_DS_BC,
	TPDM_DS_GPR,
	TPDM_DS_MCMB,
};

enum tpdm_mode {
	TPDM_MODE_ATB,
	TPDM_MODE_APB,
};

enum tpdm_support_type {
	TPDM_SUPPORT_TYPE_FULL,
	TPDM_SUPPORT_TYPE_PARTIAL,
	TPDM_SUPPORT_TYPE_NO,
};

enum tpdm_cmb_patt_bits {
	TPDM_CMB_LSB,
	TPDM_CMB_MSB,
};

#ifdef CONFIG_CORESIGHT_TPDM_DEFAULT_ENABLE
static int boot_enable = 1;
#else
static int boot_enable;
#endif

struct gpr_dataset {
	DECLARE_BITMAP(gpr_dirty, TPDM_GPR_REGS_MAX);
	uint32_t		gp_regs[TPDM_GPR_REGS_MAX];
};

struct bc_dataset {
	enum tpdm_mode		capture_mode;
	enum tpdm_mode		retrieval_mode;
	uint32_t		sat_mode;
	uint32_t		enable_counters;
	uint32_t		clear_counters;
	uint32_t		enable_irq;
	uint32_t		clear_irq;
	uint32_t		trig_val_lo[TPDM_BC_MAX_COUNTERS];
	uint32_t		trig_val_hi[TPDM_BC_MAX_COUNTERS];
	uint32_t		enable_ganging;
	uint32_t		overflow_val[TPDM_BC_MAX_OVERFLOW];
	uint32_t		msr[TPDM_BC_MAX_MSR];
};

struct tc_dataset {
	enum tpdm_mode		capture_mode;
	enum tpdm_mode		retrieval_mode;
	bool			sat_mode;
	uint32_t		enable_counters;
	uint32_t		clear_counters;
	uint32_t		enable_irq;
	uint32_t		clear_irq;
	uint32_t		trig_sel[TPDM_TC_MAX_TRIG];
	uint32_t		trig_val_lo[TPDM_TC_MAX_TRIG];
	uint32_t		trig_val_hi[TPDM_TC_MAX_TRIG];
	uint32_t		msr[TPDM_TC_MAX_MSR];
};

struct dsb_dataset {
	uint32_t		mode;
	uint32_t		edge_ctrl[TPDM_DSB_MAX_EDCR];
	uint32_t		edge_ctrl_mask[TPDM_DSB_MAX_EDCR / 2];
	uint32_t		patt_val[TPDM_DSB_MAX_PATT];
	uint32_t		patt_mask[TPDM_DSB_MAX_PATT];
	bool			patt_ts;
	bool			patt_type;
	uint32_t		trig_patt_val[TPDM_DSB_MAX_PATT];
	uint32_t		trig_patt_mask[TPDM_DSB_MAX_PATT];
	bool			trig_ts;
	bool			trig_type;
	uint32_t		select_val[TPDM_DSB_MAX_SELECT];
	uint32_t		msr[TPDM_DSB_MAX_MSR];
};

struct mcmb_dataset {
	uint8_t		mcmb_trig_lane;
	uint8_t		mcmb_lane_select;
};

struct cmb_dataset {
	bool			trace_mode;
	uint32_t		cycle_acc;
	uint32_t		patt_val[TPDM_CMB_PATT_CMP];
	uint32_t		patt_mask[TPDM_CMB_PATT_CMP];
	bool			patt_ts;
	uint32_t		trig_patt_val[TPDM_CMB_PATT_CMP];
	uint32_t		trig_patt_mask[TPDM_CMB_PATT_CMP];
	bool			trig_ts;
	bool			ts_all;
	uint32_t		msr[TPDM_CMB_MAX_MSR];
	uint8_t			read_ctl_reg;
	struct mcmb_dataset	*mcmb;
};

DEFINE_CORESIGHT_DEVLIST(tpdm_devs, "tpdm");

struct tpdm_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	int			nr_tclk;
	struct clk		**tclk;
	int			nr_treg;
	struct regulator	**treg;
	struct mutex		lock;
	bool			enable;
	bool			clk_enable;
	DECLARE_BITMAP(datasets, TPDM_DATASETS);
	DECLARE_BITMAP(enable_ds, TPDM_DATASETS);
	enum tpdm_support_type	tc_trig_type;
	enum tpdm_support_type	bc_trig_type;
	enum tpdm_support_type	bc_gang_type;
	uint32_t		bc_counters_avail;
	uint32_t		tc_counters_avail;
	struct gpr_dataset	*gpr;
	struct bc_dataset	*bc;
	struct tc_dataset	*tc;
	struct dsb_dataset	*dsb;
	struct cmb_dataset	*cmb;
	int			traceid;
	uint32_t		version;
	bool			msr_support;
	bool			msr_fix_req;
	bool			cmb_msr_skip;
};

static void tpdm_init_default_data(struct tpdm_drvdata *drvdata);

static void __tpdm_enable_gpr(struct tpdm_drvdata *drvdata)
{
	int i;

	for (i = 0; i < TPDM_GPR_REGS_MAX; i++) {
		if (!test_bit(i, drvdata->gpr->gpr_dirty))
			continue;
		tpdm_writel(drvdata, drvdata->gpr->gp_regs[i], TPDM_GPR_CR(i));
	}
}

static void __tpdm_config_bc_msr(struct tpdm_drvdata *drvdata)
{
	int i;

	if (!drvdata->msr_support)
		return;

	for (i = 0; i < TPDM_BC_MAX_MSR; i++)
		tpdm_writel(drvdata, drvdata->bc->msr[i], TPDM_BC_MSR(i));
}

static void __tpdm_config_tc_msr(struct tpdm_drvdata *drvdata)
{
	int i;

	if (!drvdata->msr_support)
		return;

	for (i = 0; i < TPDM_TC_MAX_MSR; i++)
		tpdm_writel(drvdata, drvdata->tc->msr[i], TPDM_TC_MSR(i));
}

static void __tpdm_config_dsb_msr(struct tpdm_drvdata *drvdata)
{
	int i;

	if (!drvdata->msr_support)
		return;

	for (i = 0; i < TPDM_DSB_MAX_MSR; i++)
		tpdm_writel(drvdata, drvdata->dsb->msr[i], TPDM_DSB_MSR(i));
}

static void __tpdm_config_cmb_msr(struct tpdm_drvdata *drvdata)
{
	int i;

	if (!drvdata->msr_support)
		return;

	for (i = 0; i < TPDM_CMB_MAX_MSR; i++)
		tpdm_writel(drvdata, drvdata->cmb->msr[i], TPDM_CMB_MSR(i));
}

static void __tpdm_enable_bc(struct tpdm_drvdata *drvdata)
{
	int i;
	uint32_t val;

	if (drvdata->bc->sat_mode)
		tpdm_writel(drvdata, drvdata->bc->sat_mode,
			    TPDM_BC_SATROLL);
	else
		tpdm_writel(drvdata, 0x0, TPDM_BC_SATROLL);

	if (drvdata->bc->enable_counters) {
		tpdm_writel(drvdata, 0xFFFFFFFF, TPDM_BC_CNTENCLR);
		tpdm_writel(drvdata, drvdata->bc->enable_counters,
			    TPDM_BC_CNTENSET);
	}
	if (drvdata->bc->clear_counters)
		tpdm_writel(drvdata, drvdata->bc->clear_counters,
			    TPDM_BC_CNTENCLR);

	if (drvdata->bc->enable_irq) {
		tpdm_writel(drvdata, 0xFFFFFFFF, TPDM_BC_INTENCLR);
		tpdm_writel(drvdata, drvdata->bc->enable_irq,
			    TPDM_BC_INTENSET);
	}
	if (drvdata->bc->clear_irq)
		tpdm_writel(drvdata, drvdata->bc->clear_irq,
			    TPDM_BC_INTENCLR);

	if (drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_FULL) {
		for (i = 0; i < drvdata->bc_counters_avail; i++) {
			tpdm_writel(drvdata, drvdata->bc->trig_val_lo[i],
				    TPDM_BC_TRIG_LO(i));
			tpdm_writel(drvdata, drvdata->bc->trig_val_hi[i],
				    TPDM_BC_TRIG_HI(i));
		}
	} else if (drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL) {
		tpdm_writel(drvdata, drvdata->bc->trig_val_lo[0],
			    TPDM_BC_TRIG_LO(0));
		tpdm_writel(drvdata, drvdata->bc->trig_val_hi[0],
			    TPDM_BC_TRIG_HI(0));
	}

	if (drvdata->bc->enable_ganging)
		tpdm_writel(drvdata, drvdata->bc->enable_ganging, TPDM_BC_GANG);

	for (i = 0; i < TPDM_BC_MAX_OVERFLOW; i++)
		tpdm_writel(drvdata, drvdata->bc->overflow_val[i],
			    TPDM_BC_OVERFLOW(i));

	__tpdm_config_bc_msr(drvdata);

	val = tpdm_readl(drvdata, TPDM_BC_CR);
	if (drvdata->bc->retrieval_mode == TPDM_MODE_APB)
		val = val | BIT(2);
	else
		val = val & ~BIT(2);
	tpdm_writel(drvdata, val, TPDM_BC_CR);

	val = tpdm_readl(drvdata, TPDM_BC_CR);
	/* Set the enable bit */
	val = val | BIT(0);
	tpdm_writel(drvdata, val, TPDM_BC_CR);
}

static void __tpdm_enable_tc(struct tpdm_drvdata *drvdata)
{
	int i;
	uint32_t val;

	if (drvdata->tc->enable_counters) {
		tpdm_writel(drvdata, 0xF, TPDM_TC_CNTENCLR);
		tpdm_writel(drvdata, drvdata->tc->enable_counters,
			    TPDM_TC_CNTENSET);
	}
	if (drvdata->tc->clear_counters)
		tpdm_writel(drvdata, drvdata->tc->clear_counters,
			    TPDM_TC_CNTENCLR);

	if (drvdata->tc->enable_irq) {
		tpdm_writel(drvdata, 0xF, TPDM_TC_INTENCLR);
		tpdm_writel(drvdata, drvdata->tc->enable_irq,
			    TPDM_TC_INTENSET);
	}
	if (drvdata->tc->clear_irq)
		tpdm_writel(drvdata, drvdata->tc->clear_irq,
			    TPDM_TC_INTENCLR);

	if (drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_FULL) {
		for (i = 0; i < TPDM_TC_MAX_TRIG; i++) {
			tpdm_writel(drvdata, drvdata->tc->trig_sel[i],
				    TPDM_TC_TRIG_SEL(i));
			tpdm_writel(drvdata, drvdata->tc->trig_val_lo[i],
				    TPDM_TC_TRIG_LO(i));
			tpdm_writel(drvdata, drvdata->tc->trig_val_hi[i],
				    TPDM_TC_TRIG_HI(i));
		}
	} else if (drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL) {
		tpdm_writel(drvdata, drvdata->tc->trig_sel[0],
			    TPDM_TC_TRIG_SEL(0));
		tpdm_writel(drvdata, drvdata->tc->trig_val_lo[0],
			    TPDM_TC_TRIG_LO(0));
		tpdm_writel(drvdata, drvdata->tc->trig_val_hi[0],
			    TPDM_TC_TRIG_HI(0));
	}

	__tpdm_config_tc_msr(drvdata);

	val = tpdm_readl(drvdata, TPDM_TC_CR);
	if (drvdata->tc->sat_mode)
		val = val | BIT(4);
	else
		val = val & ~BIT(4);
	if (drvdata->tc->retrieval_mode == TPDM_MODE_APB)
		val = val | BIT(2);
	else
		val = val & ~BIT(2);
	tpdm_writel(drvdata, val, TPDM_TC_CR);

	val = tpdm_readl(drvdata, TPDM_TC_CR);
	/* Set the enable bit */
	val = val | BIT(0);
	tpdm_writel(drvdata, val, TPDM_TC_CR);
}

static void __tpdm_enable_dsb(struct tpdm_drvdata *drvdata)
{
	uint32_t val, mode, i;

	for (i = 0; i < TPDM_DSB_MAX_EDCR; i++)
		tpdm_writel(drvdata, drvdata->dsb->edge_ctrl[i],
			    TPDM_DSB_EDCR(i));
	for (i = 0; i < TPDM_DSB_MAX_EDCR / 2; i++)
		tpdm_writel(drvdata, drvdata->dsb->edge_ctrl_mask[i],
			    TPDM_DSB_EDCMR(i));

	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		tpdm_writel(drvdata, drvdata->dsb->patt_val[i],
			    TPDM_DSB_TPR(i));
		tpdm_writel(drvdata, drvdata->dsb->patt_mask[i],
			    TPDM_DSB_TPMR(i));
	}

	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		tpdm_writel(drvdata, drvdata->dsb->trig_patt_val[i],
			    TPDM_DSB_XPR(i));
		tpdm_writel(drvdata, drvdata->dsb->trig_patt_mask[i],
			    TPDM_DSB_XPMR(i));
	}

	for (i = 0; i < TPDM_DSB_MAX_SELECT; i++)
		tpdm_writel(drvdata, drvdata->dsb->select_val[i],
			    TPDM_DSB_CA_SELECT(i));

	val = tpdm_readl(drvdata, TPDM_DSB_TIER);
	if (drvdata->dsb->patt_ts) {
		val = val | BIT(0);
		if (drvdata->dsb->patt_type)
			val = val | BIT(2);
		else
			val = val & ~BIT(2);
	} else {
		val = val & ~BIT(0);
	}
	if (drvdata->dsb->trig_ts)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);
	tpdm_writel(drvdata, val, TPDM_DSB_TIER);

	if (!drvdata->msr_fix_req)
		__tpdm_config_dsb_msr(drvdata);

	val = tpdm_readl(drvdata, TPDM_DSB_CR);
	/* Set the cycle accurate mode */
	mode = TPDM_DSB_MODE_CYCACC(drvdata->dsb->mode);
	val = val & ~(0x7 << 9);
	val = val | (mode << 9);
	/* Set the byte lane for high-performance mode */
	mode = TPDM_DSB_MODE_HPBYTESEL(drvdata->dsb->mode);
	val = val & ~(0x1F << 2);
	val = val | (mode << 2);
	/* Set the performance mode */
	if (drvdata->dsb->mode & TPDM_DSB_MODE_PERF)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);

	/* Set trigger type */
	if (drvdata->dsb->trig_type)
		val = val | BIT(12);
	else
		val = val & ~BIT(12);

	tpdm_writel(drvdata, val, TPDM_DSB_CR);

	val = tpdm_readl(drvdata, TPDM_DSB_CR);
	/* Set the enable bit */
	val = val | BIT(0);
	tpdm_writel(drvdata, val, TPDM_DSB_CR);

	if (drvdata->msr_fix_req)
		__tpdm_config_dsb_msr(drvdata);
}

static void __tpdm_enable_cmb(struct tpdm_drvdata *drvdata)
{
	uint32_t val;
	int i;

	for (i = 0; i < TPDM_CMB_PATT_CMP; i++) {
		tpdm_writel(drvdata, drvdata->cmb->patt_val[i],
			    TPDM_CMB_TPR(i));
		tpdm_writel(drvdata, drvdata->cmb->patt_mask[i],
			    TPDM_CMB_TPMR(i));
		tpdm_writel(drvdata, drvdata->cmb->trig_patt_val[i],
			    TPDM_CMB_XPR(i));
		tpdm_writel(drvdata, drvdata->cmb->trig_patt_mask[i],
			    TPDM_CMB_XPMR(i));
	}

	val = tpdm_readl(drvdata, TPDM_CMB_TIER);
	if (drvdata->cmb->patt_ts)
		val = val | BIT(0);
	else
		val = val & ~BIT(0);
	if (drvdata->cmb->trig_ts)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);
	if (drvdata->cmb->ts_all)
		val = val | BIT(2);
	else
		val = val & ~BIT(2);

	tpdm_writel(drvdata, val, TPDM_CMB_TIER);

	if (!drvdata->cmb_msr_skip)
		__tpdm_config_cmb_msr(drvdata);

	val = tpdm_readl(drvdata, TPDM_CMB_CR);
	/* Set the flow control bit */
	val = val & ~BIT(2);
	if (drvdata->cmb->trace_mode)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);

	val = val & ~BM(8, 9);
	val = val | BMVAL(drvdata->cmb->cycle_acc, 0, 1) << 8;
	tpdm_writel(drvdata, val, TPDM_CMB_CR);
	/* Set the enable bit */
	val = val | BIT(0);
	tpdm_writel(drvdata, val, TPDM_CMB_CR);
}

static void __tpdm_enable_mcmb(struct tpdm_drvdata *drvdata)
{
	uint32_t val;
	struct mcmb_dataset *mcmb = drvdata->cmb->mcmb;
	int i;

	for (i = 0; i < TPDM_CMB_PATT_CMP; i++) {
		tpdm_writel(drvdata, drvdata->cmb->patt_val[i],
			    TPDM_CMB_TPR(i));
		tpdm_writel(drvdata, drvdata->cmb->patt_mask[i],
			    TPDM_CMB_TPMR(i));
		tpdm_writel(drvdata, drvdata->cmb->trig_patt_val[i],
			    TPDM_CMB_XPR(i));
		tpdm_writel(drvdata, drvdata->cmb->trig_patt_mask[i],
			    TPDM_CMB_XPMR(i));
	}

	val = tpdm_readl(drvdata, TPDM_CMB_TIER);
	if (drvdata->cmb->patt_ts)
		val = val | BIT(0);
	else
		val = val & ~BIT(0);
	if (drvdata->cmb->trig_ts)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);
	if (drvdata->cmb->ts_all)
		val = val | BIT(2);
	else
		val = val & ~BIT(2);
	tpdm_writel(drvdata, val, TPDM_CMB_TIER);

	if (!drvdata->cmb_msr_skip)
		__tpdm_config_cmb_msr(drvdata);

	val = tpdm_readl(drvdata, TPDM_CMB_CR);
	/* Set the flow control bit */
	val = val & ~BIT(2);
	if (drvdata->cmb->trace_mode)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);

	val = val & ~BM(8, 9);
	val = val | BMVAL(drvdata->cmb->cycle_acc, 0, 1) << 8;
	val = val & ~BM(18, 20);
	val = val | (BMVAL(mcmb->mcmb_trig_lane, 0, 2) << 18);
	val = val & ~BM(10, 17);
	val = val | (BMVAL(mcmb->mcmb_lane_select, 0, 7) << 10);

	tpdm_writel(drvdata, val, TPDM_CMB_CR);
	/* Set the enable bit */
	val = val | BIT(0);
	tpdm_writel(drvdata, val, TPDM_CMB_CR);
}

static void __tpdm_enable(struct tpdm_drvdata *drvdata)
{
	TPDM_UNLOCK(drvdata);

	if (drvdata->clk_enable)
		tpdm_writel(drvdata, 0x1, TPDM_CLK_CTRL);

	if (test_bit(TPDM_DS_GPR, drvdata->enable_ds))
		__tpdm_enable_gpr(drvdata);

	if (test_bit(TPDM_DS_BC, drvdata->enable_ds))
		__tpdm_enable_bc(drvdata);

	if (test_bit(TPDM_DS_TC, drvdata->enable_ds))
		__tpdm_enable_tc(drvdata);

	if (test_bit(TPDM_DS_DSB, drvdata->enable_ds))
		__tpdm_enable_dsb(drvdata);

	if (test_bit(TPDM_DS_CMB, drvdata->enable_ds))
		__tpdm_enable_cmb(drvdata);
	else if (test_bit(TPDM_DS_MCMB, drvdata->enable_ds))
		__tpdm_enable_mcmb(drvdata);

	TPDM_LOCK(drvdata);
}

static int tpdm_enable(struct coresight_device *csdev,
		       struct perf_event *event, u32 mode)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret = 0;

	if (drvdata->enable) {
		dev_err(drvdata->dev,
			"TPDM setup already enabled,Skipping enablei\n");
		return ret;
	}

	mutex_lock(&drvdata->lock);
	__tpdm_enable(drvdata);
	drvdata->enable = true;
	mutex_unlock(&drvdata->lock);

	dev_info(drvdata->dev, "TPDM tracing enabled\n");
	return 0;
}

static void __tpdm_disable_bc(struct tpdm_drvdata *drvdata)
{
	uint32_t config;

	config = tpdm_readl(drvdata, TPDM_BC_CR);
	config = config & ~BIT(0);
	tpdm_writel(drvdata, config, TPDM_BC_CR);
}

static void __tpdm_disable_tc(struct tpdm_drvdata *drvdata)
{
	uint32_t config;

	config = tpdm_readl(drvdata, TPDM_TC_CR);
	config = config & ~BIT(0);
	tpdm_writel(drvdata, config, TPDM_TC_CR);
}

static void __tpdm_disable_dsb(struct tpdm_drvdata *drvdata)
{
	uint32_t config;

	config = tpdm_readl(drvdata, TPDM_DSB_CR);
	config = config & ~BIT(0);
	tpdm_writel(drvdata, config, TPDM_DSB_CR);
}

static void __tpdm_disable_cmb(struct tpdm_drvdata *drvdata)
{
	uint32_t config;

	config = tpdm_readl(drvdata, TPDM_CMB_CR);
	config = config & ~BIT(0);
	tpdm_writel(drvdata, config, TPDM_CMB_CR);
}

static void __tpdm_disable(struct tpdm_drvdata *drvdata)
{
	TPDM_UNLOCK(drvdata);

	if (test_bit(TPDM_DS_BC, drvdata->enable_ds))
		__tpdm_disable_bc(drvdata);

	if (test_bit(TPDM_DS_TC, drvdata->enable_ds))
		__tpdm_disable_tc(drvdata);

	if (test_bit(TPDM_DS_DSB, drvdata->enable_ds))
		__tpdm_disable_dsb(drvdata);

	if (test_bit(TPDM_DS_CMB, drvdata->enable_ds) ||
		test_bit(TPDM_DS_MCMB, drvdata->enable_ds))
		__tpdm_disable_cmb(drvdata);

	if (drvdata->clk_enable)
		tpdm_writel(drvdata, 0x0, TPDM_CLK_CTRL);

	TPDM_LOCK(drvdata);
}

static void tpdm_disable(struct coresight_device *csdev,
			 struct perf_event *event)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (!drvdata->enable) {
		dev_err(drvdata->dev,
			"TPDM setup already disabled, Skipping disable\n");
		return;
	}
	mutex_lock(&drvdata->lock);
	__tpdm_disable(drvdata);
	drvdata->enable = false;
	mutex_unlock(&drvdata->lock);

	dev_info(drvdata->dev, "TPDM tracing disabled\n");
}

static int tpdm_trace_id(struct coresight_device *csdev)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->traceid;
}

static const struct coresight_ops_source tpdm_source_ops = {
	.trace_id	= tpdm_trace_id,
	.enable		= tpdm_enable,
	.disable	= tpdm_disable,
};

static const struct coresight_ops tpdm_cs_ops = {
	.source_ops	= &tpdm_source_ops,
};

static ssize_t available_datasets_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;

	if (test_bit(TPDM_DS_IMPLDEF, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s",
				  "IMPLDEF");

	if (test_bit(TPDM_DS_DSB, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "DSB");

	if (test_bit(TPDM_DS_CMB, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "CMB");

	if (test_bit(TPDM_DS_TC, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "TC");

	if (test_bit(TPDM_DS_BC, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "BC");

	if (test_bit(TPDM_DS_GPR, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "GPR");

	if (test_bit(TPDM_DS_MCMB, drvdata->datasets))
		size += scnprintf(buf + size, PAGE_SIZE - size, "%-8s", "MCMB");

	size += scnprintf(buf + size, PAGE_SIZE - size, "\n");
	return size;
}
static DEVICE_ATTR_RO(available_datasets);

static ssize_t enable_datasets_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size;

	size = scnprintf(buf, PAGE_SIZE, "%*pb\n", TPDM_DATASETS,
			 drvdata->enable_ds);

	if (PAGE_SIZE - size < 2)
		size = -EINVAL;
	else
		size += scnprintf(buf + size, 2, "\n");
	return size;
}

static ssize_t enable_datasets_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	int i;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	for (i = 0; i < TPDM_DATASETS; i++) {
		if (test_bit(i, drvdata->datasets) && (val & BIT(i)))
			__set_bit(i, drvdata->enable_ds);
		else
			__clear_bit(i, drvdata->enable_ds);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(enable_datasets);

static ssize_t reset_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	int ret = 0;
	unsigned long val;
	struct mcmb_dataset *mcmb_temp = NULL;
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&drvdata->lock);
	/* Reset all datasets to ZERO */
	if (drvdata->gpr != NULL)
		memset(drvdata->gpr, 0, sizeof(struct gpr_dataset));

	if (drvdata->bc != NULL)
		memset(drvdata->bc, 0, sizeof(struct bc_dataset));

	if (drvdata->tc != NULL)
		memset(drvdata->tc, 0, sizeof(struct tc_dataset));

	if (drvdata->dsb != NULL)
		memset(drvdata->dsb, 0, sizeof(struct dsb_dataset));

	if (drvdata->cmb != NULL) {
		if (drvdata->cmb->mcmb != NULL) {
			mcmb_temp = drvdata->cmb->mcmb;
			memset(drvdata->cmb->mcmb, 0,
				sizeof(struct mcmb_dataset));
			}

		memset(drvdata->cmb, 0, sizeof(struct cmb_dataset));
		drvdata->cmb->mcmb = mcmb_temp;
	}
	/* Init the default data */
	tpdm_init_default_data(drvdata);

	mutex_unlock(&drvdata->lock);

	/* Disable tpdm if enabled */
	if (drvdata->enable)
		coresight_disable(drvdata->csdev);

	return size;
}
static DEVICE_ATTR_WO(reset);

static ssize_t integration_test_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	int i, ret = 0;
	unsigned long val;
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	if (val != 1 && val != 2)
		return -EINVAL;

	if (!drvdata->enable)
		return -EINVAL;

	if (val == 1)
		val = ATBCNTRL_VAL_64;
	else
		val = ATBCNTRL_VAL_32;
	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, 0x1, TPDM_ITCNTRL);

	for (i = 1; i < 5; i++)
		tpdm_writel(drvdata, val, TPDM_ITATBCNTRL);

	tpdm_writel(drvdata, 0, TPDM_ITCNTRL);
	TPDM_LOCK(drvdata);
	return size;
}
static DEVICE_ATTR_WO(integration_test);

static ssize_t gp_regs_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_GPR, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_GPR_REGS_MAX; i++) {
		if (!test_bit(i, drvdata->gpr->gpr_dirty))
			continue;
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->gpr->gp_regs[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t gp_regs_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_GPR, drvdata->datasets) ||
	    index >= TPDM_GPR_REGS_MAX)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->gpr->gp_regs[index] = val;
	__set_bit(index, drvdata->gpr->gpr_dirty);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(gp_regs);

static ssize_t bc_capture_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 drvdata->bc->capture_mode == TPDM_MODE_ATB ?
			 "ATB" : "APB");
}

static ssize_t bc_capture_mode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	char str[20] = "";
	uint32_t val;

	if (size >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (!strcmp(str, "ATB")) {
		drvdata->bc->capture_mode = TPDM_MODE_ATB;
	} else if (!strcmp(str, "APB") &&
		   drvdata->bc->retrieval_mode == TPDM_MODE_APB) {

		TPDM_UNLOCK(drvdata);
		val = tpdm_readl(drvdata, TPDM_BC_CR);
		val = val | BIT(3);
		tpdm_writel(drvdata, val, TPDM_BC_CR);
		TPDM_LOCK(drvdata);

		drvdata->bc->capture_mode = TPDM_MODE_APB;
	} else {
		mutex_unlock(&drvdata->lock);
		return -EINVAL;
	}

	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_capture_mode);

static ssize_t bc_retrieval_mode_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 drvdata->bc->retrieval_mode == TPDM_MODE_ATB ?
			 "ATB" : "APB");
}

static ssize_t bc_retrieval_mode_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	char str[20] = "";

	if (size >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (!strcmp(str, "ATB")) {
		drvdata->bc->retrieval_mode = TPDM_MODE_ATB;
	} else if (!strcmp(str, "APB")) {
		drvdata->bc->retrieval_mode = TPDM_MODE_APB;
	} else {
		mutex_unlock(&drvdata->lock);
		return -EINVAL;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_retrieval_mode);

static ssize_t bc_reset_counters_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		val = tpdm_readl(drvdata, TPDM_BC_CR);
		val = val | BIT(1);
		tpdm_writel(drvdata, val, TPDM_BC_CR);
		TPDM_LOCK(drvdata);
	}

	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_WO(bc_reset_counters);

static ssize_t bc_sat_mode_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->sat_mode);
}

static ssize_t bc_sat_mode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->sat_mode = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_sat_mode);

static ssize_t bc_enable_counters_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->enable_counters);
}

static ssize_t bc_enable_counters_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->enable_counters = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_enable_counters);

static ssize_t bc_clear_counters_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->clear_counters);
}

static ssize_t bc_clear_counters_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->clear_counters = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_clear_counters);

static ssize_t bc_enable_irq_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->enable_irq);
}

static ssize_t bc_enable_irq_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->enable_irq = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_enable_irq);

static ssize_t bc_clear_irq_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->clear_irq);
}

static ssize_t bc_clear_irq_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->clear_irq = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_clear_irq);

static ssize_t bc_trig_val_lo_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_BC_MAX_COUNTERS; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->bc->trig_val_lo[i]);
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t bc_trig_val_lo_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets) ||
	    index >= drvdata->bc_counters_avail ||
	    drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_NO ||
	    (drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL && index > 0))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->trig_val_lo[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_trig_val_lo);

static ssize_t bc_trig_val_hi_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_BC_MAX_COUNTERS; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->bc->trig_val_hi[i]);
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t bc_trig_val_hi_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets) ||
	    index >= drvdata->bc_counters_avail ||
	    drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_NO ||
	    (drvdata->bc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL && index > 0))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->trig_val_hi[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_trig_val_hi);

static ssize_t bc_enable_ganging_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->bc->enable_ganging);
}

static ssize_t bc_enable_ganging_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->enable_ganging = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_enable_ganging);

static ssize_t bc_overflow_val_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_BC_MAX_OVERFLOW; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->bc->overflow_val[i]);
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t bc_overflow_val_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->datasets) ||
	    index >= TPDM_BC_MAX_OVERFLOW)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->bc->overflow_val[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_overflow_val);

static ssize_t bc_ovsr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_BC_OVSR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t bc_ovsr_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		tpdm_writel(drvdata, val, TPDM_BC_OVSR);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_ovsr);

static ssize_t bc_counter_sel_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_BC_SELR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t bc_counter_sel_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable || val >= drvdata->bc_counters_avail) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_BC_SELR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_counter_sel);

static ssize_t bc_count_val_lo_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_BC_CNTR_LO);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t bc_count_val_lo_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, select;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		select = tpdm_readl(drvdata, TPDM_BC_SELR);

		/* Check if selected counter is disabled */
		if (BVAL(tpdm_readl(drvdata, TPDM_BC_CNTENSET), select)) {
			mutex_unlock(&drvdata->lock);
			return -EPERM;
		}

		tpdm_writel(drvdata, val, TPDM_BC_CNTR_LO);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_count_val_lo);

static ssize_t bc_count_val_hi_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_BC_CNTR_HI);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t bc_count_val_hi_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, select;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		select = tpdm_readl(drvdata, TPDM_BC_SELR);

		/* Check if selected counter is disabled */
		if (BVAL(tpdm_readl(drvdata, TPDM_BC_CNTENSET), select)) {
			mutex_unlock(&drvdata->lock);
			return -EPERM;
		}

		tpdm_writel(drvdata, val, TPDM_BC_CNTR_HI);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_count_val_hi);

static ssize_t bc_shadow_val_lo_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	for (i = 0; i < drvdata->bc_counters_avail; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  tpdm_readl(drvdata, TPDM_BC_SHADOW_LO(i)));
	}
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RO(bc_shadow_val_lo);

static ssize_t bc_shadow_val_hi_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	for (i = 0; i < drvdata->bc_counters_avail; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  tpdm_readl(drvdata, TPDM_BC_SHADOW_HI(i)));
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RO(bc_shadow_val_hi);

static ssize_t bc_sw_inc_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_BC_SWINC);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t bc_sw_inc_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_BC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		tpdm_writel(drvdata, val, TPDM_BC_SWINC);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_sw_inc);

static ssize_t bc_msr_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int i;
	ssize_t len = 0;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	for (i = 0; i < TPDM_BC_MAX_MSR; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u 0x%x\n",
				 i, drvdata->bc->msr[i]);

	return len;
}

static ssize_t bc_msr_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int num, val;
	int nval;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_BC, drvdata->datasets))
		return -EPERM;

	nval = sscanf(buf, "%u %x", &num, &val);
	if (nval != 2)
		return -EINVAL;

	if (num >= TPDM_BC_MAX_MSR)
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	drvdata->bc->msr[num] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(bc_msr);

static ssize_t tc_capture_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 drvdata->tc->capture_mode == TPDM_MODE_ATB ?
			 "ATB" : "APB");
}

static ssize_t tc_capture_mode_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	char str[20] = "";
	uint32_t val;

	if (size >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (!strcmp(str, "ATB")) {
		drvdata->tc->capture_mode = TPDM_MODE_ATB;
	} else if (!strcmp(str, "APB") &&
		   drvdata->tc->retrieval_mode == TPDM_MODE_APB) {

		TPDM_UNLOCK(drvdata);
		val = tpdm_readl(drvdata, TPDM_TC_CR);
		val = val | BIT(3);
		tpdm_writel(drvdata, val, TPDM_TC_CR);
		TPDM_LOCK(drvdata);

		drvdata->tc->capture_mode = TPDM_MODE_APB;
	} else {
		mutex_unlock(&drvdata->lock);
		return -EINVAL;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_capture_mode);

static ssize_t tc_retrieval_mode_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
			 drvdata->tc->retrieval_mode == TPDM_MODE_ATB ?
			 "ATB" : "APB");
}

static ssize_t tc_retrieval_mode_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	char str[20] = "";

	if (size >= 20)
		return -EINVAL;
	if (sscanf(buf, "%s", str) != 1)
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (!strcmp(str, "ATB")) {
		drvdata->tc->retrieval_mode = TPDM_MODE_ATB;
	} else if (!strcmp(str, "APB")) {
		drvdata->tc->retrieval_mode = TPDM_MODE_APB;
	} else {
		mutex_unlock(&drvdata->lock);
		return -EINVAL;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_retrieval_mode);

static ssize_t tc_reset_counters_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		val = tpdm_readl(drvdata, TPDM_TC_CR);
		val = val | BIT(1);
		tpdm_writel(drvdata, val, TPDM_TC_CR);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_WO(tc_reset_counters);

static ssize_t tc_sat_mode_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->tc->sat_mode);
}

static ssize_t tc_sat_mode_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->tc->sat_mode = true;
	else
		drvdata->tc->sat_mode = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_sat_mode);

static ssize_t tc_enable_counters_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->tc->enable_counters);
}

static ssize_t tc_enable_counters_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;
	if (val >> drvdata->tc_counters_avail)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->enable_counters = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_enable_counters);

static ssize_t tc_clear_counters_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->tc->clear_counters);
}

static ssize_t tc_clear_counters_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;
	if (val >> drvdata->tc_counters_avail)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->clear_counters = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_clear_counters);

static ssize_t tc_enable_irq_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->tc->enable_irq);
}

static ssize_t tc_enable_irq_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->enable_irq = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_enable_irq);

static ssize_t tc_clear_irq_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->tc->clear_irq);
}

static ssize_t tc_clear_irq_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->clear_irq = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_clear_irq);

static ssize_t tc_trig_sel_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_TC_MAX_TRIG; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->tc->trig_sel[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t tc_trig_sel_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets) ||
	    index >= TPDM_TC_MAX_TRIG ||
	    drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_NO ||
	    (drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL && index > 0))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->trig_sel[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_trig_sel);

static ssize_t tc_trig_val_lo_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_TC_MAX_TRIG; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->tc->trig_val_lo[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t tc_trig_val_lo_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets) ||
	    index >= TPDM_TC_MAX_TRIG ||
	    drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_NO ||
	    (drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL && index > 0))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->trig_val_lo[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_trig_val_lo);

static ssize_t tc_trig_val_hi_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_TC_MAX_TRIG; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->tc->trig_val_hi[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t tc_trig_val_hi_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->datasets) ||
	    index >= TPDM_TC_MAX_TRIG ||
	    drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_NO ||
	    (drvdata->tc_trig_type == TPDM_SUPPORT_TYPE_PARTIAL && index > 0))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->tc->trig_val_hi[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_trig_val_hi);

static ssize_t tc_ovsr_gp_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_OVSR_GP);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_ovsr_gp_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		tpdm_writel(drvdata, val, TPDM_TC_OVSR_GP);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_ovsr_gp);

static ssize_t tc_ovsr_impl_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_OVSR_IMPL);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_ovsr_impl_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		tpdm_writel(drvdata, val, TPDM_TC_OVSR_IMPL);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_ovsr_impl);

static ssize_t tc_counter_sel_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_SELR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_counter_sel_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_TC_SELR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_counter_sel);

static ssize_t tc_count_val_lo_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_CNTR_LO);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_count_val_lo_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, select;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		select = tpdm_readl(drvdata, TPDM_TC_SELR);
		select = (select >> 11) & 0x3;

		/* Check if selected counter is disabled */
		if (BVAL(tpdm_readl(drvdata, TPDM_TC_CNTENSET), select)) {
			mutex_unlock(&drvdata->lock);
			return -EPERM;
		}

		tpdm_writel(drvdata, val, TPDM_TC_CNTR_LO);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_count_val_lo);

static ssize_t tc_count_val_hi_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_CNTR_HI);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_count_val_hi_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, select;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		select = tpdm_readl(drvdata, TPDM_TC_SELR);
		select = (select >> 11) & 0x3;

		/* Check if selected counter is disabled */
		if (BVAL(tpdm_readl(drvdata, TPDM_TC_CNTENSET), select)) {
			mutex_unlock(&drvdata->lock);
			return -EPERM;
		}

		tpdm_writel(drvdata, val, TPDM_TC_CNTR_HI);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_count_val_hi);

static ssize_t tc_shadow_val_lo_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	for (i = 0; i < TPDM_TC_MAX_COUNTERS; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  tpdm_readl(drvdata, TPDM_TC_SHADOW_LO(i)));
	}
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RO(tc_shadow_val_lo);

static ssize_t tc_shadow_val_hi_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	for (i = 0; i < TPDM_TC_MAX_COUNTERS; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  tpdm_readl(drvdata, TPDM_TC_SHADOW_HI(i)));
	}
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RO(tc_shadow_val_hi);

static ssize_t tc_sw_inc_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_TC_SWINC);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t tc_sw_inc_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_TC, drvdata->enable_ds))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDM_UNLOCK(drvdata);
		tpdm_writel(drvdata, val, TPDM_TC_SWINC);
		TPDM_LOCK(drvdata);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_sw_inc);

static ssize_t tc_msr_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int i;
	ssize_t len = 0;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	for (i = 0; i < TPDM_TC_MAX_MSR; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u 0x%x\n",
				 i, drvdata->tc->msr[i]);

	return len;
}

static ssize_t tc_msr_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int num, val;
	int nval;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_TC, drvdata->datasets))
		return -EPERM;

	nval = sscanf(buf, "%u %x", &num, &val);
	if (nval != 2)
		return -EINVAL;

	if (num >= TPDM_TC_MAX_MSR)
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	drvdata->tc->msr[num] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(tc_msr);

static ssize_t dsb_mode_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%lx\n",
			 (unsigned long)drvdata->dsb->mode);
}

static ssize_t dsb_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->mode = val & TPDM_MODE_ALL;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_mode);

static ssize_t dsb_testmode_data_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	if (index > 3)
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	mutex_unlock(&drvdata->lock);

	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_DSB_TESTMODE_DATA(index));
	TPDM_LOCK(drvdata);

	return size;
}
static DEVICE_ATTR_WO(dsb_testmode_data);

static ssize_t dsb_testmode_data_valid_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	mutex_unlock(&drvdata->lock);

	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_DSB_TESTMODE_DATA_VALID);
	TPDM_LOCK(drvdata);
	return size;
}
static DEVICE_ATTR_WO(dsb_testmode_data_valid);

static ssize_t dsb_edge_ctrl_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_EDCR; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index:0x%x Val:0x%x\n", i,
				  drvdata->dsb->edge_ctrl[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_edge_ctrl_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long start, end, edge_ctrl;
	uint32_t val;
	int i, bit, reg;

	if (sscanf(buf, "%lx %lx %lx", &start, &end, &edge_ctrl) != 3)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    (start >= TPDM_DSB_MAX_LINES) || (end >= TPDM_DSB_MAX_LINES) ||
	    edge_ctrl > 0x2)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = start; i <= end; i++) {
		reg = i / (NUM_OF_BITS / 2);
		bit = i % (NUM_OF_BITS / 2);
		bit = bit * 2;

		val = drvdata->dsb->edge_ctrl[reg];
		val = val & ~BM(bit, (bit + 1));
		val = val | (edge_ctrl << bit);
		drvdata->dsb->edge_ctrl[reg] = val;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_edge_ctrl);

static ssize_t dsb_edge_ctrl_mask_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_EDCR / 2; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index:0x%x Val:0x%x\n", i,
				  drvdata->dsb->edge_ctrl_mask[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_edge_ctrl_mask_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long start, end, val;
	uint32_t set;
	int i, bit, reg;

	if (sscanf(buf, "%lx %lx %lx", &start, &end, &val) != 3)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    (start >= TPDM_DSB_MAX_LINES) || (end >= TPDM_DSB_MAX_LINES))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = start; i <= end; i++) {
		reg = i / NUM_OF_BITS;
		bit = (i % NUM_OF_BITS);

		set = drvdata->dsb->edge_ctrl_mask[reg];
		if (val)
			set = set | BIT(bit);
		else
			set = set & ~BIT(bit);
		drvdata->dsb->edge_ctrl_mask[reg] = set;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_edge_ctrl_mask);

static ssize_t dsb_patt_val_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->dsb->patt_val[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_patt_val_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    index >= TPDM_DSB_MAX_PATT)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->patt_val[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_patt_val);

static ssize_t dsb_patt_mask_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->dsb->patt_mask[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_patt_mask_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    index >= TPDM_DSB_MAX_PATT)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->patt_mask[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_patt_mask);

static ssize_t dsb_patt_ts_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->dsb->patt_ts);
}

static ssize_t dsb_patt_ts_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->dsb->patt_ts = true;
	else
		drvdata->dsb->patt_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_patt_ts);

static ssize_t dsb_patt_type_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->dsb->patt_type);
}

static ssize_t dsb_patt_type_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->dsb->patt_type = true;
	else
		drvdata->dsb->patt_type = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_patt_type);

static ssize_t dsb_trig_patt_val_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->dsb->trig_patt_val[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_trig_patt_val_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    index >= TPDM_DSB_MAX_PATT)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->trig_patt_val[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_patt_val);

static ssize_t dsb_trig_patt_mask_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i = 0;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_PATT; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->dsb->trig_patt_mask[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_trig_patt_mask_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    index >= TPDM_DSB_MAX_PATT)
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->trig_patt_mask[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_patt_mask);

static ssize_t dsb_trig_type_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->dsb->trig_type);
}

static ssize_t dsb_trig_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->dsb->trig_type = true;
	else
		drvdata->dsb->trig_type = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_type);

static ssize_t dsb_trig_ts_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->dsb->trig_ts);
}

static ssize_t dsb_trig_ts_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->dsb->trig_ts = true;
	else
		drvdata->dsb->trig_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_trig_ts);

static ssize_t dsb_select_val_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_DSB_MAX_SELECT; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index:0x%x Val:0x%x\n", i,
				  drvdata->dsb->select_val[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t dsb_select_val_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long start, end;
	uint32_t val;
	int i, bit, reg;

	if (sscanf(buf, "%lx %lx", &start, &end) != 2)
		return -EINVAL;
	if (!test_bit(TPDM_DS_DSB, drvdata->datasets) ||
	    (start >= TPDM_DSB_MAX_LINES) || (end >= TPDM_DSB_MAX_LINES))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = start; i <= end; i++) {
		reg = i / NUM_OF_BITS;
		bit = (i % NUM_OF_BITS);

		val = drvdata->dsb->select_val[reg];
		val = val | BIT(bit);
		drvdata->dsb->select_val[reg] = val;
	}
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_select_val);

static ssize_t dsb_msr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int i;
	ssize_t len = 0;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	for (i = 0; i < TPDM_DSB_MAX_MSR; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u 0x%x\n",
				 i, drvdata->dsb->msr[i]);

	return len;
}

static ssize_t dsb_msr_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int num, val;
	int nval;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!test_bit(TPDM_DS_DSB, drvdata->datasets))
		return -EPERM;

	nval = sscanf(buf, "%u %x", &num, &val);
	if (nval != 2)
		return -EINVAL;

	if (num >= TPDM_DSB_MAX_MSR)
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	drvdata->dsb->msr[num] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(dsb_msr);

static ssize_t cmb_available_modes_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", "continuous trace_on_change");
}
static DEVICE_ATTR_RO(cmb_available_modes);

static ssize_t cmb_mode_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "trace_mode: %s cycle_acc: %d\n",
			 drvdata->cmb->trace_mode ?
			 "trace_on_change" : "continuous",
			 drvdata->cmb->cycle_acc);
}

static ssize_t cmb_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int trace_mode, cycle_acc;
	int nval;

	nval = sscanf(buf, "%u %u", &trace_mode, &cycle_acc);
	if (nval != 2)
		return -EINVAL;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->trace_mode = trace_mode;
	drvdata->cmb->cycle_acc = cycle_acc;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_mode);

static ssize_t cmb_patt_val_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_CMB_PATT_CMP; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->cmb->patt_val[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;
}

static ssize_t cmb_patt_val_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (index >= TPDM_CMB_PATT_CMP)
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->patt_val[index] = val;
	mutex_unlock(&drvdata->lock);

	return size;
}
static DEVICE_ATTR_RW(cmb_patt_val);

static ssize_t cmb_patt_mask_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	ssize_t size = 0;
	int i;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	for (i = 0; i < TPDM_CMB_PATT_CMP; i++) {
		size += scnprintf(buf + size, PAGE_SIZE - size,
				  "Index: 0x%x Value: 0x%x\n", i,
				  drvdata->cmb->patt_mask[i]);
	}
	mutex_unlock(&drvdata->lock);
	return size;

}

static ssize_t cmb_patt_mask_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long index, val;

	if (sscanf(buf, "%lx %lx", &index, &val) != 2)
		return -EINVAL;
	if (index >= TPDM_CMB_PATT_CMP)
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->patt_mask[index] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_patt_mask);

static ssize_t cmb_patt_ts_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmb->patt_ts);
}

static ssize_t cmb_patt_ts_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->cmb->patt_ts = true;
	else
		drvdata->cmb->patt_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_patt_ts);

static ssize_t cmb_ts_all_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmb->ts_all);
}

static ssize_t cmb_ts_all_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->cmb->ts_all = true;
	else
		drvdata->cmb->ts_all = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_ts_all);

static ssize_t cmb_trig_patt_val_lsb_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	val = drvdata->cmb->trig_patt_val[TPDM_CMB_LSB];

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cmb_trig_patt_val_lsb_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->trig_patt_val[TPDM_CMB_LSB] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_trig_patt_val_lsb);

static ssize_t cmb_trig_patt_mask_lsb_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	val = drvdata->cmb->trig_patt_mask[TPDM_CMB_LSB];

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cmb_trig_patt_mask_lsb_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->trig_patt_mask[TPDM_CMB_LSB] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_trig_patt_mask_lsb);

static ssize_t cmb_trig_patt_val_msb_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	val = drvdata->cmb->trig_patt_val[TPDM_CMB_MSB];

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cmb_trig_patt_val_msb_store(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->trig_patt_val[TPDM_CMB_MSB] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_trig_patt_val_msb);

static ssize_t cmb_trig_patt_mask_msb_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	val = drvdata->cmb->trig_patt_mask[TPDM_CMB_MSB];

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t cmb_trig_patt_mask_msb_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->trig_patt_mask[TPDM_CMB_MSB] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_trig_patt_mask_msb);

static ssize_t cmb_trig_ts_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmb->trig_ts);
}

static ssize_t cmb_trig_ts_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->cmb->trig_ts = true;
	else
		drvdata->cmb->trig_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_trig_ts);

static ssize_t cmb_msr_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int i;
	ssize_t len = 0;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	for (i = 0; i < TPDM_CMB_MAX_MSR; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%u 0x%x\n",
				 i, drvdata->cmb->msr[i]);

	return len;
}

static ssize_t cmb_msr_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned int num, val;
	int nval;

	if (!drvdata->msr_support)
		return -EINVAL;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	nval = sscanf(buf, "%u %x", &num, &val);
	if (nval != 2)
		return -EINVAL;

	if (num >= TPDM_CMB_MAX_MSR)
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->msr[num] = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmb_msr);

static ssize_t cmb_read_interface_state_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_CMB_READVAL);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);

	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}
static DEVICE_ATTR_RO(cmb_read_interface_state);

static ssize_t cmb_read_ctl_reg_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	TPDM_UNLOCK(drvdata);
	val = tpdm_readl(drvdata, TPDM_CMB_READCTL);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);

	if (test_bit(TPDM_DS_CMB, drvdata->datasets))
		return scnprintf(buf, PAGE_SIZE, "SEL: %lx\n", val);
	else
		return scnprintf(buf, PAGE_SIZE, "Lane %u SEL: %lx\n",
				 (unsigned int)BMVAL(val, 1, 3), val & 0x1);
}

static ssize_t cmb_read_ctl_reg_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_CMB_READCTL);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);

	return size;
}
static DEVICE_ATTR_RW(cmb_read_ctl_reg);

static ssize_t mcmb_trig_lane_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_MCMB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmb->mcmb->mcmb_trig_lane);
}

static ssize_t mcmb_trig_lane_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;
	if (val >= TPDM_MCMB_MAX_LANES)
		return -EINVAL;
	if (!test_bit(TPDM_DS_MCMB, drvdata->datasets))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	drvdata->cmb->mcmb->mcmb_trig_lane = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(mcmb_trig_lane);

static ssize_t mcmb_lanes_select_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	if (!test_bit(TPDM_DS_MCMB, drvdata->datasets))
		return -EPERM;

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmb->mcmb->mcmb_lane_select);
}

static ssize_t mcmb_lanes_select_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;
	if (!test_bit(TPDM_DS_MCMB, drvdata->datasets))
		return -EPERM;

	val = BMVAL(val, 0, TPDM_MCMB_MAX_LANES - 1);

	mutex_lock(&drvdata->lock);
	drvdata->cmb->mcmb->mcmb_lane_select = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(mcmb_lanes_select);

static ssize_t cmb_markr_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t size)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	if (!(test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	      test_bit(TPDM_DS_MCMB, drvdata->datasets)))
		return -EPERM;

	mutex_lock(&drvdata->lock);
	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}
	TPDM_UNLOCK(drvdata);
	tpdm_writel(drvdata, val, TPDM_CMB_MARKR);
	TPDM_LOCK(drvdata);
	mutex_unlock(&drvdata->lock);

	return size;
}
static DEVICE_ATTR_WO(cmb_markr);

static struct attribute *tpdm_bc_attrs[] = {
	&dev_attr_bc_capture_mode.attr,
	&dev_attr_bc_retrieval_mode.attr,
	&dev_attr_bc_reset_counters.attr,
	&dev_attr_bc_sat_mode.attr,
	&dev_attr_bc_enable_counters.attr,
	&dev_attr_bc_clear_counters.attr,
	&dev_attr_bc_enable_irq.attr,
	&dev_attr_bc_clear_irq.attr,
	&dev_attr_bc_trig_val_lo.attr,
	&dev_attr_bc_trig_val_hi.attr,
	&dev_attr_bc_enable_ganging.attr,
	&dev_attr_bc_overflow_val.attr,
	&dev_attr_bc_ovsr.attr,
	&dev_attr_bc_counter_sel.attr,
	&dev_attr_bc_count_val_lo.attr,
	&dev_attr_bc_count_val_hi.attr,
	&dev_attr_bc_shadow_val_lo.attr,
	&dev_attr_bc_shadow_val_hi.attr,
	&dev_attr_bc_sw_inc.attr,
	&dev_attr_bc_msr.attr,
	NULL,
};

static struct attribute *tpdm_tc_attrs[] = {
	&dev_attr_tc_capture_mode.attr,
	&dev_attr_tc_retrieval_mode.attr,
	&dev_attr_tc_reset_counters.attr,
	&dev_attr_tc_sat_mode.attr,
	&dev_attr_tc_enable_counters.attr,
	&dev_attr_tc_clear_counters.attr,
	&dev_attr_tc_enable_irq.attr,
	&dev_attr_tc_clear_irq.attr,
	&dev_attr_tc_trig_sel.attr,
	&dev_attr_tc_trig_val_lo.attr,
	&dev_attr_tc_trig_val_hi.attr,
	&dev_attr_tc_ovsr_gp.attr,
	&dev_attr_tc_ovsr_impl.attr,
	&dev_attr_tc_counter_sel.attr,
	&dev_attr_tc_count_val_lo.attr,
	&dev_attr_tc_count_val_hi.attr,
	&dev_attr_tc_shadow_val_lo.attr,
	&dev_attr_tc_shadow_val_hi.attr,
	&dev_attr_tc_sw_inc.attr,
	&dev_attr_tc_msr.attr,
	NULL,
};

static struct attribute *tpdm_dsb_attrs[] = {
	&dev_attr_dsb_mode.attr,
	&dev_attr_dsb_testmode_data.attr,
	&dev_attr_dsb_testmode_data_valid.attr,
	&dev_attr_dsb_edge_ctrl.attr,
	&dev_attr_dsb_edge_ctrl_mask.attr,
	&dev_attr_dsb_patt_val.attr,
	&dev_attr_dsb_patt_mask.attr,
	&dev_attr_dsb_patt_ts.attr,
	&dev_attr_dsb_patt_type.attr,
	&dev_attr_dsb_trig_patt_val.attr,
	&dev_attr_dsb_trig_patt_mask.attr,
	&dev_attr_dsb_trig_ts.attr,
	&dev_attr_dsb_trig_type.attr,
	&dev_attr_dsb_select_val.attr,
	&dev_attr_dsb_msr.attr,
	NULL,
};

static struct attribute *tpdm_cmb_attrs[] = {
	&dev_attr_cmb_available_modes.attr,
	&dev_attr_cmb_mode.attr,
	&dev_attr_cmb_patt_val.attr,
	&dev_attr_cmb_patt_mask.attr,
	&dev_attr_cmb_patt_ts.attr,
	&dev_attr_cmb_ts_all.attr,
	&dev_attr_cmb_trig_patt_val_lsb.attr,
	&dev_attr_cmb_trig_patt_mask_lsb.attr,
	&dev_attr_cmb_trig_patt_val_msb.attr,
	&dev_attr_cmb_trig_patt_mask_msb.attr,
	&dev_attr_cmb_trig_ts.attr,
	&dev_attr_cmb_msr.attr,
	&dev_attr_cmb_read_interface_state.attr,
	&dev_attr_cmb_read_ctl_reg.attr,
	&dev_attr_cmb_markr.attr,
	&dev_attr_mcmb_trig_lane.attr,
	&dev_attr_mcmb_lanes_select.attr,
	NULL,
};

static struct attribute_group tpdm_bc_attr_grp = {
	.attrs = tpdm_bc_attrs,
};

static struct attribute_group tpdm_tc_attr_grp = {
	.attrs = tpdm_tc_attrs,
};

static struct attribute_group tpdm_dsb_attr_grp = {
	.attrs = tpdm_dsb_attrs,
};

static struct attribute_group tpdm_cmb_attr_grp = {
	.attrs = tpdm_cmb_attrs,
};

static struct attribute *tpdm_attrs[] = {
	&dev_attr_available_datasets.attr,
	&dev_attr_enable_datasets.attr,
	&dev_attr_reset.attr,
	&dev_attr_integration_test.attr,
	&dev_attr_gp_regs.attr,
	NULL,
};

static struct attribute_group tpdm_attr_grp = {
	.attrs = tpdm_attrs,
};
static const struct attribute_group *tpdm_attr_grps[] = {
	&tpdm_attr_grp,
	&tpdm_bc_attr_grp,
	&tpdm_tc_attr_grp,
	&tpdm_dsb_attr_grp,
	&tpdm_cmb_attr_grp,
	NULL,
};

static int tpdm_datasets_alloc(struct tpdm_drvdata *drvdata)
{
	if (test_bit(TPDM_DS_GPR, drvdata->datasets)) {
		drvdata->gpr = devm_kzalloc(drvdata->dev, sizeof(*drvdata->gpr),
					    GFP_KERNEL);
		if (!drvdata->gpr)
			return -ENOMEM;
	}
	if (test_bit(TPDM_DS_BC, drvdata->datasets)) {
		drvdata->bc = devm_kzalloc(drvdata->dev, sizeof(*drvdata->bc),
					   GFP_KERNEL);
		if (!drvdata->bc)
			return -ENOMEM;
	}
	if (test_bit(TPDM_DS_TC, drvdata->datasets)) {
		drvdata->tc = devm_kzalloc(drvdata->dev, sizeof(*drvdata->tc),
					   GFP_KERNEL);
		if (!drvdata->tc)
			return -ENOMEM;
	}
	if (test_bit(TPDM_DS_DSB, drvdata->datasets)) {
		drvdata->dsb = devm_kzalloc(drvdata->dev, sizeof(*drvdata->dsb),
					    GFP_KERNEL);
		if (!drvdata->dsb)
			return -ENOMEM;
	}
	if (test_bit(TPDM_DS_CMB, drvdata->datasets)) {
		drvdata->cmb = devm_kzalloc(drvdata->dev, sizeof(*drvdata->cmb),
					    GFP_KERNEL);
		if (!drvdata->cmb)
			return -ENOMEM;
	} else if (test_bit(TPDM_DS_MCMB, drvdata->datasets)) {
		drvdata->cmb = devm_kzalloc(drvdata->dev, sizeof(*drvdata->cmb),
					    GFP_KERNEL);
		if (!drvdata->cmb)
			return -ENOMEM;
		drvdata->cmb->mcmb = devm_kzalloc(drvdata->dev,
						  sizeof(*drvdata->cmb->mcmb),
						  GFP_KERNEL);
		if (!drvdata->cmb->mcmb)
			return -ENOMEM;
	}
	return 0;
}

static void tpdm_init_default_data(struct tpdm_drvdata *drvdata)
{
	if (test_bit(TPDM_DS_BC, drvdata->datasets))
		drvdata->bc->retrieval_mode = TPDM_MODE_ATB;

	if (test_bit(TPDM_DS_TC, drvdata->datasets))
		drvdata->tc->retrieval_mode = TPDM_MODE_ATB;

	if (test_bit(TPDM_DS_DSB, drvdata->datasets)) {
		drvdata->dsb->trig_ts = true;
		drvdata->dsb->trig_type = false;
	}

	if (test_bit(TPDM_DS_CMB, drvdata->datasets) ||
	    test_bit(TPDM_DS_MCMB, drvdata->datasets))
		drvdata->cmb->trig_ts = true;
}

static int tpdm_parse_of_data(struct tpdm_drvdata *drvdata)
{
	int i, ret;
	const char *tclk_name, *treg_name;
	struct device_node *node = drvdata->dev->of_node;

	drvdata->clk_enable = of_property_read_bool(node, "qcom,clk-enable");
	drvdata->msr_fix_req = of_property_read_bool(node, "qcom,msr-fix-req");
	drvdata->cmb_msr_skip = of_property_read_bool(node,
					"qcom,cmb-msr-skip");

	drvdata->nr_tclk = of_property_count_strings(node, "qcom,tpdm-clks");
	if (drvdata->nr_tclk > 0) {
		drvdata->tclk = devm_kzalloc(drvdata->dev, drvdata->nr_tclk *
					     sizeof(*drvdata->tclk),
					     GFP_KERNEL);
		if (!drvdata->tclk)
			return -ENOMEM;

		for (i = 0; i < drvdata->nr_tclk; i++) {
			ret = of_property_read_string_index(node,
					    "qcom,tpdm-clks", i, &tclk_name);
			if (ret)
				return ret;

			drvdata->tclk[i] = devm_clk_get(drvdata->dev,
							tclk_name);
			if (IS_ERR(drvdata->tclk[i]))
				return PTR_ERR(drvdata->tclk[i]);
		}
	}

	drvdata->nr_treg = of_property_count_strings(node, "qcom,tpdm-regs");
	if (drvdata->nr_treg > 0) {
		drvdata->treg = devm_kzalloc(drvdata->dev, drvdata->nr_treg *
					     sizeof(*drvdata->treg),
					     GFP_KERNEL);
		if (!drvdata->treg)
			return -ENOMEM;

		for (i = 0; i < drvdata->nr_treg; i++) {
			ret = of_property_read_string_index(node,
					    "qcom,tpdm-regs", i, &treg_name);
			if (ret)
				return ret;

			drvdata->treg[i] = devm_regulator_get(drvdata->dev,
							treg_name);
			if (IS_ERR(drvdata->treg[i]))
				return PTR_ERR(drvdata->treg[i]);
		}
	}

	return 0;
}

static int tpdm_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret, i;
	uint32_t pidr, devid;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct tpdm_drvdata *drvdata;
	struct coresight_desc desc = { 0 };
	static int traceid = TPDM_TRACE_ID_START;
	uint32_t version;
	u32 dump_state = 0;

	desc.name = coresight_alloc_device_name(&tpdm_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	if (of_property_read_bool(adev->dev.of_node, "qcom,hw-enable-check")) {
		ret = qcom_scm_get_sec_dump_state(&dump_state);
		if (ret || !dump_state)
			return -ENXIO;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (!drvdata->base)
		return -ENOMEM;

	mutex_init(&drvdata->lock);

	ret = tpdm_parse_of_data(drvdata);
	if (ret) {
		dev_err(drvdata->dev, "TPDM parse of data fail\n");
		return -EINVAL;
	}

	desc.type = CORESIGHT_DEV_TYPE_SOURCE;
	desc.subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_PROC;
	desc.ops = &tpdm_cs_ops;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.groups = tpdm_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	version = tpdm_readl(drvdata, CORESIGHT_PERIPHIDR2);
	drvdata->version = BMVAL(version, 4, 7);

	if (drvdata->version)
		drvdata->msr_support = true;

	pidr = tpdm_readl(drvdata, CORESIGHT_PERIPHIDR0);
	for (i = 0; i < TPDM_DATASETS; i++) {
		if (pidr & BIT(i)) {
			__set_bit(i, drvdata->datasets);
			__set_bit(i, drvdata->enable_ds);
		}
	}

	ret = tpdm_datasets_alloc(drvdata);
	if (ret) {
		coresight_unregister(drvdata->csdev);
		return ret;
	}

	tpdm_init_default_data(drvdata);

	devid = tpdm_readl(drvdata, CORESIGHT_DEVID);
	drvdata->tc_trig_type = BMVAL(devid, 27, 28);
	drvdata->bc_trig_type = BMVAL(devid, 25, 26);
	drvdata->bc_gang_type = BMVAL(devid, 23, 24);
	drvdata->bc_counters_avail = BMVAL(devid, 6, 10) + 1;
	drvdata->tc_counters_avail = BMVAL(devid, 4, 5) + 1;

	drvdata->traceid = traceid++;

	dev_dbg(drvdata->dev, "TPDM initialized\n");

	if (boot_enable)
		coresight_enable(drvdata->csdev);

	pm_runtime_put(&adev->dev);

	return 0;
}

static void __exit tpdm_remove(struct amba_device *adev)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
}

#ifdef CONFIG_DEEPSLEEP
static int tpdm_suspend(struct device *dev)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev);

	if (pm_suspend_via_firmware())
		coresight_disable(drvdata->csdev);

	return 0;
}
#endif

#ifdef CONFIG_HIBERNATION
static int tpdm_freeze(struct device *dev)
{
	struct tpdm_drvdata *drvdata = dev_get_drvdata(dev);

	coresight_disable(drvdata->csdev);

	return 0;
}
#endif

static const struct dev_pm_ops tpdm_dev_pm_ops = {
#ifdef CONFIG_DEEPSLEEP
	.suspend = tpdm_suspend,
#endif
#ifdef CONFIG_HIBERNATION
	.freeze  = tpdm_freeze,
#endif
};

static struct amba_id tpdm_ids[] = {
	{
		.id     = 0x0003b968,
		.mask   = 0x0003ffff,
		.data	= "TPDM",
	},
	{ 0, 0},
};
MODULE_DEVICE_TABLE(amba, tpdm_ids);

static struct amba_driver tpdm_driver = {
	.drv = {
		.name   = "coresight-tpdm",
		.owner	= THIS_MODULE,
		.suppress_bind_attrs = true,
		.pm	= &tpdm_dev_pm_ops,
	},
	.probe          = tpdm_probe,
	.remove		= tpdm_remove,
	.id_table	= tpdm_ids,
};

module_amba_driver(tpdm_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace, Profiling & Diagnostic Monitor driver");
