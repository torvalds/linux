/*
 * arch/sh/boards/dreamcast/rtc.c
 *
 * Dreamcast AICA RTC routines.
 *
 * Copyright (c) 2001, 2002 M. R. Brown <mrbrown@0xd6.org>
 * Copyright (c) 2002 Paul Mundt <lethal@chaoticdreams.org>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */

#include <linux/time.h>
#include <asm/rtc.h>
#include <asm/io.h>

/* The AICA RTC has an Epoch of 1/1/1950, so we must subtract 20 years (in
   seconds) to get the standard Unix Epoch when getting the time, and add
   20 years when setting the time. */
#define TWENTY_YEARS ((20 * 365LU + 5) * 86400)

/* The AICA RTC is represented by a 32-bit seconds counter stored in 2 16-bit
   registers.*/
#define AICA_RTC_SECS_H		0xa0710000
#define AICA_RTC_SECS_L		0xa0710004

/**
 * aica_rtc_gettimeofday - Get the time from the AICA RTC
 * @ts: pointer to resulting timespec
 *
 * Grabs the current RTC seconds counter and adjusts it to the Unix Epoch.
 */
static void aica_rtc_gettimeofday(struct timespec *ts)
{
	unsigned long val1, val2;

	do {
		val1 = ((ctrl_inl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(ctrl_inl(AICA_RTC_SECS_L) & 0xffff);

		val2 = ((ctrl_inl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(ctrl_inl(AICA_RTC_SECS_L) & 0xffff);
	} while (val1 != val2);

	ts->tv_sec = val1 - TWENTY_YEARS;

	/* Can't get nanoseconds with just a seconds counter. */
	ts->tv_nsec = 0;
}

/**
 * aica_rtc_settimeofday - Set the AICA RTC to the current time
 * @secs: contains the time_t to set
 *
 * Adjusts the given @tv to the AICA Epoch and sets the RTC seconds counter.
 */
static int aica_rtc_settimeofday(const time_t secs)
{
	unsigned long val1, val2;
	unsigned long adj = secs + TWENTY_YEARS;

	do {
		ctrl_outl((adj & 0xffff0000) >> 16, AICA_RTC_SECS_H);
		ctrl_outl((adj & 0xffff), AICA_RTC_SECS_L);

		val1 = ((ctrl_inl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(ctrl_inl(AICA_RTC_SECS_L) & 0xffff);

		val2 = ((ctrl_inl(AICA_RTC_SECS_H) & 0xffff) << 16) |
			(ctrl_inl(AICA_RTC_SECS_L) & 0xffff);
	} while (val1 != val2);

	return 0;
}

void aica_time_init(void)
{
	rtc_sh_get_time = aica_rtc_gettimeofday;
	rtc_sh_set_time = aica_rtc_settimeofday;
}

