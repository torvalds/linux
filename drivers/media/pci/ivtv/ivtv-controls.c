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
#include "ivtv-ioctl.h"
#include "ivtv-controls.h"
#include "ivtv-mailbox.h"

static int ivtv_s_stream_vbi_fmt(struct cx2341x_handler *cxhdl, u32 fmt)
{
	struct ivtv *itv = container_of(cxhdl, struct ivtv, cxhdl);

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
	if (ivtv_get_service_set(itv->vbi.sliced_in) == 0) {
		if (itv->is_60hz)
			itv->vbi.sliced_in->service_set = V4L2_SLICED_CAPTION_525;
		else
			itv->vbi.sliced_in->service_set = V4L2_SLICED_WSS_625;
		ivtv_expand_service_set(itv->vbi.sliced_in, itv->is_50hz);
	}
	return 0;
}

static int ivtv_s_video_encoding(struct cx2341x_handler *cxhdl, u32 val)
{
	struct ivtv *itv = container_of(cxhdl, struct ivtv, cxhdl);
	int is_mpeg1 = val == V4L2_MPEG_VIDEO_ENCODING_MPEG_1;
	struct v4l2_mbus_framefmt fmt;

	/* fix videodecoder resolution */
	fmt.width = cxhdl->width / (is_mpeg1 ? 2 : 1);
	fmt.height = cxhdl->height;
	fmt.code = V4L2_MBUS_FMT_FIXED;
	v4l2_subdev_call(itv->sd_video, video, s_mbus_fmt, &fmt);
	return 0;
}

static int ivtv_s_audio_sampling_freq(struct cx2341x_handler *cxhdl, u32 idx)
{
	static const u32 freqs[3] = { 44100, 48000, 32000 };
	struct ivtv *itv = container_of(cxhdl, struct ivtv, cxhdl);

	/* The audio clock of the digitizer must match the codec sample
	   rate otherwise you get some very strange effects. */
	if (idx < ARRAY_SIZE(freqs))
		ivtv_call_all(itv, audio, s_clock_freq, freqs[idx]);
	return 0;
}

static int ivtv_s_audio_mode(struct cx2341x_handler *cxhdl, u32 val)
{
	struct ivtv *itv = container_of(cxhdl, struct ivtv, cxhdl);

	itv->dualwatch_stereo_mode = val;
	return 0;
}

struct cx2341x_handler_ops ivtv_cxhdl_ops = {
	.s_audio_mode = ivtv_s_audio_mode,
	.s_audio_sampling_freq = ivtv_s_audio_sampling_freq,
	.s_video_encoding = ivtv_s_video_encoding,
	.s_stream_vbi_fmt = ivtv_s_stream_vbi_fmt,
};

int ivtv_g_pts_frame(struct ivtv *itv, s64 *pts, s64 *frame)
{
	u32 data[CX2341X_MBOX_MAX_DATA];

	if (test_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags)) {
		*pts = (s64)((u64)itv->last_dec_timing[2] << 32) |
			(u64)itv->last_dec_timing[1];
		*frame = itv->last_dec_timing[0];
		return 0;
	}
	*pts = 0;
	*frame = 0;
	if (atomic_read(&itv->decoding)) {
		if (ivtv_api(itv, CX2341X_DEC_GET_TIMING_INFO, 5, data)) {
			IVTV_DEBUG_WARN("GET_TIMING: couldn't read clock\n");
			return -EIO;
		}
		memcpy(itv->last_dec_timing, data, sizeof(itv->last_dec_timing));
		set_bit(IVTV_F_I_VALID_DEC_TIMINGS, &itv->i_flags);
		*pts = (s64)((u64) data[2] << 32) | (u64) data[1];
		*frame = data[0];
		/*timing->scr = (u64) (((u64) data[4] << 32) | (u64) (data[3]));*/
	}
	return 0;
}

static int ivtv_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ivtv *itv = container_of(ctrl->handler, struct ivtv, cxhdl.hdl);

	switch (ctrl->id) {
	/* V4L2_CID_MPEG_VIDEO_DEC_PTS and V4L2_CID_MPEG_VIDEO_DEC_FRAME
	   control cluster */
	case V4L2_CID_MPEG_VIDEO_DEC_PTS:
		return ivtv_g_pts_frame(itv, itv->ctrl_pts->p_new.p_s64,
					     itv->ctrl_frame->p_new.p_s64);
	}
	return 0;
}

static int ivtv_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ivtv *itv = container_of(ctrl->handler, struct ivtv, cxhdl.hdl);

	switch (ctrl->id) {
	/* V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK and MULTILINGUAL_PLAYBACK
	   control cluster */
	case V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK:
		itv->audio_stereo_mode = itv->ctrl_audio_playback->val - 1;
		itv->audio_bilingual_mode = itv->ctrl_audio_multilingual_playback->val - 1;
		ivtv_vapi(itv, CX2341X_DEC_SET_AUDIO_MODE, 2, itv->audio_bilingual_mode, itv->audio_stereo_mode);
		break;
	}
	return 0;
}

const struct v4l2_ctrl_ops ivtv_hdl_out_ops = {
	.s_ctrl = ivtv_s_ctrl,
	.g_volatile_ctrl = ivtv_g_volatile_ctrl,
};
