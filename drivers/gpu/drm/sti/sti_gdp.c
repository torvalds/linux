/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Benjamin Gaignard <benjamin.gaignard@st.com>
 *          Fabien Dessenne <fabien.dessenne@st.com>
 *          for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>

#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "sti_compositor.h"
#include "sti_gdp.h"
#include "sti_plane.h"
#include "sti_vtg.h"

#define ALPHASWITCH     BIT(6)
#define ENA_COLOR_FILL  BIT(8)
#define BIGNOTLITTLE    BIT(23)
#define WAIT_NEXT_VSYNC BIT(31)

/* GDP color formats */
#define GDP_RGB565      0x00
#define GDP_RGB888      0x01
#define GDP_RGB888_32   0x02
#define GDP_XBGR8888    (GDP_RGB888_32 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB8565    0x04
#define GDP_ARGB8888    0x05
#define GDP_ABGR8888    (GDP_ARGB8888 | BIGNOTLITTLE | ALPHASWITCH)
#define GDP_ARGB1555    0x06
#define GDP_ARGB4444    0x07
#define GDP_CLUT8       0x0B
#define GDP_YCBR888     0x10
#define GDP_YCBR422R    0x12
#define GDP_AYCBR8888   0x15

#define GAM_GDP_CTL_OFFSET      0x00
#define GAM_GDP_AGC_OFFSET      0x04
#define GAM_GDP_VPO_OFFSET      0x0C
#define GAM_GDP_VPS_OFFSET      0x10
#define GAM_GDP_PML_OFFSET      0x14
#define GAM_GDP_PMP_OFFSET      0x18
#define GAM_GDP_SIZE_OFFSET     0x1C
#define GAM_GDP_NVN_OFFSET      0x24
#define GAM_GDP_KEY1_OFFSET     0x28
#define GAM_GDP_KEY2_OFFSET     0x2C
#define GAM_GDP_PPT_OFFSET      0x34
#define GAM_GDP_CML_OFFSET      0x3C
#define GAM_GDP_MST_OFFSET      0x68

#define GAM_GDP_ALPHARANGE_255  BIT(5)
#define GAM_GDP_AGC_FULL_RANGE  0x00808080
#define GAM_GDP_PPT_IGNORE      (BIT(1) | BIT(0))
#define GAM_GDP_SIZE_MAX        0x7FF

#define GDP_NODE_NB_BANK        2
#define GDP_NODE_PER_FIELD      2

struct sti_gdp_node {
	u32 gam_gdp_ctl;
	u32 gam_gdp_agc;
	u32 reserved1;
	u32 gam_gdp_vpo;
	u32 gam_gdp_vps;
	u32 gam_gdp_pml;
	u32 gam_gdp_pmp;
	u32 gam_gdp_size;
	u32 reserved2;
	u32 gam_gdp_nvn;
	u32 gam_gdp_key1;
	u32 gam_gdp_key2;
	u32 reserved3;
	u32 gam_gdp_ppt;
	u32 reserved4;
	u32 gam_gdp_cml;
};

struct sti_gdp_node_list {
	struct sti_gdp_node *top_field;
	dma_addr_t top_field_paddr;
	struct sti_gdp_node *btm_field;
	dma_addr_t btm_field_paddr;
};

/**
 * STI GDP structure
 *
 * @sti_plane:          sti_plane structure
 * @dev:                driver device
 * @regs:               gdp registers
 * @clk_pix:            pixel clock for the current gdp
 * @clk_main_parent:    gdp parent clock if main path used
 * @clk_aux_parent:     gdp parent clock if aux path used
 * @vtg_field_nb:       callback for VTG FIELD (top or bottom) notification
 * @is_curr_top:        true if the current node processed is the top field
 * @node_list:          array of node list
 */
struct sti_gdp {
	struct sti_plane plane;
	struct device *dev;
	void __iomem *regs;
	struct clk *clk_pix;
	struct clk *clk_main_parent;
	struct clk *clk_aux_parent;
	struct notifier_block vtg_field_nb;
	bool is_curr_top;
	struct sti_gdp_node_list node_list[GDP_NODE_NB_BANK];
};

#define to_sti_gdp(x) container_of(x, struct sti_gdp, plane)

static const uint32_t gdp_supported_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_AYUV,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_C8,
};

static int sti_gdp_fourcc2format(int fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_XRGB8888:
		return GDP_RGB888_32;
	case DRM_FORMAT_XBGR8888:
		return GDP_XBGR8888;
	case DRM_FORMAT_ARGB8888:
		return GDP_ARGB8888;
	case DRM_FORMAT_ABGR8888:
		return GDP_ABGR8888;
	case DRM_FORMAT_ARGB4444:
		return GDP_ARGB4444;
	case DRM_FORMAT_ARGB1555:
		return GDP_ARGB1555;
	case DRM_FORMAT_RGB565:
		return GDP_RGB565;
	case DRM_FORMAT_RGB888:
		return GDP_RGB888;
	case DRM_FORMAT_AYUV:
		return GDP_AYCBR8888;
	case DRM_FORMAT_YUV444:
		return GDP_YCBR888;
	case DRM_FORMAT_VYUY:
		return GDP_YCBR422R;
	case DRM_FORMAT_C8:
		return GDP_CLUT8;
	}
	return -1;
}

static int sti_gdp_get_alpharange(int format)
{
	switch (format) {
	case GDP_ARGB8565:
	case GDP_ARGB8888:
	case GDP_AYCBR8888:
	case GDP_ABGR8888:
		return GAM_GDP_ALPHARANGE_255;
	}
	return 0;
}

/**
 * sti_gdp_get_free_nodes
 * @gdp: gdp pointer
 *
 * Look for a GDP node list that is not currently read by the HW.
 *
 * RETURNS:
 * Pointer to the free GDP node list
 */
static struct sti_gdp_node_list *sti_gdp_get_free_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	unsigned int i;

	hw_nvn = readl(gdp->regs + GAM_GDP_NVN_OFFSET);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn != gdp->node_list[i].btm_field_paddr) &&
		    (hw_nvn != gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

	/* in hazardious cases restart with the first node */
	DRM_ERROR("inconsistent NVN for %s: 0x%08X\n",
			sti_plane_to_str(&gdp->plane), hw_nvn);

end:
	return &gdp->node_list[0];
}

/**
 * sti_gdp_get_current_nodes
 * @gdp: gdp pointer
 *
 * Look for GDP nodes that are currently read by the HW.
 *
 * RETURNS:
 * Pointer to the current GDP node list
 */
static
struct sti_gdp_node_list *sti_gdp_get_current_nodes(struct sti_gdp *gdp)
{
	int hw_nvn;
	unsigned int i;

	hw_nvn = readl(gdp->regs + GAM_GDP_NVN_OFFSET);
	if (!hw_nvn)
		goto end;

	for (i = 0; i < GDP_NODE_NB_BANK; i++)
		if ((hw_nvn == gdp->node_list[i].btm_field_paddr) ||
				(hw_nvn == gdp->node_list[i].top_field_paddr))
			return &gdp->node_list[i];

end:
	DRM_DEBUG_DRIVER("Warning, NVN 0x%08X for %s does not match any node\n",
				hw_nvn, sti_plane_to_str(&gdp->plane));

	return NULL;
}

/**
 * sti_gdp_disable
 * @gdp: gdp pointer
 *
 * Disable a GDP.
 */
static void sti_gdp_disable(struct sti_gdp *gdp)
{
	struct drm_plane *drm_plane = &gdp->plane.drm_plane;
	struct sti_mixer *mixer = to_sti_mixer(drm_plane->crtc);
	struct sti_compositor *compo = dev_get_drvdata(gdp->dev);
	unsigned int i;

	DRM_DEBUG_DRIVER("%s\n", sti_plane_to_str(&gdp->plane));

	/* Set the nodes as 'to be ignored on mixer' */
	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		gdp->node_list[i].top_field->gam_gdp_ppt |= GAM_GDP_PPT_IGNORE;
		gdp->node_list[i].btm_field->gam_gdp_ppt |= GAM_GDP_PPT_IGNORE;
	}

	if (sti_vtg_unregister_client(mixer->id == STI_MIXER_MAIN ?
			compo->vtg_main : compo->vtg_aux, &gdp->vtg_field_nb))
		DRM_DEBUG_DRIVER("Warning: cannot unregister VTG notifier\n");

	if (gdp->clk_pix)
		clk_disable_unprepare(gdp->clk_pix);

	gdp->plane.status = STI_PLANE_DISABLED;
}

/**
 * sti_gdp_field_cb
 * @nb: notifier block
 * @event: event message
 * @data: private data
 *
 * Handle VTG top field and bottom field event.
 *
 * RETURNS:
 * 0 on success.
 */
int sti_gdp_field_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct sti_gdp *gdp = container_of(nb, struct sti_gdp, vtg_field_nb);

	if (gdp->plane.status == STI_PLANE_FLUSHING) {
		/* disable need to be synchronize on vsync event */
		DRM_DEBUG_DRIVER("Vsync event received => disable %s\n",
				 sti_plane_to_str(&gdp->plane));

		sti_gdp_disable(gdp);
	}

	switch (event) {
	case VTG_TOP_FIELD_EVENT:
		gdp->is_curr_top = true;
		break;
	case VTG_BOTTOM_FIELD_EVENT:
		gdp->is_curr_top = false;
		break;
	default:
		DRM_ERROR("unsupported event: %lu\n", event);
		break;
	}

	return 0;
}

static void sti_gdp_init(struct sti_gdp *gdp)
{
	struct device_node *np = gdp->dev->of_node;
	dma_addr_t dma_addr;
	void *base;
	unsigned int i, size;

	/* Allocate all the nodes within a single memory page */
	size = sizeof(struct sti_gdp_node) *
	    GDP_NODE_PER_FIELD * GDP_NODE_NB_BANK;
	base = dma_alloc_writecombine(gdp->dev,
				      size, &dma_addr, GFP_KERNEL | GFP_DMA);

	if (!base) {
		DRM_ERROR("Failed to allocate memory for GDP node\n");
		return;
	}
	memset(base, 0, size);

	for (i = 0; i < GDP_NODE_NB_BANK; i++) {
		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].top_field = base;
		gdp->node_list[i].top_field_paddr = dma_addr;

		DRM_DEBUG_DRIVER("node[%d].top_field=%p\n", i, base);
		base += sizeof(struct sti_gdp_node);
		dma_addr += sizeof(struct sti_gdp_node);

		if (dma_addr & 0xF) {
			DRM_ERROR("Mem alignment failed\n");
			return;
		}
		gdp->node_list[i].btm_field = base;
		gdp->node_list[i].btm_field_paddr = dma_addr;
		DRM_DEBUG_DRIVER("node[%d].btm_field=%p\n", i, base);
		base += sizeof(struct sti_gdp_node);
		dma_addr += sizeof(struct sti_gdp_node);
	}

	if (of_device_is_compatible(np, "st,stih407-compositor")) {
		/* GDP of STiH407 chip have its own pixel clock */
		char *clk_name;

		switch (gdp->plane.desc) {
		case STI_GDP_0:
			clk_name = "pix_gdp1";
			break;
		case STI_GDP_1:
			clk_name = "pix_gdp2";
			break;
		case STI_GDP_2:
			clk_name = "pix_gdp3";
			break;
		case STI_GDP_3:
			clk_name = "pix_gdp4";
			break;
		default:
			DRM_ERROR("GDP id not recognized\n");
			return;
		}

		gdp->clk_pix = devm_clk_get(gdp->dev, clk_name);
		if (IS_ERR(gdp->clk_pix))
			DRM_ERROR("Cannot get %s clock\n", clk_name);

		gdp->clk_main_parent = devm_clk_get(gdp->dev, "main_parent");
		if (IS_ERR(gdp->clk_main_parent))
			DRM_ERROR("Cannot get main_parent clock\n");

		gdp->clk_aux_parent = devm_clk_get(gdp->dev, "aux_parent");
		if (IS_ERR(gdp->clk_aux_parent))
			DRM_ERROR("Cannot get aux_parent clock\n");
	}
}

static void sti_gdp_atomic_update(struct drm_plane *drm_plane,
				  struct drm_plane_state *oldstate)
{
	struct drm_plane_state *state = drm_plane->state;
	struct sti_plane *plane = to_sti_plane(drm_plane);
	struct sti_gdp *gdp = to_sti_gdp(plane);
	struct drm_crtc *crtc = state->crtc;
	struct sti_compositor *compo = dev_get_drvdata(gdp->dev);
	struct drm_framebuffer *fb =  state->fb;
	bool first_prepare = plane->status == STI_PLANE_DISABLED ? true : false;
	struct sti_mixer *mixer;
	struct drm_display_mode *mode;
	int dst_x, dst_y, dst_w, dst_h;
	int src_x, src_y, src_w, src_h;
	struct drm_gem_cma_object *cma_obj;
	struct sti_gdp_node_list *list;
	struct sti_gdp_node_list *curr_list;
	struct sti_gdp_node *top_field, *btm_field;
	u32 dma_updated_top;
	u32 dma_updated_btm;
	int format;
	unsigned int depth, bpp;
	u32 ydo, xdo, yds, xds;
	int res;

	/* Manage the case where crtc is null (disabled) */
	if (!crtc)
		return;

	mixer = to_sti_mixer(crtc);
	mode = &crtc->mode;
	dst_x = state->crtc_x;
	dst_y = state->crtc_y;
	dst_w = clamp_val(state->crtc_w, 0, mode->crtc_hdisplay - dst_x);
	dst_h = clamp_val(state->crtc_h, 0, mode->crtc_vdisplay - dst_y);
	/* src_x are in 16.16 format */
	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;

	DRM_DEBUG_KMS("CRTC:%d (%s) drm plane:%d (%s)\n",
		      crtc->base.id, sti_mixer_to_str(mixer),
		      drm_plane->base.id, sti_plane_to_str(plane));
	DRM_DEBUG_KMS("%s dst=(%dx%d)@(%d,%d) - src=(%dx%d)@(%d,%d)\n",
		      sti_plane_to_str(plane),
		      dst_w, dst_h, dst_x, dst_y,
		      src_w, src_h, src_x, src_y);

	list = sti_gdp_get_free_nodes(gdp);
	top_field = list->top_field;
	btm_field = list->btm_field;

	dev_dbg(gdp->dev, "%s %s top_node:0x%p btm_node:0x%p\n", __func__,
		sti_plane_to_str(plane), top_field, btm_field);

	/* build the top field */
	top_field->gam_gdp_agc = GAM_GDP_AGC_FULL_RANGE;
	top_field->gam_gdp_ctl = WAIT_NEXT_VSYNC;
	format = sti_gdp_fourcc2format(fb->pixel_format);
	if (format == -1) {
		DRM_ERROR("Format not supported by GDP %.4s\n",
			  (char *)&fb->pixel_format);
		return;
	}
	top_field->gam_gdp_ctl |= format;
	top_field->gam_gdp_ctl |= sti_gdp_get_alpharange(format);
	top_field->gam_gdp_ppt &= ~GAM_GDP_PPT_IGNORE;

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	if (!cma_obj) {
		DRM_ERROR("Can't get CMA GEM object for fb\n");
		return;
	}

	DRM_DEBUG_DRIVER("drm FB:%d format:%.4s phys@:0x%lx\n", fb->base.id,
			 (char *)&fb->pixel_format,
			 (unsigned long)cma_obj->paddr);

	/* pixel memory location */
	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	top_field->gam_gdp_pml = (u32)cma_obj->paddr + fb->offsets[0];
	top_field->gam_gdp_pml += src_x * (bpp >> 3);
	top_field->gam_gdp_pml += src_y * fb->pitches[0];

	/* input parameters */
	top_field->gam_gdp_pmp = fb->pitches[0];
	top_field->gam_gdp_size = clamp_val(src_h, 0, GAM_GDP_SIZE_MAX) << 16 |
				  clamp_val(src_w, 0, GAM_GDP_SIZE_MAX);

	/* output parameters */
	ydo = sti_vtg_get_line_number(*mode, dst_y);
	yds = sti_vtg_get_line_number(*mode, dst_y + dst_h - 1);
	xdo = sti_vtg_get_pixel_number(*mode, dst_x);
	xds = sti_vtg_get_pixel_number(*mode, dst_x + dst_w - 1);
	top_field->gam_gdp_vpo = (ydo << 16) | xdo;
	top_field->gam_gdp_vps = (yds << 16) | xds;

	/* Same content and chained together */
	memcpy(btm_field, top_field, sizeof(*btm_field));
	top_field->gam_gdp_nvn = list->btm_field_paddr;
	btm_field->gam_gdp_nvn = list->top_field_paddr;

	/* Interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		btm_field->gam_gdp_pml = top_field->gam_gdp_pml +
					 fb->pitches[0];

	if (first_prepare) {
		/* Register gdp callback */
		if (sti_vtg_register_client(mixer->id == STI_MIXER_MAIN ?
				compo->vtg_main : compo->vtg_aux,
				&gdp->vtg_field_nb, mixer->id)) {
			DRM_ERROR("Cannot register VTG notifier\n");
			return;
		}

		/* Set and enable gdp clock */
		if (gdp->clk_pix) {
			struct clk *clkp;
			int rate = mode->clock * 1000;

			/* According to the mixer used, the gdp pixel clock
			 * should have a different parent clock. */
			if (mixer->id == STI_MIXER_MAIN)
				clkp = gdp->clk_main_parent;
			else
				clkp = gdp->clk_aux_parent;

			if (clkp)
				clk_set_parent(gdp->clk_pix, clkp);

			res = clk_set_rate(gdp->clk_pix, rate);
			if (res < 0) {
				DRM_ERROR("Cannot set rate (%dHz) for gdp\n",
					  rate);
				return;
			}

			if (clk_prepare_enable(gdp->clk_pix)) {
				DRM_ERROR("Failed to prepare/enable gdp\n");
				return;
			}
		}
	}

	/* Update the NVN field of the 'right' field of the current GDP node
	 * (being used by the HW) with the address of the updated ('free') top
	 * field GDP node.
	 * - In interlaced mode the 'right' field is the bottom field as we
	 *   update frames starting from their top field
	 * - In progressive mode, we update both bottom and top fields which
	 *   are equal nodes.
	 * At the next VSYNC, the updated node list will be used by the HW.
	 */
	curr_list = sti_gdp_get_current_nodes(gdp);
	dma_updated_top = list->top_field_paddr;
	dma_updated_btm = list->btm_field_paddr;

	dev_dbg(gdp->dev, "Current NVN:0x%X\n",
		readl(gdp->regs + GAM_GDP_NVN_OFFSET));
	dev_dbg(gdp->dev, "Posted buff: %lx current buff: %x\n",
		(unsigned long)cma_obj->paddr,
		readl(gdp->regs + GAM_GDP_PML_OFFSET));

	if (!curr_list) {
		/* First update or invalid node should directly write in the
		 * hw register */
		DRM_DEBUG_DRIVER("%s first update (or invalid node)",
				 sti_plane_to_str(plane));

		writel(gdp->is_curr_top ?
				dma_updated_btm : dma_updated_top,
				gdp->regs + GAM_GDP_NVN_OFFSET);
		goto end;
	}

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		if (gdp->is_curr_top) {
			/* Do not update in the middle of the frame, but
			 * postpone the update after the bottom field has
			 * been displayed */
			curr_list->btm_field->gam_gdp_nvn = dma_updated_top;
		} else {
			/* Direct update to avoid one frame delay */
			writel(dma_updated_top,
			       gdp->regs + GAM_GDP_NVN_OFFSET);
		}
	} else {
		/* Direct update for progressive to avoid one frame delay */
		writel(dma_updated_top, gdp->regs + GAM_GDP_NVN_OFFSET);
	}

end:
	plane->status = STI_PLANE_UPDATED;
}

static void sti_gdp_atomic_disable(struct drm_plane *drm_plane,
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

static const struct drm_plane_helper_funcs sti_gdp_helpers_funcs = {
	.atomic_update = sti_gdp_atomic_update,
	.atomic_disable = sti_gdp_atomic_disable,
};

struct drm_plane *sti_gdp_create(struct drm_device *drm_dev,
				 struct device *dev, int desc,
				 void __iomem *baseaddr,
				 unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	struct sti_gdp *gdp;
	int res;

	gdp = devm_kzalloc(dev, sizeof(*gdp), GFP_KERNEL);
	if (!gdp) {
		DRM_ERROR("Failed to allocate memory for GDP\n");
		return NULL;
	}

	gdp->dev = dev;
	gdp->regs = baseaddr;
	gdp->plane.desc = desc;
	gdp->plane.status = STI_PLANE_DISABLED;

	gdp->vtg_field_nb.notifier_call = sti_gdp_field_cb;

	sti_gdp_init(gdp);

	res = drm_universal_plane_init(drm_dev, &gdp->plane.drm_plane,
				       possible_crtcs,
				       &sti_plane_helpers_funcs,
				       gdp_supported_formats,
				       ARRAY_SIZE(gdp_supported_formats),
				       type);
	if (res) {
		DRM_ERROR("Failed to initialize universal plane\n");
		goto err;
	}

	drm_plane_helper_add(&gdp->plane.drm_plane, &sti_gdp_helpers_funcs);

	sti_plane_init_property(&gdp->plane, type);

	return &gdp->plane.drm_plane;

err:
	devm_kfree(dev, gdp);
	return NULL;
}
