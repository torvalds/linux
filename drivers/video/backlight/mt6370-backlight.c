// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Author: ChiaEn Wu <chiaen_wu@richtek.com>
 */

#include <linux/backlight.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define MT6370_REG_DEV_INFO		0x100
#define MT6370_REG_BL_EN		0x1A0
#define MT6370_REG_BL_BSTCTRL		0x1A1
#define MT6370_REG_BL_PWM		0x1A2
#define MT6370_REG_BL_DIM2		0x1A4

#define MT6370_VENID_MASK		GENMASK(7, 4)
#define MT6370_BL_EXT_EN_MASK		BIT(7)
#define MT6370_BL_EN_MASK		BIT(6)
#define MT6370_BL_CODE_MASK		BIT(0)
#define MT6370_BL_CH_MASK		GENMASK(5, 2)
#define MT6370_BL_CH_SHIFT		2
#define MT6370_BL_DIM2_COMMON_MASK	GENMASK(2, 0)
#define MT6370_BL_DIM2_COMMON_SHIFT	3
#define MT6370_BL_DIM2_6372_MASK	GENMASK(5, 0)
#define MT6370_BL_DIM2_6372_SHIFT	6
#define MT6370_BL_PWM_EN_MASK		BIT(7)
#define MT6370_BL_PWM_HYS_EN_MASK	BIT(2)
#define MT6370_BL_PWM_HYS_SEL_MASK	GENMASK(1, 0)
#define MT6370_BL_OVP_EN_MASK		BIT(7)
#define MT6370_BL_OVP_SEL_MASK		GENMASK(6, 5)
#define MT6370_BL_OVP_SEL_SHIFT		5
#define MT6370_BL_OC_EN_MASK		BIT(3)
#define MT6370_BL_OC_SEL_MASK		GENMASK(2, 1)
#define MT6370_BL_OC_SEL_SHIFT		1

#define MT6370_BL_PWM_HYS_TH_MIN_STEP	1
#define MT6370_BL_PWM_HYS_TH_MAX_STEP	64
#define MT6370_BL_OVP_MIN_UV		17000000
#define MT6370_BL_OVP_MAX_UV		29000000
#define MT6370_BL_OVP_STEP_UV		4000000
#define MT6370_BL_OCP_MIN_UA		900000
#define MT6370_BL_OCP_MAX_UA		1800000
#define MT6370_BL_OCP_STEP_UA		300000
#define MT6370_BL_MAX_COMMON_BRIGHTNESS	2048
#define MT6370_BL_MAX_6372_BRIGHTNESS	16384
#define MT6370_BL_MAX_CH		15

enum {
	MT6370_VID_COMMON = 1,
	MT6370_VID_6372,
};

struct mt6370_priv {
	u8 dim2_mask;
	u8 dim2_shift;
	int def_max_brightness;
	struct backlight_device *bl;
	struct device *dev;
	struct gpio_desc *enable_gpio;
	struct regmap *regmap;
};

static int mt6370_bl_update_status(struct backlight_device *bl_dev)
{
	struct mt6370_priv *priv = bl_get_data(bl_dev);
	int brightness = backlight_get_brightness(bl_dev);
	unsigned int enable_val;
	u8 brightness_val[2];
	int ret;

	if (brightness) {
		brightness_val[0] = (brightness - 1) & priv->dim2_mask;
		brightness_val[1] = (brightness - 1) >> priv->dim2_shift;

		ret = regmap_raw_write(priv->regmap, MT6370_REG_BL_DIM2,
				       brightness_val, sizeof(brightness_val));
		if (ret)
			return ret;
	}

	gpiod_set_value(priv->enable_gpio, !!brightness);

	enable_val = brightness ? MT6370_BL_EN_MASK : 0;
	return regmap_update_bits(priv->regmap, MT6370_REG_BL_EN,
				  MT6370_BL_EN_MASK, enable_val);
}

static int mt6370_bl_get_brightness(struct backlight_device *bl_dev)
{
	struct mt6370_priv *priv = bl_get_data(bl_dev);
	unsigned int enable;
	u8 brightness_val[2];
	int brightness, ret;

	ret = regmap_read(priv->regmap, MT6370_REG_BL_EN, &enable);
	if (ret)
		return ret;

	if (!(enable & MT6370_BL_EN_MASK))
		return 0;

	ret = regmap_raw_read(priv->regmap, MT6370_REG_BL_DIM2,
			      brightness_val, sizeof(brightness_val));
	if (ret)
		return ret;

	brightness = brightness_val[1] << priv->dim2_shift;
	brightness += brightness_val[0] & priv->dim2_mask;

	return brightness + 1;
}

static const struct backlight_ops mt6370_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = mt6370_bl_update_status,
	.get_brightness = mt6370_bl_get_brightness,
};

static int mt6370_init_backlight_properties(struct mt6370_priv *priv,
					    struct backlight_properties *props)
{
	struct device *dev = priv->dev;
	u8 prop_val;
	u32 brightness, ovp_uV, ocp_uA;
	unsigned int mask, val;
	int ret;

	/* Vendor optional properties */
	val = 0;
	if (device_property_read_bool(dev, "mediatek,bled-pwm-enable"))
		val |= MT6370_BL_PWM_EN_MASK;

	if (device_property_read_bool(dev, "mediatek,bled-pwm-hys-enable"))
		val |= MT6370_BL_PWM_HYS_EN_MASK;

	ret = device_property_read_u8(dev,
				      "mediatek,bled-pwm-hys-input-th-steps",
				      &prop_val);
	if (!ret) {
		prop_val = clamp_val(prop_val,
				     MT6370_BL_PWM_HYS_TH_MIN_STEP,
				     MT6370_BL_PWM_HYS_TH_MAX_STEP);
		prop_val = prop_val <= 1 ? 0 :
			   prop_val <= 4 ? 1 :
			   prop_val <= 16 ? 2 : 3;
		val |= prop_val;
	}

	ret = regmap_update_bits(priv->regmap, MT6370_REG_BL_PWM,
				 val, val);
	if (ret)
		return ret;

	val = 0;
	if (device_property_read_bool(dev, "mediatek,bled-ovp-shutdown"))
		val |= MT6370_BL_OVP_EN_MASK;

	ret = device_property_read_u32(dev, "mediatek,bled-ovp-microvolt",
				       &ovp_uV);
	if (!ret) {
		ovp_uV = clamp_val(ovp_uV, MT6370_BL_OVP_MIN_UV,
				   MT6370_BL_OVP_MAX_UV);
		ovp_uV = DIV_ROUND_UP(ovp_uV - MT6370_BL_OVP_MIN_UV,
				      MT6370_BL_OVP_STEP_UV);
		val |= ovp_uV << MT6370_BL_OVP_SEL_SHIFT;
	}

	if (device_property_read_bool(dev, "mediatek,bled-ocp-shutdown"))
		val |= MT6370_BL_OC_EN_MASK;

	ret = device_property_read_u32(dev, "mediatek,bled-ocp-microamp",
				       &ocp_uA);
	if (!ret) {
		ocp_uA = clamp_val(ocp_uA, MT6370_BL_OCP_MIN_UA,
				   MT6370_BL_OCP_MAX_UA);
		ocp_uA = DIV_ROUND_UP(ocp_uA - MT6370_BL_OCP_MIN_UA,
				      MT6370_BL_OCP_STEP_UA);
		val |= ocp_uA << MT6370_BL_OC_SEL_SHIFT;
	}

	ret = regmap_update_bits(priv->regmap, MT6370_REG_BL_BSTCTRL,
				 val, val);
	if (ret)
		return ret;

	/* Common properties */
	ret = device_property_read_u32(dev, "max-brightness", &brightness);
	if (ret)
		brightness = priv->def_max_brightness;

	props->max_brightness = min_t(u32, brightness, priv->def_max_brightness);

	ret = device_property_read_u32(dev, "default-brightness", &brightness);
	if (ret)
		brightness = props->max_brightness;

	props->brightness = min_t(u32, brightness, props->max_brightness);

	val = 0;
	if (device_property_read_bool(dev, "mediatek,bled-exponential-mode-enable")) {
		val |= MT6370_BL_CODE_MASK;
		props->scale = BACKLIGHT_SCALE_NON_LINEAR;
	} else
		props->scale = BACKLIGHT_SCALE_LINEAR;

	ret = device_property_read_u8(dev, "mediatek,bled-channel-use",
				      &prop_val);
	if (ret) {
		dev_err(dev, "mediatek,bled-channel-use DT property missing\n");
		return ret;
	}

	if (!prop_val || prop_val > MT6370_BL_MAX_CH) {
		dev_err(dev,
			"No channel specified or over than upper bound (%d)\n",
			prop_val);
		return -EINVAL;
	}

	mask = MT6370_BL_EXT_EN_MASK | MT6370_BL_CH_MASK;
	val |= prop_val << MT6370_BL_CH_SHIFT;

	if (priv->enable_gpio)
		val |= MT6370_BL_EXT_EN_MASK;

	return regmap_update_bits(priv->regmap, MT6370_REG_BL_EN, mask, val);
}

static int mt6370_check_vendor_info(struct mt6370_priv *priv)
{
	/*
	 * Because MT6372 uses 14 bits to control the brightness,
	 * MT6370 and MT6371 use 11 bits. This function is used
	 * to check the vendor's ID and set the relative hardware
	 * mask, shift and default maximum brightness value that
	 * should be used.
	 */
	unsigned int dev_info, hw_vid, of_vid;
	int ret;

	ret = regmap_read(priv->regmap, MT6370_REG_DEV_INFO, &dev_info);
	if (ret)
		return ret;

	of_vid = (uintptr_t)device_get_match_data(priv->dev);
	hw_vid = FIELD_GET(MT6370_VENID_MASK, dev_info);
	hw_vid = (hw_vid == 0x9 || hw_vid == 0xb) ? MT6370_VID_6372 : MT6370_VID_COMMON;
	if (hw_vid != of_vid)
		return dev_err_probe(priv->dev, -EINVAL,
				     "Buggy DT, wrong compatible string\n");

	if (hw_vid == MT6370_VID_6372) {
		priv->dim2_mask = MT6370_BL_DIM2_6372_MASK;
		priv->dim2_shift = MT6370_BL_DIM2_6372_SHIFT;
		priv->def_max_brightness = MT6370_BL_MAX_6372_BRIGHTNESS;
	} else {
		priv->dim2_mask = MT6370_BL_DIM2_COMMON_MASK;
		priv->dim2_shift = MT6370_BL_DIM2_COMMON_SHIFT;
		priv->def_max_brightness = MT6370_BL_MAX_COMMON_BRIGHTNESS;
	}

	return 0;
}

static int mt6370_bl_probe(struct platform_device *pdev)
{
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
	};
	struct device *dev = &pdev->dev;
	struct mt6370_priv *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap)
		return dev_err_probe(dev, -ENODEV, "Failed to get regmap\n");

	ret = mt6370_check_vendor_info(priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to check vendor info\n");

	priv->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(priv->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->enable_gpio),
				     "Failed to get 'enable' gpio\n");

	ret = mt6370_init_backlight_properties(priv, &props);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to init backlight properties\n");

	priv->bl = devm_backlight_device_register(dev, pdev->name, dev, priv,
						  &mt6370_bl_ops, &props);
	if (IS_ERR(priv->bl))
		return dev_err_probe(dev, PTR_ERR(priv->bl),
				     "Failed to register backlight\n");

	backlight_update_status(priv->bl);
	platform_set_drvdata(pdev, priv);

	return 0;
}

static void mt6370_bl_remove(struct platform_device *pdev)
{
	struct mt6370_priv *priv = platform_get_drvdata(pdev);
	struct backlight_device *bl_dev = priv->bl;

	bl_dev->props.brightness = 0;
	backlight_update_status(priv->bl);
}

static const struct of_device_id mt6370_bl_of_match[] = {
	{ .compatible = "mediatek,mt6370-backlight", .data = (void *)MT6370_VID_COMMON },
	{ .compatible = "mediatek,mt6372-backlight", .data = (void *)MT6370_VID_6372 },
	{}
};
MODULE_DEVICE_TABLE(of, mt6370_bl_of_match);

static struct platform_driver mt6370_bl_driver = {
	.driver = {
		.name = "mt6370-backlight",
		.of_match_table = mt6370_bl_of_match,
	},
	.probe = mt6370_bl_probe,
	.remove_new = mt6370_bl_remove,
};
module_platform_driver(mt6370_bl_driver);

MODULE_AUTHOR("ChiaEn Wu <chiaen_wu@richtek.com>");
MODULE_DESCRIPTION("MediaTek MT6370 Backlight Driver");
MODULE_LICENSE("GPL v2");
