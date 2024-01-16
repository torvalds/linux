// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/kernel/kprobes-test.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 */

/*
 * This file contains test code for ARM kprobes.
 *
 * The top level function run_all_tests() executes tests for all of the
 * supported instruction sets: ARM, 16-bit Thumb, and 32-bit Thumb. These tests
 * fall into two categories; run_api_tests() checks basic functionality of the
 * kprobes API, and run_test_cases() is a comprehensive test for kprobes
 * instruction decoding and simulation.
 *
 * run_test_cases() first checks the kprobes decoding table for self consistency
 * (using table_test()) then executes a series of test cases for each of the CPU
 * instruction forms. coverage_start() and coverage_end() are used to verify
 * that these test cases cover all of the possible combinations of instructions
 * described by the kprobes decoding tables.
 *
 * The individual test cases are in kprobes-test-arm.c and kprobes-test-thumb.c
 * which use the macros defined in kprobes-test.h. The rest of this
 * documentation will describe the operation of the framework used by these
 * test cases.
 */

/*
 * TESTING METHODOLOGY
 * -------------------
 *
 * The methodology used to test an ARM instruction 'test_insn' is to use
 * inline assembler like:
 *
 * test_before: nop
 * test_case:	test_insn
 * test_after:	nop
 *
 * When the test case is run a kprobe is placed of each nop. The
 * post-handler of the test_before probe is used to modify the saved CPU
 * register context to that which we require for the test case. The
 * pre-handler of the of the test_after probe saves a copy of the CPU
 * register context. In this way we can execute test_insn with a specific
 * register context and see the results afterwards.
 *
 * To actually test the kprobes instruction emulation we perform the above
 * step a second time but with an additional kprobe on the test_case
 * instruction itself. If the emulation is accurate then the results seen
 * by the test_after probe will be identical to the first run which didn't
 * have a probe on test_case.
 *
 * Each test case is run several times with a variety of variations in the
 * flags value of stored in CPSR, and for Thumb code, different ITState.
 *
 * For instructions which can modify PC, a second test_after probe is used
 * like this:
 *
 * test_before: nop
 * test_case:	test_insn
 * test_after:	nop
 *		b test_done
 * test_after2: nop
 * test_done:
 *
 * The test case is constructed such that test_insn branches to
 * test_after2, or, if testing a conditional instruction, it may just
 * continue to test_after. The probes inserted at both locations let us
 * determine which happened. A similar approach is used for testing
 * backwards branches...
 *
 *		b test_before
 *		b test_done  @ helps to cope with off by 1 branches
 * test_after2: nop
 *		b test_done
 * test_before: nop
 * test_case:	test_insn
 * test_after:	nop
 * test_done:
 *
 * The macros used to generate the assembler instructions describe above
 * are TEST_INSTRUCTION, TEST_BRANCH_F (branch forwards) and TEST_BRANCH_B
 * (branch backwards). In these, the local variables numbered 1, 50, 2 and
 * 99 represent: test_before, test_case, test_after2 and test_done.
 *
 * FRAMEWORK
 * ---------
 *
 * Each test case is wrapped between the pair of macros TESTCASE_START and
 * TESTCASE_END. As well as performing the inline assembler boilerplate,
 * these call out to the kprobes_test_case_start() and
 * kprobes_test_case_end() functions which drive the execution of the test
 * case. The specific arguments to use for each test case are stored as
 * inline data constructed using the various TEST_ARG_* macros. Putting
 * this all together, a simple test case may look like:
 *
 *	TESTCASE_START("Testing mov r0, r7")
 *	TEST_ARG_REG(7, 0x12345678) // Set r7=0x12345678
 *	TEST_ARG_END("")
 *	TEST_INSTRUCTION("mov r0, r7")
 *	TESTCASE_END
 *
 * Note, in practice the single convenience macro TEST_R would be used for this
 * instead.
 *
 * The above would expand to assembler looking something like:
 *
 *	@ TESTCASE_START
 *	bl	__kprobes_test_case_start
 *	.pushsection .rodata
 *	"10:
 *	.ascii "mov r0, r7"	@ text title for test case
 *	.byte	0
 *	.popsection
 *	@ start of inline data...
 *	.word	10b		@ pointer to title in .rodata section
 *
 *	@ TEST_ARG_REG
 *	.byte	ARG_TYPE_REG
 *	.byte	7
 *	.short	0
 *	.word	0x1234567
 *
 *	@ TEST_ARG_END
 *	.byte	ARG_TYPE_END
 *	.byte	TEST_ISA	@ flags, including ISA being tested
 *	.short	50f-0f		@ offset of 'test_before'
 *	.short	2f-0f		@ offset of 'test_after2' (if relevent)
 *	.short	99f-0f		@ offset of 'test_done'
 *	@ start of test case code...
 *	0:
 *	.code	TEST_ISA	@ switch to ISA being tested
 *
 *	@ TEST_INSTRUCTION
 *	50:	nop		@ location for 'test_before' probe
 *	1:	mov r0, r7	@ the test case instruction 'test_insn'
 *		nop		@ location for 'test_after' probe
 *
 *	// TESTCASE_END
 *	2:
 *	99:	bl __kprobes_test_case_end_##TEST_ISA
 *	.code	NONMAL_ISA
 *
 * When the above is execute the following happens...
 *
 * __kprobes_test_case_start() is an assembler wrapper which sets up space
 * for a stack buffer and calls the C function kprobes_test_case_start().
 * This C function will do some initial processing of the inline data and
 * setup some global state. It then inserts the test_before and test_after
 * kprobes and returns a value which causes the assembler wrapper to jump
 * to the start of the test case code, (local label '0').
 *
 * When the test case code executes, the test_before probe will be hit and
 * test_before_post_handler will call setup_test_context(). This fills the
 * stack buffer and CPU registers with a test pattern and then processes
 * the test case arguments. In our example there is one TEST_ARG_REG which
 * indicates that R7 should be loaded with the value 0x12345678.
 *
 * When the test_before probe ends, the test case continues and executes
 * the "mov r0, r7" instruction. It then hits the test_after probe and the
 * pre-handler for this (test_after_pre_handler) will save a copy of the
 * CPU register context. This should now have R0 holding the same value as
 * R7.
 *
 * Finally we get to the call to __kprobes_test_case_end_{32,16}. This is
 * an assembler wrapper which switches back to the ISA used by the test
 * code and calls the C function kprobes_test_case_end().
 *
 * For each run through the test case, test_case_run_count is incremented
 * by one. For even runs, kprobes_test_case_end() saves a copy of the
 * register and stack buffer contents from the test case just run. It then
 * inserts a kprobe on the test case instruction 'test_insn' and returns a
 * value to cause the test case code to be re-run.
 *
 * For odd numbered runs, kprobes_test_case_end() compares the register and
 * stack buffer contents to those that were saved on the previous even
 * numbered run (the one without the kprobe on test_insn). These should be
 * the same if the kprobe instruction simulation routine is correct.
 *
 * The pair of test case runs is repeated with different combinations of
 * flag values in CPSR and, for Thumb, different ITState. This is
 * controlled by test_context_cpsr().
 *
 * BUILDING TEST CASES
 * -------------------
 *
 *
 * As an aid to building test cases, the stack buffer is initialised with
 * some special values:
 *
 *   [SP+13*4]	Contains SP+120. This can be used to test instructions
 *		which load a value into SP.
 *
 *   [SP+15*4]	When testing branching instructions using TEST_BRANCH_{F,B},
 *		this holds the target address of the branch, 'test_after2'.
 *		This can be used to test instructions which load a PC value
 *		from memory.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/kprobes.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/bug.h>
#include <asm/opcodes.h>

#include "core.h"
#include "test-core.h"
#include "../decode-arm.h"
#include "../decode-thumb.h"


#define BENCHMARKING	1


/*
 * Test basic API
 */

static bool test_regs_ok;
static int test_func_instance;
static int pre_handler_called;
static int post_handler_called;
static int kretprobe_handler_called;
static int tests_failed;

#define FUNC_ARG1 0x12345678
#define FUNC_ARG2 0xabcdef


#ifndef CONFIG_THUMB2_KERNEL

#define RET(reg)	"mov	pc, "#reg

long arm_func(long r0, long r1);

static void __used __naked __arm_kprobes_test_func(void)
{
	__asm__ __volatile__ (
		".arm					\n\t"
		".type arm_func, %%function		\n\t"
		"arm_func:				\n\t"
		"adds	r0, r0, r1			\n\t"
		"mov	pc, lr				\n\t"
		".code "NORMAL_ISA	 /* Back to Thumb if necessary */
		: : : "r0", "r1", "cc"
	);
}

#else /* CONFIG_THUMB2_KERNEL */

#define RET(reg)	"bx	"#reg

long thumb16_func(long r0, long r1);
long thumb32even_func(long r0, long r1);
long thumb32odd_func(long r0, long r1);

static void __used __naked __thumb_kprobes_test_funcs(void)
{
	__asm__ __volatile__ (
		".type thumb16_func, %%function		\n\t"
		"thumb16_func:				\n\t"
		"adds.n	r0, r0, r1			\n\t"
		"bx	lr				\n\t"

		".align					\n\t"
		".type thumb32even_func, %%function	\n\t"
		"thumb32even_func:			\n\t"
		"adds.w	r0, r0, r1			\n\t"
		"bx	lr				\n\t"

		".align					\n\t"
		"nop.n					\n\t"
		".type thumb32odd_func, %%function	\n\t"
		"thumb32odd_func:			\n\t"
		"adds.w	r0, r0, r1			\n\t"
		"bx	lr				\n\t"

		: : : "r0", "r1", "cc"
	);
}

#endif /* CONFIG_THUMB2_KERNEL */


static int call_test_func(long (*func)(long, long), bool check_test_regs)
{
	long ret;

	++test_func_instance;
	test_regs_ok = false;

	ret = (*func)(FUNC_ARG1, FUNC_ARG2);
	if (ret != FUNC_ARG1 + FUNC_ARG2) {
		pr_err("FAIL: call_test_func: func returned %lx\n", ret);
		return false;
	}

	if (check_test_regs && !test_regs_ok) {
		pr_err("FAIL: test regs not OK\n");
		return false;
	}

	return true;
}

static int __kprobes pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	pre_handler_called = test_func_instance;
	if (regs->ARM_r0 == FUNC_ARG1 && regs->ARM_r1 == FUNC_ARG2)
		test_regs_ok = true;
	return 0;
}

static void __kprobes post_handler(struct kprobe *p, struct pt_regs *regs,
				unsigned long flags)
{
	post_handler_called = test_func_instance;
	if (regs->ARM_r0 != FUNC_ARG1 + FUNC_ARG2 || regs->ARM_r1 != FUNC_ARG2)
		test_regs_ok = false;
}

static struct kprobe the_kprobe = {
	.addr		= 0,
	.pre_handler	= pre_handler,
	.post_handler	= post_handler
};

static int test_kprobe(long (*func)(long, long))
{
	int ret;

	the_kprobe.addr = (kprobe_opcode_t *)func;
	ret = register_kprobe(&the_kprobe);
	if (ret < 0) {
		pr_err("FAIL: register_kprobe failed with %d\n", ret);
		return ret;
	}

	ret = call_test_func(func, true);

	unregister_kprobe(&the_kprobe);
	the_kprobe.flags = 0; /* Clear disable flag to allow reuse */

	if (!ret)
		return -EINVAL;
	if (pre_handler_called != test_func_instance) {
		pr_err("FAIL: kprobe pre_handler not called\n");
		return -EINVAL;
	}
	if (post_handler_called != test_func_instance) {
		pr_err("FAIL: kprobe post_handler not called\n");
		return -EINVAL;
	}
	if (!call_test_func(func, false))
		return -EINVAL;
	if (pre_handler_called == test_func_instance ||
				post_handler_called == test_func_instance) {
		pr_err("FAIL: probe called after unregistering\n");
		return -EINVAL;
	}

	return 0;
}

static int __kprobes
kretprobe_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	kretprobe_handler_called = test_func_instance;
	if (regs_return_value(regs) == FUNC_ARG1 + FUNC_ARG2)
		test_regs_ok = true;
	return 0;
}

static struct kretprobe the_kretprobe = {
	.handler	= kretprobe_handler,
};

static int test_kretprobe(long (*func)(long, long))
{
	int ret;

	the_kretprobe.kp.addr = (kprobe_opcode_t *)func;
	ret = register_kretprobe(&the_kretprobe);
	if (ret < 0) {
		pr_err("FAIL: register_kretprobe failed with %d\n", ret);
		return ret;
	}

	ret = call_test_func(func, true);

	unregister_kretprobe(&the_kretprobe);
	the_kretprobe.kp.flags = 0; /* Clear disable flag to allow reuse */

	if (!ret)
		return -EINVAL;
	if (kretprobe_handler_called != test_func_instance) {
		pr_err("FAIL: kretprobe handler not called\n");
		return -EINVAL;
	}
	if (!call_test_func(func, false))
		return -EINVAL;
	if (kretprobe_handler_called == test_func_instance) {
		pr_err("FAIL: kretprobe called after unregistering\n");
		return -EINVAL;
	}

	return 0;
}

static int run_api_tests(long (*func)(long, long))
{
	int ret;

	pr_info("    kprobe\n");
	ret = test_kprobe(func);
	if (ret < 0)
		return ret;

	pr_info("    kretprobe\n");
	ret = test_kretprobe(func);
	if (ret < 0)
		return ret;

	return 0;
}


/*
 * Benchmarking
 */

#if BENCHMARKING

static void __naked benchmark_nop(void)
{
	__asm__ __volatile__ (
		"nop		\n\t"
		RET(lr)"	\n\t"
	);
}

#ifdef CONFIG_THUMB2_KERNEL
#define wide ".w"
#else
#define wide
#endif

static void __naked benchmark_pushpop1(void)
{
	__asm__ __volatile__ (
		"stmdb"wide"	sp!, {r3-r11,lr}  \n\t"
		"ldmia"wide"	sp!, {r3-r11,pc}"
	);
}

static void __naked benchmark_pushpop2(void)
{
	__asm__ __volatile__ (
		"stmdb"wide"	sp!, {r0-r8,lr}  \n\t"
		"ldmia"wide"	sp!, {r0-r8,pc}"
	);
}

static void __naked benchmark_pushpop3(void)
{
	__asm__ __volatile__ (
		"stmdb"wide"	sp!, {r4,lr}  \n\t"
		"ldmia"wide"	sp!, {r4,pc}"
	);
}

static void __naked benchmark_pushpop4(void)
{
	__asm__ __volatile__ (
		"stmdb"wide"	sp!, {r0,lr}  \n\t"
		"ldmia"wide"	sp!, {r0,pc}"
	);
}


#ifdef CONFIG_THUMB2_KERNEL

static void __naked benchmark_pushpop_thumb(void)
{
	__asm__ __volatile__ (
		"push.n	{r0-r7,lr}  \n\t"
		"pop.n	{r0-r7,pc}"
	);
}

#endif

static int __kprobes
benchmark_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	return 0;
}

static int benchmark(void(*fn)(void))
{
	unsigned n, i, t, t0;

	for (n = 1000; ; n *= 2) {
		t0 = sched_clock();
		for (i = n; i > 0; --i)
			fn();
		t = sched_clock() - t0;
		if (t >= 250000000)
			break; /* Stop once we took more than 0.25 seconds */
	}
	return t / n; /* Time for one iteration in nanoseconds */
};

static int kprobe_benchmark(void(*fn)(void), unsigned offset)
{
	struct kprobe k = {
		.addr		= (kprobe_opcode_t *)((uintptr_t)fn + offset),
		.pre_handler	= benchmark_pre_handler,
	};

	int ret = register_kprobe(&k);
	if (ret < 0) {
		pr_err("FAIL: register_kprobe failed with %d\n", ret);
		return ret;
	}

	ret = benchmark(fn);

	unregister_kprobe(&k);
	return ret;
};

struct benchmarks {
	void		(*fn)(void);
	unsigned	offset;
	const char	*title;
};

static int run_benchmarks(void)
{
	int ret;
	struct benchmarks list[] = {
		{&benchmark_nop, 0, "nop"},
		/*
		 * benchmark_pushpop{1,3} will have the optimised
		 * instruction emulation, whilst benchmark_pushpop{2,4} will
		 * be the equivalent unoptimised instructions.
		 */
		{&benchmark_pushpop1, 0, "stmdb	sp!, {r3-r11,lr}"},
		{&benchmark_pushpop1, 4, "ldmia	sp!, {r3-r11,pc}"},
		{&benchmark_pushpop2, 0, "stmdb	sp!, {r0-r8,lr}"},
		{&benchmark_pushpop2, 4, "ldmia	sp!, {r0-r8,pc}"},
		{&benchmark_pushpop3, 0, "stmdb	sp!, {r4,lr}"},
		{&benchmark_pushpop3, 4, "ldmia	sp!, {r4,pc}"},
		{&benchmark_pushpop4, 0, "stmdb	sp!, {r0,lr}"},
		{&benchmark_pushpop4, 4, "ldmia	sp!, {r0,pc}"},
#ifdef CONFIG_THUMB2_KERNEL
		{&benchmark_pushpop_thumb, 0, "push.n	{r0-r7,lr}"},
		{&benchmark_pushpop_thumb, 2, "pop.n	{r0-r7,pc}"},
#endif
		{0}
	};

	struct benchmarks *b;
	for (b = list; b->fn; ++b) {
		ret = kprobe_benchmark(b->fn, b->offset);
		if (ret < 0)
			return ret;
		pr_info("    %dns for kprobe %s\n", ret, b->title);
	}

	pr_info("\n");
	return 0;
}

#endif /* BENCHMARKING */


/*
 * Decoding table self-consistency tests
 */

static const int decode_struct_sizes[NUM_DECODE_TYPES] = {
	[DECODE_TYPE_TABLE]	= sizeof(struct decode_table),
	[DECODE_TYPE_CUSTOM]	= sizeof(struct decode_custom),
	[DECODE_TYPE_SIMULATE]	= sizeof(struct decode_simulate),
	[DECODE_TYPE_EMULATE]	= sizeof(struct decode_emulate),
	[DECODE_TYPE_OR]	= sizeof(struct decode_or),
	[DECODE_TYPE_REJECT]	= sizeof(struct decode_reject)
};

static int table_iter(const union decode_item *table,
			int (*fn)(const struct decode_header *, void *),
			void *args)
{
	const struct decode_header *h = (struct decode_header *)table;
	int result;

	for (;;) {
		enum decode_type type = h->type_regs.bits & DECODE_TYPE_MASK;

		if (type == DECODE_TYPE_END)
			return 0;

		result = fn(h, args);
		if (result)
			return result;

		h = (struct decode_header *)
			((uintptr_t)h + decode_struct_sizes[type]);

	}
}

static int table_test_fail(const struct decode_header *h, const char* message)
{

	pr_err("FAIL: kprobes test failure \"%s\" (mask %08x, value %08x)\n",
					message, h->mask.bits, h->value.bits);
	return -EINVAL;
}

struct table_test_args {
	const union decode_item *root_table;
	u32			parent_mask;
	u32			parent_value;
};

static int table_test_fn(const struct decode_header *h, void *args)
{
	struct table_test_args *a = (struct table_test_args *)args;
	enum decode_type type = h->type_regs.bits & DECODE_TYPE_MASK;

	if (h->value.bits & ~h->mask.bits)
		return table_test_fail(h, "Match value has bits not in mask");

	if ((h->mask.bits & a->parent_mask) != a->parent_mask)
		return table_test_fail(h, "Mask has bits not in parent mask");

	if ((h->value.bits ^ a->parent_value) & a->parent_mask)
		return table_test_fail(h, "Value is inconsistent with parent");

	if (type == DECODE_TYPE_TABLE) {
		struct decode_table *d = (struct decode_table *)h;
		struct table_test_args args2 = *a;
		args2.parent_mask = h->mask.bits;
		args2.parent_value = h->value.bits;
		return table_iter(d->table.table, table_test_fn, &args2);
	}

	return 0;
}

static int table_test(const union decode_item *table)
{
	struct table_test_args args = {
		.root_table	= table,
		.parent_mask	= 0,
		.parent_value	= 0
	};
	return table_iter(args.root_table, table_test_fn, &args);
}


/*
 * Decoding table test coverage analysis
 *
 * coverage_start() builds a coverage_table which contains a list of
 * coverage_entry's to match each entry in the specified kprobes instruction
 * decoding table.
 *
 * When test cases are run, coverage_add() is called to process each case.
 * This looks up the corresponding entry in the coverage_table and sets it as
 * being matched, as well as clearing the regs flag appropriate for the test.
 *
 * After all test cases have been run, coverage_end() is called to check that
 * all entries in coverage_table have been matched and that all regs flags are
 * cleared. I.e. that all possible combinations of instructions described by
 * the kprobes decoding tables have had a test case executed for them.
 */

bool coverage_fail;

#define MAX_COVERAGE_ENTRIES 256

struct coverage_entry {
	const struct decode_header	*header;
	unsigned			regs;
	unsigned			nesting;
	char				matched;
};

struct coverage_table {
	struct coverage_entry	*base;
	unsigned		num_entries;
	unsigned		nesting;
};

struct coverage_table coverage;

#define COVERAGE_ANY_REG	(1<<0)
#define COVERAGE_SP		(1<<1)
#define COVERAGE_PC		(1<<2)
#define COVERAGE_PCWB		(1<<3)

static const char coverage_register_lookup[16] = {
	[REG_TYPE_ANY]		= COVERAGE_ANY_REG | COVERAGE_SP | COVERAGE_PC,
	[REG_TYPE_SAMEAS16]	= COVERAGE_ANY_REG,
	[REG_TYPE_SP]		= COVERAGE_SP,
	[REG_TYPE_PC]		= COVERAGE_PC,
	[REG_TYPE_NOSP]		= COVERAGE_ANY_REG | COVERAGE_SP,
	[REG_TYPE_NOSPPC]	= COVERAGE_ANY_REG | COVERAGE_SP | COVERAGE_PC,
	[REG_TYPE_NOPC]		= COVERAGE_ANY_REG | COVERAGE_PC,
	[REG_TYPE_NOPCWB]	= COVERAGE_ANY_REG | COVERAGE_PC | COVERAGE_PCWB,
	[REG_TYPE_NOPCX]	= COVERAGE_ANY_REG,
	[REG_TYPE_NOSPPCX]	= COVERAGE_ANY_REG | COVERAGE_SP,
};

static unsigned coverage_start_registers(const struct decode_header *h)
{
	unsigned regs = 0;
	int i;
	for (i = 0; i < 20; i += 4) {
		int r = (h->type_regs.bits >> (DECODE_TYPE_BITS + i)) & 0xf;
		regs |= coverage_register_lookup[r] << i;
	}
	return regs;
}

static int coverage_start_fn(const struct decode_header *h, void *args)
{
	struct coverage_table *coverage = (struct coverage_table *)args;
	enum decode_type type = h->type_regs.bits & DECODE_TYPE_MASK;
	struct coverage_entry *entry = coverage->base + coverage->num_entries;

	if (coverage->num_entries == MAX_COVERAGE_ENTRIES - 1) {
		pr_err("FAIL: Out of space for test coverage data");
		return -ENOMEM;
	}

	++coverage->num_entries;

	entry->header = h;
	entry->regs = coverage_start_registers(h);
	entry->nesting = coverage->nesting;
	entry->matched = false;

	if (type == DECODE_TYPE_TABLE) {
		struct decode_table *d = (struct decode_table *)h;
		int ret;
		++coverage->nesting;
		ret = table_iter(d->table.table, coverage_start_fn, coverage);
		--coverage->nesting;
		return ret;
	}

	return 0;
}

static int coverage_start(const union decode_item *table)
{
	coverage.base = kmalloc_array(MAX_COVERAGE_ENTRIES,
				      sizeof(struct coverage_entry),
				      GFP_KERNEL);
	coverage.num_entries = 0;
	coverage.nesting = 0;
	return table_iter(table, coverage_start_fn, &coverage);
}

static void
coverage_add_registers(struct coverage_entry *entry, kprobe_opcode_t insn)
{
	int regs = entry->header->type_regs.bits >> DECODE_TYPE_BITS;
	int i;
	for (i = 0; i < 20; i += 4) {
		enum decode_reg_type reg_type = (regs >> i) & 0xf;
		int reg = (insn >> i) & 0xf;
		int flag;

		if (!reg_type)
			continue;

		if (reg == 13)
			flag = COVERAGE_SP;
		else if (reg == 15)
			flag = COVERAGE_PC;
		else
			flag = COVERAGE_ANY_REG;
		entry->regs &= ~(flag << i);

		switch (reg_type) {

		case REG_TYPE_NONE:
		case REG_TYPE_ANY:
		case REG_TYPE_SAMEAS16:
			break;

		case REG_TYPE_SP:
			if (reg != 13)
				return;
			break;

		case REG_TYPE_PC:
			if (reg != 15)
				return;
			break;

		case REG_TYPE_NOSP:
			if (reg == 13)
				return;
			break;

		case REG_TYPE_NOSPPC:
		case REG_TYPE_NOSPPCX:
			if (reg == 13 || reg == 15)
				return;
			break;

		case REG_TYPE_NOPCWB:
			if (!is_writeback(insn))
				break;
			if (reg == 15) {
				entry->regs &= ~(COVERAGE_PCWB << i);
				return;
			}
			break;

		case REG_TYPE_NOPC:
		case REG_TYPE_NOPCX:
			if (reg == 15)
				return;
			break;
		}

	}
}

static void coverage_add(kprobe_opcode_t insn)
{
	struct coverage_entry *entry = coverage.base;
	struct coverage_entry *end = coverage.base + coverage.num_entries;
	bool matched = false;
	unsigned nesting = 0;

	for (; entry < end; ++entry) {
		const struct decode_header *h = entry->header;
		enum decode_type type = h->type_regs.bits & DECODE_TYPE_MASK;

		if (entry->nesting > nesting)
			continue; /* Skip sub-table we didn't match */

		if (entry->nesting < nesting)
			break; /* End of sub-table we were scanning */

		if (!matched) {
			if ((insn & h->mask.bits) != h->value.bits)
				continue;
			entry->matched = true;
		}

		switch (type) {

		case DECODE_TYPE_TABLE:
			++nesting;
			break;

		case DECODE_TYPE_CUSTOM:
		case DECODE_TYPE_SIMULATE:
		case DECODE_TYPE_EMULATE:
			coverage_add_registers(entry, insn);
			return;

		case DECODE_TYPE_OR:
			matched = true;
			break;

		case DECODE_TYPE_REJECT:
		default:
			return;
		}

	}
}

static void coverage_end(void)
{
	struct coverage_entry *entry = coverage.base;
	struct coverage_entry *end = coverage.base + coverage.num_entries;

	for (; entry < end; ++entry) {
		u32 mask = entry->header->mask.bits;
		u32 value = entry->header->value.bits;

		if (entry->regs) {
			pr_err("FAIL: Register test coverage missing for %08x %08x (%05x)\n",
				mask, value, entry->regs);
			coverage_fail = true;
		}
		if (!entry->matched) {
			pr_err("FAIL: Test coverage entry missing for %08x %08x\n",
				mask, value);
			coverage_fail = true;
		}
	}

	kfree(coverage.base);
}


/*
 * Framework for instruction set test cases
 */

void __naked __kprobes_test_case_start(void)
{
	__asm__ __volatile__ (
		"mov	r2, sp					\n\t"
		"bic	r3, r2, #7				\n\t"
		"mov	sp, r3					\n\t"
		"stmdb	sp!, {r2-r11}				\n\t"
		"sub	sp, sp, #"__stringify(TEST_MEMORY_SIZE)"\n\t"
		"bic	r0, lr, #1  @ r0 = inline data		\n\t"
		"mov	r1, sp					\n\t"
		"bl	kprobes_test_case_start			\n\t"
		RET(r0)"					\n\t"
	);
}

#ifndef CONFIG_THUMB2_KERNEL

void __naked __kprobes_test_case_end_32(void)
{
	__asm__ __volatile__ (
		"mov	r4, lr					\n\t"
		"bl	kprobes_test_case_end			\n\t"
		"cmp	r0, #0					\n\t"
		"movne	pc, r0					\n\t"
		"mov	r0, r4					\n\t"
		"add	sp, sp, #"__stringify(TEST_MEMORY_SIZE)"\n\t"
		"ldmia	sp!, {r2-r11}				\n\t"
		"mov	sp, r2					\n\t"
		"mov	pc, r0					\n\t"
	);
}

#else /* CONFIG_THUMB2_KERNEL */

void __naked __kprobes_test_case_end_16(void)
{
	__asm__ __volatile__ (
		"mov	r4, lr					\n\t"
		"bl	kprobes_test_case_end			\n\t"
		"cmp	r0, #0					\n\t"
		"bxne	r0					\n\t"
		"mov	r0, r4					\n\t"
		"add	sp, sp, #"__stringify(TEST_MEMORY_SIZE)"\n\t"
		"ldmia	sp!, {r2-r11}				\n\t"
		"mov	sp, r2					\n\t"
		"bx	r0					\n\t"
	);
}

void __naked __kprobes_test_case_end_32(void)
{
	__asm__ __volatile__ (
		".arm						\n\t"
		"orr	lr, lr, #1  @ will return to Thumb code	\n\t"
		"ldr	pc, 1f					\n\t"
		"1:						\n\t"
		".word	__kprobes_test_case_end_16		\n\t"
	);
}

#endif


int kprobe_test_flags;
int kprobe_test_cc_position;

static int test_try_count;
static int test_pass_count;
static int test_fail_count;

static struct pt_regs initial_regs;
static struct pt_regs expected_regs;
static struct pt_regs result_regs;

static u32 expected_memory[TEST_MEMORY_SIZE/sizeof(u32)];

static const char *current_title;
static struct test_arg *current_args;
static u32 *current_stack;
static uintptr_t current_branch_target;

static uintptr_t current_code_start;
static kprobe_opcode_t current_instruction;


#define TEST_CASE_PASSED -1
#define TEST_CASE_FAILED -2

static int test_case_run_count;
static bool test_case_is_thumb;
static int test_instance;

static unsigned long test_check_cc(int cc, unsigned long cpsr)
{
	int ret = arm_check_condition(cc << 28, cpsr);

	return (ret != ARM_OPCODE_CONDTEST_FAIL);
}

static int is_last_scenario;
static int probe_should_run; /* 0 = no, 1 = yes, -1 = unknown */
static int memory_needs_checking;

static unsigned long test_context_cpsr(int scenario)
{
	unsigned long cpsr;

	probe_should_run = 1;

	/* Default case is that we cycle through 16 combinations of flags */
	cpsr  = (scenario & 0xf) << 28; /* N,Z,C,V flags */
	cpsr |= (scenario & 0xf) << 16; /* GE flags */
	cpsr |= (scenario & 0x1) << 27; /* Toggle Q flag */

	if (!test_case_is_thumb) {
		/* Testing ARM code */
		int cc = current_instruction >> 28;

		probe_should_run = test_check_cc(cc, cpsr) != 0;
		if (scenario == 15)
			is_last_scenario = true;

	} else if (kprobe_test_flags & TEST_FLAG_NO_ITBLOCK) {
		/* Testing Thumb code without setting ITSTATE */
		if (kprobe_test_cc_position) {
			int cc = (current_instruction >> kprobe_test_cc_position) & 0xf;
			probe_should_run = test_check_cc(cc, cpsr) != 0;
		}

		if (scenario == 15)
			is_last_scenario = true;

	} else if (kprobe_test_flags & TEST_FLAG_FULL_ITBLOCK) {
		/* Testing Thumb code with all combinations of ITSTATE */
		unsigned x = (scenario >> 4);
		unsigned cond_base = x % 7; /* ITSTATE<7:5> */
		unsigned mask = x / 7 + 2;  /* ITSTATE<4:0>, bits reversed */

		if (mask > 0x1f) {
			/* Finish by testing state from instruction 'itt al' */
			cond_base = 7;
			mask = 0x4;
			if ((scenario & 0xf) == 0xf)
				is_last_scenario = true;
		}

		cpsr |= cond_base << 13;	/* ITSTATE<7:5> */
		cpsr |= (mask & 0x1) << 12;	/* ITSTATE<4> */
		cpsr |= (mask & 0x2) << 10;	/* ITSTATE<3> */
		cpsr |= (mask & 0x4) << 8;	/* ITSTATE<2> */
		cpsr |= (mask & 0x8) << 23;	/* ITSTATE<1> */
		cpsr |= (mask & 0x10) << 21;	/* ITSTATE<0> */

		probe_should_run = test_check_cc((cpsr >> 12) & 0xf, cpsr) != 0;

	} else {
		/* Testing Thumb code with several combinations of ITSTATE */
		switch (scenario) {
		case 16: /* Clear NZCV flags and 'it eq' state (false as Z=0) */
			cpsr = 0x00000800;
			probe_should_run = 0;
			break;
		case 17: /* Set NZCV flags and 'it vc' state (false as V=1) */
			cpsr = 0xf0007800;
			probe_should_run = 0;
			break;
		case 18: /* Clear NZCV flags and 'it ls' state (true as C=0) */
			cpsr = 0x00009800;
			break;
		case 19: /* Set NZCV flags and 'it cs' state (true as C=1) */
			cpsr = 0xf0002800;
			is_last_scenario = true;
			break;
		}
	}

	return cpsr;
}

static void setup_test_context(struct pt_regs *regs)
{
	int scenario = test_case_run_count>>1;
	unsigned long val;
	struct test_arg *args;
	int i;

	is_last_scenario = false;
	memory_needs_checking = false;

	/* Initialise test memory on stack */
	val = (scenario & 1) ? VALM : ~VALM;
	for (i = 0; i < TEST_MEMORY_SIZE / sizeof(current_stack[0]); ++i)
		current_stack[i] = val + (i << 8);
	/* Put target of branch on stack for tests which load PC from memory */
	if (current_branch_target)
		current_stack[15] = current_branch_target;
	/* Put a value for SP on stack for tests which load SP from memory */
	current_stack[13] = (u32)current_stack + 120;

	/* Initialise register values to their default state */
	val = (scenario & 2) ? VALR : ~VALR;
	for (i = 0; i < 13; ++i)
		regs->uregs[i] = val ^ (i << 8);
	regs->ARM_lr = val ^ (14 << 8);
	regs->ARM_cpsr &= ~(APSR_MASK | PSR_IT_MASK);
	regs->ARM_cpsr |= test_context_cpsr(scenario);

	/* Perform testcase specific register setup  */
	args = current_args;
	for (; args[0].type != ARG_TYPE_END; ++args)
		switch (args[0].type) {
		case ARG_TYPE_REG: {
			struct test_arg_regptr *arg =
				(struct test_arg_regptr *)args;
			regs->uregs[arg->reg] = arg->val;
			break;
		}
		case ARG_TYPE_PTR: {
			struct test_arg_regptr *arg =
				(struct test_arg_regptr *)args;
			regs->uregs[arg->reg] =
				(unsigned long)current_stack + arg->val;
			memory_needs_checking = true;
			/*
			 * Test memory at an address below SP is in danger of
			 * being altered by an interrupt occurring and pushing
			 * data onto the stack. Disable interrupts to stop this.
			 */
			if (arg->reg == 13)
				regs->ARM_cpsr |= PSR_I_BIT;
			break;
		}
		case ARG_TYPE_MEM: {
			struct test_arg_mem *arg = (struct test_arg_mem *)args;
			current_stack[arg->index] = arg->val;
			break;
		}
		default:
			break;
		}
}

struct test_probe {
	struct kprobe	kprobe;
	bool		registered;
	int		hit;
};

static void unregister_test_probe(struct test_probe *probe)
{
	if (probe->registered) {
		unregister_kprobe(&probe->kprobe);
		probe->kprobe.flags = 0; /* Clear disable flag to allow reuse */
	}
	probe->registered = false;
}

static int register_test_probe(struct test_probe *probe)
{
	int ret;

	if (probe->registered)
		BUG();

	ret = register_kprobe(&probe->kprobe);
	if (ret >= 0) {
		probe->registered = true;
		probe->hit = -1;
	}
	return ret;
}

static int __kprobes
test_before_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	container_of(p, struct test_probe, kprobe)->hit = test_instance;
	return 0;
}

static void __kprobes
test_before_post_handler(struct kprobe *p, struct pt_regs *regs,
							unsigned long flags)
{
	setup_test_context(regs);
	initial_regs = *regs;
	initial_regs.ARM_cpsr &= ~PSR_IGNORE_BITS;
}

static int __kprobes
test_case_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	container_of(p, struct test_probe, kprobe)->hit = test_instance;
	return 0;
}

static int __kprobes
test_after_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct test_arg *args;

	if (container_of(p, struct test_probe, kprobe)->hit == test_instance)
		return 0; /* Already run for this test instance */

	result_regs = *regs;

	/* Mask out results which are indeterminate */
	result_regs.ARM_cpsr &= ~PSR_IGNORE_BITS;
	for (args = current_args; args[0].type != ARG_TYPE_END; ++args)
		if (args[0].type == ARG_TYPE_REG_MASKED) {
			struct test_arg_regptr *arg =
				(struct test_arg_regptr *)args;
			result_regs.uregs[arg->reg] &= arg->val;
		}

	/* Undo any changes done to SP by the test case */
	regs->ARM_sp = (unsigned long)current_stack;
	/* Enable interrupts in case setup_test_context disabled them */
	regs->ARM_cpsr &= ~PSR_I_BIT;

	container_of(p, struct test_probe, kprobe)->hit = test_instance;
	return 0;
}

static struct test_probe test_before_probe = {
	.kprobe.pre_handler	= test_before_pre_handler,
	.kprobe.post_handler	= test_before_post_handler,
};

static struct test_probe test_case_probe = {
	.kprobe.pre_handler	= test_case_pre_handler,
};

static struct test_probe test_after_probe = {
	.kprobe.pre_handler	= test_after_pre_handler,
};

static struct test_probe test_after2_probe = {
	.kprobe.pre_handler	= test_after_pre_handler,
};

static void test_case_cleanup(void)
{
	unregister_test_probe(&test_before_probe);
	unregister_test_probe(&test_case_probe);
	unregister_test_probe(&test_after_probe);
	unregister_test_probe(&test_after2_probe);
}

static void print_registers(struct pt_regs *regs)
{
	pr_err("r0  %08lx | r1  %08lx | r2  %08lx | r3  %08lx\n",
		regs->ARM_r0, regs->ARM_r1, regs->ARM_r2, regs->ARM_r3);
	pr_err("r4  %08lx | r5  %08lx | r6  %08lx | r7  %08lx\n",
		regs->ARM_r4, regs->ARM_r5, regs->ARM_r6, regs->ARM_r7);
	pr_err("r8  %08lx | r9  %08lx | r10 %08lx | r11 %08lx\n",
		regs->ARM_r8, regs->ARM_r9, regs->ARM_r10, regs->ARM_fp);
	pr_err("r12 %08lx | sp  %08lx | lr  %08lx | pc  %08lx\n",
		regs->ARM_ip, regs->ARM_sp, regs->ARM_lr, regs->ARM_pc);
	pr_err("cpsr %08lx\n", regs->ARM_cpsr);
}

static void print_memory(u32 *mem, size_t size)
{
	int i;
	for (i = 0; i < size / sizeof(u32); i += 4)
		pr_err("%08x %08x %08x %08x\n", mem[i], mem[i+1],
						mem[i+2], mem[i+3]);
}

static size_t expected_memory_size(u32 *sp)
{
	size_t size = sizeof(expected_memory);
	int offset = (uintptr_t)sp - (uintptr_t)current_stack;
	if (offset > 0)
		size -= offset;
	return size;
}

static void test_case_failed(const char *message)
{
	test_case_cleanup();

	pr_err("FAIL: %s\n", message);
	pr_err("FAIL: Test %s\n", current_title);
	pr_err("FAIL: Scenario %d\n", test_case_run_count >> 1);
}

static unsigned long next_instruction(unsigned long pc)
{
#ifdef CONFIG_THUMB2_KERNEL
	if ((pc & 1) &&
	    !is_wide_instruction(__mem_to_opcode_thumb16(*(u16 *)(pc - 1))))
		return pc + 2;
	else
#endif
	return pc + 4;
}

static uintptr_t __used kprobes_test_case_start(const char **title, void *stack)
{
	struct test_arg *args;
	struct test_arg_end *end_arg;
	unsigned long test_code;

	current_title = *title++;
	args = (struct test_arg *)title;
	current_args = args;
	current_stack = stack;

	++test_try_count;

	while (args->type != ARG_TYPE_END)
		++args;
	end_arg = (struct test_arg_end *)args;

	test_code = (unsigned long)(args + 1); /* Code starts after args */

	test_case_is_thumb = end_arg->flags & ARG_FLAG_THUMB;
	if (test_case_is_thumb)
		test_code |= 1;

	current_code_start = test_code;

	current_branch_target = 0;
	if (end_arg->branch_offset != end_arg->end_offset)
		current_branch_target = test_code + end_arg->branch_offset;

	test_code += end_arg->code_offset;
	test_before_probe.kprobe.addr = (kprobe_opcode_t *)test_code;

	test_code = next_instruction(test_code);
	test_case_probe.kprobe.addr = (kprobe_opcode_t *)test_code;

	if (test_case_is_thumb) {
		u16 *p = (u16 *)(test_code & ~1);
		current_instruction = __mem_to_opcode_thumb16(p[0]);
		if (is_wide_instruction(current_instruction)) {
			u16 instr2 = __mem_to_opcode_thumb16(p[1]);
			current_instruction = __opcode_thumb32_compose(current_instruction, instr2);
		}
	} else {
		current_instruction = __mem_to_opcode_arm(*(u32 *)test_code);
	}

	if (current_title[0] == '.')
		verbose("%s\n", current_title);
	else
		verbose("%s\t@ %0*x\n", current_title,
					test_case_is_thumb ? 4 : 8,
					current_instruction);

	test_code = next_instruction(test_code);
	test_after_probe.kprobe.addr = (kprobe_opcode_t *)test_code;

	if (kprobe_test_flags & TEST_FLAG_NARROW_INSTR) {
		if (!test_case_is_thumb ||
			is_wide_instruction(current_instruction)) {
				test_case_failed("expected 16-bit instruction");
				goto fail;
		}
	} else {
		if (test_case_is_thumb &&
			!is_wide_instruction(current_instruction)) {
				test_case_failed("expected 32-bit instruction");
				goto fail;
		}
	}

	coverage_add(current_instruction);

	if (end_arg->flags & ARG_FLAG_UNSUPPORTED) {
		if (register_test_probe(&test_case_probe) < 0)
			goto pass;
		test_case_failed("registered probe for unsupported instruction");
		goto fail;
	}

	if (end_arg->flags & ARG_FLAG_SUPPORTED) {
		if (register_test_probe(&test_case_probe) >= 0)
			goto pass;
		test_case_failed("couldn't register probe for supported instruction");
		goto fail;
	}

	if (register_test_probe(&test_before_probe) < 0) {
		test_case_failed("register test_before_probe failed");
		goto fail;
	}
	if (register_test_probe(&test_after_probe) < 0) {
		test_case_failed("register test_after_probe failed");
		goto fail;
	}
	if (current_branch_target) {
		test_after2_probe.kprobe.addr =
				(kprobe_opcode_t *)current_branch_target;
		if (register_test_probe(&test_after2_probe) < 0) {
			test_case_failed("register test_after2_probe failed");
			goto fail;
		}
	}

	/* Start first run of test case */
	test_case_run_count = 0;
	++test_instance;
	return current_code_start;
pass:
	test_case_run_count = TEST_CASE_PASSED;
	return (uintptr_t)test_after_probe.kprobe.addr;
fail:
	test_case_run_count = TEST_CASE_FAILED;
	return (uintptr_t)test_after_probe.kprobe.addr;
}

static bool check_test_results(void)
{
	size_t mem_size = 0;
	u32 *mem = 0;

	if (memcmp(&expected_regs, &result_regs, sizeof(expected_regs))) {
		test_case_failed("registers differ");
		goto fail;
	}

	if (memory_needs_checking) {
		mem = (u32 *)result_regs.ARM_sp;
		mem_size = expected_memory_size(mem);
		if (memcmp(expected_memory, mem, mem_size)) {
			test_case_failed("test memory differs");
			goto fail;
		}
	}

	return true;

fail:
	pr_err("initial_regs:\n");
	print_registers(&initial_regs);
	pr_err("expected_regs:\n");
	print_registers(&expected_regs);
	pr_err("result_regs:\n");
	print_registers(&result_regs);

	if (mem) {
		pr_err("expected_memory:\n");
		print_memory(expected_memory, mem_size);
		pr_err("result_memory:\n");
		print_memory(mem, mem_size);
	}

	return false;
}

static uintptr_t __used kprobes_test_case_end(void)
{
	if (test_case_run_count < 0) {
		if (test_case_run_count == TEST_CASE_PASSED)
			/* kprobes_test_case_start did all the needed testing */
			goto pass;
		else
			/* kprobes_test_case_start failed */
			goto fail;
	}

	if (test_before_probe.hit != test_instance) {
		test_case_failed("test_before_handler not run");
		goto fail;
	}

	if (test_after_probe.hit != test_instance &&
				test_after2_probe.hit != test_instance) {
		test_case_failed("test_after_handler not run");
		goto fail;
	}

	/*
	 * Even numbered test runs ran without a probe on the test case so
	 * we can gather reference results. The subsequent odd numbered run
	 * will have the probe inserted.
	*/
	if ((test_case_run_count & 1) == 0) {
		/* Save results from run without probe */
		u32 *mem = (u32 *)result_regs.ARM_sp;
		expected_regs = result_regs;
		memcpy(expected_memory, mem, expected_memory_size(mem));

		/* Insert probe onto test case instruction */
		if (register_test_probe(&test_case_probe) < 0) {
			test_case_failed("register test_case_probe failed");
			goto fail;
		}
	} else {
		/* Check probe ran as expected */
		if (probe_should_run == 1) {
			if (test_case_probe.hit != test_instance) {
				test_case_failed("test_case_handler not run");
				goto fail;
			}
		} else if (probe_should_run == 0) {
			if (test_case_probe.hit == test_instance) {
				test_case_failed("test_case_handler ran");
				goto fail;
			}
		}

		/* Remove probe for any subsequent reference run */
		unregister_test_probe(&test_case_probe);

		if (!check_test_results())
			goto fail;

		if (is_last_scenario)
			goto pass;
	}

	/* Do next test run */
	++test_case_run_count;
	++test_instance;
	return current_code_start;
fail:
	++test_fail_count;
	goto end;
pass:
	++test_pass_count;
end:
	test_case_cleanup();
	return 0;
}


/*
 * Top level test functions
 */

static int run_test_cases(void (*tests)(void), const union decode_item *table)
{
	int ret;

	pr_info("    Check decoding tables\n");
	ret = table_test(table);
	if (ret)
		return ret;

	pr_info("    Run test cases\n");
	ret = coverage_start(table);
	if (ret)
		return ret;

	tests();

	coverage_end();
	return 0;
}


static int __init run_all_tests(void)
{
	int ret = 0;

	pr_info("Beginning kprobe tests...\n");

#ifndef CONFIG_THUMB2_KERNEL

	pr_info("Probe ARM code\n");
	ret = run_api_tests(arm_func);
	if (ret)
		goto out;

	pr_info("ARM instruction simulation\n");
	ret = run_test_cases(kprobe_arm_test_cases, probes_decode_arm_table);
	if (ret)
		goto out;

#else /* CONFIG_THUMB2_KERNEL */

	pr_info("Probe 16-bit Thumb code\n");
	ret = run_api_tests(thumb16_func);
	if (ret)
		goto out;

	pr_info("Probe 32-bit Thumb code, even halfword\n");
	ret = run_api_tests(thumb32even_func);
	if (ret)
		goto out;

	pr_info("Probe 32-bit Thumb code, odd halfword\n");
	ret = run_api_tests(thumb32odd_func);
	if (ret)
		goto out;

	pr_info("16-bit Thumb instruction simulation\n");
	ret = run_test_cases(kprobe_thumb16_test_cases,
				probes_decode_thumb16_table);
	if (ret)
		goto out;

	pr_info("32-bit Thumb instruction simulation\n");
	ret = run_test_cases(kprobe_thumb32_test_cases,
				probes_decode_thumb32_table);
	if (ret)
		goto out;
#endif

	pr_info("Total instruction simulation tests=%d, pass=%d fail=%d\n",
		test_try_count, test_pass_count, test_fail_count);
	if (test_fail_count) {
		ret = -EINVAL;
		goto out;
	}

#if BENCHMARKING
	pr_info("Benchmarks\n");
	ret = run_benchmarks();
	if (ret)
		goto out;
#endif

#if __LINUX_ARM_ARCH__ >= 7
	/* We are able to run all test cases so coverage should be complete */
	if (coverage_fail) {
		pr_err("FAIL: Test coverage checks failed\n");
		ret = -EINVAL;
		goto out;
	}
#endif

out:
	if (ret == 0)
		ret = tests_failed;
	if (ret == 0)
		pr_info("Finished kprobe tests OK\n");
	else
		pr_err("kprobe tests failed\n");

	return ret;
}


/*
 * Module setup
 */

#ifdef MODULE

static void __exit kprobe_test_exit(void)
{
}

module_init(run_all_tests)
module_exit(kprobe_test_exit)
MODULE_LICENSE("GPL");

#else /* !MODULE */

late_initcall(run_all_tests);

#endif
