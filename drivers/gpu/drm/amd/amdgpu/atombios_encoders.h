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

#ifndef __ATOMBIOS_ENCODER_H__
#define __ATOMBIOS_ENCODER_H__

u8
amdgpu_atombios_encoder_get_backlight_level_from_reg(struct amdgpu_device *adev);
void
amdgpu_atombios_encoder_set_backlight_level_to_reg(struct amdgpu_device *adev,
						   u8 backlight_level);
u8
amdgpu_atombios_encoder_get_backlight_level(struct amdgpu_encoder *amdgpu_encoder);
void
amdgpu_atombios_encoder_set_backlight_level(struct amdgpu_encoder *amdgpu_encoder,
				     u8 level);
void amdgpu_atombios_encoder_init_backlight(struct amdgpu_encoder *amdgpu_encoder,
				     struct drm_connector *drm_connector);
void
amdgpu_atombios_encoder_fini_backlight(struct amdgpu_encoder *amdgpu_encoder);
bool amdgpu_atombios_encoder_is_digital(struct drm_encoder *encoder);
bool amdgpu_atombios_encoder_mode_fixup(struct drm_encoder *encoder,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode);
int amdgpu_atombios_encoder_get_encoder_mode(struct drm_encoder *encoder);
void
amdgpu_atombios_encoder_setup_dig_encoder(struct drm_encoder *encoder,
				   int action, int panel_mode);
void
amdgpu_atombios_encoder_setup_dig_transmitter(struct drm_encoder *encoder, int action,
				       uint8_t lane_num, uint8_t lane_set);
bool
amdgpu_atombios_encoder_set_edp_panel_power(struct drm_connector *connector,
				     int action);
void
amdgpu_atombios_encoder_dpms(struct drm_encoder *encoder, int mode);
void
amdgpu_atombios_encoder_set_crtc_source(struct drm_encoder *encoder);
void
amdgpu_atombios_encoder_init_dig(struct amdgpu_device *adev);
enum drm_connector_status
amdgpu_atombios_encoder_dac_detect(struct drm_encoder *encoder,
			    struct drm_connector *connector);
enum drm_connector_status
amdgpu_atombios_encoder_dig_detect(struct drm_encoder *encoder,
			    struct drm_connector *connector);
void
amdgpu_atombios_encoder_setup_ext_encoder_ddc(struct drm_encoder *encoder);
void
amdgpu_atombios_encoder_set_bios_scratch_regs(struct drm_connector *connector,
				       struct drm_encoder *encoder,
				       bool connected);
struct amdgpu_encoder_atom_dig *
amdgpu_atombios_encoder_get_lcd_info(struct amdgpu_encoder *encoder);
struct amdgpu_encoder_atom_dig *
amdgpu_atombios_encoder_get_dig_info(struct amdgpu_encoder *amdgpu_encoder);

#endif
