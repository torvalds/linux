/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#pragma once

#include "inc/dc_core_interface.h"

struct dc;

struct dc_core2 {
	const struct dc2_funcs *legacy_funcs;
};

/* Core2-local; selection lives in the API shim, not here. */
const struct dc2_funcs *core2_funcs(void);
void core2_construct(struct dc *dc);
void core2_destruct(struct dc *dc);
