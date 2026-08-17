/* Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef MODULES_POWER_POWER_HELPERS_H_
#define MODULES_POWER_POWER_HELPERS_H_

#include "dc/inc/hw/dmcu.h"
#include "dc/inc/hw/abm.h"
#include "dc/inc/core_types.h"
#include "mod_power.h"

struct resource_pool;


enum abm_defines {
	abm_defines_max_level = 4,
	abm_defines_max_config = 4,
};

struct dmcu_iram_parameters {
	unsigned int *backlight_lut_array;
	unsigned int backlight_lut_array_size;
	bool backlight_ramping_override;
	unsigned int backlight_ramping_reduction;
	unsigned int backlight_ramping_start;
	unsigned int min_abm_backlight;
	unsigned int set;
};
struct backlight_state {
	/* HW uses u16.16 format for backlight PWM */
	unsigned int backlight_pwm;
	/* DM may call power module to set backlight
	 * targeting percent brightness
	 */
	unsigned int backlight_millipercent;
	/* DM may call power module to set backlight based on an explicit
	 * nits value.
	 */
	unsigned int backlight_millinit;
	unsigned int frame_ramp;
	bool smooth_brightness_enabled;
	bool isHDR;
};
struct power_entity {
	struct dc_stream_state *stream;
	struct psr_caps *caps;
	struct mod_power_psr_context *psr_context;

	/*PSR cached properties*/
	bool psr_enabled;
	unsigned int psr_events;
	unsigned int psr_power_opt;
	unsigned int replay_events;
};

struct pwr_backlight_properties {
	bool use_nits_based_brightness;
	bool disable_fractional_pwm;

	unsigned int min_abm_backlight;
	unsigned int num_backlight_levels;

	bool backlight_ramping_override;
	unsigned int backlight_ramping_reduction;
	unsigned int backlight_ramping_start;

	/* Backlight cached properties */
	unsigned int ac_backlight_percent;
	unsigned int dc_backlight_percent;

	/* backlight LUT stored in HW u16.16 format*/
	unsigned int *backlight_lut;
	unsigned int min_backlight_pwm;
	unsigned int max_backlight_pwm;
	unsigned int backlight_range;

	/* Describes the panel's min and max luminance in millinits measured
	 * on full white screen, in min and max backlight settings.
	 */
	unsigned int min_brightness_millinits;
	unsigned int max_brightness_millinits;
	unsigned int nits_range;

	bool backlight_caps_valid;
	bool use_custom_backlight_caps;
	unsigned int custom_backlight_caps_config_no;
	bool use_linear_backlight_curve;
};

struct dmcu_varibright_cached_properties {
	unsigned int varibright_config_setting;
	unsigned int varibright_level;
	unsigned int varibright_hw_level;
	unsigned int def_varibright_level;
	bool varibright_user_enable;
	bool varibright_active;
};

struct core_power {
	struct mod_power mod_public;
	struct dc *dc;
	struct power_entity *map;
	struct dmcu_varibright_cached_properties varibright_prop;
	struct pwr_backlight_properties bl_prop[MAX_NUM_EDP];
	struct backlight_state bl_state[MAX_NUM_EDP];
	unsigned int edp_num;

	bool psr_smu_optimizations_support;
	bool multi_disp_optimizations_support;

	unsigned int num_entities;
};

union dmcu_abm_set_bl_params {
	struct {
		unsigned int gradual_change : 1; /* [0:0] */
		unsigned int reserved : 15; /* [15:1] */
		unsigned int frame_ramp : 16; /* [31:16] */
	} bits;
	unsigned int u32All;
};
bool dmcu_load_iram(struct dmcu *dmcu,
		struct dmcu_iram_parameters params);
bool dmub_init_abm_config(struct resource_pool *res_pool,
		struct dmcu_iram_parameters params,
		unsigned int inst);

void init_replay_config(struct dc_link *link, struct replay_config *pr_config);
void set_replay_coasting_vtotal(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t vtotal);
void set_replay_defer_update_coasting_vtotal(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t vtotal);
void set_replay_frame_skip_number(struct dc_link *link,
	enum replay_coasting_vtotal_type type,
	uint32_t coasting_vtotal_refresh_rate_Mhz,
	uint32_t flicker_free_refresh_rate_Mhz,
	bool is_defer);
void update_replay_coasting_vtotal_from_defer(struct dc_link *link,
	enum replay_coasting_vtotal_type type);
void set_replay_low_rr_full_screen_video_src_vtotal(struct dc_link *link, uint16_t vtotal);
void calculate_replay_link_off_frame_count(struct dc_link *link,
	uint16_t vtotal, uint16_t htotal);

bool is_psr_su_specific_panel(struct dc_link *link);
void mod_power_calc_psr_configs(struct psr_config *psr_config,
		struct dc_link *link,
		const struct dc_stream_state *stream);
bool mod_power_only_edp(const struct dc_state *context,
		const struct dc_stream_state *stream);
bool psr_su_set_dsc_slice_height(struct dc *dc, struct dc_link *link,
			      struct dc_stream_state *stream,
			      struct psr_config *config);

bool fill_custom_backlight_caps(unsigned int config_no,
		struct dm_acpi_atif_backlight_caps *caps);
void reset_replay_dsync_error_count(struct dc_link *link);
void change_replay_to_psr(struct dc_link *link);
void change_psr_to_replay(struct dc_link *link);
void initialize_backlight_caps(struct core_power *core_power, unsigned int inst);
unsigned int backlight_millipercent_to_pwm(
		struct core_power *core_power, unsigned int millipercent, unsigned int inst);
unsigned int backlight_millipercent_to_millinit(
		struct core_power *core_power, unsigned int millipercent, unsigned int inst);
void fill_backlight_level_params(struct core_power *core_power,
	struct set_backlight_level_params *backlight_level_params,
	int panel_inst, uint8_t aux_inst, unsigned int backlight_pwm,
	enum backlight_control_type backlight_control_type,
	unsigned int backlight_millinit, unsigned int transition_time_millisec,
	bool is_hdr);
bool mod_power_hw_init_backlight(struct mod_power *mod_power);
void mod_power_update_backlight_on_mode_change(
    struct core_power *core_power,
    struct dc_link *link,
    unsigned int panel_inst,
    uint8_t aux_inst,
    bool is_hdr);
unsigned int map_index_from_stream(struct core_power *core_power,
		const struct dc_stream_state *stream);
bool mod_power_psr_notify_mode_change(struct mod_power *mod_power,
	const struct dc_stream_state *stream,
	struct dc_link *link,
	unsigned int stream_index);
void mod_power_replay_notify_mode_change(struct mod_power *mod_power,
	struct dc *dc,
	struct dc_link *link,
	const struct dc_stream_state *stream,
	unsigned int stream_index);
#endif /* MODULES_POWER_POWER_HELPERS_H_ */
