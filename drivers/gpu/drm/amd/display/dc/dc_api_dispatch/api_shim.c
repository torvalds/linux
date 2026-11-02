// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dm_services.h"
#include "dc.h"
#include "dc_api_dispatch/api_shim.h"
#include "core2/dc_core2.h"
#include "inc/dc_core_interface.h"

void api_shim_construct(struct dc *dc, const struct dc_init_data *init_params)
{
	/* DM override today; ASIC allow-list later. Owned only by this shim. */
	TO_DC2(dc)->selection = init_params->dc2_selection;
}

void dc_hardware_init(struct dc *dc)
{
	if (TO_DC2(dc)->selection == DC2_SELECTION_CORE2)
		core2_funcs()->hardware_init(dc);
	else
		legacy_hardware_init(dc);
}

void dc_hardware_release(struct dc *dc)
{
	if (TO_DC2(dc)->selection == DC2_SELECTION_CORE2)
		core2_funcs()->hardware_release(dc);
	else
		legacy_hardware_release(dc);
}
