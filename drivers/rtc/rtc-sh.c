/*
 * SuperH On-Chip RTC Support
 *
 * Copyright (C) 2006, 2007, 2008  Paul Mundt
 * Copyright (C) 2006  Jamie Lenehan
 * Copyright (C) 2008  Angelo Castello
 *
 * Based on the old arch/sh/kernel/cpu/rtc.c by:
 *
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <asm/rtc.h>

#define DRV_NAME	"sh-rtc"
#define DRV_VERSION	"0.2.0"

#define RTC_REG(r)	((r) * rtc_reg_size)

#define R64CNT		RTC_REG(0)

#define RSECCNT		RTC_REG(1)	/* RTC sec */
#define RMINCNT		RTC_REG(2)	/* RTC min */
#define RHRCNT		RTC_REG(3)	/* RTC hour */
#define RWKCNT		RTC_REG(4)	/* RTC week */
#define RDAYCNT		RTC_REG(5)	/* RTC day */
#define RMONCNT		RTC_REG(6)	/* RTC month */
#define RYRCNT		RTC_REG(7)	/* RTC year */
#define RSECAR		RTC_REG(8)	/* ALARM sec */
#define RMINAR		RTC_REG(9)	/* ALARM min */
#define RHRAR		RTC_REG(10)	/* ALARM hour */
#define RWKAR		RTC_REG(11)	/* ALARM week */
#define RDAYAR		RTC_REG(12)	/* ALARM day */
#define RMONAR		RTC_REG(13)	/* ALARM month */
#define RCR1		RTC_REG(14)	/* Control */
#define RCR2		RTC_REG(15)	/* Control */

/*
 * Note on RYRAR and RCR3: Up until this point most of the register
 * definitions are consistent across all of the available parts. However,
 * the placement of the optional RYRAR and RCR3 (the RYRAR control
 * register used to control RYRCNT/RYRAR compare) varies considerably
 * across various parts, occasionally being mapped in to a completely
 * unrelated address space. For proper RYRAR support a separate resource
 * would have to be handed off, but as this is purely optional in
 * practice, we simply opt not to support it, thereby keeping the code
 * quite a bit more simplified.
 */

/* ALARM Bits - or with BCD encoded value */
#define AR_ENB		0x80	/* Enable for alarm cmp   */

/* Period Bits */
#define PF_HP		0x100	/* Enable Half Period to support 8,32,128Hz */
#define PF_COUNT	0x200	/* Half periodic counter */
#define PF_OXS		0x400	/* Periodic One x Second */
#define PF_KOU		0x800	/* Kernel or User periodic request 1=kernel */
#define PF_MASK		0xf00

/* RCR1 Bits */
#define RCR1_CF		0x80	/* Carry Flag             */
#define RCR1_CIE	0x10	/* Carry Interrupt Enable */
#define RCR1_AIE	0x08	/* Alarm Interrupt Enable */
#define RCR1_AF		0x01	/* Alarm Flag             */

/* RCR2 Bits */
#define RCR2_PEF	0x80	/* PEriodic interrupt Flag */
#define RCR2_PESMASK	0x70	/* Periodic interrupt Set  */
#define RCR2_RTCEN	0x08	/* ENable RTC              */
#define RCR2_ADJ	0x04	/* ADJustment (30-second)  */
#define RCR2_RESET	0x02	/* Reset bit               */
#define RCR2_START	0x01	/* Start bit               */

struct sh_rtc {
	void __iomem *regbase;
	unsigned long regsize;
	struct resource *res;
	unsigned int alarm_irq, periodic_irq, carry_irq;
	struct rtc_device *rtc_dev;
	spinlock_t lock;
	unsigned long capabilities;	/* See asm-sh/rtc.h for cap bits */
	unsigned short periodic_freq;
};

static irqreturn_t sh_rtc_interrupt(int irq, void *dev_id)
{
	struct sh_rtc *rtc = dev_id;
	unsigned int tmp;

	spin_lock(&rtc->lock);

	tmp = readb(rtc->regbase + RCR1);
	tmp &= ~RCR1_CF;
	writeb(tmp, rtc->regbase + RCR1);

	/* Users have requested One x Second IRQ */
	if (rtc->periodic_freq & PF_OXS)
		rtc_update_irq(rtc->rtc_dev, 1, RTC_UF | RTC_IRQF);

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t sh_rtc_alarm(int irq, void *dev_id)
{
	struct sh_rtc *rtc = dev_id;
	unsigned int tmp;

	spin_lock(&rtc->lock);

	tmp = readb(rtc->regbase + RCR1);
	tmp &= ~(RCR1_AF | RCR1_AIE);
		writeb(tmp, rtc->regbase + RCR1);

	rtc_update_irq(rtc->rtc_dev, 1, RTC_AF | RTC_IRQF);

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t sh_rtc_periodic(int irq, void *dev_id)
{
	struct sh_rtc *rtc = dev_id;
	struct rtc_device *rtc_dev = rtc->rtc_dev;
	unsigned int tmp;

	spin_lock(&rtc->lock);

	tmp = readb(rtc->regbase + RCR2);
	tmp &= ~RCR2_PEF;
	writeb(tmp, rtc->regbase + RCR2);

	/* Half period enabled than one skipped and the next notified */
	if ((rtc->periodic_freq & PF_HP) && (rtc->periodic_freq & PF_COUNT))
		rtc->periodic_freq &= ~PF_COUNT;
	else {
		if (rtc->periodic_freq & PF_HP)
			rtc->periodic_freq |= PF_COUNT;
		if (rtc->periodic_freq & PF_KOU) {
			spin_lock(&rtc_dev->irq_task_lock);
			if (rtc_dev->irq_task)
				rtc_dev->irq_task->func(rtc_dev->irq_task->private_data);
			spin_unlock(&rtc_dev->irq_task_lock);
		} else
			rtc_update_irq(rtc->rtc_dev, 1, RTC_PF | RTC_IRQF);
	}

	spin_unlock(&rtc->lock);

	return IRQ_HANDLED;
}

static inline void sh_rtc_setpie(struct device *dev, unsigned int enable)
{
	struct sh_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	spin_lock_irq(&rtc->lock);

	tmp = readb(rtc->regbase + RCR2);

	if (enable) {
		tmp &= ~RCR2_PEF;	/* Clear PES bit */
		tmp |= (rtc->periodic_freq & ~PF_HP);	/* Set PES2-0 */
	} else
		tmp &= ~(RCR2_PESMASK | RCR2_PEF);

	writeb(tmp, rtc->regbase + RCR2);

	spin_unlock_irq(&rtc->lock);
}

static inline int sh_rtc_setfreq(struct device *dev, unsigned int freq)
{
	struct sh_rtc *rtc = dev_get_drvdata(dev);
	int tmp, ret = 0;

	spin_lock_irq(&rtc->lock);
	tmp = rtc->periodic_freq & PF_MASK;

	switch (freq) {
	case 0:
		rtc->periodic_freq = 0x00;
		break;
	case 1:
		rtc->periodic_freq = 0x60;
		break;
	case 2:
		rtc->periodic_freq = 0x50;
		break;
	case 4:
		rtc->periodic_freq = 0x40;
		break;
	case 8:
		rtc->periodic_freq = 0x30 | PF_HP;
		break;
	case 16:
		rtc->periodic_freq = 0x30;
		break;
	case 32:
		rtc->periodic_freq = 0x20 | PF_HP;
		break;
	case 64:
		rtc->periodic_freq = 0x20;
		break;
	case 128:
		rtc->periodic_freq = 0x10 | PF_HP;
		break;
	case 256:
		rtc->periodic_freq = 0x10;
		break;
	default:
		ret = -ENOTSUPP;
	}

	if (ret == 0) {
		rtc->periodic_freq |= tmp;
		rtc->rtc_dev->irq_freq = freq;
	}

	spin_unlock_irq(&rtc->lock);
	return ret;
}

static inline void sh_rtc_setaie(struct device *dev, unsigned int enable)
{
	struct sh_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	spin_lock_irq(&rtc->lock);

	tmp = readb(rtc->regbase + RCR1);

	if (!enable)
		tmp &= ~RCR1_AIE;
	else
		tmp |= RCR1_AIE;

	writeb(tmp, rtc->regbase + RCR1);

	spin_unlock_irq(&rtc->lock);
}

static void sh_rtc_release(struct device *dev)
{
	sh_rtc_setpie(dev, 0);
	sh_rtc_setaie(dev, 0);
}

static int sh_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct sh_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tmp;

	tmp = readb(rtc->regbase + RCR1);
	seq_printf(seq, "carry_IRQ\t: %s\n", (tmp & RCR1_CIE) ? "yes" : "no");

	tmp = readb(rtc->regbase + RCR2);
	seq_printf(seq, "periodic_IRQ\t: %s\n",
		   (tmp & RCR2_PESMASK) ? "yes" : "no");

	return 0;
}

static int sh_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct sh_rtc *rtc = dev_get_drvdata(dev);
	unsigned int ret = 0;

	switch (cmd) {
	case RTC_PIE_OFF:
	case RTC_PIE_ON:
		sh_rtc_setpie(dev, cmd == RTC_PIE_ON);
		break;
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
		sh_rtc_setaie(dev, cmd == RTC_AIE_ON);
		break;
	case RTC_UIE_OFF:
		rtc->periodic_freq &= ~PF_OXS;
		break;
	case RTC_UIE_ON:
		rtc->periodic_freq |= PF_OXS;
		break;
	case RTC_IRQP_READ:
		ret = put_user(rtc->rtc_dev->irq_freq,
			       (unsigned long __user *)arg);
		break;
	case RTC_IRQP_SET:
		ret = sh_rtc_setfreq(dev, arg);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	return ret;
}

static int sh_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_rtc *rtc = platform_get_drvdata(pdev);
	unsigned int sec128, sec2, yr, yr100, cf_bit;

	do {
		unsigned int tmp;

		spin_lock_irq(&rtc->lock);

		tmp = readb(rtc->regbase + RCR1);
		tmp &= ~RCR1_CF; /* Clear CF-bit */
		tmp |= RCR1_CIE;
		writeb(tmp, rtc->regbase + RCR1);

		sec128 = readb(rtc->regbase + R64CNT);

		tm->tm_sec	= BCD2BIN(readb(rtc->regbase + RSECCNT));
		tm->tm_min	= BCD2BIN(readb(rtc->regbase + RMINCNT));
		tm->tm_hour	= BCD2BIN(readb(rtc->regbase + RHRCNT));
		tm->tm_wday	= BCD2BIN(readb(rtc->regbase + RWKCNT));
		tm->tm_mday	= BCD2BIN(readb(rtc->regbase + RDAYCNT));
		tm->tm_mon	= BCD2BIN(readb(rtc->regbase + RMONCNT)) - 1;

		if (rtc->capabilities & RTC_CAP_4_DIGIT_YEAR) {
			yr  = readw(rtc->regbase + RYRCNT);
			yr100 = BCD2BIN(yr >> 8);
			yr &= 0xff;
		} else {
			yr  = readb(rtc->regbase + RYRCNT);
			yr100 = BCD2BIN((yr == 0x99) ? 0x19 : 0x20);
		}

		tm->tm_year = (yr100 * 100 + BCD2BIN(yr)) - 1900;

		sec2 = readb(rtc->regbase + R64CNT);
		cf_bit = readb(rtc->regbase + RCR1) & RCR1_CF;

		spin_unlock_irq(&rtc->lock);
	} while (cf_bit != 0 || ((sec128 ^ sec2) & RTC_BIT_INVERTED) != 0);

#if RTC_BIT_INVERTED != 0
	if ((sec128 & RTC_BIT_INVERTED))
		tm->tm_sec--;
#endif

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year, tm->tm_wday);

	if (rtc_valid_tm(tm) < 0) {
		dev_err(dev, "invalid date\n");
		rtc_time_to_tm(0, tm);
	}

	return 0;
}

static int sh_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_rtc *rtc = platform_get_drvdata(pdev);
	unsigned int tmp;
	int year;

	spin_lock_irq(&rtc->lock);

	/* Reset pre-scaler & stop RTC */
	tmp = readb(rtc->regbase + RCR2);
	tmp |= RCR2_RESET;
	tmp &= ~RCR2_START;
	writeb(tmp, rtc->regbase + RCR2);

	writeb(BIN2BCD(tm->tm_sec),  rtc->regbase + RSECCNT);
	writeb(BIN2BCD(tm->tm_min),  rtc->regbase + RMINCNT);
	writeb(BIN2BCD(tm->tm_hour), rtc->regbase + RHRCNT);
	writeb(BIN2BCD(tm->tm_wday), rtc->regbase + RWKCNT);
	writeb(BIN2BCD(tm->tm_mday), rtc->regbase + RDAYCNT);
	writeb(BIN2BCD(tm->tm_mon + 1), rtc->regbase + RMONCNT);

	if (rtc->capabilities & RTC_CAP_4_DIGIT_YEAR) {
		year = (BIN2BCD((tm->tm_year + 1900) / 100) << 8) |
			BIN2BCD(tm->tm_year % 100);
		writew(year, rtc->regbase + RYRCNT);
	} else {
		year = tm->tm_year % 100;
		writeb(BIN2BCD(year), rtc->regbase + RYRCNT);
	}

	/* Start RTC */
	tmp = readb(rtc->regbase + RCR2);
	tmp &= ~RCR2_RESET;
	tmp |= RCR2_RTCEN | RCR2_START;
	writeb(tmp, rtc->regbase + RCR2);

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static inline int sh_rtc_read_alarm_value(struct sh_rtc *rtc, int reg_off)
{
	unsigned int byte;
	int value = 0xff;	/* return 0xff for ignored values */

	byte = readb(rtc->regbase + reg_off);
	if (byte & AR_ENB) {
		byte &= ~AR_ENB;	/* strip the enable bit */
		value = BCD2BIN(byte);
	}

	return value;
}

static int sh_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_rtc *rtc = platform_get_drvdata(pdev);
	struct rtc_time *tm = &wkalrm->time;

	spin_lock_irq(&rtc->lock);

	tm->tm_sec	= sh_rtc_read_alarm_value(rtc, RSECAR);
	tm->tm_min	= sh_rtc_read_alarm_value(rtc, RMINAR);
	tm->tm_hour	= sh_rtc_read_alarm_value(rtc, RHRAR);
	tm->tm_wday	= sh_rtc_read_alarm_value(rtc, RWKAR);
	tm->tm_mday	= sh_rtc_read_alarm_value(rtc, RDAYAR);
	tm->tm_mon	= sh_rtc_read_alarm_value(rtc, RMONAR);
	if (tm->tm_mon > 0)
		tm->tm_mon -= 1; /* RTC is 1-12, tm_mon is 0-11 */
	tm->tm_year     = 0xffff;

	wkalrm->enabled = (readb(rtc->regbase + RCR1) & RCR1_AIE) ? 1 : 0;

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static inline void sh_rtc_write_alarm_value(struct sh_rtc *rtc,
					    int value, int reg_off)
{
	/* < 0 for a value that is ignored */
	if (value < 0)
		writeb(0, rtc->regbase + reg_off);
	else
		writeb(BIN2BCD(value) | AR_ENB,  rtc->regbase + reg_off);
}

static int sh_rtc_check_alarm(struct rtc_time *tm)
{
	/*
	 * The original rtc says anything > 0xc0 is "don't care" or "match
	 * all" - most users use 0xff but rtc-dev uses -1 for the same thing.
	 * The original rtc doesn't support years - some things use -1 and
	 * some 0xffff. We use -1 to make out tests easier.
	 */
	if (tm->tm_year == 0xffff)
		tm->tm_year = -1;
	if (tm->tm_mon >= 0xff)
		tm->tm_mon = -1;
	if (tm->tm_mday >= 0xff)
		tm->tm_mday = -1;
	if (tm->tm_wday >= 0xff)
		tm->tm_wday = -1;
	if (tm->tm_hour >= 0xff)
		tm->tm_hour = -1;
	if (tm->tm_min >= 0xff)
		tm->tm_min = -1;
	if (tm->tm_sec >= 0xff)
		tm->tm_sec = -1;

	if (tm->tm_year > 9999 ||
		tm->tm_mon >= 12 ||
		tm->tm_mday == 0 || tm->tm_mday >= 32 ||
		tm->tm_wday >= 7 ||
		tm->tm_hour >= 24 ||
		tm->tm_min >= 60 ||
		tm->tm_sec >= 60)
		return -EINVAL;

	return 0;
}

static int sh_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_rtc *rtc = platform_get_drvdata(pdev);
	unsigned int rcr1;
	struct rtc_time *tm = &wkalrm->time;
	int mon, err;

	err = sh_rtc_check_alarm(tm);
	if (unlikely(err < 0))
		return err;

	spin_lock_irq(&rtc->lock);

	/* disable alarm interrupt and clear the alarm flag */
	rcr1 = readb(rtc->regbase + RCR1);
	rcr1 &= ~(RCR1_AF | RCR1_AIE);
	writeb(rcr1, rtc->regbase + RCR1);

	/* set alarm time */
	sh_rtc_write_alarm_value(rtc, tm->tm_sec,  RSECAR);
	sh_rtc_write_alarm_value(rtc, tm->tm_min,  RMINAR);
	sh_rtc_write_alarm_value(rtc, tm->tm_hour, RHRAR);
	sh_rtc_write_alarm_value(rtc, tm->tm_wday, RWKAR);
	sh_rtc_write_alarm_value(rtc, tm->tm_mday, RDAYAR);
	mon = tm->tm_mon;
	if (mon >= 0)
		mon += 1;
	sh_rtc_write_alarm_value(rtc, mon, RMONAR);

	if (wkalrm->enabled) {
		rcr1 |= RCR1_AIE;
		writeb(rcr1, rtc->regbase + RCR1);
	}

	spin_unlock_irq(&rtc->lock);

	return 0;
}

static int sh_rtc_irq_set_state(struct device *dev, int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_rtc *rtc = platform_get_drvdata(pdev);

	if (enabled) {
		rtc->periodic_freq |= PF_KOU;
		return sh_rtc_ioctl(dev, RTC_PIE_ON, 0);
	} else {
		rtc->periodic_freq &= ~PF_KOU;
		return sh_rtc_ioctl(dev, RTC_PIE_OFF, 0);
	}
}

static int sh_rtc_irq_set_freq(struct device *dev, int freq)
{
	return sh_rtc_ioctl(dev, RTC_IRQP_SET, freq);
}

static struct rtc_class_ops sh_rtc_ops = {
	.release	= sh_rtc_release,
	.ioctl		= sh_rtc_ioctl,
	.read_time	= sh_rtc_read_time,
	.set_time	= sh_rtc_set_time,
	.read_alarm	= sh_rtc_read_alarm,
	.set_alarm	= sh_rtc_set_alarm,
	.irq_set_state	= sh_rtc_irq_set_state,
	.irq_set_freq	= sh_rtc_irq_set_freq,
	.proc		= sh_rtc_proc,
};

static int __devinit sh_rtc_probe(struct platform_device *pdev)
{
	struct sh_rtc *rtc;
	struct resource *res;
	unsigned int tmp;
	int ret = -ENOENT;

	rtc = kzalloc(sizeof(struct sh_rtc), GFP_KERNEL);
	if (unlikely(!rtc))
		return -ENOMEM;

	spin_lock_init(&rtc->lock);

	/* get periodic/carry/alarm irqs */
	rtc->periodic_irq = platform_get_irq(pdev, 0);
	if (unlikely(rtc->periodic_irq < 0)) {
		dev_err(&pdev->dev, "No IRQ for period\n");
		goto err_badres;
	}

	rtc->carry_irq = platform_get_irq(pdev, 1);
	if (unlikely(rtc->carry_irq < 0)) {
		dev_err(&pdev->dev, "No IRQ for carry\n");
		goto err_badres;
	}

	rtc->alarm_irq = platform_get_irq(pdev, 2);
	if (unlikely(rtc->alarm_irq < 0)) {
		dev_err(&pdev->dev, "No IRQ for alarm\n");
		goto err_badres;
	}

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "No IO resource\n");
		goto err_badres;
	}

	rtc->regsize = res->end - res->start + 1;

	rtc->res = request_mem_region(res->start, rtc->regsize, pdev->name);
	if (unlikely(!rtc->res)) {
		ret = -EBUSY;
		goto err_badres;
	}

	rtc->regbase = (void __iomem *)rtc->res->start;
	if (unlikely(!rtc->regbase)) {
		ret = -EINVAL;
		goto err_badmap;
	}

	rtc->rtc_dev = rtc_device_register("sh", &pdev->dev,
					   &sh_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		goto err_badmap;
	}

	rtc->capabilities = RTC_DEF_CAPABILITIES;
	if (pdev->dev.platform_data) {
		struct sh_rtc_platform_info *pinfo = pdev->dev.platform_data;

		/*
		 * Some CPUs have special capabilities in addition to the
		 * default set. Add those in here.
		 */
		rtc->capabilities |= pinfo->capabilities;
	}

	rtc->rtc_dev->max_user_freq = 256;
	rtc->rtc_dev->irq_freq = 1;
	rtc->periodic_freq = 0x60;

	platform_set_drvdata(pdev, rtc);

	/* register periodic/carry/alarm irqs */
	ret = request_irq(rtc->periodic_irq, sh_rtc_periodic, IRQF_DISABLED,
			  "sh-rtc period", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request period IRQ failed with %d, IRQ %d\n", ret,
			rtc->periodic_irq);
		goto err_badmap;
	}

	ret = request_irq(rtc->carry_irq, sh_rtc_interrupt, IRQF_DISABLED,
			  "sh-rtc carry", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request carry IRQ failed with %d, IRQ %d\n", ret,
			rtc->carry_irq);
		free_irq(rtc->periodic_irq, rtc);
		goto err_badmap;
	}

	ret = request_irq(rtc->alarm_irq, sh_rtc_alarm, IRQF_DISABLED,
			  "sh-rtc alarm", rtc);
	if (unlikely(ret)) {
		dev_err(&pdev->dev,
			"request alarm IRQ failed with %d, IRQ %d\n", ret,
			rtc->alarm_irq);
		free_irq(rtc->carry_irq, rtc);
		free_irq(rtc->periodic_irq, rtc);
		goto err_badmap;
	}

	tmp = readb(rtc->regbase + RCR1);
	tmp &= ~RCR1_CF;
	tmp |= RCR1_CIE;
	writeb(tmp, rtc->regbase + RCR1);

	return 0;

err_badmap:
	release_resource(rtc->res);
err_badres:
	kfree(rtc);

	return ret;
}

static int __devexit sh_rtc_remove(struct platform_device *pdev)
{
	struct sh_rtc *rtc = platform_get_drvdata(pdev);

	if (likely(rtc->rtc_dev))
		rtc_device_unregister(rtc->rtc_dev);

	sh_rtc_setpie(&pdev->dev, 0);
	sh_rtc_setaie(&pdev->dev, 0);

	free_irq(rtc->carry_irq, rtc);
	free_irq(rtc->periodic_irq, rtc);
	free_irq(rtc->alarm_irq, rtc);

	release_resource(rtc->res);

	platform_set_drvdata(pdev, NULL);

	kfree(rtc);

	return 0;
}
static struct platform_driver sh_rtc_platform_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= sh_rtc_probe,
	.remove		= __devexit_p(sh_rtc_remove),
};

static int __init sh_rtc_init(void)
{
	return platform_driver_register(&sh_rtc_platform_driver);
}

static void __exit sh_rtc_exit(void)
{
	platform_driver_unregister(&sh_rtc_platform_driver);
}

module_init(sh_rtc_init);
module_exit(sh_rtc_exit);

MODULE_DESCRIPTION("SuperH on-chip RTC driver");
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>, "
	      "Jamie Lenehan <lenehan@twibble.org>, "
	      "Angelo Castello <angelo.castello@st.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
