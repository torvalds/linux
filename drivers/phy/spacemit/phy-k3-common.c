// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/usb.h>

#include <dt-bindings/phy/phy.h>

#include "phy-k3-common.h"

/* PHY Registers */
#define PHY_VERSION			0x0

#define PHY_RESET_CFG			0x04

#define PHY_RESET_RXBUF_RST		BIT(0)
#define PHY_RESET_SOFT_RST_PCS		BIT(1)
#define PHY_RESET_SOFT_RST_AHB		BIT(2)
#define PHY_RESET_EN_SD_AFTER_LOCK	BIT(6)

#define PHY_CLK_CFG			0x08

#define PHY_CLK_PLL_READY		BIT(0)
#define PHY_CLK_TXCLK_INV		BIT(2)
#define PHY_CLK_RXCLK_EN		BIT(3)
#define PHY_CLK_TXCLK_EN		BIT(4)
#define PHY_CLK_PCLK_EN			BIT(5)
#define PHY_CLK_PIPE_PCLK_EN		BIT(6)
#define PHY_CLK_REFCLK_FREQ		GENMASK(10, 7)
#define PHY_CLK_REFCLK_24M		2
#define PHY_CLK_SW_INIT_DONE		BIT(11)
#define PHY_CLK_PU_SSC_OUT		BIT(23)

#define PHY_MODE_CFG			0x0C

#define PHY_MODE_PCIE_INT_EN		BIT(0)
#define PHY_MODE_LFPS_TPERIOD		GENMASK(9, 8)
#define PHY_MODE_LFPS_TPERIOD_USB	3

#define PHY_PU_SEL			0x40

#define PHY_PU_CFG_STATUS		BIT(9)
#define PHY_PU_OVRD_STATUS		BIT(10)

#define PHY_PU_CK_REG			0x54

#define PHY_PU_REFCLK_100		BIT(25)

#define PHY_PLL_REG1			0x58

#define PHY_PLL_FREF_SEL		GENMASK(15, 13)
#define PHY_PLL_FREF_24M		0x1
#define PHY_PLL_SSC_DEP_SEL		GENMASK(27, 24)
#define PHY_PLL_SSC_5000PPM		0xa
#define PHY_PLL_SSC_MODE		GENMASK(29, 28)
#define PHY_PLL_SSC_MODE_CENTER_SPREAD	0
#define PHY_PLL_SSC_MODE_UP_SPREAD	1
#define PHY_PLL_SSC_MODE_DOWN_SPREAD	2
#define PHY_PLL_SSC_MODE_DOWN_SPREAD1	3

#define PHY_PLL_REG2			0x5c

#define PHY_PLL_SEL_REF100		BIT(21)

/* PHY RX Register Definitions */
#define PHY_RX_REG_A			0x60

#define PHY_RX_REG0_MASK		GENMASK(7, 0)
#define PHY_RX_REG1_MASK		GENMASK(15, 8)
#define PHY_RX_REG2_MASK		GENMASK(23, 16)
#define PHY_RX_REG3_MASK		GENMASK(31, 24)

#define PHY_RX_REG0_RLOAD		BIT(4)
#define PHY_RX_REG1_RTERM		GENMASK(11, 8)
#define PHY_RX_REG1_RC_CALI		GENMASK(15, 12)
#define PHY_RX_REG2_CSEL		GENMASK(19, 16)
#define PHY_RX_REG2_FORCE_CSEL		BIT(20)
#define PHY_RX_REG2_PSEL		GENMASK(23, 21)
#define PHY_RX_REG3_I_LOAD		GENMASK(26, 24)
#define PHY_RX_REG3_SEL_CBOOST_CODE	BIT(27)
#define PHY_RX_REG3_ADJ_BIAS		GENMASK(29, 28)
#define PHY_RX_REG3_RDEG1		GENMASK(31, 30)

#define PHY_RX_REG_B			0x64

#define PHY_RX_REG4_MASK		GENMASK(7, 0)
#define PHY_RX_REG5_MASK		GENMASK(15, 8)
#define PHY_RX_REG6_MASK		GENMASK(23, 16)

#define PHY_RX_REGB_MASK		GENMASK(23, 0)

#define PHY_RX_REG4_RDEG2		GENMASK(2, 1)
#define PHY_RX_REG4_ENVOS		BIT(4)
#define PHY_RX_REG4_RTERM_SEL		BIT(5)
#define PHY_RX_REG4_MANUAL_CFG		BIT(7)
#define PHY_RX_REG5_RCELL_VCM		GENMASK(11, 8)
#define PHY_RX_REG5_RCELL_BIAS		GENMASK(15, 12)
#define PHY_RX_REG6_H1_REG		GENMASK(19, 16)
#define PHY_RX_REG6_ADAPT_GAIN		GENMASK(21, 20)
#define PHY_RX_REG6_BYPASS_ADPT		BIT(22)

#define PHY_ADPT_CFG0			0x140
#define PHY_ADPT_AFE_RST_OVRD_EN	BIT(1)
#define PHY_ADPT_AFE_RST_OVRD_VAL	BIT(4)

#define PHY_RXEQ_TIME			0xb4
#define PHY_RXEQ_TIME_OVRD_POST_C_SOC	BIT(21)
#define PHY_RXEQ_TIME_CFG_AMP_SOC	GENMASK(23, 22)
#define PHY_RXEQ_TIME_AMP_SOC_650M	0
#define PHY_RXEQ_TIME_AMP_SOC_800M	1
#define PHY_RXEQ_TIME_AMP_SOC_870M	2
#define PHY_RXEQ_TIME_AMP_SOC_900M	3
#define PHY_RXEQ_TIME_OVRD_AMP_SOC	BIT(24)

#define PCIE_PU_ADDR_CLK_CFG		0x0008
#define PHY_CLK_PLL_READY		BIT(0)
#define PCIE_INITAL_TIMER		GENMASK(6, 3)
#define CFG_INTERNAL_TIMER_ADJ		GENMASK(10, 7)
#define CFG_SW_PHY_INIT_DONE		BIT(11)

/* Lane RX/TX configuration (per‑lane, at lane_base) */
#define PCIE_RX_REG1			0x050
#define PCIE_RX_REFCLK_MODE		GENMASK(1, 0)
#define PCIE_RX_REFCLK_MODE_DRIVER	1
#define PCIE_RX_SEL_TRI_CODE		BIT(2)
#define PCIE_RX_LEGACY			GENMASK(15, 8)
#define PCIE_RX_LEGACY_DEFAULT		0x65

#define PCIE_TX_REG1			0x064

#define PCIE_PLL_TIMEOUT		500000
#define PCIE_POLL_DELAY			500

static int k3_usb3phy_init_single(struct k3_lane_group *lg, void __iomem *base)
{
	struct phy *phy = lg->phy;
	u32 val, tmp;
	int ret;

	/* Do not wait CDR lock before sampling data */
	val = readl(base + PHY_RESET_CFG);
	val = u32_replace_bits(val, 0, PHY_RESET_EN_SD_AFTER_LOCK);
	writel(val, base + PHY_RESET_CFG);

	/* Power down 100MHz refclk buffer */
	val = readl(base + PHY_PU_CK_REG);
	val = u32_replace_bits(val, 0, PHY_PU_REFCLK_100);
	writel(val, base + PHY_PU_CK_REG);

	/* Program PLL REG1 configure the SSC */
	val = FIELD_PREP(PHY_PLL_SSC_MODE, PHY_PLL_SSC_MODE_DOWN_SPREAD1) |
	      FIELD_PREP(PHY_PLL_SSC_DEP_SEL, PHY_PLL_SSC_5000PPM) |
	      FIELD_PREP(PHY_PLL_FREF_SEL, PHY_PLL_FREF_24M);
	writel(val, base + PHY_PLL_REG1);

	/* Un-select 100MHz PLL reference */
	val = readl(base + PHY_PLL_REG2);
	val = u32_replace_bits(val, 0, PHY_PLL_SEL_REF100);
	writel(val, base + PHY_PLL_REG2);

	/* USB LFPS period configuration */
	val = readl(base + PHY_MODE_CFG);
	val = u32_replace_bits(val, PHY_MODE_LFPS_TPERIOD_USB, PHY_MODE_LFPS_TPERIOD);
	writel(val, base + PHY_MODE_CFG);

	/* Force AFE adaptation reset */
	val = readl(base + PHY_ADPT_CFG0);
	val |= PHY_ADPT_AFE_RST_OVRD_EN | PHY_ADPT_AFE_RST_OVRD_VAL;
	writel(val, base + PHY_ADPT_CFG0);

	/* Override driver amplitude value to 900m */
	val = readl(base + PHY_RXEQ_TIME);
	val |= PHY_RXEQ_TIME_OVRD_AMP_SOC;
	val = u32_replace_bits(val, PHY_RXEQ_TIME_AMP_SOC_900M, PHY_RXEQ_TIME_CFG_AMP_SOC);
	writel(val, base + PHY_RXEQ_TIME);

	/* Configure RX parameters */
	val = PHY_RX_REG0_RLOAD |
		FIELD_PREP(PHY_RX_REG1_RTERM, 0x8) |
		FIELD_PREP(PHY_RX_REG1_RC_CALI, 0x7) |
		FIELD_PREP(PHY_RX_REG2_CSEL, 0x8) |
		PHY_RX_REG2_FORCE_CSEL |
		FIELD_PREP(PHY_RX_REG2_PSEL, 0x4) |
		FIELD_PREP(PHY_RX_REG3_I_LOAD, 0x7) |
		PHY_RX_REG3_SEL_CBOOST_CODE |
		FIELD_PREP(PHY_RX_REG3_ADJ_BIAS, 0x1) |
		FIELD_PREP(PHY_RX_REG3_RDEG1, 0x3);
	writel(val, base + PHY_RX_REG_A);

	val = readl(base + PHY_RX_REG_B);
	tmp = FIELD_PREP(PHY_RX_REG4_RDEG2, 0x2) |
		PHY_RX_REG4_ENVOS | PHY_RX_REG4_RTERM_SEL | PHY_RX_REG4_MANUAL_CFG |
		FIELD_PREP(PHY_RX_REG5_RCELL_VCM, 0x8) |
		FIELD_PREP(PHY_RX_REG5_RCELL_BIAS, 0x8) |
		FIELD_PREP(PHY_RX_REG6_H1_REG, 0x8) |
		FIELD_PREP(PHY_RX_REG6_ADAPT_GAIN, 0x2);
	val = u32_replace_bits(val, tmp, PHY_RX_REGB_MASK);
	writel(val, base + PHY_RX_REG_B);

	/*
	 * Inform PHY that all PLL-related configuration is done.
	 * PLL will not start locking until PHY_CLK_SW_INIT_DONE is set.
	 */
	val = PHY_CLK_SW_INIT_DONE | PHY_CLK_PU_SSC_OUT |
	      FIELD_PREP(PHY_CLK_REFCLK_FREQ, PHY_CLK_REFCLK_24M) |
	      PHY_CLK_RXCLK_EN | PHY_CLK_TXCLK_EN |
	      PHY_CLK_PCLK_EN | PHY_CLK_PIPE_PCLK_EN;
	writel(val, base + PHY_CLK_CFG);

	ret = readl_poll_timeout(base + PHY_CLK_CFG, val,
				 (val & PHY_CLK_PLL_READY),
				 PCIE_POLL_DELAY, PCIE_PLL_TIMEOUT);
	if (ret) {
		dev_err(&phy->dev, "PHY PLL polling timeout\n");
		return ret;
	}

	return 0;
}

static int k3_usb3phy_init(struct phy *phy)
{
	struct k3_lane_group *lg = phy_get_drvdata(phy);
	int ret, i;

	for (i = 0; i < lg->data->lanes; i++) {
		ret = k3_usb3phy_init_single(lg, lg->base + lg->data->offsets[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

const struct phy_ops k3_usb3_phy_ops = {
	.init = k3_usb3phy_init,
	.owner = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(k3_usb3_phy_ops);

static int k3_pcie_phy_init(struct phy *phy)
{
	struct k3_lane_group *lg = phy_get_drvdata(phy);
	void __iomem *phy_base = lg->base + lg->data->offsets[0];
	u32 val;
	int ret;
	int i;

	val = readl(phy_base + PHY_PLL_REG1);
	val = u32_replace_bits(val, 0x2, GENMASK(15, 12));
	writel(val, phy_base + PHY_PLL_REG1);

	val = readl(phy_base + PHY_PLL_REG2);
	val = u32_replace_bits(val, 0, BIT(21));
	writel(val, phy_base + PHY_PLL_REG2);

	for (i = 0; i < lg->data->lanes; i++) {
		void __iomem *lane_base = lg->base + lg->data->offsets[i];

		val = readl(lane_base + PCIE_RX_REG1);
		val = u32_replace_bits(val, 0, 0x3);
		writel(val, lane_base + PCIE_RX_REG1);
	}

	val = readl(phy_base + PHY_PLL_REG2);
	val |= BIT(20);
	writel(val, phy_base + PHY_PLL_REG2);

	/* The write is needed as clock requires renegotiation */
	val = FIELD_PREP(PCIE_RX_REFCLK_MODE, PCIE_RX_REFCLK_MODE_DRIVER) |
	      PCIE_RX_SEL_TRI_CODE |
	      FIELD_PREP(PCIE_RX_LEGACY, PCIE_RX_LEGACY_DEFAULT);
	writel(val, phy_base + PCIE_RX_REG1);

	/* pll_reg1 of lane0, disable SSC: pll[27:24] = 0 */
	val = readl(phy_base + PHY_PLL_REG1);
	val = u32_replace_bits(val, 0, GENMASK(27, 24));
	writel(val, phy_base + PHY_PLL_REG1);

	for (i = 0; i < lg->data->lanes; i++) {
		void __iomem *lane_base = lg->base + lg->data->offsets[i];

		/* set cfg_tx_send_dummy_data to be 1'b1 for disable dash data */
		val = readl(lane_base + PHY_PU_SEL);
		val = u32_replace_bits(val, 1, BIT(13));
		writel(val, lane_base + PHY_PU_SEL);

		/* disable en_sample_data_after_cdr_locked */
		val = readl(lane_base + PHY_RESET_CFG);
		val = u32_replace_bits(val, 0, BIT(6));
		writel(val, lane_base + PHY_RESET_CFG);

		/* Dynamic Lock */
		val = readl(lane_base + PHY_MODE_CFG);
		val = u32_replace_bits(val, 1, BIT(2));
		writel(val, lane_base + PHY_MODE_CFG);

		val = FIELD_PREP(PHY_RX_REG0_MASK, 0x10) |
			FIELD_PREP(PHY_RX_REG1_MASK, 0x78) |
			FIELD_PREP(PHY_RX_REG2_MASK, 0x98) |
			FIELD_PREP(PHY_RX_REG3_MASK, 0xdf);
		writel(val, lane_base + PHY_RX_REG_A);

		val = readl(lane_base + PHY_RX_REG_B);
		val &= ~PHY_RX_REGB_MASK;
		val |= FIELD_PREP(PHY_RX_REG4_MASK, 0xb4) |
			FIELD_PREP(PHY_RX_REG5_MASK, 0x88) |
			FIELD_PREP(PHY_RX_REG6_MASK, 0x28);
		writel(val, lane_base + PHY_RX_REG_B);

		/* Set init done */
		val = readl(lane_base + PCIE_PU_ADDR_CLK_CFG);
		val = u32_replace_bits(val, 1, CFG_SW_PHY_INIT_DONE);
		writel(val, lane_base + PCIE_PU_ADDR_CLK_CFG);
	}

	ret = readl_poll_timeout(phy_base + PCIE_PU_ADDR_CLK_CFG, val,
				 (val & PHY_CLK_PLL_READY), PCIE_POLL_DELAY,
				 PCIE_PLL_TIMEOUT);
	if (ret) {
		dev_err(&lg->phy->dev, "PHY PLL lock timeout\n");
		return ret;
	}

	return 0;
}

const struct phy_ops k3_pcie_phy_ops = {
	.init		= k3_pcie_phy_init,
	.owner		= THIS_MODULE,
};
EXPORT_SYMBOL_GPL(k3_pcie_phy_ops);

/* PHY rcal init requires APB_SPARE regmap access */

#define APB_SPARE_PU_CAL		0x178
#define PU_CAL				BIT(17)

#define APB_SPARE_RCAL_HSIO		0x17c
#define APB_SPARE_PU_CAL_DONE		BIT(8)
#define RCAL_OVRD_PTRIM			GENMASK(23, 20)
#define RCAL_OVRD_NTRIM			GENMASK(27, 24)
#define RCAL_OVRD_PTRIM_EN		BIT(28)
#define RCAL_OVRD_NTRIM_EN		BIT(29)
#define RCAL_OVRD_STABLE_VAL		BIT(30)
#define RCAL_OVRD_STABLE_EN		BIT(31)

#define RCAL_OVRD_TRIM_EN		(RCAL_OVRD_NTRIM_EN | RCAL_OVRD_PTRIM_EN)
#define RCAL_OVRD_TRIM_MASK		(RCAL_OVRD_NTRIM | RCAL_OVRD_PTRIM)

#define PU_CAL_TIMEOUT 2000000

static DEFINE_MUTEX(calibrate_lock);

int k3_phy_calibrate(struct regmap *apb_spare)
{
	unsigned int val = 0;
	int ret;

	guard(mutex)(&calibrate_lock);

	regmap_read(apb_spare, APB_SPARE_RCAL_HSIO, &val);
	if (val & APB_SPARE_PU_CAL_DONE)
		return 0;

	regmap_update_bits(apb_spare, APB_SPARE_PU_CAL, PU_CAL,
			   PU_CAL);

	ret = regmap_read_poll_timeout(apb_spare, APB_SPARE_RCAL_HSIO,
				       val, (val & APB_SPARE_PU_CAL_DONE), PCIE_POLL_DELAY,
				       PU_CAL_TIMEOUT);

	if (ret)
		regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
				   RCAL_OVRD_TRIM_EN | RCAL_OVRD_STABLE_VAL |
				   RCAL_OVRD_TRIM_MASK | RCAL_OVRD_STABLE_EN,
				   RCAL_OVRD_TRIM_EN | RCAL_OVRD_STABLE_VAL |
				   FIELD_PREP(RCAL_OVRD_NTRIM, 0x6) |
				   FIELD_PREP(RCAL_OVRD_PTRIM, 0xa) |
				   RCAL_OVRD_STABLE_EN);

	return 0;
}
EXPORT_SYMBOL_GPL(k3_phy_calibrate);

MODULE_DESCRIPTION("SpacemiT K3 PHY common ops");
MODULE_LICENSE("GPL");
