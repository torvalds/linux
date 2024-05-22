// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>

#define MAX31760_CTRL_REG1		0x00
#define MAX31760_CTRL_REG2		0x01
#define MAX31760_CTRL_REG3		0x02
#define MAX31760_DUTY_CYCLE_CTRL_REG	0x50
#define MAX31760_TC1H_REG		0x52
#define MAX31760_TC1L_REG		0x53
#define MAX31760_TC2H_REG		0x54
#define MAX31760_TC2L_REG		0x55

#define VDD_MAX_UV	3300000
#define VDD_MIN_UV	3296000
#define VDD_LOAD_UA	300000
#define VCCA_MAX_UV	1800000
#define VCCA_MIN_UV	1800000
#define VCCA_LOAD_UA	600000

#define FAN_SPEED_LEVEL0	0
#define FAN_SPEED_MAX		100
#define SPEED_CAL_CONST		(60 * 100000)
#define MSB_CONVERT_DEC		(256)
#define PWM_FACTOR		39

struct max31760_data {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct thermal_cooling_device *cdev;
	struct mutex update_lock;
	struct regulator *vdd_reg;
	struct regulator *vcca_reg;
	u32 fan_num;
	u32 pwr_en_gpio;
	unsigned int cur_state;
	atomic_t in_suspend;
};

static int max31760_read_byte(struct max31760_data *pdata, u8 reg, u8 *val)
{
	int ret;

	struct i2c_client *client = pdata->i2c_client;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(pdata->dev, "failed read reg 0x%02x failure, ret:%d\n", reg, ret);

	*val = (u8)ret;

	dev_dbg(pdata->dev, "success read reg 0x%x=0x%x\n", reg, *val);

	return ret;
}

static int max31760_write_byte(struct max31760_data *pdata, u8 reg, u8 val)
{
	int ret = 0;
	struct i2c_client *client = pdata->i2c_client;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(pdata->dev, "failed write reg %#x failure, ret:%d\n", reg, ret);
		return ret;
	}

	dev_dbg(pdata->dev, "successfully write reg %#x=%#x\n", reg, val);
	return 0;
}

static void max31760_enable_gpio(struct max31760_data *pdata, int on)
{
	gpio_direction_output(pdata->pwr_en_gpio, on);
	dev_dbg(pdata->dev, "max31760 gpio:%d set to %d\n", pdata->pwr_en_gpio, on);
	usleep_range(20000, 20100);
}

static void max31760_speed_control(struct max31760_data *pdata, unsigned long level)
{
	unsigned long data;

	data = level * 255 / 100;
	max31760_write_byte(pdata, MAX31760_DUTY_CYCLE_CTRL_REG, data);
}

static void max31760_set_cur_state_common(struct max31760_data *pdata,
				unsigned long state)
{
	if (!atomic_read(&pdata->in_suspend))
		max31760_speed_control(pdata, state);
	pdata->cur_state = state;
}

static int max31760_get_max_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	*state = FAN_SPEED_MAX;
	return 0;
}

static int max31760_get_cur_state(struct thermal_cooling_device *cdev,
				unsigned long *state)
{
	struct max31760_data *data = cdev->devdata;

	mutex_lock(&data->update_lock);
	*state = data->cur_state;
	mutex_unlock(&data->update_lock);

	return 0;
}

static int max31760_set_cur_state(struct thermal_cooling_device *cdev,
				unsigned long state)
{
	struct max31760_data *data = cdev->devdata;

	if (state > FAN_SPEED_MAX) {
		dev_err(data->dev, "fail to set current state\n");
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	max31760_set_cur_state_common(data, state);
	mutex_unlock(&data->update_lock);

	return 0;
}

static struct thermal_cooling_device_ops max31760_cooling_ops = {
	.get_max_state = max31760_get_max_state,
	.get_cur_state = max31760_get_cur_state,
	.set_cur_state = max31760_set_cur_state,
};

static ssize_t speed_control_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct max31760_data *data = dev_get_drvdata(dev);
	int ret;

	if (!data) {
		pr_err("invalid driver pointer\n");
		return -ENODEV;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", data->cur_state);

	return ret;
}

static ssize_t speed_control_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct max31760_data *data = dev_get_drvdata(dev);
	unsigned long value;

	if (kstrtoul(buf, 0, &value))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	max31760_set_cur_state_common(data, value);
	mutex_unlock(&data->update_lock);

	return count;
}

static int max31760_read_speed(struct max31760_data *data,
				u8 index, u32 *speed)
{
	u8 tch = 0, tcl = 0;
	u8 value = 0;
	int ret = 0;

	if (index == 1) {
		ret = max31760_read_byte(data, MAX31760_TC1H_REG, &value);
		if (ret < 0)
			return ret;
		tch = value;

		ret = max31760_read_byte(data, MAX31760_TC1L_REG, &value);
		if (ret < 0)
			return ret;
		tcl = value;
	} else if (index == 2) {
		ret = max31760_read_byte(data, MAX31760_TC2H_REG, &value);
		if (ret < 0)
			return ret;
		tch = value;

		ret = max31760_read_byte(data, MAX31760_TC2L_REG, &value);
		if (ret < 0)
			return ret;
		tcl = value;
	}

	*speed = SPEED_CAL_CONST / (tch * MSB_CONVERT_DEC + tcl) / 2;

	return 0;
}

static ssize_t speed_tc1_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct max31760_data *data = dev_get_drvdata(dev);
	u32 speed;
	int ret;

	if (!data) {
		pr_err("invalid driver pointer\n");
		return -ENODEV;
	}

	ret = max31760_read_speed(data, 1, &speed);
	if (ret < 0) {
		dev_err(data->dev, "can not read fan speed\n");
		return -EINVAL;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", speed);
	dev_dbg(data->dev, "TC1 current speed is %d\n", speed);

	return ret;
}

static ssize_t speed_tc2_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct max31760_data *data = dev_get_drvdata(dev);
	u32 speed;
	int ret;

	if (!data) {
		pr_err("invalid driver pointer\n");
		return -ENODEV;
	}

	ret = max31760_read_speed(data, 2, &speed);
	if (ret < 0) {
		dev_err(data->dev, "can not read fan speed\n");
		return -EINVAL;
	}

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", speed);
	dev_dbg(data->dev, "TC2 current speed is %d\n", speed);

	return ret;
}

static ssize_t pwm_duty_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct max31760_data *data = dev_get_drvdata(dev);
	u32 duty;
	u8 value = 0;
	int ret;

	if (!data) {
		pr_err("invalid driver pointer\n");
		return -ENODEV;
	}

	ret = max31760_read_byte(data, MAX31760_DUTY_CYCLE_CTRL_REG, &value);
	if (ret < 0)
		return ret;

	duty = value * PWM_FACTOR;
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", duty);

	return ret;
}

static DEVICE_ATTR_RW(speed_control);
static DEVICE_ATTR_RO(speed_tc1);
static DEVICE_ATTR_RO(speed_tc2);
static DEVICE_ATTR_RO(pwm_duty);

static struct attribute *max31760_sysfs_attrs[] = {
	&dev_attr_speed_control.attr,
	&dev_attr_speed_tc1.attr,
	&dev_attr_speed_tc2.attr,
	&dev_attr_pwm_duty.attr,
	NULL,
};

static int max31760_register_cdev(struct max31760_data *pdata)
{
	int ret = 0;
	char cdev_name[THERMAL_NAME_LENGTH] = "";

	snprintf(cdev_name, THERMAL_NAME_LENGTH, "fan-max31760");

	pdata->cdev = thermal_of_cooling_device_register(pdata->dev->of_node, cdev_name,
						pdata, &max31760_cooling_ops);
	if (IS_ERR(pdata->cdev)) {
		ret = PTR_ERR(pdata->cdev);
		dev_err(pdata->dev, "Cooling register failed for %s, ret:%d\n", cdev_name, ret);
		pdata->cdev = NULL;
		return ret;
	}

	dev_dbg(pdata->dev, "Cooling register success for %s\n", cdev_name);
	return 0;
}

static void max31760_hw_init(struct max31760_data *pdata)
{
	max31760_write_byte(pdata, MAX31760_CTRL_REG1, 0x19);
	max31760_write_byte(pdata, MAX31760_CTRL_REG2, 0x11);
	if (pdata->fan_num == 1)
		max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x31);
	else if (pdata->fan_num == 2)
		max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x33);
	mutex_lock(&pdata->update_lock);
	max31760_speed_control(pdata, FAN_SPEED_LEVEL0);
	pdata->cur_state = FAN_SPEED_LEVEL0;
	mutex_unlock(&pdata->update_lock);

	atomic_set(&pdata->in_suspend, 0);
}

static int max31760_parse_dt(struct max31760_data *pdata)
{
	int ret = 0;
	struct device_node *node = pdata->dev->of_node;

	if (!node) {
		pr_err("device tree info missing\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "maxim,fan-num", &pdata->fan_num);
	if (ret)
		pdata->fan_num = 1;
	if (pdata->fan_num > 2)
		pdata->fan_num = 2;

	pdata->pwr_en_gpio = of_get_named_gpio(node, "maxim,pwr-en-gpio", 0);
	if (!gpio_is_valid(pdata->pwr_en_gpio)) {
		dev_err(pdata->dev, "enable gpio not specified\n");
		return -EINVAL;
	}

	ret = gpio_request(pdata->pwr_en_gpio, "pwr_en_gpio");
	if (ret) {
		pr_err("enable gpio request failed, ret:%d\n", ret);
		goto error;
	}

	max31760_enable_gpio(pdata, 1);

	return ret;

error:
	gpio_free(pdata->pwr_en_gpio);
	return ret;
}

static struct attribute_group max31760_attribute_group = {
	.attrs = max31760_sysfs_attrs,
};

static int max31760_enable_vregs(struct max31760_data *pdata)
{
	int ret = 0;

	pdata->vdd_reg = devm_regulator_get(pdata->dev, "maxim,vdd");
	if (IS_ERR(pdata->vdd_reg)) {
		ret = PTR_ERR(pdata->vdd_reg);
		dev_err(pdata->dev, "couldn't get vdd_reg regulator, ret:%d\n", ret);
		pdata->vdd_reg = NULL;
		return ret;
	}

	regulator_set_voltage(pdata->vdd_reg, VDD_MIN_UV, VDD_MAX_UV);
	regulator_set_load(pdata->vdd_reg, VDD_LOAD_UA);
	ret = regulator_enable(pdata->vdd_reg);
	if (ret < 0) {
		dev_err(pdata->dev, "vdd_reg regulator failed, ret:%d\n", ret);
		regulator_set_voltage(pdata->vdd_reg, 0, VDD_MAX_UV);
		regulator_set_load(pdata->vdd_reg, 0);
		return -EINVAL;
	}

	pdata->vcca_reg = devm_regulator_get(pdata->dev, "maxim,vcca");
	if (IS_ERR(pdata->vcca_reg)) {
		ret = PTR_ERR(pdata->vcca_reg);
		dev_err(pdata->dev, "couldn't get vcca_reg regulator, ret:%d\n", ret);
		pdata->vcca_reg = NULL;
		return ret;
	}

	regulator_set_voltage(pdata->vcca_reg, VCCA_MIN_UV, VCCA_MAX_UV);
	regulator_set_load(pdata->vcca_reg, VCCA_LOAD_UA);
	ret = regulator_enable(pdata->vcca_reg);
	if (ret < 0) {
		dev_err(pdata->dev, "vcca_reg regulator failed, ret:%d\n", ret);
		regulator_set_voltage(pdata->vcca_reg, 0, VCCA_MAX_UV);
		regulator_set_load(pdata->vcca_reg, 0);
		return -EINVAL;
	}

	return 0;
}

static void max31760_remove(struct i2c_client *client)
{
	struct max31760_data *pdata = i2c_get_clientdata(client);

	if (!pdata)
		return;

	thermal_cooling_device_unregister(pdata->cdev);
	regulator_disable(pdata->vdd_reg);
	regulator_disable(pdata->vcca_reg);
	max31760_enable_gpio(pdata, 0);
	gpio_free(pdata->pwr_en_gpio);
}

static int max31760_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct max31760_data *pdata;

	if (!client || !client->dev.of_node) {
		pr_err("max31760 invalid input\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct max31760_data), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = &client->dev;
	pdata->i2c_client = client;
	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);
	mutex_init(&pdata->update_lock);

	ret = max31760_parse_dt(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to parse device tree, ret:%d\n", ret);
		goto fail_parse_dt;
	}

	ret = max31760_enable_vregs(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to enable regulators, ret:%d\n", ret);
		goto fail_enable_vregs;
	}

	max31760_hw_init(pdata);
	ret = max31760_register_cdev(pdata);
	if (ret) {
		dev_err(pdata->dev, "failed to register cooling device, ret:%d\n", ret);
		goto fail_register_cdev;
	}

	ret = devm_device_add_group(&client->dev, &max31760_attribute_group);
	if (ret < 0) {
		dev_err(pdata->dev, "couldn't register sysfs group\n");
		return ret;
	}

	return ret;

fail_register_cdev:
	max31760_remove(client);
	return ret;
fail_enable_vregs:
	max31760_enable_gpio(pdata, 0);
	gpio_free(pdata->pwr_en_gpio);
fail_parse_dt:
	i2c_set_clientdata(client, NULL);
	dev_set_drvdata(&client->dev, NULL);
	return ret;
}

static void max31760_shutdown(struct i2c_client *client)
{
	max31760_remove(client);
}

static int max31760_suspend(struct device *dev)
{
	struct max31760_data *pdata = dev_get_drvdata(dev);

	dev_dbg(dev, "enter suspend now\n");
	if (pdata) {
		atomic_set(&pdata->in_suspend, 1);
		mutex_lock(&pdata->update_lock);
		max31760_speed_control(pdata, FAN_SPEED_LEVEL0);
		max31760_enable_gpio(pdata, 0);
		regulator_disable(pdata->vdd_reg);
		mutex_unlock(&pdata->update_lock);
	}

	return 0;
}

static int max31760_resume(struct device *dev)
{
	struct max31760_data *pdata = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "enter resume now\n");
	if (pdata) {
		atomic_set(&pdata->in_suspend, 0);
		mutex_lock(&pdata->update_lock);
		max31760_enable_gpio(pdata, 1);

		ret = regulator_enable(pdata->vdd_reg);
		if (ret < 0)
			dev_err(pdata->dev, "vdd_reg regulator failed, ret:%d\n", ret);

		max31760_write_byte(pdata, MAX31760_CTRL_REG1, 0x19);
		max31760_write_byte(pdata, MAX31760_CTRL_REG2, 0x11);
		if (pdata->fan_num == 1)
			max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x31);
		else if (pdata->fan_num == 2)
			max31760_write_byte(pdata, MAX31760_CTRL_REG3, 0x33);
		max31760_set_cur_state_common(pdata, pdata->cur_state);
		mutex_unlock(&pdata->update_lock);
	}

	return 0;
}

static const struct of_device_id max31760_id_table[] = {
	{ .compatible = "maxim,max31760",},
	{ },
};

static const struct i2c_device_id max31760_i2c_table[] = {
	{ "max31760", 0 },
	{ },
};

static SIMPLE_DEV_PM_OPS(max31760_pm_ops, max31760_suspend, max31760_resume);

static struct i2c_driver max31760_i2c_driver = {
	.probe = max31760_probe,
	.remove = max31760_remove,
	.shutdown = max31760_shutdown,
	.driver = {
		.name = "max31760",
		.of_match_table = max31760_id_table,
		.pm = &max31760_pm_ops,
	},
	.id_table = max31760_i2c_table,
};

module_i2c_driver(max31760_i2c_driver);
MODULE_DEVICE_TABLE(i2c, max31760_i2c_table);
MODULE_DESCRIPTION("Maxim 31760 Fan Controller");
MODULE_LICENSE("GPL");
