/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AMD SEV header common between the guest and the hypervisor.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 */

#ifndef __ASM_X86_SEV_COMMON_H
#define __ASM_X86_SEV_COMMON_H

#define GHCB_MSR_INFO_POS		0
#define GHCB_DATA_LOW			12
#define GHCB_MSR_INFO_MASK		(BIT_ULL(GHCB_DATA_LOW) - 1)

#define GHCB_DATA(v)			\
	(((unsigned long)(v) & ~GHCB_MSR_INFO_MASK) >> GHCB_DATA_LOW)

/* SEV Information Request/Response */
#define GHCB_MSR_SEV_INFO_RESP		0x001
#define GHCB_MSR_SEV_INFO_REQ		0x002

#define GHCB_MSR_SEV_INFO(_max, _min, _cbit)	\
	/* GHCBData[63:48] */			\
	((((_max) & 0xffff) << 48) |		\
	 /* GHCBData[47:32] */			\
	 (((_min) & 0xffff) << 32) |		\
	 /* GHCBData[31:24] */			\
	 (((_cbit) & 0xff)  << 24) |		\
	 GHCB_MSR_SEV_INFO_RESP)

#define GHCB_MSR_INFO(v)		((v) & 0xfffUL)
#define GHCB_MSR_PROTO_MAX(v)		(((v) >> 48) & 0xffff)
#define GHCB_MSR_PROTO_MIN(v)		(((v) >> 32) & 0xffff)

/* CPUID Request/Response */
#define GHCB_MSR_CPUID_REQ		0x004
#define GHCB_MSR_CPUID_RESP		0x005
#define GHCB_MSR_CPUID_FUNC_POS		32
#define GHCB_MSR_CPUID_FUNC_MASK	0xffffffff
#define GHCB_MSR_CPUID_VALUE_POS	32
#define GHCB_MSR_CPUID_VALUE_MASK	0xffffffff
#define GHCB_MSR_CPUID_REG_POS		30
#define GHCB_MSR_CPUID_REG_MASK		0x3
#define GHCB_CPUID_REQ_EAX		0
#define GHCB_CPUID_REQ_EBX		1
#define GHCB_CPUID_REQ_ECX		2
#define GHCB_CPUID_REQ_EDX		3
#define GHCB_CPUID_REQ(fn, reg)				\
	/* GHCBData[11:0] */				\
	(GHCB_MSR_CPUID_REQ |				\
	/* GHCBData[31:12] */				\
	(((unsigned long)(reg) & 0x3) << 30) |		\
	/* GHCBData[63:32] */				\
	(((unsigned long)fn) << 32))

/* AP Reset Hold */
#define GHCB_MSR_AP_RESET_HOLD_REQ		0x006
#define GHCB_MSR_AP_RESET_HOLD_RESP		0x007
#define GHCB_MSR_AP_RESET_HOLD_RESULT_POS	12
#define GHCB_MSR_AP_RESET_HOLD_RESULT_MASK	GENMASK_ULL(51, 0)

/* Preferred GHCB GPA Request */
#define GHCB_MSR_PREF_GPA_REQ		0x010
#define GHCB_MSR_GPA_VALUE_POS		12
#define GHCB_MSR_GPA_VALUE_MASK		GENMASK_ULL(51, 0)

#define GHCB_MSR_PREF_GPA_RESP		0x011
#define GHCB_MSR_PREF_GPA_NONE		0xfffffffffffff

/* GHCB GPA Register */
#define GHCB_MSR_REG_GPA_REQ		0x012
#define GHCB_MSR_REG_GPA_REQ_VAL(v)			\
	/* GHCBData[63:12] */				\
	(((u64)((v) & GENMASK_ULL(51, 0)) << 12) |	\
	/* GHCBData[11:0] */				\
	GHCB_MSR_REG_GPA_REQ)

#define GHCB_MSR_REG_GPA_RESP		0x013
#define GHCB_MSR_REG_GPA_RESP_VAL(v)			\
	/* GHCBData[63:12] */				\
	(((u64)(v) & GENMASK_ULL(63, 12)) >> 12)

/*
 * SNP Page State Change Operation
 *
 * GHCBData[55:52] - Page operation:
 *   0x0001	Page assignment, Private
 *   0x0002	Page assignment, Shared
 */
enum psc_op {
	SNP_PAGE_STATE_PRIVATE = 1,
	SNP_PAGE_STATE_SHARED,
};

#define GHCB_MSR_PSC_REQ		0x014
#define GHCB_MSR_PSC_REQ_GFN(gfn, op)			\
	/* GHCBData[55:52] */				\
	(((u64)((op) & 0xf) << 52) |			\
	/* GHCBData[51:12] */				\
	((u64)((gfn) & GENMASK_ULL(39, 0)) << 12) |	\
	/* GHCBData[11:0] */				\
	GHCB_MSR_PSC_REQ)

#define GHCB_MSR_PSC_REQ_TO_GFN(msr) (((msr) & GENMASK_ULL(51, 12)) >> 12)
#define GHCB_MSR_PSC_REQ_TO_OP(msr) (((msr) & GENMASK_ULL(55, 52)) >> 52)

#define GHCB_MSR_PSC_RESP		0x015
#define GHCB_MSR_PSC_RESP_VAL(val)			\
	/* GHCBData[63:32] */				\
	(((u64)(val) & GENMASK_ULL(63, 32)) >> 32)

/* Set highest bit as a generic error response */
#define GHCB_MSR_PSC_RESP_ERROR (BIT_ULL(63) | GHCB_MSR_PSC_RESP)

/* GHCB Run at VMPL Request/Response */
#define GHCB_MSR_VMPL_REQ		0x016
#define GHCB_MSR_VMPL_REQ_LEVEL(v)			\
	/* GHCBData[39:32] */				\
	(((u64)(v) & GENMASK_ULL(7, 0) << 32) |		\
	/* GHCBDdata[11:0] */				\
	GHCB_MSR_VMPL_REQ)

#define GHCB_MSR_VMPL_RESP		0x017
#define GHCB_MSR_VMPL_RESP_VAL(v)			\
	/* GHCBData[63:32] */				\
	(((u64)(v) & GENMASK_ULL(63, 32)) >> 32)

/* GHCB Hypervisor Feature Request/Response */
#define GHCB_MSR_HV_FT_REQ		0x080
#define GHCB_MSR_HV_FT_RESP		0x081
#define GHCB_MSR_HV_FT_POS		12
#define GHCB_MSR_HV_FT_MASK		GENMASK_ULL(51, 0)
#define GHCB_MSR_HV_FT_RESP_VAL(v)			\
	/* GHCBData[63:12] */				\
	(((u64)(v) & GENMASK_ULL(63, 12)) >> 12)

#define GHCB_HV_FT_SNP			BIT_ULL(0)
#define GHCB_HV_FT_SNP_AP_CREATION	BIT_ULL(1)
#define GHCB_HV_FT_SNP_MULTI_VMPL	BIT_ULL(5)

/*
 * SNP Page State Change NAE event
 *   The VMGEXIT_PSC_MAX_ENTRY determines the size of the PSC structure, which
 *   is a local stack variable in set_pages_state(). Do not increase this value
 *   without evaluating the impact to stack usage.
 *
 *   Use VMGEXIT_PSC_MAX_COUNT in cases where the actual GHCB-defined max value
 *   is needed, such as when processing GHCB requests on the hypervisor side.
 */
#define VMGEXIT_PSC_MAX_ENTRY		64
#define VMGEXIT_PSC_MAX_COUNT		253

#define VMGEXIT_PSC_ERROR_GENERIC	(0x100UL << 32)
#define VMGEXIT_PSC_ERROR_INVALID_HDR	((1UL << 32) | 1)
#define VMGEXIT_PSC_ERROR_INVALID_ENTRY	((1UL << 32) | 2)

#define VMGEXIT_PSC_OP_PRIVATE		1
#define VMGEXIT_PSC_OP_SHARED		2

struct psc_hdr {
	u16 cur_entry;
	u16 end_entry;
	u32 reserved;
} __packed;

struct psc_entry {
	u64	cur_page	: 12,
		gfn		: 40,
		operation	: 4,
		pagesize	: 1,
		reserved	: 7;
} __packed;

struct snp_psc_desc {
	struct psc_hdr hdr;
	struct psc_entry entries[VMGEXIT_PSC_MAX_ENTRY];
} __packed;

#define GHCB_MSR_TERM_REQ		0x100
#define GHCB_MSR_TERM_REASON_SET_POS	12
#define GHCB_MSR_TERM_REASON_SET_MASK	0xf
#define GHCB_MSR_TERM_REASON_POS	16
#define GHCB_MSR_TERM_REASON_MASK	0xff

#define GHCB_SEV_TERM_REASON(reason_set, reason_val)	\
	/* GHCBData[15:12] */				\
	(((((u64)reason_set) &  0xf) << 12) |		\
	 /* GHCBData[23:16] */				\
	((((u64)reason_val) & 0xff) << 16))

/* Error codes from reason set 0 */
#define SEV_TERM_SET_GEN		0
#define GHCB_SEV_ES_GEN_REQ		0
#define GHCB_SEV_ES_PROT_UNSUPPORTED	1
#define GHCB_SNP_UNSUPPORTED		2

/* Linux-specific reason codes (used with reason set 1) */
#define SEV_TERM_SET_LINUX		1
#define GHCB_TERM_REGISTER		0	/* GHCB GPA registration failure */
#define GHCB_TERM_PSC			1	/* Page State Change failure */
#define GHCB_TERM_PVALIDATE		2	/* Pvalidate failure */
#define GHCB_TERM_NOT_VMPL0		3	/* SNP guest is not running at VMPL-0 */
#define GHCB_TERM_CPUID			4	/* CPUID-validation failure */
#define GHCB_TERM_CPUID_HV		5	/* CPUID failure during hypervisor fallback */
#define GHCB_TERM_SECRETS_PAGE		6	/* Secrets page failure */
#define GHCB_TERM_NO_SVSM		7	/* SVSM is not advertised in the secrets page */
#define GHCB_TERM_SVSM_VMPL0		8	/* SVSM is present but has set VMPL to 0 */
#define GHCB_TERM_SVSM_CAA		9	/* SVSM is present but CAA is not page aligned */

#define GHCB_RESP_CODE(v)		((v) & GHCB_MSR_INFO_MASK)

/*
 * Error codes related to GHCB input that can be communicated back to the guest
 * by setting the lower 32-bits of the GHCB SW_EXITINFO1 field to 2.
 */
#define GHCB_ERR_NOT_REGISTERED		1
#define GHCB_ERR_INVALID_USAGE		2
#define GHCB_ERR_INVALID_SCRATCH_AREA	3
#define GHCB_ERR_MISSING_INPUT		4
#define GHCB_ERR_INVALID_INPUT		5
#define GHCB_ERR_INVALID_EVENT		6

struct sev_config {
	__u64 debug		: 1,

	      /*
	       * Indicates when the per-CPU GHCB has been created and registered
	       * and thus can be used by the BSP instead of the early boot GHCB.
	       *
	       * For APs, the per-CPU GHCB is created before they are started
	       * and registered upon startup, so this flag can be used globally
	       * for the BSP and APs.
	       */
	      ghcbs_initialized	: 1,

	      /*
	       * Indicates when the per-CPU SVSM CA is to be used instead of the
	       * boot SVSM CA.
	       *
	       * For APs, the per-CPU SVSM CA is created as part of the AP
	       * bringup, so this flag can be used globally for the BSP and APs.
	       */
	      use_cas		: 1,

	      __reserved	: 61;
};

extern struct sev_config sev_cfg;

#endif
