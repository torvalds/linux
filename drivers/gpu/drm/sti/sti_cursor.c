/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Vincent Abriou <vincent.abriou@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <drm/drmP.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "sti_compositor.h"
#include "sti_cursor.h"
#include "sti_plane.h"
#include "sti_vtg.h"

/* Registers */
#define CUR_CTL             0x00
#define CUR_VPO             0x0C
#define CUR_PML             0x14
#define CUR_PMP             0x18
#define CUR_SIZE            0x1C
#define CUR_CML             0x20
#define CUR_AWS             0x28
#define CUR_AWE             0x2C

#define CUR_CTL_CLUT_UPDATE BIT(1)

#define STI_CURS_MIN_SIZE   1
#define STI_CURS_MAX_SIZE   128

/*
 * pixmap dma buffer stucture
 *
 * @paddr:  physical address
 * @size:   buffer size
 * @base:   virtual address
 */
struct dma_pixmap {
	dma_addr_t paddr;
	size_t size;
	void *base;
};

/**
 * STI Cursor structure
 *
 * @sti_plane:    sti_plane structure
 * @dev:          driver device
 * @regs:         cursor registers
 * @width:        cursor width
 * @height:       cursor height
 * @clut:         color look up table
 * @clut_paddr:   color look up table physical address
 * @pixmap:       pixmap dma buffer (clut8-format cursor)
 */
struct sti_cursor {
	struct sti_plane plane;
	struct device *dev;
	void __iomem *regs;
	unsigned int width;
	unsigned int height;
	unsigned short *clut;
	dma_addr_t clut_paddr;
	struct dma_pixmap pixmap;
};

static const uint32_t cursor_supported_formats[] = {
	DRM_FORMAT_ARGB8888,
};

#define to_sti_cursor(x) container_of(x, struct sti_cursor, plane)

static void sti_cursor_argb8888_to_clut8(struct sti_cursor *cursor, u32 *src)
{
	u8  *dst = cursor->pixmap.base;
	unsigned int i, j;
	u32 a, r, g, b;

	for (i = 0; i < cursor->height; i++) {
		for (j = 0; j < cursor->width; j++) {
			/* Pick the 2 higher bits of each component */
			a = (*src >> 30) & 3;
			r = (*src >> 22) & 3;
			g = (*src >> 14) & 3;
			b = (*src >> 6) & 3;
			*dst = a << 6 | r << 4 | g << 2 | b;
			src++;
			dst++;
		}
	}
}

static void sti_cursor_init(struct sti_cursor *cursor)
{
	unsigned short *base = cursor->clut;
	unsigned int a, r, g, b;

	/* Assign CLUT values, ARGB444 format */
	for (a = 0; a < 4; a++)
		for (r = 0; r < 4; r++)
			for (g = 0; g < 4; g++)
				for (b = 0; b < 4; b++)
					*base++ = (a * 5) << 12 |
						  (r * 5) << 8 |
						  (g * 5) << 4 |
						  (b * 5);
}

static void sti_cursor_atomic_update(struct drm_plane *drm_plane,
				     struct drm_plane_state *oldstate)
{
	struct drm_plane_state *state = drm_plane->state;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_cursor *cursor = to_sti_cursor(plane);
	struct drm_crtc *crtc = state->crtc;
	struct sti_mixer *mixer = to_sti_mixer(crtc);
	struct drm_framebuffer *fb = state->fb;
	struct drm_display_mode *mode = &crtc->mode;
	int dst_x = state->crtc_x;
	int dst_y = state->crtc_y;
	int dst_w = clamp_val(state->crtc_w, 0, mode->crtc_hdisplay - dst_x);
	int dst_h = clamp_val(state->crtc_h, 0, mode->crtc_vdisplay - dst_y);
	/* src_x are in 16.16 format */
	int src_w = state->src_w >> 16;
	int src_h = state->src_h >> 16;
	bool first_prepare = plane->status == STI_PLANE_DISABLED ? true : false;
	struct drm_gem_cma_object *cma_obj;
	u32 y, x;
	u32 val;

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("(%dx%d)@(%d,%d)\n", dst_w, dst_h, dst_x, dst_y);

	dev_dbg(cursor->dev, "%s %s\n", __func__,
		sti_plane_to_str(plane));

	if (src_w < STI_CURS_MIN_SIZE ||
	    src_h < STI_CURS_MIN_SIZE ||
	    src_w > STI_CURS_MAX_SIZE ||
	    src_h > STI_CURS_MAX_SIZE) {
		DRM_ERROR("Invalid cursor size (%dx%d)\n",
				src_w, src_h);
		return;
	}

	/* If the cursor size has changed, re-allocated the pixmap */
	if (!cursor->pixmap.base ||
	    (cursor->width != src_w) ||
	    (cursor->height != src_h)) {
		cursor->width = src_w;
		cursor->height = src_h;

		if (cursor->pixmap.base)
			dma_free_writecombine(cursor->dev,
					      cursor->pixmap.size,
					      cursor->pixmap.base,
					      cursor->pixmap.paddr);

		cursor->pixmap.size = cursor->width * cursor->height;

		cursor->pixmap.base = dma_alloc_writecombine(cursor->dev,
							cursor->pixmap.size,
							&cursor->pixmap.paddr,
							GFP_KERNEL | GFP_DMA);
		if (!cursor->pixmap.base) {
			DRM_ERROR("Failed to allocate memory for pixmap\n");
			return;
		}
	}

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!cma_obj) {
		DRM_ERROR("Can't get CMA GEM object for fb\n");
		return;
	}

	/* Convert ARGB8888 to CLUT8 */
	sti_cursor_argb8888_to_clut8(cursor, (u32 *)cma_obj->vaddr);

	/* AWS and AWE depend on the mode */
	y = sti_vtg_get_line_number(*mode, 0);
	x = sti_vtg_get_pixel_number(*mode, 0);
	val = y << 16 | x;
	writel(val, cursor->regs + CUR_AWS);
	y = sti_vtg_get_line_number(*mode, mode->vdisplay - 1);
	x = sti_vtg_get_pixel_number(*mode, mode->hdisplay - 1);
	val = y << 16 | x;
	writel(val, cursor->regs + CUR_AWE);

	if (first_prepare) {
		/* Set and fetch CLUT */
		writel(cursor->clut_paddr, cursor->regs + CUR_CML);
		writel(CUR_CTL_CLUT_UPDATE, cursor->regs + CUR_CTL);
	}

	/* Set memory location, size, and position */
	writel(cursor->pixmap.paddr, cursor->regs + CUR_PML);
	writel(cursor->width, cursor->regs + CUR_PMP);
	writel(cursor->height << 16 | cursor->width, cursor->regs + CUR_SIZE);

	y = sti_vtg_get_line_number(*mode, dst_y);
	x = sti_vtg_get_pixel_number(*mode, dst_y);
	writel((y << 16) | x, cursor->regs + CUR_VPO);

	plane->status = STI_PLANE_UPDATED;
}

static void sti_cursor_atomic_disable(struct drm_plane *drm_plane,
				      struct drm_plane_state *oldstate)
{
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_mixer *mixer = to_sti_mixer(drm_plane->crtc);

	if (!drm_plane->crtc) {
		DRM_DEBUG_DRIVER("drm plane:%d not enabled\n",
				 drm_plane->base.id);
		return;
	}

	DRM_DEBUG_DRIVER("CRTC:%d (%s) drm plane:%d (%s)\n",
			 drm_plane->crtc->base.id, sti_mixer_to_str(mixer),
			 drm_plane->base.id, sti_plane_to_str(plane));

	plane->status = STI_PLANE_DISABLING;
}

static const struct drm_plane_helper_funcs sti_cursor_helpers_funcs = {
	.atomic_update = sti_cursor_atomic_update,
	.atomic_disable = sti_cursor_atomic_disable,
};

struct drm_plane *sti_cursor_create(struct drm_device *drm_dev,
				    struct device *dev, int desc,
				    void __iomem *baseaddr,
				    unsigned int possible_crtcs)
{
	struct sti_cursor *cursor;
	size_t size;
	int res;

	cursor = devm_kzalloc(dev, sizeof(*cursor), GFP_KERNEL);
	if (!cursor) {
		DRM_ERROR("Failed to allocate memory for cursor\n");
		return NULL;
	}

	/* Allocate clut buffer */
	size = 0x100 * sizeof(unsigned short);
	cursor->clut = dma_alloc_writecombine(dev, size, &cursor->clut_paddr,
					      GFP_KERNEL | GFP_DMA);

	if (!cursor->clut) {
		DRM_ERROR("Failed to allocate memory for cursor clut\n");
		goto err_clut;
	}

	cursor->dev = dev;
	cursor->regs = baseaddr;
	cursor->plane.desc = desc;
	cursor->plane.status = STI_PLANE_DISABLED;

	sti_cursor_init(cursor);

	res = drm_universal_plane_init(drm_dev, &cursor->plane.drm_plane,
				       possible_crtcs,
				       &sti_plane_helpers_funcs,
				       cursor_supported_formats,
				       ARRAY_SIZE(cursor_supported_formats),
				       DRM_PLANE_TYPE_CURSOR, NULL);
	if (res) {
		DRM_ERROR("Failed to initialize universal plane\n");
		goto err_plane;
	}

	drm_plane_helper_add(&cursor->plane.drm_plane,
			     &sti_cursor_helpers_funcs);

	sti_plane_init_property(&cursor->plane, DRM_PLANE_TYPE_CURSOR);

	return &cursor->plane.drm_plane;

err_plane:
	dma_free_writecombine(dev, size, cursor->clut, cursor->clut_paddr);
err_clut:
	devm_kfree(dev, cursor);
	return NULL;
}
