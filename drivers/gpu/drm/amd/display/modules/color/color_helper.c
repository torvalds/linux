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
#include "mod_color.h"
#include "color_helper.h"

const struct gamut_space_entry predefined_gamuts[] = {
	/*                         x_red y_red x_gr  y_gr  x_blue y_blue   a0        a1     a2  a3  gamma */
	[gamut_type_bt709]	= {6400, 3300, 3000, 6000, 1500,  600,     180000,   4500,  99, 99, 2200},
	[gamut_type_bt601]	= {6400, 3300, 2900, 6000, 1500,  600,     180000,   4500,  99, 99, 2200},
	[gamut_type_adobe_rgb]	= {6400, 3300, 2100, 7100, 1500,  600,     180000,   4500,  99, 99, 2200},
	[gamut_type_srgb]	= {6400, 3300, 3000, 6000, 1500,  600,      31308,  12920,  55, 55, 2400},
	[gamut_type_bt2020]	= {7080, 2920, 1700, 7970, 1310,  460,     180000,   4500,  99, 99, 2200}
};

const struct white_point_coodinates_entry predefined_white_points[] = {
	[white_point_type_5000k_horizon]	= {5000, 3473, 3561},
	[white_point_type_6500k_noon]		= {6500, 3127, 3290},
	[white_point_type_7500k_north_sky]	= {7500, 3022, 3129},
	[white_point_type_9300k]		= {9300, 2866, 2950}
};

const unsigned int white_point_entries = 91;

const struct white_point_coodinates_entry white_point_temps[] = {
	/*001*/{1000, 6499, 3474},
	/*002*/{1100, 6361, 3594},
	/*003*/{1200, 6226, 3703},
	/*004*/{1300, 6095, 3801},
	/*005*/{1400, 5966, 3887},
	/*006*/{1500, 5841, 3962},
	/*007*/{1600, 5720, 4025},
	/*008*/{1700, 5601, 4076},
	/*009*/{1800, 5486, 4118},
	/*010*/{1900, 5375, 4150},
	/*011*/{2000, 5267, 4173},
	/*012*/{2100, 5162, 4188},
	/*013*/{2200, 5062, 4196},
	/*014*/{2300, 4965, 4198},
	/*015*/{2400, 4872, 4194},
	/*016*/{2500, 4782, 4186},
	/*017*/{2600, 4696, 4173},
	/*018*/{2700, 4614, 4158},
	/*019*/{2800, 4535, 4139},
	/*020*/{2900, 4460, 4118},
	/*021*/{3000, 4388, 4095},
	/*022*/{3100, 4320, 4070},
	/*023*/{3200, 4254, 4044},
	/*024*/{3300, 4192, 4018},
	/*025*/{3400, 4132, 3990},
	/*026*/{3500, 4075, 3962},
	/*027*/{3600, 4021, 3934},
	/*028*/{3700, 3969, 3905},
	/*029*/{3800, 3919, 3877},
	/*030*/{3900, 3872, 3849},
	/*031*/{4000, 3827, 3820},
	/*032*/{4100, 3784, 3793},
	/*033*/{4200, 3743, 3765},
	/*034*/{4300, 3704, 3738},
	/*035*/{4400, 3666, 3711},
	/*036*/{4500, 3631, 3685},
	/*037*/{4600, 3596, 3659},
	/*038*/{4700, 3563, 3634},
	/*039*/{4800, 3532, 3609},
	/*040*/{4900, 3502, 3585},
	/*041*/{5000, 3473, 3561},
	/*042*/{5100, 3446, 3538},
	/*043*/{5200, 3419, 3516},
	/*044*/{5300, 3394, 3494},
	/*045*/{5400, 3369, 3472},
	/*046*/{5500, 3346, 3451},
	/*047*/{5600, 3323, 3431},
	/*048*/{5700, 3302, 3411},
	/*049*/{5800, 3281, 3392},
	/*050*/{5900, 3261, 3373},
	/*051*/{6000, 3242, 3355},
	/*052*/{6100, 3223, 3337},
	/*053*/{6200, 3205, 3319},
	/*054*/{6300, 3188, 3302},
	/*055*/{6400, 3161, 3296},
	/*056*/{6500, 3127, 3290}, /* This is  the real white point sRGB */
	/*057*/{6600, 3126, 3264},
	/*058*/{6700, 3125, 3238},
	/*059*/{6800, 3110, 3224},
	/*060*/{6900, 3097, 3209},
	/*061*/{7000, 3083, 3195},
	/*062*/{7100, 3070, 3181},
	/*063*/{7200, 3058, 3168},
	/*064*/{7300, 3045, 3154},
	/*065*/{7400, 3034, 3142},
	/*066*/{7500, 3022, 3129},
	/*067*/{7600, 3011, 3117},
	/*068*/{7700, 3000, 3105},
	/*069*/{7800, 2990, 3094},
	/*070*/{7900, 2980, 3082},
	/*071*/{8000, 2970, 3071},
	/*072*/{8100, 2961, 3061},
	/*073*/{8200, 2952, 3050},
	/*074*/{8300, 2943, 3040},
	/*075*/{8400, 2934, 3030},
	/*076*/{8500, 2926, 3020},
	/*077*/{8600, 2917, 3011},
	/*078*/{8700, 2910, 3001},
	/*079*/{8800, 2902, 2992},
	/*080*/{8900, 2894, 2983},
	/*081*/{9000, 2887, 2975},
	/*082*/{9100, 2880, 2966},
	/*083*/{9200, 2873, 2958},
	/*084*/{9300, 2866, 2950},
	/*085*/{9400, 2860, 2942},
	/*086*/{9500, 2853, 2934},
	/*087*/{9600, 2847, 2927},
	/*088*/{9700, 2841, 2919},
	/*089*/{9800, 2835, 2912},
	/*090*/{9900, 2829, 2905},
	/*091*/{10000, 2824, 2898}
};

bool mod_color_find_predefined_gamut(
		struct gamut_space_coordinates *out_gamut,
		enum predefined_gamut_type type)
{
	out_gamut->redX = predefined_gamuts[type].redX;
	out_gamut->redY = predefined_gamuts[type].redY;
	out_gamut->greenX = predefined_gamuts[type].greenX;
	out_gamut->greenY = predefined_gamuts[type].greenY;
	out_gamut->blueX = predefined_gamuts[type].blueX;
	out_gamut->blueY = predefined_gamuts[type].blueY;

	return true;
}

bool mod_color_find_predefined_white_point(
		struct white_point_coodinates *out_white_point,
		enum predefined_white_point_type type)
{
	out_white_point->whiteX = predefined_white_points[type].whiteX;
	out_white_point->whiteY = predefined_white_points[type].whiteY;

	return true;
}

bool mod_color_find_white_point_from_temperature(
		struct white_point_coodinates *out_white_point,
		unsigned int temperature)
{
	int i;
	unsigned int found = false;
	struct white_point_coodinates_entry temp_white_point =
			white_point_temps[55];

	if (temperature < 1000 || temperature > 10000)
		return false;

	for (i = 0; i < white_point_entries; i++) {
		if (temperature == white_point_temps[i].temperature) {
			temp_white_point = white_point_temps[i];
			found = true;
			break;
		}
	}

	out_white_point->whiteX = temp_white_point.whiteX;
	out_white_point->whiteY = temp_white_point.whiteY;

	return found;
}

bool mod_color_find_temperature_from_white_point(
		struct white_point_coodinates *in_white_point,
		unsigned int *out_temperature)
{
	unsigned int i;
	*out_temperature = 6500;

	for (i = 0; i < white_point_entries; i++) {
		if (in_white_point->whiteX == white_point_temps[i].whiteX &&
			in_white_point->whiteY == white_point_temps[i].whiteY) {
			*out_temperature = white_point_temps[i].temperature;
			return true;
		}
	}

	return false;
}

