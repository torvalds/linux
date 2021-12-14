// SPDX-License-Identifier: GPL-2.0+
/*
 * Tegra30 External Memory Controller driver
 *
 * Based on downstream driver from NVIDIA and tegra124-emc.c
 * Copyright (C) 2011-2014 NVIDIA Corporation
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 * Copyright (C) 2019 GRATE-DRIVER project
 */

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/types.h>

#include <soc/tegra/fuse.h>

#include "mc.h"

#define EMC_INTSTATUS				0x000
#define EMC_INTMASK				0x004
#define EMC_DBG					0x008
#define EMC_CFG					0x00c
#define EMC_REFCTRL				0x020
#define EMC_TIMING_CONTROL			0x028
#define EMC_RC					0x02c
#define EMC_RFC					0x030
#define EMC_RAS					0x034
#define EMC_RP					0x038
#define EMC_R2W					0x03c
#define EMC_W2R					0x040
#define EMC_R2P					0x044
#define EMC_W2P					0x048
#define EMC_RD_RCD				0x04c
#define EMC_WR_RCD				0x050
#define EMC_RRD					0x054
#define EMC_REXT				0x058
#define EMC_WDV					0x05c
#define EMC_QUSE				0x060
#define EMC_QRST				0x064
#define EMC_QSAFE				0x068
#define EMC_RDV					0x06c
#define EMC_REFRESH				0x070
#define EMC_BURST_REFRESH_NUM			0x074
#define EMC_PDEX2WR				0x078
#define EMC_PDEX2RD				0x07c
#define EMC_PCHG2PDEN				0x080
#define EMC_ACT2PDEN				0x084
#define EMC_AR2PDEN				0x088
#define EMC_RW2PDEN				0x08c
#define EMC_TXSR				0x090
#define EMC_TCKE				0x094
#define EMC_TFAW				0x098
#define EMC_TRPAB				0x09c
#define EMC_TCLKSTABLE				0x0a0
#define EMC_TCLKSTOP				0x0a4
#define EMC_TREFBW				0x0a8
#define EMC_QUSE_EXTRA				0x0ac
#define EMC_ODT_WRITE				0x0b0
#define EMC_ODT_READ				0x0b4
#define EMC_WEXT				0x0b8
#define EMC_CTT					0x0bc
#define EMC_MRS_WAIT_CNT			0x0c8
#define EMC_MRS					0x0cc
#define EMC_EMRS				0x0d0
#define EMC_SELF_REF				0x0e0
#define EMC_MRW					0x0e8
#define EMC_XM2DQSPADCTRL3			0x0f8
#define EMC_FBIO_SPARE				0x100
#define EMC_FBIO_CFG5				0x104
#define EMC_FBIO_CFG6				0x114
#define EMC_CFG_RSV				0x120
#define EMC_AUTO_CAL_CONFIG			0x2a4
#define EMC_AUTO_CAL_INTERVAL			0x2a8
#define EMC_AUTO_CAL_STATUS			0x2ac
#define EMC_STATUS				0x2b4
#define EMC_CFG_2				0x2b8
#define EMC_CFG_DIG_DLL				0x2bc
#define EMC_CFG_DIG_DLL_PERIOD			0x2c0
#define EMC_CTT_DURATION			0x2d8
#define EMC_CTT_TERM_CTRL			0x2dc
#define EMC_ZCAL_INTERVAL			0x2e0
#define EMC_ZCAL_WAIT_CNT			0x2e4
#define EMC_ZQ_CAL				0x2ec
#define EMC_XM2CMDPADCTRL			0x2f0
#define EMC_XM2DQSPADCTRL2			0x2fc
#define EMC_XM2DQPADCTRL2			0x304
#define EMC_XM2CLKPADCTRL			0x308
#define EMC_XM2COMPPADCTRL			0x30c
#define EMC_XM2VTTGENPADCTRL			0x310
#define EMC_XM2VTTGENPADCTRL2			0x314
#define EMC_XM2QUSEPADCTRL			0x318
#define EMC_DLL_XFORM_DQS0			0x328
#define EMC_DLL_XFORM_DQS1			0x32c
#define EMC_DLL_XFORM_DQS2			0x330
#define EMC_DLL_XFORM_DQS3			0x334
#define EMC_DLL_XFORM_DQS4			0x338
#define EMC_DLL_XFORM_DQS5			0x33c
#define EMC_DLL_XFORM_DQS6			0x340
#define EMC_DLL_XFORM_DQS7			0x344
#define EMC_DLL_XFORM_QUSE0			0x348
#define EMC_DLL_XFORM_QUSE1			0x34c
#define EMC_DLL_XFORM_QUSE2			0x350
#define EMC_DLL_XFORM_QUSE3			0x354
#define EMC_DLL_XFORM_QUSE4			0x358
#define EMC_DLL_XFORM_QUSE5			0x35c
#define EMC_DLL_XFORM_QUSE6			0x360
#define EMC_DLL_XFORM_QUSE7			0x364
#define EMC_DLL_XFORM_DQ0			0x368
#define EMC_DLL_XFORM_DQ1			0x36c
#define EMC_DLL_XFORM_DQ2			0x370
#define EMC_DLL_XFORM_DQ3			0x374
#define EMC_DLI_TRIM_TXDQS0			0x3a8
#define EMC_DLI_TRIM_TXDQS1			0x3ac
#define EMC_DLI_TRIM_TXDQS2			0x3b0
#define EMC_DLI_TRIM_TXDQS3			0x3b4
#define EMC_DLI_TRIM_TXDQS4			0x3b8
#define EMC_DLI_TRIM_TXDQS5			0x3bc
#define EMC_DLI_TRIM_TXDQS6			0x3c0
#define EMC_DLI_TRIM_TXDQS7			0x3c4
#define EMC_STALL_THEN_EXE_BEFORE_CLKCHANGE	0x3c8
#define EMC_STALL_THEN_EXE_AFTER_CLKCHANGE	0x3cc
#define EMC_UNSTALL_RW_AFTER_CLKCHANGE		0x3d0
#define EMC_SEL_DPD_CTRL			0x3d8
#define EMC_PRE_REFRESH_REQ_CNT			0x3dc
#define EMC_DYN_SELF_REF_CONTROL		0x3e0
#define EMC_TXSRDLL				0x3e4

#define EMC_STATUS_TIMING_UPDATE_STALLED	BIT(23)

#define EMC_MODE_SET_DLL_RESET			BIT(8)
#define EMC_MODE_SET_LONG_CNT			BIT(26)

#define EMC_SELF_REF_CMD_ENABLED		BIT(0)

#define DRAM_DEV_SEL_ALL			(0 << 30)
#define DRAM_DEV_SEL_0				BIT(31)
#define DRAM_DEV_SEL_1				BIT(30)
#define DRAM_BROADCAST(num) \
	((num) > 1 ? DRAM_DEV_SEL_ALL : DRAM_DEV_SEL_0)

#define EMC_ZQ_CAL_CMD				BIT(0)
#define EMC_ZQ_CAL_LONG				BIT(4)
#define EMC_ZQ_CAL_LONG_CMD_DEV0 \
	(DRAM_DEV_SEL_0 | EMC_ZQ_CAL_LONG | EMC_ZQ_CAL_CMD)
#define EMC_ZQ_CAL_LONG_CMD_DEV1 \
	(DRAM_DEV_SEL_1 | EMC_ZQ_CAL_LONG | EMC_ZQ_CAL_CMD)

#define EMC_DBG_READ_MUX_ASSEMBLY		BIT(0)
#define EMC_DBG_WRITE_MUX_ACTIVE		BIT(1)
#define EMC_DBG_FORCE_UPDATE			BIT(2)
#define EMC_DBG_CFG_PRIORITY			BIT(24)

#define EMC_CFG5_QUSE_MODE_SHIFT		13
#define EMC_CFG5_QUSE_MODE_MASK			(7 << EMC_CFG5_QUSE_MODE_SHIFT)

#define EMC_CFG5_QUSE_MODE_INTERNAL_LPBK	2
#define EMC_CFG5_QUSE_MODE_PULSE_INTERN		3

#define EMC_SEL_DPD_CTRL_QUSE_DPD_ENABLE	BIT(9)

#define EMC_XM2COMPPADCTRL_VREF_CAL_ENABLE	BIT(10)

#define EMC_XM2QUSEPADCTRL_IVREF_ENABLE		BIT(4)

#define EMC_XM2DQSPADCTRL2_VREF_ENABLE		BIT(5)
#define EMC_XM2DQSPADCTRL3_VREF_ENABLE		BIT(5)

#define EMC_AUTO_CAL_STATUS_ACTIVE		BIT(31)

#define	EMC_FBIO_CFG5_DRAM_TYPE_MASK		0x3

#define EMC_MRS_WAIT_CNT_SHORT_WAIT_MASK	0x3ff
#define EMC_MRS_WAIT_CNT_LONG_WAIT_SHIFT	16
#define EMC_MRS_WAIT_CNT_LONG_WAIT_MASK \
	(0x3ff << EMC_MRS_WAIT_CNT_LONG_WAIT_SHIFT)

#define EMC_REFCTRL_DEV_SEL_MASK		0x3
#define EMC_REFCTRL_ENABLE			BIT(31)
#define EMC_REFCTRL_ENABLE_ALL(num) \
	(((num) > 1 ? 0 : 2) | EMC_REFCTRL_ENABLE)
#define EMC_REFCTRL_DISABLE_ALL(num)		((num) > 1 ? 0 : 2)

#define EMC_CFG_PERIODIC_QRST			BIT(21)
#define EMC_CFG_DYN_SREF_ENABLE			BIT(28)

#define EMC_CLKCHANGE_REQ_ENABLE		BIT(0)
#define EMC_CLKCHANGE_PD_ENABLE			BIT(1)
#define EMC_CLKCHANGE_SR_ENABLE			BIT(2)

#define EMC_TIMING_UPDATE			BIT(0)

#define EMC_REFRESH_OVERFLOW_INT		BIT(3)
#define EMC_CLKCHANGE_COMPLETE_INT		BIT(4)

enum emc_dram_type {
	DRAM_TYPE_DDR3,
	DRAM_TYPE_DDR1,
	DRAM_TYPE_LPDDR2,
	DRAM_TYPE_DDR2,
};

enum emc_dll_change {
	DLL_CHANGE_NONE,
	DLL_CHANGE_ON,
	DLL_CHANGE_OFF
};

static const u16 emc_timing_registers[] = {
	[0] = EMC_RC,
	[1] = EMC_RFC,
	[2] = EMC_RAS,
	[3] = EMC_RP,
	[4] = EMC_R2W,
	[5] = EMC_W2R,
	[6] = EMC_R2P,
	[7] = EMC_W2P,
	[8] = EMC_RD_RCD,
	[9] = EMC_WR_RCD,
	[10] = EMC_RRD,
	[11] = EMC_REXT,
	[12] = EMC_WEXT,
	[13] = EMC_WDV,
	[14] = EMC_QUSE,
	[15] = EMC_QRST,
	[16] = EMC_QSAFE,
	[17] = EMC_RDV,
	[18] = EMC_REFRESH,
	[19] = EMC_BURST_REFRESH_NUM,
	[20] = EMC_PRE_REFRESH_REQ_CNT,
	[21] = EMC_PDEX2WR,
	[22] = EMC_PDEX2RD,
	[23] = EMC_PCHG2PDEN,
	[24] = EMC_ACT2PDEN,
	[25] = EMC_AR2PDEN,
	[26] = EMC_RW2PDEN,
	[27] = EMC_TXSR,
	[28] = EMC_TXSRDLL,
	[29] = EMC_TCKE,
	[30] = EMC_TFAW,
	[31] = EMC_TRPAB,
	[32] = EMC_TCLKSTABLE,
	[33] = EMC_TCLKSTOP,
	[34] = EMC_TREFBW,
	[35] = EMC_QUSE_EXTRA,
	[36] = EMC_FBIO_CFG6,
	[37] = EMC_ODT_WRITE,
	[38] = EMC_ODT_READ,
	[39] = EMC_FBIO_CFG5,
	[40] = EMC_CFG_DIG_DLL,
	[41] = EMC_CFG_DIG_DLL_PERIOD,
	[42] = EMC_DLL_XFORM_DQS0,
	[43] = EMC_DLL_XFORM_DQS1,
	[44] = EMC_DLL_XFORM_DQS2,
	[45] = EMC_DLL_XFORM_DQS3,
	[46] = EMC_DLL_XFORM_DQS4,
	[47] = EMC_DLL_XFORM_DQS5,
	[48] = EMC_DLL_XFORM_DQS6,
	[49] = EMC_DLL_XFORM_DQS7,
	[50] = EMC_DLL_XFORM_QUSE0,
	[51] = EMC_DLL_XFORM_QUSE1,
	[52] = EMC_DLL_XFORM_QUSE2,
	[53] = EMC_DLL_XFORM_QUSE3,
	[54] = EMC_DLL_XFORM_QUSE4,
	[55] = EMC_DLL_XFORM_QUSE5,
	[56] = EMC_DLL_XFORM_QUSE6,
	[57] = EMC_DLL_XFORM_QUSE7,
	[58] = EMC_DLI_TRIM_TXDQS0,
	[59] = EMC_DLI_TRIM_TXDQS1,
	[60] = EMC_DLI_TRIM_TXDQS2,
	[61] = EMC_DLI_TRIM_TXDQS3,
	[62] = EMC_DLI_TRIM_TXDQS4,
	[63] = EMC_DLI_TRIM_TXDQS5,
	[64] = EMC_DLI_TRIM_TXDQS6,
	[65] = EMC_DLI_TRIM_TXDQS7,
	[66] = EMC_DLL_XFORM_DQ0,
	[67] = EMC_DLL_XFORM_DQ1,
	[68] = EMC_DLL_XFORM_DQ2,
	[69] = EMC_DLL_XFORM_DQ3,
	[70] = EMC_XM2CMDPADCTRL,
	[71] = EMC_XM2DQSPADCTRL2,
	[72] = EMC_XM2DQPADCTRL2,
	[73] = EMC_XM2CLKPADCTRL,
	[74] = EMC_XM2COMPPADCTRL,
	[75] = EMC_XM2VTTGENPADCTRL,
	[76] = EMC_XM2VTTGENPADCTRL2,
	[77] = EMC_XM2QUSEPADCTRL,
	[78] = EMC_XM2DQSPADCTRL3,
	[79] = EMC_CTT_TERM_CTRL,
	[80] = EMC_ZCAL_INTERVAL,
	[81] = EMC_ZCAL_WAIT_CNT,
	[82] = EMC_MRS_WAIT_CNT,
	[83] = EMC_AUTO_CAL_CONFIG,
	[84] = EMC_CTT,
	[85] = EMC_CTT_DURATION,
	[86] = EMC_DYN_SELF_REF_CONTROL,
	[87] = EMC_FBIO_SPARE,
	[88] = EMC_CFG_RSV,
};

struct emc_timing {
	unsigned long rate;

	u32 data[ARRAY_SIZE(emc_timing_registers)];

	u32 emc_auto_cal_interval;
	u32 emc_mode_1;
	u32 emc_mode_2;
	u32 emc_mode_reset;
	u32 emc_zcal_cnt_long;
	bool emc_cfg_periodic_qrst;
	bool emc_cfg_dyn_self_ref;
};

struct tegra_emc {
	struct device *dev;
	struct tegra_mc *mc;
	struct notifier_block clk_nb;
	struct clk *clk;
	void __iomem *regs;
	unsigned int irq;
	bool bad_state;

	struct emc_timing *new_timing;
	struct emc_timing *timings;
	unsigned int num_timings;

	u32 mc_override;
	u32 emc_cfg;

	u32 emc_mode_1;
	u32 emc_mode_2;
	u32 emc_mode_reset;

	bool vref_cal_toggle : 1;
	bool zcal_long : 1;
	bool dll_on : 1;

	struct {
		struct dentry *root;
		unsigned long min_rate;
		unsigned long max_rate;
	} debugfs;
};

static int emc_seq_update_timing(struct tegra_emc *emc)
{
	u32 val;
	int err;

	writel_relaxed(EMC_TIMING_UPDATE, emc->regs + EMC_TIMING_CONTROL);

	err = readl_relaxed_poll_timeout_atomic(emc->regs + EMC_STATUS, val,
				!(val & EMC_STATUS_TIMING_UPDATE_STALLED),
				1, 200);
	if (err) {
		dev_err(emc->dev, "failed to update timing: %d\n", err);
		return err;
	}

	return 0;
}

static irqreturn_t tegra_emc_isr(int irq, void *data)
{
	struct tegra_emc *emc = data;
	u32 intmask = EMC_REFRESH_OVERFLOW_INT;
	u32 status;

	status = readl_relaxed(emc->regs + EMC_INTSTATUS) & intmask;
	if (!status)
		return IRQ_NONE;

	/* notify about HW problem */
	if (status & EMC_REFRESH_OVERFLOW_INT)
		dev_err_ratelimited(emc->dev,
				    "refresh request overflow timeout\n");

	/* clear interrupts */
	writel_relaxed(status, emc->regs + EMC_INTSTATUS);

	return IRQ_HANDLED;
}

static struct emc_timing *emc_find_timing(struct tegra_emc *emc,
					  unsigned long rate)
{
	struct emc_timing *timing = NULL;
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate >= rate) {
			timing = &emc->timings[i];
			break;
		}
	}

	if (!timing) {
		dev_err(emc->dev, "no timing for rate %lu\n", rate);
		return NULL;
	}

	return timing;
}

static bool emc_dqs_preset(struct tegra_emc *emc, struct emc_timing *timing,
			   bool *schmitt_to_vref)
{
	bool preset = false;
	u32 val;

	if (timing->data[71] & EMC_XM2DQSPADCTRL2_VREF_ENABLE) {
		val = readl_relaxed(emc->regs + EMC_XM2DQSPADCTRL2);

		if (!(val & EMC_XM2DQSPADCTRL2_VREF_ENABLE)) {
			val |= EMC_XM2DQSPADCTRL2_VREF_ENABLE;
			writel_relaxed(val, emc->regs + EMC_XM2DQSPADCTRL2);

			preset = true;
		}
	}

	if (timing->data[78] & EMC_XM2DQSPADCTRL3_VREF_ENABLE) {
		val = readl_relaxed(emc->regs + EMC_XM2DQSPADCTRL3);

		if (!(val & EMC_XM2DQSPADCTRL3_VREF_ENABLE)) {
			val |= EMC_XM2DQSPADCTRL3_VREF_ENABLE;
			writel_relaxed(val, emc->regs + EMC_XM2DQSPADCTRL3);

			preset = true;
		}
	}

	if (timing->data[77] & EMC_XM2QUSEPADCTRL_IVREF_ENABLE) {
		val = readl_relaxed(emc->regs + EMC_XM2QUSEPADCTRL);

		if (!(val & EMC_XM2QUSEPADCTRL_IVREF_ENABLE)) {
			val |= EMC_XM2QUSEPADCTRL_IVREF_ENABLE;
			writel_relaxed(val, emc->regs + EMC_XM2QUSEPADCTRL);

			*schmitt_to_vref = true;
			preset = true;
		}
	}

	return preset;
}

static int emc_prepare_mc_clk_cfg(struct tegra_emc *emc, unsigned long rate)
{
	struct tegra_mc *mc = emc->mc;
	unsigned int misc0_index = 16;
	unsigned int i;
	bool same;

	for (i = 0; i < mc->num_timings; i++) {
		if (mc->timings[i].rate != rate)
			continue;

		if (mc->timings[i].emem_data[misc0_index] & BIT(27))
			same = true;
		else
			same = false;

		return tegra20_clk_prepare_emc_mc_same_freq(emc->clk, same);
	}

	return -EINVAL;
}

static int emc_prepare_timing_change(struct tegra_emc *emc, unsigned long rate)
{
	struct emc_timing *timing = emc_find_timing(emc, rate);
	enum emc_dll_change dll_change;
	enum emc_dram_type dram_type;
	bool schmitt_to_vref = false;
	unsigned int pre_wait = 0;
	bool qrst_used = false;
	unsigned int dram_num;
	unsigned int i;
	u32 fbio_cfg5;
	u32 emc_dbg;
	u32 val;
	int err;

	if (!timing || emc->bad_state)
		return -EINVAL;

	dev_dbg(emc->dev, "%s: using timing rate %lu for requested rate %lu\n",
		__func__, timing->rate, rate);

	emc->bad_state = true;

	err = emc_prepare_mc_clk_cfg(emc, rate);
	if (err) {
		dev_err(emc->dev, "mc clock preparation failed: %d\n", err);
		return err;
	}

	emc->vref_cal_toggle = false;
	emc->mc_override = mc_readl(emc->mc, MC_EMEM_ARB_OVERRIDE);
	emc->emc_cfg = readl_relaxed(emc->regs + EMC_CFG);
	emc_dbg = readl_relaxed(emc->regs + EMC_DBG);

	if (emc->dll_on == !!(timing->emc_mode_1 & 0x1))
		dll_change = DLL_CHANGE_NONE;
	else if (timing->emc_mode_1 & 0x1)
		dll_change = DLL_CHANGE_ON;
	else
		dll_change = DLL_CHANGE_OFF;

	emc->dll_on = !!(timing->emc_mode_1 & 0x1);

	if (timing->data[80] && !readl_relaxed(emc->regs + EMC_ZCAL_INTERVAL))
		emc->zcal_long = true;
	else
		emc->zcal_long = false;

	fbio_cfg5 = readl_relaxed(emc->regs + EMC_FBIO_CFG5);
	dram_type = fbio_cfg5 & EMC_FBIO_CFG5_DRAM_TYPE_MASK;

	dram_num = tegra_mc_get_emem_device_count(emc->mc);

	/* disable dynamic self-refresh */
	if (emc->emc_cfg & EMC_CFG_DYN_SREF_ENABLE) {
		emc->emc_cfg &= ~EMC_CFG_DYN_SREF_ENABLE;
		writel_relaxed(emc->emc_cfg, emc->regs + EMC_CFG);

		pre_wait = 5;
	}

	/* update MC arbiter settings */
	val = mc_readl(emc->mc, MC_EMEM_ARB_OUTSTANDING_REQ);
	if (!(val & MC_EMEM_ARB_OUTSTANDING_REQ_HOLDOFF_OVERRIDE) ||
	    ((val & MC_EMEM_ARB_OUTSTANDING_REQ_MAX_MASK) > 0x50)) {

		val = MC_EMEM_ARB_OUTSTANDING_REQ_LIMIT_ENABLE |
		      MC_EMEM_ARB_OUTSTANDING_REQ_HOLDOFF_OVERRIDE | 0x50;
		mc_writel(emc->mc, val, MC_EMEM_ARB_OUTSTANDING_REQ);
		mc_writel(emc->mc, MC_TIMING_UPDATE, MC_TIMING_CONTROL);
	}

	if (emc->mc_override & MC_EMEM_ARB_OVERRIDE_EACK_MASK)
		mc_writel(emc->mc,
			  emc->mc_override & ~MC_EMEM_ARB_OVERRIDE_EACK_MASK,
			  MC_EMEM_ARB_OVERRIDE);

	/* check DQ/DQS VREF delay */
	if (emc_dqs_preset(emc, timing, &schmitt_to_vref)) {
		if (pre_wait < 3)
			pre_wait = 3;
	}

	if (pre_wait) {
		err = emc_seq_update_timing(emc);
		if (err)
			return err;

		udelay(pre_wait);
	}

	/* disable auto-calibration if VREF mode is switching */
	if (timing->emc_auto_cal_interval) {
		val = readl_relaxed(emc->regs + EMC_XM2COMPPADCTRL);
		val ^= timing->data[74];

		if (val & EMC_XM2COMPPADCTRL_VREF_CAL_ENABLE) {
			writel_relaxed(0, emc->regs + EMC_AUTO_CAL_INTERVAL);

			err = readl_relaxed_poll_timeout_atomic(
				emc->regs + EMC_AUTO_CAL_STATUS, val,
				!(val & EMC_AUTO_CAL_STATUS_ACTIVE), 1, 300);
			if (err) {
				dev_err(emc->dev,
					"auto-cal finish timeout: %d\n", err);
				return err;
			}

			emc->vref_cal_toggle = true;
		}
	}

	/* program shadow registers */
	for (i = 0; i < ARRAY_SIZE(timing->data); i++) {
		/* EMC_XM2CLKPADCTRL should be programmed separately */
		if (i != 73)
			writel_relaxed(timing->data[i],
				       emc->regs + emc_timing_registers[i]);
	}

	err = tegra_mc_write_emem_configuration(emc->mc, timing->rate);
	if (err)
		return err;

	/* DDR3: predict MRS long wait count */
	if (dram_type == DRAM_TYPE_DDR3 && dll_change == DLL_CHANGE_ON) {
		u32 cnt = 512;

		if (emc->zcal_long)
			cnt -= dram_num * 256;

		val = timing->data[82] & EMC_MRS_WAIT_CNT_SHORT_WAIT_MASK;
		if (cnt < val)
			cnt = val;

		val = timing->data[82] & ~EMC_MRS_WAIT_CNT_LONG_WAIT_MASK;
		val |= (cnt << EMC_MRS_WAIT_CNT_LONG_WAIT_SHIFT) &
			EMC_MRS_WAIT_CNT_LONG_WAIT_MASK;

		writel_relaxed(val, emc->regs + EMC_MRS_WAIT_CNT);
	}

	/* this read also completes the writes */
	val = readl_relaxed(emc->regs + EMC_SEL_DPD_CTRL);

	if (!(val & EMC_SEL_DPD_CTRL_QUSE_DPD_ENABLE) && schmitt_to_vref) {
		u32 cur_mode, new_mode;

		cur_mode = fbio_cfg5 & EMC_CFG5_QUSE_MODE_MASK;
		cur_mode >>= EMC_CFG5_QUSE_MODE_SHIFT;

		new_mode = timing->data[39] & EMC_CFG5_QUSE_MODE_MASK;
		new_mode >>= EMC_CFG5_QUSE_MODE_SHIFT;

		if ((cur_mode != EMC_CFG5_QUSE_MODE_PULSE_INTERN &&
		     cur_mode != EMC_CFG5_QUSE_MODE_INTERNAL_LPBK) ||
		    (new_mode != EMC_CFG5_QUSE_MODE_PULSE_INTERN &&
		     new_mode != EMC_CFG5_QUSE_MODE_INTERNAL_LPBK))
			qrst_used = true;
	}

	/* flow control marker 1 */
	writel_relaxed(0x1, emc->regs + EMC_STALL_THEN_EXE_BEFORE_CLKCHANGE);

	/* enable periodic reset */
	if (qrst_used) {
		writel_relaxed(emc_dbg | EMC_DBG_WRITE_MUX_ACTIVE,
			       emc->regs + EMC_DBG);
		writel_relaxed(emc->emc_cfg | EMC_CFG_PERIODIC_QRST,
			       emc->regs + EMC_CFG);
		writel_relaxed(emc_dbg, emc->regs + EMC_DBG);
	}

	/* disable auto-refresh to save time after clock change */
	writel_relaxed(EMC_REFCTRL_DISABLE_ALL(dram_num),
		       emc->regs + EMC_REFCTRL);

	/* turn off DLL and enter self-refresh on DDR3 */
	if (dram_type == DRAM_TYPE_DDR3) {
		if (dll_change == DLL_CHANGE_OFF)
			writel_relaxed(timing->emc_mode_1,
				       emc->regs + EMC_EMRS);

		writel_relaxed(DRAM_BROADCAST(dram_num) |
			       EMC_SELF_REF_CMD_ENABLED,
			       emc->regs + EMC_SELF_REF);
	}

	/* flow control marker 2 */
	writel_relaxed(0x1, emc->regs + EMC_STALL_THEN_EXE_AFTER_CLKCHANGE);

	/* enable write-active MUX, update unshadowed pad control */
	writel_relaxed(emc_dbg | EMC_DBG_WRITE_MUX_ACTIVE, emc->regs + EMC_DBG);
	writel_relaxed(timing->data[73], emc->regs + EMC_XM2CLKPADCTRL);

	/* restore periodic QRST and disable write-active MUX */
	val = !!(emc->emc_cfg & EMC_CFG_PERIODIC_QRST);
	if (qrst_used || timing->emc_cfg_periodic_qrst != val) {
		if (timing->emc_cfg_periodic_qrst)
			emc->emc_cfg |= EMC_CFG_PERIODIC_QRST;
		else
			emc->emc_cfg &= ~EMC_CFG_PERIODIC_QRST;

		writel_relaxed(emc->emc_cfg, emc->regs + EMC_CFG);
	}
	writel_relaxed(emc_dbg, emc->regs + EMC_DBG);

	/* exit self-refresh on DDR3 */
	if (dram_type == DRAM_TYPE_DDR3)
		writel_relaxed(DRAM_BROADCAST(dram_num),
			       emc->regs + EMC_SELF_REF);

	/* set DRAM-mode registers */
	if (dram_type == DRAM_TYPE_DDR3) {
		if (timing->emc_mode_1 != emc->emc_mode_1)
			writel_relaxed(timing->emc_mode_1,
				       emc->regs + EMC_EMRS);

		if (timing->emc_mode_2 != emc->emc_mode_2)
			writel_relaxed(timing->emc_mode_2,
				       emc->regs + EMC_EMRS);

		if (timing->emc_mode_reset != emc->emc_mode_reset ||
		    dll_change == DLL_CHANGE_ON) {
			val = timing->emc_mode_reset;
			if (dll_change == DLL_CHANGE_ON) {
				val |= EMC_MODE_SET_DLL_RESET;
				val |= EMC_MODE_SET_LONG_CNT;
			} else {
				val &= ~EMC_MODE_SET_DLL_RESET;
			}
			writel_relaxed(val, emc->regs + EMC_MRS);
		}
	} else {
		if (timing->emc_mode_2 != emc->emc_mode_2)
			writel_relaxed(timing->emc_mode_2,
				       emc->regs + EMC_MRW);

		if (timing->emc_mode_1 != emc->emc_mode_1)
			writel_relaxed(timing->emc_mode_1,
				       emc->regs + EMC_MRW);
	}

	emc->emc_mode_1 = timing->emc_mode_1;
	emc->emc_mode_2 = timing->emc_mode_2;
	emc->emc_mode_reset = timing->emc_mode_reset;

	/* issue ZCAL command if turning ZCAL on */
	if (emc->zcal_long) {
		writel_relaxed(EMC_ZQ_CAL_LONG_CMD_DEV0,
			       emc->regs + EMC_ZQ_CAL);

		if (dram_num > 1)
			writel_relaxed(EMC_ZQ_CAL_LONG_CMD_DEV1,
				       emc->regs + EMC_ZQ_CAL);
	}

	/* flow control marker 3 */
	writel_relaxed(0x1, emc->regs + EMC_UNSTALL_RW_AFTER_CLKCHANGE);

	/*
	 * Read and discard an arbitrary MC register (Note: EMC registers
	 * can't be used) to ensure the register writes are completed.
	 */
	mc_readl(emc->mc, MC_EMEM_ARB_OVERRIDE);

	return 0;
}

static int emc_complete_timing_change(struct tegra_emc *emc,
				      unsigned long rate)
{
	struct emc_timing *timing = emc_find_timing(emc, rate);
	unsigned int dram_num;
	int err;
	u32 v;

	err = readl_relaxed_poll_timeout_atomic(emc->regs + EMC_INTSTATUS, v,
						v & EMC_CLKCHANGE_COMPLETE_INT,
						1, 100);
	if (err) {
		dev_err(emc->dev, "emc-car handshake timeout: %d\n", err);
		return err;
	}

	/* re-enable auto-refresh */
	dram_num = tegra_mc_get_emem_device_count(emc->mc);
	writel_relaxed(EMC_REFCTRL_ENABLE_ALL(dram_num),
		       emc->regs + EMC_REFCTRL);

	/* restore auto-calibration */
	if (emc->vref_cal_toggle)
		writel_relaxed(timing->emc_auto_cal_interval,
			       emc->regs + EMC_AUTO_CAL_INTERVAL);

	/* restore dynamic self-refresh */
	if (timing->emc_cfg_dyn_self_ref) {
		emc->emc_cfg |= EMC_CFG_DYN_SREF_ENABLE;
		writel_relaxed(emc->emc_cfg, emc->regs + EMC_CFG);
	}

	/* set number of clocks to wait after each ZQ command */
	if (emc->zcal_long)
		writel_relaxed(timing->emc_zcal_cnt_long,
			       emc->regs + EMC_ZCAL_WAIT_CNT);

	/* wait for writes to settle */
	udelay(2);

	/* update restored timing */
	err = emc_seq_update_timing(emc);
	if (!err)
		emc->bad_state = false;

	/* restore early ACK */
	mc_writel(emc->mc, emc->mc_override, MC_EMEM_ARB_OVERRIDE);

	return err;
}

static int emc_unprepare_timing_change(struct tegra_emc *emc,
				       unsigned long rate)
{
	if (!emc->bad_state) {
		/* shouldn't ever happen in practice */
		dev_err(emc->dev, "timing configuration can't be reverted\n");
		emc->bad_state = true;
	}

	return 0;
}

static int emc_clk_change_notify(struct notifier_block *nb,
				 unsigned long msg, void *data)
{
	struct tegra_emc *emc = container_of(nb, struct tegra_emc, clk_nb);
	struct clk_notifier_data *cnd = data;
	int err;

	switch (msg) {
	case PRE_RATE_CHANGE:
		/*
		 * Disable interrupt since read accesses are prohibited after
		 * stalling.
		 */
		disable_irq(emc->irq);
		err = emc_prepare_timing_change(emc, cnd->new_rate);
		enable_irq(emc->irq);
		break;

	case ABORT_RATE_CHANGE:
		err = emc_unprepare_timing_change(emc, cnd->old_rate);
		break;

	case POST_RATE_CHANGE:
		err = emc_complete_timing_change(emc, cnd->new_rate);
		break;

	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(err);
}

static int load_one_timing_from_dt(struct tegra_emc *emc,
				   struct emc_timing *timing,
				   struct device_node *node)
{
	u32 value;
	int err;

	err = of_property_read_u32(node, "clock-frequency", &value);
	if (err) {
		dev_err(emc->dev, "timing %pOF: failed to read rate: %d\n",
			node, err);
		return err;
	}

	timing->rate = value;

	err = of_property_read_u32_array(node, "nvidia,emc-configuration",
					 timing->data,
					 ARRAY_SIZE(emc_timing_registers));
	if (err) {
		dev_err(emc->dev,
			"timing %pOF: failed to read emc timing data: %d\n",
			node, err);
		return err;
	}

#define EMC_READ_BOOL(prop, dtprop) \
	timing->prop = of_property_read_bool(node, dtprop);

#define EMC_READ_U32(prop, dtprop) \
	err = of_property_read_u32(node, dtprop, &timing->prop); \
	if (err) { \
		dev_err(emc->dev, \
			"timing %pOFn: failed to read " #prop ": %d\n", \
			node, err); \
		return err; \
	}

	EMC_READ_U32(emc_auto_cal_interval, "nvidia,emc-auto-cal-interval")
	EMC_READ_U32(emc_mode_1, "nvidia,emc-mode-1")
	EMC_READ_U32(emc_mode_2, "nvidia,emc-mode-2")
	EMC_READ_U32(emc_mode_reset, "nvidia,emc-mode-reset")
	EMC_READ_U32(emc_zcal_cnt_long, "nvidia,emc-zcal-cnt-long")
	EMC_READ_BOOL(emc_cfg_dyn_self_ref, "nvidia,emc-cfg-dyn-self-ref")
	EMC_READ_BOOL(emc_cfg_periodic_qrst, "nvidia,emc-cfg-periodic-qrst")

#undef EMC_READ_U32
#undef EMC_READ_BOOL

	dev_dbg(emc->dev, "%s: %pOF: rate %lu\n", __func__, node, timing->rate);

	return 0;
}

static int cmp_timings(const void *_a, const void *_b)
{
	const struct emc_timing *a = _a;
	const struct emc_timing *b = _b;

	if (a->rate < b->rate)
		return -1;

	if (a->rate > b->rate)
		return 1;

	return 0;
}

static int emc_check_mc_timings(struct tegra_emc *emc)
{
	struct tegra_mc *mc = emc->mc;
	unsigned int i;

	if (emc->num_timings != mc->num_timings) {
		dev_err(emc->dev, "emc/mc timings number mismatch: %u %u\n",
			emc->num_timings, mc->num_timings);
		return -EINVAL;
	}

	for (i = 0; i < mc->num_timings; i++) {
		if (emc->timings[i].rate != mc->timings[i].rate) {
			dev_err(emc->dev,
				"emc/mc timing rate mismatch: %lu %lu\n",
				emc->timings[i].rate, mc->timings[i].rate);
			return -EINVAL;
		}
	}

	return 0;
}

static int emc_load_timings_from_dt(struct tegra_emc *emc,
				    struct device_node *node)
{
	struct device_node *child;
	struct emc_timing *timing;
	int child_count;
	int err;

	child_count = of_get_child_count(node);
	if (!child_count) {
		dev_err(emc->dev, "no memory timings in: %pOF\n", node);
		return -EINVAL;
	}

	emc->timings = devm_kcalloc(emc->dev, child_count, sizeof(*timing),
				    GFP_KERNEL);
	if (!emc->timings)
		return -ENOMEM;

	emc->num_timings = child_count;
	timing = emc->timings;

	for_each_child_of_node(node, child) {
		err = load_one_timing_from_dt(emc, timing++, child);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	sort(emc->timings, emc->num_timings, sizeof(*timing), cmp_timings,
	     NULL);

	err = emc_check_mc_timings(emc);
	if (err)
		return err;

	dev_info(emc->dev,
		 "got %u timings for RAM code %u (min %luMHz max %luMHz)\n",
		 emc->num_timings,
		 tegra_read_ram_code(),
		 emc->timings[0].rate / 1000000,
		 emc->timings[emc->num_timings - 1].rate / 1000000);

	return 0;
}

static struct device_node *emc_find_node_by_ram_code(struct device *dev)
{
	struct device_node *np;
	u32 value, ram_code;
	int err;

	ram_code = tegra_read_ram_code();

	for_each_child_of_node(dev->of_node, np) {
		err = of_property_read_u32(np, "nvidia,ram-code", &value);
		if (err || value != ram_code)
			continue;

		return np;
	}

	dev_err(dev, "no memory timings for RAM code %u found in device-tree\n",
		ram_code);

	return NULL;
}

static int emc_setup_hw(struct tegra_emc *emc)
{
	u32 intmask = EMC_REFRESH_OVERFLOW_INT;
	u32 fbio_cfg5, emc_cfg, emc_dbg;
	enum emc_dram_type dram_type;

	fbio_cfg5 = readl_relaxed(emc->regs + EMC_FBIO_CFG5);
	dram_type = fbio_cfg5 & EMC_FBIO_CFG5_DRAM_TYPE_MASK;

	emc_cfg = readl_relaxed(emc->regs + EMC_CFG_2);

	/* enable EMC and CAR to handshake on PLL divider/source changes */
	emc_cfg |= EMC_CLKCHANGE_REQ_ENABLE;

	/* configure clock change mode accordingly to DRAM type */
	switch (dram_type) {
	case DRAM_TYPE_LPDDR2:
		emc_cfg |= EMC_CLKCHANGE_PD_ENABLE;
		emc_cfg &= ~EMC_CLKCHANGE_SR_ENABLE;
		break;

	default:
		emc_cfg &= ~EMC_CLKCHANGE_SR_ENABLE;
		emc_cfg &= ~EMC_CLKCHANGE_PD_ENABLE;
		break;
	}

	writel_relaxed(emc_cfg, emc->regs + EMC_CFG_2);

	/* initialize interrupt */
	writel_relaxed(intmask, emc->regs + EMC_INTMASK);
	writel_relaxed(0xffffffff, emc->regs + EMC_INTSTATUS);

	/* ensure that unwanted debug features are disabled */
	emc_dbg = readl_relaxed(emc->regs + EMC_DBG);
	emc_dbg |= EMC_DBG_CFG_PRIORITY;
	emc_dbg &= ~EMC_DBG_READ_MUX_ASSEMBLY;
	emc_dbg &= ~EMC_DBG_WRITE_MUX_ACTIVE;
	emc_dbg &= ~EMC_DBG_FORCE_UPDATE;
	writel_relaxed(emc_dbg, emc->regs + EMC_DBG);

	return 0;
}

static long emc_round_rate(unsigned long rate,
			   unsigned long min_rate,
			   unsigned long max_rate,
			   void *arg)
{
	struct emc_timing *timing = NULL;
	struct tegra_emc *emc = arg;
	unsigned int i;

	min_rate = min(min_rate, emc->timings[emc->num_timings - 1].rate);

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate < rate && i != emc->num_timings - 1)
			continue;

		if (emc->timings[i].rate > max_rate) {
			i = max(i, 1u) - 1;

			if (emc->timings[i].rate < min_rate)
				break;
		}

		if (emc->timings[i].rate < min_rate)
			continue;

		timing = &emc->timings[i];
		break;
	}

	if (!timing) {
		dev_err(emc->dev, "no timing for rate %lu min %lu max %lu\n",
			rate, min_rate, max_rate);
		return -EINVAL;
	}

	return timing->rate;
}

/*
 * debugfs interface
 *
 * The memory controller driver exposes some files in debugfs that can be used
 * to control the EMC frequency. The top-level directory can be found here:
 *
 *   /sys/kernel/debug/emc
 *
 * It contains the following files:
 *
 *   - available_rates: This file contains a list of valid, space-separated
 *     EMC frequencies.
 *
 *   - min_rate: Writing a value to this file sets the given frequency as the
 *       floor of the permitted range. If this is higher than the currently
 *       configured EMC frequency, this will cause the frequency to be
 *       increased so that it stays within the valid range.
 *
 *   - max_rate: Similarily to the min_rate file, writing a value to this file
 *       sets the given frequency as the ceiling of the permitted range. If
 *       the value is lower than the currently configured EMC frequency, this
 *       will cause the frequency to be decreased so that it stays within the
 *       valid range.
 */

static bool tegra_emc_validate_rate(struct tegra_emc *emc, unsigned long rate)
{
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++)
		if (rate == emc->timings[i].rate)
			return true;

	return false;
}

static int tegra_emc_debug_available_rates_show(struct seq_file *s, void *data)
{
	struct tegra_emc *emc = s->private;
	const char *prefix = "";
	unsigned int i;

	for (i = 0; i < emc->num_timings; i++) {
		seq_printf(s, "%s%lu", prefix, emc->timings[i].rate);
		prefix = " ";
	}

	seq_puts(s, "\n");

	return 0;
}

static int tegra_emc_debug_available_rates_open(struct inode *inode,
						struct file *file)
{
	return single_open(file, tegra_emc_debug_available_rates_show,
			   inode->i_private);
}

static const struct file_operations tegra_emc_debug_available_rates_fops = {
	.open = tegra_emc_debug_available_rates_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int tegra_emc_debug_min_rate_get(void *data, u64 *rate)
{
	struct tegra_emc *emc = data;

	*rate = emc->debugfs.min_rate;

	return 0;
}

static int tegra_emc_debug_min_rate_set(void *data, u64 rate)
{
	struct tegra_emc *emc = data;
	int err;

	if (!tegra_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_min_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.min_rate = rate;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(tegra_emc_debug_min_rate_fops,
			tegra_emc_debug_min_rate_get,
			tegra_emc_debug_min_rate_set, "%llu\n");

static int tegra_emc_debug_max_rate_get(void *data, u64 *rate)
{
	struct tegra_emc *emc = data;

	*rate = emc->debugfs.max_rate;

	return 0;
}

static int tegra_emc_debug_max_rate_set(void *data, u64 rate)
{
	struct tegra_emc *emc = data;
	int err;

	if (!tegra_emc_validate_rate(emc, rate))
		return -EINVAL;

	err = clk_set_max_rate(emc->clk, rate);
	if (err < 0)
		return err;

	emc->debugfs.max_rate = rate;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(tegra_emc_debug_max_rate_fops,
			tegra_emc_debug_max_rate_get,
			tegra_emc_debug_max_rate_set, "%llu\n");

static void tegra_emc_debugfs_init(struct tegra_emc *emc)
{
	struct device *dev = emc->dev;
	unsigned int i;
	int err;

	emc->debugfs.min_rate = ULONG_MAX;
	emc->debugfs.max_rate = 0;

	for (i = 0; i < emc->num_timings; i++) {
		if (emc->timings[i].rate < emc->debugfs.min_rate)
			emc->debugfs.min_rate = emc->timings[i].rate;

		if (emc->timings[i].rate > emc->debugfs.max_rate)
			emc->debugfs.max_rate = emc->timings[i].rate;
	}

	if (!emc->num_timings) {
		emc->debugfs.min_rate = clk_get_rate(emc->clk);
		emc->debugfs.max_rate = emc->debugfs.min_rate;
	}

	err = clk_set_rate_range(emc->clk, emc->debugfs.min_rate,
				 emc->debugfs.max_rate);
	if (err < 0) {
		dev_err(dev, "failed to set rate range [%lu-%lu] for %pC\n",
			emc->debugfs.min_rate, emc->debugfs.max_rate,
			emc->clk);
	}

	emc->debugfs.root = debugfs_create_dir("emc", NULL);
	if (!emc->debugfs.root) {
		dev_err(emc->dev, "failed to create debugfs directory\n");
		return;
	}

	debugfs_create_file("available_rates", 0444, emc->debugfs.root,
			    emc, &tegra_emc_debug_available_rates_fops);
	debugfs_create_file("min_rate", 0644, emc->debugfs.root,
			    emc, &tegra_emc_debug_min_rate_fops);
	debugfs_create_file("max_rate", 0644, emc->debugfs.root,
			    emc, &tegra_emc_debug_max_rate_fops);
}

static int tegra_emc_probe(struct platform_device *pdev)
{
	struct platform_device *mc;
	struct device_node *np;
	struct tegra_emc *emc;
	int err;

	if (of_get_child_count(pdev->dev.of_node) == 0) {
		dev_info(&pdev->dev,
			 "device-tree node doesn't have memory timings\n");
		return -ENODEV;
	}

	np = of_parse_phandle(pdev->dev.of_node, "nvidia,memory-controller", 0);
	if (!np) {
		dev_err(&pdev->dev, "could not get memory controller node\n");
		return -ENOENT;
	}

	mc = of_find_device_by_node(np);
	of_node_put(np);
	if (!mc)
		return -ENOENT;

	np = emc_find_node_by_ram_code(&pdev->dev);
	if (!np)
		return -EINVAL;

	emc = devm_kzalloc(&pdev->dev, sizeof(*emc), GFP_KERNEL);
	if (!emc) {
		of_node_put(np);
		return -ENOMEM;
	}

	emc->mc = platform_get_drvdata(mc);
	if (!emc->mc)
		return -EPROBE_DEFER;

	emc->clk_nb.notifier_call = emc_clk_change_notify;
	emc->dev = &pdev->dev;

	err = emc_load_timings_from_dt(emc, np);
	of_node_put(np);
	if (err)
		return err;

	emc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(emc->regs))
		return PTR_ERR(emc->regs);

	err = emc_setup_hw(emc);
	if (err)
		return err;

	err = platform_get_irq(pdev, 0);
	if (err < 0) {
		dev_err(&pdev->dev, "interrupt not specified: %d\n", err);
		return err;
	}
	emc->irq = err;

	err = devm_request_irq(&pdev->dev, emc->irq, tegra_emc_isr, 0,
			       dev_name(&pdev->dev), emc);
	if (err) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", err);
		return err;
	}

	tegra20_clk_set_emc_round_callback(emc_round_rate, emc);

	emc->clk = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(emc->clk)) {
		err = PTR_ERR(emc->clk);
		dev_err(&pdev->dev, "failed to get emc clock: %d\n", err);
		goto unset_cb;
	}

	err = clk_notifier_register(emc->clk, &emc->clk_nb);
	if (err) {
		dev_err(&pdev->dev, "failed to register clk notifier: %d\n",
			err);
		goto unset_cb;
	}

	platform_set_drvdata(pdev, emc);
	tegra_emc_debugfs_init(emc);

	return 0;

unset_cb:
	tegra20_clk_set_emc_round_callback(NULL, NULL);

	return err;
}

static int tegra_emc_suspend(struct device *dev)
{
	struct tegra_emc *emc = dev_get_drvdata(dev);
	int err;

	/* take exclusive control over the clock's rate */
	err = clk_rate_exclusive_get(emc->clk);
	if (err) {
		dev_err(emc->dev, "failed to acquire clk: %d\n", err);
		return err;
	}

	/* suspending in a bad state will hang machine */
	if (WARN(emc->bad_state, "hardware in a bad state\n"))
		return -EINVAL;

	emc->bad_state = true;

	return 0;
}

static int tegra_emc_resume(struct device *dev)
{
	struct tegra_emc *emc = dev_get_drvdata(dev);

	emc_setup_hw(emc);
	emc->bad_state = false;

	clk_rate_exclusive_put(emc->clk);

	return 0;
}

static const struct dev_pm_ops tegra_emc_pm_ops = {
	.suspend = tegra_emc_suspend,
	.resume = tegra_emc_resume,
};

static const struct of_device_id tegra_emc_of_match[] = {
	{ .compatible = "nvidia,tegra30-emc", },
	{},
};

static struct platform_driver tegra_emc_driver = {
	.probe = tegra_emc_probe,
	.driver = {
		.name = "tegra30-emc",
		.of_match_table = tegra_emc_of_match,
		.pm = &tegra_emc_pm_ops,
		.suppress_bind_attrs = true,
	},
};

static int __init tegra_emc_init(void)
{
	return platform_driver_register(&tegra_emc_driver);
}
subsys_initcall(tegra_emc_init);
