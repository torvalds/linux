/*
 * RTC class driver for "CMOS RTC":  PCs, ACPI, etc
 *
 * Copyright (C) 1996 Paul Gortmaker (drivers/char/rtc.c)
 * Copyright (C) 2006 David Brownell (convert to new framework)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * The original "cmos clock" chip was an MC146818 chip, now obsolete.
 * That defined the register interface now provided by all PCs, some
 * non-PC systems, and incorporated into ACPI.  Modern PC chipsets
 * integrate an MC146818 clone in their southbridge, and boards use
 * that instead of discrete clones like the DS12887 or M48T86.  There
 * are also clones that connect using the LPC bus.
 *
 * That register API is also used directly by various other drivers
 * (notably for integrated NVRAM), infrastructure (x86 has code to
 * bypass the RTC framework, directly reading the RTC during boot
 * and updating minutes/seconds for systems using NTP synch) and
 * utilities (like userspace 'hwclock', if no /dev node exists).
 *
 * So **ALL** calls to CMOS_READ and CMOS_WRITE must be done with
 * interrupts disabled, holding the global rtc_lock, to exclude those
 * other drivers and utilities on correctly configured systems.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

/* this is for "generic access to PC-style RTC" using CMOS_READ/CMOS_WRITE */
#include <asm-generic/rtc.h>


struct cmos_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	int			irq;
	struct resource		*iomem;

	void			(*wake_on)(struct device *);
	void			(*wake_off)(struct device *);

	u8			enabled_wake;
	u8			suspend_ctrl;

	/* newer hardware extends the original register set */
	u8			day_alrm;
	u8			mon_alrm;
	u8			century;
};

/* both platform and pnp busses use negative numbers for invalid irqs */
#define is_valid_irq(n)		((n) >= 0)

static const char driver_name[] = "rtc_cmos";

/* The RTC_INTR register may have e.g. RTC_PF set even if RTC_PIE is clear;
 * always mask it against the irq enable bits in RTC_CONTROL.  Bit values
 * are the same: PF==PIE, AF=AIE, UF=UIE; so RTC_IRQMASK works with both.
 */
#define	RTC_IRQMASK	(RTC_PF | RTC_AF | RTC_UF)

static inline int is_intr(u8 rtc_intr)
{
	if (!(rtc_intr & RTC_IRQF))
		return 0;
	return rtc_intr & RTC_IRQMASK;
}

/*----------------------------------------------------------------*/

static int cmos_read_time(struct device *dev, struct rtc_time *t)
{
	/* REVISIT:  if the clock has a "century" register, use
	 * that instead of the heuristic in get_rtc_time().
	 * That'll make Y3K compatility (year > 2070) easy!
	 */
	get_rtc_time(t);
	return 0;
}

static int cmos_set_time(struct device *dev, struct rtc_time *t)
{
	/* REVISIT:  set the "century" register if available
	 *
	 * NOTE: this ignores the issue whereby updating the seconds
	 * takes effect exactly 500ms after we write the register.
	 * (Also queueing and other delays before we get this far.)
	 */
	return set_rtc_time(t);
}

static int cmos_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	rtc_control;

	if (!is_valid_irq(cmos->irq))
		return -EIO;

	/* Basic alarms only support hour, minute, and seconds fields.
	 * Some also support day and month, for alarms up to a year in
	 * the future.
	 */
	t->time.tm_mday = -1;
	t->time.tm_mon = -1;

	spin_lock_irq(&rtc_lock);
	t->time.tm_sec = CMOS_READ(RTC_SECONDS_ALARM);
	t->time.tm_min = CMOS_READ(RTC_MINUTES_ALARM);
	t->time.tm_hour = CMOS_READ(RTC_HOURS_ALARM);

	if (cmos->day_alrm) {
		/* ignore upper bits on readback per ACPI spec */
		t->time.tm_mday = CMOS_READ(cmos->day_alrm) & 0x3f;
		if (!t->time.tm_mday)
			t->time.tm_mday = -1;

		if (cmos->mon_alrm) {
			t->time.tm_mon = CMOS_READ(cmos->mon_alrm);
			if (!t->time.tm_mon)
				t->time.tm_mon = -1;
		}
	}

	rtc_control = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	/* REVISIT this assumes PC style usage:  always BCD */

	if (((unsigned)t->time.tm_sec) < 0x60)
		t->time.tm_sec = BCD2BIN(t->time.tm_sec);
	else
		t->time.tm_sec = -1;
	if (((unsigned)t->time.tm_min) < 0x60)
		t->time.tm_min = BCD2BIN(t->time.tm_min);
	else
		t->time.tm_min = -1;
	if (((unsigned)t->time.tm_hour) < 0x24)
		t->time.tm_hour = BCD2BIN(t->time.tm_hour);
	else
		t->time.tm_hour = -1;

	if (cmos->day_alrm) {
		if (((unsigned)t->time.tm_mday) <= 0x31)
			t->time.tm_mday = BCD2BIN(t->time.tm_mday);
		else
			t->time.tm_mday = -1;
		if (cmos->mon_alrm) {
			if (((unsigned)t->time.tm_mon) <= 0x12)
				t->time.tm_mon = BCD2BIN(t->time.tm_mon) - 1;
			else
				t->time.tm_mon = -1;
		}
	}
	t->time.tm_year = -1;

	t->enabled = !!(rtc_control & RTC_AIE);
	t->pending = 0;

	return 0;
}

static int cmos_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	mon, mday, hrs, min, sec;
	unsigned char	rtc_control, rtc_intr;

	if (!is_valid_irq(cmos->irq))
		return -EIO;

	/* REVISIT this assumes PC style usage:  always BCD */

	/* Writing 0xff means "don't care" or "match all".  */

	mon = t->time.tm_mon;
	mon = (mon < 12) ? BIN2BCD(mon) : 0xff;
	mon++;

	mday = t->time.tm_mday;
	mday = (mday >= 1 && mday <= 31) ? BIN2BCD(mday) : 0xff;

	hrs = t->time.tm_hour;
	hrs = (hrs < 24) ? BIN2BCD(hrs) : 0xff;

	min = t->time.tm_min;
	min = (min < 60) ? BIN2BCD(min) : 0xff;

	sec = t->time.tm_sec;
	sec = (sec < 60) ? BIN2BCD(sec) : 0xff;

	spin_lock_irq(&rtc_lock);

	/* next rtc irq must not be from previous alarm setting */
	rtc_control = CMOS_READ(RTC_CONTROL);
	rtc_control &= ~RTC_AIE;
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	rtc_intr = CMOS_READ(RTC_INTR_FLAGS);
	rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	if (is_intr(rtc_intr))
		rtc_update_irq(cmos->rtc, 1, rtc_intr);

	/* update alarm */
	CMOS_WRITE(hrs, RTC_HOURS_ALARM);
	CMOS_WRITE(min, RTC_MINUTES_ALARM);
	CMOS_WRITE(sec, RTC_SECONDS_ALARM);

	/* the system may support an "enhanced" alarm */
	if (cmos->day_alrm) {
		CMOS_WRITE(mday, cmos->day_alrm);
		if (cmos->mon_alrm)
			CMOS_WRITE(mon, cmos->mon_alrm);
	}

	if (t->enabled) {
		rtc_control |= RTC_AIE;
		CMOS_WRITE(rtc_control, RTC_CONTROL);
		rtc_intr = CMOS_READ(RTC_INTR_FLAGS);
		rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
		if (is_intr(rtc_intr))
			rtc_update_irq(cmos->rtc, 1, rtc_intr);
	}

	spin_unlock_irq(&rtc_lock);

	return 0;
}

static int cmos_irq_set_freq(struct device *dev, int freq)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	int		f;
	unsigned long	flags;

	if (!is_valid_irq(cmos->irq))
		return -ENXIO;

	/* 0 = no irqs; 1 = 2^15 Hz ... 15 = 2^0 Hz */
	f = ffs(freq);
	if (f-- > 16)
		return -EINVAL;
	f = 16 - f;

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(RTC_REF_CLCK_32KHZ | f, RTC_FREQ_SELECT);
	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}

static int cmos_irq_set_state(struct device *dev, int enabled)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	rtc_control, rtc_intr;
	unsigned long	flags;

	if (!is_valid_irq(cmos->irq))
		return -ENXIO;

	spin_lock_irqsave(&rtc_lock, flags);
	rtc_control = CMOS_READ(RTC_CONTROL);

	if (enabled)
		rtc_control |= RTC_PIE;
	else
		rtc_control &= ~RTC_PIE;

	CMOS_WRITE(rtc_control, RTC_CONTROL);

	rtc_intr = CMOS_READ(RTC_INTR_FLAGS);
	rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	if (is_intr(rtc_intr))
		rtc_update_irq(cmos->rtc, 1, rtc_intr);

	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0;
}

#if defined(CONFIG_RTC_INTF_DEV) || defined(CONFIG_RTC_INTF_DEV_MODULE)

static int
cmos_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	rtc_control, rtc_intr;
	unsigned long	flags;

	switch (cmd) {
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
	case RTC_UIE_OFF:
	case RTC_UIE_ON:
	case RTC_PIE_OFF:
	case RTC_PIE_ON:
		if (!is_valid_irq(cmos->irq))
			return -EINVAL;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	spin_lock_irqsave(&rtc_lock, flags);
	rtc_control = CMOS_READ(RTC_CONTROL);
	switch (cmd) {
	case RTC_AIE_OFF:	/* alarm off */
		rtc_control &= ~RTC_AIE;
		break;
	case RTC_AIE_ON:	/* alarm on */
		rtc_control |= RTC_AIE;
		break;
	case RTC_UIE_OFF:	/* update off */
		rtc_control &= ~RTC_UIE;
		break;
	case RTC_UIE_ON:	/* update on */
		rtc_control |= RTC_UIE;
		break;
	case RTC_PIE_OFF:	/* periodic off */
		rtc_control &= ~RTC_PIE;
		break;
	case RTC_PIE_ON:	/* periodic on */
		rtc_control |= RTC_PIE;
		break;
	}
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	rtc_intr = CMOS_READ(RTC_INTR_FLAGS);
	rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	if (is_intr(rtc_intr))
		rtc_update_irq(cmos->rtc, 1, rtc_intr);
	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0;
}

#else
#define	cmos_rtc_ioctl	NULL
#endif

#if defined(CONFIG_RTC_INTF_PROC) || defined(CONFIG_RTC_INTF_PROC_MODULE)

static int cmos_procfs(struct device *dev, struct seq_file *seq)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	rtc_control, valid;

	spin_lock_irq(&rtc_lock);
	rtc_control = CMOS_READ(RTC_CONTROL);
	valid = CMOS_READ(RTC_VALID);
	spin_unlock_irq(&rtc_lock);

	/* NOTE:  at least ICH6 reports battery status using a different
	 * (non-RTC) bit; and SQWE is ignored on many current systems.
	 */
	return seq_printf(seq,
			"periodic_IRQ\t: %s\n"
			"update_IRQ\t: %s\n"
			// "square_wave\t: %s\n"
			// "BCD\t\t: %s\n"
			"DST_enable\t: %s\n"
			"periodic_freq\t: %d\n"
			"batt_status\t: %s\n",
			(rtc_control & RTC_PIE) ? "yes" : "no",
			(rtc_control & RTC_UIE) ? "yes" : "no",
			// (rtc_control & RTC_SQWE) ? "yes" : "no",
			// (rtc_control & RTC_DM_BINARY) ? "no" : "yes",
			(rtc_control & RTC_DST_EN) ? "yes" : "no",
			cmos->rtc->irq_freq,
			(valid & RTC_VRT) ? "okay" : "dead");
}

#else
#define	cmos_procfs	NULL
#endif

static const struct rtc_class_ops cmos_rtc_ops = {
	.ioctl		= cmos_rtc_ioctl,
	.read_time	= cmos_read_time,
	.set_time	= cmos_set_time,
	.read_alarm	= cmos_read_alarm,
	.set_alarm	= cmos_set_alarm,
	.proc		= cmos_procfs,
	.irq_set_freq	= cmos_irq_set_freq,
	.irq_set_state	= cmos_irq_set_state,
};

/*----------------------------------------------------------------*/

/*
 * All these chips have at least 64 bytes of address space, shared by
 * RTC registers and NVRAM.  Most of those bytes of NVRAM are used
 * by boot firmware.  Modern chips have 128 or 256 bytes.
 */

#define NVRAM_OFFSET	(RTC_REG_D + 1)

static ssize_t
cmos_nvram_read(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	int	retval;

	if (unlikely(off >= attr->size))
		return 0;
	if ((off + count) > attr->size)
		count = attr->size - off;

	spin_lock_irq(&rtc_lock);
	for (retval = 0, off += NVRAM_OFFSET; count--; retval++, off++)
		*buf++ = CMOS_READ(off);
	spin_unlock_irq(&rtc_lock);

	return retval;
}

static ssize_t
cmos_nvram_write(struct kobject *kobj, struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct cmos_rtc	*cmos;
	int		retval;

	cmos = dev_get_drvdata(container_of(kobj, struct device, kobj));
	if (unlikely(off >= attr->size))
		return -EFBIG;
	if ((off + count) > attr->size)
		count = attr->size - off;

	/* NOTE:  on at least PCs and Ataris, the boot firmware uses a
	 * checksum on part of the NVRAM data.  That's currently ignored
	 * here.  If userspace is smart enough to know what fields of
	 * NVRAM to update, updating checksums is also part of its job.
	 */
	spin_lock_irq(&rtc_lock);
	for (retval = 0, off += NVRAM_OFFSET; count--; retval++, off++) {
		/* don't trash RTC registers */
		if (off == cmos->day_alrm
				|| off == cmos->mon_alrm
				|| off == cmos->century)
			buf++;
		else
			CMOS_WRITE(*buf++, off);
	}
	spin_unlock_irq(&rtc_lock);

	return retval;
}

static struct bin_attribute nvram = {
	.attr = {
		.name	= "nvram",
		.mode	= S_IRUGO | S_IWUSR,
		.owner	= THIS_MODULE,
	},

	.read	= cmos_nvram_read,
	.write	= cmos_nvram_write,
	/* size gets set up later */
};

/*----------------------------------------------------------------*/

static struct cmos_rtc	cmos_rtc;

static irqreturn_t cmos_interrupt(int irq, void *p)
{
	u8		irqstat;

	spin_lock(&rtc_lock);
	irqstat = CMOS_READ(RTC_INTR_FLAGS);
	irqstat &= (CMOS_READ(RTC_CONTROL) & RTC_IRQMASK) | RTC_IRQF;
	spin_unlock(&rtc_lock);

	if (is_intr(irqstat)) {
		rtc_update_irq(p, 1, irqstat);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

#ifdef	CONFIG_PNP
#define	INITSECTION

#else
#define	INITSECTION	__init
#endif

static int INITSECTION
cmos_do_probe(struct device *dev, struct resource *ports, int rtc_irq)
{
	struct cmos_rtc_board_info	*info = dev->platform_data;
	int				retval = 0;
	unsigned char			rtc_control;
	unsigned			address_space;

	/* there can be only one ... */
	if (cmos_rtc.dev)
		return -EBUSY;

	if (!ports)
		return -ENODEV;

	/* Claim I/O ports ASAP, minimizing conflict with legacy driver.
	 *
	 * REVISIT non-x86 systems may instead use memory space resources
	 * (needing ioremap etc), not i/o space resources like this ...
	 */
	ports = request_region(ports->start,
			ports->end + 1 - ports->start,
			driver_name);
	if (!ports) {
		dev_dbg(dev, "i/o registers already in use\n");
		return -EBUSY;
	}

	cmos_rtc.irq = rtc_irq;
	cmos_rtc.iomem = ports;

	/* Heuristic to deduce NVRAM size ... do what the legacy NVRAM
	 * driver did, but don't reject unknown configs.   Old hardware
	 * won't address 128 bytes, and for now we ignore the way newer
	 * chips can address 256 bytes (using two more i/o ports).
	 */
#if	defined(CONFIG_ATARI)
	address_space = 64;
#elif defined(__i386__) || defined(__x86_64__) || defined(__arm__)
	address_space = 128;
#else
#warning Assuming 128 bytes of RTC+NVRAM address space, not 64 bytes.
	address_space = 128;
#endif

	/* For ACPI systems extension info comes from the FADT.  On others,
	 * board specific setup provides it as appropriate.  Systems where
	 * the alarm IRQ isn't automatically a wakeup IRQ (like ACPI, and
	 * some almost-clones) can provide hooks to make that behave.
	 *
	 * Note that ACPI doesn't preclude putting these registers into
	 * "extended" areas of the chip, including some that we won't yet
	 * expect CMOS_READ and friends to handle.
	 */
	if (info) {
		if (info->rtc_day_alarm && info->rtc_day_alarm < 128)
			cmos_rtc.day_alrm = info->rtc_day_alarm;
		if (info->rtc_mon_alarm && info->rtc_mon_alarm < 128)
			cmos_rtc.mon_alrm = info->rtc_mon_alarm;
		if (info->rtc_century && info->rtc_century < 128)
			cmos_rtc.century = info->rtc_century;

		if (info->wake_on && info->wake_off) {
			cmos_rtc.wake_on = info->wake_on;
			cmos_rtc.wake_off = info->wake_off;
		}
	}

	cmos_rtc.rtc = rtc_device_register(driver_name, dev,
				&cmos_rtc_ops, THIS_MODULE);
	if (IS_ERR(cmos_rtc.rtc)) {
		retval = PTR_ERR(cmos_rtc.rtc);
		goto cleanup0;
	}

	cmos_rtc.dev = dev;
	dev_set_drvdata(dev, &cmos_rtc);
	rename_region(ports, cmos_rtc.rtc->dev.bus_id);

	spin_lock_irq(&rtc_lock);

	/* force periodic irq to CMOS reset default of 1024Hz;
	 *
	 * REVISIT it's been reported that at least one x86_64 ALI mobo
	 * doesn't use 32KHz here ... for portability we might need to
	 * do something about other clock frequencies.
	 */
	CMOS_WRITE(RTC_REF_CLCK_32KHZ | 0x06, RTC_FREQ_SELECT);
	cmos_rtc.rtc->irq_freq = 1024;

	/* disable irqs.
	 *
	 * NOTE after changing RTC_xIE bits we always read INTR_FLAGS;
	 * allegedly some older rtcs need that to handle irqs properly
	 */
	rtc_control = CMOS_READ(RTC_CONTROL);
	rtc_control &= ~(RTC_PIE | RTC_AIE | RTC_UIE);
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);

	spin_unlock_irq(&rtc_lock);

	/* FIXME teach the alarm code how to handle binary mode;
	 * <asm-generic/rtc.h> doesn't know 12-hour mode either.
	 */
	if (!(rtc_control & RTC_24H) || (rtc_control & (RTC_DM_BINARY))) {
		dev_dbg(dev, "only 24-hr BCD mode supported\n");
		retval = -ENXIO;
		goto cleanup1;
	}

	if (is_valid_irq(rtc_irq))
		retval = request_irq(rtc_irq, cmos_interrupt, IRQF_DISABLED,
				cmos_rtc.rtc->dev.bus_id,
				cmos_rtc.rtc);
	if (retval < 0) {
		dev_dbg(dev, "IRQ %d is already in use\n", rtc_irq);
		goto cleanup1;
	}

	/* export at least the first block of NVRAM */
	nvram.size = address_space - NVRAM_OFFSET;
	retval = sysfs_create_bin_file(&dev->kobj, &nvram);
	if (retval < 0) {
		dev_dbg(dev, "can't create nvram file? %d\n", retval);
		goto cleanup2;
	}

	pr_info("%s: alarms up to one %s%s\n",
			cmos_rtc.rtc->dev.bus_id,
			is_valid_irq(rtc_irq)
				?  (cmos_rtc.mon_alrm
					? "year"
					: (cmos_rtc.day_alrm
						? "month" : "day"))
				: "no",
			cmos_rtc.century ? ", y3k" : ""
			);

	return 0;

cleanup2:
	if (is_valid_irq(rtc_irq))
		free_irq(rtc_irq, cmos_rtc.rtc);
cleanup1:
	cmos_rtc.dev = NULL;
	rtc_device_unregister(cmos_rtc.rtc);
cleanup0:
	release_region(ports->start, ports->end + 1 - ports->start);
	return retval;
}

static void cmos_do_shutdown(void)
{
	unsigned char	rtc_control;

	spin_lock_irq(&rtc_lock);
	rtc_control = CMOS_READ(RTC_CONTROL);
	rtc_control &= ~(RTC_PIE|RTC_AIE|RTC_UIE);
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	CMOS_READ(RTC_INTR_FLAGS);
	spin_unlock_irq(&rtc_lock);
}

static void __exit cmos_do_remove(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	struct resource *ports;

	cmos_do_shutdown();

	sysfs_remove_bin_file(&dev->kobj, &nvram);

	if (is_valid_irq(cmos->irq))
		free_irq(cmos->irq, cmos->rtc);

	rtc_device_unregister(cmos->rtc);
	cmos->rtc = NULL;

	ports = cmos->iomem;
	release_region(ports->start, ports->end + 1 - ports->start);
	cmos->iomem = NULL;

	cmos->dev = NULL;
	dev_set_drvdata(dev, NULL);
}

#ifdef	CONFIG_PM

static int cmos_suspend(struct device *dev, pm_message_t mesg)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	int		do_wake = device_may_wakeup(dev);
	unsigned char	tmp;

	/* only the alarm might be a wakeup event source */
	spin_lock_irq(&rtc_lock);
	cmos->suspend_ctrl = tmp = CMOS_READ(RTC_CONTROL);
	if (tmp & (RTC_PIE|RTC_AIE|RTC_UIE)) {
		unsigned char	irqstat;

		if (do_wake)
			tmp &= ~(RTC_PIE|RTC_UIE);
		else
			tmp &= ~(RTC_PIE|RTC_AIE|RTC_UIE);
		CMOS_WRITE(tmp, RTC_CONTROL);
		irqstat = CMOS_READ(RTC_INTR_FLAGS);
		irqstat &= (tmp & RTC_IRQMASK) | RTC_IRQF;
		if (is_intr(irqstat))
			rtc_update_irq(cmos->rtc, 1, irqstat);
	}
	spin_unlock_irq(&rtc_lock);

	if (tmp & RTC_AIE) {
		cmos->enabled_wake = 1;
		if (cmos->wake_on)
			cmos->wake_on(dev);
		else
			enable_irq_wake(cmos->irq);
	}

	pr_debug("%s: suspend%s, ctrl %02x\n",
			cmos_rtc.rtc->dev.bus_id,
			(tmp & RTC_AIE) ? ", alarm may wake" : "",
			tmp);

	return 0;
}

static int cmos_resume(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	tmp = cmos->suspend_ctrl;

	/* re-enable any irqs previously active */
	if (tmp & (RTC_PIE|RTC_AIE|RTC_UIE)) {

		if (cmos->enabled_wake) {
			if (cmos->wake_off)
				cmos->wake_off(dev);
			else
				disable_irq_wake(cmos->irq);
			cmos->enabled_wake = 0;
		}

		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(tmp, RTC_CONTROL);
		tmp = CMOS_READ(RTC_INTR_FLAGS);
		tmp &= (cmos->suspend_ctrl & RTC_IRQMASK) | RTC_IRQF;
		if (is_intr(tmp))
			rtc_update_irq(cmos->rtc, 1, tmp);
		spin_unlock_irq(&rtc_lock);
	}

	pr_debug("%s: resume, ctrl %02x\n",
			cmos_rtc.rtc->dev.bus_id,
			cmos->suspend_ctrl);


	return 0;
}

#else
#define	cmos_suspend	NULL
#define	cmos_resume	NULL
#endif

/*----------------------------------------------------------------*/

/* On non-x86 systems, a "CMOS" RTC lives most naturally on platform_bus.
 * ACPI systems always list these as PNPACPI devices, and pre-ACPI PCs
 * probably list them in similar PNPBIOS tables; so PNP is more common.
 *
 * We don't use legacy "poke at the hardware" probing.  Ancient PCs that
 * predate even PNPBIOS should set up platform_bus devices.
 */

#ifdef	CONFIG_PNP

#include <linux/pnp.h>

static int __devinit
cmos_pnp_probe(struct pnp_dev *pnp, const struct pnp_device_id *id)
{
	/* REVISIT paranoia argues for a shutdown notifier, since PNP
	 * drivers can't provide shutdown() methods to disable IRQs.
	 * Or better yet, fix PNP to allow those methods...
	 */
	if (pnp_port_start(pnp,0) == 0x70 && !pnp_irq_valid(pnp,0))
		/* Some machines contain a PNP entry for the RTC, but
		 * don't define the IRQ. It should always be safe to
		 * hardcode it in these cases
		 */
		return cmos_do_probe(&pnp->dev, &pnp->res.port_resource[0], 8);
	else
		return cmos_do_probe(&pnp->dev,
				     &pnp->res.port_resource[0],
				     pnp->res.irq_resource[0].start);
}

static void __exit cmos_pnp_remove(struct pnp_dev *pnp)
{
	cmos_do_remove(&pnp->dev);
}

#ifdef	CONFIG_PM

static int cmos_pnp_suspend(struct pnp_dev *pnp, pm_message_t mesg)
{
	return cmos_suspend(&pnp->dev, mesg);
}

static int cmos_pnp_resume(struct pnp_dev *pnp)
{
	return cmos_resume(&pnp->dev);
}

#else
#define	cmos_pnp_suspend	NULL
#define	cmos_pnp_resume		NULL
#endif


static const struct pnp_device_id rtc_ids[] = {
	{ .id = "PNP0b00", },
	{ .id = "PNP0b01", },
	{ .id = "PNP0b02", },
	{ },
};
MODULE_DEVICE_TABLE(pnp, rtc_ids);

static struct pnp_driver cmos_pnp_driver = {
	.name		= (char *) driver_name,
	.id_table	= rtc_ids,
	.probe		= cmos_pnp_probe,
	.remove		= __exit_p(cmos_pnp_remove),

	/* flag ensures resume() gets called, and stops syslog spam */
	.flags		= PNP_DRIVER_RES_DO_NOT_CHANGE,
	.suspend	= cmos_pnp_suspend,
	.resume		= cmos_pnp_resume,
};

static int __init cmos_init(void)
{
	return pnp_register_driver(&cmos_pnp_driver);
}
module_init(cmos_init);

static void __exit cmos_exit(void)
{
	pnp_unregister_driver(&cmos_pnp_driver);
}
module_exit(cmos_exit);

#else	/* no PNP */

/*----------------------------------------------------------------*/

/* Platform setup should have set up an RTC device, when PNP is
 * unavailable ... this could happen even on (older) PCs.
 */

static int __init cmos_platform_probe(struct platform_device *pdev)
{
	return cmos_do_probe(&pdev->dev,
			platform_get_resource(pdev, IORESOURCE_IO, 0),
			platform_get_irq(pdev, 0));
}

static int __exit cmos_platform_remove(struct platform_device *pdev)
{
	cmos_do_remove(&pdev->dev);
	return 0;
}

static void cmos_platform_shutdown(struct platform_device *pdev)
{
	cmos_do_shutdown();
}

static struct platform_driver cmos_platform_driver = {
	.remove		= __exit_p(cmos_platform_remove),
	.shutdown	= cmos_platform_shutdown,
	.driver = {
		.name		= (char *) driver_name,
		.suspend	= cmos_suspend,
		.resume		= cmos_resume,
	}
};

static int __init cmos_init(void)
{
	return platform_driver_probe(&cmos_platform_driver,
			cmos_platform_probe);
}
module_init(cmos_init);

static void __exit cmos_exit(void)
{
	platform_driver_unregister(&cmos_platform_driver);
}
module_exit(cmos_exit);


#endif	/* !PNP */

MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("Driver for PC-style 'CMOS' RTCs");
MODULE_LICENSE("GPL");
