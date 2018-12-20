/*
 * Watchdog driver for TS-4800 based boards
 *
 * Copyright (c) 2015 - Savoir-faire Linux
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/watchdog.h>

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/* possible feed values */
#define TS4800_WDT_FEED_2S       0x1
#define TS4800_WDT_FEED_10S      0x2
#define TS4800_WDT_DISABLE       0x3

struct ts4800_wdt {
	struct watchdog_device  wdd;
	struct regmap           *regmap;
	u32                     feed_offset;
	u32                     feed_val;
};

/*
 * TS-4800 supports the following timeout values:
 *
 *   value desc
 *   ---------------------
 *     0    feed for 338ms
 *     1    feed for 2.706s
 *     2    feed for 10.824s
 *     3    disable watchdog
 *
 * Keep the regmap/timeout map ordered by timeout
 */
static const struct {
	const int timeout;
	const int regval;
} ts4800_wdt_map[] = {
	{ 2,  TS4800_WDT_FEED_2S },
	{ 10, TS4800_WDT_FEED_10S },
};

#define MAX_TIMEOUT_INDEX       (ARRAY_SIZE(ts4800_wdt_map) - 1)

static void ts4800_write_feed(struct ts4800_wdt *wdt, u32 val)
{
	regmap_write(wdt->regmap, wdt->feed_offset, val);
}

static int ts4800_wdt_start(struct watchdog_device *wdd)
{
	struct ts4800_wdt *wdt = watchdog_get_drvdata(wdd);

	ts4800_write_feed(wdt, wdt->feed_val);
	return 0;
}

static int ts4800_wdt_stop(struct watchdog_device *wdd)
{
	struct ts4800_wdt *wdt = watchdog_get_drvdata(wdd);

	ts4800_write_feed(wdt, TS4800_WDT_DISABLE);
	return 0;
}

static int ts4800_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct ts4800_wdt *wdt = watchdog_get_drvdata(wdd);
	int i;

	for (i = 0; i < MAX_TIMEOUT_INDEX; i++) {
		if (ts4800_wdt_map[i].timeout >= timeout)
			break;
	}

	wdd->timeout = ts4800_wdt_map[i].timeout;
	wdt->feed_val = ts4800_wdt_map[i].regval;

	return 0;
}

static const struct watchdog_ops ts4800_wdt_ops = {
	.owner = THIS_MODULE,
	.start = ts4800_wdt_start,
	.stop = ts4800_wdt_stop,
	.set_timeout = ts4800_wdt_set_timeout,
};

static const struct watchdog_info ts4800_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "TS-4800 Watchdog",
};

static int ts4800_wdt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *syscon_np;
	struct watchdog_device *wdd;
	struct ts4800_wdt *wdt;
	u32 reg;
	int ret;

	syscon_np = of_parse_phandle(np, "syscon", 0);
	if (!syscon_np) {
		dev_err(&pdev->dev, "no syscon property\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_index(np, "syscon", 1, &reg);
	if (ret < 0) {
		dev_err(&pdev->dev, "no offset in syscon\n");
		return ret;
	}

	/* allocate memory for watchdog struct */
	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	/* set regmap and offset to know where to write */
	wdt->feed_offset = reg;
	wdt->regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);
	if (IS_ERR(wdt->regmap)) {
		dev_err(&pdev->dev, "cannot get parent's regmap\n");
		return PTR_ERR(wdt->regmap);
	}

	/* Initialize struct watchdog_device */
	wdd = &wdt->wdd;
	wdd->parent = &pdev->dev;
	wdd->info = &ts4800_wdt_info;
	wdd->ops = &ts4800_wdt_ops;
	wdd->min_timeout = ts4800_wdt_map[0].timeout;
	wdd->max_timeout = ts4800_wdt_map[MAX_TIMEOUT_INDEX].timeout;

	watchdog_set_drvdata(wdd, wdt);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_init_timeout(wdd, 0, &pdev->dev);

	/*
	 * As this watchdog supports only a few values, ts4800_wdt_set_timeout
	 * must be called to initialize timeout and feed_val with valid values.
	 * Default to maximum timeout if none, or an invalid one, is provided in
	 * device tree.
	 */
	if (!wdd->timeout)
		wdd->timeout = wdd->max_timeout;
	ts4800_wdt_set_timeout(wdd, wdd->timeout);

	/*
	 * The feed register is write-only, so it is not possible to determine
	 * watchdog's state. Disable it to be in a known state.
	 */
	ts4800_wdt_stop(wdd);

	ret = watchdog_register_device(wdd);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register watchdog device\n");
		return ret;
	}

	platform_set_drvdata(pdev, wdt);

	dev_info(&pdev->dev,
		 "initialized (timeout = %d sec, nowayout = %d)\n",
		 wdd->timeout, nowayout);

	return 0;
}

static int ts4800_wdt_remove(struct platform_device *pdev)
{
	struct ts4800_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdd);

	return 0;
}

static const struct of_device_id ts4800_wdt_of_match[] = {
	{ .compatible = "technologic,ts4800-wdt", },
	{ },
};
MODULE_DEVICE_TABLE(of, ts4800_wdt_of_match);

static struct platform_driver ts4800_wdt_driver = {
	.probe		= ts4800_wdt_probe,
	.remove		= ts4800_wdt_remove,
	.driver		= {
		.name	= "ts4800_wdt",
		.of_match_table = ts4800_wdt_of_match,
	},
};

module_platform_driver(ts4800_wdt_driver);

MODULE_AUTHOR("Damien Riegel <damien.riegel@savoirfairelinux.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ts4800_wdt");
