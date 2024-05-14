/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Module interface for CPU features
 *
 * Copyright IBM Corp. 2015, 2022
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef __ASM_S390_CPUFEATURE_H
#define __ASM_S390_CPUFEATURE_H

enum {
	S390_CPU_FEATURE_MSA,
	S390_CPU_FEATURE_VXRS,
	S390_CPU_FEATURE_UV,
	MAX_CPU_FEATURES
};

#define cpu_feature(feature)	(feature)

int cpu_have_feature(unsigned int nr);

#endif /* __ASM_S390_CPUFEATURE_H */
