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
 * 2002 (c) Florian Schirmer <jolt@tuxbox.org> This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <asm/mv64x60.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/* MV64x60 WDC (config) register access definitions */
#define MV64x60_WDC_CTL1_MASK	(3 << 24)
#define MV64x60_WDC_CTL1(val)	((val & 3) << 24)
#define MV64x60_WDC_CTL2_MASK	(3 << 26)
#define MV64x60_WDC_CTL2(val)	((val & 3) << 26)

/* Flags bits */
#define MV64x60_WDOG_FLAG_OPENED	0
#define MV64x60_WDOG_FLAG_ENABLED	1

static unsigned long wdt_flags;
static int wdt_status;
static void __iomem *mv64x60_regs;
static int mv64x60_wdt_timeout;

static void mv64x60_wdt_reg_write(u32 val)
{
	/* Allow write only to CTL1 / CTL2 fields, retaining values in
	 * other fields.
	 */
	u32 data = readl(mv64x60_regs + MV64x60_WDT_WDC);
	data &= ~(MV64x60_WDC_CTL1_MASK | MV64x60_WDC_CTL2_MASK);
	data |= val;
	writel(data, mv64x60_regs + MV64x60_WDT_WDC);
}

static void mv64x60_wdt_service(void)
{
	/* Write 01 followed by 10 to CTL2 */
	mv64x60_wdt_reg_write(MV64x60_WDC_CTL2(0x01));
	mv64x60_wdt_reg_write(MV64x60_WDC_CTL2(0x02));
}

static void mv64x60_wdt_handler_disable(void)
{
	if (test_and_clear_bit(MV64x60_WDOG_FLAG_ENABLED, &wdt_flags)) {
		/* Write 01 followed by 10 to CTL1 */
		mv64x60_wdt_reg_write(MV64x60_WDC_CTL1(0x01));
		mv64x60_wdt_reg_write(MV64x60_WDC_CTL1(0x02));
		printk(KERN_NOTICE "mv64x60_wdt: watchdog deactivated\n");
	}
}

static void mv64x60_wdt_handler_enable(void)
{
	if (!test_and_set_bit(MV64x60_WDOG_FLAG_ENABLED, &wdt_flags)) {
		/* Write 01 followed by 10 to CTL1 */
		mv64x60_wdt_reg_write(MV64x60_WDC_CTL1(0x01));
		mv64x60_wdt_reg_write(MV64x60_WDC_CTL1(0x02));
		printk(KERN_NOTICE "mv64x60_wdt: watchdog activated\n");
	}
}

static int mv64x60_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(MV64x60_WDOG_FLAG_OPENED, &wdt_flags))
		return -EBUSY;

	mv64x60_wdt_service();
	mv64x60_wdt_handler_enable();

	nonseekable_open(inode, file);

	return 0;
}

static int mv64x60_wdt_release(struct inode *inode, struct file *file)
{
	mv64x60_wdt_service();

#if !defined(CONFIG_WATCHDOG_NOWAYOUT)
	mv64x60_wdt_handler_disable();
#endif

	clear_bit(MV64x60_WDOG_FLAG_OPENED, &wdt_flags);

	return 0;
}

static ssize_t mv64x60_wdt_write(struct file *file, const char __user *data,
				 size_t len, loff_t * ppos)
{
	if (len)
		mv64x60_wdt_service();

	return len;
}

static int mv64x60_wdt_ioctl(struct inode *inode, struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	int timeout;
	void __user *argp = (void __user *)arg;
	static struct watchdog_info info = {
		.options = WDIOF_KEEPALIVEPING,
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
		return -EOPNOTSUPP;

	case WDIOC_KEEPALIVE:
		mv64x60_wdt_service();
		wdt_status |= WDIOF_KEEPALIVEPING;
		break;

	case WDIOC_SETTIMEOUT:
		return -EOPNOTSUPP;

	case WDIOC_GETTIMEOUT:
		timeout = mv64x60_wdt_timeout * HZ;
		if (put_user(timeout, (int __user *)argp))
			return -EFAULT;
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static struct file_operations mv64x60_wdt_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.write = mv64x60_wdt_write,
	.ioctl = mv64x60_wdt_ioctl,
	.open = mv64x60_wdt_open,
	.release = mv64x60_wdt_release,
};

static struct miscdevice mv64x60_wdt_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &mv64x60_wdt_fops,
};

static int __devinit mv64x60_wdt_probe(struct device *dev)
{
	struct platform_device *pd = to_platform_device(dev);
	struct mv64x60_wdt_pdata *pdata = pd->dev.platform_data;
	int bus_clk = 133;

	mv64x60_wdt_timeout = 10;
	if (pdata) {
		mv64x60_wdt_timeout = pdata->timeout;
		bus_clk = pdata->bus_clk;
	}

	mv64x60_regs = mv64x60_get_bridge_vbase();

	writel((mv64x60_wdt_timeout * (bus_clk * 1000000)) >> 8,
	       mv64x60_regs + MV64x60_WDT_WDC);

	return misc_register(&mv64x60_wdt_miscdev);
}

static int __devexit mv64x60_wdt_remove(struct device *dev)
{
	misc_deregister(&mv64x60_wdt_miscdev);

	mv64x60_wdt_service();
	mv64x60_wdt_handler_disable();

	return 0;
}

static struct device_driver mv64x60_wdt_driver = {
	.name = MV64x60_WDT_NAME,
	.bus = &platform_bus_type,
	.probe = mv64x60_wdt_probe,
	.remove = __devexit_p(mv64x60_wdt_remove),
};

static struct platform_device *mv64x60_wdt_dev;

static int __init mv64x60_wdt_init(void)
{
	int ret;

	printk(KERN_INFO "MV64x60 watchdog driver\n");

	mv64x60_wdt_dev = platform_device_register_simple(MV64x60_WDT_NAME,
							  -1, NULL, 0);
	if (IS_ERR(mv64x60_wdt_dev)) {
		ret = PTR_ERR(mv64x60_wdt_dev);
		goto out;
	}

	ret = driver_register(&mv64x60_wdt_driver);
      out:
	return ret;
}

static void __exit mv64x60_wdt_exit(void)
{
	driver_unregister(&mv64x60_wdt_driver);
	platform_device_unregister(mv64x60_wdt_dev);
}

module_init(mv64x60_wdt_init);
module_exit(mv64x60_wdt_exit);

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("MV64x60 watchdog driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
