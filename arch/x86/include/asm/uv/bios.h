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

/* Address map parameters */
struct uv_gam_parameters {
	u64	mmr_base;
	u64	gru_base;
	u8	mmr_shift;	/* Convert PNode to MMR space offset */
	u8	gru_shift;	/* Convert PNode to GRU space offset */
	u8	gpa_shift;	/* Size of offset field in GRU phys addr */
	u8	unused1;
};

/* UV_TABLE_GAM_RANGE_ENTRY values */
#define UV_GAM_RANGE_TYPE_UNUSED	0 /* End of table */
#define UV_GAM_RANGE_TYPE_RAM		1 /* Normal RAM */
#define UV_GAM_RANGE_TYPE_NVRAM		2 /* Non-volatile memory */
#define UV_GAM_RANGE_TYPE_NV_WINDOW	3 /* NVMDIMM block window */
#define UV_GAM_RANGE_TYPE_NV_MAILBOX	4 /* NVMDIMM mailbox */
#define UV_GAM_RANGE_TYPE_HOLE		5 /* Unused address range */
#define UV_GAM_RANGE_TYPE_MAX		6

/* The structure stores PA bits 56:26, for 64MB granularity */
#define UV_GAM_RANGE_SHFT		26		/* 64MB */

struct uv_gam_range_entry {
	char	type;		/* Entry type: GAM_RANGE_TYPE_UNUSED, etc. */
	char	unused1;
	u16	nasid;		/* HNasid */
	u16	sockid;		/* Socket ID, high bits of APIC ID */
	u16	pnode;		/* Index to MMR and GRU spaces */
	u32	unused2;
	u32	limit;		/* PA bits 56:26 (UV_GAM_RANGE_SHFT) */
};

#define	UV_SYSTAB_SIG			"UVST"
#define	UV_SYSTAB_VERSION_1		1	/* UV1/2/3 BIOS version */
#define	UV_SYSTAB_VERSION_UV4		0x400	/* UV4 BIOS base version */
#define	UV_SYSTAB_VERSION_UV4_1		0x401	/* + gpa_shift */
#define	UV_SYSTAB_VERSION_UV4_2		0x402	/* + TYPE_NVRAM/WINDOW/MBOX */
#define	UV_SYSTAB_VERSION_UV4_3		0x403	/* - GAM Range PXM Value */
#define	UV_SYSTAB_VERSION_UV4_LATEST	UV_SYSTAB_VERSION_UV4_3

#define	UV_SYSTAB_TYPE_UNUSED		0	/* End of table (offset == 0) */
#define	UV_SYSTAB_TYPE_GAM_PARAMS	1	/* GAM PARAM conversions */
#define	UV_SYSTAB_TYPE_GAM_RNG_TBL	2	/* GAM entry table */
#define	UV_SYSTAB_TYPE_MAX		3

/*
 * The UV system table describes specific firmware
 * capabilities available to the Linux kernel at runtime.
 */
struct uv_systab {
	char signature[4];	/* must be UV_SYSTAB_SIG */
	u32 revision;		/* distinguish different firmware revs */
	u64 function;		/* BIOS runtime callback function ptr */
	u32 size;		/* systab size (starting with _VERSION_UV4) */
	struct {
		u32 type:8;	/* type of entry */
		u32 offset:24;	/* byte offset from struct start to entry */
	} entry[1];		/* additional entries follow */
};
extern struct uv_systab *uv_systab;
/* (... end of definitions from UV BIOS ...) */

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

#ifdef CONFIG_EFI
extern void uv_bios_init(void);
#else
void uv_bios_init(void) { }
#endif

extern unsigned long sn_rtc_cycles_per_second;
extern int uv_type;
extern long sn_partition_id;
extern long sn_coherency_id;
extern long sn_region_size;
extern long system_serial_number;
#define uv_partition_coherence_id()	(sn_coherency_id)

extern struct kobject *sgi_uv_kobj;	/* /sys/firmware/sgi_uv */

#endif /* _ASM_X86_UV_BIOS_H */
