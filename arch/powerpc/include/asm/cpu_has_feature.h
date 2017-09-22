#ifndef __ASM_POWERPC_CPU_HAS_FEATURE_H
#define __ASM_POWERPC_CPU_HAS_FEATURE_H

#ifndef __ASSEMBLY__

#include <linux/bug.h>
#include <asm/cputable.h>

static inline bool early_cpu_has_feature(unsigned long feature)
{
	return !!((CPU_FTRS_ALWAYS & feature) ||
		  (CPU_FTRS_POSSIBLE & cur_cpu_spec->cpu_features & feature));
}

#ifdef CONFIG_JUMP_LABEL_FEATURE_CHECKS
#include <linux/jump_label.h>

#define NUM_CPU_FTR_KEYS	BITS_PER_LONG

extern struct static_key_true cpu_feature_keys[NUM_CPU_FTR_KEYS];

static __always_inline bool cpu_has_feature(unsigned long feature)
{
	int i;

#ifndef __clang__ /* clang can't cope with this */
	BUILD_BUG_ON(!__builtin_constant_p(feature));
#endif

#ifdef CONFIG_JUMP_LABEL_FEATURE_CHECK_DEBUG
	if (!static_key_initialized) {
		printk("Warning! cpu_has_feature() used prior to jump label init!\n");
		dump_stack();
		return early_cpu_has_feature(feature);
	}
#endif

	if (CPU_FTRS_ALWAYS & feature)
		return true;

	if (!(CPU_FTRS_POSSIBLE & feature))
		return false;

	i = __builtin_ctzl(feature);
	return static_branch_likely(&cpu_feature_keys[i]);
}
#else
static inline bool cpu_has_feature(unsigned long feature)
{
	return early_cpu_has_feature(feature);
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_POWERPC_CPU_HAS_FEATURE_H */
