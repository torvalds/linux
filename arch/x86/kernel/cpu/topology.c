// SPDX-License-Identifier: GPL-2.0
/*
 * Check for extended topology enumeration cpuid leaf 0xb and if it
 * exists, use it for populating initial_apicid and cpu topology
 * detection.
 */

#include <linux/cpu.h>
#include <asm/apic.h>
#include <asm/memtype.h>
#include <asm/processor.h>

#include "cpu.h"

/* leaf 0xb SMT level */
#define SMT_LEVEL	0

/* extended topology sub-leaf types */
#define INVALID_TYPE	0
#define SMT_TYPE	1
#define CORE_TYPE	2
#define DIE_TYPE	5

#define LEAFB_SUBTYPE(ecx)		(((ecx) >> 8) & 0xff)
#define BITS_SHIFT_NEXT_LEVEL(eax)	((eax) & 0x1f)
#define LEVEL_MAX_SIBLINGS(ebx)		((ebx) & 0xffff)

unsigned int __max_die_per_package __read_mostly = 1;
EXPORT_SYMBOL(__max_die_per_package);

#ifdef CONFIG_SMP
/*
 * Check if given CPUID extended toplogy "leaf" is implemented
 */
static int check_extended_topology_leaf(int leaf)
{
	unsigned int eax, ebx, ecx, edx;

	cpuid_count(leaf, SMT_LEVEL, &eax, &ebx, &ecx, &edx);

	if (ebx == 0 || (LEAFB_SUBTYPE(ecx) != SMT_TYPE))
		return -1;

	return 0;
}
/*
 * Return best CPUID Extended Toplogy Leaf supported
 */
static int detect_extended_topology_leaf(struct cpuinfo_x86 *c)
{
	if (c->cpuid_level >= 0x1f) {
		if (check_extended_topology_leaf(0x1f) == 0)
			return 0x1f;
	}

	if (c->cpuid_level >= 0xb) {
		if (check_extended_topology_leaf(0xb) == 0)
			return 0xb;
	}

	return -1;
}
#endif

int detect_extended_topology_early(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned int eax, ebx, ecx, edx;
	int leaf;

	leaf = detect_extended_topology_leaf(c);
	if (leaf < 0)
		return -1;

	set_cpu_cap(c, X86_FEATURE_XTOPOLOGY);

	cpuid_count(leaf, SMT_LEVEL, &eax, &ebx, &ecx, &edx);
	/*
	 * initial apic id, which also represents 32-bit extended x2apic id.
	 */
	c->initial_apicid = edx;
	smp_num_siblings = LEVEL_MAX_SIBLINGS(ebx);
#endif
	return 0;
}

/*
 * Check for extended topology enumeration cpuid leaf, and if it
 * exists, use it for populating initial_apicid and cpu topology
 * detection.
 */
int detect_extended_topology(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned int eax, ebx, ecx, edx, sub_index;
	unsigned int ht_mask_width, core_plus_mask_width, die_plus_mask_width;
	unsigned int core_select_mask, core_level_siblings;
	unsigned int die_select_mask, die_level_siblings;
	int leaf;

	leaf = detect_extended_topology_leaf(c);
	if (leaf < 0)
		return -1;

	/*
	 * Populate HT related information from sub-leaf level 0.
	 */
	cpuid_count(leaf, SMT_LEVEL, &eax, &ebx, &ecx, &edx);
	c->initial_apicid = edx;
	core_level_siblings = smp_num_siblings = LEVEL_MAX_SIBLINGS(ebx);
	core_plus_mask_width = ht_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);
	die_level_siblings = LEVEL_MAX_SIBLINGS(ebx);
	die_plus_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);

	sub_index = 1;
	do {
		cpuid_count(leaf, sub_index, &eax, &ebx, &ecx, &edx);

		/*
		 * Check for the Core type in the implemented sub leaves.
		 */
		if (LEAFB_SUBTYPE(ecx) == CORE_TYPE) {
			core_level_siblings = LEVEL_MAX_SIBLINGS(ebx);
			core_plus_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);
			die_level_siblings = core_level_siblings;
			die_plus_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);
		}
		if (LEAFB_SUBTYPE(ecx) == DIE_TYPE) {
			die_level_siblings = LEVEL_MAX_SIBLINGS(ebx);
			die_plus_mask_width = BITS_SHIFT_NEXT_LEVEL(eax);
		}

		sub_index++;
	} while (LEAFB_SUBTYPE(ecx) != INVALID_TYPE);

	core_select_mask = (~(-1 << core_plus_mask_width)) >> ht_mask_width;
	die_select_mask = (~(-1 << die_plus_mask_width)) >>
				core_plus_mask_width;

	c->cpu_core_id = apic->phys_pkg_id(c->initial_apicid,
				ht_mask_width) & core_select_mask;
	c->cpu_die_id = apic->phys_pkg_id(c->initial_apicid,
				core_plus_mask_width) & die_select_mask;
	c->phys_proc_id = apic->phys_pkg_id(c->initial_apicid,
				die_plus_mask_width);
	/*
	 * Reinit the apicid, now that we have extended initial_apicid.
	 */
	c->apicid = apic->phys_pkg_id(c->initial_apicid, 0);

	c->x86_max_cores = (core_level_siblings / smp_num_siblings);
	__max_die_per_package = (die_level_siblings / core_level_siblings);
#endif
	return 0;
}
