/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Copyright(c) 2016-20 Intel Corporation.
 *
 * Intel Software Guard Extensions (SGX) support.
 */
#ifndef _ASM_X86_SGX_H
#define _ASM_X86_SGX_H

#include <linux/bits.h>
#include <linux/types.h>

/*
 * This file contains both data structures defined by SGX architecture and Linux
 * defined software data structures and functions.  The two should not be mixed
 * together for better readability.  The architectural definitions come first.
 */

/* The SGX specific CPUID function. */
#define SGX_CPUID		0x12
/* EPC enumeration. */
#define SGX_CPUID_EPC		2
/* An invalid EPC section, i.e. the end marker. */
#define SGX_CPUID_EPC_INVALID	0x0
/* A valid EPC section. */
#define SGX_CPUID_EPC_SECTION	0x1
/* The bitmask for the EPC section type. */
#define SGX_CPUID_EPC_MASK	GENMASK(3, 0)

enum sgx_encls_function {
	ECREATE	= 0x00,
	EADD	= 0x01,
	EINIT	= 0x02,
	EREMOVE	= 0x03,
	EDGBRD	= 0x04,
	EDGBWR	= 0x05,
	EEXTEND	= 0x06,
	ELDU	= 0x08,
	EBLOCK	= 0x09,
	EPA	= 0x0A,
	EWB	= 0x0B,
	ETRACK	= 0x0C,
	EAUG	= 0x0D,
	EMODPR	= 0x0E,
	EMODT	= 0x0F,
};

/**
 * SGX_ENCLS_FAULT_FLAG - flag signifying an ENCLS return code is a trapnr
 *
 * ENCLS has its own (positive value) error codes and also generates
 * ENCLS specific #GP and #PF faults.  And the ENCLS values get munged
 * with system error codes as everything percolates back up the stack.
 * Unfortunately (for us), we need to precisely identify each unique
 * error code, e.g. the action taken if EWB fails varies based on the
 * type of fault and on the exact SGX error code, i.e. we can't simply
 * convert all faults to -EFAULT.
 *
 * To make all three error types coexist, we set bit 30 to identify an
 * ENCLS fault.  Bit 31 (technically bits N:31) is used to differentiate
 * between positive (faults and SGX error codes) and negative (system
 * error codes) values.
 */
#define SGX_ENCLS_FAULT_FLAG 0x40000000

/**
 * enum sgx_return_code - The return code type for ENCLS, ENCLU and ENCLV
 * %SGX_EPC_PAGE_CONFLICT:	Page is being written by other ENCLS function.
 * %SGX_NOT_TRACKED:		Previous ETRACK's shootdown sequence has not
 *				been completed yet.
 * %SGX_CHILD_PRESENT		SECS has child pages present in the EPC.
 * %SGX_INVALID_EINITTOKEN:	EINITTOKEN is invalid and enclave signer's
 *				public key does not match IA32_SGXLEPUBKEYHASH.
 * %SGX_PAGE_NOT_MODIFIABLE:	The EPC page cannot be modified because it
 *				is in the PENDING or MODIFIED state.
 * %SGX_UNMASKED_EVENT:		An unmasked event, e.g. INTR, was received
 */
enum sgx_return_code {
	SGX_EPC_PAGE_CONFLICT		= 7,
	SGX_NOT_TRACKED			= 11,
	SGX_CHILD_PRESENT		= 13,
	SGX_INVALID_EINITTOKEN		= 16,
	SGX_PAGE_NOT_MODIFIABLE		= 20,
	SGX_UNMASKED_EVENT		= 128,
};

/* The modulus size for 3072-bit RSA keys. */
#define SGX_MODULUS_SIZE 384

/**
 * enum sgx_miscselect - additional information to an SSA frame
 * %SGX_MISC_EXINFO:	Report #PF or #GP to the SSA frame.
 *
 * Save State Area (SSA) is a stack inside the enclave used to store processor
 * state when an exception or interrupt occurs. This enum defines additional
 * information stored to an SSA frame.
 */
enum sgx_miscselect {
	SGX_MISC_EXINFO		= BIT(0),
};

#define SGX_MISC_RESERVED_MASK	GENMASK_ULL(63, 1)

#define SGX_SSA_GPRS_SIZE		184
#define SGX_SSA_MISC_EXINFO_SIZE	16

/**
 * enum sgx_attributes - the attributes field in &struct sgx_secs
 * %SGX_ATTR_INIT:		Enclave can be entered (is initialized).
 * %SGX_ATTR_DEBUG:		Allow ENCLS(EDBGRD) and ENCLS(EDBGWR).
 * %SGX_ATTR_MODE64BIT:		Tell that this a 64-bit enclave.
 * %SGX_ATTR_PROVISIONKEY:      Allow to use provisioning keys for remote
 *				attestation.
 * %SGX_ATTR_KSS:		Allow to use key separation and sharing (KSS).
 * %SGX_ATTR_EINITTOKENKEY:	Allow to use token signing key that is used to
 *				sign cryptographic tokens that can be passed to
 *				EINIT as an authorization to run an enclave.
 */
enum sgx_attribute {
	SGX_ATTR_INIT		= BIT(0),
	SGX_ATTR_DEBUG		= BIT(1),
	SGX_ATTR_MODE64BIT	= BIT(2),
	SGX_ATTR_PROVISIONKEY	= BIT(4),
	SGX_ATTR_EINITTOKENKEY	= BIT(5),
	SGX_ATTR_KSS		= BIT(7),
};

#define SGX_ATTR_RESERVED_MASK	(BIT_ULL(3) | BIT_ULL(6) | GENMASK_ULL(63, 8))

/**
 * struct sgx_secs - SGX Enclave Control Structure (SECS)
 * @size:		size of the address space
 * @base:		base address of the  address space
 * @ssa_frame_size:	size of an SSA frame
 * @miscselect:		additional information stored to an SSA frame
 * @attributes:		attributes for enclave
 * @xfrm:		XSave-Feature Request Mask (subset of XCR0)
 * @mrenclave:		SHA256-hash of the enclave contents
 * @mrsigner:		SHA256-hash of the public key used to sign the SIGSTRUCT
 * @config_id:		a user-defined value that is used in key derivation
 * @isv_prod_id:	a user-defined value that is used in key derivation
 * @isv_svn:		a user-defined value that is used in key derivation
 * @config_svn:		a user-defined value that is used in key derivation
 *
 * SGX Enclave Control Structure (SECS) is a special enclave page that is not
 * visible in the address space. In fact, this structure defines the address
 * range and other global attributes for the enclave and it is the first EPC
 * page created for any enclave. It is moved from a temporary buffer to an EPC
 * by the means of ENCLS[ECREATE] function.
 */
struct sgx_secs {
	u64 size;
	u64 base;
	u32 ssa_frame_size;
	u32 miscselect;
	u8  reserved1[24];
	u64 attributes;
	u64 xfrm;
	u32 mrenclave[8];
	u8  reserved2[32];
	u32 mrsigner[8];
	u8  reserved3[32];
	u32 config_id[16];
	u16 isv_prod_id;
	u16 isv_svn;
	u16 config_svn;
	u8  reserved4[3834];
} __packed;

/**
 * enum sgx_tcs_flags - execution flags for TCS
 * %SGX_TCS_DBGOPTIN:	If enabled allows single-stepping and breakpoints
 *			inside an enclave. It is cleared by EADD but can
 *			be set later with EDBGWR.
 */
enum sgx_tcs_flags {
	SGX_TCS_DBGOPTIN	= 0x01,
};

#define SGX_TCS_RESERVED_MASK	GENMASK_ULL(63, 1)
#define SGX_TCS_RESERVED_SIZE	4024

/**
 * struct sgx_tcs - Thread Control Structure (TCS)
 * @state:		used to mark an entered TCS
 * @flags:		execution flags (cleared by EADD)
 * @ssa_offset:		SSA stack offset relative to the enclave base
 * @ssa_index:		the current SSA frame index (cleard by EADD)
 * @nr_ssa_frames:	the number of frame in the SSA stack
 * @entry_offset:	entry point offset relative to the enclave base
 * @exit_addr:		address outside the enclave to exit on an exception or
 *			interrupt
 * @fs_offset:		offset relative to the enclave base to become FS
 *			segment inside the enclave
 * @gs_offset:		offset relative to the enclave base to become GS
 *			segment inside the enclave
 * @fs_limit:		size to become a new FS-limit (only 32-bit enclaves)
 * @gs_limit:		size to become a new GS-limit (only 32-bit enclaves)
 *
 * Thread Control Structure (TCS) is an enclave page visible in its address
 * space that defines an entry point inside the enclave. A thread enters inside
 * an enclave by supplying address of TCS to ENCLU(EENTER). A TCS can be entered
 * by only one thread at a time.
 */
struct sgx_tcs {
	u64 state;
	u64 flags;
	u64 ssa_offset;
	u32 ssa_index;
	u32 nr_ssa_frames;
	u64 entry_offset;
	u64 exit_addr;
	u64 fs_offset;
	u64 gs_offset;
	u32 fs_limit;
	u32 gs_limit;
	u8  reserved[SGX_TCS_RESERVED_SIZE];
} __packed;

/**
 * struct sgx_pageinfo - an enclave page descriptor
 * @addr:	address of the enclave page
 * @contents:	pointer to the page contents
 * @metadata:	pointer either to a SECINFO or PCMD instance
 * @secs:	address of the SECS page
 */
struct sgx_pageinfo {
	u64 addr;
	u64 contents;
	u64 metadata;
	u64 secs;
} __packed __aligned(32);


/**
 * enum sgx_page_type - bits in the SECINFO flags defining the page type
 * %SGX_PAGE_TYPE_SECS:	a SECS page
 * %SGX_PAGE_TYPE_TCS:	a TCS page
 * %SGX_PAGE_TYPE_REG:	a regular page
 * %SGX_PAGE_TYPE_VA:	a VA page
 * %SGX_PAGE_TYPE_TRIM:	a page in trimmed state
 *
 * Make sure when making changes to this enum that its values can still fit
 * in the bitfield within &struct sgx_encl_page
 */
enum sgx_page_type {
	SGX_PAGE_TYPE_SECS,
	SGX_PAGE_TYPE_TCS,
	SGX_PAGE_TYPE_REG,
	SGX_PAGE_TYPE_VA,
	SGX_PAGE_TYPE_TRIM,
};

#define SGX_NR_PAGE_TYPES	5
#define SGX_PAGE_TYPE_MASK	GENMASK(7, 0)

/**
 * enum sgx_secinfo_flags - the flags field in &struct sgx_secinfo
 * %SGX_SECINFO_R:	allow read
 * %SGX_SECINFO_W:	allow write
 * %SGX_SECINFO_X:	allow execution
 * %SGX_SECINFO_SECS:	a SECS page
 * %SGX_SECINFO_TCS:	a TCS page
 * %SGX_SECINFO_REG:	a regular page
 * %SGX_SECINFO_VA:	a VA page
 * %SGX_SECINFO_TRIM:	a page in trimmed state
 */
enum sgx_secinfo_flags {
	SGX_SECINFO_R			= BIT(0),
	SGX_SECINFO_W			= BIT(1),
	SGX_SECINFO_X			= BIT(2),
	SGX_SECINFO_SECS		= (SGX_PAGE_TYPE_SECS << 8),
	SGX_SECINFO_TCS			= (SGX_PAGE_TYPE_TCS << 8),
	SGX_SECINFO_REG			= (SGX_PAGE_TYPE_REG << 8),
	SGX_SECINFO_VA			= (SGX_PAGE_TYPE_VA << 8),
	SGX_SECINFO_TRIM		= (SGX_PAGE_TYPE_TRIM << 8),
};

#define SGX_SECINFO_PERMISSION_MASK	GENMASK_ULL(2, 0)
#define SGX_SECINFO_PAGE_TYPE_MASK	(SGX_PAGE_TYPE_MASK << 8)
#define SGX_SECINFO_RESERVED_MASK	~(SGX_SECINFO_PERMISSION_MASK | \
					  SGX_SECINFO_PAGE_TYPE_MASK)

/**
 * struct sgx_secinfo - describes attributes of an EPC page
 * @flags:	permissions and type
 *
 * Used together with ENCLS leaves that add or modify an EPC page to an
 * enclave to define page permissions and type.
 */
struct sgx_secinfo {
	u64 flags;
	u8  reserved[56];
} __packed __aligned(64);

#define SGX_PCMD_RESERVED_SIZE 40

/**
 * struct sgx_pcmd - Paging Crypto Metadata (PCMD)
 * @enclave_id:	enclave identifier
 * @mac:	MAC over PCMD, page contents and isvsvn
 *
 * PCMD is stored for every swapped page to the regular memory. When ELDU loads
 * the page back it recalculates the MAC by using a isvsvn number stored in a
 * VA page. Together these two structures bring integrity and rollback
 * protection.
 */
struct sgx_pcmd {
	struct sgx_secinfo secinfo;
	u64 enclave_id;
	u8  reserved[SGX_PCMD_RESERVED_SIZE];
	u8  mac[16];
} __packed __aligned(128);

#define SGX_SIGSTRUCT_RESERVED1_SIZE 84
#define SGX_SIGSTRUCT_RESERVED2_SIZE 20
#define SGX_SIGSTRUCT_RESERVED3_SIZE 32
#define SGX_SIGSTRUCT_RESERVED4_SIZE 12

/**
 * struct sgx_sigstruct_header -  defines author of the enclave
 * @header1:		constant byte string
 * @vendor:		must be either 0x0000 or 0x8086
 * @date:		YYYYMMDD in BCD
 * @header2:		constant byte string
 * @swdefined:		software defined value
 */
struct sgx_sigstruct_header {
	u64 header1[2];
	u32 vendor;
	u32 date;
	u64 header2[2];
	u32 swdefined;
	u8  reserved1[84];
} __packed;

/**
 * struct sgx_sigstruct_body - defines contents of the enclave
 * @miscselect:		additional information stored to an SSA frame
 * @misc_mask:		required miscselect in SECS
 * @attributes:		attributes for enclave
 * @xfrm:		XSave-Feature Request Mask (subset of XCR0)
 * @attributes_mask:	required attributes in SECS
 * @xfrm_mask:		required XFRM in SECS
 * @mrenclave:		SHA256-hash of the enclave contents
 * @isvprodid:		a user-defined value that is used in key derivation
 * @isvsvn:		a user-defined value that is used in key derivation
 */
struct sgx_sigstruct_body {
	u32 miscselect;
	u32 misc_mask;
	u8  reserved2[20];
	u64 attributes;
	u64 xfrm;
	u64 attributes_mask;
	u64 xfrm_mask;
	u8  mrenclave[32];
	u8  reserved3[32];
	u16 isvprodid;
	u16 isvsvn;
} __packed;

/**
 * struct sgx_sigstruct - an enclave signature
 * @header:		defines author of the enclave
 * @modulus:		the modulus of the public key
 * @exponent:		the exponent of the public key
 * @signature:		the signature calculated over the fields except modulus,
 * @body:		defines contents of the enclave
 * @q1:			a value used in RSA signature verification
 * @q2:			a value used in RSA signature verification
 *
 * Header and body are the parts that are actual signed. The remaining fields
 * define the signature of the enclave.
 */
struct sgx_sigstruct {
	struct sgx_sigstruct_header header;
	u8  modulus[SGX_MODULUS_SIZE];
	u32 exponent;
	u8  signature[SGX_MODULUS_SIZE];
	struct sgx_sigstruct_body body;
	u8  reserved4[12];
	u8  q1[SGX_MODULUS_SIZE];
	u8  q2[SGX_MODULUS_SIZE];
} __packed;

#define SGX_LAUNCH_TOKEN_SIZE 304

/*
 * Do not put any hardware-defined SGX structure representations below this
 * comment!
 */

#ifdef CONFIG_X86_SGX_KVM
int sgx_virt_ecreate(struct sgx_pageinfo *pageinfo, void __user *secs,
		     int *trapnr);
int sgx_virt_einit(void __user *sigstruct, void __user *token,
		   void __user *secs, u64 *lepubkeyhash, int *trapnr);
#endif

int sgx_set_attribute(unsigned long *allowed_attributes,
		      unsigned int attribute_fd);

#endif /* _ASM_X86_SGX_H */
