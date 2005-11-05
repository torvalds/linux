/*
 * RTC routines for RICOH Rx5C348 SPI chip.
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <asm/time.h>
#include <asm/tx4938/spi.h>

#define	EPOCH		2000

/* registers */
#define Rx5C348_REG_SECOND	0
#define Rx5C348_REG_MINUTE	1
#define Rx5C348_REG_HOUR	2
#define Rx5C348_REG_WEEK	3
#define Rx5C348_REG_DAY	4
#define Rx5C348_REG_MONTH	5
#define Rx5C348_REG_YEAR	6
#define Rx5C348_REG_ADJUST	7
#define Rx5C348_REG_ALARM_W_MIN	8
#define Rx5C348_REG_ALARM_W_HOUR	9
#define Rx5C348_REG_ALARM_W_WEEK	10
#define Rx5C348_REG_ALARM_D_MIN	11
#define Rx5C348_REG_ALARM_D_HOUR	12
#define Rx5C348_REG_CTL1	14
#define Rx5C348_REG_CTL2	15

/* register bits */
#define Rx5C348_BIT_PM	0x20	/* REG_HOUR */
#define Rx5C348_BIT_Y2K	0x80	/* REG_MONTH */
#define Rx5C348_BIT_24H	0x20	/* REG_CTL1 */
#define Rx5C348_BIT_XSTP	0x10	/* REG_CTL2 */

/* commands */
#define Rx5C348_CMD_W(addr)	(((addr) << 4) | 0x08)	/* single write */
#define Rx5C348_CMD_R(addr)	(((addr) << 4) | 0x0c)	/* single read */
#define Rx5C348_CMD_MW(addr)	(((addr) << 4) | 0x00)	/* burst write */
#define Rx5C348_CMD_MR(addr)	(((addr) << 4) | 0x04)	/* burst read */

static struct spi_dev_desc srtc_dev_desc = {
	.baud 		= 1000000,	/* 1.0Mbps @ Vdd 2.0V */
	.tcss		= 31,
	.tcsh		= 1,
	.tcsr		= 62,
	/* 31us for Tcss (62us for Tcsr) is required for carry operation) */
	.byteorder	= 1,		/* MSB-First */
	.polarity	= 0,		/* High-Active */
	.phase		= 1,		/* Shift-Then-Sample */

};
static int srtc_chipid;
static int srtc_24h;

static inline int
spi_rtc_io(unsigned char *inbuf, unsigned char *outbuf, unsigned int count)
{
	unsigned char *inbufs[1], *outbufs[1];
	unsigned int incounts[2], outcounts[2];
	inbufs[0] = inbuf;
	incounts[0] = count;
	incounts[1] = 0;
	outbufs[0] = outbuf;
	outcounts[0] = count;
	outcounts[1] = 0;
	return txx9_spi_io(srtc_chipid, &srtc_dev_desc,
			   inbufs, incounts, outbufs, outcounts, 0);
}

/*
 * Conversion between binary and BCD.
 */
#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

/* RTC-dependent code for time.c */

static int
rtc_rx5c348_set_time(unsigned long t)
{
	unsigned char inbuf[8];
	struct rtc_time tm;
	u8 year, month, day, hour, minute, second, century;

	/* convert */
	to_tm(t, &tm);

	year = tm.tm_year % 100;
	month = tm.tm_mon+1;	/* tm_mon starts from 0 to 11 */
	day = tm.tm_mday;
	hour = tm.tm_hour;
	minute = tm.tm_min;
	second = tm.tm_sec;
	century = tm.tm_year / 100;

	inbuf[0] = Rx5C348_CMD_MW(Rx5C348_REG_SECOND);
	BIN_TO_BCD(second);
	inbuf[1] = second;
	BIN_TO_BCD(minute);
	inbuf[2] = minute;

	if (srtc_24h) {
		BIN_TO_BCD(hour);
		inbuf[3] = hour;
	} else {
		/* hour 0 is AM12, noon is PM12 */
		inbuf[3] = 0;
		if (hour >= 12)
			inbuf[3] = Rx5C348_BIT_PM;
		hour = (hour + 11) % 12 + 1;
		BIN_TO_BCD(hour);
		inbuf[3] |= hour;
	}
	inbuf[4] = 0;	/* ignore week */
	BIN_TO_BCD(day);
	inbuf[5] = day;
	BIN_TO_BCD(month);
	inbuf[6] = month;
	if (century >= 20)
		inbuf[6] |= Rx5C348_BIT_Y2K;
	BIN_TO_BCD(year);
	inbuf[7] = year;
	/* write in one transfer to avoid data inconsistency */
	return spi_rtc_io(inbuf, NULL, 8);
}

static unsigned long
rtc_rx5c348_get_time(void)
{
	unsigned char inbuf[8], outbuf[8];
	unsigned int year, month, day, hour, minute, second;

	inbuf[0] = Rx5C348_CMD_MR(Rx5C348_REG_SECOND);
	memset(inbuf + 1, 0, 7);
	/* read in one transfer to avoid data inconsistency */
	if (spi_rtc_io(inbuf, outbuf, 8))
		return 0;
	second = outbuf[1];
	BCD_TO_BIN(second);
	minute = outbuf[2];
	BCD_TO_BIN(minute);
	if (srtc_24h) {
		hour = outbuf[3];
		BCD_TO_BIN(hour);
	} else {
		hour = outbuf[3] & ~Rx5C348_BIT_PM;
		BCD_TO_BIN(hour);
		hour %= 12;
		if (outbuf[3] & Rx5C348_BIT_PM)
			hour += 12;
	}
	day = outbuf[5];
	BCD_TO_BIN(day);
	month = outbuf[6] & ~Rx5C348_BIT_Y2K;
	BCD_TO_BIN(month);
	year = outbuf[7];
	BCD_TO_BIN(year);
	year += EPOCH;

	return mktime(year, month, day, hour, minute, second);
}

void __init
rtc_rx5c348_init(int chipid)
{
	unsigned char inbuf[2], outbuf[2];
	srtc_chipid = chipid;
	/* turn on RTC if it is not on */
	inbuf[0] = Rx5C348_CMD_R(Rx5C348_REG_CTL2);
	inbuf[1] = 0;
	spi_rtc_io(inbuf, outbuf, 2);
	if (outbuf[1] & Rx5C348_BIT_XSTP) {
		inbuf[0] = Rx5C348_CMD_W(Rx5C348_REG_CTL2);
		inbuf[1] = 0;
		spi_rtc_io(inbuf, NULL, 2);
	}

	inbuf[0] = Rx5C348_CMD_R(Rx5C348_REG_CTL1);
	inbuf[1] = 0;
	spi_rtc_io(inbuf, outbuf, 2);
	if (outbuf[1] & Rx5C348_BIT_24H)
		srtc_24h = 1;

	/* set the function pointers */
	rtc_get_time = rtc_rx5c348_get_time;
	rtc_set_time = rtc_rx5c348_set_time;
}
