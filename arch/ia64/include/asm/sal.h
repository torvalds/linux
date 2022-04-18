/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_SAL_H
#define _ASM_IA64_SAL_H

/*
 * System Abstraction Layer definitions.
 *
 * This is based on version 2.5 of the manual "IA-64 System
 * Abstraction Layer".
 *
 * Copyright (C) 2001 Intel
 * Copyright (C) 2002 Jenna Hall <jenna.s.hall@intel.com>
 * Copyright (C) 2001 Fred Lewis <frederick.v.lewis@intel.com>
 * Copyright (C) 1998, 1999, 2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Srinivasa Prasad Thirumalachar <sprasad@sprasad.engr.sgi.com>
 *
 * 02/01/04 J. Hall Updated Error Record Structures to conform to July 2001
 *		    revision of the SAL spec.
 * 01/01/03 fvlewis Updated Error Record Structures to conform with Nov. 2000
 *                  revision of the SAL spec.
 * 99/09/29 davidm	Updated for SAL 2.6.
 * 00/03/29 cfleck      Updated SAL Error Logging info for processor (SAL 2.6)
 *                      (plus examples of platform error info structures from smariset @ Intel)
 */

#define IA64_SAL_PLATFORM_FEATURE_BUS_LOCK_BIT		0
#define IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT_BIT	1
#define IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT_BIT	2
#define IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT_BIT	 	3

#define IA64_SAL_PLATFORM_FEATURE_BUS_LOCK	  (1<<IA64_SAL_PLATFORM_FEATURE_BUS_LOCK_BIT)
#define IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT (1<<IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT_BIT)
#define IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT (1<<IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT_BIT)
#define IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT	  (1<<IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT_BIT)

#ifndef __ASSEMBLY__

#include <linux/bcd.h>
#include <linux/spinlock.h>
#include <linux/efi.h>

#include <asm/pal.h>
#include <asm/fpu.h>

extern unsigned long sal_systab_phys;
extern spinlock_t sal_lock;

/* SAL spec _requires_ eight args for each call. */
#define __IA64_FW_CALL(entry,result,a0,a1,a2,a3,a4,a5,a6,a7)	\
	result = (*entry)(a0,a1,a2,a3,a4,a5,a6,a7)

# define IA64_FW_CALL(entry,result,args...) do {		\
	unsigned long __ia64_sc_flags;				\
	struct ia64_fpreg __ia64_sc_fr[6];			\
	ia64_save_scratch_fpregs(__ia64_sc_fr);			\
	spin_lock_irqsave(&sal_lock, __ia64_sc_flags);		\
	__IA64_FW_CALL(entry, result, args);			\
	spin_unlock_irqrestore(&sal_lock, __ia64_sc_flags);	\
	ia64_load_scratch_fpregs(__ia64_sc_fr);			\
} while (0)

# define SAL_CALL(result,args...)			\
	IA64_FW_CALL(ia64_sal, result, args);

# define SAL_CALL_NOLOCK(result,args...) do {		\
	unsigned long __ia64_scn_flags;			\
	struct ia64_fpreg __ia64_scn_fr[6];		\
	ia64_save_scratch_fpregs(__ia64_scn_fr);	\
	local_irq_save(__ia64_scn_flags);		\
	__IA64_FW_CALL(ia64_sal, result, args);		\
	local_irq_restore(__ia64_scn_flags);		\
	ia64_load_scratch_fpregs(__ia64_scn_fr);	\
} while (0)

# define SAL_CALL_REENTRANT(result,args...) do {	\
	struct ia64_fpreg __ia64_scs_fr[6];		\
	ia64_save_scratch_fpregs(__ia64_scs_fr);	\
	preempt_disable();				\
	__IA64_FW_CALL(ia64_sal, result, args);		\
	preempt_enable();				\
	ia64_load_scratch_fpregs(__ia64_scs_fr);	\
} while (0)

#define SAL_SET_VECTORS			0x01000000
#define SAL_GET_STATE_INFO		0x01000001
#define SAL_GET_STATE_INFO_SIZE		0x01000002
#define SAL_CLEAR_STATE_INFO		0x01000003
#define SAL_MC_RENDEZ			0x01000004
#define SAL_MC_SET_PARAMS		0x01000005
#define SAL_REGISTER_PHYSICAL_ADDR	0x01000006

#define SAL_CACHE_FLUSH			0x01000008
#define SAL_CACHE_INIT			0x01000009
#define SAL_PCI_CONFIG_READ		0x01000010
#define SAL_PCI_CONFIG_WRITE		0x01000011
#define SAL_FREQ_BASE			0x01000012
#define SAL_PHYSICAL_ID_INFO		0x01000013

#define SAL_UPDATE_PAL			0x01000020

struct ia64_sal_retval {
	/*
	 * A zero status value indicates call completed without error.
	 * A negative status value indicates reason of call failure.
	 * A positive status value indicates success but an
	 * informational value should be printed (e.g., "reboot for
	 * change to take effect").
	 */
	long status;
	unsigned long v0;
	unsigned long v1;
	unsigned long v2;
};

typedef struct ia64_sal_retval (*ia64_sal_handler) (u64, ...);

enum {
	SAL_FREQ_BASE_PLATFORM = 0,
	SAL_FREQ_BASE_INTERVAL_TIMER = 1,
	SAL_FREQ_BASE_REALTIME_CLOCK = 2
};

/*
 * The SAL system table is followed by a variable number of variable
 * length descriptors.  The structure of these descriptors follows
 * below.
 * The defininition follows SAL specs from July 2000
 */
struct ia64_sal_systab {
	u8 signature[4];	/* should be "SST_" */
	u32 size;		/* size of this table in bytes */
	u8 sal_rev_minor;
	u8 sal_rev_major;
	u16 entry_count;	/* # of entries in variable portion */
	u8 checksum;
	u8 reserved1[7];
	u8 sal_a_rev_minor;
	u8 sal_a_rev_major;
	u8 sal_b_rev_minor;
	u8 sal_b_rev_major;
	/* oem_id & product_id: terminating NUL is missing if string is exactly 32 bytes long. */
	u8 oem_id[32];
	u8 product_id[32];	/* ASCII product id  */
	u8 reserved2[8];
};

enum sal_systab_entry_type {
	SAL_DESC_ENTRY_POINT = 0,
	SAL_DESC_MEMORY = 1,
	SAL_DESC_PLATFORM_FEATURE = 2,
	SAL_DESC_TR = 3,
	SAL_DESC_PTC = 4,
	SAL_DESC_AP_WAKEUP = 5
};

/*
 * Entry type:	Size:
 *	0	48
 *	1	32
 *	2	16
 *	3	32
 *	4	16
 *	5	16
 */
#define SAL_DESC_SIZE(type)	"\060\040\020\040\020\020"[(unsigned) type]

typedef struct ia64_sal_desc_entry_point {
	u8 type;
	u8 reserved1[7];
	u64 pal_proc;
	u64 sal_proc;
	u64 gp;
	u8 reserved2[16];
}ia64_sal_desc_entry_point_t;

typedef struct ia64_sal_desc_memory {
	u8 type;
	u8 used_by_sal;	/* needs to be mapped for SAL? */
	u8 mem_attr;		/* current memory attribute setting */
	u8 access_rights;	/* access rights set up by SAL */
	u8 mem_attr_mask;	/* mask of supported memory attributes */
	u8 reserved1;
	u8 mem_type;		/* memory type */
	u8 mem_usage;		/* memory usage */
	u64 addr;		/* physical address of memory */
	u32 length;	/* length (multiple of 4KB pages) */
	u32 reserved2;
	u8 oem_reserved[8];
} ia64_sal_desc_memory_t;

typedef struct ia64_sal_desc_platform_feature {
	u8 type;
	u8 feature_mask;
	u8 reserved1[14];
} ia64_sal_desc_platform_feature_t;

typedef struct ia64_sal_desc_tr {
	u8 type;
	u8 tr_type;		/* 0 == instruction, 1 == data */
	u8 regnum;		/* translation register number */
	u8 reserved1[5];
	u64 addr;		/* virtual address of area covered */
	u64 page_size;		/* encoded page size */
	u8 reserved2[8];
} ia64_sal_desc_tr_t;

typedef struct ia64_sal_desc_ptc {
	u8 type;
	u8 reserved1[3];
	u32 num_domains;	/* # of coherence domains */
	u64 domain_info;	/* physical address of domain info table */
} ia64_sal_desc_ptc_t;

typedef struct ia64_sal_ptc_domain_info {
	u64 proc_count;		/* number of processors in domain */
	u64 proc_list;		/* physical address of LID array */
} ia64_sal_ptc_domain_info_t;

typedef struct ia64_sal_ptc_domain_proc_entry {
	u64 id  : 8;		/* id of processor */
	u64 eid : 8;		/* eid of processor */
} ia64_sal_ptc_domain_proc_entry_t;


#define IA64_SAL_AP_EXTERNAL_INT 0

typedef struct ia64_sal_desc_ap_wakeup {
	u8 type;
	u8 mechanism;		/* 0 == external interrupt */
	u8 reserved1[6];
	u64 vector;		/* interrupt vector in range 0x10-0xff */
} ia64_sal_desc_ap_wakeup_t ;

extern ia64_sal_handler ia64_sal;
extern struct ia64_sal_desc_ptc *ia64_ptc_domain_info;

extern unsigned short sal_revision;	/* supported SAL spec revision */
extern unsigned short sal_version;	/* SAL version; OEM dependent */
#define SAL_VERSION_CODE(major, minor) ((bin2bcd(major) << 8) | bin2bcd(minor))

extern const char *ia64_sal_strerror (long status);
extern void ia64_sal_init (struct ia64_sal_systab *sal_systab);

/* SAL information type encodings */
enum {
	SAL_INFO_TYPE_MCA  = 0,		/* Machine check abort information */
        SAL_INFO_TYPE_INIT = 1,		/* Init information */
        SAL_INFO_TYPE_CMC  = 2,		/* Corrected machine check information */
        SAL_INFO_TYPE_CPE  = 3		/* Corrected platform error information */
};

/* Encodings for machine check parameter types */
enum {
	SAL_MC_PARAM_RENDEZ_INT    = 1,	/* Rendezvous interrupt */
	SAL_MC_PARAM_RENDEZ_WAKEUP = 2,	/* Wakeup */
	SAL_MC_PARAM_CPE_INT	   = 3	/* Corrected Platform Error Int */
};

/* Encodings for rendezvous mechanisms */
enum {
	SAL_MC_PARAM_MECHANISM_INT = 1,	/* Use interrupt */
	SAL_MC_PARAM_MECHANISM_MEM = 2	/* Use memory synchronization variable*/
};

/* Encodings for vectors which can be registered by the OS with SAL */
enum {
	SAL_VECTOR_OS_MCA	  = 0,
	SAL_VECTOR_OS_INIT	  = 1,
	SAL_VECTOR_OS_BOOT_RENDEZ = 2
};

/* Encodings for mca_opt parameter sent to SAL_MC_SET_PARAMS */
#define	SAL_MC_PARAM_RZ_ALWAYS		0x1
#define	SAL_MC_PARAM_BINIT_ESCALATE	0x10

/*
 * Definition of the SAL Error Log from the SAL spec
 */

/* SAL Error Record Section GUID Definitions */
#define SAL_PROC_DEV_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf1, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_MEM_DEV_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf2, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_SEL_DEV_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf3, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_PCI_BUS_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf4, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_SMBIOS_DEV_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf5, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_PCI_COMP_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf6, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_SPECIFIC_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf7, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_HOST_CTLR_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf8, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SAL_PLAT_BUS_ERR_SECT_GUID  \
    EFI_GUID(0xe429faf9, 0x3cb7, 0x11d4, 0xbc, 0xa7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define PROCESSOR_ABSTRACTION_LAYER_OVERWRITE_GUID \
    EFI_GUID(0x6cb0a200, 0x893a, 0x11da, 0x96, 0xd2, 0x0, 0x10, 0x83, 0xff, \
		0xca, 0x4d)

#define MAX_CACHE_ERRORS	6
#define MAX_TLB_ERRORS		6
#define MAX_BUS_ERRORS		1

/* Definition of version  according to SAL spec for logging purposes */
typedef struct sal_log_revision {
	u8 minor;		/* BCD (0..99) */
	u8 major;		/* BCD (0..99) */
} sal_log_revision_t;

/* Definition of timestamp according to SAL spec for logging purposes */
typedef struct sal_log_timestamp {
	u8 slh_second;		/* Second (0..59) */
	u8 slh_minute;		/* Minute (0..59) */
	u8 slh_hour;		/* Hour (0..23) */
	u8 slh_reserved;
	u8 slh_day;		/* Day (1..31) */
	u8 slh_month;		/* Month (1..12) */
	u8 slh_year;		/* Year (00..99) */
	u8 slh_century;		/* Century (19, 20, 21, ...) */
} sal_log_timestamp_t;

/* Definition of log record  header structures */
typedef struct sal_log_record_header {
	u64 id;				/* Unique monotonically increasing ID */
	sal_log_revision_t revision;	/* Major and Minor revision of header */
	u8 severity;			/* Error Severity */
	u8 validation_bits;		/* 0: platform_guid, 1: !timestamp */
	u32 len;			/* Length of this error log in bytes */
	sal_log_timestamp_t timestamp;	/* Timestamp */
	efi_guid_t platform_guid;	/* Unique OEM Platform ID */
} sal_log_record_header_t;

#define sal_log_severity_recoverable	0
#define sal_log_severity_fatal		1
#define sal_log_severity_corrected	2

/*
 * Error Recovery Info (ERI) bit decode.  From SAL Spec section B.2.2 Table B-3
 * Error Section Error_Recovery_Info Field Definition.
 */
#define ERI_NOT_VALID		0x0	/* Error Recovery Field is not valid */
#define ERI_NOT_ACCESSIBLE	0x30	/* Resource not accessible */
#define ERI_CONTAINMENT_WARN	0x22	/* Corrupt data propagated */
#define ERI_UNCORRECTED_ERROR	0x20	/* Uncorrected error */
#define ERI_COMPONENT_RESET	0x24	/* Component must be reset */
#define ERI_CORR_ERROR_LOG	0x21	/* Corrected error, needs logging */
#define ERI_CORR_ERROR_THRESH	0x29	/* Corrected error threshold exceeded */

/* Definition of log section header structures */
typedef struct sal_log_sec_header {
    efi_guid_t guid;			/* Unique Section ID */
    sal_log_revision_t revision;	/* Major and Minor revision of Section */
    u8 error_recovery_info;		/* Platform error recovery status */
    u8 reserved;
    u32 len;				/* Section length */
} sal_log_section_hdr_t;

typedef struct sal_log_mod_error_info {
	struct {
		u64 check_info              : 1,
		    requestor_identifier    : 1,
		    responder_identifier    : 1,
		    target_identifier       : 1,
		    precise_ip              : 1,
		    reserved                : 59;
	} valid;
	u64 check_info;
	u64 requestor_identifier;
	u64 responder_identifier;
	u64 target_identifier;
	u64 precise_ip;
} sal_log_mod_error_info_t;

typedef struct sal_processor_static_info {
	struct {
		u64 minstate        : 1,
		    br              : 1,
		    cr              : 1,
		    ar              : 1,
		    rr              : 1,
		    fr              : 1,
		    reserved        : 58;
	} valid;
	struct pal_min_state_area min_state_area;
	u64 br[8];
	u64 cr[128];
	u64 ar[128];
	u64 rr[8];
	struct ia64_fpreg __attribute__ ((packed)) fr[128];
} sal_processor_static_info_t;

struct sal_cpuid_info {
	u64 regs[5];
	u64 reserved;
};

typedef struct sal_log_processor_info {
	sal_log_section_hdr_t header;
	struct {
		u64 proc_error_map      : 1,
		    proc_state_param    : 1,
		    proc_cr_lid         : 1,
		    psi_static_struct   : 1,
		    num_cache_check     : 4,
		    num_tlb_check       : 4,
		    num_bus_check       : 4,
		    num_reg_file_check  : 4,
		    num_ms_check        : 4,
		    cpuid_info          : 1,
		    reserved1           : 39;
	} valid;
	u64 proc_error_map;
	u64 proc_state_parameter;
	u64 proc_cr_lid;
	/*
	 * The rest of this structure consists of variable-length arrays, which can't be
	 * expressed in C.
	 */
	sal_log_mod_error_info_t info[];
	/*
	 * This is what the rest looked like if C supported variable-length arrays:
	 *
	 * sal_log_mod_error_info_t cache_check_info[.valid.num_cache_check];
	 * sal_log_mod_error_info_t tlb_check_info[.valid.num_tlb_check];
	 * sal_log_mod_error_info_t bus_check_info[.valid.num_bus_check];
	 * sal_log_mod_error_info_t reg_file_check_info[.valid.num_reg_file_check];
	 * sal_log_mod_error_info_t ms_check_info[.valid.num_ms_check];
	 * struct sal_cpuid_info cpuid_info;
	 * sal_processor_static_info_t processor_static_info;
	 */
} sal_log_processor_info_t;

/* Given a sal_log_processor_info_t pointer, return a pointer to the processor_static_info: */
#define SAL_LPI_PSI_INFO(l)									\
({	sal_log_processor_info_t *_l = (l);							\
	((sal_processor_static_info_t *)							\
	 ((char *) _l->info + ((_l->valid.num_cache_check + _l->valid.num_tlb_check		\
				+ _l->valid.num_bus_check + _l->valid.num_reg_file_check	\
				+ _l->valid.num_ms_check) * sizeof(sal_log_mod_error_info_t)	\
			       + sizeof(struct sal_cpuid_info))));				\
})

/* platform error log structures */

typedef struct sal_log_mem_dev_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 error_status    : 1,
		    physical_addr   : 1,
		    addr_mask       : 1,
		    node            : 1,
		    card            : 1,
		    module          : 1,
		    bank            : 1,
		    device          : 1,
		    row             : 1,
		    column          : 1,
		    bit_position    : 1,
		    requestor_id    : 1,
		    responder_id    : 1,
		    target_id       : 1,
		    bus_spec_data   : 1,
		    oem_id          : 1,
		    oem_data        : 1,
		    reserved        : 47;
	} valid;
	u64 error_status;
	u64 physical_addr;
	u64 addr_mask;
	u16 node;
	u16 card;
	u16 module;
	u16 bank;
	u16 device;
	u16 row;
	u16 column;
	u16 bit_position;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u64 bus_spec_data;
	u8 oem_id[16];
	u8 oem_data[1];			/* Variable length data */
} sal_log_mem_dev_err_info_t;

typedef struct sal_log_sel_dev_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 record_id       : 1,
		    record_type     : 1,
		    generator_id    : 1,
		    evm_rev         : 1,
		    sensor_type     : 1,
		    sensor_num      : 1,
		    event_dir       : 1,
		    event_data1     : 1,
		    event_data2     : 1,
		    event_data3     : 1,
		    reserved        : 54;
	} valid;
	u16 record_id;
	u8 record_type;
	u8 timestamp[4];
	u16 generator_id;
	u8 evm_rev;
	u8 sensor_type;
	u8 sensor_num;
	u8 event_dir;
	u8 event_data1;
	u8 event_data2;
	u8 event_data3;
} sal_log_sel_dev_err_info_t;

typedef struct sal_log_pci_bus_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 err_status      : 1,
		    err_type        : 1,
		    bus_id          : 1,
		    bus_address     : 1,
		    bus_data        : 1,
		    bus_cmd         : 1,
		    requestor_id    : 1,
		    responder_id    : 1,
		    target_id       : 1,
		    oem_data        : 1,
		    reserved        : 54;
	} valid;
	u64 err_status;
	u16 err_type;
	u16 bus_id;
	u32 reserved;
	u64 bus_address;
	u64 bus_data;
	u64 bus_cmd;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u8 oem_data[1];			/* Variable length data */
} sal_log_pci_bus_err_info_t;

typedef struct sal_log_smbios_dev_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 event_type      : 1,
		    length          : 1,
		    time_stamp      : 1,
		    data            : 1,
		    reserved1       : 60;
	} valid;
	u8 event_type;
	u8 length;
	u8 time_stamp[6];
	u8 data[1];			/* data of variable length, length == slsmb_length */
} sal_log_smbios_dev_err_info_t;

typedef struct sal_log_pci_comp_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 err_status      : 1,
		    comp_info       : 1,
		    num_mem_regs    : 1,
		    num_io_regs     : 1,
		    reg_data_pairs  : 1,
		    oem_data        : 1,
		    reserved        : 58;
	} valid;
	u64 err_status;
	struct {
		u16 vendor_id;
		u16 device_id;
		u8 class_code[3];
		u8 func_num;
		u8 dev_num;
		u8 bus_num;
		u8 seg_num;
		u8 reserved[5];
	} comp_info;
	u32 num_mem_regs;
	u32 num_io_regs;
	u64 reg_data_pairs[1];
	/*
	 * array of address/data register pairs is num_mem_regs + num_io_regs elements
	 * long.  Each array element consists of a u64 address followed by a u64 data
	 * value.  The oem_data array immediately follows the reg_data_pairs array
	 */
	u8 oem_data[1];			/* Variable length data */
} sal_log_pci_comp_err_info_t;

typedef struct sal_log_plat_specific_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 err_status      : 1,
		    guid            : 1,
		    oem_data        : 1,
		    reserved        : 61;
	} valid;
	u64 err_status;
	efi_guid_t guid;
	u8 oem_data[1];			/* platform specific variable length data */
} sal_log_plat_specific_err_info_t;

typedef struct sal_log_host_ctlr_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 err_status      : 1,
		    requestor_id    : 1,
		    responder_id    : 1,
		    target_id       : 1,
		    bus_spec_data   : 1,
		    oem_data        : 1,
		    reserved        : 58;
	} valid;
	u64 err_status;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u64 bus_spec_data;
	u8 oem_data[1];			/* Variable length OEM data */
} sal_log_host_ctlr_err_info_t;

typedef struct sal_log_plat_bus_err_info {
	sal_log_section_hdr_t header;
	struct {
		u64 err_status      : 1,
		    requestor_id    : 1,
		    responder_id    : 1,
		    target_id       : 1,
		    bus_spec_data   : 1,
		    oem_data        : 1,
		    reserved        : 58;
	} valid;
	u64 err_status;
	u64 requestor_id;
	u64 responder_id;
	u64 target_id;
	u64 bus_spec_data;
	u8 oem_data[1];			/* Variable length OEM data */
} sal_log_plat_bus_err_info_t;

/* Overall platform error section structure */
typedef union sal_log_platform_err_info {
	sal_log_mem_dev_err_info_t mem_dev_err;
	sal_log_sel_dev_err_info_t sel_dev_err;
	sal_log_pci_bus_err_info_t pci_bus_err;
	sal_log_smbios_dev_err_info_t smbios_dev_err;
	sal_log_pci_comp_err_info_t pci_comp_err;
	sal_log_plat_specific_err_info_t plat_specific_err;
	sal_log_host_ctlr_err_info_t host_ctlr_err;
	sal_log_plat_bus_err_info_t plat_bus_err;
} sal_log_platform_err_info_t;

/* SAL log over-all, multi-section error record structure (processor+platform) */
typedef struct err_rec {
	sal_log_record_header_t sal_elog_header;
	sal_log_processor_info_t proc_err;
	sal_log_platform_err_info_t plat_err;
	u8 oem_data_pad[1024];
} ia64_err_rec_t;

/*
 * Now define a couple of inline functions for improved type checking
 * and convenience.
 */

extern s64 ia64_sal_cache_flush (u64 cache_type);
extern void __init check_sal_cache_flush (void);

/* Initialize all the processor and platform level instruction and data caches */
static inline s64
ia64_sal_cache_init (void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_CACHE_INIT, 0, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}

/*
 * Clear the processor and platform information logged by SAL with respect to the machine
 * state at the time of MCA's, INITs, CMCs, or CPEs.
 */
static inline s64
ia64_sal_clear_state_info (u64 sal_info_type)
{
	struct ia64_sal_retval isrv;
	SAL_CALL_REENTRANT(isrv, SAL_CLEAR_STATE_INFO, sal_info_type, 0,
	              0, 0, 0, 0, 0);
	return isrv.status;
}


/* Get the processor and platform information logged by SAL with respect to the machine
 * state at the time of the MCAs, INITs, CMCs, or CPEs.
 */
static inline u64
ia64_sal_get_state_info (u64 sal_info_type, u64 *sal_info)
{
	struct ia64_sal_retval isrv;
	SAL_CALL_REENTRANT(isrv, SAL_GET_STATE_INFO, sal_info_type, 0,
	              sal_info, 0, 0, 0, 0);
	if (isrv.status)
		return 0;

	return isrv.v0;
}

/*
 * Get the maximum size of the information logged by SAL with respect to the machine state
 * at the time of MCAs, INITs, CMCs, or CPEs.
 */
static inline u64
ia64_sal_get_state_info_size (u64 sal_info_type)
{
	struct ia64_sal_retval isrv;
	SAL_CALL_REENTRANT(isrv, SAL_GET_STATE_INFO_SIZE, sal_info_type, 0,
	              0, 0, 0, 0, 0);
	if (isrv.status)
		return 0;
	return isrv.v0;
}

/*
 * Causes the processor to go into a spin loop within SAL where SAL awaits a wakeup from
 * the monarch processor.  Must not lock, because it will not return on any cpu until the
 * monarch processor sends a wake up.
 */
static inline s64
ia64_sal_mc_rendez (void)
{
	struct ia64_sal_retval isrv;
	SAL_CALL_NOLOCK(isrv, SAL_MC_RENDEZ, 0, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}

/*
 * Allow the OS to specify the interrupt number to be used by SAL to interrupt OS during
 * the machine check rendezvous sequence as well as the mechanism to wake up the
 * non-monarch processor at the end of machine check processing.
 * Returns the complete ia64_sal_retval because some calls return more than just a status
 * value.
 */
static inline struct ia64_sal_retval
ia64_sal_mc_set_params (u64 param_type, u64 i_or_m, u64 i_or_m_val, u64 timeout, u64 rz_always)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_MC_SET_PARAMS, param_type, i_or_m, i_or_m_val,
		 timeout, rz_always, 0, 0);
	return isrv;
}

/* Read from PCI configuration space */
static inline s64
ia64_sal_pci_config_read (u64 pci_config_addr, int type, u64 size, u64 *value)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_PCI_CONFIG_READ, pci_config_addr, size, type, 0, 0, 0, 0);
	if (value)
		*value = isrv.v0;
	return isrv.status;
}

/* Write to PCI configuration space */
static inline s64
ia64_sal_pci_config_write (u64 pci_config_addr, int type, u64 size, u64 value)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_PCI_CONFIG_WRITE, pci_config_addr, size, value,
	         type, 0, 0, 0);
	return isrv.status;
}

/*
 * Register physical addresses of locations needed by SAL when SAL procedures are invoked
 * in virtual mode.
 */
static inline s64
ia64_sal_register_physical_addr (u64 phys_entry, u64 phys_addr)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_REGISTER_PHYSICAL_ADDR, phys_entry, phys_addr,
	         0, 0, 0, 0, 0);
	return isrv.status;
}

/*
 * Register software dependent code locations within SAL. These locations are handlers or
 * entry points where SAL will pass control for the specified event. These event handlers
 * are for the bott rendezvous, MCAs and INIT scenarios.
 */
static inline s64
ia64_sal_set_vectors (u64 vector_type,
		      u64 handler_addr1, u64 gp1, u64 handler_len1,
		      u64 handler_addr2, u64 gp2, u64 handler_len2)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_SET_VECTORS, vector_type,
			handler_addr1, gp1, handler_len1,
			handler_addr2, gp2, handler_len2);

	return isrv.status;
}

/* Update the contents of PAL block in the non-volatile storage device */
static inline s64
ia64_sal_update_pal (u64 param_buf, u64 scratch_buf, u64 scratch_buf_size,
		     u64 *error_code, u64 *scratch_buf_size_needed)
{
	struct ia64_sal_retval isrv;
	SAL_CALL(isrv, SAL_UPDATE_PAL, param_buf, scratch_buf, scratch_buf_size,
	         0, 0, 0, 0);
	if (error_code)
		*error_code = isrv.v0;
	if (scratch_buf_size_needed)
		*scratch_buf_size_needed = isrv.v1;
	return isrv.status;
}

/* Get physical processor die mapping in the platform. */
static inline s64
ia64_sal_physical_id_info(u16 *splid)
{
	struct ia64_sal_retval isrv;

	if (sal_revision < SAL_VERSION_CODE(3,2))
		return -1;

	SAL_CALL(isrv, SAL_PHYSICAL_ID_INFO, 0, 0, 0, 0, 0, 0, 0);
	if (splid)
		*splid = isrv.v0;
	return isrv.status;
}

extern unsigned long sal_platform_features;

extern int (*salinfo_platform_oemdata)(const u8 *, u8 **, u64 *);

struct sal_ret_values {
	long r8; long r9; long r10; long r11;
};

#define IA64_SAL_OEMFUNC_MIN		0x02000000
#define IA64_SAL_OEMFUNC_MAX		0x03ffffff

extern int ia64_sal_oemcall(struct ia64_sal_retval *, u64, u64, u64, u64, u64,
			    u64, u64, u64);
extern int ia64_sal_oemcall_nolock(struct ia64_sal_retval *, u64, u64, u64,
				   u64, u64, u64, u64, u64);
extern int ia64_sal_oemcall_reentrant(struct ia64_sal_retval *, u64, u64, u64,
				      u64, u64, u64, u64, u64);
extern long
ia64_sal_freq_base (unsigned long which, unsigned long *ticks_per_second,
		    unsigned long *drift_info);
#ifdef CONFIG_HOTPLUG_CPU
/*
 * System Abstraction Layer Specification
 * Section 3.2.5.1: OS_BOOT_RENDEZ to SAL return State.
 * Note: region regs are stored first in head.S _start. Hence they must
 * stay up front.
 */
struct sal_to_os_boot {
	u64 rr[8];		/* Region Registers */
	u64 br[6];		/* br0:
				 * return addr into SAL boot rendez routine */
	u64 gr1;		/* SAL:GP */
	u64 gr12;		/* SAL:SP */
	u64 gr13;		/* SAL: Task Pointer */
	u64 fpsr;
	u64 pfs;
	u64 rnat;
	u64 unat;
	u64 bspstore;
	u64 dcr;		/* Default Control Register */
	u64 iva;
	u64 pta;
	u64 itv;
	u64 pmv;
	u64 cmcv;
	u64 lrr[2];
	u64 gr[4];
	u64 pr;			/* Predicate registers */
	u64 lc;			/* Loop Count */
	struct ia64_fpreg fp[20];
};

/*
 * Global array allocated for NR_CPUS at boot time
 */
extern struct sal_to_os_boot sal_boot_rendez_state[NR_CPUS];

extern void ia64_jump_to_sal(struct sal_to_os_boot *);
#endif

extern void ia64_sal_handler_init(void *entry_point, void *gpval);

#define PALO_MAX_TLB_PURGES	0xFFFF
#define PALO_SIG	"PALO"

struct palo_table {
	u8  signature[4];	/* Should be "PALO" */
	u32 length;
	u8  minor_revision;
	u8  major_revision;
	u8  checksum;
	u8  reserved1[5];
	u16 max_tlb_purges;
	u8  reserved2[6];
};

#define NPTCG_FROM_PAL			0
#define NPTCG_FROM_PALO			1
#define NPTCG_FROM_KERNEL_PARAMETER	2

#endif /* __ASSEMBLY__ */

#endif /* _ASM_IA64_SAL_H */
