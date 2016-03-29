/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _STI_VTG_H_
#define _STI_VTG_H_

#define VTG_TOP_FIELD_EVENT     1
#define VTG_BOTTOM_FIELD_EVENT  2

#define VTG_SYNC_ID_HDMI        1
#define VTG_SYNC_ID_HDDCS       2
#define VTG_SYNC_ID_HDF         3
#define VTG_SYNC_ID_DVO         4

struct sti_vtg;
struct drm_display_mode;
struct notifier_block;

struct sti_vtg *of_vtg_find(struct device_node *np);
void sti_vtg_set_config(struct sti_vtg *vtg,
		const struct drm_display_mode *mode);
int sti_vtg_register_client(struct sti_vtg *vtg, struct notifier_block *nb,
			    struct drm_crtc *crtc);
int sti_vtg_unregister_client(struct sti_vtg *vtg,
		struct notifier_block *nb);

u32 sti_vtg_get_line_number(struct drm_display_mode mode, int y);
u32 sti_vtg_get_pixel_number(struct drm_display_mode mode, int x);

#endif
