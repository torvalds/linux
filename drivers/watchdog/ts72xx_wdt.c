/*
 * Watchdog driver for Technologic Systems TS-72xx based SBCs
 * (TS-7200, TS-7250 and TS-7260). These boards have external
 * glue logic CPLD chip, which includes programmable watchdog
 * timer.
 *
 * Copyright (c) 2009 Mika Westerberg <mika.westerberg@iki.fi>
 *
 * This driver is based on ep93xx_wdt and wm831x_wdt drivers.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/fs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>

#define TS72XX_WDT_FEED_VAL		0x05
#define TS72XX_WDT_DEFAULT_TIMEOUT	8

static int timeout = TS72XX_WDT_DEFAULT_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds. "
			  "(1 <= timeout <= 8, default="
			  __MODULE_STRING(TS72XX_WDT_DEFAULT_TIMEOUT)
			  ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

/**
 * struct ts72xx_wdt - watchdog control structure
 * @lock: lock that protects this structure
 * @regval: watchdog timeout value suitable for control register
 * @flags: flags controlling watchdog device state
 * @control_reg: watchdog control register
 * @feed_reg: watchdog feed register
 * @pdev: back pointer to platform dev
 */
struct ts72xx_wdt {
	struct mutex	lock;
	int		regval;

#define TS72XX_WDT_BUSY_FLAG		1
#define TS72XX_WDT_EXPECT_CLOSE_FLAG	2
	int		flags;

	void __iomem	*control_reg;
	void __iomem	*feed_reg;

	struct platform_device *pdev;
};

struct platform_device *ts72xx_wdt_pdev;

/*
 * TS-72xx Watchdog supports following timeouts (value written
 * to control register):
 *	value	description
 *	-------------------------
 *	0x00	watchdog disabled
 *	0x01	250ms
 *	0x02	500ms
 *	0x03	1s
 *	0x04	reserved
 *	0x05	2s
 *	0x06	4s
 *	0x07	8s
 *
 * Timeouts below 1s are not very usable so we don't
 * allow them at all.
 *
 * We provide two functions that convert between these:
 * timeout_to_regval() and regval_to_timeout().
 */
static const struct {
	int	timeout;
	int	regval;
} ts72xx_wdt_map[] = {
	{ 1, 3 },
	{ 2, 5 },
	{ 4, 6 },
	{ 8, 7 },
};

/**
 * timeout_to_regval() - converts given timeout to control register value
 * @new_timeout: timeout in seconds to be converted
 *
 * Function converts given @new_timeout into valid value that can
 * be programmed into watchdog control register. When conversion is
 * not possible, function returns %-EINVAL.
 */
static int timeout_to_regval(int new_timeout)
{
	int i;

	/* first limit it to 1 - 8 seconds */
	new_timeout = clamp_val(new_timeout, 1, 8);

	for (i = 0; i < ARRAY_SIZE(ts72xx_wdt_map); i++) {
		if (ts72xx_wdt_map[i].timeout >= new_timeout)
			return ts72xx_wdt_map[i].regval;
	}

	return -EINVAL;
}

/**
 * regval_to_timeout() - converts control register value to timeout
 * @regval: control register value to be converted
 *
 * Function converts given @regval to timeout in seconds (1, 2, 4 or 8).
 * If @regval cannot be converted, function returns %-EINVAL.
 */
static int regval_to_timeout(int regval)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ts72xx_wdt_map); i++) {
		if (ts72xx_wdt_map[i].regval == regval)
			return ts72xx_wdt_map[i].timeout;
	}

	return -EINVAL;
}

/**
 * ts72xx_wdt_kick() - kick the watchdog
 * @wdt: watchdog to be kicked
 *
 * Called with @wdt->lock held.
 */
static inline void ts72xx_wdt_kick(struct ts72xx_wdt *wdt)
{
	__raw_writeb(TS72XX_WDT_FEED_VAL, wdt->feed_reg);
}

/**
 * ts72xx_wdt_start() - starts the watchdog timer
 * @wdt: watchdog to be started
 *
 * This function programs timeout to watchdog timer
 * and starts it.
 *
 * Called with @wdt->lock held.
 */
static void ts72xx_wdt_start(struct ts72xx_wdt *wdt)
{
	/*
	 * To program the wdt, it first must be "fed" and
	 * only after that (within 30 usecs) the configuration
	 * can be changed.
	 */
	ts72xx_wdt_kick(wdt);
	__raw_writeb((u8)wdt->regval, wdt->control_reg);
}

/**
 * ts72xx_wdt_stop() - stops the watchdog timer
 * @wdt: watchdog to be stopped
 *
 * Called with @wdt->lock held.
 */
static void ts72xx_wdt_stop(struct ts72xx_wdt *wdt)
{
	ts72xx_wdt_kick(wdt);
	__raw_writeb(0, wdt->control_reg);
}

static int ts72xx_wdt_open(struct inode *inode, struct file *file)
{
	struct ts72xx_wdt *wdt = platform_get_drvdata(ts72xx_wdt_pdev);
	int regval;

	/*
	 * Try to convert default timeout to valid register
	 * value first.
	 */
	regval = timeout_to_regval(timeout);
	if (regval < 0) {
		dev_err(&wdt->pdev->dev,
			"failed to convert timeout (%d) to register value\n",
			timeout);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&wdt->lock))
		return -ERESTARTSYS;

	if ((wdt->flags & TS72XX_WDT_BUSY_FLAG) != 0) {
		mutex_unlock(&wdt->lock);
		return -EBUSY;
	}

	wdt->flags = TS72XX_WDT_BUSY_FLAG;
	wdt->regval = regval;
	file->private_data = wdt;

	ts72xx_wdt_start(wdt);

	mutex_unlock(&wdt->lock);
	return nonseekable_open(inode, file);
}

static int ts72xx_wdt_release(struct inode *inode, struct file *file)
{
	struct ts72xx_wdt *wdt = file->private_data;

	if (mutex_lock_interruptible(&wdt->lock))
		return -ERESTARTSYS;

	if ((wdt->flags & TS72XX_WDT_EXPECT_CLOSE_FLAG) != 0) {
		ts72xx_wdt_stop(wdt);
	} else {
		dev_warn(&wdt->pdev->dev,
			 "TS-72XX WDT device closed unexpectly. "
			 "Watchdog timer will not stop!\n");
		/*
		 * Kick it one more time, to give userland some time
		 * to recover (for example, respawning the kicker
		 * daemon).
		 */
		ts72xx_wdt_kick(wdt);
	}

	wdt->flags = 0;

	mutex_unlock(&wdt->lock);
	return 0;
}

static ssize_t ts72xx_wdt_write(struct file *file,
				const char __user *data,
				size_t len,
				loff_t *ppos)
{
	struct ts72xx_wdt *wdt = file->private_data;

	if (!len)
		return 0;

	if (mutex_lock_interruptible(&wdt->lock))
		return -ERESTARTSYS;

	ts72xx_wdt_kick(wdt);

	/*
	 * Support for magic character closing. User process
	 * writes 'V' into the device, just before it is closed.
	 * This means that we know that the wdt timer can be
	 * stopped after user closes the device.
	 */
	if (!nowayout) {
		int i;

		for (i = 0; i < len; i++) {
			char c;

			/* In case it was set long ago */
			wdt->flags &= ~TS72XX_WDT_EXPECT_CLOSE_FLAG;

			if (get_user(c, data + i)) {
				mutex_unlock(&wdt->lock);
				return -EFAULT;
			}
			if (c == 'V') {
				wdt->flags |= TS72XX_WDT_EXPECT_CLOSE_FLAG;
				break;
			}
		}
	}

	mutex_unlock(&wdt->lock);
	return len;
}

static const struct watchdog_info winfo = {
	.options		= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
				  WDIOF_MAGICCLOSE,
	.firmware_version	= 1,
	.identity		= "TS-72XX WDT",
};

static long ts72xx_wdt_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct ts72xx_wdt *wdt = file->private_data;
	void __user *argp = (void __user *)arg;
	int __user *p = (int __user *)argp;
	int error = 0;

	if (mutex_lock_interruptible(&wdt->lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		error = copy_to_user(argp, &winfo, sizeof(winfo));
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_KEEPALIVE:
		ts72xx_wdt_kick(wdt);
		break;

	case WDIOC_SETOPTIONS: {
		int options;

		if (get_user(options, p)) {
			error = -EFAULT;
			break;
		}

		error = -EINVAL;

		if ((options & WDIOS_DISABLECARD) != 0) {
			ts72xx_wdt_stop(wdt);
			error = 0;
		}
		if ((options & WDIOS_ENABLECARD) != 0) {
			ts72xx_wdt_start(wdt);
			error = 0;
		}

		break;
	}

	case WDIOC_SETTIMEOUT: {
		int new_timeout;

		if (get_user(new_timeout, p)) {
			error = -EFAULT;
		} else {
			int regval;

			regval = timeout_to_regval(new_timeout);
			if (regval < 0) {
				error = -EINVAL;
			} else {
				ts72xx_wdt_stop(wdt);
				wdt->regval = regval;
				ts72xx_wdt_start(wdt);
			}
		}
		if (error)
			break;

		/*FALLTHROUGH*/
	}

	case WDIOC_GETTIMEOUT:
		if (put_user(regval_to_timeout(wdt->regval), p))
			error = -EFAULT;
		break;

	default:
		error = -ENOTTY;
		break;
	}

	mutex_unlock(&wdt->lock);
	return error;
}

static const struct file_operations ts72xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= ts72xx_wdt_open,
	.release	= ts72xx_wdt_release,
	.write		= ts72xx_wdt_write,
	.unlocked_ioctl	= ts72xx_wdt_ioctl,
};

static struct miscdevice ts72xx_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &ts72xx_wdt_fops,
};

static int ts72xx_wdt_probe(struct platform_device *pdev)
{
	struct ts72xx_wdt *wdt;
	struct resource *r1, *r2;
	int error = 0;

	wdt = devm_kzalloc(&pdev->dev, sizeof(struct ts72xx_wdt), GFP_KERNEL);
	if (!wdt) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	r1 = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r1) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENODEV;
	}

	wdt->control_reg = devm_ioremap_resource(&pdev->dev, r1);
	if (IS_ERR(wdt->control_reg))
		return PTR_ERR(wdt->control_reg);

	r2 = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!r2) {
		dev_err(&pdev->dev, "failed to get memory resource\n");
		return -ENODEV;
	}

	wdt->feed_reg = devm_ioremap_resource(&pdev->dev, r2);
	if (IS_ERR(wdt->feed_reg))
		return PTR_ERR(wdt->feed_reg);

	platform_set_drvdata(pdev, wdt);
	ts72xx_wdt_pdev = pdev;
	wdt->pdev = pdev;
	mutex_init(&wdt->lock);

	/* make sure that the watchdog is disabled */
	ts72xx_wdt_stop(wdt);

	error = misc_register(&ts72xx_wdt_miscdev);
	if (error) {
		dev_err(&pdev->dev, "failed to register miscdev\n");
		return error;
	}

	dev_info(&pdev->dev, "TS-72xx Watchdog driver\n");

	return 0;
}

static int ts72xx_wdt_remove(struct platform_device *pdev)
{
	int error;

	error = misc_deregister(&ts72xx_wdt_miscdev);

	return error;
}

static struct platform_driver ts72xx_wdt_driver = {
	.probe		= ts72xx_wdt_probe,
	.remove		= ts72xx_wdt_remove,
	.driver		= {
		.name	= "ts72xx-wdt",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(ts72xx_wdt_driver);

MODULE_AUTHOR("Mika Westerberg <mika.westerberg@iki.fi>");
MODULE_DESCRIPTION("TS-72xx SBC Watchdog");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ts72xx-wdt");
