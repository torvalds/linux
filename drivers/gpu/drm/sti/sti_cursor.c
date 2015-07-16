/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Vincent Abriou <vincent.abriou@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <drm/drmP.h>

#include "sti_cursor.h"
#include "sti_layer.h"
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
 * @layer:      layer structure
 * @width:      cursor width
 * @height:     cursor height
 * @clut:       color look up table
 * @clut_paddr: color look up table physical address
 * @pixmap:     pixmap dma buffer (clut8-format cursor)
 */
struct sti_cursor {
	struct sti_layer layer;
	unsigned int width;
	unsigned int height;
	unsigned short *clut;
	dma_addr_t clut_paddr;
	struct dma_pixmap pixmap;
};

static const uint32_t cursor_supported_formats[] = {
	DRM_FORMAT_ARGB8888,
};

#define to_sti_cursor(x) container_of(x, struct sti_cursor, layer)

static const uint32_t *sti_cursor_get_formats(struct sti_layer *layer)
{
	return cursor_supported_formats;
}

static unsigned int sti_cursor_get_nb_formats(struct sti_layer *layer)
{
	return ARRAY_SIZE(cursor_supported_formats);
}

static void sti_cursor_argb8888_to_clut8(struct sti_layer *layer)
{
	struct sti_cursor *cursor = to_sti_cursor(layer);
	u32 *src = layer->vaddr;
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

static int sti_cursor_prepare_layer(struct sti_layer *layer, bool first_prepare)
{
	struct sti_cursor *cursor = to_sti_cursor(layer);
	struct drm_display_mode *mode = layer->mode;
	u32 y, x;
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	dev_dbg(layer->dev, "%s %s\n", __func__, sti_layer_to_str(layer));

	if (layer->src_w < STI_CURS_MIN_SIZE ||
	    layer->src_h < STI_CURS_MIN_SIZE ||
	    layer->src_w > STI_CURS_MAX_SIZE ||
	    layer->src_h > STI_CURS_MAX_SIZE) {
		DRM_ERROR("Invalid cursor size (%dx%d)\n",
				layer->src_w, layer->src_h);
		return -EINVAL;
	}

	/* If the cursor size has changed, re-allocated the pixmap */
	if (!cursor->pixmap.base ||
	    (cursor->width != layer->src_w) ||
	    (cursor->height != layer->src_h)) {
		cursor->width = layer->src_w;
		cursor->height = layer->src_h;

		if (cursor->pixmap.base)
			dma_free_writecombine(layer->dev,
					      cursor->pixmap.size,
					      cursor->pixmap.base,
					      cursor->pixmap.paddr);

		cursor->pixmap.size = cursor->width * cursor->height;

		cursor->pixmap.base = dma_alloc_writecombine(layer->dev,
							cursor->pixmap.size,
							&cursor->pixmap.paddr,
							GFP_KERNEL | GFP_DMA);
		if (!cursor->pixmap.base) {
			DRM_ERROR("Failed to allocate memory for pixmap\n");
			return -ENOMEM;
		}
	}

	/* Convert ARGB8888 to CLUT8 */
	sti_cursor_argb8888_to_clut8(layer);

	/* AWS and AWE depend on the mode */
	y = sti_vtg_get_line_number(*mode, 0);
	x = sti_vtg_get_pixel_number(*mode, 0);
	val = y << 16 | x;
	writel(val, layer->regs + CUR_AWS);
	y = sti_vtg_get_line_number(*mode, mode->vdisplay - 1);
	x = sti_vtg_get_pixel_number(*mode, mode->hdisplay - 1);
	val = y << 16 | x;
	writel(val, layer->regs + CUR_AWE);

	if (first_prepare) {
		/* Set and fetch CLUT */
		writel(cursor->clut_paddr, layer->regs + CUR_CML);
		writel(CUR_CTL_CLUT_UPDATE, layer->regs + CUR_CTL);
	}

	return 0;
}

static int sti_cursor_commit_layer(struct sti_layer *layer)
{
	struct sti_cursor *cursor = to_sti_cursor(layer);
	struct drm_display_mode *mode = layer->mode;
	u32 ydo, xdo;

	dev_dbg(layer->dev, "%s %s\n", __func__, sti_layer_to_str(layer));

	/* Set memory location, size, and position */
	writel(cursor->pixmap.paddr, layer->regs + CUR_PML);
	writel(cursor->width, layer->regs + CUR_PMP);
	writel(cursor->height << 16 | cursor->width, layer->regs + CUR_SIZE);

	ydo = sti_vtg_get_line_number(*mode, layer->dst_y);
	xdo = sti_vtg_get_pixel_number(*mode, layer->dst_y);
	writel((ydo << 16) | xdo, layer->regs + CUR_VPO);

	return 0;
}

static int sti_cursor_disable_layer(struct sti_layer *layer)
{
	return 0;
}

static void sti_cursor_init(struct sti_layer *layer)
{
	struct sti_cursor *cursor = to_sti_cursor(layer);
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

static const struct sti_layer_funcs cursor_ops = {
	.get_formats = sti_cursor_get_formats,
	.get_nb_formats = sti_cursor_get_nb_formats,
	.init = sti_cursor_init,
	.prepare = sti_cursor_prepare_layer,
	.commit = sti_cursor_commit_layer,
	.disable = sti_cursor_disable_layer,
};

struct sti_layer *sti_cursor_create(struct device *dev)
{
	struct sti_cursor *cursor;

	cursor = devm_kzalloc(dev, sizeof(*cursor), GFP_KERNEL);
	if (!cursor) {
		DRM_ERROR("Failed to allocate memory for cursor\n");
		return NULL;
	}

	/* Allocate clut buffer */
	cursor->clut = dma_alloc_writecombine(dev,
			0x100 * sizeof(unsigned short),
			&cursor->clut_paddr,
			GFP_KERNEL | GFP_DMA);

	if (!cursor->clut) {
		DRM_ERROR("Failed to allocate memory for cursor clut\n");
		devm_kfree(dev, cursor);
		return NULL;
	}

	cursor->layer.ops = &cursor_ops;

	return (struct sti_layer *)cursor;
}
