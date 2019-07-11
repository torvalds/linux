// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 */

#include <linux/debugfs.h>
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
#ifdef CONFIG_BCM_KONA_WDT_DEBUG
	unsigned long busy_count;
	struct dentry *debugfs;
#endif
};

static int secure_register_read(struct bcm_kona_wdt *wdt, uint32_t offset)
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
		val = readl_relaxed(wdt->base + offset);
		count++;
	} while ((val & SECWDOG_WD_LOAD_FLAG) && count < SECWDOG_MAX_TRY);

#ifdef CONFIG_BCM_KONA_WDT_DEBUG
	/* Remember the maximum number iterations due to WD_LOAD_FLAG */
	if (count > wdt->busy_count)
		wdt->busy_count = count;
#endif

	/* This is the only place we return a negative value. */
	if (val & SECWDOG_WD_LOAD_FLAG)
		return -ETIMEDOUT;

	/* We always mask out reserved bits. */
	val &= SECWDOG_RESERVED_MASK;

	return val;
}

#ifdef CONFIG_BCM_KONA_WDT_DEBUG

static int bcm_kona_show(struct seq_file *s, void *data)
{
	int ctl_val, cur_val;
	unsigned long flags;
	struct bcm_kona_wdt *wdt = s->private;

	if (!wdt) {
		seq_puts(s, "No device pointer\n");
		return 0;
	}

	spin_lock_irqsave(&wdt->lock, flags);
	ctl_val = secure_register_read(wdt, SECWDOG_CTRL_REG);
	cur_val = secure_register_read(wdt, SECWDOG_COUNT_REG);
	spin_unlock_irqrestore(&wdt->lock, flags);

	if (ctl_val < 0 || cur_val < 0) {
		seq_puts(s, "Error accessing hardware\n");
	} else {
		int ctl, cur, ctl_sec, cur_sec, res;

		ctl = ctl_val & SECWDOG_COUNT_MASK;
		res = (ctl_val & SECWDOG_RES_MASK) >> SECWDOG_CLKS_SHIFT;
		cur = cur_val & SECWDOG_COUNT_MASK;
		ctl_sec = TICKS_TO_SECS(ctl, wdt);
		cur_sec = TICKS_TO_SECS(cur, wdt);
		seq_printf(s,
			   "Resolution: %d / %d\n"
			   "Control: %d s / %d (%#x) ticks\n"
			   "Current: %d s / %d (%#x) ticks\n"
			   "Busy count: %lu\n",
			   res, wdt->resolution,
			   ctl_sec, ctl, ctl,
			   cur_sec, cur, cur,
			   wdt->busy_count);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(bcm_kona);

static void bcm_kona_wdt_debug_init(struct platform_device *pdev)
{
	struct dentry *dir;
	struct bcm_kona_wdt *wdt = platform_get_drvdata(pdev);

	if (!wdt)
		return;

	wdt->debugfs = NULL;

	dir = debugfs_create_dir(BCM_KONA_WDT_NAME, NULL);
	if (IS_ERR_OR_NULL(dir))
		return;

	if (debugfs_create_file("info", S_IFREG | S_IRUGO, dir, wdt,
				&bcm_kona_fops))
		wdt->debugfs = dir;
	else
		debugfs_remove_recursive(dir);
}

static void bcm_kona_wdt_debug_exit(struct platform_device *pdev)
{
	struct bcm_kona_wdt *wdt = platform_get_drvdata(pdev);

	if (wdt && wdt->debugfs) {
		debugfs_remove_recursive(wdt->debugfs);
		wdt->debugfs = NULL;
	}
}

#else

static void bcm_kona_wdt_debug_init(struct platform_device *pdev) {}
static void bcm_kona_wdt_debug_exit(struct platform_device *pdev) {}

#endif /* CONFIG_BCM_KONA_WDT_DEBUG */

static int bcm_kona_wdt_ctrl_reg_modify(struct bcm_kona_wdt *wdt,
					unsigned mask, unsigned newval)
{
	int val;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&wdt->lock, flags);

	val = secure_register_read(wdt, SECWDOG_CTRL_REG);
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
	val = secure_register_read(wdt, SECWDOG_COUNT_REG);
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

static const struct watchdog_ops bcm_kona_wdt_ops = {
	.owner =	THIS_MODULE,
	.start =	bcm_kona_wdt_start,
	.stop =		bcm_kona_wdt_stop,
	.set_timeout =	bcm_kona_wdt_set_timeout,
	.get_timeleft =	bcm_kona_wdt_get_timeleft,
};

static const struct watchdog_info bcm_kona_wdt_info = {
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

	spin_lock_init(&wdt->lock);

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

	platform_set_drvdata(pdev, wdt);
	watchdog_set_drvdata(&bcm_kona_wdt_wdd, wdt);
	bcm_kona_wdt_wdd.parent = &pdev->dev;

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

	bcm_kona_wdt_debug_init(pdev);
	dev_dbg(dev, "Broadcom Kona Watchdog Timer");

	return 0;
}

static int bcm_kona_wdt_remove(struct platform_device *pdev)
{
	bcm_kona_wdt_debug_exit(pdev);
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
