// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DaVinci Power Management and Real Time Clock Driver for TI platforms
 *
 * Copyright (C) 2009 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>

/*
 * The DaVinci RTC is a simple RTC with the following
 * Sec: 0 - 59 : BCD count
 * Min: 0 - 59 : BCD count
 * Hour: 0 - 23 : BCD count
 * Day: 0 - 0x7FFF(32767) : Binary count ( Over 89 years )
 */

/* PRTC interface registers */
#define DAVINCI_PRTCIF_PID		0x00
#define PRTCIF_CTLR			0x04
#define PRTCIF_LDATA			0x08
#define PRTCIF_UDATA			0x0C
#define PRTCIF_INTEN			0x10
#define PRTCIF_INTFLG			0x14

/* PRTCIF_CTLR bit fields */
#define PRTCIF_CTLR_BUSY		BIT(31)
#define PRTCIF_CTLR_SIZE		BIT(25)
#define PRTCIF_CTLR_DIR			BIT(24)
#define PRTCIF_CTLR_BENU_MSB		BIT(23)
#define PRTCIF_CTLR_BENU_3RD_BYTE	BIT(22)
#define PRTCIF_CTLR_BENU_2ND_BYTE	BIT(21)
#define PRTCIF_CTLR_BENU_LSB		BIT(20)
#define PRTCIF_CTLR_BENU_MASK		(0x00F00000)
#define PRTCIF_CTLR_BENL_MSB		BIT(19)
#define PRTCIF_CTLR_BENL_3RD_BYTE	BIT(18)
#define PRTCIF_CTLR_BENL_2ND_BYTE	BIT(17)
#define PRTCIF_CTLR_BENL_LSB		BIT(16)
#define PRTCIF_CTLR_BENL_MASK		(0x000F0000)

/* PRTCIF_INTEN bit fields */
#define PRTCIF_INTEN_RTCSS		BIT(1)
#define PRTCIF_INTEN_RTCIF		BIT(0)
#define PRTCIF_INTEN_MASK		(PRTCIF_INTEN_RTCSS \
					| PRTCIF_INTEN_RTCIF)

/* PRTCIF_INTFLG bit fields */
#define PRTCIF_INTFLG_RTCSS		BIT(1)
#define PRTCIF_INTFLG_RTCIF		BIT(0)
#define PRTCIF_INTFLG_MASK		(PRTCIF_INTFLG_RTCSS \
					| PRTCIF_INTFLG_RTCIF)

/* PRTC subsystem registers */
#define PRTCSS_RTC_INTC_EXTENA1		(0x0C)
#define PRTCSS_RTC_CTRL			(0x10)
#define PRTCSS_RTC_WDT			(0x11)
#define PRTCSS_RTC_TMR0			(0x12)
#define PRTCSS_RTC_TMR1			(0x13)
#define PRTCSS_RTC_CCTRL		(0x14)
#define PRTCSS_RTC_SEC			(0x15)
#define PRTCSS_RTC_MIN			(0x16)
#define PRTCSS_RTC_HOUR			(0x17)
#define PRTCSS_RTC_DAY0			(0x18)
#define PRTCSS_RTC_DAY1			(0x19)
#define PRTCSS_RTC_AMIN			(0x1A)
#define PRTCSS_RTC_AHOUR		(0x1B)
#define PRTCSS_RTC_ADAY0		(0x1C)
#define PRTCSS_RTC_ADAY1		(0x1D)
#define PRTCSS_RTC_CLKC_CNT		(0x20)

/* PRTCSS_RTC_INTC_EXTENA1 */
#define PRTCSS_RTC_INTC_EXTENA1_MASK	(0x07)

/* PRTCSS_RTC_CTRL bit fields */
#define PRTCSS_RTC_CTRL_WDTBUS		BIT(7)
#define PRTCSS_RTC_CTRL_WEN		BIT(6)
#define PRTCSS_RTC_CTRL_WDRT		BIT(5)
#define PRTCSS_RTC_CTRL_WDTFLG		BIT(4)
#define PRTCSS_RTC_CTRL_TE		BIT(3)
#define PRTCSS_RTC_CTRL_TIEN		BIT(2)
#define PRTCSS_RTC_CTRL_TMRFLG		BIT(1)
#define PRTCSS_RTC_CTRL_TMMD		BIT(0)

/* PRTCSS_RTC_CCTRL bit fields */
#define PRTCSS_RTC_CCTRL_CALBUSY	BIT(7)
#define PRTCSS_RTC_CCTRL_DAEN		BIT(5)
#define PRTCSS_RTC_CCTRL_HAEN		BIT(4)
#define PRTCSS_RTC_CCTRL_MAEN		BIT(3)
#define PRTCSS_RTC_CCTRL_ALMFLG		BIT(2)
#define PRTCSS_RTC_CCTRL_AIEN		BIT(1)
#define PRTCSS_RTC_CCTRL_CAEN		BIT(0)

static DEFINE_SPINLOCK(davinci_rtc_lock);

struct davinci_rtc {
	struct rtc_device		*rtc;
	void __iomem			*base;
	int				irq;
};

static inline void rtcif_write(struct davinci_rtc *davinci_rtc,
			       u32 val, u32 addr)
{
	writel(val, davinci_rtc->base + addr);
}

static inline u32 rtcif_read(struct davinci_rtc *davinci_rtc, u32 addr)
{
	return readl(davinci_rtc->base + addr);
}

static inline void rtcif_wait(struct davinci_rtc *davinci_rtc)
{
	while (rtcif_read(davinci_rtc, PRTCIF_CTLR) & PRTCIF_CTLR_BUSY)
		cpu_relax();
}

static inline void rtcss_write(struct davinci_rtc *davinci_rtc,
			       unsigned long val, u8 addr)
{
	rtcif_wait(davinci_rtc);

	rtcif_write(davinci_rtc, PRTCIF_CTLR_BENL_LSB | addr, PRTCIF_CTLR);
	rtcif_write(davinci_rtc, val, PRTCIF_LDATA);

	rtcif_wait(davinci_rtc);
}

static inline u8 rtcss_read(struct davinci_rtc *davinci_rtc, u8 addr)
{
	rtcif_wait(davinci_rtc);

	rtcif_write(davinci_rtc, PRTCIF_CTLR_DIR | PRTCIF_CTLR_BENL_LSB | addr,
		    PRTCIF_CTLR);

	rtcif_wait(davinci_rtc);

	return rtcif_read(davinci_rtc, PRTCIF_LDATA);
}

static inline void davinci_rtcss_calendar_wait(struct davinci_rtc *davinci_rtc)
{
	while (rtcss_read(davinci_rtc, PRTCSS_RTC_CCTRL) &
	       PRTCSS_RTC_CCTRL_CALBUSY)
		cpu_relax();
}

static irqreturn_t davinci_rtc_interrupt(int irq, void *class_dev)
{
	struct davinci_rtc *davinci_rtc = class_dev;
	unsigned long events = 0;
	u32 irq_flg;
	u8 alm_irq, tmr_irq;
	u8 rtc_ctrl, rtc_cctrl;
	int ret = IRQ_NONE;

	irq_flg = rtcif_read(davinci_rtc, PRTCIF_INTFLG) &
		  PRTCIF_INTFLG_RTCSS;

	alm_irq = rtcss_read(davinci_rtc, PRTCSS_RTC_CCTRL) &
		  PRTCSS_RTC_CCTRL_ALMFLG;

	tmr_irq = rtcss_read(davinci_rtc, PRTCSS_RTC_CTRL) &
		  PRTCSS_RTC_CTRL_TMRFLG;

	if (irq_flg) {
		if (alm_irq) {
			events |= RTC_IRQF | RTC_AF;
			rtc_cctrl = rtcss_read(davinci_rtc, PRTCSS_RTC_CCTRL);
			rtc_cctrl |=  PRTCSS_RTC_CCTRL_ALMFLG;
			rtcss_write(davinci_rtc, rtc_cctrl, PRTCSS_RTC_CCTRL);
		} else if (tmr_irq) {
			events |= RTC_IRQF | RTC_PF;
			rtc_ctrl = rtcss_read(davinci_rtc, PRTCSS_RTC_CTRL);
			rtc_ctrl |=  PRTCSS_RTC_CTRL_TMRFLG;
			rtcss_write(davinci_rtc, rtc_ctrl, PRTCSS_RTC_CTRL);
		}

		rtcif_write(davinci_rtc, PRTCIF_INTFLG_RTCSS,
				    PRTCIF_INTFLG);
		rtc_update_irq(davinci_rtc->rtc, 1, events);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static int
davinci_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	u8 rtc_ctrl;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	rtc_ctrl = rtcss_read(davinci_rtc, PRTCSS_RTC_CTRL);

	switch (cmd) {
	case RTC_WIE_ON:
		rtc_ctrl |= PRTCSS_RTC_CTRL_WEN | PRTCSS_RTC_CTRL_WDTFLG;
		break;
	case RTC_WIE_OFF:
		rtc_ctrl &= ~PRTCSS_RTC_CTRL_WEN;
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	rtcss_write(davinci_rtc, rtc_ctrl, PRTCSS_RTC_CTRL);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);

	return ret;
}

static int convertfromdays(u16 days, struct rtc_time *tm)
{
	int tmp_days, year, mon;

	for (year = 2000;; year++) {
		tmp_days = rtc_year_days(1, 12, year);
		if (days >= tmp_days)
			days -= tmp_days;
		else {
			for (mon = 0;; mon++) {
				tmp_days = rtc_month_days(mon, year);
				if (days >= tmp_days) {
					days -= tmp_days;
				} else {
					tm->tm_year = year - 1900;
					tm->tm_mon = mon;
					tm->tm_mday = days + 1;
					break;
				}
			}
			break;
		}
	}
	return 0;
}

static int convert2days(u16 *days, struct rtc_time *tm)
{
	int i;
	*days = 0;

	/* epoch == 1900 */
	if (tm->tm_year < 100 || tm->tm_year > 199)
		return -EINVAL;

	for (i = 2000; i < 1900 + tm->tm_year; i++)
		*days += rtc_year_days(1, 12, i);

	*days += rtc_year_days(tm->tm_mday, tm->tm_mon, 1900 + tm->tm_year);

	return 0;
}

static int davinci_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	u16 days = 0;
	u8 day0, day1;
	unsigned long flags;

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	davinci_rtcss_calendar_wait(davinci_rtc);
	tm->tm_sec = bcd2bin(rtcss_read(davinci_rtc, PRTCSS_RTC_SEC));

	davinci_rtcss_calendar_wait(davinci_rtc);
	tm->tm_min = bcd2bin(rtcss_read(davinci_rtc, PRTCSS_RTC_MIN));

	davinci_rtcss_calendar_wait(davinci_rtc);
	tm->tm_hour = bcd2bin(rtcss_read(davinci_rtc, PRTCSS_RTC_HOUR));

	davinci_rtcss_calendar_wait(davinci_rtc);
	day0 = rtcss_read(davinci_rtc, PRTCSS_RTC_DAY0);

	davinci_rtcss_calendar_wait(davinci_rtc);
	day1 = rtcss_read(davinci_rtc, PRTCSS_RTC_DAY1);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);

	days |= day1;
	days <<= 8;
	days |= day0;

	if (convertfromdays(days, tm) < 0)
		return -EINVAL;

	return 0;
}

static int davinci_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	u16 days;
	u8 rtc_cctrl;
	unsigned long flags;

	if (convert2days(&days, tm) < 0)
		return -EINVAL;

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, bin2bcd(tm->tm_sec), PRTCSS_RTC_SEC);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, bin2bcd(tm->tm_min), PRTCSS_RTC_MIN);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, bin2bcd(tm->tm_hour), PRTCSS_RTC_HOUR);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, days & 0xFF, PRTCSS_RTC_DAY0);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, (days & 0xFF00) >> 8, PRTCSS_RTC_DAY1);

	rtc_cctrl = rtcss_read(davinci_rtc, PRTCSS_RTC_CCTRL);
	rtc_cctrl |= PRTCSS_RTC_CCTRL_CAEN;
	rtcss_write(davinci_rtc, rtc_cctrl, PRTCSS_RTC_CCTRL);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);

	return 0;
}

static int davinci_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	unsigned long flags;
	u8 rtc_cctrl = rtcss_read(davinci_rtc, PRTCSS_RTC_CCTRL);

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	if (enabled)
		rtc_cctrl |= PRTCSS_RTC_CCTRL_DAEN |
			     PRTCSS_RTC_CCTRL_HAEN |
			     PRTCSS_RTC_CCTRL_MAEN |
			     PRTCSS_RTC_CCTRL_ALMFLG |
			     PRTCSS_RTC_CCTRL_AIEN;
	else
		rtc_cctrl &= ~PRTCSS_RTC_CCTRL_AIEN;

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, rtc_cctrl, PRTCSS_RTC_CCTRL);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);

	return 0;
}

static int davinci_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	u16 days = 0;
	u8 day0, day1;
	unsigned long flags;

	alm->time.tm_sec = 0;

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	davinci_rtcss_calendar_wait(davinci_rtc);
	alm->time.tm_min = bcd2bin(rtcss_read(davinci_rtc, PRTCSS_RTC_AMIN));

	davinci_rtcss_calendar_wait(davinci_rtc);
	alm->time.tm_hour = bcd2bin(rtcss_read(davinci_rtc, PRTCSS_RTC_AHOUR));

	davinci_rtcss_calendar_wait(davinci_rtc);
	day0 = rtcss_read(davinci_rtc, PRTCSS_RTC_ADAY0);

	davinci_rtcss_calendar_wait(davinci_rtc);
	day1 = rtcss_read(davinci_rtc, PRTCSS_RTC_ADAY1);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);
	days |= day1;
	days <<= 8;
	days |= day0;

	if (convertfromdays(days, &alm->time) < 0)
		return -EINVAL;

	alm->pending = !!(rtcss_read(davinci_rtc,
			  PRTCSS_RTC_CCTRL) &
			PRTCSS_RTC_CCTRL_AIEN);
	alm->enabled = alm->pending && device_may_wakeup(dev);

	return 0;
}

static int davinci_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct davinci_rtc *davinci_rtc = dev_get_drvdata(dev);
	unsigned long flags;
	u16 days;

	if (alm->time.tm_mday <= 0 && alm->time.tm_mon < 0
	    && alm->time.tm_year < 0) {
		struct rtc_time tm;
		unsigned long now, then;

		davinci_rtc_read_time(dev, &tm);
		rtc_tm_to_time(&tm, &now);

		alm->time.tm_mday = tm.tm_mday;
		alm->time.tm_mon = tm.tm_mon;
		alm->time.tm_year = tm.tm_year;
		rtc_tm_to_time(&alm->time, &then);

		if (then < now) {
			rtc_time_to_tm(now + 24 * 60 * 60, &tm);
			alm->time.tm_mday = tm.tm_mday;
			alm->time.tm_mon = tm.tm_mon;
			alm->time.tm_year = tm.tm_year;
		}
	}

	if (convert2days(&days, &alm->time) < 0)
		return -EINVAL;

	spin_lock_irqsave(&davinci_rtc_lock, flags);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, bin2bcd(alm->time.tm_min), PRTCSS_RTC_AMIN);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, bin2bcd(alm->time.tm_hour), PRTCSS_RTC_AHOUR);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, days & 0xFF, PRTCSS_RTC_ADAY0);

	davinci_rtcss_calendar_wait(davinci_rtc);
	rtcss_write(davinci_rtc, (days & 0xFF00) >> 8, PRTCSS_RTC_ADAY1);

	spin_unlock_irqrestore(&davinci_rtc_lock, flags);

	return 0;
}

static const struct rtc_class_ops davinci_rtc_ops = {
	.ioctl			= davinci_rtc_ioctl,
	.read_time		= davinci_rtc_read_time,
	.set_time		= davinci_rtc_set_time,
	.alarm_irq_enable	= davinci_rtc_alarm_irq_enable,
	.read_alarm		= davinci_rtc_read_alarm,
	.set_alarm		= davinci_rtc_set_alarm,
};

static int __init davinci_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct davinci_rtc *davinci_rtc;
	int ret = 0;

	davinci_rtc = devm_kzalloc(&pdev->dev, sizeof(struct davinci_rtc), GFP_KERNEL);
	if (!davinci_rtc)
		return -ENOMEM;

	davinci_rtc->irq = platform_get_irq(pdev, 0);
	if (davinci_rtc->irq < 0)
		return davinci_rtc->irq;

	davinci_rtc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(davinci_rtc->base))
		return PTR_ERR(davinci_rtc->base);

	platform_set_drvdata(pdev, davinci_rtc);

	davinci_rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				    &davinci_rtc_ops, THIS_MODULE);
	if (IS_ERR(davinci_rtc->rtc)) {
		dev_err(dev, "unable to register RTC device, err %d\n",
				ret);
		return PTR_ERR(davinci_rtc->rtc);
	}

	rtcif_write(davinci_rtc, PRTCIF_INTFLG_RTCSS, PRTCIF_INTFLG);
	rtcif_write(davinci_rtc, 0, PRTCIF_INTEN);
	rtcss_write(davinci_rtc, 0, PRTCSS_RTC_INTC_EXTENA1);

	rtcss_write(davinci_rtc, 0, PRTCSS_RTC_CTRL);
	rtcss_write(davinci_rtc, 0, PRTCSS_RTC_CCTRL);

	ret = devm_request_irq(dev, davinci_rtc->irq, davinci_rtc_interrupt,
			  0, "davinci_rtc", davinci_rtc);
	if (ret < 0) {
		dev_err(dev, "unable to register davinci RTC interrupt\n");
		return ret;
	}

	/* Enable interrupts */
	rtcif_write(davinci_rtc, PRTCIF_INTEN_RTCSS, PRTCIF_INTEN);
	rtcss_write(davinci_rtc, PRTCSS_RTC_INTC_EXTENA1_MASK,
			    PRTCSS_RTC_INTC_EXTENA1);

	rtcss_write(davinci_rtc, PRTCSS_RTC_CCTRL_CAEN, PRTCSS_RTC_CCTRL);

	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

static int __exit davinci_rtc_remove(struct platform_device *pdev)
{
	struct davinci_rtc *davinci_rtc = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);

	rtcif_write(davinci_rtc, 0, PRTCIF_INTEN);

	return 0;
}

static struct platform_driver davinci_rtc_driver = {
	.remove		= __exit_p(davinci_rtc_remove),
	.driver		= {
		.name = "rtc_davinci",
	},
};

module_platform_driver_probe(davinci_rtc_driver, davinci_rtc_probe);

MODULE_AUTHOR("Miguel Aguilar <miguel.aguilar@ridgerun.com>");
MODULE_DESCRIPTION("Texas Instruments DaVinci PRTC Driver");
MODULE_LICENSE("GPL");
