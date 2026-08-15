// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm MSM Camera Subsystem - VFE (Video Front End) Module 340 (TFE)
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "camss.h"
#include "camss-vfe.h"

#define TFE_GLOBAL_RESET_CMD				(0x014)
#define		TFE_GLOBAL_RESET_CMD_CORE	BIT(0)

#define TFE_REG_UPDATE_CMD				(0x02c)

#define TFE_IRQ_CMD					(0x030)
#define		TFE_IRQ_CMD_CLEAR		BIT(0)
#define TFE_IRQ_MASK_0					(0x034)
#define		TFE_IRQ_MASK_0_RST_DONE		BIT(0)
#define		TFE_IRQ_MASK_0_BUS_WR		BIT(1)
#define TFE_IRQ_MASK_1					(0x038)
#define TFE_IRQ_MASK_2					(0x03c)
#define TFE_IRQ_CLEAR_0					(0x040)

#define TFE_IRQ_STATUS_0				(0x04c)

#define BUS_REG(a)					(0xa00 + (a))

#define TFE_BUS_IRQ_MASK_0				BUS_REG(0x18)
#define		TFE_BUS_IRQ_MASK_RUP_DONE_MASK	GENMASK(3, 0)
#define		TFE_BUS_IRQ_MASK_RUP_DONE(sc)	FIELD_PREP(TFE_BUS_IRQ_MASK_RUP_DONE_MASK, BIT(sc))
#define		TFE_BUS_IRQ_MASK_BUF_DONE_MASK	GENMASK(15, 8)
#define		TFE_BUS_IRQ_MASK_BUF_DONE(sg)	FIELD_PREP(TFE_BUS_IRQ_MASK_BUF_DONE_MASK, BIT(sg))
#define		TFE_BUS_IRQ_MASK_0_CONS_VIOL	BIT(28)
#define		TFE_BUS_IRQ_MASK_0_VIOL		BIT(30)
#define		TFE_BUS_IRQ_MASK_0_IMG_VIOL	BIT(31)

#define TFE_BUS_IRQ_MASK_1				BUS_REG(0x1c)
#define TFE_BUS_IRQ_CLEAR_0				BUS_REG(0x20)
#define TFE_BUS_IRQ_STATUS_0				BUS_REG(0x28)
#define TFE_BUS_IRQ_CMD					BUS_REG(0x30)
#define		TFE_BUS_IRQ_CMD_CLEAR		BIT(0)

#define TFE_BUS_STATUS_CLEAR				BUS_REG(0x60)
#define TFE_BUS_VIOLATION_STATUS			BUS_REG(0x64)
#define TFE_BUS_OVERFLOW_STATUS				BUS_REG(0x68)
#define TFE_BUS_IMAGE_SZ_VIOLATION_STATUS		BUS_REG(0x70)

#define TFE_BUS_CLIENT_CFG(c)				BUS_REG(0x200 + (c) * 0x100)
#define		TFE_BUS_CLIENT_CFG_EN		BIT(0)
#define		TFE_BUS_CLIENT_CFG_AUTORECOVER	BIT(4)
#define		TFE_BUS_CLIENT_CFG_MODE_FRAME	BIT(16)
#define TFE_BUS_IMAGE_ADDR(c)				BUS_REG(0x204 + (c) * 0x100)
#define TFE_BUS_FRAME_INCR(c)				BUS_REG(0x208 + (c) * 0x100)
#define TFE_BUS_IMAGE_CFG_0(c)				BUS_REG(0x20c + (c) * 0x100)
#define		TFE_BUS_IMAGE_CFG_0_DEFAULT	0xffff
#define TFE_BUS_IMAGE_CFG_1(c)				BUS_REG(0x210 + (c) * 0x100)
#define TFE_BUS_IMAGE_CFG_2(c)				BUS_REG(0x214 + (c) * 0x100)
#define		TFE_BUS_IMAGE_CFG_2_DEFAULT	0xffff
#define TFE_BUS_PACKER_CFG(c)				BUS_REG(0x218 + (c) * 0x100)
#define		TFE_BUS_PACKER_CFG_FMT_PLAIN8	0x1
#define		TFE_BUS_PACKER_CFG_FMT_PLAIN64	0xa
#define		TFE_BUS_PACKER_CFG_FMT_MIPI10	0xc
#define		TFE_BUS_PACKER_CFG_FMT_MIPI12	0xd
#define TFE_BUS_IRQ_SUBSAMPLE_CFG_0(c)			BUS_REG(0x230 + (c) * 0x100)
#define TFE_BUS_IRQ_SUBSAMPLE_CFG_1(c)			BUS_REG(0x234 + (c) * 0x100)
#define TFE_BUS_FRAMEDROP_CFG_0(c)			BUS_REG(0x238 + (c) * 0x100)
#define TFE_BUS_FRAMEDROP_CFG_1(c)			BUS_REG(0x23c + (c) * 0x100)

#define PP_CROP_REG(a)					(0x2800 + (a))
#define TFE_PP_CROP_CFG					PP_CROP_REG(0x60)
#define		TFE_PP_CROP_CFG_EN	(BIT(0) | BIT(9))
#define	TFE_PP_CROP_LINE_CFG				PP_CROP_REG(0x68)
#define		TFE_PP_CROP_FIRST	GENMASK(29, 16)
#define		TFE_PP_CROP_LAST	GENMASK(13, 0)
#define	TFE_PP_CROP_PIX_CFG				PP_CROP_REG(0x6C)

enum tfe_client {
	TFE_CLI_BAYER,
	TFE_CLI_IDEAL_RAW,
	TFE_CLI_STATS_TINTLESS_BG,
	TFE_CLI_STATS_BHIST,
	TFE_CLI_STATS_AWB_BG,
	TFE_CLI_STATS_AEC_BG,
	TFE_CLI_STATS_BAF,
	TFE_CLI_RDI0,
	TFE_CLI_RDI1,
	TFE_CLI_RDI2,
	TFE_CLI_NUM
};

enum tfe_iface {
	TFE_IFACE_PIX,
	TFE_IFACE_RDI0,
	TFE_IFACE_RDI1,
	TFE_IFACE_RDI2,
	TFE_IFACE_NUM
};

enum tfe_subgroups {
	TFE_SUBGROUP_BAYER,
	TFE_SUBGROUP_IDEAL_RAW,
	TFE_SUBGROUP_HDR,
	TFE_SUBGROUP_BG,
	TFE_SUBGROUP_BAF,
	TFE_SUBGROUP_RDI0,
	TFE_SUBGROUP_RDI1,
	TFE_SUBGROUP_RDI2,
	TFE_SUBGROUP_NUM
};

static enum tfe_client tfe_wm_client_map[VFE_LINE_NUM_MAX] = {
	[VFE_LINE_RDI0] = TFE_CLI_RDI0,
	[VFE_LINE_RDI1] = TFE_CLI_RDI1,
	[VFE_LINE_RDI2] = TFE_CLI_RDI2,
	[VFE_LINE_PIX] = TFE_CLI_BAYER,
};

static enum tfe_iface tfe_line_iface_map[VFE_LINE_NUM_MAX] = {
	[VFE_LINE_RDI0] = TFE_IFACE_RDI0,
	[VFE_LINE_RDI1] = TFE_IFACE_RDI1,
	[VFE_LINE_RDI2] = TFE_IFACE_RDI2,
	[VFE_LINE_PIX] = TFE_IFACE_PIX,
};

static enum vfe_line_id tfe_subgroup_line_map[TFE_SUBGROUP_NUM] = {
	[TFE_SUBGROUP_BAYER] = VFE_LINE_PIX,
	[TFE_SUBGROUP_IDEAL_RAW] = VFE_LINE_PIX,
	[TFE_SUBGROUP_HDR] = VFE_LINE_PIX,
	[TFE_SUBGROUP_BG] = VFE_LINE_PIX,
	[TFE_SUBGROUP_BAF] = VFE_LINE_PIX,
	[TFE_SUBGROUP_RDI0] = VFE_LINE_RDI0,
	[TFE_SUBGROUP_RDI1] = VFE_LINE_RDI1,
	[TFE_SUBGROUP_RDI2] = VFE_LINE_RDI2,
};

static inline enum tfe_iface  __line_to_iface(enum vfe_line_id line_id)
{
	if (line_id <= VFE_LINE_NONE || line_id >= VFE_LINE_NUM_MAX) {
		pr_warn("VFE: Invalid line %d\n", line_id);
		return TFE_IFACE_RDI0;
	}

	return tfe_line_iface_map[line_id];
}

static inline enum vfe_line_id __iface_to_line(unsigned int iface)
{
	int i;

	for (i = 0; i < VFE_LINE_NUM_MAX; i++) {
		if (tfe_line_iface_map[i] == iface)
			return i;
	}

	return VFE_LINE_NONE;
}

static inline enum vfe_line_id __subgroup_to_line(enum tfe_subgroups sg)
{
	if (sg >= TFE_SUBGROUP_NUM)
		return VFE_LINE_NONE;

	return tfe_subgroup_line_map[sg];
}

static void vfe_global_reset(struct vfe_device *vfe)
{
	writel(TFE_IRQ_MASK_0_RST_DONE, vfe->base + TFE_IRQ_MASK_0);
	writel(TFE_GLOBAL_RESET_CMD_CORE, vfe->base + TFE_GLOBAL_RESET_CMD);
}

static irqreturn_t vfe_isr(int irq, void *dev)
{
	struct vfe_device *vfe = dev;
	u32 status;
	int i;

	status = readl_relaxed(vfe->base + TFE_IRQ_STATUS_0);
	writel_relaxed(status, vfe->base + TFE_IRQ_CLEAR_0);
	writel_relaxed(TFE_IRQ_CMD_CLEAR, vfe->base + TFE_IRQ_CMD);

	if (status & TFE_IRQ_MASK_0_RST_DONE) {
		dev_dbg(vfe->camss->dev, "VFE%u: Reset done!", vfe->id);
		vfe_isr_reset_ack(vfe);
	}

	if (status & TFE_IRQ_MASK_0_BUS_WR) {
		u32 bus_status = readl_relaxed(vfe->base + TFE_BUS_IRQ_STATUS_0);

		writel_relaxed(bus_status, vfe->base + TFE_BUS_IRQ_CLEAR_0);
		writel_relaxed(TFE_BUS_IRQ_CMD_CLEAR, vfe->base + TFE_BUS_IRQ_CMD);

		for (i = 0; i < TFE_IFACE_NUM; i++) {
			if (bus_status & TFE_BUS_IRQ_MASK_RUP_DONE(i))
				vfe->res->hw_ops->reg_update_clear(vfe, __iface_to_line(i));
		}

		for (i = 0; i < TFE_SUBGROUP_NUM; i++) {
			if (bus_status & TFE_BUS_IRQ_MASK_BUF_DONE(i))
				vfe_buf_done(vfe, __subgroup_to_line(i));
		}

		if (bus_status & TFE_BUS_IRQ_MASK_0_CONS_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Bad config violation",
					    vfe->id);

		if (bus_status & TFE_BUS_IRQ_MASK_0_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Input data violation",
					    vfe->id);

		if (bus_status & TFE_BUS_IRQ_MASK_0_IMG_VIOL)
			dev_err_ratelimited(vfe->camss->dev, "VFE%u: Image size violation",
					    vfe->id);
	}

	status = readl_relaxed(vfe->base + TFE_BUS_OVERFLOW_STATUS);
	if (status) {
		writel_relaxed(status, vfe->base + TFE_BUS_STATUS_CLEAR);
		for (i = 0; i < TFE_CLI_NUM; i++) {
			if (status & BIT(i))
				dev_err_ratelimited(vfe->camss->dev,
						    "VFE%u: bus overflow for client %u\n",
						    vfe->id, i);
		}
	}

	return IRQ_HANDLED;
}

static int vfe_halt(struct vfe_device *vfe)
{
	/* rely on vfe_disable_output() to stop the VFE */
	return 0;
}

static void vfe_enable_irq(struct vfe_device *vfe)
{
	writel(TFE_IRQ_MASK_0_RST_DONE | TFE_IRQ_MASK_0_BUS_WR,
	       vfe->base + TFE_IRQ_MASK_0);
	writel(TFE_BUS_IRQ_MASK_RUP_DONE_MASK | TFE_BUS_IRQ_MASK_BUF_DONE_MASK |
	       TFE_BUS_IRQ_MASK_0_CONS_VIOL | TFE_BUS_IRQ_MASK_0_VIOL |
	       TFE_BUS_IRQ_MASK_0_IMG_VIOL, vfe->base + TFE_BUS_IRQ_MASK_0);
}

static void vfe_wm_update(struct vfe_device *vfe, u8 wm, u32 addr,
			  struct vfe_line *line)
{
	u8 client = tfe_wm_client_map[wm];

	writel_relaxed(addr, vfe->base + TFE_BUS_IMAGE_ADDR(client));
}

static u32 vfe_packer_format(struct vfe_device *vfe, u32 pixelformat)
{
	const struct camss_formats *fmt = vfe->res->formats_rdi;
	unsigned int bpp = 0;
	int i;

	for (i = 0; i < fmt->nformats; i++) {
		if (fmt->formats[i].pixelformat == pixelformat) {
			bpp = fmt->formats[i].mbus_bpp;
			break;
		}
	}

	switch (bpp) {
	case 10:
		return TFE_BUS_PACKER_CFG_FMT_MIPI10;
	case 12:
		return TFE_BUS_PACKER_CFG_FMT_MIPI12;
	default:
		return TFE_BUS_PACKER_CFG_FMT_PLAIN8;
	}
}

static void vfe_wm_start(struct vfe_device *vfe, u8 wm, struct vfe_line *line)
{
	struct v4l2_pix_format_mplane *pix = &line->video_out.active_fmt.fmt.pix_mp;
	u32 stride = pix->plane_fmt[0].bytesperline;
	u8 client = tfe_wm_client_map[wm];
	u32 cfg = TFE_BUS_CLIENT_CFG_EN;

	if (client == TFE_CLI_BAYER) { /* PIX - Line based */
		struct v4l2_rect *crop = &line->crop;

		/* Cropping */
		writel_relaxed(TFE_PP_CROP_CFG_EN, vfe->base + TFE_PP_CROP_CFG);
		writel_relaxed(FIELD_PREP(TFE_PP_CROP_FIRST, crop->top) |
			       FIELD_PREP(TFE_PP_CROP_LAST, crop->top + crop->height - 1),
			       vfe->base + TFE_PP_CROP_LINE_CFG);
		writel_relaxed(FIELD_PREP(TFE_PP_CROP_FIRST, crop->left) |
			       FIELD_PREP(TFE_PP_CROP_LAST, crop->left + crop->width - 1),
			       vfe->base + TFE_PP_CROP_PIX_CFG);

		/* Write Engine */
		writel_relaxed(pix->width | (pix->height << 16),
			       vfe->base + TFE_BUS_IMAGE_CFG_0(client));
		writel_relaxed(0u, vfe->base + TFE_BUS_IMAGE_CFG_1(client));
		writel_relaxed(stride, vfe->base + TFE_BUS_IMAGE_CFG_2(client));
		writel_relaxed(stride * pix->height, vfe->base + TFE_BUS_FRAME_INCR(client));
		writel_relaxed(vfe_packer_format(vfe, pix->pixelformat),
			       vfe->base + TFE_BUS_PACKER_CFG(client));

		cfg |= TFE_BUS_CLIENT_CFG_AUTORECOVER;
	} else { /* RDI - Frame based */
		writel_relaxed(TFE_BUS_IMAGE_CFG_0_DEFAULT,
			       vfe->base + TFE_BUS_IMAGE_CFG_0(client));
		writel_relaxed(0u, vfe->base + TFE_BUS_IMAGE_CFG_1(client));
		writel_relaxed(TFE_BUS_IMAGE_CFG_2_DEFAULT,
			       vfe->base + TFE_BUS_IMAGE_CFG_2(client));
		writel_relaxed(stride * pix->height, vfe->base + TFE_BUS_FRAME_INCR(client));
		writel_relaxed(TFE_BUS_PACKER_CFG_FMT_PLAIN64,
			       vfe->base + TFE_BUS_PACKER_CFG(client));
		cfg |= TFE_BUS_CLIENT_CFG_MODE_FRAME;
	}

	/* No dropped frames, one irq per frame */
	writel_relaxed(0, vfe->base + TFE_BUS_FRAMEDROP_CFG_0(client));
	writel_relaxed(1, vfe->base + TFE_BUS_FRAMEDROP_CFG_1(client));
	writel_relaxed(0, vfe->base + TFE_BUS_IRQ_SUBSAMPLE_CFG_0(client));
	writel_relaxed(1, vfe->base + TFE_BUS_IRQ_SUBSAMPLE_CFG_1(client));

	vfe_enable_irq(vfe);

	writel(cfg, vfe->base + TFE_BUS_CLIENT_CFG(client));

	dev_dbg(vfe->camss->dev, "VFE%u: Started client %u width %u height %u stride %u\n",
		vfe->id, client, pix->width, pix->height, stride);
}

static void vfe_wm_stop(struct vfe_device *vfe, u8 wm)
{
	u8 client = tfe_wm_client_map[wm];

	writel(0, vfe->base + TFE_BUS_CLIENT_CFG(client));

	dev_dbg(vfe->camss->dev, "VFE%u: Stopped client %u\n", vfe->id, client);
}

static const struct camss_video_ops vfe_video_ops_520 = {
	.queue_buffer = vfe_queue_buffer_v2,
	.flush_buffers = vfe_flush_buffers,
};

static void vfe_subdev_init(struct device *dev, struct vfe_device *vfe)
{
	vfe->video_ops = vfe_video_ops_520;
}

static void vfe_reg_update(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	vfe->reg_update |= BIT(__line_to_iface(line_id));
	writel_relaxed(vfe->reg_update, vfe->base + TFE_REG_UPDATE_CMD);
}

static void vfe_reg_update_clear(struct vfe_device *vfe, enum vfe_line_id line_id)
{
	vfe->reg_update &= ~BIT(__line_to_iface(line_id));
}

const struct vfe_hw_ops vfe_ops_340 = {
	.global_reset = vfe_global_reset,
	.hw_version = vfe_hw_version,
	.isr = vfe_isr,
	.pm_domain_off = vfe_pm_domain_off,
	.pm_domain_on = vfe_pm_domain_on,
	.subdev_init = vfe_subdev_init,
	.vfe_disable = vfe_disable,
	.vfe_enable = vfe_enable_v2,
	.vfe_halt = vfe_halt,
	.vfe_wm_start = vfe_wm_start,
	.vfe_wm_stop = vfe_wm_stop,
	.vfe_buf_done = vfe_buf_done,
	.vfe_wm_update = vfe_wm_update,
	.reg_update = vfe_reg_update,
	.reg_update_clear = vfe_reg_update_clear,
};
