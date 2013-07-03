/*
 *  Driver for NEC VR4100 series Real Time Clock unit.
 *
 *  Copyright (C) 2003-2008  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/log2.h>

#include <asm/div64.h>
#include <asm/io.h>
#include <asm/uaccess.h>

MODULE_AUTHOR("Yoichi Yuasa <yuasa@linux-mips.org>");
MODULE_DESCRIPTION("NEC VR4100 series RTC driver");
MODULE_LICENSE("GPL v2");

/* RTC 1 registers */
#define ETIMELREG		0x00
#define ETIMEMREG		0x02
#define ETIMEHREG		0x04
/* RFU */
#define ECMPLREG		0x08
#define ECMPMREG		0x0a
#define ECMPHREG		0x0c
/* RFU */
#define RTCL1LREG		0x10
#define RTCL1HREG		0x12
#define RTCL1CNTLREG		0x14
#define RTCL1CNTHREG		0x16
#define RTCL2LREG		0x18
#define RTCL2HREG		0x1a
#define RTCL2CNTLREG		0x1c
#define RTCL2CNTHREG		0x1e

/* RTC 2 registers */
#define TCLKLREG		0x00
#define TCLKHREG		0x02
#define TCLKCNTLREG		0x04
#define TCLKCNTHREG		0x06
/* RFU */
#define RTCINTREG		0x1e
 #define TCLOCK_INT		0x08
 #define RTCLONG2_INT		0x04
 #define RTCLONG1_INT		0x02
 #define ELAPSEDTIME_INT	0x01

#define RTC_FREQUENCY		32768
#define MAX_PERIODIC_RATE	6553

static void __iomem *rtc1_base;
static void __iomem *rtc2_base;

#define rtc1_read(offset)		readw(rtc1_base + (offset))
#define rtc1_write(offset, value)	writew((value), rtc1_base + (offset))

#define rtc2_read(offset)		readw(rtc2_base + (offset))
#define rtc2_write(offset, value)	writew((value), rtc2_base + (offset))

static unsigned long epoch = 1970;	/* Jan 1 1970 00:00:00 */

static DEFINE_SPINLOCK(rtc_lock);
static char rtc_name[] = "RTC";
static unsigned long periodic_count;
static unsigned int alarm_enabled;
static int aie_irq;
static int pie_irq;

static inline unsigned long read_elapsed_second(void)
{

	unsigned long first_low, first_mid, first_high;

	unsigned long second_low, second_mid, second_high;

	do {
		first_low = rtc1_read(ETIMELREG);
		first_mid = rtc1_read(ETIMEMREG);
		first_high = rtc1_read(ETIMEHREG);
		second_low = rtc1_read(ETIMELREG);
		second_mid = rtc1_read(ETIMEMREG);
		second_high = rtc1_read(ETIMEHREG);
	} while (first_low != second_low || first_mid != second_mid ||
		 first_high != second_high);

	return (first_high << 17) | (first_mid << 1) | (first_low >> 15);
}

static inline void write_elapsed_second(unsigned long sec)
{
	spin_lock_irq(&rtc_lock);

	rtc1_write(ETIMELREG, (uint16_t)(sec << 15));
	rtc1_write(ETIMEMREG, (uint16_t)(sec >> 1));
	rtc1_write(ETIMEHREG, (uint16_t)(sec >> 17));

	spin_unlock_irq(&rtc_lock);
}

static void vr41xx_rtc_release(struct device *dev)
{

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	spin_unlock_irq(&rtc_lock);

	disable_irq(aie_irq);
	disable_irq(pie_irq);
}

static int vr41xx_rtc_read_time(struct device *dev, struct rtc_time *time)
{
	unsigned long epoch_sec, elapsed_sec;

	epoch_sec = mktime(epoch, 1, 1, 0, 0, 0);
	elapsed_sec = read_elapsed_second();

	rtc_time_to_tm(epoch_sec + elapsed_sec, time);

	return 0;
}

static int vr41xx_rtc_set_time(struct device *dev, struct rtc_time *time)
{
	unsigned long epoch_sec, current_sec;

	epoch_sec = mktime(epoch, 1, 1, 0, 0, 0);
	current_sec = mktime(time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
			     time->tm_hour, time->tm_min, time->tm_sec);

	write_elapsed_second(current_sec - epoch_sec);

	return 0;
}

static int vr41xx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	unsigned long low, mid, high;
	struct rtc_time *time = &wkalrm->time;

	spin_lock_irq(&rtc_lock);

	low = rtc1_read(ECMPLREG);
	mid = rtc1_read(ECMPMREG);
	high = rtc1_read(ECMPHREG);
	wkalrm->enabled = alarm_enabled;

	spin_unlock_irq(&rtc_lock);

	rtc_time_to_tm((high << 17) | (mid << 1) | (low >> 15), time);

	return 0;
}

static int vr41xx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	unsigned long alarm_sec;
	struct rtc_time *time = &wkalrm->time;

	alarm_sec = mktime(time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
			   time->tm_hour, time->tm_min, time->tm_sec);

	spin_lock_irq(&rtc_lock);

	if (alarm_enabled)
		disable_irq(aie_irq);

	rtc1_write(ECMPLREG, (uint16_t)(alarm_sec << 15));
	rtc1_write(ECMPMREG, (uint16_t)(alarm_sec >> 1));
	rtc1_write(ECMPHREG, (uint16_t)(alarm_sec >> 17));

	if (wkalrm->enabled)
		enable_irq(aie_irq);

	alarm_enabled = wkalrm->enabled;

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int vr41xx_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case RTC_EPOCH_READ:
		return put_user(epoch, (unsigned long __user *)arg);
	case RTC_EPOCH_SET:
		/* Doesn't support before 1900 */
		if (arg < 1900)
			return -EINVAL;
		epoch = arg;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int vr41xx_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	spin_lock_irq(&rtc_lock);
	if (enabled) {
		if (!alarm_enabled) {
			enable_irq(aie_irq);
			alarm_enabled = 1;
		}
	} else {
		if (alarm_enabled) {
			disable_irq(aie_irq);
			alarm_enabled = 0;
		}
	}
	spin_unlock_irq(&rtc_lock);
	return 0;
}

static irqreturn_t elapsedtime_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)dev_id;
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	rtc2_write(RTCINTREG, ELAPSEDTIME_INT);

	rtc_update_irq(rtc, 1, RTC_AF);

	return IRQ_HANDLED;
}

static irqreturn_t rtclong1_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)dev_id;
	struct rtc_device *rtc = platform_get_drvdata(pdev);
	unsigned long count = periodic_count;

	rtc2_write(RTCINTREG, RTCLONG1_INT);

	rtc1_write(RTCL1LREG, count);
	rtc1_write(RTCL1HREG, count >> 16);

	rtc_update_irq(rtc, 1, RTC_PF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops vr41xx_rtc_ops = {
	.release	= vr41xx_rtc_release,
	.ioctl		= vr41xx_rtc_ioctl,
	.read_time	= vr41xx_rtc_read_time,
	.set_time	= vr41xx_rtc_set_time,
	.read_alarm	= vr41xx_rtc_read_alarm,
	.set_alarm	= vr41xx_rtc_set_alarm,
};

static int rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rtc_device *rtc;
	int retval;

	if (pdev->num_resources != 4)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EBUSY;

	rtc1_base = ioremap(res->start, resource_size(res));
	if (!rtc1_base)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		retval = -EBUSY;
		goto err_rtc1_iounmap;
	}

	rtc2_base = ioremap(res->start, resource_size(res));
	if (!rtc2_base) {
		retval = -EBUSY;
		goto err_rtc1_iounmap;
	}

	rtc = rtc_device_register(rtc_name, &pdev->dev, &vr41xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		retval = PTR_ERR(rtc);
		goto err_iounmap_all;
	}

	rtc->max_user_freq = MAX_PERIODIC_RATE;

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	spin_unlock_irq(&rtc_lock);

	aie_irq = platform_get_irq(pdev, 0);
	if (aie_irq <= 0) {
		retval = -EBUSY;
		goto err_device_unregister;
	}

	retval = request_irq(aie_irq, elapsedtime_interrupt, 0,
			     "elapsed_time", pdev);
	if (retval < 0)
		goto err_device_unregister;

	pie_irq = platform_get_irq(pdev, 1);
	if (pie_irq <= 0)
		goto err_free_irq;

	retval = request_irq(pie_irq, rtclong1_interrupt, 0,
			     "rtclong1", pdev);
	if (retval < 0)
		goto err_free_irq;

	platform_set_drvdata(pdev, rtc);

	disable_irq(aie_irq);
	disable_irq(pie_irq);

	dev_info(&pdev->dev, "Real Time Clock of NEC VR4100 series\n");

	return 0;

err_free_irq:
	free_irq(aie_irq, pdev);

err_device_unregister:
	rtc_device_unregister(rtc);

err_iounmap_all:
	iounmap(rtc2_base);
	rtc2_base = NULL;

err_rtc1_iounmap:
	iounmap(rtc1_base);
	rtc1_base = NULL;

	return retval;
}

static int rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = platform_get_drvdata(pdev);
	if (rtc)
		rtc_device_unregister(rtc);

	platform_set_drvdata(pdev, NULL);

	free_irq(aie_irq, pdev);
	free_irq(pie_irq, pdev);
	if (rtc1_base)
		iounmap(rtc1_base);
	if (rtc2_base)
		iounmap(rtc2_base);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:RTC");

static struct platform_driver rtc_platform_driver = {
	.probe		= rtc_probe,
	.remove		= rtc_remove,
	.driver		= {
		.name	= rtc_name,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(rtc_platform_driver);
