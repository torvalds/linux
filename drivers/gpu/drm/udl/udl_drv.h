/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 */

#ifndef UDL_DRV_H
#define UDL_DRV_H

#include <linux/mm_types.h>
#include <linux/usb.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_plane.h>

struct drm_mode_create_dumb;

#define DRIVER_NAME		"udl"
#define DRIVER_DESC		"DisplayLink"

#define DRIVER_MAJOR		0
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	1

struct udl_device;

struct urb_node {
	struct list_head entry;
	struct udl_device *dev;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	wait_queue_head_t sleep;
	int available;
	int count;
	size_t size;
};

struct udl_device {
	struct drm_device drm;

	unsigned long sku_pixel_limit;

	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct urb_list urbs;
};

#define to_udl(x) container_of(x, struct udl_device, drm)

static inline struct usb_device *udl_to_usb_device(struct udl_device *udl)
{
	return interface_to_usbdev(to_usb_interface(udl->drm.dev));
}

/* modeset */
int udl_modeset_init(struct udl_device *udl);
struct drm_connector *udl_connector_init(struct drm_device *dev);

struct urb *udl_get_urb(struct udl_device *udl);

int udl_submit_urb(struct udl_device *udl, struct urb *urb, size_t len);
void udl_sync_pending_urbs(struct udl_device *udl);
void udl_urb_completion(struct urb *urb);

int udl_init(struct udl_device *udl);

int udl_render_hline(struct udl_device *udl, int log_bpp, struct urb **urb_ptr,
		     const char *front, char **urb_buf_ptr,
		     u32 byte_offset, u32 device_byte_offset, u32 byte_width);

int udl_drop_usb(struct udl_device *udl);
int udl_select_std_channel(struct udl_device *udl);

#endif
