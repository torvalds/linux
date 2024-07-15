// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rivos Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <kunit/test.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test module linking");

extern int test_set32(void);
extern int test_set16(void);
extern int test_set8(void);
extern int test_set6(void);
extern long test_sub64(void);
extern int test_sub32(void);
extern int test_sub16(void);
extern int test_sub8(void);
extern int test_sub6(void);

#ifdef CONFIG_AS_HAS_ULEB128
extern int test_uleb_basic(void);
extern int test_uleb_large(void);
#endif

#define CHECK_EQ(lhs, rhs) KUNIT_ASSERT_EQ(test, lhs, rhs)

void run_test_set(struct kunit *test);
void run_test_sub(struct kunit *test);
void run_test_uleb(struct kunit *test);

void run_test_set(struct kunit *test)
{
	int val32 = test_set32();
	int val16 = test_set16();
	int val8 = test_set8();
	int val6 = test_set6();

	CHECK_EQ(val32, 0);
	CHECK_EQ(val16, 0);
	CHECK_EQ(val8, 0);
	CHECK_EQ(val6, 0);
}

void run_test_sub(struct kunit *test)
{
	int val64 = test_sub64();
	int val32 = test_sub32();
	int val16 = test_sub16();
	int val8 = test_sub8();
	int val6 = test_sub6();

	CHECK_EQ(val64, 0);
	CHECK_EQ(val32, 0);
	CHECK_EQ(val16, 0);
	CHECK_EQ(val8, 0);
	CHECK_EQ(val6, 0);
}

#ifdef CONFIG_AS_HAS_ULEB128
void run_test_uleb(struct kunit *test)
{
	int val_uleb = test_uleb_basic();
	int val_uleb2 = test_uleb_large();

	CHECK_EQ(val_uleb, 0);
	CHECK_EQ(val_uleb2, 0);
}
#endif

static struct kunit_case __refdata riscv_module_linking_test_cases[] = {
	KUNIT_CASE(run_test_set),
	KUNIT_CASE(run_test_sub),
#ifdef CONFIG_AS_HAS_ULEB128
	KUNIT_CASE(run_test_uleb),
#endif
	{}
};

static struct kunit_suite riscv_module_linking_test_suite = {
	.name = "riscv_checksum",
	.test_cases = riscv_module_linking_test_cases,
};

kunit_test_suites(&riscv_module_linking_test_suite);
