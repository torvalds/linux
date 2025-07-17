// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2017 NXP. */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/typec_mux.h>

#define PHY_CTRL0			0x0
#define PHY_CTRL0_REF_SSP_EN		BIT(2)
#define PHY_CTRL0_FSEL_MASK		GENMASK(10, 5)
#define PHY_CTRL0_FSEL_24M		0x2a

#define PHY_CTRL1			0x4
#define PHY_CTRL1_RESET			BIT(0)
#define PHY_CTRL1_COMMONONN		BIT(1)
#define PHY_CTRL1_ATERESET		BIT(3)
#define PHY_CTRL1_VDATSRCENB0		BIT(19)
#define PHY_CTRL1_VDATDETENB0		BIT(20)

#define PHY_CTRL2			0x8
#define PHY_CTRL2_TXENABLEN0		BIT(8)
#define PHY_CTRL2_OTG_DISABLE		BIT(9)

#define PHY_CTRL3			0xc
#define PHY_CTRL3_COMPDISTUNE_MASK	GENMASK(2, 0)
#define PHY_CTRL3_TXPREEMP_TUNE_MASK	GENMASK(16, 15)
#define PHY_CTRL3_TXRISE_TUNE_MASK	GENMASK(21, 20)
#define PHY_CTRL3_TXVREF_TUNE_MASK	GENMASK(25, 22)
#define PHY_CTRL3_TX_VBOOST_LEVEL_MASK	GENMASK(31, 29)

#define PHY_CTRL4			0x10
#define PHY_CTRL4_PCS_TX_DEEMPH_3P5DB_MASK	GENMASK(20, 15)

#define PHY_CTRL5			0x14
#define PHY_CTRL5_DMPWD_OVERRIDE_SEL	BIT(23)
#define PHY_CTRL5_DMPWD_OVERRIDE	BIT(22)
#define PHY_CTRL5_DPPWD_OVERRIDE_SEL	BIT(21)
#define PHY_CTRL5_DPPWD_OVERRIDE	BIT(20)
#define PHY_CTRL5_PCS_TX_SWING_FULL_MASK	GENMASK(6, 0)

#define PHY_CTRL6			0x18
#define PHY_CTRL6_ALT_CLK_EN		BIT(1)
#define PHY_CTRL6_ALT_CLK_SEL		BIT(0)

#define PHY_TUNE_DEFAULT		0xffffffff

#define TCA_CLK_RST			0x00
#define TCA_CLK_RST_SW			BIT(9)
#define TCA_CLK_RST_REF_CLK_EN		BIT(1)
#define TCA_CLK_RST_SUSPEND_CLK_EN	BIT(0)

#define TCA_INTR_EN			0x04
#define TCA_INTR_STS			0x08

#define TCA_GCFG			0x10
#define TCA_GCFG_ROLE_HSTDEV		BIT(4)
#define TCA_GCFG_OP_MODE		GENMASK(1, 0)
#define TCA_GCFG_OP_MODE_SYSMODE	0
#define TCA_GCFG_OP_MODE_SYNCMODE	1

#define TCA_TCPC			0x14
#define TCA_TCPC_VALID			BIT(4)
#define TCA_TCPC_LOW_POWER_EN		BIT(3)
#define TCA_TCPC_ORIENTATION_NORMAL	BIT(2)
#define TCA_TCPC_MUX_CONTRL		GENMASK(1, 0)
#define TCA_TCPC_MUX_CONTRL_NO_CONN	0
#define TCA_TCPC_MUX_CONTRL_USB_CONN	1

#define TCA_SYSMODE_CFG			0x18
#define TCA_SYSMODE_TCPC_DISABLE	BIT(3)
#define TCA_SYSMODE_TCPC_FLIP		BIT(2)

#define TCA_CTRLSYNCMODE_CFG0		0x20
#define TCA_CTRLSYNCMODE_CFG1           0x20

#define TCA_PSTATE			0x30
#define TCA_PSTATE_CM_STS		BIT(4)
#define TCA_PSTATE_TX_STS		BIT(3)
#define TCA_PSTATE_RX_PLL_STS		BIT(2)
#define TCA_PSTATE_PIPE0_POWER_DOWN	GENMASK(1, 0)

#define TCA_GEN_STATUS			0x34
#define TCA_GEN_DEV_POR			BIT(12)
#define TCA_GEN_REF_CLK_SEL		BIT(8)
#define TCA_GEN_TYPEC_FLIP_INVERT	BIT(4)
#define TCA_GEN_PHY_TYPEC_DISABLE	BIT(3)
#define TCA_GEN_PHY_TYPEC_FLIP		BIT(2)

#define TCA_VBUS_CTRL			0x40
#define TCA_VBUS_STATUS			0x44

#define TCA_INFO			0xfc

struct tca_blk {
	struct typec_switch_dev *sw;
	void __iomem *base;
	struct mutex mutex;
	enum typec_orientation orientation;
};

struct imx8mq_usb_phy {
	struct phy *phy;
	struct clk *clk;
	void __iomem *base;
	struct regulator *vbus;
	struct tca_blk *tca;
	u32 pcs_tx_swing_full;
	u32 pcs_tx_deemph_3p5db;
	u32 tx_vref_tune;
	u32 tx_rise_tune;
	u32 tx_preemp_amp_tune;
	u32 tx_vboost_level;
	u32 comp_dis_tune;
};


static void tca_blk_orientation_set(struct tca_blk *tca,
				enum typec_orientation orientation);

#ifdef CONFIG_TYPEC

static int tca_blk_typec_switch_set(struct typec_switch_dev *sw,
				enum typec_orientation orientation)
{
	struct imx8mq_usb_phy *imx_phy = typec_switch_get_drvdata(sw);
	struct tca_blk *tca = imx_phy->tca;
	int ret;

	if (tca->orientation == orientation)
		return 0;

	ret = clk_prepare_enable(imx_phy->clk);
	if (ret)
		return ret;

	tca_blk_orientation_set(tca, orientation);
	clk_disable_unprepare(imx_phy->clk);

	return 0;
}

static struct typec_switch_dev *tca_blk_get_typec_switch(struct platform_device *pdev,
					struct imx8mq_usb_phy *imx_phy)
{
	struct device *dev = &pdev->dev;
	struct typec_switch_dev *sw;
	struct typec_switch_desc sw_desc = { };

	sw_desc.drvdata = imx_phy;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = tca_blk_typec_switch_set;
	sw_desc.name = NULL;

	sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(sw)) {
		dev_err(dev, "Error register tca orientation switch: %ld",
				PTR_ERR(sw));
		return NULL;
	}

	return sw;
}

static void tca_blk_put_typec_switch(struct typec_switch_dev *sw)
{
	typec_switch_unregister(sw);
}

#else

static struct typec_switch_dev *tca_blk_get_typec_switch(struct platform_device *pdev,
			struct imx8mq_usb_phy *imx_phy)
{
	return NULL;
}

static void tca_blk_put_typec_switch(struct typec_switch_dev *sw) {}

#endif /* CONFIG_TYPEC */

static void tca_blk_orientation_set(struct tca_blk *tca,
				enum typec_orientation orientation)
{
	u32 val;

	mutex_lock(&tca->mutex);

	if (orientation == TYPEC_ORIENTATION_NONE) {
		/*
		 * use Controller Synced Mode for TCA low power enable and
		 * put PHY to USB safe state.
		 */
		val = FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYNCMODE);
		writel(val, tca->base + TCA_GCFG);

		val = TCA_TCPC_VALID | TCA_TCPC_LOW_POWER_EN;
		writel(val, tca->base + TCA_TCPC);

		goto out;
	}

	/* use System Configuration Mode for TCA mux control. */
	val = FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYSMODE);
	writel(val, tca->base + TCA_GCFG);

	/* Disable TCA module */
	val = readl(tca->base + TCA_SYSMODE_CFG);
	val |= TCA_SYSMODE_TCPC_DISABLE;
	writel(val, tca->base + TCA_SYSMODE_CFG);

	if (orientation == TYPEC_ORIENTATION_REVERSE)
		val |= TCA_SYSMODE_TCPC_FLIP;
	else if (orientation == TYPEC_ORIENTATION_NORMAL)
		val &= ~TCA_SYSMODE_TCPC_FLIP;

	writel(val, tca->base + TCA_SYSMODE_CFG);

	/* Enable TCA module */
	val &= ~TCA_SYSMODE_TCPC_DISABLE;
	writel(val, tca->base + TCA_SYSMODE_CFG);

out:
	tca->orientation = orientation;
	mutex_unlock(&tca->mutex);
}

static void tca_blk_init(struct tca_blk *tca)
{
	u32 val;

	/* reset XBar block */
	val = readl(tca->base + TCA_CLK_RST);
	val &= ~TCA_CLK_RST_SW;
	writel(val, tca->base + TCA_CLK_RST);

	udelay(100);

	/* clear reset */
	val |= TCA_CLK_RST_SW;
	writel(val, tca->base + TCA_CLK_RST);

	tca_blk_orientation_set(tca, tca->orientation);
}

static struct tca_blk *imx95_usb_phy_get_tca(struct platform_device *pdev,
				struct imx8mq_usb_phy *imx_phy)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct tca_blk *tca;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return NULL;

	tca = devm_kzalloc(dev, sizeof(*tca), GFP_KERNEL);
	if (!tca)
		return ERR_PTR(-ENOMEM);

	tca->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tca->base))
		return ERR_CAST(tca->base);

	mutex_init(&tca->mutex);

	tca->orientation = TYPEC_ORIENTATION_NORMAL;
	tca->sw = tca_blk_get_typec_switch(pdev, imx_phy);

	return tca;
}

static void imx95_usb_phy_put_tca(struct imx8mq_usb_phy *imx_phy)
{
	struct tca_blk *tca = imx_phy->tca;

	if (!tca)
		return;

	tca_blk_put_typec_switch(tca->sw);
}

static u32 phy_tx_vref_tune_from_property(u32 percent)
{
	percent = clamp(percent, 94U, 124U);

	return DIV_ROUND_CLOSEST(percent - 94U, 2);
}

static u32 imx95_phy_tx_vref_tune_from_property(u32 percent)
{
	percent = clamp(percent, 90U, 108U);

	switch (percent) {
	case 90 ... 91:
		percent = 0;
		break;
	case 92 ... 96:
		percent -= 91;
		break;
	case 97 ... 104:
		percent -= 92;
		break;
	case 105 ... 108:
		percent -= 93;
		break;
	}

	return percent;
}

static u32 phy_tx_rise_tune_from_property(u32 percent)
{
	switch (percent) {
	case 0 ... 98:
		return 3;
	case 99:
		return 2;
	case 100 ... 101:
		return 1;
	default:
		return 0;
	}
}

static u32 imx95_phy_tx_rise_tune_from_property(u32 percent)
{
	percent = clamp(percent, 90U, 120U);

	switch (percent) {
	case 90 ... 99:
		return 3;
	case 101 ... 115:
		return 1;
	case 116 ... 120:
		return 0;
	default:
		return 2;
	}
}

static u32 phy_tx_preemp_amp_tune_from_property(u32 microamp)
{
	microamp = min(microamp, 1800U);

	return microamp / 600;
}

static u32 phy_tx_vboost_level_from_property(u32 microvolt)
{
	switch (microvolt) {
	case 1156:
		return 5;
	case 844:
		return 3;
	default:
		return 4;
	}
}

static u32 phy_pcs_tx_deemph_3p5db_from_property(u32 decibel)
{
	return min(decibel, 36U);
}

static u32 phy_comp_dis_tune_from_property(u32 percent)
{
	switch (percent) {
	case 0 ... 92:
		return 0;
	case 93 ... 95:
		return 1;
	case 96 ... 97:
		return 2;
	case 98 ... 102:
		return 3;
	case 103 ... 105:
		return 4;
	case 106 ... 109:
		return 5;
	case 110 ... 113:
		return 6;
	default:
		return 7;
	}
}

static u32 imx95_phy_comp_dis_tune_from_property(u32 percent)
{
	percent = clamp(percent, 94, 104);

	switch (percent) {
	case 94 ... 95:
		percent = 0;
		break;
	case 96 ... 98:
		percent -= 95;
		break;
	case 99 ... 102:
		percent -= 96;
		break;
	case 103 ... 104:
		percent -= 97;
		break;
	}

	return percent;
}

static u32 phy_pcs_tx_swing_full_from_property(u32 percent)
{
	percent = min(percent, 100U);

	return (percent * 127) / 100;
}

static void imx8m_get_phy_tuning_data(struct imx8mq_usb_phy *imx_phy)
{
	struct device *dev = imx_phy->phy->dev.parent;
	bool is_imx95 = false;

	if (device_is_compatible(dev, "fsl,imx95-usb-phy"))
		is_imx95 = true;

	if (device_property_read_u32(dev, "fsl,phy-tx-vref-tune-percent",
				     &imx_phy->tx_vref_tune))
		imx_phy->tx_vref_tune = PHY_TUNE_DEFAULT;
	else if (is_imx95)
		imx_phy->tx_vref_tune =
			imx95_phy_tx_vref_tune_from_property(imx_phy->tx_vref_tune);
	else
		imx_phy->tx_vref_tune =
			phy_tx_vref_tune_from_property(imx_phy->tx_vref_tune);

	if (device_property_read_u32(dev, "fsl,phy-tx-rise-tune-percent",
				     &imx_phy->tx_rise_tune))
		imx_phy->tx_rise_tune = PHY_TUNE_DEFAULT;
	else if (is_imx95)
		imx_phy->tx_rise_tune =
			imx95_phy_tx_rise_tune_from_property(imx_phy->tx_rise_tune);
	else
		imx_phy->tx_rise_tune =
			phy_tx_rise_tune_from_property(imx_phy->tx_rise_tune);

	if (device_property_read_u32(dev, "fsl,phy-tx-preemp-amp-tune-microamp",
				     &imx_phy->tx_preemp_amp_tune))
		imx_phy->tx_preemp_amp_tune = PHY_TUNE_DEFAULT;
	else
		imx_phy->tx_preemp_amp_tune =
			phy_tx_preemp_amp_tune_from_property(imx_phy->tx_preemp_amp_tune);

	if (device_property_read_u32(dev, "fsl,phy-tx-vboost-level-microvolt",
				     &imx_phy->tx_vboost_level))
		imx_phy->tx_vboost_level = PHY_TUNE_DEFAULT;
	else
		imx_phy->tx_vboost_level =
			phy_tx_vboost_level_from_property(imx_phy->tx_vboost_level);

	if (device_property_read_u32(dev, "fsl,phy-comp-dis-tune-percent",
				     &imx_phy->comp_dis_tune))
		imx_phy->comp_dis_tune = PHY_TUNE_DEFAULT;
	else if (is_imx95)
		imx_phy->comp_dis_tune =
			imx95_phy_comp_dis_tune_from_property(imx_phy->comp_dis_tune);
	else
		imx_phy->comp_dis_tune =
			phy_comp_dis_tune_from_property(imx_phy->comp_dis_tune);

	if (device_property_read_u32(dev, "fsl,phy-pcs-tx-deemph-3p5db-attenuation-db",
				     &imx_phy->pcs_tx_deemph_3p5db))
		imx_phy->pcs_tx_deemph_3p5db = PHY_TUNE_DEFAULT;
	else
		imx_phy->pcs_tx_deemph_3p5db =
			phy_pcs_tx_deemph_3p5db_from_property(imx_phy->pcs_tx_deemph_3p5db);

	if (device_property_read_u32(dev, "fsl,phy-pcs-tx-swing-full-percent",
				     &imx_phy->pcs_tx_swing_full))
		imx_phy->pcs_tx_swing_full = PHY_TUNE_DEFAULT;
	else
		imx_phy->pcs_tx_swing_full =
			phy_pcs_tx_swing_full_from_property(imx_phy->pcs_tx_swing_full);
}

static void imx8m_phy_tune(struct imx8mq_usb_phy *imx_phy)
{
	u32 value;

	/* PHY tuning */
	if (imx_phy->pcs_tx_deemph_3p5db != PHY_TUNE_DEFAULT) {
		value = readl(imx_phy->base + PHY_CTRL4);
		value &= ~PHY_CTRL4_PCS_TX_DEEMPH_3P5DB_MASK;
		value |= FIELD_PREP(PHY_CTRL4_PCS_TX_DEEMPH_3P5DB_MASK,
				   imx_phy->pcs_tx_deemph_3p5db);
		writel(value, imx_phy->base + PHY_CTRL4);
	}

	if (imx_phy->pcs_tx_swing_full != PHY_TUNE_DEFAULT) {
		value = readl(imx_phy->base + PHY_CTRL5);
		value |= FIELD_PREP(PHY_CTRL5_PCS_TX_SWING_FULL_MASK,
				   imx_phy->pcs_tx_swing_full);
		writel(value, imx_phy->base + PHY_CTRL5);
	}

	if ((imx_phy->tx_vref_tune & imx_phy->tx_rise_tune &
	     imx_phy->tx_preemp_amp_tune & imx_phy->comp_dis_tune &
	     imx_phy->tx_vboost_level) == PHY_TUNE_DEFAULT)
		/* If all are the default values, no need update. */
		return;

	value = readl(imx_phy->base + PHY_CTRL3);

	if (imx_phy->tx_vref_tune != PHY_TUNE_DEFAULT) {
		value &= ~PHY_CTRL3_TXVREF_TUNE_MASK;
		value |= FIELD_PREP(PHY_CTRL3_TXVREF_TUNE_MASK,
				   imx_phy->tx_vref_tune);
	}

	if (imx_phy->tx_rise_tune != PHY_TUNE_DEFAULT) {
		value &= ~PHY_CTRL3_TXRISE_TUNE_MASK;
		value |= FIELD_PREP(PHY_CTRL3_TXRISE_TUNE_MASK,
				    imx_phy->tx_rise_tune);
	}

	if (imx_phy->tx_preemp_amp_tune != PHY_TUNE_DEFAULT) {
		value &= ~PHY_CTRL3_TXPREEMP_TUNE_MASK;
		value |= FIELD_PREP(PHY_CTRL3_TXPREEMP_TUNE_MASK,
				imx_phy->tx_preemp_amp_tune);
	}

	if (imx_phy->comp_dis_tune != PHY_TUNE_DEFAULT) {
		value &= ~PHY_CTRL3_COMPDISTUNE_MASK;
		value |= FIELD_PREP(PHY_CTRL3_COMPDISTUNE_MASK,
				    imx_phy->comp_dis_tune);
	}

	if (imx_phy->tx_vboost_level != PHY_TUNE_DEFAULT) {
		value &= ~PHY_CTRL3_TX_VBOOST_LEVEL_MASK;
		value |= FIELD_PREP(PHY_CTRL3_TX_VBOOST_LEVEL_MASK,
				    imx_phy->tx_vboost_level);
	}

	writel(value, imx_phy->base + PHY_CTRL3);
}

static int imx8mq_usb_phy_init(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);
	u32 value;

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_VDATSRCENB0 | PHY_CTRL1_VDATDETENB0 |
		   PHY_CTRL1_COMMONONN);
	value |= PHY_CTRL1_RESET | PHY_CTRL1_ATERESET;
	writel(value, imx_phy->base + PHY_CTRL1);

	value = readl(imx_phy->base + PHY_CTRL0);
	value |= PHY_CTRL0_REF_SSP_EN;
	writel(value, imx_phy->base + PHY_CTRL0);

	value = readl(imx_phy->base + PHY_CTRL2);
	value |= PHY_CTRL2_TXENABLEN0;
	writel(value, imx_phy->base + PHY_CTRL2);

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_RESET | PHY_CTRL1_ATERESET);
	writel(value, imx_phy->base + PHY_CTRL1);

	return 0;
}

static int imx8mp_usb_phy_init(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);
	u32 value;

	/* USB3.0 PHY signal fsel for 24M ref */
	value = readl(imx_phy->base + PHY_CTRL0);
	value &= ~PHY_CTRL0_FSEL_MASK;
	value |= FIELD_PREP(PHY_CTRL0_FSEL_MASK, PHY_CTRL0_FSEL_24M);
	writel(value, imx_phy->base + PHY_CTRL0);

	/* Disable alt_clk_en and use internal MPLL clocks */
	value = readl(imx_phy->base + PHY_CTRL6);
	value &= ~(PHY_CTRL6_ALT_CLK_SEL | PHY_CTRL6_ALT_CLK_EN);
	writel(value, imx_phy->base + PHY_CTRL6);

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_VDATSRCENB0 | PHY_CTRL1_VDATDETENB0);
	value |= PHY_CTRL1_RESET | PHY_CTRL1_ATERESET;
	writel(value, imx_phy->base + PHY_CTRL1);

	value = readl(imx_phy->base + PHY_CTRL0);
	value |= PHY_CTRL0_REF_SSP_EN;
	writel(value, imx_phy->base + PHY_CTRL0);

	value = readl(imx_phy->base + PHY_CTRL2);
	value |= PHY_CTRL2_TXENABLEN0 | PHY_CTRL2_OTG_DISABLE;
	writel(value, imx_phy->base + PHY_CTRL2);

	udelay(10);

	value = readl(imx_phy->base + PHY_CTRL1);
	value &= ~(PHY_CTRL1_RESET | PHY_CTRL1_ATERESET);
	writel(value, imx_phy->base + PHY_CTRL1);

	imx8m_phy_tune(imx_phy);

	if (imx_phy->tca)
		tca_blk_init(imx_phy->tca);

	return 0;
}

static int imx8mq_phy_power_on(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);
	int ret;

	ret = regulator_enable(imx_phy->vbus);
	if (ret)
		return ret;

	return clk_prepare_enable(imx_phy->clk);
}

static int imx8mq_phy_power_off(struct phy *phy)
{
	struct imx8mq_usb_phy *imx_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(imx_phy->clk);
	regulator_disable(imx_phy->vbus);

	return 0;
}

static const struct phy_ops imx8mq_usb_phy_ops = {
	.init		= imx8mq_usb_phy_init,
	.power_on	= imx8mq_phy_power_on,
	.power_off	= imx8mq_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct phy_ops imx8mp_usb_phy_ops = {
	.init		= imx8mp_usb_phy_init,
	.power_on	= imx8mq_phy_power_on,
	.power_off	= imx8mq_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id imx8mq_usb_phy_of_match[] = {
	{.compatible = "fsl,imx8mq-usb-phy",
	 .data = &imx8mq_usb_phy_ops,},
	{.compatible = "fsl,imx8mp-usb-phy",
	 .data = &imx8mp_usb_phy_ops,},
	{.compatible = "fsl,imx95-usb-phy",
	 .data = &imx8mp_usb_phy_ops,},
	{ }
};
MODULE_DEVICE_TABLE(of, imx8mq_usb_phy_of_match);

static int imx8mq_usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct imx8mq_usb_phy *imx_phy;
	const struct phy_ops *phy_ops;

	imx_phy = devm_kzalloc(dev, sizeof(*imx_phy), GFP_KERNEL);
	if (!imx_phy)
		return -ENOMEM;

	imx_phy->clk = devm_clk_get(dev, "phy");
	if (IS_ERR(imx_phy->clk)) {
		dev_err(dev, "failed to get imx8mq usb phy clock\n");
		return PTR_ERR(imx_phy->clk);
	}

	imx_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(imx_phy->base))
		return PTR_ERR(imx_phy->base);

	phy_ops = of_device_get_match_data(dev);
	if (!phy_ops)
		return -EINVAL;

	imx_phy->phy = devm_phy_create(dev, NULL, phy_ops);
	if (IS_ERR(imx_phy->phy))
		return PTR_ERR(imx_phy->phy);

	imx_phy->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(imx_phy->vbus))
		return dev_err_probe(dev, PTR_ERR(imx_phy->vbus), "failed to get vbus\n");

	phy_set_drvdata(imx_phy->phy, imx_phy);

	imx_phy->tca = imx95_usb_phy_get_tca(pdev, imx_phy);
	if (IS_ERR(imx_phy->tca))
		return dev_err_probe(dev, PTR_ERR(imx_phy->tca),
					"failed to get tca\n");

	imx8m_get_phy_tuning_data(imx_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static void imx8mq_usb_phy_remove(struct platform_device *pdev)
{
	struct imx8mq_usb_phy *imx_phy = platform_get_drvdata(pdev);

	imx95_usb_phy_put_tca(imx_phy);
}

static struct platform_driver imx8mq_usb_phy_driver = {
	.probe	= imx8mq_usb_phy_probe,
	.remove = imx8mq_usb_phy_remove,
	.driver = {
		.name	= "imx8mq-usb-phy",
		.of_match_table	= imx8mq_usb_phy_of_match,
	}
};
module_platform_driver(imx8mq_usb_phy_driver);

MODULE_DESCRIPTION("FSL IMX8MQ USB PHY driver");
MODULE_LICENSE("GPL");
