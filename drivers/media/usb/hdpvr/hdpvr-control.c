/*
 * Hauppauge HD PVR USB driver - video 4 linux 2 interface
 *
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#include <linux/videodev2.h>

#include <media/v4l2-common.h>

#include "hdpvr.h"


int hdpvr_config_call(struct hdpvr_device *dev, uint value, u8 valbuf)
{
	int ret;
	char request_type = 0x38, snd_request = 0x01;

	mutex_lock(&dev->usbc_mutex);
	dev->usbc_buf[0] = valbuf;
	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      snd_request, 0x00 | request_type,
			      value, CTRL_DEFAULT_INDEX,
			      dev->usbc_buf, 1, 10000);

	mutex_unlock(&dev->usbc_mutex);
	v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
		 "config call request for value 0x%x returned %d\n", value,
		 ret);

	return ret < 0 ? ret : 0;
}

int get_video_info(struct hdpvr_device *dev, struct hdpvr_video_info *vidinf)
{
	int ret;

	vidinf->valid = false;
	mutex_lock(&dev->usbc_mutex);
	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      0x81, 0x80 | 0x38,
			      0x1400, 0x0003,
			      dev->usbc_buf, 5,
			      1000);

#ifdef HDPVR_DEBUG
	if (hdpvr_debug & MSG_INFO)
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
			 "get video info returned: %d, %5ph\n", ret,
			 dev->usbc_buf);
#endif
	mutex_unlock(&dev->usbc_mutex);

	if (ret < 0)
		return ret;

	vidinf->width	= dev->usbc_buf[1] << 8 | dev->usbc_buf[0];
	vidinf->height	= dev->usbc_buf[3] << 8 | dev->usbc_buf[2];
	vidinf->fps	= dev->usbc_buf[4];
	vidinf->valid   = vidinf->width && vidinf->height && vidinf->fps;

	return 0;
}

int get_input_lines_info(struct hdpvr_device *dev)
{
	int ret, lines;

	mutex_lock(&dev->usbc_mutex);
	ret = usb_control_msg(dev->udev,
			      usb_rcvctrlpipe(dev->udev, 0),
			      0x81, 0x80 | 0x38,
			      0x1800, 0x0003,
			      dev->usbc_buf, 3,
			      1000);

#ifdef HDPVR_DEBUG
	if (hdpvr_debug & MSG_INFO)
		v4l2_dbg(MSG_INFO, hdpvr_debug, &dev->v4l2_dev,
			 "get input lines info returned: %d, %3ph\n", ret,
			 dev->usbc_buf);
#else
	(void)ret;	/* suppress compiler warning */
#endif
	lines = dev->usbc_buf[1] << 8 | dev->usbc_buf[0];
	mutex_unlock(&dev->usbc_mutex);
	return lines;
}


int hdpvr_set_bitrate(struct hdpvr_device *dev)
{
	int ret;

	mutex_lock(&dev->usbc_mutex);
	memset(dev->usbc_buf, 0, 4);
	dev->usbc_buf[0] = dev->options.bitrate;
	dev->usbc_buf[2] = dev->options.peak_bitrate;

	ret = usb_control_msg(dev->udev,
			      usb_sndctrlpipe(dev->udev, 0),
			      0x01, 0x38, CTRL_BITRATE_VALUE,
			      CTRL_DEFAULT_INDEX, dev->usbc_buf, 4, 1000);
	mutex_unlock(&dev->usbc_mutex);

	return ret;
}

int hdpvr_set_audio(struct hdpvr_device *dev, u8 input,
		    enum v4l2_mpeg_audio_encoding codec)
{
	int ret = 0;

	if (dev->flags & HDPVR_FLAG_AC3_CAP) {
		mutex_lock(&dev->usbc_mutex);
		memset(dev->usbc_buf, 0, 2);
		dev->usbc_buf[0] = input;
		if (codec == V4L2_MPEG_AUDIO_ENCODING_AAC)
			dev->usbc_buf[1] = 0;
		else if (codec == V4L2_MPEG_AUDIO_ENCODING_AC3)
			dev->usbc_buf[1] = 1;
		else {
			mutex_unlock(&dev->usbc_mutex);
			v4l2_err(&dev->v4l2_dev, "invalid audio codec %d\n",
				 codec);
			ret = -EINVAL;
			goto error;
		}

		ret = usb_control_msg(dev->udev,
				      usb_sndctrlpipe(dev->udev, 0),
				      0x01, 0x38, CTRL_AUDIO_INPUT_VALUE,
				      CTRL_DEFAULT_INDEX, dev->usbc_buf, 2,
				      1000);
		mutex_unlock(&dev->usbc_mutex);
		if (ret == 2)
			ret = 0;
	} else
		ret = hdpvr_config_call(dev, CTRL_AUDIO_INPUT_VALUE, input);
error:
	return ret;
}

int hdpvr_set_options(struct hdpvr_device *dev)
{
	hdpvr_config_call(dev, CTRL_VIDEO_STD_TYPE, dev->options.video_std);

	hdpvr_config_call(dev, CTRL_VIDEO_INPUT_VALUE,
			 dev->options.video_input+1);

	hdpvr_set_audio(dev, dev->options.audio_input+1,
		       dev->options.audio_codec);

	hdpvr_set_bitrate(dev);
	hdpvr_config_call(dev, CTRL_BITRATE_MODE_VALUE,
			 dev->options.bitrate_mode);
	hdpvr_config_call(dev, CTRL_GOP_MODE_VALUE, dev->options.gop_mode);

	hdpvr_config_call(dev, CTRL_BRIGHTNESS, dev->options.brightness);
	hdpvr_config_call(dev, CTRL_CONTRAST,   dev->options.contrast);
	hdpvr_config_call(dev, CTRL_HUE,        dev->options.hue);
	hdpvr_config_call(dev, CTRL_SATURATION, dev->options.saturation);
	hdpvr_config_call(dev, CTRL_SHARPNESS,  dev->options.sharpness);

	return 0;
}
