/*
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_VDSO_DATAPAGE_H
#define __ASM_VDSO_DATAPAGE_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

struct vdso_data {
	__u64 cs_cycle_last;	/* Timebase at clocksource init */
	__u64 raw_time_sec;	/* Raw time */
	__u64 raw_time_nsec;
	__u64 xtime_clock_sec;	/* Kernel time */
	__u64 xtime_clock_nsec;
	__u64 xtime_coarse_sec;	/* Coarse time */
	__u64 xtime_coarse_nsec;
	__u64 wtm_clock_sec;	/* Wall to monotonic time */
	__u64 wtm_clock_nsec;
	__u32 tb_seq_count;	/* Timebase sequence counter */
	/* cs_* members must be adjacent and in this order (ldp accesses) */
	__u32 cs_mono_mult;	/* NTP-adjusted clocksource multiplier */
	__u32 cs_shift;		/* Clocksource shift (mono = raw) */
	__u32 cs_raw_mult;	/* Raw clocksource multiplier */
	__u32 tz_minuteswest;	/* Whacky timezone stuff */
	__u32 tz_dsttime;
	__u32 use_syscall;
	__u32 hrtimer_res;
};

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* __ASM_VDSO_DATAPAGE_H */
