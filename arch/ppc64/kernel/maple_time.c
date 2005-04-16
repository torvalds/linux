/*
 *  arch/ppc64/kernel/maple_time.c
 *
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

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

extern void setup_default_decr(void);
extern void GregorianDay(struct rtc_time * tm);

extern unsigned long ppc_tb_freq;
extern unsigned long ppc_proc_freq;
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

void __init maple_get_boot_time(struct rtc_time *tm)
{
	struct device_node *rtcs;

	rtcs = find_compatible_devices("rtc", "pnpPNP,b00");
	if (rtcs && rtcs->addrs) {
		maple_rtc_addr = rtcs->addrs[0].address;
		printk(KERN_INFO "Maple: Found RTC at 0x%x\n", maple_rtc_addr);
	} else {
		maple_rtc_addr = RTC_PORT(0); /* legacy address */
		printk(KERN_INFO "Maple: No device node for RTC, assuming "
		       "legacy address (0x%x)\n", maple_rtc_addr);
	}
	
	maple_get_rtc_time(tm);
}

/* XXX FIXME: Some sane defaults: 125 MHz timebase, 1GHz processor */
#define DEFAULT_TB_FREQ		125000000UL
#define DEFAULT_PROC_FREQ	(DEFAULT_TB_FREQ * 8)

void __init maple_calibrate_decr(void)
{
	struct device_node *cpu;
	struct div_result divres;
	unsigned int *fp = NULL;

	/*
	 * The cpu node should have a timebase-frequency property
	 * to tell us the rate at which the decrementer counts.
	 */
	cpu = of_find_node_by_type(NULL, "cpu");

	ppc_tb_freq = DEFAULT_TB_FREQ;
	if (cpu != 0)
		fp = (unsigned int *)get_property(cpu, "timebase-frequency", NULL);
	if (fp != NULL)
		ppc_tb_freq = *fp;
	else
		printk(KERN_ERR "WARNING: Estimating decrementer frequency (not found)\n");
	fp = NULL;
	ppc_proc_freq = DEFAULT_PROC_FREQ;
	if (cpu != 0)
		fp = (unsigned int *)get_property(cpu, "clock-frequency", NULL);
	if (fp != NULL)
		ppc_proc_freq = *fp;
	else
		printk(KERN_ERR "WARNING: Estimating processor frequency (not found)\n");

	of_node_put(cpu);

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       ppc_tb_freq/1000000, ppc_tb_freq%1000000);
	printk(KERN_INFO "time_init: processor frequency   = %lu.%.6lu MHz\n",
	       ppc_proc_freq/1000000, ppc_proc_freq%1000000);

	tb_ticks_per_jiffy = ppc_tb_freq / HZ;
	tb_ticks_per_sec = tb_ticks_per_jiffy * HZ;
	tb_ticks_per_usec = ppc_tb_freq / 1000000;
	tb_to_us = mulhwu_scale_factor(ppc_tb_freq, 1000000);
	div128_by_32(1024*1024, 0, tb_ticks_per_sec, &divres);
	tb_to_xs = divres.result_low;

	setup_default_decr();
}
