// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 *
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
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/pm_wakeirq.h>

#define AB8500_RTC_SOFF_STAT_REG	0x00
#define AB8500_RTC_CC_CONF_REG		0x01
#define AB8500_RTC_READ_REQ_REG		0x02
#define AB8500_RTC_WATCH_TSECMID_REG	0x03
#define AB8500_RTC_WATCH_TSECHI_REG	0x04
#define AB8500_RTC_WATCH_TMIN_LOW_REG	0x05
#define AB8500_RTC_WATCH_TMIN_MID_REG	0x06
#define AB8500_RTC_WATCH_TMIN_HI_REG	0x07
#define AB8500_RTC_ALRM_MIN_LOW_REG	0x08
#define AB8500_RTC_ALRM_MIN_MID_REG	0x09
#define AB8500_RTC_ALRM_MIN_HI_REG	0x0A
#define AB8500_RTC_STAT_REG		0x0B
#define AB8500_RTC_BKUP_CHG_REG		0x0C
#define AB8500_RTC_FORCE_BKUP_REG	0x0D
#define AB8500_RTC_CALIB_REG		0x0E
#define AB8500_RTC_SWITCH_STAT_REG	0x0F

/* RtcReadRequest bits */
#define RTC_READ_REQUEST		0x01
#define RTC_WRITE_REQUEST		0x02

/* RtcCtrl bits */
#define RTC_ALARM_ENA			0x04
#define RTC_STATUS_DATA			0x01

#define COUNTS_PER_SEC			(0xF000 / 60)

static const u8 ab8500_rtc_time_regs[] = {
	AB8500_RTC_WATCH_TMIN_HI_REG, AB8500_RTC_WATCH_TMIN_MID_REG,
	AB8500_RTC_WATCH_TMIN_LOW_REG, AB8500_RTC_WATCH_TSECHI_REG,
	AB8500_RTC_WATCH_TSECMID_REG
};

static const u8 ab8500_rtc_alarm_regs[] = {
	AB8500_RTC_ALRM_MIN_HI_REG, AB8500_RTC_ALRM_MIN_MID_REG,
	AB8500_RTC_ALRM_MIN_LOW_REG
};

static int ab8500_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long timeout = jiffies + HZ;
	int retval, i;
	unsigned long mins, secs;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_time_regs)];
	u8 value;

	/* Request a data read */
	retval = abx500_set_register_interruptible(dev,
		AB8500_RTC, AB8500_RTC_READ_REQ_REG, RTC_READ_REQUEST);
	if (retval < 0)
		return retval;

	/* Wait for some cycles after enabling the rtc read in ab8500 */
	while (time_before(jiffies, timeout)) {
		retval = abx500_get_register_interruptible(dev,
			AB8500_RTC, AB8500_RTC_READ_REQ_REG, &value);
		if (retval < 0)
			return retval;

		if (!(value & RTC_READ_REQUEST))
			break;

		usleep_range(1000, 5000);
	}

	/* Read the Watchtime registers */
	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_time_regs); i++) {
		retval = abx500_get_register_interruptible(dev,
			AB8500_RTC, ab8500_rtc_time_regs[i], &value);
		if (retval < 0)
			return retval;
		buf[i] = value;
	}

	mins = (buf[0] << 16) | (buf[1] << 8) | buf[2];

	secs =	(buf[3] << 8) | buf[4];
	secs =	secs / COUNTS_PER_SEC;
	secs =	secs + (mins * 60);

	rtc_time64_to_tm(secs, tm);
	return 0;
}

static int ab8500_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	int retval, i;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_time_regs)];
	unsigned long no_secs, no_mins, secs = 0;

	secs = rtc_tm_to_time64(tm);

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
		retval = abx500_set_register_interruptible(dev, AB8500_RTC,
			ab8500_rtc_time_regs[i], buf[i]);
		if (retval < 0)
			return retval;
	}

	/* Request a data write */
	return abx500_set_register_interruptible(dev, AB8500_RTC,
		AB8500_RTC_READ_REQ_REG, RTC_WRITE_REQUEST);
}

static int ab8500_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	int retval, i;
	u8 rtc_ctrl, value;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_alarm_regs)];
	unsigned long secs, mins;

	/* Check if the alarm is enabled or not */
	retval = abx500_get_register_interruptible(dev, AB8500_RTC,
		AB8500_RTC_STAT_REG, &rtc_ctrl);
	if (retval < 0)
		return retval;

	if (rtc_ctrl & RTC_ALARM_ENA)
		alarm->enabled = 1;
	else
		alarm->enabled = 0;

	alarm->pending = 0;

	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_alarm_regs); i++) {
		retval = abx500_get_register_interruptible(dev, AB8500_RTC,
			ab8500_rtc_alarm_regs[i], &value);
		if (retval < 0)
			return retval;
		buf[i] = value;
	}

	mins = (buf[0] << 16) | (buf[1] << 8) | (buf[2]);
	secs = mins * 60;

	rtc_time64_to_tm(secs, &alarm->time);

	return 0;
}

static int ab8500_rtc_irq_enable(struct device *dev, unsigned int enabled)
{
	return abx500_mask_and_set_register_interruptible(dev, AB8500_RTC,
		AB8500_RTC_STAT_REG, RTC_ALARM_ENA,
		enabled ? RTC_ALARM_ENA : 0);
}

static int ab8500_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	int retval, i;
	unsigned char buf[ARRAY_SIZE(ab8500_rtc_alarm_regs)];
	unsigned long mins, secs = 0, cursec = 0;
	struct rtc_time curtm;

	/* Get the number of seconds since 1970 */
	secs = rtc_tm_to_time64(&alarm->time);

	/*
	 * Check whether alarm is set less than 1min.
	 * Since our RTC doesn't support alarm resolution less than 1min,
	 * return -EINVAL, so UIE EMUL can take it up, incase of UIE_ON
	 */
	ab8500_rtc_read_time(dev, &curtm); /* Read current time */
	cursec = rtc_tm_to_time64(&curtm);
	if ((secs - cursec) < 59) {
		dev_dbg(dev, "Alarm less than 1 minute not supported\r\n");
		return -EINVAL;
	}

	mins = secs / 60;

	buf[2] = mins & 0xFF;
	buf[1] = (mins >> 8) & 0xFF;
	buf[0] = (mins >> 16) & 0xFF;

	/* Set the alarm time */
	for (i = 0; i < ARRAY_SIZE(ab8500_rtc_alarm_regs); i++) {
		retval = abx500_set_register_interruptible(dev, AB8500_RTC,
			ab8500_rtc_alarm_regs[i], buf[i]);
		if (retval < 0)
			return retval;
	}

	return ab8500_rtc_irq_enable(dev, alarm->enabled);
}

static int ab8500_rtc_set_calibration(struct device *dev, int calibration)
{
	int retval;
	u8  rtccal = 0;

	/*
	 * Check that the calibration value (which is in units of 0.5
	 * parts-per-million) is in the AB8500's range for RtcCalibration
	 * register. -128 (0x80) is not permitted because the AB8500 uses
	 * a sign-bit rather than two's complement, so 0x80 is just another
	 * representation of zero.
	 */
	if ((calibration < -127) || (calibration > 127)) {
		dev_err(dev, "RtcCalibration value outside permitted range\n");
		return -EINVAL;
	}

	/*
	 * The AB8500 uses sign (in bit7) and magnitude (in bits0-7)
	 * so need to convert to this sort of representation before writing
	 * into RtcCalibration register...
	 */
	if (calibration >= 0)
		rtccal = 0x7F & calibration;
	else
		rtccal = ~(calibration - 1) | 0x80;

	retval = abx500_set_register_interruptible(dev, AB8500_RTC,
			AB8500_RTC_CALIB_REG, rtccal);

	return retval;
}

static int ab8500_rtc_get_calibration(struct device *dev, int *calibration)
{
	int retval;
	u8  rtccal = 0;

	retval =  abx500_get_register_interruptible(dev, AB8500_RTC,
			AB8500_RTC_CALIB_REG, &rtccal);
	if (retval >= 0) {
		/*
		 * The AB8500 uses sign (in bit7) and magnitude (in bits0-7)
		 * so need to convert value from RtcCalibration register into
		 * a two's complement signed value...
		 */
		if (rtccal & 0x80)
			*calibration = 0 - (rtccal & 0x7F);
		else
			*calibration = 0x7F & rtccal;
	}

	return retval;
}

static ssize_t ab8500_sysfs_store_rtc_calibration(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int retval;
	int calibration = 0;

	if (sscanf(buf, " %i ", &calibration) != 1) {
		dev_err(dev, "Failed to store RTC calibration attribute\n");
		return -EINVAL;
	}

	retval = ab8500_rtc_set_calibration(dev, calibration);

	return retval ? retval : count;
}

static ssize_t ab8500_sysfs_show_rtc_calibration(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int  retval = 0;
	int  calibration = 0;

	retval = ab8500_rtc_get_calibration(dev, &calibration);
	if (retval < 0) {
		dev_err(dev, "Failed to read RTC calibration attribute\n");
		sprintf(buf, "0\n");
		return retval;
	}

	return sprintf(buf, "%d\n", calibration);
}

static DEVICE_ATTR(rtc_calibration, S_IRUGO | S_IWUSR,
		   ab8500_sysfs_show_rtc_calibration,
		   ab8500_sysfs_store_rtc_calibration);

static struct attribute *ab8500_rtc_attrs[] = {
	&dev_attr_rtc_calibration.attr,
	NULL
};

static const struct attribute_group ab8500_rtc_sysfs_files = {
	.attrs	= ab8500_rtc_attrs,
};

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

static const struct platform_device_id ab85xx_rtc_ids[] = {
	{ "ab8500-rtc", (kernel_ulong_t)&ab8500_rtc_ops, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, ab85xx_rtc_ids);

static int ab8500_rtc_probe(struct platform_device *pdev)
{
	const struct platform_device_id *platid = platform_get_device_id(pdev);
	int err;
	struct rtc_device *rtc;
	u8 rtc_ctrl;
	int irq;

	irq = platform_get_irq_byname(pdev, "ALARM");
	if (irq < 0)
		return irq;

	/* For RTC supply test */
	err = abx500_mask_and_set_register_interruptible(&pdev->dev, AB8500_RTC,
		AB8500_RTC_STAT_REG, RTC_STATUS_DATA, RTC_STATUS_DATA);
	if (err < 0)
		return err;

	/* Wait for reset by the PorRtc */
	usleep_range(1000, 5000);

	err = abx500_get_register_interruptible(&pdev->dev, AB8500_RTC,
		AB8500_RTC_STAT_REG, &rtc_ctrl);
	if (err < 0)
		return err;

	/* Check if the RTC Supply fails */
	if (!(rtc_ctrl & RTC_STATUS_DATA)) {
		dev_err(&pdev->dev, "RTC supply failure\n");
		return -ENODEV;
	}

	device_init_wakeup(&pdev->dev, true);

	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = (struct rtc_class_ops *)platid->driver_data;

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
			rtc_alarm_handler, IRQF_ONESHOT,
			"ab8500-rtc", rtc);
	if (err < 0)
		return err;

	dev_pm_set_wake_irq(&pdev->dev, irq);
	platform_set_drvdata(pdev, rtc);

	rtc->uie_unsupported = 1;

	rtc->range_max = (1ULL << 24) * 60 - 1; // 24-bit minutes + 59 secs
	rtc->start_secs = RTC_TIMESTAMP_BEGIN_2000;
	rtc->set_start_time = true;

	err = rtc_add_group(rtc, &ab8500_rtc_sysfs_files);
	if (err)
		return err;

	return devm_rtc_register_device(rtc);
}

static int ab8500_rtc_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);

	return 0;
}

static struct platform_driver ab8500_rtc_driver = {
	.driver = {
		.name = "ab8500-rtc",
	},
	.probe	= ab8500_rtc_probe,
	.remove = ab8500_rtc_remove,
	.id_table = ab85xx_rtc_ids,
};

module_platform_driver(ab8500_rtc_driver);

MODULE_AUTHOR("Virupax Sadashivpetimath <virupax.sadashivpetimath@stericsson.com>");
MODULE_DESCRIPTION("AB8500 RTC Driver");
MODULE_LICENSE("GPL v2");
