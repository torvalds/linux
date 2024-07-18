// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef _DML21_WRAPPER_H_
#define _DML21_WRAPPER_H_

#include "os_types.h"
#include "dml_top_soc_parameter_types.h"

struct dc;
struct dc_state;
struct dml2_configuration_options;
struct dml2_context;

/**
 * dml2_create - Creates dml21_context.
 * @in_dc: dc.
 * @dml2: Created dml21 context.
 * @config: dml21 configuration options.
 *
 * Create of DML21 is done as part of dc_state creation.
 * DML21 IP, SOC and STATES are initialized at
 * creation time.
 *
 * Return: True if dml2 is successfully created, false otherwise.
 */
bool dml21_create(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config);
void dml21_destroy(struct dml2_context *dml2);
void dml21_copy(struct dml2_context *dst_dml_ctx,
	struct dml2_context *src_dml_ctx);
bool dml21_create_copy(struct dml2_context **dst_dml_ctx,
	struct dml2_context *src_dml_ctx);
void dml21_reinit(const struct dc *in_dc, struct dml2_context **dml_ctx, const struct dml2_configuration_options *config);

/**
 * dml21_validate - Determines if a display configuration is supported or not.
 * @in_dc: dc.
 * @context: dc_state to be validated.
 * @fast_validate: Fast validate will not populate context.res_ctx.
 *
 * Based on fast_validate option internally would call:
 *
 * -dml21_mode_check_and_programming - for non fast_validate option
 * Calculates if dc_state can be supported on the input display
 * configuration. If supported, generates the necessary HW
 * programming for the new dc_state.
 *
 * -dml21_check_mode_support - for fast_validate option
 * Calculates if dc_state can be supported for the input display
 * config.

 * Context: Two threads may not invoke this function concurrently unless they reference
 *          separate dc_states for validation.
 * Return: True if mode is supported, false otherwise.
 */
bool dml21_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx, bool fast_validate);

/* Prepare hubp mcache_regs for hubp mcache ID and split coordinate programming */
void dml21_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx);

/* Structure for inputting external SOCBB and DCNIP values for tool based debugging. */
struct socbb_ip_params_external {
	struct dml2_ip_capabilities ip_params;
	struct dml2_soc_bb soc_bb;
};
#endif
