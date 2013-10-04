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

#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/samsung_fimd.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_iommu.h"

/*
 * FIMD is stand for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

#define FIMD_DEFAULT_FRAMERATE 60

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

#define get_fimd_context(dev)	platform_get_drvdata(to_platform_device(dev))

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
	struct exynos_drm_subdrv	subdrv;
	int				irq;
	struct drm_crtc			*crtc;
	struct clk			*bus_clk;
	struct clk			*lcd_clk;
	void __iomem			*regs;
	struct fimd_win_data		win_data[WINDOWS_NR];
	unsigned int			clkdiv;
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;
	bool				suspended;
	struct mutex			lock;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;

	struct exynos_drm_panel_info panel;
	struct fimd_driver_data *driver_data;
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

static bool fimd_display_is_connected(struct device *dev)
{
	/* TODO. */

	return true;
}

static void *fimd_get_panel(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	return &ctx->panel;
}

static int fimd_check_mode(struct device *dev, struct drm_display_mode *mode)
{
	/* TODO. */

	return 0;
}

static int fimd_display_power_on(struct device *dev, int mode)
{
	/* TODO */

	return 0;
}

static struct exynos_drm_display_ops fimd_display_ops = {
	.type = EXYNOS_DISPLAY_TYPE_LCD,
	.is_connected = fimd_display_is_connected,
	.get_panel = fimd_get_panel,
	.check_mode = fimd_check_mode,
	.power_on = fimd_display_power_on,
};

static void fimd_dpms(struct device *subdrv_dev, int mode)
{
	struct fimd_context *ctx = get_fimd_context(subdrv_dev);

	DRM_DEBUG_KMS("%d\n", mode);

	mutex_lock(&ctx->lock);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		/*
		 * enable fimd hardware only if suspended status.
		 *
		 * P.S. fimd_dpms function would be called at booting time so
		 * clk_enable could be called double time.
		 */
		if (ctx->suspended)
			pm_runtime_get_sync(subdrv_dev);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		if (!ctx->suspended)
			pm_runtime_put_sync(subdrv_dev);
		break;
	default:
		DRM_DEBUG_KMS("unspecified mode %d\n", mode);
		break;
	}

	mutex_unlock(&ctx->lock);
}

static void fimd_apply(struct device *subdrv_dev)
{
	struct fimd_context *ctx = get_fimd_context(subdrv_dev);
	struct exynos_drm_manager *mgr = ctx->subdrv.manager;
	struct exynos_drm_manager_ops *mgr_ops = mgr->ops;
	struct exynos_drm_overlay_ops *ovl_ops = mgr->overlay_ops;
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		if (win_data->enabled && (ovl_ops && ovl_ops->commit))
			ovl_ops->commit(subdrv_dev, i);
	}

	if (mgr_ops && mgr_ops->commit)
		mgr_ops->commit(subdrv_dev);
}

static void fimd_commit(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct exynos_drm_panel_info *panel = &ctx->panel;
	struct videomode *vm = &panel->vm;
	struct fimd_driver_data *driver_data;
	u32 val;

	driver_data = ctx->driver_data;
	if (ctx->suspended)
		return;

	/* setup polarity values from machine code. */
	writel(ctx->vidcon1, ctx->regs + driver_data->timing_base + VIDCON1);

	/* setup vertical timing values. */
	val = VIDTCON0_VBPD(vm->vback_porch - 1) |
	       VIDTCON0_VFPD(vm->vfront_porch - 1) |
	       VIDTCON0_VSPW(vm->vsync_len - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON0);

	/* setup horizontal timing values.  */
	val = VIDTCON1_HBPD(vm->hback_porch - 1) |
	       VIDTCON1_HFPD(vm->hfront_porch - 1) |
	       VIDTCON1_HSPW(vm->hsync_len - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON1);

	/* setup horizontal and vertical display size. */
	val = VIDTCON2_LINEVAL(vm->vactive - 1) |
	       VIDTCON2_HOZVAL(vm->hactive - 1) |
	       VIDTCON2_LINEVAL_E(vm->vactive - 1) |
	       VIDTCON2_HOZVAL_E(vm->hactive - 1);
	writel(val, ctx->regs + driver_data->timing_base + VIDTCON2);

	/* setup clock source, clock divider, enable dma. */
	val = ctx->vidcon0;
	val &= ~(VIDCON0_CLKVAL_F_MASK | VIDCON0_CLKDIR);

	if (ctx->driver_data->has_clksel) {
		val &= ~VIDCON0_CLKSEL_MASK;
		val |= VIDCON0_CLKSEL_LCD;
	}

	if (ctx->clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(ctx->clkdiv - 1) | VIDCON0_CLKDIR;
	else
		val &= ~VIDCON0_CLKDIR;	/* 1:1 clock */

	/*
	 * fields of register with prefix '_F' would be updated
	 * at vsync(same as dma start)
	 */
	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;
	writel(val, ctx->regs + VIDCON0);
}

static int fimd_enable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

static void fimd_disable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

static void fimd_wait_for_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	if (ctx->suspended)
		return;

	atomic_set(&ctx->wait_vsync_event, 1);

	/*
	 * wait for FIMD to signal VSYNC interrupt or return after
	 * timeout which is set to 50ms (refresh rate of 20).
	 */
	if (!wait_event_timeout(ctx->wait_vsync_queue,
				!atomic_read(&ctx->wait_vsync_event),
				DRM_HZ/20))
		DRM_DEBUG_KMS("vblank wait timed out.\n");
}

static struct exynos_drm_manager_ops fimd_manager_ops = {
	.dpms = fimd_dpms,
	.apply = fimd_apply,
	.commit = fimd_commit,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.wait_for_vblank = fimd_wait_for_vblank,
};

static void fimd_win_mode_set(struct device *dev,
			      struct exynos_drm_overlay *overlay)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win;
	unsigned long offset;

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
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

static void fimd_win_set_pixfmt(struct device *dev, unsigned int win)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

	writel(val, ctx->regs + WINCON(win));
}

static void fimd_win_set_colkey(struct device *dev, unsigned int win)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

static void fimd_win_commit(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

	fimd_win_set_pixfmt(dev, win);

	/* hardware window 0 doesn't support color key. */
	if (win != 0)
		fimd_win_set_colkey(dev, win);

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

static void fimd_win_disable(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
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

static struct exynos_drm_overlay_ops fimd_overlay_ops = {
	.mode_set = fimd_win_mode_set,
	.commit = fimd_win_commit,
	.disable = fimd_win_disable,
};

static struct exynos_drm_manager fimd_manager = {
	.pipe		= -1,
	.ops		= &fimd_manager_ops,
	.overlay_ops	= &fimd_overlay_ops,
	.display_ops	= &fimd_display_ops,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	struct exynos_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct exynos_drm_manager *manager = subdrv->manager;
	u32 val;

	val = readl(ctx->regs + VIDINTCON1);

	if (val & VIDINTCON1_INT_FRAME)
		/* VSYNC interrupt */
		writel(VIDINTCON1_INT_FRAME, ctx->regs + VIDINTCON1);

	/* check the crtc is detached already from encoder */
	if (manager->pipe < 0)
		goto out;

	drm_handle_vblank(drm_dev, manager->pipe);
	exynos_drm_crtc_finish_pageflip(drm_dev, manager->pipe);

	/* set wait vsync event to zero and wake up queue. */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		DRM_WAKEUP(&ctx->wait_vsync_queue);
	}
out:
	return IRQ_HANDLED;
}

static int fimd_subdrv_probe(struct drm_device *drm_dev, struct device *dev)
{
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
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_attach_device(drm_dev, dev);

	return 0;
}

static void fimd_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_detach_device(drm_dev, dev);
}

static int fimd_configure_clocks(struct fimd_context *ctx, struct device *dev)
{
	struct videomode *vm = &ctx->panel.vm;
	unsigned long clk;

	ctx->bus_clk = devm_clk_get(dev, "fimd");
	if (IS_ERR(ctx->bus_clk)) {
		dev_err(dev, "failed to get bus clock\n");
		return PTR_ERR(ctx->bus_clk);
	}

	ctx->lcd_clk = devm_clk_get(dev, "sclk_fimd");
	if (IS_ERR(ctx->lcd_clk)) {
		dev_err(dev, "failed to get lcd clock\n");
		return PTR_ERR(ctx->lcd_clk);
	}

	clk = clk_get_rate(ctx->lcd_clk);
	if (clk == 0) {
		dev_err(dev, "error getting sclk_fimd clock rate\n");
		return -EINVAL;
	}

	if (vm->pixelclock == 0) {
		unsigned long c;
		c = vm->hactive + vm->hback_porch + vm->hfront_porch +
		    vm->hsync_len;
		c *= vm->vactive + vm->vback_porch + vm->vfront_porch +
		     vm->vsync_len;
		vm->pixelclock = c * FIMD_DEFAULT_FRAMERATE;
		if (vm->pixelclock == 0) {
			dev_err(dev, "incorrect display timings\n");
			return -EINVAL;
		}
		dev_warn(dev, "pixel clock recalculated to %luHz (%dHz frame rate)\n",
			 vm->pixelclock, FIMD_DEFAULT_FRAMERATE);
	}
	ctx->clkdiv = DIV_ROUND_UP(clk, vm->pixelclock);
	if (ctx->clkdiv > 256) {
		dev_warn(dev, "calculated pixel clock divider too high (%u), lowered to 256\n",
			 ctx->clkdiv);
		ctx->clkdiv = 256;
	}
	vm->pixelclock = clk / ctx->clkdiv;
	DRM_DEBUG_KMS("pixel clock = %lu, clkdiv = %d\n", vm->pixelclock,
		      ctx->clkdiv);

	return 0;
}

static void fimd_clear_win(struct fimd_context *ctx, int win)
{
	writel(0, ctx->regs + WINCON(win));
	writel(0, ctx->regs + VIDOSD_A(win));
	writel(0, ctx->regs + VIDOSD_B(win));
	writel(0, ctx->regs + VIDOSD_C(win));

	if (win == 1 || win == 2)
		writel(0, ctx->regs + VIDOSD_D(win));

	fimd_shadow_protect_win(ctx, win, false);
}

static int fimd_clock(struct fimd_context *ctx, bool enable)
{
	if (enable) {
		int ret;

		ret = clk_prepare_enable(ctx->bus_clk);
		if (ret < 0)
			return ret;

		ret = clk_prepare_enable(ctx->lcd_clk);
		if  (ret < 0) {
			clk_disable_unprepare(ctx->bus_clk);
			return ret;
		}
	} else {
		clk_disable_unprepare(ctx->lcd_clk);
		clk_disable_unprepare(ctx->bus_clk);
	}

	return 0;
}

static void fimd_window_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->resume = win_data->enabled;
		fimd_win_disable(dev, i);
	}
	fimd_wait_for_vblank(dev);
}

static void fimd_window_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int i;

	for (i = 0; i < WINDOWS_NR; i++) {
		win_data = &ctx->win_data[i];
		win_data->enabled = win_data->resume;
		win_data->resume = false;
	}
}

static int fimd_activate(struct fimd_context *ctx, bool enable)
{
	struct device *dev = ctx->subdrv.dev;
	if (enable) {
		int ret;

		ret = fimd_clock(ctx, true);
		if (ret < 0)
			return ret;

		ctx->suspended = false;

		/* if vblank was enabled status, enable it again. */
		if (test_and_clear_bit(0, &ctx->irq_flags))
			fimd_enable_vblank(dev);

		fimd_window_resume(dev);
	} else {
		fimd_window_suspend(dev);

		fimd_clock(ctx, false);
		ctx->suspended = true;
	}

	return 0;
}

static int fimd_get_platform_data(struct fimd_context *ctx, struct device *dev)
{
	struct videomode *vm;
	int ret;

	vm = &ctx->panel.vm;
	ret = of_get_videomode(dev->of_node, vm, OF_USE_NATIVE_MODE);
	if (ret) {
		DRM_ERROR("failed: of_get_videomode() : %d\n", ret);
		return ret;
	}

	if (vm->flags & DISPLAY_FLAGS_VSYNC_LOW)
		ctx->vidcon1 |= VIDCON1_INV_VSYNC;
	if (vm->flags & DISPLAY_FLAGS_HSYNC_LOW)
		ctx->vidcon1 |= VIDCON1_INV_HSYNC;
	if (vm->flags & DISPLAY_FLAGS_DE_LOW)
		ctx->vidcon1 |= VIDCON1_INV_VDEN;
	if (vm->flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		ctx->vidcon1 |= VIDCON1_INV_VCLK;

	return 0;
}

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct exynos_drm_subdrv *subdrv;
	struct resource *res;
	int win;
	int ret = -EINVAL;

	if (!dev->of_node)
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = fimd_get_platform_data(ctx, dev);
	if (ret)
		return ret;

	ret = fimd_configure_clocks(ctx, dev);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctx->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "vsync");
	if (!res) {
		dev_err(dev, "irq request failed.\n");
		return -ENXIO;
	}

	ctx->irq = res->start;

	ret = devm_request_irq(dev, ctx->irq, fimd_irq_handler,
							0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		return ret;
	}

	ctx->driver_data = drm_fimd_get_driver_data(pdev);
	DRM_INIT_WAITQUEUE(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	subdrv = &ctx->subdrv;

	subdrv->dev = dev;
	subdrv->manager = &fimd_manager;
	subdrv->probe = fimd_subdrv_probe;
	subdrv->remove = fimd_subdrv_remove;

	mutex_init(&ctx->lock);

	platform_set_drvdata(pdev, ctx);

	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	for (win = 0; win < WINDOWS_NR; win++)
		fimd_clear_win(ctx, win);

	exynos_drm_subdrv_register(subdrv);

	return 0;
}

static int fimd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx = platform_get_drvdata(pdev);

	exynos_drm_subdrv_unregister(&ctx->subdrv);

	if (ctx->suspended)
		goto out;

	pm_runtime_set_suspended(dev);
	pm_runtime_put_sync(dev);

out:
	pm_runtime_disable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int fimd_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	/*
	 * do not use pm_runtime_suspend(). if pm_runtime_suspend() is
	 * called here, an error would be returned by that interface
	 * because the usage_count of pm runtime is more than 1.
	 */
	if (!pm_runtime_suspended(dev))
		return fimd_activate(ctx, false);

	return 0;
}

static int fimd_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	/*
	 * if entered to sleep when lcd panel was on, the usage_count
	 * of pm runtime would still be 1 so in this case, fimd driver
	 * should be on directly not drawing on pm runtime interface.
	 */
	if (!pm_runtime_suspended(dev)) {
		int ret;

		ret = fimd_activate(ctx, true);
		if (ret < 0)
			return ret;

		/*
		 * in case of dpms on(standby), fimd_apply function will
		 * be called by encoder's dpms callback to update fimd's
		 * registers but in case of sleep wakeup, it's not.
		 * so fimd_apply function should be called at here.
		 */
		fimd_apply(dev);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int fimd_runtime_suspend(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	return fimd_activate(ctx, false);
}

static int fimd_runtime_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	return fimd_activate(ctx, true);
}
#endif

static const struct dev_pm_ops fimd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimd_suspend, fimd_resume)
	SET_RUNTIME_PM_OPS(fimd_runtime_suspend, fimd_runtime_resume, NULL)
};

struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= fimd_remove,
	.driver		= {
		.name	= "exynos4-fb",
		.owner	= THIS_MODULE,
		.pm	= &fimd_pm_ops,
		.of_match_table = fimd_driver_dt_match,
	},
};
