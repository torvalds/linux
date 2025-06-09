/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Module interface for CPU features
 *
 * Copyright IBM Corp. 2015, 2022
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef __ASM_S390_CPUFEATURE_H
#define __ASM_S390_CPUFEATURE_H

#include <asm/facility.h>

enum {
	S390_CPU_FEATURE_MSA,
	S390_CPU_FEATURE_VXRS,
	S390_CPU_FEATURE_UV,
	MAX_CPU_FEATURES
};

#define cpu_feature(feature)	(feature)

int cpu_have_feature(unsigned int nr);

#define cpu_has_bear()		test_facility(193)
#define cpu_has_edat1()		test_facility(8)
#define cpu_has_edat2()		test_facility(78)
#define cpu_has_gs()		test_facility(133)
#define cpu_has_idte()		test_facility(3)
#define cpu_has_nx()		test_facility(130)
#define cpu_has_rdp()		test_facility(194)
#define cpu_has_seq_insn()	test_facility(85)
#define cpu_has_tlb_lc()	test_facility(51)
#define cpu_has_topology()	test_facility(11)
#define cpu_has_vx()		test_facility(129)

#endif /* __ASM_S390_CPUFEATURE_H */
