/* radeon_atombios.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 */

#ifndef __RADEON_ATOMBIOS_H__
#define __RADEON_ATOMBIOS_H__

struct drm_connector;
struct drm_device;
struct drm_display_mode;
struct radeon_device;
struct radeon_encoder;

bool radeon_atom_get_tv_timings(struct radeon_device *rdev, int index,
				struct drm_display_mode *mode);
void radeon_add_atom_encoder(struct drm_device *dev, uint32_t encoder_enum,
			     uint32_t supported_device, u16 caps);
void radeon_atom_backlight_init(struct radeon_encoder *radeon_encoder,
				struct drm_connector *drm_connector);


#endif                         /* __RADEON_ATOMBIOS_H__ */
