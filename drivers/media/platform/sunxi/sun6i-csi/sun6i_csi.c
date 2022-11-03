// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <media/v4l2-mc.h>

#include "sun6i_csi.h"
#include "sun6i_csi_reg.h"

/* Helpers */

/* TODO add 10&12 bit YUV, RGB support */
bool sun6i_csi_is_format_supported(struct sun6i_csi_device *csi_dev,
				   u32 pixformat, u32 mbus_code)
{
	struct v4l2_fwnode_endpoint *endpoint =
		&csi_dev->bridge.source_parallel.endpoint;

	/*
	 * Some video receivers have the ability to be compatible with
	 * 8bit and 16bit bus width.
	 * Identify the media bus format from device tree.
	 */
	if ((endpoint->bus_type == V4L2_MBUS_PARALLEL
	     || endpoint->bus_type == V4L2_MBUS_BT656)
	     && endpoint->bus.parallel.bus_width == 16) {
		switch (pixformat) {
		case V4L2_PIX_FMT_NV12_16L16:
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
		case V4L2_PIX_FMT_YUV422P:
			switch (mbus_code) {
			case MEDIA_BUS_FMT_UYVY8_1X16:
			case MEDIA_BUS_FMT_VYUY8_1X16:
			case MEDIA_BUS_FMT_YUYV8_1X16:
			case MEDIA_BUS_FMT_YVYU8_1X16:
				return true;
			default:
				dev_dbg(csi_dev->dev,
					"Unsupported mbus code: 0x%x\n",
					mbus_code);
				break;
			}
			break;
		default:
			dev_dbg(csi_dev->dev, "Unsupported pixformat: 0x%x\n",
				pixformat);
			break;
		}
		return false;
	}

	switch (pixformat) {
	case V4L2_PIX_FMT_SBGGR8:
		return (mbus_code == MEDIA_BUS_FMT_SBGGR8_1X8);
	case V4L2_PIX_FMT_SGBRG8:
		return (mbus_code == MEDIA_BUS_FMT_SGBRG8_1X8);
	case V4L2_PIX_FMT_SGRBG8:
		return (mbus_code == MEDIA_BUS_FMT_SGRBG8_1X8);
	case V4L2_PIX_FMT_SRGGB8:
		return (mbus_code == MEDIA_BUS_FMT_SRGGB8_1X8);
	case V4L2_PIX_FMT_SBGGR10:
		return (mbus_code == MEDIA_BUS_FMT_SBGGR10_1X10);
	case V4L2_PIX_FMT_SGBRG10:
		return (mbus_code == MEDIA_BUS_FMT_SGBRG10_1X10);
	case V4L2_PIX_FMT_SGRBG10:
		return (mbus_code == MEDIA_BUS_FMT_SGRBG10_1X10);
	case V4L2_PIX_FMT_SRGGB10:
		return (mbus_code == MEDIA_BUS_FMT_SRGGB10_1X10);
	case V4L2_PIX_FMT_SBGGR12:
		return (mbus_code == MEDIA_BUS_FMT_SBGGR12_1X12);
	case V4L2_PIX_FMT_SGBRG12:
		return (mbus_code == MEDIA_BUS_FMT_SGBRG12_1X12);
	case V4L2_PIX_FMT_SGRBG12:
		return (mbus_code == MEDIA_BUS_FMT_SGRBG12_1X12);
	case V4L2_PIX_FMT_SRGGB12:
		return (mbus_code == MEDIA_BUS_FMT_SRGGB12_1X12);

	case V4L2_PIX_FMT_YUYV:
		return (mbus_code == MEDIA_BUS_FMT_YUYV8_2X8);
	case V4L2_PIX_FMT_YVYU:
		return (mbus_code == MEDIA_BUS_FMT_YVYU8_2X8);
	case V4L2_PIX_FMT_UYVY:
		return (mbus_code == MEDIA_BUS_FMT_UYVY8_2X8);
	case V4L2_PIX_FMT_VYUY:
		return (mbus_code == MEDIA_BUS_FMT_VYUY8_2X8);

	case V4L2_PIX_FMT_NV12_16L16:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_YUV422P:
		switch (mbus_code) {
		case MEDIA_BUS_FMT_UYVY8_2X8:
		case MEDIA_BUS_FMT_VYUY8_2X8:
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_YVYU8_2X8:
			return true;
		default:
			dev_dbg(csi_dev->dev, "Unsupported mbus code: 0x%x\n",
				mbus_code);
			break;
		}
		break;

	case V4L2_PIX_FMT_RGB565:
		return (mbus_code == MEDIA_BUS_FMT_RGB565_2X8_LE);
	case V4L2_PIX_FMT_RGB565X:
		return (mbus_code == MEDIA_BUS_FMT_RGB565_2X8_BE);

	case V4L2_PIX_FMT_JPEG:
		return (mbus_code == MEDIA_BUS_FMT_JPEG_1X8);

	default:
		dev_dbg(csi_dev->dev, "Unsupported pixformat: 0x%x\n",
			pixformat);
		break;
	}

	return false;
}

int sun6i_csi_set_power(struct sun6i_csi_device *csi_dev, bool enable)
{
	struct device *dev = csi_dev->dev;
	struct regmap *regmap = csi_dev->regmap;
	int ret;

	if (!enable) {
		regmap_update_bits(regmap, SUN6I_CSI_EN_REG,
				   SUN6I_CSI_EN_CSI_EN, 0);
		pm_runtime_put(dev);

		return 0;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	regmap_update_bits(regmap, SUN6I_CSI_EN_REG, SUN6I_CSI_EN_CSI_EN,
			   SUN6I_CSI_EN_CSI_EN);

	return 0;
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

static void sun6i_csi_setup_bus(struct sun6i_csi_device *csi_dev)
{
	struct v4l2_fwnode_endpoint *endpoint =
		&csi_dev->bridge.source_parallel.endpoint;
	struct sun6i_csi_config *config = &csi_dev->config;
	unsigned char bus_width;
	u32 flags;
	u32 cfg = 0;
	bool input_interlaced = false;

	if (config->field == V4L2_FIELD_INTERLACED
	    || config->field == V4L2_FIELD_INTERLACED_TB
	    || config->field == V4L2_FIELD_INTERLACED_BT)
		input_interlaced = true;

	bus_width = endpoint->bus.parallel.bus_width;

	if (input_interlaced)
		cfg |= SUN6I_CSI_IF_CFG_SRC_TYPE_INTERLACED |
		       SUN6I_CSI_IF_CFG_FIELD_DT_PCLK_SHIFT(1) |
		       SUN6I_CSI_IF_CFG_FIELD_DT_FIELD_VSYNC;
	else
		cfg |= SUN6I_CSI_IF_CFG_SRC_TYPE_PROGRESSIVE;

	switch (endpoint->bus_type) {
	case V4L2_MBUS_PARALLEL:
		cfg |= SUN6I_CSI_IF_CFG_IF_CSI;

		flags = endpoint->bus.parallel.flags;

		if (bus_width == 16)
			cfg |= SUN6I_CSI_IF_CFG_IF_CSI_YUV_COMBINED;
		else
			cfg |= SUN6I_CSI_IF_CFG_IF_CSI_YUV_RAW;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			cfg |= SUN6I_CSI_IF_CFG_FIELD_NEGATIVE;
		else
			cfg |= SUN6I_CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			cfg |= SUN6I_CSI_IF_CFG_VREF_POL_NEGATIVE;
		else
			cfg |= SUN6I_CSI_IF_CFG_VREF_POL_POSITIVE;

		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			cfg |= SUN6I_CSI_IF_CFG_HREF_POL_NEGATIVE;
		else
			cfg |= SUN6I_CSI_IF_CFG_HREF_POL_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			cfg |= SUN6I_CSI_IF_CFG_CLK_POL_RISING;
		else
			cfg |= SUN6I_CSI_IF_CFG_CLK_POL_FALLING;
		break;
	case V4L2_MBUS_BT656:
		cfg |= SUN6I_CSI_IF_CFG_IF_CSI;

		flags = endpoint->bus.parallel.flags;

		if (bus_width == 16)
			cfg |= SUN6I_CSI_IF_CFG_IF_CSI_BT1120;
		else
			cfg |= SUN6I_CSI_IF_CFG_IF_CSI_BT656;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			cfg |= SUN6I_CSI_IF_CFG_FIELD_NEGATIVE;
		else
			cfg |= SUN6I_CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			cfg |= SUN6I_CSI_IF_CFG_CLK_POL_RISING;
		else
			cfg |= SUN6I_CSI_IF_CFG_CLK_POL_FALLING;
		break;
	default:
		dev_warn(csi_dev->dev, "Unsupported bus type: %d\n",
			 endpoint->bus_type);
		break;
	}

	switch (bus_width) {
	case 8:
		cfg |= SUN6I_CSI_IF_CFG_DATA_WIDTH_8;
		break;
	case 10:
		cfg |= SUN6I_CSI_IF_CFG_DATA_WIDTH_10;
		break;
	case 12:
		cfg |= SUN6I_CSI_IF_CFG_DATA_WIDTH_12;
		break;
	case 16: /* No need to configure DATA_WIDTH for 16bit */
		break;
	default:
		dev_warn(csi_dev->dev, "Unsupported bus width: %u\n", bus_width);
		break;
	}

	regmap_write(csi_dev->regmap, SUN6I_CSI_IF_CFG_REG, cfg);
}

static void sun6i_csi_set_format(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_config *config = &csi_dev->config;
	u32 cfg = 0;
	u32 val;

	val = get_csi_input_format(csi_dev, config->code,
				   config->pixelformat);
	cfg |= SUN6I_CSI_CH_CFG_INPUT_FMT(val);

	val = get_csi_output_format(csi_dev, config->pixelformat,
				    config->field);
	cfg |= SUN6I_CSI_CH_CFG_OUTPUT_FMT(val);

	val = get_csi_input_seq(csi_dev, config->code,
				config->pixelformat);
	cfg |= SUN6I_CSI_CH_CFG_INPUT_YUV_SEQ(val);

	if (config->field == V4L2_FIELD_TOP)
		cfg |= SUN6I_CSI_CH_CFG_FIELD_SEL_FIELD0;
	else if (config->field == V4L2_FIELD_BOTTOM)
		cfg |= SUN6I_CSI_CH_CFG_FIELD_SEL_FIELD1;
	else
		cfg |= SUN6I_CSI_CH_CFG_FIELD_SEL_EITHER;

	regmap_write(csi_dev->regmap, SUN6I_CSI_CH_CFG_REG, cfg);
}

static void sun6i_csi_set_window(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_config *config = &csi_dev->config;
	u32 bytesperline_y;
	u32 bytesperline_c;
	u32 width = config->width;
	u32 height = config->height;
	u32 hor_len = width;

	switch (config->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		dev_dbg(csi_dev->dev,
			"Horizontal length should be 2 times of width for packed YUV formats!\n");
		hor_len = width * 2;
		break;
	default:
		break;
	}

	regmap_write(csi_dev->regmap, SUN6I_CSI_CH_HSIZE_REG,
		     SUN6I_CSI_CH_HSIZE_LEN(hor_len) |
		     SUN6I_CSI_CH_HSIZE_START(0));
	regmap_write(csi_dev->regmap, SUN6I_CSI_CH_VSIZE_REG,
		     SUN6I_CSI_CH_VSIZE_LEN(height) |
		     SUN6I_CSI_CH_VSIZE_START(0));

	switch (config->pixelformat) {
	case V4L2_PIX_FMT_NV12_16L16:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		bytesperline_y = width;
		bytesperline_c = width;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		bytesperline_y = width;
		bytesperline_c = width / 2;
		break;
	case V4L2_PIX_FMT_YUV422P:
		bytesperline_y = width;
		bytesperline_c = width / 2;
		break;
	default: /* raw */
		dev_dbg(csi_dev->dev,
			"Calculating pixelformat(0x%x)'s bytesperline as a packed format\n",
			config->pixelformat);
		bytesperline_y = (sun6i_csi_get_bpp(config->pixelformat) *
				  config->width) / 8;
		bytesperline_c = 0;
		break;
	}

	regmap_write(csi_dev->regmap, SUN6I_CSI_CH_BUF_LEN_REG,
		     SUN6I_CSI_CH_BUF_LEN_CHROMA_LINE(bytesperline_c) |
		     SUN6I_CSI_CH_BUF_LEN_LUMA_LINE(bytesperline_y));
}

int sun6i_csi_update_config(struct sun6i_csi_device *csi_dev,
			    struct sun6i_csi_config *config)
{
	if (!config)
		return -EINVAL;

	memcpy(&csi_dev->config, config, sizeof(csi_dev->config));

	sun6i_csi_setup_bus(csi_dev);
	sun6i_csi_set_format(csi_dev);
	sun6i_csi_set_window(csi_dev);

	return 0;
}

/* Media */

static const struct media_device_ops sun6i_csi_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

/* V4L2 */

static int sun6i_csi_v4l2_setup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_v4l2 *v4l2 = &csi_dev->v4l2;
	struct media_device *media_dev = &v4l2->media_dev;
	struct v4l2_device *v4l2_dev = &v4l2->v4l2_dev;
	struct device *dev = csi_dev->dev;
	int ret;

	/* Media Device */

	strscpy(media_dev->model, SUN6I_CSI_DESCRIPTION,
		sizeof(media_dev->model));
	media_dev->hw_revision = 0;
	media_dev->ops = &sun6i_csi_media_ops;
	media_dev->dev = dev;

	media_device_init(media_dev);

	ret = media_device_register(media_dev);
	if (ret) {
		dev_err(dev, "failed to register media device: %d\n", ret);
		goto error_media;
	}

	/* V4L2 Device */

	v4l2_dev->mdev = media_dev;

	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register v4l2 device: %d\n", ret);
		goto error_media;
	}

	return 0;

error_media:
	media_device_unregister(media_dev);
	media_device_cleanup(media_dev);

	return ret;
}

static void sun6i_csi_v4l2_cleanup(struct sun6i_csi_device *csi_dev)
{
	struct sun6i_csi_v4l2 *v4l2 = &csi_dev->v4l2;

	media_device_unregister(&v4l2->media_dev);
	v4l2_device_unregister(&v4l2->v4l2_dev);
	media_device_cleanup(&v4l2->media_dev);
}

/* Platform */

static irqreturn_t sun6i_csi_interrupt(int irq, void *private)
{
	struct sun6i_csi_device *csi_dev = private;
	struct regmap *regmap = csi_dev->regmap;
	u32 status;

	regmap_read(regmap, SUN6I_CSI_CH_INT_STA_REG, &status);

	if (!(status & 0xFF))
		return IRQ_NONE;

	if ((status & SUN6I_CSI_CH_INT_STA_FIFO0_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_FIFO1_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_FIFO2_OF) ||
	    (status & SUN6I_CSI_CH_INT_STA_HB_OF)) {
		regmap_write(regmap, SUN6I_CSI_CH_INT_STA_REG, status);

		regmap_update_bits(regmap, SUN6I_CSI_EN_REG,
				   SUN6I_CSI_EN_CSI_EN, 0);
		regmap_update_bits(regmap, SUN6I_CSI_EN_REG,
				   SUN6I_CSI_EN_CSI_EN, SUN6I_CSI_EN_CSI_EN);
		return IRQ_HANDLED;
	}

	if (status & SUN6I_CSI_CH_INT_STA_FD)
		sun6i_csi_capture_frame_done(csi_dev);

	if (status & SUN6I_CSI_CH_INT_STA_VS)
		sun6i_csi_capture_sync(csi_dev);

	regmap_write(regmap, SUN6I_CSI_CH_INT_STA_REG, status);

	return IRQ_HANDLED;
}

static int sun6i_csi_suspend(struct device *dev)
{
	struct sun6i_csi_device *csi_dev = dev_get_drvdata(dev);

	reset_control_assert(csi_dev->reset);
	clk_disable_unprepare(csi_dev->clock_ram);
	clk_disable_unprepare(csi_dev->clock_mod);

	return 0;
}

static int sun6i_csi_resume(struct device *dev)
{
	struct sun6i_csi_device *csi_dev = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(csi_dev->reset);
	if (ret) {
		dev_err(dev, "failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(csi_dev->clock_mod);
	if (ret) {
		dev_err(dev, "failed to enable module clock\n");
		goto error_reset;
	}

	ret = clk_prepare_enable(csi_dev->clock_ram);
	if (ret) {
		dev_err(dev, "failed to enable ram clock\n");
		goto error_clock_mod;
	}

	return 0;

error_clock_mod:
	clk_disable_unprepare(csi_dev->clock_mod);

error_reset:
	reset_control_assert(csi_dev->reset);

	return ret;
}

static const struct dev_pm_ops sun6i_csi_pm_ops = {
	.runtime_suspend	= sun6i_csi_suspend,
	.runtime_resume		= sun6i_csi_resume,
};

static const struct regmap_config sun6i_csi_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x9c,
};

static int sun6i_csi_resources_setup(struct sun6i_csi_device *csi_dev,
				     struct platform_device *platform_dev)
{
	struct device *dev = csi_dev->dev;
	const struct sun6i_csi_variant *variant;
	void __iomem *io_base;
	int ret;
	int irq;

	variant = of_device_get_match_data(dev);
	if (!variant)
		return -EINVAL;

	/* Registers */

	io_base = devm_platform_ioremap_resource(platform_dev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	csi_dev->regmap = devm_regmap_init_mmio_clk(dev, "bus", io_base,
						    &sun6i_csi_regmap_config);
	if (IS_ERR(csi_dev->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(csi_dev->regmap);
	}

	/* Clocks */

	csi_dev->clock_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(csi_dev->clock_mod)) {
		dev_err(dev, "failed to acquire module clock\n");
		return PTR_ERR(csi_dev->clock_mod);
	}

	csi_dev->clock_ram = devm_clk_get(dev, "ram");
	if (IS_ERR(csi_dev->clock_ram)) {
		dev_err(dev, "failed to acquire ram clock\n");
		return PTR_ERR(csi_dev->clock_ram);
	}

	ret = clk_set_rate_exclusive(csi_dev->clock_mod,
				     variant->clock_mod_rate);
	if (ret) {
		dev_err(dev, "failed to set mod clock rate\n");
		return ret;
	}

	/* Reset */

	csi_dev->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(csi_dev->reset)) {
		dev_err(dev, "failed to acquire reset\n");
		ret = PTR_ERR(csi_dev->reset);
		goto error_clock_rate_exclusive;
	}

	/* Interrupt */

	irq = platform_get_irq(platform_dev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto error_clock_rate_exclusive;
	}

	ret = devm_request_irq(dev, irq, sun6i_csi_interrupt, 0, SUN6I_CSI_NAME,
			       csi_dev);
	if (ret) {
		dev_err(dev, "failed to request interrupt\n");
		goto error_clock_rate_exclusive;
	}

	/* Runtime PM */

	pm_runtime_enable(dev);

	return 0;

error_clock_rate_exclusive:
	clk_rate_exclusive_put(csi_dev->clock_mod);

	return ret;
}

static void sun6i_csi_resources_cleanup(struct sun6i_csi_device *csi_dev)
{
	pm_runtime_disable(csi_dev->dev);
	clk_rate_exclusive_put(csi_dev->clock_mod);
}

static int sun6i_csi_probe(struct platform_device *platform_dev)
{
	struct sun6i_csi_device *csi_dev;
	struct device *dev = &platform_dev->dev;
	int ret;

	csi_dev = devm_kzalloc(dev, sizeof(*csi_dev), GFP_KERNEL);
	if (!csi_dev)
		return -ENOMEM;

	csi_dev->dev = &platform_dev->dev;
	platform_set_drvdata(platform_dev, csi_dev);

	ret = sun6i_csi_resources_setup(csi_dev, platform_dev);
	if (ret)
		return ret;

	ret = sun6i_csi_v4l2_setup(csi_dev);
	if (ret)
		goto error_resources;

	ret = sun6i_csi_bridge_setup(csi_dev);
	if (ret)
		goto error_v4l2;

	ret = sun6i_csi_capture_setup(csi_dev);
	if (ret)
		goto error_bridge;

	return 0;

error_bridge:
	sun6i_csi_bridge_cleanup(csi_dev);

error_v4l2:
	sun6i_csi_v4l2_cleanup(csi_dev);

error_resources:
	sun6i_csi_resources_cleanup(csi_dev);

	return ret;
}

static int sun6i_csi_remove(struct platform_device *pdev)
{
	struct sun6i_csi_device *csi_dev = platform_get_drvdata(pdev);

	sun6i_csi_capture_cleanup(csi_dev);
	sun6i_csi_bridge_cleanup(csi_dev);
	sun6i_csi_v4l2_cleanup(csi_dev);
	sun6i_csi_resources_cleanup(csi_dev);

	return 0;
}

static const struct sun6i_csi_variant sun6i_a31_csi_variant = {
	.clock_mod_rate	= 297000000,
};

static const struct sun6i_csi_variant sun50i_a64_csi_variant = {
	.clock_mod_rate	= 300000000,
};

static const struct of_device_id sun6i_csi_of_match[] = {
	{
		.compatible	= "allwinner,sun6i-a31-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-a83t-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-h3-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun8i-v3s-csi",
		.data		= &sun6i_a31_csi_variant,
	},
	{
		.compatible	= "allwinner,sun50i-a64-csi",
		.data		= &sun50i_a64_csi_variant,
	},
	{},
};

MODULE_DEVICE_TABLE(of, sun6i_csi_of_match);

static struct platform_driver sun6i_csi_platform_driver = {
	.probe	= sun6i_csi_probe,
	.remove	= sun6i_csi_remove,
	.driver	= {
		.name		= SUN6I_CSI_NAME,
		.of_match_table	= of_match_ptr(sun6i_csi_of_match),
		.pm		= &sun6i_csi_pm_ops,
	},
};

module_platform_driver(sun6i_csi_platform_driver);

MODULE_DESCRIPTION("Allwinner A31 Camera Sensor Interface driver");
MODULE_AUTHOR("Yong Deng <yong.deng@magewell.com>");
MODULE_LICENSE("GPL");
