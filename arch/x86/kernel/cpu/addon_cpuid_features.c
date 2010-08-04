/*
 *	Routines to indentify additional cpu features that are scattered in
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

	static const struct cpuid_bit __cpuinitconst cpuid_bits[] = {
		{ X86_FEATURE_IDA,   		CR_EAX, 1, 0x00000006 },
		{ X86_FEATURE_ARAT,  		CR_EAX, 2, 0x00000006 },
		{ X86_FEATURE_APERFMPERF,	CR_ECX, 0, 0x00000006 },
		{ X86_FEATURE_CPB,   		CR_EDX, 9, 0x80000007 },
		{ X86_FEATURE_NPT,   		CR_EDX, 0, 0x8000000a },
		{ X86_FEATURE_LBRV,  		CR_EDX, 1, 0x8000000a },
		{ X86_FEATURE_SVML,  		CR_EDX, 2, 0x8000000a },
		{ X86_FEATURE_NRIPS, 		CR_EDX, 3, 0x8000000a },
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

/* leaf 0xb SMT level */
#define SMT_LEVEL	0

/* leaf 0xb sub-leaf types */
#define INVALID_TYPE	0
#define SMT_TYPE	1
#define CORE_TYPE	2

#define LEAFB_SUBTYPE(ecx)		(((ecx) >> 8) & 0xff)
#define BITS_SHIFT_NEXT_LEVEL(eax)	((eax) & 0x1f)
#define LEVEL_MAX_SIBLINGS(ebx)		((ebx) & 0xffff)

/*
 * Check for extended topology enumeration cpuid leaf 0xb and if it
 * exists, use it for populating initial_apicid and cpu topology
 * detection.
 */
void __cpuinit detect_extended_topology(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned int eax, ebx, ecx, edx, sub_index;
	unsigned int ht_mask_width, core_plus_mask_width;
	unsigned int core_select_mask, core_level_siblings;
	static bool printed;

	if (c->cpuid_level < 0xb)
		return;

	cpuid_count(0xb, SMT_LEVEL, &eax, &ebx, &ecx, &edx);

	/*
	 * check if the cpuid leaf 0xb is actually implemented.
	 */
	if (ebx == 0 || (LEAFB_SUBTYPE(ecx) != SMT_TYPE))
		return;

	set_cpu_cap(c, X86_FEATURE_XTOPOLOGY);

	/*
	 * initial apic id, which also represents 32-bit extended x2apic id.
	 */
	c->initial_apicid = edx;

	/*
	 * Populate HT related information from sub-leaf level 0.
	 */
	core_level_siblings = smp_num_siblings = LEVEL_MAX_SIBLINGS(ebx);
	core_plus_mask_width = ht_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);

	sub_index = 1;
	do {
		cpuid_count(0xb, sub_index, &eax, &ebx, &ecx, &edx);

		/*
		 * Check for the Core type in the implemented sub leaves.
		 */
		if (LEAFB_SUBTYPE(ecx) == CORE_TYPE) {
			core_level_siblings = LEVEL_MAX_SIBLINGS(ebx);
			core_plus_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);
			break;
		}

		sub_index++;
	} while (LEAFB_SUBTYPE(ecx) != INVALID_TYPE);

	core_select_mask = (~(-1 << core_plus_mask_width)) >> ht_mask_width;

	c->cpu_core_id = apic->phys_pkg_id(c->initial_apicid, ht_mask_width)
						 & core_select_mask;
	c->phys_proc_id = apic->phys_pkg_id(c->initial_apicid, core_plus_mask_width);
	/*
	 * Reinit the apicid, now that we have extended initial_apicid.
	 */
	c->apicid = apic->phys_pkg_id(c->initial_apicid, 0);

	c->x86_max_cores = (core_level_siblings / smp_num_siblings);

	if (!printed) {
		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
		       c->phys_proc_id);
		if (c->x86_max_cores > 1)
			printk(KERN_INFO  "CPU: Processor Core ID: %d\n",
			       c->cpu_core_id);
		printed = 1;
	}
	return;
#endif
}
