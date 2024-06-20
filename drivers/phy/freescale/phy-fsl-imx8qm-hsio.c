// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pci_regs.h>
#include <linux/phy/phy.h>
#include <linux/phy/pcie.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/phy/phy.h>
#include <dt-bindings/phy/phy-imx8-pcie.h>

#define MAX_NUM_LANE	3
#define LANE_NUM_CLKS	5

/* Parameters for the waiting for PCIe PHY PLL to lock */
#define PHY_INIT_WAIT_USLEEP_MAX	10
#define PHY_INIT_WAIT_TIMEOUT		(1000 * PHY_INIT_WAIT_USLEEP_MAX)

/* i.MX8Q HSIO registers */
#define HSIO_CTRL0			0x0
#define HSIO_APB_RSTN_0			BIT(0)
#define HSIO_APB_RSTN_1			BIT(1)
#define HSIO_PIPE_RSTN_0_MASK		GENMASK(25, 24)
#define HSIO_PIPE_RSTN_1_MASK		GENMASK(27, 26)
#define HSIO_MODE_MASK			GENMASK(20, 17)
#define HSIO_MODE_PCIE			0x0
#define HSIO_MODE_SATA			0x4
#define HSIO_DEVICE_TYPE_MASK		GENMASK(27, 24)
#define HSIO_EPCS_TXDEEMP		BIT(5)
#define HSIO_EPCS_TXDEEMP_SEL		BIT(6)
#define HSIO_EPCS_PHYRESET_N		BIT(7)
#define HSIO_RESET_N			BIT(12)

#define HSIO_IOB_RXENA			BIT(0)
#define HSIO_IOB_TXENA			BIT(1)
#define HSIO_IOB_A_0_TXOE		BIT(2)
#define HSIO_IOB_A_0_M1M0_2		BIT(4)
#define HSIO_IOB_A_0_M1M0_MASK		GENMASK(4, 3)
#define HSIO_PHYX1_EPCS_SEL		BIT(12)
#define HSIO_PCIE_AB_SELECT		BIT(13)

#define HSIO_PHY_STS0			0x4
#define HSIO_LANE0_TX_PLL_LOCK		BIT(4)
#define HSIO_LANE1_TX_PLL_LOCK		BIT(12)

#define HSIO_CTRL2			0x8
#define HSIO_LTSSM_ENABLE		BIT(4)
#define HSIO_BUTTON_RST_N		BIT(21)
#define HSIO_PERST_N			BIT(22)
#define HSIO_POWER_UP_RST_N		BIT(23)

#define HSIO_PCIE_STS0			0xc
#define HSIO_PM_REQ_CORE_RST		BIT(19)

#define HSIO_REG48_PMA_STATUS		0x30
#define HSIO_REG48_PMA_RDY		BIT(7)

struct imx_hsio_drvdata {
	int lane_num;
};

struct imx_hsio_lane {
	u32 ctrl_index;
	u32 ctrl_off;
	u32 idx;
	u32 phy_off;
	u32 phy_type;
	const char * const *clk_names;
	struct clk_bulk_data clks[LANE_NUM_CLKS];
	struct imx_hsio_priv *priv;
	struct phy *phy;
	enum phy_mode phy_mode;
};

struct imx_hsio_priv {
	void __iomem *base;
	struct device *dev;
	struct mutex lock;
	const char *hsio_cfg;
	const char *refclk_pad;
	u32 open_cnt;
	struct regmap *phy;
	struct regmap *ctrl;
	struct regmap *misc;
	const struct imx_hsio_drvdata *drvdata;
	struct imx_hsio_lane lane[MAX_NUM_LANE];
};

static const char * const lan0_pcie_clks[] = {"apb_pclk0", "pclk0", "ctl0_crr",
					      "phy0_crr", "misc_crr"};
static const char * const lan1_pciea_clks[] = {"apb_pclk1", "pclk1", "ctl0_crr",
					       "phy0_crr", "misc_crr"};
static const char * const lan1_pcieb_clks[] = {"apb_pclk1", "pclk1", "ctl1_crr",
					       "phy0_crr", "misc_crr"};
static const char * const lan2_pcieb_clks[] = {"apb_pclk2", "pclk2", "ctl1_crr",
					       "phy1_crr", "misc_crr"};
static const char * const lan2_sata_clks[] = {"pclk2", "epcs_tx", "epcs_rx",
					      "phy1_crr", "misc_crr"};

static const struct regmap_config regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int imx_hsio_init(struct phy *phy)
{
	int ret, i;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;
	struct device *dev = priv->dev;

	/* Assign clocks refer to different modes */
	switch (lane->phy_type) {
	case PHY_TYPE_PCIE:
		lane->phy_mode = PHY_MODE_PCIE;
		if (lane->ctrl_index == 0) { /* PCIEA */
			lane->ctrl_off = 0;
			lane->phy_off = 0;

			for (i = 0; i < LANE_NUM_CLKS; i++) {
				if (lane->idx == 0)
					lane->clks[i].id = lan0_pcie_clks[i];
				else
					lane->clks[i].id = lan1_pciea_clks[i];
			}
		} else { /* PCIEB */
			if (lane->idx == 0) { /* i.MX8QXP */
				lane->ctrl_off = 0;
				lane->phy_off = 0;
			} else {
				/*
				 * On i.MX8QM, only second or third lane can be
				 * bound to PCIEB.
				 */
				lane->ctrl_off = SZ_64K;
				if (lane->idx == 1)
					lane->phy_off = 0;
				else /* the third lane is bound to PCIEB */
					lane->phy_off = SZ_64K;
			}

			for (i = 0; i < LANE_NUM_CLKS; i++) {
				if (lane->idx == 1)
					lane->clks[i].id = lan1_pcieb_clks[i];
				else if (lane->idx == 2)
					lane->clks[i].id = lan2_pcieb_clks[i];
				else /* i.MX8QXP only has PCIEB, idx is 0 */
					lane->clks[i].id = lan0_pcie_clks[i];
			}
		}
		break;
	case PHY_TYPE_SATA:
		/* On i.MX8QM, only the third lane can be bound to SATA */
		lane->phy_mode = PHY_MODE_SATA;
		lane->ctrl_off = SZ_128K;
		lane->phy_off = SZ_64K;

		for (i = 0; i < LANE_NUM_CLKS; i++)
			lane->clks[i].id = lan2_sata_clks[i];
		break;
	default:
		return -EINVAL;
	}

	/* Fetch clocks and enable them */
	ret = devm_clk_bulk_get(dev, LANE_NUM_CLKS, lane->clks);
	if (ret)
		return ret;
	ret = clk_bulk_prepare_enable(LANE_NUM_CLKS, lane->clks);
	if (ret)
		return ret;

	/* allow the clocks to stabilize */
	usleep_range(200, 500);
	return 0;
}

static int imx_hsio_exit(struct phy *phy)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(LANE_NUM_CLKS, lane->clks);

	return 0;
}

static void imx_hsio_pcie_phy_resets(struct phy *phy)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	regmap_clear_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			  HSIO_BUTTON_RST_N);
	regmap_clear_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			  HSIO_PERST_N);
	regmap_clear_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			  HSIO_POWER_UP_RST_N);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			HSIO_BUTTON_RST_N);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			HSIO_PERST_N);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			HSIO_POWER_UP_RST_N);

	if (lane->idx == 1) {
		regmap_set_bits(priv->phy, lane->phy_off + HSIO_CTRL0,
				HSIO_APB_RSTN_1);
		regmap_set_bits(priv->phy, lane->phy_off + HSIO_CTRL0,
				HSIO_PIPE_RSTN_1_MASK);
	} else {
		regmap_set_bits(priv->phy, lane->phy_off + HSIO_CTRL0,
				HSIO_APB_RSTN_0);
		regmap_set_bits(priv->phy, lane->phy_off + HSIO_CTRL0,
				HSIO_PIPE_RSTN_0_MASK);
	}
}

static void imx_hsio_sata_phy_resets(struct phy *phy)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	/* clear PHY RST, then set it */
	regmap_clear_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
			  HSIO_EPCS_PHYRESET_N);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
			HSIO_EPCS_PHYRESET_N);

	/* CTRL RST: SET -> delay 1 us -> CLEAR -> SET */
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0, HSIO_RESET_N);
	udelay(1);
	regmap_clear_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
			  HSIO_RESET_N);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0, HSIO_RESET_N);
}

static void imx_hsio_configure_clk_pad(struct phy *phy)
{
	bool pll = false;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	if (strncmp(priv->refclk_pad, "output", 6) == 0) {
		pll = true;
		regmap_update_bits(priv->misc, HSIO_CTRL0,
				   HSIO_IOB_A_0_TXOE | HSIO_IOB_A_0_M1M0_MASK,
				   HSIO_IOB_A_0_TXOE | HSIO_IOB_A_0_M1M0_2);
	} else {
		regmap_update_bits(priv->misc, HSIO_CTRL0,
				   HSIO_IOB_A_0_TXOE | HSIO_IOB_A_0_M1M0_MASK,
				   0);
	}

	regmap_update_bits(priv->misc, HSIO_CTRL0, HSIO_IOB_RXENA,
			   pll ? 0 : HSIO_IOB_RXENA);
	regmap_update_bits(priv->misc, HSIO_CTRL0, HSIO_IOB_TXENA,
			   pll ? HSIO_IOB_TXENA : 0);
}

static void imx_hsio_pre_set(struct phy *phy)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	if (strncmp(priv->hsio_cfg, "pciea-x2-pcieb", 14) == 0) {
		regmap_set_bits(priv->misc, HSIO_CTRL0, HSIO_PCIE_AB_SELECT);
	} else if (strncmp(priv->hsio_cfg, "pciea-x2-sata", 13) == 0) {
		regmap_set_bits(priv->misc, HSIO_CTRL0, HSIO_PHYX1_EPCS_SEL);
	} else if (strncmp(priv->hsio_cfg, "pciea-pcieb-sata", 16) == 0) {
		regmap_set_bits(priv->misc, HSIO_CTRL0, HSIO_PCIE_AB_SELECT);
		regmap_set_bits(priv->misc, HSIO_CTRL0, HSIO_PHYX1_EPCS_SEL);
	}

	imx_hsio_configure_clk_pad(phy);
}

static int imx_hsio_pcie_power_on(struct phy *phy)
{
	int ret;
	u32 val, addr, cond;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	imx_hsio_pcie_phy_resets(phy);

	/* Toggle apb_pclk to make sure PM_REQ_CORE_RST is cleared. */
	clk_disable_unprepare(lane->clks[0].clk);
	mdelay(1);
	ret = clk_prepare_enable(lane->clks[0].clk);
	if (ret) {
		dev_err(priv->dev, "unable to enable phy apb_pclk\n");
		return ret;
	}

	addr = lane->ctrl_off + HSIO_PCIE_STS0;
	cond = HSIO_PM_REQ_CORE_RST;
	ret = regmap_read_poll_timeout(priv->ctrl, addr, val,
				       (val & cond) == 0,
				       PHY_INIT_WAIT_USLEEP_MAX,
				       PHY_INIT_WAIT_TIMEOUT);
	if (ret)
		dev_err(priv->dev, "HSIO_PM_REQ_CORE_RST is set\n");
	return ret;
}

static int imx_hsio_sata_power_on(struct phy *phy)
{
	int ret;
	u32 val, cond;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	regmap_set_bits(priv->phy, lane->phy_off + HSIO_CTRL0, HSIO_APB_RSTN_0);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
			HSIO_EPCS_TXDEEMP);
	regmap_set_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
			HSIO_EPCS_TXDEEMP_SEL);

	imx_hsio_sata_phy_resets(phy);

	cond = HSIO_REG48_PMA_RDY;
	ret = read_poll_timeout(readb, val, ((val & cond) == cond),
				PHY_INIT_WAIT_USLEEP_MAX,
				PHY_INIT_WAIT_TIMEOUT, false,
				priv->base + HSIO_REG48_PMA_STATUS);
	if (ret)
		dev_err(priv->dev, "PHY calibration is timeout\n");
	else
		dev_dbg(priv->dev, "PHY calibration is done\n");

	return ret;
}

static int imx_hsio_power_on(struct phy *phy)
{
	int ret;
	u32 val, cond;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	scoped_guard(mutex, &priv->lock) {
		if (!priv->open_cnt)
			imx_hsio_pre_set(phy);
		priv->open_cnt++;
	}

	if (lane->phy_mode == PHY_MODE_PCIE)
		ret = imx_hsio_pcie_power_on(phy);
	else /* SATA */
		ret = imx_hsio_sata_power_on(phy);
	if (ret)
		return ret;

	/* Polling to check the PHY is ready or not. */
	if (lane->idx == 1)
		cond = HSIO_LANE1_TX_PLL_LOCK;
	else
		/*
		 * Except the phy_off, the bit-offset of lane2 is same to lane0.
		 * Merge the lane0 and lane2 bit-operations together.
		 */
		cond = HSIO_LANE0_TX_PLL_LOCK;

	ret = regmap_read_poll_timeout(priv->phy, lane->phy_off + HSIO_PHY_STS0,
				       val, ((val & cond) == cond),
				       PHY_INIT_WAIT_USLEEP_MAX,
				       PHY_INIT_WAIT_TIMEOUT);
	if (ret) {
		dev_err(priv->dev, "IMX8Q PHY%d PLL lock timeout\n", lane->idx);
		return ret;
	}
	dev_dbg(priv->dev, "IMX8Q PHY%d PLL is locked\n", lane->idx);

	return ret;
}

static int imx_hsio_power_off(struct phy *phy)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	scoped_guard(mutex, &priv->lock) {
		priv->open_cnt--;
		if (priv->open_cnt == 0) {
			regmap_clear_bits(priv->misc, HSIO_CTRL0,
					  HSIO_PCIE_AB_SELECT);
			regmap_clear_bits(priv->misc, HSIO_CTRL0,
					  HSIO_PHYX1_EPCS_SEL);

			if (lane->phy_mode == PHY_MODE_PCIE) {
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL2,
						  HSIO_BUTTON_RST_N);
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL2,
						  HSIO_PERST_N);
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL2,
						  HSIO_POWER_UP_RST_N);
			} else {
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL0,
						  HSIO_EPCS_TXDEEMP);
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL0,
						  HSIO_EPCS_TXDEEMP_SEL);
				regmap_clear_bits(priv->ctrl,
						  lane->ctrl_off + HSIO_CTRL0,
						  HSIO_RESET_N);
			}

			if (lane->idx == 1) {
				regmap_clear_bits(priv->phy,
						  lane->phy_off + HSIO_CTRL0,
						  HSIO_APB_RSTN_1);
				regmap_clear_bits(priv->phy,
						  lane->phy_off + HSIO_CTRL0,
						  HSIO_PIPE_RSTN_1_MASK);
			} else {
				/*
				 * Except the phy_off, the bit-offset of lane2 is same
				 * to lane0. Merge the lane0 and lane2 bit-operations
				 * together.
				 */
				regmap_clear_bits(priv->phy,
						  lane->phy_off + HSIO_CTRL0,
						  HSIO_APB_RSTN_0);
				regmap_clear_bits(priv->phy,
						  lane->phy_off + HSIO_CTRL0,
						  HSIO_PIPE_RSTN_0_MASK);
			}
		}
	}

	return 0;
}

static int imx_hsio_set_mode(struct phy *phy, enum phy_mode mode,
			     int submode)
{
	u32 val;
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	if (lane->phy_mode != mode)
		return -EINVAL;

	val = (mode == PHY_MODE_PCIE) ? HSIO_MODE_PCIE : HSIO_MODE_SATA;
	val = FIELD_PREP(HSIO_MODE_MASK, val);
	regmap_update_bits(priv->phy, lane->phy_off + HSIO_CTRL0,
			   HSIO_MODE_MASK, val);

	switch (submode) {
	case PHY_MODE_PCIE_RC:
		val = FIELD_PREP(HSIO_DEVICE_TYPE_MASK, PCI_EXP_TYPE_ROOT_PORT);
		break;
	case PHY_MODE_PCIE_EP:
		val = FIELD_PREP(HSIO_DEVICE_TYPE_MASK, PCI_EXP_TYPE_ENDPOINT);
		break;
	default: /* Support only PCIe EP and RC now. */
		return 0;
	}
	if (submode)
		regmap_update_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL0,
				   HSIO_DEVICE_TYPE_MASK, val);

	return 0;
}

static int imx_hsio_set_speed(struct phy *phy, int speed)
{
	struct imx_hsio_lane *lane = phy_get_drvdata(phy);
	struct imx_hsio_priv *priv = lane->priv;

	regmap_update_bits(priv->ctrl, lane->ctrl_off + HSIO_CTRL2,
			   HSIO_LTSSM_ENABLE,
			   speed ? HSIO_LTSSM_ENABLE : 0);
	return 0;
}

static const struct phy_ops imx_hsio_ops = {
	.init = imx_hsio_init,
	.exit = imx_hsio_exit,
	.power_on = imx_hsio_power_on,
	.power_off = imx_hsio_power_off,
	.set_mode = imx_hsio_set_mode,
	.set_speed = imx_hsio_set_speed,
	.owner = THIS_MODULE,
};

static const struct imx_hsio_drvdata imx8qxp_hsio_drvdata = {
	.lane_num = 0x1,
};

static const struct imx_hsio_drvdata imx8qm_hsio_drvdata = {
	.lane_num = 0x3,
};

static const struct of_device_id imx_hsio_of_match[] = {
	{.compatible = "fsl,imx8qm-hsio", .data = &imx8qm_hsio_drvdata},
	{.compatible = "fsl,imx8qxp-hsio", .data = &imx8qxp_hsio_drvdata},
	{ },
};
MODULE_DEVICE_TABLE(of, imx_hsio_of_match);

static struct phy *imx_hsio_xlate(struct device *dev,
				  const struct of_phandle_args *args)
{
	struct imx_hsio_priv *priv = dev_get_drvdata(dev);
	int idx = args->args[0];
	int phy_type = args->args[1];
	int ctrl_index = args->args[2];

	if (idx < 0 || idx >= priv->drvdata->lane_num)
		return ERR_PTR(-EINVAL);
	priv->lane[idx].idx = idx;
	priv->lane[idx].phy_type = phy_type;
	priv->lane[idx].ctrl_index = ctrl_index;

	return priv->lane[idx].phy;
}

static int imx_hsio_probe(struct platform_device *pdev)
{
	int i;
	void __iomem *off;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct imx_hsio_priv *priv;
	struct phy_provider *provider;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = &pdev->dev;
	priv->drvdata = of_device_get_match_data(dev);

	/* Get HSIO configuration mode */
	if (of_property_read_string(np, "fsl,hsio-cfg", &priv->hsio_cfg))
		priv->hsio_cfg = "pciea-pcieb-sata";
	/* Get PHY refclk pad mode */
	if (of_property_read_string(np, "fsl,refclk-pad-mode",
				    &priv->refclk_pad))
		priv->refclk_pad = NULL;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	off = devm_platform_ioremap_resource_byname(pdev, "phy");
	priv->phy = devm_regmap_init_mmio(dev, off, &regmap_config);
	if (IS_ERR(priv->phy))
		return dev_err_probe(dev, PTR_ERR(priv->phy),
				     "unable to find phy csr registers\n");

	off = devm_platform_ioremap_resource_byname(pdev, "ctrl");
	priv->ctrl = devm_regmap_init_mmio(dev, off, &regmap_config);
	if (IS_ERR(priv->ctrl))
		return dev_err_probe(dev, PTR_ERR(priv->ctrl),
				     "unable to find ctrl csr registers\n");

	off = devm_platform_ioremap_resource_byname(pdev, "misc");
	priv->misc = devm_regmap_init_mmio(dev, off, &regmap_config);
	if (IS_ERR(priv->misc))
		return dev_err_probe(dev, PTR_ERR(priv->misc),
				     "unable to find misc csr registers\n");

	for (i = 0; i < priv->drvdata->lane_num; i++) {
		struct imx_hsio_lane *lane = &priv->lane[i];
		struct phy *phy;

		phy = devm_phy_create(&pdev->dev, NULL, &imx_hsio_ops);
		if (IS_ERR(phy))
			return PTR_ERR(phy);

		lane->priv = priv;
		lane->phy = phy;
		lane->idx = i;
		phy_set_drvdata(phy, lane);
	}

	dev_set_drvdata(dev, priv);
	dev_set_drvdata(&pdev->dev, priv);

	provider = devm_of_phy_provider_register(&pdev->dev, imx_hsio_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static struct platform_driver imx_hsio_driver = {
	.probe	= imx_hsio_probe,
	.driver = {
		.name	= "imx8qm-hsio-phy",
		.of_match_table	= imx_hsio_of_match,
	}
};
module_platform_driver(imx_hsio_driver);

MODULE_DESCRIPTION("FSL IMX8QM HSIO SERDES PHY driver");
MODULE_LICENSE("GPL");
