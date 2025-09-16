/* SPDX-License-Identifier: GPL-2.0 */
/* architectural constants/data definitions for TDX SEAMCALLs */

#ifndef __KVM_X86_TDX_ARCH_H
#define __KVM_X86_TDX_ARCH_H

#include <linux/types.h>

/* TDX control structure (TDR/TDCS/TDVPS) field access codes */
#define TDX_NON_ARCH			BIT_ULL(63)
#define TDX_CLASS_SHIFT			56
#define TDX_FIELD_MASK			GENMASK_ULL(31, 0)

#define __BUILD_TDX_FIELD(non_arch, class, field)	\
	(((non_arch) ? TDX_NON_ARCH : 0) |		\
	 ((u64)(class) << TDX_CLASS_SHIFT) |		\
	 ((u64)(field) & TDX_FIELD_MASK))

#define BUILD_TDX_FIELD(class, field)			\
	__BUILD_TDX_FIELD(false, (class), (field))

#define BUILD_TDX_FIELD_NON_ARCH(class, field)		\
	__BUILD_TDX_FIELD(true, (class), (field))


/* Class code for TD */
#define TD_CLASS_EXECUTION_CONTROLS	17ULL

/* Class code for TDVPS */
#define TDVPS_CLASS_VMCS		0ULL
#define TDVPS_CLASS_GUEST_GPR		16ULL
#define TDVPS_CLASS_OTHER_GUEST		17ULL
#define TDVPS_CLASS_MANAGEMENT		32ULL

enum tdx_tdcs_execution_control {
	TD_TDCS_EXEC_TSC_OFFSET = 10,
	TD_TDCS_EXEC_TSC_MULTIPLIER = 11,
};

enum tdx_vcpu_guest_other_state {
	TD_VCPU_STATE_DETAILS_NON_ARCH = 0x100,
};

#define TDX_VCPU_STATE_DETAILS_INTR_PENDING	BIT_ULL(0)

static inline bool tdx_vcpu_state_details_intr_pending(u64 vcpu_state_details)
{
	return !!(vcpu_state_details & TDX_VCPU_STATE_DETAILS_INTR_PENDING);
}

/* @field is any of enum tdx_tdcs_execution_control */
#define TDCS_EXEC(field)		BUILD_TDX_FIELD(TD_CLASS_EXECUTION_CONTROLS, (field))

/* @field is the VMCS field encoding */
#define TDVPS_VMCS(field)		BUILD_TDX_FIELD(TDVPS_CLASS_VMCS, (field))

/* @field is any of enum tdx_guest_other_state */
#define TDVPS_STATE(field)		BUILD_TDX_FIELD(TDVPS_CLASS_OTHER_GUEST, (field))
#define TDVPS_STATE_NON_ARCH(field)	BUILD_TDX_FIELD_NON_ARCH(TDVPS_CLASS_OTHER_GUEST, (field))

/* Management class fields */
enum tdx_vcpu_guest_management {
	TD_VCPU_PEND_NMI = 11,
};

/* @field is any of enum tdx_vcpu_guest_management */
#define TDVPS_MANAGEMENT(field)		BUILD_TDX_FIELD(TDVPS_CLASS_MANAGEMENT, (field))

#define TDX_EXTENDMR_CHUNKSIZE		256

struct tdx_cpuid_value {
	u32 eax;
	u32 ebx;
	u32 ecx;
	u32 edx;
} __packed;

#define TDX_TD_ATTR_DEBUG		BIT_ULL(0)
#define TDX_TD_ATTR_SEPT_VE_DISABLE	BIT_ULL(28)
#define TDX_TD_ATTR_PKS			BIT_ULL(30)
#define TDX_TD_ATTR_KL			BIT_ULL(31)
#define TDX_TD_ATTR_PERFMON		BIT_ULL(63)

#define TDX_EXT_EXIT_QUAL_TYPE_MASK	GENMASK(3, 0)
#define TDX_EXT_EXIT_QUAL_TYPE_PENDING_EPT_VIOLATION  6
/*
 * TD_PARAMS is provided as an input to TDH_MNG_INIT, the size of which is 1024B.
 */
struct td_params {
	u64 attributes;
	u64 xfam;
	u16 max_vcpus;
	u8 reserved0[6];

	u64 eptp_controls;
	u64 config_flags;
	u16 tsc_frequency;
	u8  reserved1[38];

	u64 mrconfigid[6];
	u64 mrowner[6];
	u64 mrownerconfig[6];
	u64 reserved2[4];

	union {
		DECLARE_FLEX_ARRAY(struct tdx_cpuid_value, cpuid_values);
		u8 reserved3[768];
	};
} __packed __aligned(1024);

/*
 * Guest uses MAX_PA for GPAW when set.
 * 0: GPA.SHARED bit is GPA[47]
 * 1: GPA.SHARED bit is GPA[51]
 */
#define TDX_CONFIG_FLAGS_MAX_GPAW      BIT_ULL(0)

/*
 * TDH.VP.ENTER, TDG.VP.VMCALL preserves RBP
 * 0: RBP can be used for TDG.VP.VMCALL input. RBP is clobbered.
 * 1: RBP can't be used for TDG.VP.VMCALL input. RBP is preserved.
 */
#define TDX_CONFIG_FLAGS_NO_RBP_MOD	BIT_ULL(2)


/*
 * TDX requires the frequency to be defined in units of 25MHz, which is the
 * frequency of the core crystal clock on TDX-capable platforms, i.e. the TDX
 * module can only program frequencies that are multiples of 25MHz.  The
 * frequency must be between 100mhz and 10ghz (inclusive).
 */
#define TDX_TSC_KHZ_TO_25MHZ(tsc_in_khz)	((tsc_in_khz) / (25 * 1000))
#define TDX_TSC_25MHZ_TO_KHZ(tsc_in_25mhz)	((tsc_in_25mhz) * (25 * 1000))
#define TDX_MIN_TSC_FREQUENCY_KHZ		(100 * 1000)
#define TDX_MAX_TSC_FREQUENCY_KHZ		(10 * 1000 * 1000)

/* Additional Secure EPT entry information */
#define TDX_SEPT_LEVEL_MASK		GENMASK_ULL(2, 0)
#define TDX_SEPT_STATE_MASK		GENMASK_ULL(15, 8)
#define TDX_SEPT_STATE_SHIFT		8

enum tdx_sept_entry_state {
	TDX_SEPT_FREE = 0,
	TDX_SEPT_BLOCKED = 1,
	TDX_SEPT_PENDING = 2,
	TDX_SEPT_PENDING_BLOCKED = 3,
	TDX_SEPT_PRESENT = 4,
};

static inline u8 tdx_get_sept_level(u64 sept_entry_info)
{
	return sept_entry_info & TDX_SEPT_LEVEL_MASK;
}

static inline u8 tdx_get_sept_state(u64 sept_entry_info)
{
	return (sept_entry_info & TDX_SEPT_STATE_MASK) >> TDX_SEPT_STATE_SHIFT;
}

#define MD_FIELD_ID_FEATURES0_TOPOLOGY_ENUM	BIT_ULL(20)

/*
 * TD scope metadata field ID.
 */
#define TD_MD_FIELD_ID_CPUID_VALUES		0x9410000300000000ULL

#endif /* __KVM_X86_TDX_ARCH_H */
