/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_REG_SR_
#define _XE_REG_SR_

/*
 * Reg save/restore bookkeeping
 */

struct xe_device;
struct xe_gt;
struct xe_hw_engine;
struct xe_reg_sr;
struct xe_reg_sr_entry;
struct drm_printer;

int xe_reg_sr_init(struct xe_reg_sr *sr, const char *name, struct xe_device *xe);
void xe_reg_sr_dump(struct xe_reg_sr *sr, struct drm_printer *p);

int xe_reg_sr_add(struct xe_reg_sr *sr, const struct xe_reg_sr_entry *e,
		  struct xe_gt *gt);
void xe_reg_sr_apply_mmio(struct xe_reg_sr *sr, struct xe_gt *gt);
void xe_reg_sr_apply_whitelist(struct xe_hw_engine *hwe);

#endif
