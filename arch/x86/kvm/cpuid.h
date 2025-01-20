/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_CPUID_H
#define ARCH_X86_KVM_CPUID_H

#include "reverse_cpuid.h"
#include <asm/cpu.h>
#include <asm/processor.h>
#include <uapi/asm/kvm_para.h>

extern u32 kvm_cpu_caps[NR_KVM_CPU_CAPS] __read_mostly;
void kvm_set_cpu_caps(void);

void kvm_vcpu_after_set_cpuid(struct kvm_vcpu *vcpu);
void kvm_update_cpuid_runtime(struct kvm_vcpu *vcpu);
struct kvm_cpuid_entry2 *kvm_find_cpuid_entry_index(struct kvm_vcpu *vcpu,
						    u32 function, u32 index);
struct kvm_cpuid_entry2 *kvm_find_cpuid_entry(struct kvm_vcpu *vcpu,
					      u32 function);
int kvm_dev_ioctl_get_cpuid(struct kvm_cpuid2 *cpuid,
			    struct kvm_cpuid_entry2 __user *entries,
			    unsigned int type);
int kvm_vcpu_ioctl_set_cpuid(struct kvm_vcpu *vcpu,
			     struct kvm_cpuid *cpuid,
			     struct kvm_cpuid_entry __user *entries);
int kvm_vcpu_ioctl_set_cpuid2(struct kvm_vcpu *vcpu,
			      struct kvm_cpuid2 *cpuid,
			      struct kvm_cpuid_entry2 __user *entries);
int kvm_vcpu_ioctl_get_cpuid2(struct kvm_vcpu *vcpu,
			      struct kvm_cpuid2 *cpuid,
			      struct kvm_cpuid_entry2 __user *entries);
bool kvm_cpuid(struct kvm_vcpu *vcpu, u32 *eax, u32 *ebx,
	       u32 *ecx, u32 *edx, bool exact_only);

void __init kvm_init_xstate_sizes(void);
u32 xstate_required_size(u64 xstate_bv, bool compacted);

int cpuid_query_maxphyaddr(struct kvm_vcpu *vcpu);
u64 kvm_vcpu_reserved_gpa_bits_raw(struct kvm_vcpu *vcpu);

static inline int cpuid_maxphyaddr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.maxphyaddr;
}

static inline bool kvm_vcpu_is_legal_gpa(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	return !(gpa & vcpu->arch.reserved_gpa_bits);
}

static inline bool kvm_vcpu_is_legal_aligned_gpa(struct kvm_vcpu *vcpu,
						 gpa_t gpa, gpa_t alignment)
{
	return IS_ALIGNED(gpa, alignment) && kvm_vcpu_is_legal_gpa(vcpu, gpa);
}

static inline bool page_address_valid(struct kvm_vcpu *vcpu, gpa_t gpa)
{
	return kvm_vcpu_is_legal_aligned_gpa(vcpu, gpa, PAGE_SIZE);
}

static __always_inline void cpuid_entry_override(struct kvm_cpuid_entry2 *entry,
						 unsigned int leaf)
{
	u32 *reg = cpuid_entry_get_reg(entry, leaf * 32);

	BUILD_BUG_ON(leaf >= ARRAY_SIZE(kvm_cpu_caps));
	*reg = kvm_cpu_caps[leaf];
}

static __always_inline bool guest_cpuid_has(struct kvm_vcpu *vcpu,
					    unsigned int x86_feature)
{
	const struct cpuid_reg cpuid = x86_feature_cpuid(x86_feature);
	struct kvm_cpuid_entry2 *entry;
	u32 *reg;

	/*
	 * XSAVES is a special snowflake.  Due to lack of a dedicated intercept
	 * on SVM, KVM must assume that XSAVES (and thus XRSTORS) is usable by
	 * the guest if the host supports XSAVES and *XSAVE* is exposed to the
	 * guest.  Because the guest can execute XSAVES and XRSTORS, i.e. can
	 * indirectly consume XSS, KVM must ensure XSS is zeroed when running
	 * the guest, i.e. must set XSAVES in vCPU capabilities.  But to reject
	 * direct XSS reads and writes (to minimize the virtualization hole and
	 * honor userspace's CPUID), KVM needs to check the raw guest CPUID,
	 * not KVM's view of guest capabilities.
	 *
	 * For all other features, guest capabilities are accurate.  Expand
	 * this allowlist with extreme vigilance.
	 */
	BUILD_BUG_ON(x86_feature != X86_FEATURE_XSAVES);

	entry = kvm_find_cpuid_entry_index(vcpu, cpuid.function, cpuid.index);
	if (!entry)
		return NULL;

	reg = __cpuid_entry_get_reg(entry, cpuid.reg);
	if (!reg)
		return false;

	return *reg & __feature_bit(x86_feature);
}

static inline bool guest_cpuid_is_amd_compatible(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.is_amd_compatible;
}

static inline bool guest_cpuid_is_intel_compatible(struct kvm_vcpu *vcpu)
{
	return !guest_cpuid_is_amd_compatible(vcpu);
}

static inline int guest_cpuid_family(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1);
	if (!best)
		return -1;

	return x86_family(best->eax);
}

static inline int guest_cpuid_model(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1);
	if (!best)
		return -1;

	return x86_model(best->eax);
}

static inline bool cpuid_model_is_consistent(struct kvm_vcpu *vcpu)
{
	return boot_cpu_data.x86_model == guest_cpuid_model(vcpu);
}

static inline int guest_cpuid_stepping(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1);
	if (!best)
		return -1;

	return x86_stepping(best->eax);
}

static inline bool supports_cpuid_fault(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.msr_platform_info & MSR_PLATFORM_INFO_CPUID_FAULT;
}

static inline bool cpuid_fault_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.msr_misc_features_enables &
		  MSR_MISC_FEATURES_ENABLES_CPUID_FAULT;
}

static __always_inline void kvm_cpu_cap_clear(unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	kvm_cpu_caps[x86_leaf] &= ~__feature_bit(x86_feature);
}

static __always_inline void kvm_cpu_cap_set(unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	kvm_cpu_caps[x86_leaf] |= __feature_bit(x86_feature);
}

static __always_inline u32 kvm_cpu_cap_get(unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	return kvm_cpu_caps[x86_leaf] & __feature_bit(x86_feature);
}

static __always_inline bool kvm_cpu_cap_has(unsigned int x86_feature)
{
	return !!kvm_cpu_cap_get(x86_feature);
}

static __always_inline void kvm_cpu_cap_check_and_set(unsigned int x86_feature)
{
	if (boot_cpu_has(x86_feature))
		kvm_cpu_cap_set(x86_feature);
}

static __always_inline bool guest_pv_has(struct kvm_vcpu *vcpu,
					 unsigned int kvm_feature)
{
	if (!vcpu->arch.pv_cpuid.enforce)
		return true;

	return vcpu->arch.pv_cpuid.features & (1u << kvm_feature);
}

static __always_inline void guest_cpu_cap_set(struct kvm_vcpu *vcpu,
					      unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	vcpu->arch.cpu_caps[x86_leaf] |= __feature_bit(x86_feature);
}

static __always_inline void guest_cpu_cap_clear(struct kvm_vcpu *vcpu,
						unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	vcpu->arch.cpu_caps[x86_leaf] &= ~__feature_bit(x86_feature);
}

static __always_inline void guest_cpu_cap_change(struct kvm_vcpu *vcpu,
						 unsigned int x86_feature,
						 bool guest_has_cap)
{
	if (guest_has_cap)
		guest_cpu_cap_set(vcpu, x86_feature);
	else
		guest_cpu_cap_clear(vcpu, x86_feature);
}

static __always_inline bool guest_cpu_cap_has(struct kvm_vcpu *vcpu,
					      unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	return vcpu->arch.cpu_caps[x86_leaf] & __feature_bit(x86_feature);
}

static inline bool kvm_vcpu_is_legal_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	if (guest_cpu_cap_has(vcpu, X86_FEATURE_LAM))
		cr3 &= ~(X86_CR3_LAM_U48 | X86_CR3_LAM_U57);

	return kvm_vcpu_is_legal_gpa(vcpu, cr3);
}

static inline bool guest_has_spec_ctrl_msr(struct kvm_vcpu *vcpu)
{
	return (guest_cpu_cap_has(vcpu, X86_FEATURE_SPEC_CTRL) ||
		guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_STIBP) ||
		guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_IBRS) ||
		guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_SSBD));
}

static inline bool guest_has_pred_cmd_msr(struct kvm_vcpu *vcpu)
{
	return (guest_cpu_cap_has(vcpu, X86_FEATURE_SPEC_CTRL) ||
		guest_cpu_cap_has(vcpu, X86_FEATURE_AMD_IBPB) ||
		guest_cpu_cap_has(vcpu, X86_FEATURE_SBPB));
}

#endif
