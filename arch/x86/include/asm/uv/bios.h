#ifndef _ASM_X86_UV_BIOS_H
#define _ASM_X86_UV_BIOS_H

/*
 * UV BIOS layer definitions.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Copyright (c) 2008-2009 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Russ Anderson <rja@sgi.com>
 */

#include <linux/rtc.h>

/*
 * Values for the BIOS calls.  It is passed as the first * argument in the
 * BIOS call.  Passing any other value in the first argument will result
 * in a BIOS_STATUS_UNIMPLEMENTED return status.
 */
enum uv_bios_cmd {
	UV_BIOS_COMMON,
	UV_BIOS_GET_SN_INFO,
	UV_BIOS_FREQ_BASE,
	UV_BIOS_WATCHLIST_ALLOC,
	UV_BIOS_WATCHLIST_FREE,
	UV_BIOS_MEMPROTECT,
	UV_BIOS_GET_PARTITION_ADDR,
	UV_BIOS_SET_LEGACY_VGA_TARGET
};

/*
 * Status values returned from a BIOS call.
 */
enum {
	BIOS_STATUS_MORE_PASSES		=  1,
	BIOS_STATUS_SUCCESS		=  0,
	BIOS_STATUS_UNIMPLEMENTED	= -ENOSYS,
	BIOS_STATUS_EINVAL		= -EINVAL,
	BIOS_STATUS_UNAVAIL		= -EBUSY
};

/*
 * The UV system table describes specific firmware
 * capabilities available to the Linux kernel at runtime.
 */
struct uv_systab {
	char signature[4];	/* must be "UVST" */
	u32 revision;		/* distinguish different firmware revs */
	u64 function;		/* BIOS runtime callback function ptr */
};

enum {
	BIOS_FREQ_BASE_PLATFORM = 0,
	BIOS_FREQ_BASE_INTERVAL_TIMER = 1,
	BIOS_FREQ_BASE_REALTIME_CLOCK = 2
};

union partition_info_u {
	u64	val;
	struct {
		u64	hub_version	:  8,
			partition_id	: 16,
			coherence_id	: 16,
			region_size	: 24;
	};
};

enum uv_memprotect {
	UV_MEMPROT_RESTRICT_ACCESS,
	UV_MEMPROT_ALLOW_AMO,
	UV_MEMPROT_ALLOW_RW
};

/*
 * bios calls have 6 parameters
 */
extern s64 uv_bios_call(enum uv_bios_cmd, u64, u64, u64, u64, u64);
extern s64 uv_bios_call_irqsave(enum uv_bios_cmd, u64, u64, u64, u64, u64);
extern s64 uv_bios_call_reentrant(enum uv_bios_cmd, u64, u64, u64, u64, u64);

extern s64 uv_bios_get_sn_info(int, int *, long *, long *, long *, long *);
extern s64 uv_bios_freq_base(u64, u64 *);
extern int uv_bios_mq_watchlist_alloc(unsigned long, unsigned int,
					unsigned long *);
extern int uv_bios_mq_watchlist_free(int, int);
extern s64 uv_bios_change_memprotect(u64, u64, enum uv_memprotect);
extern s64 uv_bios_reserved_page_pa(u64, u64 *, u64 *, u64 *);
extern int uv_bios_set_legacy_vga_target(bool decode, int domain, int bus);

extern void uv_bios_init(void);

extern unsigned long sn_rtc_cycles_per_second;
extern int uv_type;
extern long sn_partition_id;
extern long sn_coherency_id;
extern long sn_region_size;
extern long system_serial_number;
#define partition_coherence_id()	(sn_coherency_id)

extern struct kobject *sgi_uv_kobj;	/* /sys/firmware/sgi_uv */

#endif /* _ASM_X86_UV_BIOS_H */
