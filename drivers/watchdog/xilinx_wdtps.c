/*
 * Xilinx Zynq WDT driver
 *
 * Copyright (c) 2010-2013 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
 * 02139, USA.
 */

#include <linux/clk.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

#define XWDTPS_DEFAULT_TIMEOUT	10
/* Supports 1 - 516 sec */
#define XWDTPS_MIN_TIMEOUT	1
#define XWDTPS_MAX_TIMEOUT	516

static int wdt_timeout = XWDTPS_DEFAULT_TIMEOUT;
static int nowayout = WATCHDOG_NOWAYOUT;

module_param(wdt_timeout, int, 0);
MODULE_PARM_DESC(wdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(XWDTPS_DEFAULT_TIMEOUT) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

/**
 * struct xwdtps - Watchdog device structure.
 * @regs: baseaddress of device.
 * @busy: flag for the device.
 *
 * Structure containing parameters specific to ps watchdog.
 */
struct xwdtps {
	void __iomem		*regs;		/* Base address */
	unsigned long		busy;		/* Device Status */
	int			rst;		/* Reset flag */
	struct clk		*clk;
	u32			prescalar;
	u32			ctrl_clksel;
	spinlock_t		io_lock;
};
static struct xwdtps *wdt;

/*
 * Info structure used to indicate the features supported by the device
 * to the upper layers. This is defined in watchdog.h header file.
 */
static struct watchdog_info xwdtps_info = {
	.identity	= "xwdtps watchdog",
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
				WDIOF_MAGICCLOSE,
};

/* Write access to Registers */
#define xwdtps_writereg(val, offset) __raw_writel(val, (wdt->regs) + offset)

/*************************Register Map**************************************/

/* Register Offsets for the WDT */
#define XWDTPS_ZMR_OFFSET	0x0	/* Zero Mode Register */
#define XWDTPS_CCR_OFFSET	0x4	/* Counter Control Register */
#define XWDTPS_RESTART_OFFSET	0x8	/* Restart Register */
#define XWDTPS_SR_OFFSET	0xC	/* Status Register */

/*
 * Zero Mode Register - This register controls how the time out is indicated
 * and also contains the access code to allow writes to the register (0xABC).
 */
#define XWDTPS_ZMR_WDEN_MASK	0x00000001 /* Enable the WDT */
#define XWDTPS_ZMR_RSTEN_MASK	0x00000002 /* Enable the reset output */
#define XWDTPS_ZMR_IRQEN_MASK	0x00000004 /* Enable IRQ output */
#define XWDTPS_ZMR_RSTLEN_16	0x00000030 /* Reset pulse of 16 pclk cycles */
#define XWDTPS_ZMR_ZKEY_VAL	0x00ABC000 /* Access key, 0xABC << 12 */
/*
 * Counter Control register - This register controls how fast the timer runs
 * and the reset value and also contains the access code to allow writes to
 * the register.
 */
#define XWDTPS_CCR_CRV_MASK	0x00003FFC /* Counter reset value */

/**
 * xwdtps_stop -  Stop the watchdog.
 *
 * Read the contents of the ZMR register, clear the WDEN bit
 * in the register and set the access key for successful write.
 */
static int xwdtps_stop(struct watchdog_device *wdd)
{
	spin_lock(&wdt->io_lock);
	xwdtps_writereg((XWDTPS_ZMR_ZKEY_VAL & (~XWDTPS_ZMR_WDEN_MASK)),
			 XWDTPS_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
	return 0;
}

/**
 * xwdtps_reload -  Reload the watchdog timer (i.e. pat the watchdog).
 *
 * Write the restart key value (0x00001999) to the restart register.
 */
static int xwdtps_reload(struct watchdog_device *wdd)
{
	spin_lock(&wdt->io_lock);
	xwdtps_writereg(0x00001999, XWDTPS_RESTART_OFFSET);
	spin_unlock(&wdt->io_lock);
	return 0;
}

/**
 * xwdtps_start -  Enable and start the watchdog.
 *
 * The counter value is calculated according to the formula:
 *		calculated count = (timeout * clock) / prescalar + 1.
 * The calculated count is divided by 0x1000 to obtain the field value
 * to write to counter control register.
 * Clears the contents of prescalar and counter reset value. Sets the
 * prescalar to 4096 and the calculated count and access key
 * to write to CCR Register.
 * Sets the WDT (WDEN bit) and either the Reset signal(RSTEN bit)
 * or Interrupt signal(IRQEN) with a specified cycles and the access
 * key to write to ZMR Register.
 */
static int xwdtps_start(struct watchdog_device *wdd)
{
	unsigned int data = 0;
	unsigned short count;
	unsigned long clock_f = clk_get_rate(wdt->clk);

	/*
	 * 0x1000	- Counter Value Divide, to obtain the value of counter
	 *		  reset to write to control register.
	 */
	count = (wdd->timeout * (clock_f / (wdt->prescalar))) / 0x1000 + 1;

	/* Check for boundary conditions of counter value */
	if (count > 0xFFF)
		count = 0xFFF;

	spin_lock(&wdt->io_lock);
	xwdtps_writereg(XWDTPS_ZMR_ZKEY_VAL, XWDTPS_ZMR_OFFSET);

	/* Shift the count value to correct bit positions */
	count = (count << 2) & XWDTPS_CCR_CRV_MASK;

	/* 0x00920000 - Counter register key value. */
	data = (count | 0x00920000 | (wdt->ctrl_clksel));
	xwdtps_writereg(data, XWDTPS_CCR_OFFSET);
	data = XWDTPS_ZMR_WDEN_MASK | XWDTPS_ZMR_RSTLEN_16 |
			XWDTPS_ZMR_ZKEY_VAL;

	/* Reset on timeout if specified in device tree. */
	if (wdt->rst) {
		data |= XWDTPS_ZMR_RSTEN_MASK;
		data &= ~XWDTPS_ZMR_IRQEN_MASK;
	} else {
		data &= ~XWDTPS_ZMR_RSTEN_MASK;
		data |= XWDTPS_ZMR_IRQEN_MASK;
	}
	xwdtps_writereg(data, XWDTPS_ZMR_OFFSET);
	spin_unlock(&wdt->io_lock);
	xwdtps_writereg(0x00001999, XWDTPS_RESTART_OFFSET);
	return 0;
}

/**
 * xwdtps_settimeout -  Set a new timeout value for the watchdog device.
 *
 * @new_time: new timeout value that needs to be set.
 * Returns 0 on success.
 *
 * Update the watchdog_device timeout with new value which is used when
 * xwdtps_start is called.
 */
static int xwdtps_settimeout(struct watchdog_device *wdd, unsigned int new_time)
{
	wdd->timeout = new_time;
	return xwdtps_start(wdd);
}

/**
 * xwdtps_irq_handler - Notifies of watchdog timeout.
 *
 * @irq: interrupt number
 * @dev_id: pointer to a platform device structure
 * Returns IRQ_HANDLED
 *
 * The handler is invoked when the watchdog times out and a
 * reset on timeout has not been enabled.
 */
static irqreturn_t xwdtps_irq_handler(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	dev_info(&pdev->dev, "Watchdog timed out.\n");
	return IRQ_HANDLED;
}

/* Watchdog Core Ops */
static struct watchdog_ops xwdtps_ops = {
	.owner = THIS_MODULE,
	.start = xwdtps_start,
	.stop = xwdtps_stop,
	.ping = xwdtps_reload,
	.set_timeout = xwdtps_settimeout,
};

/* Watchdog Core Device */
static struct watchdog_device xwdtps_device = {
	.info = &xwdtps_info,
	.ops = &xwdtps_ops,
	.timeout = XWDTPS_DEFAULT_TIMEOUT,
	.min_timeout = XWDTPS_MIN_TIMEOUT,
	.max_timeout = XWDTPS_MAX_TIMEOUT,
};

/**
 * xwdtps_notify_sys -  Notifier for reboot or shutdown.
 *
 * @this: handle to notifier block.
 * @code: turn off indicator.
 * @unused: unused.
 * Returns NOTIFY_DONE.
 *
 * This notifier is invoked whenever the system reboot or shutdown occur
 * because we need to disable the WDT before system goes down as WDT might
 * reset on the next boot.
 */
static int xwdtps_notify_sys(struct notifier_block *this, unsigned long code,
			      void *unused)
{
	if (code == SYS_DOWN || code == SYS_HALT)
		/* Stop the watchdog */
		xwdtps_stop(&xwdtps_device);
	return NOTIFY_DONE;
}

/* Notifier Structure */
static struct notifier_block xwdtps_notifier = {
	.notifier_call = xwdtps_notify_sys,
};

/************************Platform Operations*****************************/
/**
 * xwdtps_probe -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Returns 0 on success, negative error otherwise.
 *
 * It does all the memory allocation and registration for the device.
 */
static int xwdtps_probe(struct platform_device *pdev)
{
	struct resource *regs;
	int res;
	const void *prop;
	int irq;
	unsigned long clock_f;

	/* Check whether WDT is in use, just for safety */
	if (wdt) {
		dev_err(&pdev->dev,
			    "Device Busy, only 1 xwdtps instance supported.\n");
		return -EBUSY;
	}

	/* Get the device base address */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "Unable to locate mmio resource\n");
		return -ENODEV;
	}

	/* Allocate an instance of the xwdtps structure */
	wdt = kzalloc(sizeof(*wdt), GFP_KERNEL);
	if (!wdt) {
		dev_err(&pdev->dev, "No memory for wdt structure\n");
		return -ENOMEM;
	}

	wdt->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!wdt->regs) {
		res = -ENOMEM;
		dev_err(&pdev->dev, "Could not map I/O memory\n");
		goto err_free;
	}

	/* Register the reboot notifier */
	res = register_reboot_notifier(&xwdtps_notifier);
	if (res != 0) {
		dev_err(&pdev->dev, "cannot register reboot notifier err=%d)\n",
			res);
		goto err_iounmap;
	}

	/* Register the interrupt */
	prop = of_get_property(pdev->dev.of_node, "reset", NULL);
	wdt->rst = prop ? be32_to_cpup(prop) : 0;
	irq = platform_get_irq(pdev, 0);
	if (!wdt->rst && irq >= 0) {
		res = request_irq(irq, xwdtps_irq_handler, 0, pdev->name, pdev);
		if (res) {
			dev_err(&pdev->dev,
				   "cannot register interrupt handler err=%d\n",
				   res);
			goto err_notifier;
		}
	}

	/* Initialize the members of xwdtps structure */
	xwdtps_device.parent = &pdev->dev;
	prop = of_get_property(pdev->dev.of_node, "timeout", NULL);
	if (prop) {
		xwdtps_device.timeout = be32_to_cpup(prop);
	} else if (wdt_timeout < XWDTPS_MAX_TIMEOUT &&
			wdt_timeout > XWDTPS_MIN_TIMEOUT) {
		xwdtps_device.timeout = wdt_timeout;
	} else {
		dev_info(&pdev->dev,
			    "timeout limited to 1 - %d sec, using default=%d\n",
			    XWDTPS_MAX_TIMEOUT, XWDTPS_DEFAULT_TIMEOUT);
		xwdtps_device.timeout = XWDTPS_DEFAULT_TIMEOUT;
	}

	watchdog_set_nowayout(&xwdtps_device, nowayout);
	watchdog_set_drvdata(&xwdtps_device, &wdt);

	wdt->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_err(&pdev->dev, "input clock not found\n");
		res = PTR_ERR(wdt->clk);
		goto err_irq;
	}

	res = clk_prepare_enable(wdt->clk);
	if (res) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		goto err_clk_put;
	}

	clock_f = clk_get_rate(wdt->clk);
	if (clock_f <= 10000000) {/* For PEEP */
		wdt->prescalar = 64;
		wdt->ctrl_clksel = 1;
	} else if (clock_f <= 75000000) {
		wdt->prescalar = 256;
		wdt->ctrl_clksel = 2;
	} else { /* For Zynq */
		wdt->prescalar = 4096;
		wdt->ctrl_clksel = 3;
	}

	/* Initialize the busy flag to zero */
	clear_bit(0, &wdt->busy);
	spin_lock_init(&wdt->io_lock);

	/* Register the WDT */
	res = watchdog_register_device(&xwdtps_device);
	if (res) {
		dev_err(&pdev->dev, "Failed to register wdt device\n");
		goto err_clk_disable;
	}
	platform_set_drvdata(pdev, wdt);

	dev_info(&pdev->dev, "Xilinx Watchdog Timer at %p with timeout %ds%s\n",
		wdt->regs, xwdtps_device.timeout, nowayout ? ", nowayout" : "");

	return 0;

err_clk_disable:
	clk_disable_unprepare(wdt->clk);
err_clk_put:
	clk_put(wdt->clk);
err_irq:
	free_irq(irq, pdev);
err_notifier:
	unregister_reboot_notifier(&xwdtps_notifier);
err_iounmap:
	iounmap(wdt->regs);
err_free:
	kfree(wdt);
	wdt = NULL;
	return res;
}

/**
 * xwdtps_remove -  Probe call for the device.
 *
 * @pdev: handle to the platform device structure.
 * Returns 0 on success, otherwise negative error.
 *
 * Unregister the device after releasing the resources.
 * Stop is allowed only when nowayout is disabled.
 */
static int __exit xwdtps_remove(struct platform_device *pdev)
{
	int res = 0;
	int irq;

	if (wdt && !nowayout) {
		xwdtps_stop(&xwdtps_device);
		watchdog_unregister_device(&xwdtps_device);
		unregister_reboot_notifier(&xwdtps_notifier);
		irq = platform_get_irq(pdev, 0);
		free_irq(irq, pdev);
		iounmap(wdt->regs);
		clk_disable_unprepare(wdt->clk);
		clk_put(wdt->clk);
		kfree(wdt);
		wdt = NULL;
		platform_set_drvdata(pdev, NULL);
	} else {
		dev_err(&pdev->dev, "Cannot stop watchdog, still ticking\n");
		return -ENOTSUPP;
	}
	return res;
}

/**
 * xwdtps_shutdown -  Stop the device.
 *
 * @pdev: handle to the platform structure.
 *
 */
static void xwdtps_shutdown(struct platform_device *pdev)
{
	/* Stop the device */
	xwdtps_stop(&xwdtps_device);
	clk_disable_unprepare(wdt->clk);
	clk_put(wdt->clk);
}

#ifdef CONFIG_PM_SLEEP
/**
 * xwdtps_suspend -  Stop the device.
 *
 * @dev: handle to the device structure.
 * Returns 0 always.
 */
static int xwdtps_suspend(struct device *dev)
{
	/* Stop the device */
	xwdtps_stop(&xwdtps_device);
	clk_disable(wdt->clk);
	return 0;
}

/**
 * xwdtps_resume -  Resume the device.
 *
 * @dev: handle to the device structure.
 * Returns 0 on success, errno otherwise.
 */
static int xwdtps_resume(struct device *dev)
{
	int ret;

	ret = clk_enable(wdt->clk);
	if (ret) {
		dev_err(dev, "unable to enable clock\n");
		return ret;
	}
	/* Start the device */
	xwdtps_start(&xwdtps_device);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(xwdtps_pm_ops, xwdtps_suspend, xwdtps_resume);

static struct of_device_id xwdtps_of_match[] = {
	{ .compatible = "xlnx,ps7-wdt-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xwdtps_of_match);

/* Driver Structure */
static struct platform_driver xwdtps_driver = {
	.probe		= xwdtps_probe,
	.remove		= xwdtps_remove,
	.shutdown	= xwdtps_shutdown,
	.driver		= {
		.name	= "xwdtps",
		.owner	= THIS_MODULE,
		.of_match_table = xwdtps_of_match,
		.pm	= &xwdtps_pm_ops,
	},
};

/**
 * xwdtps_init -  Register the WDT.
 *
 * Returns 0 on success, otherwise negative error.
 *
 * If using noway out, the use count will be incremented.
 * This will prevent unloading the module. An attempt to
 * unload the module will result in a warning from the kernel.
 */
static int __init xwdtps_init(void)
{
	int res = platform_driver_register(&xwdtps_driver);
	if (!res && nowayout)
		try_module_get(THIS_MODULE);
	return res;
}

/**
 * xwdtps_exit -  Unregister the WDT.
 */
static void __exit xwdtps_exit(void)
{
	platform_driver_unregister(&xwdtps_driver);
}

module_init(xwdtps_init);
module_exit(xwdtps_exit);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Watchdog driver for PS WDT");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: xwdtps");
