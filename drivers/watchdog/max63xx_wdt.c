/*
 * drivers/char/watchdog/max63xx_wdt.c
 *
 * Driver for max63{69,70,71,72,73,74} watchdog timers
 *
 * Copyright (C) 2009 Marc Zyngier <maz@misterjones.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * This driver assumes the watchdog pins are memory mapped (as it is
 * the case for the Arcom Zeus). Should it be connected over GPIOs or
 * another interface, some abstraction will have to be introduced.
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
#include <linux/slab.h>

#define DEFAULT_HEARTBEAT 60
#define MAX_HEARTBEAT     60

static int heartbeat = DEFAULT_HEARTBEAT;
static int nowayout  = WATCHDOG_NOWAYOUT;

/*
 * Memory mapping: a single byte, 3 first lower bits to select bit 3
 * to ping the watchdog.
 */
#define MAX6369_WDSET	(7 << 0)
#define MAX6369_WDI   	(1 << 3)

static DEFINE_SPINLOCK(io_lock);

static unsigned long wdt_status;
#define WDT_IN_USE	0
#define WDT_RUNNING	1
#define WDT_OK_TO_CLOSE 2

static int nodelay;
static struct resource	*wdt_mem;
static void __iomem	*wdt_base;
static struct platform_device *max63xx_pdev;

/*
 * The timeout values used are actually the absolute minimum the chip
 * offers. Typical values on my board are slightly over twice as long
 * (10s setting ends up with a 25s timeout), and can be up to 3 times
 * the nominal setting (according to the datasheet). So please take
 * these values with a grain of salt. Same goes for the initial delay
 * "feature". Only max6373/74 have a few settings without this initial
 * delay (selected with the "nodelay" parameter).
 *
 * I also decided to remove from the tables any timeout smaller than a
 * second, as it looked completly overkill...
 */

/* Timeouts in second */
struct max63xx_timeout {
	u8 wdset;
	u8 tdelay;
	u8 twd;
};

static struct max63xx_timeout max6369_table[] = {
	{ 5,  1,  1 },
	{ 6, 10, 10 },
	{ 7, 60, 60 },
	{ },
};

static struct max63xx_timeout max6371_table[] = {
	{ 6, 60,  3 },
	{ 7, 60, 60 },
	{ },
};

static struct max63xx_timeout max6373_table[] = {
	{ 2, 60,  1 },
	{ 5,  0,  1 },
	{ 1,  3,  3 },
	{ 7, 60, 10 },
	{ 6,  0, 10 },
	{ },
};

static struct max63xx_timeout *current_timeout;

static struct max63xx_timeout *
max63xx_select_timeout(struct max63xx_timeout *table, int value)
{
	while (table->twd) {
		if (value <= table->twd) {
			if (nodelay && table->tdelay == 0)
				return table;

			if (!nodelay)
				return table;
		}

		table++;
	}

	return NULL;
}

static void max63xx_wdt_ping(void)
{
	u8 val;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);

	__raw_writeb(val | MAX6369_WDI, wdt_base);
	__raw_writeb(val & ~MAX6369_WDI, wdt_base);

	spin_unlock(&io_lock);
}

static void max63xx_wdt_enable(struct max63xx_timeout *entry)
{
	u8 val;

	if (test_and_set_bit(WDT_RUNNING, &wdt_status))
		return;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);
	val &= ~MAX6369_WDSET;
	val |= entry->wdset;
	__raw_writeb(val, wdt_base);

	spin_unlock(&io_lock);

	/* check for a edge triggered startup */
	if (entry->tdelay == 0)
		max63xx_wdt_ping();
}

static void max63xx_wdt_disable(void)
{
	u8 val;

	spin_lock(&io_lock);

	val = __raw_readb(wdt_base);
	val &= ~MAX6369_WDSET;
	val |= 3;
	__raw_writeb(val, wdt_base);

	spin_unlock(&io_lock);

	clear_bit(WDT_RUNNING, &wdt_status);
}

static int max63xx_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;

	max63xx_wdt_enable(current_timeout);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return nonseekable_open(inode, file);
}

static ssize_t max63xx_wdt_write(struct file *file, const char *data,
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

		max63xx_wdt_ping();
	}

	return len;
}

static const struct watchdog_info ident = {
	.options = WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "max63xx Watchdog",
};

static long max63xx_wdt_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
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
		max63xx_wdt_ping();
		ret = 0;
		break;

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;
	}
	return ret;
}

static int max63xx_wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		max63xx_wdt_disable();
	else
		dev_crit(&max63xx_pdev->dev,
			 "device closed unexpectedly - timer will not stop\n");

	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}

static const struct file_operations max63xx_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= max63xx_wdt_write,
	.unlocked_ioctl	= max63xx_wdt_ioctl,
	.open		= max63xx_wdt_open,
	.release	= max63xx_wdt_release,
};

static struct miscdevice max63xx_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &max63xx_wdt_fops,
};

static int __devinit max63xx_wdt_probe(struct platform_device *pdev)
{
	int ret = 0;
	int size;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct max63xx_timeout *table;

	table = (struct max63xx_timeout *)pdev->id_entry->driver_data;

	if (heartbeat < 1 || heartbeat > MAX_HEARTBEAT)
		heartbeat = DEFAULT_HEARTBEAT;

	dev_info(dev, "requesting %ds heartbeat\n", heartbeat);
	current_timeout = max63xx_select_timeout(table, heartbeat);

	if (!current_timeout) {
		dev_err(dev, "unable to satisfy heartbeat request\n");
		return -EINVAL;
	}

	dev_info(dev, "using %ds heartbeat with %ds initial delay\n",
		 current_timeout->twd, current_timeout->tdelay);

	heartbeat = current_timeout->twd;

	max63xx_pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "failed to get memory region resource\n");
		return -ENOENT;
	}

	size = resource_size(res);
	wdt_mem = request_mem_region(res->start, size, pdev->name);

	if (wdt_mem == NULL) {
		dev_err(dev, "failed to get memory region\n");
		return -ENOENT;
	}

	wdt_base = ioremap(res->start, size);
	if (!wdt_base) {
		dev_err(dev, "failed to map memory region\n");
		ret = -ENOMEM;
		goto out_request;
	}

	ret = misc_register(&max63xx_wdt_miscdev);
	if (ret < 0) {
		dev_err(dev, "cannot register misc device\n");
		goto out_unmap;
	}

	return 0;

out_unmap:
	iounmap(wdt_base);
out_request:
	release_resource(wdt_mem);
	kfree(wdt_mem);

	return ret;
}

static int __devexit max63xx_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&max63xx_wdt_miscdev);
	if (wdt_mem) {
		release_resource(wdt_mem);
		kfree(wdt_mem);
		wdt_mem = NULL;
	}

	if (wdt_base)
		iounmap(wdt_base);

	return 0;
}

static struct platform_device_id max63xx_id_table[] = {
	{ "max6369_wdt", (kernel_ulong_t)max6369_table, },
	{ "max6370_wdt", (kernel_ulong_t)max6369_table, },
	{ "max6371_wdt", (kernel_ulong_t)max6371_table, },
	{ "max6372_wdt", (kernel_ulong_t)max6371_table, },
	{ "max6373_wdt", (kernel_ulong_t)max6373_table, },
	{ "max6374_wdt", (kernel_ulong_t)max6373_table, },
	{ },
};
MODULE_DEVICE_TABLE(platform, max63xx_id_table);

static struct platform_driver max63xx_wdt_driver = {
	.probe		= max63xx_wdt_probe,
	.remove		= __devexit_p(max63xx_wdt_remove),
	.id_table	= max63xx_id_table,
	.driver		= {
		.name	= "max63xx_wdt",
		.owner	= THIS_MODULE,
	},
};

static int __init max63xx_wdt_init(void)
{
	return platform_driver_register(&max63xx_wdt_driver);
}

static void __exit max63xx_wdt_exit(void)
{
	platform_driver_unregister(&max63xx_wdt_driver);
}

module_init(max63xx_wdt_init);
module_exit(max63xx_wdt_exit);

MODULE_AUTHOR("Marc Zyngier <maz@misterjones.org>");
MODULE_DESCRIPTION("max63xx Watchdog Driver");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat,
		 "Watchdog heartbeat period in seconds from 1 to "
		 __MODULE_STRING(MAX_HEARTBEAT) ", default "
		 __MODULE_STRING(DEFAULT_HEARTBEAT));

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_param(nodelay, int, 0);
MODULE_PARM_DESC(nodelay,
		 "Force selection of a timeout setting without initial delay "
		 "(max6373/74 only, default=0)");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
