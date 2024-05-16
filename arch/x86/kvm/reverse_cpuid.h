/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_X86_KVM_REVERSE_CPUID_H
#define ARCH_X86_KVM_REVERSE_CPUID_H

#include <uapi/asm/kvm.h>
#include <asm/cpufeature.h>
#include <asm/cpufeatures.h>

/*
 * Hardware-defined CPUID leafs that are either scattered by the kernel or are
 * unknown to the kernel, but need to be directly used by KVM.  Note, these
 * word values conflict with the kernel's "bug" caps, but KVM doesn't use those.
 */
enum kvm_only_cpuid_leafs {
	CPUID_12_EAX	 = NCAPINTS,
	CPUID_7_1_EDX,
	CPUID_8000_0007_EDX,
	CPUID_8000_0022_EAX,
	NR_KVM_CPU_CAPS,

	NKVMCAPINTS = NR_KVM_CPU_CAPS - NCAPINTS,
};

/*
 * Define a KVM-only feature flag.
 *
 * For features that are scattered by cpufeatures.h, __feature_translate() also
 * needs to be updated to translate the kernel-defined feature into the
 * KVM-defined feature.
 *
 * For features that are 100% KVM-only, i.e. not defined by cpufeatures.h,
 * forego the intermediate KVM_X86_FEATURE and directly define X86_FEATURE_* so
 * that X86_FEATURE_* can be used in KVM.  No __feature_translate() handling is
 * needed in this case.
 */
#define KVM_X86_FEATURE(w, f)		((w)*32 + (f))

/* Intel-defined SGX sub-features, CPUID level 0x12 (EAX). */
#define KVM_X86_FEATURE_SGX1		KVM_X86_FEATURE(CPUID_12_EAX, 0)
#define KVM_X86_FEATURE_SGX2		KVM_X86_FEATURE(CPUID_12_EAX, 1)
#define KVM_X86_FEATURE_SGX_EDECCSSA	KVM_X86_FEATURE(CPUID_12_EAX, 11)

/* Intel-defined sub-features, CPUID level 0x00000007:1 (EDX) */
#define X86_FEATURE_AVX_VNNI_INT8       KVM_X86_FEATURE(CPUID_7_1_EDX, 4)
#define X86_FEATURE_AVX_NE_CONVERT      KVM_X86_FEATURE(CPUID_7_1_EDX, 5)
#define X86_FEATURE_AMX_COMPLEX         KVM_X86_FEATURE(CPUID_7_1_EDX, 8)
#define X86_FEATURE_PREFETCHITI         KVM_X86_FEATURE(CPUID_7_1_EDX, 14)

/* CPUID level 0x80000007 (EDX). */
#define KVM_X86_FEATURE_CONSTANT_TSC	KVM_X86_FEATURE(CPUID_8000_0007_EDX, 8)

/* CPUID level 0x80000022 (EAX) */
#define KVM_X86_FEATURE_PERFMON_V2	KVM_X86_FEATURE(CPUID_8000_0022_EAX, 0)

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
	[CPUID_7_1_EAX]       = {         7, 1, CPUID_EAX},
	[CPUID_12_EAX]        = {0x00000012, 0, CPUID_EAX},
	[CPUID_8000_001F_EAX] = {0x8000001f, 0, CPUID_EAX},
	[CPUID_7_1_EDX]       = {         7, 1, CPUID_EDX},
	[CPUID_8000_0007_EDX] = {0x80000007, 0, CPUID_EDX},
	[CPUID_8000_0021_EAX] = {0x80000021, 0, CPUID_EAX},
	[CPUID_8000_0022_EAX] = {0x80000022, 0, CPUID_EAX},
};

/*
 * Reverse CPUID and its derivatives can only be used for hardware-defined
 * feature words, i.e. words whose bits directly correspond to a CPUID leaf.
 * Retrieving a feature bit or masking guest CPUID from a Linux-defined word
 * is nonsensical as the bit number/mask is an arbitrary software-defined value
 * and can't be used by KVM to query/control guest capabilities.  And obviously
 * the leaf being queried must have an entry in the lookup table.
 */
static __always_inline void reverse_cpuid_check(unsigned int x86_leaf)
{
	BUILD_BUG_ON(x86_leaf == CPUID_LNX_1);
	BUILD_BUG_ON(x86_leaf == CPUID_LNX_2);
	BUILD_BUG_ON(x86_leaf == CPUID_LNX_3);
	BUILD_BUG_ON(x86_leaf == CPUID_LNX_4);
	BUILD_BUG_ON(x86_leaf >= ARRAY_SIZE(reverse_cpuid));
	BUILD_BUG_ON(reverse_cpuid[x86_leaf].function == 0);
}

/*
 * Translate feature bits that are scattered in the kernel's cpufeatures word
 * into KVM feature words that align with hardware's definitions.
 */
static __always_inline u32 __feature_translate(int x86_feature)
{
	if (x86_feature == X86_FEATURE_SGX1)
		return KVM_X86_FEATURE_SGX1;
	else if (x86_feature == X86_FEATURE_SGX2)
		return KVM_X86_FEATURE_SGX2;
	else if (x86_feature == X86_FEATURE_SGX_EDECCSSA)
		return KVM_X86_FEATURE_SGX_EDECCSSA;
	else if (x86_feature == X86_FEATURE_CONSTANT_TSC)
		return KVM_X86_FEATURE_CONSTANT_TSC;
	else if (x86_feature == X86_FEATURE_PERFMON_V2)
		return KVM_X86_FEATURE_PERFMON_V2;

	return x86_feature;
}

static __always_inline u32 __feature_leaf(int x86_feature)
{
	return __feature_translate(x86_feature) / 32;
}

/*
 * Retrieve the bit mask from an X86_FEATURE_* definition.  Features contain
 * the hardware defined bit number (stored in bits 4:0) and a software defined
 * "word" (stored in bits 31:5).  The word is used to index into arrays of
 * bit masks that hold the per-cpu feature capabilities, e.g. this_cpu_has().
 */
static __always_inline u32 __feature_bit(int x86_feature)
{
	x86_feature = __feature_translate(x86_feature);

	reverse_cpuid_check(x86_feature / 32);
	return 1 << (x86_feature & 31);
}

#define feature_bit(name)  __feature_bit(X86_FEATURE_##name)

static __always_inline struct cpuid_reg x86_feature_cpuid(unsigned int x86_feature)
{
	unsigned int x86_leaf = __feature_leaf(x86_feature);

	reverse_cpuid_check(x86_leaf);
	return reverse_cpuid[x86_leaf];
}

static __always_inline u32 *__cpuid_entry_get_reg(struct kvm_cpuid_entry2 *entry,
						  u32 reg)
{
	switch (reg) {
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

static __always_inline u32 *cpuid_entry_get_reg(struct kvm_cpuid_entry2 *entry,
						unsigned int x86_feature)
{
	const struct cpuid_reg cpuid = x86_feature_cpuid(x86_feature);

	return __cpuid_entry_get_reg(entry, cpuid.reg);
}

static __always_inline u32 cpuid_entry_get(struct kvm_cpuid_entry2 *entry,
					   unsigned int x86_feature)
{
	u32 *reg = cpuid_entry_get_reg(entry, x86_feature);

	return *reg & __feature_bit(x86_feature);
}

static __always_inline bool cpuid_entry_has(struct kvm_cpuid_entry2 *entry,
					    unsigned int x86_feature)
{
	return cpuid_entry_get(entry, x86_feature);
}

static __always_inline void cpuid_entry_clear(struct kvm_cpuid_entry2 *entry,
					      unsigned int x86_feature)
{
	u32 *reg = cpuid_entry_get_reg(entry, x86_feature);

	*reg &= ~__feature_bit(x86_feature);
}

static __always_inline void cpuid_entry_set(struct kvm_cpuid_entry2 *entry,
					    unsigned int x86_feature)
{
	u32 *reg = cpuid_entry_get_reg(entry, x86_feature);

	*reg |= __feature_bit(x86_feature);
}

static __always_inline void cpuid_entry_change(struct kvm_cpuid_entry2 *entry,
					       unsigned int x86_feature,
					       bool set)
{
	u32 *reg = cpuid_entry_get_reg(entry, x86_feature);

	/*
	 * Open coded instead of using cpuid_entry_{clear,set}() to coerce the
	 * compiler into using CMOV instead of Jcc when possible.
	 */
	if (set)
		*reg |= __feature_bit(x86_feature);
	else
		*reg &= ~__feature_bit(x86_feature);
}

#endif /* ARCH_X86_KVM_REVERSE_CPUID_H */
