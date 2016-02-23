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

#ifndef __ATOMBIOS_DP_H__
#define __ATOMBIOS_DP_H__

void amdgpu_atombios_dp_aux_init(struct amdgpu_connector *amdgpu_connector);
u8 amdgpu_atombios_dp_get_sinktype(struct amdgpu_connector *amdgpu_connector);
int amdgpu_atombios_dp_get_dpcd(struct amdgpu_connector *amdgpu_connector);
int amdgpu_atombios_dp_get_panel_mode(struct drm_encoder *encoder,
			       struct drm_connector *connector);
void amdgpu_atombios_dp_set_link_config(struct drm_connector *connector,
				 const struct drm_display_mode *mode);
int amdgpu_atombios_dp_mode_valid_helper(struct drm_connector *connector,
				  struct drm_display_mode *mode);
bool amdgpu_atombios_dp_needs_link_train(struct amdgpu_connector *amdgpu_connector);
void amdgpu_atombios_dp_set_rx_power_state(struct drm_connector *connector,
				    u8 power_state);
void amdgpu_atombios_dp_link_train(struct drm_encoder *encoder,
			    struct drm_connector *connector);

#endif
