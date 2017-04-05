/*
 * Simple sanity test for emulate_step load/store instructions.
 *
 * Copyright IBM Corp. 2016
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) "emulate_step_test: " fmt

#include <linux/ptrace.h>
#include <asm/sstep.h>
#include <asm/ppc-opcode.h>

#define IMM_L(i)		((uintptr_t)(i) & 0xffff)

/*
 * Defined with TEST_ prefix so it does not conflict with other
 * definitions.
 */
#define TEST_LD(r, base, i)	(PPC_INST_LD | ___PPC_RT(r) |		\
					___PPC_RA(base) | IMM_L(i))
#define TEST_LWZ(r, base, i)	(PPC_INST_LWZ | ___PPC_RT(r) |		\
					___PPC_RA(base) | IMM_L(i))
#define TEST_LWZX(t, a, b)	(PPC_INST_LWZX | ___PPC_RT(t) |		\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_STD(r, base, i)	(PPC_INST_STD | ___PPC_RS(r) |		\
					___PPC_RA(base) | ((i) & 0xfffc))
#define TEST_LDARX(t, a, b, eh)	(PPC_INST_LDARX | ___PPC_RT(t) |	\
					___PPC_RA(a) | ___PPC_RB(b) |	\
					__PPC_EH(eh))
#define TEST_STDCX(s, a, b)	(PPC_INST_STDCX | ___PPC_RS(s) |	\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_LFSX(t, a, b)	(PPC_INST_LFSX | ___PPC_RT(t) |		\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_STFSX(s, a, b)	(PPC_INST_STFSX | ___PPC_RS(s) |	\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_LFDX(t, a, b)	(PPC_INST_LFDX | ___PPC_RT(t) |		\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_STFDX(s, a, b)	(PPC_INST_STFDX | ___PPC_RS(s) |	\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_LVX(t, a, b)	(PPC_INST_LVX | ___PPC_RT(t) |		\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_STVX(s, a, b)	(PPC_INST_STVX | ___PPC_RS(s) |		\
					___PPC_RA(a) | ___PPC_RB(b))
#define TEST_LXVD2X(s, a, b)	(PPC_INST_LXVD2X | VSX_XX1((s), R##a, R##b))
#define TEST_STXVD2X(s, a, b)	(PPC_INST_STXVD2X | VSX_XX1((s), R##a, R##b))


static void __init init_pt_regs(struct pt_regs *regs)
{
	static unsigned long msr;
	static bool msr_cached;

	memset(regs, 0, sizeof(struct pt_regs));

	if (likely(msr_cached)) {
		regs->msr = msr;
		return;
	}

	asm volatile("mfmsr %0" : "=r"(regs->msr));

	regs->msr |= MSR_FP;
	regs->msr |= MSR_VEC;
	regs->msr |= MSR_VSX;

	msr = regs->msr;
	msr_cached = true;
}

static void __init show_result(char *ins, char *result)
{
	pr_info("%-14s : %s\n", ins, result);
}

static void __init test_ld(void)
{
	struct pt_regs regs;
	unsigned long a = 0x23;
	int stepped = -1;

	init_pt_regs(&regs);
	regs.gpr[3] = (unsigned long) &a;

	/* ld r5, 0(r3) */
	stepped = emulate_step(&regs, TEST_LD(5, 3, 0));

	if (stepped == 1 && regs.gpr[5] == a)
		show_result("ld", "PASS");
	else
		show_result("ld", "FAIL");
}

static void __init test_lwz(void)
{
	struct pt_regs regs;
	unsigned int a = 0x4545;
	int stepped = -1;

	init_pt_regs(&regs);
	regs.gpr[3] = (unsigned long) &a;

	/* lwz r5, 0(r3) */
	stepped = emulate_step(&regs, TEST_LWZ(5, 3, 0));

	if (stepped == 1 && regs.gpr[5] == a)
		show_result("lwz", "PASS");
	else
		show_result("lwz", "FAIL");
}

static void __init test_lwzx(void)
{
	struct pt_regs regs;
	unsigned int a[3] = {0x0, 0x0, 0x1234};
	int stepped = -1;

	init_pt_regs(&regs);
	regs.gpr[3] = (unsigned long) a;
	regs.gpr[4] = 8;
	regs.gpr[5] = 0x8765;

	/* lwzx r5, r3, r4 */
	stepped = emulate_step(&regs, TEST_LWZX(5, 3, 4));
	if (stepped == 1 && regs.gpr[5] == a[2])
		show_result("lwzx", "PASS");
	else
		show_result("lwzx", "FAIL");
}

static void __init test_std(void)
{
	struct pt_regs regs;
	unsigned long a = 0x1234;
	int stepped = -1;

	init_pt_regs(&regs);
	regs.gpr[3] = (unsigned long) &a;
	regs.gpr[5] = 0x5678;

	/* std r5, 0(r3) */
	stepped = emulate_step(&regs, TEST_STD(5, 3, 0));
	if (stepped == 1 || regs.gpr[5] == a)
		show_result("std", "PASS");
	else
		show_result("std", "FAIL");
}

static void __init test_ldarx_stdcx(void)
{
	struct pt_regs regs;
	unsigned long a = 0x1234;
	int stepped = -1;
	unsigned long cr0_eq = 0x1 << 29; /* eq bit of CR0 */

	init_pt_regs(&regs);
	asm volatile("mfcr %0" : "=r"(regs.ccr));


	/*** ldarx ***/

	regs.gpr[3] = (unsigned long) &a;
	regs.gpr[4] = 0;
	regs.gpr[5] = 0x5678;

	/* ldarx r5, r3, r4, 0 */
	stepped = emulate_step(&regs, TEST_LDARX(5, 3, 4, 0));

	/*
	 * Don't touch 'a' here. Touching 'a' can do Load/store
	 * of 'a' which result in failure of subsequent stdcx.
	 * Instead, use hardcoded value for comparison.
	 */
	if (stepped <= 0 || regs.gpr[5] != 0x1234) {
		show_result("ldarx / stdcx.", "FAIL (ldarx)");
		return;
	}


	/*** stdcx. ***/

	regs.gpr[5] = 0x9ABC;

	/* stdcx. r5, r3, r4 */
	stepped = emulate_step(&regs, TEST_STDCX(5, 3, 4));

	/*
	 * Two possible scenarios that indicates successful emulation
	 * of stdcx. :
	 *  1. Reservation is active and store is performed. In this
	 *     case cr0.eq bit will be set to 1.
	 *  2. Reservation is not active and store is not performed.
	 *     In this case cr0.eq bit will be set to 0.
	 */
	if (stepped == 1 && ((regs.gpr[5] == a && (regs.ccr & cr0_eq))
			|| (regs.gpr[5] != a && !(regs.ccr & cr0_eq))))
		show_result("ldarx / stdcx.", "PASS");
	else
		show_result("ldarx / stdcx.", "FAIL (stdcx.)");
}

#ifdef CONFIG_PPC_FPU
static void __init test_lfsx_stfsx(void)
{
	struct pt_regs regs;
	union {
		float a;
		int b;
	} c;
	int cached_b;
	int stepped = -1;

	init_pt_regs(&regs);


	/*** lfsx ***/

	c.a = 123.45;
	cached_b = c.b;

	regs.gpr[3] = (unsigned long) &c.a;
	regs.gpr[4] = 0;

	/* lfsx frt10, r3, r4 */
	stepped = emulate_step(&regs, TEST_LFSX(10, 3, 4));

	if (stepped == 1)
		show_result("lfsx", "PASS");
	else
		show_result("lfsx", "FAIL");


	/*** stfsx ***/

	c.a = 678.91;

	/* stfsx frs10, r3, r4 */
	stepped = emulate_step(&regs, TEST_STFSX(10, 3, 4));

	if (stepped == 1 && c.b == cached_b)
		show_result("stfsx", "PASS");
	else
		show_result("stfsx", "FAIL");
}

static void __init test_lfdx_stfdx(void)
{
	struct pt_regs regs;
	union {
		double a;
		long b;
	} c;
	long cached_b;
	int stepped = -1;

	init_pt_regs(&regs);


	/*** lfdx ***/

	c.a = 123456.78;
	cached_b = c.b;

	regs.gpr[3] = (unsigned long) &c.a;
	regs.gpr[4] = 0;

	/* lfdx frt10, r3, r4 */
	stepped = emulate_step(&regs, TEST_LFDX(10, 3, 4));

	if (stepped == 1)
		show_result("lfdx", "PASS");
	else
		show_result("lfdx", "FAIL");


	/*** stfdx ***/

	c.a = 987654.32;

	/* stfdx frs10, r3, r4 */
	stepped = emulate_step(&regs, TEST_STFDX(10, 3, 4));

	if (stepped == 1 && c.b == cached_b)
		show_result("stfdx", "PASS");
	else
		show_result("stfdx", "FAIL");
}
#else
static void __init test_lfsx_stfsx(void)
{
	show_result("lfsx", "SKIP (CONFIG_PPC_FPU is not set)");
	show_result("stfsx", "SKIP (CONFIG_PPC_FPU is not set)");
}

static void __init test_lfdx_stfdx(void)
{
	show_result("lfdx", "SKIP (CONFIG_PPC_FPU is not set)");
	show_result("stfdx", "SKIP (CONFIG_PPC_FPU is not set)");
}
#endif /* CONFIG_PPC_FPU */

#ifdef CONFIG_ALTIVEC
static void __init test_lvx_stvx(void)
{
	struct pt_regs regs;
	union {
		vector128 a;
		u32 b[4];
	} c;
	u32 cached_b[4];
	int stepped = -1;

	init_pt_regs(&regs);


	/*** lvx ***/

	cached_b[0] = c.b[0] = 923745;
	cached_b[1] = c.b[1] = 2139478;
	cached_b[2] = c.b[2] = 9012;
	cached_b[3] = c.b[3] = 982134;

	regs.gpr[3] = (unsigned long) &c.a;
	regs.gpr[4] = 0;

	/* lvx vrt10, r3, r4 */
	stepped = emulate_step(&regs, TEST_LVX(10, 3, 4));

	if (stepped == 1)
		show_result("lvx", "PASS");
	else
		show_result("lvx", "FAIL");


	/*** stvx ***/

	c.b[0] = 4987513;
	c.b[1] = 84313948;
	c.b[2] = 71;
	c.b[3] = 498532;

	/* stvx vrs10, r3, r4 */
	stepped = emulate_step(&regs, TEST_STVX(10, 3, 4));

	if (stepped == 1 && cached_b[0] == c.b[0] && cached_b[1] == c.b[1] &&
	    cached_b[2] == c.b[2] && cached_b[3] == c.b[3])
		show_result("stvx", "PASS");
	else
		show_result("stvx", "FAIL");
}
#else
static void __init test_lvx_stvx(void)
{
	show_result("lvx", "SKIP (CONFIG_ALTIVEC is not set)");
	show_result("stvx", "SKIP (CONFIG_ALTIVEC is not set)");
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_VSX
static void __init test_lxvd2x_stxvd2x(void)
{
	struct pt_regs regs;
	union {
		vector128 a;
		u32 b[4];
	} c;
	u32 cached_b[4];
	int stepped = -1;

	init_pt_regs(&regs);


	/*** lxvd2x ***/

	cached_b[0] = c.b[0] = 18233;
	cached_b[1] = c.b[1] = 34863571;
	cached_b[2] = c.b[2] = 834;
	cached_b[3] = c.b[3] = 6138911;

	regs.gpr[3] = (unsigned long) &c.a;
	regs.gpr[4] = 0;

	/* lxvd2x vsr39, r3, r4 */
	stepped = emulate_step(&regs, TEST_LXVD2X(39, 3, 4));

	if (stepped == 1)
		show_result("lxvd2x", "PASS");
	else
		show_result("lxvd2x", "FAIL");


	/*** stxvd2x ***/

	c.b[0] = 21379463;
	c.b[1] = 87;
	c.b[2] = 374234;
	c.b[3] = 4;

	/* stxvd2x vsr39, r3, r4 */
	stepped = emulate_step(&regs, TEST_STXVD2X(39, 3, 4));

	if (stepped == 1 && cached_b[0] == c.b[0] && cached_b[1] == c.b[1] &&
	    cached_b[2] == c.b[2] && cached_b[3] == c.b[3])
		show_result("stxvd2x", "PASS");
	else
		show_result("stxvd2x", "FAIL");
}
#else
static void __init test_lxvd2x_stxvd2x(void)
{
	show_result("lxvd2x", "SKIP (CONFIG_VSX is not set)");
	show_result("stxvd2x", "SKIP (CONFIG_VSX is not set)");
}
#endif /* CONFIG_VSX */

static int __init test_emulate_step(void)
{
	test_ld();
	test_lwz();
	test_lwzx();
	test_std();
	test_ldarx_stdcx();
	test_lfsx_stfsx();
	test_lfdx_stfdx();
	test_lvx_stvx();
	test_lxvd2x_stxvd2x();

	return 0;
}
late_initcall(test_emulate_step);
