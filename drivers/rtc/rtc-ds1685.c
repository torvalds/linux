// SPDX-License-Identifier: GPL-2.0-only
/*
 * An rtc driver for the Dallas/Maxim DS1685/DS1687 and related real-time
 * chips.
 *
 * Copyright (C) 2011-2014 Joshua Kinard <linux@kumba.dev>.
 * Copyright (C) 2009 Matthias Fuchs <matthias.fuchs@esd-electronics.com>.
 *
 * References:
 *    DS1685/DS1687 3V/5V Real-Time Clocks, 19-5215, Rev 4/10.
 *    DS17x85/DS17x87 3V/5V Real-Time Clocks, 19-5222, Rev 4/10.
 *    DS1689/DS1693 3V/5V Serialized Real-Time Clocks, Rev 112105.
 *    Application Note 90, Using the Multiplex Bus RTC Extended Features.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>

#include <linux/rtc/ds1685.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif


/* ----------------------------------------------------------------------- */
/*
 *  Standard read/write
 *  all registers are mapped in CPU address space
 */

/**
 * ds1685_read - read a value from an rtc register.
 * @rtc: pointer to the ds1685 rtc structure.
 * @reg: the register address to read.
 */
static u8
ds1685_read(struct ds1685_priv *rtc, int reg)
{
	return readb((u8 __iomem *)rtc->regs +
		     (reg * rtc->regstep));
}

/**
 * ds1685_write - write a value to an rtc register.
 * @rtc: pointer to the ds1685 rtc structure.
 * @reg: the register address to write.
 * @value: value to write to the register.
 */
static void
ds1685_write(struct ds1685_priv *rtc, int reg, u8 value)
{
	writeb(value, ((u8 __iomem *)rtc->regs +
		       (reg * rtc->regstep)));
}
/* ----------------------------------------------------------------------- */

/*
 * Indirect read/write functions
 * access happens via address and data register mapped in CPU address space
 */

/**
 * ds1685_indirect_read - read a value from an rtc register.
 * @rtc: pointer to the ds1685 rtc structure.
 * @reg: the register address to read.
 */
static u8
ds1685_indirect_read(struct ds1685_priv *rtc, int reg)
{
	writeb(reg, rtc->regs);
	return readb(rtc->data);
}

/**
 * ds1685_indirect_write - write a value to an rtc register.
 * @rtc: pointer to the ds1685 rtc structure.
 * @reg: the register address to write.
 * @value: value to write to the register.
 */
static void
ds1685_indirect_write(struct ds1685_priv *rtc, int reg, u8 value)
{
	writeb(reg, rtc->regs);
	writeb(value, rtc->data);
}

/* ----------------------------------------------------------------------- */
/* Inlined functions */

/**
 * ds1685_rtc_bcd2bin - bcd2bin wrapper in case platform doesn't support BCD.
 * @rtc: pointer to the ds1685 rtc structure.
 * @val: u8 time value to consider converting.
 * @bcd_mask: u8 mask value if BCD mode is used.
 * @bin_mask: u8 mask value if BIN mode is used.
 *
 * Returns the value, converted to BIN if originally in BCD and bcd_mode TRUE.
 */
static inline u8
ds1685_rtc_bcd2bin(struct ds1685_priv *rtc, u8 val, u8 bcd_mask, u8 bin_mask)
{
	if (rtc->bcd_mode)
		return (bcd2bin(val) & bcd_mask);

	return (val & bin_mask);
}

/**
 * ds1685_rtc_bin2bcd - bin2bcd wrapper in case platform doesn't support BCD.
 * @rtc: pointer to the ds1685 rtc structure.
 * @val: u8 time value to consider converting.
 * @bin_mask: u8 mask value if BIN mode is used.
 * @bcd_mask: u8 mask value if BCD mode is used.
 *
 * Returns the value, converted to BCD if originally in BIN and bcd_mode TRUE.
 */
static inline u8
ds1685_rtc_bin2bcd(struct ds1685_priv *rtc, u8 val, u8 bin_mask, u8 bcd_mask)
{
	if (rtc->bcd_mode)
		return (bin2bcd(val) & bcd_mask);

	return (val & bin_mask);
}

/**
 * ds1685_rtc_check_mday - check validity of the day of month.
 * @rtc: pointer to the ds1685 rtc structure.
 * @mday: day of month.
 *
 * Returns -EDOM if the day of month is not within 1..31 range.
 */
static inline int
ds1685_rtc_check_mday(struct ds1685_priv *rtc, u8 mday)
{
	if (rtc->bcd_mode) {
		if (mday < 0x01 || mday > 0x31 || (mday & 0x0f) > 0x09)
			return -EDOM;
	} else {
		if (mday < 1 || mday > 31)
			return -EDOM;
	}
	return 0;
}

/**
 * ds1685_rtc_switch_to_bank0 - switch the rtc to bank 0.
 * @rtc: pointer to the ds1685 rtc structure.
 */
static inline void
ds1685_rtc_switch_to_bank0(struct ds1685_priv *rtc)
{
	rtc->write(rtc, RTC_CTRL_A,
		   (rtc->read(rtc, RTC_CTRL_A) & ~(RTC_CTRL_A_DV0)));
}

/**
 * ds1685_rtc_switch_to_bank1 - switch the rtc to bank 1.
 * @rtc: pointer to the ds1685 rtc structure.
 */
static inline void
ds1685_rtc_switch_to_bank1(struct ds1685_priv *rtc)
{
	rtc->write(rtc, RTC_CTRL_A,
		   (rtc->read(rtc, RTC_CTRL_A) | RTC_CTRL_A_DV0));
}

/**
 * ds1685_rtc_begin_data_access - prepare the rtc for data access.
 * @rtc: pointer to the ds1685 rtc structure.
 *
 * This takes several steps to prepare the rtc for access to get/set time
 * and alarm values from the rtc registers:
 *  - Sets the SET bit in Control Register B.
 *  - Reads Ext Control Register 4A and checks the INCR bit.
 *  - If INCR is active, a short delay is added before Ext Control Register 4A
 *    is read again in a loop until INCR is inactive.
 *  - Switches the rtc to bank 1.  This allows access to all relevant
 *    data for normal rtc operation, as bank 0 contains only the nvram.
 */
static inline void
ds1685_rtc_begin_data_access(struct ds1685_priv *rtc)
{
	/* Set the SET bit in Ctrl B */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) | RTC_CTRL_B_SET));

	/* Switch to Bank 1 */
	ds1685_rtc_switch_to_bank1(rtc);

	/* Read Ext Ctrl 4A and check the INCR bit to avoid a lockout. */
	while (rtc->read(rtc, RTC_EXT_CTRL_4A) & RTC_CTRL_4A_INCR)
		cpu_relax();
}

/**
 * ds1685_rtc_end_data_access - end data access on the rtc.
 * @rtc: pointer to the ds1685 rtc structure.
 *
 * This ends what was started by ds1685_rtc_begin_data_access:
 *  - Switches the rtc back to bank 0.
 *  - Clears the SET bit in Control Register B.
 */
static inline void
ds1685_rtc_end_data_access(struct ds1685_priv *rtc)
{
	/* Switch back to Bank 0 */
	ds1685_rtc_switch_to_bank0(rtc);

	/* Clear the SET bit in Ctrl B */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) & ~(RTC_CTRL_B_SET)));
}

/**
 * ds1685_rtc_get_ssn - retrieve the silicon serial number.
 * @rtc: pointer to the ds1685 rtc structure.
 * @ssn: u8 array to hold the bits of the silicon serial number.
 *
 * This number starts at 0x40, and is 8-bytes long, ending at 0x47. The
 * first byte is the model number, the next six bytes are the serial number
 * digits, and the final byte is a CRC check byte.  Together, they form the
 * silicon serial number.
 *
 * These values are stored in bank1, so ds1685_rtc_switch_to_bank1 must be
 * called first before calling this function, else data will be read out of
 * the bank0 NVRAM.  Be sure to call ds1685_rtc_switch_to_bank0 when done.
 */
static inline void
ds1685_rtc_get_ssn(struct ds1685_priv *rtc, u8 *ssn)
{
	ssn[0] = rtc->read(rtc, RTC_BANK1_SSN_MODEL);
	ssn[1] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_1);
	ssn[2] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_2);
	ssn[3] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_3);
	ssn[4] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_4);
	ssn[5] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_5);
	ssn[6] = rtc->read(rtc, RTC_BANK1_SSN_BYTE_6);
	ssn[7] = rtc->read(rtc, RTC_BANK1_SSN_CRC);
}
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* Read/Set Time & Alarm functions */

/**
 * ds1685_rtc_read_time - reads the time registers.
 * @dev: pointer to device structure.
 * @tm: pointer to rtc_time structure.
 */
static int
ds1685_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);
	u8 century;
	u8 seconds, minutes, hours, wday, mday, month, years;

	/* Fetch the time info from the RTC registers. */
	ds1685_rtc_begin_data_access(rtc);
	seconds = rtc->read(rtc, RTC_SECS);
	minutes = rtc->read(rtc, RTC_MINS);
	hours   = rtc->read(rtc, RTC_HRS);
	wday    = rtc->read(rtc, RTC_WDAY);
	mday    = rtc->read(rtc, RTC_MDAY);
	month   = rtc->read(rtc, RTC_MONTH);
	years   = rtc->read(rtc, RTC_YEAR);
	century = rtc->read(rtc, RTC_CENTURY);
	ds1685_rtc_end_data_access(rtc);

	/* bcd2bin if needed, perform fixups, and store to rtc_time. */
	years        = ds1685_rtc_bcd2bin(rtc, years, RTC_YEAR_BCD_MASK,
					  RTC_YEAR_BIN_MASK);
	century      = ds1685_rtc_bcd2bin(rtc, century, RTC_CENTURY_MASK,
					  RTC_CENTURY_MASK);
	tm->tm_sec   = ds1685_rtc_bcd2bin(rtc, seconds, RTC_SECS_BCD_MASK,
					  RTC_SECS_BIN_MASK);
	tm->tm_min   = ds1685_rtc_bcd2bin(rtc, minutes, RTC_MINS_BCD_MASK,
					  RTC_MINS_BIN_MASK);
	tm->tm_hour  = ds1685_rtc_bcd2bin(rtc, hours, RTC_HRS_24_BCD_MASK,
					  RTC_HRS_24_BIN_MASK);
	tm->tm_wday  = (ds1685_rtc_bcd2bin(rtc, wday, RTC_WDAY_MASK,
					   RTC_WDAY_MASK) - 1);
	tm->tm_mday  = ds1685_rtc_bcd2bin(rtc, mday, RTC_MDAY_BCD_MASK,
					  RTC_MDAY_BIN_MASK);
	tm->tm_mon   = (ds1685_rtc_bcd2bin(rtc, month, RTC_MONTH_BCD_MASK,
					   RTC_MONTH_BIN_MASK) - 1);
	tm->tm_year  = ((years + (century * 100)) - 1900);
	tm->tm_yday  = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);
	tm->tm_isdst = 0; /* RTC has hardcoded timezone, so don't use. */

	return 0;
}

/**
 * ds1685_rtc_set_time - sets the time registers.
 * @dev: pointer to device structure.
 * @tm: pointer to rtc_time structure.
 */
static int
ds1685_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);
	u8 ctrlb, seconds, minutes, hours, wday, mday, month, years, century;

	/* Fetch the time info from rtc_time. */
	seconds = ds1685_rtc_bin2bcd(rtc, tm->tm_sec, RTC_SECS_BIN_MASK,
				     RTC_SECS_BCD_MASK);
	minutes = ds1685_rtc_bin2bcd(rtc, tm->tm_min, RTC_MINS_BIN_MASK,
				     RTC_MINS_BCD_MASK);
	hours   = ds1685_rtc_bin2bcd(rtc, tm->tm_hour, RTC_HRS_24_BIN_MASK,
				     RTC_HRS_24_BCD_MASK);
	wday    = ds1685_rtc_bin2bcd(rtc, (tm->tm_wday + 1), RTC_WDAY_MASK,
				     RTC_WDAY_MASK);
	mday    = ds1685_rtc_bin2bcd(rtc, tm->tm_mday, RTC_MDAY_BIN_MASK,
				     RTC_MDAY_BCD_MASK);
	month   = ds1685_rtc_bin2bcd(rtc, (tm->tm_mon + 1), RTC_MONTH_BIN_MASK,
				     RTC_MONTH_BCD_MASK);
	years   = ds1685_rtc_bin2bcd(rtc, (tm->tm_year % 100),
				     RTC_YEAR_BIN_MASK, RTC_YEAR_BCD_MASK);
	century = ds1685_rtc_bin2bcd(rtc, ((tm->tm_year + 1900) / 100),
				     RTC_CENTURY_MASK, RTC_CENTURY_MASK);

	/*
	 * Perform Sanity Checks:
	 *   - Months: !> 12, Month Day != 0.
	 *   - Month Day !> Max days in current month.
	 *   - Hours !>= 24, Mins !>= 60, Secs !>= 60, & Weekday !> 7.
	 */
	if ((tm->tm_mon > 11) || (mday == 0))
		return -EDOM;

	if (tm->tm_mday > rtc_month_days(tm->tm_mon, tm->tm_year))
		return -EDOM;

	if ((tm->tm_hour >= 24) || (tm->tm_min >= 60) ||
	    (tm->tm_sec >= 60)  || (wday > 7))
		return -EDOM;

	/*
	 * Set the data mode to use and store the time values in the
	 * RTC registers.
	 */
	ds1685_rtc_begin_data_access(rtc);
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	if (rtc->bcd_mode)
		ctrlb &= ~(RTC_CTRL_B_DM);
	else
		ctrlb |= RTC_CTRL_B_DM;
	rtc->write(rtc, RTC_CTRL_B, ctrlb);
	rtc->write(rtc, RTC_SECS, seconds);
	rtc->write(rtc, RTC_MINS, minutes);
	rtc->write(rtc, RTC_HRS, hours);
	rtc->write(rtc, RTC_WDAY, wday);
	rtc->write(rtc, RTC_MDAY, mday);
	rtc->write(rtc, RTC_MONTH, month);
	rtc->write(rtc, RTC_YEAR, years);
	rtc->write(rtc, RTC_CENTURY, century);
	ds1685_rtc_end_data_access(rtc);

	return 0;
}

/**
 * ds1685_rtc_read_alarm - reads the alarm registers.
 * @dev: pointer to device structure.
 * @alrm: pointer to rtc_wkalrm structure.
 *
 * There are three primary alarm registers: seconds, minutes, and hours.
 * A fourth alarm register for the month date is also available in bank1 for
 * kickstart/wakeup features.  The DS1685/DS1687 manual states that a
 * "don't care" value ranging from 0xc0 to 0xff may be written into one or
 * more of the three alarm bytes to act as a wildcard value.  The fourth
 * byte doesn't support a "don't care" value.
 */
static int
ds1685_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);
	u8 seconds, minutes, hours, mday, ctrlb, ctrlc;
	int ret;

	/* Fetch the alarm info from the RTC alarm registers. */
	ds1685_rtc_begin_data_access(rtc);
	seconds	= rtc->read(rtc, RTC_SECS_ALARM);
	minutes	= rtc->read(rtc, RTC_MINS_ALARM);
	hours	= rtc->read(rtc, RTC_HRS_ALARM);
	mday	= rtc->read(rtc, RTC_MDAY_ALARM);
	ctrlb	= rtc->read(rtc, RTC_CTRL_B);
	ctrlc	= rtc->read(rtc, RTC_CTRL_C);
	ds1685_rtc_end_data_access(rtc);

	/* Check the month date for validity. */
	ret = ds1685_rtc_check_mday(rtc, mday);
	if (ret)
		return ret;

	/*
	 * Check the three alarm bytes.
	 *
	 * The Linux RTC system doesn't support the "don't care" capability
	 * of this RTC chip.  We check for it anyways in case support is
	 * added in the future and only assign when we care.
	 */
	if (likely(seconds < 0xc0))
		alrm->time.tm_sec = ds1685_rtc_bcd2bin(rtc, seconds,
						       RTC_SECS_BCD_MASK,
						       RTC_SECS_BIN_MASK);

	if (likely(minutes < 0xc0))
		alrm->time.tm_min = ds1685_rtc_bcd2bin(rtc, minutes,
						       RTC_MINS_BCD_MASK,
						       RTC_MINS_BIN_MASK);

	if (likely(hours < 0xc0))
		alrm->time.tm_hour = ds1685_rtc_bcd2bin(rtc, hours,
							RTC_HRS_24_BCD_MASK,
							RTC_HRS_24_BIN_MASK);

	/* Write the data to rtc_wkalrm. */
	alrm->time.tm_mday = ds1685_rtc_bcd2bin(rtc, mday, RTC_MDAY_BCD_MASK,
						RTC_MDAY_BIN_MASK);
	alrm->enabled = !!(ctrlb & RTC_CTRL_B_AIE);
	alrm->pending = !!(ctrlc & RTC_CTRL_C_AF);

	return 0;
}

/**
 * ds1685_rtc_set_alarm - sets the alarm in registers.
 * @dev: pointer to device structure.
 * @alrm: pointer to rtc_wkalrm structure.
 */
static int
ds1685_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);
	u8 ctrlb, seconds, minutes, hours, mday;
	int ret;

	/* Fetch the alarm info and convert to BCD. */
	seconds	= ds1685_rtc_bin2bcd(rtc, alrm->time.tm_sec,
				     RTC_SECS_BIN_MASK,
				     RTC_SECS_BCD_MASK);
	minutes	= ds1685_rtc_bin2bcd(rtc, alrm->time.tm_min,
				     RTC_MINS_BIN_MASK,
				     RTC_MINS_BCD_MASK);
	hours	= ds1685_rtc_bin2bcd(rtc, alrm->time.tm_hour,
				     RTC_HRS_24_BIN_MASK,
				     RTC_HRS_24_BCD_MASK);
	mday	= ds1685_rtc_bin2bcd(rtc, alrm->time.tm_mday,
				     RTC_MDAY_BIN_MASK,
				     RTC_MDAY_BCD_MASK);

	/* Check the month date for validity. */
	ret = ds1685_rtc_check_mday(rtc, mday);
	if (ret)
		return ret;

	/*
	 * Check the three alarm bytes.
	 *
	 * The Linux RTC system doesn't support the "don't care" capability
	 * of this RTC chip because rtc_valid_tm tries to validate every
	 * field, and we only support four fields.  We put the support
	 * here anyways for the future.
	 */
	if (unlikely(seconds >= 0xc0))
		seconds = 0xff;

	if (unlikely(minutes >= 0xc0))
		minutes = 0xff;

	if (unlikely(hours >= 0xc0))
		hours = 0xff;

	alrm->time.tm_mon	= -1;
	alrm->time.tm_year	= -1;
	alrm->time.tm_wday	= -1;
	alrm->time.tm_yday	= -1;
	alrm->time.tm_isdst	= -1;

	/* Disable the alarm interrupt first. */
	ds1685_rtc_begin_data_access(rtc);
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	rtc->write(rtc, RTC_CTRL_B, (ctrlb & ~(RTC_CTRL_B_AIE)));

	/* Read ctrlc to clear RTC_CTRL_C_AF. */
	rtc->read(rtc, RTC_CTRL_C);

	/*
	 * Set the data mode to use and store the time values in the
	 * RTC registers.
	 */
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	if (rtc->bcd_mode)
		ctrlb &= ~(RTC_CTRL_B_DM);
	else
		ctrlb |= RTC_CTRL_B_DM;
	rtc->write(rtc, RTC_CTRL_B, ctrlb);
	rtc->write(rtc, RTC_SECS_ALARM, seconds);
	rtc->write(rtc, RTC_MINS_ALARM, minutes);
	rtc->write(rtc, RTC_HRS_ALARM, hours);
	rtc->write(rtc, RTC_MDAY_ALARM, mday);

	/* Re-enable the alarm if needed. */
	if (alrm->enabled) {
		ctrlb = rtc->read(rtc, RTC_CTRL_B);
		ctrlb |= RTC_CTRL_B_AIE;
		rtc->write(rtc, RTC_CTRL_B, ctrlb);
	}

	/* Done! */
	ds1685_rtc_end_data_access(rtc);

	return 0;
}
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* /dev/rtcX Interface functions */

/**
 * ds1685_rtc_alarm_irq_enable - replaces ioctl() RTC_AIE on/off.
 * @dev: pointer to device structure.
 * @enabled: flag indicating whether to enable or disable.
 */
static int
ds1685_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);

	/* Flip the requisite interrupt-enable bit. */
	if (enabled)
		rtc->write(rtc, RTC_CTRL_B, (rtc->read(rtc, RTC_CTRL_B) |
					     RTC_CTRL_B_AIE));
	else
		rtc->write(rtc, RTC_CTRL_B, (rtc->read(rtc, RTC_CTRL_B) &
					     ~(RTC_CTRL_B_AIE)));

	/* Read Control C to clear all the flag bits. */
	rtc->read(rtc, RTC_CTRL_C);

	return 0;
}
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* IRQ handler */

/**
 * ds1685_rtc_extended_irq - take care of extended interrupts
 * @rtc: pointer to the ds1685 rtc structure.
 * @pdev: platform device pointer.
 */
static void
ds1685_rtc_extended_irq(struct ds1685_priv *rtc, struct platform_device *pdev)
{
	u8 ctrl4a, ctrl4b;

	ds1685_rtc_switch_to_bank1(rtc);
	ctrl4a = rtc->read(rtc, RTC_EXT_CTRL_4A);
	ctrl4b = rtc->read(rtc, RTC_EXT_CTRL_4B);

	/*
	 * Check for a kickstart interrupt. With Vcc applied, this
	 * typically means that the power button was pressed, so we
	 * begin the shutdown sequence.
	 */
	if ((ctrl4b & RTC_CTRL_4B_KSE) && (ctrl4a & RTC_CTRL_4A_KF)) {
		/* Briefly disable kickstarts to debounce button presses. */
		rtc->write(rtc, RTC_EXT_CTRL_4B,
			   (rtc->read(rtc, RTC_EXT_CTRL_4B) &
			    ~(RTC_CTRL_4B_KSE)));

		/* Clear the kickstart flag. */
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (ctrl4a & ~(RTC_CTRL_4A_KF)));


		/*
		 * Sleep 500ms before re-enabling kickstarts.  This allows
		 * adequate time to avoid reading signal jitter as additional
		 * button presses.
		 */
		msleep(500);
		rtc->write(rtc, RTC_EXT_CTRL_4B,
			   (rtc->read(rtc, RTC_EXT_CTRL_4B) |
			    RTC_CTRL_4B_KSE));

		/* Call the platform pre-poweroff function. Else, shutdown. */
		if (rtc->prepare_poweroff != NULL)
			rtc->prepare_poweroff();
		else
			ds1685_rtc_poweroff(pdev);
	}

	/*
	 * Check for a wake-up interrupt.  With Vcc applied, this is
	 * essentially a second alarm interrupt, except it takes into
	 * account the 'date' register in bank1 in addition to the
	 * standard three alarm registers.
	 */
	if ((ctrl4b & RTC_CTRL_4B_WIE) && (ctrl4a & RTC_CTRL_4A_WF)) {
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (ctrl4a & ~(RTC_CTRL_4A_WF)));

		/* Call the platform wake_alarm function if defined. */
		if (rtc->wake_alarm != NULL)
			rtc->wake_alarm();
		else
			dev_warn(&pdev->dev,
				 "Wake Alarm IRQ just occurred!\n");
	}

	/*
	 * Check for a ram-clear interrupt.  This happens if RIE=1 and RF=0
	 * when RCE=1 in 4B.  This clears all NVRAM bytes in bank0 by setting
	 * each byte to a logic 1.  This has no effect on any extended
	 * NV-SRAM that might be present, nor on the time/calendar/alarm
	 * registers.  After a ram-clear is completed, there is a minimum
	 * recovery time of ~150ms in which all reads/writes are locked out.
	 * NOTE: A ram-clear can still occur if RCE=1 and RIE=0.  We cannot
	 * catch this scenario.
	 */
	if ((ctrl4b & RTC_CTRL_4B_RIE) && (ctrl4a & RTC_CTRL_4A_RF)) {
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (ctrl4a & ~(RTC_CTRL_4A_RF)));
		msleep(150);

		/* Call the platform post_ram_clear function if defined. */
		if (rtc->post_ram_clear != NULL)
			rtc->post_ram_clear();
		else
			dev_warn(&pdev->dev,
				 "RAM-Clear IRQ just occurred!\n");
	}
	ds1685_rtc_switch_to_bank0(rtc);
}

/**
 * ds1685_rtc_irq_handler - IRQ handler.
 * @irq: IRQ number.
 * @dev_id: platform device pointer.
 */
static irqreturn_t
ds1685_rtc_irq_handler(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct ds1685_priv *rtc = platform_get_drvdata(pdev);
	u8 ctrlb, ctrlc;
	unsigned long events = 0;
	u8 num_irqs = 0;

	/* Abort early if the device isn't ready yet (i.e., DEBUG_SHIRQ). */
	if (unlikely(!rtc))
		return IRQ_HANDLED;

	rtc_lock(rtc->dev);

	/* Ctrlb holds the interrupt-enable bits and ctrlc the flag bits. */
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	ctrlc = rtc->read(rtc, RTC_CTRL_C);

	/* Is the IRQF bit set? */
	if (likely(ctrlc & RTC_CTRL_C_IRQF)) {
		/*
		 * We need to determine if it was one of the standard
		 * events: PF, AF, or UF.  If so, we handle them and
		 * update the RTC core.
		 */
		if (likely(ctrlc & RTC_CTRL_B_PAU_MASK)) {
			events = RTC_IRQF;

			/* Check for a periodic interrupt. */
			if ((ctrlb & RTC_CTRL_B_PIE) &&
			    (ctrlc & RTC_CTRL_C_PF)) {
				events |= RTC_PF;
				num_irqs++;
			}

			/* Check for an alarm interrupt. */
			if ((ctrlb & RTC_CTRL_B_AIE) &&
			    (ctrlc & RTC_CTRL_C_AF)) {
				events |= RTC_AF;
				num_irqs++;
			}

			/* Check for an update interrupt. */
			if ((ctrlb & RTC_CTRL_B_UIE) &&
			    (ctrlc & RTC_CTRL_C_UF)) {
				events |= RTC_UF;
				num_irqs++;
			}
		} else {
			/*
			 * One of the "extended" interrupts was received that
			 * is not recognized by the RTC core.
			 */
			ds1685_rtc_extended_irq(rtc, pdev);
		}
	}
	rtc_update_irq(rtc->dev, num_irqs, events);
	rtc_unlock(rtc->dev);

	return events ? IRQ_HANDLED : IRQ_NONE;
}
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* ProcFS interface */

#ifdef CONFIG_PROC_FS
#define NUM_REGS	6	/* Num of control registers. */
#define NUM_BITS	8	/* Num bits per register. */
#define NUM_SPACES	4	/* Num spaces between each bit. */

/*
 * Periodic Interrupt Rates.
 */
static const char *ds1685_rtc_pirq_rate[16] = {
	"none", "3.90625ms", "7.8125ms", "0.122070ms", "0.244141ms",
	"0.488281ms", "0.9765625ms", "1.953125ms", "3.90625ms", "7.8125ms",
	"15.625ms", "31.25ms", "62.5ms", "125ms", "250ms", "500ms"
};

/*
 * Square-Wave Output Frequencies.
 */
static const char *ds1685_rtc_sqw_freq[16] = {
	"none", "256Hz", "128Hz", "8192Hz", "4096Hz", "2048Hz", "1024Hz",
	"512Hz", "256Hz", "128Hz", "64Hz", "32Hz", "16Hz", "8Hz", "4Hz", "2Hz"
};

/**
 * ds1685_rtc_proc - procfs access function.
 * @dev: pointer to device structure.
 * @seq: pointer to seq_file structure.
 */
static int
ds1685_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev);
	u8 ctrla, ctrlb, ctrld, ctrl4a, ctrl4b, ssn[8];
	char *model;

	/* Read all the relevant data from the control registers. */
	ds1685_rtc_switch_to_bank1(rtc);
	ds1685_rtc_get_ssn(rtc, ssn);
	ctrla = rtc->read(rtc, RTC_CTRL_A);
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	ctrld = rtc->read(rtc, RTC_CTRL_D);
	ctrl4a = rtc->read(rtc, RTC_EXT_CTRL_4A);
	ctrl4b = rtc->read(rtc, RTC_EXT_CTRL_4B);
	ds1685_rtc_switch_to_bank0(rtc);

	/* Determine the RTC model. */
	switch (ssn[0]) {
	case RTC_MODEL_DS1685:
		model = "DS1685/DS1687\0";
		break;
	case RTC_MODEL_DS1689:
		model = "DS1689/DS1693\0";
		break;
	case RTC_MODEL_DS17285:
		model = "DS17285/DS17287\0";
		break;
	case RTC_MODEL_DS17485:
		model = "DS17485/DS17487\0";
		break;
	case RTC_MODEL_DS17885:
		model = "DS17885/DS17887\0";
		break;
	default:
		model = "Unknown\0";
		break;
	}

	/* Print out the information. */
	seq_printf(seq,
	   "Model\t\t: %s\n"
	   "Oscillator\t: %s\n"
	   "12/24hr\t\t: %s\n"
	   "DST\t\t: %s\n"
	   "Data mode\t: %s\n"
	   "Battery\t\t: %s\n"
	   "Aux batt\t: %s\n"
	   "Update IRQ\t: %s\n"
	   "Periodic IRQ\t: %s\n"
	   "Periodic Rate\t: %s\n"
	   "SQW Freq\t: %s\n"
	   "Serial #\t: %8phC\n",
	   model,
	   ((ctrla & RTC_CTRL_A_DV1) ? "enabled" : "disabled"),
	   ((ctrlb & RTC_CTRL_B_2412) ? "24-hour" : "12-hour"),
	   ((ctrlb & RTC_CTRL_B_DSE) ? "enabled" : "disabled"),
	   ((ctrlb & RTC_CTRL_B_DM) ? "binary" : "BCD"),
	   ((ctrld & RTC_CTRL_D_VRT) ? "ok" : "exhausted or n/a"),
	   ((ctrl4a & RTC_CTRL_4A_VRT2) ? "ok" : "exhausted or n/a"),
	   ((ctrlb & RTC_CTRL_B_UIE) ? "yes" : "no"),
	   ((ctrlb & RTC_CTRL_B_PIE) ? "yes" : "no"),
	   (!(ctrl4b & RTC_CTRL_4B_E32K) ?
	    ds1685_rtc_pirq_rate[(ctrla & RTC_CTRL_A_RS_MASK)] : "none"),
	   (!((ctrl4b & RTC_CTRL_4B_E32K)) ?
	    ds1685_rtc_sqw_freq[(ctrla & RTC_CTRL_A_RS_MASK)] : "32768Hz"),
	   ssn);
	return 0;
}
#else
#define ds1685_rtc_proc NULL
#endif /* CONFIG_PROC_FS */
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* RTC Class operations */

static const struct rtc_class_ops
ds1685_rtc_ops = {
	.proc = ds1685_rtc_proc,
	.read_time = ds1685_rtc_read_time,
	.set_time = ds1685_rtc_set_time,
	.read_alarm = ds1685_rtc_read_alarm,
	.set_alarm = ds1685_rtc_set_alarm,
	.alarm_irq_enable = ds1685_rtc_alarm_irq_enable,
};
/* ----------------------------------------------------------------------- */

static int ds1685_nvram_read(void *priv, unsigned int pos, void *val,
			     size_t size)
{
	struct ds1685_priv *rtc = priv;
	struct mutex *rtc_mutex = &rtc->dev->ops_lock;
	ssize_t count;
	u8 *buf = val;
	int err;

	err = mutex_lock_interruptible(rtc_mutex);
	if (err)
		return err;

	ds1685_rtc_switch_to_bank0(rtc);

	/* Read NVRAM in time and bank0 registers. */
	for (count = 0; size > 0 && pos < NVRAM_TOTAL_SZ_BANK0;
	     count++, size--) {
		if (count < NVRAM_SZ_TIME)
			*buf++ = rtc->read(rtc, (NVRAM_TIME_BASE + pos++));
		else
			*buf++ = rtc->read(rtc, (NVRAM_BANK0_BASE + pos++));
	}

#ifndef CONFIG_RTC_DRV_DS1689
	if (size > 0) {
		ds1685_rtc_switch_to_bank1(rtc);

#ifndef CONFIG_RTC_DRV_DS1685
		/* Enable burst-mode on DS17x85/DS17x87 */
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (rtc->read(rtc, RTC_EXT_CTRL_4A) |
			    RTC_CTRL_4A_BME));

		/* We need one write to RTC_BANK1_RAM_ADDR_LSB to start
		 * reading with burst-mode */
		rtc->write(rtc, RTC_BANK1_RAM_ADDR_LSB,
			   (pos - NVRAM_TOTAL_SZ_BANK0));
#endif

		/* Read NVRAM in bank1 registers. */
		for (count = 0; size > 0 && pos < NVRAM_TOTAL_SZ;
		     count++, size--) {
#ifdef CONFIG_RTC_DRV_DS1685
			/* DS1685/DS1687 has to write to RTC_BANK1_RAM_ADDR
			 * before each read. */
			rtc->write(rtc, RTC_BANK1_RAM_ADDR,
				   (pos - NVRAM_TOTAL_SZ_BANK0));
#endif
			*buf++ = rtc->read(rtc, RTC_BANK1_RAM_DATA_PORT);
			pos++;
		}

#ifndef CONFIG_RTC_DRV_DS1685
		/* Disable burst-mode on DS17x85/DS17x87 */
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (rtc->read(rtc, RTC_EXT_CTRL_4A) &
			    ~(RTC_CTRL_4A_BME)));
#endif
		ds1685_rtc_switch_to_bank0(rtc);
	}
#endif /* !CONFIG_RTC_DRV_DS1689 */
	mutex_unlock(rtc_mutex);

	return 0;
}

static int ds1685_nvram_write(void *priv, unsigned int pos, void *val,
			      size_t size)
{
	struct ds1685_priv *rtc = priv;
	struct mutex *rtc_mutex = &rtc->dev->ops_lock;
	ssize_t count;
	u8 *buf = val;
	int err;

	err = mutex_lock_interruptible(rtc_mutex);
	if (err)
		return err;

	ds1685_rtc_switch_to_bank0(rtc);

	/* Write NVRAM in time and bank0 registers. */
	for (count = 0; size > 0 && pos < NVRAM_TOTAL_SZ_BANK0;
	     count++, size--)
		if (count < NVRAM_SZ_TIME)
			rtc->write(rtc, (NVRAM_TIME_BASE + pos++),
				   *buf++);
		else
			rtc->write(rtc, (NVRAM_BANK0_BASE), *buf++);

#ifndef CONFIG_RTC_DRV_DS1689
	if (size > 0) {
		ds1685_rtc_switch_to_bank1(rtc);

#ifndef CONFIG_RTC_DRV_DS1685
		/* Enable burst-mode on DS17x85/DS17x87 */
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (rtc->read(rtc, RTC_EXT_CTRL_4A) |
			    RTC_CTRL_4A_BME));

		/* We need one write to RTC_BANK1_RAM_ADDR_LSB to start
		 * writing with burst-mode */
		rtc->write(rtc, RTC_BANK1_RAM_ADDR_LSB,
			   (pos - NVRAM_TOTAL_SZ_BANK0));
#endif

		/* Write NVRAM in bank1 registers. */
		for (count = 0; size > 0 && pos < NVRAM_TOTAL_SZ;
		     count++, size--) {
#ifdef CONFIG_RTC_DRV_DS1685
			/* DS1685/DS1687 has to write to RTC_BANK1_RAM_ADDR
			 * before each read. */
			rtc->write(rtc, RTC_BANK1_RAM_ADDR,
				   (pos - NVRAM_TOTAL_SZ_BANK0));
#endif
			rtc->write(rtc, RTC_BANK1_RAM_DATA_PORT, *buf++);
			pos++;
		}

#ifndef CONFIG_RTC_DRV_DS1685
		/* Disable burst-mode on DS17x85/DS17x87 */
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (rtc->read(rtc, RTC_EXT_CTRL_4A) &
			    ~(RTC_CTRL_4A_BME)));
#endif
		ds1685_rtc_switch_to_bank0(rtc);
	}
#endif /* !CONFIG_RTC_DRV_DS1689 */
	mutex_unlock(rtc_mutex);

	return 0;
}

/* ----------------------------------------------------------------------- */
/* SysFS interface */

/**
 * ds1685_rtc_sysfs_battery_show - sysfs file for main battery status.
 * @dev: pointer to device structure.
 * @attr: pointer to device_attribute structure.
 * @buf: pointer to char array to hold the output.
 */
static ssize_t
ds1685_rtc_sysfs_battery_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ctrld;

	ctrld = rtc->read(rtc, RTC_CTRL_D);

	return sprintf(buf, "%s\n",
			(ctrld & RTC_CTRL_D_VRT) ? "ok" : "not ok or N/A");
}
static DEVICE_ATTR(battery, S_IRUGO, ds1685_rtc_sysfs_battery_show, NULL);

/**
 * ds1685_rtc_sysfs_auxbatt_show - sysfs file for aux battery status.
 * @dev: pointer to device structure.
 * @attr: pointer to device_attribute structure.
 * @buf: pointer to char array to hold the output.
 */
static ssize_t
ds1685_rtc_sysfs_auxbatt_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ctrl4a;

	ds1685_rtc_switch_to_bank1(rtc);
	ctrl4a = rtc->read(rtc, RTC_EXT_CTRL_4A);
	ds1685_rtc_switch_to_bank0(rtc);

	return sprintf(buf, "%s\n",
			(ctrl4a & RTC_CTRL_4A_VRT2) ? "ok" : "not ok or N/A");
}
static DEVICE_ATTR(auxbatt, S_IRUGO, ds1685_rtc_sysfs_auxbatt_show, NULL);

/**
 * ds1685_rtc_sysfs_serial_show - sysfs file for silicon serial number.
 * @dev: pointer to device structure.
 * @attr: pointer to device_attribute structure.
 * @buf: pointer to char array to hold the output.
 */
static ssize_t
ds1685_rtc_sysfs_serial_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct ds1685_priv *rtc = dev_get_drvdata(dev->parent);
	u8 ssn[8];

	ds1685_rtc_switch_to_bank1(rtc);
	ds1685_rtc_get_ssn(rtc, ssn);
	ds1685_rtc_switch_to_bank0(rtc);

	return sprintf(buf, "%8phC\n", ssn);
}
static DEVICE_ATTR(serial, S_IRUGO, ds1685_rtc_sysfs_serial_show, NULL);

/*
 * struct ds1685_rtc_sysfs_misc_attrs - list for misc RTC features.
 */
static struct attribute*
ds1685_rtc_sysfs_misc_attrs[] = {
	&dev_attr_battery.attr,
	&dev_attr_auxbatt.attr,
	&dev_attr_serial.attr,
	NULL,
};

/*
 * struct ds1685_rtc_sysfs_misc_grp - attr group for misc RTC features.
 */
static const struct attribute_group
ds1685_rtc_sysfs_misc_grp = {
	.name = "misc",
	.attrs = ds1685_rtc_sysfs_misc_attrs,
};

/* ----------------------------------------------------------------------- */
/* Driver Probe/Removal */

/**
 * ds1685_rtc_probe - initializes rtc driver.
 * @pdev: pointer to platform_device structure.
 */
static int
ds1685_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc_dev;
	struct ds1685_priv *rtc;
	struct ds1685_rtc_platform_data *pdata;
	u8 ctrla, ctrlb, hours;
	unsigned char am_pm;
	int ret = 0;
	struct nvmem_config nvmem_cfg = {
		.name = "ds1685_nvram",
		.size = NVRAM_TOTAL_SZ,
		.reg_read = ds1685_nvram_read,
		.reg_write = ds1685_nvram_write,
	};

	/* Get the platform data. */
	pdata = (struct ds1685_rtc_platform_data *) pdev->dev.platform_data;
	if (!pdata)
		return -ENODEV;

	/* Allocate memory for the rtc device. */
	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	/* Setup resources and access functions */
	switch (pdata->access_type) {
	case ds1685_reg_direct:
		rtc->regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(rtc->regs))
			return PTR_ERR(rtc->regs);
		rtc->read = ds1685_read;
		rtc->write = ds1685_write;
		break;
	case ds1685_reg_indirect:
		rtc->regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(rtc->regs))
			return PTR_ERR(rtc->regs);
		rtc->data = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(rtc->data))
			return PTR_ERR(rtc->data);
		rtc->read = ds1685_indirect_read;
		rtc->write = ds1685_indirect_write;
		break;
	}

	if (!rtc->read || !rtc->write)
		return -ENXIO;

	/* Get the register step size. */
	if (pdata->regstep > 0)
		rtc->regstep = pdata->regstep;
	else
		rtc->regstep = 1;

	/* Platform pre-shutdown function, if defined. */
	if (pdata->plat_prepare_poweroff)
		rtc->prepare_poweroff = pdata->plat_prepare_poweroff;

	/* Platform wake_alarm function, if defined. */
	if (pdata->plat_wake_alarm)
		rtc->wake_alarm = pdata->plat_wake_alarm;

	/* Platform post_ram_clear function, if defined. */
	if (pdata->plat_post_ram_clear)
		rtc->post_ram_clear = pdata->plat_post_ram_clear;

	/* set the driver data. */
	platform_set_drvdata(pdev, rtc);

	/* Turn the oscillator on if is not already on (DV1 = 1). */
	ctrla = rtc->read(rtc, RTC_CTRL_A);
	if (!(ctrla & RTC_CTRL_A_DV1))
		ctrla |= RTC_CTRL_A_DV1;

	/* Enable the countdown chain (DV2 = 0) */
	ctrla &= ~(RTC_CTRL_A_DV2);

	/* Clear RS3-RS0 in Control A. */
	ctrla &= ~(RTC_CTRL_A_RS_MASK);

	/*
	 * All done with Control A.  Switch to Bank 1 for the remainder of
	 * the RTC setup so we have access to the extended functions.
	 */
	ctrla |= RTC_CTRL_A_DV0;
	rtc->write(rtc, RTC_CTRL_A, ctrla);

	/* Default to 32768kHz output. */
	rtc->write(rtc, RTC_EXT_CTRL_4B,
		   (rtc->read(rtc, RTC_EXT_CTRL_4B) | RTC_CTRL_4B_E32K));

	/* Set the SET bit in Control B so we can do some housekeeping. */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) | RTC_CTRL_B_SET));

	/* Read Ext Ctrl 4A and check the INCR bit to avoid a lockout. */
	while (rtc->read(rtc, RTC_EXT_CTRL_4A) & RTC_CTRL_4A_INCR)
		cpu_relax();

	/*
	 * If the platform supports BCD mode, then set DM=0 in Control B.
	 * Otherwise, set DM=1 for BIN mode.
	 */
	ctrlb = rtc->read(rtc, RTC_CTRL_B);
	if (pdata->bcd_mode)
		ctrlb &= ~(RTC_CTRL_B_DM);
	else
		ctrlb |= RTC_CTRL_B_DM;
	rtc->bcd_mode = pdata->bcd_mode;

	/*
	 * Disable Daylight Savings Time (DSE = 0).
	 * The RTC has hardcoded timezone information that is rendered
	 * obselete.  We'll let the OS deal with DST settings instead.
	 */
	if (ctrlb & RTC_CTRL_B_DSE)
		ctrlb &= ~(RTC_CTRL_B_DSE);

	/* Force 24-hour mode (2412 = 1). */
	if (!(ctrlb & RTC_CTRL_B_2412)) {
		/* Reinitialize the time hours. */
		hours = rtc->read(rtc, RTC_HRS);
		am_pm = hours & RTC_HRS_AMPM_MASK;
		hours = ds1685_rtc_bcd2bin(rtc, hours, RTC_HRS_12_BCD_MASK,
					   RTC_HRS_12_BIN_MASK);
		hours = ((hours == 12) ? 0 : ((am_pm) ? hours + 12 : hours));

		/* Enable 24-hour mode. */
		ctrlb |= RTC_CTRL_B_2412;

		/* Write back to Control B, including DM & DSE bits. */
		rtc->write(rtc, RTC_CTRL_B, ctrlb);

		/* Write the time hours back. */
		rtc->write(rtc, RTC_HRS,
			   ds1685_rtc_bin2bcd(rtc, hours,
					      RTC_HRS_24_BIN_MASK,
					      RTC_HRS_24_BCD_MASK));

		/* Reinitialize the alarm hours. */
		hours = rtc->read(rtc, RTC_HRS_ALARM);
		am_pm = hours & RTC_HRS_AMPM_MASK;
		hours = ds1685_rtc_bcd2bin(rtc, hours, RTC_HRS_12_BCD_MASK,
					   RTC_HRS_12_BIN_MASK);
		hours = ((hours == 12) ? 0 : ((am_pm) ? hours + 12 : hours));

		/* Write the alarm hours back. */
		rtc->write(rtc, RTC_HRS_ALARM,
			   ds1685_rtc_bin2bcd(rtc, hours,
					      RTC_HRS_24_BIN_MASK,
					      RTC_HRS_24_BCD_MASK));
	} else {
		/* 24-hour mode is already set, so write Control B back. */
		rtc->write(rtc, RTC_CTRL_B, ctrlb);
	}

	/* Unset the SET bit in Control B so the RTC can update. */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) & ~(RTC_CTRL_B_SET)));

	/* Check the main battery. */
	if (!(rtc->read(rtc, RTC_CTRL_D) & RTC_CTRL_D_VRT))
		dev_warn(&pdev->dev,
			 "Main battery is exhausted! RTC may be invalid!\n");

	/* Check the auxillary battery.  It is optional. */
	if (!(rtc->read(rtc, RTC_EXT_CTRL_4A) & RTC_CTRL_4A_VRT2))
		dev_warn(&pdev->dev,
			 "Aux battery is exhausted or not available.\n");

	/* Read Ctrl B and clear PIE/AIE/UIE. */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) & ~(RTC_CTRL_B_PAU_MASK)));

	/* Reading Ctrl C auto-clears PF/AF/UF. */
	rtc->read(rtc, RTC_CTRL_C);

	/* Read Ctrl 4B and clear RIE/WIE/KSE. */
	rtc->write(rtc, RTC_EXT_CTRL_4B,
		   (rtc->read(rtc, RTC_EXT_CTRL_4B) & ~(RTC_CTRL_4B_RWK_MASK)));

	/* Clear RF/WF/KF in Ctrl 4A. */
	rtc->write(rtc, RTC_EXT_CTRL_4A,
		   (rtc->read(rtc, RTC_EXT_CTRL_4A) & ~(RTC_CTRL_4A_RWK_MASK)));

	/*
	 * Re-enable KSE to handle power button events.  We do not enable
	 * WIE or RIE by default.
	 */
	rtc->write(rtc, RTC_EXT_CTRL_4B,
		   (rtc->read(rtc, RTC_EXT_CTRL_4B) | RTC_CTRL_4B_KSE));

	rtc_dev = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc_dev))
		return PTR_ERR(rtc_dev);

	rtc_dev->ops = &ds1685_rtc_ops;

	/* Century bit is useless because leap year fails in 1900 and 2100 */
	rtc_dev->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc_dev->range_max = RTC_TIMESTAMP_END_2099;

	/* Maximum periodic rate is 8192Hz (0.122070ms). */
	rtc_dev->max_user_freq = RTC_MAX_USER_FREQ;

	/* See if the platform doesn't support UIE. */
	if (pdata->uie_unsupported)
		clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc_dev->features);

	rtc->dev = rtc_dev;

	/*
	 * Fetch the IRQ and setup the interrupt handler.
	 *
	 * Not all platforms have the IRQF pin tied to something.  If not, the
	 * RTC will still set the *IE / *F flags and raise IRQF in ctrlc, but
	 * there won't be an automatic way of notifying the kernel about it,
	 * unless ctrlc is explicitly polled.
	 */
	rtc->irq_num = platform_get_irq(pdev, 0);
	if (rtc->irq_num <= 0) {
		clear_bit(RTC_FEATURE_ALARM, rtc_dev->features);
	} else {
		/* Request an IRQ. */
		ret = devm_request_threaded_irq(&pdev->dev, rtc->irq_num,
				       NULL, ds1685_rtc_irq_handler,
				       IRQF_SHARED | IRQF_ONESHOT,
				       pdev->name, pdev);

		/* Check to see if something came back. */
		if (unlikely(ret)) {
			dev_warn(&pdev->dev,
				 "RTC interrupt not available\n");
			rtc->irq_num = 0;
		}
	}

	/* Setup complete. */
	ds1685_rtc_switch_to_bank0(rtc);

	ret = rtc_add_group(rtc_dev, &ds1685_rtc_sysfs_misc_grp);
	if (ret)
		return ret;

	nvmem_cfg.priv = rtc;
	ret = devm_rtc_nvmem_register(rtc_dev, &nvmem_cfg);
	if (ret)
		return ret;

	return devm_rtc_register_device(rtc_dev);
}

/**
 * ds1685_rtc_remove - removes rtc driver.
 * @pdev: pointer to platform_device structure.
 */
static void
ds1685_rtc_remove(struct platform_device *pdev)
{
	struct ds1685_priv *rtc = platform_get_drvdata(pdev);

	/* Read Ctrl B and clear PIE/AIE/UIE. */
	rtc->write(rtc, RTC_CTRL_B,
		   (rtc->read(rtc, RTC_CTRL_B) &
		    ~(RTC_CTRL_B_PAU_MASK)));

	/* Reading Ctrl C auto-clears PF/AF/UF. */
	rtc->read(rtc, RTC_CTRL_C);

	/* Read Ctrl 4B and clear RIE/WIE/KSE. */
	rtc->write(rtc, RTC_EXT_CTRL_4B,
		   (rtc->read(rtc, RTC_EXT_CTRL_4B) &
		    ~(RTC_CTRL_4B_RWK_MASK)));

	/* Manually clear RF/WF/KF in Ctrl 4A. */
	rtc->write(rtc, RTC_EXT_CTRL_4A,
		   (rtc->read(rtc, RTC_EXT_CTRL_4A) &
		    ~(RTC_CTRL_4A_RWK_MASK)));
}

/*
 * ds1685_rtc_driver - rtc driver properties.
 */
static struct platform_driver ds1685_rtc_driver = {
	.driver		= {
		.name	= "rtc-ds1685",
	},
	.probe		= ds1685_rtc_probe,
	.remove		= ds1685_rtc_remove,
};
module_platform_driver(ds1685_rtc_driver);
/* ----------------------------------------------------------------------- */


/* ----------------------------------------------------------------------- */
/* Poweroff function */

/**
 * ds1685_rtc_poweroff - uses the RTC chip to power the system off.
 * @pdev: pointer to platform_device structure.
 */
void __noreturn
ds1685_rtc_poweroff(struct platform_device *pdev)
{
	u8 ctrla, ctrl4a, ctrl4b;
	struct ds1685_priv *rtc;

	/* Check for valid RTC data, else, spin forever. */
	if (unlikely(!pdev)) {
		pr_emerg("platform device data not available, spinning forever ...\n");
		while(1);
		unreachable();
	} else {
		/* Get the rtc data. */
		rtc = platform_get_drvdata(pdev);

		/*
		 * Disable our IRQ.  We're powering down, so we're not
		 * going to worry about cleaning up.  Most of that should
		 * have been taken care of by the shutdown scripts and this
		 * is the final function call.
		 */
		if (rtc->irq_num)
			disable_irq_nosync(rtc->irq_num);

		/* Oscillator must be on and the countdown chain enabled. */
		ctrla = rtc->read(rtc, RTC_CTRL_A);
		ctrla |= RTC_CTRL_A_DV1;
		ctrla &= ~(RTC_CTRL_A_DV2);
		rtc->write(rtc, RTC_CTRL_A, ctrla);

		/*
		 * Read Control 4A and check the status of the auxillary
		 * battery.  This must be present and working (VRT2 = 1)
		 * for wakeup and kickstart functionality to be useful.
		 */
		ds1685_rtc_switch_to_bank1(rtc);
		ctrl4a = rtc->read(rtc, RTC_EXT_CTRL_4A);
		if (ctrl4a & RTC_CTRL_4A_VRT2) {
			/* Clear all of the interrupt flags on Control 4A. */
			ctrl4a &= ~(RTC_CTRL_4A_RWK_MASK);
			rtc->write(rtc, RTC_EXT_CTRL_4A, ctrl4a);

			/*
			 * The auxillary battery is present and working.
			 * Enable extended functions (ABE=1), enable
			 * wake-up (WIE=1), and enable kickstart (KSE=1)
			 * in Control 4B.
			 */
			ctrl4b = rtc->read(rtc, RTC_EXT_CTRL_4B);
			ctrl4b |= (RTC_CTRL_4B_ABE | RTC_CTRL_4B_WIE |
				   RTC_CTRL_4B_KSE);
			rtc->write(rtc, RTC_EXT_CTRL_4B, ctrl4b);
		}

		/* Set PAB to 1 in Control 4A to power the system down. */
		dev_warn(&pdev->dev, "Powerdown.\n");
		msleep(20);
		rtc->write(rtc, RTC_EXT_CTRL_4A,
			   (ctrl4a | RTC_CTRL_4A_PAB));

		/* Spin ... we do not switch back to bank0. */
		while(1);
		unreachable();
	}
}
EXPORT_SYMBOL_GPL(ds1685_rtc_poweroff);
/* ----------------------------------------------------------------------- */


MODULE_AUTHOR("Joshua Kinard <linux@kumba.dev>");
MODULE_AUTHOR("Matthias Fuchs <matthias.fuchs@esd-electronics.com>");
MODULE_DESCRIPTION("Dallas/Maxim DS1685/DS1687-series RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-ds1685");
