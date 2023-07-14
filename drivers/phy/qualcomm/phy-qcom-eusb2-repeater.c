// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>

/* eUSB2 status registers */
#define EUSB2_RPTR_STATUS		0x08
#define	RPTR_OK				BIT(7)

/* eUSB2 control registers */
#define EUSB2_EN_CTL1			0x46
#define EUSB2_RPTR_EN			BIT(7)

#define EUSB2_FORCE_EN_5		0xe8
#define F_CLK_19P2M_EN			BIT(6)

#define EUSB2_FORCE_VAL_5		0xeD
#define V_CLK_19P2M_EN			BIT(6)

#define EUSB2_TUNE_IUSB2		0x51
#define EUSB2_TUNE_SQUELCH_U		0x54
#define EUSB2_TUNE_USB2_PREEM		0x57

#define QCOM_EUSB2_REPEATER_INIT_CFG(o, v)	\
	{					\
		.offset = o,			\
		.val = v,			\
	}

struct eusb2_repeater_init_tbl {
	unsigned int offset;
	unsigned int val;
};

struct eusb2_repeater_cfg {
	const struct eusb2_repeater_init_tbl *init_tbl;
	int init_tbl_num;
	const char * const *vreg_list;
	int num_vregs;
};

struct eusb2_repeater {
	struct device *dev;
	struct regmap *regmap;
	struct phy *phy;
	struct regulator_bulk_data *vregs;
	const struct eusb2_repeater_cfg *cfg;
	u16 base;
	enum phy_mode mode;
};

static const char * const pm8550b_vreg_l[] = {
	"vdd18", "vdd3",
};

static const struct eusb2_repeater_init_tbl pm8550b_init_tbl[] = {
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_IUSB2, 0x8),
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_SQUELCH_U, 0x3),
	QCOM_EUSB2_REPEATER_INIT_CFG(EUSB2_TUNE_USB2_PREEM, 0x5),
};

static const struct eusb2_repeater_cfg pm8550b_eusb2_cfg = {
	.init_tbl	= pm8550b_init_tbl,
	.init_tbl_num	= ARRAY_SIZE(pm8550b_init_tbl),
	.vreg_list	= pm8550b_vreg_l,
	.num_vregs	= ARRAY_SIZE(pm8550b_vreg_l),
};

static int eusb2_repeater_init_vregs(struct eusb2_repeater *rptr)
{
	int num = rptr->cfg->num_vregs;
	struct device *dev = rptr->dev;
	int i;

	rptr->vregs = devm_kcalloc(dev, num, sizeof(*rptr->vregs), GFP_KERNEL);
	if (!rptr->vregs)
		return -ENOMEM;

	for (i = 0; i < num; i++)
		rptr->vregs[i].supply = rptr->cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, num, rptr->vregs);
}

static int eusb2_repeater_init(struct phy *phy)
{
	struct eusb2_repeater *rptr = phy_get_drvdata(phy);
	const struct eusb2_repeater_init_tbl *init_tbl = rptr->cfg->init_tbl;
	int num = rptr->cfg->init_tbl_num;
	u32 val;
	int ret;
	int i;

	ret = regulator_bulk_enable(rptr->cfg->num_vregs, rptr->vregs);
	if (ret)
		return ret;

	regmap_update_bits(rptr->regmap, rptr->base + EUSB2_EN_CTL1,
			   EUSB2_RPTR_EN, EUSB2_RPTR_EN);

	for (i = 0; i < num; i++)
		regmap_update_bits(rptr->regmap,
				   rptr->base + init_tbl[i].offset,
				   init_tbl[i].val, init_tbl[i].val);

	ret = regmap_read_poll_timeout(rptr->regmap,
				       rptr->base + EUSB2_RPTR_STATUS, val,
				       val & RPTR_OK, 10, 5);
	if (ret)
		dev_err(rptr->dev, "initialization timed-out\n");

	return ret;
}

static int eusb2_repeater_set_mode(struct phy *phy,
				   enum phy_mode mode, int submode)
{
	struct eusb2_repeater *rptr = phy_get_drvdata(phy);

	switch (mode) {
	case PHY_MODE_USB_HOST:
		/*
		 * CM.Lx is prohibited when repeater is already into Lx state as
		 * per eUSB 1.2 Spec. Below implement software workaround until
		 * PHY and controller is fixing seen observation.
		 */
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_EN_5,
				   F_CLK_19P2M_EN, F_CLK_19P2M_EN);
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_VAL_5,
				   V_CLK_19P2M_EN, V_CLK_19P2M_EN);
		break;
	case PHY_MODE_USB_DEVICE:
		/*
		 * In device mode clear host mode related workaround as there
		 * is no repeater reset available, and enable/disable of
		 * repeater doesn't clear previous value due to shared
		 * regulators (say host <-> device mode switch).
		 */
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_EN_5,
				   F_CLK_19P2M_EN, 0);
		regmap_update_bits(rptr->regmap, rptr->base + EUSB2_FORCE_VAL_5,
				   V_CLK_19P2M_EN, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int eusb2_repeater_exit(struct phy *phy)
{
	struct eusb2_repeater *rptr = phy_get_drvdata(phy);

	return regulator_bulk_disable(rptr->cfg->num_vregs, rptr->vregs);
}

static const struct phy_ops eusb2_repeater_ops = {
	.init		= eusb2_repeater_init,
	.exit		= eusb2_repeater_exit,
	.set_mode	= eusb2_repeater_set_mode,
	.owner		= THIS_MODULE,
};

static int eusb2_repeater_probe(struct platform_device *pdev)
{
	struct eusb2_repeater *rptr;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct device_node *np = dev->of_node;
	u32 res;
	int ret;

	rptr = devm_kzalloc(dev, sizeof(*rptr), GFP_KERNEL);
	if (!rptr)
		return -ENOMEM;

	rptr->dev = dev;
	dev_set_drvdata(dev, rptr);

	rptr->cfg = of_device_get_match_data(dev);
	if (!rptr->cfg)
		return -EINVAL;

	rptr->regmap = dev_get_regmap(dev->parent, NULL);
	if (!rptr->regmap)
		return -ENODEV;

	ret = of_property_read_u32(np, "reg", &res);
	if (ret < 0)
		return ret;

	rptr->base = res;

	ret = eusb2_repeater_init_vregs(rptr);
	if (ret < 0) {
		dev_err(dev, "unable to get supplies\n");
		return ret;
	}

	rptr->phy = devm_phy_create(dev, np, &eusb2_repeater_ops);
	if (IS_ERR(rptr->phy)) {
		dev_err(dev, "failed to create PHY: %d\n", ret);
		return PTR_ERR(rptr->phy);
	}

	phy_set_drvdata(rptr->phy, rptr);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	dev_info(dev, "Registered Qcom-eUSB2 repeater\n");

	return 0;
}

static void eusb2_repeater_remove(struct platform_device *pdev)
{
	struct eusb2_repeater *rptr = platform_get_drvdata(pdev);

	if (!rptr)
		return;

	eusb2_repeater_exit(rptr->phy);
}

static const struct of_device_id eusb2_repeater_of_match_table[] = {
	{
		.compatible = "qcom,pm8550b-eusb2-repeater",
		.data = &pm8550b_eusb2_cfg,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, eusb2_repeater_of_match_table);

static struct platform_driver eusb2_repeater_driver = {
	.probe		= eusb2_repeater_probe,
	.remove_new	= eusb2_repeater_remove,
	.driver = {
		.name	= "qcom-eusb2-repeater",
		.of_match_table = eusb2_repeater_of_match_table,
	},
};

module_platform_driver(eusb2_repeater_driver);

MODULE_DESCRIPTION("Qualcomm PMIC eUSB2 Repeater driver");
MODULE_LICENSE("GPL");
