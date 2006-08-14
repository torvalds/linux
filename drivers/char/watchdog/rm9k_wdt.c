/*
 *  Watchdog implementation for GPI h/w found on PMC-Sierra RM9xxx
 *  chips.
 *
 *  Copyright (C) 2004 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/rm9k-ocd.h>

#include <rm9k_wdt.h>


#define CLOCK                  125000000
#define MAX_TIMEOUT_SECONDS    32
#define CPCCR                  0x0080
#define CPGIG1SR               0x0044
#define CPGIG1ER               0x0054



/* Function prototypes */
static int __init wdt_gpi_probe(struct device *);
static int __exit wdt_gpi_remove(struct device *);
static void wdt_gpi_set_timeout(unsigned int);
static int wdt_gpi_open(struct inode *, struct file *);
static int wdt_gpi_release(struct inode *, struct file *);
static ssize_t wdt_gpi_write(struct file *, const char __user *, size_t, loff_t *);
static long wdt_gpi_ioctl(struct file *, unsigned int, unsigned long);
static const struct resource *wdt_gpi_get_resource(struct platform_device *, const char *, unsigned int);
static int wdt_gpi_notify(struct notifier_block *, unsigned long, void *);
static irqreturn_t wdt_gpi_irqhdl(int, void *, struct pt_regs *);




static const char wdt_gpi_name[] = "wdt_gpi";
static atomic_t opencnt;
static int expect_close;
static int locked = 0;



/* These are set from device resources */
static void __iomem * wd_regs;
static unsigned int wd_irq, wd_ctr;



/* Module arguments */
static int timeout = MAX_TIMEOUT_SECONDS;
module_param(timeout, int, 0444);
static unsigned long resetaddr = 0xbffdc200;
module_param(resetaddr, ulong, 0444);
static unsigned long flagaddr = 0xbffdc104;
module_param(flagaddr, ulong, 0444);
static int powercycle = 0;
module_param(powercycle, bool, 0444);

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);



static struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= wdt_gpi_open,
	.release	= wdt_gpi_release,
	.write		= wdt_gpi_write,
	.unlocked_ioctl	= wdt_gpi_ioctl,
};

static struct miscdevice miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= wdt_gpi_name,
	.fops		= &fops,
};

static struct device_driver wdt_gpi_driver = {
	.name		= (char *) wdt_gpi_name,
	.bus		= &platform_bus_type,
	.owner		= THIS_MODULE,
	.probe		= wdt_gpi_probe,
	.remove		= __exit_p(wdt_gpi_remove),
	.shutdown	= NULL,
	.suspend	= NULL,
	.resume		= NULL,
};

static struct notifier_block wdt_gpi_shutdown = {
	.notifier_call	= wdt_gpi_notify,
};



static const struct resource *
wdt_gpi_get_resource(struct platform_device *pdv, const char *name,
		      unsigned int type)
{
	char buf[80];
	if (snprintf(buf, sizeof buf, "%s_0", name) >= sizeof buf)
		return NULL;
	return platform_get_resource_byname(pdv, type, buf);
}



/* No hotplugging on the platform bus - use __init */
static int __init wdt_gpi_probe(struct device *dev)
{
	int res;
	struct platform_device * const pdv = to_platform_device(dev);
	const struct resource
		* const rr = wdt_gpi_get_resource(pdv, WDT_RESOURCE_REGS,
						  IORESOURCE_MEM),
		* const ri = wdt_gpi_get_resource(pdv, WDT_RESOURCE_IRQ,
						  IORESOURCE_IRQ),
		* const rc = wdt_gpi_get_resource(pdv, WDT_RESOURCE_COUNTER,
						  0);

	if (unlikely(!rr || !ri || !rc))
		return -ENXIO;

	wd_regs = ioremap_nocache(rr->start, rr->end + 1 - rr->start);
	if (unlikely(!wd_regs))
		return -ENOMEM;
	wd_irq = ri->start;
	wd_ctr = rc->start;
	res = misc_register(&miscdev);
	if (res)
		iounmap(wd_regs);
	else
		register_reboot_notifier(&wdt_gpi_shutdown);
	return res;
}



static int __exit wdt_gpi_remove(struct device *dev)
{
	int res;

	unregister_reboot_notifier(&wdt_gpi_shutdown);
	res = misc_deregister(&miscdev);
	iounmap(wd_regs);
	wd_regs = NULL;
	return res;
}


static void wdt_gpi_set_timeout(unsigned int to)
{
	u32 reg;
	const u32 wdval = (to * CLOCK) & ~0x0000000f;

	lock_titan_regs();
	reg = titan_readl(CPCCR) & ~(0xf << (wd_ctr * 4));
	titan_writel(reg, CPCCR);
	wmb();
	__raw_writel(wdval, wd_regs + 0x0000);
	wmb();
	titan_writel(reg | (0x2 << (wd_ctr * 4)), CPCCR);
	wmb();
	titan_writel(reg | (0x5 << (wd_ctr * 4)), CPCCR);
	iob();
	unlock_titan_regs();
}



static int wdt_gpi_open(struct inode *i, struct file *f)
{
	int res;
	u32 reg;

	if (unlikely(0 > atomic_dec_if_positive(&opencnt)))
		return -EBUSY;

	expect_close = 0;
	if (locked) {
		module_put(THIS_MODULE);
		free_irq(wd_irq, &miscdev);
		locked = 0;
	}

	res = request_irq(wd_irq, wdt_gpi_irqhdl, SA_SHIRQ | SA_INTERRUPT,
			  wdt_gpi_name, &miscdev);
	if (unlikely(res))
		return res;

	wdt_gpi_set_timeout(timeout);

	lock_titan_regs();
	reg = titan_readl(CPGIG1ER);
	titan_writel(reg | (0x100 << wd_ctr), CPGIG1ER);
	iob();
	unlock_titan_regs();

	printk(KERN_INFO "%s: watchdog started, timeout = %u seconds\n",
		wdt_gpi_name, timeout);
	return 0;
}



static int wdt_gpi_release(struct inode *i, struct file *f)
{
	if (nowayout) {
		printk(KERN_NOTICE "%s: no way out - watchdog left running\n",
			wdt_gpi_name);
		__module_get(THIS_MODULE);
		locked = 1;
	} else {
		if (expect_close) {
			u32 reg;

			lock_titan_regs();
			reg = titan_readl(CPCCR) & ~(0xf << (wd_ctr * 4));
			titan_writel(reg, CPCCR);
			reg = titan_readl(CPGIG1ER);
			titan_writel(reg & ~(0x100 << wd_ctr), CPGIG1ER);
			iob();
			unlock_titan_regs();
			free_irq(wd_irq, &miscdev);
			printk(KERN_INFO "%s: watchdog stopped\n", wdt_gpi_name);
		} else {
			printk(KERN_NOTICE "%s: unexpected close() -"
				" watchdog left running\n",
				wdt_gpi_name);
			wdt_gpi_set_timeout(timeout);
			__module_get(THIS_MODULE);
			locked = 1;
		}
	}

	atomic_inc(&opencnt);
	return 0;
}



static ssize_t
wdt_gpi_write(struct file *f, const char __user *d, size_t s, loff_t *o)
{
	char val;

	wdt_gpi_set_timeout(timeout);
	expect_close = (s > 0) && !get_user(val, d) && (val == 'V');
	return s ? 1 : 0;
}



static long
wdt_gpi_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	long res = -ENOTTY;
	const long size = _IOC_SIZE(cmd);
	int stat;
	static struct watchdog_info wdinfo = {
		.identity               = "RM9xxx/GPI watchdog",
		.firmware_version       = 0,
		.options                = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING
	};

	if (unlikely(_IOC_TYPE(cmd) != WATCHDOG_IOCTL_BASE))
		return -ENOTTY;

	if ((_IOC_DIR(cmd) & _IOC_READ)
	    && !access_ok(VERIFY_WRITE, arg, size))
		return -EFAULT;

	if ((_IOC_DIR(cmd) & _IOC_WRITE)
	    && !access_ok(VERIFY_READ, arg, size))
		return -EFAULT;

	expect_close = 0;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		wdinfo.options = nowayout ?
			WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING :
			WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE;
		res = __copy_to_user((void __user *)arg, &wdinfo, size) ?
			-EFAULT : size;
		break;

	case WDIOC_GETSTATUS:
		break;

	case WDIOC_GETBOOTSTATUS:
		stat = (*(volatile char *) flagaddr & 0x01)
			? WDIOF_CARDRESET : 0;
		res = __copy_to_user((void __user *)arg, &stat, size) ?
			-EFAULT : size;
		break;

	case WDIOC_SETOPTIONS:
		break;

	case WDIOC_KEEPALIVE:
		wdt_gpi_set_timeout(timeout);
		res = size;
		break;

	case WDIOC_SETTIMEOUT:
		{
			int val;
			if (unlikely(__copy_from_user(&val, (const void __user *) arg,
					size))) {
				res = -EFAULT;
				break;
			}

			if (val > 32)
				val = 32;
			timeout = val;
			wdt_gpi_set_timeout(val);
			res = size;
			printk("%s: timeout set to %u seconds\n",
				wdt_gpi_name, timeout);
		}
		break;

	case WDIOC_GETTIMEOUT:
		res = __copy_to_user((void __user *) arg, &timeout, size) ?
			-EFAULT : size;
		break;
	}

	return res;
}




static irqreturn_t wdt_gpi_irqhdl(int irq, void *ctxt, struct pt_regs *regs)
{
	if (!unlikely(__raw_readl(wd_regs + 0x0008) & 0x1))
		return IRQ_NONE;
	__raw_writel(0x1, wd_regs + 0x0008);


	printk(KERN_WARNING "%s: watchdog expired - resetting system\n",
		wdt_gpi_name);

	*(volatile char *) flagaddr |= 0x01;
	*(volatile char *) resetaddr = powercycle ? 0x01 : 0x2;
	iob();
	while (1)
		cpu_relax();
}



static int
wdt_gpi_notify(struct notifier_block *this, unsigned long code, void *unused)
{
	if(code == SYS_DOWN || code == SYS_HALT) {
		u32 reg;

		lock_titan_regs();
		reg = titan_readl(CPCCR) & ~(0xf << (wd_ctr * 4));
		titan_writel(reg, CPCCR);
		reg = titan_readl(CPGIG1ER);
		titan_writel(reg & ~(0x100 << wd_ctr), CPGIG1ER);
		iob();
		unlock_titan_regs();
	}
	return NOTIFY_DONE;
}



static int __init wdt_gpi_init_module(void)
{
	atomic_set(&opencnt, 1);
	if (timeout > MAX_TIMEOUT_SECONDS)
		timeout = MAX_TIMEOUT_SECONDS;
	return driver_register(&wdt_gpi_driver);
}



static void __exit wdt_gpi_cleanup_module(void)
{
	driver_unregister(&wdt_gpi_driver);
}

module_init(wdt_gpi_init_module);
module_exit(wdt_gpi_cleanup_module);



MODULE_AUTHOR("Thomas Koeller <thomas.koeller@baslerweb.com>");
MODULE_DESCRIPTION("Basler eXcite watchdog driver for gpi devices");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_PARM_DESC(timeout, "Watchdog timeout in seconds");
MODULE_PARM_DESC(resetaddr, "Address to write to to force a reset");
MODULE_PARM_DESC(flagaddr, "Address to write to boot flags to");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be disabled once started");
MODULE_PARM_DESC(powercycle, "Cycle power if watchdog expires");
