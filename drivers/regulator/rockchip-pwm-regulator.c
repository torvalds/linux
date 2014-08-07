/*
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/bug.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regulator/rockchip-pwm-regulator.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/pwm.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>


#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

const static int pwm_voltage_map[] = {
	925000 , 950000, 975000, 1000000, 1025000, 1050000,
	1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000,
	1325000, 1350000, 1375000, 1400000
};

static int pwm_set_rate(struct pwm_platform_data *pdata, u32 rate)
{
	int duty_cycle;
	DBG("%s:id=%d,rate=%d clkrate =%d\n", __func__, pdata->pwm_id, rate, pdata->period);

	duty_cycle = (rate * (pdata->period) / 100) ;

	pwm_config(pdata->pwm, duty_cycle, pdata->period);

	pwm_enable(pdata->pwm);

	return 0;
}

static int pwm_regulator_list_voltage(struct regulator_dev *dev, unsigned int index)
{
	struct pwm_platform_data *pdata = rdev_get_drvdata(dev);

	DBG("%s:line=%d,pdata=%p,pwm_id=%d\n", __func__,
		__LINE__, pdata, pdata->pwm_id);
	if (index < (pdata->pwm_vol_map_count + 1))
	return pdata->pwm_voltage_map[index];
	else
		return -1;
}

static int pwm_regulator_is_enabled(struct regulator_dev *dev)
{
	DBG("Enter %s\n", __func__);
	return 0;
}

static int pwm_regulator_enable(struct regulator_dev *dev)
{
	DBG("Enter %s\n", __func__);
	return 0;
}

static int pwm_regulator_disable(struct regulator_dev *dev)
{
	DBG("Enter %s\n", __func__);
	return 0;
}

static int pwm_regulator_get_voltage(struct regulator_dev *dev)
{
	u32 vol;
	struct pwm_platform_data *pdata = rdev_get_drvdata(dev);

	DBG("%s:line=%d,pdata=%p,pwm_id=%d\n", __func__, __LINE__,
		pdata, pdata->pwm_id);
	vol = pdata->pwm_voltage;

	return vol;
}

static int pwm_regulator_set_voltage(struct regulator_dev *dev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct pwm_platform_data *pdata = rdev_get_drvdata(dev);
	const int *voltage_map = pdata->pwm_voltage_map;
	int max = pdata->max_uV;
	int coefficient = pdata->coefficient;
	u32 size = pdata->pwm_vol_map_count;
	u32 i, vol, pwm_value;

	DBG("%s:  min_uV = %d, max_uV = %d\n", __func__, min_uV, max_uV);
	DBG("%s:line=%d,pdata=%p,pwm_id=%d\n", __func__, __LINE__,
		pdata, pdata->pwm_id);
	mutex_lock(&pdata->mutex_pwm);

	if (min_uV < voltage_map[0] || max_uV > voltage_map[size-1]) {
		printk("%s: voltage_map voltage is out of table\n", __func__);
		mutex_unlock(&pdata->mutex_pwm);
		return -EINVAL;
	}

	for (i = 0; i < size; i++) {
		if (voltage_map[i] >= min_uV)
			break;
	}
	vol =  voltage_map[i];
	pdata->pwm_voltage = vol;

	/* VDD12 = 1.40 - 0.455*D , DÎªPWMÕ¼¿Õ±È*/
	pwm_value = (max-vol) / coefficient / 10;
	/*pwm_value %, coefficient *1000*/

	if (pwm_set_rate(pdata, pwm_value) != 0) {
		printk("%s:fail to set pwm rate,pwm_value=%d\n", __func__, pwm_value);
		mutex_unlock(&pdata->mutex_pwm);
		return -1;

	}
	*selector = i;

	mutex_unlock(&pdata->mutex_pwm);

	DBG("%s:ok,vol=%d,pwm_value=%d %d\n", __func__, vol,
		pwm_value, pdata->pwm_voltage);

	return 0;

}

static struct regulator_ops pwm_voltage_ops = {
	.list_voltage	= pwm_regulator_list_voltage,
	.set_voltage	= pwm_regulator_set_voltage,
	.get_voltage	= pwm_regulator_get_voltage,
	.enable		= pwm_regulator_enable,
	.disable	= pwm_regulator_disable,
	.is_enabled	= pwm_regulator_is_enabled,
};
static struct regulator_desc regulators[] = {

	{
		.name = "PWM_DCDC1",
		.id = 0,
		.ops = &pwm_voltage_ops,
		.n_voltages =  ARRAY_SIZE(pwm_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

	{
		.name = "PWM_DCDC2",
		.id = 1,
		.ops = &pwm_voltage_ops,
		.n_voltages =  ARRAY_SIZE(pwm_voltage_map),
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

};

#ifdef CONFIG_OF
static struct of_device_id pwm_of_match[] = {
	{ .compatible = "rockchip_pwm_regulator"},
	{ },
};
MODULE_DEVICE_TABLE(of, pwm_of_match);

static struct of_regulator_match pwm_matches[] = {
	{ .name = "pwm_dcdc1",	.driver_data = (void *)0  },
	{ .name = "pwm_dcdc2",	.driver_data = (void *)1  },
};

static struct pwm_regulator_board *pwm_regulator_parse_dt(
		struct platform_device *pdev,
		struct of_regulator_match **pwm_reg_matches)
{
	struct pwm_regulator_board *pwm_plat_data;
	struct device_node *np, *regulators;
	struct of_regulator_match *matches;
	int idx = 0, ret, count;
	struct property *prop;
	int length;
	const __be32 *init_vol, *max_vol, *min_vol, *suspend_vol, *coefficient, *id;

	DBG("%s,line=%d\n", __func__, __LINE__);

	pwm_plat_data = devm_kzalloc(&pdev->dev, sizeof(*pwm_plat_data),
					GFP_KERNEL);

	if (!pwm_plat_data) {
		dev_err(&pdev->dev, "Failure to alloc pdata for regulators.\n");
		return NULL;
	}

	np = of_node_get(pdev->dev.of_node);
	regulators = of_find_node_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&pdev->dev, "regulator node not found\n");
		return NULL;
	}
	count = ARRAY_SIZE(pwm_matches);
	matches = pwm_matches;
	ret = of_regulator_match(&pdev->dev, regulators, matches, count);
	of_node_put(regulators);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error parsing regulator init data: %d\n",
			ret);
		return NULL;
	}

	pwm_plat_data->num_regulators = count;
	*pwm_reg_matches = matches;

	for (idx = 0; idx < count; idx++) {
		if (!matches[idx].init_data || !matches[idx].of_node)
			continue;
		pwm_plat_data->pwm_init_data[idx] = matches[idx].init_data;
		pwm_plat_data->of_node[idx] = matches[idx].of_node;
	}

	init_vol = of_get_property(np, "rockchip,pwm_voltage", NULL);
	if (init_vol)
		pwm_plat_data->pwm_init_vol = be32_to_cpu(*init_vol);

	max_vol = of_get_property(np, "rockchip,pwm_max_voltage", NULL);
	if (max_vol)
		pwm_plat_data->pwm_max_vol = be32_to_cpu(*max_vol);

	min_vol = of_get_property(np, "rockchip,pwm_min_voltage", NULL);
	if (min_vol)
		pwm_plat_data->pwm_min_vol = be32_to_cpu(*min_vol);

	suspend_vol = of_get_property(np, "rockchip,pwm_suspend_voltage", NULL);
	if (suspend_vol)
		pwm_plat_data->pwm_suspend_vol = be32_to_cpu(*suspend_vol);

	coefficient  = of_get_property(np, "rockchip,pwm_coefficient", NULL);
	if (coefficient)
		pwm_plat_data->pwm_coefficient = be32_to_cpu(*coefficient);

	id  = of_get_property(np, "rockchip,pwm_id", NULL);
	if (id)
		pwm_plat_data->pwm_id = be32_to_cpu(*id);

	prop = of_find_property(np, "rockchip,pwm_voltage_map", &length);
	if (!prop)
		return NULL;
	pwm_plat_data->pwm_vol_map_count = length / sizeof(u32);
	if (pwm_plat_data->pwm_vol_map_count > 0) {
		size_t size = sizeof(*pwm_plat_data->pwm_voltage_map) * pwm_plat_data->pwm_vol_map_count;

	pwm_plat_data->pwm_voltage_map = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!pwm_plat_data->pwm_voltage_map)
		return NULL;
	ret = of_property_read_u32_array(np, "rockchip,pwm_voltage_map",
		pwm_plat_data->pwm_voltage_map, pwm_plat_data->pwm_vol_map_count);
	if (ret < 0)
		printk("pwm voltage map not specified\n");
	}
	return pwm_plat_data;
}
#else
static inline struct pwm_regulator_board *pwm_regulator_parse_dt(
			struct platform_device *pdev,
			struct of_regulator_match **pwm_reg_matches)
{
	return NULL;
}
#endif


static int __init pwm_regulator_probe(struct platform_device *pdev)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	struct pwm_regulator_board *pwm_pdev;
	struct of_regulator_match *pwm_reg_matches = NULL;
	struct regulator_init_data *reg_data;
	struct regulator_config config = { };
	const char *rail_name = NULL;
	struct regulator_dev *pwm_rdev;
	int ret, i = 0;
	struct regulator *dc;

	pwm_pdev = devm_kzalloc(&pdev->dev, sizeof(*pwm_pdev),
					GFP_KERNEL);
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata),
					GFP_KERNEL);

	if (pdev->dev.of_node)
		pwm_pdev = pwm_regulator_parse_dt(pdev, &pwm_reg_matches);

	if (!pwm_pdev) {
		dev_err(&pdev->dev, "Platform data not found\n");
		return -EINVAL;
	}

	if (!pwm_pdev->pwm_init_vol)
		pdata->pwm_voltage = 1100000;	/* default 1.1v*/
	else
		pdata->pwm_voltage = pwm_pdev->pwm_init_vol;

	if (!pwm_pdev->pwm_max_vol)
		pdata->max_uV = 1400000;
	else
		pdata->max_uV = pwm_pdev->pwm_max_vol;

	if (!pwm_pdev->pwm_min_vol)
		pdata->min_uV = 1000000;
	else
		pdata->min_uV = pwm_pdev->pwm_min_vol;

	if (pwm_pdev->pwm_suspend_vol < pwm_pdev->pwm_min_vol)
		pdata->suspend_voltage = pwm_pdev->pwm_min_vol;
	else if (pwm_pdev->pwm_suspend_vol > pwm_pdev->pwm_max_vol)
		pdata->suspend_voltage = pwm_pdev->pwm_max_vol;
	else
		pdata->suspend_voltage = pwm_pdev->pwm_suspend_vol;

	pdata->pwm_voltage_map = pwm_pdev->pwm_voltage_map;
	pdata->pwm_id = pwm_pdev->pwm_id;
	pdata->coefficient = pwm_pdev->pwm_coefficient;
	pdata->pwm_vol_map_count = pwm_pdev->pwm_vol_map_count;

	pdata->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->pwm)) {
		dev_err(&pdev->dev, "unable to request PWM, trying legacy API\n");

		pdata->pwm = pwm_request(pdata->pwm_id, "pwm-regulator");
		if (IS_ERR(pdata->pwm)) {
			dev_err(&pdev->dev, "unable to request legacy PWM\n");
			ret = PTR_ERR(pdata->pwm);
			goto err;
		}
	}
	if (pdata->pwm_period_ns > 0)
		pwm_set_period(pdata->pwm, pdata->pwm_period_ns);

	pdata->period = pwm_get_period(pdata->pwm);

	mutex_init(&pdata->mutex_pwm);

	if (pwm_pdev) {
		pdata->num_regulators = pwm_pdev->num_regulators;
		pdata->rdev = kcalloc(pdata->num_regulators, sizeof(struct regulator_dev *), GFP_KERNEL);
		if (!pdata->rdev) {
			return -ENOMEM;
		}
		/* Instantiate the regulators */
		for (i = 0; i < pdata->num_regulators; i++) {
		reg_data = pwm_pdev->pwm_init_data[i];
		if (!reg_data)
			continue;
		config.dev = &pdev->dev;
		config.driver_data = pdata;
		if (&pdev->dev.of_node)
			config.of_node = pwm_pdev->of_node[i];
		if (reg_data && reg_data->constraints.name)
				rail_name = reg_data->constraints.name;
			else
				rail_name = regulators[i].name;
			reg_data->supply_regulator = rail_name;

		config.init_data = reg_data;

		pwm_rdev = regulator_register(&regulators[i], &config);
		if (IS_ERR(pwm_rdev)) {
			printk("failed to register %d regulator\n", i);
		goto err;
		}
		pdata->rdev[i] = pwm_rdev;

		/*********set pwm vol by defult***********/
		dc = regulator_get(NULL, rail_name);
		regulator_set_voltage(dc, pdata->pwm_voltage, pdata->pwm_voltage);
		regulator_put(dc);
		/**************************************/
		}
	}
	return 0;
err:
	printk("%s:error\n", __func__);
	return ret;

}

void pwm_suspend_voltage(void)
{

}

void pwm_resume_voltage(void)
{

}

static int pwm_regulator_remove(struct platform_device *pdev)
{
	struct pwm_platform_data *pdata = pdev->dev.platform_data;
	int i;

	for (i = 0; i < pdata->num_regulators; i++)
		if (pdata->rdev[i])
			regulator_unregister(pdata->rdev[i]);
	kfree(pdata->rdev);
	return 0;
}

static struct platform_driver pwm_regulator_driver = {
	.driver = {
		.name = "pwm-voltage-regulator",
		.of_match_table = of_match_ptr(pwm_of_match),
	},
	.remove = pwm_regulator_remove,
};


static int __init pwm_regulator_module_init(void)
{
	return platform_driver_probe(&pwm_regulator_driver, pwm_regulator_probe);
}

static void __exit pwm_regulator_module_exit(void)
{
	platform_driver_unregister(&pwm_regulator_driver);
}

/*fs_initcall(pwm_regulator_module_init);*/
module_init(pwm_regulator_module_init);
module_exit(pwm_regulator_module_exit);
MODULE_LICENSE("GPL");
