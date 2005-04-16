/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * arch/mips/ddb5xxx/common/rtc_ds1386.c
 *     low-level RTC hookups for s for Dallas 1396 chip.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */


/*
 * This file exports a function, rtc_ds1386_init(), which expects an
 * uncached base address as the argument.  It will set the two function
 * pointers expected by the MIPS generic timer code.
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/bcd.h>

#include <asm/time.h>
#include <asm/addrspace.h>

#include <asm/mc146818rtc.h>
#include <asm/debug.h>

#define	EPOCH		2000

#define	READ_RTC(x)	*(volatile unsigned char*)(rtc_base+x)
#define	WRITE_RTC(x, y)	*(volatile unsigned char*)(rtc_base+x) = y

static unsigned long rtc_base;

static unsigned long
rtc_ds1386_get_time(void)
{
	u8 byte;
	u8 temp;
	unsigned int year, month, day, hour, minute, second;

	/* let us freeze external registers */
	byte = READ_RTC(0xB);
	byte &= 0x3f;
	WRITE_RTC(0xB, byte);

	/* read time data */
	year = BCD2BIN(READ_RTC(0xA)) + EPOCH;
	month = BCD2BIN(READ_RTC(0x9) & 0x1f);
	day = BCD2BIN(READ_RTC(0x8));
	minute = BCD2BIN(READ_RTC(0x2));
	second = BCD2BIN(READ_RTC(0x1));

	/* hour is special - deal with it later */
	temp = READ_RTC(0x4);

	/* enable time transfer */
	byte |= 0x80;
	WRITE_RTC(0xB, byte);

	/* calc hour */
	if (temp & 0x40) {
		/* 12 hour format */
		hour = BCD2BIN(temp & 0x1f);
		if (temp & 0x20) hour += 12; 		/* PM */
	} else {
		/* 24 hour format */
		hour = BCD2BIN(temp & 0x3f);
	}

	return mktime(year, month, day, hour, minute, second);
}

static int
rtc_ds1386_set_time(unsigned long t)
{
	struct rtc_time tm;
	u8 byte;
	u8 temp;
	u8 year, month, day, hour, minute, second;

	/* let us freeze external registers */
	byte = READ_RTC(0xB);
	byte &= 0x3f;
	WRITE_RTC(0xB, byte);

	/* convert */
	to_tm(t, &tm);


	/* check each field one by one */
	year = BIN2BCD(tm.tm_year - EPOCH);
	if (year != READ_RTC(0xA)) {
		WRITE_RTC(0xA, year);
	}

	temp = READ_RTC(0x9);
	month = BIN2BCD(tm.tm_mon+1);	/* tm_mon starts from 0 to 11 */
	if (month != (temp & 0x1f)) {
		WRITE_RTC( 0x9,
			   (month & 0x1f) | (temp & ~0x1f) );
	}

	day = BIN2BCD(tm.tm_mday);
	if (day != READ_RTC(0x8)) {
		WRITE_RTC(0x8, day);
	}

	temp = READ_RTC(0x4);
	if (temp & 0x40) {
		/* 12 hour format */
		hour = 0x40;
		if (tm.tm_hour > 12) {
			hour |= 0x20 | (BIN2BCD(hour-12) & 0x1f);
		} else {
			hour |= BIN2BCD(tm.tm_hour);
		}
	} else {
		/* 24 hour format */
		hour = BIN2BCD(tm.tm_hour) & 0x3f;
	}
	if (hour != temp) WRITE_RTC(0x4, hour);

	minute = BIN2BCD(tm.tm_min);
	if (minute != READ_RTC(0x2)) {
		WRITE_RTC(0x2, minute);
	}

	second = BIN2BCD(tm.tm_sec);
	if (second != READ_RTC(0x1)) {
		WRITE_RTC(0x1, second);
	}

	return 0;
}

void
rtc_ds1386_init(unsigned long base)
{
	unsigned char byte;

	/* remember the base */
	rtc_base = base;
	db_assert((rtc_base & 0xe0000000) == KSEG1);

	/* turn on RTC if it is not on */
	byte = READ_RTC(0x9);
	if (byte & 0x80) {
		byte &= 0x7f;
		WRITE_RTC(0x9, byte);
	}

	/* enable time transfer */
	byte = READ_RTC(0xB);
	byte |= 0x80;
	WRITE_RTC(0xB, byte);

	/* set the function pointers */
	rtc_get_time = rtc_ds1386_get_time;
	rtc_set_time = rtc_ds1386_set_time;
}
