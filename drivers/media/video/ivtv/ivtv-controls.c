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
