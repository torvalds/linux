/*
 *  (c) Copyright 2004 Benjamin Herrenschmidt (benh@kernel.crashing.org),
 *                     IBM Corp. 
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/time.h>

#include "maple.h"

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

extern void GregorianDay(struct rtc_time * tm);

static int maple_rtc_addr;

static int maple_clock_read(int addr)
{
	outb_p(addr, maple_rtc_addr);
	return inb_p(maple_rtc_addr+1);
}

static void maple_clock_write(unsigned long val, int addr)
{
	outb_p(addr, maple_rtc_addr);
	outb_p(val, maple_rtc_addr+1);
}

void maple_get_rtc_time(struct rtc_time *tm)
{
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

	for (i = 0; i<1000000; i++) {
		uip = maple_clock_read(RTC_FREQ_SELECT);
		tm->tm_sec = maple_clock_read(RTC_SECONDS);
		tm->tm_min = maple_clock_read(RTC_MINUTES);
		tm->tm_hour = maple_clock_read(RTC_HOURS);
		tm->tm_mday = maple_clock_read(RTC_DAY_OF_MONTH);
		tm->tm_mon = maple_clock_read(RTC_MONTH);
		tm->tm_year = maple_clock_read(RTC_YEAR);
		uip |= maple_clock_read(RTC_FREQ_SELECT);
		if ((uip & RTC_UIP)==0)
			break;
	}

	if (!(maple_clock_read(RTC_CONTROL) & RTC_DM_BINARY)
	    || RTC_ALWAYS_BCD) {
		BCD_TO_BIN(tm->tm_sec);
		BCD_TO_BIN(tm->tm_min);
		BCD_TO_BIN(tm->tm_hour);
		BCD_TO_BIN(tm->tm_mday);
		BCD_TO_BIN(tm->tm_mon);
		BCD_TO_BIN(tm->tm_year);
	  }
	if ((tm->tm_year + 1900) < 1970)
		tm->tm_year += 100;

	GregorianDay(tm);
}

int maple_set_rtc_time(struct rtc_time *tm)
{
	unsigned char save_control, save_freq_select;
	int sec, min, hour, mon, mday, year;

	spin_lock(&rtc_lock);

	save_control = maple_clock_read(RTC_CONTROL); /* tell the clock it's being set */

	maple_clock_write((save_control|RTC_SET), RTC_CONTROL);

	save_freq_select = maple_clock_read(RTC_FREQ_SELECT); /* stop and reset prescaler */

	maple_clock_write((save_freq_select|RTC_DIV_RESET2), RTC_FREQ_SELECT);

	sec = tm->tm_sec;
	min = tm->tm_min;
	hour = tm->tm_hour;
	mon = tm->tm_mon;
	mday = tm->tm_mday;
	year = tm->tm_year;

	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		BIN_TO_BCD(sec);
		BIN_TO_BCD(min);
		BIN_TO_BCD(hour);
		BIN_TO_BCD(mon);
		BIN_TO_BCD(mday);
		BIN_TO_BCD(year);
	}
	maple_clock_write(sec, RTC_SECONDS);
	maple_clock_write(min, RTC_MINUTES);
	maple_clock_write(hour, RTC_HOURS);
	maple_clock_write(mon, RTC_MONTH);
	maple_clock_write(mday, RTC_DAY_OF_MONTH);
	maple_clock_write(year, RTC_YEAR);

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS12887 (popular MC146818A clone with integrated
	 * battery and quartz) will not reset the oscillator and will not
	 * update precisely 500 ms later. You won't find this mentioned in
	 * the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	maple_clock_write(save_control, RTC_CONTROL);
	maple_clock_write(save_freq_select, RTC_FREQ_SELECT);

	spin_unlock(&rtc_lock);

	return 0;
}

static struct resource rtc_iores = {
	.name = "rtc",
	.flags = IORESOURCE_BUSY,
};

unsigned long __init maple_get_boot_time(void)
{
	struct rtc_time tm;
	struct device_node *rtcs;

	rtcs = of_find_compatible_node(NULL, "rtc", "pnpPNP,b00");
	if (rtcs) {
		struct resource r;
		if (of_address_to_resource(rtcs, 0, &r)) {
			printk(KERN_EMERG "Maple: Unable to translate RTC"
			       " address\n");
			goto bail;
		}
		if (!(r.flags & IORESOURCE_IO)) {
			printk(KERN_EMERG "Maple: RTC address isn't PIO!\n");
			goto bail;
		}
		maple_rtc_addr = r.start;
		printk(KERN_INFO "Maple: Found RTC at IO 0x%x\n",
		       maple_rtc_addr);
	}
 bail:
	if (maple_rtc_addr == 0) {
		maple_rtc_addr = RTC_PORT(0); /* legacy address */
		printk(KERN_INFO "Maple: No device node for RTC, assuming "
		       "legacy address (0x%x)\n", maple_rtc_addr);
	}

	rtc_iores.start = maple_rtc_addr;
	rtc_iores.end = maple_rtc_addr + 7;
	request_resource(&ioport_resource, &rtc_iores);

	maple_get_rtc_time(&tm);
	return mktime(tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
		      tm.tm_hour, tm.tm_min, tm.tm_sec);
}

