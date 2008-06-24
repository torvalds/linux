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
	long offset;

	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (offset < -0x2000000 || offset > 0x1fffffc || offset & 0x3)
		return 0;

	/* Mask out the flags and target, so they don't step on each other. */
	instruction = 0x48000000 | (flags & 0x3) | (offset & 0x03FFFFFC);

	return instruction;
}

unsigned int create_cond_branch(const unsigned int *addr,
				unsigned long target, int flags)
{
	unsigned int instruction;
	long offset;

	offset = target;
	if (! (flags & BRANCH_ABSOLUTE))
		offset = offset - (unsigned long)addr;

	/* Check we can represent the target in the instruction format */
	if (offset < -0x8000 || offset > 0x7FFF || offset & 0x3)
		return 0;

	/* Mask out the flags and target, so they don't step on each other. */
	instruction = 0x40000000 | (flags & 0x3FF0003) | (offset & 0xFFFC);

	return instruction;
}

static unsigned int branch_opcode(unsigned int instr)
{
	return (instr >> 26) & 0x3F;
}

static int instr_is_branch_iform(unsigned int instr)
{
	return branch_opcode(instr) == 18;
}

static int instr_is_branch_bform(unsigned int instr)
{
	return branch_opcode(instr) == 16;
}

int instr_is_relative_branch(unsigned int instr)
{
	if (instr & BRANCH_ABSOLUTE)
		return 0;

	return instr_is_branch_iform(instr) || instr_is_branch_bform(instr);
}

static unsigned long branch_iform_target(const unsigned int *instr)
{
	signed long imm;

	imm = *instr & 0x3FFFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x2000000)
		imm -= 0x4000000;

	if ((*instr & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

static unsigned long branch_bform_target(const unsigned int *instr)
{
	signed long imm;

	imm = *instr & 0xFFFC;

	/* If the top bit of the immediate value is set this is negative */
	if (imm & 0x8000)
		imm -= 0x10000;

	if ((*instr & BRANCH_ABSOLUTE) == 0)
		imm += (unsigned long)instr;

	return (unsigned long)imm;
}

unsigned long branch_target(const unsigned int *instr)
{
	if (instr_is_branch_iform(*instr))
		return branch_iform_target(instr);
	else if (instr_is_branch_bform(*instr))
		return branch_bform_target(instr);

	return 0;
}

int instr_is_branch_to_addr(const unsigned int *instr, unsigned long addr)
{
	if (instr_is_branch_iform(*instr) || instr_is_branch_bform(*instr))
		return branch_target(instr) == addr;

	return 0;
}

unsigned int translate_branch(const unsigned int *dest, const unsigned int *src)
{
	unsigned long target;

	target = branch_target(src);

	if (instr_is_branch_iform(*src))
		return create_branch(dest, target, *src);
	else if (instr_is_branch_bform(*src))
		return create_cond_branch(dest, target, *src);

	return 0;
}
