
/*
 *	Routines to indentify additional cpu features that are scattered in
 *	cpuid space.
 */

#include <linux/cpu.h>

#include <asm/pat.h>
#include <asm/processor.h>

struct cpuid_bit {
	u16 feature;
	u8 reg;
	u8 bit;
	u32 level;
};

enum cpuid_regs {
	CR_EAX = 0,
	CR_ECX,
	CR_EDX,
	CR_EBX
};

void __cpuinit init_scattered_cpuid_features(struct cpuinfo_x86 *c)
{
	u32 max_level;
	u32 regs[4];
	const struct cpuid_bit *cb;

	static const struct cpuid_bit cpuid_bits[] = {
		{ X86_FEATURE_IDA, CR_EAX, 1, 0x00000006 },
		{ 0, 0, 0, 0 }
	};

	for (cb = cpuid_bits; cb->feature; cb++) {

		/* Verify that the level is valid */
		max_level = cpuid_eax(cb->level & 0xffff0000);
		if (max_level < cb->level ||
		    max_level > (cb->level | 0xffff))
			continue;

		cpuid(cb->level, &regs[CR_EAX], &regs[CR_EBX],
			&regs[CR_ECX], &regs[CR_EDX]);

		if (regs[cb->reg] & (1 << cb->bit))
			set_cpu_cap(c, cb->feature);
	}
}

#ifdef CONFIG_X86_PAT
void __cpuinit validate_pat_support(struct cpuinfo_x86 *c)
{
	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		if (c->x86 >= 0xf && c->x86 <= 0x11)
			return;
		break;
	case X86_VENDOR_INTEL:
		if (c->x86 == 0xF || (c->x86 == 6 && c->x86_model >= 15))
			return;
		break;
	}

	pat_disable(cpu_has_pat ?
		    "PAT disabled. Not yet verified on this CPU type." :
		    "PAT not supported by CPU.");
}
#endif
