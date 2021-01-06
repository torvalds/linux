/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_CPUID_H
#define ARCH_X86_KVM_CPUID_H

#include "x86.h"
#include <asm/cpu.h>
#include <asm/processor.h>

int kvm_update_cpuid(struct kvm_vcpu *vcpu);
bool kvm_mpx_supported(void);
struct kvm_cpuid_entry2 *kvm_find_cpuid_entry(struct kvm_vcpu *vcpu,
					      u32 function, u32 index);
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
	       u32 *ecx, u32 *edx, bool check_limit);

int cpuid_query_maxphyaddr(struct kvm_vcpu *vcpu);

static inline int cpuid_maxphyaddr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.maxphyaddr;
}

struct cpuid_reg {
	u32 function;
	u32 index;
	int reg;
};

static const struct cpuid_reg reverse_cpuid[] = {
	[CPUID_1_EDX]         = {         1, 0, CPUID_EDX},
	[CPUID_8000_0001_EDX] = {0x80000001, 0, CPUID_EDX},
	[CPUID_8086_0001_EDX] = {0x80860001, 0, CPUID_EDX},
	[CPUID_1_ECX]         = {         1, 0, CPUID_ECX},
	[CPUID_C000_0001_EDX] = {0xc0000001, 0, CPUID_EDX},
	[CPUID_8000_0001_ECX] = {0x80000001, 0, CPUID_ECX},
	[CPUID_7_0_EBX]       = {         7, 0, CPUID_EBX},
	[CPUID_D_1_EAX]       = {       0xd, 1, CPUID_EAX},
	[CPUID_8000_0008_EBX] = {0x80000008, 0, CPUID_EBX},
	[CPUID_6_EAX]         = {         6, 0, CPUID_EAX},
	[CPUID_8000_000A_EDX] = {0x8000000a, 0, CPUID_EDX},
	[CPUID_7_ECX]         = {         7, 0, CPUID_ECX},
	[CPUID_8000_0007_EBX] = {0x80000007, 0, CPUID_EBX},
	[CPUID_7_EDX]         = {         7, 0, CPUID_EDX},
};

static __always_inline struct cpuid_reg x86_feature_cpuid(unsigned x86_feature)
{
	unsigned x86_leaf = x86_feature / 32;

	BUILD_BUG_ON(x86_leaf >= ARRAY_SIZE(reverse_cpuid));
	BUILD_BUG_ON(reverse_cpuid[x86_leaf].function == 0);

	return reverse_cpuid[x86_leaf];
}

static __always_inline int *guest_cpuid_get_register(struct kvm_vcpu *vcpu, unsigned x86_feature)
{
	struct kvm_cpuid_entry2 *entry;
	const struct cpuid_reg cpuid = x86_feature_cpuid(x86_feature);

	entry = kvm_find_cpuid_entry(vcpu, cpuid.function, cpuid.index);
	if (!entry)
		return NULL;

	switch (cpuid.reg) {
	case CPUID_EAX:
		return &entry->eax;
	case CPUID_EBX:
		return &entry->ebx;
	case CPUID_ECX:
		return &entry->ecx;
	case CPUID_EDX:
		return &entry->edx;
	default:
		BUILD_BUG();
		return NULL;
	}
}

static __always_inline bool guest_cpuid_has(struct kvm_vcpu *vcpu, unsigned x86_feature)
{
	int *reg;

	if (x86_feature == X86_FEATURE_XSAVE &&
			!static_cpu_has(X86_FEATURE_XSAVE))
		return false;

	reg = guest_cpuid_get_register(vcpu, x86_feature);
	if (!reg)
		return false;

	return *reg & bit(x86_feature);
}

static __always_inline void guest_cpuid_clear(struct kvm_vcpu *vcpu, unsigned x86_feature)
{
	int *reg;

	reg = guest_cpuid_get_register(vcpu, x86_feature);
	if (reg)
		*reg &= ~bit(x86_feature);
}

static inline bool guest_cpuid_is_amd(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0, 0);
	return best && best->ebx == X86EMUL_CPUID_VENDOR_AuthenticAMD_ebx;
}

static inline int guest_cpuid_family(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1, 0);
	if (!best)
		return -1;

	return x86_family(best->eax);
}

static inline int guest_cpuid_model(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1, 0);
	if (!best)
		return -1;

	return x86_model(best->eax);
}

static inline int guest_cpuid_stepping(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x1, 0);
	if (!best)
		return -1;

	return x86_stepping(best->eax);
}

static inline bool guest_has_spec_ctrl_msr(struct kvm_vcpu *vcpu)
{
	return (guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL) ||
		guest_cpuid_has(vcpu, X86_FEATURE_AMD_STIBP) ||
		guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBRS) ||
		guest_cpuid_has(vcpu, X86_FEATURE_AMD_SSBD));
}

static inline bool guest_has_pred_cmd_msr(struct kvm_vcpu *vcpu)
{
	return (guest_cpuid_has(vcpu, X86_FEATURE_SPEC_CTRL) ||
		guest_cpuid_has(vcpu, X86_FEATURE_AMD_IBPB));
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

#endif
