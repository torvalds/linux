/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2024
 */

#ifndef __ASM_S390_MACHINE_H
#define __ASM_S390_MACHINE_H

#include <linux/const.h>

#define MFEATURE_LOWCORE	0
#define MFEATURE_PCI_MIO	1
#define MFEATURE_SCC		2

#ifndef __ASSEMBLY__

#include <linux/bitops.h>
#include <asm/alternative.h>

extern unsigned long machine_features[1];

#define MAX_MFEATURE_BIT (sizeof(machine_features) * BITS_PER_BYTE)

static inline void __set_machine_feature(unsigned int nr, unsigned long *mfeatures)
{
	if (nr >= MAX_MFEATURE_BIT)
		return;
	__set_bit(nr, mfeatures);
}

static inline void set_machine_feature(unsigned int nr)
{
	__set_machine_feature(nr, machine_features);
}

static inline void __clear_machine_feature(unsigned int nr, unsigned long *mfeatures)
{
	if (nr >= MAX_MFEATURE_BIT)
		return;
	__clear_bit(nr, mfeatures);
}

static inline void clear_machine_feature(unsigned int nr)
{
	__clear_machine_feature(nr, machine_features);
}

static bool __test_machine_feature(unsigned int nr, unsigned long *mfeatures)
{
	if (nr >= MAX_MFEATURE_BIT)
		return false;
	return test_bit(nr, mfeatures);
}

static bool test_machine_feature(unsigned int nr)
{
	return __test_machine_feature(nr, machine_features);
}

static __always_inline bool __test_machine_feature_constant(unsigned int nr)
{
	asm goto(
		ALTERNATIVE("brcl 15,%l[l_no]", "brcl 0,0", ALT_FEATURE(%[nr]))
		:
		: [nr] "i" (nr)
		:
		: l_no);
	return true;
l_no:
	return false;
}

#define DEFINE_MACHINE_HAS_FEATURE(name, feature)				\
static __always_inline bool machine_has_##name(void)				\
{										\
	if (!__is_defined(__DECOMPRESSOR) && __builtin_constant_p(feature))	\
		return __test_machine_feature_constant(feature);		\
	return test_machine_feature(feature);					\
}

DEFINE_MACHINE_HAS_FEATURE(relocated_lowcore, MFEATURE_LOWCORE)
DEFINE_MACHINE_HAS_FEATURE(scc, MFEATURE_SCC)

#endif /* __ASSEMBLY__ */
#endif /* __ASM_S390_MACHINE_H */
