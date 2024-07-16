// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright 2020 Google LLC.

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

struct cros_ec_regulator_data {
	struct regulator_desc desc;
	struct regulator_dev *dev;
	struct cros_ec_device *ec_dev;

	u32 index;

	u16 *voltages_mV;
	u16 num_voltages;
};

static int cros_ec_regulator_enable(struct regulator_dev *dev)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);
	struct ec_params_regulator_enable cmd = {
		.index = data->index,
		.enable = 1,
	};

	return cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_ENABLE, &cmd,
			   sizeof(cmd), NULL, 0);
}

static int cros_ec_regulator_disable(struct regulator_dev *dev)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);
	struct ec_params_regulator_enable cmd = {
		.index = data->index,
		.enable = 0,
	};

	return cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_ENABLE, &cmd,
			   sizeof(cmd), NULL, 0);
}

static int cros_ec_regulator_is_enabled(struct regulator_dev *dev)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);
	struct ec_params_regulator_is_enabled cmd = {
		.index = data->index,
	};
	struct ec_response_regulator_is_enabled resp;
	int ret;

	ret = cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_IS_ENABLED, &cmd,
			  sizeof(cmd), &resp, sizeof(resp));
	if (ret < 0)
		return ret;
	return resp.enabled;
}

static int cros_ec_regulator_list_voltage(struct regulator_dev *dev,
					  unsigned int selector)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);

	if (selector >= data->num_voltages)
		return -EINVAL;

	return data->voltages_mV[selector] * 1000;
}

static int cros_ec_regulator_get_voltage(struct regulator_dev *dev)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);
	struct ec_params_regulator_get_voltage cmd = {
		.index = data->index,
	};
	struct ec_response_regulator_get_voltage resp;
	int ret;

	ret = cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_GET_VOLTAGE, &cmd,
			  sizeof(cmd), &resp, sizeof(resp));
	if (ret < 0)
		return ret;
	return resp.voltage_mv * 1000;
}

static int cros_ec_regulator_set_voltage(struct regulator_dev *dev, int min_uV,
					 int max_uV, unsigned int *selector)
{
	struct cros_ec_regulator_data *data = rdev_get_drvdata(dev);
	int min_mV = DIV_ROUND_UP(min_uV, 1000);
	int max_mV = max_uV / 1000;
	struct ec_params_regulator_set_voltage cmd = {
		.index = data->index,
		.min_mv = min_mV,
		.max_mv = max_mV,
	};

	/*
	 * This can happen when the given range [min_uV, max_uV] doesn't
	 * contain any voltage that can be represented exactly in mV.
	 */
	if (min_mV > max_mV)
		return -EINVAL;

	return cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_SET_VOLTAGE, &cmd,
			   sizeof(cmd), NULL, 0);
}

static const struct regulator_ops cros_ec_regulator_voltage_ops = {
	.enable = cros_ec_regulator_enable,
	.disable = cros_ec_regulator_disable,
	.is_enabled = cros_ec_regulator_is_enabled,
	.list_voltage = cros_ec_regulator_list_voltage,
	.get_voltage = cros_ec_regulator_get_voltage,
	.set_voltage = cros_ec_regulator_set_voltage,
};

static int cros_ec_regulator_init_info(struct device *dev,
				       struct cros_ec_regulator_data *data)
{
	struct ec_params_regulator_get_info cmd = {
		.index = data->index,
	};
	struct ec_response_regulator_get_info resp;
	int ret;

	ret = cros_ec_cmd(data->ec_dev, 0, EC_CMD_REGULATOR_GET_INFO, &cmd,
			  sizeof(cmd), &resp, sizeof(resp));
	if (ret < 0)
		return ret;

	data->num_voltages =
		min_t(u16, ARRAY_SIZE(resp.voltages_mv), resp.num_voltages);
	data->voltages_mV =
		devm_kmemdup(dev, resp.voltages_mv,
			     sizeof(u16) * data->num_voltages, GFP_KERNEL);
	if (!data->voltages_mV)
		return -ENOMEM;

	data->desc.n_voltages = data->num_voltages;

	/* Make sure the returned name is always a valid string */
	resp.name[ARRAY_SIZE(resp.name) - 1] = '\0';
	data->desc.name = devm_kstrdup(dev, resp.name, GFP_KERNEL);
	if (!data->desc.name)
		return -ENOMEM;

	return 0;
}

static int cros_ec_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct cros_ec_regulator_data *drvdata;
	struct regulator_init_data *init_data;
	struct regulator_config cfg = {};
	struct regulator_desc *desc;
	int ret;

	drvdata = devm_kzalloc(
		&pdev->dev, sizeof(struct cros_ec_regulator_data), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->ec_dev = dev_get_drvdata(dev->parent);
	desc = &drvdata->desc;

	init_data = of_get_regulator_init_data(dev, np, desc);
	if (!init_data)
		return -EINVAL;

	ret = of_property_read_u32(np, "reg", &drvdata->index);
	if (ret < 0)
		return ret;

	desc->owner = THIS_MODULE;
	desc->type = REGULATOR_VOLTAGE;
	desc->ops = &cros_ec_regulator_voltage_ops;

	ret = cros_ec_regulator_init_info(dev, drvdata);
	if (ret < 0)
		return ret;

	cfg.dev = &pdev->dev;
	cfg.init_data = init_data;
	cfg.driver_data = drvdata;
	cfg.of_node = np;

	drvdata->dev = devm_regulator_register(dev, &drvdata->desc, &cfg);
	if (IS_ERR(drvdata->dev)) {
		ret = PTR_ERR(drvdata->dev);
		dev_err(&pdev->dev, "Failed to register regulator: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);

	return 0;
}

static const struct of_device_id regulator_cros_ec_of_match[] = {
	{ .compatible = "google,cros-ec-regulator", },
	{}
};
MODULE_DEVICE_TABLE(of, regulator_cros_ec_of_match);

static struct platform_driver cros_ec_regulator_driver = {
	.probe		= cros_ec_regulator_probe,
	.driver		= {
		.name		= "cros-ec-regulator",
		.of_match_table = regulator_cros_ec_of_match,
	},
};

module_platform_driver(cros_ec_regulator_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC controlled regulator");
MODULE_AUTHOR("Pi-Hsun Shih <pihsun@chromium.org>");
