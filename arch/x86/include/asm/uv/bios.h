/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_X86_UV_BIOS_H
#define _ASM_X86_UV_BIOS_H

/*
 * UV BIOS layer definitions.
 *
 * (C) Copyright 2020 Hewlett Packard Enterprise Development LP
 * Copyright (C) 2007-2017 Silicon Graphics, Inc. All rights reserved.
 * Copyright (c) Russ Anderson <rja@sgi.com>
 */

#include <linux/efi.h>
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

#define UV_BIOS_EXTRA			    0x10000
#define UV_BIOS_GET_PCI_TOPOLOGY	    0x10001
#define UV_BIOS_GET_GEOINFO		    0x10003

#define UV_BIOS_EXTRA_OP_MEM_COPYIN	    0x1000
#define UV_BIOS_EXTRA_OP_MEM_COPYOUT	    0x2000
#define UV_BIOS_EXTRA_OP_MASK		    0x0fff
#define UV_BIOS_EXTRA_GET_HEAPSIZE	    1
#define UV_BIOS_EXTRA_INSTALL_HEAP	    2
#define UV_BIOS_EXTRA_MASTER_NASID	    3
#define UV_BIOS_EXTRA_OBJECT_COUNT	    (10|UV_BIOS_EXTRA_OP_MEM_COPYOUT)
#define UV_BIOS_EXTRA_ENUM_OBJECTS	    (12|UV_BIOS_EXTRA_OP_MEM_COPYOUT)
#define UV_BIOS_EXTRA_ENUM_PORTS	    (13|UV_BIOS_EXTRA_OP_MEM_COPYOUT)

/*
 * Status values returned from a BIOS call.
 */
enum {
	BIOS_STATUS_MORE_PASSES		=  1,
	BIOS_STATUS_SUCCESS		=  0,
	BIOS_STATUS_UNIMPLEMENTED	= -ENOSYS,
	BIOS_STATUS_EINVAL		= -EINVAL,
	BIOS_STATUS_UNAVAIL		= -EBUSY,
	BIOS_STATUS_ABORT		= -EINTR,
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

#define	UV_AT_SIZE	8	/* 7 character arch type + NULL char */
struct uv_arch_type_entry {
	char	archtype[UV_AT_SIZE];
};

#define	UV_SYSTAB_SIG			"UVST"
#define	UV_SYSTAB_VERSION_1		1	/* UV2/3 BIOS version */
#define	UV_SYSTAB_VERSION_UV4		0x400	/* UV4 BIOS base version */
#define	UV_SYSTAB_VERSION_UV4_1		0x401	/* + gpa_shift */
#define	UV_SYSTAB_VERSION_UV4_2		0x402	/* + TYPE_NVRAM/WINDOW/MBOX */
#define	UV_SYSTAB_VERSION_UV4_3		0x403	/* - GAM Range PXM Value */
#define	UV_SYSTAB_VERSION_UV4_LATEST	UV_SYSTAB_VERSION_UV4_3

#define	UV_SYSTAB_VERSION_UV5		0x500	/* UV5 GAM base version */
#define	UV_SYSTAB_VERSION_UV5_LATEST	UV_SYSTAB_VERSION_UV5

#define	UV_SYSTAB_TYPE_UNUSED		0	/* End of table (offset == 0) */
#define	UV_SYSTAB_TYPE_GAM_PARAMS	1	/* GAM PARAM conversions */
#define	UV_SYSTAB_TYPE_GAM_RNG_TBL	2	/* GAM entry table */
#define	UV_SYSTAB_TYPE_ARCH_TYPE	3	/* UV arch type */
#define	UV_SYSTAB_TYPE_MAX		4

/*
 * The UV system table describes specific firmware
 * capabilities available to the Linux kernel at runtime.
 */
struct uv_systab {
	char signature[4];	/* must be UV_SYSTAB_SIG */
	u32 revision;		/* distinguish different firmware revs */
	u64 (__efiapi *function)(enum uv_bios_cmd, ...);
				/* BIOS runtime callback function ptr */
	u32 size;		/* systab size (starting with _VERSION_UV4) */
	struct {
		u32 type:8;	/* type of entry */
		u32 offset:24;	/* byte offset from struct start to entry */
	} entry[1];		/* additional entries follow */
};
extern struct uv_systab *uv_systab;

#define UV_BIOS_MAXSTRING	      128
struct uv_bios_hub_info {
	unsigned int id;
	union {
		struct {
			unsigned long long this_part:1;
			unsigned long long is_shared:1;
			unsigned long long is_disabled:1;
		} fields;
		struct {
			unsigned long long flags;
			unsigned long long reserved;
		} b;
	} f;
	char name[UV_BIOS_MAXSTRING];
	char location[UV_BIOS_MAXSTRING];
	unsigned int ports;
};

struct uv_bios_port_info {
	unsigned int port;
	unsigned int conn_id;
	unsigned int conn_port;
};

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

extern s64 uv_bios_get_sn_info(int, int *, long *, long *, long *, long *);
extern s64 uv_bios_freq_base(u64, u64 *);
extern int uv_bios_mq_watchlist_alloc(unsigned long, unsigned int,
					unsigned long *);
extern int uv_bios_mq_watchlist_free(int, int);
extern s64 uv_bios_change_memprotect(u64, u64, enum uv_memprotect);
extern s64 uv_bios_reserved_page_pa(u64, u64 *, u64 *, u64 *);
extern int uv_bios_set_legacy_vga_target(bool decode, int domain, int bus);

extern s64 uv_bios_get_master_nasid(u64 sz, u64 *nasid);
extern s64 uv_bios_get_heapsize(u64 nasid, u64 sz, u64 *heap_sz);
extern s64 uv_bios_install_heap(u64 nasid, u64 sz, u64 *heap);
extern s64 uv_bios_obj_count(u64 nasid, u64 sz, u64 *objcnt);
extern s64 uv_bios_enum_objs(u64 nasid, u64 sz, u64 *objbuf);
extern s64 uv_bios_enum_ports(u64 nasid, u64 obj_id, u64 sz, u64 *portbuf);
extern s64 uv_bios_get_geoinfo(u64 nasid, u64 sz, u64 *geo);
extern s64 uv_bios_get_pci_topology(u64 sz, u64 *buf);

extern int uv_bios_init(void);
extern unsigned long get_uv_systab_phys(bool msg);

extern unsigned long sn_rtc_cycles_per_second;
extern int uv_type;
extern long sn_partition_id;
extern long sn_coherency_id;
extern long sn_region_size;
extern long system_serial_number;
extern ssize_t uv_get_archtype(char *buf, int len);
extern int uv_get_hubless_system(void);

extern struct kobject *sgi_uv_kobj;	/* /sys/firmware/sgi_uv */

/*
 * EFI runtime lock; cf. firmware/efi/runtime-wrappers.c for details
 */
extern struct semaphore __efi_uv_runtime_lock;

#endif /* _ASM_X86_UV_BIOS_H */
