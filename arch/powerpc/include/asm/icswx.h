/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ICSWX api
 *
 * Copyright (C) 2015 IBM Corp.
 *
 * This provides the Initiate Coprocessor Store Word Indexed (ICSWX)
 * instruction.  This instruction is used to communicate with PowerPC
 * coprocessors.  This also provides definitions of the structures used
 * to communicate with the coprocessor.
 *
 * The RFC02130: Coprocessor Architecture document is the reference for
 * everything in this file unless otherwise noted.
 */
#ifndef _ARCH_POWERPC_INCLUDE_ASM_ICSWX_H_
#define _ARCH_POWERPC_INCLUDE_ASM_ICSWX_H_

#include <asm/ppc-opcode.h> /* for PPC_ICSWX */

/* Chapter 6.5.8 Coprocessor-Completion Block (CCB) */

#define CCB_VALUE		(0x3fffffffffffffff)
#define CCB_ADDRESS		(0xfffffffffffffff8)
#define CCB_CM			(0x0000000000000007)
#define CCB_CM0			(0x0000000000000004)
#define CCB_CM12		(0x0000000000000003)

#define CCB_CM0_ALL_COMPLETIONS	(0x0)
#define CCB_CM0_LAST_IN_CHAIN	(0x4)
#define CCB_CM12_STORE		(0x0)
#define CCB_CM12_INTERRUPT	(0x1)

#define CCB_SIZE		(0x10)
#define CCB_ALIGN		CCB_SIZE

struct coprocessor_completion_block {
	__be64 value;
	__be64 address;
} __packed __aligned(CCB_ALIGN);


/* Chapter 6.5.7 Coprocessor-Status Block (CSB) */

#define CSB_V			(0x80)
#define CSB_F			(0x04)
#define CSB_CH			(0x03)
#define CSB_CE_INCOMPLETE	(0x80)
#define CSB_CE_TERMINATION	(0x40)
#define CSB_CE_TPBC		(0x20)

#define CSB_CC_SUCCESS		(0)
#define CSB_CC_INVALID_ALIGN	(1)
#define CSB_CC_OPERAND_OVERLAP	(2)
#define CSB_CC_DATA_LENGTH	(3)
#define CSB_CC_TRANSLATION	(5)
#define CSB_CC_PROTECTION	(6)
#define CSB_CC_RD_EXTERNAL	(7)
#define CSB_CC_INVALID_OPERAND	(8)
#define CSB_CC_PRIVILEGE	(9)
#define CSB_CC_INTERNAL		(10)
#define CSB_CC_WR_EXTERNAL	(12)
#define CSB_CC_NOSPC		(13)
#define CSB_CC_EXCESSIVE_DDE	(14)
#define CSB_CC_WR_TRANSLATION	(15)
#define CSB_CC_WR_PROTECTION	(16)
#define CSB_CC_UNKNOWN_CODE	(17)
#define CSB_CC_ABORT		(18)
#define CSB_CC_EXCEED_BYTE_COUNT	(19)	/* P9 or later */
#define CSB_CC_TRANSPORT	(20)
#define CSB_CC_INVALID_CRB	(21)	/* P9 or later */
#define CSB_CC_INVALID_DDE	(30)	/* P9 or later */
#define CSB_CC_SEGMENTED_DDL	(31)
#define CSB_CC_PROGRESS_POINT	(32)
#define CSB_CC_DDE_OVERFLOW	(33)
#define CSB_CC_SESSION		(34)
#define CSB_CC_PROVISION	(36)
#define CSB_CC_CHAIN		(37)
#define CSB_CC_SEQUENCE		(38)
#define CSB_CC_HW		(39)
/* P9 DD2 NX Workbook 3.2 (Table 4-36): Address translation fault */
#define	CSB_CC_FAULT_ADDRESS	(250)

#define CSB_SIZE		(0x10)
#define CSB_ALIGN		CSB_SIZE

struct coprocessor_status_block {
	u8 flags;
	u8 cs;
	u8 cc;
	u8 ce;
	__be32 count;
	__be64 address;
} __packed __aligned(CSB_ALIGN);


/* Chapter 6.5.10 Data-Descriptor List (DDL)
 * each list contains one or more Data-Descriptor Entries (DDE)
 */

#define DDE_P			(0x8000)

#define DDE_SIZE		(0x10)
#define DDE_ALIGN		DDE_SIZE

struct data_descriptor_entry {
	__be16 flags;
	u8 count;
	u8 index;
	__be32 length;
	__be64 address;
} __packed __aligned(DDE_ALIGN);

/* 4.3.2 NX-stamped Fault CRB */

#define NX_STAMP_ALIGN          (0x10)

struct nx_fault_stamp {
	__be64 fault_storage_addr;
	__be16 reserved;
	__u8   flags;
	__u8   fault_status;
	__be32 pswid;
} __packed __aligned(NX_STAMP_ALIGN);

/* Chapter 6.5.2 Coprocessor-Request Block (CRB) */

#define CRB_SIZE		(0x80)
#define CRB_ALIGN		(0x100) /* Errata: requires 256 alignment */

/* Coprocessor Status Block field
 *   ADDRESS	address of CSB
 *   C		CCB is valid
 *   AT		0 = addrs are virtual, 1 = addrs are phys
 *   M		enable perf monitor
 */
#define CRB_CSB_ADDRESS		(0xfffffffffffffff0)
#define CRB_CSB_C		(0x0000000000000008)
#define CRB_CSB_AT		(0x0000000000000002)
#define CRB_CSB_M		(0x0000000000000001)

struct coprocessor_request_block {
	__be32 ccw;
	__be32 flags;
	__be64 csb_addr;

	struct data_descriptor_entry source;
	struct data_descriptor_entry target;

	struct coprocessor_completion_block ccb;

	union {
		struct nx_fault_stamp nx;
		u8 reserved[16];
	} stamp;

	u8 reserved[32];

	struct coprocessor_status_block csb;
} __packed;


/* RFC02167 Initiate Coprocessor Instructions document
 * Chapter 8.2.1.1.1 RS
 * Chapter 8.2.3 Coprocessor Directive
 * Chapter 8.2.4 Execution
 *
 * The CCW must be converted to BE before passing to icswx()
 */

#define CCW_PS			(0xff000000)
#define CCW_CT			(0x00ff0000)
#define CCW_CD			(0x0000ffff)
#define CCW_CL			(0x0000c000)


/* RFC02167 Initiate Coprocessor Instructions document
 * Chapter 8.2.1 Initiate Coprocessor Store Word Indexed (ICSWX)
 * Chapter 8.2.4.1 Condition Register 0
 */

#define ICSWX_INITIATED		(0x8)
#define ICSWX_BUSY		(0x4)
#define ICSWX_REJECTED		(0x2)
#define ICSWX_XERS0		(0x1)	/* undefined or set from XERSO. */

static inline int icswx(__be32 ccw, struct coprocessor_request_block *crb)
{
	__be64 ccw_reg = ccw;
	u32 cr;

	__asm__ __volatile__(
	PPC_ICSWX(%1,0,%2) "\n"
	"mfcr %0\n"
	: "=r" (cr)
	: "r" (ccw_reg), "r" (crb)
	: "cr0", "memory");

	return (int)((cr >> 28) & 0xf);
}


#endif /* _ARCH_POWERPC_INCLUDE_ASM_ICSWX_H_ */
