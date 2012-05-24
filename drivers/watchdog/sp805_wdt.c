/*
 * drivers/char/watchdog/sp805-wdt.c
 *
 * Watchdog driver for ARM SP805 watchdog module
 *
 * Copyright (C) 2010 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2 or later. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/resource.h>
#include <linux/amba/bus.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>

/* default timeout in seconds */
#define DEFAULT_TIMEOUT		60

#define MODULE_NAME		"sp805-wdt"

/* watchdog register offsets and masks */
#define WDTLOAD			0x000
	#define LOAD_MIN	0x00000001
	#define LOAD_MAX	0xFFFFFFFF
#define WDTVALUE		0x004
#define WDTCONTROL		0x008
	/* control register masks */
	#define	INT_ENABLE	(1 << 0)
	#define	RESET_ENABLE	(1 << 1)
#define WDTINTCLR		0x00C
#define WDTRIS			0x010
#define WDTMIS			0x014
	#define INT_MASK	(1 << 0)
#define WDTLOCK			0xC00
	#define	UNLOCK		0x1ACCE551
	#define	LOCK		0x00000001

/**
 * struct sp805_wdt: sp805 wdt device structure
 * @lock: spin lock protecting dev structure and io access
 * @base: base address of wdt
 * @clk: clock structure of wdt
 * @adev: amba device structure of wdt
 * @status: current status of wdt
 * @load_val: load value to be set for current timeout
 * @timeout: current programmed timeout
 */
struct sp805_wdt {
	spinlock_t			lock;
	void __iomem			*base;
	struct clk			*clk;
	struct amba_device		*adev;
	unsigned long			status;
	#define WDT_BUSY		0
	#define WDT_CAN_BE_CLOSED	1
	unsigned int			load_val;
	unsigned int			timeout;
};

/* local variables */
static struct sp805_wdt *wdt;
static bool nowayout = WATCHDOG_NOWAYOUT;

/* This routine finds load value that will reset system in required timout */
static void wdt_setload(unsigned int timeout)
{
	u64 load, rate;

	rate = clk_get_rate(wdt->clk);

	/*
	 * sp805 runs counter with given value twice, after the end of first
	 * counter it gives an interrupt and then starts counter again. If
	 * interrupt already occurred then it resets the system. This is why
	 * load is half of what should be required.
	 */
	load = div_u64(rate, 2) * timeout - 1;

	load = (load > LOAD_MAX) ? LOAD_MAX : load;
	load = (load < LOAD_MIN) ? LOAD_MIN : load;

	spin_lock(&wdt->lock);
	wdt->load_val = load;
	/* roundup timeout to closest positive integer value */
	wdt->timeout = div_u64((load + 1) * 2 + (rate / 2), rate);
	spin_unlock(&wdt->lock);
}

/* returns number of seconds left for reset to occur */
static u32 wdt_timeleft(void)
{
	u64 load, rate;

	rate = clk_get_rate(wdt->clk);

	spin_lock(&wdt->lock);
	load = readl_relaxed(wdt->base + WDTVALUE);

	/*If the interrupt is inactive then time left is WDTValue + WDTLoad. */
	if (!(readl_relaxed(wdt->base + WDTRIS) & INT_MASK))
		load += wdt->load_val + 1;
	spin_unlock(&wdt->lock);

	return div_u64(load, rate);
}

/* enables watchdog timers reset */
static void wdt_enable(void)
{
	spin_lock(&wdt->lock);

	writel_relaxed(UNLOCK, wdt->base + WDTLOCK);
	writel_relaxed(wdt->load_val, wdt->base + WDTLOAD);
	writel_relaxed(INT_MASK, wdt->base + WDTINTCLR);
	writel_relaxed(INT_ENABLE | RESET_ENABLE, wdt->base + WDTCONTROL);
	writel_relaxed(LOCK, wdt->base + WDTLOCK);

	/* Flush posted writes. */
	readl_relaxed(wdt->base + WDTLOCK);
	spin_unlock(&wdt->lock);
}

/* disables watchdog timers reset */
static void wdt_disable(void)
{
	spin_lock(&wdt->lock);

	writel_relaxed(UNLOCK, wdt->base + WDTLOCK);
	writel_relaxed(0, wdt->base + WDTCONTROL);
	writel_relaxed(LOCK, wdt->base + WDTLOCK);

	/* Flush posted writes. */
	readl_relaxed(wdt->base + WDTLOCK);
	spin_unlock(&wdt->lock);
}

static ssize_t sp805_wdt_write(struct file *file, const char *data,
		size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			clear_bit(WDT_CAN_BE_CLOSED, &wdt->status);

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				/* Check for Magic Close character */
				if (c == 'V') {
					set_bit(WDT_CAN_BE_CLOSED,
							&wdt->status);
					break;
				}
			}
		}
		wdt_enable();
	}
	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = MODULE_NAME,
};

static long sp805_wdt_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int ret = -ENOTTY;
	unsigned int timeout;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_enable();
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(timeout, (unsigned int *)arg);
		if (ret)
			break;

		wdt_setload(timeout);

		wdt_enable();
		/* Fall through */

	case WDIOC_GETTIMEOUT:
		ret = put_user(wdt->timeout, (unsigned int *)arg);
		break;
	case WDIOC_GETTIMELEFT:
		ret = put_user(wdt_timeleft(), (unsigned int *)arg);
		break;
	}
	return ret;
}

static int sp805_wdt_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	if (test_and_set_bit(WDT_BUSY, &wdt->status))
		return -EBUSY;

	ret = clk_enable(wdt->clk);
	if (ret) {
		dev_err(&wdt->adev->dev, "clock enable fail");
		goto err;
	}

	wdt_enable();

	/* can not be closed, once enabled */
	clear_bit(WDT_CAN_BE_CLOSED, &wdt->status);
	return nonseekable_open(inode, file);

err:
	clear_bit(WDT_BUSY, &wdt->status);
	return ret;
}

static int sp805_wdt_release(struct inode *inode, struct file *file)
{
	if (!test_bit(WDT_CAN_BE_CLOSED, &wdt->status)) {
		clear_bit(WDT_BUSY, &wdt->status);
		dev_warn(&wdt->adev->dev, "Device closed unexpectedly\n");
		return 0;
	}

	wdt_disable();
	clk_disable(wdt->clk);
	clear_bit(WDT_BUSY, &wdt->status);

	return 0;
}

static const struct file_operations sp805_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = sp805_wdt_write,
	.unlocked_ioctl = sp805_wdt_ioctl,
	.open = sp805_wdt_open,
	.release = sp805_wdt_release,
};

static struct miscdevice sp805_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &sp805_wdt_fops,
};

static int __devinit
sp805_wdt_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;

	if (!devm_request_mem_region(&adev->dev, adev->res.start,
				resource_size(&adev->res), "sp805_wdt")) {
		dev_warn(&adev->dev, "Failed to get memory region resource\n");
		ret = -ENOENT;
		goto err;
	}

	wdt = devm_kzalloc(&adev->dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt) {
		dev_warn(&adev->dev, "Kzalloc failed\n");
		ret = -ENOMEM;
		goto err;
	}

	wdt->base = devm_ioremap(&adev->dev, adev->res.start,
			resource_size(&adev->res));
	if (!wdt->base) {
		ret = -ENOMEM;
		dev_warn(&adev->dev, "ioremap fail\n");
		goto err;
	}

	wdt->clk = clk_get(&adev->dev, NULL);
	if (IS_ERR(wdt->clk)) {
		dev_warn(&adev->dev, "Clock not found\n");
		ret = PTR_ERR(wdt->clk);
		goto err;
	}

	wdt->adev = adev;
	spin_lock_init(&wdt->lock);
	wdt_setload(DEFAULT_TIMEOUT);

	ret = misc_register(&sp805_wdt_miscdev);
	if (ret < 0) {
		dev_warn(&adev->dev, "cannot register misc device\n");
		goto err_misc_register;
	}

	dev_info(&adev->dev, "registration successful\n");
	return 0;

err_misc_register:
	clk_put(wdt->clk);
err:
	dev_err(&adev->dev, "Probe Failed!!!\n");
	return ret;
}

static int __devexit sp805_wdt_remove(struct amba_device *adev)
{
	misc_deregister(&sp805_wdt_miscdev);
	clk_put(wdt->clk);

	return 0;
}

#ifdef CONFIG_PM
static int sp805_wdt_suspend(struct device *dev)
{
	if (test_bit(WDT_BUSY, &wdt->status)) {
		wdt_disable();
		clk_disable(wdt->clk);
	}

	return 0;
}

static int sp805_wdt_resume(struct device *dev)
{
	int ret = 0;

	if (test_bit(WDT_BUSY, &wdt->status)) {
		ret = clk_enable(wdt->clk);
		if (ret) {
			dev_err(dev, "clock enable fail");
			return ret;
		}
		wdt_enable();
	}

	return ret;
}
#endif /* CONFIG_PM */

static SIMPLE_DEV_PM_OPS(sp805_wdt_dev_pm_ops, sp805_wdt_suspend,
		sp805_wdt_resume);

static struct amba_id sp805_wdt_ids[] = {
	{
		.id	= 0x00141805,
		.mask	= 0x00ffffff,
	},
	{ 0, 0 },
};

MODULE_DEVICE_TABLE(amba, sp805_wdt_ids);

static struct amba_driver sp805_wdt_driver = {
	.drv = {
		.name	= MODULE_NAME,
		.pm	= &sp805_wdt_dev_pm_ops,
	},
	.id_table	= sp805_wdt_ids,
	.probe		= sp805_wdt_probe,
	.remove = __devexit_p(sp805_wdt_remove),
};

module_amba_driver(sp805_wdt_driver);

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		"Set to 1 to keep watchdog running after device release");

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@st.com>");
MODULE_DESCRIPTION("ARM SP805 Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
