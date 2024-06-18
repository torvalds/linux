/*
 * Copyright © 2016 Intel Corporation
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
 */

/*
 * This header file contains mode setting related functions and definitions
 * which are only used within the drm kms helper module as internal
 * implementation details and are not exported to drivers.
 */

#ifndef __DRM_CRTC_HELPER_INTERNAL_H__
#define __DRM_CRTC_HELPER_INTERNAL_H__

enum drm_mode_status;
struct drm_connector;
struct drm_crtc;
struct drm_display_mode;
struct drm_encoder;
struct drm_modeset_acquire_ctx;

/* drm_probe_helper.c */
enum drm_mode_status drm_crtc_mode_valid(struct drm_crtc *crtc,
					 const struct drm_display_mode *mode);
enum drm_mode_status drm_encoder_mode_valid(struct drm_encoder *encoder,
					    const struct drm_display_mode *mode);
int
drm_connector_mode_valid(struct drm_connector *connector,
			 struct drm_display_mode *mode,
			 struct drm_modeset_acquire_ctx *ctx,
			 enum drm_mode_status *status);

struct drm_encoder *
drm_connector_get_single_encoder(struct drm_connector *connector);

#endif /* __DRM_CRTC_HELPER_INTERNAL_H__ */
