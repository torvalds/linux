/*
 * Copyright (C) 2013 Broadcom Corporation
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

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define SECWDOG_CTRL_REG		0x00000000
#define SECWDOG_COUNT_REG		0x00000004

#define SECWDOG_RESERVED_MASK		0x1dffffff
#define SECWDOG_WD_LOAD_FLAG		0x10000000
#define SECWDOG_EN_MASK			0x08000000
#define SECWDOG_SRSTEN_MASK		0x04000000
#define SECWDOG_RES_MASK		0x00f00000
#define SECWDOG_COUNT_MASK		0x000fffff

#define SECWDOG_MAX_COUNT		SECWDOG_COUNT_MASK
#define SECWDOG_CLKS_SHIFT		20
#define SECWDOG_MAX_RES			15
#define SECWDOG_DEFAULT_RESOLUTION	4
#define SECWDOG_MAX_TRY			1000

#define SECS_TO_TICKS(x, w)		((x) << (w)->resolution)
#define TICKS_TO_SECS(x, w)		((x) >> (w)->resolution)

#define BCM_KONA_WDT_NAME		"bcm_kona_wdt"

struct bcm_kona_wdt {
	void __iomem *base;
	/*
	 * One watchdog tick is 1/(2^resolution) seconds. Resolution can take
	 * the values 0-15, meaning one tick can be 1s to 30.52us. Our default
	 * resolution of 4 means one tick is 62.5ms.
	 *
	 * The watchdog counter is 20 bits. Depending on resolution, the maximum
	 * counter value of 0xfffff expires after about 12 days (resolution 0)
	 * down to only 32s (resolution 15). The default resolution of 4 gives
	 * us a maximum of about 18 hours and 12 minutes before the watchdog
	 * times out.
	 */
	int resolution;
	spinlock_t lock;
};

static int secure_register_read(void __iomem *addr)
{
	uint32_t val;
	unsigned count = 0;

	/*
	 * If the WD_LOAD_FLAG is set, the watchdog counter field is being
	 * updated in hardware. Once the WD timer is updated in hardware, it
	 * gets cleared.
	 */
	do {
		if (unlikely(count > 1))
			udelay(5);
		val = readl_relaxed(addr);
		count++;
	} while ((val & SECWDOG_WD_LOAD_FLAG) && count < SECWDOG_MAX_TRY);

	/* This is the only place we return a negative value. */
	if (val & SECWDOG_WD_LOAD_FLAG)
		return -ETIMEDOUT;

	/* We always mask out reserved bits. */
	val &= SECWDOG_RESERVED_MASK;

	return val;
}

static int bcm_kona_wdt_ctrl_reg_modify(struct bcm_kona_wdt *wdt,
					unsigned mask, unsigned newval)
{
	int val;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&wdt->lock, flags);

	val = secure_register_read(wdt->base + SECWDOG_CTRL_REG);
	if (val < 0) {
		ret = val;
	} else {
		val &= ~mask;
		val |= newval;
		writel_relaxed(val, wdt->base + SECWDOG_CTRL_REG);
	}

	spin_unlock_irqrestore(&wdt->lock, flags);

	return ret;
}

static int bcm_kona_wdt_set_resolution_reg(struct bcm_kona_wdt *wdt)
{
	if (wdt->resolution > SECWDOG_MAX_RES)
		return -EINVAL;

	return bcm_kona_wdt_ctrl_reg_modify(wdt, SECWDOG_RES_MASK,
					wdt->resolution << SECWDOG_CLKS_SHIFT);
}

static int bcm_kona_wdt_set_timeout_reg(struct watchdog_device *wdog,
					unsigned watchdog_flags)
{
	struct bcm_kona_wdt *wdt = watchdog_get_drvdata(wdog);

	return bcm_kona_wdt_ctrl_reg_modify(wdt, SECWDOG_COUNT_MASK,
					SECS_TO_TICKS(wdog->timeout, wdt) |
					watchdog_flags);
}

static int bcm_kona_wdt_set_timeout(struct watchdog_device *wdog,
	unsigned int t)
{
	wdog->timeout = t;
	return 0;
}

static unsigned int bcm_kona_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct bcm_kona_wdt *wdt = watchdog_get_drvdata(wdog);
	int val;
	unsigned long flags;

	spin_lock_irqsave(&wdt->lock, flags);
	val = secure_register_read(wdt->base + SECWDOG_COUNT_REG);
	spin_unlock_irqrestore(&wdt->lock, flags);

	if (val < 0)
		return val;

	return TICKS_TO_SECS(val & SECWDOG_COUNT_MASK, wdt);
}

static int bcm_kona_wdt_start(struct watchdog_device *wdog)
{
	return bcm_kona_wdt_set_timeout_reg(wdog,
					SECWDOG_EN_MASK | SECWDOG_SRSTEN_MASK);
}

static int bcm_kona_wdt_stop(struct watchdog_device *wdog)
{
	struct bcm_kona_wdt *wdt = watchdog_get_drvdata(wdog);

	return bcm_kona_wdt_ctrl_reg_modify(wdt, SECWDOG_EN_MASK |
					    SECWDOG_SRSTEN_MASK, 0);
}

static struct watchdog_ops bcm_kona_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	bcm_kona_wdt_start,
	.stop =		bcm_kona_wdt_stop,
	.set_timeout =	bcm_kona_wdt_set_timeout,
	.get_timeleft =	bcm_kona_wdt_get_timeleft,
};

static struct watchdog_info bcm_kona_wdt_info = {
	.options =	WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE |
			WDIOF_KEEPALIVEPING,
	.identity =	"Broadcom Kona Watchdog Timer",
};

static struct watchdog_device bcm_kona_wdt_wdd = {
	.info =		&bcm_kona_wdt_info,
	.ops =		&bcm_kona_wdt_ops,
	.min_timeout =	1,
	.max_timeout =	SECWDOG_MAX_COUNT >> SECWDOG_DEFAULT_RESOLUTION,
	.timeout =	SECWDOG_MAX_COUNT >> SECWDOG_DEFAULT_RESOLUTION,
};

static void bcm_kona_wdt_shutdown(struct platform_device *pdev)
{
	bcm_kona_wdt_stop(&bcm_kona_wdt_wdd);
}

static int bcm_kona_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm_kona_wdt *wdt;
	struct resource *res;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(wdt->base))
		return -ENODEV;

	wdt->resolution = SECWDOG_DEFAULT_RESOLUTION;
	ret = bcm_kona_wdt_set_resolution_reg(wdt);
	if (ret) {
		dev_err(dev, "Failed to set resolution (error: %d)", ret);
		return ret;
	}

	spin_lock_init(&wdt->lock);
	platform_set_drvdata(pdev, wdt);
	watchdog_set_drvdata(&bcm_kona_wdt_wdd, wdt);

	ret = bcm_kona_wdt_set_timeout_reg(&bcm_kona_wdt_wdd, 0);
	if (ret) {
		dev_err(dev, "Failed set watchdog timeout");
		return ret;
	}

	ret = watchdog_register_device(&bcm_kona_wdt_wdd);
	if (ret) {
		dev_err(dev, "Failed to register watchdog device");
		return ret;
	}

	dev_dbg(dev, "Broadcom Kona Watchdog Timer");

	return 0;
}

static int bcm_kona_wdt_remove(struct platform_device *pdev)
{
	bcm_kona_wdt_shutdown(pdev);
	watchdog_unregister_device(&bcm_kona_wdt_wdd);
	dev_dbg(&pdev->dev, "Watchdog driver disabled");

	return 0;
}

static const struct of_device_id bcm_kona_wdt_of_match[] = {
	{ .compatible = "brcm,kona-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm_kona_wdt_of_match);

static struct platform_driver bcm_kona_wdt_driver = {
	.driver = {
			.name = BCM_KONA_WDT_NAME,
			.owner = THIS_MODULE,
			.of_match_table = bcm_kona_wdt_of_match,
		  },
	.probe = bcm_kona_wdt_probe,
	.remove = bcm_kona_wdt_remove,
	.shutdown = bcm_kona_wdt_shutdown,
};

module_platform_driver(bcm_kona_wdt_driver);

MODULE_ALIAS("platform:" BCM_KONA_WDT_NAME);
MODULE_AUTHOR("Markus Mayer <mmayer@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Kona Watchdog Driver");
MODULE_LICENSE("GPL v2");
