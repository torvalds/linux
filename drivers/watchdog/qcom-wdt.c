// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/of_device.h>

enum wdt_reg {
	WDT_RST,
	WDT_EN,
	WDT_STS,
	WDT_BARK_TIME,
	WDT_BITE_TIME,
};

static const u32 reg_offset_data_apcs_tmr[] = {
	[WDT_RST] = 0x38,
	[WDT_EN] = 0x40,
	[WDT_STS] = 0x44,
	[WDT_BARK_TIME] = 0x4C,
	[WDT_BITE_TIME] = 0x5C,
};

static const u32 reg_offset_data_kpss[] = {
	[WDT_RST] = 0x4,
	[WDT_EN] = 0x8,
	[WDT_STS] = 0xC,
	[WDT_BARK_TIME] = 0x10,
	[WDT_BITE_TIME] = 0x14,
};

struct qcom_wdt {
	struct watchdog_device	wdd;
	struct clk		*clk;
	unsigned long		rate;
	void __iomem		*base;
	const u32		*layout;
};

static void __iomem *wdt_addr(struct qcom_wdt *wdt, enum wdt_reg reg)
{
	return wdt->base + wdt->layout[reg];
}

static inline
struct qcom_wdt *to_qcom_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct qcom_wdt, wdd);
}

static int qcom_wdt_start(struct watchdog_device *wdd)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);

	writel(0, wdt_addr(wdt, WDT_EN));
	writel(1, wdt_addr(wdt, WDT_RST));
	writel(wdd->timeout * wdt->rate, wdt_addr(wdt, WDT_BARK_TIME));
	writel(wdd->timeout * wdt->rate, wdt_addr(wdt, WDT_BITE_TIME));
	writel(1, wdt_addr(wdt, WDT_EN));
	return 0;
}

static int qcom_wdt_stop(struct watchdog_device *wdd)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);

	writel(0, wdt_addr(wdt, WDT_EN));
	return 0;
}

static int qcom_wdt_ping(struct watchdog_device *wdd)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);

	writel(1, wdt_addr(wdt, WDT_RST));
	return 0;
}

static int qcom_wdt_set_timeout(struct watchdog_device *wdd,
				unsigned int timeout)
{
	wdd->timeout = timeout;
	return qcom_wdt_start(wdd);
}

static int qcom_wdt_restart(struct watchdog_device *wdd, unsigned long action,
			    void *data)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);
	u32 timeout;

	/*
	 * Trigger watchdog bite:
	 *    Setup BITE_TIME to be 128ms, and enable WDT.
	 */
	timeout = 128 * wdt->rate / 1000;

	writel(0, wdt_addr(wdt, WDT_EN));
	writel(1, wdt_addr(wdt, WDT_RST));
	writel(timeout, wdt_addr(wdt, WDT_BARK_TIME));
	writel(timeout, wdt_addr(wdt, WDT_BITE_TIME));
	writel(1, wdt_addr(wdt, WDT_EN));

	/*
	 * Actually make sure the above sequence hits hardware before sleeping.
	 */
	wmb();

	msleep(150);
	return 0;
}

static const struct watchdog_ops qcom_wdt_ops = {
	.start		= qcom_wdt_start,
	.stop		= qcom_wdt_stop,
	.ping		= qcom_wdt_ping,
	.set_timeout	= qcom_wdt_set_timeout,
	.restart        = qcom_wdt_restart,
	.owner		= THIS_MODULE,
};

static const struct watchdog_info qcom_wdt_info = {
	.options	= WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE
			| WDIOF_SETTIMEOUT
			| WDIOF_CARDRESET,
	.identity	= KBUILD_MODNAME,
};

static void qcom_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static int qcom_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_wdt *wdt;
	struct resource *res;
	struct device_node *np = dev->of_node;
	const u32 *regs;
	u32 percpu_offset;
	int ret;

	regs = of_device_get_match_data(dev);
	if (!regs) {
		dev_err(dev, "Unsupported QCOM WDT module\n");
		return -ENODEV;
	}

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	/* We use CPU0's DGT for the watchdog */
	if (of_property_read_u32(np, "cpu-offset", &percpu_offset))
		percpu_offset = 0;

	res->start += percpu_offset;
	res->end += percpu_offset;

	wdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_err(dev, "failed to get input clock\n");
		return PTR_ERR(wdt->clk);
	}

	ret = clk_prepare_enable(wdt->clk);
	if (ret) {
		dev_err(dev, "failed to setup clock\n");
		return ret;
	}
	ret = devm_add_action_or_reset(dev, qcom_clk_disable_unprepare,
				       wdt->clk);
	if (ret)
		return ret;

	/*
	 * We use the clock rate to calculate the max timeout, so ensure it's
	 * not zero to avoid a divide-by-zero exception.
	 *
	 * WATCHDOG_CORE assumes units of seconds, if the WDT is clocked such
	 * that it would bite before a second elapses it's usefulness is
	 * limited.  Bail if this is the case.
	 */
	wdt->rate = clk_get_rate(wdt->clk);
	if (wdt->rate == 0 ||
	    wdt->rate > 0x10000000U) {
		dev_err(dev, "invalid clock rate\n");
		return -EINVAL;
	}

	wdt->wdd.info = &qcom_wdt_info;
	wdt->wdd.ops = &qcom_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = 0x10000000U / wdt->rate;
	wdt->wdd.parent = dev;
	wdt->layout = regs;

	if (readl(wdt_addr(wdt, WDT_STS)) & 1)
		wdt->wdd.bootstatus = WDIOF_CARDRESET;

	/*
	 * If 'timeout-sec' unspecified in devicetree, assume a 30 second
	 * default, unless the max timeout is less than 30 seconds, then use
	 * the max instead.
	 */
	wdt->wdd.timeout = min(wdt->wdd.max_timeout, 30U);
	watchdog_init_timeout(&wdt->wdd, 0, dev);

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret) {
		dev_err(dev, "failed to register watchdog\n");
		return ret;
	}

	platform_set_drvdata(pdev, wdt);
	return 0;
}

static int __maybe_unused qcom_wdt_suspend(struct device *dev)
{
	struct qcom_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		qcom_wdt_stop(&wdt->wdd);

	return 0;
}

static int __maybe_unused qcom_wdt_resume(struct device *dev)
{
	struct qcom_wdt *wdt = dev_get_drvdata(dev);

	if (watchdog_active(&wdt->wdd))
		qcom_wdt_start(&wdt->wdd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(qcom_wdt_pm_ops, qcom_wdt_suspend, qcom_wdt_resume);

static const struct of_device_id qcom_wdt_of_table[] = {
	{ .compatible = "qcom,kpss-timer", .data = reg_offset_data_apcs_tmr },
	{ .compatible = "qcom,scss-timer", .data = reg_offset_data_apcs_tmr },
	{ .compatible = "qcom,kpss-wdt", .data = reg_offset_data_kpss },
	{ },
};
MODULE_DEVICE_TABLE(of, qcom_wdt_of_table);

static struct platform_driver qcom_watchdog_driver = {
	.probe	= qcom_wdt_probe,
	.driver	= {
		.name		= KBUILD_MODNAME,
		.of_match_table	= qcom_wdt_of_table,
		.pm		= &qcom_wdt_pm_ops,
	},
};
module_platform_driver(qcom_watchdog_driver);

MODULE_DESCRIPTION("QCOM KPSS Watchdog Driver");
MODULE_LICENSE("GPL v2");
