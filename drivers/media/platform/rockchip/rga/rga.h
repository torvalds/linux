/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */
#ifndef __RGA_H__
#define __RGA_H__

#include <linux/platform_device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define RGA_NAME "rockchip-rga"

struct rga_fmt {
	u32 fourcc;
	int depth;
	u8 uv_factor;
	u8 y_div;
	u8 x_div;
	u8 color_swap;
	u8 hw_format;
};

struct rga_frame {
	/* Original dimensions */
	u32 width;
	u32 height;
	u32 colorspace;

	/* Crop */
	struct v4l2_rect crop;

	/* Image format */
	struct rga_fmt *fmt;

	/* Variables that can calculated once and reused */
	u32 stride;
	u32 size;
};

struct rockchip_rga_version {
	u32 major;
	u32 minor;
};

struct rga_ctx {
	struct v4l2_fh fh;
	struct rockchip_rga *rga;
	struct rga_frame in;
	struct rga_frame out;
	struct v4l2_ctrl_handler ctrl_handler;

	/* Control values */
	u32 op;
	u32 hflip;
	u32 vflip;
	u32 rotate;
	u32 fill_color;
};

struct rockchip_rga {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device *vfd;

	struct device *dev;
	struct regmap *grf;
	void __iomem *regs;
	struct clk *sclk;
	struct clk *aclk;
	struct clk *hclk;
	struct rockchip_rga_version version;

	/* vfd lock */
	struct mutex mutex;
	/* ctrl parm lock */
	spinlock_t ctrl_lock;

	struct rga_ctx *curr;
	dma_addr_t cmdbuf_phy;
	void *cmdbuf_virt;
	unsigned int *src_mmu_pages;
	unsigned int *dst_mmu_pages;
};

struct rga_frame *rga_get_frame(struct rga_ctx *ctx, enum v4l2_buf_type type);

/* RGA Buffers Manage */
extern const struct vb2_ops rga_qops;
void rga_buf_map(struct vb2_buffer *vb);

/* RGA Hardware */
static inline void rga_write(struct rockchip_rga *rga, u32 reg, u32 value)
{
	writel(value, rga->regs + reg);
};

static inline u32 rga_read(struct rockchip_rga *rga, u32 reg)
{
	return readl(rga->regs + reg);
};

static inline void rga_mod(struct rockchip_rga *rga, u32 reg, u32 val, u32 mask)
{
	u32 temp = rga_read(rga, reg) & ~(mask);

	temp |= val & mask;
	rga_write(rga, reg, temp);
};

void rga_hw_start(struct rockchip_rga *rga);

#endif
