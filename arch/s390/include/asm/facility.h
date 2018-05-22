/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_FACILITY_H
#define __ASM_FACILITY_H

#include <asm/facility-defs.h>
#include <linux/string.h>
#include <linux/preempt.h>
#include <asm/lowcore.h>

#define MAX_FACILITY_BIT (sizeof(((struct lowcore *)0)->stfle_fac_list) * 8)

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

static inline int __test_facility(unsigned long nr, void *facilities)
{
	unsigned char *ptr;

	if (nr >= MAX_FACILITY_BIT)
		return 0;
	ptr = (unsigned char *) facilities + (nr >> 3);
	return (*ptr & (0x80 >> (nr & 7))) != 0;
}

/*
 * The test_facility function uses the bit odering where the MSB is bit 0.
 * That makes it easier to query facility bits with the bit number as
 * documented in the Principles of Operation.
 */
static inline int test_facility(unsigned long nr)
{
	unsigned long facilities_als[] = { FACILITIES_ALS };

	if (__builtin_constant_p(nr) && nr < sizeof(facilities_als) * 8) {
		if (__test_facility(nr, &facilities_als))
			return 1;
	}
	return __test_facility(nr, &S390_lowcore.stfle_fac_list);
}

/**
 * stfle - Store facility list extended
 * @stfle_fac_list: array where facility list can be stored
 * @size: size of passed in array in double words
 */
static inline void stfle(u64 *stfle_fac_list, int size)
{
	unsigned long nr;

	preempt_disable();
	asm volatile(
		"	stfl	0(0)\n"
		: "=m" (S390_lowcore.stfl_fac_list));
	nr = 4; /* bytes stored by stfl */
	memcpy(stfle_fac_list, &S390_lowcore.stfl_fac_list, 4);
	if (S390_lowcore.stfl_fac_list & 0x01000000) {
		/* More facility bits available with stfle */
		register unsigned long reg0 asm("0") = size - 1;

		asm volatile(".insn s,0xb2b00000,0(%1)" /* stfle */
			     : "+d" (reg0)
			     : "a" (stfle_fac_list)
			     : "memory", "cc");
		nr = (reg0 + 1) * 8; /* # bytes stored by stfle */
	}
	memset((char *) stfle_fac_list + nr, 0, size * 8 - nr);
	preempt_enable();
}

#endif /* __ASM_FACILITY_H */
