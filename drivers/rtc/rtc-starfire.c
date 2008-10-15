/* rtc-starfire.c: Starfire platform RTC driver.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

#include <asm/oplib.h>

MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_DESCRIPTION("Starfire RTC driver");
MODULE_LICENSE("GPL");

struct starfire_rtc {
	struct rtc_device	*rtc;
	spinlock_t		lock;
};

static u32 starfire_get_time(void)
{
	static char obp_gettod[32];
	static u32 unix_tod;

	sprintf(obp_gettod, "h# %08x unix-gettod",
		(unsigned int) (long) &unix_tod);
	prom_feval(obp_gettod);

	return unix_tod;
}

static int starfire_read_time(struct device *dev, struct rtc_time *tm)
{
	struct starfire_rtc *p = dev_get_drvdata(dev);
	unsigned long flags, secs;

	spin_lock_irqsave(&p->lock, flags);
	secs = starfire_get_time();
	spin_unlock_irqrestore(&p->lock, flags);

	rtc_time_to_tm(secs, tm);

	return 0;
}

static int starfire_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long secs;
	int err;

	err = rtc_tm_to_time(tm, &secs);
	if (err)
		return err;

	/* Do nothing, time is set using the service processor
	 * console on this platform.
	 */
	return 0;
}

static const struct rtc_class_ops starfire_rtc_ops = {
	.read_time	= starfire_read_time,
	.set_time	= starfire_set_time,
};

static int __devinit starfire_rtc_probe(struct platform_device *pdev)
{
	struct starfire_rtc *p = kzalloc(sizeof(*p), GFP_KERNEL);

	if (!p)
		return -ENOMEM;

	spin_lock_init(&p->lock);

	p->rtc = rtc_device_register("starfire", &pdev->dev,
				     &starfire_rtc_ops, THIS_MODULE);
	if (IS_ERR(p->rtc)) {
		int err = PTR_ERR(p->rtc);
		kfree(p);
		return err;
	}
	platform_set_drvdata(pdev, p);
	return 0;
}

static int __devexit starfire_rtc_remove(struct platform_device *pdev)
{
	struct starfire_rtc *p = platform_get_drvdata(pdev);

	rtc_device_unregister(p->rtc);
	kfree(p);

	return 0;
}

static struct platform_driver starfire_rtc_driver = {
	.driver		= {
		.name	= "rtc-starfire",
		.owner	= THIS_MODULE,
	},
	.probe		= starfire_rtc_probe,
	.remove		= __devexit_p(starfire_rtc_remove),
};

static int __init starfire_rtc_init(void)
{
	return platform_driver_register(&starfire_rtc_driver);
}

static void __exit starfire_rtc_exit(void)
{
	platform_driver_unregister(&starfire_rtc_driver);
}

module_init(starfire_rtc_init);
module_exit(starfire_rtc_exit);
