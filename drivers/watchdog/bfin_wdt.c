/*
 * Blackfin On-Chip Watchdog Driver
 *
 * Originally based on softdog.c
 * Copyright 2006-2010 Analog Devices Inc.
 * Copyright 2006-2007 Michele d'Amico
 * Copyright 1996 Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/blackfin.h>
#include <asm/bfin_watchdog.h>

#define stamp(fmt, args...) \
	pr_debug("%s:%i: " fmt "\n", __func__, __LINE__, ## args)
#define stampit() stamp("here i am")

#define WATCHDOG_NAME "bfin-wdt"

/* The BF561 has two watchdogs (one per core), but since Linux
 * only runs on core A, we'll just work with that one.
 */
#ifdef BF561_FAMILY
# define bfin_read_WDOG_CTL()    bfin_read_WDOGA_CTL()
# define bfin_read_WDOG_CNT()    bfin_read_WDOGA_CNT()
# define bfin_read_WDOG_STAT()   bfin_read_WDOGA_STAT()
# define bfin_write_WDOG_CTL(x)  bfin_write_WDOGA_CTL(x)
# define bfin_write_WDOG_CNT(x)  bfin_write_WDOGA_CNT(x)
# define bfin_write_WDOG_STAT(x) bfin_write_WDOGA_STAT(x)
#endif

/* some defaults */
#define WATCHDOG_TIMEOUT 20

static unsigned int timeout = WATCHDOG_TIMEOUT;
static bool nowayout = WATCHDOG_NOWAYOUT;
static const struct watchdog_info bfin_wdt_info;
static unsigned long open_check;
static char expect_close;
static DEFINE_SPINLOCK(bfin_wdt_spinlock);

/**
 *	bfin_wdt_keepalive - Keep the Userspace Watchdog Alive
 *
 *	The Userspace watchdog got a KeepAlive: schedule the next timeout.
 */
static int bfin_wdt_keepalive(void)
{
	stampit();
	bfin_write_WDOG_STAT(0);
	return 0;
}

/**
 *	bfin_wdt_stop - Stop the Watchdog
 *
 *	Stops the on-chip watchdog.
 */
static int bfin_wdt_stop(void)
{
	stampit();
	bfin_write_WDOG_CTL(WDEN_DISABLE);
	return 0;
}

/**
 *	bfin_wdt_start - Start the Watchdog
 *
 *	Starts the on-chip watchdog.  Automatically loads WDOG_CNT
 *	into WDOG_STAT for us.
 */
static int bfin_wdt_start(void)
{
	stampit();
	bfin_write_WDOG_CTL(WDEN_ENABLE | ICTL_RESET);
	return 0;
}

/**
 *	bfin_wdt_running - Check Watchdog status
 *
 *	See if the watchdog is running.
 */
static int bfin_wdt_running(void)
{
	stampit();
	return ((bfin_read_WDOG_CTL() & WDEN_MASK) != WDEN_DISABLE);
}

/**
 *	bfin_wdt_set_timeout - Set the Userspace Watchdog timeout
 *	@t: new timeout value (in seconds)
 *
 *	Translate the specified timeout in seconds into System Clock
 *	terms which is what the on-chip Watchdog requires.
 */
static int bfin_wdt_set_timeout(unsigned long t)
{
	u32 cnt, max_t, sclk;
	unsigned long flags;

	sclk = get_sclk();
	max_t = -1 / sclk;
	cnt = t * sclk;
	stamp("maxtimeout=%us newtimeout=%lus (cnt=%#x)", max_t, t, cnt);

	if (t > max_t) {
		pr_warn("timeout value is too large\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&bfin_wdt_spinlock, flags);
	{
		int run = bfin_wdt_running();
		bfin_wdt_stop();
		bfin_write_WDOG_CNT(cnt);
		if (run)
			bfin_wdt_start();
	}
	spin_unlock_irqrestore(&bfin_wdt_spinlock, flags);

	timeout = t;

	return 0;
}

/**
 *	bfin_wdt_open - Open the Device
 *	@inode: inode of device
 *	@file: file handle of device
 *
 *	Watchdog device is opened and started.
 */
static int bfin_wdt_open(struct inode *inode, struct file *file)
{
	stampit();

	if (test_and_set_bit(0, &open_check))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	bfin_wdt_keepalive();
	bfin_wdt_start();

	return nonseekable_open(inode, file);
}

/**
 *	bfin_wdt_close - Close the Device
 *	@inode: inode of device
 *	@file: file handle of device
 *
 *	Watchdog device is closed and stopped.
 */
static int bfin_wdt_release(struct inode *inode, struct file *file)
{
	stampit();

	if (expect_close == 42)
		bfin_wdt_stop();
	else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		bfin_wdt_keepalive();
	}
	expect_close = 0;
	clear_bit(0, &open_check);
	return 0;
}

/**
 *	bfin_wdt_write - Write to Device
 *	@file: file handle of device
 *	@buf: buffer to write
 *	@count: length of buffer
 *	@ppos: offset
 *
 *	Pings the watchdog on write.
 */
static ssize_t bfin_wdt_write(struct file *file, const char __user *data,
						size_t len, loff_t *ppos)
{
	stampit();

	if (len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		bfin_wdt_keepalive();
	}

	return len;
}

/**
 *	bfin_wdt_ioctl - Query Device
 *	@file: file handle of device
 *	@cmd: watchdog command
 *	@arg: argument
 *
 *	Query basic information from the device or ping it, as outlined by the
 *	watchdog API.
 */
static long bfin_wdt_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	stampit();

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user(argp, &bfin_wdt_info, sizeof(bfin_wdt_info)))
			return -EFAULT;
		else
			return 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(!!(_bfin_swrst & SWRST_RESET_WDOG), p);
	case WDIOC_SETOPTIONS: {
		unsigned long flags;
		int options, ret = -EINVAL;

		if (get_user(options, p))
			return -EFAULT;

		spin_lock_irqsave(&bfin_wdt_spinlock, flags);
		if (options & WDIOS_DISABLECARD) {
			bfin_wdt_stop();
			ret = 0;
		}
		if (options & WDIOS_ENABLECARD) {
			bfin_wdt_start();
			ret = 0;
		}
		spin_unlock_irqrestore(&bfin_wdt_spinlock, flags);
		return ret;
	}
	case WDIOC_KEEPALIVE:
		bfin_wdt_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT: {
		int new_timeout;

		if (get_user(new_timeout, p))
			return -EFAULT;
		if (bfin_wdt_set_timeout(new_timeout))
			return -EINVAL;
	}
	/* Fall */
	case WDIOC_GETTIMEOUT:
		return put_user(timeout, p);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_PM
static int state_before_suspend;

/**
 *	bfin_wdt_suspend - suspend the watchdog
 *	@pdev: device being suspended
 *	@state: requested suspend state
 *
 *	Remember if the watchdog was running and stop it.
 *	TODO: is this even right?  Doesn't seem to be any
 *	      standard in the watchdog world ...
 */
static int bfin_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	stampit();

	state_before_suspend = bfin_wdt_running();
	bfin_wdt_stop();

	return 0;
}

/**
 *	bfin_wdt_resume - resume the watchdog
 *	@pdev: device being resumed
 *
 *	If the watchdog was running, turn it back on.
 */
static int bfin_wdt_resume(struct platform_device *pdev)
{
	stampit();

	if (state_before_suspend) {
		bfin_wdt_set_timeout(timeout);
		bfin_wdt_start();
	}

	return 0;
}
#else
# define bfin_wdt_suspend NULL
# define bfin_wdt_resume NULL
#endif

static const struct file_operations bfin_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= bfin_wdt_write,
	.unlocked_ioctl	= bfin_wdt_ioctl,
	.open		= bfin_wdt_open,
	.release	= bfin_wdt_release,
};

static struct miscdevice bfin_wdt_miscdev = {
	.minor    = WATCHDOG_MINOR,
	.name     = "watchdog",
	.fops     = &bfin_wdt_fops,
};

static const struct watchdog_info bfin_wdt_info = {
	.identity = "Blackfin Watchdog",
	.options  = WDIOF_SETTIMEOUT |
		    WDIOF_KEEPALIVEPING |
		    WDIOF_MAGICCLOSE,
};

/**
 *	bfin_wdt_probe - Initialize module
 *
 *	Registers the misc device.  Actual device
 *	initialization is handled by bfin_wdt_open().
 */
static int __devinit bfin_wdt_probe(struct platform_device *pdev)
{
	int ret;

	ret = misc_register(&bfin_wdt_miscdev);
	if (ret) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		return ret;
	}

	pr_info("initialized: timeout=%d sec (nowayout=%d)\n",
		timeout, nowayout);

	return 0;
}

/**
 *	bfin_wdt_remove - Initialize module
 *
 *	Unregisters the misc device.  Actual device
 *	deinitialization is handled by bfin_wdt_close().
 */
static int __devexit bfin_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&bfin_wdt_miscdev);
	return 0;
}

/**
 *	bfin_wdt_shutdown - Soft Shutdown Handler
 *
 *	Handles the soft shutdown event.
 */
static void bfin_wdt_shutdown(struct platform_device *pdev)
{
	stampit();

	bfin_wdt_stop();
}

static struct platform_device *bfin_wdt_device;

static struct platform_driver bfin_wdt_driver = {
	.probe     = bfin_wdt_probe,
	.remove    = __devexit_p(bfin_wdt_remove),
	.shutdown  = bfin_wdt_shutdown,
	.suspend   = bfin_wdt_suspend,
	.resume    = bfin_wdt_resume,
	.driver    = {
		.name  = WATCHDOG_NAME,
		.owner = THIS_MODULE,
	},
};

/**
 *	bfin_wdt_init - Initialize module
 *
 *	Checks the module params and registers the platform device & driver.
 *	Real work is in the platform probe function.
 */
static int __init bfin_wdt_init(void)
{
	int ret;

	stampit();

	/* Check that the timeout value is within range */
	if (bfin_wdt_set_timeout(timeout))
		return -EINVAL;

	/* Since this is an on-chip device and needs no board-specific
	 * resources, we'll handle all the platform device stuff here.
	 */
	ret = platform_driver_register(&bfin_wdt_driver);
	if (ret) {
		pr_err("unable to register driver\n");
		return ret;
	}

	bfin_wdt_device = platform_device_register_simple(WATCHDOG_NAME,
								-1, NULL, 0);
	if (IS_ERR(bfin_wdt_device)) {
		pr_err("unable to register device\n");
		platform_driver_unregister(&bfin_wdt_driver);
		return PTR_ERR(bfin_wdt_device);
	}

	return 0;
}

/**
 *	bfin_wdt_exit - Deinitialize module
 *
 *	Back out the platform device & driver steps.  Real work is in the
 *	platform remove function.
 */
static void __exit bfin_wdt_exit(void)
{
	platform_device_unregister(bfin_wdt_device);
	platform_driver_unregister(&bfin_wdt_driver);
}

module_init(bfin_wdt_init);
module_exit(bfin_wdt_exit);

MODULE_AUTHOR("Michele d'Amico, Mike Frysinger <vapier@gentoo.org>");
MODULE_DESCRIPTION("Blackfin Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. (1<=timeout<=((2^32)/SCLK), default="
		__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
		__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
