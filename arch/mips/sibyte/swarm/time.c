/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * Time routines for the swarm board.  We pass all the hard stuff
 * through to the sb1250 handling code.  Only thing we really keep
 * track of here is what time of day we think it is.  And we don't
 * really even do a good job of that...
 */


#include <linux/bcd.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <asm/system.h>
#include <asm/addrspace.h>
#include <asm/io.h>

#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_smbus.h>

static unsigned long long sec_bias = 0;
static unsigned int usec_bias = 0;

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

#define SMB_CSR(reg) (IOADDR(A_SMB_REGISTER(1, reg)))

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

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 *
 * BUG: This routine does not handle hour overflow properly; it just
 *      sets the minutes. Usually you'll only notice that after reboot!
 */
int set_rtc_mmss(unsigned long nowtime)
{
	int retval = 0;
	int real_seconds, real_minutes, cmos_minutes;

	cmos_minutes = xicor_read(X1241REG_MN);
	cmos_minutes = BCD2BIN(cmos_minutes);

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - cmos_minutes) + 15)/30) & 1)
		real_minutes += 30;		/* correct for half hour time zone */
	real_minutes %= 60;

	/* unlock writes to the CCR */
	xicor_write(X1241REG_SR, X1241REG_SR_WEL);
	xicor_write(X1241REG_SR, X1241REG_SR_WEL | X1241REG_SR_RWEL);

	if (abs(real_minutes - cmos_minutes) < 30) {
		real_seconds = BIN2BCD(real_seconds);
		real_minutes = BIN2BCD(real_minutes);
		xicor_write(X1241REG_SC, real_seconds);
		xicor_write(X1241REG_MN, real_minutes);
	} else {
		printk(KERN_WARNING
		       "set_rtc_mmss: can't update from %d to %d\n",
		       cmos_minutes, real_minutes);
		retval = -1;
	}

	xicor_write(X1241REG_SR, 0);

	printk("set_rtc_mmss: %02d:%02d\n", real_minutes, real_seconds);

	return retval;
}

static unsigned long __init get_swarm_time(void)
{
	unsigned int year, mon, day, hour, min, sec, y2k;

	sec = xicor_read(X1241REG_SC);
	min = xicor_read(X1241REG_MN);
	hour = xicor_read(X1241REG_HR);

	if (hour & X1241REG_HR_MIL) {
		hour &= 0x3f;
	} else {
		if (hour & 0x20)
			hour = (hour & 0xf) + 0x12;
	}

	sec = BCD2BIN(sec);
	min = BCD2BIN(min);
	hour = BCD2BIN(hour);

	day = xicor_read(X1241REG_DT);
	mon = xicor_read(X1241REG_MO);
	year = xicor_read(X1241REG_YR);
	y2k = xicor_read(X1241REG_Y2K);

	day = BCD2BIN(day);
	mon = BCD2BIN(mon);
	year = BCD2BIN(year);
	y2k = BCD2BIN(y2k);

	year += (y2k * 100);

	return mktime(year, mon, day, hour, min, sec);
}

/*
 *  Bring up the timer at 100 Hz.
 */
void __init swarm_time_init(void)
{
	unsigned int flags;
	int status;

	/* Set up the scd general purpose timer 0 to cpu 0 */
	sb1250_time_init();

	/* Establish communication with the Xicor 1241 RTC */
	/* XXXKW how do I share the SMBus with the I2C subsystem? */

	__raw_writeq(K_SMB_FREQ_400KHZ, SMB_CSR(R_SMB_FREQ));
	__raw_writeq(0, SMB_CSR(R_SMB_CONTROL));

	if ((status = xicor_read(X1241REG_SR_RTCF)) < 0) {
		printk("x1241: couldn't detect on SWARM SMBus 1\n");
	} else {
		if (status & X1241REG_SR_RTCF)
			printk("x1241: battery failed -- time is probably wrong\n");
		write_seqlock_irqsave(&xtime_lock, flags);
		xtime.tv_sec = get_swarm_time();
		xtime.tv_nsec = 0;
		write_sequnlock_irqrestore(&xtime_lock, flags);
	}
}
