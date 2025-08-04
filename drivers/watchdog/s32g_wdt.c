// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Watchdog driver for S32G SoC
 *
 * Copyright 2017-2019, 2021-2025 NXP.
 *
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define DRIVER_NAME "s32g-swt"

#define S32G_SWT_CR(__base)	((__base) + 0x00)	/* Control Register offset	*/
#define S32G_SWT_CR_SM		(BIT(9) | BIT(10))	/* -> Service Mode		*/
#define S32G_SWT_CR_STP		BIT(2)			/* -> Stop Mode Control		*/
#define S32G_SWT_CR_FRZ		BIT(1)			/* -> Debug Mode Control	*/
#define S32G_SWT_CR_WEN		BIT(0)			/* -> Watchdog Enable		*/

#define S32G_SWT_TO(__base)	((__base) + 0x08)	/* Timeout Register offset	*/

#define S32G_SWT_SR(__base)	((__base) + 0x10)	/* Service Register offset	*/
#define S32G_WDT_SEQ1		0xA602			/* -> service sequence number 1	*/
#define S32G_WDT_SEQ2		0xB480			/* -> service sequence number 2	*/

#define S32G_SWT_CO(__base)	((__base) + 0x14)	/* Counter output register	*/

#define S32G_WDT_DEFAULT_TIMEOUT	30

struct s32g_wdt_device {
	int rate;
	void __iomem *base;
	struct watchdog_device wdog;
};

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int timeout_param = S32G_WDT_DEFAULT_TIMEOUT;
module_param(timeout_param, uint, 0);
MODULE_PARM_DESC(timeout_param, "Watchdog timeout in seconds (default="
		 __MODULE_STRING(S32G_WDT_DEFAULT_TIMEOUT) ")");

static bool early_enable;
module_param(early_enable, bool, 0);
MODULE_PARM_DESC(early_enable,
		 "Watchdog is started on module insertion (default=false)");

static const struct watchdog_info s32g_wdt_info = {
	.identity = "s32g watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE |
	WDIOC_GETTIMEOUT | WDIOC_GETTIMELEFT,
};

static struct s32g_wdt_device *wdd_to_s32g_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct s32g_wdt_device, wdog);
}

static unsigned int wdog_sec_to_count(struct s32g_wdt_device *wdev, unsigned int timeout)
{
	return wdev->rate * timeout;
}

static int s32g_wdt_ping(struct watchdog_device *wdog)
{
	struct s32g_wdt_device *wdev = wdd_to_s32g_wdt(wdog);

	writel(S32G_WDT_SEQ1, S32G_SWT_SR(wdev->base));
	writel(S32G_WDT_SEQ2, S32G_SWT_SR(wdev->base));

	return 0;
}

static int s32g_wdt_start(struct watchdog_device *wdog)
{
	struct s32g_wdt_device *wdev = wdd_to_s32g_wdt(wdog);
	unsigned long val;

	val = readl(S32G_SWT_CR(wdev->base));

	val |= S32G_SWT_CR_WEN;

	writel(val, S32G_SWT_CR(wdev->base));

	return 0;
}

static int s32g_wdt_stop(struct watchdog_device *wdog)
{
	struct s32g_wdt_device *wdev = wdd_to_s32g_wdt(wdog);
	unsigned long val;

	val = readl(S32G_SWT_CR(wdev->base));

	val &= ~S32G_SWT_CR_WEN;

	writel(val, S32G_SWT_CR(wdev->base));

	return 0;
}

static int s32g_wdt_set_timeout(struct watchdog_device *wdog, unsigned int timeout)
{
	struct s32g_wdt_device *wdev = wdd_to_s32g_wdt(wdog);

	writel(wdog_sec_to_count(wdev, timeout), S32G_SWT_TO(wdev->base));

	wdog->timeout = timeout;

	/*
	 * Conforming to the documentation, the timeout counter is
	 * loaded when servicing is operated (aka ping) or when the
	 * counter is enabled. In case the watchdog is already started
	 * it must be stopped and started again to update the timeout
	 * register or a ping can be sent to refresh the counter. Here
	 * we choose to send a ping to the watchdog which is harmless
	 * if the watchdog is stopped.
	 */
	return s32g_wdt_ping(wdog);
}

static unsigned int s32g_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct s32g_wdt_device *wdev = wdd_to_s32g_wdt(wdog);
	unsigned long counter;
	bool is_running;

	/*
	 * The counter output can be read only if the SWT is
	 * disabled. Given the latency between the internal counter
	 * and the counter output update, there can be very small
	 * difference. However, we can accept this matter of fact
	 * given the resolution is a second based unit for the output.
	 */
	is_running = watchdog_hw_running(wdog);

	if (is_running)
		s32g_wdt_stop(wdog);

	counter = readl(S32G_SWT_CO(wdev->base));

	if (is_running)
		s32g_wdt_start(wdog);

	return counter / wdev->rate;
}

static const struct watchdog_ops s32g_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= s32g_wdt_start,
	.stop		= s32g_wdt_stop,
	.ping		= s32g_wdt_ping,
	.set_timeout	= s32g_wdt_set_timeout,
	.get_timeleft	= s32g_wdt_get_timeleft,
};

static void s32g_wdt_init(struct s32g_wdt_device *wdev)
{
	unsigned long val;

	/* Set the watchdog's Time-Out value */
	val = wdog_sec_to_count(wdev, wdev->wdog.timeout);

	writel(val, S32G_SWT_TO(wdev->base));

	/*
	 * Get the control register content. We are at init time, the
	 * watchdog should not be started.
	 */
	val = readl(S32G_SWT_CR(wdev->base));

	/*
	 * We want to allow the watchdog timer to be stopped when
	 * device enters debug mode.
	 */
	val |= S32G_SWT_CR_FRZ;

	/*
	 * However, when the CPU is in WFI or suspend mode, the
	 * watchdog must continue. The documentation refers it as the
	 * stopped mode.
	 */
	val &= ~S32G_SWT_CR_STP;

	/*
	 * Use Fixed Service Sequence to ping the watchdog which is
	 * 0x00 configuration value for the service mode. It should be
	 * already set because it is the default value but we reset it
	 * in case.
	 */
	val &= ~S32G_SWT_CR_SM;

	writel(val, S32G_SWT_CR(wdev->base));

	/*
	 * When the 'early_enable' option is set, we start the
	 * watchdog from the kernel.
	 */
	if (early_enable) {
		s32g_wdt_start(&wdev->wdog);
		set_bit(WDOG_HW_RUNNING, &wdev->wdog.status);
	}
}

static int s32g_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct clk *clk;
	struct s32g_wdt_device *wdev;
	struct watchdog_device *wdog;
	int ret;

	wdev = devm_kzalloc(dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdev->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(wdev->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(wdev->base), "Can not get resource\n");

	clk = devm_clk_get_enabled(dev, "counter");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Can't get Watchdog clock\n");

	wdev->rate = clk_get_rate(clk);
	if (!wdev->rate) {
		dev_err(dev, "Input clock rate is not valid\n");
		return -EINVAL;
	}

	wdog = &wdev->wdog;
	wdog->info = &s32g_wdt_info;
	wdog->ops = &s32g_wdt_ops;

	/*
	 * The code converts the timeout into a counter a value, if
	 * the value is less than 0x100, then it is clamped by the SWT
	 * module, so it is safe to specify a zero value as the
	 * minimum timeout.
	 */
	wdog->min_timeout = 0;

	/*
	 * The counter register is a 32 bits long, so the maximum
	 * counter value is UINT_MAX and the timeout in second is the
	 * value divided by the rate.
	 *
	 * For instance, a rate of 51MHz lead to 84 seconds maximum
	 * timeout.
	 */
	wdog->max_timeout = UINT_MAX / wdev->rate;

	/*
	 * The module param and the DT 'timeout-sec' property will
	 * override the default value if they are specified.
	 */
	ret = watchdog_init_timeout(wdog, timeout_param, dev);
	if (ret)
		return ret;

	/*
	 * As soon as the watchdog is started, there is no way to stop
	 * it if the 'nowayout' option is set at boot time
	 */
	watchdog_set_nowayout(wdog, nowayout);

	/*
	 * The devm_ version of the watchdog_register_device()
	 * function will call watchdog_unregister_device() when the
	 * device is removed.
	 */
	watchdog_stop_on_unregister(wdog);

	s32g_wdt_init(wdev);

	ret = devm_watchdog_register_device(dev, wdog);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register watchdog device\n");

	dev_info(dev, "S32G Watchdog Timer Registered, timeout=%ds, nowayout=%d, early_enable=%d\n",
		 wdog->timeout, nowayout, early_enable);

	return 0;
}

static const struct of_device_id s32g_wdt_dt_ids[] = {
	{ .compatible = "nxp,s32g2-swt" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s32g_wdt_dt_ids);

static struct platform_driver s32g_wdt_driver = {
	.probe = s32g_wdt_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = s32g_wdt_dt_ids,
	},
};

module_platform_driver(s32g_wdt_driver);

MODULE_AUTHOR("Daniel Lezcano <daniel.lezcano@linaro.org>");
MODULE_DESCRIPTION("Watchdog driver for S32G SoC");
MODULE_LICENSE("GPL");
