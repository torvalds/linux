/*
 *	Routines to identify additional cpu features that are scattered in
 *	cpuid space.
 */
#include <linux/cpu.h>

#include <asm/pat.h>
#include <asm/processor.h>

#include <asm/apic.h>

struct cpuid_bit {
	u16 feature;
	u8 reg;
	u8 bit;
	u32 level;
	u32 sub_leaf;
};

void init_scattered_cpuid_features(struct cpuinfo_x86 *c)
{
	u32 max_level;
	u32 regs[4];
	const struct cpuid_bit *cb;

	static const struct cpuid_bit cpuid_bits[] = {
		{ X86_FEATURE_INTEL_PT,		CPUID_EBX, 25, 0x00000007, 0 },
		{ X86_FEATURE_AVX512_4VNNIW,	CPUID_EDX,  2, 0x00000007, 0 },
		{ X86_FEATURE_AVX512_4FMAPS,	CPUID_EDX,  3, 0x00000007, 0 },
		{ X86_FEATURE_APERFMPERF,	CPUID_ECX,  0, 0x00000006, 0 },
		{ X86_FEATURE_EPB,		CPUID_ECX,  3, 0x00000006, 0 },
		{ X86_FEATURE_HW_PSTATE,	CPUID_EDX,  7, 0x80000007, 0 },
		{ X86_FEATURE_CPB,		CPUID_EDX,  9, 0x80000007, 0 },
		{ X86_FEATURE_PROC_FEEDBACK,	CPUID_EDX, 11, 0x80000007, 0 },
		{ 0, 0, 0, 0, 0 }
	};

	for (cb = cpuid_bits; cb->feature; cb++) {

		/* Verify that the level is valid */
		max_level = cpuid_eax(cb->level & 0xffff0000);
		if (max_level < cb->level ||
		    max_level > (cb->level | 0xffff))
			continue;

		cpuid_count(cb->level, cb->sub_leaf, &regs[CPUID_EAX],
			    &regs[CPUID_EBX], &regs[CPUID_ECX],
			    &regs[CPUID_EDX]);

		if (regs[cb->reg] & (1 << cb->bit))
			set_cpu_cap(c, cb->feature);
	}
}
