/*
 * rk_camera_module.c
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef CONFIG_OF
#error "this file requires device tree support"
#endif

#ifndef SOFIA_3G_CAMERA_MODULE_H
#define SOFIA_3G_CAMERA_MODULE_H

#include <linux/i2c.h>
#include <linux/slab.h>
#ifdef CONFIG_PLATFORM_DEVICE_PM
#include <linux/device_state_pm.h>
#endif
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <media/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <linux/lcm.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_data/rk_isp10_platform_camera_module.h>
#include <linux/platform_data/rk_isp10_platform.h>
#include <media/v4l2-controls_rockchip.h>

#define OF_OV_GPIO_PD "rockchip,pd-gpio"
#define OF_OV_GPIO_PWR "rockchip,pwr-gpio"
#define OF_OV_GPIO_FLASH "rockchip,flash-gpio"
#define OF_OV_GPIO_TORCH "rockchip,torch-gpio"
#define OF_OV_GPIO_RESET "rockchip,rst-gpio"

#define OF_CAMERA_MODULE_NAME "rockchip,camera-module-name"
#define OF_CAMERA_MODULE_LEN_NAME "rockchip,camera-module-len-name"
#define OF_CAMERA_MODULE_FOV_H "rockchip,camera-module-fov-h"
#define OF_CAMERA_MODULE_FOV_V "rockchip,camera-module-fov-v"
#define OF_CAMERA_MODULE_ORIENTATION "rockchip,camera-module-orientation"
#define OF_CAMERA_MODULE_FOCAL_LENGTH "rockchip,camera-module-focal-length"
#define OF_CAMERA_MODULE_FOCUS_DISTANCE "rockchip,camera-module-focus-distance"
#define OF_CAMERA_MODULE_IQ_MIRROR "rockchip,camera-module-iq-mirror"
#define OF_CAMERA_MODULE_IQ_FLIP "rockchip,camera-module-iq-flip"
#define OF_CAMERA_MODULE_FLIP "rockchip,camera-module-flip"
#define OF_CAMERA_MODULE_MIRROR "rockchip,camera-module-mirror"
#define OF_CAMERA_FLASH_SUPPORT "rockchip,camera-module-flash-support"
#define OF_CAMERA_FLASH_EXP_PERCENT "rockchip,camera-module-flash-exp-percent"
#define OF_CAMERA_FLASH_TURN_ON_TIME "rockchip,camera-module-flash-turn-on-time"
#define OF_CAMERA_FLASH_ON_TIMEOUT "rockchip,camera-module-flash-on-timeout"
#define OF_CAMERA_MODULE_DEFRECT0 "rockchip,camera-module-defrect0"
#define OF_CAMERA_MODULE_DEFRECT1 "rockchip,camera-module-defrect1"
#define OF_CAMERA_MODULE_DEFRECT2 "rockchip,camera-module-defrect2"
#define OF_CAMERA_MODULE_DEFRECT3 "rockchip,camera-module-defrect3"
#define OF_CAMERA_MODULE_MIPI_DPHY_INDEX "rockchip,camera-module-mipi-dphy-index"

#define OF_CAMERA_MODULE_REGULATORS "rockchip,camera-module-regulator-names"
#define OF_CAMERA_MODULE_REGULATOR_VOLTAGES "rockchip,camera-module-regulator-voltages"
#define OF_CAMERA_MODULE_MCLK_NAME "rockchip,camera-module-mclk-name"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT "rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP "rockchip,camera_sleep"

const char *PLTFRM_CAMERA_MODULE_PIN_PD = OF_OV_GPIO_PD;
const char *PLTFRM_CAMERA_MODULE_PIN_PWR = OF_OV_GPIO_PWR;
const char *PLTFRM_CAMERA_MODULE_PIN_FLASH = OF_OV_GPIO_FLASH;
const char *PLTFRM_CAMERA_MODULE_PIN_TORCH = OF_OV_GPIO_TORCH;
const char *PLTFRM_CAMERA_MODULE_PIN_RESET = OF_OV_GPIO_RESET;

#define I2C_M_WR 0
#define I2C_MSG_MAX 300
#define I2C_DATA_MAX (I2C_MSG_MAX * 3)

struct pltfrm_camera_module_gpio {
	int pltfrm_gpio;
	const char *label;
	enum of_gpio_flags active_low;
};

struct pltfrm_camera_module_regulator {
	struct regulator *regulator;
	unsigned int min_uV;
	unsigned int max_uV;
};

struct pltfrm_camera_module_regulators {
	unsigned int cnt;
	struct pltfrm_camera_module_regulator *regulator;
};

struct pltfrm_camera_module_fl {
	const char *flash_driver_name;
	/* flash ,torch */
	char fl_init_status;
	struct pltfrm_camera_module_gpio *fl_flash;
	struct pltfrm_camera_module_gpio *fl_torch;
	struct v4l2_subdev *flsh_ctrl;
};

struct pltfrm_camera_module_info_s {
	const char *module_name;
	const char *len_name;
	const char *fov_h;
	const char *fov_v;
	const char *focal_length;
	const char *focus_distance;
	int facing;
	int orientation;
	bool iq_mirror;
	bool iq_flip;
	int flip;
	int mirror;
	int flash_support;
	int flash_exp_percent;
	int flash_turn_on_time;
	int flash_on_timeout;
};

struct pltfrm_camera_module_mipi {
	unsigned int dphy_index;
};

struct pltfrm_camera_module_itf {
	union {
		struct pltfrm_camera_module_mipi mipi;
	} itf;
};

struct pltfrm_camera_module_data {
	struct pltfrm_camera_module_gpio gpios[6];
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct pinctrl_state *pins_sleep;
	struct v4l2_subdev *af_ctrl;
	/* move to struct pltfrm_camera_module_fl */
	/* const char *flash_driver_name; */
	struct pltfrm_camera_module_fl fl_ctrl;

	struct pltfrm_camera_module_info_s info;
	struct pltfrm_camera_module_itf itf;
	struct pltfrm_cam_defrect defrects[4];
	struct clk *mclk;
	struct pltfrm_soc_cfg *soc_cfg;
	struct pltfrm_camera_module_regulators regulators;

	void *priv;
};

/* ======================================================================== */

static int pltfrm_camera_module_set_pinctrl_state(
	struct v4l2_subdev *sd,
	struct pinctrl_state *state)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int ret = 0;

	if (!IS_ERR_OR_NULL(state)) {
		ret = pinctrl_select_state(pdata->pinctrl, state);
		if (IS_ERR_VALUE(ret))
			pltfrm_camera_module_pr_debug(sd,
				"could not set pins\n");
	}

	return ret;
}

static int pltfrm_camera_module_init_gpio(
	struct v4l2_subdev *sd)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int i = 0;

	ret = pltfrm_camera_module_set_pinctrl_state(sd, pdata->pins_default);
	if (IS_ERR_VALUE(ret))
		goto err;

	for (i = 0; i < ARRAY_SIZE(pdata->gpios); i++) {
		if (gpio_is_valid(pdata->gpios[i].pltfrm_gpio)) {
			if (pdata->gpios[i].label ==
				PLTFRM_CAMERA_MODULE_PIN_FLASH ||
				pdata->gpios[i].label ==
				PLTFRM_CAMERA_MODULE_PIN_TORCH) {
				if (pdata->fl_ctrl.fl_init_status &&
		    pdata->fl_ctrl.fl_flash->pltfrm_gpio ==
		    pdata->fl_ctrl.fl_torch->pltfrm_gpio) {
					pltfrm_camera_module_pr_info(
						sd,
						"fl gpio has been inited, continue!\n");
					continue;
				}
				pdata->fl_ctrl.fl_init_status = 1;
			}

			pltfrm_camera_module_pr_debug(
			    sd,
				"requesting GPIO #%d ('%s')\n",
				pdata->gpios[i].pltfrm_gpio,
				pdata->gpios[i].label);
			ret = gpio_request_one(
				pdata->gpios[i].pltfrm_gpio,
				GPIOF_DIR_OUT,
				pdata->gpios[i].label);
			if (IS_ERR_VALUE(ret)) {
				if ((pdata->gpios[i].label ==
					PLTFRM_CAMERA_MODULE_PIN_RESET) ||
					(pdata->gpios[i].label ==
					PLTFRM_CAMERA_MODULE_PIN_PWR)) {
					pltfrm_camera_module_pr_warn(sd,
					"GPIO #%d ('%s') may be reused!\n",
					pdata->gpios[i].pltfrm_gpio,
					pdata->gpios[i].label);
				} else {
					pltfrm_camera_module_pr_err(sd,
						"failed to request GPIO #%d ('%s')\n",
						pdata->gpios[i].pltfrm_gpio,
						pdata->gpios[i].label);
					goto err;
				}
			}

			if (pdata->gpios[i].label ==
				PLTFRM_CAMERA_MODULE_PIN_PD)
				ret = pltfrm_camera_module_set_pin_state(sd,
				pdata->gpios[i].label,
				PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE);
			else if (pdata->gpios[i].label ==
				PLTFRM_CAMERA_MODULE_PIN_RESET)
				ret = pltfrm_camera_module_set_pin_state(sd,
					pdata->gpios[i].label,
					PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE);
			else
				ret = pltfrm_camera_module_set_pin_state(sd,
					pdata->gpios[i].label,
					PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE
					);
		}
	}
	return 0;
err:
	pltfrm_camera_module_pr_err(sd, "failed with error %d\n", ret);
	for (; i < ARRAY_SIZE(pdata->gpios); i++)
		pdata->gpios[i].pltfrm_gpio = -1;

	return ret;
}

static struct pltfrm_camera_module_data *pltfrm_camera_module_get_data(
	struct v4l2_subdev *sd)
{
	int ret = 0;
	int elem_size, elem_index;
	const char *str = "";
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device_node *np = of_node_get(client->dev.of_node);
	struct device_node *af_ctrl_node = NULL;
	struct i2c_client *af_ctrl_client = NULL;
	struct device_node *fl_ctrl_node = NULL;
	struct i2c_client *fl_ctrl_client = NULL;
	struct pltfrm_camera_module_data *pdata = NULL;
	struct property *prop;

	pltfrm_camera_module_pr_debug(sd, "\n");

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pdata)) {
		ret = -ENOMEM;
		goto err;
	}

	client->dev.platform_data = pdata;

	ret = of_property_read_string(np, OF_CAMERA_MODULE_MCLK_NAME, &str);
	if (ret) {
		pltfrm_camera_module_pr_err(sd,
			"cannot not get camera-module-mclk-name property of node %s\n",
			np->name);
		ret = -ENODEV;
		goto err;
	}

	pdata->mclk = devm_clk_get(&client->dev, str);
	if (IS_ERR_OR_NULL(pdata->mclk)) {
		pltfrm_camera_module_pr_err(sd,
			"cannot not get %s property of node %s\n",
			str, np->name);
		ret = -ENODEV;
		goto err;
	}

	ret = of_property_read_string(np,
			"rockchip,camera-module-facing", &str);
	if (ret) {
		pltfrm_camera_module_pr_err(sd,
			"cannot not get camera-module-facing property of node %s\n",
			np->name);
	} else {
		pltfrm_camera_module_pr_debug(sd,
			"camera module camera-module-facing driver is %s\n",
			str);
	}
	pdata->info.facing = -1;
	if (!strcmp(str, "back"))
		pdata->info.facing = 0;
	else if (!strcmp(str, "front"))
		pdata->info.facing = 1;

	ret = of_property_read_string(np, "rockchip,flash-driver",
			&pdata->fl_ctrl.flash_driver_name);
	if (ret) {
		pltfrm_camera_module_pr_debug(sd,
				"cannot not get flash-driver property of node %s\n",
				np->name);
		pdata->fl_ctrl.flash_driver_name = "0";
	} else {
		pltfrm_camera_module_pr_info(sd,
			"camera module flash driver is %s\n",
			(char *)pdata->fl_ctrl.flash_driver_name);

		/*parse flash node info*/
		fl_ctrl_node = of_parse_phandle(np, "rockchip,fl-ctrl", 0);
		if (!IS_ERR_OR_NULL(fl_ctrl_node)) {
			fl_ctrl_client =
				of_find_i2c_device_by_node(fl_ctrl_node);
			of_node_put(fl_ctrl_node);
			if (IS_ERR_OR_NULL(fl_ctrl_client)) {
				pltfrm_camera_module_pr_err(sd,
					"cannot not get node\n");
				ret = -EFAULT;
				goto err;
			}
			pdata->fl_ctrl.flsh_ctrl =
				i2c_get_clientdata(fl_ctrl_client);
			if (IS_ERR_OR_NULL(pdata->fl_ctrl.flsh_ctrl)) {
				pltfrm_camera_module_pr_warn(sd,
					"cannot not get camera i2c client, maybe not yet created, deferring device probing...\n");
				ret = -EPROBE_DEFER;
				goto err;
			}
			pltfrm_camera_module_pr_info(sd,
				"camera module has flash driver %s\n",
				pltfrm_dev_string(pdata->fl_ctrl.flsh_ctrl));
		}
	}

	af_ctrl_node = of_parse_phandle(np, "rockchip,af-ctrl", 0);
	if (!IS_ERR_OR_NULL(af_ctrl_node)) {
		af_ctrl_client = of_find_i2c_device_by_node(af_ctrl_node);
		of_node_put(af_ctrl_node);
		if (IS_ERR_OR_NULL(af_ctrl_client)) {
			pltfrm_camera_module_pr_err(sd,
				"cannot not get node\n");
			ret = -EFAULT;
			goto err;
		}
		pdata->af_ctrl = i2c_get_clientdata(af_ctrl_client);
		if (IS_ERR_OR_NULL(pdata->af_ctrl)) {
			pltfrm_camera_module_pr_warn(sd,
				"cannot not get camera i2c client, maybe not yet created, deferring device probing...\n");
			ret = -EPROBE_DEFER;
			goto err;
		}
		pltfrm_camera_module_pr_info(sd,
			"camera module has auto focus controller %s\n",
			pltfrm_dev_string(pdata->af_ctrl));
	}

	pdata->pinctrl = devm_pinctrl_get(&client->dev);
	if (!IS_ERR(pdata->pinctrl)) {
		pdata->pins_default = pinctrl_lookup_state(
					pdata->pinctrl,
					OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(pdata->pins_default))
			pltfrm_camera_module_pr_warn(sd,
			"could not get default pinstate\n");

		pdata->pins_sleep = pinctrl_lookup_state(
				pdata->pinctrl, OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(pdata->pins_sleep))
			pltfrm_camera_module_pr_warn(sd,
			"could not get sleep pinstate\n");
	}

	elem_size = of_property_count_elems_of_size(
		np,
		OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
		sizeof(u32));
	prop = of_find_property(
		np,
		OF_CAMERA_MODULE_REGULATORS,
		NULL);
	if (!IS_ERR_VALUE(elem_size) &&
		!IS_ERR_OR_NULL(prop)) {
		struct pltfrm_camera_module_regulator *regulator;

		pdata->regulators.regulator = devm_kzalloc(&client->dev,
			elem_size *
			sizeof(struct pltfrm_camera_module_regulator),
			GFP_KERNEL);
		if (!pdata->regulators.regulator) {
			pltfrm_camera_module_pr_err(sd,
				"could not malloc pltfrm_camera_module_regulator\n");
			goto err;
		}
		pdata->regulators.cnt = elem_size;
		str = NULL;
		elem_index = 0;
		regulator = pdata->regulators.regulator;
		do {
			str = of_prop_next_string(prop, str);
			if (!str) {
				pltfrm_camera_module_pr_err(sd,
					"%s is not match %s in dts\n",
					OF_CAMERA_MODULE_REGULATORS,
					OF_CAMERA_MODULE_REGULATOR_VOLTAGES);
				break;
			}
			regulator->regulator =
				devm_regulator_get_optional(&client->dev, str);
			if (IS_ERR(regulator->regulator))
				pltfrm_camera_module_pr_err(sd,
					"devm_regulator_get %s failed\n",
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
	pdata->gpios[0].label = PLTFRM_CAMERA_MODULE_PIN_PD;
	pdata->gpios[0].pltfrm_gpio = of_get_named_gpio_flags(
		np,
		pdata->gpios[0].label,
		0,
		&pdata->gpios[0].active_low);

	pdata->gpios[1].label = PLTFRM_CAMERA_MODULE_PIN_PWR;
	pdata->gpios[1].pltfrm_gpio = of_get_named_gpio_flags(
		np,
		pdata->gpios[1].label,
		0,
		&pdata->gpios[1].active_low);

	pdata->gpios[2].label = PLTFRM_CAMERA_MODULE_PIN_FLASH;
	pdata->gpios[2].pltfrm_gpio = of_get_named_gpio_flags(
		np,
		pdata->gpios[2].label,
		0,
		&pdata->gpios[2].active_low);

	/*set fl_ctrl  flash reference*/
	pdata->fl_ctrl.fl_flash = &pdata->gpios[2];

	pdata->gpios[3].label = PLTFRM_CAMERA_MODULE_PIN_TORCH;
	pdata->gpios[3].pltfrm_gpio = of_get_named_gpio_flags(
		np,
		pdata->gpios[3].label,
		0,
		&pdata->gpios[3].active_low);

	/*set fl_ctrl torch reference*/
	pdata->fl_ctrl.fl_torch = &pdata->gpios[3];

	pdata->gpios[4].label = PLTFRM_CAMERA_MODULE_PIN_RESET;
	pdata->gpios[4].pltfrm_gpio = of_get_named_gpio_flags(
		np,
		pdata->gpios[4].label,
		0,
		&pdata->gpios[4].active_low);

	ret = of_property_read_string(np, OF_CAMERA_MODULE_NAME,
			&pdata->info.module_name);
	ret |= of_property_read_string(np, OF_CAMERA_MODULE_LEN_NAME,
			&pdata->info.len_name);
	if (ret) {
		pltfrm_camera_module_pr_err(
			sd,
			"could not get module %s and %s from dts!\n",
			OF_CAMERA_MODULE_NAME,
			OF_CAMERA_MODULE_LEN_NAME);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_MODULE_ORIENTATION,
		(unsigned int *)&pdata->info.orientation)) {
		pdata->info.orientation = -1;
		pltfrm_camera_module_pr_err(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_MODULE_ORIENTATION);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_MODULE_FLIP,
		(unsigned int *)&pdata->info.flip)) {
		pdata->info.flip = -1;
		pltfrm_camera_module_pr_err(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_MODULE_FLIP);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_MODULE_MIRROR,
		(unsigned int *)&pdata->info.mirror)) {
		pdata->info.mirror = -1;
		pltfrm_camera_module_pr_err(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_MODULE_MIRROR);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_FLASH_SUPPORT,
		(unsigned int *)&pdata->info.flash_support)) {
		pdata->info.flash_support = -1;
		pltfrm_camera_module_pr_err(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_FLASH_SUPPORT);
	}

	ret = of_property_read_string(np, OF_CAMERA_MODULE_FOV_H,
			&pdata->info.fov_h);
	ret |= of_property_read_string(np, OF_CAMERA_MODULE_FOV_V,
			&pdata->info.fov_v);
	if (ret) {
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get module %s and %s from dts!",
			OF_CAMERA_MODULE_FOV_H,
			OF_CAMERA_MODULE_FOV_V);
	}

	ret = of_property_read_string(np, OF_CAMERA_MODULE_FOCAL_LENGTH,
			&pdata->info.focal_length);
	if (ret) {
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get %s from dts!\n",
			OF_CAMERA_MODULE_FOCAL_LENGTH);
	}
	ret = of_property_read_string(np, OF_CAMERA_MODULE_FOCUS_DISTANCE,
			&pdata->info.focus_distance);
	if (ret) {
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get %s from dts!\n",
			OF_CAMERA_MODULE_FOCUS_DISTANCE);
	}

	ret = 0;
	of_property_read_u32(np, OF_CAMERA_MODULE_IQ_MIRROR, &ret);
	pdata->info.iq_mirror = (ret == 0) ? false : true;

	ret = 0;
	of_property_read_u32(np, OF_CAMERA_MODULE_IQ_FLIP, &ret);
	pdata->info.iq_flip = (ret == 0) ? false : true;

	if (of_property_read_u32(
		np,
		OF_CAMERA_FLASH_EXP_PERCENT,
		(unsigned int *)&pdata->info.flash_exp_percent)) {
		pdata->info.flash_exp_percent = -1;
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_FLASH_EXP_PERCENT);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_FLASH_TURN_ON_TIME,
		(unsigned int *)&pdata->info.flash_turn_on_time)) {
		pdata->info.flash_turn_on_time = -1;
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_FLASH_TURN_ON_TIME);
	}

	if (of_property_read_u32(
		np,
		OF_CAMERA_FLASH_ON_TIMEOUT,
		(unsigned int *)&pdata->info.flash_on_timeout)) {
		pdata->info.flash_on_timeout = -1;
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_FLASH_ON_TIMEOUT);
	}

	of_property_read_u32_array(
		np,
		OF_CAMERA_MODULE_DEFRECT0,
		(unsigned int *)&pdata->defrects[0],
		6);
	of_property_read_u32_array(
		np,
		OF_CAMERA_MODULE_DEFRECT1,
		(unsigned int *)&pdata->defrects[1],
		6);
	of_property_read_u32_array(
		np,
		OF_CAMERA_MODULE_DEFRECT2,
		(unsigned int *)&pdata->defrects[2],
		6);
	of_property_read_u32_array(
		np,
		OF_CAMERA_MODULE_DEFRECT3,
		(unsigned int *)&pdata->defrects[3],
		6);

	if (of_property_read_u32(
		np,
		OF_CAMERA_MODULE_MIPI_DPHY_INDEX,
		(unsigned int *)&pdata->itf.itf.mipi.dphy_index)) {
		pdata->itf.itf.mipi.dphy_index = -1;
		pltfrm_camera_module_pr_debug(
			sd,
			"could not get module %s from dts!\n",
			OF_CAMERA_MODULE_MIPI_DPHY_INDEX);
	}

	of_node_put(np);
	return pdata;
err:
	pltfrm_camera_module_pr_err(sd, "failed with error %d\n", ret);
	if (!IS_ERR_OR_NULL(pdata->regulators.regulator)) {
		devm_kfree(
			&client->dev,
			pdata->regulators.regulator);
		pdata->regulators.regulator = NULL;
	}

	if (!IS_ERR_OR_NULL(pdata->mclk)) {
		devm_clk_put(&client->dev, pdata->mclk);
		pdata->mclk = NULL;
	}
	if (!IS_ERR_OR_NULL(pdata)) {
		devm_kfree(&client->dev, pdata);
		pdata = NULL;
	}
	of_node_put(np);
	return ERR_PTR(ret);
}

static int pltfrm_camera_module_pix_frmt2code(
	const char *pix_frmt)
{
	if (strcmp(pix_frmt, "BAYER_BGGR8") == 0)
		return MEDIA_BUS_FMT_SBGGR8_1X8;
	if (strcmp(pix_frmt, "BAYER_GBRG8") == 0)
		return MEDIA_BUS_FMT_SGBRG8_1X8;
	if (strcmp(pix_frmt, "BAYER_GRBG8") == 0)
		return MEDIA_BUS_FMT_SGRBG8_1X8;
	if (strcmp(pix_frmt, "BAYER_RGGB8") == 0)
		return MEDIA_BUS_FMT_SRGGB8_1X8;
	if (strcmp(pix_frmt, "BAYER_BGGR10") == 0)
		return MEDIA_BUS_FMT_SBGGR10_1X10;
	if (strcmp(pix_frmt, "BAYER_GBRG10") == 0)
		return MEDIA_BUS_FMT_SGBRG10_1X10;
	if (strcmp(pix_frmt, "BAYER_GRBG10") == 0)
		return MEDIA_BUS_FMT_SGRBG10_1X10;
	if (strcmp(pix_frmt, "BAYER_RGGB10") == 0)
		return MEDIA_BUS_FMT_SRGGB10_1X10;
	if (strcmp(pix_frmt, "BAYER_BGGR12") == 0)
		return MEDIA_BUS_FMT_SBGGR12_1X12;
	if (strcmp(pix_frmt, "BAYER_GBRG12") == 0)
		return MEDIA_BUS_FMT_SGBRG12_1X12;
	if (strcmp(pix_frmt, "BAYER_GRBG12") == 0)
		return MEDIA_BUS_FMT_SGRBG12_1X12;
	if (strcmp(pix_frmt, "BAYER_RGGB12") == 0)
		return MEDIA_BUS_FMT_SRGGB12_1X12;
	if (strcmp(pix_frmt, "YUYV8") == 0)
		return MEDIA_BUS_FMT_YUYV8_2X8;
	if (strcmp(pix_frmt, "YUYV10") == 0)
		return MEDIA_BUS_FMT_YUYV10_2X10;
	if (strcmp(pix_frmt, "UYUV8") == 0)
		return MEDIA_BUS_FMT_UYVY8_2X8;
	return -EINVAL;
}

static int pltfrm_camera_module_config_matches(
	struct v4l2_subdev *sd,
	struct device_node *config,
	struct v4l2_mbus_framefmt *frm_fmt,
	struct v4l2_subdev_frame_interval *frm_intrvl)
{
	int ret = 0;
	struct property *prop;
	const char *of_pix_fmt;
	bool match = true;
	u32 min, min2, max, max2;
	u32 numerator, denominator;

	pltfrm_camera_module_pr_debug(sd,
		"pix_frm %d, %dx%d@%d/%dfps, config %s\n",
		frm_fmt->code, frm_fmt->width, frm_fmt->height,
		frm_intrvl->interval.denominator,
		frm_intrvl->interval.numerator,
		config->name);

	/* check pixel format */
	of_property_for_each_string(config, "rockchip,frm-pixel-format",
		prop, of_pix_fmt) {
		if (pltfrm_camera_module_pix_frmt2code(of_pix_fmt) ==
			frm_fmt->code) {
			match = true;
			break;
		}
	}

	if (!match)
		return 0;

	/* check frame width */
	ret = of_property_read_u32(config, "rockchip,frm-width", &min);
	if (ret == -EINVAL) {
		ret = of_property_read_u32_index(config,
				"rockchip,frm-width-range", 0, &min);
		if (ret == -EINVAL) {
			min = 0;
			max = UINT_MAX;
		} else if (IS_ERR_VALUE(ret)) {
			pltfrm_camera_module_pr_err(sd,
					"malformed property 'rockchip,frm-width-range'\n");
			goto err;
		} else {
			ret = of_property_read_u32_index(config,
					"rockchip,frm-width-range", 1, &max);
			if (IS_ERR_VALUE(ret)) {
				pltfrm_camera_module_pr_err(sd,
				"malformed property 'rockchip,frm-width-range'\n");
				goto err;
			}
		}
	} else if (IS_ERR_VALUE(ret)) {
		pltfrm_camera_module_pr_err(sd,
				"malformed property 'rockchip,frm-width'\n");
		goto err;
	} else {
		max = min;
	}
	if ((frm_fmt->width < min) || (frm_fmt->width > max))
		return 0;

	/* check frame height */
	ret = of_property_read_u32(config, "rockchip,frm-height", &min);
	if (ret == -EINVAL) {
		ret = of_property_read_u32_index(config,
				"rockchip,frm-height-range", 0, &min);
		if (ret == -EINVAL) {
			min = 0;
			max = UINT_MAX;
		} else if (IS_ERR_VALUE(ret)) {
			pltfrm_camera_module_pr_err(sd,
				"malformed property 'rockchip,frm-height-range'\n");
			goto err;
		} else {
			ret = of_property_read_u32_index(config,
					"rockchip,frm-height-range", 1, &max);
			if (IS_ERR_VALUE(ret)) {
				pltfrm_camera_module_pr_err(sd,
					"malformed property 'rockchip,frm-height-range'\n");
				goto err;
			}
		}
	} else if (IS_ERR_VALUE(ret)) {
		pltfrm_camera_module_pr_err(sd,
				"malformed property 'rockchip,frm-height'\n");
		goto err;
	} else {
		max = min;
	}
	if ((frm_fmt->height < min) || (frm_fmt->height > max))
		return 0;

	/* check frame interval */
	ret = of_property_read_u32_index(config,
				"rockchip,frm-interval", 0, &min);
	if (ret == -EINVAL) {
		ret = of_property_read_u32_index(config,
				"rockchip,frm-interval-range", 0, &min);
		if (ret == -EINVAL) {
			min = 0;
			max = UINT_MAX;
		} else if (IS_ERR_VALUE(ret)) {
			pltfrm_camera_module_pr_err(sd,
					"malformed property 'rockchip,frm-interval-range'\n");
			goto err;
		} else {
			ret |= of_property_read_u32_index(config,
				"rockchip,frm-interval-range", 1, &min2);
			ret |= of_property_read_u32_index(config,
				"rockchip,frm-interval-range", 2, &max);
			ret |= of_property_read_u32_index(config,
				"rockchip,frm-interval-range", 3, &max2);
			if (IS_ERR_VALUE(ret)) {
				pltfrm_camera_module_pr_err(sd,
					"malformed property 'rockchip,frm-interval-range'\n");
				goto err;
			}
		}
	} else if (IS_ERR_VALUE(ret)) {
		pltfrm_camera_module_pr_err(sd,
			"malformed property 'rockchip,frm-interval'\n");
		goto err;
	} else {
		ret = of_property_read_u32_index(config,
			"rockchip,frm-interval", 1, &min2);
		if (IS_ERR_VALUE(ret)) {
			pltfrm_camera_module_pr_err(sd,
				"malformed property 'rockchip,frm-interval'\n");
			goto err;
		}
		max = min;
		max2 = min2;
	}

	/* normalize frame intervals */
	denominator = lcm(min2, frm_intrvl->interval.denominator);
	denominator = lcm(max2, denominator);
	numerator = denominator / frm_intrvl->interval.denominator *
		frm_intrvl->interval.numerator;

	min = denominator / min2 * min;
	max = denominator / max2 * max;

	if ((numerator < min) || (numerator > max))
		return 0;

	return 1;
err:
	pltfrm_camera_module_pr_err(sd,
			"failed with error %d\n", ret);
	return ret;
}

static int pltfrm_camera_module_write_reglist_node(
	struct v4l2_subdev *sd,
	struct device_node *config_node)
{
	struct property *reg_table_prop;
	struct pltfrm_camera_module_reg *reg_table = NULL;
	u32 reg_table_num_entries;
	u32 i = 0;
	int ret = 0;

	reg_table_prop = of_find_property(config_node, "rockchip,reg-table",
		&reg_table_num_entries);
	if (!IS_ERR_OR_NULL(reg_table_prop)) {
		if (((reg_table_num_entries / 12) == 0) ||
		(reg_table_num_entries % 3)) {
			pltfrm_camera_module_pr_err(sd,
				"wrong register format in %s, must be 'type, address, value' per register\n",
				config_node->name);
			ret = -EINVAL;
			goto err;
		}

		reg_table_num_entries /= 12;
		reg_table = (struct pltfrm_camera_module_reg *)
			kmalloc(reg_table_num_entries *
				sizeof(struct pltfrm_camera_module_reg),
				GFP_KERNEL);
		if (IS_ERR_OR_NULL(reg_table)) {
			pltfrm_camera_module_pr_err(sd,
				"memory allocation failed\n");
			ret = -ENOMEM;
			goto err;
		}

		pltfrm_camera_module_pr_debug(sd,
			"patching config with %s (%d registers)\n",
			 config_node->name, reg_table_num_entries);
		for (i = 0; i < reg_table_num_entries; i++) {
			u32 val;

			ret |= of_property_read_u32_index(
				config_node, "rockchip,reg-table",
				3 * i, &val);
			reg_table[i].flag = val;
			ret |= of_property_read_u32_index(
				config_node, "rockchip,reg-table",
				3 * i + 1, &val);
			reg_table[i].reg = val;
			ret |= of_property_read_u32_index(
				config_node, "rockchip,reg-table",
				3 * i + 2, &val);
			reg_table[i].val = val;
			if (IS_ERR_VALUE(ret)) {
				pltfrm_camera_module_pr_err(sd,
					"error while reading property %s at index %d\n",
					"rockchip,reg-table", i);
				goto err;
			}
		}
		ret = pltfrm_camera_module_write_reglist(
			sd, reg_table, reg_table_num_entries);
		if (IS_ERR_VALUE(ret))
			goto err;
		kfree(reg_table);
		reg_table = NULL;
	}
	return 0;
err:
	pltfrm_camera_module_pr_err(sd,
			"failed with error %d\n", ret);
	kfree(reg_table);
	return ret;
}

/* ======================================================================== */

const char *pltfrm_dev_string(
	struct v4l2_subdev *sd)
{
	struct i2c_client *client;

	if (IS_ERR_OR_NULL(sd))
		return "";
	client = v4l2_get_subdevdata(sd);
	if (IS_ERR_OR_NULL(client))
		return "";
	return dev_driver_string(&client->dev);
}

int pltfrm_camera_module_read_reg(
		struct v4l2_subdev *sd,
	u16 data_length,
	u16 reg,
	u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	struct i2c_msg msg[1];
	unsigned char data[4] = { 0, 0, 0, 0 };

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	msg->addr = client->addr;
	msg->flags = I2C_M_WR;
	msg->len = 2;
	msg->buf = data;

	/* High byte goes out first */
	data[0] = (u8)(reg >> 8);
	data[1] = (u8)(reg & 0xff);

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret >= 0) {
		mdelay(3);
		msg->flags = I2C_M_RD;
		msg->len = data_length;
		i2c_transfer(client->adapter, msg, 1);
	}
	if (ret >= 0) {
		*val = 0;
		/* High byte comes first */
		if (data_length == 1)
			*val = data[0];
		else if (data_length == 2)
			*val = data[1] + (data[0] << 8);
		else
			*val = data[3] + (data[2] << 8) +
			    (data[1] << 16) + (data[0] << 24);
		return 0;
	}
	pltfrm_camera_module_pr_err(sd,
		"i2c read from offset 0x%08x failed with error %d\n", reg, ret);

	return ret;
}

int pltfrm_camera_module_read_reg_ex(
	struct v4l2_subdev *sd,
	u16 data_length,
	u32 flag,
	u16 reg,
	u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	struct i2c_msg msg[2];
	unsigned char data[4] = { 0, 0, 0, 0 };

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	msg->addr = client->addr;
	msg->flags = I2C_M_WR;
	msg->buf = data;

	if (PLTFRM_CAMERA_MODULE_REG_LEN(flag) == 1) {
		data[0] = (u8)(reg & 0xff);
		msg->len = 1;
	} else {
		/* High byte goes out first */
		data[0] = (u8)(reg >> 8);
		data[1] = (u8)(reg & 0xff);
		msg->len = 2;
	}

	if ((flag & PLTFRM_CAMERA_MODULE_RD_CONTINUE_MASK) ==
		PLTFRM_CAMERA_MODULE_RD_CONTINUE) {
		msg[1].addr = client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = data_length;
		msg[1].buf = data;

		ret = i2c_transfer(client->adapter, msg, 2);
	} else {
		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret >= 0) {
			mdelay(3);
			msg->flags = I2C_M_RD;
			msg->len = data_length;
			i2c_transfer(client->adapter, msg, 1);
		}
	}
	if (ret >= 0) {
		*val = 0;
		/* High byte comes first */
		if (data_length == 1)
			*val = data[0];
		else if (data_length == 2)
			*val = data[1] + (data[0] << 8);
		else
			*val = data[3] + (data[2] << 8) +
			    (data[1] << 16) + (data[0] << 24);
		return 0;
	}
	pltfrm_camera_module_pr_err(sd,
		"i2c read from offset 0x%08x failed with error %d\n", reg, ret);
	return ret;
}
/* ======================================================================== */

int pltfrm_camera_module_write_reg(
	struct v4l2_subdev *sd,
	u16 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	struct i2c_msg msg[1];
	unsigned char data[3];
	int retries;

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	for (retries = 0; retries < 5; retries++) {
		msg->addr = client->addr;
		msg->flags = I2C_M_WR;
		msg->len = 3;
		msg->buf = data;

		/* high byte goes out first */
		data[0] = (u8)(reg >> 8);
		data[1] = (u8)(reg & 0xff);
		data[2] = val;

		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
		pltfrm_camera_module_pr_debug(sd,
			"i2c write to offset 0x%08x success\n", reg);
			return 0;
		}

		pltfrm_camera_module_pr_debug(sd,
			"retrying I2C... %d\n", retries);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(20));
	}
	pltfrm_camera_module_pr_err(sd,
		"i2c write to offset 0x%08x failed with error %d\n", reg, ret);
	return ret;
}

int pltfrm_camera_module_write_reg_ex(
	struct v4l2_subdev *sd,
	u32 flag, u16 reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	struct i2c_msg msg[1];
	unsigned char data[4];
	int retries;

	if (!client->adapter) {
		pltfrm_camera_module_pr_err(sd, "client->adapter NULL\n");
		return -ENODEV;
	}

	for (retries = 0; retries < 5; retries++) {
		msg->addr = client->addr;
		msg->flags = I2C_M_WR;
		msg->buf = data;

		if (PLTFRM_CAMERA_MODULE_REG_LEN(flag) == 1 &&
			PLTFRM_CAMERA_MODULE_DATA_LEN(flag) == 1) {
			data[0] = (u8)(reg & 0xff);
			data[1] = val;
			msg->len = 2;
		} else if (PLTFRM_CAMERA_MODULE_REG_LEN(flag) == 2 &&
			PLTFRM_CAMERA_MODULE_DATA_LEN(flag) == 1) {
			data[0] = (u8)(reg >> 8);
			data[1] = (u8)(reg & 0xff);
			data[2] = val;
			msg->len = 3;
		} else if (PLTFRM_CAMERA_MODULE_REG_LEN(flag) == 1 &&
			PLTFRM_CAMERA_MODULE_DATA_LEN(flag) == 2) {
			data[0] = (u8)(reg & 0xff);
			data[1] = (u8)(val >> 8);
			data[2] = (u8)(val & 0xff);
			msg->len = 3;
		} else if (PLTFRM_CAMERA_MODULE_REG_LEN(flag) == 2 &&
			PLTFRM_CAMERA_MODULE_DATA_LEN(flag) == 2) {
			data[0] = (u8)(reg >> 8);
			data[1] = (u8)(reg & 0xff);
			data[2] = (u8)(val >> 8);
			data[3] = (u8)(val & 0xff);
			msg->len = 4;
		} else {
			pltfrm_camera_module_pr_err(sd,
				"i2c write flag 0x%x error\n", flag);
			return -EINVAL;
		}

		ret = i2c_transfer(client->adapter, msg, 1);
		if (ret == 1) {
			pltfrm_camera_module_pr_debug(sd,
				"i2c write to offset 0x%08x success\n", reg);
			return 0;
		}

		pltfrm_camera_module_pr_debug(sd,
			"retrying I2C... %d\n", retries);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(20));
	}
	pltfrm_camera_module_pr_err(sd,
		"i2c write to offset 0x%08x failed with error %d\n", reg, ret);
	return ret;
}

/* ======================================================================== */
int pltfrm_camera_module_write_reglist(
	struct v4l2_subdev *sd,
	const struct pltfrm_camera_module_reg reglist[],
	int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;
	unsigned int k = 0, j = 0;
	int i = 0;
	struct i2c_msg *msg;
	unsigned char *data;
	unsigned int max_entries = len;

	msg = kmalloc((sizeof(struct i2c_msg) * I2C_MSG_MAX),
			GFP_KERNEL);
	if (NULL == msg)
		return -ENOMEM;
	data = kmalloc((sizeof(unsigned char) * I2C_DATA_MAX),
			GFP_KERNEL);
	if (NULL == data) {
		kfree(msg);
		return -ENOMEM;
	}

	for (i = 0; i < max_entries; i++) {
		switch (reglist[i].flag & PLTFRM_CAMERA_MODULE_WR_CONTINUE_MASK) {
		case PLTFRM_CAMERA_MODULE_WR_CONTINUE:
			(msg + j)->addr = client->addr;
			(msg + j)->flags = I2C_M_WR;
			(msg + j)->buf = (data + k);

			if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 1 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 1) {
				data[k + 0] = (u8)(reglist[i].reg & 0xFF);
				data[k + 1] = (u8)(reglist[i].val & 0xFF);
				k = k + 2;
				(msg + j)->len = 2;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 2 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 1) {
				data[k + 0] = (u8)((reglist[i].reg & 0xFF00) >> 8);
				data[k + 1] = (u8)(reglist[i].reg & 0xFF);
				data[k + 2] = (u8)(reglist[i].val & 0xFF);
				k = k + 3;
				(msg + j)->len = 3;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 1 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 2) {
				data[k + 0] = (u8)(reglist[i].reg & 0xFF);
				data[k + 1] = (u8)((reglist[i].val & 0xFF00) >> 8);
				data[k + 2] = (u8)(reglist[i].val & 0xFF);
				k = k + 3;
				(msg + j)->len = 3;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 2 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 2) {
				data[k + 0] = (u8)((reglist[i].reg & 0xFF00) >> 8);
				data[k + 1] = (u8)(reglist[i].reg & 0xFF);
				data[k + 2] = (u8)((reglist[i].val & 0xFF00) >> 8);
				data[k + 3] = (u8)(reglist[i].val & 0xFF);
				k = k + 4;
				(msg + j)->len = 4;
			}

			j++;
			if (j == (I2C_MSG_MAX - 1)) {
				/* Bulk I2C transfer */
				pltfrm_camera_module_pr_debug(sd,
					"messages transfers 1 0x%p msg %d bytes %d\n",
					msg, j, k);
				ret = i2c_transfer(client->adapter, msg, j);
				if (ret < 0) {
					pltfrm_camera_module_pr_err(sd,
						"i2c transfer returned with err %d\n",
						ret);
					kfree(msg);
					kfree(data);
					return ret;
				}
				j = 0;
				k = 0;
				pltfrm_camera_module_pr_debug(sd,
					"i2c_transfer return %d\n", ret);
			}
			break;
		case PLTFRM_CAMERA_MODULE_WR_SINGLE:
			msg->addr = client->addr;
			msg->flags = I2C_M_WR;
			msg->buf = data;

			if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 1 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 1) {
				data[0] = (u8)(reglist[i].reg & 0xFF);
				data[1] = (u8)(reglist[i].val & 0xFF);
				msg->len = 2;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 2 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 1) {
				data[0] = (u8)((reglist[i].reg & 0xFF00) >> 8);
				data[1] = (u8)(reglist[i].reg & 0xFF);
				data[2] = (u8)(reglist[i].val & 0xFF);
				msg->len = 3;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 1 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 2) {
				data[0] = (u8)(reglist[i].reg & 0xFF);
				data[1] = (u8)((reglist[i].val & 0xFF00) >> 8);
				data[2] = (u8)(reglist[i].val & 0xFF);
				msg->len = 3;
			} else if (PLTFRM_CAMERA_MODULE_REG_LEN(reglist[i].flag) == 2 &&
				PLTFRM_CAMERA_MODULE_DATA_LEN(reglist[i].flag) == 2) {
				data[0] = (u8)((reglist[i].reg & 0xFF00) >> 8);
				data[1] = (u8)(reglist[i].reg & 0xFF);
				data[2] = (u8)((reglist[i].val & 0xFF00) >> 8);
				data[3] = (u8)(reglist[i].val & 0xFF);
				msg->len = 4;
			}

			pltfrm_camera_module_pr_debug(sd,
				"messages transfers 1 0x%p msg\n", msg);
			ret = i2c_transfer(client->adapter, msg, 1);
			if (ret < 0) {
				pltfrm_camera_module_pr_err(sd,
					"i2c transfer returned with err %d\n",
					ret);
				kfree(msg);
				kfree(data);
				return ret;
			}
			break;
		case PLTFRM_CAMERA_MODULE_REG_TYPE_TIMEOUT:
			if (j > 0) {
				/* Bulk I2C transfer */
				pltfrm_camera_module_pr_debug(sd,
					"messages transfers 1 0x%p msg %d bytes %d\n",
					msg, j, k);
				ret = i2c_transfer(client->adapter, msg, j);
				if (ret < 0) {
					pltfrm_camera_module_pr_debug(sd,
						"i2c transfer returned with err %d\n",
						ret);
					kfree(msg);
					kfree(data);
					return ret;
				}
				pltfrm_camera_module_pr_debug(sd,
					"i2c_transfer return %d\n", ret);
			}
			mdelay(reglist[i].val);
			j = 0;
			k = 0;
			break;
		default:
			pltfrm_camera_module_pr_debug(sd, "unknown command\n");
			kfree(msg);
			kfree(data);
			return -1;
		}
	}

	if (j != 0) {	   /*Remaining I2C message*/
		pltfrm_camera_module_pr_debug(sd,
			"messages transfers 1 0x%p msg %d bytes %d\n",
			msg, j, k);
		ret = i2c_transfer(client->adapter, msg, j);
		if (ret < 0) {
			pltfrm_camera_module_pr_err(sd,
				"i2c transfer returned with err %d\n", ret);
			kfree(msg);
			kfree(data);
			return ret;
		}
		pltfrm_camera_module_pr_debug(sd,
			"i2c_transfer return %d\n", ret);
	}

	kfree(msg);
	kfree(data);
	return 0;
}

static int pltfrm_camera_module_init_pm(
	struct v4l2_subdev *sd,
	struct pltfrm_soc_cfg *soc_cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);

	pdata->soc_cfg = soc_cfg;
	return 0;
}

int pltfrm_camera_module_set_pm_state(
	struct v4l2_subdev *sd,
	int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	struct pltfrm_soc_cfg *soc_cfg =
		pdata->soc_cfg;
	struct pltfrm_soc_mclk_para mclk_para;
	struct pltfrm_soc_cfg_para cfg_para;
	struct pltfrm_cam_itf itf_cfg;
	unsigned int i;

	if (on) {
		if (IS_ERR_OR_NULL(soc_cfg)) {
			pltfrm_camera_module_pr_err(sd,
				"set_pm_state failed! soc_cfg is %p!\n",
				soc_cfg);
			return -EINVAL;
		}

		if (pdata->regulators.regulator) {
			for (i = 0; i < pdata->regulators.cnt; i++) {
				struct pltfrm_camera_module_regulator
							*regulator;

				regulator = pdata->regulators.regulator + i;
				if (IS_ERR(regulator->regulator))
					continue;
				regulator_set_voltage(
					regulator->regulator,
					regulator->min_uV,
					regulator->max_uV);
				if (regulator_enable(regulator->regulator))
					pltfrm_camera_module_pr_err(sd,
						"regulator_enable failed!\n");
			}
		}

		pltfrm_camera_module_set_pin_state(
			sd,
			PLTFRM_CAMERA_MODULE_PIN_PWR,
			PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE);

		pltfrm_camera_module_set_pin_state(
			sd,
			PLTFRM_CAMERA_MODULE_PIN_RESET,
			PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE);
		usleep_range(100, 300);
		pltfrm_camera_module_set_pin_state(
			sd,
			PLTFRM_CAMERA_MODULE_PIN_RESET,
			PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE);

		mclk_para.io_voltage = PLTFRM_IO_1V8;
		mclk_para.drv_strength = PLTFRM_DRV_STRENGTH_2;
		cfg_para.cmd = PLTFRM_MCLK_CFG;
		cfg_para.cfg_para = (void *)&mclk_para;
		cfg_para.isp_config = &(soc_cfg->isp_config);
		soc_cfg->soc_cfg(&cfg_para);

		if (v4l2_subdev_call(sd,
			core,
			ioctl,
			PLTFRM_CIFCAM_G_ITF_CFG,
			(void *)&itf_cfg) == 0) {
			clk_set_rate(pdata->mclk, itf_cfg.mclk_hz);
		} else {
			pltfrm_camera_module_pr_err(sd,
				"PLTFRM_CIFCAM_G_ITF_CFG failed, mclk set 24m default.\n");
			clk_set_rate(pdata->mclk, 24000000);
		}
		clk_prepare_enable(pdata->mclk);
	} else {
		clk_disable_unprepare(pdata->mclk);

		pltfrm_camera_module_set_pin_state(
			sd,
			PLTFRM_CAMERA_MODULE_PIN_PWR,
			PLTFRM_CAMERA_MODULE_PIN_STATE_INACTIVE);

		if (pdata->regulators.regulator) {
			for (i = 0; i < pdata->regulators.cnt; i++) {
				struct pltfrm_camera_module_regulator
							*regulator;

				regulator = pdata->regulators.regulator + i;
				if (IS_ERR(regulator->regulator))
					continue;
				regulator_disable(regulator->regulator);
			}
		}
	}

	return 0;
}

int pltfrm_camera_module_set_pin_state(
	struct v4l2_subdev *sd,
	const char *pin,
	enum pltfrm_camera_module_pin_state state)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int gpio_val;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata->gpios); i++) {
		if (pin == pdata->gpios[i].label) {
			if (!gpio_is_valid(pdata->gpios[i].pltfrm_gpio))
				return 0;
			if (state == PLTFRM_CAMERA_MODULE_PIN_STATE_ACTIVE)
				gpio_val = (pdata->gpios[i].active_low ==
					OF_GPIO_ACTIVE_LOW) ? 0 : 1;
			else
				gpio_val = (pdata->gpios[i].active_low ==
					OF_GPIO_ACTIVE_LOW) ? 1 : 0;
			gpio_set_value(pdata->gpios[i].pltfrm_gpio, gpio_val);
			pltfrm_camera_module_pr_debug(sd,
				"set GPIO #%d ('%s') to %s\n",
				pdata->gpios[i].pltfrm_gpio,
				pdata->gpios[i].label,
				gpio_val ? "HIGH" : "LOW");

			return 0;
		}
	}

	pltfrm_camera_module_pr_err(sd,
		"unknown pin '%s'\n",
		pin);
	return -EINVAL;
}

int pltfrm_camera_module_get_pin_state(
	struct v4l2_subdev *sd,
	const char *pin)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int gpio_val;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdata->gpios); i++) {
		if (pin == pdata->gpios[i].label) {
			if (!gpio_is_valid(pdata->gpios[i].pltfrm_gpio))
				return 0;
			gpio_val = gpio_get_value(pdata->gpios[i].pltfrm_gpio);
			pltfrm_camera_module_pr_debug(
				sd,
				"get GPIO #%d ('%s') is %s\n",
				pdata->gpios[i].pltfrm_gpio,
				pdata->gpios[i].label,
				gpio_val ? "HIGH" : "LOW");

			return gpio_val;
		}
	}

	pltfrm_camera_module_pr_err(
		sd,
		"unknown pin '%s'\n",
		pin);
	return -EINVAL;
}

int pltfrm_camera_module_s_power(
	struct v4l2_subdev *sd,
	int on)
{
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);

	pltfrm_camera_module_pr_debug(sd, "%s\n", on ? "on" : "off");

	if (on) {
		/* Enable clock and voltage to Secondary Camera Sensor */
		ret = pltfrm_camera_module_set_pm_state(sd, on);
		if (IS_ERR_VALUE(ret))
			pltfrm_camera_module_pr_err(sd,
				"set PM state failed (%d), could not power on camera\n",
				ret);
		else {
			pltfrm_camera_module_pr_debug(sd,
				"set PM state to %d successful, camera module is on\n",
				on);
			ret = pltfrm_camera_module_set_pinctrl_state(
				sd, pdata->pins_default);
		}
	} else {
		/* Disable clock and voltage to Secondary Camera Sensor  */
		ret = pltfrm_camera_module_set_pinctrl_state(
			sd, pdata->pins_sleep);
		if (!IS_ERR_VALUE(ret)) {
			ret = pltfrm_camera_module_set_pm_state(
				sd, on);
			if (IS_ERR_VALUE(ret))
				pltfrm_camera_module_pr_err(sd,
					"set PM state failed (%d), could not power off camera\n",
					ret);
			else
				pltfrm_camera_module_pr_debug(sd,
					"set PM state to %d successful, camera module is off\n",
					on);
		}
	}
	return ret;
}

struct v4l2_subdev *pltfrm_camera_module_get_af_ctrl(
	struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);

	return pdata->af_ctrl;
}

char *pltfrm_camera_module_get_flash_driver_name(
	struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);

	return (char *)pdata->fl_ctrl.flash_driver_name;
}

struct v4l2_subdev *pltfrm_camera_module_get_fl_ctrl(
	struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);

	return pdata->fl_ctrl.flsh_ctrl;
}

int pltfrm_camera_module_patch_config(
	struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *frm_fmt,
	struct v4l2_subdev_frame_interval *frm_intrvl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device_node *parent_node = of_node_get(client->dev.of_node);
	struct device_node *child_node = NULL, *prev_node = NULL;
	int ret = 0;

	pltfrm_camera_module_pr_debug(sd, "pix_fmt %d, %dx%d@%d/%dfps\n",
		frm_fmt->code, frm_fmt->width, frm_fmt->height,
		frm_intrvl->interval.denominator,
		frm_intrvl->interval.numerator);

	while (!IS_ERR_OR_NULL(child_node =
		of_get_next_child(parent_node, prev_node))) {
		if (strncasecmp(child_node->name,
			"rockchip,camera-module-config",
			strlen("rockchip,camera-module-config")) == 0) {
			ret = pltfrm_camera_module_config_matches(
				sd, child_node, frm_fmt, frm_intrvl);
			if (IS_ERR_VALUE(ret))
				goto err;
			if (ret) {
				ret =
					pltfrm_camera_module_write_reglist_node(
						sd, child_node);
				if (!IS_ERR_VALUE(ret))
					goto err;
			}
		}
		of_node_put(prev_node);
		prev_node = child_node;
	}
	of_node_put(prev_node);
	of_node_put(parent_node);

	return 0;
err:
	pltfrm_camera_module_pr_err(sd,
			"failed with error %d\n", ret);
	of_node_put(prev_node);
	of_node_put(child_node);
	of_node_put(parent_node);
	return ret;
}

int pltfrm_camera_module_init(
	struct v4l2_subdev *sd,
	void **pldata)
{
	int ret = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata;

	pltfrm_camera_module_pr_debug(sd, "\n");

	pdata = pltfrm_camera_module_get_data(sd);
	if (IS_ERR_OR_NULL(pdata)) {
		pltfrm_camera_module_pr_err(sd,
			"unable to get platform data\n");
		return -EFAULT;
	}

	ret = pltfrm_camera_module_init_gpio(sd);
	if (IS_ERR_VALUE(ret)) {
		pltfrm_camera_module_pr_err(sd,
			"GPIO initialization failed (%d)\n", ret);
	}

	if (IS_ERR_VALUE(ret))
		devm_kfree(&client->dev, pdata);
	else
		*(struct pltfrm_camera_module_data **)pldata = pdata;

	return ret;
}

void pltfrm_camera_module_release(
	struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int i;

	/* GPIOs also needs to be freed for other sensors to use */
	for (i = 0; i < ARRAY_SIZE(pdata->gpios); i++) {
		if (gpio_is_valid(pdata->gpios[i].pltfrm_gpio)) {
			pltfrm_camera_module_pr_debug(sd,
				"free GPIO #%d ('%s')\n",
				pdata->gpios[i].pltfrm_gpio,
				pdata->gpios[i].label);
			gpio_free(
				pdata->gpios[i].pltfrm_gpio);
		}
	}
	for (i = 0; i < pdata->regulators.cnt; i++) {
		if (!IS_ERR(pdata->regulators.regulator[i].regulator))
			devm_regulator_put(
				pdata->regulators.regulator[i].regulator);
	}
	if (pdata->pinctrl)
		devm_pinctrl_put(pdata->pinctrl);
}

/* ======================================================================== */
long pltfrm_camera_module_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd,
	void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
		dev_get_platdata(&client->dev);
	int ret = 0;

	pltfrm_camera_module_pr_debug(sd, "cmd: 0x%x\n", cmd);

	if (cmd == RK_VIDIOC_CAMERA_MODULEINFO) {
		struct camera_module_info_s *p_camera_module =
		(struct camera_module_info_s *)arg;

		/*
		 * strlcpy((char *)p_camera_module->sensor_name,
		 * (char *)client->driver->driver.name,
		 * sizeof(p_camera_module->sensor_name));
		 */

		if (pdata->info.module_name)
			strcpy((char *)p_camera_module->module_name,
				pdata->info.module_name);
		else
			strcpy((char *)p_camera_module->module_name, "(null)");
		if (pdata->info.len_name)
			strcpy((char *)p_camera_module->len_name,
				pdata->info.len_name);
		else
			strcpy((char *)p_camera_module->len_name, "(null)");
		if (pdata->info.fov_h)
			strcpy((char *)p_camera_module->fov_h,
				pdata->info.fov_h);
		else
			strcpy((char *)p_camera_module->fov_h, "(null)");
		if (pdata->info.fov_v)
			strcpy((char *)p_camera_module->fov_v,
				pdata->info.fov_v);
		else
			strcpy((char *)p_camera_module->fov_v, "(null)");
		if (pdata->info.focal_length)
			strcpy((char *)p_camera_module->focal_length,
			pdata->info.focal_length);
		else
			strcpy((char *)p_camera_module->focal_length, "(null)");
		if (pdata->info.focus_distance)
			strcpy((char *)p_camera_module->focus_distance,
				pdata->info.focus_distance);
		else
			strcpy((char *)p_camera_module->focus_distance,
				"(null)");

		p_camera_module->facing = pdata->info.facing;
		p_camera_module->orientation = pdata->info.orientation;
		p_camera_module->iq_mirror = pdata->info.iq_mirror;
		p_camera_module->iq_flip = pdata->info.iq_flip;
		p_camera_module->flash_support = pdata->info.flash_support;
		p_camera_module->flash_exp_percent =
			pdata->info.flash_exp_percent;

		return 0;
	} else if (cmd == PLTFRM_CIFCAM_G_DEFRECT) {
		struct pltfrm_cam_defrect *defrect =
			(struct pltfrm_cam_defrect *)arg;
		unsigned int i;

		for (i = 0; i < 4; i++) {
			if ((pdata->defrects[i].width == defrect->width) &&
				(pdata->defrects[i].height == defrect->height))
				defrect->defrect = pdata->defrects[i].defrect;
		}
		return 0;
	} else if (cmd == PLTFRM_CIFCAM_G_ITF_CFG) {
		struct pltfrm_cam_itf *itf_cfg = (struct pltfrm_cam_itf *)arg;

		if (PLTFRM_CAM_ITF_IS_MIPI(itf_cfg->type))
			itf_cfg->cfg.mipi.dphy_index =
				pdata->itf.itf.mipi.dphy_index;

		return 0;
	} else if (cmd == PLTFRM_CIFCAM_ATTACH) {
		return pltfrm_camera_module_init_pm(sd,
			(struct pltfrm_soc_cfg *)arg);
	}

	return ret;
}

int pltfrm_camera_module_get_flip_mirror(
	struct v4l2_subdev *sd)
{
	int mode = 0;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct pltfrm_camera_module_data *pdata =
	dev_get_platdata(&client->dev);

	if ((pdata->info.flip == -1) && pdata->info.mirror == -1)
		return -1;

	if (pdata->info.flip)
		mode |= 0x02;
	else
		mode &= ~0x02;

	if (pdata->info.mirror)
		mode |= 0x01;
	else
		mode &= ~0x01;

	return mode;
}
#endif

