/*
 * Machine check exception header file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#ifndef __ASM_PPC64_MCE_H__
#define __ASM_PPC64_MCE_H__

#include <linux/bitops.h>

/*
 * Machine Check bits on power7 and power8
 */
#define P7_SRR1_MC_LOADSTORE(srr1)	((srr1) & PPC_BIT(42)) /* P8 too */

/* SRR1 bits for machine check (On Power7 and Power8) */
#define P7_SRR1_MC_IFETCH(srr1)	((srr1) & PPC_BITMASK(43, 45)) /* P8 too */

#define P7_SRR1_MC_IFETCH_UE		(0x1 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_PARITY	(0x2 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_MULTIHIT	(0x3 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_SLB_BOTH	(0x4 << PPC_BITLSHIFT(45))
#define P7_SRR1_MC_IFETCH_TLB_MULTIHIT	(0x5 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_TLB_RELOAD	(0x6 << PPC_BITLSHIFT(45)) /* P8 too */
#define P7_SRR1_MC_IFETCH_UE_IFU_INTERNAL	(0x7 << PPC_BITLSHIFT(45))

/* SRR1 bits for machine check (On Power8) */
#define P8_SRR1_MC_IFETCH_ERAT_MULTIHIT	(0x4 << PPC_BITLSHIFT(45))

/* DSISR bits for machine check (On Power7 and Power8) */
#define P7_DSISR_MC_UE			(PPC_BIT(48))	/* P8 too */
#define P7_DSISR_MC_UE_TABLEWALK	(PPC_BIT(49))	/* P8 too */
#define P7_DSISR_MC_ERAT_MULTIHIT	(PPC_BIT(52))	/* P8 too */
#define P7_DSISR_MC_TLB_MULTIHIT_MFTLB	(PPC_BIT(53))	/* P8 too */
#define P7_DSISR_MC_SLB_PARITY_MFSLB	(PPC_BIT(55))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT	(PPC_BIT(56))	/* P8 too */
#define P7_DSISR_MC_SLB_MULTIHIT_PARITY	(PPC_BIT(57))	/* P8 too */

/*
 * DSISR bits for machine check (Power8) in addition to above.
 * Secondary DERAT Multihit
 */
#define P8_DSISR_MC_ERAT_MULTIHIT_SEC	(PPC_BIT(54))

/* SLB error bits */
#define P7_DSISR_MC_SLB_ERRORS		(P7_DSISR_MC_ERAT_MULTIHIT | \
					 P7_DSISR_MC_SLB_PARITY_MFSLB | \
					 P7_DSISR_MC_SLB_MULTIHIT | \
					 P7_DSISR_MC_SLB_MULTIHIT_PARITY)

#define P8_DSISR_MC_SLB_ERRORS		(P7_DSISR_MC_SLB_ERRORS | \
					 P8_DSISR_MC_ERAT_MULTIHIT_SEC)

/*
 * Machine Check bits on power9
 */
#define P9_SRR1_MC_LOADSTORE(srr1)	(((srr1) >> PPC_BITLSHIFT(42)) & 1)

#define P9_SRR1_MC_IFETCH(srr1)	(	\
	PPC_BITEXTRACT(srr1, 45, 0) |	\
	PPC_BITEXTRACT(srr1, 44, 1) |	\
	PPC_BITEXTRACT(srr1, 43, 2) |	\
	PPC_BITEXTRACT(srr1, 36, 3) )

/* 0 is reserved */
#define P9_SRR1_MC_IFETCH_UE				1
#define P9_SRR1_MC_IFETCH_SLB_PARITY			2
#define P9_SRR1_MC_IFETCH_SLB_MULTIHIT			3
#define P9_SRR1_MC_IFETCH_ERAT_MULTIHIT			4
#define P9_SRR1_MC_IFETCH_TLB_MULTIHIT			5
#define P9_SRR1_MC_IFETCH_UE_TLB_RELOAD			6
/* 7 is reserved */
#define P9_SRR1_MC_IFETCH_LINK_TIMEOUT			8
#define P9_SRR1_MC_IFETCH_LINK_TABLEWALK_TIMEOUT	9
/* 10 ? */
#define P9_SRR1_MC_IFETCH_RA			11
#define P9_SRR1_MC_IFETCH_RA_TABLEWALK		12
#define P9_SRR1_MC_IFETCH_RA_ASYNC_STORE		13
#define P9_SRR1_MC_IFETCH_LINK_ASYNC_STORE_TIMEOUT	14
#define P9_SRR1_MC_IFETCH_RA_TABLEWALK_FOREIGN	15

/* DSISR bits for machine check (On Power9) */
#define P9_DSISR_MC_UE					(PPC_BIT(48))
#define P9_DSISR_MC_UE_TABLEWALK			(PPC_BIT(49))
#define P9_DSISR_MC_LINK_LOAD_TIMEOUT			(PPC_BIT(50))
#define P9_DSISR_MC_LINK_TABLEWALK_TIMEOUT		(PPC_BIT(51))
#define P9_DSISR_MC_ERAT_MULTIHIT			(PPC_BIT(52))
#define P9_DSISR_MC_TLB_MULTIHIT_MFTLB			(PPC_BIT(53))
#define P9_DSISR_MC_USER_TLBIE				(PPC_BIT(54))
#define P9_DSISR_MC_SLB_PARITY_MFSLB			(PPC_BIT(55))
#define P9_DSISR_MC_SLB_MULTIHIT_MFSLB			(PPC_BIT(56))
#define P9_DSISR_MC_RA_LOAD				(PPC_BIT(57))
#define P9_DSISR_MC_RA_TABLEWALK			(PPC_BIT(58))
#define P9_DSISR_MC_RA_TABLEWALK_FOREIGN		(PPC_BIT(59))
#define P9_DSISR_MC_RA_FOREIGN				(PPC_BIT(60))

/* SLB error bits */
#define P9_DSISR_MC_SLB_ERRORS		(P9_DSISR_MC_ERAT_MULTIHIT | \
					 P9_DSISR_MC_SLB_PARITY_MFSLB | \
					 P9_DSISR_MC_SLB_MULTIHIT_MFSLB)

enum MCE_Version {
	MCE_V1 = 1,
};

enum MCE_Severity {
	MCE_SEV_NO_ERROR = 0,
	MCE_SEV_WARNING = 1,
	MCE_SEV_ERROR_SYNC = 2,
	MCE_SEV_FATAL = 3,
};

enum MCE_Disposition {
	MCE_DISPOSITION_RECOVERED = 0,
	MCE_DISPOSITION_NOT_RECOVERED = 1,
};

enum MCE_Initiator {
	MCE_INITIATOR_UNKNOWN = 0,
	MCE_INITIATOR_CPU = 1,
};

enum MCE_ErrorType {
	MCE_ERROR_TYPE_UNKNOWN = 0,
	MCE_ERROR_TYPE_UE = 1,
	MCE_ERROR_TYPE_SLB = 2,
	MCE_ERROR_TYPE_ERAT = 3,
	MCE_ERROR_TYPE_TLB = 4,
	MCE_ERROR_TYPE_USER = 5,
	MCE_ERROR_TYPE_RA = 6,
	MCE_ERROR_TYPE_LINK = 7,
};

enum MCE_UeErrorType {
	MCE_UE_ERROR_INDETERMINATE = 0,
	MCE_UE_ERROR_IFETCH = 1,
	MCE_UE_ERROR_PAGE_TABLE_WALK_IFETCH = 2,
	MCE_UE_ERROR_LOAD_STORE = 3,
	MCE_UE_ERROR_PAGE_TABLE_WALK_LOAD_STORE = 4,
};

enum MCE_SlbErrorType {
	MCE_SLB_ERROR_INDETERMINATE = 0,
	MCE_SLB_ERROR_PARITY = 1,
	MCE_SLB_ERROR_MULTIHIT = 2,
};

enum MCE_EratErrorType {
	MCE_ERAT_ERROR_INDETERMINATE = 0,
	MCE_ERAT_ERROR_PARITY = 1,
	MCE_ERAT_ERROR_MULTIHIT = 2,
};

enum MCE_TlbErrorType {
	MCE_TLB_ERROR_INDETERMINATE = 0,
	MCE_TLB_ERROR_PARITY = 1,
	MCE_TLB_ERROR_MULTIHIT = 2,
};

enum MCE_UserErrorType {
	MCE_USER_ERROR_INDETERMINATE = 0,
	MCE_USER_ERROR_TLBIE = 1,
};

enum MCE_RaErrorType {
	MCE_RA_ERROR_INDETERMINATE = 0,
	MCE_RA_ERROR_IFETCH = 1,
	MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH = 2,
	MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH_FOREIGN = 3,
	MCE_RA_ERROR_LOAD = 4,
	MCE_RA_ERROR_STORE = 5,
	MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE = 6,
	MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE_FOREIGN = 7,
	MCE_RA_ERROR_LOAD_STORE_FOREIGN = 8,
};

enum MCE_LinkErrorType {
	MCE_LINK_ERROR_INDETERMINATE = 0,
	MCE_LINK_ERROR_IFETCH_TIMEOUT = 1,
	MCE_LINK_ERROR_PAGE_TABLE_WALK_IFETCH_TIMEOUT = 2,
	MCE_LINK_ERROR_LOAD_TIMEOUT = 3,
	MCE_LINK_ERROR_STORE_TIMEOUT = 4,
	MCE_LINK_ERROR_PAGE_TABLE_WALK_LOAD_STORE_TIMEOUT = 5,
};

struct machine_check_event {
	enum MCE_Version	version:8;	/* 0x00 */
	uint8_t			in_use;		/* 0x01 */
	enum MCE_Severity	severity:8;	/* 0x02 */
	enum MCE_Initiator	initiator:8;	/* 0x03 */
	enum MCE_ErrorType	error_type:8;	/* 0x04 */
	enum MCE_Disposition	disposition:8;	/* 0x05 */
	uint8_t			reserved_1[2];	/* 0x06 */
	uint64_t		gpr3;		/* 0x08 */
	uint64_t		srr0;		/* 0x10 */
	uint64_t		srr1;		/* 0x18 */
	union {					/* 0x20 */
		struct {
			enum MCE_UeErrorType ue_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		physical_address_provided;
			uint8_t		reserved_1[5];
			uint64_t	effective_address;
			uint64_t	physical_address;
			uint8_t		reserved_2[8];
		} ue_error;

		struct {
			enum MCE_SlbErrorType slb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} slb_error;

		struct {
			enum MCE_EratErrorType erat_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} erat_error;

		struct {
			enum MCE_TlbErrorType tlb_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} tlb_error;

		struct {
			enum MCE_UserErrorType user_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} user_error;

		struct {
			enum MCE_RaErrorType ra_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} ra_error;

		struct {
			enum MCE_LinkErrorType link_error_type:8;
			uint8_t		effective_address_provided;
			uint8_t		reserved_1[6];
			uint64_t	effective_address;
			uint8_t		reserved_2[16];
		} link_error;
	} u;
};

struct mce_error_info {
	enum MCE_ErrorType error_type:8;
	union {
		enum MCE_UeErrorType ue_error_type:8;
		enum MCE_SlbErrorType slb_error_type:8;
		enum MCE_EratErrorType erat_error_type:8;
		enum MCE_TlbErrorType tlb_error_type:8;
		enum MCE_UserErrorType user_error_type:8;
		enum MCE_RaErrorType ra_error_type:8;
		enum MCE_LinkErrorType link_error_type:8;
	} u;
	enum MCE_Severity	severity:8;
	enum MCE_Initiator	initiator:8;
};

#define MAX_MC_EVT	100

/* Release flags for get_mce_event() */
#define MCE_EVENT_RELEASE	true
#define MCE_EVENT_DONTRELEASE	false

extern void save_mce_event(struct pt_regs *regs, long handled,
			   struct mce_error_info *mce_err, uint64_t nip,
			   uint64_t addr);
extern int get_mce_event(struct machine_check_event *mce, bool release);
extern void release_mce_event(void);
extern void machine_check_queue_event(void);
extern void machine_check_print_event_info(struct machine_check_event *evt);
extern uint64_t get_mce_fault_addr(struct machine_check_event *evt);

#endif /* __ASM_PPC64_MCE_H__ */
