/* dce6_afmt.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
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

#ifndef __DCE6_AFMT_H__
#define __DCE6_AFMT_H__

struct cea_sad;
struct drm_connector;
struct drm_display_mode;
struct drm_encoder;
struct radeon_crtc;
struct radeon_device;

u32 dce6_endpoint_rreg(struct radeon_device *rdev, u32 offset, u32 reg);
void dce6_endpoint_wreg(struct radeon_device *rdev, u32 offset, u32 reg, u32 v);
void dce6_afmt_write_sad_regs(struct drm_encoder *encoder,
			      struct cea_sad *sads, int sad_count);
void dce6_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
					     u8 *sadb, int sad_count);
void dce6_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
					   u8 *sadb, int sad_count);
void dce6_afmt_write_latency_fields(struct drm_encoder *encoder,
				    struct drm_connector *connector,
				    struct drm_display_mode *mode);
void dce6_afmt_select_pin(struct drm_encoder *encoder);
void dce6_hdmi_audio_set_dto(struct radeon_device *rdev,
			     struct radeon_crtc *crtc, unsigned int clock);
void dce6_dp_audio_set_dto(struct radeon_device *rdev,
			   struct radeon_crtc *crtc, unsigned int clock);

#endif                         /* __DCE6_AFMT_H__ */
