// SPDX-License-Identifier: GPL-2.0
/*
 * pisp_dmy driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION				KERNEL_VERSION(0, 0x01, 0x0)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN			V4L2_CID_GAIN
#endif

#define PISP_DMY_XVCLK_FREQ			24000000

#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

#define OF_CAMERA_MODULE_REGULATORS		"rockchip,regulator-names"
#define OF_CAMERA_MODULE_REGULATOR_VOLTAGES	"rockchip,regulator-voltages"

#define PISP_DMY_NAME				"pisp_dmy"

struct pisp_dmy_gpio {
	int pltfrm_gpio;
	const char *label;
	enum of_gpio_flags active_low;
};

struct pisp_dmy_regulator {
	struct regulator *regulator;
	u32 min_uV;
	u32 max_uV;
};

struct pisp_dmy_regulators {
	u32 cnt;
	struct pisp_dmy_regulator *regulator;
};

struct pisp_dmy {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*rst_gpio;
	struct gpio_desc	*rst2_gpio;
	struct gpio_desc	*pd_gpio;
	struct gpio_desc	*pd2_gpio;
	struct gpio_desc	*pwd_gpio;
	struct gpio_desc	*pwd2_gpio;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct mutex		mutex;
	bool			power_on;
	struct pisp_dmy_regulators regulators;

	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_pisp_dmy(sd) container_of(sd, struct pisp_dmy, subdev)

static int __pisp_dmy_power_on(struct pisp_dmy *pisp_dmy)
{
	u32 i;
	int ret;
	struct pisp_dmy_regulator *regulator;
	struct device *dev = &pisp_dmy->client->dev;

	if (!IS_ERR_OR_NULL(pisp_dmy->pins_default)) {
		ret = pinctrl_select_state(pisp_dmy->pinctrl,
					   pisp_dmy->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

	ret = clk_prepare_enable(pisp_dmy->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (pisp_dmy->regulators.regulator) {
		for (i = 0; i < pisp_dmy->regulators.cnt; i++) {
			regulator = pisp_dmy->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_set_voltage(
				regulator->regulator,
				regulator->min_uV,
				regulator->max_uV);
			if (regulator_enable(regulator->regulator)) {
				dev_err(dev,
					"regulator_enable failed!\n");
				goto disable_clk;
			}
		}
	}
	usleep_range(3000, 5000);

	if (!IS_ERR(pisp_dmy->pwd_gpio)) {
		gpiod_direction_output(pisp_dmy->pwd_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(pisp_dmy->pwd2_gpio)) {
		gpiod_direction_output(pisp_dmy->pwd2_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(pisp_dmy->pd_gpio)) {
		gpiod_direction_output(pisp_dmy->pd_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(pisp_dmy->pd2_gpio)) {
		gpiod_direction_output(pisp_dmy->pd2_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(pisp_dmy->rst_gpio)) {
		gpiod_direction_output(pisp_dmy->rst_gpio, 0);
		usleep_range(1500, 2000);
		gpiod_direction_output(pisp_dmy->rst_gpio, 1);
	}

	if (!IS_ERR(pisp_dmy->rst2_gpio)) {
		gpiod_direction_output(pisp_dmy->rst2_gpio, 0);
		usleep_range(1500, 2000);
		gpiod_direction_output(pisp_dmy->rst2_gpio, 1);
	}

	return 0;

disable_clk:
	clk_disable_unprepare(pisp_dmy->xvclk);

	return ret;
}

static void __pisp_dmy_power_off(struct pisp_dmy *pisp_dmy)
{
	u32 i;
	int ret;
	struct pisp_dmy_regulator *regulator;
	struct device *dev = &pisp_dmy->client->dev;

	if (!IS_ERR(pisp_dmy->pd_gpio))
		gpiod_direction_output(pisp_dmy->pd_gpio, 0);

	if (!IS_ERR(pisp_dmy->pd2_gpio))
		gpiod_direction_output(pisp_dmy->pd2_gpio, 0);

	clk_disable_unprepare(pisp_dmy->xvclk);

	if (!IS_ERR(pisp_dmy->rst_gpio))
		gpiod_direction_output(pisp_dmy->rst_gpio, 0);

	if (!IS_ERR(pisp_dmy->rst2_gpio))
		gpiod_direction_output(pisp_dmy->rst2_gpio, 0);

	if (!IS_ERR(pisp_dmy->pwd_gpio))
		gpiod_direction_output(pisp_dmy->pwd_gpio, 0);

	if (!IS_ERR(pisp_dmy->pwd2_gpio))
		gpiod_direction_output(pisp_dmy->pwd2_gpio, 0);

	if (!IS_ERR_OR_NULL(pisp_dmy->pins_sleep)) {
		ret = pinctrl_select_state(pisp_dmy->pinctrl,
					   pisp_dmy->pins_sleep);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (pisp_dmy->regulators.regulator) {
		for (i = 0; i < pisp_dmy->regulators.cnt; i++) {
			regulator = pisp_dmy->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_disable(regulator->regulator);
		}
	}
}

static int pisp_dmy_power(struct v4l2_subdev *sd, int on)
{
	struct pisp_dmy *pisp_dmy = to_pisp_dmy(sd);
	int ret = 0;

	mutex_lock(&pisp_dmy->mutex);

	/* If the power state is not modified - no work to do. */
	if (pisp_dmy->power_on == !!on)
		goto exit;

	if (on) {
		ret = __pisp_dmy_power_on(pisp_dmy);
		if (ret < 0)
			goto exit;

		pisp_dmy->power_on = true;
	} else {
		__pisp_dmy_power_off(pisp_dmy);
		pisp_dmy->power_on = false;
	}

exit:
	mutex_unlock(&pisp_dmy->mutex);

	return ret;
}

static void pisp_dmy_get_module_inf(struct pisp_dmy *pisp_dmy,
				    struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, PISP_DMY_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, pisp_dmy->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, pisp_dmy->len_name, sizeof(inf->base.lens));
}

static long pisp_dmy_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct pisp_dmy *pisp_dmy = to_pisp_dmy(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		pisp_dmy_get_module_inf(pisp_dmy, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long pisp_dmy_compat_ioctl32(struct v4l2_subdev *sd,
				    unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = pisp_dmy_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = pisp_dmy_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int pisp_dmy_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct pisp_dmy *pisp_dmy = to_pisp_dmy(sd);

	return __pisp_dmy_power_on(pisp_dmy);
}

static int pisp_dmy_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct pisp_dmy *pisp_dmy = to_pisp_dmy(sd);

	__pisp_dmy_power_off(pisp_dmy);

	return 0;
}

static const struct dev_pm_ops pisp_dmy_pm_ops = {
	SET_RUNTIME_PM_OPS(pisp_dmy_runtime_suspend,
			   pisp_dmy_runtime_resume, NULL)
};

static const struct v4l2_subdev_core_ops pisp_dmy_core_ops = {
	.s_power = pisp_dmy_power,
	.ioctl = pisp_dmy_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = pisp_dmy_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops pisp_dmy_subdev_ops = {
	.core = &pisp_dmy_core_ops,
};

static int pisp_dmy_analyze_dts(struct pisp_dmy *pisp_dmy)
{
	int ret;
	int elem_size, elem_index;
	const char *str = "";
	struct property *prop;
	struct pisp_dmy_regulator *regulator;
	struct device *dev = &pisp_dmy->client->dev;
	struct device_node *np = of_node_get(dev->of_node);

	pisp_dmy->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(pisp_dmy->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(pisp_dmy->xvclk, PISP_DMY_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(pisp_dmy->xvclk) != PISP_DMY_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	pisp_dmy->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(pisp_dmy->pinctrl)) {
		pisp_dmy->pins_default =
			pinctrl_lookup_state(pisp_dmy->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(pisp_dmy->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		pisp_dmy->pins_sleep =
			pinctrl_lookup_state(pisp_dmy->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(pisp_dmy->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	elem_size = of_property_count_elems_of_size(
		np,
		OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
		sizeof(u32));
	prop = of_find_property(
		np,
		OF_CAMERA_MODULE_REGULATORS,
		NULL);
	if (elem_size > 0 && !IS_ERR_OR_NULL(prop)) {
		pisp_dmy->regulators.regulator =
			devm_kzalloc(&pisp_dmy->client->dev,
				     elem_size * sizeof(struct pisp_dmy_regulator),
				     GFP_KERNEL);
		if (!pisp_dmy->regulators.regulator) {
			dev_err(dev, "could not malloc pisp_dmy_regulator\n");
			return -ENOMEM;
		}

		pisp_dmy->regulators.cnt = elem_size;

		str = NULL;
		elem_index = 0;
		regulator = pisp_dmy->regulators.regulator;
		do {
			str = of_prop_next_string(prop, str);
			if (!str) {
				dev_err(dev, "%s is not match %s in dts\n",
					OF_CAMERA_MODULE_REGULATORS,
					OF_CAMERA_MODULE_REGULATOR_VOLTAGES);
				break;
			}
			regulator->regulator =
				devm_regulator_get_optional(dev, str);
			if (IS_ERR(regulator->regulator))
				dev_err(dev, "devm_regulator_get %s failed\n",
					str);
			of_property_read_u32_index(
				np,
				OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
				elem_index++,
				&regulator->min_uV);
			regulator->max_uV = regulator->min_uV;
			regulator++;
		} while (--elem_size);
	}

	pisp_dmy->pd_gpio = devm_gpiod_get(dev, "pd", GPIOD_OUT_LOW);
	if (IS_ERR(pisp_dmy->pd_gpio))
		dev_warn(dev, "can not find pd-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->pd_gpio));

	pisp_dmy->pd2_gpio = devm_gpiod_get(dev, "pd2", GPIOD_OUT_LOW);
	if (IS_ERR(pisp_dmy->pd2_gpio))
		dev_warn(dev, "can not find pd2-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->pd2_gpio));

	pisp_dmy->rst_gpio = devm_gpiod_get(dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(pisp_dmy->rst_gpio))
		dev_warn(dev, "can not find rst-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->rst_gpio));

	pisp_dmy->rst2_gpio = devm_gpiod_get(dev, "rst2", GPIOD_OUT_LOW);
	if (IS_ERR(pisp_dmy->rst2_gpio))
		dev_warn(dev, "can not find rst2-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->rst2_gpio));

	pisp_dmy->pwd_gpio = devm_gpiod_get(dev, "pwd", GPIOD_OUT_HIGH);
	if (IS_ERR(pisp_dmy->pwd_gpio))
		dev_warn(dev, "can not find pwd-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->pwd_gpio));

	pisp_dmy->pwd2_gpio = devm_gpiod_get(dev, "pwd2", GPIOD_OUT_HIGH);
	if (IS_ERR(pisp_dmy->pwd2_gpio))
		dev_warn(dev, "can not find pwd2-gpios, error %ld\n",
			 PTR_ERR(pisp_dmy->pwd2_gpio));

	return 0;
}

static int pisp_dmy_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct pisp_dmy *pisp_dmy;
	struct v4l2_subdev *sd;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	pisp_dmy = devm_kzalloc(dev, sizeof(*pisp_dmy), GFP_KERNEL);
	if (!pisp_dmy)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &pisp_dmy->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &pisp_dmy->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &pisp_dmy->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &pisp_dmy->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	pisp_dmy->client = client;

	ret = pisp_dmy_analyze_dts(pisp_dmy);
	if (ret) {
		dev_err(dev, "Failed to analyze dts\n");
		return ret;
	}

	mutex_init(&pisp_dmy->mutex);

	sd = &pisp_dmy->subdev;
	v4l2_i2c_subdev_init(sd, client, &pisp_dmy_subdev_ops);

	__pisp_dmy_power_on(pisp_dmy);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;
}

static int pisp_dmy_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct pisp_dmy *pisp_dmy = to_pisp_dmy(sd);

	mutex_destroy(&pisp_dmy->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__pisp_dmy_power_off(pisp_dmy);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id pisp_dmy_of_match[] = {
	{ .compatible = "pisp_dmy" },
	{},
};
MODULE_DEVICE_TABLE(of, pisp_dmy_of_match);
#endif

static const struct i2c_device_id pisp_dmy_match_id[] = {
	{ "pisp_dmy", 0 },
	{ },
};

static struct i2c_driver pisp_dmy_i2c_driver = {
	.driver = {
		.name = PISP_DMY_NAME,
		.pm = &pisp_dmy_pm_ops,
		.of_match_table = of_match_ptr(pisp_dmy_of_match),
	},
	.probe		= &pisp_dmy_probe,
	.remove		= &pisp_dmy_remove,
	.id_table	= pisp_dmy_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&pisp_dmy_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&pisp_dmy_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("preisp dummy sensor driver");
MODULE_LICENSE("GPL v2");
