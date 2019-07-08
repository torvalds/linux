/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    lm75.h - Part of lm_sensors, Linux kernel modules for hardware
	      monitoring
    Copyright (c) 2003 Mark M. Hoffman <mhoffman@lightlink.com>

*/

/*
    This file contains common code for encoding/decoding LM75 type
    temperature readings, which are emulated by many of the chips
    we support.  As the user is unlikely to load more than one driver
    which contains this code, we don't worry about the wasted space.
*/

#include <linux/kernel.h>

/* straight from the datasheet */
#define LM75_TEMP_MIN (-55000)
#define LM75_TEMP_MAX 125000
#define LM75_SHUTDOWN 0x01

/* TEMP: 0.001C/bit (-55C to +125C)
   REG: (0.5C/bit, two's complement) << 7 */
static inline u16 LM75_TEMP_TO_REG(long temp)
{
	int ntemp = clamp_val(temp, LM75_TEMP_MIN, LM75_TEMP_MAX);
	ntemp += (ntemp < 0 ? -250 : 250);
	return (u16)((ntemp / 500) << 7);
}

static inline int LM75_TEMP_FROM_REG(u16 reg)
{
	/* use integer division instead of equivalent right shift to
	   guarantee arithmetic shift and preserve the sign */
	return ((s16)reg / 128) * 500;
}
