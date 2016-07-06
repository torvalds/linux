/*
 * Copyright 2013 Ilia Mirkin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Implementation based on the pre-KMS implementation in xf86-video-nouveau,
 * written by Arthur Huillet.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fourcc.h>

#include "nouveau_drv.h"

#include "nouveau_bo.h"
#include "nouveau_connector.h"
#include "nouveau_display.h"
#include "nvreg.h"


struct nouveau_plane {
	struct drm_plane base;
	bool flip;
	struct nouveau_bo *cur;

	struct {
		struct drm_property *colorkey;
		struct drm_property *contrast;
		struct drm_property *brightness;
		struct drm_property *hue;
		struct drm_property *saturation;
		struct drm_property *iturbt_709;
	} props;

	int colorkey;
	int contrast;
	int brightness;
	int hue;
	int saturation;
	int iturbt_709;

	void (*set_params)(struct nouveau_plane *);
};

static uint32_t formats[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_NV12,
};

/* Sine can be approximated with
 * http://en.wikipedia.org/wiki/Bhaskara_I's_sine_approximation_formula
 * sin(x degrees) ~= 4 x (180 - x) / (40500 - x (180 - x) )
 * Note that this only works for the range [0, 180].
 * Also note that sin(x) == -sin(x - 180)
 */
static inline int
sin_mul(int degrees, int factor)
{
	if (degrees > 180) {
		degrees -= 180;
		factor *= -1;
	}
	return factor * 4 * degrees * (180 - degrees) /
		(40500 - degrees * (180 - degrees));
}

/* cos(x) = sin(x + 90) */
static inline int
cos_mul(int degrees, int factor)
{
	return sin_mul((degrees + 90) % 360, factor);
}

static int
nv10_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		  struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		  unsigned int crtc_w, unsigned int crtc_h,
		  uint32_t src_x, uint32_t src_y,
		  uint32_t src_w, uint32_t src_h)
{
	struct nouveau_drm *drm = nouveau_drm(plane->dev);
	struct nvif_object *dev = &drm->device.object;
	struct nouveau_plane *nv_plane =
		container_of(plane, struct nouveau_plane, base);
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nouveau_bo *cur = nv_plane->cur;
	bool flip = nv_plane->flip;
	int soff = NV_PCRTC0_SIZE * nv_crtc->index;
	int soff2 = NV_PCRTC0_SIZE * !nv_crtc->index;
	int format, ret;

	/* Source parameters given in 16.16 fixed point, ignore fractional. */
	src_x >>= 16;
	src_y >>= 16;
	src_w >>= 16;
	src_h >>= 16;

	format = ALIGN(src_w * 4, 0x100);

	if (format > 0xffff)
		return -ERANGE;

	if (drm->device.info.chipset >= 0x30) {
		if (crtc_w < (src_w >> 1) || crtc_h < (src_h >> 1))
			return -ERANGE;
	} else {
		if (crtc_w < (src_w >> 3) || crtc_h < (src_h >> 3))
			return -ERANGE;
	}

	ret = nouveau_bo_pin(nv_fb->nvbo, TTM_PL_FLAG_VRAM, false);
	if (ret)
		return ret;

	nv_plane->cur = nv_fb->nvbo;

	nvif_mask(dev, NV_PCRTC_ENGINE_CTRL + soff, NV_CRTC_FSEL_OVERLAY, NV_CRTC_FSEL_OVERLAY);
	nvif_mask(dev, NV_PCRTC_ENGINE_CTRL + soff2, NV_CRTC_FSEL_OVERLAY, 0);

	nvif_wr32(dev, NV_PVIDEO_BASE(flip), 0);
	nvif_wr32(dev, NV_PVIDEO_OFFSET_BUFF(flip), nv_fb->nvbo->bo.offset);
	nvif_wr32(dev, NV_PVIDEO_SIZE_IN(flip), src_h << 16 | src_w);
	nvif_wr32(dev, NV_PVIDEO_POINT_IN(flip), src_y << 16 | src_x);
	nvif_wr32(dev, NV_PVIDEO_DS_DX(flip), (src_w << 20) / crtc_w);
	nvif_wr32(dev, NV_PVIDEO_DT_DY(flip), (src_h << 20) / crtc_h);
	nvif_wr32(dev, NV_PVIDEO_POINT_OUT(flip), crtc_y << 16 | crtc_x);
	nvif_wr32(dev, NV_PVIDEO_SIZE_OUT(flip), crtc_h << 16 | crtc_w);

	if (fb->pixel_format != DRM_FORMAT_UYVY)
		format |= NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8;
	if (fb->pixel_format == DRM_FORMAT_NV12)
		format |= NV_PVIDEO_FORMAT_PLANAR;
	if (nv_plane->iturbt_709)
		format |= NV_PVIDEO_FORMAT_MATRIX_ITURBT709;
	if (nv_plane->colorkey & (1 << 24))
		format |= NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY;

	if (fb->pixel_format == DRM_FORMAT_NV12) {
		nvif_wr32(dev, NV_PVIDEO_UVPLANE_BASE(flip), 0);
		nvif_wr32(dev, NV_PVIDEO_UVPLANE_OFFSET_BUFF(flip),
			nv_fb->nvbo->bo.offset + fb->offsets[1]);
	}
	nvif_wr32(dev, NV_PVIDEO_FORMAT(flip), format);
	nvif_wr32(dev, NV_PVIDEO_STOP, 0);
	/* TODO: wait for vblank? */
	nvif_wr32(dev, NV_PVIDEO_BUFFER, flip ? 0x10 : 0x1);
	nv_plane->flip = !flip;

	if (cur)
		nouveau_bo_unpin(cur);

	return 0;
}

static int
nv10_disable_plane(struct drm_plane *plane)
{
	struct nvif_object *dev = &nouveau_drm(plane->dev)->device.object;
	struct nouveau_plane *nv_plane =
		container_of(plane, struct nouveau_plane, base);

	nvif_wr32(dev, NV_PVIDEO_STOP, 1);
	if (nv_plane->cur) {
		nouveau_bo_unpin(nv_plane->cur);
		nv_plane->cur = NULL;
	}

	return 0;
}

static void
nv_destroy_plane(struct drm_plane *plane)
{
	plane->funcs->disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(plane);
}

static void
nv10_set_params(struct nouveau_plane *plane)
{
	struct nvif_object *dev = &nouveau_drm(plane->base.dev)->device.object;
	u32 luma = (plane->brightness - 512) << 16 | plane->contrast;
	u32 chroma = ((sin_mul(plane->hue, plane->saturation) & 0xffff) << 16) |
		(cos_mul(plane->hue, plane->saturation) & 0xffff);
	u32 format = 0;

	nvif_wr32(dev, NV_PVIDEO_LUMINANCE(0), luma);
	nvif_wr32(dev, NV_PVIDEO_LUMINANCE(1), luma);
	nvif_wr32(dev, NV_PVIDEO_CHROMINANCE(0), chroma);
	nvif_wr32(dev, NV_PVIDEO_CHROMINANCE(1), chroma);
	nvif_wr32(dev, NV_PVIDEO_COLOR_KEY, plane->colorkey & 0xffffff);

	if (plane->cur) {
		if (plane->iturbt_709)
			format |= NV_PVIDEO_FORMAT_MATRIX_ITURBT709;
		if (plane->colorkey & (1 << 24))
			format |= NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY;
		nvif_mask(dev, NV_PVIDEO_FORMAT(plane->flip),
			NV_PVIDEO_FORMAT_MATRIX_ITURBT709 |
			NV_PVIDEO_FORMAT_DISPLAY_COLOR_KEY,
			format);
	}
}

static int
nv_set_property(struct drm_plane *plane,
		struct drm_property *property,
		uint64_t value)
{
	struct nouveau_plane *nv_plane =
		container_of(plane, struct nouveau_plane, base);

	if (property == nv_plane->props.colorkey)
		nv_plane->colorkey = value;
	else if (property == nv_plane->props.contrast)
		nv_plane->contrast = value;
	else if (property == nv_plane->props.brightness)
		nv_plane->brightness = value;
	else if (property == nv_plane->props.hue)
		nv_plane->hue = value;
	else if (property == nv_plane->props.saturation)
		nv_plane->saturation = value;
	else if (property == nv_plane->props.iturbt_709)
		nv_plane->iturbt_709 = value;
	else
		return -EINVAL;

	if (nv_plane->set_params)
		nv_plane->set_params(nv_plane);
	return 0;
}

static const struct drm_plane_funcs nv10_plane_funcs = {
	.update_plane = nv10_update_plane,
	.disable_plane = nv10_disable_plane,
	.set_property = nv_set_property,
	.destroy = nv_destroy_plane,
};

static void
nv10_overlay_init(struct drm_device *device)
{
	struct nouveau_drm *drm = nouveau_drm(device);
	struct nouveau_plane *plane = kzalloc(sizeof(struct nouveau_plane), GFP_KERNEL);
	unsigned int num_formats = ARRAY_SIZE(formats);
	int ret;

	if (!plane)
		return;

	switch (drm->device.info.chipset) {
	case 0x10:
	case 0x11:
	case 0x15:
	case 0x1a:
	case 0x20:
		num_formats = 2;
		break;
	}

	ret = drm_plane_init(device, &plane->base, 3 /* both crtc's */,
			     &nv10_plane_funcs,
			     formats, num_formats, false);
	if (ret)
		goto err;

	/* Set up the plane properties */
	plane->props.colorkey = drm_property_create_range(
			device, 0, "colorkey", 0, 0x01ffffff);
	plane->props.contrast = drm_property_create_range(
			device, 0, "contrast", 0, 8192 - 1);
	plane->props.brightness = drm_property_create_range(
			device, 0, "brightness", 0, 1024);
	plane->props.hue = drm_property_create_range(
			device, 0, "hue", 0, 359);
	plane->props.saturation = drm_property_create_range(
			device, 0, "saturation", 0, 8192 - 1);
	plane->props.iturbt_709 = drm_property_create_range(
			device, 0, "iturbt_709", 0, 1);
	if (!plane->props.colorkey ||
	    !plane->props.contrast ||
	    !plane->props.brightness ||
	    !plane->props.hue ||
	    !plane->props.saturation ||
	    !plane->props.iturbt_709)
		goto cleanup;

	plane->colorkey = 0;
	drm_object_attach_property(&plane->base.base,
				   plane->props.colorkey, plane->colorkey);

	plane->contrast = 0x1000;
	drm_object_attach_property(&plane->base.base,
				   plane->props.contrast, plane->contrast);

	plane->brightness = 512;
	drm_object_attach_property(&plane->base.base,
				   plane->props.brightness, plane->brightness);

	plane->hue = 0;
	drm_object_attach_property(&plane->base.base,
				   plane->props.hue, plane->hue);

	plane->saturation = 0x1000;
	drm_object_attach_property(&plane->base.base,
				   plane->props.saturation, plane->saturation);

	plane->iturbt_709 = 0;
	drm_object_attach_property(&plane->base.base,
				   plane->props.iturbt_709, plane->iturbt_709);

	plane->set_params = nv10_set_params;
	nv10_set_params(plane);
	nv10_disable_plane(&plane->base);
	return;
cleanup:
	drm_plane_cleanup(&plane->base);
err:
	kfree(plane);
	NV_ERROR(drm, "Failed to create plane\n");
}

static int
nv04_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		  struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		  unsigned int crtc_w, unsigned int crtc_h,
		  uint32_t src_x, uint32_t src_y,
		  uint32_t src_w, uint32_t src_h)
{
	struct nvif_object *dev = &nouveau_drm(plane->dev)->device.object;
	struct nouveau_plane *nv_plane =
		container_of(plane, struct nouveau_plane, base);
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nouveau_bo *cur = nv_plane->cur;
	uint32_t overlay = 1;
	int brightness = (nv_plane->brightness - 512) * 62 / 512;
	int pitch, ret, i;

	/* Source parameters given in 16.16 fixed point, ignore fractional. */
	src_x >>= 16;
	src_y >>= 16;
	src_w >>= 16;
	src_h >>= 16;

	pitch = ALIGN(src_w * 4, 0x100);

	if (pitch > 0xffff)
		return -ERANGE;

	/* TODO: Compute an offset? Not sure how to do this for YUYV. */
	if (src_x != 0 || src_y != 0)
		return -ERANGE;

	if (crtc_w < src_w || crtc_h < src_h)
		return -ERANGE;

	ret = nouveau_bo_pin(nv_fb->nvbo, TTM_PL_FLAG_VRAM, false);
	if (ret)
		return ret;

	nv_plane->cur = nv_fb->nvbo;

	nvif_wr32(dev, NV_PVIDEO_OE_STATE, 0);
	nvif_wr32(dev, NV_PVIDEO_SU_STATE, 0);
	nvif_wr32(dev, NV_PVIDEO_RM_STATE, 0);

	for (i = 0; i < 2; i++) {
		nvif_wr32(dev, NV_PVIDEO_BUFF0_START_ADDRESS + 4 * i,
			nv_fb->nvbo->bo.offset);
		nvif_wr32(dev, NV_PVIDEO_BUFF0_PITCH_LENGTH + 4 * i, pitch);
		nvif_wr32(dev, NV_PVIDEO_BUFF0_OFFSET + 4 * i, 0);
	}
	nvif_wr32(dev, NV_PVIDEO_WINDOW_START, crtc_y << 16 | crtc_x);
	nvif_wr32(dev, NV_PVIDEO_WINDOW_SIZE, crtc_h << 16 | crtc_w);
	nvif_wr32(dev, NV_PVIDEO_STEP_SIZE,
		(uint32_t)(((src_h - 1) << 11) / (crtc_h - 1)) << 16 | (uint32_t)(((src_w - 1) << 11) / (crtc_w - 1)));

	/* It should be possible to convert hue/contrast to this */
	nvif_wr32(dev, NV_PVIDEO_RED_CSC_OFFSET, 0x69 - brightness);
	nvif_wr32(dev, NV_PVIDEO_GREEN_CSC_OFFSET, 0x3e + brightness);
	nvif_wr32(dev, NV_PVIDEO_BLUE_CSC_OFFSET, 0x89 - brightness);
	nvif_wr32(dev, NV_PVIDEO_CSC_ADJUST, 0);

	nvif_wr32(dev, NV_PVIDEO_CONTROL_Y, 0x001); /* (BLUR_ON, LINE_HALF) */
	nvif_wr32(dev, NV_PVIDEO_CONTROL_X, 0x111); /* (WEIGHT_HEAVY, SHARPENING_ON, SMOOTHING_ON) */

	nvif_wr32(dev, NV_PVIDEO_FIFO_BURST_LENGTH, 0x03);
	nvif_wr32(dev, NV_PVIDEO_FIFO_THRES_SIZE, 0x38);

	nvif_wr32(dev, NV_PVIDEO_KEY, nv_plane->colorkey);

	if (nv_plane->colorkey & (1 << 24))
		overlay |= 0x10;
	if (fb->pixel_format == DRM_FORMAT_YUYV)
		overlay |= 0x100;

	nvif_wr32(dev, NV_PVIDEO_OVERLAY, overlay);

	nvif_wr32(dev, NV_PVIDEO_SU_STATE, nvif_rd32(dev, NV_PVIDEO_SU_STATE) ^ (1 << 16));

	if (cur)
		nouveau_bo_unpin(cur);

	return 0;
}

static int
nv04_disable_plane(struct drm_plane *plane)
{
	struct nvif_object *dev = &nouveau_drm(plane->dev)->device.object;
	struct nouveau_plane *nv_plane =
		container_of(plane, struct nouveau_plane, base);

	nvif_mask(dev, NV_PVIDEO_OVERLAY, 1, 0);
	nvif_wr32(dev, NV_PVIDEO_OE_STATE, 0);
	nvif_wr32(dev, NV_PVIDEO_SU_STATE, 0);
	nvif_wr32(dev, NV_PVIDEO_RM_STATE, 0);
	if (nv_plane->cur) {
		nouveau_bo_unpin(nv_plane->cur);
		nv_plane->cur = NULL;
	}

	return 0;
}

static const struct drm_plane_funcs nv04_plane_funcs = {
	.update_plane = nv04_update_plane,
	.disable_plane = nv04_disable_plane,
	.set_property = nv_set_property,
	.destroy = nv_destroy_plane,
};

static void
nv04_overlay_init(struct drm_device *device)
{
	struct nouveau_drm *drm = nouveau_drm(device);
	struct nouveau_plane *plane = kzalloc(sizeof(struct nouveau_plane), GFP_KERNEL);
	int ret;

	if (!plane)
		return;

	ret = drm_plane_init(device, &plane->base, 1 /* single crtc */,
			     &nv04_plane_funcs,
			     formats, 2, false);
	if (ret)
		goto err;

	/* Set up the plane properties */
	plane->props.colorkey = drm_property_create_range(
			device, 0, "colorkey", 0, 0x01ffffff);
	plane->props.brightness = drm_property_create_range(
			device, 0, "brightness", 0, 1024);
	if (!plane->props.colorkey ||
	    !plane->props.brightness)
		goto cleanup;

	plane->colorkey = 0;
	drm_object_attach_property(&plane->base.base,
				   plane->props.colorkey, plane->colorkey);

	plane->brightness = 512;
	drm_object_attach_property(&plane->base.base,
				   plane->props.brightness, plane->brightness);

	nv04_disable_plane(&plane->base);
	return;
cleanup:
	drm_plane_cleanup(&plane->base);
err:
	kfree(plane);
	NV_ERROR(drm, "Failed to create plane\n");
}

void
nouveau_overlay_init(struct drm_device *device)
{
	struct nvif_device *dev = &nouveau_drm(device)->device;
	if (dev->info.chipset < 0x10)
		nv04_overlay_init(device);
	else if (dev->info.chipset <= 0x40)
		nv10_overlay_init(device);
}
