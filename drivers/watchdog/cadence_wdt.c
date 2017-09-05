/*
 * Cadence WDT driver - Used by Xilinx Zynq
 *
 * Copyright (C) 2010 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define CDNS_WDT_DEFAULT_TIMEOUT	10
/* Supports 1 - 516 sec */
#define CDNS_WDT_MIN_TIMEOUT	1
#define CDNS_WDT_MAX_TIMEOUT	516

/* Restart key */
#define CDNS_WDT_RESTART_KEY 0x00001999

/* Counter register access key */
#define CDNS_WDT_REGISTER_ACCESS_KEY 0x00920000

/* Counter value divisor */
#define CDNS_WDT_COUNTER_VALUE_DIVISOR 0x1000

/* Clock prescaler value and selection */
#define CDNS_WDT_PRESCALE_64	64
#define CDNS_WDT_PRESCALE_512	512
#define CDNS_WDT_PRESCALE_4096	4096
#define CDNS_WDT_PRESCALE_SELECT_64	1
#define CDNS_WDT_PRESCALE_SELECT_512	2
#define CDNS_WDT_PRESCALE_SELECT_4096	3

/* Input clock frequency */
#define CDNS_WDT_CLK_10MHZ	10000000
#define CDNS_WDT_CLK_75MHZ	75000000

/* Counter maximum value */
#define CDNS_WDT_COUNTER_MAX 0xFFF

static int wdt_timeout;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(CDNS_WDT_DEFAULT_TIMEOUT) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/**
 * struct cdns_wdt - Watchdog device structure
 * @regs: baseaddress of device
 * @rst: reset flag
 * @clk: struct clk * of a clock source
 * @prescaler: for saving prescaler value
 * @ctrl_clksel: counter clock prescaler selection
 * @io_lock: spinlock for IO register access
 * @cdns_wdt_device: watchdog device structure
 *
 * Structure containing parameters specific to cadence watchdog.
 */
struct cdns_wdt {
	void __iomem		*regs;
	bool			rst;
	struct clk		*clk;
	u32			prescaler;
	u32			ctrl_clksel;
	spinlock_t		io_lock;
	struct watchdog_device	cdns_wdt_device;
};

/* Write access to Registers */
static inline void cdns_wdt_writereg(struct cdns_wdt *wdt, u32 offset, u32 val)
{
	writel_relaxed(val, wdt->regs + offset);
}

/*************************Register Map**************************************/

/* Register Offsets for the WDT */
#define CDNS_WDT_ZMR_OFFSET	0x0	/* Zero Mode Register */
#define CDNS_WDT_CCR_OFFSET	0x4	/* Counter Control Register */
#define CDNS_WDT_RESTART_OFFSET	0x8	/* Restart Register */
#define CDNS_WDT_SR_OFFSET	0xC	/* Status Register */

/*
 * Zero Mode Register - This register controls how the time out is indicated
 * and also contains the access code to allow writes to the register (0xABC).
 */
#define CDNS_WDT_ZMR_WDEN_MASK	0x00000001 /* Enable the WDT */
#define CDNS_WDT_ZMR_RSTEN_MASK	0x00000002 /* Enable the reset output */
#define CDNS_WDT_ZMR_IRQEN_MASK	0x00000004 /* Enable IRQ output */
#define CDNS_WDT_ZMR_RSTLEN_16	0x00000030 /* Reset pulse of 16 pclk cycles */
#define CDNS_WDT_ZMR_ZKEY_VAL	0x00ABC000 /* Access key, 0xABC << 12 */
/*
 * Counter Control register - This register controls how fast the timer runs
 * and the reset value and also contains the access code to allow writes to
 * the register.
 */
#define CDNS_WDT_CCR_CRV_MASK	0x00003FFC /* Counter reset value */

/**
 * cdns_wdt_stop - Stop the watchdog.
 *
 * @wdd: watchdog device
 *
 * Read the contents of the ZMR register, clear the WDEN bit
 * in the register and set the access key for successful write.
 *
 * Return: always 0
 */
static int cdns_wdt_stop(struct watchdog_device *wdd)
{
	struct cdns_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->io_lock);
	cdns_wdt_writereg(wdt, CDNS_WDT_ZMR_OFFSET,
			  CDNS_WDT_ZMR_ZKEY_VAL & (~CDNS_WDT_ZMR_WDEN_MASK));
	spin_unlock(&wdt->io_lock);

	return 0;
}

/**
 * cdns_wdt_reload - Reload the watchdog timer (i.e. pat the watchdog).
 *
 * @wdd: watchdog device
 *
 * Write the restart key value (0x00001999) to the restart register.
 *
 * Return: always 0
 */
static int cdns_wdt_reload(struct watchdog_device *wdd)
{
	struct cdns_wdt *wdt = watchdog_get_drvdata(wdd);

	spin_lock(&wdt->io_lock);
	cdns_wdt_writereg(wdt, CDNS_WDT_RESTART_OFFSET,
			  CDNS_WDT_RESTART_KEY);
	spin_unlock(&wdt->io_lock);

	return 0;
}

/**
 * cdns_wdt_start - Enable and start the watchdog.
 *
 * @wdd: watchdog device
 *
 * The counter value is calculated according to the formula:
 *		calculated count = (timeout * clock) / prescaler + 1.
 * The calculated count is divided by 0x1000 to obtain the field value
 * to write to counter control register.
 * Clears the contents of prescaler and counter reset value. Sets the
 * prescaler to 4096 and the calculated count and access key
 * to write to CCR Register.
 * Sets the WDT (WDEN bit) and either the Reset signal(RSTEN bit)
 * or Interrupt signal(IRQEN) with a specified cycles and the access
 * key to write to ZMR Register.
 *
 * Return: always 0
 */
static int cdns_wdt_start(struct watchdog_device *wdd)
{
	struct cdns_wdt *wdt = watchdog_get_drvdata(wdd);
	unsigned int data = 0;
	unsigned short count;
	unsigned long clock_f = clk_get_rate(wdt->clk);

	/*
	 * Counter value divisor to obtain the value of
	 * counter reset to be written to control register.
	 */
	count = (wdd->timeout * (clock_f / wdt->prescaler)) /
		 CDNS_WDT_COUNTER_VALUE_DIVISOR + 1;

	if (count > CDNS_WDT_COUNTER_MAX)
		count = CDNS_WDT_COUNTER_MAX;

	spin_lock(&wdt->io_lock);
	cdns_wdt_writereg(wdt, CDNS_WDT_ZMR_OFFSET,
			  CDNS_WDT_ZMR_ZKEY_VAL);

	count = (count << 2) & CDNS_WDT_CCR_CRV_MASK;

	/* Write counter access key first to be able write to register */
	data = count | CDNS_WDT_REGISTER_ACCESS_KEY | wdt->ctrl_clksel;
	cdns_wdt_writereg(wdt, CDNS_WDT_CCR_OFFSET, data);
	data = CDNS_WDT_ZMR_WDEN_MASK | CDNS_WDT_ZMR_RSTLEN_16 |
	       CDNS_WDT_ZMR_ZKEY_VAL;

	/* Reset on timeout if specified in device tree. */
	if (wdt->rst) {
		data |= CDNS_WDT_ZMR_RSTEN_MASK;
		data &= ~CDNS_WDT_ZMR_IRQEN_MASK;
	} else {
		data &= ~CDNS_WDT_ZMR_RSTEN_MASK;
		data |= CDNS_WDT_ZMR_IRQEN_MASK;
	}
	cdns_wdt_writereg(wdt, CDNS_WDT_ZMR_OFFSET, data);
	cdns_wdt_writereg(wdt, CDNS_WDT_RESTART_OFFSET,
			  CDNS_WDT_RESTART_KEY);
	spin_unlock(&wdt->io_lock);

	return 0;
}

/**
 * cdns_wdt_settimeout - Set a new timeout value for the watchdog device.
 *
 * @wdd: watchdog device
 * @new_time: new timeout value that needs to be set
 * Return: 0 on success
 *
 * Update the watchdog_device timeout with new value which is used when
 * cdns_wdt_start is called.
 */
static int cdns_wdt_settimeout(struct watchdog_device *wdd,
			       unsigned int new_time)
{
	wdd->timeout = new_time;

	return cdns_wdt_start(wdd);
}

/**
 * cdns_wdt_irq_handler - Notifies of watchdog timeout.
 *
 * @irq: interrupt number
 * @dev_id: pointer to a platform device structure
 * Return: IRQ_HANDLED
 *
 * The handler is invoked when the watchdog times out and a
 * reset on timeout has not been enabled.
 */
static irqreturn_t cdns_wdt_irq_handler(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;

	dev_info(&pdev->dev,
		 "Watchdog timed out. Internal reset not enabled\n");

	return IRQ_HANDLED;
}

/*
 * Info structure used to indicate the features supported by the device
 * to the upper layers. This is defined in watchdog.h header file.
 */
static const struct watchdog_info cdns_wdt_info = {
	.identity	= "cdns_wdt watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

/* Watchdog Core Ops */
static const struct watchdog_ops cdns_wdt_ops = {
	.owner = THIS_MODULE,
	.start = cdns_wdt_start,
	.stop = cdns_wdt_stop,
	.ping = cdns_wdt_reload,
	.set_timeout = cdns_wdt_settimeout,
};

/************************Platform Operations*****************************/
/**
 * cdns_wdt_probe - Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Return: 0 on success, negative error otherwise.
 *
 * It does all the memory allocation and registration for the device.
 */
static int cdns_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret, irq;
	unsigned long clock_f;
	struct cdns_wdt *wdt;
	struct watchdog_device *cdns_wdt_device;

	wdt = devm_kzalloc(&pdev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	cdns_wdt_device = &wdt->cdns_wdt_device;
	cdns_wdt_device->info = &cdns_wdt_info;
	cdns_wdt_device->ops = &cdns_wdt_ops;
	cdns_wdt_device->timeout = CDNS_WDT_DEFAULT_TIMEOUT;
	cdns_wdt_device->min_timeout = CDNS_WDT_MIN_TIMEOUT;
	cdns_wdt_device->max_timeout = CDNS_WDT_MAX_TIMEOUT;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	wdt->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(wdt->regs))
		return PTR_ERR(wdt->regs);

	/* Register the interrupt */
	wdt->rst = of_property_read_bool(pdev->dev.of_node, "reset-on-timeout");
	irq = platform_get_irq(pdev, 0);
	if (!wdt->rst && irq >= 0) {
		ret = devm_request_irq(&pdev->dev, irq, cdns_wdt_irq_handler, 0,
				       pdev->name, pdev);
		if (ret) {
			dev_err(&pdev->dev,
				"cannot register interrupt handler err=%d\n",
				ret);
			return ret;
		}
	}

	/* Initialize the members of cdns_wdt structure */
	cdns_wdt_device->parent = &pdev->dev;

	ret = watchdog_init_timeout(cdns_wdt_device, wdt_timeout, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to set timeout value\n");
		return ret;
	}

	watchdog_set_nowayout(cdns_wdt_device, nowayout);
	watchdog_stop_on_reboot(cdns_wdt_device);
	watchdog_set_drvdata(cdns_wdt_device, wdt);

	wdt->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		ret = PTR_ERR(wdt->clk);
		return ret;
	}

	ret = clk_prepare_enable(wdt->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		return ret;
	}

	clock_f = clk_get_rate(wdt->clk);
	if (clock_f <= CDNS_WDT_CLK_75MHZ) {
		wdt->prescaler = CDNS_WDT_PRESCALE_512;
		wdt->ctrl_clksel = CDNS_WDT_PRESCALE_SELECT_512;
	} else {
		wdt->prescaler = CDNS_WDT_PRESCALE_4096;
		wdt->ctrl_clksel = CDNS_WDT_PRESCALE_SELECT_4096;
	}

	spin_lock_init(&wdt->io_lock);

	ret = watchdog_register_device(cdns_wdt_device);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register wdt device\n");
		goto err_clk_disable;
	}
	platform_set_drvdata(pdev, wdt);

	dev_dbg(&pdev->dev, "Xilinx Watchdog Timer at %p with timeout %ds%s\n",
		 wdt->regs, cdns_wdt_device->timeout,
		 nowayout ? ", nowayout" : "");

	return 0;

err_clk_disable:
	clk_disable_unprepare(wdt->clk);

	return ret;
}

/**
 * cdns_wdt_remove - Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Return: 0 on success, otherwise negative error.
 *
 * Unregister the device after releasing the resources.
 */
static int cdns_wdt_remove(struct platform_device *pdev)
{
	struct cdns_wdt *wdt = platform_get_drvdata(pdev);

	cdns_wdt_stop(&wdt->cdns_wdt_device);
	watchdog_unregister_device(&wdt->cdns_wdt_device);
	clk_disable_unprepare(wdt->clk);

	return 0;
}

/**
 * cdns_wdt_shutdown - Stop the device.
 *
 * @pdev: handle to the platform structure.
 *
 */
static void cdns_wdt_shutdown(struct platform_device *pdev)
{
	struct cdns_wdt *wdt = platform_get_drvdata(pdev);

	cdns_wdt_stop(&wdt->cdns_wdt_device);
	clk_disable_unprepare(wdt->clk);
}

/**
 * cdns_wdt_suspend - Stop the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 always.
 */
static int __maybe_unused cdns_wdt_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cdns_wdt *wdt = platform_get_drvdata(pdev);

	if (watchdog_active(&wdt->cdns_wdt_device)) {
		cdns_wdt_stop(&wdt->cdns_wdt_device);
		clk_disable_unprepare(wdt->clk);
	}

	return 0;
}

/**
 * cdns_wdt_resume - Resume the device.
 *
 * @dev: handle to the device structure.
 * Return: 0 on success, errno otherwise.
 */
static int __maybe_unused cdns_wdt_resume(struct device *dev)
{
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct cdns_wdt *wdt = platform_get_drvdata(pdev);

	if (watchdog_active(&wdt->cdns_wdt_device)) {
		ret = clk_prepare_enable(wdt->clk);
		if (ret) {
			dev_err(dev, "unable to enable clock\n");
			return ret;
		}
		cdns_wdt_start(&wdt->cdns_wdt_device);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(cdns_wdt_pm_ops, cdns_wdt_suspend, cdns_wdt_resume);

static const struct of_device_id cdns_wdt_of_match[] = {
	{ .compatible = "cdns,wdt-r1p2", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, cdns_wdt_of_match);

/* Driver Structure */
static struct platform_driver cdns_wdt_driver = {
	.probe		= cdns_wdt_probe,
	.remove		= cdns_wdt_remove,
	.shutdown	= cdns_wdt_shutdown,
	.driver		= {
		.name	= "cdns-wdt",
		.of_match_table = cdns_wdt_of_match,
		.pm	= &cdns_wdt_pm_ops,
	},
};

module_platform_driver(cdns_wdt_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Watchdog driver for Cadence WDT");
MODULE_LICENSE("GPL");
