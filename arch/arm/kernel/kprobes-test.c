/*
 * arch/arm/kernel/kprobes-test.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

#include "kprobes.h"


/*
 * Test basic API
 */

static bool test_regs_ok;
static int test_func_instance;
static int pre_handler_called;
static int post_handler_called;
static int jprobe_func_called;
static int kretprobe_handler_called;

#define FUNC_ARG1 0x12345678
#define FUNC_ARG2 0xabcdef


#ifndef CONFIG_THUMB2_KERNEL

long arm_func(long r0, long r1);

static void __used __naked __arm_kprobes_test_func(void)
{
	__asm__ __volatile__ (
		".arm					\n\t"
		".type arm_func, %%function		\n\t"
		"arm_func:				\n\t"
		"adds	r0, r0, r1			\n\t"
		"bx	lr				\n\t"
		".code "NORMAL_ISA	 /* Back to Thumb if necessary */
		: : : "r0", "r1", "cc"
	);
}

#else /* CONFIG_THUMB2_KERNEL */

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

static void __kprobes jprobe_func(long r0, long r1)
{
	jprobe_func_called = test_func_instance;
	if (r0 == FUNC_ARG1 && r1 == FUNC_ARG2)
		test_regs_ok = true;
	jprobe_return();
}

static struct jprobe the_jprobe = {
	.entry		= jprobe_func,
};

static int test_jprobe(long (*func)(long, long))
{
	int ret;

	the_jprobe.kp.addr = (kprobe_opcode_t *)func;
	ret = register_jprobe(&the_jprobe);
	if (ret < 0) {
		pr_err("FAIL: register_jprobe failed with %d\n", ret);
		return ret;
	}

	ret = call_test_func(func, true);

	unregister_jprobe(&the_jprobe);
	the_jprobe.kp.flags = 0; /* Clear disable flag to allow reuse */

	if (!ret)
		return -EINVAL;
	if (jprobe_func_called != test_func_instance) {
		pr_err("FAIL: jprobe handler function not called\n");
		return -EINVAL;
	}
	if (!call_test_func(func, false))
		return -EINVAL;
	if (jprobe_func_called == test_func_instance) {
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
	if (jprobe_func_called == test_func_instance) {
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

	pr_info("    jprobe\n");
	ret = test_jprobe(func);
	if (ret < 0)
		return ret;

	pr_info("    kretprobe\n");
	ret = test_kretprobe(func);
	if (ret < 0)
		return ret;

	return 0;
}


/*
 * Top level test functions
 */

static int __init run_all_tests(void)
{
	int ret = 0;

	pr_info("Begining kprobe tests...\n");

#ifndef CONFIG_THUMB2_KERNEL

	pr_info("Probe ARM code\n");
	ret = run_api_tests(arm_func);
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

#endif

out:
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
