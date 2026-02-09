// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2025 Andreas Kemnade

/* Datasheet: https://www.fitipower.com/dl/file/flXa6hIchVeu0W3K */

#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regmap.h>

#define FP9931_REG_TMST_VALUE 0
#define FP9931_REG_VCOM_SETTING 1
#define FP9931_REG_VPOSNEG_SETTING 2
#define FP9931_REG_PWRON_DELAY 3
#define FP9931_REG_CONTROL_REG1 11

#define PGOOD_TIMEOUT_MSECS 200

struct fp9931_data {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *vin_reg;
	struct gpio_desc *pgood_gpio;
	struct gpio_desc *en_gpio;
	struct gpio_desc *en_ts_gpio;
	struct completion pgood_completion;
	int pgood_irq;
};

static const unsigned int VPOSNEG_table[] = {
	7040000,
	7040000,
	7040000,
	7040000,
	7040000,
	7040000,
	7260000,
	7490000,
	7710000,
	7930000,
	8150000,
	8380000,
	8600000,
	8820000,
	9040000,
	9270000,
	9490000,
	9710000,
	9940000,
	10160000,
	10380000,
	10600000,
	10830000,
	11050000,
	11270000,
	11490000,
	11720000,
	11940000,
	12160000,
	12380000,
	12610000,
	12830000,
	13050000,
	13280000,
	13500000,
	13720000,
	13940000,
	14170000,
	14390000,
	14610000,
	14830000,
	15060000,
};

static const struct hwmon_channel_info *fp9931_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static int setup_timings(struct fp9931_data *data)
{
	u32 tdly[4];
	u8 tdlys = 0;
	int i;
	int ret;

	ret = device_property_count_u32(data->dev, "fitipower,tdly-ms");
	if (ret == -EINVAL) /* property is optional */
		return 0;

	if (ret < 0)
		return ret;

	if (ret != ARRAY_SIZE(tdly)) {
		dev_err(data->dev, "invalid delay specification");
		return -EINVAL;
	}

	ret = device_property_read_u32_array(data->dev, "fitipower,tdly-ms",
					     tdly, ARRAY_SIZE(tdly));
	if (ret)
		return ret;

	for (i = ARRAY_SIZE(tdly) - 1; i >= 0; i--) {
		if (tdly[i] > 4 || tdly[i] == 3)
			return -EINVAL;

		if (tdly[i] == 4) /* convert from ms */
			tdly[i] = 3;

		tdlys <<= 2;
		tdlys |= tdly[i];
	}

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	ret = regmap_write(data->regmap, FP9931_REG_PWRON_DELAY, tdlys);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int fp9931_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *temp)
{
	struct fp9931_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, FP9931_REG_TMST_VALUE, &val);
	if (ret)
		return ret;

	pm_runtime_put_autosuspend(data->dev);
	*temp = (s8)val * 1000;

	return 0;
}

static umode_t fp9931_hwmon_is_visible(const void *data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	return 0444;
}

static const struct hwmon_ops fp9931_hwmon_ops = {
	.is_visible = fp9931_hwmon_is_visible,
	.read = fp9931_hwmon_read,
};

static const struct hwmon_chip_info fp9931_chip_info = {
	.ops = &fp9931_hwmon_ops,
	.info = fp9931_info,
};

static int fp9931_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct fp9931_data *data = dev_get_drvdata(dev);

	if (data->en_ts_gpio)
		gpiod_set_value_cansleep(data->en_ts_gpio, 0);

	if (data->vin_reg) {
		ret = regulator_disable(data->vin_reg);
		regcache_mark_dirty(data->regmap);
	}

	return ret;
}

static int fp9931_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct fp9931_data *data = dev_get_drvdata(dev);

	if (data->vin_reg)
		ret = regulator_enable(data->vin_reg);

	if (ret)
		return ret;

	if (data->en_ts_gpio) {
		gpiod_set_value_cansleep(data->en_ts_gpio, 1);
		/* wait for one ADC conversion to have sane temperature */
		usleep_range(10000, 15000);
	}

	ret = regcache_sync(data->regmap);

	return ret;
}

static bool fp9931_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == FP9931_REG_TMST_VALUE;
}

static const struct reg_default fp9931_reg_default = {
	.reg = FP9931_REG_VCOM_SETTING,
	.def = 0x80,
};

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 12,
	.cache_type = REGCACHE_FLAT,
	.volatile_reg = fp9931_volatile_reg,
	.reg_defaults = &fp9931_reg_default,
	.num_reg_defaults = 1,
};

static void disable_nopm(void *d)
{
	struct fp9931_data *data = d;

	fp9931_runtime_suspend(data->dev);
}

static int fp9931_v3p3_enable(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	ret = regulator_enable_regmap(rdev);
	if (ret < 0)
		pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int fp9931_v3p3_disable(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = regulator_disable_regmap(rdev);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int fp9931_v3p3_is_enabled(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	if (pm_runtime_status_suspended(data->dev))
		return 0;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return 0;

	ret = regulator_is_enabled_regmap(rdev);

	pm_runtime_put_autosuspend(data->dev);
	return ret;
}

static const struct regulator_ops fp9931_v3p3ops = {
	.list_voltage = regulator_list_voltage_linear,
	.enable = fp9931_v3p3_enable,
	.disable = fp9931_v3p3_disable,
	.is_enabled = fp9931_v3p3_is_enabled,
};

static int fp9931_check_powergood(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);

	if (pm_runtime_status_suspended(data->dev))
		return 0;

	return gpiod_get_value_cansleep(data->pgood_gpio);
}

static int fp9931_get_voltage_sel(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	ret = regulator_get_voltage_sel_regmap(rdev);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int fp9931_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	ret = regulator_set_voltage_sel_regmap(rdev, selector);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static irqreturn_t pgood_handler(int irq, void *dev_id)
{
	struct fp9931_data *data = dev_id;

	complete(&data->pgood_completion);

	return IRQ_HANDLED;
}

static int fp9931_set_enable(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);
	int ret;

	ret = pm_runtime_resume_and_get(data->dev);
	if (ret < 0)
		return ret;

	reinit_completion(&data->pgood_completion);
	gpiod_set_value_cansleep(data->en_gpio, 1);
	dev_dbg(data->dev, "turning on...");
	wait_for_completion_timeout(&data->pgood_completion,
				    msecs_to_jiffies(PGOOD_TIMEOUT_MSECS));
	dev_dbg(data->dev, "turned on");
	if (gpiod_get_value_cansleep(data->pgood_gpio) != 1) {
		pm_runtime_put_autosuspend(data->dev);
		return -ETIMEDOUT;
	}

	return 0;
}

static int fp9931_clear_enable(struct regulator_dev *rdev)
{
	struct fp9931_data *data = rdev_get_drvdata(rdev);

	gpiod_set_value_cansleep(data->en_gpio, 0);
	pm_runtime_put_autosuspend(data->dev);
	return 0;
}

static const struct regulator_ops fp9931_vcom_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.enable = fp9931_set_enable,
	.disable = fp9931_clear_enable,
	.is_enabled = fp9931_check_powergood,
	.set_voltage_sel = fp9931_set_voltage_sel,
	.get_voltage_sel = fp9931_get_voltage_sel,
};

static const struct regulator_ops fp9931_vposneg_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	/* gets enabled by enabling vcom, too */
	.is_enabled = fp9931_check_powergood,
	.set_voltage_sel = fp9931_set_voltage_sel,
	.get_voltage_sel = fp9931_get_voltage_sel,
};

static const struct regulator_desc regulators[] = {
	{
		.name = "v3p3",
		.of_match = of_match_ptr("v3p3"),
		.regulators_node = of_match_ptr("regulators"),
		.id = 0,
		.ops = &fp9931_v3p3ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.enable_reg = FP9931_REG_CONTROL_REG1,
		.enable_mask = BIT(1),
		.n_voltages = 1,
		.min_uV = 3300000
	},
	{
		.name = "vposneg",
		.of_match = of_match_ptr("vposneg"),
		.regulators_node = of_match_ptr("regulators"),
		.id = 1,
		.ops = &fp9931_vposneg_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = ARRAY_SIZE(VPOSNEG_table),
		.vsel_reg = FP9931_REG_VPOSNEG_SETTING,
		.vsel_mask = 0x3F,
		.volt_table = VPOSNEG_table,
	},
	{
		.name = "vcom",
		.of_match = of_match_ptr("vcom"),
		.regulators_node = of_match_ptr("regulators"),
		.id = 2,
		.ops = &fp9931_vcom_ops,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
		.n_voltages = 255,
		.min_uV = 0,
		.uV_step = 5000000 / 255,
		.vsel_reg = FP9931_REG_VCOM_SETTING,
		.vsel_mask = 0xFF
	},
};

static int fp9931_probe(struct i2c_client *client)
{
	struct fp9931_data *data;
	struct regulator_config config = { };
	struct regulator_dev *rdev;
	int ret = 0;
	int i;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(&client->dev, PTR_ERR(data->regmap),
				     "failed to allocate regmap!\n");

	data->vin_reg = devm_regulator_get_optional(&client->dev, "vin");
	if (IS_ERR(data->vin_reg))
		return dev_err_probe(&client->dev, PTR_ERR(data->vin_reg),
				     "failed to get vin regulator\n");

	data->pgood_gpio = devm_gpiod_get(&client->dev, "pg", GPIOD_IN);
	if (IS_ERR(data->pgood_gpio))
		return dev_err_probe(&client->dev,
				     PTR_ERR(data->pgood_gpio),
				     "failed to get power good gpio\n");

	data->pgood_irq = gpiod_to_irq(data->pgood_gpio);
	if (data->pgood_irq < 0)
		return data->pgood_irq;

	data->en_gpio = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(data->en_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(data->en_gpio),
				     "failed to get en gpio\n");

	data->en_ts_gpio = devm_gpiod_get_optional(&client->dev, "en-ts", GPIOD_OUT_LOW);
	if (IS_ERR(data->en_ts_gpio))
		return dev_err_probe(&client->dev,
				     PTR_ERR(data->en_ts_gpio),
				     "failed to get en gpio\n");

	data->dev = &client->dev;
	i2c_set_clientdata(client, data);

	init_completion(&data->pgood_completion);

	ret = devm_request_threaded_irq(&client->dev, data->pgood_irq, NULL,
					pgood_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"PGOOD", data);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "failed to request irq\n");

	if (IS_ENABLED(CONFIG_PM)) {
		devm_pm_runtime_enable(&client->dev);
		pm_runtime_set_autosuspend_delay(&client->dev, 4000);
		pm_runtime_use_autosuspend(&client->dev);
	} else {
		ret = fp9931_runtime_resume(&client->dev);
		if (ret < 0)
			return ret;

		devm_add_action_or_reset(&client->dev, disable_nopm, data);
	}

	ret = setup_timings(data);
	if (ret)
		return dev_err_probe(&client->dev, ret, "failed to setup timings\n");

	config.driver_data = data;
	config.dev = &client->dev;
	config.regmap = data->regmap;

	for (i = 0; i < ARRAY_SIZE(regulators); i++) {
		rdev = devm_regulator_register(&client->dev, &regulators[i],
					       &config);
		if (IS_ERR(rdev))
			return dev_err_probe(&client->dev, PTR_ERR(rdev),
					     "failed to register %s regulator\n",
					     regulators[i].name);
	}

	if (IS_REACHABLE(CONFIG_HWMON)) {
		struct device *hwmon_dev;

		hwmon_dev = devm_hwmon_device_register_with_info(&client->dev, "fp9931", data,
								 &fp9931_chip_info, NULL);
		if (IS_ERR(hwmon_dev))
			dev_notice(&client->dev, "failed to register hwmon\n");
	}

	return 0;
}

static const struct dev_pm_ops fp9931_pm_ops = {
	SET_RUNTIME_PM_OPS(fp9931_runtime_suspend, fp9931_runtime_resume, NULL)
};

static const struct of_device_id fp9931_dt_ids[] = {
	{
		.compatible = "fitipower,fp9931",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, fp9931_dt_ids);

static struct i2c_driver fp9931_i2c_driver = {
	.driver = {
		   .name = "fp9931",
		   .of_match_table = fp9931_dt_ids,
		   .pm = &fp9931_pm_ops,
	},
	.probe = fp9931_probe,
};

module_i2c_driver(fp9931_i2c_driver);

/* Module information */
MODULE_DESCRIPTION("FP9931 regulator driver");
MODULE_LICENSE("GPL");

