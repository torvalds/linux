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
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>

#include <nvif/class.h>

#include "nouveau_fbcon.h"
#include "dispnv04/hw.h"
#include "nouveau_crtc.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nv50_display.h"

#include "nouveau_fence.h"

#include <nvif/cl0046.h>
#include <nvif/event.h>

static int
nouveau_display_vblank_handler(struct nvif_notify *notify)
{
	struct nouveau_crtc *nv_crtc =
		container_of(notify, typeof(*nv_crtc), vblank);
	drm_crtc_handle_vblank(&nv_crtc->base);
	return NVIF_NOTIFY_KEEP;
}

int
nouveau_display_vblank_enable(struct drm_device *dev, unsigned int pipe)
{
	struct drm_crtc *crtc;
	struct nouveau_crtc *nv_crtc;

	crtc = drm_crtc_from_index(dev, pipe);
	if (!crtc)
		return -EINVAL;

	nv_crtc = nouveau_crtc(crtc);
	nvif_notify_get(&nv_crtc->vblank);

	return 0;
}

void
nouveau_display_vblank_disable(struct drm_device *dev, unsigned int pipe)
{
	struct drm_crtc *crtc;
	struct nouveau_crtc *nv_crtc;

	crtc = drm_crtc_from_index(dev, pipe);
	if (!crtc)
		return;

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
		ret = nvif_mthd(&disp->disp, 0, &args, sizeof(args));
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
nouveau_display_scanoutpos(struct drm_device *dev, unsigned int pipe,
			   bool in_vblank_irq, int *vpos, int *hpos,
			   ktime_t *stime, ktime_t *etime,
			   const struct drm_display_mode *mode)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (nouveau_crtc(crtc)->index == pipe) {
			return nouveau_display_scanoutpos_head(crtc, vpos, hpos,
							       stime, etime);
		}
	}

	return false;
}

static void
nouveau_display_vblank_fini(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		nvif_notify_fini(&nv_crtc->vblank);
	}
}

static int
nouveau_display_vblank_init(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct drm_crtc *crtc;
	int ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		ret = nvif_notify_init(&disp->disp,
				       nouveau_display_vblank_handler, false,
				       NV04_DISP_NTFY_VBLANK,
				       &(struct nvif_notify_head_req_v0) {
					.head = nv_crtc->index,
				       },
				       sizeof(struct nvif_notify_head_req_v0),
				       sizeof(struct nvif_notify_head_rep_v0),
				       &nv_crtc->vblank);
		if (ret) {
			nouveau_display_vblank_fini(dev);
			return ret;
		}
	}

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret) {
		nouveau_display_vblank_fini(dev);
		return ret;
	}

	return 0;
}

static void
nouveau_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	if (fb->nvbo)
		drm_gem_object_unreference_unlocked(&fb->nvbo->gem);

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static int
nouveau_user_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
				       struct drm_file *file_priv,
				       unsigned int *handle)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, &fb->nvbo->gem, handle);
}

static const struct drm_framebuffer_funcs nouveau_framebuffer_funcs = {
	.destroy = nouveau_user_framebuffer_destroy,
	.create_handle = nouveau_user_framebuffer_create_handle,
};

int
nouveau_framebuffer_new(struct drm_device *dev,
			const struct drm_mode_fb_cmd2 *mode_cmd,
			struct nouveau_bo *nvbo,
			struct nouveau_framebuffer **pfb)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_framebuffer *fb;
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

	if (!(fb = *pfb = kzalloc(sizeof(*fb), GFP_KERNEL)))
		return -ENOMEM;

	drm_helper_mode_fill_fb_struct(dev, &fb->base, mode_cmd);
	fb->nvbo = nvbo;

	ret = drm_framebuffer_init(dev, &fb->base, &nouveau_framebuffer_funcs);
	if (ret)
		kfree(fb);
	return ret;
}

struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct nouveau_framebuffer *fb;
	struct nouveau_bo *nvbo;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(file_priv, mode_cmd->handles[0]);
	if (!gem)
		return ERR_PTR(-ENOENT);
	nvbo = nouveau_gem_object(gem);

	ret = nouveau_framebuffer_new(dev, mode_cmd, nvbo, &fb);
	if (ret == 0)
		return &fb->base;

	drm_gem_object_unreference_unlocked(gem);
	return ERR_PTR(ret);
}

static const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
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
		c = 0;                                                         \
		while (p && l->gen_mask) {                                     \
			if (l->gen_mask & (1 << (gen))) {                      \
				drm_property_add_enum(p, c, l->type, l->name); \
				c++;                                           \
			}                                                      \
			l++;                                                   \
		}                                                              \
	}                                                                      \
} while(0)

static void
nouveau_display_hpd_work(struct work_struct *work)
{
	struct nouveau_drm *drm = container_of(work, typeof(*drm), hpd_work);

	pm_runtime_get_sync(drm->dev->dev);

	drm_helper_hpd_irq_event(drm->dev);
	/* enable polling for external displays */
	drm_kms_helper_poll_enable(drm->dev);

	pm_runtime_mark_last_busy(drm->dev->dev);
	pm_runtime_put_sync(drm->dev->dev);
}

#ifdef CONFIG_ACPI

/*
 * Hans de Goede: This define belongs in acpi/video.h, I've submitted a patch
 * to the acpi subsys to move it there from drivers/acpi/acpi_video.c .
 * This should be dropped once that is merged.
 */
#ifndef ACPI_VIDEO_NOTIFY_PROBE
#define ACPI_VIDEO_NOTIFY_PROBE			0x81
#endif

static int
nouveau_display_acpi_ntfy(struct notifier_block *nb, unsigned long val,
			  void *data)
{
	struct nouveau_drm *drm = container_of(nb, typeof(*drm), acpi_nb);
	struct acpi_bus_event *info = data;

	if (!strcmp(info->device_class, ACPI_VIDEO_CLASS)) {
		if (info->type == ACPI_VIDEO_NOTIFY_PROBE) {
			/*
			 * This may be the only indication we receive of a
			 * connector hotplug on a runtime suspended GPU,
			 * schedule hpd_work to check.
			 */
			schedule_work(&drm->hpd_work);

			/* acpi-video should not generate keypresses for this */
			return NOTIFY_BAD;
		}
	}

	return NOTIFY_DONE;
}
#endif

int
nouveau_display_init(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_connector *connector;
	int ret;

	ret = disp->init(dev);
	if (ret)
		return ret;

	/* enable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nvif_notify_get(&conn->hpd);
	}

	/* enable flip completion events */
	nvif_notify_get(&drm->flip);
	return ret;
}

void
nouveau_display_fini(struct drm_device *dev, bool suspend)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_connector *connector;

	if (!suspend) {
		if (drm_drv_uses_atomic_modeset(dev))
			drm_atomic_helper_shutdown(dev);
		else
			drm_crtc_force_disable_all(dev);
	}

	/* disable flip completion events */
	nvif_notify_put(&drm->flip);

	/* disable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nvif_notify_put(&conn->hpd);
	}

	drm_kms_helper_poll_disable(dev);
	disp->fini(dev);
}

static void
nouveau_display_create_properties(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);
	int gen;

	if (disp->disp.oclass < NV50_DISP)
		gen = 0;
	else
	if (disp->disp.oclass < GF110_DISP)
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
		static const u16 oclass[] = {
			GP102_DISP,
			GP100_DISP,
			GM200_DISP,
			GM107_DISP,
			GK110_DISP,
			GK104_DISP,
			GF110_DISP,
			GT214_DISP,
			GT206_DISP,
			GT200_DISP,
			G82_DISP,
			NV50_DISP,
			NV04_DISP,
		};
		int i;

		for (i = 0, ret = -ENODEV; ret && i < ARRAY_SIZE(oclass); i++) {
			ret = nvif_object_init(&drm->client.device.object, 0,
					       oclass[i], NULL, 0, &disp->disp);
		}

		if (ret == 0) {
			nouveau_display_create_properties(dev);
			if (disp->disp.oclass < NV50_DISP)
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
		ret = nouveau_display_vblank_init(dev);
		if (ret)
			goto vblank_err;
	}

	nouveau_backlight_init(dev);
	INIT_WORK(&drm->hpd_work, nouveau_display_hpd_work);
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

#ifdef CONFIG_ACPI
	unregister_acpi_notifier(&nouveau_drm(dev)->acpi_nb);
#endif
	nouveau_backlight_exit(dev);
	nouveau_display_vblank_fini(dev);

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);

	if (disp->dtor)
		disp->dtor(dev);

	nvif_object_fini(&disp->disp);

	nouveau_drm(dev)->display = NULL;
	kfree(disp);
}

int
nouveau_display_suspend(struct drm_device *dev, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct drm_crtc *crtc;

	if (drm_drv_uses_atomic_modeset(dev)) {
		if (!runtime) {
			disp->suspend = drm_atomic_helper_suspend(dev);
			if (IS_ERR(disp->suspend)) {
				int ret = PTR_ERR(disp->suspend);
				disp->suspend = NULL;
				return ret;
			}
		}

		nouveau_display_fini(dev, true);
		return 0;
	}

	nouveau_display_fini(dev, true);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->primary->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		nouveau_bo_unpin(nouveau_fb->nvbo);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		if (nv_crtc->cursor.nvbo) {
			if (nv_crtc->cursor.set_offset)
				nouveau_bo_unmap(nv_crtc->cursor.nvbo);
			nouveau_bo_unpin(nv_crtc->cursor.nvbo);
		}
	}

	return 0;
}

void
nouveau_display_resume(struct drm_device *dev, bool runtime)
{
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_crtc *crtc;
	int ret;

	if (drm_drv_uses_atomic_modeset(dev)) {
		nouveau_display_init(dev);
		if (disp->suspend) {
			drm_atomic_helper_resume(dev, disp->suspend);
			disp->suspend = NULL;
		}
		return;
	}

	/* re-pin fb/cursors */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->primary->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		ret = nouveau_bo_pin(nouveau_fb->nvbo, TTM_PL_FLAG_VRAM, true);
		if (ret)
			NV_ERROR(drm, "Could not pin framebuffer\n");
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		if (!nv_crtc->cursor.nvbo)
			continue;

		ret = nouveau_bo_pin(nv_crtc->cursor.nvbo, TTM_PL_FLAG_VRAM, true);
		if (!ret && nv_crtc->cursor.set_offset)
			ret = nouveau_bo_map(nv_crtc->cursor.nvbo);
		if (ret)
			NV_ERROR(drm, "Could not pin/map cursor.\n");
	}

	nouveau_display_init(dev);

	/* Force CLUT to get re-loaded during modeset */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nv_crtc->lut.depth = 0;
	}

	/* This should ensure we don't hit a locking problem when someone
	 * wakes us up via a connector.  We should never go into suspend
	 * while the display is on anyways.
	 */
	if (runtime)
		return;

	drm_helper_resume_force_mode(dev);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		if (!nv_crtc->cursor.nvbo)
			continue;

		if (nv_crtc->cursor.set_offset)
			nv_crtc->cursor.set_offset(nv_crtc, nv_crtc->cursor.nvbo->bo.offset);
		nv_crtc->cursor.set_pos(nv_crtc, nv_crtc->cursor_saved_x,
						 nv_crtc->cursor_saved_y);
	}
}

static int
nouveau_page_flip_emit(struct nouveau_channel *chan,
		       struct nouveau_bo *old_bo,
		       struct nouveau_bo *new_bo,
		       struct nouveau_page_flip_state *s,
		       struct nouveau_fence **pfence)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	unsigned long flags;
	int ret;

	/* Queue it to the pending list */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_add_tail(&s->head, &fctx->flip);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Synchronize with the old framebuffer */
	ret = nouveau_fence_sync(old_bo, chan, false, false);
	if (ret)
		goto fail;

	/* Emit the pageflip */
	ret = RING_SPACE(chan, 2);
	if (ret)
		goto fail;

	BEGIN_NV04(chan, NvSubSw, NV_SW_PAGE_FLIP, 1);
	OUT_RING  (chan, 0x00000000);
	FIRE_RING (chan);

	ret = nouveau_fence_new(chan, false, pfence);
	if (ret)
		goto fail;

	return 0;
fail:
	spin_lock_irqsave(&dev->event_lock, flags);
	list_del(&s->head);
	spin_unlock_irqrestore(&dev->event_lock, flags);
	return ret;
}

int
nouveau_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct drm_pending_vblank_event *event, u32 flags,
		       struct drm_modeset_acquire_ctx *ctx)
{
	const int swap_interval = (flags & DRM_MODE_PAGE_FLIP_ASYNC) ? 0 : 1;
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_bo *old_bo = nouveau_framebuffer(crtc->primary->fb)->nvbo;
	struct nouveau_bo *new_bo = nouveau_framebuffer(fb)->nvbo;
	struct nouveau_page_flip_state *s;
	struct nouveau_channel *chan;
	struct nouveau_cli *cli;
	struct nouveau_fence *fence;
	struct nv04_display *dispnv04 = nv04_display(dev);
	int head = nouveau_crtc(crtc)->index;
	int ret;

	chan = drm->channel;
	if (!chan)
		return -ENODEV;
	cli = (void *)chan->user.client;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	if (new_bo != old_bo) {
		ret = nouveau_bo_pin(new_bo, TTM_PL_FLAG_VRAM, true);
		if (ret)
			goto fail_free;
	}

	mutex_lock(&cli->mutex);
	ret = ttm_bo_reserve(&new_bo->bo, true, false, NULL);
	if (ret)
		goto fail_unpin;

	/* synchronise rendering channel with the kernel's channel */
	ret = nouveau_fence_sync(new_bo, chan, false, true);
	if (ret) {
		ttm_bo_unreserve(&new_bo->bo);
		goto fail_unpin;
	}

	if (new_bo != old_bo) {
		ttm_bo_unreserve(&new_bo->bo);

		ret = ttm_bo_reserve(&old_bo->bo, true, false, NULL);
		if (ret)
			goto fail_unpin;
	}

	/* Initialize a page flip struct */
	*s = (struct nouveau_page_flip_state)
		{ { }, event, crtc, fb->format->cpp[0] * 8, fb->pitches[0],
		  new_bo->bo.offset };

	/* Keep vblanks on during flip, for the target crtc of this flip */
	drm_crtc_vblank_get(crtc);

	/* Emit a page flip */
	if (swap_interval) {
		ret = RING_SPACE(chan, 8);
		if (ret)
			goto fail_unreserve;

		BEGIN_NV04(chan, NvSubImageBlit, 0x012c, 1);
		OUT_RING  (chan, 0);
		BEGIN_NV04(chan, NvSubImageBlit, 0x0134, 1);
		OUT_RING  (chan, head);
		BEGIN_NV04(chan, NvSubImageBlit, 0x0100, 1);
		OUT_RING  (chan, 0);
		BEGIN_NV04(chan, NvSubImageBlit, 0x0130, 1);
		OUT_RING  (chan, 0);
	}

	nouveau_bo_ref(new_bo, &dispnv04->image[head]);

	ret = nouveau_page_flip_emit(chan, old_bo, new_bo, s, &fence);
	if (ret)
		goto fail_unreserve;
	mutex_unlock(&cli->mutex);

	/* Update the crtc struct and cleanup */
	crtc->primary->fb = fb;

	nouveau_bo_fence(old_bo, fence, false);
	ttm_bo_unreserve(&old_bo->bo);
	if (old_bo != new_bo)
		nouveau_bo_unpin(old_bo);
	nouveau_fence_unref(&fence);
	return 0;

fail_unreserve:
	drm_crtc_vblank_put(crtc);
	ttm_bo_unreserve(&old_bo->bo);
fail_unpin:
	mutex_unlock(&cli->mutex);
	if (old_bo != new_bo)
		nouveau_bo_unpin(new_bo);
fail_free:
	kfree(s);
	return ret;
}

int
nouveau_finish_page_flip(struct nouveau_channel *chan,
			 struct nouveau_page_flip_state *ps)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	struct nouveau_page_flip_state *s;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (list_empty(&fctx->flip)) {
		NV_ERROR(drm, "unexpected pageflip\n");
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EINVAL;
	}

	s = list_first_entry(&fctx->flip, struct nouveau_page_flip_state, head);
	if (s->event) {
		drm_crtc_arm_vblank_event(s->crtc, s->event);
	} else {
		/* Give up ownership of vblank for page-flipped crtc */
		drm_crtc_vblank_put(s->crtc);
	}

	list_del(&s->head);
	if (ps)
		*ps = *s;
	kfree(s);

	spin_unlock_irqrestore(&dev->event_lock, flags);
	return 0;
}

int
nouveau_flip_complete(struct nvif_notify *notify)
{
	struct nouveau_drm *drm = container_of(notify, typeof(*drm), flip);
	struct nouveau_channel *chan = drm->channel;
	struct nouveau_page_flip_state state;

	if (!nouveau_finish_page_flip(chan, &state)) {
		nv_set_crtc_base(drm->dev, drm_crtc_index(state.crtc),
				 state.offset + state.crtc->y *
				 state.pitch + state.crtc->x *
				 state.bpp / 8);
	}

	return NVIF_NOTIFY_KEEP;
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

	ret = drm_gem_handle_create(file_priv, &bo->gem, &args->handle);
	drm_gem_object_unreference_unlocked(&bo->gem);
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
		*poffset = drm_vma_node_offset_addr(&bo->bo.vma_node);
		drm_gem_object_unreference_unlocked(gem);
		return 0;
	}

	return -ENOENT;
}
