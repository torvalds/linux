/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#ifndef __AMDGPU_PLL_H__
#define __AMDGPU_PLL_H__

void amdgpu_pll_compute(struct amdgpu_device *adev,
			 struct amdgpu_pll *pll,
			 u32 freq,
			 u32 *dot_clock_p,
			 u32 *fb_div_p,
			 u32 *frac_fb_div_p,
			 u32 *ref_div_p,
			 u32 *post_div_p);
u32 amdgpu_pll_get_use_mask(struct drm_crtc *crtc);
int amdgpu_pll_get_shared_dp_ppll(struct drm_crtc *crtc);
int amdgpu_pll_get_shared_nondp_ppll(struct drm_crtc *crtc);

#endif
