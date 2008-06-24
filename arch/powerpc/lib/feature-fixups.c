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

static unsigned int *calc_addr(struct fixup_entry *fcur, long offset)
{
	/*
	 * We store the offset to the code as a negative offset from
	 * the start of the alt_entry, to support the VDSO. This
	 * routine converts that back into an actual address.
	 */
	return (unsigned int *)((unsigned long)fcur + offset);
}

static int patch_alt_instruction(unsigned int *src, unsigned int *dest,
				 unsigned int *alt_start, unsigned int *alt_end)
{
	unsigned int instr;

	instr = *src;

	if (instr_is_relative_branch(*src)) {
		unsigned int *target = (unsigned int *)branch_target(src);

		/* Branch within the section doesn't need translating */
		if (target < alt_start || target >= alt_end) {
			instr = translate_branch(dest, src);
			if (!instr)
				return 1;
		}
	}

	patch_instruction(dest, instr);

	return 0;
}

static int patch_feature_section(unsigned long value, struct fixup_entry *fcur)
{
	unsigned int *start, *end, *alt_start, *alt_end, *src, *dest;

	start = calc_addr(fcur, fcur->start_off);
	end = calc_addr(fcur, fcur->end_off);
	alt_start = calc_addr(fcur, fcur->alt_start_off);
	alt_end = calc_addr(fcur, fcur->alt_end_off);

	if ((alt_end - alt_start) > (end - start))
		return 1;

	if ((value & fcur->mask) == fcur->value)
		return 0;

	src = alt_start;
	dest = start;

	for (; src < alt_end; src++, dest++) {
		if (patch_alt_instruction(src, dest, alt_start, alt_end))
			return 1;
	}

	for (; dest < end; dest++)
		patch_instruction(dest, PPC_NOP_INSTR);

	return 0;
}

void do_feature_fixups(unsigned long value, void *fixup_start, void *fixup_end)
{
	struct fixup_entry *fcur, *fend;

	fcur = fixup_start;
	fend = fixup_end;

	for (; fcur < fend; fcur++) {
		if (patch_feature_section(value, fcur)) {
			__WARN();
			printk("Unable to patch feature section at %p - %p" \
				" with %p - %p\n",
				calc_addr(fcur, fcur->start_off),
				calc_addr(fcur, fcur->end_off),
				calc_addr(fcur, fcur->alt_start_off),
				calc_addr(fcur, fcur->alt_end_off));
		}
	}
}
