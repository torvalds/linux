/*
 * Simple routines for Celleb/Beat
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/rtc.h>

#include <asm/hvconsole.h>
#include <asm/time.h>

#include "beat_wrapper.h"
#include "beat.h"

void beat_restart(char *cmd)
{
	beat_shutdown_logical_partition(1);
}

void beat_power_off(void)
{
	beat_shutdown_logical_partition(0);
}

u64 beat_halt_code = 0x1000000000000000UL;

void beat_halt(void)
{
	beat_shutdown_logical_partition(beat_halt_code);
}

int beat_set_rtc_time(struct rtc_time *rtc_time)
{
	u64 tim;
	tim = mktime(rtc_time->tm_year+1900,
		     rtc_time->tm_mon+1, rtc_time->tm_mday,
		     rtc_time->tm_hour, rtc_time->tm_min, rtc_time->tm_sec);
	if (beat_rtc_write(tim))
		return -1;
	return 0;
}

void beat_get_rtc_time(struct rtc_time *rtc_time)
{
	u64 tim;

	if (beat_rtc_read(&tim))
		tim = 0;
	to_tm(tim, rtc_time);
	rtc_time->tm_year -= 1900;
	rtc_time->tm_mon -= 1;
}

#define	BEAT_NVRAM_SIZE	4096

ssize_t beat_nvram_read(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	char *p = buf;

	if (*index >= BEAT_NVRAM_SIZE)
		return -ENODEV;
	i = *index;
	if (i + count > BEAT_NVRAM_SIZE)
		count = BEAT_NVRAM_SIZE - i;

	for (; count != 0; count -= len) {
		len = count;
		if (len > BEAT_NVRW_CNT)
			len = BEAT_NVRW_CNT;
		if (beat_eeprom_read(i, len, p)) {
			return -EIO;
		}

		p += len;
		i += len;
	}
	*index = i;
	return p - buf;
}

ssize_t beat_nvram_write(char *buf, size_t count, loff_t *index)
{
	unsigned int i;
	unsigned long len;
	char *p = buf;

	if (*index >= BEAT_NVRAM_SIZE)
		return -ENODEV;
	i = *index;
	if (i + count > BEAT_NVRAM_SIZE)
		count = BEAT_NVRAM_SIZE - i;

	for (; count != 0; count -= len) {
		len = count;
		if (len > BEAT_NVRW_CNT)
			len = BEAT_NVRW_CNT;
		if (beat_eeprom_write(i, len, p)) {
			return -EIO;
		}

		p += len;
		i += len;
	}
	*index = i;
	return p - buf;
}

ssize_t beat_nvram_get_size(void)
{
	return BEAT_NVRAM_SIZE;
}

int beat_set_xdabr(unsigned long dabr)
{
	if (beat_set_dabr(dabr, DABRX_KERNEL | DABRX_USER))
		return -1;
	return 0;
}

int64_t beat_get_term_char(u64 vterm, u64 *len, u64 *t1, u64 *t2)
{
	u64 db[2];
	s64 ret;

	ret = beat_get_characters_from_console(vterm, len, (u8*)db);
	if (ret == 0) {
		*t1 = db[0];
		*t2 = db[1];
	}
	return ret;
}

int64_t beat_put_term_char(u64 vterm, u64 len, u64 t1, u64 t2)
{
	u64 db[2];

	db[0] = t1;
	db[1] = t2;
	return beat_put_characters_to_console(vterm, len, (u8*)db);
}

EXPORT_SYMBOL(beat_get_term_char);
EXPORT_SYMBOL(beat_put_term_char);
EXPORT_SYMBOL(beat_halt_code);
