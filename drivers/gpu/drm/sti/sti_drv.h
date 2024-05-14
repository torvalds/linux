/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 */

#ifndef _STI_DRV_H_
#define _STI_DRV_H_

#include <linux/platform_device.h>

struct drm_device;
struct drm_property;
struct sti_compositor;

/**
 * STI drm private structure
 * This structure is stored as private in the drm_device
 *
 * @compo:                 compositor
 * @plane_zorder_property: z-order property for CRTC planes
 * @drm_dev:               drm device
 */
struct sti_private {
	struct sti_compositor *compo;
	struct drm_property *plane_zorder_property;
	struct drm_device *drm_dev;
};

extern struct platform_driver sti_tvout_driver;
extern struct platform_driver sti_hqvdp_driver;
extern struct platform_driver sti_hdmi_driver;
extern struct platform_driver sti_hda_driver;
extern struct platform_driver sti_dvo_driver;
extern struct platform_driver sti_vtg_driver;
extern struct platform_driver sti_compositor_driver;

#endif
