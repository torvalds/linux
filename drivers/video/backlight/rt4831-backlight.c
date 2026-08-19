// SPDX-License-Identifier: GPL-2.0-only

#include <dt-bindings/leds/rt4831-backlight.h>
#include <linux/backlight.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define RT4831_REG_BLCFG	0x02
#define RT4831_REG_BLDIML	0x04
#define RT4831_REG_ENABLE	0x08
#define RT4831_REG_BLOPT2	0x11

#define RT4831_BLMAX_BRIGHTNESS	2048

#define RT4831_BLOVP_MASK	GENMASK(7, 5)
#define RT4831_BLOVP_SHIFT	5
#define RT4831_BLPWMEN_MASK	BIT(0)
#define RT4831_BLEN_MASK	BIT(4)
#define RT4831_BLCH_MASK	GENMASK(3, 0)
#define RT4831_BLDIML_MASK	GENMASK(2, 0)
#define RT4831_BLDIMH_MASK	GENMASK(10, 3)
#define RT4831_BLDIMH_SHIFT	3
#define RT4831_BLOCP_MASK	GENMASK(1, 0)

#define RT4831_BLOCP_MINUA	900000
#define RT4831_BLOCP_MAXUA	1800000
#define RT4831_BLOCP_STEPUA	300000

struct rt4831_priv {
	struct device *dev;
	struct regmap *regmap;
	struct backlight_device *bl;
};

static int rt4831_bl_update_status(struct backlight_device *bl_dev)
{
	struct rt4831_priv *priv = bl_get_data(bl_dev);
	int brightness = backlight_get_brightness(bl_dev);
	unsigned int enable = brightness ? RT4831_BLEN_MASK : 0;
	u8 v[2];
	int ret;

	if (brightness) {
		v[0] = (brightness - 1) & RT4831_BLDIML_MASK;
		v[1] = ((brightness - 1) & RT4831_BLDIMH_MASK) >> RT4831_BLDIMH_SHIFT;

		ret = regmap_raw_write(priv->regmap, RT4831_REG_BLDIML, v, sizeof(v));
		if (ret)
			return ret;
	}

	return regmap_update_bits(priv->regmap, RT4831_REG_ENABLE, RT4831_BLEN_MASK, enable);

}

static int rt4831_bl_get_brightness(struct backlight_device *bl_dev)
{
	struct rt4831_priv *priv = bl_get_data(bl_dev);
	unsigned int val;
	u8 v[2];
	int ret;

	ret = regmap_read(priv->regmap, RT4831_REG_ENABLE, &val);
	if (ret)
		return ret;

	if (!(val & RT4831_BLEN_MASK))
		return 0;

	ret = regmap_raw_read(priv->regmap, RT4831_REG_BLDIML, v, sizeof(v));
	if (ret)
		return ret;

	ret = (v[1] << RT4831_BLDIMH_SHIFT) + (v[0] & RT4831_BLDIML_MASK) + 1;

	return ret;
}

static const struct backlight_ops rt4831_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = rt4831_bl_update_status,
	.get_brightness = rt4831_bl_get_brightness,
};

static int rt4831_parse_backlight_properties(struct rt4831_priv *priv,
					     struct backlight_properties *bl_props)
{
	struct device *dev = priv->dev;
	u8 propval;
	u32 brightness, ocp_uA;
	unsigned int val = 0;
	int ret;

	/* common properties */
	ret = device_property_read_u32(dev, "max-brightness", &brightness);
	if (ret)
		brightness = RT4831_BLMAX_BRIGHTNESS;

	bl_props->max_brightness = min_t(u32, brightness, RT4831_BLMAX_BRIGHTNESS);

	ret = device_property_read_u32(dev, "default-brightness", &brightness);
	if (ret)
		brightness = bl_props->max_brightness;

	bl_props->brightness = min_t(u32, brightness, bl_props->max_brightness);

	/* vendor properties */
	if (device_property_read_bool(dev, "richtek,pwm-enable"))
		val = RT4831_BLPWMEN_MASK;

	ret = regmap_update_bits(priv->regmap, RT4831_REG_BLCFG, RT4831_BLPWMEN_MASK, val);
	if (ret)
		return ret;

	ret = device_property_read_u8(dev, "richtek,bled-ovp-sel", &propval);
	if (ret)
		propval = RT4831_BLOVPLVL_21V;

	propval = min_t(u8, propval, RT4831_BLOVPLVL_29V);
	ret = regmap_update_bits(priv->regmap, RT4831_REG_BLCFG, RT4831_BLOVP_MASK,
				 propval << RT4831_BLOVP_SHIFT);
	if (ret)
		return ret;

	/*
	 * This OCP level is used to protect and limit the inductor current.
	 * If inductor peak current reach the level, low-side MOSFET will be
	 * turned off. Meanwhile, the output channel current may be limited.
	 * To match the configured channel current, the inductor chosen must
	 * be higher than the OCP level.
	 *
	 * Not like the OVP level, the default 21V can be used in the most
	 * application. But if the chosen OCP level is smaller than needed,
	 * it will also affect the backlight channel output current to be
	 * smaller than the register setting.
	 */
	ret = device_property_read_u32(dev, "richtek,bled-ocp-microamp",
				       &ocp_uA);
	if (!ret) {
		ocp_uA = clamp_val(ocp_uA, RT4831_BLOCP_MINUA,
				   RT4831_BLOCP_MAXUA);
		val = DIV_ROUND_UP(ocp_uA - RT4831_BLOCP_MINUA,
				   RT4831_BLOCP_STEPUA);
		ret = regmap_update_bits(priv->regmap, RT4831_REG_BLOPT2,
					 RT4831_BLOCP_MASK, val);
		if (ret)
			return ret;
	}

	ret = device_property_read_u8(dev, "richtek,channel-use", &propval);
	if (ret) {
		dev_err(dev, "richtek,channel-use DT property missing\n");
		return ret;
	}

	if (!(propval & RT4831_BLCH_MASK)) {
		dev_err(dev, "No channel specified\n");
		return -EINVAL;
	}

	return regmap_update_bits(priv->regmap, RT4831_REG_ENABLE, RT4831_BLCH_MASK, propval);
}

static int rt4831_bl_probe(struct platform_device *pdev)
{
	struct rt4831_priv *priv;
	struct backlight_properties bl_props = { .type = BACKLIGHT_RAW,
						 .scale = BACKLIGHT_SCALE_LINEAR };
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	priv->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!priv->regmap) {
		dev_err(&pdev->dev, "Failed to init regmap\n");
		return -ENODEV;
	}

	ret = rt4831_parse_backlight_properties(priv, &bl_props);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse backlight properties\n");
		return ret;
	}

	priv->bl = devm_backlight_device_register(&pdev->dev, pdev->name, &pdev->dev, priv,
						  &rt4831_bl_ops, &bl_props);
	if (IS_ERR(priv->bl)) {
		dev_err(&pdev->dev, "Failed to register backlight\n");
		return PTR_ERR(priv->bl);
	}

	backlight_update_status(priv->bl);
	platform_set_drvdata(pdev, priv);

	return 0;
}

static void rt4831_bl_remove(struct platform_device *pdev)
{
	struct rt4831_priv *priv = platform_get_drvdata(pdev);
	struct backlight_device *bl_dev = priv->bl;

	bl_dev->props.brightness = 0;
	backlight_update_status(priv->bl);
}

static const struct of_device_id __maybe_unused rt4831_bl_of_match[] = {
	{ .compatible = "richtek,rt4831-backlight", },
	{}
};
MODULE_DEVICE_TABLE(of, rt4831_bl_of_match);

static struct platform_driver rt4831_bl_driver = {
	.driver = {
		.name = "rt4831-backlight",
		.of_match_table = rt4831_bl_of_match,
	},
	.probe = rt4831_bl_probe,
	.remove = rt4831_bl_remove,
};
module_platform_driver(rt4831_bl_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("Richtek RT4831 Backlight Driver");
MODULE_LICENSE("GPL v2");
