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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/samsung_fimd.h>
#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_plane.h"
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

#define VIDWnALPHA0(win)	(VIDW_ALPHA + 0x00 + (win) * 8)
#define VIDWnALPHA1(win)	(VIDW_ALPHA + 0x04 + (win) * 8)

#define VIDWx_BUF_START(win, buf)	(VIDW_BUF_START(buf) + (win) * 8)
#define VIDWx_BUF_END(win, buf)		(VIDW_BUF_END(buf) + (win) * 8)
#define VIDWx_BUF_SIZE(win, buf)	(VIDW_BUF_SIZE(buf) + (win) * 4)

/* color key control register for hardware window 1 ~ 4. */
#define WKEYCON0_BASE(x)		((WKEYCON0 + 0x140) + ((x - 1) * 8))
/* color key value register for hardware window 1 ~ 4. */
#define WKEYCON1_BASE(x)		((WKEYCON1 + 0x140) + ((x - 1) * 8))

/* I80 / RGB trigger control register */
#define TRIGCON				0x1A4
#define TRGMODE_I80_RGB_ENABLE_I80	(1 << 0)
#define SWTRGCMD_I80_RGB_ENABLE		(1 << 1)

/* display mode change control register except exynos4 */
#define VIDOUT_CON			0x000
#define VIDOUT_CON_F_I80_LDI0		(0x2 << 8)

/* I80 interface control for main LDI register */
#define I80IFCONFAx(x)			(0x1B0 + (x) * 4)
#define I80IFCONFBx(x)			(0x1B8 + (x) * 4)
#define LCD_CS_SETUP(x)			((x) << 16)
#define LCD_WR_SETUP(x)			((x) << 12)
#define LCD_WR_ACTIVE(x)		((x) << 8)
#define LCD_WR_HOLD(x)			((x) << 4)
#define I80IFEN_ENABLE			(1 << 0)

/* FIMD has totally five hardware windows. */
#define WINDOWS_NR	5

struct fimd_driver_data {
	unsigned int timing_base;
	unsigned int lcdblk_offset;
	unsigned int lcdblk_vt_shift;
	unsigned int lcdblk_bypass_shift;

	unsigned int has_shadowcon:1;
	unsigned int has_clksel:1;
	unsigned int has_limited_fmt:1;
	unsigned int has_vidoutcon:1;
	unsigned int has_vtsel:1;
};

static struct fimd_driver_data s3c64xx_fimd_driver_data = {
	.timing_base = 0x0,
	.has_clksel = 1,
	.has_limited_fmt = 1,
};

static struct fimd_driver_data exynos3_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x210,
	.lcdblk_bypass_shift = 1,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
};

static struct fimd_driver_data exynos4_fimd_driver_data = {
	.timing_base = 0x0,
	.lcdblk_offset = 0x210,
	.lcdblk_vt_shift = 10,
	.lcdblk_bypass_shift = 1,
	.has_shadowcon = 1,
	.has_vtsel = 1,
};

static struct fimd_driver_data exynos4415_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x210,
	.lcdblk_vt_shift = 10,
	.lcdblk_bypass_shift = 1,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
	.has_vtsel = 1,
};

static struct fimd_driver_data exynos5_fimd_driver_data = {
	.timing_base = 0x20000,
	.lcdblk_offset = 0x214,
	.lcdblk_vt_shift = 24,
	.lcdblk_bypass_shift = 15,
	.has_shadowcon = 1,
	.has_vidoutcon = 1,
	.has_vtsel = 1,
};

struct fimd_context {
	struct device			*dev;
	struct drm_device		*drm_dev;
	struct exynos_drm_crtc		*crtc;
	struct exynos_drm_plane		planes[WINDOWS_NR];
	struct clk			*bus_clk;
	struct clk			*lcd_clk;
	void __iomem			*regs;
	struct regmap			*sysreg;
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;
	u32				vidout_con;
	u32				i80ifcon;
	bool				i80_if;
	bool				suspended;
	int				pipe;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;
	atomic_t			win_updated;
	atomic_t			triggering;

	struct exynos_drm_panel_info panel;
	struct fimd_driver_data *driver_data;
	struct exynos_drm_display *display;
};

static const struct of_device_id fimd_driver_dt_match[] = {
	{ .compatible = "samsung,s3c6400-fimd",
	  .data = &s3c64xx_fimd_driver_data },
	{ .compatible = "samsung,exynos3250-fimd",
	  .data = &exynos3_fimd_driver_data },
	{ .compatible = "samsung,exynos4210-fimd",
	  .data = &exynos4_fimd_driver_data },
	{ .compatible = "samsung,exynos4415-fimd",
	  .data = &exynos4415_fimd_driver_data },
	{ .compatible = "samsung,exynos5250-fimd",
	  .data = &exynos5_fimd_driver_data },
	{},
};
MODULE_DEVICE_TABLE(of, fimd_driver_dt_match);

static inline struct fimd_driver_data *drm_fimd_get_driver_data(
	struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(fimd_driver_dt_match, &pdev->dev);

	return (struct fimd_driver_data *)of_id->data;
}

static int fimd_enable_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	u32 val;

	if (ctx->suspended)
		return -EPERM;

	if (!test_and_set_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val |= VIDINTCON0_INT_ENABLE;

		if (ctx->i80_if) {
			val |= VIDINTCON0_INT_I80IFDONE;
			val |= VIDINTCON0_INT_SYSMAINCON;
			val &= ~VIDINTCON0_INT_SYSSUBCON;
		} else {
			val |= VIDINTCON0_INT_FRAME;

			val &= ~VIDINTCON0_FRAMESEL0_MASK;
			val |= VIDINTCON0_FRAMESEL0_VSYNC;
			val &= ~VIDINTCON0_FRAMESEL1_MASK;
			val |= VIDINTCON0_FRAMESEL1_NONE;
		}

		writel(val, ctx->regs + VIDINTCON0);
	}

	return 0;
}

static void fimd_disable_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	u32 val;

	if (ctx->suspended)
		return;

	if (test_and_clear_bit(0, &ctx->irq_flags)) {
		val = readl(ctx->regs + VIDINTCON0);

		val &= ~VIDINTCON0_INT_ENABLE;

		if (ctx->i80_if) {
			val &= ~VIDINTCON0_INT_I80IFDONE;
			val &= ~VIDINTCON0_INT_SYSMAINCON;
			val &= ~VIDINTCON0_INT_SYSSUBCON;
		} else
			val &= ~VIDINTCON0_INT_FRAME;

		writel(val, ctx->regs + VIDINTCON0);
	}
}

static void fimd_wait_for_vblank(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

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

static void fimd_enable_video_output(struct fimd_context *ctx, unsigned int win,
					bool enable)
{
	u32 val = readl(ctx->regs + WINCON(win));

	if (enable)
		val |= WINCONx_ENWIN;
	else
		val &= ~WINCONx_ENWIN;

	writel(val, ctx->regs + WINCON(win));
}

static void fimd_enable_shadow_channel_path(struct fimd_context *ctx,
						unsigned int win,
						bool enable)
{
	u32 val = readl(ctx->regs + SHADOWCON);

	if (enable)
		val |= SHADOWCON_CHx_ENABLE(win);
	else
		val &= ~SHADOWCON_CHx_ENABLE(win);

	writel(val, ctx->regs + SHADOWCON);
}

static void fimd_clear_channels(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	unsigned int win, ch_enabled = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* Hardware is in unknown state, so ensure it gets enabled properly */
	pm_runtime_get_sync(ctx->dev);

	clk_prepare_enable(ctx->bus_clk);
	clk_prepare_enable(ctx->lcd_clk);

	/* Check if any channel is enabled. */
	for (win = 0; win < WINDOWS_NR; win++) {
		u32 val = readl(ctx->regs + WINCON(win));

		if (val & WINCONx_ENWIN) {
			fimd_enable_video_output(ctx, win, false);

			if (ctx->driver_data->has_shadowcon)
				fimd_enable_shadow_channel_path(ctx, win,
								false);

			ch_enabled = 1;
		}
	}

	/* Wait for vsync, as disable channel takes effect at next vsync */
	if (ch_enabled) {
		int pipe = ctx->pipe;

		/* ensure that vblank interrupt won't be reported to core */
		ctx->suspended = false;
		ctx->pipe = -1;

		fimd_enable_vblank(ctx->crtc);
		fimd_wait_for_vblank(ctx->crtc);
		fimd_disable_vblank(ctx->crtc);

		ctx->suspended = true;
		ctx->pipe = pipe;
	}

	clk_disable_unprepare(ctx->lcd_clk);
	clk_disable_unprepare(ctx->bus_clk);

	pm_runtime_put(ctx->dev);
}

static u32 fimd_calc_clkdiv(struct fimd_context *ctx,
		const struct drm_display_mode *mode)
{
	unsigned long ideal_clk = mode->htotal * mode->vtotal * mode->vrefresh;
	u32 clkdiv;

	if (ctx->i80_if) {
		/*
		 * The frame done interrupt should be occurred prior to the
		 * next TE signal.
		 */
		ideal_clk *= 2;
	}

	/* Find the clock divider value that gets us closest to ideal_clk */
	clkdiv = DIV_ROUND_UP(clk_get_rate(ctx->lcd_clk), ideal_clk);

	return (clkdiv < 0x100) ? clkdiv : 0xff;
}

static bool fimd_mode_fixup(struct exynos_drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	if (adjusted_mode->vrefresh == 0)
		adjusted_mode->vrefresh = FIMD_DEFAULT_FRAMERATE;

	return true;
}

static void fimd_commit(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	struct drm_display_mode *mode = &crtc->base.state->adjusted_mode;
	struct fimd_driver_data *driver_data = ctx->driver_data;
	void *timing_base = ctx->regs + driver_data->timing_base;
	u32 val, clkdiv;

	if (ctx->suspended)
		return;

	/* nothing to do if we haven't set the mode yet */
	if (mode->htotal == 0 || mode->vtotal == 0)
		return;

	if (ctx->i80_if) {
		val = ctx->i80ifcon | I80IFEN_ENABLE;
		writel(val, timing_base + I80IFCONFAx(0));

		/* disable auto frame rate */
		writel(0, timing_base + I80IFCONFBx(0));

		/* set video type selection to I80 interface */
		if (driver_data->has_vtsel && ctx->sysreg &&
				regmap_update_bits(ctx->sysreg,
					driver_data->lcdblk_offset,
					0x3 << driver_data->lcdblk_vt_shift,
					0x1 << driver_data->lcdblk_vt_shift)) {
			DRM_ERROR("Failed to update sysreg for I80 i/f.\n");
			return;
		}
	} else {
		int vsync_len, vbpd, vfpd, hsync_len, hbpd, hfpd;
		u32 vidcon1;

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
	}

	if (driver_data->has_vidoutcon)
		writel(ctx->vidout_con, timing_base + VIDOUT_CON);

	/* set bypass selection */
	if (ctx->sysreg && regmap_update_bits(ctx->sysreg,
				driver_data->lcdblk_offset,
				0x1 << driver_data->lcdblk_bypass_shift,
				0x1 << driver_data->lcdblk_bypass_shift)) {
		DRM_ERROR("Failed to update sysreg for bypass setting.\n");
		return;
	}

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
	val = ctx->vidcon0;
	val |= VIDCON0_ENVID | VIDCON0_ENVID_F;

	if (ctx->driver_data->has_clksel)
		val |= VIDCON0_CLKSEL_LCD;

	clkdiv = fimd_calc_clkdiv(ctx, mode);
	if (clkdiv > 1)
		val |= VIDCON0_CLKVAL_F(clkdiv - 1) | VIDCON0_CLKDIR;

	writel(val, ctx->regs + VIDCON0);
}


static void fimd_win_set_pixfmt(struct fimd_context *ctx, unsigned int win)
{
	struct exynos_drm_plane *plane = &ctx->planes[win];
	unsigned long val;

	val = WINCONx_ENWIN;

	/*
	 * In case of s3c64xx, window 0 doesn't support alpha channel.
	 * So the request format is ARGB8888 then change it to XRGB8888.
	 */
	if (ctx->driver_data->has_limited_fmt && !win) {
		if (plane->pixel_format == DRM_FORMAT_ARGB8888)
			plane->pixel_format = DRM_FORMAT_XRGB8888;
	}

	switch (plane->pixel_format) {
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

	DRM_DEBUG_KMS("bpp = %d\n", plane->bpp);

	/*
	 * In case of exynos, setting dma-burst to 16Word causes permanent
	 * tearing for very small buffers, e.g. cursor buffer. Burst Mode
	 * switching which is based on plane size is not recommended as
	 * plane size varies alot towards the end of the screen and rapid
	 * movement causes unstable DMA which results into iommu crash/tear.
	 */

	if (plane->fb_width < MIN_FB_WIDTH_FOR_16WORD_BURST) {
		val &= ~WINCONx_BURSTLEN_MASK;
		val |= WINCONx_BURSTLEN_4WORD;
	}

	writel(val, ctx->regs + WINCON(win));

	/* hardware window 0 doesn't support alpha channel. */
	if (win != 0) {
		/* OSD alpha */
		val = VIDISD14C_ALPHA0_R(0xf) |
			VIDISD14C_ALPHA0_G(0xf) |
			VIDISD14C_ALPHA0_B(0xf) |
			VIDISD14C_ALPHA1_R(0xf) |
			VIDISD14C_ALPHA1_G(0xf) |
			VIDISD14C_ALPHA1_B(0xf);

		writel(val, ctx->regs + VIDOSD_C(win));

		val = VIDW_ALPHA_R(0xf) | VIDW_ALPHA_G(0xf) |
			VIDW_ALPHA_G(0xf);
		writel(val, ctx->regs + VIDWnALPHA0(win));
		writel(val, ctx->regs + VIDWnALPHA1(win));
	}
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
				    unsigned int win, bool protect)
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

static void fimd_win_commit(struct exynos_drm_crtc *crtc, unsigned int win)
{
	struct fimd_context *ctx = crtc->ctx;
	struct exynos_drm_plane *plane;
	dma_addr_t dma_addr;
	unsigned long val, size, offset;
	unsigned int last_x, last_y, buf_offsize, line_size;

	if (ctx->suspended)
		return;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	plane = &ctx->planes[win];

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


	offset = plane->src_x * (plane->bpp >> 3);
	offset += plane->src_y * plane->pitch;

	/* buffer start address */
	dma_addr = plane->dma_addr[0] + offset;
	val = (unsigned long)dma_addr;
	writel(val, ctx->regs + VIDWx_BUF_START(win, 0));

	/* buffer end address */
	size = plane->pitch * plane->crtc_height;
	val = (unsigned long)(dma_addr + size);
	writel(val, ctx->regs + VIDWx_BUF_END(win, 0));

	DRM_DEBUG_KMS("start addr = 0x%lx, end addr = 0x%lx, size = 0x%lx\n",
			(unsigned long)dma_addr, val, size);
	DRM_DEBUG_KMS("ovl_width = %d, ovl_height = %d\n",
			plane->crtc_width, plane->crtc_height);

	/* buffer size */
	buf_offsize = plane->pitch - (plane->crtc_width * (plane->bpp >> 3));
	line_size = plane->crtc_width * (plane->bpp >> 3);
	val = VIDW_BUF_SIZE_OFFSET(buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH(line_size) |
		VIDW_BUF_SIZE_OFFSET_E(buf_offsize) |
		VIDW_BUF_SIZE_PAGEWIDTH_E(line_size);
	writel(val, ctx->regs + VIDWx_BUF_SIZE(win, 0));

	/* OSD position */
	val = VIDOSDxA_TOPLEFT_X(plane->crtc_x) |
		VIDOSDxA_TOPLEFT_Y(plane->crtc_y) |
		VIDOSDxA_TOPLEFT_X_E(plane->crtc_x) |
		VIDOSDxA_TOPLEFT_Y_E(plane->crtc_y);
	writel(val, ctx->regs + VIDOSD_A(win));

	last_x = plane->crtc_x + plane->crtc_width;
	if (last_x)
		last_x--;
	last_y = plane->crtc_y + plane->crtc_height;
	if (last_y)
		last_y--;

	val = VIDOSDxB_BOTRIGHT_X(last_x) | VIDOSDxB_BOTRIGHT_Y(last_y) |
		VIDOSDxB_BOTRIGHT_X_E(last_x) | VIDOSDxB_BOTRIGHT_Y_E(last_y);

	writel(val, ctx->regs + VIDOSD_B(win));

	DRM_DEBUG_KMS("osd pos: tx = %d, ty = %d, bx = %d, by = %d\n",
			plane->crtc_x, plane->crtc_y, last_x, last_y);

	/* OSD size */
	if (win != 3 && win != 4) {
		u32 offset = VIDOSD_D(win);
		if (win == 0)
			offset = VIDOSD_C(win);
		val = plane->crtc_width * plane->crtc_height;
		writel(val, ctx->regs + offset);

		DRM_DEBUG_KMS("osd size = 0x%x\n", (unsigned int)val);
	}

	fimd_win_set_pixfmt(ctx, win);

	/* hardware window 0 doesn't support color key. */
	if (win != 0)
		fimd_win_set_colkey(ctx, win);

	fimd_enable_video_output(ctx, win, true);

	if (ctx->driver_data->has_shadowcon)
		fimd_enable_shadow_channel_path(ctx, win, true);

	/* Enable DMA channel and unprotect windows */
	fimd_shadow_protect_win(ctx, win, false);

	if (ctx->i80_if)
		atomic_set(&ctx->win_updated, 1);
}

static void fimd_win_disable(struct exynos_drm_crtc *crtc, unsigned int win)
{
	struct fimd_context *ctx = crtc->ctx;
	struct exynos_drm_plane *plane;

	if (win < 0 || win >= WINDOWS_NR)
		return;

	plane = &ctx->planes[win];

	if (ctx->suspended)
		return;

	/* protect windows */
	fimd_shadow_protect_win(ctx, win, true);

	fimd_enable_video_output(ctx, win, false);

	if (ctx->driver_data->has_shadowcon)
		fimd_enable_shadow_channel_path(ctx, win, false);

	/* unprotect windows */
	fimd_shadow_protect_win(ctx, win, false);
}

static void fimd_enable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	int ret;

	if (!ctx->suspended)
		return;

	ctx->suspended = false;

	pm_runtime_get_sync(ctx->dev);

	ret = clk_prepare_enable(ctx->bus_clk);
	if (ret < 0) {
		DRM_ERROR("Failed to prepare_enable the bus clk [%d]\n", ret);
		return;
	}

	ret = clk_prepare_enable(ctx->lcd_clk);
	if  (ret < 0) {
		DRM_ERROR("Failed to prepare_enable the lcd clk [%d]\n", ret);
		return;
	}

	/* if vblank was enabled status, enable it again. */
	if (test_and_clear_bit(0, &ctx->irq_flags))
		fimd_enable_vblank(ctx->crtc);

	fimd_commit(ctx->crtc);
}

static void fimd_disable(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;
	int i;

	if (ctx->suspended)
		return;

	/*
	 * We need to make sure that all windows are disabled before we
	 * suspend that connector. Otherwise we might try to scan from
	 * a destroyed buffer later.
	 */
	for (i = 0; i < WINDOWS_NR; i++)
		fimd_win_disable(crtc, i);

	fimd_enable_vblank(crtc);
	fimd_wait_for_vblank(crtc);
	fimd_disable_vblank(crtc);

	writel(0, ctx->regs + VIDCON0);

	clk_disable_unprepare(ctx->lcd_clk);
	clk_disable_unprepare(ctx->bus_clk);

	pm_runtime_put_sync(ctx->dev);

	ctx->suspended = true;
}

static void fimd_trigger(struct device *dev)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);
	struct fimd_driver_data *driver_data = ctx->driver_data;
	void *timing_base = ctx->regs + driver_data->timing_base;
	u32 reg;

	 /*
	  * Skips triggering if in triggering state, because multiple triggering
	  * requests can cause panel reset.
	  */
	if (atomic_read(&ctx->triggering))
		return;

	/* Enters triggering mode */
	atomic_set(&ctx->triggering, 1);

	reg = readl(timing_base + TRIGCON);
	reg |= (TRGMODE_I80_RGB_ENABLE_I80 | SWTRGCMD_I80_RGB_ENABLE);
	writel(reg, timing_base + TRIGCON);

	/*
	 * Exits triggering mode if vblank is not enabled yet, because when the
	 * VIDINTCON0 register is not set, it can not exit from triggering mode.
	 */
	if (!test_bit(0, &ctx->irq_flags))
		atomic_set(&ctx->triggering, 0);
}

static void fimd_te_handler(struct exynos_drm_crtc *crtc)
{
	struct fimd_context *ctx = crtc->ctx;

	/* Checks the crtc is detached already from encoder */
	if (ctx->pipe < 0 || !ctx->drm_dev)
		return;

	/*
	 * If there is a page flip request, triggers and handles the page flip
	 * event so that current fb can be updated into panel GRAM.
	 */
	if (atomic_add_unless(&ctx->win_updated, -1, 0))
		fimd_trigger(ctx->dev);

	/* Wakes up vsync event queue */
	if (atomic_read(&ctx->wait_vsync_event)) {
		atomic_set(&ctx->wait_vsync_event, 0);
		wake_up(&ctx->wait_vsync_queue);
	}

	if (test_bit(0, &ctx->irq_flags))
		drm_crtc_handle_vblank(&ctx->crtc->base);
}

static void fimd_dp_clock_enable(struct exynos_drm_crtc *crtc, bool enable)
{
	struct fimd_context *ctx = crtc->ctx;
	u32 val;

	/*
	 * Only Exynos 5250, 5260, 5410 and 542x requires enabling DP/MIE
	 * clock. On these SoCs the bootloader may enable it but any
	 * power domain off/on will reset it to disable state.
	 */
	if (ctx->driver_data != &exynos5_fimd_driver_data)
		return;

	val = enable ? DP_MIE_CLK_DP_ENABLE : DP_MIE_CLK_DISABLE;
	writel(DP_MIE_CLK_DP_ENABLE, ctx->regs + DP_MIE_CLKCON);
}

static const struct exynos_drm_crtc_ops fimd_crtc_ops = {
	.enable = fimd_enable,
	.disable = fimd_disable,
	.mode_fixup = fimd_mode_fixup,
	.commit = fimd_commit,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.wait_for_vblank = fimd_wait_for_vblank,
	.win_commit = fimd_win_commit,
	.win_disable = fimd_win_disable,
	.te_handler = fimd_te_handler,
	.clock_enable = fimd_dp_clock_enable,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	u32 val, clear_bit;

	val = readl(ctx->regs + VIDINTCON1);

	clear_bit = ctx->i80_if ? VIDINTCON1_INT_I80 : VIDINTCON1_INT_FRAME;
	if (val & clear_bit)
		writel(clear_bit, ctx->regs + VIDINTCON1);

	/* check the crtc is detached already from encoder */
	if (ctx->pipe < 0 || !ctx->drm_dev)
		goto out;

	if (ctx->i80_if) {
		exynos_drm_crtc_finish_pageflip(ctx->crtc);

		/* Exits triggering mode */
		atomic_set(&ctx->triggering, 0);
	} else {
		drm_crtc_handle_vblank(&ctx->crtc->base);
		exynos_drm_crtc_finish_pageflip(ctx->crtc);

		/* set wait vsync event to zero and wake up queue. */
		if (atomic_read(&ctx->wait_vsync_event)) {
			atomic_set(&ctx->wait_vsync_event, 0);
			wake_up(&ctx->wait_vsync_queue);
		}
	}

out:
	return IRQ_HANDLED;
}

static int fimd_bind(struct device *dev, struct device *master, void *data)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct exynos_drm_private *priv = drm_dev->dev_private;
	struct exynos_drm_plane *exynos_plane;
	enum drm_plane_type type;
	unsigned int zpos;
	int ret;

	ctx->drm_dev = drm_dev;
	ctx->pipe = priv->pipe++;

	for (zpos = 0; zpos < WINDOWS_NR; zpos++) {
		type = (zpos == ctx->default_win) ? DRM_PLANE_TYPE_PRIMARY :
						DRM_PLANE_TYPE_OVERLAY;
		ret = exynos_plane_init(drm_dev, &ctx->planes[zpos],
					1 << ctx->pipe, type, zpos);
		if (ret)
			return ret;
	}

	exynos_plane = &ctx->planes[ctx->default_win];
	ctx->crtc = exynos_drm_crtc_create(drm_dev, &exynos_plane->base,
					   ctx->pipe, EXYNOS_DISPLAY_TYPE_LCD,
					   &fimd_crtc_ops, ctx);
	if (IS_ERR(ctx->crtc))
		return PTR_ERR(ctx->crtc);

	if (ctx->display)
		exynos_drm_create_enc_conn(drm_dev, ctx->display);

	if (is_drm_iommu_supported(drm_dev))
		fimd_clear_channels(ctx->crtc);

	ret = drm_iommu_attach_device(drm_dev, dev);
	if (ret)
		priv->pipe--;

	return ret;
}

static void fimd_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct fimd_context *ctx = dev_get_drvdata(dev);

	fimd_disable(ctx->crtc);

	drm_iommu_detach_device(ctx->drm_dev, ctx->dev);

	if (ctx->display)
		exynos_dpi_remove(ctx->display);
}

static const struct component_ops fimd_component_ops = {
	.bind	= fimd_bind,
	.unbind = fimd_unbind,
};

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct device_node *i80_if_timings;
	struct resource *res;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->suspended = true;
	ctx->driver_data = drm_fimd_get_driver_data(pdev);

	if (of_property_read_bool(dev->of_node, "samsung,invert-vden"))
		ctx->vidcon1 |= VIDCON1_INV_VDEN;
	if (of_property_read_bool(dev->of_node, "samsung,invert-vclk"))
		ctx->vidcon1 |= VIDCON1_INV_VCLK;

	i80_if_timings = of_get_child_by_name(dev->of_node, "i80-if-timings");
	if (i80_if_timings) {
		u32 val;

		ctx->i80_if = true;

		if (ctx->driver_data->has_vidoutcon)
			ctx->vidout_con |= VIDOUT_CON_F_I80_LDI0;
		else
			ctx->vidcon0 |= VIDCON0_VIDOUT_I80_LDI0;
		/*
		 * The user manual describes that this "DSI_EN" bit is required
		 * to enable I80 24-bit data interface.
		 */
		ctx->vidcon0 |= VIDCON0_DSI_EN;

		if (of_property_read_u32(i80_if_timings, "cs-setup", &val))
			val = 0;
		ctx->i80ifcon = LCD_CS_SETUP(val);
		if (of_property_read_u32(i80_if_timings, "wr-setup", &val))
			val = 0;
		ctx->i80ifcon |= LCD_WR_SETUP(val);
		if (of_property_read_u32(i80_if_timings, "wr-active", &val))
			val = 1;
		ctx->i80ifcon |= LCD_WR_ACTIVE(val);
		if (of_property_read_u32(i80_if_timings, "wr-hold", &val))
			val = 0;
		ctx->i80ifcon |= LCD_WR_HOLD(val);
	}
	of_node_put(i80_if_timings);

	ctx->sysreg = syscon_regmap_lookup_by_phandle(dev->of_node,
							"samsung,sysreg");
	if (IS_ERR(ctx->sysreg)) {
		dev_warn(dev, "failed to get system register.\n");
		ctx->sysreg = NULL;
	}

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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctx->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
					   ctx->i80_if ? "lcd_sys" : "vsync");
	if (!res) {
		dev_err(dev, "irq request failed.\n");
		return -ENXIO;
	}

	ret = devm_request_irq(dev, res->start, fimd_irq_handler,
							0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		return ret;
	}

	init_waitqueue_head(&ctx->wait_vsync_queue);
	atomic_set(&ctx->wait_vsync_event, 0);

	platform_set_drvdata(pdev, ctx);

	ctx->display = exynos_dpi_probe(dev);
	if (IS_ERR(ctx->display)) {
		return PTR_ERR(ctx->display);
	}

	pm_runtime_enable(dev);

	ret = component_add(dev, &fimd_component_ops);
	if (ret)
		goto err_disable_pm_runtime;

	return ret;

err_disable_pm_runtime:
	pm_runtime_disable(dev);

	return ret;
}

static int fimd_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	component_del(&pdev->dev, &fimd_component_ops);

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
