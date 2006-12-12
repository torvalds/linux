/*
 *  Driver for NEC VR4100 series Real Time Clock unit.
 *
 *  Copyright (C) 2003-2006  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/div64.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/vr41xx/irq.h>

MODULE_AUTHOR("Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>");
MODULE_DESCRIPTION("NEC VR4100 series RTC driver");
MODULE_LICENSE("GPL");

#define RTC1_TYPE1_START	0x0b0000c0UL
#define RTC1_TYPE1_END		0x0b0000dfUL
#define RTC2_TYPE1_START	0x0b0001c0UL
#define RTC2_TYPE1_END		0x0b0001dfUL

#define RTC1_TYPE2_START	0x0f000100UL
#define RTC1_TYPE2_END		0x0f00011fUL
#define RTC2_TYPE2_START	0x0f000120UL
#define RTC2_TYPE2_END		0x0f00013fUL

#define RTC1_SIZE		0x20
#define RTC2_SIZE		0x20

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
static unsigned long periodic_frequency;
static unsigned long periodic_count;

struct resource rtc_resource[2] = {
	{	.name	= rtc_name,
		.flags	= IORESOURCE_MEM,	},
	{	.name	= rtc_name,
		.flags	= IORESOURCE_MEM,	},
};

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

	disable_irq(ELAPSEDTIME_IRQ);
	disable_irq(RTCLONG1_IRQ);
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

	rtc1_write(ECMPLREG, (uint16_t)(alarm_sec << 15));
	rtc1_write(ECMPMREG, (uint16_t)(alarm_sec >> 1));
	rtc1_write(ECMPHREG, (uint16_t)(alarm_sec >> 17));

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int vr41xx_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	unsigned long count;

	switch (cmd) {
	case RTC_AIE_ON:
		enable_irq(ELAPSEDTIME_IRQ);
		break;
	case RTC_AIE_OFF:
		disable_irq(ELAPSEDTIME_IRQ);
		break;
	case RTC_PIE_ON:
		enable_irq(RTCLONG1_IRQ);
		break;
	case RTC_PIE_OFF:
		disable_irq(RTCLONG1_IRQ);
		break;
	case RTC_IRQP_READ:
		return put_user(periodic_frequency, (unsigned long __user *)arg);
		break;
	case RTC_IRQP_SET:
		if (arg > MAX_PERIODIC_RATE)
			return -EINVAL;

		periodic_frequency = arg;

		count = RTC_FREQUENCY;
		do_div(count, arg);

		periodic_count = count;

		spin_lock_irq(&rtc_lock);

		rtc1_write(RTCL1LREG, count);
		rtc1_write(RTCL1HREG, count >> 16);

		spin_unlock_irq(&rtc_lock);
		break;
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

static irqreturn_t elapsedtime_interrupt(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device *)dev_id;
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	rtc2_write(RTCINTREG, ELAPSEDTIME_INT);

	rtc_update_irq(&rtc->class_dev, 1, RTC_AF);

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

	rtc_update_irq(&rtc->class_dev, 1, RTC_PF);

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

static int __devinit rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	unsigned int irq;
	int retval;

	if (pdev->num_resources != 2)
		return -EBUSY;

	rtc1_base = ioremap(pdev->resource[0].start, RTC1_SIZE);
	if (rtc1_base == NULL)
		return -EBUSY;

	rtc2_base = ioremap(pdev->resource[1].start, RTC2_SIZE);
	if (rtc2_base == NULL) {
		iounmap(rtc1_base);
		rtc1_base = NULL;
		return -EBUSY;
	}

	rtc = rtc_device_register(rtc_name, &pdev->dev, &vr41xx_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		iounmap(rtc1_base);
		iounmap(rtc2_base);
		rtc1_base = NULL;
		rtc2_base = NULL;
		return PTR_ERR(rtc);
	}

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	spin_unlock_irq(&rtc_lock);

	irq = ELAPSEDTIME_IRQ;
	retval = request_irq(irq, elapsedtime_interrupt, IRQF_DISABLED,
	                     "elapsed_time", pdev);
	if (retval == 0) {
		irq = RTCLONG1_IRQ;
		retval = request_irq(irq, rtclong1_interrupt, IRQF_DISABLED,
		                     "rtclong1", pdev);
	}

	if (retval < 0) {
		printk(KERN_ERR "rtc: IRQ%d is busy\n", irq);
		rtc_device_unregister(rtc);
		if (irq == RTCLONG1_IRQ)
			free_irq(ELAPSEDTIME_IRQ, NULL);
		iounmap(rtc1_base);
		iounmap(rtc2_base);
		rtc1_base = NULL;
		rtc2_base = NULL;
		return retval;
	}

	platform_set_drvdata(pdev, rtc);

	disable_irq(ELAPSEDTIME_IRQ);
	disable_irq(RTCLONG1_IRQ);

	printk(KERN_INFO "rtc: Real Time Clock of NEC VR4100 series\n");

	return 0;
}

static int __devexit rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	rtc = platform_get_drvdata(pdev);
	if (rtc != NULL)
		rtc_device_unregister(rtc);

	platform_set_drvdata(pdev, NULL);

	free_irq(ELAPSEDTIME_IRQ, NULL);
	free_irq(RTCLONG1_IRQ, NULL);
	if (rtc1_base != NULL)
		iounmap(rtc1_base);
	if (rtc2_base != NULL)
		iounmap(rtc2_base);

	return 0;
}

static struct platform_device *rtc_platform_device;

static struct platform_driver rtc_platform_driver = {
	.probe		= rtc_probe,
	.remove		= __devexit_p(rtc_remove),
	.driver		= {
		.name	= rtc_name,
		.owner	= THIS_MODULE,
	},
};

static int __init vr41xx_rtc_init(void)
{
	int retval;

	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		rtc_resource[0].start = RTC1_TYPE1_START;
		rtc_resource[0].end = RTC1_TYPE1_END;
		rtc_resource[1].start = RTC2_TYPE1_START;
		rtc_resource[1].end = RTC2_TYPE1_END;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		rtc_resource[0].start = RTC1_TYPE2_START;
		rtc_resource[0].end = RTC1_TYPE2_END;
		rtc_resource[1].start = RTC2_TYPE2_START;
		rtc_resource[1].end = RTC2_TYPE2_END;
		break;
	default:
		return -ENODEV;
		break;
	}

	rtc_platform_device = platform_device_alloc("RTC", -1);
	if (rtc_platform_device == NULL)
		return -ENOMEM;

	retval = platform_device_add_resources(rtc_platform_device,
				rtc_resource, ARRAY_SIZE(rtc_resource));

	if (retval == 0)
		retval = platform_device_add(rtc_platform_device);

	if (retval < 0) {
		platform_device_put(rtc_platform_device);
		return retval;
	}

	retval = platform_driver_register(&rtc_platform_driver);
	if (retval < 0)
		platform_device_unregister(rtc_platform_device);

	return retval;
}

static void __exit vr41xx_rtc_exit(void)
{
	platform_driver_unregister(&rtc_platform_driver);
	platform_device_unregister(rtc_platform_device);
}

module_init(vr41xx_rtc_init);
module_exit(vr41xx_rtc_exit);
