/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_REG_SR_
#define _XE_REG_SR_

#include "xe_reg_sr_types.h"

/*
 * Reg save/restore bookkeeping
 */

struct xe_device;
struct xe_gt;
struct drm_printer;

int xe_reg_sr_init(struct xe_reg_sr *sr, const char *name, struct xe_device *xe);
void xe_reg_sr_dump(struct xe_reg_sr *sr, struct drm_printer *p);

int xe_reg_sr_add(struct xe_reg_sr *sr, u32 reg,
		  const struct xe_reg_sr_entry *e);
void xe_reg_sr_apply_mmio(struct xe_reg_sr *sr, struct xe_gt *gt);
void xe_reg_sr_apply_whitelist(struct xe_reg_sr *sr, u32 mmio_base,
			       struct xe_gt *gt);

#endif
