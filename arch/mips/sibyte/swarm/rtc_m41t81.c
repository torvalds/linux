/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * Copyright (C) 2002 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
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


/* M41T81 definitions */

/*
 * Register bits
 */

#define M41T81REG_SC_ST		0x80		/* stop bit */
#define M41T81REG_HR_CB		0x40		/* century bit */
#define M41T81REG_HR_CEB	0x80		/* century enable bit */
#define M41T81REG_CTL_S		0x20		/* sign bit */
#define M41T81REG_CTL_FT	0x40		/* frequency test bit */
#define M41T81REG_CTL_OUT	0x80		/* output level */
#define M41T81REG_WD_RB0	0x01		/* watchdog resolution bit 0 */
#define M41T81REG_WD_RB1	0x02		/* watchdog resolution bit 1 */
#define M41T81REG_WD_BMB0	0x04		/* watchdog multiplier bit 0 */
#define M41T81REG_WD_BMB1	0x08		/* watchdog multiplier bit 1 */
#define M41T81REG_WD_BMB2	0x10		/* watchdog multiplier bit 2 */
#define M41T81REG_WD_BMB3	0x20		/* watchdog multiplier bit 3 */
#define M41T81REG_WD_BMB4	0x40		/* watchdog multiplier bit 4 */
#define M41T81REG_AMO_ABE	0x20		/* alarm in "battery back-up mode" enable bit */
#define M41T81REG_AMO_SQWE	0x40		/* square wave enable */
#define M41T81REG_AMO_AFE	0x80		/* alarm flag enable flag */
#define M41T81REG_ADT_RPT5	0x40		/* alarm repeat mode bit 5 */
#define M41T81REG_ADT_RPT4	0x80		/* alarm repeat mode bit 4 */
#define M41T81REG_AHR_RPT3	0x80		/* alarm repeat mode bit 3 */
#define M41T81REG_AHR_HT	0x40		/* halt update bit */
#define M41T81REG_AMN_RPT2	0x80		/* alarm repeat mode bit 2 */
#define M41T81REG_ASC_RPT1	0x80		/* alarm repeat mode bit 1 */
#define M41T81REG_FLG_AF	0x40		/* alarm flag (read only) */
#define M41T81REG_FLG_WDF	0x80		/* watchdog flag (read only) */
#define M41T81REG_SQW_RS0	0x10		/* sqw frequency bit 0 */
#define M41T81REG_SQW_RS1	0x20		/* sqw frequency bit 1 */
#define M41T81REG_SQW_RS2	0x40		/* sqw frequency bit 2 */
#define M41T81REG_SQW_RS3	0x80		/* sqw frequency bit 3 */


/*
 * Register numbers
 */

#define M41T81REG_TSC	0x00		/* tenths/hundredths of second */
#define M41T81REG_SC	0x01		/* seconds */
#define M41T81REG_MN	0x02		/* minute */
#define M41T81REG_HR	0x03		/* hour/century */
#define M41T81REG_DY	0x04		/* day of week */
#define M41T81REG_DT	0x05		/* date of month */
#define M41T81REG_MO	0x06		/* month */
#define M41T81REG_YR	0x07		/* year */
#define M41T81REG_CTL	0x08		/* control */
#define M41T81REG_WD	0x09		/* watchdog */
#define M41T81REG_AMO	0x0A		/* alarm: month */
#define M41T81REG_ADT	0x0B		/* alarm: date */
#define M41T81REG_AHR	0x0C		/* alarm: hour */
#define M41T81REG_AMN	0x0D		/* alarm: minute */
#define M41T81REG_ASC	0x0E		/* alarm: second */
#define M41T81REG_FLG	0x0F		/* flags */
#define M41T81REG_SQW	0x13		/* square wave register */

#define M41T81_CCR_ADDRESS	0x68
#define SMB_CSR(reg) ((u8 *) (IOADDR(A_SMB_REGISTER(1, reg))))

static int m41t81_read(uint8_t addr)
{
	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	bus_writeq(addr & 0xff, SMB_CSR(R_SMB_CMD));
	bus_writeq((V_SMB_ADDR(M41T81_CCR_ADDRESS) | V_SMB_TT_WR1BYTE),
		   SMB_CSR(R_SMB_START));

	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	bus_writeq((V_SMB_ADDR(M41T81_CCR_ADDRESS) | V_SMB_TT_RD1BYTE),
		   SMB_CSR(R_SMB_START));

	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	if (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_ERROR) {
		/* Clear error bit by writing a 1 */
		bus_writeq(M_SMB_ERROR, SMB_CSR(R_SMB_STATUS));
		return -1;
	}

	return (bus_readq(SMB_CSR(R_SMB_DATA)) & 0xff);
}

static int m41t81_write(uint8_t addr, int b)
{
	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	bus_writeq((addr & 0xFF), SMB_CSR(R_SMB_CMD));
	bus_writeq((b & 0xff), SMB_CSR(R_SMB_DATA));
	bus_writeq(V_SMB_ADDR(M41T81_CCR_ADDRESS) | V_SMB_TT_WR2BYTE,
		   SMB_CSR(R_SMB_START));

	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	if (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_ERROR) {
		/* Clear error bit by writing a 1 */
		bus_writeq(M_SMB_ERROR, SMB_CSR(R_SMB_STATUS));
		return -1;
	}

	/* read the same byte again to make sure it is written */
	bus_writeq(V_SMB_ADDR(M41T81_CCR_ADDRESS) | V_SMB_TT_RD1BYTE,
		   SMB_CSR(R_SMB_START));

	while (bus_readq(SMB_CSR(R_SMB_STATUS)) & M_SMB_BUSY)
		;

	return 0;
}

int m41t81_set_time(unsigned long t)
{
	struct rtc_time tm;

	to_tm(t, &tm);

	/*
	 * Note the write order matters as it ensures the correctness.
	 * When we write sec, 10th sec is clear.  It is reasonable to
	 * believe we should finish writing min within a second.
	 */

	tm.tm_sec = BIN2BCD(tm.tm_sec);
	m41t81_write(M41T81REG_SC, tm.tm_sec);

	tm.tm_min = BIN2BCD(tm.tm_min);
	m41t81_write(M41T81REG_MN, tm.tm_min);

	tm.tm_hour = BIN2BCD(tm.tm_hour);
	tm.tm_hour = (tm.tm_hour & 0x3f) | (m41t81_read(M41T81REG_HR) & 0xc0);
	m41t81_write(M41T81REG_HR, tm.tm_hour);

	/* tm_wday starts from 0 to 6 */
	if (tm.tm_wday == 0) tm.tm_wday = 7;
	tm.tm_wday = BIN2BCD(tm.tm_wday);
	m41t81_write(M41T81REG_DY, tm.tm_wday);

	tm.tm_mday = BIN2BCD(tm.tm_mday);
	m41t81_write(M41T81REG_DT, tm.tm_mday);

	/* tm_mon starts from 0, *ick* */
	tm.tm_mon ++;
	tm.tm_mon = BIN2BCD(tm.tm_mon);
	m41t81_write(M41T81REG_MO, tm.tm_mon);

	/* we don't do century, everything is beyond 2000 */
	tm.tm_year %= 100;
	tm.tm_year = BIN2BCD(tm.tm_year);
	m41t81_write(M41T81REG_YR, tm.tm_year);

	return 0;
}

unsigned long m41t81_get_time(void)
{
	unsigned int year, mon, day, hour, min, sec;

	/*
	 * min is valid if two reads of sec are the same.
	 */
	for (;;) {
		sec = m41t81_read(M41T81REG_SC);
		min = m41t81_read(M41T81REG_MN);
		if (sec == m41t81_read(M41T81REG_SC)) break;
	}
	hour = m41t81_read(M41T81REG_HR) & 0x3f;
	day = m41t81_read(M41T81REG_DT);
	mon = m41t81_read(M41T81REG_MO);
	year = m41t81_read(M41T81REG_YR);

	sec = BCD2BIN(sec);
	min = BCD2BIN(min);
	hour = BCD2BIN(hour);
	day = BCD2BIN(day);
	mon = BCD2BIN(mon);
	year = BCD2BIN(year);

	year += 2000;

	return mktime(year, mon, day, hour, min, sec);
}

int m41t81_probe(void)
{
	unsigned int tmp;

	/* enable chip if it is not enabled yet */
	tmp = m41t81_read(M41T81REG_SC);
	m41t81_write(M41T81REG_SC, tmp & 0x7f);

	return (m41t81_read(M41T81REG_SC) != -1);
}
