// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 */

#include <linux/vmalloc.h>
#include <linux/init.h>

#include <asm/text-patching.h>

static int __init instr_is_branch_to_addr(const u32 *instr, unsigned long addr)
{
	if (instr_is_branch_iform(ppc_inst_read(instr)) ||
	    instr_is_branch_bform(ppc_inst_read(instr)))
		return branch_target(instr) == addr;

	return 0;
}

static void __init test_trampoline(void)
{
	asm ("nop;nop;\n");
}

#define check(x)	do {	\
	if (!(x))		\
		pr_err("code-patching: test failed at line %d\n", __LINE__); \
} while (0)

static void __init test_branch_iform(void)
{
	int err;
	ppc_inst_t instr;
	u32 tmp[2];
	u32 *iptr = tmp;
	unsigned long addr = (unsigned long)tmp;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_iform(ppc_inst(0x48000000)));
	/* All bits of target set, and flags */
	check(instr_is_branch_iform(ppc_inst(0x4bffffff)));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_iform(ppc_inst(0xcbffffff)));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_iform(ppc_inst(0x7bffffff)));

	/* Simplest case, branch to self with link */
	check(instr_is_branch_iform(ppc_inst(0x48000001)));
	/* All bits of targets set */
	check(instr_is_branch_iform(ppc_inst(0x4bfffffd)));
	/* Some bits of targets set */
	check(instr_is_branch_iform(ppc_inst(0x4bff00fd)));
	/* Must be a valid branch to start with */
	check(!instr_is_branch_iform(ppc_inst(0x7bfffffd)));

	/* Absolute branch to 0x100 */
	ppc_inst_write(iptr, ppc_inst(0x48000103));
	check(instr_is_branch_to_addr(iptr, 0x100));
	/* Absolute branch to 0x420fc */
	ppc_inst_write(iptr, ppc_inst(0x480420ff));
	check(instr_is_branch_to_addr(iptr, 0x420fc));
	/* Maximum positive relative branch, + 20MB - 4B */
	ppc_inst_write(iptr, ppc_inst(0x49fffffc));
	check(instr_is_branch_to_addr(iptr, addr + 0x1FFFFFC));
	/* Smallest negative relative branch, - 4B */
	ppc_inst_write(iptr, ppc_inst(0x4bfffffc));
	check(instr_is_branch_to_addr(iptr, addr - 4));
	/* Largest negative relative branch, - 32 MB */
	ppc_inst_write(iptr, ppc_inst(0x4a000000));
	check(instr_is_branch_to_addr(iptr, addr - 0x2000000));

	/* Branch to self, with link */
	err = create_branch(&instr, iptr, addr, BRANCH_SET_LINK);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));

	/* Branch to self - 0x100, with link */
	err = create_branch(&instr, iptr, addr - 0x100, BRANCH_SET_LINK);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x100));

	/* Branch to self + 0x100, no link */
	err = create_branch(&instr, iptr, addr + 0x100, 0);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr + 0x100));

	/* Maximum relative negative offset, - 32 MB */
	err = create_branch(&instr, iptr, addr - 0x2000000, BRANCH_SET_LINK);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x2000000));

	/* Out of range relative negative offset, - 32 MB + 4*/
	err = create_branch(&instr, iptr, addr - 0x2000004, BRANCH_SET_LINK);
	check(err);

	/* Out of range relative positive offset, + 32 MB */
	err = create_branch(&instr, iptr, addr + 0x2000000, BRANCH_SET_LINK);
	check(err);

	/* Unaligned target */
	err = create_branch(&instr, iptr, addr + 3, BRANCH_SET_LINK);
	check(err);

	/* Check flags are masked correctly */
	err = create_branch(&instr, iptr, addr, 0xFFFFFFFC);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));
	check(ppc_inst_equal(instr, ppc_inst(0x48000000)));
}

static void __init test_create_function_call(void)
{
	u32 *iptr;
	unsigned long dest;
	ppc_inst_t instr;

	/* Check we can create a function call */
	iptr = (u32 *)ppc_function_entry(test_trampoline);
	dest = ppc_function_entry(test_create_function_call);
	create_branch(&instr, iptr, dest, BRANCH_SET_LINK);
	patch_instruction(iptr, instr);
	check(instr_is_branch_to_addr(iptr, dest));
}

static void __init test_branch_bform(void)
{
	int err;
	unsigned long addr;
	ppc_inst_t instr;
	u32 tmp[2];
	u32 *iptr = tmp;
	unsigned int flags;

	addr = (unsigned long)iptr;

	/* The simplest case, branch to self, no flags */
	check(instr_is_branch_bform(ppc_inst(0x40000000)));
	/* All bits of target set, and flags */
	check(instr_is_branch_bform(ppc_inst(0x43ffffff)));
	/* High bit of opcode set, which is wrong */
	check(!instr_is_branch_bform(ppc_inst(0xc3ffffff)));
	/* Middle bits of opcode set, which is wrong */
	check(!instr_is_branch_bform(ppc_inst(0x7bffffff)));

	/* Absolute conditional branch to 0x100 */
	ppc_inst_write(iptr, ppc_inst(0x43ff0103));
	check(instr_is_branch_to_addr(iptr, 0x100));
	/* Absolute conditional branch to 0x20fc */
	ppc_inst_write(iptr, ppc_inst(0x43ff20ff));
	check(instr_is_branch_to_addr(iptr, 0x20fc));
	/* Maximum positive relative conditional branch, + 32 KB - 4B */
	ppc_inst_write(iptr, ppc_inst(0x43ff7ffc));
	check(instr_is_branch_to_addr(iptr, addr + 0x7FFC));
	/* Smallest negative relative conditional branch, - 4B */
	ppc_inst_write(iptr, ppc_inst(0x43fffffc));
	check(instr_is_branch_to_addr(iptr, addr - 4));
	/* Largest negative relative conditional branch, - 32 KB */
	ppc_inst_write(iptr, ppc_inst(0x43ff8000));
	check(instr_is_branch_to_addr(iptr, addr - 0x8000));

	/* All condition code bits set & link */
	flags = 0x3ff000 | BRANCH_SET_LINK;

	/* Branch to self */
	err = create_cond_branch(&instr, iptr, addr, flags);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));

	/* Branch to self - 0x100 */
	err = create_cond_branch(&instr, iptr, addr - 0x100, flags);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x100));

	/* Branch to self + 0x100 */
	err = create_cond_branch(&instr, iptr, addr + 0x100, flags);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr + 0x100));

	/* Maximum relative negative offset, - 32 KB */
	err = create_cond_branch(&instr, iptr, addr - 0x8000, flags);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr - 0x8000));

	/* Out of range relative negative offset, - 32 KB + 4*/
	err = create_cond_branch(&instr, iptr, addr - 0x8004, flags);
	check(err);

	/* Out of range relative positive offset, + 32 KB */
	err = create_cond_branch(&instr, iptr, addr + 0x8000, flags);
	check(err);

	/* Unaligned target */
	err = create_cond_branch(&instr, iptr, addr + 3, flags);
	check(err);

	/* Check flags are masked correctly */
	err = create_cond_branch(&instr, iptr, addr, 0xFFFFFFFC);
	ppc_inst_write(iptr, instr);
	check(instr_is_branch_to_addr(iptr, addr));
	check(ppc_inst_equal(instr, ppc_inst(0x43FF0000)));
}

static void __init test_translate_branch(void)
{
	unsigned long addr;
	void *p, *q;
	ppc_inst_t instr;
	void *buf;

	buf = vmalloc(PAGE_ALIGN(0x2000000 + 1));
	check(buf);
	if (!buf)
		return;

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	create_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	check(instr_is_branch_to_addr(p, addr));
	q = p + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 MB */
	p = buf;
	addr = (unsigned long)p;
	create_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	q = buf + 0x2000000;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x4a000000)));

	/* Maximum positive case, move x to x - 32 MB + 4 */
	p = buf + 0x2000000;
	addr = (unsigned long)p;
	create_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x49fffffc)));

	/* Jump to x + 16 MB moved to x + 20 MB */
	p = buf;
	addr = 0x1000000 + (unsigned long)buf;
	create_branch(&instr, p, addr, BRANCH_SET_LINK);
	ppc_inst_write(p, instr);
	q = buf + 0x1400000;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 16 MB moved to x - 16 MB + 4 */
	p = buf + 0x1000000;
	addr = 0x2000000 + (unsigned long)buf;
	create_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));


	/* Conditional branch tests */

	/* Simple case, branch to self moved a little */
	p = buf;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	check(instr_is_branch_to_addr(p, addr));
	q = buf + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(q, addr));

	/* Maximum negative case, move b . to addr + 32 KB */
	p = buf;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0xFFFFFFFC);
	ppc_inst_write(p, instr);
	q = buf + 0x8000;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x43ff8000)));

	/* Maximum positive case, move x to x - 32 KB + 4 */
	p = buf + 0x8000;
	addr = (unsigned long)p;
	create_cond_branch(&instr, p, addr, 0xFFFFFFFC);
	ppc_inst_write(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));
	check(ppc_inst_equal(ppc_inst_read(q), ppc_inst(0x43ff7ffc)));

	/* Jump to x + 12 KB moved to x + 20 KB */
	p = buf;
	addr = 0x3000 + (unsigned long)buf;
	create_cond_branch(&instr, p, addr, BRANCH_SET_LINK);
	ppc_inst_write(p, instr);
	q = buf + 0x5000;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Jump to x + 8 KB moved to x - 8 KB + 4 */
	p = buf + 0x2000;
	addr = 0x4000 + (unsigned long)buf;
	create_cond_branch(&instr, p, addr, 0);
	ppc_inst_write(p, instr);
	q = buf + 4;
	translate_branch(&instr, q, p);
	ppc_inst_write(q, instr);
	check(instr_is_branch_to_addr(p, addr));
	check(instr_is_branch_to_addr(q, addr));

	/* Free the buffer we were using */
	vfree(buf);
}

static void __init test_prefixed_patching(void)
{
	u32 *iptr = (u32 *)ppc_function_entry(test_trampoline);
	u32 expected[2] = {OP_PREFIX << 26, 0};
	ppc_inst_t inst = ppc_inst_prefix(OP_PREFIX << 26, 0);

	if (!IS_ENABLED(CONFIG_PPC64))
		return;

	patch_instruction(iptr, inst);

	check(!memcmp(iptr, expected, sizeof(expected)));
}

static void __init test_multi_instruction_patching(void)
{
	u32 code[32];
	void *buf;
	u32 *addr32;
	u64 *addr64;
	ppc_inst_t inst64 = ppc_inst_prefix(OP_PREFIX << 26 | 3UL << 24, PPC_RAW_TRAP());
	u32 inst32 = PPC_RAW_NOP();

	buf = vzalloc(PAGE_SIZE * 8);
	check(buf);
	if (!buf)
		return;

	/* Test single page 32-bit repeated instruction */
	addr32 = buf + PAGE_SIZE;
	check(!patch_instructions(addr32 + 1, &inst32, 12, true));

	check(addr32[0] == 0);
	check(addr32[1] == inst32);
	check(addr32[2] == inst32);
	check(addr32[3] == inst32);
	check(addr32[4] == 0);

	/* Test single page 64-bit repeated instruction */
	if (IS_ENABLED(CONFIG_PPC64)) {
		check(ppc_inst_prefixed(inst64));

		addr64 = buf + PAGE_SIZE * 2;
		ppc_inst_write(code, inst64);
		check(!patch_instructions((u32 *)(addr64 + 1), code, 24, true));

		check(addr64[0] == 0);
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[1]), inst64));
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[2]), inst64));
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[3]), inst64));
		check(addr64[4] == 0);
	}

	/* Test single page memcpy */
	addr32 = buf + PAGE_SIZE * 3;

	for (int i = 0; i < ARRAY_SIZE(code); i++)
		code[i] = i + 1;

	check(!patch_instructions(addr32 + 1, code, sizeof(code), false));

	check(addr32[0] == 0);
	check(!memcmp(&addr32[1], code, sizeof(code)));
	check(addr32[ARRAY_SIZE(code) + 1] == 0);

	/* Test multipage 32-bit repeated instruction */
	addr32 = buf + PAGE_SIZE * 4 - 8;
	check(!patch_instructions(addr32 + 1, &inst32, 12, true));

	check(addr32[0] == 0);
	check(addr32[1] == inst32);
	check(addr32[2] == inst32);
	check(addr32[3] == inst32);
	check(addr32[4] == 0);

	/* Test multipage 64-bit repeated instruction */
	if (IS_ENABLED(CONFIG_PPC64)) {
		check(ppc_inst_prefixed(inst64));

		addr64 = buf + PAGE_SIZE * 5 - 8;
		ppc_inst_write(code, inst64);
		check(!patch_instructions((u32 *)(addr64 + 1), code, 24, true));

		check(addr64[0] == 0);
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[1]), inst64));
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[2]), inst64));
		check(ppc_inst_equal(ppc_inst_read((u32 *)&addr64[3]), inst64));
		check(addr64[4] == 0);
	}

	/* Test multipage memcpy */
	addr32 = buf + PAGE_SIZE * 6 - 12;

	for (int i = 0; i < ARRAY_SIZE(code); i++)
		code[i] = i + 1;

	check(!patch_instructions(addr32 + 1, code, sizeof(code), false));

	check(addr32[0] == 0);
	check(!memcmp(&addr32[1], code, sizeof(code)));
	check(addr32[ARRAY_SIZE(code) + 1] == 0);

	vfree(buf);
}

static void __init test_data_patching(void)
{
	void *buf;
	u32 *addr32;

	buf = vzalloc(PAGE_SIZE);
	check(buf);
	if (!buf)
		return;

	addr32 = buf + 128;

	addr32[1] = 0xA0A1A2A3;
	addr32[2] = 0xB0B1B2B3;

	check(!patch_uint(&addr32[1], 0xC0C1C2C3));

	check(addr32[0] == 0);
	check(addr32[1] == 0xC0C1C2C3);
	check(addr32[2] == 0xB0B1B2B3);
	check(addr32[3] == 0);

	/* Unaligned patch_ulong() should fail */
	if (IS_ENABLED(CONFIG_PPC64))
		check(patch_ulong(&addr32[1], 0xD0D1D2D3) == -EINVAL);

	check(!patch_ulong(&addr32[2], 0xD0D1D2D3));

	check(addr32[0] == 0);
	check(addr32[1] == 0xC0C1C2C3);
	check(*(unsigned long *)(&addr32[2]) == 0xD0D1D2D3);

	if (!IS_ENABLED(CONFIG_PPC64))
		check(addr32[3] == 0);

	check(addr32[4] == 0);

	vfree(buf);
}

static int __init test_code_patching(void)
{
	pr_info("Running code patching self-tests ...\n");

	test_branch_iform();
	test_branch_bform();
	test_create_function_call();
	test_translate_branch();
	test_prefixed_patching();
	test_multi_instruction_patching();
	test_data_patching();

	return 0;
}
late_initcall(test_code_patching);
