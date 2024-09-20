// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/debugfs.h>
#include <linux/delay.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_vblank.h>

#include "lsdc_drv.h"

/*
 * After the CRTC soft reset, the vblank counter would be reset to zero.
 * But the address and other settings in the CRTC register remain the same
 * as before.
 */

static void lsdc_crtc0_soft_reset(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_CRTC0_CFG_REG);

	val &= CFG_VALID_BITS_MASK;

	/* Soft reset bit, active low */
	val &= ~CFG_RESET_N;

	val &= ~CFG_PIX_FMT_MASK;

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, val);

	udelay(1);

	val |= CFG_RESET_N | LSDC_PF_XRGB8888 | CFG_OUTPUT_ENABLE;

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, val);

	/* Wait about a vblank time */
	mdelay(20);
}

static void lsdc_crtc1_soft_reset(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_CRTC1_CFG_REG);

	val &= CFG_VALID_BITS_MASK;

	/* Soft reset bit, active low */
	val &= ~CFG_RESET_N;

	val &= ~CFG_PIX_FMT_MASK;

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, val);

	udelay(1);

	val |= CFG_RESET_N | LSDC_PF_XRGB8888 | CFG_OUTPUT_ENABLE;

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, val);

	/* Wait about a vblank time */
	msleep(20);
}

static void lsdc_crtc0_enable(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_CRTC0_CFG_REG);

	/*
	 * This may happen in extremely rare cases, but a soft reset can
	 * bring it back to normal. We add a warning here, hoping to catch
	 * something if it happens.
	 */
	if (val & CRTC_ANCHORED) {
		drm_warn(&ldev->base, "%s stall\n", lcrtc->base.name);
		return lsdc_crtc0_soft_reset(lcrtc);
	}

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, val | CFG_OUTPUT_ENABLE);
}

static void lsdc_crtc0_disable(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_clr(ldev, LSDC_CRTC0_CFG_REG, CFG_OUTPUT_ENABLE);

	udelay(9);
}

static void lsdc_crtc1_enable(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	/*
	 * This may happen in extremely rare cases, but a soft reset can
	 * bring it back to normal. We add a warning here, hoping to catch
	 * something if it happens.
	 */
	val = lsdc_rreg32(ldev, LSDC_CRTC1_CFG_REG);
	if (val & CRTC_ANCHORED) {
		drm_warn(&ldev->base, "%s stall\n", lcrtc->base.name);
		return lsdc_crtc1_soft_reset(lcrtc);
	}

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, val | CFG_OUTPUT_ENABLE);
}

static void lsdc_crtc1_disable(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_clr(ldev, LSDC_CRTC1_CFG_REG, CFG_OUTPUT_ENABLE);

	udelay(9);
}

/* All Loongson display controllers have hardware scanout position recoders */

static void lsdc_crtc0_scan_pos(struct lsdc_crtc *lcrtc, int *hpos, int *vpos)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_CRTC0_SCAN_POS_REG);

	*hpos = val >> 16;
	*vpos = val & 0xffff;
}

static void lsdc_crtc1_scan_pos(struct lsdc_crtc *lcrtc, int *hpos, int *vpos)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val;

	val = lsdc_rreg32(ldev, LSDC_CRTC1_SCAN_POS_REG);

	*hpos = val >> 16;
	*vpos = val & 0xffff;
}

static void lsdc_crtc0_enable_vblank(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_INT_REG, INT_CRTC0_VSYNC_EN);
}

static void lsdc_crtc0_disable_vblank(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_clr(ldev, LSDC_INT_REG, INT_CRTC0_VSYNC_EN);
}

static void lsdc_crtc1_enable_vblank(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_INT_REG, INT_CRTC1_VSYNC_EN);
}

static void lsdc_crtc1_disable_vblank(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_clr(ldev, LSDC_INT_REG, INT_CRTC1_VSYNC_EN);
}

static void lsdc_crtc0_flip(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_CRTC0_CFG_REG, CFG_PAGE_FLIP);
}

static void lsdc_crtc1_flip(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_CRTC1_CFG_REG, CFG_PAGE_FLIP);
}

/*
 * CRTC0 clone from CRTC1 or CRTC1 clone from CRTC0 using hardware logic
 * This may be useful for custom cloning (TWIN) applications. Saving the
 * bandwidth compared with the clone (mirroring) display mode provided by
 * drm core.
 */

static void lsdc_crtc0_clone(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_CRTC0_CFG_REG, CFG_HW_CLONE);
}

static void lsdc_crtc1_clone(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_ureg32_set(ldev, LSDC_CRTC1_CFG_REG, CFG_HW_CLONE);
}

static void lsdc_crtc0_set_mode(struct lsdc_crtc *lcrtc,
				const struct drm_display_mode *mode)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC0_HDISPLAY_REG,
		    (mode->crtc_htotal << 16) | mode->crtc_hdisplay);

	lsdc_wreg32(ldev, LSDC_CRTC0_VDISPLAY_REG,
		    (mode->crtc_vtotal << 16) | mode->crtc_vdisplay);

	lsdc_wreg32(ldev, LSDC_CRTC0_HSYNC_REG,
		    (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start | HSYNC_EN);

	lsdc_wreg32(ldev, LSDC_CRTC0_VSYNC_REG,
		    (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start | VSYNC_EN);
}

static void lsdc_crtc1_set_mode(struct lsdc_crtc *lcrtc,
				const struct drm_display_mode *mode)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC1_HDISPLAY_REG,
		    (mode->crtc_htotal << 16) | mode->crtc_hdisplay);

	lsdc_wreg32(ldev, LSDC_CRTC1_VDISPLAY_REG,
		    (mode->crtc_vtotal << 16) | mode->crtc_vdisplay);

	lsdc_wreg32(ldev, LSDC_CRTC1_HSYNC_REG,
		    (mode->crtc_hsync_end << 16) | mode->crtc_hsync_start | HSYNC_EN);

	lsdc_wreg32(ldev, LSDC_CRTC1_VSYNC_REG,
		    (mode->crtc_vsync_end << 16) | mode->crtc_vsync_start | VSYNC_EN);
}

/*
 * This is required for S3 support.
 * After resuming from suspend, LSDC_CRTCx_CFG_REG (x = 0 or 1) is filled
 * with garbage value, which causes the CRTC hang there.
 *
 * This function provides minimal settings for the affected registers.
 * This overrides the firmware's settings on startup, making the CRTC work
 * on our own, similar to the functional of GPU POST (Power On Self Test).
 * Only touch CRTC hardware-related parts.
 */

static void lsdc_crtc0_reset(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, CFG_RESET_N | LSDC_PF_XRGB8888);
}

static void lsdc_crtc1_reset(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, CFG_RESET_N | LSDC_PF_XRGB8888);
}

static const struct lsdc_crtc_hw_ops ls7a1000_crtc_hw_ops[2] = {
	{
		.enable = lsdc_crtc0_enable,
		.disable = lsdc_crtc0_disable,
		.enable_vblank = lsdc_crtc0_enable_vblank,
		.disable_vblank = lsdc_crtc0_disable_vblank,
		.flip = lsdc_crtc0_flip,
		.clone = lsdc_crtc0_clone,
		.set_mode = lsdc_crtc0_set_mode,
		.get_scan_pos = lsdc_crtc0_scan_pos,
		.soft_reset = lsdc_crtc0_soft_reset,
		.reset = lsdc_crtc0_reset,
	},
	{
		.enable = lsdc_crtc1_enable,
		.disable = lsdc_crtc1_disable,
		.enable_vblank = lsdc_crtc1_enable_vblank,
		.disable_vblank = lsdc_crtc1_disable_vblank,
		.flip = lsdc_crtc1_flip,
		.clone = lsdc_crtc1_clone,
		.set_mode = lsdc_crtc1_set_mode,
		.get_scan_pos = lsdc_crtc1_scan_pos,
		.soft_reset = lsdc_crtc1_soft_reset,
		.reset = lsdc_crtc1_reset,
	},
};

/*
 * The 32-bit hardware vblank counter has been available since LS7A2000
 * and LS2K2000. The counter increases even though the CRTC is disabled,
 * it will be reset only if the CRTC is being soft reset.
 * Those registers are also readable for ls7a1000, but its value does not
 * change.
 */

static u32 lsdc_crtc0_get_vblank_count(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	return lsdc_rreg32(ldev, LSDC_CRTC0_VSYNC_COUNTER_REG);
}

static u32 lsdc_crtc1_get_vblank_count(struct lsdc_crtc *lcrtc)
{
	struct lsdc_device *ldev = lcrtc->ldev;

	return lsdc_rreg32(ldev, LSDC_CRTC1_VSYNC_COUNTER_REG);
}

/*
 * The DMA step bit fields are available since LS7A2000/LS2K2000, for
 * supporting odd resolutions. But a large DMA step save the bandwidth.
 * The larger, the better. Behavior of writing those bits on LS7A1000
 * or LS2K1000 is underfined.
 */

static void lsdc_crtc0_set_dma_step(struct lsdc_crtc *lcrtc,
				    enum lsdc_dma_steps dma_step)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val = lsdc_rreg32(ldev, LSDC_CRTC0_CFG_REG);

	val &= ~CFG_DMA_STEP_MASK;
	val |= dma_step << CFG_DMA_STEP_SHIFT;

	lsdc_wreg32(ldev, LSDC_CRTC0_CFG_REG, val);
}

static void lsdc_crtc1_set_dma_step(struct lsdc_crtc *lcrtc,
				    enum lsdc_dma_steps dma_step)
{
	struct lsdc_device *ldev = lcrtc->ldev;
	u32 val = lsdc_rreg32(ldev, LSDC_CRTC1_CFG_REG);

	val &= ~CFG_DMA_STEP_MASK;
	val |= dma_step << CFG_DMA_STEP_SHIFT;

	lsdc_wreg32(ldev, LSDC_CRTC1_CFG_REG, val);
}

static const struct lsdc_crtc_hw_ops ls7a2000_crtc_hw_ops[2] = {
	{
		.enable = lsdc_crtc0_enable,
		.disable = lsdc_crtc0_disable,
		.enable_vblank = lsdc_crtc0_enable_vblank,
		.disable_vblank = lsdc_crtc0_disable_vblank,
		.flip = lsdc_crtc0_flip,
		.clone = lsdc_crtc0_clone,
		.set_mode = lsdc_crtc0_set_mode,
		.soft_reset = lsdc_crtc0_soft_reset,
		.get_scan_pos = lsdc_crtc0_scan_pos,
		.set_dma_step = lsdc_crtc0_set_dma_step,
		.get_vblank_counter = lsdc_crtc0_get_vblank_count,
		.reset = lsdc_crtc0_reset,
	},
	{
		.enable = lsdc_crtc1_enable,
		.disable = lsdc_crtc1_disable,
		.enable_vblank = lsdc_crtc1_enable_vblank,
		.disable_vblank = lsdc_crtc1_disable_vblank,
		.flip = lsdc_crtc1_flip,
		.clone = lsdc_crtc1_clone,
		.set_mode = lsdc_crtc1_set_mode,
		.get_scan_pos = lsdc_crtc1_scan_pos,
		.soft_reset = lsdc_crtc1_soft_reset,
		.set_dma_step = lsdc_crtc1_set_dma_step,
		.get_vblank_counter = lsdc_crtc1_get_vblank_count,
		.reset = lsdc_crtc1_reset,
	},
};

static void lsdc_crtc_reset(struct drm_crtc *crtc)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	const struct lsdc_crtc_hw_ops *ops = lcrtc->hw_ops;
	struct lsdc_crtc_state *priv_crtc_state;

	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	priv_crtc_state = kzalloc(sizeof(*priv_crtc_state), GFP_KERNEL);

	if (!priv_crtc_state)
		__drm_atomic_helper_crtc_reset(crtc, NULL);
	else
		__drm_atomic_helper_crtc_reset(crtc, &priv_crtc_state->base);

	/* Reset the CRTC hardware, this is required for S3 support */
	ops->reset(lcrtc);
}

static void lsdc_crtc_atomic_destroy_state(struct drm_crtc *crtc,
					   struct drm_crtc_state *state)
{
	struct lsdc_crtc_state *priv_state = to_lsdc_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(&priv_state->base);

	kfree(priv_state);
}

static struct drm_crtc_state *
lsdc_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct lsdc_crtc_state *new_priv_state;
	struct lsdc_crtc_state *old_priv_state;

	new_priv_state = kzalloc(sizeof(*new_priv_state), GFP_KERNEL);
	if (!new_priv_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_priv_state->base);

	old_priv_state = to_lsdc_crtc_state(crtc->state);

	memcpy(&new_priv_state->pparms, &old_priv_state->pparms,
	       sizeof(new_priv_state->pparms));

	return &new_priv_state->base;
}

static u32 lsdc_crtc_get_vblank_counter(struct drm_crtc *crtc)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);

	/* 32-bit hardware vblank counter */
	return lcrtc->hw_ops->get_vblank_counter(lcrtc);
}

static int lsdc_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);

	if (!lcrtc->has_vblank)
		return -EINVAL;

	lcrtc->hw_ops->enable_vblank(lcrtc);

	return 0;
}

static void lsdc_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);

	if (!lcrtc->has_vblank)
		return;

	lcrtc->hw_ops->disable_vblank(lcrtc);
}

/*
 * CRTC related debugfs
 * Primary planes and cursor planes belong to the CRTC as well.
 * For the sake of convenience, plane-related registers are also add here.
 */

#define REG_DEF(reg) { \
	.name = __stringify_1(LSDC_##reg##_REG), \
	.offset = LSDC_##reg##_REG, \
}

static const struct lsdc_reg32 lsdc_crtc_regs_array[2][21] = {
	[0] = {
		REG_DEF(CRTC0_CFG),
		REG_DEF(CRTC0_FB_ORIGIN),
		REG_DEF(CRTC0_DVO_CONF),
		REG_DEF(CRTC0_HDISPLAY),
		REG_DEF(CRTC0_HSYNC),
		REG_DEF(CRTC0_VDISPLAY),
		REG_DEF(CRTC0_VSYNC),
		REG_DEF(CRTC0_GAMMA_INDEX),
		REG_DEF(CRTC0_GAMMA_DATA),
		REG_DEF(CRTC0_SYNC_DEVIATION),
		REG_DEF(CRTC0_VSYNC_COUNTER),
		REG_DEF(CRTC0_SCAN_POS),
		REG_DEF(CRTC0_STRIDE),
		REG_DEF(CRTC0_FB1_ADDR_HI),
		REG_DEF(CRTC0_FB1_ADDR_LO),
		REG_DEF(CRTC0_FB0_ADDR_HI),
		REG_DEF(CRTC0_FB0_ADDR_LO),
		REG_DEF(CURSOR0_CFG),
		REG_DEF(CURSOR0_POSITION),
		REG_DEF(CURSOR0_BG_COLOR),
		REG_DEF(CURSOR0_FG_COLOR),
	},
	[1] = {
		REG_DEF(CRTC1_CFG),
		REG_DEF(CRTC1_FB_ORIGIN),
		REG_DEF(CRTC1_DVO_CONF),
		REG_DEF(CRTC1_HDISPLAY),
		REG_DEF(CRTC1_HSYNC),
		REG_DEF(CRTC1_VDISPLAY),
		REG_DEF(CRTC1_VSYNC),
		REG_DEF(CRTC1_GAMMA_INDEX),
		REG_DEF(CRTC1_GAMMA_DATA),
		REG_DEF(CRTC1_SYNC_DEVIATION),
		REG_DEF(CRTC1_VSYNC_COUNTER),
		REG_DEF(CRTC1_SCAN_POS),
		REG_DEF(CRTC1_STRIDE),
		REG_DEF(CRTC1_FB1_ADDR_HI),
		REG_DEF(CRTC1_FB1_ADDR_LO),
		REG_DEF(CRTC1_FB0_ADDR_HI),
		REG_DEF(CRTC1_FB0_ADDR_LO),
		REG_DEF(CURSOR1_CFG),
		REG_DEF(CURSOR1_POSITION),
		REG_DEF(CURSOR1_BG_COLOR),
		REG_DEF(CURSOR1_FG_COLOR),
	},
};

static int lsdc_crtc_show_regs(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct lsdc_crtc *lcrtc = (struct lsdc_crtc *)node->info_ent->data;
	struct lsdc_device *ldev = lcrtc->ldev;
	unsigned int i;

	for (i = 0; i < lcrtc->nreg; i++) {
		const struct lsdc_reg32 *preg = &lcrtc->preg[i];
		u32 offset = preg->offset;

		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   preg->name, offset, lsdc_rreg32(ldev, offset));
	}

	return 0;
}

static int lsdc_crtc_show_scan_position(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct lsdc_crtc *lcrtc = (struct lsdc_crtc *)node->info_ent->data;
	int x, y;

	lcrtc->hw_ops->get_scan_pos(lcrtc, &x, &y);
	seq_printf(m, "Scanout position: x: %08u, y: %08u\n", x, y);

	return 0;
}

static int lsdc_crtc_show_vblank_counter(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct lsdc_crtc *lcrtc = (struct lsdc_crtc *)node->info_ent->data;

	if (lcrtc->hw_ops->get_vblank_counter)
		seq_printf(m, "%s vblank counter: %08u\n\n", lcrtc->base.name,
			   lcrtc->hw_ops->get_vblank_counter(lcrtc));

	return 0;
}

static int lsdc_pixpll_show_clock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct lsdc_crtc *lcrtc = (struct lsdc_crtc *)node->info_ent->data;
	struct lsdc_pixpll *pixpll = &lcrtc->pixpll;
	const struct lsdc_pixpll_funcs *funcs = pixpll->funcs;
	struct drm_crtc *crtc = &lcrtc->base;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_printer printer = drm_seq_file_printer(m);
	unsigned int out_khz;

	out_khz = funcs->get_rate(pixpll);

	seq_printf(m, "%s: %dx%d@%d\n", crtc->name,
		   mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode));

	seq_printf(m, "Pixel clock required: %d kHz\n", mode->clock);
	seq_printf(m, "Actual frequency output: %u kHz\n", out_khz);
	seq_printf(m, "Diff: %d kHz\n", out_khz - mode->clock);

	funcs->print(pixpll, &printer);

	return 0;
}

static struct drm_info_list lsdc_crtc_debugfs_list[2][4] = {
	[0] = {
		{ "regs", lsdc_crtc_show_regs, 0, NULL },
		{ "pixclk", lsdc_pixpll_show_clock, 0, NULL },
		{ "scanpos", lsdc_crtc_show_scan_position, 0, NULL },
		{ "vblanks", lsdc_crtc_show_vblank_counter, 0, NULL },
	},
	[1] = {
		{ "regs", lsdc_crtc_show_regs, 0, NULL },
		{ "pixclk", lsdc_pixpll_show_clock, 0, NULL },
		{ "scanpos", lsdc_crtc_show_scan_position, 0, NULL },
		{ "vblanks", lsdc_crtc_show_vblank_counter, 0, NULL },
	},
};

/* operate manually */

static int lsdc_crtc_man_op_show(struct seq_file *m, void *data)
{
	seq_puts(m, "soft_reset: soft reset this CRTC\n");
	seq_puts(m, "enable: enable this CRTC\n");
	seq_puts(m, "disable: disable this CRTC\n");
	seq_puts(m, "flip: trigger the page flip\n");
	seq_puts(m, "clone: clone the another crtc with hardware logic\n");

	return 0;
}

static int lsdc_crtc_man_op_open(struct inode *inode, struct file *file)
{
	struct drm_crtc *crtc = inode->i_private;

	return single_open(file, lsdc_crtc_man_op_show, crtc);
}

static ssize_t lsdc_crtc_man_op_write(struct file *file,
				      const char __user *ubuf,
				      size_t len,
				      loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct lsdc_crtc *lcrtc = m->private;
	const struct lsdc_crtc_hw_ops *ops = lcrtc->hw_ops;
	char buf[16];

	if (len > sizeof(buf) - 1)
		return -EINVAL;

	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sysfs_streq(buf, "soft_reset"))
		ops->soft_reset(lcrtc);
	else if (sysfs_streq(buf, "enable"))
		ops->enable(lcrtc);
	else if (sysfs_streq(buf, "disable"))
		ops->disable(lcrtc);
	else if (sysfs_streq(buf, "flip"))
		ops->flip(lcrtc);
	else if (sysfs_streq(buf, "clone"))
		ops->clone(lcrtc);

	return len;
}

static const struct file_operations lsdc_crtc_man_op_fops = {
	.owner = THIS_MODULE,
	.open = lsdc_crtc_man_op_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = lsdc_crtc_man_op_write,
};

static int lsdc_crtc_late_register(struct drm_crtc *crtc)
{
	struct lsdc_display_pipe *dispipe = crtc_to_display_pipe(crtc);
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	struct drm_minor *minor = crtc->dev->primary;
	unsigned int index = dispipe->index;
	unsigned int i;

	lcrtc->preg = lsdc_crtc_regs_array[index];
	lcrtc->nreg = ARRAY_SIZE(lsdc_crtc_regs_array[index]);
	lcrtc->p_info_list = lsdc_crtc_debugfs_list[index];
	lcrtc->n_info_list = ARRAY_SIZE(lsdc_crtc_debugfs_list[index]);

	for (i = 0; i < lcrtc->n_info_list; ++i)
		lcrtc->p_info_list[i].data = lcrtc;

	drm_debugfs_create_files(lcrtc->p_info_list, lcrtc->n_info_list,
				 crtc->debugfs_entry, minor);

	/* Manual operations supported */
	debugfs_create_file("ops", 0644, crtc->debugfs_entry, lcrtc,
			    &lsdc_crtc_man_op_fops);

	return 0;
}

static void lsdc_crtc_atomic_print_state(struct drm_printer *p,
					 const struct drm_crtc_state *state)
{
	const struct lsdc_crtc_state *priv_state;
	const struct lsdc_pixpll_parms *pparms;

	priv_state = container_of_const(state, struct lsdc_crtc_state, base);
	pparms = &priv_state->pparms;

	drm_printf(p, "\tInput clock divider = %u\n", pparms->div_ref);
	drm_printf(p, "\tMedium clock multiplier = %u\n", pparms->loopc);
	drm_printf(p, "\tOutput clock divider = %u\n", pparms->div_out);
}

static const struct drm_crtc_funcs ls7a1000_crtc_funcs = {
	.reset = lsdc_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = lsdc_crtc_atomic_duplicate_state,
	.atomic_destroy_state = lsdc_crtc_atomic_destroy_state,
	.late_register = lsdc_crtc_late_register,
	.enable_vblank = lsdc_crtc_enable_vblank,
	.disable_vblank = lsdc_crtc_disable_vblank,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
	.atomic_print_state = lsdc_crtc_atomic_print_state,
};

static const struct drm_crtc_funcs ls7a2000_crtc_funcs = {
	.reset = lsdc_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = lsdc_crtc_atomic_duplicate_state,
	.atomic_destroy_state = lsdc_crtc_atomic_destroy_state,
	.late_register = lsdc_crtc_late_register,
	.get_vblank_counter = lsdc_crtc_get_vblank_counter,
	.enable_vblank = lsdc_crtc_enable_vblank,
	.disable_vblank = lsdc_crtc_disable_vblank,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
	.atomic_print_state = lsdc_crtc_atomic_print_state,
};

static enum drm_mode_status
lsdc_crtc_mode_valid(struct drm_crtc *crtc, const struct drm_display_mode *mode)
{
	struct drm_device *ddev = crtc->dev;
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct lsdc_desc *descp = ldev->descp;
	unsigned int pitch;

	if (mode->hdisplay > descp->max_width)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > descp->max_height)
		return MODE_BAD_VVALUE;

	if (mode->clock > descp->max_pixel_clk) {
		drm_dbg_kms(ddev, "mode %dx%d, pixel clock=%d is too high\n",
			    mode->hdisplay, mode->vdisplay, mode->clock);
		return MODE_CLOCK_HIGH;
	}

	/* 4 for DRM_FORMAT_XRGB8888 */
	pitch = mode->hdisplay * 4;

	if (pitch % descp->pitch_align) {
		drm_dbg_kms(ddev, "align to %u bytes is required: %u\n",
			    descp->pitch_align, pitch);
		return MODE_BAD_WIDTH;
	}

	return MODE_OK;
}

static int lsdc_pixpll_atomic_check(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	struct lsdc_pixpll *pixpll = &lcrtc->pixpll;
	const struct lsdc_pixpll_funcs *pfuncs = pixpll->funcs;
	struct lsdc_crtc_state *priv_state = to_lsdc_crtc_state(state);
	unsigned int clock = state->mode.clock;
	int ret;

	ret = pfuncs->compute(pixpll, clock, &priv_state->pparms);
	if (ret) {
		drm_warn(crtc->dev, "Failed to find PLL params for %ukHz\n",
			 clock);
		return -EINVAL;
	}

	return 0;
}

static int lsdc_crtc_helper_atomic_check(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

	if (!crtc_state->enable)
		return 0;

	return lsdc_pixpll_atomic_check(crtc, crtc_state);
}

static void lsdc_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	const struct lsdc_crtc_hw_ops *crtc_hw_ops = lcrtc->hw_ops;
	struct lsdc_pixpll *pixpll = &lcrtc->pixpll;
	const struct lsdc_pixpll_funcs *pixpll_funcs = pixpll->funcs;
	struct drm_crtc_state *state = crtc->state;
	struct drm_display_mode *mode = &state->mode;
	struct lsdc_crtc_state *priv_state = to_lsdc_crtc_state(state);

	pixpll_funcs->update(pixpll, &priv_state->pparms);

	if (crtc_hw_ops->set_dma_step) {
		unsigned int width_in_bytes = mode->hdisplay * 4;
		enum lsdc_dma_steps dma_step;

		/*
		 * Using DMA step as large as possible, for improving
		 * hardware DMA efficiency.
		 */
		if (width_in_bytes % 256 == 0)
			dma_step = LSDC_DMA_STEP_256_BYTES;
		else if (width_in_bytes % 128 == 0)
			dma_step = LSDC_DMA_STEP_128_BYTES;
		else if (width_in_bytes % 64 == 0)
			dma_step = LSDC_DMA_STEP_64_BYTES;
		else  /* width_in_bytes % 32 == 0 */
			dma_step = LSDC_DMA_STEP_32_BYTES;

		crtc_hw_ops->set_dma_step(lcrtc, dma_step);
	}

	crtc_hw_ops->set_mode(lcrtc, mode);
}

static void lsdc_crtc_send_vblank(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	unsigned long flags;

	if (!crtc->state || !crtc->state->event)
		return;

	drm_dbg(ddev, "Send vblank manually\n");

	spin_lock_irqsave(&ddev->event_lock, flags);
	drm_crtc_send_vblank_event(crtc, crtc->state->event);
	crtc->state->event = NULL;
	spin_unlock_irqrestore(&ddev->event_lock, flags);
}

static void lsdc_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);

	if (lcrtc->has_vblank)
		drm_crtc_vblank_on(crtc);

	lcrtc->hw_ops->enable(lcrtc);
}

static void lsdc_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);

	if (lcrtc->has_vblank)
		drm_crtc_vblank_off(crtc);

	lcrtc->hw_ops->disable(lcrtc);

	/*
	 * Make sure we issue a vblank event after disabling the CRTC if
	 * someone was waiting it.
	 */
	lsdc_crtc_send_vblank(crtc);
}

static void lsdc_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
{
	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);
}

static bool lsdc_crtc_get_scanout_position(struct drm_crtc *crtc,
					   bool in_vblank_irq,
					   int *vpos,
					   int *hpos,
					   ktime_t *stime,
					   ktime_t *etime,
					   const struct drm_display_mode *mode)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	const struct lsdc_crtc_hw_ops *ops = lcrtc->hw_ops;
	int vsw, vbp, vactive_start, vactive_end, vfp_end;
	int x, y;

	vsw = mode->crtc_vsync_end - mode->crtc_vsync_start;
	vbp = mode->crtc_vtotal - mode->crtc_vsync_end;

	vactive_start = vsw + vbp + 1;
	vactive_end = vactive_start + mode->crtc_vdisplay;

	/* last scan line before VSYNC */
	vfp_end = mode->crtc_vtotal;

	if (stime)
		*stime = ktime_get();

	ops->get_scan_pos(lcrtc, &x, &y);

	if (y > vactive_end)
		y = y - vfp_end - vactive_start;
	else
		y -= vactive_start;

	*vpos = y;
	*hpos = 0;

	if (etime)
		*etime = ktime_get();

	return true;
}

static const struct drm_crtc_helper_funcs lsdc_crtc_helper_funcs = {
	.mode_valid = lsdc_crtc_mode_valid,
	.mode_set_nofb = lsdc_crtc_mode_set_nofb,
	.atomic_enable = lsdc_crtc_atomic_enable,
	.atomic_disable = lsdc_crtc_atomic_disable,
	.atomic_check = lsdc_crtc_helper_atomic_check,
	.atomic_flush = lsdc_crtc_atomic_flush,
	.get_scanout_position = lsdc_crtc_get_scanout_position,
};

int ls7a1000_crtc_init(struct drm_device *ddev,
		       struct drm_crtc *crtc,
		       struct drm_plane *primary,
		       struct drm_plane *cursor,
		       unsigned int index,
		       bool has_vblank)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	int ret;

	ret = lsdc_pixpll_init(&lcrtc->pixpll, ddev, index);
	if (ret) {
		drm_err(ddev, "pixel pll init failed: %d\n", ret);
		return ret;
	}

	lcrtc->ldev = to_lsdc(ddev);
	lcrtc->has_vblank = has_vblank;
	lcrtc->hw_ops = &ls7a1000_crtc_hw_ops[index];

	ret = drm_crtc_init_with_planes(ddev, crtc, primary, cursor,
					&ls7a1000_crtc_funcs,
					"LS-CRTC-%d", index);
	if (ret) {
		drm_err(ddev, "crtc init with planes failed: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(crtc, &lsdc_crtc_helper_funcs);

	ret = drm_mode_crtc_set_gamma_size(crtc, 256);
	if (ret)
		return ret;

	drm_crtc_enable_color_mgmt(crtc, 0, false, 256);

	return 0;
}

int ls7a2000_crtc_init(struct drm_device *ddev,
		       struct drm_crtc *crtc,
		       struct drm_plane *primary,
		       struct drm_plane *cursor,
		       unsigned int index,
		       bool has_vblank)
{
	struct lsdc_crtc *lcrtc = to_lsdc_crtc(crtc);
	int ret;

	ret = lsdc_pixpll_init(&lcrtc->pixpll, ddev, index);
	if (ret) {
		drm_err(ddev, "crtc init with pll failed: %d\n", ret);
		return ret;
	}

	lcrtc->ldev = to_lsdc(ddev);
	lcrtc->has_vblank = has_vblank;
	lcrtc->hw_ops = &ls7a2000_crtc_hw_ops[index];

	ret = drm_crtc_init_with_planes(ddev, crtc, primary, cursor,
					&ls7a2000_crtc_funcs,
					"LS-CRTC-%u", index);
	if (ret) {
		drm_err(ddev, "crtc init with planes failed: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(crtc, &lsdc_crtc_helper_funcs);

	ret = drm_mode_crtc_set_gamma_size(crtc, 256);
	if (ret)
		return ret;

	drm_crtc_enable_color_mgmt(crtc, 0, false, 256);

	return 0;
}
