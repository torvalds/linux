// SPDX-License-Identifier: GPL-2.0
/*
 *  arch/arm/mach-footbridge/isa-rtc.c
 *
 *  Copyright (C) 1998 Russell King.
 *  Copyright (C) 1998 Phil Blundell
 *
 * CATS has a real-time clock, though the evaluation board doesn't.
 *
 * Changelog:
 *  21-Mar-1998	RMK	Created
 *  27-Aug-1998	PJB	CATS support
 *  28-Dec-1998	APH	Made leds optional
 *  20-Jan-1999	RMK	Started merge of EBSA285, CATS and NetWinder
 *  16-Mar-1999	RMK	More support for EBSA285-like machines with RTCs in
 */

#define RTC_PORT(x)		(0x70+(x))
#define RTC_ALWAYS_BCD		0

#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/bcd.h>
#include <linux/io.h>

#include "common.h"

void __init isa_rtc_init(void)
{
	int reg_d, reg_b;

	/*
	 * Probe for the RTC.
	 */
	reg_d = CMOS_READ(RTC_REG_D);

	/*
	 * make sure the divider is set
	 */
	CMOS_WRITE(RTC_REF_CLCK_32KHZ, RTC_REG_A);

	/*
	 * Set control reg B
	 *   (24 hour mode, update enabled)
	 */
	reg_b = CMOS_READ(RTC_REG_B) & 0x7f;
	reg_b |= 2;
	CMOS_WRITE(reg_b, RTC_REG_B);

	if ((CMOS_READ(RTC_REG_A) & 0x7f) == RTC_REF_CLCK_32KHZ &&
	    CMOS_READ(RTC_REG_B) == reg_b) {
		/*
		 * We have a RTC.  Check the battery
		 */
		if ((reg_d & 0x80) == 0)
			printk(KERN_WARNING "RTC: *** warning: CMOS battery bad\n");
	}
}
