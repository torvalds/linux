/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 NextThing Co
 * Copyright (C) 2016-2019 Bootlin
 *
 * Author: Maxime Ripard <maxime.ripard@bootlin.com>
 */

#ifndef _SUN4I_CSI_H_
#define _SUN4I_CSI_H_

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/videobuf2-core.h>

#define CSI_EN_REG			0x00

#define CSI_CFG_REG			0x04
#define CSI_CFG_INPUT_FMT(fmt)			((fmt) << 20)
#define CSI_CFG_OUTPUT_FMT(fmt)			((fmt) << 16)
#define CSI_CFG_YUV_DATA_SEQ(seq)		((seq) << 8)
#define CSI_CFG_VREF_POL(pol)			((pol) << 2)
#define CSI_CFG_HREF_POL(pol)			((pol) << 1)
#define CSI_CFG_PCLK_POL(pol)			((pol) << 0)

#define CSI_CPT_CTRL_REG		0x08
#define CSI_CPT_CTRL_VIDEO_START		BIT(1)
#define CSI_CPT_CTRL_IMAGE_START		BIT(0)

#define CSI_BUF_ADDR_REG(fifo, buf)	(0x10 + (0x8 * (fifo)) + (0x4 * (buf)))

#define CSI_BUF_CTRL_REG		0x28
#define CSI_BUF_CTRL_DBN			BIT(2)
#define CSI_BUF_CTRL_DBS			BIT(1)
#define CSI_BUF_CTRL_DBE			BIT(0)

#define CSI_INT_EN_REG			0x30
#define CSI_INT_FRM_DONE			BIT(1)
#define CSI_INT_CPT_DONE			BIT(0)

#define CSI_INT_STA_REG			0x34

#define CSI_WIN_CTRL_W_REG		0x40
#define CSI_WIN_CTRL_W_ACTIVE(w)		((w) << 16)

#define CSI_WIN_CTRL_H_REG		0x44
#define CSI_WIN_CTRL_H_ACTIVE(h)		((h) << 16)

#define CSI_BUF_LEN_REG			0x48

#define CSI_MAX_BUFFER		2
#define CSI_MAX_HEIGHT		8192U
#define CSI_MAX_WIDTH		8192U

enum csi_input {
	CSI_INPUT_RAW	= 0,
	CSI_INPUT_BT656	= 2,
	CSI_INPUT_YUV	= 3,
};

enum csi_output_raw {
	CSI_OUTPUT_RAW_PASSTHROUGH = 0,
};

enum csi_output_yuv {
	CSI_OUTPUT_YUV_422_PLANAR	= 0,
	CSI_OUTPUT_YUV_420_PLANAR	= 1,
	CSI_OUTPUT_YUV_422_UV		= 4,
	CSI_OUTPUT_YUV_420_UV		= 5,
	CSI_OUTPUT_YUV_422_MACRO	= 8,
	CSI_OUTPUT_YUV_420_MACRO	= 9,
};

enum csi_yuv_data_seq {
	CSI_YUV_DATA_SEQ_YUYV	= 0,
	CSI_YUV_DATA_SEQ_YVYU	= 1,
	CSI_YUV_DATA_SEQ_UYVY	= 2,
	CSI_YUV_DATA_SEQ_VYUY	= 3,
};

enum csi_subdev_pads {
	CSI_SUBDEV_SINK,
	CSI_SUBDEV_SOURCE,

	CSI_SUBDEV_PADS,
};

extern const struct v4l2_subdev_ops sun4i_csi_subdev_ops;

struct sun4i_csi_format {
	u32			mbus;
	u32			fourcc;
	enum csi_input		input;
	u32			output;
	unsigned int		num_planes;
	u8			bpp[3];
	unsigned int		hsub;
	unsigned int		vsub;
};

const struct sun4i_csi_format *sun4i_csi_find_format(const u32 *fourcc,
						     const u32 *mbus);

struct sun4i_csi {
	/* Device resources */
	struct device			*dev;

	void __iomem			*regs;
	struct clk			*bus_clk;
	struct clk			*isp_clk;
	struct clk			*ram_clk;
	struct reset_control		*rst;

	struct vb2_v4l2_buffer		*current_buf[CSI_MAX_BUFFER];

	struct {
		size_t			size;
		void			*vaddr;
		dma_addr_t		paddr;
	} scratch;

	struct v4l2_fwnode_bus_parallel	bus;

	/* Main Device */
	struct v4l2_device		v4l;
	struct media_device		mdev;
	struct video_device		vdev;
	struct media_pad		vdev_pad;
	struct v4l2_pix_format_mplane	fmt;

	/* Local subdev */
	struct v4l2_subdev		subdev;
	struct media_pad		subdev_pads[CSI_SUBDEV_PADS];
	struct v4l2_mbus_framefmt	subdev_fmt;

	/* V4L2 Async variables */
	struct v4l2_async_subdev	asd;
	struct v4l2_async_notifier	notifier;
	struct v4l2_subdev		*src_subdev;
	int				src_pad;

	/* V4L2 variables */
	struct mutex			lock;

	/* Videobuf2 */
	struct vb2_queue		queue;
	struct list_head		buf_list;
	spinlock_t			qlock;
	unsigned int			sequence;
};

int sun4i_csi_dma_register(struct sun4i_csi *csi, int irq);
void sun4i_csi_dma_unregister(struct sun4i_csi *csi);

int sun4i_csi_v4l2_register(struct sun4i_csi *csi);

#endif /* _SUN4I_CSI_H_ */
