/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_address.h>
#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "zx_drm_drv.h"
#include "zx_plane.h"
#include "zx_vou.h"
#include "zx_vou_regs.h"

#define GL_NUM	2
#define VL_NUM	3

enum vou_chn_type {
	VOU_CHN_MAIN,
	VOU_CHN_AUX,
};

struct zx_crtc_regs {
	u32 fir_active;
	u32 fir_htiming;
	u32 fir_vtiming;
	u32 timing_shift;
	u32 timing_pi_shift;
};

static const struct zx_crtc_regs main_crtc_regs = {
	.fir_active = FIR_MAIN_ACTIVE,
	.fir_htiming = FIR_MAIN_H_TIMING,
	.fir_vtiming = FIR_MAIN_V_TIMING,
	.timing_shift = TIMING_MAIN_SHIFT,
	.timing_pi_shift = TIMING_MAIN_PI_SHIFT,
};

static const struct zx_crtc_regs aux_crtc_regs = {
	.fir_active = FIR_AUX_ACTIVE,
	.fir_htiming = FIR_AUX_H_TIMING,
	.fir_vtiming = FIR_AUX_V_TIMING,
	.timing_shift = TIMING_AUX_SHIFT,
	.timing_pi_shift = TIMING_AUX_PI_SHIFT,
};

struct zx_crtc_bits {
	u32 polarity_mask;
	u32 polarity_shift;
	u32 int_frame_mask;
	u32 tc_enable;
	u32 gl_enable;
};

static const struct zx_crtc_bits main_crtc_bits = {
	.polarity_mask = MAIN_POL_MASK,
	.polarity_shift = MAIN_POL_SHIFT,
	.int_frame_mask = TIMING_INT_MAIN_FRAME,
	.tc_enable = MAIN_TC_EN,
	.gl_enable = OSD_CTRL0_GL0_EN,
};

static const struct zx_crtc_bits aux_crtc_bits = {
	.polarity_mask = AUX_POL_MASK,
	.polarity_shift = AUX_POL_SHIFT,
	.int_frame_mask = TIMING_INT_AUX_FRAME,
	.tc_enable = AUX_TC_EN,
	.gl_enable = OSD_CTRL0_GL1_EN,
};

struct zx_crtc {
	struct drm_crtc crtc;
	struct drm_plane *primary;
	struct zx_vou_hw *vou;
	void __iomem *chnreg;
	const struct zx_crtc_regs *regs;
	const struct zx_crtc_bits *bits;
	enum vou_chn_type chn_type;
	struct clk *pixclk;
};

#define to_zx_crtc(x) container_of(x, struct zx_crtc, crtc)

struct zx_vou_hw {
	struct device *dev;
	void __iomem *osd;
	void __iomem *timing;
	void __iomem *vouctl;
	void __iomem *otfppu;
	void __iomem *dtrc;
	struct clk *axi_clk;
	struct clk *ppu_clk;
	struct clk *main_clk;
	struct clk *aux_clk;
	struct zx_crtc *main_crtc;
	struct zx_crtc *aux_crtc;
};

static inline struct zx_vou_hw *crtc_to_vou(struct drm_crtc *crtc)
{
	struct zx_crtc *zcrtc = to_zx_crtc(crtc);

	return zcrtc->vou;
}

void vou_inf_enable(const struct vou_inf *inf, struct drm_crtc *crtc)
{
	struct zx_crtc *zcrtc = to_zx_crtc(crtc);
	struct zx_vou_hw *vou = zcrtc->vou;
	bool is_main = zcrtc->chn_type == VOU_CHN_MAIN;
	u32 data_sel_shift = inf->id << 1;

	/* Select data format */
	zx_writel_mask(vou->vouctl + VOU_INF_DATA_SEL, 0x3 << data_sel_shift,
		       inf->data_sel << data_sel_shift);

	/* Select channel */
	zx_writel_mask(vou->vouctl + VOU_INF_CH_SEL, 0x1 << inf->id,
		       zcrtc->chn_type << inf->id);

	/* Select interface clocks */
	zx_writel_mask(vou->vouctl + VOU_CLK_SEL, inf->clocks_sel_bits,
		       is_main ? 0 : inf->clocks_sel_bits);

	/* Enable interface clocks */
	zx_writel_mask(vou->vouctl + VOU_CLK_EN, inf->clocks_en_bits,
		       inf->clocks_en_bits);

	/* Enable the device */
	zx_writel_mask(vou->vouctl + VOU_INF_EN, 1 << inf->id, 1 << inf->id);
}

void vou_inf_disable(const struct vou_inf *inf, struct drm_crtc *crtc)
{
	struct zx_vou_hw *vou = crtc_to_vou(crtc);

	/* Disable the device */
	zx_writel_mask(vou->vouctl + VOU_INF_EN, 1 << inf->id, 0);

	/* Disable interface clocks */
	zx_writel_mask(vou->vouctl + VOU_CLK_EN, inf->clocks_en_bits, 0);
}

static inline void vou_chn_set_update(struct zx_crtc *zcrtc)
{
	zx_writel(zcrtc->chnreg + CHN_UPDATE, 1);
}

static void zx_crtc_enable(struct drm_crtc *crtc)
{
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct zx_crtc *zcrtc = to_zx_crtc(crtc);
	struct zx_vou_hw *vou = zcrtc->vou;
	const struct zx_crtc_regs *regs = zcrtc->regs;
	const struct zx_crtc_bits *bits = zcrtc->bits;
	struct videomode vm;
	u32 pol = 0;
	u32 val;
	int ret;

	drm_display_mode_to_videomode(mode, &vm);

	/* Set up timing parameters */
	val = V_ACTIVE(vm.vactive - 1);
	val |= H_ACTIVE(vm.hactive - 1);
	zx_writel(vou->timing + regs->fir_active, val);

	val = SYNC_WIDE(vm.hsync_len - 1);
	val |= BACK_PORCH(vm.hback_porch - 1);
	val |= FRONT_PORCH(vm.hfront_porch - 1);
	zx_writel(vou->timing + regs->fir_htiming, val);

	val = SYNC_WIDE(vm.vsync_len - 1);
	val |= BACK_PORCH(vm.vback_porch - 1);
	val |= FRONT_PORCH(vm.vfront_porch - 1);
	zx_writel(vou->timing + regs->fir_vtiming, val);

	/* Set up polarities */
	if (vm.flags & DISPLAY_FLAGS_VSYNC_LOW)
		pol |= 1 << POL_VSYNC_SHIFT;
	if (vm.flags & DISPLAY_FLAGS_HSYNC_LOW)
		pol |= 1 << POL_HSYNC_SHIFT;

	zx_writel_mask(vou->timing + TIMING_CTRL, bits->polarity_mask,
		       pol << bits->polarity_shift);

	/* Setup SHIFT register by following what ZTE BSP does */
	zx_writel(vou->timing + regs->timing_shift, H_SHIFT_VAL);
	zx_writel(vou->timing + regs->timing_pi_shift, H_PI_SHIFT_VAL);

	/* Enable TIMING_CTRL */
	zx_writel_mask(vou->timing + TIMING_TC_ENABLE, bits->tc_enable,
		       bits->tc_enable);

	/* Configure channel screen size */
	zx_writel_mask(zcrtc->chnreg + CHN_CTRL1, CHN_SCREEN_W_MASK,
		       vm.hactive << CHN_SCREEN_W_SHIFT);
	zx_writel_mask(zcrtc->chnreg + CHN_CTRL1, CHN_SCREEN_H_MASK,
		       vm.vactive << CHN_SCREEN_H_SHIFT);

	/* Update channel */
	vou_chn_set_update(zcrtc);

	/* Enable channel */
	zx_writel_mask(zcrtc->chnreg + CHN_CTRL0, CHN_ENABLE, CHN_ENABLE);

	/* Enable Graphic Layer */
	zx_writel_mask(vou->osd + OSD_CTRL0, bits->gl_enable,
		       bits->gl_enable);

	drm_crtc_vblank_on(crtc);

	ret = clk_set_rate(zcrtc->pixclk, mode->clock * 1000);
	if (ret) {
		DRM_DEV_ERROR(vou->dev, "failed to set pixclk rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(zcrtc->pixclk);
	if (ret)
		DRM_DEV_ERROR(vou->dev, "failed to enable pixclk: %d\n", ret);
}

static void zx_crtc_disable(struct drm_crtc *crtc)
{
	struct zx_crtc *zcrtc = to_zx_crtc(crtc);
	const struct zx_crtc_bits *bits = zcrtc->bits;
	struct zx_vou_hw *vou = zcrtc->vou;

	clk_disable_unprepare(zcrtc->pixclk);

	drm_crtc_vblank_off(crtc);

	/* Disable Graphic Layer */
	zx_writel_mask(vou->osd + OSD_CTRL0, bits->gl_enable, 0);

	/* Disable channel */
	zx_writel_mask(zcrtc->chnreg + CHN_CTRL0, CHN_ENABLE, 0);

	/* Disable TIMING_CTRL */
	zx_writel_mask(vou->timing + TIMING_TC_ENABLE, bits->tc_enable, 0);
}

static void zx_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (!event)
		return;

	crtc->state->event = NULL;

	spin_lock_irq(&crtc->dev->event_lock);
	if (drm_crtc_vblank_get(crtc) == 0)
		drm_crtc_arm_vblank_event(crtc, event);
	else
		drm_crtc_send_vblank_event(crtc, event);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static const struct drm_crtc_helper_funcs zx_crtc_helper_funcs = {
	.enable = zx_crtc_enable,
	.disable = zx_crtc_disable,
	.atomic_flush = zx_crtc_atomic_flush,
};

static const struct drm_crtc_funcs zx_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static int zx_crtc_init(struct drm_device *drm, struct zx_vou_hw *vou,
			enum vou_chn_type chn_type)
{
	struct device *dev = vou->dev;
	struct zx_layer_data data;
	struct zx_crtc *zcrtc;
	int ret;

	zcrtc = devm_kzalloc(dev, sizeof(*zcrtc), GFP_KERNEL);
	if (!zcrtc)
		return -ENOMEM;

	zcrtc->vou = vou;
	zcrtc->chn_type = chn_type;

	if (chn_type == VOU_CHN_MAIN) {
		data.layer = vou->osd + MAIN_GL_OFFSET;
		data.csc = vou->osd + MAIN_CSC_OFFSET;
		data.hbsc = vou->osd + MAIN_HBSC_OFFSET;
		data.rsz = vou->otfppu + MAIN_RSZ_OFFSET;
		zcrtc->chnreg = vou->osd + OSD_MAIN_CHN;
		zcrtc->regs = &main_crtc_regs;
		zcrtc->bits = &main_crtc_bits;
	} else {
		data.layer = vou->osd + AUX_GL_OFFSET;
		data.csc = vou->osd + AUX_CSC_OFFSET;
		data.hbsc = vou->osd + AUX_HBSC_OFFSET;
		data.rsz = vou->otfppu + AUX_RSZ_OFFSET;
		zcrtc->chnreg = vou->osd + OSD_AUX_CHN;
		zcrtc->regs = &aux_crtc_regs;
		zcrtc->bits = &aux_crtc_bits;
	}

	zcrtc->pixclk = devm_clk_get(dev, (chn_type == VOU_CHN_MAIN) ?
					  "main_wclk" : "aux_wclk");
	if (IS_ERR(zcrtc->pixclk)) {
		ret = PTR_ERR(zcrtc->pixclk);
		DRM_DEV_ERROR(dev, "failed to get pix clk: %d\n", ret);
		return ret;
	}

	zcrtc->primary = zx_plane_init(drm, dev, &data, DRM_PLANE_TYPE_PRIMARY);
	if (IS_ERR(zcrtc->primary)) {
		ret = PTR_ERR(zcrtc->primary);
		DRM_DEV_ERROR(dev, "failed to init primary plane: %d\n", ret);
		return ret;
	}

	ret = drm_crtc_init_with_planes(drm, &zcrtc->crtc, zcrtc->primary, NULL,
					&zx_crtc_funcs, NULL);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init drm crtc: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(&zcrtc->crtc, &zx_crtc_helper_funcs);

	if (chn_type == VOU_CHN_MAIN)
		vou->main_crtc = zcrtc;
	else
		vou->aux_crtc = zcrtc;

	return 0;
}

static inline struct drm_crtc *zx_find_crtc(struct drm_device *drm, int pipe)
{
	struct drm_crtc *crtc;

	list_for_each_entry(crtc, &drm->mode_config.crtc_list, head)
		if (crtc->index == pipe)
			return crtc;

	return NULL;
}

int zx_vou_enable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct drm_crtc *crtc;
	struct zx_crtc *zcrtc;
	struct zx_vou_hw *vou;
	u32 int_frame_mask;

	crtc = zx_find_crtc(drm, pipe);
	if (!crtc)
		return 0;

	vou = crtc_to_vou(crtc);
	zcrtc = to_zx_crtc(crtc);
	int_frame_mask = zcrtc->bits->int_frame_mask;

	zx_writel_mask(vou->timing + TIMING_INT_CTRL, int_frame_mask,
		       int_frame_mask);

	return 0;
}

void zx_vou_disable_vblank(struct drm_device *drm, unsigned int pipe)
{
	struct drm_crtc *crtc;
	struct zx_crtc *zcrtc;
	struct zx_vou_hw *vou;

	crtc = zx_find_crtc(drm, pipe);
	if (!crtc)
		return;

	vou = crtc_to_vou(crtc);
	zcrtc = to_zx_crtc(crtc);

	zx_writel_mask(vou->timing + TIMING_INT_CTRL,
		       zcrtc->bits->int_frame_mask, 0);
}

static irqreturn_t vou_irq_handler(int irq, void *dev_id)
{
	struct zx_vou_hw *vou = dev_id;
	u32 state;

	/* Handle TIMING_CTRL frame interrupts */
	state = zx_readl(vou->timing + TIMING_INT_STATE);
	zx_writel(vou->timing + TIMING_INT_STATE, state);

	if (state & TIMING_INT_MAIN_FRAME)
		drm_crtc_handle_vblank(&vou->main_crtc->crtc);

	if (state & TIMING_INT_AUX_FRAME)
		drm_crtc_handle_vblank(&vou->aux_crtc->crtc);

	/* Handle OSD interrupts */
	state = zx_readl(vou->osd + OSD_INT_STA);
	zx_writel(vou->osd + OSD_INT_CLRSTA, state);

	if (state & OSD_INT_MAIN_UPT) {
		vou_chn_set_update(vou->main_crtc);
		zx_plane_set_update(vou->main_crtc->primary);
	}

	if (state & OSD_INT_AUX_UPT) {
		vou_chn_set_update(vou->aux_crtc);
		zx_plane_set_update(vou->aux_crtc->primary);
	}

	if (state & OSD_INT_ERROR)
		DRM_DEV_ERROR(vou->dev, "OSD ERROR: 0x%08x!\n", state);

	return IRQ_HANDLED;
}

static void vou_dtrc_init(struct zx_vou_hw *vou)
{
	/* Clear bit for bypass by ID */
	zx_writel_mask(vou->dtrc + DTRC_DETILE_CTRL,
		       TILE2RASTESCAN_BYPASS_MODE, 0);

	/* Select ARIDR mode */
	zx_writel_mask(vou->dtrc + DTRC_DETILE_CTRL, DETILE_ARIDR_MODE_MASK,
		       DETILE_ARID_IN_ARIDR);

	/* Bypass decompression for both frames */
	zx_writel_mask(vou->dtrc + DTRC_F0_CTRL, DTRC_DECOMPRESS_BYPASS,
		       DTRC_DECOMPRESS_BYPASS);
	zx_writel_mask(vou->dtrc + DTRC_F1_CTRL, DTRC_DECOMPRESS_BYPASS,
		       DTRC_DECOMPRESS_BYPASS);

	/* Set up ARID register */
	zx_writel(vou->dtrc + DTRC_ARID, DTRC_ARID3(0xf) | DTRC_ARID2(0xe) |
		  DTRC_ARID1(0xf) | DTRC_ARID0(0xe));
}

static void vou_hw_init(struct zx_vou_hw *vou)
{
	/* Set GL0 to main channel and GL1 to aux channel */
	zx_writel_mask(vou->osd + OSD_CTRL0, OSD_CTRL0_GL0_SEL, 0);
	zx_writel_mask(vou->osd + OSD_CTRL0, OSD_CTRL0_GL1_SEL,
		       OSD_CTRL0_GL1_SEL);

	/* Release reset for all VOU modules */
	zx_writel(vou->vouctl + VOU_SOFT_RST, ~0);

	/* Select main clock for GL0 and aux clock for GL1 module */
	zx_writel_mask(vou->vouctl + VOU_CLK_SEL, VOU_CLK_GL0_SEL, 0);
	zx_writel_mask(vou->vouctl + VOU_CLK_SEL, VOU_CLK_GL1_SEL,
		       VOU_CLK_GL1_SEL);

	/* Enable clock auto-gating for all VOU modules */
	zx_writel(vou->vouctl + VOU_CLK_REQEN, ~0);

	/* Enable all VOU module clocks */
	zx_writel(vou->vouctl + VOU_CLK_EN, ~0);

	/* Clear both OSD and TIMING_CTRL interrupt state */
	zx_writel(vou->osd + OSD_INT_CLRSTA, ~0);
	zx_writel(vou->timing + TIMING_INT_STATE, ~0);

	/* Enable OSD and TIMING_CTRL interrrupts */
	zx_writel(vou->osd + OSD_INT_MSK, OSD_INT_ENABLE);
	zx_writel(vou->timing + TIMING_INT_CTRL, TIMING_INT_ENABLE);

	/* Select GPC as input to gl/vl scaler as a sane default setting */
	zx_writel(vou->otfppu + OTFPPU_RSZ_DATA_SOURCE, 0x2a);

	/*
	 * Needs to reset channel and layer logic per frame when frame starts
	 * to get VOU work properly.
	 */
	zx_writel_mask(vou->osd + OSD_RST_CLR, RST_PER_FRAME, RST_PER_FRAME);

	vou_dtrc_init(vou);
}

static int zx_crtc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct zx_vou_hw *vou;
	struct resource *res;
	int irq;
	int ret;

	vou = devm_kzalloc(dev, sizeof(*vou), GFP_KERNEL);
	if (!vou)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "osd");
	vou->osd = devm_ioremap_resource(dev, res);
	if (IS_ERR(vou->osd)) {
		ret = PTR_ERR(vou->osd);
		DRM_DEV_ERROR(dev, "failed to remap osd region: %d\n", ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "timing_ctrl");
	vou->timing = devm_ioremap_resource(dev, res);
	if (IS_ERR(vou->timing)) {
		ret = PTR_ERR(vou->timing);
		DRM_DEV_ERROR(dev, "failed to remap timing_ctrl region: %d\n",
			      ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dtrc");
	vou->dtrc = devm_ioremap_resource(dev, res);
	if (IS_ERR(vou->dtrc)) {
		ret = PTR_ERR(vou->dtrc);
		DRM_DEV_ERROR(dev, "failed to remap dtrc region: %d\n", ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vou_ctrl");
	vou->vouctl = devm_ioremap_resource(dev, res);
	if (IS_ERR(vou->vouctl)) {
		ret = PTR_ERR(vou->vouctl);
		DRM_DEV_ERROR(dev, "failed to remap vou_ctrl region: %d\n",
			      ret);
		return ret;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "otfppu");
	vou->otfppu = devm_ioremap_resource(dev, res);
	if (IS_ERR(vou->otfppu)) {
		ret = PTR_ERR(vou->otfppu);
		DRM_DEV_ERROR(dev, "failed to remap otfppu region: %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	vou->axi_clk = devm_clk_get(dev, "aclk");
	if (IS_ERR(vou->axi_clk)) {
		ret = PTR_ERR(vou->axi_clk);
		DRM_DEV_ERROR(dev, "failed to get axi_clk: %d\n", ret);
		return ret;
	}

	vou->ppu_clk = devm_clk_get(dev, "ppu_wclk");
	if (IS_ERR(vou->ppu_clk)) {
		ret = PTR_ERR(vou->ppu_clk);
		DRM_DEV_ERROR(dev, "failed to get ppu_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vou->axi_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to enable axi_clk: %d\n", ret);
		return ret;
	}

	clk_prepare_enable(vou->ppu_clk);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to enable ppu_clk: %d\n", ret);
		goto disable_axi_clk;
	}

	vou->dev = dev;
	dev_set_drvdata(dev, vou);

	vou_hw_init(vou);

	ret = devm_request_irq(dev, irq, vou_irq_handler, 0, "zx_vou", vou);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to request vou irq: %d\n", ret);
		goto disable_ppu_clk;
	}

	ret = zx_crtc_init(drm, vou, VOU_CHN_MAIN);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init main channel crtc: %d\n",
			      ret);
		goto disable_ppu_clk;
	}

	ret = zx_crtc_init(drm, vou, VOU_CHN_AUX);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init aux channel crtc: %d\n",
			      ret);
		goto disable_ppu_clk;
	}

	return 0;

disable_ppu_clk:
	clk_disable_unprepare(vou->ppu_clk);
disable_axi_clk:
	clk_disable_unprepare(vou->axi_clk);
	return ret;
}

static void zx_crtc_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct zx_vou_hw *vou = dev_get_drvdata(dev);

	clk_disable_unprepare(vou->axi_clk);
	clk_disable_unprepare(vou->ppu_clk);
}

static const struct component_ops zx_crtc_component_ops = {
	.bind = zx_crtc_bind,
	.unbind = zx_crtc_unbind,
};

static int zx_crtc_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &zx_crtc_component_ops);
}

static int zx_crtc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &zx_crtc_component_ops);
	return 0;
}

static const struct of_device_id zx_crtc_of_match[] = {
	{ .compatible = "zte,zx296718-dpc", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, zx_crtc_of_match);

struct platform_driver zx_crtc_driver = {
	.probe = zx_crtc_probe,
	.remove = zx_crtc_remove,
	.driver	= {
		.name = "zx-crtc",
		.of_match_table	= zx_crtc_of_match,
	},
};
