/**
 * Copyright (C) ARM Limited 2012-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/**
 * Test functions for mali_t600_hw code.
 */

static int is_read_scheduled(const struct timespec *current_time, u32 *prev_time_s, s32 *next_read_time_ns);

static int test_is_read_scheduled(u32 s, u32 ns, u32 prev_s, s32 next_ns, int expected_result, s32 expected_next_ns)
{
	struct timespec current_time;
	u32 prev_time_s = prev_s;
	s32 next_read_time_ns = next_ns;

	current_time.tv_sec = s;
	current_time.tv_nsec = ns;

	if (is_read_scheduled(&current_time, &prev_time_s, &next_read_time_ns) != expected_result) {
		printk("Failed do_read(%u, %u, %u, %d): expected %d\n", s, ns, prev_s, next_ns, expected_result);
		return 0;
	}

	if (next_read_time_ns != expected_next_ns) {
		printk("Failed: next_read_ns expected=%d, actual=%d\n", expected_next_ns, next_read_time_ns);
		return 0;
	}

	return 1;
}

static void test_all_is_read_scheduled(void)
{
	const int HIGHEST_NS = 999999999;
	int n_tests_passed = 0;

	printk("gator: running tests on %s\n", __FILE__);

	n_tests_passed += test_is_read_scheduled(0, 0, 0, 0, 1, READ_INTERVAL_NSEC);	/* Null time */
	n_tests_passed += test_is_read_scheduled(100, 1000, 0, 0, 1, READ_INTERVAL_NSEC + 1000);	/* Initial values */

	n_tests_passed += test_is_read_scheduled(100, HIGHEST_NS, 100, HIGHEST_NS + 500, 0, HIGHEST_NS + 500);
	n_tests_passed += test_is_read_scheduled(101, 0001, 100, HIGHEST_NS + 500, 0, HIGHEST_NS + 500 - NSEC_PER_SEC);
	n_tests_passed += test_is_read_scheduled(101, 600, 100, HIGHEST_NS + 500 - NSEC_PER_SEC, 1, 600 + READ_INTERVAL_NSEC);

	n_tests_passed += test_is_read_scheduled(101, 600, 100, HIGHEST_NS + 500, 1, 600 + READ_INTERVAL_NSEC);

	printk("gator: %d tests passed\n", n_tests_passed);
}
