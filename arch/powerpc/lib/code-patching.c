/*
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/code-patching.h>
#include <linux/uaccess.h>


int patch_instruction(unsigned int *addr, unsigned int instr)
{
	int err;

	__put_user_size(instr, addr, 4, err);
	if (err)
		return err;
	asm ("dcbst 0, %0; sync; icbi 0,%0; sync; isync" : : "r" (addr));
	return 0;
}

int patch_branch(unsigned int *addr, unsigned long target, int flags)
{
	return patch_instruction(addr, create_branch(addr, target, flags));
}

bool is_offset_in_branch_range(long offset)
{
	/*
	 * Powerpc branch instruction is :
	 *
	 *  0         6                 30   31
	 *  +---------+----------------+---+---+
	 *  | opcode  |     LI         |AA |LK |
	 *  +---------+----------------+---+---+
	 *  Where AA = 0 and LK = 0
	 *
	 * LI is a signed 24 bits integer. The real branch offset is computed
	 * by: imm32 = SignExtend(LI:'0b00', 32);
	 *
	 * So the maximum forward branch should be:
	 *   (0x007fffff << 2) = 0x01fffffc =  0x1fffffc
	 * The maximum backward branch should be:
	 *   (0xff800000 << 2) = 0xfe000000 = -0x2000000
	 */
	return (offset >= -0x2000000 && offset <= 0x1fffffc && !(offset & 0x3));
}

/*
 * Helper to check if a given instruction is a conditional branch
 * Derived from the conditional checks in analyse_instr()
 */
bool __kprobes is_conditional_branch(unsigned int instr)
{
	unsigned int opcode = instr >> 26;

	if (opcode == 16)       /* bc, bca, bcl, bcla */
		return true;
	if (opcode == 19) {
		switch ((instr >> 1) & 0x3ff) {
		case 16:        /* bclr, bclrl */
		case 528:       /* bcctr, bcctrl */
		case 560:       /* bctar, bctarl */
			return true;
		}
	}
	return false;
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
	if (!is_offset_in_branch_range(offset))
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

#ifdef CONFIG_PPC_BOOK3E_64
void __patch_exception(int exc, unsigned long addr)
{
	extern unsigned int interrupt_base_book3e;
	unsigned int *ibase = &interrupt_base_book3e;

	/* Our exceptions vectors start with a NOP and -then- a branch
	 * to deal with single stepping from userspace which stops on
	 * the second instruction. Thus we need to patch the second
	 * instruction of the exception, not the first one
	 */

	patch_branch(ibase + (exc / 4) + 1, addr, 0);
}
#endif

#ifdef CONFIG_CODE_PATCHING_SELFTEST

static void __init test_trampoline(void)
{
	asm ("nop;\n");
}

#define check(x)	\
	if (!(x)) printk("code-patching: test failed at line %d\n", __LINE__);

static void __init test_branch_iform(void)
{
	unsigned int instr;
	unsigned long addr;

	addr = (unsigned long)&instr;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_iform(0x48000000));
	/* All bits of target set, and flags */
	check(instr_is_branch_iform(0x4bffffff));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_iform(0xcbffffff));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_iform(0x7bffffff));

	/* Simplest case, branch to self with link */
	check(instr_is_branch_iform(0x48000001));
	/* All bits of targets set */
	check(instr_is_branch_iform(0x4bfffffd));
	/* Some bits of targets set */
	check(instr_is_branch_iform(0x4bff00fd));
	/* Must be a valid branch to start with */
	check(!instr_is_branch_iform(0x7bfffffd));

	/* Absolute branch to 0x100 */
	instr = 0x48000103;
	check(instr_is_branch_to_addr(&instr, 0x100));
	/* Absolute branch to 0x420fc */
	instr = 0x480420ff;
	check(instr_is_branch_to_addr(&instr, 0x420fc));
	/* Maximum positive relative branch, + 20MB - 4B */
	instr = 0x49fffffc;
	check(instr_is_branch_to_addr(&instr, addr + 0x1FFFFFC));
	/* Smallest negative relative branch, - 4B */
	instr = 0x4bfffffc;
	check(instr_is_branch_to_addr(&instr, addr - 4));
	/* Largest negative relative branch, - 32 MB */
	instr = 0x4a000000;
	check(instr_is_branch_to_addr(&instr, addr - 0x2000000));

	/* Branch to self, with link */
	instr = create_branch(&instr, addr, BRANCH_SET_LINK);
	check(instr_is_branch_to_addr(&instr, addr));

	/* Branch to self - 0x100, with link */
	instr = create_branch(&instr, addr - 0x100, BRANCH_SET_LINK);
	check(instr_is_branch_to_addr(&instr, addr - 0x100));

	/* Branch to self + 0x100, no link */
	instr = create_branch(&instr, addr + 0x100, 0);
	check(instr_is_branch_to_addr(&instr, addr + 0x100));

	/* Maximum relative negative offset, - 32 MB */
	instr = create_branch(&instr, addr - 0x2000000, BRANCH_SET_LINK);
	check(instr_is_branch_to_addr(&instr, addr - 0x2000000));

	/* Out of range relative negative offset, - 32 MB + 4*/
	instr = create_branch(&instr, addr - 0x2000004, BRANCH_SET_LINK);
	check(instr == 0);

	/* Out of range relative positive offset, + 32 MB */
	instr = create_branch(&instr, addr + 0x2000000, BRANCH_SET_LINK);
	check(instr == 0);

	/* Unaligned target */
	instr = create_branch(&instr, addr + 3, BRANCH_SET_LINK);
	check(instr == 0);

	/* Check flags are masked correctly */
	instr = create_branch(&instr, addr, 0xFFFFFFFC);
	check(instr_is_branch_to_addr(&instr, addr));
	check(instr == 0x48000000);
}

static void __init test_create_function_call(void)
{
	unsigned int *iptr;
	unsigned long dest;

	/* Check we can create a function call */
	iptr = (unsigned int *)ppc_function_entry(test_trampoline);
	dest = ppc_function_entry(test_create_function_call);
	patch_instruction(iptr, create_branch(iptr, dest, BRANCH_SET_LINK));
	check(instr_is_branch_to_addr(iptr, dest));
}

static void __init test_branch_bform(void)
{
	unsigned long addr;
	unsigned int *iptr, instr, flags;

	iptr = &instr;
	addr = (unsigned long)iptr;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_bform(0x40000000));
	/* All bits of target set, and flags */
	check(instr_is_branch_bform(0x43ffffff));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_bform(0xc3ffffff));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_bform(0x7bffffff));

	/* Absolute conditional branch to 0x100 */
	instr = 0x43ff0103;
	check(instr_is_branch_to_addr(&instr, 0x100));
	/* Absolute conditional branch to 0x20fc */
	instr = 0x43ff20ff;
	check(instr_is_branch_to_addr(&instr, 0x20fc));
	/* Maximum positive relative conditional branch, + 32 KB - 4B */
	instr = 0x43ff7ffc;
	check(instr_is_branch_to_addr(&instr, addr + 0x7FFC));
	/* Smallest negative relative conditional branch, - 4B */
	instr = 0x43fffffc;
	check(instr_is_branch_to_addr(&instr, addr - 4));
	/* Largest negative relative conditional branch, - 32 KB */
	instr = 0x43ff8000;
	check(instr_is_branch_to_addr(&instr, addr - 0x8000));

	/* All condition code bits set & link */
	flags = 0x3ff000 | BRANCH_SET_LINK;

	/* Branch to self */
	instr = create_cond_branch(iptr, addr, flags);
	check(instr_is_branch_to_addr(&instr, addr));

	/* Branch to self - 0x100 */
	instr = create_cond_branch(iptr, addr - 0x100, flags);
	check(instr_is_branch_to_addr(&instr, addr - 0x100));

	/* Branch to self + 0x100 */
	instr = create_cond_branch(iptr, addr + 0x100, flags);
	check(instr_is_branch_to_addr(&instr, addr + 0x100));

	/* Maximum relative negative offset, - 32 KB */
	instr = create_cond_branch(iptr, addr - 0x8000, flags);
	check(instr_is_branch_to_addr(&instr, addr - 0x8000));

	/* Out of range relative negative offset, - 32 KB + 4*/
	instr = create_cond_branch(iptr, addr - 0x8004, flags);
	check(instr == 0);

	/* Out of range relative positive offset, + 32 KB */
	instr = create_cond_branch(iptr, addr + 0x8000, flags);
	check(instr == 0);

	/* Unaligned target */
	instr = create_cond_branch(iptr, addr + 3, flags);
	check(instr == 0);

	/* Check flags are masked correctly */
	instr = create_cond_branch(iptr, addr, 0xFFFFFFFC);
	check(instr_is_branch_to_addr(&instr, addr));
	check(instr == 0x43FF0000);
}

static void __init test_translate_branch(void)
{
	unsigned long addr;
	unsigned int *p, *q;
	void *buf;

	buf = vmalloc(PAGE_ALIGN(0x2000000 + 1));
	check(buf);
	if (!buf)
		return;

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	check(instr_is_branch_to_addr(p, addr));
	q = p + 1;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 MB */
	p = buf;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	q = buf + 0x2000000;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(*q == 0x4a000000);

	/* Maximum positive case, move x to x - 32 MB + 4 */
	p = buf + 0x2000000;
	addr = (unsigned long)p;
	patch_branch(p, addr, 0);
	q = buf + 4;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(*q == 0x49fffffc);

	/* Jump to x + 16 MB moved to x + 20 MB */
	p = buf;
	addr = 0x1000000 + (unsigned long)buf;
	patch_branch(p, addr, BRANCH_SET_LINK);
	q = buf + 0x1400000;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 16 MB moved to x - 16 MB + 4 */
	p = buf + 0x1000000;
	addr = 0x2000000 + (unsigned long)buf;
	patch_branch(p, addr, 0);
	q = buf + 4;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));


	/* Conditional branch tests */

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	patch_instruction(p, create_cond_branch(p, addr, 0));
	check(instr_is_branch_to_addr(p, addr));
	q = p + 1;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 KB */
	p = buf;
	addr = (unsigned long)p;
	patch_instruction(p, create_cond_branch(p, addr, 0xFFFFFFFC));
	q = buf + 0x8000;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(*q == 0x43ff8000);

	/* Maximum positive case, move x to x - 32 KB + 4 */
	p = buf + 0x8000;
	addr = (unsigned long)p;
	patch_instruction(p, create_cond_branch(p, addr, 0xFFFFFFFC));
	q = buf + 4;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(*q == 0x43ff7ffc);

	/* Jump to x + 12 KB moved to x + 20 KB */
	p = buf;
	addr = 0x3000 + (unsigned long)buf;
	patch_instruction(p, create_cond_branch(p, addr, BRANCH_SET_LINK));
	q = buf + 0x5000;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 8 KB moved to x - 8 KB + 4 */
	p = buf + 0x2000;
	addr = 0x4000 + (unsigned long)buf;
	patch_instruction(p, create_cond_branch(p, addr, 0));
	q = buf + 4;
	patch_instruction(q, translate_branch(q, p));
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Free the buffer we were using */
	vfree(buf);
}

static int __init test_code_patching(void)
{
	printk(KERN_DEBUG "Running code patching self-tests ...\n");

	test_branch_iform();
	test_branch_bform();
	test_create_function_call();
	test_translate_branch();

	return 0;
}
late_initcall(test_code_patching);

#endif /* CONFIG_CODE_PATCHING_SELFTEST */
