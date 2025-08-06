// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2025 Intel Corporation
 */
#include <kunit/test.h>

#include <iwl-trans.h>
#include "../mvm.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static void test_hcmd_names_sorted(struct kunit *test)
{
	for (int i = 0; i < iwl_mvm_groups_size; i++) {
		const struct iwl_hcmd_arr *arr = &iwl_mvm_groups[i];

		if (!arr->arr)
			continue;

		for (int j = 0; j < arr->size - 1; j++)
			KUNIT_EXPECT_LE(test, arr->arr[j].cmd_id,
					arr->arr[j + 1].cmd_id);
	}
}

static struct kunit_case hcmd_names_cases[] = {
	KUNIT_CASE(test_hcmd_names_sorted),
	{},
};

static struct kunit_suite hcmd_names = {
	.name = "iwlmvm-hcmd-names",
	.test_cases = hcmd_names_cases,
};

kunit_test_suite(hcmd_names);
