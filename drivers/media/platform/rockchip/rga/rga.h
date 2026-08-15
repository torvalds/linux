/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */
#ifndef __RGA_H__
#define __RGA_H__

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>

#define RGA_NAME "rockchip-rga"

#define DEFAULT_WIDTH 100
#define DEFAULT_HEIGHT 100

struct rga_frame {
	/* Crop */
	struct v4l2_rect crop;

	/* Image format */
	void *fmt;
	struct v4l2_pix_format_mplane pix;
};

struct rga_dma_desc {
	u32 addr;
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

	void *cmdbuf_virt;
	dma_addr_t cmdbuf_phy;
	bool cmdbuf_dirty;

	int osequence;
	int csequence;

	/* Control values */
	u32 op;
	u32 hflip;
	u32 vflip;
	u32 rotate;
	u32 fill_color;
};

static inline struct rga_ctx *file_to_rga_ctx(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct rga_ctx, fh);
}

struct rga_hw;

struct rockchip_rga {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device *vfd;

	struct device *dev;
	struct regmap *grf;
	void __iomem *regs;
	struct clk_bulk_data *clks;
	int num_clks;
	struct rockchip_rga_version version;

	/* vfd lock */
	struct mutex mutex;
	/* ctrl parm lock */
	spinlock_t ctrl_lock;

	struct rga_ctx *curr;

	const struct rga_hw *hw;
};

struct rga_addrs {
	dma_addr_t y_addr;
	dma_addr_t u_addr;
	dma_addr_t v_addr;
};

struct rga_vb_buffer {
	struct vb2_v4l2_buffer vb_buf;
	struct list_head queue;

	/* RGA MMU mapping for this buffer */
	struct rga_dma_desc *dma_desc;
	dma_addr_t dma_desc_pa;
	size_t n_desc;

	/* Plane DMA addresses after the MMU mapping of the buffer */
	struct rga_addrs dma_addrs;
};

static inline struct rga_vb_buffer *vb_to_rga(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rga_vb_buffer, vb_buf);
}

struct rga_frame *rga_get_frame(struct rga_ctx *ctx, enum v4l2_buf_type type);

int rga_check_scaling(const struct rga_hw *hw, const struct v4l2_rect *crop_in,
		      const struct v4l2_rect *crop_out, u32 rotate);

/* RGA Buffers Manage */
extern const struct vb2_ops rga_qops;

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

#define RGA_FEATURE_FLIP	BIT(0)
#define RGA_FEATURE_ROTATE	BIT(1)
#define RGA_FEATURE_BG_COLOR	BIT(2)

struct rga_hw {
	const char *card_type;
	bool has_internal_iommu;
	size_t cmdbuf_size;
	u32 min_width, min_height;
	u32 max_width, max_height;
	u8 max_scaling_factor;
	u8 stride_alignment;
	u8 features;

	void (*setup_cmdbuf)(struct rga_ctx *ctx);
	void (*start)(struct rockchip_rga *rga,
		      struct rga_vb_buffer *src, struct rga_vb_buffer *dst);
	bool (*handle_irq)(struct rockchip_rga *rga);
	void (*get_version)(struct rockchip_rga *rga);
	void *(*adjust_and_map_format)(struct rga_ctx *ctx,
				       struct v4l2_pix_format_mplane *format,
				       bool is_output);
	int (*enum_format)(struct v4l2_fmtdesc *f);
};

static inline bool rga_has_internal_iommu(const struct rockchip_rga *rga)
{
	return rga->hw->has_internal_iommu;
}

extern const struct rga_hw rga2_hw;
extern const struct rga_hw rga3_hw;

#endif
