// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#include "dm_services.h"
#include "dc.h"
#include "core2/dc_core2.h"
#include "core2/dc_core2_init.h"

static void core2_hardware_release(struct dc *dc)
{
	dc2_legacy_funcs()->hardware_release(dc);
}

static const struct dc2_funcs core2_funcs_table = {
	.hardware_init = core2_hardware_init,
	.hardware_release = core2_hardware_release,
};

const struct dc2_funcs *core2_funcs(void)
{
	return &core2_funcs_table;
}

void core2_construct(struct dc *dc)
{
	(void)dc;
}

void core2_destruct(struct dc *dc)
{
	(void)dc;
}
