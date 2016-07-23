#ifndef __ASM_POWERPC_CPUFEATURES_H
#define __ASM_POWERPC_CPUFEATURES_H

#ifndef __ASSEMBLY__

#include <asm/cputable.h>

static inline bool early_cpu_has_feature(unsigned long feature)
{
	return !!((CPU_FTRS_ALWAYS & feature) ||
		  (CPU_FTRS_POSSIBLE & cur_cpu_spec->cpu_features & feature));
}

static inline bool cpu_has_feature(unsigned long feature)
{
	return early_cpu_has_feature(feature);
}

#endif /* __ASSEMBLY__ */
#endif /* __ASM_POWERPC_CPUFEATURE_H */
