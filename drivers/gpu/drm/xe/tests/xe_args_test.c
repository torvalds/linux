// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2024 Intel Corporation
 */

#include <kunit/test.h>

#include "xe_args.h"

static void call_args_example(struct kunit *test)
{
#define foo	X, Y, Z, Q
#define bar	COUNT_ARGS(foo)
#define buz	CALL_ARGS(COUNT_ARGS, foo)

	KUNIT_EXPECT_EQ(test, bar, 1);
	KUNIT_EXPECT_EQ(test, buz, 4);

#undef foo
#undef bar
#undef buz
}

static void drop_first_example(struct kunit *test)
{
#define foo	X, Y, Z, Q
#define bar	CALL_ARGS(COUNT_ARGS, DROP_FIRST(foo))

	KUNIT_EXPECT_EQ(test, bar, 3);

#undef foo
#undef bar
}

static void pick_first_example(struct kunit *test)
{
	int X = 1;

#define foo	X, Y, Z, Q
#define bar	PICK_FIRST(foo)

	KUNIT_EXPECT_EQ(test, bar, X);
	KUNIT_EXPECT_STREQ(test, __stringify(bar), "X");

#undef foo
#undef bar
}

static void pick_last_example(struct kunit *test)
{
	int Q = 1;

#define foo	X, Y, Z, Q
#define bar	PICK_LAST(foo)

	KUNIT_EXPECT_EQ(test, bar, Q);
	KUNIT_EXPECT_STREQ(test, __stringify(bar), "Q");

#undef foo
#undef bar
}

static void sep_comma_example(struct kunit *test)
{
#define foo(f)	f(X) f(Y) f(Z) f(Q)
#define bar	DROP_FIRST(foo(ARGS_SEP_COMMA __stringify))
#define buz	CALL_ARGS(COUNT_ARGS, DROP_FIRST(foo(ARGS_SEP_COMMA)))

	static const char * const a[] = { bar };

	KUNIT_EXPECT_STREQ(test, a[0], "X");
	KUNIT_EXPECT_STREQ(test, a[1], "Y");
	KUNIT_EXPECT_STREQ(test, a[2], "Z");
	KUNIT_EXPECT_STREQ(test, a[3], "Q");

	KUNIT_EXPECT_EQ(test, buz, 4);

#undef foo
#undef bar
#undef buz
}

#define NO_ARGS
#define FOO_ARGS	X, Y, Z, Q
#define MAX_ARGS	-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12

static void count_args_test(struct kunit *test)
{
	int count;

	/* COUNT_ARGS() counts to 12 */

	count = COUNT_ARGS();
	KUNIT_EXPECT_EQ(test, count, 0);

	count = COUNT_ARGS(1);
	KUNIT_EXPECT_EQ(test, count, 1);

	count = COUNT_ARGS(a, b, c, d, e);
	KUNIT_EXPECT_EQ(test, count, 5);

	count = COUNT_ARGS(a, b, c, d, e, f, g, h, i, j, k, l);
	KUNIT_EXPECT_EQ(test, count, 12);

	/* COUNT_ARGS() does not expand params */

	count = COUNT_ARGS(NO_ARGS);
	KUNIT_EXPECT_EQ(test, count, 1);

	count = COUNT_ARGS(FOO_ARGS);
	KUNIT_EXPECT_EQ(test, count, 1);
}

static void call_args_test(struct kunit *test)
{
	int count;

	count = CALL_ARGS(COUNT_ARGS, NO_ARGS);
	KUNIT_EXPECT_EQ(test, count, 0);
	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, NO_ARGS), 0);
	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, FOO_ARGS), 4);
	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, FOO_ARGS, FOO_ARGS), 8);
	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, MAX_ARGS), 12);
}

static void drop_first_test(struct kunit *test)
{
	int Y = -2, Z = -3, Q = -4;
	int a[] = { DROP_FIRST(FOO_ARGS) };

	KUNIT_EXPECT_EQ(test, DROP_FIRST(0, -1), -1);
	KUNIT_EXPECT_EQ(test, DROP_FIRST(DROP_FIRST(0, -1, -2)), -2);

	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, DROP_FIRST(FOO_ARGS)), 3);
	KUNIT_EXPECT_EQ(test, DROP_FIRST(DROP_FIRST(DROP_FIRST(FOO_ARGS))), -4);
	KUNIT_EXPECT_EQ(test, a[0], -2);
	KUNIT_EXPECT_EQ(test, a[1], -3);
	KUNIT_EXPECT_EQ(test, a[2], -4);
	KUNIT_EXPECT_STREQ(test, __stringify(DROP_FIRST(DROP_FIRST(DROP_FIRST(FOO_ARGS)))), "Q");
}

static void pick_first_test(struct kunit *test)
{
	int X = -1;
	int a[] = { PICK_FIRST(FOO_ARGS) };

	KUNIT_EXPECT_EQ(test, PICK_FIRST(-1, -2), -1);

	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, PICK_FIRST(FOO_ARGS)), 1);
	KUNIT_EXPECT_EQ(test, PICK_FIRST(FOO_ARGS), -1);
	KUNIT_EXPECT_EQ(test, a[0], -1);
	KUNIT_EXPECT_STREQ(test, __stringify(PICK_FIRST(FOO_ARGS)), "X");
}

static void pick_last_test(struct kunit *test)
{
	int Q = -4;
	int a[] = { PICK_LAST(FOO_ARGS) };

	KUNIT_EXPECT_EQ(test, PICK_LAST(-1, -2), -2);

	KUNIT_EXPECT_EQ(test, CALL_ARGS(COUNT_ARGS, PICK_LAST(FOO_ARGS)), 1);
	KUNIT_EXPECT_EQ(test, PICK_LAST(FOO_ARGS), -4);
	KUNIT_EXPECT_EQ(test, a[0], -4);
	KUNIT_EXPECT_STREQ(test, __stringify(PICK_LAST(FOO_ARGS)), "Q");

	KUNIT_EXPECT_EQ(test, PICK_LAST(MAX_ARGS), -12);
	KUNIT_EXPECT_STREQ(test, __stringify(PICK_LAST(MAX_ARGS)), "-12");
}

static struct kunit_case args_tests[] = {
	KUNIT_CASE(count_args_test),
	KUNIT_CASE(call_args_example),
	KUNIT_CASE(call_args_test),
	KUNIT_CASE(drop_first_example),
	KUNIT_CASE(drop_first_test),
	KUNIT_CASE(pick_first_example),
	KUNIT_CASE(pick_first_test),
	KUNIT_CASE(pick_last_example),
	KUNIT_CASE(pick_last_test),
	KUNIT_CASE(sep_comma_example),
	{}
};

static struct kunit_suite args_test_suite = {
	.name = "args",
	.test_cases = args_tests,
};

kunit_test_suite(args_test_suite);
