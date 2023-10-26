// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <dt-bindings/phy/phy.h>

#include "phy-qcom-qmp.h"

/* QPHY_SW_RESET bit */
#define SW_RESET				BIT(0)
/* QPHY_POWER_DOWN_CONTROL */
#define SW_PWRDN				BIT(0)
#define REFCLK_DRV_DSBL				BIT(1)
/* QPHY_START_CONTROL bits */
#define SERDES_START				BIT(0)
#define PCS_START				BIT(1)
#define PLL_READY_GATE_EN			BIT(3)
/* QPHY_PCS_STATUS bit */
#define PHYSTATUS				BIT(6)
#define PHYSTATUS_4_20				BIT(7)
/* QPHY_COM_PCS_READY_STATUS bit */
#define PCS_READY				BIT(0)

#define PHY_INIT_COMPLETE_TIMEOUT		10000
#define POWER_DOWN_DELAY_US_MIN			10
#define POWER_DOWN_DELAY_US_MAX			11

struct qmp_phy_init_tbl {
	unsigned int offset;
	unsigned int val;
	/*
	 * register part of layout ?
	 * if yes, then offset gives index in the reg-layout
	 */
	bool in_layout;
	/*
	 * mask of lanes for which this register is written
	 * for cases when second lane needs different values
	 */
	u8 lane_mask;
};

#define QMP_PHY_INIT_CFG(o, v)		\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_L(o, v)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.in_layout = true,	\
		.lane_mask = 0xff,	\
	}

#define QMP_PHY_INIT_CFG_LANE(o, v, l)	\
	{				\
		.offset = o,		\
		.val = v,		\
		.lane_mask = l,		\
	}

/* set of registers with offsets different per-PHY */
enum qphy_reg_layout {
	/* Common block control registers */
	QPHY_COM_SW_RESET,
	QPHY_COM_POWER_DOWN_CONTROL,
	QPHY_COM_START_CONTROL,
	QPHY_COM_PCS_READY_STATUS,
	/* PCS registers */
	QPHY_SW_RESET,
	QPHY_START_CTRL,
	QPHY_PCS_STATUS,
	QPHY_PCS_POWER_DOWN_CONTROL,
	/* Keep last to ensure regs_layout arrays are properly initialized */
	QPHY_LAYOUT_SIZE
};

static const unsigned int pciephy_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_COM_SW_RESET]		= 0x400,
	[QPHY_COM_POWER_DOWN_CONTROL]	= 0x404,
	[QPHY_COM_START_CONTROL]	= 0x408,
	[QPHY_COM_PCS_READY_STATUS]	= 0x448,
	[QPHY_SW_RESET]			= 0x00,
	[QPHY_START_CTRL]		= 0x08,
	[QPHY_PCS_STATUS]		= 0x174,
};

static const struct qmp_phy_init_tbl msm8996_pcie_serdes_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_COM_BIAS_EN_CLKBUFLR_EN, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_COM_CMN_CONFIG, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP_EN, 0x42),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_MAP, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_COM_VCO_TUNE_TIMER2, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SVS_MODE_CLK_SEL, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORE_CLK_EN, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_CORECLK_DIV, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TIMER, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_COM_DEC_START_MODE0, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START3_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START2_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_DIV_FRAC_START1_MODE0, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP3_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP2_MODE0, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_COM_LOCK_CMP1_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_SELECT, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYS_CLK_CTRL, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_BUF_ENABLE, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SYSCLK_EN_SEL, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_COM_CP_CTRL_MODE0, 0x0b),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_CCTRL_MODE0, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN1_MODE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_INTEGLOOP_GAIN0_MODE0, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER1, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_PER2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_ADJ_PER2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE1, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_COM_SSC_STEP_SIZE2, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESCODE_DIV_NUM, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_COM_BG_TRIM, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_PLL_IVCO, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_EP_DIV, 0x19),
	QMP_PHY_INIT_CFG(QSERDES_COM_CLK_ENABLE1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_COM_HSCLK_SEL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_COM_RESCODE_DIV_NUM, 0x40),
};

static const struct qmp_phy_init_tbl msm8996_pcie_tx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_TX_HIGHZ_TRANSCEIVEREN_BIAS_DRVR_EN, 0x45),
	QMP_PHY_INIT_CFG(QSERDES_TX_LANE_MODE, 0x06),
};

static const struct qmp_phy_init_tbl msm8996_pcie_rx_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_ENABLES, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL3, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_EQU_ADAPTOR_CNTRL4, 0xdb),
	QMP_PHY_INIT_CFG(QSERDES_RX_RX_BAND, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_GAIN_HALF, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_RX_UCDR_SO_SATURATION_AND_ENABLE, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_DEGLITCH_CNTRL, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_RX_SIGDET_LVL, 0x19),
};

static const struct qmp_phy_init_tbl msm8996_pcie_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_RX_IDLE_DTCT_CNTRL, 0x4c),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_PWRUP_RESET_DLY_TIME_AUXCLK, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_LP_WAKEUP_DLY_TIME_AUXCLK, 0x01),

	QMP_PHY_INIT_CFG(QPHY_V2_PCS_PLL_LOCK_CHK_DLY_TIME, 0x05),

	QMP_PHY_INIT_CFG(QPHY_V2_PCS_ENDPOINT_REFCLK_DRIVE, 0x05),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_POWER_DOWN_CONTROL, 0x02),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_POWER_STATE_CONFIG4, 0x00),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_POWER_STATE_CONFIG1, 0xa3),
	QMP_PHY_INIT_CFG(QPHY_V2_PCS_TXDEEMPH_M3P5DB_V0, 0x0e),
};

/* struct qmp_phy_cfg - per-PHY initialization config */
struct qmp_phy_cfg {
	/* number of PHYs provided by this block */
	int num_phys;

	/* Init sequence for PHY blocks - serdes, tx, rx, pcs */
	const struct qmp_phy_init_tbl *serdes_tbl;
	int serdes_tbl_num;
	const struct qmp_phy_init_tbl *tx_tbl;
	int tx_tbl_num;
	const struct qmp_phy_init_tbl *rx_tbl;
	int rx_tbl_num;
	const struct qmp_phy_init_tbl *pcs_tbl;
	int pcs_tbl_num;

	/* clock ids to be requested */
	const char * const *clk_list;
	int num_clks;
	/* resets to be requested */
	const char * const *reset_list;
	int num_resets;
	/* regulators to be requested */
	const char * const *vreg_list;
	int num_vregs;

	/* array of registers with different offsets */
	const unsigned int *regs;

	unsigned int start_ctrl;
	unsigned int pwrdn_ctrl;
	unsigned int mask_com_pcs_ready;
	/* bit offset of PHYSTATUS in QPHY_PCS_STATUS register */
	unsigned int phy_status;

	/* true, if PHY needs delay after POWER_DOWN */
	bool has_pwrdn_delay;
	/* power_down delay in usec */
	int pwrdn_delay_min;
	int pwrdn_delay_max;
};

/**
 * struct qmp_phy - per-lane phy descriptor
 *
 * @phy: generic phy
 * @cfg: phy specific configuration
 * @serdes: iomapped memory space for phy's serdes (i.e. PLL)
 * @tx: iomapped memory space for lane's tx
 * @rx: iomapped memory space for lane's rx
 * @pcs: iomapped memory space for lane's pcs
 * @pipe_clk: pipe clock
 * @index: lane index
 * @qmp: QMP phy to which this lane belongs
 * @lane_rst: lane's reset controller
 */
struct qmp_phy {
	struct phy *phy;
	const struct qmp_phy_cfg *cfg;
	void __iomem *serdes;
	void __iomem *tx;
	void __iomem *rx;
	void __iomem *pcs;
	struct clk *pipe_clk;
	unsigned int index;
	struct qcom_qmp *qmp;
	struct reset_control *lane_rst;
};

/**
 * struct qcom_qmp - structure holding QMP phy block attributes
 *
 * @dev: device
 *
 * @clks: array of clocks required by phy
 * @resets: array of resets required by phy
 * @vregs: regulator supplies bulk data
 *
 * @phys: array of per-lane phy descriptors
 * @phy_mutex: mutex lock for PHY common block initialization
 * @init_count: phy common block initialization count
 */
struct qcom_qmp {
	struct device *dev;

	struct clk_bulk_data *clks;
	struct reset_control_bulk_data *resets;
	struct regulator_bulk_data *vregs;

	struct qmp_phy **phys;

	struct mutex phy_mutex;
	int init_count;
};

static inline void qphy_setbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg |= val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

static inline void qphy_clrbits(void __iomem *base, u32 offset, u32 val)
{
	u32 reg;

	reg = readl(base + offset);
	reg &= ~val;
	writel(reg, base + offset);

	/* ensure that above write is through */
	readl(base + offset);
}

/* list of clocks required by phy */
static const char * const msm8996_phy_clk_l[] = {
	"aux", "cfg_ahb", "ref",
};

/* list of resets */
static const char * const msm8996_pciephy_reset_l[] = {
	"phy", "common", "cfg",
};

/* list of regulators */
static const char * const qmp_phy_vreg_l[] = {
	"vdda-phy", "vdda-pll",
};

static const struct qmp_phy_cfg msm8996_pciephy_cfg = {
	.num_phys		= 3,

	.serdes_tbl		= msm8996_pcie_serdes_tbl,
	.serdes_tbl_num		= ARRAY_SIZE(msm8996_pcie_serdes_tbl),
	.tx_tbl			= msm8996_pcie_tx_tbl,
	.tx_tbl_num		= ARRAY_SIZE(msm8996_pcie_tx_tbl),
	.rx_tbl			= msm8996_pcie_rx_tbl,
	.rx_tbl_num		= ARRAY_SIZE(msm8996_pcie_rx_tbl),
	.pcs_tbl		= msm8996_pcie_pcs_tbl,
	.pcs_tbl_num		= ARRAY_SIZE(msm8996_pcie_pcs_tbl),
	.clk_list		= msm8996_phy_clk_l,
	.num_clks		= ARRAY_SIZE(msm8996_phy_clk_l),
	.reset_list		= msm8996_pciephy_reset_l,
	.num_resets		= ARRAY_SIZE(msm8996_pciephy_reset_l),
	.vreg_list		= qmp_phy_vreg_l,
	.num_vregs		= ARRAY_SIZE(qmp_phy_vreg_l),
	.regs			= pciephy_regs_layout,

	.start_ctrl		= PCS_START | PLL_READY_GATE_EN,
	.pwrdn_ctrl		= SW_PWRDN | REFCLK_DRV_DSBL,
	.mask_com_pcs_ready	= PCS_READY,
	.phy_status		= PHYSTATUS,

	.has_pwrdn_delay	= true,
	.pwrdn_delay_min	= POWER_DOWN_DELAY_US_MIN,
	.pwrdn_delay_max	= POWER_DOWN_DELAY_US_MAX,
};

static void qmp_pcie_msm8996_configure_lane(void __iomem *base,
					const unsigned int *regs,
					const struct qmp_phy_init_tbl tbl[],
					int num,
					u8 lane_mask)
{
	int i;
	const struct qmp_phy_init_tbl *t = tbl;

	if (!t)
		return;

	for (i = 0; i < num; i++, t++) {
		if (!(t->lane_mask & lane_mask))
			continue;

		if (t->in_layout)
			writel(t->val, base + regs[t->offset]);
		else
			writel(t->val, base + t->offset);
	}
}

static void qmp_pcie_msm8996_configure(void __iomem *base,
				   const unsigned int *regs,
				   const struct qmp_phy_init_tbl tbl[],
				   int num)
{
	qmp_pcie_msm8996_configure_lane(base, regs, tbl, num, 0xff);
}

static int qmp_pcie_msm8996_serdes_init(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *serdes = qphy->serdes;
	const struct qmp_phy_init_tbl *serdes_tbl = cfg->serdes_tbl;
	int serdes_tbl_num = cfg->serdes_tbl_num;
	void __iomem *status;
	unsigned int mask, val;
	int ret;

	qmp_pcie_msm8996_configure(serdes, cfg->regs, serdes_tbl, serdes_tbl_num);

	qphy_clrbits(serdes, cfg->regs[QPHY_COM_SW_RESET], SW_RESET);
	qphy_setbits(serdes, cfg->regs[QPHY_COM_START_CONTROL],
		     SERDES_START | PCS_START);

	status = serdes + cfg->regs[QPHY_COM_PCS_READY_STATUS];
	mask = cfg->mask_com_pcs_ready;

	ret = readl_poll_timeout(status, val, (val & mask), 10,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev,
			"phy common block init timed-out\n");
		return ret;
	}

	return 0;
}

static int qmp_pcie_msm8996_com_init(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *serdes = qphy->serdes;
	int ret;

	mutex_lock(&qmp->phy_mutex);
	if (qmp->init_count++) {
		mutex_unlock(&qmp->phy_mutex);
		return 0;
	}

	/* turn on regulator supplies */
	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators, err=%d\n", ret);
		goto err_decrement_count;
	}

	ret = reset_control_bulk_assert(cfg->num_resets, qmp->resets);
	if (ret) {
		dev_err(qmp->dev, "reset assert failed\n");
		goto err_disable_regulators;
	}

	ret = reset_control_bulk_deassert(cfg->num_resets, qmp->resets);
	if (ret) {
		dev_err(qmp->dev, "reset deassert failed\n");
		goto err_disable_regulators;
	}

	ret = clk_bulk_prepare_enable(cfg->num_clks, qmp->clks);
	if (ret)
		goto err_assert_reset;

	qphy_setbits(serdes, cfg->regs[QPHY_COM_POWER_DOWN_CONTROL],
		     SW_PWRDN);

	mutex_unlock(&qmp->phy_mutex);

	return 0;

err_assert_reset:
	reset_control_bulk_assert(cfg->num_resets, qmp->resets);
err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
err_decrement_count:
	qmp->init_count--;
	mutex_unlock(&qmp->phy_mutex);

	return ret;
}

static int qmp_pcie_msm8996_com_exit(struct qmp_phy *qphy)
{
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *serdes = qphy->serdes;

	mutex_lock(&qmp->phy_mutex);
	if (--qmp->init_count) {
		mutex_unlock(&qmp->phy_mutex);
		return 0;
	}

	qphy_setbits(serdes, cfg->regs[QPHY_COM_START_CONTROL],
		     SERDES_START | PCS_START);
	qphy_clrbits(serdes, cfg->regs[QPHY_COM_SW_RESET],
		     SW_RESET);
	qphy_setbits(serdes, cfg->regs[QPHY_COM_POWER_DOWN_CONTROL],
		     SW_PWRDN);

	reset_control_bulk_assert(cfg->num_resets, qmp->resets);

	clk_bulk_disable_unprepare(cfg->num_clks, qmp->clks);

	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);

	mutex_unlock(&qmp->phy_mutex);

	return 0;
}

static int qmp_pcie_msm8996_init(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	int ret;
	dev_vdbg(qmp->dev, "Initializing QMP phy\n");

	ret = qmp_pcie_msm8996_com_init(qphy);
	if (ret)
		return ret;

	return 0;
}

static int qmp_pcie_msm8996_power_on(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	struct qcom_qmp *qmp = qphy->qmp;
	const struct qmp_phy_cfg *cfg = qphy->cfg;
	void __iomem *tx = qphy->tx;
	void __iomem *rx = qphy->rx;
	void __iomem *pcs = qphy->pcs;
	void __iomem *status;
	unsigned int mask, val, ready;
	int ret;

	qmp_pcie_msm8996_serdes_init(qphy);

	ret = reset_control_deassert(qphy->lane_rst);
	if (ret) {
		dev_err(qmp->dev, "lane%d reset deassert failed\n",
			qphy->index);
		return ret;
	}

	ret = clk_prepare_enable(qphy->pipe_clk);
	if (ret) {
		dev_err(qmp->dev, "pipe_clk enable failed err=%d\n", ret);
		goto err_reset_lane;
	}

	/* Tx, Rx, and PCS configurations */
	qmp_pcie_msm8996_configure_lane(tx, cfg->regs, cfg->tx_tbl,
					cfg->tx_tbl_num, 1);

	qmp_pcie_msm8996_configure_lane(rx, cfg->regs, cfg->rx_tbl,
					cfg->rx_tbl_num, 1);

	qmp_pcie_msm8996_configure(pcs, cfg->regs, cfg->pcs_tbl, cfg->pcs_tbl_num);

	/*
	 * Pull out PHY from POWER DOWN state.
	 * This is active low enable signal to power-down PHY.
	 */
	qphy_setbits(pcs, QPHY_V2_PCS_POWER_DOWN_CONTROL, cfg->pwrdn_ctrl);

	if (cfg->has_pwrdn_delay)
		usleep_range(cfg->pwrdn_delay_min, cfg->pwrdn_delay_max);

	/* Pull PHY out of reset state */
	qphy_clrbits(pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* start SerDes and Phy-Coding-Sublayer */
	qphy_setbits(pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	status = pcs + cfg->regs[QPHY_PCS_STATUS];
	mask = cfg->phy_status;
	ready = 0;

	ret = readl_poll_timeout(status, val, (val & mask) == ready, 10,
				 PHY_INIT_COMPLETE_TIMEOUT);
	if (ret) {
		dev_err(qmp->dev, "phy initialization timed-out\n");
		goto err_disable_pipe_clk;
	}

	return 0;

err_disable_pipe_clk:
	clk_disable_unprepare(qphy->pipe_clk);
err_reset_lane:
	reset_control_assert(qphy->lane_rst);

	return ret;
}

static int qmp_pcie_msm8996_power_off(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qphy->cfg;

	clk_disable_unprepare(qphy->pipe_clk);

	/* PHY reset */
	qphy_setbits(qphy->pcs, cfg->regs[QPHY_SW_RESET], SW_RESET);

	/* stop SerDes and Phy-Coding-Sublayer */
	qphy_clrbits(qphy->pcs, cfg->regs[QPHY_START_CTRL], cfg->start_ctrl);

	/* Put PHY into POWER DOWN state: active low */
	if (cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL]) {
		qphy_clrbits(qphy->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
			     cfg->pwrdn_ctrl);
	} else {
		qphy_clrbits(qphy->pcs, QPHY_V2_PCS_POWER_DOWN_CONTROL,
				cfg->pwrdn_ctrl);
	}

	return 0;
}

static int qmp_pcie_msm8996_exit(struct phy *phy)
{
	struct qmp_phy *qphy = phy_get_drvdata(phy);

	reset_control_assert(qphy->lane_rst);

	qmp_pcie_msm8996_com_exit(qphy);

	return 0;
}

static int qmp_pcie_msm8996_enable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_msm8996_init(phy);
	if (ret)
		return ret;

	ret = qmp_pcie_msm8996_power_on(phy);
	if (ret)
		qmp_pcie_msm8996_exit(phy);

	return ret;
}

static int qmp_pcie_msm8996_disable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_msm8996_power_off(phy);
	if (ret)
		return ret;
	return qmp_pcie_msm8996_exit(phy);
}

static int qmp_pcie_msm8996_vreg_init(struct device *dev, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int num = cfg->num_vregs;
	int i;

	qmp->vregs = devm_kcalloc(dev, num, sizeof(*qmp->vregs), GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->vregs[i].supply = cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, qmp->vregs);
}

static int qmp_pcie_msm8996_reset_init(struct device *dev, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int i;
	int ret;

	qmp->resets = devm_kcalloc(dev, cfg->num_resets,
				   sizeof(*qmp->resets), GFP_KERNEL);
	if (!qmp->resets)
		return -ENOMEM;

	for (i = 0; i < cfg->num_resets; i++)
		qmp->resets[i].id = cfg->reset_list[i];

	ret = devm_reset_control_bulk_get_exclusive(dev, cfg->num_resets, qmp->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get resets\n");

	return 0;
}

static int qmp_pcie_msm8996_clk_init(struct device *dev, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	int num = cfg->num_clks;
	int i;

	qmp->clks = devm_kcalloc(dev, num, sizeof(*qmp->clks), GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		qmp->clks[i].id = cfg->clk_list[i];

	return devm_clk_bulk_get(dev, num, qmp->clks);
}

static void phy_clk_release_provider(void *res)
{
	of_clk_del_provider(res);
}

/*
 * Register a fixed rate pipe clock.
 *
 * The <s>_pipe_clksrc generated by PHY goes to the GCC that gate
 * controls it. The <s>_pipe_clk coming out of the GCC is requested
 * by the PHY driver for its operations.
 * We register the <s>_pipe_clksrc here. The gcc driver takes care
 * of assigning this <s>_pipe_clksrc as parent to <s>_pipe_clk.
 * Below picture shows this relationship.
 *
 *         +---------------+
 *         |   PHY block   |<<---------------------------------------+
 *         |               |                                         |
 *         |   +-------+   |                   +-----+               |
 *   I/P---^-->|  PLL  |---^--->pipe_clksrc--->| GCC |--->pipe_clk---+
 *    clk  |   +-------+   |                   +-----+
 *         +---------------+
 */
static int phy_pipe_clk_register(struct qcom_qmp *qmp, struct device_node *np)
{
	struct clk_fixed_rate *fixed;
	struct clk_init_data init = { };
	int ret;

	ret = of_property_read_string(np, "clock-output-names", &init.name);
	if (ret) {
		dev_err(qmp->dev, "%pOFn: No clock-output-names\n", np);
		return ret;
	}

	fixed = devm_kzalloc(qmp->dev, sizeof(*fixed), GFP_KERNEL);
	if (!fixed)
		return -ENOMEM;

	init.ops = &clk_fixed_rate_ops;

	/* controllers using QMP phys use 125MHz pipe clock interface */
	fixed->fixed_rate = 125000000;
	fixed->hw.init = &init;

	ret = devm_clk_hw_register(qmp->dev, &fixed->hw);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, &fixed->hw);
	if (ret)
		return ret;

	/*
	 * Roll a devm action because the clock provider is the child node, but
	 * the child node is not actually a device.
	 */
	return devm_add_action_or_reset(qmp->dev, phy_clk_release_provider, np);
}

static const struct phy_ops qmp_pcie_msm8996_ops = {
	.power_on	= qmp_pcie_msm8996_enable,
	.power_off	= qmp_pcie_msm8996_disable,
	.owner		= THIS_MODULE,
};

static void qcom_qmp_reset_control_put(void *data)
{
	reset_control_put(data);
}

static int qmp_pcie_msm8996_create(struct device *dev, struct device_node *np, int id,
			void __iomem *serdes, const struct qmp_phy_cfg *cfg)
{
	struct qcom_qmp *qmp = dev_get_drvdata(dev);
	struct phy *generic_phy;
	struct qmp_phy *qphy;
	int ret;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->cfg = cfg;
	qphy->serdes = serdes;
	/*
	 * Get memory resources for each phy lane:
	 * Resources are indexed as: tx -> 0; rx -> 1; pcs -> 2.
	 */
	qphy->tx = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(qphy->tx))
		return PTR_ERR(qphy->tx);

	qphy->rx = devm_of_iomap(dev, np, 1, NULL);
	if (IS_ERR(qphy->rx))
		return PTR_ERR(qphy->rx);

	qphy->pcs = devm_of_iomap(dev, np, 2, NULL);
	if (IS_ERR(qphy->pcs))
		return PTR_ERR(qphy->pcs);

	qphy->pipe_clk = devm_get_clk_from_child(dev, np, NULL);
	if (IS_ERR(qphy->pipe_clk)) {
		return dev_err_probe(dev, PTR_ERR(qphy->pipe_clk),
				     "failed to get lane%d pipe clock\n", id);
	}

	qphy->lane_rst = of_reset_control_get_exclusive_by_index(np, 0);
	if (IS_ERR(qphy->lane_rst)) {
		dev_err(dev, "failed to get lane%d reset\n", id);
		return PTR_ERR(qphy->lane_rst);
	}
	ret = devm_add_action_or_reset(dev, qcom_qmp_reset_control_put,
				       qphy->lane_rst);
	if (ret)
		return ret;

	generic_phy = devm_phy_create(dev, np, &qmp_pcie_msm8996_ops);
	if (IS_ERR(generic_phy)) {
		ret = PTR_ERR(generic_phy);
		dev_err(dev, "failed to create qphy %d\n", ret);
		return ret;
	}

	qphy->phy = generic_phy;
	qphy->index = id;
	qphy->qmp = qmp;
	qmp->phys[id] = qphy;
	phy_set_drvdata(generic_phy, qphy);

	return 0;
}

static const struct of_device_id qmp_pcie_msm8996_of_match_table[] = {
	{
		.compatible = "qcom,msm8996-qmp-pcie-phy",
		.data = &msm8996_pciephy_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, qmp_pcie_msm8996_of_match_table);

static int qmp_pcie_msm8996_probe(struct platform_device *pdev)
{
	struct qcom_qmp *qmp;
	struct device *dev = &pdev->dev;
	struct device_node *child;
	struct phy_provider *phy_provider;
	void __iomem *serdes;
	const struct qmp_phy_cfg *cfg = NULL;
	int num, id, expected_phys;
	int ret;

	qmp = devm_kzalloc(dev, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	qmp->dev = dev;
	dev_set_drvdata(dev, qmp);

	/* Get the specific init parameters of QMP phy */
	cfg = of_device_get_match_data(dev);
	if (!cfg)
		return -EINVAL;

	/* per PHY serdes; usually located at base address */
	serdes = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(serdes))
		return PTR_ERR(serdes);

	expected_phys = cfg->num_phys;

	mutex_init(&qmp->phy_mutex);

	ret = qmp_pcie_msm8996_clk_init(dev, cfg);
	if (ret)
		return ret;

	ret = qmp_pcie_msm8996_reset_init(dev, cfg);
	if (ret)
		return ret;

	ret = qmp_pcie_msm8996_vreg_init(dev, cfg);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get regulator supplies\n");

	num = of_get_available_child_count(dev->of_node);
	/* do we have a rogue child node ? */
	if (num > expected_phys)
		return -EINVAL;

	qmp->phys = devm_kcalloc(dev, num, sizeof(*qmp->phys), GFP_KERNEL);
	if (!qmp->phys)
		return -ENOMEM;

	id = 0;
	for_each_available_child_of_node(dev->of_node, child) {
		/* Create per-lane phy */
		ret = qmp_pcie_msm8996_create(dev, child, id, serdes, cfg);
		if (ret) {
			dev_err(dev, "failed to create lane%d phy, %d\n",
				id, ret);
			goto err_node_put;
		}

		/*
		 * Register the pipe clock provided by phy.
		 * See function description to see details of this pipe clock.
		 */
		ret = phy_pipe_clk_register(qmp, child);
		if (ret) {
			dev_err(qmp->dev,
				"failed to register pipe clock source\n");
			goto err_node_put;
		}

		id++;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);

err_node_put:
	of_node_put(child);
	return ret;
}

static struct platform_driver qmp_pcie_msm8996_driver = {
	.probe		= qmp_pcie_msm8996_probe,
	.driver = {
		.name	= "qcom-qmp-msm8996-pcie-phy",
		.of_match_table = qmp_pcie_msm8996_of_match_table,
	},
};

module_platform_driver(qmp_pcie_msm8996_driver);

MODULE_AUTHOR("Vivek Gautam <vivek.gautam@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm QMP MSM8996 PCIe PHY driver");
MODULE_LICENSE("GPL v2");
