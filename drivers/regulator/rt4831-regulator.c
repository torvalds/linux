// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>

enum {
	DSV_OUT_VLCM = 0,
	DSV_OUT_VPOS,
	DSV_OUT_VNEG,
	DSV_OUT_MAX
};

#define RT4831_REG_DSVEN	0x09
#define RT4831_REG_VLCM		0x0c
#define RT4831_REG_VPOS		0x0d
#define RT4831_REG_VNEG		0x0e
#define RT4831_REG_FLAGS	0x0f

#define RT4831_VOLT_MASK	GENMASK(5, 0)
#define RT4831_DSVMODE_SHIFT	5
#define RT4831_DSVMODE_MASK	GENMASK(7, 5)
#define RT4831_POSADEN_MASK	BIT(4)
#define RT4831_NEGADEN_MASK	BIT(3)
#define RT4831_POSEN_MASK	BIT(2)
#define RT4831_NEGEN_MASK	BIT(1)

#define RT4831_OTP_MASK		BIT(6)
#define RT4831_LCMOVP_MASK	BIT(5)
#define RT4831_VPOSSCP_MASK	BIT(3)
#define RT4831_VNEGSCP_MASK	BIT(2)

#define DSV_MODE_NORMAL		(0x4 << RT4831_DSVMODE_SHIFT)
#define DSV_MODE_BYPASS		(0x6 << RT4831_DSVMODE_SHIFT)
#define STEP_UV			50000
#define VLCM_MIN_UV		4000000
#define VLCM_MAX_UV		7150000
#define VLCM_N_VOLTAGES		((VLCM_MAX_UV - VLCM_MIN_UV) / STEP_UV + 1)
#define VPN_MIN_UV		4000000
#define VPN_MAX_UV		6500000
#define VPN_N_VOLTAGES		((VPN_MAX_UV - VPN_MIN_UV) / STEP_UV + 1)

static int rt4831_get_error_flags(struct regulator_dev *rdev, unsigned int *flags)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int rid = rdev_get_id(rdev);
	unsigned int val, events = 0;
	int ret;

	ret = regmap_read(regmap, RT4831_REG_FLAGS, &val);
	if (ret)
		return ret;

	if (val & RT4831_OTP_MASK)
		events |= REGULATOR_ERROR_OVER_TEMP;

	if (rid == DSV_OUT_VLCM && (val & RT4831_LCMOVP_MASK))
		events |= REGULATOR_ERROR_OVER_CURRENT;

	if (rid == DSV_OUT_VPOS && (val & RT4831_VPOSSCP_MASK))
		events |= REGULATOR_ERROR_OVER_CURRENT;

	if (rid == DSV_OUT_VNEG && (val & RT4831_VNEGSCP_MASK))
		events |= REGULATOR_ERROR_OVER_CURRENT;

	*flags = events;
	return 0;
}

static const struct regulator_ops rt4831_dsvlcm_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_bypass = regulator_set_bypass_regmap,
	.get_bypass = regulator_get_bypass_regmap,
	.get_error_flags = rt4831_get_error_flags,
};

static const struct regulator_ops rt4831_dsvpn_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.get_error_flags = rt4831_get_error_flags,
};

static const struct regulator_desc rt4831_regulator_descs[] = {
	{
		.name = "DSVLCM",
		.ops = &rt4831_dsvlcm_ops,
		.of_match = of_match_ptr("DSVLCM"),
		.regulators_node = of_match_ptr("regulators"),
		.type = REGULATOR_VOLTAGE,
		.id = DSV_OUT_VLCM,
		.n_voltages = VLCM_N_VOLTAGES,
		.min_uV = VLCM_MIN_UV,
		.uV_step = STEP_UV,
		.vsel_reg = RT4831_REG_VLCM,
		.vsel_mask = RT4831_VOLT_MASK,
		.bypass_reg = RT4831_REG_DSVEN,
		.bypass_val_on = DSV_MODE_BYPASS,
		.bypass_val_off = DSV_MODE_NORMAL,
		.owner = THIS_MODULE,
	},
	{
		.name = "DSVP",
		.ops = &rt4831_dsvpn_ops,
		.of_match = of_match_ptr("DSVP"),
		.regulators_node = of_match_ptr("regulators"),
		.type = REGULATOR_VOLTAGE,
		.id = DSV_OUT_VPOS,
		.n_voltages = VPN_N_VOLTAGES,
		.min_uV = VPN_MIN_UV,
		.uV_step = STEP_UV,
		.vsel_reg = RT4831_REG_VPOS,
		.vsel_mask = RT4831_VOLT_MASK,
		.enable_reg = RT4831_REG_DSVEN,
		.enable_mask = RT4831_POSEN_MASK,
		.active_discharge_reg = RT4831_REG_DSVEN,
		.active_discharge_mask = RT4831_POSADEN_MASK,
		.owner = THIS_MODULE,
	},
	{
		.name = "DSVN",
		.ops = &rt4831_dsvpn_ops,
		.of_match = of_match_ptr("DSVN"),
		.regulators_node = of_match_ptr("regulators"),
		.type = REGULATOR_VOLTAGE,
		.id = DSV_OUT_VNEG,
		.n_voltages = VPN_N_VOLTAGES,
		.min_uV = VPN_MIN_UV,
		.uV_step = STEP_UV,
		.vsel_reg = RT4831_REG_VNEG,
		.vsel_mask = RT4831_VOLT_MASK,
		.enable_reg = RT4831_REG_DSVEN,
		.enable_mask = RT4831_NEGEN_MASK,
		.active_discharge_reg = RT4831_REG_DSVEN,
		.active_discharge_mask = RT4831_NEGADEN_MASK,
		.owner = THIS_MODULE,
	}
};

static int rt4831_regulator_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct regulator_dev *rdev;
	struct regulator_config config = {};
	int i, ret;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "Failed to init regmap\n");
		return -ENODEV;
	}

	/* Configure DSV mode to normal by default */
	ret = regmap_update_bits(regmap, RT4831_REG_DSVEN, RT4831_DSVMODE_MASK, DSV_MODE_NORMAL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to configure dsv mode to normal\n");
		return ret;
	}

	config.dev = pdev->dev.parent;
	config.regmap = regmap;

	for (i = 0; i < DSV_OUT_MAX; i++) {
		rdev = devm_regulator_register(&pdev->dev, rt4831_regulator_descs + i, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %d regulator\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id rt4831_regulator_match[] = {
	{ "rt4831-regulator", 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, rt4831_regulator_match);

static struct platform_driver rt4831_regulator_driver = {
	.driver = {
		.name = "rt4831-regulator",
	},
	.id_table = rt4831_regulator_match,
	.probe = rt4831_regulator_probe,
};
module_platform_driver(rt4831_regulator_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_LICENSE("GPL v2");
