/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD Encrypted Register State Support
 *
 * Author: Joerg Roedel <jroedel@suse.de>
 */

#ifndef __ASM_ENCRYPTED_STATE_H
#define __ASM_ENCRYPTED_STATE_H

#include <linux/types.h>
#include <linux/sev-guest.h>

#include <asm/insn.h>
#include <asm/sev-common.h>
#include <asm/coco.h>
#include <asm/set_memory.h>

#define GHCB_PROTOCOL_MIN	1ULL
#define GHCB_PROTOCOL_MAX	2ULL
#define GHCB_DEFAULT_USAGE	0ULL

#define	VMGEXIT()			{ asm volatile("rep; vmmcall\n\r"); }

struct boot_params;

enum es_result {
	ES_OK,			/* All good */
	ES_UNSUPPORTED,		/* Requested operation not supported */
	ES_VMM_ERROR,		/* Unexpected state from the VMM */
	ES_DECODE_FAILED,	/* Instruction decoding failed */
	ES_EXCEPTION,		/* Instruction caused exception */
	ES_RETRY,		/* Retry instruction emulation */
};

struct es_fault_info {
	unsigned long vector;
	unsigned long error_code;
	unsigned long cr2;
};

struct pt_regs;

/* ES instruction emulation context */
struct es_em_ctxt {
	struct pt_regs *regs;
	struct insn insn;
	struct es_fault_info fi;
};

/*
 * AMD SEV Confidential computing blob structure. The structure is
 * defined in OVMF UEFI firmware header:
 * https://github.com/tianocore/edk2/blob/master/OvmfPkg/Include/Guid/ConfidentialComputingSevSnpBlob.h
 */
#define CC_BLOB_SEV_HDR_MAGIC	0x45444d41
struct cc_blob_sev_info {
	u32 magic;
	u16 version;
	u16 reserved;
	u64 secrets_phys;
	u32 secrets_len;
	u32 rsvd1;
	u64 cpuid_phys;
	u32 cpuid_len;
	u32 rsvd2;
} __packed;

void do_vc_no_ghcb(struct pt_regs *regs, unsigned long exit_code);

static inline u64 lower_bits(u64 val, unsigned int bits)
{
	u64 mask = (1ULL << bits) - 1;

	return (val & mask);
}

struct real_mode_header;
enum stack_type;

/* Early IDT entry points for #VC handler */
extern void vc_no_ghcb(void);
extern void vc_boot_ghcb(void);
extern bool handle_vc_boot_ghcb(struct pt_regs *regs);

/* PVALIDATE return codes */
#define PVALIDATE_FAIL_SIZEMISMATCH	6

/* Software defined (when rFlags.CF = 1) */
#define PVALIDATE_FAIL_NOUPDATE		255

/* RMUPDATE detected 4K page and 2MB page overlap. */
#define RMPUPDATE_FAIL_OVERLAP		4

/* PSMASH failed due to concurrent access by another CPU */
#define PSMASH_FAIL_INUSE		3

/* RMP page size */
#define RMP_PG_SIZE_4K			0
#define RMP_PG_SIZE_2M			1
#define RMP_TO_PG_LEVEL(level)		(((level) == RMP_PG_SIZE_4K) ? PG_LEVEL_4K : PG_LEVEL_2M)
#define PG_LEVEL_TO_RMP(level)		(((level) == PG_LEVEL_4K) ? RMP_PG_SIZE_4K : RMP_PG_SIZE_2M)

struct rmp_state {
	u64 gpa;
	u8 assigned;
	u8 pagesize;
	u8 immutable;
	u8 rsvd;
	u32 asid;
} __packed;

#define RMPADJUST_VMSA_PAGE_BIT		BIT(16)

/* SNP Guest message request */
struct snp_req_data {
	unsigned long req_gpa;
	unsigned long resp_gpa;
	unsigned long data_gpa;
	unsigned int data_npages;
};

#define MAX_AUTHTAG_LEN		32
#define AUTHTAG_LEN		16
#define AAD_LEN			48
#define MSG_HDR_VER		1

#define SNP_REQ_MAX_RETRY_DURATION      (60*HZ)
#define SNP_REQ_RETRY_DELAY             (2*HZ)

/* See SNP spec SNP_GUEST_REQUEST section for the structure */
enum msg_type {
	SNP_MSG_TYPE_INVALID = 0,
	SNP_MSG_CPUID_REQ,
	SNP_MSG_CPUID_RSP,
	SNP_MSG_KEY_REQ,
	SNP_MSG_KEY_RSP,
	SNP_MSG_REPORT_REQ,
	SNP_MSG_REPORT_RSP,
	SNP_MSG_EXPORT_REQ,
	SNP_MSG_EXPORT_RSP,
	SNP_MSG_IMPORT_REQ,
	SNP_MSG_IMPORT_RSP,
	SNP_MSG_ABSORB_REQ,
	SNP_MSG_ABSORB_RSP,
	SNP_MSG_VMRK_REQ,
	SNP_MSG_VMRK_RSP,

	SNP_MSG_TSC_INFO_REQ = 17,
	SNP_MSG_TSC_INFO_RSP,

	SNP_MSG_TYPE_MAX
};

enum aead_algo {
	SNP_AEAD_INVALID,
	SNP_AEAD_AES_256_GCM,
};

struct snp_guest_msg_hdr {
	u8 authtag[MAX_AUTHTAG_LEN];
	u64 msg_seqno;
	u8 rsvd1[8];
	u8 algo;
	u8 hdr_version;
	u16 hdr_sz;
	u8 msg_type;
	u8 msg_version;
	u16 msg_sz;
	u32 rsvd2;
	u8 msg_vmpck;
	u8 rsvd3[35];
} __packed;

struct snp_guest_msg {
	struct snp_guest_msg_hdr hdr;
	u8 payload[PAGE_SIZE - sizeof(struct snp_guest_msg_hdr)];
} __packed;

#define SNP_TSC_INFO_REQ_SZ	128

struct snp_tsc_info_req {
	u8 rsvd[SNP_TSC_INFO_REQ_SZ];
} __packed;

struct snp_tsc_info_resp {
	u32 status;
	u32 rsvd1;
	u64 tsc_scale;
	u64 tsc_offset;
	u32 tsc_factor;
	u8 rsvd2[100];
} __packed;

struct snp_guest_req {
	void *req_buf;
	size_t req_sz;

	void *resp_buf;
	size_t resp_sz;

	u64 exit_code;
	unsigned int vmpck_id;
	u8 msg_version;
	u8 msg_type;

	struct snp_req_data input;
	void *certs_data;
};

/*
 * The secrets page contains 96-bytes of reserved field that can be used by
 * the guest OS. The guest OS uses the area to save the message sequence
 * number for each VMPCK.
 *
 * See the GHCB spec section Secret page layout for the format for this area.
 */
struct secrets_os_area {
	u32 msg_seqno_0;
	u32 msg_seqno_1;
	u32 msg_seqno_2;
	u32 msg_seqno_3;
	u64 ap_jump_table_pa;
	u8 rsvd[40];
	u8 guest_usage[32];
} __packed;

#define VMPCK_KEY_LEN		32

/* See the SNP spec version 0.9 for secrets page format */
struct snp_secrets_page {
	u32 version;
	u32 imien	: 1,
	    rsvd1	: 31;
	u32 fms;
	u32 rsvd2;
	u8 gosvw[16];
	u8 vmpck0[VMPCK_KEY_LEN];
	u8 vmpck1[VMPCK_KEY_LEN];
	u8 vmpck2[VMPCK_KEY_LEN];
	u8 vmpck3[VMPCK_KEY_LEN];
	struct secrets_os_area os_area;

	u8 vmsa_tweak_bitmap[64];

	/* SVSM fields */
	u64 svsm_base;
	u64 svsm_size;
	u64 svsm_caa;
	u32 svsm_max_version;
	u8 svsm_guest_vmpl;
	u8 rsvd3[3];

	/* Remainder of page */
	u8 rsvd4[3744];
} __packed;

struct snp_msg_desc {
	/* request and response are in unencrypted memory */
	struct snp_guest_msg *request, *response;

	/*
	 * Avoid information leakage by double-buffering shared messages
	 * in fields that are in regular encrypted memory.
	 */
	struct snp_guest_msg secret_request, secret_response;

	struct snp_secrets_page *secrets;

	struct aesgcm_ctx *ctx;

	u32 *os_area_msg_seqno;
	u8 *vmpck;
	int vmpck_id;
};

/*
 * The SVSM Calling Area (CA) related structures.
 */
struct svsm_ca {
	u8 call_pending;
	u8 mem_available;
	u8 rsvd1[6];

	u8 svsm_buffer[PAGE_SIZE - 8];
};

#define SVSM_SUCCESS				0
#define SVSM_ERR_INCOMPLETE			0x80000000
#define SVSM_ERR_UNSUPPORTED_PROTOCOL		0x80000001
#define SVSM_ERR_UNSUPPORTED_CALL		0x80000002
#define SVSM_ERR_INVALID_ADDRESS		0x80000003
#define SVSM_ERR_INVALID_FORMAT			0x80000004
#define SVSM_ERR_INVALID_PARAMETER		0x80000005
#define SVSM_ERR_INVALID_REQUEST		0x80000006
#define SVSM_ERR_BUSY				0x80000007
#define SVSM_PVALIDATE_FAIL_SIZEMISMATCH	0x80001006

/*
 * The SVSM PVALIDATE related structures
 */
struct svsm_pvalidate_entry {
	u64 page_size		: 2,
	    action		: 1,
	    ignore_cf		: 1,
	    rsvd		: 8,
	    pfn			: 52;
};

struct svsm_pvalidate_call {
	u16 num_entries;
	u16 cur_index;

	u8 rsvd1[4];

	struct svsm_pvalidate_entry entry[];
};

#define SVSM_PVALIDATE_MAX_COUNT	((sizeof_field(struct svsm_ca, svsm_buffer) -		\
					  offsetof(struct svsm_pvalidate_call, entry)) /	\
					 sizeof(struct svsm_pvalidate_entry))

/*
 * The SVSM Attestation related structures
 */
struct svsm_loc_entry {
	u64 pa;
	u32 len;
	u8 rsvd[4];
};

struct svsm_attest_call {
	struct svsm_loc_entry report_buf;
	struct svsm_loc_entry nonce;
	struct svsm_loc_entry manifest_buf;
	struct svsm_loc_entry certificates_buf;

	/* For attesting a single service */
	u8 service_guid[16];
	u32 service_manifest_ver;
	u8 rsvd[4];
};

/* PTE descriptor used for the prepare_pte_enc() operations. */
struct pte_enc_desc {
	pte_t *kpte;
	int pte_level;
	bool encrypt;
	/* pfn of the kpte above */
	unsigned long pfn;
	/* physical address of @pfn */
	unsigned long pa;
	/* virtual address of @pfn */
	void *va;
	/* memory covered by the pte */
	unsigned long size;
	pgprot_t new_pgprot;
};

/*
 * SVSM protocol structure
 */
struct svsm_call {
	struct svsm_ca *caa;
	u64 rax;
	u64 rcx;
	u64 rdx;
	u64 r8;
	u64 r9;
	u64 rax_out;
	u64 rcx_out;
	u64 rdx_out;
	u64 r8_out;
	u64 r9_out;
};

#define SVSM_CORE_CALL(x)		((0ULL << 32) | (x))
#define SVSM_CORE_REMAP_CA		0
#define SVSM_CORE_PVALIDATE		1
#define SVSM_CORE_CREATE_VCPU		2
#define SVSM_CORE_DELETE_VCPU		3

#define SVSM_ATTEST_CALL(x)		((1ULL << 32) | (x))
#define SVSM_ATTEST_SERVICES		0
#define SVSM_ATTEST_SINGLE_SERVICE	1

#ifdef CONFIG_AMD_MEM_ENCRYPT

extern u8 snp_vmpl;

extern void __sev_es_ist_enter(struct pt_regs *regs);
extern void __sev_es_ist_exit(void);
static __always_inline void sev_es_ist_enter(struct pt_regs *regs)
{
	if (cc_vendor == CC_VENDOR_AMD &&
	    cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		__sev_es_ist_enter(regs);
}
static __always_inline void sev_es_ist_exit(void)
{
	if (cc_vendor == CC_VENDOR_AMD &&
	    cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		__sev_es_ist_exit();
}
extern int sev_es_setup_ap_jump_table(struct real_mode_header *rmh);
extern void __sev_es_nmi_complete(void);
static __always_inline void sev_es_nmi_complete(void)
{
	if (cc_vendor == CC_VENDOR_AMD &&
	    cc_platform_has(CC_ATTR_GUEST_STATE_ENCRYPT))
		__sev_es_nmi_complete();
}
extern int __init sev_es_efi_map_ghcbs(pgd_t *pgd);
extern void sev_enable(struct boot_params *bp);

/*
 * RMPADJUST modifies the RMP permissions of a page of a lesser-
 * privileged (numerically higher) VMPL.
 *
 * If the guest is running at a higher-privilege than the privilege
 * level the instruction is targeting, the instruction will succeed,
 * otherwise, it will fail.
 */
static inline int rmpadjust(unsigned long vaddr, bool rmp_psize, unsigned long attrs)
{
	int rc;

	/* "rmpadjust" mnemonic support in binutils 2.36 and newer */
	asm volatile(".byte 0xF3,0x0F,0x01,0xFE\n\t"
		     : "=a"(rc)
		     : "a"(vaddr), "c"(rmp_psize), "d"(attrs)
		     : "memory", "cc");

	return rc;
}
static inline int pvalidate(unsigned long vaddr, bool rmp_psize, bool validate)
{
	bool no_rmpupdate;
	int rc;

	/* "pvalidate" mnemonic support in binutils 2.36 and newer */
	asm volatile(".byte 0xF2, 0x0F, 0x01, 0xFF\n\t"
		     CC_SET(c)
		     : CC_OUT(c) (no_rmpupdate), "=a"(rc)
		     : "a"(vaddr), "c"(rmp_psize), "d"(validate)
		     : "memory", "cc");

	if (no_rmpupdate)
		return PVALIDATE_FAIL_NOUPDATE;

	return rc;
}

struct snp_guest_request_ioctl;

void setup_ghcb(void);
void early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr,
				  unsigned long npages);
void early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr,
				 unsigned long npages);
void snp_set_memory_shared(unsigned long vaddr, unsigned long npages);
void snp_set_memory_private(unsigned long vaddr, unsigned long npages);
void snp_set_wakeup_secondary_cpu(void);
bool snp_init(struct boot_params *bp);
void __noreturn snp_abort(void);
void snp_dmi_setup(void);
int snp_issue_svsm_attest_req(u64 call_id, struct svsm_call *call, struct svsm_attest_call *input);
void snp_accept_memory(phys_addr_t start, phys_addr_t end);
u64 snp_get_unsupported_features(u64 status);
u64 sev_get_status(void);
void sev_show_status(void);
void snp_update_svsm_ca(void);
int prepare_pte_enc(struct pte_enc_desc *d);
void set_pte_enc_mask(pte_t *kpte, unsigned long pfn, pgprot_t new_prot);
void snp_kexec_finish(void);
void snp_kexec_begin(void);

int snp_msg_init(struct snp_msg_desc *mdesc, int vmpck_id);
struct snp_msg_desc *snp_msg_alloc(void);
void snp_msg_free(struct snp_msg_desc *mdesc);
int snp_send_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
			   struct snp_guest_request_ioctl *rio);

void __init snp_secure_tsc_prepare(void);
void __init snp_secure_tsc_init(void);

#else	/* !CONFIG_AMD_MEM_ENCRYPT */

#define snp_vmpl 0
static inline void sev_es_ist_enter(struct pt_regs *regs) { }
static inline void sev_es_ist_exit(void) { }
static inline int sev_es_setup_ap_jump_table(struct real_mode_header *rmh) { return 0; }
static inline void sev_es_nmi_complete(void) { }
static inline int sev_es_efi_map_ghcbs(pgd_t *pgd) { return 0; }
static inline void sev_enable(struct boot_params *bp) { }
static inline int pvalidate(unsigned long vaddr, bool rmp_psize, bool validate) { return 0; }
static inline int rmpadjust(unsigned long vaddr, bool rmp_psize, unsigned long attrs) { return 0; }
static inline void setup_ghcb(void) { }
static inline void __init
early_snp_set_memory_private(unsigned long vaddr, unsigned long paddr, unsigned long npages) { }
static inline void __init
early_snp_set_memory_shared(unsigned long vaddr, unsigned long paddr, unsigned long npages) { }
static inline void snp_set_memory_shared(unsigned long vaddr, unsigned long npages) { }
static inline void snp_set_memory_private(unsigned long vaddr, unsigned long npages) { }
static inline void snp_set_wakeup_secondary_cpu(void) { }
static inline bool snp_init(struct boot_params *bp) { return false; }
static inline void snp_abort(void) { }
static inline void snp_dmi_setup(void) { }
static inline int snp_issue_svsm_attest_req(u64 call_id, struct svsm_call *call, struct svsm_attest_call *input)
{
	return -ENOTTY;
}
static inline void snp_accept_memory(phys_addr_t start, phys_addr_t end) { }
static inline u64 snp_get_unsupported_features(u64 status) { return 0; }
static inline u64 sev_get_status(void) { return 0; }
static inline void sev_show_status(void) { }
static inline void snp_update_svsm_ca(void) { }
static inline int prepare_pte_enc(struct pte_enc_desc *d) { return 0; }
static inline void set_pte_enc_mask(pte_t *kpte, unsigned long pfn, pgprot_t new_prot) { }
static inline void snp_kexec_finish(void) { }
static inline void snp_kexec_begin(void) { }
static inline int snp_msg_init(struct snp_msg_desc *mdesc, int vmpck_id) { return -1; }
static inline struct snp_msg_desc *snp_msg_alloc(void) { return NULL; }
static inline void snp_msg_free(struct snp_msg_desc *mdesc) { }
static inline int snp_send_guest_request(struct snp_msg_desc *mdesc, struct snp_guest_req *req,
					 struct snp_guest_request_ioctl *rio) { return -ENODEV; }
static inline void __init snp_secure_tsc_prepare(void) { }
static inline void __init snp_secure_tsc_init(void) { }

#endif	/* CONFIG_AMD_MEM_ENCRYPT */

#ifdef CONFIG_KVM_AMD_SEV
bool snp_probe_rmptable_info(void);
int snp_rmptable_init(void);
int snp_lookup_rmpentry(u64 pfn, bool *assigned, int *level);
void snp_dump_hva_rmpentry(unsigned long address);
int psmash(u64 pfn);
int rmp_make_private(u64 pfn, u64 gpa, enum pg_level level, u32 asid, bool immutable);
int rmp_make_shared(u64 pfn, enum pg_level level);
void snp_leak_pages(u64 pfn, unsigned int npages);
void kdump_sev_callback(void);
void snp_fixup_e820_tables(void);
#else
static inline bool snp_probe_rmptable_info(void) { return false; }
static inline int snp_rmptable_init(void) { return -ENOSYS; }
static inline int snp_lookup_rmpentry(u64 pfn, bool *assigned, int *level) { return -ENODEV; }
static inline void snp_dump_hva_rmpentry(unsigned long address) {}
static inline int psmash(u64 pfn) { return -ENODEV; }
static inline int rmp_make_private(u64 pfn, u64 gpa, enum pg_level level, u32 asid,
				   bool immutable)
{
	return -ENODEV;
}
static inline int rmp_make_shared(u64 pfn, enum pg_level level) { return -ENODEV; }
static inline void snp_leak_pages(u64 pfn, unsigned int npages) {}
static inline void kdump_sev_callback(void) { }
static inline void snp_fixup_e820_tables(void) {}
#endif

#endif
