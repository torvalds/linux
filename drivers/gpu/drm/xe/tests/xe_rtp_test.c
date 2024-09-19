// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/string.h>
#include <linux/xarray.h>

#include <drm/drm_drv.h>
#include <drm/drm_kunit_helpers.h>

#include <kunit/test.h>

#include "regs/xe_gt_regs.h"
#include "regs/xe_reg_defs.h"
#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_kunit_helpers.h"
#include "xe_pci_test.h"
#include "xe_reg_sr.h"
#include "xe_rtp.h"

#define REGULAR_REG1	XE_REG(1)
#define REGULAR_REG2	XE_REG(2)
#define REGULAR_REG3	XE_REG(3)
#define MCR_REG1	XE_REG_MCR(1)
#define MCR_REG2	XE_REG_MCR(2)
#define MCR_REG3	XE_REG_MCR(3)
#define MASKED_REG1	XE_REG(1, XE_REG_OPTION_MASKED)

#undef XE_REG_MCR
#define XE_REG_MCR(...)     XE_REG(__VA_ARGS__, .mcr = 1)

struct rtp_to_sr_test_case {
	const char *name;
	struct xe_reg expected_reg;
	u32 expected_set_bits;
	u32 expected_clr_bits;
	unsigned long expected_count_sr_entries;
	unsigned int expected_sr_errors;
	unsigned long expected_active;
	const struct xe_rtp_entry_sr *entries;
};

struct rtp_test_case {
	const char *name;
	unsigned long expected_active;
	const struct xe_rtp_entry *entries;
};

static bool match_yes(const struct xe_gt *gt, const struct xe_hw_engine *hwe)
{
	return true;
}

static bool match_no(const struct xe_gt *gt, const struct xe_hw_engine *hwe)
{
	return false;
}

static const struct rtp_to_sr_test_case rtp_to_sr_cases[] = {
	{
		.name = "coalesce-same-reg",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0) | REG_BIT(1),
		.expected_clr_bits = REG_BIT(0) | REG_BIT(1),
		.expected_active = BIT(0) | BIT(1),
		.expected_count_sr_entries = 1,
		/* Different bits on the same register: create a single entry */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(1)))
			},
			{}
		},
	},
	{
		.name = "no-match-no-add",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0),
		.expected_count_sr_entries = 1,
		/* Don't coalesce second entry since rules don't match */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_no)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(1)))
			},
			{}
		},
	},
	{
		.name = "match-or",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0) | REG_BIT(1) | REG_BIT(2),
		.expected_clr_bits = REG_BIT(0) | REG_BIT(1) | REG_BIT(2),
		.expected_active = BIT(0) | BIT(1) | BIT(2),
		.expected_count_sr_entries = 1,
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("first"),
			  XE_RTP_RULES(FUNC(match_yes), OR, FUNC(match_no)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("middle"),
			  XE_RTP_RULES(FUNC(match_no), FUNC(match_no), OR,
				       FUNC(match_yes), OR,
				       FUNC(match_no)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(1)))
			},
			{ XE_RTP_NAME("last"),
			  XE_RTP_RULES(FUNC(match_no), OR, FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(2)))
			},
			{ XE_RTP_NAME("no-match"),
			  XE_RTP_RULES(FUNC(match_no), OR, FUNC(match_no)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(3)))
			},
			{}
		},
	},
	{
		.name = "match-or-xfail",
		.expected_reg = REGULAR_REG1,
		.expected_count_sr_entries = 0,
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("leading-or"),
			  XE_RTP_RULES(OR, FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("trailing-or"),
			  /*
			   * First condition is match_no, otherwise the failure
			   * wouldn't really trigger as RTP stops processing as
			   * soon as it has a matching set of rules
			   */
			  XE_RTP_RULES(FUNC(match_no), OR),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(1)))
			},
			{ XE_RTP_NAME("no-or-or-yes"),
			  XE_RTP_RULES(FUNC(match_no), OR, OR, FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(2)))
			},
			{}
		},
	},
	{
		.name = "no-match-no-add-multiple-rules",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0),
		.expected_count_sr_entries = 1,
		/* Don't coalesce second entry due to one of the rules */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes), FUNC(match_no)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(1)))
			},
			{}
		},
	},
	{
		.name = "two-regs-two-entries",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0) | BIT(1),
		.expected_count_sr_entries = 2,
		/* Same bits on different registers are not coalesced */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG2, REG_BIT(0)))
			},
			{}
		},
	},
	{
		.name = "clr-one-set-other",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(1) | REG_BIT(0),
		.expected_active = BIT(0) | BIT(1),
		.expected_count_sr_entries = 1,
		/* Check clr vs set actions on different bits */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(CLR(REGULAR_REG1, REG_BIT(1)))
			},
			{}
		},
	},
	{
#define TEMP_MASK	REG_GENMASK(10, 8)
#define TEMP_FIELD	REG_FIELD_PREP(TEMP_MASK, 2)
		.name = "set-field",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = TEMP_FIELD,
		.expected_clr_bits = TEMP_MASK,
		.expected_active = BIT(0),
		.expected_count_sr_entries = 1,
		/* Check FIELD_SET works */
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(FIELD_SET(REGULAR_REG1,
						   TEMP_MASK, TEMP_FIELD))
			},
			{}
		},
#undef TEMP_MASK
#undef TEMP_FIELD
	},
	{
		.name = "conflict-duplicate",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0) | BIT(1),
		.expected_count_sr_entries = 1,
		.expected_sr_errors = 1,
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			/* drop: setting same values twice */
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			{}
		},
	},
	{
		.name = "conflict-not-disjoint",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0) | BIT(1),
		.expected_count_sr_entries = 1,
		.expected_sr_errors = 1,
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			/* drop: bits are not disjoint with previous entries */
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(CLR(REGULAR_REG1, REG_GENMASK(1, 0)))
			},
			{}
		},
	},
	{
		.name = "conflict-reg-type",
		.expected_reg = REGULAR_REG1,
		.expected_set_bits = REG_BIT(0),
		.expected_clr_bits = REG_BIT(0),
		.expected_active = BIT(0) | BIT(1) | BIT(2),
		.expected_count_sr_entries = 1,
		.expected_sr_errors = 2,
		.entries = (const struct xe_rtp_entry_sr[]) {
			{ XE_RTP_NAME("basic-1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(REGULAR_REG1, REG_BIT(0)))
			},
			/* drop: regular vs MCR */
			{ XE_RTP_NAME("basic-2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(MCR_REG1, REG_BIT(1)))
			},
			/* drop: regular vs masked */
			{ XE_RTP_NAME("basic-3"),
			  XE_RTP_RULES(FUNC(match_yes)),
			  XE_RTP_ACTIONS(SET(MASKED_REG1, REG_BIT(0)))
			},
			{}
		},
	},
};

static void xe_rtp_process_to_sr_tests(struct kunit *test)
{
	const struct rtp_to_sr_test_case *param = test->param_value;
	struct xe_device *xe = test->priv;
	struct xe_gt *gt = xe_device_get_root_tile(xe)->primary_gt;
	struct xe_reg_sr *reg_sr = &gt->reg_sr;
	const struct xe_reg_sr_entry *sre, *sr_entry = NULL;
	struct xe_rtp_process_ctx ctx = XE_RTP_PROCESS_CTX_INITIALIZER(gt);
	unsigned long idx, count_sr_entries = 0, count_rtp_entries = 0, active = 0;

	xe_reg_sr_init(reg_sr, "xe_rtp_to_sr_tests", xe);

	while (param->entries[count_rtp_entries].rules)
		count_rtp_entries++;

	xe_rtp_process_ctx_enable_active_tracking(&ctx, &active, count_rtp_entries);
	xe_rtp_process_to_sr(&ctx, param->entries, reg_sr);

	xa_for_each(&reg_sr->xa, idx, sre) {
		if (idx == param->expected_reg.addr)
			sr_entry = sre;

		count_sr_entries++;
	}

	KUNIT_EXPECT_EQ(test, active, param->expected_active);

	KUNIT_EXPECT_EQ(test, count_sr_entries, param->expected_count_sr_entries);
	if (count_sr_entries) {
		KUNIT_EXPECT_EQ(test, sr_entry->clr_bits, param->expected_clr_bits);
		KUNIT_EXPECT_EQ(test, sr_entry->set_bits, param->expected_set_bits);
		KUNIT_EXPECT_EQ(test, sr_entry->reg.raw, param->expected_reg.raw);
	} else {
		KUNIT_EXPECT_NULL(test, sr_entry);
	}

	KUNIT_EXPECT_EQ(test, reg_sr->errors, param->expected_sr_errors);
}

/*
 * Entries below follow the logic used with xe_wa_oob.rules:
 * 1) Entries with empty name are OR'ed: all entries marked active since the
 *    last entry with a name
 * 2) There are no action associated with rules
 */
static const struct rtp_test_case rtp_cases[] = {
	{
		.name = "active1",
		.expected_active = BIT(0),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			},
			{}
		},
	},
	{
		.name = "active2",
		.expected_active = BIT(0) | BIT(1),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			},
			{ XE_RTP_NAME("r2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			},
			{}
		},
	},
	{
		.name = "active-inactive",
		.expected_active = BIT(0),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_yes)),
			},
			{ XE_RTP_NAME("r2"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{}
		},
	},
	{
		.name = "inactive-active",
		.expected_active = BIT(1),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{ XE_RTP_NAME("r2"),
			  XE_RTP_RULES(FUNC(match_yes)),
			},
			{}
		},
	},
	{
		.name = "inactive-1st_or_active-inactive",
		.expected_active = BIT(1),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{ XE_RTP_NAME("r2_or_conditions"),
			  XE_RTP_RULES(FUNC(match_yes), OR,
				       FUNC(match_no), OR,
				       FUNC(match_no)) },
			{ XE_RTP_NAME("r3"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{}
		},
	},
	{
		.name = "inactive-2nd_or_active-inactive",
		.expected_active = BIT(1),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{ XE_RTP_NAME("r2_or_conditions"),
			  XE_RTP_RULES(FUNC(match_no), OR,
				       FUNC(match_yes), OR,
				       FUNC(match_no)) },
			{ XE_RTP_NAME("r3"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{}
		},
	},
	{
		.name = "inactive-last_or_active-inactive",
		.expected_active = BIT(1),
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{ XE_RTP_NAME("r2_or_conditions"),
			  XE_RTP_RULES(FUNC(match_no), OR,
				       FUNC(match_no), OR,
				       FUNC(match_yes)) },
			{ XE_RTP_NAME("r3"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{}
		},
	},
	{
		.name = "inactive-no_or_active-inactive",
		.expected_active = 0,
		.entries = (const struct xe_rtp_entry[]) {
			{ XE_RTP_NAME("r1"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{ XE_RTP_NAME("r2_or_conditions"),
			  XE_RTP_RULES(FUNC(match_no), OR,
				       FUNC(match_no), OR,
				       FUNC(match_no)) },
			{ XE_RTP_NAME("r3"),
			  XE_RTP_RULES(FUNC(match_no)),
			},
			{}
		},
	},
};

static void xe_rtp_process_tests(struct kunit *test)
{
	const struct rtp_test_case *param = test->param_value;
	struct xe_device *xe = test->priv;
	struct xe_gt *gt = xe_device_get_root_tile(xe)->primary_gt;
	struct xe_rtp_process_ctx ctx = XE_RTP_PROCESS_CTX_INITIALIZER(gt);
	unsigned long count_rtp_entries = 0, active = 0;

	while (param->entries[count_rtp_entries].rules)
		count_rtp_entries++;

	xe_rtp_process_ctx_enable_active_tracking(&ctx, &active, count_rtp_entries);
	xe_rtp_process(&ctx, param->entries);

	KUNIT_EXPECT_EQ(test, active, param->expected_active);
}

static void rtp_to_sr_desc(const struct rtp_to_sr_test_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(rtp_to_sr, rtp_to_sr_cases, rtp_to_sr_desc);

static void rtp_desc(const struct rtp_test_case *t, char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(rtp, rtp_cases, rtp_desc);

static int xe_rtp_test_init(struct kunit *test)
{
	struct xe_device *xe;
	struct device *dev;
	int ret;

	dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);

	xe = xe_kunit_helper_alloc_xe_device(test, dev);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, xe);

	/* Initialize an empty device */
	test->priv = NULL;
	ret = xe_pci_fake_device_init(xe);
	KUNIT_ASSERT_EQ(test, ret, 0);

	xe->drm.dev = dev;
	test->priv = xe;

	return 0;
}

static void xe_rtp_test_exit(struct kunit *test)
{
	struct xe_device *xe = test->priv;

	drm_kunit_helper_free_device(test, xe->drm.dev);
}

static struct kunit_case xe_rtp_tests[] = {
	KUNIT_CASE_PARAM(xe_rtp_process_to_sr_tests, rtp_to_sr_gen_params),
	KUNIT_CASE_PARAM(xe_rtp_process_tests, rtp_gen_params),
	{}
};

static struct kunit_suite xe_rtp_test_suite = {
	.name = "xe_rtp",
	.init = xe_rtp_test_init,
	.exit = xe_rtp_test_exit,
	.test_cases = xe_rtp_tests,
};

kunit_test_suite(xe_rtp_test_suite);
