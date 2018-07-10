/* vi: set sw=4 ts=4: */
/*
 * bbunit: Simple unit-testing framework for Busybox.
 *
 * Copyright (C) 2014 by Bartosz Golaszewski <bartekgola@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//applet:IF_UNIT_TEST(APPLET(unit, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_UNIT_TEST) += bbunit.o

//usage:#define unit_trivial_usage
//usage:       ""
//usage:#define unit_full_usage "\n\n"
//usage:       "Run the unit-test suite"

#include "libbb.h"

static llist_t *tests = NULL;
static unsigned tests_registered = 0;
static int test_retval;

void bbunit_registertest(struct bbunit_listelem *test)
{
	llist_add_to_end(&tests, test);
	tests_registered++;
}

void bbunit_settestfailed(void)
{
	test_retval = -1;
}

int unit_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM) MAIN_EXTERNALLY_VISIBLE;
int unit_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	unsigned tests_run = 0;
	unsigned tests_failed = 0;

	bb_error_msg("Running %d test(s)...", tests_registered);
	for (;;) {
		struct bbunit_listelem* el = llist_pop(&tests);
		if (!el)
			break;

		bb_error_msg("Case: [%s]", el->name);
		test_retval = 0;
		el->testfunc();

		if (test_retval < 0) {
			bb_error_msg("[ERROR] [%s]: TEST FAILED", el->name);
			tests_failed++;
		}
		tests_run++;
	}

	if (tests_failed > 0) {
		bb_error_msg("[ERROR] %u test(s) FAILED", tests_failed);
		return EXIT_FAILURE;
	}

	bb_error_msg("All tests passed");
	return EXIT_SUCCESS;
}
