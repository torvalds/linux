/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drm.h>
#include <drm/drmP.h>
#include <drm/drm_panel.h>

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <uapi/drm/rockchip_drm.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_backlight.h"

struct sub_backlight {
	struct pwm_device *pwm;
	struct pinctrl *pinctrl;
	struct pinctrl_state *dummy_state;
	struct pinctrl_state *active_state;
	struct drm_crtc *crtc;
	struct device *dev;

	const struct rockchip_sub_backlight_ops *ops;
	struct list_head list;
};

#define to_rockchip_backlight_device(x) \
		container_of((x), struct rockchip_drm_backlight, pwm_pdev)

static DEFINE_MUTEX(backlight_lock);
static LIST_HEAD(backlight_list);

static int compute_duty_cycle(struct rockchip_drm_backlight *bl,
			      int brightness, int period)
{
	unsigned int lth = bl->lth_brightness;
	int duty_cycle;

	duty_cycle = bl->levels[brightness];

	return (duty_cycle * (period - lth) / bl->scale) + lth;
}

static void rockchip_pwm_power_on(struct rockchip_drm_backlight *bl,
				  struct pwm_device *pwm, int brightness)
{
	struct pwm_args pargs;
	int duty_cycle;

	pwm_get_args(pwm, &pargs);
	duty_cycle = compute_duty_cycle(bl, brightness, pargs.period);
	pwm_config(pwm, duty_cycle, pargs.period);
	pwm_enable(pwm);
}

static void rockchip_pwm_power_off(struct rockchip_drm_backlight *bl,
				   struct pwm_device *pwm)
{
	struct pwm_args pargs;
	struct pwm_state state;

	pwm_get_state(pwm, &state);
	if (!state.enabled)
		return;

	pwm_get_args(pwm, &pargs);
	pwm_config(pwm, 0, pargs.period);
	pwm_disable(pwm);
}

static void rockchip_backlight_power_on(struct rockchip_drm_backlight *bl)
{
	int err;

	if (bl->enabled)
		return;

	err = regulator_enable(bl->power_supply);
	if (err < 0)
		dev_err(bl->dev, "failed to enable power supply\n");

	if (bl->enable_gpio)
		gpiod_set_value(bl->enable_gpio, 1);

	bl->enabled = true;
}

static void rockchip_backlight_power_off(struct rockchip_drm_backlight *bl)
{
	if (!bl->enabled)
		return;

	if (bl->enable_gpio)
		gpiod_set_value(bl->enable_gpio, 0);

	regulator_disable(bl->power_supply);
	bl->enabled = false;
}

static int backlight_parse_dt(struct rockchip_drm_backlight *bl)
{
	struct device_node *node = bl->dev->of_node;
	struct device *dev = bl->dev;
	struct property *prop;
	size_t size;
	int length;
	u32 value;
	int ret, i;

	if (!node)
		return -ENODEV;

	bl->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(bl->pwm)) {
		dev_err(dev, "unable to request PWM: %ld\n",
			PTR_ERR(bl->pwm));
		return PTR_ERR(bl->pwm);
	}

	if (!bl->pwm->chip->dev->pins) {
		dev_err(dev, "failed to find pwm pinctrl\n");
		return -ENODEV;
	}
	bl->dummy_state = pinctrl_lookup_state(bl->pwm->chip->dev->pins->p,
					       "dummy");
	if (IS_ERR_OR_NULL(bl->dummy_state)) {
		dev_err(dev, "failed to find pwm dummy state\n");
		return -ENODEV;
	}

	bl->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(bl->enable_gpio)) {
		dev_err(dev, "unable to request enable gpio: %ld\n",
			PTR_ERR(bl->enable_gpio));
		return PTR_ERR(bl->enable_gpio);
	}

	bl->power_supply = devm_regulator_get(dev, "power");
	if (IS_ERR(bl->power_supply)) {
		dev_err(dev, "unable to request power supply: %ld\n",
			PTR_ERR(bl->power_supply));
		return PTR_ERR(bl->power_supply);
	}

	/* determine the number of brightness levels */
	prop = of_find_property(node, "brightness-levels", &length);
	if (!prop)
		return -EINVAL;

	bl->max_brightness = length / sizeof(u32);

	if (bl->max_brightness <= 0)
		return -EINVAL;

	/* read brightness levels from DT property */
	size = sizeof(*bl->levels) * bl->max_brightness;

	bl->levels = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!bl->levels)
		return -ENOMEM;

	ret = of_property_read_u32_array(node, "brightness-levels",
					 bl->levels,
					 bl->max_brightness);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(node, "default-brightness-level", &value);
	if (ret < 0)
		return ret;

	bl->dft_brightness = value;
	bl->max_brightness--;

	for (i = 0; i <= bl->max_brightness; i++)
		if (bl->levels[i] > bl->scale)
			bl->scale = bl->levels[i];

	return 0;
}

void rockchip_drm_backlight_update(struct drm_device *drm)
{
	struct rockchip_drm_private *private = drm->dev_private;
	struct rockchip_drm_backlight *bl = private->backlight;
	struct drm_connector *connector;
	struct sub_backlight *sub;
	struct rockchip_crtc_state *s;
	struct drm_crtc *crtc;
	bool backlight_changed = false;

	if (!bl || !bl->connector)
		return;

	sub = bl->sub;
	connector = bl->connector;
	crtc = connector->state->crtc;
	if (!crtc) {
		if (sub) {
			bl->sub = NULL;
			backlight_changed = true;
		}
	} else if (!sub || sub->dev->of_node != crtc->port) {
		s = to_rockchip_crtc_state(crtc->state);
		if (s->cabc_mode != ROCKCHIP_DRM_CABC_MODE_DISABLE) {
			list_for_each_entry(sub, &backlight_list, list) {
				if (sub->crtc == crtc) {
					bl->sub = sub;
					backlight_changed = true;
					break;
				}
			}
		} else if (bl->sub) {
			bl->sub = NULL;
			backlight_changed = true;
		}
	}

	if (backlight_changed)
		backlight_update_status(bl->bd);
}
EXPORT_SYMBOL(rockchip_drm_backlight_update);

int of_rockchip_drm_sub_backlight_register(struct device *dev,
				struct drm_crtc *crtc,
				const struct rockchip_sub_backlight_ops *ops)
{
	struct sub_backlight *sub;
	struct pwm_device *pwm;

	pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(pwm)) {
		dev_err(dev, "unable to request PWM\n");
		return PTR_ERR(pwm);
	}

	sub = devm_kzalloc(dev, sizeof(*sub), GFP_KERNEL);
	if (!sub)
		return -ENOMEM;

	sub->pinctrl = devm_pinctrl_get(pwm->chip->dev);
	if (IS_ERR(sub->pinctrl)) {
		dev_err(dev, "failed to find pwm pinctrl\n");
		return PTR_ERR(sub->pinctrl);
	}

	sub->dummy_state = pinctrl_lookup_state(sub->pinctrl, "dummy");
	if (IS_ERR_OR_NULL(sub->dummy_state)) {
		dev_err(dev, "failed to find pwm dummy state\n");
		return -ENODEV;
	}

	sub->active_state = pinctrl_lookup_state(sub->pinctrl, "active");
	if (IS_ERR_OR_NULL(sub->active_state)) {
		dev_err(dev, "failed to find pwm active state\n");
		return -ENODEV;
	}

	pwm_adjust_config(pwm);

	sub->pwm = pwm;
	sub->dev = dev;
	sub->crtc = crtc;
	sub->ops = ops;

	INIT_LIST_HEAD(&sub->list);
	list_add_tail(&sub->list, &backlight_list);

	return 0;
}
EXPORT_SYMBOL(of_rockchip_drm_sub_backlight_register);

static int rockchip_drm_backlight_bind(struct device *dev,
				       struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_drm_backlight *bl = dev_get_drvdata(dev);
	struct drm_connector *connector;
	struct drm_panel *panel;
	struct device_node *backlight_np;

	private->backlight = bl;

	mutex_lock(&drm_dev->mode_config.mutex);

	drm_for_each_connector(connector, drm_dev) {
		panel = drm_find_panel_by_connector(connector);
		if (!panel && !panel->dev)
			continue;
		backlight_np = of_parse_phandle(panel->dev->of_node,
						"backlight", 0);
		if (backlight_np == dev->of_node) {
			bl->connector = connector;
			break;
		}
	}

	mutex_unlock(&drm_dev->mode_config.mutex);

	if (!bl->connector) {
		dev_warn(dev, "failed to bind drm backlight\n");
		return -ENODEV;
	}

	return 0;
}

static void rockchip_drm_backlight_unbind(struct device *dev,
					  struct device *master, void *data)
{
	struct drm_device *drm_dev = data;
	struct rockchip_drm_private *private = drm_dev->dev_private;
	struct rockchip_drm_backlight *bl = dev_get_drvdata(dev);

	private->backlight = NULL;
	bl->connector = NULL;
}

static const struct component_ops rockchip_drm_backlight_component_ops = {
	.bind = rockchip_drm_backlight_bind,
	.unbind = rockchip_drm_backlight_unbind,
};

static int
rockchip_drm_backlight_update_status(struct backlight_device *backlight)
{
	int brightness = backlight->props.brightness;
	struct rockchip_drm_backlight *bl = bl_get_data(backlight);
	struct sub_backlight *sub = bl->sub;
	struct sub_backlight *current_sub = bl->current_sub;
	bool async;

	if (backlight->props.power != FB_BLANK_UNBLANK ||
	    backlight->props.fb_blank != FB_BLANK_UNBLANK ||
	    backlight->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	if (!sub || brightness <= 0) {
		if (current_sub)
			pinctrl_select_state(current_sub->pinctrl,
					     current_sub->dummy_state);

		if (brightness > 0) {
			rockchip_pwm_power_on(bl, bl->pwm, brightness);
			rockchip_backlight_power_on(bl);
		} else {
			rockchip_backlight_power_off(bl);
			rockchip_pwm_power_off(bl, bl->pwm);
		}

		pinctrl_pm_select_default_state(bl->pwm->chip->dev);
		if (current_sub) {
			rockchip_pwm_power_off(bl, current_sub->pwm);

			if (current_sub->ops->config_done)
				current_sub->ops->config_done(current_sub->dev,
							      true);
		}

		return 0;
	}

	pinctrl_select_state(bl->pwm->chip->dev->pins->p, bl->dummy_state);

	async = !!current_sub;
	if (current_sub && sub != current_sub) {
		pinctrl_select_state(current_sub->pinctrl,
				     current_sub->dummy_state);
		async = false;
	}

	rockchip_pwm_power_on(bl, sub->pwm, brightness);

	if (current_sub && sub != current_sub) {
		rockchip_pwm_power_on(bl, current_sub->pwm, brightness);
		if (current_sub->ops->config_done)
			current_sub->ops->config_done(current_sub->dev, true);
	}

	if (sub->ops->config_done)
		sub->ops->config_done(sub->dev, async);

	pinctrl_select_state(sub->pinctrl, sub->active_state);

	rockchip_backlight_power_on(bl);

	bl->current_sub = sub;

	return 0;
}

static const struct backlight_ops rockchip_drm_backlight_ops = {
	.update_status = rockchip_drm_backlight_update_status,
};

static int rockchip_drm_backlight_probe(struct platform_device *pdev)
{
	struct rockchip_drm_backlight *bl;
	struct backlight_properties props;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	int initial_blank = FB_BLANK_UNBLANK;
	int ret;

	bl = devm_kzalloc(dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	bl->dev = dev;
	ret = backlight_parse_dt(bl);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to find parse backlight dts\n");
		return ret;
	}

	bl->enabled = false;

	if (bl->enable_gpio) {
		/*
		 * If the driver is probed from the device tree and there is a
		 * phandle link pointing to the backlight node, it is safe to
		 * assume that another driver will enable the backlight at the
		 * appropriate time. Therefore, if it is disabled, keep it so.
		 */
		if (node && node->phandle &&
		    gpiod_get_direction(bl->enable_gpio) == GPIOF_DIR_OUT &&
		    gpiod_get_value(bl->enable_gpio) == 0)
			initial_blank = FB_BLANK_POWERDOWN;
		else
			gpiod_direction_output(bl->enable_gpio, 1);
	}

	if (node && node->phandle && !regulator_is_enabled(bl->power_supply))
		initial_blank = FB_BLANK_POWERDOWN;

	pwm_adjust_config(bl->pwm);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = bl->max_brightness;
	bl->bd = backlight_device_register(dev_name(dev), dev, bl,
					   &rockchip_drm_backlight_ops, &props);
	if (IS_ERR(bl->bd)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		ret = PTR_ERR(bl->bd);
		return ret;
	}

	bl->bd->props.brightness = bl->dft_brightness;
	bl->bd->props.power = initial_blank;
	backlight_update_status(bl->bd);

	platform_set_drvdata(pdev, bl);

	ret = component_add(dev, &rockchip_drm_backlight_component_ops);
	if (ret)
		backlight_device_unregister(bl->bd);

	return ret;
}

static int rockchip_drm_backlight_remove(struct platform_device *pdev)
{
	struct rockchip_drm_backlight *bl = platform_get_drvdata(pdev);

	backlight_device_unregister(bl->bd);
	component_del(&pdev->dev, &rockchip_drm_backlight_component_ops);

	return 0;
}

static const struct of_device_id rockchip_drm_backlight_dt_ids[] = {
	{.compatible = "rockchip,drm-backlight",
	 .data = NULL },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_drm_backlight_dt_ids);

static struct platform_driver rockchip_drm_backlight_driver = {
	.probe = rockchip_drm_backlight_probe,
	.remove = rockchip_drm_backlight_remove,
	.driver = {
		   .name = "drm-backlight",
		   .of_match_table =
			of_match_ptr(rockchip_drm_backlight_dt_ids),
	},
};

module_platform_driver(rockchip_drm_backlight_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Drm Backlight Driver");
MODULE_LICENSE("GPL v2");
