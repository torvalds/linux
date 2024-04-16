// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 ROHM Semiconductors
 *
 * ROHM BD9576MUF and BD9573MUF Watchdog driver
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/rohm-bd957x.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

static bool nowayout;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default=\"false\")");

#define HW_MARGIN_MIN 2
#define HW_MARGIN_MAX 4416
#define BD957X_WDT_DEFAULT_MARGIN 4416
#define WATCHDOG_TIMEOUT 30

struct bd9576_wdt_priv {
	struct gpio_desc	*gpiod_ping;
	struct gpio_desc	*gpiod_en;
	struct device		*dev;
	struct regmap		*regmap;
	bool			always_running;
	struct watchdog_device	wdd;
};

static void bd9576_wdt_disable(struct bd9576_wdt_priv *priv)
{
	gpiod_set_value_cansleep(priv->gpiod_en, 0);
}

static int bd9576_wdt_ping(struct watchdog_device *wdd)
{
	struct bd9576_wdt_priv *priv = watchdog_get_drvdata(wdd);

	/* Pulse */
	gpiod_set_value_cansleep(priv->gpiod_ping, 1);
	gpiod_set_value_cansleep(priv->gpiod_ping, 0);

	return 0;
}

static int bd9576_wdt_start(struct watchdog_device *wdd)
{
	struct bd9576_wdt_priv *priv = watchdog_get_drvdata(wdd);

	gpiod_set_value_cansleep(priv->gpiod_en, 1);

	return bd9576_wdt_ping(wdd);
}

static int bd9576_wdt_stop(struct watchdog_device *wdd)
{
	struct bd9576_wdt_priv *priv = watchdog_get_drvdata(wdd);

	if (!priv->always_running)
		bd9576_wdt_disable(priv);
	else
		set_bit(WDOG_HW_RUNNING, &wdd->status);

	return 0;
}

static const struct watchdog_info bd957x_wdt_ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING |
			  WDIOF_SETTIMEOUT,
	.identity	= "BD957x Watchdog",
};

static const struct watchdog_ops bd957x_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= bd9576_wdt_start,
	.stop		= bd9576_wdt_stop,
	.ping		= bd9576_wdt_ping,
};

/* Unit is hundreds of uS */
#define FASTNG_MIN 23

static int find_closest_fast(int target, int *sel, int *val)
{
	int i;
	int window = FASTNG_MIN;

	for (i = 0; i < 8 && window < target; i++)
		window <<= 1;

	*val = window;
	*sel = i;

	if (i == 8)
		return -EINVAL;

	return 0;

}

static int find_closest_slow_by_fast(int fast_val, int target, int *slowsel)
{
	int sel;
	static const int multipliers[] = {2, 3, 7, 15};

	for (sel = 0; sel < ARRAY_SIZE(multipliers) &&
	     multipliers[sel] * fast_val < target; sel++)
		;

	if (sel == ARRAY_SIZE(multipliers))
		return -EINVAL;

	*slowsel = sel;

	return 0;
}

static int find_closest_slow(int target, int *slow_sel, int *fast_sel)
{
	static const int multipliers[] = {2, 3, 7, 15};
	int i, j;
	int val = 0;
	int window = FASTNG_MIN;

	for (i = 0; i < 8; i++) {
		for (j = 0; j < ARRAY_SIZE(multipliers); j++) {
			int slow;

			slow = window * multipliers[j];
			if (slow >= target && (!val || slow < val)) {
				val = slow;
				*fast_sel = i;
				*slow_sel = j;
			}
		}
		window <<= 1;
	}
	if (!val)
		return -EINVAL;

	return 0;
}

#define BD957X_WDG_TYPE_WINDOW BIT(5)
#define BD957X_WDG_TYPE_SLOW 0
#define BD957X_WDG_TYPE_MASK BIT(5)
#define BD957X_WDG_NG_RATIO_MASK 0x18
#define BD957X_WDG_FASTNG_MASK 0x7

static int bd957x_set_wdt_mode(struct bd9576_wdt_priv *priv, int hw_margin,
			       int hw_margin_min)
{
	int ret, fastng, slowng, type, reg, mask;
	struct device *dev = priv->dev;

	/* convert to 100uS */
	hw_margin *= 10;
	hw_margin_min *= 10;
	if (hw_margin_min) {
		int min;

		type = BD957X_WDG_TYPE_WINDOW;
		dev_dbg(dev, "Setting type WINDOW 0x%x\n", type);
		ret = find_closest_fast(hw_margin_min, &fastng, &min);
		if (ret) {
			dev_err(dev, "bad WDT window for fast timeout\n");
			return ret;
		}

		ret = find_closest_slow_by_fast(min, hw_margin, &slowng);
		if (ret) {
			dev_err(dev, "bad WDT window\n");
			return ret;
		}

	} else {
		type = BD957X_WDG_TYPE_SLOW;
		dev_dbg(dev, "Setting type SLOW 0x%x\n", type);
		ret = find_closest_slow(hw_margin, &slowng, &fastng);
		if (ret) {
			dev_err(dev, "bad WDT window\n");
			return ret;
		}
	}

	slowng <<= ffs(BD957X_WDG_NG_RATIO_MASK) - 1;
	reg = type | slowng | fastng;
	mask = BD957X_WDG_TYPE_MASK | BD957X_WDG_NG_RATIO_MASK |
	       BD957X_WDG_FASTNG_MASK;
	ret = regmap_update_bits(priv->regmap, BD957X_REG_WDT_CONF,
				 mask, reg);

	return ret;
}

static int bd9576_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bd9576_wdt_priv *priv;
	u32 hw_margin[2];
	u32 hw_margin_max = BD957X_WDT_DEFAULT_MARGIN, hw_margin_min = 0;
	int count;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->dev = dev;
	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap) {
		dev_err(dev, "No regmap found\n");
		return -ENODEV;
	}

	priv->gpiod_en = devm_fwnode_gpiod_get(dev, dev_fwnode(dev->parent),
					       "rohm,watchdog-enable",
					       GPIOD_OUT_LOW,
					       "watchdog-enable");
	if (IS_ERR(priv->gpiod_en))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_en),
			      "getting watchdog-enable GPIO failed\n");

	priv->gpiod_ping = devm_fwnode_gpiod_get(dev, dev_fwnode(dev->parent),
						 "rohm,watchdog-ping",
						 GPIOD_OUT_LOW,
						 "watchdog-ping");
	if (IS_ERR(priv->gpiod_ping))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_ping),
				     "getting watchdog-ping GPIO failed\n");

	count = device_property_count_u32(dev->parent, "rohm,hw-timeout-ms");
	if (count < 0 && count != -EINVAL)
		return count;

	if (count > 0) {
		if (count > ARRAY_SIZE(hw_margin))
			return -EINVAL;

		ret = device_property_read_u32_array(dev->parent,
						     "rohm,hw-timeout-ms",
						     hw_margin, count);
		if (ret < 0)
			return ret;

		if (count == 1)
			hw_margin_max = hw_margin[0];

		if (count == 2) {
			hw_margin_max = hw_margin[1];
			hw_margin_min = hw_margin[0];
		}
	}

	ret = bd957x_set_wdt_mode(priv, hw_margin_max, hw_margin_min);
	if (ret)
		return ret;

	priv->always_running = device_property_read_bool(dev->parent,
							 "always-running");

	watchdog_set_drvdata(&priv->wdd, priv);

	priv->wdd.info			= &bd957x_wdt_ident;
	priv->wdd.ops			= &bd957x_wdt_ops;
	priv->wdd.min_hw_heartbeat_ms	= hw_margin_min;
	priv->wdd.max_hw_heartbeat_ms	= hw_margin_max;
	priv->wdd.parent		= dev;
	priv->wdd.timeout		= WATCHDOG_TIMEOUT;

	watchdog_init_timeout(&priv->wdd, 0, dev);
	watchdog_set_nowayout(&priv->wdd, nowayout);

	watchdog_stop_on_reboot(&priv->wdd);

	if (priv->always_running)
		bd9576_wdt_start(&priv->wdd);

	return devm_watchdog_register_device(dev, &priv->wdd);
}

static struct platform_driver bd9576_wdt_driver = {
	.driver	= {
		.name = "bd9576-wdt",
	},
	.probe	= bd9576_wdt_probe,
};

module_platform_driver(bd9576_wdt_driver);

MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("ROHM BD9576/BD9573 Watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:bd9576-wdt");
