/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_DRRS_H__
#define __INTEL_DRRS_H__

#include <linux/types.h>

enum drrs_type;
enum transcoder;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;

bool intel_cpu_transcoder_has_drrs(struct intel_display *display,
				   enum transcoder cpu_transcoder);
const char *intel_drrs_type_str(enum drrs_type drrs_type);
bool intel_drrs_is_active(struct intel_crtc *crtc);
void intel_drrs_activate(const struct intel_crtc_state *crtc_state);
void intel_drrs_deactivate(const struct intel_crtc_state *crtc_state);
void intel_drrs_invalidate(struct intel_display *display,
			   unsigned int frontbuffer_bits);
void intel_drrs_flush(struct intel_display *display,
		      unsigned int frontbuffer_bits);
void intel_drrs_crtc_init(struct intel_crtc *crtc);
void intel_drrs_crtc_debugfs_add(struct intel_crtc *crtc);
void intel_drrs_connector_debugfs_add(struct intel_connector *connector);

#endif /* __INTEL_DRRS_H__ */
