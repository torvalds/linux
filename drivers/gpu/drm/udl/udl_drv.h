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

#include <drm/drm_device.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_simple_kms_helper.h>

struct drm_mode_create_dumb;

#define DRIVER_NAME		"udl"
#define DRIVER_DESC		"DisplayLink"
#define DRIVER_DATE		"20120220"

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
	struct device *dev;
	struct device *dmadev;

	struct drm_simple_display_pipe display_pipe;

	struct mutex gem_lock;

	int sku_pixel_limit;

	struct urb_list urbs;

	char mode_buf[1024];
	uint32_t mode_buf_len;
};

#define to_udl(x) container_of(x, struct udl_device, drm)

static inline struct usb_device *udl_to_usb_device(struct udl_device *udl)
{
	return interface_to_usbdev(to_usb_interface(udl->drm.dev));
}

/* modeset */
int udl_modeset_init(struct drm_device *dev);
struct drm_connector *udl_connector_init(struct drm_device *dev);

struct urb *udl_get_urb(struct drm_device *dev);

int udl_submit_urb(struct drm_device *dev, struct urb *urb, size_t len);
void udl_sync_pending_urbs(struct drm_device *dev);
void udl_urb_completion(struct urb *urb);

int udl_init(struct udl_device *udl);

int udl_render_hline(struct drm_device *dev, int log_bpp, struct urb **urb_ptr,
		     const char *front, char **urb_buf_ptr,
		     u32 byte_offset, u32 device_byte_offset, u32 byte_width);

int udl_drop_usb(struct drm_device *dev);
int udl_select_std_channel(struct udl_device *udl);

#define CMD_WRITE_RAW8   "\xAF\x60" /**< 8 bit raw write command. */
#define CMD_WRITE_RL8    "\xAF\x61" /**< 8 bit run length command. */
#define CMD_WRITE_COPY8  "\xAF\x62" /**< 8 bit copy command. */
#define CMD_WRITE_RLX8   "\xAF\x63" /**< 8 bit extended run length command. */

#define CMD_WRITE_RAW16  "\xAF\x68" /**< 16 bit raw write command. */
#define CMD_WRITE_RL16   "\xAF\x69" /**< 16 bit run length command. */
#define CMD_WRITE_COPY16 "\xAF\x6A" /**< 16 bit copy command. */
#define CMD_WRITE_RLX16  "\xAF\x6B" /**< 16 bit extended run length command. */

/* On/Off for driving the DisplayLink framebuffer to the display */
#define UDL_REG_BLANK_MODE		0x1f

#define UDL_BLANK_MODE_ON		0x00 /* hsync and vsync on, visible */
#define UDL_BLANK_MODE_BLANKED		0x01 /* hsync and vsync on, blanked */
#define UDL_BLANK_MODE_VSYNC_OFF	0x03 /* vsync off, blanked */
#define UDL_BLANK_MODE_HSYNC_OFF	0x05 /* hsync off, blanked */
#define UDL_BLANK_MODE_POWERDOWN	0x07 /* powered off; requires modeset */

#endif
