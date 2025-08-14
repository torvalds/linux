// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip PIPE USB3.0 PCIE SATA Combo Phy driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 */

#include <dt-bindings/phy/phy.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/units.h>

#define BIT_WRITEABLE_SHIFT		16
#define REF_CLOCK_24MHz			(24 * HZ_PER_MHZ)
#define REF_CLOCK_25MHz			(25 * HZ_PER_MHZ)
#define REF_CLOCK_100MHz		(100 * HZ_PER_MHZ)

/* RK3528 COMBO PHY REG */
#define RK3528_PHYREG6				0x18
#define RK3528_PHYREG6_PLL_KVCO			GENMASK(12, 10)
#define RK3528_PHYREG6_PLL_KVCO_VALUE		0x2
#define RK3528_PHYREG6_SSC_DIR			GENMASK(5, 4)
#define RK3528_PHYREG6_SSC_UPWARD		0
#define RK3528_PHYREG6_SSC_DOWNWARD		1

#define RK3528_PHYREG40				0x100
#define RK3528_PHYREG40_SSC_EN			BIT(20)
#define RK3528_PHYREG40_SSC_CNT			GENMASK(10, 0)
#define RK3528_PHYREG40_SSC_CNT_VALUE		0x17d

#define RK3528_PHYREG42				0x108
#define RK3528_PHYREG42_CKDRV_CLK_SEL		BIT(29)
#define RK3528_PHYREG42_CKDRV_CLK_PLL		0
#define RK3528_PHYREG42_CKDRV_CLK_CKRCV		1
#define RK3528_PHYREG42_PLL_LPF_R1_ADJ		GENMASK(10, 7)
#define RK3528_PHYREG42_PLL_LPF_R1_ADJ_VALUE	0x9
#define RK3528_PHYREG42_PLL_CHGPUMP_CUR_ADJ	GENMASK(6, 4)
#define RK3528_PHYREG42_PLL_CHGPUMP_CUR_ADJ_VALUE 0x7
#define RK3528_PHYREG42_PLL_KVCO_ADJ		GENMASK(2, 0)
#define RK3528_PHYREG42_PLL_KVCO_ADJ_VALUE	0x0

#define RK3528_PHYREG80				0x200
#define RK3528_PHYREG80_CTLE_EN			BIT(17)

#define RK3528_PHYREG81				0x204
#define RK3528_PHYREG81_CDR_PHASE_PATH_GAIN_2X	BIT(5)
#define RK3528_PHYREG81_SLEW_RATE_CTRL		GENMASK(2, 0)
#define RK3528_PHYREG81_SLEW_RATE_CTRL_SLOW	0x7

#define RK3528_PHYREG83				0x20c
#define RK3528_PHYREG83_RX_SQUELCH		GENMASK(2, 0)
#define RK3528_PHYREG83_RX_SQUELCH_VALUE	0x6

#define RK3528_PHYREG86				0x218
#define RK3528_PHYREG86_RTERM_DET_CLK_EN	BIT(14)

/* RK3568 COMBO PHY REG */
#define RK3568_PHYREG6				0x14
#define RK3568_PHYREG6_PLL_DIV_MASK		GENMASK(7, 6)
#define RK3568_PHYREG6_PLL_DIV_SHIFT		6
#define RK3568_PHYREG6_PLL_DIV_2		1

#define RK3568_PHYREG7				0x18
#define RK3568_PHYREG7_TX_RTERM_MASK		GENMASK(7, 4)
#define RK3568_PHYREG7_TX_RTERM_SHIFT		4
#define RK3568_PHYREG7_TX_RTERM_50OHM		8
#define RK3568_PHYREG7_RX_RTERM_MASK		GENMASK(3, 0)
#define RK3568_PHYREG7_RX_RTERM_SHIFT		0
#define RK3568_PHYREG7_RX_RTERM_44OHM		15

#define RK3568_PHYREG8				0x1C
#define RK3568_PHYREG8_SSC_EN			BIT(4)

#define RK3568_PHYREG11				0x28
#define RK3568_PHYREG11_SU_TRIM_0_7		0xF0

#define RK3568_PHYREG12				0x2C
#define RK3568_PHYREG12_PLL_LPF_ADJ_VALUE	4

#define RK3568_PHYREG13				0x30
#define RK3568_PHYREG13_RESISTER_MASK		GENMASK(5, 4)
#define RK3568_PHYREG13_RESISTER_SHIFT		0x4
#define RK3568_PHYREG13_RESISTER_HIGH_Z		3
#define RK3568_PHYREG13_CKRCV_AMP0		BIT(7)

#define RK3568_PHYREG14				0x34
#define RK3568_PHYREG14_CKRCV_AMP1		BIT(0)

#define RK3568_PHYREG15				0x38
#define RK3568_PHYREG15_CTLE_EN			BIT(0)
#define RK3568_PHYREG15_SSC_CNT_MASK		GENMASK(7, 6)
#define RK3568_PHYREG15_SSC_CNT_SHIFT		6
#define RK3568_PHYREG15_SSC_CNT_VALUE		1

#define RK3568_PHYREG16				0x3C
#define RK3568_PHYREG16_SSC_CNT_VALUE		0x5f

#define RK3568_PHYREG18				0x44
#define RK3568_PHYREG18_PLL_LOOP		0x32

#define RK3568_PHYREG32				0x7C
#define RK3568_PHYREG32_SSC_MASK		GENMASK(7, 4)
#define RK3568_PHYREG32_SSC_DIR_MASK		GENMASK(5, 4)
#define RK3568_PHYREG32_SSC_DIR_SHIFT		4
#define RK3568_PHYREG32_SSC_UPWARD		0
#define RK3568_PHYREG32_SSC_DOWNWARD		1
#define RK3568_PHYREG32_SSC_OFFSET_MASK	GENMASK(7, 6)
#define RK3568_PHYREG32_SSC_OFFSET_SHIFT	6
#define RK3568_PHYREG32_SSC_OFFSET_500PPM	1

#define RK3568_PHYREG33				0x80
#define RK3568_PHYREG33_PLL_KVCO_MASK		GENMASK(4, 2)
#define RK3568_PHYREG33_PLL_KVCO_SHIFT		2
#define RK3568_PHYREG33_PLL_KVCO_VALUE		2
#define RK3576_PHYREG33_PLL_KVCO_VALUE		4

/* RK3588 COMBO PHY registers */
#define RK3588_PHYREG27				0x6C
#define RK3588_PHYREG27_RX_TRIM			0x4C

/* RK3576 COMBO PHY registers */
#define RK3576_PHYREG10				0x24
#define RK3576_PHYREG10_SSC_PCM_MASK		GENMASK(3, 0)
#define RK3576_PHYREG10_SSC_PCM_3500PPM		7

#define RK3576_PHYREG17				0x40

#define RK3576_PHYREG21				0x50
#define RK3576_PHYREG21_RX_SQUELCH_VAL		0x0D

#define RK3576_PHYREG30				0x74

struct rockchip_combphy_priv;

struct combphy_reg {
	u16 offset;
	u16 bitend;
	u16 bitstart;
	u16 disable;
	u16 enable;
};

struct rockchip_combphy_grfcfg {
	struct combphy_reg pcie_mode_set;
	struct combphy_reg usb_mode_set;
	struct combphy_reg sgmii_mode_set;
	struct combphy_reg qsgmii_mode_set;
	struct combphy_reg pipe_rxterm_set;
	struct combphy_reg pipe_txelec_set;
	struct combphy_reg pipe_txcomp_set;
	struct combphy_reg pipe_clk_24m;
	struct combphy_reg pipe_clk_25m;
	struct combphy_reg pipe_clk_100m;
	struct combphy_reg pipe_phymode_sel;
	struct combphy_reg pipe_rate_sel;
	struct combphy_reg pipe_rxterm_sel;
	struct combphy_reg pipe_txelec_sel;
	struct combphy_reg pipe_txcomp_sel;
	struct combphy_reg pipe_clk_ext;
	struct combphy_reg pipe_sel_usb;
	struct combphy_reg pipe_sel_qsgmii;
	struct combphy_reg pipe_phy_status;
	struct combphy_reg con0_for_pcie;
	struct combphy_reg con1_for_pcie;
	struct combphy_reg con2_for_pcie;
	struct combphy_reg con3_for_pcie;
	struct combphy_reg con0_for_sata;
	struct combphy_reg con1_for_sata;
	struct combphy_reg con2_for_sata;
	struct combphy_reg con3_for_sata;
	struct combphy_reg pipe_con0_for_sata;
	struct combphy_reg pipe_con1_for_sata;
	struct combphy_reg pipe_xpcs_phy_ready;
	struct combphy_reg pipe_pcie1l0_sel;
	struct combphy_reg pipe_pcie1l1_sel;
	struct combphy_reg u3otg0_port_en;
	struct combphy_reg u3otg1_port_en;
};

struct rockchip_combphy_cfg {
	unsigned int num_phys;
	unsigned int phy_ids[3];
	const struct rockchip_combphy_grfcfg *grfcfg;
	int (*combphy_cfg)(struct rockchip_combphy_priv *priv);
};

struct rockchip_combphy_priv {
	u8 type;
	int id;
	void __iomem *mmio;
	int num_clks;
	struct clk_bulk_data *clks;
	struct device *dev;
	struct regmap *pipe_grf;
	struct regmap *phy_grf;
	struct phy *phy;
	struct reset_control *phy_rst;
	const struct rockchip_combphy_cfg *cfg;
	bool enable_ssc;
	bool ext_refclk;
	struct clk *refclk;
};

static void rockchip_combphy_updatel(struct rockchip_combphy_priv *priv,
				     int mask, int val, int reg)
{
	unsigned int temp;

	temp = readl(priv->mmio + reg);
	temp = (temp & ~(mask)) | val;
	writel(temp, priv->mmio + reg);
}

static int rockchip_combphy_param_write(struct regmap *base,
					const struct combphy_reg *reg, bool en)
{
	u32 val, mask, tmp;

	tmp = en ? reg->enable : reg->disable;
	mask = GENMASK(reg->bitend, reg->bitstart);
	val = (tmp << reg->bitstart) | (mask << BIT_WRITEABLE_SHIFT);

	return regmap_write(base, reg->offset, val);
}

static u32 rockchip_combphy_is_ready(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	u32 mask, val;

	mask = GENMASK(cfg->pipe_phy_status.bitend,
		       cfg->pipe_phy_status.bitstart);

	regmap_read(priv->phy_grf, cfg->pipe_phy_status.offset, &val);
	val = (val & mask) >> cfg->pipe_phy_status.bitstart;

	return val;
}

static int rockchip_combphy_init(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	u32 val;
	int ret;

	ret = clk_bulk_prepare_enable(priv->num_clks, priv->clks);
	if (ret) {
		dev_err(priv->dev, "failed to enable clks\n");
		return ret;
	}

	switch (priv->type) {
	case PHY_TYPE_PCIE:
	case PHY_TYPE_USB3:
	case PHY_TYPE_SATA:
	case PHY_TYPE_SGMII:
	case PHY_TYPE_QSGMII:
		if (priv->cfg->combphy_cfg)
			ret = priv->cfg->combphy_cfg(priv);
		break;
	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		ret = -EINVAL;
		break;
	}

	if (ret) {
		dev_err(priv->dev, "failed to init phy for phy type %x\n", priv->type);
		goto err_clk;
	}

	ret = reset_control_deassert(priv->phy_rst);
	if (ret)
		goto err_clk;

	if (priv->type == PHY_TYPE_USB3) {
		ret = readx_poll_timeout_atomic(rockchip_combphy_is_ready,
						priv, val,
						val == cfg->pipe_phy_status.enable,
						10, 1000);
		if (ret)
			dev_warn(priv->dev, "wait phy status ready timeout\n");
	}

	return 0;

err_clk:
	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);

	return ret;
}

static int rockchip_combphy_exit(struct phy *phy)
{
	struct rockchip_combphy_priv *priv = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(priv->num_clks, priv->clks);
	reset_control_assert(priv->phy_rst);

	return 0;
}

static const struct phy_ops rockchip_combphy_ops = {
	.init = rockchip_combphy_init,
	.exit = rockchip_combphy_exit,
	.owner = THIS_MODULE,
};

static struct phy *rockchip_combphy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct rockchip_combphy_priv *priv = dev_get_drvdata(dev);

	if (args->args_count != 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	if (priv->type != PHY_NONE && priv->type != args->args[0])
		dev_warn(dev, "phy type select %d overwriting type %d\n",
			 args->args[0], priv->type);

	priv->type = args->args[0];

	return priv->phy;
}

static int rockchip_combphy_parse_dt(struct device *dev, struct rockchip_combphy_priv *priv)
{
	int i;

	priv->num_clks = devm_clk_bulk_get_all(dev, &priv->clks);
	if (priv->num_clks < 1)
		return -EINVAL;

	priv->refclk = NULL;
	for (i = 0; i < priv->num_clks; i++) {
		if (!strncmp(priv->clks[i].id, "ref", 3)) {
			priv->refclk = priv->clks[i].clk;
			break;
		}
	}

	if (!priv->refclk) {
		dev_err(dev, "no refclk found\n");
		return -EINVAL;
	}

	priv->pipe_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,pipe-grf");
	if (IS_ERR(priv->pipe_grf)) {
		dev_err(dev, "failed to find peri_ctrl pipe-grf regmap\n");
		return PTR_ERR(priv->pipe_grf);
	}

	priv->phy_grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,pipe-phy-grf");
	if (IS_ERR(priv->phy_grf)) {
		dev_err(dev, "failed to find peri_ctrl pipe-phy-grf regmap\n");
		return PTR_ERR(priv->phy_grf);
	}

	priv->enable_ssc = device_property_present(dev, "rockchip,enable-ssc");

	priv->ext_refclk = device_property_present(dev, "rockchip,ext-refclk");

	priv->phy_rst = devm_reset_control_get_exclusive(dev, "phy");
	/* fallback to old behaviour */
	if (PTR_ERR(priv->phy_rst) == -ENOENT)
		priv->phy_rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(priv->phy_rst))
		return dev_err_probe(dev, PTR_ERR(priv->phy_rst), "failed to get phy reset\n");

	return 0;
}

static int rockchip_combphy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct rockchip_combphy_priv *priv;
	const struct rockchip_combphy_cfg *phy_cfg;
	struct resource *res;
	int ret, id;

	phy_cfg = of_device_get_match_data(dev);
	if (!phy_cfg) {
		dev_err(dev, "no OF match data provided\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->mmio = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->mmio)) {
		ret = PTR_ERR(priv->mmio);
		return ret;
	}

	/* find the phy-id from the io address */
	priv->id = -ENODEV;
	for (id = 0; id < phy_cfg->num_phys; id++) {
		if (res->start == phy_cfg->phy_ids[id]) {
			priv->id = id;
			break;
		}
	}

	priv->dev = dev;
	priv->type = PHY_NONE;
	priv->cfg = phy_cfg;

	ret = rockchip_combphy_parse_dt(dev, priv);
	if (ret)
		return ret;

	ret = reset_control_assert(priv->phy_rst);
	if (ret) {
		dev_err(dev, "failed to reset phy\n");
		return ret;
	}

	priv->phy = devm_phy_create(dev, NULL, &rockchip_combphy_ops);
	if (IS_ERR(priv->phy)) {
		dev_err(dev, "failed to create combphy\n");
		return PTR_ERR(priv->phy);
	}

	dev_set_drvdata(dev, priv);
	phy_set_drvdata(priv->phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, rockchip_combphy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static int rk3528_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	unsigned long rate;
	u32 val;

	/* Set SSC downward spread spectrum */
	val = FIELD_PREP(RK3528_PHYREG6_SSC_DIR, RK3528_PHYREG6_SSC_DOWNWARD);
	rockchip_combphy_updatel(priv, RK3528_PHYREG6_SSC_DIR, val, RK3528_PHYREG6);

	switch (priv->type) {
	case PHY_TYPE_PCIE:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_pcie, true);
		break;
	case PHY_TYPE_USB3:
		/* Enable adaptive CTLE for USB3.0 Rx */
		rockchip_combphy_updatel(priv, RK3528_PHYREG80_CTLE_EN, RK3528_PHYREG80_CTLE_EN,
					 RK3528_PHYREG80);

		/* Set slow slew rate control for PI */
		val = FIELD_PREP(RK3528_PHYREG81_SLEW_RATE_CTRL,
				 RK3528_PHYREG81_SLEW_RATE_CTRL_SLOW);
		rockchip_combphy_updatel(priv, RK3528_PHYREG81_SLEW_RATE_CTRL, val,
					 RK3528_PHYREG81);

		/* Set CDR phase path with 2x gain */
		rockchip_combphy_updatel(priv, RK3528_PHYREG81_CDR_PHASE_PATH_GAIN_2X,
					 RK3528_PHYREG81_CDR_PHASE_PATH_GAIN_2X, RK3528_PHYREG81);

		/* Set Rx squelch input filler bandwidth */
		val = FIELD_PREP(RK3528_PHYREG83_RX_SQUELCH, RK3528_PHYREG83_RX_SQUELCH_VALUE);
		rockchip_combphy_updatel(priv, RK3528_PHYREG83_RX_SQUELCH, val, RK3528_PHYREG83);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txcomp_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txelec_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->usb_mode_set, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->u3otg0_port_en, true);
		break;
	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	rate = clk_get_rate(priv->refclk);

	switch (rate) {
	case REF_CLOCK_24MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_24m, true);
		if (priv->type == PHY_TYPE_USB3) {
			/* Set ssc_cnt[10:0]=00101111101 & 31.5KHz */
			val = FIELD_PREP(RK3528_PHYREG40_SSC_CNT, RK3528_PHYREG40_SSC_CNT_VALUE);
			rockchip_combphy_updatel(priv, RK3528_PHYREG40_SSC_CNT, val,
						 RK3528_PHYREG40);
		} else if (priv->type == PHY_TYPE_PCIE) {
			/* tx_trim[14]=1, Enable the counting clock of the rterm detect */
			rockchip_combphy_updatel(priv, RK3528_PHYREG86_RTERM_DET_CLK_EN,
						 RK3528_PHYREG86_RTERM_DET_CLK_EN, RK3528_PHYREG86);
		}
		break;
	case REF_CLOCK_100MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_100m, true);
		if (priv->type == PHY_TYPE_PCIE) {
			/* PLL KVCO tuning fine */
			val = FIELD_PREP(RK3528_PHYREG6_PLL_KVCO, RK3528_PHYREG6_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3528_PHYREG6_PLL_KVCO, val,
						 RK3528_PHYREG6);

			/* su_trim[6:4]=111, [10:7]=1001, [2:0]=000, swing 650mv */
			writel(0x570804f0, priv->mmio + RK3528_PHYREG42);
		}
		break;
	default:
		dev_err(priv->dev, "Unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (device_property_read_bool(priv->dev, "rockchip,ext-refclk")) {
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_ext, true);

		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_100MHz) {
			val = FIELD_PREP(RK3528_PHYREG42_CKDRV_CLK_SEL,
					 RK3528_PHYREG42_CKDRV_CLK_CKRCV);
			val |= FIELD_PREP(RK3528_PHYREG42_PLL_LPF_R1_ADJ,
					  RK3528_PHYREG42_PLL_LPF_R1_ADJ_VALUE);
			val |= FIELD_PREP(RK3528_PHYREG42_PLL_CHGPUMP_CUR_ADJ,
					  RK3528_PHYREG42_PLL_CHGPUMP_CUR_ADJ_VALUE);
			val |= FIELD_PREP(RK3528_PHYREG42_PLL_KVCO_ADJ,
					  RK3528_PHYREG42_PLL_KVCO_ADJ_VALUE);
			rockchip_combphy_updatel(priv,
						 RK3528_PHYREG42_CKDRV_CLK_SEL		|
						 RK3528_PHYREG42_PLL_LPF_R1_ADJ		|
						 RK3528_PHYREG42_PLL_CHGPUMP_CUR_ADJ	|
						 RK3528_PHYREG42_PLL_KVCO_ADJ,
						 val, RK3528_PHYREG42);

			val = FIELD_PREP(RK3528_PHYREG6_PLL_KVCO, RK3528_PHYREG6_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3528_PHYREG6_PLL_KVCO, val,
						 RK3528_PHYREG6);
		}
	}

	if (priv->type == PHY_TYPE_PCIE) {
		if (device_property_read_bool(priv->dev, "rockchip,enable-ssc"))
			rockchip_combphy_updatel(priv, RK3528_PHYREG40_SSC_EN,
						 RK3528_PHYREG40_SSC_EN, RK3528_PHYREG40);
	}

	return 0;
}

static const struct rockchip_combphy_grfcfg rk3528_combphy_grfcfgs = {
	/* pipe-phy-grf */
	.pcie_mode_set		= { 0x0000, 5, 0, 0x00, 0x11 },
	.usb_mode_set		= { 0x0000, 5, 0, 0x00, 0x04 },
	.pipe_rxterm_set	= { 0x0000, 12, 12, 0x00, 0x01 },
	.pipe_txelec_set	= { 0x0004, 1, 1, 0x00, 0x01 },
	.pipe_txcomp_set	= { 0x0004, 4, 4, 0x00, 0x01 },
	.pipe_clk_24m		= { 0x0004, 14, 13, 0x00, 0x00 },
	.pipe_clk_100m		= { 0x0004, 14, 13, 0x00, 0x02 },
	.pipe_rxterm_sel	= { 0x0008, 8, 8, 0x00, 0x01 },
	.pipe_txelec_sel	= { 0x0008, 12, 12, 0x00, 0x01 },
	.pipe_txcomp_sel	= { 0x0008, 15, 15, 0x00, 0x01 },
	.pipe_clk_ext		= { 0x000c, 9, 8, 0x02, 0x01 },
	.pipe_phy_status	= { 0x0034, 6, 6, 0x01, 0x00 },
	.con0_for_pcie		= { 0x0000, 15, 0, 0x00, 0x110 },
	.con1_for_pcie		= { 0x0004, 15, 0, 0x00, 0x00 },
	.con2_for_pcie		= { 0x0008, 15, 0, 0x00, 0x101 },
	.con3_for_pcie		= { 0x000c, 15, 0, 0x00, 0x0200 },
	/* pipe-grf */
	.u3otg0_port_en         = { 0x0044, 15, 0, 0x0181, 0x1100 },
};

static const struct rockchip_combphy_cfg rk3528_combphy_cfgs = {
	.num_phys	= 1,
	.phy_ids	= {
		0xffdc0000,
	},
	.grfcfg		= &rk3528_combphy_grfcfgs,
	.combphy_cfg	= rk3528_combphy_cfg,
};

static int rk3562_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	unsigned long rate;
	u32 val;

	switch (priv->type) {
	case PHY_TYPE_PCIE:
		/* Set SSC downward spread spectrum */
		val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_pcie, true);
		break;
	case PHY_TYPE_USB3:
		/* Set SSC downward spread spectrum */
		val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val,
					 RK3568_PHYREG32);

		/* Enable adaptive CTLE for USB3.0 Rx */
		rockchip_combphy_updatel(priv, RK3568_PHYREG15_CTLE_EN,
					 RK3568_PHYREG15_CTLE_EN, RK3568_PHYREG15);

		/* Set PLL KVCO fine tuning signals */
		rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
					 BIT(3), RK3568_PHYREG33);

		/* Set PLL LPF R1 to su_trim[10:7]=1001 */
		writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

		/* Set PLL input clock divider 1/2 */
		val = FIELD_PREP(RK3568_PHYREG6_PLL_DIV_MASK, RK3568_PHYREG6_PLL_DIV_2);
		rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK, val, RK3568_PHYREG6);

		/* Set PLL loop divider */
		writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);

		/* Set PLL KVCO to min and set PLL charge pump current to max */
		writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_sel_usb, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txcomp_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txelec_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->usb_mode_set, true);
		break;
	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	rate = clk_get_rate(priv->refclk);

	switch (rate) {
	case REF_CLOCK_24MHz:
		if (priv->type == PHY_TYPE_USB3) {
			/* Set ssc_cnt[9:0]=0101111101 & 31.5KHz */
			val = FIELD_PREP(RK3568_PHYREG15_SSC_CNT_MASK,
					 RK3568_PHYREG15_SSC_CNT_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG15_SSC_CNT_MASK,
						 val, RK3568_PHYREG15);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);
		}
		break;
	case REF_CLOCK_25MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_25m, true);
		break;
	case REF_CLOCK_100MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_100m, true);
		if (priv->type == PHY_TYPE_PCIE) {
			/* PLL KVCO tuning fine */
			val = FIELD_PREP(RK3568_PHYREG33_PLL_KVCO_MASK,
					 RK3568_PHYREG33_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Enable controlling random jitter, aka RMJ */
			writel(0x4, priv->mmio + RK3568_PHYREG12);

			val = RK3568_PHYREG6_PLL_DIV_2 << RK3568_PHYREG6_PLL_DIV_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK,
						 val, RK3568_PHYREG6);

			writel(0x32, priv->mmio + RK3568_PHYREG18);
			writel(0xf0, priv->mmio + RK3568_PHYREG11);
		}
		break;
	default:
		dev_err(priv->dev, "Unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (priv->ext_refclk) {
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_ext, true);
		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_100MHz) {
			val = RK3568_PHYREG13_RESISTER_HIGH_Z << RK3568_PHYREG13_RESISTER_SHIFT;
			val |= RK3568_PHYREG13_CKRCV_AMP0;
			rockchip_combphy_updatel(priv, RK3568_PHYREG13_RESISTER_MASK, val,
						 RK3568_PHYREG13);

			val = readl(priv->mmio + RK3568_PHYREG14);
			val |= RK3568_PHYREG14_CKRCV_AMP1;
			writel(val, priv->mmio + RK3568_PHYREG14);
		}
	}

	if (priv->enable_ssc) {
		val = readl(priv->mmio + RK3568_PHYREG8);
		val |= RK3568_PHYREG8_SSC_EN;
		writel(val, priv->mmio + RK3568_PHYREG8);
	}

	return 0;
}

static const struct rockchip_combphy_grfcfg rk3562_combphy_grfcfgs = {
	/* pipe-phy-grf */
	.pcie_mode_set		= { 0x0000, 5, 0, 0x00, 0x11 },
	.usb_mode_set		= { 0x0000, 5, 0, 0x00, 0x04 },
	.pipe_rxterm_set	= { 0x0000, 12, 12, 0x00, 0x01 },
	.pipe_txelec_set	= { 0x0004, 1, 1, 0x00, 0x01 },
	.pipe_txcomp_set	= { 0x0004, 4, 4, 0x00, 0x01 },
	.pipe_clk_25m		= { 0x0004, 14, 13, 0x00, 0x01 },
	.pipe_clk_100m		= { 0x0004, 14, 13, 0x00, 0x02 },
	.pipe_phymode_sel	= { 0x0008, 1, 1, 0x00, 0x01 },
	.pipe_rate_sel		= { 0x0008, 2, 2, 0x00, 0x01 },
	.pipe_rxterm_sel	= { 0x0008, 8, 8, 0x00, 0x01 },
	.pipe_txelec_sel	= { 0x0008, 12, 12, 0x00, 0x01 },
	.pipe_txcomp_sel	= { 0x0008, 15, 15, 0x00, 0x01 },
	.pipe_clk_ext		= { 0x000c, 9, 8, 0x02, 0x01 },
	.pipe_sel_usb		= { 0x000c, 14, 13, 0x00, 0x01 },
	.pipe_phy_status	= { 0x0034, 6, 6, 0x01, 0x00 },
	.con0_for_pcie		= { 0x0000, 15, 0, 0x00, 0x1000 },
	.con1_for_pcie		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_pcie		= { 0x0008, 15, 0, 0x00, 0x0101 },
	.con3_for_pcie		= { 0x000c, 15, 0, 0x00, 0x0200 },
};

static const struct rockchip_combphy_cfg rk3562_combphy_cfgs = {
	.num_phys = 1,
	.phy_ids = {
		0xff750000
	},
	.grfcfg		= &rk3562_combphy_grfcfgs,
	.combphy_cfg	= rk3562_combphy_cfg,
};

static int rk3568_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	unsigned long rate;
	u32 val;

	switch (priv->type) {
	case PHY_TYPE_PCIE:
		/* Set SSC downward spread spectrum. */
		val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;

		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_pcie, true);
		break;

	case PHY_TYPE_USB3:
		/* Set SSC downward spread spectrum. */
		val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT,
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		/* Enable adaptive CTLE for USB3.0 Rx. */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);

		/* Set PLL KVCO fine tuning signals. */
		val = RK3568_PHYREG33_PLL_KVCO_VALUE << RK3568_PHYREG33_PLL_KVCO_SHIFT;
		rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK, val, RK3568_PHYREG33);

		/* Enable controlling random jitter. */
		writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

		/* Set PLL input clock divider 1/2. */
		rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK,
					 RK3568_PHYREG6_PLL_DIV_2 << RK3568_PHYREG6_PLL_DIV_SHIFT,
					 RK3568_PHYREG6);

		writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);
		writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_sel_usb, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txcomp_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txelec_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->usb_mode_set, true);
		switch (priv->id) {
		case 0:
			rockchip_combphy_param_write(priv->pipe_grf, &cfg->u3otg0_port_en, true);
			break;
		case 1:
			rockchip_combphy_param_write(priv->pipe_grf, &cfg->u3otg1_port_en, true);
			break;
		}
		break;

	case PHY_TYPE_SATA:
		/* Enable adaptive CTLE for SATA Rx. */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);
		/*
		 * Set tx_rterm=50ohm and rx_rterm=44ohm for SATA.
		 * 0: 60ohm, 8: 50ohm 15: 44ohm (by step abort 1ohm)
		 */
		val = RK3568_PHYREG7_TX_RTERM_50OHM << RK3568_PHYREG7_TX_RTERM_SHIFT;
		val |= RK3568_PHYREG7_RX_RTERM_44OHM << RK3568_PHYREG7_RX_RTERM_SHIFT;
		writel(val, priv->mmio + RK3568_PHYREG7);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_sata, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_con0_for_sata, true);
		break;

	case PHY_TYPE_SGMII:
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_xpcs_phy_ready, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_phymode_sel, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_sel_qsgmii, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->sgmii_mode_set, true);
		break;

	case PHY_TYPE_QSGMII:
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_xpcs_phy_ready, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_phymode_sel, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_rate_sel, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_sel_qsgmii, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->qsgmii_mode_set, true);
		break;

	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	rate = clk_get_rate(priv->refclk);

	switch (rate) {
	case REF_CLOCK_24MHz:
		if (priv->type == PHY_TYPE_USB3 || priv->type == PHY_TYPE_SATA) {
			/* Set ssc_cnt[9:0]=0101111101 & 31.5KHz. */
			val = RK3568_PHYREG15_SSC_CNT_VALUE << RK3568_PHYREG15_SSC_CNT_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG15_SSC_CNT_MASK,
						 val, RK3568_PHYREG15);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);
		}
		break;

	case REF_CLOCK_25MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_25m, true);
		break;

	case REF_CLOCK_100MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_100m, true);
		if (priv->type == PHY_TYPE_PCIE) {
			/* PLL KVCO  fine tuning. */
			val = RK3568_PHYREG33_PLL_KVCO_VALUE << RK3568_PHYREG33_PLL_KVCO_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Enable controlling random jitter. */
			writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

			val = RK3568_PHYREG6_PLL_DIV_2 << RK3568_PHYREG6_PLL_DIV_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK,
						 val, RK3568_PHYREG6);

			writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);
			writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);
		} else if (priv->type == PHY_TYPE_SATA) {
			/* downward spread spectrum +500ppm */
			val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;
			val |= RK3568_PHYREG32_SSC_OFFSET_500PPM <<
			       RK3568_PHYREG32_SSC_OFFSET_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val,
						 RK3568_PHYREG32);
		}
		break;

	default:
		dev_err(priv->dev, "unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (priv->ext_refclk) {
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_ext, true);
		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_100MHz) {
			val = RK3568_PHYREG13_RESISTER_HIGH_Z << RK3568_PHYREG13_RESISTER_SHIFT;
			val |= RK3568_PHYREG13_CKRCV_AMP0;
			rockchip_combphy_updatel(priv, RK3568_PHYREG13_RESISTER_MASK, val,
						 RK3568_PHYREG13);

			val = readl(priv->mmio + RK3568_PHYREG14);
			val |= RK3568_PHYREG14_CKRCV_AMP1;
			writel(val, priv->mmio + RK3568_PHYREG14);
		}
	}

	if (priv->enable_ssc) {
		val = readl(priv->mmio + RK3568_PHYREG8);
		val |= RK3568_PHYREG8_SSC_EN;
		writel(val, priv->mmio + RK3568_PHYREG8);
	}

	return 0;
}

static const struct rockchip_combphy_grfcfg rk3568_combphy_grfcfgs = {
	/* pipe-phy-grf */
	.pcie_mode_set		= { 0x0000, 5, 0, 0x00, 0x11 },
	.usb_mode_set		= { 0x0000, 5, 0, 0x00, 0x04 },
	.sgmii_mode_set		= { 0x0000, 5, 0, 0x00, 0x01 },
	.qsgmii_mode_set	= { 0x0000, 5, 0, 0x00, 0x21 },
	.pipe_rxterm_set	= { 0x0000, 12, 12, 0x00, 0x01 },
	.pipe_txelec_set	= { 0x0004, 1, 1, 0x00, 0x01 },
	.pipe_txcomp_set	= { 0x0004, 4, 4, 0x00, 0x01 },
	.pipe_clk_25m		= { 0x0004, 14, 13, 0x00, 0x01 },
	.pipe_clk_100m		= { 0x0004, 14, 13, 0x00, 0x02 },
	.pipe_phymode_sel	= { 0x0008, 1, 1, 0x00, 0x01 },
	.pipe_rate_sel		= { 0x0008, 2, 2, 0x00, 0x01 },
	.pipe_rxterm_sel	= { 0x0008, 8, 8, 0x00, 0x01 },
	.pipe_txelec_sel	= { 0x0008, 12, 12, 0x00, 0x01 },
	.pipe_txcomp_sel	= { 0x0008, 15, 15, 0x00, 0x01 },
	.pipe_clk_ext		= { 0x000c, 9, 8, 0x02, 0x01 },
	.pipe_sel_usb		= { 0x000c, 14, 13, 0x00, 0x01 },
	.pipe_sel_qsgmii	= { 0x000c, 15, 13, 0x00, 0x07 },
	.pipe_phy_status	= { 0x0034, 6, 6, 0x01, 0x00 },
	.con0_for_pcie		= { 0x0000, 15, 0, 0x00, 0x1000 },
	.con1_for_pcie		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_pcie		= { 0x0008, 15, 0, 0x00, 0x0101 },
	.con3_for_pcie		= { 0x000c, 15, 0, 0x00, 0x0200 },
	.con0_for_sata		= { 0x0000, 15, 0, 0x00, 0x0119 },
	.con1_for_sata		= { 0x0004, 15, 0, 0x00, 0x0040 },
	.con2_for_sata		= { 0x0008, 15, 0, 0x00, 0x80c3 },
	.con3_for_sata		= { 0x000c, 15, 0, 0x00, 0x4407 },
	/* pipe-grf */
	.pipe_con0_for_sata	= { 0x0000, 15, 0, 0x00, 0x2220 },
	.pipe_xpcs_phy_ready	= { 0x0040, 2, 2, 0x00, 0x01 },
	.u3otg0_port_en		= { 0x0104, 15, 0, 0x0181, 0x1100 },
	.u3otg1_port_en		= { 0x0144, 15, 0, 0x0181, 0x1100 },
};

static const struct rockchip_combphy_cfg rk3568_combphy_cfgs = {
	.num_phys = 3,
	.phy_ids = {
		0xfe820000,
		0xfe830000,
		0xfe840000,
	},
	.grfcfg		= &rk3568_combphy_grfcfgs,
	.combphy_cfg	= rk3568_combphy_cfg,
};

static int rk3576_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	unsigned long rate;
	u32 val;

	switch (priv->type) {
	case PHY_TYPE_PCIE:
		/* Set SSC downward spread spectrum */
		val = FIELD_PREP(RK3568_PHYREG32_SSC_MASK, RK3568_PHYREG32_SSC_DOWNWARD);
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_pcie, true);
		break;

	case PHY_TYPE_USB3:
		/* Set SSC downward spread spectrum */
		val = FIELD_PREP(RK3568_PHYREG32_SSC_MASK, RK3568_PHYREG32_SSC_DOWNWARD);
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		/* Enable adaptive CTLE for USB3.0 Rx */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);

		/* Set PLL KVCO fine tuning signals */
		rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK, BIT(3),
					 RK3568_PHYREG33);

		/* Set PLL LPF R1 to su_trim[10:7]=1001 */
		writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

		/* Set PLL input clock divider 1/2 */
		val = FIELD_PREP(RK3568_PHYREG6_PLL_DIV_MASK, RK3568_PHYREG6_PLL_DIV_2);
		rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK, val, RK3568_PHYREG6);

		/* Set PLL loop divider */
		writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);

		/* Set PLL KVCO to min and set PLL charge pump current to max */
		writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);

		/* Set Rx squelch input filler bandwidth */
		writel(RK3576_PHYREG21_RX_SQUELCH_VAL, priv->mmio + RK3576_PHYREG21);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txcomp_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txelec_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->usb_mode_set, true);
		break;

	case PHY_TYPE_SATA:
		/* Enable adaptive CTLE for SATA Rx */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);

		/* Set tx_rterm = 50 ohm and rx_rterm = 43.5 ohm */
		val = RK3568_PHYREG7_TX_RTERM_50OHM << RK3568_PHYREG7_TX_RTERM_SHIFT;
		val |= RK3568_PHYREG7_RX_RTERM_44OHM << RK3568_PHYREG7_RX_RTERM_SHIFT;
		writel(val, priv->mmio + RK3568_PHYREG7);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_sata, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_con0_for_sata, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_con1_for_sata, true);
		break;

	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	rate = clk_get_rate(priv->refclk);

	switch (rate) {
	case REF_CLOCK_24MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_24m, true);
		if (priv->type == PHY_TYPE_USB3 || priv->type == PHY_TYPE_SATA) {
			/* Set ssc_cnt[9:0]=0101111101 & 31.5KHz */
			val = FIELD_PREP(RK3568_PHYREG15_SSC_CNT_MASK,
					 RK3568_PHYREG15_SSC_CNT_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG15_SSC_CNT_MASK,
						 val, RK3568_PHYREG15);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);
		} else if (priv->type == PHY_TYPE_PCIE) {
			/* PLL KVCO tuning fine */
			val = FIELD_PREP(RK3568_PHYREG33_PLL_KVCO_MASK,
					 RK3576_PHYREG33_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Set up rx_pck invert and rx msb to disable */
			writel(0x00, priv->mmio + RK3588_PHYREG27);

			/*
			 * Set up SU adjust signal:
			 * su_trim[7:0],   PLL KVCO adjust bits[2:0] to min
			 * su_trim[15:8],  PLL LPF R1 adujst bits[9:7]=3'b011
			 * su_trim[31:24], CKDRV adjust
			 */
			writel(0x90, priv->mmio + RK3568_PHYREG11);
			writel(0x02, priv->mmio + RK3568_PHYREG12);
			writel(0x57, priv->mmio + RK3568_PHYREG14);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);
		}
		break;

	case REF_CLOCK_25MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_25m, true);
		break;

	case REF_CLOCK_100MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_100m, true);
		if (priv->type == PHY_TYPE_PCIE) {
			/* gate_tx_pck_sel length select work for L1SS */
			writel(0xc0, priv->mmio + RK3576_PHYREG30);

			/* PLL KVCO tuning fine */
			val = FIELD_PREP(RK3568_PHYREG33_PLL_KVCO_MASK,
					 RK3576_PHYREG33_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Set up rx_trim: PLL LPF C1 85pf R1 1.25kohm */
			writel(0x4c, priv->mmio + RK3588_PHYREG27);

			/*
			 * Set up SU adjust signal:
			 * su_trim[7:0],   PLL KVCO adjust bits[2:0] to min
			 * su_trim[15:8],  bypass PLL loop divider code, and
			 *                 PLL LPF R1 adujst bits[9:7]=3'b101
			 * su_trim[23:16], CKRCV adjust
			 * su_trim[31:24], CKDRV adjust
			 */
			writel(0x90, priv->mmio + RK3568_PHYREG11);
			writel(0x43, priv->mmio + RK3568_PHYREG12);
			writel(0x88, priv->mmio + RK3568_PHYREG13);
			writel(0x56, priv->mmio + RK3568_PHYREG14);
		} else if (priv->type == PHY_TYPE_SATA) {
			/* downward spread spectrum +500ppm */
			val = FIELD_PREP(RK3568_PHYREG32_SSC_DIR_MASK,
					 RK3568_PHYREG32_SSC_DOWNWARD);
			val |= FIELD_PREP(RK3568_PHYREG32_SSC_OFFSET_MASK,
					  RK3568_PHYREG32_SSC_OFFSET_500PPM);
			rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val,
						 RK3568_PHYREG32);

			/* ssc ppm adjust to 3500ppm */
			rockchip_combphy_updatel(priv, RK3576_PHYREG10_SSC_PCM_MASK,
						 RK3576_PHYREG10_SSC_PCM_3500PPM,
						 RK3576_PHYREG10);
		}
		break;

	default:
		dev_err(priv->dev, "Unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (priv->ext_refclk) {
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_ext, true);
		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_100MHz) {
			val = FIELD_PREP(RK3568_PHYREG33_PLL_KVCO_MASK,
					 RK3576_PHYREG33_PLL_KVCO_VALUE);
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Set up rx_trim: PLL LPF C1 85pf R1 2.5kohm */
			writel(0x0c, priv->mmio + RK3588_PHYREG27);

			/*
			 * Set up SU adjust signal:
			 * su_trim[7:0],   PLL KVCO adjust bits[2:0] to min
			 * su_trim[15:8],  bypass PLL loop divider code, and
			 *                 PLL LPF R1 adujst bits[9:7]=3'b101.
			 * su_trim[23:16], CKRCV adjust
			 * su_trim[31:24], CKDRV adjust
			 */
			writel(0x90, priv->mmio + RK3568_PHYREG11);
			writel(0x43, priv->mmio + RK3568_PHYREG12);
			writel(0x88, priv->mmio + RK3568_PHYREG13);
			writel(0x56, priv->mmio + RK3568_PHYREG14);
		}
	}

	if (priv->enable_ssc) {
		val = readl(priv->mmio + RK3568_PHYREG8);
		val |= RK3568_PHYREG8_SSC_EN;
		writel(val, priv->mmio + RK3568_PHYREG8);

		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_24MHz) {
			/* Set PLL loop divider */
			writel(0x00, priv->mmio + RK3576_PHYREG17);
			writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);

			/* Set up rx_pck invert and rx msb to disable */
			writel(0x00, priv->mmio + RK3588_PHYREG27);

			/*
			 * Set up SU adjust signal:
			 * su_trim[7:0],   PLL KVCO adjust bits[2:0] to min
			 * su_trim[15:8],  PLL LPF R1 adujst bits[9:7]=3'b101
			 * su_trim[23:16], CKRCV adjust
			 * su_trim[31:24], CKDRV adjust
			 */
			writel(0x90, priv->mmio + RK3568_PHYREG11);
			writel(0x02, priv->mmio + RK3568_PHYREG12);
			writel(0x08, priv->mmio + RK3568_PHYREG13);
			writel(0x57, priv->mmio + RK3568_PHYREG14);
			writel(0x40, priv->mmio + RK3568_PHYREG15);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);

			val = FIELD_PREP(RK3568_PHYREG33_PLL_KVCO_MASK,
					 RK3576_PHYREG33_PLL_KVCO_VALUE);
			writel(val, priv->mmio + RK3568_PHYREG33);
		}
	}

	return 0;
}

static const struct rockchip_combphy_grfcfg rk3576_combphy_grfcfgs = {
	/* pipe-phy-grf */
	.pcie_mode_set		= { 0x0000, 5, 0, 0x00, 0x11 },
	.usb_mode_set		= { 0x0000, 5, 0, 0x00, 0x04 },
	.pipe_rxterm_set	= { 0x0000, 12, 12, 0x00, 0x01 },
	.pipe_txelec_set	= { 0x0004, 1, 1, 0x00, 0x01 },
	.pipe_txcomp_set	= { 0x0004, 4, 4, 0x00, 0x01 },
	.pipe_clk_24m		= { 0x0004, 14, 13, 0x00, 0x00 },
	.pipe_clk_25m		= { 0x0004, 14, 13, 0x00, 0x01 },
	.pipe_clk_100m		= { 0x0004, 14, 13, 0x00, 0x02 },
	.pipe_phymode_sel	= { 0x0008, 1, 1, 0x00, 0x01 },
	.pipe_rate_sel		= { 0x0008, 2, 2, 0x00, 0x01 },
	.pipe_rxterm_sel	= { 0x0008, 8, 8, 0x00, 0x01 },
	.pipe_txelec_sel	= { 0x0008, 12, 12, 0x00, 0x01 },
	.pipe_txcomp_sel	= { 0x0008, 15, 15, 0x00, 0x01 },
	.pipe_clk_ext		= { 0x000c, 9, 8, 0x02, 0x01 },
	.pipe_phy_status	= { 0x0034, 6, 6, 0x01, 0x00 },
	.con0_for_pcie		= { 0x0000, 15, 0, 0x00, 0x1000 },
	.con1_for_pcie		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_pcie		= { 0x0008, 15, 0, 0x00, 0x0101 },
	.con3_for_pcie		= { 0x000c, 15, 0, 0x00, 0x0200 },
	.con0_for_sata		= { 0x0000, 15, 0, 0x00, 0x0129 },
	.con1_for_sata		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_sata		= { 0x0008, 15, 0, 0x00, 0x80c1 },
	.con3_for_sata		= { 0x000c, 15, 0, 0x00, 0x0407 },
	/* php-grf */
	.pipe_con0_for_sata	= { 0x001C, 2, 0, 0x00, 0x2 },
	.pipe_con1_for_sata	= { 0x0020, 2, 0, 0x00, 0x2 },
};

static const struct rockchip_combphy_cfg rk3576_combphy_cfgs = {
	.num_phys = 2,
	.phy_ids = {
		0x2b050000,
		0x2b060000
	},
	.grfcfg		= &rk3576_combphy_grfcfgs,
	.combphy_cfg	= rk3576_combphy_cfg,
};

static int rk3588_combphy_cfg(struct rockchip_combphy_priv *priv)
{
	const struct rockchip_combphy_grfcfg *cfg = priv->cfg->grfcfg;
	unsigned long rate;
	u32 val;

	switch (priv->type) {
	case PHY_TYPE_PCIE:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_pcie, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_pcie, true);
		switch (priv->id) {
		case 1:
			rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_pcie1l0_sel, true);
			break;
		case 2:
			rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_pcie1l1_sel, true);
			break;
		}
		break;
	case PHY_TYPE_USB3:
		/* Set SSC downward spread spectrum */
		val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;
		rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val, RK3568_PHYREG32);

		/* Enable adaptive CTLE for USB3.0 Rx. */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);

		/* Set PLL KVCO fine tuning signals. */
		val = RK3568_PHYREG33_PLL_KVCO_VALUE << RK3568_PHYREG33_PLL_KVCO_SHIFT;
		rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK, val, RK3568_PHYREG33);

		/* Enable controlling random jitter. */
		writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

		/* Set PLL input clock divider 1/2. */
		rockchip_combphy_updatel(priv, RK3568_PHYREG6_PLL_DIV_MASK,
					 RK3568_PHYREG6_PLL_DIV_2 << RK3568_PHYREG6_PLL_DIV_SHIFT,
					 RK3568_PHYREG6);

		writel(RK3568_PHYREG18_PLL_LOOP, priv->mmio + RK3568_PHYREG18);
		writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txcomp_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_txelec_sel, false);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->usb_mode_set, true);
		break;
	case PHY_TYPE_SATA:
		/* Enable adaptive CTLE for SATA Rx. */
		val = readl(priv->mmio + RK3568_PHYREG15);
		val |= RK3568_PHYREG15_CTLE_EN;
		writel(val, priv->mmio + RK3568_PHYREG15);
		/*
		 * Set tx_rterm=50ohm and rx_rterm=44ohm for SATA.
		 * 0: 60ohm, 8: 50ohm 15: 44ohm (by step abort 1ohm)
		 */
		val = RK3568_PHYREG7_TX_RTERM_50OHM << RK3568_PHYREG7_TX_RTERM_SHIFT;
		val |= RK3568_PHYREG7_RX_RTERM_44OHM << RK3568_PHYREG7_RX_RTERM_SHIFT;
		writel(val, priv->mmio + RK3568_PHYREG7);

		rockchip_combphy_param_write(priv->phy_grf, &cfg->con0_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con1_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con2_for_sata, true);
		rockchip_combphy_param_write(priv->phy_grf, &cfg->con3_for_sata, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_con0_for_sata, true);
		rockchip_combphy_param_write(priv->pipe_grf, &cfg->pipe_con1_for_sata, true);
		break;
	case PHY_TYPE_SGMII:
	case PHY_TYPE_QSGMII:
	default:
		dev_err(priv->dev, "incompatible PHY type\n");
		return -EINVAL;
	}

	rate = clk_get_rate(priv->refclk);

	switch (rate) {
	case REF_CLOCK_24MHz:
		if (priv->type == PHY_TYPE_USB3 || priv->type == PHY_TYPE_SATA) {
			/* Set ssc_cnt[9:0]=0101111101 & 31.5KHz. */
			val = RK3568_PHYREG15_SSC_CNT_VALUE << RK3568_PHYREG15_SSC_CNT_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG15_SSC_CNT_MASK,
						 val, RK3568_PHYREG15);

			writel(RK3568_PHYREG16_SSC_CNT_VALUE, priv->mmio + RK3568_PHYREG16);
		}
		break;

	case REF_CLOCK_25MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_25m, true);
		break;
	case REF_CLOCK_100MHz:
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_100m, true);
		if (priv->type == PHY_TYPE_PCIE) {
			/* PLL KVCO fine tuning. */
			val = 4 << RK3568_PHYREG33_PLL_KVCO_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG33_PLL_KVCO_MASK,
						 val, RK3568_PHYREG33);

			/* Enable controlling random jitter. */
			writel(RK3568_PHYREG12_PLL_LPF_ADJ_VALUE, priv->mmio + RK3568_PHYREG12);

			/* Set up rx_trim: PLL LPF C1 85pf R1 1.25kohm */
			writel(RK3588_PHYREG27_RX_TRIM, priv->mmio + RK3588_PHYREG27);

			/* Set up su_trim:  */
			writel(RK3568_PHYREG11_SU_TRIM_0_7, priv->mmio + RK3568_PHYREG11);
		} else if (priv->type == PHY_TYPE_SATA) {
			/* downward spread spectrum +500ppm */
			val = RK3568_PHYREG32_SSC_DOWNWARD << RK3568_PHYREG32_SSC_DIR_SHIFT;
			val |= RK3568_PHYREG32_SSC_OFFSET_500PPM <<
			       RK3568_PHYREG32_SSC_OFFSET_SHIFT;
			rockchip_combphy_updatel(priv, RK3568_PHYREG32_SSC_MASK, val,
						 RK3568_PHYREG32);
		}
		break;
	default:
		dev_err(priv->dev, "Unsupported rate: %lu\n", rate);
		return -EINVAL;
	}

	if (priv->ext_refclk) {
		rockchip_combphy_param_write(priv->phy_grf, &cfg->pipe_clk_ext, true);
		if (priv->type == PHY_TYPE_PCIE && rate == REF_CLOCK_100MHz) {
			val = RK3568_PHYREG13_RESISTER_HIGH_Z << RK3568_PHYREG13_RESISTER_SHIFT;
			val |= RK3568_PHYREG13_CKRCV_AMP0;
			rockchip_combphy_updatel(priv, RK3568_PHYREG13_RESISTER_MASK, val,
						 RK3568_PHYREG13);

			val = readl(priv->mmio + RK3568_PHYREG14);
			val |= RK3568_PHYREG14_CKRCV_AMP1;
			writel(val, priv->mmio + RK3568_PHYREG14);
		}
	}

	if (priv->enable_ssc) {
		val = readl(priv->mmio + RK3568_PHYREG8);
		val |= RK3568_PHYREG8_SSC_EN;
		writel(val, priv->mmio + RK3568_PHYREG8);
	}

	return 0;
}

static const struct rockchip_combphy_grfcfg rk3588_combphy_grfcfgs = {
	/* pipe-phy-grf */
	.pcie_mode_set		= { 0x0000, 5, 0, 0x00, 0x11 },
	.usb_mode_set		= { 0x0000, 5, 0, 0x00, 0x04 },
	.pipe_rxterm_set	= { 0x0000, 12, 12, 0x00, 0x01 },
	.pipe_txelec_set	= { 0x0004, 1, 1, 0x00, 0x01 },
	.pipe_txcomp_set	= { 0x0004, 4, 4, 0x00, 0x01 },
	.pipe_clk_25m		= { 0x0004, 14, 13, 0x00, 0x01 },
	.pipe_clk_100m		= { 0x0004, 14, 13, 0x00, 0x02 },
	.pipe_rxterm_sel	= { 0x0008, 8, 8, 0x00, 0x01 },
	.pipe_txelec_sel	= { 0x0008, 12, 12, 0x00, 0x01 },
	.pipe_txcomp_sel	= { 0x0008, 15, 15, 0x00, 0x01 },
	.pipe_clk_ext		= { 0x000c, 9, 8, 0x02, 0x01 },
	.pipe_phy_status	= { 0x0034, 6, 6, 0x01, 0x00 },
	.con0_for_pcie		= { 0x0000, 15, 0, 0x00, 0x1000 },
	.con1_for_pcie		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_pcie		= { 0x0008, 15, 0, 0x00, 0x0101 },
	.con3_for_pcie		= { 0x000c, 15, 0, 0x00, 0x0200 },
	.con0_for_sata		= { 0x0000, 15, 0, 0x00, 0x0129 },
	.con1_for_sata		= { 0x0004, 15, 0, 0x00, 0x0000 },
	.con2_for_sata		= { 0x0008, 15, 0, 0x00, 0x80c1 },
	.con3_for_sata		= { 0x000c, 15, 0, 0x00, 0x0407 },
	/* pipe-grf */
	.pipe_con0_for_sata	= { 0x0000, 11, 5, 0x00, 0x22 },
	.pipe_con1_for_sata	= { 0x0000, 2, 0, 0x00, 0x2 },
	.pipe_pcie1l0_sel	= { 0x0100, 0, 0, 0x01, 0x0 },
	.pipe_pcie1l1_sel	= { 0x0100, 1, 1, 0x01, 0x0 },
};

static const struct rockchip_combphy_cfg rk3588_combphy_cfgs = {
	.num_phys = 3,
	.phy_ids = {
		0xfee00000,
		0xfee10000,
		0xfee20000,
	},
	.grfcfg		= &rk3588_combphy_grfcfgs,
	.combphy_cfg	= rk3588_combphy_cfg,
};

static const struct of_device_id rockchip_combphy_of_match[] = {
	{
		.compatible = "rockchip,rk3528-naneng-combphy",
		.data = &rk3528_combphy_cfgs,
	},
	{
		.compatible = "rockchip,rk3562-naneng-combphy",
		.data = &rk3562_combphy_cfgs,
	},
	{
		.compatible = "rockchip,rk3568-naneng-combphy",
		.data = &rk3568_combphy_cfgs,
	},
	{
		.compatible = "rockchip,rk3576-naneng-combphy",
		.data = &rk3576_combphy_cfgs,
	},
	{
		.compatible = "rockchip,rk3588-naneng-combphy",
		.data = &rk3588_combphy_cfgs,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_combphy_of_match);

static struct platform_driver rockchip_combphy_driver = {
	.probe	= rockchip_combphy_probe,
	.driver = {
		.name = "rockchip-naneng-combphy",
		.of_match_table = rockchip_combphy_of_match,
	},
};
module_platform_driver(rockchip_combphy_driver);

MODULE_DESCRIPTION("Rockchip NANENG COMBPHY driver");
MODULE_LICENSE("GPL v2");
