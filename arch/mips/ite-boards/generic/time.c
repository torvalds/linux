/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * Copyright (C) 2003 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Setting up the clock on the MIPS boards.
 */
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/spinlock.h>

#include <asm/time.h>
#include <asm/mipsregs.h>
#include <asm/ptrace.h>
#include <asm/it8172/it8172.h>
#include <asm/it8172/it8172_int.h>
#include <asm/debug.h>

#define IT8172_RTC_ADR_REG  (IT8172_PCI_IO_BASE + IT_RTC_BASE)
#define IT8172_RTC_DAT_REG  (IT8172_RTC_ADR_REG + 1)
#define IT8172_RTC_CENTURY_REG  (IT8172_PCI_IO_BASE + IT_RTC_CENTURY)

static volatile char *rtc_adr_reg = (char*)KSEG1ADDR(IT8172_RTC_ADR_REG);
static volatile char *rtc_dat_reg = (char*)KSEG1ADDR(IT8172_RTC_DAT_REG);
static volatile char *rtc_century_reg = (char*)KSEG1ADDR(IT8172_RTC_CENTURY_REG);

unsigned char it8172_rtc_read_data(unsigned long addr)
{
	unsigned char retval;

	*rtc_adr_reg = addr;
	retval =  *rtc_dat_reg;
	return retval;
}

void it8172_rtc_write_data(unsigned char data, unsigned long addr)
{
	*rtc_adr_reg = addr;
	*rtc_dat_reg = data;
}

#undef 	CMOS_READ
#undef 	CMOS_WRITE
#define	CMOS_READ(addr)			it8172_rtc_read_data(addr)
#define CMOS_WRITE(data, addr) 		it8172_rtc_write_data(data, addr)

static unsigned char saved_control;	/* remember rtc control reg */
static inline int rtc_24h(void) { return saved_control & RTC_24H; }
static inline int rtc_dm_binary(void) { return saved_control & RTC_DM_BINARY; }

static inline unsigned char
bin_to_hw(unsigned char c)
{
	if (rtc_dm_binary())
		return c;
	else
		return ((c/10) << 4) + (c%10);
}

static inline unsigned char
hw_to_bin(unsigned char c)
{
	if (rtc_dm_binary())
		return c;
	else
		return (c>>4)*10 + (c &0xf);
}

/* 0x80 bit indicates pm in 12-hour format */
static inline unsigned char
hour_bin_to_hw(unsigned char c)
{
	if (rtc_24h())
		return bin_to_hw(c);
	if (c >= 12)
		return 0x80 | bin_to_hw((c==12)?12:c-12);  /* 12 is 12pm */
	else
		return bin_to_hw((c==0)?12:c);	/* 0 is 12 AM, not 0 am */
}

static inline unsigned char
hour_hw_to_bin(unsigned char c)
{
	unsigned char tmp = hw_to_bin(c&0x3f);
	if (rtc_24h())
		return tmp;
	if (c & 0x80)
		return (tmp==12)?12:tmp+12;  	/* 12pm is 12, not 24 */
	else
		return (tmp==12)?0:tmp;		/* 12am is 0 */
}

static unsigned long r4k_offset; /* Amount to increment compare reg each time */
static unsigned long r4k_cur;    /* What counter should be at next timer irq */
extern unsigned int mips_hpt_frequency;

/*
 * Figure out the r4k offset, the amount to increment the compare
 * register for each time tick.
 * Use the RTC to calculate offset.
 */
static unsigned long __init cal_r4koff(void)
{
	unsigned int flags;

	local_irq_save(flags);

	/* Start counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	/* Start r4k counter. */
	write_c0_count(0);

	/* Read counter exactly on falling edge of update flag */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));

	mips_hpt_frequency = read_c0_count();

	/* restore interrupts */
	local_irq_restore(flags);

	return (mips_hpt_frequency / HZ);
}

static unsigned long
it8172_rtc_get_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned int flags;

	/* avoid update-in-progress. */
	for (;;) {
		local_irq_save(flags);
		if (! (CMOS_READ(RTC_REG_A) & RTC_UIP))
			break;
		/* don't hold intr closed all the time */
		local_irq_restore(flags);
	}

	/* Read regs. */
	sec = hw_to_bin(CMOS_READ(RTC_SECONDS));
	min = hw_to_bin(CMOS_READ(RTC_MINUTES));
	hour = hour_hw_to_bin(CMOS_READ(RTC_HOURS));
	day = hw_to_bin(CMOS_READ(RTC_DAY_OF_MONTH));
	mon = hw_to_bin(CMOS_READ(RTC_MONTH));
	year = hw_to_bin(CMOS_READ(RTC_YEAR)) +
		hw_to_bin(*rtc_century_reg) * 100;

	/* restore interrupts */
	local_irq_restore(flags);

	return mktime(year, mon, day, hour, min, sec);
}

static int
it8172_rtc_set_time(unsigned long t)
{
	struct rtc_time tm;
	unsigned int flags;

	/* convert */
	to_tm(t, &tm);

	/* avoid update-in-progress. */
	for (;;) {
		local_irq_save(flags);
		if (! (CMOS_READ(RTC_REG_A) & RTC_UIP))
			break;
		/* don't hold intr closed all the time */
		local_irq_restore(flags);
	}

	*rtc_century_reg = bin_to_hw(tm.tm_year/100);
	CMOS_WRITE(bin_to_hw(tm.tm_sec), RTC_SECONDS);
	CMOS_WRITE(bin_to_hw(tm.tm_min), RTC_MINUTES);
	CMOS_WRITE(hour_bin_to_hw(tm.tm_hour), RTC_HOURS);
	CMOS_WRITE(bin_to_hw(tm.tm_mday), RTC_DAY_OF_MONTH);
	CMOS_WRITE(bin_to_hw(tm.tm_mon+1), RTC_MONTH);	/* tm_mon starts from 0 */
	CMOS_WRITE(bin_to_hw(tm.tm_year%100), RTC_YEAR);

	/* restore interrupts */
	local_irq_restore(flags);

	return 0;
}

void __init it8172_time_init(void)
{
        unsigned int est_freq, flags;

	local_irq_save(flags);

	saved_control = CMOS_READ(RTC_CONTROL);

	printk("calculating r4koff... ");
	r4k_offset = cal_r4koff();
	printk("%08lx(%d)\n", r4k_offset, (int) r4k_offset);

	est_freq = 2*r4k_offset*HZ;
	est_freq += 5000;    /* round */
	est_freq -= est_freq%10000;
	printk("CPU frequency %d.%02d MHz\n", est_freq/1000000,
	       (est_freq%1000000)*100/1000000);

	local_irq_restore(flags);

	rtc_mips_get_time = it8172_rtc_get_time;
	rtc_mips_set_time = it8172_rtc_set_time;
}

#define ALLINTS (IE_IRQ0 | IE_IRQ1 | IE_IRQ2 | IE_IRQ3 | IE_IRQ4 | IE_IRQ5)
void __init it8172_timer_setup(struct irqaction *irq)
{
	puts("timer_setup\n");
	put32(NR_IRQS);
	puts("");
        /* we are using the cpu counter for timer interrupts */
	setup_irq(MIPS_CPU_TIMER_IRQ, irq);

        /* to generate the first timer interrupt */
	r4k_cur = (read_c0_count() + r4k_offset);
	write_c0_compare(r4k_cur);
	set_c0_status(ALLINTS);
}
