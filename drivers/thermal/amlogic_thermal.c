// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Thermal Sensor Driver
 *
 * Copyright (C) 2017 Huan Biao <huan.biao@amlogic.com>
 * Copyright (C) 2019 Guillaume La Roque <glaroque@baylibre.com>
 *
 * Register value to celsius temperature formulas:
 *	Read_Val	    m * U
 * U = ---------, Uptat = ---------
 *	2^16		  1 + n * U
 *
 * Temperature = A * ( Uptat + u_efuse / 2^16 )- B
 *
 *  A B m n : calibration parameters
 *  u_efuse : fused calibration value, it's a signed 16 bits value
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#include "thermal_hwmon.h"

#define TSENSOR_CFG_REG1			0x4
	#define TSENSOR_CFG_REG1_RSET_VBG	BIT(12)
	#define TSENSOR_CFG_REG1_RSET_ADC	BIT(11)
	#define TSENSOR_CFG_REG1_VCM_EN		BIT(10)
	#define TSENSOR_CFG_REG1_VBG_EN		BIT(9)
	#define TSENSOR_CFG_REG1_OUT_CTL	BIT(6)
	#define TSENSOR_CFG_REG1_FILTER_EN	BIT(5)
	#define TSENSOR_CFG_REG1_DEM_EN		BIT(3)
	#define TSENSOR_CFG_REG1_CH_SEL		GENMASK(1, 0)
	#define TSENSOR_CFG_REG1_ENABLE		\
		(TSENSOR_CFG_REG1_FILTER_EN |	\
		 TSENSOR_CFG_REG1_VCM_EN |	\
		 TSENSOR_CFG_REG1_VBG_EN |	\
		 TSENSOR_CFG_REG1_DEM_EN |	\
		 TSENSOR_CFG_REG1_CH_SEL)

#define TSENSOR_STAT0			0x40

#define TSENSOR_STAT9			0x64

#define TSENSOR_READ_TEMP_MASK		GENMASK(15, 0)
#define TSENSOR_TEMP_MASK		GENMASK(11, 0)

#define TSENSOR_TRIM_SIGN_MASK		BIT(15)
#define TSENSOR_TRIM_TEMP_MASK		GENMASK(14, 0)
#define TSENSOR_TRIM_VERSION_MASK	GENMASK(31, 24)

#define TSENSOR_TRIM_VERSION(_version)	\
	FIELD_GET(TSENSOR_TRIM_VERSION_MASK, _version)

#define TSENSOR_TRIM_CALIB_VALID_MASK	(GENMASK(3, 2) | BIT(7))

#define TSENSOR_CALIB_OFFSET	1
#define TSENSOR_CALIB_SHIFT	4

/**
 * struct amlogic_thermal_soc_calib_data
 * @A: calibration parameters
 * @B: calibration parameters
 * @m: calibration parameters
 * @n: calibration parameters
 *
 * This structure is required for configuration of amlogic thermal driver.
 */
struct amlogic_thermal_soc_calib_data {
	int A;
	int B;
	int m;
	int n;
};

/**
 * struct amlogic_thermal_data
 * @u_efuse_off: register offset to read fused calibration value
 * @calibration_parameters: calibration parameters structure pointer
 * @regmap_config: regmap config for the device
 * This structure is required for configuration of amlogic thermal driver.
 */
struct amlogic_thermal_data {
	int u_efuse_off;
	const struct amlogic_thermal_soc_calib_data *calibration_parameters;
	const struct regmap_config *regmap_config;
};

struct amlogic_thermal {
	struct platform_device *pdev;
	const struct amlogic_thermal_data *data;
	struct regmap *regmap;
	struct regmap *sec_ao_map;
	struct clk *clk;
	struct thermal_zone_device *tzd;
	u32 trim_info;
};

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree milliCelsius.
 */
static int amlogic_thermal_code_to_millicelsius(struct amlogic_thermal *pdata,
						int temp_code)
{
	const struct amlogic_thermal_soc_calib_data *param =
					pdata->data->calibration_parameters;
	int temp;
	s64 factor, Uptat, uefuse;

	uefuse = pdata->trim_info & TSENSOR_TRIM_SIGN_MASK ?
			     ~(pdata->trim_info & TSENSOR_TRIM_TEMP_MASK) + 1 :
			     (pdata->trim_info & TSENSOR_TRIM_TEMP_MASK);

	factor = param->n * temp_code;
	factor = div_s64(factor, 100);

	Uptat = temp_code * param->m;
	Uptat = div_s64(Uptat, 100);
	Uptat = Uptat * BIT(16);
	Uptat = div_s64(Uptat, BIT(16) + factor);

	temp = (Uptat + uefuse) * param->A;
	temp = div_s64(temp, BIT(16));
	temp = (temp - param->B) * 100;

	return temp;
}

static int amlogic_thermal_initialize(struct amlogic_thermal *pdata)
{
	int ret = 0;
	int ver;

	regmap_read(pdata->sec_ao_map, pdata->data->u_efuse_off,
		    &pdata->trim_info);

	ver = TSENSOR_TRIM_VERSION(pdata->trim_info);

	if ((ver & TSENSOR_TRIM_CALIB_VALID_MASK) == 0) {
		ret = -EINVAL;
		dev_err(&pdata->pdev->dev,
			"tsensor thermal calibration not supported: 0x%x!\n",
			ver);
	}

	return ret;
}

static int amlogic_thermal_enable(struct amlogic_thermal *data)
{
	int ret;

	ret = clk_prepare_enable(data->clk);
	if (ret)
		return ret;

	regmap_update_bits(data->regmap, TSENSOR_CFG_REG1,
			   TSENSOR_CFG_REG1_ENABLE, TSENSOR_CFG_REG1_ENABLE);

	return 0;
}

static int amlogic_thermal_disable(struct amlogic_thermal *data)
{
	regmap_update_bits(data->regmap, TSENSOR_CFG_REG1,
			   TSENSOR_CFG_REG1_ENABLE, 0);
	clk_disable_unprepare(data->clk);

	return 0;
}

static int amlogic_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	unsigned int tval;
	struct amlogic_thermal *pdata = tz->devdata;

	if (!pdata)
		return -EINVAL;

	regmap_read(pdata->regmap, TSENSOR_STAT0, &tval);
	*temp =
	   amlogic_thermal_code_to_millicelsius(pdata,
						tval & TSENSOR_READ_TEMP_MASK);

	return 0;
}

static const struct thermal_zone_device_ops amlogic_thermal_ops = {
	.get_temp	= amlogic_thermal_get_temp,
};

static const struct regmap_config amlogic_thermal_regmap_config_g12a = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = TSENSOR_STAT9,
};

static const struct amlogic_thermal_soc_calib_data amlogic_thermal_g12a = {
	.A = 9411,
	.B = 3159,
	.m = 424,
	.n = 324,
};

static const struct amlogic_thermal_data amlogic_thermal_g12a_cpu_param = {
	.u_efuse_off = 0x128,
	.calibration_parameters = &amlogic_thermal_g12a,
	.regmap_config = &amlogic_thermal_regmap_config_g12a,
};

static const struct amlogic_thermal_data amlogic_thermal_g12a_ddr_param = {
	.u_efuse_off = 0xf0,
	.calibration_parameters = &amlogic_thermal_g12a,
	.regmap_config = &amlogic_thermal_regmap_config_g12a,
};

static const struct of_device_id of_amlogic_thermal_match[] = {
	{
		.compatible = "amlogic,g12a-ddr-thermal",
		.data = &amlogic_thermal_g12a_ddr_param,
	},
	{
		.compatible = "amlogic,g12a-cpu-thermal",
		.data = &amlogic_thermal_g12a_cpu_param,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_amlogic_thermal_match);

static int amlogic_thermal_probe(struct platform_device *pdev)
{
	struct amlogic_thermal *pdata;
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->data = of_device_get_match_data(dev);
	pdata->pdev = pdev;
	platform_set_drvdata(pdev, pdata);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pdata->regmap = devm_regmap_init_mmio(dev, base,
					      pdata->data->regmap_config);
	if (IS_ERR(pdata->regmap))
		return PTR_ERR(pdata->regmap);

	pdata->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(pdata->clk)) {
		if (PTR_ERR(pdata->clk) != -EPROBE_DEFER)
			dev_err(dev, "failed to get clock\n");
		return PTR_ERR(pdata->clk);
	}

	pdata->sec_ao_map = syscon_regmap_lookup_by_phandle
		(pdev->dev.of_node, "amlogic,ao-secure");
	if (IS_ERR(pdata->sec_ao_map)) {
		dev_err(dev, "syscon regmap lookup failed.\n");
		return PTR_ERR(pdata->sec_ao_map);
	}

	pdata->tzd = devm_thermal_of_zone_register(&pdev->dev,
						   0,
						   pdata,
						   &amlogic_thermal_ops);
	if (IS_ERR(pdata->tzd)) {
		ret = PTR_ERR(pdata->tzd);
		dev_err(dev, "Failed to register tsensor: %d\n", ret);
		return ret;
	}

	if (devm_thermal_add_hwmon_sysfs(pdata->tzd))
		dev_warn(&pdev->dev, "Failed to add hwmon sysfs attributes\n");

	ret = amlogic_thermal_initialize(pdata);
	if (ret)
		return ret;

	ret = amlogic_thermal_enable(pdata);

	return ret;
}

static int amlogic_thermal_remove(struct platform_device *pdev)
{
	struct amlogic_thermal *data = platform_get_drvdata(pdev);

	return amlogic_thermal_disable(data);
}

static int __maybe_unused amlogic_thermal_suspend(struct device *dev)
{
	struct amlogic_thermal *data = dev_get_drvdata(dev);

	return amlogic_thermal_disable(data);
}

static int __maybe_unused amlogic_thermal_resume(struct device *dev)
{
	struct amlogic_thermal *data = dev_get_drvdata(dev);

	return amlogic_thermal_enable(data);
}

static SIMPLE_DEV_PM_OPS(amlogic_thermal_pm_ops,
			 amlogic_thermal_suspend, amlogic_thermal_resume);

static struct platform_driver amlogic_thermal_driver = {
	.driver = {
		.name		= "amlogic_thermal",
		.pm		= &amlogic_thermal_pm_ops,
		.of_match_table = of_amlogic_thermal_match,
	},
	.probe	= amlogic_thermal_probe,
	.remove	= amlogic_thermal_remove,
};

module_platform_driver(amlogic_thermal_driver);

MODULE_AUTHOR("Guillaume La Roque <glaroque@baylibre.com>");
MODULE_DESCRIPTION("Amlogic thermal driver");
MODULE_LICENSE("GPL v2");
