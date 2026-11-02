/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 */

#pragma once

struct dc;

/*
 * Construction-time Core implementation selection. Zero is LEGACY so existing
 * callers that leave dc uninitialized keep production behavior.
 */
enum dc2_selection {
	DC2_SELECTION_LEGACY = 0,
	DC2_SELECTION_CORE2,
};

enum dc2_hw_ownership {
	DC2_HW_OWNERSHIP_RELEASED = 0,
	DC2_HW_OWNERSHIP_OWNED,
};

struct dc2_funcs {
	void (*hardware_init)(struct dc *dc);
	void (*hardware_release)(struct dc *dc);
};

void legacy_hardware_init(struct dc *dc);
void legacy_hardware_release(struct dc *dc);
const struct dc2_funcs *dc2_legacy_funcs(void);
