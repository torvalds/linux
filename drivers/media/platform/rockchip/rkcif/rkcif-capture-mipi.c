// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Camera Interface (CIF) Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 * Copyright (C) 2025 Michael Riesch <michael.riesch@wolfvision.net>
 * Copyright (C) 2025 Collabora, Ltd.
 */

#include <linux/interrupt.h>

#include <media/mipi-csi2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#include "rkcif-capture-mipi.h"
#include "rkcif-common.h"
#include "rkcif-interface.h"
#include "rkcif-regs.h"
#include "rkcif-stream.h"

#define RK3568_MIPI_CTRL0_HIGH_ALIGN  BIT(31)
#define RK3568_MIPI_CTRL0_UV_SWAP_EN  BIT(7)
#define RK3568_MIPI_CTRL0_COMPACT_EN  BIT(6)
#define RK3568_MIPI_CTRL0_CROP_EN     BIT(5)
#define RK3568_MIPI_CTRL0_WRDDR(type) ((type) << 1)

#define RKCIF_MIPI_CTRL0_DT_ID(id)    ((id) << 10)
#define RKCIF_MIPI_CTRL0_VC_ID(id)    ((id) << 8)
#define RKCIF_MIPI_CTRL0_CAP_EN	      BIT(0)

#define RKCIF_MIPI_INT_FRAME0_END(id) BIT(8 + (id) * 2 + 0)
#define RKCIF_MIPI_INT_FRAME1_END(id) BIT(8 + (id) * 2 + 1)

static const struct rkcif_output_fmt mipi_out_fmts[] = {
	/* YUV formats */
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
		.depth = 16,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_YUV422_8B,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_UYVY,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
		.depth = 16,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_YUV422_8B,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_YVYU,
		.mbus_code = MEDIA_BUS_FMT_YVYU8_1X16,
		.depth = 16,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_YUV422_8B,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_VYUY,
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
		.depth = 16,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_YUV422_8B,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	/* RGB formats */
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
		.depth = 24,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RGB888,
			.type = RKCIF_MIPI_TYPE_RGB888,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.mbus_code = MEDIA_BUS_FMT_BGR888_1X24,
		.depth = 24,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RGB888,
			.type = RKCIF_MIPI_TYPE_RGB888,
		},
	},
	/* Bayer formats */
	{
		.fourcc = V4L2_PIX_FMT_SBGGR8,
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.depth = 8,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW8,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG8,
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.depth = 8,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW8,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG8,
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.depth = 8,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW8,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB8,
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.depth = 8,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW8,
			.type = RKCIF_MIPI_TYPE_RAW8,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR10,
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR10P,
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG10,
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG10P,
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG10,
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG10P,
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB10,
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB10P,
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.depth = 10,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW10,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW10,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR12,
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SBGGR12P,
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG12,
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGBRG12P,
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG12,
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SGRBG12P,
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB12,
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
	{
		.fourcc = V4L2_PIX_FMT_SRGGB12P,
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.depth = 12,
		.cplanes = 1,
		.mipi = {
			.dt = MIPI_CSI2_DT_RAW12,
			.compact = true,
			.type = RKCIF_MIPI_TYPE_RAW12,
		},
	},
};

static const struct rkcif_input_fmt mipi_in_fmts[] = {
	/* YUV formats */
	{
		.mbus_code = MEDIA_BUS_FMT_YUYV8_1X16,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_UYVY8_1X16,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_YVYU8_1X16,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_VYUY8_1X16,
	},
	/* RGB formats */
	{
		.mbus_code = MEDIA_BUS_FMT_RGB888_1X24,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_BGR888_1X24,
	},
	/* Bayer formats */
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR8_1X8,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG8_1X8,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG8_1X8,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB8_1X8,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR10_1X10,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG10_1X10,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB10_1X10,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SBGGR12_1X12,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGBRG12_1X12,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SGRBG12_1X12,
	},
	{
		.mbus_code = MEDIA_BUS_FMT_SRGGB12_1X12,
	},
};

static u32
rkcif_rk3568_mipi_ctrl0(struct rkcif_stream *stream,
			const struct rkcif_output_fmt *active_out_fmt)
{
	u32 ctrl0 = 0;

	ctrl0 |= RKCIF_MIPI_CTRL0_DT_ID(active_out_fmt->mipi.dt);
	ctrl0 |= RKCIF_MIPI_CTRL0_CAP_EN;
	ctrl0 |= RK3568_MIPI_CTRL0_CROP_EN;

	if (active_out_fmt->mipi.compact)
		ctrl0 |= RK3568_MIPI_CTRL0_COMPACT_EN;

	switch (active_out_fmt->mipi.type) {
	case RKCIF_MIPI_TYPE_RAW8:
		break;
	case RKCIF_MIPI_TYPE_RAW10:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x1);
		break;
	case RKCIF_MIPI_TYPE_RAW12:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x2);
		break;
	case RKCIF_MIPI_TYPE_RGB888:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x3);
		break;
	case RKCIF_MIPI_TYPE_YUV422SP:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x4);
		break;
	case RKCIF_MIPI_TYPE_YUV420SP:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x5);
		break;
	case RKCIF_MIPI_TYPE_YUV400:
		ctrl0 |= RK3568_MIPI_CTRL0_WRDDR(0x6);
		break;
	default:
		break;
	}

	return ctrl0;
}

const struct rkcif_mipi_match_data rkcif_rk3568_vicap_mipi_match_data = {
	.mipi_num = 1,
	.mipi_ctrl0 = rkcif_rk3568_mipi_ctrl0,
	.regs = {
		[RKCIF_MIPI_CTRL] = 0x20,
		[RKCIF_MIPI_INTEN] = 0xa4,
		[RKCIF_MIPI_INTSTAT] = 0xa8,
	},
	.regs_id = {
		[RKCIF_ID0] = {
			[RKCIF_MIPI_CTRL0] = 0x00,
			[RKCIF_MIPI_CTRL1] = 0x04,
			[RKCIF_MIPI_FRAME0_ADDR_Y] = 0x24,
			[RKCIF_MIPI_FRAME0_ADDR_UV] = 0x2c,
			[RKCIF_MIPI_FRAME0_VLW_Y] = 0x34,
			[RKCIF_MIPI_FRAME0_VLW_UV] = 0x3c,
			[RKCIF_MIPI_FRAME1_ADDR_Y] = 0x28,
			[RKCIF_MIPI_FRAME1_ADDR_UV] = 0x30,
			[RKCIF_MIPI_FRAME1_VLW_Y] = 0x38,
			[RKCIF_MIPI_FRAME1_VLW_UV] = 0x40,
			[RKCIF_MIPI_CROP_START] = 0xbc,
		},
		[RKCIF_ID1] = {
			[RKCIF_MIPI_CTRL0] = 0x08,
			[RKCIF_MIPI_CTRL1] = 0x0c,
			[RKCIF_MIPI_FRAME0_ADDR_Y] = 0x44,
			[RKCIF_MIPI_FRAME0_ADDR_UV] = 0x4c,
			[RKCIF_MIPI_FRAME0_VLW_Y] = 0x54,
			[RKCIF_MIPI_FRAME0_VLW_UV] = 0x5c,
			[RKCIF_MIPI_FRAME1_ADDR_Y] = 0x48,
			[RKCIF_MIPI_FRAME1_ADDR_UV] = 0x50,
			[RKCIF_MIPI_FRAME1_VLW_Y] = 0x58,
			[RKCIF_MIPI_FRAME1_VLW_UV] = 0x60,
			[RKCIF_MIPI_CROP_START] = 0xc0,
		},
		[RKCIF_ID2] = {
			[RKCIF_MIPI_CTRL0] = 0x10,
			[RKCIF_MIPI_CTRL1] = 0x14,
			[RKCIF_MIPI_FRAME0_ADDR_Y] = 0x64,
			[RKCIF_MIPI_FRAME0_ADDR_UV] = 0x6c,
			[RKCIF_MIPI_FRAME0_VLW_Y] = 0x74,
			[RKCIF_MIPI_FRAME0_VLW_UV] = 0x7c,
			[RKCIF_MIPI_FRAME1_ADDR_Y] = 0x68,
			[RKCIF_MIPI_FRAME1_ADDR_UV] = 0x70,
			[RKCIF_MIPI_FRAME1_VLW_Y] = 0x78,
			[RKCIF_MIPI_FRAME1_VLW_UV] = 0x80,
			[RKCIF_MIPI_CROP_START] = 0xc4,
		},
		[RKCIF_ID3] = {
			[RKCIF_MIPI_CTRL0] = 0x18,
			[RKCIF_MIPI_CTRL1] = 0x1c,
			[RKCIF_MIPI_FRAME0_ADDR_Y] = 0x84,
			[RKCIF_MIPI_FRAME0_ADDR_UV] = 0x8c,
			[RKCIF_MIPI_FRAME0_VLW_Y] = 0x94,
			[RKCIF_MIPI_FRAME0_VLW_UV] = 0x9c,
			[RKCIF_MIPI_FRAME1_ADDR_Y] = 0x88,
			[RKCIF_MIPI_FRAME1_ADDR_UV] = 0x90,
			[RKCIF_MIPI_FRAME1_VLW_Y] = 0x98,
			[RKCIF_MIPI_FRAME1_VLW_UV] = 0xa0,
			[RKCIF_MIPI_CROP_START] = 0xc8,
		},
	},
	.blocks = {
		{
			.offset = 0x80,
		},
	},
};

static inline unsigned int rkcif_mipi_get_reg(struct rkcif_interface *interface,
					      unsigned int index)
{
	struct rkcif_device *rkcif = interface->rkcif;
	unsigned int block, offset, reg;

	block = interface->index - RKCIF_MIPI_BASE;

	if (WARN_ON_ONCE(block > RKCIF_MIPI_MAX - RKCIF_MIPI_BASE) ||
	    WARN_ON_ONCE(index > RKCIF_MIPI_REGISTER_MAX))
		return RKCIF_REGISTER_NOTSUPPORTED;

	offset = rkcif->match_data->mipi->blocks[block].offset;
	reg = rkcif->match_data->mipi->regs[index];
	if (reg == RKCIF_REGISTER_NOTSUPPORTED)
		return reg;

	return offset + reg;
}

static inline unsigned int rkcif_mipi_id_get_reg(struct rkcif_stream *stream,
						 unsigned int index)
{
	struct rkcif_device *rkcif = stream->rkcif;
	unsigned int block, id, offset, reg;

	block = stream->interface->index - RKCIF_MIPI_BASE;
	id = stream->id;

	if (WARN_ON_ONCE(block > RKCIF_MIPI_MAX - RKCIF_MIPI_BASE) ||
	    WARN_ON_ONCE(id > RKCIF_ID_MAX) ||
	    WARN_ON_ONCE(index > RKCIF_MIPI_ID_REGISTER_MAX))
		return RKCIF_REGISTER_NOTSUPPORTED;

	offset = rkcif->match_data->mipi->blocks[block].offset;
	reg = rkcif->match_data->mipi->regs_id[id][index];
	if (reg == RKCIF_REGISTER_NOTSUPPORTED)
		return reg;

	return offset + reg;
}

static inline __maybe_unused void
rkcif_mipi_write(struct rkcif_interface *interface, unsigned int index, u32 val)
{
	unsigned int addr = rkcif_mipi_get_reg(interface, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return;

	writel(val, interface->rkcif->base_addr + addr);
}

static inline __maybe_unused void
rkcif_mipi_stream_write(struct rkcif_stream *stream, unsigned int index,
			u32 val)
{
	unsigned int addr = rkcif_mipi_id_get_reg(stream, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return;

	writel(val, stream->rkcif->base_addr + addr);
}

static inline __maybe_unused u32
rkcif_mipi_read(struct rkcif_interface *interface, unsigned int index)
{
	unsigned int addr = rkcif_mipi_get_reg(interface, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return 0;

	return readl(interface->rkcif->base_addr + addr);
}

static inline __maybe_unused u32
rkcif_mipi_stream_read(struct rkcif_stream *stream, unsigned int index)
{
	unsigned int addr = rkcif_mipi_id_get_reg(stream, index);

	if (addr == RKCIF_REGISTER_NOTSUPPORTED)
		return 0;

	return readl(stream->rkcif->base_addr + addr);
}

static void rkcif_mipi_queue_buffer(struct rkcif_stream *stream,
				    unsigned int index)
{
	struct rkcif_buffer *buffer = stream->buffers[index];
	u32 frm_addr_y, frm_addr_uv;

	frm_addr_y = index ? RKCIF_MIPI_FRAME1_ADDR_Y :
			     RKCIF_MIPI_FRAME0_ADDR_Y;
	frm_addr_uv = index ? RKCIF_MIPI_FRAME1_ADDR_UV :
			      RKCIF_MIPI_FRAME0_ADDR_UV;

	rkcif_mipi_stream_write(stream, frm_addr_y,
				buffer->buff_addr[RKCIF_PLANE_Y]);
	rkcif_mipi_stream_write(stream, frm_addr_uv,
				buffer->buff_addr[RKCIF_PLANE_UV]);
}

static int rkcif_mipi_start_streaming(struct rkcif_stream *stream)
{
	struct rkcif_interface *interface = stream->interface;
	const struct rkcif_output_fmt *active_out_fmt;
	const struct rkcif_mipi_match_data *match_data;
	struct v4l2_subdev_state *state;
	u32 ctrl0 = 0, ctrl1 = 0, int_temp = 0, int_mask = 0, vlw = 0;
	u16 height, width;
	int ret = -EINVAL;

	state = v4l2_subdev_lock_and_get_active_state(&interface->sd);

	active_out_fmt = rkcif_stream_find_output_fmt(stream, false,
						      stream->pix.pixelformat);
	if (!active_out_fmt)
		goto out;

	height = stream->pix.height;
	width = stream->pix.width;
	vlw = stream->pix.plane_fmt[0].bytesperline;

	match_data = stream->rkcif->match_data->mipi;
	if (match_data->mipi_ctrl0)
		ctrl0 = match_data->mipi_ctrl0(stream, active_out_fmt);

	ctrl1 = RKCIF_XY_COORD(width, height);

	int_mask |= RKCIF_MIPI_INT_FRAME0_END(stream->id);
	int_mask |= RKCIF_MIPI_INT_FRAME1_END(stream->id);

	int_temp = rkcif_mipi_read(interface, RKCIF_MIPI_INTEN);
	int_temp |= int_mask;
	rkcif_mipi_write(interface, RKCIF_MIPI_INTEN, int_temp);

	int_temp = rkcif_mipi_read(interface, RKCIF_MIPI_INTSTAT);
	int_temp &= ~int_mask;
	rkcif_mipi_write(interface, RKCIF_MIPI_INTSTAT, int_temp);

	rkcif_mipi_stream_write(stream, RKCIF_MIPI_FRAME0_VLW_Y, vlw);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_FRAME1_VLW_Y, vlw);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_FRAME0_VLW_UV, vlw);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_FRAME1_VLW_UV, vlw);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_CROP_START, 0x0);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_CTRL1, ctrl1);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_CTRL0, ctrl0);

	ret = 0;

out:
	v4l2_subdev_unlock_state(state);
	return ret;
}

static void rkcif_mipi_stop_streaming(struct rkcif_stream *stream)
{
	struct rkcif_interface *interface = stream->interface;
	struct v4l2_subdev_state *state;
	u32 int_temp = 0, int_mask = 0;

	state = v4l2_subdev_lock_and_get_active_state(&interface->sd);

	rkcif_mipi_stream_write(stream, RKCIF_MIPI_CTRL0, 0);

	int_mask |= RKCIF_MIPI_INT_FRAME0_END(stream->id);
	int_mask |= RKCIF_MIPI_INT_FRAME1_END(stream->id);

	int_temp = rkcif_mipi_read(interface, RKCIF_MIPI_INTEN);
	int_temp &= ~int_mask;
	rkcif_mipi_write(interface, RKCIF_MIPI_INTEN, int_temp);

	int_temp = rkcif_mipi_read(interface, RKCIF_MIPI_INTSTAT);
	int_temp &= ~int_mask;
	rkcif_mipi_write(interface, RKCIF_MIPI_INTSTAT, int_temp);

	stream->stopping = false;

	v4l2_subdev_unlock_state(state);
}

static void rkcif_mipi_set_crop(struct rkcif_stream *stream, u16 left, u16 top)
{
	u32 val;

	val = RKCIF_XY_COORD(left, top);
	rkcif_mipi_stream_write(stream, RKCIF_MIPI_CROP_START, val);
}

irqreturn_t rkcif_mipi_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkcif_device *rkcif = dev_get_drvdata(dev);
	irqreturn_t ret = IRQ_NONE;
	u32 intstat;

	for (unsigned int i = 0; i < rkcif->match_data->mipi->mipi_num; i++) {
		enum rkcif_interface_index index = RKCIF_MIPI_BASE + i;
		struct rkcif_interface *interface = &rkcif->interfaces[index];

		intstat = rkcif_mipi_read(interface, RKCIF_MIPI_INTSTAT);
		rkcif_mipi_write(interface, RKCIF_MIPI_INTSTAT, intstat);

		for (unsigned int j = 0; j < interface->streams_num; j++) {
			struct rkcif_stream *stream = &interface->streams[j];

			if (intstat & RKCIF_MIPI_INT_FRAME0_END(stream->id) ||
			    intstat & RKCIF_MIPI_INT_FRAME1_END(stream->id)) {
				ret = IRQ_HANDLED;

				if (stream->stopping) {
					rkcif_mipi_stop_streaming(stream);
					wake_up(&stream->wq_stopped);
					continue;
				}

				rkcif_stream_pingpong(stream);
			}
		}
	}

	return ret;
}

int rkcif_mipi_register(struct rkcif_device *rkcif)
{
	int ret;

	if (!rkcif->match_data->mipi)
		return 0;

	for (unsigned int i = 0; i < rkcif->match_data->mipi->mipi_num; i++) {
		enum rkcif_interface_index index = RKCIF_MIPI_BASE + i;
		struct rkcif_interface *interface = &rkcif->interfaces[index];

		interface->index = index;
		interface->type = RKCIF_IF_MIPI;
		interface->in_fmts = mipi_in_fmts;
		interface->in_fmts_num = ARRAY_SIZE(mipi_in_fmts);
		interface->set_crop = rkcif_mipi_set_crop;
		interface->streams_num = 0;
		ret = rkcif_interface_register(rkcif, interface);
		if (ret)
			continue;

		for (unsigned int j = 0; j < RKCIF_ID_MAX; j++) {
			struct rkcif_stream *stream = &interface->streams[j];

			stream->id = j;
			stream->interface = interface;
			stream->out_fmts = mipi_out_fmts;
			stream->out_fmts_num = ARRAY_SIZE(mipi_out_fmts);
			stream->queue_buffer = rkcif_mipi_queue_buffer;
			stream->start_streaming = rkcif_mipi_start_streaming;
			stream->stop_streaming = rkcif_mipi_stop_streaming;
			ret = rkcif_stream_register(rkcif, stream);
			if (ret)
				goto err;
			interface->streams_num++;
		}
	}

	return 0;

err:
	for (unsigned int i = 0; i < rkcif->match_data->mipi->mipi_num; i++) {
		enum rkcif_interface_index index = RKCIF_MIPI_BASE + i;
		struct rkcif_interface *interface = &rkcif->interfaces[index];

		for (unsigned int j = 0; j < interface->streams_num; j++)
			rkcif_stream_unregister(&interface->streams[j]);

		rkcif_interface_unregister(interface);
	}
	return ret;
}

void rkcif_mipi_unregister(struct rkcif_device *rkcif)
{
	if (!rkcif->match_data->mipi)
		return;

	for (unsigned int i = 0; i < rkcif->match_data->mipi->mipi_num; i++) {
		enum rkcif_interface_index index = RKCIF_MIPI_BASE + i;
		struct rkcif_interface *interface = &rkcif->interfaces[index];

		for (unsigned int j = 0; j < interface->streams_num; j++)
			rkcif_stream_unregister(&interface->streams[j]);

		rkcif_interface_unregister(interface);
	}
}
