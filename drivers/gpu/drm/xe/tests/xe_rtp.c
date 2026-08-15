// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include "tests/xe_rtp_test.h"

#include <kunit/visibility.h>

/**
 * xe_rtp_rule_matches - Check if a set of RTP rule set match against the
 *			 device/GT/hwe
 * @xe: The xe device
 * @gt: The GT struct (may be NULL)
 * @hwe: The hw_engine  (may be NULL)
 * @rules: The array of rules to match against
 * @n_rules: Number of items in @rules
 * @err: Pointer (may be NULL) to set error number.
 *
 * This parses the set of rules and check if they match against the passed
 * parameters.
 *
 * If passed, @err is updated with a non-zero negative error number or zero if
 * no errors were found during the parsing/evaluation of rules.
 *
 * Returns true if there is a match and false if there is no match or if an
 * error was found.
 */
bool xe_rtp_rule_matches(const struct xe_device *xe,
			 struct xe_gt *gt,
			 struct xe_hw_engine *hwe,
			 const struct xe_rtp_rule *rules,
			 unsigned int n_rules,
			 int *err)
{
	return rule_matches_with_err(xe, gt, hwe, rules, n_rules, err);
}
EXPORT_SYMBOL_IF_KUNIT(xe_rtp_rule_matches);
