/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <acpi/video.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "nouveau_fbcon.h"
#include "nouveau_crtc.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nv50_display.h"

#include <nvif/class.h>
#include <nvif/cl0046.h>
#include <nvif/event.h>
#include <dispnv50/crc.h>

int
nouveau_display_vblank_enable(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc;

	nv_crtc = nouveau_crtc(crtc);
	nvif_notify_get(&nv_crtc->vblank);

	return 0;
}

void
nouveau_display_vblank_disable(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc;

	nv_crtc = nouveau_crtc(crtc);
	nvif_notify_put(&nv_crtc->vblank);
}

static inline int
calc(int blanks, int blanke, int total, int line)
{
	if (blanke >= blanks) {
		if (line >= blanks)
			line -= total;
	} else {
		if (line >= blanks)
			line -= total;
		line -= blanke + 1;
	}
	return line;
}

static bool
nouveau_display_scanoutpos_head(struct drm_crtc *crtc, int *vpos, int *hpos,
				ktime_t *stime, ktime_t *etime)
{
	struct {
		struct nv04_disp_mthd_v0 base;
		struct nv04_disp_scanoutpos_v0 scan;
	} args = {
		.base.method = NV04_DISP_SCANOUTPOS,
		.base.head = nouveau_crtc(crtc)->index,
	};
	struct nouveau_display *disp = nouveau_display(crtc->dev);
	struct drm_vblank_crtc *vblank = &crtc->dev->vblank[drm_crtc_index(crtc)];
	int retry = 20;
	bool ret = false;

	do {
		ret = nvif_mthd(&disp->disp.object, 0, &args, sizeof(args));
		if (ret != 0)
			return false;

		if (args.scan.vline) {
			ret = true;
			break;
		}

		if (retry) ndelay(vblank->linedur_ns);
	} while (retry--);

	*hpos = args.scan.hline;
	*vpos = calc(args.scan.vblanks, args.scan.vblanke,
		     args.scan.vtotal, args.scan.vline);
	if (stime) *stime = ns_to_ktime(args.scan.time[0]);
	if (etime) *etime = ns_to_ktime(args.scan.time[1]);

	return ret;
}

bool
nouveau_display_scanoutpos(struct drm_crtc *crtc,
			   bool in_vblank_irq, int *vpos, int *hpos,
			   ktime_t *stime, ktime_t *etime,
			   const struct drm_display_mode *mode)
{
	return nouveau_display_scanoutpos_head(crtc, vpos, hpos,
					       stime, etime);
}

static const struct drm_framebuffer_funcs nouveau_framebuffer_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

static void
nouveau_decode_mod(struct nouveau_drm *drm,
		   uint64_t modifier,
		   uint32_t *tile_mode,
		   uint8_t *kind)
{
	struct nouveau_display *disp = nouveau_display(drm->dev);
	BUG_ON(!tile_mode || !kind);

	if (modifier == DRM_FORMAT_MOD_LINEAR) {
		/* tile_mode will not be used in this case */
		*tile_mode = 0;
		*kind = 0;
	} else {
		/*
		 * Extract the block height and kind from the corresponding
		 * modifier fields.  See drm_fourcc.h for details.
		 */

		if ((modifier & (0xffull << 12)) == 0ull) {
			/* Legacy modifier.  Translate to this dev's 'kind.' */
			modifier |= disp->format_modifiers[0] & (0xffull << 12);
		}

		*tile_mode = (uint32_t)(modifier & 0xF);
		*kind = (uint8_t)((modifier >> 12) & 0xFF);

		if (drm->client.device.info.chipset >= 0xc0)
			*tile_mode <<= 4;
	}
}

void
nouveau_framebuffer_get_layout(struct drm_framebuffer *fb,
			       uint32_t *tile_mode,
			       uint8_t *kind)
{
	if (fb->flags & DRM_MODE_FB_MODIFIERS) {
		struct nouveau_drm *drm = nouveau_drm(fb->dev);

		nouveau_decode_mod(drm, fb->modifier, tile_mode, kind);
	} else {
		const struct nouveau_bo *nvbo = nouveau_gem_object(fb->obj[0]);

		*tile_mode = nvbo->mode;
		*kind = nvbo->kind;
	}
}

static const u64 legacy_modifiers[] = {
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4),
	DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5),
	DRM_FORMAT_MOD_INVALID
};

static int
nouveau_validate_decode_mod(struct nouveau_drm *drm,
			    uint64_t modifier,
			    uint32_t *tile_mode,
			    uint8_t *kind)
{
	struct nouveau_display *disp = nouveau_display(drm->dev);
	int mod;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		return -EINVAL;
	}

	BUG_ON(!disp->format_modifiers);

	for (mod = 0;
	     (disp->format_modifiers[mod] != DRM_FORMAT_MOD_INVALID) &&
	     (disp->format_modifiers[mod] != modifier);
	     mod++);

	if (disp->format_modifiers[mod] == DRM_FORMAT_MOD_INVALID) {
		for (mod = 0;
		     (legacy_modifiers[mod] != DRM_FORMAT_MOD_INVALID) &&
		     (legacy_modifiers[mod] != modifier);
		     mod++);
		if (legacy_modifiers[mod] == DRM_FORMAT_MOD_INVALID)
			return -EINVAL;
	}

	nouveau_decode_mod(drm, modifier, tile_mode, kind);

	return 0;
}

static inline uint32_t
nouveau_get_width_in_blocks(uint32_t stride)
{
	/* GOBs per block in the x direction is always one, and GOBs are
	 * 64 bytes wide
	 */
	static const uint32_t log_block_width = 6;

	return (stride + (1 << log_block_width) - 1) >> log_block_width;
}

static inline uint32_t
nouveau_get_height_in_blocks(struct nouveau_drm *drm,
			     uint32_t height,
			     uint32_t log_block_height_in_gobs)
{
	uint32_t log_gob_height;
	uint32_t log_block_height;

	BUG_ON(drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA);

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_FERMI)
		log_gob_height = 2;
	else
		log_gob_height = 3;

	log_block_height = log_block_height_in_gobs + log_gob_height;

	return (height + (1 << log_block_height) - 1) >> log_block_height;
}

static int
nouveau_check_bl_size(struct nouveau_drm *drm, struct nouveau_bo *nvbo,
		      uint32_t offset, uint32_t stride, uint32_t h,
		      uint32_t tile_mode)
{
	uint32_t gob_size, bw, bh;
	uint64_t bl_size;

	BUG_ON(drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA);

	if (drm->client.device.info.chipset >= 0xc0) {
		if (tile_mode & 0xF)
			return -EINVAL;
		tile_mode >>= 4;
	}

	if (tile_mode & 0xFFFFFFF0)
		return -EINVAL;

	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_FERMI)
		gob_size = 256;
	else
		gob_size = 512;

	bw = nouveau_get_width_in_blocks(stride);
	bh = nouveau_get_height_in_blocks(drm, h, tile_mode);

	bl_size = bw * bh * (1 << tile_mode) * gob_size;

	DRM_DEBUG_KMS("offset=%u stride=%u h=%u tile_mode=0x%02x bw=%u bh=%u gob_size=%u bl_size=%llu size=%lu\n",
		      offset, stride, h, tile_mode, bw, bh, gob_size, bl_size,
		      nvbo->bo.mem.size);

	if (bl_size + offset > nvbo->bo.mem.size)
		return -ERANGE;

	return 0;
}

int
nouveau_framebuffer_new(struct drm_device *dev,
			const struct drm_mode_fb_cmd2 *mode_cmd,
			struct drm_gem_object *gem,
			struct drm_framebuffer **pfb)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_bo *nvbo = nouveau_gem_object(gem);
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	unsigned int width, height, i;
	uint32_t tile_mode;
	uint8_t kind;
	int ret;

        /* YUV overlays have special requirements pre-NV50 */
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA &&

	    (mode_cmd->pixel_format == DRM_FORMAT_YUYV ||
	     mode_cmd->pixel_format == DRM_FORMAT_UYVY ||
	     mode_cmd->pixel_format == DRM_FORMAT_NV12 ||
	     mode_cmd->pixel_format == DRM_FORMAT_NV21) &&
	    (mode_cmd->pitches[0] & 0x3f || /* align 64 */
	     mode_cmd->pitches[0] >= 0x10000 || /* at most 64k pitch */
	     (mode_cmd->pitches[1] && /* pitches for planes must match */
	      mode_cmd->pitches[0] != mode_cmd->pitches[1]))) {
		struct drm_format_name_buf format_name;
		DRM_DEBUG_KMS("Unsuitable framebuffer: format: %s; pitches: 0x%x\n 0x%x\n",
			      drm_get_format_name(mode_cmd->pixel_format,
						  &format_name),
			      mode_cmd->pitches[0],
			      mode_cmd->pitches[1]);
		return -EINVAL;
	}

	if (mode_cmd->flags & DRM_MODE_FB_MODIFIERS) {
		if (nouveau_validate_decode_mod(drm, mode_cmd->modifier[0],
						&tile_mode, &kind)) {
			DRM_DEBUG_KMS("Unsupported modifier: 0x%llx\n",
				      mode_cmd->modifier[0]);
			return -EINVAL;
		}
	} else {
		tile_mode = nvbo->mode;
		kind = nvbo->kind;
	}

	info = drm_get_format_info(dev, mode_cmd);

	for (i = 0; i < info->num_planes; i++) {
		width = drm_format_info_plane_width(info,
						    mode_cmd->width,
						    i);
		height = drm_format_info_plane_height(info,
						      mode_cmd->height,
						      i);

		if (kind) {
			ret = nouveau_check_bl_size(drm, nvbo,
						    mode_cmd->offsets[i],
						    mode_cmd->pitches[i],
						    height, tile_mode);
			if (ret)
				return ret;
		} else {
			uint32_t size = mode_cmd->pitches[i] * height;

			if (size + mode_cmd->offsets[i] > nvbo->bo.mem.size)
				return -ERANGE;
		}
	}

	if (!(fb = *pfb = kzalloc(sizeof(*fb), GFP_KERNEL)))
		return -ENOMEM;

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);
	fb->obj[0] = gem;

	ret = drm_framebuffer_init(dev, fb, &nouveau_framebuffer_funcs);
	if (ret)
		kfree(fb);
	return ret;
}

struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_framebuffer *fb;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(file_priv, mode_cmd->handles[0]);
	if (!gem)
		return ERR_PTR(-ENOENT);

	ret = nouveau_framebuffer_new(dev, mode_cmd, gem, &fb);
	if (ret == 0)
		return fb;

	drm_gem_object_put(gem);
	return ERR_PTR(ret);
}

static const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = nouveau_fbcon_output_poll_changed,
};


struct nouveau_drm_prop_enum_list {
	u8 gen_mask;
	int type;
	char *name;
};

static struct nouveau_drm_prop_enum_list underscan[] = {
	{ 6, UNDERSCAN_AUTO, "auto" },
	{ 6, UNDERSCAN_OFF, "off" },
	{ 6, UNDERSCAN_ON, "on" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_mode[] = {
	{ 7, DITHERING_MODE_AUTO, "auto" },
	{ 7, DITHERING_MODE_OFF, "off" },
	{ 1, DITHERING_MODE_ON, "on" },
	{ 6, DITHERING_MODE_STATIC2X2, "static 2x2" },
	{ 6, DITHERING_MODE_DYNAMIC2X2, "dynamic 2x2" },
	{ 4, DITHERING_MODE_TEMPORAL, "temporal" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_depth[] = {
	{ 6, DITHERING_DEPTH_AUTO, "auto" },
	{ 6, DITHERING_DEPTH_6BPC, "6 bpc" },
	{ 6, DITHERING_DEPTH_8BPC, "8 bpc" },
	{}
};

#define PROP_ENUM(p,gen,n,list) do {                                           \
	struct nouveau_drm_prop_enum_list *l = (list);                         \
	int c = 0;                                                             \
	while (l->gen_mask) {                                                  \
		if (l->gen_mask & (1 << (gen)))                                \
			c++;                                                   \
		l++;                                                           \
	}                                                                      \
	if (c) {                                                               \
		p = drm_property_create(dev, DRM_MODE_PROP_ENUM, n, c);        \
		l = (list);                                                    \
		while (p && l->gen_mask) {                                     \
			if (l->gen_mask & (1 << (gen))) {                      \
				drm_property_add_enum(p, l->type, l->name);    \
			}                                                      \
			l++;                                                   \
		}                                                              \
	}                                                                      \
} while(0)

void
nouveau_display_hpd_resume(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);

	mutex_lock(&drm->hpd_lock);
	drm->hpd_pending = ~0;
	mutex_unlock(&drm->hpd_lock);

	schedule_work(&drm->hpd_work);
}

static void
nouveau_display_hpd_work(struct work_struct *work)
{
	struct nouveau_drm *drm = container_of(work, typeof(*drm), hpd_work);
	struct drm_device *dev = drm->dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	u32 pending;
	bool changed = false;

	pm_runtime_get_sync(dev->dev);

	mutex_lock(&drm->hpd_lock);
	pending = drm->hpd_pending;
	drm->hpd_pending = 0;
	mutex_unlock(&drm->hpd_lock);

	/* Nothing to do, exit early without updating the last busy counter */
	if (!pending)
		goto noop;

	mutex_lock(&dev->mode_config.mutex);
	drm_connector_list_iter_begin(dev, &conn_iter);

	nouveau_for_each_non_mst_connector_iter(connector, &conn_iter) {
		enum drm_connector_status old_status = connector->status;
		u64 old_epoch_counter = connector->epoch_counter;

		if (!(pending & drm_connector_mask(connector)))
			continue;

		connector->status = drm_helper_probe_detect(connector, NULL,
							    false);
		if (old_epoch_counter == connector->epoch_counter)
			continue;

		changed = true;
		drm_dbg_kms(dev, "[CONNECTOR:%d:%s] status updated from %s to %s (epoch counter %llu->%llu)\n",
			    connector->base.id, connector->name,
			    drm_get_connector_status_name(old_status),
			    drm_get_connector_status_name(connector->status),
			    old_epoch_counter, connector->epoch_counter);
	}

	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	if (changed)
		drm_kms_helper_hotplug_event(dev);

	pm_runtime_mark_last_busy(drm->dev->dev);
noop:
	pm_runtime_put_autosuspend(dev->dev);
}

#ifdef CONFIG_ACPI

static int
nouveau_display_acpi_ntfy(struct notifier_block *nb, unsigned long val,
			  void *data)
{
	struct nouveau_drm *drm = container_of(nb, typeof(*drm), acpi_nb);
	struct acpi_bus_event *info = data;
	int ret;

	if (!strcmp(info->device_class, ACPI_VIDEO_CLASS)) {
		if (info->type == ACPI_VIDEO_NOTIFY_PROBE) {
			ret = pm_runtime_get(drm->dev->dev);
			if (ret == 1 || ret == -EACCES) {
				/* If the GPU is already awake, or in a state
				 * where we can't wake it up, it can handle
				 * it's own hotplug events.
				 */
				pm_runtime_put_autosuspend(drm->dev->dev);
			} else if (ret == 0 || ret == -EINPROGRESS) {
				/* We've started resuming the GPU already, so
				 * it will handle scheduling a full reprobe
				 * itself
				 */
				NV_DEBUG(drm, "ACPI requested connector reprobe\n");
				pm_runtime_put_noidle(drm->dev->dev);
			} else {
				NV_WARN(drm, "Dropped ACPI reprobe event due to RPM error: %d\n",
					ret);
			}

			/* acpi-video should not generate keypresses for this */
			return NOTIFY_BAD;
		}
	}

	return NOTIFY_DONE;
}
#endif

int
nouveau_display_init(struct drm_device *dev, bool resume, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	int ret;

	/*
	 * Enable hotplug interrupts (done as early as possible, since we need
	 * them for MST)
	 */
	drm_connector_list_iter_begin(dev, &conn_iter);
	nouveau_for_each_non_mst_connector_iter(connector, &conn_iter) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nvif_notify_get(&conn->hpd);
	}
	drm_connector_list_iter_end(&conn_iter);

	ret = disp->init(dev, resume, runtime);
	if (ret)
		return ret;

	/* enable connector detection and polling for connectors without HPD
	 * support
	 */
	drm_kms_helper_poll_enable(dev);

	return ret;
}

void
nouveau_display_fini(struct drm_device *dev, bool suspend, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	if (!suspend) {
		if (drm_drv_uses_atomic_modeset(dev))
			drm_atomic_helper_shutdown(dev);
		else
			drm_helper_force_disable_all(dev);
	}

	/* disable hotplug interrupts */
	drm_connector_list_iter_begin(dev, &conn_iter);
	nouveau_for_each_non_mst_connector_iter(connector, &conn_iter) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nvif_notify_put(&conn->hpd);
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!runtime)
		cancel_work_sync(&drm->hpd_work);

	drm_kms_helper_poll_disable(dev);
	disp->fini(dev, runtime, suspend);
}

static void
nouveau_display_create_properties(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);
	int gen;

	if (disp->disp.object.oclass < NV50_DISP)
		gen = 0;
	else
	if (disp->disp.object.oclass < GF110_DISP)
		gen = 1;
	else
		gen = 2;

	PROP_ENUM(disp->dithering_mode, gen, "dithering mode", dither_mode);
	PROP_ENUM(disp->dithering_depth, gen, "dithering depth", dither_depth);
	PROP_ENUM(disp->underscan_property, gen, "underscan", underscan);

	disp->underscan_hborder_property =
		drm_property_create_range(dev, 0, "underscan hborder", 0, 128);

	disp->underscan_vborder_property =
		drm_property_create_range(dev, 0, "underscan vborder", 0, 128);

	if (gen < 1)
		return;

	/* -90..+90 */
	disp->vibrant_hue_property =
		drm_property_create_range(dev, 0, "vibrant hue", 0, 180);

	/* -100..+100 */
	disp->color_vibrance_property =
		drm_property_create_range(dev, 0, "color vibrance", 0, 200);
}

int
nouveau_display_create(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nvkm_device *device = nvxx_device(&drm->client.device);
	struct nouveau_display *disp;
	int ret;

	disp = drm->display = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	drm_mode_config_init(dev);
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dvi_i_properties(dev);

	dev->mode_config.funcs = &nouveau_mode_config_funcs;
	dev->mode_config.fb_base = device->func->resource_addr(device, 1);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_CELSIUS) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_TESLA) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_FERMI) {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	} else {
		dev->mode_config.max_width = 16384;
		dev->mode_config.max_height = 16384;
	}

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	if (drm->client.device.info.chipset < 0x11)
		dev->mode_config.async_page_flip = false;
	else
		dev->mode_config.async_page_flip = true;

	drm_kms_helper_poll_init(dev);
	drm_kms_helper_poll_disable(dev);

	if (nouveau_modeset != 2 && drm->vbios.dcb.entries) {
		ret = nvif_disp_ctor(&drm->client.device, "kmsDisp", 0,
				     &disp->disp);
		if (ret == 0) {
			nouveau_display_create_properties(dev);
			if (disp->disp.object.oclass < NV50_DISP)
				ret = nv04_display_create(dev);
			else
				ret = nv50_display_create(dev);
		}
	} else {
		ret = 0;
	}

	if (ret)
		goto disp_create_err;

	drm_mode_config_reset(dev);

	if (dev->mode_config.num_crtc) {
		ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
		if (ret)
			goto vblank_err;

		if (disp->disp.object.oclass >= NV50_DISP)
			nv50_crc_init(dev);
	}

	INIT_WORK(&drm->hpd_work, nouveau_display_hpd_work);
	mutex_init(&drm->hpd_lock);
#ifdef CONFIG_ACPI
	drm->acpi_nb.notifier_call = nouveau_display_acpi_ntfy;
	register_acpi_notifier(&drm->acpi_nb);
#endif

	return 0;

vblank_err:
	disp->dtor(dev);
disp_create_err:
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	return ret;
}

void
nouveau_display_destroy(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);

#ifdef CONFIG_ACPI
	unregister_acpi_notifier(&drm->acpi_nb);
#endif

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);

	if (disp->dtor)
		disp->dtor(dev);

	nvif_disp_dtor(&disp->disp);

	nouveau_drm(dev)->display = NULL;
	mutex_destroy(&drm->hpd_lock);
	kfree(disp);
}

int
nouveau_display_suspend(struct drm_device *dev, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);

	if (drm_drv_uses_atomic_modeset(dev)) {
		if (!runtime) {
			disp->suspend = drm_atomic_helper_suspend(dev);
			if (IS_ERR(disp->suspend)) {
				int ret = PTR_ERR(disp->suspend);
				disp->suspend = NULL;
				return ret;
			}
		}
	}

	nouveau_display_fini(dev, true, runtime);
	return 0;
}

void
nouveau_display_resume(struct drm_device *dev, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);

	nouveau_display_init(dev, true, runtime);

	if (drm_drv_uses_atomic_modeset(dev)) {
		if (disp->suspend) {
			drm_atomic_helper_resume(dev, disp->suspend);
			disp->suspend = NULL;
		}
		return;
	}
}

int
nouveau_display_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct nouveau_cli *cli = nouveau_cli(file_priv);
	struct nouveau_bo *bo;
	uint32_t domain;
	int ret;

	args->pitch = roundup(args->width * (args->bpp / 8), 256);
	args->size = args->pitch * args->height;
	args->size = roundup(args->size, PAGE_SIZE);

	/* Use VRAM if there is any ; otherwise fallback to system memory */
	if (nouveau_drm(dev)->client.device.info.ram_size != 0)
		domain = NOUVEAU_GEM_DOMAIN_VRAM;
	else
		domain = NOUVEAU_GEM_DOMAIN_GART;

	ret = nouveau_gem_new(cli, args->size, 0, domain, 0, 0, &bo);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file_priv, &bo->bo.base, &args->handle);
	drm_gem_object_put(&bo->bo.base);
	return ret;
}

int
nouveau_display_dumb_map_offset(struct drm_file *file_priv,
				struct drm_device *dev,
				uint32_t handle, uint64_t *poffset)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(file_priv, handle);
	if (gem) {
		struct nouveau_bo *bo = nouveau_gem_object(gem);
		*poffset = drm_vma_node_offset_addr(&bo->bo.base.vma_node);
		drm_gem_object_put(gem);
		return 0;
	}

	return -ENOENT;
}
