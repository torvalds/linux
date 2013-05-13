/*
 * drivers/char/watchdog/davinci_wdt.c
 *
 * Watchdog driver for DaVinci DM644x/DM646x processors
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
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
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>

#define MODULE_NAME "DAVINCI-WDT: "

#define DEFAULT_HEARTBEAT 60
#define MAX_HEARTBEAT     600	/* really the max margin is 264/27MHz*/

/* Timer register set definition */
#define PID12	(0x0)
#define EMUMGT	(0x4)
#define TIM12	(0x10)
#define TIM34	(0x14)
#define PRD12	(0x18)
#define PRD34	(0x1C)
#define TCR	(0x20)
#define TGCR	(0x24)
#define WDTCR	(0x28)

/* TCR bit definitions */
#define ENAMODE12_DISABLED	(0 << 6)
#define ENAMODE12_ONESHOT	(1 << 6)
#define ENAMODE12_PERIODIC	(2 << 6)

/* TGCR bit definitions */
#define TIM12RS_UNRESET		(1 << 0)
#define TIM34RS_UNRESET		(1 << 1)
#define TIMMODE_64BIT_WDOG      (2 << 2)

/* WDTCR bit definitions */
#define WDEN			(1 << 14)
#define WDFLAG			(1 << 15)
#define WDKEY_SEQ0		(0xa5c6 << 16)
#define WDKEY_SEQ1		(0xda7e << 16)

static int heartbeat = DEFAULT_HEARTBEAT;

static DEFINE_SPINLOCK(io_lock);
static unsigned long wdt_status;
#define WDT_IN_USE        0
#define WDT_OK_TO_CLOSE   1
#define WDT_REGION_INITED 2
#define WDT_DEVICE_INITED 3

static void __iomem	*wdt_base;
struct clk		*wdt_clk;

static void wdt_service(void)
{
	spin_lock(&io_lock);

	/* put watchdog in service state */
	iowrite32(WDKEY_SEQ0, wdt_base + WDTCR);
	/* put watchdog in active state */
	iowrite32(WDKEY_SEQ1, wdt_base + WDTCR);

	spin_unlock(&io_lock);
}

static void wdt_enable(void)
{
	u32 tgcr;
	u32 timer_margin;
	unsigned long wdt_freq;

	wdt_freq = clk_get_rate(wdt_clk);

	spin_lock(&io_lock);

	/* disable, internal clock source */
	iowrite32(0, wdt_base + TCR);
	/* reset timer, set mode to 64-bit watchdog, and unreset */
	iowrite32(0, wdt_base + TGCR);
	tgcr = TIMMODE_64BIT_WDOG | TIM12RS_UNRESET | TIM34RS_UNRESET;
	iowrite32(tgcr, wdt_base + TGCR);
	/* clear counter regs */
	iowrite32(0, wdt_base + TIM12);
	iowrite32(0, wdt_base + TIM34);
	/* set timeout period */
	timer_margin = (((u64)heartbeat * wdt_freq) & 0xffffffff);
	iowrite32(timer_margin, wdt_base + PRD12);
	timer_margin = (((u64)heartbeat * wdt_freq) >> 32);
	iowrite32(timer_margin, wdt_base + PRD34);
	/* enable run continuously */
	iowrite32(ENAMODE12_PERIODIC, wdt_base + TCR);
	/* Once the WDT is in pre-active state write to
	 * TIM12, TIM34, PRD12, PRD34, TCR, TGCR, WDTCR are
	 * write protected (except for the WDKEY field)
	 */
	/* put watchdog in pre-active state */
	iowrite32(WDKEY_SEQ0 | WDEN, wdt_base + WDTCR);
	/* put watchdog in active state */
	iowrite32(WDKEY_SEQ1 | WDEN, wdt_base + WDTCR);

	spin_unlock(&io_lock);
}

static int davinci_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	wdt_enable();

	return nonseekable_open(inode, file);
}

static ssize_t
davinci_wdt_write(struct file *file, const char *data, size_t len,
		  loff_t *ppos)
{
	if (len)
		wdt_service();

	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_KEEPALIVEPING,
	.identity = "DaVinci Watchdog",
};

static long davinci_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ret = copy_to_user((struct watchdog_info *)arg, &ident,
				   sizeof(ident)) ? -EFAULT : 0;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_service();
		ret = 0;
		break;

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;
	}
	return ret;
}

static int davinci_wdt_release(struct inode *inode, struct file *file)
{
	wdt_service();
	clear_bit(WDT_IN_USE, &wdt_status);

	return 0;
}

static const struct file_operations davinci_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = davinci_wdt_write,
	.unlocked_ioctl = davinci_wdt_ioctl,
	.open = davinci_wdt_open,
	.release = davinci_wdt_release,
};

static struct miscdevice davinci_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &davinci_wdt_fops,
};

static int davinci_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct resource  *wdt_mem;

	wdt_clk = devm_clk_get(dev, NULL);
	if (WARN_ON(IS_ERR(wdt_clk)))
		return PTR_ERR(wdt_clk);

	clk_prepare_enable(wdt_clk);

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	dev_info(dev, "heartbeat %d sec\n", heartbeat);

	wdt_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (wdt_mem == NULL) {
		dev_err(dev, "failed to get memory region resource\n");
		return -ENOENT;
	}

	wdt_base = devm_ioremap_resource(dev, wdt_mem);
	if (IS_ERR(wdt_base))
		return PTR_ERR(wdt_base);

	ret = misc_register(&davinci_wdt_miscdev);
	if (ret < 0) {
		dev_err(dev, "cannot register misc device\n");
	} else {
		set_bit(WDT_DEVICE_INITED, &wdt_status);
	}

	return ret;
}

static int davinci_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&davinci_wdt_miscdev);
	clk_disable_unprepare(wdt_clk);

	return 0;
}

static const struct of_device_id davinci_wdt_of_match[] = {
	{ .compatible = "ti,davinci-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, davinci_wdt_of_match);

static struct platform_driver platform_wdt_driver = {
	.driver = {
		.name = "watchdog",
		.owner	= THIS_MODULE,
		.of_match_table = davinci_wdt_of_match,
	},
	.probe = davinci_wdt_probe,
	.remove = davinci_wdt_remove,
};

module_platform_driver(platform_wdt_driver);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("DaVinci Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:watchdog");
