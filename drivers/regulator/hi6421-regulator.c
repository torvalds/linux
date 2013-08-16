/*
 * Device driver for regulators in Hi6421 IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/hi6421-pmic.h>
#include <linux/delay.h>
#include <linux/time.h>

struct hi6421_regulator_register_info {
	u32 ctrl_reg;
	u32 enable_mask;
	u32 eco_mode_mask;
	u32 vset_reg;
	u32 vset_mask;
};

struct hi6421_regulator {
	const char *name;
	struct hi6421_regulator_register_info register_info;
	struct timeval last_off_time;
	u32 off_on_delay;
	u32 eco_uA;
	struct regulator_desc rdesc;
	int (*dt_parse)(struct hi6421_regulator *, struct platform_device *);
};

static inline struct hi6421_pmic *rdev_to_pmic(struct regulator_dev *dev)
{
	/* regulator_dev parent to->
	 * hi6421 regulator platform device_dev parent to->
	 * hi6421 pmic platform device_dev
	 */
	return dev_get_drvdata(rdev_get_dev(dev)->parent->parent);
}

/* helper function to ensure when it returns it is at least 'delay_us'
 * microseconds after 'since'.
 */
static void ensured_time_after(struct timeval since, u32 delay_us)
{
	struct timeval now;
	u64 elapsed_ns64, delay_ns64;
	u32 actual_us32;

	delay_ns64 = delay_us * NSEC_PER_USEC;
	do_gettimeofday(&now);
	elapsed_ns64 = timeval_to_ns(&now) - timeval_to_ns(&since);
	if (delay_ns64 > elapsed_ns64) {
		actual_us32 = ((u32)(delay_ns64 - elapsed_ns64) /
							NSEC_PER_USEC);
		if (actual_us32 >= 1000) {
			mdelay(actual_us32 / 1000);
			udelay(actual_us32 % 1000);
		} else if (actual_us32 > 0) {
			udelay(actual_us32);
		}
	}
	return;
}

static int hi6421_regulator_is_enabled(struct regulator_dev *dev)
{
	u32 reg_val;
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);

	reg_val = hi6421_pmic_read(pmic, sreg->register_info.ctrl_reg);

	return ((reg_val & sreg->register_info.enable_mask) != 0);
}

static int hi6421_regulator_enable(struct regulator_dev *dev)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);

	/* keep a distance of off_on_delay from last time disabled */
	ensured_time_after(sreg->last_off_time, sreg->off_on_delay);

	/* cannot enable more than one regulator at one time */
	mutex_lock(&pmic->enable_mutex);
	ensured_time_after(pmic->last_enabled, HI6421_REGS_ENA_PROTECT_TIME);

	/* set enable register */
	hi6421_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
				sreg->register_info.enable_mask,
				sreg->register_info.enable_mask);

	do_gettimeofday(&pmic->last_enabled);
	mutex_unlock(&pmic->enable_mutex);

	return 0;
}

static int hi6421_regulator_disable(struct regulator_dev *dev)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);

	/* set enable register to 0 */
	hi6421_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
				sreg->register_info.enable_mask, 0);

	do_gettimeofday(&sreg->last_off_time);

	return 0;
}

static int hi6421_regulator_get_voltage(struct regulator_dev *dev)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);
	u32 reg_val, selector;

	/* get voltage selector */
	reg_val = hi6421_pmic_read(pmic, sreg->register_info.vset_reg);
	selector = (reg_val & sreg->register_info.vset_mask) >>
				(ffs(sreg->register_info.vset_mask) - 1);

	return sreg->rdesc.ops->list_voltage(dev, selector);
}

static int hi6421_regulator_ldo_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV, unsigned *selector)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);
	u32 vsel;
	int ret = 0;

	for (vsel = 0; vsel < sreg->rdesc.n_voltages; vsel++) {
		int uV = sreg->rdesc.volt_table[vsel];
		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV)
			break;
	}

	/* unlikely to happen. sanity test done by regulator core */
	if (unlikely(vsel == sreg->rdesc.n_voltages))
		return -EINVAL;

	*selector = vsel;
	/* set voltage selector */
	hi6421_pmic_rmw(pmic, sreg->register_info.vset_reg,
		sreg->register_info.vset_mask,
		vsel << (ffs(sreg->register_info.vset_mask) - 1));

	return ret;
}

static int hi6421_regulator_buck012_set_voltage(struct regulator_dev *dev,
				int min_uV, int max_uV, unsigned *selector)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);
	u32 vsel;
	int ret = 0;

	vsel = DIV_ROUND_UP((max_uV - sreg->rdesc.min_uV),
				sreg->rdesc.uV_step);

	*selector = vsel;
	/* set voltage selector */
	hi6421_pmic_rmw(pmic, sreg->register_info.vset_reg,
		sreg->register_info.vset_mask,
		vsel << (ffs(sreg->register_info.vset_mask) - 1));

	return ret;
}

static unsigned int hi6421_regulator_get_mode(struct regulator_dev *dev)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);
	u32 reg_val;

	reg_val = hi6421_pmic_read(pmic, sreg->register_info.ctrl_reg);
	if (reg_val & sreg->register_info.eco_mode_mask)
		return REGULATOR_MODE_IDLE;
	else
		return REGULATOR_MODE_NORMAL;
}

static int hi6421_regulator_set_mode(struct regulator_dev *dev,
						unsigned int mode)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);
	struct hi6421_pmic *pmic = rdev_to_pmic(dev);
	u32 eco_mode;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		eco_mode = HI6421_ECO_MODE_DISABLE;
		break;
	case REGULATOR_MODE_IDLE:
		eco_mode = HI6421_ECO_MODE_ENABLE;
		break;
	default:
		return -EINVAL;
	}

	/* set mode */
	hi6421_pmic_rmw(pmic, sreg->register_info.ctrl_reg,
		sreg->register_info.eco_mode_mask,
		eco_mode << (ffs(sreg->register_info.eco_mode_mask) - 1));

	return 0;
}


unsigned int hi6421_regulator_get_optimum_mode(struct regulator_dev *dev,
			int input_uV, int output_uV, int load_uA)
{
	struct hi6421_regulator *sreg = rdev_get_drvdata(dev);

	if ((load_uA == 0) || (load_uA > sreg->eco_uA))
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_IDLE;
}

static int hi6421_dt_parse_common(struct hi6421_regulator *sreg,
					struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc = &sreg->rdesc;
	unsigned int register_info[3];
	int ret = 0;

	/* parse .register_info.ctrl_reg */
	ret = of_property_read_u32_array(np, "hisilicon,hi6421-ctrl",
						register_info, 3);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-ctrl property set\n");
		goto dt_parse_common_end;
	}
	sreg->register_info.ctrl_reg = register_info[0];
	sreg->register_info.enable_mask = register_info[1];
	sreg->register_info.eco_mode_mask = register_info[2];

	/* parse .register_info.vset_reg */
	ret = of_property_read_u32_array(np, "hisilicon,hi6421-vset",
						register_info, 2);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-vset property set\n");
		goto dt_parse_common_end;
	}
	sreg->register_info.vset_reg = register_info[0];
	sreg->register_info.vset_mask = register_info[1];

	/* parse .off-on-delay */
	ret = of_property_read_u32(np, "hisilicon,hi6421-off-on-delay-us",
						&sreg->off_on_delay);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-off-on-delay-us property set\n");
		goto dt_parse_common_end;
	}

	/* parse .enable_time */
	ret = of_property_read_u32(np, "hisilicon,hi6421-enable-time-us",
				   &rdesc->enable_time);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-enable-time-us property set\n");
		goto dt_parse_common_end;
	}

	/* parse .eco_uA */
	ret = of_property_read_u32(np, "hisilicon,hi6421-eco-microamp",
				   &sreg->eco_uA);
	if (ret) {
		sreg->eco_uA = 0;
		ret = 0;
	}

dt_parse_common_end:
	return ret;
}

static int hi6421_dt_parse_ldo(struct hi6421_regulator *sreg,
				struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc = &sreg->rdesc;
	unsigned int *v_table;
	int ret = 0;

	/* parse .n_voltages, and .volt_table */
	ret = of_property_read_u32(np, "hisilicon,hi6421-n-voltages",
				   &rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-n-voltages property set\n");
		goto dt_parse_ldo_end;
	}

	/* alloc space for .volt_table */
	v_table = devm_kzalloc(dev, sizeof(unsigned int) * rdesc->n_voltages,
								GFP_KERNEL);
	if (unlikely(!v_table)) {
		ret = -ENOMEM;
		dev_err(dev, "no memory for .volt_table\n");
		goto dt_parse_ldo_end;
	}

	ret = of_property_read_u32_array(np, "hisilicon,hi6421-vset-table",
						v_table, rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-vset-table property set\n");
		goto dt_parse_ldo_end;
	}
	rdesc->volt_table = v_table;

	/* parse hi6421 regulator's dt common part */
	ret = hi6421_dt_parse_common(sreg, pdev);
	if (ret) {
		dev_err(dev, "failure in hi6421_dt_parse_common\n");
		goto dt_parse_ldo_end;
	}

dt_parse_ldo_end:
	return ret;
}

static int hi6421_dt_parse_buck012(struct hi6421_regulator *sreg,
					struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc = &sreg->rdesc;
	int ret = 0;

	/* parse .n_voltages, and .uV_step */
	ret = of_property_read_u32(np, "hisilicon,hi6421-n-voltages",
				   &rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-n-voltages property set\n");
		goto dt_parse_buck012_end;
	}
	ret = of_property_read_u32(np, "hisilicon,hi6421-uv-step",
				   &rdesc->uV_step);
	if (ret) {
		dev_err(dev, "no hisilicon,hi6421-uv-step property set\n");
		goto dt_parse_buck012_end;
	}

	/* parse hi6421 regulator's dt common part */
	ret = hi6421_dt_parse_common(sreg, pdev);
	if (ret) {
		dev_err(dev, "failure in hi6421_dt_parse_common\n");
		goto dt_parse_buck012_end;
	}

dt_parse_buck012_end:
	return ret;
}

static struct regulator_ops hi6421_ldo_rops = {
	.is_enabled = hi6421_regulator_is_enabled,
	.enable = hi6421_regulator_enable,
	.disable = hi6421_regulator_disable,
	.list_voltage = regulator_list_voltage_table,
	.get_voltage = hi6421_regulator_get_voltage,
	.set_voltage = hi6421_regulator_ldo_set_voltage,
	.get_mode = hi6421_regulator_get_mode,
	.set_mode = hi6421_regulator_set_mode,
	.get_optimum_mode = hi6421_regulator_get_optimum_mode,
};

static struct regulator_ops hi6421_buck012_rops = {
	.is_enabled = hi6421_regulator_is_enabled,
	.enable = hi6421_regulator_enable,
	.disable = hi6421_regulator_disable,
	.list_voltage = regulator_list_voltage_linear,
	.get_voltage = hi6421_regulator_get_voltage,
	.set_voltage = hi6421_regulator_buck012_set_voltage,
	.get_mode = hi6421_regulator_get_mode,
	.set_mode = hi6421_regulator_set_mode,
	.get_optimum_mode = hi6421_regulator_get_optimum_mode,
};

static struct regulator_ops hi6421_buck345_rops = {
	.is_enabled = hi6421_regulator_is_enabled,
	.enable = hi6421_regulator_enable,
	.disable = hi6421_regulator_disable,
	.list_voltage = regulator_list_voltage_table,
	.get_voltage = hi6421_regulator_get_voltage,
	.set_voltage = hi6421_regulator_ldo_set_voltage,
	.get_mode = hi6421_regulator_get_mode,
	.set_mode = hi6421_regulator_set_mode,
	.get_optimum_mode = hi6421_regulator_get_optimum_mode,
};

static const struct hi6421_regulator hi6421_regulator_ldo = {
	.rdesc = {
	.ops = &hi6421_ldo_rops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		},
	.dt_parse = hi6421_dt_parse_ldo,
};

static const struct hi6421_regulator hi6421_regulator_buck012 = {
	.rdesc = {
		.ops = &hi6421_buck012_rops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		},
	.dt_parse = hi6421_dt_parse_buck012,
};

static const struct hi6421_regulator hi6421_regulator_buck345 = {
	.rdesc = {
		.ops = &hi6421_buck345_rops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		},
	.dt_parse = hi6421_dt_parse_ldo,
};

static struct of_device_id of_hi6421_regulator_match_tbl[] = {
	{
		.compatible = "hisilicon,hi6421-ldo",
		.data = &hi6421_regulator_ldo,
	},
	{
		.compatible = "hisilicon,hi6421-buck012",
		.data = &hi6421_regulator_buck012,
	},
	{
		.compatible = "hisilicon,hi6421-buck345",
		.data = &hi6421_regulator_buck345,
	},
	{ /* end */ }
};

static int hi6421_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct hi6421_regulator *sreg = NULL;
	struct regulator_init_data *initdata;
	struct regulation_constraints *c;
	struct regulator_config config = { };
	const struct of_device_id *match;
	const struct hi6421_regulator *template = NULL;
	int ret = 0;

	/* to check which type of regulator this is */
	match = of_match_device(of_hi6421_regulator_match_tbl, &pdev->dev);
	if (match)
		template = match->data;
	else
		return -EINVAL;

	initdata = of_get_regulator_init_data(dev, np);

	/* hi6421 regulator supports two modes */
	c = &initdata->constraints;
	c->valid_modes_mask = REGULATOR_MODE_NORMAL | REGULATOR_MODE_IDLE;
	c->valid_ops_mask |= (REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_DRMS);
	c->input_uV = c->min_uV;

	sreg = devm_kzalloc(dev, sizeof(*sreg), GFP_KERNEL);
	if (sreg == NULL)
		return -ENOMEM;
	memcpy(sreg, template, sizeof(*sreg));

	sreg->name = initdata->constraints.name;
	rdesc = &sreg->rdesc;
	rdesc->name = sreg->name;
	rdesc->min_uV = initdata->constraints.min_uV;
	if (of_get_property(np, "ldo-supply", NULL)) {
		rdesc->supply_name = "ldo";
	}

	/* to parse device tree data for regulator specific */
	ret = sreg->dt_parse(sreg, pdev);
	if (ret) {
		dev_err(dev, "device tree parameter parse error!\n");
		goto hi6421_probe_end;
	}

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = sreg;
	config.of_node = pdev->dev.of_node;

	/* register regulator */
	rdev = regulator_register(rdesc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "failed to register %s\n",
			rdesc->name);
		ret = PTR_ERR(rdev);
		goto hi6421_probe_end;
	}

	platform_set_drvdata(pdev, rdev);

hi6421_probe_end:
	return ret;
}

static int hi6421_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	regulator_unregister(rdev);

	return 0;
}

static struct platform_driver hi6421_regulator_driver = {
	.driver = {
		.name	= "hi6421_regulator",
		.owner  = THIS_MODULE,
		.of_match_table = of_hi6421_regulator_match_tbl,
	},
	.probe	= hi6421_regulator_probe,
	.remove	= hi6421_regulator_remove,
};
module_platform_driver(hi6421_regulator_driver);

MODULE_AUTHOR("Guodong Xu <guodong.xu@linaro.org>");
MODULE_DESCRIPTION("Hi6421 regulator driver");
MODULE_LICENSE("GPL v2");
