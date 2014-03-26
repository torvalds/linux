/*
 * rk3188_drm_fimd.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <video/of_display_timing.h>
#include <drm/rockchip_drm.h>
#include <linux/rockchip/cpu.h>
#include <linux/rk_fb.h>
#include <video/display_timing.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_crtc.h"
#include "rockchip_drm_iommu.h"

/*
 * FIMD is stand for Fully Interactive Mobile Display and
 * as a display controller, it transfers contents drawn on memory
 * to a LCD Panel through Display Interfaces such as RGB or
 * CPU Interface.
 */

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
#define WINDOWS_NR	4

#define get_fimd_context(dev)	platform_get_drvdata(to_platform_device(dev))

struct fimd_driver_data {
	unsigned int timing_base;
};

static struct fimd_driver_data rockchip4_fimd_driver_data = {
	.timing_base = 0x0,
};

static struct fimd_driver_data rockchip5_fimd_driver_data = {
	.timing_base = 0x20000,
};

struct fimd_win_data {
	unsigned int		offset_x;
	unsigned int		offset_y;
	unsigned int		ovl_width;
	unsigned int		ovl_height;
	unsigned int		fb_width;
	unsigned int		fb_height;
	unsigned int		bpp;
	dma_addr_t		dma_addr;
	unsigned int		buf_offsize;
	unsigned int		line_size;	/* bytes */
	bool			enabled;
	bool			resume;
};

struct fimd_context {
	struct rockchip_drm_subdrv	subdrv;
	int				irq;
	struct drm_crtc			*crtc;
	struct clk			*pd;				//lcdc power domain
	struct clk			*hclk;				//lcdc AHP clk
	struct clk			*dclk;				//lcdc dclk
	struct clk			*aclk;				//lcdc share memory frequency
	void __iomem			*regs;
	struct fimd_win_data		win_data[WINDOWS_NR];
	unsigned int			clkdiv;
	unsigned int			default_win;
	unsigned long			irq_flags;
	u32				vidcon0;
	u32				vidcon1;
	int 				lcdc_id;
	bool				suspended;
	struct mutex			lock;
	wait_queue_head_t		wait_vsync_queue;
	atomic_t			wait_vsync_event;

	int clkon;
	void *regsbak;			//back up reg
	struct rockchip_drm_panel_info *panel;
	struct rk_screen *screen;
};

#include "rk3188_drm_fimd.h"
static int rk3188_lcdc_get_id(u32 phy_base)
{
	if (cpu_is_rk319x()) {
		if (phy_base == 0xffc40000)
			return 0;
		else if (phy_base == 0xffc50000)
			return 1;
		else
			return -EINVAL;
	} else if (cpu_is_rk3188()) {
		if (phy_base == 0x1010c000)
			return 0;
		else if (phy_base == 0x1010e000)
			return 1;
		else
			return -EINVAL;
	} else if (cpu_is_rk3026()) {
		if (phy_base == 0x1010e000)
			return 0;
		else if (phy_base == 0x01110000)
			return 1;
		else
			return -EINVAL;
	} else {
		pr_err("un supported platform \n");
		return -EINVAL;
	}

}


static bool fimd_display_is_connected(struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return true;
}

static void *fimd_get_panel(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return ctx->panel;
}

static int fimd_check_timing(struct device *dev, void *timing)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO. */

	return 0;
}

static int fimd_display_power_on(struct device *dev, int mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* TODO */

	return 0;
}

static struct rockchip_drm_display_ops fimd_display_ops = {
	.type = ROCKCHIP_DISPLAY_TYPE_LCD,
	.is_connected = fimd_display_is_connected,
	.get_panel = fimd_get_panel,
	.check_timing = fimd_check_timing,
	.power_on = fimd_display_power_on,
};

static void fimd_dpms(struct device *subdrv_dev, int mode)
{
	struct fimd_context *ctx = get_fimd_context(subdrv_dev);

	DRM_DEBUG_KMS("%s, %d\n", __FILE__, mode);

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
	struct rockchip_drm_manager *mgr = ctx->subdrv.manager;
	struct rockchip_drm_manager_ops *mgr_ops = mgr->ops;
	struct rockchip_drm_overlay_ops *ovl_ops = mgr->overlay_ops;
	struct fimd_win_data *win_data;
	int i;

	DRM_DEBUG_KMS("%s\n", __FILE__);

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
	struct rockchip_drm_panel_info *panel = ctx->panel;
	struct rk_screen *screen = ctx->screen;
	u16 right_margin = screen->mode.right_margin;
	u16 left_margin = screen->mode.left_margin;
	u16 lower_margin = screen->mode.lower_margin;
	u16 upper_margin = screen->mode.upper_margin;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	int face;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if (ctx->suspended)
		return;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if(!ctx->clkon)
		return;
#if 1
	switch (screen->face) {
		case OUT_P565:
			face = OUT_P565;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);
			break;
		case OUT_P666:
			face = OUT_P666;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);
			break;
		case OUT_P888:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
			val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
			lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);
			break;
		default:
			printk( "un supported interface!\n");
			break;
	}
	mask = m_DSP_OUT_FORMAT | m_HSYNC_POL | m_VSYNC_POL |
		m_DEN_POL | m_DCLK_POL;
	val = v_DSP_OUT_FORMAT(face) | v_HSYNC_POL(screen->pin_hsync) |
		v_VSYNC_POL(screen->pin_vsync) | v_DEN_POL(screen->pin_den) |
		v_DCLK_POL(screen->pin_dclk);
	lcdc_msk_reg(ctx, DSP_CTRL0, mask, val);

	mask = m_BG_COLOR | m_DSP_BG_SWAP | m_DSP_RB_SWAP |
		m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		m_DSP_DUMMY_SWAP | m_BLANK_EN;
	val = v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_gb) |
		v_DSP_RB_SWAP(screen->swap_rb) | v_DSP_RG_SWAP(screen->
				swap_rg) |
		v_DSP_DELTA_SWAP(screen->
				swap_delta) | v_DSP_DUMMY_SWAP(screen->
					swap_dumy) |
				v_BLANK_EN(0) | v_BLACK_EN(0);
	lcdc_msk_reg(ctx, DSP_CTRL1, mask, val);
	val =
		v_HSYNC(screen->mode.hsync_len) | v_HORPRD(screen->mode.
				hsync_len +
				left_margin +
				x_res +
				right_margin);
	lcdc_writel(ctx, DSP_HTOTAL_HS_END, val);
	val = v_HAEP(screen->mode.hsync_len + left_margin + x_res) |
		v_HASP(screen->mode.hsync_len + left_margin);
	lcdc_writel(ctx, DSP_HACT_ST_END, val);

	val =
		v_VSYNC(screen->mode.vsync_len) | v_VERPRD(screen->mode.
				vsync_len +
				upper_margin +
				y_res +
				lower_margin);
	lcdc_writel(ctx, DSP_VTOTAL_VS_END, val);

	val = v_VAEP(screen->mode.vsync_len + upper_margin + y_res) |
		v_VASP(screen->mode.vsync_len + screen->mode.upper_margin);
	lcdc_writel(ctx, DSP_VACT_ST_END, val);

	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
	lcdc_msk_reg(ctx, SYS_CTRL, m_LCDC_STANDBY,
			v_LCDC_STANDBY(0));
	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
	lcdc_msk_reg(ctx, SYS_CTRL,
			m_WIN0_EN | m_WIN1_EN | m_WIN0_RB_SWAP |
			m_WIN1_RB_SWAP,
			v_WIN0_EN(1) | v_WIN1_EN(0) |
			v_WIN0_RB_SWAP(screen->swap_rb) |
			v_WIN1_RB_SWAP(screen->swap_rb));
	lcdc_cfg_done(ctx);
	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
#endif
}

static int fimd_enable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val,mask;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return -EPERM;

	if (!test_and_set_bit(0, &ctx->irq_flags)) {
#if 1
		mask = m_HS_INT_CLEAR | m_HS_INT_EN | m_FS_INT_CLEAR |
			m_FS_INT_EN | m_LF_INT_EN | m_LF_INT_CLEAR |
			m_LF_INT_NUM | m_BUS_ERR_INT_CLEAR | m_BUS_ERR_INT_EN;
		val = v_FS_INT_CLEAR(1) | v_FS_INT_EN(1) | v_HS_INT_CLEAR(0) |
			v_HS_INT_EN(0) | v_LF_INT_CLEAR(0) | v_LF_INT_EN(0);
		lcdc_msk_reg(ctx, INT_STATUS, mask, val);
		lcdc_cfg_done(ctx);
#endif
	}

	return 0;
}

static void fimd_disable_vblank(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	u32 val,mask;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (ctx->suspended)
		return;

	if (test_and_clear_bit(0, &ctx->irq_flags)) {
#if 1
		lcdc_msk_reg(ctx, INT_STATUS, m_FS_INT_CLEAR,
				v_FS_INT_CLEAR(1));
		mask = m_HS_INT_EN | m_FS_INT_EN | m_LF_INT_EN |
			m_BUS_ERR_INT_EN;
		val = v_HS_INT_EN(0) | v_FS_INT_EN(0) |
			v_LF_INT_EN(0) | v_BUS_ERR_INT_EN(0);
		lcdc_msk_reg(ctx, INT_STATUS, mask, val);
#endif

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

static struct rockchip_drm_manager_ops fimd_manager_ops = {
	.dpms = fimd_dpms,
	.apply = fimd_apply,
	.commit = fimd_commit,
	.enable_vblank = fimd_enable_vblank,
	.disable_vblank = fimd_disable_vblank,
	.wait_for_vblank = fimd_wait_for_vblank,
};

static void fimd_win_mode_set(struct device *dev,
			      struct rockchip_drm_overlay *overlay)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win;
	unsigned long offset;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (!overlay) {
		dev_err(dev, "overlay is NULL\n");
		return;
	}

	win = overlay->zpos;
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
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
	u8 fmt_cfg = 0;

	DRM_DEBUG_KMS("%s\n", __FILE__);
	switch(win_data->bpp){
		case 32:
			fmt_cfg = 0;
			break;
		case 24:
			fmt_cfg = 1;
			break;
		case 16:
			fmt_cfg = 2;
			break;
		default:
			printk("not support format %d\n",win_data->bpp);
			break;
	}


	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
	lcdc_msk_reg(ctx , SYS_CTRL, m_WIN0_FORMAT, v_WIN0_FORMAT(fmt_cfg));
	printk(KERN_ERR"------>yzq %d  SYS_CTRL=%x \n",__LINE__,lcdc_readl(ctx,SYS_CTRL));
}

static void fimd_win_set_colkey(struct device *dev, unsigned int win)
{
//	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

}

static void fimd_win_commit(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	struct rk_screen *screen = ctx->screen;
	int win = zpos;
	unsigned long val,  size;
	u32 xpos, ypos;

	printk(KERN_ERR"%s %d\n", __func__,__LINE__);

	if (ctx->suspended)
		return;

	if (!ctx->clkon)
		return;
	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];
#if 1
	/*
	 * SHADOWCON register is used for enabling timing.
	 *
	 * for example, once only width value of a register is set,
	 * if the dma is started then fimd hardware could malfunction so
	 * with protect window setting, the register fields with prefix '_F'
	 * wouldn't be updated at vsync also but updated once unprotect window
	 * is set.
	 */
	
	/* buffer end address */
	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	size = win_data->fb_width * win_data->ovl_height * (win_data->bpp >> 3);
	val = (unsigned long)(win_data->dma_addr + size);
	printk(KERN_ERR"%s %d\n", __func__,__LINE__);
	printk(KERN_ERR"ctx->regs=%x\n", __func__,__LINE__,ctx->regs);
	printk(KERN_ERR"-->yzq dma_addr=%x buf_offsize=%x win_data->fb_width=%d \nwin_data->fb_height=%d win_data->ovl_height=%d  win_data->ovl_width=%d \n win_data->offset_x=%d win_data->offset_y=%d win_data->line_size=%d\n win_data->bpp=%d ",win_data->dma_addr,win_data->buf_offsize,win_data->fb_width,win_data->fb_height,win_data->ovl_height, win_data->ovl_width,win_data->offset_x,win_data->offset_y,win_data->line_size,win_data->bpp);
	xpos = win_data->offset_x + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win_data->offset_y + screen->mode.upper_margin + screen->mode.vsync_len;

	lcdc_writel(ctx, WIN0_YRGB_MST0, win_data->dma_addr +win_data->buf_offsize );
	//lcdc_writel(ctx, WIN0_CBR_MST0, win0->area[0].uv_addr);

//	lcdc_writel(ctx, WIN0_SCL_FACTOR_YRGB,
//			v_X_SCL_FACTOR(win0->scale_yrgb_x) |
//			v_Y_SCL_FACTOR(win0->scale_yrgb_y));
//	lcdc_writel(ctx, WIN0_SCL_FACTOR_CBR,
//			v_X_SCL_FACTOR(win0->scale_cbcr_x) |
//			v_Y_SCL_FACTOR(win0->scale_cbcr_y));
	lcdc_writel(ctx, WIN0_ACT_INFO, v_ACT_WIDTH(win_data->ovl_width) |
			v_ACT_HEIGHT(win_data->ovl_height));
	lcdc_writel(ctx, WIN0_DSP_ST, v_DSP_STX(xpos) |
			v_DSP_STY(ypos));
	lcdc_writel(ctx, WIN0_DSP_INFO, v_DSP_WIDTH(win_data->fb_width) |
			v_DSP_HEIGHT(win_data->fb_height));
	lcdc_msk_reg(ctx, WIN_VIR, m_WIN0_VIR,
			v_WIN0_VIR_VAL(win_data->line_size/(win_data->bpp>>3)));
	lcdc_cfg_done(ctx);

#if 0
	/* protect windows */
	val = readl(ctx->regs + SHADOWCON);
	val |= SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);

	/* buffer start address */
	val = (unsigned long)win_data->dma_addr;
	writel(val, ctx->regs + VIDWx_BUF_START(win, 0));


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
	val = readl(ctx->regs + SHADOWCON);
	val |= SHADOWCON_CHx_ENABLE(win);
	val &= ~SHADOWCON_WINx_PROTECT(win);
	writel(val, ctx->regs + SHADOWCON);
#endif
#endif
	win_data->enabled = true;

}

static void fimd_win_disable(struct device *dev, int zpos)
{
	struct fimd_context *ctx = get_fimd_context(dev);
	struct fimd_win_data *win_data;
	int win = zpos;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (win == DEFAULT_ZPOS)
		win = ctx->default_win;

	if (win < 0 || win > WINDOWS_NR)
		return;

	win_data = &ctx->win_data[win];

	if (ctx->suspended) {
		/* do not resume this window*/
		win_data->resume = false;
		return;
	}

	win_data->enabled = false;
}

static struct rockchip_drm_overlay_ops fimd_overlay_ops = {
	.mode_set = fimd_win_mode_set,
	.commit = fimd_win_commit,
	.disable = fimd_win_disable,
};

static struct rockchip_drm_manager fimd_manager = {
	.pipe		= -1,
	.ops		= &fimd_manager_ops,
	.overlay_ops	= &fimd_overlay_ops,
	.display_ops	= &fimd_display_ops,
};

static irqreturn_t fimd_irq_handler(int irq, void *dev_id)
{
	struct fimd_context *ctx = (struct fimd_context *)dev_id;
	struct rockchip_drm_subdrv *subdrv = &ctx->subdrv;
	struct drm_device *drm_dev = subdrv->drm_dev;
	struct rockchip_drm_manager *manager = subdrv->manager;
	u32 int_reg = lcdc_readl(ctx, INT_STATUS);

	if (int_reg & m_FS_INT_STA) {
		lcdc_msk_reg(ctx, INT_STATUS, m_FS_INT_CLEAR,
			     v_FS_INT_CLEAR(1));
	}

	/* check the crtc is detached already from encoder */
	if (manager->pipe < 0)
		goto out;

	drm_handle_vblank(drm_dev, manager->pipe);
	rockchip_drm_crtc_finish_pageflip(drm_dev, manager->pipe);

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
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = 1, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	drm_dev->irq_enabled = 1;

	/*
	 * with vblank_disable_allowed = 1, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	drm_dev->vblank_disable_allowed = 1;

	/* attach this sub driver to iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_attach_device(drm_dev, dev);

	return 0;
}

static void fimd_subdrv_remove(struct drm_device *drm_dev, struct device *dev)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* detach this sub driver from iommu mapping if supported. */
	if (is_drm_iommu_supported(drm_dev))
		drm_iommu_detach_device(drm_dev, dev);
}


static void fimd_clear_win(struct fimd_context *ctx, int win)
{
	u32 val;

	DRM_DEBUG_KMS("%s\n", __FILE__);

}

static int fimd_clock(struct fimd_context *ctx, bool enable)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);
printk(KERN_ERR"---->yzq %s %d \n",__func__,__LINE__);
	if (enable) {
		if(ctx->clkon)
			return 0;
		int ret;

		ret = clk_prepare_enable(ctx->hclk);
		if (ret < 0)
			return ret;

		ret = clk_prepare_enable(ctx->dclk);
		if (ret < 0)
			return ret;
		
		ret = clk_prepare_enable(ctx->aclk);
		if (ret < 0)
			return ret;
		ctx->clkon=1;
	} else {
		if(!ctx->clkon)
			return 0;
		clk_disable_unprepare(ctx->aclk);
		clk_disable_unprepare(ctx->dclk);
		clk_disable_unprepare(ctx->hclk);
		ctx->clkon=0;
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

int rk_fb_video_mode_from_timing(const struct display_timing *dt, 
				struct rk_screen *screen)
{
	screen->mode.pixclock = dt->pixelclock.typ;
	screen->mode.left_margin = dt->hback_porch.typ;
	screen->mode.right_margin = dt->hfront_porch.typ;
	screen->mode.xres = dt->hactive.typ;
	screen->mode.hsync_len = dt->hsync_len.typ;
	screen->mode.upper_margin = dt->vback_porch.typ;
	screen->mode.lower_margin = dt->vfront_porch.typ;
	screen->mode.yres = dt->vactive.typ;
	screen->mode.vsync_len = dt->vsync_len.typ;
	screen->type = dt->screen_type;
	screen->lvds_format = dt->lvds_format;
	screen->face = dt->face;

	if (dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		screen->pin_dclk = 1;
	else
		screen->pin_dclk = 0;
	if(dt->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(dt->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
	if(dt->flags & DISPLAY_FLAGS_DE_HIGH)
		screen->pin_den = 1;
	else
		screen->pin_den = 0;
	
	return 0;
	
}

int rk_fb_prase_timing_dt(struct device_node *np, struct rk_screen *screen)
{
	struct display_timings *disp_timing;
	struct display_timing *dt;
	disp_timing = of_get_display_timings(np);
	if (!disp_timing) {
		pr_err("parse display timing err\n");
		return -EINVAL;
	}
	dt = display_timings_get(disp_timing, 0);
	rk_fb_video_mode_from_timing(dt, screen);
	printk(KERN_DEBUG "dclk:%d\n"
			 "hactive:%d\n"
			 "hback_porch:%d\n"
			 "hfront_porch:%d\n"
			 "hsync_len:%d\n"
			 "vactive:%d\n"
			 "vback_porch:%d\n"
			 "vfront_porch:%d\n"
			 "vsync_len:%d\n"
			 "screen_type:%d\n"
			 "lvds_format:%d\n"
			 "face:%d\n",
			dt->pixelclock.typ,
			dt->hactive.typ,
			dt->hback_porch.typ,
			dt->hfront_porch.typ,
			dt->hsync_len.typ,
			dt->vactive.typ,
			dt->vback_porch.typ,
			dt->vfront_porch.typ,
			dt->vsync_len.typ,
			dt->screen_type,
			dt->lvds_format,
			dt->face);
	return 0;

}

static int fimd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx;
	struct rockchip_drm_subdrv *subdrv;
	struct rockchip_drm_fimd_pdata *pdata;
	struct rockchip_drm_panel_info *panel;
	struct device_node *np = pdev->dev.of_node;
	struct rk_screen *screen;
	int prop;
	int reg_len;
	struct resource *res;
	int win;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("%s\n", __FILE__);
	of_property_read_u32(np, "rockchip,prop", &prop);
	if (prop == EXTEND) {
		printk("---->%s not support lcdc EXTEND\n");
			return 0;
	}
	printk("------>yzq dev=%x \n",dev);
	if (dev->of_node) {
		printk("------>yzq %s %d \n",__func__,__LINE__);
		panel = devm_kzalloc(dev, sizeof(struct rockchip_drm_panel_info), GFP_KERNEL);
		screen = devm_kzalloc(dev, sizeof(struct rk_screen), GFP_KERNEL);
		rk_fb_get_prmry_screen(screen);
		memcpy(&panel->timing,&screen->mode,sizeof(struct fb_videomode)); 
	} else {
		DRM_ERROR("no platform data specified\n");
		return -EINVAL;
	}

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	ctx->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->regs))
		return PTR_ERR(ctx->regs);
	reg_len = resource_size(res);
	ctx->regsbak = devm_kzalloc(dev,reg_len,GFP_KERNEL);
	ctx->lcdc_id = rk3188_lcdc_get_id(res->start);	
	ctx->screen = screen;
	if(ctx->lcdc_id == 0){
		ctx->hclk = clk_get(NULL, "g_h_lcdc0");
		ctx->aclk = clk_get(NULL, "aclk_lcdc0");
		ctx->dclk = clk_get(NULL, "dclk_lcdc0");
	}else{
		ctx->hclk = clk_get(NULL, "g_h_lcdc1");
		ctx->aclk = clk_get(NULL, "aclk_lcdc1");
		ctx->dclk = clk_get(NULL, "dclk_lcdc1");
	}

	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		dev_err(dev, "cannot find IRQ for lcdc%d\n",
			ctx->lcdc_id);
		return -ENXIO;
	}
	ret = devm_request_irq(dev, ctx->irq, fimd_irq_handler,
							0, "drm_fimd", ctx);
	if (ret) {
		dev_err(dev, "irq request failed.\n");
		return ret;
	}

	ctx->default_win = 0;// pdata->default_win;
	ctx->panel = panel;
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
	
	ret = clk_set_rate(ctx->dclk, ctx->screen->mode.pixclock);
	if (ret)
		printk( "set lcdc%d dclk failed\n", ctx->lcdc_id);
	
	fimd_activate(ctx, true);

	memcpy(ctx->regsbak,ctx->regs,reg_len);
	rockchip_drm_subdrv_register(subdrv);

	return 0;
}

static int fimd_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fimd_context *ctx = platform_get_drvdata(pdev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	rockchip_drm_subdrv_unregister(&ctx->subdrv);

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

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return fimd_activate(ctx, false);
}

static int fimd_runtime_resume(struct device *dev)
{
	struct fimd_context *ctx = get_fimd_context(dev);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	return fimd_activate(ctx, true);
}
#endif
#if defined(CONFIG_OF)
static const struct of_device_id rk3188_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3188-lcdc",},
	{}
};
#endif

static const struct dev_pm_ops fimd_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimd_suspend, fimd_resume)
	SET_RUNTIME_PM_OPS(fimd_runtime_suspend, fimd_runtime_resume, NULL)
};

struct platform_driver fimd_driver = {
	.probe		= fimd_probe,
	.remove		= fimd_remove,
	.id_table       = rk3188_lcdc_dt_ids,
	.driver		= {
		.name	= "rk3188-lcdc",
		.owner	= THIS_MODULE,
		.pm	= &fimd_pm_ops,
		.of_match_table = of_match_ptr(rk3188_lcdc_dt_ids),
	},
};
