/*
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <asm/code-patching.h>


void create_instruction(unsigned long addr, unsigned int instr)
{
	unsigned int *p;
	p  = (unsigned int *)addr;
	*p = instr;
	asm ("dcbst 0, %0; sync; icbi 0,%0; sync; isync" : : "r" (p));
}

void create_branch(unsigned long addr, unsigned long target, int flags)
{
	unsigned int instruction;

	if (! (flags & BRANCH_ABSOLUTE))
		target = target - addr;

	/* Mask out the flags and target, so they don't step on each other. */
	instruction = 0x48000000 | (flags & 0x3) | (target & 0x03FFFFFC);

	create_instruction(addr, instruction);
}
