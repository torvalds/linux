// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/mc146818rtc.h>

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif

/*
 * Execute a function while the UIP (Update-in-progress) bit of the RTC is
 * unset.
 *
 * Warning: callback may be executed more then once.
 */
bool mc146818_avoid_UIP(void (*callback)(unsigned char seconds, void *param),
			void *param)
{
	int i;
	unsigned long flags;
	unsigned char seconds;

	for (i = 0; i < 10; i++) {
		spin_lock_irqsave(&rtc_lock, flags);

		/*
		 * Check whether there is an update in progress during which the
		 * readout is unspecified. The maximum update time is ~2ms. Poll
		 * every msec for completion.
		 *
		 * Store the second value before checking UIP so a long lasting
		 * NMI which happens to hit after the UIP check cannot make
		 * an update cycle invisible.
		 */
		seconds = CMOS_READ(RTC_SECONDS);

		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			mdelay(1);
			continue;
		}

		/* Revalidate the above readout */
		if (seconds != CMOS_READ(RTC_SECONDS)) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			continue;
		}

		if (callback)
			callback(seconds, param);

		/*
		 * Check for the UIP bit again. If it is set now then
		 * the above values may contain garbage.
		 */
		if (CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			mdelay(1);
			continue;
		}

		/*
		 * A NMI might have interrupted the above sequence so check
		 * whether the seconds value has changed which indicates that
		 * the NMI took longer than the UIP bit was set. Unlikely, but
		 * possible and there is also virt...
		 */
		if (seconds != CMOS_READ(RTC_SECONDS)) {
			spin_unlock_irqrestore(&rtc_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&rtc_lock, flags);

		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(mc146818_avoid_UIP);

/*
 * If the UIP (Update-in-progress) bit of the RTC is set for more then
 * 10ms, the RTC is apparently broken or not present.
 */
bool mc146818_does_rtc_work(void)
{
	return mc146818_avoid_UIP(NULL, NULL);
}
EXPORT_SYMBOL_GPL(mc146818_does_rtc_work);

struct mc146818_get_time_callback_param {
	struct rtc_time *time;
	unsigned char ctrl;
#ifdef CONFIG_ACPI
	unsigned char century;
#endif
#ifdef CONFIG_MACH_DECSTATION
	unsigned int real_year;
#endif
};

static void mc146818_get_time_callback(unsigned char seconds, void *param_in)
{
	struct mc146818_get_time_callback_param *p = param_in;

	/*
	 * Only the values that we read from the RTC are set. We leave
	 * tm_wday, tm_yday and tm_isdst untouched. Even though the
	 * RTC has RTC_DAY_OF_WEEK, we ignore it, as it is only updated
	 * by the RTC when initially set to a non-zero value.
	 */
	p->time->tm_sec = seconds;
	p->time->tm_min = CMOS_READ(RTC_MINUTES);
	p->time->tm_hour = CMOS_READ(RTC_HOURS);
	p->time->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	p->time->tm_mon = CMOS_READ(RTC_MONTH);
	p->time->tm_year = CMOS_READ(RTC_YEAR);
#ifdef CONFIG_MACH_DECSTATION
	p->real_year = CMOS_READ(RTC_DEC_YEAR);
#endif
#ifdef CONFIG_ACPI
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
	    acpi_gbl_FADT.century) {
		p->century = CMOS_READ(acpi_gbl_FADT.century);
	} else {
		p->century = 0;
	}
#endif

	p->ctrl = CMOS_READ(RTC_CONTROL);
}

int mc146818_get_time(struct rtc_time *time)
{
	struct mc146818_get_time_callback_param p = {
		.time = time
	};

	if (!mc146818_avoid_UIP(mc146818_get_time_callback, &p)) {
		memset(time, 0, sizeof(*time));
		return -EIO;
	}

	if (!(p.ctrl & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	{
		time->tm_sec = bcd2bin(time->tm_sec);
		time->tm_min = bcd2bin(time->tm_min);
		time->tm_hour = bcd2bin(time->tm_hour);
		time->tm_mday = bcd2bin(time->tm_mday);
		time->tm_mon = bcd2bin(time->tm_mon);
		time->tm_year = bcd2bin(time->tm_year);
#ifdef CONFIG_ACPI
		p.century = bcd2bin(p.century);
#endif
	}

#ifdef CONFIG_MACH_DECSTATION
	time->tm_year += p.real_year - 72;
#endif

#ifdef CONFIG_ACPI
	if (p.century > 19)
		time->tm_year += (p.century - 19) * 100;
#endif

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */
	if (time->tm_year <= 69)
		time->tm_year += 100;

	time->tm_mon--;

	return 0;
}
EXPORT_SYMBOL_GPL(mc146818_get_time);

/* Set the current date and time in the real time clock. */
int mc146818_set_time(struct rtc_time *time)
{
	unsigned long flags;
	unsigned char mon, day, hrs, min, sec;
	unsigned char save_control, save_freq_select;
	unsigned int yrs;
#ifdef CONFIG_MACH_DECSTATION
	unsigned int real_yrs, leap_yr;
#endif
	unsigned char century = 0;

	yrs = time->tm_year;
	mon = time->tm_mon + 1;   /* tm_mon starts at zero */
	day = time->tm_mday;
	hrs = time->tm_hour;
	min = time->tm_min;
	sec = time->tm_sec;

	if (yrs > 255)	/* They are unsigned */
		return -EINVAL;

#ifdef CONFIG_MACH_DECSTATION
	real_yrs = yrs;
	leap_yr = ((!((yrs + 1900) % 4) && ((yrs + 1900) % 100)) ||
			!((yrs + 1900) % 400));
	yrs = 72;

	/*
	 * We want to keep the year set to 73 until March
	 * for non-leap years, so that Feb, 29th is handled
	 * correctly.
	 */
	if (!leap_yr && mon < 3) {
		real_yrs--;
		yrs = 73;
	}
#endif

#ifdef CONFIG_ACPI
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
	    acpi_gbl_FADT.century) {
		century = (yrs + 1900) / 100;
		yrs %= 100;
	}
#endif

	/* These limits and adjustments are independent of
	 * whether the chip is in binary mode or not.
	 */
	if (yrs > 169)
		return -EINVAL;

	if (yrs >= 100)
		yrs -= 100;

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY)
	    || RTC_ALWAYS_BCD) {
		sec = bin2bcd(sec);
		min = bin2bcd(min);
		hrs = bin2bcd(hrs);
		day = bin2bcd(day);
		mon = bin2bcd(mon);
		yrs = bin2bcd(yrs);
		century = bin2bcd(century);
	}

	spin_lock_irqsave(&rtc_lock, flags);
	save_control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE((save_control|RTC_SET), RTC_CONTROL);
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

#ifdef CONFIG_MACH_DECSTATION
	CMOS_WRITE(real_yrs, RTC_DEC_YEAR);
#endif
	CMOS_WRITE(yrs, RTC_YEAR);
	CMOS_WRITE(mon, RTC_MONTH);
	CMOS_WRITE(day, RTC_DAY_OF_MONTH);
	CMOS_WRITE(hrs, RTC_HOURS);
	CMOS_WRITE(min, RTC_MINUTES);
	CMOS_WRITE(sec, RTC_SECONDS);
#ifdef CONFIG_ACPI
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
	    acpi_gbl_FADT.century)
		CMOS_WRITE(century, acpi_gbl_FADT.century);
#endif

	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);

	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(mc146818_set_time);
