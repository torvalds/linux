/*
 * drivers/rtc/rtc-spear.c
 *
 * Copyright (C) 2010 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/* RTC registers */
#define TIME_REG		0x00
#define DATE_REG		0x04
#define ALARM_TIME_REG		0x08
#define ALARM_DATE_REG		0x0C
#define CTRL_REG		0x10
#define STATUS_REG		0x14

/* TIME_REG & ALARM_TIME_REG */
#define SECONDS_UNITS		(0xf<<0)	/* seconds units position */
#define SECONDS_TENS		(0x7<<4)	/* seconds tens position */
#define MINUTES_UNITS		(0xf<<8)	/* minutes units position */
#define MINUTES_TENS		(0x7<<12)	/* minutes tens position */
#define HOURS_UNITS		(0xf<<16)	/* hours units position */
#define HOURS_TENS		(0x3<<20)	/* hours tens position */

/* DATE_REG & ALARM_DATE_REG */
#define DAYS_UNITS		(0xf<<0)	/* days units position */
#define DAYS_TENS		(0x3<<4)	/* days tens position */
#define MONTHS_UNITS		(0xf<<8)	/* months units position */
#define MONTHS_TENS		(0x1<<12)	/* months tens position */
#define YEARS_UNITS		(0xf<<16)	/* years units position */
#define YEARS_TENS		(0xf<<20)	/* years tens position */
#define YEARS_HUNDREDS		(0xf<<24)	/* years hundereds position */
#define YEARS_MILLENIUMS	(0xf<<28)	/* years millenium position */

/* MASK SHIFT TIME_REG & ALARM_TIME_REG*/
#define SECOND_SHIFT		0x00		/* seconds units */
#define MINUTE_SHIFT		0x08		/* minutes units position */
#define HOUR_SHIFT		0x10		/* hours units position */
#define MDAY_SHIFT		0x00		/* Month day shift */
#define MONTH_SHIFT		0x08		/* Month shift */
#define YEAR_SHIFT		0x10		/* Year shift */

#define SECOND_MASK		0x7F
#define MIN_MASK		0x7F
#define HOUR_MASK		0x3F
#define DAY_MASK		0x3F
#define MONTH_MASK		0x7F
#define YEAR_MASK		0xFFFF

/* date reg equal to time reg, for debug only */
#define TIME_BYP		(1<<9)
#define INT_ENABLE		(1<<31)		/* interrupt enable */

/* STATUS_REG */
#define CLK_UNCONNECTED		(1<<0)
#define PEND_WR_TIME		(1<<2)
#define PEND_WR_DATE		(1<<3)
#define LOST_WR_TIME		(1<<4)
#define LOST_WR_DATE		(1<<5)
#define RTC_INT_MASK		(1<<31)
#define STATUS_BUSY		(PEND_WR_TIME | PEND_WR_DATE)
#define STATUS_FAIL		(LOST_WR_TIME | LOST_WR_DATE)

struct spear_rtc_config {
	struct rtc_device *rtc;
	struct clk *clk;
	spinlock_t lock;
	void __iomem *ioaddr;
	unsigned int irq_wake;
};

static inline void spear_rtc_clear_interrupt(struct spear_rtc_config *config)
{
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&config->lock, flags);
	val = readl(config->ioaddr + STATUS_REG);
	val |= RTC_INT_MASK;
	writel(val, config->ioaddr + STATUS_REG);
	spin_unlock_irqrestore(&config->lock, flags);
}

static inline void spear_rtc_enable_interrupt(struct spear_rtc_config *config)
{
	unsigned int val;

	val = readl(config->ioaddr + CTRL_REG);
	if (!(val & INT_ENABLE)) {
		spear_rtc_clear_interrupt(config);
		val |= INT_ENABLE;
		writel(val, config->ioaddr + CTRL_REG);
	}
}

static inline void spear_rtc_disable_interrupt(struct spear_rtc_config *config)
{
	unsigned int val;

	val = readl(config->ioaddr + CTRL_REG);
	if (val & INT_ENABLE) {
		val &= ~INT_ENABLE;
		writel(val, config->ioaddr + CTRL_REG);
	}
}

static inline int is_write_complete(struct spear_rtc_config *config)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&config->lock, flags);
	if ((readl(config->ioaddr + STATUS_REG)) & STATUS_FAIL)
		ret = -EIO;
	spin_unlock_irqrestore(&config->lock, flags);

	return ret;
}

static void rtc_wait_not_busy(struct spear_rtc_config *config)
{
	int status, count = 0;
	unsigned long flags;

	/* Assuming BUSY may stay active for 80 msec) */
	for (count = 0; count < 80; count++) {
		spin_lock_irqsave(&config->lock, flags);
		status = readl(config->ioaddr + STATUS_REG);
		spin_unlock_irqrestore(&config->lock, flags);
		if ((status & STATUS_BUSY) == 0)
			break;
		/* check status busy, after each msec */
		msleep(1);
	}
}

static irqreturn_t spear_rtc_irq(int irq, void *dev_id)
{
	struct spear_rtc_config *config = dev_id;
	unsigned long flags, events = 0;
	unsigned int irq_data;

	spin_lock_irqsave(&config->lock, flags);
	irq_data = readl(config->ioaddr + STATUS_REG);
	spin_unlock_irqrestore(&config->lock, flags);

	if ((irq_data & RTC_INT_MASK)) {
		spear_rtc_clear_interrupt(config);
		events = RTC_IRQF | RTC_AF;
		rtc_update_irq(config->rtc, 1, events);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;

}

static int tm2bcd(struct rtc_time *tm)
{
	if (rtc_valid_tm(tm) != 0)
		return -EINVAL;
	tm->tm_sec = bin2bcd(tm->tm_sec);
	tm->tm_min = bin2bcd(tm->tm_min);
	tm->tm_hour = bin2bcd(tm->tm_hour);
	tm->tm_mday = bin2bcd(tm->tm_mday);
	tm->tm_mon = bin2bcd(tm->tm_mon + 1);
	tm->tm_year = bin2bcd(tm->tm_year);

	return 0;
}

static void bcd2tm(struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);
	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon) - 1;
	/* epoch == 1900 */
	tm->tm_year = bcd2bin(tm->tm_year);
}

/*
 * spear_rtc_read_time - set the time
 * @dev: rtc device in use
 * @tm: holds date and time
 *
 * This function read time and date. On success it will return 0
 * otherwise -ve error is returned.
 */
static int spear_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct spear_rtc_config *config = dev_get_drvdata(dev);
	unsigned int time, date;

	/* we don't report wday/yday/isdst ... */
	rtc_wait_not_busy(config);

	time = readl(config->ioaddr + TIME_REG);
	date = readl(config->ioaddr + DATE_REG);
	tm->tm_sec = (time >> SECOND_SHIFT) & SECOND_MASK;
	tm->tm_min = (time >> MINUTE_SHIFT) & MIN_MASK;
	tm->tm_hour = (time >> HOUR_SHIFT) & HOUR_MASK;
	tm->tm_mday = (date >> MDAY_SHIFT) & DAY_MASK;
	tm->tm_mon = (date >> MONTH_SHIFT) & MONTH_MASK;
	tm->tm_year = (date >> YEAR_SHIFT) & YEAR_MASK;

	bcd2tm(tm);
	return 0;
}

/*
 * spear_rtc_set_time - set the time
 * @dev: rtc device in use
 * @tm: holds date and time
 *
 * This function set time and date. On success it will return 0
 * otherwise -ve error is returned.
 */
static int spear_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct spear_rtc_config *config = dev_get_drvdata(dev);
	unsigned int time, date;

	if (tm2bcd(tm) < 0)
		return -EINVAL;

	rtc_wait_not_busy(config);
	time = (tm->tm_sec << SECOND_SHIFT) | (tm->tm_min << MINUTE_SHIFT) |
		(tm->tm_hour << HOUR_SHIFT);
	date = (tm->tm_mday << MDAY_SHIFT) | (tm->tm_mon << MONTH_SHIFT) |
		(tm->tm_year << YEAR_SHIFT);
	writel(time, config->ioaddr + TIME_REG);
	writel(date, config->ioaddr + DATE_REG);

	return is_write_complete(config);
}

/*
 * spear_rtc_read_alarm - read the alarm time
 * @dev: rtc device in use
 * @alm: holds alarm date and time
 *
 * This function read alarm time and date. On success it will return 0
 * otherwise -ve error is returned.
 */
static int spear_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct spear_rtc_config *config = dev_get_drvdata(dev);
	unsigned int time, date;

	rtc_wait_not_busy(config);

	time = readl(config->ioaddr + ALARM_TIME_REG);
	date = readl(config->ioaddr + ALARM_DATE_REG);
	alm->time.tm_sec = (time >> SECOND_SHIFT) & SECOND_MASK;
	alm->time.tm_min = (time >> MINUTE_SHIFT) & MIN_MASK;
	alm->time.tm_hour = (time >> HOUR_SHIFT) & HOUR_MASK;
	alm->time.tm_mday = (date >> MDAY_SHIFT) & DAY_MASK;
	alm->time.tm_mon = (date >> MONTH_SHIFT) & MONTH_MASK;
	alm->time.tm_year = (date >> YEAR_SHIFT) & YEAR_MASK;

	bcd2tm(&alm->time);
	alm->enabled = readl(config->ioaddr + CTRL_REG) & INT_ENABLE;

	return 0;
}

/*
 * spear_rtc_set_alarm - set the alarm time
 * @dev: rtc device in use
 * @alm: holds alarm date and time
 *
 * This function set alarm time and date. On success it will return 0
 * otherwise -ve error is returned.
 */
static int spear_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct spear_rtc_config *config = dev_get_drvdata(dev);
	unsigned int time, date;
	int err;

	if (tm2bcd(&alm->time) < 0)
		return -EINVAL;

	rtc_wait_not_busy(config);

	time = (alm->time.tm_sec << SECOND_SHIFT) | (alm->time.tm_min <<
			MINUTE_SHIFT) |	(alm->time.tm_hour << HOUR_SHIFT);
	date = (alm->time.tm_mday << MDAY_SHIFT) | (alm->time.tm_mon <<
			MONTH_SHIFT) | (alm->time.tm_year << YEAR_SHIFT);

	writel(time, config->ioaddr + ALARM_TIME_REG);
	writel(date, config->ioaddr + ALARM_DATE_REG);
	err = is_write_complete(config);
	if (err < 0)
		return err;

	if (alm->enabled)
		spear_rtc_enable_interrupt(config);
	else
		spear_rtc_disable_interrupt(config);

	return 0;
}

static int spear_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct spear_rtc_config *config = dev_get_drvdata(dev);
	int ret = 0;

	spear_rtc_clear_interrupt(config);

	switch (enabled) {
	case 0:
		/* alarm off */
		spear_rtc_disable_interrupt(config);
		break;
	case 1:
		/* alarm on */
		spear_rtc_enable_interrupt(config);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct rtc_class_ops spear_rtc_ops = {
	.read_time = spear_rtc_read_time,
	.set_time = spear_rtc_set_time,
	.read_alarm = spear_rtc_read_alarm,
	.set_alarm = spear_rtc_set_alarm,
	.alarm_irq_enable = spear_alarm_irq_enable,
};

static int __devinit spear_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct spear_rtc_config *config;
	int status = 0;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no resource defined\n");
		return -EBUSY;
	}
	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "rtc region already claimed\n");
		return -EBUSY;
	}

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config) {
		dev_err(&pdev->dev, "out of memory\n");
		status = -ENOMEM;
		goto err_release_region;
	}

	config->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(config->clk)) {
		status = PTR_ERR(config->clk);
		goto err_kfree;
	}

	status = clk_enable(config->clk);
	if (status < 0)
		goto err_clk_put;

	config->ioaddr = ioremap(res->start, resource_size(res));
	if (!config->ioaddr) {
		dev_err(&pdev->dev, "ioremap fail\n");
		status = -ENOMEM;
		goto err_disable_clock;
	}

	spin_lock_init(&config->lock);
	platform_set_drvdata(pdev, config);

	config->rtc = rtc_device_register(pdev->name, &pdev->dev,
			&spear_rtc_ops, THIS_MODULE);
	if (IS_ERR(config->rtc)) {
		dev_err(&pdev->dev, "can't register RTC device, err %ld\n",
				PTR_ERR(config->rtc));
		status = PTR_ERR(config->rtc);
		goto err_iounmap;
	}

	/* alarm irqs */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no update irq?\n");
		status = irq;
		goto err_clear_platdata;
	}

	status = request_irq(irq, spear_rtc_irq, 0, pdev->name, config);
	if (status) {
		dev_err(&pdev->dev, "Alarm interrupt IRQ%d already \
				claimed\n", irq);
		goto err_clear_platdata;
	}

	if (!device_can_wakeup(&pdev->dev))
		device_init_wakeup(&pdev->dev, 1);

	return 0;

err_clear_platdata:
	platform_set_drvdata(pdev, NULL);
	rtc_device_unregister(config->rtc);
err_iounmap:
	iounmap(config->ioaddr);
err_disable_clock:
	clk_disable(config->clk);
err_clk_put:
	clk_put(config->clk);
err_kfree:
	kfree(config);
err_release_region:
	release_mem_region(res->start, resource_size(res));

	return status;
}

static int __devexit spear_rtc_remove(struct platform_device *pdev)
{
	struct spear_rtc_config *config = platform_get_drvdata(pdev);
	int irq;
	struct resource *res;

	/* leave rtc running, but disable irqs */
	spear_rtc_disable_interrupt(config);
	device_init_wakeup(&pdev->dev, 0);
	irq = platform_get_irq(pdev, 0);
	if (irq)
		free_irq(irq, pdev);
	clk_disable(config->clk);
	clk_put(config->clk);
	iounmap(config->ioaddr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));
	platform_set_drvdata(pdev, NULL);
	rtc_device_unregister(config->rtc);
	kfree(config);

	return 0;
}

#ifdef CONFIG_PM

static int spear_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct spear_rtc_config *config = platform_get_drvdata(pdev);
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (device_may_wakeup(&pdev->dev)) {
		if (!enable_irq_wake(irq))
			config->irq_wake = 1;
	} else {
		spear_rtc_disable_interrupt(config);
		clk_disable(config->clk);
	}

	return 0;
}

static int spear_rtc_resume(struct platform_device *pdev)
{
	struct spear_rtc_config *config = platform_get_drvdata(pdev);
	int irq;

	irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(&pdev->dev)) {
		if (config->irq_wake) {
			disable_irq_wake(irq);
			config->irq_wake = 0;
		}
	} else {
		clk_enable(config->clk);
		spear_rtc_enable_interrupt(config);
	}

	return 0;
}

#else
#define spear_rtc_suspend	NULL
#define spear_rtc_resume	NULL
#endif

static void spear_rtc_shutdown(struct platform_device *pdev)
{
	struct spear_rtc_config *config = platform_get_drvdata(pdev);

	spear_rtc_disable_interrupt(config);
	clk_disable(config->clk);
}

#ifdef CONFIG_OF
static const struct of_device_id spear_rtc_id_table[] = {
	{ .compatible = "st,spear600-rtc" },
	{}
};
MODULE_DEVICE_TABLE(of, spear_rtc_id_table);
#endif

static struct platform_driver spear_rtc_driver = {
	.probe = spear_rtc_probe,
	.remove = __devexit_p(spear_rtc_remove),
	.suspend = spear_rtc_suspend,
	.resume = spear_rtc_resume,
	.shutdown = spear_rtc_shutdown,
	.driver = {
		.name = "rtc-spear",
		.of_match_table = of_match_ptr(spear_rtc_id_table),
	},
};

module_platform_driver(spear_rtc_driver);

MODULE_ALIAS("platform:rtc-spear");
MODULE_AUTHOR("Rajeev Kumar <rajeev-dlh.kumar@st.com>");
MODULE_DESCRIPTION("ST SPEAr Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
