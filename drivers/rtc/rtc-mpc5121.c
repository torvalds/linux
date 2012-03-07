/*
 * Real-time clock driver for MPC5121
 *
 * Copyright 2007, Domen Puncer <domen.puncer@telargo.com>
 * Copyright 2008, Freescale Semiconductor, Inc. All rights reserved.
 * Copyright 2011, Dmitry Eremin-Solenikov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/slab.h>

struct mpc5121_rtc_regs {
	u8 set_time;		/* RTC + 0x00 */
	u8 hour_set;		/* RTC + 0x01 */
	u8 minute_set;		/* RTC + 0x02 */
	u8 second_set;		/* RTC + 0x03 */

	u8 set_date;		/* RTC + 0x04 */
	u8 month_set;		/* RTC + 0x05 */
	u8 weekday_set;		/* RTC + 0x06 */
	u8 date_set;		/* RTC + 0x07 */

	u8 write_sw;		/* RTC + 0x08 */
	u8 sw_set;		/* RTC + 0x09 */
	u16 year_set;		/* RTC + 0x0a */

	u8 alm_enable;		/* RTC + 0x0c */
	u8 alm_hour_set;	/* RTC + 0x0d */
	u8 alm_min_set;		/* RTC + 0x0e */
	u8 int_enable;		/* RTC + 0x0f */

	u8 reserved1;
	u8 hour;		/* RTC + 0x11 */
	u8 minute;		/* RTC + 0x12 */
	u8 second;		/* RTC + 0x13 */

	u8 month;		/* RTC + 0x14 */
	u8 wday_mday;		/* RTC + 0x15 */
	u16 year;		/* RTC + 0x16 */

	u8 int_alm;		/* RTC + 0x18 */
	u8 int_sw;		/* RTC + 0x19 */
	u8 alm_status;		/* RTC + 0x1a */
	u8 sw_minute;		/* RTC + 0x1b */

	u8 bus_error_1;		/* RTC + 0x1c */
	u8 int_day;		/* RTC + 0x1d */
	u8 int_min;		/* RTC + 0x1e */
	u8 int_sec;		/* RTC + 0x1f */

	/*
	 * target_time:
	 *	intended to be used for hibernation but hibernation
	 *	does not work on silicon rev 1.5 so use it for non-volatile
	 *	storage of offset between the actual_time register and linux
	 *	time
	 */
	u32 target_time;	/* RTC + 0x20 */
	/*
	 * actual_time:
	 * 	readonly time since VBAT_RTC was last connected
	 */
	u32 actual_time;	/* RTC + 0x24 */
	u32 keep_alive;		/* RTC + 0x28 */
};

struct mpc5121_rtc_data {
	unsigned irq;
	unsigned irq_periodic;
	struct mpc5121_rtc_regs __iomem *regs;
	struct rtc_device *rtc;
	struct rtc_wkalrm wkalarm;
};

/*
 * Update second/minute/hour registers.
 *
 * This is just so alarm will work.
 */
static void mpc5121_rtc_update_smh(struct mpc5121_rtc_regs __iomem *regs,
				   struct rtc_time *tm)
{
	out_8(&regs->second_set, tm->tm_sec);
	out_8(&regs->minute_set, tm->tm_min);
	out_8(&regs->hour_set, tm->tm_hour);

	/* set time sequence */
	out_8(&regs->set_time, 0x1);
	out_8(&regs->set_time, 0x3);
	out_8(&regs->set_time, 0x1);
	out_8(&regs->set_time, 0x0);
}

static int mpc5121_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;
	unsigned long now;

	/*
	 * linux time is actual_time plus the offset saved in target_time
	 */
	now = in_be32(&regs->actual_time) + in_be32(&regs->target_time);

	rtc_time_to_tm(now, tm);

	/*
	 * update second minute hour registers
	 * so alarms will work
	 */
	mpc5121_rtc_update_smh(regs, tm);

	return rtc_valid_tm(tm);
}

static int mpc5121_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;
	int ret;
	unsigned long now;

	/*
	 * The actual_time register is read only so we write the offset
	 * between it and linux time to the target_time register.
	 */
	ret = rtc_tm_to_time(tm, &now);
	if (ret == 0)
		out_be32(&regs->target_time, now - in_be32(&regs->actual_time));

	/*
	 * update second minute hour registers
	 * so alarms will work
	 */
	mpc5121_rtc_update_smh(regs, tm);

	return 0;
}

static int mpc5200_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;
	int tmp;

	tm->tm_sec = in_8(&regs->second);
	tm->tm_min = in_8(&regs->minute);

	/* 12 hour format? */
	if (in_8(&regs->hour) & 0x20)
		tm->tm_hour = (in_8(&regs->hour) >> 1) +
			(in_8(&regs->hour) & 1 ? 12 : 0);
	else
		tm->tm_hour = in_8(&regs->hour);

	tmp = in_8(&regs->wday_mday);
	tm->tm_mday = tmp & 0x1f;
	tm->tm_mon = in_8(&regs->month) - 1;
	tm->tm_year = in_be16(&regs->year) - 1900;
	tm->tm_wday = (tmp >> 5) % 7;
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_isdst = 0;

	return 0;
}

static int mpc5200_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	mpc5121_rtc_update_smh(regs, tm);

	/* date */
	out_8(&regs->month_set, tm->tm_mon + 1);
	out_8(&regs->weekday_set, tm->tm_wday ? tm->tm_wday : 7);
	out_8(&regs->date_set, tm->tm_mday);
	out_be16(&regs->year_set, tm->tm_year + 1900);

	/* set date sequence */
	out_8(&regs->set_date, 0x1);
	out_8(&regs->set_date, 0x3);
	out_8(&regs->set_date, 0x1);
	out_8(&regs->set_date, 0x0);

	return 0;
}

static int mpc5121_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	*alarm = rtc->wkalarm;

	alarm->pending = in_8(&regs->alm_status);

	return 0;
}

static int mpc5121_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	/*
	 * the alarm has no seconds so deal with it
	 */
	if (alarm->time.tm_sec) {
		alarm->time.tm_sec = 0;
		alarm->time.tm_min++;
		if (alarm->time.tm_min >= 60) {
			alarm->time.tm_min = 0;
			alarm->time.tm_hour++;
			if (alarm->time.tm_hour >= 24)
				alarm->time.tm_hour = 0;
		}
	}

	alarm->time.tm_mday = -1;
	alarm->time.tm_mon = -1;
	alarm->time.tm_year = -1;

	out_8(&regs->alm_min_set, alarm->time.tm_min);
	out_8(&regs->alm_hour_set, alarm->time.tm_hour);

	out_8(&regs->alm_enable, alarm->enabled);

	rtc->wkalarm = *alarm;
	return 0;
}

static irqreturn_t mpc5121_rtc_handler(int irq, void *dev)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata((struct device *)dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	if (in_8(&regs->int_alm)) {
		/* acknowledge and clear status */
		out_8(&regs->int_alm, 1);
		out_8(&regs->alm_status, 1);

		rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t mpc5121_rtc_handler_upd(int irq, void *dev)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata((struct device *)dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	if (in_8(&regs->int_sec) && (in_8(&regs->int_enable) & 0x1)) {
		/* acknowledge */
		out_8(&regs->int_sec, 1);

		rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_UF);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mpc5121_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;
	int val;

	if (enabled)
		val = 1;
	else
		val = 0;

	out_8(&regs->alm_enable, val);
	rtc->wkalarm.enabled = val;

	return 0;
}

static const struct rtc_class_ops mpc5121_rtc_ops = {
	.read_time = mpc5121_rtc_read_time,
	.set_time = mpc5121_rtc_set_time,
	.read_alarm = mpc5121_rtc_read_alarm,
	.set_alarm = mpc5121_rtc_set_alarm,
	.alarm_irq_enable = mpc5121_rtc_alarm_irq_enable,
};

static const struct rtc_class_ops mpc5200_rtc_ops = {
	.read_time = mpc5200_rtc_read_time,
	.set_time = mpc5200_rtc_set_time,
	.read_alarm = mpc5121_rtc_read_alarm,
	.set_alarm = mpc5121_rtc_set_alarm,
	.alarm_irq_enable = mpc5121_rtc_alarm_irq_enable,
};

static int __devinit mpc5121_rtc_probe(struct platform_device *op)
{
	struct mpc5121_rtc_data *rtc;
	int err = 0;

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->regs = of_iomap(op->dev.of_node, 0);
	if (!rtc->regs) {
		dev_err(&op->dev, "%s: couldn't map io space\n", __func__);
		err = -ENOSYS;
		goto out_free;
	}

	device_init_wakeup(&op->dev, 1);

	dev_set_drvdata(&op->dev, rtc);

	rtc->irq = irq_of_parse_and_map(op->dev.of_node, 1);
	err = request_irq(rtc->irq, mpc5121_rtc_handler, IRQF_DISABLED,
						"mpc5121-rtc", &op->dev);
	if (err) {
		dev_err(&op->dev, "%s: could not request irq: %i\n",
							__func__, rtc->irq);
		goto out_dispose;
	}

	rtc->irq_periodic = irq_of_parse_and_map(op->dev.of_node, 0);
	err = request_irq(rtc->irq_periodic, mpc5121_rtc_handler_upd,
				IRQF_DISABLED, "mpc5121-rtc_upd", &op->dev);
	if (err) {
		dev_err(&op->dev, "%s: could not request irq: %i\n",
						__func__, rtc->irq_periodic);
		goto out_dispose2;
	}

	if (of_device_is_compatible(op->dev.of_node, "fsl,mpc5121-rtc")) {
		u32 ka;
		ka = in_be32(&rtc->regs->keep_alive);
		if (ka & 0x02) {
			dev_warn(&op->dev,
				"mpc5121-rtc: Battery or oscillator failure!\n");
			out_be32(&rtc->regs->keep_alive, ka);
		}

		rtc->rtc = rtc_device_register("mpc5121-rtc", &op->dev,
						&mpc5121_rtc_ops, THIS_MODULE);
	} else {
		rtc->rtc = rtc_device_register("mpc5200-rtc", &op->dev,
						&mpc5200_rtc_ops, THIS_MODULE);
	}

	rtc->rtc->uie_unsupported = 1;

	if (IS_ERR(rtc->rtc)) {
		err = PTR_ERR(rtc->rtc);
		goto out_free_irq;
	}

	return 0;

out_free_irq:
	free_irq(rtc->irq_periodic, &op->dev);
out_dispose2:
	irq_dispose_mapping(rtc->irq_periodic);
	free_irq(rtc->irq, &op->dev);
out_dispose:
	irq_dispose_mapping(rtc->irq);
	iounmap(rtc->regs);
out_free:
	kfree(rtc);

	return err;
}

static int __devexit mpc5121_rtc_remove(struct platform_device *op)
{
	struct mpc5121_rtc_data *rtc = dev_get_drvdata(&op->dev);
	struct mpc5121_rtc_regs __iomem *regs = rtc->regs;

	/* disable interrupt, so there are no nasty surprises */
	out_8(&regs->alm_enable, 0);
	out_8(&regs->int_enable, in_8(&regs->int_enable) & ~0x1);

	rtc_device_unregister(rtc->rtc);
	iounmap(rtc->regs);
	free_irq(rtc->irq, &op->dev);
	free_irq(rtc->irq_periodic, &op->dev);
	irq_dispose_mapping(rtc->irq);
	irq_dispose_mapping(rtc->irq_periodic);
	dev_set_drvdata(&op->dev, NULL);
	kfree(rtc);

	return 0;
}

static struct of_device_id mpc5121_rtc_match[] __devinitdata = {
	{ .compatible = "fsl,mpc5121-rtc", },
	{ .compatible = "fsl,mpc5200-rtc", },
	{},
};

static struct platform_driver mpc5121_rtc_driver = {
	.driver = {
		.name = "mpc5121-rtc",
		.owner = THIS_MODULE,
		.of_match_table = mpc5121_rtc_match,
	},
	.probe = mpc5121_rtc_probe,
	.remove = __devexit_p(mpc5121_rtc_remove),
};

module_platform_driver(mpc5121_rtc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Rigby <jcrigby@gmail.com>");
