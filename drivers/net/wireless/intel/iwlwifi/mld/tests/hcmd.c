// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for channel helper functions
 *
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <kunit/test.h>

#include <iwl-trans.h>
#include "mld.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static void test_hcmd_names_sorted(struct kunit *test)
{
	int i;

	for (i = 0; i < global_iwl_mld_goups_size; i++) {
		const struct iwl_hcmd_arr *arr = &iwl_mld_groups[i];
		int j;

		if (!arr->arr)
			continue;
		for (j = 0; j < arr->size - 1; j++)
			KUNIT_EXPECT_LE(test, arr->arr[j].cmd_id,
					arr->arr[j + 1].cmd_id);
	}
}

static void test_hcmd_names_for_rx(struct kunit *test)
{
	static struct iwl_trans t = {
		.conf.command_groups = iwl_mld_groups,
	};

	t.conf.command_groups_size = global_iwl_mld_goups_size;

	for (unsigned int i = 0; i < iwl_mld_rx_handlers_num; i++) {
		const struct iwl_rx_handler *rxh;
		const char *name;

		rxh = &iwl_mld_rx_handlers[i];

		name = iwl_get_cmd_string(&t, rxh->cmd_id);
		KUNIT_EXPECT_NOT_NULL(test, name);
		KUNIT_EXPECT_NE_MSG(test, strcmp(name, "UNKNOWN"), 0,
				    "ID 0x%04x is UNKNOWN", rxh->cmd_id);
	}
}

static struct kunit_case hcmd_names_cases[] = {
	KUNIT_CASE(test_hcmd_names_sorted),
	KUNIT_CASE(test_hcmd_names_for_rx),
	{},
};

static struct kunit_suite hcmd_names = {
	.name = "iwlmld-hcmd-names",
	.test_cases = hcmd_names_cases,
};

kunit_test_suite(hcmd_names);
