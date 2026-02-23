/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Video Decoder driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on rkvdec driver by Google LLC. (Tomasz Figa <tfiga@chromium.org>)
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */
#ifndef RKVDEC_H_
#define RKVDEC_H_

#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/wait.h>
#include <linux/clk.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#define RKVDEC_QUIRK_DISABLE_QOS	BIT(0)

#define RKVDEC_1080P_PIXELS		(1920 * 1088)
#define RKVDEC_4K_PIXELS		(4096 * 2304)
#define RKVDEC_8K_PIXELS		(7680 * 4320)

struct rkvdec_ctx;
struct rkvdec_rcb_config;

struct rkvdec_ctrl_desc {
	struct v4l2_ctrl_config cfg;
};

struct rkvdec_ctrls {
	const struct rkvdec_ctrl_desc *ctrls;
	unsigned int num_ctrls;
};

struct rkvdec_run {
	struct {
		struct vb2_v4l2_buffer *src;
		struct vb2_v4l2_buffer *dst;
	} bufs;
};

struct rkvdec_vp9_decoded_buffer_info {
	/* Info needed when the decoded frame serves as a reference frame. */
	unsigned short width;
	unsigned short height;
	unsigned int bit_depth : 4;
};

struct rkvdec_decoded_buffer {
	/* Must be the first field in this struct. */
	struct v4l2_m2m_buffer base;

	union {
		struct rkvdec_vp9_decoded_buffer_info vp9;
	};
};

static inline struct rkvdec_decoded_buffer *
vb2_to_rkvdec_decoded_buf(struct vb2_buffer *buf)
{
	return container_of(buf, struct rkvdec_decoded_buffer,
			    base.vb.vb2_buf);
}

struct rkvdec_variant_ops {
	irqreturn_t (*irq_handler)(struct rkvdec_ctx *ctx);
	u32 (*colmv_size)(u16 width, u16 height);
	void (*flatten_matrices)(u8 *output, const u8 *input, int matrices, int row_length);
};

struct rkvdec_variant {
	unsigned int num_regs;
	const struct rkvdec_coded_fmt_desc *coded_fmts;
	size_t num_coded_fmts;
	const struct rcb_size_info *rcb_sizes;
	size_t num_rcb_sizes;
	const struct rkvdec_variant_ops *ops;
	bool has_single_reg_region;
	unsigned int quirks;
};

struct rkvdec_coded_fmt_ops {
	int (*adjust_fmt)(struct rkvdec_ctx *ctx,
			  struct v4l2_format *f);
	int (*start)(struct rkvdec_ctx *ctx);
	void (*stop)(struct rkvdec_ctx *ctx);
	int (*run)(struct rkvdec_ctx *ctx);
	void (*done)(struct rkvdec_ctx *ctx, struct vb2_v4l2_buffer *src_buf,
		     struct vb2_v4l2_buffer *dst_buf,
		     enum vb2_buffer_state result);
	int (*try_ctrl)(struct rkvdec_ctx *ctx, struct v4l2_ctrl *ctrl);
	enum rkvdec_image_fmt (*get_image_fmt)(struct rkvdec_ctx *ctx,
					       struct v4l2_ctrl *ctrl);
};

enum rkvdec_image_fmt {
	RKVDEC_IMG_FMT_ANY = 0,
	RKVDEC_IMG_FMT_420_8BIT,
	RKVDEC_IMG_FMT_420_10BIT,
	RKVDEC_IMG_FMT_422_8BIT,
	RKVDEC_IMG_FMT_422_10BIT,
};

struct rkvdec_decoded_fmt_desc {
	u32 fourcc;
	enum rkvdec_image_fmt image_fmt;
};

struct rkvdec_coded_fmt_desc {
	u32 fourcc;
	struct v4l2_frmsize_stepwise frmsize;
	const struct rkvdec_ctrls *ctrls;
	const struct rkvdec_coded_fmt_ops *ops;
	unsigned int num_decoded_fmts;
	const struct rkvdec_decoded_fmt_desc *decoded_fmts;
	u32 subsystem_flags;
};

struct rkvdec_dev {
	struct v4l2_device v4l2_dev;
	struct media_device mdev;
	struct video_device vdev;
	struct v4l2_m2m_dev *m2m_dev;
	struct device *dev;
	struct clk_bulk_data *clocks;
	unsigned int num_clocks;
	struct clk *axi_clk;
	void __iomem *regs;
	void __iomem *link;
	struct mutex vdev_lock; /* serializes ioctls */
	struct delayed_work watchdog_work;
	struct gen_pool *sram_pool;
	struct iommu_domain *iommu_domain;
	struct iommu_domain *empty_domain;
	const struct rkvdec_variant *variant;
};

struct rkvdec_ctx {
	struct v4l2_fh fh;
	struct v4l2_format coded_fmt;
	struct v4l2_format decoded_fmt;
	const struct rkvdec_coded_fmt_desc *coded_fmt_desc;
	struct v4l2_ctrl_handler ctrl_hdl;
	struct rkvdec_dev *dev;
	enum rkvdec_image_fmt image_fmt;
	struct rkvdec_rcb_config *rcb_config;
	u32 colmv_offset;
	void *priv;
	u8 has_sps_st_rps: 1;
	u8 has_sps_lt_rps: 1;
};

static inline struct rkvdec_ctx *file_to_rkvdec_ctx(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct rkvdec_ctx, fh);
}

enum rkvdec_alloc_type {
	RKVDEC_ALLOC_DMA  = 0,
	RKVDEC_ALLOC_SRAM = 1,
};

struct rkvdec_aux_buf {
	void *cpu;
	dma_addr_t dma;
	size_t size;
	enum rkvdec_alloc_type type;
};

void rkvdec_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run);
void rkvdec_run_postamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run);
void rkvdec_memcpy_toio(void __iomem *dst, void *src, size_t len);
void rkvdec_schedule_watchdog(struct rkvdec_dev *rkvdec, u32 timeout_threshold);

void rkvdec_quirks_disable_qos(struct rkvdec_ctx *ctx);

/* RKVDEC ops */
extern const struct rkvdec_coded_fmt_ops rkvdec_h264_fmt_ops;
extern const struct rkvdec_coded_fmt_ops rkvdec_hevc_fmt_ops;
extern const struct rkvdec_coded_fmt_ops rkvdec_vp9_fmt_ops;

/* VDPU381 ops */
extern const struct rkvdec_coded_fmt_ops rkvdec_vdpu381_h264_fmt_ops;
extern const struct rkvdec_coded_fmt_ops rkvdec_vdpu381_hevc_fmt_ops;

/* VDPU383 ops */
extern const struct rkvdec_coded_fmt_ops rkvdec_vdpu383_h264_fmt_ops;
extern const struct rkvdec_coded_fmt_ops rkvdec_vdpu383_hevc_fmt_ops;

#endif /* RKVDEC_H_ */
