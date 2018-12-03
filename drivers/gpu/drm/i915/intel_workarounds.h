/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _I915_WORKAROUNDS_H_
#define _I915_WORKAROUNDS_H_

#include <linux/slab.h>

struct i915_wa {
	i915_reg_t	  reg;
	u32		  mask;
	u32		  val;
};

struct i915_wa_list {
	const char	*name;
	struct i915_wa	*list;
	unsigned int	count;
};

static inline void intel_wa_list_free(struct i915_wa_list *wal)
{
	kfree(wal->list);
	memset(wal, 0, sizeof(*wal));
}

int intel_ctx_workarounds_init(struct drm_i915_private *dev_priv);
int intel_ctx_workarounds_emit(struct i915_request *rq);

void intel_gt_init_workarounds(struct drm_i915_private *dev_priv);
void intel_gt_apply_workarounds(struct drm_i915_private *dev_priv);

void intel_whitelist_workarounds_apply(struct intel_engine_cs *engine);

#endif
