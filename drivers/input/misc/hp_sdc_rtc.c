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
#include <linux/poll.h>
#include <linux/rtc.h>

MODULE_AUTHOR("Brian S. Julin <bri@calyx.com>");
MODULE_DESCRIPTION("HP i8042 SDC + MSM-58321 RTC Driver");
MODULE_LICENSE("Dual BSD/GPL");

#define RTC_VERSION "1.10d"

static unsigned long epoch = 2000;

static struct semaphore i8042tregs;

static hp_sdc_irqhook hp_sdc_rtc_isr;

static struct fasync_struct *hp_sdc_rtc_async_queue;

static DECLARE_WAIT_QUEUE_HEAD(hp_sdc_rtc_wait);

static ssize_t hp_sdc_rtc_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos);

static int hp_sdc_rtc_ioctl(struct inode *inode, struct file *file,
			    unsigned int cmd, unsigned long arg);

static unsigned int hp_sdc_rtc_poll(struct file *file, poll_table *wait);

static int hp_sdc_rtc_open(struct inode *inode, struct file *file);
static int hp_sdc_rtc_release(struct inode *inode, struct file *file);
static int hp_sdc_rtc_fasync (int fd, struct file *filp, int on);

static int hp_sdc_rtc_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data);

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
	init_MUTEX_LOCKED(&tsem);
	
	if (hp_sdc_enqueue_transaction(&t)) return -1;
	
	down_interruptible(&tsem);  /* Put ourselves to sleep for results. */
	
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

	down_interruptible(&i8042tregs);  /* Sleep if output regs in use. */

	if (hp_sdc_enqueue_transaction(&t)) return -1;
	
	down_interruptible(&i8042tregs);  /* Sleep until results come back. */
	up(&i8042tregs);

	return (tseq[5] | 
		((uint64_t)(tseq[10]) << 8)  | ((uint64_t)(tseq[15]) << 16) |
		((uint64_t)(tseq[20]) << 24) | ((uint64_t)(tseq[25]) << 32));
}


/* Read the i8042 real-time clock */
static inline int hp_sdc_rtc_read_rt(struct timeval *res) {
	int64_t raw;
	uint32_t tenms; 
	unsigned int days;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_RT, 5);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;
	days  = (unsigned int)(raw >> 24) & 0xffff;

	res->tv_usec = (suseconds_t)(tenms % 100) * 10000;
	res->tv_sec =  (time_t)(tenms / 100) + days * 86400;

	return 0;
}


/* Read the i8042 fast handshake timer */
static inline int hp_sdc_rtc_read_fhs(struct timeval *res) {
	uint64_t raw;
	unsigned int tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_FHS, 2);
	if (raw < 0) return -1;

	tenms = (unsigned int)raw & 0xffff;

	res->tv_usec = (suseconds_t)(tenms % 100) * 10000;
	res->tv_sec  = (time_t)(tenms / 100);

	return 0;
}


/* Read the i8042 match timer (a.k.a. alarm) */
static inline int hp_sdc_rtc_read_mt(struct timeval *res) {
	int64_t raw;	
	uint32_t tenms; 

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_MT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_usec = (suseconds_t)(tenms % 100) * 10000;
	res->tv_sec  = (time_t)(tenms / 100);

	return 0;
}


/* Read the i8042 delay timer */
static inline int hp_sdc_rtc_read_dt(struct timeval *res) {
	int64_t raw;
	uint32_t tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_DT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_usec = (suseconds_t)(tenms % 100) * 10000;
	res->tv_sec  = (time_t)(tenms / 100);

	return 0;
}


/* Read the i8042 cycle timer (a.k.a. periodic) */
static inline int hp_sdc_rtc_read_ct(struct timeval *res) {
	int64_t raw;
	uint32_t tenms;

	raw = hp_sdc_rtc_read_i8042timer(HP_SDC_CMD_LOAD_CT, 3);
	if (raw < 0) return -1;

	tenms = (uint32_t)raw & 0xffffff;

	res->tv_usec = (suseconds_t)(tenms % 100) * 10000;
	res->tv_sec  = (time_t)(tenms / 100);

	return 0;
}


/* Set the i8042 real-time clock */
static int hp_sdc_rtc_set_rt (struct timeval *setto)
{
	uint32_t tenms;
	unsigned int days;
	hp_sdc_transaction t;
	uint8_t tseq[11] = {
		HP_SDC_ACT_PRECMD | HP_SDC_ACT_DATAOUT,
		HP_SDC_CMD_SET_RTMS, 3, 0, 0, 0,
		HP_SDC_ACT_PRECMD | HP_SDC_ACT_DATAOUT,
		HP_SDC_CMD_SET_RTD, 2, 0, 0 
	};

	t.endidx = 10;

	if (0xffff < setto->tv_sec / 86400) return -1;
	days = setto->tv_sec / 86400;
	if (0xffff < setto->tv_usec / 1000000 / 86400) return -1;
	days += ((setto->tv_sec % 86400) + setto->tv_usec / 1000000) / 86400;
	if (days > 0xffff) return -1;

	if (0xffffff < setto->tv_sec) return -1;
	tenms  = setto->tv_sec * 100;
	if (0xffffff < setto->tv_usec / 10000) return -1;
	tenms += setto->tv_usec / 10000;
	if (tenms > 0xffffff) return -1;

	tseq[3] = (uint8_t)(tenms & 0xff);
	tseq[4] = (uint8_t)((tenms >> 8)  & 0xff);
	tseq[5] = (uint8_t)((tenms >> 16) & 0xff);

	tseq[9] = (uint8_t)(days & 0xff);
	tseq[10] = (uint8_t)((days >> 8) & 0xff);

	t.seq =	tseq;

	if (hp_sdc_enqueue_transaction(&t)) return -1;
	return 0;
}

/* Set the i8042 fast handshake timer */
static int hp_sdc_rtc_set_fhs (struct timeval *setto)
{
	uint32_t tenms;
	hp_sdc_transaction t;
	uint8_t tseq[5] = {
		HP_SDC_ACT_PRECMD | HP_SDC_ACT_DATAOUT,
		HP_SDC_CMD_SET_FHS, 2, 0, 0
	};

	t.endidx = 4;

	if (0xffff < setto->tv_sec) return -1;
	tenms  = setto->tv_sec * 100;
	if (0xffff < setto->tv_usec / 10000) return -1;
	tenms += setto->tv_usec / 10000;
	if (tenms > 0xffff) return -1;

	tseq[3] = (uint8_t)(tenms & 0xff);
	tseq[4] = (uint8_t)((tenms >> 8)  & 0xff);

	t.seq =	tseq;

	if (hp_sdc_enqueue_transaction(&t)) return -1;
	return 0;
}


/* Set the i8042 match timer (a.k.a. alarm) */
#define hp_sdc_rtc_set_mt (setto) \
	hp_sdc_rtc_set_i8042timer(setto, HP_SDC_CMD_SET_MT)

/* Set the i8042 delay timer */
#define hp_sdc_rtc_set_dt (setto) \
	hp_sdc_rtc_set_i8042timer(setto, HP_SDC_CMD_SET_DT)

/* Set the i8042 cycle timer (a.k.a. periodic) */
#define hp_sdc_rtc_set_ct (setto) \
	hp_sdc_rtc_set_i8042timer(setto, HP_SDC_CMD_SET_CT)

/* Set one of the i8042 3-byte wide timers */
static int hp_sdc_rtc_set_i8042timer (struct timeval *setto, uint8_t setcmd)
{
	uint32_t tenms;
	hp_sdc_transaction t;
	uint8_t tseq[6] = {
		HP_SDC_ACT_PRECMD | HP_SDC_ACT_DATAOUT,
		0, 3, 0, 0, 0
	};

	t.endidx = 6;

	if (0xffffff < setto->tv_sec) return -1;
	tenms  = setto->tv_sec * 100;
	if (0xffffff < setto->tv_usec / 10000) return -1;
	tenms += setto->tv_usec / 10000;
	if (tenms > 0xffffff) return -1;

	tseq[1] = setcmd;
	tseq[3] = (uint8_t)(tenms & 0xff);
	tseq[4] = (uint8_t)((tenms >> 8)  & 0xff);
	tseq[5] = (uint8_t)((tenms >> 16)  & 0xff);

	t.seq =			tseq;

	if (hp_sdc_enqueue_transaction(&t)) { 
		return -1;
	}
	return 0;
}

static ssize_t hp_sdc_rtc_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos) {
	ssize_t retval;

        if (count < sizeof(unsigned long))
                return -EINVAL;

	retval = put_user(68, (unsigned long __user *)buf);
	return retval;
}

static unsigned int hp_sdc_rtc_poll(struct file *file, poll_table *wait)
{
        unsigned long l;

	l = 0;
        if (l != 0)
                return POLLIN | POLLRDNORM;
        return 0;
}

static int hp_sdc_rtc_open(struct inode *inode, struct file *file)
{
        return 0;
}

static int hp_sdc_rtc_release(struct inode *inode, struct file *file)
{
	/* Turn off interrupts? */

        if (file->f_flags & FASYNC) {
                hp_sdc_rtc_fasync (-1, file, 0);
        }

        return 0;
}

static int hp_sdc_rtc_fasync (int fd, struct file *filp, int on)
{
        return fasync_helper (fd, filp, on, &hp_sdc_rtc_async_queue);
}

static int hp_sdc_rtc_proc_output (char *buf)
{
#define YN(bit) ("no")
#define NY(bit) ("yes")
        char *p;
        struct rtc_time tm;
	struct timeval tv;

	memset(&tm, 0, sizeof(struct rtc_time));

	p = buf;

	if (hp_sdc_rtc_read_bbrtc(&tm)) {
		p += sprintf(p, "BBRTC\t\t: READ FAILED!\n");
	} else {
		p += sprintf(p,
			     "rtc_time\t: %02d:%02d:%02d\n"
			     "rtc_date\t: %04d-%02d-%02d\n"
			     "rtc_epoch\t: %04lu\n",
			     tm.tm_hour, tm.tm_min, tm.tm_sec,
			     tm.tm_year + 1900, tm.tm_mon + 1, 
			     tm.tm_mday, epoch);
	}

	if (hp_sdc_rtc_read_rt(&tv)) {
		p += sprintf(p, "i8042 rtc\t: READ FAILED!\n");
	} else {
		p += sprintf(p, "i8042 rtc\t: %ld.%02d seconds\n", 
			     tv.tv_sec, tv.tv_usec/1000);
	}

	if (hp_sdc_rtc_read_fhs(&tv)) {
		p += sprintf(p, "handshake\t: READ FAILED!\n");
	} else {
        	p += sprintf(p, "handshake\t: %ld.%02d seconds\n", 
			     tv.tv_sec, tv.tv_usec/1000);
	}

	if (hp_sdc_rtc_read_mt(&tv)) {
		p += sprintf(p, "alarm\t\t: READ FAILED!\n");
	} else {
		p += sprintf(p, "alarm\t\t: %ld.%02d seconds\n", 
			     tv.tv_sec, tv.tv_usec/1000);
	}

	if (hp_sdc_rtc_read_dt(&tv)) {
		p += sprintf(p, "delay\t\t: READ FAILED!\n");
	} else {
		p += sprintf(p, "delay\t\t: %ld.%02d seconds\n", 
			     tv.tv_sec, tv.tv_usec/1000);
	}

	if (hp_sdc_rtc_read_ct(&tv)) {
		p += sprintf(p, "periodic\t: READ FAILED!\n");
	} else {
		p += sprintf(p, "periodic\t: %ld.%02d seconds\n", 
			     tv.tv_sec, tv.tv_usec/1000);
	}

        p += sprintf(p,
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

        return  p - buf;
#undef YN
#undef NY
}

static int hp_sdc_rtc_read_proc(char *page, char **start, off_t off,
                         int count, int *eof, void *data)
{
	int len = hp_sdc_rtc_proc_output (page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

static int hp_sdc_rtc_ioctl(struct inode *inode, struct file *file, 
			    unsigned int cmd, unsigned long arg)
{
#if 1
	return -EINVAL;
#else
	
        struct rtc_time wtime; 
	struct timeval ttime;
	int use_wtime = 0;

	/* This needs major work. */

        switch (cmd) {

        case RTC_AIE_OFF:       /* Mask alarm int. enab. bit    */
        case RTC_AIE_ON:        /* Allow alarm interrupts.      */
	case RTC_PIE_OFF:       /* Mask periodic int. enab. bit */
        case RTC_PIE_ON:        /* Allow periodic ints          */
        case RTC_UIE_ON:        /* Allow ints for RTC updates.  */
        case RTC_UIE_OFF:       /* Allow ints for RTC updates.  */
        {
		/* We cannot mask individual user timers and we
		   cannot tell them apart when they occur, so it 
		   would be disingenuous to succeed these IOCTLs */
		return -EINVAL;
        }
        case RTC_ALM_READ:      /* Read the present alarm time */
        {
		if (hp_sdc_rtc_read_mt(&ttime)) return -EFAULT;
		if (hp_sdc_rtc_read_bbrtc(&wtime)) return -EFAULT;

		wtime.tm_hour = ttime.tv_sec / 3600;  ttime.tv_sec %= 3600;
		wtime.tm_min  = ttime.tv_sec / 60;    ttime.tv_sec %= 60;
		wtime.tm_sec  = ttime.tv_sec;
                
		break;
        }
        case RTC_IRQP_READ:     /* Read the periodic IRQ rate.  */
        {
                return put_user(hp_sdc_rtc_freq, (unsigned long *)arg);
        }
        case RTC_IRQP_SET:      /* Set periodic IRQ rate.       */
        {
                /* 
                 * The max we can do is 100Hz.
		 */

                if ((arg < 1) || (arg > 100)) return -EINVAL;
		ttime.tv_sec = 0;
		ttime.tv_usec = 1000000 / arg;
		if (hp_sdc_rtc_set_ct(&ttime)) return -EFAULT;
		hp_sdc_rtc_freq = arg;
                return 0;
        }
        case RTC_ALM_SET:       /* Store a time into the alarm */
        {
                /*
                 * This expects a struct hp_sdc_rtc_time. Writing 0xff means
                 * "don't care" or "match all" for PC timers.  The HP SDC
		 * does not support that perk, but it could be emulated fairly
		 * easily.  Only the tm_hour, tm_min and tm_sec are used.
		 * We could do it with 10ms accuracy with the HP SDC, if the 
		 * rtc interface left us a way to do that.
                 */
                struct hp_sdc_rtc_time alm_tm;

                if (copy_from_user(&alm_tm, (struct hp_sdc_rtc_time*)arg,
                                   sizeof(struct hp_sdc_rtc_time)))
                       return -EFAULT;

                if (alm_tm.tm_hour > 23) return -EINVAL;
		if (alm_tm.tm_min  > 59) return -EINVAL;
		if (alm_tm.tm_sec  > 59) return -EINVAL;  

		ttime.sec = alm_tm.tm_hour * 3600 + 
		  alm_tm.tm_min * 60 + alm_tm.tm_sec;
		ttime.usec = 0;
		if (hp_sdc_rtc_set_mt(&ttime)) return -EFAULT;
                return 0;
        }
        case RTC_RD_TIME:       /* Read the time/date from RTC  */
        {
		if (hp_sdc_rtc_read_bbrtc(&wtime)) return -EFAULT;
                break;
        }
        case RTC_SET_TIME:      /* Set the RTC */
        {
                struct rtc_time hp_sdc_rtc_tm;
                unsigned char mon, day, hrs, min, sec, leap_yr;
                unsigned int yrs;

                if (!capable(CAP_SYS_TIME))
                        return -EACCES;
		if (copy_from_user(&hp_sdc_rtc_tm, (struct rtc_time *)arg,
                                   sizeof(struct rtc_time)))
                        return -EFAULT;

                yrs = hp_sdc_rtc_tm.tm_year + 1900;
                mon = hp_sdc_rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
                day = hp_sdc_rtc_tm.tm_mday;
                hrs = hp_sdc_rtc_tm.tm_hour;
                min = hp_sdc_rtc_tm.tm_min;
                sec = hp_sdc_rtc_tm.tm_sec;

                if (yrs < 1970)
                        return -EINVAL;

                leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

                if ((mon > 12) || (day == 0))
                        return -EINVAL;
                if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
                        return -EINVAL;
		if ((hrs >= 24) || (min >= 60) || (sec >= 60))
                        return -EINVAL;

                if ((yrs -= eH) > 255)    /* They are unsigned */
                        return -EINVAL;


                return 0;
        }
        case RTC_EPOCH_READ:    /* Read the epoch.      */
        {
                return put_user (epoch, (unsigned long *)arg);
        }
        case RTC_EPOCH_SET:     /* Set the epoch.       */
        {
                /* 
                 * There were no RTC clocks before 1900.
                 */
                if (arg < 1900)
		  return -EINVAL;
		if (!capable(CAP_SYS_TIME))
		  return -EACCES;
		
                epoch = arg;
                return 0;
        }
        default:
                return -EINVAL;
        }
        return copy_to_user((void *)arg, &wtime, sizeof wtime) ? -EFAULT : 0;
#endif
}

static const struct file_operations hp_sdc_rtc_fops = {
        .owner =	THIS_MODULE,
        .llseek =	no_llseek,
        .read =		hp_sdc_rtc_read,
        .poll =		hp_sdc_rtc_poll,
        .ioctl =	hp_sdc_rtc_ioctl,
        .open =		hp_sdc_rtc_open,
        .release =	hp_sdc_rtc_release,
        .fasync =	hp_sdc_rtc_fasync,
};

static struct miscdevice hp_sdc_rtc_dev = {
        .minor =	RTC_MINOR,
        .name =		"rtc_HIL",
        .fops =		&hp_sdc_rtc_fops
};

static int __init hp_sdc_rtc_init(void)
{
	int ret;

#ifdef __mc68000__
	if (!MACH_IS_HP300)
		return -ENODEV;
#endif

	init_MUTEX(&i8042tregs);

	if ((ret = hp_sdc_request_timer_irq(&hp_sdc_rtc_isr)))
		return ret;
	if (misc_register(&hp_sdc_rtc_dev) != 0)
		printk(KERN_INFO "Could not register misc. dev for i8042 rtc\n");

        create_proc_read_entry ("driver/rtc", 0, NULL,
				hp_sdc_rtc_read_proc, NULL);

	printk(KERN_INFO "HP i8042 SDC + MSM-58321 RTC support loaded "
			 "(RTC v " RTC_VERSION ")\n");

	return 0;
}

static void __exit hp_sdc_rtc_exit(void)
{
	remove_proc_entry ("driver/rtc", NULL);
        misc_deregister(&hp_sdc_rtc_dev);
	hp_sdc_release_timer_irq(hp_sdc_rtc_isr);
        printk(KERN_INFO "HP i8042 SDC + MSM-58321 RTC support unloaded\n");
}

module_init(hp_sdc_rtc_init);
module_exit(hp_sdc_rtc_exit);
