// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#include "dml2_internal_types.h"
#include "dml2_wrapper_fpu.h"

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

static void dml2_init(const struct dc *in_dc, const struct dml2_configuration_options *config, struct dml2_context **dml2)
{
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01)) {
		dml21_reinit(in_dc, *dml2, config);
		return;
	}

	// Store config options
	(*dml2)->config = *config;

	switch (in_dc->ctx->dce_version) {
	case DCN_VERSION_3_5:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn35;
		break;
	case DCN_VERSION_3_51:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn351;
		break;
	case DCN_VERSION_3_6:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn36;
		break;
	case DCN_VERSION_3_2:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn32;
		break;
	case DCN_VERSION_3_21:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn321;
		break;
	case DCN_VERSION_4_01:
		(*dml2)->v20.dml_core_ctx.project = dml_project_dcn401;
		break;
	default:
		(*dml2)->v20.dml_core_ctx.project = dml_project_default;
		break;
	}

	DC_FP_START();

	initialize_dml2_ip_params(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.ip);

	initialize_dml2_soc_bbox(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.soc);

	initialize_dml2_soc_states(*dml2, in_dc, &(*dml2)->v20.dml_core_ctx.soc, &(*dml2)->v20.dml_core_ctx.states);

	DC_FP_END();
}

bool dml2_create(const struct dc *in_dc, const struct dml2_configuration_options *config, struct dml2_context **dml2)
{
	// TODO : Temporarily add DCN_VERSION_3_2 for N-1 validation. Remove DCN_VERSION_3_2 after N-1 validation phase is complete.
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01))
		return dml21_create(in_dc, dml2, config);

	// Allocate Mode Lib Ctx
	*dml2 = dml2_allocate_memory();

	if (!(*dml2))
		return false;

	dml2_init(in_dc, config, dml2);

	return true;
}

void dml2_reinit(const struct dc *in_dc,
				 const struct dml2_configuration_options *config,
				 struct dml2_context **dml2)
{
	if ((in_dc->debug.using_dml21) && (in_dc->ctx->dce_version >= DCN_VERSION_4_01)) {
		dml21_reinit(in_dc, *dml2, config);
		return;
	}

	dml2_init(in_dc, config, dml2);
}
