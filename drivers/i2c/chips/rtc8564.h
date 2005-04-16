/*
 *  linux/drivers/i2c/chips/rtc8564.h
 *
 *  Copyright (C) 2002-2004 Stefan Eletzhofer
 *
 *	based on linux/drivers/acron/char/pcf8583.h
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
struct rtc_tm {
	unsigned char	secs;
	unsigned char	mins;
	unsigned char	hours;
	unsigned char	mday;
	unsigned char	mon;
	unsigned short	year; /* xxxx 4 digits :) */
	unsigned char	wday;
	unsigned char	vl;
};

struct mem {
	unsigned int	loc;
	unsigned int	nr;
	unsigned char	*data;
};

#define RTC_GETDATETIME	0
#define RTC_SETTIME	1
#define RTC_SETDATETIME	2
#define RTC_GETCTRL	3
#define RTC_SETCTRL	4
#define MEM_READ	5
#define MEM_WRITE	6

#define RTC8564_REG_CTRL1		0x0 /* T  0 S 0 | T 0 0 0 */
#define RTC8564_REG_CTRL2		0x1 /* 0  0 0 TI/TP | AF TF AIE TIE */
#define RTC8564_REG_SEC			0x2 /* VL 4 2 1 | 8 4 2 1 */
#define RTC8564_REG_MIN			0x3 /* x  4 2 1 | 8 4 2 1 */
#define RTC8564_REG_HR			0x4 /* x  x 2 1 | 8 4 2 1 */
#define RTC8564_REG_DAY			0x5 /* x  x 2 1 | 8 4 2 1 */
#define RTC8564_REG_WDAY		0x6 /* x  x x x | x 4 2 1 */
#define RTC8564_REG_MON_CENT	0x7 /* C  x x 1 | 8 4 2 1 */
#define RTC8564_REG_YEAR		0x8 /* 8  4 2 1 | 8 4 2 1 */
#define RTC8564_REG_AL_MIN		0x9 /* AE 4 2 1 | 8 4 2 1 */
#define RTC8564_REG_AL_HR		0xa /* AE 4 2 1 | 8 4 2 1 */
#define RTC8564_REG_AL_DAY		0xb /* AE x 2 1 | 8 4 2 1 */
#define RTC8564_REG_AL_WDAY		0xc /* AE x x x | x 4 2 1 */
#define RTC8564_REG_CLKOUT		0xd /* FE x x x | x x FD1 FD0 */
#define RTC8564_REG_TCTL		0xe /* TE x x x | x x FD1 FD0 */
#define RTC8564_REG_TIMER		0xf /* 8 bit binary */

/* Control reg */
#define RTC8564_CTRL1_TEST1		(1<<3)
#define RTC8564_CTRL1_STOP		(1<<5)
#define RTC8564_CTRL1_TEST2		(1<<7)

#define RTC8564_CTRL2_TIE		(1<<0)
#define RTC8564_CTRL2_AIE		(1<<1)
#define RTC8564_CTRL2_TF		(1<<2)
#define RTC8564_CTRL2_AF		(1<<3)
#define RTC8564_CTRL2_TI_TP		(1<<4)

/* CLKOUT frequencies */
#define RTC8564_FD_32768HZ		(0x0)
#define RTC8564_FD_1024HZ		(0x1)
#define RTC8564_FD_32			(0x2)
#define RTC8564_FD_1HZ			(0x3)

/* Timer CTRL */
#define RTC8564_TD_4096HZ		(0x0)
#define RTC8564_TD_64HZ			(0x1)
#define RTC8564_TD_1HZ			(0x2)
#define RTC8564_TD_1_60HZ		(0x3)

#define I2C_DRIVERID_RTC8564 0xf000
