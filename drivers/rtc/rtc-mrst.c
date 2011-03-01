/*
 * rtc-mrst.c: Driver for Moorestown virtual RTC
 *
 * (C) Copyright 2009 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *	   Feng Tang (feng.tang@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Note:
 * VRTC is emulated by system controller firmware, the real HW
 * RTC is located in the PMIC device. SCU FW shadows PMIC RTC
 * in a memory mapped IO space that is visible to the host IA
 * processor.
 *
 * This driver is based upon drivers/rtc/rtc-cmos.c
 */

/*
 * Note:
 *  * vRTC only supports binary mode and 24H mode
 *  * vRTC only support PIE and AIE, no UIE, and its PIE only happens
 *    at 23:59:59pm everyday, no support for adjustable frequency
 *  * Alarm function is also limited to hr/min/sec.
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sfi.h>

#include <asm-generic/rtc.h>
#include <asm/intel_scu_ipc.h>
#include <asm/mrst.h>
#include <asm/mrst-vrtc.h>

struct mrst_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	int			irq;
	struct resource		*iomem;

	u8			enabled_wake;
	u8			suspend_ctrl;
};

static const char driver_name[] = "rtc_mrst";

#define	RTC_IRQMASK	(RTC_PF | RTC_AF)

static inline int is_intr(u8 rtc_intr)
{
	if (!(rtc_intr & RTC_IRQF))
		return 0;
	return rtc_intr & RTC_IRQMASK;
}

/*
 * rtc_time's year contains the increment over 1900, but vRTC's YEAR
 * register can't be programmed to value larger than 0x64, so vRTC
 * driver chose to use 1960 (1970 is UNIX time start point) as the base,
 * and does the translation at read/write time.
 *
 * Why not just use 1970 as the offset? it's because using 1960 will
 * make it consistent in leap year setting for both vrtc and low-level
 * physical rtc devices.
 */
static int mrst_read_time(struct device *dev, struct rtc_time *time)
{
	unsigned long flags;

	if (rtc_is_updating())
		mdelay(20);

	spin_lock_irqsave(&rtc_lock, flags);
	time->tm_sec = vrtc_cmos_read(RTC_SECONDS);
	time->tm_min = vrtc_cmos_read(RTC_MINUTES);
	time->tm_hour = vrtc_cmos_read(RTC_HOURS);
	time->tm_mday = vrtc_cmos_read(RTC_DAY_OF_MONTH);
	time->tm_mon = vrtc_cmos_read(RTC_MONTH);
	time->tm_year = vrtc_cmos_read(RTC_YEAR);
	spin_unlock_irqrestore(&rtc_lock, flags);

	/* Adjust for the 1960/1900 */
	time->tm_year += 60;
	time->tm_mon--;
	return RTC_24H;
}

static int mrst_set_time(struct device *dev, struct rtc_time *time)
{
	int ret;
	unsigned long flags;
	unsigned char mon, day, hrs, min, sec;
	unsigned int yrs;

	yrs = time->tm_year;
	mon = time->tm_mon + 1;   /* tm_mon starts at zero */
	day = time->tm_mday;
	hrs = time->tm_hour;
	min = time->tm_min;
	sec = time->tm_sec;

	if (yrs < 70 || yrs > 138)
		return -EINVAL;
	yrs -= 60;

	spin_lock_irqsave(&rtc_lock, flags);

	vrtc_cmos_write(yrs, RTC_YEAR);
	vrtc_cmos_write(mon, RTC_MONTH);
	vrtc_cmos_write(day, RTC_DAY_OF_MONTH);
	vrtc_cmos_write(hrs, RTC_HOURS);
	vrtc_cmos_write(min, RTC_MINUTES);
	vrtc_cmos_write(sec, RTC_SECONDS);

	spin_unlock_irqrestore(&rtc_lock, flags);

	ret = intel_scu_ipc_simple_command(IPCMSG_VRTC, IPC_CMD_VRTC_SETTIME);
	return ret;
}

static int mrst_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned char rtc_control;

	if (mrst->irq <= 0)
		return -EIO;

	/* Basic alarms only support hour, minute, and seconds fields.
	 * Some also support day and month, for alarms up to a year in
	 * the future.
	 */
	t->time.tm_mday = -1;
	t->time.tm_mon = -1;
	t->time.tm_year = -1;

	/* vRTC only supports binary mode */
	spin_lock_irq(&rtc_lock);
	t->time.tm_sec = vrtc_cmos_read(RTC_SECONDS_ALARM);
	t->time.tm_min = vrtc_cmos_read(RTC_MINUTES_ALARM);
	t->time.tm_hour = vrtc_cmos_read(RTC_HOURS_ALARM);

	rtc_control = vrtc_cmos_read(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	t->enabled = !!(rtc_control & RTC_AIE);
	t->pending = 0;

	return 0;
}

static void mrst_checkintr(struct mrst_rtc *mrst, unsigned char rtc_control)
{
	unsigned char	rtc_intr;

	/*
	 * NOTE after changing RTC_xIE bits we always read INTR_FLAGS;
	 * allegedly some older rtcs need that to handle irqs properly
	 */
	rtc_intr = vrtc_cmos_read(RTC_INTR_FLAGS);
	rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	if (is_intr(rtc_intr))
		rtc_update_irq(mrst->rtc, 1, rtc_intr);
}

static void mrst_irq_enable(struct mrst_rtc *mrst, unsigned char mask)
{
	unsigned char	rtc_control;

	/*
	 * Flush any pending IRQ status, notably for update irqs,
	 * before we enable new IRQs
	 */
	rtc_control = vrtc_cmos_read(RTC_CONTROL);
	mrst_checkintr(mrst, rtc_control);

	rtc_control |= mask;
	vrtc_cmos_write(rtc_control, RTC_CONTROL);

	mrst_checkintr(mrst, rtc_control);
}

static void mrst_irq_disable(struct mrst_rtc *mrst, unsigned char mask)
{
	unsigned char	rtc_control;

	rtc_control = vrtc_cmos_read(RTC_CONTROL);
	rtc_control &= ~mask;
	vrtc_cmos_write(rtc_control, RTC_CONTROL);
	mrst_checkintr(mrst, rtc_control);
}

static int mrst_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned char hrs, min, sec;
	int ret = 0;

	if (!mrst->irq)
		return -EIO;

	hrs = t->time.tm_hour;
	min = t->time.tm_min;
	sec = t->time.tm_sec;

	spin_lock_irq(&rtc_lock);
	/* Next rtc irq must not be from previous alarm setting */
	mrst_irq_disable(mrst, RTC_AIE);

	/* Update alarm */
	vrtc_cmos_write(hrs, RTC_HOURS_ALARM);
	vrtc_cmos_write(min, RTC_MINUTES_ALARM);
	vrtc_cmos_write(sec, RTC_SECONDS_ALARM);

	spin_unlock_irq(&rtc_lock);

	ret = intel_scu_ipc_simple_command(IPCMSG_VRTC, IPC_CMD_VRTC_SETALARM);
	if (ret)
		return ret;

	spin_lock_irq(&rtc_lock);
	if (t->enabled)
		mrst_irq_enable(mrst, RTC_AIE);

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int mrst_irq_set_state(struct device *dev, int enabled)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned long	flags;

	if (!mrst->irq)
		return -ENXIO;

	spin_lock_irqsave(&rtc_lock, flags);

	if (enabled)
		mrst_irq_enable(mrst, RTC_PIE);
	else
		mrst_irq_disable(mrst, RTC_PIE);

	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0;
}

/* Currently, the vRTC doesn't support UIE ON/OFF */
static int mrst_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned long	flags;

	spin_lock_irqsave(&rtc_lock, flags);
	if (enabled)
		mrst_irq_enable(mrst, RTC_AIE);
	else
		mrst_irq_disable(mrst, RTC_AIE);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0;
}


#if defined(CONFIG_RTC_INTF_PROC) || defined(CONFIG_RTC_INTF_PROC_MODULE)

static int mrst_procfs(struct device *dev, struct seq_file *seq)
{
	unsigned char	rtc_control, valid;

	spin_lock_irq(&rtc_lock);
	rtc_control = vrtc_cmos_read(RTC_CONTROL);
	valid = vrtc_cmos_read(RTC_VALID);
	spin_unlock_irq(&rtc_lock);

	return seq_printf(seq,
			"periodic_IRQ\t: %s\n"
			"alarm\t\t: %s\n"
			"BCD\t\t: no\n"
			"periodic_freq\t: daily (not adjustable)\n",
			(rtc_control & RTC_PIE) ? "on" : "off",
			(rtc_control & RTC_AIE) ? "on" : "off");
}

#else
#define	mrst_procfs	NULL
#endif

static const struct rtc_class_ops mrst_rtc_ops = {
	.read_time	= mrst_read_time,
	.set_time	= mrst_set_time,
	.read_alarm	= mrst_read_alarm,
	.set_alarm	= mrst_set_alarm,
	.proc		= mrst_procfs,
	.irq_set_state	= mrst_irq_set_state,
	.alarm_irq_enable = mrst_rtc_alarm_irq_enable,
};

static struct mrst_rtc	mrst_rtc;

/*
 * When vRTC IRQ is captured by SCU FW, FW will clear the AIE bit in
 * Reg B, so no need for this driver to clear it
 */
static irqreturn_t mrst_rtc_irq(int irq, void *p)
{
	u8 irqstat;

	spin_lock(&rtc_lock);
	/* This read will clear all IRQ flags inside Reg C */
	irqstat = vrtc_cmos_read(RTC_INTR_FLAGS);
	spin_unlock(&rtc_lock);

	irqstat &= RTC_IRQMASK | RTC_IRQF;
	if (is_intr(irqstat)) {
		rtc_update_irq(p, 1, irqstat);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int __init
vrtc_mrst_do_probe(struct device *dev, struct resource *iomem, int rtc_irq)
{
	int retval = 0;
	unsigned char rtc_control;

	/* There can be only one ... */
	if (mrst_rtc.dev)
		return -EBUSY;

	if (!iomem)
		return -ENODEV;

	iomem = request_mem_region(iomem->start,
			iomem->end + 1 - iomem->start,
			driver_name);
	if (!iomem) {
		dev_dbg(dev, "i/o mem already in use.\n");
		return -EBUSY;
	}

	mrst_rtc.irq = rtc_irq;
	mrst_rtc.iomem = iomem;

	mrst_rtc.rtc = rtc_device_register(driver_name, dev,
				&mrst_rtc_ops, THIS_MODULE);
	if (IS_ERR(mrst_rtc.rtc)) {
		retval = PTR_ERR(mrst_rtc.rtc);
		goto cleanup0;
	}

	mrst_rtc.dev = dev;
	dev_set_drvdata(dev, &mrst_rtc);
	rename_region(iomem, dev_name(&mrst_rtc.rtc->dev));

	spin_lock_irq(&rtc_lock);
	mrst_irq_disable(&mrst_rtc, RTC_PIE | RTC_AIE);
	rtc_control = vrtc_cmos_read(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	if (!(rtc_control & RTC_24H) || (rtc_control & (RTC_DM_BINARY)))
		dev_dbg(dev, "TODO: support more than 24-hr BCD mode\n");

	if (rtc_irq) {
		retval = request_irq(rtc_irq, mrst_rtc_irq,
				IRQF_DISABLED, dev_name(&mrst_rtc.rtc->dev),
				mrst_rtc.rtc);
		if (retval < 0) {
			dev_dbg(dev, "IRQ %d is already in use, err %d\n",
				rtc_irq, retval);
			goto cleanup1;
		}
	}
	dev_dbg(dev, "initialised\n");
	return 0;

cleanup1:
	mrst_rtc.dev = NULL;
	rtc_device_unregister(mrst_rtc.rtc);
cleanup0:
	release_region(iomem->start, iomem->end + 1 - iomem->start);
	dev_err(dev, "rtc-mrst: unable to initialise\n");
	return retval;
}

static void rtc_mrst_do_shutdown(void)
{
	spin_lock_irq(&rtc_lock);
	mrst_irq_disable(&mrst_rtc, RTC_IRQMASK);
	spin_unlock_irq(&rtc_lock);
}

static void __exit rtc_mrst_do_remove(struct device *dev)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	struct resource *iomem;

	rtc_mrst_do_shutdown();

	if (mrst->irq)
		free_irq(mrst->irq, mrst->rtc);

	rtc_device_unregister(mrst->rtc);
	mrst->rtc = NULL;

	iomem = mrst->iomem;
	release_region(iomem->start, iomem->end + 1 - iomem->start);
	mrst->iomem = NULL;

	mrst->dev = NULL;
	dev_set_drvdata(dev, NULL);
}

#ifdef	CONFIG_PM
static int mrst_suspend(struct device *dev, pm_message_t mesg)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned char	tmp;

	/* Only the alarm might be a wakeup event source */
	spin_lock_irq(&rtc_lock);
	mrst->suspend_ctrl = tmp = vrtc_cmos_read(RTC_CONTROL);
	if (tmp & (RTC_PIE | RTC_AIE)) {
		unsigned char	mask;

		if (device_may_wakeup(dev))
			mask = RTC_IRQMASK & ~RTC_AIE;
		else
			mask = RTC_IRQMASK;
		tmp &= ~mask;
		vrtc_cmos_write(tmp, RTC_CONTROL);

		mrst_checkintr(mrst, tmp);
	}
	spin_unlock_irq(&rtc_lock);

	if (tmp & RTC_AIE) {
		mrst->enabled_wake = 1;
		enable_irq_wake(mrst->irq);
	}

	dev_dbg(&mrst_rtc.rtc->dev, "suspend%s, ctrl %02x\n",
			(tmp & RTC_AIE) ? ", alarm may wake" : "",
			tmp);

	return 0;
}

/*
 * We want RTC alarms to wake us from the deep power saving state
 */
static inline int mrst_poweroff(struct device *dev)
{
	return mrst_suspend(dev, PMSG_HIBERNATE);
}

static int mrst_resume(struct device *dev)
{
	struct mrst_rtc	*mrst = dev_get_drvdata(dev);
	unsigned char tmp = mrst->suspend_ctrl;

	/* Re-enable any irqs previously active */
	if (tmp & RTC_IRQMASK) {
		unsigned char	mask;

		if (mrst->enabled_wake) {
			disable_irq_wake(mrst->irq);
			mrst->enabled_wake = 0;
		}

		spin_lock_irq(&rtc_lock);
		do {
			vrtc_cmos_write(tmp, RTC_CONTROL);

			mask = vrtc_cmos_read(RTC_INTR_FLAGS);
			mask &= (tmp & RTC_IRQMASK) | RTC_IRQF;
			if (!is_intr(mask))
				break;

			rtc_update_irq(mrst->rtc, 1, mask);
			tmp &= ~RTC_AIE;
		} while (mask & RTC_AIE);
		spin_unlock_irq(&rtc_lock);
	}

	dev_dbg(&mrst_rtc.rtc->dev, "resume, ctrl %02x\n", tmp);

	return 0;
}

#else
#define	mrst_suspend	NULL
#define	mrst_resume	NULL

static inline int mrst_poweroff(struct device *dev)
{
	return -ENOSYS;
}

#endif

static int __init vrtc_mrst_platform_probe(struct platform_device *pdev)
{
	return vrtc_mrst_do_probe(&pdev->dev,
			platform_get_resource(pdev, IORESOURCE_MEM, 0),
			platform_get_irq(pdev, 0));
}

static int __exit vrtc_mrst_platform_remove(struct platform_device *pdev)
{
	rtc_mrst_do_remove(&pdev->dev);
	return 0;
}

static void vrtc_mrst_platform_shutdown(struct platform_device *pdev)
{
	if (system_state == SYSTEM_POWER_OFF && !mrst_poweroff(&pdev->dev))
		return;

	rtc_mrst_do_shutdown();
}

MODULE_ALIAS("platform:vrtc_mrst");

static struct platform_driver vrtc_mrst_platform_driver = {
	.probe		= vrtc_mrst_platform_probe,
	.remove		= __exit_p(vrtc_mrst_platform_remove),
	.shutdown	= vrtc_mrst_platform_shutdown,
	.driver = {
		.name		= (char *) driver_name,
		.suspend	= mrst_suspend,
		.resume		= mrst_resume,
	}
};

static int __init vrtc_mrst_init(void)
{
	return platform_driver_register(&vrtc_mrst_platform_driver);
}

static void __exit vrtc_mrst_exit(void)
{
	platform_driver_unregister(&vrtc_mrst_platform_driver);
}

module_init(vrtc_mrst_init);
module_exit(vrtc_mrst_exit);

MODULE_AUTHOR("Jacob Pan; Feng Tang");
MODULE_DESCRIPTION("Driver for Moorestown virtual RTC");
MODULE_LICENSE("GPL");
