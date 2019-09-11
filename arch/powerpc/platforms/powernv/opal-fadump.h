/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware-Assisted Dump support on POWER platform (OPAL).
 *
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#ifndef _POWERNV_OPAL_FADUMP_H
#define _POWERNV_OPAL_FADUMP_H

/*
 * OPAL FADump metadata structure format version
 *
 * OPAL FADump kernel metadata structure stores kernel metadata needed to
 * register-for/process crash dump. Format version is used to keep a tab on
 * the changes in the structure format. The changes, if any, to the format
 * are expected to be minimal and backward compatible.
 */
#define OPAL_FADUMP_VERSION			0x1

/* Maximum number of memory regions kernel supports */
#define OPAL_FADUMP_MAX_MEM_REGS		128

/*
 * OPAL FADump kernel metadata
 *
 * The address of this structure will be registered with f/w for retrieving
 * and processing during crash dump.
 */
struct opal_fadump_mem_struct {
	u8	version;
	u8	reserved[3];
	u16	region_cnt;		/* number of regions */
	u16	registered_regions;	/* Regions registered for MPIPL */
	u64	fadumphdr_addr;
	struct opal_mpipl_region	rgn[OPAL_FADUMP_MAX_MEM_REGS];
} __packed;

/*
 * CPU state data
 *
 * CPU state data information is provided by f/w. The format for this data
 * is defined in the HDAT spec. Version is used to keep a tab on the changes
 * in this CPU state data format. Changes to this format are unlikely, but
 * if there are any changes, please refer to latest HDAT specification.
 */
#define HDAT_FADUMP_CPU_DATA_VER		1

#define HDAT_FADUMP_CORE_INACTIVE		(0x0F)

/* HDAT thread header for register entries */
struct hdat_fadump_thread_hdr {
	__be32  pir;
	/* 0x00 - 0x0F - The corresponding stop state of the core */
	u8      core_state;
	u8      reserved[3];

	__be32	offset;	/* Offset to Register Entries array */
	__be32	ecnt;	/* Number of entries */
	__be32	esize;	/* Alloc size of each array entry in bytes */
	__be32	eactsz;	/* Actual size of each array entry in bytes */
} __packed;

/* Register types populated by f/w */
#define HDAT_FADUMP_REG_TYPE_GPR		0x01
#define HDAT_FADUMP_REG_TYPE_SPR		0x02

/* ID numbers used by f/w while populating certain registers */
#define HDAT_FADUMP_REG_ID_NIP			0x7D0
#define HDAT_FADUMP_REG_ID_MSR			0x7D1
#define HDAT_FADUMP_REG_ID_CCR			0x7D2

/* HDAT register entry. */
struct hdat_fadump_reg_entry {
	__be32		reg_type;
	__be32		reg_num;
	__be64		reg_val;
} __packed;

#endif /* _POWERNV_OPAL_FADUMP_H */
