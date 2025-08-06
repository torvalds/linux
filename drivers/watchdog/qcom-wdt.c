// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

enum wdt_reg {
	WDT_RST,
	WDT_EN,
	WDT_STS,
	WDT_BARK_TIME,
	WDT_BITE_TIME,
};

#define QCOM_WDT_ENABLE		BIT(0)

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

struct qcom_wdt_match_data {
	const u32 *offset;
	bool pretimeout;
	u32 max_tick_count;
};

struct qcom_wdt {
	struct watchdog_device	wdd;
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

static irqreturn_t qcom_wdt_isr(int irq, void *arg)
{
	struct watchdog_device *wdd = arg;

	watchdog_notify_pretimeout(wdd);

	return IRQ_HANDLED;
}

static int qcom_wdt_start(struct watchdog_device *wdd)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);
	unsigned int bark = wdd->timeout - wdd->pretimeout;

	writel(0, wdt_addr(wdt, WDT_EN));
	writel(1, wdt_addr(wdt, WDT_RST));
	writel(bark * wdt->rate, wdt_addr(wdt, WDT_BARK_TIME));
	writel(wdd->timeout * wdt->rate, wdt_addr(wdt, WDT_BITE_TIME));
	writel(QCOM_WDT_ENABLE, wdt_addr(wdt, WDT_EN));
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

static int qcom_wdt_set_pretimeout(struct watchdog_device *wdd,
				   unsigned int timeout)
{
	wdd->pretimeout = timeout;
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
	writel(QCOM_WDT_ENABLE, wdt_addr(wdt, WDT_EN));

	/*
	 * Actually make sure the above sequence hits hardware before sleeping.
	 */
	wmb();

	mdelay(150);
	return 0;
}

static int qcom_wdt_is_running(struct watchdog_device *wdd)
{
	struct qcom_wdt *wdt = to_qcom_wdt(wdd);

	return (readl(wdt_addr(wdt, WDT_EN)) & QCOM_WDT_ENABLE);
}

static const struct watchdog_ops qcom_wdt_ops = {
	.start		= qcom_wdt_start,
	.stop		= qcom_wdt_stop,
	.ping		= qcom_wdt_ping,
	.set_timeout	= qcom_wdt_set_timeout,
	.set_pretimeout	= qcom_wdt_set_pretimeout,
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

static const struct watchdog_info qcom_wdt_pt_info = {
	.options	= WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE
			| WDIOF_SETTIMEOUT
			| WDIOF_PRETIMEOUT
			| WDIOF_CARDRESET,
	.identity	= KBUILD_MODNAME,
};

static const struct qcom_wdt_match_data match_data_apcs_tmr = {
	.offset = reg_offset_data_apcs_tmr,
	.pretimeout = false,
	.max_tick_count = 0x10000000U,
};

static const struct qcom_wdt_match_data match_data_ipq5424 = {
	.offset = reg_offset_data_kpss,
	.pretimeout = true,
	.max_tick_count = 0xFFFFFU,
};

static const struct qcom_wdt_match_data match_data_kpss = {
	.offset = reg_offset_data_kpss,
	.pretimeout = true,
	.max_tick_count = 0xFFFFFU,
};

static int qcom_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcom_wdt *wdt;
	struct resource *res;
	struct device_node *np = dev->of_node;
	const struct qcom_wdt_match_data *data;
	u32 percpu_offset;
	int irq, ret;
	struct clk *clk;

	data = of_device_get_match_data(dev);
	if (!data) {
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

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get input clock\n");
		return PTR_ERR(clk);
	}

	/*
	 * We use the clock rate to calculate the max timeout, so ensure it's
	 * not zero to avoid a divide-by-zero exception.
	 *
	 * WATCHDOG_CORE assumes units of seconds, if the WDT is clocked such
	 * that it would bite before a second elapses it's usefulness is
	 * limited.  Bail if this is the case.
	 */
	wdt->rate = clk_get_rate(clk);
	if (wdt->rate == 0 ||
	    wdt->rate > data->max_tick_count) {
		dev_err(dev, "invalid clock rate\n");
		return -EINVAL;
	}

	/* check if there is pretimeout support */
	irq = platform_get_irq_optional(pdev, 0);
	if (data->pretimeout && irq > 0) {
		ret = devm_request_irq(dev, irq, qcom_wdt_isr, 0,
				       "wdt_bark", &wdt->wdd);
		if (ret)
			return ret;

		wdt->wdd.info = &qcom_wdt_pt_info;
		wdt->wdd.pretimeout = 1;
	} else {
		if (irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		wdt->wdd.info = &qcom_wdt_info;
	}

	wdt->wdd.ops = &qcom_wdt_ops;
	wdt->wdd.min_timeout = 1;
	wdt->wdd.max_timeout = data->max_tick_count / wdt->rate;
	wdt->wdd.parent = dev;
	wdt->layout = data->offset;

	if (readl(wdt_addr(wdt, WDT_STS)) & 1)
		wdt->wdd.bootstatus = WDIOF_CARDRESET;

	/*
	 * If 'timeout-sec' unspecified in devicetree, assume a 30 second
	 * default, unless the max timeout is less than 30 seconds, then use
	 * the max instead.
	 */
	wdt->wdd.timeout = min(wdt->wdd.max_timeout, 30U);
	watchdog_init_timeout(&wdt->wdd, 0, dev);

	/*
	 * If WDT is already running, call WDT start which
	 * will stop the WDT, set timeouts as bootloader
	 * might use different ones and set running bit
	 * to inform the WDT subsystem to ping the WDT
	 */
	if (qcom_wdt_is_running(&wdt->wdd)) {
		qcom_wdt_start(&wdt->wdd);
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret)
		return ret;

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

static const struct dev_pm_ops qcom_wdt_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(qcom_wdt_suspend, qcom_wdt_resume)
};

static const struct of_device_id qcom_wdt_of_table[] = {
	{ .compatible = "qcom,apss-wdt-ipq5424", .data = &match_data_ipq5424 },
	{ .compatible = "qcom,kpss-timer", .data = &match_data_apcs_tmr },
	{ .compatible = "qcom,scss-timer", .data = &match_data_apcs_tmr },
	{ .compatible = "qcom,kpss-wdt", .data = &match_data_kpss },
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
