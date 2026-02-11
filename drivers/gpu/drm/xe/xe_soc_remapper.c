// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "regs/xe_soc_remapper_regs.h"
#include "xe_mmio.h"
#include "xe_soc_remapper.h"

static void xe_soc_remapper_set_region(struct xe_device *xe, struct xe_reg reg,
				       u32 mask, u32 val)
{
	guard(spinlock_irqsave)(&xe->soc_remapper.lock);
	xe_mmio_rmw32(xe_root_tile_mmio(xe), reg, mask, val);
}

static void xe_soc_remapper_set_telem_region(struct xe_device *xe, u32 index)
{
	xe_soc_remapper_set_region(xe, SG_REMAP_INDEX1, SG_REMAP_TELEM_MASK,
				   REG_FIELD_PREP(SG_REMAP_TELEM_MASK, index));
}

static void xe_soc_remapper_set_sysctrl_region(struct xe_device *xe, u32 index)
{
	xe_soc_remapper_set_region(xe, SG_REMAP_INDEX1, SG_REMAP_SYSCTRL_MASK,
				   REG_FIELD_PREP(SG_REMAP_SYSCTRL_MASK, index));
}

/**
 * xe_soc_remapper_init() - Initialize SoC remapper
 * @xe: Pointer to xe device.
 *
 * Initialize SoC remapper.
 *
 * Return: 0 on success, error code on failure
 */
int xe_soc_remapper_init(struct xe_device *xe)
{
	bool has_soc_remapper = xe->info.has_soc_remapper_telem ||
				xe->info.has_soc_remapper_sysctrl;

	if (has_soc_remapper)
		spin_lock_init(&xe->soc_remapper.lock);

	if (xe->info.has_soc_remapper_telem)
		xe->soc_remapper.set_telem_region = xe_soc_remapper_set_telem_region;

	if (xe->info.has_soc_remapper_sysctrl)
		xe->soc_remapper.set_sysctrl_region = xe_soc_remapper_set_sysctrl_region;

	return 0;
}
