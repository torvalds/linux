// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for NVM parse
 *
 * Copyright (C) 2025 Intel Corporation
 */
#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <iwl-nvm-parse.h>

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static const struct nvm_flag_case {
	const char *desc;
	u16 nvm_flags;
	u32 reg_rule_flags;
	u32 set_reg_rule_flags;
	u32 clear_reg_rule_flags;
} nvm_flag_cases[] = {
	{
		.desc = "Restricting VLP client and AP access",
		.nvm_flags = 0,
		.set_reg_rule_flags = NL80211_RRF_NO_6GHZ_VLP_CLIENT,
		.clear_reg_rule_flags = NL80211_RRF_ALLOW_6GHZ_VLP_AP,
	},
	{
		.desc = "Allow VLP client and AP access",
		.nvm_flags = NVM_CHANNEL_VLP,
		.set_reg_rule_flags = NL80211_RRF_ALLOW_6GHZ_VLP_AP,
		.clear_reg_rule_flags = NL80211_RRF_NO_6GHZ_VLP_CLIENT,
	},
	{
		.desc = "Allow VLP client access, while restricting AP access",
		.nvm_flags = NVM_CHANNEL_VLP | NVM_CHANNEL_VLP_AP_NOT_ALLOWED,
		.set_reg_rule_flags = 0,
		.clear_reg_rule_flags = NL80211_RRF_ALLOW_6GHZ_VLP_AP |
					NL80211_RRF_NO_6GHZ_VLP_CLIENT,
	},
};

KUNIT_ARRAY_PARAM_DESC(nvm_flag, nvm_flag_cases, desc)

static void test_nvm_flags(struct kunit *test)
{
	const struct nvm_flag_case *params = test->param_value;
	struct iwl_reg_capa reg_capa = {};
	u32 flags = 0;

	flags = iwl_nvm_get_regdom_bw_flags(NULL, 0, params->nvm_flags,
					    reg_capa);

	if ((params->set_reg_rule_flags & flags) != params->set_reg_rule_flags)
		KUNIT_FAIL(test, "Expected set bits:0x%08x flags:0x%08x\n",
			   params->set_reg_rule_flags, flags);

	if (params->clear_reg_rule_flags & flags)
		KUNIT_FAIL(test, "Expected clear bits:0x%08x flags:0x%08x\n",
			   params->clear_reg_rule_flags, flags);
}

static struct kunit_case nvm_flags_test_cases[] = {
	KUNIT_CASE_PARAM(test_nvm_flags,
			 nvm_flag_gen_params),
	{},
};

static struct kunit_suite nvm_flags_suite = {
	.name = "iwlwifi-nvm_flags",
	.test_cases = nvm_flags_test_cases,
};

kunit_test_suite(nvm_flags_suite);
