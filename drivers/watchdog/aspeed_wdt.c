/*
 * Copyright 2016 IBM Corporation
 *
 * Joel Stanley <joel@jms.id.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

struct aspeed_wdt {
	struct watchdog_device	wdd;
	void __iomem		*base;
	u32			ctrl;
};

struct aspeed_wdt_config {
	u32 ext_pulse_width_mask;
};

static const struct aspeed_wdt_config ast2400_config = {
	.ext_pulse_width_mask = 0xff,
};

static const struct aspeed_wdt_config ast2500_config = {
	.ext_pulse_width_mask = 0xfffff,
};

static const struct of_device_id aspeed_wdt_of_table[] = {
	{ .compatible = "aspeed,ast2400-wdt", .data = &ast2400_config },
	{ .compatible = "aspeed,ast2500-wdt", .data = &ast2500_config },
	{ },
};
MODULE_DEVICE_TABLE(of, aspeed_wdt_of_table);

#define WDT_STATUS		0x00
#define WDT_RELOAD_VALUE	0x04
#define WDT_RESTART		0x08
#define WDT_CTRL		0x0C
#define   WDT_CTRL_RESET_MODE_SOC	(0x00 << 5)
#define   WDT_CTRL_RESET_MODE_FULL_CHIP	(0x01 << 5)
#define   WDT_CTRL_RESET_MODE_ARM_CPU	(0x10 << 5)
#define   WDT_CTRL_1MHZ_CLK		BIT(4)
#define   WDT_CTRL_WDT_EXT		BIT(3)
#define   WDT_CTRL_WDT_INTR		BIT(2)
#define   WDT_CTRL_RESET_SYSTEM		BIT(1)
#define   WDT_CTRL_ENABLE		BIT(0)

/*
 * WDT_RESET_WIDTH controls the characteristics of the external pulse (if
 * enabled), specifically:
 *
 * * Pulse duration
 * * Drive mode: push-pull vs open-drain
 * * Polarity: Active high or active low
 *
 * Pulse duration configuration is available on both the AST2400 and AST2500,
 * though the field changes between SoCs:
 *
 * AST2400: Bits 7:0
 * AST2500: Bits 19:0
 *
 * This difference is captured in struct aspeed_wdt_config.
 *
 * The AST2500 exposes the drive mode and polarity options, but not in a
 * regular fashion. For read purposes, bit 31 represents active high or low,
 * and bit 30 represents push-pull or open-drain. With respect to write, magic
 * values need to be written to the top byte to change the state of the drive
 * mode and polarity bits. Any other value written to the top byte has no
 * effect on the state of the drive mode or polarity bits. However, the pulse
 * width value must be preserved (as desired) if written.
 */
#define WDT_RESET_WIDTH		0x18
#define   WDT_RESET_WIDTH_ACTIVE_HIGH	BIT(31)
#define     WDT_ACTIVE_HIGH_MAGIC	(0xA5 << 24)
#define     WDT_ACTIVE_LOW_MAGIC	(0x5A << 24)
#define   WDT_RESET_WIDTH_PUSH_PULL	BIT(30)
#define     WDT_PUSH_PULL_MAGIC		(0xA8 << 24)
#define     WDT_OPEN_DRAIN_MAGIC	(0x8A << 24)

#define WDT_RESTART_MAGIC	0x4755

/* 32 bits at 1MHz, in milliseconds */
#define WDT_MAX_TIMEOUT_MS	4294967
#define WDT_DEFAULT_TIMEOUT	30
#define WDT_RATE_1MHZ		1000000

static struct aspeed_wdt *to_aspeed_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct aspeed_wdt, wdd);
}

static void aspeed_wdt_enable(struct aspeed_wdt *wdt, int count)
{
	wdt->ctrl |= WDT_CTRL_ENABLE;

	writel(0, wdt->base + WDT_CTRL);
	writel(count, wdt->base + WDT_RELOAD_VALUE);
	writel(WDT_RESTART_MAGIC, wdt->base + WDT_RESTART);
	writel(wdt->ctrl, wdt->base + WDT_CTRL);
}

static int aspeed_wdt_start(struct watchdog_device *wdd)
{
	struct aspeed_wdt *wdt = to_aspeed_wdt(wdd);

	aspeed_wdt_enable(wdt, wdd->timeout * WDT_RATE_1MHZ);

	return 0;
}

static int aspeed_wdt_stop(struct watchdog_device *wdd)
{
	struct aspeed_wdt *wdt = to_aspeed_wdt(wdd);

	wdt->ctrl &= ~WDT_CTRL_ENABLE;
	writel(wdt->ctrl, wdt->base + WDT_CTRL);

	return 0;
}

static int aspeed_wdt_ping(struct watchdog_device *wdd)
{
	struct aspeed_wdt *wdt = to_aspeed_wdt(wdd);

	writel(WDT_RESTART_MAGIC, wdt->base + WDT_RESTART);

	return 0;
}

static int aspeed_wdt_set_timeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct aspeed_wdt *wdt = to_aspeed_wdt(wdd);
	u32 actual;

	wdd->timeout = timeout;

	actual = min(timeout, wdd->max_hw_heartbeat_ms * 1000);

	writel(actual * WDT_RATE_1MHZ, wdt->base + WDT_RELOAD_VALUE);
	writel(WDT_RESTART_MAGIC, wdt->base + WDT_RESTART);

	return 0;
}

static int aspeed_wdt_restart(struct watchdog_device *wdd,
			      unsigned long action, void *data)
{
	struct aspeed_wdt *wdt = to_aspeed_wdt(wdd);

	aspeed_wdt_enable(wdt, 128 * WDT_RATE_1MHZ / 1000);

	mdelay(1000);

	return 0;
}

static const struct watchdog_ops aspeed_wdt_ops = {
	.start		= aspeed_wdt_start,
	.stop		= aspeed_wdt_stop,
	.ping		= aspeed_wdt_ping,
	.set_timeout	= aspeed_wdt_set_timeout,
	.restart	= aspeed_wdt_restart,
	.owner		= THIS_MODULE,
};

static const struct watchdog_info aspeed_wdt_info = {
	.options	= WDIOF_KEEPALIVEPING
			| WDIOF_MAGICCLOSE
			| WDIOF_SETTIMEOUT,
	.identity	= KBUILD_MODNAME,
};

static int aspeed_wdt_probe(struct platform_device *pdev)
{
	const struct aspeed_wdt_config *config;
	const struct of_device_id *ofdid;
	struct aspeed_wdt *wdt;
	struct resource *res;
	struct device_node *np;
	const char *reset_type;
	u32 duration;
	int ret;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	/*
	 * The ast2400 wdt can run at PCLK, or 1MHz. The ast2500 only
	 * runs at 1MHz. We chose to always run at 1MHz, as there's no
	 * good reason to have a faster watchdog counter.
	 */
	wdt->wdd.info = &aspeed_wdt_info;
	wdt->wdd.ops = &aspeed_wdt_ops;
	wdt->wdd.max_hw_heartbeat_ms = WDT_MAX_TIMEOUT_MS;
	wdt->wdd.parent = &pdev->dev;

	wdt->wdd.timeout = WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(&wdt->wdd, 0, &pdev->dev);

	np = pdev->dev.of_node;

	ofdid = of_match_node(aspeed_wdt_of_table, np);
	if (!ofdid)
		return -EINVAL;
	config = ofdid->data;

	wdt->ctrl = WDT_CTRL_1MHZ_CLK;

	/*
	 * Control reset on a per-device basis to ensure the
	 * host is not affected by a BMC reboot
	 */
	ret = of_property_read_string(np, "aspeed,reset-type", &reset_type);
	if (ret) {
		wdt->ctrl |= WDT_CTRL_RESET_MODE_SOC | WDT_CTRL_RESET_SYSTEM;
	} else {
		if (!strcmp(reset_type, "cpu"))
			wdt->ctrl |= WDT_CTRL_RESET_MODE_ARM_CPU;
		else if (!strcmp(reset_type, "soc"))
			wdt->ctrl |= WDT_CTRL_RESET_MODE_SOC;
		else if (!strcmp(reset_type, "system"))
			wdt->ctrl |= WDT_CTRL_RESET_SYSTEM;
		else if (strcmp(reset_type, "none"))
			return -EINVAL;
	}
	if (of_property_read_bool(np, "aspeed,external-signal"))
		wdt->ctrl |= WDT_CTRL_WDT_EXT;

	writel(wdt->ctrl, wdt->base + WDT_CTRL);

	if (readl(wdt->base + WDT_CTRL) & WDT_CTRL_ENABLE)  {
		aspeed_wdt_start(&wdt->wdd);
		set_bit(WDOG_HW_RUNNING, &wdt->wdd.status);
	}

	if (of_device_is_compatible(np, "aspeed,ast2500-wdt")) {
		u32 reg = readl(wdt->base + WDT_RESET_WIDTH);

		reg &= config->ext_pulse_width_mask;
		if (of_property_read_bool(np, "aspeed,ext-push-pull"))
			reg |= WDT_PUSH_PULL_MAGIC;
		else
			reg |= WDT_OPEN_DRAIN_MAGIC;

		writel(reg, wdt->base + WDT_RESET_WIDTH);

		reg &= config->ext_pulse_width_mask;
		if (of_property_read_bool(np, "aspeed,ext-active-high"))
			reg |= WDT_ACTIVE_HIGH_MAGIC;
		else
			reg |= WDT_ACTIVE_LOW_MAGIC;

		writel(reg, wdt->base + WDT_RESET_WIDTH);
	}

	if (!of_property_read_u32(np, "aspeed,ext-pulse-duration", &duration)) {
		u32 max_duration = config->ext_pulse_width_mask + 1;

		if (duration == 0 || duration > max_duration) {
			dev_err(&pdev->dev, "Invalid pulse duration: %uus\n",
					duration);
			duration = max(1U, min(max_duration, duration));
			dev_info(&pdev->dev, "Pulse duration set to %uus\n",
					duration);
		}

		/*
		 * The watchdog is always configured with a 1MHz source, so
		 * there is no need to scale the microsecond value. However we
		 * need to offset it - from the datasheet:
		 *
		 * "This register decides the asserting duration of wdt_ext and
		 * wdt_rstarm signal. The default value is 0xFF. It means the
		 * default asserting duration of wdt_ext and wdt_rstarm is
		 * 256us."
		 *
		 * This implies a value of 0 gives a 1us pulse.
		 */
		writel(duration - 1, wdt->base + WDT_RESET_WIDTH);
	}

	ret = devm_watchdog_register_device(&pdev->dev, &wdt->wdd);
	if (ret) {
		dev_err(&pdev->dev, "failed to register\n");
		return ret;
	}

	return 0;
}

static struct platform_driver aspeed_watchdog_driver = {
	.probe = aspeed_wdt_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(aspeed_wdt_of_table),
	},
};
module_platform_driver(aspeed_watchdog_driver);

MODULE_DESCRIPTION("Aspeed Watchdog Driver");
MODULE_LICENSE("GPL");
