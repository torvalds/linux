/*
 * HP i8042 SDC + MSM-58321 BBRTC driver.
 *
 * Copyright (c) 2001 Brian S. Julin
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 * References:
 * System Device Controller Microprocessor Firmware Theory of Operation
 *      for Part Number 1820-4784 Revision B.  Dwg No. A-1820-4784-2
 * efirtc.c by Stephane Eranian/Hewlett Packard
 *
 */

#include <linux/hp_sdc.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/rtc.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>

MODULE_AUTHOR("Brian S. Julin <bri@calyx.com>");
MODULE_DESCRIPTION("HP i8042 SDC + MSM-58321 RTC Driver");
MODULE_LICENSE("Dual BSD/GPL");

#define RTC_VERSION "1.10d"

static unsigned long epoch = 2000;

static struct semaphore i8042tregs;

static void hp_sdc_rtc_isr (int irq, void *dev_id, 
			    uint8_t status, uint8_t data) 
{
	return;
}

static int hp_sdc_rtc_do_read_bbrtc (struct rtc_time *rtctm)
{
	struct semaphore tsem;
	hp_sdc_transaction t;
	uint8_t tseq[91];
	int i;
	
	i = 0;
	while (i < 91) {
		tseq[i++] = HP_SDC_ACT_DATAREG |
			HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN;
		tseq[i++] = 0x01;			/* write i8042[0x70] */
	  	tseq[i]   = i / 7;			/* BBRTC reg address */
		i++;
		tseq[i++] = HP_SDC_CMD_DO_RTCR;		/* Trigger command   */
		tseq[i++] = 2;		/* expect 1 stat/dat pair back.   */
		i++; i++;               /* buffer for stat/dat pair       */
	}
	tseq[84] |= HP_SDC_ACT_SEMAPHORE;
	t.endidx =		91;
	t.seq =			tseq;
	t.act.semaphore =	&tsem;
	sema_init(&tsem, 0);
	
	if (hp_sdc_enqueue_transaction(&t)) return -1;
	
	/* Put ourselves to sleep for results. */
	if (WARN_ON(down_interruptible(&tsem)))
		return -1;
	
	/* Check for nonpresence of BBRTC */
	if (!((tseq[83] | tseq[90] | tseq[69] | tseq[76] |
	       tseq[55] | tseq[62] | tseq[34] | tseq[41] |
	       tseq[20] | tseq[27] | tseq[6]  | tseq[13]) & 0x0f))
		return -1;

	memset(rtctm, 0, sizeof(struct rtc_time));
	rtctm->tm_year = (tseq[83] & 0x0f) + (tseq[90] & 0x0f) * 10;
	rtctm->tm_mon  = (tseq[69] & 0x0f) + (tseq[76] & 0x0f) * 10;
	rtctm->tm_mday = (tseq[55] & 0x0f) + (tseq[62] & 0x0f) * 10;
	rtctm->tm_wday = (tseq[48] & 0x0f);
	rtctm->tm_hour = (tseq[34] & 0x0f) + (tseq[41] & 0x0f) * 10;
	rtctm->tm_min  = (tseq[20] & 0x0f) + (tseq[27] & 0x0f) * 10;
	rtctm->tm_sec  = (tseq[6]  & 0x0f) + (tseq[13] & 0x0f) * 10;
	
	return 0;
}

static int hp_sdc_rtc_read_bbrtc (struct rtc_time *rtctm)
{
	struct rtc_time tm, tm_last;
	int i = 0;

	/* MSM-58321 has no read latch, so must read twice and compare. */

	if (hp_sdc_rtc_do_read_bbrtc(&tm_last)) return -1;
	if (hp_sdc_rtc_do_read_bbrtc(&tm)) return -1;

	while (memcmp(&tm, &tm_last, sizeof(struct rtc_time))) {
		if (i++ > 4) return -1;
		memcpy(&tm_last, &tm, sizeof(struct rtc_time));
		if (hp_sdc_rtc_do_read_bbrtc(&tm)) return -1;
	}

	memcpy(rtctm, &tm, sizeof(struct rtc_time));

	return 0;
}


static int64_t hp_sdc_rtc_read_i8042timer (uint8_t loadcmd, int numreg)
{
	hp_sdc_transaction t;
	uint8_t tseq[26] = {
		HP_SDC_ACT_PRECMD | HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN,
		0,
		HP_SDC_CMD_READ_T1, 2, 0, 0,
		HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN, 
		HP_SDC_CMD_READ_T2, 2, 0, 0,
		HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN, 
		HP_SDC_CMD_READ_T3, 2, 0, 0,
		HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN, 
		HP_SDC_CMD_READ_T4, 2, 0, 0,
		HP_SDC_ACT_POSTCMD | HP_SDC_ACT_DATAIN, 
		HP_SDC_CMD_READ_T5, 2, 0, 0
	};

	t.endidx = numreg * 5;

	tseq[1] = loadcmd;
	tseq[t.endidx - 4] |= HP_SDC_ACT_SEMAPHORE; /* numreg assumed > 1 */

	t.seq =			tseq;
	t.act.semaphore =	&i8042tregs;

	/* Sleep if output regs in use. */
	if (WARN_ON(down_interruptible(&i8042tregs)))
		return -1;

	if (hp_sdc_enqueue_transaction(&t)) {
		up(&i8042tregs);
		return -1;
	}
	
	/* Sleep until results come back. */
	if (WARN_ON(down_interruptible(&i8042tregs)))
		return -1;

	up(&i8042tregs);

	return (tseq[5] | 
		((uint64_t)(tseq[10]) << 8)  | ((uint64_t)(tseq[15]) << 16) |
		((uint64_t)(tseq[20]) << 24) | ((uint64_t)(tseq[25]) << 32));
}


/* Read the i8042 real-time clock */
static inline int hp_sdc_rtc_read_rt(struct timespec64 *res) {
	int64_t raw;
	uint32_t tenms; 
	unsigned int days;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_RT, 5);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;
	days  = (unsigned int)(raw >> 24) & 0xffff;

	res->tv_nsec = (long)(tenms % 100) * 10000 * 1000;
	res->tv_sec =  (tenms / 100) + (time64_t)days * 86400;

	return 0;
}


/* Read the i8042 fast handshake timer */
static inline int hp_sdc_rtc_read_fhs(struct timespec64 *res) {
	int64_t raw;
	unsigned int tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_FHS, 2);
	if (raw < 0) return -1;

	tenms = (unsigned int)raw & 0xffff;

	res->tv_nsec = (long)(tenms % 100) * 10000 * 1000;
	res->tv_sec  = (time64_t)(tenms / 100);

	return 0;
}


/* Read the i8042 match timer (a.k.a. alarm) */
static inline int hp_sdc_rtc_read_mt(struct timespec64 *res) {
	int64_t raw;	
	uint32_t tenms; 

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_MT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_nsec = (long)(tenms % 100) * 10000 * 1000;
	res->tv_sec  = (time64_t)(tenms / 100);

	return 0;
}


/* Read the i8042 delay timer */
static inline int hp_sdc_rtc_read_dt(struct timespec64 *res) {
	int64_t raw;
	uint32_t tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_DT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_nsec = (long)(tenms % 100) * 10000 * 1000;
	res->tv_sec  = (time64_t)(tenms / 100);

	return 0;
}


/* Read the i8042 cycle timer (a.k.a. periodic) */
static inline int hp_sdc_rtc_read_ct(struct timespec64 *res) {
	int64_t raw;
	uint32_t tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_CT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_nsec = (long)(tenms % 100) * 10000 * 1000;
	res->tv_sec  = (time64_t)(tenms / 100);

	return 0;
}

static int hp_sdc_rtc_proc_show(struct seq_file *m, void *v)
{
#define YN(bit) ("no")
#define NY(bit) ("yes")
        struct rtc_time tm;
	struct timespec64 tv;

	memset(&tm, 0, sizeof(struct rtc_time));

	if (hp_sdc_rtc_read_bbrtc(&tm)) {
		seq_puts(m, "BBRTC\t\t: READ FAILED!\n");
	} else {
		seq_printf(m,
			     "rtc_time\t: %ptRt\n"
			     "rtc_date\t: %ptRd\n"
			     "rtc_epoch\t: %04lu\n",
			     &tm, &tm, epoch);
	}

	if (hp_sdc_rtc_read_rt(&tv)) {
		seq_puts(m, "i8042 rtc\t: READ FAILED!\n");
	} else {
		seq_printf(m, "i8042 rtc\t: %lld.%02ld seconds\n",
			     (s64)tv.tv_sec, (long)tv.tv_nsec/1000000L);
	}

	if (hp_sdc_rtc_read_fhs(&tv)) {
		seq_puts(m, "handshake\t: READ FAILED!\n");
	} else {
		seq_printf(m, "handshake\t: %lld.%02ld seconds\n",
			     (s64)tv.tv_sec, (long)tv.tv_nsec/1000000L);
	}

	if (hp_sdc_rtc_read_mt(&tv)) {
		seq_puts(m, "alarm\t\t: READ FAILED!\n");
	} else {
		seq_printf(m, "alarm\t\t: %lld.%02ld seconds\n",
			     (s64)tv.tv_sec, (long)tv.tv_nsec/1000000L);
	}

	if (hp_sdc_rtc_read_dt(&tv)) {
		seq_puts(m, "delay\t\t: READ FAILED!\n");
	} else {
		seq_printf(m, "delay\t\t: %lld.%02ld seconds\n",
			     (s64)tv.tv_sec, (long)tv.tv_nsec/1000000L);
	}

	if (hp_sdc_rtc_read_ct(&tv)) {
		seq_puts(m, "periodic\t: READ FAILED!\n");
	} else {
		seq_printf(m, "periodic\t: %lld.%02ld seconds\n",
			     (s64)tv.tv_sec, (long)tv.tv_nsec/1000000L);
	}

        seq_printf(m,
                     "DST_enable\t: %s\n"
                     "BCD\t\t: %s\n"
                     "24hr\t\t: %s\n"
                     "square_wave\t: %s\n"
                     "alarm_IRQ\t: %s\n"
                     "update_IRQ\t: %s\n"
                     "periodic_IRQ\t: %s\n"
		     "periodic_freq\t: %ld\n"
                     "batt_status\t: %s\n",
                     YN(RTC_DST_EN),
                     NY(RTC_DM_BINARY),
                     YN(RTC_24H),
                     YN(RTC_SQWE),
                     YN(RTC_AIE),
                     YN(RTC_UIE),
                     YN(RTC_PIE),
                     1UL,
                     1 ? "okay" : "dead");

        return 0;
#undef YN
#undef NY
}

static int __init hp_sdc_rtc_init(void)
{
	int ret;

#ifdef __mc68000__
	if (!MACH_IS_HP300)
		return -ENODEV;
#endif

	sema_init(&i8042tregs, 1);

	if ((ret = hp_sdc_request_timer_irq(&hp_sdc_rtc_isr)))
		return ret;

        proc_create_single("driver/rtc", 0, NULL, hp_sdc_rtc_proc_show);

	printk(KERN_INFO "HP i8042 SDC + MSM-58321 RTC support loaded "
			 "(RTC v " RTC_VERSION ")\n");

	return 0;
}

static void __exit hp_sdc_rtc_exit(void)
{
	remove_proc_entry ("driver/rtc", NULL);
	hp_sdc_release_timer_irq(hp_sdc_rtc_isr);
        printk(KERN_INFO "HP i8042 SDC + MSM-58321 RTC support unloaded\n");
}

module_init(hp_sdc_rtc_init);
module_exit(hp_sdc_rtc_exit);
