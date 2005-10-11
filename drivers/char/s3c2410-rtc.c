/* drivers/char/s3c2410_rtc.c
 *
 * Copyright (c) 2004 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 Internal RTC Driver
 *
 *  Changelog:
 *	08-Nov-2004	BJD	Initial creation
 *	12-Nov-2004	BJD	Added periodic IRQ and PM code
 *	22-Nov-2004	BJD	Sign-test on alarm code to check for <0
 *	10-Mar-2005	LCVR	Changed S3C2410_VA_RTC to S3C24XX_VA_RTC
*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>

#include <asm/hardware.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/rtc.h>

#include <asm/mach/time.h>

#include <asm/hardware/clock.h>
#include <asm/arch/regs-rtc.h>

/* need this for the RTC_AF definitions */
#include <linux/mc146818rtc.h>

#undef S3C24XX_VA_RTC
#define S3C24XX_VA_RTC s3c2410_rtc_base

static struct resource *s3c2410_rtc_mem;

static void __iomem *s3c2410_rtc_base;
static int s3c2410_rtc_alarmno = NO_IRQ;
static int s3c2410_rtc_tickno  = NO_IRQ;
static int s3c2410_rtc_freq    = 1;

static DEFINE_SPINLOCK(s3c2410_rtc_pie_lock);

/* IRQ Handlers */

static irqreturn_t s3c2410_rtc_alarmirq(int irq, void *id, struct pt_regs *r)
{
	rtc_update(1, RTC_AF | RTC_IRQF);
	return IRQ_HANDLED;
}

static irqreturn_t s3c2410_rtc_tickirq(int irq, void *id, struct pt_regs *r)
{
	rtc_update(1, RTC_PF | RTC_IRQF);
	return IRQ_HANDLED;
}

/* Update control registers */
static void s3c2410_rtc_setaie(int to)
{
	unsigned int tmp;

	pr_debug("%s: aie=%d\n", __FUNCTION__, to);

	tmp = readb(S3C2410_RTCALM);

	if (to)
		tmp |= S3C2410_RTCALM_ALMEN;
	else
		tmp &= ~S3C2410_RTCALM_ALMEN;


	writeb(tmp, S3C2410_RTCALM);
}

static void s3c2410_rtc_setpie(int to)
{
	unsigned int tmp;

	pr_debug("%s: pie=%d\n", __FUNCTION__, to);

	spin_lock_irq(&s3c2410_rtc_pie_lock);
	tmp = readb(S3C2410_TICNT) & ~S3C2410_TICNT_ENABLE;

	if (to)
		tmp |= S3C2410_TICNT_ENABLE;

	writeb(tmp, S3C2410_TICNT);
	spin_unlock_irq(&s3c2410_rtc_pie_lock);
}

static void s3c2410_rtc_setfreq(int freq)
{
	unsigned int tmp;

	spin_lock_irq(&s3c2410_rtc_pie_lock);
	tmp = readb(S3C2410_TICNT) & S3C2410_TICNT_ENABLE;

	s3c2410_rtc_freq = freq;

	tmp |= (128 / freq)-1;

	writeb(tmp, S3C2410_TICNT);
	spin_unlock_irq(&s3c2410_rtc_pie_lock);
}

/* Time read/write */

static int s3c2410_rtc_gettime(struct rtc_time *rtc_tm)
{
	unsigned int have_retried = 0;

 retry_get_time:
	rtc_tm->tm_min  = readb(S3C2410_RTCMIN);
	rtc_tm->tm_hour = readb(S3C2410_RTCHOUR);
	rtc_tm->tm_mday = readb(S3C2410_RTCDATE);
	rtc_tm->tm_mon  = readb(S3C2410_RTCMON);
	rtc_tm->tm_year = readb(S3C2410_RTCYEAR);
	rtc_tm->tm_sec  = readb(S3C2410_RTCSEC);

	/* the only way to work out wether the system was mid-update
	 * when we read it is to check the second counter, and if it
	 * is zero, then we re-try the entire read
	 */

	if (rtc_tm->tm_sec == 0 && !have_retried) {
		have_retried = 1;
		goto retry_get_time;
	}

	pr_debug("read time %02x.%02x.%02x %02x/%02x/%02x\n",
		 rtc_tm->tm_year, rtc_tm->tm_mon, rtc_tm->tm_mday,
		 rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

	BCD_TO_BIN(rtc_tm->tm_sec);
	BCD_TO_BIN(rtc_tm->tm_min);
	BCD_TO_BIN(rtc_tm->tm_hour);
	BCD_TO_BIN(rtc_tm->tm_mday);
	BCD_TO_BIN(rtc_tm->tm_mon);
	BCD_TO_BIN(rtc_tm->tm_year);

	rtc_tm->tm_year += 100;
	rtc_tm->tm_mon -= 1;

	return 0;
}


static int s3c2410_rtc_settime(struct rtc_time *tm)
{
	/* the rtc gets round the y2k problem by just not supporting it */

	if (tm->tm_year < 100)
		return -EINVAL;

	writeb(BIN2BCD(tm->tm_sec),  S3C2410_RTCSEC);
	writeb(BIN2BCD(tm->tm_min),  S3C2410_RTCMIN);
	writeb(BIN2BCD(tm->tm_hour), S3C2410_RTCHOUR);
	writeb(BIN2BCD(tm->tm_mday), S3C2410_RTCDATE);
	writeb(BIN2BCD(tm->tm_mon + 1), S3C2410_RTCMON);
	writeb(BIN2BCD(tm->tm_year - 100), S3C2410_RTCYEAR);

	return 0;
}

static int s3c2410_rtc_getalarm(struct rtc_wkalrm *alrm)
{
	struct rtc_time *alm_tm = &alrm->time;
	unsigned int alm_en;

	alm_tm->tm_sec  = readb(S3C2410_ALMSEC);
	alm_tm->tm_min  = readb(S3C2410_ALMMIN);
	alm_tm->tm_hour = readb(S3C2410_ALMHOUR);
	alm_tm->tm_mon  = readb(S3C2410_ALMMON);
	alm_tm->tm_mday = readb(S3C2410_ALMDATE);
	alm_tm->tm_year = readb(S3C2410_ALMYEAR);

	alm_en = readb(S3C2410_RTCALM);

	pr_debug("read alarm %02x %02x.%02x.%02x %02x/%02x/%02x\n",
		 alm_en,
		 alm_tm->tm_year, alm_tm->tm_mon, alm_tm->tm_mday,
		 alm_tm->tm_hour, alm_tm->tm_min, alm_tm->tm_sec);


	/* decode the alarm enable field */

	if (alm_en & S3C2410_RTCALM_SECEN) {
		BCD_TO_BIN(alm_tm->tm_sec);
	} else {
		alm_tm->tm_sec = 0xff;
	}

	if (alm_en & S3C2410_RTCALM_MINEN) {
		BCD_TO_BIN(alm_tm->tm_min);
	} else {
		alm_tm->tm_min = 0xff;
	}

	if (alm_en & S3C2410_RTCALM_HOUREN) {
		BCD_TO_BIN(alm_tm->tm_hour);
	} else {
		alm_tm->tm_hour = 0xff;
	}

	if (alm_en & S3C2410_RTCALM_DAYEN) {
		BCD_TO_BIN(alm_tm->tm_mday);
	} else {
		alm_tm->tm_mday = 0xff;
	}

	if (alm_en & S3C2410_RTCALM_MONEN) {
		BCD_TO_BIN(alm_tm->tm_mon);
		alm_tm->tm_mon -= 1;
	} else {
		alm_tm->tm_mon = 0xff;
	}

	if (alm_en & S3C2410_RTCALM_YEAREN) {
		BCD_TO_BIN(alm_tm->tm_year);
	} else {
		alm_tm->tm_year = 0xffff;
	}

	/* todo - set alrm->enabled ? */

	return 0;
}

static int s3c2410_rtc_setalarm(struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &alrm->time;
	unsigned int alrm_en;

	pr_debug("s3c2410_rtc_setalarm: %d, %02x/%02x/%02x %02x.%02x.%02x\n",
		 alrm->enabled,
		 tm->tm_mday & 0xff, tm->tm_mon & 0xff, tm->tm_year & 0xff,
		 tm->tm_hour & 0xff, tm->tm_min & 0xff, tm->tm_sec);

	if (alrm->enabled || 1) {
		alrm_en = readb(S3C2410_RTCALM) & S3C2410_RTCALM_ALMEN;
		writeb(0x00, S3C2410_RTCALM);

		if (tm->tm_sec < 60 && tm->tm_sec >= 0) {
			alrm_en |= S3C2410_RTCALM_SECEN;
			writeb(BIN2BCD(tm->tm_sec), S3C2410_ALMSEC);
		}

		if (tm->tm_min < 60 && tm->tm_min >= 0) {
			alrm_en |= S3C2410_RTCALM_MINEN;
			writeb(BIN2BCD(tm->tm_min), S3C2410_ALMMIN);
		}

		if (tm->tm_hour < 24 && tm->tm_hour >= 0) {
			alrm_en |= S3C2410_RTCALM_HOUREN;
			writeb(BIN2BCD(tm->tm_hour), S3C2410_ALMHOUR);
		}

		pr_debug("setting S3C2410_RTCALM to %08x\n", alrm_en);

		writeb(alrm_en, S3C2410_RTCALM);
		enable_irq_wake(s3c2410_rtc_alarmno);
	} else {
		alrm_en = readb(S3C2410_RTCALM);
		alrm_en &= ~S3C2410_RTCALM_ALMEN;
		writeb(alrm_en, S3C2410_RTCALM);
		disable_irq_wake(s3c2410_rtc_alarmno);
	}

	return 0;
}

static int s3c2410_rtc_ioctl(unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
		s3c2410_rtc_setaie((cmd == RTC_AIE_ON) ? 1 : 0);
		return 0;

	case RTC_PIE_OFF:
	case RTC_PIE_ON:
		s3c2410_rtc_setpie((cmd == RTC_PIE_ON) ? 1 : 0);
		return 0;

	case RTC_IRQP_READ:
		return put_user(s3c2410_rtc_freq, (unsigned long __user *)arg);

	case RTC_IRQP_SET:
		if (arg < 1 || arg > 64)
			return -EINVAL;

		if (!capable(CAP_SYS_RESOURCE))
			return -EACCES;

		/* check for power of 2 */

		if ((arg & (arg-1)) != 0)
			return -EINVAL;

		pr_debug("s3c2410_rtc: setting frequency %ld\n", arg);

		s3c2410_rtc_setfreq(arg);
		return 0;

	case RTC_UIE_ON:
	case RTC_UIE_OFF:
		return -EINVAL;
	}

	return -EINVAL;
}

static int s3c2410_rtc_proc(char *buf)
{
	unsigned int rtcalm = readb(S3C2410_RTCALM);
	unsigned int ticnt = readb (S3C2410_TICNT);
	char *p = buf;

	p += sprintf(p, "alarm_IRQ\t: %s\n",
		     (rtcalm & S3C2410_RTCALM_ALMEN) ? "yes" : "no" );
	p += sprintf(p, "periodic_IRQ\t: %s\n",
		     (ticnt & S3C2410_TICNT_ENABLE) ? "yes" : "no" );
	p += sprintf(p, "periodic_freq\t: %d\n", s3c2410_rtc_freq);

	return p - buf;
}

static int s3c2410_rtc_open(void)
{
	int ret;

	ret = request_irq(s3c2410_rtc_alarmno, s3c2410_rtc_alarmirq,
			  SA_INTERRUPT,  "s3c2410-rtc alarm", NULL);

	if (ret)
		printk(KERN_ERR "IRQ%d already in use\n", s3c2410_rtc_alarmno);

	ret = request_irq(s3c2410_rtc_tickno, s3c2410_rtc_tickirq,
			  SA_INTERRUPT,  "s3c2410-rtc tick", NULL);

	if (ret) {
		printk(KERN_ERR "IRQ%d already in use\n", s3c2410_rtc_tickno);
		goto tick_err;
	}

	return ret;

 tick_err:
	free_irq(s3c2410_rtc_alarmno, NULL);
	return ret;
}

static void s3c2410_rtc_release(void)
{
	/* do not clear AIE here, it may be needed for wake */

	s3c2410_rtc_setpie(0);
	free_irq(s3c2410_rtc_alarmno, NULL);
	free_irq(s3c2410_rtc_tickno, NULL);
}

static struct rtc_ops s3c2410_rtcops = {
	.owner		= THIS_MODULE,
	.open		= s3c2410_rtc_open,
	.release	= s3c2410_rtc_release,
	.ioctl		= s3c2410_rtc_ioctl,
	.read_time	= s3c2410_rtc_gettime,
	.set_time	= s3c2410_rtc_settime,
	.read_alarm	= s3c2410_rtc_getalarm,
	.set_alarm	= s3c2410_rtc_setalarm,
	.proc	        = s3c2410_rtc_proc,
};

static void s3c2410_rtc_enable(struct device *dev, int en)
{
	unsigned int tmp;

	if (s3c2410_rtc_base == NULL)
		return;

	if (!en) {
		tmp = readb(S3C2410_RTCCON);
		writeb(tmp & ~S3C2410_RTCCON_RTCEN, S3C2410_RTCCON);

		tmp = readb(S3C2410_TICNT);
		writeb(tmp & ~S3C2410_TICNT_ENABLE, S3C2410_TICNT);
	} else {
		/* re-enable the device, and check it is ok */

		if ((readb(S3C2410_RTCCON) & S3C2410_RTCCON_RTCEN) == 0){
			dev_info(dev, "rtc disabled, re-enabling\n");

			tmp = readb(S3C2410_RTCCON);
			writeb(tmp | S3C2410_RTCCON_RTCEN , S3C2410_RTCCON);
		}

		if ((readb(S3C2410_RTCCON) & S3C2410_RTCCON_CNTSEL)){
			dev_info(dev, "removing S3C2410_RTCCON_CNTSEL\n");

			tmp = readb(S3C2410_RTCCON);
			writeb(tmp& ~S3C2410_RTCCON_CNTSEL , S3C2410_RTCCON);
		}

		if ((readb(S3C2410_RTCCON) & S3C2410_RTCCON_CLKRST)){
			dev_info(dev, "removing S3C2410_RTCCON_CLKRST\n");

			tmp = readb(S3C2410_RTCCON);
			writeb(tmp & ~S3C2410_RTCCON_CLKRST, S3C2410_RTCCON);
		}
	}
}

static int s3c2410_rtc_remove(struct device *dev)
{
	unregister_rtc(&s3c2410_rtcops);

	s3c2410_rtc_setpie(0);
	s3c2410_rtc_setaie(0);

	if (s3c2410_rtc_mem != NULL) {
		pr_debug("s3c2410_rtc: releasing s3c2410_rtc_mem\n");
		iounmap(s3c2410_rtc_base);
		release_resource(s3c2410_rtc_mem);
		kfree(s3c2410_rtc_mem);
	}

	return 0;
}

static int s3c2410_rtc_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;
	int ret;

	pr_debug("%s: probe=%p, device=%p\n", __FUNCTION__, pdev, dev);

	/* find the IRQs */

	s3c2410_rtc_tickno = platform_get_irq(pdev, 1);
	if (s3c2410_rtc_tickno <= 0) {
		dev_err(dev, "no irq for rtc tick\n");
		return -ENOENT;
	}

	s3c2410_rtc_alarmno = platform_get_irq(pdev, 0);
	if (s3c2410_rtc_alarmno <= 0) {
		dev_err(dev, "no irq for alarm\n");
		return -ENOENT;
	}

	pr_debug("s3c2410_rtc: tick irq %d, alarm irq %d\n",
		 s3c2410_rtc_tickno, s3c2410_rtc_alarmno);

	/* get the memory region */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "failed to get memory region resource\n");
		return -ENOENT;
	}

	s3c2410_rtc_mem = request_mem_region(res->start, res->end-res->start+1,
				     pdev->name);

	if (s3c2410_rtc_mem == NULL) {
		dev_err(dev, "failed to reserve memory region\n");
		ret = -ENOENT;
		goto exit_err;
	}

	s3c2410_rtc_base = ioremap(res->start, res->end - res->start + 1);
	if (s3c2410_rtc_base == NULL) {
		dev_err(dev, "failed ioremap()\n");
		ret = -EINVAL;
		goto exit_err;
	}

	s3c2410_rtc_mem = res;
	pr_debug("s3c2410_rtc_base=%p\n", s3c2410_rtc_base);

 	pr_debug("s3c2410_rtc: RTCCON=%02x\n", readb(S3C2410_RTCCON));

	/* check to see if everything is setup correctly */

	s3c2410_rtc_enable(dev, 1);

 	pr_debug("s3c2410_rtc: RTCCON=%02x\n", readb(S3C2410_RTCCON));

	s3c2410_rtc_setfreq(s3c2410_rtc_freq);

	/* register RTC and exit */

	register_rtc(&s3c2410_rtcops);
	return 0;

 exit_err:
	dev_err(dev, "error %d during initialisation\n", ret);

	return ret;
}

#ifdef CONFIG_PM

/* S3C2410 RTC Power management control */

static struct timespec s3c2410_rtc_delta;

static int ticnt_save;

static int s3c2410_rtc_suspend(struct device *dev, pm_message_t state, u32 level)
{
	struct rtc_time tm;
	struct timespec time;

	time.tv_nsec = 0;

	if (level == SUSPEND_POWER_DOWN) {
		/* save TICNT for anyone using periodic interrupts */

		ticnt_save = readb(S3C2410_TICNT);

		/* calculate time delta for suspend */

		s3c2410_rtc_gettime(&tm);
		rtc_tm_to_time(&tm, &time.tv_sec);
		save_time_delta(&s3c2410_rtc_delta, &time);
		s3c2410_rtc_enable(dev, 0);
	}

	return 0;
}

static int s3c2410_rtc_resume(struct device *dev, u32 level)
{
	struct rtc_time tm;
	struct timespec time;

	time.tv_nsec = 0;

	s3c2410_rtc_enable(dev, 1);
	s3c2410_rtc_gettime(&tm);
	rtc_tm_to_time(&tm, &time.tv_sec);
	restore_time_delta(&s3c2410_rtc_delta, &time);

	writeb(ticnt_save, S3C2410_TICNT);
	return 0;
}
#else
#define s3c2410_rtc_suspend NULL
#define s3c2410_rtc_resume  NULL
#endif

static struct device_driver s3c2410_rtcdrv = {
	.name		= "s3c2410-rtc",
	.owner		= THIS_MODULE,
	.bus		= &platform_bus_type,
	.probe		= s3c2410_rtc_probe,
	.remove		= s3c2410_rtc_remove,
	.suspend	= s3c2410_rtc_suspend,
	.resume		= s3c2410_rtc_resume,
};

static char __initdata banner[] = "S3C2410 RTC, (c) 2004 Simtec Electronics\n";

static int __init s3c2410_rtc_init(void)
{
	printk(banner);
	return driver_register(&s3c2410_rtcdrv);
}

static void __exit s3c2410_rtc_exit(void)
{
	driver_unregister(&s3c2410_rtcdrv);
}

module_init(s3c2410_rtc_init);
module_exit(s3c2410_rtc_exit);

MODULE_DESCRIPTION("S3C24XX RTC Driver");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
