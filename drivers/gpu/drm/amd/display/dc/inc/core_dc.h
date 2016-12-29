/*
 * core_dc.h
 *
 *  Created on: Nov 13, 2015
 *      Author: yonsun
 */

#ifndef __CORE_DC_H__
#define __CORE_DC_H__

#include "core_types.h"
#include "hw_sequencer.h"

#define DC_TO_CORE(dc)\
	container_of(dc, struct core_dc, public)

struct core_dc {
	struct dc public;
	struct dc_context *ctx;

	uint8_t link_count;
	struct core_link *links[MAX_PIPES * 2];

	struct validate_context *current_context;
	struct validate_context *temp_flip_context;
	struct validate_context *scratch_val_ctx;
	struct resource_pool *res_pool;

	/*Power State*/
	enum dc_video_power_state previous_power_state;
	enum dc_video_power_state current_power_state;

	/* Display Engine Clock levels */
	struct dm_pp_clock_levels sclk_lvls;

	/* Inputs into BW and WM calculations. */
	struct bw_calcs_dceip bw_dceip;
	struct bw_calcs_vbios bw_vbios;

	/* HW functions */
	struct hw_sequencer_funcs hwss;
	struct dce_hwseq *hwseq;

	/* temp store of dm_pp_display_configuration
	 * to compare to see if display config changed
	 */
	struct dm_pp_display_configuration prev_display_config;
};

#endif /* __CORE_DC_H__ */
