/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_REG_WHITELIST_
#define _XE_REG_WHITELIST_

#include <linux/types.h>

struct drm_printer;
struct xe_hw_engine;
struct xe_reg_sr_entry;

void xe_reg_whitelist_process_engine(struct xe_hw_engine *hwe);

void xe_reg_whitelist_print_entry(struct drm_printer *p, unsigned int indent,
				  u32 reg, struct xe_reg_sr_entry *entry);

#endif
