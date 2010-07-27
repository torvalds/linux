/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 * Author: Virupax Sadashivpetimath <virupax.sadashivpetimath@stericsson.com>
 *
 * RTC clock driver for the RTC part of the AB8500 Power management chip.
 * Based on RTC clock driver for the AB3100 Analog Baseband Chip by
 * Linus Walleij <linus.walleij@stericsson.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/mfd/ab8500.h>
#include <linux/delay.h>

#define AB8500_RTC_SOFF_STAT_REG	0x0F00
#define AB8500_RTC_CC_CONF_REG		0x0F01
#define AB8500_RTC_READ_REQ_REG		0x0F02
#define AB8500_RTC_WATCH_TSECMID_REG	0x0F03
#define AB8500_RTC_WATCH_TSECHI_REG	0x0F04
#define AB8500_RTC_WATCH_TMIN_LOW_REG	0x0F05
#define AB8500_RTC_WATCH_TMIN_MID_REG	0x0F06
#define AB8500_RTC_WATCH_TMIN_HI_REG	0x0F07
#define AB8500_RTC_ALRM_MIN_LOW_REG	0x0F08
#define AB8500_RTC_ALRM_MIN_MID_REG	0x0F09
#define AB8500_RTC_ALRM_MIN_HI_REG	0x0F0A
#define AB8500_RTC_STAT_REG		0x0F0B
#define AB8500_RTC_BKUP_CHG_REG		0x0F0C
#define AB8500_RTC_FORCE_BKUP_REG	0x0F0D
#define AB8500_RTC_CALIB_REG		0x0F0E
#define AB8500_RTC_SWITCH_STAT_REG	0x0F0F
#define AB8500_REV_REG			0x1080

/* RtcReadRequest bits */
#define RTC_READ_REQUEST		0x01
#define RTC_WRITE_REQUEST		0x02

/* RtcCtrl bits */
#define RTC_ALARM_ENA			0x04
#define RTC_STATUS_DATA			0x01

#define COUNTS_PER_SEC			(0xF000 / 60)
#define AB8500_RTC_EPOCH		2000

static const unsigned long ab8500_rtc_time_regs[] = {
	AB8500_RTC_WATCH_TMIN_HI_REG, AB8500_RTC_WATCH_TMIN_MID_REG,
	AB8500_RTC_WATCH_TMIN_LOW_REG, AB8500_RTC_WATCH_TSECHI_REG,
	AB8500_RTC_WATCH_TSECMID_REG
};

static const unsigned long ab8500_rtc_alarm_regs[] = {
	AB8500_RTC_ALRM_MIN_HI_REG, AB8500_RTC_ALRM_MIN_MID_REG,
	AB8500_RTC_ALRM_MIN_LOW_REG
};

/* Calculate the seconds from 1970 to 01-01-2000 00:00:00 */
static unsigned long get_elapsed_seconds(int year)
{
	unsigned long secs;
	struct rtc_time tm = {
		.tm_year = year - 1900,
		.tm_mday = 1,
	};

	/*
	 * This function calculates secs from 1970 and not from
	 * 1900, even if we supply the offset from year 1900.
	 */
	rtc_tm_to_time(&tm, &secs);
	return secs;
}

static int ab8500_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);
	unsigned long timeout = jiffies + HZ;
	int retval, i;
	unsigned long mins, secs;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_time_regs)];

	/* Request a data read */
	retval = ab8500_write(ab8500, AB8500_RTC_READ_REQ_REG,
			      RTC_READ_REQUEST);
	if (retval < 0)
		return retval;

	/* Early AB8500 chips will not clear the rtc read request bit */
	if (ab8500->revision == 0) {
		msleep(1);
	} else {
		/* Wait for some cycles after enabling the rtc read in ab8500 */
		while (time_before(jiffies, timeout)) {
			retval = ab8500_read(ab8500, AB8500_RTC_READ_REQ_REG);
			if (retval < 0)
				return retval;

			if (!(retval & RTC_READ_REQUEST))
				break;

			msleep(1);
		}
	}

	/* Read the Watchtime registers */
	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_time_regs); i++) {
		retval = ab8500_read(ab8500, ab8500_rtc_time_regs[i]);
		if (retval < 0)
			return retval;
		buf[i] = retval;
	}

	mins = (buf[0] << 16) | (buf[1] << 8) | buf[2];

	secs =	(buf[3] << 8) | buf[4];
	secs =	secs / COUNTS_PER_SEC;
	secs =	secs + (mins * 60);

	/* Add back the initially subtracted number of seconds */
	secs += get_elapsed_seconds(AB8500_RTC_EPOCH);

	rtc_time_to_tm(secs, tm);
	return rtc_valid_tm(tm);
}

static int ab8500_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);
	int retval, i;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_time_regs)];
	unsigned long no_secs, no_mins, secs = 0;

	if (tm->tm_year < (AB8500_RTC_EPOCH - 1900)) {
		dev_dbg(dev, "year should be equal to or greater than %d\n",
				AB8500_RTC_EPOCH);
		return -EINVAL;
	}

	/* Get the number of seconds since 1970 */
	rtc_tm_to_time(tm, &secs);

	/*
	 * Convert it to the number of seconds since 01-01-2000 00:00:00, since
	 * we only have a small counter in the RTC.
	 */
	secs -= get_elapsed_seconds(AB8500_RTC_EPOCH);

	no_mins = secs / 60;

	no_secs = secs % 60;
	/* Make the seconds count as per the RTC resolution */
	no_secs = no_secs * COUNTS_PER_SEC;

	buf[4] = no_secs & 0xFF;
	buf[3] = (no_secs >> 8) & 0xFF;

	buf[2] = no_mins & 0xFF;
	buf[1] = (no_mins >> 8) & 0xFF;
	buf[0] = (no_mins >> 16) & 0xFF;

	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_time_regs); i++) {
		retval = ab8500_write(ab8500, ab8500_rtc_time_regs[i], buf[i]);
		if (retval < 0)
			return retval;
	}

	/* Request a data write */
	return ab8500_write(ab8500, AB8500_RTC_READ_REQ_REG, RTC_WRITE_REQUEST);
}

static int ab8500_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);
	int retval, i;
	int rtc_ctrl;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_alarm_regs)];
	unsigned long secs, mins;

	/* Check if the alarm is enabled or not */
	rtc_ctrl = ab8500_read(ab8500, AB8500_RTC_STAT_REG);
	if (rtc_ctrl < 0)
		return rtc_ctrl;

	if (rtc_ctrl & RTC_ALARM_ENA)
		alarm->enabled = 1;
	else
		alarm->enabled = 0;

	alarm->pending = 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_alarm_regs); i++) {
		retval = ab8500_read(ab8500, ab8500_rtc_alarm_regs[i]);
		if (retval < 0)
			return retval;
		buf[i] = retval;
	}

	mins = (buf[0] << 16) | (buf[1] << 8) | (buf[2]);
	secs = mins * 60;

	/* Add back the initially subtracted number of seconds */
	secs += get_elapsed_seconds(AB8500_RTC_EPOCH);

	rtc_time_to_tm(secs, &alarm->time);

	return rtc_valid_tm(&alarm->time);
}

static int ab8500_rtc_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);

	return ab8500_set_bits(ab8500, AB8500_RTC_STAT_REG, RTC_ALARM_ENA,
			       enabled ? RTC_ALARM_ENA : 0);
}

static int ab8500_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct ab8500 *ab8500 = dev_get_drvdata(dev->parent);
	int retval, i;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_alarm_regs)];
	unsigned long mins, secs = 0;

	if (alarm->time.tm_year < (AB8500_RTC_EPOCH - 1900)) {
		dev_dbg(dev, "year should be equal to or greater than %d\n",
				AB8500_RTC_EPOCH);
		return -EINVAL;
	}

	/* Get the number of seconds since 1970 */
	rtc_tm_to_time(&alarm->time, &secs);

	/*
	 * Convert it to the number of seconds since 01-01-2000 00:00:00, since
	 * we only have a small counter in the RTC.
	 */
	secs -= get_elapsed_seconds(AB8500_RTC_EPOCH);

	mins = secs / 60;

	buf[2] = mins & 0xFF;
	buf[1] = (mins >> 8) & 0xFF;
	buf[0] = (mins >> 16) & 0xFF;

	/* Set the alarm time */
	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_alarm_regs); i++) {
		retval = ab8500_write(ab8500, ab8500_rtc_alarm_regs[i], buf[i]);
		if (retval < 0)
			return retval;
	}

	return ab8500_rtc_irq_enable(dev, alarm->enabled);
}

static irqreturn_t rtc_alarm_handler(int irq, void *data)
{
	struct rtc_device *rtc = data;
	unsigned long events = RTC_IRQF | RTC_AF;

	dev_dbg(&rtc->dev, "%s\n", __func__);
	rtc_update_irq(rtc, 1, events);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops ab8500_rtc_ops = {
	.read_time		= ab8500_rtc_read_time,
	.set_time		= ab8500_rtc_set_time,
	.read_alarm		= ab8500_rtc_read_alarm,
	.set_alarm		= ab8500_rtc_set_alarm,
	.alarm_irq_enable	= ab8500_rtc_irq_enable,
};

static int __devinit ab8500_rtc_probe(struct platform_device *pdev)
{
	struct ab8500 *ab8500 = dev_get_drvdata(pdev->dev.parent);
	int err;
	struct rtc_device *rtc;
	int rtc_ctrl;
	int irq;

	irq = platform_get_irq_byname(pdev, "ALARM");
	if (irq < 0)
		return irq;

	/* For RTC supply test */
	err = ab8500_set_bits(ab8500, AB8500_RTC_STAT_REG, RTC_STATUS_DATA,
			RTC_STATUS_DATA);
	if (err < 0)
		return err;

	/* Wait for reset by the PorRtc */
	msleep(1);

	rtc_ctrl = ab8500_read(ab8500, AB8500_RTC_STAT_REG);
	if (rtc_ctrl < 0)
		return rtc_ctrl;

	/* Check if the RTC Supply fails */
	if (!(rtc_ctrl & RTC_STATUS_DATA)) {
		dev_err(&pdev->dev, "RTC supply failure\n");
		return -ENODEV;
	}

	rtc = rtc_device_register("ab8500-rtc", &pdev->dev, &ab8500_rtc_ops,
			THIS_MODULE);
	if (IS_ERR(rtc)) {
		dev_err(&pdev->dev, "Registration failed\n");
		err = PTR_ERR(rtc);
		return err;
	}

	err = request_threaded_irq(irq, NULL, rtc_alarm_handler, 0,
				   "ab8500-rtc", rtc);
	if (err < 0) {
		rtc_device_unregister(rtc);
		return err;
	}

	platform_set_drvdata(pdev, rtc);

	return 0;
}

static int __devexit ab8500_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	int irq = platform_get_irq_byname(pdev, "ALARM");

	free_irq(irq, rtc);
	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ab8500_rtc_driver = {
	.driver = {
		.name = "ab8500-rtc",
		.owner = THIS_MODULE,
	},
	.probe	= ab8500_rtc_probe,
	.remove = __devexit_p(ab8500_rtc_remove),
};

static int __init ab8500_rtc_init(void)
{
	return platform_driver_register(&ab8500_rtc_driver);
}

static void __exit ab8500_rtc_exit(void)
{
	platform_driver_unregister(&ab8500_rtc_driver);
}

module_init(ab8500_rtc_init);
module_exit(ab8500_rtc_exit);
MODULE_AUTHOR("Virupax Sadashivpetimath <virupax.sadashivpetimath@stericsson.com>");
MODULE_DESCRIPTION("AB8500 RTC Driver");
MODULE_LICENSE("GPL v2");
