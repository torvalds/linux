// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2016-2017 Oracle Corporation
 * This file is based on qxl_irq.c
 * Copyright 2013 Red Hat Inc.
 * Authors: Dave Airlie
 *          Alon Levy
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */

#include <drm/drm_crtc_helper.h>

#include "vbox_drv.h"
#include "vboxvideo.h"

static void vbox_clear_irq(void)
{
	outl((u32)~0, VGA_PORT_HGSMI_HOST);
}

static u32 vbox_get_flags(struct vbox_private *vbox)
{
	return readl(vbox->guest_heap + HOST_FLAGS_OFFSET);
}

void vbox_report_hotplug(struct vbox_private *vbox)
{
	schedule_work(&vbox->hotplug_work);
}

irqreturn_t vbox_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *)arg;
	struct vbox_private *vbox = (struct vbox_private *)dev->dev_private;
	u32 host_flags = vbox_get_flags(vbox);

	if (!(host_flags & HGSMIHOSTFLAGS_IRQ))
		return IRQ_NONE;

	/*
	 * Due to a bug in the initial host implementation of hot-plug irqs,
	 * the hot-plug and cursor capability flags were never cleared.
	 * Fortunately we can tell when they would have been set by checking
	 * that the VSYNC flag is not set.
	 */
	if (host_flags &
	    (HGSMIHOSTFLAGS_HOTPLUG | HGSMIHOSTFLAGS_CURSOR_CAPABILITIES) &&
	    !(host_flags & HGSMIHOSTFLAGS_VSYNC))
		vbox_report_hotplug(vbox);

	vbox_clear_irq();

	return IRQ_HANDLED;
}

/*
 * Check that the position hints provided by the host are suitable for GNOME
 * shell (i.e. all screens disjoint and hints for all enabled screens) and if
 * not replace them with default ones.  Providing valid hints improves the
 * chances that we will get a known screen layout for pointer mapping.
 */
static void validate_or_set_position_hints(struct vbox_private *vbox)
{
	struct vbva_modehint *hintsi, *hintsj;
	bool valid = true;
	u16 currentx = 0;
	int i, j;

	for (i = 0; i < vbox->num_crtcs; ++i) {
		for (j = 0; j < i; ++j) {
			hintsi = &vbox->last_mode_hints[i];
			hintsj = &vbox->last_mode_hints[j];

			if (hintsi->enabled && hintsj->enabled) {
				if (hintsi->dx >= 0xffff ||
				    hintsi->dy >= 0xffff ||
				    hintsj->dx >= 0xffff ||
				    hintsj->dy >= 0xffff ||
				    (hintsi->dx <
					hintsj->dx + (hintsj->cx & 0x8fff) &&
				     hintsi->dx + (hintsi->cx & 0x8fff) >
					hintsj->dx) ||
				    (hintsi->dy <
					hintsj->dy + (hintsj->cy & 0x8fff) &&
				     hintsi->dy + (hintsi->cy & 0x8fff) >
					hintsj->dy))
					valid = false;
			}
		}
	}
	if (!valid)
		for (i = 0; i < vbox->num_crtcs; ++i) {
			if (vbox->last_mode_hints[i].enabled) {
				vbox->last_mode_hints[i].dx = currentx;
				vbox->last_mode_hints[i].dy = 0;
				currentx +=
				    vbox->last_mode_hints[i].cx & 0x8fff;
			}
		}
}

/* Query the host for the most recent video mode hints. */
static void vbox_update_mode_hints(struct vbox_private *vbox)
{
	struct drm_device *dev = &vbox->ddev;
	struct drm_connector *connector;
	struct vbox_connector *vbox_conn;
	struct vbva_modehint *hints;
	u16 flags;
	bool disconnected;
	unsigned int crtc_id;
	int ret;

	ret = hgsmi_get_mode_hints(vbox->guest_pool, vbox->num_crtcs,
				   vbox->last_mode_hints);
	if (ret) {
		DRM_ERROR("vboxvideo: hgsmi_get_mode_hints failed: %d\n", ret);
		return;
	}

	validate_or_set_position_hints(vbox);
	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		vbox_conn = to_vbox_connector(connector);

		hints = &vbox->last_mode_hints[vbox_conn->vbox_crtc->crtc_id];
		if (hints->magic != VBVAMODEHINT_MAGIC)
			continue;

		disconnected = !(hints->enabled);
		crtc_id = vbox_conn->vbox_crtc->crtc_id;
		vbox_conn->mode_hint.width = hints->cx;
		vbox_conn->mode_hint.height = hints->cy;
		vbox_conn->vbox_crtc->x_hint = hints->dx;
		vbox_conn->vbox_crtc->y_hint = hints->dy;
		vbox_conn->mode_hint.disconnected = disconnected;

		if (vbox_conn->vbox_crtc->disconnected == disconnected)
			continue;

		if (disconnected)
			flags = VBVA_SCREEN_F_ACTIVE | VBVA_SCREEN_F_DISABLED;
		else
			flags = VBVA_SCREEN_F_ACTIVE | VBVA_SCREEN_F_BLANK;

		hgsmi_process_display_info(vbox->guest_pool, crtc_id, 0, 0, 0,
					   hints->cx * 4, hints->cx,
					   hints->cy, 0, flags);

		vbox_conn->vbox_crtc->disconnected = disconnected;
	}
	drm_modeset_unlock_all(dev);
}

static void vbox_hotplug_worker(struct work_struct *work)
{
	struct vbox_private *vbox = container_of(work, struct vbox_private,
						 hotplug_work);

	vbox_update_mode_hints(vbox);
	drm_kms_helper_hotplug_event(&vbox->ddev);
}

int vbox_irq_init(struct vbox_private *vbox)
{
	INIT_WORK(&vbox->hotplug_work, vbox_hotplug_worker);
	vbox_update_mode_hints(vbox);

	return drm_irq_install(&vbox->ddev, vbox->ddev.pdev->irq);
}

void vbox_irq_fini(struct vbox_private *vbox)
{
	drm_irq_uninstall(&vbox->ddev);
	flush_work(&vbox->hotplug_work);
}
