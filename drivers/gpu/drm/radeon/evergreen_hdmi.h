/* evergreen_hdmi.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Christian König.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __EVERGREEN_HDMI_H__
#define __EVERGREEN_HDMI_H__

struct cea_sa;
struct cea_sad;
struct drm_connector;
struct drm_display_mode;
struct drm_encoder;
struct r600_audio_pin;
struct radeon_crtc;
struct radeon_device;
struct radeon_hdmi_acr;

void evergreen_hdmi_write_sad_regs(struct drm_encoder *encoder,
				   struct cea_sad *sads, int sad_count);
void evergreen_set_avi_packet(struct radeon_device *rdev, u32 offset,
			      unsigned char *buffer, size_t size);
void evergreen_hdmi_update_acr(struct drm_encoder *encoder, long offset,
			       const struct radeon_hdmi_acr *acr);
void evergreen_hdmi_enable(struct drm_encoder *encoder, bool enable);
void evergreen_dp_enable(struct drm_encoder *encoder, bool enable);

void dce4_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		       u8 enable_mask);
void dce4_afmt_hdmi_write_speaker_allocation(struct drm_encoder *encoder,
					     u8 *sadb, int sad_count);
void dce4_afmt_dp_write_speaker_allocation(struct drm_encoder *encoder,
					   u8 *sadb, int sad_count);
void dce4_afmt_write_latency_fields(struct drm_encoder *encoder,
				    struct drm_connector *connector,
				    struct drm_display_mode *mode);
void dce4_hdmi_audio_set_dto(struct radeon_device *rdev,
			     struct radeon_crtc *crtc, unsigned int clock);
void dce4_dp_audio_set_dto(struct radeon_device *rdev,
			   struct radeon_crtc *crtc, unsigned int clock);
void dce4_set_vbi_packet(struct drm_encoder *encoder, u32 offset);
void dce4_hdmi_set_color_depth(struct drm_encoder *encoder,
			       u32 offset, int bpc);
void dce4_set_audio_packet(struct drm_encoder *encoder, u32 offset);
void dce4_set_mute(struct drm_encoder *encoder, u32 offset, bool mute);

#endif				/* __EVERGREEN_HDMI_H__ */
