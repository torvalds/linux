/*
 * watchdog driver for ZTE's zx2967 family
 *
 * Copyright (C) 2017 ZTE Ltd.
 *
 * Author: Baoyou Xie <baoyou.xie@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/watchdog.h>

#define ZX2967_WDT_CFG_REG			0x4
#define ZX2967_WDT_LOAD_REG			0x8
#define ZX2967_WDT_REFRESH_REG			0x18
#define ZX2967_WDT_START_REG			0x1c

#define ZX2967_WDT_REFRESH_MASK			GENMASK(5, 0)

#define ZX2967_WDT_CFG_DIV(n)			((((n) & 0xff) - 1) << 8)
#define ZX2967_WDT_START_EN			0x1

/*
 * Hardware magic number.
 * When watchdog reg is written, the lowest 16 bits are valid, but
 * the highest 16 bits should be always this number.
 */
#define ZX2967_WDT_WRITEKEY			(0x1234 << 16)
#define ZX2967_WDT_VAL_MASK			GENMASK(15, 0)

#define ZX2967_WDT_DIV_DEFAULT			16
#define ZX2967_WDT_DEFAULT_TIMEOUT		32
#define ZX2967_WDT_MIN_TIMEOUT			1
#define ZX2967_WDT_MAX_TIMEOUT			524
#define ZX2967_WDT_MAX_COUNT			0xffff

#define ZX2967_WDT_CLK_FREQ			0x8000

#define ZX2967_WDT_FLAG_REBOOT_MON		BIT(0)

struct zx2967_wdt {
	struct watchdog_device	wdt_device;
	void __iomem		*reg_base;
	struct clk		*clock;
};

static inline u32 zx2967_wdt_readl(struct zx2967_wdt *wdt, u16 reg)
{
	return readl_relaxed(wdt->reg_base + reg);
}

static inline void zx2967_wdt_writel(struct zx2967_wdt *wdt, u16 reg, u32 val)
{
	writel_relaxed(val | ZX2967_WDT_WRITEKEY, wdt->reg_base + reg);
}

static void zx2967_wdt_refresh(struct zx2967_wdt *wdt)
{
	u32 val;

	val = zx2967_wdt_readl(wdt, ZX2967_WDT_REFRESH_REG);
	/*
	 * Bit 4-5, 1 and 2: refresh config info
	 * Bit 2-3, 1 and 2: refresh counter
	 * Bit 0-1, 1 and 2: refresh int-value
	 * we shift each group value between 1 and 2 to refresh all data.
	 */
	val ^= ZX2967_WDT_REFRESH_MASK;
	zx2967_wdt_writel(wdt, ZX2967_WDT_REFRESH_REG,
			  val & ZX2967_WDT_VAL_MASK);
}

static int
zx2967_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct zx2967_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned int divisor = ZX2967_WDT_DIV_DEFAULT;
	u32 count;

	count = timeout * ZX2967_WDT_CLK_FREQ;
	if (count > divisor * ZX2967_WDT_MAX_COUNT)
		divisor = DIV_ROUND_UP(count, ZX2967_WDT_MAX_COUNT);
	count = DIV_ROUND_UP(count, divisor);
	zx2967_wdt_writel(wdt, ZX2967_WDT_CFG_REG,
			ZX2967_WDT_CFG_DIV(divisor) & ZX2967_WDT_VAL_MASK);
	zx2967_wdt_writel(wdt, ZX2967_WDT_LOAD_REG,
			count & ZX2967_WDT_VAL_MASK);
	zx2967_wdt_refresh(wdt);
	wdd->timeout =  (count * divisor) / ZX2967_WDT_CLK_FREQ;

	return 0;
}

static void __zx2967_wdt_start(struct zx2967_wdt *wdt)
{
	u32 val;

	val = zx2967_wdt_readl(wdt, ZX2967_WDT_START_REG);
	val |= ZX2967_WDT_START_EN;
	zx2967_wdt_writel(wdt, ZX2967_WDT_START_REG,
			val & ZX2967_WDT_VAL_MASK);
}

static void __zx2967_wdt_stop(struct zx2967_wdt *wdt)
{
	u32 val;

	val = zx2967_wdt_readl(wdt, ZX2967_WDT_START_REG);
	val &= ~ZX2967_WDT_START_EN;
	zx2967_wdt_writel(wdt, ZX2967_WDT_START_REG,
			val & ZX2967_WDT_VAL_MASK);
}

static int zx2967_wdt_start(struct watchdog_device *wdd)
{
	struct zx2967_wdt *wdt = watchdog_get_drvdata(wdd);

	zx2967_wdt_set_timeout(wdd, wdd->timeout);
	__zx2967_wdt_start(wdt);

	return 0;
}

static int zx2967_wdt_stop(struct watchdog_device *wdd)
{
	struct zx2967_wdt *wdt = watchdog_get_drvdata(wdd);

	__zx2967_wdt_stop(wdt);

	return 0;
}

static int zx2967_wdt_keepalive(struct watchdog_device *wdd)
{
	struct zx2967_wdt *wdt = watchdog_get_drvdata(wdd);

	zx2967_wdt_refresh(wdt);

	return 0;
}

#define ZX2967_WDT_OPTIONS \
	(WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)
static const struct watchdog_info zx2967_wdt_ident = {
	.options          =     ZX2967_WDT_OPTIONS,
	.identity         =	"zx2967 watchdog",
};

static const struct watchdog_ops zx2967_wdt_ops = {
	.owner = THIS_MODULE,
	.start = zx2967_wdt_start,
	.stop = zx2967_wdt_stop,
	.ping = zx2967_wdt_keepalive,
	.set_timeout = zx2967_wdt_set_timeout,
};

static void zx2967_wdt_reset_sysctrl(struct device *dev)
{
	int ret;
	void __iomem *regmap;
	unsigned int offset, mask, config;
	struct of_phandle_args out_args;

	ret = of_parse_phandle_with_fixed_args(dev->of_node,
			"zte,wdt-reset-sysctrl", 3, 0, &out_args);
	if (ret)
		return;

	offset = out_args.args[0];
	config = out_args.args[1];
	mask = out_args.args[2];

	regmap = syscon_node_to_regmap(out_args.np);
	if (IS_ERR(regmap)) {
		of_node_put(out_args.np);
		return;
	}

	regmap_update_bits(regmap, offset, mask, config);
	of_node_put(out_args.np);
}

static int zx2967_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zx2967_wdt *wdt;
	struct resource *base;
	int ret;
	struct reset_control *rstc;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, wdt);

	wdt->wdt_device.info = &zx2967_wdt_ident;
	wdt->wdt_device.ops = &zx2967_wdt_ops;
	wdt->wdt_device.timeout = ZX2967_WDT_DEFAULT_TIMEOUT;
	wdt->wdt_device.max_timeout = ZX2967_WDT_MAX_TIMEOUT;
	wdt->wdt_device.min_timeout = ZX2967_WDT_MIN_TIMEOUT;
	wdt->wdt_device.parent = &pdev->dev;

	base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->reg_base = devm_ioremap_resource(dev, base);
	if (IS_ERR(wdt->reg_base))
		return PTR_ERR(wdt->reg_base);

	zx2967_wdt_reset_sysctrl(dev);

	wdt->clock = devm_clk_get(dev, NULL);
	if (IS_ERR(wdt->clock)) {
		dev_err(dev, "failed to find watchdog clock source\n");
		return PTR_ERR(wdt->clock);
	}

	ret = clk_prepare_enable(wdt->clock);
	if (ret < 0) {
		dev_err(dev, "failed to enable clock\n");
		return ret;
	}
	clk_set_rate(wdt->clock, ZX2967_WDT_CLK_FREQ);

	rstc = devm_reset_control_get(dev, NULL);
	if (IS_ERR(rstc)) {
		dev_err(dev, "failed to get rstc");
		ret = PTR_ERR(rstc);
		goto err;
	}

	reset_control_assert(rstc);
	reset_control_deassert(rstc);

	watchdog_set_drvdata(&wdt->wdt_device, wdt);
	watchdog_init_timeout(&wdt->wdt_device,
			ZX2967_WDT_DEFAULT_TIMEOUT, dev);
	watchdog_set_nowayout(&wdt->wdt_device, WATCHDOG_NOWAYOUT);

	ret = watchdog_register_device(&wdt->wdt_device);
	if (ret)
		goto err;

	dev_info(dev, "watchdog enabled (timeout=%d sec, nowayout=%d)",
		 wdt->wdt_device.timeout, WATCHDOG_NOWAYOUT);

	return 0;

err:
	clk_disable_unprepare(wdt->clock);
	return ret;
}

static int zx2967_wdt_remove(struct platform_device *pdev)
{
	struct zx2967_wdt *wdt = platform_get_drvdata(pdev);

	watchdog_unregister_device(&wdt->wdt_device);
	clk_disable_unprepare(wdt->clock);

	return 0;
}

static const struct of_device_id zx2967_wdt_match[] = {
	{ .compatible = "zte,zx296718-wdt", },
	{}
};
MODULE_DEVICE_TABLE(of, zx2967_wdt_match);

static struct platform_driver zx2967_wdt_driver = {
	.probe		= zx2967_wdt_probe,
	.remove		= zx2967_wdt_remove,
	.driver		= {
		.name	= "zx2967-wdt",
		.of_match_table	= of_match_ptr(zx2967_wdt_match),
	},
};
module_platform_driver(zx2967_wdt_driver);

MODULE_AUTHOR("Baoyou Xie <baoyou.xie@linaro.org>");
MODULE_DESCRIPTION("ZTE zx2967 Watchdog Device Driver");
MODULE_LICENSE("GPL v2");
