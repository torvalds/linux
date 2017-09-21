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
#include <linux/of.h>
#include <linux/of_device.h>

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

enum imx_rtc_type {
	IMX1_RTC,
	IMX21_RTC,
};

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	int irq;
	struct clk *clk_ref;
	struct clk *clk_ipg;
	struct rtc_time g_rtc_alarm;
	enum imx_rtc_type devtype;
};

static const struct platform_device_id imx_rtc_devtype[] = {
	{
		.name = "imx1-rtc",
		.driver_data = IMX1_RTC,
	}, {
		.name = "imx21-rtc",
		.driver_data = IMX21_RTC,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_rtc_devtype);

#ifdef CONFIG_OF
static const struct of_device_id imx_rtc_dt_ids[] = {
	{ .compatible = "fsl,imx1-rtc", .data = (const void *)IMX1_RTC },
	{ .compatible = "fsl,imx21-rtc", .data = (const void *)IMX21_RTC },
	{}
};
MODULE_DEVICE_TABLE(of, imx_rtc_dt_ids);
#endif

static inline int is_imx1_rtc(struct rtc_plat_data *data)
{
	return data->devtype == IMX1_RTC;
}

/*
 * This function is used to obtain the RTC time or the alarm value in
 * second.
 */
static time64_t get_alarm_or_time(struct device *dev, int time_alarm)
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

	return ((((time64_t)day * 24 + hr) * 60) + min) * 60 + sec;
}

/*
 * This function sets the RTC alarm value or the time value.
 */
static void set_alarm_or_time(struct device *dev, int time_alarm, time64_t time)
{
	u32 tod, day, hr, min, sec, temp;
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	day = div_s64_rem(time, 86400, &tod);

	/* time is within a day now */
	hr = tod / 3600;
	tod -= hr * 3600;

	/* time is within an hour now */
	min = tod / 60;
	sec = tod - min * 60;

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
static void rtc_update_alarm(struct device *dev, struct rtc_time *alrm)
{
	time64_t time;
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	time = rtc_tm_to_time64(alrm);

	/* clear all the interrupt status bits */
	writew(readw(ioaddr + RTC_RTCISR), ioaddr + RTC_RTCISR);
	set_alarm_or_time(dev, MXC_RTC_ALARM, time);
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

/* This function is the RTC interrupt service routine. */
static irqreturn_t mxc_rtc_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;
	unsigned long flags;
	u32 status;
	u32 events = 0;

	spin_lock_irqsave(&pdata->rtc->irq_lock, flags);
	status = readw(ioaddr + RTC_RTCISR) & readw(ioaddr + RTC_RTCIENR);
	/* clear interrupt sources */
	writew(status, ioaddr + RTC_RTCISR);

	/* update irq data & counter */
	if (status & RTC_ALM_BIT) {
		events |= (RTC_AF | RTC_IRQF);
		/* RTC alarm should be one-shot */
		mxc_rtc_irq_enable(&pdev->dev, RTC_ALM_BIT, 0);
	}

	if (status & PIT_ALL_ON)
		events |= (RTC_PF | RTC_IRQF);

	rtc_update_irq(pdata->rtc, 1, events);
	spin_unlock_irqrestore(&pdata->rtc->irq_lock, flags);

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

static int mxc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, enabled);
	return 0;
}

/*
 * This function reads the current RTC time into tm in Gregorian date.
 */
static int mxc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	time64_t val;

	/* Avoid roll-over from reading the different registers */
	do {
		val = get_alarm_or_time(dev, MXC_RTC_TIME);
	} while (val != get_alarm_or_time(dev, MXC_RTC_TIME));

	rtc_time64_to_tm(val, tm);

	return 0;
}

/*
 * This function sets the internal RTC time based on tm in Gregorian date.
 */
static int mxc_rtc_set_mmss(struct device *dev, time64_t time)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	/*
	 * TTC_DAYR register is 9-bit in MX1 SoC, save time and day of year only
	 */
	if (is_imx1_rtc(pdata)) {
		struct rtc_time tm;

		rtc_time64_to_tm(time, &tm);
		tm.tm_year = 70;
		time = rtc_tm_to_time64(&tm);
	}

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

	rtc_time64_to_tm(get_alarm_or_time(dev, MXC_RTC_ALARM), &alrm->time);
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

	rtc_update_alarm(dev, &alrm->time);

	memcpy(&pdata->g_rtc_alarm, &alrm->time, sizeof(struct rtc_time));
	mxc_rtc_irq_enable(dev, RTC_ALM_BIT, alrm->enabled);

	return 0;
}

/* RTC layer */
static const struct rtc_class_ops mxc_rtc_ops = {
	.release		= mxc_rtc_release,
	.read_time		= mxc_rtc_read_time,
	.set_mmss64		= mxc_rtc_set_mmss,
	.read_alarm		= mxc_rtc_read_alarm,
	.set_alarm		= mxc_rtc_set_alarm,
	.alarm_irq_enable	= mxc_rtc_alarm_irq_enable,
};

static int mxc_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rtc_device *rtc;
	struct rtc_plat_data *pdata = NULL;
	u32 reg;
	unsigned long rate;
	int ret;
	const struct of_device_id *of_id;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	of_id = of_match_device(imx_rtc_dt_ids, &pdev->dev);
	if (of_id)
		pdata->devtype = (enum imx_rtc_type)of_id->data;
	else
		pdata->devtype = pdev->id_entry->driver_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->ioaddr))
		return PTR_ERR(pdata->ioaddr);

	pdata->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(pdata->clk_ipg)) {
		dev_err(&pdev->dev, "unable to get ipg clock!\n");
		return PTR_ERR(pdata->clk_ipg);
	}

	ret = clk_prepare_enable(pdata->clk_ipg);
	if (ret)
		return ret;

	pdata->clk_ref = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(pdata->clk_ref)) {
		dev_err(&pdev->dev, "unable to get ref clock!\n");
		ret = PTR_ERR(pdata->clk_ref);
		goto exit_put_clk_ipg;
	}

	ret = clk_prepare_enable(pdata->clk_ref);
	if (ret)
		goto exit_put_clk_ipg;

	rate = clk_get_rate(pdata->clk_ref);

	if (rate == 32768)
		reg = RTC_INPUT_CLK_32768HZ;
	else if (rate == 32000)
		reg = RTC_INPUT_CLK_32000HZ;
	else if (rate == 38400)
		reg = RTC_INPUT_CLK_38400HZ;
	else {
		dev_err(&pdev->dev, "rtc clock is not valid (%lu)\n", rate);
		ret = -EINVAL;
		goto exit_put_clk_ref;
	}

	reg |= RTC_ENABLE_BIT;
	writew(reg, (pdata->ioaddr + RTC_RTCCTL));
	if (((readw(pdata->ioaddr + RTC_RTCCTL)) & RTC_ENABLE_BIT) == 0) {
		dev_err(&pdev->dev, "hardware module can't be enabled!\n");
		ret = -EIO;
		goto exit_put_clk_ref;
	}

	platform_set_drvdata(pdev, pdata);

	/* Configure and enable the RTC */
	pdata->irq = platform_get_irq(pdev, 0);

	if (pdata->irq >= 0 &&
	    devm_request_irq(&pdev->dev, pdata->irq, mxc_rtc_interrupt,
			     IRQF_SHARED, pdev->name, pdev) < 0) {
		dev_warn(&pdev->dev, "interrupt not available.\n");
		pdata->irq = -1;
	}

	if (pdata->irq >= 0)
		device_init_wakeup(&pdev->dev, 1);

	rtc = devm_rtc_device_register(&pdev->dev, pdev->name, &mxc_rtc_ops,
				  THIS_MODULE);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		goto exit_put_clk_ref;
	}

	pdata->rtc = rtc;

	return 0;

exit_put_clk_ref:
	clk_disable_unprepare(pdata->clk_ref);
exit_put_clk_ipg:
	clk_disable_unprepare(pdata->clk_ipg);

	return ret;
}

static int mxc_rtc_remove(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	clk_disable_unprepare(pdata->clk_ref);
	clk_disable_unprepare(pdata->clk_ipg);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mxc_rtc_suspend(struct device *dev)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(pdata->irq);

	return 0;
}

static int mxc_rtc_resume(struct device *dev)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(pdata->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mxc_rtc_pm_ops, mxc_rtc_suspend, mxc_rtc_resume);

static struct platform_driver mxc_rtc_driver = {
	.driver = {
		   .name	= "mxc_rtc",
		   .of_match_table = of_match_ptr(imx_rtc_dt_ids),
		   .pm		= &mxc_rtc_pm_ops,
	},
	.id_table = imx_rtc_devtype,
	.probe = mxc_rtc_probe,
	.remove = mxc_rtc_remove,
};

module_platform_driver(mxc_rtc_driver)

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("RTC driver for Freescale MXC");
MODULE_LICENSE("GPL");

