
/*
 * The Capture code for Fujitsu M-5MOLS ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/i2c/m5mols.h>
#include <media/drv-intf/exynos-fimc.h>

#include "m5mols.h"
#include "m5mols_reg.h"

/**
 * m5mols_read_rational - I2C read of a rational number
 *
 * Read numerator and denominator from registers @addr_num and @addr_den
 * respectively and return the division result in @val.
 */
static int m5mols_read_rational(struct v4l2_subdev *sd, u32 addr_num,
				u32 addr_den, u32 *val)
{
	u32 num, den;

	int ret = m5mols_read_u32(sd, addr_num, &num);
	if (!ret)
		ret = m5mols_read_u32(sd, addr_den, &den);
	if (ret)
		return ret;
	*val = den == 0 ? 0 : num / den;
	return ret;
}

/**
 * m5mols_capture_info - Gather captured image information
 *
 * For now it gathers only EXIF information and file size.
 */
static int m5mols_capture_info(struct m5mols_info *info)
{
	struct m5mols_exif *exif = &info->cap.exif;
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	ret = m5mols_read_rational(sd, EXIF_INFO_EXPTIME_NU,
				   EXIF_INFO_EXPTIME_DE, &exif->exposure_time);
	if (ret)
		return ret;
	ret = m5mols_read_rational(sd, EXIF_INFO_TV_NU, EXIF_INFO_TV_DE,
				   &exif->shutter_speed);
	if (ret)
		return ret;
	ret = m5mols_read_rational(sd, EXIF_INFO_AV_NU, EXIF_INFO_AV_DE,
				   &exif->aperture);
	if (ret)
		return ret;
	ret = m5mols_read_rational(sd, EXIF_INFO_BV_NU, EXIF_INFO_BV_DE,
				   &exif->brightness);
	if (ret)
		return ret;
	ret = m5mols_read_rational(sd, EXIF_INFO_EBV_NU, EXIF_INFO_EBV_DE,
				   &exif->exposure_bias);
	if (ret)
		return ret;

	ret = m5mols_read_u16(sd, EXIF_INFO_ISO, &exif->iso_speed);
	if (!ret)
		ret = m5mols_read_u16(sd, EXIF_INFO_FLASH, &exif->flash);
	if (!ret)
		ret = m5mols_read_u16(sd, EXIF_INFO_SDR, &exif->sdr);
	if (!ret)
		ret = m5mols_read_u16(sd, EXIF_INFO_QVAL, &exif->qval);
	if (ret)
		return ret;

	if (!ret)
		ret = m5mols_read_u32(sd, CAPC_IMAGE_SIZE, &info->cap.main);
	if (!ret)
		ret = m5mols_read_u32(sd, CAPC_THUMB_SIZE, &info->cap.thumb);
	if (!ret)
		info->cap.total = info->cap.main + info->cap.thumb;

	return ret;
}

int m5mols_start_capture(struct m5mols_info *info)
{
	unsigned int framesize = info->cap.buf_size - M5MOLS_JPEG_TAGS_SIZE;
	struct v4l2_subdev *sd = &info->sd;
	int ret;

	/*
	 * Synchronize the controls, set the capture frame resolution and color
	 * format. The frame capture is initiated during switching from Monitor
	 * to Capture mode.
	 */
	ret = m5mols_set_mode(info, REG_MONITOR);
	if (!ret)
		ret = m5mols_restore_controls(info);
	if (!ret)
		ret = m5mols_write(sd, CAPP_YUVOUT_MAIN, REG_JPEG);
	if (!ret)
		ret = m5mols_write(sd, CAPP_MAIN_IMAGE_SIZE, info->resolution);
	if (!ret)
		ret = m5mols_write(sd, CAPP_JPEG_SIZE_MAX, framesize);
	if (!ret)
		ret = m5mols_set_mode(info, REG_CAPTURE);
	if (!ret)
		/* Wait until a frame is captured to ISP internal memory */
		ret = m5mols_wait_interrupt(sd, REG_INT_CAPTURE, 2000);
	if (ret)
		return ret;

	/*
	 * Initiate the captured data transfer to a MIPI-CSI receiver.
	 */
	ret = m5mols_write(sd, CAPC_SEL_FRAME, 1);
	if (!ret)
		ret = m5mols_write(sd, CAPC_START, REG_CAP_START_MAIN);
	if (!ret) {
		bool captured = false;
		unsigned int size;

		/* Wait for the capture completion interrupt */
		ret = m5mols_wait_interrupt(sd, REG_INT_CAPTURE, 2000);
		if (!ret) {
			captured = true;
			ret = m5mols_capture_info(info);
		}
		size = captured ? info->cap.main : 0;
		v4l2_dbg(1, m5mols_debug, sd, "%s: size: %d, thumb.: %d B\n",
			 __func__, size, info->cap.thumb);

		v4l2_subdev_notify(sd, S5P_FIMC_TX_END_NOTIFY, &size);
	}

	return ret;
}
