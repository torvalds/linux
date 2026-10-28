// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Copyright (c) 2026 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "phy-mtk-io.h"

/* PHY System Interface (SIF) registers */
#define PEXTP_DIG_GLB_TOP				0x20
#  define RG_XTP_BYPASS_PIPE_RST_RC			BIT(17)
#define PEXTP_DIG_GLB_CKBG0				0x30
#  define RG_XTP_CKBG_XTAL_STABLE_TIME_SEL		GENMASK(25, 16)
#define PEXTP_DIG_GLB_TPLL_CTL0				0x38
#  define RG_XTP_TPLL_SET_STABLE_TIME_SEL		GENMASK(7, 2)
#  define RG_XTP_TPLL_PWE_ON_STABLE_TIME_SEL		GENMASK(9, 8)
#define PEXTP_DIG_GLB_CLKREQ_CTL			0x50
#  define RG_XTP_CKM_EN_L1S0				BIT(13)
#  define RG_XTP_CKM_EN_L1S1				BIT(14)
#define PEXTP_DIG_GLB_TPLL_CTL2				0xf4
#  define RG_XTP_TPLL_ISO_EN_STABLE_TIME_SEL		GENMASK(13, 12)

/* PHY System Interface Digital registers */
#define PEXTP_DIG_LN_TRX_PIPE_IF_17			0x30e8
#  define RG_XTP_LN_RX_LF_CTLE_CSEL_GEN4		GENMASK(14, 12)
#define PEXTP_DIG_LN_RX_F0				0x50f0
#  define RG_XTP_LN_RX_GEN1_CTLE1_CSEL			GENMASK(3, 0)
#  define RG_XTP_LN_RX_GEN2_CTLE1_CSEL			GENMASK(7, 4)
#  define RG_XTP_LN_RX_GEN3_CTLE1_CSEL			GENMASK(11, 8)
#  define RG_XTP_LN_RX_GEN4_CTLE1_CSEL			GENMASK(15, 12)
#define PEXTP_DIG_LN_RX2_AEQ_EDGE_0			0x6004
#  define RG_XTP_LN_RX_AEQ_EGEQ_RATIO_GEN3		GENMASK(21, 16)
#  define RG_XTP_LN_RX_AEQ_EGEQ_RATIO_GEN4		GENMASK(29, 24)

/* PHY System Interface Analog registers */
#define PEXTP_ANA_GLB_TPLL1_RSVD			0x902c
#  define RG_XTP_GLB_TPLL1_P_PATH_GAIN			GENMASK(2, 0)
#define PEXTP_ANA_GLB_BIAS_0				0x9060
#  define RG_XTP_GLB_BIAS_INTR_CTRL			GENMASK(5, 0)
#define PEXTP_ANA_GLB_BIAS_1				0x90c0
#  define RG_XTP_GLB_BIAS_V2V_VTRIM			GENMASK(9, 6)
#define PEXTP_ANA_LN_TRX_0C				0xa00c
#  define RG_XTP_LN_TX_RSWN_IMPSEL			GENMASK(20, 16)
#define PEXTP_ANA_LN_TRX_34				0xa034
#  define RG_XTP_LN_RX_FE				BIT(15)
#define PEXTP_ANA_LN_TRX_6C				0xa06c
#  define RG_XTP_LN_RX_AEQ_CTLE_ERR_TYPE		GENMASK(14, 13)
#   define AEQ_CTLE_SEARCH_ERR_TYPE_H1P5		0
#   define AEQ_CTLE_SEARCH_ERR_TYPE_H1P5_H2P5		1
#   define AEQ_CTLE_SEARCH_ERR_TYPE_P1P5_H2P5_H3P5	2
#define PEXTP_ANA_LN_TRX_A0				0xa0a0
#  define RG_XTP_LN_TX_IMPSEL_PMOS			GENMASK(4, 0)
#  define RG_XTP_LN_TX_IMPSEL_NMOS			GENMASK(11, 7)
#  define RG_XTP_LN_RX_IMPSEL				GENMASK(15, 12)
#define PEXTP_ANA_LN_TRX_A8				0xa0a8
#  define RG_XTP_LN_RX_LEQ_RL_CTLE_CAL			GENMASK(6, 2)
#  define RG_XTP_LN_RX_LEQ_RL_VGA_CAL			GENMASK(11, 7)
#  define RG_XTP_LN_RX_LEQ_RL_DFE_CAL			GENMASK(23, 19)
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_4			0xb004
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_8			0xb008
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_C			0xb00c
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_10		0xb010
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_14		0xb014
#define PEXTP_DIG_LN_TX_LC_TABLE_RSWN_18		0xb018
#  define RG_XTP_LN_TX_LC_PRESET_MGx_Px_CM1		GENMASK(5, 0)
#  define RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0		GENMASK(13, 8)
#  define RG_XTP_LN_TX_LC_PRESET_MGx_Px_CP1		GENMASK(21, 16)
#define PEXTP_REG_LANE(x)				((x) * 0x100)

/* PHY Clock Management (CKM) registers */
#define XTP_CKM_FORCE_6					0x38
#  define RG_CKM_BIAS_WAIT_PRD_US			GENMASK(21, 16)
#define XTP_CKM_REG_SPLL_FBKDIV_5			0xd4
#  define RG_CKM_CKTX_IMPSEL_PMOS			GENMASK(19, 16)
#  define RG_CKM_CKTX_IMPSEL_NMOS			GENMASK(23, 20)
#  define RG_CKM_CKTX_IMPSEL_SW				GENMASK(27, 24)

/* Calibration data from eFuses */
#define MTK_PCIE_SPHY_CALIBRATION_MAX_DATA_LANES	2
#define MTK_PCIE_SPHY_CALIBRATION_LAST_QUIRK_VER	4

/**
 * struct mtk_pcie_sphy_imp_sel - Impedance Selection parameters
 * @pmos: Impedance selection for P-Channel MOSFET
 * @nmos: Impedance selection for N-Channel MOSFET
 */
struct mtk_pcie_sphy_imp_sel {
	u8 pmos;
	u8 nmos;
};

/**
 * struct mtk_pcie_sphy_efuse - eFuse calibration data for S-PHY
 * @cktx_impsel:    SPLL CKTX Impedance Selection (P and N MOSFET)
 * @tx_impsel:      TX Impedance Selection (P and N MOSFET)
 * @int_r_ctrl:     Internal resistor selection of TX Bias Current
 * @xtp_vtrim:      XTP Bias V2V voltage calibration
 * @cktx_r_mid:     SPLL CKTX Intermediate Transition Impedance (Rmid)
 * @rx_leq_rl_ctle: RX Front-End Return Loss Continuous Time Linear Equalization value
 * @rx_leq_rl_vga:  RX Front-End Return Loss Variable Gain Amplifier value
 * @rx_leq_rl_dfe:  RX Front-End Return Loss Decision Feedback Equalization value
 * @rx_impsel:      RX Impedance Selection
 * @tx_rswn_impsel: TX RSWn (Switch Resistance) impedance selection
 * @supported:      eFuse calibration data is supported
 */
struct mtk_pcie_sphy_efuse {
	struct mtk_pcie_sphy_imp_sel cktx_impsel;
	struct mtk_pcie_sphy_imp_sel tx_impsel;
	u8 int_r_ctrl;
	u8 xtp_vtrim;
	u8 cktx_r_mid;
	u8 rx_leq_rl_ctle;
	u8 rx_leq_rl_vga;
	u8 rx_leq_rl_dfe;
	u8 rx_impsel;
	u8 tx_rswn_impsel[MTK_PCIE_SPHY_CALIBRATION_MAX_DATA_LANES];
	bool supported;
};

/**
 * struct mtk_pcie_sphy - PCI-Express S-PHY driver main structure
 * @dev:         Pointer to device structure
 * @phy:         Pointer to generic phy structure
 * @sif_base:    IO mapped register base address of system interface
 * @ckm_base:    IO mapped register base address of clock management interface
 * @num_lanes:   Number of lanes
 * @calibration: eFuse calibration data for S-PHY
 */
struct mtk_pcie_sphy {
	struct device *dev;
	struct phy *phy;
	void __iomem *sif_base;
	void __iomem *ckm_base;
	u8 num_lanes;
	struct mtk_pcie_sphy_efuse calibration;
};

static void mtk_pcie_sphy_apply_calibration(struct mtk_pcie_sphy *pcie_sphy)
{
	struct mtk_pcie_sphy_efuse *cal = &pcie_sphy->calibration;
	int i;

	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_ANA_GLB_BIAS_0,
			     RG_XTP_GLB_BIAS_INTR_CTRL, cal->int_r_ctrl);

	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_ANA_GLB_BIAS_1,
			     RG_XTP_GLB_BIAS_V2V_VTRIM, cal->xtp_vtrim);

	mtk_phy_update_field(pcie_sphy->ckm_base + XTP_CKM_REG_SPLL_FBKDIV_5,
			     RG_CKM_CKTX_IMPSEL_PMOS, cal->cktx_impsel.pmos);

	mtk_phy_update_field(pcie_sphy->ckm_base + XTP_CKM_REG_SPLL_FBKDIV_5,
			     RG_CKM_CKTX_IMPSEL_NMOS, cal->cktx_impsel.nmos);

	mtk_phy_update_field(pcie_sphy->ckm_base + XTP_CKM_REG_SPLL_FBKDIV_5,
			     RG_CKM_CKTX_IMPSEL_SW, cal->cktx_r_mid);

	for (i = 0; i < pcie_sphy->num_lanes; i++) {
		void __iomem *sif_lane_base = pcie_sphy->sif_base + PEXTP_REG_LANE(i);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_0C,
				     RG_XTP_LN_TX_RSWN_IMPSEL, cal->tx_rswn_impsel[i]);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A8,
				     RG_XTP_LN_RX_LEQ_RL_CTLE_CAL, cal->rx_leq_rl_ctle);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A8,
				     RG_XTP_LN_RX_LEQ_RL_VGA_CAL, cal->rx_leq_rl_vga);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A8,
				     RG_XTP_LN_RX_LEQ_RL_DFE_CAL, cal->rx_leq_rl_dfe);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A0,
				     RG_XTP_LN_RX_IMPSEL, cal->rx_impsel);
	}
}

/**
 * mtk_pcie_sphy_init() - Initialize the PCI-Express S-PHY
 * @phy: the phy to be initialized
 *
 * The hardware settings will be reset during suspend, it should be
 * reinitialized when the consumer calls phy_init() again on resume.
 */
static int mtk_pcie_sphy_init(struct phy *phy)
{
	struct mtk_pcie_sphy *pcie_sphy = phy_get_drvdata(phy);
	struct mtk_pcie_sphy_imp_sel tx_impsel;
	int i;

	/* Set CKM Bias wait time to 4 microseconds */
	mtk_phy_update_field(pcie_sphy->ckm_base + XTP_CKM_FORCE_6,
			     RG_CKM_BIAS_WAIT_PRD_US, 4);

	/* TPLL needs 63 ref_ck ticks to stabilize when setting frequency */
	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_DIG_GLB_TPLL_CTL0,
			     RG_XTP_TPLL_SET_STABLE_TIME_SEL, 63);

	/* TPLL needs 3 ref_ck ticks to stabilize when powering on... */
	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_DIG_GLB_TPLL_CTL0,
			     RG_XTP_TPLL_PWE_ON_STABLE_TIME_SEL, 3);

	/* ...and the same goes for setting isolation */
	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_DIG_GLB_TPLL_CTL2,
			     RG_XTP_TPLL_ISO_EN_STABLE_TIME_SEL, 3);

	/* XTAL doesn't need any stabilization time */
	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_DIG_GLB_CKBG0,
			     RG_XTP_CKBG_XTAL_STABLE_TIME_SEL, 0);

	/* Keep pextp_ckm enabled when in L1SS_L1S1 state */
	mtk_phy_clear_bits(pcie_sphy->sif_base + PEXTP_DIG_GLB_CLKREQ_CTL, RG_XTP_CKM_EN_L1S1);

	/* Set PIPE to reset TPLL */
	mtk_phy_clear_bits(pcie_sphy->sif_base + PEXTP_DIG_GLB_TOP, RG_XTP_BYPASS_PIPE_RST_RC);

	/* Set TPLL P-Path gain compensation to 1 */
	mtk_phy_update_field(pcie_sphy->sif_base + PEXTP_ANA_GLB_TPLL1_RSVD,
			     RG_XTP_GLB_TPLL1_P_PATH_GAIN, 1);

	for (i = 0; i < pcie_sphy->num_lanes; i++) {
		void __iomem *sif_lane_base = pcie_sphy->sif_base + PEXTP_REG_LANE(i);

		/* Set RX Lane AEQ CTRL-E Search Error type to h1.5 + h2.5 */
		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_6C,
				     RG_XTP_LN_RX_AEQ_CTLE_ERR_TYPE,
				     AEQ_CTLE_SEARCH_ERR_TYPE_H1P5_H2P5);

		mtk_phy_set_bits(sif_lane_base + PEXTP_ANA_LN_TRX_34, RG_XTP_LN_RX_FE);

		/* TRX: Select CTLE1 for RX Lane AutoEQ CTRL-E Setting on Gen4 */
		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TRX_PIPE_IF_17,
				     RG_XTP_LN_RX_LF_CTLE_CSEL_GEN4, 1);

		/* Set RX Lane AutoEQ CTRL-E for PCI-Express Gen1 to Gen 4 */
		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX_F0,
				     RG_XTP_LN_RX_GEN1_CTLE1_CSEL, 13);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX_F0,
				     RG_XTP_LN_RX_GEN2_CTLE1_CSEL, 13);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX_F0,
				     RG_XTP_LN_RX_GEN3_CTLE1_CSEL, 13);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX_F0,
				     RG_XTP_LN_RX_GEN4_CTLE1_CSEL, 0);

		/* Set RX Lane AutoEQ's Edge EQ Ratio to 22 * 0.0625 = 1.375 */
		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX2_AEQ_EDGE_0,
				     RG_XTP_LN_RX_AEQ_EGEQ_RATIO_GEN3, 22);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_RX2_AEQ_EDGE_0,
				     RG_XTP_LN_RX_AEQ_EGEQ_RATIO_GEN4, 22);

		/* Setup Digital lane TX Link Characteristics Table */
		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_4,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 10);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_4,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_CP1, 2);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_8,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 11);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_8,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_CP1, 1);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_C,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 12);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_10,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 13);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_10,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_CM1, 1);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_14,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 11);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_14,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_CM1, 1);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_18,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_C0, 10);

		mtk_phy_update_field(sif_lane_base + PEXTP_DIG_LN_TX_LC_TABLE_RSWN_18,
				     RG_XTP_LN_TX_LC_PRESET_MGx_Px_CM1, 2);
	}

	if (pcie_sphy->calibration.supported) {
		mtk_pcie_sphy_apply_calibration(pcie_sphy);

		tx_impsel.pmos = pcie_sphy->calibration.tx_impsel.pmos;
		tx_impsel.nmos = pcie_sphy->calibration.tx_impsel.nmos;
	} else {
		/* Set P=10, N=9 to prevent EMI if no calibration present */
		tx_impsel.pmos = 10;
		tx_impsel.nmos = 9;
	}

	/* Select TX Impedance on N and P MOSFETs */
	for (i = 0; i < pcie_sphy->num_lanes; i++) {
		void __iomem *sif_lane_base = pcie_sphy->sif_base + PEXTP_REG_LANE(i);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A0,
				     RG_XTP_LN_TX_IMPSEL_PMOS, tx_impsel.pmos);

		mtk_phy_update_field(sif_lane_base + PEXTP_ANA_LN_TRX_A0,
				     RG_XTP_LN_TX_IMPSEL_NMOS, tx_impsel.nmos);
	}

	return 0;
}

static const struct phy_ops mtk_pcie_sphy_ops = {
	.init	= mtk_pcie_sphy_init,
	.owner	= THIS_MODULE,
};

static int mtk_pcie_sphy_get_one_cal_para(struct device *dev, const char *name, u8 max_val)
{
	u16 buf;
	u8 tmp;
	int ret;

	/*
	 * All of the calibrations are always max 8 bits long, but some may
	 * be split between two different 8-bits cells: handle this corner
	 * case by retrying reading as u16.
	 */
	ret = nvmem_cell_read_u8(dev, name, &tmp);
	if (ret == 0)
		buf = tmp;
	else
		ret = nvmem_cell_read_u16(dev, name, &buf);

	if (ret == -ENOENT) {
		dev_info(dev, "No calibration for %s. Using defaults\n", name);
		return -ENOENT;
	} else if (ret)
		return dev_err_probe(dev, ret,
				     "Cannot get calibration data for %s\n", name);

	if (buf > max_val)
		return dev_err_probe(dev, -ERANGE,
				     "Bad value %u retrieved for %s.\n", buf, name);

	return buf;
}

static int mtk_pcie_sphy_get_calibration_data(struct mtk_pcie_sphy *pcie_sphy)
{
	struct mtk_pcie_sphy_efuse *cal = &pcie_sphy->calibration;
	struct device *dev = pcie_sphy->dev;
	u8 version;
	int ret;

	if (pcie_sphy->num_lanes > MTK_PCIE_SPHY_CALIBRATION_MAX_DATA_LANES) {
		dev_info(dev, "Skipping PHY calibration for more than %u lanes.\n",
			 MTK_PCIE_SPHY_CALIBRATION_MAX_DATA_LANES);
		ret = -EOPNOTSUPP;
		goto end;
	}

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "int-r",
					     FIELD_MAX(RG_XTP_GLB_BIAS_INTR_CTRL));
	if (ret < 0)
		goto end;
	cal->int_r_ctrl = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "xtp-vtrim",
					     FIELD_MAX(RG_XTP_GLB_BIAS_V2V_VTRIM));
	if (ret < 0)
		goto end;
	cal->xtp_vtrim = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "cktx-pmos",
					     FIELD_MAX(RG_CKM_CKTX_IMPSEL_PMOS));
	if (ret < 0)
		goto end;
	cal->cktx_impsel.pmos = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "cktx-nmos",
					     FIELD_MAX(RG_CKM_CKTX_IMPSEL_NMOS));
	if (ret < 0)
		goto end;
	cal->cktx_impsel.nmos = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "cktx-r-mid",
					     FIELD_MAX(RG_CKM_CKTX_IMPSEL_SW));
	if (ret < 0)
		goto end;
	cal->cktx_r_mid = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "rxfe-lanes-rl-ctle",
					     FIELD_MAX(RG_XTP_LN_RX_LEQ_RL_CTLE_CAL));
	if (ret < 0)
		goto end;
	cal->rx_leq_rl_ctle = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "rxfe-lanes-rl-vga",
					     FIELD_MAX(RG_XTP_LN_RX_LEQ_RL_VGA_CAL));
	if (ret < 0)
		goto end;
	cal->rx_leq_rl_vga = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "rxfe-lanes-rl-dfe",
					     FIELD_MAX(RG_XTP_LN_RX_LEQ_RL_DFE_CAL));
	if (ret < 0)
		goto end;
	cal->rx_leq_rl_dfe = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "rx-lanes-imp",
					     FIELD_MAX(RG_XTP_LN_RX_IMPSEL));
	if (ret < 0)
		goto end;
	cal->rx_impsel = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "tx-lanes-pmos",
					     FIELD_MAX(RG_XTP_LN_TX_IMPSEL_PMOS));
	if (ret < 0)
		goto end;
	cal->tx_impsel.pmos = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "tx-lanes-nmos",
					     FIELD_MAX(RG_XTP_LN_TX_IMPSEL_NMOS));
	if (ret < 0)
		goto end;
	cal->tx_impsel.nmos = ret;

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "tx-ln0-rswn",
					     FIELD_MAX(RG_XTP_LN_TX_RSWN_IMPSEL));
	if (ret < 0)
		goto end;
	cal->tx_rswn_impsel[0] = ret;

	if (pcie_sphy->num_lanes == 2) {
		ret = mtk_pcie_sphy_get_one_cal_para(dev, "tx-ln1-rswn",
						     FIELD_MAX(RG_XTP_LN_TX_RSWN_IMPSEL));
		if (ret < 0)
			goto end;
		cal->tx_rswn_impsel[1] = ret;
	}

	ret = mtk_pcie_sphy_get_one_cal_para(dev, "cal-version", 15);
	if (ret < 0)
		goto end;
	version = ret;

	/* Quirk for eFuse calibration table versions 0 to 4 */
	if ((version <= MTK_PCIE_SPHY_CALIBRATION_LAST_QUIRK_VER) &&
	    cal->rx_leq_rl_ctle == 10) {
		cal->rx_leq_rl_vga = cal->rx_leq_rl_ctle;
		cal->rx_leq_rl_dfe = cal->rx_leq_rl_ctle;
	}

end:
	if (ret < 0) {
		/*
		 * If any of the calibration values is missing, or if there is
		 * no calibration at all in the eFuses, this is not a problem,
		 * as the PHY doesn't require one to actually work.
		 *
		 * Note that if even just one of the calibration values is
		 * missing it would not be safe to trust the correctness of
		 * the others as this may be a different eFuse layout.
		 */
		if (ret == -ENOENT || ret == -EOPNOTSUPP) {
			cal->supported = false;
			return 0;
		}
		return ret;
	};
	cal->supported = true;

	return 0;
}

static int mtk_pcie_sphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *provider;
	struct mtk_pcie_sphy *pcie_sphy;
	u32 num_lanes;
	int ret;

	pcie_sphy = devm_kzalloc(dev, sizeof(*pcie_sphy), GFP_KERNEL);
	if (!pcie_sphy)
		return -ENOMEM;

	pcie_sphy->sif_base = devm_platform_ioremap_resource_byname(pdev, "sif");
	if (IS_ERR(pcie_sphy->sif_base))
		return dev_err_probe(dev, PTR_ERR(pcie_sphy->sif_base),
				     "Failed to map phy-sif base\n");

	pcie_sphy->ckm_base = devm_platform_ioremap_resource_byname(pdev, "ckm");
	if (IS_ERR(pcie_sphy->ckm_base))
		return dev_err_probe(dev, PTR_ERR(pcie_sphy->ckm_base),
				     "Failed to map phy-ckm base\n");

	pcie_sphy->phy = devm_phy_create(dev, dev->of_node, &mtk_pcie_sphy_ops);
	if (IS_ERR(pcie_sphy->phy))
		return dev_err_probe(dev, PTR_ERR(pcie_sphy->phy),
				     "Failed to create PCIe phy\n");

	ret = of_property_read_u32(dev->of_node, "num-lanes", &num_lanes);
	if (ret)
		num_lanes = 1;
	else if (num_lanes > 4)
		return dev_err_probe(dev, -EINVAL, "Invalid number of lanes.\n");

	pcie_sphy->num_lanes = num_lanes;
	pcie_sphy->dev = dev;

	ret = mtk_pcie_sphy_get_calibration_data(pcie_sphy);
	if (ret)
		return ret;

	phy_set_drvdata(pcie_sphy->phy, pcie_sphy);

	pm_runtime_no_callbacks(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider),
				     "Could not register PCI-Express S-PHY\n");

	return 0;
}

static const struct of_device_id mtk_pcie_sphy_of_match[] = {
	{ .compatible = "mediatek,mt8196-pcie-sphy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_pcie_sphy_of_match);

static struct platform_driver mtk_pcie_sphy_driver = {
	.probe	= mtk_pcie_sphy_probe,
	.driver	= {
		.name = "mtk-pcie-sphy",
		.of_match_table = mtk_pcie_sphy_of_match,
	},
};
module_platform_driver(mtk_pcie_sphy_driver);

MODULE_DESCRIPTION("MediaTek PCIe SPHY driver");
MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_LICENSE("GPL");
