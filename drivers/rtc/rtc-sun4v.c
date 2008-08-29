/* rtc-sun4c.c: Hypervisor based RTC for SUN4V systems.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include <asm/hypervisor.h>

MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_DESCRIPTION("SUN4V RTC driver");
MODULE_LICENSE("GPL");

struct sun4v_rtc {
	struct rtc_device	*rtc;
	spinlock_t		lock;
};

static unsigned long hypervisor_get_time(void)
{
	unsigned long ret, time;
	int retries = 10000;

retry:
	ret = sun4v_tod_get(&time);
	if (ret == HV_EOK)
		return time;
	if (ret == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		printk(KERN_WARNING "SUN4V: tod_get() timed out.\n");
		return 0;
	}
	printk(KERN_WARNING "SUN4V: tod_get() not supported.\n");
	return 0;
}

static int sun4v_read_time(struct device *dev, struct rtc_time *tm)
{
	struct sun4v_rtc *p = dev_get_drvdata(dev);
	unsigned long flags, secs;

	spin_lock_irqsave(&p->lock, flags);
	secs = hypervisor_get_time();
	spin_unlock_irqrestore(&p->lock, flags);

	rtc_time_to_tm(secs, tm);

	return 0;
}

static int hypervisor_set_time(unsigned long secs)
{
	unsigned long ret;
	int retries = 10000;

retry:
	ret = sun4v_tod_set(secs);
	if (ret == HV_EOK)
		return 0;
	if (ret == HV_EWOULDBLOCK) {
		if (--retries > 0) {
			udelay(100);
			goto retry;
		}
		printk(KERN_WARNING "SUN4V: tod_set() timed out.\n");
		return -EAGAIN;
	}
	printk(KERN_WARNING "SUN4V: tod_set() not supported.\n");
	return -EOPNOTSUPP;
}

static int sun4v_set_time(struct device *dev, struct rtc_time *tm)
{
	struct sun4v_rtc *p = dev_get_drvdata(dev);
	unsigned long flags, secs;
	int err;

	err = rtc_tm_to_time(tm, &secs);
	if (err)
		return err;

	spin_lock_irqsave(&p->lock, flags);
	err = hypervisor_set_time(secs);
	spin_unlock_irqrestore(&p->lock, flags);

	return err;
}

static const struct rtc_class_ops sun4v_rtc_ops = {
	.read_time	= sun4v_read_time,
	.set_time	= sun4v_set_time,
};

static int __devinit sun4v_rtc_probe(struct platform_device *pdev)
{
	struct sun4v_rtc *p = kzalloc(sizeof(*p), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	spin_lock_init(&p->lock);

	p->rtc = rtc_device_register("sun4v", &pdev->dev,
				     &sun4v_rtc_ops, THIS_MODULE);
	if (IS_ERR(p->rtc)) {
		int err = PTR_ERR(p->rtc);
		kfree(p);
		return err;
	}
	platform_set_drvdata(pdev, p);
	return 0;
}

static int __devexit sun4v_rtc_remove(struct platform_device *pdev)
{
	struct sun4v_rtc *p = platform_get_drvdata(pdev);

	rtc_device_unregister(p->rtc);
	kfree(p);

	return 0;
}

static struct platform_driver sun4v_rtc_driver = {
	.driver		= {
		.name	= "rtc-sun4v",
		.owner	= THIS_MODULE,
	},
	.probe		= sun4v_rtc_probe,
	.remove		= __devexit_p(sun4v_rtc_remove),
};

static int __init sun4v_rtc_init(void)
{
	return platform_driver_register(&sun4v_rtc_driver);
}

static void __exit sun4v_rtc_exit(void)
{
	platform_driver_unregister(&sun4v_rtc_driver);
}

module_init(sun4v_rtc_init);
module_exit(sun4v_rtc_exit);
