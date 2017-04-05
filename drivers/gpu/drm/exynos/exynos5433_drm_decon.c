/* drivers/gpu/drm/exynos5433_drm_decon.c
 *
 * Copyright (C) 2015 Samsung Electronics Co.Ltd
 * Authors:
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Hyungwon Hwang <human.hwang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <video/exynos5433_decon.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_plane.h"
#include "exynos_drm_iommu.h"

#define DSD_CFG_MUX 0x1004
#define DSD_CFG_MUX_TE_UNMASK_GLOBAL BIT(13)

#define WINDOWS_NR	3
#define MIN_FB_WIDTH_FOR_16WORD_BURST	128

#define IFTYPE_I80	(1 << 0)
#define I80_HW_TRG	(1 << 1)
#define IFTYPE_HDMI	(1 << 2)

static const char * const decon_clks_name[] = {
	"pclk",
	"aclk_decon",
	"aclk_smmu_decon0x",
	"aclk_xiu_decon0x",
	"pclk_smmu_decon0x",
	"sclk_decon_vclk",
	"sclk_decon_eclk",
};

struct decon_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	struct exynos_drm_crtc		*crtc;
	struct exynos_drm_plane		planes[WINDOWS_NR];
	struct exynos_drm_plane_config	configs[WINDOWS_NR];
	void __iomem			*addr;
	struct regmap			*sysreg;
	struct clk			*clks[ARRAY_SIZE(decon_clks_name)];
	unsigned int			irq;
	unsigned int			te_irq;
	unsigned long			out_type;
	int				first_win;
	spinlock_t			vblank_lock;
	u32				frame_id;
};

static const uint32_t decon_formats[] = {
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const enum drm_plane_type decon_win_types[WINDOWS_NR] = {
	DRM_PLANE_TYPE_PRIMARY,
	DRM_PLANE_TYPE_OVERLAY,
	DRM_PLANE_TYPE_CURSOR,
};

static inline void decon_set_bits(struct decon_context *ctx, u32 reg, u32 mask,
				  u32 val)
{
	val = (val & mask) | (readl(ctx->addr + reg) & ~mask);
	writel(val, ctx->addr + reg);
}

static int decon_enable_vblank(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;
	u32 val;

	val = VIDINTCON0_INTEN;
	if (ctx->out_type & IFTYPE_I80)
		val |= VIDINTCON0_FRAMEDONE;
	else
		val |= VIDINTCON0_INTFRMEN | VIDINTCON0_FRAMESEL_FP;

	writel(val, ctx->addr + DECON_VIDINTCON0);

	enable_irq(ctx->irq);
	if (!(ctx->out_type & I80_HW_TRG))
		enable_irq(ctx->te_irq);

	return 0;
}

static void decon_disable_vblank(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;

	if (!(ctx->out_type & I80_HW_TRG))
		disable_irq_nosync(ctx->te_irq);
	disable_irq_nosync(ctx->irq);

	writel(0, ctx->addr + DECON_VIDINTCON0);
}

/* return number of starts/ends of frame transmissions since reset */
static u32 decon_get_frame_count(struct decon_context *ctx, bool end)
{
	u32 frm, pfrm, status, cnt = 2;

	/* To get consistent result repeat read until frame id is stable.
	 * Usually the loop will be executed once, in rare cases when the loop
	 * is executed at frame change time 2nd pass will be needed.
	 */
	frm = readl(ctx->addr + DECON_CRFMID);
	do {
		status = readl(ctx->addr + DECON_VIDCON1);
		pfrm = frm;
		frm = readl(ctx->addr + DECON_CRFMID);
	} while (frm != pfrm && --cnt);

	/* CRFMID is incremented on BPORCH in case of I80 and on VSYNC in case
	 * of RGB, it should be taken into account.
	 */
	if (!frm)
		return 0;

	switch (status & (VIDCON1_VSTATUS_MASK | VIDCON1_I80_ACTIVE)) {
	case VIDCON1_VSTATUS_VS:
		if (!(ctx->out_type & IFTYPE_I80))
			--frm;
		break;
	case VIDCON1_VSTATUS_BP:
		--frm;
		break;
	case VIDCON1_I80_ACTIVE:
	case VIDCON1_VSTATUS_AC:
		if (end)
			--frm;
		break;
	default:
		break;
	}

	return frm;
}

static u32 decon_get_vblank_counter(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;

	return decon_get_frame_count(ctx, false);
}

static void decon_setup_trigger(struct decon_context *ctx)
{
	if (!(ctx->out_type & (IFTYPE_I80 | I80_HW_TRG)))
		return;

	if (!(ctx->out_type & I80_HW_TRG)) {
		writel(TRIGCON_TRIGEN_PER_F | TRIGCON_TRIGEN_F |
		       TRIGCON_TE_AUTO_MASK | TRIGCON_SWTRIGEN,
		       ctx->addr + DECON_TRIGCON);
		return;
	}

	writel(TRIGCON_TRIGEN_PER_F | TRIGCON_TRIGEN_F | TRIGCON_HWTRIGMASK
	       | TRIGCON_HWTRIGEN, ctx->addr + DECON_TRIGCON);

	if (regmap_update_bits(ctx->sysreg, DSD_CFG_MUX,
			       DSD_CFG_MUX_TE_UNMASK_GLOBAL, ~0))
		DRM_ERROR("Cannot update sysreg.\n");
}

static void decon_commit(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;
	struct drm_display_mode *m = &crtc->base.mode;
	bool interlaced = false;
	u32 val;

	if (ctx->out_type & IFTYPE_HDMI) {
		m->crtc_hsync_start = m->crtc_hdisplay + 10;
		m->crtc_hsync_end = m->crtc_htotal - 92;
		m->crtc_vsync_start = m->crtc_vdisplay + 1;
		m->crtc_vsync_end = m->crtc_vsync_start + 1;
		if (m->flags & DRM_MODE_FLAG_INTERLACE)
			interlaced = true;
	}

	decon_setup_trigger(ctx);

	/* lcd on and use command if */
	val = VIDOUT_LCD_ON;
	if (interlaced)
		val |= VIDOUT_INTERLACE_EN_F;
	if (ctx->out_type & IFTYPE_I80) {
		val |= VIDOUT_COMMAND_IF;
	} else {
		val |= VIDOUT_RGB_IF;
	}

	writel(val, ctx->addr + DECON_VIDOUTCON0);

	if (interlaced)
		val = VIDTCON2_LINEVAL(m->vdisplay / 2 - 1) |
			VIDTCON2_HOZVAL(m->hdisplay - 1);
	else
		val = VIDTCON2_LINEVAL(m->vdisplay - 1) |
			VIDTCON2_HOZVAL(m->hdisplay - 1);
	writel(val, ctx->addr + DECON_VIDTCON2);

	if (!(ctx->out_type & IFTYPE_I80)) {
		int vbp = m->crtc_vtotal - m->crtc_vsync_end;
		int vfp = m->crtc_vsync_start - m->crtc_vdisplay;

		if (interlaced)
			vbp = vbp / 2 - 1;
		val = VIDTCON00_VBPD_F(vbp - 1) | VIDTCON00_VFPD_F(vfp - 1);
		writel(val, ctx->addr + DECON_VIDTCON00);

		val = VIDTCON01_VSPW_F(
				m->crtc_vsync_end - m->crtc_vsync_start - 1);
		writel(val, ctx->addr + DECON_VIDTCON01);

		val = VIDTCON10_HBPD_F(
				m->crtc_htotal - m->crtc_hsync_end - 1) |
			VIDTCON10_HFPD_F(
				m->crtc_hsync_start - m->crtc_hdisplay - 1);
		writel(val, ctx->addr + DECON_VIDTCON10);

		val = VIDTCON11_HSPW_F(
				m->crtc_hsync_end - m->crtc_hsync_start - 1);
		writel(val, ctx->addr + DECON_VIDTCON11);
	}

	/* enable output and display signal */
	decon_set_bits(ctx, DECON_VIDCON0, VIDCON0_ENVID | VIDCON0_ENVID_F, ~0);

	decon_set_bits(ctx, DECON_UPDATE, STANDALONE_UPDATE_F, ~0);
}

static void decon_win_set_pixfmt(struct decon_context *ctx, unsigned int win,
				 struct drm_framebuffer *fb)
{
	unsigned long val;

	val = readl(ctx->addr + DECON_WINCONx(win));
	val &= ~WINCONx_BPPMODE_MASK;

	switch (fb->format->format) {
	case DRM_FORMAT_XRGB1555:
		val |= WINCONx_BPPMODE_16BPP_I1555;
		val |= WINCONx_HAWSWP_F;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_RGB565:
		val |= WINCONx_BPPMODE_16BPP_565;
		val |= WINCONx_HAWSWP_F;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_XRGB8888:
		val |= WINCONx_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP_F;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_ARGB8888:
		val |= WINCONx_BPPMODE_32BPP_A8888;
		val |= WINCONx_WSWP_F | WINCONx_BLD_PIX_F | WINCONx_ALPHA_SEL_F;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	default:
		DRM_ERROR("Proper pixel format is not set\n");
		return;
	}

	DRM_DEBUG_KMS("bpp = %u\n", fb->format->cpp[0] * 8);

	/*
	 * In case of exynos, setting dma-burst to 16Word causes permanent
	 * tearing for very small buffers, e.g. cursor buffer. Burst Mode
	 * switching which is based on plane size is not recommended as
	 * plane size varies a lot towards the end of the screen and rapid
	 * movement causes unstable DMA which results into iommu crash/tear.
	 */

	if (fb->width < MIN_FB_WIDTH_FOR_16WORD_BURST) {
		val &= ~WINCONx_BURSTLEN_MASK;
		val |= WINCONx_BURSTLEN_8WORD;
	}

	writel(val, ctx->addr + DECON_WINCONx(win));
}

static void decon_shadow_protect(struct decon_context *ctx, bool protect)
{
	decon_set_bits(ctx, DECON_SHADOWCON, SHADOWCON_PROTECT_MASK,
		       protect ? ~0 : 0);
}

static void decon_atomic_begin(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;

	decon_shadow_protect(ctx, true);
}

#define BIT_VAL(x, e, s) (((x) & ((1 << ((e) - (s) + 1)) - 1)) << (s))
#define COORDINATE_X(x) BIT_VAL((x), 23, 12)
#define COORDINATE_Y(x) BIT_VAL((x), 11, 0)

static void decon_update_plane(struct exynos_drm_crtc *crtc,
			       struct exynos_drm_plane *plane)
{
	struct exynos_drm_plane_state *state =
				to_exynos_plane_state(plane->base.state);
	struct decon_context *ctx = crtc->ctx;
	struct drm_framebuffer *fb = state->base.fb;
	unsigned int win = plane->index;
	unsigned int bpp = fb->format->cpp[0];
	unsigned int pitch = fb->pitches[0];
	dma_addr_t dma_addr = exynos_drm_fb_dma_addr(fb, 0);
	u32 val;

	if (crtc->base.mode.flags & DRM_MODE_FLAG_INTERLACE) {
		val = COORDINATE_X(state->crtc.x) |
			COORDINATE_Y(state->crtc.y / 2);
		writel(val, ctx->addr + DECON_VIDOSDxA(win));

		val = COORDINATE_X(state->crtc.x + state->crtc.w - 1) |
			COORDINATE_Y((state->crtc.y + state->crtc.h) / 2 - 1);
		writel(val, ctx->addr + DECON_VIDOSDxB(win));
	} else {
		val = COORDINATE_X(state->crtc.x) | COORDINATE_Y(state->crtc.y);
		writel(val, ctx->addr + DECON_VIDOSDxA(win));

		val = COORDINATE_X(state->crtc.x + state->crtc.w - 1) |
				COORDINATE_Y(state->crtc.y + state->crtc.h - 1);
		writel(val, ctx->addr + DECON_VIDOSDxB(win));
	}

	val = VIDOSD_Wx_ALPHA_R_F(0x0) | VIDOSD_Wx_ALPHA_G_F(0x0) |
		VIDOSD_Wx_ALPHA_B_F(0x0);
	writel(val, ctx->addr + DECON_VIDOSDxC(win));

	val = VIDOSD_Wx_ALPHA_R_F(0x0) | VIDOSD_Wx_ALPHA_G_F(0x0) |
		VIDOSD_Wx_ALPHA_B_F(0x0);
	writel(val, ctx->addr + DECON_VIDOSDxD(win));

	writel(dma_addr, ctx->addr + DECON_VIDW0xADD0B0(win));

	val = dma_addr + pitch * state->src.h;
	writel(val, ctx->addr + DECON_VIDW0xADD1B0(win));

	if (!(ctx->out_type & IFTYPE_HDMI))
		val = BIT_VAL(pitch - state->crtc.w * bpp, 27, 14)
			| BIT_VAL(state->crtc.w * bpp, 13, 0);
	else
		val = BIT_VAL(pitch - state->crtc.w * bpp, 29, 15)
			| BIT_VAL(state->crtc.w * bpp, 14, 0);
	writel(val, ctx->addr + DECON_VIDW0xADD2(win));

	decon_win_set_pixfmt(ctx, win, fb);

	/* window enable */
	decon_set_bits(ctx, DECON_WINCONx(win), WINCONx_ENWIN_F, ~0);
}

static void decon_disable_plane(struct exynos_drm_crtc *crtc,
				struct exynos_drm_plane *plane)
{
	struct decon_context *ctx = crtc->ctx;
	unsigned int win = plane->index;

	decon_set_bits(ctx, DECON_WINCONx(win), WINCONx_ENWIN_F, 0);
}

static void decon_atomic_flush(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->vblank_lock, flags);

	decon_shadow_protect(ctx, false);

	decon_set_bits(ctx, DECON_UPDATE, STANDALONE_UPDATE_F, ~0);

	ctx->frame_id = decon_get_frame_count(ctx, true);

	exynos_crtc_handle_event(crtc);

	spin_unlock_irqrestore(&ctx->vblank_lock, flags);
}

static void decon_swreset(struct decon_context *ctx)
{
	unsigned int tries;
	unsigned long flags;

	writel(0, ctx->addr + DECON_VIDCON0);
	for (tries = 2000; tries; --tries) {
		if (~readl(ctx->addr + DECON_VIDCON0) & VIDCON0_STOP_STATUS)
			break;
		udelay(10);
	}

	writel(VIDCON0_SWRESET, ctx->addr + DECON_VIDCON0);
	for (tries = 2000; tries; --tries) {
		if (~readl(ctx->addr + DECON_VIDCON0) & VIDCON0_SWRESET)
			break;
		udelay(10);
	}

	WARN(tries == 0, "failed to software reset DECON\n");

	spin_lock_irqsave(&ctx->vblank_lock, flags);
	ctx->frame_id = 0;
	spin_unlock_irqrestore(&ctx->vblank_lock, flags);

	if (!(ctx->out_type & IFTYPE_HDMI))
		return;

	writel(VIDCON0_CLKVALUP | VIDCON0_VLCKFREE, ctx->addr + DECON_VIDCON0);
	decon_set_bits(ctx, DECON_CMU,
		       CMU_CLKGAGE_MODE_SFR_F | CMU_CLKGAGE_MODE_MEM_F, ~0);
	writel(VIDCON1_VCLK_RUN_VDEN_DISABLE, ctx->addr + DECON_VIDCON1);
	writel(CRCCTRL_CRCEN | CRCCTRL_CRCSTART_F | CRCCTRL_CRCCLKEN,
	       ctx->addr + DECON_CRCCTRL);
}

static void decon_enable(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;

	pm_runtime_get_sync(ctx->dev);

	exynos_drm_pipe_clk_enable(crtc, true);

	decon_swreset(ctx);

	decon_commit(ctx->crtc);
}

static void decon_disable(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;
	int i;

	if (!(ctx->out_type & I80_HW_TRG))
		synchronize_irq(ctx->te_irq);
	synchronize_irq(ctx->irq);

	/*
	 * We need to make sure that all windows are disabled before we
	 * suspend that connector. Otherwise we might try to scan from
	 * a destroyed buffer later.
	 */
	for (i = ctx->first_win; i < WINDOWS_NR; i++)
		decon_disable_plane(crtc, &ctx->planes[i]);

	decon_swreset(ctx);

	exynos_drm_pipe_clk_enable(crtc, false);

	pm_runtime_put_sync(ctx->dev);
}

static irqreturn_t decon_te_irq_handler(int irq, void *dev_id)
{
	struct decon_context *ctx = dev_id;

	if (ctx->out_type & I80_HW_TRG)
		return IRQ_HANDLED;

	decon_set_bits(ctx, DECON_TRIGCON, TRIGCON_SWTRIGCMD, ~0);

	return IRQ_HANDLED;
}

static void decon_clear_channels(struct exynos_drm_crtc *crtc)
{
	struct decon_context *ctx = crtc->ctx;
	int win, i, ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	for (i = 0; i < ARRAY_SIZE(decon_clks_name); i++) {
		ret = clk_prepare_enable(ctx->clks[i]);
		if (ret < 0)
			goto err;
	}

	decon_shadow_protect(ctx, true);
	for (win = 0; win < WINDOWS_NR; win++)
		decon_set_bits(ctx, DECON_WINCONx(win), WINCONx_ENWIN_F, 0);
	decon_shadow_protect(ctx, false);

	decon_set_bits(ctx, DECON_UPDATE, STANDALONE_UPDATE_F, ~0);

	/* TODO: wait for possible vsync */
	msleep(50);

err:
	while (--i >= 0)
		clk_disable_unprepare(ctx->clks[i]);
}

static const struct exynos_drm_crtc_ops decon_crtc_ops = {
	.enable			= decon_enable,
	.disable		= decon_disable,
	.enable_vblank		= decon_enable_vblank,
	.disable_vblank		= decon_disable_vblank,
	.get_vblank_counter	= decon_get_vblank_counter,
	.atomic_begin		= decon_atomic_begin,
	.update_plane		= decon_update_plane,
	.disable_plane		= decon_disable_plane,
	.atomic_flush		= decon_atomic_flush,
};

static int decon_bind(struct device *dev, struct device *master, void *data)
{
	struct decon_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_plane *exynos_plane;
	enum exynos_drm_output_type out_type;
	unsigned int win;
	int ret;

	ctx->drm_dev = drm_dev;
	drm_dev->max_vblank_count = 0xffffffff;

	for (win = ctx->first_win; win < WINDOWS_NR; win++) {
		int tmp = (win == ctx->first_win) ? 0 : win;

		ctx->configs[win].pixel_formats = decon_formats;
		ctx->configs[win].num_pixel_formats = ARRAY_SIZE(decon_formats);
		ctx->configs[win].zpos = win;
		ctx->configs[win].type = decon_win_types[tmp];

		ret = exynos_plane_init(drm_dev, &ctx->planes[win], win,
					&ctx->configs[win]);
		if (ret)
			return ret;
	}

	exynos_plane = &ctx->planes[ctx->first_win];
	out_type = (ctx->out_type & IFTYPE_HDMI) ? EXYNOS_DISPLAY_TYPE_HDMI
						  : EXYNOS_DISPLAY_TYPE_LCD;
	ctx->crtc = exynos_drm_crtc_create(drm_dev, &exynos_plane->base,
			out_type, &decon_crtc_ops, ctx);
	if (IS_ERR(ctx->crtc))
		return PTR_ERR(ctx->crtc);

	decon_clear_channels(ctx->crtc);

	return drm_iommu_attach_device(drm_dev, dev);
}

static void decon_unbind(struct device *dev, struct device *master, void *data)
{
	struct decon_context *ctx = dev_get_drvdata(dev);

	decon_disable(ctx->crtc);

	/* detach this sub driver from iommu mapping if supported. */
	drm_iommu_detach_device(ctx->drm_dev, ctx->dev);
}

static const struct component_ops decon_component_ops = {
	.bind	= decon_bind,
	.unbind = decon_unbind,
};

static void decon_handle_vblank(struct decon_context *ctx)
{
	u32 frm;

	spin_lock(&ctx->vblank_lock);

	frm = decon_get_frame_count(ctx, true);

	if (frm != ctx->frame_id) {
		/* handle only if incremented, take care of wrap-around */
		if ((s32)(frm - ctx->frame_id) > 0)
			drm_crtc_handle_vblank(&ctx->crtc->base);
		ctx->frame_id = frm;
	}

	spin_unlock(&ctx->vblank_lock);
}

static irqreturn_t decon_irq_handler(int irq, void *dev_id)
{
	struct decon_context *ctx = dev_id;
	u32 val;

	val = readl(ctx->addr + DECON_VIDINTCON1);
	val &= VIDINTCON1_INTFRMDONEPEND | VIDINTCON1_INTFRMPEND;

	if (val) {
		writel(val, ctx->addr + DECON_VIDINTCON1);
		if (ctx->out_type & IFTYPE_HDMI) {
			val = readl(ctx->addr + DECON_VIDOUTCON0);
			val &= VIDOUT_INTERLACE_EN_F | VIDOUT_INTERLACE_FIELD_F;
			if (val ==
			    (VIDOUT_INTERLACE_EN_F | VIDOUT_INTERLACE_FIELD_F))
				return IRQ_HANDLED;
		}
		decon_handle_vblank(ctx);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_PM
static int exynos5433_decon_suspend(struct device *dev)
{
	struct decon_context *ctx = dev_get_drvdata(dev);
	int i = ARRAY_SIZE(decon_clks_name);

	while (--i >= 0)
		clk_disable_unprepare(ctx->clks[i]);

	return 0;
}

static int exynos5433_decon_resume(struct device *dev)
{
	struct decon_context *ctx = dev_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(decon_clks_name); i++) {
		ret = clk_prepare_enable(ctx->clks[i]);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	while (--i >= 0)
		clk_disable_unprepare(ctx->clks[i]);

	return ret;
}
#endif

static const struct dev_pm_ops exynos5433_decon_pm_ops = {
	SET_RUNTIME_PM_OPS(exynos5433_decon_suspend, exynos5433_decon_resume,
			   NULL)
};

static const struct of_device_id exynos5433_decon_driver_dt_match[] = {
	{
		.compatible = "samsung,exynos5433-decon",
		.data = (void *)I80_HW_TRG
	},
	{
		.compatible = "samsung,exynos5433-decon-tv",
		.data = (void *)(I80_HW_TRG | IFTYPE_HDMI)
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos5433_decon_driver_dt_match);

static int decon_conf_irq(struct decon_context *ctx, const char *name,
		irq_handler_t handler, unsigned long int flags, bool required)
{
	struct platform_device *pdev = to_platform_device(ctx->dev);
	int ret, irq = platform_get_irq_byname(pdev, name);

	if (irq < 0) {
		if (irq == -EPROBE_DEFER)
			return irq;
		if (required)
			dev_err(ctx->dev, "cannot get %s IRQ\n", name);
		else
			irq = 0;
		return irq;
	}
	irq_set_status_flags(irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(ctx->dev, irq, handler, flags, "drm_decon", ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "IRQ %s request failed\n", name);
		return ret;
	}

	return irq;
}

static int exynos5433_decon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct decon_context *ctx;
	struct resource *res;
	int ret;
	int i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->out_type = (unsigned long)of_device_get_match_data(dev);
	spin_lock_init(&ctx->vblank_lock);

	if (ctx->out_type & IFTYPE_HDMI) {
		ctx->first_win = 1;
	} else if (of_get_child_by_name(dev->of_node, "i80-if-timings")) {
		ctx->out_type |= IFTYPE_I80;
	}

	for (i = 0; i < ARRAY_SIZE(decon_clks_name); i++) {
		struct clk *clk;

		clk = devm_clk_get(ctx->dev, decon_clks_name[i]);
		if (IS_ERR(clk))
			return PTR_ERR(clk);

		ctx->clks[i] = clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		return -ENXIO;
	}

	ctx->addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->addr)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(ctx->addr);
	}

	if (ctx->out_type & IFTYPE_I80) {
		ret = decon_conf_irq(ctx, "lcd_sys", decon_irq_handler, 0, true);
		if (ret < 0)
			return ret;
		ctx->irq = ret;

		ret = decon_conf_irq(ctx, "te", decon_te_irq_handler,
				     IRQF_TRIGGER_RISING, false);
		if (ret < 0)
			return ret;
		if (ret) {
			ctx->te_irq = ret;
			ctx->out_type &= ~I80_HW_TRG;
		}
	} else {
		ret = decon_conf_irq(ctx, "vsync", decon_irq_handler, 0, true);
		if (ret < 0)
			return ret;
		ctx->irq = ret;
	}

	if (ctx->out_type & I80_HW_TRG) {
		ctx->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
							"samsung,disp-sysreg");
		if (IS_ERR(ctx->sysreg)) {
			dev_err(dev, "failed to get system register\n");
			return PTR_ERR(ctx->sysreg);
		}
	}

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);

	ret = component_add(dev, &decon_component_ops);
	if (ret)
		goto err_disable_pm_runtime;

	return 0;

err_disable_pm_runtime:
	pm_runtime_disable(dev);

	return ret;
}

static int exynos5433_decon_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	component_del(&pdev->dev, &decon_component_ops);

	return 0;
}

struct platform_driver exynos5433_decon_driver = {
	.probe		= exynos5433_decon_probe,
	.remove		= exynos5433_decon_remove,
	.driver		= {
		.name	= "exynos5433-decon",
		.pm	= &exynos5433_decon_pm_ops,
		.of_match_table = exynos5433_decon_driver_dt_match,
	},
};
