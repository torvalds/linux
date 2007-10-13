/*
 * Guest OS Interfaces.
 *
 * (C) Copyright 2006 TOSHIBA CORPORATION
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

#ifndef _CELLEB_BEAT_H
#define _CELLEB_BEAT_H

#define DABRX_KERNEL		(1UL<<1)
#define DABRX_USER		(1UL<<0)

int64_t beat_get_term_char(uint64_t,uint64_t*,uint64_t*,uint64_t*);
int64_t beat_put_term_char(uint64_t,uint64_t,uint64_t,uint64_t);
int64_t beat_repository_encode(int, const char *, uint64_t[4]);
void beat_restart(char *);
void beat_power_off(void);
void beat_halt(void);
int beat_set_rtc_time(struct rtc_time *);
void beat_get_rtc_time(struct rtc_time *);
ssize_t beat_nvram_get_size(void);
ssize_t beat_nvram_read(char *, size_t, loff_t *);
ssize_t beat_nvram_write(char *, size_t, loff_t *);
int beat_set_xdabr(unsigned long);
void beat_power_save(void);
void beat_kexec_cpu_down(int, int);

#endif /* _CELLEB_BEAT_H */
