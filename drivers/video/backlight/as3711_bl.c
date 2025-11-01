// SPDX-License-Identifier: GPL-2.0
/*
 * AS3711 PMIC backlight driver, using DCDC Step Up Converters
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 * Author: Guennadi Liakhovetski, <g.liakhovetski@gmx.de>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/as3711.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

enum as3711_bl_type {
	AS3711_BL_SU1,
	AS3711_BL_SU2,
};

struct as3711_bl_data {
	bool powered;
	enum as3711_bl_type type;
	int brightness;
	struct backlight_device *bl;
};

struct as3711_bl_supply {
	struct as3711_bl_data su1;
	struct as3711_bl_data su2;
	const struct as3711_bl_pdata *pdata;
	struct as3711 *as3711;
};

static struct as3711_bl_supply *to_supply(struct as3711_bl_data *su)
{
	switch (su->type) {
	case AS3711_BL_SU1:
		return container_of(su, struct as3711_bl_supply, su1);
	case AS3711_BL_SU2:
		return container_of(su, struct as3711_bl_supply, su2);
	}
	return NULL;
}

static int as3711_set_brightness_auto_i(struct as3711_bl_data *data,
					unsigned int brightness)
{
	struct as3711_bl_supply *supply = to_supply(data);
	struct as3711 *as3711 = supply->as3711;
	const struct as3711_bl_pdata *pdata = supply->pdata;
	int ret = 0;

	/* Only all equal current values are supported */
	if (pdata->su2_auto_curr1)
		ret = regmap_write(as3711->regmap, AS3711_CURR1_VALUE,
				   brightness);
	if (!ret && pdata->su2_auto_curr2)
		ret = regmap_write(as3711->regmap, AS3711_CURR2_VALUE,
				   brightness);
	if (!ret && pdata->su2_auto_curr3)
		ret = regmap_write(as3711->regmap, AS3711_CURR3_VALUE,
				   brightness);

	return ret;
}

static int as3711_set_brightness_v(struct as3711 *as3711,
				   unsigned int brightness,
				   unsigned int reg)
{
	if (brightness > 31)
		return -EINVAL;

	return regmap_update_bits(as3711->regmap, reg, 0xf0,
				  brightness << 4);
}

static int as3711_bl_su2_reset(struct as3711_bl_supply *supply)
{
	struct as3711 *as3711 = supply->as3711;
	int ret = regmap_update_bits(as3711->regmap, AS3711_STEPUP_CONTROL_5,
				     3, supply->pdata->su2_fbprot);
	if (!ret)
		ret = regmap_update_bits(as3711->regmap,
					 AS3711_STEPUP_CONTROL_2, 1, 0);
	if (!ret)
		ret = regmap_update_bits(as3711->regmap,
					 AS3711_STEPUP_CONTROL_2, 1, 1);
	return ret;
}

/*
 * Someone with less fragile or less expensive hardware could try to simplify
 * the brightness adjustment procedure.
 */
static int as3711_bl_update_status(struct backlight_device *bl)
{
	struct as3711_bl_data *data = bl_get_data(bl);
	struct as3711_bl_supply *supply = to_supply(data);
	struct as3711 *as3711 = supply->as3711;
	int brightness;
	int ret = 0;

	brightness = backlight_get_brightness(bl);

	if (data->type == AS3711_BL_SU1) {
		ret = as3711_set_brightness_v(as3711, brightness,
					      AS3711_STEPUP_CONTROL_1);
	} else {
		const struct as3711_bl_pdata *pdata = supply->pdata;

		switch (pdata->su2_feedback) {
		case AS3711_SU2_VOLTAGE:
			ret = as3711_set_brightness_v(as3711, brightness,
						      AS3711_STEPUP_CONTROL_2);
			break;
		case AS3711_SU2_CURR_AUTO:
			ret = as3711_set_brightness_auto_i(data, brightness / 4);
			if (ret < 0)
				return ret;
			if (brightness) {
				ret = as3711_bl_su2_reset(supply);
				if (ret < 0)
					return ret;
				udelay(500);
				ret = as3711_set_brightness_auto_i(data, brightness);
			} else {
				ret = regmap_update_bits(as3711->regmap,
						AS3711_STEPUP_CONTROL_2, 1, 0);
			}
			break;
		/* Manual one current feedback pin below */
		case AS3711_SU2_CURR1:
			ret = regmap_write(as3711->regmap, AS3711_CURR1_VALUE,
					   brightness);
			break;
		case AS3711_SU2_CURR2:
			ret = regmap_write(as3711->regmap, AS3711_CURR2_VALUE,
					   brightness);
			break;
		case AS3711_SU2_CURR3:
			ret = regmap_write(as3711->regmap, AS3711_CURR3_VALUE,
					   brightness);
			break;
		default:
			ret = -EINVAL;
		}
	}
	if (!ret)
		data->brightness = brightness;

	return ret;
}

static int as3711_bl_get_brightness(struct backlight_device *bl)
{
	struct as3711_bl_data *data = bl_get_data(bl);

	return data->brightness;
}

static const struct backlight_ops as3711_bl_ops = {
	.update_status	= as3711_bl_update_status,
	.get_brightness	= as3711_bl_get_brightness,
};

static int as3711_bl_init_su2(struct as3711_bl_supply *supply)
{
	struct as3711 *as3711 = supply->as3711;
	const struct as3711_bl_pdata *pdata = supply->pdata;
	u8 ctl = 0;
	int ret;

	dev_dbg(as3711->dev, "%s(): use %u\n", __func__, pdata->su2_feedback);

	/* Turn SU2 off */
	ret = regmap_write(as3711->regmap, AS3711_STEPUP_CONTROL_2, 0);
	if (ret < 0)
		return ret;

	switch (pdata->su2_feedback) {
	case AS3711_SU2_VOLTAGE:
		ret = regmap_update_bits(as3711->regmap, AS3711_STEPUP_CONTROL_4, 3, 0);
		break;
	case AS3711_SU2_CURR1:
		ctl = 1;
		ret = regmap_update_bits(as3711->regmap, AS3711_STEPUP_CONTROL_4, 3, 1);
		break;
	case AS3711_SU2_CURR2:
		ctl = 4;
		ret = regmap_update_bits(as3711->regmap, AS3711_STEPUP_CONTROL_4, 3, 2);
		break;
	case AS3711_SU2_CURR3:
		ctl = 0x10;
		ret = regmap_update_bits(as3711->regmap, AS3711_STEPUP_CONTROL_4, 3, 3);
		break;
	case AS3711_SU2_CURR_AUTO:
		if (pdata->su2_auto_curr1)
			ctl = 2;
		if (pdata->su2_auto_curr2)
			ctl |= 8;
		if (pdata->su2_auto_curr3)
			ctl |= 0x20;
		ret = 0;
		break;
	default:
		return -EINVAL;
	}

	if (!ret)
		ret = regmap_write(as3711->regmap, AS3711_CURR_CONTROL, ctl);

	return ret;
}

static int as3711_bl_register(struct platform_device *pdev,
			      unsigned int max_brightness, struct as3711_bl_data *su)
{
	struct backlight_properties props = {.type = BACKLIGHT_RAW,};
	struct backlight_device *bl;

	/* max tuning I = 31uA for voltage- and 38250uA for current-feedback */
	props.max_brightness = max_brightness;

	bl = devm_backlight_device_register(&pdev->dev,
				       su->type == AS3711_BL_SU1 ?
				       "as3711-su1" : "as3711-su2",
				       &pdev->dev, su,
				       &as3711_bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = props.max_brightness;

	backlight_update_status(bl);

	su->bl = bl;

	return 0;
}

static int as3711_backlight_parse_dt(struct device *dev)
{
	struct as3711_bl_pdata *pdata = dev_get_platdata(dev);
	struct device_node *bl, *fb;
	int ret;

	bl = of_get_child_by_name(dev->parent->of_node, "backlight");
	if (!bl) {
		dev_dbg(dev, "backlight node not found\n");
		return -ENODEV;
	}

	fb = of_parse_phandle(bl, "su1-dev", 0);
	if (fb) {
		of_node_put(fb);

		pdata->su1_fb = true;

		ret = of_property_read_u32(bl, "su1-max-uA", &pdata->su1_max_uA);
		if (pdata->su1_max_uA <= 0)
			ret = -EINVAL;
		if (ret < 0)
			goto err_put_bl;
	}

	fb = of_parse_phandle(bl, "su2-dev", 0);
	if (fb) {
		int count = 0;

		of_node_put(fb);

		pdata->su2_fb = true;

		ret = of_property_read_u32(bl, "su2-max-uA", &pdata->su2_max_uA);
		if (pdata->su2_max_uA <= 0)
			ret = -EINVAL;
		if (ret < 0)
			goto err_put_bl;

		if (of_property_read_bool(bl, "su2-feedback-voltage")) {
			pdata->su2_feedback = AS3711_SU2_VOLTAGE;
			count++;
		}
		if (of_property_read_bool(bl, "su2-feedback-curr1")) {
			pdata->su2_feedback = AS3711_SU2_CURR1;
			count++;
		}
		if (of_property_read_bool(bl, "su2-feedback-curr2")) {
			pdata->su2_feedback = AS3711_SU2_CURR2;
			count++;
		}
		if (of_property_read_bool(bl, "su2-feedback-curr3")) {
			pdata->su2_feedback = AS3711_SU2_CURR3;
			count++;
		}
		if (of_property_read_bool(bl, "su2-feedback-curr-auto")) {
			pdata->su2_feedback = AS3711_SU2_CURR_AUTO;
			count++;
		}
		if (count != 1) {
			ret = -EINVAL;
			goto err_put_bl;
		}

		count = 0;
		if (of_property_read_bool(bl, "su2-fbprot-lx-sd4")) {
			pdata->su2_fbprot = AS3711_SU2_LX_SD4;
			count++;
		}
		if (of_property_read_bool(bl, "su2-fbprot-gpio2")) {
			pdata->su2_fbprot = AS3711_SU2_GPIO2;
			count++;
		}
		if (of_property_read_bool(bl, "su2-fbprot-gpio3")) {
			pdata->su2_fbprot = AS3711_SU2_GPIO3;
			count++;
		}
		if (of_property_read_bool(bl, "su2-fbprot-gpio4")) {
			pdata->su2_fbprot = AS3711_SU2_GPIO4;
			count++;
		}
		if (count != 1) {
			ret = -EINVAL;
			goto err_put_bl;
		}

		count = 0;
		if (of_property_read_bool(bl, "su2-auto-curr1")) {
			pdata->su2_auto_curr1 = true;
			count++;
		}
		if (of_property_read_bool(bl, "su2-auto-curr2")) {
			pdata->su2_auto_curr2 = true;
			count++;
		}
		if (of_property_read_bool(bl, "su2-auto-curr3")) {
			pdata->su2_auto_curr3 = true;
			count++;
		}

		/*
		 * At least one su2-auto-curr* must be specified iff
		 * AS3711_SU2_CURR_AUTO is used
		 */
		if (!count ^ (pdata->su2_feedback != AS3711_SU2_CURR_AUTO)) {
			ret = -EINVAL;
			goto err_put_bl;
		}
	}

	of_node_put(bl);

	return 0;

err_put_bl:
	of_node_put(bl);

	return ret;
}

static int as3711_backlight_probe(struct platform_device *pdev)
{
	struct as3711_bl_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct as3711 *as3711 = dev_get_drvdata(pdev->dev.parent);
	struct as3711_bl_supply *supply;
	struct as3711_bl_data *su;
	unsigned int max_brightness;
	int ret;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data, exiting...\n");
		return -ENODEV;
	}

	if (pdev->dev.parent->of_node) {
		ret = as3711_backlight_parse_dt(&pdev->dev);
		if (ret < 0)
			return dev_err_probe(&pdev->dev, ret, "DT parsing failed\n");
	}

	if (!pdata->su1_fb && !pdata->su2_fb) {
		dev_err(&pdev->dev, "No framebuffer specified\n");
		return -EINVAL;
	}

	/*
	 * Due to possible hardware damage I chose to block all modes,
	 * unsupported on my hardware. Anyone, wishing to use any of those modes
	 * will have to first review the code, then activate and test it.
	 */
	if (pdata->su1_fb ||
	    pdata->su2_fbprot != AS3711_SU2_GPIO4 ||
	    pdata->su2_feedback != AS3711_SU2_CURR_AUTO) {
		dev_warn(&pdev->dev,
			 "Attention! An untested mode has been chosen!\n"
			 "Please, review the code, enable, test, and report success:-)\n");
		return -EINVAL;
	}

	supply = devm_kzalloc(&pdev->dev, sizeof(*supply), GFP_KERNEL);
	if (!supply)
		return -ENOMEM;

	supply->as3711 = as3711;
	supply->pdata = pdata;

	if (pdata->su1_fb) {
		su = &supply->su1;
		su->type = AS3711_BL_SU1;

		max_brightness = min(pdata->su1_max_uA, 31);
		ret = as3711_bl_register(pdev, max_brightness, su);
		if (ret < 0)
			return ret;
	}

	if (pdata->su2_fb) {
		su = &supply->su2;
		su->type = AS3711_BL_SU2;

		switch (pdata->su2_fbprot) {
		case AS3711_SU2_GPIO2:
		case AS3711_SU2_GPIO3:
		case AS3711_SU2_GPIO4:
		case AS3711_SU2_LX_SD4:
			break;
		default:
			return -EINVAL;
		}

		switch (pdata->su2_feedback) {
		case AS3711_SU2_VOLTAGE:
			max_brightness = min(pdata->su2_max_uA, 31);
			break;
		case AS3711_SU2_CURR1:
		case AS3711_SU2_CURR2:
		case AS3711_SU2_CURR3:
		case AS3711_SU2_CURR_AUTO:
			max_brightness = min(pdata->su2_max_uA / 150, 255);
			break;
		default:
			return -EINVAL;
		}

		ret = as3711_bl_init_su2(supply);
		if (ret < 0)
			return ret;

		ret = as3711_bl_register(pdev, max_brightness, su);
		if (ret < 0)
			return ret;
	}

	platform_set_drvdata(pdev, supply);

	return 0;
}

static struct platform_driver as3711_backlight_driver = {
	.driver		= {
		.name	= "as3711-backlight",
	},
	.probe		= as3711_backlight_probe,
};

module_platform_driver(as3711_backlight_driver);

MODULE_DESCRIPTION("Backlight Driver for AS3711 PMICs");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:as3711-backlight");
