/*
 * drivers/watchdog/ar7_wdt.c
 *
 * Copyright (C) 2007 Nicolas Thill <nico@openwrt.org>
 * Copyright (c) 2005 Enrik Berkhan <Enrik.Berkhan@akk.org>
 *
 * Some code taken from:
 * National Semiconductor SCx200 Watchdog support
 * Copyright (c) 2001,2002 Christer Weinigel <wingel@nano-system.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/clk.h>

#include <asm/addrspace.h>
#include <asm/mach-ar7/ar7.h>

#define LONGNAME "TI AR7 Watchdog Timer"

MODULE_AUTHOR("Nicolas Thill <nico@openwrt.org>");
MODULE_DESCRIPTION(LONGNAME);
MODULE_LICENSE("GPL");

static int margin = 60;
module_param(margin, int, 0);
MODULE_PARM_DESC(margin, "Watchdog margin in seconds");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

#define READ_REG(x) readl((void __iomem *)&(x))
#define WRITE_REG(x, v) writel((v), (void __iomem *)&(x))

struct ar7_wdt {
	u32 kick_lock;
	u32 kick;
	u32 change_lock;
	u32 change;
	u32 disable_lock;
	u32 disable;
	u32 prescale_lock;
	u32 prescale;
};

static unsigned long wdt_is_open;
static unsigned expect_close;
static DEFINE_SPINLOCK(wdt_lock);

/* XXX currently fixed, allows max margin ~68.72 secs */
#define prescale_value 0xffff

/* Resource of the WDT registers */
static struct resource *ar7_regs_wdt;
/* Pointer to the remapped WDT IO space */
static struct ar7_wdt *ar7_wdt;

static struct clk *vbus_clk;

static void ar7_wdt_kick(u32 value)
{
	WRITE_REG(ar7_wdt->kick_lock, 0x5555);
	if ((READ_REG(ar7_wdt->kick_lock) & 3) == 1) {
		WRITE_REG(ar7_wdt->kick_lock, 0xaaaa);
		if ((READ_REG(ar7_wdt->kick_lock) & 3) == 3) {
			WRITE_REG(ar7_wdt->kick, value);
			return;
		}
	}
	pr_err("failed to unlock WDT kick reg\n");
}

static void ar7_wdt_prescale(u32 value)
{
	WRITE_REG(ar7_wdt->prescale_lock, 0x5a5a);
	if ((READ_REG(ar7_wdt->prescale_lock) & 3) == 1) {
		WRITE_REG(ar7_wdt->prescale_lock, 0xa5a5);
		if ((READ_REG(ar7_wdt->prescale_lock) & 3) == 3) {
			WRITE_REG(ar7_wdt->prescale, value);
			return;
		}
	}
	pr_err("failed to unlock WDT prescale reg\n");
}

static void ar7_wdt_change(u32 value)
{
	WRITE_REG(ar7_wdt->change_lock, 0x6666);
	if ((READ_REG(ar7_wdt->change_lock) & 3) == 1) {
		WRITE_REG(ar7_wdt->change_lock, 0xbbbb);
		if ((READ_REG(ar7_wdt->change_lock) & 3) == 3) {
			WRITE_REG(ar7_wdt->change, value);
			return;
		}
	}
	pr_err("failed to unlock WDT change reg\n");
}

static void ar7_wdt_disable(u32 value)
{
	WRITE_REG(ar7_wdt->disable_lock, 0x7777);
	if ((READ_REG(ar7_wdt->disable_lock) & 3) == 1) {
		WRITE_REG(ar7_wdt->disable_lock, 0xcccc);
		if ((READ_REG(ar7_wdt->disable_lock) & 3) == 2) {
			WRITE_REG(ar7_wdt->disable_lock, 0xdddd);
			if ((READ_REG(ar7_wdt->disable_lock) & 3) == 3) {
				WRITE_REG(ar7_wdt->disable, value);
				return;
			}
		}
	}
	pr_err("failed to unlock WDT disable reg\n");
}

static void ar7_wdt_update_margin(int new_margin)
{
	u32 change;
	u32 vbus_rate;

	vbus_rate = clk_get_rate(vbus_clk);
	change = new_margin * (vbus_rate / prescale_value);
	if (change < 1)
		change = 1;
	if (change > 0xffff)
		change = 0xffff;
	ar7_wdt_change(change);
	margin = change * prescale_value / vbus_rate;
	pr_info("timer margin %d seconds (prescale %d, change %d, freq %d)\n",
		margin, prescale_value, change, vbus_rate);
}

static void ar7_wdt_enable_wdt(void)
{
	pr_debug("enabling watchdog timer\n");
	ar7_wdt_disable(1);
	ar7_wdt_kick(1);
}

static void ar7_wdt_disable_wdt(void)
{
	pr_debug("disabling watchdog timer\n");
	ar7_wdt_disable(0);
}

static int ar7_wdt_open(struct inode *inode, struct file *file)
{
	/* only allow one at a time */
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;
	ar7_wdt_enable_wdt();
	expect_close = 0;

	return nonseekable_open(inode, file);
}

static int ar7_wdt_release(struct inode *inode, struct file *file)
{
	if (!expect_close)
		pr_warn("watchdog device closed unexpectedly, will not disable the watchdog timer\n");
	else if (!nowayout)
		ar7_wdt_disable_wdt();
	clear_bit(0, &wdt_is_open);
	return 0;
}

static ssize_t ar7_wdt_write(struct file *file, const char *data,
			     size_t len, loff_t *ppos)
{
	/* check for a magic close character */
	if (len) {
		size_t i;

		spin_lock(&wdt_lock);
		ar7_wdt_kick(1);
		spin_unlock(&wdt_lock);

		expect_close = 0;
		for (i = 0; i < len; ++i) {
			char c;
			if (get_user(c, data + i))
				return -EFAULT;
			if (c == 'V')
				expect_close = 1;
		}

	}
	return len;
}

static long ar7_wdt_ioctl(struct file *file,
					unsigned int cmd, unsigned long arg)
{
	static const struct watchdog_info ident = {
		.identity = LONGNAME,
		.firmware_version = 1,
		.options = (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
						WDIOF_MAGICCLOSE),
	};
	int new_margin;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user((struct watchdog_info *)arg, &ident,
				sizeof(ident)))
			return -EFAULT;
		return 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		if (put_user(0, (int *)arg))
			return -EFAULT;
		return 0;
	case WDIOC_KEEPALIVE:
		ar7_wdt_kick(1);
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, (int *)arg))
			return -EFAULT;
		if (new_margin < 1)
			return -EINVAL;

		spin_lock(&wdt_lock);
		ar7_wdt_update_margin(new_margin);
		ar7_wdt_kick(1);
		spin_unlock(&wdt_lock);

	case WDIOC_GETTIMEOUT:
		if (put_user(margin, (int *)arg))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations ar7_wdt_fops = {
	.owner		= THIS_MODULE,
	.write		= ar7_wdt_write,
	.unlocked_ioctl	= ar7_wdt_ioctl,
	.open		= ar7_wdt_open,
	.release	= ar7_wdt_release,
	.llseek		= no_llseek,
};

static struct miscdevice ar7_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &ar7_wdt_fops,
};

static int ar7_wdt_probe(struct platform_device *pdev)
{
	int rc;

	ar7_regs_wdt =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	ar7_wdt = devm_ioremap_resource(&pdev->dev, ar7_regs_wdt);
	if (IS_ERR(ar7_wdt))
		return PTR_ERR(ar7_wdt);

	vbus_clk = clk_get(NULL, "vbus");
	if (IS_ERR(vbus_clk)) {
		pr_err("could not get vbus clock\n");
		return PTR_ERR(vbus_clk);
	}

	ar7_wdt_disable_wdt();
	ar7_wdt_prescale(prescale_value);
	ar7_wdt_update_margin(margin);

	rc = misc_register(&ar7_wdt_miscdev);
	if (rc) {
		pr_err("unable to register misc device\n");
		goto out;
	}
	return 0;

out:
	clk_put(vbus_clk);
	vbus_clk = NULL;
	return rc;
}

static int ar7_wdt_remove(struct platform_device *pdev)
{
	misc_deregister(&ar7_wdt_miscdev);
	clk_put(vbus_clk);
	vbus_clk = NULL;
	return 0;
}

static void ar7_wdt_shutdown(struct platform_device *pdev)
{
	if (!nowayout)
		ar7_wdt_disable_wdt();
}

static struct platform_driver ar7_wdt_driver = {
	.probe = ar7_wdt_probe,
	.remove = ar7_wdt_remove,
	.shutdown = ar7_wdt_shutdown,
	.driver = {
		.owner = THIS_MODULE,
		.name = "ar7_wdt",
	},
};

module_platform_driver(ar7_wdt_driver);
