/*
 *  cx18 ioctl control functions
 *
 *  Derived from ivtv-controls.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-cards.h"
#include "cx18-ioctl.h"
#include "cx18-audio.h"
#include "cx18-mailbox.h"
#include "cx18-controls.h"

/* Must be sorted from low to high control ID! */
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

int cx18_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *qctrl)
{
	struct cx18 *cx = ((struct cx18_open_id *)fh)->cx;
	const char *name;

	qctrl->id = v4l2_ctrl_next(ctrl_classes, qctrl->id);
	if (qctrl->id == 0)
		return -EINVAL;

	switch (qctrl->id) {
	/* Standard V4L2 controls */
	case V4L2_CID_USER_CLASS:
		return v4l2_ctrl_query_fill(qctrl, 0, 0, 0, 0);
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		if (v4l2_subdev_call(cx->sd_av, core, queryctrl, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		if (v4l2_subdev_call(cx->sd_av, core, queryctrl, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;

	default:
		if (cx2341x_ctrl_query(&cx->params, qctrl))
			qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
		return 0;
	}
	strncpy(qctrl->name, name, sizeof(qctrl->name) - 1);
	qctrl->name[sizeof(qctrl->name) - 1] = 0;
	return 0;
}

int cx18_querymenu(struct file *file, void *fh, struct v4l2_querymenu *qmenu)
{
	struct cx18 *cx = ((struct cx18_open_id *)fh)->cx;
	struct v4l2_queryctrl qctrl;

	qctrl.id = qmenu->id;
	cx18_queryctrl(file, fh, &qctrl);
	return v4l2_ctrl_query_menu(qmenu, &qctrl,
			cx2341x_ctrl_get_menu(&cx->params, qmenu->id));
}

static int cx18_try_ctrl(struct file *file, void *fh,
					struct v4l2_ext_control *vctrl)
{
	struct v4l2_queryctrl qctrl;
	const char **menu_items = NULL;
	int err;

	qctrl.id = vctrl->id;
	err = cx18_queryctrl(file, fh, &qctrl);
	if (err)
		return err;
	if (qctrl.type == V4L2_CTRL_TYPE_MENU)
		menu_items = v4l2_ctrl_get_menu(qctrl.id);
	return v4l2_ctrl_check(vctrl, &qctrl, menu_items);
}

static int cx18_s_ctrl(struct cx18 *cx, struct v4l2_control *vctrl)
{
	switch (vctrl->id) {
		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return v4l2_subdev_call(cx->sd_av, core, s_ctrl, vctrl);

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return v4l2_subdev_call(cx->sd_av, core, s_ctrl, vctrl);

	default:
		CX18_DEBUG_IOCTL("invalid control 0x%x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

static int cx18_g_ctrl(struct cx18 *cx, struct v4l2_control *vctrl)
{
	switch (vctrl->id) {
		/* Standard V4L2 controls */
	case V4L2_CID_BRIGHTNESS:
	case V4L2_CID_HUE:
	case V4L2_CID_SATURATION:
	case V4L2_CID_CONTRAST:
		return v4l2_subdev_call(cx->sd_av, core, g_ctrl, vctrl);

	case V4L2_CID_AUDIO_VOLUME:
	case V4L2_CID_AUDIO_MUTE:
	case V4L2_CID_AUDIO_BALANCE:
	case V4L2_CID_AUDIO_BASS:
	case V4L2_CID_AUDIO_TREBLE:
	case V4L2_CID_AUDIO_LOUDNESS:
		return v4l2_subdev_call(cx->sd_av, core, g_ctrl, vctrl);

	default:
		CX18_DEBUG_IOCTL("invalid control 0x%x\n", vctrl->id);
		return -EINVAL;
	}
	return 0;
}

static int cx18_setup_vbi_fmt(struct cx18 *cx,
			      enum v4l2_mpeg_stream_vbi_fmt fmt,
			      enum v4l2_mpeg_stream_type type)
{
	if (!(cx->v4l2_cap & V4L2_CAP_SLICED_VBI_CAPTURE))
		return -EINVAL;
	if (atomic_read(&cx->ana_capturing) > 0)
		return -EBUSY;

	if (fmt != V4L2_MPEG_STREAM_VBI_FMT_IVTV ||
	    !(type == V4L2_MPEG_STREAM_TYPE_MPEG2_PS ||
	      type == V4L2_MPEG_STREAM_TYPE_MPEG2_DVD ||
	      type == V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD)) {
		/* Only IVTV fmt VBI insertion & only MPEG-2 PS type streams */
		cx->vbi.insert_mpeg = V4L2_MPEG_STREAM_VBI_FMT_NONE;
		CX18_DEBUG_INFO("disabled insertion of sliced VBI data into "
				"the MPEG stream\n");
		return 0;
	}

	/* Allocate sliced VBI buffers if needed. */
	if (cx->vbi.sliced_mpeg_data[0] == NULL) {
		int i;

		for (i = 0; i < CX18_VBI_FRAMES; i++) {
			cx->vbi.sliced_mpeg_data[i] =
			       kmalloc(CX18_SLICED_MPEG_DATA_BUFSZ, GFP_KERNEL);
			if (cx->vbi.sliced_mpeg_data[i] == NULL) {
				while (--i >= 0) {
					kfree(cx->vbi.sliced_mpeg_data[i]);
					cx->vbi.sliced_mpeg_data[i] = NULL;
				}
				cx->vbi.insert_mpeg =
						  V4L2_MPEG_STREAM_VBI_FMT_NONE;
				CX18_WARN("Unable to allocate buffers for "
					  "sliced VBI data insertion\n");
				return -ENOMEM;
			}
		}
	}

	cx->vbi.insert_mpeg = fmt;
	CX18_DEBUG_INFO("enabled insertion of sliced VBI data into the MPEG PS,"
			"when sliced VBI is enabled\n");

	/*
	 * If our current settings have no lines set for capture, store a valid,
	 * default set of service lines to capture, in our current settings.
	 */
	if (cx18_get_service_set(cx->vbi.sliced_in) == 0) {
		if (cx->is_60hz)
			cx->vbi.sliced_in->service_set =
							V4L2_SLICED_CAPTION_525;
		else
			cx->vbi.sliced_in->service_set = V4L2_SLICED_WSS_625;
		cx18_expand_service_set(cx->vbi.sliced_in, cx->is_50hz);
	}
	return 0;
}

int cx18_g_ext_ctrls(struct file *file, void *fh, struct v4l2_ext_controls *c)
{
	struct cx18 *cx = ((struct cx18_open_id *)fh)->cx;
	struct v4l2_control ctrl;

	if (c->ctrl_class == V4L2_CTRL_CLASS_USER) {
		int i;
		int err = 0;

		for (i = 0; i < c->count; i++) {
			ctrl.id = c->controls[i].id;
			ctrl.value = c->controls[i].value;
			err = cx18_g_ctrl(cx, &ctrl);
			c->controls[i].value = ctrl.value;
			if (err) {
				c->error_idx = i;
				break;
			}
		}
		return err;
	}
	if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG)
		return cx2341x_ext_ctrls(&cx->params, 0, c, VIDIOC_G_EXT_CTRLS);
	return -EINVAL;
}

int cx18_s_ext_ctrls(struct file *file, void *fh, struct v4l2_ext_controls *c)
{
	struct cx18_open_id *id = fh;
	struct cx18 *cx = id->cx;
	int ret;
	struct v4l2_control ctrl;

	ret = v4l2_prio_check(&cx->prio, &id->prio);
	if (ret)
		return ret;

	if (c->ctrl_class == V4L2_CTRL_CLASS_USER) {
		int i;
		int err = 0;

		for (i = 0; i < c->count; i++) {
			ctrl.id = c->controls[i].id;
			ctrl.value = c->controls[i].value;
			err = cx18_s_ctrl(cx, &ctrl);
			c->controls[i].value = ctrl.value;
			if (err) {
				c->error_idx = i;
				break;
			}
		}
		return err;
	}
	if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG) {
		static u32 freqs[3] = { 44100, 48000, 32000 };
		struct cx18_api_func_private priv;
		struct cx2341x_mpeg_params p = cx->params;
		int err = cx2341x_ext_ctrls(&p, atomic_read(&cx->ana_capturing),
						c, VIDIOC_S_EXT_CTRLS);
		unsigned int idx;

		if (err)
			return err;

		if (p.video_encoding != cx->params.video_encoding) {
			int is_mpeg1 = p.video_encoding ==
						V4L2_MPEG_VIDEO_ENCODING_MPEG_1;
			struct v4l2_format fmt;

			/* fix videodecoder resolution */
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width = cx->params.width
						/ (is_mpeg1 ? 2 : 1);
			fmt.fmt.pix.height = cx->params.height;
			v4l2_subdev_call(cx->sd_av, video, s_fmt, &fmt);
		}
		priv.cx = cx;
		priv.s = &cx->streams[id->type];
		err = cx2341x_update(&priv, cx18_api_func, &cx->params, &p);
		if (!err &&
		    (cx->params.stream_vbi_fmt != p.stream_vbi_fmt ||
		     cx->params.stream_type != p.stream_type))
			err = cx18_setup_vbi_fmt(cx, p.stream_vbi_fmt,
						 p.stream_type);
		cx->params = p;
		cx->dualwatch_stereo_mode = p.audio_properties & 0x0300;
		idx = p.audio_properties & 0x03;
		/* The audio clock of the digitizer must match the codec sample
		   rate otherwise you get some very strange effects. */
		if (idx < sizeof(freqs))
			cx18_call_all(cx, audio, s_clock_freq, freqs[idx]);
		return err;
	}
	return -EINVAL;
}

int cx18_try_ext_ctrls(struct file *file, void *fh, struct v4l2_ext_controls *c)
{
	struct cx18 *cx = ((struct cx18_open_id *)fh)->cx;

	if (c->ctrl_class == V4L2_CTRL_CLASS_USER) {
		int i;
		int err = 0;

		for (i = 0; i < c->count; i++) {
			err = cx18_try_ctrl(file, fh, &c->controls[i]);
			if (err) {
				c->error_idx = i;
				break;
			}
		}
		return err;
	}
	if (c->ctrl_class == V4L2_CTRL_CLASS_MPEG)
		return cx2341x_ext_ctrls(&cx->params,
						atomic_read(&cx->ana_capturing),
						c, VIDIOC_TRY_EXT_CTRLS);
	return -EINVAL;
}
