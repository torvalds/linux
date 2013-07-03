/*
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * RTC clock driver for the AB3100 Analog Baseband Chip
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/mfd/abx500.h>

/* Clock rate in Hz */
#define AB3100_RTC_CLOCK_RATE	32768

/*
 * The AB3100 RTC registers. These are the same for
 * AB3000 and AB3100.
 * Control register:
 * Bit 0: RTC Monitor cleared=0, active=1, if you set it
 *        to 1 it remains active until RTC power is lost.
 * Bit 1: 32 kHz Oscillator, 0 = on, 1 = bypass
 * Bit 2: Alarm on, 0 = off, 1 = on
 * Bit 3: 32 kHz buffer disabling, 0 = enabled, 1 = disabled
 */
#define AB3100_RTC		0x53
/* default setting, buffer disabled, alarm on */
#define RTC_SETTING		0x30
/* Alarm when AL0-AL3 == TI0-TI3  */
#define AB3100_AL0		0x56
#define AB3100_AL1		0x57
#define AB3100_AL2		0x58
#define AB3100_AL3		0x59
/* This 48-bit register that counts up at 32768 Hz */
#define AB3100_TI0		0x5a
#define AB3100_TI1		0x5b
#define AB3100_TI2		0x5c
#define AB3100_TI3		0x5d
#define AB3100_TI4		0x5e
#define AB3100_TI5		0x5f

/*
 * RTC clock functions and device struct declaration
 */
static int ab3100_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	u8 regs[] = {AB3100_TI0, AB3100_TI1, AB3100_TI2,
		     AB3100_TI3, AB3100_TI4, AB3100_TI5};
	unsigned char buf[6];
	u64 fat_time = (u64) secs * AB3100_RTC_CLOCK_RATE * 2;
	int err = 0;
	int i;

	buf[0] = (fat_time) & 0xFF;
	buf[1] = (fat_time >> 8) & 0xFF;
	buf[2] = (fat_time >> 16) & 0xFF;
	buf[3] = (fat_time >> 24) & 0xFF;
	buf[4] = (fat_time >> 32) & 0xFF;
	buf[5] = (fat_time >> 40) & 0xFF;

	for (i = 0; i < 6; i++) {
		err = abx500_set_register_interruptible(dev, 0,
							regs[i], buf[i]);
		if (err)
			return err;
	}

	/* Set the flag to mark that the clock is now set */
	return abx500_mask_and_set_register_interruptible(dev, 0,
							  AB3100_RTC,
							  0x01, 0x01);

}

static int ab3100_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long time;
	u8 rtcval;
	int err;

	err = abx500_get_register_interruptible(dev, 0,
						AB3100_RTC, &rtcval);
	if (err)
		return err;

	if (!(rtcval & 0x01)) {
		dev_info(dev, "clock not set (lost power)");
		return -EINVAL;
	} else {
		u64 fat_time;
		u8 buf[6];

		/* Read out time registers */
		err = abx500_get_register_page_interruptible(dev, 0,
							     AB3100_TI0,
							     buf, 6);
		if (err != 0)
			return err;

		fat_time = ((u64) buf[5] << 40) | ((u64) buf[4] << 32) |
			((u64) buf[3] << 24) | ((u64) buf[2] << 16) |
			((u64) buf[1] << 8) | (u64) buf[0];
		time = (unsigned long) (fat_time /
					(u64) (AB3100_RTC_CLOCK_RATE * 2));
	}

	rtc_time_to_tm(time, tm);

	return rtc_valid_tm(tm);
}

static int ab3100_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	unsigned long time;
	u64 fat_time;
	u8 buf[6];
	u8 rtcval;
	int err;

	/* Figure out if alarm is enabled or not */
	err = abx500_get_register_interruptible(dev, 0,
						AB3100_RTC, &rtcval);
	if (err)
		return err;
	if (rtcval & 0x04)
		alarm->enabled = 1;
	else
		alarm->enabled = 0;
	/* No idea how this could be represented */
	alarm->pending = 0;
	/* Read out alarm registers, only 4 bytes */
	err = abx500_get_register_page_interruptible(dev, 0,
						     AB3100_AL0, buf, 4);
	if (err)
		return err;
	fat_time = ((u64) buf[3] << 40) | ((u64) buf[2] << 32) |
		((u64) buf[1] << 24) | ((u64) buf[0] << 16);
	time = (unsigned long) (fat_time / (u64) (AB3100_RTC_CLOCK_RATE * 2));

	rtc_time_to_tm(time, &alarm->time);

	return rtc_valid_tm(&alarm->time);
}

static int ab3100_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	u8 regs[] = {AB3100_AL0, AB3100_AL1, AB3100_AL2, AB3100_AL3};
	unsigned char buf[4];
	unsigned long secs;
	u64 fat_time;
	int err;
	int i;

	rtc_tm_to_time(&alarm->time, &secs);
	fat_time = (u64) secs * AB3100_RTC_CLOCK_RATE * 2;
	buf[0] = (fat_time >> 16) & 0xFF;
	buf[1] = (fat_time >> 24) & 0xFF;
	buf[2] = (fat_time >> 32) & 0xFF;
	buf[3] = (fat_time >> 40) & 0xFF;

	/* Set the alarm */
	for (i = 0; i < 4; i++) {
		err = abx500_set_register_interruptible(dev, 0,
							regs[i], buf[i]);
		if (err)
			return err;
	}
	/* Then enable the alarm */
	return abx500_mask_and_set_register_interruptible(dev, 0,
							  AB3100_RTC, (1 << 2),
							  alarm->enabled << 2);
}

static int ab3100_rtc_irq_enable(struct device *dev, unsigned int enabled)
{
	/*
	 * It's not possible to enable/disable the alarm IRQ for this RTC.
	 * It does not actually trigger any IRQ: instead its only function is
	 * to power up the system, if it wasn't on. This will manifest as
	 * a "power up cause" in the AB3100 power driver (battery charging etc)
	 * and need to be handled there instead.
	 */
	if (enabled)
		return abx500_mask_and_set_register_interruptible(dev, 0,
						    AB3100_RTC, (1 << 2),
						    1 << 2);
	else
		return abx500_mask_and_set_register_interruptible(dev, 0,
						    AB3100_RTC, (1 << 2),
						    0);
}

static const struct rtc_class_ops ab3100_rtc_ops = {
	.read_time	= ab3100_rtc_read_time,
	.set_mmss	= ab3100_rtc_set_mmss,
	.read_alarm	= ab3100_rtc_read_alarm,
	.set_alarm	= ab3100_rtc_set_alarm,
	.alarm_irq_enable = ab3100_rtc_irq_enable,
};

static int __init ab3100_rtc_probe(struct platform_device *pdev)
{
	int err;
	u8 regval;
	struct rtc_device *rtc;

	/* The first RTC register needs special treatment */
	err = abx500_get_register_interruptible(&pdev->dev, 0,
						AB3100_RTC, &regval);
	if (err) {
		dev_err(&pdev->dev, "unable to read RTC register\n");
		return -ENODEV;
	}

	if ((regval & 0xFE) != RTC_SETTING) {
		dev_warn(&pdev->dev, "not default value in RTC reg 0x%x\n",
			 regval);
	}

	if ((regval & 1) == 0) {
		/*
		 * Set bit to detect power loss.
		 * This bit remains until RTC power is lost.
		 */
		regval = 1 | RTC_SETTING;
		err = abx500_set_register_interruptible(&pdev->dev, 0,
							AB3100_RTC, regval);
		/* Ignore any error on this write */
	}

	rtc = devm_rtc_device_register(&pdev->dev, "ab3100-rtc",
					&ab3100_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		err = PTR_ERR(rtc);
		return err;
	}
	platform_set_drvdata(pdev, rtc);

	return 0;
}

static int __exit ab3100_rtc_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver ab3100_rtc_driver = {
	.driver = {
		.name = "ab3100-rtc",
		.owner = THIS_MODULE,
	},
	.remove	 = __exit_p(ab3100_rtc_remove),
};

module_platform_driver_probe(ab3100_rtc_driver, ab3100_rtc_probe);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("AB3100 RTC Driver");
MODULE_LICENSE("GPL");
