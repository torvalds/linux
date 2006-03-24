/*
 *  Driver for NEC VR4100 series  Real Time Clock unit.
 *
 *  Copyright (C) 2003-2005  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
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
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/mc146818rtc.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/div64.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/uaccess.h>
#include <asm/vr41xx/vr41xx.h>

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
#define MAX_USER_PERIODIC_RATE	64

static void __iomem *rtc1_base;
static void __iomem *rtc2_base;

#define rtc1_read(offset)		readw(rtc1_base + (offset))
#define rtc1_write(offset, value)	writew((value), rtc1_base + (offset))

#define rtc2_read(offset)		readw(rtc2_base + (offset))
#define rtc2_write(offset, value)	writew((value), rtc2_base + (offset))

static unsigned long epoch = 1970;	/* Jan 1 1970 00:00:00 */

static spinlock_t rtc_task_lock;
static wait_queue_head_t rtc_wait;
static unsigned long rtc_irq_data;
static struct fasync_struct *rtc_async_queue;
static rtc_task_t *rtc_callback;
static char rtc_name[] = "RTC";
static unsigned long periodic_frequency;
static unsigned long periodic_count;

typedef enum {
	RTC_RELEASE,
	RTC_OPEN,
} rtc_status_t;

static rtc_status_t rtc_status;

typedef enum {
	FUNCTION_RTC_IOCTL,
	FUNCTION_RTC_CONTROL,
} rtc_callfrom_t;

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

static void set_alarm(struct rtc_time *time)
{
	unsigned long alarm_sec;

	alarm_sec = mktime(time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
	                   time->tm_hour, time->tm_min, time->tm_sec);

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, (uint16_t)(alarm_sec << 15));
	rtc1_write(ECMPMREG, (uint16_t)(alarm_sec >> 1));
	rtc1_write(ECMPHREG, (uint16_t)(alarm_sec >> 17));

	spin_unlock_irq(&rtc_lock);
}

static void read_alarm(struct rtc_time *time)
{
	unsigned long low, mid, high;

	spin_lock_irq(&rtc_lock);

	low = rtc1_read(ECMPLREG);
	mid = rtc1_read(ECMPMREG);
	high = rtc1_read(ECMPHREG);

	spin_unlock_irq(&rtc_lock);

	to_tm((high << 17) | (mid << 1) | (low >> 15), time);
	time->tm_year -= 1900;
}

static void read_time(struct rtc_time *time)
{
	unsigned long epoch_sec, elapsed_sec;

	epoch_sec = mktime(epoch, 1, 1, 0, 0, 0);
	elapsed_sec = read_elapsed_second();

	to_tm(epoch_sec + elapsed_sec, time);
	time->tm_year -= 1900;
}

static void set_time(struct rtc_time *time)
{
	unsigned long epoch_sec, current_sec;

	epoch_sec = mktime(epoch, 1, 1, 0, 0, 0);
	current_sec = mktime(time->tm_year + 1900, time->tm_mon + 1, time->tm_mday,
	                     time->tm_hour, time->tm_min, time->tm_sec);

	write_elapsed_second(current_sec - epoch_sec);
}

static ssize_t rtc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long irq_data;
	int retval = 0;

	if (count != sizeof(unsigned int) && count != sizeof(unsigned long))
		return -EINVAL;

	add_wait_queue(&rtc_wait, &wait);

	do {
		__set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(&rtc_lock);
		irq_data = rtc_irq_data;
		rtc_irq_data = 0;
		spin_unlock_irq(&rtc_lock);

		if (irq_data != 0)
			break;

		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
	} while (1);

	if (retval == 0) {
		if (count == sizeof(unsigned int)) {
			retval = put_user(irq_data, (unsigned int __user *)buf);
			if (retval == 0)
				retval = sizeof(unsigned int);
		} else {
			retval = put_user(irq_data, (unsigned long __user *)buf);
			if (retval == 0)
				retval = sizeof(unsigned long);
		}

	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&rtc_wait, &wait);

	return retval;
}

static unsigned int rtc_poll(struct file *file, struct poll_table_struct *table)
{
	poll_wait(file, &rtc_wait, table);

	if (rtc_irq_data != 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

static int rtc_do_ioctl(unsigned int cmd, unsigned long arg, rtc_callfrom_t from)
{
	struct rtc_time time;
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
	case RTC_ALM_SET:
		if (copy_from_user(&time, (struct rtc_time __user *)arg,
		                   sizeof(struct rtc_time)))
			return -EFAULT;

		set_alarm(&time);
		break;
	case RTC_ALM_READ:
		memset(&time, 0, sizeof(struct rtc_time));
		read_alarm(&time);
		break;
	case RTC_RD_TIME:
		memset(&time, 0, sizeof(struct rtc_time));
		read_time(&time);
		if (copy_to_user((void __user *)arg, &time, sizeof(struct rtc_time)))
			return -EFAULT;
		break;
	case RTC_SET_TIME:
		if (capable(CAP_SYS_TIME) == 0)
			return -EACCES;

		if (copy_from_user(&time, (struct rtc_time __user *)arg,
		                   sizeof(struct rtc_time)))
			return -EFAULT;

		set_time(&time);
		break;
	case RTC_IRQP_READ:
		return put_user(periodic_frequency, (unsigned long __user *)arg);
		break;
	case RTC_IRQP_SET:
		if (arg > MAX_PERIODIC_RATE)
			return -EINVAL;

		if (from == FUNCTION_RTC_IOCTL && arg > MAX_USER_PERIODIC_RATE &&
		    capable(CAP_SYS_RESOURCE) == 0)
			return -EACCES;

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

		if (capable(CAP_SYS_TIME) == 0)
			return -EACCES;

		epoch = arg;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
                     unsigned long arg)
{
	return rtc_do_ioctl(cmd, arg, FUNCTION_RTC_IOCTL);
}

static int rtc_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&rtc_lock);

	if (rtc_status == RTC_OPEN) {
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}

	rtc_status = RTC_OPEN;
	rtc_irq_data = 0;

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int rtc_release(struct inode *inode, struct file *file)
{
	if (file->f_flags & FASYNC)
		(void)fasync_helper(-1, file, 0, &rtc_async_queue);

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	rtc_status = RTC_RELEASE;

	spin_unlock_irq(&rtc_lock);

	disable_irq(ELAPSEDTIME_IRQ);
	disable_irq(RTCLONG1_IRQ);

	return 0;
}

static int rtc_fasync(int fd, struct file *file, int on)
{
	return fasync_helper(fd, file, on, &rtc_async_queue);
}

static struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= rtc_read,
	.poll		= rtc_poll,
	.ioctl		= rtc_ioctl,
	.open		= rtc_open,
	.release	= rtc_release,
	.fasync		= rtc_fasync,
};

static irqreturn_t elapsedtime_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	spin_lock(&rtc_lock);
	rtc2_write(RTCINTREG, ELAPSEDTIME_INT);

	rtc_irq_data += 0x100;
	rtc_irq_data &= ~0xff;
	rtc_irq_data |= RTC_AF;
	spin_unlock(&rtc_lock);

	spin_lock(&rtc_lock);
	if (rtc_callback)
		rtc_callback->func(rtc_callback->private_data);
	spin_unlock(&rtc_lock);

	wake_up_interruptible(&rtc_wait);

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);

	return IRQ_HANDLED;
}

static irqreturn_t rtclong1_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long count = periodic_count;

	spin_lock(&rtc_lock);
	rtc2_write(RTCINTREG, RTCLONG1_INT);

	rtc1_write(RTCL1LREG, count);
	rtc1_write(RTCL1HREG, count >> 16);

	rtc_irq_data += 0x100;
	rtc_irq_data &= ~0xff;
	rtc_irq_data |= RTC_PF;
	spin_unlock(&rtc_lock);

	spin_lock(&rtc_task_lock);
	if (rtc_callback)
		rtc_callback->func(rtc_callback->private_data);
	spin_unlock(&rtc_task_lock);

	wake_up_interruptible(&rtc_wait);

	kill_fasync(&rtc_async_queue, SIGIO, POLL_IN);

	return IRQ_HANDLED;
}

int rtc_register(rtc_task_t *task)
{
	if (task == NULL || task->func == NULL)
		return -EINVAL;

	spin_lock_irq(&rtc_lock);
	if (rtc_status == RTC_OPEN) {
		spin_unlock_irq(&rtc_lock);
		return -EBUSY;
	}

	spin_lock(&rtc_task_lock);
	if (rtc_callback != NULL) {
		spin_unlock(&rtc_task_lock);
		spin_unlock_irq(&rtc_task_lock);
		return -EBUSY;
	}

	rtc_callback = task;
	spin_unlock(&rtc_task_lock);

	rtc_status = RTC_OPEN;

	spin_unlock_irq(&rtc_lock);

	return 0;
}

EXPORT_SYMBOL_GPL(rtc_register);

int rtc_unregister(rtc_task_t *task)
{
	spin_lock_irq(&rtc_task_lock);
	if (task == NULL || rtc_callback != task) {
		spin_unlock_irq(&rtc_task_lock);
		return -ENXIO;
	}

	spin_lock(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	rtc_status = RTC_RELEASE;

	spin_unlock(&rtc_lock);

	rtc_callback = NULL;

	spin_unlock_irq(&rtc_task_lock);

	disable_irq(ELAPSEDTIME_IRQ);
	disable_irq(RTCLONG1_IRQ);

	return 0;
}

EXPORT_SYMBOL_GPL(rtc_unregister);

int rtc_control(rtc_task_t *task, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	spin_lock_irq(&rtc_task_lock);

	if (rtc_callback != task)
		retval = -ENXIO;
	else
		rtc_do_ioctl(cmd, arg, FUNCTION_RTC_CONTROL);

	spin_unlock_irq(&rtc_task_lock);

	return retval;
}

EXPORT_SYMBOL_GPL(rtc_control);

static struct miscdevice rtc_miscdevice = {
	.minor	= RTC_MINOR,
	.name	= rtc_name,
	.fops	= &rtc_fops,
};

static int __devinit rtc_probe(struct platform_device *pdev)
{
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

	retval = misc_register(&rtc_miscdevice);
	if (retval < 0) {
		iounmap(rtc1_base);
		iounmap(rtc2_base);
		rtc1_base = NULL;
		rtc2_base = NULL;
		return retval;
	}

	spin_lock_irq(&rtc_lock);

	rtc1_write(ECMPLREG, 0);
	rtc1_write(ECMPMREG, 0);
	rtc1_write(ECMPHREG, 0);
	rtc1_write(RTCL1LREG, 0);
	rtc1_write(RTCL1HREG, 0);

	rtc_status = RTC_RELEASE;
	rtc_irq_data = 0;

	spin_unlock_irq(&rtc_lock);

	init_waitqueue_head(&rtc_wait);

	irq = ELAPSEDTIME_IRQ;
	retval = request_irq(irq, elapsedtime_interrupt, SA_INTERRUPT,
	                     "elapsed_time", NULL);
	if (retval == 0) {
		irq = RTCLONG1_IRQ;
		retval = request_irq(irq, rtclong1_interrupt, SA_INTERRUPT,
		                     "rtclong1", NULL);
	}

	if (retval < 0) {
		printk(KERN_ERR "rtc: IRQ%d is busy\n", irq);
		if (irq == RTCLONG1_IRQ)
			free_irq(ELAPSEDTIME_IRQ, NULL);
		iounmap(rtc1_base);
		iounmap(rtc2_base);
		rtc1_base = NULL;
		rtc2_base = NULL;
		return retval;
	}

	disable_irq(ELAPSEDTIME_IRQ);
	disable_irq(RTCLONG1_IRQ);

	spin_lock_init(&rtc_task_lock);

	printk(KERN_INFO "rtc: Real Time Clock of NEC VR4100 series\n");

	return 0;
}

static int __devexit rtc_remove(struct platform_device *dev)
{
	int retval;

	retval = misc_deregister(&rtc_miscdevice);
	if (retval < 0)
		return retval;

	free_irq(ELAPSEDTIME_IRQ, NULL);
	free_irq(RTCLONG1_IRQ, NULL);
	if (rtc1_base != NULL)
		iounmap(rtc1_base);
	if (rtc2_base != NULL)
		iounmap(rtc2_base);

	return 0;
}

static struct platform_device *rtc_platform_device;

static struct platform_driver rtc_device_driver = {
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
	if (!rtc_platform_device)
		return -ENOMEM;

	retval = platform_device_add_resources(rtc_platform_device,
				rtc_resource, ARRAY_SIZE(rtc_resource));

	if (retval == 0)
		retval = platform_device_add(rtc_platform_device);

	if (retval < 0) {
		platform_device_put(rtc_platform_device);
		return retval;
	}

	retval = platform_driver_register(&rtc_device_driver);
	if (retval < 0)
		platform_device_unregister(rtc_platform_device);

	return retval;
}

static void __exit vr41xx_rtc_exit(void)
{
	platform_driver_unregister(&rtc_device_driver);
	platform_device_unregister(rtc_platform_device);
}

module_init(vr41xx_rtc_init);
module_exit(vr41xx_rtc_exit);
