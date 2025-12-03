// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <kunit/test.h>
#include "test-kprobes.h"

static int kprobe_dummy_handler(struct kprobe *kp, struct pt_regs *regs)
{
	return 0;
}

static void test_kprobe_riscv(struct kunit *test)
{
	unsigned int num_kprobe = 0;
	long (*func)(void);
	struct kprobe *kp;
	int i;

	while (test_kprobes_addresses[num_kprobe])
		num_kprobe++;

	kp = kcalloc(num_kprobe, sizeof(*kp), GFP_KERNEL);
	KUNIT_EXPECT_TRUE(test, kp);
	if (!kp)
		return;

	for (i = 0; i < num_kprobe; ++i) {
		kp[i].addr = test_kprobes_addresses[i];
		kp[i].pre_handler = kprobe_dummy_handler;
		KUNIT_EXPECT_EQ(test, 0, register_kprobe(&kp[i]));
	}

	for (i = 0;; ++i) {
		func = test_kprobes_functions[i];
		if (!func)
			break;
		KUNIT_EXPECT_EQ_MSG(test, KPROBE_TEST_MAGIC, func(), "function %d broken", i);
	}

	for (i = 0; i < num_kprobe; ++i)
		unregister_kprobe(&kp[i]);
	kfree(kp);
}

static struct kunit_case kprobes_testcases[] = {
	KUNIT_CASE(test_kprobe_riscv),
	{}
};

static struct kunit_suite kprobes_test_suite = {
	.name = "kprobes_riscv",
	.test_cases = kprobes_testcases,
};

kunit_test_suites(&kprobes_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit test for riscv kprobes");
