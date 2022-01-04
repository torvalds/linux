/*
 * drivers/watchdog/orion_wdt.c
 *
 * Watchdog driver for Orion/Kirkwood processors
 *
 * Author: Sylver Bruneau <sylver.bruneau@googlemail.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* RSTOUT mask register physical address for Orion5x, Kirkwood and Dove */
#define ORION_RSTOUT_MASK_OFFSET	0x20108

/* Internal registers can be configured at any 1 MiB aligned address */
#define INTERNAL_REGS_MASK		~(SZ_1M - 1)

/*
 * Watchdog timer block registers.
 */
#define TIMER_CTRL		0x0000
#define TIMER1_FIXED_ENABLE_BIT	BIT(12)
#define WDT_AXP_FIXED_ENABLE_BIT BIT(10)
#define TIMER1_ENABLE_BIT	BIT(2)

#define TIMER_A370_STATUS	0x0004
#define WDT_A370_EXPIRED	BIT(31)
#define TIMER1_STATUS_BIT	BIT(8)

#define TIMER1_VAL_OFF		0x001c

#define WDT_MAX_CYCLE_COUNT	0xffffffff

#define WDT_A370_RATIO_MASK(v)	((v) << 16)
#define WDT_A370_RATIO_SHIFT	5
#define WDT_A370_RATIO		(1 << WDT_A370_RATIO_SHIFT)

static bool nowayout = WATCHDOG_NOWAYOUT;
static int heartbeat;		/* module parameter (seconds) */

struct orion_watchdog;

struct orion_watchdog_data {
	int wdt_counter_offset;
	int wdt_enable_bit;
	int rstout_enable_bit;
	int rstout_mask_bit;
	int (*clock_init)(struct platform_device *,
			  struct orion_watchdog *);
	int (*enabled)(struct orion_watchdog *);
	int (*start)(struct watchdog_device *);
	int (*stop)(struct watchdog_device *);
};

struct orion_watchdog {
	struct watchdog_device wdt;
	void __iomem *reg;
	void __iomem *rstout;
	void __iomem *rstout_mask;
	unsigned long clk_rate;
	struct clk *clk;
	const struct orion_watchdog_data *data;
};

static int orion_wdt_clock_init(struct platform_device *pdev,
				struct orion_watchdog *dev)
{
	int ret;

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);
	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		clk_put(dev->clk);
		return ret;
	}

	dev->clk_rate = clk_get_rate(dev->clk);
	return 0;
}

static int armada370_wdt_clock_init(struct platform_device *pdev,
				    struct orion_watchdog *dev)
{
	int ret;

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);
	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		clk_put(dev->clk);
		return ret;
	}

	/* Setup watchdog input clock */
	atomic_io_modify(dev->reg + TIMER_CTRL,
			WDT_A370_RATIO_MASK(WDT_A370_RATIO_SHIFT),
			WDT_A370_RATIO_MASK(WDT_A370_RATIO_SHIFT));

	dev->clk_rate = clk_get_rate(dev->clk) / WDT_A370_RATIO;
	return 0;
}

static int armada375_wdt_clock_init(struct platform_device *pdev,
				    struct orion_watchdog *dev)
{
	int ret;

	dev->clk = of_clk_get_by_name(pdev->dev.of_node, "fixed");
	if (!IS_ERR(dev->clk)) {
		ret = clk_prepare_enable(dev->clk);
		if (ret) {
			clk_put(dev->clk);
			return ret;
		}

		atomic_io_modify(dev->reg + TIMER_CTRL,
				WDT_AXP_FIXED_ENABLE_BIT,
				WDT_AXP_FIXED_ENABLE_BIT);
		dev->clk_rate = clk_get_rate(dev->clk);

		return 0;
	}

	/* Mandatory fallback for proper devicetree backward compatibility */
	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);

	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		clk_put(dev->clk);
		return ret;
	}

	atomic_io_modify(dev->reg + TIMER_CTRL,
			WDT_A370_RATIO_MASK(WDT_A370_RATIO_SHIFT),
			WDT_A370_RATIO_MASK(WDT_A370_RATIO_SHIFT));
	dev->clk_rate = clk_get_rate(dev->clk) / WDT_A370_RATIO;

	return 0;
}

static int armadaxp_wdt_clock_init(struct platform_device *pdev,
				   struct orion_watchdog *dev)
{
	int ret;
	u32 val;

	dev->clk = of_clk_get_by_name(pdev->dev.of_node, "fixed");
	if (IS_ERR(dev->clk))
		return PTR_ERR(dev->clk);
	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		clk_put(dev->clk);
		return ret;
	}

	/* Fix the wdt and timer1 clock frequency to 25MHz */
	val = WDT_AXP_FIXED_ENABLE_BIT | TIMER1_FIXED_ENABLE_BIT;
	atomic_io_modify(dev->reg + TIMER_CTRL, val, val);

	dev->clk_rate = clk_get_rate(dev->clk);
	return 0;
}

static int orion_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	/* Reload watchdog duration */
	writel(dev->clk_rate * wdt_dev->timeout,
	       dev->reg + dev->data->wdt_counter_offset);
	if (dev->wdt.info->options & WDIOF_PRETIMEOUT)
		writel(dev->clk_rate * (wdt_dev->timeout - wdt_dev->pretimeout),
		       dev->reg + TIMER1_VAL_OFF);

	return 0;
}

static int armada375_start(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	/* Set watchdog duration */
	writel(dev->clk_rate * wdt_dev->timeout,
	       dev->reg + dev->data->wdt_counter_offset);
	if (dev->wdt.info->options & WDIOF_PRETIMEOUT)
		writel(dev->clk_rate * (wdt_dev->timeout - wdt_dev->pretimeout),
		       dev->reg + TIMER1_VAL_OFF);

	/* Clear the watchdog expiration bit */
	atomic_io_modify(dev->reg + TIMER_A370_STATUS, WDT_A370_EXPIRED, 0);

	/* Enable watchdog timer */
	reg = dev->data->wdt_enable_bit;
	if (dev->wdt.info->options & WDIOF_PRETIMEOUT)
		reg |= TIMER1_ENABLE_BIT;
	atomic_io_modify(dev->reg + TIMER_CTRL, reg, reg);

	/* Enable reset on watchdog */
	reg = readl(dev->rstout);
	reg |= dev->data->rstout_enable_bit;
	writel(reg, dev->rstout);

	atomic_io_modify(dev->rstout_mask, dev->data->rstout_mask_bit, 0);
	return 0;
}

static int armada370_start(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	/* Set watchdog duration */
	writel(dev->clk_rate * wdt_dev->timeout,
	       dev->reg + dev->data->wdt_counter_offset);

	/* Clear the watchdog expiration bit */
	atomic_io_modify(dev->reg + TIMER_A370_STATUS, WDT_A370_EXPIRED, 0);

	/* Enable watchdog timer */
	atomic_io_modify(dev->reg + TIMER_CTRL, dev->data->wdt_enable_bit,
						dev->data->wdt_enable_bit);

	/* Enable reset on watchdog */
	reg = readl(dev->rstout);
	reg |= dev->data->rstout_enable_bit;
	writel(reg, dev->rstout);
	return 0;
}

static int orion_start(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	/* Set watchdog duration */
	writel(dev->clk_rate * wdt_dev->timeout,
	       dev->reg + dev->data->wdt_counter_offset);

	/* Enable watchdog timer */
	atomic_io_modify(dev->reg + TIMER_CTRL, dev->data->wdt_enable_bit,
						dev->data->wdt_enable_bit);

	/* Enable reset on watchdog */
	atomic_io_modify(dev->rstout, dev->data->rstout_enable_bit,
				      dev->data->rstout_enable_bit);

	return 0;
}

static int orion_wdt_start(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	/* There are some per-SoC quirks to handle */
	return dev->data->start(wdt_dev);
}

static int orion_stop(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	/* Disable reset on watchdog */
	atomic_io_modify(dev->rstout, dev->data->rstout_enable_bit, 0);

	/* Disable watchdog timer */
	atomic_io_modify(dev->reg + TIMER_CTRL, dev->data->wdt_enable_bit, 0);

	return 0;
}

static int armada375_stop(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	u32 reg, mask;

	/* Disable reset on watchdog */
	atomic_io_modify(dev->rstout_mask, dev->data->rstout_mask_bit,
					   dev->data->rstout_mask_bit);
	reg = readl(dev->rstout);
	reg &= ~dev->data->rstout_enable_bit;
	writel(reg, dev->rstout);

	/* Disable watchdog timer */
	mask = dev->data->wdt_enable_bit;
	if (wdt_dev->info->options & WDIOF_PRETIMEOUT)
		mask |= TIMER1_ENABLE_BIT;
	atomic_io_modify(dev->reg + TIMER_CTRL, mask, 0);

	return 0;
}

static int armada370_stop(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	u32 reg;

	/* Disable reset on watchdog */
	reg = readl(dev->rstout);
	reg &= ~dev->data->rstout_enable_bit;
	writel(reg, dev->rstout);

	/* Disable watchdog timer */
	atomic_io_modify(dev->reg + TIMER_CTRL, dev->data->wdt_enable_bit, 0);

	return 0;
}

static int orion_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	return dev->data->stop(wdt_dev);
}

static int orion_enabled(struct orion_watchdog *dev)
{
	bool enabled, running;

	enabled = readl(dev->rstout) & dev->data->rstout_enable_bit;
	running = readl(dev->reg + TIMER_CTRL) & dev->data->wdt_enable_bit;

	return enabled && running;
}

static int armada375_enabled(struct orion_watchdog *dev)
{
	bool masked, enabled, running;

	masked = readl(dev->rstout_mask) & dev->data->rstout_mask_bit;
	enabled = readl(dev->rstout) & dev->data->rstout_enable_bit;
	running = readl(dev->reg + TIMER_CTRL) & dev->data->wdt_enable_bit;

	return !masked && enabled && running;
}

static int orion_wdt_enabled(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	return dev->data->enabled(dev);
}

static unsigned int orion_wdt_get_timeleft(struct watchdog_device *wdt_dev)
{
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);
	return readl(dev->reg + dev->data->wdt_counter_offset) / dev->clk_rate;
}

static struct watchdog_info orion_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Orion Watchdog",
};

static const struct watchdog_ops orion_wdt_ops = {
	.owner = THIS_MODULE,
	.start = orion_wdt_start,
	.stop = orion_wdt_stop,
	.ping = orion_wdt_ping,
	.get_timeleft = orion_wdt_get_timeleft,
};

static irqreturn_t orion_wdt_irq(int irq, void *devid)
{
	panic("Watchdog Timeout");
	return IRQ_HANDLED;
}

static irqreturn_t orion_wdt_pre_irq(int irq, void *devid)
{
	struct orion_watchdog *dev = devid;

	atomic_io_modify(dev->reg + TIMER_A370_STATUS,
			 TIMER1_STATUS_BIT, 0);
	watchdog_notify_pretimeout(&dev->wdt);
	return IRQ_HANDLED;
}

/*
 * The original devicetree binding for this driver specified only
 * one memory resource, so in order to keep DT backwards compatibility
 * we try to fallback to a hardcoded register address, if the resource
 * is missing from the devicetree.
 */
static void __iomem *orion_wdt_ioremap_rstout(struct platform_device *pdev,
					      phys_addr_t internal_regs)
{
	struct resource *res;
	phys_addr_t rstout;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res)
		return devm_ioremap(&pdev->dev, res->start,
				    resource_size(res));

	rstout = internal_regs + ORION_RSTOUT_MASK_OFFSET;

	WARN(1, FW_BUG "falling back to hardcoded RSTOUT reg %pa\n", &rstout);
	return devm_ioremap(&pdev->dev, rstout, 0x4);
}

static const struct orion_watchdog_data orion_data = {
	.rstout_enable_bit = BIT(1),
	.wdt_enable_bit = BIT(4),
	.wdt_counter_offset = 0x24,
	.clock_init = orion_wdt_clock_init,
	.enabled = orion_enabled,
	.start = orion_start,
	.stop = orion_stop,
};

static const struct orion_watchdog_data armada370_data = {
	.rstout_enable_bit = BIT(8),
	.wdt_enable_bit = BIT(8),
	.wdt_counter_offset = 0x34,
	.clock_init = armada370_wdt_clock_init,
	.enabled = orion_enabled,
	.start = armada370_start,
	.stop = armada370_stop,
};

static const struct orion_watchdog_data armadaxp_data = {
	.rstout_enable_bit = BIT(8),
	.wdt_enable_bit = BIT(8),
	.wdt_counter_offset = 0x34,
	.clock_init = armadaxp_wdt_clock_init,
	.enabled = orion_enabled,
	.start = armada370_start,
	.stop = armada370_stop,
};

static const struct orion_watchdog_data armada375_data = {
	.rstout_enable_bit = BIT(8),
	.rstout_mask_bit = BIT(10),
	.wdt_enable_bit = BIT(8),
	.wdt_counter_offset = 0x34,
	.clock_init = armada375_wdt_clock_init,
	.enabled = armada375_enabled,
	.start = armada375_start,
	.stop = armada375_stop,
};

static const struct orion_watchdog_data armada380_data = {
	.rstout_enable_bit = BIT(8),
	.rstout_mask_bit = BIT(10),
	.wdt_enable_bit = BIT(8),
	.wdt_counter_offset = 0x34,
	.clock_init = armadaxp_wdt_clock_init,
	.enabled = armada375_enabled,
	.start = armada375_start,
	.stop = armada375_stop,
};

static const struct of_device_id orion_wdt_of_match_table[] = {
	{
		.compatible = "marvell,orion-wdt",
		.data = &orion_data,
	},
	{
		.compatible = "marvell,armada-370-wdt",
		.data = &armada370_data,
	},
	{
		.compatible = "marvell,armada-xp-wdt",
		.data = &armadaxp_data,
	},
	{
		.compatible = "marvell,armada-375-wdt",
		.data = &armada375_data,
	},
	{
		.compatible = "marvell,armada-380-wdt",
		.data = &armada380_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, orion_wdt_of_match_table);

static int orion_wdt_get_regs(struct platform_device *pdev,
			      struct orion_watchdog *dev)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	dev->reg = devm_ioremap(&pdev->dev, res->start,
				resource_size(res));
	if (!dev->reg)
		return -ENOMEM;

	/* Each supported compatible has some RSTOUT register quirk */
	if (of_device_is_compatible(node, "marvell,orion-wdt")) {

		dev->rstout = orion_wdt_ioremap_rstout(pdev, res->start &
						       INTERNAL_REGS_MASK);
		if (!dev->rstout)
			return -ENODEV;

	} else if (of_device_is_compatible(node, "marvell,armada-370-wdt") ||
		   of_device_is_compatible(node, "marvell,armada-xp-wdt")) {

		/* Dedicated RSTOUT register, can be requested. */
		dev->rstout = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(dev->rstout))
			return PTR_ERR(dev->rstout);

	} else if (of_device_is_compatible(node, "marvell,armada-375-wdt") ||
		   of_device_is_compatible(node, "marvell,armada-380-wdt")) {

		/* Dedicated RSTOUT register, can be requested. */
		dev->rstout = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(dev->rstout))
			return PTR_ERR(dev->rstout);

		res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
		if (!res)
			return -ENODEV;
		dev->rstout_mask = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
		if (!dev->rstout_mask)
			return -ENOMEM;

	} else {
		return -ENODEV;
	}

	return 0;
}

static int orion_wdt_probe(struct platform_device *pdev)
{
	struct orion_watchdog *dev;
	const struct of_device_id *match;
	unsigned int wdt_max_duration;	/* (seconds) */
	int ret, irq;

	dev = devm_kzalloc(&pdev->dev, sizeof(struct orion_watchdog),
			   GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	match = of_match_device(orion_wdt_of_match_table, &pdev->dev);
	if (!match)
		/* Default legacy match */
		match = &orion_wdt_of_match_table[0];

	dev->wdt.info = &orion_wdt_info;
	dev->wdt.ops = &orion_wdt_ops;
	dev->wdt.min_timeout = 1;
	dev->data = match->data;

	ret = orion_wdt_get_regs(pdev, dev);
	if (ret)
		return ret;

	ret = dev->data->clock_init(pdev, dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot initialize clock\n");
		return ret;
	}

	wdt_max_duration = WDT_MAX_CYCLE_COUNT / dev->clk_rate;

	dev->wdt.timeout = wdt_max_duration;
	dev->wdt.max_timeout = wdt_max_duration;
	dev->wdt.parent = &pdev->dev;
	watchdog_init_timeout(&dev->wdt, heartbeat, &pdev->dev);

	platform_set_drvdata(pdev, &dev->wdt);
	watchdog_set_drvdata(&dev->wdt, dev);

	/*
	 * Let's make sure the watchdog is fully stopped, unless it's
	 * explicitly enabled. This may be the case if the module was
	 * removed and re-inserted, or if the bootloader explicitly
	 * set a running watchdog before booting the kernel.
	 */
	if (!orion_wdt_enabled(&dev->wdt))
		orion_wdt_stop(&dev->wdt);
	else
		set_bit(WDOG_HW_RUNNING, &dev->wdt.status);

	/* Request the IRQ only after the watchdog is disabled */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		/*
		 * Not all supported platforms specify an interrupt for the
		 * watchdog, so let's make it optional.
		 */
		ret = devm_request_irq(&pdev->dev, irq, orion_wdt_irq, 0,
				       pdev->name, dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request IRQ\n");
			goto disable_clk;
		}
	}

	/* Optional 2nd interrupt for pretimeout */
	irq = platform_get_irq_optional(pdev, 1);
	if (irq > 0) {
		orion_wdt_info.options |= WDIOF_PRETIMEOUT;
		ret = devm_request_irq(&pdev->dev, irq, orion_wdt_pre_irq,
				       0, pdev->name, dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request IRQ\n");
			goto disable_clk;
		}
	}


	watchdog_set_nowayout(&dev->wdt, nowayout);
	ret = watchdog_register_device(&dev->wdt);
	if (ret)
		goto disable_clk;

	pr_info("Initial timeout %d sec%s\n",
		dev->wdt.timeout, nowayout ? ", nowayout" : "");
	return 0;

disable_clk:
	clk_disable_unprepare(dev->clk);
	clk_put(dev->clk);
	return ret;
}

static int orion_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdt_dev = platform_get_drvdata(pdev);
	struct orion_watchdog *dev = watchdog_get_drvdata(wdt_dev);

	watchdog_unregister_device(wdt_dev);
	clk_disable_unprepare(dev->clk);
	clk_put(dev->clk);
	return 0;
}

static void orion_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdt_dev = platform_get_drvdata(pdev);
	orion_wdt_stop(wdt_dev);
}

static struct platform_driver orion_wdt_driver = {
	.probe		= orion_wdt_probe,
	.remove		= orion_wdt_remove,
	.shutdown	= orion_wdt_shutdown,
	.driver		= {
		.name	= "orion_wdt",
		.of_match_table = orion_wdt_of_match_table,
	},
};

module_platform_driver(orion_wdt_driver);

MODULE_AUTHOR("Sylver Bruneau <sylver.bruneau@googlemail.com>");
MODULE_DESCRIPTION("Orion Processor Watchdog");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Initial watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:orion_wdt");
