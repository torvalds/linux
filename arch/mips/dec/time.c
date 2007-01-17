/*
 *  linux/arch/mips/dec/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Copyright (C) 2000, 2003  Maciej W. Rozycki
 *
 * This file contains the time handling details for PC-style clocks as
 * found in some MIPS systems.
 *
 */
#include <linux/bcd.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/div64.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/sections.h>
#include <asm/time.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/machtype.h>

static unsigned long dec_rtc_get_time(void)
{
	unsigned int year, mon, day, hour, min, sec, real_year;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	do {
		sec = CMOS_READ(RTC_SECONDS);
		min = CMOS_READ(RTC_MINUTES);
		hour = CMOS_READ(RTC_HOURS);
		day = CMOS_READ(RTC_DAY_OF_MONTH);
		mon = CMOS_READ(RTC_MONTH);
		year = CMOS_READ(RTC_YEAR);
		/*
		 * The PROM will reset the year to either '72 or '73.
		 * Therefore we store the real year separately, in one
		 * of unused BBU RAM locations.
		 */
		real_year = CMOS_READ(RTC_DEC_YEAR);
	} while (sec != CMOS_READ(RTC_SECONDS));

	spin_unlock_irqrestore(&rtc_lock, flags);

	if (!(CMOS_READ(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		sec = BCD2BIN(sec);
		min = BCD2BIN(min);
		hour = BCD2BIN(hour);
		day = BCD2BIN(day);
		mon = BCD2BIN(mon);
		year = BCD2BIN(year);
	}

	year += real_year - 72 + 2000;

	return mktime(year, mon, day, hour, min, sec);
}

/*
 * In order to set the CMOS clock precisely, dec_rtc_set_mmss has to
 * be called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later.  Check the Dallas
 * DS1287 data sheet for details.
 */
static int dec_rtc_set_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;
	unsigned char save_control, save_freq_select;

	/* irq are locally disabled here */
	spin_lock(&rtc_lock);
	/* tell the clock it's being set */
	save_control = CMOS_READ(RTC_CONTROL);
	CMOS_WRITE((save_control | RTC_SET), RTC_CONTROL);

	/* stop and reset prescaler */
	save_freq_select = CMOS_READ(RTC_FREQ_SELECT);
	CMOS_WRITE((save_freq_select | RTC_DIV_RESET2), RTC_FREQ_SELECT);

	cmos_minutes = CMOS_READ(RTC_MINUTES);
	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
		cmos_minutes = BCD2BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15) / 30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - cmos_minutes) < 30) {
		if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
			real_seconds = BIN2BCD(real_seconds);
			real_minutes = BIN2BCD(real_minutes);
		}
		CMOS_WRITE(real_seconds, RTC_SECONDS);
		CMOS_WRITE(real_minutes, RTC_MINUTES);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	/* The following flags have to be released exactly in this order,
	 * otherwise the DS1287 will not reset the oscillator and will not
	 * update precisely 500 ms later.  You won't find this mentioned
	 * in the Dallas Semiconductor data sheets, but who believes data
	 * sheets anyway ...                           -- Markus Kuhn
	 */
	CMOS_WRITE(save_control, RTC_CONTROL);
	CMOS_WRITE(save_freq_select, RTC_FREQ_SELECT);
	spin_unlock(&rtc_lock);

	return retval;
}


static int dec_timer_state(void)
{
	return (CMOS_READ(RTC_REG_C) & RTC_PF) != 0;
}

static void dec_timer_ack(void)
{
	CMOS_READ(RTC_REG_C);			/* Ack the RTC interrupt.  */
}

static cycle_t dec_ioasic_hpt_read(void)
{
	/*
	 * The free-running counter is 32-bit which is good for about
	 * 2 minutes, 50 seconds at possible count rates of up to 25MHz.
	 */
	return ioasic_read(IO_REG_FCTR);
}


void __init dec_time_init(void)
{
	rtc_mips_get_time = dec_rtc_get_time;
	rtc_mips_set_mmss = dec_rtc_set_mmss;

	mips_timer_state = dec_timer_state;
	mips_timer_ack = dec_timer_ack;

	if (!cpu_has_counter && IOASIC)
		/* For pre-R4k systems we use the I/O ASIC's counter.  */
		clocksource_mips.read = dec_ioasic_hpt_read;

	/* Set up the rate of periodic DS1287 interrupts.  */
	CMOS_WRITE(RTC_REF_CLCK_32KHZ | (16 - __ffs(HZ)), RTC_REG_A);
}

void __init plat_timer_setup(struct irqaction *irq)
{
	setup_irq(dec_interrupt[DEC_IRQ_RTC], irq);

	/* Enable periodic DS1287 interrupts.  */
	CMOS_WRITE(CMOS_READ(RTC_REG_B) | RTC_PIE, RTC_REG_B);
}
