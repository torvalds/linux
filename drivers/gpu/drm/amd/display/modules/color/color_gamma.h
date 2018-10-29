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

#ifndef COLOR_MOD_COLOR_GAMMA_H_
#define COLOR_MOD_COLOR_GAMMA_H_

struct dc_transfer_func;
struct dc_gamma;
struct dc_transfer_func_distributed_points;
struct dc_rgb_fixed;
enum dc_transfer_func_predefined;

/* For SetRegamma ADL interface support
 * Must match escape type
 */
union regamma_flags {
	unsigned int raw;
	struct {
		unsigned int gammaRampArray       :1;    // RegammaRamp is in use
		unsigned int gammaFromEdid        :1;    //gamma from edid is in use
		unsigned int gammaFromEdidEx      :1;    //gamma from edid is in use , but only for Display Id 1.2
		unsigned int gammaFromUser        :1;    //user custom gamma is used
		unsigned int coeffFromUser        :1;    //coeff. A0-A3 from user is in use
		unsigned int coeffFromEdid        :1;    //coeff. A0-A3 from edid is in use
		unsigned int applyDegamma         :1;    //flag for additional degamma correction in driver
		unsigned int gammaPredefinedSRGB  :1;    //flag for SRGB gamma
		unsigned int gammaPredefinedPQ    :1;    //flag for PQ gamma
		unsigned int gammaPredefinedPQ2084Interim :1;    //flag for PQ gamma, lower max nits
		unsigned int gammaPredefined36    :1;    //flag for 3.6 gamma
		unsigned int gammaPredefinedReset :1;    //flag to return to previous gamma
	} bits;
};

struct regamma_ramp {
	unsigned short gamma[256*3];  // gamma ramp packed  in same way as OS windows ,r , g & b
};

struct regamma_coeff {
	int    gamma[3];
	int    A0[3];
	int    A1[3];
	int    A2[3];
	int    A3[3];
};

struct regamma_lut {
	union regamma_flags flags;
	union {
		struct regamma_ramp ramp;
		struct regamma_coeff coeff;
	};
};

void setup_x_points_distribution(void);
void precompute_pq(void);
void precompute_de_pq(void);

bool mod_color_calculate_regamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp, bool canRomBeUsed);

bool mod_color_calculate_degamma_params(struct dc_transfer_func *output_tf,
		const struct dc_gamma *ramp, bool mapUserRamp);

bool mod_color_calculate_curve(enum dc_transfer_func_predefined  trans,
		struct dc_transfer_func_distributed_points *points,
		uint32_t sdr_ref_white_level);

bool mod_color_calculate_degamma_curve(enum dc_transfer_func_predefined trans,
				struct dc_transfer_func_distributed_points *points);

bool calculate_user_regamma_coeff(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma);

bool calculate_user_regamma_ramp(struct dc_transfer_func *output_tf,
		const struct regamma_lut *regamma);


#endif /* COLOR_MOD_COLOR_GAMMA_H_ */
