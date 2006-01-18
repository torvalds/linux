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
int chrp_set_rtc_time(unsigned long nowtime)
{
	unsigned char save_control, save_freq_select;
	struct rtc_time tm;

	spin_lock(&rtc_lock);
	to_tm(nowtime, &tm);

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

unsigned long chrp_get_rtc_time(void)
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

	if (!(chrp_cmos_clock_read(RTC_CONTROL) & RTC_DM_BINARY) || RTC_ALWAYS_BCD)
	  {
	    BCD_TO_BIN(sec);
	    BCD_TO_BIN(min);
	    BCD_TO_BIN(hour);
	    BCD_TO_BIN(day);
	    BCD_TO_BIN(mon);
	    BCD_TO_BIN(year);
	  }
	if ((year += 1900) < 1970)
		year += 100;
	return mktime(year, mon, day, hour, min, sec);
}

/*
 * Calibrate the decrementer frequency with the VIA timer 1.
 */
#define VIA_TIMER_FREQ_6	4700000	/* time 1 frequency * 6 */

/* VIA registers */
#define RS		0x200		/* skip between registers */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define ACR		(11*RS)		/* Auxiliary control register */
#define IFR		(13*RS)		/* Interrupt flag register */

/* Bits in ACR */
#define T1MODE		0xc0		/* Timer 1 mode */
#define T1MODE_CONT	0x40		/*  continuous interrupts */

/* Bits in IFR and IER */
#define T1_INT		0x40		/* Timer 1 interrupt */

static int __init chrp_via_calibrate_decr(void)
{
	struct device_node *vias;
	volatile unsigned char __iomem *via;
	int count = VIA_TIMER_FREQ_6 / 100;
	unsigned int dstart, dend;

	vias = find_devices("via-cuda");
	if (vias == 0)
		vias = find_devices("via");
	if (vias == 0 || vias->n_addrs == 0)
		return 0;
	via = ioremap(vias->addrs[0].address, vias->addrs[0].size);

	/* set timer 1 for continuous interrupts */
	out_8(&via[ACR], (via[ACR] & ~T1MODE) | T1MODE_CONT);
	/* set the counter to a small value */
	out_8(&via[T1CH], 2);
	/* set the latch to `count' */
	out_8(&via[T1LL], count);
	out_8(&via[T1LH], count >> 8);
	/* wait until it hits 0 */
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dstart = get_dec();
	/* clear the interrupt & wait until it hits 0 again */
	in_8(&via[T1CL]);
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dend = get_dec();

	tb_ticks_per_jiffy = (dstart - dend) / ((6 * HZ)/100);
	tb_to_us = mulhwu_scale_factor(dstart - dend, 60000);

	printk(KERN_INFO "via_calibrate_decr: ticks per jiffy = %u (%u ticks)\n",
	       tb_ticks_per_jiffy, dstart - dend);

	iounmap(via);
	
	return 1;
}

void __init chrp_calibrate_decr(void)
{
	struct device_node *cpu;
	unsigned int freq, *fp;

	if (chrp_via_calibrate_decr())
		return;

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
	printk("time_init: decrementer frequency = %u.%.6u MHz\n",
 	       freq/1000000, freq%1000000);
	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}
