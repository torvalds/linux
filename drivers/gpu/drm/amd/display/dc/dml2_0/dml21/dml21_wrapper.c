// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dml2_internal_types.h"
#include "dml_top.h"
#include "dml2_core_dcn4_calcs.h"
#include "dml2_internal_shared_types.h"
#include "dml21_utils.h"
#include "dml21_translation_helper.h"
#include "dml2_dc_resource_mgmt.h"
#include "dml2_wrapper.h"
#include "dml2_wrapper_fpu.h"
#include "dml21_wrapper.h"
#include "dml21_wrapper_fpu.h"
#include "dc_fpu.h"

#if !defined(DC_RUN_WITH_PREEMPTION_ENABLED)
#define DC_RUN_WITH_PREEMPTION_ENABLED(code) code
#endif // !DC_RUN_WITH_PREEMPTION_ENABLED

#define INVALID -1

static bool dml21_allocate_memory(struct dml2_context **dml_ctx)
{
	DC_RUN_WITH_PREEMPTION_ENABLED(*dml_ctx = vzalloc(sizeof(struct dml2_context)));
	if (!(*dml_ctx))
		return false;

	DC_RUN_WITH_PREEMPTION_ENABLED((*dml_ctx)->v21.dml_init.dml2_instance = vzalloc(sizeof(struct dml2_instance)));
	if (!((*dml_ctx)->v21.dml_init.dml2_instance))
		return false;

	(*dml_ctx)->v21.mode_support.dml2_instance = (*dml_ctx)->v21.dml_init.dml2_instance;
	(*dml_ctx)->v21.mode_programming.dml2_instance = (*dml_ctx)->v21.dml_init.dml2_instance;

	(*dml_ctx)->v21.mode_support.display_config = &(*dml_ctx)->v21.display_config;
	(*dml_ctx)->v21.mode_programming.display_config = (*dml_ctx)->v21.mode_support.display_config;

	DC_RUN_WITH_PREEMPTION_ENABLED((*dml_ctx)->v21.mode_programming.programming = vzalloc(sizeof(struct dml2_display_cfg_programming)));

	if (!((*dml_ctx)->v21.mode_programming.programming))
		return false;

	return true;
}

bool dml21_create(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config)
{
	/* Allocate memory for initializing DML21 instance */
	if (!dml21_allocate_memory(dml_ctx))
		return false;

	dml21_init(in_dc, *dml_ctx, config);

	return true;
}

void dml21_destroy(struct dml2_context *dml2)
{
	vfree(dml2->v21.dml_init.dml2_instance);
	vfree(dml2->v21.mode_programming.programming);
}

void dml21_copy(struct dml2_context *dst_dml_ctx,
	struct dml2_context *src_dml_ctx)
{
	/* Preserve references to internals */
	struct dml2_instance *dst_dml2_instance = dst_dml_ctx->v21.dml_init.dml2_instance;
	struct dml2_display_cfg_programming *dst_dml2_programming = dst_dml_ctx->v21.mode_programming.programming;

	/* Copy context */
	memcpy(dst_dml_ctx, src_dml_ctx, sizeof(struct dml2_context));

	/* Copy Internals */
	memcpy(dst_dml2_instance, src_dml_ctx->v21.dml_init.dml2_instance, sizeof(struct dml2_instance));
	memcpy(dst_dml2_programming, src_dml_ctx->v21.mode_programming.programming, sizeof(struct dml2_display_cfg_programming));

	/* Restore references to internals */
	dst_dml_ctx->v21.dml_init.dml2_instance = dst_dml2_instance;

	dst_dml_ctx->v21.mode_support.dml2_instance = dst_dml2_instance;
	dst_dml_ctx->v21.mode_programming.dml2_instance = dst_dml2_instance;

	dst_dml_ctx->v21.mode_support.display_config = &dst_dml_ctx->v21.display_config;
	dst_dml_ctx->v21.mode_programming.display_config = dst_dml_ctx->v21.mode_support.display_config;

	dst_dml_ctx->v21.mode_programming.programming = dst_dml2_programming;

	/* need to initialize copied instance for internal references to be correct */
	dml2_initialize_instance(&dst_dml_ctx->v21.dml_init);
}

bool dml21_create_copy(struct dml2_context **dst_dml_ctx,
	struct dml2_context *src_dml_ctx)
{
	/* Allocate memory for initializing DML21 instance */
	if (!dml21_allocate_memory(dst_dml_ctx))
		return false;

	dml21_copy(*dst_dml_ctx, src_dml_ctx);

	return true;
}

