// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "sun6i_csi.h"
#include "sun6i_csi_bridge.h"
#include "sun6i_csi_capture.h"
#include "sun6i_csi_reg.h"

/* Helpers */

void sun6i_csi_capture_dimensions(struct sun6i_csi_device *csi_dev,
				  unsigned int *width, unsigned int *height)
{
	if (width)
		*width = csi_dev->capture.format.fmt.pix.width;
	if (height)
		*height = csi_dev->capture.format.fmt.pix.height;
}

void sun6i_csi_capture_format(struct sun6i_csi_device *csi_dev,
			      u32 *pixelformat, u32 *field)
{
	if (pixelformat)
		*pixelformat = csi_dev->capture.format.fmt.pix.pixelformat;

	if (field)
		*field = csi_dev->capture.format.fmt.pix.field;
}

static struct v4l2_subdev *
sun6i_csi_capture_remote_subdev(struct sun6i_csi_capture *capture, u32 *pad)
{
	struct media_pad *remote;

	remote = media_pad_remote_pad_first(&capture->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

/* Format */

static const struct sun6i_csi_capture_format sun6i_csi_capture_formats[] = {
	/* Bayer */
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB8,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB10,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_10,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_10,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SBGGR12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGBRG12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SGRBG12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_SRGGB12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_12,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_12,
	},
	/* RGB */
	{
		.pixelformat		= V4L2_PIX_FMT_RGB565,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RGB565,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RGB565,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_RGB565X,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RGB565,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RGB565,
	},
	/* YUV422 */
	{
		.pixelformat		= V4L2_PIX_FMT_YUYV,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YVYU,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_UYVY,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_VYUY,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
		.input_format_raw	= true,
		.hsize_len_factor	= 2,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV16,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422SP,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV61,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422SP,
		.input_yuv_seq_invert	= true,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YUV422P,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV422P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV422P,
	},
	/* YUV420 */
	{
		.pixelformat		= V4L2_PIX_FMT_NV12_16L16,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420MB,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420MB,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV12,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420SP,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_NV21,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420SP,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420SP,
		.input_yuv_seq_invert	= true,
	},

	{
		.pixelformat		= V4L2_PIX_FMT_YUV420,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420P,
	},
	{
		.pixelformat		= V4L2_PIX_FMT_YVU420,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_YUV420P,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_YUV420P,
		.input_yuv_seq_invert	= true,
	},
	/* Compressed */
	{
		.pixelformat		= V4L2_PIX_FMT_JPEG,
		.output_format_frame	= SUN6I_CSI_OUTPUT_FMT_FRAME_RAW_8,
		.output_format_field	= SUN6I_CSI_OUTPUT_FMT_FIELD_RAW_8,
	},
};

const
struct sun6i_csi_capture_format *sun6i_csi_capture_format_find(u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun6i_csi_capture_formats); i++)
		if (sun6i_csi_capture_formats[i].pixelformat == pixelformat)
			return &sun6i_csi_capture_formats[i];

	return NULL;
}

/* Capture */

static void sun6i_csi_capture_irq_enable(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;

	regmap_write(regmap, SUN6I_CSI_CH_INT_EN_REG,
		     SUN6I_CSI_CH_INT_EN_VS |
		     SUN6I_CSI_CH_INT_EN_HB_OF |
		     SUN6I_CSI_CH_INT_EN_FIFO2_OF |
		     SUN6I_CSI_CH_INT_EN_FIFO1_OF |
		     SUN6I_CSI_CH_INT_EN_FIFO0_OF |
		     SUN6I_CSI_CH_INT_EN_FD |
		     SUN6I_CSI_CH_INT_EN_CD);
}

static void sun6i_csi_capture_irq_disable(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;

	regmap_write(regmap, SUN6I_CSI_CH_INT_EN_REG, 0);
}

static void sun6i_csi_capture_irq_clear(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;

	regmap_write(regmap, SUN6I_CSI_CH_INT_EN_REG, 0);
	regmap_write(regmap, SUN6I_CSI_CH_INT_STA_REG,
		     SUN6I_CSI_CH_INT_STA_CLEAR);
}

static void sun6i_csi_capture_enable(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;

	regmap_update_bits(regmap, SUN6I_CSI_EN_REG, SUN6I_CSI_EN_CSI_EN,
			   SUN6I_CSI_EN_CSI_EN);

	regmap_update_bits(regmap, SUN6I_CSI_CAP_REG, SUN6I_CSI_CAP_VCAP_ON,
			   SUN6I_CSI_CAP_VCAP_ON);
}

static void sun6i_csi_capture_disable(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;

	regmap_update_bits(regmap, SUN6I_CSI_CAP_REG, SUN6I_CSI_CAP_VCAP_ON, 0);
	regmap_update_bits(regmap, SUN6I_CSI_EN_REG, SUN6I_CSI_EN_CSI_EN, 0);
}

static void
sun6i_csi_capture_buffer_configure(struct sun6i_csi_device *csi_dev,
				   struct sun6i_csi_buffer *csi_buffer)
{
	struct regmap *regmap = csi_dev->regmap;
	const struct v4l2_format_info *info;
	struct vb2_buffer *vb2_buffer;
	unsigned int width, height;
	dma_addr_t address;
	u32 pixelformat;

	vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
	address = vb2_dma_contig_plane_dma_addr(vb2_buffer, 0);

	regmap_write(regmap, SUN6I_CSI_CH_FIFO0_ADDR_REG,
		     SUN6I_CSI_ADDR_VALUE(address));

	sun6i_csi_capture_dimensions(csi_dev, &width, &height);
	sun6i_csi_capture_format(csi_dev, &pixelformat, NULL);

	info = v4l2_format_info(pixelformat);
	/* Unsupported formats are single-plane, so we can stop here. */
	if (!info)
		return;

	if (info->comp_planes > 1) {
		address += info->bpp[0] * width * height;

		regmap_write(regmap, SUN6I_CSI_CH_FIFO1_ADDR_REG,
			     SUN6I_CSI_ADDR_VALUE(address));
	}

	if (info->comp_planes > 2) {
		address += info->bpp[1] * DIV_ROUND_UP(width, info->hdiv) *
			   DIV_ROUND_UP(height, info->vdiv);

		regmap_write(regmap, SUN6I_CSI_CH_FIFO2_ADDR_REG,
			     SUN6I_CSI_ADDR_VALUE(address));
	}
}

static enum csi_input_fmt get_csi_input_format(struct sun6i_csi_device *csi_dev,
					       u32 mbus_code, u32 pixformat)
{
	/* non-YUV */
	if ((mbus_code & 0xF000) != 0x2000)
		return CSI_INPUT_FORMAT_RAW;

	switch (pixformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		return CSI_INPUT_FORMAT_RAW;
	default:
		break;
	}

	/* not support YUV420 input format yet */
	dev_dbg(csi_dev->dev, "Select YUV422 as default input format of CSI.\n");
	return CSI_INPUT_FORMAT_YUV422;
}

static enum csi_output_fmt
get_csi_output_format(struct sun6i_csi_device *csi_dev, u32 pixformat,
		      u32 field)
{
	bool buf_interlaced = false;

	if (field == V4L2_FIELD_INTERLACED
	    || field == V4L2_FIELD_INTERLACED_TB
	    || field == V4L2_FIELD_INTERLACED_BT)
		buf_interlaced = true;

	switch (pixformat) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		return buf_interlaced ? CSI_FRAME_RAW_8 : CSI_FIELD_RAW_8;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		return buf_interlaced ? CSI_FRAME_RAW_10 : CSI_FIELD_RAW_10;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		return buf_interlaced ? CSI_FRAME_RAW_12 : CSI_FIELD_RAW_12;

	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		return buf_interlaced ? CSI_FRAME_RAW_8 : CSI_FIELD_RAW_8;

	case V4L2_PIX_FMT_NV12_16L16:
		return buf_interlaced ? CSI_FRAME_MB_YUV420 :
					CSI_FIELD_MB_YUV420;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return buf_interlaced ? CSI_FRAME_UV_CB_YUV420 :
					CSI_FIELD_UV_CB_YUV420;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		return buf_interlaced ? CSI_FRAME_PLANAR_YUV420 :
					CSI_FIELD_PLANAR_YUV420;
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		return buf_interlaced ? CSI_FRAME_UV_CB_YUV422 :
					CSI_FIELD_UV_CB_YUV422;
	case V4L2_PIX_FMT_YUV422P:
		return buf_interlaced ? CSI_FRAME_PLANAR_YUV422 :
					CSI_FIELD_PLANAR_YUV422;

	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
		return buf_interlaced ? CSI_FRAME_RGB565 : CSI_FIELD_RGB565;

	case V4L2_PIX_FMT_JPEG:
		return buf_interlaced ? CSI_FRAME_RAW_8 : CSI_FIELD_RAW_8;

	default:
		dev_warn(csi_dev->dev, "Unsupported pixformat: 0x%x\n", pixformat);
		break;
	}

	return CSI_FIELD_RAW_8;
}

static enum csi_input_seq get_csi_input_seq(struct sun6i_csi_device *csi_dev,
					    u32 mbus_code, u32 pixformat)
{
	/* Input sequence does not apply to non-YUV formats */
	if ((mbus_code & 0xF000) != 0x2000)
		return 0;

	switch (pixformat) {
	case V4L2_PIX_FMT_NV12_16L16:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YUV422P:
		switch (mbus_code) {
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_UYVY8_1X16:
			return CSI_INPUT_SEQ_UYVY;
		case MEDIA_BUS_FMT_VYUY8_2X8:
		case MEDIA_BUS_FMT_VYUY8_1X16:
			return CSI_INPUT_SEQ_VYUY;
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YUYV8_1X16:
			return CSI_INPUT_SEQ_YUYV;
		case MEDIA_BUS_FMT_YVYU8_1X16:
		case MEDIA_BUS_FMT_YVYU8_2X8:
			return CSI_INPUT_SEQ_YVYU;
		default:
			dev_warn(csi_dev->dev, "Unsupported mbus code: 0x%x\n",
				 mbus_code);
			break;
		}
		break;
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YVU420:
		switch (mbus_code) {
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_UYVY8_1X16:
			return CSI_INPUT_SEQ_VYUY;
		case MEDIA_BUS_FMT_VYUY8_2X8:
		case MEDIA_BUS_FMT_VYUY8_1X16:
			return CSI_INPUT_SEQ_UYVY;
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YUYV8_1X16:
			return CSI_INPUT_SEQ_YVYU;
		case MEDIA_BUS_FMT_YVYU8_1X16:
		case MEDIA_BUS_FMT_YVYU8_2X8:
			return CSI_INPUT_SEQ_YUYV;
		default:
			dev_warn(csi_dev->dev, "Unsupported mbus code: 0x%x\n",
				 mbus_code);
			break;
		}
		break;

	case V4L2_PIX_FMT_YUYV:
		return CSI_INPUT_SEQ_YUYV;

	default:
		dev_warn(csi_dev->dev, "Unsupported pixformat: 0x%x, defaulting to YUYV\n",
			 pixformat);
		break;
	}

	return CSI_INPUT_SEQ_YUYV;
}

static void
sun6i_csi_capture_configure_interface(struct sun6i_csi_device *csi_dev)
{
	struct device *dev = csi_dev->dev;
	struct regmap *regmap = csi_dev->regmap;
	struct v4l2_fwnode_endpoint *endpoint =
		&csi_dev->bridge.source_parallel.endpoint;
	unsigned char bus_width = endpoint->bus.parallel.bus_width;
	unsigned int flags = endpoint->bus.parallel.flags;
	u32 pixelformat, field;
	u32 value = SUN6I_CSI_IF_CFG_IF_CSI;

	sun6i_csi_capture_format(csi_dev, &pixelformat, &field);

	if (field == V4L2_FIELD_INTERLACED ||
	    field == V4L2_FIELD_INTERLACED_TB ||
	    field == V4L2_FIELD_INTERLACED_BT)
		value |= SUN6I_CSI_IF_CFG_SRC_TYPE_INTERLACED |
			 SUN6I_CSI_IF_CFG_FIELD_DT_PCLK_SHIFT(1) |
			 SUN6I_CSI_IF_CFG_FIELD_DT_FIELD_VSYNC;
	else
		value |= SUN6I_CSI_IF_CFG_SRC_TYPE_PROGRESSIVE;

	switch (endpoint->bus_type) {
	case V4L2_MBUS_PARALLEL:
		if (bus_width == 16)
			value |= SUN6I_CSI_IF_CFG_IF_CSI_YUV_COMBINED;
		else
			value |= SUN6I_CSI_IF_CFG_IF_CSI_YUV_RAW;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			value |= SUN6I_CSI_IF_CFG_FIELD_NEGATIVE;
		else
			value |= SUN6I_CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			value |= SUN6I_CSI_IF_CFG_VREF_POL_NEGATIVE;
		else
			value |= SUN6I_CSI_IF_CFG_VREF_POL_POSITIVE;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			value |= SUN6I_CSI_IF_CFG_HREF_POL_NEGATIVE;
		else
			value |= SUN6I_CSI_IF_CFG_HREF_POL_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			value |= SUN6I_CSI_IF_CFG_CLK_POL_RISING;
		else
			value |= SUN6I_CSI_IF_CFG_CLK_POL_FALLING;
		break;
	case V4L2_MBUS_BT656:
		if (bus_width == 16)
			value |= SUN6I_CSI_IF_CFG_IF_CSI_BT1120;
		else
			value |= SUN6I_CSI_IF_CFG_IF_CSI_BT656;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			value |= SUN6I_CSI_IF_CFG_FIELD_NEGATIVE;
		else
			value |= SUN6I_CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			value |= SUN6I_CSI_IF_CFG_CLK_POL_RISING;
		else
			value |= SUN6I_CSI_IF_CFG_CLK_POL_FALLING;
		break;
	default:
		dev_warn(dev, "unsupported bus type: %d\n", endpoint->bus_type);
		break;
	}

	switch (bus_width) {
	case 8:
	/* 16-bit YUV formats use a doubled width in 8-bit mode. */
	case 16:
		value |= SUN6I_CSI_IF_CFG_DATA_WIDTH_8;
		break;
	case 10:
		value |= SUN6I_CSI_IF_CFG_DATA_WIDTH_10;
		break;
	case 12:
		value |= SUN6I_CSI_IF_CFG_DATA_WIDTH_12;
		break;
	default:
		dev_warn(dev, "unsupported bus width: %u\n", bus_width);
		break;
	}

	regmap_write(regmap, SUN6I_CSI_IF_CFG_REG, value);
}

static void sun6i_csi_capture_configure_format(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;
	u32 mbus_code, pixelformat, field;
	u8 input_format, input_yuv_seq, output_format;
	u32 value = 0;

	sun6i_csi_capture_format(csi_dev, &pixelformat, &field);
	sun6i_csi_bridge_format(csi_dev, &mbus_code, NULL);

	input_format = get_csi_input_format(csi_dev, mbus_code, pixelformat);
	input_yuv_seq = get_csi_input_seq(csi_dev, mbus_code, pixelformat);
	output_format = get_csi_output_format(csi_dev, pixelformat, field);

	value |= SUN6I_CSI_CH_CFG_OUTPUT_FMT(output_format);
	value |= SUN6I_CSI_CH_CFG_INPUT_FMT(input_format);
	value |= SUN6I_CSI_CH_CFG_INPUT_YUV_SEQ(input_yuv_seq);

	if (field == V4L2_FIELD_TOP)
		value |= SUN6I_CSI_CH_CFG_FIELD_SEL_FIELD0;
	else if (field == V4L2_FIELD_BOTTOM)
		value |= SUN6I_CSI_CH_CFG_FIELD_SEL_FIELD1;
	else
		value |= SUN6I_CSI_CH_CFG_FIELD_SEL_EITHER;

	regmap_write(regmap, SUN6I_CSI_CH_CFG_REG, value);
}

static void sun6i_csi_capture_configure_window(struct sun6i_csi_device *csi_dev)
{
	struct regmap *regmap = csi_dev->regmap;
	const struct v4l2_format_info *info;
	u32 hsize_len, vsize_len;
	u32 luma_line, chroma_line = 0;
	u32 pixelformat, field;
	u32 width, height;

	sun6i_csi_capture_dimensions(csi_dev, &width, &height);
	sun6i_csi_capture_format(csi_dev, &pixelformat, &field);

	hsize_len = width;
	vsize_len = height;

	switch (pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		/*
		 * Horizontal length should be 2 times of width for packed
		 * YUV formats.
		 */
		hsize_len *= 2;
		break;
	default:
		break;
	}

	regmap_write(regmap, SUN6I_CSI_CH_HSIZE_REG,
		     SUN6I_CSI_CH_HSIZE_LEN(hsize_len) |
		     SUN6I_CSI_CH_HSIZE_START(0));

	regmap_write(regmap, SUN6I_CSI_CH_VSIZE_REG,
		     SUN6I_CSI_CH_VSIZE_LEN(vsize_len) |
		     SUN6I_CSI_CH_VSIZE_START(0));

	switch (pixelformat) {
	case V4L2_PIX_FMT_RGB565X:
		luma_line = width * 2;
		break;
	case V4L2_PIX_FMT_NV12_16L16:
		luma_line = width;
		chroma_line = width;
		break;
	case V4L2_PIX_FMT_JPEG:
		luma_line = width;
		break;
	default:
		info = v4l2_format_info(pixelformat);
		if (WARN_ON(!info))
			return;

		luma_line = width * info->bpp[0];

		if (info->comp_planes > 1)
			chroma_line = width * info->bpp[1] / info->hdiv;
		break;
	}

	regmap_write(regmap, SUN6I_CSI_CH_BUF_LEN_REG,
		     SUN6I_CSI_CH_BUF_LEN_CHROMA_LINE(chroma_line) |
		     SUN6I_CSI_CH_BUF_LEN_LUMA_LINE(luma_line));
}

static void sun6i_csi_capture_configure(struct sun6i_csi_device *csi_dev)
{
	sun6i_csi_capture_configure_interface(csi_dev);
	sun6i_csi_capture_configure_format(csi_dev);
	sun6i_csi_capture_configure_window(csi_dev);
}

/* State */

static void sun6i_csi_capture_state_cleanup(struct sun6i_csi_device *csi_dev,
					    bool error)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer **csi_buffer_states[] = {
		&state->pending, &state->current, &state->complete,
	};
	struct sun6i_csi_buffer *csi_buffer;
	struct vb2_buffer *vb2_buffer;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->lock, flags);

	for (i = 0; i < ARRAY_SIZE(csi_buffer_states); i++) {
		csi_buffer = *csi_buffer_states[i];
		if (!csi_buffer)
			continue;

		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);

		*csi_buffer_states[i] = NULL;
	}

	list_for_each_entry(csi_buffer, &state->queue, list) {
		vb2_buffer = &csi_buffer->v4l2_buffer.vb2_buf;
		vb2_buffer_done(vb2_buffer, error ? VB2_BUF_STATE_ERROR :
				VB2_BUF_STATE_QUEUED);
	}

	INIT_LIST_HEAD(&state->queue);

	spin_unlock_irqrestore(&state->lock, flags);
}

static void sun6i_csi_capture_state_update(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct sun6i_csi_buffer *csi_buffer;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (list_empty(&state->queue))
		goto complete;

	if (state->pending)
		goto complete;

	csi_buffer = list_first_entry(&state->queue, struct sun6i_csi_buffer,
				      list);

	sun6i_csi_capture_buffer_configure(csi_dev, csi_buffer);

	list_del(&csi_buffer->list);

	state->pending = csi_buffer;

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

static void sun6i_csi_capture_state_complete(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);

	if (!state->pending)
		goto complete;

	state->complete = state->current;
	state->current = state->pending;
	state->pending = NULL;

	if (state->complete) {
		struct sun6i_csi_buffer *csi_buffer = state->complete;
		struct vb2_buffer *vb2_buffer =
			&csi_buffer->v4l2_buffer.vb2_buf;

		vb2_buffer->timestamp = ktime_get_ns();
		csi_buffer->v4l2_buffer.sequence = state->sequence;

		vb2_buffer_done(vb2_buffer, VB2_BUF_STATE_DONE);

		state->complete = NULL;
	}

complete:
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_frame_done(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	state->sequence++;
	spin_unlock_irqrestore(&state->lock, flags);
}

void sun6i_csi_capture_sync(struct sun6i_csi_device *csi_dev)
{
	sun6i_csi_capture_state_complete(csi_dev);
	sun6i_csi_capture_state_update(csi_dev);
}

/* Queue */

static int sun6i_csi_capture_queue_setup(struct vb2_queue *queue,
					 unsigned int *buffers_count,
					 unsigned int *planes_count,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	unsigned int size = csi_dev->capture.format.fmt.pix.sizeimage;

	if (*planes_count)
		return sizes[0] < size ? -EINVAL : 0;

	*planes_count = 1;
	sizes[0] = size;

	return 0;
}

static int sun6i_csi_capture_buffer_prepare(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	unsigned long size = capture->format.fmt.pix.sizeimage;

	if (vb2_plane_size(buffer, 0) < size) {
		v4l2_err(v4l2_dev, "buffer too small (%lu < %lu)\n",
			 vb2_plane_size(buffer, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(buffer, 0, size);

	v4l2_buffer->field = capture->format.fmt.pix.field;

	return 0;
}

static void sun6i_csi_capture_buffer_queue(struct vb2_buffer *buffer)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(buffer->vb2_queue);
	struct sun6i_csi_capture_state *state = &csi_dev->capture.state;
	struct vb2_v4l2_buffer *v4l2_buffer = to_vb2_v4l2_buffer(buffer);
	struct sun6i_csi_buffer *csi_buffer =
		container_of(v4l2_buffer, struct sun6i_csi_buffer, v4l2_buffer);
	unsigned long flags;

	spin_lock_irqsave(&state->lock, flags);
	list_add_tail(&csi_buffer->list, &state->queue);
	spin_unlock_irqrestore(&state->lock, flags);
}

static int sun6i_csi_capture_start_streaming(struct vb2_queue *queue,
					     unsigned int count)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_capture_state *state = &capture->state;
	struct video_device *video_dev = &capture->video_dev;
	struct device *dev = csi_dev->dev;
	struct v4l2_subdev *subdev;
	int ret;

	state->sequence = 0;

	ret = video_device_pipeline_alloc_start(video_dev);
	if (ret < 0)
		goto error_state;

	subdev = sun6i_csi_capture_remote_subdev(capture, NULL);
	if (!subdev) {
		ret = -EINVAL;
		goto error_media_pipeline;
	}

	/* PM */

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto error_media_pipeline;

	/* Clear */

	sun6i_csi_capture_irq_clear(csi_dev);

	/* Configure */

	sun6i_csi_capture_configure(csi_dev);

	/* State Update */

	sun6i_csi_capture_state_update(csi_dev);

	/* Enable */

	sun6i_csi_capture_irq_enable(csi_dev);
	sun6i_csi_capture_enable(csi_dev);

	ret = v4l2_subdev_call(subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto error_stream;

	return 0;

error_stream:
	sun6i_csi_capture_disable(csi_dev);
	sun6i_csi_capture_irq_disable(csi_dev);

	pm_runtime_put(dev);

error_media_pipeline:
	video_device_pipeline_stop(video_dev);

error_state:
	sun6i_csi_capture_state_cleanup(csi_dev, false);

	return ret;
}

static void sun6i_csi_capture_stop_streaming(struct vb2_queue *queue)
{
	struct sun6i_csi_device *csi_dev = vb2_get_drv_priv(queue);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct device *dev = csi_dev->dev;
	struct v4l2_subdev *subdev;

	subdev = sun6i_csi_capture_remote_subdev(capture, NULL);
	if (subdev)
		v4l2_subdev_call(subdev, video, s_stream, 0);

	sun6i_csi_capture_disable(csi_dev);
	sun6i_csi_capture_irq_disable(csi_dev);

	pm_runtime_put(dev);

	video_device_pipeline_stop(&capture->video_dev);

	sun6i_csi_capture_state_cleanup(csi_dev, true);
}

static const struct vb2_ops sun6i_csi_capture_queue_ops = {
	.queue_setup		= sun6i_csi_capture_queue_setup,
	.buf_prepare		= sun6i_csi_capture_buffer_prepare,
	.buf_queue		= sun6i_csi_capture_buffer_queue,
	.start_streaming	= sun6i_csi_capture_start_streaming,
	.stop_streaming		= sun6i_csi_capture_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* V4L2 Device */

static void sun6i_csi_capture_format_prepare(struct v4l2_format *format)
{
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	const struct v4l2_format_info *info;
	unsigned int width, height;

	v4l_bound_align_image(&pix_format->width, SUN6I_CSI_CAPTURE_WIDTH_MIN,
			      SUN6I_CSI_CAPTURE_WIDTH_MAX, 1,
			      &pix_format->height, SUN6I_CSI_CAPTURE_HEIGHT_MIN,
			      SUN6I_CSI_CAPTURE_HEIGHT_MAX, 1, 0);

	if (!sun6i_csi_capture_format_find(pix_format->pixelformat))
		pix_format->pixelformat =
			sun6i_csi_capture_formats[0].pixelformat;

	width = pix_format->width;
	height = pix_format->height;

	info = v4l2_format_info(pix_format->pixelformat);

	switch (pix_format->pixelformat) {
	case V4L2_PIX_FMT_NV12_16L16:
		pix_format->bytesperline = width * 12 / 8;
		pix_format->sizeimage = pix_format->bytesperline * height;
		break;
	case V4L2_PIX_FMT_JPEG:
		pix_format->bytesperline = width;
		pix_format->sizeimage = pix_format->bytesperline * height;
		break;
	default:
		v4l2_fill_pixfmt(pix_format, pix_format->pixelformat,
				 width, height);
		break;
	}

	if (pix_format->field == V4L2_FIELD_ANY)
		pix_format->field = V4L2_FIELD_NONE;

	if (pix_format->pixelformat == V4L2_PIX_FMT_JPEG)
		pix_format->colorspace = V4L2_COLORSPACE_JPEG;
	else if (info && info->pixel_enc == V4L2_PIXEL_ENC_BAYER)
		pix_format->colorspace = V4L2_COLORSPACE_RAW;
	else
		pix_format->colorspace = V4L2_COLORSPACE_SRGB;

	pix_format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	pix_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	pix_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun6i_csi_capture_querycap(struct file *file, void *private,
				      struct v4l2_capability *capability)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct video_device *video_dev = &csi_dev->capture.video_dev;

	strscpy(capability->driver, SUN6I_CSI_NAME, sizeof(capability->driver));
	strscpy(capability->card, video_dev->name, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", dev_name(csi_dev->dev));

	return 0;
}

static int sun6i_csi_capture_enum_fmt(struct file *file, void *private,
				      struct v4l2_fmtdesc *fmtdesc)
{
	u32 index = fmtdesc->index;

	if (index >= ARRAY_SIZE(sun6i_csi_capture_formats))
		return -EINVAL;

	fmtdesc->pixelformat = sun6i_csi_capture_formats[index].pixelformat;

	return 0;
}

static int sun6i_csi_capture_g_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);

	*format = csi_dev->capture.format;

	return 0;
}

static int sun6i_csi_capture_s_fmt(struct file *file, void *private,
				   struct v4l2_format *format)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	if (vb2_is_busy(&capture->queue))
		return -EBUSY;

	sun6i_csi_capture_format_prepare(format);

	csi_dev->capture.format = *format;

	return 0;
}

static int sun6i_csi_capture_try_fmt(struct file *file, void *private,
				     struct v4l2_format *format)
{
	sun6i_csi_capture_format_prepare(format);

	return 0;
}

static int sun6i_csi_capture_enum_input(struct file *file, void *private,
					struct v4l2_input *input)
{
	if (input->index != 0)
		return -EINVAL;

	input->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(input->name, "Camera", sizeof(input->name));

	return 0;
}

static int sun6i_csi_capture_g_input(struct file *file, void *private,
				     unsigned int *index)
{
	*index = 0;

	return 0;
}

static int sun6i_csi_capture_s_input(struct file *file, void *private,
				     unsigned int index)
{
	if (index != 0)
		return -EINVAL;

	return 0;
}

static const struct v4l2_ioctl_ops sun6i_csi_capture_ioctl_ops = {
	.vidioc_querycap		= sun6i_csi_capture_querycap,

	.vidioc_enum_fmt_vid_cap	= sun6i_csi_capture_enum_fmt,
	.vidioc_g_fmt_vid_cap		= sun6i_csi_capture_g_fmt,
	.vidioc_s_fmt_vid_cap		= sun6i_csi_capture_s_fmt,
	.vidioc_try_fmt_vid_cap		= sun6i_csi_capture_try_fmt,

	.vidioc_enum_input		= sun6i_csi_capture_enum_input,
	.vidioc_g_input			= sun6i_csi_capture_g_input,
	.vidioc_s_input			= sun6i_csi_capture_s_input,

	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* V4L2 File */

static int sun6i_csi_capture_open(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	int ret = 0;

	if (mutex_lock_interruptible(&capture->lock))
		return -ERESTARTSYS;

	ret = v4l2_pipeline_pm_get(&capture->video_dev.entity);
	if (ret < 0)
		goto error_lock;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto error_pipeline;

	mutex_unlock(&capture->lock);

	return 0;

error_pipeline:
	v4l2_pipeline_pm_put(&capture->video_dev.entity);

error_lock:
	mutex_unlock(&capture->lock);

	return ret;
}

static int sun6i_csi_capture_close(struct file *file)
{
	struct sun6i_csi_device *csi_dev = video_drvdata(file);
	struct sun6i_csi_capture *capture = &csi_dev->capture;

	mutex_lock(&capture->lock);

	_vb2_fop_release(file, NULL);
	v4l2_pipeline_pm_put(&capture->video_dev.entity);

	mutex_unlock(&capture->lock);

	return 0;
}

static const struct v4l2_file_operations sun6i_csi_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= sun6i_csi_capture_open,
	.release	= sun6i_csi_capture_close,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll
};

/* Media Entity */

static int
sun6i_csi_capture_link_validate_get_format(struct media_pad *pad,
					   struct v4l2_subdev_format *fmt)
{
	if (is_media_entity_v4l2_subdev(pad->entity)) {
		struct v4l2_subdev *sd =
				media_entity_to_v4l2_subdev(pad->entity);

		fmt->which = V4L2_SUBDEV_FORMAT_ACTIVE;
		fmt->pad = pad->index;
		return v4l2_subdev_call(sd, pad, get_fmt, NULL, fmt);
	}

	return -EINVAL;
}

static int sun6i_csi_capture_link_validate(struct media_link *link)
{
	struct video_device *vdev = container_of(link->sink->entity,
						 struct video_device, entity);
	struct sun6i_csi_device *csi_dev = video_get_drvdata(vdev);
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct v4l2_subdev_format source_fmt;
	int ret;

	if (!media_pad_remote_pad_first(link->sink->entity->pads)) {
		dev_info(csi_dev->dev, "capture node %s pad not connected\n",
			 vdev->name);
		return -ENOLINK;
	}

	ret = sun6i_csi_capture_link_validate_get_format(link->source,
							 &source_fmt);
	if (ret < 0)
		return ret;

	if (!sun6i_csi_is_format_supported(csi_dev,
					   capture->format.fmt.pix.pixelformat,
					   source_fmt.format.code)) {
		dev_err(csi_dev->dev,
			"Unsupported pixformat: 0x%x with mbus code: 0x%x!\n",
			capture->format.fmt.pix.pixelformat,
			source_fmt.format.code);
		return -EPIPE;
	}

	if (source_fmt.format.width != capture->format.fmt.pix.width ||
	    source_fmt.format.height != capture->format.fmt.pix.height) {
		dev_err(csi_dev->dev,
			"Wrong width or height %ux%u (%ux%u expected)\n",
			capture->format.fmt.pix.width,
			capture->format.fmt.pix.height,
			source_fmt.format.width, source_fmt.format.height);
		return -EPIPE;
	}

	return 0;
}

static const struct media_entity_operations sun6i_csi_capture_media_ops = {
	.link_validate = sun6i_csi_capture_link_validate
};

/* Capture */

int sun6i_csi_capture_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct sun6i_csi_capture_state *state = &capture->state;
	struct v4l2_device *v4l2_dev = &csi_dev->v4l2.v4l2_dev;
	struct v4l2_subdev *bridge_subdev = &csi_dev->bridge.subdev;
	struct video_device *video_dev = &capture->video_dev;
	struct vb2_queue *queue = &capture->queue;
	struct media_pad *pad = &capture->pad;
	struct v4l2_format *format = &csi_dev->capture.format;
	struct v4l2_pix_format *pix_format = &format->fmt.pix;
	int ret;

	/* State */

	INIT_LIST_HEAD(&state->queue);
	spin_lock_init(&state->lock);

	/* Media Entity */

	video_dev->entity.ops = &sun6i_csi_capture_media_ops;

	/* Media Pad */

	pad->flags = MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&video_dev->entity, 1, pad);
	if (ret < 0)
		return ret;

	/* Queue */

	mutex_init(&capture->lock);

	queue->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	queue->io_modes = VB2_MMAP | VB2_DMABUF;
	queue->buf_struct_size = sizeof(struct sun6i_csi_buffer);
	queue->ops = &sun6i_csi_capture_queue_ops;
	queue->mem_ops = &vb2_dma_contig_memops;
	queue->min_buffers_needed = 2;
	queue->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	queue->lock = &capture->lock;
	queue->dev = csi_dev->dev;
	queue->drv_priv = csi_dev;

	ret = vb2_queue_init(queue);
	if (ret) {
		v4l2_err(v4l2_dev, "failed to initialize vb2 queue: %d\n", ret);
		goto error_media_entity;
	}

	/* V4L2 Format */

	format->type = queue->type;
	pix_format->pixelformat = sun6i_csi_capture_formats[0].pixelformat;
	pix_format->width = 1280;
	pix_format->height = 720;
	pix_format->field = V4L2_FIELD_NONE;

	sun6i_csi_capture_format_prepare(format);

	/* Video Device */

	strscpy(video_dev->name, SUN6I_CSI_NAME, sizeof(video_dev->name));
	video_dev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	video_dev->vfl_dir = VFL_DIR_RX;
	video_dev->release = video_device_release_empty;
	video_dev->fops = &sun6i_csi_capture_fops;
	video_dev->ioctl_ops = &sun6i_csi_capture_ioctl_ops;
	video_dev->v4l2_dev = v4l2_dev;
	video_dev->queue = queue;
	video_dev->lock = &capture->lock;

	video_set_drvdata(video_dev, csi_dev);

	ret = video_register_device(video_dev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to register video device: %d\n",
			 ret);
		goto error_media_entity;
	}

	/* Media Pad Link */

	ret = media_create_pad_link(&bridge_subdev->entity,
				    SUN6I_CSI_BRIDGE_PAD_SOURCE,
				    &video_dev->entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "failed to create %s:%u -> %s:%u link\n",
			 bridge_subdev->entity.name,
			 SUN6I_CSI_BRIDGE_PAD_SOURCE,
			 video_dev->entity.name, 0);
		goto error_video_device;
	}

	return 0;

error_video_device:
	vb2_video_unregister_device(video_dev);

error_media_entity:
	media_entity_cleanup(&video_dev->entity);

	mutex_destroy(&capture->lock);

	return ret;
}

void sun6i_csi_capture_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_capture *capture = &csi_dev->capture;
	struct video_device *video_dev = &capture->video_dev;

	vb2_video_unregister_device(video_dev);
	media_entity_cleanup(&video_dev->entity);
	mutex_destroy(&capture->lock);
}
