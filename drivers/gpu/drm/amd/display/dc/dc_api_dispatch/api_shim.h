/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#pragma once

#include "dc.h"
#include "inc/dc_core_interface.h"

struct dc2 {
	/* First member: TO_DC2 is a typed upcast, not container_of. */
	struct dc dc;
	enum dc2_selection selection;
};

#define TO_DC2(ptr) ((struct dc2 *)(ptr))

void api_shim_construct(struct dc *dc, const struct dc_init_data *init_params);
