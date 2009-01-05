/*
    V4L2 sub-device support.

    Copyright (C) 2008  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>

int v4l2_subdev_command(struct v4l2_subdev *sd, unsigned cmd, void *arg)
{
	switch (cmd) {
	case VIDIOC_QUERYCTRL:
		return v4l2_subdev_call(sd, core, querymenu, arg);
	case VIDIOC_G_CTRL:
		return v4l2_subdev_call(sd, core, g_ctrl, arg);
	case VIDIOC_S_CTRL:
		return v4l2_subdev_call(sd, core, s_ctrl, arg);
	case VIDIOC_QUERYMENU:
		return v4l2_subdev_call(sd, core, queryctrl, arg);
	case VIDIOC_LOG_STATUS:
		return v4l2_subdev_call(sd, core, log_status);
	case VIDIOC_DBG_G_CHIP_IDENT:
		return v4l2_subdev_call(sd, core, g_chip_ident, arg);
	case VIDIOC_INT_S_STANDBY:
		return v4l2_subdev_call(sd, core, s_standby, arg ? (*(u32 *)arg) : 0);
	case VIDIOC_INT_RESET:
		return v4l2_subdev_call(sd, core, reset, arg ? (*(u32 *)arg) : 0);
	case VIDIOC_INT_S_GPIO:
		return v4l2_subdev_call(sd, core, s_gpio, arg ? (*(u32 *)arg) : 0);
	case VIDIOC_INT_INIT:
		return v4l2_subdev_call(sd, core, init, arg ? (*(u32 *)arg) : 0);
#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_DBG_G_REGISTER:
		return v4l2_subdev_call(sd, core, g_register, arg);
	case VIDIOC_DBG_S_REGISTER:
		return v4l2_subdev_call(sd, core, s_register, arg);
#endif

	case VIDIOC_INT_S_TUNER_MODE:
		return v4l2_subdev_call(sd, tuner, s_mode, *(enum v4l2_tuner_type *)arg);
	case AUDC_SET_RADIO:
		return v4l2_subdev_call(sd, tuner, s_radio);
	case VIDIOC_S_TUNER:
		return v4l2_subdev_call(sd, tuner, s_tuner, arg);
	case VIDIOC_G_TUNER:
		return v4l2_subdev_call(sd, tuner, g_tuner, arg);
	case VIDIOC_S_STD:
		return v4l2_subdev_call(sd, tuner, s_std, *(v4l2_std_id *)arg);
	case VIDIOC_S_FREQUENCY:
		return v4l2_subdev_call(sd, tuner, s_frequency, arg);
	case VIDIOC_G_FREQUENCY:
		return v4l2_subdev_call(sd, tuner, g_frequency, arg);
	case TUNER_SET_TYPE_ADDR:
		return v4l2_subdev_call(sd, tuner, s_type_addr, arg);
	case TUNER_SET_CONFIG:
		return v4l2_subdev_call(sd, tuner, s_config, arg);

	case VIDIOC_INT_AUDIO_CLOCK_FREQ:
		return v4l2_subdev_call(sd, audio, s_clock_freq, *(u32 *)arg);
	case VIDIOC_INT_S_AUDIO_ROUTING:
		return v4l2_subdev_call(sd, audio, s_routing, arg);
	case VIDIOC_INT_I2S_CLOCK_FREQ:
		return v4l2_subdev_call(sd, audio, s_i2s_clock_freq, *(u32 *)arg);

	case VIDIOC_INT_S_VIDEO_ROUTING:
		return v4l2_subdev_call(sd, video, s_routing, arg);
	case VIDIOC_INT_S_CRYSTAL_FREQ:
		return v4l2_subdev_call(sd, video, s_crystal_freq, arg);
	case VIDIOC_INT_DECODE_VBI_LINE:
		return v4l2_subdev_call(sd, video, decode_vbi_line, arg);
	case VIDIOC_INT_S_VBI_DATA:
		return v4l2_subdev_call(sd, video, s_vbi_data, arg);
	case VIDIOC_INT_G_VBI_DATA:
		return v4l2_subdev_call(sd, video, g_vbi_data, arg);
	case VIDIOC_G_SLICED_VBI_CAP:
		return v4l2_subdev_call(sd, video, g_sliced_vbi_cap, arg);
	case VIDIOC_S_FMT:
		return v4l2_subdev_call(sd, video, s_fmt, arg);
	case VIDIOC_G_FMT:
		return v4l2_subdev_call(sd, video, g_fmt, arg);
	case VIDIOC_INT_S_STD_OUTPUT:
		return v4l2_subdev_call(sd, video, s_std_output, *(v4l2_std_id *)arg);
	case VIDIOC_STREAMON:
		return v4l2_subdev_call(sd, video, s_stream, 1);
	case VIDIOC_STREAMOFF:
		return v4l2_subdev_call(sd, video, s_stream, 0);

	default:
		return v4l2_subdev_call(sd, core, ioctl, cmd, arg);
	}
}
EXPORT_SYMBOL_GPL(v4l2_subdev_command);
