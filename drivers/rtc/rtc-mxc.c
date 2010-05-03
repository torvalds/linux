/*
 * Copyright 2004-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/io.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <mach/hardware.h>

#define RTC_INPUT_CLK_32768HZ	(0x00 << 5)
#define RTC_INPUT_CLK_32000HZ	(0x01 << 5)
#define RTC_INPUT_CLK_38400HZ	(0x02 << 5)

#define RTC_SW_BIT      (1 << 0)
#define RTC_ALM_BIT     (1 << 2)
#define RTC_1HZ_BIT     (1 << 4)
#define RTC_2HZ_BIT     (1 << 7)
#define RTC_SAM0_BIT    (1 << 8)
#define RTC_SAM1_BIT    (1 << 9)
#define RTC_SAM2_BIT    (1 << 10)
#define RTC_SAM3_BIT    (1 << 11)
#define RTC_SAM4_BIT    (1 << 12)
#define RTC_SAM5_BIT    (1 << 13)
#define RTC_SAM6_BIT    (1 << 14)
#define RTC_SAM7_BIT    (1 << 15)
#define PIT_ALL_ON      (RTC_2HZ_BIT | RTC_SAM0_BIT | RTC_SAM1_BIT | \
			 RTC_SAM2_BIT | RTC_SAM3_BIT | RTC_SAM4_BIT | \
			 RTC_SAM5_BIT | RTC_SAM6_BIT | RTC_SAM7_BIT)

#define RTC_ENABLE_BIT  (1 << 7)

#define MAX_PIE_NUM     9
#define MAX_PIE_FREQ    512
static const u32 PIE_BIT_DEF[MAX_PIE_NUM][2] = {
	{ 2,		RTC_2HZ_BIT },
	{ 4,		RTC_SAM0_BIT },
	{ 8,		RTC_SAM1_BIT },
	{ 16,		RTC_SAM2_BIT },
	{ 32,		RTC_SAM3_BIT },
	{ 64,		RTC_SAM4_BIT },
	{ 128,		RTC_SAM5_BIT },
	{ 256,		RTC_SAM6_BIT },
	{ MAX_PIE_FREQ,	RTC_SAM7_BIT },
};

/* Those are the bits from a classic RTC we want to mimic */
#define RTC_IRQF	0x80	/* any of the following 3 is active */
#define RTC_PF		0x40	/* Periodic interrupt */
#define RTC_AF		0x20	/* Alarm interrupt */
#define RTC_UF		0x10	/* Update interrupt for 1Hz RTC */

#define MXC_RTC_TIME	0
#define MXC_RTC_ALARM	1

#define RTC_HOURMIN	0x00	/*  32bit rtc hour/min counter reg */
#define RTC_SECOND	0x04	/*  32bit rtc seconds counter reg */
#define RTC_ALRM_HM	0x08	/*  32bit rtc alarm hour/min reg */
#define RTC_ALRM_SEC	0x0C	/*  32bit rtc alarm seconds reg */
#define RTC_RTCCTL	0x10	/*  32bit rtc control reg */
#define RTC_RTCISR	0x14	/*  32bit rtc interrupt status reg */
#define RTC_RTCIENR	0x18	/*  32bit rtc interrupt enable reg */
#define RTC_STPWCH	0x1C	/*  32bit rtc stopwatch min reg */
#define RTC_DAYR	0x20	/*  32bit rtc days counter reg */
#define RTC_DAYALARM	0x24	/*  32bit rtc day alarm reg */
#define RTC_TEST1	0x28	/*  32bit rtc test reg 1 */
#define RTC_TEST2	0x2C	/*  32bit rtc test reg 2 */
#define RTC_TEST3	0x30	/*  32bit rtc test reg 3 */

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	int irq;
	struct clk *clk;
	unsigned int irqen;
	int alrm_sec;
	int alrm_min;
	int alrm_hour;
	int alrm_mday;
	struct timespec mxc_rtc_delta;
	struct rtc_time g_rtc_alarm;
};

/*
 * This function is used to obtain the RTC time or the alarm value in
 * second.
 */
static u32 get_alarm_or_time(struct device *dev, int time_alarm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 day = 0, hr = 0, min = 0, sec = 0, hr_min = 0;

	switch (time_alarm) {
	case MXC_RTC_TIME:
		day = readw(ioaddr + RTC_DAYR);
		hr_min = readw(ioaddr + RTC_HOURMIN);
		sec = readw(ioaddr + RTC_SECOND);
		break;
	case MXC_RTC_ALARM:
		day = readw(ioaddr + RTC_DAYALARM);
		hr_min = readw(ioaddr + RTC_ALRM_HM) & 0xffff;
		sec = readw(ioaddr + RTC_ALRM_SEC);
		break;
	}

	hr = hr_min >> 8;
	min = hr_min & 0xff;

	return (((day * 24 + hr) * 60) + min) * 60 + sec;
}

/*
 * This function sets the RTC alarm value or the time value.
 */
static void set_alarm_or_time(struct device *dev, int time_alarm, u32 time)
{
	u32 day, hr, min, sec, temp;
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	day = time / 86400;
	time -= day * 86400;

	/* time is within a day now */
	hr = time / 3600;
	time -= hr * 3600;

	/* time is within an hour now */
	min = time / 60;
	sec = time - min * 60;

	temp = (hr << 8) + min;

	switch (time_alarm) {
	case MXC_RTC_TIME:
		writew(day, ioaddr + RTC_DAYR);
		writew(sec, ioaddr + RTC_SECOND);
		writew(temp, ioaddr + RTC_HOURMIN);
		break;
	case MXC_RTC_ALARM:
		writew(day, ioaddr + RTC_DAYALARM);
		writew(sec, ioaddr + RTC_ALRM_SEC);
		writew(temp, ioaddr + RTC_ALRM_HM);
		break;
	}
}

/*
 * This function updates the RTC alarm registers and then clears all the
 * interrupt status bits.
 */
static int rtc_update_alarm(struct device *dev, struct rtc_time *alrm)
{
	struct rtc_time alarm_tm, now_tm;
	unsigned long now, time;
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	now = get_alarm_or_time(dev, MXC_RTC_TIME);
	rtc_time_to_tm(now, &now_tm);
	alarm_tm.tm_year = now_tm.tm_year;
	alarm_tm.tm_mon = now_tm.tm_mon;
	alarm_tm.tm_mday = now_tm.tm_mday;
	alarm_tm.tm_hour = alrm->tm_hour;
	alarm_tm.tm_min = alrm->tm_min;
	alarm_tm.tm_sec = alrm->tm_sec;
	rtc_tm_to_time(&now_tm, &now);
	rtc_tm_to_time(&alarm_tm, &time);

	if (time < now) {
		time += 60 * 60 * 24;
		rtc_time_to_tm(time, &alarm_tm);
	}

	ret = rtc_tm_to_time(&alarm_tm, &time);

	/* clear all the interrupt status bits */
	writew(readw(ioaddr + RTC_RTCISR), ioaddr + RTC_RTCISR);
	set_alarm_or_time(dev, MXC_RTC_ALARM, time);

	return ret;
}

/* This function is the RTC interrupt service routine. */
static irqreturn_t mxc_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 status;
	u32 events = 0;

	spin_lock_irq(&pdata->rtc->irq_lock);
	status = readw(ioaddr + RTC_RTCISR) & readw(ioaddr + RTC_RTCIENR);
	/* clear interrupt sources */
	writew(status, ioaddr + RTC_RTCISR);

	/* clear alarm interrupt if it has occurred */
	if (status & RTC_ALM_BIT)
		status &= ~RTC_ALM_BIT;

	/* update irq data & counter */
	if (status & RTC_ALM_BIT)
		events |= (RTC_AF | RTC_IRQF);

	if (status & RTC_1HZ_BIT)
		events |= (RTC_UF | RTC_IRQF);

	if (status & PIT_ALL_ON)
		events |= (RTC_PF | RTC_IRQF);

	if ((status & RTC_ALM_BIT) && rtc_valid_tm(&pdata->g_rtc_alarm))
		rtc_update_alarm(&pdev->dev, &pdata->g_rtc_alarm);

	rtc_update_irq(pdata->rtc, 1, events);
	spin_unlock_irq(&pdata->rtc->irq_lock);

	return IRQ_HANDLED;
}

/*
 * Clear all interrupts and release the IRQ
 */
static void mxc_rtc_release(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	spin_lock_irq(&pdata->rtc->irq_lock);

	/* Disable all rtc interrupts */
	writew(0, ioaddr + RTC_RTCIENR);

	/* Clear all interrupt status */
	writew(0xffffffff, ioaddr + RTC_RTCISR);

	spin_unlock_irq(&pdata->rtc->irq_lock);
}

static void mxc_rtc_irq_enable(struct device *dev, unsigned int bit,
				unsigned int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 reg;

	spin_lock_irq(&pdata->rtc->irq_lock);
	reg = readw(ioaddr + RTC_RTCIENR);

	if (enabled)
		reg |= bit;
	else
		reg &= ~bit;

	writew(reg, ioaddr + RTC_RTCIENR);
	spin_unlock_irq(&pdata->rtc->irq_lock);
}

static int mxc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, enabled);
	return 0;
}

static int mxc_rtc_update_irq_enable(struct device *dev, unsigned int enabled)
{
	mxc_rtc_irq_enable(dev, RTC_1HZ_BIT, enabled);
	return 0;
}

/*
 * This function reads the current RTC time into tm in Gregorian date.
 */
static int mxc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	u32 val;

	/* Avoid roll-over from reading the different registers */
	do {
		val = get_alarm_or_time(dev, MXC_RTC_TIME);
	} while (val != get_alarm_or_time(dev, MXC_RTC_TIME));

	rtc_time_to_tm(val, tm);

	return 0;
}

/*
 * This function sets the internal RTC time based on tm in Gregorian date.
 */
static int mxc_rtc_set_mmss(struct device *dev, unsigned long time)
{
	/* Avoid roll-over from reading the different registers */
	do {
		set_alarm_or_time(dev, MXC_RTC_TIME, time);
	} while (time != get_alarm_or_time(dev, MXC_RTC_TIME));

	return 0;
}

/*
 * This function reads the current alarm value into the passed in 'alrm'
 * argument. It updates the alrm's pending field value based on the whether
 * an alarm interrupt occurs or not.
 */
static int mxc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	rtc_time_to_tm(get_alarm_or_time(dev, MXC_RTC_ALARM), &alrm->time);
	alrm->pending = ((readw(ioaddr + RTC_RTCISR) & RTC_ALM_BIT)) ? 1 : 0;

	return 0;
}

/*
 * This function sets the RTC alarm based on passed in alrm.
 */
static int mxc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	int ret;

	if (rtc_valid_tm(&alrm->time)) {
		if (alrm->time.tm_sec > 59 ||
		    alrm->time.tm_hour > 23 ||
		    alrm->time.tm_min > 59)
			return -EINVAL;

		ret = rtc_update_alarm(dev, &alrm->time);
	} else {
		ret = rtc_valid_tm(&alrm->time);
		if (ret)
			return ret;

		ret = rtc_update_alarm(dev, &alrm->time);
	}

	if (ret)
		return ret;

	memcpy(&pdata->g_rtc_alarm, &alrm->time, sizeof(struct rtc_time));
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, alrm->enabled);

	return 0;
}

/* RTC layer */
static struct rtc_class_ops mxc_rtc_ops = {
	.release		= mxc_rtc_release,
	.read_time		= mxc_rtc_read_time,
	.set_mmss		= mxc_rtc_set_mmss,
	.read_alarm		= mxc_rtc_read_alarm,
	.set_alarm		= mxc_rtc_set_alarm,
	.alarm_irq_enable	= mxc_rtc_alarm_irq_enable,
	.update_irq_enable	= mxc_rtc_update_irq_enable,
};

static int __init mxc_rtc_probe(struct platform_device *pdev)
{
	struct clk *clk;
	struct resource *res;
	struct rtc_device *rtc;
	struct rtc_plat_data *pdata = NULL;
	u32 reg;
	unsigned long rate;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;

	pdata->ioaddr = devm_ioremap(&pdev->dev, res->start,
				     resource_size(res));

	clk = clk_get(&pdev->dev, "ckil");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto exit_free_pdata;
	}

	rate = clk_get_rate(clk);
	clk_put(clk);

	if (rate == 32768)
		reg = RTC_INPUT_CLK_32768HZ;
	else if (rate == 32000)
		reg = RTC_INPUT_CLK_32000HZ;
	else if (rate == 38400)
		reg = RTC_INPUT_CLK_38400HZ;
	else {
		dev_err(&pdev->dev, "rtc clock is not valid (%lu)\n", rate);
		ret = -EINVAL;
		goto exit_free_pdata;
	}

	reg |= RTC_ENABLE_BIT;
	writew(reg, (pdata->ioaddr + RTC_RTCCTL));
	if (((readw(pdata->ioaddr + RTC_RTCCTL)) & RTC_ENABLE_BIT) == 0) {
		dev_err(&pdev->dev, "hardware module can't be enabled!\n");
		ret = -EIO;
		goto exit_free_pdata;
	}

	pdata->clk = clk_get(&pdev->dev, "rtc");
	if (IS_ERR(pdata->clk)) {
		dev_err(&pdev->dev, "unable to get clock!\n");
		ret = PTR_ERR(pdata->clk);
		goto exit_free_pdata;
	}

	clk_enable(pdata->clk);

	rtc = rtc_device_register(pdev->name, &pdev->dev, &mxc_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		goto exit_put_clk;
	}

	pdata->rtc = rtc;
	platform_set_drvdata(pdev, pdata);

	/* Configure and enable the RTC */
	pdata->irq = platform_get_irq(pdev, 0);

	if (pdata->irq >= 0 &&
	    devm_request_irq(&pdev->dev, pdata->irq, mxc_rtc_interrupt,
			     IRQF_SHARED, pdev->name, pdev) < 0) {
		dev_warn(&pdev->dev, "interrupt not available.\n");
		pdata->irq = -1;
	}

	return 0;

exit_put_clk:
	clk_disable(pdata->clk);
	clk_put(pdata->clk);

exit_free_pdata:

	return ret;
}

static int __exit mxc_rtc_remove(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	rtc_device_unregister(pdata->rtc);

	clk_disable(pdata->clk);
	clk_put(pdata->clk);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mxc_rtc_driver = {
	.driver = {
		   .name	= "mxc_rtc",
		   .owner	= THIS_MODULE,
	},
	.remove		= __exit_p(mxc_rtc_remove),
};

static int __init mxc_rtc_init(void)
{
	return platform_driver_probe(&mxc_rtc_driver, mxc_rtc_probe);
}

static void __exit mxc_rtc_exit(void)
{
	platform_driver_unregister(&mxc_rtc_driver);
}

module_init(mxc_rtc_init);
module_exit(mxc_rtc_exit);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("RTC driver for Freescale MXC");
MODULE_LICENSE("GPL");

