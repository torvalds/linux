/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_FACILITY_H
#define __ASM_FACILITY_H

#include <linux/string.h>
#include <linux/preempt.h>
#include <asm/lowcore.h>

#define MAX_FACILITY_BIT (256*8)	/* stfle_fac_list has 256 bytes */

/*
 * The test_facility function uses the bit odering where the MSB is bit 0.
 * That makes it easier to query facility bits with the bit number as
 * documented in the Principles of Operation.
 */
static inline int test_facility(unsigned long nr)
{
	unsigned char *ptr;

	if (nr >= MAX_FACILITY_BIT)
		return 0;
	ptr = (unsigned char *) &S390_lowcore.stfle_fac_list + (nr >> 3);
	return (*ptr & (0x80 >> (nr & 7))) != 0;
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
		"	.insn s,0xb2b10000,0(0)\n" /* stfl */
		"0:\n"
		EX_TABLE(0b, 0b)
		: "+m" (S390_lowcore.stfl_fac_list));
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
