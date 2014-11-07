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

#include <linux/seqlock.h>
#include <linux/types.h>

/*
 * Note about the vdso_data structure:
 *
 * NEVER USE THEM IN USERSPACE CODE DIRECTLY. The layout of the
 * structure is supposed to be known only to the function in the vdso
 * itself and may change without notice.
 */

struct vdso_data {
	seqcount_t tz_seq;	/* Timezone seqlock                   */
	seqcount_t tb_seq;	/* Timebase seqlock                   */
	__u64 cycle_last;       /* TOD clock for xtime                */
	__u64 mask;             /* Cycle mask                         */
	__u32 mult;             /* Cycle to nanosecond multiplier     */
	__u32 shift;            /* Cycle to nanosecond divisor (power of two) */
	__u64 wall_time_sec;
	__u64 wall_time_snsec;
	__u64 monotonic_time_sec;
	__u64 monotonic_time_snsec;
	__u64 wall_time_coarse_sec;
	__u64 wall_time_coarse_nsec;
	__u64 monotonic_time_coarse_sec;
	__u64 monotonic_time_coarse_nsec;
	__u32 tz_minuteswest;   /* Minutes west of Greenwich          */
	__u32 tz_dsttime;       /* Type of dst correction             */
};

extern struct vdso_data *vdso_data;

/* __vdso_rt_sigreturn is defined with the addresses in the vdso page. */
extern void __vdso_rt_sigreturn(void);

extern int setup_vdso_pages(void);

#endif /* __TILE_VDSO_H__ */
