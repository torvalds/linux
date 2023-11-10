/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#ifndef __ARM64_KVM_FIXED_CONFIG_H__
#define __ARM64_KVM_FIXED_CONFIG_H__

#include <asm/sysreg.h>

/*
 * This file contains definitions for features to be allowed or restricted for
 * guest virtual machines, depending on the mode KVM is running in and on the
 * type of guest that is running.
 *
 * The ALLOW masks represent a bitmask of feature fields that are allowed
 * without any restrictions as long as they are supported by the system.
 *
 * The RESTRICT_UNSIGNED masks, if present, represent unsigned fields for
 * features that are restricted to support at most the specified feature.
 *
 * If a feature field is not present in either, than it is not supported.
 *
 * The approach taken for protected VMs is to allow features that are:
 * - Needed by common Linux distributions (e.g., floating point)
 * - Trivial to support, e.g., supporting the feature does not introduce or
 * require tracking of additional state in KVM
 * - Cannot be trapped or prevent the guest from using anyway
 */

/*
 * Allow for protected VMs:
 * - Floating-point and Advanced SIMD
 * - Data Independent Timing
 * - Spectre/Meltdown Mitigation
 */
#define PVM_ID_AA64PFR0_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_FP) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_AdvSIMD) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_DIT) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3) \
	)

/*
 * Restrict to the following *unsigned* features for protected VMs:
 * - AArch64 guests only (no support for AArch32 guests):
 *	AArch32 adds complexity in trap handling, emulation, condition codes,
 *	etc...
 * - RAS (v1)
 *	Supported by KVM
 */
#define PVM_ID_AA64PFR0_RESTRICT_UNSIGNED (\
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL0), ID_AA64PFR0_EL1_ELx_64BIT_ONLY) | \
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL1), ID_AA64PFR0_EL1_ELx_64BIT_ONLY) | \
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL2), ID_AA64PFR0_EL1_ELx_64BIT_ONLY) | \
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_EL3), ID_AA64PFR0_EL1_ELx_64BIT_ONLY) | \
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_RAS), ID_AA64PFR0_EL1_RAS_IMP) \
	)

/*
 * Allow for protected VMs:
 * - Branch Target Identification
 * - Speculative Store Bypassing
 */
#define PVM_ID_AA64PFR1_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_BT) | \
	ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_SSBS) \
	)

/*
 * Allow for protected VMs:
 * - Mixed-endian
 * - Distinction between Secure and Non-secure Memory
 * - Mixed-endian at EL0 only
 * - Non-context synchronizing exception entry and exit
 */
#define PVM_ID_AA64MMFR0_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_BIGEND) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_SNSMEM) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_BIGENDEL0) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_EXS) \
	)

/*
 * Restrict to the following *unsigned* features for protected VMs:
 * - 40-bit IPA
 * - 16-bit ASID
 */
#define PVM_ID_AA64MMFR0_RESTRICT_UNSIGNED (\
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_PARANGE), ID_AA64MMFR0_EL1_PARANGE_40) | \
	FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64MMFR0_EL1_ASIDBITS), ID_AA64MMFR0_EL1_ASIDBITS_16) \
	)

/*
 * Allow for protected VMs:
 * - Hardware translation table updates to Access flag and Dirty state
 * - Number of VMID bits from CPU
 * - Hierarchical Permission Disables
 * - Privileged Access Never
 * - SError interrupt exceptions from speculative reads
 * - Enhanced Translation Synchronization
 */
#define PVM_ID_AA64MMFR1_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_HAFDBS) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_VMIDBits) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_HPDS) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_PAN) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_SpecSEI) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR1_EL1_ETS) \
	)

/*
 * Allow for protected VMs:
 * - Common not Private translations
 * - User Access Override
 * - IESB bit in the SCTLR_ELx registers
 * - Unaligned single-copy atomicity and atomic functions
 * - ESR_ELx.EC value on an exception by read access to feature ID space
 * - TTL field in address operations.
 * - Break-before-make sequences when changing translation block size
 * - E0PDx mechanism
 */
#define PVM_ID_AA64MMFR2_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_CnP) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_UAO) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_IESB) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_AT) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_IDS) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_TTL) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_BBM) | \
	ARM64_FEATURE_MASK(ID_AA64MMFR2_EL1_E0PD) \
	)

/*
 * No support for Scalable Vectors for protected VMs:
 *	Requires additional support from KVM, e.g., context-switching and
 *	trapping at EL2
 */
#define PVM_ID_AA64ZFR0_ALLOW (0ULL)

/*
 * No support for debug, including breakpoints, and watchpoints for protected
 * VMs:
 *	The Arm architecture mandates support for at least the Armv8 debug
 *	architecture, which would include at least 2 hardware breakpoints and
 *	watchpoints. Providing that support to protected guests adds
 *	considerable state and complexity. Therefore, the reserved value of 0 is
 *	used for debug-related fields.
 */
#define PVM_ID_AA64DFR0_ALLOW (0ULL)
#define PVM_ID_AA64DFR1_ALLOW (0ULL)

/*
 * No support for implementation defined features.
 */
#define PVM_ID_AA64AFR0_ALLOW (0ULL)
#define PVM_ID_AA64AFR1_ALLOW (0ULL)

/*
 * No restrictions on instructions implemented in AArch64.
 */
#define PVM_ID_AA64ISAR0_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_AES) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_SHA1) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_SHA2) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_CRC32) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_ATOMIC) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_RDM) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_SHA3) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_SM3) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_SM4) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_DP) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_FHM) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_TS) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_TLB) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR0_EL1_RNDR) \
	)

#define PVM_ID_AA64ISAR1_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_DPB) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_APA) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_API) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_JSCVT) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_FCMA) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_LRCPC) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPA) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPI) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_FRINTTS) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_SB) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_SPECRES) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_BF16) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_DGH) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_I8MM) \
	)

#define PVM_ID_AA64ISAR2_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_GPA3) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_APA3) | \
	ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_MOPS) \
	)

u64 pvm_read_id_reg(const struct kvm_vcpu *vcpu, u32 id);
bool kvm_handle_pvm_sysreg(struct kvm_vcpu *vcpu, u64 *exit_code);
bool kvm_handle_pvm_restricted(struct kvm_vcpu *vcpu, u64 *exit_code);
int kvm_check_pvm_sysreg_table(void);

#endif /* __ARM64_KVM_FIXED_CONFIG_H__ */
