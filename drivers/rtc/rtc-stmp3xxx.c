/*
 * Freescale STMP37XX/STMP378X Real Time Clock driver
 *
 * Copyright (c) 2007 Sigmatel, Inc.
 * Peter Hartley, <peter.hartley@sigmatel.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 * Copyright 2011 Wolfram Sang, Pengutronix e.K.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/stmp_device.h>
#include <linux/stmp3xxx_rtc_wdt.h>

#define STMP3XXX_RTC_CTRL			0x0
#define STMP3XXX_RTC_CTRL_ALARM_IRQ_EN		0x00000001
#define STMP3XXX_RTC_CTRL_ONEMSEC_IRQ_EN	0x00000002
#define STMP3XXX_RTC_CTRL_ALARM_IRQ		0x00000004
#define STMP3XXX_RTC_CTRL_WATCHDOGEN		0x00000010

#define STMP3XXX_RTC_STAT			0x10
#define STMP3XXX_RTC_STAT_STALE_SHIFT		16
#define STMP3XXX_RTC_STAT_RTC_PRESENT		0x80000000
#define STMP3XXX_RTC_STAT_XTAL32000_PRESENT	0x10000000
#define STMP3XXX_RTC_STAT_XTAL32768_PRESENT	0x08000000

#define STMP3XXX_RTC_SECONDS			0x30

#define STMP3XXX_RTC_ALARM			0x40

#define STMP3XXX_RTC_WATCHDOG			0x50

#define STMP3XXX_RTC_PERSISTENT0		0x60
#define STMP3XXX_RTC_PERSISTENT0_CLOCKSOURCE		(1 << 0)
#define STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN		(1 << 1)
#define STMP3XXX_RTC_PERSISTENT0_ALARM_EN		(1 << 2)
#define STMP3XXX_RTC_PERSISTENT0_XTAL24MHZ_PWRUP	(1 << 4)
#define STMP3XXX_RTC_PERSISTENT0_XTAL32KHZ_PWRUP	(1 << 5)
#define STMP3XXX_RTC_PERSISTENT0_XTAL32_FREQ		(1 << 6)
#define STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE		(1 << 7)

#define STMP3XXX_RTC_PERSISTENT1		0x70
/* missing bitmask in headers */
#define STMP3XXX_RTC_PERSISTENT1_FORCE_UPDATER	0x80000000

struct stmp3xxx_rtc_data {
	struct rtc_device *rtc;
	void __iomem *io;
	int irq_alarm;
};

#if IS_ENABLED(CONFIG_STMP3XXX_RTC_WATCHDOG)
/**
 * stmp3xxx_wdt_set_timeout - configure the watchdog inside the STMP3xxx RTC
 * @dev: the parent device of the watchdog (= the RTC)
 * @timeout: the desired value for the timeout register of the watchdog.
 *           0 disables the watchdog
 *
 * The watchdog needs one register and two bits which are in the RTC domain.
 * To handle the resource conflict, the RTC driver will create another
 * platform_device for the watchdog driver as a child of the RTC device.
 * The watchdog driver is passed the below accessor function via platform_data
 * to configure the watchdog. Locking is not needed because accessing SET/CLR
 * registers is atomic.
 */

static void stmp3xxx_wdt_set_timeout(struct device *dev, u32 timeout)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	if (timeout) {
		writel(timeout, rtc_data->io + STMP3XXX_RTC_WATCHDOG);
		writel(STMP3XXX_RTC_CTRL_WATCHDOGEN,
		       rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_SET);
		writel(STMP3XXX_RTC_PERSISTENT1_FORCE_UPDATER,
		       rtc_data->io + STMP3XXX_RTC_PERSISTENT1 + STMP_OFFSET_REG_SET);
	} else {
		writel(STMP3XXX_RTC_CTRL_WATCHDOGEN,
		       rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_CLR);
		writel(STMP3XXX_RTC_PERSISTENT1_FORCE_UPDATER,
		       rtc_data->io + STMP3XXX_RTC_PERSISTENT1 + STMP_OFFSET_REG_CLR);
	}
}

static struct stmp3xxx_wdt_pdata wdt_pdata = {
	.wdt_set_timeout = stmp3xxx_wdt_set_timeout,
};

static void stmp3xxx_wdt_register(struct platform_device *rtc_pdev)
{
	int rc = -1;
	struct platform_device *wdt_pdev =
		platform_device_alloc("stmp3xxx_rtc_wdt", rtc_pdev->id);

	if (wdt_pdev) {
		wdt_pdev->dev.parent = &rtc_pdev->dev;
		wdt_pdev->dev.platform_data = &wdt_pdata;
		rc = platform_device_add(wdt_pdev);
	}

	if (rc)
		dev_err(&rtc_pdev->dev,
			"failed to register stmp3xxx_rtc_wdt\n");
}
#else
static void stmp3xxx_wdt_register(struct platform_device *rtc_pdev)
{
}
#endif /* CONFIG_STMP3XXX_RTC_WATCHDOG */

static int stmp3xxx_wait_time(struct stmp3xxx_rtc_data *rtc_data)
{
	int timeout = 5000; /* 3ms according to i.MX28 Ref Manual */
	/*
	 * The i.MX28 Applications Processor Reference Manual, Rev. 1, 2010
	 * states:
	 * | The order in which registers are updated is
	 * | Persistent 0, 1, 2, 3, 4, 5, Alarm, Seconds.
	 * | (This list is in bitfield order, from LSB to MSB, as they would
	 * | appear in the STALE_REGS and NEW_REGS bitfields of the HW_RTC_STAT
	 * | register. For example, the Seconds register corresponds to
	 * | STALE_REGS or NEW_REGS containing 0x80.)
	 */
	do {
		if (!(readl(rtc_data->io + STMP3XXX_RTC_STAT) &
				(0x80 << STMP3XXX_RTC_STAT_STALE_SHIFT)))
			return 0;
		udelay(1);
	} while (--timeout > 0);
	return (readl(rtc_data->io + STMP3XXX_RTC_STAT) &
		(0x80 << STMP3XXX_RTC_STAT_STALE_SHIFT)) ? -ETIME : 0;
}

/* Time read/write */
static int stmp3xxx_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	int ret;
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	ret = stmp3xxx_wait_time(rtc_data);
	if (ret)
		return ret;

	rtc_time_to_tm(readl(rtc_data->io + STMP3XXX_RTC_SECONDS), rtc_tm);
	return 0;
}

static int stmp3xxx_rtc_set_mmss(struct device *dev, unsigned long t)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	writel(t, rtc_data->io + STMP3XXX_RTC_SECONDS);
	return stmp3xxx_wait_time(rtc_data);
}

/* interrupt(s) handler */
static irqreturn_t stmp3xxx_rtc_interrupt(int irq, void *dev_id)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev_id);
	u32 status = readl(rtc_data->io + STMP3XXX_RTC_CTRL);

	if (status & STMP3XXX_RTC_CTRL_ALARM_IRQ) {
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ,
			rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_CLR);
		rtc_update_irq(rtc_data->rtc, 1, RTC_AF | RTC_IRQF);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int stmp3xxx_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	if (enabled) {
		writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
				STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN,
			rtc_data->io + STMP3XXX_RTC_PERSISTENT0 +
				STMP_OFFSET_REG_SET);
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
			rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_SET);
	} else {
		writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
				STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN,
			rtc_data->io + STMP3XXX_RTC_PERSISTENT0 +
				STMP_OFFSET_REG_CLR);
		writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
			rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_CLR);
	}
	return 0;
}

static int stmp3xxx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	rtc_time_to_tm(readl(rtc_data->io + STMP3XXX_RTC_ALARM), &alm->time);
	return 0;
}

static int stmp3xxx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned long t;
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	rtc_tm_to_time(&alm->time, &t);
	writel(t, rtc_data->io + STMP3XXX_RTC_ALARM);

	stmp3xxx_alarm_irq_enable(dev, alm->enabled);

	return 0;
}

static struct rtc_class_ops stmp3xxx_rtc_ops = {
	.alarm_irq_enable =
			  stmp3xxx_alarm_irq_enable,
	.read_time	= stmp3xxx_rtc_gettime,
	.set_mmss	= stmp3xxx_rtc_set_mmss,
	.read_alarm	= stmp3xxx_rtc_read_alarm,
	.set_alarm	= stmp3xxx_rtc_set_alarm,
};

static int stmp3xxx_rtc_remove(struct platform_device *pdev)
{
	struct stmp3xxx_rtc_data *rtc_data = platform_get_drvdata(pdev);

	if (!rtc_data)
		return 0;

	writel(STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
		rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_CLR);

	return 0;
}

static int stmp3xxx_rtc_probe(struct platform_device *pdev)
{
	struct stmp3xxx_rtc_data *rtc_data;
	struct resource *r;
	u32 rtc_stat;
	u32 pers0_set, pers0_clr;
	u32 crystalfreq = 0;
	int err;

	rtc_data = devm_kzalloc(&pdev->dev, sizeof(*rtc_data), GFP_KERNEL);
	if (!rtc_data)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "failed to get resource\n");
		return -ENXIO;
	}

	rtc_data->io = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!rtc_data->io) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -EIO;
	}

	rtc_data->irq_alarm = platform_get_irq(pdev, 0);

	rtc_stat = readl(rtc_data->io + STMP3XXX_RTC_STAT);
	if (!(rtc_stat & STMP3XXX_RTC_STAT_RTC_PRESENT)) {
		dev_err(&pdev->dev, "no device onboard\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, rtc_data);

	err = stmp_reset_block(rtc_data->io);
	if (err) {
		dev_err(&pdev->dev, "stmp_reset_block failed: %d\n", err);
		return err;
	}

	/*
	 * Obviously the rtc needs a clock input to be able to run.
	 * This clock can be provided by an external 32k crystal. If that one is
	 * missing XTAL must not be disabled in suspend which consumes a
	 * lot of power. Normally the presence and exact frequency (supported
	 * are 32000 Hz and 32768 Hz) is detectable from fuses, but as reality
	 * proves these fuses are not blown correctly on all machines, so the
	 * frequency can be overridden in the device tree.
	 */
	if (rtc_stat & STMP3XXX_RTC_STAT_XTAL32000_PRESENT)
		crystalfreq = 32000;
	else if (rtc_stat & STMP3XXX_RTC_STAT_XTAL32768_PRESENT)
		crystalfreq = 32768;

	of_property_read_u32(pdev->dev.of_node, "stmp,crystal-freq",
			     &crystalfreq);

	switch (crystalfreq) {
	case 32000:
		/* keep 32kHz crystal running in low-power mode */
		pers0_set = STMP3XXX_RTC_PERSISTENT0_XTAL32_FREQ |
			STMP3XXX_RTC_PERSISTENT0_XTAL32KHZ_PWRUP |
			STMP3XXX_RTC_PERSISTENT0_CLOCKSOURCE;
		pers0_clr = STMP3XXX_RTC_PERSISTENT0_XTAL24MHZ_PWRUP;
		break;
	case 32768:
		/* keep 32.768kHz crystal running in low-power mode */
		pers0_set = STMP3XXX_RTC_PERSISTENT0_XTAL32KHZ_PWRUP |
			STMP3XXX_RTC_PERSISTENT0_CLOCKSOURCE;
		pers0_clr = STMP3XXX_RTC_PERSISTENT0_XTAL24MHZ_PWRUP |
			STMP3XXX_RTC_PERSISTENT0_XTAL32_FREQ;
		break;
	default:
		dev_warn(&pdev->dev,
			 "invalid crystal-freq specified in device-tree. Assuming no crystal\n");
		/* fall-through */
	case 0:
		/* keep XTAL on in low-power mode */
		pers0_set = STMP3XXX_RTC_PERSISTENT0_XTAL24MHZ_PWRUP;
		pers0_clr = STMP3XXX_RTC_PERSISTENT0_XTAL32KHZ_PWRUP |
			STMP3XXX_RTC_PERSISTENT0_CLOCKSOURCE;
	}

	writel(pers0_set, rtc_data->io + STMP3XXX_RTC_PERSISTENT0 +
			STMP_OFFSET_REG_SET);

	writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE | pers0_clr,
		rtc_data->io + STMP3XXX_RTC_PERSISTENT0 + STMP_OFFSET_REG_CLR);

	writel(STMP3XXX_RTC_CTRL_ONEMSEC_IRQ_EN |
			STMP3XXX_RTC_CTRL_ALARM_IRQ_EN,
		rtc_data->io + STMP3XXX_RTC_CTRL + STMP_OFFSET_REG_CLR);

	rtc_data->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				&stmp3xxx_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc_data->rtc))
		return PTR_ERR(rtc_data->rtc);

	err = devm_request_irq(&pdev->dev, rtc_data->irq_alarm,
			stmp3xxx_rtc_interrupt, 0, "RTC alarm", &pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot claim IRQ%d\n",
			rtc_data->irq_alarm);
		return err;
	}

	stmp3xxx_wdt_register(pdev);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stmp3xxx_rtc_suspend(struct device *dev)
{
	return 0;
}

static int stmp3xxx_rtc_resume(struct device *dev)
{
	struct stmp3xxx_rtc_data *rtc_data = dev_get_drvdata(dev);

	stmp_reset_block(rtc_data->io);
	writel(STMP3XXX_RTC_PERSISTENT0_ALARM_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE_EN |
			STMP3XXX_RTC_PERSISTENT0_ALARM_WAKE,
		rtc_data->io + STMP3XXX_RTC_PERSISTENT0 + STMP_OFFSET_REG_CLR);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(stmp3xxx_rtc_pm_ops, stmp3xxx_rtc_suspend,
			stmp3xxx_rtc_resume);

static const struct of_device_id rtc_dt_ids[] = {
	{ .compatible = "fsl,stmp3xxx-rtc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtc_dt_ids);

static struct platform_driver stmp3xxx_rtcdrv = {
	.probe		= stmp3xxx_rtc_probe,
	.remove		= stmp3xxx_rtc_remove,
	.driver		= {
		.name	= "stmp3xxx-rtc",
		.pm	= &stmp3xxx_rtc_pm_ops,
		.of_match_table = rtc_dt_ids,
	},
};

module_platform_driver(stmp3xxx_rtcdrv);

MODULE_DESCRIPTION("STMP3xxx RTC Driver");
MODULE_AUTHOR("dmitry pervushin <dpervushin@embeddedalley.com> and "
		"Wolfram Sang <w.sang@pengutronix.de>");
MODULE_LICENSE("GPL");
