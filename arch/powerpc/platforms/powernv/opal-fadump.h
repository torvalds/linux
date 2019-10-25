/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Firmware-Assisted Dump support on POWER platform (OPAL).
 *
 * Copyright 2019, Hari Bathini, IBM Corporation.
 */

#ifndef _POWERNV_OPAL_FADUMP_H
#define _POWERNV_OPAL_FADUMP_H

#include <asm/reg.h>

/*
 * With kernel & initrd loaded at 512MB (with 256MB size), enforce a minimum
 * boot memory size of 768MB to ensure f/w loading kernel and initrd doesn't
 * mess with crash'ed kernel's memory during MPIPL.
 */
#define OPAL_FADUMP_MIN_BOOT_MEM		(0x30000000UL)

/*
 * OPAL FADump metadata structure format version
 *
 * OPAL FADump kernel metadata structure stores kernel metadata needed to
 * register-for/process crash dump. Format version is used to keep a tab on
 * the changes in the structure format. The changes, if any, to the format
 * are expected to be minimal and backward compatible.
 */
#define OPAL_FADUMP_VERSION			0x1

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
	struct opal_mpipl_region	rgn[FADUMP_MAX_MEM_REGS];
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

static inline void opal_fadump_set_regval_regnum(struct pt_regs *regs,
						 u32 reg_type, u32 reg_num,
						 u64 reg_val)
{
	if (reg_type == HDAT_FADUMP_REG_TYPE_GPR) {
		if (reg_num < 32)
			regs->gpr[reg_num] = reg_val;
		return;
	}

	switch (reg_num) {
	case SPRN_CTR:
		regs->ctr = reg_val;
		break;
	case SPRN_LR:
		regs->link = reg_val;
		break;
	case SPRN_XER:
		regs->xer = reg_val;
		break;
	case SPRN_DAR:
		regs->dar = reg_val;
		break;
	case SPRN_DSISR:
		regs->dsisr = reg_val;
		break;
	case HDAT_FADUMP_REG_ID_NIP:
		regs->nip = reg_val;
		break;
	case HDAT_FADUMP_REG_ID_MSR:
		regs->msr = reg_val;
		break;
	case HDAT_FADUMP_REG_ID_CCR:
		regs->ccr = reg_val;
		break;
	}
}

static inline void opal_fadump_read_regs(char *bufp, unsigned int regs_cnt,
					 unsigned int reg_entry_size,
					 bool cpu_endian,
					 struct pt_regs *regs)
{
	struct hdat_fadump_reg_entry *reg_entry;
	u64 val;
	int i;

	memset(regs, 0, sizeof(struct pt_regs));

	for (i = 0; i < regs_cnt; i++, bufp += reg_entry_size) {
		reg_entry = (struct hdat_fadump_reg_entry *)bufp;
		val = (cpu_endian ? be64_to_cpu(reg_entry->reg_val) :
		       reg_entry->reg_val);
		opal_fadump_set_regval_regnum(regs,
					      be32_to_cpu(reg_entry->reg_type),
					      be32_to_cpu(reg_entry->reg_num),
					      val);
	}
}

#endif /* _POWERNV_OPAL_FADUMP_H */
