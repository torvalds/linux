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

#include "sun6i_csi.h"
#include "sun6i_csi_reg.h"

#define MODULE_NAME	"sun6i-csi"

struct sun6i_csi_dev {
	struct sun6i_csi		csi;
	struct device			*dev;

	struct regmap			*regmap;
	struct clk			*clk_mod;
	struct clk			*clk_ram;
	struct reset_control		*rstc_bus;

	int				planar_offset[3];
};

static inline struct sun6i_csi_dev *sun6i_csi_to_dev(struct sun6i_csi *csi)
{
	return container_of(csi, struct sun6i_csi_dev, csi);
}

/* TODO add 10&12 bit YUV, RGB support */
bool sun6i_csi_is_format_supported(struct sun6i_csi *csi,
				   u32 pixformat, u32 mbus_code)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);

	/*
	 * Some video receivers have the ability to be compatible with
	 * 8bit and 16bit bus width.
	 * Identify the media bus format from device tree.
	 */
	if ((sdev->csi.v4l2_ep.bus_type == V4L2_MBUS_PARALLEL
	     || sdev->csi.v4l2_ep.bus_type == V4L2_MBUS_BT656)
	     && sdev->csi.v4l2_ep.bus.parallel.bus_width == 16) {
		switch (pixformat) {
		case V4L2_PIX_FMT_HM12:
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
				dev_dbg(sdev->dev, "Unsupported mbus code: 0x%x\n",
					mbus_code);
				break;
			}
			break;
		default:
			dev_dbg(sdev->dev, "Unsupported pixformat: 0x%x\n",
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

	case V4L2_PIX_FMT_HM12:
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
			dev_dbg(sdev->dev, "Unsupported mbus code: 0x%x\n",
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
		dev_dbg(sdev->dev, "Unsupported pixformat: 0x%x\n", pixformat);
		break;
	}

	return false;
}

int sun6i_csi_set_power(struct sun6i_csi *csi, bool enable)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);
	struct device *dev = sdev->dev;
	struct regmap *regmap = sdev->regmap;
	int ret;

	if (!enable) {
		regmap_update_bits(regmap, CSI_EN_REG, CSI_EN_CSI_EN, 0);

		clk_disable_unprepare(sdev->clk_ram);
		if (of_device_is_compatible(dev->of_node,
					    "allwinner,sun50i-a64-csi"))
			clk_rate_exclusive_put(sdev->clk_mod);
		clk_disable_unprepare(sdev->clk_mod);
		reset_control_assert(sdev->rstc_bus);
		return 0;
	}

	ret = clk_prepare_enable(sdev->clk_mod);
	if (ret) {
		dev_err(sdev->dev, "Enable csi clk err %d\n", ret);
		return ret;
	}

	if (of_device_is_compatible(dev->of_node, "allwinner,sun50i-a64-csi"))
		clk_set_rate_exclusive(sdev->clk_mod, 300000000);

	ret = clk_prepare_enable(sdev->clk_ram);
	if (ret) {
		dev_err(sdev->dev, "Enable clk_dram_csi clk err %d\n", ret);
		goto clk_mod_disable;
	}

	ret = reset_control_deassert(sdev->rstc_bus);
	if (ret) {
		dev_err(sdev->dev, "reset err %d\n", ret);
		goto clk_ram_disable;
	}

	regmap_update_bits(regmap, CSI_EN_REG, CSI_EN_CSI_EN, CSI_EN_CSI_EN);

	return 0;

clk_ram_disable:
	clk_disable_unprepare(sdev->clk_ram);
clk_mod_disable:
	if (of_device_is_compatible(dev->of_node, "allwinner,sun50i-a64-csi"))
		clk_rate_exclusive_put(sdev->clk_mod);
	clk_disable_unprepare(sdev->clk_mod);
	return ret;
}

static enum csi_input_fmt get_csi_input_format(struct sun6i_csi_dev *sdev,
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
	dev_dbg(sdev->dev, "Select YUV422 as default input format of CSI.\n");
	return CSI_INPUT_FORMAT_YUV422;
}

static enum csi_output_fmt get_csi_output_format(struct sun6i_csi_dev *sdev,
						 u32 pixformat, u32 field)
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

	case V4L2_PIX_FMT_HM12:
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
		dev_warn(sdev->dev, "Unsupported pixformat: 0x%x\n", pixformat);
		break;
	}

	return CSI_FIELD_RAW_8;
}

static enum csi_input_seq get_csi_input_seq(struct sun6i_csi_dev *sdev,
					    u32 mbus_code, u32 pixformat)
{
	/* Input sequence does not apply to non-YUV formats */
	if ((mbus_code & 0xF000) != 0x2000)
		return 0;

	switch (pixformat) {
	case V4L2_PIX_FMT_HM12:
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
			dev_warn(sdev->dev, "Unsupported mbus code: 0x%x\n",
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
			dev_warn(sdev->dev, "Unsupported mbus code: 0x%x\n",
				 mbus_code);
			break;
		}
		break;

	case V4L2_PIX_FMT_YUYV:
		return CSI_INPUT_SEQ_YUYV;

	default:
		dev_warn(sdev->dev, "Unsupported pixformat: 0x%x, defaulting to YUYV\n",
			 pixformat);
		break;
	}

	return CSI_INPUT_SEQ_YUYV;
}

static void sun6i_csi_setup_bus(struct sun6i_csi_dev *sdev)
{
	struct v4l2_fwnode_endpoint *endpoint = &sdev->csi.v4l2_ep;
	struct sun6i_csi *csi = &sdev->csi;
	unsigned char bus_width;
	u32 flags;
	u32 cfg;
	bool input_interlaced = false;

	if (csi->config.field == V4L2_FIELD_INTERLACED
	    || csi->config.field == V4L2_FIELD_INTERLACED_TB
	    || csi->config.field == V4L2_FIELD_INTERLACED_BT)
		input_interlaced = true;

	bus_width = endpoint->bus.parallel.bus_width;

	regmap_read(sdev->regmap, CSI_IF_CFG_REG, &cfg);

	cfg &= ~(CSI_IF_CFG_CSI_IF_MASK | CSI_IF_CFG_MIPI_IF_MASK |
		 CSI_IF_CFG_IF_DATA_WIDTH_MASK |
		 CSI_IF_CFG_CLK_POL_MASK | CSI_IF_CFG_VREF_POL_MASK |
		 CSI_IF_CFG_HREF_POL_MASK | CSI_IF_CFG_FIELD_MASK |
		 CSI_IF_CFG_SRC_TYPE_MASK);

	if (input_interlaced)
		cfg |= CSI_IF_CFG_SRC_TYPE_INTERLACED;
	else
		cfg |= CSI_IF_CFG_SRC_TYPE_PROGRESSED;

	switch (endpoint->bus_type) {
	case V4L2_MBUS_PARALLEL:
		cfg |= CSI_IF_CFG_MIPI_IF_CSI;

		flags = endpoint->bus.parallel.flags;

		cfg |= (bus_width == 16) ? CSI_IF_CFG_CSI_IF_YUV422_16BIT :
					   CSI_IF_CFG_CSI_IF_YUV422_INTLV;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			cfg |= CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_VSYNC_ACTIVE_LOW)
			cfg |= CSI_IF_CFG_VREF_POL_POSITIVE;
		if (flags & V4L2_MBUS_HSYNC_ACTIVE_LOW)
			cfg |= CSI_IF_CFG_HREF_POL_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_RISING)
			cfg |= CSI_IF_CFG_CLK_POL_FALLING_EDGE;
		break;
	case V4L2_MBUS_BT656:
		cfg |= CSI_IF_CFG_MIPI_IF_CSI;

		flags = endpoint->bus.parallel.flags;

		cfg |= (bus_width == 16) ? CSI_IF_CFG_CSI_IF_BT1120 :
					   CSI_IF_CFG_CSI_IF_BT656;

		if (flags & V4L2_MBUS_FIELD_EVEN_LOW)
			cfg |= CSI_IF_CFG_FIELD_POSITIVE;

		if (flags & V4L2_MBUS_PCLK_SAMPLE_FALLING)
			cfg |= CSI_IF_CFG_CLK_POL_FALLING_EDGE;
		break;
	default:
		dev_warn(sdev->dev, "Unsupported bus type: %d\n",
			 endpoint->bus_type);
		break;
	}

	switch (bus_width) {
	case 8:
		cfg |= CSI_IF_CFG_IF_DATA_WIDTH_8BIT;
		break;
	case 10:
		cfg |= CSI_IF_CFG_IF_DATA_WIDTH_10BIT;
		break;
	case 12:
		cfg |= CSI_IF_CFG_IF_DATA_WIDTH_12BIT;
		break;
	case 16: /* No need to configure DATA_WIDTH for 16bit */
		break;
	default:
		dev_warn(sdev->dev, "Unsupported bus width: %u\n", bus_width);
		break;
	}

	regmap_write(sdev->regmap, CSI_IF_CFG_REG, cfg);
}

static void sun6i_csi_set_format(struct sun6i_csi_dev *sdev)
{
	struct sun6i_csi *csi = &sdev->csi;
	u32 cfg;
	u32 val;

	regmap_read(sdev->regmap, CSI_CH_CFG_REG, &cfg);

	cfg &= ~(CSI_CH_CFG_INPUT_FMT_MASK |
		 CSI_CH_CFG_OUTPUT_FMT_MASK | CSI_CH_CFG_VFLIP_EN |
		 CSI_CH_CFG_HFLIP_EN | CSI_CH_CFG_FIELD_SEL_MASK |
		 CSI_CH_CFG_INPUT_SEQ_MASK);

	val = get_csi_input_format(sdev, csi->config.code,
				   csi->config.pixelformat);
	cfg |= CSI_CH_CFG_INPUT_FMT(val);

	val = get_csi_output_format(sdev, csi->config.pixelformat,
				    csi->config.field);
	cfg |= CSI_CH_CFG_OUTPUT_FMT(val);

	val = get_csi_input_seq(sdev, csi->config.code,
				csi->config.pixelformat);
	cfg |= CSI_CH_CFG_INPUT_SEQ(val);

	if (csi->config.field == V4L2_FIELD_TOP)
		cfg |= CSI_CH_CFG_FIELD_SEL_FIELD0;
	else if (csi->config.field == V4L2_FIELD_BOTTOM)
		cfg |= CSI_CH_CFG_FIELD_SEL_FIELD1;
	else
		cfg |= CSI_CH_CFG_FIELD_SEL_BOTH;

	regmap_write(sdev->regmap, CSI_CH_CFG_REG, cfg);
}

static void sun6i_csi_set_window(struct sun6i_csi_dev *sdev)
{
	struct sun6i_csi_config *config = &sdev->csi.config;
	u32 bytesperline_y;
	u32 bytesperline_c;
	int *planar_offset = sdev->planar_offset;
	u32 width = config->width;
	u32 height = config->height;
	u32 hor_len = width;

	switch (config->pixelformat) {
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
		dev_dbg(sdev->dev,
			"Horizontal length should be 2 times of width for packed YUV formats!\n");
		hor_len = width * 2;
		break;
	default:
		break;
	}

	regmap_write(sdev->regmap, CSI_CH_HSIZE_REG,
		     CSI_CH_HSIZE_HOR_LEN(hor_len) |
		     CSI_CH_HSIZE_HOR_START(0));
	regmap_write(sdev->regmap, CSI_CH_VSIZE_REG,
		     CSI_CH_VSIZE_VER_LEN(height) |
		     CSI_CH_VSIZE_VER_START(0));

	planar_offset[0] = 0;
	switch (config->pixelformat) {
	case V4L2_PIX_FMT_HM12:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		bytesperline_y = width;
		bytesperline_c = width;
		planar_offset[1] = bytesperline_y * height;
		planar_offset[2] = -1;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		bytesperline_y = width;
		bytesperline_c = width / 2;
		planar_offset[1] = bytesperline_y * height;
		planar_offset[2] = planar_offset[1] +
				bytesperline_c * height / 2;
		break;
	case V4L2_PIX_FMT_YUV422P:
		bytesperline_y = width;
		bytesperline_c = width / 2;
		planar_offset[1] = bytesperline_y * height;
		planar_offset[2] = planar_offset[1] +
				bytesperline_c * height;
		break;
	default: /* raw */
		dev_dbg(sdev->dev,
			"Calculating pixelformat(0x%x)'s bytesperline as a packed format\n",
			config->pixelformat);
		bytesperline_y = (sun6i_csi_get_bpp(config->pixelformat) *
				  config->width) / 8;
		bytesperline_c = 0;
		planar_offset[1] = -1;
		planar_offset[2] = -1;
		break;
	}

	regmap_write(sdev->regmap, CSI_CH_BUF_LEN_REG,
		     CSI_CH_BUF_LEN_BUF_LEN_C(bytesperline_c) |
		     CSI_CH_BUF_LEN_BUF_LEN_Y(bytesperline_y));
}

int sun6i_csi_update_config(struct sun6i_csi *csi,
			    struct sun6i_csi_config *config)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);

	if (!config)
		return -EINVAL;

	memcpy(&csi->config, config, sizeof(csi->config));

	sun6i_csi_setup_bus(sdev);
	sun6i_csi_set_format(sdev);
	sun6i_csi_set_window(sdev);

	return 0;
}

void sun6i_csi_update_buf_addr(struct sun6i_csi *csi, dma_addr_t addr)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);

	regmap_write(sdev->regmap, CSI_CH_F0_BUFA_REG,
		     (addr + sdev->planar_offset[0]) >> 2);
	if (sdev->planar_offset[1] != -1)
		regmap_write(sdev->regmap, CSI_CH_F1_BUFA_REG,
			     (addr + sdev->planar_offset[1]) >> 2);
	if (sdev->planar_offset[2] != -1)
		regmap_write(sdev->regmap, CSI_CH_F2_BUFA_REG,
			     (addr + sdev->planar_offset[2]) >> 2);
}

void sun6i_csi_set_stream(struct sun6i_csi *csi, bool enable)
{
	struct sun6i_csi_dev *sdev = sun6i_csi_to_dev(csi);
	struct regmap *regmap = sdev->regmap;

	if (!enable) {
		regmap_update_bits(regmap, CSI_CAP_REG, CSI_CAP_CH0_VCAP_ON, 0);
		regmap_write(regmap, CSI_CH_INT_EN_REG, 0);
		return;
	}

	regmap_write(regmap, CSI_CH_INT_STA_REG, 0xFF);
	regmap_write(regmap, CSI_CH_INT_EN_REG,
		     CSI_CH_INT_EN_HB_OF_INT_EN |
		     CSI_CH_INT_EN_FIFO2_OF_INT_EN |
		     CSI_CH_INT_EN_FIFO1_OF_INT_EN |
		     CSI_CH_INT_EN_FIFO0_OF_INT_EN |
		     CSI_CH_INT_EN_FD_INT_EN |
		     CSI_CH_INT_EN_CD_INT_EN);

	regmap_update_bits(regmap, CSI_CAP_REG, CSI_CAP_CH0_VCAP_ON,
			   CSI_CAP_CH0_VCAP_ON);
}

/* -----------------------------------------------------------------------------
 * Media Controller and V4L2
 */
static int sun6i_csi_link_entity(struct sun6i_csi *csi,
				 struct media_entity *entity,
				 struct fwnode_handle *fwnode)
{
	struct media_entity *sink;
	struct media_pad *sink_pad;
	int src_pad_index;
	int ret;

	ret = media_entity_get_fwnode_pad(entity, fwnode, MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(csi->dev, "%s: no source pad in external entity %s\n",
			__func__, entity->name);
		return -EINVAL;
	}

	src_pad_index = ret;

	sink = &csi->video.vdev.entity;
	sink_pad = &csi->video.pad;

	dev_dbg(csi->dev, "creating %s:%u -> %s:%u link\n",
		entity->name, src_pad_index, sink->name, sink_pad->index);
	ret = media_create_pad_link(entity, src_pad_index, sink,
				    sink_pad->index,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		dev_err(csi->dev, "failed to create %s:%u -> %s:%u link\n",
			entity->name, src_pad_index,
			sink->name, sink_pad->index);
		return ret;
	}

	return 0;
}

static int sun6i_subdev_notify_complete(struct v4l2_async_notifier *notifier)
{
	struct sun6i_csi *csi = container_of(notifier, struct sun6i_csi,
					     notifier);
	struct v4l2_device *v4l2_dev = &csi->v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	dev_dbg(csi->dev, "notify complete, all subdevs registered\n");

	sd = list_first_entry(&v4l2_dev->subdevs, struct v4l2_subdev, list);
	if (!sd)
		return -EINVAL;

	ret = sun6i_csi_link_entity(csi, &sd->entity, sd->fwnode);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register_subdev_nodes(&csi->v4l2_dev);
	if (ret < 0)
		return ret;

	return media_device_register(&csi->media_dev);
}

static const struct v4l2_async_notifier_operations sun6i_csi_async_ops = {
	.complete = sun6i_subdev_notify_complete,
};

static int sun6i_csi_fwnode_parse(struct device *dev,
				  struct v4l2_fwnode_endpoint *vep,
				  struct v4l2_async_subdev *asd)
{
	struct sun6i_csi *csi = dev_get_drvdata(dev);

	if (vep->base.port || vep->base.id) {
		dev_warn(dev, "Only support a single port with one endpoint\n");
		return -ENOTCONN;
	}

	switch (vep->bus_type) {
	case V4L2_MBUS_PARALLEL:
	case V4L2_MBUS_BT656:
		csi->v4l2_ep = *vep;
		return 0;
	default:
		dev_err(dev, "Unsupported media bus type\n");
		return -ENOTCONN;
	}
}

static void sun6i_csi_v4l2_cleanup(struct sun6i_csi *csi)
{
	media_device_unregister(&csi->media_dev);
	v4l2_async_notifier_unregister(&csi->notifier);
	v4l2_async_notifier_cleanup(&csi->notifier);
	sun6i_video_cleanup(&csi->video);
	v4l2_device_unregister(&csi->v4l2_dev);
	v4l2_ctrl_handler_free(&csi->ctrl_handler);
	media_device_cleanup(&csi->media_dev);
}

static int sun6i_csi_v4l2_init(struct sun6i_csi *csi)
{
	int ret;

	csi->media_dev.dev = csi->dev;
	strscpy(csi->media_dev.model, "Allwinner Video Capture Device",
		sizeof(csi->media_dev.model));
	csi->media_dev.hw_revision = 0;

	media_device_init(&csi->media_dev);
	v4l2_async_notifier_init(&csi->notifier);

	ret = v4l2_ctrl_handler_init(&csi->ctrl_handler, 0);
	if (ret) {
		dev_err(csi->dev, "V4L2 controls handler init failed (%d)\n",
			ret);
		goto clean_media;
	}

	csi->v4l2_dev.mdev = &csi->media_dev;
	csi->v4l2_dev.ctrl_handler = &csi->ctrl_handler;
	ret = v4l2_device_register(csi->dev, &csi->v4l2_dev);
	if (ret) {
		dev_err(csi->dev, "V4L2 device registration failed (%d)\n",
			ret);
		goto free_ctrl;
	}

	ret = sun6i_video_init(&csi->video, csi, "sun6i-csi");
	if (ret)
		goto unreg_v4l2;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(csi->dev,
							 &csi->notifier,
							 sizeof(struct v4l2_async_subdev),
							 sun6i_csi_fwnode_parse);
	if (ret)
		goto clean_video;

	csi->notifier.ops = &sun6i_csi_async_ops;

	ret = v4l2_async_notifier_register(&csi->v4l2_dev, &csi->notifier);
	if (ret) {
		dev_err(csi->dev, "notifier registration failed\n");
		goto clean_video;
	}

	return 0;

clean_video:
	sun6i_video_cleanup(&csi->video);
unreg_v4l2:
	v4l2_device_unregister(&csi->v4l2_dev);
free_ctrl:
	v4l2_ctrl_handler_free(&csi->ctrl_handler);
clean_media:
	v4l2_async_notifier_cleanup(&csi->notifier);
	media_device_cleanup(&csi->media_dev);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Resources and IRQ
 */
static irqreturn_t sun6i_csi_isr(int irq, void *dev_id)
{
	struct sun6i_csi_dev *sdev = (struct sun6i_csi_dev *)dev_id;
	struct regmap *regmap = sdev->regmap;
	u32 status;

	regmap_read(regmap, CSI_CH_INT_STA_REG, &status);

	if (!(status & 0xFF))
		return IRQ_NONE;

	if ((status & CSI_CH_INT_STA_FIFO0_OF_PD) ||
	    (status & CSI_CH_INT_STA_FIFO1_OF_PD) ||
	    (status & CSI_CH_INT_STA_FIFO2_OF_PD) ||
	    (status & CSI_CH_INT_STA_HB_OF_PD)) {
		regmap_write(regmap, CSI_CH_INT_STA_REG, status);
		regmap_update_bits(regmap, CSI_EN_REG, CSI_EN_CSI_EN, 0);
		regmap_update_bits(regmap, CSI_EN_REG, CSI_EN_CSI_EN,
				   CSI_EN_CSI_EN);
		return IRQ_HANDLED;
	}

	if (status & CSI_CH_INT_STA_FD_PD)
		sun6i_video_frame_done(&sdev->csi.video);

	regmap_write(regmap, CSI_CH_INT_STA_REG, status);

	return IRQ_HANDLED;
}

static const struct regmap_config sun6i_csi_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x9c,
};

static int sun6i_csi_resource_request(struct sun6i_csi_dev *sdev,
				      struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *io_base;
	int ret;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	sdev->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "bus", io_base,
						 &sun6i_csi_regmap_config);
	if (IS_ERR(sdev->regmap)) {
		dev_err(&pdev->dev, "Failed to init register map\n");
		return PTR_ERR(sdev->regmap);
	}

	sdev->clk_mod = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(sdev->clk_mod)) {
		dev_err(&pdev->dev, "Unable to acquire csi clock\n");
		return PTR_ERR(sdev->clk_mod);
	}

	sdev->clk_ram = devm_clk_get(&pdev->dev, "ram");
	if (IS_ERR(sdev->clk_ram)) {
		dev_err(&pdev->dev, "Unable to acquire dram-csi clock\n");
		return PTR_ERR(sdev->clk_ram);
	}

	sdev->rstc_bus = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(sdev->rstc_bus)) {
		dev_err(&pdev->dev, "Cannot get reset controller\n");
		return PTR_ERR(sdev->rstc_bus);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENXIO;

	ret = devm_request_irq(&pdev->dev, irq, sun6i_csi_isr, 0, MODULE_NAME,
			       sdev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot request csi IRQ\n");
		return ret;
	}

	return 0;
}

/*
 * PHYS_OFFSET isn't available on all architectures. In order to
 * accommodate for COMPILE_TEST, let's define it to something dumb.
 */
#if defined(CONFIG_COMPILE_TEST) && !defined(PHYS_OFFSET)
#define PHYS_OFFSET 0
#endif

static int sun6i_csi_probe(struct platform_device *pdev)
{
	struct sun6i_csi_dev *sdev;
	int ret;

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->dev = &pdev->dev;
	/* The DMA bus has the memory mapped at 0 */
	sdev->dev->dma_pfn_offset = PHYS_OFFSET >> PAGE_SHIFT;

	ret = sun6i_csi_resource_request(sdev, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, sdev);

	sdev->csi.dev = &pdev->dev;
	return sun6i_csi_v4l2_init(&sdev->csi);
}

static int sun6i_csi_remove(struct platform_device *pdev)
{
	struct sun6i_csi_dev *sdev = platform_get_drvdata(pdev);

	sun6i_csi_v4l2_cleanup(&sdev->csi);

	return 0;
}

static const struct of_device_id sun6i_csi_of_match[] = {
	{ .compatible = "allwinner,sun6i-a31-csi", },
	{ .compatible = "allwinner,sun8i-a83t-csi", },
	{ .compatible = "allwinner,sun8i-h3-csi", },
	{ .compatible = "allwinner,sun8i-v3s-csi", },
	{ .compatible = "allwinner,sun50i-a64-csi", },
	{},
};
MODULE_DEVICE_TABLE(of, sun6i_csi_of_match);

static struct platform_driver sun6i_csi_platform_driver = {
	.probe = sun6i_csi_probe,
	.remove = sun6i_csi_remove,
	.driver = {
		.name = MODULE_NAME,
		.of_match_table = of_match_ptr(sun6i_csi_of_match),
	},
};
module_platform_driver(sun6i_csi_platform_driver);

MODULE_DESCRIPTION("Allwinner V3s Camera Sensor Interface driver");
MODULE_AUTHOR("Yong Deng <yong.deng@magewell.com>");
MODULE_LICENSE("GPL");
