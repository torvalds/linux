// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/random.h>
#include <kunit/test.h>
#include "test_kprobes.h"

static struct kprobe kp;

static void setup_kprobe(struct kunit *test, struct kprobe *kp,
			 const char *symbol, int offset)
{
	kp->offset = offset;
	kp->addr = NULL;
	kp->symbol_name = symbol;
}

static void test_kprobe_offset(struct kunit *test, struct kprobe *kp,
			       const char *target, int offset)
{
	int ret;

	setup_kprobe(test, kp, target, 0);
	ret = register_kprobe(kp);
	if (!ret)
		unregister_kprobe(kp);
	KUNIT_EXPECT_EQ(test, 0, ret);
	setup_kprobe(test, kp, target, offset);
	ret = register_kprobe(kp);
	KUNIT_EXPECT_EQ(test, -EINVAL, ret);
	if (!ret)
		unregister_kprobe(kp);
}

static void test_kprobe_odd(struct kunit *test)
{
	test_kprobe_offset(test, &kp, "kprobes_target_odd",
			   kprobes_target_odd_offs);
}

static void test_kprobe_in_insn4(struct kunit *test)
{
	test_kprobe_offset(test, &kp, "kprobes_target_in_insn4",
			   kprobes_target_in_insn4_offs);
}

static void test_kprobe_in_insn6_lo(struct kunit *test)
{
	test_kprobe_offset(test, &kp, "kprobes_target_in_insn6_lo",
			   kprobes_target_in_insn6_lo_offs);
}

static void test_kprobe_in_insn6_hi(struct kunit *test)
{
	test_kprobe_offset(test, &kp, "kprobes_target_in_insn6_hi",
			   kprobes_target_in_insn6_hi_offs);
}

static struct kunit_case kprobes_testcases[] = {
	KUNIT_CASE(test_kprobe_odd),
	KUNIT_CASE(test_kprobe_in_insn4),
	KUNIT_CASE(test_kprobe_in_insn6_lo),
	KUNIT_CASE(test_kprobe_in_insn6_hi),
	{}
};

static struct kunit_suite kprobes_test_suite = {
	.name = "kprobes_test_s390",
	.test_cases = kprobes_testcases,
};

kunit_test_suites(&kprobes_test_suite);

MODULE_LICENSE("GPL");
