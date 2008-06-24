/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <asm/cputable.h>
#include <asm/code-patching.h>


struct fixup_entry {
	unsigned long	mask;
	unsigned long	value;
	long		start_off;
	long		end_off;
	long		alt_start_off;
	long		alt_end_off;
};

static void patch_feature_section(unsigned long value, struct fixup_entry *fcur)
{
	unsigned int *pstart, *pend, *p;

	if ((value & fcur->mask) == fcur->value)
		return;

	pstart = ((unsigned int *)fcur) + (fcur->start_off / 4);
	pend = ((unsigned int *)fcur) + (fcur->end_off / 4);

	for (p = pstart; p < pend; p++) {
		*p = PPC_NOP_INSTR;
		asm volatile ("dcbst 0, %0" : : "r" (p));
	}
	asm volatile ("sync" : : : "memory");
	for (p = pstart; p < pend; p++)
		asm volatile ("icbi 0,%0" : : "r" (p));
	asm volatile ("sync; isync" : : : "memory");
}

void do_feature_fixups(unsigned long value, void *fixup_start, void *fixup_end)
{
	struct fixup_entry *fcur, *fend;

	fcur = fixup_start;
	fend = fixup_end;

	for (; fcur < fend; fcur++)
		patch_feature_section(value, fcur);
}
