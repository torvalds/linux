/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef _DMUB_CMD_DAL_H_
#define _DMUB_CMD_DAL_H_

#define NUM_AMBI_LEVEL                  5
#define NUM_AGGR_LEVEL                  4
#define NUM_POWER_FN_SEGS               8
#define NUM_BL_CURVE_SEGS               16

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_cmd_psr_type {
	DMUB_CMD__PSR_SET_VERSION		= 0,
	DMUB_CMD__PSR_COPY_SETTINGS		= 1,
	DMUB_CMD__PSR_ENABLE			= 2,
	DMUB_CMD__PSR_DISABLE			= 3,
	DMUB_CMD__PSR_SET_LEVEL			= 4,
};

enum psr_version {
	PSR_VERSION_1				= 0,
	PSR_VERSION_UNSUPPORTED			= 0xFFFFFFFF,
};

enum dmub_cmd_abm_type {
	DMUB_CMD__ABM_INIT_CONFIG	= 0,
	DMUB_CMD__ABM_SET_PIPE		= 1,
	DMUB_CMD__ABM_SET_BACKLIGHT	= 2,
	DMUB_CMD__ABM_SET_LEVEL		= 3,
	DMUB_CMD__ABM_SET_AMBIENT_LEVEL	= 4,
	DMUB_CMD__ABM_SET_PWM_FRAC	= 5,
};

/*
 * Parameters for ABM2.4 algorithm.
 * Padded explicitly to 32-bit boundary.
 */
struct abm_config_table {
	/* Parameters for crgb conversion */
	uint16_t crgb_thresh[NUM_POWER_FN_SEGS];                 // 0B
	uint16_t crgb_offset[NUM_POWER_FN_SEGS];                 // 15B
	uint16_t crgb_slope[NUM_POWER_FN_SEGS];                  // 31B

	/* Parameters for custom curve */
	uint16_t backlight_thresholds[NUM_BL_CURVE_SEGS];        // 47B
	uint16_t backlight_offsets[NUM_BL_CURVE_SEGS];           // 79B

	uint16_t ambient_thresholds_lux[NUM_AMBI_LEVEL];         // 111B
	uint16_t min_abm_backlight;                              // 121B

	uint8_t min_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 123B
	uint8_t max_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 143B
	uint8_t bright_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL]; // 163B
	uint8_t dark_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 183B
	uint8_t hybrid_factor[NUM_AGGR_LEVEL];                   // 203B
	uint8_t contrast_factor[NUM_AGGR_LEVEL];                 // 207B
	uint8_t deviation_gain[NUM_AGGR_LEVEL];                  // 211B
	uint8_t min_knee[NUM_AGGR_LEVEL];                        // 215B
	uint8_t max_knee[NUM_AGGR_LEVEL];                        // 219B
	uint8_t iir_curve[NUM_AMBI_LEVEL];                       // 223B
	uint8_t pad3[3];                                         // 228B
};

#endif /* _DMUB_CMD_DAL_H_ */
