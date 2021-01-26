// SPDX-License-Identifier: GPL-2.0
/*
 * mv64x60_wdt.c - MV64X60 (Marvell Discovery) watchdog userspace interface
 *
 * Author: James Chapman <jchapman@katalix.com>
 *
 * Platform-specific setup code should configure the dog to generate
 * interrupt or reset as required.  This code only enables/disables
 * and services the watchdog.
 *
 * Derived from mpc8xx_wdt.c, with the following copyright.
 *
 * 2002 (c) Florian Schirmer <jolt@tuxbox.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>
#include <linux/mv643xx.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#define MV64x60_WDT_WDC_OFFSET	0

/*
 * The watchdog configuration register contains a pair of 2-bit fields,
 *   1.  a reload field, bits 27-26, which triggers a reload of
 *       the countdown register, and
 *   2.  an enable field, bits 25-24, which toggles between
 *       enabling and disabling the watchdog timer.
 * Bit 31 is a read-only field which indicates whether the
 * watchdog timer is currently enabled.
 *
 * The low 24 bits contain the timer reload value.
 */
#define MV64x60_WDC_ENABLE_SHIFT	24
#define MV64x60_WDC_SERVICE_SHIFT	26
#define MV64x60_WDC_ENABLED_SHIFT	31

#define MV64x60_WDC_ENABLED_TRUE	1
#define MV64x60_WDC_ENABLED_FALSE	0

/* Flags bits */
#define MV64x60_WDOG_FLAG_OPENED	0

static unsigned long wdt_flags;
static int wdt_status;
static void __iomem *mv64x60_wdt_regs;
static int mv64x60_wdt_timeout;
static int mv64x60_wdt_count;
static unsigned int bus_clk;
static char expect_close;
static DEFINE_SPINLOCK(mv64x60_wdt_spinlock);

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int mv64x60_wdt_toggle_wdc(int enabled_predicate, int field_shift)
{
	u32 data;
	u32 enabled;
	int ret = 0;

	spin_lock(&mv64x60_wdt_spinlock);
	data = readl(mv64x60_wdt_regs + MV64x60_WDT_WDC_OFFSET);
	enabled = (data >> MV64x60_WDC_ENABLED_SHIFT) & 1;

	/* only toggle the requested field if enabled state matches predicate */
	if ((enabled ^ enabled_predicate) == 0) {
		/* We write a 1, then a 2 -- to the appropriate field */
		data = (1 << field_shift) | mv64x60_wdt_count;
		writel(data, mv64x60_wdt_regs + MV64x60_WDT_WDC_OFFSET);

		data = (2 << field_shift) | mv64x60_wdt_count;
		writel(data, mv64x60_wdt_regs + MV64x60_WDT_WDC_OFFSET);
		ret = 1;
	}
	spin_unlock(&mv64x60_wdt_spinlock);

	return ret;
}

static void mv64x60_wdt_service(void)
{
	mv64x60_wdt_toggle_wdc(MV64x60_WDC_ENABLED_TRUE,
			       MV64x60_WDC_SERVICE_SHIFT);
}

static void mv64x60_wdt_handler_enable(void)
{
	if (mv64x60_wdt_toggle_wdc(MV64x60_WDC_ENABLED_FALSE,
				   MV64x60_WDC_ENABLE_SHIFT)) {
		mv64x60_wdt_service();
		pr_notice("watchdog activated\n");
	}
}

static void mv64x60_wdt_handler_disable(void)
{
	if (mv64x60_wdt_toggle_wdc(MV64x60_WDC_ENABLED_TRUE,
				   MV64x60_WDC_ENABLE_SHIFT))
		pr_notice("watchdog deactivated\n");
}

static void mv64x60_wdt_set_timeout(unsigned int timeout)
{
	/* maximum bus cycle count is 0xFFFFFFFF */
	if (timeout > 0xFFFFFFFF / bus_clk)
		timeout = 0xFFFFFFFF / bus_clk;

	mv64x60_wdt_count = timeout * bus_clk >> 8;
	mv64x60_wdt_timeout = timeout;
}

static int mv64x60_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(MV64x60_WDOG_FLAG_OPENED, &wdt_flags))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	mv64x60_wdt_handler_enable();

	return stream_open(inode, file);
}

static int mv64x60_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42)
		mv64x60_wdt_handler_disable();
	else {
		pr_crit("unexpected close, not stopping timer!\n");
		mv64x60_wdt_service();
	}
	expect_close = 0;

	clear_bit(MV64x60_WDOG_FLAG_OPENED, &wdt_flags);

	return 0;
}

static ssize_t mv64x60_wdt_write(struct file *file, const char __user *data,
				 size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		mv64x60_wdt_service();
	}

	return len;
}

static long mv64x60_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	int timeout;
	int options;
	void __user *argp = (void __user *)arg;
	static const struct watchdog_info info = {
		.options =	WDIOF_SETTIMEOUT	|
				WDIOF_MAGICCLOSE	|
				WDIOF_KEEPALIVEPING,
		.firmware_version = 0,
		.identity = "MV64x60 watchdog",
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &info, sizeof(info)))
			return -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(wdt_status, (int __user *)argp))
			return -EFAULT;
		wdt_status &= ~WDIOF_KEEPALIVEPING;
		break;

	case WDIOC_GETTEMP:
		return -EOPNOTSUPP;

	case WDIOC_SETOPTIONS:
		if (get_user(options, (int __user *)argp))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD)
			mv64x60_wdt_handler_disable();

		if (options & WDIOS_ENABLECARD)
			mv64x60_wdt_handler_enable();
		break;

	case WDIOC_KEEPALIVE:
		mv64x60_wdt_service();
		wdt_status |= WDIOF_KEEPALIVEPING;
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(timeout, (int __user *)argp))
			return -EFAULT;
		mv64x60_wdt_set_timeout(timeout);
		fallthrough;

	case WDIOC_GETTIMEOUT:
		if (put_user(mv64x60_wdt_timeout, (int __user *)argp))
			return -EFAULT;
		break;

	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations mv64x60_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = mv64x60_wdt_write,
	.unlocked_ioctl = mv64x60_wdt_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.open = mv64x60_wdt_open,
	.release = mv64x60_wdt_release,
};

static struct miscdevice mv64x60_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &mv64x60_wdt_fops,
};

static int mv64x60_wdt_probe(struct platform_device *dev)
{
	struct mv64x60_wdt_pdata *pdata = dev_get_platdata(&dev->dev);
	struct resource *r;
	int timeout = 10;

	bus_clk = 133;			/* in MHz */
	if (pdata) {
		timeout = pdata->timeout;
		bus_clk = pdata->bus_clk;
	}

	/* Since bus_clk is truncated MHz, actual frequency could be
	 * up to 1MHz higher.  Round up, since it's better to time out
	 * too late than too soon.
	 */
	bus_clk++;
	bus_clk *= 1000000;		/* convert to Hz */

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;

	mv64x60_wdt_regs = devm_ioremap(&dev->dev, r->start, resource_size(r));
	if (mv64x60_wdt_regs == NULL)
		return -ENOMEM;

	mv64x60_wdt_set_timeout(timeout);

	mv64x60_wdt_handler_disable();	/* in case timer was already running */

	return misc_register(&mv64x60_wdt_miscdev);
}

static int mv64x60_wdt_remove(struct platform_device *dev)
{
	misc_deregister(&mv64x60_wdt_miscdev);

	mv64x60_wdt_handler_disable();

	return 0;
}

static struct platform_driver mv64x60_wdt_driver = {
	.probe = mv64x60_wdt_probe,
	.remove = mv64x60_wdt_remove,
	.driver = {
		.name = MV64x60_WDT_NAME,
	},
};

static int __init mv64x60_wdt_init(void)
{
	pr_info("MV64x60 watchdog driver\n");

	return platform_driver_register(&mv64x60_wdt_driver);
}

static void __exit mv64x60_wdt_exit(void)
{
	platform_driver_unregister(&mv64x60_wdt_driver);
}

module_init(mv64x60_wdt_init);
module_exit(mv64x60_wdt_exit);

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("MV64x60 watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MV64x60_WDT_NAME);
