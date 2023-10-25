// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 */

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/jump_label.h>
#include <linux/of.h>

#include <asm/cputable.h>
#include <asm/mce.h>
#include <asm/mmu.h>
#include <asm/setup.h>
#include <asm/cpu_setup.h>

static struct cpu_spec the_cpu_spec __ro_after_init;

struct cpu_spec *cur_cpu_spec __ro_after_init = NULL;
EXPORT_SYMBOL(cur_cpu_spec);

/* The platform string corresponding to the real PVR */
const char *powerpc_base_platform;

#include "cpu_specs.h"

void __init set_cur_cpu_spec(struct cpu_spec *s)
{
	struct cpu_spec *t = &the_cpu_spec;

	t = PTRRELOC(t);
	/*
	 * use memcpy() instead of *t = *s so that GCC replaces it
	 * by __memcpy() when KASAN is active
	 */
	memcpy(t, s, sizeof(*t));

	*PTRRELOC(&cur_cpu_spec) = &the_cpu_spec;
}

static struct cpu_spec * __init setup_cpu_spec(unsigned long offset,
					       struct cpu_spec *s)
{
	struct cpu_spec *t = &the_cpu_spec;
	struct cpu_spec old;

	t = PTRRELOC(t);
	old = *t;

	/*
	 * Copy everything, then do fixups. Use memcpy() instead of *t = *s
	 * so that GCC replaces it by __memcpy() when KASAN is active
	 */
	memcpy(t, s, sizeof(*t));

	/*
	 * If we are overriding a previous value derived from the real
	 * PVR with a new value obtained using a logical PVR value,
	 * don't modify the performance monitor fields.
	 */
	if (old.num_pmcs && !s->num_pmcs) {
		t->num_pmcs = old.num_pmcs;
		t->pmc_type = old.pmc_type;

		/*
		 * Let's ensure that the
		 * fix for the PMAO bug is enabled on compatibility mode.
		 */
		t->cpu_features |= old.cpu_features & CPU_FTR_PMAO_BUG;
	}

	/* Set kuap ON at startup, will be disabled later if cmdline has 'nosmap' */
	if (IS_ENABLED(CONFIG_PPC_KUAP) && IS_ENABLED(CONFIG_PPC32))
		t->mmu_features |= MMU_FTR_KUAP;

	*PTRRELOC(&cur_cpu_spec) = &the_cpu_spec;

	/*
	 * Set the base platform string once; assumes
	 * we're called with real pvr first.
	 */
	if (*PTRRELOC(&powerpc_base_platform) == NULL)
		*PTRRELOC(&powerpc_base_platform) = t->platform;

#if defined(CONFIG_PPC64) || defined(CONFIG_BOOKE)
	/* ppc64 and booke expect identify_cpu to also call setup_cpu for
	 * that processor. I will consolidate that at a later time, for now,
	 * just use #ifdef. We also don't need to PTRRELOC the function
	 * pointer on ppc64 and booke as we are running at 0 in real mode
	 * on ppc64 and reloc_offset is always 0 on booke.
	 */
	if (t->cpu_setup) {
		t->cpu_setup(offset, t);
	}
#endif /* CONFIG_PPC64 || CONFIG_BOOKE */

	return t;
}

struct cpu_spec * __init identify_cpu(unsigned long offset, unsigned int pvr)
{
	struct cpu_spec *s = cpu_specs;
	int i;

	BUILD_BUG_ON(!ARRAY_SIZE(cpu_specs));

	s = PTRRELOC(s);

	for (i = 0; i < ARRAY_SIZE(cpu_specs); i++,s++) {
		if ((pvr & s->pvr_mask) == s->pvr_value)
			return setup_cpu_spec(offset, s);
	}

	BUG();

	return NULL;
}

/*
 * Used by cpufeatures to get the name for CPUs with a PVR table.
 * If they don't hae a PVR table, cpufeatures gets the name from
 * cpu device-tree node.
 */
void __init identify_cpu_name(unsigned int pvr)
{
	struct cpu_spec *s = cpu_specs;
	struct cpu_spec *t = &the_cpu_spec;
	int i;

	s = PTRRELOC(s);
	t = PTRRELOC(t);

	for (i = 0; i < ARRAY_SIZE(cpu_specs); i++,s++) {
		if ((pvr & s->pvr_mask) == s->pvr_value) {
			t->cpu_name = s->cpu_name;
			return;
		}
	}
}


#ifdef CONFIG_JUMP_LABEL_FEATURE_CHECKS
struct static_key_true cpu_feature_keys[NUM_CPU_FTR_KEYS] = {
			[0 ... NUM_CPU_FTR_KEYS - 1] = STATIC_KEY_TRUE_INIT
};
EXPORT_SYMBOL_GPL(cpu_feature_keys);

void __init cpu_feature_keys_init(void)
{
	int i;

	for (i = 0; i < NUM_CPU_FTR_KEYS; i++) {
		unsigned long f = 1ul << i;

		if (!(cur_cpu_spec->cpu_features & f))
			static_branch_disable(&cpu_feature_keys[i]);
	}
}

struct static_key_true mmu_feature_keys[NUM_MMU_FTR_KEYS] = {
			[0 ... NUM_MMU_FTR_KEYS - 1] = STATIC_KEY_TRUE_INIT
};
EXPORT_SYMBOL(mmu_feature_keys);

void __init mmu_feature_keys_init(void)
{
	int i;

	for (i = 0; i < NUM_MMU_FTR_KEYS; i++) {
		unsigned long f = 1ul << i;

		if (!(cur_cpu_spec->mmu_features & f))
			static_branch_disable(&mmu_feature_keys[i]);
	}
}
#endif
