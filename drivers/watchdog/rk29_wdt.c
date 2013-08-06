/* linux/drivers/watchdog/rk29_wdt.c
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *	hhb@rock-chips.com
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
*/


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <asm/mach/map.h>


/* RK29 registers define */
#define RK29_WDT_CR 	0X00
#define RK29_WDT_TORR 	0X04
#define RK29_WDT_CCVR 	0X08
#define RK29_WDT_CRR 	0X0C
#define RK29_WDT_STAT 	0X10
#define RK29_WDT_EOI 	0X14

#define RK29_WDT_EN     1
#define RK29_RESPONSE_MODE	1
#define RK29_RESET_PULSE    4

//THAT wdt's clock is 24MHZ
#define RK29_WDT_2730US 	0
#define RK29_WDT_5460US 	1
#define RK29_WDT_10920US 	2
#define RK29_WDT_21840US 	3
#define RK29_WDT_43680US 	4
#define RK29_WDT_87360US 	5
#define RK29_WDT_174720US 	6
#define RK29_WDT_349440US 	7
#define RK29_WDT_698880US 	8
#define RK29_WDT_1397760US 	9
#define RK29_WDT_2795520US 	10
#define RK29_WDT_5591040US 	11
#define RK29_WDT_11182080US 	12
#define RK29_WDT_22364160US 	13
#define RK29_WDT_44728320US 	14
#define RK29_WDT_89456640US 	15

/*
#define CONFIG_RK29_WATCHDOG_ATBOOT		(1)
#define CONFIG_RK29_WATCHDOG_DEFAULT_TIME	10      //unit second
#define CONFIG_RK29_WATCHDOG_DEBUG	1
*/

static int nowayout	= WATCHDOG_NOWAYOUT;
static int tmr_margin	= CONFIG_RK29_WATCHDOG_DEFAULT_TIME;
#ifdef CONFIG_RK29_WATCHDOG_ATBOOT
static int tmr_atboot	= 1;
#else
static int tmr_atboot	= 0;
#endif

static int soft_noboot;

#ifdef CONFIG_RK29_WATCHDOG_DEBUG
static int debug	= 1;
#else
static int debug	= 0;
#endif

module_param(tmr_margin,  int, 0);
module_param(tmr_atboot,  int, 0);
module_param(nowayout,    int, 0);
module_param(soft_noboot, int, 0);
module_param(debug,	  int, 0);

MODULE_PARM_DESC(tmr_margin, "Watchdog tmr_margin in seconds. default="
		__MODULE_STRING(CONFIG_RK29_WATCHDOG_DEFAULT_TIME) ")");
MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(CONFIG_RK29_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");
MODULE_PARM_DESC(soft_noboot, "Watchdog action, set to 1 to ignore reboots, "
			"0 to reboot (default depends on ONLY_TESTING)");
MODULE_PARM_DESC(debug, "Watchdog debug, set to >1 for debug, (default 0)");


static unsigned long open_lock;
static struct device    *wdt_dev;	/* platform device attached to */
static struct resource	*wdt_mem;
static struct resource	*wdt_irq;
static struct clk	*wdt_clock;
static void __iomem	*wdt_base;
static char		 expect_close;


/* watchdog control routines */

#define DBG(msg...) do { \
	if (debug) \
		printk(KERN_INFO msg); \
	} while (0)

#define wdt_writel(v, offset) do { writel_relaxed(v, wdt_base + offset); dsb(); } while (0)

/* functions */
void rk29_wdt_keepalive(void)
{
	if (wdt_base)
		wdt_writel(0x76, RK29_WDT_CRR);
}

static void __rk29_wdt_stop(void)
{
	rk29_wdt_keepalive();    //feed dog
	wdt_writel(0x0a, RK29_WDT_CR);
}

static void rk29_wdt_stop(void)
{
	__rk29_wdt_stop();
	clk_disable(wdt_clock);
}

/* timeout unit us */
static int rk29_wdt_set_heartbeat(int timeout)
{
	unsigned int freq = clk_get_rate(wdt_clock);
	unsigned int count;
	unsigned int torr = 0;
	unsigned int acc = 1;

	if (timeout < 1)
		return -EINVAL;

//	freq /= 1000000;
	count = timeout * freq;
	count /= 0x10000;

	while(acc < count){
		acc *= 2;
		torr++;
	}
	if(torr > 15){
		torr = 15;
	}
	DBG("%s:%d\n", __func__, torr);
	wdt_writel(torr, RK29_WDT_TORR);
	return 0;
}

static void rk29_wdt_start(void)
{
	unsigned long wtcon;
	clk_enable(wdt_clock);
	rk29_wdt_set_heartbeat(tmr_margin);
	wtcon = (RK29_WDT_EN << 0) | (RK29_RESPONSE_MODE << 1) | (RK29_RESET_PULSE << 2);
	wdt_writel(wtcon, RK29_WDT_CR);
}

/*
 *	/dev/watchdog handling
 */

static int rk29_wdt_open(struct inode *inode, struct file *file)
{
	DBG("%s\n", __func__);
	if (test_and_set_bit(0, &open_lock))
		return -EBUSY;

	if (nowayout)
		__module_get(THIS_MODULE);

	expect_close = 0;

	/* start the timer */
	rk29_wdt_start();
	return nonseekable_open(inode, file);
}

static int rk29_wdt_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */
	DBG("%s\n", __func__);
	if (expect_close == 42)
		rk29_wdt_stop();
	else {
		dev_err(wdt_dev, "Unexpected close, not stopping watchdog\n");
		rk29_wdt_keepalive();
	}
	expect_close = 0;
	clear_bit(0, &open_lock);
	return 0;
}

static ssize_t rk29_wdt_write(struct file *file, const char __user *data,
				size_t len, loff_t *ppos)
{
	/*
	 *	Refresh the timer.
	 */
	DBG("%s\n", __func__);
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
		rk29_wdt_keepalive();
	}
	return len;
}

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE)

static const struct watchdog_info rk29_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"rk29 Watchdog",
};


static long rk29_wdt_ioctl(struct file *file,	unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int new_margin;
	DBG("%s\n", __func__);
	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &rk29_wdt_ident,
			sizeof(rk29_wdt_ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		DBG("%s:rk29_wdt_keepalive\n", __func__);
		rk29_wdt_keepalive();
		return 0;
	case WDIOC_SETTIMEOUT:
		if (get_user(new_margin, p))
			return -EFAULT;
		if (rk29_wdt_set_heartbeat(new_margin))
			return -EINVAL;
		rk29_wdt_keepalive();
		return put_user(tmr_margin, p);
	case WDIOC_GETTIMEOUT:
		return put_user(tmr_margin, p);
	default:
		return -ENOTTY;
	}
}



/* kernel interface */

static const struct file_operations rk29_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= rk29_wdt_write,
	.unlocked_ioctl	= rk29_wdt_ioctl,
	.open		= rk29_wdt_open,
	.release	= rk29_wdt_release,
};

static struct miscdevice rk29_wdt_miscdev = {
	.minor		= WATCHDOG_MINOR,
	.name		= "watchdog",
	.fops		= &rk29_wdt_fops,
};


/* interrupt handler code */

static irqreturn_t rk29_wdt_irq_handler(int irqno, void *param)
{
	DBG("RK29_wdt:watchdog timer expired (irq)\n");
	rk29_wdt_keepalive();
	return IRQ_HANDLED;
}


/* device interface */

static int __devinit rk29_wdt_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct device *dev;
	int started = 0;
	int ret;
	int size;

	dev = &pdev->dev;
	wdt_dev = &pdev->dev;

	/* get the memory region for the watchdog timer */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "no memory resource specified\n");
		return -ENOENT;
	}

	size = (res->end - res->start) + 1;
	wdt_mem = request_mem_region(res->start, size, pdev->name);
	if (wdt_mem == NULL) {
		dev_err(dev, "failed to get memory region\n");
		ret = -ENOENT;
		goto err_req;
	}

	wdt_base = ioremap(res->start, size);
	if (wdt_base == NULL) {
		dev_err(dev, "failed to ioremap() region\n");
		ret = -EINVAL;
		goto err_req;
	}

	DBG("probe: mapped wdt_base=%p\n", wdt_base);


#ifdef	CONFIG_RK29_FEED_DOG_BY_INTE

	wdt_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (wdt_irq == NULL) {
		dev_err(dev, "no irq resource specified\n");
		ret = -ENOENT;
		goto err_map;
	}

	ret = request_irq(wdt_irq->start, rk29_wdt_irq_handler, 0, pdev->name, pdev);

	if (ret != 0) {
		dev_err(dev, "failed to install irq (%d)\n", ret);
		goto err_map;
	}

#endif

	wdt_clock = clk_get(&pdev->dev, "wdt");
	if (IS_ERR(wdt_clock)) {
		wdt_clock = clk_get(&pdev->dev, "pclk_wdt");
	}
	if (IS_ERR(wdt_clock)) {
		dev_err(dev, "failed to find watchdog clock source\n");
		ret = PTR_ERR(wdt_clock);
		goto err_irq;
	}

	ret = misc_register(&rk29_wdt_miscdev);
	if (ret) {
		dev_err(dev, "cannot register miscdev on minor=%d (%d)\n",
			WATCHDOG_MINOR, ret);
		goto err_clk;
	}
	if (tmr_atboot && started == 0) {
		dev_info(dev, "starting watchdog timer\n");
		rk29_wdt_start();
	} else if (!tmr_atboot) {
		/* if we're not enabling the watchdog, then ensure it is
		 * disabled if it has been left running from the bootloader
		 * or other source */

		rk29_wdt_stop();
	}

	return 0;

 err_clk:
	clk_disable(wdt_clock);
	clk_put(wdt_clock);

 err_irq:
	free_irq(wdt_irq->start, pdev);

 err_map:
	iounmap(wdt_base);

 err_req:
	release_resource(wdt_mem);
	kfree(wdt_mem);

	return ret;
}

static int __devexit rk29_wdt_remove(struct platform_device *dev)
{
	release_resource(wdt_mem);
	kfree(wdt_mem);
	wdt_mem = NULL;

	free_irq(wdt_irq->start, dev);
	wdt_irq = NULL;

	clk_disable(wdt_clock);
	clk_put(wdt_clock);
	wdt_clock = NULL;

	iounmap(wdt_base);
	misc_deregister(&rk29_wdt_miscdev);

	return 0;
}

static void rk29_wdt_shutdown(struct platform_device *dev)
{
	rk29_wdt_stop();
}

#ifdef CONFIG_PM

static int rk29_wdt_suspend(struct platform_device *dev, pm_message_t state)
{
	rk29_wdt_stop();
	return 0;
}

static int rk29_wdt_resume(struct platform_device *dev)
{
	rk29_wdt_start();
	return 0;
}

#else
#define rk29_wdt_suspend NULL
#define rk29_wdt_resume  NULL
#endif /* CONFIG_PM */


static struct platform_driver rk29_wdt_driver = {
	.probe		= rk29_wdt_probe,
	.remove		= __devexit_p(rk29_wdt_remove),
	.shutdown	= rk29_wdt_shutdown,
	.suspend	= rk29_wdt_suspend,
	.resume		= rk29_wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rk29-wdt",
	},
};


static char banner[] __initdata =
	KERN_INFO "RK29 Watchdog Timer, (c) 2011 Rockchip Electronics\n";

static int __init watchdog_init(void)
{
	printk(banner);
	return platform_driver_register(&rk29_wdt_driver);
}

static void __exit watchdog_exit(void)
{
	platform_driver_unregister(&rk29_wdt_driver);
}

subsys_initcall(watchdog_init);
module_exit(watchdog_exit);

MODULE_AUTHOR("hhb@rock-chips.com");
MODULE_DESCRIPTION("RK29 Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:rk29-wdt");
