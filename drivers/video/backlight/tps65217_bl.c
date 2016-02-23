/*
 * tps65217_bl.c
 *
 * TPS65217 backlight driver
 *
 * Copyright (C) 2012 Matthias Kaehlcke
 * Author: Matthias Kaehlcke <matthias@kaehlcke.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/mfd/tps65217.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct tps65217_bl {
	struct tps65217 *tps;
	struct device *dev;
	struct backlight_device *bl;
	bool is_enabled;
};

static int tps65217_bl_enable(struct tps65217_bl *tps65217_bl)
{
	int rc;

	rc = tps65217_set_bits(tps65217_bl->tps, TPS65217_REG_WLEDCTRL1,
			TPS65217_WLEDCTRL1_ISINK_ENABLE,
			TPS65217_WLEDCTRL1_ISINK_ENABLE, TPS65217_PROTECT_NONE);
	if (rc) {
		dev_err(tps65217_bl->dev,
			"failed to enable backlight: %d\n", rc);
		return rc;
	}

	tps65217_bl->is_enabled = true;

	dev_dbg(tps65217_bl->dev, "backlight enabled\n");

	return 0;
}

static int tps65217_bl_disable(struct tps65217_bl *tps65217_bl)
{
	int rc;

	rc = tps65217_clear_bits(tps65217_bl->tps,
				TPS65217_REG_WLEDCTRL1,
				TPS65217_WLEDCTRL1_ISINK_ENABLE,
				TPS65217_PROTECT_NONE);
	if (rc) {
		dev_err(tps65217_bl->dev,
			"failed to disable backlight: %d\n", rc);
		return rc;
	}

	tps65217_bl->is_enabled = false;

	dev_dbg(tps65217_bl->dev, "backlight disabled\n");

	return 0;
}

static int tps65217_bl_update_status(struct backlight_device *bl)
{
	struct tps65217_bl *tps65217_bl = bl_get_data(bl);
	int rc;
	int brightness = bl->props.brightness;

	if (bl->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	if ((bl->props.power != FB_BLANK_UNBLANK) ||
		(bl->props.fb_blank != FB_BLANK_UNBLANK))
		/* framebuffer in low power mode or blanking active */
		brightness = 0;

	if (brightness > 0) {
		rc = tps65217_reg_write(tps65217_bl->tps,
					TPS65217_REG_WLEDCTRL2,
					brightness - 1,
					TPS65217_PROTECT_NONE);
		if (rc) {
			dev_err(tps65217_bl->dev,
				"failed to set brightness level: %d\n", rc);
			return rc;
		}

		dev_dbg(tps65217_bl->dev, "brightness set to %d\n", brightness);

		if (!tps65217_bl->is_enabled)
			rc = tps65217_bl_enable(tps65217_bl);
	} else {
		rc = tps65217_bl_disable(tps65217_bl);
	}

	return rc;
}

static const struct backlight_ops tps65217_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= tps65217_bl_update_status,
};

static int tps65217_bl_hw_init(struct tps65217_bl *tps65217_bl,
			struct tps65217_bl_pdata *pdata)
{
	int rc;

	rc = tps65217_bl_disable(tps65217_bl);
	if (rc)
		return rc;

	switch (pdata->isel) {
	case TPS65217_BL_ISET1:
		/* select ISET_1 current level */
		rc = tps65217_clear_bits(tps65217_bl->tps,
					TPS65217_REG_WLEDCTRL1,
					TPS65217_WLEDCTRL1_ISEL,
					TPS65217_PROTECT_NONE);
		if (rc) {
			dev_err(tps65217_bl->dev,
				"failed to select ISET1 current level: %d)\n",
				rc);
			return rc;
		}

		dev_dbg(tps65217_bl->dev, "selected ISET1 current level\n");

		break;

	case TPS65217_BL_ISET2:
		/* select ISET2 current level */
		rc = tps65217_set_bits(tps65217_bl->tps, TPS65217_REG_WLEDCTRL1,
				TPS65217_WLEDCTRL1_ISEL,
				TPS65217_WLEDCTRL1_ISEL, TPS65217_PROTECT_NONE);
		if (rc) {
			dev_err(tps65217_bl->dev,
				"failed to select ISET2 current level: %d\n",
				rc);
			return rc;
		}

		dev_dbg(tps65217_bl->dev, "selected ISET2 current level\n");

		break;

	default:
		dev_err(tps65217_bl->dev,
			"invalid value for current level: %d\n", pdata->isel);
		return -EINVAL;
	}

	/* set PWM frequency */
	rc = tps65217_set_bits(tps65217_bl->tps,
			TPS65217_REG_WLEDCTRL1,
			TPS65217_WLEDCTRL1_FDIM_MASK,
			pdata->fdim,
			TPS65217_PROTECT_NONE);
	if (rc) {
		dev_err(tps65217_bl->dev,
			"failed to select PWM dimming frequency: %d\n",
			rc);
		return rc;
	}

	return 0;
}

#ifdef CONFIG_OF
static struct tps65217_bl_pdata *
tps65217_bl_parse_dt(struct platform_device *pdev)
{
	struct tps65217 *tps = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = of_node_get(tps->dev->of_node);
	struct tps65217_bl_pdata *pdata, *err;
	u32 val;

	node = of_find_node_by_name(node, "backlight");
	if (!node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		err = ERR_PTR(-ENOMEM);
		goto err;
	}

	pdata->isel = TPS65217_BL_ISET1;
	if (!of_property_read_u32(node, "isel", &val)) {
		if (val < TPS65217_BL_ISET1 ||
			val > TPS65217_BL_ISET2) {
			dev_err(&pdev->dev,
				"invalid 'isel' value in the device tree\n");
			err = ERR_PTR(-EINVAL);
			goto err;
		}

		pdata->isel = val;
	}

	pdata->fdim = TPS65217_BL_FDIM_200HZ;
	if (!of_property_read_u32(node, "fdim", &val)) {
		switch (val) {
		case 100:
			pdata->fdim = TPS65217_BL_FDIM_100HZ;
			break;

		case 200:
			pdata->fdim = TPS65217_BL_FDIM_200HZ;
			break;

		case 500:
			pdata->fdim = TPS65217_BL_FDIM_500HZ;
			break;

		case 1000:
			pdata->fdim = TPS65217_BL_FDIM_1000HZ;
			break;

		default:
			dev_err(&pdev->dev,
				"invalid 'fdim' value in the device tree\n");
			err = ERR_PTR(-EINVAL);
			goto err;
		}
	}

	if (!of_property_read_u32(node, "default-brightness", &val)) {
		if (val < 0 ||
			val > 100) {
			dev_err(&pdev->dev,
				"invalid 'default-brightness' value in the device tree\n");
			err = ERR_PTR(-EINVAL);
			goto err;
		}

		pdata->dft_brightness = val;
	}

	of_node_put(node);

	return pdata;

err:
	of_node_put(node);

	return err;
}
#else
static struct tps65217_bl_pdata *
tps65217_bl_parse_dt(struct platform_device *pdev)
{
	return NULL;
}
#endif

static int tps65217_bl_probe(struct platform_device *pdev)
{
	int rc;
	struct tps65217 *tps = dev_get_drvdata(pdev->dev.parent);
	struct tps65217_bl *tps65217_bl;
	struct tps65217_bl_pdata *pdata;
	struct backlight_properties bl_props;

	if (tps->dev->of_node) {
		pdata = tps65217_bl_parse_dt(pdev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	} else {
		pdata = dev_get_platdata(&pdev->dev);
		if (!pdata) {
			dev_err(&pdev->dev, "no platform data provided\n");
			return -EINVAL;
		}
	}

	tps65217_bl = devm_kzalloc(&pdev->dev, sizeof(*tps65217_bl),
				GFP_KERNEL);
	if (tps65217_bl == NULL)
		return -ENOMEM;

	tps65217_bl->tps = tps;
	tps65217_bl->dev = &pdev->dev;
	tps65217_bl->is_enabled = false;

	rc = tps65217_bl_hw_init(tps65217_bl, pdata);
	if (rc)
		return rc;

	memset(&bl_props, 0, sizeof(struct backlight_properties));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.max_brightness = 100;

	tps65217_bl->bl = devm_backlight_device_register(&pdev->dev, pdev->name,
						tps65217_bl->dev, tps65217_bl,
						&tps65217_bl_ops, &bl_props);
	if (IS_ERR(tps65217_bl->bl)) {
		dev_err(tps65217_bl->dev,
			"registration of backlight device failed: %d\n", rc);
		return PTR_ERR(tps65217_bl->bl);
	}

	tps65217_bl->bl->props.brightness = pdata->dft_brightness;
	backlight_update_status(tps65217_bl->bl);
	platform_set_drvdata(pdev, tps65217_bl);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tps65217_bl_of_match[] = {
	{ .compatible = "ti,tps65217-bl", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, tps65217_bl_of_match);
#endif

static struct platform_driver tps65217_bl_driver = {
	.probe		= tps65217_bl_probe,
	.driver		= {
		.name	= "tps65217-bl",
		.of_match_table = of_match_ptr(tps65217_bl_of_match),
	},
};

module_platform_driver(tps65217_bl_driver);

MODULE_DESCRIPTION("TPS65217 Backlight driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Matthias Kaehlcke <matthias@kaehlcke.net>");
