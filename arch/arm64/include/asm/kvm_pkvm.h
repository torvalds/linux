// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 - Google LLC
 * Author: Quentin Perret <qperret@google.com>
 * Author: Fuad Tabba <tabba@google.com>
 */
#ifndef __ARM64_KVM_PKVM_H__
#define __ARM64_KVM_PKVM_H__

#include <linux/arm_ffa.h>
#include <linux/memblock.h>
#include <linux/scatterlist.h>
#include <asm/kvm_pgtable.h>
#include <asm/sysreg.h>

/*
 * Stores the sve state for the host in protected mode.
 */
struct kvm_host_sve_state {
	u64 zcr_el1;

	/*
	 * Ordering is important since __sve_save_state/__sve_restore_state
	 * relies on it.
	 */
	u32 fpsr;
	u32 fpcr;

	/* Must be SVE_VQ_BYTES (128 bit) aligned. */
	char sve_regs[];
};

/* Maximum number of VMs that can co-exist under pKVM. */
#define KVM_MAX_PVMS 255

#define HYP_MEMBLOCK_REGIONS 128
#define PVMFW_INVALID_LOAD_ADDR	(-1)

int pkvm_vm_ioctl_enable_cap(struct kvm *kvm,struct kvm_enable_cap *cap);
int pkvm_init_host_vm(struct kvm *kvm, unsigned long type);
int pkvm_create_hyp_vm(struct kvm *kvm);
void pkvm_destroy_hyp_vm(struct kvm *kvm);
void pkvm_host_reclaim_page(struct kvm *host_kvm, phys_addr_t ipa);

/*
 * Definitions for features to be allowed or restricted for guest virtual
 * machines, depending on the mode KVM is running in and on the type of guest
 * that is running.
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
 * - GICv3(+) system register interface
 * - Data Independent Timing
 * - Spectre/Meltdown Mitigation
 */
#define PVM_ID_AA64PFR0_ALLOW (\
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_FP) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_AdvSIMD) | \
	ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_GIC) | \
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
	ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_APA3) \
	)

/*
 * Returns the maximum number of breakpoints supported for protected VMs.
 */
static inline int pkvm_get_max_brps(void)
{
	int num = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_BRPs),
			    PVM_ID_AA64DFR0_ALLOW);

	/*
	 * If breakpoints are supported, the maximum number is 1 + the field.
	 * Otherwise, return 0, which is not compliant with the architecture,
	 * but is reserved and is used here to indicate no debug support.
	 */
	return num ? num + 1 : 0;
}

/*
 * Returns the maximum number of watchpoints supported for protected VMs.
 */
static inline int pkvm_get_max_wrps(void)
{
	int num = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_WRPs),
			    PVM_ID_AA64DFR0_ALLOW);

	return num ? num + 1 : 0;
}

enum pkvm_moveable_reg_type {
	PKVM_MREG_MEMORY,
	PKVM_MREG_PROTECTED_RANGE,
};

struct pkvm_moveable_reg {
	phys_addr_t start;
	u64 size;
	enum pkvm_moveable_reg_type type;
};

#define PKVM_NR_MOVEABLE_REGS 512
extern struct pkvm_moveable_reg kvm_nvhe_sym(pkvm_moveable_regs)[];
extern unsigned int kvm_nvhe_sym(pkvm_moveable_regs_nr);

extern struct memblock_region kvm_nvhe_sym(hyp_memory)[];
extern unsigned int kvm_nvhe_sym(hyp_memblock_nr);

extern phys_addr_t kvm_nvhe_sym(pvmfw_base);
extern phys_addr_t kvm_nvhe_sym(pvmfw_size);

static inline unsigned long
hyp_vmemmap_memblock_size(struct memblock_region *reg, size_t vmemmap_entry_size)
{
	unsigned long nr_pages = reg->size >> PAGE_SHIFT;
	unsigned long start, end;

	start = (reg->base >> PAGE_SHIFT) * vmemmap_entry_size;
	end = start + nr_pages * vmemmap_entry_size;
	start = ALIGN_DOWN(start, PAGE_SIZE);
	end = ALIGN(end, PAGE_SIZE);

	return end - start;
}

static inline unsigned long hyp_vmemmap_pages(size_t vmemmap_entry_size)
{
	unsigned long res = 0, i;

	for (i = 0; i < kvm_nvhe_sym(hyp_memblock_nr); i++) {
		res += hyp_vmemmap_memblock_size(&kvm_nvhe_sym(hyp_memory)[i],
						 vmemmap_entry_size);
	}

	return res >> PAGE_SHIFT;
}

static inline unsigned long hyp_vm_table_pages(void)
{
	return PAGE_ALIGN(KVM_MAX_PVMS * sizeof(void *)) >> PAGE_SHIFT;
}

static inline unsigned long __hyp_pgtable_max_pages(unsigned long nr_pages)
{
	unsigned long total = 0, i;

	/* Provision the worst case scenario */
	for (i = 0; i < KVM_PGTABLE_MAX_LEVELS; i++) {
		nr_pages = DIV_ROUND_UP(nr_pages, PTRS_PER_PTE);
		total += nr_pages;
	}

	return total;
}

static inline unsigned long __hyp_pgtable_moveable_regs_pages(void)
{
	unsigned long res = 0, i;

	/* Cover all of moveable regions with page-granularity */
	for (i = 0; i < kvm_nvhe_sym(pkvm_moveable_regs_nr); i++) {
		struct pkvm_moveable_reg *reg = &kvm_nvhe_sym(pkvm_moveable_regs)[i];
		res += __hyp_pgtable_max_pages(reg->size >> PAGE_SHIFT);
	}

	return res;
}

#define __PKVM_PRIVATE_SZ SZ_1G

static inline unsigned long hyp_s1_pgtable_pages(void)
{
	unsigned long res;

	res = __hyp_pgtable_moveable_regs_pages();

	res += __hyp_pgtable_max_pages(__PKVM_PRIVATE_SZ >> PAGE_SHIFT);

	return res;
}

static inline unsigned long host_s2_pgtable_pages(void)
{
	unsigned long res;

	/*
	 * Include an extra 16 pages to safely upper-bound the worst case of
	 * concatenated pgds.
	 */
	res = __hyp_pgtable_moveable_regs_pages() + 16;

	/* Allow 1 GiB for non-moveable regions */
	res += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT);

	return res;
}

#define KVM_FFA_MBOX_NR_PAGES	1

/*
 * Maximum number of consitutents allowed in a descriptor. This number is
 * arbitrary, see comment below on SG_MAX_SEGMENTS in hyp_ffa_proxy_pages().
 */
#define KVM_FFA_MAX_NR_CONSTITUENTS	4096

static inline unsigned long hyp_ffa_proxy_pages(void)
{
	size_t desc_max;

	/*
	 * SG_MAX_SEGMENTS is supposed to bound the number of elements in an
	 * sglist, which should match the number of consituents in the
	 * corresponding FFA descriptor. As such, the EL2 buffer needs to be
	 * large enough to hold a descriptor with SG_MAX_SEGMENTS consituents
	 * at least. But the kernel's DMA code doesn't enforce the limit, and
	 * it is sometimes abused, so let's allow larger descriptors and hope
	 * for the best.
	 */
	BUILD_BUG_ON(KVM_FFA_MAX_NR_CONSTITUENTS < SG_MAX_SEGMENTS);

	/*
	 * The hypervisor FFA proxy needs enough memory to buffer a fragmented
	 * descriptor returned from EL3 in response to a RETRIEVE_REQ call.
	 */
	desc_max = sizeof(struct ffa_mem_region) +
		   sizeof(struct ffa_mem_region_attributes) +
		   sizeof(struct ffa_composite_mem_region) +
		   KVM_FFA_MAX_NR_CONSTITUENTS * sizeof(struct ffa_mem_region_addr_range);

	/* Plus a page each for the hypervisor's RX and TX mailboxes. */
	return (2 * KVM_FFA_MBOX_NR_PAGES) + DIV_ROUND_UP(desc_max, PAGE_SIZE);
}

static inline size_t pkvm_host_fp_state_size(void)
{
	if (system_supports_sve())
		return size_add(sizeof(struct kvm_host_sve_state),
		       SVE_SIG_REGS_SIZE(sve_vq_from_vl(kvm_host_sve_max_vl)));
	else
		return sizeof(struct user_fpsimd_state);
}

#endif	/* __ARM64_KVM_PKVM_H__ */
