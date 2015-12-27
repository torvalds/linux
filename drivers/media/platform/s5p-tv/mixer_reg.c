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

#include <linux/delay.h>

/* Register access subroutines */

static inline u32 vp_read(struct mxr_device *mdev, u32 reg_id)
{
	return readl(mdev->res.vp_regs + reg_id);
}

static inline void vp_write(struct mxr_device *mdev, u32 reg_id, u32 val)
{
	writel(val, mdev->res.vp_regs + reg_id);
}

static inline void vp_write_mask(struct mxr_device *mdev, u32 reg_id,
	u32 val, u32 mask)
{
	u32 old = vp_read(mdev, reg_id);

	val = (val & mask) | (old & ~mask);
	writel(val, mdev->res.vp_regs + reg_id);
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

void mxr_vsync_set_update(struct mxr_device *mdev, int en)
{
	/* block update on vsync */
	mxr_write_mask(mdev, MXR_STATUS, en ? MXR_STATUS_SYNC_ENABLE : 0,
		MXR_STATUS_SYNC_ENABLE);
	vp_write(mdev, VP_SHADOW_UPDATE, en ? VP_SHADOW_UPDATE_ENABLE : 0);
}

static void __mxr_reg_vp_reset(struct mxr_device *mdev)
{
	int tries = 100;

	vp_write(mdev, VP_SRESET, VP_SRESET_PROCESSING);
	for (tries = 100; tries; --tries) {
		/* waiting until VP_SRESET_PROCESSING is 0 */
		if (~vp_read(mdev, VP_SRESET) & VP_SRESET_PROCESSING)
			break;
		mdelay(10);
	}
	WARN(tries == 0, "failed to reset Video Processor\n");
}

static void mxr_reg_vp_default_filter(struct mxr_device *mdev);

void mxr_reg_reset(struct mxr_device *mdev)
{
	unsigned long flags;
	u32 val; /* value stored to register */

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	/* set output in RGB888 mode */
	mxr_write(mdev, MXR_CFG, MXR_CFG_OUT_RGB888);

	/* 16 beat burst in DMA */
	mxr_write_mask(mdev, MXR_STATUS, MXR_STATUS_16_BURST,
		MXR_STATUS_BURST_MASK);

	/* setting default layer priority: layer1 > video > layer0
	 * because typical usage scenario would be
	 * layer0 - framebuffer
	 * video - video overlay
	 * layer1 - OSD
	 */
	val  = MXR_LAYER_CFG_GRP0_VAL(1);
	val |= MXR_LAYER_CFG_VP_VAL(2);
	val |= MXR_LAYER_CFG_GRP1_VAL(3);
	mxr_write(mdev, MXR_LAYER_CFG, val);

	/* use dark gray background color */
	mxr_write(mdev, MXR_BG_COLOR0, 0x808080);
	mxr_write(mdev, MXR_BG_COLOR1, 0x808080);
	mxr_write(mdev, MXR_BG_COLOR2, 0x808080);

	/* setting graphical layers */

	val  = MXR_GRP_CFG_COLOR_KEY_DISABLE; /* no blank key */
	val |= MXR_GRP_CFG_BLEND_PRE_MUL; /* premul mode */
	val |= MXR_GRP_CFG_ALPHA_VAL(0xff); /* non-transparent alpha */

	/* the same configuration for both layers */
	mxr_write(mdev, MXR_GRAPHIC_CFG(0), val);
	mxr_write(mdev, MXR_GRAPHIC_CFG(1), val);

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
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	/* setup format */
	mxr_write_mask(mdev, MXR_GRAPHIC_CFG(idx),
		MXR_GRP_CFG_FORMAT_VAL(fmt->cookie), MXR_GRP_CFG_FORMAT_MASK);

	/* setup geometry */
	mxr_write(mdev, MXR_GRAPHIC_SPAN(idx), geo->src.full_width);
	val  = MXR_GRP_WH_WIDTH(geo->src.width);
	val |= MXR_GRP_WH_HEIGHT(geo->src.height);
	val |= MXR_GRP_WH_H_SCALE(geo->x_ratio);
	val |= MXR_GRP_WH_V_SCALE(geo->y_ratio);
	mxr_write(mdev, MXR_GRAPHIC_WH(idx), val);

	/* setup offsets in source image */
	val  = MXR_GRP_SXY_SX(geo->src.x_offset);
	val |= MXR_GRP_SXY_SY(geo->src.y_offset);
	mxr_write(mdev, MXR_GRAPHIC_SXY(idx), val);

	/* setup offsets in display image */
	val  = MXR_GRP_DXY_DX(geo->dst.x_offset);
	val |= MXR_GRP_DXY_DY(geo->dst.y_offset);
	mxr_write(mdev, MXR_GRAPHIC_DXY(idx), val);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_vp_format(struct mxr_device *mdev,
	const struct mxr_format *fmt, const struct mxr_geometry *geo)
{
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

}

void mxr_reg_graph_buffer(struct mxr_device *mdev, int idx, dma_addr_t addr)
{
	u32 val = addr ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	if (idx == 0)
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_GRP0_ENABLE);
	else
		mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_GRP1_ENABLE);
	mxr_write(mdev, MXR_GRAPHIC_BASE(idx), addr);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

void mxr_reg_vp_buffer(struct mxr_device *mdev,
	dma_addr_t luma_addr[2], dma_addr_t chroma_addr[2])
{
	u32 val = luma_addr[0] ? ~0 : 0;
	unsigned long flags;

	spin_lock_irqsave(&mdev->reg_slock, flags);
	mxr_vsync_set_update(mdev, MXR_DISABLE);

	mxr_write_mask(mdev, MXR_CFG, val, MXR_CFG_VP_ENABLE);
	vp_write_mask(mdev, VP_ENABLE, val, VP_ENABLE_ON);
	/* TODO: fix tiled mode */
	vp_write(mdev, VP_TOP_Y_PTR, luma_addr[0]);
	vp_write(mdev, VP_TOP_C_PTR, chroma_addr[0]);
	vp_write(mdev, VP_BOT_Y_PTR, luma_addr[1]);
	vp_write(mdev, VP_BOT_C_PTR, chroma_addr[1]);

	mxr_vsync_set_update(mdev, MXR_ENABLE);
	spin_unlock_irqrestore(&mdev->reg_slock, flags);
}

static void mxr_irq_layer_handle(struct mxr_layer *layer)
{
	struct list_head *head = &layer->enq_list;
	struct mxr_buffer *done;

	/* skip non-existing layer */
	if (layer == NULL)
		return;

	spin_lock(&layer->enq_slock);
	if (layer->state == MXR_LAYER_IDLE)
		goto done;

	done = layer->shadow_buf;
	layer->shadow_buf = layer->update_buf;

	if (list_empty(head)) {
		if (layer->state != MXR_LAYER_STREAMING)
			layer->update_buf = NULL;
	} else {
		struct mxr_buffer *next;
		next = list_first_entry(head, struct mxr_buffer, list);
		list_del(&next->list);
		layer->update_buf = next;
	}

	layer->ops.buffer_set(layer, layer->update_buf);

	if (done && done != layer->shadow_buf)
		vb2_buffer_done(&done->vb.vb2_buf, VB2_BUF_STATE_DONE);

done:
	spin_unlock(&layer->enq_slock);
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
		/* toggle TOP field event if working in interlaced mode */
		if (~mxr_read(mdev, MXR_CFG) & MXR_CFG_SCAN_PROGRASSIVE)
			change_bit(MXR_EVENT_TOP, &mdev->event_flags);
		wake_up(&mdev->event_queue);
		/* vsync interrupt use different bit for read and clear */
		val &= ~MXR_INT_STATUS_VSYNC;
		val |= MXR_INT_CLEAR_VSYNC;
	}

	/* clear interrupts */
	mxr_write(mdev, MXR_INT_STATUS, val);

	spin_unlock(&mdev->reg_slock);
	/* leave on non-vsync event */
	if (~val & MXR_INT_CLEAR_VSYNC)
		return IRQ_HANDLED;
	/* skip layer update on bottom field */
	if (!test_bit(MXR_EVENT_TOP, &mdev->event_flags))
		return IRQ_HANDLED;
	for (i = 0; i < MXR_MAX_LAYERS; ++i)
		mxr_irq_layer_handle(mdev->layer[i]);
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
	set_bit(MXR_EVENT_TOP, &mdev->event_flags);

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
	long time_left;

	clear_bit(MXR_EVENT_VSYNC, &mdev->event_flags);
	/* TODO: consider adding interruptible */
	time_left = wait_event_timeout(mdev->event_queue,
			test_bit(MXR_EVENT_VSYNC, &mdev->event_flags),
				 msecs_to_jiffies(1000));
	if (time_left > 0)
		return 0;
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

	/* selecting colorspace accepted by output */
	if (fmt->colorspace == V4L2_COLORSPACE_JPEG)
		val |= MXR_CFG_OUT_YUV444;
	else
		val |= MXR_CFG_OUT_RGB888;

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

void mxr_reg_graph_layer_stream(struct mxr_device *mdev, int idx, int en)
{
	/* no extra actions need to be done */
}

void mxr_reg_vp_layer_stream(struct mxr_device *mdev, int en)
{
	/* no extra actions need to be done */
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
	mxr_reg_vp_filter_set(mdev, VP_POLY8_Y0_LL,
		filter_y_horiz_tap8, sizeof(filter_y_horiz_tap8));
	mxr_reg_vp_filter_set(mdev, VP_POLY4_Y0_LL,
		filter_y_vert_tap4, sizeof(filter_y_vert_tap4));
	mxr_reg_vp_filter_set(mdev, VP_POLY4_C0_LL,
		filter_cr_horiz_tap4, sizeof(filter_cr_horiz_tap4));
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
#undef DUMPREG
}

static void mxr_reg_vp_dump(struct mxr_device *mdev)
{
#define DUMPREG(reg_id) \
do { \
	mxr_dbg(mdev, #reg_id " = %08x\n", \
		(u32) readl(mdev->res.vp_regs + reg_id)); \
} while (0)


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

#undef DUMPREG
}

void mxr_reg_dump(struct mxr_device *mdev)
{
	mxr_reg_mxr_dump(mdev);
	mxr_reg_vp_dump(mdev);
}

