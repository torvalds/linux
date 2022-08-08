// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon Hi6220 SoC ADE(Advanced Display Engine)'s crtc&plane driver
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * Author:
 *	Xinliang Liu <z.liuxinliang@hisilicon.com>
 *	Xinliang Liu <xinliang.liu@linaro.org>
 *	Xinwei Kong <kong.kongxinwei@hisilicon.com>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <video/display_timing.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "kirin_drm_drv.h"
#include "kirin_ade_reg.h"

#define OUT_OVLY	ADE_OVLY2 /* output overlay compositor */
#define ADE_DEBUG	1


struct ade_hw_ctx {
	void __iomem  *base;
	struct regmap *noc_regmap;
	struct clk *ade_core_clk;
	struct clk *media_noc_clk;
	struct clk *ade_pix_clk;
	struct reset_control *reset;
	bool power_on;
	int irq;

	struct drm_crtc *crtc;
};

static const struct kirin_format ade_formats[] = {
	/* 16bpp RGB: */
	{ DRM_FORMAT_RGB565, ADE_RGB_565 },
	{ DRM_FORMAT_BGR565, ADE_BGR_565 },
	/* 24bpp RGB: */
	{ DRM_FORMAT_RGB888, ADE_RGB_888 },
	{ DRM_FORMAT_BGR888, ADE_BGR_888 },
	/* 32bpp [A]RGB: */
	{ DRM_FORMAT_XRGB8888, ADE_XRGB_8888 },
	{ DRM_FORMAT_XBGR8888, ADE_XBGR_8888 },
	{ DRM_FORMAT_RGBA8888, ADE_RGBA_8888 },
	{ DRM_FORMAT_BGRA8888, ADE_BGRA_8888 },
	{ DRM_FORMAT_ARGB8888, ADE_ARGB_8888 },
	{ DRM_FORMAT_ABGR8888, ADE_ABGR_8888 },
};

static const u32 channel_formats[] = {
	/* channel 1,2,3,4 */
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565, DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888, DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888, DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888
};

/* convert from fourcc format to ade format */
static u32 ade_get_format(u32 pixel_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ade_formats); i++)
		if (ade_formats[i].pixel_format == pixel_format)
			return ade_formats[i].hw_format;

	/* not found */
	DRM_ERROR("Not found pixel format!!fourcc_format= %d\n",
		  pixel_format);
	return ADE_FORMAT_UNSUPPORT;
}

static void ade_update_reload_bit(void __iomem *base, u32 bit_num, u32 val)
{
	u32 bit_ofst, reg_num;

	bit_ofst = bit_num % 32;
	reg_num = bit_num / 32;

	ade_update_bits(base + ADE_RELOAD_DIS(reg_num), bit_ofst,
			MASK(1), !!val);
}

static u32 ade_read_reload_bit(void __iomem *base, u32 bit_num)
{
	u32 tmp, bit_ofst, reg_num;

	bit_ofst = bit_num % 32;
	reg_num = bit_num / 32;

	tmp = readl(base + ADE_RELOAD_DIS(reg_num));
	return !!(BIT(bit_ofst) & tmp);
}

static void ade_init(struct ade_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	/* enable clk gate */
	ade_update_bits(base + ADE_CTRL1, AUTO_CLK_GATE_EN_OFST,
			AUTO_CLK_GATE_EN, ADE_ENABLE);
	/* clear overlay */
	writel(0, base + ADE_OVLY1_TRANS_CFG);
	writel(0, base + ADE_OVLY_CTL);
	writel(0, base + ADE_OVLYX_CTL(OUT_OVLY));
	/* clear reset and reload regs */
	writel(MASK(32), base + ADE_SOFT_RST_SEL(0));
	writel(MASK(32), base + ADE_SOFT_RST_SEL(1));
	writel(MASK(32), base + ADE_RELOAD_DIS(0));
	writel(MASK(32), base + ADE_RELOAD_DIS(1));
	/*
	 * for video mode, all the ade registers should
	 * become effective at frame end.
	 */
	ade_update_bits(base + ADE_CTRL, FRM_END_START_OFST,
			FRM_END_START_MASK, REG_EFFECTIVE_IN_ADEEN_FRMEND);
}

static bool ade_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;

	adjusted_mode->clock =
		clk_round_rate(ctx->ade_pix_clk, mode->clock * 1000) / 1000;
	return true;
}


static void ade_set_pix_clk(struct ade_hw_ctx *ctx,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adj_mode)
{
	u32 clk_Hz = mode->clock * 1000;
	int ret;

	/*
	 * Success should be guaranteed in mode_valid call back,
	 * so failure shouldn't happen here
	 */
	ret = clk_set_rate(ctx->ade_pix_clk, clk_Hz);
	if (ret)
		DRM_ERROR("failed to set pixel clk %dHz (%d)\n", clk_Hz, ret);
	adj_mode->clock = clk_get_rate(ctx->ade_pix_clk) / 1000;
}

static void ade_ldi_set_mode(struct ade_hw_ctx *ctx,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adj_mode)
{
	void __iomem *base = ctx->base;
	u32 width = mode->hdisplay;
	u32 height = mode->vdisplay;
	u32 hfp, hbp, hsw, vfp, vbp, vsw;
	u32 plr_flags;

	plr_flags = (mode->flags & DRM_MODE_FLAG_NVSYNC) ? FLAG_NVSYNC : 0;
	plr_flags |= (mode->flags & DRM_MODE_FLAG_NHSYNC) ? FLAG_NHSYNC : 0;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;
	if (vsw > 15) {
		DRM_DEBUG_DRIVER("vsw exceeded 15\n");
		vsw = 15;
	}

	writel((hbp << HBP_OFST) | hfp, base + LDI_HRZ_CTRL0);
	 /* the configured value is actual value - 1 */
	writel(hsw - 1, base + LDI_HRZ_CTRL1);
	writel((vbp << VBP_OFST) | vfp, base + LDI_VRT_CTRL0);
	 /* the configured value is actual value - 1 */
	writel(vsw - 1, base + LDI_VRT_CTRL1);
	 /* the configured value is actual value - 1 */
	writel(((height - 1) << VSIZE_OFST) | (width - 1),
	       base + LDI_DSP_SIZE);
	writel(plr_flags, base + LDI_PLR_CTRL);

	/* set overlay compositor output size */
	writel(((width - 1) << OUTPUT_XSIZE_OFST) | (height - 1),
	       base + ADE_OVLY_OUTPUT_SIZE(OUT_OVLY));

	/* ctran6 setting */
	writel(CTRAN_BYPASS_ON, base + ADE_CTRAN_DIS(ADE_CTRAN6));
	 /* the configured value is actual value - 1 */
	writel(width * height - 1, base + ADE_CTRAN_IMAGE_SIZE(ADE_CTRAN6));
	ade_update_reload_bit(base, CTRAN_OFST + ADE_CTRAN6, 0);

	ade_set_pix_clk(ctx, mode, adj_mode);

	DRM_DEBUG_DRIVER("set mode: %dx%d\n", width, height);
}

static int ade_power_up(struct ade_hw_ctx *ctx)
{
	int ret;

	ret = clk_prepare_enable(ctx->media_noc_clk);
	if (ret) {
		DRM_ERROR("failed to enable media_noc_clk (%d)\n", ret);
		return ret;
	}

	ret = reset_control_deassert(ctx->reset);
	if (ret) {
		DRM_ERROR("failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(ctx->ade_core_clk);
	if (ret) {
		DRM_ERROR("failed to enable ade_core_clk (%d)\n", ret);
		return ret;
	}

	ade_init(ctx);
	ctx->power_on = true;
	return 0;
}

static void ade_power_down(struct ade_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	writel(ADE_DISABLE, base + LDI_CTRL);
	/* dsi pixel off */
	writel(DSI_PCLK_OFF, base + LDI_HDMI_DSI_GT);

	clk_disable_unprepare(ctx->ade_core_clk);
	reset_control_assert(ctx->reset);
	clk_disable_unprepare(ctx->media_noc_clk);
	ctx->power_on = false;
}

static void ade_set_medianoc_qos(struct ade_hw_ctx *ctx)
{
	struct regmap *map = ctx->noc_regmap;

	regmap_update_bits(map, ADE0_QOSGENERATOR_MODE,
			   QOSGENERATOR_MODE_MASK, BYPASS_MODE);
	regmap_update_bits(map, ADE0_QOSGENERATOR_EXTCONTROL,
			   SOCKET_QOS_EN, SOCKET_QOS_EN);

	regmap_update_bits(map, ADE1_QOSGENERATOR_MODE,
			   QOSGENERATOR_MODE_MASK, BYPASS_MODE);
	regmap_update_bits(map, ADE1_QOSGENERATOR_EXTCONTROL,
			   SOCKET_QOS_EN, SOCKET_QOS_EN);
}

static int ade_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	void __iomem *base = ctx->base;

	if (!ctx->power_on)
		(void)ade_power_up(ctx);

	ade_update_bits(base + LDI_INT_EN, FRAME_END_INT_EN_OFST,
			MASK(1), 1);

	return 0;
}

static void ade_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	void __iomem *base = ctx->base;

	if (!ctx->power_on) {
		DRM_ERROR("power is down! vblank disable fail\n");
		return;
	}

	ade_update_bits(base + LDI_INT_EN, FRAME_END_INT_EN_OFST,
			MASK(1), 0);
}

static irqreturn_t ade_irq_handler(int irq, void *data)
{
	struct ade_hw_ctx *ctx = data;
	struct drm_crtc *crtc = ctx->crtc;
	void __iomem *base = ctx->base;
	u32 status;

	status = readl(base + LDI_MSK_INT);
	DRM_DEBUG_VBL("LDI IRQ: status=0x%X\n", status);

	/* vblank irq */
	if (status & BIT(FRAME_END_INT_EN_OFST)) {
		ade_update_bits(base + LDI_INT_CLR, FRAME_END_INT_EN_OFST,
				MASK(1), 1);
		drm_crtc_handle_vblank(crtc);
	}

	return IRQ_HANDLED;
}

static void ade_display_enable(struct ade_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;
	u32 out_fmt = LDI_OUT_RGB_888;

	/* enable output overlay compositor */
	writel(ADE_ENABLE, base + ADE_OVLYX_CTL(OUT_OVLY));
	ade_update_reload_bit(base, OVLY_OFST + OUT_OVLY, 0);

	/* display source setting */
	writel(DISP_SRC_OVLY2, base + ADE_DISP_SRC_CFG);

	/* enable ade */
	writel(ADE_ENABLE, base + ADE_EN);
	/* enable ldi */
	writel(NORMAL_MODE, base + LDI_WORK_MODE);
	writel((out_fmt << BPP_OFST) | DATA_GATE_EN | LDI_EN,
	       base + LDI_CTRL);
	/* dsi pixel on */
	writel(DSI_PCLK_ON, base + LDI_HDMI_DSI_GT);
}

#if ADE_DEBUG
static void ade_rdma_dump_regs(void __iomem *base, u32 ch)
{
	u32 reg_ctrl, reg_addr, reg_size, reg_stride, reg_space, reg_en;
	u32 val;

	reg_ctrl = RD_CH_CTRL(ch);
	reg_addr = RD_CH_ADDR(ch);
	reg_size = RD_CH_SIZE(ch);
	reg_stride = RD_CH_STRIDE(ch);
	reg_space = RD_CH_SPACE(ch);
	reg_en = RD_CH_EN(ch);

	val = ade_read_reload_bit(base, RDMA_OFST + ch);
	DRM_DEBUG_DRIVER("[rdma%d]: reload(%d)\n", ch + 1, val);
	val = readl(base + reg_ctrl);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_ctrl(0x%08x)\n", ch + 1, val);
	val = readl(base + reg_addr);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_addr(0x%08x)\n", ch + 1, val);
	val = readl(base + reg_size);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_size(0x%08x)\n", ch + 1, val);
	val = readl(base + reg_stride);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_stride(0x%08x)\n", ch + 1, val);
	val = readl(base + reg_space);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_space(0x%08x)\n", ch + 1, val);
	val = readl(base + reg_en);
	DRM_DEBUG_DRIVER("[rdma%d]: reg_en(0x%08x)\n", ch + 1, val);
}

static void ade_clip_dump_regs(void __iomem *base, u32 ch)
{
	u32 val;

	val = ade_read_reload_bit(base, CLIP_OFST + ch);
	DRM_DEBUG_DRIVER("[clip%d]: reload(%d)\n", ch + 1, val);
	val = readl(base + ADE_CLIP_DISABLE(ch));
	DRM_DEBUG_DRIVER("[clip%d]: reg_clip_disable(0x%08x)\n", ch + 1, val);
	val = readl(base + ADE_CLIP_SIZE0(ch));
	DRM_DEBUG_DRIVER("[clip%d]: reg_clip_size0(0x%08x)\n", ch + 1, val);
	val = readl(base + ADE_CLIP_SIZE1(ch));
	DRM_DEBUG_DRIVER("[clip%d]: reg_clip_size1(0x%08x)\n", ch + 1, val);
}

static void ade_compositor_routing_dump_regs(void __iomem *base, u32 ch)
{
	u8 ovly_ch = 0; /* TODO: Only primary plane now */
	u32 val;

	val = readl(base + ADE_OVLY_CH_XY0(ovly_ch));
	DRM_DEBUG_DRIVER("[overlay ch%d]: reg_ch_xy0(0x%08x)\n", ovly_ch, val);
	val = readl(base + ADE_OVLY_CH_XY1(ovly_ch));
	DRM_DEBUG_DRIVER("[overlay ch%d]: reg_ch_xy1(0x%08x)\n", ovly_ch, val);
	val = readl(base + ADE_OVLY_CH_CTL(ovly_ch));
	DRM_DEBUG_DRIVER("[overlay ch%d]: reg_ch_ctl(0x%08x)\n", ovly_ch, val);
}

static void ade_dump_overlay_compositor_regs(void __iomem *base, u32 comp)
{
	u32 val;

	val = ade_read_reload_bit(base, OVLY_OFST + comp);
	DRM_DEBUG_DRIVER("[overlay%d]: reload(%d)\n", comp + 1, val);
	writel(ADE_ENABLE, base + ADE_OVLYX_CTL(comp));
	DRM_DEBUG_DRIVER("[overlay%d]: reg_ctl(0x%08x)\n", comp + 1, val);
	val = readl(base + ADE_OVLY_CTL);
	DRM_DEBUG_DRIVER("ovly_ctl(0x%08x)\n", val);
}

static void ade_dump_regs(void __iomem *base)
{
	u32 i;

	/* dump channel regs */
	for (i = 0; i < ADE_CH_NUM; i++) {
		/* dump rdma regs */
		ade_rdma_dump_regs(base, i);

		/* dump clip regs */
		ade_clip_dump_regs(base, i);

		/* dump compositor routing regs */
		ade_compositor_routing_dump_regs(base, i);
	}

	/* dump overlay compositor regs */
	ade_dump_overlay_compositor_regs(base, OUT_OVLY);
}
#else
static void ade_dump_regs(void __iomem *base) { }
#endif

static void ade_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	int ret;

	if (kcrtc->enable)
		return;

	if (!ctx->power_on) {
		ret = ade_power_up(ctx);
		if (ret)
			return;
	}

	ade_set_medianoc_qos(ctx);
	ade_display_enable(ctx);
	ade_dump_regs(ctx->base);
	drm_crtc_vblank_on(crtc);
	kcrtc->enable = true;
}

static void ade_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;

	if (!kcrtc->enable)
		return;

	drm_crtc_vblank_off(crtc);
	ade_power_down(ctx);
	kcrtc->enable = false;
}

static void ade_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_display_mode *adj_mode = &crtc->state->adjusted_mode;

	if (!ctx->power_on)
		(void)ade_power_up(ctx);
	ade_ldi_set_mode(ctx, mode, adj_mode);
}

static void ade_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_display_mode *adj_mode = &crtc->state->adjusted_mode;

	if (!ctx->power_on)
		(void)ade_power_up(ctx);
	ade_ldi_set_mode(ctx, mode, adj_mode);
}

static void ade_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)

{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct ade_hw_ctx *ctx = kcrtc->hw_ctx;
	struct drm_pending_vblank_event *event = crtc->state->event;
	void __iomem *base = ctx->base;

	/* only crtc is enabled regs take effect */
	if (kcrtc->enable) {
		ade_dump_regs(base);
		/* flush ade registers */
		writel(ADE_ENABLE, base + ADE_EN);
	}

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs ade_crtc_helper_funcs = {
	.mode_fixup	= ade_crtc_mode_fixup,
	.mode_set_nofb	= ade_crtc_mode_set_nofb,
	.atomic_begin	= ade_crtc_atomic_begin,
	.atomic_flush	= ade_crtc_atomic_flush,
	.atomic_enable	= ade_crtc_atomic_enable,
	.atomic_disable	= ade_crtc_atomic_disable,
};

static const struct drm_crtc_funcs ade_crtc_funcs = {
	.destroy	= drm_crtc_cleanup,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank	= ade_crtc_enable_vblank,
	.disable_vblank	= ade_crtc_disable_vblank,
};

static void ade_rdma_set(void __iomem *base, struct drm_framebuffer *fb,
			 u32 ch, u32 y, u32 in_h, u32 fmt)
{
	struct drm_gem_cma_object *obj = drm_fb_cma_get_gem_obj(fb, 0);
	u32 reg_ctrl, reg_addr, reg_size, reg_stride, reg_space, reg_en;
	u32 stride = fb->pitches[0];
	u32 addr = (u32)obj->paddr + y * stride;

	DRM_DEBUG_DRIVER("rdma%d: (y=%d, height=%d), stride=%d, paddr=0x%x\n",
			 ch + 1, y, in_h, stride, (u32)obj->paddr);
	DRM_DEBUG_DRIVER("addr=0x%x, fb:%dx%d, pixel_format=%d(%p4cc)\n",
			 addr, fb->width, fb->height, fmt,
			 &fb->format->format);

	/* get reg offset */
	reg_ctrl = RD_CH_CTRL(ch);
	reg_addr = RD_CH_ADDR(ch);
	reg_size = RD_CH_SIZE(ch);
	reg_stride = RD_CH_STRIDE(ch);
	reg_space = RD_CH_SPACE(ch);
	reg_en = RD_CH_EN(ch);

	/*
	 * TODO: set rotation
	 */
	writel((fmt << 16) & 0x1f0000, base + reg_ctrl);
	writel(addr, base + reg_addr);
	writel((in_h << 16) | stride, base + reg_size);
	writel(stride, base + reg_stride);
	writel(in_h * stride, base + reg_space);
	writel(ADE_ENABLE, base + reg_en);
	ade_update_reload_bit(base, RDMA_OFST + ch, 0);
}

static void ade_rdma_disable(void __iomem *base, u32 ch)
{
	u32 reg_en;

	/* get reg offset */
	reg_en = RD_CH_EN(ch);
	writel(0, base + reg_en);
	ade_update_reload_bit(base, RDMA_OFST + ch, 1);
}

static void ade_clip_set(void __iomem *base, u32 ch, u32 fb_w, u32 x,
			 u32 in_w, u32 in_h)
{
	u32 disable_val;
	u32 clip_left;
	u32 clip_right;

	/*
	 * clip width, no need to clip height
	 */
	if (fb_w == in_w) { /* bypass */
		disable_val = 1;
		clip_left = 0;
		clip_right = 0;
	} else {
		disable_val = 0;
		clip_left = x;
		clip_right = fb_w - (x + in_w) - 1;
	}

	DRM_DEBUG_DRIVER("clip%d: clip_left=%d, clip_right=%d\n",
			 ch + 1, clip_left, clip_right);

	writel(disable_val, base + ADE_CLIP_DISABLE(ch));
	writel((fb_w - 1) << 16 | (in_h - 1), base + ADE_CLIP_SIZE0(ch));
	writel(clip_left << 16 | clip_right, base + ADE_CLIP_SIZE1(ch));
	ade_update_reload_bit(base, CLIP_OFST + ch, 0);
}

static void ade_clip_disable(void __iomem *base, u32 ch)
{
	writel(1, base + ADE_CLIP_DISABLE(ch));
	ade_update_reload_bit(base, CLIP_OFST + ch, 1);
}

static bool has_Alpha_channel(int format)
{
	switch (format) {
	case ADE_ARGB_8888:
	case ADE_ABGR_8888:
	case ADE_RGBA_8888:
	case ADE_BGRA_8888:
		return true;
	default:
		return false;
	}
}

static void ade_get_blending_params(u32 fmt, u8 glb_alpha, u8 *alp_mode,
				    u8 *alp_sel, u8 *under_alp_sel)
{
	bool has_alpha = has_Alpha_channel(fmt);

	/*
	 * get alp_mode
	 */
	if (has_alpha && glb_alpha < 255)
		*alp_mode = ADE_ALP_PIXEL_AND_GLB;
	else if (has_alpha)
		*alp_mode = ADE_ALP_PIXEL;
	else
		*alp_mode = ADE_ALP_GLOBAL;

	/*
	 * get alp sel
	 */
	*alp_sel = ADE_ALP_MUL_COEFF_3; /* 1 */
	*under_alp_sel = ADE_ALP_MUL_COEFF_2; /* 0 */
}

static void ade_compositor_routing_set(void __iomem *base, u8 ch,
				       u32 x0, u32 y0,
				       u32 in_w, u32 in_h, u32 fmt)
{
	u8 ovly_ch = 0; /* TODO: This is the zpos, only one plane now */
	u8 glb_alpha = 255;
	u32 x1 = x0 + in_w - 1;
	u32 y1 = y0 + in_h - 1;
	u32 val;
	u8 alp_sel;
	u8 under_alp_sel;
	u8 alp_mode;

	ade_get_blending_params(fmt, glb_alpha, &alp_mode, &alp_sel,
				&under_alp_sel);

	/* overlay routing setting
	 */
	writel(x0 << 16 | y0, base + ADE_OVLY_CH_XY0(ovly_ch));
	writel(x1 << 16 | y1, base + ADE_OVLY_CH_XY1(ovly_ch));
	val = (ch + 1) << CH_SEL_OFST | BIT(CH_EN_OFST) |
		alp_sel << CH_ALP_SEL_OFST |
		under_alp_sel << CH_UNDER_ALP_SEL_OFST |
		glb_alpha << CH_ALP_GBL_OFST |
		alp_mode << CH_ALP_MODE_OFST;
	writel(val, base + ADE_OVLY_CH_CTL(ovly_ch));
	/* connect this plane/channel to overlay2 compositor */
	ade_update_bits(base + ADE_OVLY_CTL, CH_OVLY_SEL_OFST(ovly_ch),
			CH_OVLY_SEL_MASK, CH_OVLY_SEL_VAL(OUT_OVLY));
}

static void ade_compositor_routing_disable(void __iomem *base, u32 ch)
{
	u8 ovly_ch = 0; /* TODO: Only primary plane now */

	/* disable this plane/channel */
	ade_update_bits(base + ADE_OVLY_CH_CTL(ovly_ch), CH_EN_OFST,
			MASK(1), 0);
	/* dis-connect this plane/channel of overlay2 compositor */
	ade_update_bits(base + ADE_OVLY_CTL, CH_OVLY_SEL_OFST(ovly_ch),
			CH_OVLY_SEL_MASK, 0);
}

/*
 * Typicaly, a channel looks like: DMA-->clip-->scale-->ctrans-->compositor
 */
static void ade_update_channel(struct kirin_plane *kplane,
			       struct drm_framebuffer *fb, int crtc_x,
			       int crtc_y, unsigned int crtc_w,
			       unsigned int crtc_h, u32 src_x,
			       u32 src_y, u32 src_w, u32 src_h)
{
	struct ade_hw_ctx *ctx = kplane->hw_ctx;
	void __iomem *base = ctx->base;
	u32 fmt = ade_get_format(fb->format->format);
	u32 ch = kplane->ch;
	u32 in_w;
	u32 in_h;

	DRM_DEBUG_DRIVER("channel%d: src:(%d, %d)-%dx%d, crtc:(%d, %d)-%dx%d",
			 ch + 1, src_x, src_y, src_w, src_h,
			 crtc_x, crtc_y, crtc_w, crtc_h);

	/* 1) DMA setting */
	in_w = src_w;
	in_h = src_h;
	ade_rdma_set(base, fb, ch, src_y, in_h, fmt);

	/* 2) clip setting */
	ade_clip_set(base, ch, fb->width, src_x, in_w, in_h);

	/* 3) TODO: scale setting for overlay planes */

	/* 4) TODO: ctran/csc setting for overlay planes */

	/* 5) compositor routing setting */
	ade_compositor_routing_set(base, ch, crtc_x, crtc_y, in_w, in_h, fmt);
}

static void ade_disable_channel(struct kirin_plane *kplane)
{
	struct ade_hw_ctx *ctx = kplane->hw_ctx;
	void __iomem *base = ctx->base;
	u32 ch = kplane->ch;

	DRM_DEBUG_DRIVER("disable channel%d\n", ch + 1);

	/* disable read DMA */
	ade_rdma_disable(base, ch);

	/* disable clip */
	ade_clip_disable(base, ch);

	/* disable compositor routing */
	ade_compositor_routing_disable(base, ch);
}

static int ade_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *crtc_state;
	u32 src_x = new_plane_state->src_x >> 16;
	u32 src_y = new_plane_state->src_y >> 16;
	u32 src_w = new_plane_state->src_w >> 16;
	u32 src_h = new_plane_state->src_h >> 16;
	int crtc_x = new_plane_state->crtc_x;
	int crtc_y = new_plane_state->crtc_y;
	u32 crtc_w = new_plane_state->crtc_w;
	u32 crtc_h = new_plane_state->crtc_h;
	u32 fmt;

	if (!crtc || !fb)
		return 0;

	fmt = ade_get_format(fb->format->format);
	if (fmt == ADE_FORMAT_UNSUPPORT)
		return -EINVAL;

	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (src_w != crtc_w || src_h != crtc_h) {
		return -EINVAL;
	}

	if (src_x + src_w > fb->width ||
	    src_y + src_h > fb->height)
		return -EINVAL;

	if (crtc_x < 0 || crtc_y < 0)
		return -EINVAL;

	if (crtc_x + crtc_w > crtc_state->adjusted_mode.hdisplay ||
	    crtc_y + crtc_h > crtc_state->adjusted_mode.vdisplay)
		return -EINVAL;

	return 0;
}

static void ade_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct kirin_plane *kplane = to_kirin_plane(plane);

	ade_update_channel(kplane, new_state->fb, new_state->crtc_x,
			   new_state->crtc_y,
			   new_state->crtc_w, new_state->crtc_h,
			   new_state->src_x >> 16, new_state->src_y >> 16,
			   new_state->src_w >> 16, new_state->src_h >> 16);
}

static void ade_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct kirin_plane *kplane = to_kirin_plane(plane);

	ade_disable_channel(kplane);
}

static const struct drm_plane_helper_funcs ade_plane_helper_funcs = {
	.atomic_check = ade_plane_atomic_check,
	.atomic_update = ade_plane_atomic_update,
	.atomic_disable = ade_plane_atomic_disable,
};

static struct drm_plane_funcs ade_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static void *ade_hw_ctx_alloc(struct platform_device *pdev,
			      struct drm_crtc *crtc)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct ade_hw_ctx *ctx = NULL;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		DRM_ERROR("failed to alloc ade_hw_ctx\n");
		return ERR_PTR(-ENOMEM);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->base)) {
		DRM_ERROR("failed to remap ade io base\n");
		return ERR_PTR(-EIO);
	}

	ctx->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(ctx->reset))
		return ERR_PTR(-ENODEV);

	ctx->noc_regmap =
		syscon_regmap_lookup_by_phandle(np, "hisilicon,noc-syscon");
	if (IS_ERR(ctx->noc_regmap)) {
		DRM_ERROR("failed to get noc regmap\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		DRM_ERROR("failed to get irq\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->ade_core_clk = devm_clk_get(dev, "clk_ade_core");
	if (IS_ERR(ctx->ade_core_clk)) {
		DRM_ERROR("failed to parse clk ADE_CORE\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->media_noc_clk = devm_clk_get(dev, "clk_codec_jpeg");
	if (IS_ERR(ctx->media_noc_clk)) {
		DRM_ERROR("failed to parse clk CODEC_JPEG\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->ade_pix_clk = devm_clk_get(dev, "clk_ade_pix");
	if (IS_ERR(ctx->ade_pix_clk)) {
		DRM_ERROR("failed to parse clk ADE_PIX\n");
		return ERR_PTR(-ENODEV);
	}

	/* vblank irq init */
	ret = devm_request_irq(dev, ctx->irq, ade_irq_handler,
			       IRQF_SHARED, dev->driver->name, ctx);
	if (ret)
		return ERR_PTR(-EIO);

	ctx->crtc = crtc;

	return ctx;
}

static void ade_hw_ctx_cleanup(void *hw_ctx)
{
}

static const struct drm_mode_config_funcs ade_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,

};

DEFINE_DRM_GEM_CMA_FOPS(ade_fops);

static const struct drm_driver ade_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &ade_fops,
	DRM_GEM_CMA_DRIVER_OPS,
	.name = "kirin",
	.desc = "Hisilicon Kirin620 SoC DRM Driver",
	.date = "20150718",
	.major = 1,
	.minor = 0,
};

struct kirin_drm_data ade_driver_data = {
	.num_planes = ADE_CH_NUM,
	.prim_plane = ADE_CH1,
	.channel_formats = channel_formats,
	.channel_formats_cnt = ARRAY_SIZE(channel_formats),
	.config_max_width = 2048,
	.config_max_height = 2048,
	.driver = &ade_driver,
	.crtc_helper_funcs = &ade_crtc_helper_funcs,
	.crtc_funcs = &ade_crtc_funcs,
	.plane_helper_funcs = &ade_plane_helper_funcs,
	.plane_funcs = &ade_plane_funcs,
	.mode_config_funcs = &ade_mode_config_funcs,

	.alloc_hw_ctx = ade_hw_ctx_alloc,
	.cleanup_hw_ctx = ade_hw_ctx_cleanup,
};
