/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_FACILITY_H
#define __ASM_FACILITY_H

#include <asm/facility-defs.h>

#include <linux/minmax.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/preempt.h>
#include <asm/alternative.h>
#include <asm/lowcore.h>

#define MAX_FACILITY_BIT (sizeof(stfle_fac_list) * 8)

extern u64 stfle_fac_list[16];

static inline void __set_facility(unsigned long nr, void *facilities)
{
	unsigned char *ptr = (unsigned char *) facilities;

	if (nr >= MAX_FACILITY_BIT)
		return;
	ptr[nr >> 3] |= 0x80 >> (nr & 7);
}

static inline void __clear_facility(unsigned long nr, void *facilities)
{
	unsigned char *ptr = (unsigned char *) facilities;

	if (nr >= MAX_FACILITY_BIT)
		return;
	ptr[nr >> 3] &= ~(0x80 >> (nr & 7));
}

static __always_inline bool __test_facility(unsigned long nr, void *facilities)
{
	unsigned char *ptr;

	if (nr >= MAX_FACILITY_BIT)
		return false;
	ptr = (unsigned char *) facilities + (nr >> 3);
	return (*ptr & (0x80 >> (nr & 7))) != 0;
}

/*
 * __test_facility_constant() generates a single instruction branch. If the
 * tested facility is available (likely) the branch is patched into a nop.
 *
 * Do not use this function unless you know what you are doing. All users are
 * supposed to use test_facility() which will do the right thing.
 */
static __always_inline bool __test_facility_constant(unsigned long nr)
{
	asm goto(
		ALTERNATIVE("brcl 15,%l[l_no]", "brcl 0,0", ALT_FACILITY(%[nr]))
		:
		: [nr] "i" (nr)
		:
		: l_no);
	return true;
l_no:
	return false;
}

/*
 * The test_facility function uses the bit ordering where the MSB is bit 0.
 * That makes it easier to query facility bits with the bit number as
 * documented in the Principles of Operation.
 */
static __always_inline bool test_facility(unsigned long nr)
{
	unsigned long facilities_als[] = { FACILITIES_ALS };

	if (!__is_defined(__DECOMPRESSOR) && __builtin_constant_p(nr)) {
		if (nr < sizeof(facilities_als) * 8) {
			if (__test_facility(nr, &facilities_als))
				return true;
		}
		return __test_facility_constant(nr);
	}
	return __test_facility(nr, &stfle_fac_list);
}

static inline unsigned long __stfle_asm(u64 *stfle_fac_list, int size)
{
	unsigned long reg0 = size - 1;

	asm volatile(
		"	lgr	0,%[reg0]\n"
		"	.insn	s,0xb2b00000,%[list]\n" /* stfle */
		"	lgr	%[reg0],0\n"
		: [reg0] "+&d" (reg0), [list] "+Q" (*stfle_fac_list)
		:
		: "memory", "cc", "0");
	return reg0;
}

/**
 * stfle - Store facility list extended
 * @stfle_fac_list: array where facility list can be stored
 * @size: size of passed in array in double words
 */
static inline void __stfle(u64 *stfle_fac_list, int size)
{
	unsigned long nr;
	u32 stfl_fac_list;

	asm volatile(
		"	stfl	0(0)\n"
		: "=m" (get_lowcore()->stfl_fac_list));
	stfl_fac_list = get_lowcore()->stfl_fac_list;
	memcpy(stfle_fac_list, &stfl_fac_list, 4);
	nr = 4; /* bytes stored by stfl */
	if (stfl_fac_list & 0x01000000) {
		/* More facility bits available with stfle */
		nr = __stfle_asm(stfle_fac_list, size);
		nr = min_t(unsigned long, (nr + 1) * 8, size * 8);
	}
	memset((char *) stfle_fac_list + nr, 0, size * 8 - nr);
}

static inline void stfle(u64 *stfle_fac_list, int size)
{
	preempt_disable();
	__stfle(stfle_fac_list, size);
	preempt_enable();
}

/**
 * stfle_size - Actual size of the facility list as specified by stfle
 * (number of double words)
 */
unsigned int stfle_size(void);

#endif /* __ASM_FACILITY_H */
