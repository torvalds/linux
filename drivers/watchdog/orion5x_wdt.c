/*
 * drivers/watchdog/orion5x_wdt.c
 *
 * Watchdog driver for Orion5x processors
 *
 * Author: Sylver Bruneau <sylver.bruneau@googlemail.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/spinlock.h>

/*
 * Watchdog timer block registers.
 */
#define TIMER_CTRL		(TIMER_VIRT_BASE + 0x0000)
#define  WDT_EN			0x0010
#define WDT_VAL			(TIMER_VIRT_BASE + 0x0024)

#define WDT_MAX_DURATION	(0xffffffff / ORION5X_TCLK)
#define WDT_IN_USE		0
#define WDT_OK_TO_CLOSE		1

static int nowayout = WATCHDOG_NOWAYOUT;
static int heartbeat =  WDT_MAX_DURATION;	/* (seconds) */
static unsigned long wdt_status;
static spinlock_t wdt_lock;

static void wdt_enable(void)
{
	u32 reg;

	spin_lock(&wdt_lock);

	/* Set watchdog duration */
	writel(ORION5X_TCLK * heartbeat, WDT_VAL);

	/* Clear watchdog timer interrupt */
	reg = readl(BRIDGE_CAUSE);
	reg &= ~WDT_INT_REQ;
	writel(reg, BRIDGE_CAUSE);

	/* Enable watchdog timer */
	reg = readl(TIMER_CTRL);
	reg |= WDT_EN;
	writel(reg, TIMER_CTRL);

	/* Enable reset on watchdog */
	reg = readl(CPU_RESET_MASK);
	reg |= WDT_RESET;
	writel(reg, CPU_RESET_MASK);

	spin_unlock(&wdt_lock);
}

static void wdt_disable(void)
{
	u32 reg;

	spin_lock(&wdt_lock);

	/* Disable reset on watchdog */
	reg = readl(CPU_RESET_MASK);
	reg &= ~WDT_RESET;
	writel(reg, CPU_RESET_MASK);

	/* Disable watchdog timer */
	reg = readl(TIMER_CTRL);
	reg &= ~WDT_EN;
	writel(reg, TIMER_CTRL);

	spin_unlock(&wdt_lock);
}

static int orion5x_wdt_get_timeleft(int *time_left)
{
	spin_lock(&wdt_lock);
	*time_left = readl(WDT_VAL) / ORION5X_TCLK;
	spin_unlock(&wdt_lock);
	return 0;
}

static int orion5x_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(WDT_IN_USE, &wdt_status))
		return -EBUSY;
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);
	wdt_enable();
	return nonseekable_open(inode, file);
}

static ssize_t orion5x_wdt_write(struct file *file, const char *data,
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

static struct watchdog_info ident = {
	.options	= WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING,
	.identity	= "Orion5x Watchdog",
};


static long orion5x_wdt_ioctl(struct file *file, unsigned int cmd,
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
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, (int *)arg);
		break;

	case WDIOC_KEEPALIVE:
		wdt_enable();
		ret = 0;
		break;

	case WDIOC_SETTIMEOUT:
		ret = get_user(time, (int *)arg);
		if (ret)
			break;

		if (time <= 0 || time > WDT_MAX_DURATION) {
			ret = -EINVAL;
			break;
		}
		heartbeat = time;
		wdt_enable();
		/* Fall through */

	case WDIOC_GETTIMEOUT:
		ret = put_user(heartbeat, (int *)arg);
		break;

	case WDIOC_GETTIMELEFT:
		if (orion5x_wdt_get_timeleft(&time)) {
			ret = -EINVAL;
			break;
		}
		ret = put_user(time, (int *)arg);
		break;
	}
	return ret;
}

static int orion5x_wdt_release(struct inode *inode, struct file *file)
{
	if (test_bit(WDT_OK_TO_CLOSE, &wdt_status))
		wdt_disable();
	else
		printk(KERN_CRIT "WATCHDOG: Device closed unexpectedly - "
					"timer will not stop\n");
	clear_bit(WDT_IN_USE, &wdt_status);
	clear_bit(WDT_OK_TO_CLOSE, &wdt_status);

	return 0;
}


static const struct file_operations orion5x_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= orion5x_wdt_write,
	.unlocked_ioctl	= orion5x_wdt_ioctl,
	.open		= orion5x_wdt_open,
	.release	= orion5x_wdt_release,
};

static struct miscdevice orion5x_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &orion5x_wdt_fops,
};

static int __init orion5x_wdt_init(void)
{
	int ret;

	spin_lock_init(&wdt_lock);

	ret = misc_register(&orion5x_wdt_miscdev);
	if (ret == 0)
		printk("Orion5x Watchdog Timer: heartbeat %d sec\n",
								heartbeat);

	return ret;
}

static void __exit orion5x_wdt_exit(void)
{
	misc_deregister(&orion5x_wdt_miscdev);
}

module_init(orion5x_wdt_init);
module_exit(orion5x_wdt_exit);

MODULE_AUTHOR("Sylver Bruneau <sylver.bruneau@googlemail.com>");
MODULE_DESCRIPTION("Orion5x Processor Watchdog");

module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog heartbeat in seconds (default is "
					__MODULE_STRING(WDT_MAX_DURATION) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started");

MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
