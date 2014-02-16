/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef __TILE_VDSO_H__
#define __TILE_VDSO_H__

#include <linux/types.h>

/*
 * Note about the vdso_data structure:
 *
 * NEVER USE THEM IN USERSPACE CODE DIRECTLY. The layout of the
 * structure is supposed to be known only to the function in the vdso
 * itself and may change without notice.
 */

struct vdso_data {
	__u64 tz_update_count;  /* Timezone atomicity ctr             */
	__u64 tb_update_count;  /* Timebase atomicity ctr             */
	__u64 xtime_tod_stamp;  /* TOD clock for xtime                */
	__u64 xtime_clock_sec;  /* Kernel time second                 */
	__u64 xtime_clock_nsec; /* Kernel time nanosecond             */
	__u64 wtom_clock_sec;   /* Wall to monotonic clock second     */
	__u64 wtom_clock_nsec;  /* Wall to monotonic clock nanosecond */
	__u32 mult;             /* Cycle to nanosecond multiplier     */
	__u32 shift;            /* Cycle to nanosecond divisor (power of two) */
	__u32 tz_minuteswest;   /* Minutes west of Greenwich          */
	__u32 tz_dsttime;       /* Type of dst correction             */
};

extern struct vdso_data *vdso_data;

/* __vdso_rt_sigreturn is defined with the addresses in the vdso page. */
extern void __vdso_rt_sigreturn(void);

extern int setup_vdso_pages(void);

#endif /* __TILE_VDSO_H__ */
