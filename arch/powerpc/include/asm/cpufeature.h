/*
 * CPU feature definitions for module loading, used by
 * module_cpu_feature_match(), see asm/cputable.h for powerpc CPU features.
 *
 * Copyright 2016 Alastair D'Silva, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __ASM_POWERPC_CPUFEATURE_H
#define __ASM_POWERPC_CPUFEATURE_H

#include <asm/cputable.h>

/* Keep these in step with powerpc/include/asm/cputable.h */
#define MAX_CPU_FEATURES (2 * 32)

/*
 * Currently we don't have a need for any of the feature bits defined in
 * cpu_user_features. When we do, they should be defined such as:
 *
 * #define PPC_MODULE_FEATURE_32 (ilog2(PPC_FEATURE_32))
 */

#define PPC_MODULE_FEATURE_VEC_CRYPTO			(32 + ilog2(PPC_FEATURE2_VEC_CRYPTO))

#define cpu_feature(x)		(x)

static inline bool cpu_have_feature(unsigned int num)
{
	if (num < 32)
		return !!(cur_cpu_spec->cpu_user_features & 1UL << num);
	else
		return !!(cur_cpu_spec->cpu_user_features2 & 1UL << (num - 32));
}

#endif /* __ASM_POWERPC_CPUFEATURE_H */
