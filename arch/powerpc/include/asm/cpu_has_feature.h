#ifndef __ASM_POWERPC_CPUFEATURES_H
#define __ASM_POWERPC_CPUFEATURES_H

#ifndef __ASSEMBLY__

#include <asm/cputable.h>

static inline bool early_cpu_has_feature(unsigned long feature)
{
	return !!((CPU_FTRS_ALWAYS & feature) ||
		  (CPU_FTRS_POSSIBLE & cur_cpu_spec->cpu_features & feature));
}

#ifdef CONFIG_JUMP_LABEL_FEATURE_CHECKS
#include <linux/jump_label.h>

#define NUM_CPU_FTR_KEYS	64

extern struct static_key_true cpu_feature_keys[NUM_CPU_FTR_KEYS];

static __always_inline bool cpu_has_feature(unsigned long feature)
{
	int i;

	BUILD_BUG_ON(!__builtin_constant_p(feature));

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
#endif /* __ASM_POWERPC_CPUFEATURE_H */
