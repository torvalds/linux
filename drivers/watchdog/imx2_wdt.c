/*
 * Watchdog driver for IMX2 and later processors
 *
 *  Copyright (C) 2010 Wolfram Sang, Pengutronix e.K. <w.sang@pengutronix.de>
 *
 * some parts adapted by similar drivers from Darius Augulis and Vladimir
 * Zapolskiy, additional improvements by Wim Van Sebroeck.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * NOTE: MX1 has a slightly different Watchdog than MX2 and later:
 *
 *			MX1:		MX2+:
 *			----		-----
 * Registers:		32-bit		16-bit
 * Stopable timer:	Yes		No
 * Need to enable clk:	No		Yes
 * Halt on suspend:	Manual		Can be automatic
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <mach/hardware.h>

#define DRIVER_NAME "imx2-wdt"

#define IMX2_WDT_WCR		0x00		/* Control Register */
#define IMX2_WDT_WCR_WT		(0xFF << 8)	/* -> Watchdog Timeout Field */
#define IMX2_WDT_WCR_WRE	(1 << 3)	/* -> WDOG Reset Enable */
#define IMX2_WDT_WCR_WDE	(1 << 2)	/* -> Watchdog Enable */

#define IMX2_WDT_WSR		0x02		/* Service Register */
#define IMX2_WDT_SEQ1		0x5555		/* -> service sequence 1 */
#define IMX2_WDT_SEQ2		0xAAAA		/* -> service sequence 2 */

#define IMX2_WDT_WRSR		0x04		/* Reset Status Register */
#define IMX2_WDT_WRSR_TOUT	(1 << 1)	/* -> Reset due to Timeout */

#define IMX2_WDT_MAX_TIME	128
#define IMX2_WDT_DEFAULT_TIME	60		/* in seconds */

#define WDOG_SEC_TO_COUNT(s)	((s * 2 - 1) << 8)

#define IMX2_WDT_STATUS_OPEN	0
#define IMX2_WDT_STATUS_STARTED	1
#define IMX2_WDT_EXPECT_CLOSE	2

static struct {
	struct clk *clk;
	void __iomem *base;
	unsigned timeout;
	unsigned long status;
	struct timer_list timer;	/* Pings the watchdog when closed */
} imx2_wdt;

static struct miscdevice imx2_wdt_miscdev;

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");


static unsigned timeout = IMX2_WDT_DEFAULT_TIME;
module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds (default="
				__MODULE_STRING(IMX2_WDT_DEFAULT_TIME) ")");

static const struct watchdog_info imx2_wdt_info = {
	.identity = "imx2+ watchdog",
	.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
};

static inline void imx2_wdt_setup(void)
{
	u16 val = __raw_readw(imx2_wdt.base + IMX2_WDT_WCR);

	/* Strip the old watchdog Time-Out value */
	val &= ~IMX2_WDT_WCR_WT;
	/* Generate reset if WDOG times out */
	val &= ~IMX2_WDT_WCR_WRE;
	/* Keep Watchdog Disabled */
	val &= ~IMX2_WDT_WCR_WDE;
	/* Set the watchdog's Time-Out value */
	val |= WDOG_SEC_TO_COUNT(imx2_wdt.timeout);

	__raw_writew(val, imx2_wdt.base + IMX2_WDT_WCR);

	/* enable the watchdog */
	val |= IMX2_WDT_WCR_WDE;
	__raw_writew(val, imx2_wdt.base + IMX2_WDT_WCR);
}

static inline void imx2_wdt_ping(void)
{
	__raw_writew(IMX2_WDT_SEQ1, imx2_wdt.base + IMX2_WDT_WSR);
	__raw_writew(IMX2_WDT_SEQ2, imx2_wdt.base + IMX2_WDT_WSR);
}

static void imx2_wdt_timer_ping(unsigned long arg)
{
	/* ping it every imx2_wdt.timeout / 2 seconds to prevent reboot */
	imx2_wdt_ping();
	mod_timer(&imx2_wdt.timer, jiffies + imx2_wdt.timeout * HZ / 2);
}

static void imx2_wdt_start(void)
{
	if (!test_and_set_bit(IMX2_WDT_STATUS_STARTED, &imx2_wdt.status)) {
		/* at our first start we enable clock and do initialisations */
		clk_prepare_enable(imx2_wdt.clk);

		imx2_wdt_setup();
	} else	/* delete the timer that pings the watchdog after close */
		del_timer_sync(&imx2_wdt.timer);

	/* Watchdog is enabled - time to reload the timeout value */
	imx2_wdt_ping();
}

static void imx2_wdt_stop(void)
{
	/* we don't need a clk_disable, it cannot be disabled once started.
	 * We use a timer to ping the watchdog while /dev/watchdog is closed */
	imx2_wdt_timer_ping(0);
}

static void imx2_wdt_set_timeout(int new_timeout)
{
	u16 val = __raw_readw(imx2_wdt.base + IMX2_WDT_WCR);

	/* set the new timeout value in the WSR */
	val &= ~IMX2_WDT_WCR_WT;
	val |= WDOG_SEC_TO_COUNT(new_timeout);
	__raw_writew(val, imx2_wdt.base + IMX2_WDT_WCR);
}

static int imx2_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(IMX2_WDT_STATUS_OPEN, &imx2_wdt.status))
		return -EBUSY;

	imx2_wdt_start();
	return nonseekable_open(inode, file);
}

static int imx2_wdt_close(struct inode *inode, struct file *file)
{
	if (test_bit(IMX2_WDT_EXPECT_CLOSE, &imx2_wdt.status) && !nowayout)
		imx2_wdt_stop();
	else {
		dev_crit(imx2_wdt_miscdev.parent,
			"Unexpected close: Expect reboot!\n");
		imx2_wdt_ping();
	}

	clear_bit(IMX2_WDT_EXPECT_CLOSE, &imx2_wdt.status);
	clear_bit(IMX2_WDT_STATUS_OPEN, &imx2_wdt.status);
	return 0;
}

static long imx2_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_value;
	u16 val;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &imx2_wdt_info,
			sizeof(struct watchdog_info)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
		return put_user(0, p);

	case WDIOC_GETBOOTSTATUS:
		val = __raw_readw(imx2_wdt.base + IMX2_WDT_WRSR);
		new_value = val & IMX2_WDT_WRSR_TOUT ? WDIOF_CARDRESET : 0;
		return put_user(new_value, p);

	case WDIOC_KEEPALIVE:
		imx2_wdt_ping();
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_value, p))
			return -EFAULT;
		if ((new_value < 1) || (new_value > IMX2_WDT_MAX_TIME))
			return -EINVAL;
		imx2_wdt_set_timeout(new_value);
		imx2_wdt.timeout = new_value;
		imx2_wdt_ping();

		/* Fallthrough to return current value */
	case WDIOC_GETTIMEOUT:
		return put_user(imx2_wdt.timeout, p);

	default:
		return -ENOTTY;
	}
}

static ssize_t imx2_wdt_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	size_t i;
	char c;

	if (len == 0)	/* Can we see this even ? */
		return 0;

	clear_bit(IMX2_WDT_EXPECT_CLOSE, &imx2_wdt.status);
	/* scan to see whether or not we got the magic character */
	for (i = 0; i != len; i++) {
		if (get_user(c, data + i))
			return -EFAULT;
		if (c == 'V')
			set_bit(IMX2_WDT_EXPECT_CLOSE, &imx2_wdt.status);
	}

	imx2_wdt_ping();
	return len;
}

static const struct file_operations imx2_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = imx2_wdt_ioctl,
	.open = imx2_wdt_open,
	.release = imx2_wdt_close,
	.write = imx2_wdt_write,
};

static struct miscdevice imx2_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &imx2_wdt_fops,
};

static int __init imx2_wdt_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get device resources\n");
		return -ENODEV;
	}

	imx2_wdt.base = devm_request_and_ioremap(&pdev->dev, res);
	if (!imx2_wdt.base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	imx2_wdt.clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(imx2_wdt.clk)) {
		dev_err(&pdev->dev, "can't get Watchdog clock\n");
		return PTR_ERR(imx2_wdt.clk);
	}

	imx2_wdt.timeout = clamp_t(unsigned, timeout, 1, IMX2_WDT_MAX_TIME);
	if (imx2_wdt.timeout != timeout)
		dev_warn(&pdev->dev, "Initial timeout out of range! "
			"Clamped from %u to %u\n", timeout, imx2_wdt.timeout);

	setup_timer(&imx2_wdt.timer, imx2_wdt_timer_ping, 0);

	imx2_wdt_miscdev.parent = &pdev->dev;
	ret = misc_register(&imx2_wdt_miscdev);
	if (ret)
		goto fail;

	dev_info(&pdev->dev,
		"IMX2+ Watchdog Timer enabled. timeout=%ds (nowayout=%d)\n",
						imx2_wdt.timeout, nowayout);
	return 0;

fail:
	imx2_wdt_miscdev.parent = NULL;
	clk_put(imx2_wdt.clk);
	return ret;
}

static int __exit imx2_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&imx2_wdt_miscdev);

	if (test_bit(IMX2_WDT_STATUS_STARTED, &imx2_wdt.status)) {
		del_timer_sync(&imx2_wdt.timer);

		dev_crit(imx2_wdt_miscdev.parent,
			"Device removed: Expect reboot!\n");
	} else
		clk_put(imx2_wdt.clk);

	imx2_wdt_miscdev.parent = NULL;
	return 0;
}

static void imx2_wdt_shutdown(struct platform_device *pdev)
{
	if (test_bit(IMX2_WDT_STATUS_STARTED, &imx2_wdt.status)) {
		/* we are running, we need to delete the timer but will give
		 * max timeout before reboot will take place */
		del_timer_sync(&imx2_wdt.timer);
		imx2_wdt_set_timeout(IMX2_WDT_MAX_TIME);
		imx2_wdt_ping();

		dev_crit(imx2_wdt_miscdev.parent,
			"Device shutdown: Expect reboot!\n");
	}
}

static const struct of_device_id imx2_wdt_dt_ids[] = {
	{ .compatible = "fsl,imx21-wdt", },
	{ /* sentinel */ }
};

static struct platform_driver imx2_wdt_driver = {
	.remove		= __exit_p(imx2_wdt_remove),
	.shutdown	= imx2_wdt_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = imx2_wdt_dt_ids,
	},
};

static int __init imx2_wdt_init(void)
{
	return platform_driver_probe(&imx2_wdt_driver, imx2_wdt_probe);
}
module_init(imx2_wdt_init);

static void __exit imx2_wdt_exit(void)
{
	platform_driver_unregister(&imx2_wdt_driver);
}
module_exit(imx2_wdt_exit);

MODULE_AUTHOR("Wolfram Sang");
MODULE_DESCRIPTION("Watchdog driver for IMX2 and later");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:" DRIVER_NAME);
