/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2011-2018 Magewell Electronics Co., Ltd. (Nanjing)
 * All rights reserved.
 * Author: Yong Deng <yong.deng@magewell.com>
 */

#ifndef __SUN6I_CSI_H__
#define __SUN6I_CSI_H__

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun6i_video.h"

struct sun6i_csi;

/**
 * struct sun6i_csi_config - configs for sun6i csi
 * @pixelformat: v4l2 pixel format (V4L2_PIX_FMT_*)
 * @code:	media bus format code (MEDIA_BUS_FMT_*)
 * @field:	used interlacing type (enum v4l2_field)
 * @width:	frame width
 * @height:	frame height
 */
struct sun6i_csi_config {
	u32		pixelformat;
	u32		code;
	u32		field;
	u32		width;
	u32		height;
};

struct sun6i_csi {
	struct device			*dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_device		v4l2_dev;
	struct media_device		media_dev;

	struct v4l2_async_notifier	notifier;

	/* video port settings */
	struct v4l2_fwnode_endpoint	v4l2_ep;

	struct sun6i_csi_config		config;

	struct sun6i_video		video;
};

/**
 * sun6i_csi_is_format_supported() - check if the format supported by csi
 * @csi:	pointer to the csi
 * @pixformat:	v4l2 pixel format (V4L2_PIX_FMT_*)
 * @mbus_code:	media bus format code (MEDIA_BUS_FMT_*)
 */
bool sun6i_csi_is_format_supported(struct sun6i_csi *csi, u32 pixformat,
				   u32 mbus_code);

/**
 * sun6i_csi_set_power() - power on/off the csi
 * @csi:	pointer to the csi
 * @enable:	on/off
 */
int sun6i_csi_set_power(struct sun6i_csi *csi, bool enable);

/**
 * sun6i_csi_update_config() - update the csi register settings
 * @csi:	pointer to the csi
 * @config:	see struct sun6i_csi_config
 */
int sun6i_csi_update_config(struct sun6i_csi *csi,
			    struct sun6i_csi_config *config);

/**
 * sun6i_csi_update_buf_addr() - update the csi frame buffer address
 * @csi:	pointer to the csi
 * @addr:	frame buffer's physical address
 */
void sun6i_csi_update_buf_addr(struct sun6i_csi *csi, dma_addr_t addr);

/**
 * sun6i_csi_set_stream() - start/stop csi streaming
 * @csi:	pointer to the csi
 * @enable:	start/stop
 */
void sun6i_csi_set_stream(struct sun6i_csi *csi, bool enable);

/* get bpp form v4l2 pixformat */
static inline int sun6i_csi_get_bpp(unsigned int pixformat)
{
	switch (pixformat) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
	case V4L2_PIX_FMT_JPEG:
		return 8;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		return 10;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
	case V4L2_PIX_FMT_NV12_16L16:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		return 12;
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB565X:
		return 16;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		return 24;
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR32:
		return 32;
	default:
		WARN(1, "Unsupported pixformat: 0x%x\n", pixformat);
		break;
	}

	return 0;
}

#endif /* __SUN6I_CSI_H__ */
