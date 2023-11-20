// SPDX-License-Identifier: MIT
/*
 * Copyright 2020 Noralf Trønnes
 */

#include <linux/lz4.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_rect.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/gud.h>

#include "gud_internal.h"

/*
 * Some userspace rendering loops run all displays in the same loop.
 * This means that a fast display will have to wait for a slow one.
 * Such users might want to enable this module parameter.
 */
static bool gud_async_flush;
module_param_named(async_flush, gud_async_flush, bool, 0644);
MODULE_PARM_DESC(async_flush, "Enable asynchronous flushing [default=0]");

/*
 * FIXME: The driver is probably broken on Big Endian machines.
 * See discussion:
 * https://lore.kernel.org/dri-devel/CAKb7UvihLX0hgBOP3VBG7O+atwZcUVCPVuBdfmDMpg0NjXe-cQ@mail.gmail.com/
 */

static bool gud_is_big_endian(void)
{
#if defined(__BIG_ENDIAN)
	return true;
#else
	return false;
#endif
}

static size_t gud_xrgb8888_to_r124(u8 *dst, const struct drm_format_info *format,
				   void *src, struct drm_framebuffer *fb,
				   struct drm_rect *rect)
{
	unsigned int block_width = drm_format_info_block_width(format, 0);
	unsigned int bits_per_pixel = 8 / block_width;
	unsigned int x, y, width, height;
	u8 pix, *pix8, *block = dst; /* Assign to silence compiler warning */
	struct iosys_map dst_map, vmap;
	size_t len;
	void *buf;

	WARN_ON_ONCE(format->char_per_block[0] != 1);

	/* Start on a byte boundary */
	rect->x1 = ALIGN_DOWN(rect->x1, block_width);
	width = drm_rect_width(rect);
	height = drm_rect_height(rect);
	len = drm_format_info_min_pitch(format, 0, width) * height;

	buf = kmalloc(width * height, GFP_KERNEL);
	if (!buf)
		return 0;

	iosys_map_set_vaddr(&dst_map, buf);
	iosys_map_set_vaddr(&vmap, src);
	drm_fb_xrgb8888_to_gray8(&dst_map, NULL, &vmap, fb, rect);
	pix8 = buf;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			unsigned int pixpos = x % block_width; /* within byte from the left */
			unsigned int pixshift = (block_width - pixpos - 1) * bits_per_pixel;

			if (!pixpos) {
				block = dst++;
				*block = 0;
			}

			pix = (*pix8++) >> (8 - bits_per_pixel);
			*block |= pix << pixshift;
		}
	}

	kfree(buf);

	return len;
}

static size_t gud_xrgb8888_to_color(u8 *dst, const struct drm_format_info *format,
				    void *src, struct drm_framebuffer *fb,
				    struct drm_rect *rect)
{
	unsigned int block_width = drm_format_info_block_width(format, 0);
	unsigned int bits_per_pixel = 8 / block_width;
	u8 r, g, b, pix, *block = dst; /* Assign to silence compiler warning */
	unsigned int x, y, width;
	__le32 *sbuf32;
	u32 pix32;
	size_t len;

	/* Start on a byte boundary */
	rect->x1 = ALIGN_DOWN(rect->x1, block_width);
	width = drm_rect_width(rect);
	len = drm_format_info_min_pitch(format, 0, width) * drm_rect_height(rect);

	for (y = rect->y1; y < rect->y2; y++) {
		sbuf32 = src + (y * fb->pitches[0]);
		sbuf32 += rect->x1;

		for (x = 0; x < width; x++) {
			unsigned int pixpos = x % block_width; /* within byte from the left */
			unsigned int pixshift = (block_width - pixpos - 1) * bits_per_pixel;

			if (!pixpos) {
				block = dst++;
				*block = 0;
			}

			pix32 = le32_to_cpu(*sbuf32++);
			r = pix32 >> 16;
			g = pix32 >> 8;
			b = pix32;

			switch (format->format) {
			case GUD_DRM_FORMAT_XRGB1111:
				pix = ((r >> 7) << 2) | ((g >> 7) << 1) | (b >> 7);
				break;
			default:
				WARN_ON_ONCE(1);
				return len;
			}

			*block |= pix << pixshift;
		}
	}

	return len;
}

static int gud_prep_flush(struct gud_device *gdrm, struct drm_framebuffer *fb,
			  const struct iosys_map *src, bool cached_reads,
			  const struct drm_format_info *format, struct drm_rect *rect,
			  struct gud_set_buffer_req *req)
{
	u8 compression = gdrm->compression;
	struct iosys_map dst;
	void *vaddr, *buf;
	size_t pitch, len;

	pitch = drm_format_info_min_pitch(format, 0, drm_rect_width(rect));
	len = pitch * drm_rect_height(rect);
	if (len > gdrm->bulk_len)
		return -E2BIG;

	vaddr = src[0].vaddr;
retry:
	if (compression)
		buf = gdrm->compress_buf;
	else
		buf = gdrm->bulk_buf;
	iosys_map_set_vaddr(&dst, buf);

	/*
	 * Imported buffers are assumed to be write-combined and thus uncached
	 * with slow reads (at least on ARM).
	 */
	if (format != fb->format) {
		if (format->format == GUD_DRM_FORMAT_R1) {
			len = gud_xrgb8888_to_r124(buf, format, vaddr, fb, rect);
			if (!len)
				return -ENOMEM;
		} else if (format->format == DRM_FORMAT_R8) {
			drm_fb_xrgb8888_to_gray8(&dst, NULL, src, fb, rect);
		} else if (format->format == DRM_FORMAT_RGB332) {
			drm_fb_xrgb8888_to_rgb332(&dst, NULL, src, fb, rect);
		} else if (format->format == DRM_FORMAT_RGB565) {
			drm_fb_xrgb8888_to_rgb565(&dst, NULL, src, fb, rect,
						  gud_is_big_endian());
		} else if (format->format == DRM_FORMAT_RGB888) {
			drm_fb_xrgb8888_to_rgb888(&dst, NULL, src, fb, rect);
		} else {
			len = gud_xrgb8888_to_color(buf, format, vaddr, fb, rect);
		}
	} else if (gud_is_big_endian() && format->cpp[0] > 1) {
		drm_fb_swab(&dst, NULL, src, fb, rect, cached_reads);
	} else if (compression && cached_reads && pitch == fb->pitches[0]) {
		/* can compress directly from the framebuffer */
		buf = vaddr + rect->y1 * pitch;
	} else {
		drm_fb_memcpy(&dst, NULL, src, fb, rect);
	}

	memset(req, 0, sizeof(*req));
	req->x = cpu_to_le32(rect->x1);
	req->y = cpu_to_le32(rect->y1);
	req->width = cpu_to_le32(drm_rect_width(rect));
	req->height = cpu_to_le32(drm_rect_height(rect));
	req->length = cpu_to_le32(len);

	if (compression & GUD_COMPRESSION_LZ4) {
		int complen;

		complen = LZ4_compress_default(buf, gdrm->bulk_buf, len, len, gdrm->lz4_comp_mem);
		if (complen <= 0) {
			compression = 0;
			goto retry;
		}

		req->compression = GUD_COMPRESSION_LZ4;
		req->compressed_length = cpu_to_le32(complen);
	}

	return 0;
}

struct gud_usb_bulk_context {
	struct timer_list timer;
	struct usb_sg_request sgr;
};

static void gud_usb_bulk_timeout(struct timer_list *t)
{
	struct gud_usb_bulk_context *ctx = from_timer(ctx, t, timer);

	usb_sg_cancel(&ctx->sgr);
}

static int gud_usb_bulk(struct gud_device *gdrm, size_t len)
{
	struct gud_usb_bulk_context ctx;
	int ret;

	ret = usb_sg_init(&ctx.sgr, gud_to_usb_device(gdrm), gdrm->bulk_pipe, 0,
			  gdrm->bulk_sgt.sgl, gdrm->bulk_sgt.nents, len, GFP_KERNEL);
	if (ret)
		return ret;

	timer_setup_on_stack(&ctx.timer, gud_usb_bulk_timeout, 0);
	mod_timer(&ctx.timer, jiffies + msecs_to_jiffies(3000));

	usb_sg_wait(&ctx.sgr);

	if (!del_timer_sync(&ctx.timer))
		ret = -ETIMEDOUT;
	else if (ctx.sgr.status < 0)
		ret = ctx.sgr.status;
	else if (ctx.sgr.bytes != len)
		ret = -EIO;

	destroy_timer_on_stack(&ctx.timer);

	return ret;
}

static int gud_flush_rect(struct gud_device *gdrm, struct drm_framebuffer *fb,
			  const struct iosys_map *src, bool cached_reads,
			  const struct drm_format_info *format, struct drm_rect *rect)
{
	struct gud_set_buffer_req req;
	size_t len, trlen;
	int ret;

	drm_dbg(&gdrm->drm, "Flushing [FB:%d] " DRM_RECT_FMT "\n", fb->base.id, DRM_RECT_ARG(rect));

	ret = gud_prep_flush(gdrm, fb, src, cached_reads, format, rect, &req);
	if (ret)
		return ret;

	len = le32_to_cpu(req.length);

	if (req.compression)
		trlen = le32_to_cpu(req.compressed_length);
	else
		trlen = len;

	gdrm->stats_length += len;
	/* Did it wrap around? */
	if (gdrm->stats_length <= len && gdrm->stats_actual_length) {
		gdrm->stats_length = len;
		gdrm->stats_actual_length = 0;
	}
	gdrm->stats_actual_length += trlen;

	if (!(gdrm->flags & GUD_DISPLAY_FLAG_FULL_UPDATE) || gdrm->prev_flush_failed) {
		ret = gud_usb_set(gdrm, GUD_REQ_SET_BUFFER, 0, &req, sizeof(req));
		if (ret)
			return ret;
	}

	ret = gud_usb_bulk(gdrm, trlen);
	if (ret)
		gdrm->stats_num_errors++;

	return ret;
}

void gud_clear_damage(struct gud_device *gdrm)
{
	gdrm->damage.x1 = INT_MAX;
	gdrm->damage.y1 = INT_MAX;
	gdrm->damage.x2 = 0;
	gdrm->damage.y2 = 0;
}

static void gud_flush_damage(struct gud_device *gdrm, struct drm_framebuffer *fb,
			     const struct iosys_map *src, bool cached_reads,
			     struct drm_rect *damage)
{
	const struct drm_format_info *format;
	unsigned int i, lines;
	size_t pitch;
	int ret;

	format = fb->format;
	if (format->format == DRM_FORMAT_XRGB8888 && gdrm->xrgb8888_emulation_format)
		format = gdrm->xrgb8888_emulation_format;

	/* Split update if it's too big */
	pitch = drm_format_info_min_pitch(format, 0, drm_rect_width(damage));
	lines = drm_rect_height(damage);

	if (gdrm->bulk_len < lines * pitch)
		lines = gdrm->bulk_len / pitch;

	for (i = 0; i < DIV_ROUND_UP(drm_rect_height(damage), lines); i++) {
		struct drm_rect rect = *damage;

		rect.y1 += i * lines;
		rect.y2 = min_t(u32, rect.y1 + lines, damage->y2);

		ret = gud_flush_rect(gdrm, fb, src, cached_reads, format, &rect);
		if (ret) {
			if (ret != -ENODEV && ret != -ECONNRESET &&
			    ret != -ESHUTDOWN && ret != -EPROTO)
				dev_err_ratelimited(fb->dev->dev,
						    "Failed to flush framebuffer: error=%d\n", ret);
			gdrm->prev_flush_failed = true;
			break;
		}
	}
}

void gud_flush_work(struct work_struct *work)
{
	struct gud_device *gdrm = container_of(work, struct gud_device, work);
	struct iosys_map shadow_map;
	struct drm_framebuffer *fb;
	struct drm_rect damage;
	int idx;

	if (!drm_dev_enter(&gdrm->drm, &idx))
		return;

	mutex_lock(&gdrm->damage_lock);
	fb = gdrm->fb;
	gdrm->fb = NULL;
	iosys_map_set_vaddr(&shadow_map, gdrm->shadow_buf);
	damage = gdrm->damage;
	gud_clear_damage(gdrm);
	mutex_unlock(&gdrm->damage_lock);

	if (!fb)
		goto out;

	gud_flush_damage(gdrm, fb, &shadow_map, true, &damage);

	drm_framebuffer_put(fb);
out:
	drm_dev_exit(idx);
}

static int gud_fb_queue_damage(struct gud_device *gdrm, struct drm_framebuffer *fb,
			       const struct iosys_map *src, struct drm_rect *damage)
{
	struct drm_framebuffer *old_fb = NULL;
	struct iosys_map shadow_map;

	mutex_lock(&gdrm->damage_lock);

	if (!gdrm->shadow_buf) {
		gdrm->shadow_buf = vcalloc(fb->pitches[0], fb->height);
		if (!gdrm->shadow_buf) {
			mutex_unlock(&gdrm->damage_lock);
			return -ENOMEM;
		}
	}

	iosys_map_set_vaddr(&shadow_map, gdrm->shadow_buf);
	iosys_map_incr(&shadow_map, drm_fb_clip_offset(fb->pitches[0], fb->format, damage));
	drm_fb_memcpy(&shadow_map, fb->pitches, src, fb, damage);

	if (fb != gdrm->fb) {
		old_fb = gdrm->fb;
		drm_framebuffer_get(fb);
		gdrm->fb = fb;
	}

	gdrm->damage.x1 = min(gdrm->damage.x1, damage->x1);
	gdrm->damage.y1 = min(gdrm->damage.y1, damage->y1);
	gdrm->damage.x2 = max(gdrm->damage.x2, damage->x2);
	gdrm->damage.y2 = max(gdrm->damage.y2, damage->y2);

	mutex_unlock(&gdrm->damage_lock);

	queue_work(system_long_wq, &gdrm->work);

	if (old_fb)
		drm_framebuffer_put(old_fb);

	return 0;
}

static void gud_fb_handle_damage(struct gud_device *gdrm, struct drm_framebuffer *fb,
				 const struct iosys_map *src, struct drm_rect *damage)
{
	int ret;

	if (gdrm->flags & GUD_DISPLAY_FLAG_FULL_UPDATE)
		drm_rect_init(damage, 0, 0, fb->width, fb->height);

	if (gud_async_flush) {
		ret = gud_fb_queue_damage(gdrm, fb, src, damage);
		if (ret != -ENOMEM)
			return;
	}

	/* Imported buffers are assumed to be WriteCombined with uncached reads */
	gud_flush_damage(gdrm, fb, src, !fb->obj[0]->import_attach, damage);
}

int gud_pipe_check(struct drm_simple_display_pipe *pipe,
		   struct drm_plane_state *new_plane_state,
		   struct drm_crtc_state *new_crtc_state)
{
	struct gud_device *gdrm = to_gud_device(pipe->crtc.dev);
	struct drm_plane_state *old_plane_state = pipe->plane.state;
	const struct drm_display_mode *mode = &new_crtc_state->mode;
	struct drm_atomic_state *state = new_plane_state->state;
	struct drm_framebuffer *old_fb = old_plane_state->fb;
	struct drm_connector_state *connector_state = NULL;
	struct drm_framebuffer *fb = new_plane_state->fb;
	const struct drm_format_info *format = fb->format;
	struct drm_connector *connector;
	unsigned int i, num_properties;
	struct gud_state_req *req;
	int idx, ret;
	size_t len;

	if (WARN_ON_ONCE(!fb))
		return -EINVAL;

	if (old_plane_state->rotation != new_plane_state->rotation)
		new_crtc_state->mode_changed = true;

	if (old_fb && old_fb->format != format)
		new_crtc_state->mode_changed = true;

	if (!new_crtc_state->mode_changed && !new_crtc_state->connectors_changed)
		return 0;

	/* Only one connector is supported */
	if (hweight32(new_crtc_state->connector_mask) != 1)
		return -EINVAL;

	if (format->format == DRM_FORMAT_XRGB8888 && gdrm->xrgb8888_emulation_format)
		format = gdrm->xrgb8888_emulation_format;

	for_each_new_connector_in_state(state, connector, connector_state, i) {
		if (connector_state->crtc)
			break;
	}

	/*
	 * DRM_IOCTL_MODE_OBJ_SETPROPERTY on the rotation property will not have
	 * the connector included in the state.
	 */
	if (!connector_state) {
		struct drm_connector_list_iter conn_iter;

		drm_connector_list_iter_begin(pipe->crtc.dev, &conn_iter);
		drm_for_each_connector_iter(connector, &conn_iter) {
			if (connector->state->crtc) {
				connector_state = connector->state;
				break;
			}
		}
		drm_connector_list_iter_end(&conn_iter);
	}

	if (WARN_ON_ONCE(!connector_state))
		return -ENOENT;

	len = struct_size(req, properties,
			  size_add(GUD_PROPERTIES_MAX_NUM, GUD_CONNECTOR_PROPERTIES_MAX_NUM));
	req = kzalloc(len, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	gud_from_display_mode(&req->mode, mode);

	req->format = gud_from_fourcc(format->format);
	if (WARN_ON_ONCE(!req->format)) {
		ret = -EINVAL;
		goto out;
	}

	req->connector = drm_connector_index(connector_state->connector);

	ret = gud_connector_fill_properties(connector_state, req->properties);
	if (ret < 0)
		goto out;

	num_properties = ret;
	for (i = 0; i < gdrm->num_properties; i++) {
		u16 prop = gdrm->properties[i];
		u64 val;

		switch (prop) {
		case GUD_PROPERTY_ROTATION:
			/* DRM UAPI matches the protocol so use value directly */
			val = new_plane_state->rotation;
			break;
		default:
			WARN_ON_ONCE(1);
			ret = -EINVAL;
			goto out;
		}

		req->properties[num_properties + i].prop = cpu_to_le16(prop);
		req->properties[num_properties + i].val = cpu_to_le64(val);
		num_properties++;
	}

	if (drm_dev_enter(fb->dev, &idx)) {
		len = struct_size(req, properties, num_properties);
		ret = gud_usb_set(gdrm, GUD_REQ_SET_STATE_CHECK, 0, req, len);
		drm_dev_exit(idx);
	}  else {
		ret = -ENODEV;
	}
out:
	kfree(req);

	return ret;
}

void gud_pipe_update(struct drm_simple_display_pipe *pipe,
		     struct drm_plane_state *old_state)
{
	struct drm_device *drm = pipe->crtc.dev;
	struct gud_device *gdrm = to_gud_device(drm);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(state);
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_rect damage;
	int ret, idx;

	if (crtc->state->mode_changed || !crtc->state->enable) {
		cancel_work_sync(&gdrm->work);
		mutex_lock(&gdrm->damage_lock);
		if (gdrm->fb) {
			drm_framebuffer_put(gdrm->fb);
			gdrm->fb = NULL;
		}
		gud_clear_damage(gdrm);
		vfree(gdrm->shadow_buf);
		gdrm->shadow_buf = NULL;
		mutex_unlock(&gdrm->damage_lock);
	}

	if (!drm_dev_enter(drm, &idx))
		return;

	if (!old_state->fb)
		gud_usb_set_u8(gdrm, GUD_REQ_SET_CONTROLLER_ENABLE, 1);

	if (fb && (crtc->state->mode_changed || crtc->state->connectors_changed))
		gud_usb_set(gdrm, GUD_REQ_SET_STATE_COMMIT, 0, NULL, 0);

	if (crtc->state->active_changed)
		gud_usb_set_u8(gdrm, GUD_REQ_SET_DISPLAY_ENABLE, crtc->state->active);

	if (!fb)
		goto ctrl_disable;

	ret = drm_gem_fb_begin_cpu_access(fb, DMA_FROM_DEVICE);
	if (ret)
		goto ctrl_disable;

	if (drm_atomic_helper_damage_merged(old_state, state, &damage))
		gud_fb_handle_damage(gdrm, fb, &shadow_plane_state->data[0], &damage);

	drm_gem_fb_end_cpu_access(fb, DMA_FROM_DEVICE);

ctrl_disable:
	if (!crtc->state->enable)
		gud_usb_set_u8(gdrm, GUD_REQ_SET_CONTROLLER_ENABLE, 0);

	drm_dev_exit(idx);
}
