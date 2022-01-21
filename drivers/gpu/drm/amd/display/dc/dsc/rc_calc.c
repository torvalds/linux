
/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#include "rc_calc.h"

/**
 * calc_rc_params - reads the user's cmdline mode
 * @rc: DC internal DSC parameters
 * @pps: DRM struct with all required DSC values
 *
 * This function expects a drm_dsc_config data struct with all the required DSC
 * values previously filled out by our driver and based on this information it
 * computes some of the DSC values.
 *
 * @note This calculation requires float point operation, most of it executes
 * under kernel_fpu_{begin,end}.
 */
void calc_rc_params(struct rc_params *rc, const struct drm_dsc_config *pps)
{
	enum colour_mode mode;
	enum bits_per_comp bpc;
	bool is_navite_422_or_420;
	u16 drm_bpp = pps->bits_per_pixel;
	int slice_width  = pps->slice_width;
	int slice_height = pps->slice_height;

	mode = pps->convert_rgb ? CM_RGB : (pps->simple_422  ? CM_444 :
					   (pps->native_422  ? CM_422 :
					    pps->native_420  ? CM_420 : CM_444));
	bpc = (pps->bits_per_component == 8) ? BPC_8 : (pps->bits_per_component == 10)
					     ? BPC_10 : BPC_12;

	is_navite_422_or_420 = pps->native_422 || pps->native_420;

	DC_FP_START();
	_do_calc_rc_params(rc, mode, bpc, drm_bpp, is_navite_422_or_420,
			   slice_width, slice_height,
			   pps->dsc_version_minor);
	DC_FP_END();
}
