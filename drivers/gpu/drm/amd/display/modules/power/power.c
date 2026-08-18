/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dc.h"
#include "mod_power.h"
#include "core_types.h"
#include "dmcu.h"
#include "abm.h"
#include "power_helpers.h"
#include "dce/dmub_psr.h"
#include "dal_asic_id.h"
#include "link_service.h"
#include <linux/math.h>

#define DC_TRACE_LEVEL_MESSAGE(...) /* do nothing */
#define DC_TRACE_LEVEL_MESSAGEP(...) /* do nothing */

#define MOD_POWER_MAX_CONCURRENT_STREAMS 32
#define SMOOTH_BRIGHTNESS_ADJUSTMENT_TIME_IN_MS 500
#define LOW_REFRESH_RATE_DURATION_US_UPPER_BOUND 25000

/* If system or panel does not report some sort of brightness percent to nits
 * mapping, we will use following default values so backlight control using
 * nits based interfaces will still work, but might not describe panel
 * correctly. In this case percentage based backlight control should ideally
 * be used.
 * Min = 5 nits
 * Max = 300 nits
 */

static const unsigned int pwr_default_min_brightness_millinits = 1000;
static const unsigned int pwr_default_sdr_brightness_millinits = 270000;

static const unsigned int default_ac_backlight_percent   = 100;
static const unsigned int default_dc_backlight_percent   = 70;

#define MOD_POWER_TO_CORE(mod_power)\
		container_of(mod_power, struct core_power, mod_public)

/* Given a specific dc_stream* this function finds its equivalent
 * on the core_freesync->map and returns the corresponding index
 */
unsigned int map_index_from_stream(struct core_power *core_power,
		const struct dc_stream_state *stream)
{
	unsigned int index = 0;

	for (index = 0; index < core_power->num_entities; index++) {
		if (core_power->map[index].stream == stream)
			return index;
	}
	/* Could not find stream requested, this is not trivial, fix when hit*/
	DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
						WPP_BIT_FLAG_Firmware_PsrState,
						"map index from stream: ERROR: core_power=%p stream=%p",
						core_power,
						stream);
	ASSERT(false);
	/* We come here only when we can't map stream index.
	 * In good cases, this would happen when we attempt to change
	 * brightness before stream creation, in which case we create a
	 * dummy stream with index 0.
	 * With external monitor connected, the index passed from this return
	 * is 1. Passing anything greater than 0 from here would always point
	 * to bad memory.
	 */
	return 0;
}

bool mod_power_hw_init(struct mod_power *mod_power)
{
	/* Call backlight initialization */
	return mod_power_hw_init_backlight(mod_power);

	/* Future: Add other HW init here */
}

struct mod_power *mod_power_create(struct dc *dc,
		struct mod_power_init_params *init_params,
		unsigned int edp_num)
{
	struct core_power *core_power = NULL;
	int i = 0;
	unsigned int abm_max_config = 0;
	unsigned int inst = 0;
	bool is_brightness_range_valid = false;

	if (dc == NULL)
		goto fail_dc_null;

	core_power = kzalloc(sizeof(struct core_power), GFP_KERNEL);

	if (core_power == NULL)
		goto fail_alloc_context;

	core_power->edp_num = edp_num;
	core_power->map = kzalloc(sizeof(struct power_entity) * MOD_POWER_MAX_CONCURRENT_STREAMS,
				  GFP_KERNEL);

	if (core_power->map == NULL)
		goto fail_alloc_map;

	for (i = 0; i < MOD_POWER_MAX_CONCURRENT_STREAMS; i++) {
		core_power->map[i].stream = NULL;
	}

	for (i = 0; i < MOD_POWER_MAX_CONCURRENT_STREAMS; i++) {
		core_power->map[i].psr_context =
				kzalloc(sizeof(struct mod_power_psr_context),
					GFP_KERNEL);
		if (core_power->map[i].psr_context == NULL)
			goto fail_construct;
	}

	core_power->psr_smu_optimizations_support = init_params->allow_psr_smu_optimizations;
	core_power->multi_disp_optimizations_support = init_params->allow_psr_multi_disp_optimizations;

	for (inst = 0; inst < edp_num; inst++) {
		core_power->bl_prop[inst].min_abm_backlight =
				init_params[inst].min_abm_backlight;
		core_power->bl_prop[inst].disable_fractional_pwm =
				init_params[inst].disable_fractional_pwm;
		core_power->bl_prop[inst].use_linear_backlight_curve =
				init_params[inst].use_linear_backlight_curve;
		core_power->bl_prop[inst].use_nits_based_brightness =
				init_params[inst].use_nits_based_brightness;
		core_power->bl_prop[inst].backlight_ramping_override =
				init_params[inst].backlight_ramping_override;
		core_power->bl_prop[inst].backlight_ramping_reduction =
				init_params[inst].backlight_ramping_reduction;
		core_power->bl_prop[inst].backlight_ramping_start =
				init_params[inst].backlight_ramping_start;
		core_power->bl_prop[inst].use_custom_backlight_caps =
				init_params[inst].use_custom_backlight_caps;
		core_power->bl_prop[inst].custom_backlight_caps_config_no =
				init_params[inst].custom_backlight_caps_config_no;

		// Do not allow less than 101 backlight levels
		if (init_params[inst].num_backlight_levels < 101)
			core_power->bl_prop[inst].num_backlight_levels = 101;
		else
			core_power->bl_prop[inst].num_backlight_levels =
				init_params[inst].num_backlight_levels;

		core_power->bl_prop[inst].backlight_lut = (unsigned int *)
				(kzalloc(sizeof(unsigned int) *
				core_power->bl_prop[inst].num_backlight_levels, GFP_KERNEL));
		if (core_power->bl_prop[inst].backlight_lut == NULL)
			goto fail_alloc_backlight_array;
	}

	core_power->varibright_prop.varibright_active = false;

	core_power->varibright_prop.varibright_user_enable =
			init_params->def_varibright_enable;

	// Table of ABM levels here is 1-4, but level 0 also exists as 'off'
	if (init_params->varibright_level <= abm_defines_max_level) {
		core_power->varibright_prop.varibright_level =
			init_params->varibright_level;

	} else {
		core_power->varibright_prop.varibright_level = 3;
	}
	if (init_params->def_varibright_level <= abm_defines_max_level) {
		core_power->varibright_prop.def_varibright_level =
			init_params->def_varibright_level;
	} else {
		core_power->varibright_prop.def_varibright_level = 3;
	}

	// ABM used to contain 4 different configs. There is only 3 since ABM 2.3.
	if ((dc->res_pool->dmcu != NULL) && (dc->res_pool->dmcu->dmcu_version.abm_version < 0x23))
		abm_max_config = 4;
	else
		abm_max_config = 3;

	if (init_params->abm_config_setting < abm_max_config)
		core_power->varibright_prop.varibright_config_setting =
			init_params->abm_config_setting;
	else
		core_power->varibright_prop.varibright_config_setting = 0;

	for (inst = 0; inst < edp_num; inst++) {
		core_power->bl_prop[inst].backlight_lut[0] = init_params[inst].min_backlight_pwm;
		core_power->bl_prop[inst].backlight_lut[
			core_power->bl_prop[inst].num_backlight_levels-1] =
				init_params[inst].max_backlight_pwm;
		core_power->bl_prop[inst].min_backlight_pwm = init_params[inst].min_backlight_pwm;
		core_power->bl_prop[inst].max_backlight_pwm = init_params[inst].max_backlight_pwm;
		core_power->bl_prop[inst].ac_backlight_percent =
				default_ac_backlight_percent;
		core_power->bl_prop[inst].dc_backlight_percent =
				default_dc_backlight_percent;
		core_power->bl_prop[inst].backlight_caps_valid = false;

		if (core_power->bl_prop[inst].use_nits_based_brightness) {
			core_power->bl_prop[inst].min_brightness_millinits =
					init_params[inst].panel_min_millinits;
			core_power->bl_prop[inst].max_brightness_millinits =
					init_params[inst].panel_max_millinits;
		} else {

			core_power->bl_prop[inst].min_brightness_millinits =
					pwr_default_min_brightness_millinits;
			core_power->bl_prop[inst].max_brightness_millinits =
					pwr_default_sdr_brightness_millinits;
		}

		core_power->bl_prop[inst].backlight_range =
				core_power->bl_prop[inst].max_backlight_pwm-
				core_power->bl_prop[inst].min_backlight_pwm;

		core_power->bl_prop[inst].nits_range =
				core_power->bl_prop[inst].max_brightness_millinits -
				core_power->bl_prop[inst].min_brightness_millinits;

		core_power->bl_state[inst].smooth_brightness_enabled = true;
	}

	/* Check if at least 1 instance in core_power is populated before failing */
	for (inst = 0; inst < edp_num; inst++) {
		if (core_power->bl_prop[inst].nits_range != 0 && core_power->bl_prop[inst].backlight_range != 0) {
			is_brightness_range_valid = true;
			break;
		}

	}
	if (!is_brightness_range_valid)
		goto fail_bad_brightness_range;

	core_power->num_entities = 0;

	core_power->dc = dc;
	for (inst = 0; inst < edp_num; inst++) {
		initialize_backlight_caps(core_power, inst);
		core_power->bl_state[inst].backlight_millipercent =
			core_power->bl_prop[inst].dc_backlight_percent * 1000;
		core_power->bl_state[inst].backlight_pwm = backlight_millipercent_to_pwm(core_power,
		core_power->bl_state[inst].backlight_millipercent, inst);
		core_power->bl_state[inst].backlight_millinit = backlight_millipercent_to_millinit(core_power,
		core_power->bl_state[inst].backlight_millipercent, inst);
	}

	return &core_power->mod_public;

fail_bad_brightness_range:
fail_alloc_backlight_array:
	for (inst = 0; inst < edp_num; inst++)
		if (core_power->bl_prop[inst].backlight_lut)
			kfree(core_power->bl_prop[inst].backlight_lut);
fail_construct:
	for (i = 0; i < MOD_POWER_MAX_CONCURRENT_STREAMS; i++) {
		if (core_power->map[i].psr_context)
			kfree(core_power->map[i].psr_context);
	}
	kfree(core_power->map);

fail_alloc_map:
	kfree(core_power);

fail_alloc_context:
fail_dc_null:
	return NULL;
}

void mod_power_destroy(struct mod_power *mod_power)
{
	if (mod_power != NULL) {
		unsigned int i;
		struct core_power *core_power =
				MOD_POWER_TO_CORE(mod_power);

		for (i = 0; i < MOD_POWER_MAX_CONCURRENT_STREAMS; i++)
			if (core_power->map[i].psr_context)
				kfree(core_power->map[i].psr_context);

		for (i = 0; i < core_power->num_entities; i++)
			if (core_power->map[i].stream)
				dc_stream_release(core_power->map[i].stream);

		kfree(core_power->map);

		for (i = 0; i < MAX_NUM_EDP; i++)
			if (core_power->bl_prop[i].backlight_lut)
				kfree(core_power->bl_prop[i].backlight_lut);

		kfree(core_power);
	}
}

bool mod_power_add_stream(struct mod_power *mod_power,
		struct dc_stream_state *stream, struct psr_caps *caps)
{
	struct core_power *core_power = NULL;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities < MOD_POWER_MAX_CONCURRENT_STREAMS) {
		dc_stream_retain(stream);

		core_power->map[core_power->num_entities].stream = stream;
		core_power->map[core_power->num_entities].caps = caps;

		// initialize cached PSR params to something "safe" (something that is
		// consistent with disabled PSR state)
		core_power->map[core_power->num_entities].psr_enabled = 0;
		core_power->map[core_power->num_entities].psr_events = psr_event_vsync;
		core_power->map[core_power->num_entities].psr_power_opt = 0;
		core_power->num_entities++;
		return true;
	}

	DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
						WPP_BIT_FLAG_Firmware_PsrState,
						"mod_power: add_stream: ERROR: stream=%p num_entities=%u >= MOD_POWER_MAX_CONCURRENT_STREAMS",
						stream,
						core_power->num_entities);

	return false;
}

bool mod_power_remove_stream(struct mod_power *mod_power,
		const struct dc_stream_state *stream)
{
	unsigned int i = 0;
	struct core_power *core_power = NULL;
	unsigned int index = 0;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0) {
		/* trying to remove a stream a second time or have not added yet */
		BREAK_TO_DEBUGGER();
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"mod_power: remove_stream: ERROR: num_entities=0 stream=%p",
							stream);
		return false;
	}

	index = map_index_from_stream(core_power, stream);

	if (index >= core_power->num_entities) {
		/* trying to remove a stream a second time or have not added yet */
		BREAK_TO_DEBUGGER();
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"mod_power: remove_stream: ERROR: index=%u >= num_entities=%u stream=%p",
							index,
							core_power->num_entities,
							stream);
		return false;
	}

	dc_stream_release(core_power->map[index].stream);
	core_power->map[index].stream = NULL;
	/* To remove this entity, shift everything after down */
	for (i = index; i < core_power->num_entities - 1; i++) {
		core_power->map[i].stream = core_power->map[i + 1].stream;
		core_power->map[i].caps = core_power->map[i + 1].caps;

		// copy over cached parameters in case they map to PSR capable display
		core_power->map[i].psr_enabled = core_power->map[i + 1].psr_enabled;
		core_power->map[i].psr_events = core_power->map[i + 1].psr_events;
		core_power->map[i].psr_power_opt = core_power->map[i + 1].psr_power_opt;

		memcpy(core_power->map[i].psr_context, core_power->map[i + 1].psr_context, sizeof(struct mod_power_psr_context));
		memset(core_power->map[i + 1].psr_context, 0, sizeof(struct mod_power_psr_context));
	}
	core_power->num_entities--;

	return true;
}

/*
 * Replace_stream should be used when there is a mode set for existing
 * display target with a valid stream. In this case might need to retain
 * cached PSR state (events, power opt, en/dis) if we are dealing with PSR
 * capable display. If mod_power_remove and mod_power_add are used instead,
 * then stream may be assigned to a different slot and may end up with
 * wrong cached PSR state. It is hard to tell which PSR events should
 * persist through mode set or what psr_events should be initialized to, so
 * it might be better just to retain them all.
 */
bool mod_power_replace_stream(struct mod_power *mod_power,
		const struct dc_stream_state *current_stream,
		struct dc_stream_state *new_stream,
		struct psr_caps *new_caps)
{
	struct core_power *core_power = NULL;
	unsigned int index = 0;

	if (mod_power == NULL)
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);
	if (core_power->num_entities == 0) {
		/* no streams exist in the table yet */
		BREAK_TO_DEBUGGER();
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"mod_power: replace_stream: ERROR: num_entities=0 stream=%p",
							current_stream);
		return false;
	}

	index = map_index_from_stream(core_power, current_stream);

	if (index >= core_power->num_entities) {
		/* trying to replace a non-existent stream */
		BREAK_TO_DEBUGGER();
		DC_TRACE_LEVEL_MESSAGE(DAL_TRACE_LEVEL_ERROR,
							WPP_BIT_FLAG_Firmware_PsrState,
							"mod_power: replace_stream: ERROR: index=%u >= num_entities=%u stream=%p",
							index,
							core_power->num_entities,
							current_stream);
		return false;
	}

	dc_stream_release(core_power->map[index].stream);
	dc_stream_retain(new_stream);
	core_power->map[index].stream = new_stream;
	core_power->map[index].caps = new_caps;
	memset(core_power->map[index].psr_context, 0, sizeof(struct mod_power_psr_context));

	return true;
}
bool mod_power_notify_mode_change(struct mod_power *mod_power,
		const struct dc_stream_state *stream,
		bool is_hdr)
{
	unsigned int stream_index = 0;
	struct core_power *core_power = NULL;
	struct dc_link *link = NULL;
	struct dc *dc = NULL;
	unsigned int panel_inst = 0;
	uint8_t aux_inst = 0;

	if ((mod_power == NULL) || (stream == NULL))
		return false;

	core_power = MOD_POWER_TO_CORE(mod_power);

	if (core_power->num_entities == 0)
		return false;

	stream_index = map_index_from_stream(core_power, stream);

	if (stream_index >= core_power->num_entities)
		return false;

	dc = core_power->dc;
	link = dc_stream_get_link(stream);

	if (link != NULL && dc_get_edp_link_panel_inst(dc, link, &panel_inst)) {
		if (link->ctx->dc->config.dp_connector_no_native_i2c && link->no_ddc_pin) {
			aux_inst = (uint8_t)link->aux_hw_inst;
		} else {
			ASSERT(link->ddc->ddc_pin->hw_info.ddc_channel <= 0xFF);
			aux_inst = (uint8_t)link->ddc->ddc_pin->hw_info.ddc_channel;
		}

		mod_power_update_backlight_on_mode_change(core_power, link, panel_inst, aux_inst, is_hdr);

		/* Handle PSR notification */
		mod_power_psr_notify_mode_change(mod_power, stream, link, stream_index);

		/* Handle Replay notification */
		mod_power_replay_notify_mode_change(mod_power, dc, link, stream, stream_index);
	}

	return true;
}
