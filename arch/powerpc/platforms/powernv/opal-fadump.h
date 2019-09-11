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

#endif /* _POWERNV_OPAL_FADUMP_H */
