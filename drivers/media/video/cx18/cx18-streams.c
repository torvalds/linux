/*
 *  cx18 init/start/stop/exit stream functions
 *
 *  Derived from ivtv-streams.c
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
#include "cx18-fileops.h"
#include "cx18-mailbox.h"
#include "cx18-i2c.h"
#include "cx18-queue.h"
#include "cx18-ioctl.h"
#include "cx18-streams.h"
#include "cx18-cards.h"
#include "cx18-scb.h"
#include "cx18-av-core.h"
#include "cx18-dvb.h"

#define CX18_DSP0_INTERRUPT_MASK     	0xd0004C

static struct file_operations cx18_v4l2_enc_fops = {
	.owner = THIS_MODULE,
	.read = cx18_v4l2_read,
	.open = cx18_v4l2_open,
	/* FIXME change to video_ioctl2 if serialization lock can be removed */
	.ioctl = cx18_v4l2_ioctl,
	.compat_ioctl = v4l_compat_ioctl32,
	.release = cx18_v4l2_close,
	.poll = cx18_v4l2_enc_poll,
};

/* offset from 0 to register ts v4l2 minors on */
#define CX18_V4L2_ENC_TS_OFFSET   16
/* offset from 0 to register pcm v4l2 minors on */
#define CX18_V4L2_ENC_PCM_OFFSET  24
/* offset from 0 to register yuv v4l2 minors on */
#define CX18_V4L2_ENC_YUV_OFFSET  32

static struct {
	const char *name;
	int vfl_type;
	int minor_offset;
	int dma;
	enum v4l2_buf_type buf_type;
	struct file_operations *fops;
} cx18_stream_info[] = {
	{	/* CX18_ENC_STREAM_TYPE_MPG */
		"encoder MPEG",
		VFL_TYPE_GRABBER, 0,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_TS */
		"TS",
		VFL_TYPE_GRABBER, -1,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_YUV */
		"encoder YUV",
		VFL_TYPE_GRABBER, CX18_V4L2_ENC_YUV_OFFSET,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_VBI */
		"encoder VBI",
		VFL_TYPE_VBI, 0,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VBI_CAPTURE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_PCM */
		"encoder PCM audio",
		VFL_TYPE_GRABBER, CX18_V4L2_ENC_PCM_OFFSET,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_PRIVATE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_IDX */
		"encoder IDX",
		VFL_TYPE_GRABBER, -1,
		PCI_DMA_FROMDEVICE, V4L2_BUF_TYPE_VIDEO_CAPTURE,
		&cx18_v4l2_enc_fops
	},
	{	/* CX18_ENC_STREAM_TYPE_RAD */
		"encoder radio",
		VFL_TYPE_RADIO, 0,
		PCI_DMA_NONE, V4L2_BUF_TYPE_PRIVATE,
		&cx18_v4l2_enc_fops
	},
};

static void cx18_stream_init(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	struct video_device *dev = s->v4l2dev;
	u32 max_size = cx->options.megabytes[type] * 1024 * 1024;

	/* we need to keep v4l2dev, so restore it afterwards */
	memset(s, 0, sizeof(*s));
	s->v4l2dev = dev;

	/* initialize cx18_stream fields */
	s->cx = cx;
	s->type = type;
	s->name = cx18_stream_info[type].name;
	s->handle = 0xffffffff;

	s->dma = cx18_stream_info[type].dma;
	s->buf_size = cx->stream_buf_size[type];
	if (s->buf_size)
		s->buffers = max_size / s->buf_size;
	if (s->buffers > 63) {
		/* Each stream has a maximum of 63 buffers,
		   ensure we do not exceed that. */
		s->buffers = 63;
		s->buf_size = (max_size / s->buffers) & ~0xfff;
	}
	spin_lock_init(&s->qlock);
	init_waitqueue_head(&s->waitq);
	s->id = -1;
	cx18_queue_init(&s->q_free);
	cx18_queue_init(&s->q_full);
	cx18_queue_init(&s->q_io);
}

static int cx18_prep_dev(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	u32 cap = cx->v4l2_cap;
	int minor_offset = cx18_stream_info[type].minor_offset;
	int minor;

	/* These four fields are always initialized. If v4l2dev == NULL, then
	   this stream is not in use. In that case no other fields but these
	   four can be used. */
	s->v4l2dev = NULL;
	s->cx = cx;
	s->type = type;
	s->name = cx18_stream_info[type].name;

	/* Check whether the radio is supported */
	if (type == CX18_ENC_STREAM_TYPE_RAD && !(cap & V4L2_CAP_RADIO))
		return 0;

	/* Check whether VBI is supported */
	if (type == CX18_ENC_STREAM_TYPE_VBI &&
	    !(cap & (V4L2_CAP_VBI_CAPTURE | V4L2_CAP_SLICED_VBI_CAPTURE)))
		return 0;

	/* card number + user defined offset + device offset */
	minor = cx->num + cx18_first_minor + minor_offset;

	/* User explicitly selected 0 buffers for these streams, so don't
	   create them. */
	if (cx18_stream_info[type].dma != PCI_DMA_NONE &&
	    cx->options.megabytes[type] == 0) {
		CX18_INFO("Disabled %s device\n", cx18_stream_info[type].name);
		return 0;
	}

	cx18_stream_init(cx, type);

	if (minor_offset == -1)
		return 0;

	/* allocate and initialize the v4l2 video device structure */
	s->v4l2dev = video_device_alloc();
	if (s->v4l2dev == NULL) {
		CX18_ERR("Couldn't allocate v4l2 video_device for %s\n",
				s->name);
		return -ENOMEM;
	}

	snprintf(s->v4l2dev->name, sizeof(s->v4l2dev->name), "cx18-%d",
			cx->num);

	s->v4l2dev->minor = minor;
	s->v4l2dev->parent = &cx->dev->dev;
	s->v4l2dev->fops = cx18_stream_info[type].fops;
	s->v4l2dev->release = video_device_release;
	s->v4l2dev->tvnorms = V4L2_STD_ALL;
	cx18_set_funcs(s->v4l2dev);
	return 0;
}

/* Initialize v4l2 variables and register v4l2 devices */
int cx18_streams_setup(struct cx18 *cx)
{
	int type;

	/* Setup V4L2 Devices */
	for (type = 0; type < CX18_MAX_STREAMS; type++) {
		/* Prepare device */
		if (cx18_prep_dev(cx, type))
			break;

		/* Allocate Stream */
		if (cx18_stream_alloc(&cx->streams[type]))
			break;
	}
	if (type == CX18_MAX_STREAMS)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	cx18_streams_cleanup(cx, 0);
	return -ENOMEM;
}

static int cx18_reg_dev(struct cx18 *cx, int type)
{
	struct cx18_stream *s = &cx->streams[type];
	int vfl_type = cx18_stream_info[type].vfl_type;
	int minor;

	/* TODO: Shouldn't this be a VFL_TYPE_TRANSPORT or something?
	 * We need a VFL_TYPE_TS defined.
	 */
	if (strcmp("TS", s->name) == 0) {
		/* just return if no DVB is supported */
		if ((cx->card->hw_all & CX18_HW_DVB) == 0)
			return 0;
		if (cx18_dvb_register(s) < 0) {
			CX18_ERR("DVB failed to register\n");
			return -EINVAL;
		}
	}

	if (s->v4l2dev == NULL)
		return 0;

	minor = s->v4l2dev->minor;

	/* Register device. First try the desired minor, then any free one. */
	if (video_register_device(s->v4l2dev, vfl_type, minor) &&
			video_register_device(s->v4l2dev, vfl_type, -1)) {
		CX18_ERR("Couldn't register v4l2 device for %s minor %d\n",
			s->name, minor);
		video_device_release(s->v4l2dev);
		s->v4l2dev = NULL;
		return -ENOMEM;
	}
	minor = s->v4l2dev->minor;

	switch (vfl_type) {
	case VFL_TYPE_GRABBER:
		CX18_INFO("Registered device video%d for %s (%d MB)\n",
			minor, s->name, cx->options.megabytes[type]);
		break;

	case VFL_TYPE_RADIO:
		CX18_INFO("Registered device radio%d for %s\n",
			minor - MINOR_VFL_TYPE_RADIO_MIN, s->name);
		break;

	case VFL_TYPE_VBI:
		if (cx->options.megabytes[type])
			CX18_INFO("Registered device vbi%d for %s (%d MB)\n",
				minor - MINOR_VFL_TYPE_VBI_MIN,
				s->name, cx->options.megabytes[type]);
		else
			CX18_INFO("Registered device vbi%d for %s\n",
				minor - MINOR_VFL_TYPE_VBI_MIN, s->name);
		break;
	}

	return 0;
}

/* Register v4l2 devices */
int cx18_streams_register(struct cx18 *cx)
{
	int type;
	int err = 0;

	/* Register V4L2 devices */
	for (type = 0; type < CX18_MAX_STREAMS; type++)
		err |= cx18_reg_dev(cx, type);

	if (err == 0)
		return 0;

	/* One or more streams could not be initialized. Clean 'em all up. */
	cx18_streams_cleanup(cx, 1);
	return -ENOMEM;
}

/* Unregister v4l2 devices */
void cx18_streams_cleanup(struct cx18 *cx, int unregister)
{
	struct video_device *vdev;
	int type;

	/* Teardown all streams */
	for (type = 0; type < CX18_MAX_STREAMS; type++) {
		if (cx->streams[type].dvb.enabled) {
			cx18_dvb_unregister(&cx->streams[type]);
			cx->streams[type].dvb.enabled = false;
		}

		vdev = cx->streams[type].v4l2dev;

		cx->streams[type].v4l2dev = NULL;
		if (vdev == NULL)
			continue;

		cx18_stream_free(&cx->streams[type]);

		/* Unregister or release device */
		if (unregister)
			video_unregister_device(vdev);
		else
			video_device_release(vdev);
	}
}

static void cx18_vbi_setup(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	int raw = cx->vbi.sliced_in->service_set == 0;
	u32 data[CX2341X_MBOX_MAX_DATA];
	int lines;

	if (cx->is_60hz) {
		cx->vbi.count = 12;
		cx->vbi.start[0] = 10;
		cx->vbi.start[1] = 273;
	} else {        /* PAL/SECAM */
		cx->vbi.count = 18;
		cx->vbi.start[0] = 6;
		cx->vbi.start[1] = 318;
	}

	/* setup VBI registers */
	cx18_av_cmd(cx, VIDIOC_S_FMT, &cx->vbi.in);

	/* determine number of lines and total number of VBI bytes.
	   A raw line takes 1443 bytes: 2 * 720 + 4 byte frame header - 1
	   The '- 1' byte is probably an unused U or V byte. Or something...
	   A sliced line takes 51 bytes: 4 byte frame header, 4 byte internal
	   header, 42 data bytes + checksum (to be confirmed) */
	if (raw) {
		lines = cx->vbi.count * 2;
	} else {
		lines = cx->is_60hz ? 24 : 38;
		if (cx->is_60hz)
			lines += 2;
	}

	cx->vbi.enc_size = lines *
		(raw ? cx->vbi.raw_size : cx->vbi.sliced_size);

	data[0] = s->handle;
	/* Lines per field */
	data[1] = (lines / 2) | ((lines / 2) << 16);
	/* bytes per line */
	data[2] = (raw ? cx->vbi.raw_size : cx->vbi.sliced_size);
	/* Every X number of frames a VBI interrupt arrives
	   (frames as in 25 or 30 fps) */
	data[3] = 1;
	/* Setup VBI for the cx25840 digitizer */
	if (raw) {
		data[4] = 0x20602060;
		data[5] = 0x30703070;
	} else {
		data[4] = 0xB0F0B0F0;
		data[5] = 0xA0E0A0E0;
	}

	CX18_DEBUG_INFO("Setup VBI h: %d lines %x bpl %d fr %d %x %x\n",
			data[0], data[1], data[2], data[3], data[4], data[5]);

	if (s->type == CX18_ENC_STREAM_TYPE_VBI)
		cx18_api(cx, CX18_CPU_SET_RAW_VBI_PARAM, 6, data);
}

int cx18_start_v4l2_encode_stream(struct cx18_stream *s)
{
	u32 data[MAX_MB_ARGUMENTS];
	struct cx18 *cx = s->cx;
	struct list_head *p;
	int ts = 0;
	int captype = 0;

	if (s->v4l2dev == NULL && s->dvb.enabled == 0)
		return -EINVAL;

	CX18_DEBUG_INFO("Start encoder stream %s\n", s->name);

	switch (s->type) {
	case CX18_ENC_STREAM_TYPE_MPG:
		captype = CAPTURE_CHANNEL_TYPE_MPEG;
		cx->mpg_data_received = cx->vbi_data_inserted = 0;
		cx->dualwatch_jiffies = jiffies;
		cx->dualwatch_stereo_mode = cx->params.audio_properties & 0x300;
		cx->search_pack_header = 0;
		break;

	case CX18_ENC_STREAM_TYPE_TS:
		captype = CAPTURE_CHANNEL_TYPE_TS;
		ts = 1;
		break;
	case CX18_ENC_STREAM_TYPE_YUV:
		captype = CAPTURE_CHANNEL_TYPE_YUV;
		break;
	case CX18_ENC_STREAM_TYPE_PCM:
		captype = CAPTURE_CHANNEL_TYPE_PCM;
		break;
	case CX18_ENC_STREAM_TYPE_VBI:
		captype = cx->vbi.sliced_in->service_set ?
		    CAPTURE_CHANNEL_TYPE_SLICED_VBI : CAPTURE_CHANNEL_TYPE_VBI;
		cx->vbi.frame = 0;
		cx->vbi.inserted_frame = 0;
		memset(cx->vbi.sliced_mpeg_size,
			0, sizeof(cx->vbi.sliced_mpeg_size));
		break;
	default:
		return -EINVAL;
	}
	s->buffers_stolen = 0;

	/* mute/unmute video */
	cx18_vapi(cx, CX18_CPU_SET_VIDEO_MUTE, 2,
		  s->handle, !!test_bit(CX18_F_I_RADIO_USER, &cx->i_flags));

	/* Clear Streamoff flags in case left from last capture */
	clear_bit(CX18_F_S_STREAMOFF, &s->s_flags);

	cx18_vapi_result(cx, data, CX18_CREATE_TASK, 1, CPU_CMD_MASK_CAPTURE);
	s->handle = data[0];
	cx18_vapi(cx, CX18_CPU_SET_CHANNEL_TYPE, 2, s->handle, captype);

	if (atomic_read(&cx->ana_capturing) == 0 && !ts) {
		/* Stuff from Windows, we don't know what it is */
		cx18_vapi(cx, CX18_CPU_SET_VER_CROP_LINE, 2, s->handle, 0);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 3, 1);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 8, 0);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 3, s->handle, 4, 1);
		cx18_vapi(cx, CX18_CPU_SET_MISC_PARAMETERS, 2, s->handle, 12);

		cx18_vapi(cx, CX18_CPU_SET_CAPTURE_LINE_NO, 3,
			       s->handle, cx->digitizer, cx->digitizer);

		/* Setup VBI */
		if (cx->v4l2_cap & V4L2_CAP_VBI_CAPTURE)
			cx18_vbi_setup(s);

		/* assign program index info.
		   Mask 7: select I/P/B, Num_req: 400 max */
		cx18_vapi_result(cx, data, CX18_CPU_SET_INDEXTABLE, 1, 0);

		/* Setup API for Stream */
		cx2341x_update(cx, cx18_api_func, NULL, &cx->params);
	}

	if (atomic_read(&cx->tot_capturing) == 0) {
		clear_bit(CX18_F_I_EOS, &cx->i_flags);
		write_reg(7, CX18_DSP0_INTERRUPT_MASK);
	}

	cx18_vapi(cx, CX18_CPU_DE_SET_MDL_ACK, 3, s->handle,
		(void __iomem *)&cx->scb->cpu_mdl_ack[s->type][0] - cx->enc_mem,
		(void __iomem *)&cx->scb->cpu_mdl_ack[s->type][1] - cx->enc_mem);

	list_for_each(p, &s->q_free.list) {
		struct cx18_buffer *buf = list_entry(p, struct cx18_buffer, list);

		writel(buf->dma_handle, &cx->scb->cpu_mdl[buf->id].paddr);
		writel(s->buf_size, &cx->scb->cpu_mdl[buf->id].length);
		cx18_vapi(cx, CX18_CPU_DE_SET_MDL, 5, s->handle,
			(void __iomem *)&cx->scb->cpu_mdl[buf->id] - cx->enc_mem,
			1, buf->id, s->buf_size);
	}
	/* begin_capture */
	if (cx18_vapi(cx, CX18_CPU_CAPTURE_START, 1, s->handle)) {
		CX18_DEBUG_WARN("Error starting capture!\n");
		cx18_vapi(cx, CX18_DESTROY_TASK, 1, s->handle);
		return -EINVAL;
	}

	/* you're live! sit back and await interrupts :) */
	if (!ts)
		atomic_inc(&cx->ana_capturing);
	atomic_inc(&cx->tot_capturing);
	return 0;
}

void cx18_stop_all_captures(struct cx18 *cx)
{
	int i;

	for (i = CX18_MAX_STREAMS - 1; i >= 0; i--) {
		struct cx18_stream *s = &cx->streams[i];

		if (s->v4l2dev == NULL && s->dvb.enabled == 0)
			continue;
		if (test_bit(CX18_F_S_STREAMING, &s->s_flags))
			cx18_stop_v4l2_encode_stream(s, 0);
	}
}

int cx18_stop_v4l2_encode_stream(struct cx18_stream *s, int gop_end)
{
	struct cx18 *cx = s->cx;
	unsigned long then;

	if (s->v4l2dev == NULL && s->dvb.enabled == 0)
		return -EINVAL;

	/* This function assumes that you are allowed to stop the capture
	   and that we are actually capturing */

	CX18_DEBUG_INFO("Stop Capture\n");

	if (atomic_read(&cx->tot_capturing) == 0)
		return 0;

	if (s->type == CX18_ENC_STREAM_TYPE_MPG)
		cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 2, s->handle, !gop_end);
	else
		cx18_vapi(cx, CX18_CPU_CAPTURE_STOP, 1, s->handle);

	then = jiffies;

	if (s->type == CX18_ENC_STREAM_TYPE_MPG && gop_end) {
		CX18_INFO("ignoring gop_end: not (yet?) supported by the firmware\n");
	}

	if (s->type != CX18_ENC_STREAM_TYPE_TS)
		atomic_dec(&cx->ana_capturing);
	atomic_dec(&cx->tot_capturing);

	/* Clear capture and no-read bits */
	clear_bit(CX18_F_S_STREAMING, &s->s_flags);

	cx18_vapi(cx, CX18_DESTROY_TASK, 1, s->handle);
	s->handle = 0xffffffff;

	if (atomic_read(&cx->tot_capturing) > 0)
		return 0;

	write_reg(5, CX18_DSP0_INTERRUPT_MASK);
	wake_up(&s->waitq);

	return 0;
}

u32 cx18_find_handle(struct cx18 *cx)
{
	int i;

	/* find first available handle to be used for global settings */
	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		struct cx18_stream *s = &cx->streams[i];

		if (s->v4l2dev && s->handle)
			return s->handle;
	}
	return 0;
}
