/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *  Copyright (C) 2004 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include "pvrusb2-context.h"
#include "pvrusb2-hdw.h"
#include "pvrusb2.h"
#include "pvrusb2-debug.h"
#include "pvrusb2-v4l2.h"
#include "pvrusb2-ioread.h"
#include <linux/videodev2.h>
#include <linux/module.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

struct pvr2_v4l2_dev;
struct pvr2_v4l2_fh;
struct pvr2_v4l2;

struct pvr2_v4l2_dev {
	struct video_device devbase; /* MUST be first! */
	struct pvr2_v4l2 *v4lp;
	struct pvr2_context_stream *stream;
	/* Information about this device: */
	enum pvr2_config config; /* Expected stream format */
	int v4l_type; /* V4L defined type for this device node */
	enum pvr2_v4l_type minor_type; /* pvr2-understood minor device type */
};

struct pvr2_v4l2_fh {
	struct v4l2_fh fh;
	struct pvr2_channel channel;
	struct pvr2_v4l2_dev *pdi;
	struct pvr2_ioread *rhp;
	struct file *file;
	wait_queue_head_t wait_data;
	int fw_mode_flag;
	/* Map contiguous ordinal value to input id */
	unsigned char *input_map;
	unsigned int input_cnt;
};

struct pvr2_v4l2 {
	struct pvr2_channel channel;

	/* streams - Note that these must be separately, individually,
	 * allocated pointers.  This is because the v4l core is going to
	 * manage their deletion - separately, individually...  */
	struct pvr2_v4l2_dev *dev_video;
	struct pvr2_v4l2_dev *dev_radio;
};

static int video_nr[PVR_NUM] = {[0 ... PVR_NUM-1] = -1};
module_param_array(video_nr, int, NULL, 0444);
MODULE_PARM_DESC(video_nr, "Offset for device's video dev minor");
static int radio_nr[PVR_NUM] = {[0 ... PVR_NUM-1] = -1};
module_param_array(radio_nr, int, NULL, 0444);
MODULE_PARM_DESC(radio_nr, "Offset for device's radio dev minor");
static int vbi_nr[PVR_NUM] = {[0 ... PVR_NUM-1] = -1};
module_param_array(vbi_nr, int, NULL, 0444);
MODULE_PARM_DESC(vbi_nr, "Offset for device's vbi dev minor");

#define PVR_FORMAT_PIX  0
#define PVR_FORMAT_VBI  1

static struct v4l2_format pvr_format [] = {
	[PVR_FORMAT_PIX] = {
		.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt    = {
			.pix        = {
				.width          = 720,
				.height         = 576,
				.pixelformat    = V4L2_PIX_FMT_MPEG,
				.field          = V4L2_FIELD_INTERLACED,
				/* FIXME : Don't know what to put here... */
				.sizeimage      = 32 * 1024,
			}
		}
	},
	[PVR_FORMAT_VBI] = {
		.type   = V4L2_BUF_TYPE_VBI_CAPTURE,
		.fmt    = {
			.vbi        = {
				.sampling_rate = 27000000,
				.offset = 248,
				.samples_per_line = 1443,
				.sample_format = V4L2_PIX_FMT_GREY,
				.start = { 0, 0 },
				.count = { 0, 0 },
				.flags = 0,
			}
		}
	}
};



/*
 * This is part of Video 4 Linux API. These procedures handle ioctl() calls.
 */
static int pvr2_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;

	strscpy(cap->driver, "pvrusb2", sizeof(cap->driver));
	strscpy(cap->bus_info, pvr2_hdw_get_bus_info(hdw),
		sizeof(cap->bus_info));
	strscpy(cap->card, pvr2_hdw_get_desc(hdw), sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_TUNER |
			    V4L2_CAP_AUDIO | V4L2_CAP_RADIO |
			    V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;
	switch (fh->pdi->devbase.vfl_type) {
	case VFL_TYPE_GRABBER:
		cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_AUDIO;
		break;
	case VFL_TYPE_RADIO:
		cap->device_caps = V4L2_CAP_RADIO;
		break;
	default:
		return -EINVAL;
	}
	cap->device_caps |= V4L2_CAP_TUNER | V4L2_CAP_READWRITE;
	return 0;
}

static int pvr2_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int val = 0;
	int ret;

	ret = pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_STDCUR), &val);
	*std = val;
	return ret;
}

static int pvr2_s_std(struct file *file, void *priv, v4l2_std_id std)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int ret;

	ret = pvr2_ctrl_set_value(
		pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_STDCUR), std);
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_querystd(struct file *file, void *priv, v4l2_std_id *std)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int val = 0;
	int ret;

	ret = pvr2_ctrl_get_value(
		pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_STDDETECT), &val);
	*std = val;
	return ret;
}

static int pvr2_enum_input(struct file *file, void *priv, struct v4l2_input *vi)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct pvr2_ctrl *cptr;
	struct v4l2_input tmp;
	unsigned int cnt;
	int val;

	cptr = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_INPUT);

	memset(&tmp, 0, sizeof(tmp));
	tmp.index = vi->index;
	if (vi->index >= fh->input_cnt)
		return -EINVAL;
	val = fh->input_map[vi->index];
	switch (val) {
	case PVR2_CVAL_INPUT_TV:
	case PVR2_CVAL_INPUT_DTV:
	case PVR2_CVAL_INPUT_RADIO:
		tmp.type = V4L2_INPUT_TYPE_TUNER;
		break;
	case PVR2_CVAL_INPUT_SVIDEO:
	case PVR2_CVAL_INPUT_COMPOSITE:
		tmp.type = V4L2_INPUT_TYPE_CAMERA;
		break;
	default:
		return -EINVAL;
	}

	cnt = 0;
	pvr2_ctrl_get_valname(cptr, val,
			tmp.name, sizeof(tmp.name) - 1, &cnt);
	tmp.name[cnt] = 0;

	/* Don't bother with audioset, since this driver currently
	   always switches the audio whenever the video is
	   switched. */

	/* Handling std is a tougher problem.  It doesn't make
	   sense in cases where a device might be multi-standard.
	   We could just copy out the current value for the
	   standard, but it can change over time.  For now just
	   leave it zero. */
	*vi = tmp;
	return 0;
}

static int pvr2_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	unsigned int idx;
	struct pvr2_ctrl *cptr;
	int val;
	int ret;

	cptr = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_INPUT);
	val = 0;
	ret = pvr2_ctrl_get_value(cptr, &val);
	*i = 0;
	for (idx = 0; idx < fh->input_cnt; idx++) {
		if (fh->input_map[idx] == val) {
			*i = idx;
			break;
		}
	}
	return ret;
}

static int pvr2_s_input(struct file *file, void *priv, unsigned int inp)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int ret;

	if (inp >= fh->input_cnt)
		return -EINVAL;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_INPUT),
			fh->input_map[inp]);
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_enumaudio(struct file *file, void *priv, struct v4l2_audio *vin)
{
	/* pkt: FIXME: We are returning one "fake" input here
	   which could very well be called "whatever_we_like".
	   This is for apps that want to see an audio input
	   just to feel comfortable, as well as to test if
	   it can do stereo or sth. There is actually no guarantee
	   that the actual audio input cannot change behind the app's
	   back, but most applications should not mind that either.

	   Hopefully, mplayer people will work with us on this (this
	   whole mess is to support mplayer pvr://), or Hans will come
	   up with a more standard way to say "we have inputs but we
	   don 't want you to change them independent of video" which
	   will sort this mess.
	 */

	if (vin->index > 0)
		return -EINVAL;
	strscpy(vin->name, "PVRUSB2 Audio", sizeof(vin->name));
	vin->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int pvr2_g_audio(struct file *file, void *priv, struct v4l2_audio *vin)
{
	/* pkt: FIXME: see above comment (VIDIOC_ENUMAUDIO) */
	vin->index = 0;
	strscpy(vin->name, "PVRUSB2 Audio", sizeof(vin->name));
	vin->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int pvr2_s_audio(struct file *file, void *priv, const struct v4l2_audio *vout)
{
	if (vout->index)
		return -EINVAL;
	return 0;
}

static int pvr2_g_tuner(struct file *file, void *priv, struct v4l2_tuner *vt)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;

	if (vt->index != 0)
		return -EINVAL; /* Only answer for the 1st tuner */

	pvr2_hdw_execute_tuner_poll(hdw);
	return pvr2_hdw_get_tuner_status(hdw, vt);
}

static int pvr2_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *vt)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int ret;

	if (vt->index != 0)
		return -EINVAL;

	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_AUDIOMODE),
			vt->audmode);
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_s_frequency(struct file *file, void *priv, const struct v4l2_frequency *vf)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	unsigned long fv;
	struct v4l2_tuner vt;
	int cur_input;
	struct pvr2_ctrl *ctrlp;
	int ret;

	ret = pvr2_hdw_get_tuner_status(hdw, &vt);
	if (ret != 0)
		return ret;
	ctrlp = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_INPUT);
	ret = pvr2_ctrl_get_value(ctrlp, &cur_input);
	if (ret != 0)
		return ret;
	if (vf->type == V4L2_TUNER_RADIO) {
		if (cur_input != PVR2_CVAL_INPUT_RADIO)
			pvr2_ctrl_set_value(ctrlp, PVR2_CVAL_INPUT_RADIO);
	} else {
		if (cur_input == PVR2_CVAL_INPUT_RADIO)
			pvr2_ctrl_set_value(ctrlp, PVR2_CVAL_INPUT_TV);
	}
	fv = vf->frequency;
	if (vt.capability & V4L2_TUNER_CAP_LOW)
		fv = (fv * 125) / 2;
	else
		fv = fv * 62500;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw,PVR2_CID_FREQUENCY),fv);
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_g_frequency(struct file *file, void *priv, struct v4l2_frequency *vf)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int val = 0;
	int cur_input;
	struct v4l2_tuner vt;
	int ret;

	ret = pvr2_hdw_get_tuner_status(hdw, &vt);
	if (ret != 0)
		return ret;
	ret = pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_FREQUENCY),
			&val);
	if (ret != 0)
		return ret;
	pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_INPUT),
			&cur_input);
	if (cur_input == PVR2_CVAL_INPUT_RADIO)
		vf->type = V4L2_TUNER_RADIO;
	else
		vf->type = V4L2_TUNER_ANALOG_TV;
	if (vt.capability & V4L2_TUNER_CAP_LOW)
		val = (val * 2) / 125;
	else
		val /= 62500;
	vf->frequency = val;
	return 0;
}

static int pvr2_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *fd)
{
	/* Only one format is supported: MPEG. */
	if (fd->index)
		return -EINVAL;

	fd->pixelformat = V4L2_PIX_FMT_MPEG;
	return 0;
}

static int pvr2_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *vf)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int val;

	memcpy(vf, &pvr_format[PVR_FORMAT_PIX], sizeof(struct v4l2_format));
	val = 0;
	pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_HRES),
			&val);
	vf->fmt.pix.width = val;
	val = 0;
	pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_VRES),
			&val);
	vf->fmt.pix.height = val;
	return 0;
}

static int pvr2_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *vf)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int lmin, lmax, ldef;
	struct pvr2_ctrl *hcp, *vcp;
	int h = vf->fmt.pix.height;
	int w = vf->fmt.pix.width;

	hcp = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_HRES);
	vcp = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_VRES);

	lmin = pvr2_ctrl_get_min(hcp);
	lmax = pvr2_ctrl_get_max(hcp);
	pvr2_ctrl_get_def(hcp, &ldef);
	if (w == -1)
		w = ldef;
	else if (w < lmin)
		w = lmin;
	else if (w > lmax)
		w = lmax;
	lmin = pvr2_ctrl_get_min(vcp);
	lmax = pvr2_ctrl_get_max(vcp);
	pvr2_ctrl_get_def(vcp, &ldef);
	if (h == -1)
		h = ldef;
	else if (h < lmin)
		h = lmin;
	else if (h > lmax)
		h = lmax;

	memcpy(vf, &pvr_format[PVR_FORMAT_PIX],
			sizeof(struct v4l2_format));
	vf->fmt.pix.width = w;
	vf->fmt.pix.height = h;
	return 0;
}

static int pvr2_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *vf)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct pvr2_ctrl *hcp, *vcp;
	int ret = pvr2_try_fmt_vid_cap(file, fh, vf);

	if (ret)
		return ret;
	hcp = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_HRES);
	vcp = pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_VRES);
	pvr2_ctrl_set_value(hcp, vf->fmt.pix.width);
	pvr2_ctrl_set_value(vcp, vf->fmt.pix.height);
	pvr2_hdw_commit_ctl(hdw);
	return 0;
}

static int pvr2_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct pvr2_v4l2_dev *pdi = fh->pdi;
	int ret;

	if (!fh->pdi->stream) {
		/* No stream defined for this node.  This means
		   that we're not currently allowed to stream from
		   this node. */
		return -EPERM;
	}
	ret = pvr2_hdw_set_stream_type(hdw, pdi->config);
	if (ret < 0)
		return ret;
	return pvr2_hdw_set_streaming(hdw, !0);
}

static int pvr2_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;

	if (!fh->pdi->stream) {
		/* No stream defined for this node.  This means
		   that we're not currently allowed to stream from
		   this node. */
		return -EPERM;
	}
	return pvr2_hdw_set_streaming(hdw, 0);
}

static int pvr2_queryctrl(struct file *file, void *priv,
		struct v4l2_queryctrl *vc)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct pvr2_ctrl *cptr;
	int val;

	if (vc->id & V4L2_CTRL_FLAG_NEXT_CTRL) {
		cptr = pvr2_hdw_get_ctrl_nextv4l(
				hdw, (vc->id & ~V4L2_CTRL_FLAG_NEXT_CTRL));
		if (cptr)
			vc->id = pvr2_ctrl_get_v4lid(cptr);
	} else {
		cptr = pvr2_hdw_get_ctrl_v4l(hdw, vc->id);
	}
	if (!cptr) {
		pvr2_trace(PVR2_TRACE_V4LIOCTL,
				"QUERYCTRL id=0x%x not implemented here",
				vc->id);
		return -EINVAL;
	}

	pvr2_trace(PVR2_TRACE_V4LIOCTL,
			"QUERYCTRL id=0x%x mapping name=%s (%s)",
			vc->id, pvr2_ctrl_get_name(cptr),
			pvr2_ctrl_get_desc(cptr));
	strscpy(vc->name, pvr2_ctrl_get_desc(cptr), sizeof(vc->name));
	vc->flags = pvr2_ctrl_get_v4lflags(cptr);
	pvr2_ctrl_get_def(cptr, &val);
	vc->default_value = val;
	switch (pvr2_ctrl_get_type(cptr)) {
	case pvr2_ctl_enum:
		vc->type = V4L2_CTRL_TYPE_MENU;
		vc->minimum = 0;
		vc->maximum = pvr2_ctrl_get_cnt(cptr) - 1;
		vc->step = 1;
		break;
	case pvr2_ctl_bool:
		vc->type = V4L2_CTRL_TYPE_BOOLEAN;
		vc->minimum = 0;
		vc->maximum = 1;
		vc->step = 1;
		break;
	case pvr2_ctl_int:
		vc->type = V4L2_CTRL_TYPE_INTEGER;
		vc->minimum = pvr2_ctrl_get_min(cptr);
		vc->maximum = pvr2_ctrl_get_max(cptr);
		vc->step = 1;
		break;
	default:
		pvr2_trace(PVR2_TRACE_V4LIOCTL,
				"QUERYCTRL id=0x%x name=%s not mappable",
				vc->id, pvr2_ctrl_get_name(cptr));
		return -EINVAL;
	}
	return 0;
}

static int pvr2_querymenu(struct file *file, void *priv, struct v4l2_querymenu *vm)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	unsigned int cnt = 0;
	int ret;

	ret = pvr2_ctrl_get_valname(pvr2_hdw_get_ctrl_v4l(hdw, vm->id),
			vm->index,
			vm->name, sizeof(vm->name) - 1,
			&cnt);
	vm->name[cnt] = 0;
	return ret;
}

static int pvr2_g_ctrl(struct file *file, void *priv, struct v4l2_control *vc)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int val = 0;
	int ret;

	ret = pvr2_ctrl_get_value(pvr2_hdw_get_ctrl_v4l(hdw, vc->id),
			&val);
	vc->value = val;
	return ret;
}

static int pvr2_s_ctrl(struct file *file, void *priv, struct v4l2_control *vc)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int ret;

	ret = pvr2_ctrl_set_value(pvr2_hdw_get_ctrl_v4l(hdw, vc->id),
			vc->value);
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_g_ext_ctrls(struct file *file, void *priv,
					struct v4l2_ext_controls *ctls)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct v4l2_ext_control *ctrl;
	struct pvr2_ctrl *cptr;
	unsigned int idx;
	int val;
	int ret;

	ret = 0;
	for (idx = 0; idx < ctls->count; idx++) {
		ctrl = ctls->controls + idx;
		cptr = pvr2_hdw_get_ctrl_v4l(hdw, ctrl->id);
		if (cptr) {
			if (ctls->which == V4L2_CTRL_WHICH_DEF_VAL)
				pvr2_ctrl_get_def(cptr, &val);
			else
				ret = pvr2_ctrl_get_value(cptr, &val);
		} else
			ret = -EINVAL;

		if (ret) {
			ctls->error_idx = idx;
			return ret;
		}
		/* Ensure that if read as a 64 bit value, the user
		   will still get a hopefully sane value */
		ctrl->value64 = 0;
		ctrl->value = val;
	}
	return 0;
}

static int pvr2_s_ext_ctrls(struct file *file, void *priv,
		struct v4l2_ext_controls *ctls)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct v4l2_ext_control *ctrl;
	unsigned int idx;
	int ret;

	/* Default value cannot be changed */
	if (ctls->which == V4L2_CTRL_WHICH_DEF_VAL)
		return -EINVAL;

	ret = 0;
	for (idx = 0; idx < ctls->count; idx++) {
		ctrl = ctls->controls + idx;
		ret = pvr2_ctrl_set_value(
				pvr2_hdw_get_ctrl_v4l(hdw, ctrl->id),
				ctrl->value);
		if (ret) {
			ctls->error_idx = idx;
			goto commit;
		}
	}
commit:
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_try_ext_ctrls(struct file *file, void *priv,
		struct v4l2_ext_controls *ctls)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct v4l2_ext_control *ctrl;
	struct pvr2_ctrl *pctl;
	unsigned int idx;

	/* For the moment just validate that the requested control
	   actually exists. */
	for (idx = 0; idx < ctls->count; idx++) {
		ctrl = ctls->controls + idx;
		pctl = pvr2_hdw_get_ctrl_v4l(hdw, ctrl->id);
		if (!pctl) {
			ctls->error_idx = idx;
			return -EINVAL;
		}
	}
	return 0;
}

static int pvr2_g_pixelaspect(struct file *file, void *priv,
			      int type, struct v4l2_fract *f)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct v4l2_cropcap cap = { .type = type };
	int ret;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	ret = pvr2_hdw_get_cropcap(hdw, &cap);
	if (!ret)
		*f = cap.pixelaspect;
	return ret;
}

static int pvr2_g_selection(struct file *file, void *priv,
			    struct v4l2_selection *sel)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	struct v4l2_cropcap cap;
	int val = 0;
	int ret;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		ret = pvr2_ctrl_get_value(
			  pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPL), &val);
		if (ret != 0)
			return -EINVAL;
		sel->r.left = val;
		ret = pvr2_ctrl_get_value(
			  pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPT), &val);
		if (ret != 0)
			return -EINVAL;
		sel->r.top = val;
		ret = pvr2_ctrl_get_value(
			  pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPW), &val);
		if (ret != 0)
			return -EINVAL;
		sel->r.width = val;
		ret = pvr2_ctrl_get_value(
			  pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPH), &val);
		if (ret != 0)
			return -EINVAL;
		sel->r.height = val;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		ret = pvr2_hdw_get_cropcap(hdw, &cap);
		sel->r = cap.defrect;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		ret = pvr2_hdw_get_cropcap(hdw, &cap);
		sel->r = cap.bounds;
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int pvr2_s_selection(struct file *file, void *priv,
			    struct v4l2_selection *sel)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
	int ret;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	    sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPL),
			sel->r.left);
	if (ret != 0)
		goto commit;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPT),
			sel->r.top);
	if (ret != 0)
		goto commit;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPW),
			sel->r.width);
	if (ret != 0)
		goto commit;
	ret = pvr2_ctrl_set_value(
			pvr2_hdw_get_ctrl_by_id(hdw, PVR2_CID_CROPH),
			sel->r.height);
commit:
	pvr2_hdw_commit_ctl(hdw);
	return ret;
}

static int pvr2_log_status(struct file *file, void *priv)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;

	pvr2_hdw_trigger_module_log(hdw);
	return 0;
}

static const struct v4l2_ioctl_ops pvr2_ioctl_ops = {
	.vidioc_querycap		    = pvr2_querycap,
	.vidioc_s_audio			    = pvr2_s_audio,
	.vidioc_g_audio			    = pvr2_g_audio,
	.vidioc_enumaudio		    = pvr2_enumaudio,
	.vidioc_enum_input		    = pvr2_enum_input,
	.vidioc_g_pixelaspect		    = pvr2_g_pixelaspect,
	.vidioc_s_selection		    = pvr2_s_selection,
	.vidioc_g_selection		    = pvr2_g_selection,
	.vidioc_g_input			    = pvr2_g_input,
	.vidioc_s_input			    = pvr2_s_input,
	.vidioc_g_frequency		    = pvr2_g_frequency,
	.vidioc_s_frequency		    = pvr2_s_frequency,
	.vidioc_s_tuner			    = pvr2_s_tuner,
	.vidioc_g_tuner			    = pvr2_g_tuner,
	.vidioc_g_std			    = pvr2_g_std,
	.vidioc_s_std			    = pvr2_s_std,
	.vidioc_querystd		    = pvr2_querystd,
	.vidioc_log_status		    = pvr2_log_status,
	.vidioc_enum_fmt_vid_cap	    = pvr2_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		    = pvr2_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		    = pvr2_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		    = pvr2_try_fmt_vid_cap,
	.vidioc_streamon		    = pvr2_streamon,
	.vidioc_streamoff		    = pvr2_streamoff,
	.vidioc_queryctrl		    = pvr2_queryctrl,
	.vidioc_querymenu		    = pvr2_querymenu,
	.vidioc_g_ctrl			    = pvr2_g_ctrl,
	.vidioc_s_ctrl			    = pvr2_s_ctrl,
	.vidioc_g_ext_ctrls		    = pvr2_g_ext_ctrls,
	.vidioc_s_ext_ctrls		    = pvr2_s_ext_ctrls,
	.vidioc_try_ext_ctrls		    = pvr2_try_ext_ctrls,
};

static void pvr2_v4l2_dev_destroy(struct pvr2_v4l2_dev *dip)
{
	struct pvr2_hdw *hdw = dip->v4lp->channel.mc_head->hdw;
	enum pvr2_config cfg = dip->config;
	char msg[80];
	unsigned int mcnt;

	/* Construct the unregistration message *before* we actually
	   perform the unregistration step.  By doing it this way we don't
	   have to worry about potentially touching deleted resources. */
	mcnt = scnprintf(msg, sizeof(msg) - 1,
			 "pvrusb2: unregistered device %s [%s]",
			 video_device_node_name(&dip->devbase),
			 pvr2_config_get_name(cfg));
	msg[mcnt] = 0;

	pvr2_hdw_v4l_store_minor_number(hdw,dip->minor_type,-1);

	/* Paranoia */
	dip->v4lp = NULL;
	dip->stream = NULL;

	/* Actual deallocation happens later when all internal references
	   are gone. */
	video_unregister_device(&dip->devbase);

	pr_info("%s\n", msg);

}


static void pvr2_v4l2_dev_disassociate_parent(struct pvr2_v4l2_dev *dip)
{
	if (!dip) return;
	if (!dip->devbase.v4l2_dev->dev) return;
	dip->devbase.v4l2_dev->dev = NULL;
	device_move(&dip->devbase.dev, NULL, DPM_ORDER_NONE);
}


static void pvr2_v4l2_destroy_no_lock(struct pvr2_v4l2 *vp)
{
	if (vp->dev_video) {
		pvr2_v4l2_dev_destroy(vp->dev_video);
		vp->dev_video = NULL;
	}
	if (vp->dev_radio) {
		pvr2_v4l2_dev_destroy(vp->dev_radio);
		vp->dev_radio = NULL;
	}

	pvr2_trace(PVR2_TRACE_STRUCT,"Destroying pvr2_v4l2 id=%p",vp);
	pvr2_channel_done(&vp->channel);
	kfree(vp);
}


static void pvr2_video_device_release(struct video_device *vdev)
{
	struct pvr2_v4l2_dev *dev;
	dev = container_of(vdev,struct pvr2_v4l2_dev,devbase);
	kfree(dev);
}


static void pvr2_v4l2_internal_check(struct pvr2_channel *chp)
{
	struct pvr2_v4l2 *vp;
	vp = container_of(chp,struct pvr2_v4l2,channel);
	if (!vp->channel.mc_head->disconnect_flag) return;
	pvr2_v4l2_dev_disassociate_parent(vp->dev_video);
	pvr2_v4l2_dev_disassociate_parent(vp->dev_radio);
	if (!list_empty(&vp->dev_video->devbase.fh_list) ||
	    !list_empty(&vp->dev_radio->devbase.fh_list))
		return;
	pvr2_v4l2_destroy_no_lock(vp);
}


static int pvr2_v4l2_release(struct file *file)
{
	struct pvr2_v4l2_fh *fhp = file->private_data;
	struct pvr2_v4l2 *vp = fhp->pdi->v4lp;
	struct pvr2_hdw *hdw = fhp->channel.mc_head->hdw;

	pvr2_trace(PVR2_TRACE_OPEN_CLOSE,"pvr2_v4l2_release");

	if (fhp->rhp) {
		struct pvr2_stream *sp;
		pvr2_hdw_set_streaming(hdw,0);
		sp = pvr2_ioread_get_stream(fhp->rhp);
		if (sp) pvr2_stream_set_callback(sp,NULL,NULL);
		pvr2_ioread_destroy(fhp->rhp);
		fhp->rhp = NULL;
	}

	v4l2_fh_del(&fhp->fh);
	v4l2_fh_exit(&fhp->fh);
	file->private_data = NULL;

	pvr2_channel_done(&fhp->channel);
	pvr2_trace(PVR2_TRACE_STRUCT,
		   "Destroying pvr_v4l2_fh id=%p",fhp);
	if (fhp->input_map) {
		kfree(fhp->input_map);
		fhp->input_map = NULL;
	}
	kfree(fhp);
	if (vp->channel.mc_head->disconnect_flag &&
	    list_empty(&vp->dev_video->devbase.fh_list) &&
	    list_empty(&vp->dev_radio->devbase.fh_list)) {
		pvr2_v4l2_destroy_no_lock(vp);
	}
	return 0;
}


static int pvr2_v4l2_open(struct file *file)
{
	struct pvr2_v4l2_dev *dip; /* Our own context pointer */
	struct pvr2_v4l2_fh *fhp;
	struct pvr2_v4l2 *vp;
	struct pvr2_hdw *hdw;
	unsigned int input_mask = 0;
	unsigned int input_cnt,idx;
	int ret = 0;

	dip = container_of(video_devdata(file),struct pvr2_v4l2_dev,devbase);

	vp = dip->v4lp;
	hdw = vp->channel.hdw;

	pvr2_trace(PVR2_TRACE_OPEN_CLOSE,"pvr2_v4l2_open");

	if (!pvr2_hdw_dev_ok(hdw)) {
		pvr2_trace(PVR2_TRACE_OPEN_CLOSE,
			   "pvr2_v4l2_open: hardware not ready");
		return -EIO;
	}

	fhp = kzalloc(sizeof(*fhp),GFP_KERNEL);
	if (!fhp) {
		return -ENOMEM;
	}

	v4l2_fh_init(&fhp->fh, &dip->devbase);
	init_waitqueue_head(&fhp->wait_data);
	fhp->pdi = dip;

	pvr2_trace(PVR2_TRACE_STRUCT,"Creating pvr_v4l2_fh id=%p",fhp);
	pvr2_channel_init(&fhp->channel,vp->channel.mc_head);

	if (dip->v4l_type == VFL_TYPE_RADIO) {
		/* Opening device as a radio, legal input selection subset
		   is just the radio. */
		input_mask = (1 << PVR2_CVAL_INPUT_RADIO);
	} else {
		/* Opening the main V4L device, legal input selection
		   subset includes all analog inputs. */
		input_mask = ((1 << PVR2_CVAL_INPUT_RADIO) |
			      (1 << PVR2_CVAL_INPUT_TV) |
			      (1 << PVR2_CVAL_INPUT_COMPOSITE) |
			      (1 << PVR2_CVAL_INPUT_SVIDEO));
	}
	ret = pvr2_channel_limit_inputs(&fhp->channel,input_mask);
	if (ret) {
		pvr2_channel_done(&fhp->channel);
		pvr2_trace(PVR2_TRACE_STRUCT,
			   "Destroying pvr_v4l2_fh id=%p (input mask error)",
			   fhp);
		v4l2_fh_exit(&fhp->fh);
		kfree(fhp);
		return ret;
	}

	input_mask &= pvr2_hdw_get_input_available(hdw);
	input_cnt = 0;
	for (idx = 0; idx < (sizeof(input_mask) << 3); idx++) {
		if (input_mask & (1 << idx)) input_cnt++;
	}
	fhp->input_cnt = input_cnt;
	fhp->input_map = kzalloc(input_cnt,GFP_KERNEL);
	if (!fhp->input_map) {
		pvr2_channel_done(&fhp->channel);
		pvr2_trace(PVR2_TRACE_STRUCT,
			   "Destroying pvr_v4l2_fh id=%p (input map failure)",
			   fhp);
		v4l2_fh_exit(&fhp->fh);
		kfree(fhp);
		return -ENOMEM;
	}
	input_cnt = 0;
	for (idx = 0; idx < (sizeof(input_mask) << 3); idx++) {
		if (!(input_mask & (1 << idx))) continue;
		fhp->input_map[input_cnt++] = idx;
	}

	fhp->file = file;
	file->private_data = fhp;

	fhp->fw_mode_flag = pvr2_hdw_cpufw_get_enabled(hdw);
	v4l2_fh_add(&fhp->fh);

	return 0;
}


static void pvr2_v4l2_notify(struct pvr2_v4l2_fh *fhp)
{
	wake_up(&fhp->wait_data);
}

static int pvr2_v4l2_iosetup(struct pvr2_v4l2_fh *fh)
{
	int ret;
	struct pvr2_stream *sp;
	struct pvr2_hdw *hdw;
	if (fh->rhp) return 0;

	if (!fh->pdi->stream) {
		/* No stream defined for this node.  This means that we're
		   not currently allowed to stream from this node. */
		return -EPERM;
	}

	/* First read() attempt.  Try to claim the stream and start
	   it... */
	if ((ret = pvr2_channel_claim_stream(&fh->channel,
					     fh->pdi->stream)) != 0) {
		/* Someone else must already have it */
		return ret;
	}

	fh->rhp = pvr2_channel_create_mpeg_stream(fh->pdi->stream);
	if (!fh->rhp) {
		pvr2_channel_claim_stream(&fh->channel,NULL);
		return -ENOMEM;
	}

	hdw = fh->channel.mc_head->hdw;
	sp = fh->pdi->stream->stream;
	pvr2_stream_set_callback(sp,(pvr2_stream_callback)pvr2_v4l2_notify,fh);
	pvr2_hdw_set_stream_type(hdw,fh->pdi->config);
	if ((ret = pvr2_hdw_set_streaming(hdw,!0)) < 0) return ret;
	return pvr2_ioread_set_enabled(fh->rhp,!0);
}


static ssize_t pvr2_v4l2_read(struct file *file,
			      char __user *buff, size_t count, loff_t *ppos)
{
	struct pvr2_v4l2_fh *fh = file->private_data;
	int ret;

	if (fh->fw_mode_flag) {
		struct pvr2_hdw *hdw = fh->channel.mc_head->hdw;
		char *tbuf;
		int c1,c2;
		int tcnt = 0;
		unsigned int offs = *ppos;

		tbuf = kmalloc(PAGE_SIZE,GFP_KERNEL);
		if (!tbuf) return -ENOMEM;

		while (count) {
			c1 = count;
			if (c1 > PAGE_SIZE) c1 = PAGE_SIZE;
			c2 = pvr2_hdw_cpufw_get(hdw,offs,tbuf,c1);
			if (c2 < 0) {
				tcnt = c2;
				break;
			}
			if (!c2) break;
			if (copy_to_user(buff,tbuf,c2)) {
				tcnt = -EFAULT;
				break;
			}
			offs += c2;
			tcnt += c2;
			buff += c2;
			count -= c2;
			*ppos += c2;
		}
		kfree(tbuf);
		return tcnt;
	}

	if (!fh->rhp) {
		ret = pvr2_v4l2_iosetup(fh);
		if (ret) {
			return ret;
		}
	}

	for (;;) {
		ret = pvr2_ioread_read(fh->rhp,buff,count);
		if (ret >= 0) break;
		if (ret != -EAGAIN) break;
		if (file->f_flags & O_NONBLOCK) break;
		/* Doing blocking I/O.  Wait here. */
		ret = wait_event_interruptible(
			fh->wait_data,
			pvr2_ioread_avail(fh->rhp) >= 0);
		if (ret < 0) break;
	}

	return ret;
}


static __poll_t pvr2_v4l2_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;
	struct pvr2_v4l2_fh *fh = file->private_data;
	int ret;

	if (fh->fw_mode_flag) {
		mask |= EPOLLIN | EPOLLRDNORM;
		return mask;
	}

	if (!fh->rhp) {
		ret = pvr2_v4l2_iosetup(fh);
		if (ret) return EPOLLERR;
	}

	poll_wait(file,&fh->wait_data,wait);

	if (pvr2_ioread_avail(fh->rhp) >= 0) {
		mask |= EPOLLIN | EPOLLRDNORM;
	}

	return mask;
}


static const struct v4l2_file_operations vdev_fops = {
	.owner      = THIS_MODULE,
	.open       = pvr2_v4l2_open,
	.release    = pvr2_v4l2_release,
	.read       = pvr2_v4l2_read,
	.unlocked_ioctl = video_ioctl2,
	.poll       = pvr2_v4l2_poll,
};


static const struct video_device vdev_template = {
	.fops       = &vdev_fops,
};


static void pvr2_v4l2_dev_init(struct pvr2_v4l2_dev *dip,
			       struct pvr2_v4l2 *vp,
			       int v4l_type)
{
	int mindevnum;
	int unit_number;
	struct pvr2_hdw *hdw;
	int *nr_ptr = NULL;
	dip->v4lp = vp;

	hdw = vp->channel.mc_head->hdw;
	dip->v4l_type = v4l_type;
	switch (v4l_type) {
	case VFL_TYPE_GRABBER:
		dip->stream = &vp->channel.mc_head->video_stream;
		dip->config = pvr2_config_mpeg;
		dip->minor_type = pvr2_v4l_type_video;
		nr_ptr = video_nr;
		if (!dip->stream) {
			pr_err(KBUILD_MODNAME
				": Failed to set up pvrusb2 v4l video dev due to missing stream instance\n");
			return;
		}
		break;
	case VFL_TYPE_VBI:
		dip->config = pvr2_config_vbi;
		dip->minor_type = pvr2_v4l_type_vbi;
		nr_ptr = vbi_nr;
		break;
	case VFL_TYPE_RADIO:
		dip->stream = &vp->channel.mc_head->video_stream;
		dip->config = pvr2_config_mpeg;
		dip->minor_type = pvr2_v4l_type_radio;
		nr_ptr = radio_nr;
		break;
	default:
		/* Bail out (this should be impossible) */
		pr_err(KBUILD_MODNAME ": Failed to set up pvrusb2 v4l dev due to unrecognized config\n");
		return;
	}

	dip->devbase = vdev_template;
	dip->devbase.release = pvr2_video_device_release;
	dip->devbase.ioctl_ops = &pvr2_ioctl_ops;
	{
		int val;
		pvr2_ctrl_get_value(
			pvr2_hdw_get_ctrl_by_id(hdw,
						PVR2_CID_STDAVAIL), &val);
		dip->devbase.tvnorms = (v4l2_std_id)val;
	}

	mindevnum = -1;
	unit_number = pvr2_hdw_get_unit_number(hdw);
	if (nr_ptr && (unit_number >= 0) && (unit_number < PVR_NUM)) {
		mindevnum = nr_ptr[unit_number];
	}
	pvr2_hdw_set_v4l2_dev(hdw, &dip->devbase);
	if ((video_register_device(&dip->devbase,
				   dip->v4l_type, mindevnum) < 0) &&
	    (video_register_device(&dip->devbase,
				   dip->v4l_type, -1) < 0)) {
		pr_err(KBUILD_MODNAME
			": Failed to register pvrusb2 v4l device\n");
	}

	pr_info("pvrusb2: registered device %s [%s]\n",
	       video_device_node_name(&dip->devbase),
	       pvr2_config_get_name(dip->config));

	pvr2_hdw_v4l_store_minor_number(hdw,
					dip->minor_type,dip->devbase.minor);
}


struct pvr2_v4l2 *pvr2_v4l2_create(struct pvr2_context *mnp)
{
	struct pvr2_v4l2 *vp;

	vp = kzalloc(sizeof(*vp),GFP_KERNEL);
	if (!vp) return vp;
	pvr2_channel_init(&vp->channel,mnp);
	pvr2_trace(PVR2_TRACE_STRUCT,"Creating pvr2_v4l2 id=%p",vp);

	vp->channel.check_func = pvr2_v4l2_internal_check;

	/* register streams */
	vp->dev_video = kzalloc(sizeof(*vp->dev_video),GFP_KERNEL);
	if (!vp->dev_video) goto fail;
	pvr2_v4l2_dev_init(vp->dev_video,vp,VFL_TYPE_GRABBER);
	if (pvr2_hdw_get_input_available(vp->channel.mc_head->hdw) &
	    (1 << PVR2_CVAL_INPUT_RADIO)) {
		vp->dev_radio = kzalloc(sizeof(*vp->dev_radio),GFP_KERNEL);
		if (!vp->dev_radio) goto fail;
		pvr2_v4l2_dev_init(vp->dev_radio,vp,VFL_TYPE_RADIO);
	}

	return vp;
 fail:
	pvr2_trace(PVR2_TRACE_STRUCT,"Failure creating pvr2_v4l2 id=%p",vp);
	pvr2_v4l2_destroy_no_lock(vp);
	return NULL;
}
