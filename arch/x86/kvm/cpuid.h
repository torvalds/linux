#ifndef ARCH_X86_KVM_CPUID_H
#define ARCH_X86_KVM_CPUID_H

#include "x86.h"

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
void kvm_cpuid(struct kvm_vcpu *vcpu, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);

int cpuid_query_maxphyaddr(struct kvm_vcpu *vcpu);

static inline int cpuid_maxphyaddr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.maxphyaddr;
}

static inline bool guest_cpuid_has_xsave(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	if (!static_cpu_has(X86_FEATURE_XSAVE))
		return false;

	best = kvm_find_cpuid_entry(vcpu, 1, 0);
	return best && (best->ecx & bit(X86_FEATURE_XSAVE));
}

static inline bool guest_cpuid_has_mtrr(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 1, 0);
	return best && (best->edx & bit(X86_FEATURE_MTRR));
}

static inline bool guest_cpuid_has_tsc_adjust(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_TSC_ADJUST));
}

static inline bool guest_cpuid_has_smep(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_SMEP));
}

static inline bool guest_cpuid_has_smap(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_SMAP));
}

static inline bool guest_cpuid_has_fsgsbase(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_FSGSBASE));
}

static inline bool guest_cpuid_has_longmode(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
	return best && (best->edx & bit(X86_FEATURE_LM));
}

static inline bool guest_cpuid_has_osvw(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
	return best && (best->ecx & bit(X86_FEATURE_OSVW));
}

static inline bool guest_cpuid_has_pcid(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 1, 0);
	return best && (best->ecx & bit(X86_FEATURE_PCID));
}

static inline bool guest_cpuid_has_x2apic(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 1, 0);
	return best && (best->ecx & bit(X86_FEATURE_X2APIC));
}

static inline bool guest_cpuid_is_amd(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0, 0);
	return best && best->ebx == X86EMUL_CPUID_VENDOR_AuthenticAMD_ebx;
}

static inline bool guest_cpuid_has_gbpages(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
	return best && (best->edx & bit(X86_FEATURE_GBPAGES));
}

static inline bool guest_cpuid_has_rtm(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_RTM));
}

static inline bool guest_cpuid_has_pcommit(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_PCOMMIT));
}

static inline bool guest_cpuid_has_mpx(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->ebx & bit(X86_FEATURE_MPX));
}

static inline bool guest_cpuid_has_rdtscp(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000001, 0);
	return best && (best->edx & bit(X86_FEATURE_RDTSCP));
}

static inline bool guest_cpuid_has_ibpb(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000008, 0);
	if (best && (best->ebx & bit(X86_FEATURE_AMD_IBPB)))
		return true;
	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->edx & bit(X86_FEATURE_SPEC_CTRL));
}

static inline bool guest_cpuid_has_spec_ctrl(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000008, 0);
	if (best && (best->ebx & bit(X86_FEATURE_AMD_IBRS)))
		return true;
	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->edx & (bit(X86_FEATURE_SPEC_CTRL) | bit(X86_FEATURE_SPEC_CTRL_SSBD)));
}

static inline bool guest_cpuid_has_arch_capabilities(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 7, 0);
	return best && (best->edx & bit(X86_FEATURE_ARCH_CAPABILITIES));
}

static inline bool guest_cpuid_has_virt_ssbd(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x80000008, 0);
	return best && (best->ebx & bit(X86_FEATURE_VIRT_SSBD));
}



/*
 * NRIPS is provided through cpuidfn 0x8000000a.edx bit 3
 */
#define BIT_NRIPS	3

static inline bool guest_cpuid_has_nrips(struct kvm_vcpu *vcpu)
{
	struct kvm_cpuid_entry2 *best;

	best = kvm_find_cpuid_entry(vcpu, 0x8000000a, 0);

	/*
	 * NRIPS is a scattered cpuid feature, so we can't use
	 * X86_FEATURE_NRIPS here (X86_FEATURE_NRIPS would be bit
	 * position 8, not 3).
	 */
	return best && (best->edx & bit(BIT_NRIPS));
}
#undef BIT_NRIPS

#endif
