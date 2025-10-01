// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NCT6694 RTC driver based on USB interface.
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/bcd.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mfd/nct6694.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>

/*
 * USB command module type for NCT6694 RTC controller.
 * This defines the module type used for communication with the NCT6694
 * RTC controller over the USB interface.
 */
#define NCT6694_RTC_MOD		0x08

/* Command 00h - RTC Time */
#define NCT6694_RTC_TIME	0x0000
#define NCT6694_RTC_TIME_SEL	0x00

/* Command 01h - RTC Alarm */
#define NCT6694_RTC_ALARM	0x01
#define NCT6694_RTC_ALARM_SEL	0x00

/* Command 02h - RTC Status */
#define NCT6694_RTC_STATUS	0x02
#define NCT6694_RTC_STATUS_SEL	0x00

#define NCT6694_RTC_IRQ_INT_EN	BIT(0)	/* Transmit a USB INT-in when RTC alarm */
#define NCT6694_RTC_IRQ_GPO_EN	BIT(5)	/* Trigger a GPO Low Pulse when RTC alarm */

#define NCT6694_RTC_IRQ_EN	(NCT6694_RTC_IRQ_INT_EN | NCT6694_RTC_IRQ_GPO_EN)
#define NCT6694_RTC_IRQ_STS	BIT(0)	/* Write 1 clear IRQ status */

struct __packed nct6694_rtc_time {
	u8 sec;
	u8 min;
	u8 hour;
	u8 week;
	u8 day;
	u8 month;
	u8 year;
};

struct __packed nct6694_rtc_alarm {
	u8 sec;
	u8 min;
	u8 hour;
	u8 alarm_en;
	u8 alarm_pend;
};

struct __packed nct6694_rtc_status {
	u8 irq_en;
	u8 irq_pend;
};

union __packed nct6694_rtc_msg {
	struct nct6694_rtc_time time;
	struct nct6694_rtc_alarm alarm;
	struct nct6694_rtc_status sts;
};

struct nct6694_rtc_data {
	struct nct6694 *nct6694;
	struct rtc_device *rtc;
	union nct6694_rtc_msg *msg;
	int irq;
};

static int nct6694_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct nct6694_rtc_data *data = dev_get_drvdata(dev);
	struct nct6694_rtc_time *time = &data->msg->time;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_TIME,
		.sel = NCT6694_RTC_TIME_SEL,
		.len = cpu_to_le16(sizeof(*time))
	};
	int ret;

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, time);
	if (ret)
		return ret;

	tm->tm_sec = bcd2bin(time->sec);		/* tm_sec expect 0 ~ 59 */
	tm->tm_min = bcd2bin(time->min);		/* tm_min expect 0 ~ 59 */
	tm->tm_hour = bcd2bin(time->hour);		/* tm_hour expect 0 ~ 23 */
	tm->tm_wday = bcd2bin(time->week) - 1;		/* tm_wday expect 0 ~ 6 */
	tm->tm_mday = bcd2bin(time->day);		/* tm_mday expect 1 ~ 31 */
	tm->tm_mon = bcd2bin(time->month) - 1;		/* tm_month expect 0 ~ 11 */
	tm->tm_year = bcd2bin(time->year) + 100;	/* tm_year expect since 1900 */

	return ret;
}

static int nct6694_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct nct6694_rtc_data *data = dev_get_drvdata(dev);
	struct nct6694_rtc_time *time = &data->msg->time;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_TIME,
		.sel = NCT6694_RTC_TIME_SEL,
		.len = cpu_to_le16(sizeof(*time))
	};

	time->sec = bin2bcd(tm->tm_sec);
	time->min = bin2bcd(tm->tm_min);
	time->hour = bin2bcd(tm->tm_hour);
	time->week = bin2bcd(tm->tm_wday + 1);
	time->day = bin2bcd(tm->tm_mday);
	time->month = bin2bcd(tm->tm_mon + 1);
	time->year = bin2bcd(tm->tm_year - 100);

	return nct6694_write_msg(data->nct6694, &cmd_hd, time);
}

static int nct6694_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct nct6694_rtc_data *data = dev_get_drvdata(dev);
	struct nct6694_rtc_alarm *alarm = &data->msg->alarm;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_ALARM,
		.sel = NCT6694_RTC_ALARM_SEL,
		.len = cpu_to_le16(sizeof(*alarm))
	};
	int ret;

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, alarm);
	if (ret)
		return ret;

	alrm->time.tm_sec = bcd2bin(alarm->sec);
	alrm->time.tm_min = bcd2bin(alarm->min);
	alrm->time.tm_hour = bcd2bin(alarm->hour);
	alrm->enabled = alarm->alarm_en;
	alrm->pending = alarm->alarm_pend;

	return ret;
}

static int nct6694_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct nct6694_rtc_data *data = dev_get_drvdata(dev);
	struct nct6694_rtc_alarm *alarm = &data->msg->alarm;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_ALARM,
		.sel = NCT6694_RTC_ALARM_SEL,
		.len = cpu_to_le16(sizeof(*alarm))
	};

	alarm->sec = bin2bcd(alrm->time.tm_sec);
	alarm->min = bin2bcd(alrm->time.tm_min);
	alarm->hour = bin2bcd(alrm->time.tm_hour);
	alarm->alarm_en = alrm->enabled ? NCT6694_RTC_IRQ_EN : 0;
	alarm->alarm_pend = 0;

	return nct6694_write_msg(data->nct6694, &cmd_hd, alarm);
}

static int nct6694_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct nct6694_rtc_data *data = dev_get_drvdata(dev);
	struct nct6694_rtc_status *sts = &data->msg->sts;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_STATUS,
		.sel = NCT6694_RTC_STATUS_SEL,
		.len = cpu_to_le16(sizeof(*sts))
	};

	if (enabled)
		sts->irq_en |= NCT6694_RTC_IRQ_EN;
	else
		sts->irq_en &= ~NCT6694_RTC_IRQ_EN;

	sts->irq_pend = 0;

	return nct6694_write_msg(data->nct6694, &cmd_hd, sts);
}

static const struct rtc_class_ops nct6694_rtc_ops = {
	.read_time = nct6694_rtc_read_time,
	.set_time = nct6694_rtc_set_time,
	.read_alarm = nct6694_rtc_read_alarm,
	.set_alarm = nct6694_rtc_set_alarm,
	.alarm_irq_enable = nct6694_rtc_alarm_irq_enable,
};

static irqreturn_t nct6694_irq(int irq, void *dev_id)
{
	struct nct6694_rtc_data *data = dev_id;
	struct nct6694_rtc_status *sts = &data->msg->sts;
	static const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_RTC_MOD,
		.cmd = NCT6694_RTC_STATUS,
		.sel = NCT6694_RTC_STATUS_SEL,
		.len = cpu_to_le16(sizeof(*sts))
	};
	int ret;

	rtc_lock(data->rtc);

	sts->irq_en = NCT6694_RTC_IRQ_EN;
	sts->irq_pend = NCT6694_RTC_IRQ_STS;
	ret = nct6694_write_msg(data->nct6694, &cmd_hd, sts);
	if (ret) {
		rtc_unlock(data->rtc);
		return IRQ_NONE;
	}

	rtc_update_irq(data->rtc, 1, RTC_IRQF | RTC_AF);

	rtc_unlock(data->rtc);

	return IRQ_HANDLED;
}

static void nct6694_irq_dispose_mapping(void *d)
{
	struct nct6694_rtc_data *data = d;

	irq_dispose_mapping(data->irq);
}

static int nct6694_rtc_probe(struct platform_device *pdev)
{
	struct nct6694_rtc_data *data;
	struct nct6694 *nct6694 = dev_get_drvdata(pdev->dev.parent);
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->msg = devm_kzalloc(&pdev->dev, sizeof(union nct6694_rtc_msg),
				 GFP_KERNEL);
	if (!data->msg)
		return -ENOMEM;

	data->irq = irq_create_mapping(nct6694->domain, NCT6694_IRQ_RTC);
	if (!data->irq)
		return -EINVAL;

	ret = devm_add_action_or_reset(&pdev->dev, nct6694_irq_dispose_mapping,
				       data);
	if (ret)
		return ret;

	ret = devm_device_init_wakeup(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to init wakeup\n");

	data->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(data->rtc))
		return PTR_ERR(data->rtc);

	data->nct6694 = nct6694;
	data->rtc->ops = &nct6694_rtc_ops;
	data->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	data->rtc->range_max = RTC_TIMESTAMP_END_2099;

	platform_set_drvdata(pdev, data);

	ret = devm_request_threaded_irq(&pdev->dev, data->irq, NULL,
					nct6694_irq, IRQF_ONESHOT,
					"rtc-nct6694", data);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to request irq\n");

	return devm_rtc_register_device(data->rtc);
}

static struct platform_driver nct6694_rtc_driver = {
	.driver = {
		.name	= "nct6694-rtc",
	},
	.probe		= nct6694_rtc_probe,
};

module_platform_driver(nct6694_rtc_driver);

MODULE_DESCRIPTION("USB-RTC driver for NCT6694");
MODULE_AUTHOR("Ming Yu <tmyu0@nuvoton.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nct6694-rtc");
