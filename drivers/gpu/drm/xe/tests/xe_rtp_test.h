/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_RTP_TEST_H_
#define _XE_RTP_TEST_H_

#include <linux/types.h>

struct xe_device;
struct xe_gt;
struct xe_hw_engine;
struct xe_rtp_rule;

bool xe_rtp_rule_matches(const struct xe_device *xe,
			 struct xe_gt *gt,
			 struct xe_hw_engine *hwe,
			 const struct xe_rtp_rule *rules,
			 unsigned int n_rules,
			 int *err);

#endif
