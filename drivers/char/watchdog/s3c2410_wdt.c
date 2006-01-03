/* linux/drivers/char/watchdog/s3c2410_wdt.c
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Watchdog Timer Support
 *
 * Based on, softdog.c by Alan Cox,
 *     (c) Copyright 1996 Alan Cox <alan@redhat.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Changelog:
 *	05-Oct-2004	BJD	Added semaphore init to stop crashes on open
 *				Fixed tmr_count / wdt_count confusion
 *				Added configurable debug
 *
 *	11-Jan-2005	BJD	Fixed divide-by-2 in timeout code
 *
 *	25-Jan-2005	DA	Added suspend/resume support
 *				Replaced reboot notifier with .shutdown method
 *
 *	10-Mar-2005	LCVR	Changed S3C2410_VA to S3C24XX_VA
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <asm/arch/map.h>
#include <asm/hardware/clock.h>

#undef S3C24XX_VA_WATCHDOG
#define S3C24XX_VA_WATCHDOG (0)

#include <asm/arch/regs-watchdog.h>

#define PFX "s3c2410-wdt: "

#define CONFIG_S3C2410_WATCHDOG_ATBOOT		(0)
#define CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME	(15)

static int nowayout = WATCHDOG_NOWAYOUT;
static int tmr_margin	= CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME;
static int tmr_atboot	= CONFIG_S3C2410_WATCHDOG_ATBOOT;
static int soft_noboot	= 0;
static int debug	= 0;

module_param(tmr_margin,  int, 0);
module_param(tmr_atboot,  int, 0);
module_param(nowayout,    int, 0);
module_param(soft_noboot, int, 0);
module_param(debug,	  int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. default=" __MODULE_STRING(CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME) ")");

MODULE_PARM_DESC(tmr_atboot, "Watchdog is started at boot time if set to 1, default=" __MODULE_STRING(CONFIG_S3C2410_WATCHDOG_ATBOOT));

MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

MODULE_PARM_DESC(soft_noboot, "Watchdog action, set to 1 to ignore reboots, 0 to reboot (default depends on ONLY_TESTING)");

MODULE_PARM_DESC(debug, "Watchdog debug, set to >1 for debug, (default 0)");


typedef enum close_state {
	CLOSE_STATE_NOT,
	CLOSE_STATE_ALLOW=0x4021
} close_state_t;

static DECLARE_MUTEX(open_lock);

static struct resource	*wdt_mem;
static struct resource	*wdt_irq;
static struct clk	*wdt_clock;
static void __iomem	*wdt_base;
static unsigned int	 wdt_count;
static close_state_t	 allow_close;

/* watchdog control routines */

#define DBG(msg...) do { \
	if (debug) \
		printk(KERN_INFO msg); \
	} while(0)

/* functions */

static int s3c2410wdt_keepalive(void)
{
	writel(wdt_count, wdt_base + S3C2410_WTCNT);
	return 0;
}

static int s3c2410wdt_stop(void)
{
	unsigned long wtcon;

	wtcon = readl(wdt_base + S3C2410_WTCON);
	wtcon &= ~(S3C2410_WTCON_ENABLE | S3C2410_WTCON_RSTEN);
	writel(wtcon, wdt_base + S3C2410_WTCON);

	return 0;
}

static int s3c2410wdt_start(void)
{
	unsigned long wtcon;

	s3c2410wdt_stop();

	wtcon = readl(wdt_base + S3C2410_WTCON);
	wtcon |= S3C2410_WTCON_ENABLE | S3C2410_WTCON_DIV128;

	if (soft_noboot) {
		wtcon |= S3C2410_WTCON_INTEN;
		wtcon &= ~S3C2410_WTCON_RSTEN;
	} else {
		wtcon &= ~S3C2410_WTCON_INTEN;
		wtcon |= S3C2410_WTCON_RSTEN;
	}

	DBG("%s: wdt_count=0x%08x, wtcon=%08lx\n",
	    __FUNCTION__, wdt_count, wtcon);

	writel(wdt_count, wdt_base + S3C2410_WTDAT);
	writel(wdt_count, wdt_base + S3C2410_WTCNT);
	writel(wtcon, wdt_base + S3C2410_WTCON);

	return 0;
}

static int s3c2410wdt_set_heartbeat(int timeout)
{
	unsigned int freq = clk_get_rate(wdt_clock);
	unsigned int count;
	unsigned int divisor = 1;
	unsigned long wtcon;

	if (timeout < 1)
		return -EINVAL;

	freq /= 128;
	count = timeout * freq;

	DBG("%s: count=%d, timeout=%d, freq=%d\n",
	    __FUNCTION__, count, timeout, freq);

	/* if the count is bigger than the watchdog register,
	   then work out what we need to do (and if) we can
	   actually make this value
	*/

	if (count >= 0x10000) {
		for (divisor = 1; divisor <= 0x100; divisor++) {
			if ((count / divisor) < 0x10000)
				break;
		}

		if ((count / divisor) >= 0x10000) {
			printk(KERN_ERR PFX "timeout %d too big\n", timeout);
			return -EINVAL;
		}
	}

	tmr_margin = timeout;

	DBG("%s: timeout=%d, divisor=%d, count=%d (%08x)\n",
	    __FUNCTION__, timeout, divisor, count, count/divisor);

	count /= divisor;
	wdt_count = count;

	/* update the pre-scaler */
	wtcon = readl(wdt_base + S3C2410_WTCON);
	wtcon &= ~S3C2410_WTCON_PRESCALE_MASK;
	wtcon |= S3C2410_WTCON_PRESCALE(divisor-1);

	writel(count, wdt_base + S3C2410_WTDAT);
	writel(wtcon, wdt_base + S3C2410_WTCON);

	return 0;
}

/*
 *	/dev/watchdog handling
 */

static int s3c2410wdt_open(struct inode *inode, struct file *file)
{
	if(down_trylock(&open_lock))
		return -EBUSY;

	if (nowayout) {
		__module_get(THIS_MODULE);
	} else {
		allow_close = CLOSE_STATE_ALLOW;
	}

	/* start the timer */
	s3c2410wdt_start();
	return nonseekable_open(inode, file);
}

static int s3c2410wdt_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */
	if (allow_close == CLOSE_STATE_ALLOW) {
		s3c2410wdt_stop();
	} else {
		printk(KERN_CRIT PFX "Unexpected close, not stopping watchdog!\n");
		s3c2410wdt_keepalive();
	}

	allow_close = CLOSE_STATE_NOT;
	up(&open_lock);
	return 0;
}

static ssize_t s3c2410wdt_write(struct file *file, const char __user *data,
				size_t len, loff_t *ppos)
{
	/*
	 *	Refresh the timer.
	 */
	if(len) {
		if (!nowayout) {
			size_t i;

			/* In case it was set long ago */
			allow_close = CLOSE_STATE_NOT;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					allow_close = CLOSE_STATE_ALLOW;
			}
		}

		s3c2410wdt_keepalive();
	}
	return len;
}

#define OPTIONS WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE

static struct watchdog_info s3c2410_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"S3C2410 Watchdog",
};


static int s3c2410wdt_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin;

	switch (cmd) {
		default:
			return -ENOIOCTLCMD;

		case WDIOC_GETSUPPORT:
			return copy_to_user(argp, &s3c2410_wdt_ident,
				sizeof(s3c2410_wdt_ident)) ? -EFAULT : 0;

		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, p);

		case WDIOC_KEEPALIVE:
			s3c2410wdt_keepalive();
			return 0;

		case WDIOC_SETTIMEOUT:
			if (get_user(new_margin, p))
				return -EFAULT;

			if (s3c2410wdt_set_heartbeat(new_margin))
				return -EINVAL;

			s3c2410wdt_keepalive();
			return put_user(tmr_margin, p);

		case WDIOC_GETTIMEOUT:
			return put_user(tmr_margin, p);
	}
}

/* kernel interface */

static struct file_operations s3c2410wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= s3c2410wdt_write,
	.ioctl		= s3c2410wdt_ioctl,
	.open		= s3c2410wdt_open,
	.release	= s3c2410wdt_release,
};

static struct miscdevice s3c2410wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &s3c2410wdt_fops,
};

/* interrupt handler code */

static irqreturn_t s3c2410wdt_irq(int irqno, void *param,
				  struct pt_regs *regs)
{
	printk(KERN_INFO PFX "Watchdog timer expired!\n");

	s3c2410wdt_keepalive();
	return IRQ_HANDLED;
}
/* device interface */

static int s3c2410wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	int started = 0;
	int ret;
	int size;

	DBG("%s: probe=%p\n", __FUNCTION__, pdev);

	/* get the memory region for the watchdog timer */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_INFO PFX "failed to get memory region resouce\n");
		return -ENOENT;
	}

	size = (res->end-res->start)+1;
	wdt_mem = request_mem_region(res->start, size, pdev->name);
	if (wdt_mem == NULL) {
		printk(KERN_INFO PFX "failed to get memory region\n");
		return -ENOENT;
	}

	wdt_base = ioremap(res->start, size);
	if (wdt_base == 0) {
		printk(KERN_INFO PFX "failed to ioremap() region\n");
		return -EINVAL;
	}

	DBG("probe: mapped wdt_base=%p\n", wdt_base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		printk(KERN_INFO PFX "failed to get irq resource\n");
		return -ENOENT;
	}

	ret = request_irq(res->start, s3c2410wdt_irq, 0, pdev->name, pdev);
	if (ret != 0) {
		printk(KERN_INFO PFX "failed to install irq (%d)\n", ret);
		return ret;
	}

	wdt_clock = clk_get(&pdev->dev, "watchdog");
	if (wdt_clock == NULL) {
		printk(KERN_INFO PFX "failed to find watchdog clock source\n");
		return -ENOENT;
	}

	clk_use(wdt_clock);
	clk_enable(wdt_clock);

	/* see if we can actually set the requested timer margin, and if
	 * not, try the default value */

	if (s3c2410wdt_set_heartbeat(tmr_margin)) {
		started = s3c2410wdt_set_heartbeat(CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME);

		if (started == 0) {
			printk(KERN_INFO PFX "tmr_margin value out of range, default %d used\n",
			       CONFIG_S3C2410_WATCHDOG_DEFAULT_TIME);
		} else {
			printk(KERN_INFO PFX "default timer value is out of range, cannot start\n");
		}
	}

	ret = misc_register(&s3c2410wdt_miscdev);
	if (ret) {
		printk (KERN_ERR PFX "cannot register miscdev on minor=%d (%d)\n",
			WATCHDOG_MINOR, ret);
		return ret;
	}

	if (tmr_atboot && started == 0) {
		printk(KERN_INFO PFX "Starting Watchdog Timer\n");
		s3c2410wdt_start();
	}

	return 0;
}

static int s3c2410wdt_remove(struct platform_device *dev)
{
	if (wdt_mem != NULL) {
		release_resource(wdt_mem);
		kfree(wdt_mem);
		wdt_mem = NULL;
	}

	if (wdt_irq != NULL) {
		free_irq(wdt_irq->start, dev);
		wdt_irq = NULL;
	}

	if (wdt_clock != NULL) {
		clk_disable(wdt_clock);
		clk_unuse(wdt_clock);
		clk_put(wdt_clock);
		wdt_clock = NULL;
	}

	misc_deregister(&s3c2410wdt_miscdev);
	return 0;
}

static void s3c2410wdt_shutdown(struct platform_device *dev)
{
	s3c2410wdt_stop();	
}

#ifdef CONFIG_PM

static unsigned long wtcon_save;
static unsigned long wtdat_save;

static int s3c2410wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	/* Save watchdog state, and turn it off. */
	wtcon_save = readl(wdt_base + S3C2410_WTCON);
	wtdat_save = readl(wdt_base + S3C2410_WTDAT);

	/* Note that WTCNT doesn't need to be saved. */
	s3c2410wdt_stop();

	return 0;
}

static int s3c2410wdt_resume(struct platform_device *dev)
{
	/* Restore watchdog state. */

	writel(wtdat_save, wdt_base + S3C2410_WTDAT);
	writel(wtdat_save, wdt_base + S3C2410_WTCNT); /* Reset count */
	writel(wtcon_save, wdt_base + S3C2410_WTCON);

	printk(KERN_INFO PFX "watchdog %sabled\n",
	       (wtcon_save & S3C2410_WTCON_ENABLE) ? "en" : "dis");

	return 0;
}

#else
#define s3c2410wdt_suspend NULL
#define s3c2410wdt_resume  NULL
#endif /* CONFIG_PM */


static struct platform_driver s3c2410wdt_driver = {
	.probe		= s3c2410wdt_probe,
	.remove		= s3c2410wdt_remove,
	.shutdown	= s3c2410wdt_shutdown,
	.suspend	= s3c2410wdt_suspend,
	.resume		= s3c2410wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s3c2410-wdt",
	},
};


static char banner[] __initdata = KERN_INFO "S3C2410 Watchdog Timer, (c) 2004 Simtec Electronics\n";

static int __init watchdog_init(void)
{
	printk(banner);
	return platform_driver_register(&s3c2410wdt_driver);
}

static void __exit watchdog_exit(void)
{
	platform_driver_unregister(&s3c2410wdt_driver);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>, "
	      "Dimitry Andric <dimitry.andric@tomtom.com>");
MODULE_DESCRIPTION("S3C2410 Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
