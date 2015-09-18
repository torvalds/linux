/*
 * s5m8767.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/err.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s5m8767.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

#define S5M8767_OPMODE_NORMAL_MODE 0x1

struct s5m8767_info {
	struct device *dev;
	struct sec_pmic_dev *iodev;
	int num_regulators;
	struct sec_opmode_data *opmode;

	int ramp_delay;
	bool buck2_ramp;
	bool buck3_ramp;
	bool buck4_ramp;

	bool buck2_gpiodvs;
	bool buck3_gpiodvs;
	bool buck4_gpiodvs;
	u8 buck2_vol[8];
	u8 buck3_vol[8];
	u8 buck4_vol[8];
	int buck_gpios[3];
	int buck_ds[3];
	int buck_gpioindex;
};

struct sec_voltage_desc {
	int max;
	int min;
	int step;
};

static const struct sec_voltage_desc buck_voltage_val1 = {
	.max = 2225000,
	.min =  650000,
	.step =   6250,
};

static const struct sec_voltage_desc buck_voltage_val2 = {
	.max = 1600000,
	.min =  600000,
	.step =   6250,
};

static const struct sec_voltage_desc buck_voltage_val3 = {
	.max = 3000000,
	.min =  750000,
	.step =  12500,
};

static const struct sec_voltage_desc ldo_voltage_val1 = {
	.max = 3950000,
	.min =  800000,
	.step =  50000,
};

static const struct sec_voltage_desc ldo_voltage_val2 = {
	.max = 2375000,
	.min =  800000,
	.step =  25000,
};

static const struct sec_voltage_desc *reg_voltage_map[] = {
	[S5M8767_LDO1] = &ldo_voltage_val2,
	[S5M8767_LDO2] = &ldo_voltage_val2,
	[S5M8767_LDO3] = &ldo_voltage_val1,
	[S5M8767_LDO4] = &ldo_voltage_val1,
	[S5M8767_LDO5] = &ldo_voltage_val1,
	[S5M8767_LDO6] = &ldo_voltage_val2,
	[S5M8767_LDO7] = &ldo_voltage_val2,
	[S5M8767_LDO8] = &ldo_voltage_val2,
	[S5M8767_LDO9] = &ldo_voltage_val1,
	[S5M8767_LDO10] = &ldo_voltage_val1,
	[S5M8767_LDO11] = &ldo_voltage_val1,
	[S5M8767_LDO12] = &ldo_voltage_val1,
	[S5M8767_LDO13] = &ldo_voltage_val1,
	[S5M8767_LDO14] = &ldo_voltage_val1,
	[S5M8767_LDO15] = &ldo_voltage_val2,
	[S5M8767_LDO16] = &ldo_voltage_val1,
	[S5M8767_LDO17] = &ldo_voltage_val1,
	[S5M8767_LDO18] = &ldo_voltage_val1,
	[S5M8767_LDO19] = &ldo_voltage_val1,
	[S5M8767_LDO20] = &ldo_voltage_val1,
	[S5M8767_LDO21] = &ldo_voltage_val1,
	[S5M8767_LDO22] = &ldo_voltage_val1,
	[S5M8767_LDO23] = &ldo_voltage_val1,
	[S5M8767_LDO24] = &ldo_voltage_val1,
	[S5M8767_LDO25] = &ldo_voltage_val1,
	[S5M8767_LDO26] = &ldo_voltage_val1,
	[S5M8767_LDO27] = &ldo_voltage_val1,
	[S5M8767_LDO28] = &ldo_voltage_val1,
	[S5M8767_BUCK1] = &buck_voltage_val1,
	[S5M8767_BUCK2] = &buck_voltage_val2,
	[S5M8767_BUCK3] = &buck_voltage_val2,
	[S5M8767_BUCK4] = &buck_voltage_val2,
	[S5M8767_BUCK5] = &buck_voltage_val1,
	[S5M8767_BUCK6] = &buck_voltage_val1,
	[S5M8767_BUCK7] = &buck_voltage_val3,
	[S5M8767_BUCK8] = &buck_voltage_val3,
	[S5M8767_BUCK9] = &buck_voltage_val3,
};

static unsigned int s5m8767_opmode_reg[][4] = {
	/* {OFF, ON, LOWPOWER, SUSPEND} */
	/* LDO1 ... LDO28 */
	{0x0, 0x3, 0x2, 0x1}, /* LDO1 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x0, 0x0, 0x0},
	{0x0, 0x3, 0x2, 0x1}, /* LDO5 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* LDO10 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* LDO15 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x0, 0x0, 0x0},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* LDO20 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x0, 0x0, 0x0},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* LDO25 */
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* LDO28 */

	/* BUCK1 ... BUCK9 */
	{0x0, 0x3, 0x1, 0x1}, /* BUCK1 */
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x2, 0x1}, /* BUCK5 */
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x1, 0x1},
	{0x0, 0x3, 0x1, 0x1}, /* BUCK9 */
};

static int s5m8767_get_register(struct s5m8767_info *s5m8767, int reg_id,
				int *reg, int *enable_ctrl)
{
	int i;
	unsigned int mode;

	switch (reg_id) {
	case S5M8767_LDO1 ... S5M8767_LDO2:
		*reg = S5M8767_REG_LDO1CTRL + (reg_id - S5M8767_LDO1);
		break;
	case S5M8767_LDO3 ... S5M8767_LDO28:
		*reg = S5M8767_REG_LDO3CTRL + (reg_id - S5M8767_LDO3);
		break;
	case S5M8767_BUCK1:
		*reg = S5M8767_REG_BUCK1CTRL1;
		break;
	case S5M8767_BUCK2 ... S5M8767_BUCK4:
		*reg = S5M8767_REG_BUCK2CTRL + (reg_id - S5M8767_BUCK2) * 9;
		break;
	case S5M8767_BUCK5:
		*reg = S5M8767_REG_BUCK5CTRL1;
		break;
	case S5M8767_BUCK6 ... S5M8767_BUCK9:
		*reg = S5M8767_REG_BUCK6CTRL1 + (reg_id - S5M8767_BUCK6) * 2;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < s5m8767->num_regulators; i++) {
		if (s5m8767->opmode[i].id == reg_id) {
			mode = s5m8767->opmode[i].mode;
			break;
		}
	}

	if (i < s5m8767->num_regulators)
		*enable_ctrl =
		s5m8767_opmode_reg[reg_id][mode] << S5M8767_ENCTRL_SHIFT;

	return 0;
}

static int s5m8767_get_vsel_reg(int reg_id, struct s5m8767_info *s5m8767)
{
	int reg;

	switch (reg_id) {
	case S5M8767_LDO1 ... S5M8767_LDO2:
		reg = S5M8767_REG_LDO1CTRL + (reg_id - S5M8767_LDO1);
		break;
	case S5M8767_LDO3 ... S5M8767_LDO28:
		reg = S5M8767_REG_LDO3CTRL + (reg_id - S5M8767_LDO3);
		break;
	case S5M8767_BUCK1:
		reg = S5M8767_REG_BUCK1CTRL2;
		break;
	case S5M8767_BUCK2:
		reg = S5M8767_REG_BUCK2DVS1;
		if (s5m8767->buck2_gpiodvs)
			reg += s5m8767->buck_gpioindex;
		break;
	case S5M8767_BUCK3:
		reg = S5M8767_REG_BUCK3DVS1;
		if (s5m8767->buck3_gpiodvs)
			reg += s5m8767->buck_gpioindex;
		break;
	case S5M8767_BUCK4:
		reg = S5M8767_REG_BUCK4DVS1;
		if (s5m8767->buck4_gpiodvs)
			reg += s5m8767->buck_gpioindex;
		break;
	case S5M8767_BUCK5:
		reg = S5M8767_REG_BUCK5CTRL2;
		break;
	case S5M8767_BUCK6 ... S5M8767_BUCK9:
		reg = S5M8767_REG_BUCK6CTRL2 + (reg_id - S5M8767_BUCK6) * 2;
		break;
	default:
		return -EINVAL;
	}

	return reg;
}

static int s5m8767_convert_voltage_to_sel(const struct sec_voltage_desc *desc,
					  int min_vol)
{
	int selector = 0;

	if (desc == NULL)
		return -EINVAL;

	if (min_vol > desc->max)
		return -EINVAL;

	if (min_vol < desc->min)
		min_vol = desc->min;

	selector = DIV_ROUND_UP(min_vol - desc->min, desc->step);

	if (desc->min + desc->step * selector > desc->max)
		return -EINVAL;

	return selector;
}

static inline int s5m8767_set_high(struct s5m8767_info *s5m8767)
{
	int temp_index = s5m8767->buck_gpioindex;

	gpio_set_value(s5m8767->buck_gpios[0], (temp_index >> 2) & 0x1);
	gpio_set_value(s5m8767->buck_gpios[1], (temp_index >> 1) & 0x1);
	gpio_set_value(s5m8767->buck_gpios[2], temp_index & 0x1);

	return 0;
}

static inline int s5m8767_set_low(struct s5m8767_info *s5m8767)
{
	int temp_index = s5m8767->buck_gpioindex;

	gpio_set_value(s5m8767->buck_gpios[2], temp_index & 0x1);
	gpio_set_value(s5m8767->buck_gpios[1], (temp_index >> 1) & 0x1);
	gpio_set_value(s5m8767->buck_gpios[0], (temp_index >> 2) & 0x1);

	return 0;
}

static int s5m8767_set_voltage_sel(struct regulator_dev *rdev,
				   unsigned selector)
{
	struct s5m8767_info *s5m8767 = rdev_get_drvdata(rdev);
	int reg_id = rdev_get_id(rdev);
	int old_index, index = 0;
	u8 *buck234_vol = NULL;

	switch (reg_id) {
	case S5M8767_LDO1 ... S5M8767_LDO28:
		break;
	case S5M8767_BUCK1 ... S5M8767_BUCK6:
		if (reg_id == S5M8767_BUCK2 && s5m8767->buck2_gpiodvs)
			buck234_vol = &s5m8767->buck2_vol[0];
		else if (reg_id == S5M8767_BUCK3 && s5m8767->buck3_gpiodvs)
			buck234_vol = &s5m8767->buck3_vol[0];
		else if (reg_id == S5M8767_BUCK4 && s5m8767->buck4_gpiodvs)
			buck234_vol = &s5m8767->buck4_vol[0];
		break;
	case S5M8767_BUCK7 ... S5M8767_BUCK8:
		return -EINVAL;
	case S5M8767_BUCK9:
		break;
	default:
		return -EINVAL;
	}

	/* buck234_vol != NULL means to control buck234 voltage via DVS GPIO */
	if (buck234_vol) {
		while (*buck234_vol != selector) {
			buck234_vol++;
			index++;
		}
		old_index = s5m8767->buck_gpioindex;
		s5m8767->buck_gpioindex = index;

		if (index > old_index)
			return s5m8767_set_high(s5m8767);
		else
			return s5m8767_set_low(s5m8767);
	} else {
		return regulator_set_voltage_sel_regmap(rdev, selector);
	}
}

static int s5m8767_set_voltage_time_sel(struct regulator_dev *rdev,
					     unsigned int old_sel,
					     unsigned int new_sel)
{
	struct s5m8767_info *s5m8767 = rdev_get_drvdata(rdev);
	const struct sec_voltage_desc *desc;
	int reg_id = rdev_get_id(rdev);

	desc = reg_voltage_map[reg_id];

	if ((old_sel < new_sel) && s5m8767->ramp_delay)
		return DIV_ROUND_UP(desc->step * (new_sel - old_sel),
					s5m8767->ramp_delay * 1000);
	return 0;
}

static struct regulator_ops s5m8767_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= s5m8767_set_voltage_sel,
	.set_voltage_time_sel	= s5m8767_set_voltage_time_sel,
};

static struct regulator_ops s5m8767_buck78_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

#define s5m8767_regulator_desc(_name) {		\
	.name		= #_name,		\
	.id		= S5M8767_##_name,	\
	.ops		= &s5m8767_ops,		\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}

#define s5m8767_regulator_buck78_desc(_name) {	\
	.name		= #_name,		\
	.id		= S5M8767_##_name,	\
	.ops		= &s5m8767_buck78_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}

static struct regulator_desc regulators[] = {
	s5m8767_regulator_desc(LDO1),
	s5m8767_regulator_desc(LDO2),
	s5m8767_regulator_desc(LDO3),
	s5m8767_regulator_desc(LDO4),
	s5m8767_regulator_desc(LDO5),
	s5m8767_regulator_desc(LDO6),
	s5m8767_regulator_desc(LDO7),
	s5m8767_regulator_desc(LDO8),
	s5m8767_regulator_desc(LDO9),
	s5m8767_regulator_desc(LDO10),
	s5m8767_regulator_desc(LDO11),
	s5m8767_regulator_desc(LDO12),
	s5m8767_regulator_desc(LDO13),
	s5m8767_regulator_desc(LDO14),
	s5m8767_regulator_desc(LDO15),
	s5m8767_regulator_desc(LDO16),
	s5m8767_regulator_desc(LDO17),
	s5m8767_regulator_desc(LDO18),
	s5m8767_regulator_desc(LDO19),
	s5m8767_regulator_desc(LDO20),
	s5m8767_regulator_desc(LDO21),
	s5m8767_regulator_desc(LDO22),
	s5m8767_regulator_desc(LDO23),
	s5m8767_regulator_desc(LDO24),
	s5m8767_regulator_desc(LDO25),
	s5m8767_regulator_desc(LDO26),
	s5m8767_regulator_desc(LDO27),
	s5m8767_regulator_desc(LDO28),
	s5m8767_regulator_desc(BUCK1),
	s5m8767_regulator_desc(BUCK2),
	s5m8767_regulator_desc(BUCK3),
	s5m8767_regulator_desc(BUCK4),
	s5m8767_regulator_desc(BUCK5),
	s5m8767_regulator_desc(BUCK6),
	s5m8767_regulator_buck78_desc(BUCK7),
	s5m8767_regulator_buck78_desc(BUCK8),
	s5m8767_regulator_desc(BUCK9),
};

/*
 * Enable GPIO control over BUCK9 in regulator_config for that regulator.
 */
static void s5m8767_regulator_config_ext_control(struct s5m8767_info *s5m8767,
		struct sec_regulator_data *rdata,
		struct regulator_config *config)
{
	int i, mode = 0;

	if (rdata->id != S5M8767_BUCK9)
		return;

	/* Check if opmode for regulator matches S5M8767_ENCTRL_USE_GPIO */
	for (i = 0; i < s5m8767->num_regulators; i++) {
		const struct sec_opmode_data *opmode = &s5m8767->opmode[i];
		if (opmode->id == rdata->id) {
			mode = s5m8767_opmode_reg[rdata->id][opmode->mode];
			break;
		}
	}
	if (mode != S5M8767_ENCTRL_USE_GPIO) {
		dev_warn(s5m8767->dev,
				"ext-control for %s: mismatched op_mode (%x), ignoring\n",
				rdata->reg_node->name, mode);
		return;
	}

	if (!gpio_is_valid(rdata->ext_control_gpio)) {
		dev_warn(s5m8767->dev,
				"ext-control for %s: GPIO not valid, ignoring\n",
				rdata->reg_node->name);
		return;
	}

	config->ena_gpio = rdata->ext_control_gpio;
	config->ena_gpio_flags = GPIOF_OUT_INIT_HIGH;
}

/*
 * Turn on GPIO control over BUCK9.
 */
static int s5m8767_enable_ext_control(struct s5m8767_info *s5m8767,
		struct regulator_dev *rdev)
{
	int id = rdev_get_id(rdev);
	int ret, reg, enable_ctrl;

	if (id != S5M8767_BUCK9)
		return -EINVAL;

	ret = s5m8767_get_register(s5m8767, id, &reg, &enable_ctrl);
	if (ret)
		return ret;

	return regmap_update_bits(s5m8767->iodev->regmap_pmic,
			reg, S5M8767_ENCTRL_MASK,
			S5M8767_ENCTRL_USE_GPIO << S5M8767_ENCTRL_SHIFT);
}


#ifdef CONFIG_OF
static int s5m8767_pmic_dt_parse_dvs_gpio(struct sec_pmic_dev *iodev,
			struct sec_platform_data *pdata,
			struct device_node *pmic_np)
{
	int i, gpio;

	for (i = 0; i < 3; i++) {
		gpio = of_get_named_gpio(pmic_np,
					"s5m8767,pmic-buck-dvs-gpios", i);
		if (!gpio_is_valid(gpio)) {
			dev_err(iodev->dev, "invalid gpio[%d]: %d\n", i, gpio);
			return -EINVAL;
		}
		pdata->buck_gpios[i] = gpio;
	}
	return 0;
}

static int s5m8767_pmic_dt_parse_ds_gpio(struct sec_pmic_dev *iodev,
			struct sec_platform_data *pdata,
			struct device_node *pmic_np)
{
	int i, gpio;

	for (i = 0; i < 3; i++) {
		gpio = of_get_named_gpio(pmic_np,
					"s5m8767,pmic-buck-ds-gpios", i);
		if (!gpio_is_valid(gpio)) {
			dev_err(iodev->dev, "invalid gpio[%d]: %d\n", i, gpio);
			return -EINVAL;
		}
		pdata->buck_ds[i] = gpio;
	}
	return 0;
}

static int s5m8767_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct sec_platform_data *pdata)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct device_node *pmic_np, *regulators_np, *reg_np;
	struct sec_regulator_data *rdata;
	struct sec_opmode_data *rmode;
	unsigned int i, dvs_voltage_nr = 8, ret;

	pmic_np = iodev->dev->of_node;
	if (!pmic_np) {
		dev_err(iodev->dev, "could not find pmic sub-node\n");
		return -ENODEV;
	}

	regulators_np = of_get_child_by_name(pmic_np, "regulators");
	if (!regulators_np) {
		dev_err(iodev->dev, "could not find regulators sub-node\n");
		return -EINVAL;
	}

	/* count the number of regulators to be supported in pmic */
	pdata->num_regulators = of_get_child_count(regulators_np);

	rdata = devm_kzalloc(&pdev->dev, sizeof(*rdata) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rdata)
		return -ENOMEM;

	rmode = devm_kzalloc(&pdev->dev, sizeof(*rmode) *
				pdata->num_regulators, GFP_KERNEL);
	if (!rmode)
		return -ENOMEM;

	pdata->regulators = rdata;
	pdata->opmode = rmode;
	for_each_child_of_node(regulators_np, reg_np) {
		for (i = 0; i < ARRAY_SIZE(regulators); i++)
			if (!of_node_cmp(reg_np->name, regulators[i].name))
				break;

		if (i == ARRAY_SIZE(regulators)) {
			dev_warn(iodev->dev,
			"don't know how to configure regulator %s\n",
			reg_np->name);
			continue;
		}

		rdata->ext_control_gpio = of_get_named_gpio(reg_np,
			"s5m8767,pmic-ext-control-gpios", 0);

		rdata->id = i;
		rdata->initdata = of_get_regulator_init_data(
						&pdev->dev, reg_np,
						&regulators[i]);
		rdata->reg_node = reg_np;
		rdata++;
		rmode->id = i;
		if (of_property_read_u32(reg_np, "op_mode",
				&rmode->mode)) {
			dev_warn(iodev->dev,
				"no op_mode property property at %s\n",
				reg_np->full_name);

			rmode->mode = S5M8767_OPMODE_NORMAL_MODE;
		}
		rmode++;
	}

	of_node_put(regulators_np);

	if (of_get_property(pmic_np, "s5m8767,pmic-buck2-uses-gpio-dvs", NULL)) {
		pdata->buck2_gpiodvs = true;

		if (of_property_read_u32_array(pmic_np,
				"s5m8767,pmic-buck2-dvs-voltage",
				pdata->buck2_voltage, dvs_voltage_nr)) {
			dev_err(iodev->dev, "buck2 voltages not specified\n");
			return -EINVAL;
		}
	}

	if (of_get_property(pmic_np, "s5m8767,pmic-buck3-uses-gpio-dvs", NULL)) {
		pdata->buck3_gpiodvs = true;

		if (of_property_read_u32_array(pmic_np,
				"s5m8767,pmic-buck3-dvs-voltage",
				pdata->buck3_voltage, dvs_voltage_nr)) {
			dev_err(iodev->dev, "buck3 voltages not specified\n");
			return -EINVAL;
		}
	}

	if (of_get_property(pmic_np, "s5m8767,pmic-buck4-uses-gpio-dvs", NULL)) {
		pdata->buck4_gpiodvs = true;

		if (of_property_read_u32_array(pmic_np,
				"s5m8767,pmic-buck4-dvs-voltage",
				pdata->buck4_voltage, dvs_voltage_nr)) {
			dev_err(iodev->dev, "buck4 voltages not specified\n");
			return -EINVAL;
		}
	}

	if (pdata->buck2_gpiodvs || pdata->buck3_gpiodvs ||
						pdata->buck4_gpiodvs) {
		ret = s5m8767_pmic_dt_parse_dvs_gpio(iodev, pdata, pmic_np);
		if (ret)
			return -EINVAL;

		if (of_property_read_u32(pmic_np,
				"s5m8767,pmic-buck-default-dvs-idx",
				&pdata->buck_default_idx)) {
			pdata->buck_default_idx = 0;
		} else {
			if (pdata->buck_default_idx >= 8) {
				pdata->buck_default_idx = 0;
				dev_info(iodev->dev,
				"invalid value for default dvs index, use 0\n");
			}
		}
	}

	ret = s5m8767_pmic_dt_parse_ds_gpio(iodev, pdata, pmic_np);
	if (ret)
		return -EINVAL;

	if (of_get_property(pmic_np, "s5m8767,pmic-buck2-ramp-enable", NULL))
		pdata->buck2_ramp_enable = true;

	if (of_get_property(pmic_np, "s5m8767,pmic-buck3-ramp-enable", NULL))
		pdata->buck3_ramp_enable = true;

	if (of_get_property(pmic_np, "s5m8767,pmic-buck4-ramp-enable", NULL))
		pdata->buck4_ramp_enable = true;

	if (pdata->buck2_ramp_enable || pdata->buck3_ramp_enable
			|| pdata->buck4_ramp_enable) {
		if (of_property_read_u32(pmic_np, "s5m8767,pmic-buck-ramp-delay",
				&pdata->buck_ramp_delay))
			pdata->buck_ramp_delay = 0;
	}

	return 0;
}
#else
static int s5m8767_pmic_dt_parse_pdata(struct platform_device *pdev,
					struct sec_platform_data *pdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int s5m8767_pmic_probe(struct platform_device *pdev)
{
	struct sec_pmic_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct sec_platform_data *pdata = iodev->pdata;
	struct regulator_config config = { };
	struct s5m8767_info *s5m8767;
	int i, ret, buck_init;

	if (!pdata) {
		dev_err(pdev->dev.parent, "Platform data not supplied\n");
		return -ENODEV;
	}

	if (iodev->dev->of_node) {
		ret = s5m8767_pmic_dt_parse_pdata(pdev, pdata);
		if (ret)
			return ret;
	}

	if (pdata->buck2_gpiodvs) {
		if (pdata->buck3_gpiodvs || pdata->buck4_gpiodvs) {
			dev_err(&pdev->dev, "S5M8767 GPIO DVS NOT VALID\n");
			return -EINVAL;
		}
	}

	if (pdata->buck3_gpiodvs) {
		if (pdata->buck2_gpiodvs || pdata->buck4_gpiodvs) {
			dev_err(&pdev->dev, "S5M8767 GPIO DVS NOT VALID\n");
			return -EINVAL;
		}
	}

	if (pdata->buck4_gpiodvs) {
		if (pdata->buck2_gpiodvs || pdata->buck3_gpiodvs) {
			dev_err(&pdev->dev, "S5M8767 GPIO DVS NOT VALID\n");
			return -EINVAL;
		}
	}

	s5m8767 = devm_kzalloc(&pdev->dev, sizeof(struct s5m8767_info),
				GFP_KERNEL);
	if (!s5m8767)
		return -ENOMEM;

	s5m8767->dev = &pdev->dev;
	s5m8767->iodev = iodev;
	s5m8767->num_regulators = pdata->num_regulators;
	platform_set_drvdata(pdev, s5m8767);

	s5m8767->buck_gpioindex = pdata->buck_default_idx;
	s5m8767->buck2_gpiodvs = pdata->buck2_gpiodvs;
	s5m8767->buck3_gpiodvs = pdata->buck3_gpiodvs;
	s5m8767->buck4_gpiodvs = pdata->buck4_gpiodvs;
	s5m8767->buck_gpios[0] = pdata->buck_gpios[0];
	s5m8767->buck_gpios[1] = pdata->buck_gpios[1];
	s5m8767->buck_gpios[2] = pdata->buck_gpios[2];
	s5m8767->buck_ds[0] = pdata->buck_ds[0];
	s5m8767->buck_ds[1] = pdata->buck_ds[1];
	s5m8767->buck_ds[2] = pdata->buck_ds[2];

	s5m8767->ramp_delay = pdata->buck_ramp_delay;
	s5m8767->buck2_ramp = pdata->buck2_ramp_enable;
	s5m8767->buck3_ramp = pdata->buck3_ramp_enable;
	s5m8767->buck4_ramp = pdata->buck4_ramp_enable;
	s5m8767->opmode = pdata->opmode;

	buck_init = s5m8767_convert_voltage_to_sel(&buck_voltage_val2,
						   pdata->buck2_init);

	regmap_write(s5m8767->iodev->regmap_pmic, S5M8767_REG_BUCK2DVS2,
			buck_init);

	buck_init = s5m8767_convert_voltage_to_sel(&buck_voltage_val2,
						   pdata->buck3_init);

	regmap_write(s5m8767->iodev->regmap_pmic, S5M8767_REG_BUCK3DVS2,
			buck_init);

	buck_init = s5m8767_convert_voltage_to_sel(&buck_voltage_val2,
						   pdata->buck4_init);

	regmap_write(s5m8767->iodev->regmap_pmic, S5M8767_REG_BUCK4DVS2,
			buck_init);

	for (i = 0; i < 8; i++) {
		if (s5m8767->buck2_gpiodvs) {
			s5m8767->buck2_vol[i] =
				s5m8767_convert_voltage_to_sel(
						&buck_voltage_val2,
						pdata->buck2_voltage[i]);
		}

		if (s5m8767->buck3_gpiodvs) {
			s5m8767->buck3_vol[i] =
				s5m8767_convert_voltage_to_sel(
						&buck_voltage_val2,
						pdata->buck3_voltage[i]);
		}

		if (s5m8767->buck4_gpiodvs) {
			s5m8767->buck4_vol[i] =
				s5m8767_convert_voltage_to_sel(
						&buck_voltage_val2,
						pdata->buck4_voltage[i]);
		}
	}

	if (pdata->buck2_gpiodvs || pdata->buck3_gpiodvs ||
						pdata->buck4_gpiodvs) {

		if (!gpio_is_valid(pdata->buck_gpios[0]) ||
			!gpio_is_valid(pdata->buck_gpios[1]) ||
			!gpio_is_valid(pdata->buck_gpios[2])) {
			dev_err(&pdev->dev, "GPIO NOT VALID\n");
			return -EINVAL;
		}

		ret = devm_gpio_request(&pdev->dev, pdata->buck_gpios[0],
					"S5M8767 SET1");
		if (ret)
			return ret;

		ret = devm_gpio_request(&pdev->dev, pdata->buck_gpios[1],
					"S5M8767 SET2");
		if (ret)
			return ret;

		ret = devm_gpio_request(&pdev->dev, pdata->buck_gpios[2],
					"S5M8767 SET3");
		if (ret)
			return ret;

		/* SET1 GPIO */
		gpio_direction_output(pdata->buck_gpios[0],
				(s5m8767->buck_gpioindex >> 2) & 0x1);
		/* SET2 GPIO */
		gpio_direction_output(pdata->buck_gpios[1],
				(s5m8767->buck_gpioindex >> 1) & 0x1);
		/* SET3 GPIO */
		gpio_direction_output(pdata->buck_gpios[2],
				(s5m8767->buck_gpioindex >> 0) & 0x1);
	}

	ret = devm_gpio_request(&pdev->dev, pdata->buck_ds[0], "S5M8767 DS2");
	if (ret)
		return ret;

	ret = devm_gpio_request(&pdev->dev, pdata->buck_ds[1], "S5M8767 DS3");
	if (ret)
		return ret;

	ret = devm_gpio_request(&pdev->dev, pdata->buck_ds[2], "S5M8767 DS4");
	if (ret)
		return ret;

	/* DS2 GPIO */
	gpio_direction_output(pdata->buck_ds[0], 0x0);
	/* DS3 GPIO */
	gpio_direction_output(pdata->buck_ds[1], 0x0);
	/* DS4 GPIO */
	gpio_direction_output(pdata->buck_ds[2], 0x0);

	if (pdata->buck2_gpiodvs || pdata->buck3_gpiodvs ||
	   pdata->buck4_gpiodvs) {
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_BUCK2CTRL, 1 << 1,
				(pdata->buck2_gpiodvs) ? (1 << 1) : (0 << 1));
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_BUCK3CTRL, 1 << 1,
				(pdata->buck3_gpiodvs) ? (1 << 1) : (0 << 1));
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_BUCK4CTRL, 1 << 1,
				(pdata->buck4_gpiodvs) ? (1 << 1) : (0 << 1));
	}

	/* Initialize GPIO DVS registers */
	for (i = 0; i < 8; i++) {
		if (s5m8767->buck2_gpiodvs) {
			regmap_write(s5m8767->iodev->regmap_pmic,
					S5M8767_REG_BUCK2DVS1 + i,
					s5m8767->buck2_vol[i]);
		}

		if (s5m8767->buck3_gpiodvs) {
			regmap_write(s5m8767->iodev->regmap_pmic,
					S5M8767_REG_BUCK3DVS1 + i,
					s5m8767->buck3_vol[i]);
		}

		if (s5m8767->buck4_gpiodvs) {
			regmap_write(s5m8767->iodev->regmap_pmic,
					S5M8767_REG_BUCK4DVS1 + i,
					s5m8767->buck4_vol[i]);
		}
	}

	if (s5m8767->buck2_ramp)
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_DVSRAMP, 0x08, 0x08);

	if (s5m8767->buck3_ramp)
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_DVSRAMP, 0x04, 0x04);

	if (s5m8767->buck4_ramp)
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
				S5M8767_REG_DVSRAMP, 0x02, 0x02);

	if (s5m8767->buck2_ramp || s5m8767->buck3_ramp
		|| s5m8767->buck4_ramp) {
		unsigned int val;
		switch (s5m8767->ramp_delay) {
		case 5:
			val = S5M8767_DVS_BUCK_RAMP_5;
			break;
		case 10:
			val = S5M8767_DVS_BUCK_RAMP_10;
			break;
		case 25:
			val = S5M8767_DVS_BUCK_RAMP_25;
			break;
		case 50:
			val = S5M8767_DVS_BUCK_RAMP_50;
			break;
		case 100:
			val = S5M8767_DVS_BUCK_RAMP_100;
			break;
		default:
			val = S5M8767_DVS_BUCK_RAMP_10;
		}
		regmap_update_bits(s5m8767->iodev->regmap_pmic,
					S5M8767_REG_DVSRAMP,
					S5M8767_DVS_BUCK_RAMP_MASK,
					val << S5M8767_DVS_BUCK_RAMP_SHIFT);
	}

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct sec_voltage_desc *desc;
		int id = pdata->regulators[i].id;
		int enable_reg, enable_val;
		struct regulator_dev *rdev;

		desc = reg_voltage_map[id];
		if (desc) {
			regulators[id].n_voltages =
				(desc->max - desc->min) / desc->step + 1;
			regulators[id].min_uV = desc->min;
			regulators[id].uV_step = desc->step;
			regulators[id].vsel_reg =
				s5m8767_get_vsel_reg(id, s5m8767);
			if (id < S5M8767_BUCK1)
				regulators[id].vsel_mask = 0x3f;
			else
				regulators[id].vsel_mask = 0xff;

			s5m8767_get_register(s5m8767, id, &enable_reg,
					     &enable_val);
			regulators[id].enable_reg = enable_reg;
			regulators[id].enable_mask = S5M8767_ENCTRL_MASK;
			regulators[id].enable_val = enable_val;
		}

		config.dev = s5m8767->dev;
		config.init_data = pdata->regulators[i].initdata;
		config.driver_data = s5m8767;
		config.regmap = iodev->regmap_pmic;
		config.of_node = pdata->regulators[i].reg_node;
		config.ena_gpio = -EINVAL;
		config.ena_gpio_flags = 0;
		config.ena_gpio_initialized = true;
		if (gpio_is_valid(pdata->regulators[i].ext_control_gpio))
			s5m8767_regulator_config_ext_control(s5m8767,
					&pdata->regulators[i], &config);

		rdev = devm_regulator_register(&pdev->dev, &regulators[id],
						  &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(s5m8767->dev, "regulator init failed for %d\n",
					id);
			return ret;
		}

		if (gpio_is_valid(pdata->regulators[i].ext_control_gpio)) {
			ret = s5m8767_enable_ext_control(s5m8767, rdev);
			if (ret < 0) {
				dev_err(s5m8767->dev,
						"failed to enable gpio control over %s: %d\n",
						rdev->desc->name, ret);
				return ret;
			}
		}
	}

	return 0;
}

static const struct platform_device_id s5m8767_pmic_id[] = {
	{ "s5m8767-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, s5m8767_pmic_id);

static struct platform_driver s5m8767_pmic_driver = {
	.driver = {
		.name = "s5m8767-pmic",
	},
	.probe = s5m8767_pmic_probe,
	.id_table = s5m8767_pmic_id,
};

static int __init s5m8767_pmic_init(void)
{
	return platform_driver_register(&s5m8767_pmic_driver);
}
subsys_initcall(s5m8767_pmic_init);

static void __exit s5m8767_pmic_exit(void)
{
	platform_driver_unregister(&s5m8767_pmic_driver);
}
module_exit(s5m8767_pmic_exit);

/* Module information */
MODULE_AUTHOR("Sangbeom Kim <sbkim73@samsung.com>");
MODULE_DESCRIPTION("SAMSUNG S5M8767 Regulator Driver");
MODULE_LICENSE("GPL");
