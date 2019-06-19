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

/* Please keep the leaf sorted by cpuid_bit.level for faster search. */
static const struct cpuid_bit cpuid_bits[] = {
	{ X86_FEATURE_APERFMPERF,       CPUID_ECX,  0, 0x00000006, 0 },
	{ X86_FEATURE_EPB,		CPUID_ECX,  3, 0x00000006, 0 },
	{ X86_FEATURE_CQM_LLC,		CPUID_EDX,  1, 0x0000000f, 0 },
	{ X86_FEATURE_CQM_OCCUP_LLC,	CPUID_EDX,  0, 0x0000000f, 1 },
	{ X86_FEATURE_CQM_MBM_TOTAL,	CPUID_EDX,  1, 0x0000000f, 1 },
	{ X86_FEATURE_CQM_MBM_LOCAL,	CPUID_EDX,  2, 0x0000000f, 1 },
	{ X86_FEATURE_CAT_L3,		CPUID_EBX,  1, 0x00000010, 0 },
	{ X86_FEATURE_CAT_L2,		CPUID_EBX,  2, 0x00000010, 0 },
	{ X86_FEATURE_CDP_L3,		CPUID_ECX,  2, 0x00000010, 1 },
	{ X86_FEATURE_CDP_L2,		CPUID_ECX,  2, 0x00000010, 2 },
	{ X86_FEATURE_MBA,		CPUID_EBX,  3, 0x00000010, 0 },
	{ X86_FEATURE_HW_PSTATE,	CPUID_EDX,  7, 0x80000007, 0 },
	{ X86_FEATURE_CPB,		CPUID_EDX,  9, 0x80000007, 0 },
	{ X86_FEATURE_PROC_FEEDBACK,    CPUID_EDX, 11, 0x80000007, 0 },
	{ X86_FEATURE_SME,		CPUID_EAX,  0, 0x8000001f, 0 },
	{ X86_FEATURE_SEV,		CPUID_EAX,  1, 0x8000001f, 0 },
	{ 0, 0, 0, 0, 0 }
};

void init_scattered_cpuid_features(struct cpuinfo_x86 *c)
{
	u32 max_level;
	u32 regs[4];
	const struct cpuid_bit *cb;

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

u32 get_scattered_cpuid_leaf(unsigned int level, unsigned int sub_leaf,
			     enum cpuid_regs_idx reg)
{
	const struct cpuid_bit *cb;
	u32 cpuid_val = 0;

	for (cb = cpuid_bits; cb->feature; cb++) {

		if (level > cb->level)
			continue;

		if (level < cb->level)
			break;

		if (reg == cb->reg && sub_leaf == cb->sub_leaf) {
			if (cpu_has(&boot_cpu_data, cb->feature))
				cpuid_val |= BIT(cb->bit);
		}
	}

	return cpuid_val;
}
EXPORT_SYMBOL_GPL(get_scattered_cpuid_leaf);
