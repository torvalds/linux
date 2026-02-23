// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#ifndef _DML21_WRAPPER_FPU_H_
#define _DML21_WRAPPER_FPU_H_

#include "os_types.h"
#include "dml_top_soc_parameter_types.h"
#include "dml_top_display_cfg_types.h"

struct dc;
struct dc_state;
struct dml2_configuration_options;
struct dml2_context;
enum dc_validate_mode;

/**
 * dml21_init - Initialize DML21 context
 * @in_dc: dc.
 * @dml_ctx: DML21 context to initialize.
 * @config: dml21 configuration options.
 *
 * Performs FPU-requiring initialization. Must be called with FPU protection.
 */
void dml21_init(const struct dc *in_dc, struct dml2_context *dml_ctx, const struct dml2_configuration_options *config);

/**
 * dml21_validate - Determines if a display configuration is supported or not.
 * @in_dc: dc.
 * @context: dc_state to be validated.
 * @dml_ctx: dml21 context.
 * @validate_mode: DC_VALIDATE_MODE_ONLY and DC_VALIDATE_MODE_AND_STATE_INDEX
 *           will not populate context.res_ctx.
 *
 * Based on fast_validate option internally would call:
 *
 * -dml21_mode_check_and_programming - for DC_VALIDATE_MODE_AND_PROGRAMMING option
 * Calculates if dc_state can be supported on the input display
 * configuration. If supported, generates the necessary HW
 * programming for the new dc_state.
 *
 * -dml21_check_mode_support - for DC_VALIDATE_MODE_ONLY and DC_VALIDATE_MODE_AND_STATE_INDEX option
 * Calculates if dc_state can be supported for the input display
 * config.
 *
 * Context: Two threads may not invoke this function concurrently unless they reference
 *          separate dc_states for validation.
 * Return: True if mode is supported, false otherwise.
 */

void dml21_reinit(const struct dc *in_dc, struct dml2_context *dml_ctx,
		  const struct dml2_configuration_options *config);
bool dml21_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx,
	enum dc_validate_mode validate_mode);

/* Prepare hubp mcache_regs for hubp mcache ID and split coordinate programming */
void dml21_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx);

#endif /* _DML21_WRAPPER_FPU_H_ */
