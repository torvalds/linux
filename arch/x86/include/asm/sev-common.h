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
#define GHCB_MSR_AP_RESET_HOLD_REQ	0x006
#define GHCB_MSR_AP_RESET_HOLD_RESP	0x007

/* GHCB Hypervisor Feature Request/Response */
#define GHCB_MSR_HV_FT_REQ		0x080
#define GHCB_MSR_HV_FT_RESP		0x081

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

/* Linux-specific reason codes (used with reason set 1) */
#define SEV_TERM_SET_LINUX		1
#define GHCB_TERM_REGISTER		0	/* GHCB GPA registration failure */
#define GHCB_TERM_PSC			1	/* Page State Change failure */
#define GHCB_TERM_PVALIDATE		2	/* Pvalidate failure */

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

#endif
