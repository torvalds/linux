/*
 * Au1xxx counter0 (aka Time-Of-Year counter) RTC interface driver.
 *
 * Copyright (C) 2008 Manuel Lauss <mano@roarinelk.homelinux.net>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

/* All current Au1xxx SoCs have 2 counters fed by an external 32.768 kHz
 * crystal. Counter 0, which keeps counting during sleep/powerdown, is
 * used to count seconds since the beginning of the unix epoch.
 *
 * The counters must be configured and enabled by bootloader/board code;
 * no checks as to whether they really get a proper 32.768kHz clock are
 * made as this would take far too long.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <asm/mach-au1x00/au1000.h>

/* 32kHz clock enabled and detected */
#define CNTR_OK (SYS_CNTRL_E0 | SYS_CNTRL_32S)

static int au1xtoy_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t;

	t = alchemy_rdsys(AU1000_SYS_TOYREAD);

	rtc_time64_to_tm(t, tm);

	return 0;
}

static int au1xtoy_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long t;

	t = rtc_tm_to_time64(tm);

	alchemy_wrsys(t, AU1000_SYS_TOYWRITE);

	/* wait for the pending register write to succeed.  This can
	 * take up to 6 seconds...
	 */
	while (alchemy_rdsys(AU1000_SYS_CNTRCTRL) & SYS_CNTRL_C0S)
		msleep(1);

	return 0;
}

static const struct rtc_class_ops au1xtoy_rtc_ops = {
	.read_time	= au1xtoy_rtc_read_time,
	.set_time	= au1xtoy_rtc_set_time,
};

static int au1xtoy_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtcdev;
	unsigned long t;

	t = alchemy_rdsys(AU1000_SYS_CNTRCTRL);
	if (!(t & CNTR_OK)) {
		dev_err(&pdev->dev, "counters not working; aborting.\n");
		return -ENODEV;
	}

	/* set counter0 tickrate to 1Hz if necessary */
	if (alchemy_rdsys(AU1000_SYS_TOYTRIM) != 32767) {
		/* wait until hardware gives access to TRIM register */
		t = 0x00100000;
		while ((alchemy_rdsys(AU1000_SYS_CNTRCTRL) & SYS_CNTRL_T0S) && --t)
			msleep(1);

		if (!t) {
			/* timed out waiting for register access; assume
			 * counters are unusable.
			 */
			dev_err(&pdev->dev, "timeout waiting for access\n");
			return -ETIMEDOUT;
		}

		/* set 1Hz TOY tick rate */
		alchemy_wrsys(32767, AU1000_SYS_TOYTRIM);
	}

	/* wait until the hardware allows writes to the counter reg */
	while (alchemy_rdsys(AU1000_SYS_CNTRCTRL) & SYS_CNTRL_C0S)
		msleep(1);

	rtcdev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtcdev))
		return PTR_ERR(rtcdev);

	rtcdev->ops = &au1xtoy_rtc_ops;
	rtcdev->range_max = U32_MAX;

	platform_set_drvdata(pdev, rtcdev);

	return devm_rtc_register_device(rtcdev);
}

static struct platform_driver au1xrtc_driver = {
	.driver		= {
		.name	= "rtc-au1xxx",
	},
};

module_platform_driver_probe(au1xrtc_driver, au1xtoy_rtc_probe);

MODULE_DESCRIPTION("Au1xxx TOY-counter-based RTC driver");
MODULE_AUTHOR("Manuel Lauss <manuel.lauss@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-au1xxx");
