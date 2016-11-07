/* MN10300 RTC management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/rtc-regs.h>
#include <asm/rtc.h>

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

static const __initdata struct resource res[] = {
	DEFINE_RES_IO(RTC_PORT(0), RTC_IO_EXTENT),
	DEFINE_RES_IRQ(RTC_IRQ),
};

/*
 * calibrate the TSC clock against the RTC
 */
void __init calibrate_clock(void)
{
	unsigned char status;

	/* make sure the RTC is running and is set to operate in 24hr mode */
	status = RTSRC;
	RTCRB |= RTCRB_SET;
	RTCRB |= RTCRB_TM_24HR;
	RTCRB &= ~RTCRB_DM_BINARY;
	RTCRA |= RTCRA_DVR;
	RTCRA &= ~RTCRA_DVR;
	RTCRB &= ~RTCRB_SET;

	platform_device_register_simple("rtc_cmos", -1, res, ARRAY_SIZE(res));
}
