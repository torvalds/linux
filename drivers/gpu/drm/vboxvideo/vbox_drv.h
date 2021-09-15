/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_drv.h
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 *          Hans de Goede <hdegoede@redhat.com>
 */
#ifndef __VBOX_DRV_H__
#define __VBOX_DRV_H__

#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/string.h>

#include <drm/drm_encoder.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_vram_helper.h>

#include "vboxvideo_guest.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_ch_setup.h"

#define DRIVER_NAME         "vboxvideo"
#define DRIVER_DESC         "Oracle VM VirtualBox Graphics Card"
#define DRIVER_DATE         "20130823"

#define DRIVER_MAJOR        1
#define DRIVER_MINOR        0
#define DRIVER_PATCHLEVEL   0

#define VBOX_MAX_CURSOR_WIDTH  64
#define VBOX_MAX_CURSOR_HEIGHT 64
#define CURSOR_PIXEL_COUNT (VBOX_MAX_CURSOR_WIDTH * VBOX_MAX_CURSOR_HEIGHT)
#define CURSOR_DATA_SIZE (CURSOR_PIXEL_COUNT * 4 + CURSOR_PIXEL_COUNT / 8)

#define VBOX_MAX_SCREENS  32

#define GUEST_HEAP_OFFSET(vbox) ((vbox)->full_vram_size - \
				 VBVA_ADAPTER_INFORMATION_SIZE)
#define GUEST_HEAP_SIZE   VBVA_ADAPTER_INFORMATION_SIZE
#define GUEST_HEAP_USABLE_SIZE (VBVA_ADAPTER_INFORMATION_SIZE - \
				sizeof(struct hgsmi_host_flags))
#define HOST_FLAGS_OFFSET GUEST_HEAP_USABLE_SIZE

struct vbox_private {
	/* Must be first; or we must define our own release callback */
	struct drm_device ddev;

	u8 __iomem *guest_heap;
	u8 __iomem *vbva_buffers;
	struct gen_pool *guest_pool;
	struct vbva_buf_ctx *vbva_info;
	bool any_pitch;
	u32 num_crtcs;
	/* Amount of available VRAM, including space used for buffers. */
	u32 full_vram_size;
	/* Amount of available VRAM, not including space used for buffers. */
	u32 available_vram_size;
	/* Array of structures for receiving mode hints. */
	struct vbva_modehint *last_mode_hints;

	int fb_mtrr;

	struct mutex hw_mutex; /* protects modeset and accel/vbva accesses */
	struct work_struct hotplug_work;
	u32 input_mapping_width;
	u32 input_mapping_height;
	/*
	 * Is user-space using an X.Org-style layout of one large frame-buffer
	 * encompassing all screen ones or is the fbdev console active?
	 */
	bool single_framebuffer;
	u8 cursor_data[CURSOR_DATA_SIZE];
};

#undef CURSOR_PIXEL_COUNT
#undef CURSOR_DATA_SIZE

struct vbox_connector {
	struct drm_connector base;
	char name[32];
	struct vbox_crtc *vbox_crtc;
	struct {
		u32 width;
		u32 height;
		bool disconnected;
	} mode_hint;
};

struct vbox_crtc {
	struct drm_crtc base;
	bool disconnected;
	unsigned int crtc_id;
	u32 fb_offset;
	bool cursor_enabled;
	u32 x_hint;
	u32 y_hint;
	/*
	 * When setting a mode we not only pass the mode to the hypervisor,
	 * but also information on how to map / translate input coordinates
	 * for the emulated USB tablet.  This input-mapping may change when
	 * the mode on *another* crtc changes.
	 *
	 * This means that sometimes we must do a modeset on other crtc-s then
	 * the one being changed to update the input-mapping. Including crtc-s
	 * which may be disabled inside the guest (shown as a black window
	 * on the host unless closed by the user).
	 *
	 * With atomic modesetting the mode-info of disabled crtcs gets zeroed
	 * yet we need it when updating the input-map to avoid resizing the
	 * window as a side effect of a mode_set on another crtc. Therefor we
	 * cache the info of the last mode below.
	 */
	u32 width;
	u32 height;
	u32 x;
	u32 y;
};

struct vbox_encoder {
	struct drm_encoder base;
};

#define to_vbox_crtc(x) container_of(x, struct vbox_crtc, base)
#define to_vbox_connector(x) container_of(x, struct vbox_connector, base)
#define to_vbox_encoder(x) container_of(x, struct vbox_encoder, base)
#define to_vbox_dev(x) container_of(x, struct vbox_private, ddev)

bool vbox_check_supported(u16 id);
int vbox_hw_init(struct vbox_private *vbox);
void vbox_hw_fini(struct vbox_private *vbox);

int vbox_mode_init(struct vbox_private *vbox);
void vbox_mode_fini(struct vbox_private *vbox);

void vbox_report_caps(struct vbox_private *vbox);

int vbox_mm_init(struct vbox_private *vbox);
void vbox_mm_fini(struct vbox_private *vbox);

/* vbox_irq.c */
int vbox_irq_init(struct vbox_private *vbox);
void vbox_irq_fini(struct vbox_private *vbox);
void vbox_report_hotplug(struct vbox_private *vbox);

/* vbox_hgsmi.c */
void *hgsmi_buffer_alloc(struct gen_pool *guest_pool, size_t size,
			 u8 channel, u16 channel_info);
void hgsmi_buffer_free(struct gen_pool *guest_pool, void *buf);
int hgsmi_buffer_submit(struct gen_pool *guest_pool, void *buf);

static inline void vbox_write_ioport(u16 index, u16 data)
{
	outw(index, VBE_DISPI_IOPORT_INDEX);
	outw(data, VBE_DISPI_IOPORT_DATA);
}

#endif
