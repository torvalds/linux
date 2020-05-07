// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#include <linux/delay.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "hibmc_drm_drv.h"
#include "hibmc_drm_regs.h"

struct hibmc_display_panel_pll {
	unsigned long M;
	unsigned long N;
	unsigned long OD;
	unsigned long POD;
};

struct hibmc_dislay_pll_config {
	unsigned long hdisplay;
	unsigned long vdisplay;
	u32 pll1_config_value;
	u32 pll2_config_value;
};

static const struct hibmc_dislay_pll_config hibmc_pll_table[] = {
	{640, 480, CRT_PLL1_HS_25MHZ, CRT_PLL2_HS_25MHZ},
	{800, 600, CRT_PLL1_HS_40MHZ, CRT_PLL2_HS_40MHZ},
	{1024, 768, CRT_PLL1_HS_65MHZ, CRT_PLL2_HS_65MHZ},
	{1152, 864, CRT_PLL1_HS_80MHZ_1152, CRT_PLL2_HS_80MHZ},
	{1280, 768, CRT_PLL1_HS_80MHZ, CRT_PLL2_HS_80MHZ},
	{1280, 720, CRT_PLL1_HS_74MHZ, CRT_PLL2_HS_74MHZ},
	{1280, 960, CRT_PLL1_HS_108MHZ, CRT_PLL2_HS_108MHZ},
	{1280, 1024, CRT_PLL1_HS_108MHZ, CRT_PLL2_HS_108MHZ},
	{1440, 900, CRT_PLL1_HS_106MHZ, CRT_PLL2_HS_106MHZ},
	{1600, 900, CRT_PLL1_HS_108MHZ, CRT_PLL2_HS_108MHZ},
	{1600, 1200, CRT_PLL1_HS_162MHZ, CRT_PLL2_HS_162MHZ},
	{1920, 1080, CRT_PLL1_HS_148MHZ, CRT_PLL2_HS_148MHZ},
	{1920, 1200, CRT_PLL1_HS_193MHZ, CRT_PLL2_HS_193MHZ},
};

#define PADDING(align, data) (((data) + (align) - 1) & (~((align) - 1)))

static int hibmc_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	u32 src_w = state->src_w >> 16;
	u32 src_h = state->src_h >> 16;

	if (!crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state->state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (src_w != state->crtc_w || src_h != state->crtc_h) {
		DRM_DEBUG_ATOMIC("scale not support\n");
		return -EINVAL;
	}

	if (state->crtc_x < 0 || state->crtc_y < 0) {
		DRM_DEBUG_ATOMIC("crtc_x/y of drm_plane state is invalid\n");
		return -EINVAL;
	}

	if (!crtc_state->enable)
		return 0;

	if (state->crtc_x + state->crtc_w >
	    crtc_state->adjusted_mode.hdisplay ||
	    state->crtc_y + state->crtc_h >
	    crtc_state->adjusted_mode.vdisplay) {
		DRM_DEBUG_ATOMIC("visible portion of plane is invalid\n");
		return -EINVAL;
	}

	return 0;
}

static void hibmc_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct drm_plane_state	*state	= plane->state;
	u32 reg;
	s64 gpu_addr = 0;
	unsigned int line_l;
	struct hibmc_drm_private *priv = plane->dev->dev_private;
	struct drm_gem_vram_object *gbo;

	if (!state->fb)
		return;

	gbo = drm_gem_vram_of_gem(state->fb->obj[0]);

	gpu_addr = drm_gem_vram_offset(gbo);
	if (WARN_ON_ONCE(gpu_addr < 0))
		return; /* Bug: we didn't pin the BO to VRAM in prepare_fb. */

	writel(gpu_addr, priv->mmio + HIBMC_CRT_FB_ADDRESS);

	reg = state->fb->width * (state->fb->format->cpp[0]);
	/* now line_pad is 16 */
	reg = PADDING(16, reg);

	line_l = state->fb->width * state->fb->format->cpp[0];
	line_l = PADDING(16, line_l);
	writel(HIBMC_FIELD(HIBMC_CRT_FB_WIDTH_WIDTH, reg) |
	       HIBMC_FIELD(HIBMC_CRT_FB_WIDTH_OFFS, line_l),
	       priv->mmio + HIBMC_CRT_FB_WIDTH);

	/* SET PIXEL FORMAT */
	reg = readl(priv->mmio + HIBMC_CRT_DISP_CTL);
	reg &= ~HIBMC_CRT_DISP_CTL_FORMAT_MASK;
	reg |= HIBMC_FIELD(HIBMC_CRT_DISP_CTL_FORMAT,
			   state->fb->format->cpp[0] * 8 / 16);
	writel(reg, priv->mmio + HIBMC_CRT_DISP_CTL);
}

static const u32 channel_formats1[] = {
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565, DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888, DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888, DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888
};

static struct drm_plane_funcs hibmc_plane_funcs = {
	.update_plane	= drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const struct drm_plane_helper_funcs hibmc_plane_helper_funcs = {
	.prepare_fb	= drm_gem_vram_plane_helper_prepare_fb,
	.cleanup_fb	= drm_gem_vram_plane_helper_cleanup_fb,
	.atomic_check = hibmc_plane_atomic_check,
	.atomic_update = hibmc_plane_atomic_update,
};

static struct drm_plane *hibmc_plane_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct drm_plane *plane;
	int ret = 0;

	plane = devm_kzalloc(dev->dev, sizeof(*plane), GFP_KERNEL);
	if (!plane) {
		DRM_ERROR("failed to alloc memory when init plane\n");
		return ERR_PTR(-ENOMEM);
	}
	/*
	 * plane init
	 * TODO: Now only support primary plane, overlay planes
	 * need to do.
	 */
	ret = drm_universal_plane_init(dev, plane, 1, &hibmc_plane_funcs,
				       channel_formats1,
				       ARRAY_SIZE(channel_formats1),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY,
				       NULL);
	if (ret) {
		DRM_ERROR("failed to init plane: %d\n", ret);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, &hibmc_plane_helper_funcs);
	return plane;
}

static void hibmc_crtc_dpms(struct drm_crtc *crtc, int dpms)
{
	struct hibmc_drm_private *priv = crtc->dev->dev_private;
	unsigned int reg;

	reg = readl(priv->mmio + HIBMC_CRT_DISP_CTL);
	reg &= ~HIBMC_CRT_DISP_CTL_DPMS_MASK;
	reg |= HIBMC_FIELD(HIBMC_CRT_DISP_CTL_DPMS, dpms);
	reg &= ~HIBMC_CRT_DISP_CTL_TIMING_MASK;
	if (dpms == HIBMC_CRT_DPMS_ON)
		reg |= HIBMC_CRT_DISP_CTL_TIMING(1);
	writel(reg, priv->mmio + HIBMC_CRT_DISP_CTL);
}

static void hibmc_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	unsigned int reg;
	struct hibmc_drm_private *priv = crtc->dev->dev_private;

	hibmc_set_power_mode(priv, HIBMC_PW_MODE_CTL_MODE_MODE0);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = readl(priv->mmio + HIBMC_CURRENT_GATE);
	reg &= ~HIBMC_CURR_GATE_LOCALMEM_MASK;
	reg &= ~HIBMC_CURR_GATE_DISPLAY_MASK;
	reg |= HIBMC_CURR_GATE_LOCALMEM(1);
	reg |= HIBMC_CURR_GATE_DISPLAY(1);
	hibmc_set_current_gate(priv, reg);
	drm_crtc_vblank_on(crtc);
	hibmc_crtc_dpms(crtc, HIBMC_CRT_DPMS_ON);
}

static void hibmc_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	unsigned int reg;
	struct hibmc_drm_private *priv = crtc->dev->dev_private;

	hibmc_crtc_dpms(crtc, HIBMC_CRT_DPMS_OFF);
	drm_crtc_vblank_off(crtc);

	hibmc_set_power_mode(priv, HIBMC_PW_MODE_CTL_MODE_SLEEP);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = readl(priv->mmio + HIBMC_CURRENT_GATE);
	reg &= ~HIBMC_CURR_GATE_LOCALMEM_MASK;
	reg &= ~HIBMC_CURR_GATE_DISPLAY_MASK;
	reg |= HIBMC_CURR_GATE_LOCALMEM(0);
	reg |= HIBMC_CURR_GATE_DISPLAY(0);
	hibmc_set_current_gate(priv, reg);
}

static enum drm_mode_status
hibmc_crtc_mode_valid(struct drm_crtc *crtc,
		      const struct drm_display_mode *mode)
{
	int i = 0;
	int vrefresh = drm_mode_vrefresh(mode);

	if (vrefresh < 59 || vrefresh > 61)
		return MODE_NOCLOCK;

	for (i = 0; i < ARRAY_SIZE(hibmc_pll_table); i++) {
		if (hibmc_pll_table[i].hdisplay == mode->hdisplay &&
		    hibmc_pll_table[i].vdisplay == mode->vdisplay)
			return MODE_OK;
	}

	return MODE_BAD;
}

static unsigned int format_pll_reg(void)
{
	unsigned int pllreg = 0;
	struct hibmc_display_panel_pll pll = {0};

	/*
	 * Note that all PLL's have the same format. Here,
	 * we just use Panel PLL parameter to work out the bit
	 * fields in the register.On returning a 32 bit number, the value can
	 * be applied to any PLL in the calling function.
	 */
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_BYPASS, 0);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_POWER, 1);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_INPUT, 0);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_POD, pll.POD);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_OD, pll.OD);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_N, pll.N);
	pllreg |= HIBMC_FIELD(HIBMC_PLL_CTRL_M, pll.M);

	return pllreg;
}

static void set_vclock_hisilicon(struct drm_device *dev, unsigned long pll)
{
	u32 val;
	struct hibmc_drm_private *priv = dev->dev_private;

	val = readl(priv->mmio + CRT_PLL1_HS);
	val &= ~(CRT_PLL1_HS_OUTER_BYPASS(1));
	writel(val, priv->mmio + CRT_PLL1_HS);

	val = CRT_PLL1_HS_INTER_BYPASS(1) | CRT_PLL1_HS_POWERON(1);
	writel(val, priv->mmio + CRT_PLL1_HS);

	writel(pll, priv->mmio + CRT_PLL1_HS);

	usleep_range(1000, 2000);

	val = pll & ~(CRT_PLL1_HS_POWERON(1));
	writel(val, priv->mmio + CRT_PLL1_HS);

	usleep_range(1000, 2000);

	val &= ~(CRT_PLL1_HS_INTER_BYPASS(1));
	writel(val, priv->mmio + CRT_PLL1_HS);

	usleep_range(1000, 2000);

	val |= CRT_PLL1_HS_OUTER_BYPASS(1);
	writel(val, priv->mmio + CRT_PLL1_HS);
}

static void get_pll_config(unsigned long x, unsigned long y,
			   u32 *pll1, u32 *pll2)
{
	int i;
	int count = ARRAY_SIZE(hibmc_pll_table);

	for (i = 0; i < count; i++) {
		if (hibmc_pll_table[i].hdisplay == x &&
		    hibmc_pll_table[i].vdisplay == y) {
			*pll1 = hibmc_pll_table[i].pll1_config_value;
			*pll2 = hibmc_pll_table[i].pll2_config_value;
			return;
		}
	}

	/* if found none, we use default value */
	*pll1 = CRT_PLL1_HS_25MHZ;
	*pll2 = CRT_PLL2_HS_25MHZ;
}

/*
 * This function takes care the extra registers and bit fields required to
 * setup a mode in board.
 * Explanation about Display Control register:
 * FPGA only supports 7 predefined pixel clocks, and clock select is
 * in bit 4:0 of new register 0x802a8.
 */
static unsigned int display_ctrl_adjust(struct drm_device *dev,
					struct drm_display_mode *mode,
					unsigned int ctrl)
{
	unsigned long x, y;
	u32 pll1; /* bit[31:0] of PLL */
	u32 pll2; /* bit[63:32] of PLL */
	struct hibmc_drm_private *priv = dev->dev_private;

	x = mode->hdisplay;
	y = mode->vdisplay;

	get_pll_config(x, y, &pll1, &pll2);
	writel(pll2, priv->mmio + CRT_PLL2_HS);
	set_vclock_hisilicon(dev, pll1);

	/*
	 * Hisilicon has to set up the top-left and bottom-right
	 * registers as well.
	 * Note that normal chip only use those two register for
	 * auto-centering mode.
	 */
	writel(HIBMC_FIELD(HIBMC_CRT_AUTO_CENTERING_TL_TOP, 0) |
	       HIBMC_FIELD(HIBMC_CRT_AUTO_CENTERING_TL_LEFT, 0),
	       priv->mmio + HIBMC_CRT_AUTO_CENTERING_TL);

	writel(HIBMC_FIELD(HIBMC_CRT_AUTO_CENTERING_BR_BOTTOM, y - 1) |
	       HIBMC_FIELD(HIBMC_CRT_AUTO_CENTERING_BR_RIGHT, x - 1),
	       priv->mmio + HIBMC_CRT_AUTO_CENTERING_BR);

	/*
	 * Assume common fields in ctrl have been properly set before
	 * calling this function.
	 * This function only sets the extra fields in ctrl.
	 */

	/* Set bit 25 of display controller: Select CRT or VGA clock */
	ctrl &= ~HIBMC_CRT_DISP_CTL_CRTSELECT_MASK;
	ctrl &= ~HIBMC_CRT_DISP_CTL_CLOCK_PHASE_MASK;

	ctrl |= HIBMC_CRT_DISP_CTL_CRTSELECT(HIBMC_CRTSELECT_CRT);

	/* clock_phase_polarity is 0 */
	ctrl |= HIBMC_CRT_DISP_CTL_CLOCK_PHASE(0);

	writel(ctrl, priv->mmio + HIBMC_CRT_DISP_CTL);

	return ctrl;
}

static void hibmc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	unsigned int val;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_device *dev = crtc->dev;
	struct hibmc_drm_private *priv = dev->dev_private;
	int width = mode->hsync_end - mode->hsync_start;
	int height = mode->vsync_end - mode->vsync_start;

	writel(format_pll_reg(), priv->mmio + HIBMC_CRT_PLL_CTRL);
	writel(HIBMC_FIELD(HIBMC_CRT_HORZ_TOTAL_TOTAL, mode->htotal - 1) |
	       HIBMC_FIELD(HIBMC_CRT_HORZ_TOTAL_DISP_END, mode->hdisplay - 1),
	       priv->mmio + HIBMC_CRT_HORZ_TOTAL);

	writel(HIBMC_FIELD(HIBMC_CRT_HORZ_SYNC_WIDTH, width) |
	       HIBMC_FIELD(HIBMC_CRT_HORZ_SYNC_START, mode->hsync_start - 1),
	       priv->mmio + HIBMC_CRT_HORZ_SYNC);

	writel(HIBMC_FIELD(HIBMC_CRT_VERT_TOTAL_TOTAL, mode->vtotal - 1) |
	       HIBMC_FIELD(HIBMC_CRT_VERT_TOTAL_DISP_END, mode->vdisplay - 1),
	       priv->mmio + HIBMC_CRT_VERT_TOTAL);

	writel(HIBMC_FIELD(HIBMC_CRT_VERT_SYNC_HEIGHT, height) |
	       HIBMC_FIELD(HIBMC_CRT_VERT_SYNC_START, mode->vsync_start - 1),
	       priv->mmio + HIBMC_CRT_VERT_SYNC);

	val = HIBMC_FIELD(HIBMC_CRT_DISP_CTL_VSYNC_PHASE, 0);
	val |= HIBMC_FIELD(HIBMC_CRT_DISP_CTL_HSYNC_PHASE, 0);
	val |= HIBMC_CRT_DISP_CTL_TIMING(1);
	val |= HIBMC_CRT_DISP_CTL_PLANE(1);

	display_ctrl_adjust(dev, mode, val);
}

static void hibmc_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	unsigned int reg;
	struct drm_device *dev = crtc->dev;
	struct hibmc_drm_private *priv = dev->dev_private;

	hibmc_set_power_mode(priv, HIBMC_PW_MODE_CTL_MODE_MODE0);

	/* Enable display power gate & LOCALMEM power gate*/
	reg = readl(priv->mmio + HIBMC_CURRENT_GATE);
	reg &= ~HIBMC_CURR_GATE_DISPLAY_MASK;
	reg &= ~HIBMC_CURR_GATE_LOCALMEM_MASK;
	reg |= HIBMC_CURR_GATE_DISPLAY(1);
	reg |= HIBMC_CURR_GATE_LOCALMEM(1);
	hibmc_set_current_gate(priv, reg);

	/* We can add more initialization as needed. */
}

static void hibmc_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)

{
	unsigned long flags;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (crtc->state->event)
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

static int hibmc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct hibmc_drm_private *priv = crtc->dev->dev_private;

	writel(HIBMC_RAW_INTERRUPT_EN_VBLANK(1),
	       priv->mmio + HIBMC_RAW_INTERRUPT_EN);

	return 0;
}

static void hibmc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct hibmc_drm_private *priv = crtc->dev->dev_private;

	writel(HIBMC_RAW_INTERRUPT_EN_VBLANK(0),
	       priv->mmio + HIBMC_RAW_INTERRUPT_EN);
}

static void hibmc_crtc_load_lut(struct drm_crtc *crtc)
{
	struct hibmc_drm_private *priv = crtc->dev->dev_private;
	void __iomem   *mmio = priv->mmio;
	u16 *r, *g, *b;
	unsigned int reg;
	int i;

	r = crtc->gamma_store;
	g = r + crtc->gamma_size;
	b = g + crtc->gamma_size;

	for (i = 0; i < crtc->gamma_size; i++) {
		unsigned int offset = i << 2;
		u8 red = *r++ >> 8;
		u8 green = *g++ >> 8;
		u8 blue = *b++ >> 8;
		u32 rgb = (red << 16) | (green << 8) | blue;

		writel(rgb, mmio + HIBMC_CRT_PALETTE + offset);
	}

	reg = readl(priv->mmio + HIBMC_CRT_DISP_CTL);
	reg |= HIBMC_FIELD(HIBMC_CTL_DISP_CTL_GAMMA, 1);
	writel(reg, priv->mmio + HIBMC_CRT_DISP_CTL);
}

static int hibmc_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				u16 *blue, uint32_t size,
				struct drm_modeset_acquire_ctx *ctx)
{
	hibmc_crtc_load_lut(crtc);

	return 0;
}

static const struct drm_crtc_funcs hibmc_crtc_funcs = {
	.page_flip = drm_atomic_helper_page_flip,
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state =  drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = hibmc_crtc_enable_vblank,
	.disable_vblank = hibmc_crtc_disable_vblank,
	.gamma_set = hibmc_crtc_gamma_set,
};

static const struct drm_crtc_helper_funcs hibmc_crtc_helper_funcs = {
	.mode_set_nofb	= hibmc_crtc_mode_set_nofb,
	.atomic_begin	= hibmc_crtc_atomic_begin,
	.atomic_flush	= hibmc_crtc_atomic_flush,
	.atomic_enable	= hibmc_crtc_atomic_enable,
	.atomic_disable	= hibmc_crtc_atomic_disable,
	.mode_valid = hibmc_crtc_mode_valid,
};

int hibmc_de_init(struct hibmc_drm_private *priv)
{
	struct drm_device *dev = priv->dev;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret;

	plane = hibmc_plane_init(priv);
	if (IS_ERR(plane)) {
		DRM_ERROR("failed to create plane: %ld\n", PTR_ERR(plane));
		return PTR_ERR(plane);
	}

	crtc = devm_kzalloc(dev->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc) {
		DRM_ERROR("failed to alloc memory when init crtc\n");
		return -ENOMEM;
	}

	ret = drm_crtc_init_with_planes(dev, crtc, plane,
					NULL, &hibmc_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to init crtc: %d\n", ret);
		return ret;
	}

	ret = drm_mode_crtc_set_gamma_size(crtc, 256);
	if (ret) {
		DRM_ERROR("failed to set gamma size: %d\n", ret);
		return ret;
	}
	drm_crtc_helper_add(crtc, &hibmc_crtc_helper_funcs);

	return 0;
}
