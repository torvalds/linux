// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 *
 * This file can optionally be built into fips140.ko in order to support certain
 * types of testing that the FIPS lab has to do to evaluate the module.  It
 * should not be included in production builds of the module.
 */

#include <linux/module.h>

#include "fips140-module.h"

/*
 * This option allows deliberately failing the self-tests for a particular
 * algorithm.
 */
static char *fips140_fail_selftest;
module_param_named(fail_selftest, fips140_fail_selftest, charp, 0);

/* Inject a self-test failure (via corrupting the result) if requested. */
void fips140_inject_selftest_failure(const char *impl, u8 *result)
{
	if (fips140_fail_selftest && strcmp(impl, fips140_fail_selftest) == 0)
		result[0] ^= 0xff;
}

bool fips140_eval_testing_init(void)
{
	return true;
}
