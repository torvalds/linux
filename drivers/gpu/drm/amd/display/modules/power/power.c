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

#include "mod_power.h"
#include "dm_services.h"
#include "dc.h"
#include "core_types.h"
#include "core_dc.h"

#define MOD_POWER_MAX_CONCURRENT_SINKS 32
#define SMOOTH_BRIGHTNESS_ADJUSTMENT_TIME_IN_MS 500

struct sink_caps {
	const struct dc_sink *sink;
};

struct backlight_state {
	unsigned int backlight;
	unsigned int frame_ramp;
	bool smooth_brightness_enabled;
};

struct core_power {
	struct mod_power public;
	struct dc *dc;
	int num_sinks;
	struct sink_caps *caps;
	struct backlight_state *state;
};

union dmcu_abm_set_bl_params {
	struct {
		unsigned int gradual_change : 1; /* [0:0] */
		unsigned int reserved : 15; /* [15:1] */
		unsigned int frame_ramp : 16; /* [31:16] */
	} bits;
	unsigned int u32All;
};

/* Backlight cached properties */
static unsigned int backlight_8bit_lut_array[101];
static unsigned int ac_level_percentage;
static unsigned int dc_level_percentage;
static bool  backlight_caps_valid;
/* we use lazy initialization of backlight capabilities cache */
static bool backlight_caps_initialized;
/* AC/DC levels initialized later in separate context */
static bool  backlight_def_levels_valid;

/* ABM cached properties */
static unsigned int abm_level;
static bool abm_user_enable;
static bool abm_active;

/*PSR cached properties*/
static unsigned int block_psr;

/* Defines default backlight curve F(x) = A(x*x) + Bx + C.
 *
 * Backlight curve should always  satisfy F(0) = min, F(100) = max,
 * so polynom coefficients are:
 * A is 0.0255 - B/100 - min/10000 - (255-max)/10000 = (max - min)/10000 - B/100
 * B is adjustable factor to modify the curve.
 * Bigger B results in less concave curve. B range is [0..(max-min)/100]
 * C is backlight minimum
 */
static const unsigned int backlight_curve_coeff_a_factor = 10000;
static const unsigned int backlight_curve_coeff_b        = 100;
static const unsigned int backlight_curve_coeff_b_factor = 100;

/* Minimum and maximum backlight input signal levels */
static const unsigned int default_min_backlight          = 12;
static const unsigned int default_max_backlight          = 255;

/* Other backlight constants */
static const unsigned int absolute_backlight_max         = 255;

#define MOD_POWER_TO_CORE(mod_power)\
		container_of(mod_power, struct core_power, public)

static bool check_dc_support(const struct dc *dc)
{
	if (dc->stream_funcs.set_backlight == NULL)
		return false;

	return true;
}

/* Given a specific dc_sink* this function finds its equivalent
 * on the dc_sink array and returns the corresponding index
 */
static unsigned int sink_index_from_sink(struct core_power *core_power,
		const struct dc_sink *sink)
{
	unsigned int index = 0;

	for (index = 0; index < core_power->num_sinks; index++)
		if (core_power->caps[index].sink == sink)
			return index;

	/* Could not find sink requested */
	ASSERT(false);
	return index;
}

static unsigned int convertBL8to17(unsigned int backlight_8bit)
{
	unsigned int temp_ulong = backlight_8bit * 0x10101;
	unsigned char temp_uchar =
			(unsigned char)(((temp_ulong & 0x80) >> 7) & 1);

	temp_ulong = (temp_ulong >> 8) + temp_uchar;

	return temp_ulong;
}

static uint16_t convertBL8to16(unsigned int backlight_8bit)
{
	return (uint16_t)((backlight_8bit * 0x10101) >> 8);
}

/*This is used when OS wants to retrieve the current BL.
 * We return the 8bit value to OS.
 */
static unsigned int convertBL17to8(unsigned int backlight_17bit)
{
	if (backlight_17bit & 0x10000)
		return default_max_backlight;
	else
		return (backlight_17bit >> 8);
}

struct mod_power *mod_power_create(struct dc *dc)
{
	struct core_power *core_power =
			dm_alloc(sizeof(struct core_power));

	struct core_dc *core_dc = DC_TO_CORE(dc);

	int i = 0;

	if (core_power == NULL)
		goto fail_alloc_context;

	core_power->caps = dm_alloc(sizeof(struct sink_caps) *
			MOD_POWER_MAX_CONCURRENT_SINKS);

	if (core_power->caps == NULL)
		goto fail_alloc_caps;

	for (i = 0; i < MOD_POWER_MAX_CONCURRENT_SINKS; i++)
		core_power->caps[i].sink = NULL;

	core_power->state = dm_alloc(sizeof(struct backlight_state) *
				MOD_POWER_MAX_CONCURRENT_SINKS);

	if (core_power->state == NULL)
		goto fail_alloc_state;

	core_power->num_sinks = 0;
	backlight_caps_valid = false;

	if (dc == NULL)
		goto fail_construct;

	core_power->dc = dc;

	if (!check_dc_support(dc))
		goto fail_construct;

	abm_user_enable = false;
	abm_active = false;

	return &core_power->public;

fail_construct:
	dm_free(core_power->state);

fail_alloc_state:
	dm_free(core_power->caps);

fail_alloc_caps:
	dm_free(core_power);

fail_alloc_context:
	return NULL;
}


void mod_power_destroy(struct mod_power *mod_power)
{
	if (mod_power != NULL) {
		int i;
		struct core_power *core_power =
				MOD_POWER_TO_CORE(mod_power);

		dm_free(core_power->state);

		for (i = 0; i < core_power->num_sinks; i++)
			dc_sink_release(core_power->caps[i].sink);

		dm_free(core_power->caps);

		dm_free(core_power);
	}
}

bool mod_power_add_sink(struct mod_power *mod_power,
						const struct dc_sink *sink)
{
	if (sink->sink_signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	struct core_power *core_power =
				MOD_POWER_TO_CORE(mod_power);
	struct core_dc *core_dc = DC_TO_CORE(core_power->dc);

	if (core_power->num_sinks < MOD_POWER_MAX_CONCURRENT_SINKS) {
		dc_sink_retain(sink);
		core_power->caps[core_power->num_sinks].sink = sink;
		core_power->state[core_power->num_sinks].
					smooth_brightness_enabled = false;
		core_power->state[core_power->num_sinks].
					backlight = 100;
		core_power->num_sinks++;
		return true;
	}

	return false;
}

bool mod_power_remove_sink(struct mod_power *mod_power,
		const struct dc_sink *sink)
{
	int i = 0, j = 0;
	struct core_power *core_power =
			MOD_POWER_TO_CORE(mod_power);

	for (i = 0; i < core_power->num_sinks; i++) {
		if (core_power->caps[i].sink == sink) {
			/* To remove this sink, shift everything after down */
			for (j = i; j < core_power->num_sinks - 1; j++) {
				core_power->caps[j].sink =
					core_power->caps[j + 1].sink;

				memcpy(&core_power->state[j],
					&core_power->state[j + 1],
					sizeof(struct backlight_state));
			}
			core_power->num_sinks--;
			dc_sink_release(sink);
			return true;
		}
	}
	return false;
}

bool mod_power_set_backlight(struct mod_power *mod_power,
		const struct dc_stream **streams, int num_streams,
		unsigned int backlight_8bit)
{
	struct core_power *core_power =
			MOD_POWER_TO_CORE(mod_power);

	unsigned int frame_ramp = 0;

	unsigned int stream_index, sink_index, vsync_rate_hz;

	union dmcu_abm_set_bl_params params;

	for (stream_index = 0; stream_index < num_streams; stream_index++) {
		if (streams[stream_index]->sink->sink_signal == SIGNAL_TYPE_VIRTUAL) {
			core_power->state[sink_index].backlight = 0;
			core_power->state[sink_index].frame_ramp = 0;
			core_power->state[sink_index].smooth_brightness_enabled = false;
			continue;
		}

		sink_index = sink_index_from_sink(core_power,
				streams[stream_index]->sink);

		vsync_rate_hz = div64_u64(div64_u64((streams[stream_index]->
				timing.pix_clk_khz * 1000),
				streams[stream_index]->timing.v_total),
				streams[stream_index]->timing.h_total);

		core_power->state[sink_index].backlight = backlight_8bit;

		if (core_power->state[sink_index].smooth_brightness_enabled)
			frame_ramp = ((vsync_rate_hz *
				SMOOTH_BRIGHTNESS_ADJUSTMENT_TIME_IN_MS) + 500)
				/ 1000;
		else
			frame_ramp = 0;

		core_power->state[sink_index].frame_ramp = frame_ramp;
	}

	params.u32All = 0;
	params.bits.gradual_change = (frame_ramp > 0);
	params.bits.frame_ramp = frame_ramp;

	core_power->dc->stream_funcs.set_backlight
		(core_power->dc, backlight_8bit, params.u32All, streams[0]);

	return true;
}

bool mod_power_get_backlight(struct mod_power *mod_power,
		const struct dc_sink *sink,
		unsigned int *backlight_8bit)
{
	if (sink->sink_signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	struct core_power *core_power =
				MOD_POWER_TO_CORE(mod_power);

	unsigned int sink_index = sink_index_from_sink(core_power, sink);

	*backlight_8bit = core_power->state[sink_index].backlight;

	return true;
}

/* hard coded to default backlight curve. */
void mod_power_initialize_backlight_caps(struct mod_power
							*mod_power)
{
	struct core_power *core_power =
			MOD_POWER_TO_CORE(mod_power);
	struct core_dc *core_dc = DC_TO_CORE(core_power->dc);
	unsigned int i;

	backlight_caps_initialized = true;

	struct dm_acpi_atif_backlight_caps *pExtCaps = NULL;
	bool customCurvePresent = false;
	bool customMinMaxPresent = false;
	bool customDefLevelsPresent = false;

	/* Allocate memory for ATIF output
	 * (do not want to use 256 bytes on the stack)
	 */
	pExtCaps = (struct dm_acpi_atif_backlight_caps *)
			(dm_alloc(sizeof(struct dm_acpi_atif_backlight_caps)));
	if (pExtCaps == NULL)
		return;

	/* Retrieve ACPI extended brightness caps */
	if (dm_query_extended_brightness_caps
			(core_dc->ctx, AcpiDisplayType_LCD1, pExtCaps)) {
		ac_level_percentage    = pExtCaps->acLevelPercentage;
		dc_level_percentage    = pExtCaps->dcLevelPercentage;
		customMinMaxPresent    = true;
		customDefLevelsPresent = true;
		customCurvePresent     = (pExtCaps->numOfDataPoints > 0);

		ASSERT(pExtCaps->numOfDataPoints <= 99);
	} else {
		dm_free(pExtCaps);
		return;
	}

	if (customMinMaxPresent)
		backlight_8bit_lut_array[0] = pExtCaps->minInputSignal;
	else
		backlight_8bit_lut_array[0] = default_min_backlight;

	if (customMinMaxPresent)
		backlight_8bit_lut_array[100] = pExtCaps->maxInputSignal;
	else
		backlight_8bit_lut_array[100] = default_max_backlight;

	ASSERT(backlight_8bit_lut_array[100] <= absolute_backlight_max);
	ASSERT(backlight_8bit_lut_array[0] <=
					backlight_8bit_lut_array[100]);

	/* Just to make sure we use valid values */
	if (backlight_8bit_lut_array[100] > absolute_backlight_max)
		backlight_8bit_lut_array[100] = absolute_backlight_max;
	if (backlight_8bit_lut_array[0] > backlight_8bit_lut_array[100]) {
		unsigned int swap;

		swap = backlight_8bit_lut_array[0];
		backlight_8bit_lut_array[0] = backlight_8bit_lut_array[100];
		backlight_8bit_lut_array[100] = swap;
	}

	/* Build backlight translation table for custom curve */
	if (customCurvePresent) {
		unsigned int index = 1;
		unsigned int numOfDataPoints =
				(pExtCaps->numOfDataPoints <= 99 ?
						pExtCaps->numOfDataPoints : 99);

		/* Filling translation table from data points -
		 * between every two provided data points we
		 * lineary interpolate missing values
		 */
		for (i = 0; i < numOfDataPoints; i++) {
			/* Clamp signal level between min and max
			 * (since min and max might come other
			 * soruce like registry)
			 */
			unsigned int luminance =
					pExtCaps->dataPoints[i].luminance;
			unsigned int signalLevel =
					pExtCaps->dataPoints[i].signalLevel;

			if (signalLevel < backlight_8bit_lut_array[0])
				signalLevel = backlight_8bit_lut_array[0];
			if (signalLevel > backlight_8bit_lut_array[100])
				signalLevel = backlight_8bit_lut_array[100];

			/* Lineary interpolate missing values */
			if (index < luminance) {
				unsigned int baseValue =
					backlight_8bit_lut_array[index-1];
				unsigned int deltaSignal =
					signalLevel - baseValue;
				unsigned int deltaLuma =
					luminance - index + 1;
				unsigned int step  = deltaSignal;

				for (; index < luminance; index++) {
					backlight_8bit_lut_array[index] =
						baseValue + (step / deltaLuma);
					step += deltaSignal;
				}
			}

			/* Now [index == luminance],
			 * so we can add data point to the translation table
			 */
			backlight_8bit_lut_array[index++] = signalLevel;
		}

		/* Complete the final segment of interpolation -
		 * between last datapoint and maximum value
		 */
		if (index < 100) {
			unsigned int baseValue =
					backlight_8bit_lut_array[index-1];
			unsigned int deltaSignal =
					backlight_8bit_lut_array[100] -
								baseValue;
			unsigned int deltaLuma = 100 - index + 1;
			unsigned int step = deltaSignal;

			for (; index < 100; index++) {
				backlight_8bit_lut_array[index] =
						baseValue + (step / deltaLuma);
				step += deltaSignal;
			}
		}
	/* Build backlight translation table based on default curve */
	} else {
		unsigned int delta =
				backlight_8bit_lut_array[100] -
					backlight_8bit_lut_array[0];
		unsigned int coeffC = backlight_8bit_lut_array[0];
		unsigned int coeffB =
				(backlight_curve_coeff_b < delta ?
					backlight_curve_coeff_b : delta);
		unsigned int coeffA = delta - coeffB; /* coeffB is B*100 */

		for (i = 1; i < 100; i++) {
			backlight_8bit_lut_array[i] =
				(coeffA * i * i) /
				backlight_curve_coeff_a_factor +
				(coeffB * i) /
				backlight_curve_coeff_b_factor +
				coeffC;
		}
	}

	if (pExtCaps != NULL)
		dm_free(pExtCaps);

	/* Successfully initialized */
	backlight_caps_valid = true;
	backlight_def_levels_valid = customDefLevelsPresent;
}

unsigned int mod_power_backlight_level_percentage_to_signal(
		struct mod_power *mod_power, unsigned int percentage)
{
	/* Do lazy initialization of backlight capabilities*/
	if (!backlight_caps_initialized)
		mod_power_initialize_backlight_caps(mod_power);

	/* Since the translation table is indexed by percentage,
	* we simply return backlight value at given percent
	*/
	if (backlight_caps_valid && percentage <= 100)
		return backlight_8bit_lut_array[percentage];

	return -1;
}

unsigned int mod_power_backlight_level_signal_to_percentage(
		struct mod_power *mod_power,
		unsigned int signalLevel8bit)
{
	unsigned int invalid_backlight = (unsigned int)(-1);
	/* Do lazy initialization of backlight capabilities */
	if (!backlight_caps_initialized)
		mod_power_initialize_backlight_caps(mod_power);

	/* If customer curve cannot convert to differentiated value near min
	* it is important to report 0 for min signal to pass setting "Dimmed"
	* setting in HCK brightness2 tests.
	*/
	if (signalLevel8bit <= backlight_8bit_lut_array[0])
		return 0;

	/* Since the translation table is indexed by percentage
	 * we need to do a binary search over the array
	 * Another option would be to guess entry based on linear distribution
	 * and then do linear search in correct direction
	 */
	if (backlight_caps_valid && signalLevel8bit <=
					absolute_backlight_max) {
		unsigned int min = 0;
		unsigned int max = 100;
		unsigned int mid = invalid_backlight;

		while (max >= min) {
			mid = (min + max) / 2; /* floor of half range */

			if (backlight_8bit_lut_array[mid] < signalLevel8bit)
				min = mid + 1;
			else if (backlight_8bit_lut_array[mid] >
							signalLevel8bit)
				max = mid - 1;
			else
				break;

			if (max == 0 || max == 1)
				return invalid_backlight;
		}
		return mid;
	}

	return invalid_backlight;
}


bool mod_power_get_panel_backlight_boundaries(
				struct mod_power *mod_power,
				unsigned int *min_backlight,
				unsigned int *max_backlight,
				unsigned int *output_ac_level_percentage,
				unsigned int *output_dc_level_percentage)
{
	/* Do lazy initialization of backlight capabilities */
	if (!backlight_caps_initialized)
		mod_power_initialize_backlight_caps(mod_power);

	/* If cache was successfully updated,
	 * copy the values to output structure and return success
	 */
	if (backlight_caps_valid) {
		*min_backlight = backlight_8bit_lut_array[0];
		*max_backlight = backlight_8bit_lut_array[100];

		*output_ac_level_percentage = ac_level_percentage;
		*output_dc_level_percentage = dc_level_percentage;

		return true;
	}

	return false;
}

bool mod_power_set_smooth_brightness(struct mod_power *mod_power,
		const struct dc_sink *sink, bool enable_brightness)
{
	if (sink->sink_signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	struct core_power *core_power =
			MOD_POWER_TO_CORE(mod_power);
	unsigned int sink_index = sink_index_from_sink(core_power, sink);

	core_power->state[sink_index].smooth_brightness_enabled
					= enable_brightness;
	return true;
}

bool mod_power_notify_mode_change(struct mod_power *mod_power,
		const struct dc_stream *stream)
{
	if (stream->sink->sink_signal == SIGNAL_TYPE_VIRTUAL)
		return false;

	struct core_power *core_power =
			MOD_POWER_TO_CORE(mod_power);

	unsigned int sink_index = sink_index_from_sink(core_power,
					stream->sink);
	unsigned int frame_ramp = core_power->state[sink_index].frame_ramp;
	union dmcu_abm_set_bl_params params;

	params.u32All = 0;
	params.bits.gradual_change = (frame_ramp > 0);
	params.bits.frame_ramp = frame_ramp;

	core_power->dc->stream_funcs.set_backlight
			(core_power->dc,
			core_power->state[sink_index].backlight,
			params.u32All, stream);

	core_power->dc->stream_funcs.setup_psr
			(core_power->dc, stream);

	return true;
}


static bool mod_power_abm_feature_enable(struct mod_power
		*mod_power, bool enable)
{
	struct core_power *core_power =
					MOD_POWER_TO_CORE(mod_power);
	if (abm_user_enable == enable)
		return true;

	abm_user_enable = enable;

	if (enable) {
		if (abm_level != 0 && abm_active)
			core_power->dc->stream_funcs.set_abm_level
					(core_power->dc, abm_level);
	} else {
		if (abm_level != 0 && abm_active) {
			abm_level = 0;
			core_power->dc->stream_funcs.set_abm_level
					(core_power->dc, abm_level);
		}
	}

	return true;
}

static bool mod_power_abm_activate(struct mod_power
		*mod_power, bool activate)
{
	struct core_power *core_power =
					MOD_POWER_TO_CORE(mod_power);
	if (abm_active == activate)
		return true;

	abm_active = activate;

	if (activate) {
		if (abm_level != 0 && abm_user_enable)
			core_power->dc->stream_funcs.set_abm_level
					(core_power->dc, abm_level);
	} else {
		if (abm_level != 0 && abm_user_enable) {
			abm_level = 0;
			core_power->dc->stream_funcs.set_abm_level
					(core_power->dc, abm_level);
		}
	}

	return true;
}

static bool mod_power_abm_set_level(struct mod_power *mod_power,
		unsigned int level)
{
	struct core_power *core_power =
					MOD_POWER_TO_CORE(mod_power);
	if (abm_level == level)
		return true;

	if (abm_active && abm_user_enable && level == 0)
		core_power->dc->stream_funcs.set_abm_level
			(core_power->dc, 0);
	else if (abm_active && abm_user_enable && level != 0)
		core_power->dc->stream_funcs.set_abm_level
				(core_power->dc, level);

	abm_level = level;

	return true;
}

bool mod_power_varibright_control(struct mod_power *mod_power,
		struct varibright_info *input_varibright_info)
{
	switch (input_varibright_info->cmd) {
	case VariBright_Cmd__SetVBLevel:
	{
		/* Set VariBright user level. */
		mod_power_abm_set_level(mod_power,
				input_varibright_info->level);
	}
	break;

	case VariBright_Cmd__UserEnable:
	{
		/* Set VariBright user enable state. */
		mod_power_abm_feature_enable(mod_power,
				input_varibright_info->enable);
	}
	break;

	case VariBright_Cmd__PostDisplayConfigChange:
	{
		/* Set VariBright user level. */
		mod_power_abm_set_level(mod_power,
						input_varibright_info->level);

		/* Set VariBright user enable state. */
		mod_power_abm_feature_enable(mod_power,
				input_varibright_info->enable);

		/* Set VariBright activate based on power state. */
		mod_power_abm_activate(mod_power,
				input_varibright_info->activate);
	}
	break;

	default:
	{
		return false;
	}
	break;
	}

	return true;
}

bool mod_power_block_psr(bool block_enable, enum dmcu_block_psr_reason reason)
{
	if (block_enable)
		block_psr |= reason;
	else
		block_psr &= ~reason;

	return true;
}


bool mod_power_set_psr_enable(struct mod_power *mod_power,
		bool psr_enable)
{
	struct core_power *core_power =
				MOD_POWER_TO_CORE(mod_power);

	if (block_psr == 0)
		return core_power->dc->stream_funcs.set_psr_enable
				(core_power->dc, psr_enable);

	return false;
}


