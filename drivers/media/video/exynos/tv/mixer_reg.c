/*
 * Samsung TV Mixer driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include "mixer.h"
#include "regs-mixer.h"
#include "regs-vp.h"

#include <plat/cpu.h>
#include <linux/delay.h>

/* Register access subroutines */

static inline u32 vp_read(struct mxr_device *mdev, u32 reg_id)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	return readl(mdev->res.vp_regs + reg_id);
#else
	return 0;
#endif
}

static inline void vp_write(struct mxr_device *mdev, u32 reg_id, u32 val)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	writel(val, mdev->res.vp_regs + reg_id);
#endif
}

static inline void vp_write_mask(struct mxr_device *mdev, u32 reg_id,
	u32 val, u32 mask)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	u32 old = vp_read(mdev, reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, mdev->res.vp_regs + reg_id);
#endif
}

static inline u32 mxr_read(struct mxr_device *mdev, u32 reg_id)
{
	return readl(mdev->res.mxr_regs + reg_id);
}

static inline void mxr_write(struct mxr_device *mdev, u32 reg_id, u32 val)
{
	writel(val, mdev->res.mxr_regs + reg_id);
}

static inline void mxr_write_mask(struct mxr_device *mdev, u32 reg_id,
	u32 val, u32 mask)
{
	u32 old = mxr_read(mdev, reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, mdev->res.mxr_regs + reg_id);
}

void mxr_layer_sync(struct mxr_device *mdev, int en)
{
	mxr_write_mask(mdev, MXR_STATUS, en ? MXR_STATUS_LAYER_SYNC : 0,
		MXR_STATUS_LAYER_SYNC);
}

void mxr_vsync_set_update(struct mxr_device *mdev, int en)
{
	/* block update on vsync */
	mxr_write_mask(mdev, MXR_STATUS, en ? MXR_STATUS_SYNC_ENABLE : 0,
		MXR_STATUS_SYNC_ENABLE);
#if defined(CONFIG_ARCH_EXYNOS4)
	vp_write(mdev, VP_SHADOW_UPDATE, en ? VP_SHADOW_UPDATE_ENABLE : 0);
#endif
}

static void __mxr_reg_vp_reset(struct mxr_device *mdev)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	int tries = 100;

	vp_write(mdev, VP_SRESET, VP_SRESET_PROCESSING);
	for (tries = 100; tries; --tries) {
		/* waiting until VP_SRESET_PROCESSING is 0 */
		if (~vp_read(mdev, VP_SRESET) & VP_SRESET_PROCESSING)
			break;
		mdelay(10);
	}
	WARN(tries == 0, "failed to reset Video Processor\n");
#endif
}

static void mxr_reg_sub_mxr_reset(struct mxr_device *mdev, int mxr_num)
{
	u32 val; /* value stored to register */

	if (mxr_num == MXR_SUB_MIXER0) {
		/* use dark gray background color */
		mxr_write(mdev, MXR_BG_COLOR0, 0x008080);
		mxr_write(mdev, MXR_BG_COLOR1, 0x008080);
		mxr_write(mdev, MXR_BG_COLOR2, 0x008080);

		/* setting graphical layers */

		val  = MXR_GRP_CFG_BLANK_KEY_OFF; /* no blank key */
		val |= MXR_GRP_CFG_ALPHA_VAL(0xff); /* non-transparent alpha */

		/* the same configuration for both layers */
		mxr_write(mdev, MXR_GRAPHIC_CFG(0), val);
		mxr_write(mdev, MXR_GRAPHIC_CFG(1), val);
	} else if (mxr_num == MXR_SUB_MIXER1) {
		val  = MXR_LAYER_CFG_GRP1_VAL(3);
		val |= MXR_LAYER_CFG_GRP0_VAL(2);
		val |= MXR_LAYER_CFG_VP_VAL(1);
		mxr_write(mdev, MXR1_LAYER_CFG, val);

		/* use dark gray background color */
		mxr_write(mdev, MXR1_BG_COLOR0, 0x008080);
		mxr_write(mdev, MXR1_BG_COLOR1, 0x008080);
		mxr_write(mdev, MXR1_BG_COLOR2, 0x008080);

		/* setting graphical layers */

		val  = MXR_GRP_CFG_BLANK_KEY_OFF; /* no blank key */
		val |= MXR_GRP_CFG_ALPHA_VAL(0xff); /* non-transparent alpha */

		/* the same configuration for both layers */
		mxr_write(mdev, MXR1_GRAPHIC_CFG(0), val);
		mxr_write(mdev, MXR1_GRAPHIC_CFG(1), val);
	}
}

static void mxr_reg_vp_default_filter(struct mxr_device *mdev);

void mxr_reg_reset(struct mxr_device *mdev)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);

	mxr_write_mask(mdev, MXR_STATUS, ~0, MXR_STATUS_SOFT_RESET);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	/* set output in RGB888 mode */
	mxr_write(mdev, MXR_CFG, MXR_CFG_OUT_RGB888);

	/* 16 beat burst in DMA */
	mxr_write_mask(mdev, MXR_STATUS, MXR_STATUS_16_BURST,
		MXR_STATUS_BURST_MASK);

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i)
		mxr_reg_sub_mxr_reset(mdev, i);

	/* configuration of Video Processor Registers */
	__mxr_reg_vp_reset(mdev);
	mxr_reg_vp_default_filter(mdev);

	/* enable all interrupts */
	mxr_write_mask(mdev, MXR_INT_EN, ~0, MXR_INT_EN_ALL);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_graph_format(struct mxr_device *mdev, int idx,
	const struct mxr_format *fmt, const struct mxr_geometry *geo)
{
	u32 wh, sxy, dxy;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	/* Mostly, src width, height and dst width, height are same.
	 * However, in case of doing src and dst cropping, those are different.
	 * So you have to write dst width and height to MXR_GRAPHIC_WH register.
	 */
	wh  = MXR_GRP_WH_WIDTH(geo->dst.width);
	wh |= MXR_GRP_WH_HEIGHT(geo->dst.height);
	wh |= MXR_GRP_WH_H_SCALE(geo->x_ratio);
	wh |= MXR_GRP_WH_V_SCALE(geo->y_ratio);

	/* setup offsets in source image */
	sxy  = MXR_GRP_SXY_SX(geo->src.x_offset);
	sxy |= MXR_GRP_SXY_SY(geo->src.y_offset);

	/* setup offsets in display image */
	dxy  = MXR_GRP_DXY_DX(geo->dst.x_offset);
	dxy |= MXR_GRP_DXY_DY(geo->dst.y_offset);

	if (idx == 0) {
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(0),
				MXR_GRP_CFG_FORMAT_VAL(fmt->cookie),
				MXR_GRP_CFG_FORMAT_MASK);
		mxr_write(mdev, MXR_GRAPHIC_SPAN(0), geo->src.full_width);
		mxr_write(mdev, MXR_GRAPHIC_WH(0), wh);
		mxr_write(mdev, MXR_GRAPHIC_SXY(0), sxy);
		mxr_write(mdev, MXR_GRAPHIC_DXY(0), dxy);
	} else if (idx == 1) {
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(1),
				MXR_GRP_CFG_FORMAT_VAL(fmt->cookie),
				MXR_GRP_CFG_FORMAT_MASK);
		mxr_write(mdev, MXR_GRAPHIC_SPAN(1), geo->src.full_width);
		mxr_write(mdev, MXR_GRAPHIC_WH(1), wh);
		mxr_write(mdev, MXR_GRAPHIC_SXY(1), sxy);
		mxr_write(mdev, MXR_GRAPHIC_DXY(1), dxy);
	} else if (idx == 2) {
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(0),
				MXR_GRP_CFG_FORMAT_VAL(fmt->cookie),
				MXR_GRP_CFG_FORMAT_MASK);
		mxr_write(mdev, MXR1_GRAPHIC_SPAN(0), geo->src.full_width);
		mxr_write(mdev, MXR1_GRAPHIC_WH(0), wh);
		mxr_write(mdev, MXR1_GRAPHIC_SXY(0), sxy);
		mxr_write(mdev, MXR1_GRAPHIC_DXY(0), dxy);
	} else if (idx == 3) {
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(1),
				MXR_GRP_CFG_FORMAT_VAL(fmt->cookie),
				MXR_GRP_CFG_FORMAT_MASK);
		mxr_write(mdev, MXR1_GRAPHIC_SPAN(1), geo->src.full_width);
		mxr_write(mdev, MXR1_GRAPHIC_WH(1), wh);
		mxr_write(mdev, MXR1_GRAPHIC_SXY(1), sxy);
		mxr_write(mdev, MXR1_GRAPHIC_DXY(1), dxy);
	}

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_video_geo(struct mxr_device *mdev, int cur_mxr, int idx,
		const struct mxr_geometry *geo)
{
	u32 lt, rb;
	unsigned long flags;

	mxr_dbg(mdev, "%s\n", __func__);

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	lt  = MXR_VIDEO_LT_LEFT_VAL(geo->dst.x_offset);
	lt |= MXR_VIDEO_LT_TOP_VAL(geo->dst.y_offset);
	rb  = MXR_VIDEO_RB_RIGHT_VAL(geo->dst.x_offset + geo->dst.width - 1);
	rb |= MXR_VIDEO_RB_BOTTOM_VAL(geo->dst.y_offset + geo->dst.height - 1);

	if (cur_mxr == MXR_SUB_MIXER0) {
		mxr_write(mdev, MXR_VIDEO_LT, lt);
		mxr_write(mdev, MXR_VIDEO_RB, rb);
	} else if (cur_mxr == MXR_SUB_MIXER1) {
		mxr_write(mdev, MXR1_VIDEO_LT, lt);
		mxr_write(mdev, MXR1_VIDEO_RB, rb);
	}

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);

	mxr_dbg(mdev, "destination x = %d, y = %d, width = %d, height = %d\n",
			geo->dst.x_offset, geo->dst.y_offset,
			geo->dst.width, geo->dst.height);
}

void mxr_reg_vp_format(struct mxr_device *mdev,
	const struct mxr_format *fmt, const struct mxr_geometry *geo)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	vp_write_mask(mdev, VP_MODE, fmt->cookie, VP_MODE_FMT_MASK);

	/* setting size of input image */
	vp_write(mdev, VP_IMG_SIZE_Y, VP_IMG_HSIZE(geo->src.full_width) |
		VP_IMG_VSIZE(geo->src.full_height));
	/* chroma height has to reduced by 2 to avoid chroma distorions */
	vp_write(mdev, VP_IMG_SIZE_C, VP_IMG_HSIZE(geo->src.full_width) |
		VP_IMG_VSIZE(geo->src.full_height / 2));

	vp_write(mdev, VP_SRC_WIDTH, geo->src.width);
	vp_write(mdev, VP_SRC_HEIGHT, geo->src.height);
	vp_write(mdev, VP_SRC_H_POSITION,
		VP_SRC_H_POSITION_VAL(geo->src.x_offset));
	vp_write(mdev, VP_SRC_V_POSITION, geo->src.y_offset);

	vp_write(mdev, VP_DST_WIDTH, geo->dst.width);
	vp_write(mdev, VP_DST_H_POSITION, geo->dst.x_offset);
	if (geo->dst.field == V4L2_FIELD_INTERLACED) {
		vp_write(mdev, VP_DST_HEIGHT, geo->dst.height / 2);
		vp_write(mdev, VP_DST_V_POSITION, geo->dst.y_offset / 2);
	} else {
		vp_write(mdev, VP_DST_HEIGHT, geo->dst.height);
		vp_write(mdev, VP_DST_V_POSITION, geo->dst.y_offset);
	}

	vp_write(mdev, VP_H_RATIO, geo->x_ratio);
	vp_write(mdev, VP_V_RATIO, geo->y_ratio);

	vp_write(mdev, VP_ENDIAN_MODE, VP_ENDIAN_MODE_LITTLE);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
#endif
}

void mxr_reg_graph_buffer(struct mxr_device *mdev, int idx, dma_addr_t addr)
{
	u32 val = addr ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (idx == 0) {
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_GRP0_ENABLE);
		mxr_write(mdev, MXR_GRAPHIC_BASE(0), addr);
	} else if (idx == 1) {
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_GRP1_ENABLE);
		mxr_write(mdev, MXR_GRAPHIC_BASE(1), addr);
	} else if (idx == 2) {
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_MX1_GRP0_ENABLE);
		mxr_write(mdev, MXR1_GRAPHIC_BASE(0), addr);
	} else if (idx == 3) {
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_MX1_GRP1_ENABLE);
		mxr_write(mdev, MXR1_GRAPHIC_BASE(1), addr);
	}

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_vp_buffer(struct mxr_device *mdev,
	dma_addr_t luma_addr[2], dma_addr_t chroma_addr[2])
{
#if defined(CONFIG_ARCH_EXYNOS4)
	u32 val = luma_addr[0] ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_VIDEO_ENABLE);
	vp_write_mask(mdev, VP_ENABLE, val, VP_ENABLE_ON);
	/* TODO: fix tiled mode */
	vp_write(mdev, VP_TOP_Y_PTR, luma_addr[0]);
	vp_write(mdev, VP_TOP_C_PTR, chroma_addr[0]);
	vp_write(mdev, VP_BOT_Y_PTR, luma_addr[1]);
	vp_write(mdev, VP_BOT_C_PTR, chroma_addr[1]);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
#endif
}

void mxr_reg_set_layer_prio(struct mxr_device *mdev)
{
	u8 p_g0, p_g1, p_v;
	u32 val;

	p_v = mdev->sub_mxr[MXR_SUB_MIXER0].layer[MXR_LAYER_VIDEO]->prio;
	p_g0 = mdev->sub_mxr[MXR_SUB_MIXER0].layer[MXR_LAYER_GRP0]->prio;
	p_g1 = mdev->sub_mxr[MXR_SUB_MIXER0].layer[MXR_LAYER_GRP1]->prio;
	mxr_dbg(mdev, "video layer priority = %d\n", p_v);
	mxr_dbg(mdev, "graphic0 layer priority = %d\n", p_g0);
	mxr_dbg(mdev, "graphic1 layer priority = %d\n", p_g1);

	val  = MXR_LAYER_CFG_GRP1_VAL(p_g1);
	val |= MXR_LAYER_CFG_GRP0_VAL(p_g0);
	val |= MXR_LAYER_CFG_VP_VAL(p_v);
	mxr_write(mdev, MXR_LAYER_CFG, val);
}

void mxr_reg_set_layer_blend(struct mxr_device *mdev, int sub_mxr, int num,
		int en)
{
	u32 val = en ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_VIDEO)
		mxr_write_mask(mdev, MXR_VIDEO_CFG, val,
				MXR_VIDEO_CFG_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_LAYER_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_LAYER_BLEND_EN);
#if defined(CONFIG_ARCH_EXYNOS5)
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_VIDEO)
		mxr_write_mask(mdev, MXR1_VIDEO_CFG, val,
				MXR_VIDEO_CFG_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_LAYER_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_LAYER_BLEND_EN);
#endif

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_layer_alpha(struct mxr_device *mdev, int sub_mxr, int num, u32 a)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_VIDEO)
		mxr_write_mask(mdev, MXR_VIDEO_CFG, MXR_VIDEO_CFG_ALPHA(a),
				0xff);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(0), MXR_GRP_CFG_ALPHA_VAL(a),
				0xff);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(1), MXR_GRP_CFG_ALPHA_VAL(a),
				0xff);
#if defined(CONFIG_ARCH_EXYNOS5)
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_VIDEO)
		mxr_write_mask(mdev, MXR1_VIDEO_CFG, MXR_VIDEO_CFG_ALPHA(a),
				0xff);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(0), MXR_GRP_CFG_ALPHA_VAL(a),
				0xff);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(1), MXR_GRP_CFG_ALPHA_VAL(a),
				0xff);
#endif

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_set_pixel_blend(struct mxr_device *mdev, int sub_mxr, int num,
		int en)
{
	u32 val = en ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_PIXEL_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_PIXEL_BLEND_EN);
#if defined(CONFIG_ARCH_EXYNOS5)
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_PIXEL_BLEND_EN);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_PIXEL_BLEND_EN);
#endif

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_set_colorkey(struct mxr_device *mdev, int sub_mxr, int num, int en)
{
	u32 val = en ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_BLANK_KEY_OFF);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_BLANK_KEY_OFF);
#if defined(CONFIG_ARCH_EXYNOS5)
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP0)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(0), val,
				MXR_GRP_CFG_BLANK_KEY_OFF);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP1)
		mxr_write_mask(mdev, MXR1_GRAPHIC_CFG(1), val,
				MXR_GRP_CFG_BLANK_KEY_OFF);
#endif

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_colorkey_val(struct mxr_device *mdev, int sub_mxr, int num, u32 v)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP0)
		mxr_write(mdev, MXR_GRAPHIC_BLANK(0), v);
	else if (sub_mxr == MXR_SUB_MIXER0 && num == MXR_LAYER_GRP1)
		mxr_write(mdev, MXR_GRAPHIC_BLANK(1), v);
#if defined(CONFIG_ARCH_EXYNOS5)
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP0)
		mxr_write(mdev, MXR1_GRAPHIC_BLANK(0), v);
	else if (sub_mxr == MXR_SUB_MIXER1 && num == MXR_LAYER_GRP1)
		mxr_write(mdev, MXR1_GRAPHIC_BLANK(1), v);
#endif

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

static void mxr_irq_layer_handle(struct mxr_layer *layer)
{
	struct list_head *head = &layer->enq_list;
	struct mxr_pipeline *pipe = &layer->pipe;
	struct mxr_buffer *done;

	/* skip non-existing layer */
	if (layer == NULL)
		return;

	spin_lock(&layer->enq_slock);
	if (pipe->state == MXR_PIPELINE_IDLE)
		goto done;

	done = layer->shadow_buf;
	layer->shadow_buf = layer->update_buf;

	if (list_empty(head)) {
		if (pipe->state != MXR_PIPELINE_STREAMING)
			layer->update_buf = NULL;
	} else {
		struct mxr_buffer *next;
		next = list_first_entry(head, struct mxr_buffer, list);
		list_del(&next->list);
		layer->update_buf = next;
	}

	layer->ops.buffer_set(layer, layer->update_buf);

	if (done && done != layer->shadow_buf)
		vb2_buffer_done(&done->vb, VB2_BUF_STATE_DONE);

done:
	spin_unlock(&layer->enq_slock);
}

u32 mxr_irq_underrun_handle(struct mxr_device *mdev, u32 val)
{
	if (val & MXR_INT_STATUS_MX0_VIDEO) {
		mxr_dbg(mdev, "mixer0 video layer underrun occur\n");
		val |= MXR_INT_STATUS_MX0_VIDEO;
	} else if (val & MXR_INT_STATUS_MX0_GRP0) {
		mxr_dbg(mdev, "mixer0 graphic0 layer underrun occur\n");
		val |= MXR_INT_STATUS_MX0_GRP0;
	} else if (val & MXR_INT_STATUS_MX0_GRP1) {
		mxr_dbg(mdev, "mixer0 graphic1 layer underrun occur\n");
		val |= MXR_INT_STATUS_MX0_GRP1;
	} else if (val & MXR_INT_STATUS_MX1_VIDEO) {
		mxr_dbg(mdev, "mixer1 video layer underrun occur\n");
		val |= MXR_INT_STATUS_MX1_VIDEO;
	} else if (val & MXR_INT_STATUS_MX1_GRP0) {
		mxr_dbg(mdev, "mixer1 graphic0 layer underrun occur\n");
		val |= MXR_INT_STATUS_MX1_GRP0;
	} else if (val & MXR_INT_STATUS_MX1_GRP1) {
		mxr_dbg(mdev, "mixer1 graphic1 layer underrun occur\n");
		val |= MXR_INT_STATUS_MX1_GRP1;
	}

	return val;
}

irqreturn_t mxr_irq_handler(int irq, void *dev_data)
{
	struct mxr_device *mdev = dev_data;
	u32 i, val;

	spin_lock(&mdev->reg_slock);
	val = mxr_read(mdev, MXR_INT_STATUS);

	/* wake up process waiting for VSYNC */
	if (val & MXR_INT_STATUS_VSYNC) {
		set_bit(MXR_EVENT_VSYNC, &mdev->event_flags);
		wake_up(&mdev->event_queue);
	}

	/* clear interrupts.
	   vsync is updated after write MXR_CFG_LAYER_UPDATE bit */
	if (val & MXR_INT_CLEAR_VSYNC)
		mxr_write_mask(mdev, MXR_INT_STATUS, ~0, MXR_INT_CLEAR_VSYNC);

	val = mxr_irq_underrun_handle(mdev, val);
	mxr_write(mdev, MXR_INT_STATUS, val);

	spin_unlock(&mdev->reg_slock);
	/* leave on non-vsync event */
	if (~val & MXR_INT_CLEAR_VSYNC)
		return IRQ_HANDLED;

	for (i = 0; i < MXR_MAX_SUB_MIXERS; ++i) {
#if defined(CONFIG_ARCH_EXYNOS4)
		mxr_irq_layer_handle(mdev->sub_mxr[i].layer[MXR_LAYER_VIDEO]);
#endif
		mxr_irq_layer_handle(mdev->sub_mxr[i].layer[MXR_LAYER_GRP0]);
		mxr_irq_layer_handle(mdev->sub_mxr[i].layer[MXR_LAYER_GRP1]);
	}

	if (test_bit(MXR_EVENT_VSYNC, &mdev->event_flags)) {
		spin_lock(&mdev->reg_slock);
		mxr_write_mask(mdev, MXR_CFG, ~0, MXR_CFG_LAYER_UPDATE);
		spin_unlock(&mdev->reg_slock);
	}

	return IRQ_HANDLED;
}

void mxr_reg_s_output(struct mxr_device *mdev, int cookie)
{
	u32 val;

	val = cookie == 0 ? MXR_CFG_DST_SDO : MXR_CFG_DST_HDMI;
	mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_DST_MASK);
}

void mxr_reg_streamon(struct mxr_device *mdev)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	/* single write -> no need to block vsync update */

	/* start MIXER */
	mxr_write_mask(mdev, MXR_STATUS, ~0, MXR_STATUS_REG_RUN);

	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_streamoff(struct mxr_device *mdev)
{
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	/* single write -> no need to block vsync update */

	/* stop MIXER */
	mxr_write_mask(mdev, MXR_STATUS, 0, MXR_STATUS_REG_RUN);

	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

int mxr_reg_wait4vsync(struct mxr_device *mdev)
{
	int ret;

	clear_bit(MXR_EVENT_VSYNC, &mdev->event_flags);
	/* TODO: consider adding interruptible */
	ret = wait_event_timeout(mdev->event_queue,
		test_bit(MXR_EVENT_VSYNC, &mdev->event_flags),
		msecs_to_jiffies(1000));
	if (ret > 0)
		return 0;
	if (ret < 0)
		return ret;
	mxr_warn(mdev, "no vsync detected - timeout\n");
	return -ETIME;
}

void mxr_reg_set_mbus_fmt(struct mxr_device *mdev,
	struct v4l2_mbus_framefmt *fmt)
{
	u32 val = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	/* choosing between YUV444 and RGB888 as mixer output type */
	if (mdev->sub_mxr[MXR_SUB_MIXER0].mbus_fmt[MXR_PAD_SOURCE_GRP0].code ==
		V4L2_MBUS_FMT_YUV8_1X24) {
		val = MXR_CFG_OUT_YUV444;
		fmt->code = V4L2_MBUS_FMT_YUV8_1X24;
	} else {
		val = MXR_CFG_OUT_RGB888;
		fmt->code = V4L2_MBUS_FMT_XRGB8888_4X8_LE;
	}

	/* choosing between interlace and progressive mode */
	if (fmt->field == V4L2_FIELD_INTERLACED)
		val |= MXR_CFG_SCAN_INTERLACE;
	else
		val |= MXR_CFG_SCAN_PROGRASSIVE;

	/* choosing between porper HD and SD mode */
	if (fmt->height == 480)
		val |= MXR_CFG_SCAN_NTSC | MXR_CFG_SCAN_SD;
	else if (fmt->height == 576)
		val |= MXR_CFG_SCAN_PAL | MXR_CFG_SCAN_SD;
	else if (fmt->height == 720)
		val |= MXR_CFG_SCAN_HD_720 | MXR_CFG_SCAN_HD;
	else if (fmt->height == 1080)
		val |= MXR_CFG_SCAN_HD_1080 | MXR_CFG_SCAN_HD;
	else
		WARN(1, "unrecognized mbus height %u!\n", fmt->height);

	mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_SCAN_MASK |
			MXR_CFG_OUT_MASK);

	val = (fmt->field == V4L2_FIELD_INTERLACED) ? ~0 : 0;
	vp_write_mask(mdev, VP_MODE, val,
		VP_MODE_LINE_SKIP | VP_MODE_FIELD_ID_AUTO_TOGGLING);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_local_path_clear(struct mxr_device *mdev)
{
	u32 val;

	val = readl(SYSREG_DISP1BLK_CFG);
	val &= ~(DISP1BLK_CFG_MIXER0_VALID | DISP1BLK_CFG_MIXER1_VALID);
	writel(val, SYSREG_DISP1BLK_CFG);
	mxr_dbg(mdev, "SYSREG_DISP1BLK_CFG = 0x%x\n", readl(SYSREG_DISP1BLK_CFG));
}

void mxr_reg_local_path_set(struct mxr_device *mdev, int mxr0_gsc, int mxr1_gsc,
		u32 flags)
{
	u32 val = 0;
	int mxr0_local = mdev->sub_mxr[MXR_SUB_MIXER0].local;
	int mxr1_local = mdev->sub_mxr[MXR_SUB_MIXER1].local;

	if (mxr0_local && !mxr1_local) { /* 1-path : sub-mixer0 */
		val  = MXR_TVOUT_CFG_ONE_PATH;
		val |= MXR_TVOUT_CFG_PATH_MIXER0;
	} else if (!mxr0_local && mxr1_local) { /* 1-path : sub-mixer1 */
		val  = MXR_TVOUT_CFG_ONE_PATH;
		val |= MXR_TVOUT_CFG_PATH_MIXER1;
	} else if (mxr0_local && mxr1_local) { /* 2-path */
		val  = MXR_TVOUT_CFG_TWO_PATH;
		val |= MXR_TVOUT_CFG_STEREO_SCOPIC;
	}

	mxr_write(mdev, MXR_TVOUT_CFG, val);

	/* set local path gscaler to mixer */
	val = readl(SYSREG_DISP1BLK_CFG);
	val |= DISP1BLK_CFG_FIFORST_DISP1;
	val &= ~DISP1BLK_CFG_MIXER_MASK;
	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (mxr0_local) {
			val |= DISP1BLK_CFG_MIXER0_VALID;
			val |= DISP1BLK_CFG_MIXER0_SRC_GSC(mxr0_gsc);
		}
		if (mxr1_local) {
			val |= DISP1BLK_CFG_MIXER1_VALID;
			val |= DISP1BLK_CFG_MIXER1_SRC_GSC(mxr1_gsc);
		}
	}
	mxr_dbg(mdev, "%s: SYSREG value = 0x%x\n", __func__, val);
	writel(val, SYSREG_DISP1BLK_CFG);
}

void mxr_reg_graph_layer_stream(struct mxr_device *mdev, int idx, int en)
{
	u32 val = 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (mdev->frame_packing) {
		val  = MXR_TVOUT_CFG_TWO_PATH;
		val |= MXR_TVOUT_CFG_STEREO_SCOPIC;
	} else {
		val  = MXR_TVOUT_CFG_ONE_PATH;
		val |= MXR_TVOUT_CFG_PATH_MIXER0;
	}

	mxr_write(mdev, MXR_TVOUT_CFG, val);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_vp_layer_stream(struct mxr_device *mdev, int en)
{
	/* no extra actions need to be done */
}

void mxr_reg_video_layer_stream(struct mxr_device *mdev, int idx, int en)
{
	u32 val = en ? ~0 : 0;

	if (idx == 0)
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_VIDEO_ENABLE);
	else if (idx == 1)
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_MX1_VIDEO_ENABLE);
}

static const u8 filter_y_horiz_tap8[] = {
	0,	-1,	-1,	-1,	-1,	-1,	-1,	-1,
	-1,	-1,	-1,	-1,	-1,	0,	0,	0,
	0,	2,	4,	5,	6,	6,	6,	6,
	6,	5,	5,	4,	3,	2,	1,	1,
	0,	-6,	-12,	-16,	-18,	-20,	-21,	-20,
	-20,	-18,	-16,	-13,	-10,	-8,	-5,	-2,
	127,	126,	125,	121,	114,	107,	99,	89,
	79,	68,	57,	46,	35,	25,	16,	8,
};

static const u8 filter_y_vert_tap4[] = {
	0,	-3,	-6,	-8,	-8,	-8,	-8,	-7,
	-6,	-5,	-4,	-3,	-2,	-1,	-1,	0,
	127,	126,	124,	118,	111,	102,	92,	81,
	70,	59,	48,	37,	27,	19,	11,	5,
	0,	5,	11,	19,	27,	37,	48,	59,
	70,	81,	92,	102,	111,	118,	124,	126,
	0,	0,	-1,	-1,	-2,	-3,	-4,	-5,
	-6,	-7,	-8,	-8,	-8,	-8,	-6,	-3,
};

static const u8 filter_cr_horiz_tap4[] = {
	0,	-3,	-6,	-8,	-8,	-8,	-8,	-7,
	-6,	-5,	-4,	-3,	-2,	-1,	-1,	0,
	127,	126,	124,	118,	111,	102,	92,	81,
	70,	59,	48,	37,	27,	19,	11,	5,
};

static inline void mxr_reg_vp_filter_set(struct mxr_device *mdev,
	int reg_id, const u8 *data, unsigned int size)
{
	/* assure 4-byte align */
	BUG_ON(size & 3);
	for (; size; size -= 4, reg_id += 4, data += 4) {
		u32 val = (data[0] << 24) |  (data[1] << 16) |
			(data[2] << 8) | data[3];
		vp_write(mdev, reg_id, val);
	}
}

static void mxr_reg_vp_default_filter(struct mxr_device *mdev)
{
#if defined(CONFIG_ARCH_EXYNOS4)
	mxr_reg_vp_filter_set(mdev, VP_POLY8_Y0_LL,
		filter_y_horiz_tap8, sizeof filter_y_horiz_tap8);
	mxr_reg_vp_filter_set(mdev, VP_POLY4_Y0_LL,
		filter_y_vert_tap4, sizeof filter_y_vert_tap4);
	mxr_reg_vp_filter_set(mdev, VP_POLY4_C0_LL,
		filter_cr_horiz_tap4, sizeof filter_cr_horiz_tap4);
#endif
}

static void mxr_reg_mxr_dump(struct mxr_device *mdev)
{
#define DUMPREG(reg_id) \
do { \
	mxr_dbg(mdev, #reg_id " = %08x\n", \
		(u32)readl(mdev->res.mxr_regs + reg_id)); \
} while (0)

	DUMPREG(MXR_STATUS);
	DUMPREG(MXR_CFG);
	DUMPREG(MXR_INT_EN);
	DUMPREG(MXR_INT_STATUS);

	DUMPREG(MXR_LAYER_CFG);
	DUMPREG(MXR_VIDEO_CFG);

	DUMPREG(MXR_GRAPHIC0_CFG);
	DUMPREG(MXR_GRAPHIC0_BASE);
	DUMPREG(MXR_GRAPHIC0_SPAN);
	DUMPREG(MXR_GRAPHIC0_WH);
	DUMPREG(MXR_GRAPHIC0_SXY);
	DUMPREG(MXR_GRAPHIC0_DXY);

	DUMPREG(MXR_GRAPHIC1_CFG);
	DUMPREG(MXR_GRAPHIC1_BASE);
	DUMPREG(MXR_GRAPHIC1_SPAN);
	DUMPREG(MXR_GRAPHIC1_WH);
	DUMPREG(MXR_GRAPHIC1_SXY);
	DUMPREG(MXR_GRAPHIC1_DXY);

	if (soc_is_exynos5250()) {
		DUMPREG(MXR1_LAYER_CFG);
		DUMPREG(MXR1_VIDEO_CFG);

		DUMPREG(MXR1_GRAPHIC0_CFG);
		DUMPREG(MXR1_GRAPHIC0_BASE);
		DUMPREG(MXR1_GRAPHIC0_SPAN);
		DUMPREG(MXR1_GRAPHIC0_WH);
		DUMPREG(MXR1_GRAPHIC0_SXY);
		DUMPREG(MXR1_GRAPHIC0_DXY);

		DUMPREG(MXR1_GRAPHIC1_CFG);
		DUMPREG(MXR1_GRAPHIC1_BASE);
		DUMPREG(MXR1_GRAPHIC1_SPAN);
		DUMPREG(MXR1_GRAPHIC1_WH);
		DUMPREG(MXR1_GRAPHIC1_SXY);
		DUMPREG(MXR1_GRAPHIC1_DXY);

		DUMPREG(MXR_TVOUT_CFG);
	}
#undef DUMPREG
}

static void mxr_reg_vp_dump(struct mxr_device *mdev)
{
#define DUMPREG(reg_id) \
do { \
	mxr_dbg(mdev, #reg_id " = %08x\n", \
		(u32) readl(mdev->res.vp_regs + reg_id)); \
} while (0)

#if defined(CONFIG_ARCH_EXYNOS4)
	DUMPREG(VP_ENABLE);
	DUMPREG(VP_SRESET);
	DUMPREG(VP_SHADOW_UPDATE);
	DUMPREG(VP_FIELD_ID);
	DUMPREG(VP_MODE);
	DUMPREG(VP_IMG_SIZE_Y);
	DUMPREG(VP_IMG_SIZE_C);
	DUMPREG(VP_PER_RATE_CTRL);
	DUMPREG(VP_TOP_Y_PTR);
	DUMPREG(VP_BOT_Y_PTR);
	DUMPREG(VP_TOP_C_PTR);
	DUMPREG(VP_BOT_C_PTR);
	DUMPREG(VP_ENDIAN_MODE);
	DUMPREG(VP_SRC_H_POSITION);
	DUMPREG(VP_SRC_V_POSITION);
	DUMPREG(VP_SRC_WIDTH);
	DUMPREG(VP_SRC_HEIGHT);
	DUMPREG(VP_DST_H_POSITION);
	DUMPREG(VP_DST_V_POSITION);
	DUMPREG(VP_DST_WIDTH);
	DUMPREG(VP_DST_HEIGHT);
	DUMPREG(VP_H_RATIO);
	DUMPREG(VP_V_RATIO);
#endif

#undef DUMPREG
}

void mxr_reg_dump(struct mxr_device *mdev)
{
	mxr_reg_mxr_dump(mdev);
	mxr_reg_vp_dump(mdev);
}
