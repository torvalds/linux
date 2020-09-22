/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Machine check exception header file.
 *
 * Copyright 2013 IBM Corporation
 * Author: Mahesh Salgaonkar <mahesh@linux.vnet.ibm.com>
 */

#ifndef __ASM_PPC64_MCE_H__
#define __ASM_PPC64_MCE_H__

#include <linux/bitops.h>

enum MCE_Version {
	MCE_V1 = 1,
};

enum MCE_Severity {
	MCE_SEV_NO_ERROR = 0,
	MCE_SEV_WARNING = 1,
	MCE_SEV_SEVERE = 2,
	MCE_SEV_FATAL = 3,
};

enum MCE_Disposition {
	MCE_DISPOSITION_RECOVERED = 0,
	MCE_DISPOSITION_NOT_RECOVERED = 1,
};

enum MCE_Initiator {
	MCE_INITIATOR_UNKNOWN = 0,
	MCE_INITIATOR_CPU = 1,
	MCE_INITIATOR_PCI = 2,
	MCE_INITIATOR_ISA = 3,
	MCE_INITIATOR_MEMORY= 4,
	MCE_INITIATOR_POWERMGM = 5,
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
	MCE_ERROR_TYPE_DCACHE = 8,
	MCE_ERROR_TYPE_ICACHE = 9,
};

enum MCE_ErrorClass {
	MCE_ECLASS_UNKNOWN = 0,
	MCE_ECLASS_HARDWARE,
	MCE_ECLASS_HARD_INDETERMINATE,
	MCE_ECLASS_SOFTWARE,
	MCE_ECLASS_SOFT_INDETERMINATE,
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
	MCE_USER_ERROR_SCV = 2,
};

enum MCE_RaErrorType {
	MCE_RA_ERROR_INDETERMINATE = 0,
	MCE_RA_ERROR_IFETCH = 1,
	MCE_RA_ERROR_IFETCH_FOREIGN = 2,
	MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH = 3,
	MCE_RA_ERROR_PAGE_TABLE_WALK_IFETCH_FOREIGN = 4,
	MCE_RA_ERROR_LOAD = 5,
	MCE_RA_ERROR_STORE = 6,
	MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE = 7,
	MCE_RA_ERROR_PAGE_TABLE_WALK_LOAD_STORE_FOREIGN = 8,
	MCE_RA_ERROR_LOAD_STORE_FOREIGN = 9,
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
	enum MCE_Version	version:8;
	u8			in_use;
	enum MCE_Severity	severity:8;
	enum MCE_Initiator	initiator:8;
	enum MCE_ErrorType	error_type:8;
	enum MCE_ErrorClass	error_class:8;
	enum MCE_Disposition	disposition:8;
	bool			sync_error;
	u16			cpu;
	u64			gpr3;
	u64			srr0;
	u64			srr1;
	union {
		struct {
			enum MCE_UeErrorType ue_error_type:8;
			u8		effective_address_provided;
			u8		physical_address_provided;
			u8		ignore_event;
			u8		reserved_1[4];
			u64		effective_address;
			u64		physical_address;
			u8		reserved_2[8];
		} ue_error;

		struct {
			enum MCE_SlbErrorType slb_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
		} slb_error;

		struct {
			enum MCE_EratErrorType erat_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
		} erat_error;

		struct {
			enum MCE_TlbErrorType tlb_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
		} tlb_error;

		struct {
			enum MCE_UserErrorType user_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
		} user_error;

		struct {
			enum MCE_RaErrorType ra_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
		} ra_error;

		struct {
			enum MCE_LinkErrorType link_error_type:8;
			u8		effective_address_provided;
			u8		reserved_1[6];
			u64		effective_address;
			u8		reserved_2[16];
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
	enum MCE_ErrorClass	error_class:8;
	bool			sync_error;
	bool			ignore_event;
};

#define MAX_MC_EVT	100

/* Release flags for get_mce_event() */
#define MCE_EVENT_RELEASE	true
#define MCE_EVENT_DONTRELEASE	false

struct pt_regs;
struct notifier_block;

extern void save_mce_event(struct pt_regs *regs, long handled,
			   struct mce_error_info *mce_err, uint64_t nip,
			   uint64_t addr, uint64_t phys_addr);
extern int get_mce_event(struct machine_check_event *mce, bool release);
extern void release_mce_event(void);
extern void machine_check_queue_event(void);
extern void machine_check_print_event_info(struct machine_check_event *evt,
					   bool user_mode, bool in_guest);
unsigned long addr_to_pfn(struct pt_regs *regs, unsigned long addr);
extern void mce_common_process_ue(struct pt_regs *regs,
				  struct mce_error_info *mce_err);
int mce_register_notifier(struct notifier_block *nb);
int mce_unregister_notifier(struct notifier_block *nb);
#ifdef CONFIG_PPC_BOOK3S_64
void flush_and_reload_slb(void);
long __machine_check_early_realmode_p7(struct pt_regs *regs);
long __machine_check_early_realmode_p8(struct pt_regs *regs);
long __machine_check_early_realmode_p9(struct pt_regs *regs);
long __machine_check_early_realmode_p10(struct pt_regs *regs);
#endif /* CONFIG_PPC_BOOK3S_64 */
#endif /* __ASM_PPC64_MCE_H__ */
