// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_soc_remapper.h"

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
	spin_lock_init(&xe->soc_remapper.lock);

	return 0;
}
