// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Limited
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/of.h>
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

#define EUSB2_TUNE_USB2_CROSSOVER	0x50
#define EUSB2_TUNE_IUSB2		0x51
#define EUSB2_TUNE_RES_FSDIF		0x52
#define EUSB2_TUNE_HSDISC		0x53
#define EUSB2_TUNE_SQUELCH_U		0x54
#define EUSB2_TUNE_USB2_SLEW		0x55
#define EUSB2_TUNE_USB2_EQU		0x56
#define EUSB2_TUNE_USB2_PREEM		0x57
#define EUSB2_TUNE_USB2_HS_COMP_CUR	0x58
#define EUSB2_TUNE_EUSB_SLEW		0x59
#define EUSB2_TUNE_EUSB_EQU		0x5A
#define EUSB2_TUNE_EUSB_HS_COMP_CUR	0x5B

#define QCOM_EUSB2_REPEATER_INIT_CFG(r, v)	\
	{					\
		.reg = r,			\
		.val = v,			\
	}

enum reg_fields {
	F_TUNE_EUSB_HS_COMP_CUR,
	F_TUNE_EUSB_EQU,
	F_TUNE_EUSB_SLEW,
	F_TUNE_USB2_HS_COMP_CUR,
	F_TUNE_USB2_PREEM,
	F_TUNE_USB2_EQU,
	F_TUNE_USB2_SLEW,
	F_TUNE_SQUELCH_U,
	F_TUNE_HSDISC,
	F_TUNE_RES_FSDIF,
	F_TUNE_IUSB2,
	F_TUNE_USB2_CROSSOVER,
	F_NUM_TUNE_FIELDS,

	F_FORCE_VAL_5 = F_NUM_TUNE_FIELDS,
	F_FORCE_EN_5,

	F_EN_CTL1,

	F_RPTR_STATUS,
	F_NUM_FIELDS,
};

static struct reg_field eusb2_repeater_tune_reg_fields[F_NUM_FIELDS] = {
	[F_TUNE_EUSB_HS_COMP_CUR] = REG_FIELD(EUSB2_TUNE_EUSB_HS_COMP_CUR, 0, 1),
	[F_TUNE_EUSB_EQU] = REG_FIELD(EUSB2_TUNE_EUSB_EQU, 0, 1),
	[F_TUNE_EUSB_SLEW] = REG_FIELD(EUSB2_TUNE_EUSB_SLEW, 0, 1),
	[F_TUNE_USB2_HS_COMP_CUR] = REG_FIELD(EUSB2_TUNE_USB2_HS_COMP_CUR, 0, 1),
	[F_TUNE_USB2_PREEM] = REG_FIELD(EUSB2_TUNE_USB2_PREEM, 0, 2),
	[F_TUNE_USB2_EQU] = REG_FIELD(EUSB2_TUNE_USB2_EQU, 0, 1),
	[F_TUNE_USB2_SLEW] = REG_FIELD(EUSB2_TUNE_USB2_SLEW, 0, 1),
	[F_TUNE_SQUELCH_U] = REG_FIELD(EUSB2_TUNE_SQUELCH_U, 0, 2),
	[F_TUNE_HSDISC] = REG_FIELD(EUSB2_TUNE_HSDISC, 0, 2),
	[F_TUNE_RES_FSDIF] = REG_FIELD(EUSB2_TUNE_RES_FSDIF, 0, 2),
	[F_TUNE_IUSB2] = REG_FIELD(EUSB2_TUNE_IUSB2, 0, 3),
	[F_TUNE_USB2_CROSSOVER] = REG_FIELD(EUSB2_TUNE_USB2_CROSSOVER, 0, 2),

	[F_FORCE_VAL_5] = REG_FIELD(EUSB2_FORCE_VAL_5, 0, 7),
	[F_FORCE_EN_5] = REG_FIELD(EUSB2_FORCE_EN_5, 0, 7),

	[F_EN_CTL1] = REG_FIELD(EUSB2_EN_CTL1, 0, 7),

	[F_RPTR_STATUS] = REG_FIELD(EUSB2_RPTR_STATUS, 0, 7),
};

struct eusb2_repeater_cfg {
	const u32 *init_tbl;
	int init_tbl_num;
	const char * const *vreg_list;
	int num_vregs;
};

struct eusb2_repeater {
	struct device *dev;
	struct regmap_field *regs[F_NUM_FIELDS];
	struct phy *phy;
	struct regulator_bulk_data *vregs;
	const struct eusb2_repeater_cfg *cfg;
	enum phy_mode mode;
};

static const char * const pm8550b_vreg_l[] = {
	"vdd18", "vdd3",
};

static const u32 pm8550b_init_tbl[F_NUM_TUNE_FIELDS] = {
	[F_TUNE_IUSB2] = 0x8,
	[F_TUNE_SQUELCH_U] = 0x3,
	[F_TUNE_USB2_PREEM] = 0x5,
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
	struct reg_field *regfields = eusb2_repeater_tune_reg_fields;
	struct eusb2_repeater *rptr = phy_get_drvdata(phy);
	const u32 *init_tbl = rptr->cfg->init_tbl;
	u32 val;
	int ret;
	int i;

	ret = regulator_bulk_enable(rptr->cfg->num_vregs, rptr->vregs);
	if (ret)
		return ret;

	regmap_field_update_bits(rptr->regs[F_EN_CTL1], EUSB2_RPTR_EN, EUSB2_RPTR_EN);

	for (i = 0; i < F_NUM_TUNE_FIELDS; i++) {
		if (init_tbl[i]) {
			regmap_field_update_bits(rptr->regs[i], init_tbl[i], init_tbl[i]);
		} else {
			/* Write 0 if there's no value set */
			u32 mask = GENMASK(regfields[i].msb, regfields[i].lsb);

			regmap_field_update_bits(rptr->regs[i], mask, 0);
		}
	}

	ret = regmap_field_read_poll_timeout(rptr->regs[F_RPTR_STATUS],
					     val, val & RPTR_OK, 10, 5);
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
		regmap_field_update_bits(rptr->regs[F_FORCE_EN_5],
					 F_CLK_19P2M_EN, F_CLK_19P2M_EN);
		regmap_field_update_bits(rptr->regs[F_FORCE_VAL_5],
					 V_CLK_19P2M_EN, V_CLK_19P2M_EN);
		break;
	case PHY_MODE_USB_DEVICE:
		/*
		 * In device mode clear host mode related workaround as there
		 * is no repeater reset available, and enable/disable of
		 * repeater doesn't clear previous value due to shared
		 * regulators (say host <-> device mode switch).
		 */
		regmap_field_update_bits(rptr->regs[F_FORCE_EN_5],
					 F_CLK_19P2M_EN, 0);
		regmap_field_update_bits(rptr->regs[F_FORCE_VAL_5],
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
	struct regmap *regmap;
	int i, ret;
	u32 res;

	rptr = devm_kzalloc(dev, sizeof(*rptr), GFP_KERNEL);
	if (!rptr)
		return -ENOMEM;

	rptr->dev = dev;
	dev_set_drvdata(dev, rptr);

	rptr->cfg = of_device_get_match_data(dev);
	if (!rptr->cfg)
		return -EINVAL;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return -ENODEV;

	ret = of_property_read_u32(np, "reg", &res);
	if (ret < 0)
		return ret;

	for (i = 0; i < F_NUM_FIELDS; i++)
		eusb2_repeater_tune_reg_fields[i].reg += res;

	ret = devm_regmap_field_bulk_alloc(dev, regmap, rptr->regs,
					   eusb2_repeater_tune_reg_fields,
					   F_NUM_FIELDS);
	if (ret)
		return ret;

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
