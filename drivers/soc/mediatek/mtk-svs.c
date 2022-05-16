// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/cpuidle.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/* svs bank 1-line software id */
#define SVSB_CPU_LITTLE			BIT(0)
#define SVSB_CPU_BIG			BIT(1)
#define SVSB_CCI			BIT(2)
#define SVSB_GPU			BIT(3)

/* svs bank mode support */
#define SVSB_MODE_ALL_DISABLE		0
#define SVSB_MODE_INIT01		BIT(1)
#define SVSB_MODE_INIT02		BIT(2)

/* svs bank volt flags */
#define SVSB_INIT01_PD_REQ		BIT(0)
#define SVSB_INIT01_VOLT_IGNORE		BIT(1)
#define SVSB_INIT01_VOLT_INC_ONLY	BIT(2)

/* svs bank register common configuration */
#define SVSB_DET_MAX			0xffff
#define SVSB_DET_WINDOW			0xa28
#define SVSB_DTHI			0x1
#define SVSB_DTLO			0xfe
#define SVSB_EN_INIT01			0x1
#define SVSB_EN_INIT02			0x5
#define SVSB_EN_OFF			0x0
#define SVSB_INTEN_INIT0x		0x00005f01
#define SVSB_INTSTS_CLEAN		0x00ffffff
#define SVSB_INTSTS_COMPLETE		0x1
#define SVSB_RUNCONFIG_DEFAULT		0x80000000

/* svs bank related setting */
#define MAX_OPP_ENTRIES			16
#define SVSB_DC_SIGNED_BIT		BIT(15)
#define SVSB_DET_CLK_EN			BIT(31)

static DEFINE_SPINLOCK(svs_lock);

/**
 * enum svsb_phase - svs bank phase enumeration
 * @SVSB_PHASE_ERROR: svs bank encounters unexpected condition
 * @SVSB_PHASE_INIT01: svs bank basic init for data calibration
 * @SVSB_PHASE_INIT02: svs bank can provide voltages to opp table
 * @SVSB_PHASE_MAX: total number of svs bank phase (debug purpose)
 *
 * Each svs bank has its own independent phase and we enable each svs bank by
 * running their phase orderly. However, when svs bank encounters unexpected
 * condition, it will fire an irq (PHASE_ERROR) to inform svs software.
 *
 * svs bank general phase-enabled order:
 * SVSB_PHASE_INIT01 -> SVSB_PHASE_INIT02
 */
enum svsb_phase {
	SVSB_PHASE_ERROR = 0,
	SVSB_PHASE_INIT01,
	SVSB_PHASE_INIT02,
	SVSB_PHASE_MAX,
};

enum svs_reg_index {
	DESCHAR = 0,
	TEMPCHAR,
	DETCHAR,
	AGECHAR,
	DCCONFIG,
	AGECONFIG,
	FREQPCT30,
	FREQPCT74,
	LIMITVALS,
	VBOOT,
	DETWINDOW,
	CONFIG,
	TSCALCS,
	RUNCONFIG,
	SVSEN,
	INIT2VALS,
	DCVALUES,
	AGEVALUES,
	VOP30,
	VOP74,
	TEMP,
	INTSTS,
	INTSTSRAW,
	INTEN,
	CHKINT,
	CHKSHIFT,
	STATUS,
	VDESIGN30,
	VDESIGN74,
	DVT30,
	DVT74,
	AGECOUNT,
	SMSTATE0,
	SMSTATE1,
	CTL0,
	DESDETSEC,
	TEMPAGESEC,
	CTRLSPARE0,
	CTRLSPARE1,
	CTRLSPARE2,
	CTRLSPARE3,
	CORESEL,
	THERMINTST,
	INTST,
	THSTAGE0ST,
	THSTAGE1ST,
	THSTAGE2ST,
	THAHBST0,
	THAHBST1,
	SPARE0,
	SPARE1,
	SPARE2,
	SPARE3,
	THSLPEVEB,
	SVS_REG_MAX,
};

static const u32 svs_regs_v2[] = {
	[DESCHAR]		= 0xc00,
	[TEMPCHAR]		= 0xc04,
	[DETCHAR]		= 0xc08,
	[AGECHAR]		= 0xc0c,
	[DCCONFIG]		= 0xc10,
	[AGECONFIG]		= 0xc14,
	[FREQPCT30]		= 0xc18,
	[FREQPCT74]		= 0xc1c,
	[LIMITVALS]		= 0xc20,
	[VBOOT]			= 0xc24,
	[DETWINDOW]		= 0xc28,
	[CONFIG]		= 0xc2c,
	[TSCALCS]		= 0xc30,
	[RUNCONFIG]		= 0xc34,
	[SVSEN]			= 0xc38,
	[INIT2VALS]		= 0xc3c,
	[DCVALUES]		= 0xc40,
	[AGEVALUES]		= 0xc44,
	[VOP30]			= 0xc48,
	[VOP74]			= 0xc4c,
	[TEMP]			= 0xc50,
	[INTSTS]		= 0xc54,
	[INTSTSRAW]		= 0xc58,
	[INTEN]			= 0xc5c,
	[CHKINT]		= 0xc60,
	[CHKSHIFT]		= 0xc64,
	[STATUS]		= 0xc68,
	[VDESIGN30]		= 0xc6c,
	[VDESIGN74]		= 0xc70,
	[DVT30]			= 0xc74,
	[DVT74]			= 0xc78,
	[AGECOUNT]		= 0xc7c,
	[SMSTATE0]		= 0xc80,
	[SMSTATE1]		= 0xc84,
	[CTL0]			= 0xc88,
	[DESDETSEC]		= 0xce0,
	[TEMPAGESEC]		= 0xce4,
	[CTRLSPARE0]		= 0xcf0,
	[CTRLSPARE1]		= 0xcf4,
	[CTRLSPARE2]		= 0xcf8,
	[CTRLSPARE3]		= 0xcfc,
	[CORESEL]		= 0xf00,
	[THERMINTST]		= 0xf04,
	[INTST]			= 0xf08,
	[THSTAGE0ST]		= 0xf0c,
	[THSTAGE1ST]		= 0xf10,
	[THSTAGE2ST]		= 0xf14,
	[THAHBST0]		= 0xf18,
	[THAHBST1]		= 0xf1c,
	[SPARE0]		= 0xf20,
	[SPARE1]		= 0xf24,
	[SPARE2]		= 0xf28,
	[SPARE3]		= 0xf2c,
	[THSLPEVEB]		= 0xf30,
};

/**
 * struct svs_platform - svs platform control
 * @name: svs platform name
 * @base: svs platform register base
 * @dev: svs platform device
 * @main_clk: main clock for svs bank
 * @pbank: svs bank pointer needing to be protected by spin_lock section
 * @banks: svs banks that svs platform supports
 * @efuse_parsing: svs platform efuse parsing function pointer
 * @probe: svs platform probe function pointer
 * @irqflags: svs platform irq settings flags
 * @efuse_max: total number of svs efuse
 * @regs: svs platform registers map
 * @bank_max: total number of svs banks
 * @efuse: svs efuse data received from NVMEM framework
 */
struct svs_platform {
	char *name;
	void __iomem *base;
	struct device *dev;
	struct clk *main_clk;
	struct svs_bank *pbank;
	struct svs_bank *banks;
	bool (*efuse_parsing)(struct svs_platform *svsp);
	int (*probe)(struct svs_platform *svsp);
	unsigned long irqflags;
	size_t efuse_max;
	const u32 *regs;
	u32 bank_max;
	u32 *efuse;
};

struct svs_platform_data {
	char *name;
	struct svs_bank *banks;
	bool (*efuse_parsing)(struct svs_platform *svsp);
	int (*probe)(struct svs_platform *svsp);
	unsigned long irqflags;
	const u32 *regs;
	u32 bank_max;
};

/**
 * struct svs_bank - svs bank representation
 * @dev: bank device
 * @opp_dev: device for opp table/buck control
 * @init_completion: the timeout completion for bank init
 * @buck: regulator used by opp_dev
 * @lock: mutex lock to protect voltage update process
 * @set_freq_pct: function pointer to set bank frequency percent table
 * @get_volts: function pointer to get bank voltages
 * @name: bank name
 * @buck_name: regulator name
 * @phase: bank current phase
 * @volt_od: bank voltage overdrive
 * @pm_runtime_enabled_count: bank pm runtime enabled count
 * @mode_support: bank mode support.
 * @freq_base: reference frequency for bank init
 * @vboot: voltage request for bank init01 only
 * @opp_dfreq: default opp frequency table
 * @opp_dvolt: default opp voltage table
 * @freq_pct: frequency percent table for bank init
 * @volt: bank voltage table
 * @volt_step: bank voltage step
 * @volt_base: bank voltage base
 * @volt_flags: bank voltage flags
 * @vmax: bank voltage maximum
 * @vmin: bank voltage minimum
 * @age_config: bank age configuration
 * @age_voffset_in: bank age voltage offset
 * @dc_config: bank dc configuration
 * @dc_voffset_in: bank dc voltage offset
 * @dvt_fixed: bank dvt fixed value
 * @vco: bank VCO value
 * @chk_shift: bank chicken shift
 * @core_sel: bank selection
 * @opp_count: bank opp count
 * @int_st: bank interrupt identification
 * @sw_id: bank software identification
 * @cpu_id: cpu core id for SVS CPU bank use only
 * @ctl0: TS-x selection
 * @bdes: svs efuse data
 * @mdes: svs efuse data
 * @mtdes: svs efuse data
 * @dcbdet: svs efuse data
 * @dcmdet: svs efuse data
 *
 * Svs bank will generate suitalbe voltages by below general math equation
 * and provide these voltages to opp voltage table.
 *
 * opp_volt[i] = (volt[i] * volt_step) + volt_base;
 */
struct svs_bank {
	struct device *dev;
	struct device *opp_dev;
	struct completion init_completion;
	struct regulator *buck;
	struct mutex lock;	/* lock to protect voltage update process */
	void (*set_freq_pct)(struct svs_platform *svsp);
	void (*get_volts)(struct svs_platform *svsp);
	char *name;
	char *buck_name;
	enum svsb_phase phase;
	s32 volt_od;
	u32 pm_runtime_enabled_count;
	u32 mode_support;
	u32 freq_base;
	u32 vboot;
	u32 opp_dfreq[MAX_OPP_ENTRIES];
	u32 opp_dvolt[MAX_OPP_ENTRIES];
	u32 freq_pct[MAX_OPP_ENTRIES];
	u32 volt[MAX_OPP_ENTRIES];
	u32 volt_step;
	u32 volt_base;
	u32 volt_flags;
	u32 vmax;
	u32 vmin;
	u32 age_config;
	u32 age_voffset_in;
	u32 dc_config;
	u32 dc_voffset_in;
	u32 dvt_fixed;
	u32 vco;
	u32 chk_shift;
	u32 core_sel;
	u32 opp_count;
	u32 int_st;
	u32 sw_id;
	u32 cpu_id;
	u32 ctl0;
	u32 bdes;
	u32 mdes;
	u32 mtdes;
	u32 dcbdet;
	u32 dcmdet;
};

static u32 percent(u32 numerator, u32 denominator)
{
	/* If not divide 1000, "numerator * 100" will have data overflow. */
	numerator /= 1000;
	denominator /= 1000;

	return DIV_ROUND_UP(numerator * 100, denominator);
}

static u32 svs_readl_relaxed(struct svs_platform *svsp, enum svs_reg_index rg_i)
{
	return readl_relaxed(svsp->base + svsp->regs[rg_i]);
}

static void svs_writel_relaxed(struct svs_platform *svsp, u32 val,
			       enum svs_reg_index rg_i)
{
	writel_relaxed(val, svsp->base + svsp->regs[rg_i]);
}

static void svs_switch_bank(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	svs_writel_relaxed(svsp, svsb->core_sel, CORESEL);
}

static u32 svs_bank_volt_to_opp_volt(u32 svsb_volt, u32 svsb_volt_step,
				     u32 svsb_volt_base)
{
	return (svsb_volt * svsb_volt_step) + svsb_volt_base;
}

static int svs_adjust_pm_opp_volts(struct svs_bank *svsb)
{
	int ret = -EPERM;
	u32 i, svsb_volt, opp_volt;

	mutex_lock(&svsb->lock);

	/* vmin <= svsb_volt (opp_volt) <= default opp voltage */
	for (i = 0; i < svsb->opp_count; i++) {
		switch (svsb->phase) {
		case SVSB_PHASE_ERROR:
			opp_volt = svsb->opp_dvolt[i];
			break;
		case SVSB_PHASE_INIT01:
			/* do nothing */
			goto unlock_mutex;
		case SVSB_PHASE_INIT02:
			svsb_volt = max(svsb->volt[i], svsb->vmin);
			opp_volt = svs_bank_volt_to_opp_volt(svsb_volt,
							     svsb->volt_step,
							     svsb->volt_base);
			break;
		default:
			dev_err(svsb->dev, "unknown phase: %u\n", svsb->phase);
			ret = -EINVAL;
			goto unlock_mutex;
		}

		opp_volt = min(opp_volt, svsb->opp_dvolt[i]);
		ret = dev_pm_opp_adjust_voltage(svsb->opp_dev,
						svsb->opp_dfreq[i],
						opp_volt, opp_volt,
						svsb->opp_dvolt[i]);
		if (ret) {
			dev_err(svsb->dev, "set %uuV fail: %d\n",
				opp_volt, ret);
			goto unlock_mutex;
		}
	}

unlock_mutex:
	mutex_unlock(&svsb->lock);

	return ret;
}

static u32 interpolate(u32 f0, u32 f1, u32 v0, u32 v1, u32 fx)
{
	u32 vx;

	if (v0 == v1 || f0 == f1)
		return v0;

	/* *100 to have decimal fraction factor */
	vx = (v0 * 100) - ((((v0 - v1) * 100) / (f0 - f1)) * (f0 - fx));

	return DIV_ROUND_UP(vx, 100);
}

static void svs_get_bank_volts_v2(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 temp, i;

	temp = svs_readl_relaxed(svsp, VOP74);
	svsb->volt[14] = (temp >> 24) & GENMASK(7, 0);
	svsb->volt[12] = (temp >> 16) & GENMASK(7, 0);
	svsb->volt[10] = (temp >> 8)  & GENMASK(7, 0);
	svsb->volt[8] = (temp & GENMASK(7, 0));

	temp = svs_readl_relaxed(svsp, VOP30);
	svsb->volt[6] = (temp >> 24) & GENMASK(7, 0);
	svsb->volt[4] = (temp >> 16) & GENMASK(7, 0);
	svsb->volt[2] = (temp >> 8)  & GENMASK(7, 0);
	svsb->volt[0] = (temp & GENMASK(7, 0));

	for (i = 0; i <= 12; i += 2)
		svsb->volt[i + 1] = interpolate(svsb->freq_pct[i],
						svsb->freq_pct[i + 2],
						svsb->volt[i],
						svsb->volt[i + 2],
						svsb->freq_pct[i + 1]);

	svsb->volt[15] = interpolate(svsb->freq_pct[12],
				     svsb->freq_pct[14],
				     svsb->volt[12],
				     svsb->volt[14],
				     svsb->freq_pct[15]);

	for (i = 0; i < svsb->opp_count; i++)
		svsb->volt[i] += svsb->volt_od;
}

static void svs_set_bank_freq_pct_v2(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	svs_writel_relaxed(svsp,
			   (svsb->freq_pct[14] << 24) |
			   (svsb->freq_pct[12] << 16) |
			   (svsb->freq_pct[10] << 8) |
			   svsb->freq_pct[8],
			   FREQPCT74);

	svs_writel_relaxed(svsp,
			   (svsb->freq_pct[6] << 24) |
			   (svsb->freq_pct[4] << 16) |
			   (svsb->freq_pct[2] << 8) |
			   svsb->freq_pct[0],
			   FREQPCT30);
}

static void svs_set_bank_phase(struct svs_platform *svsp,
			       enum svsb_phase target_phase)
{
	struct svs_bank *svsb = svsp->pbank;
	u32 des_char, temp_char, det_char, limit_vals, init2vals;

	svs_switch_bank(svsp);

	des_char = (svsb->bdes << 8) | svsb->mdes;
	svs_writel_relaxed(svsp, des_char, DESCHAR);

	temp_char = (svsb->vco << 16) | (svsb->mtdes << 8) | svsb->dvt_fixed;
	svs_writel_relaxed(svsp, temp_char, TEMPCHAR);

	det_char = (svsb->dcbdet << 8) | svsb->dcmdet;
	svs_writel_relaxed(svsp, det_char, DETCHAR);

	svs_writel_relaxed(svsp, svsb->dc_config, DCCONFIG);
	svs_writel_relaxed(svsp, svsb->age_config, AGECONFIG);
	svs_writel_relaxed(svsp, SVSB_RUNCONFIG_DEFAULT, RUNCONFIG);

	svsb->set_freq_pct(svsp);

	limit_vals = (svsb->vmax << 24) | (svsb->vmin << 16) |
		     (SVSB_DTHI << 8) | SVSB_DTLO;
	svs_writel_relaxed(svsp, limit_vals, LIMITVALS);

	svs_writel_relaxed(svsp, SVSB_DET_WINDOW, DETWINDOW);
	svs_writel_relaxed(svsp, SVSB_DET_MAX, CONFIG);
	svs_writel_relaxed(svsp, svsb->chk_shift, CHKSHIFT);
	svs_writel_relaxed(svsp, svsb->ctl0, CTL0);
	svs_writel_relaxed(svsp, SVSB_INTSTS_CLEAN, INTSTS);

	switch (target_phase) {
	case SVSB_PHASE_INIT01:
		svs_writel_relaxed(svsp, svsb->vboot, VBOOT);
		svs_writel_relaxed(svsp, SVSB_INTEN_INIT0x, INTEN);
		svs_writel_relaxed(svsp, SVSB_EN_INIT01, SVSEN);
		break;
	case SVSB_PHASE_INIT02:
		svs_writel_relaxed(svsp, SVSB_INTEN_INIT0x, INTEN);
		init2vals = (svsb->age_voffset_in << 16) | svsb->dc_voffset_in;
		svs_writel_relaxed(svsp, init2vals, INIT2VALS);
		svs_writel_relaxed(svsp, SVSB_EN_INIT02, SVSEN);
		break;
	default:
		dev_err(svsb->dev, "requested unknown target phase: %u\n",
			target_phase);
		break;
	}
}

static inline void svs_error_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_err(svsb->dev, "%s: CORESEL = 0x%08x\n",
		__func__, svs_readl_relaxed(svsp, CORESEL));
	dev_err(svsb->dev, "SVSEN = 0x%08x, INTSTS = 0x%08x\n",
		svs_readl_relaxed(svsp, SVSEN),
		svs_readl_relaxed(svsp, INTSTS));
	dev_err(svsb->dev, "SMSTATE0 = 0x%08x, SMSTATE1 = 0x%08x\n",
		svs_readl_relaxed(svsp, SMSTATE0),
		svs_readl_relaxed(svsp, SMSTATE1));

	svsb->phase = SVSB_PHASE_ERROR;
	svs_writel_relaxed(svsp, SVSB_EN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_CLEAN, INTSTS);
}

static inline void svs_init01_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_info(svsb->dev, "%s: VDN74~30:0x%08x~0x%08x, DC:0x%08x\n",
		 __func__, svs_readl_relaxed(svsp, VDESIGN74),
		 svs_readl_relaxed(svsp, VDESIGN30),
		 svs_readl_relaxed(svsp, DCVALUES));

	svsb->phase = SVSB_PHASE_INIT01;
	svsb->dc_voffset_in = ~(svs_readl_relaxed(svsp, DCVALUES) &
				GENMASK(15, 0)) + 1;
	if (svsb->volt_flags & SVSB_INIT01_VOLT_IGNORE ||
	    (svsb->dc_voffset_in & SVSB_DC_SIGNED_BIT &&
	     svsb->volt_flags & SVSB_INIT01_VOLT_INC_ONLY))
		svsb->dc_voffset_in = 0;

	svsb->age_voffset_in = svs_readl_relaxed(svsp, AGEVALUES) &
			       GENMASK(15, 0);

	svs_writel_relaxed(svsp, SVSB_EN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_COMPLETE, INTSTS);
	svsb->core_sel &= ~SVSB_DET_CLK_EN;
}

static inline void svs_init02_isr_handler(struct svs_platform *svsp)
{
	struct svs_bank *svsb = svsp->pbank;

	dev_info(svsb->dev, "%s: VOP74~30:0x%08x~0x%08x, DC:0x%08x\n",
		 __func__, svs_readl_relaxed(svsp, VOP74),
		 svs_readl_relaxed(svsp, VOP30),
		 svs_readl_relaxed(svsp, DCVALUES));

	svsb->phase = SVSB_PHASE_INIT02;
	svsb->get_volts(svsp);

	svs_writel_relaxed(svsp, SVSB_EN_OFF, SVSEN);
	svs_writel_relaxed(svsp, SVSB_INTSTS_COMPLETE, INTSTS);
}

static irqreturn_t svs_isr(int irq, void *data)
{
	struct svs_platform *svsp = data;
	struct svs_bank *svsb = NULL;
	unsigned long flags;
	u32 idx, int_sts, svs_en;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];
		WARN(!svsb, "%s: svsb(%s) is null", __func__, svsb->name);

		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;

		/* Find out which svs bank fires interrupt */
		if (svsb->int_st & svs_readl_relaxed(svsp, INTST)) {
			spin_unlock_irqrestore(&svs_lock, flags);
			continue;
		}

		svs_switch_bank(svsp);
		int_sts = svs_readl_relaxed(svsp, INTSTS);
		svs_en = svs_readl_relaxed(svsp, SVSEN);

		if (int_sts == SVSB_INTSTS_COMPLETE &&
		    svs_en == SVSB_EN_INIT01)
			svs_init01_isr_handler(svsp);
		else if (int_sts == SVSB_INTSTS_COMPLETE &&
			 svs_en == SVSB_EN_INIT02)
			svs_init02_isr_handler(svsp);
		else
			svs_error_isr_handler(svsp);

		spin_unlock_irqrestore(&svs_lock, flags);
		break;
	}

	svs_adjust_pm_opp_volts(svsb);

	if (svsb->phase == SVSB_PHASE_INIT01 ||
	    svsb->phase == SVSB_PHASE_INIT02)
		complete(&svsb->init_completion);

	return IRQ_HANDLED;
}

static int svs_init01(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	unsigned long flags, time_left;
	bool search_done;
	int ret = 0, r;
	u32 opp_freq, opp_vboot, buck_volt, idx, i;

	/* Keep CPUs' core power on for svs_init01 initialization */
	cpuidle_pause_and_lock();

	 /* Svs bank init01 preparation - power enable */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		ret = regulator_enable(svsb->buck);
		if (ret) {
			dev_err(svsb->dev, "%s enable fail: %d\n",
				svsb->buck_name, ret);
			goto svs_init01_resume_cpuidle;
		}

		/* Some buck doesn't support mode change. Show fail msg only */
		ret = regulator_set_mode(svsb->buck, REGULATOR_MODE_FAST);
		if (ret)
			dev_notice(svsb->dev, "set fast mode fail: %d\n", ret);

		if (svsb->volt_flags & SVSB_INIT01_PD_REQ) {
			if (!pm_runtime_enabled(svsb->opp_dev)) {
				pm_runtime_enable(svsb->opp_dev);
				svsb->pm_runtime_enabled_count++;
			}

			ret = pm_runtime_get_sync(svsb->opp_dev);
			if (ret < 0) {
				dev_err(svsb->dev, "mtcmos on fail: %d\n", ret);
				goto svs_init01_resume_cpuidle;
			}
		}
	}

	/*
	 * Svs bank init01 preparation - vboot voltage adjustment
	 * Sometimes two svs banks use the same buck. Therefore,
	 * we have to set each svs bank to target voltage(vboot) first.
	 */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		/*
		 * Find the fastest freq that can be run at vboot and
		 * fix to that freq until svs_init01 is done.
		 */
		search_done = false;
		opp_vboot = svs_bank_volt_to_opp_volt(svsb->vboot,
						      svsb->volt_step,
						      svsb->volt_base);

		for (i = 0; i < svsb->opp_count; i++) {
			opp_freq = svsb->opp_dfreq[i];
			if (!search_done && svsb->opp_dvolt[i] <= opp_vboot) {
				ret = dev_pm_opp_adjust_voltage(svsb->opp_dev,
								opp_freq,
								opp_vboot,
								opp_vboot,
								opp_vboot);
				if (ret) {
					dev_err(svsb->dev,
						"set opp %uuV vboot fail: %d\n",
						opp_vboot, ret);
					goto svs_init01_finish;
				}

				search_done = true;
			} else {
				ret = dev_pm_opp_disable(svsb->opp_dev,
							 svsb->opp_dfreq[i]);
				if (ret) {
					dev_err(svsb->dev,
						"opp %uHz disable fail: %d\n",
						svsb->opp_dfreq[i], ret);
					goto svs_init01_finish;
				}
			}
		}
	}

	/* Svs bank init01 begins */
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		opp_vboot = svs_bank_volt_to_opp_volt(svsb->vboot,
						      svsb->volt_step,
						      svsb->volt_base);

		buck_volt = regulator_get_voltage(svsb->buck);
		if (buck_volt != opp_vboot) {
			dev_err(svsb->dev,
				"buck voltage: %uuV, expected vboot: %uuV\n",
				buck_volt, opp_vboot);
			ret = -EPERM;
			goto svs_init01_finish;
		}

		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_set_bank_phase(svsp, SVSB_PHASE_INIT01);
		spin_unlock_irqrestore(&svs_lock, flags);

		time_left = wait_for_completion_timeout(&svsb->init_completion,
							msecs_to_jiffies(5000));
		if (!time_left) {
			dev_err(svsb->dev, "init01 completion timeout\n");
			ret = -EBUSY;
			goto svs_init01_finish;
		}
	}

svs_init01_finish:
	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT01))
			continue;

		for (i = 0; i < svsb->opp_count; i++) {
			r = dev_pm_opp_enable(svsb->opp_dev,
					      svsb->opp_dfreq[i]);
			if (r)
				dev_err(svsb->dev, "opp %uHz enable fail: %d\n",
					svsb->opp_dfreq[i], r);
		}

		if (svsb->volt_flags & SVSB_INIT01_PD_REQ) {
			r = pm_runtime_put_sync(svsb->opp_dev);
			if (r)
				dev_err(svsb->dev, "mtcmos off fail: %d\n", r);

			if (svsb->pm_runtime_enabled_count > 0) {
				pm_runtime_disable(svsb->opp_dev);
				svsb->pm_runtime_enabled_count--;
			}
		}

		r = regulator_set_mode(svsb->buck, REGULATOR_MODE_NORMAL);
		if (r)
			dev_notice(svsb->dev, "set normal mode fail: %d\n", r);

		r = regulator_disable(svsb->buck);
		if (r)
			dev_err(svsb->dev, "%s disable fail: %d\n",
				svsb->buck_name, r);
	}

svs_init01_resume_cpuidle:
	cpuidle_resume_and_unlock();

	return ret;
}

static int svs_init02(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	unsigned long flags, time_left;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (!(svsb->mode_support & SVSB_MODE_INIT02))
			continue;

		reinit_completion(&svsb->init_completion);
		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_set_bank_phase(svsp, SVSB_PHASE_INIT02);
		spin_unlock_irqrestore(&svs_lock, flags);

		time_left = wait_for_completion_timeout(&svsb->init_completion,
							msecs_to_jiffies(5000));
		if (!time_left) {
			dev_err(svsb->dev, "init02 completion timeout\n");
			return -EBUSY;
		}
	}

	return 0;
}

static int svs_start(struct svs_platform *svsp)
{
	int ret;

	ret = svs_init01(svsp);
	if (ret)
		return ret;

	ret = svs_init02(svsp);
	if (ret)
		return ret;

	return 0;
}

static int svs_suspend(struct device *dev)
{
	struct svs_platform *svsp = dev_get_drvdata(dev);
	struct svs_bank *svsb;
	unsigned long flags;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		/* This might wait for svs_isr() process */
		spin_lock_irqsave(&svs_lock, flags);
		svsp->pbank = svsb;
		svs_switch_bank(svsp);
		svs_writel_relaxed(svsp, SVSB_EN_OFF, SVSEN);
		svs_writel_relaxed(svsp, SVSB_INTSTS_CLEAN, INTSTS);
		spin_unlock_irqrestore(&svs_lock, flags);

		svsb->phase = SVSB_PHASE_ERROR;
		svs_adjust_pm_opp_volts(svsb);
	}

	clk_disable_unprepare(svsp->main_clk);

	return 0;
}

static int svs_resume(struct device *dev)
{
	struct svs_platform *svsp = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(svsp->main_clk);
	if (ret) {
		dev_err(svsp->dev, "cannot enable main_clk, disable svs\n");
		return ret;
	}

	ret = svs_init02(svsp);
	if (ret)
		return ret;

	return 0;
}

static int svs_bank_resource_setup(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	struct dev_pm_opp *opp;
	unsigned long freq;
	int count, ret;
	u32 idx, i;

	dev_set_drvdata(svsp->dev, svsp);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
			svsb->name = "SVSB_CPU_LITTLE";
			break;
		case SVSB_CPU_BIG:
			svsb->name = "SVSB_CPU_BIG";
			break;
		case SVSB_CCI:
			svsb->name = "SVSB_CCI";
			break;
		case SVSB_GPU:
			svsb->name = "SVSB_GPU";
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return -EINVAL;
		}

		svsb->dev = devm_kzalloc(svsp->dev, sizeof(*svsb->dev),
					 GFP_KERNEL);
		if (!svsb->dev)
			return -ENOMEM;

		ret = dev_set_name(svsb->dev, "%s", svsb->name);
		if (ret)
			return ret;

		dev_set_drvdata(svsb->dev, svsp);

		ret = dev_pm_opp_of_add_table(svsb->opp_dev);
		if (ret) {
			dev_err(svsb->dev, "add opp table fail: %d\n", ret);
			return ret;
		}

		mutex_init(&svsb->lock);
		init_completion(&svsb->init_completion);

		if (svsb->mode_support & SVSB_MODE_INIT01) {
			svsb->buck = devm_regulator_get_optional(svsb->opp_dev,
								 svsb->buck_name);
			if (IS_ERR(svsb->buck)) {
				dev_err(svsb->dev, "cannot get \"%s-supply\"\n",
					svsb->buck_name);
				return PTR_ERR(svsb->buck);
			}
		}

		count = dev_pm_opp_get_opp_count(svsb->opp_dev);
		if (svsb->opp_count != count) {
			dev_err(svsb->dev,
				"opp_count not \"%u\" but get \"%d\"?\n",
				svsb->opp_count, count);
			return count;
		}

		for (i = 0, freq = U32_MAX; i < svsb->opp_count; i++, freq--) {
			opp = dev_pm_opp_find_freq_floor(svsb->opp_dev, &freq);
			if (IS_ERR(opp)) {
				dev_err(svsb->dev, "cannot find freq = %ld\n",
					PTR_ERR(opp));
				return PTR_ERR(opp);
			}

			svsb->opp_dfreq[i] = freq;
			svsb->opp_dvolt[i] = dev_pm_opp_get_voltage(opp);
			svsb->freq_pct[i] = percent(svsb->opp_dfreq[i],
						    svsb->freq_base);
			dev_pm_opp_put(opp);
		}
	}

	return 0;
}

static bool svs_mt8183_efuse_parsing(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	u32 idx, i, ft_pgm;

	for (i = 0; i < svsp->efuse_max; i++)
		if (svsp->efuse[i])
			dev_info(svsp->dev, "M_HW_RES%d: 0x%08x\n",
				 i, svsp->efuse[i]);

	if (!svsp->efuse[2]) {
		dev_notice(svsp->dev, "svs_efuse[2] = 0x0?\n");
		return false;
	}

	/* Svs efuse parsing */
	ft_pgm = (svsp->efuse[0] >> 4) & GENMASK(3, 0);

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		if (ft_pgm <= 1)
			svsb->volt_flags |= SVSB_INIT01_VOLT_IGNORE;

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
			svsb->bdes = svsp->efuse[16] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[16] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[16] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[16] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = (svsp->efuse[17] >> 16) & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 10;
			else
				svsb->volt_od += 2;
			break;
		case SVSB_CPU_BIG:
			svsb->bdes = svsp->efuse[18] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[18] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[18] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[18] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = svsp->efuse[17] & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 15;
			else
				svsb->volt_od += 12;
			break;
		case SVSB_CCI:
			svsb->bdes = svsp->efuse[4] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[4] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[4] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[4] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = (svsp->efuse[5] >> 16) & GENMASK(7, 0);

			if (ft_pgm <= 3)
				svsb->volt_od += 10;
			else
				svsb->volt_od += 2;
			break;
		case SVSB_GPU:
			svsb->bdes = svsp->efuse[6] & GENMASK(7, 0);
			svsb->mdes = (svsp->efuse[6] >> 8) & GENMASK(7, 0);
			svsb->dcbdet = (svsp->efuse[6] >> 16) & GENMASK(7, 0);
			svsb->dcmdet = (svsp->efuse[6] >> 24) & GENMASK(7, 0);
			svsb->mtdes  = svsp->efuse[5] & GENMASK(7, 0);

			if (ft_pgm >= 2) {
				svsb->freq_base = 800000000; /* 800MHz */
				svsb->dvt_fixed = 2;
			}
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return false;
		}
	}

	return true;
}

static bool svs_is_efuse_data_correct(struct svs_platform *svsp)
{
	struct nvmem_cell *cell;

	/* Get svs efuse by nvmem */
	cell = nvmem_cell_get(svsp->dev, "svs-calibration-data");
	if (IS_ERR(cell)) {
		dev_err(svsp->dev, "no \"svs-calibration-data\"? %ld\n",
			PTR_ERR(cell));
		return false;
	}

	svsp->efuse = nvmem_cell_read(cell, &svsp->efuse_max);
	if (IS_ERR(svsp->efuse)) {
		dev_err(svsp->dev, "cannot read svs efuse: %ld\n",
			PTR_ERR(svsp->efuse));
		nvmem_cell_put(cell);
		return false;
	}

	svsp->efuse_max /= sizeof(u32);
	nvmem_cell_put(cell);

	return svsp->efuse_parsing(svsp);
}

static struct device *svs_get_subsys_device(struct svs_platform *svsp,
					    const char *node_name)
{
	struct platform_device *pdev;
	struct device_node *np;

	np = of_find_node_by_name(NULL, node_name);
	if (!np) {
		dev_err(svsp->dev, "cannot find %s node\n", node_name);
		return ERR_PTR(-ENODEV);
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		of_node_put(np);
		dev_err(svsp->dev, "cannot find pdev by %s\n", node_name);
		return ERR_PTR(-ENXIO);
	}

	of_node_put(np);

	return &pdev->dev;
}

static struct device *svs_add_device_link(struct svs_platform *svsp,
					  const char *node_name)
{
	struct device *dev;
	struct device_link *sup_link;

	if (!node_name) {
		dev_err(svsp->dev, "node name cannot be null\n");
		return ERR_PTR(-EINVAL);
	}

	dev = svs_get_subsys_device(svsp, node_name);
	if (IS_ERR(dev))
		return dev;

	sup_link = device_link_add(svsp->dev, dev,
				   DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!sup_link) {
		dev_err(svsp->dev, "sup_link is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	if (sup_link->supplier->links.status != DL_DEV_DRIVER_BOUND)
		return ERR_PTR(-EPROBE_DEFER);

	return dev;
}

static int svs_mt8183_platform_probe(struct svs_platform *svsp)
{
	struct svs_bank *svsb;
	u32 idx;

	for (idx = 0; idx < svsp->bank_max; idx++) {
		svsb = &svsp->banks[idx];

		switch (svsb->sw_id) {
		case SVSB_CPU_LITTLE:
		case SVSB_CPU_BIG:
			svsb->opp_dev = get_cpu_device(svsb->cpu_id);
			break;
		case SVSB_CCI:
			svsb->opp_dev = svs_add_device_link(svsp, "cci");
			break;
		case SVSB_GPU:
			svsb->opp_dev = svs_add_device_link(svsp, "gpu");
			break;
		default:
			dev_err(svsb->dev, "unknown sw_id: %u\n", svsb->sw_id);
			return -EINVAL;
		}

		if (IS_ERR(svsb->opp_dev))
			return dev_err_probe(svsp->dev, PTR_ERR(svsb->opp_dev),
					     "failed to get OPP device for bank %d\n",
					     idx);
	}

	return 0;
}

static struct svs_bank svs_mt8183_banks[] = {
	{
		.sw_id			= SVSB_CPU_LITTLE,
		.set_freq_pct		= svs_set_bank_freq_pct_v2,
		.get_volts		= svs_get_bank_volts_v2,
		.cpu_id			= 0,
		.buck_name		= "proc",
		.volt_flags		= SVSB_INIT01_VOLT_INC_ONLY,
		.mode_support		= SVSB_MODE_INIT01 | SVSB_MODE_INIT02,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 1989000000,
		.vboot			= 0x30,
		.volt_step		= 6250,
		.volt_base		= 500000,
		.vmax			= 0x64,
		.vmin			= 0x18,
		.age_config		= 0x555555,
		.dc_config		= 0x555555,
		.dvt_fixed		= 0x7,
		.vco			= 0x10,
		.chk_shift		= 0x77,
		.core_sel		= 0x8fff0000,
		.int_st			= BIT(0),
		.ctl0			= 0x00010001,
	},
	{
		.sw_id			= SVSB_CPU_BIG,
		.set_freq_pct		= svs_set_bank_freq_pct_v2,
		.get_volts		= svs_get_bank_volts_v2,
		.cpu_id			= 4,
		.buck_name		= "proc",
		.volt_flags		= SVSB_INIT01_VOLT_INC_ONLY,
		.mode_support		= SVSB_MODE_INIT01 | SVSB_MODE_INIT02,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 1989000000,
		.vboot			= 0x30,
		.volt_step		= 6250,
		.volt_base		= 500000,
		.vmax			= 0x58,
		.vmin			= 0x10,
		.age_config		= 0x555555,
		.dc_config		= 0x555555,
		.dvt_fixed		= 0x7,
		.vco			= 0x10,
		.chk_shift		= 0x77,
		.core_sel		= 0x8fff0001,
		.int_st			= BIT(1),
		.ctl0			= 0x00000001,
	},
	{
		.sw_id			= SVSB_CCI,
		.set_freq_pct		= svs_set_bank_freq_pct_v2,
		.get_volts		= svs_get_bank_volts_v2,
		.buck_name		= "proc",
		.volt_flags		= SVSB_INIT01_VOLT_INC_ONLY,
		.mode_support		= SVSB_MODE_INIT01 | SVSB_MODE_INIT02,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 1196000000,
		.vboot			= 0x30,
		.volt_step		= 6250,
		.volt_base		= 500000,
		.vmax			= 0x64,
		.vmin			= 0x18,
		.age_config		= 0x555555,
		.dc_config		= 0x555555,
		.dvt_fixed		= 0x7,
		.vco			= 0x10,
		.chk_shift		= 0x77,
		.core_sel		= 0x8fff0002,
		.int_st			= BIT(2),
		.ctl0			= 0x00100003,
	},
	{
		.sw_id			= SVSB_GPU,
		.set_freq_pct		= svs_set_bank_freq_pct_v2,
		.get_volts		= svs_get_bank_volts_v2,
		.buck_name		= "mali",
		.volt_flags		= SVSB_INIT01_PD_REQ |
					  SVSB_INIT01_VOLT_INC_ONLY,
		.mode_support		= SVSB_MODE_INIT01 | SVSB_MODE_INIT02,
		.opp_count		= MAX_OPP_ENTRIES,
		.freq_base		= 900000000,
		.vboot			= 0x30,
		.volt_step		= 6250,
		.volt_base		= 500000,
		.vmax			= 0x40,
		.vmin			= 0x14,
		.age_config		= 0x555555,
		.dc_config		= 0x555555,
		.dvt_fixed		= 0x3,
		.vco			= 0x10,
		.chk_shift		= 0x77,
		.core_sel		= 0x8fff0003,
		.int_st			= BIT(3),
		.ctl0			= 0x00050001,
	},
};

static const struct svs_platform_data svs_mt8183_platform_data = {
	.name = "mt8183-svs",
	.banks = svs_mt8183_banks,
	.efuse_parsing = svs_mt8183_efuse_parsing,
	.probe = svs_mt8183_platform_probe,
	.irqflags = IRQF_TRIGGER_LOW,
	.regs = svs_regs_v2,
	.bank_max = ARRAY_SIZE(svs_mt8183_banks),
};

static const struct of_device_id svs_of_match[] = {
	{
		.compatible = "mediatek,mt8183-svs",
		.data = &svs_mt8183_platform_data,
	}, {
		/* Sentinel */
	},
};

static struct svs_platform *svs_platform_probe(struct platform_device *pdev)
{
	struct svs_platform *svsp;
	const struct svs_platform_data *svsp_data;
	int ret;

	svsp_data = of_device_get_match_data(&pdev->dev);
	if (!svsp_data) {
		dev_err(&pdev->dev, "no svs platform data?\n");
		return ERR_PTR(-EPERM);
	}

	svsp = devm_kzalloc(&pdev->dev, sizeof(*svsp), GFP_KERNEL);
	if (!svsp)
		return ERR_PTR(-ENOMEM);

	svsp->dev = &pdev->dev;
	svsp->name = svsp_data->name;
	svsp->banks = svsp_data->banks;
	svsp->efuse_parsing = svsp_data->efuse_parsing;
	svsp->probe = svsp_data->probe;
	svsp->irqflags = svsp_data->irqflags;
	svsp->regs = svsp_data->regs;
	svsp->bank_max = svsp_data->bank_max;

	ret = svsp->probe(svsp);
	if (ret)
		return ERR_PTR(ret);

	return svsp;
}

static int svs_probe(struct platform_device *pdev)
{
	struct svs_platform *svsp;
	unsigned int svsp_irq;
	int ret;

	svsp = svs_platform_probe(pdev);
	if (IS_ERR(svsp))
		return PTR_ERR(svsp);

	if (!svs_is_efuse_data_correct(svsp)) {
		dev_notice(svsp->dev, "efuse data isn't correct\n");
		ret = -EPERM;
		goto svs_probe_free_resource;
	}

	ret = svs_bank_resource_setup(svsp);
	if (ret) {
		dev_err(svsp->dev, "svs bank resource setup fail: %d\n", ret);
		goto svs_probe_free_resource;
	}

	svsp_irq = irq_of_parse_and_map(svsp->dev->of_node, 0);
	ret = devm_request_threaded_irq(svsp->dev, svsp_irq, NULL, svs_isr,
					svsp->irqflags | IRQF_ONESHOT,
					svsp->name, svsp);
	if (ret) {
		dev_err(svsp->dev, "register irq(%d) failed: %d\n",
			svsp_irq, ret);
		goto svs_probe_free_resource;
	}

	svsp->main_clk = devm_clk_get(svsp->dev, "main");
	if (IS_ERR(svsp->main_clk)) {
		dev_err(svsp->dev, "failed to get clock: %ld\n",
			PTR_ERR(svsp->main_clk));
		ret = PTR_ERR(svsp->main_clk);
		goto svs_probe_free_resource;
	}

	ret = clk_prepare_enable(svsp->main_clk);
	if (ret) {
		dev_err(svsp->dev, "cannot enable main clk: %d\n", ret);
		goto svs_probe_free_resource;
	}

	svsp->base = of_iomap(svsp->dev->of_node, 0);
	if (IS_ERR_OR_NULL(svsp->base)) {
		dev_err(svsp->dev, "cannot find svs register base\n");
		ret = -EINVAL;
		goto svs_probe_clk_disable;
	}

	ret = svs_start(svsp);
	if (ret) {
		dev_err(svsp->dev, "svs start fail: %d\n", ret);
		goto svs_probe_iounmap;
	}

	return 0;

svs_probe_iounmap:
	iounmap(svsp->base);

svs_probe_clk_disable:
	clk_disable_unprepare(svsp->main_clk);

svs_probe_free_resource:
	if (!IS_ERR_OR_NULL(svsp->efuse))
		kfree(svsp->efuse);

	return ret;
}

static SIMPLE_DEV_PM_OPS(svs_pm_ops, svs_suspend, svs_resume);

static struct platform_driver svs_driver = {
	.probe	= svs_probe,
	.driver	= {
		.name		= "mtk-svs",
		.pm		= &svs_pm_ops,
		.of_match_table	= of_match_ptr(svs_of_match),
	},
};

module_platform_driver(svs_driver);

MODULE_AUTHOR("Roger Lu <roger.lu@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SVS driver");
MODULE_LICENSE("GPL");
