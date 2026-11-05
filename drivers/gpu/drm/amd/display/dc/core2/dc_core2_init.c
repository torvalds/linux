// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dm_services.h"
#include "dc.h"
#include "core2/dc_core2_init.h"

void core2_hardware_init(struct dc *dc)
{
	dc2_legacy_funcs()->hardware_init(dc);
}
