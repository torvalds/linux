/*
 *  arch/ppc/platforms/chrp_time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * Adapted for PowerPC (PReP) by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu).
 * Copied and modified from arch/i386/kernel/time.c
 *
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/init.h>
#include <linux/bcd.h>

#include <asm/io.h>
#include <asm/nvram.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/time.h>

extern spinlock_t rtc_lock;

static int nvram_as1 = NVRAM_AS1;
static int nvram_as0 = NVRAM_AS0;
static int nvram_data = NVRAM_DATA;

long __init chrp_time_init(void)
{
	struct device_node *rtcs;
	int base;

	rtcs = find_compatible_devices("rtc", "pnpPNP,b00");
	if (rtcs == NULL)
		rtcs = find_compatible_devices("rtc", "ds1385-rtc");
	if (rtcs == NULL || rtcs->addrs == NULL)
		return 0;
	base = rtcs->addrs[0].address;
	nvram_as1 = 0;
	nvram_as0 = base;
	nvram_data = base + 1;

	return 0;
}

int chrp_cmos_clock_read(int addr)
{
	if (nvram_as1 != 0)
		outb(addr>>8, nvram_as1);
	outb(addr, nvram_as0);
	return (inb(nvram_data));
}

void chrp_cmos_clock_write(unsigned long val, int addr)
{
	if (nvram_as1 != 0)
		outb(addr>>8, nvram_as1);
	outb(addr, nvram_as0);
	outb(val, nvram_data);
	return;
}

/*
 * Set the hardware clock. -- Cort
 */
int chrp_set_rtc_time(struct rtc_time *tmarg)
{
	unsigned char save_control, save_freq_select;
	struct rtc_time tm = *tmarg;

	spin_lock(&rtc_lock);

	save_control = chrp_cmos_clock_read(RTC_CONTROL); /* tell the clock it's being set */

	chrp_cmos_clock_write((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = chrp_cmos_clock_read(RTC_FREQ_SELECT); /* stop and reset prescaler */

	chrp_cmos_clock_write((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

        tm.tm_year -= 1900;
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}
	chrp_cmos_clock_write(tm.tm_sec,RTC_SECONDS);
	chrp_cmos_clock_write(tm.tm_min,RTC_MINUTES);
	chrp_cmos_clock_write(tm.tm_hour,RTC_HOURS);
	chrp_cmos_clock_write(tm.tm_mon,RTC_MONTH);
	chrp_cmos_clock_write(tm.tm_mday,RTC_DAY_OF_MONTH);
	chrp_cmos_clock_write(tm.tm_year,RTC_YEAR);

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	chrp_cmos_clock_write(save_control, RTC_CONTROL);
	chrp_cmos_clock_write(save_freq_select, RTC_FREQ_SELECT);

	spin_unlock(&rtc_lock);
	return 0;
}

void chrp_get_rtc_time(struct rtc_time *tm)
{
	unsigned int year, mon, day, hour, min, sec;
	int uip, i;

	/* The Linux interpretation of the CMOS clock register contents:
	 * When the Update-In-Progress (UIP) flag goes from 1 to 0, the
	 * RTC registers show the second which has precisely just started.
	 * Let's hope other operating systems interpret the RTC the same way.
	 */

	/* Since the UIP flag is set for about 2.2 ms and the clock
	 * is typically written with a precision of 1 jiffy, trying
	 * to obtain a precision better than a few milliseconds is
	 * an illusion. Only consistency is interesting, this also
	 * allows to use the routine for /dev/rtc without a potential
	 * 1 second kernel busy loop triggered by any reader of /dev/rtc.
	 */

	for ( i = 0; i<1000000; i++) {
		uip = chrp_cmos_clock_read(RTC_FREQ_SELECT);
		sec = chrp_cmos_clock_read(RTC_SECONDS);
		min = chrp_cmos_clock_read(RTC_MINUTES);
		hour = chrp_cmos_clock_read(RTC_HOURS);
		day = chrp_cmos_clock_read(RTC_DAY_OF_MONTH);
		mon = chrp_cmos_clock_read(RTC_MONTH);
		year = chrp_cmos_clock_read(RTC_YEAR);
		uip |= chrp_cmos_clock_read(RTC_FREQ_SELECT);
		if ((uip & RTC_UIP)==0) break;
	}

	if (!(chrp_cmos_clock_read(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}
	if ((year += 1900) < 1970)
		year += 100;
	tm->tm_sec = sec;
	tm->tm_min = min;
	tm->tm_hour = hour;
	tm->tm_mday = day;
	tm->tm_mon = mon;
	tm->tm_year = year;
}


void __init chrp_calibrate_decr(void)
{
	struct device_node *cpu;
	unsigned int freq, *fp;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	freq = 16666000;		/* hardcoded default */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (unsigned int *)
			get_property(cpu, "timebase-frequency", NULL);
		if (fp != 0)
			freq = *fp;
	}
	ppc_tb_freq = freq;
}
