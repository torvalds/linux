/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Module interface for CPU features
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef __ASM_S390_CPUFEATURE_H
#define __ASM_S390_CPUFEATURE_H

#include <asm/elf.h>

/* Hardware features on Linux on z Systems are indicated by facility bits that
 * are mapped to the so-called machine flags.  Particular machine flags are
 * then used to define ELF hardware capabilities; most notably hardware flags
 * that are essential for user space / glibc.
 *
 * Restrict the set of exposed CPU features to ELF hardware capabilities for
 * now.  Additional machine flags can be indicated by values larger than
 * MAX_ELF_HWCAP_FEATURES.
 */
#define MAX_ELF_HWCAP_FEATURES	(8 * sizeof(elf_hwcap))
#define MAX_CPU_FEATURES	MAX_ELF_HWCAP_FEATURES

#define cpu_feature(feat)	ilog2(HWCAP_S390_ ## feat)

int cpu_have_feature(unsigned int nr);

#endif /* __ASM_S390_CPUFEATURE_H */
