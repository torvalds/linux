/* SPDX-License-Identifier: MIT */

#ifndef __LINUX_GUD_INTERNAL_H
#define __LINUX_GUD_INTERNAL_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <uapi/drm/drm_fourcc.h>

#include <drm/drm_modes.h>
#include <drm/drm_simple_kms_helper.h>

struct gud_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct device *dmadev;
	struct work_struct work;
	u32 flags;
	const struct drm_format_info *xrgb8888_emulation_format;

	u16 *properties;
	unsigned int num_properties;

	unsigned int bulk_pipe;
	void *bulk_buf;
	size_t bulk_len;
	struct sg_table bulk_sgt;

	u8 compression;
	void *lz4_comp_mem;
	void *compress_buf;

	u64 stats_length;
	u64 stats_actual_length;
	unsigned int stats_num_errors;

	struct mutex ctrl_lock; /* Serialize get/set and status transfers */

	struct mutex damage_lock; /* Protects the following members: */
	struct drm_framebuffer *fb;
	struct drm_rect damage;
	bool prev_flush_failed;
	void *shadow_buf;
};

static inline struct gud_device *to_gud_device(struct drm_device *drm)
{
	return container_of(drm, struct gud_device, drm);
}

static inline struct usb_device *gud_to_usb_device(struct gud_device *gdrm)
{
	return interface_to_usbdev(to_usb_interface(gdrm->drm.dev));
}

int gud_usb_get(struct gud_device *gdrm, u8 request, u16 index, void *buf, size_t len);
int gud_usb_set(struct gud_device *gdrm, u8 request, u16 index, void *buf, size_t len);
int gud_usb_get_u8(struct gud_device *gdrm, u8 request, u16 index, u8 *val);
int gud_usb_set_u8(struct gud_device *gdrm, u8 request, u8 val);

void gud_clear_damage(struct gud_device *gdrm);
void gud_flush_work(struct work_struct *work);
int gud_pipe_check(struct drm_simple_display_pipe *pipe,
		   struct drm_plane_state *new_plane_state,
		   struct drm_crtc_state *new_crtc_state);
void gud_pipe_update(struct drm_simple_display_pipe *pipe,
		     struct drm_plane_state *old_state);
int gud_connector_fill_properties(struct drm_connector_state *connector_state,
				  struct gud_property_req *properties);
int gud_get_connectors(struct gud_device *gdrm);

/* Driver internal fourcc transfer formats */
#define GUD_DRM_FORMAT_R1		0x00000122
#define GUD_DRM_FORMAT_XRGB1111		0x03121722

static inline u8 gud_from_fourcc(u32 fourcc)
{
	switch (fourcc) {
	case GUD_DRM_FORMAT_R1:
		return GUD_PIXEL_FORMAT_R1;
	case DRM_FORMAT_R8:
		return GUD_PIXEL_FORMAT_R8;
	case GUD_DRM_FORMAT_XRGB1111:
		return GUD_PIXEL_FORMAT_XRGB1111;
	case DRM_FORMAT_RGB332:
		return GUD_PIXEL_FORMAT_RGB332;
	case DRM_FORMAT_RGB565:
		return GUD_PIXEL_FORMAT_RGB565;
	case DRM_FORMAT_RGB888:
		return GUD_PIXEL_FORMAT_RGB888;
	case DRM_FORMAT_XRGB8888:
		return GUD_PIXEL_FORMAT_XRGB8888;
	case DRM_FORMAT_ARGB8888:
		return GUD_PIXEL_FORMAT_ARGB8888;
	}

	return 0;
}

static inline u32 gud_to_fourcc(u8 format)
{
	switch (format) {
	case GUD_PIXEL_FORMAT_R1:
		return GUD_DRM_FORMAT_R1;
	case GUD_PIXEL_FORMAT_R8:
		return DRM_FORMAT_R8;
	case GUD_PIXEL_FORMAT_XRGB1111:
		return GUD_DRM_FORMAT_XRGB1111;
	case GUD_PIXEL_FORMAT_RGB332:
		return DRM_FORMAT_RGB332;
	case GUD_PIXEL_FORMAT_RGB565:
		return DRM_FORMAT_RGB565;
	case GUD_PIXEL_FORMAT_RGB888:
		return DRM_FORMAT_RGB888;
	case GUD_PIXEL_FORMAT_XRGB8888:
		return DRM_FORMAT_XRGB8888;
	case GUD_PIXEL_FORMAT_ARGB8888:
		return DRM_FORMAT_ARGB8888;
	}

	return 0;
}

static inline void gud_from_display_mode(struct gud_display_mode_req *dst,
					 const struct drm_display_mode *src)
{
	u32 flags = src->flags & GUD_DISPLAY_MODE_FLAG_USER_MASK;

	if (src->type & DRM_MODE_TYPE_PREFERRED)
		flags |= GUD_DISPLAY_MODE_FLAG_PREFERRED;

	dst->clock = cpu_to_le32(src->clock);
	dst->hdisplay = cpu_to_le16(src->hdisplay);
	dst->hsync_start = cpu_to_le16(src->hsync_start);
	dst->hsync_end = cpu_to_le16(src->hsync_end);
	dst->htotal = cpu_to_le16(src->htotal);
	dst->vdisplay = cpu_to_le16(src->vdisplay);
	dst->vsync_start = cpu_to_le16(src->vsync_start);
	dst->vsync_end = cpu_to_le16(src->vsync_end);
	dst->vtotal = cpu_to_le16(src->vtotal);
	dst->flags = cpu_to_le32(flags);
}

static inline void gud_to_display_mode(struct drm_display_mode *dst,
				       const struct gud_display_mode_req *src)
{
	u32 flags = le32_to_cpu(src->flags);

	memset(dst, 0, sizeof(*dst));
	dst->clock = le32_to_cpu(src->clock);
	dst->hdisplay = le16_to_cpu(src->hdisplay);
	dst->hsync_start = le16_to_cpu(src->hsync_start);
	dst->hsync_end = le16_to_cpu(src->hsync_end);
	dst->htotal = le16_to_cpu(src->htotal);
	dst->vdisplay = le16_to_cpu(src->vdisplay);
	dst->vsync_start = le16_to_cpu(src->vsync_start);
	dst->vsync_end = le16_to_cpu(src->vsync_end);
	dst->vtotal = le16_to_cpu(src->vtotal);
	dst->flags = flags & GUD_DISPLAY_MODE_FLAG_USER_MASK;
	dst->type = DRM_MODE_TYPE_DRIVER;
	if (flags & GUD_DISPLAY_MODE_FLAG_PREFERRED)
		dst->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(dst);
}

#endif
