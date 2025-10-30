// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#include "dml2_internal_types.h"

bool dml2_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml2,
	enum dc_validate_mode validate_mode)
{
	bool out = false;

	if (!dml2)
		return false;
	dml2_apply_debug_options(in_dc, dml2);

	/* DML2.1 validation path */
	if (dml2->architecture == dml2_architecture_21) {
		out = dml21_validate(in_dc, context, dml2, validate_mode);
		return out;
	}

	DC_FP_START();

	/* Use dml_validate_only for DC_VALIDATE_MODE_ONLY and DC_VALIDATE_MODE_AND_STATE_INDEX path */
	if (validate_mode != DC_VALIDATE_MODE_AND_PROGRAMMING)
		out = dml2_validate_only(context, validate_mode);
	else
		out = dml2_validate_and_build_resource(in_dc, context, validate_mode);

	DC_FP_END();

	return out;
}
