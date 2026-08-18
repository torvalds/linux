/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_REG_WHITELIST_H_
#define _XE_REG_WHITELIST_H_

#include <linux/types.h>

struct drm_printer;
struct xe_gt;
struct xe_hw_engine;
struct xe_reg_sr;
struct xe_reg_sr_entry;

void xe_reg_whitelist_process_engine(struct xe_hw_engine *hwe);

void xe_reg_whitelist_oa_regs(struct xe_gt *gt);
void xe_reg_dewhitelist_oa_regs(struct xe_gt *gt);

void xe_reg_whitelist_print_entry(struct drm_printer *p, unsigned int indent,
				  u32 reg, struct xe_reg_sr_entry *entry);

void xe_reg_whitelist_dump(struct xe_reg_sr *sr, struct drm_printer *p);

#endif
