/* r600.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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

#ifndef __R600_H__
#define __R600_H__

struct radeon_bo_list;
struct radeon_cs_parser;
struct r600_audio_pin;
struct radeon_crtc;
struct radeon_device;
struct radeon_hdmi_acr;

u32 r600_gpu_check_soft_reset(struct radeon_device *rdev);
int r600_ih_ring_alloc(struct radeon_device *rdev);
void r600_ih_ring_fini(struct radeon_device *rdev);

void r600_audio_enable(struct radeon_device *rdev, struct r600_audio_pin *pin,
		       u8 enable_mask);
void r600_set_audio_packet(struct drm_encoder *encoder, u32 offset);
void r600_set_mute(struct drm_encoder *encoder, u32 offset, bool mute);
void r600_hdmi_audio_set_dto(struct radeon_device *rdev,
			     struct radeon_crtc *crtc, unsigned int clock);
void r600_set_avi_packet(struct radeon_device *rdev, u32 offset,
			 unsigned char *buffer, size_t size);
void r600_hdmi_update_acr(struct drm_encoder *encoder, long offset,
			  const struct radeon_hdmi_acr *acr);
void r600_set_vbi_packet(struct drm_encoder *encoder, u32 offset);
void r600_hdmi_enable(struct drm_encoder *encoder, bool enable);

int r600_dma_cs_next_reloc(struct radeon_cs_parser *p,
			   struct radeon_bo_list **cs_reloc);

#endif				/* __R600_H__ */
