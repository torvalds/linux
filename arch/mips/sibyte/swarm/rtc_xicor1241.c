/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * Copyright (C) 2002 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/bcd.h>
#include <linux/types.h>
#include <linux/time.h>

#include <asm/time.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_smbus.h>


/* Xicor 1241 definitions */

/*
 * Register bits
 */

#define X1241REG_SR_BAT	0x80		/* currently on battery power */
#define X1241REG_SR_RWEL 0x04		/* r/w latch is enabled, can write RTC */
#define X1241REG_SR_WEL 0x02		/* r/w latch is unlocked, can enable r/w now */
#define X1241REG_SR_RTCF 0x01		/* clock failed */
#define X1241REG_BL_BP2 0x80		/* block protect 2 */
#define X1241REG_BL_BP1 0x40		/* block protect 1 */
#define X1241REG_BL_BP0 0x20		/* block protect 0 */
#define X1241REG_BL_WD1	0x10
#define X1241REG_BL_WD0	0x08
#define X1241REG_HR_MIL 0x80		/* military time format */

/*
 * Register numbers
 */

#define X1241REG_BL	0x10		/* block protect bits */
#define X1241REG_INT	0x11		/*  */
#define X1241REG_SC	0x30		/* Seconds */
#define X1241REG_MN	0x31		/* Minutes */
#define X1241REG_HR	0x32		/* Hours */
#define X1241REG_DT	0x33		/* Day of month */
#define X1241REG_MO	0x34		/* Month */
#define X1241REG_YR	0x35		/* Year */
#define X1241REG_DW	0x36		/* Day of Week */
#define X1241REG_Y2K	0x37		/* Year 2K */
#define X1241REG_SR	0x3F		/* Status register */

#define X1241_CCR_ADDRESS	0x6F

#define SMB_CSR(reg)	IOADDR(A_SMB_REGISTER(1, reg))

static int xicor_read(uint8_t addr)
{
        while (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
                ;

	__raw_writeq((addr >> 8) & 0x7, SMB_CSR(R_SMB_CMD));
	__raw_writeq(addr & 0xff, SMB_CSR(R_SMB_DATA));
	__raw_writeq(V_SMB_ADDR(X1241_CCR_ADDRESS) | V_SMB_TT_WR2BYTE,
		     SMB_CSR(R_SMB_START));

        while (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
                ;

	__raw_writeq(V_SMB_ADDR(X1241_CCR_ADDRESS) | V_SMB_TT_RD1BYTE,
		     SMB_CSR(R_SMB_START));

        while (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
                ;

        if (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_ERROR) {
                /* Clear error bit by writing a 1 */
                __raw_writeq(M_SMB_ERROR, SMB_CSR(R_SMB_STATUS));
                return -1;
        }

	return (__raw_readq(SMB_CSR(R_SMB_DATA)) & 0xff);
}

static int xicor_write(uint8_t addr, int b)
{
        while (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
                ;

	__raw_writeq(addr, SMB_CSR(R_SMB_CMD));
	__raw_writeq((addr & 0xff) | ((b & 0xff) << 8), SMB_CSR(R_SMB_DATA));
	__raw_writeq(V_SMB_ADDR(X1241_CCR_ADDRESS) | V_SMB_TT_WR3BYTE,
		     SMB_CSR(R_SMB_START));

        while (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
                ;

        if (__raw_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_ERROR) {
                /* Clear error bit by writing a 1 */
                __raw_writeq(M_SMB_ERROR, SMB_CSR(R_SMB_STATUS));
                return -1;
        } else {
		return 0;
	}
}

int xicor_set_time(unsigned long t)
{
	struct rtc_time tm;
	int tmp;
	unsigned long flags;

	rtc_time_to_tm(t, &tm);
	tm.tm_year += 1900;

	spin_lock_irqsave(&rtc_lock, flags);
	/* unlock writes to the CCR */
	xicor_write(X1241REG_SR, X1241REG_SR_WEL);
	xicor_write(X1241REG_SR, X1241REG_SR_WEL | X1241REG_SR_RWEL);

	/* trivial ones */
	tm.tm_sec = BIN2BCD(tm.tm_sec);
	xicor_write(X1241REG_SC, tm.tm_sec);

	tm.tm_min = BIN2BCD(tm.tm_min);
	xicor_write(X1241REG_MN, tm.tm_min);

	tm.tm_mday = BIN2BCD(tm.tm_mday);
	xicor_write(X1241REG_DT, tm.tm_mday);

	/* tm_mon starts from 0, *ick* */
	tm.tm_mon ++;
	tm.tm_mon = BIN2BCD(tm.tm_mon);
	xicor_write(X1241REG_MO, tm.tm_mon);

	/* year is split */
	tmp = tm.tm_year / 100;
	tm.tm_year %= 100;
	xicor_write(X1241REG_YR, tm.tm_year);
	xicor_write(X1241REG_Y2K, tmp);

	/* hour is the most tricky one */
	tmp = xicor_read(X1241REG_HR);
	if (tmp & X1241REG_HR_MIL) {
		/* 24 hour format */
		tm.tm_hour = BIN2BCD(tm.tm_hour);
		tmp = (tmp & ~0x3f) | (tm.tm_hour & 0x3f);
	} else {
		/* 12 hour format, with 0x2 for pm */
		tmp = tmp & ~0x3f;
		if (tm.tm_hour >= 12) {
			tmp |= 0x20;
			tm.tm_hour -= 12;
		}
		tm.tm_hour = BIN2BCD(tm.tm_hour);
		tmp |= tm.tm_hour;
	}
	xicor_write(X1241REG_HR, tmp);

	xicor_write(X1241REG_SR, 0);
	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}

unsigned long xicor_get_time(void)
{
	unsigned int year, mon, day, hour, min, sec, y2k;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	sec = xicor_read(X1241REG_SC);
	min = xicor_read(X1241REG_MN);
	hour = xicor_read(X1241REG_HR);

	if (hour & X1241REG_HR_MIL) {
		hour &= 0x3f;
	} else {
		if (hour & 0x20)
			hour = (hour & 0xf) + 0x12;
	}

	day = xicor_read(X1241REG_DT);
	mon = xicor_read(X1241REG_MO);
	year = xicor_read(X1241REG_YR);
	y2k = xicor_read(X1241REG_Y2K);
	spin_unlock_irqrestore(&rtc_lock, flags);

	sec = BCD2BIN(sec);
	min = BCD2BIN(min);
	hour = BCD2BIN(hour);
	day = BCD2BIN(day);
	mon = BCD2BIN(mon);
	year = BCD2BIN(year);
	y2k = BCD2BIN(y2k);

	year += (y2k * 100);

	return mktime(year, mon, day, hour, min, sec);
}

int xicor_probe(void)
{
	return (xicor_read(X1241REG_SC) != -1);
}
