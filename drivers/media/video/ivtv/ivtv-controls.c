/*
    ioctl control functions
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

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

#include "ivtv-driver.h"
#include "ivtv-cards.h"
#include "ivtv-ioctl.h"
#include "ivtv-audio.h"
#include "ivtv-i2c.h"
#include "ivtv-mailbox.h"
#include "ivtv-controls.h"

static const u32 user_ctrls[] = {
	V4L2_CID_USER_CLASS,
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_CONTRAST,
	V4L2_CID_SATURATION,
	V4L2_CID_HUE,
	V4L2_CID_AUDIO_VOLUME,
	V4L2_CID_AUDIO_BALANCE,
	V4L2_CID_AUDIO_BASS,
	V4L2_CID_AUDIO_TREBLE,
	V4L2_CID_AUDIO_MUTE,
	V4L2_CID_AUDIO_LOUDNESS,
	0
};

static const u32 *ctrl_classes[] = {
	user_ctrls,
	cx2341x_mpeg_ctrls,
	NULL
};

static int ivtv_queryctrl(struct ivtv *itv, struct v4l2_queryctrl *qctrl)
{
	const char *name;

	IVTV_DEBUG_IOCTL("VIDIOC_QUERYCTRL(%08x)\n", qctrl->id);

	qctrl->id = v4l2_ctrl_next(ctrl_classes, qctrl->id);
	if (qctrl->id == 0)
		return -EINVAL;

	switch (qctrl->id) {
	/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		if (itv->video_dec_func(itv, VIDIOC_QUERYCTRL, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		if (ivtv_i2c_hw(itv, itv->card->hw_audio_ctrl, VIDIOC_QUERYCTRL, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;

	default:
		if (cx2341x_ctrl_query(&itv->params, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;
	}
	strncpy(qctrl->name, name, sizeof(qctrl->name) - 1);
	qctrl->name[sizeof(qctrl->name) - 1] = 0;
	return 0;
}

static int ivtv_querymenu(struct ivtv *itv, struct v4l2_querymenu *qmenu)
{
	struct v4l2_queryctrl qctrl;

	qctrl.id = qmenu->id;
	ivtv_queryctrl(itv, &qctrl);
	return v4l2_ctrl_query_menu(qmenu, &qctrl, cx2341x_ctrl_get_menu(qmenu->id));
}

static int ivtv_s_ctrl(struct ivtv *itv, struct v4l2_control *vctrl)
{
	s32 v = vctrl->value;

	IVTV_DEBUG_IOCTL("VIDIOC_S_CTRL(%08x, %x)\n", vctrl->id, v);

	switch (vctrl->id) {
		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return itv->video_dec_func(itv, VIDIOC_S_CTRL, vctrl);

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return ivtv_i2c_hw(itv, itv->card->hw_audio_ctrl, VIDIOC_S_CTRL, vctrl);

	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

static int ivtv_g_ctrl(struct ivtv *itv, struct v4l2_control *vctrl)
{
	IVTV_DEBUG_IOCTL("VIDIOC_G_CTRL(%08x)\n", vctrl->id);

	switch (vctrl->id) {
		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return itv->video_dec_func(itv, VIDIOC_G_CTRL, vctrl);

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return ivtv_i2c_hw(itv, itv->card->hw_audio_ctrl, VIDIOC_G_CTRL, vctrl);
	default:
		IVTV_DEBUG_IOCTL("invalid control %x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

static int ivtv_setup_vbi_fmt(struct ivtv *itv, enum v4l2_mpeg_stream_vbi_fmt fmt)
{
	if (!(itv->v4l2_cap & V4L2_CAP_SLICED_VBI_CAPTURE))
		return -EINVAL;
	if (atomic_read(&itv->capturing) > 0)
		return -EBUSY;

	/* First try to allocate sliced VBI buffers if needed. */
	if (fmt && itv->vbi.sliced_mpeg_data[0] == NULL) {
		int i;

		for (i = 0; i < IVTV_VBI_FRAMES; i++) {
			/* Yuck, hardcoded. Needs to be a define */
			itv->vbi.sliced_mpeg_data[i] = kmalloc(2049, GFP_KERNEL);
			if (itv->vbi.sliced_mpeg_data[i] == NULL) {
				while (--i >= 0) {
					kfree(itv->vbi.sliced_mpeg_data[i]);
					itv->vbi.sliced_mpeg_data[i] = NULL;
				}
				return -ENOMEM;
			}
		}
	}

	itv->vbi.insert_mpeg = fmt;

	if (itv->vbi.insert_mpeg == 0) {
		return 0;
	}
	/* Need sliced data for mpeg insertion */
	if (get_service_set(itv->vbi.sliced_in) == 0) {
		if (itv->is_60hz)
			itv->vbi.sliced_in->service_set = V4L2_SLICED_CAPTION_525;
		else
			itv->vbi.sliced_in->service_set = V4L2_SLICED_WSS_625;
		expand_service_set(itv->vbi.sliced_in, itv->is_50hz);
	}
	return 0;
}

int ivtv_control_ioctls(struct ivtv *itv, unsigned int cmd, void *arg)
{
	struct v4l2_control ctrl;

	switch (cmd) {
	case VIDIOC_QUERYMENU:
		IVTV_DEBUG_IOCTL("VIDIOC_QUERYMENU\n");
		return ivtv_querymenu(itv, arg);

	case VIDIOC_QUERYCTRL:
		return ivtv_queryctrl(itv, arg);

	case VIDIOC_S_CTRL:
		return ivtv_s_ctrl(itv, arg);

	case VIDIOC_G_CTRL:
		return ivtv_g_ctrl(itv, arg);

	case VIDIOC_S_EXT_CTRLS:
	{
		struct v4l2_ext_controls *c = arg;

		if (c->ctrl_class == V4L2_CTRL_CLASS_USER) {
			int i;
			int err = 0;

			for (i = 0; i < c->count; i++) {
				ctrl.id = c->controls[i].id;
				ctrl.value = c->controls[i].value;
				err = ivtv_s_ctrl(itv, &ctrl);
				c->controls[i].value = ctrl.value;
				if (err) {
					c->error_idx = i;
					break;
				}
			}
			return err;
		}
		IVTV_DEBUG_IOCTL("VIDIOC_S_EXT_CTRLS\n");
		if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG) {
			struct cx2341x_mpeg_params p = itv->params;
			int err = cx2341x_ext_ctrls(&p, arg, cmd);

			if (err)
				return err;

			if (p.video_encoding != itv->params.video_encoding) {
				int is_mpeg1 = p.video_encoding ==
						V4L2_MPEG_VIDEO_ENCODING_MPEG_1;
				struct v4l2_format fmt;

				/* fix videodecoder resolution */
				fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				fmt.fmt.pix.width = itv->params.width / (is_mpeg1 ? 2 : 1);
				fmt.fmt.pix.height = itv->params.height;
				itv->video_dec_func(itv, VIDIOC_S_FMT, &fmt);
			}
			err = cx2341x_update(itv, ivtv_api_func, &itv->params, &p);
			if (!err && itv->params.stream_vbi_fmt != p.stream_vbi_fmt) {
				err = ivtv_setup_vbi_fmt(itv, p.stream_vbi_fmt);
			}
			itv->params = p;
			itv->dualwatch_stereo_mode = p.audio_properties & 0x0300;
			ivtv_audio_set_audio_clock_freq(itv, p.audio_properties & 0x03);
			return err;
		}
		return -EINVAL;
	}

	case VIDIOC_G_EXT_CTRLS:
	{
		struct v4l2_ext_controls *c = arg;

		if (c->ctrl_class == V4L2_CTRL_CLASS_USER) {
			int i;
			int err = 0;

			for (i = 0; i < c->count; i++) {
				ctrl.id = c->controls[i].id;
				ctrl.value = c->controls[i].value;
				err = ivtv_g_ctrl(itv, &ctrl);
				c->controls[i].value = ctrl.value;
				if (err) {
					c->error_idx = i;
					break;
				}
			}
			return err;
		}
		IVTV_DEBUG_IOCTL("VIDIOC_G_EXT_CTRLS\n");
		if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG)
			return cx2341x_ext_ctrls(&itv->params, arg, cmd);
		return -EINVAL;
	}

	case VIDIOC_TRY_EXT_CTRLS:
	{
		struct v4l2_ext_controls *c = arg;

		IVTV_DEBUG_IOCTL("VIDIOC_TRY_EXT_CTRLS\n");
		if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG)
			return cx2341x_ext_ctrls(&itv->params, arg, cmd);
		return -EINVAL;
	}

	default:
		return -EINVAL;
	}
	return 0;
}
