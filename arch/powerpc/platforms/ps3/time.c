/*
 *  PS3 time and rtc routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>

#include <asm/rtc.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>

#include "platform.h"

#define dump_tm(_a) _dump_tm(_a, __func__, __LINE__)
static void _dump_tm(const struct rtc_time *tm, const char* func, int line)
{
	pr_debug("%s:%d tm_sec  %d\n", func, line, tm->tm_sec);
	pr_debug("%s:%d tm_min  %d\n", func, line, tm->tm_min);
	pr_debug("%s:%d tm_hour %d\n", func, line, tm->tm_hour);
	pr_debug("%s:%d tm_mday %d\n", func, line, tm->tm_mday);
	pr_debug("%s:%d tm_mon  %d\n", func, line, tm->tm_mon);
	pr_debug("%s:%d tm_year %d\n", func, line, tm->tm_year);
	pr_debug("%s:%d tm_wday %d\n", func, line, tm->tm_wday);
}

#define dump_time(_a) _dump_time(_a, __func__, __LINE__)
static void __maybe_unused _dump_time(int time, const char *func,
	int line)
{
	struct rtc_time tm;

	to_tm(time, &tm);

	pr_debug("%s:%d time    %d\n", func, line, time);
	_dump_tm(&tm, func, line);
}

/**
 * rtc_shift - Difference in seconds between 1970 and the ps3 rtc value.
 */

static s64 rtc_shift;

void __init ps3_calibrate_decr(void)
{
	int result;
	u64 tmp;

	result = ps3_repository_read_be_tb_freq(0, &tmp);
	BUG_ON(result);

	ppc_tb_freq = tmp;
	ppc_proc_freq = ppc_tb_freq * 40;

	rtc_shift = ps3_os_area_rtc_diff();
}

static u64 read_rtc(void)
{
	int result;
	u64 rtc_val;
	u64 tb_val;

	result = lv1_get_rtc(&rtc_val, &tb_val);
	BUG_ON(result);

	return rtc_val;
}

int ps3_set_rtc_time(struct rtc_time *tm)
{
	u64 now = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc_shift = now - read_rtc();
	return 0;
}

void ps3_get_rtc_time(struct rtc_time *tm)
{
	to_tm(read_rtc() + rtc_shift, tm);
	tm->tm_year -= 1900;
	tm->tm_mon -= 1;
}

unsigned long __init ps3_get_boot_time(void)
{
	return read_rtc() + rtc_shift;
}
