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


void patch_instruction(unsigned int *addr, unsigned int instr)
{
	*addr = instr;
	asm ("dcbst 0, %0; sync; icbi 0,%0; sync; isync" : : "r" (addr));
}

void patch_branch(unsigned int *addr, unsigned long target, int flags)
{
	patch_instruction(addr, create_branch(addr, target, flags));
}

unsigned int create_branch(const unsigned int *addr,
			   unsigned long target, int flags)
{
	unsigned int instruction;

	if (! (flags & BRANCH_ABSOLUTE))
		target = target - (unsigned long)addr;

	/* Mask out the flags and target, so they don't step on each other. */
	instruction = 0x48000000 | (flags & 0x3) | (target & 0x03FFFFFC);

	return instruction;
}
