/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#ifndef __MINIDUMP_PRIVATE_H
#define __MINIDUMP_PRIVATE_H

#define MD_REVISION		1
#define SBL_MINIDUMP_SMEM_ID	0
#define MAX_NUM_OF_SS		10
#define MD_SS_HLOS_ID		0
#define SMEM_ENTRY_SIZE		40

/* Bootloader has 16 byte support, 4 bytes reserved for itself */
#define MAX_REGION_NAME_LENGTH	16

#define MD_REGION_VALID		('V' << 24 | 'A' << 16 | 'L' << 8 | 'I' << 0)
#define MD_REGION_INVALID	('I' << 24 | 'N' << 16 | 'V' << 8 | 'A' << 0)
#define MD_REGION_INIT		('I' << 24 | 'N' << 16 | 'I' << 8 | 'T' << 0)
#define MD_REGION_NOINIT	0

#define MD_SS_ENCR_REQ		(0 << 24 | 'Y' << 16 | 'E' << 8 | 'S' << 0)
#define MD_SS_ENCR_NOTREQ	(0 << 24 | 0 << 16 | 'N' << 8 | 'R' << 0)
#define MD_SS_ENCR_NONE		('N' << 24 | 'O' << 16 | 'N' << 8 | 'E' << 0)
#define MD_SS_ENCR_DONE		('D' << 24 | 'O' << 16 | 'N' << 8 | 'E' << 0)
#define MD_SS_ENCR_START	('S' << 24 | 'T' << 16 | 'R' << 8 | 'T' << 0)
#define MD_SS_ENABLED		('E' << 24 | 'N' << 16 | 'B' << 8 | 'L' << 0)
#define MD_SS_DISABLED		('D' << 24 | 'S' << 16 | 'B' << 8 | 'L' << 0)

/**
 * md_ss_region - Minidump region
 * @name		: Name of the region to be dumped
 * @seq_num:		: Use to differentiate regions with same name.
 * @md_valid		: This entry to be dumped (if set to 1)
 * @region_base_address	: Physical address of region to be dumped
 * @region_size		: Size of the region
 */
struct md_ss_region {
	char	name[MAX_REGION_NAME_LENGTH];
	u32	seq_num;
	u32	md_valid;
	u64	region_base_address;
	u64	region_size;
};

/**
 * md_ss_toc: Sub system SMEM Table of content
 * @md_ss_toc_init : SS toc init status
 * @md_ss_enable_status : if set to 1, Bootloader would dump this SS regions
 * @encryption_status: Encryption status for this subsystem
 * @encryption_required : Decides to encrypt the SS regions or not
 * @ss_region_count : Number of regions added in this SS toc
 * @md_ss_smem_regions_baseptr : regions base pointer of the Subsystem
 * @elf_header : base pointer of the minidump elf header
 * @minidump_table : base pointer of the minidump_table
 */
struct md_ss_toc {
	u32			md_ss_toc_init;
	u32			md_ss_enable_status;
	u32			encryption_status;
	u32			encryption_required;
	u32			ss_region_count;
	u64			md_ss_smem_regions_baseptr;
	u64			elf_header;
	u64			elf_size;
	u64			minidump_table;
};

/**
 * md_global_toc: Global Table of Content
 * @md_toc_init : Global Minidump init status
 * @md_revision : Minidump revision
 * @md_enable_status : Minidump enable status
 * @md_ss_toc : Array of subsystems toc
 */
struct md_global_toc {
	u32			md_toc_init;
	u32			md_revision;
	u32			md_enable_status;
	struct md_ss_toc	md_ss_toc[MAX_NUM_OF_SS];
};

int rk_minidump_log_init(void);
extern void rk_minidump_flush_elfheader(void);
extern void dump_stack_minidump(u64 sp);
extern struct md_region *md_get_region(char *name);
int md_is_in_the_region(u64 addr);
#endif
