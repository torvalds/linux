// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#define USB_PHY_UTMI_CTRL0		(0x3c)
#define SLEEPM				BIT(0)
#define OPMODE_MASK			GENMASK(4, 3)
#define OPMODE_NONDRIVING		BIT(3)

#define USB_PHY_UTMI_CTRL5		(0x50)
#define POR				BIT(1)

#define USB_PHY_HS_PHY_CTRL_COMMON0	(0x54)
#define PHY_ENABLE			BIT(0)
#define SIDDQ_SEL			BIT(1)
#define SIDDQ				BIT(2)
#define RETENABLEN			BIT(3)
#define FSEL_MASK			GENMASK(6, 4)
#define FSEL_19_2_MHZ_VAL		(0x0)
#define FSEL_38_4_MHZ_VAL		(0x4)

#define USB_PHY_CFG_CTRL_1		(0x58)
#define PHY_CFG_PLL_CPBIAS_CNTRL_MASK	GENMASK(7, 1)

#define USB_PHY_CFG_CTRL_2		(0x5c)
#define PHY_CFG_PLL_FB_DIV_7_0_MASK	GENMASK(7, 0)
#define DIV_7_0_19_2_MHZ_VAL		(0x90)
#define DIV_7_0_38_4_MHZ_VAL		(0xc8)

#define USB_PHY_CFG_CTRL_3		(0x60)
#define PHY_CFG_PLL_FB_DIV_11_8_MASK	GENMASK(3, 0)
#define DIV_11_8_19_2_MHZ_VAL		(0x1)
#define DIV_11_8_38_4_MHZ_VAL		(0x0)

#define PHY_CFG_PLL_REF_DIV		GENMASK(7, 4)
#define PLL_REF_DIV_VAL			(0x0)

#define USB_PHY_HS_PHY_CTRL2		(0x64)
#define VBUSVLDEXT0			BIT(0)
#define USB2_SUSPEND_N			BIT(2)
#define USB2_SUSPEND_N_SEL		BIT(3)
#define VBUS_DET_EXT_SEL		BIT(4)

#define USB_PHY_CFG_CTRL_4		(0x68)
#define PHY_CFG_PLL_GMP_CNTRL_MASK	GENMASK(1, 0)
#define PHY_CFG_PLL_INT_CNTRL_MASK	GENMASK(7, 2)

#define USB_PHY_CFG_CTRL_5		(0x6c)
#define PHY_CFG_PLL_PROP_CNTRL_MASK	GENMASK(4, 0)
#define PHY_CFG_PLL_VREF_TUNE_MASK	GENMASK(7, 6)

#define USB_PHY_CFG_CTRL_6		(0x70)
#define PHY_CFG_PLL_VCO_CNTRL_MASK	GENMASK(2, 0)

#define USB_PHY_CFG_CTRL_7		(0x74)

#define USB_PHY_CFG_CTRL_8		(0x78)
#define PHY_CFG_TX_FSLS_VREF_TUNE_MASK	GENMASK(1, 0)
#define PHY_CFG_TX_FSLS_VREG_BYPASS	BIT(2)
#define PHY_CFG_TX_HS_VREF_TUNE_MASK	GENMASK(5, 3)
#define PHY_CFG_TX_HS_XV_TUNE_MASK	GENMASK(7, 6)

#define USB_PHY_CFG_CTRL_9		(0x7c)
#define PHY_CFG_TX_PREEMP_TUNE_MASK	GENMASK(2, 0)
#define PHY_CFG_TX_RES_TUNE_MASK	GENMASK(4, 3)
#define PHY_CFG_TX_RISE_TUNE_MASK	GENMASK(6, 5)
#define PHY_CFG_RCAL_BYPASS		BIT(7)

#define USB_PHY_CFG_CTRL_10		(0x80)

#define USB_PHY_CFG0			(0x94)
#define DATAPATH_CTRL_OVERRIDE_EN	BIT(0)
#define CMN_CTRL_OVERRIDE_EN		BIT(1)

#define UTMI_PHY_CMN_CTRL0		(0x98)
#define TESTBURNIN			BIT(6)

#define USB_PHY_FSEL_SEL		(0xb8)
#define FSEL_SEL			BIT(0)

#define USB_PHY_APB_ACCESS_CMD		(0x130)
#define RW_ACCESS			BIT(0)
#define APB_START_CMD			BIT(1)
#define APB_LOGIC_RESET			BIT(2)

#define USB_PHY_APB_ACCESS_STATUS	(0x134)
#define ACCESS_DONE			BIT(0)
#define TIMED_OUT			BIT(1)
#define ACCESS_ERROR			BIT(2)
#define ACCESS_IN_PROGRESS		BIT(3)

#define USB_PHY_APB_ADDRESS		(0x138)
#define APB_REG_ADDR_MASK		GENMASK(7, 0)

#define USB_PHY_APB_WRDATA_LSB		(0x13c)
#define APB_REG_WRDATA_7_0_MASK		GENMASK(3, 0)

#define USB_PHY_APB_WRDATA_MSB		(0x140)
#define APB_REG_WRDATA_15_8_MASK	GENMASK(7, 4)

#define USB_PHY_APB_RDDATA_LSB		(0x144)
#define APB_REG_RDDATA_7_0_MASK		GENMASK(3, 0)

#define USB_PHY_APB_RDDATA_MSB		(0x148)
#define APB_REG_RDDATA_15_8_MASK	GENMASK(7, 4)

static const char * const eusb2_hsphy_vreg_names[] = {
	"vdd", "vdda12",
};

#define EUSB2_NUM_VREGS		ARRAY_SIZE(eusb2_hsphy_vreg_names)

struct qcom_snps_eusb2_hsphy {
	struct phy *phy;
	void __iomem *base;

	struct clk *ref_clk;
	struct reset_control *phy_reset;

	struct regulator_bulk_data vregs[EUSB2_NUM_VREGS];

	enum phy_mode mode;

	struct phy *repeater;
};

static int qcom_snps_eusb2_hsphy_set_mode(struct phy *p, enum phy_mode mode, int submode)
{
	struct qcom_snps_eusb2_hsphy *phy = phy_get_drvdata(p);

	phy->mode = mode;

	return phy_set_mode_ext(phy->repeater, mode, submode);
}

static void qcom_snps_eusb2_hsphy_write_mask(void __iomem *base, u32 offset,
					     u32 mask, u32 val)
{
	u32 reg;

	reg = readl_relaxed(base + offset);
	reg &= ~mask;
	reg |= val & mask;
	writel_relaxed(reg, base + offset);

	/* Ensure above write is completed */
	readl_relaxed(base + offset);
}

static void qcom_eusb2_default_parameters(struct qcom_snps_eusb2_hsphy *phy)
{
	/* default parameters: tx pre-emphasis */
	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_9,
					 PHY_CFG_TX_PREEMP_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_TX_PREEMP_TUNE_MASK, 0));

	/* tx rise/fall time */
	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_9,
					 PHY_CFG_TX_RISE_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_TX_RISE_TUNE_MASK, 0x2));

	/* source impedance adjustment */
	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_9,
					 PHY_CFG_TX_RES_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_TX_RES_TUNE_MASK, 0x1));

	/* dc voltage level adjustement */
	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_8,
					 PHY_CFG_TX_HS_VREF_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_TX_HS_VREF_TUNE_MASK, 0x3));

	/* transmitter HS crossover adjustement */
	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_8,
					 PHY_CFG_TX_HS_XV_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_TX_HS_XV_TUNE_MASK, 0x0));
}

static int qcom_eusb2_ref_clk_init(struct qcom_snps_eusb2_hsphy *phy)
{
	unsigned long ref_clk_freq = clk_get_rate(phy->ref_clk);

	switch (ref_clk_freq) {
	case 19200000:
		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
						 FSEL_MASK,
						 FIELD_PREP(FSEL_MASK, FSEL_19_2_MHZ_VAL));

		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_2,
						 PHY_CFG_PLL_FB_DIV_7_0_MASK,
						 DIV_7_0_19_2_MHZ_VAL);

		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_3,
						 PHY_CFG_PLL_FB_DIV_11_8_MASK,
						 DIV_11_8_19_2_MHZ_VAL);
		break;

	case 38400000:
		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
						 FSEL_MASK,
						 FIELD_PREP(FSEL_MASK, FSEL_38_4_MHZ_VAL));

		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_2,
						 PHY_CFG_PLL_FB_DIV_7_0_MASK,
						 DIV_7_0_38_4_MHZ_VAL);

		qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_3,
						 PHY_CFG_PLL_FB_DIV_11_8_MASK,
						 DIV_11_8_38_4_MHZ_VAL);
		break;

	default:
		dev_err(&phy->phy->dev, "unsupported ref_clk_freq:%lu\n", ref_clk_freq);
		return -EINVAL;
	}

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_3,
					 PHY_CFG_PLL_REF_DIV, PLL_REF_DIV_VAL);

	return 0;
}

static int qcom_snps_eusb2_hsphy_init(struct phy *p)
{
	struct qcom_snps_eusb2_hsphy *phy = phy_get_drvdata(p);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(phy->vregs), phy->vregs);
	if (ret)
		return ret;

	ret = phy_init(phy->repeater);
	if (ret) {
		dev_err(&p->dev, "repeater init failed. %d\n", ret);
		goto disable_vreg;
	}

	ret = clk_prepare_enable(phy->ref_clk);
	if (ret) {
		dev_err(&p->dev, "failed to enable ref clock, %d\n", ret);
		goto disable_vreg;
	}

	ret = reset_control_assert(phy->phy_reset);
	if (ret) {
		dev_err(&p->dev, "failed to assert phy_reset, %d\n", ret);
		goto disable_ref_clk;
	}

	usleep_range(100, 150);

	ret = reset_control_deassert(phy->phy_reset);
	if (ret) {
		dev_err(&p->dev, "failed to de-assert phy_reset, %d\n", ret);
		goto disable_ref_clk;
	}

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG0,
					 CMN_CTRL_OVERRIDE_EN, CMN_CTRL_OVERRIDE_EN);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_UTMI_CTRL5, POR, POR);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
					 PHY_ENABLE | RETENABLEN, PHY_ENABLE | RETENABLEN);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_APB_ACCESS_CMD,
					 APB_LOGIC_RESET, APB_LOGIC_RESET);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, UTMI_PHY_CMN_CTRL0, TESTBURNIN, 0);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_FSEL_SEL,
					 FSEL_SEL, FSEL_SEL);

	/* update ref_clk related registers */
	ret = qcom_eusb2_ref_clk_init(phy);
	if (ret)
		goto disable_ref_clk;

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_1,
					 PHY_CFG_PLL_CPBIAS_CNTRL_MASK,
					 FIELD_PREP(PHY_CFG_PLL_CPBIAS_CNTRL_MASK, 0x1));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_4,
					 PHY_CFG_PLL_INT_CNTRL_MASK,
					 FIELD_PREP(PHY_CFG_PLL_INT_CNTRL_MASK, 0x8));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_4,
					 PHY_CFG_PLL_GMP_CNTRL_MASK,
					 FIELD_PREP(PHY_CFG_PLL_GMP_CNTRL_MASK, 0x1));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_5,
					 PHY_CFG_PLL_PROP_CNTRL_MASK,
					 FIELD_PREP(PHY_CFG_PLL_PROP_CNTRL_MASK, 0x10));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_6,
					 PHY_CFG_PLL_VCO_CNTRL_MASK,
					 FIELD_PREP(PHY_CFG_PLL_VCO_CNTRL_MASK, 0x0));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_CFG_CTRL_5,
					 PHY_CFG_PLL_VREF_TUNE_MASK,
					 FIELD_PREP(PHY_CFG_PLL_VREF_TUNE_MASK, 0x1));

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL2,
					 VBUS_DET_EXT_SEL, VBUS_DET_EXT_SEL);

	/* set default parameters */
	qcom_eusb2_default_parameters(phy);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL2,
					 USB2_SUSPEND_N_SEL | USB2_SUSPEND_N,
					 USB2_SUSPEND_N_SEL | USB2_SUSPEND_N);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_UTMI_CTRL0, SLEEPM, SLEEPM);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
					 SIDDQ_SEL, SIDDQ_SEL);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL_COMMON0,
					 SIDDQ, 0);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_UTMI_CTRL5, POR, 0);

	qcom_snps_eusb2_hsphy_write_mask(phy->base, USB_PHY_HS_PHY_CTRL2,
					 USB2_SUSPEND_N_SEL, 0);

	return 0;

disable_ref_clk:
	clk_disable_unprepare(phy->ref_clk);

disable_vreg:
	regulator_bulk_disable(ARRAY_SIZE(phy->vregs), phy->vregs);

	return ret;
}

static int qcom_snps_eusb2_hsphy_exit(struct phy *p)
{
	struct qcom_snps_eusb2_hsphy *phy = phy_get_drvdata(p);

	clk_disable_unprepare(phy->ref_clk);

	regulator_bulk_disable(ARRAY_SIZE(phy->vregs), phy->vregs);

	phy_exit(phy->repeater);

	return 0;
}

static const struct phy_ops qcom_snps_eusb2_hsphy_ops = {
	.init		= qcom_snps_eusb2_hsphy_init,
	.exit		= qcom_snps_eusb2_hsphy_exit,
	.set_mode	= qcom_snps_eusb2_hsphy_set_mode,
	.owner		= THIS_MODULE,
};

static int qcom_snps_eusb2_hsphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct qcom_snps_eusb2_hsphy *phy;
	struct phy_provider *phy_provider;
	struct phy *generic_phy;
	int ret, i;
	int num;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->phy_reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(phy->phy_reset))
		return PTR_ERR(phy->phy_reset);

	phy->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(phy->ref_clk))
		return dev_err_probe(dev, PTR_ERR(phy->ref_clk),
				     "failed to get ref clk\n");

	num = ARRAY_SIZE(phy->vregs);
	for (i = 0; i < num; i++)
		phy->vregs[i].supply = eusb2_hsphy_vreg_names[i];

	ret = devm_regulator_bulk_get(dev, num, phy->vregs);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get regulator supplies\n");

	phy->repeater = devm_of_phy_get_by_index(dev, np, 0);
	if (IS_ERR(phy->repeater))
		return dev_err_probe(dev, PTR_ERR(phy->repeater),
				     "failed to get repeater\n");

	generic_phy = devm_phy_create(dev, NULL, &qcom_snps_eusb2_hsphy_ops);
	if (IS_ERR(generic_phy)) {
		dev_err(dev, "failed to create phy %d\n", ret);
		return PTR_ERR(generic_phy);
	}

	dev_set_drvdata(dev, phy);
	phy_set_drvdata(generic_phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	dev_info(dev, "Registered Qcom-eUSB2 phy\n");

	return 0;
}

static const struct of_device_id qcom_snps_eusb2_hsphy_of_match_table[] = {
	{ .compatible = "qcom,sm8550-snps-eusb2-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_snps_eusb2_hsphy_of_match_table);

static struct platform_driver qcom_snps_eusb2_hsphy_driver = {
	.probe		= qcom_snps_eusb2_hsphy_probe,
	.driver = {
		.name	= "qcom-snps-eusb2-hsphy",
		.of_match_table = qcom_snps_eusb2_hsphy_of_match_table,
	},
};

module_platform_driver(qcom_snps_eusb2_hsphy_driver);
MODULE_DESCRIPTION("Qualcomm SNPS eUSB2 HS PHY driver");
MODULE_LICENSE("GPL");
