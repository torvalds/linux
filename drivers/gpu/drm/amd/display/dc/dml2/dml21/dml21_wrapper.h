// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.


#ifndef _DML21_WRAPPER_H_
#define _DML21_WRAPPER_H_

#include "os_types.h"
#include "dml_top_soc_parameter_types.h"
#include "dml_top_display_cfg_types.h"

struct dc;
struct dc_state;
struct dml2_configuration_options;
struct dml2_context;
enum dc_validate_mode;

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
void dml21_reinit(const struct dc *in_dc, struct dml2_context *dml_ctx, const struct dml2_configuration_options *config);

/**
 * dml21_validate - Determines if a display configuration is supported or not.
 * @in_dc: dc.
 * @context: dc_state to be validated.
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

 * Context: Two threads may not invoke this function concurrently unless they reference
 *          separate dc_states for validation.
 * Return: True if mode is supported, false otherwise.
 */
bool dml21_validate(const struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx,
	enum dc_validate_mode validate_mode);

/* Prepare hubp mcache_regs for hubp mcache ID and split coordinate programming */
void dml21_prepare_mcache_programming(struct dc *in_dc, struct dc_state *context, struct dml2_context *dml_ctx);

/* Structure for inputting external SOCBB and DCNIP values for tool based debugging. */
struct socbb_ip_params_external {
	struct dml2_ip_capabilities ip_params;
	struct dml2_soc_bb soc_bb;
};

/*mcache parameters decided by dml*/
struct dc_mcache_params {
	bool valid;
	/*
	* For iMALL, dedicated mall mcaches are required (sharing of last
	* slice possible), for legacy phantom or phantom without return
	* the only mall mcaches need to be valid.
	*/
	bool requires_dedicated_mall_mcache;
	unsigned int num_mcaches_plane0;
	unsigned int num_mcaches_plane1;
	/*
	* Generally, plane0/1 slices must use a disjoint set of caches
	* but in some cases the final segement of the two planes can
	* use the same cache. If plane0_plane1 is set, then this is
	* allowed.
	*
	* Similarly, the caches allocated to MALL prefetcher are generally
	* disjoint, but if mall_prefetch is set, then the final segment
	* between the main and the mall pixel requestor can use the same
	* cache.
	*
	* Note that both bits may be set at the same time.
	*/
	struct {
		bool mall_comb_mcache_p0;
		bool mall_comb_mcache_p1;
		bool plane0_plane1;
	} last_slice_sharing;
	/*
	* A plane is divided into vertical slices of mcaches,
	* which wrap on the surface width.
	*
	* For example, if the surface width is 7680, and split into
	* three slices of equal width, the boundary array would contain
	* [2560, 5120, 7680]
	*
	* The assignments are
	* 0 = [0 .. 2559]
	* 1 = [2560 .. 5119]
	* 2 = [5120 .. 7679]
	* 0 = [7680 .. INF]
	* The final element implicitly is the same as the first, and
	* at first seems invalid since it is never referenced (since)
	* it is outside the surface. However, its useful when shifting
	* (see below).
	*
	* For any given valid mcache assignment, a shifted version, wrapped
	* on the surface width boundary is also assumed to be valid.
	*
	* For example, shifting [2560, 5120, 7680] by -50 results in
	* [2510, 5170, 7630].
	*
	* The assignments are now:
	* 0 = [0 .. 2509]
	* 1 = [2510 .. 5169]
	* 2 = [5170 .. 7629]
	* 0 = [7630 .. INF]
	*/
	int mcache_x_offsets_plane0[DML2_MAX_MCACHES + 1];
	int mcache_x_offsets_plane1[DML2_MAX_MCACHES + 1];
};
#endif
