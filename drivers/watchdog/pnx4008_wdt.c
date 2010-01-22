/*
 * drivers/char/watchdog/pnx4008_wdt.c
 *
 * Watchdog driver for PNX4008 board
 *
 * Authors: Dmitry Chigirev <source@mvista.com>,
 * 	    Vitaly Wool <vitalywool@gmail.com>
 * Based on sa1100 driver,
 * Copyright (C) 2000 Oleg Drokin <green@crimea.edu>
 *
 * 2005-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <mach/hardware.h>

#define MODULE_NAME "PNX4008-WDT: "

/* WatchDog Timer - Chapter 23 Page 207 */

#define DEFAULT_HEARTBEAT 19
#define MAX_HEARTBEAT     60

/* Watchdog timer register set definition */
#define WDTIM_INT(p)     ((p) + 0x0)
#define WDTIM_CTRL(p)    ((p) + 0x4)
#define WDTIM_COUNTER(p) ((p) + 0x8)
#define WDTIM_MCTRL(p)   ((p) + 0xC)
#define WDTIM_MATCH0(p)  ((p) + 0x10)
#define WDTIM_EMR(p)     ((p) + 0x14)
#define WDTIM_PULSE(p)   ((p) + 0x18)
#define WDTIM_RES(p)     ((p) + 0x1C)

/* WDTIM_INT bit definitions */
#define MATCH_INT      1

/* WDTIM_CTRL bit definitions */
#define COUNT_ENAB     1
#define RESET_COUNT    (1 << 1)
#define DEBUG_EN       (1 << 2)

/* WDTIM_MCTRL bit definitions */
#define MR0_INT        1
#undef  RESET_COUNT0
#define RESET_COUNT0   (1 << 2)
#define STOP_COUNT0    (1 << 2)
#define M_RES1         (1 << 3)
#define M_RES2         (1 << 4)
#define RESFRC1        (1 << 5)
#define RESFRC2        (1 << 6)

/* WDTIM_EMR bit definitions */
#define EXT_MATCH0      1
#define MATCH_OUTPUT_HIGH (2 << 4)	/*a MATCH_CTRL setting */

/* WDTIM_RES bit definitions */
#define WDOG_RESET      1	/* read only */

#define WDOG_COUNTER_RATE 13000000	/*the counter clock is 13 MHz fixed */

static int nowayout = WATCHDOG_NOWAYOUT;
static int heartbeat = DEFAULT_HEARTBEAT;

static DEFINE_SPINLOCK(io_lock);
static unsigned long wdt_status;
#define WDT_IN_USE        0
#define WDT_OK_TO_CLOSE   1
#define WDT_REGION_INITED 2
#define WDT_DEVICE_INITED 3

static unsigned long boot_status;

static struct resource	*wdt_mem;
static void __iomem	*wdt_base;
struct clk		*wdt_clk;

static void wdt_enable(void)
{
	spin_lock(&io_lock);

	if (wdt_clk)
		clk_set_rate(wdt_clk, 1);

	/* stop counter, initiate counter reset */
	__raw_writel(RESET_COUNT, WDTIM_CTRL(wdt_base));
	/*wait for reset to complete. 100% guarantee event */
	while (__raw_readl(WDTIM_COUNTER(wdt_base)))
		cpu_relax();
	/* internal and external reset, stop after that */
	__raw_writel(M_RES2 | STOP_COUNT0 | RESET_COUNT0,
		WDTIM_MCTRL(wdt_base));
	/* configure match output */
	__raw_writel(MATCH_OUTPUT_HIGH, WDTIM_EMR(wdt_base));
	/* clear interrupt, just in case */
	__raw_writel(MATCH_INT, WDTIM_INT(wdt_base));
	/* the longest pulse period 65541/(13*10^6) seconds ~ 5 ms. */
	__raw_writel(0xFFFF, WDTIM_PULSE(wdt_base));
	__raw_writel(heartbeat * WDOG_COUNTER_RATE, WDTIM_MATCH0(wdt_base));
	/*enable counter, stop when debugger active */
	__raw_writel(COUNT_ENAB | DEBUG_EN, WDTIM_CTRL(wdt_base));

	spin_unlock(&io_lock);
}

static void wdt_disable(void)
{
	spin_lock(&io_lock);

	__raw_writel(0, WDTIM_CTRL(wdt_base));	/*stop counter */
	if (wdt_clk)
		clk_set_rate(wdt_clk, 0);

	spin_unlock(&io_lock);
}

static int pnx4008_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	wdt_enable();

	return nonseekable_open(inode, file);
}

static ssize_t pnx4008_wdt_write(struct file *file, const char *data,
					size_t len, loff_t *ppos)
{
	if (len) {
		if (!nowayout) {
			size_t i;

			clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					set_bit(WDT_OK_TO_CLOSE, &wdt_status);
			}
		}
		wdt_enable();
	}

	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_CARDRESET | WDIOF_MAGICCLOSE |
	    WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "PNX4008 Watchdog",
};

static long pnx4008_wdt_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = -ENOTTY;
	int time;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_GETBOOTSTATUS:
		ret = put_user(boot_status, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_enable();
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, (int *)arg);
		if (ret)
			break;

		if (time <= 0 || time > MAX_HEARTBEAT) {
			ret = -EINVAL;
			break;
		}

		heartbeat = time;
		wdt_enable();
		/* Fall through */

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;
	}
	return ret;
}

static int pnx4008_wdt_release(struct inode *inode, struct file *file)
{
	if (!test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		printk(KERN_WARNING "WATCHDOG: Device closed unexpectdly\n");

	wdt_disable();
	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}

static const struct file_operations pnx4008_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = pnx4008_wdt_write,
	.unlocked_ioctl = pnx4008_wdt_ioctl,
	.open = pnx4008_wdt_open,
	.release = pnx4008_wdt_release,
};

static struct miscdevice pnx4008_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &pnx4008_wdt_fops,
};

static int __devinit pnx4008_wdt_probe(struct platform_device *pdev)
{
	int ret = 0, size;
	struct resource *res;

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	printk(KERN_INFO MODULE_NAME
		"PNX4008 Watchdog Timer: heartbeat %d sec\n", heartbeat);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_INFO MODULE_NAME
			"failed to get memory region resouce\n");
		return -ENOENT;
	}

	size = resource_size(res);
	wdt_mem = request_mem_region(res->start, size, pdev->name);

	if (wdt_mem == NULL) {
		printk(KERN_INFO MODULE_NAME "failed to get memory region\n");
		return -ENOENT;
	}
	wdt_base = (void __iomem *)IO_ADDRESS(res->start);

	wdt_clk = clk_get(&pdev->dev, "wdt_ck");
	if (IS_ERR(wdt_clk)) {
		ret = PTR_ERR(wdt_clk);
		release_resource(wdt_mem);
		kfree(wdt_mem);
		goto out;
	} else
		clk_set_rate(wdt_clk, 1);

	ret = misc_register(&pnx4008_wdt_miscdev);
	if (ret < 0) {
		printk(KERN_ERR MODULE_NAME "cannot register misc device\n");
		release_resource(wdt_mem);
		kfree(wdt_mem);
		clk_set_rate(wdt_clk, 0);
	} else {
		boot_status = (__raw_readl(WDTIM_RES(wdt_base)) & WDOG_RESET) ?
		    WDIOF_CARDRESET : 0;
		wdt_disable();		/*disable for now */
		set_bit(WDT_DEVICE_INITED, &wdt_status);
	}

out:
	return ret;
}

static int __devexit pnx4008_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&pnx4008_wdt_miscdev);
	if (wdt_clk) {
		clk_set_rate(wdt_clk, 0);
		clk_put(wdt_clk);
		wdt_clk = NULL;
	}
	if (wdt_mem) {
		release_resource(wdt_mem);
		kfree(wdt_mem);
		wdt_mem = NULL;
	}
	return 0;
}

static struct platform_driver platform_wdt_driver = {
	.driver = {
		.name = "pnx4008-watchdog",
		.owner	= THIS_MODULE,
	},
	.probe = pnx4008_wdt_probe,
	.remove = __devexit_p(pnx4008_wdt_remove),
};

static int __init pnx4008_wdt_init(void)
{
	return platform_driver_register(&platform_wdt_driver);
}

static void __exit pnx4008_wdt_exit(void)
{
	platform_driver_unregister(&platform_wdt_driver);
}

module_init(pnx4008_wdt_init);
module_exit(pnx4008_wdt_exit);

MODULE_AUTHOR("MontaVista Software, Inc. <source@mvista.com>");
MODULE_DESCRIPTION("PNX4008 Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout,
		 "Set to 1 to keep watchdog running after device release");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:pnx4008-watchdog");
