// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: jitao.shi <jitao.shi@mediatek.com>
 *
 * Copyright (c) 2026 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include "phy-mtk-io.h"
#include "phy-mtk-mipi-dsi.h"

#define MIPITX_LANE_CON		0x0004
#define RG_DSI_CPHY_T1DRV_EN		BIT(0)
#define RG_DSI_ANA_CK_SEL		BIT(1)
#define RG_DSI_PHY_CK_SEL		BIT(2)
#define RG_DSI_CPHY_EN			BIT(3)
#define RG_DSI_PHYCK_INV_EN		BIT(4)
#define RG_DSI_PWR04_EN			BIT(5)
#define RG_DSI_BG_LPF_EN		BIT(6)
#define RG_DSI_BG_CORE_EN		BIT(7)
#define RG_DSI_PAD_TIEL_SEL		BIT(8)

#define MIPITX_VOLTAGE_SEL	0x0008
#define RG_DSI_HSTX_LDO_REF_SEL		GENMASK(9, 6)
#define RG_DSI_PRD_REF_SEL		GENMASK(5, 0)
#define RG_DSI_PRD_REF_MINI		0
#define RG_DSI_PRD_REF_DEF		4
#define RG_DSI_PRD_REF_MAX		7

#define MIPITX_PRESERVED	0x000c
#define MIPITX_PRESERVED_DEF		0xffff0040
#define MIPITX_PRESERVED_MINI		0xffff00f0

#define MIPITX_PLL_PWR		0x0028
#define AD_DSI_PLL_SDM_PWR_ON		BIT(0)
#define AD_DSI_PLL_SDM_ISO_EN		BIT(1)
#define MIPITX_PLL_CON0		0x002c
#define MIPITX_PLL_CON1		0x0030
#define RG_DSI_PLL_EN			BIT(0)
#define RG_DSI_PLL_POSDIV		GENMASK(10, 8)
#define MIPITX_PLL_CON2		0x0034
#define MIPITX_PLL_CON3		0x0038
#define MIPITX_PLL_CON4		0x003c
#define RG_DSI_PLL_IBIAS		GENMASK(11, 10)

#define MIPITX_D2_SW_CTL_EN	0x015c
#define MIPITX_D0_SW_CTL_EN	0x025c
#define MIPITX_CK_CKMODE_EN	0x0320
#define DSI_CK_CKMODE_EN		BIT(0)
#define MIPITX_CK_SW_CTL_EN	0x035c
#define MIPITX_D1_SW_CTL_EN	0x045c
#define MIPITX_D3_SW_CTL_EN	0x055c
#define DSI_SW_CTL_EN			BIT(0)

#define DSI_PHY_XTAL_CLK_HZ		26000000
#define DSI_PHY_PLL_MIN_RATE_HZ		125000000
#define DSI_PHY_PLL_MAX_RATE_HZ		2000000000

static int mtk_mipi_tx_pll_enable(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);
	void __iomem *base = mipi_tx->regs;
	u32 voltage = RG_DSI_PRD_REF_MINI;
	u32 pres = MIPITX_PRESERVED_MINI;
	unsigned long long pcw_calc;
	unsigned int txdiv, txdiv0;
	u32 pcw;

	dev_dbg(mipi_tx->dev, "enable: %u bps\n", mipi_tx->data_rate);

	if (mipi_tx->data_rate >= DSI_PHY_PLL_MAX_RATE_HZ) {
		/* Select higher signaling voltage for fast data rates */
		voltage = RG_DSI_PRD_REF_DEF;
		pres = MIPITX_PRESERVED_DEF;
		txdiv = 1;
		txdiv0 = 0;
	} else if (mipi_tx->data_rate >= 1000000000) {
		txdiv = 2;
		txdiv0 = 1;
	} else if (mipi_tx->data_rate >= 500000000) {
		txdiv = 4;
		txdiv0 = 2;
	} else if (mipi_tx->data_rate > 250000000) {
		txdiv = 8;
		txdiv0 = 3;
	} else if (mipi_tx->data_rate >= 125000000) {
		txdiv = 16;
		txdiv0 = 4;
	} else {
		return -EINVAL;
	}

	pcw_calc = ((u64)(mipi_tx->data_rate / 2) * txdiv) << 24;
	pcw_calc = div_u64(pcw_calc, DSI_PHY_XTAL_CLK_HZ);

	if (pcw_calc > U32_MAX) {
		dev_err(mipi_tx->dev, "Calculated PCW=%llu overflow!\n", pcw_calc);
		return -EINVAL;
	}
	pcw = (u32)pcw_calc;

	mtk_phy_update_field(base + MIPITX_VOLTAGE_SEL, RG_DSI_PRD_REF_SEL, voltage);
	writel(pres, base + MIPITX_PRESERVED);

	/* Enable the SDM and wait for power to stabilize */
	mtk_phy_set_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_PWR_ON);
	mtk_phy_clear_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);
	udelay(30);

	/* Disable isolation and program PLL's PCW and dividers */
	mtk_phy_clear_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_ISO_EN);
	writel(pcw, base + MIPITX_PLL_CON0);
	mtk_phy_update_field(base + MIPITX_PLL_CON1, RG_DSI_PLL_POSDIV, txdiv0);

	/* Enable the PLL and wait for the clock output to stabilize */
	mtk_phy_set_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);
	udelay(30);

	return 0;
}

static void mtk_mipi_tx_pll_disable(struct clk_hw *hw)
{
	struct mtk_mipi_tx *mipi_tx = mtk_mipi_tx_from_clk_hw(hw);
	void __iomem *base = mipi_tx->regs;

	mtk_phy_clear_bits(base + MIPITX_PLL_CON1, RG_DSI_PLL_EN);

	mtk_phy_set_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_ISO_EN);
	mtk_phy_clear_bits(base + MIPITX_PLL_PWR, AD_DSI_PLL_SDM_PWR_ON);
}

static int mtk_mipi_tx_pll_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	req->rate = clamp_val(req->rate,
			      DSI_PHY_PLL_MIN_RATE_HZ, DSI_PHY_PLL_MAX_RATE_HZ);

	return 0;
}

static const struct clk_ops mtk_mipi_tx_pll_ops = {
	.enable = mtk_mipi_tx_pll_enable,
	.disable = mtk_mipi_tx_pll_disable,
	.determine_rate = mtk_mipi_tx_pll_determine_rate,
	.set_rate = mtk_mipi_tx_pll_set_rate,
	.recalc_rate = mtk_mipi_tx_pll_recalc_rate,
};

static void mtk_mipi_tx_power_on_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	void __iomem *base = mipi_tx->regs;

	/* BG_LPF_EN / BG_CORE_EN */
	writel(RG_DSI_PAD_TIEL_SEL | RG_DSI_BG_CORE_EN, base + MIPITX_LANE_CON);
	/* Wait for MIPI core to enable */
	usleep_range(30, 100);
	writel(RG_DSI_BG_CORE_EN | RG_DSI_BG_LPF_EN, base + MIPITX_LANE_CON);

	/* Switch OFF each Lane */
	mtk_phy_clear_bits(base + MIPITX_D0_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_clear_bits(base + MIPITX_D1_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_clear_bits(base + MIPITX_D2_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_clear_bits(base + MIPITX_D3_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_clear_bits(base + MIPITX_CK_SW_CTL_EN, DSI_SW_CTL_EN);

	/*
	 * The MIPI TX drive strength is in the range of 3000 ~ 6000 microamps:
	 * RG_DSI_HSTX_LDO_REF_SEL expresses an offset from the minimum drive
	 * strength (3000uA) and can add a maximum offset of 3000uA, reaching a
	 * maximum drive strength of 3000+3000=6000uA.
	 */
	mtk_phy_update_field(base + MIPITX_VOLTAGE_SEL, RG_DSI_HSTX_LDO_REF_SEL,
			     (mipi_tx->mipitx_drive - 3000) / 200);

	mtk_phy_set_bits(base + MIPITX_CK_CKMODE_EN, DSI_CK_CKMODE_EN);
}

static void mtk_mipi_tx_power_off_signal(struct phy *phy)
{
	struct mtk_mipi_tx *mipi_tx = phy_get_drvdata(phy);
	void __iomem *base = mipi_tx->regs;

	/* Switch ON each lane one by one */
	mtk_phy_set_bits(base + MIPITX_D0_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_set_bits(base + MIPITX_D1_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_set_bits(base + MIPITX_D2_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_set_bits(base + MIPITX_D3_SW_CTL_EN, DSI_SW_CTL_EN);
	mtk_phy_set_bits(base + MIPITX_CK_SW_CTL_EN, DSI_SW_CTL_EN);

	writel(RG_DSI_PAD_TIEL_SEL | RG_DSI_BG_CORE_EN, base + MIPITX_LANE_CON);
	writel(RG_DSI_PAD_TIEL_SEL, base + MIPITX_LANE_CON);
}

const struct mtk_mipitx_data mt8196_mipitx_data = {
	.mipi_tx_clk_ops = &mtk_mipi_tx_pll_ops,
	.mipi_tx_enable_signal = mtk_mipi_tx_power_on_signal,
	.mipi_tx_disable_signal = mtk_mipi_tx_power_off_signal,
};
