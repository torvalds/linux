/* exynos_drm_fimd.c
 *
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors:
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Inki Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/component.h>

#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/samsung_fimd.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_iommu.h"

/*
 * FIMD stands for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

#define FIMD_DEFAULT_FRAMERATE 60
#define MIN_FB_WIDTH_FOR_16WORD_BURST 128

/* position control register for hardware window 0, 2 ~ 4.*/
#define VIDOSD_A(win)		(VIDOSD_BASE + 0x00 + (win) * 16)
#define VIDOSD_B(win)		(VIDOSD_BASE + 0x04 + (win) * 16)
/*
 * size control register for hardware windows 0 and alpha control register
 * for hardware windows 1 ~ 4
 */
#define VIDOSD_C(win)		(VIDOSD_BASE + 0x08 + (win) * 16)
/* size control register for hardware windows 1 ~ 2. */
#define VIDOSD_D(win)		(VIDOSD_BASE + 0x0C + (win) * 16)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON0 + 0x140) + ((x - 1) * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON1 + 0x140) + ((x - 1) * 8))

/* FIMD has totally five hardware windows. */
#define WINDOWS_NR	5

#define get_fimd_manager(mgr)	platform_get_drvdata(to_platform_device(dev))

struct fimd_driver_data {
	unsigned int timing_base;

	unsigned int has_shadowcon:1;
	unsigned int has_clksel:1;
	unsigned int has_limited_fmt:1;
};

static struct fimd_driver_data s3c64xx_fimd_driver_data = {
	.timing_base = 0x0,
	.has_clksel = 1,
	.has_limited_fmt = 1,
};

static struct fimd_driver_data exynos4_fimd_driver_data = {
	.timing_base = 0x0,
	.has_shadowcon = 1,
};

static struct fimd_driver_data exynos5_fimd_driver_data = {
	.timing_base = 0x20000,
	.has_shadowcon = 1,
};

struct fimd_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	unsigned int		pixel_format;
	dma_addr_t		dma_addr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
	bool			enabled;
	bool			resume;
};

struct fimd_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	struct clk			*bus_clk;
	struct clk			*lcd_clk;
	void __iomem			*regs;
	struct drm_display_mode		mode;
	struct fimd_win_data		win_data[WINDOWS_NR];
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon1;
	bool				suspended;
	int				pipe;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;

	struct exynos_drm_panel_info panel;
	struct fimd_driver_data *driver_data;
	struct exynos_drm_display *display;
};

static const struct of_device_id fimd_driver_dt_match[] = {
	{ .compatible = "samsung,s3c6400-fimd",
	  .data = &s3c64xx_fimd_driver_data },
	{ .compatible = "samsung,exynos4210-fimd",
	  .data = &exynos4_fimd_driver_data },
	{ .compatible = "samsung,exynos5250-fimd",
	  .data = &exynos5_fimd_driver_data },
	{},
};

static inline struct fimd_driver_data *drm_fimd_get_driver_data(
	struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(fimd_driver_dt_match, &pdev->dev);

	return (struct fimd_driver_data *)of_id->data;
}

static void fimd_wait_for_vblank(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	/*
	 * wait for FIMD to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");
}


static void fimd_clear_channel(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	int win, ch_enabled = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* Check if any channel is enabled. */
	for (win = 0; win < WINDOWS_NR; win++) {
		u32 val = readl(ctx->regs + SHADOWCON);
		if (val & SHADOWCON_CHx_ENABLE(win)) {
			val &= ~SHADOWCON_CHx_ENABLE(win);
			writel(val, ctx->regs + SHADOWCON);
			ch_enabled = 1;
		}
	}

	/* Wait for vsync, as disable channel takes effect at next vsync */
	if (ch_enabled)
		fimd_wait_for_vblank(mgr);
}

static int fimd_mgr_initialize(struct exynos_drm_manager *mgr,
			struct drm_device *drm_dev)
{
	struct fimd_context *ctx = mgr->ctx;
	struct exynos_drm_private *priv;
	priv = drm_dev->dev_private;

	mgr->drm_dev = ctx->drm_dev = drm_dev;
	mgr->pipe = ctx->pipe = priv->pipe++;

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = true;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = true;

	/* attach this sub driver to iommu mapping if supported. */
	if (is_drm_iommu_supported(ctx->drm_dev)) {
		/*
		 * If any channel is already active, iommu will throw
		 * a PAGE FAULT when enabled. So clear any channel if enabled.
		 */
		fimd_clear_channel(mgr);
		drm_iommu_attach_device(ctx->drm_dev, ctx->dev);
	}

	return 0;
}

static void fimd_mgr_remove(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;

	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(ctx->drm_dev))
		drm_iommu_detach_device(ctx->drm_dev, ctx->dev);
}

static u32 fimd_calc_clkdiv(struct fimd_context *ctx,
		const struct drm_display_mode *mode)
{
	unsigned long ideal_clk = mode->htotal * mode->vtotal * mode->vrefresh;
	u32 clkdiv;

	/* Find the clock divider value that gets us closest to ideal_clk */
	clkdiv = DIV_ROUND_UP(clk_get_rate(ctx->lcd_clk), ideal_clk);

	return (clkdiv < 0x100) ? clkdiv : 0xff;
}

static bool fimd_mode_fixup(struct exynos_drm_manager *mgr,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	if (adjusted_mode->vrefresh == 0)
		adjusted_mode->vrefresh = FIMD_DEFAULT_FRAMERATE;

	return true;
}

static void fimd_mode_set(struct exynos_drm_manager *mgr,
		const struct drm_display_mode *in_mode)
{
	struct fimd_context *ctx = mgr->ctx;

	drm_mode_copy(&ctx->mode, in_mode);
}

static void fimd_commit(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	struct drm_display_mode *mode = &ctx->mode;
	struct fimd_driver_data *driver_data;
	u32 val, clkdiv, vidcon1;
	int vsync_len, vbpd, vfpd, hsync_len, hbpd, hfpd;

	driver_data = ctx->driver_data;
	if (ctx->suspended)
		return;

	/* nothing to do if we haven't set the mode yet */
	if (mode->htotal == 0 || mode->vtotal == 0)
		return;

	/* setup polarity values */
	vidcon1 = ctx->vidcon1;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		vidcon1 |= VIDCON1_INV_VSYNC;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		vidcon1 |= VIDCON1_INV_HSYNC;
	writel(vidcon1, ctx->regs + driver_data->timing_base + VIDCON1);

	/* setup vertical timing values. */
	vsync_len = mode->crtc_vsync_end - mode->crtc_vsync_start;
	vbpd = mode->crtc_vtotal - mode->crtc_vsync_end;
	vfpd = mode->crtc_vsync_start - mode->crtc_vdisplay;

	val = VIDTCON0_VBPD(vbpd - 1) |
		VIDTCON0_VFPD(vfpd - 1) |
		VIDTCON0_VSPW(vsync_len - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON0);

	/* setup horizontal timing values.  */
	hsync_len = mode->crtc_hsync_end - mode->crtc_hsync_start;
	hbpd = mode->crtc_htotal - mode->crtc_hsync_end;
	hfpd = mode->crtc_hsync_start - mode->crtc_hdisplay;

	val = VIDTCON1_HBPD(hbpd - 1) |
		VIDTCON1_HFPD(hfpd - 1) |
		VIDTCON1_HSPW(hsync_len - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON1);

	/* setup horizontal and vertical display size. */
	val = VIDTCON2_LINEVAL(mode->vdisplay - 1) |
	       VIDTCON2_HOZVAL(mode->hdisplay - 1) |
	       VIDTCON2_LINEVAL_E(mode->vdisplay - 1) |
	       VIDTCON2_HOZVAL_E(mode->hdisplay - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON2);

	/*
	 * fields of register with prefix '_F' would be updated
	 * at vsync(same as dma start)
	 */
	val = VIDCON0_ENVID | VIDCON0_ENVID_F;

	if (ctx->driver_data->has_clksel)
		val |= VIDCON0_CLKSEL_LCD;

	clkdiv = fimd_calc_clkdiv(ctx, mode);
	if (clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(clkdiv - 1) | VIDCON0_CLKDIR;

	writel(val, ctx->regs + VIDCON0);
}

static int fimd_enable_vblank(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	u32 val;

	if (ctx->suspended)
		return -EPERM;

	if (!test_and_set_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val |= VIDINTCON0_INT_ENABLE;
		val |= VIDINTCON0_INT_FRAME;

		val &= ~VIDINTCON0_FRAMESEL0_MASK;
		val |= VIDINTCON0_FRAMESEL0_VSYNC;
		val &= ~VIDINTCON0_FRAMESEL1_MASK;
		val |= VIDINTCON0_FRAMESEL1_NONE;

		writel(val, ctx->regs + VIDINTCON0);
	}

	return 0;
}

static void fimd_disable_vblank(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	u32 val;

	if (ctx->suspended)
		return;

	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val &= ~VIDINTCON0_INT_FRAME;
		val &= ~VIDINTCON0_INT_ENABLE;

		writel(val, ctx->regs + VIDINTCON0);
	}
}

static void fimd_win_mode_set(struct exynos_drm_manager *mgr,
			struct exynos_drm_overlay *overlay)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int win;
	unsigned long offset;

	if (!overlay) {
		DRM_ERROR("overlay is NULL\n");
		return;
	}

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	offset = overlay->fb_x * (overlay->bpp >> 3);
	offset += overlay->fb_y * overlay->pitch;

	DRM_DEBUG_KMS("offset = 0x%lx, pitch = %x\n", offset, overlay->pitch);

	win_data = &ctx->win_data[win];

	win_data->offset_x = overlay->crtc_x;
	win_data->offset_y = overlay->crtc_y;
	win_data->ovl_width = overlay->crtc_width;
	win_data->ovl_height = overlay->crtc_height;
	win_data->fb_width = overlay->fb_width;
	win_data->fb_height = overlay->fb_height;
	win_data->dma_addr = overlay->dma_addr[0] + offset;
	win_data->bpp = overlay->bpp;
	win_data->pixel_format = overlay->pixel_format;
	win_data->buf_offsize = (overlay->fb_width - overlay->crtc_width) *
				(overlay->bpp >> 3);
	win_data->line_size = overlay->crtc_width * (overlay->bpp >> 3);

	DRM_DEBUG_KMS("offset_x = %d, offset_y = %d\n",
			win_data->offset_x, win_data->offset_y);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);
	DRM_DEBUG_KMS("paddr = 0x%lx\n", (unsigned long)win_data->dma_addr);
	DRM_DEBUG_KMS("fb_width = %d, crtc_width = %d\n",
			overlay->fb_width, overlay->crtc_width);
}

static void fimd_win_set_pixfmt(struct fimd_context *ctx, unsigned int win)
{
	struct fimd_win_data *win_data = &ctx->win_data[win];
	unsigned long val;

	val = WINCONx_ENWIN;

	/*
	 * In case of s3c64xx, window 0 doesn't support alpha channel.
	 * So the request format is ARGB8888 then change it to XRGB8888.
	 */
	if (ctx->driver_data->has_limited_fmt && !win) {
		if (win_data->pixel_format == DRM_FORMAT_ARGB8888)
			win_data->pixel_format = DRM_FORMAT_XRGB8888;
	}

	switch (win_data->pixel_format) {
	case DRM_FORMAT_C8:
		val |= WINCON0_BPPMODE_8BPP_PALETTE;
		val |= WINCONx_BURSTLEN_8WORD;
		val |= WINCONx_BYTSWP;
		break;
	case DRM_FORMAT_XRGB1555:
		val |= WINCON0_BPPMODE_16BPP_1555;
		val |= WINCONx_HAWSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_RGB565:
		val |= WINCON0_BPPMODE_16BPP_565;
		val |= WINCONx_HAWSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_XRGB8888:
		val |= WINCON0_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	case DRM_FORMAT_ARGB8888:
		val |= WINCON1_BPPMODE_25BPP_A1888
			| WINCON1_BLD_PIX | WINCON1_ALPHA_SEL;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	default:
		DRM_DEBUG_KMS("invalid pixel size so using unpacked 24bpp.\n");

		val |= WINCON0_BPPMODE_24BPP_888;
		val |= WINCONx_WSWP;
		val |= WINCONx_BURSTLEN_16WORD;
		break;
	}

	DRM_DEBUG_KMS("bpp = %d\n", win_data->bpp);

	/*
	 * In case of exynos, setting dma-burst to 16Word causes permanent
	 * tearing for very small buffers, e.g. cursor buffer. Burst Mode
	 * switching which is based on overlay size is not recommended as
	 * overlay size varies alot towards the end of the screen and rapid
	 * movement causes unstable DMA which results into iommu crash/tear.
	 */

	if (win_data->fb_width < MIN_FB_WIDTH_FOR_16WORD_BURST) {
		val &= ~WINCONx_BURSTLEN_MASK;
		val |= WINCONx_BURSTLEN_4WORD;
	}

	writel(val, ctx->regs + WINCON(win));
}

static void fimd_win_set_colkey(struct fimd_context *ctx, unsigned int win)
{
	unsigned int keycon0 = 0, keycon1 = 0;

	keycon0 = ~(WxKEYCON0_KEYBL_EN | WxKEYCON0_KEYEN_F |
			WxKEYCON0_DIRCON) | WxKEYCON0_COMPKEY(0);

	keycon1 = WxKEYCON1_COLVAL(0xffffffff);

	writel(keycon0, ctx->regs + WKEYCON0_BASE(win));
	writel(keycon1, ctx->regs + WKEYCON1_BASE(win));
}

/**
 * shadow_protect_win() - disable updating values from shadow registers at vsync
 *
 * @win: window to protect registers for
 * @protect: 1 to protect (disable updates)
 */
static void fimd_shadow_protect_win(struct fimd_context *ctx,
							int win, bool protect)
{
	u32 reg, bits, val;

	if (ctx->driver_data->has_shadowcon) {
		reg = SHADOWCON;
		bits = SHADOWCON_WINx_PROTECT(win);
	} else {
		reg = PRTCON;
		bits = PRTCON_PROTECT;
	}

	val = readl(ctx->regs + reg);
	if (protect)
		val |= bits;
	else
		val &= ~bits;
	writel(val, ctx->regs + reg);
}

static void fimd_win_commit(struct exynos_drm_manager *mgr, int zpos)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int win = zpos;
	unsigned long val, alpha, size;
	unsigned int last_x;
	unsigned int last_y;

	if (ctx->suspended)
		return;

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	/* If suspended, enable this on resume */
	if (ctx->suspended) {
		win_data->resume = true;
		return;
	}

	/*
	 * SHADOWCON/PRTCON register is used for enabling timing.
	 *
	 * for example, once only width value of a register is set,
	 * if the dma is started then fimd hardware could malfunction so
	 * with protect window setting, the register fields with prefix '_F'
	 * wouldn't be updated at vsync also but updated once unprotect window
	 * is set.
	 */

	/* protect windows */
	fimd_shadow_protect_win(ctx, win, true);

	/* buffer start address */
	val = (unsigned long)win_data->dma_addr;
	writel(val, ctx->regs + VIDWx_BUF_START(win, 0));

	/* buffer end address */
	size = win_data->fb_width * win_data->ovl_height * (win_data->bpp >> 3);
	val = (unsigned long)(win_data->dma_addr + size);
	writel(val, ctx->regs + VIDWx_BUF_END(win, 0));

	DRM_DEBUG_KMS("start addr = 0x%lx, end addr = 0x%lx, size = 0x%lx\n",
			(unsigned long)win_data->dma_addr, val, size);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			win_data->ovl_width, win_data->ovl_height);

	/* buffer size */
	val = VIDW_BUF_SIZE_OFFSET(win_data->buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH(win_data->line_size) |
		VIDW_BUF_SIZE_OFFSET_E(win_data->buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH_E(win_data->line_size);
	writel(val, ctx->regs + VIDWx_BUF_SIZE(win, 0));

	/* OSD position */
	val = VIDOSDxA_TOPLEFT_X(win_data->offset_x) |
		VIDOSDxA_TOPLEFT_Y(win_data->offset_y) |
		VIDOSDxA_TOPLEFT_X_E(win_data->offset_x) |
		VIDOSDxA_TOPLEFT_Y_E(win_data->offset_y);
	writel(val, ctx->regs + VIDOSD_A(win));

	last_x = win_data->offset_x + win_data->ovl_width;
	if (last_x)
		last_x--;
	last_y = win_data->offset_y + win_data->ovl_height;
	if (last_y)
		last_y--;

	val = VIDOSDxB_BOTRIGHT_X(last_x) | VIDOSDxB_BOTRIGHT_Y(last_y) |
		VIDOSDxB_BOTRIGHT_X_E(last_x) | VIDOSDxB_BOTRIGHT_Y_E(last_y);

	writel(val, ctx->regs + VIDOSD_B(win));

	DRM_DEBUG_KMS("osd pos: tx = %d, ty = %d, bx = %d, by = %d\n",
			win_data->offset_x, win_data->offset_y, last_x, last_y);

	/* hardware window 0 doesn't support alpha channel. */
	if (win != 0) {
		/* OSD alpha */
		alpha = VIDISD14C_ALPHA1_R(0xf) |
			VIDISD14C_ALPHA1_G(0xf) |
			VIDISD14C_ALPHA1_B(0xf);

		writel(alpha, ctx->regs + VIDOSD_C(win));
	}

	/* OSD size */
	if (win != 3 && win != 4) {
		u32 offset = VIDOSD_D(win);
		if (win == 0)
			offset = VIDOSD_C(win);
		val = win_data->ovl_width * win_data->ovl_height;
		writel(val, ctx->regs + offset);

		DRM_DEBUG_KMS("osd size = 0x%x\n", (unsigned int)val);
	}

	fimd_win_set_pixfmt(ctx, win);

	/* hardware window 0 doesn't support color key. */
	if (win != 0)
		fimd_win_set_colkey(ctx, win);

	/* wincon */
	val = readl(ctx->regs + WINCON(win));
	val |= WINCONx_ENWIN;
	writel(val, ctx->regs + WINCON(win));

	/* Enable DMA channel and unprotect windows */
	fimd_shadow_protect_win(ctx, win, false);

	if (ctx->driver_data->has_shadowcon) {
		val = readl(ctx->regs + SHADOWCON);
		val |= SHADOWCON_CHx_ENABLE(win);
		writel(val, ctx->regs + SHADOWCON);
	}

	win_data->enabled = true;
}

static void fimd_win_disable(struct exynos_drm_manager *mgr, int zpos)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int win = zpos;
	u32 val;

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	if (ctx->suspended) {
		/* do not resume this window*/
		win_data->resume = false;
		return;
	}

	/* protect windows */
	fimd_shadow_protect_win(ctx, win, true);

	/* wincon */
	val = readl(ctx->regs + WINCON(win));
	val &= ~WINCONx_ENWIN;
	writel(val, ctx->regs + WINCON(win));

	/* unprotect windows */
	if (ctx->driver_data->has_shadowcon) {
		val = readl(ctx->regs + SHADOWCON);
		val &= ~SHADOWCON_CHx_ENABLE(win);
		writel(val, ctx->regs + SHADOWCON);
	}

	fimd_shadow_protect_win(ctx, win, false);

	win_data->enabled = false;
}

static void fimd_window_suspend(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->resume = win_data->enabled;
		if (win_data->enabled)
			fimd_win_disable(mgr, i);
	}
	fimd_wait_for_vblank(mgr);
}

static void fimd_window_resume(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->enabled = win_data->resume;
		win_data->resume = false;
	}
}

static void fimd_apply(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		if (win_data->enabled)
			fimd_win_commit(mgr, i);
		else
			fimd_win_disable(mgr, i);
	}

	fimd_commit(mgr);
}

static int fimd_poweron(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;
	int ret;

	if (!ctx->suspended)
		return 0;

	ctx->suspended = false;

	pm_runtime_get_sync(ctx->dev);

	ret = clk_prepare_enable(ctx->bus_clk);
	if (ret < 0) {
		DRM_ERROR("Failed to prepare_enable the bus clk [%d]\n", ret);
		goto bus_clk_err;
	}

	ret = clk_prepare_enable(ctx->lcd_clk);
	if  (ret < 0) {
		DRM_ERROR("Failed to prepare_enable the lcd clk [%d]\n", ret);
		goto lcd_clk_err;
	}

	/* if vblank was enabled status, enable it again. */
	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		ret = fimd_enable_vblank(mgr);
		if (ret) {
			DRM_ERROR("Failed to re-enable vblank [%d]\n", ret);
			goto enable_vblank_err;
		}
	}

	fimd_window_resume(mgr);

	fimd_apply(mgr);

	return 0;

enable_vblank_err:
	clk_disable_unprepare(ctx->lcd_clk);
lcd_clk_err:
	clk_disable_unprepare(ctx->bus_clk);
bus_clk_err:
	ctx->suspended = true;
	return ret;
}

static int fimd_poweroff(struct exynos_drm_manager *mgr)
{
	struct fimd_context *ctx = mgr->ctx;

	if (ctx->suspended)
		return 0;

	/*
	 * We need to make sure that all windows are disabled before we
	 * suspend that connector. Otherwise we might try to scan from
	 * a destroyed buffer later.
	 */
	fimd_window_suspend(mgr);

	clk_disable_unprepare(ctx->lcd_clk);
	clk_disable_unprepare(ctx->bus_clk);

	pm_runtime_put_sync(ctx->dev);

	ctx->suspended = true;
	return 0;
}

static void fimd_dpms(struct exynos_drm_manager *mgr, int mode)
{
	DRM_DEBUG_KMS("%s, %d\n", __FILE__, mode);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		fimd_poweron(mgr);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		fimd_poweroff(mgr);
		break;
	default:
		DRM_DEBUG_KMS("unspecified mode %d\n", mode);
		break;
	}
}

static struct exynos_drm_manager_ops fimd_manager_ops = {
	.dpms = fimd_dpms,
	.mode_fixup = fimd_mode_fixup,
	.mode_set = fimd_mode_set,
	.commit = fimd_commit,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.wait_for_vblank = fimd_wait_for_vblank,
	.win_mode_set = fimd_win_mode_set,
	.win_commit = fimd_win_commit,
	.win_disable = fimd_win_disable,
};

static struct exynos_drm_manager fimd_manager = {
	.type = EXYNOS_DISPLAY_TYPE_LCD,
	.ops = &fimd_manager_ops,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	u32 val;

	val = readl(ctx->regs + VIDINTCON1);

	if (val & VIDINTCON1_INT_FRAME)
		/* VSYNC interrupt */
		writel(VIDINTCON1_INT_FRAME, ctx->regs + VIDINTCON1);

	/* check the crtc is detached already from encoder */
	if (ctx->pipe < 0 || !ctx->drm_dev)
		goto out;

	drm_handle_vblank(ctx->drm_dev, ctx->pipe);
	exynos_drm_crtc_finish_pageflip(ctx->drm_dev, ctx->pipe);

	/* set wait vsync event to zero and wake up queue. */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		wake_up(&ctx->wait_vsync_queue);
	}
out:
	return IRQ_HANDLED;
}

static int fimd_bind(struct device *dev, struct device *master, void *data)
{
	struct fimd_context *ctx = fimd_manager.ctx;
	struct drm_device *drm_dev = data;

	fimd_mgr_initialize(&fimd_manager, drm_dev);
	exynos_drm_crtc_create(&fimd_manager);
	if (ctx->display)
		exynos_drm_create_enc_conn(drm_dev, ctx->display);

	return 0;

}

static void fimd_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct exynos_drm_manager *mgr = dev_get_drvdata(dev);
	struct fimd_context *ctx = fimd_manager.ctx;
	struct drm_crtc *crtc = mgr->crtc;

	fimd_dpms(mgr, DRM_MODE_DPMS_OFF);

	if (ctx->display)
		exynos_dpi_remove(dev);

	fimd_mgr_remove(mgr);

	crtc->funcs->destroy(crtc);
}

static const struct component_ops fimd_component_ops = {
	.bind	= fimd_bind,
	.unbind = fimd_unbind,
};

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct resource *res;
	int ret = -EINVAL;

	ret = exynos_drm_component_add(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC,
					fimd_manager.type);
	if (ret)
		return ret;

	if (!dev->of_node) {
		ret = -ENODEV;
		goto err_del_component;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto err_del_component;
	}

	ctx->dev = dev;
	ctx->suspended = true;

	if (of_property_read_bool(dev->of_node, "samsung,invert-vden"))
		ctx->vidcon1 |= VIDCON1_INV_VDEN;
	if (of_property_read_bool(dev->of_node, "samsung,invert-vclk"))
		ctx->vidcon1 |= VIDCON1_INV_VCLK;

	ctx->bus_clk = devm_clk_get(dev, "fimd");
	if (IS_ERR(ctx->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		ret = PTR_ERR(ctx->bus_clk);
		goto err_del_component;
	}

	ctx->lcd_clk = devm_clk_get(dev, "sclk_fimd");
	if (IS_ERR(ctx->lcd_clk)) {
		dev_err(dev, "failed to get lcd clock\n");
		ret = PTR_ERR(ctx->lcd_clk);
		goto err_del_component;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctx->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->regs)) {
		ret = PTR_ERR(ctx->regs);
		goto err_del_component;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "vsync");
	if (!res) {
		dev_err(dev, "irq request failed.\n");
		ret = -ENXIO;
		goto err_del_component;
	}

	ret = devm_request_irq(dev, res->start, fimd_irq_handler,
							0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		goto err_del_component;
	}

	ctx->driver_data = drm_fimd_get_driver_data(pdev);
	init_waitqueue_head(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	platform_set_drvdata(pdev, &fimd_manager);

	fimd_manager.ctx = ctx;

	ctx->display = exynos_dpi_probe(dev);
	if (IS_ERR(ctx->display))
		return PTR_ERR(ctx->display);

	pm_runtime_enable(&pdev->dev);

	ret = component_add(&pdev->dev, &fimd_component_ops);
	if (ret)
		goto err_disable_pm_runtime;

	return ret;

err_disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);

err_del_component:
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);
	return ret;
}

static int fimd_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	component_del(&pdev->dev, &fimd_component_ops);
	exynos_drm_component_del(&pdev->dev, EXYNOS_DEVICE_TYPE_CRTC);

	return 0;
}

struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= fimd_remove,
	.driver		= {
		.name	= "exynos4-fb",
		.owner	= THIS_MODULE,
		.of_match_table = fimd_driver_dt_match,
	},
};
