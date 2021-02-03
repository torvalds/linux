// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RTC class driver for "CMOS RTC":  PCs, ACPI, etc
 *
 * Copyright (C) 1996 Paul Gortmaker (drivers/char/rtc.c)
 * Copyright (C) 2006 David Brownell (convert to new framework)
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/log2.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#ifdef CONFIG_X86
#include <asm/i8259.h>
#include <asm/processor.h>
#include <linux/dmi.h>
#endif

/* this is for "generic access to PC-style RTC" using CMOS_READ/CMOS_WRITE */
#include <linux/mc146818rtc.h>

#ifdef CONFIG_ACPI
/*
 * Use ACPI SCI to replace HPET interrupt for RTC Alarm event
 *
 * If cleared, ACPI SCI is only used to wake up the system from suspend
 *
 * If set, ACPI SCI is used to handle UIE/AIE and system wakeup
 */

static bool use_acpi_alarm;
module_param(use_acpi_alarm, bool, 0444);

static inline int cmos_use_acpi_alarm(void)
{
	return use_acpi_alarm;
}
#else /* !CONFIG_ACPI */

static inline int cmos_use_acpi_alarm(void)
{
	return 0;
}
#endif

struct cmos_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	int			irq;
	struct resource		*iomem;
	time64_t		alarm_expires;

	void			(*wake_on)(struct device *);
	void			(*wake_off)(struct device *);

	u8			enabled_wake;
	u8			suspend_ctrl;

	/* newer hardware extends the original register set */
	u8			day_alrm;
	u8			mon_alrm;
	u8			century;

	struct rtc_wkalrm	saved_wkalrm;
};

/* both platform and pnp busses use negative numbers for invalid irqs */
#define is_valid_irq(n)		((n) > 0)

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

/* Much modern x86 hardware has HPETs (10+ MHz timers) which, because
 * many BIOS programmers don't set up "sane mode" IRQ routing, are mostly
 * used in a broken "legacy replacement" mode.  The breakage includes
 * HPET #1 hijacking the IRQ for this RTC, and being unavailable for
 * other (better) use.
 *
 * When that broken mode is in use, platform glue provides a partial
 * emulation of hardware RTC IRQ facilities using HPET #1.  We don't
 * want to use HPET for anything except those IRQs though...
 */
#ifdef CONFIG_HPET_EMULATE_RTC
#include <asm/hpet.h>
#else

static inline int is_hpet_enabled(void)
{
	return 0;
}

static inline int hpet_mask_rtc_irq_bit(unsigned long mask)
{
	return 0;
}

static inline int hpet_set_rtc_irq_bit(unsigned long mask)
{
	return 0;
}

static inline int
hpet_set_alarm_time(unsigned char hrs, unsigned char min, unsigned char sec)
{
	return 0;
}

static inline int hpet_set_periodic_freq(unsigned long freq)
{
	return 0;
}

static inline int hpet_rtc_dropped_irq(void)
{
	return 0;
}

static inline int hpet_rtc_timer_init(void)
{
	return 0;
}

extern irq_handler_t hpet_rtc_interrupt;

static inline int hpet_register_irq_handler(irq_handler_t handler)
{
	return 0;
}

static inline int hpet_unregister_irq_handler(irq_handler_t handler)
{
	return 0;
}

#endif

/* Don't use HPET for RTC Alarm event if ACPI Fixed event is used */
static inline int use_hpet_alarm(void)
{
	return is_hpet_enabled() && !cmos_use_acpi_alarm();
}

/*----------------------------------------------------------------*/

#ifdef RTC_PORT

/* Most newer x86 systems have two register banks, the first used
 * for RTC and NVRAM and the second only for NVRAM.  Caller must
 * own rtc_lock ... and we won't worry about access during NMI.
 */
#define can_bank2	true

static inline unsigned char cmos_read_bank2(unsigned char addr)
{
	outb(addr, RTC_PORT(2));
	return inb(RTC_PORT(3));
}

static inline void cmos_write_bank2(unsigned char val, unsigned char addr)
{
	outb(addr, RTC_PORT(2));
	outb(val, RTC_PORT(3));
}

#else

#define can_bank2	false

static inline unsigned char cmos_read_bank2(unsigned char addr)
{
	return 0;
}

static inline void cmos_write_bank2(unsigned char val, unsigned char addr)
{
}

#endif

/*----------------------------------------------------------------*/

static int cmos_read_time(struct device *dev, struct rtc_time *t)
{
	int ret;

	/*
	 * If pm_trace abused the RTC for storage, set the timespec to 0,
	 * which tells the caller that this RTC value is unusable.
	 */
	if (!pm_trace_rtc_valid())
		return -EIO;

	ret = mc146818_get_time(t);
	if (ret < 0) {
		dev_err_ratelimited(dev, "unable to read current time\n");
		return ret;
	}

	return 0;
}

static int cmos_set_time(struct device *dev, struct rtc_time *t)
{
	/* NOTE: this ignores the issue whereby updating the seconds
	 * takes effect exactly 500ms after we write the register.
	 * (Also queueing and other delays before we get this far.)
	 */
	return mc146818_set_time(t);
}

struct cmos_read_alarm_callback_param {
	struct cmos_rtc *cmos;
	struct rtc_time *time;
	unsigned char	rtc_control;
};

static void cmos_read_alarm_callback(unsigned char __always_unused seconds,
				     void *param_in)
{
	struct cmos_read_alarm_callback_param *p =
		(struct cmos_read_alarm_callback_param *)param_in;
	struct rtc_time *time = p->time;

	time->tm_sec = CMOS_READ(RTC_SECONDS_ALARM);
	time->tm_min = CMOS_READ(RTC_MINUTES_ALARM);
	time->tm_hour = CMOS_READ(RTC_HOURS_ALARM);

	if (p->cmos->day_alrm) {
		/* ignore upper bits on readback per ACPI spec */
		time->tm_mday = CMOS_READ(p->cmos->day_alrm) & 0x3f;
		if (!time->tm_mday)
			time->tm_mday = -1;

		if (p->cmos->mon_alrm) {
			time->tm_mon = CMOS_READ(p->cmos->mon_alrm);
			if (!time->tm_mon)
				time->tm_mon = -1;
		}
	}

	p->rtc_control = CMOS_READ(RTC_CONTROL);
}

static int cmos_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	struct cmos_read_alarm_callback_param p = {
		.cmos = cmos,
		.time = &t->time,
	};

	/* This not only a rtc_op, but also called directly */
	if (!is_valid_irq(cmos->irq))
		return -EIO;

	/* Basic alarms only support hour, minute, and seconds fields.
	 * Some also support day and month, for alarms up to a year in
	 * the future.
	 */

	/* Some Intel chipsets disconnect the alarm registers when the clock
	 * update is in progress - during this time reads return bogus values
	 * and writes may fail silently. See for example "7th Generation Intel®
	 * Processor Family I/O for U/Y Platforms [...] Datasheet", section
	 * 27.7.1
	 *
	 * Use the mc146818_avoid_UIP() function to avoid this.
	 */
	if (!mc146818_avoid_UIP(cmos_read_alarm_callback, &p))
		return -EIO;

	if (!(p.rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		if (((unsigned)t->time.tm_sec) < 0x60)
			t->time.tm_sec = bcd2bin(t->time.tm_sec);
		else
			t->time.tm_sec = -1;
		if (((unsigned)t->time.tm_min) < 0x60)
			t->time.tm_min = bcd2bin(t->time.tm_min);
		else
			t->time.tm_min = -1;
		if (((unsigned)t->time.tm_hour) < 0x24)
			t->time.tm_hour = bcd2bin(t->time.tm_hour);
		else
			t->time.tm_hour = -1;

		if (cmos->day_alrm) {
			if (((unsigned)t->time.tm_mday) <= 0x31)
				t->time.tm_mday = bcd2bin(t->time.tm_mday);
			else
				t->time.tm_mday = -1;

			if (cmos->mon_alrm) {
				if (((unsigned)t->time.tm_mon) <= 0x12)
					t->time.tm_mon = bcd2bin(t->time.tm_mon)-1;
				else
					t->time.tm_mon = -1;
			}
		}
	}

	t->enabled = !!(p.rtc_control & RTC_AIE);
	t->pending = 0;

	return 0;
}

static void cmos_checkintr(struct cmos_rtc *cmos, unsigned char rtc_control)
{
	unsigned char	rtc_intr;

	/* NOTE after changing RTC_xIE bits we always read INTR_FLAGS;
	 * allegedly some older rtcs need that to handle irqs properly
	 */
	rtc_intr = CMOS_READ(RTC_INTR_FLAGS);

	if (use_hpet_alarm())
		return;

	rtc_intr &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	if (is_intr(rtc_intr))
		rtc_update_irq(cmos->rtc, 1, rtc_intr);
}

static void cmos_irq_enable(struct cmos_rtc *cmos, unsigned char mask)
{
	unsigned char	rtc_control;

	/* flush any pending IRQ status, notably for update irqs,
	 * before we enable new IRQs
	 */
	rtc_control = CMOS_READ(RTC_CONTROL);
	cmos_checkintr(cmos, rtc_control);

	rtc_control |= mask;
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	if (use_hpet_alarm())
		hpet_set_rtc_irq_bit(mask);

	if ((mask & RTC_AIE) && cmos_use_acpi_alarm()) {
		if (cmos->wake_on)
			cmos->wake_on(cmos->dev);
	}

	cmos_checkintr(cmos, rtc_control);
}

static void cmos_irq_disable(struct cmos_rtc *cmos, unsigned char mask)
{
	unsigned char	rtc_control;

	rtc_control = CMOS_READ(RTC_CONTROL);
	rtc_control &= ~mask;
	CMOS_WRITE(rtc_control, RTC_CONTROL);
	if (use_hpet_alarm())
		hpet_mask_rtc_irq_bit(mask);

	if ((mask & RTC_AIE) && cmos_use_acpi_alarm()) {
		if (cmos->wake_off)
			cmos->wake_off(cmos->dev);
	}

	cmos_checkintr(cmos, rtc_control);
}

static int cmos_validate_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct cmos_rtc *cmos = dev_get_drvdata(dev);
	struct rtc_time now;

	cmos_read_time(dev, &now);

	if (!cmos->day_alrm) {
		time64_t t_max_date;
		time64_t t_alrm;

		t_max_date = rtc_tm_to_time64(&now);
		t_max_date += 24 * 60 * 60 - 1;
		t_alrm = rtc_tm_to_time64(&t->time);
		if (t_alrm > t_max_date) {
			dev_err(dev,
				"Alarms can be up to one day in the future\n");
			return -EINVAL;
		}
	} else if (!cmos->mon_alrm) {
		struct rtc_time max_date = now;
		time64_t t_max_date;
		time64_t t_alrm;
		int max_mday;

		if (max_date.tm_mon == 11) {
			max_date.tm_mon = 0;
			max_date.tm_year += 1;
		} else {
			max_date.tm_mon += 1;
		}
		max_mday = rtc_month_days(max_date.tm_mon, max_date.tm_year);
		if (max_date.tm_mday > max_mday)
			max_date.tm_mday = max_mday;

		t_max_date = rtc_tm_to_time64(&max_date);
		t_max_date -= 1;
		t_alrm = rtc_tm_to_time64(&t->time);
		if (t_alrm > t_max_date) {
			dev_err(dev,
				"Alarms can be up to one month in the future\n");
			return -EINVAL;
		}
	} else {
		struct rtc_time max_date = now;
		time64_t t_max_date;
		time64_t t_alrm;
		int max_mday;

		max_date.tm_year += 1;
		max_mday = rtc_month_days(max_date.tm_mon, max_date.tm_year);
		if (max_date.tm_mday > max_mday)
			max_date.tm_mday = max_mday;

		t_max_date = rtc_tm_to_time64(&max_date);
		t_max_date -= 1;
		t_alrm = rtc_tm_to_time64(&t->time);
		if (t_alrm > t_max_date) {
			dev_err(dev,
				"Alarms can be up to one year in the future\n");
			return -EINVAL;
		}
	}

	return 0;
}

struct cmos_set_alarm_callback_param {
	struct cmos_rtc *cmos;
	unsigned char mon, mday, hrs, min, sec;
	struct rtc_wkalrm *t;
};

/* Note: this function may be executed by mc146818_avoid_UIP() more then
 *	 once
 */
static void cmos_set_alarm_callback(unsigned char __always_unused seconds,
				    void *param_in)
{
	struct cmos_set_alarm_callback_param *p =
		(struct cmos_set_alarm_callback_param *)param_in;

	/* next rtc irq must not be from previous alarm setting */
	cmos_irq_disable(p->cmos, RTC_AIE);

	/* update alarm */
	CMOS_WRITE(p->hrs, RTC_HOURS_ALARM);
	CMOS_WRITE(p->min, RTC_MINUTES_ALARM);
	CMOS_WRITE(p->sec, RTC_SECONDS_ALARM);

	/* the system may support an "enhanced" alarm */
	if (p->cmos->day_alrm) {
		CMOS_WRITE(p->mday, p->cmos->day_alrm);
		if (p->cmos->mon_alrm)
			CMOS_WRITE(p->mon, p->cmos->mon_alrm);
	}

	if (use_hpet_alarm()) {
		/*
		 * FIXME the HPET alarm glue currently ignores day_alrm
		 * and mon_alrm ...
		 */
		hpet_set_alarm_time(p->t->time.tm_hour, p->t->time.tm_min,
				    p->t->time.tm_sec);
	}

	if (p->t->enabled)
		cmos_irq_enable(p->cmos, RTC_AIE);
}

static int cmos_set_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	struct cmos_set_alarm_callback_param p = {
		.cmos = cmos,
		.t = t
	};
	unsigned char rtc_control;
	int ret;

	/* This not only a rtc_op, but also called directly */
	if (!is_valid_irq(cmos->irq))
		return -EIO;

	ret = cmos_validate_alarm(dev, t);
	if (ret < 0)
		return ret;

	p.mon = t->time.tm_mon + 1;
	p.mday = t->time.tm_mday;
	p.hrs = t->time.tm_hour;
	p.min = t->time.tm_min;
	p.sec = t->time.tm_sec;

	spin_lock_irq(&rtc_lock);
	rtc_control = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	if (!(rtc_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		/* Writing 0xff means "don't care" or "match all".  */
		p.mon = (p.mon <= 12) ? bin2bcd(p.mon) : 0xff;
		p.mday = (p.mday >= 1 && p.mday <= 31) ? bin2bcd(p.mday) : 0xff;
		p.hrs = (p.hrs < 24) ? bin2bcd(p.hrs) : 0xff;
		p.min = (p.min < 60) ? bin2bcd(p.min) : 0xff;
		p.sec = (p.sec < 60) ? bin2bcd(p.sec) : 0xff;
	}

	/*
	 * Some Intel chipsets disconnect the alarm registers when the clock
	 * update is in progress - during this time writes fail silently.
	 *
	 * Use mc146818_avoid_UIP() to avoid this.
	 */
	if (!mc146818_avoid_UIP(cmos_set_alarm_callback, &p))
		return -EIO;

	cmos->alarm_expires = rtc_tm_to_time64(&t->time);

	return 0;
}

static int cmos_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned long	flags;

	spin_lock_irqsave(&rtc_lock, flags);

	if (enabled)
		cmos_irq_enable(cmos, RTC_AIE);
	else
		cmos_irq_disable(cmos, RTC_AIE);

	spin_unlock_irqrestore(&rtc_lock, flags);
	return 0;
}

#if IS_ENABLED(CONFIG_RTC_INTF_PROC)

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
	seq_printf(seq,
		   "periodic_IRQ\t: %s\n"
		   "update_IRQ\t: %s\n"
		   "HPET_emulated\t: %s\n"
		   // "square_wave\t: %s\n"
		   "BCD\t\t: %s\n"
		   "DST_enable\t: %s\n"
		   "periodic_freq\t: %d\n"
		   "batt_status\t: %s\n",
		   (rtc_control & RTC_PIE) ? "yes" : "no",
		   (rtc_control & RTC_UIE) ? "yes" : "no",
		   use_hpet_alarm() ? "yes" : "no",
		   // (rtc_control & RTC_SQWE) ? "yes" : "no",
		   (rtc_control & RTC_DM_BINARY) ? "no" : "yes",
		   (rtc_control & RTC_DST_EN) ? "yes" : "no",
		   cmos->rtc->irq_freq,
		   (valid & RTC_VRT) ? "okay" : "dead");

	return 0;
}

#else
#define	cmos_procfs	NULL
#endif

static const struct rtc_class_ops cmos_rtc_ops = {
	.read_time		= cmos_read_time,
	.set_time		= cmos_set_time,
	.read_alarm		= cmos_read_alarm,
	.set_alarm		= cmos_set_alarm,
	.proc			= cmos_procfs,
	.alarm_irq_enable	= cmos_alarm_irq_enable,
};

static const struct rtc_class_ops cmos_rtc_ops_no_alarm = {
	.read_time		= cmos_read_time,
	.set_time		= cmos_set_time,
	.proc			= cmos_procfs,
};

/*----------------------------------------------------------------*/

/*
 * All these chips have at least 64 bytes of address space, shared by
 * RTC registers and NVRAM.  Most of those bytes of NVRAM are used
 * by boot firmware.  Modern chips have 128 or 256 bytes.
 */

#define NVRAM_OFFSET	(RTC_REG_D + 1)

static int cmos_nvram_read(void *priv, unsigned int off, void *val,
			   size_t count)
{
	unsigned char *buf = val;
	int	retval;

	off += NVRAM_OFFSET;
	spin_lock_irq(&rtc_lock);
	for (retval = 0; count; count--, off++, retval++) {
		if (off < 128)
			*buf++ = CMOS_READ(off);
		else if (can_bank2)
			*buf++ = cmos_read_bank2(off);
		else
			break;
	}
	spin_unlock_irq(&rtc_lock);

	return retval;
}

static int cmos_nvram_write(void *priv, unsigned int off, void *val,
			    size_t count)
{
	struct cmos_rtc	*cmos = priv;
	unsigned char	*buf = val;
	int		retval;

	/* NOTE:  on at least PCs and Ataris, the boot firmware uses a
	 * checksum on part of the NVRAM data.  That's currently ignored
	 * here.  If userspace is smart enough to know what fields of
	 * NVRAM to update, updating checksums is also part of its job.
	 */
	off += NVRAM_OFFSET;
	spin_lock_irq(&rtc_lock);
	for (retval = 0; count; count--, off++, retval++) {
		/* don't trash RTC registers */
		if (off == cmos->day_alrm
				|| off == cmos->mon_alrm
				|| off == cmos->century)
			buf++;
		else if (off < 128)
			CMOS_WRITE(*buf++, off);
		else if (can_bank2)
			cmos_write_bank2(*buf++, off);
		else
			break;
	}
	spin_unlock_irq(&rtc_lock);

	return retval;
}

/*----------------------------------------------------------------*/

static struct cmos_rtc	cmos_rtc;

static irqreturn_t cmos_interrupt(int irq, void *p)
{
	u8		irqstat;
	u8		rtc_control;

	spin_lock(&rtc_lock);

	/* When the HPET interrupt handler calls us, the interrupt
	 * status is passed as arg1 instead of the irq number.  But
	 * always clear irq status, even when HPET is in the way.
	 *
	 * Note that HPET and RTC are almost certainly out of phase,
	 * giving different IRQ status ...
	 */
	irqstat = CMOS_READ(RTC_INTR_FLAGS);
	rtc_control = CMOS_READ(RTC_CONTROL);
	if (use_hpet_alarm())
		irqstat = (unsigned long)irq & 0xF0;

	/* If we were suspended, RTC_CONTROL may not be accurate since the
	 * bios may have cleared it.
	 */
	if (!cmos_rtc.suspend_ctrl)
		irqstat &= (rtc_control & RTC_IRQMASK) | RTC_IRQF;
	else
		irqstat &= (cmos_rtc.suspend_ctrl & RTC_IRQMASK) | RTC_IRQF;

	/* All Linux RTC alarms should be treated as if they were oneshot.
	 * Similar code may be needed in system wakeup paths, in case the
	 * alarm woke the system.
	 */
	if (irqstat & RTC_AIE) {
		cmos_rtc.suspend_ctrl &= ~RTC_AIE;
		rtc_control &= ~RTC_AIE;
		CMOS_WRITE(rtc_control, RTC_CONTROL);
		if (use_hpet_alarm())
			hpet_mask_rtc_irq_bit(RTC_AIE);
		CMOS_READ(RTC_INTR_FLAGS);
	}
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
	struct cmos_rtc_board_info	*info = dev_get_platdata(dev);
	int				retval = 0;
	unsigned char			rtc_control;
	unsigned			address_space;
	u32				flags = 0;
	struct nvmem_config nvmem_cfg = {
		.name = "cmos_nvram",
		.word_size = 1,
		.stride = 1,
		.reg_read = cmos_nvram_read,
		.reg_write = cmos_nvram_write,
		.priv = &cmos_rtc,
	};

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
	if (RTC_IOMAPPED)
		ports = request_region(ports->start, resource_size(ports),
				       driver_name);
	else
		ports = request_mem_region(ports->start, resource_size(ports),
					   driver_name);
	if (!ports) {
		dev_dbg(dev, "i/o registers already in use\n");
		return -EBUSY;
	}

	cmos_rtc.irq = rtc_irq;
	cmos_rtc.iomem = ports;

	/* Heuristic to deduce NVRAM size ... do what the legacy NVRAM
	 * driver did, but don't reject unknown configs.   Old hardware
	 * won't address 128 bytes.  Newer chips have multiple banks,
	 * though they may not be listed in one I/O resource.
	 */
#if	defined(CONFIG_ATARI)
	address_space = 64;
#elif defined(__i386__) || defined(__x86_64__) || defined(__arm__) \
			|| defined(__sparc__) || defined(__mips__) \
			|| defined(__powerpc__)
	address_space = 128;
#else
#warning Assuming 128 bytes of RTC+NVRAM address space, not 64 bytes.
	address_space = 128;
#endif
	if (can_bank2 && ports->end > (ports->start + 1))
		address_space = 256;

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
		if (info->flags)
			flags = info->flags;
		if (info->address_space)
			address_space = info->address_space;

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

	cmos_rtc.dev = dev;
	dev_set_drvdata(dev, &cmos_rtc);

	cmos_rtc.rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(cmos_rtc.rtc)) {
		retval = PTR_ERR(cmos_rtc.rtc);
		goto cleanup0;
	}

	rename_region(ports, dev_name(&cmos_rtc.rtc->dev));

	if (!mc146818_does_rtc_work()) {
		dev_warn(dev, "broken or not accessible\n");
		retval = -ENXIO;
		goto cleanup1;
	}

	spin_lock_irq(&rtc_lock);

	if (!(flags & CMOS_RTC_FLAGS_NOFREQ)) {
		/* force periodic irq to CMOS reset default of 1024Hz;
		 *
		 * REVISIT it's been reported that at least one x86_64 ALI
		 * mobo doesn't use 32KHz here ... for portability we might
		 * need to do something about other clock frequencies.
		 */
		cmos_rtc.rtc->irq_freq = 1024;
		if (use_hpet_alarm())
			hpet_set_periodic_freq(cmos_rtc.rtc->irq_freq);
		CMOS_WRITE(RTC_REF_CLCK_32KHZ | 0x06, RTC_FREQ_SELECT);
	}

	/* disable irqs */
	if (is_valid_irq(rtc_irq))
		cmos_irq_disable(&cmos_rtc, RTC_PIE | RTC_AIE | RTC_UIE);

	rtc_control = CMOS_READ(RTC_CONTROL);

	spin_unlock_irq(&rtc_lock);

	if (is_valid_irq(rtc_irq) && !(rtc_control & RTC_24H)) {
		dev_warn(dev, "only 24-hr supported\n");
		retval = -ENXIO;
		goto cleanup1;
	}

	if (use_hpet_alarm())
		hpet_rtc_timer_init();

	if (is_valid_irq(rtc_irq)) {
		irq_handler_t rtc_cmos_int_handler;

		if (use_hpet_alarm()) {
			rtc_cmos_int_handler = hpet_rtc_interrupt;
			retval = hpet_register_irq_handler(cmos_interrupt);
			if (retval) {
				hpet_mask_rtc_irq_bit(RTC_IRQMASK);
				dev_warn(dev, "hpet_register_irq_handler "
						" failed in rtc_init().");
				goto cleanup1;
			}
		} else
			rtc_cmos_int_handler = cmos_interrupt;

		retval = request_irq(rtc_irq, rtc_cmos_int_handler,
				0, dev_name(&cmos_rtc.rtc->dev),
				cmos_rtc.rtc);
		if (retval < 0) {
			dev_dbg(dev, "IRQ %d is already in use\n", rtc_irq);
			goto cleanup1;
		}

		cmos_rtc.rtc->ops = &cmos_rtc_ops;
	} else {
		cmos_rtc.rtc->ops = &cmos_rtc_ops_no_alarm;
	}

	cmos_rtc.rtc->nvram_old_abi = true;
	retval = rtc_register_device(cmos_rtc.rtc);
	if (retval)
		goto cleanup2;

	/* export at least the first block of NVRAM */
	nvmem_cfg.size = address_space - NVRAM_OFFSET;
	if (rtc_nvmem_register(cmos_rtc.rtc, &nvmem_cfg))
		dev_err(dev, "nvmem registration failed\n");

	dev_info(dev, "%s%s, %d bytes nvram%s\n",
		 !is_valid_irq(rtc_irq) ? "no alarms" :
		 cmos_rtc.mon_alrm ? "alarms up to one year" :
		 cmos_rtc.day_alrm ? "alarms up to one month" :
		 "alarms up to one day",
		 cmos_rtc.century ? ", y3k" : "",
		 nvmem_cfg.size,
		 use_hpet_alarm() ? ", hpet irqs" : "");

	return 0;

cleanup2:
	if (is_valid_irq(rtc_irq))
		free_irq(rtc_irq, cmos_rtc.rtc);
cleanup1:
	cmos_rtc.dev = NULL;
cleanup0:
	if (RTC_IOMAPPED)
		release_region(ports->start, resource_size(ports));
	else
		release_mem_region(ports->start, resource_size(ports));
	return retval;
}

static void cmos_do_shutdown(int rtc_irq)
{
	spin_lock_irq(&rtc_lock);
	if (is_valid_irq(rtc_irq))
		cmos_irq_disable(&cmos_rtc, RTC_IRQMASK);
	spin_unlock_irq(&rtc_lock);
}

static void cmos_do_remove(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	struct resource *ports;

	cmos_do_shutdown(cmos->irq);

	if (is_valid_irq(cmos->irq)) {
		free_irq(cmos->irq, cmos->rtc);
		if (use_hpet_alarm())
			hpet_unregister_irq_handler(cmos_interrupt);
	}

	cmos->rtc = NULL;

	ports = cmos->iomem;
	if (RTC_IOMAPPED)
		release_region(ports->start, resource_size(ports));
	else
		release_mem_region(ports->start, resource_size(ports));
	cmos->iomem = NULL;

	cmos->dev = NULL;
}

static int cmos_aie_poweroff(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	struct rtc_time now;
	time64_t t_now;
	int retval = 0;
	unsigned char rtc_control;

	if (!cmos->alarm_expires)
		return -EINVAL;

	spin_lock_irq(&rtc_lock);
	rtc_control = CMOS_READ(RTC_CONTROL);
	spin_unlock_irq(&rtc_lock);

	/* We only care about the situation where AIE is disabled. */
	if (rtc_control & RTC_AIE)
		return -EBUSY;

	cmos_read_time(dev, &now);
	t_now = rtc_tm_to_time64(&now);

	/*
	 * When enabling "RTC wake-up" in BIOS setup, the machine reboots
	 * automatically right after shutdown on some buggy boxes.
	 * This automatic rebooting issue won't happen when the alarm
	 * time is larger than now+1 seconds.
	 *
	 * If the alarm time is equal to now+1 seconds, the issue can be
	 * prevented by cancelling the alarm.
	 */
	if (cmos->alarm_expires == t_now + 1) {
		struct rtc_wkalrm alarm;

		/* Cancel the AIE timer by configuring the past time. */
		rtc_time64_to_tm(t_now - 1, &alarm.time);
		alarm.enabled = 0;
		retval = cmos_set_alarm(dev, &alarm);
	} else if (cmos->alarm_expires > t_now + 1) {
		retval = -EBUSY;
	}

	return retval;
}

static int cmos_suspend(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char	tmp;

	/* only the alarm might be a wakeup event source */
	spin_lock_irq(&rtc_lock);
	cmos->suspend_ctrl = tmp = CMOS_READ(RTC_CONTROL);
	if (tmp & (RTC_PIE|RTC_AIE|RTC_UIE)) {
		unsigned char	mask;

		if (device_may_wakeup(dev))
			mask = RTC_IRQMASK & ~RTC_AIE;
		else
			mask = RTC_IRQMASK;
		tmp &= ~mask;
		CMOS_WRITE(tmp, RTC_CONTROL);
		if (use_hpet_alarm())
			hpet_mask_rtc_irq_bit(mask);
		cmos_checkintr(cmos, tmp);
	}
	spin_unlock_irq(&rtc_lock);

	if ((tmp & RTC_AIE) && !cmos_use_acpi_alarm()) {
		cmos->enabled_wake = 1;
		if (cmos->wake_on)
			cmos->wake_on(dev);
		else
			enable_irq_wake(cmos->irq);
	}

	memset(&cmos->saved_wkalrm, 0, sizeof(struct rtc_wkalrm));
	cmos_read_alarm(dev, &cmos->saved_wkalrm);

	dev_dbg(dev, "suspend%s, ctrl %02x\n",
			(tmp & RTC_AIE) ? ", alarm may wake" : "",
			tmp);

	return 0;
}

/* We want RTC alarms to wake us from e.g. ACPI G2/S5 "soft off", even
 * after a detour through G3 "mechanical off", although the ACPI spec
 * says wakeup should only work from G1/S4 "hibernate".  To most users,
 * distinctions between S4 and S5 are pointless.  So when the hardware
 * allows, don't draw that distinction.
 */
static inline int cmos_poweroff(struct device *dev)
{
	if (!IS_ENABLED(CONFIG_PM))
		return -ENOSYS;

	return cmos_suspend(dev);
}

static void cmos_check_wkalrm(struct device *dev)
{
	struct cmos_rtc *cmos = dev_get_drvdata(dev);
	struct rtc_wkalrm current_alarm;
	time64_t t_now;
	time64_t t_current_expires;
	time64_t t_saved_expires;
	struct rtc_time now;

	/* Check if we have RTC Alarm armed */
	if (!(cmos->suspend_ctrl & RTC_AIE))
		return;

	cmos_read_time(dev, &now);
	t_now = rtc_tm_to_time64(&now);

	/*
	 * ACPI RTC wake event is cleared after resume from STR,
	 * ACK the rtc irq here
	 */
	if (t_now >= cmos->alarm_expires && cmos_use_acpi_alarm()) {
		cmos_interrupt(0, (void *)cmos->rtc);
		return;
	}

	memset(&current_alarm, 0, sizeof(struct rtc_wkalrm));
	cmos_read_alarm(dev, &current_alarm);
	t_current_expires = rtc_tm_to_time64(&current_alarm.time);
	t_saved_expires = rtc_tm_to_time64(&cmos->saved_wkalrm.time);
	if (t_current_expires != t_saved_expires ||
	    cmos->saved_wkalrm.enabled != current_alarm.enabled) {
		cmos_set_alarm(dev, &cmos->saved_wkalrm);
	}
}

static void cmos_check_acpi_rtc_status(struct device *dev,
				       unsigned char *rtc_control);

static int __maybe_unused cmos_resume(struct device *dev)
{
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);
	unsigned char tmp;

	if (cmos->enabled_wake && !cmos_use_acpi_alarm()) {
		if (cmos->wake_off)
			cmos->wake_off(dev);
		else
			disable_irq_wake(cmos->irq);
		cmos->enabled_wake = 0;
	}

	/* The BIOS might have changed the alarm, restore it */
	cmos_check_wkalrm(dev);

	spin_lock_irq(&rtc_lock);
	tmp = cmos->suspend_ctrl;
	cmos->suspend_ctrl = 0;
	/* re-enable any irqs previously active */
	if (tmp & RTC_IRQMASK) {
		unsigned char	mask;

		if (device_may_wakeup(dev) && use_hpet_alarm())
			hpet_rtc_timer_init();

		do {
			CMOS_WRITE(tmp, RTC_CONTROL);
			if (use_hpet_alarm())
				hpet_set_rtc_irq_bit(tmp & RTC_IRQMASK);

			mask = CMOS_READ(RTC_INTR_FLAGS);
			mask &= (tmp & RTC_IRQMASK) | RTC_IRQF;
			if (!use_hpet_alarm() || !is_intr(mask))
				break;

			/* force one-shot behavior if HPET blocked
			 * the wake alarm's irq
			 */
			rtc_update_irq(cmos->rtc, 1, mask);
			tmp &= ~RTC_AIE;
			hpet_mask_rtc_irq_bit(RTC_AIE);
		} while (mask & RTC_AIE);

		if (tmp & RTC_AIE)
			cmos_check_acpi_rtc_status(dev, &tmp);
	}
	spin_unlock_irq(&rtc_lock);

	dev_dbg(dev, "resume, ctrl %02x\n", tmp);

	return 0;
}

static SIMPLE_DEV_PM_OPS(cmos_pm_ops, cmos_suspend, cmos_resume);

/*----------------------------------------------------------------*/

/* On non-x86 systems, a "CMOS" RTC lives most naturally on platform_bus.
 * ACPI systems always list these as PNPACPI devices, and pre-ACPI PCs
 * probably list them in similar PNPBIOS tables; so PNP is more common.
 *
 * We don't use legacy "poke at the hardware" probing.  Ancient PCs that
 * predate even PNPBIOS should set up platform_bus devices.
 */

#ifdef	CONFIG_ACPI

#include <linux/acpi.h>

static u32 rtc_handler(void *context)
{
	struct device *dev = context;
	struct cmos_rtc *cmos = dev_get_drvdata(dev);
	unsigned char rtc_control = 0;
	unsigned char rtc_intr;
	unsigned long flags;


	/*
	 * Always update rtc irq when ACPI is used as RTC Alarm.
	 * Or else, ACPI SCI is enabled during suspend/resume only,
	 * update rtc irq in that case.
	 */
	if (cmos_use_acpi_alarm())
		cmos_interrupt(0, (void *)cmos->rtc);
	else {
		/* Fix me: can we use cmos_interrupt() here as well? */
		spin_lock_irqsave(&rtc_lock, flags);
		if (cmos_rtc.suspend_ctrl)
			rtc_control = CMOS_READ(RTC_CONTROL);
		if (rtc_control & RTC_AIE) {
			cmos_rtc.suspend_ctrl &= ~RTC_AIE;
			CMOS_WRITE(rtc_control, RTC_CONTROL);
			rtc_intr = CMOS_READ(RTC_INTR_FLAGS);
			rtc_update_irq(cmos->rtc, 1, rtc_intr);
		}
		spin_unlock_irqrestore(&rtc_lock, flags);
	}

	pm_wakeup_hard_event(dev);
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
	return ACPI_INTERRUPT_HANDLED;
}

static inline void rtc_wake_setup(struct device *dev)
{
	acpi_install_fixed_event_handler(ACPI_EVENT_RTC, rtc_handler, dev);
	/*
	 * After the RTC handler is installed, the Fixed_RTC event should
	 * be disabled. Only when the RTC alarm is set will it be enabled.
	 */
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_disable_event(ACPI_EVENT_RTC, 0);
}

static void rtc_wake_on(struct device *dev)
{
	acpi_clear_event(ACPI_EVENT_RTC);
	acpi_enable_event(ACPI_EVENT_RTC, 0);
}

static void rtc_wake_off(struct device *dev)
{
	acpi_disable_event(ACPI_EVENT_RTC, 0);
}

#ifdef CONFIG_X86
/* Enable use_acpi_alarm mode for Intel platforms no earlier than 2015 */
static void use_acpi_alarm_quirks(void)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return;

	if (!(acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0))
		return;

	if (!is_hpet_enabled())
		return;

	if (dmi_get_bios_year() < 2015)
		return;

	use_acpi_alarm = true;
}
#else
static inline void use_acpi_alarm_quirks(void) { }
#endif

/* Every ACPI platform has a mc146818 compatible "cmos rtc".  Here we find
 * its device node and pass extra config data.  This helps its driver use
 * capabilities that the now-obsolete mc146818 didn't have, and informs it
 * that this board's RTC is wakeup-capable (per ACPI spec).
 */
static struct cmos_rtc_board_info acpi_rtc_info;

static void cmos_wake_setup(struct device *dev)
{
	if (acpi_disabled)
		return;

	use_acpi_alarm_quirks();

	rtc_wake_setup(dev);
	acpi_rtc_info.wake_on = rtc_wake_on;
	acpi_rtc_info.wake_off = rtc_wake_off;

	/* workaround bug in some ACPI tables */
	if (acpi_gbl_FADT.month_alarm && !acpi_gbl_FADT.day_alarm) {
		dev_dbg(dev, "bogus FADT month_alarm (%d)\n",
			acpi_gbl_FADT.month_alarm);
		acpi_gbl_FADT.month_alarm = 0;
	}

	acpi_rtc_info.rtc_day_alarm = acpi_gbl_FADT.day_alarm;
	acpi_rtc_info.rtc_mon_alarm = acpi_gbl_FADT.month_alarm;
	acpi_rtc_info.rtc_century = acpi_gbl_FADT.century;

	/* NOTE:  S4_RTC_WAKE is NOT currently useful to Linux */
	if (acpi_gbl_FADT.flags & ACPI_FADT_S4_RTC_WAKE)
		dev_info(dev, "RTC can wake from S4\n");

	dev->platform_data = &acpi_rtc_info;

	/* RTC always wakes from S1/S2/S3, and often S4/STD */
	device_init_wakeup(dev, 1);
}

static void cmos_check_acpi_rtc_status(struct device *dev,
				       unsigned char *rtc_control)
{
	struct cmos_rtc *cmos = dev_get_drvdata(dev);
	acpi_event_status rtc_status;
	acpi_status status;

	if (acpi_gbl_FADT.flags & ACPI_FADT_FIXED_RTC)
		return;

	status = acpi_get_event_status(ACPI_EVENT_RTC, &rtc_status);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Could not get RTC status\n");
	} else if (rtc_status & ACPI_EVENT_FLAG_SET) {
		unsigned char mask;
		*rtc_control &= ~RTC_AIE;
		CMOS_WRITE(*rtc_control, RTC_CONTROL);
		mask = CMOS_READ(RTC_INTR_FLAGS);
		rtc_update_irq(cmos->rtc, 1, mask);
	}
}

#else

static void cmos_wake_setup(struct device *dev)
{
}

static void cmos_check_acpi_rtc_status(struct device *dev,
				       unsigned char *rtc_control)
{
}

#endif

#ifdef	CONFIG_PNP

#include <linux/pnp.h>

static int cmos_pnp_probe(struct pnp_dev *pnp, const struct pnp_device_id *id)
{
	cmos_wake_setup(&pnp->dev);

	if (pnp_port_start(pnp, 0) == 0x70 && !pnp_irq_valid(pnp, 0)) {
		unsigned int irq = 0;
#ifdef CONFIG_X86
		/* Some machines contain a PNP entry for the RTC, but
		 * don't define the IRQ. It should always be safe to
		 * hardcode it on systems with a legacy PIC.
		 */
		if (nr_legacy_irqs())
			irq = RTC_IRQ;
#endif
		return cmos_do_probe(&pnp->dev,
				pnp_get_resource(pnp, IORESOURCE_IO, 0), irq);
	} else {
		return cmos_do_probe(&pnp->dev,
				pnp_get_resource(pnp, IORESOURCE_IO, 0),
				pnp_irq(pnp, 0));
	}
}

static void cmos_pnp_remove(struct pnp_dev *pnp)
{
	cmos_do_remove(&pnp->dev);
}

static void cmos_pnp_shutdown(struct pnp_dev *pnp)
{
	struct device *dev = &pnp->dev;
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);

	if (system_state == SYSTEM_POWER_OFF) {
		int retval = cmos_poweroff(dev);

		if (cmos_aie_poweroff(dev) < 0 && !retval)
			return;
	}

	cmos_do_shutdown(cmos->irq);
}

static const struct pnp_device_id rtc_ids[] = {
	{ .id = "PNP0b00", },
	{ .id = "PNP0b01", },
	{ .id = "PNP0b02", },
	{ },
};
MODULE_DEVICE_TABLE(pnp, rtc_ids);

static struct pnp_driver cmos_pnp_driver = {
	.name		= driver_name,
	.id_table	= rtc_ids,
	.probe		= cmos_pnp_probe,
	.remove		= cmos_pnp_remove,
	.shutdown	= cmos_pnp_shutdown,

	/* flag ensures resume() gets called, and stops syslog spam */
	.flags		= PNP_DRIVER_RES_DO_NOT_CHANGE,
	.driver		= {
			.pm = &cmos_pm_ops,
	},
};

#endif	/* CONFIG_PNP */

#ifdef CONFIG_OF
static const struct of_device_id of_cmos_match[] = {
	{
		.compatible = "motorola,mc146818",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, of_cmos_match);

static __init void cmos_of_init(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const __be32 *val;

	if (!node)
		return;

	val = of_get_property(node, "ctrl-reg", NULL);
	if (val)
		CMOS_WRITE(be32_to_cpup(val), RTC_CONTROL);

	val = of_get_property(node, "freq-reg", NULL);
	if (val)
		CMOS_WRITE(be32_to_cpup(val), RTC_FREQ_SELECT);
}
#else
static inline void cmos_of_init(struct platform_device *pdev) {}
#endif
/*----------------------------------------------------------------*/

/* Platform setup should have set up an RTC device, when PNP is
 * unavailable ... this could happen even on (older) PCs.
 */

static int __init cmos_platform_probe(struct platform_device *pdev)
{
	struct resource *resource;
	int irq;

	cmos_of_init(pdev);
	cmos_wake_setup(&pdev->dev);

	if (RTC_IOMAPPED)
		resource = platform_get_resource(pdev, IORESOURCE_IO, 0);
	else
		resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		irq = -1;

	return cmos_do_probe(&pdev->dev, resource, irq);
}

static int cmos_platform_remove(struct platform_device *pdev)
{
	cmos_do_remove(&pdev->dev);
	return 0;
}

static void cmos_platform_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cmos_rtc	*cmos = dev_get_drvdata(dev);

	if (system_state == SYSTEM_POWER_OFF) {
		int retval = cmos_poweroff(dev);

		if (cmos_aie_poweroff(dev) < 0 && !retval)
			return;
	}

	cmos_do_shutdown(cmos->irq);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:rtc_cmos");

static struct platform_driver cmos_platform_driver = {
	.remove		= cmos_platform_remove,
	.shutdown	= cmos_platform_shutdown,
	.driver = {
		.name		= driver_name,
		.pm		= &cmos_pm_ops,
		.of_match_table = of_match_ptr(of_cmos_match),
	}
};

#ifdef CONFIG_PNP
static bool pnp_driver_registered;
#endif
static bool platform_driver_registered;

static int __init cmos_init(void)
{
	int retval = 0;

#ifdef	CONFIG_PNP
	retval = pnp_register_driver(&cmos_pnp_driver);
	if (retval == 0)
		pnp_driver_registered = true;
#endif

	if (!cmos_rtc.dev) {
		retval = platform_driver_probe(&cmos_platform_driver,
					       cmos_platform_probe);
		if (retval == 0)
			platform_driver_registered = true;
	}

	if (retval == 0)
		return 0;

#ifdef	CONFIG_PNP
	if (pnp_driver_registered)
		pnp_unregister_driver(&cmos_pnp_driver);
#endif
	return retval;
}
module_init(cmos_init);

static void __exit cmos_exit(void)
{
#ifdef	CONFIG_PNP
	if (pnp_driver_registered)
		pnp_unregister_driver(&cmos_pnp_driver);
#endif
	if (platform_driver_registered)
		platform_driver_unregister(&cmos_platform_driver);
}
module_exit(cmos_exit);


MODULE_AUTHOR("David Brownell");
MODULE_DESCRIPTION("Driver for PC-style 'CMOS' RTCs");
MODULE_LICENSE("GPL");
